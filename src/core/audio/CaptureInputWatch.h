// =================================================================
// src/core/audio/CaptureInputWatch.h  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  The microphone helper's "the input
// is lost" watch; no Thetis logic.
//
// Why it exists (2026-10-12, a Mac window on a remote Core): the helper
// called its input lost when the shared ring's last-write time stood
// still for 500 ms.  The ring stores that time only for a block it
// accepted, and it drops every block once its reader has stopped reading
// (DeviceRateMatcher, "the reader has stopped reading").  So a window
// that stopped taking the microphone for 85 ms made a working microphone
// read as lost; the window reopened it, the new ring filled the same way,
// and the microphone was opened and closed about every 1.5 s for as long
// as the reader stayed away.  This watch counts the blocks the device
// delivers instead: a reader that is away costs audio, never the input.
//
// Modification history (NereusSDR):
//   2026-10-12: created.  J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//               Claude Code.
// =================================================================

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace NereusSDR {

// One input callback (noteBlock) and one owner thread (restart, check).
//
//   noteBlock()  the device delivered a block.  Called from the input
//                callback for every block, whether or not the ring took
//                it: one relaxed atomic add, nothing else.
//   restart()    a new stream: the count so far is forgotten and the
//                silence is measured from now.
//   check()      Waiting until the first block; then Progress while blocks
//                keep coming (FirstBlock once, for the first of them);
//                Lost when none came for lostAfter.
class CaptureInputWatch final {
public:
    using Clock = std::chrono::steady_clock;

    enum class Verdict { Waiting, FirstBlock, Progress, Lost };

    void noteBlock() noexcept { m_blocks.fetch_add(1, std::memory_order_relaxed); }

    std::uint64_t blocks() const noexcept { return m_blocks.load(std::memory_order_relaxed); }

    void restart(Clock::time_point now) noexcept
    {
        m_seen = blocks();
        m_lastProgress = now;
        m_first = true;
    }

    Verdict check(Clock::time_point now, Clock::duration lostAfter) noexcept
    {
        const std::uint64_t count = blocks();
        if (count != m_seen) {
            m_seen = count;
            m_lastProgress = now;
            if (m_first) {
                m_first = false;
                return Verdict::FirstBlock;
            }
            return Verdict::Progress;
        }
        if (now - m_lastProgress >= lostAfter) {
            return Verdict::Lost;
        }
        return m_first ? Verdict::Waiting : Verdict::Progress;
    }

    Clock::time_point lastProgress() const noexcept { return m_lastProgress; }

private:
    std::atomic<std::uint64_t> m_blocks{0};   // the callback adds
    std::uint64_t m_seen = 0;                 // the owner thread only, from here down
    Clock::time_point m_lastProgress{};
    bool m_first = true;
};

} // namespace NereusSDR
