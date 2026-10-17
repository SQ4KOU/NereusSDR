#pragma once
// tests/HighResolutionWait.h
//
// no-port-check: NereusSDR-original test helper.
//
// A wait until a steady-clock deadline that keeps sub-millisecond pacing on
// Windows, for test threads that stand in for a real-time clock (a pump, a
// sender, a feeder).
//
// With MinGW, std::this_thread::sleep_until and sleep_for last a whole
// 15.6 ms scheduler tick however short the request, and timeBeginPeriod(1)
// does not shorten them (measured on a Windows 11 laptop, 2026-10-15:
// 15.6 ms per 1.333 ms request either way). A high-resolution waitable
// timer measured 1.52 ms there, as the Core's own Windows send loops rely
// on (P1RadioConnection EP2 sender, P2RadioConnection transmit I/Q sender).
// Elsewhere this is std::this_thread::sleep_until.
//
// One per thread: the timer is set and waited on by the thread that owns it.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-15  J.J. Boyd / KG4VCF  Created for the stalled-owner case of
//                                    tst_media_transport, whose 1.33 ms
//                                    pump fell to one pull per 15.6 ms on
//                                    Windows. AI-assisted via Anthropic
//                                    Claude Code.
// =================================================================

#include <QtGlobal>

#include <chrono>
#include <thread>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace NereusSDR::Test {

class HighResolutionWait {
public:
    HighResolutionWait()
    {
#if defined(Q_OS_WIN) && defined(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION)
        m_timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                         TIMER_ALL_ACCESS);
#endif
#if defined(Q_OS_WIN)
        if (m_timer == nullptr) {
            qWarning("no high-resolution timer: waits last a whole 15.6 ms scheduler tick");
        }
#endif
    }
    ~HighResolutionWait()
    {
#if defined(Q_OS_WIN)
        if (m_timer != nullptr) {
            CloseHandle(m_timer);
        }
#endif
    }
    HighResolutionWait(const HighResolutionWait&) = delete;
    HighResolutionWait& operator=(const HighResolutionWait&) = delete;

    /// Returns at `deadline` or soon after; at once when it has passed.
    void until(std::chrono::steady_clock::time_point deadline)
    {
#if defined(Q_OS_WIN)
        if (m_timer != nullptr) {
            const auto remaining = deadline - std::chrono::steady_clock::now();
            if (remaining <= std::chrono::steady_clock::duration::zero()) {
                return;
            }
            LARGE_INTEGER due;
            // Relative, in 100 ns units, rounded up so it never wakes early.
            due.QuadPart = -static_cast<LONGLONG>(
                (std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count() + 99)
                / 100);
            if (SetWaitableTimer(m_timer, &due, 0, nullptr, nullptr, FALSE)) {
                WaitForSingleObject(m_timer, INFINITE);
                return;
            }
        }
#endif
        std::this_thread::sleep_until(deadline);
    }

private:
#if defined(Q_OS_WIN)
    HANDLE m_timer{nullptr};
#endif
};

} // namespace NereusSDR::Test
