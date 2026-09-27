// =================================================================
// src/core/session/media/DualPathAudio.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original.
//
// iPhone app plan Task 29 (R-IOS-16): see DualPathAudio.h.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-27: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include "core/session/media/DualPathAudio.h"

#include <QtEndian>

#include <algorithm>

namespace NereusSDR {

namespace {
/// The old path's arrivals kept for matching: a few seconds of every
/// stream's packets.
constexpr int kMaxOldArrivals = 1024;
} // namespace

quint64 DualPathAudio::keyOf(const QByteArray& packet)
{
    if (packet.size() < 12) {
        return 0;
    }
    const quint64 ssrc = qFromBigEndian<quint32>(packet.constData() + 8);
    const quint64 timestamp = qFromBigEndian<quint32>(packet.constData() + 4);
    return (ssrc << 32) | timestamp;
}

void DualPathAudio::start(qint64 nowMs)
{
    Q_UNUSED(nowMs);
    m_held.clear();
    m_oldArrivals.clear();
    m_duplicates.clear();
    m_replacedWhileHeld = 0;
    m_leadMs.reset();
    m_oldDone = false;
    m_nextEaseMs = 0;
    m_active = true;
}

void DualPathAudio::deliver(const QByteArray& packet)
{
    if (m_duplicates.admit(packet) && m_deliver) {
        m_deliver(packet);
    }
}

void DualPathAudio::submit(const QByteArray& packet, bool fromNewPath, qint64 nowMs)
{
    const quint64 key = keyOf(packet);
    if (!m_active || key == 0) {
        deliver(packet);
        return;
    }
    if (!fromNewPath) {
        if (m_oldArrivals.size() >= kMaxOldArrivals) {
            m_oldArrivals.clear();
        }
        m_oldArrivals.insert(key, nowMs);
        const auto held = m_held.find(key);
        if (held != m_held.end()) {
            // The new path brought it this much earlier.
            m_leadMs = std::max(m_leadMs.value_or(0), nowMs - held->second.arrivalMs);
            m_held.erase(held);
            ++m_replacedWhileHeld;
        }
        // The old path sets the schedule; a copy the new path already
        // handed on is dropped here.
        deliver(packet);
        return;
    }
    if (m_oldArrivals.contains(key)) {
        // The old path was first: the new one leads by nothing, and this
        // is a copy.
        m_leadMs = std::max(m_leadMs.value_or(0), qint64{0});
        deliver(packet);
        return;
    }
    if (m_oldDone && (!m_leadMs || *m_leadMs == 0)) {
        deliver(packet);
        return;
    }
    m_held.emplace(key, Held{packet, nowMs});
    tick(nowMs);
}

void DualPathAudio::oldPathDone(qint64 nowMs)
{
    if (!m_active) {
        return;
    }
    m_oldDone = true;
    m_nextEaseMs = nowMs + kEaseIntervalMs;
    tick(nowMs);
}

void DualPathAudio::newPathGone()
{
    m_held.clear();
    m_oldArrivals.clear();
    m_leadMs.reset();
    m_oldDone = false;
    m_active = false;
}

void DualPathAudio::tick(qint64 nowMs)
{
    if (!m_active) {
        return;
    }
    if (m_oldDone && m_leadMs && *m_leadMs > 0 && nowMs >= m_nextEaseMs) {
        m_leadMs = std::max(qint64{0}, *m_leadMs - kEaseStepMs);
        m_nextEaseMs = nowMs + kEaseIntervalMs;
    }
    const qint64 wait = m_leadMs ? *m_leadMs : static_cast<qint64>(kMaxWaitMs);
    for (auto it = m_held.begin(); it != m_held.end();) {
        if (nowMs - it->second.arrivalMs >= wait) {
            const QByteArray packet = it->second.packet;
            it = m_held.erase(it);
            deliver(packet);
        } else {
            ++it;
        }
    }
    finishIfIdle();
}

void DualPathAudio::finishIfIdle()
{
    if (m_oldDone && m_held.empty() && (!m_leadMs || *m_leadMs == 0)) {
        m_active = false;
        m_oldArrivals.clear();
    }
}

} // namespace NereusSDR
