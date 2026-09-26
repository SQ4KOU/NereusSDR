// =================================================================
// src/core/session/DataChannelTransport.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original.
//
// iPhone app plan Task 28 (R-IOS-16). See DataChannelTransport.h.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-26: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include "core/session/DataChannelTransport.h"

#include "core/session/media/LibDataChannelMediaTransport.h"

#include <QHostAddress>
#include <QLoggingCategory>
#include <QMetaMethod>
#include <QMetaObject>
#include <QtEndian>

#include <rtc/rtc.hpp>

#include <openssl/err.h>

#include <atomic>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <utility>
#include <variant>
#include <vector>

Q_LOGGING_CATEGORY(lcControlChannel, "nereus.session.controlchannel")

namespace NereusSDR {

// ── The wire ────────────────────────────────────────────────────────────

QList<QByteArray> ControlFraming::chunk(const QByteArray& message)
{
    QList<QByteArray> chunks;
    qsizetype offset = 0;
    while (offset < message.size()) {
        const qsizetype length = qMin(kMaxChunkPayloadBytes, message.size() - offset);
        const bool last = offset + length == message.size();
        QByteArray frame;
        frame.reserve(length + 1);
        frame.append(static_cast<char>(last ? kChunkLast : kChunkMore));
        frame.append(message.constData() + offset, length);
        chunks.append(frame);
        offset += length;
    }
    return chunks;
}

namespace {

QByteArray heartbeatFrame(quint8 kind, quint32 id)
{
    QByteArray frame(ControlFraming::kPingBytes, Qt::Uninitialized);
    frame[0] = static_cast<char>(kind);
    qToBigEndian(id, frame.data() + 1);
    return frame;
}

} // namespace

QByteArray ControlFraming::ping(quint32 id)
{
    return heartbeatFrame(kPing, id);
}

QByteArray ControlFraming::pong(quint32 id)
{
    return heartbeatFrame(kPong, id);
}

ControlFraming::Reassembler::Result ControlFraming::Reassembler::refuse(const QString& reason)
{
    m_refused = true;
    m_reason = reason;
    m_pending.clear();
    m_pending.squeeze();
    return Result::Refused;
}

ControlFraming::Reassembler::Result ControlFraming::Reassembler::feed(const QByteArray& frame)
{
    if (m_refused) {
        return Result::Refused;
    }
    if (frame.isEmpty()) {
        return refuse(QStringLiteral("an empty message on the control channel"));
    }
    if (frame.size() > kMaxChunkBytes) {
        return refuse(QStringLiteral("a control channel message longer than %1 bytes")
                          .arg(kMaxChunkBytes));
    }
    const auto kind = static_cast<quint8>(frame.at(0));
    if (kind == kPing || kind == kPong) {
        if (frame.size() != kPingBytes) {
            return refuse(QStringLiteral("a ping or pong that is not %1 bytes").arg(kPingBytes));
        }
        m_id = qFromBigEndian<quint32>(frame.constData() + 1);
        return kind == kPing ? Result::Ping : Result::Pong;
    }
    if (kind != kChunkMore && kind != kChunkLast) {
        return refuse(QStringLiteral("a control channel message of unknown kind 0x%1")
                          .arg(kind, 2, 16, QLatin1Char('0')));
    }
    if (frame.size() < 2) {
        return refuse(QStringLiteral("a chunk with nothing in it"));
    }
    const quint64 total =
        static_cast<quint64>(m_pending.size()) + static_cast<quint64>(frame.size() - 1);
    if (total > m_maxMessageBytes) {
        return refuse(QStringLiteral("a message larger than %1 bytes").arg(m_maxMessageBytes));
    }
    m_pending.append(frame.constData() + 1, frame.size() - 1);
    if (kind == kChunkMore) {
        return Result::Pending;
    }
    m_message = m_pending;
    m_pending.clear();
    return Result::Message;
}

// ── The bridge from the library's threads ───────────────────────────────

namespace {

struct Event {
    enum class Kind {
        Description,
        Candidate,
        GatheringComplete,
        Open,
        Message,
        Pong,
        Refused,
        Closed,
        Failed,
    };
    Kind kind;
    QByteArray bytes;
    QString first;
    QString second;
    quint32 id = 0;
};

// NereusSDR's own bound on events waiting for this object's thread. A
// complete message is at most the inbound cap, and a burst of these past
// the bound means the owner thread has stopped; the connection ends then
// rather than grow without limit.
constexpr std::size_t kMaxPendingEvents = 4096;

} // namespace

struct DataChannelTransport::Bridge {
    explicit Bridge(DataChannelTransport* o, quint64 cap) : owner(o), reassembler(cap) {}

    std::mutex mutex;
    DataChannelTransport* owner = nullptr;
    bool cancelled = false;
    bool drainPosted = false;
    bool overflowed = false;
    std::deque<Event> events;
    ControlFraming::Reassembler reassembler;
    std::shared_ptr<rtc::PeerConnection> peer;
    std::shared_ptr<rtc::DataChannel> channel;
    bool channelAssigned = false;
    std::atomic<bool> answersPings{true};
    std::atomic<quint64> pingsReceived{0};
    std::atomic<quint64> pongsSent{0};

    // Holds `mutex`.
    void postLocked(Event event)
    {
        if (cancelled) {
            return;
        }
        if (events.size() >= kMaxPendingEvents) {
            if (!overflowed) {
                overflowed = true;
                events.clear();
                events.push_back({Event::Kind::Refused, {},
                                  QStringLiteral("the control channel fell behind"), {}, 0});
            }
        } else if (!overflowed) {
            events.push_back(std::move(event));
        }
        if (!drainPosted) {
            drainPosted = true;
            DataChannelTransport* target = owner;
            QMetaObject::invokeMethod(target, [target]() { target->drain(); },
                                      Qt::QueuedConnection);
        }
    }

    void post(Event event)
    {
        std::lock_guard lock(mutex);
        postLocked(std::move(event));
    }
};

namespace {

void onFrame(const std::weak_ptr<DataChannelTransport::Bridge>& weak, rtc::binary data)
{
    const auto bridge = weak.lock();
    if (!bridge) {
        return;
    }
    const QByteArray frame(reinterpret_cast<const char*>(data.data()),
                           static_cast<qsizetype>(data.size()));
    std::shared_ptr<rtc::DataChannel> answerOn;
    quint32 pongId = 0;
    {
        std::lock_guard lock(bridge->mutex);
        if (bridge->cancelled || bridge->overflowed) {
            return;
        }
        ControlFraming::Reassembler& reassembler = bridge->reassembler;
        switch (reassembler.feed(frame)) {
        case ControlFraming::Reassembler::Result::Pending:
            break;
        case ControlFraming::Reassembler::Result::Message:
            bridge->postLocked({Event::Kind::Message, reassembler.message(), {}, {}, 0});
            break;
        case ControlFraming::Reassembler::Result::Ping:
            bridge->pingsReceived.fetch_add(1, std::memory_order_relaxed);
            if (bridge->answersPings.load(std::memory_order_relaxed)) {
                answerOn = bridge->channel;
                pongId = reassembler.id();
            }
            break;
        case ControlFraming::Reassembler::Result::Pong:
            bridge->postLocked({Event::Kind::Pong, {}, {}, {}, reassembler.id()});
            break;
        case ControlFraming::Reassembler::Result::Refused:
            bridge->postLocked({Event::Kind::Refused, {}, reassembler.reason(), {}, 0});
            break;
        }
    }
    // Answered here, off this object's thread and outside the lock, as a
    // WebSocket stack answers a ping without the application.
    if (answerOn) {
        const QByteArray pong = ControlFraming::pong(pongId);
        try {
            answerOn->send(reinterpret_cast<const std::byte*>(pong.constData()),
                           static_cast<std::size_t>(pong.size()));
            bridge->pongsSent.fetch_add(1, std::memory_order_relaxed);
        } catch (const std::exception&) {
            // The channel closed under the ping; its close is reported.
        }
    }
}

void bindChannel(const std::shared_ptr<rtc::DataChannel>& channel,
                 const std::weak_ptr<DataChannelTransport::Bridge>& weak)
{
    channel->onOpen([weak]() {
        if (const auto bridge = weak.lock()) {
            bridge->post({Event::Kind::Open, {}, {}, {}, 0});
        }
    });
    channel->onClosed([weak]() {
        if (const auto bridge = weak.lock()) {
            bridge->post({Event::Kind::Closed, {}, QStringLiteral("the control channel closed"),
                          {}, 0});
        }
    });
    channel->onError([weak](std::string error) {
        if (const auto bridge = weak.lock()) {
            bridge->post({Event::Kind::Failed, {}, QString::fromStdString(error), {}, 0});
        }
    });
    channel->onMessage([weak](rtc::binary data) { onFrame(weak, std::move(data)); },
                       [weak](std::string) {
        if (const auto bridge = weak.lock()) {
            std::lock_guard lock(bridge->mutex);
            if (!bridge->overflowed) {
                bridge->postLocked({Event::Kind::Refused, {},
                                    QStringLiteral("a text message on the control channel"), {},
                                    0});
            }
        }
    });
}

QString typeName(const rtc::Candidate& candidate)
{
    switch (candidate.type()) {
    case rtc::Candidate::Type::Host:
        return QStringLiteral("host");
    case rtc::Candidate::Type::ServerReflexive:
        return QStringLiteral("srflx");
    case rtc::Candidate::Type::PeerReflexive:
        return QStringLiteral("prflx");
    case rtc::Candidate::Type::Relayed:
        return QStringLiteral("relay");
    default:
        return QString();
    }
}

} // namespace

// ── The transport ───────────────────────────────────────────────────────

DataChannelTransport::DataChannelTransport(QObject* parent)
    : SessionTransport(parent)
{
}

DataChannelTransport::~DataChannelTransport()
{
    stopPeer();
}

bool DataChannelTransport::start(const Options& options)
{
    if (m_started || options.maxIncomingBytes == 0
        || (options.role == Role::Answerer
            && options.certificatePemPath.isEmpty() != options.privateKeyPemPath.isEmpty())) {
        return false;
    }
    m_options = options;
    m_bridge = std::make_shared<Bridge>(this, options.maxIncomingBytes);
    const std::weak_ptr<Bridge> weak = m_bridge;

    try {
        rtc::Configuration config;
        config.mtu = static_cast<std::size_t>(IMediaTransport::kConfiguredMtuBytes);
        // Every chunk fits; a larger message from the far end is refused by
        // the library before it is delivered.
        config.maxMessageSize = static_cast<std::size_t>(ControlFraming::kMaxChunkBytes);
        config.disableAutoNegotiation = true;
        config.enableIceTcp = false;
        config.iceServers.clear();
        if (options.ice) {
            // As the media transport (Task 27): one STUN server, gathering
            // held until the relay is known, the MTU with room for TURN's
            // ChannelData header.
            config.mtu = static_cast<std::size_t>(IceConfiguration::kMtuBytes);
            if (const auto stun = options.ice->stunServer()) {
                config.iceServers.emplace_back(stun->host.toStdString(), stun->port);
            }
            config.disableAutoGathering = true;
            if (options.ice->relayKnown()) {
                m_gatherRequested = true;
                m_relays = options.ice->relayServers();
            }
        }
        if (!options.certificatePemPath.isEmpty()) {
            // The Core's own persistent TLS certificate: the SHA-256 a
            // device sees in DTLS is then the one the hello's binding
            // covers (link section 3.4).
            config.certificatePemFile = options.certificatePemPath.toStdString();
            config.keyPemFile = options.privateKeyPemPath.toStdString();
        }

        // The process-wide SCTP limits come before the first peer of any
        // kind (LibDataChannelMediaTransport.h).
        applyMediaSctpSettingsOnce();
        auto peer = std::make_shared<rtc::PeerConnection>(std::move(config));
        // libdatachannel reads the PEM files here, on this thread, and its
        // loop over further certificates in the file ends on a failed read
        // that stays in this thread's OpenSSL error queue (libdatachannel
        // v0.24.5 src/impl/certificate.cpp:424-428). Qt's OpenSSL TLS
        // backend reads that queue after its own calls on the same thread,
        // and a stale error there ends a healthy wss:// connection: the
        // Core's own connection to the remote access service dropped the
        // moment it answered an introduction (the traversal harness, Linux).
        ERR_clear_error();
        m_bridge->peer = peer;
        peer->onLocalDescription([weak](rtc::Description description) {
            const auto bridge = weak.lock();
            if (!bridge) {
                return;
            }
            const std::string sdp = description.generateSdp();
            if (sdp.size() > static_cast<std::size_t>(IMediaTransport::kMaxDescriptionBytes)) {
                bridge->post({Event::Kind::Failed, {},
                              QStringLiteral("oversized local description"), {}, 0});
                return;
            }
            bridge->post({Event::Kind::Description, {}, QString::fromStdString(sdp),
                          QString::fromStdString(description.typeString()), 0});
        });
        peer->onLocalCandidate([weak](rtc::Candidate candidate) {
            const auto bridge = weak.lock();
            if (!bridge) {
                return;
            }
            const std::string value = candidate.candidate();
            if (value.size() > static_cast<std::size_t>(IMediaTransport::kMaxCandidateBytes)) {
                return;
            }
            bridge->post({Event::Kind::Candidate, {}, QString::fromStdString(value), {}, 0});
        });
        peer->onGatheringStateChange([weak](rtc::PeerConnection::GatheringState state) {
            const auto bridge = weak.lock();
            if (bridge && state == rtc::PeerConnection::GatheringState::Complete) {
                bridge->post({Event::Kind::GatheringComplete, {}, {}, {}, 0});
            }
        });
        peer->onStateChange([weak](rtc::PeerConnection::State state) {
            const auto bridge = weak.lock();
            if (!bridge) {
                return;
            }
            if (state == rtc::PeerConnection::State::Failed) {
                bridge->post({Event::Kind::Failed, {},
                              QStringLiteral("the control connection could not be made"), {}, 0});
            } else if (state == rtc::PeerConnection::State::Closed) {
                bridge->post({Event::Kind::Closed, {},
                              QStringLiteral("the control connection closed"), {}, 0});
            }
        });
        peer->onDataChannel([weak](std::shared_ptr<rtc::DataChannel> channel) {
            const auto bridge = weak.lock();
            if (!bridge) {
                channel->close();
                return;
            }
            const rtc::Reliability reliability = channel->reliability();
            // Only the one reliable, ordered channel labelled "control".
            const bool usable = channel->label() == DataChannelTransport::kLabel
                && !reliability.unordered && !reliability.maxRetransmits
                && !reliability.maxPacketLifeTime;
            bool take = false;
            {
                std::lock_guard lock(bridge->mutex);
                take = usable && !bridge->cancelled && !bridge->channelAssigned;
                if (take) {
                    bridge->channelAssigned = true;
                    bridge->channel = channel;
                }
            }
            if (!take) {
                channel->close();
                bridge->post({Event::Kind::Failed, {},
                              QStringLiteral("an unexpected data channel was refused"), {}, 0});
                return;
            }
            bindChannel(channel, weak);
            // An incoming channel is open when it is reported.
            bridge->post({Event::Kind::Open, {}, {}, {}, 0});
        });

        if (options.role == Role::Offerer) {
            // Reliable and ordered: the library's defaults.
            auto channel = peer->createDataChannel(kLabel);
            {
                std::lock_guard lock(m_bridge->mutex);
                m_bridge->channel = channel;
                m_bridge->channelAssigned = true;
            }
            bindChannel(channel, weak);
        }
        m_started = true;
        if (options.role == Role::Offerer) {
            peer->setLocalDescription(rtc::Description::Type::Offer);
            gatherIfReady();
        }
        return true;
    } catch (const std::exception& error) {
        qCWarning(lcControlChannel) << "The control connection could not start:" << error.what();
        stopPeer();
        m_started = false;
        return false;
    }
}

bool DataChannelTransport::acceptDescription(const QString& sdp, const QString& type)
{
    if (!m_started || m_remoteDescriptionAccepted || !m_bridge || !m_bridge->peer) {
        return false;
    }
    const QByteArray sdpBytes = sdp.toUtf8();
    const QByteArray typeBytes = type.toUtf8().toLower();
    if (sdpBytes.isEmpty() || sdpBytes.size() > IMediaTransport::kMaxDescriptionBytes
        || sdpBytes.contains('\0')) {
        return false;
    }
    const QByteArray expected = m_options.role == Role::Offerer ? QByteArrayLiteral("answer")
                                                                : QByteArrayLiteral("offer");
    if (typeBytes != expected) {
        return false;
    }
    try {
        rtc::Description description(sdpBytes.toStdString(), typeBytes.toStdString());
        // Candidates come one at a time, through acceptCandidate() and its
        // rules, never inside the description.
        // A control connection is its one data channel and nothing else: a
        // description with any other line (audio, video) is not one.
        if (!description.candidates().empty() || !description.hasApplication()
            || description.mediaCount() != 1) {
            return false;
        }
        m_bridge->peer->setRemoteDescription(std::move(description));
        m_remoteDescriptionAccepted = true;
        if (m_options.role == Role::Answerer) {
            m_bridge->peer->setLocalDescription(rtc::Description::Type::Answer);
            gatherIfReady();
        }
        return true;
    } catch (const std::exception& error) {
        qCWarning(lcControlChannel) << "A control connection description was refused:"
                                    << error.what();
        return false;
    }
}

bool DataChannelTransport::acceptCandidate(const QString& candidate)
{
    if (!m_started || !m_bridge || !m_bridge->peer
        || m_acceptedCandidates >= IMediaTransport::kMaxRemoteCandidates) {
        return false;
    }
    const QByteArray bytes = candidate.toUtf8();
    if (bytes.isEmpty() || bytes.size() > IMediaTransport::kMaxCandidateBytes
        || bytes.contains('\0')) {
        return false;
    }
    try {
        rtc::Candidate parsed(bytes.toStdString(), std::string());
        if (m_options.ice) {
            if (!m_options.ice->acceptsRemoteCandidate(QString::fromUtf8(bytes))) {
                return false;
            }
        } else if (parsed.type() != rtc::Candidate::Type::Host) {
            return false;
        }
        if (parsed.type() == rtc::Candidate::Type::Relayed) {
            // Where the far end's relay is, so a remote learned there as
            // peer-reflexive still reads as relayed (MediaIcePath).
            rtc::Candidate relay = parsed;
            if (relay.resolve(rtc::Candidate::ResolveMode::Simple) && relay.address()
                && relay.port()) {
                m_farEndRelays.append(
                    qMakePair(QString::fromStdString(*relay.address()), *relay.port()));
            }
        }
        m_bridge->peer->addRemoteCandidate(std::move(parsed));
        ++m_acceptedCandidates;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool DataChannelTransport::gatherCandidates(const IceConfiguration& configured)
{
    if (!m_started || !m_options.ice || m_gatherRequested) {
        return false;
    }
    m_gatherRequested = true;
    m_options.ice = configured;
    m_relays = configured.relayAllowed() ? configured.relayServers() : QList<IceRelayServer>();
    gatherIfReady();
    return true;
}

void DataChannelTransport::gatherIfReady()
{
    if (!m_started || !m_bridge || !m_bridge->peer || !m_options.ice || !m_gatherRequested
        || m_gatheringStarted || !m_bridge->peer->localDescription()) {
        return;
    }
    m_gatheringStarted = true;
    std::vector<rtc::IceServer> relays;
    for (const IceRelayServer& relay : std::as_const(m_relays)) {
        if (relays.size() >= static_cast<std::size_t>(IceConfiguration::kMaxRelayServers)) {
            break;
        }
        relays.emplace_back(relay.host.toStdString(), relay.port, relay.username.toStdString(),
                            relay.password.toStdString(), rtc::IceServer::RelayType::TurnUdp);
    }
    try {
        m_bridge->peer->gatherLocalCandidates(std::move(relays));
    } catch (const std::exception& error) {
        qCWarning(lcControlChannel) << "Gathering failed:" << error.what();
    }
}

std::optional<MediaIcePath> DataChannelTransport::selectedPath() const
{
    if (!m_bridge || !m_bridge->peer) {
        return std::nullopt;
    }
    try {
        rtc::Candidate local;
        rtc::Candidate remote;
        if (!m_bridge->peer->getSelectedCandidatePair(&local, &remote)) {
            return std::nullopt;
        }
        MediaIcePath path;
        path.localType = typeName(local);
        path.remoteType = typeName(remote);
        path.localAddress = QString::fromStdString(local.address().value_or(std::string()));
        path.remoteAddress = QString::fromStdString(remote.address().value_or(std::string()));
        path.remotePort = remote.port().value_or(0);
        path.farEndRelays = m_farEndRelays;
        return path;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void DataChannelTransport::sendText(const QByteArray& wire)
{
    if (!isOpen() || wire.isEmpty()) {
        return;
    }
    std::shared_ptr<rtc::DataChannel> channel;
    {
        std::lock_guard lock(m_bridge->mutex);
        channel = m_bridge->channel;
    }
    if (!channel) {
        return;
    }
    try {
        for (const QByteArray& chunk : ControlFraming::chunk(wire)) {
            // Returns false when the library buffered it; nothing is lost.
            channel->send(reinterpret_cast<const std::byte*>(chunk.constData()),
                          static_cast<std::size_t>(chunk.size()));
            ++m_chunksSent;
        }
        ++m_messagesSent;
        m_telemetry.acceptedPayloadBytes += static_cast<quint64>(wire.size());
    } catch (const std::exception& error) {
        qCWarning(lcControlChannel) << "A control message was not sent:" << error.what();
    }
}

bool DataChannelTransport::sendRawFrameForTest(const QByteArray& frame)
{
    if (!isOpen()) {
        return false;
    }
    std::shared_ptr<rtc::DataChannel> channel;
    {
        std::lock_guard lock(m_bridge->mutex);
        channel = m_bridge->channel;
    }
    if (!channel) {
        return false;
    }
    try {
        // False only means the library buffered it.
        channel->send(reinterpret_cast<const std::byte*>(frame.constData()),
                      static_cast<std::size_t>(frame.size()));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void DataChannelTransport::ping()
{
    if (!isOpen()) {
        return;
    }
    std::shared_ptr<rtc::DataChannel> channel;
    {
        std::lock_guard lock(m_bridge->mutex);
        channel = m_bridge->channel;
    }
    if (!channel) {
        return;
    }
    const QByteArray frame = ControlFraming::ping(m_nextPingId);
    try {
        channel->send(reinterpret_cast<const std::byte*>(frame.constData()),
                      static_cast<std::size_t>(frame.size()));
        ++m_nextPingId;
        if (m_nextPingId == 0) {
            m_nextPingId = 1;
        }
        ++m_pingsSent;
        m_pingSentAt.start();
    } catch (const std::exception&) {
        // Closing; the close is reported.
    }
}

void DataChannelTransport::closeLink(const QString& reason)
{
    if (m_closing || !m_started) {
        return;
    }
    m_closing = true;
    qCInfo(lcControlChannel) << "Closing the control connection:" << reason;
    stopPeer();
    // As a WebSocket's close: closed() follows, from the event loop.
    QMetaObject::invokeMethod(this, [this]() { finishClose(); }, Qt::QueuedConnection);
}

void DataChannelTransport::finishClose()
{
    if (m_closedEmitted) {
        return;
    }
    m_closedEmitted = true;
    const bool wasOpen = m_open;
    m_open = false;
    if (wasOpen) {
        emit closed();
    } else {
        emit failed(QStringLiteral("the control connection closed before it opened"));
    }
}

bool DataChannelTransport::isOpen() const
{
    return m_open && !m_closing;
}

QString DataChannelTransport::peerDescription() const
{
    const std::optional<MediaIcePath> path = selectedPath();
    if (!path) {
        return QStringLiteral("control channel");
    }
    if (path->relayed()) {
        return QStringLiteral("control channel through the relay");
    }
    return QStringLiteral("control channel %1:%2").arg(path->remoteAddress).arg(path->remotePort);
}

QString DataChannelTransport::peerAddress() const
{
    if (m_peerAddressForTest) {
        return *m_peerAddressForTest;
    }
    const std::optional<MediaIcePath> path = selectedPath();
    if (!path || path->relayed() || path->remoteAddress.isEmpty()) {
        return {};
    }
    QHostAddress address(path->remoteAddress);
    bool isV4 = false;
    const quint32 v4 = address.toIPv4Address(&isV4);
    if (isV4) {
        address = QHostAddress(v4);
    }
    address.setScopeId(QString());
    return address.isNull() ? QString() : address.toString();
}

QByteArray DataChannelTransport::peerCertificateSha256() const
{
    if (m_options.role != Role::Offerer || !m_bridge || !m_bridge->peer) {
        return {};
    }
    try {
        // The fingerprint of the certificate the DTLS handshake carried
        // (libdatachannel records it in its verifier, which also holds it
        // to the SDP's), never the SDP's own claim.
        const rtc::CertificateFingerprint fingerprint = m_bridge->peer->remoteFingerprint();
        if (fingerprint.algorithm != rtc::CertificateFingerprint::Algorithm::Sha256
            || fingerprint.value.empty()) {
            return {};
        }
        QByteArray hex = QByteArray::fromStdString(fingerprint.value);
        hex.replace(':', QByteArray());
        const QByteArray digest = QByteArray::fromHex(hex);
        return digest.size() == 32 ? digest : QByteArray();
    } catch (const std::exception&) {
        return {};
    }
}

std::optional<SessionTransportTelemetry> DataChannelTransport::telemetry() const
{
    if (!isOpen()) {
        return std::nullopt;
    }
    SessionTransportTelemetry snapshot = m_telemetry;
    if (m_pongAge.isValid()) {
        snapshot.pongAgeMs = m_pongAge.elapsed();
    }
    return snapshot;
}

void DataChannelTransport::setAnswersPingsForTest(bool answers)
{
    if (m_bridge) {
        m_bridge->answersPings.store(answers, std::memory_order_relaxed);
    }
}

DataChannelTransport::Counts DataChannelTransport::countsForTest() const
{
    Counts counts;
    counts.messagesSent = m_messagesSent;
    counts.messagesDelivered = m_messagesDelivered;
    counts.pingsSent = m_pingsSent;
    counts.pongsReceived = m_pongsReceived;
    counts.chunksSent = m_chunksSent;
    if (m_bridge) {
        counts.pingsReceived = m_bridge->pingsReceived.load(std::memory_order_relaxed);
        counts.pongsSent = m_bridge->pongsSent.load(std::memory_order_relaxed);
    }
    return counts;
}

void DataChannelTransport::drain()
{
    std::deque<Event> events;
    if (m_bridge) {
        std::lock_guard lock(m_bridge->mutex);
        m_bridge->drainPosted = false;
        events.swap(m_bridge->events);
    }
    for (Event& event : events) {
        if (!m_bridge || m_closedEmitted) {
            return;
        }
        switch (event.kind) {
        case Event::Kind::Description:
            emit localDescription(event.first, event.second);
            gatherIfReady();
            break;
        case Event::Kind::Candidate:
            emit localCandidate(event.first);
            break;
        case Event::Kind::GatheringComplete:
            emit gatheringComplete();
            break;
        case Event::Kind::Open:
            handleOpen();
            break;
        case Event::Kind::Message:
            if (isOpen()) {
                m_telemetry.receivedPayloadBytes += static_cast<quint64>(event.bytes.size());
                // Held, in order, until something listens: the Core sends
                // its hello the moment its end opens, which can be before
                // the device's end has been handed to its session.
                const QMetaMethod signal = QMetaMethod::fromSignal(&SessionTransport::textReceived);
                if (!m_held.isEmpty() || !isSignalConnected(signal)) {
                    m_held.append(event.bytes);
                    scheduleHeldDelivery();
                } else {
                    ++m_messagesDelivered;
                    emit textReceived(event.bytes);
                }
            }
            break;
        case Event::Kind::Pong:
            // A pong for a ping this end sent; any other is ignored.
            if (event.id != 0 && event.id < m_nextPingId) {
                ++m_pongsReceived;
                if (m_pingSentAt.isValid()) {
                    m_telemetry.pongRttMs = static_cast<quint64>(m_pingSentAt.elapsed());
                }
                m_pongAge.start();
                emit pongReceived();
            }
            break;
        case Event::Kind::Refused:
            // As the WebSocket's cap: the connection ends.
            qCWarning(lcControlChannel) << "Ending the control connection:" << event.first;
            closeLink(event.first);
            break;
        case Event::Kind::Closed:
        case Event::Kind::Failed:
            if (!m_closing) {
                if (event.kind == Event::Kind::Failed) {
                    qCInfo(lcControlChannel) << event.first;
                }
                m_closing = true;
                stopPeer();
                finishClose();
            }
            break;
        }
    }
}

void DataChannelTransport::connectNotify(const QMetaMethod& signal)
{
    SessionTransport::connectNotify(signal);
    if (signal == QMetaMethod::fromSignal(&SessionTransport::textReceived)) {
        scheduleHeldDelivery();
    }
}

void DataChannelTransport::scheduleHeldDelivery()
{
    if (m_held.isEmpty() || m_heldDeliveryPosted) {
        return;
    }
    m_heldDeliveryPosted = true;
    QMetaObject::invokeMethod(this, [this]() { deliverHeld(); }, Qt::QueuedConnection);
}

void DataChannelTransport::deliverHeld()
{
    m_heldDeliveryPosted = false;
    const QMetaMethod signal = QMetaMethod::fromSignal(&SessionTransport::textReceived);
    while (!m_held.isEmpty() && isOpen() && isSignalConnected(signal)) {
        const QByteArray message = m_held.takeFirst();
        ++m_messagesDelivered;
        emit textReceived(message);
    }
}

void DataChannelTransport::handleOpen()
{
    if (m_open || m_closing) {
        return;
    }
    m_open = true;
    emit opened();
}

void DataChannelTransport::stopPeer()
{
    if (!m_bridge) {
        return;
    }
    std::shared_ptr<rtc::PeerConnection> peer;
    std::shared_ptr<rtc::DataChannel> channel;
    {
        std::lock_guard lock(m_bridge->mutex);
        m_bridge->cancelled = true;
        m_bridge->events.clear();
        peer = std::move(m_bridge->peer);
        channel = std::move(m_bridge->channel);
    }
    try {
        if (channel) {
            channel->resetCallbacks();
            channel->close();
        }
        if (peer) {
            peer->resetCallbacks();
            // Closing the peer ends ICE, which gives back any relay
            // allocation (cmake/NereusRemoteMedia.cmake's libjuice change).
            peer->close();
        }
    } catch (const std::exception&) {
        // Closing what is already closing.
    }
    // m_bridge stays, cancelled, for the counters and for selectedPath()
    // returning nothing.
}

} // namespace NereusSDR
