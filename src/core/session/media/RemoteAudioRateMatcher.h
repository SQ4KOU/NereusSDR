// =================================================================
// src/core/session/media/RemoteAudioRateMatcher.h  (NereusSDR)
// =================================================================
//
// Ported from Thetis ivac.c and WDSP rmatch (v2.10.3.15, commit 3759d09).
// Worker-thread wrapper for WDSP rmatch's adaptive variable resampler.
// It deliberately owns neither an audio device nor a clock: its caller
// supplies input on decoded network arrival and calls take() only to
// replenish an output bus using actual hardware-consumed-frame state.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-21 — Reimplemented in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via OpenAI Codex.
//                 Ported the IVAC rmatchOUT call boundary from Thetis
//                 Project Files/Source/ChannelMaster/ivac.c:34-45,
//                 145-168, and 254 [v2.10.3.15 @3759d09]. The existing
//                 WDSP rmatch/varsamp engine retains its interpolation,
//                 filter, ring, and adaptive-feedback history.
// =================================================================
//
// === Verbatim Thetis Project Files/Source/ChannelMaster/ivac.c header ===
/*  ivac.c

This file is part of a program that implements a Software-Defined Radio.

Copyright (C) 2015-2025 Warren Pratt, NR0V
Copyright (C) 2015-2016 Doug Wigley, W5WC

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

warren@wpratt.com

*/
// === Verbatim Thetis Project Files/Source/wdsp/rmatch.h header ===
/*  rmatch.h

This file is part of a program that implements a Software-Defined Radio.

Copyright (C) 2017, 2022 Warren Pratt, NR0V

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

warren@wpratt.com

*/

#pragma once

#include <QVector>

namespace NereusSDR {

struct RemoteAudioRateMatcherStats {
    int underflows = 0;
    int overflows = 0;
    double currentRatio = 1.0;
    int ringCapacityFrames = 0;
    int ringFillFrames = 0;
};

/// Bounded worker-thread-only bridge to WDSP rmatch for 48 kHz stereo audio.
///
/// The interleaved float channels map directly to WDSP's double complex pair:
/// left is the real element and right is the imaginary element. push() and
/// take() must be serialized on the receiver worker; neither may run in an
/// audio device callback.
class RemoteAudioRateMatcher final {
public:
    static constexpr int kSampleRateHz = 48'000;
    static constexpr int kChannels = 2;
    // NereusSDR receiver resource policy: a single call is at most one
    // second and the bounded WDSP ring at most two seconds at 48 kHz.
    static constexpr int kMaxFramesPerCall = kSampleRateHz;
    static constexpr int kMaxRingFrames = 2 * kSampleRateHz;

    RemoteAudioRateMatcher();
    ~RemoteAudioRateMatcher();

    RemoteAudioRateMatcher(const RemoteAudioRateMatcher&) = delete;
    RemoteAudioRateMatcher& operator=(const RemoteAudioRateMatcher&) = delete;

    /// Rejects non-positive or resource-policy-exceeding dimensions.
    bool configure(int inputFrames, int outputFrames, int ringFrames);
    bool push(const QVector<float>& pcmInterleaved);
    QVector<float> take();
    void reset();

    /// getRMatchDiags() projection: underflows, overflows, var, and ringsize.
    /// ringFillFrames also includes the bounded native output suffix that has
    /// left WDSP but has not yet been returned by take().
    RemoteAudioRateMatcherStats stats() const;

private:
    void destroy() noexcept;

    void* m_matcher = nullptr;
    int m_inputFrames = 0;
    int m_outputFrames = 0;
    int m_ringFrames = 0;
    QVector<double> m_inputCarry;
    QVector<double> m_nativeOutput;
    int m_inputCarryFrames = 0;
    int m_outputCarryOffsetFrames = 0;
    int m_outputCarryFrames = 0;
};

} // namespace NereusSDR
