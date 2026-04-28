// =================================================================
// src/core/audio/CompositeTxMicRouter.h  (NereusSDR)
// =================================================================
//
// NereusSDR-original file. No Thetis logic ported here — Thetis bakes
// mic-source selection directly into audio.cs rather than using the
// strategy pattern. CompositeTxMicRouter is a NereusSDR-native selector
// that holds PcMicSource + RadioMicSource and dispatches pullSamples
// to the active source at runtime.
//
// Design: docs/architecture/phase3m-1b-thetis-pre-code-review.md §0.3
// (PcMicSource arch lock) + master design §5.2.1.
// Plan: 3M-1b F.3.
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
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
// 02110-1301, USA.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-27 — Original implementation for NereusSDR by J.J. Boyd
//                 (KG4VCF), Phase 3M-1b Task F.3, with AI-assisted
//                 implementation via Anthropic Claude Code.
// =================================================================

// no-port-check: NereusSDR-original file; no Thetis logic ported here.

#pragma once

#include "core/TxMicRouter.h"

#include <atomic>

namespace NereusSDR {

class PcMicSource;
class RadioMicSource;

/// MicSource — strategy-pattern enum for which mic source is active.
///
/// Pc    — PC microphone (host audio device via PortAudio/PipeWire).
///          Always available. Used by default and forced on HL2.
/// Radio — Radio mic-jack input via P1/P2 mic-frame stream.
///          Requires hasMicJack == true (not available on HL2).
enum class MicSource : int {
    Pc    = 0,  ///< PC microphone (host audio device)
    Radio = 1,  ///< Radio mic-jack input via P1/P2 mic-frame stream
};

/// CompositeTxMicRouter — selector that holds PcMicSource + RadioMicSource
/// and dispatches pullSamples based on the user's active selection.
///
/// Special behaviors:
///
/// HL2 force-PC: when hasMicJack is false (HL2 has no radio mic-jack),
/// setActiveSource(Radio) is silently ignored. The effective source
/// is always Pc on HL2 regardless of user selection.
///
/// MOX-locked: source switching while MOX is active is deferred.
/// setActiveSource() during TX records the request as "pending" and
/// applies it on the next setMoxActive(false) transition. This prevents
/// audible clicks and mid-TX mic-buffer state inconsistency.
///
/// Constructor takes non-owning pointers. The router does not own its
/// sources — lifetime is managed by the caller (RadioModel in production).
///
/// hasMicJack is collapsed with radioSource at construction: if either
/// hasMicJack is false OR radioSource is nullptr, the router behaves as
/// if Radio is unavailable (force-PC). This means passing a non-null
/// radioSource with hasMicJack=false is handled identically to null.
///
/// Audio-thread contract: pullSamples() is called from the WDSP audio
/// thread. All state is accessed via std::atomic with acquire/release
/// ordering. No blocking, no allocation.
///
/// Plan: 3M-1b F.3. Pre-code review §0.3 + master design §5.2.1.
class CompositeTxMicRouter : public TxMicRouter {
public:
    /// Construct with PcMicSource (always present) and optional
    /// RadioMicSource (null on HL2 since the radio-mic path doesn't
    /// exist). hasMicJack mirrors BoardCapabilities::hasMicJack.
    ///
    /// If hasMicJack is false OR radioSource is nullptr, the router
    /// collapses to Pc-only mode (Radio selection silently ignored).
    CompositeTxMicRouter(PcMicSource*    pcSource,
                         RadioMicSource* radioSource,
                         bool            hasMicJack);

    ~CompositeTxMicRouter() override = default;

    /// Pull `n` mono float samples into `dst`. Dispatches to the
    /// currently active source. Falls back to zero-fill only if both
    /// sources are null (should not occur in normal operation).
    ///
    /// Called from the WDSP audio thread — lock-free, no allocation.
    int pullSamples(float* dst, int n) override;

    /// User-facing source selection.
    ///
    /// The request is applied immediately when:
    ///   - hasMicJack is true (no HL2 force-PC gate), AND
    ///   - moxActive is false (no MOX-locked deferral)
    ///
    /// If the HL2 gate fires (hasMicJack false AND source == Radio),
    /// the request is silently dropped — it will never be applied.
    ///
    /// If the MOX gate fires, the request is recorded as pending and
    /// applied on the next setMoxActive(false) transition.
    void setActiveSource(MicSource source);

    /// Currently dispatching source (the actual effective source,
    /// which may differ from the last setActiveSource() request if
    /// MOX deferral is in effect).
    MicSource activeSource() const
    {
        return m_activeSource.load(std::memory_order_acquire);
    }

    /// Notify the router of MOX state changes.
    ///
    /// On MOX-off transition (wasActive && !active), any pending switch
    /// recorded during TX is applied to m_activeSource. On MOX-on
    /// transition there is no effect on the active source.
    void setMoxActive(bool active);

#ifdef NEREUS_BUILD_TESTS
    /// True iff a source switch was deferred because MOX was active
    /// and has not yet been applied. For test inspection only.
    bool hasPendingSwitchForTest() const
    {
        return m_hasPendingSwitch.load(std::memory_order_acquire);
    }
#endif

private:
    PcMicSource*    m_pcSource;     // non-owning; always present
    RadioMicSource* m_radioSource;  // non-owning; may be null (HL2)
    const bool      m_hasMicJack;   // collapsed: false if !hasMicJack || !radioSource

    // Audio-thread–accessible state. All accessed with acquire/release.
    std::atomic<MicSource> m_activeSource;     // currently dispatching source
    std::atomic<MicSource> m_pendingSource;    // queued switch (when MOX-locked)
    std::atomic<bool>      m_hasPendingSwitch {false};
    std::atomic<bool>      m_moxActive {false};
};

} // namespace NereusSDR
