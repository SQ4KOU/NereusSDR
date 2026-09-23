#pragma once
// no-port-check: NereusSDR-original. Bounded ordering for validated audio RTP.
#include <QByteArray>
#include <QtGlobal>
#include <map>
#include <optional>

namespace NereusSDR {

/// Single consumer-thread queue. RTP parsing/SSRC and audio-generation checks
/// happen before insertion. It does not decode or own a device clock.
///
/// R-R3-23: the packet size is a parameter. Opus sends one 1920-frame, 40 ms
/// packet per block (the default); lossless sends 192-frame, 4 ms packets.
/// The reordering window and the hold are times, so both profiles tolerate
/// the same network delay: 8 Opus packets or 80 lossless packets.
class AudioJitterBuffer {
public:
    /// The Opus packet, the default shape.
    static constexpr int kDefaultPacketFrames = 1920;
    static constexpr qint64 kDefaultPacketDurationNs = 40'000'000;
    /// How far ahead of the next expected packet a packet may arrive: 320 ms,
    /// the eight-packet Opus window this queue has always had.
    static constexpr qint64 kWindowNs = 320'000'000;
    /// Every packet is held this long after arrival before release.
    static constexpr qint64 kHoldNs = 80'000'000;
    enum class Admission { Accepted, Duplicate, Late, OutsideWindow, Invalid };
    struct Playout {
        QByteArray packet; // empty means one explicit missing-packet interval
        quint32 timestamp{0};
        bool concealed() const { return packet.isEmpty(); }
    };

    /// Packets of the window a packet duration allows (at least one).
    static constexpr int windowPackets(qint64 packetDurationNs)
    {
        return packetDurationNs > 0 && packetDurationNs <= kWindowNs
            ? int(kWindowNs / packetDurationNs) : 1;
    }

    AudioJitterBuffer() = default;
    /// A non-positive size or duration keeps the Opus default shape.
    AudioJitterBuffer(int packetFrames, qint64 packetDurationNs);

    void reset(quint32 firstTimestamp);
    Admission insert(const QByteArray& packet, quint32 timestamp, qint64 arrivalNs);
    std::optional<Playout> takeReady(qint64 nowNs);
    /// Releases only the exact expected packet when downstream PCM is about
    /// to underrun. Future packets never conceal a missing head through this
    /// path. Its original due time remains the empty-queue missing deadline;
    /// the existing future-packet anchor rule is unchanged.
    std::optional<Playout> takeExpectedPresentEarly();
    int queuedPackets() const { return static_cast<int>(m_packets.size()); }
    quint32 nextTimestamp() const { return m_nextTimestamp; }
    int packetFrames() const { return m_packetFrames; }
    qint64 packetDurationNs() const { return m_packetDurationNs; }
    /// The window in packets of this queue's size.
    int maxPackets() const { return m_maxPackets; }

private:
    struct Entry { QByteArray packet; qint64 dueNs; };
    std::map<quint64, Entry> m_packets;
    int m_packetFrames{kDefaultPacketFrames};
    qint64 m_packetDurationNs{kDefaultPacketDurationNs};
    int m_maxPackets{windowPackets(kDefaultPacketDurationNs)};
    quint64 m_nextIndex{0};
    quint32 m_nextTimestamp{0};
    std::optional<qint64> m_nextMissingDue;
};
static_assert(AudioJitterBuffer::windowPackets(AudioJitterBuffer::kDefaultPacketDurationNs) == 8,
              "the Opus window stays eight packets");
} // namespace NereusSDR
