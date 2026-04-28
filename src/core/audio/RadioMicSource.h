// =================================================================
// src/core/audio/RadioMicSource.h  (NereusSDR)
// =================================================================
//
// NereusSDR-original file. No Thetis logic ported here — Thetis bakes
// mic-source selection directly into audio.cs rather than using the
// strategy pattern. RadioMicSource is a NereusSDR-native implementation
// of TxMicRouter that drains a lock-free SPSC ring fed by
// RadioConnection::micFrameDecoded (added in 3M-1b Task F.4).
//
// Design: docs/architecture/phase3m-tx-epic-master-design.md §5.2.1
// Plan:   3M-1b Task F.2.
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
//                 (KG4VCF), Phase 3M-1b Task F.2, with AI-assisted
//                 implementation via Anthropic Claude Code.
// =================================================================

// no-port-check: NereusSDR-original file; no Thetis logic ported here.

#pragma once

#include "core/TxMicRouter.h"

#include <QObject>
#include <array>
#include <atomic>

namespace NereusSDR {

class RadioConnection;

/// RadioMicSource — TxMicRouter implementation that drains a lock-free
/// SPSC float ring fed by RadioConnection::micFrameDecoded.
///
/// Threading model (SPSC):
///   Producer: RadioConnection (network I/O thread) emits micFrameDecoded
///             via DirectConnection → onMicFrame slot pushes to ring.
///   Consumer: WDSP audio thread calls pullSamples() to drain the ring.
///
/// Ring design: 4096 float samples (~85 ms at 48 kHz). Power-of-two
/// capacity allows bitwise masking instead of modulo for index wrap.
/// Monotonic unsigned counters avoid UB on overflow (unsigned overflow
/// is defined by the C++ standard; the effective index is obtained via
/// `idx & kMask`). At 48 kHz mono, a counter increments at 48000/s.
/// A 32-bit unsigned counter wraps after 2^32 / 48000 ≈ 24.8 hours —
/// harmless because the index extraction `& kMask` sees only the low
/// bits. Reads and writes remain coherent across the wrap.
///
/// Overflow handling: when the ring is full, onMicFrame drops the
/// excess samples and increments m_dropped for diagnostics. This is
/// more appropriate than the yielding pushCopy pattern (AudioRingSpsc)
/// because onMicFrame runs on the connection thread which must not
/// block the network I/O loop.
///
/// Underrun handling: pullSamples zero-fills the remainder when fewer
/// samples are available than requested. Zero-fill produces silence,
/// which is more user-friendly than repeating stale audio or glitching.
/// pullSamples always returns `n` (the requested count) regardless of
/// underrun — the audio pipeline always gets a full buffer.
///
/// Constructor takes a non-owning RadioConnection*. Passing nullptr is
/// safe (no slot is connected; pullSamples always returns zero-fill).
///
/// Plan: 3M-1b F.2. Master design §5.2.1.
class RadioMicSource : public QObject, public TxMicRouter {
    Q_OBJECT
public:
    explicit RadioMicSource(RadioConnection* connection,
                            QObject* parent = nullptr);
    ~RadioMicSource() override = default;

    /// Pull `n` mono float samples into `dst`. Always returns `n`:
    /// fills from the ring when data is available, zero-fills the rest
    /// on underrun. Returns 0 only if `dst` is null or `n <= 0`.
    ///
    /// Called from the WDSP audio thread — lock-free, no allocation.
    int pullSamples(float* dst, int n) override;

    // Ring capacity: 4096 float samples (~85 ms at 48 kHz mono).
    static constexpr unsigned int kRingCapacity = 4096u;

#ifdef NEREUS_BUILD_TESTS
    /// Number of samples currently in the ring (consumer-visible fill).
    int ringFillForTest() const;
    /// Cumulative count of samples dropped due to ring overflow.
    int ringDroppedForTest() const;
#endif

public slots:
    /// Receive a decoded mic frame from RadioConnection::micFrameDecoded.
    /// Copies up to `frames` samples into the ring; drops excess on overflow.
    ///
    /// Wired by RadioModel (L.1) with QueuedConnection so this slot runs on
    /// RadioMicSource's thread affinity (main thread) regardless of which
    /// thread emits micFrameDecoded (connection thread). Lock-free ring push
    /// — safe to call from any thread, but queued dispatch documents intent.
    ///
    /// Previously declared private slots (F.2 design expected self-wiring in
    /// the constructor, but L.1 moves wiring to RadioModel so the slot must
    /// be accessible from outside the class).
    void onMicFrame(const float* samples, int frames);

private:
    RadioConnection* m_connection;  // non-owning; lifetime managed by RadioModel

    // SPSC ring storage. Index arithmetic uses unsigned monotonic counters
    // and bitwise masking (kMask = kRingCapacity - 1) for power-of-two wrap.
    static constexpr unsigned int kMask = kRingCapacity - 1u;
    std::array<float, kRingCapacity> m_ring{};

    // Producer-owned: written by onMicFrame (connection thread),
    // read (with acquire) by pullSamples (audio thread).
    std::atomic<unsigned int> m_writeIdx{0u};

    // Consumer-owned: written by pullSamples (audio thread),
    // read (with acquire) by onMicFrame (connection thread).
    std::atomic<unsigned int> m_readIdx{0u};

    // Diagnostic counter: incremented when onMicFrame drops samples
    // because the ring is full. Accessible via test seam only.
    std::atomic<unsigned int> m_dropped{0u};
};

} // namespace NereusSDR
