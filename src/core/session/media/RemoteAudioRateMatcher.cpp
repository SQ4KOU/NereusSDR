// =================================================================
// src/core/session/media/RemoteAudioRateMatcher.cpp  (NereusSDR)
// =================================================================
//
// Ported from Thetis ivac.c and WDSP rmatch (v2.10.3.15, commit 3759d09).
// Porting from Project Files/Source/ChannelMaster/ivac.c:34-45,
// 145-168, and 254 — original C logic creates rmatchOUT with
// create_rmatchV(audio_size, vac_size, audio_rate, vac_rate,
// OUTringsize, initial_OUTvar), pushes mixed audio through xrmatchIN,
// and pulls each device block through xrmatchOUT.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-21 — Reimplemented in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via OpenAI Codex.
//                 Faithful 48 kHz stereo wrapper over the existing WDSP
//                 rmatch/varsamp engine. It adds no feedback or correction
//                 math and preserves the engine's variable interpolation and
//                 history across every valid push/take call.
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
// === Verbatim Thetis Project Files/Source/wdsp/rmatch.c header ===
/*  rmatch.c

This file is part of a program that implements a Software-Defined Radio.

Copyright (C) 2017, 2018, 2022 Warren Pratt, NR0V

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

// === Verbatim Thetis Project Files/Source/ChannelMaster/cmsetup.c header ===
/*  cmsetup.c

This file is part of a program that implements a Software-Defined Radio.

Copyright (C) 2014 Warren Pratt, NR0V

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

#include "core/session/media/RemoteAudioRateMatcher.h"

#include <algorithm>
#include <cmath>

#ifdef HAVE_WDSP
extern "C" {
// Exact declarations from Thetis Project Files/Source/wdsp/rmatch.h:117-139
// [v2.10.3.15 @3759d09]. Keeping this narrow avoids importing WDSP's private
// platform synchronization types into the C++ interface.
void* create_rmatchV(int in_size, int out_size, int nom_inrate, int nom_outrate,
                     int ringsize, double var);
void destroy_rmatchV(void* ptr);
void xrmatchOUT(void* b, double* out);
void xrmatchIN(void* b, double* in);
void getRMatchDiags(void* b, int* underflows, int* overflows, double* var,
                    int* ringsize, int* nring);
void forceRMatchVar(void* b, int force, double fvar);
}
#endif

namespace NereusSDR {
namespace {

// Porting from Project Files/Source/ChannelMaster/cmsetup.c:getbuffsize()
// (lines 104-111) [v2.10.3.15 @3759d09] — original C logic:
// buffer sizes are a function of sample rate to yield constant latency
// const int base_rate = 48000; const int base_size = 64;
// return base_size * rate / base_rate;
// Keep each WDSP rmatch call at this native 48 kHz quantum. The public
// packet/device dimensions are bridged by bounded carries below; WDSP's
// feedback, interpolation, phase, and ratio control remain unchanged.
constexpr int kThetisNativeBlockFrames = 64;

} // namespace

RemoteAudioRateMatcher::RemoteAudioRateMatcher() = default;

RemoteAudioRateMatcher::~RemoteAudioRateMatcher()
{
    destroy();
}

bool RemoteAudioRateMatcher::configure(int inputFrames, int outputFrames, int ringFrames)
{
    destroy();

    if (inputFrames <= 0 || outputFrames <= 0
        || inputFrames > kMaxFramesPerCall
        || outputFrames > kMaxFramesPerCall
        || ringFrames > kMaxRingFrames) {
        return false;
    }

    // From Thetis Project Files/Source/wdsp/rmatch.c:132-144 [v2.10.3.15 @3759d09]:
    // max_ring_insize = (int)(1.0 + insize * (1.05 * nom_ratio)); and
    // the rmatch ring is at least twice that size and twice outsize.
    const int minimumResampledFrames = static_cast<int>(1.0 + inputFrames * 1.05);
    const int minimumRingFrames = 2 * std::max(minimumResampledFrames, outputFrames);
    if (ringFrames < minimumRingFrames) {
        return false;
    }

#ifdef HAVE_WDSP
    // From Thetis Project Files/Source/ChannelMaster/ivac.c:41 [v2.10.3.15 @3759d09]
    // — data FROM RADIO TO VAC. The remote receive path is the same
    // source-to-output direction at equal nominal 48 kHz rates.
    m_matcher = create_rmatchV(kThetisNativeBlockFrames, kThetisNativeBlockFrames,
                               kSampleRateHz, kSampleRateHz,
                               ringFrames, 1.0);
    if (!m_matcher) {
        return false;
    }
    // From Thetis Project Files/Source/ChannelMaster/ivac.c:41-44
    // [v2.10.3.15 @3759d09]. `force == 0` preserves WDSP's natural
    // feedback controller; fvar is inactive in that mode.
    forceRMatchVar(m_matcher, 0, 1.0);

    m_inputFrames = inputFrames;
    m_outputFrames = outputFrames;
    m_ringFrames = ringFrames;
    m_inputCarry.resize(kThetisNativeBlockFrames * kChannels);
    m_nativeOutput.resize(kThetisNativeBlockFrames * kChannels);
    m_inputCarryFrames = 0;
    m_outputCarryOffsetFrames = 0;
    m_outputCarryFrames = 0;
    return true;
#else
    Q_UNUSED(inputFrames);
    Q_UNUSED(outputFrames);
    Q_UNUSED(ringFrames);
    return false;
#endif
}

bool RemoteAudioRateMatcher::push(const QVector<float>& pcmInterleaved)
{
    if (!m_matcher || pcmInterleaved.size() != m_inputFrames * kChannels) {
        return false;
    }

    for (float sample : pcmInterleaved) {
        if (!std::isfinite(sample)) {
            return false;
        }
    }

#ifdef HAVE_WDSP
    int sourceFrame = 0;
    while (sourceFrame < m_inputFrames) {
        const int frames = std::min(kThetisNativeBlockFrames - m_inputCarryFrames,
                                    m_inputFrames - sourceFrame);
        for (int frame = 0; frame < frames; ++frame) {
            const int carrySample = (m_inputCarryFrames + frame) * kChannels;
            const int sourceSample = (sourceFrame + frame) * kChannels;
            m_inputCarry[carrySample] = pcmInterleaved.at(sourceSample);
            m_inputCarry[carrySample + 1] = pcmInterleaved.at(sourceSample + 1);
        }
        m_inputCarryFrames += frames;
        sourceFrame += frames;
        if (m_inputCarryFrames == kThetisNativeBlockFrames) {
            // From Thetis Project Files/Source/ChannelMaster/ivac.c:168
            // [v2.10.3.15 @3759d09] — xrmatchIN(rmatchOUT, buff).
            xrmatchIN(m_matcher, m_inputCarry.data());
            m_inputCarryFrames = 0;
        }
    }
    return true;
#else
    return false;
#endif
}

QVector<float> RemoteAudioRateMatcher::take()
{
    if (!m_matcher) {
        return {};
    }

#ifdef HAVE_WDSP
    QVector<float> pcm(m_outputFrames * kChannels);
    int destinationFrame = 0;
    while (destinationFrame < m_outputFrames) {
        if (m_outputCarryOffsetFrames == m_outputCarryFrames) {
            // From Thetis Project Files/Source/ChannelMaster/ivac.c:254
            // [v2.10.3.15 @3759d09] — xrmatchOUT(rmatchOUT, out_ptr).
            xrmatchOUT(m_matcher, m_nativeOutput.data());
            m_outputCarryOffsetFrames = 0;
            m_outputCarryFrames = kThetisNativeBlockFrames;
        }
        const int frames = std::min(m_outputCarryFrames - m_outputCarryOffsetFrames,
                                    m_outputFrames - destinationFrame);
        for (int frame = 0; frame < frames; ++frame) {
            const int sourceSample = (m_outputCarryOffsetFrames + frame) * kChannels;
            const int destinationSample = (destinationFrame + frame) * kChannels;
            pcm[destinationSample] = static_cast<float>(m_nativeOutput.at(sourceSample));
            pcm[destinationSample + 1] = static_cast<float>(m_nativeOutput.at(sourceSample + 1));
        }
        m_outputCarryOffsetFrames += frames;
        destinationFrame += frames;
    }
    return pcm;
#else
    return {};
#endif
}

bool RemoteAudioRateMatcher::canTakeWithoutUnderflow() const
{
#ifdef HAVE_WDSP
    if (!m_matcher) {
        return false;
    }
    int underflows = 0;
    int overflows = 0;
    double ratio = 1.0;
    int capacity = 0;
    int ringFill = 0;
    getRMatchDiags(m_matcher, &underflows, &overflows, &ratio, &capacity, &ringFill);
    Q_UNUSED(underflows);
    Q_UNUSED(overflows);
    Q_UNUSED(ratio);
    Q_UNUSED(capacity);
    const int carry = m_outputCarryFrames - m_outputCarryOffsetFrames;
    const int remaining = std::max(0, m_outputFrames - carry);
    const int nativeFrames = ((remaining + kThetisNativeBlockFrames - 1)
                              / kThetisNativeBlockFrames)
        * kThetisNativeBlockFrames;
    return ringFill >= nativeFrames;
#else
    return false;
#endif
}

void RemoteAudioRateMatcher::reset()
{
    if (m_inputFrames == 0 || m_outputFrames == 0 || m_ringFrames == 0) {
        return;
    }

    const int inputFrames = m_inputFrames;
    const int outputFrames = m_outputFrames;
    const int ringFrames = m_ringFrames;
    configure(inputFrames, outputFrames, ringFrames);
}

RemoteAudioRateMatcherStats RemoteAudioRateMatcher::stats() const
{
    RemoteAudioRateMatcherStats result;
#ifdef HAVE_WDSP
    if (m_matcher) {
        // From Thetis Project Files/Source/ChannelMaster/ivac.c:718 [v2.10.3.15 @3759d09]
        // — getRMatchDiags(a, underflows, overflows, var, ringsize, nring).
        getRMatchDiags(m_matcher, &result.underflows, &result.overflows,
                       &result.currentRatio, &result.ringCapacityFrames,
                       &result.ringFillFrames);
        // xrmatchOUT removes a 64-frame native block before take() returns
        // its public-sized prefix. Include the bounded suffix so callers see
        // the fill available to playback, rather than artificial free room.
        result.ringFillFrames += m_outputCarryFrames - m_outputCarryOffsetFrames;
    }
#endif
    return result;
}

void RemoteAudioRateMatcher::destroy() noexcept
{
#ifdef HAVE_WDSP
    if (m_matcher) {
        destroy_rmatchV(m_matcher);
    }
#endif
    m_matcher = nullptr;
    m_inputFrames = 0;
    m_outputFrames = 0;
    m_ringFrames = 0;
    m_inputCarry.clear();
    m_nativeOutput.clear();
    m_inputCarryFrames = 0;
    m_outputCarryOffsetFrames = 0;
    m_outputCarryFrames = 0;
}

} // namespace NereusSDR
