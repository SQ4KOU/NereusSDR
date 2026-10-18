// =================================================================
// src/core/session/media/DaemonAudioSendThread.h  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  The thread that encodes and sends a
// headless Core's receive audio (R-AUD-SEND); no Thetis logic.
//
// Why it exists (bench, 2026-10-10, a window on a remote Core): the senders
// were drained by a 10 ms timer on the Core's main thread.  Whenever that
// thread was busy the four-block capture ring (160 ms) filled and dropped,
// and the listener got bursts after gaps of 170 to 446 ms.  This thread
// drains them whatever the main thread does, and wakes the moment the DSP
// thread queues a block.
//
// Modification history (NereusSDR):
//   2026-10-10: created.  J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//               Claude Code.
// =================================================================

#pragma once

#include "core/session/media/DaemonAudioSource.h"

#include <QtGlobal>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace NereusSDR {

class DaemonAudioSender;

// One per DaemonMediaController, serving all of its senders.
//
//   wake()    what each served sender's capture bridge is told to wake
//             (DaemonAudioSender::setBlockWake), before the sender starts.
//   add()     the thread drains this sender from now on.  The first add
//             starts the thread, named "NereusAudioTx"; it asks for
//             real-time audio priority on itself (RealtimeAudioPriority.h; a
//             refusal is a soft failure and it runs anyway) and gives it
//             back before it ends.
//   remove()  after it returns the thread never touches the sender again.
//             It waits only for a drain of that very sender that is under
//             way; stop the sender first and that is one block at most.
//   stop()    wakes the thread and joins it.  The destructor calls it.
//
// The loop: wait until a block is queued on any served sender's source, or
// DaemonAudioSender::kDrainIntervalMs passes (the lossless pacing's tick),
// then drain each sender once.  With no sender it waits without a timeout.
// It holds no lock of its own while a sender drains.
class DaemonAudioSendThread final {
public:
    DaemonAudioSendThread();
    ~DaemonAudioSendThread();
    DaemonAudioSendThread(const DaemonAudioSendThread&) = delete;
    DaemonAudioSendThread& operator=(const DaemonAudioSendThread&) = delete;

    // ── Owner's thread ──────────────────────────────────────────────────
    std::shared_ptr<DaemonAudioBlockWake> wake() const { return m_wake; }
    void add(DaemonAudioSender* sender);
    void remove(DaemonAudioSender* sender);
    void stop();
    bool isRunning() const { return m_thread.joinable(); }
    // Whether the running thread got its real-time priority (false when not
    // running, before the thread has asked, or when the system refused).
    bool realtimePriorityHeld() const { return m_elevated.load(std::memory_order_acquire); }
    // Passes over the senders completed (diagnostics and tests).
    quint64 passes() const { return m_passes.load(std::memory_order_acquire); }
#ifdef NEREUS_BUILD_TESTS
    // Test seam, no production caller: the longest the thread waits for a
    // block before it drains anyway (DaemonAudioSender::kDrainIntervalMs
    // unless set).  Only before the first add(), which starts the thread.
    void setTickMsForTest(int tickMs) { m_tickMs = tickMs; }
#endif

private:
    void run();

    const std::shared_ptr<DaemonAudioBlockWake> m_wake;
    // Guards m_senders and m_draining.  Never held while a sender drains.
    std::mutex m_mutex;
    std::condition_variable m_drained;
    std::vector<DaemonAudioSender*> m_senders;
    DaemonAudioSender* m_draining{nullptr};
    // Written only before the thread starts; the thread reads it.
    int m_tickMs;
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_elevated{false};
    std::atomic<quint64> m_passes{0};
    std::thread m_thread;
};

} // namespace NereusSDR
