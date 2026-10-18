// =================================================================
// src/core/audio/PcMicSource.h  (NereusSDR)
// =================================================================
//
// NereusSDR-original file. No Thetis logic ported here — Thetis bakes
// mic-source selection directly into audio.cs rather than using the
// strategy pattern. PcMicSource is a NereusSDR-native thin shim that
// implements the TxMicRouter interface by dispatching to
// AudioEngine::pullTxMic (added in Phase 3M-1b Task E.1).
//
// Design: docs/architecture/phase3m-1b-thetis-pre-code-review.md §0.3
// (PcMicSource arch lock) + §12.7 (architecture LOCKED).
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
//   2026-04-27 — Original implementation for NereusSDR by J.J. Boyd
//                 (KG4VCF), Phase 3M-1b Task F.1, with AI-assisted
//                 implementation via Anthropic Claude Code.
// =================================================================

// no-port-check: NereusSDR-original file; no Thetis logic ported here.

#pragma once

#include "core/TxMicRouter.h"

namespace NereusSDR {

class AudioEngine;

/// PcMicSource — TxMicRouter implementation that taps AudioEngine's
/// TX-input bus (the host PC's microphone) via AudioEngine::pullTxMic.
///
/// Constructor takes a non-owning AudioEngine pointer. The PcMicSource
/// holds no state of its own; pullSamples() dispatches directly to
/// AudioEngine::pullTxMic. Audio-thread-safe (no allocations, no blocking;
/// just dispatches to E.1's accessor). Returns 0 on null AudioEngine.
///
/// Plan: 3M-1b F.1. Pre-code review §0.3 + §12.7.
class PcMicSource : public TxMicRouter {
public:
    explicit PcMicSource(AudioEngine* engine);
    ~PcMicSource() override = default;

    /// Pull `n` mono float samples from AudioEngine's TX-input bus.
    /// Dispatches to AudioEngine::pullTxMic(dst, n). Returns the sample
    /// count actually written (0..n). Returns 0 if m_engine is null.
    ///
    /// Called from the WDSP audio thread — must not block or allocate.
    int pullSamples(float* dst, int n) override;

private:
    AudioEngine* m_engine;  // non-owning; lifetime managed by RadioModel
};

} // namespace NereusSDR
