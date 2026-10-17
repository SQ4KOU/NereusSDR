#pragma once

// =================================================================
// src/gui/DisplayPillWiring.h  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original. What one pan's Display pill asks of
// the window, as a set of hooks, so the wiring can be tested with a real
// strip and pan and no MainWindow.
//
// Display pill plan Task 18 (D2, D5, D6, D25, D34; R-DSP-09, R-DSP-10,
// R-DSP-33, R-DSP-34, R-DSP-46). The pill's per-pan rows read and write
// the pan themselves (SpectrumOverlayPanel::setDisplayTarget). Everything
// else it only asks for: the station's FFT settings, the band's dB Max and
// Min, the Clarity switch, a reset of the pan and a Setup page. This file
// connects each of those requests to a hook.
//
// Modification history (NereusSDR):
//   2026-10-10 - Created. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//                 Claude Code.
//   2026-10-11 - applyFftSizingMode, for the pill's "Sizing:" row (design
//                 D38). J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//                 Claude Code.
// =================================================================

#include <QString>

#include <functional>

#include "gui/SpectrumOverlayPanel.h"

namespace NereusSDR {

class SpectrumWidget;

/// What the window does for one pan's pill. An empty hook is skipped.
struct DisplayPillHooks {
    /// The FFT tab's six requests. Each applies the value to the station
    /// (every pan and every stream), not to this pan alone.
    std::function<void(int)> applyFftBaseline;
    /// The "Sizing:" row (design D38): the mode, and the Hz/bin target to
    /// start from when the station remembers none.
    std::function<void(ControlRanges::DisplayFftSizingMode, double)> applyFftSizingMode;
    std::function<void(int)> applyFps;
    std::function<void(int)> applyWindowType;
    std::function<void(double)> applyHzPerBinTarget;
    std::function<void(int)> applyDecimation;
    /// The station's FFT settings as they stand, with this pan's running
    /// size and rate. Read after every request (taken or refused), when
    /// the pill is wired and each time the pill is opened.
    std::function<DisplayPillFftState()> fftState;
    /// The Clarity switch. The pill shows what the window then tells it
    /// (SpectrumOverlayPanel::setClarityState), not what was asked.
    std::function<void(bool)> setClarityEnabled;
    std::function<void()> retuneClarity;
    /// The stored dB Max and Min of the band this pan is on.
    std::function<void(int)> setPerBandDbMax;
    std::function<void(int)> setPerBandDbMin;
    /// "Setup defaults": the Setup page named by the open tab.
    std::function<void(const QString&)> openSetupPage;
};

/// Makes `widget` the pan `panel`'s pill edits and connects the pill's
/// requests to `hooks`. "Reset this pan" calls
/// SpectrumWidget::resetPanDisplayOverrides() on `widget`. Call it once per
/// strip: a second call would connect everything again.
void wireDisplayPill(SpectrumOverlayPanel* panel, SpectrumWidget* widget,
                     const DisplayPillHooks& hooks);

} // namespace NereusSDR
