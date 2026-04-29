// =================================================================
// src/core/audio/MicReBlocker.cpp  (NereusSDR)
// =================================================================
//
// NereusSDR-original file.  See MicReBlocker.h for the full attribution
// block and design notes.
// =================================================================
//
// Modification history (NereusSDR):
//   2026-04-29 — Phase 3M-1c chunk L.4 — initial implementation by J.J. Boyd
//                 (KG4VCF), with AI-assisted implementation via Anthropic
//                 Claude Code.
// =================================================================

// no-port-check: NereusSDR-original file; size constants come from the
// in-tree contract between AudioEngine and TxChannel, not Thetis.

#include "MicReBlocker.h"

#include <utility>

namespace NereusSDR {

MicReBlocker::MicReBlocker(int outputBlockSize)
    : m_outputBlockSize(outputBlockSize > 0 ? outputBlockSize : kDefaultOutputBlockSize)
{
    // Reserve enough headroom for one full 720-sample push on top of a
    // worst-case (outputBlockSize - 1) partial fill.  Avoids reallocations
    // in the steady state.
    m_buf.reserve(static_cast<std::size_t>(m_outputBlockSize) + 720);
}

void MicReBlocker::setSinkCallback(SinkCallback cb)
{
    m_sink = std::move(cb);
    // A sink swap implies the previous TX cycle is over (or never started).
    // Drop any partial fill so the new sink starts on aligned data.
    reset();
}

void MicReBlocker::onMicBlock(const float* samples, int frames)
{
    if (frames <= 0) {
        return;
    }
    if (samples == nullptr) {
        // Defensive: a non-zero frame count with a null pointer is a caller
        // bug.  Skip silently — real callers always have a buffer at this
        // point (AudioEngine::micBlockReady passes `m_micBlockBuffer.data()`).
        return;
    }

    // Append.
    m_buf.insert(m_buf.end(), samples, samples + frames);

    // Drain whatever full output blocks are now available.
    drain();
}

void MicReBlocker::reset()
{
    m_buf.clear();
}

void MicReBlocker::drain()
{
    if (!m_sink) {
        // No sink installed — leave samples in the FIFO; they will be
        // emitted once a sink is attached and another onMicBlock call
        // triggers drain.  (In practice the sink is wired before any
        // onMicBlock fires, so this branch only matters for tests that
        // poke onMicBlock before setSinkCallback.)
        return;
    }

    while (m_buf.size() >= static_cast<std::size_t>(m_outputBlockSize)) {
        // Emit the head of the FIFO as one full output block.
        m_sink(m_buf.data(), m_outputBlockSize);
        // Drop the consumed samples.  vector::erase on a contiguous
        // FIFO is O(remaining); steady-state remaining is < outputBlockSize
        // so the cost is bounded and small.
        m_buf.erase(m_buf.begin(), m_buf.begin() + m_outputBlockSize);
    }
}

} // namespace NereusSDR
