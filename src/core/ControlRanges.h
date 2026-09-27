#pragma once
// no-port-check: NereusSDR-original. The values below were moved here from
// the desktop widgets that draw them; each keeps the upstream cite it had
// there (AetherSDR, GPLv3 like NereusSDR; Thetis, GPLv2 or later).
// =================================================================
// src/core/ControlRanges.h  (NereusSDR)
// =================================================================
//
// The ranges and scales the operator's controls and gauges use, in one
// place the Core can read (iPhone app plan Task 19, R-IOS-06). The desktop
// widgets read them from here, and the Core's catalogue
// (src/core/session/StationCatalog) sends the same numbers to an app, so a
// phone draws the AGC-T slider, the S-meter and the transmit gauges with
// exactly the desktop's values.
//
// Only what a widget used to hold as a literal lives here. Values a board
// decides (the attenuator range, the preamp items, the PA rating) stay in
// BoardCapabilities and HpsdrModel.h, where the widgets already read them.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-24: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code. Moved from VfoWidget, RxApplet, SMeterWidget,
//               PhoneCwApplet and TxApplet.
//   2026-09-24: AGC-T range re-cited against Thetis v2.10.3.15 and its top
//               raised from 0 to +2 dB to match Thetis's clamp. J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
//   2026-09-24: The receive ranges (AF gain, SSQL, AM and FM squelch)
//               moved here from VfoWidget, RxApplet, SliceModel and the
//               DSP setup pages for the catalogue's `receive` key. J.J.
//               Boyd (KG4VCF), with AI-assisted implementation via
//               Anthropic Claude Code.
//   2026-09-27: The Setup > Display controls (FFT size, window, Hz/bin
//               target, FPS, the spectrum and waterfall detector,
//               averaging and averaging time, decimation) moved here from
//               DisplaySetupPages for the catalogue's `display` key, their
//               Thetis cites restamped against v2.10.3.15 (R-IOS-18,
//               R-IOS-27, R-IOS-06, R-R3-08). J.J. Boyd (KG4VCF), with
//               AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include <array>
#include <cstdint>

namespace NereusSDR::ControlRanges {

// ── AGC ───────────────────────────────────────────────────────────────────

/// One AGC mode the operator can pick: `id` is its AGCMode value.
struct AgcModeItem {
    int id;
    const char* label;
};

// The five modes the VFO flag's AGC row and the RX applet's AGC combo offer,
// in their order (AGCMode::Off..Fast; Custom is not offered).
inline constexpr std::array<AgcModeItem, 5> kAgcModes{{
    {0, "Off"},
    {1, "Long"},
    {2, "Slow"},
    {3, "Med"},
    {4, "Fast"},
}};

// AGC-T, the VFO flag's and the RX applet's AGC threshold slider, in dB.
// Thetis clamps the threshold it hands WDSP to -160..+2 in setAGCThresholdPoint:
// From Thetis Project Files/Source/Console/console.cs:46048-46049 [v2.10.3.15]
//   // MW0LGE_21k9d values are already offset as part of Display
//   if (agc_thresh_point > 2) agc_thresh_point = 2;
//   if (agc_thresh_point < -160.0) agc_thresh_point = -160.0;
inline constexpr int kAgcThresholdMinDb = -160;
inline constexpr int kAgcThresholdMaxDb = 2;
inline constexpr int kAgcThresholdStepDb = 1;

// ── Receive ───────────────────────────────────────────────────────────────

// AF gain, the VFO flag's AF slider and SliceModel::setAfGain's clamp, in
// the slider's own units (0 to 100). Thetis's AF slider spans the same:
// From Thetis Project Files/Source/Console/console.Designer.cs:3729-3730 [v2.10.3.15]
//   this.ptbAF.Maximum = 100;
//   this.ptbAF.Minimum = 0;
inline constexpr int kAfGainMin = 0;
inline constexpr int kAfGainMax = 100;
inline constexpr int kAfGainStep = 1;

// SSB squelch (SSQL), the VFO flag's and the RX applet's SQL slider, in
// slider units (0 to 100). SliceModel's ssqlThresh holds the slider value;
// RadioModel divides it by 100 for WDSP's 0.0..1.0.
inline constexpr int kSsqlThreshMin = 0;
inline constexpr int kSsqlThreshMax = 100;
inline constexpr int kSsqlThreshStep = 1;

// AM squelch, Setup > DSP > AM/SAM's threshold slider, in dB (SliceModel
// amsqThresh). Thetis's squelch slider spans the same:
// From Thetis Project Files/Source/Console/console.Designer.cs:7572-7573 [v2.10.3.15]
//   this.ptbSquelch.Maximum = 0;
//   this.ptbSquelch.Minimum = -160;
inline constexpr int kAmsqThreshMinDb = -160;
inline constexpr int kAmsqThreshMaxDb = 0;
inline constexpr int kAmsqThreshStepDb = 1;

// FM squelch, Setup > DSP > FM's threshold slider, in dB (SliceModel
// fmsqThresh; RxChannel::setFmsqThresh converts it for WDSP). The page
// uses the AM slider's range.
inline constexpr int kFmsqThreshMinDb = -160;
inline constexpr int kFmsqThreshMaxDb = 0;
inline constexpr int kFmsqThreshStepDb = 1;

// ── S-meter ───────────────────────────────────────────────────────────────

// S-unit reference: S0 = -127 dBm, each S-unit = 6 dB
// From AetherSDR src/gui/SMeterWidget.h:114-117 [@0cd4559]
inline constexpr float kSMeterS0Dbm = -127.0f;
inline constexpr float kSMeterS9Dbm = -73.0f;
inline constexpr float kSMeterMaxDbm = -13.0f;  // S9+60
inline constexpr float kSMeterDbPerSUnit = 6.0f;
// The analog S-meter's steps above S9: a tick every 10 dB up to S9+60
// (SMeterWidget's vintage faces; the classic face labels +20 and +40).
inline constexpr int kSMeterOverS9StepDb = 10;

// ── Transmit gauges ───────────────────────────────────────────────────────

// Mic level, the Phone/CW applet's gauge, in dB:
// HGauge(-40, +10, redStart=0, yellowStart=-10)
inline constexpr double kMicLevelMinDb = -40.0;
inline constexpr double kMicLevelMaxDb = 10.0;
inline constexpr double kMicLevelYellowFromDb = -10.0;
inline constexpr double kMicLevelRedFromDb = 0.0;

// SWR, the TX applet's gauge: 1.0 to 3.0, red from 2.5.
// Ticks: 1 / 1.5 / 2.5 / 3  (AetherSDR TxApplet.cpp:77)
inline constexpr double kSwrGaugeMin = 1.0;
inline constexpr double kSwrGaugeMax = 3.0;
inline constexpr double kSwrGaugeRedFrom = 2.5;

// RF power, the TX applet's gauge: red from the board's PA rating
// (paMaxWattsFor), full scale 20% past it.
inline constexpr double kRfPowerGaugeHeadroom = 1.2;

// ── Setup > Display ───────────────────────────────────────────────────────
//
// The Spectrum Defaults and Waterfall Defaults controls an app offers,
// moved here from src/gui/setup/DisplaySetupPages.cpp so the desktop page
// and the Core's catalogue (its `display` key) read one table. Each
// control's settings key, the desktop's label (without the colon), its
// range or items, and the default the desktop applies when the key is unset.

// The pages and groups the controls sit in, by their desktop titles.
inline constexpr const char* kDisplaySpectrumPageTitle = "Spectrum Defaults";
inline constexpr const char* kDisplayWaterfallPageTitle = "Waterfall Defaults";
inline constexpr const char* kDisplayFftGroupTitle = "Fast Fourier Transform";
inline constexpr const char* kDisplayRenderingGroupTitle = "Rendering";
inline constexpr const char* kDisplayWaterfallGroupTitle = "Display";

/// One item of a Setup > Display combo: `value` is what the control stores
/// (the combo index), `label` the desktop's text.
struct DisplayChoiceItem {
    int value;
    const char* label;
};

// FFT size: the Fast Fourier Transform group's "Size" slider. Its positions
// 0..6 give 4096 x 2^position points, 4096 to 262144:
// From Thetis Project Files/Source/Console/setup.designer.cs:35165 [v2.10.3.15]
//   this.tbDisplayFFTSize.Maximum = 6;
// From Thetis Project Files/Source/Console/setup.cs:16189 [v2.10.3.15]
//   FFTSize = (int)(4096 * Math.Pow(2, Math.Floor((double)(tbDisplayFFTSize.Value))));
inline constexpr const char* kDisplayFftSizeKey = "DisplayFftSize";
inline constexpr const char* kDisplayFftSizeLabel = "Size";
inline constexpr int kDisplayFftSizeBase = 4096;
inline constexpr int kDisplayFftSizePositionMax = 6;
// NereusSDR-native default: the size every reader of DisplayFftSize applies
// when it is unset (the FFT engine pool, a remote window's request, this
// page), slider position 0. Thetis's slider starts at position 5
// (setup.designer.cs:35169 [v2.10.3.15], `tbDisplayFFTSize.Value = 5`); the
// desktop's slider holds 5 only until its first load replaces it.
inline constexpr int kDisplayFftSizeDefault = 4096;
inline constexpr int kDisplayFftSizeSliderInitial = 5;

/// The FFT size at slider `position` (clamped to 0..6).
constexpr int displayFftSizeAt(int position) noexcept
{
    if (position < 0) { position = 0; }
    if (position > kDisplayFftSizePositionMax) { position = kDisplayFftSizePositionMax; }
    return kDisplayFftSizeBase << position;
}

/// The slider position for an FFT size: log2(size / 4096), 0..6. A size
/// that is not one of the seven takes the position below it.
constexpr int displayFftSizePosition(int fftSize) noexcept
{
    int position = 0;
    for (int n = fftSize / kDisplayFftSizeBase; n > 1 && position < kDisplayFftSizePositionMax;
         n >>= 1) {
        ++position;
    }
    return position;
}

// The Bin Width (Hz) readout beside the slider: sample rate / FFT size, to
// three places:
// From Thetis Project Files/Source/Console/setup.cs:16192-16193 [v2.10.3.15]
//   double bin_width = (double)Display.SampleRateRX1 / (double)console.specRX.GetSpecRX(0).FFTSize;
//   lblDisplayBinWidth.Text = bin_width.ToString("N3");
inline constexpr const char* kDisplayBinWidthLabel = "Bin Width (Hz)";
inline constexpr int kDisplayBinWidthDecimals = 3;

// The FFT sizes a pan's request rounds to (a remote window's plannedFftSize,
// src/gui/RemoteMediaController.cpp): the smallest power of two from 1024 up
// that reaches the wanted size, at most FFTEngine::maximumFftSize() (262144).
// NereusSDR-native.
inline constexpr int kDisplayFftPlanMinSize = 1024;
inline constexpr int kDisplayFftPlanMaxSize = 262144;

// FFT window: the "Window" combo, in Thetis's order, each item's value its
// WindowFunction (WDSP analyzer.c's case order):
// From Thetis Project Files/Source/Console/setup.designer.cs:35082-35089 [v2.10.3.15]
//   this.comboDispWinType.Items.AddRange(new object[] {
//   "Rectangular", "Blackman-Harris 4T", "Hann", "Flat-Top", "Hamming",
//   "Kaiser", "Blackman-Harris 7T"});
inline constexpr const char* kDisplayFftWindowKey = "DisplayFftWindow";
inline constexpr const char* kDisplayFftWindowLabel = "Window";
inline constexpr std::array<DisplayChoiceItem, 7> kDisplayFftWindows{{
    {0, "Rectangular"},
    {1, "Blackman-Harris 4T"},
    {2, "Hann"},
    {3, "Flat-Top"},
    {4, "Hamming"},
    {5, "Kaiser"},
    {6, "Blackman-Harris 7T"},
}};
// NereusSDR-native default: FFTEngine's own (WindowFunction::BlackmanHarris4).
inline constexpr int kDisplayFftWindowDefault = 1;

// Hz/bin Target: NereusSDR-native (the 2026-05-08 auto-zoom override, no
// Thetis equivalent). 0 is "Off"; above 0 the pan's FFT is at least
// sample rate / target points, the FFT size slider still its floor.
inline constexpr const char* kDisplayHzPerBinTargetKey = "DisplayHzPerBinTarget";
inline constexpr const char* kDisplayHzPerBinTargetLabel = "Hz/bin Target";
inline constexpr double kDisplayHzPerBinTargetMin = 0.0;
inline constexpr double kDisplayHzPerBinTargetMax = 200.0;
inline constexpr double kDisplayHzPerBinTargetStep = 0.5;
inline constexpr int kDisplayHzPerBinTargetDecimals = 2;
inline constexpr double kDisplayHzPerBinTargetDefault = 0.0;
inline constexpr const char* kDisplayHzPerBinTargetOffLabel = "Off";
inline constexpr const char* kDisplayHzPerBinTargetUnit = "Hz/bin";

// FPS: the Rendering group's frame-rate slider. NereusSDR-native range and
// default (10 to 60, 30); Thetis's udDisplayFPS spans 1 to 144 and starts
// at 60 (setup.designer.cs:33958-33977 [v2.10.3.15]).
inline constexpr const char* kDisplaySpectrumFpsKey = "DisplaySpectrumFps";
inline constexpr const char* kDisplaySpectrumFpsLabel = "FPS";
inline constexpr int kDisplaySpectrumFpsMin = 10;
inline constexpr int kDisplaySpectrumFpsMax = 60;
inline constexpr int kDisplaySpectrumFpsStep = 1;
inline constexpr int kDisplaySpectrumFpsDefault = 30;
inline constexpr const char* kDisplaySpectrumFpsUnit = "fps";

// Spectrum Detector, in Thetis's order (SpectrumDetectorMode's values):
// From Thetis Project Files/Source/Console/setup.designer.cs:34984-34989 [v2.10.3.15]
//   this.comboDispPanDetector.Items.AddRange(new object[] {
//   "Peak", "Rosenfell", "Average", "Sample", "RMS"});
inline constexpr const char* kDisplaySpectrumDetectorKey = "DisplaySpectrumDetector";
inline constexpr const char* kDisplaySpectrumDetectorLabel = "Spectrum Detector";
inline constexpr std::array<DisplayChoiceItem, 5> kDisplaySpectrumDetectors{{
    {0, "Peak"},
    {1, "Rosenfell"},
    {2, "Average"},
    {3, "Sample"},
    {4, "RMS"},
}};
// NereusSDR-native default (SpectrumWidget: Peak).
inline constexpr int kDisplaySpectrumDetectorDefault = 0;

// Spectrum Averaging, in Thetis's order (SpectrumAveraging's values):
// From Thetis Project Files/Source/Console/setup.designer.cs:34959-34963 [v2.10.3.15]
//   this.comboDispPanAveraging.Items.AddRange(new object[] {
//   "None", "Recursive", "Time Window", "Log Recursive"});
inline constexpr const char* kDisplaySpectrumAveragingKey = "DisplaySpectrumAveraging";
inline constexpr const char* kDisplaySpectrumAveragingLabel = "Spectrum Averaging";
inline constexpr std::array<DisplayChoiceItem, 4> kDisplayAveragingModes{{
    {0, "None"},
    {1, "Recursive"},
    {2, "Time Window"},
    {3, "Log Recursive"},
}};
// NereusSDR-native default (SpectrumWidget: Log Recursive).
inline constexpr int kDisplaySpectrumAveragingDefault = 3;

// Spectrum Avg Time, in ms. Thetis's udDisplayAVGTime starts at 30:
// From Thetis Project Files/Source/Console/setup.designer.cs:35019-35023 [v2.10.3.15]
//   this.udDisplayAVGTime.Value = new decimal(new int[] { 30, 0, 0, 0});
// Its range is 1 to 9999 in steps of 1 (setup.designer.cs:34998-35013
// [v2.10.3.15]); NereusSDR raises the floor to 10 ms and steps by 10.
inline constexpr const char* kDisplaySpectrumAvgTimeKey = "DisplaySpectrumAverageTimeMs";
inline constexpr const char* kDisplaySpectrumAvgTimeLabel = "Spectrum Avg Time";
inline constexpr int kDisplayAvgTimeMinMs = 10;
inline constexpr int kDisplayAvgTimeMaxMs = 9999;
inline constexpr int kDisplayAvgTimeStepMs = 10;
inline constexpr int kDisplaySpectrumAvgTimeDefaultMs = 30;

// Decimation. NereusSDR-native range 1 to 32 (FFTEngine::setDecimation and
// the Core's request check); Thetis's udDisplayDecimation spans 1 to 16 and
// starts at 1 (setup.designer.cs:33834-33853 [v2.10.3.15]). The desktop
// keeps no setting for it: each window's engines hold it.
inline constexpr const char* kDisplayDecimationLabel = "Decimation";
inline constexpr int kDisplayDecimationMin = 1;
inline constexpr int kDisplayDecimationMax = 32;
inline constexpr int kDisplayDecimationStep = 1;
inline constexpr int kDisplayDecimationDefault = 1;

// WF Detector: Thetis's waterfall detector has no RMS:
// From Thetis Project Files/Source/Console/setup.designer.cs:34577-34581 [v2.10.3.15]
//   this.comboDispWFDetector.Items.AddRange(new object[] {
//   "Peak", "Rosenfell", "Average", "Sample"});
inline constexpr const char* kDisplayWaterfallDetectorKey = "DisplayWaterfallDetector";
inline constexpr const char* kDisplayWaterfallDetectorLabel = "WF Detector";
inline constexpr std::array<DisplayChoiceItem, 4> kDisplayWaterfallDetectors{{
    {0, "Peak"},
    {1, "Rosenfell"},
    {2, "Average"},
    {3, "Sample"},
}};
// NereusSDR-native default (SpectrumWidget: Peak).
inline constexpr int kDisplayWaterfallDetectorDefault = 0;

// WF Averaging: the same four modes as the spectrum's:
// From Thetis Project Files/Source/Console/setup.designer.cs:34552-34556 [v2.10.3.15]
//   this.comboDispWFAveraging.Items.AddRange(new object[] {
//   "None", "Recursive", "Time Window", "Log Recursive"});
inline constexpr const char* kDisplayWaterfallAveragingKey = "DisplayWaterfallAveraging";
inline constexpr const char* kDisplayWaterfallAveragingLabel = "WF Averaging";
// NereusSDR-native default (SpectrumWidget: None).
inline constexpr int kDisplayWaterfallAveragingDefault = 0;

// WF Avg Time, in ms, in the spectrum's range. Thetis's udDisplayAVTimeWF
// starts at 120:
// From Thetis Project Files/Source/Console/setup.designer.cs:34611-34615 [v2.10.3.15]
//   this.udDisplayAVTimeWF.Value = new decimal(new int[] { 120, 0, 0, 0});
inline constexpr const char* kDisplayWaterfallAvgTimeKey = "DisplayWaterfallAverageTimeMs";
inline constexpr const char* kDisplayWaterfallAvgTimeLabel = "WF Avg Time";
inline constexpr int kDisplayWaterfallAvgTimeDefaultMs = 120;

// ── Slice colours ─────────────────────────────────────────────────────────

// The slice badge colours, slice A first, as 0xRRGGBB. A slice past the
// last one takes the first colour (VfoWidget::sliceColor).
// From AetherSDR SliceColors.h
inline constexpr std::array<std::uint32_t, 4> kSliceColours{
    0x00d4ffu,  // cyan
    0xff40ffu,  // magenta
    0x40ff40u,  // green
    0xffff00u,  // yellow
};

/// The colour of slice `index` (0 is slice A), as 0xRRGGBB.
constexpr std::uint32_t sliceColour(int index) noexcept
{
    if (index < 0 || index >= static_cast<int>(kSliceColours.size())) {
        return kSliceColours[0];
    }
    return kSliceColours[static_cast<std::size_t>(index)];
}

} // namespace NereusSDR::ControlRanges
