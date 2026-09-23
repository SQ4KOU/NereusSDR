#pragma once
// =================================================================
// src/core/session/media/IMediaTransport.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R3 Task 1.
//
// One authenticated session's direct encrypted media peer. Authentication
// and model ownership remain above this interface. Implementations carry
// already-encoded display messages and raw RTP packets only.
//
// =================================================================

#include <QByteArray>
#include <QObject>
#include <QString>

#include <optional>

namespace NereusSDR {

struct MediaTransportTelemetry {
    quint64 receivedDisplayPayloadBytes = 0;
    quint64 submittedDisplayPayloadBytes = 0;
    quint64 receivedRtpBytes = 0;
    quint64 submittedRtpBytes = 0;
    /// Received display messages this side discarded, oldest first, because
    /// more than kMaxPendingDisplayMessages (or their byte bound) were waiting
    /// for one drain. Each discarded message was already counted in
    /// receivedDisplayPayloadBytes, which measures arrival, not use.
    quint64 displayMessagesDropped = 0;
};

class IMediaTransport : public QObject {
    Q_OBJECT

public:
    enum class Role {
        Offerer,
        Answerer,
    };
    Q_ENUM(Role)

    // OpusAudioCodecConfig's default bitrate, named here so this interface
    // does not include the codec for one number; MediaPeer.cpp checks that
    // the two agree.
    static constexpr int kDefaultAudioTargetBitrate = 24000;

    struct StartOptions {
        Role role;
        // Per-session RTP routing identity. DTLS authenticates the peer;
        // this value is not an authentication token.
        quint32 localAudioSsrc;
        // The Opus encoder target this side sends at, bit/s (R-R3-23). An
        // offerer's audio description never advertises a higher average
        // bitrate than this. Defaults to the encoder's own default target,
        // so a caller that names only role and SSRC keeps today's offer.
        int audioTargetBitrate = kDefaultAudioTargetBitrate;
        // R-R3-23 lossless audio. An offerer adds the L16 rtpmap
        // (PcmAudioCodecConfig::kPayloadType, "L16/48000/2") to its one audio
        // m-line, after Opus, which stays first. Set only for a GUI that
        // declared it understands the lossless profile, so every other GUI
        // receives exactly today's offer. An answerer ignores it: its answer
        // follows the offer.
        bool offerLosslessAudio = false;
    };

    static constexpr qsizetype kMaxDescriptionBytes = 64 * 1024;
    static constexpr qsizetype kMaxCandidateBytes = 4 * 1024;
    static constexpr qsizetype kMaxCandidateMidBytes = 256;
    static constexpr qsizetype kMaxDisplayMessageBytes = 64 * 1024;
    static constexpr qsizetype kMaxRawRtpBytes = 940;
    static constexpr qsizetype kMinRawRtpBytes = 12;
    static constexpr int kMaxRemoteCandidates = 64;
    static constexpr int kConfiguredMtuBytes = 1000;

    // Process-wide SCTP limits, applied once before the first peer in each
    // process (applyMediaSctpSettingsOnce() in the libdatachannel adapter).
    // Latest-value-wins at the producer: a slow link must refuse new display
    // frames, not hold seconds of stale ones. libdatachannel v0.24.5 defaults
    // both buffers to 1 MiB (src/impl/sctptransport.cpp:101-106).
    //
    // Send buffer: the floor is the 64 KiB maximum message, which PureSignal
    // display chunks need. libdatachannel raises SO_SNDBUF to that size on
    // every socket anyway (sctptransport.cpp:299-310), and usrsctp refuses a
    // message larger than the buffer (usrsctp fec583d5
    // sctp_output.c:14090-14096), so the buffer cannot be smaller.
    static constexpr int kSctpSendBufferBytes = 65536;
    // Receive buffer: twice the maximum message. usrsctp holds a message
    // until it reaches half the receive buffer before partial delivery
    // (sctp_indata.c:1078, 1131), so the window always admits a whole 64 KiB
    // message, and libdatachannel joins partial reads up to end of record
    // (sctptransport.cpp:498-533): every message reaches the callback whole.
    static constexpr int kSctpReceiveBufferBytes = 131072;
    // User bytes carried by one SCTP DATA chunk, which is one UDP datagram.
    // libdatachannel disables path MTU discovery and sets the SCTP path MTU
    // to 1000 - 12 (SCTP) - 48 (DTLS) - 8 (UDP) - 40 (IPv6) = 892
    // (sctptransport.cpp:247); usrsctp adds the 12-byte SCTP common header
    // back for its AF_CONN socket, 904 (sctp_pcb.c:4465-4481), and subtracts
    // that header plus the 16-byte DATA chunk header to fragment:
    // 904 - 12 - 16 = 876 (sctp_output.c:6856-6905). A display message of
    // n bytes therefore leaves as ceil(n / 876) datagrams.
    static constexpr int kSctpDataPayloadBytes = 876;

    static constexpr quint64 sctpFragmentCount(quint64 messageBytes)
    {
        return (messageBytes + kSctpDataPayloadBytes - 1) / kSctpDataPayloadBytes;
    }
    static_assert(kSctpSendBufferBytes >= kMaxDisplayMessageBytes,
                  "the SCTP send buffer must hold the largest display message");
    static_assert(kSctpReceiveBufferBytes >= 2 * kMaxDisplayMessageBytes,
                  "the SCTP receive buffer must deliver the largest message whole");

    explicit IMediaTransport(QObject* parent = nullptr) : QObject(parent) {}
    ~IMediaTransport() override = default;

    /// Returns false to refuse. A refusal that has emitted errorOccurred()
    /// first means the backend could not be built, which is transient and
    /// retried; a refusal without an error is a precondition (already
    /// started, an SSRC of zero) and is permanent (R-R3-28). MediaPeer reads
    /// the difference to type its own refusal.
    virtual bool start(const StartOptions& options) = 0;
    virtual void stop() = 0;

    virtual bool acceptDescription(const QString& sdp, const QString& type) = 0;
    virtual bool acceptCandidate(const QString& candidate, const QString& mid) = 0;

    /// What became of one display message handed to submitDisplay().
    enum class DisplaySendResult {
        /// The transport library passed it to SCTP at once.
        Sent,
        /// The library took it but holds it until SCTP has room; it is still
        /// sent, exactly once. At most one message is ever held this way.
        Queued,
        /// Not taken: the library still holds an earlier message. The caller
        /// may offer it (or a newer one) again after displayWritable().
        Busy,
        /// Not taken, and never will be: not ready, invalid, or failed.
        Refused,
    };
    Q_ENUM(DisplaySendResult)

    /// Send without waiting for transport backpressure. The adapter adds no
    /// application-side queue. True when the message was taken (Sent or
    /// Queued), so it will be delivered unless the network loses it.
    virtual bool sendDisplay(const QByteArray& message) = 0;
    /// sendDisplay() with the outcome spelled out. A transport without a
    /// library-side hold reports only Sent or Refused.
    virtual DisplaySendResult submitDisplay(const QByteArray& message)
    {
        return sendDisplay(message) ? DisplaySendResult::Sent : DisplaySendResult::Refused;
    }
    /// True while the library still holds a display message it took, so a
    /// new one would be Busy. displayWritable() follows when it clears.
    virtual bool displayBusy() const { return false; }
    virtual bool sendRtp(const QByteArray& packet) = 0;

    virtual bool isReady() const = 0;

    /// R-R3-23: true once both this side's description and the remote
    /// description carry the L16 rtpmap on the audio m-line, so lossless
    /// packets may be sent. False before negotiation and for a transport
    /// without the lossless profile.
    virtual bool losslessAudioNegotiated() const { return false; }

    /// Cumulative application payload bytes for the current transport start.
    /// Submitted values count preflight-valid calls into the transport
    /// library, including calls which return false or throw; they do not
    /// assert network delivery or SCTP queue acceptance. Unsupported
    /// transports return nullopt.
    virtual std::optional<MediaTransportTelemetry> telemetry() const
    {
        return std::nullopt;
    }

signals:
    void localDescription(const QString& sdp, const QString& type);
    void localCandidate(const QString& candidate, const QString& mid);
    void displayReceived(const QByteArray& message);
    void rtpReceived(const QByteArray& packet);
    void ready();
    void closed();
    /// The underlying peer connection entered a terminal transport-failure
    /// state.  Kept separate from validation/decoder errors so session
    /// recovery never depends on matching an error string.
    void connectionFailed(const QString& message);
    void errorOccurred(const QString& message);
    /// The display message the library held has gone to SCTP; the display
    /// channel takes a new message again.
    void displayWritable();
    /// An error on the display channel itself (a failed display send or the
    /// display channel's own error). Reported here only, not also through
    /// errorOccurred.
    void displayErrorOccurred(const QString& message);
};

} // namespace NereusSDR
