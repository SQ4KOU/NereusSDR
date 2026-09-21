// =================================================================
// src/core/session/StationServer.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R2 Task 18.
// See StationServer.h for the connect sequence, the one-session topology
// decision, the threading invariant, and the heartbeat's detection model.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-08  J.J. Boyd / KG4VCF  Remote daemon R2 Task 18: the daemon
//                                    half of the wss session. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
//   2026-08-09  J.J. Boyd / KG4VCF  Whole-branch review, Important 4:
//                                    relay a settings removal as an
//                                    absence frame, not as a value of "".
//                                    AI-assisted transformation via
//                                    Anthropic Claude Code.
//   2026-08-09  J.J. Boyd / KG4VCF  Whole-branch review, Minor 4:
//                                    handlePropertyWrite() answers one
//                                    inbound frame with one snapshot and
//                                    one outbound frame, not N of each.
//                                    AI-assisted transformation via
//                                    Anthropic Claude Code.
// =================================================================

#include "core/session/StationServer.h"

#include "core/AppSettings.h"
#include "core/BoardCapabilities.h"
#include "core/security/CertificateStore.h"
#include "core/security/TokenStore.h"
#include "core/session/ObjectRegistry.h"
#include "core/session/SessionCommandDispatcher.h"
#include "core/session/SessionTransport.h"
#include "core/session/StateMirror.h"
#include "core/settings/SettingsProxyServer.h"
#include "core/settings/SettingsScope.h"
#include "models/PanadapterModel.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"
#include "models/TunerModel.h"

#include <QLoggingCategory>
#include <QSet>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QTimer>
#include <QWebSocket>
#include <QWebSocketServer>

#include <algorithm>
#include <cstdio>

namespace NereusSDR {

namespace {
Q_LOGGING_CATEGORY(lcStation, "nereus.station")

// The wire identities the five singleton mirrored models are watched
// under. Slices use ObjectRegistry::keyForSlice() instead, which is
// already shared with the daemon's own lifecycle tracking.
constexpr const char* kRadioKey = "radio";
constexpr const char* kTransmitKey = "transmit";
constexpr const char* kTunerKey = "tuner";

QByteArray panKey(int index)
{
    return QByteArrayLiteral("pan:") + QByteArray::number(index);
}

QString peerNameForThisProcess()
{
    return QStringLiteral("nereusd");
}

// Each side's own AppSettings schema version, read by the key name
// AppSettings::ensureSettingsAtVersion() writes it under. Read rather than
// hardcoded: the literal lives at exactly one place today (CoreInit.cpp's
// ensureSettingsAtVersion(6) call), and duplicating it here would create a
// second copy free to drift from the migrations that actually ran.
qint32 settingsSchemaVersionOf(const AppSettings& settings)
{
    return static_cast<qint32>(
        settings.value(QStringLiteral("SettingsSchemaVersion"), QStringLiteral("0"))
            .toString()
            .toInt());
}

// Written straight to stdout with C stdio, deliberately NOT through
// qCInfo() like every other line in this class. That is a correctness fix,
// not a style preference, and it closes two separate defects.
//
// FIRST, the banner was arriving MANGLED. CoreInit::initialize() installs
// a process-wide qInstallMessageHandler whose handler passes every message
// through redactPii() before it reaches stderr or the log file, and
// redactPii's MAC rule used to be a bare six-pair hex body -- which is a
// strict prefix of the 32-pair colon-separated SHA-256 fingerprint printed
// two lines below. It matched five times over inside one fingerprint and
// replaced 25 of its 32 bytes with asterisks, while the banner still said
// the values were printed once, here. StationClient::connectToStation
// refuses to dial without a fingerprint and nereusd has no option to
// reprint one, so that left an operator with no way forward. redactPii is
// now narrowed too (CoreInit.cpp), because a fingerprint logged from
// anywhere else would otherwise still be destroyed; this function is the
// other half, not a substitute for it.
//
// SECOND, and the reason the fix is a different STREAM rather than a
// different regex: that same handler writes every message verbatim into
// ~/.config/NereusSDR/profiles/<profile>/nereussdr-<stamp>.log, kept
// indefinitely and symlinked as nereussdr.log. That is the file
// CONTRIBUTING.md tells operators to attach to a bug report. Routing the
// token through it contradicts TokenStore.h's own stated reason for
// keeping the secret out of AppSettings -- "a secret sitting in the same
// XML the operator backs up, mails to a maintainer with a bug report" --
// against a worse medium than the one that header rejects. Bypassing the
// handler entirely is what keeps the token out of the log file; no
// redaction rule could, because the token is 43 characters of base64url
// with no shape to match on.
//
// STDOUT rather than stderr. Under packaging/nereusd.service.in this
// process sets neither StandardOutput= nor StandardError=, so systemd's
// defaults put both streams in the journal and the banner reaches
// `journalctl -u nereusd` either way; the systemd case does not decide it.
// What decides it is what each stream means. This banner is the run's
// primary output, two values the operator is being asked to copy, not a
// diagnostic. stderr in this process is already owned by the Qt handler,
// so putting the banner there would interleave a copy-paste block with
// redacted diagnostic lines, and an operator debugging by hand with
// `nereusd 2> daemon-errors.log` would lose it off the terminal.
//
// The explicit fflush is load-bearing rather than hygiene: stdout is
// block-buffered whenever it is not a terminal, which is exactly the
// systemd case, so without it the banner would sit in libc's buffer until
// it filled or the daemon exited.
void writePairingBanner(const QString& banner)
{
    const QByteArray bytes = banner.toUtf8();
    std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stdout);
    std::fflush(stdout);
}
} // namespace

StationServer::StationServer(RadioModel* radioModel, AppSettings& settings,
                             const QString& securityDirectory, QObject* parent)
    : QObject(parent)
    , m_radioModel(radioModel)
    , m_settings(settings)
    , m_securityDirectory(securityDirectory.isEmpty() ? CertificateStore::defaultDirectory()
                                                      : securityDirectory)
{
    // Every capability set this R3 server advertises is receive-only. Make
    // that a persistent property of the hardware-owning model as well, so a
    // standalone StationServer host cannot admit a TX/accessory side effect
    // through a local callback. DaemonApp installs the same policy earlier,
    // before startup; neither owner clears it when a session ends.
    if (m_radioModel) {
        m_radioModel->setReceiveOnlyStationPolicy(true);
    }

    m_certificates = std::make_unique<CertificateStore>(m_securityDirectory);
    m_tokens = std::make_unique<TokenStore>(m_securityDirectory);

    // Step 4: the token distribution mechanism parent section 7.1 requires
    // be specified before R2. Printed ONCE, on the run that generates it,
    // beside the certificate fingerprint the client pins (section 10.5) --
    // the two things an operator has to carry to the client by hand, in
    // one place, at the one moment they are new. Deliberately not repeated
    // on later starts: a secret echoed into every log file forever is a
    // different problem from a secret nobody can find.
    if (m_tokens->wasGeneratedThisRun()) {
        writePairingBanner(formatPairingBanner(m_tokens->token(),
                                               m_certificates->fingerprintSha256(),
                                               m_securityDirectory));
        // The LOG gets a pointer, never either secret. Without this line a
        // first run leaves no trace at all in the file an operator goes
        // looking in, which is its own support problem; with it, the log
        // says what happened and where the values went without carrying
        // them. See writePairingBanner() for why they went to stdout.
        qCInfo(lcStation)
            << "First run for this profile: a pairing token and a TLS certificate "
               "fingerprint were generated and printed to stdout. Both are "
               "deliberately kept out of this log file.";
    }
    if (!m_tokens->isValid()) {
        qCWarning(lcStation) << "Auth token unavailable:" << m_tokens->lastError();
    }

    m_mirror = new StateMirror(this);
    m_registry = new ObjectRegistry(radioModel, m_mirror, this);
    m_dispatcher = new SessionCommandDispatcher(radioModel, this);
    m_settingsServer = new SettingsProxyServer(settings, this);

    // Outbound: everything the daemon has to say goes to whichever
    // transport currently holds the session, and to nothing at all when
    // there is none.
    connect(m_mirror, &StateMirror::sessionMessageReady, this,
            [this](const SessionMessage& message) { sendToSession(message); });
    connect(m_dispatcher, &SessionCommandDispatcher::commandResultReady, this,
            [this](const SessionMessage& result) { sendToSession(result); });
    connect(m_settingsServer, &SettingsProxyServer::outboundValueChanged, this,
            [this](const QString& key, const QVariant& value, const QString& originTag) {
                sendToSession(
                    SessionMessages::settingsValue(key, value.toString(), originTag));
            });
    // Whole-branch review, Important 4. A removal has its own signal and
    // its own frame. It used to arrive here as an outboundValueChanged
    // carrying an INVALID QVariant, and the value.toString() above turned
    // that into "" -- so every client cached an empty string for a key
    // the station no longer had, and a client that had just correctly
    // removed the key itself had it resurrected by the echo.
    connect(m_settingsServer, &SettingsProxyServer::outboundValueRemoved, this,
            [this](const QString& key) {
                sendToSession(SessionMessages::settingsValueAbsent(key, QString()));
            });

    // ObjectRegistry's create/destroy events are the lifecycle half of the
    // mirror; StateMirror only carries property deltas for objects it
    // already knows about.
    connect(m_registry, &ObjectRegistry::objectCreated, this,
            [this](const QByteArray& objectKey, const QByteArray& className, int,
                   const QList<MirrorUpdate>& snapshot) {
                sendToSession(
                    SessionMessages::objectCreate(objectKey, className, snapshot));
            });
    connect(m_registry, &ObjectRegistry::objectDestroyed, this,
            [this](const QByteArray& objectKey, const QByteArray& className, int) {
                sendToSession(SessionMessages::objectDestroy(objectKey, className));
            });

    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(m_heartbeatIntervalMs);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &StationServer::onHeartbeatTick);

    m_deltaFlushTimer = new QTimer(this);
    m_deltaFlushTimer->setInterval(kDefaultDeltaFlushMs);
    connect(m_deltaFlushTimer, &QTimer::timeout, this, [this]() {
        if (m_session != nullptr && m_mirror != nullptr) {
            m_mirror->flushCoalescedDeltas();
        }
    });
}

StationServer::~StationServer()
{
    close();
}

// ── Listener lifecycle ───────────────────────────────────────────────────

bool StationServer::listen(const QHostAddress& address, quint16 port)
{
    m_lastError.clear();

    // Idempotent. A second call used to re-apply the SSL configuration and
    // then fail the bind into lastError(), so a caller that could not
    // cheaply tell whether it had already started ended up with a working
    // listener AND an error string describing it as broken. Rebinding
    // somewhere else is close() then listen() again, deliberately explicit.
    if (isListening()) {
        qCDebug(lcStation) << "listen() ignored: already listening on port"
                            << m_wsServer->serverPort();
        return true;
    }

    if (!QSslSocket::supportsSsl()) {
        m_lastError = CertificateStore::tlsBackendDiagnostic();
        if (m_lastError.isEmpty()) {
            m_lastError = QStringLiteral("Qt reports no working TLS backend");
        }
        qCWarning(lcStation) << "Refusing to listen:" << m_lastError;
        return false;
    }
    if (!m_certificates->isValid()) {
        m_lastError = m_certificates->lastError();
        qCWarning(lcStation) << "Refusing to listen:" << m_lastError;
        return false;
    }
    if (!m_tokens->isValid()) {
        // Listening with no token would accept nobody, forever, while
        // looking healthy. Refuse loudly instead.
        m_lastError = m_tokens->lastError().isEmpty()
                          ? QStringLiteral("No authentication token available")
                          : m_tokens->lastError();
        qCWarning(lcStation) << "Refusing to listen:" << m_lastError;
        return false;
    }

    if (m_wsServer == nullptr) {
        m_wsServer = new QWebSocketServer(QStringLiteral("NereusSDR station"),
                                          QWebSocketServer::SecureMode, this);
        connect(m_wsServer, &QWebSocketServer::newConnection, this,
                &StationServer::onNewWebSocketConnection);
    }

    QSslConfiguration tls = QSslConfiguration::defaultConfiguration();
    tls.setLocalCertificate(m_certificates->certificate());
    tls.setPrivateKey(m_certificates->privateKey());
    // The client pins this certificate's fingerprint (parent design
    // section 10.5), so it is the client's job to decide whether to trust
    // it. Asking for a client certificate here would be a second,
    // unimplemented identity mechanism.
    tls.setPeerVerifyMode(QSslSocket::VerifyNone);
    m_wsServer->setSslConfiguration(tls);

    if (!m_wsServer->listen(address, port)) {
        m_lastError = m_wsServer->errorString();
        qCWarning(lcStation) << "Listen failed:" << m_lastError;
        return false;
    }

    qCInfo(lcStation) << "Station listening on wss://" << address.toString() << ":"
                      << m_wsServer->serverPort();
    return true;
}

void StationServer::close()
{
    const QList<SessionTransport*> transports = m_peers.keys();
    for (SessionTransport* transport : transports) {
        dropPeer(transport, QStringLiteral("station shutting down"), true,
                 /*retryable=*/true);
    }
    if (m_wsServer != nullptr) {
        m_wsServer->close();
    }
    if (m_heartbeatTimer != nullptr) {
        m_heartbeatTimer->stop();
    }
    if (m_deltaFlushTimer != nullptr) {
        m_deltaFlushTimer->stop();
    }
}

bool StationServer::isListening() const
{
    return m_wsServer != nullptr && m_wsServer->isListening();
}

quint16 StationServer::serverPort() const
{
    return m_wsServer != nullptr ? m_wsServer->serverPort() : 0;
}

QString StationServer::token() const
{
    return m_tokens != nullptr ? m_tokens->token() : QString();
}

QString StationServer::certificateFingerprint() const
{
    return m_certificates != nullptr ? m_certificates->fingerprintSha256() : QString();
}

QString StationServer::formatPairingBanner(const QString& token,
                                           const QString& fingerprint,
                                           const QString& storedIn)
{
    return QStringLiteral(
               "\n"
               "  ============================================================\n"
               "  NereusSDR station: first run, pairing details\n"
               "  ------------------------------------------------------------\n"
               "  Token:       %1\n"
               "  TLS SHA-256: %2\n"
               "  Stored in:   %3\n"
               "  ------------------------------------------------------------\n"
               "  Give both to the client. They are printed once, here, on\n"
               "  stdout, and are deliberately kept out of the log file.\n"
               "  ============================================================\n")
        .arg(token, fingerprint, storedIn);
}

bool StationServer::hasAuthenticatedSession() const
{
    return m_session != nullptr;
}

// ── Peer lifecycle ───────────────────────────────────────────────────────

void StationServer::onNewWebSocketConnection()
{
    while (m_wsServer != nullptr && m_wsServer->hasPendingConnections()) {
        QWebSocket* socket = m_wsServer->nextPendingConnection();
        if (socket == nullptr) {
            break;
        }
        // The cap goes on inside WebSocketTransport's constructor, which
        // runs here, inside the newConnection slot, before control returns
        // to the event loop -- so no frame on this socket has been
        // processed yet. See kMaxIncomingMessageBytes for the arithmetic
        // and for why an uncapped accepted socket is a pre-authentication
        // memory-exhaustion path rather than a theoretical one.
        acceptTransport(
            new WebSocketTransport(socket, kMaxIncomingMessageBytes));
    }
}

void StationServer::acceptTransport(SessionTransport* transport)
{
    if (transport == nullptr) {
        return;
    }
    transport->setParent(this);

    // Peer cap. Refused BEFORE any state is allocated for it, and with a
    // reason on the wire so a legitimate client that hits this knows why
    // rather than seeing an unexplained close. Only ever one session is
    // authenticated (see the topology decision in the header), so this
    // bounds peers that are mid-handshake.
    if (m_peers.size() >= kMaxConcurrentPeers) {
        qCWarning(lcStation) << "Refusing connection from" << transport->peerDescription()
                             << ": already at" << kMaxConcurrentPeers << "peers";
        // RETRYABLE, and this is the one that mattered most. The header
        // sizes kMaxConcurrentPeers for "one client, and a couple of stale
        // sockets from a reconnecting client", so this cap is expected to
        // be hit BY a reconnecting client, transiently, while its own dead
        // sockets are still draining. Sent as permanent, it told exactly
        // that client to stop trying forever.
        transport->sendText(SessionMessages::encode(SessionMessages::sessionEnd(
            QStringLiteral("Station is at its concurrent-connection limit"),
            /*retryable=*/true)));
        transport->closeLink(QStringLiteral("peer limit reached"));
        transport->deleteLater();
        return;
    }

    Peer peer;
    peer.transport = transport;
    peer.description = transport->peerDescription();

    // Authenticate-or-drop. Parented to the transport so it cannot outlive
    // the peer it is about, and stopped the moment authentication
    // succeeds. An OWNED single-shot timer, not static
    // QTimer::singleShot: cancellability is the whole point.
    if (m_authDeadlineMs > 0) {
        auto* deadline = new QTimer(transport);
        deadline->setSingleShot(true);
        deadline->setInterval(m_authDeadlineMs);
        connect(deadline, &QTimer::timeout, this, [this, transport]() {
            auto it = m_peers.find(transport);
            if (it == m_peers.end() || it->authenticated) {
                return;
            }
            qCWarning(lcStation) << "Dropping" << it->description
                                 << ": did not authenticate within" << m_authDeadlineMs
                                 << "ms";
            dropPeer(transport, QStringLiteral("handshake deadline expired"), true,
                     /*retryable=*/true);
        });
        deadline->start();
        peer.authDeadline = deadline;
    }

    m_peers.insert(transport, peer);

    connect(transport, &SessionTransport::textReceived, this,
            [this, transport](const QByteArray& wire) { onTransportText(transport, wire); });
    connect(transport, &SessionTransport::pongReceived, this, [this, transport]() {
        auto it = m_peers.find(transport);
        if (it != m_peers.end()) {
            it->pingsAwaitingPong = 0;
        }
    });
    connect(transport, &SessionTransport::closed, this,
            [this, transport]() { onTransportClosed(transport); });

    if (!m_heartbeatTimer->isActive() && m_heartbeatIntervalMs > 0) {
        m_heartbeatTimer->start();
    }

    // The daemon greets first, so a client can refuse on a major version
    // mismatch without ever having sent its token. Section 7.0's sequence
    // does not fix which end speaks first; sending it in the direction
    // that avoids exposing a secret to an incompatible peer is this
    // task's own choice, recorded here.
    send(transport,
         SessionMessages::hello(kSessionProtocolMajor, kSessionProtocolMinor,
                                settingsSchemaVersionOf(m_settings),
                                peerNameForThisProcess()));

    qCDebug(lcStation) << "Peer attached:" << peer.description;
}

void StationServer::onTransportClosed(SessionTransport* transport)
{
    dropPeer(transport, QStringLiteral("peer closed the link"), false,
             /*retryable=*/true);
}

void StationServer::dropPeer(SessionTransport* transport, const QString& reason,
                             bool sendSessionEnd, bool retryable)
{
    auto it = m_peers.find(transport);
    if (it == m_peers.end()) {
        return;
    }
    const QString description = it->description;

    if (sendSessionEnd) {
        send(transport, SessionMessages::sessionEnd(reason, retryable));
    }
    m_peers.erase(it);

    if (m_session == transport) {
        m_session = nullptr;
        if (!m_radioModel.isNull()) {
            m_radioModel->clearStreamCtunPins();
        }
        emit mediaSessionEnded(m_mediaSessionEpoch);
        // Stop draining deltas into nothing. StateMirror keeps watching --
        // the daemon's own state is not the session's to tear down -- and
        // the next attachSession() clears whatever the coalescer holds
        // anyway, because a fresh burst already carries every watched
        // object's current value.
        m_deltaFlushTimer->stop();
    }

    transport->closeLink(reason);
    transport->deleteLater();

    if (m_peers.isEmpty() && m_heartbeatTimer != nullptr) {
        m_heartbeatTimer->stop();
    }

    qCInfo(lcStation) << "Peer detached:" << description << "reason:" << reason;
    emit peerDisconnected(description, reason);
}

// ── Heartbeat ────────────────────────────────────────────────────────────

void StationServer::setHeartbeatIntervalMs(int ms)
{
    m_heartbeatIntervalMs = ms;
    if (ms <= 0) {
        qCWarning(lcStation)
            << "Heartbeat disabled. A peer that dies without closing the TCP "
               "connection will not be detected.";
        m_heartbeatTimer->stop();
        return;
    }
    m_heartbeatTimer->setInterval(ms);
    if (!m_peers.isEmpty()) {
        m_heartbeatTimer->start();
    }
}

void StationServer::setMaxMissedPongs(int misses)
{
    m_maxMissedPongs = misses < 1 ? 1 : misses;
}

void StationServer::setAuthDeadlineMs(int ms)
{
    m_authDeadlineMs = ms;
    if (ms < 1) {
        qCWarning(lcStation)
            << "Handshake deadline disabled. A peer that connects and answers pings "
               "but never authenticates will hold its slot indefinitely.";
    }
}

void StationServer::setAuthRateLimit(int maxFailures, int lockoutMs)
{
    if (m_tokens != nullptr) {
        m_tokens->setRateLimit(maxFailures, lockoutMs);
    }
}

void StationServer::onHeartbeatTick()
{
    // Copied deliberately: dropPeer() mutates m_peers, and a peer declared
    // dead here is dropped inside this loop.
    const QList<SessionTransport*> transports = m_peers.keys();
    for (SessionTransport* transport : transports) {
        auto it = m_peers.find(transport);
        if (it == m_peers.end()) {
            continue;
        }
        if (it->pingsAwaitingPong >= m_maxMissedPongs) {
            const QString description = it->description;
            qCWarning(lcStation)
                << "Peer" << description << "missed" << it->pingsAwaitingPong
                << "consecutive pongs; declaring the link dead";
            emit peerHeartbeatTimeout(description);
            dropPeer(transport, QStringLiteral("heartbeat timeout"), true,
                     /*retryable=*/true);
            continue;
        }
        ++it->pingsAwaitingPong;
        transport->ping();
    }
}

// ── Inbound dispatch ─────────────────────────────────────────────────────

void StationServer::onTransportText(SessionTransport* transport, const QByteArray& wire)
{
    auto it = m_peers.find(transport);
    if (it == m_peers.end()) {
        return;
    }

    SessionMessage message;
    if (!SessionMessages::decode(wire, &message)) {
        dropPeer(transport, QStringLiteral("undecodable message"), true,
                 /*retryable=*/false);
        return;
    }

    switch (message.kind) {
    case SessionMessageKind::Hello:
        handleHello(transport, message);
        return;
    case SessionMessageKind::AuthRequest:
        handleAuthRequest(transport, message);
        return;
    default:
        break;
    }

    if (!it->authenticated) {
        // Everything below this line moves radio or settings state. A peer
        // that has not proved it holds the token gets exactly one answer.
        dropPeer(transport, QStringLiteral("message sent before authentication"), true,
                 /*retryable=*/false);
        return;
    }

    switch (message.kind) {
    case SessionMessageKind::CommandInvoke:
        if ((message.commandVerb == "requestStreamCtunPinned"
             || message.commandVerb == "requestStreamCentre")
            && it->agreedMinor < kRemoteCtunSessionProtocolMinor) {
            send(transport, SessionMessages::commandResult(
                message.commandVerb, message.commandId, false,
                QStringLiteral("Remote C-Tune requires a newer station protocol."), {}));
            break;
        }
        m_dispatcher->dispatch(message);
        break;
    case SessionMessageKind::MediaControl:
        if (transport == m_session && mediaAvailable()) {
            emit mediaControlReceived(message.mediaPayload, m_mediaSessionEpoch);
        }
        break;
    case SessionMessageKind::PropertyWrite:
        handlePropertyWrite(transport, message);
        break;
    case SessionMessageKind::SettingsWrite:
        handleSettingsWrite(transport, message);
        break;
    case SessionMessageKind::SettingsRemove:
        handleSettingsRemove(message);
        break;
    default:
        // Every remaining kind is daemon-to-client. A client sending one
        // is confused rather than hostile, so it is logged and ignored
        // rather than being grounds to close a working session.
        qCWarning(lcStation) << "Ignoring client message of daemon-only kind:"
                             << SessionMessages::kindName(message.kind);
        break;
    }
}

void StationServer::handleHello(SessionTransport* transport, const SessionMessage& message)
{
    auto it = m_peers.find(transport);
    if (it == m_peers.end()) {
        return;
    }
    if (it->helloReceived) {
        dropPeer(transport, QStringLiteral("duplicate hello"), true, /*retryable=*/false);
        return;
    }
    it->helloReceived = true;

    // Parent design section 7.0's version policy, both halves.
    if (message.protocolMajor != kSessionProtocolMajor) {
        const QString reason =
            QStringLiteral("Protocol major version mismatch: station speaks %1.%2, "
                           "client speaks %3.%4. A differing major means an "
                           "incompatible wire contract.")
                .arg(kSessionProtocolMajor)
                .arg(kSessionProtocolMinor)
                .arg(message.protocolMajor)
                .arg(message.protocolMinor);
        qCWarning(lcStation) << reason;
        // NOT retryable: an incompatible wire contract does not become
        // compatible by being dialed again. The operator has to upgrade
        // one end.
        dropPeer(transport, reason, true, /*retryable=*/false);
        return;
    }

    // Equal major, differing minor: negotiate DOWN to the lower of the
    // two. A desktop GUI several releases ahead of a Pi still running this
    // one is the EXPECTED case, and it degrades rather than refusing.
    it->agreedMinor = std::min(kSessionProtocolMinor, message.protocolMinor);

    if (message.settingsSchemaVersion != settingsSchemaVersionOf(m_settings)) {
        // Not a refusal. The settings schema governs how each side's own
        // local store is shaped, not the wire contract, and the client is
        // the side that has to decide what to do about it (see
        // StationClient's own skew check). Logged here so a bench session
        // shows the skew from both ends.
        qCWarning(lcStation) << "Settings schema skew: station is at"
                             << settingsSchemaVersionOf(m_settings) << "client is at"
                             << message.settingsSchemaVersion;
    }

    qCDebug(lcStation) << "Hello from" << message.peerName << "version"
                       << message.protocolMajor << "." << message.protocolMinor
                       << "agreed minor" << it->agreedMinor;
}

void StationServer::handleAuthRequest(SessionTransport* transport,
                                      const SessionMessage& message)
{
    auto it = m_peers.find(transport);
    if (it == m_peers.end()) {
        return;
    }
    if (!it->helloReceived) {
        dropPeer(transport, QStringLiteral("auth before hello"), true, /*retryable=*/false);
        return;
    }
    if (it->authenticated) {
        dropPeer(transport, QStringLiteral("duplicate auth"), true, /*retryable=*/false);
        return;
    }

    const QString description = it->description;

    // The candidate token is never logged, at any level, on any path.
    const TokenStore::VerifyResult result = m_tokens->verify(message.token);
    if (result != TokenStore::VerifyResult::Accepted) {
        // THE distinction TokenStore.h says the two results exist to
        // preserve, carried through to the client's retry policy.
        //
        // RateLimited is retryable: it is transient BY CONSTRUCTION -- the
        // lockout expires on TokenStore's own timer, and the refusal text
        // literally says "try again later". Crucially, the rate limiter is
        // global rather than per-peer (TokenStore.h:44-48 says so outright:
        // a lockout refuses a connection "including one carrying the
        // correct token"), so five bad guesses from anyone who can reach
        // the port refuse the OPERATOR too. Marked permanent, that turned
        // somebody else's failed guesses into the operator being locked
        // out of their own station with no automatic recovery.
        //
        // Rejected is NOT retryable: the token is simply wrong, redialing
        // cannot make it right, and a client that retried forever would
        // feed the very rate limiter above and keep the station locked out
        // on the operator's own behalf.
        const bool rateLimited = result == TokenStore::VerifyResult::RateLimited;
        const QString reason =
            rateLimited
                ? QStringLiteral("Too many failed authentication attempts; try again later")
                : QStringLiteral("Authentication failed");
        send(transport, SessionMessages::authResult(false, reason, rateLimited));
        qCWarning(lcStation) << "Authentication refused for" << description << ":" << reason;
        dropPeer(transport, reason, false, rateLimited);
        return;
    }

    it->authenticated = true;
    if (it->authDeadline != nullptr) {
        it->authDeadline->stop();
    }
    send(transport, SessionMessages::authResult(true, QString(), /*retryable=*/false));
    promoteToSession(transport);
}

void StationServer::promoteToSession(SessionTransport* transport)
{
    const QString description = m_peers.value(transport).description;

    // Parent design section 7.1: "A second authenticated connection
    // preempts the existing session ... The displaced session is told
    // why." The token is the authority, and the realistic sequence is the
    // same operator reconnecting after a link drop from a different
    // device. Leaving the stale session in place, or refusing the second
    // connection, locks the operator out of their own transmitter for an
    // undefined interval.
    if (m_session != nullptr && m_session != transport) {
        const QString displaced = m_peers.contains(m_session)
                                      ? m_peers.value(m_session).description
                                      : QStringLiteral("<unknown>");
        const QString reason =
            QStringLiteral("Displaced by a newer authenticated connection from %1")
                .arg(description);
        qCInfo(lcStation) << "Preempting session" << displaced << "for" << description;
        // NOT retryable, and deliberately so even though the CONDITION is
        // transient. Another authenticated peer has deliberately taken the
        // session; a displaced client that redialed on a backoff would
        // preempt the newcomer straight back, and the two would trade the
        // radio between them indefinitely. Section 7.1 makes the token the
        // authority, so the most recent authenticated connection wins and
        // the displaced operator reconnects by hand.
        dropPeer(m_session, reason, true, /*retryable=*/false);
        emit sessionPreempted(displaced);
    }

    // BEFORE m_session is assigned, deliberately. buildMirror() ends in
    // ObjectRegistry::backfillExistingSlices(), which emits objectCreated
    // for every slice the daemon already holds -- and those are wired
    // straight to sendToSession(). With m_session still null they are
    // dropped, which is exactly right: attachSession() below sends an
    // object.create for every watched object anyway, AFTER the schema
    // messages that a client needs in order to make sense of one. Assign
    // first and the backfill's creates go out ahead of any schema, and
    // then get sent a second time by the burst.
    buildMirror();

    m_session = transport;
    ++m_mediaSessionEpoch;

    // Capability exchange (section 7.0 step 4). Sent before any state, so
    // the client has sized its own limits before the first object arrives.
    send(transport, SessionMessages::capabilities(buildCapabilities().toUpdates()));

    // State snapshot, settings half. Scoped to the connected radio's MAC
    // plus everything else Station-classified; see SettingsProxyServer.
    const QMap<QString, QString> snapshot = m_settingsServer->buildSnapshot(
        m_radioModel.isNull() ? QString() : m_radioModel->currentRadioMac());
    QList<MirrorUpdate> entries;
    entries.reserve(snapshot.size());
    for (auto it = snapshot.cbegin(); it != snapshot.cend(); ++it) {
        entries.append(
            MirrorUpdate{ 0, it.key().toUtf8(), MirrorWireKind::Utf8, QVariant(it.value()) });
    }
    send(transport, SessionMessages::settingsSnapshot(entries));

    // State snapshot, model half, ending in the snapshot-complete marker.
    // attachSession() emits the whole burst synchronously through
    // sessionMessageReady before it returns, which reaches sendToSession()
    // above -- and m_session is already set by now, which is what makes
    // the burst go anywhere at all.
    m_mirror->attachSession();
    if (m_session != transport || !m_peers.contains(transport)) {
        return;
    }
    m_peers[transport].snapshotComplete = true;

    if (!m_deltaFlushTimer->isActive()) {
        m_deltaFlushTimer->start();
    }

    qCInfo(lcStation) << "Session established with" << description;
    emit clientAuthenticated(description);
    if (m_session == transport && mediaAvailable()) {
        emit mediaSessionStarted(m_mediaSessionEpoch);
    }
}

// ── Mirror wiring ────────────────────────────────────────────────────────

void StationServer::buildMirror()
{
    if (m_mirrorBuilt || m_radioModel.isNull()) {
        return;
    }
    m_mirrorBuilt = true;

    m_mirror->watch(QByteArray(kRadioKey), m_radioModel.data());
    m_mirror->watch(QByteArray(kTransmitKey), &m_radioModel->transmitModel());
    if (m_radioModel->tunerModel() != nullptr) {
        m_mirror->watch(QByteArray(kTunerKey), m_radioModel->tunerModel());
    }
    const QList<PanadapterModel*> pans = m_radioModel->panadapters();
    for (int i = 0; i < pans.size(); ++i) {
        m_mirror->watch(panKey(i), pans.at(i));
    }

    // The third step of ObjectRegistry's three-step, and the one that is
    // easy to forget: the constructor only wires sliceAdded/sliceRemoved,
    // so every slice DaemonApp::start() already created is invisible until
    // this runs. StateMirror::snapshotAll() is not a substitute -- it walks
    // its own watch list and cannot discover an object nobody watched.
    // Called after the objectCreated/objectDestroyed wiring in the
    // constructor, which is the ordering its own doc comment requires.
    m_registry->backfillExistingSlices();
}

// ── Inbound state and settings ───────────────────────────────────────────

void StationServer::handlePropertyWrite(SessionTransport* transport,
                                        const SessionMessage& message)
{
    QSet<QByteArray> refused;
    const bool receiveOnlyTransmitWrite = message.objectKey == QByteArray(kTransmitKey)
        && !m_radioModel.isNull() && m_radioModel->receiveOnlyStationPolicy();
    for (const MirrorUpdate& update : message.updates) {
        // R3 advertises txPermitted=false and installs the matching persistent
        // model policy. TransmitModel remains bidirectional in the generic
        // mirror table for later phases and other contexts, so enforce the
        // station's current authority here before any setter can run.
        if (receiveOnlyTransmitWrite) {
            qCWarning(lcStation) << "Refused remote transmit write on receive-only station"
                                 << message.objectKey << "." << update.name;
            refused.insert(update.name);
            continue;
        }
        const MirrorApplyResult result =
            m_mirror->applyInbound(message.objectKey, update.name, update.value);
        if (result.accepted) {
            continue;
        }
        qCWarning(lcStation) << "Refused remote write" << message.objectKey << "."
                             << update.name << ":" << result.reason;
        refused.insert(update.name);
    }
    if (refused.isEmpty()) {
        return;
    }

    // Self-correcting: hand the peer back what the daemon actually holds
    // for every refused property, as an ordinary Delta, so a GUI control
    // that optimistically moved snaps back rather than displaying a value
    // the station never accepted.
    //
    // ONE snapshot and ONE send for the whole message. Whole-branch
    // review, Minor 4: this used to sit inside the loop above, so a single
    // inbound frame carrying N refusable properties cost N full-object
    // snapshots and N separately encoded outbound frames. Post-auth, so
    // not an unauthenticated amplifier, but a peer whose whole message is
    // refusable had no reason to be the cheapest thing in the session
    // either.
    //
    // Taking the snapshot after the loop rather than per refusal is also
    // the more correct answer, not merely the cheaper one: what the client
    // needs is the settled state once the entire message has been applied,
    // which is the mirror's own latest-wins principle. Reading it mid-loop
    // could hand back a value a later update in the same frame then
    // changed.
    QList<MirrorUpdate> corrections;
    const QList<MirrorUpdate> settled = m_mirror->snapshot(message.objectKey);
    for (const MirrorUpdate& live : settled) {
        if (refused.contains(live.name)) {
            corrections.append(live);
        }
    }
    if (!corrections.isEmpty()) {
        send(transport, SessionMessages::delta(message.objectKey, corrections));
    }
}

void StationServer::handleSettingsWrite(SessionTransport* transport,
                                        const SessionMessage& message)
{
    if (message.updates.isEmpty()) {
        return;
    }
    const QString key = QString::fromUtf8(message.objectKey);
    const SettingsApplyResult result =
        m_settingsServer->applyInboundWrite(key, message.updates.first().value,
                                            message.originTag);
    if (!result.accepted) {
        qCWarning(lcStation) << "Refused remote settings write" << key << ":"
                             << result.reason;
        send(transport,
             SessionMessages::settingsReject(key, result.restoredValue.isValid(),
                                             result.restoredValue.toString()));
    }
}

void StationServer::handleSettingsRemove(const SessionMessage& message)
{
    const QString key = QString::fromUtf8(message.objectKey);
    // SettingsProxyServer has no remove path of its own: AppSettings::
    // remove() fires the same Task 13 change hook a setValue() does, so
    // the broadcast that reaches every client is produced by the same
    // generic path, with an empty origin tag. Routed through the daemon's
    // own store directly, and gated on the same Station classification
    // applyInboundWrite() enforces so a client cannot reach an
    // OperatorLocal key by removing it instead of writing it.
    if (classifySettingsKey(key) != SettingsScope::Station) {
        qCWarning(lcStation) << "Refused remote settings remove of non-station key" << key;
        return;
    }
    m_settings.remove(key);
}

// ── Send helpers ─────────────────────────────────────────────────────────

void StationServer::send(SessionTransport* transport, const SessionMessage& message)
{
    if (transport == nullptr) {
        return;
    }
    transport->sendText(SessionMessages::encode(message));
}

void StationServer::sendToSession(const SessionMessage& message)
{
    if (m_session == nullptr) {
        return;
    }
    m_session->sendText(SessionMessages::encode(message));
}

void StationServer::setMediaEnabled(bool enabled)
{
    // A live session negotiated its capability already. Do not advertise a
    // different contract midway through it.
    if (!m_session) {
        m_mediaEnabled = enabled;
    }
}

bool StationServer::mediaAvailable() const
{
    const auto it = m_peers.constFind(m_session);
    return m_mediaEnabled && it != m_peers.cend() && it->authenticated
        && it->snapshotComplete && it->agreedMinor >= kMediaSessionProtocolMinor;
}

bool StationServer::sendMediaControl(const QJsonObject& payload, quint64 expectedEpoch)
{
    if (!mediaAvailable() || expectedEpoch != m_mediaSessionEpoch) {
        return false;
    }
    SessionMessage message;
    message.kind = SessionMessageKind::MediaControl;
    message.mediaPayload = payload;
    const QByteArray wire = SessionMessages::encode(message);
    if (wire.isEmpty()) {
        return false;
    }
    m_session->sendText(wire);
    return true;
}

// ── Capability descriptor ────────────────────────────────────────────────

void StationServer::setSustainableSliceLimit(int slices)
{
    if (slices < 1) {
        return;
    }
    m_sustainableSliceLimit = slices;
}

StationCapabilities StationServer::buildCapabilities() const
{
    StationCapabilities caps;
    caps.settingsSchemaVersion = settingsSchemaVersionOf(m_settings);
    if (m_radioModel.isNull()) {
        return caps;
    }

    const BoardCapabilities& board = m_radioModel->boardCapabilities();

    caps.stationName = m_radioModel->name();
    caps.radioModelName = m_radioModel->model();
    caps.firmwareVersion = m_radioModel->version();
    caps.macAddress = m_radioModel->currentRadioMac();
    caps.board = board.board;
    caps.radioConnected = m_radioModel->isConnected();

    caps.boardMaxSlices = board.maxSlices > 0 ? board.maxSlices : 1;
    caps.userDdcCount = board.userDdcCount;
    caps.pureSignalPresent = board.hasPureSignal;

    // EFFECTIVE, not board (parent section 4.5). R2 has no PerfMonitor to
    // compute a sustainable number, so the effective value is whatever an
    // operator configured, clamped to what the radio can actually do --
    // advertising more slices than the board has would be a worse failure
    // than advertising fewer.
    caps.effectiveMaxSlices = m_sustainableSliceLimit > 0
                                  ? std::min(m_sustainableSliceLimit, caps.boardMaxSlices)
                                  : caps.boardMaxSlices;

    // Always false in R2: TX is R4 in its entirety.
    caps.txPermitted = false;
    caps.remoteMediaVersion = m_mediaEnabled ? 1 : 0;
    caps.remoteCtunVersion = 1;

    return caps;
}

} // namespace NereusSDR
