// =================================================================
// src/core/session/media/DaemonDisplaySendThread.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  See DaemonDisplaySendThread.h.
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

#include "core/session/media/DaemonDisplaySendThread.h"

#include "core/session/media/DisplayBudget.h"

#include <QLoggingCategory>

#include <algorithm>
#include <atomic>
#include <utility>

#if defined(Q_OS_MAC) || defined(Q_OS_LINUX)
#include <pthread.h>
#endif
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace NereusSDR {

Q_DECLARE_LOGGING_CATEGORY(lcDaemonMedia)

namespace {

void nameThisThread()
{
#if defined(Q_OS_MAC)
    pthread_setname_np("NereusDisplayTx");
#elif defined(Q_OS_LINUX)
    pthread_setname_np(pthread_self(), "NereusDisplayTx");
#endif
}

} // namespace

// ── DaemonDisplayLock ────────────────────────────────────────────────────

void DaemonDisplayLock::lock()
{
    m_mutex.lock();
    if (m_depth++ == 0) {
        m_owner.store(std::this_thread::get_id(), std::memory_order_release);
    }
}

bool DaemonDisplayLock::tryLockFor(std::chrono::microseconds timeout)
{
    if (!m_mutex.try_lock_for(timeout)) {
        return false;
    }
    if (m_depth++ == 0) {
        m_owner.store(std::this_thread::get_id(), std::memory_order_release);
    }
    return true;
}

void DaemonDisplayLock::unlock()
{
    // The outermost hold of a thread other than the exempt one runs the
    // hook before it lets go; a hold the hook itself takes does not.
    if (m_depth == 1 && m_hook && !m_inHook
        && std::this_thread::get_id() != m_hookExempt.load(std::memory_order_acquire)) {
        m_inHook = true;
        m_hook();
        m_inHook = false;
    }
    if (--m_depth == 0) {
        m_owner.store(std::thread::id(), std::memory_order_release);
    }
    m_mutex.unlock();
}

bool DaemonDisplayLock::heldByCurrentThread() const
{
    return m_owner.load(std::memory_order_acquire) == std::this_thread::get_id();
}

void DaemonDisplayLock::setReleaseHook(ReleaseHook hook)
{
    m_hook = std::move(hook);
}

void DaemonDisplayLock::setHookExemptThread(std::thread::id hookExempt)
{
    m_hookExempt.store(hookExempt, std::memory_order_release);
}

// ── DaemonDisplaySendThread ──────────────────────────────────────────────

DaemonDisplaySendThread::DaemonDisplaySendThread()
{
#ifdef Q_OS_WIN
#ifdef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
    m_turnTimer = CreateWaitableTimerExW(nullptr, nullptr,
                                         CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                         TIMER_ALL_ACCESS);
    if (m_turnTimer == nullptr) {
        m_turnTimerMissing = "CreateWaitableTimerExW failed";
        m_turnTimerError = GetLastError();
    } else {
        m_wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (m_wakeEvent == nullptr) {
            m_turnTimerMissing = "CreateEventW failed";
            m_turnTimerError = GetLastError();
            CloseHandle(m_turnTimer);
            m_turnTimer = nullptr;
        }
    }
#else
    m_turnTimerMissing = "this build's Windows SDK has none";
#endif
#endif
}

DaemonDisplaySendThread::~DaemonDisplaySendThread()
{
    stop();
#ifdef Q_OS_WIN
    if (m_wakeEvent != nullptr) {
        CloseHandle(m_wakeEvent);
    }
    if (m_turnTimer != nullptr) {
        CloseHandle(m_turnTimer);
    }
#endif
}

void DaemonDisplaySendThread::start(Pass pass)
{
    if (m_thread.joinable()) {
        return;
    }
#ifdef Q_OS_WIN
    // Once per process, and only for a thread that starts: every
    // controller makes one, and the reason is the same for each of them.
    // A timer given up after a failure (dropTurnTimer) was reported there.
    static std::atomic<bool> s_warned{false};
    if (m_turnTimer == nullptr && m_turnTimerMissing != nullptr && !s_warned.exchange(true)) {
        qCWarning(lcDaemonMedia) << "no high-resolution timer:" << m_turnTimerMissing
                                 << "( error" << m_turnTimerError
                                 << "); the display send thread's timed turns wait"
                                    " a whole scheduler tick";
    }
#endif
    m_pass = std::move(pass);
    m_stopRequested.store(false, std::memory_order_release);
    m_thread = std::thread([this] { run(); });
    // Known on this thread from here; run() stores the same id before its
    // first pass.
    m_threadId.store(m_thread.get_id(), std::memory_order_release);
}

void DaemonDisplaySendThread::setActive(bool active)
{
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_active = active;
    }
    notifyWake();
}

void DaemonDisplaySendThread::stop()
{
    if (!m_thread.joinable()) {
        return;
    }
    m_stopRequested.store(true, std::memory_order_release);
    {
        // Under the mutex, so a wait that checked the flag just before it
        // was set has begun waiting and is woken.
        const std::lock_guard<std::mutex> lock(m_mutex);
    }
    notifyWake();
    m_thread.join();
    m_threadId.store(std::thread::id(), std::memory_order_release);
}

void DaemonDisplaySendThread::wakeFrames()
{
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_framesPending = true;
    }
    notifyWake();
}

void DaemonDisplaySendThread::wakeTurn()
{
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_turnPending = true;
    }
    notifyWake();
}

#ifdef Q_OS_WIN
void DaemonDisplaySendThread::dropTurnTimer(const char* call, unsigned long error)
{
    qCWarning(lcDaemonMedia) << "the display send thread's" << call << "failed, error" << error
                             << "; its timed turns now wait a whole scheduler tick";
    CloseHandle(m_turnTimer);
    m_turnTimer = nullptr;
}
#endif

bool DaemonDisplaySendThread::wakePending() const
{
    return m_stopRequested.load(std::memory_order_acquire) || m_framesPending || m_turnPending;
}

void DaemonDisplaySendThread::notifyWake()
{
    m_wake.notify_all();
#ifdef Q_OS_WIN
    if (m_wakeEvent != nullptr) {
        SetEvent(m_wakeEvent);
    }
#endif
}

void DaemonDisplaySendThread::waitForTurn(std::unique_lock<std::mutex>& lock,
                                          Clock::time_point nextTurn)
{
#ifdef Q_OS_WIN
    if (m_turnTimer != nullptr) {
        // The event is set after every flag change under the mutex, so a
        // wake between the check and the wait returns the wait at once.
        while (!wakePending()) {
            const auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(
                nextTurn - Clock::now()).count();
            if (remaining <= 0) {
                return;
            }
            LARGE_INTEGER due;
            due.QuadPart = -std::max<LONGLONG>(1, remaining / 100);  // relative, in 100 ns units
            if (!SetWaitableTimer(m_turnTimer, &due, 0, nullptr, nullptr, FALSE)) {
                // Given up as a failed wait is, so a timer that cannot be
                // set is not tried again and failed quietly every turn.
                dropTurnTimer("timer set", GetLastError());
                break;
            }
            const HANDLE handles[] = {m_wakeEvent, m_turnTimer};
            lock.unlock();
            const DWORD woke = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
            const DWORD error = woke == WAIT_FAILED ? GetLastError() : 0;
            lock.lock();
            if (woke == WAIT_FAILED) {
                // Not expected while the handles are open. The condition
                // variable takes the timed turns from here, so a failing
                // wait never spins.
                dropTurnTimer("timer wait", error);
                break;
            }
        }
        if (wakePending()) {
            return;
        }
    }
#endif
    m_wake.wait_until(lock, nextTurn, [this] { return wakePending(); });
}

void DaemonDisplaySendThread::run()
{
    nameThisThread();
    m_threadId.store(std::this_thread::get_id(), std::memory_order_release);
    constexpr auto kInterval = std::chrono::milliseconds(kDisplaySenderIntervalMs);
    Clock::time_point nextTurn = Clock::now() + kInterval;
    while (!m_stopRequested.load(std::memory_order_acquire)) {
        bool askedTurn = false;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if (m_active) {
                waitForTurn(lock, nextTurn);
            } else {
                m_wake.wait(lock, [this] { return wakePending() || m_active; });
                // Turns start again one interval after the thread is used.
                nextTurn = Clock::now() + kInterval;
            }
            if (m_stopRequested.load(std::memory_order_acquire)) {
                break;
            }
            m_framesPending = false;
            askedTurn = std::exchange(m_turnPending, false);
        }
        const Clock::time_point now = Clock::now();
        const bool timedTurn = now >= nextTurn;
        if (timedTurn) {
            nextTurn += kInterval;
            if (nextTurn <= now) {
                nextTurn = now + kInterval;
            }
        }
        m_pass(timedTurn, askedTurn);
        m_passes.fetch_add(1, std::memory_order_acq_rel);
    }
}

} // namespace NereusSDR
