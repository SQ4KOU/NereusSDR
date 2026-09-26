#pragma once
// no-port-check: NereusSDR-original. Bounded ordering for validated audio RTP.
#include <QByteArray>
#include <QtGlobal>
#include <deque>
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
///
/// R-R3-21: the hold adapts to the link. A packet that arrives after its
/// interval was already concealed deepens the hold by as much as it was
/// late (plus a margin), up to kMaxHoldNs, so the next stall of the same
/// length is ridden through instead of concealed. After kShrinkQuietNs
/// with no such packet the hold eases back by kShrinkStepNs a
/// kShrinkIntervalNs, never below kHoldNs. The window grows and shrinks
/// with the hold: it is always kWindowNs plus the hold's growth. The late
/// packet itself is not thrown away: the queue rewinds to it (Rewound), so
/// the concealment already heard becomes the added delay and the late
/// audio plays after it, instead of a second run of concealment.
class AudioJitterBuffer {
public:
    /// The Opus packet, the default shape.
    static constexpr int kDefaultPacketFrames = 1920;
    static constexpr qint64 kDefaultPacketDurationNs = 40'000'000;
    /// How far ahead of the next expected packet a packet may arrive: 320 ms,
    /// the eight-packet Opus window this queue has always had.
    static constexpr qint64 kWindowNs = 320'000'000;
    /// Every packet is held this long after arrival before release: the
    /// starting hold, and its floor.
    static constexpr qint64 kHoldNs = 80'000'000;
    /// The deepest the hold grows after late packets (R-R3-21).
    static constexpr qint64 kMaxHoldNs = 500'000'000;
    /// Added to a late packet's lateness when the hold grows for it.
    static constexpr qint64 kGrowMarginNs = 20'000'000;
    /// A steady link: this long without a late packet before the hold
    /// eases back, by kShrinkStepNs every kShrinkIntervalNs.
    static constexpr qint64 kShrinkQuietNs = 2'000'000'000;
    static constexpr qint64 kShrinkStepNs = 20'000'000;
    static constexpr qint64 kShrinkIntervalNs = 1'000'000'000;
    /// The window at the deepest hold.
    static constexpr qint64 kMaxWindowNs = kWindowNs + (kMaxHoldNs - kHoldNs);
    /// Rewound (R-R3-21): the packet came late, after its interval was
    /// concealed, and the queue rewound to play it next. The stream runs
    /// that much later from here, and the hold grew to match.
    enum class Admission { Accepted, Duplicate, Late, OutsideWindow, Invalid, Rewound };
    struct Playout {
        QByteArray packet; // empty means one explicit missing-packet interval
        quint32 timestamp{0};
        bool concealed() const { return packet.isEmpty(); }
    };

    /// Packets of the window a packet duration allows (at least one).
    static constexpr int windowPackets(qint64 packetDurationNs)
    {
        return windowPackets(kWindowNs, packetDurationNs);
    }
    /// Packets of a given window (at least one).
    static constexpr int windowPackets(qint64 windowNs, qint64 packetDurationNs)
    {
        return packetDurationNs > 0 && packetDurationNs <= windowNs
            ? int(windowNs / packetDurationNs) : 1;
    }

    AudioJitterBuffer() = default;
    /// A non-positive size or duration keeps the Opus default shape.
    AudioJitterBuffer(int packetFrames, qint64 packetDurationNs);

    /// Clears the queue and anchors it on firstTimestamp. The hold is the
    /// link's, not the anchor's, so it is kept.
    void reset(quint32 firstTimestamp);
    Admission insert(const QByteArray& packet, quint32 timestamp, qint64 arrivalNs);
    std::optional<Playout> takeReady(qint64 nowNs);
    /// Releases only the exact expected packet when downstream PCM is about
    /// to underrun. Future packets never conceal a missing head through this
    /// path. Its original due time remains the empty-queue missing deadline;
    /// the existing future-packet anchor rule is unchanged.
    std::optional<Playout> takeExpectedPresentEarly();
    /// R-R3-21: conceals the expected interval now, when downstream PCM is
    /// about to underflow, the expected packet is not here and a later one
    /// is queued (a hole, whose deadline a deepened hold pushes out). An
    /// underflow would cost a fresh context; one concealed interval costs
    /// one packet of PLC. Nothing when the expected packet is present (use
    /// takeExpectedPresentEarly()) or the queue is empty.
    std::optional<Playout> concealExpectedNow(qint64 nowNs);
    /// R-R3-21: a packet at `timestamp` fell outside the window. Moves the
    /// head forward just far enough for it to fit, dropping the oldest
    /// queued packets and skipping missing intervals. Returns the packets
    /// dropped. Nothing moves when it already fits or is behind the head.
    int advanceToFit(quint32 timestamp);
    int queuedPackets() const { return static_cast<int>(m_packets.size()); }
    quint32 nextTimestamp() const { return m_nextTimestamp; }
    int packetFrames() const { return m_packetFrames; }
    qint64 packetDurationNs() const { return m_packetDurationNs; }
    /// The window in packets of this queue's size, at the current hold.
    int maxPackets() const { return m_maxPackets; }
    /// The current hold (R-R3-21): kHoldNs up to kMaxHoldNs.
    qint64 holdNs() const { return m_holdNs; }

private:
    struct Entry { QByteArray packet; qint64 dueNs; };
    // R-R3-21: a recently released interval. A late packet finds its own
    // here to learn how late it was and what the hold was then.
    struct Released { quint32 timestamp; qint64 releasedNs; qint64 holdNs; bool concealed; };
    void noteRelease(quint32 timestamp, qint64 releasedNs, bool concealed);
    void setHold(qint64 holdNs);
    void easeHold(qint64 nowNs);
    std::map<quint64, Entry> m_packets;
    std::deque<Released> m_released;
    qint64 m_holdNs{kHoldNs};
    std::optional<qint64> m_lastLateNs;
    qint64 m_lastEaseNs{0};
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
