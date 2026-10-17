// =================================================================
// src/core/ClarityController.h  (NereusSDR)
// =================================================================
//
// Ported from Thetis source:
//   Project Files/Source/Console/display.cs, original licence from Thetis source is included below
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-17 — Reimplemented in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via Anthropic
//                 Claude Code.
//   2026-10-10 — The level is kept through a key, a hold follows the un-key,
//                 the smoothing clock restarts after it, the bins take the
//                 calibration offset and the deadband is gone. J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via Anthropic
//                 Claude Code.
//   2026-10-10 — CarriedState, so the Core's replacement controller for a
//                 display carries on where the old one was. J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via Anthropic
//                 Claude Code.
// =================================================================

//=================================================================
// display.cs
//=================================================================
// Thetis is a C# implementation of a Software Defined Radio.
// Copyright (C) 2004-2009  FlexRadio Systems
// Copyright (C) 2010-2020  Doug Wigley (W5WC)
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//
// You may contact us via email at: sales@flex-radio.com.
// Paper mail may be sent to: 
//    FlexRadio Systems
//    8900 Marybank Dr.
//    Austin, TX 78750
//    USA
//
//=================================================================
// Waterfall AGC Modifications Copyright (C) 2013 Phil Harman (VK6APH)
// Transitions to directX and continual modifications Copyright (C) 2020-2025 Richard Samphire (MW0LGE)
//=================================================================
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

#pragma once

#include "core/NereusCoreExport.h"
#include "core/NoiseFloorEstimator.h"

#include <QObject>
#include <QVector>

namespace NereusSDR {

class NEREUS_CORE_EXPORT ClarityController : public QObject {
    Q_OBJECT
public:
    explicit ClarityController(QObject* parent = nullptr);

    bool isEnabled() const noexcept     { return m_enabled;     }
    bool isPaused() const noexcept      { return m_paused;      }
    bool isTransmitting() const noexcept{ return m_transmitting;}

    // True from beginHold() until its length has run. `nowMs` as feedBins().
    bool inHold(qint64 nowMs = -1) const;
    float smoothedFloor() const noexcept{ return m_smoothedFloor;}

    // Last emitted thresholds (qQNaN() before any emission).
    float lastLow() const noexcept  { return m_lastLow;  }
    float lastHigh() const noexcept { return m_lastHigh; }

    // What a replacement controller needs to carry on where this one is:
    // the smoothed floor, the last emitted thresholds, the smoothing clock
    // and a hold that is still running. The Core hands it from a display's
    // old controller to its new one when the display is asked for again on
    // the same receiver. NereusSDR-original, no Thetis equivalent.
    struct CarriedState {
        float  smoothedFloor    = 0.0f;
        bool   hasSmoothed      = false;
        float  lastLow          = 0.0f;
        float  lastHigh         = 0.0f;
        float  lastEmittedFloor = 0.0f;
        bool   hasEmitted       = false;
        qint64 lastPollMs       = 0;
        bool   holdActive       = false;
        qint64 holdEndMs        = 0;
    };
    CarriedState carriedState() const;
    // Takes the state as its own and emits nothing: the caller already has
    // the thresholds. The tunables, enabled, paused and transmitting are
    // not part of it.
    void adoptCarriedState(const CarriedState& state);

    // Tunables — default to spec-locked values, exposed for tests.
    void setPollIntervalMs(int ms);
    void setSmoothingTauSec(float sec);
    void setLowMarginDb(float db);
    void setHighMarginDb(float db);
    void setMinGapDb(float db);
    void setPercentile(float p);

public slots:
    // Master toggle. When off, controller stops acting on feedBins()
    // and does not emit. Currently-displayed thresholds are left alone.
    void setEnabled(bool on);

    // TX pause via MOX signal — per spec §6.2.4 failure mode mitigation.
    // The smoothed floor and the last emitted thresholds are kept through the
    // key: the un-key restores them, it does not re-anchor. Keying cancels a
    // hold that is still running.
    void setTransmitting(bool tx);

    // The hold after an un-key. Until `nowMs + lengthMs` the controller
    // takes in no bins and no floors and emits nothing. When the hold ends
    // the smoothing clock restarts: the first reading accepted after it is
    // smoothed over the time since the hold's end, not since the last
    // reading before the key. The caller gives the length.
    void beginHold(qint64 lengthMs, qint64 nowMs = -1);
    void cancelHold();

    // Added to the estimate feedBins() makes, before smoothing, so the floor
    // is in calibrated dB. feedNoiseFloor() values are calibrated already
    // and get nothing added.
    void setCalibrationOffsetDb(float db);

    // User dragged a threshold slider while Clarity was on. Pause until
    // the user clicks Re-tune or toggles Clarity off/on.
    void notifyManualOverride();

    // "Re-tune now" button hook — resumes after manual override and
    // snaps smoothing to the latest estimate.
    void retuneNow();

    // Band-switch snap — PanadapterModel feeds the stored floor for the
    // new band. EWMA state re-anchors at this value and thresholds are
    // emitted immediately so the waterfall snaps to the remembered state.
    // NaN input is ignored (band has no stored Clarity data).
    void snapToFloor(float floorDbm);

    // Per-frame FFT bin vector (dB) from FFTEngine. The controller may
    // no-op if disabled, paused, transmitting, or inside the poll window.
    //
    // `nowMs` is an optional monotonic timestamp for deterministic tests.
    // Production callers omit it and the implementation uses the Qt wall
    // clock. Not a test-only surface — any caller can pin a time source
    // (useful for replay/playback features).
    void feedBins(const QVector<float>& bins, qint64 nowMs = -1);

    // Remote Core has the full FFT bins. Feed its unsmoothed percentile
    // estimate through the same cadence, smoothing and operator gates as
    // local feedBins(); reduced display pixels are not estimator input.
    void feedNoiseFloor(float rawFloorDbm, qint64 nowMs = -1);

signals:
    // Thresholds should change. Emitted on every accepted reading whose
    // smoothed floor differs from the last emitted one; the consumer slews
    // between them.
    void waterfallThresholdsChanged(float lowDbm, float highDbm);

    // Smoothed noise-floor estimate. Emitted on every cadence tick, after
    // EWMA smoothing. Not emitted while keyed or in a hold. Used by NF-aware
    // grid (Task 2.9) and per-band NF priming (Task 2.10).
    // NereusSDR-original — no Thetis equivalent.
    void noiseFloorChanged(float nfDbm);

    // Status badge feed for SpectrumOverlayPanel. Green when active, amber
    // when paused (TX, manual override, or disabled).
    void pausedChanged(bool paused);

private:
    bool acceptsInput(qint64 nowMs);
    NoiseFloorEstimator m_estimator;

    bool   m_enabled       = false;
    bool   m_transmitting  = false;
    bool   m_paused        = false;

    // EWMA state — NaN until first valid frame.
    float  m_smoothedFloor = 0.0f;  // placeholder for RED stub
    bool   m_hasSmoothed   = false;

    // Last emitted thresholds + floor.
    float  m_lastLow           = 0.0f;
    float  m_lastHigh          = 0.0f;
    float  m_lastEmittedFloor  = 0.0f;
    bool   m_hasEmitted        = false;

    // Cadence — milliseconds since epoch of last poll.
    qint64 m_lastPollMs    = 0;

    // The hold after an un-key (beginHold).
    bool   m_holdActive    = false;
    qint64 m_holdEndMs     = 0;

    // Added to feedBins() estimates (setCalibrationOffsetDb).
    float  m_calibrationOffsetDb = 0.0f;

    // Tunables (spec defaults).
    int    m_pollIntervalMs = 500;
    float  m_tauSec         = 3.0f;
    float  m_lowMarginDb    = -5.0f;
    float  m_highMarginDb   = 55.0f;
    float  m_minGapDb       = 30.0f;
};

}  // namespace NereusSDR
