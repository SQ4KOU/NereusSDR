#pragma once
// =================================================================
// src/core/spectrum/ClarityLevels.h  (NereusSDR)
// =================================================================
//
// Source attribution (AetherSDR, GPLv3):
//
//   Copyright (C) 2024-2026  Jeremy (KK7GWY) / AetherSDR contributors
//       per https://github.com/ten9876/AetherSDR (GPLv3; see LICENSE
//       and About dialog for the live contributor list)
//
//   This file is a port or structural derivative of AetherSDR source.
//   AetherSDR is licensed under the GNU General Public License v3.
//   NereusSDR is also GPLv3. Attribution follows GPLv3 section 5
//   requirements.
//
// Ported from AetherSDR src/gui/SpectrumWidget.cpp [@1e0718ad]: the
// Black Level step (0.5 dB, 50 = no offset, :11928) and the Color Gain
// span (80 dB less 0.7 dB a step, :11593). Upstream has no per-file
// copyright header.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-10: Created for NereusSDR by J.J. Boyd (KG4VCF), with
//                 AI-assisted transformation via Anthropic Claude Code.
//                 Display pill plan Task 3: the waterfall level model
//                 used while Clarity drives the levels, and the one-time
//                 conversion of Black Level and Color Gain values stored
//                 under the model before it.
// =================================================================
//
// The waterfall's Black Level and Color Gain while Clarity drives the
// levels. Clarity measures the noise floor N; the two sliders place the
// colour range against it:
//
//   black point = N + (Black - 50) * 0.5 dB     Black up = blacker
//   colour span = 80 - Gain * 0.7 dB            Gain up = more contrast
//   top         = black point + colour span
//
// NereusSDR, based on AetherSDR. AetherSDR's own comment says "higher
// black_level = more of the noise is black"
// (src/gui/SpectrumWidget.cpp:11588 [@1e0718ad]) but its formula moves
// the floor by (125 - black) * 0.4, so higher shows more noise (:11592).
// NereusSDR copied that formula and the mismatch with it. Here the
// slider does what it says, and the values stored under the old formula
// are converted once (convertLegacyBlackLevel / convertLegacyColorGain)
// so the picture does not jump on upgrade. Thetis has neither control;
// with Clarity off its Low and High levels apply unchanged.
//
// Header-only and Qt-free, so the renderer, the settings conversion and
// the tests share one copy of the arithmetic.

#include <algorithm>
#include <cmath>

namespace NereusSDR::ClarityLevels {

// ---- Black Level ----
inline constexpr int kBlackLevelMin = 0;
inline constexpr int kBlackLevelMax = 100;
/// The slider position that puts the black point on the noise floor.
inline constexpr int kBlackLevelAtNoise = 50;
/// Reproduces the look before this model: N + 3.5 dB (was N + 3.4 dB).
inline constexpr int kBlackLevelDefault = 57;
// From AetherSDR src/gui/SpectrumWidget.cpp:11928 [@1e0718ad]
//   blackThresh = m_autoBlackThresh + (50 - m_wfAutoBlackOffset) * 0.5f;
// NereusSDR takes the step and the centre; the sign is ours (up = blacker).
inline constexpr float kBlackLevelDbPerStep = 0.5f;

// ---- Color Gain ----
inline constexpr int kColorGainMin = 0;
inline constexpr int kColorGainMax = 100;
/// Reproduces the look before this model: a 38 dB span (was 38.1 dB).
inline constexpr int kColorGainDefault = 60;
// From AetherSDR src/gui/SpectrumWidget.cpp:11593 [@1e0718ad]
//   const float visRange = 80.0f - m_wfColorGain * 0.7f;  // 80 dB down to 10 dB
inline constexpr float kColorSpanAtZeroGainDb = 80.0f;
inline constexpr float kColorSpanDbPerGainStep = 0.7f;

/// Where black starts, in dBm, for a noise floor and a Black Level.
constexpr float blackPointDbm(float noiseFloorDbm, int blackLevel) noexcept
{
    return noiseFloorDbm
        + static_cast<float>(blackLevel - kBlackLevelAtNoise) * kBlackLevelDbPerStep;
}

/// How many dB the colours cover above the black point.
constexpr float colorSpanDb(int colorGain) noexcept
{
    return kColorSpanAtZeroGainDb
        - static_cast<float>(colorGain) * kColorSpanDbPerGainStep;
}

// ---- The model these values were stored under ----
//
// Before this model the renderer applied both sliders to Clarity's pair
// (low = N - 5 dB, high = N + 55 dB, ClarityController's margins):
//
//   black point = N - 5 + 0.4 * (125 - oldBlack)
//   top         = N + 55 - 0.3 * oldGain
//
// so the span depended on both sliders.
inline constexpr int kLegacyBlackLevelMax = 125;
inline constexpr int kLegacyBlackLevelDefault = 104;
inline constexpr int kLegacyColorGainMax = 100;
inline constexpr int kLegacyColorGainDefault = 45;
// From AetherSDR src/gui/SpectrumWidget.cpp:11592 [@1e0718ad]
//   const float floorShift = (125 - m_wfBlackLevel) * 0.4f;  // inverted: 0=max shift, 125=no shift
inline constexpr double kLegacyBlackDbPerStep = 0.4;
/// NereusSDR-original: dB the old Color Gain took off the top, per step.
inline constexpr double kLegacyGainDbPerStep = 0.3;
/// ClarityController's low and high margins around the noise floor.
inline constexpr double kLegacyClarityLowMarginDb = -5.0;
inline constexpr double kLegacyClarityHighMarginDb = 55.0;

/// dB above the noise floor where an old Black Level put the black point.
inline double legacyBlackOffsetDb(int oldBlack) noexcept
{
    const int black = std::clamp(oldBlack, 0, kLegacyBlackLevelMax);
    return kLegacyClarityLowMarginDb
        + kLegacyBlackDbPerStep * static_cast<double>(kLegacyBlackLevelMax - black);
}

/// dB between the black point and the top for an old Gain and Black.
inline double legacyColorSpanDb(int oldGain, int oldBlack) noexcept
{
    const int gain = std::clamp(oldGain, 0, kLegacyColorGainMax);
    return kLegacyClarityHighMarginDb - kLegacyGainDbPerStep * static_cast<double>(gain)
        - legacyBlackOffsetDb(oldBlack);
}

/// The Black Level that keeps an old Black Level's black point: the
/// nearest slider position, clamped to the slider's ends.
inline int convertLegacyBlackLevel(int oldBlack) noexcept
{
    const double steps = legacyBlackOffsetDb(oldBlack)
        / static_cast<double>(kBlackLevelDbPerStep);
    const long level = std::lround(static_cast<double>(kBlackLevelAtNoise) + steps);
    return static_cast<int>(std::clamp<long>(level, kBlackLevelMin, kBlackLevelMax));
}

/// The Color Gain that keeps the old span. The old span depended on the
/// Black Level too, so the pan's own old Black Level is needed.
inline int convertLegacyColorGain(int oldGain, int oldBlack) noexcept
{
    const double steps =
        (static_cast<double>(kColorSpanAtZeroGainDb) - legacyColorSpanDb(oldGain, oldBlack))
        / static_cast<double>(kColorSpanDbPerGainStep);
    const long gain = std::lround(steps);
    return static_cast<int>(std::clamp<long>(gain, kColorGainMin, kColorGainMax));
}

} // namespace NereusSDR::ClarityLevels
