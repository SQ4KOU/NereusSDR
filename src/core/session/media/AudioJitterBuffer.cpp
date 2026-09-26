// no-port-check: NereusSDR-original. Network queue policy; no DSP algorithm.
#include "core/session/media/AudioJitterBuffer.h"
#include "core/session/media/OpusAudioCodec.h"
#include <bit>
#include <algorithm>
#include <limits>

namespace NereusSDR {
AudioJitterBuffer::AudioJitterBuffer(int packetFrames, qint64 packetDurationNs)
{
    if (packetFrames > 0 && packetDurationNs > 0) {
        m_packetFrames = packetFrames;
        m_packetDurationNs = packetDurationNs;
        m_maxPackets = windowPackets(packetDurationNs);
    }
}

void AudioJitterBuffer::reset(quint32 firstTimestamp)
{
    m_packets.clear();
    m_nextIndex = 0;
    m_nextTimestamp = firstTimestamp;
    m_nextMissingDue.reset();
    m_released.clear();
}

void AudioJitterBuffer::setHold(qint64 holdNs)
{
    m_holdNs = std::clamp(holdNs, kHoldNs, kMaxHoldNs);
    m_maxPackets = windowPackets(kWindowNs + (m_holdNs - kHoldNs), m_packetDurationNs);
}

void AudioJitterBuffer::easeHold(qint64 nowNs)
{
    if (m_holdNs <= kHoldNs || !m_lastLateNs || nowNs - *m_lastLateNs < kShrinkQuietNs
        || nowNs - m_lastEaseNs < kShrinkIntervalNs) {
        return;
    }
    setHold(m_holdNs - kShrinkStepNs);
    m_lastEaseNs = nowNs;
}

void AudioJitterBuffer::noteRelease(quint32 timestamp, qint64 releasedNs, bool concealed)
{
    m_released.push_back({timestamp, releasedNs, m_holdNs, concealed});
    // Enough to find any packet the deepest window could still call late.
    const auto bound = static_cast<std::size_t>(windowPackets(kMaxWindowNs, m_packetDurationNs));
    while (m_released.size() > bound) { m_released.pop_front(); }
}

AudioJitterBuffer::Admission AudioJitterBuffer::insert(
    const QByteArray& packet, quint32 timestamp, qint64 arrivalNs)
{
    if (packet.isEmpty() || packet.size() > OpusAudioCodecConfig::kMaxRtpPacketBytes
        || arrivalNs < 0 || arrivalNs > std::numeric_limits<qint64>::max() - kMaxHoldNs) {
        return Admission::Invalid;
    }
    const qint32 delta = std::bit_cast<qint32>(quint32(timestamp - m_nextTimestamp));
    if (delta < 0) {
        // R-R3-21: a packet whose interval was concealed arrived this much
        // too late. Had the hold then been that much deeper (plus the
        // margin) it would have played, so the hold grows to that. A late
        // duplicate of a packet that did play changes nothing.
        for (auto it = m_released.rbegin(); it != m_released.rend(); ++it) {
            if (it->timestamp != timestamp) { continue; }
            if (!it->concealed || arrivalNs <= it->releasedNs) { break; }
            m_lastLateNs = arrivalNs;
            // The intervals from this one on play again, this packet first,
            // so the stream runs `back` intervals later from here; the hold
            // grows by at least that (and by the lateness, if more), plus
            // the margin. When that would pass the deepest hold, the hold
            // goes to the ceiling and the packet stays late.
            const auto back = static_cast<quint64>(
                quint32(m_nextTimestamp - timestamp) / quint32(m_packetFrames));
            const qint64 delayNs = std::max(arrivalNs - it->releasedNs,
                                            qint64(back) * m_packetDurationNs);
            const qint64 needed = std::min(kMaxHoldNs, it->holdNs + delayNs + kGrowMarginNs);
            if (needed > m_holdNs) { setHold(needed); }
            if (it->holdNs + delayNs > kMaxHoldNs || back > m_nextIndex) { break; }
            m_nextIndex -= back;
            m_nextTimestamp = timestamp;
            m_released.erase(std::prev(it.base()), m_released.end());
            m_packets.try_emplace(m_nextIndex, Entry{packet, arrivalNs + m_holdNs});
            return Admission::Rewound;
        }
        return Admission::Late;
    }
    if (delta % m_packetFrames != 0) { return Admission::Invalid; }
    const int ahead = delta / m_packetFrames;
    if (ahead >= m_maxPackets) { return Admission::OutsideWindow; }
    const auto [unused, inserted] = m_packets.try_emplace(
        m_nextIndex + static_cast<quint64>(ahead), Entry{packet, arrivalNs + m_holdNs});
    Q_UNUSED(unused);
    return inserted ? Admission::Accepted : Admission::Duplicate;
}

std::optional<AudioJitterBuffer::Playout> AudioJitterBuffer::takeReady(qint64 nowNs)
{
    easeHold(nowNs);
    auto first = m_packets.begin();
    std::optional<qint64> due;
    const bool present = first != m_packets.end() && first->first == m_nextIndex;
    if (present) {
        // Preserve the producer's clock: received blocks are released at
        // their own arrival time plus the hold interval. A fixed packet-period
        // software playout clock here would hide clock drift from rmatch
        // and instead let this queue grow until it periodically dropped.
        due = first->second.dueNs;
    } else if (first != m_packets.end()) {
        due = first->second.dueNs
            - static_cast<qint64>(first->first - m_nextIndex) * m_packetDurationNs;
    } else {
        due = m_nextMissingDue;
    }
    if (!due || nowNs < *due) { return std::nullopt; }
    Playout result;
    result.timestamp = m_nextTimestamp;
    if (present) {
        result.packet = std::move(first->second.packet);
        m_packets.erase(first);
    }
    ++m_nextIndex;
    m_nextTimestamp += quint32(m_packetFrames);
    // If bounded downstream backpressure delayed a burst, do not manufacture
    // a catch-up run of PLC after its final packet. Present packets still
    // keep their arrival clock; only an empty-queue loss deadline follows
    // the actual last release.
    m_nextMissingDue = std::max(*due, nowNs) + m_packetDurationNs;
    noteRelease(result.timestamp, nowNs, !present);
    return result;
}

std::optional<AudioJitterBuffer::Playout> AudioJitterBuffer::concealExpectedNow(qint64 nowNs)
{
    // Only a hole: the expected packet missing with a later one queued.
    // An empty queue is a stall or a device outrunning the stream; its
    // loss deadline and the rate matcher's own accounting keep that case.
    const auto first = m_packets.begin();
    if (first == m_packets.end() || first->first == m_nextIndex) {
        return std::nullopt;
    }
    Playout result;
    result.timestamp = m_nextTimestamp;
    ++m_nextIndex;
    m_nextTimestamp += quint32(m_packetFrames);
    // The next loss deadline follows this concealment, as a deadline
    // release's does; a queued future packet keeps its own anchor.
    m_nextMissingDue = nowNs + m_packetDurationNs;
    noteRelease(result.timestamp, nowNs, true);
    return result;
}

int AudioJitterBuffer::advanceToFit(quint32 timestamp)
{
    const qint32 delta = std::bit_cast<qint32>(quint32(timestamp - m_nextTimestamp));
    if (delta < 0 || delta % m_packetFrames != 0) { return 0; }
    const int ahead = delta / m_packetFrames;
    if (ahead < m_maxPackets) { return 0; }
    const auto skip = static_cast<quint64>(ahead - (m_maxPackets - 1));
    m_nextIndex += skip;
    m_nextTimestamp += quint32(skip) * quint32(m_packetFrames);
    int dropped = 0;
    while (!m_packets.empty() && m_packets.begin()->first < m_nextIndex) {
        m_packets.erase(m_packets.begin());
        ++dropped;
    }
    // The skipped intervals were never heard: nothing to find later.
    m_released.clear();
    return dropped;
}

std::optional<AudioJitterBuffer::Playout> AudioJitterBuffer::takeExpectedPresentEarly()
{
    auto first = m_packets.begin();
    if (first == m_packets.end() || first->first != m_nextIndex) {
        return std::nullopt;
    }

    Playout result;
    result.packet = std::move(first->second.packet);
    result.timestamp = m_nextTimestamp;
    const qint64 originalDue = first->second.dueNs;
    m_packets.erase(first);
    ++m_nextIndex;
    m_nextTimestamp += quint32(m_packetFrames);
    // Demand release changes only when usable PCM becomes available. Keep the
    // producer-derived due time as the empty-queue missing deadline; a queued
    // future packet retains the same anchor precedence as takeReady(). Using
    // the early wall-clock release here would silently create a local clock.
    m_nextMissingDue = originalDue + m_packetDurationNs;
    noteRelease(result.timestamp, originalDue, false);
    return result;
}
} // namespace NereusSDR
