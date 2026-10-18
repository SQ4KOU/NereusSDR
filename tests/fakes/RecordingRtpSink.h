// =================================================================
// tests/fakes/RecordingRtpSink.h  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  A media transport's audio sink for
// tests (R-AUD-SEND): the Core's receive audio is sent by a thread of its
// own, so a fake transport records each packet under a lock, with the
// thread it came from, and its readiness is read from that thread.
//
// Modification history (NereusSDR):
//   2026-10-10: created.  J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//               Claude Code.
// =================================================================

#pragma once

#include "core/session/media/IMediaTransport.h"

#include <QByteArray>
#include <QList>

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

namespace NereusSDR::Test {

// The packets a fake transport took, appended by the sender thread and read
// by the test's thread. Every read is a copy made under the lock.
class SharedPacketList final {
public:
    void append(const QByteArray& packet)
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_packets.append(packet);
    }
    qsizetype size() const
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_packets.size();
    }
    qsizetype count() const { return size(); }
    bool isEmpty() const { return size() == 0; }
    QByteArray at(qsizetype index) const
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_packets.at(index);
    }
    QByteArray value(qsizetype index) const
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_packets.value(index);
    }
    QByteArray first() const { return at(0); }
    QByteArray constFirst() const { return at(0); }
    QByteArray last() const
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_packets.last();
    }
    QByteArray constLast() const { return last(); }
    QList<QByteArray> mid(qsizetype pos, qsizetype length = -1) const
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_packets.mid(pos, length);
    }
    void clear()
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_packets.clear();
    }
    // Every packet so far, to iterate over.
    QList<QByteArray> snapshot() const
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_packets;
    }
    operator QList<QByteArray>() const { return snapshot(); }

private:
    mutable std::mutex m_mutex;
    QList<QByteArray> m_packets;
};

class RecordingRtpSink final : public MediaRtpSink {
public:
    // Any thread. A packet is recorded only while `ready`. The answer is a
    // transport's: not ready while `ready` is false, else accepted or
    // refused by `accept`.
    RtpSendResult sendRtp(const QByteArray& packet) override
    {
        attempts.fetch_add(1, std::memory_order_acq_rel);
        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            m_threads.append(std::this_thread::get_id());
        }
        const bool isReady = ready.load(std::memory_order_acquire);
        const bool taken = isReady && accept.load(std::memory_order_acquire);
        if (recordRefused ? isReady : taken) {
            packets.append(packet);
        }
        std::function<void(const QByteArray&)> hook;
        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            hook = m_hook;
        }
        if (hook) {
            hook(packet);
        }
        if (!isReady) {
            return RtpSendResult::NotReady;
        }
        return taken ? RtpSendResult::Accepted : RtpSendResult::Refused;
    }
    // The owner's thread's yes or no (IMediaTransport::sendRtp).
    bool sendRtpAccepted(const QByteArray& packet)
    {
        return sendRtp(packet) == RtpSendResult::Accepted;
    }

    // Called on the sending thread after each attempt, with no lock held.
    void setHook(std::function<void(const QByteArray&)> hook)
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_hook = std::move(hook);
    }
    // The thread of every attempt, in order.
    QList<std::thread::id> threads() const
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_threads;
    }

    std::atomic<bool> ready{false};
    std::atomic<bool> accept{true};
    // Some fakes record a packet they then refuse; set before any send.
    bool recordRefused{false};
    std::atomic<int> attempts{0};
    SharedPacketList packets;

private:
    mutable std::mutex m_mutex;
    QList<std::thread::id> m_threads;
    std::function<void(const QByteArray&)> m_hook;
};

} // namespace NereusSDR::Test
