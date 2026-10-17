// =================================================================
// src/core/session/media/DaemonAudioSendThread.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  See DaemonAudioSendThread.h.
//
// Modification history (NereusSDR):
//   2026-10-10: created.  J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//               Claude Code.
// =================================================================

#include "core/session/media/DaemonAudioSendThread.h"

#include "core/audio/RealtimeAudioPriority.h"
#include "core/session/media/DaemonAudioSender.h"

#include <algorithm>

#if defined(Q_OS_MAC) || defined(Q_OS_LINUX)
#include <pthread.h>
#endif

namespace NereusSDR {

namespace {

void nameThisThread()
{
#if defined(Q_OS_MAC)
    pthread_setname_np("NereusAudioTx");
#elif defined(Q_OS_LINUX)
    pthread_setname_np(pthread_self(), "NereusAudioTx");
#endif
}

} // namespace

DaemonAudioSendThread::DaemonAudioSendThread()
    : m_wake(std::make_shared<DaemonAudioBlockWake>())
    , m_tickMs(DaemonAudioSender::kDrainIntervalMs)
{
}

DaemonAudioSendThread::~DaemonAudioSendThread()
{
    stop();
}

void DaemonAudioSendThread::add(DaemonAudioSender* sender)
{
    if (sender == nullptr) {
        return;
    }
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        if (std::find(m_senders.begin(), m_senders.end(), sender) == m_senders.end()) {
            m_senders.push_back(sender);
        }
    }
    if (!m_thread.joinable()) {
        m_stopRequested.store(false, std::memory_order_release);
        m_thread = std::thread([this] { run(); });
    }
    // A thread waiting with no sender waits without a timeout.
    m_wake->notify();
}

void DaemonAudioSendThread::remove(DaemonAudioSender* sender)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_senders.erase(std::remove(m_senders.begin(), m_senders.end(), sender), m_senders.end());
    // A drain of this sender under way finishes first; the thread looks the
    // sender up again, under this mutex, before every drain.
    m_drained.wait(lock, [this, sender] { return m_draining != sender; });
}

void DaemonAudioSendThread::stop()
{
    if (!m_thread.joinable()) {
        return;
    }
    m_stopRequested.store(true, std::memory_order_release);
    m_wake->notify();
    m_thread.join();
}

void DaemonAudioSendThread::run()
{
    nameThisThread();
    // Real-time audio priority, as the microphone uplink's collector takes
    // it (MicUplinkCollector::run): asked for on this thread, given back on
    // it before it ends.  nullptr is a refusal; the thread runs regardless.
    AudioPriorityToken* priority = elevateAudioThreadPriority();
    m_elevated.store(priority != nullptr, std::memory_order_release);

    std::vector<DaemonAudioSender*> pass;
    while (!m_stopRequested.load(std::memory_order_acquire)) {
        bool idle = false;
        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            idle = m_senders.empty();
        }
        // A block queued while the last pass ran left its wake pending, so
        // this returns at once for it.
        m_wake->wait(idle ? -1 : m_tickMs);
        if (m_stopRequested.load(std::memory_order_acquire)) {
            break;
        }
        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            pass = m_senders;
        }
        for (DaemonAudioSender* sender : pass) {
            {
                const std::lock_guard<std::mutex> lock(m_mutex);
                if (std::find(m_senders.begin(), m_senders.end(), sender) == m_senders.end()) {
                    continue;
                }
                m_draining = sender;
            }
            sender->drain();
            {
                const std::lock_guard<std::mutex> lock(m_mutex);
                m_draining = nullptr;
            }
            m_drained.notify_all();
        }
        m_passes.fetch_add(1, std::memory_order_acq_rel);
    }

    m_elevated.store(false, std::memory_order_release);
    leaveAudioThreadPriority(priority);
}

} // namespace NereusSDR
