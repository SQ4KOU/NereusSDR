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
}

AudioJitterBuffer::Admission AudioJitterBuffer::insert(
    const QByteArray& packet, quint32 timestamp, qint64 arrivalNs)
{
    if (packet.isEmpty() || packet.size() > OpusAudioCodecConfig::kMaxRtpPacketBytes
        || arrivalNs < 0 || arrivalNs > std::numeric_limits<qint64>::max() - kHoldNs) {
        return Admission::Invalid;
    }
    const qint32 delta = std::bit_cast<qint32>(quint32(timestamp - m_nextTimestamp));
    if (delta < 0) { return Admission::Late; }
    if (delta % m_packetFrames != 0) { return Admission::Invalid; }
    const int ahead = delta / m_packetFrames;
    if (ahead >= m_maxPackets) { return Admission::OutsideWindow; }
    const auto [unused, inserted] = m_packets.try_emplace(
        m_nextIndex + static_cast<quint64>(ahead), Entry{packet, arrivalNs + kHoldNs});
    Q_UNUSED(unused);
    return inserted ? Admission::Accepted : Admission::Duplicate;
}

std::optional<AudioJitterBuffer::Playout> AudioJitterBuffer::takeReady(qint64 nowNs)
{
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
    return result;
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
    return result;
}
} // namespace NereusSDR
