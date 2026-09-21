// =================================================================
// src/core/meters/SliceMeterPump.cpp  (NereusSDR)
// =================================================================
//
// Ported from Thetis source:
//   Project Files/Source/Console/MeterManager.cs, original licence from Thetis source is included below
//
// See SliceMeterPump.h for the full design comment. This is the same
// per-slice polling logic src/gui/meters/MeterPoller.cpp's
// pollSliceSMeters() ran, plus pollSMeter()'s rxMode()-driven source
// selection (Remote-daemon R2 Task 12 step 7), relocated to src/core/ and
// adapted to write SliceModel::signalStrengthDbm directly instead of
// emitting a signal MainWindow had to route by hand.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-06 -- Remote-daemon R2 Task 12: extracted from
//                 src/gui/meters/MeterPoller.cpp's pollSliceSMeters() /
//                 pollSMeter() into a core-side, RadioModel-owned QTimer.
//                 J.J. Boyd (KG4VCF), with AI-assisted transformation via
//                 Anthropic Claude Code.
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

#include "core/meters/SliceMeterPump.h"

#include "core/AppSettings.h"
#include "core/RadioStatus.h"
#include "core/RxChannel.h"
#include "core/WdspEngine.h"
#include "core/WdspTypes.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <algorithm>

namespace NereusSDR {

SliceMeterPump::SliceMeterPump(RadioModel* radioModel, QObject* parent)
    : QObject(parent)
    , m_radioModel(radioModel)
{
    // Seed the interval from the persisted Multimeter delay so a headless
    // daemon -- which never constructs MultimeterPage, ever -- still
    // honours the operator's last-chosen cadence from the very first
    // tick, rather than a hardcoded default the delay slider cannot
    // reach. Same key MultimeterPage.cpp's loadSettings()/connectSignals()
    // read/write, and the same AppSettings-at-construction pattern
    // SliceModel::setupRadeIdleClearTimer already uses for RadeIdleClearMs.
    auto& s = AppSettings::instance();
    const int ms = s.value(QStringLiteral("MultimeterDelayMs"), 100).toInt();
    m_timer.setInterval(std::clamp(ms, 10, 2000));
    connect(&m_timer, &QTimer::timeout, this, &SliceMeterPump::poll);
}

SliceMeterPump::~SliceMeterPump() = default;

void SliceMeterPump::setSourceSelector(std::function<MeterSource()> selector)
{
    m_sourceSelector = std::move(selector);
}

// Matches MeterPoller::setIntervalMs's clamp (MeterPoller.cpp) against the
// same MultimeterPage spinbox range.
void SliceMeterPump::setIntervalMs(int ms)
{
    m_timer.setInterval(std::clamp(ms, 10, 2000));
}

int SliceMeterPump::intervalMs() const
{
    return m_timer.interval();
}

void SliceMeterPump::start()
{
    m_timer.start();
}

void SliceMeterPump::stop()
{
    m_timer.stop();
}

void SliceMeterPump::poll()
{
    if (!m_radioModel) { return; }

    // MOX gate. RadioStatus::isTransmitting() covers MOX asserted by ANY
    // PTT source, unlike MeterPoller's own m_inTx, which is fed only by
    // MoxController::moxStateChanged (MeterPoller.cpp's setInTx doc
    // comment) -- the narrower of the two. Matches the headless precedent
    // at TciServer.cpp's TX-sensor broadcast timer, which gates the same
    // way for the same reason. While transmitting this touches NO slice at
    // all, leaving every S-meter reading exactly where the last RX-mode poll
    // left it.
    if (m_radioModel->radioStatus().isTransmitting()) {
        return;
    }

    WdspEngine* engine = m_radioModel->wdspEngine();
    if (!engine) { return; }

    // rxMode()-driven source selection (Task 12 step 7). Unset (every
    // headless-daemon construction, and any test that never calls
    // setSourceSelector) defaults to SignalAverage, matching the
    // pre-Task-12 pollSliceSMeters()'s fixed choice exactly.
    const MeterSource source =
        m_sourceSelector ? m_sourceSelector() : MeterSource::SignalAverage;

    // From Thetis console.cs:21040 [v2.10.3.13] -- RXOffset = RXPreampOffset
    // + RXCalibrationOffset. Same cumulative cal term MeterPoller.cpp's
    // poll()/pollSMeter() apply, read once per tick here too.
    const double rxOffsetDb = m_radioModel->rxMeterOffsetDb();

    // MaxBin is a single global reading (FFTEngine display channel 0 --
    // single-panadapter assumption; see WdspEngine::getMaxBinDbm's own doc
    // comment), so it is computed once per tick rather than once per
    // slice -- every slice would read the identical value in MaxBin mode
    // regardless. Matches MeterPoller::pollSMeter()'s MaxBin branch:
    //
    // From Thetis console.cs:46881 [v2.10.3.13]:
    //   if (max_bin > -400f)
    //       _RX1MeterValues[Reading.SIGNAL_MAX_BIN] = max_bin + offset;
    // (else the -400 "not active" sentinel passes through unchanged).
    double maxBinDbm = -400.0;
    if (source == MeterSource::MaxBin) {
        const double raw = engine->getMaxBinDbm(/*disp=*/0);
        maxBinDbm = (raw > -400.0) ? (raw + rxOffsetDb) : raw;
    }

    for (SliceModel* slice : m_radioModel->slices()) {
        if (!slice) { continue; }

        // Slice id doubles as the WDSP RX channel id (the invariant
        // Sub-Epic I establishes), so sliceIndex() indexes rxChannel()
        // directly -- the same fact the now-removed MeterPoller::
        // setSliceChannels() relied on for the mechanism this replaces.
        RxChannel* ch = engine->rxChannel(slice->sliceIndex());
        if (!ch) {
            // No channel yet (not connected, or this slice has not been
            // bound to hardware) -- leave all readings at whatever they
            // already hold (their constructed -140.0 defaults for a slice
            // that has never had a channel at all) rather than writing a
            // sentinel of this pump's own invention.
            continue;
        }

        // Read and publish both source readings on every RX tick.  The
        // selected signalStrengthDbm below remains the legacy analog-meter
        // view; publishing the sources independently lets a remote GUI make
        // that selection from station telemetry without a local WDSP read.
        // From Thetis Console/dsp.cs:954 [@501e3f5] (CalculateRXMeter):
        //   case MeterType.SIGNAL_STRENGTH: val = GetRXAMeter(channel, RXA_S_PK);
        // The adjacent ADC_REAL case at dsp.cs:959 carries //MW0LGE [2.9.0.7]
        // attribution that we preserve verbatim per GPL inline-tag rule.
        // Display-side offset per console.cs:46824 [v2.10.3.13].
        const double signalPeakDbm =
            ch->getMeter(RxMeterType::SignalPeak) + rxOffsetDb;

        // From Thetis Console/dsp.cs:957 [@501e3f5] (CalculateRXMeter):
        //   case MeterType.AVG_SIGNAL_STRENGTH: val = GetRXAMeter(channel, RXA_S_AV);
        // The adjacent ADC_REAL case at dsp.cs:959 carries //MW0LGE [2.9.0.7]
        // attribution that we preserve verbatim per GPL inline-tag rule.
        // Display-side offset per console.cs:46828 [v2.10.3.13].
        const double signalAverageDbm =
            ch->getMeter(RxMeterType::SignalAvg) + rxOffsetDb;

        double dbm = -140.0;
        switch (source) {
        case MeterSource::SignalPeak:
            // From Thetis Console/dsp.cs:954 [@501e3f5] (CalculateRXMeter):
            //   case MeterType.SIGNAL_STRENGTH: val = GetRXAMeter(channel, RXA_S_PK);
            // The adjacent ADC_REAL case at dsp.cs:959 carries //MW0LGE [2.9.0.7]
            // attribution that we preserve verbatim per GPL inline-tag rule
            // (same preservation MeterPoller.cpp's own pollSMeter() carries
            // for this identical citation).
            // Display-side offset per console.cs:46824 [v2.10.3.13].
            dbm = signalPeakDbm;
            break;
        case MeterSource::SignalAverage:
            // From Thetis Console/dsp.cs:957 [@501e3f5] (CalculateRXMeter):
            //   case MeterType.AVG_SIGNAL_STRENGTH: val = GetRXAMeter(channel, RXA_S_AV);
            // The adjacent ADC_REAL case at dsp.cs:959 carries //MW0LGE [2.9.0.7]
            // attribution that we preserve verbatim per GPL inline-tag rule
            // (same preservation MeterPoller.cpp's own pollSMeter() carries
            // for this identical citation).
            // Display-side offset per console.cs:46828 [v2.10.3.13].
            dbm = signalAverageDbm;
            break;
        case MeterSource::MaxBin:
            dbm = maxBinDbm;
            break;
        }
        slice->setSignalStrengthDbm(dbm);
        slice->setSignalPeakDbm(signalPeakDbm);
        slice->setSignalAverageDbm(signalAverageDbm);
    }
}

} // namespace NereusSDR
