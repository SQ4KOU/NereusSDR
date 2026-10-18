// =================================================================
// src/core/audio/MicUplinkCollector.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  See MicUplinkCollector.h.
//
// Modification history (NereusSDR):
//   2026-10-10: created.  J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//               Claude Code.
//   2026-10-12: collectOnce() counts its wakes and frames.  J.J. Boyd
//               (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-15: stop() lets a thread still waiting on its priority request
//               go instead of joining it.  J.J. Boyd (KG4VCF), AI-assisted
//               via Anthropic Claude Code.
// =================================================================

#include "core/audio/MicUplinkCollector.h"

#include "core/audio/RealtimeAudioPriority.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <utility>

namespace NereusSDR {

// Whether the thread got past its priority request before stop() came.
// Whichever of the two takes the lock first decides: the thread runs and
// stop() joins it, or stop() lets it go and it never touches the collector.
struct MicUplinkCollector::ThreadGate {
    std::mutex mutex;
    bool running = false;
    bool letGo = false;
};

MicUplinkCollector::MicUplinkCollector(Source source, int wakeIntervalMs)
    : MicUplinkCollector(std::move(source), wakeIntervalMs,
                         PriorityCalls{ &elevateAudioThreadPriority, &leaveAudioThreadPriority })
{
}

MicUplinkCollector::MicUplinkCollector(Source source, int wakeIntervalMs, PriorityCalls priority)
    : m_source(std::move(source))
    , m_wakeIntervalMs(std::max(1, wakeIntervalMs))
    , m_priority(std::move(priority))
    , m_ring(std::make_unique<std::atomic<float>[]>(static_cast<std::size_t>(kQueueFrames)))
{
    for (int i = 0; i < kQueueFrames; ++i) {
        m_ring[static_cast<std::size_t>(i)].store(0.0f, std::memory_order_relaxed);
    }
}

MicUplinkCollector::~MicUplinkCollector()
{
    stop();
}

void MicUplinkCollector::start()
{
    if (m_thread.joinable() || !m_source) {
        return;
    }
    // Nothing collected for an earlier run may follow into this one.
    discardQueued();
    m_stopRequested.store(false, std::memory_order_release);
    while (m_stopWake.tryAcquire(1)) {
    }
    m_gate = std::make_shared<ThreadGate>();
    // Until it is past the gate the thread uses only what it holds itself:
    // stop() may let it go, and the collector may be gone, before the
    // priority request returns.
    m_thread = std::thread([this, gate = m_gate, priority = m_priority] {
        // Real-time audio priority, as the local transmit pump takes it
        // (TxWorkerThread::run): asked for on this thread, given back on it
        // before it ends.  nullptr is a refusal; the thread runs regardless.
        AudioPriorityToken* token = priority.elevate();
        bool letGo = false;
        {
            std::lock_guard<std::mutex> lock(gate->mutex);
            letGo = gate->letGo;
            gate->running = !letGo;
        }
        if (letGo) {
            priority.leave(token);
            return;
        }
        run(token);
    });
}

MicUplinkCollector::StopOutcome MicUplinkCollector::stop()
{
    StopOutcome outcome = StopOutcome::NotRunning;
    if (m_thread.joinable()) {
        bool letGo = false;
        {
            std::lock_guard<std::mutex> lock(m_gate->mutex);
            if (!m_gate->running) {
                m_gate->letGo = true;
                letGo = true;
            }
        }
        if (letGo) {
            // Still waiting on its priority request (Core Audio on the Mac):
            // it ends on its own once answered.
            m_thread.detach();
            outcome = StopOutcome::LeftWaitingOnPriority;
        } else {
            m_stopRequested.store(true, std::memory_order_release);
            m_stopWake.release();
            m_thread.join();
            outcome = StopOutcome::Joined;
        }
        m_gate.reset();
    }
    discardQueued();
    return outcome;
}

void MicUplinkCollector::run(AudioPriorityToken* priority)
{
    m_elevated.store(priority != nullptr, std::memory_order_release);

    using Clock = std::chrono::steady_clock;
    const auto interval = std::chrono::milliseconds(m_wakeIntervalMs);
    Clock::time_point next = Clock::now() + interval;
    while (!m_stopRequested.load(std::memory_order_acquire)) {
        collectOnce();
        // The wake keeps to the clock, not to how long the pull took; a
        // wake that comes late starts its count again from now.
        const Clock::time_point now = Clock::now();
        if (next <= now) {
            next = now + interval;
        }
        const auto waitMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            next - now + std::chrono::microseconds(999)).count();
        next += interval;
        // stop() releases the semaphore, so a stop never waits out the wake.
        if (m_stopWake.tryAcquire(1, static_cast<int>(waitMs))) {
            break;
        }
    }

    m_elevated.store(false, std::memory_order_release);
    m_priority.leave(priority);
}

int MicUplinkCollector::collectOnce()
{
    if (!m_source) {
        return 0;
    }
    m_wakes.fetch_add(1, std::memory_order_relaxed);
    int total = 0;
    for (int pull = 0; pull < kMaxPullsPerWake; ++pull) {
        const int got = std::min(m_source(m_scratch.data(), kPullFrames), kPullFrames);
        if (got <= 0) {
            break;
        }
        push(m_scratch.data(), got);
        total += got;
        // A short pull means nothing more is waiting.
        if (got < kPullFrames) {
            break;
        }
    }
    if (total > 0) {
        m_collected.fetch_add(static_cast<quint64>(total), std::memory_order_relaxed);
    }
    return total;
}

void MicUplinkCollector::push(const float* src, int frames)
{
    const std::uint64_t write = m_write.load(std::memory_order_relaxed);
    const std::uint64_t count = static_cast<std::uint64_t>(frames);
    // Full: the oldest frames go.  The read index moves first, so a
    // consumer copying those very frames sees its own move fail and takes
    // them again from the new place.
    std::uint64_t read = m_read.load(std::memory_order_acquire);
    for (;;) {
        const std::uint64_t free = static_cast<std::uint64_t>(kQueueFrames) - (write - read);
        if (count <= free) {
            break;
        }
        const std::uint64_t drop = count - free;
        if (m_read.compare_exchange_weak(read, read + drop, std::memory_order_acq_rel,
                                         std::memory_order_acquire)) {
            m_dropped.fetch_add(drop, std::memory_order_relaxed);
            break;
        }
    }
    for (std::uint64_t i = 0; i < count; ++i) {
        m_ring[static_cast<std::size_t>((write + i) & kMask)].store(src[i],
                                                                    std::memory_order_relaxed);
    }
    m_write.store(write + count, std::memory_order_release);
}

int MicUplinkCollector::drain(float* dst, int maxFrames)
{
    if (dst == nullptr || maxFrames <= 0) {
        return 0;
    }
    for (;;) {
        std::uint64_t read = m_read.load(std::memory_order_acquire);
        const std::uint64_t write = m_write.load(std::memory_order_acquire);
        const std::uint64_t waiting = write - read;
        if (waiting == 0) {
            return 0;
        }
        if (waiting > static_cast<std::uint64_t>(kQueueFrames)) {
            continue;   // the producer dropped between the two reads
        }
        const std::uint64_t take = std::min<std::uint64_t>(waiting,
                                                           static_cast<std::uint64_t>(maxFrames));
        for (std::uint64_t i = 0; i < take; ++i) {
            dst[i] = m_ring[static_cast<std::size_t>((read + i) & kMask)].load(
                std::memory_order_relaxed);
        }
        // Fails only when the producer dropped frames meanwhile, and then
        // what was copied may be overwritten: take again.
        if (m_read.compare_exchange_strong(read, read + take, std::memory_order_acq_rel,
                                           std::memory_order_acquire)) {
            return static_cast<int>(take);
        }
    }
}

void MicUplinkCollector::discardQueued()
{
    std::uint64_t read = m_read.load(std::memory_order_acquire);
    for (;;) {
        const std::uint64_t write = m_write.load(std::memory_order_acquire);
        if (read == write
            || m_read.compare_exchange_weak(read, write, std::memory_order_acq_rel,
                                            std::memory_order_acquire)) {
            return;
        }
    }
}

int MicUplinkCollector::queuedFrames() const
{
    const std::uint64_t read = m_read.load(std::memory_order_acquire);
    const std::uint64_t write = m_write.load(std::memory_order_acquire);
    return write >= read
        ? static_cast<int>(std::min<std::uint64_t>(write - read,
                                                   static_cast<std::uint64_t>(kQueueFrames)))
        : 0;
}

} // namespace NereusSDR
