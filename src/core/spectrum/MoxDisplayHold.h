#pragma once
// =================================================================
// src/core/spectrum/MoxDisplayHold.h  (NereusSDR)
// =================================================================
//
// Ported from Thetis sources:
//   Project Files/Source/Console/display.cs, original licence from Thetis
//   source is included below
//
// The length of the one hold a display runs after an un-key: Clarity, the
// waterfall AGC and the noise-floor fast attack all wait this long before
// they estimate again (design 2026-10-09 display pill and levels, 5.1 and
// 5.5). It is Thetis's noise-floor settle time with "Purge WDSP buffers on
// MOX" on (display.cs:5912).
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-10 - Written for NereusSDR by J.J. Boyd (KG4VCF), with
//                 AI-assisted transformation via Anthropic Claude Code. The
//                 settle time of display.cs:5912 as one helper; the fill
//                 time counts the display decimation, and the "2 extra
//                 frames" term of display.cs:2857 is not ported (see below).
// =================================================================

// --- From display.cs ---
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
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
// 02111-1307, USA.
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

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace NereusSDR {

// From Thetis display.cs:5912 [v2.10.3.15]:
//   float tmpDelay = Math.Max(1000f, fftFillTime + (_wdsp_mox_transition_buffer_clear ? fftFillTime : 0f));
// The 1000 of Math.Max: no hold is shorter than a second.
inline constexpr qint64 kMoxHoldMinMs = 1000;

/// How long a display holds its estimates after an un-key, in milliseconds:
/// max(kMoxHoldMinMs, 2 x fill), where fill = fftSize x decimation x 1000 /
/// sampleRateHz, truncated to whole milliseconds (the engine takes
/// size x decimation samples for one transform). A rate that is not finite
/// or not positive, or a size or decimation below 1, gives kMoxHoldMinMs.
///
/// NereusSDR, based on Thetis display.cs:5912 [v2.10.3.15] (the line quoted
/// at kMoxHoldMinMs). Thetis doubles the fill time only when
/// _wdsp_mox_transition_buffer_clear is set (Setup's "Purge WDSP buffers on
/// MOX", off by default). NereusSDR always clears the FFT input on a MOX
/// edge, so the doubled case is the only one here.
///
/// Not ported: the "2 extra frames" Thetis adds to its waterfall AGC delay,
/// From Thetis display.cs:2856-2858 [v2.10.3.15]:
///   //delay waterfall agc
///   _rx1_no_agc_duration = _high_perf_timer.ElapsedMsec + _fft_fill_timeRX1 + ((m_nFps / 1000f) * 2); // 2 extra frames
///   _ignore_waterfall_rx1_agc = true;
/// (m_nFps / 1000f) * 2 is frames per millisecond times two, about 0.06 ms at
/// 30 fps, not two frame periods: an upstream units bug. This hold still
/// covers two frames without it: a fill of two frame periods or more is
/// doubled, which adds at least those two periods to the fill, and a shorter
/// fill gets the full second.
inline qint64 moxHoldLengthMs(int fftSize, int decimation, double sampleRateHz) noexcept
{
    if (fftSize < 1 || decimation < 1 || !std::isfinite(sampleRateHz) || sampleRateHz <= 0.0) {
        return kMoxHoldMinMs;
    }
    // One hour bounds a rate so small that the product would not fit.
    constexpr double kFillCeilingMs = 3'600'000.0;
    const double fill = std::min(
        kFillCeilingMs,
        static_cast<double>(fftSize) * static_cast<double>(decimation) * 1000.0 / sampleRateHz);
    const qint64 fillMs = static_cast<qint64>(fill);
    return std::max<qint64>(kMoxHoldMinMs, 2 * fillMs);
}

} // namespace NereusSDR
