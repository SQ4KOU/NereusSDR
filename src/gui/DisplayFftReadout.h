#pragma once

// =================================================================
// src/gui/DisplayFftReadout.h  (NereusSDR)
// =================================================================
//
// NereusSDR-original. What the Display pill's FFT tab and Setup > Display >
// Spectrum Defaults share for the FFT Size control: the line under the
// slider and the wait after a keyboard step (design D22, D23; R-DSP-31,
// R-DSP-61). One place, so the two read and behave alike.
//
// Modification history (NereusSDR):
//   2026-10-10  J.J. Boyd (KG4VCF). Created for the display pill plan
//                (Task 20), from SpectrumOverlayPanel's own readout.
//                AI-assisted via Anthropic Claude Code.
//   2026-10-11  J.J. Boyd (KG4VCF). FFT sizing mode (design D38 to D44):
//                the words of the "Sizing" control and of the two controls
//                it grays, shared by the pill and Setup. The line under
//                the slider always states what runs; its "Hz/bin target is
//                setting the size" form is gone. AI-assisted via Anthropic
//                Claude Code.
// =================================================================

#include <QString>

#include <algorithm>

#include "core/ControlRanges.h"

namespace NereusSDR {

/// Keyboard steps on an FFT Size slider (and a click on its groove) apply
/// once, this long after the last one. A drag applies as the handle is let
/// go.
inline constexpr int kDisplayFftSizeKeyDelayMs = 300;

/// The facts the line under an FFT Size slider is written from.
struct DisplayFftReadoutFacts {
    int    runningSize{0};          ///< the size the FFT runs at now
    double sampleRateHz{0.0};       ///< the rate it runs at
    int    decimation{1};

    /// The bin width that runs: sample rate / (decimation x size). 0 while
    /// there is no running size or rate.
    double binWidthHz() const
    {
        if (runningSize <= 0 || !(sampleRateHz > 0.0)) {
            return 0.0;
        }
        return sampleRateHz / (double(std::max(1, decimation)) * double(runningSize));
    }
};

/// The line under an FFT Size slider: bin width and time per transform at
/// the running size, in every sizing mode. Empty while there is no running
/// size or rate to state.
inline QString displayFftReadoutText(const DisplayFftReadoutFacts& facts)
{
    if (facts.runningSize > 0 && facts.sampleRateHz > 0.0) {
        const int decimation = std::max(1, facts.decimation);
        const double points = double(facts.runningSize) * double(decimation);
        return QStringLiteral("%1 Hz per bin · %2 s per transform at %3 kHz")
            .arg(facts.sampleRateHz / points, 0, 'f', 2)
            .arg(points / facts.sampleRateHz, 0, 'f', 2)
            .arg(facts.sampleRateHz / 1000.0, 0, 'g', 6);
    }
    return QString();
}

// ── The "Sizing" control (design D38): its words, in the pill and in Setup ──

/// The three choices, in the order they are shown.
struct DisplayFftSizingChoice {
    ControlRanges::DisplayFftSizingMode mode;
    const char* label;
};
inline constexpr DisplayFftSizingChoice kDisplayFftSizingChoices[] = {
    {ControlRanges::DisplayFftSizingMode::Auto, "Auto"},
    {ControlRanges::DisplayFftSizingMode::HzPerBin, "Hz/bin"},
    {ControlRanges::DisplayFftSizingMode::Fixed, "Fixed"},
};

inline QString displayFftSizingTip()
{
    return QStringLiteral(
        "How the FFT size is chosen. Auto: the zoom picks the size to keep the "
        "picture sharp. Hz/bin: the size follows the bin width you set. Fixed: "
        "the FFT Size slider sets the size and zoom does not change it.");
}

/// Why the FFT Size slider is grayed in `mode`. Empty in Fixed, where the
/// slider sets the size.
inline QString displayFftSizeHeldTip(ControlRanges::DisplayFftSizingMode mode)
{
    switch (mode) {
    case ControlRanges::DisplayFftSizingMode::Auto:
        return QStringLiteral("Sizing is Auto, so the zoom sets the FFT size. "
                              "Choose Fixed to set it yourself.");
    case ControlRanges::DisplayFftSizingMode::HzPerBin:
        return QStringLiteral("Sizing is Hz/bin, so the bin width sets the FFT size. "
                              "Choose Fixed to set it yourself.");
    case ControlRanges::DisplayFftSizingMode::Fixed:
        break;
    }
    return QString();
}

/// The Hz/bin control's tooltip while it sets the size.
inline QString displayHzPerBinTip()
{
    return QStringLiteral(
        "The bin width to hold at every zoom. The FFT size follows it: sample "
        "rate divided by bin width, rounded up to a power of two, within the "
        "size limits.");
}

/// Why the Hz/bin control is grayed outside the Hz/bin mode.
inline QString displayHzPerBinHeldTip()
{
    return QStringLiteral("Choose Hz/bin under Sizing to set the bin width yourself.");
}

/// What a grayed Hz/bin control shows: the remembered target, or with none
/// the target that entering Hz/bin would start from, which is the bin
/// width running now on the control's step (design D39).
inline double displayHzPerBinShownWhileHeld(double lastTarget, double runningBinWidthHz)
{
    return lastTarget > 0.0 ? lastTarget
                            : ControlRanges::hzPerBinTargetForBinWidth(runningBinWidthHz);
}

} // namespace NereusSDR
