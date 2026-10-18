// =================================================================
// src/core/audio/VaxTxMicSource.h  (NereusSDR)
// =================================================================
//
// NereusSDR-original file. No Thetis logic ported here — Thetis has
// VAC/IVAC for this role and uses a fundamentally different pipeline
// (named pipes + SetupForm). VaxTxMicSource is a NereusSDR-native
// thin shim that implements the TxMicRouter interface by dispatching
// to AudioEngine::pullVaxTxMic, the accessor that drains the VAX TX
// shared-memory bus.
//
// Design mirror: PcMicSource. Both classes are stateless dispatchers;
// the heavy lifting lives in AudioEngine. The split into VaxTxMicSource
// + AudioEngine::pullVaxTxMic keeps the audio-thread contract explicit:
// `pullSamples` is the WDSP-thread entry point, `pullVaxTxMic` is the
// AudioEngine-internal pull implementation (format conversion +
// stereo→mono downmix lives in the engine).
//
// This is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
// 02110-1301, USA.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-06 — Original implementation for NereusSDR by J.J. Boyd
//                 (KG4VCF), VAX TX → mic-source wiring, with
//                 AI-assisted implementation via Anthropic Claude
//                 Code (eager-borg-d64bed). Closes the Phase 3M
//                 VAX-TX-consumer TODO referenced from
//                 AudioEngine.cpp:306 ("pull TX audio from
//                 m_vaxTxBus when [...] consumer that pulls from
//                 m_vaxTxBus / mic lives — Sub-Phase 9").
// =================================================================

// no-port-check: NereusSDR-original file; no Thetis logic ported here.

#pragma once

#include "core/TxMicRouter.h"

namespace NereusSDR {

class AudioEngine;

/// VaxTxMicSource — TxMicRouter implementation that taps the
/// AudioEngine VAX-TX bus (m_vaxTxBus) via AudioEngine::pullVaxTxMic.
///
/// When a 3rd-party app writes audio to the "NereusSDR TX" CoreAudio
/// device on macOS (or the equivalent virtual sink on Linux/PipeWire),
/// the HAL plugin captures it into /nereussdr-vax-tx shared memory.
/// VaxTxMicSource pulls from that shm at WDSP-thread cadence, downmixes
/// stereo→mono, and feeds it as the TX mic input.
///
/// Lifetime: constructor takes a non-owning AudioEngine pointer. The
/// VaxTxMicSource holds no state of its own; pullSamples() dispatches
/// directly to AudioEngine::pullVaxTxMic. Audio-thread-safe (no
/// allocations, no blocking; just a thread_local scratch buffer inside
/// the engine accessor). Returns 0 on null AudioEngine.
class VaxTxMicSource : public TxMicRouter {
public:
    explicit VaxTxMicSource(AudioEngine* engine);
    ~VaxTxMicSource() override = default;

    /// Pull `n` mono float samples from AudioEngine's VAX-TX bus.
    /// Dispatches to AudioEngine::pullVaxTxMic(dst, n). Returns the
    /// sample count actually written (0..n). Returns 0 if m_engine
    /// is null.
    ///
    /// Called from the WDSP audio thread — must not block or allocate.
    int pullSamples(float* dst, int n) override;

private:
    AudioEngine* m_engine;  // non-owning; lifetime managed by RadioModel
};

} // namespace NereusSDR
