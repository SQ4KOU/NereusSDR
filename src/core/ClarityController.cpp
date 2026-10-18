// =================================================================
// src/core/ClarityController.cpp  (NereusSDR)
// =================================================================
//
// Independently implemented from ClarityController.h interface.
// The .h cites Thetis display.cs:5866 (processNoiseFloor) as the
// lineage that NereusSDR's percentile-based estimator deliberately
// REPLACES, not ports. This .cpp is the percentile-based estimator —
// original NereusSDR work licensed under GPLv3.
// =================================================================

#include "ClarityController.h"

#include <QDateTime>
#include <QtNumeric>

#include <algorithm>
#include <cmath>

namespace NereusSDR {

ClarityController::ClarityController(QObject* parent)
    : QObject(parent)
{
}

void ClarityController::setEnabled(bool on)
{
    if (m_enabled == on) { return; }
    m_enabled = on;
    if (on) {
        // P1 fix: reset the emitted state so the next frame unconditionally
        // emits — otherwise an off→on cycle on a stable band leaves
        // m_clarityActive false because an unchanged floor is not re-emitted.
        m_hasEmitted  = false;
        m_hasSmoothed = false;
    }
}

void ClarityController::setTransmitting(bool tx)
{
    if (m_transmitting == tx) { return; }
    m_transmitting = tx;
    // P2 fix: emit pausedChanged so MainWindow can clear m_clarityActive
    // on TX and let legacy AGC resume. Without this, setTransmitting
    // silently suppresses feedBins but nothing tells SpectrumWidget.
    if (m_enabled) {
        emit pausedChanged(tx);
    }
    // The smoothed floor and the last emitted thresholds are kept through
    // the key. The receive floor after it is the floor before it, so the
    // un-key restores the pre-key level: it does not re-anchor on the first
    // post-key frames, which still hold transmit energy while the FFT
    // refills. The caller starts the hold on the un-key (beginHold).
    if (tx) {
        // A re-key inside the hold cancels it; the next un-key starts a
        // fresh one.
        cancelHold();
    }
}

void ClarityController::beginHold(qint64 lengthMs, qint64 nowMs)
{
    if (nowMs < 0) {
        nowMs = QDateTime::currentMSecsSinceEpoch();
    }
    // NereusSDR, based on Thetis: Thetis runs the noise-floor fast attack
    // for Math.Max(1000f, fftFillTime + ...) after it is switched on
    // (display.cs:5912 [v2.10.3.15]), so its floor chases the frames that
    // follow a key. NereusSDR rejects frames for the same length and keeps
    // the pre-key floor, because Clarity places the waterfall on that floor
    // and a chased one flashes the first receive rows. The caller computes
    // the length.
    m_holdActive = true;
    m_holdEndMs  = nowMs + std::max<qint64>(0, lengthMs);
}

void ClarityController::cancelHold()
{
    m_holdActive = false;
}

bool ClarityController::inHold(qint64 nowMs) const
{
    if (!m_holdActive) {
        return false;
    }
    if (nowMs < 0) {
        nowMs = QDateTime::currentMSecsSinceEpoch();
    }
    return nowMs < m_holdEndMs;
}

ClarityController::CarriedState ClarityController::carriedState() const
{
    CarriedState state;
    state.smoothedFloor    = m_smoothedFloor;
    state.hasSmoothed      = m_hasSmoothed;
    state.lastLow          = m_lastLow;
    state.lastHigh         = m_lastHigh;
    state.lastEmittedFloor = m_lastEmittedFloor;
    state.hasEmitted       = m_hasEmitted;
    state.lastPollMs       = m_lastPollMs;
    state.holdActive       = m_holdActive;
    state.holdEndMs        = m_holdEndMs;
    return state;
}

void ClarityController::adoptCarriedState(const CarriedState& state)
{
    m_smoothedFloor    = state.smoothedFloor;
    m_hasSmoothed      = state.hasSmoothed;
    m_lastLow          = state.lastLow;
    m_lastHigh         = state.lastHigh;
    m_lastEmittedFloor = state.lastEmittedFloor;
    m_hasEmitted       = state.hasEmitted;
    m_lastPollMs       = state.lastPollMs;
    m_holdActive       = state.holdActive;
    m_holdEndMs        = state.holdEndMs;
}

void ClarityController::notifyManualOverride()
{
    if (m_paused) {
        return;
    }
    m_paused = true;
    emit pausedChanged(true);
}

void ClarityController::retuneNow()
{
    if (m_paused) {
        m_paused = false;
        emit pausedChanged(false);
    }
    // Snap EWMA state — next frame re-anchors instead of slow-lerping
    // from the pre-override floor.
    m_hasSmoothed = false;
    m_hasEmitted  = false;
}

void ClarityController::snapToFloor(float floorDbm)
{
    if (qIsNaN(floorDbm) || !m_enabled) {
        return;
    }
    m_smoothedFloor    = floorDbm;
    m_hasSmoothed      = true;

    float low  = floorDbm + m_lowMarginDb;
    float high = floorDbm + m_highMarginDb;
    if ((high - low) < m_minGapDb) {
        const float center = (low + high) / 2.0f;
        low  = center - m_minGapDb / 2.0f;
        high = center + m_minGapDb / 2.0f;
    }

    m_lastLow          = low;
    m_lastHigh         = high;
    m_lastEmittedFloor = floorDbm;
    m_hasEmitted       = true;
    emit waterfallThresholdsChanged(low, high);
}

void ClarityController::setPollIntervalMs(int ms)      { m_pollIntervalMs = ms; }
void ClarityController::setSmoothingTauSec(float sec)  { m_tauSec = sec; }
void ClarityController::setCalibrationOffsetDb(float db) { m_calibrationOffsetDb = db; }
void ClarityController::setLowMarginDb(float db)       { m_lowMarginDb = db; }
void ClarityController::setHighMarginDb(float db)      { m_highMarginDb = db; }
void ClarityController::setMinGapDb(float db)          { m_minGapDb = db; }
void ClarityController::setPercentile(float p)         { m_estimator.setPercentile(p); }

void ClarityController::feedBins(const QVector<float>& bins, qint64 nowMs)
{
    if (nowMs < 0) {
        nowMs = QDateTime::currentMSecsSinceEpoch();
    }
    if (!acceptsInput(nowMs)) {
        return;
    }
    // The bins are uncalibrated; the offset puts the floor in calibrated dB.
    // A NaN estimate (no bins) stays NaN and is dropped below.
    feedNoiseFloor(m_estimator.estimate(bins) + m_calibrationOffsetDb, nowMs);
}

bool ClarityController::acceptsInput(qint64 nowMs)
{
    if (!m_enabled || m_transmitting || m_paused) {
        return false;
    }
    if (m_holdActive) {
        if (nowMs < m_holdEndMs) {
            return false;
        }
        // The hold has run. Restart the smoothing clock at its end, so the
        // next reading is smoothed over the time since then and a key longer
        // than the time constant causes no jump. The cadence gate below
        // counts from the same instant.
        m_holdActive = false;
        m_lastPollMs = m_holdEndMs;
    }
    // Cadence gate: skip frames that land inside the current poll window.
    if (m_pollIntervalMs > 0 && m_hasEmitted &&
        (nowMs - m_lastPollMs) < m_pollIntervalMs) {
        return false;
    }
    return true;
}

void ClarityController::feedNoiseFloor(float rawFloor, qint64 nowMs)
{
    if (nowMs < 0) {
        nowMs = QDateTime::currentMSecsSinceEpoch();
    }
    if (!std::isfinite(rawFloor) || !acceptsInput(nowMs)) {
        return;
    }

    // EWMA smoothing. τ ≤ 0 is passthrough (no smoothing) for test
    // isolation. Otherwise alpha = 1 - exp(-dt/τ) where dt is the
    // interval since last computation.
    float smoothed;
    if (!m_hasSmoothed || m_tauSec <= 0.0f) {
        smoothed = rawFloor;
    } else {
        const float dt  = static_cast<float>(nowMs - m_lastPollMs) / 1000.0f;
        const float alpha = 1.0f - std::exp(-dt / m_tauSec);
        // Written as a step from the kept floor, so a reading equal to it
        // leaves it exactly where it was and emits no new thresholds.
        smoothed = m_smoothedFloor + alpha * (rawFloor - m_smoothedFloor);
    }
    m_smoothedFloor = smoothed;
    m_hasSmoothed   = true;
    m_lastPollMs    = nowMs;

    // Emit the smoothed floor on every cadence tick so NF-aware grid
    // and per-band NF priming (Tasks 2.9/2.10) receive each one.
    // NereusSDR-original — no Thetis equivalent.
    emit noiseFloorChanged(smoothed);

    // Thresholds go out whenever the floor differs from the last emitted
    // one. There is no deadband: the consumer slews between pairs.
    if (m_hasEmitted && smoothed == m_lastEmittedFloor) {
        return;
    }

    // Compute thresholds with margin around the smoothed floor.
    float low  = smoothed + m_lowMarginDb;
    float high = smoothed + m_highMarginDb;

    // Min-gap clamp — empty-band failure mode (§6.2.4).
    if ((high - low) < m_minGapDb) {
        const float center = (low + high) / 2.0f;
        low  = center - m_minGapDb / 2.0f;
        high = center + m_minGapDb / 2.0f;
    }

    m_lastLow          = low;
    m_lastHigh         = high;
    m_lastEmittedFloor = smoothed;
    m_hasEmitted       = true;
    emit waterfallThresholdsChanged(low, high);
}

}  // namespace NereusSDR
