// =================================================================
// src/core/audio/PortAudioLibrary.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  See PortAudioLibrary.h.
//
// Modification history (NereusSDR):
//   2026-10-09: native audio plan Task 7 fix (R-AUD-06). J.J. Boyd
//               (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-09: final review fix (R-AUD-03, R-AUD-06): bounded waits
//               (lockBounded, LongHold); the counts are atomics read
//               without the lock. J.J. Boyd (KG4VCF), AI-assisted via
//               Anthropic Claude Code.
//   2026-10-12: PortAudio starts on first need, not at launch
//               (ensureStarted, addUser, removeUser). J.J. Boyd (KG4VCF),
//               AI-assisted via Anthropic Claude Code.
//   2026-10-16: Pa_Initialize marks the process's first native audio call
//               (NativeAudioFirstCall), for the persistent capture helper's
//               timing. J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude
//               Code.
// =================================================================

#include "core/audio/PortAudioLibrary.h"

#include "core/LogCategories.h"
#include "core/audio/NativeAudioFirstCall.h"
#include "core/audio/PortAudioBus.h"

#include <portaudio.h>

#include <QThread>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <utility>

namespace NereusSDR::PortAudioLibrary {

namespace {

// Changed under mutex(); read without it.
std::atomic<int> g_references{0};
std::atomic<int> g_openStreams{0};

// steady_clock nanoseconds since a LongHold took the lock; 0 while none
// holds it.
std::atomic<std::int64_t> g_longHoldSinceNs{0};
// The LongHold a bounded caller last said it gave up on, so a stuck
// driver is reported once, not on every query.
std::atomic<std::int64_t> g_reportedHoldNs{0};

constexpr std::int64_t kBoundedWaitNs = std::int64_t(kBoundedWaitMs) * 1000000;
constexpr int kPollMs = 1;

// ensureStarted()'s reference is held; changed under mutex().
std::atomic<bool> g_startedOnDemand{false};
std::atomic<int> g_users{0};
std::atomic<int> g_initializeCalls{0};
std::atomic<int> g_terminateCalls{0};

#ifdef NEREUS_BUILD_TESTS
// Set by a test before any thread uses PortAudio.
std::function<int()> g_initializeForTest;
std::function<int()> g_terminateForTest;
#endif

PaError paInitialize()
{
    g_initializeCalls.fetch_add(1, std::memory_order_relaxed);
#ifdef NEREUS_BUILD_TESTS
    if (g_initializeForTest) {
        return PaError(g_initializeForTest());
    }
#endif
    markNativeAudioCall("PortAudio start");
    return Pa_Initialize();
}

PaError paTerminate()
{
    g_terminateCalls.fetch_add(1, std::memory_order_relaxed);
#ifdef NEREUS_BUILD_TESTS
    if (g_terminateForTest) {
        return PaError(g_terminateForTest());
    }
#endif
    return Pa_Terminate();
}

std::int64_t nowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

std::recursive_mutex& mutex()
{
    static std::recursive_mutex lock;
    return lock;
}

std::unique_lock<std::recursive_mutex> lockBounded()
{
    std::unique_lock<std::recursive_mutex> lock(mutex(), std::try_to_lock);
    if (lock.owns_lock()) {
        return lock;
    }
    const std::int64_t startNs = nowNs();
    for (;;) {
        const std::int64_t now = nowNs();
        const std::int64_t heldSince = g_longHoldSinceNs.load(std::memory_order_acquire);
        if (now - startNs >= kBoundedWaitNs
            || (heldSince != 0 && now - heldSince >= kBoundedWaitNs)) {
            break;
        }
        QThread::msleep(kPollMs);
        if (lock.try_lock()) {
            return lock;
        }
    }
    const std::int64_t heldSince = g_longHoldSinceNs.load(std::memory_order_acquire);
    if (g_reportedHoldNs.exchange(heldSince, std::memory_order_acq_rel) != heldSince
        || heldSince == 0) {
        qCWarning(lcAudio) << "Older drivers: PortAudio is busy in a driver call that has"
                           << "not returned; not waiting for it";
    }
    return lock;
}

LongHold::LongHold()
    : m_lock(mutex())
{
    std::int64_t expected = 0;
    m_marked = g_longHoldSinceNs.compare_exchange_strong(expected, nowNs(),
                                                         std::memory_order_acq_rel);
}

LongHold::~LongHold()
{
    if (m_marked) {
        g_longHoldSinceNs.store(0, std::memory_order_release);
    }
}

bool acquire()
{
    std::unique_lock<std::recursive_mutex> lock = lockBounded();
    if (!lock.owns_lock()) {
        return false;
    }
    const PaError err = paInitialize();
    if (err != paNoError) {
        qCWarning(lcAudio) << "Pa_Initialize failed:" << Pa_GetErrorText(err);
        return false;
    }
    g_references.fetch_add(1, std::memory_order_acq_rel);
    return true;
}

void release()
{
    std::unique_lock<std::recursive_mutex> lock = lockBounded();
    if (!lock.owns_lock()) {
        // A driver call has not returned: Pa_Terminate now would pull
        // PortAudio away under it.  The reference stays; the process's
        // exit reclaims PortAudio.
        qCWarning(lcAudio) << "Older drivers: PortAudio left running at shutdown;"
                           << "a driver call has not returned";
        return;
    }
    if (g_references.load(std::memory_order_acquire) <= 0) {
        return;
    }
    g_references.fetch_sub(1, std::memory_order_acq_rel);
    paTerminate();
}

int references()
{
    return g_references.load(std::memory_order_acquire);
}

bool ensureStarted()
{
    if (PortAudioBus::portAudioBarredForTestRun()) {
        return false;
    }
    std::unique_lock<std::recursive_mutex> lock = lockBounded();
    if (!lock.owns_lock()) {
        return g_startedOnDemand.load(std::memory_order_acquire);
    }
    if (g_startedOnDemand.load(std::memory_order_acquire)) {
        return true;
    }
    // Already running on another start (acquire(), or the mic helper's
    // own): nothing to add.  Pa_GetHostApiCount answers from PortAudio's
    // initialization count alone and touches no device.
    if (g_references.load(std::memory_order_acquire) > 0
        || Pa_GetHostApiCount() != PaHostApiIndex(paNotInitialized)) {
        return true;
    }
    if (!acquire()) {
        return false;
    }
    g_startedOnDemand.store(true, std::memory_order_release);
    qCInfo(lcAudio) << "Older drivers: PortAudio started on first need:" << Pa_GetVersionText();
    return true;
}

bool startedOnDemand()
{
    return g_startedOnDemand.load(std::memory_order_acquire);
}

void addUser()
{
    g_users.fetch_add(1, std::memory_order_acq_rel);
}

void removeUser()
{
    if (g_users.fetch_sub(1, std::memory_order_acq_rel) > 1) {
        return;
    }
    if (!g_startedOnDemand.load(std::memory_order_acquire)) {
        return;
    }
    // release() leaves the reference, and says so, when a driver call has
    // not returned; the reference is then still ensureStarted()'s.
    const int before = g_references.load(std::memory_order_acquire);
    release();
    if (g_references.load(std::memory_order_acquire) < before) {
        g_startedOnDemand.store(false, std::memory_order_release);
    }
}

int initializeCalls()
{
    return g_initializeCalls.load(std::memory_order_relaxed);
}

int terminateCalls()
{
    return g_terminateCalls.load(std::memory_order_relaxed);
}

#ifdef NEREUS_BUILD_TESTS
void setDriverForTest(std::function<int()> initialize, std::function<int()> terminate)
{
    g_initializeForTest = std::move(initialize);
    g_terminateForTest = std::move(terminate);
}
#endif

bool reinitialize()
{
    const LongHold hold;
    const int openStreams = g_openStreams.load(std::memory_order_acquire);
    if (openStreams > 0) {
        qCWarning(lcAudio) << "Older drivers: PortAudio not listed again;" << openStreams
                           << "of its streams are still open, so the list is unchanged";
        return false;
    }
    const int references = g_references.load(std::memory_order_acquire);
    if (references <= 0) {
        return false;   // not running: the next start lists the devices present then
    }
    for (int i = 0; i < references; ++i) {
        paTerminate();
    }
    g_references.store(0, std::memory_order_release);
    for (int i = 0; i < references; ++i) {
        const PaError err = paInitialize();
        if (err != paNoError) {
            qCWarning(lcAudio) << "Older drivers: PortAudio did not start again:"
                               << Pa_GetErrorText(err);
            break;
        }
        g_references.fetch_add(1, std::memory_order_acq_rel);
    }
    if (g_references.load(std::memory_order_acquire) != references) {
        return false;
    }
    qCInfo(lcAudio) << "Older drivers: PortAudio started again to list the devices present now";
    return true;
}

void streamOpened()
{
    g_openStreams.fetch_add(1, std::memory_order_acq_rel);
}

void streamClosed()
{
    int open = g_openStreams.load(std::memory_order_acquire);
    while (open > 0
           && !g_openStreams.compare_exchange_weak(open, open - 1, std::memory_order_acq_rel)) {
    }
}

int openStreams()
{
    return g_openStreams.load(std::memory_order_acquire);
}

} // namespace NereusSDR::PortAudioLibrary
