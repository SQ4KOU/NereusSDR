#pragma once
// =================================================================
// src/core/session/media/RemoteMicReceiver.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original.
//
// iPhone app plan Task 36 (R-IOS-13; remote design sections 8.2 and 8.3;
// spec section 4.1): the microphone uplink at the Core.
//
// A remote device sends its microphone on the media connection's
// microphone line (mid "mic"): Opus mono 48 kHz in 20 ms frames with
// in-band FEC, or, from a desktop remote window whose link can carry it,
// the L16 format of PcmAudioCodec. Three pieces live here:
//
//   RemoteMicReceiver (the Core's event loop): RTP in, sequence order,
//     Opus or L16 decode, loss concealment (Opus PLC, and in-band FEC
//     from the next packet for the packet just before it), the key's
//     readiness wait and the starvation signal. Decoded audio goes into
//     the feed.
//   RemoteMicFeed (the boundary): a lock-free ring from the receiver to
//     the transmit pump, and on the pump's side WDSP rmatch
//     (RemoteAudioRateMatcher), which matches the sender's clock to the
//     radio's, both ways. Its ring is the transmit jitter buffer: 60 ms
//     target (rmatch holds its ring half full), 120 ms maximum (rmatch
//     drops the oldest audio and counts it). After every change of use it
//     gives the pump silence until it holds its 60 ms, then audio.
//   RemoteMicEncoder: the encoder a desktop remote window (and the tests)
//     send the line with, Opus mono 20 ms frames with in-band FEC.
//
// Threading: the receiver, and the feed's owner half (setInUse, write),
// run on the Core's event loop. The feed's pump half (pull) runs on the
// transmit pump's thread and never locks, waits or allocates except when
// the feed changes use (the rate matcher is rebuilt then). No codec work
// runs on the pump (R-R3-06).
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-25: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), iPhone app plan Task 36 (R-IOS-13), with
//               AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include "core/audio/AudioRingSpsc.h"

#include <QByteArray>
#include <QObject>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace NereusSDR {

class RemoteAudioRateMatcher;

/// The microphone line's numbers (Task 36; the transmit numbers of the plan:
/// buffer 60 ms target and 120 ms maximum, starvation 250 ms).
struct RemoteMicConfig {
    static constexpr int kSampleRate = 48'000;
    /// Opus, payload type 111, mono, 20 ms frames (the app's microphone
    /// encoder profile).
    static constexpr int kOpusPayloadType = 111;
    static constexpr int kOpusFrameSamples = 960;
    static constexpr int kOpusBitrate = 24'000;
    /// The transmit jitter buffer.
    static constexpr int kTargetDepthMs = 60;
    static constexpr int kMaxDepthMs = 120;
    static constexpr int kTargetDepthFrames = kSampleRate / 1000 * kTargetDepthMs;
    static constexpr int kMaxDepthFrames = kSampleRate / 1000 * kMaxDepthMs;
    /// How long a keyed device's line may carry no audio before it counts
    /// as starved, and how long a key waits for the buffer to fill.
    static constexpr int kStarvationMs = 250;
    static constexpr int kReadyDeadlineMs = 250;
    /// The transmit pump's block (TxWorkerThread::kBlockFrames).
    static constexpr int kPumpBlockFrames = 64;

    static_assert(kTargetDepthFrames * 2 == kMaxDepthFrames,
                  "rmatch holds its ring half full: the target is half the maximum");
    static_assert(kMaxDepthMs < kStarvationMs,
                  "the transmit jitter buffer is shorter than the starvation deadline");
};

/// Whether an Opus payload carries in-band FEC for the frame before it
/// (opus_packet_has_lbrr). Opus codes it only for frames its voice detector
/// calls active.
bool opusPacketCarriesFec(const QByteArray& payload);

/// The boundary between the receiver and the transmit pump.
class RemoteMicFeed {
public:
    RemoteMicFeed();
    ~RemoteMicFeed();
    RemoteMicFeed(const RemoteMicFeed&) = delete;
    RemoteMicFeed& operator=(const RemoteMicFeed&) = delete;

    // ---- the owner's thread (the Core's event loop) ----

    /// Whether the pump takes its audio from this feed. Every change empties
    /// the feed: the audio that was waiting is dropped and the pump starts
    /// again from silence until the buffer holds its target.
    void setInUse(bool inUse);
    bool inUse() const { return m_inUse; }
    /// Frames written since the feed last went in use.
    qint64 framesSinceInUse() const { return m_framesSinceInUse; }
    /// Mono 48 kHz. Refused (false) while not in use; audio that does not
    /// fit the pump's input ring is dropped and counted.
    bool write(const float* mono, int frames);

    // ---- the transmit pump's thread ----

    /// Called on every pump block. While the feed is in use, fills `dst`
    /// with `frames` mono samples (silence until the buffer first reaches
    /// its target after a change of use, the matched audio after) and
    /// returns true. Otherwise leaves `dst` alone, drops what arrived, and
    /// returns false. `frames` must be RemoteMicConfig::kPumpBlockFrames.
    bool pull(float* dst, int frames);

    // ---- any thread ----

    struct Stats {
        /// Whether the pump has reached the target since the last change of
        /// use and is taking audio.
        bool started{false};
        /// Frames in the buffer (the rate matcher's ring), as the pump last
        /// read it.
        int fillFrames{0};
        /// The rate matcher's ratio (output per input; 1 before its control
        /// starts).
        double ratio{1.0};
        /// Since the last change of use: times the buffer ran empty after it
        /// started, and times it overflowed its maximum (the oldest audio
        /// dropped).
        int underflows{0};
        int overflows{0};
        /// Frames the owner could not put in the pump's input ring.
        quint64 droppedFrames{0};
        /// Changes of use the pump has seen.
        quint64 changes{0};
    };
    Stats stats() const;

private:
    static constexpr std::size_t kInputRingBytes = 65536;   // 341 ms of mono float

    void discardInputTo(quint64 bytes);
    void discardAllInput();

    // Owner.
    bool m_inUse{false};
    qint64 m_framesSinceInUse{0};
    quint64 m_writtenBytes{0};

    // Shared.
    AudioRingSpsc<kInputRingBytes> m_input;
    std::atomic<bool> m_inUseForPump{false};
    std::atomic<quint64> m_clearAtBytes{0};
    std::atomic<quint64> m_change{0};
    std::atomic<quint64> m_droppedFrames{0};
    std::atomic<bool> m_statsStarted{false};
    std::atomic<int> m_statsFill{0};
    std::atomic<double> m_statsRatio{1.0};
    std::atomic<int> m_statsUnderflows{0};
    std::atomic<int> m_statsOverflows{0};
    std::atomic<quint64> m_statsChanges{0};

    // Pump.
    std::unique_ptr<RemoteAudioRateMatcher> m_matcher;
    quint64 m_seenChange{0};
    quint64 m_readBytes{0};
    bool m_started{false};
    int m_underflowsAtStart{0};
    std::vector<float> m_monoScratch;
    std::vector<float> m_stereoScratch;
};

/// Opus mono 48 kHz, 20 ms frames, 24 kbit/s, in-band FEC: the microphone
/// line as a desktop remote window sends it.
class RemoteMicEncoder {
public:
    RemoteMicEncoder();
    ~RemoteMicEncoder();
    RemoteMicEncoder(const RemoteMicEncoder&) = delete;
    RemoteMicEncoder& operator=(const RemoteMicEncoder&) = delete;

    bool isReady() const;
    /// One RTP packet from RemoteMicConfig::kOpusFrameSamples mono samples;
    /// empty on failure.
    QByteArray encode(const float* mono, quint16 sequence, quint32 timestamp,
                      quint32 ssrc);
    void reset();

private:
    struct State;
    std::unique_ptr<State> m_state;
};

/// The receiver of one media connection's microphone line.
class RemoteMicReceiver final : public QObject {
    Q_OBJECT

public:
    /// Monotonic milliseconds.
    using Clock = std::function<qint64()>;
    /// Runs `fire` once after `ms` (a QTimer by default; tests drive it).
    using Scheduler = std::function<void(int ms, std::function<void()> fire)>;

    struct Stats {
        quint64 accepted{0};
        /// Frames decoded from packets that arrived.
        quint64 decodedPackets{0};
        /// Lost packets rebuilt from the next packet's in-band FEC.
        quint64 recoveredPackets{0};
        /// Lost packets concealed (Opus PLC, including a packet whose next
        /// carried no FEC, or silence for L16).
        quint64 concealedPackets{0};
        /// Arrived behind the stream (reordered too late, or repeated).
        quint64 latePackets{0};
        /// Gaps longer than the buffer's target: nothing inserted.
        quint64 longGaps{0};
        /// Refused: another payload type, an undecodable payload, or L16
        /// the connection did not agree.
        quint64 rejectedPackets{0};
        /// Frames given to the feed while it was in use.
        quint64 framesWritten{0};
    };

    RemoteMicReceiver(RemoteMicFeed* feed, QObject* parent = nullptr, Clock clock = {},
                      Scheduler scheduler = {});
    ~RemoteMicReceiver() override;

    /// One connection's line: its SSRC, and whether it agreed the L16
    /// format. False when the decoder cannot be built.
    bool start(quint32 ssrc, bool losslessNegotiated);
    /// Ends the line; a key waiting on it is answered not ready.
    void stop();
    bool isRunning() const { return m_running; }
    quint32 ssrc() const { return m_ssrc; }
    void setLosslessNegotiated(bool negotiated) { m_lossless = negotiated; }

    /// One RTP packet from the line (MediaPeer::micRtpReceived).
    void submit(const QByteArray& packet);

    /// The key's wait (Task 36): calls done(true) as soon as the feed, in
    /// use, holds its 60 ms target, or done(false) when it has not within
    /// 250 ms. A second wait replaces the first, which is never answered.
    void awaitReady(std::function<void(bool ready)> done);
    /// Ends a wait without answering it.
    void cancelWait();
    bool isWaiting() const { return static_cast<bool>(m_waitDone); }

    /// Whether a device holding transmit is keyed on this line's audio now.
    /// While it is, 250 ms without audio emits starved(true), and audio
    /// arriving again starved(false). Turning it off ends a starvation.
    void setWatching(bool watching);
    bool isWatching() const { return m_watching; }
    bool isStarved() const { return m_starved; }

    Stats stats() const { return m_stats; }

signals:
    void starved(bool starved);

private:
    void decodeOpus(const QByteArray& payload, int missing);
    void decodeL16(const QByteArray& packet, int missing);
    void writeAudio(const float* mono, int frames);
    void checkReady();
    void scheduleStarvationCheck(int ms);
    void checkStarvation();
    qint64 now() const;

    struct Decoder;
    RemoteMicFeed* m_feed{nullptr};
    Clock m_clock;
    Scheduler m_scheduler;
    std::unique_ptr<Decoder> m_decoder;
    bool m_running{false};
    bool m_lossless{false};
    quint32 m_ssrc{0};
    bool m_haveSequence{false};
    quint16 m_expectedSequence{0};
    int m_lastOpusFrames{RemoteMicConfig::kOpusFrameSamples};
    std::vector<float> m_pcm;
    Stats m_stats;

    std::function<void(bool)> m_waitDone;
    quint64 m_waitGeneration{0};

    bool m_watching{false};
    bool m_starved{false};
    bool m_starvationCheckPending{false};
    quint64 m_starvationGeneration{0};
    qint64 m_lastAudioMs{0};
};

} // namespace NereusSDR
