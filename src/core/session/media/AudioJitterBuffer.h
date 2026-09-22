#pragma once
// no-port-check: NereusSDR-original. Bounded ordering for validated Opus RTP.
#include <QByteArray>
#include <QtGlobal>
#include <map>
#include <optional>

namespace NereusSDR {

/// Single consumer-thread queue. RTP parsing/SSRC and audio-generation checks
/// happen before insertion. It does not decode or own a device clock.
class AudioJitterBuffer {
public:
    static constexpr int kPacketFrames = 1920;
    static constexpr qint64 kPacketDurationNs = 40'000'000;
    static constexpr int kMaxPackets = 8;
    static constexpr qint64 kHoldNs = 80'000'000;
    enum class Admission { Accepted, Duplicate, Late, OutsideWindow, Invalid };
    struct Playout {
        QByteArray packet; // empty means one explicit 40 ms PLC interval
        quint32 timestamp{0};
        bool concealed() const { return packet.isEmpty(); }
    };

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

private:
    struct Entry { QByteArray packet; qint64 dueNs; };
    std::map<quint64, Entry> m_packets;
    quint64 m_nextIndex{0};
    quint32 m_nextTimestamp{0};
    std::optional<qint64> m_nextMissingDue;
};
} // namespace NereusSDR
