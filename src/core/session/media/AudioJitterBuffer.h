#pragma once
// no-port-check: NereusSDR-original. Bounded ordering for validated audio RTP.
#include <QByteArray>
#include <QtGlobal>
#include <algorithm>
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
/// with the hold: it is always kWindowNs plus the hold's growth.
///
/// When every interval released since the late packet's own was concealed
/// too, the packet is not thrown away: the queue rewinds to it (Rewound),
/// so the concealment already heard becomes the added delay and the late
/// audio plays after it, in order. When any of them played real audio, a
/// rewind would replay out of order, so the packet stays late
/// (LateConcealed). Once the link has been quiet for kShrinkQuietNs after
/// a late packet, queued audio beyond the hold plus kShedReserveNs is shed
/// a whole interval at a time, so as the hold eases the delay the
/// operator hears comes back down with it, to what the readout shows.
///
/// A fixed-hold queue (setAdaptive(false), the PCM sink's) never grows,
/// rewinds or sheds: its consumer is paced by the hold alone, and a late
/// packet there is only late.
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
    static constexpr qint64 kShrinkStepNs = 40'000'000;
    static constexpr qint64 kShrinkIntervalNs = 1'000'000'000;
    /// Queued audio past the hold that shedding leaves alone: a packet in
    /// flight while arrival and release interleave.
    static constexpr qint64 kShedReserveNs = 40'000'000;
    /// The window at the deepest hold.
    static constexpr qint64 kMaxWindowNs = kWindowNs + (kMaxHoldNs - kHoldNs);
    /// R-R3-21. Late: behind the head (a copy of a packet that played, or
    /// one whose interval is no longer remembered). LateConcealed: its
    /// interval was concealed and it cannot be rewound to; an adaptive
    /// queue grew its hold for it. Rewound: its interval was concealed, as
    /// was every one since, and the queue rewound to play it next; the
    /// stream runs that much later from here, and the hold grew to match.
    enum class Admission { Accepted, Duplicate, Late, OutsideWindow, Invalid, Rewound,
                           LateConcealed };
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
    /// the existing future-packet anchor rule is unchanged. R-R3-21: given
    /// the time, that deadline is anchored no later than nowNs plus the
    /// base hold, so a deepened hold cannot leave a following stall
    /// unconcealed until the rate matcher has run dry.
    std::optional<Playout> takeExpectedPresentEarly(std::optional<qint64> nowNs = std::nullopt);
    /// R-R3-21: conceals the expected interval now, when downstream PCM is
    /// about to underflow, the expected packet is not here and a later one
    /// is queued (a hole, whose deadline a deepened hold pushes out). An
    /// underflow would cost a fresh context; one concealed interval costs
    /// one packet of PLC. Nothing when the expected packet is present (use
    /// takeExpectedPresentEarly()) or the queue is empty.
    std::optional<Playout> concealExpectedNow(qint64 nowNs);
    /// R-R3-21: a packet at `timestamp` fell outside the window. Moves the
    /// head forward just far enough for it to fit, dropping the oldest
    /// queued packets and skipping missing intervals; with nothing queued
    /// the head moves to the packet itself, so no run of concealment is
    /// made for audio that was never going to arrive. Returns the packets
    /// dropped. Nothing moves when it already fits or is behind the head.
    int advanceToFit(quint32 timestamp);
    /// R-R3-21: false for a fixed hold (see the class comment). Default true.
    void setAdaptive(bool adaptive) { m_adaptive = adaptive; }
    /// Packets dropped unheard to bound the delay: by advanceToFit() and by
    /// shedding as the hold eases. Cumulative for this queue.
    quint64 trimmedPackets() const { return m_trimmed; }
    /// The queued span in time: from the head to the newest queued packet.
    qint64 queuedSpanNs() const;
    /// R-R3-21: audio downstream (the rate matcher) holds beyond its own
    /// working level, which shedding counts with the queued span: a
    /// backlog released into the matcher is delay the same as one queued
    /// here, and skipping an interval here lets the matcher drain it.
    void setDownstreamExcessNs(qint64 excessNs) { m_downstreamExcessNs = std::max<qint64>(0, excessNs); }
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
    void shedExcess(qint64 nowNs);
    std::map<quint64, Entry> m_packets;
    std::deque<Released> m_released;
    qint64 m_holdNs{kHoldNs};
    std::optional<qint64> m_lastLateNs;
    qint64 m_lastEaseNs{0};
    bool m_adaptive{true};
    qint64 m_downstreamExcessNs{0};
    quint64 m_trimmed{0};
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
