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
// From Thetis Project Files/Source/Console/console.cs:45977 — agc_thresh_point, range -160..0
inline constexpr int kAgcThresholdMinDb = -160;
inline constexpr int kAgcThresholdMaxDb = 0;
inline constexpr int kAgcThresholdStepDb = 1;

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
