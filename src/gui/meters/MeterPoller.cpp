// =================================================================
// src/gui/meters/MeterPoller.cpp  (NereusSDR)
// =================================================================
//
// Ported from Thetis source:
//   Project Files/Source/Console/MeterManager.cs, original licence from Thetis source is included below
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-17 — Reimplemented in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via Anthropic
//                 Claude Code.
//   2026-04-26 — Phase 3M-1a H.2: TX meter bindings.  setTxChannel(),
//                 setInTx(bool) slot, pollTxMeters() helper.
//                 Cite: Thetis dsp.cs:999-1050 [v2.10.3.13] CalculateTXMeter.
//   2026-05-01 — Task 3.1: setIntervalMs/intervalMs + setAverageWindow/averageWindow
//                 accessors for MultimeterPage live wire-up.
//                 Corresponds to Thetis udDisplayMeterDelay + udDisplayMeterAvg
//                 (display.cs) [v2.10.3.13].
//   2026-08-06: Remote Daemon R2 Task 12: pollSliceSMeters() / setSliceChannels()
//                 / sliceSmeterUpdated / m_sliceChannels extracted to the new
//                 core-side src/core/meters/SliceMeterPump.{h,cpp}, so a
//                 headless nereusd (which never links this GUI-only class)
//                 also produces the per-slice S-meter reading. smeterUpdated
//                 removed with its only listener (MainWindow.cpp:8132);
//                 pollSMeter() itself is unchanged. J.J. Boyd (KG4VCF), with
//                 AI-assisted transformation via Anthropic Claude Code.
//   2026-09-22: R-R3-13: pollRemoteRxMeters() feeds the -400 dBm no-reading
//                 sentinel to the S-meter and the flags in every RX mode when
//                 there is no reading (disconnected, snapshot not ready, no
//                 slice). J.J. Boyd (KG4VCF), with AI-assisted transformation
//                 via Anthropic Claude Code.
//   2026-09-23: R-R3-13: the container meter items learned the no-reading
//                 rule (isNoMeterReading), so pollRemoteRxMeters() now feeds
//                 the -400 dBm sentinel to SignalPeak / SignalAvg with no
//                 reading (was the -140 floor), and poll() feeds it to the
//                 RX bindings once the local RX channel is gone instead of
//                 leaving them frozen. J.J. Boyd (KG4VCF), with AI-assisted
//                 transformation via Anthropic Claude Code.
//   2026-09-24: R-R3-21: the Compression reading (TxComp) takes Thetis's
//                 -30 floor, max(-30, TXA_COMP_AV) (console.cs:46979 with
//                 dsp.cs:1056 [v2.10.3.15]); PROC off reads -30, not -400.
//                 J.J. Boyd (KG4VCF), with AI-assisted transformation via
//                 Anthropic Claude Code.
//   2026-09-24: R-R3-39: the RX readings (RxChannel::getMeter and
//                 WdspEngine::getRxaSignalPeak) come from the cache the
//                 receive lane refreshes, so this GUI-thread poll makes no
//                 RX WDSP call. J.J. Boyd (KG4VCF), with AI-assisted
//                 implementation via Anthropic Claude Code.
//   2026-09-25: R-R3-39 (station Task 32): the TX readings too
//               (TxChannel::txMeter, the transmit lane's last reading).
//               J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

/*  MeterManager.cs

This file is part of a program that implements a Software-Defined Radio.

This code/file can be found on GitHub : https://github.com/ramdor/Thetis

Copyright (C) 2020-2026 Richard Samphire MW0LGE

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

The author can be reached by email at

mw0lge@grange-lane.co.uk
*/
//
//============================================================================================//
// Dual-Licensing Statement (Applies Only to Author's Contributions, Richard Samphire MW0LGE) //
// ------------------------------------------------------------------------------------------ //
// For any code originally written by Richard Samphire MW0LGE, or for any modifications       //
// made by him, the copyright holder for those portions (Richard Samphire) reserves the       //
// right to use, license, and distribute such code under different terms, including           //
// closed-source and proprietary licences, in addition to the GNU General Public License      //
// granted above. Nothing in this statement restricts any rights granted to recipients under  //
// the GNU GPL. Code contributed by others (not Richard Samphire) remains licensed under      //
// its original terms and is not affected by this dual-licensing statement in any way.        //
// Richard Samphire can be reached by email at :  mw0lge@grange-lane.co.uk                    //
//============================================================================================//

#include "MeterPoller.h"
#include "MeterWidget.h"

#include <algorithm>  // std::clamp (Task 3.1 setIntervalMs / setAverageWindow)
#include "MeterItem.h"
#include "core/RxChannel.h"
#include "core/TxChannel.h"
#include "core/RadioStatus.h"
#include "core/LogCategories.h"
#include "core/mmio/ExternalVariableEngine.h"
#include "core/mmio/MmioEndpoint.h"
// Task 41 (Phase 3P-II): SMeterWidget + WdspEngine for the pollSMeter() path.
#include "gui/SMeterWidget.h"
#include "core/WdspEngine.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <cmath>

// WDSP GetTXAMeter — lock-free TX meter read.
// From Thetis dsp.cs:390-391 [v2.10.3.13]:
//   [DllImport("wdsp.dll")] extern double GetTXAMeter(int channel, txaMeterType meter);
// NereusSDR declaration in src/core/wdsp_api.h.
#ifdef HAVE_WDSP
#include "core/wdsp_api.h"
#endif

namespace NereusSDR {

MeterPoller::MeterPoller(QObject* parent)
    : QObject(parent)
{
    m_timer.setInterval(100);  // 10 fps default (from Thetis UpdateInterval=100ms)
    connect(&m_timer, &QTimer::timeout, this, &MeterPoller::poll);
}

MeterPoller::~MeterPoller() = default;

void MeterPoller::setRxChannel(RxChannel* channel)
{
    m_rxChannel = channel;
    qCDebug(lcMeter) << "MeterPoller: RxChannel set, channelId:"
                      << (channel ? channel->channelId() : -1);
}

void MeterPoller::setLocalRxReadingAvailable(bool available)
{
    m_localRxReadingAvailable = available;
}

// H.2 (Phase 3M-1a): store non-owning pointer to the TX channel.
// WdspEngine owns the object; call setTxChannel(nullptr) on radio disconnect.
void MeterPoller::setTxChannel(TxChannel* channel)
{
    m_txChannel = channel;
    qCDebug(lcMeter) << "MeterPoller: TxChannel set, channelId:"
                      << (channel ? channel->channelId() : -1);
}

// Task 41 (Phase 3P-II): store non-owning pointer to the analog SMeterWidget.
// Call with nullptr on panel destruction (QPointer auto-clears on widget delete).
void MeterPoller::setSMeter(SMeterWidget* widget)
{
    m_sMeter = widget;
    qCDebug(lcMeter) << "MeterPoller: SMeterWidget set:" << (widget ? "yes" : "nullptr");
}

// Task 41 (Phase 3P-II): store non-owning pointer to WdspEngine for getMaxBinDbm.
// RadioModel owns the engine; call with nullptr on disconnect if needed.
void MeterPoller::setWdspEngine(WdspEngine* engine)
{
    m_wdspEngine = engine;
    qCDebug(lcMeter) << "MeterPoller: WdspEngine set:" << (engine ? "yes" : "nullptr");
}

void MeterPoller::setRemoteRadioModel(RadioModel* model,
                                     std::function<bool()> snapshotReady,
                                     std::function<double(const SliceModel*)> maxBinSource)
{
    m_remoteRole = model && model->role() == RadioModel::Role::Remote;
    m_remoteModel = m_remoteRole ? model : nullptr;
    m_remoteSnapshotReady = std::move(snapshotReady);
    m_remoteMaxBinSource = std::move(maxBinSource);
}

// RX meter cal offset source (Thetis-faithful port).
//
// Stores a std::function returning the current dB offset.  Called once per
// pollSMeter() and the SignalPeak/SignalAvg loop in poll() to add the
// cumulative RXOffset(rx) value to WDSP-sourced readings before display.
//
// Thetis equivalent: console.cs:46821 [v2.10.3.13]:
//   float offset = RXOffset(1);
// where RXOffset = RXPreampOffset + RXCalibrationOffset.
//
// Set by MainWindow once during RadioModel wiring; refreshed at every poll
// tick (the callable is cheap; see RadioModel::rxMeterOffsetDb).
void MeterPoller::setRxOffsetSource(std::function<double()> source)
{
    m_rxOffsetSource = std::move(source);
    qCDebug(lcMeter) << "MeterPoller: rxOffsetSource set:"
                      << (m_rxOffsetSource ? "yes" : "nullptr");
}

// H.2 (Phase 3M-1a): switch poll set on MOX engage/release.
// Porting from Thetis dsp.cs:995-1050 [v2.10.3.13] CalculateTXMeter dispatch:
//   the switch on MeterType selects TX vs RX meter reads.
// NereusSDR translates the dispatch to a bool flag set at MOX boundary.
void MeterPoller::setInTx(bool isTx)
{
    if (m_inTx == isTx) {
        return;  // idempotent
    }
    m_inTx = isTx;
    qCDebug(lcMeter) << "MeterPoller: TX mode" << (isTx ? "on" : "off");

    // Bench-reported #167 follow-up: on MOX falling edge, snap all TX-meter
    // BarItems to 0 to bypass the per-bar attack/decay smoothing.  Without
    // this, a single zero update from RadioStatus::powerChanged after MOX-off
    // only walks the smoothed value down by one decay tick (e.g. 60 → 54 at
    // decayRatio=0.1), so Power / SWR / ALC bars appear stuck at the last
    // sample for several seconds.  TextItem readouts already update via
    // their own (non-smoothed) path so the numeric "0 W" / "1.0:1" labels
    // were already correct; this fixes the visual-bar half of the meter
    // ensemble.
    if (!m_inTx) {
        for (auto& guarded : m_targets) {
            MeterWidget* target = guarded.data();
            if (!target) { continue; }
            for (MeterItem* item : target->items()) {
                if (!item) { continue; }
                BarItem* bar = qobject_cast<BarItem*>(item);
                if (!bar) { continue; }
                const int bid = bar->bindingId();
                // Snap every TX-domain binding (100..199 range) — the
                // 200+ hardware-telemetry bindings (HwVolts / HwAmps /
                // HwTemperature) are NOT TX-gated; their last reading is
                // still meaningful post-key.
                if (bid >= MeterBinding::TxPower && bid < MeterBinding::HwVolts) {
                    bar->clearSmoothing(0.0);
                }
            }
            target->update();   // schedule repaint
        }
    }
}

void MeterPoller::addTarget(MeterWidget* widget)
{
    if (!widget) { return; }
    // Drop any stale entries whose widgets have been destroyed, then add
    // only if not already present.
    m_targets.removeAll(QPointer<MeterWidget>(nullptr));
    for (const auto& p : m_targets) {
        if (p.data() == widget) { return; }
    }
    m_targets.append(QPointer<MeterWidget>(widget));
}

void MeterPoller::removeTarget(MeterWidget* widget)
{
    m_targets.removeAll(QPointer<MeterWidget>(widget));
    m_targets.removeAll(QPointer<MeterWidget>(nullptr));
}

// ── Task 3.1: MultimeterPage-facing interval API ─────────────────────────────
// setIntervalMs / intervalMs: preferred interface for MultimeterPage.
// Clamps to [10..2000] matching the MultimeterPage spinbox range so a
// spurious 0 from a default-constructed AppSettings value can't stall the
// timer.  Corresponds to Thetis udDisplayMeterDelay (display.cs) [v2.10.3.13].
void MeterPoller::setIntervalMs(int ms)
{
    m_timer.setInterval(std::clamp(ms, 10, 2000));
}

int MeterPoller::intervalMs() const
{
    return m_timer.interval();
}

// setAverageWindow: store the averaging window size; full dispatch in Task 3.2.
// Clamps to [1..32] matching MultimeterPage spinbox range.
// From Thetis udDisplayMeterAvg (display.cs) [v2.10.3.13].
void MeterPoller::setAverageWindow(int n)
{
    m_avgWindow = std::clamp(n, 1, 32);
}

int MeterPoller::averageWindow() const
{
    return m_avgWindow;
}

// ── Legacy interval API (backward-compat, delegates to setIntervalMs) ────────
void MeterPoller::setInterval(int ms)
{
    m_timer.setInterval(ms);
}

int MeterPoller::interval() const
{
    return m_timer.interval();
}

void MeterPoller::start()
{
    // Phase 3G-6 block 5: poll runs regardless of m_rxChannel so
    // MMIO-bound items update even before a radio is connected.
    m_timer.start();
    qCDebug(lcMeter) << "MeterPoller: started at" << m_timer.interval() << "ms";
}

void MeterPoller::stop()
{
    m_timer.stop();
    qCDebug(lcMeter) << "MeterPoller: stopped";
}

void MeterPoller::poll()
{
    // Phase 3G-6 block 5: MMIO item polling is independent of the
    // RX channel, so this branch runs even when m_rxChannel is
    // unset (e.g. no radio connected). For every target widget,
    // walk its items and push the latest value from the bound
    // endpoint's variable cache into each item with an MMIO
    // binding.
    auto& engine = ExternalVariableEngine::instance();
    for (auto& guarded : m_targets) {
        MeterWidget* target = guarded.data();
        if (!target) { continue; }
        for (MeterItem* item : target->items()) {
            if (!item || !item->hasMmioBinding()) { continue; }
            MmioEndpoint* ep = engine.endpoint(item->mmioGuid());
            if (!ep) { continue; }
            const QVariant v = ep->valueForName(item->mmioVariable());
            if (!v.isValid()) { continue; }
            bool ok = false;
            const double d = v.toDouble(&ok);
            if (!ok) { continue; }
            item->setValue(d);
        }
        target->update();
    }

    // R3: a remote window must never fall through to the inactive local
    // DSP, including after its model has been destroyed or disconnected.
    if (m_remoteRole) {
        pollRemoteRxMeters();
        return;
    }

    // H.2 (Phase 3M-1a): when MOX is active, switch to TX meter polling.
    // From Thetis dsp.cs:995-1050 [v2.10.3.13] CalculateTXMeter — the switch
    // on MeterType dispatches TX vs RX reads from the same timer tick.
    if (m_inTx) {
        pollTxMeters();
        return;  // don't poll RX meters while transmitting
    }

    // NereusSDR (R-R3-13): with no RX channel (never created, or destroyed:
    // the QPointer clears) there is no reading, and with the radio link not
    // up (LinkLost keeps the channels alive, but their meters stop and an
    // inactive one reads -140 dBm) there is none either.  Feed the -400 dBm
    // sentinel to the RX bindings this loop drives and to the analog
    // S-meter header, so each shows "--" rather than the last value.
    // MeterWidget::updateMeterValue and SMeterWidget::setLevel drop repeats.
    if (!m_rxChannel || !m_localRxReadingAvailable) {
        for (int bindingId = MeterBinding::SignalPeak;
             bindingId <= MeterBinding::AgcAvg; ++bindingId) {
            for (auto& guarded : m_targets) {
                MeterWidget* target = guarded.data();
                if (!target) { continue; }
                target->updateMeterValue(bindingId, kNoMeterReadingDbm);
            }
        }
        if (SMeterWidget* sm = m_sMeter.data()) {
            sm->setLevel(static_cast<float>(kNoMeterReadingDbm));
        }
        return;
    }

    // Thetis-faithful RX meter cal offset for the SignalPeak / SignalAvg
    // bindings (RXA_S_PK / RXA_S_AV).  ADC_PK / ADC_AV / AGC_PK / AGC_AV /
    // AGC_GAIN bindings do NOT take the offset (Thetis console.cs:46831-
    // 46835 [v2.10.3.13] omit +offset for those exact rows.
    // Cite: console.cs:46821 -> float offset = RXOffset(1);
    //       console.cs:46824 -> ... = CalculateRXMeter(...) + offset;  // SIGNAL_STRENGTH
    //       console.cs:46828 -> ... = CalculateRXMeter(...) + offset;  // AVG_SIGNAL_STRENGTH
    const double rxOffsetDb = m_rxOffsetSource ? m_rxOffsetSource() : 0.0;

    // Poll all RX meter types. R-R3-39: RxChannel::getMeter reads the cache
    // the receive lane refreshes (GetRXAMeter takes a meter lock, so it
    // never runs on this, the GUI, thread); with no lane it reads WDSP.
    double smeterDbm = -140.0;
    for (int bindingId = MeterBinding::SignalPeak;
         bindingId <= MeterBinding::AgcAvg; ++bindingId) {
        double value = m_rxChannel->getMeter(static_cast<RxMeterType>(bindingId));
        // Apply RXOffset to SignalPeak / SignalAvg only (matches Thetis
        // console.cs:46824 + :46828, NOT :46831-:46835).
        if (bindingId == MeterBinding::SignalPeak
         || bindingId == MeterBinding::SignalAvg) {
            value += rxOffsetDb;
        }
        if (bindingId == MeterBinding::SignalAvg) {
            smeterDbm = value;   // post-offset; matches VfoWidget expectation
        }
        for (auto& guarded : m_targets) {
            MeterWidget* target = guarded.data();
            if (!target) { continue; }
            target->updateMeterValue(bindingId, value);
        }
    }

    // Task 41 (Phase 3P-II): drive the analog SMeterWidget header.
    //
    // Remote Daemon R2 Task 12: the per-slice pass that used to run here
    // (pollSliceSMeters(), emitting sliceSmeterUpdated for every flag) has
    // moved to SliceMeterPump (src/core/meters/), a separate core-side
    // QTimer owned by RadioModel -- GUI-only, this poller could never run
    // for a headless nereusd, so the per-slice S-meter reading a remote
    // GUI's mirror carries had no producer there. Every flag now listens
    // to its own SliceModel::signalStrengthDbmChanged directly
    // (MainWindow.cpp's createSliceFlag) instead of a signal from this
    // class. pollSMeter() below is unchanged: it still drives the analog
    // SMeterWidget header, and MainWindow wires the SAME rxMode()
    // selector into SliceMeterPump so the flags and the analog needle
    // never disagree on source.
    Q_UNUSED(smeterDbm);
    pollSMeter();
}

void MeterPoller::pollRemoteRxMeters()
{
    // The station owns calibration and RX meter production. The GUI only
    // chooses a reading and applies the existing widget ballistics.
    const bool ready = m_remoteModel && m_remoteModel->isConnected()
        && m_remoteSnapshotReady && m_remoteSnapshotReady();
    SliceModel* slice = ready ? m_remoteModel->activeSlice() : nullptr;
    if (slice && (m_inTx || m_remoteModel->radioStatus().isTransmitting()
                        || m_remoteModel->transmitModel().isMox())) {
        return;
    }
    auto finiteOr = [](double value, double fallback) {
        return std::isfinite(value) ? value : fallback;
    };
    // NereusSDR (R-R3-13): with no reading (disconnected, snapshot not
    // ready, no slice) the S-meter, the flags and the container meter
    // items (SignalPeak / SignalAvg / SignalMaxBin) all get the -400 dBm
    // no-reading sentinel in every RX mode, which each shows as "--".  The
    // -140 floor below applies only while a slice reading exists.
    constexpr double kNoReadingDbm = kNoMeterReadingDbm;
    const double peak = slice ? finiteOr(slice->signalPeakDbm(), -140.0) : kNoReadingDbm;
    const double average = slice ? finiteOr(slice->signalAverageDbm(), -140.0) : kNoReadingDbm;
    const double maxBin = slice && m_remoteMaxBinSource
        ? finiteOr(m_remoteMaxBinSource(slice), -400.0) : -400.0;
    for (const auto& guarded : m_targets) {
        MeterWidget* target = guarded.data();
        if (!target) { continue; }
        target->updateMeterValue(MeterBinding::SignalPeak, peak);
        target->updateMeterValue(MeterBinding::SignalAvg, average);
        target->updateMeterValue(MeterBinding::SignalMaxBin, maxBin);
    }
    if (!m_sMeter) { return; }
    double level = slice ? peak : kNoReadingDbm;
    switch (m_sMeter->rxMode()) {
    case SMeterWidget::RxMode::SMeter:
    case SMeterWidget::RxMode::SMeterPeak:
        break;
    case SMeterWidget::RxMode::SignalAverage:
        level = slice ? average : kNoReadingDbm;
        break;
    case SMeterWidget::RxMode::MaxBin:
        level = maxBin;
        break;
    }
    m_sMeter->setLevel(static_cast<float>(level));

    // Keep each flag on the same selected meter source as the applet,
    // resolving stable slice IDs on every tick rather than caching channels.
    if (!m_remoteModel) { return; }
    for (const SliceModel* flagSlice : m_remoteModel->slices()) {
        if (!flagSlice) { continue; }
        double flagLevel = kNoReadingDbm;
        if (ready) {
            switch (m_sMeter->rxMode()) {
            case SMeterWidget::RxMode::SMeter:
            case SMeterWidget::RxMode::SMeterPeak:
                flagLevel = flagSlice->signalPeakDbm();
                break;
            case SMeterWidget::RxMode::SignalAverage:
                flagLevel = flagSlice->signalAverageDbm();
                break;
            case SMeterWidget::RxMode::MaxBin:
                flagLevel = m_remoteMaxBinSource ? m_remoteMaxBinSource(flagSlice) : -400.0;
                break;
            }
        }
        emit remoteSliceLevelUpdated(flagSlice->sliceIndex(),
                                     ready ? finiteOr(flagLevel, -140.0) : kNoReadingDbm);
    }
}

void MeterPoller::pollSMeter()
{
    SMeterWidget* sm = m_sMeter.data();
    if (!sm) { return; }
    if (!m_rxChannel) { return; }

    const int ch = m_rxChannel->channelId();

    // Thetis-faithful RX meter cal offset (Thetis-faithful port).
    // Applied to ALL three RxMode branches (SIGNAL_STRENGTH,
    // AVG_SIGNAL_STRENGTH, SIGNAL_MAX_BIN) to match Thetis console.cs:
    //   :46824 (SIGNAL_STRENGTH)      ... + offset
    //   :46828 (AVG_SIGNAL_STRENGTH)  ... + offset
    //   :46881 (SIGNAL_MAX_BIN)       ... + offset
    // RXOffset = RXPreampOffset + RXCalibrationOffset (console.cs:21040).
    const float rxOffsetDb = m_rxOffsetSource
        ? static_cast<float>(m_rxOffsetSource())
        : 0.0f;

    float dbm = -127.0f;
    switch (sm->rxMode()) {
    case SMeterWidget::RxMode::SMeter:
    case SMeterWidget::RxMode::SMeterPeak:
        // From Thetis Console/dsp.cs:954 [@501e3f5] (CalculateRXMeter):
        //   case MeterType.SIGNAL_STRENGTH: val = GetRXAMeter(channel, RXA_S_PK);
        // The adjacent ADC_REAL case at dsp.cs:959 carries //MW0LGE [2.9.0.7]
        // attribution that we preserve verbatim per GPL inline-tag rule.
        // Display-side offset add per console.cs:46824 [v2.10.3.13]:
        //   _RX1MeterValues[Reading.SIGNAL_STRENGTH] = ... + offset;
        if (m_wdspEngine) {
            dbm = static_cast<float>(m_wdspEngine->getRxaSignalPeak(ch)) + rxOffsetDb;
        } else {
            // Fallback via RxChannel wrapper (RXA_S_PK = RxMeterType::SignalPeak = 0).
            dbm = static_cast<float>(m_rxChannel->getMeter(RxMeterType::SignalPeak)) + rxOffsetDb;
        }
        break;
    case SMeterWidget::RxMode::SignalAverage:
        // From Thetis Console/dsp.cs:957 [@501e3f5] (CalculateRXMeter):
        //   case MeterType.AVG_SIGNAL_STRENGTH: val = GetRXAMeter(channel, RXA_S_AV);
        // The adjacent ADC_REAL case at dsp.cs:959 carries //MW0LGE [2.9.0.7]
        // attribution that we preserve verbatim per GPL inline-tag rule.
        // Display-side offset add per console.cs:46828 [v2.10.3.13]:
        //   _RX1MeterValues[Reading.AVG_SIGNAL_STRENGTH] = ... + offset;
        dbm = static_cast<float>(m_rxChannel->getMeter(RxMeterType::SignalAvg)) + rxOffsetDb;
        break;
    case SMeterWidget::RxMode::MaxBin:
        // GetDetectMaxBin(disp=0) -- single-pan display channel 0.
        // From Thetis Console/dsp.cs:849-850 [@501e3f5] (P/Invoke GetDetectMaxBin).
        // Display-side offset add per console.cs:46881 [v2.10.3.13]:
        //   if (max_bin > -400f)
        //       _RX1MeterValues[Reading.SIGNAL_MAX_BIN] = max_bin + offset;
        // NereusSDR sources MaxBin from FFTEngine via WdspEngine::getMaxBinDbm
        // (single-panadapter assumption; see WdspEngine.cpp:1327).  Same
        // logical reading as Thetis GetDetectMaxBin so the same +offset
        // applies (gates on the -400 sentinel matching Thetis).
        if (m_wdspEngine) {
            const float maxBinRaw = static_cast<float>(m_wdspEngine->getMaxBinDbm(/*disp=*/0));
            if (maxBinRaw > -400.0f) {
                dbm = maxBinRaw + rxOffsetDb;
            } else {
                dbm = maxBinRaw;  // pass through sentinel unchanged
            }
        }
        break;
    }
    sm->setLevel(dbm);
}

// Poll the four WDSP TX meters active in 3M-1a and push to meter widget targets.
//
// Porting from Thetis dsp.cs:999-1029 [v2.10.3.13] CalculateTXMeter:
//   case MeterType.TXA_OUT_PK:   val = GetTXAMeter(channel, TXA_OUT_PK);   // output peak
//   case MeterType.TXA_ALC_AV:   val = GetTXAMeter(channel, TXA_ALC_AV);   // ALC average
//   case MeterType.TXA_ALC_PK:   val = GetTXAMeter(channel, TXA_ALC_PK);   // ALC peak
//   case MeterType.TXA_ALC_GAIN: val = GetTXAMeter(channel, TXA_ALC_GAIN) + alcgain; // ALC gain
//
// 3M-1a scope: hardware PA meters (forward/reflected/SWR) are driven by the
// existing RadioStatus::powerChanged connection (setRadioStatus()), which
// is active regardless of TX/RX state. No duplication needed.
//
// Without HAVE_WDSP the reads return -140.0 (silent fallback — no WDSP channel).
void MeterPoller::pollTxMeters()
{
    if (!m_txChannel) {
        return;  // no TX channel yet (WDSP not initialized or disconnected)
    }

    const int chanId = m_txChannel->channelId();

    // Meter binding IDs → WDSP TxMeterType values.
    // From Thetis dsp.cs:999-1029 [v2.10.3.13]:
    //   TXA_OUT_PK  → TxMeterType::OutPeak  (12)
    //   TXA_ALC_PK  → TxMeterType::AlcPeak  (9)
    //   TXA_ALC_AV  → TxMeterType::AlcAvg   (10)
    //   TXA_ALC_GAIN→ TxMeterType::AlcGain  (11)
    struct TxPollEntry { int bindingId; int wdspMt; };
    static constexpr TxPollEntry kTxPollSet[] = {
        { MeterBinding::TxAlc,     static_cast<int>(TxMeterType::AlcAvg)  },   // TXA_ALC_AV  [v2.10.3.13]
        { MeterBinding::TxAlcGain, static_cast<int>(TxMeterType::AlcGain) },   // TXA_ALC_GAIN [v2.10.3.13]
        // TxPower uses the TXA_OUT_PK reading for the power bar in 3M-1a.
        // Hardware PA forward power is pushed via RadioStatus::powerChanged
        // (the existing setRadioStatus() path); this reading is the WDSP
        // TXA output peak (post-ALC, pre-PA), a different quantity.
        // Both are useful; 3M-1a populates both for completeness.
        // R-R3-21: the compression reading. It read TXA_OUT_PK, the output
        // peak, although TxComp is TXA_COMP_AV (MeterPoller.h) as in
        // Thetis: From Thetis dsp.cs:1013-1014 [v2.10.3.15]
        //   case MeterType.COMP: val = GetTXAMeter(channel, txaMeterType.TXA_COMP_AV);
        { MeterBinding::TxComp,    static_cast<int>(TxMeterType::CompAvg) },   // TXA_COMP_AV [v2.10.3.15]
    };

    for (const auto& entry : kTxPollSet) {
        double value = -140.0;
#ifdef HAVE_WDSP
        // GetTXAMeter(channel, mt) — lock-free, matches GetRXAMeter pattern.
        // From Thetis dsp.cs:390-391 [v2.10.3.13].
        // R-R3-39: TxChannel::txMeter makes that call on the transmit lane
        // and returns the lane's last reading, so this poll never waits on
        // WDSP.
        value = m_txChannel->txMeter(entry.wdspMt);
        Q_UNUSED(chanId)
#else
        Q_UNUSED(chanId)
        Q_UNUSED(entry)
#endif
        handOutTxReading(entry.bindingId, value);
    }
}

// Hands one WDSP transmit reading to the meters and to txMeterReading.
void MeterPoller::handOutTxReading(int bindingId, double value)
{
    // R-R3-21: Thetis floors the Compression reading at -30 before any
    // meter sees it (console.cs:46979 [v2.10.3.15]); PROC off (-400)
    // reads -30.
    if (bindingId == MeterBinding::TxComp) {
        value = compressionReading(value);
    }
    for (auto& guarded : m_targets) {
        MeterWidget* target = guarded.data();
        if (!target) { continue; }
        target->updateMeterValue(bindingId, value);
    }
    emit txMeterReading(bindingId, value);
}

// From Thetis console.cs:46979 [v2.10.3.15]:
//   updateMetersReading(Reading.COMP, (float)Math.Max(-30.0f, -WDSP.CalculateTXMeter(1, WDSP.MeterType.COMP)), 0);
// with dsp.cs:1013-1014 + :1056 [v2.10.3.15]: CalculateTXMeter reads
// TXA_COMP_AV and returns -(float)val, so the reading is max(-30, raw).
double MeterPoller::compressionReading(double rawTxaCompAv)
{
    if (!std::isfinite(rawTxaCompAv)) { return MeterBinding::kTxCompFloorDb; }
    return std::max(MeterBinding::kTxCompFloorDb, rawTxaCompAv);
}

void MeterPoller::setRadioStatus(RadioStatus* status)
{
    // Disconnect any previous connection before re-wiring.
    if (m_powerConn) {
        QObject::disconnect(m_powerConn);
        m_powerConn = QMetaObject::Connection{};
    }
    m_radioStatus = status;
    if (m_radioStatus) {
        // From Thetis console.cs PollPAPWR loop [v2.10.3.13]:
        // RadioStatus::powerChanged aggregates forward/reflected/swr from
        // PollPAPWR's alex_fwd / alex_rev / swr locals and emits them
        // together. Fan values out to all registered MeterWidget targets
        // via updateMeterValue() — same pattern as the RX poll() loop.
        m_powerConn = connect(
            m_radioStatus, &RadioStatus::powerChanged,
            this, [this](double fwd, double rev, double swr) {
                for (auto& guarded : m_targets) {
                    MeterWidget* target = guarded.data();
                    if (!target) { continue; }
                    target->updateMeterValue(MeterBinding::TxPower,        fwd);
                    target->updateMeterValue(MeterBinding::TxReversePower, rev);
                    target->updateMeterValue(MeterBinding::TxSwr,          swr);
                }
            });
    }
}

} // namespace NereusSDR
