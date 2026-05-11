// SPDX-License-Identifier: GPL-3.0-or-later
//
// =================================================================
// src/core/RadeChannel.cpp  (NereusSDR)
// =================================================================
//
// NereusSDR - RadeChannel implementation. Skeleton bodies at Phase
// 3R Task I1; I2 / I3 / I4 will fill in the DSP slot bodies.
//
// Ported from sources (hybrid):
//   Structure: AetherSDR src/core/RADEEngine.cpp [@0cd4559]
//     Ctor / dtor pair, idempotent start()/stop() guards on the
//     active flag, isActive() / isSynced() accessors. See
//     RadeChannel.h for the full AetherSDR-block attribution.
//   DSP API:   freedv-gui src/pipeline/RADEReceiveStep.cpp,
//              src/pipeline/RADETransmitStep.cpp        [@77e793a]
//     The slot-body call sequences that Tasks I2 / I3 will plug in
//     (rade_rx with inputBufCplx_ + featuresOut_ + rade_nin frame
//     readiness check; rade_tx with featureList_ + 12-frame
//     accumulation; LPCNet feature extractor lifecycle; FARGAN
//     vocoder warm-up; embedded rade_text aux channel) all come
//     from these two freedv-gui pipeline-step files.
//
// License (upstream):
//   - AetherSDR has no per-file copyright header, so per
//     docs/attribution/HOW-TO-PORT.md rule 6 we cite the project URL
//     and primary author at NereusSDR block level rather than copying
//     a verbatim header that does not exist:
//       Copyright (C) 2024-2026  Jeremy (KK7GWY) / AetherSDR contributors
//         - per https://github.com/ten9876/AetherSDR (GPLv3; see
//           LICENSE and About dialog for the live contributor list)
//   - freedv-gui carries an LGPLv2.1+ root license (`freedv-gui/COPYING`).
//     The specific RADEReceiveStep.cpp and RADETransmitStep.cpp
//     files each carry a permissive BSD-2-Clause-style file header
//     (Copyright Mooneer Salem, no per-file project copyright line);
//     the BSD permission block is reproduced verbatim below per the
//     upstream redistribution clause. Both files carry the same
//     header text; we reproduce it once.
//
// --- From freedv-gui/src/pipeline/RADEReceiveStep.cpp ---
// --- From freedv-gui/src/pipeline/RADETransmitStep.cpp ---
//
//=========================================================================
// Name:            RADEReceiveStep.cpp
// Purpose:         Describes a demodulation step in the audio pipeline.
//
// Authors:         Mooneer Salem
// License:
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
// - Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// - Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
// OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//=========================================================================
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-11  J.J. Boyd / KG4VCF  Phase 3R Task I1. See
//                 RadeChannel.h for the full attribution block.
//                 Skeleton implementation: lifecycle bodies
//                 (ctor/dtor pair, start() path-exists check +
//                 active-flag flip, stop() active-flag unflip, the
//                 isActive() / isSynced() accessors) ported from
//                 AetherSDR src/core/RADEEngine.cpp:18-124
//                 [@0cd4559]. DSP-bearing slot bodies (processIq /
//                 txEncode / resetTx) are TODO-marked for Phase 3R
//                 Tasks I2 / I3 with inline cites pointing at the
//                 freedv-gui RADEReceiveStep::execute /
//                 RADETransmitStep::execute / RADETransmitStep::reset
//                 line ranges they will follow. AI tooling: Anthropic
//                 Claude Code.
// =================================================================

#include "core/RadeChannel.h"

#include <QFile>

// Pull in the NereusSDR-native helper definitions so the
// std::unique_ptr<Resampler> and std::unique_ptr<RadeText> members
// can be destroyed. They are forward-declared in RadeChannel.h to
// keep the freedv-gui / opus include surface out of every callsite.
#include "core/Resampler.h"
#include "core/RadeText.h"

namespace NereusSDR {

// From AetherSDR src/core/RADEEngine.cpp:18-25 [@0cd4559]
//   Trivial QObject ctor; dtor calls stop() so a destruction
//   without an explicit stop() still unwinds the RADE handles.
//   I1 skeleton: the dtor is defined out-of-line here so the
//   forward-declared std::unique_ptr<Resampler>/<RadeText>
//   members can resolve their destructors (they default-construct
//   to nullptr; deleting nullptr is a no-op).
RadeChannel::RadeChannel(QObject* parent)
    : QObject(parent)
{
}

RadeChannel::~RadeChannel()
{
    stop();
}

// From AetherSDR src/core/RADEEngine.cpp:27-78 [@0cd4559]
//   Skeleton at I1: validate that the model path exists, flip
//   m_active to true, and return success. I2 will fill in the
//   real rade_open + lpcnet_encoder_create + fargan_init +
//   resampler-construction sequence following the AetherSDR
//   start() body (lines 32-72) and the freedv-gui RADEReceiveStep
//   ctor (RADEReceiveStep.cpp:35-150 [@77e793a]).
//
//   NereusSDR divergence: start() takes a model-path argument
//   instead of AetherSDR's hard-coded "dummy" first arg to
//   rade_open. The OpenHPSDR client lets the user pick the .f32
//   model file in Setup -> RADE, so the path is a runtime input.
bool RadeChannel::start(const QString& modelPath)
{
    if (m_active) {
        // Idempotent: a second start() with the same channel already
        // running is a no-op success (matches AetherSDR's `if (m_rade)
        // return true` guard at RADEEngine.cpp:30 [@0cd4559]).
        return true;
    }
    if (modelPath.isEmpty() || !QFile::exists(modelPath)) {
        return false;
    }

    // TODO Phase 3R I2/I3: open the RADE codec, initialise the
    // LPCNet feature extractor + FARGAN vocoder, build the
    // resampler chain. Follows AetherSDR RADEEngine.cpp:32-72
    // [@0cd4559] for the call sequence + cleanup-on-failure
    // pattern, and freedv-gui RADEReceiveStep.cpp:35-150 +
    // RADETransmitStep.cpp:30-130 [@77e793a] for the DSP API
    // surface (rade_n_features_in_out, rade_n_tx_out, rade_nin
    // queries; lpcnet_encoder_create; fargan_init).
    m_active = true;
    return true;
}

// From AetherSDR src/core/RADEEngine.cpp:80-106 [@0cd4559]
//   Skeleton at I1: flip m_active back to false. I2 will fill in
//   the real lpcnet_encoder_destroy + fargan-delete + rade_close +
//   rade_finalize unwind following the AetherSDR stop() body
//   (lines 82-105) and the freedv-gui RADEReceiveStep dtor
//   (RADEReceiveStep.cpp:152-175 [@77e793a]).
void RadeChannel::stop()
{
    if (!m_active) {
        return;
    }

    // TODO Phase 3R I2/I3: tear down LPCNet encoder, FARGAN
    // vocoder, RADE handle, and resamplers in reverse order. Clear
    // the TX/RX accumulator QByteArrays. See AetherSDR
    // RADEEngine.cpp:82-105 [@0cd4559] for the exact ordering.

    m_active = false;
    m_synced = false;
    m_farganWarmedUp = false;
}

// From AetherSDR src/core/RADEEngine.cpp:108-115 [@0cd4559]
bool RadeChannel::isActive() const
{
    return m_active;
}

// From AetherSDR src/core/RADEEngine.cpp:117-124 [@0cd4559]
bool RadeChannel::isSynced() const
{
    return m_synced;
}

// I1 leaves the RX path body empty. The real body lands at
// Phase 3R Task I2 and follows freedv-gui RADEReceiveStep::execute
// (RADEReceiveStep.cpp:200-330 [@77e793a]): accumulate input I/Q
// into m_rxAccum, when rade_nin() samples are available call
// rade_rx, accumulate the decoded features into m_rxFeatAccum,
// when 4 frames of features are available call FARGAN to synthesise
// 16 kHz speech, upsample to 24 kHz stereo, emit rxSpeechReady.
// AetherSDR's feedRxAudio (RADEEngine.cpp:200-310 [@0cd4559])
// is the AetherSDR-shaped variant of the same logic.
void RadeChannel::processIq(const QByteArray& iqSamples)
{
    // TODO Phase 3R I2: implement DSP body.
    Q_UNUSED(iqSamples);
}

// I1 leaves the TX path body empty. The real body lands at
// Phase 3R Task I3 and follows freedv-gui RADETransmitStep::execute
// (RADETransmitStep.cpp:150-260 [@77e793a]): accumulate input
// speech samples into m_txAccum, when LPCNet has a frame worth
// run the encoder, accumulate features into m_txFeatAccum, when
// 12 frames are available call rade_tx to produce 8 kHz RADE_COMP
// modem samples, upsample to 24 kHz stereo, emit txModemReady.
// AetherSDR's feedTxAudio (RADEEngine.cpp:130-200 [@0cd4559]) is
// the AetherSDR-shaped variant of the same logic.
void RadeChannel::txEncode(const QByteArray& speechSamples)
{
    // TODO Phase 3R I3: implement DSP body.
    Q_UNUSED(speechSamples);
}

// From AetherSDR src/core/RADEEngine.cpp:126-150 [@0cd4559]
//   Skeleton at I1: empty body. I3 will fill in the TX-accumulator
//   clear + lpcnet_encoder_reset call sequence following the
//   AetherSDR resetTx body and the freedv-gui RADETransmitStep::reset
//   (RADETransmitStep.cpp:265-280 [@77e793a]).
void RadeChannel::resetTx()
{
    // TODO Phase 3R I3: implement reset body.
}

}  // namespace NereusSDR
