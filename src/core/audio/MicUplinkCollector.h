// =================================================================
// src/core/audio/MicUplinkCollector.h  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  The real-time thread that collects
// the microphone for a remote window's uplink, and the lock-free queue it
// hands the audio over in; no Thetis logic.
//
// Why it exists (bench, 2026-10-10, a Windows window on a remote Core):
// the PC microphone's reader (CaptureAudioBus::pull) is paced by the
// 48 kHz clock and gives a late caller at most 30 ms, so a window that
// pulled it from its GUI thread lost the rest of every GUI stall longer
// than that, and the Core's transmit ran dry.  This thread pulls every
// 5 ms at real-time audio priority, whatever the GUI thread does, and the
// queue holds what it pulled until the GUI thread sends it.
//
// Modification history (NereusSDR):
//   2026-10-10: created.  J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//               Claude Code.
//   2026-10-12: wakes() and collectedFrames(), so the owner can report a
//               collector that takes nothing.  J.J. Boyd (KG4VCF),
//               AI-assisted via Anthropic Claude Code.
//   2026-10-15: stop() no longer waits on the thread's priority request,
//               which reads Core Audio on the Mac and can take seconds
//               there; it lets such a thread go and says so.  The priority
//               calls can be swapped for tests.  J.J. Boyd (KG4VCF),
//               AI-assisted via Anthropic Claude Code.
// =================================================================

#pragma once

#include <QSemaphore>
#include <QtGlobal>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>

namespace NereusSDR {

struct AudioPriorityToken;

// One producer (the collector thread, or collectOnce() called inline while
// the thread is not running) and one consumer (the owner's thread).
//
//   start()        starts the thread; it elevates itself to real-time audio
//                  priority (RealtimeAudioPriority.h; a refusal is a soft
//                  failure and the thread runs anyway), then every
//                  wakeIntervalMs calls collectOnce() until stop().
//   stop()         wakes the thread at once, joins it, and discards what is
//                  queued.  Safe when not running.  The source is never
//                  called after stop() returns.  A thread still waiting on
//                  its priority request is not waited for: on the Mac that
//                  request reads Core Audio, which can take seconds while
//                  Core Audio starts or a voice isolation storm runs, and
//                  the owner's thread is the GUI thread.  stop() lets it
//                  go; once answered it gives the priority back and ends
//                  without touching the collector, which may be gone.
//   collectOnce()  pulls the source until it gives a short block and queues
//                  what it gave.  No allocation, no lock, no logging.
//   drain()        the consumer takes up to maxFrames, oldest first.
//
// A full queue (the consumer away for longer than the queue holds) drops
// its OLDEST frames and counts them in droppedFrames(); the collector never
// logs, so the owner reports the count from its own thread.
class MicUplinkCollector final {
public:
    // Fills dst with up to maxFrames of 48 kHz mono float and returns how
    // many it gave (0 or less: none).  Called on the collector thread, one
    // call at a time; it must not block.
    using Source = std::function<int(float* dst, int maxFrames)>;

    static constexpr int kSampleRate = 48'000;
    // The wake, as the local transmit pump's blocks are (TxWorkerThread).
    static constexpr int kWakeIntervalMs = 5;
    // One pull's block: 20 ms.
    static constexpr int kPullFrames = 960;
    // What the queue holds: 65536 frames, 1.365 s (a power of two at least
    // one second).
    static constexpr int kQueueFrames = 1 << 16;
    // A source that fills every pull ends a wake after this many pulls.
    static constexpr int kMaxPullsPerWake = kQueueFrames / kPullFrames;

    // How the thread asks for real-time audio priority and gives it back,
    // on its own thread (RealtimeAudioPriority.h by default; tests put in
    // their own).
    struct PriorityCalls {
        std::function<AudioPriorityToken*()> elevate;
        std::function<void(AudioPriorityToken*)> leave;
    };

    // How stop() ended the run.
    enum class StopOutcome {
        NotRunning,             // there was no thread
        Joined,                 // the thread ended before stop() returned
        LeftWaitingOnPriority   // let go while it waited on its priority request
    };

    explicit MicUplinkCollector(Source source, int wakeIntervalMs = kWakeIntervalMs);
    MicUplinkCollector(Source source, int wakeIntervalMs, PriorityCalls priority);
    ~MicUplinkCollector();                       // stop()
    MicUplinkCollector(const MicUplinkCollector&) = delete;
    MicUplinkCollector& operator=(const MicUplinkCollector&) = delete;

    // ── Owner's thread ──────────────────────────────────────────────────
    void start();
    StopOutcome stop();
    bool isRunning() const { return m_thread.joinable(); }
    int drain(float* dst, int maxFrames);
    void discardQueued();
    int queuedFrames() const;
    quint64 droppedFrames() const { return m_dropped.load(std::memory_order_relaxed); }
    // 2026-10-12: how often collectOnce() ran and how many frames the
    // source gave it, for the owner's "the collector took nothing" report:
    // wakes that stand still are a thread that is not running, wakes that
    // move with no frames are a source that gives nothing.
    quint64 wakes() const { return m_wakes.load(std::memory_order_relaxed); }
    quint64 collectedFrames() const { return m_collected.load(std::memory_order_relaxed); }
    // Whether the running thread got its real-time priority (false when not
    // running, before the thread has asked, or when the system refused).
    bool realtimePriorityHeld() const { return m_elevated.load(std::memory_order_acquire); }

    // ── The producer: the thread, or one inline caller while it is not
    //    running (tests) ─────────────────────────────────────────────────
    int collectOnce();

private:
    // Shared with the thread, which may outlive the collector.
    struct ThreadGate;

    void run(AudioPriorityToken* priority);
    void push(const float* src, int frames);

    static constexpr std::uint64_t kMask = static_cast<std::uint64_t>(kQueueFrames) - 1;

    Source m_source;
    int m_wakeIntervalMs;
    PriorityCalls m_priority;
    std::unique_ptr<std::atomic<float>[]> m_ring;
    std::atomic<std::uint64_t> m_write{0};       // the producer advances it
    std::atomic<std::uint64_t> m_read{0};        // the consumer; the producer when full
    std::atomic<quint64> m_dropped{0};
    std::atomic<quint64> m_wakes{0};
    std::atomic<quint64> m_collected{0};
    std::atomic<bool> m_elevated{false};
    std::array<float, kPullFrames> m_scratch{};  // the producer only
    QSemaphore m_stopWake;
    std::atomic<bool> m_stopRequested{false};
    std::thread m_thread;
    std::shared_ptr<ThreadGate> m_gate;
};

} // namespace NereusSDR
