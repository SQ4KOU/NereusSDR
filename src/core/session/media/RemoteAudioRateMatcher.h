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
//   2026-09-23: J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude
//                 Code. stats() also reports WDSP getControlFlag()
//                 (rmatch.h:157, rmatch.c:699-706) as controlActive, so a
//                 caller can tell a measured ratio from the initial one.
//   2026-09-23: J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude
//                 Code. kFilterDelayFrames names the varsamp FIR's delay
//                 (varsamp.c:60-63, 122-123, 175), so the measured remote
//                 audio delay (R-R3-35) counts it.
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
    // WDSP rmatch's control_flag (getControlFlag()): false until the
    // create_rmatchV 3.0 s startup delay has passed for both the audio
    // written and the audio read, and currentRatio is still the initial
    // 1.0; true once the controller measures and adjusts it.
    bool controlActive = false;
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
    // R-R3-35: how many frames later than the ring fill ahead of it an input
    // frame leaves take(). The resampler is a symmetric FIR over the newest
    // rsize input frames, newest first.
    // From Thetis Project Files/Source/wdsp/varsamp.c:60-63 [v2.10.3.15 @3759d09]:
    // rsize = 140 at equal rates, ncoef = (rsize + 1) + (R - 1) * rsize with
    // R = 1024 (rmatch.c:517), so the coefficient centre is h[70 * R].
    // From varsamp.c:122-123 and 175 [v2.10.3.15 @3759d09]: output tap j
    // (j frames old) uses h[(rsize - 1 - j) * R + R * h_offset], so the centre
    // is j = 69 at h_offset 0 and moves by the fraction h_offset (under one
    // frame) while the ratio is adjusted.
    static constexpr int kFilterDelayFrames = 69;

    RemoteAudioRateMatcher();
    ~RemoteAudioRateMatcher();

    RemoteAudioRateMatcher(const RemoteAudioRateMatcher&) = delete;
    RemoteAudioRateMatcher& operator=(const RemoteAudioRateMatcher&) = delete;

    /// Rejects non-positive or resource-policy-exceeding dimensions.
    bool configure(int inputFrames, int outputFrames, int ringFrames);
    bool push(const QVector<float>& pcmInterleaved);
    QVector<float> take();
    /// True when one public output block can be returned without asking WDSP
    /// for more input than its ring currently contains. Accounts for the
    /// partially consumed native 64-frame output block.
    bool canTakeWithoutUnderflow() const;
    void reset();

    /// getRMatchDiags() projection: underflows, overflows, var, and ringsize,
    /// plus getControlFlag() as controlActive.
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
