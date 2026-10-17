// =================================================================
// src/core/session/media/DaemonDisplaySendThread.h  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  The thread that takes in a Core's
// spectrum frames and sends its displays, and the lock it shares with the
// Core's main thread; no Thetis logic.
//
// Why it exists (STRIX bench, 2026-10-15, the desktop app hosting the Core
// with Windows locked): the display sender was a 5 ms timer on the GUI main
// thread, which Direct3D held for up to half a second a frame.  The sender
// turned 2 to 4 times a second and 55 frames a second were replaced unsent,
// while nereusd beside it, with nothing drawing on its main thread, kept
// 6.7 to 9.8 ms.  This thread takes in and sends whatever the main thread
// does, as receive audio's own thread does (DaemonAudioSendThread).
// Plan: docs/architecture/2026-10-15-display-send-thread-plan.md.
//
// Modification history (NereusSDR):
//   2026-10-15: created.  J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//               Claude Code.
//   2026-10-15: Windows timed turns on a high-resolution waitable timer.
//               J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-15: a timer that cannot be set is given up with one warning,
//               as a failed wait is; a timer that cannot be made is
//               reported once per process, naming the call that failed.
//               J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-16: that report waits for a thread that starts. J.J. Boyd
//               (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#pragma once

#include <QtGlobal>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace NereusSDR {

// The lock over every display controller of one DaemonSharedSpectrum (one
// per DaemonMediaHub, or one inside a stand-alone controller).  The main
// thread holds it whenever it runs controller code; the display thread holds
// it for each pass.  Recursive, since controller code reaches controller code
// again through signals.
//
// When the main thread lets go of its outermost hold, the release hook runs
// first, still under the lock: the controllers copy the main-thread values
// their display thread reads (DaemonMediaController::refreshDisplaySnapshot),
// so the display thread never runs on values older than the controller code
// that just ran.  The hook does not run for the display thread's own holds.
//
// No audio callback, DSP thread, FFT engine thread or transport library
// thread ever takes it.
class DaemonDisplayLock final {
public:
    using ReleaseHook = std::function<void()>;

    DaemonDisplayLock() = default;
    DaemonDisplayLock(const DaemonDisplayLock&) = delete;
    DaemonDisplayLock& operator=(const DaemonDisplayLock&) = delete;

    void lock();
    // The display thread's wait: false after `timeout` without the lock.
    bool tryLockFor(std::chrono::microseconds timeout);
    void unlock();
    bool heldByCurrentThread() const;

    // Under the lock.  `hookExempt` is the thread whose releases never run
    // the hook (the display thread); an empty id exempts none.
    void setReleaseHook(ReleaseHook hook);
    void setHookExemptThread(std::thread::id hookExempt);

private:
    std::recursive_timed_mutex m_mutex;
    std::atomic<std::thread::id> m_owner{};
    std::atomic<std::thread::id> m_hookExempt{};
    // Under m_mutex.
    int m_depth{0};
    bool m_inHook{false};
    ReleaseHook m_hook;
};

// Holds a DaemonDisplayLock for its scope, keeping the lock itself alive.
class DaemonDisplayLockGuard final {
public:
    explicit DaemonDisplayLockGuard(std::shared_ptr<DaemonDisplayLock> lock)
        : m_lock(std::move(lock))
    {
        if (m_lock) {
            m_lock->lock();
        }
    }
    ~DaemonDisplayLockGuard()
    {
        if (m_lock) {
            m_lock->unlock();
        }
    }
    DaemonDisplayLockGuard(const DaemonDisplayLockGuard&) = delete;
    DaemonDisplayLockGuard& operator=(const DaemonDisplayLockGuard&) = delete;

private:
    std::shared_ptr<DaemonDisplayLock> m_lock;
};

// One per DaemonSharedSpectrum, serving all of its controllers.
//
//   start()      starts the thread, named "NereusDisplayTx", at normal
//                priority, with the pass it runs.  Idempotent.
//   setActive()  whether any controller sends from it.  Inactive, the
//                thread waits for a wake without a timeout.
//   wakeFrames() an FFT engine published a frame; any thread.
//   wakeTurn()   a display channel took its held message: one turn now,
//                besides the timed ones; any thread.
//   stop()       wakes the thread and joins it.  The destructor calls it.
//                Never call it from inside a pass.
//
// The loop: wait until a frame is published, a turn is asked for, or the
// next timed turn (kDisplaySenderIntervalMs apart) is due, then run the pass
// with whether a timed turn is due and whether one was asked for.  Timed
// turns that fell behind are not made up: the next is one interval after
// the late one.  It holds no lock of its own while the pass runs.
//
// On Windows the timed wait is on a high-resolution waitable timer, as the
// Core's transmit I/Q sender's is (P2RadioConnection): a plain timed wait
// there lasts a whole 15.6 ms scheduler tick, three intervals, where the
// station thread's sender timer this replaces kept its longest gap under
// 10 ms (nereusd on the STRIX).
class DaemonDisplaySendThread final {
public:
    // (timedTurn, askedTurn); frames are always taken in.
    using Pass = std::function<void(bool, bool)>;

    DaemonDisplaySendThread();
    ~DaemonDisplaySendThread();
    DaemonDisplaySendThread(const DaemonDisplaySendThread&) = delete;
    DaemonDisplaySendThread& operator=(const DaemonDisplaySendThread&) = delete;

    // ── Owner's thread ──────────────────────────────────────────────────
    void start(Pass pass);
    void setActive(bool active);
    void stop();
    bool isRunning() const { return m_thread.joinable(); }

    // ── Any thread ──────────────────────────────────────────────────────
    void wakeFrames();
    void wakeTurn();
    bool stopRequested() const { return m_stopRequested.load(std::memory_order_acquire); }
    // The running thread's id (empty when not running).
    std::thread::id threadId() const { return m_threadId.load(std::memory_order_acquire); }
    bool onThisThread() const { return std::this_thread::get_id() == threadId(); }
    // Passes completed (diagnostics and tests).
    quint64 passes() const { return m_passes.load(std::memory_order_acquire); }

private:
    using Clock = std::chrono::steady_clock;

    void run();
    // Under m_mutex: a frame, an asked turn or a stop is waiting.
    bool wakePending() const;
    // Under m_mutex, held on return: until a wake or nextTurn.
    void waitForTurn(std::unique_lock<std::mutex>& lock, Clock::time_point nextTurn);
    // Any thread, after a flag changed under m_mutex.
    void notifyWake();
#ifdef Q_OS_WIN
    // The display thread, after `call` failed with `error`: one warning,
    // and the condition variable takes the timed turns from then on, so a
    // failing timer never spins and never fails quietly every turn.
    void dropTurnTimer(const char* call, unsigned long error);
#endif

    std::mutex m_mutex;
    std::condition_variable m_wake;
    // Under m_mutex.
    bool m_active{false};
    bool m_framesPending{false};
    bool m_turnPending{false};
    // Written only before the thread starts; the thread reads it.
    Pass m_pass;
    std::atomic<bool> m_stopRequested{false};
    std::atomic<std::thread::id> m_threadId{};
    std::atomic<quint64> m_passes{0};
#ifdef Q_OS_WIN
    // The timed wait's high-resolution timer and the auto-reset event every
    // wake sets (HANDLEs; both null where the system has no such timer, and
    // the wait is the condition variable's).
    void* m_turnTimer{nullptr};
    void* m_wakeEvent{nullptr};
    // Why there is no timer (the call that failed, and its GetLastError),
    // for start()'s one warning.
    const char* m_turnTimerMissing{nullptr};
    unsigned long m_turnTimerError{0};
#endif
    std::thread m_thread;
};

} // namespace NereusSDR
