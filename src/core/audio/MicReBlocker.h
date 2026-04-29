// =================================================================
// src/core/audio/MicReBlocker.h  (NereusSDR)
// =================================================================
//
// NereusSDR-original file.  Adapter that re-blocks mono audio between
// AudioEngine's `micBlockReady(samples, 720)` cadence and TxChannel's
// `driveOneTxBlock(samples, 256)` contract.
//
// Why this exists:
//   • AudioEngine emits 720-sample mono blocks (Thetis cmaster.cs:495
//     [v2.10.3.13] mic stream block size; see AudioEngine.h kMicBlockFrames).
//   • TxChannel::driveOneTxBlock requires exactly m_inputBufferSize=256
//     samples per push (256 divides the WDSP r2-ring size 2048 cleanly;
//     see TxChannel.cpp E.1 contract notes).
//   • 720 doesn't divide 256, so a small accumulator is needed.  This class
//     accepts 720-sample chunks, splices into a sliding buffer, and emits
//     `blockReady(samples, 256)` for every full 256-sample slice that fits.
//
// Behaviour:
//   • Stateful FIFO of mono float samples.
//   • Each call to `onMicBlock(samples, frames)` appends `frames` samples,
//     then drains the FIFO in 256-sample chunks via `blockReady` (DirectConnection).
//   • Worked example: 720 → emit 256, 256, leftover 208 in FIFO.  Next 720 →
//     208+720=928 → emit 256, 256, 256, leftover 160.  Pattern repeats.
//
// Threading:
//   • All ops run on the audio thread (caller's thread).  No internal locking.
//   • The class is NOT a QObject — no signals/slots.  The producer thread
//     calls `onMicBlock` directly; the sink callback is registered via
//     `setSinkCallback()` (function pointer style) so that DirectConnection
//     semantics are preserved end-to-end without going through Qt's signal
//     dispatcher.
//
// =================================================================
//
// Modification history (NereusSDR):
//   2026-04-29 — Original implementation for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted implementation via Anthropic
//                 Claude Code.  Phase 3M-1c chunk L.4 — 720→256 mic
//                 re-blocker bridging AudioEngine::micBlockReady to
//                 TxChannel::driveOneTxBlock.
// =================================================================

// no-port-check: NereusSDR-original file; behaviour is dictated by the
// AudioEngine / TxChannel contract sizes, not by upstream Thetis logic.

#pragma once

#include <cstddef>
#include <functional>
#include <vector>

namespace NereusSDR {

// ---------------------------------------------------------------------------
// MicReBlocker — bridge AudioEngine 720-sample emits → TxChannel 256-sample
// pushes.
//
// Sink callback shape: `void(const float* samples, int frames)`.  Called
// once per full 256-sample chunk while draining the internal FIFO.  Frames
// is always equal to outputBlockSize() (default 256).
//
// Reset behaviour: `reset()` drops any partial fill, mirroring
// AudioEngine::clearMicBuffer for use on MOX-off.
// ---------------------------------------------------------------------------
class MicReBlocker
{
public:
    using SinkCallback = std::function<void(const float* samples, int frames)>;

    /// Default output block size: 256 (matches TxChannel m_inputBufferSize).
    static constexpr int kDefaultOutputBlockSize = 256;

    /// Construct with the desired output block size.  Reserves enough buffer
    /// to absorb one full input block (kMicBlockFrames=720) plus the worst-
    /// case partial fill (output_block_size - 1) without reallocating.
    explicit MicReBlocker(int outputBlockSize = kDefaultOutputBlockSize);

    /// Install the sink callback.  The callback must run synchronously
    /// (DirectConnection-style).  Pass {} (empty function) to clear.
    /// Setting the callback also `reset()`s the FIFO so the new sink
    /// starts on a clean accumulator.
    void setSinkCallback(SinkCallback cb);

    /// Push a mono mic block.  Appends `frames` samples to the internal
    /// FIFO and drains it in `outputBlockSize()` chunks via the sink
    /// callback.  Safe to call with `frames == 0` (no-op).  `samples`
    /// may be null only if `frames == 0`; otherwise must point to at
    /// least `frames` valid float samples.
    void onMicBlock(const float* samples, int frames);

    /// Drop any partial fill.  Mirrors AudioEngine::clearMicBuffer; use
    /// on MOX-off transitions to avoid mixing pre-MOX residue into the
    /// next TX cycle.
    void reset();

    /// Output block size in samples (default 256).
    int outputBlockSize() const noexcept { return m_outputBlockSize; }

    /// Number of samples currently buffered (partial fill).  Useful
    /// for tests; production code never reads this.
    std::size_t bufferedSamples() const noexcept { return m_buf.size(); }

private:
    /// Drain as many full output blocks as the FIFO contains.
    void drain();

    int          m_outputBlockSize;
    SinkCallback m_sink;
    /// FIFO: appended at the back, drained in `m_outputBlockSize` slices
    /// from the front via std::vector::erase.  vector is fine because
    /// the steady-state size is bounded (≤ outputBlockSize + 720).
    std::vector<float> m_buf;
};

} // namespace NereusSDR
