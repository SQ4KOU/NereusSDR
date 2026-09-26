// =================================================================
// src/core/session/media/LibDataChannelMediaTransport.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R3 Task 1.
// See LibDataChannelMediaTransport.h for the boundary contract.
//
// Modification history (NereusSDR):
//   2026-09-25: iPhone app plan Task 36 (R-IOS-13): the microphone line, a
//               second audio m-line (mid "mic") receive-only at the Core,
//               offered only when asked. J.J. Boyd (KG4VCF), AI-assisted
//               via Anthropic Claude Code.
//   2026-09-25: iPhone app plan Task 37 (R-IOS-13): the "tx" data channel
//               (unordered, never retransmitted) for the transmit
//               keepalive, created and taken only when asked, and the
//               receive-severed test seam. J.J. Boyd (KG4VCF), AI-assisted
//               via Anthropic Claude Code.
//
// =================================================================

#include "core/session/media/LibDataChannelMediaTransport.h"
#include "core/session/media/PcmAudioCodec.h"

#include <QDebug>
#include <QPointer>
#include <QTimer>

#include <rtc/rtc.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace NereusSDR {

namespace {

constexpr char kDisplayLabel[] = "display";
// Task 37: the transmit keepalive's channel.
constexpr char kTxLabel[] = "tx";
constexpr char kAudioMid[] = "audio";
// Task 36: the microphone line and its a=ssrc cname.
constexpr char kMicMid[] = "mic";
constexpr char kMicStreamName[] = "nereus-microphone";
constexpr int kOpusPayloadType = 111;
constexpr std::size_t kMaxPendingEvents = 128;
constexpr std::size_t kMaxPendingDisplayMessages = 8;
constexpr std::size_t kMaxPendingDisplayBytes = 256 * 1024;
// R-R3-43: every declared audio stream keeps the 256 ms of lossless cushion
// the one main stream had: 64 packets at 250 packets/s.
static_assert(IMediaTransport::kReceivedRtpPacketsPerStream * 1000
                      / (PcmAudioCodecConfig::kSampleRate / PcmAudioCodecConfig::kPacketFrames)
                  >= 256,
              "each audio stream's receive queue must hold 256 ms of lossless audio");
constexpr char kMainAudioStreamName[] = "nereus-mixed-stereo";
constexpr char kReceiverAudioStreamPrefix[] = "nereus-receiver-";
// R-R3-45: the headphones mix's a=ssrc cname.
constexpr char kHeadphonesAudioStreamName[] = "nereus-headphones-mix";
constexpr auto kRtpTimingWarningThreshold = std::chrono::milliseconds(80);
constexpr auto kRtpTimingWarningInterval = std::chrono::seconds(1);

struct CallbackEvent {
    enum class Kind {
        Description,
        Candidate,
        Error,
        DisplayError,
        DisplayWritable,
        PeerClosed,
        PeerFailed,
        // Task 37: a message on the "tx" channel (in `first`).
        TxMessage,
    };

    Kind kind;
    std::string first;
    std::string second;
};

struct PendingRtpPacket {
    rtc::binary data;
    std::chrono::steady_clock::time_point receivedAt;
    // Task 36: arrived on the microphone line.
    bool mic = false;
};

std::once_flag g_sctpSettingsOnce;
std::atomic<int> g_sctpSettingsApplications{0};
std::atomic<quint64> g_peersCreated{0};
std::atomic<quint64> g_peersCreatedBeforeSctpSettings{0};

struct CallbackBridge {
    std::mutex mutex;
    std::condition_variable displayReceiveGate;
    bool displayReceiveStalledForTest = false;
    bool cancelled = false;
    bool overflowReported = false;
    std::deque<CallbackEvent> events;
    std::deque<rtc::binary> displayMessages;
    std::size_t displayBytes = 0;
    std::deque<PendingRtpPacket> rtpPackets;
    // R-R3-43: kReceivedRtpPacketsPerStream for each declared audio stream.
    std::size_t rtpPacketCapacity =
        static_cast<std::size_t>(IMediaTransport::kReceivedRtpPacketsPerStream);
    std::chrono::steady_clock::time_point lastRtpReceipt;
    std::chrono::steady_clock::duration maxRtpCallbackGap {};
    std::size_t droppedRtpPackets = 0;
    std::shared_ptr<rtc::DataChannel> dataChannel;
    std::shared_ptr<rtc::Track> track;
    // Task 36: the answerer's microphone line, and the SSRC it declares on
    // it (0: no microphone line).
    std::shared_ptr<rtc::Track> micTrack;
    quint32 micSsrc = 0;
    bool dataChannelAssigned = false;
    bool trackAssigned = false;
    bool micTrackAssigned = false;
    // Task 37: whether this side takes a "tx" channel, and the answerer's
    // once it arrived.
    bool txChannelWanted = false;
    bool txChannelAssigned = false;
    std::shared_ptr<rtc::DataChannel> txChannel;
    // Task 37 test seam: drop everything that arrives.
    bool receiveSeveredForTest = false;
    std::atomic<quint64> receivedDisplayPayloadBytes{0};
    std::atomic<quint64> submittedDisplayPayloadBytes{0};
    std::atomic<quint64> receivedRtpBytes{0};
    std::atomic<quint64> submittedRtpBytes{0};
    std::atomic<quint64> displayMessagesDropped{0};
};

void queueEvent(const std::weak_ptr<CallbackBridge>& weak,
                CallbackEvent::Kind kind,
                std::string first = {}, std::string second = {})
{
    const auto bridge = weak.lock();
    if (!bridge) {
        return;
    }
    std::lock_guard lock(bridge->mutex);
    if (bridge->cancelled) {
        return;
    }
    if (bridge->events.size() >= kMaxPendingEvents) {
        if (!bridge->overflowReported) {
            bridge->events.pop_front();
            bridge->events.push_back({CallbackEvent::Kind::Error,
                                      "media callback queue overflow", {}});
            bridge->overflowReported = true;
        }
        return;
    }
    bridge->events.push_back({kind, std::move(first), std::move(second)});
}

void queueDisplay(const std::weak_ptr<CallbackBridge>& weak, rtc::binary data)
{
    const auto bridge = weak.lock();
    if (!bridge) {
        return;
    }
    std::unique_lock lock(bridge->mutex);
    bridge->displayReceiveGate.wait(lock, [&bridge] {
        return !bridge->displayReceiveStalledForTest || bridge->cancelled;
    });
    if (bridge->cancelled || bridge->receiveSeveredForTest) {
        return;
    }
    if (data.size() > static_cast<std::size_t>(IMediaTransport::kMaxDisplayMessageBytes)) {
        if (bridge->events.size() < kMaxPendingEvents) {
            bridge->events.push_back({CallbackEvent::Kind::Error,
                                      "oversized display message rejected", {}});
        }
        return;
    }
    bridge->receivedDisplayPayloadBytes.fetch_add(
        static_cast<quint64>(data.size()), std::memory_order_relaxed);
    while (!bridge->displayMessages.empty()
           && (bridge->displayMessages.size() >= kMaxPendingDisplayMessages
               || bridge->displayBytes + data.size() > kMaxPendingDisplayBytes)) {
        bridge->displayBytes -= bridge->displayMessages.front().size();
        bridge->displayMessages.pop_front();
        bridge->displayMessagesDropped.fetch_add(1, std::memory_order_relaxed);
    }
    bridge->displayBytes += data.size();
    bridge->displayMessages.push_back(std::move(data));
}

void queueRtp(const std::weak_ptr<CallbackBridge>& weak, rtc::binary data, bool mic)
{
    const auto bridge = weak.lock();
    if (!bridge) {
        return;
    }
    const auto receivedAt = std::chrono::steady_clock::now();
    std::lock_guard lock(bridge->mutex);
    if (bridge->cancelled || bridge->receiveSeveredForTest) {
        return;
    }
    if (data.size() < static_cast<std::size_t>(IMediaTransport::kMinRawRtpBytes)
        || data.size() > static_cast<std::size_t>(IMediaTransport::kMaxRawRtpBytes)) {
        if (bridge->events.size() < kMaxPendingEvents) {
            bridge->events.push_back({CallbackEvent::Kind::Error,
                                      "invalid raw RTP packet size rejected", {}});
        }
        return;
    }
    bridge->receivedRtpBytes.fetch_add(
        static_cast<quint64>(data.size()), std::memory_order_relaxed);
    if (bridge->lastRtpReceipt != std::chrono::steady_clock::time_point {}) {
        bridge->maxRtpCallbackGap = std::max(
            bridge->maxRtpCallbackGap, receivedAt - bridge->lastRtpReceipt);
    }
    bridge->lastRtpReceipt = receivedAt;
    if (bridge->rtpPackets.size() >= bridge->rtpPacketCapacity) {
        bridge->rtpPackets.pop_front();
        ++bridge->droppedRtpPackets;
    }
    bridge->rtpPackets.push_back({std::move(data), receivedAt, mic});
}

void bindDataChannel(const std::shared_ptr<rtc::DataChannel>& channel,
                     const std::weak_ptr<CallbackBridge>& weak)
{
    channel->onError([weak](std::string error) {
        queueEvent(weak, CallbackEvent::Kind::DisplayError, std::move(error));
    });
    channel->onClosed([weak] {
        queueEvent(weak, CallbackEvent::Kind::PeerClosed,
                   "display data channel closed");
    });
    // Threshold 0: libdatachannel calls this when the one message it held
    // for SCTP has gone (src/impl/channel.cpp:52-60), which is when the
    // display channel takes a new message again.
    channel->setBufferedAmountLowThreshold(0);
    channel->onBufferedAmountLow([weak] {
        queueEvent(weak, CallbackEvent::Kind::DisplayWritable);
    });
    channel->onMessage(
        [weak](rtc::binary data) { queueDisplay(weak, std::move(data)); },
        [weak](std::string) {
            queueEvent(weak, CallbackEvent::Kind::Error,
                       "text display message rejected");
        });
}

// Task 37: the "tx" channel. Its messages go through the event queue in
// arrival order. Its closing or an error on it ends nothing else: a lost
// keepalive channel shows at the Core as keepalives stopping, which is the
// safe direction.
void queueTx(const std::weak_ptr<CallbackBridge>& weak, rtc::binary data)
{
    const auto bridge = weak.lock();
    if (!bridge) {
        return;
    }
    std::lock_guard lock(bridge->mutex);
    if (bridge->cancelled || bridge->receiveSeveredForTest || data.empty()
        || data.size() > static_cast<std::size_t>(IMediaTransport::kMaxTxMessageBytes)
        || bridge->events.size() >= kMaxPendingEvents) {
        return;
    }
    bridge->events.push_back({CallbackEvent::Kind::TxMessage,
                              std::string(reinterpret_cast<const char*>(data.data()), data.size()),
                              {}});
}

void bindTxChannel(const std::shared_ptr<rtc::DataChannel>& channel,
                   const std::weak_ptr<CallbackBridge>& weak)
{
    channel->onMessage(
        [weak](rtc::binary data) { queueTx(weak, std::move(data)); },
        [](std::string) {});
}

bool isUnorderedWithoutRetransmits(const rtc::Reliability& reliability)
{
    return reliability.unordered && reliability.maxRetransmits
        && *reliability.maxRetransmits == 0;
}

void bindTrack(const std::shared_ptr<rtc::Track>& track,
               const std::weak_ptr<CallbackBridge>& weak, bool mic = false)
{
    track->onError([weak](std::string error) {
        queueEvent(weak, CallbackEvent::Kind::Error, std::move(error));
    });
    track->onClosed([weak] {
        queueEvent(weak, CallbackEvent::Kind::PeerClosed, "RTP track closed");
    });
    track->onMessage(
        [weak, mic](rtc::binary data) { queueRtp(weak, std::move(data), mic); },
        [weak](std::string) {
            queueEvent(weak, CallbackEvent::Kind::Error,
                       "text RTP message rejected");
        });
}

QByteArray toByteArray(const rtc::binary& data)
{
    if (data.size() > static_cast<std::size_t>(std::numeric_limits<qsizetype>::max())) {
        return {};
    }
    return QByteArray(reinterpret_cast<const char*>(data.data()),
                      static_cast<qsizetype>(data.size()));
}

// R-R3-23: whether a description's audio m-line maps the lossless payload
// type to L16/48000/2 (RFC 3551 L16, 48 kHz, stereo). Task 36: or the
// m-line of another mid (the microphone line).
bool describesLosslessAudio(const rtc::Description& description,
                            const char* mid = kAudioMid)
{
    for (int index = 0; index < description.mediaCount(); ++index) {
        const auto entry = description.media(index);
        const rtc::Description::Media* const* media =
            std::get_if<const rtc::Description::Media*>(&entry);
        if (media == nullptr || *media == nullptr || (*media)->mid() != mid
            || !(*media)->hasPayloadType(PcmAudioCodecConfig::kPayloadType)) {
            continue;
        }
        const rtc::Description::Media::RtpMap* map =
            (*media)->rtpMap(PcmAudioCodecConfig::kPayloadType);
        return QString::fromStdString(map->format).compare(
                   QLatin1String("L16"), Qt::CaseInsensitive) == 0
            && map->clockRate == PcmAudioCodecConfig::kSampleRate
            && map->encParams == std::to_string(PcmAudioCodecConfig::kChannels);
    }
    return false;
}

quint32 rtpSsrc(const QByteArray& packet)
{
    return (static_cast<quint32>(static_cast<quint8>(packet.at(8))) << 24)
        | (static_cast<quint32>(static_cast<quint8>(packet.at(9))) << 16)
        | (static_cast<quint32>(static_cast<quint8>(packet.at(10))) << 8)
        | static_cast<quint32>(static_cast<quint8>(packet.at(11)));
}

// R-R3-43: the main stream or one of the declared receiver streams.
// R-R3-45: or the declared headphones mix (0 when none is declared).
bool isDeclaredAudioSsrc(quint32 ssrc, quint32 mainSsrc, const QList<quint32>& receiverSsrcs,
                         quint32 headphonesSsrc)
{
    return ssrc == mainSsrc || receiverSsrcs.contains(ssrc)
        || (headphonesSsrc != 0 && ssrc == headphonesSsrc);
}

// R-R3-43: at most kMaxReceiverAudioStreams, none zero, none the main SSRC,
// no repeats.
bool validReceiverAudioSsrcs(quint32 mainSsrc, const QList<quint32>& receiverSsrcs)
{
    if (receiverSsrcs.size() > IMediaTransport::kMaxReceiverAudioStreams) {
        return false;
    }
    for (qsizetype index = 0; index < receiverSsrcs.size(); ++index) {
        const quint32 ssrc = receiverSsrcs.at(index);
        if (ssrc == 0 || ssrc == mainSsrc || receiverSsrcs.indexOf(ssrc) != index) {
            return false;
        }
    }
    return true;
}

// R-R3-45: 0 (none), or an id that is neither the main stream's nor a
// receiver stream's.
bool validHeadphonesAudioSsrc(quint32 mainSsrc, const QList<quint32>& receiverSsrcs,
                              quint32 headphonesSsrc)
{
    return headphonesSsrc == 0
        || (headphonesSsrc != mainSsrc && !receiverSsrcs.contains(headphonesSsrc));
}

// Task 36: 0 (none), or an id that is none of the Core's own streams'.
bool validMicAudioSsrc(quint32 mainSsrc, const QList<quint32>& receiverSsrcs,
                       quint32 headphonesSsrc, quint32 micSsrc)
{
    return micSsrc == 0
        || (micSsrc != mainSsrc && !receiverSsrcs.contains(micSsrc)
            && micSsrc != headphonesSsrc);
}

} // namespace

bool applyMediaSctpSettingsOnce()
{
    bool applied = false;
    std::call_once(g_sctpSettingsOnce, [&applied] {
        rtc::SctpSettings settings;
        settings.sendBufferSize =
            static_cast<std::size_t>(IMediaTransport::kSctpSendBufferBytes);
        settings.recvBufferSize =
            static_cast<std::size_t>(IMediaTransport::kSctpReceiveBufferBytes);
        // Every other field stays unset, which keeps libdatachannel's own
        // defaults. Before global initialisation the library stores these
        // for its first peer; afterwards it applies them to new sockets
        // (libdatachannel v0.24.5 src/impl/init.cpp:105-111, 144-145).
        rtc::SetSctpSettings(std::move(settings));
        g_peersCreatedBeforeSctpSettings.store(
            g_peersCreated.load(std::memory_order_relaxed), std::memory_order_relaxed);
        g_sctpSettingsApplications.fetch_add(1, std::memory_order_relaxed);
        applied = true;
    });
    return applied;
}

MediaSctpSettingsRecord mediaSctpSettingsRecord()
{
    return {g_sctpSettingsApplications.load(std::memory_order_relaxed),
            g_peersCreatedBeforeSctpSettings.load(std::memory_order_relaxed),
            g_peersCreated.load(std::memory_order_relaxed)};
}

QString opusOfferFormatParameters(int targetBitrate)
{
    // RFC 7587 section 6.1: stereo, useinbandfec, maxaveragebitrate and
    // minptime describe what the AUTHOR of the description (here the Core)
    // prefers to RECEIVE; only the sprop-* parameters describe what the
    // author sends (sprop-stereo=1: the Core sends stereo). The Core's
    // offer is send-only and it receives no audio, so the receive
    // preferences are set to mirror what it sends, never aspirational:
    // stereo, useinbandfec omitted (default 0, as OpusAudioEncoder sets
    // OPUS_SET_INBAND_FEC(0)), and maxaveragebitrate equal to the
    // configured encoder target (R-R3-23). The encoder itself is reported to
    // the GUI by the minor-8 audio context, not by this line. Receiver
    // streams (R-R3-43) ride this m-line but run their own 48 kbit/s
    // (DaemonMediaController::kReceiverAudioOpusBitrate), each reported by
    // its receiver context; the line stays as it was so a window sees the
    // same offer as before, and no receiver of the Core's audio reads it
    // (the Core receives no audio). Tying the
    // receive preferences to the send target must be revisited when the
    // m-line becomes sendrecv (TX audio, R4): then they describe what the
    // Core really wants to receive.
    return QStringLiteral("minptime=10;maxaveragebitrate=%1;stereo=1;sprop-stereo=1")
        .arg(targetBitrate);
}

QString micLineOpusFormatParameters()
{
    // RFC 7587 section 6.1: these describe what the author of the offer
    // (the Core) prefers to receive on this line: mono (stereo=0), in-band
    // FEC, a 24 kbit/s average and 10 ms minimum packet time, which is what
    // the app's microphone encoder sends (20 ms frames, mono 48 kHz).
    return QStringLiteral("minptime=10;useinbandfec=1;stereo=0;maxaveragebitrate=24000");
}

struct LibDataChannelMediaTransport::Private {
    QTimer* drainTimer = nullptr;
    std::shared_ptr<CallbackBridge> bridge;
    std::shared_ptr<rtc::PeerConnection> peer;
    std::shared_ptr<rtc::DataChannel> display;
    // Task 37: the "tx" channel, null without one.
    std::shared_ptr<rtc::DataChannel> tx;
    std::shared_ptr<rtc::Track> audio;
    // Task 36: the microphone line (the offerer's receive-only track or the
    // answerer's send-only one), null without one.
    std::shared_ptr<rtc::Track> micAudio;
    quint32 micAudioSsrc = 0;
    Role role = Role::Answerer;
    quint32 localAudioSsrc = 0;
    // R-R3-43: the declared receiver audio streams' SSRCs, empty for today.
    QList<quint32> receiverAudioSsrcs;
    // R-R3-45: the declared headphones mix's SSRC, 0 for today.
    quint32 headphonesAudioSsrc = 0;
    CandidatePolicy candidatePolicy = CandidatePolicy::HostOnly;
    bool started = false;
    bool ready = false;
    bool remoteDescriptionAccepted = false;
    bool remoteDescribesLossless = false;
    bool remoteDescribesMicLossless = false;
    int acceptedCandidates = 0;
    std::chrono::steady_clock::time_point lastRtpTimingWarning;
};

LibDataChannelMediaTransport::LibDataChannelMediaTransport(
    QObject* parent, CandidatePolicy candidatePolicy)
    : IMediaTransport(parent)
    , d(std::make_unique<Private>())
{
    d->candidatePolicy = candidatePolicy;
    d->drainTimer = new QTimer(this);
    d->drainTimer->setInterval(2);
    d->drainTimer->setTimerType(Qt::PreciseTimer);
    connect(d->drainTimer, &QTimer::timeout, this,
            &LibDataChannelMediaTransport::drainCallbacks);
}

LibDataChannelMediaTransport::~LibDataChannelMediaTransport()
{
    stopInternal(false);
}

bool LibDataChannelMediaTransport::start(const StartOptions& options)
{
    if (d->started || options.localAudioSsrc == 0
        || !validReceiverAudioSsrcs(options.localAudioSsrc, options.receiverAudioSsrcs)
        || !validHeadphonesAudioSsrc(options.localAudioSsrc, options.receiverAudioSsrcs,
                                     options.headphonesAudioSsrc)
        || !validMicAudioSsrc(options.localAudioSsrc, options.receiverAudioSsrcs,
                              options.headphonesAudioSsrc, options.micAudioSsrc)) {
        return false;
    }

    d->role = options.role;
    d->localAudioSsrc = options.localAudioSsrc;
    d->receiverAudioSsrcs = options.receiverAudioSsrcs;
    d->headphonesAudioSsrc = options.headphonesAudioSsrc;
    d->micAudioSsrc = options.micAudioSsrc;
    d->bridge = std::make_shared<CallbackBridge>();
    d->bridge->micSsrc = options.micAudioSsrc;
    d->bridge->txChannelWanted = options.txChannel;
    // Task 36: the microphone line's packets share the queue with one more
    // stream's worth of room.
    d->bridge->rtpPacketCapacity =
        static_cast<std::size_t>(kReceivedRtpPacketsPerStream)
        * static_cast<std::size_t>(1 + options.receiverAudioSsrcs.size()
                                   + (options.headphonesAudioSsrc != 0 ? 1 : 0)
                                   + (options.micAudioSsrc != 0 ? 1 : 0));
    const std::weak_ptr<CallbackBridge> weak = d->bridge;

    try {
        rtc::Configuration config;
        config.mtu = static_cast<std::size_t>(kConfiguredMtuBytes);
        // libdatachannel applies this before delivering a complete SCTP
        // message, so the allocation is bounded before our callback runs.
        config.maxMessageSize = static_cast<std::size_t>(kMaxDisplayMessageBytes);
        config.forceMediaTransport = true;
        config.disableAutoNegotiation = true;
        config.enableIceTcp = false;
        config.iceServers.clear();

        applyMediaSctpSettingsOnce();
        d->peer = std::make_shared<rtc::PeerConnection>(std::move(config));
        g_peersCreated.fetch_add(1, std::memory_order_relaxed);
        d->peer->onLocalDescription([weak](rtc::Description description) {
            const std::string sdp = description.generateSdp();
            const std::string type = description.typeString();
            if (sdp.size() > static_cast<std::size_t>(IMediaTransport::kMaxDescriptionBytes)) {
                queueEvent(weak, CallbackEvent::Kind::Error,
                           "oversized local description rejected");
                return;
            }
            queueEvent(weak, CallbackEvent::Kind::Description, sdp, type);
        });
        d->peer->onLocalCandidate([weak](rtc::Candidate candidate) {
            const std::string value = candidate.candidate();
            const std::string mid = candidate.mid();
            if (value.size() > static_cast<std::size_t>(IMediaTransport::kMaxCandidateBytes)
                || mid.size() > static_cast<std::size_t>(IMediaTransport::kMaxCandidateMidBytes)) {
                queueEvent(weak, CallbackEvent::Kind::Error,
                           "oversized local candidate rejected");
                return;
            }
            queueEvent(weak, CallbackEvent::Kind::Candidate, value, mid);
        });
        d->peer->onStateChange([weak](rtc::PeerConnection::State state) {
            if (state == rtc::PeerConnection::State::Failed) {
                queueEvent(weak, CallbackEvent::Kind::PeerFailed,
                           "media peer connection failed");
            } else if (state == rtc::PeerConnection::State::Closed) {
                queueEvent(weak, CallbackEvent::Kind::PeerClosed,
                           "media peer connection closed");
            }
        });
        d->peer->onDataChannel([weak](std::shared_ptr<rtc::DataChannel> channel) {
            const rtc::Reliability reliability = channel->reliability();
            // Task 37: the "tx" channel, taken only by a side started with
            // it, unordered and never retransmitted.
            if (channel->label() == kTxLabel) {
                const auto bridge = weak.lock();
                bool take = false;
                if (bridge && isUnorderedWithoutRetransmits(reliability)) {
                    std::lock_guard lock(bridge->mutex);
                    take = !bridge->cancelled && bridge->txChannelWanted
                        && !bridge->txChannelAssigned;
                    if (take) {
                        bridge->txChannelAssigned = true;
                        bridge->txChannel = channel;
                    }
                }
                if (!take) {
                    channel->close();
                    queueEvent(weak, CallbackEvent::Kind::Error,
                               "unexpected tx data channel rejected");
                    return;
                }
                bindTxChannel(channel, weak);
                return;
            }
            if (channel->label() != kDisplayLabel || !reliability.unordered
                || !reliability.maxRetransmits || *reliability.maxRetransmits != 0) {
                channel->close();
                queueEvent(weak, CallbackEvent::Kind::Error,
                           "unexpected display data channel rejected");
                return;
            }
            bindDataChannel(channel, weak);
            const auto bridge = weak.lock();
            if (!bridge) {
                channel->resetCallbacks();
                channel->close();
                return;
            }
            bool reject = false;
            {
                std::lock_guard lock(bridge->mutex);
                reject = bridge->cancelled || bridge->dataChannelAssigned;
                if (!reject) {
                    bridge->dataChannelAssigned = true;
                    bridge->dataChannel = channel;
                }
            }
            if (reject) {
                channel->resetCallbacks();
                channel->close();
            }
        });
        d->peer->onTrack([weak](std::shared_ptr<rtc::Track> track) {
            rtc::Description::Media description = track->description();
            // Task 36: the microphone line, taken only by an answerer that
            // was started with one. libdatachannel calls this from inside
            // setRemoteDescription(), before the answer is written, so the
            // SSRC declared here goes out in the answer and the Core's
            // library routes the line's packets by it.
            const auto owner = weak.lock();
            const quint32 micSsrc = owner ? owner->micSsrc : 0;
            const bool mic = track->mid() == kMicMid && micSsrc != 0;
            if ((track->mid() != kAudioMid && !mic)
                || !description.hasPayloadType(kOpusPayloadType)) {
                track->close();
                queueEvent(weak, CallbackEvent::Kind::Error,
                           "unexpected media track rejected");
                return;
            }
            if (mic) {
                try {
                    description.addSSRC(micSsrc, kMicStreamName);
                    track->setDescription(std::move(description));
                } catch (const std::exception&) {
                    track->close();
                    queueEvent(weak, CallbackEvent::Kind::Error,
                               "microphone line rejected");
                    return;
                }
            }
            bindTrack(track, weak, mic);
            const auto bridge = weak.lock();
            if (!bridge) {
                track->resetCallbacks();
                track->close();
                return;
            }
            bool reject = false;
            {
                std::lock_guard lock(bridge->mutex);
                if (mic) {
                    reject = bridge->cancelled || bridge->micTrackAssigned;
                    if (!reject) {
                        bridge->micTrackAssigned = true;
                        bridge->micTrack = track;
                    }
                } else {
                    reject = bridge->cancelled || bridge->trackAssigned;
                    if (!reject) {
                        bridge->trackAssigned = true;
                        bridge->track = track;
                    }
                }
            }
            if (reject) {
                track->resetCallbacks();
                track->close();
            }
        });

        if (options.role == Role::Offerer) {
            rtc::DataChannelInit init;
            init.reliability.unordered = true;
            init.reliability.maxRetransmits = 0;
            d->display = d->peer->createDataChannel(kDisplayLabel, init);
            bindDataChannel(d->display, weak);
            // Task 37: the keepalive's own channel, only when asked.
            if (options.txChannel) {
                d->tx = d->peer->createDataChannel(kTxLabel, init);
                bindTxChannel(d->tx, weak);
            }

            rtc::Description::Audio opus(kAudioMid,
                                         rtc::Description::Direction::SendOnly);
            opus.addOpusCodec(
                kOpusPayloadType,
                opusOfferFormatParameters(options.audioTargetBitrate)
                    .toStdString());
            if (options.offerLosslessAudio) {
                // R-R3-23: the lossless profile rides the same m-line and
                // SSRC as Opus; the payload type tells the two apart. Opus
                // stays first, the preferred format.
                opus.addAudioCodec(PcmAudioCodecConfig::kPayloadType,
                                   l16RtpMapEncoding());
            }
            opus.addSSRC(options.localAudioSsrc, kMainAudioStreamName);
            // R-R3-43: receiver streams ride the same m-line, each declared
            // by its own a=ssrc line after the main one, in list order. With
            // none asked for, the offer is today's.
            for (qsizetype index = 0; index < options.receiverAudioSsrcs.size(); ++index) {
                opus.addSSRC(options.receiverAudioSsrcs.at(index),
                             kReceiverAudioStreamPrefix + std::to_string(index));
            }
            // R-R3-45: the headphones mix, last, only when declared.
            if (options.headphonesAudioSsrc != 0) {
                opus.addSSRC(options.headphonesAudioSsrc, kHeadphonesAudioStreamName);
            }
            d->audio = d->peer->addTrack(opus);
            bindTrack(d->audio, weak);

            // Task 36: the microphone line, after the main one and only
            // when asked. The Core receives on it and declares no SSRC of
            // its own; the answer declares the microphone's.
            if (options.micAudioSsrc != 0) {
                rtc::Description::Audio mic(kMicMid,
                                            rtc::Description::Direction::RecvOnly);
                mic.addOpusCodec(kOpusPayloadType,
                                 micLineOpusFormatParameters().toStdString());
                if (options.offerLosslessAudio) {
                    mic.addAudioCodec(PcmAudioCodecConfig::kPayloadType,
                                      l16RtpMapEncoding());
                }
                d->micAudio = d->peer->addTrack(mic);
                bindTrack(d->micAudio, weak, /*mic=*/true);
            }
        }

        d->started = true;
        d->ready = false;
        d->remoteDescriptionAccepted = false;
        d->remoteDescribesLossless = false;
        d->remoteDescribesMicLossless = false;
        d->acceptedCandidates = 0;
        d->drainTimer->start();

        if (options.role == Role::Offerer) {
            d->peer->setLocalDescription(rtc::Description::Type::Offer);
        }
        return true;
    } catch (const std::exception& error) {
        const QString message = QString::fromUtf8(error.what());
        stopInternal(false);
        emit errorOccurred(message);
        return false;
    }
}

void LibDataChannelMediaTransport::stop()
{
    stopInternal(true);
}

void LibDataChannelMediaTransport::stopInternal(bool notify)
{
    if (!d->started && !d->peer && !d->bridge) {
        return;
    }

    const bool wasStarted = d->started;
    d->started = false;
    d->ready = false;
    d->localAudioSsrc = 0;
    d->receiverAudioSsrcs.clear();
    d->headphonesAudioSsrc = 0;
    d->micAudioSsrc = 0;
    d->remoteDescriptionAccepted = false;
    d->remoteDescribesLossless = false;
    d->remoteDescribesMicLossless = false;
    d->acceptedCandidates = 0;
    d->drainTimer->stop();

    std::shared_ptr<rtc::DataChannel> pendingDisplay;
    std::shared_ptr<rtc::DataChannel> pendingTx;
    std::shared_ptr<rtc::Track> pendingAudio;
    std::shared_ptr<rtc::Track> pendingMic;
    if (d->bridge) {
        std::lock_guard lock(d->bridge->mutex);
        d->bridge->cancelled = true;
        d->bridge->displayReceiveGate.notify_all();
        d->bridge->events.clear();
        d->bridge->displayMessages.clear();
        d->bridge->displayBytes = 0;
        d->bridge->rtpPackets.clear();
        pendingDisplay = std::move(d->bridge->dataChannel);
        pendingTx = std::move(d->bridge->txChannel);
        pendingAudio = std::move(d->bridge->track);
        pendingMic = std::move(d->bridge->micTrack);
    }

    if (pendingTx && pendingTx != d->tx) {
        pendingTx->resetCallbacks();
        pendingTx->close();
    }
    if (pendingDisplay && pendingDisplay != d->display) {
        pendingDisplay->resetCallbacks();
        pendingDisplay->close();
    }
    if (pendingAudio && pendingAudio != d->audio) {
        pendingAudio->resetCallbacks();
        pendingAudio->close();
    }
    if (pendingMic && pendingMic != d->micAudio) {
        pendingMic->resetCallbacks();
        pendingMic->close();
    }

    if (d->display) {
        d->display->resetCallbacks();
        d->display->close();
    }
    if (d->tx) {
        d->tx->resetCallbacks();
        d->tx->close();
    }
    if (d->audio) {
        d->audio->resetCallbacks();
        d->audio->close();
    }
    if (d->micAudio) {
        d->micAudio->resetCallbacks();
        d->micAudio->close();
    }
    if (d->peer) {
        d->peer->resetCallbacks();
        d->peer->close();
    }

    d->display.reset();
    d->tx.reset();
    d->audio.reset();
    d->micAudio.reset();
    d->peer.reset();
    d->bridge.reset();

    if (notify && wasStarted) {
        emit closed();
    }
}

bool LibDataChannelMediaTransport::acceptDescription(const QString& sdp,
                                                      const QString& type)
{
    if (!d->started || d->remoteDescriptionAccepted || !d->peer) {
        return false;
    }
    const QByteArray sdpBytes = sdp.toUtf8();
    const QByteArray typeBytes = type.toUtf8().toLower();
    if (sdpBytes.isEmpty() || sdpBytes.size() > kMaxDescriptionBytes
        || sdpBytes.contains('\0')) {
        return false;
    }
    const QByteArray expected = d->role == Role::Offerer
        ? QByteArrayLiteral("answer") : QByteArrayLiteral("offer");
    if (typeBytes != expected) {
        return false;
    }

    try {
        rtc::Description description(sdpBytes.toStdString(),
                                     typeBytes.toStdString());
        // Candidate admission is intentionally confined to acceptCandidate(),
        // where R3 applies the host-only policy and the shared count bound.
        // libdatachannel otherwise imports candidates embedded in SDP directly.
        if (!description.candidates().empty()) {
            return false;
        }
        const bool describesLossless = describesLosslessAudio(description);
        const bool describesMicLossless = describesLosslessAudio(description, kMicMid);
        d->peer->setRemoteDescription(std::move(description));
        d->remoteDescriptionAccepted = true;
        d->remoteDescribesLossless = describesLossless;
        d->remoteDescribesMicLossless = describesMicLossless;
        if (d->role == Role::Answerer) {
            d->peer->setLocalDescription(rtc::Description::Type::Answer);
        }
        return true;
    } catch (const std::exception& error) {
        emit errorOccurred(QString::fromUtf8(error.what()));
        return false;
    }
}

bool LibDataChannelMediaTransport::acceptCandidate(const QString& candidate,
                                                    const QString& mid)
{
    if (!d->started || !d->peer || d->acceptedCandidates >= kMaxRemoteCandidates) {
        return false;
    }
    const QByteArray candidateBytes = candidate.toUtf8();
    const QByteArray midBytes = mid.toUtf8();
    if (candidateBytes.isEmpty() || candidateBytes.size() > kMaxCandidateBytes
        || midBytes.isEmpty() || midBytes.size() > kMaxCandidateMidBytes
        || candidateBytes.contains('\0') || midBytes.contains('\0')) {
        return false;
    }

    try {
        rtc::Candidate parsed(candidateBytes.toStdString(), midBytes.toStdString());
        // R3 selects HostOnly. AnyIceType keeps the same transport interface
        // usable by a later approved STUN or relay configuration.
        if (d->candidatePolicy == CandidatePolicy::HostOnly
            && parsed.type() != rtc::Candidate::Type::Host) {
            return false;
        }
        d->peer->addRemoteCandidate(std::move(parsed));
        ++d->acceptedCandidates;
        return true;
    } catch (const std::exception& error) {
        emit errorOccurred(QString::fromUtf8(error.what()));
        return false;
    }
}

bool LibDataChannelMediaTransport::sendDisplay(const QByteArray& message)
{
    const DisplaySendResult result = submitDisplay(message);
    return result == DisplaySendResult::Sent || result == DisplaySendResult::Queued;
}

IMediaTransport::DisplaySendResult
LibDataChannelMediaTransport::submitDisplay(const QByteArray& message)
{
    if (!d->ready || !d->display || message.isEmpty()
        || message.size() > kMaxDisplayMessageBytes) {
        return DisplaySendResult::Refused;
    }
    // Latest-value-wins: while the library still holds a message, a new one
    // is not taken, so at most one message ever waits behind SCTP.
    if (d->display->bufferedAmount() != 0) {
        return DisplaySendResult::Busy;
    }
    const std::shared_ptr<CallbackBridge> bridge = d->bridge;
    if (!bridge) {
        return DisplaySendResult::Refused;
    }
    bridge->submittedDisplayPayloadBytes.fetch_add(
        static_cast<quint64>(message.size()), std::memory_order_relaxed);
    try {
        // False means usrsctp had no room (its send buffer counts data not
        // yet acknowledged, usrsctp fec583d5 sctp_output.c:14081-14099), so
        // libdatachannel queued the message and sends it when room appears
        // (v0.24.5 src/impl/sctptransport.cpp:374-393).
        return d->display->send(
                   reinterpret_cast<const rtc::byte*>(message.constData()),
                   static_cast<std::size_t>(message.size()))
            ? DisplaySendResult::Sent : DisplaySendResult::Queued;
    } catch (const std::exception& error) {
        // R-R3-05: a display error is reported once, as a display error.
        emit displayErrorOccurred(QString::fromUtf8(error.what()));
        return DisplaySendResult::Refused;
    }
}

bool LibDataChannelMediaTransport::displayBusy() const
{
    return d->ready && d->display && d->display->bufferedAmount() != 0;
}

bool LibDataChannelMediaTransport::sendRtp(const QByteArray& packet)
{
    if (!d->ready || !d->audio || packet.size() < kMinRawRtpBytes
        || packet.size() > kMaxRawRtpBytes || d->audio->bufferedAmount() != 0
        || d->audio->maxMessageSize() < static_cast<std::size_t>(packet.size())
        || !isDeclaredAudioSsrc(rtpSsrc(packet), d->localAudioSsrc, d->receiverAudioSsrcs,
                                d->headphonesAudioSsrc)) {
        return false;
    }
    const std::shared_ptr<CallbackBridge> bridge = d->bridge;
    if (!bridge) {
        return false;
    }
    bridge->submittedRtpBytes.fetch_add(
        static_cast<quint64>(packet.size()), std::memory_order_relaxed);
    try {
        return d->audio->send(
            reinterpret_cast<const rtc::byte*>(packet.constData()),
            static_cast<std::size_t>(packet.size()));
    } catch (const std::exception& error) {
        emit errorOccurred(QString::fromUtf8(error.what()));
        return false;
    }
}

bool LibDataChannelMediaTransport::sendMicRtp(const QByteArray& packet)
{
    // Task 36: only an answerer sends on the microphone line, and only the
    // SSRC it declared there.
    // Not before the line's track is open: the transport reports ready
    // without waiting for it.
    if (!d->ready || !d->micAudio || !d->micAudio->isOpen() || d->role != Role::Answerer
        || d->micAudioSsrc == 0 || packet.size() < kMinRawRtpBytes || packet.size() > kMaxRawRtpBytes
        || rtpSsrc(packet) != d->micAudioSsrc || d->micAudio->bufferedAmount() != 0
        || d->micAudio->maxMessageSize() < static_cast<std::size_t>(packet.size())) {
        return false;
    }
    const std::shared_ptr<CallbackBridge> bridge = d->bridge;
    if (!bridge) {
        return false;
    }
    bridge->submittedRtpBytes.fetch_add(
        static_cast<quint64>(packet.size()), std::memory_order_relaxed);
    try {
        return d->micAudio->send(
            reinterpret_cast<const rtc::byte*>(packet.constData()),
            static_cast<std::size_t>(packet.size()));
    } catch (const std::exception& error) {
        emit errorOccurred(QString::fromUtf8(error.what()));
        return false;
    }
}

bool LibDataChannelMediaTransport::sendTx(const QByteArray& message)
{
    // Task 37: now or not at all. The channel never retransmits, so nothing
    // waits behind a lost message; a message still held by the library is
    // not joined by another (the next keepalive supersedes it anyway).
    if (!d->started || !d->tx || !d->tx->isOpen() || message.isEmpty()
        || message.size() > kMaxTxMessageBytes || d->tx->bufferedAmount() != 0) {
        return false;
    }
    try {
        d->tx->send(reinterpret_cast<const rtc::byte*>(message.constData()),
                    static_cast<std::size_t>(message.size()));
        return true;
    } catch (const std::exception& error) {
        qWarning().nospace() << "media tx channel send failed: " << error.what();
        return false;
    }
}

void LibDataChannelMediaTransport::setReceiveSeveredForTest(bool severed)
{
    const std::shared_ptr<CallbackBridge> bridge = d->bridge;
    if (!bridge) {
        return;
    }
    std::lock_guard lock(bridge->mutex);
    bridge->receiveSeveredForTest = severed;
}

bool LibDataChannelMediaTransport::micLosslessNegotiated() const
{
    if (!d->started || !d->peer || d->micAudioSsrc == 0 || !d->remoteDescribesMicLossless) {
        return false;
    }
    const std::optional<rtc::Description> local = d->peer->localDescription();
    return local && describesLosslessAudio(*local, kMicMid);
}

bool LibDataChannelMediaTransport::isReady() const
{
    return d->ready;
}

bool LibDataChannelMediaTransport::losslessAudioNegotiated() const
{
    if (!d->started || !d->peer || !d->remoteDescribesLossless) {
        return false;
    }
    // Both sides, not only the remote one: the offerer's own offer, or the
    // answer libdatachannel built from the offer.
    const std::optional<rtc::Description> local = d->peer->localDescription();
    return local && describesLosslessAudio(*local);
}

std::optional<MediaTransportTelemetry>
LibDataChannelMediaTransport::telemetry() const
{
    if (!d->started || !d->bridge) {
        return std::nullopt;
    }
    const std::shared_ptr<CallbackBridge> bridge = d->bridge;
    return MediaTransportTelemetry{
        bridge->receivedDisplayPayloadBytes.load(std::memory_order_relaxed),
        bridge->submittedDisplayPayloadBytes.load(std::memory_order_relaxed),
        bridge->receivedRtpBytes.load(std::memory_order_relaxed),
        bridge->submittedRtpBytes.load(std::memory_order_relaxed),
        bridge->displayMessagesDropped.load(std::memory_order_relaxed),
    };
}

void LibDataChannelMediaTransport::setDisplayReceiveStalledForTest(bool stalled)
{
    const std::shared_ptr<CallbackBridge> bridge = d->bridge;
    if (!bridge) {
        return;
    }
    std::lock_guard lock(bridge->mutex);
    bridge->displayReceiveStalledForTest = stalled;
    bridge->displayReceiveGate.notify_all();
}

void LibDataChannelMediaTransport::drainCallbacks()
{
    if (!d->started || !d->bridge) {
        return;
    }

    QPointer<LibDataChannelMediaTransport> self(this);
    const std::shared_ptr<CallbackBridge> bridge = d->bridge;
    const auto isCurrentGeneration = [&self, &bridge] {
        return self && self->d->started && self->d->bridge == bridge;
    };

    std::deque<CallbackEvent> events;
    std::deque<rtc::binary> displayMessages;
    std::deque<PendingRtpPacket> rtpPackets;
    std::chrono::steady_clock::duration maxRtpCallbackGap {};
    std::size_t droppedRtpPackets = 0;
    std::shared_ptr<rtc::DataChannel> incomingDisplay;
    std::shared_ptr<rtc::DataChannel> incomingTx;
    std::shared_ptr<rtc::Track> incomingAudio;
    std::shared_ptr<rtc::Track> incomingMic;
    {
        std::lock_guard lock(bridge->mutex);
        if (bridge->cancelled) {
            return;
        }
        events.swap(bridge->events);
        displayMessages.swap(bridge->displayMessages);
        bridge->displayBytes = 0;
        rtpPackets.swap(bridge->rtpPackets);
        maxRtpCallbackGap = bridge->maxRtpCallbackGap;
        bridge->maxRtpCallbackGap = {};
        droppedRtpPackets = bridge->droppedRtpPackets;
        bridge->droppedRtpPackets = 0;
        incomingDisplay = std::move(bridge->dataChannel);
        incomingTx = std::move(bridge->txChannel);
        incomingAudio = std::move(bridge->track);
        incomingMic = std::move(bridge->micTrack);
    }

    if (!isCurrentGeneration()) {
        return;
    }
    if (!d->tx && incomingTx) {
        d->tx = std::move(incomingTx);
    }

    if (!d->display && incomingDisplay) {
        d->display = std::move(incomingDisplay);
    }
    if (!d->audio && incomingAudio) {
        d->audio = std::move(incomingAudio);
    }
    if (!d->micAudio && incomingMic) {
        d->micAudio = std::move(incomingMic);
    }

    bool mustStop = false;
    for (CallbackEvent& event : events) {
        switch (event.kind) {
        case CallbackEvent::Kind::Description:
            emit localDescription(QString::fromStdString(event.first),
                                  QString::fromStdString(event.second));
            break;
        case CallbackEvent::Kind::Candidate:
            emit localCandidate(QString::fromStdString(event.first),
                                QString::fromStdString(event.second));
            break;
        case CallbackEvent::Kind::Error:
            emit errorOccurred(QString::fromStdString(event.first));
            break;
        case CallbackEvent::Kind::DisplayError: {
            // Reported once: as a display error while this display channel
            // is current, otherwise as a plain error, as it always was.
            const QString text = QString::fromStdString(event.first);
            if (isCurrentGeneration()) {
                emit displayErrorOccurred(text);
            } else {
                emit errorOccurred(text);
            }
            break;
        }
        case CallbackEvent::Kind::DisplayWritable:
            emit displayWritable();
            break;
        case CallbackEvent::Kind::PeerFailed:
            // Terminal peer connectivity is a recovery decision for the
            // authenticated session owner.  Keep it typed: generic media
            // errors also describe malformed packets and local decoder/device
            // failures, none of which may redial the station.
            emit connectionFailed(QString::fromStdString(event.first));
            mustStop = true;
            break;
        case CallbackEvent::Kind::PeerClosed:
            mustStop = true;
            break;
        case CallbackEvent::Kind::TxMessage:
            emit txReceived(QByteArray(event.first.data(),
                                       static_cast<qsizetype>(event.first.size())));
            break;
        }
        if (!isCurrentGeneration()) {
            return;
        }
    }

    if (mustStop) {
        stop();
        return;
    }

    for (const rtc::binary& message : displayMessages) {
        emit displayReceived(toByteArray(message));
        if (!isCurrentGeneration()) {
            return;
        }
    }
    if (!rtpPackets.empty()) {
        const auto drainedAt = std::chrono::steady_clock::now();
        const auto oldestRtpQueueAge = drainedAt - rtpPackets.front().receivedAt;
        if ((maxRtpCallbackGap > kRtpTimingWarningThreshold
             || oldestRtpQueueAge > kRtpTimingWarningThreshold)
            && (d->lastRtpTimingWarning == std::chrono::steady_clock::time_point {}
                || drainedAt - d->lastRtpTimingWarning >= kRtpTimingWarningInterval)) {
            d->lastRtpTimingWarning = drainedAt;
            qWarning().nospace()
                << "media RTP timing: callbackGapMs="
                << std::chrono::duration_cast<std::chrono::milliseconds>(maxRtpCallbackGap).count()
                << " ownerDrainAgeMs="
                << std::chrono::duration_cast<std::chrono::milliseconds>(oldestRtpQueueAge).count()
                << " batchPackets=" << rtpPackets.size()
                << " droppedPending=" << droppedRtpPackets;
        }
    }
    for (const PendingRtpPacket& packet : rtpPackets) {
        // Task 36: the microphone line's packets are reported apart.
        if (packet.mic) {
            emit micRtpReceived(toByteArray(packet.data));
        } else {
            emit rtpReceived(toByteArray(packet.data));
        }
        if (!isCurrentGeneration()) {
            return;
        }
    }

    if (!isCurrentGeneration()) {
        return;
    }
    const bool nowReady = d->peer
        && d->peer->state() == rtc::PeerConnection::State::Connected
        && d->display && d->display->isOpen()
        && d->audio && d->audio->isOpen();
    if (nowReady && !d->ready) {
        d->ready = true;
        emit ready();
        if (!isCurrentGeneration()) {
            return;
        }
    } else if (!nowReady) {
        d->ready = false;
    }
}

} // namespace NereusSDR
