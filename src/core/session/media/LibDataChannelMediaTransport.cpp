// =================================================================
// src/core/session/media/LibDataChannelMediaTransport.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R3 Task 1.
// See LibDataChannelMediaTransport.h for the boundary contract.
//
// =================================================================

#include "core/session/media/LibDataChannelMediaTransport.h"

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
#include <vector>

namespace NereusSDR {

namespace {

constexpr char kDisplayLabel[] = "display";
constexpr char kAudioMid[] = "audio";
constexpr int kOpusPayloadType = 111;
constexpr std::size_t kMaxPendingEvents = 128;
constexpr std::size_t kMaxPendingDisplayMessages = 8;
constexpr std::size_t kMaxPendingDisplayBytes = 256 * 1024;
constexpr std::size_t kMaxPendingRtpPackets = 64;
constexpr auto kRtpTimingWarningThreshold = std::chrono::milliseconds(80);
constexpr auto kRtpTimingWarningInterval = std::chrono::seconds(1);

struct CallbackEvent {
    enum class Kind {
        Description,
        Candidate,
        Error,
        PeerClosed,
        PeerFailed,
    };

    Kind kind;
    std::string first;
    std::string second;
};

struct PendingRtpPacket {
    rtc::binary data;
    std::chrono::steady_clock::time_point receivedAt;
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
    std::chrono::steady_clock::time_point lastRtpReceipt;
    std::chrono::steady_clock::duration maxRtpCallbackGap {};
    std::size_t droppedRtpPackets = 0;
    std::shared_ptr<rtc::DataChannel> dataChannel;
    std::shared_ptr<rtc::Track> track;
    bool dataChannelAssigned = false;
    bool trackAssigned = false;
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
    if (bridge->cancelled) {
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

void queueRtp(const std::weak_ptr<CallbackBridge>& weak, rtc::binary data)
{
    const auto bridge = weak.lock();
    if (!bridge) {
        return;
    }
    const auto receivedAt = std::chrono::steady_clock::now();
    std::lock_guard lock(bridge->mutex);
    if (bridge->cancelled) {
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
    if (bridge->rtpPackets.size() >= kMaxPendingRtpPackets) {
        bridge->rtpPackets.pop_front();
        ++bridge->droppedRtpPackets;
    }
    bridge->rtpPackets.push_back({std::move(data), receivedAt});
}

void bindDataChannel(const std::shared_ptr<rtc::DataChannel>& channel,
                     const std::weak_ptr<CallbackBridge>& weak)
{
    channel->onError([weak](std::string error) {
        queueEvent(weak, CallbackEvent::Kind::Error, std::move(error));
    });
    channel->onClosed([weak] {
        queueEvent(weak, CallbackEvent::Kind::PeerClosed,
                   "display data channel closed");
    });
    channel->onMessage(
        [weak](rtc::binary data) { queueDisplay(weak, std::move(data)); },
        [weak](std::string) {
            queueEvent(weak, CallbackEvent::Kind::Error,
                       "text display message rejected");
        });
}

void bindTrack(const std::shared_ptr<rtc::Track>& track,
               const std::weak_ptr<CallbackBridge>& weak)
{
    track->onError([weak](std::string error) {
        queueEvent(weak, CallbackEvent::Kind::Error, std::move(error));
    });
    track->onClosed([weak] {
        queueEvent(weak, CallbackEvent::Kind::PeerClosed, "RTP track closed");
    });
    track->onMessage(
        [weak](rtc::binary data) { queueRtp(weak, std::move(data)); },
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

quint32 rtpSsrc(const QByteArray& packet)
{
    return (static_cast<quint32>(static_cast<quint8>(packet.at(8))) << 24)
        | (static_cast<quint32>(static_cast<quint8>(packet.at(9))) << 16)
        | (static_cast<quint32>(static_cast<quint8>(packet.at(10))) << 8)
        | static_cast<quint32>(static_cast<quint8>(packet.at(11)));
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

struct LibDataChannelMediaTransport::Private {
    QTimer* drainTimer = nullptr;
    std::shared_ptr<CallbackBridge> bridge;
    std::shared_ptr<rtc::PeerConnection> peer;
    std::shared_ptr<rtc::DataChannel> display;
    std::shared_ptr<rtc::Track> audio;
    Role role = Role::Answerer;
    quint32 localAudioSsrc = 0;
    CandidatePolicy candidatePolicy = CandidatePolicy::HostOnly;
    bool started = false;
    bool ready = false;
    bool remoteDescriptionAccepted = false;
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
    if (d->started || options.localAudioSsrc == 0) {
        return false;
    }

    d->role = options.role;
    d->localAudioSsrc = options.localAudioSsrc;
    d->bridge = std::make_shared<CallbackBridge>();
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
            const rtc::Description::Media description = track->description();
            if (track->mid() != kAudioMid
                || !description.hasPayloadType(kOpusPayloadType)) {
                track->close();
                queueEvent(weak, CallbackEvent::Kind::Error,
                           "unexpected media track rejected");
                return;
            }
            bindTrack(track, weak);
            const auto bridge = weak.lock();
            if (!bridge) {
                track->resetCallbacks();
                track->close();
                return;
            }
            bool reject = false;
            {
                std::lock_guard lock(bridge->mutex);
                reject = bridge->cancelled || bridge->trackAssigned;
                if (!reject) {
                    bridge->trackAssigned = true;
                    bridge->track = track;
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

            rtc::Description::Audio opus(kAudioMid,
                                         rtc::Description::Direction::SendOnly);
            opus.addOpusCodec(
                kOpusPayloadType,
                "minptime=10;maxaveragebitrate=96000;stereo=1;"
                "sprop-stereo=1;useinbandfec=1");
            opus.addSSRC(options.localAudioSsrc, "nereus-mixed-stereo");
            d->audio = d->peer->addTrack(opus);
            bindTrack(d->audio, weak);
        }

        d->started = true;
        d->ready = false;
        d->remoteDescriptionAccepted = false;
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
    d->remoteDescriptionAccepted = false;
    d->acceptedCandidates = 0;
    d->drainTimer->stop();

    std::shared_ptr<rtc::DataChannel> pendingDisplay;
    std::shared_ptr<rtc::Track> pendingAudio;
    if (d->bridge) {
        std::lock_guard lock(d->bridge->mutex);
        d->bridge->cancelled = true;
        d->bridge->displayReceiveGate.notify_all();
        d->bridge->events.clear();
        d->bridge->displayMessages.clear();
        d->bridge->displayBytes = 0;
        d->bridge->rtpPackets.clear();
        pendingDisplay = std::move(d->bridge->dataChannel);
        pendingAudio = std::move(d->bridge->track);
    }

    if (pendingDisplay && pendingDisplay != d->display) {
        pendingDisplay->resetCallbacks();
        pendingDisplay->close();
    }
    if (pendingAudio && pendingAudio != d->audio) {
        pendingAudio->resetCallbacks();
        pendingAudio->close();
    }

    if (d->display) {
        d->display->resetCallbacks();
        d->display->close();
    }
    if (d->audio) {
        d->audio->resetCallbacks();
        d->audio->close();
    }
    if (d->peer) {
        d->peer->resetCallbacks();
        d->peer->close();
    }

    d->display.reset();
    d->audio.reset();
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
        d->peer->setRemoteDescription(std::move(description));
        d->remoteDescriptionAccepted = true;
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
    if (!d->ready || !d->display || message.isEmpty()
        || message.size() > kMaxDisplayMessageBytes
        || d->display->bufferedAmount() != 0) {
        return false;
    }
    const std::shared_ptr<CallbackBridge> bridge = d->bridge;
    if (!bridge) {
        return false;
    }
    bridge->submittedDisplayPayloadBytes.fetch_add(
        static_cast<quint64>(message.size()), std::memory_order_relaxed);
    try {
        return d->display->send(
            reinterpret_cast<const rtc::byte*>(message.constData()),
            static_cast<std::size_t>(message.size()));
    } catch (const std::exception& error) {
        emit errorOccurred(QString::fromUtf8(error.what()));
        return false;
    }
}

bool LibDataChannelMediaTransport::sendRtp(const QByteArray& packet)
{
    if (!d->ready || !d->audio || packet.size() < kMinRawRtpBytes
        || packet.size() > kMaxRawRtpBytes || d->audio->bufferedAmount() != 0
        || d->audio->maxMessageSize() < static_cast<std::size_t>(packet.size())
        || rtpSsrc(packet) != d->localAudioSsrc) {
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

bool LibDataChannelMediaTransport::isReady() const
{
    return d->ready;
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
    std::shared_ptr<rtc::Track> incomingAudio;
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
        incomingAudio = std::move(bridge->track);
    }

    if (!isCurrentGeneration()) {
        return;
    }

    if (!d->display && incomingDisplay) {
        d->display = std::move(incomingDisplay);
    }
    if (!d->audio && incomingAudio) {
        d->audio = std::move(incomingAudio);
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
        emit rtpReceived(toByteArray(packet.data));
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
