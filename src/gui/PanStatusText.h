// no-port-check: NereusSDR-original. Remote daemon R3 pan status wording.

// SPDX-License-Identifier: GPL-3.0-or-later
//
// =================================================================
// src/gui/PanStatusText.h  (NereusSDR)
// =================================================================
//
// NereusSDR-original; no upstream port. Turns what a pan's remote display
// is doing into the words a user reads: a short line that fits a narrow pan
// and a full explanation shown on hover (R-R3-37, R-R3-21).
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-23  J.J. Boyd / KG4VCF  Created. AI-assisted via Anthropic
//                                    Claude Code.
// =================================================================
#pragma once

#include <QString>

namespace NereusSDR {

/// What a pan paints and what it says on hover. Both empty: nothing to say.
struct PanStatusText {
    QString shortLine;
    QString explanation;
    bool operator==(const PanStatusText&) const = default;
};

/// A pan's remote display, as RemoteMediaController knows it. Pure data:
/// the controller fills it, buildPanStatusText words it.
struct PanDisplayState {
    enum class Phase {
        None,           // Nothing to report.
        Showing,        // The Core accepted this pan's display.
        Waiting,        // Asked; no answer yet.
        ChangingWindow, // Asking again after the receiver's spectrum settings changed.
        Stalled,        // Asked; the answer is overdue.
        Paused,         // The Core's display limit has no room for this pan.
        Refused,        // Turned down; refusalReason says why.
        TooManyPans,    // This computer already shows as many pans as it can.
    };
    enum class PureSignal { Fine, Refused, Stalled };
    enum class ZoomLimit { None, LargestSize, SharedEngine, SourceBins };

    Phase phase = Phase::None;

    // Showing: what the Core sends, what the pan asked for, and the update
    // rate actually arriving (below zero when not measured).
    int pixels = 0;
    int fps = 0;
    int requestedPixels = 0;
    int requestedFps = 0;
    double receivedFps = -1.0;
    bool extendedView = false;

    // Paused: a PureSignal display the Core already accepted is over the
    // Core's current limit.
    bool pureSignalOverLimit = false;

    // Refused: the raw reason, as the Core sent it or as this computer
    // recorded it. It is translated only when worded (OperatorReasonText).
    QString refusalReason;

    // The PureSignal display, reported on every pan.
    PureSignal pureSignal = PureSignal::Fine;
    QString pureSignalRefusalReason;

    // The zoom-detail limit the Core reported for this pan.
    ZoomLimit zoomLimit = ZoomLimit::None;
    int zoomPoints = 0;

    /// Showing, but at less than the pan asked for.
    bool reduced() const
    {
        return phase == Phase::Showing
            && (pixels != requestedPixels || fps != requestedFps);
    }

    bool operator==(const PanDisplayState&) const = default;
};

/// The pan's short line and hover explanation for `state`. The short line
/// is empty at full quality.
PanStatusText buildPanStatusText(const PanDisplayState& state);

} // namespace NereusSDR
