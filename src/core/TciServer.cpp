// no-port-check: AetherSDR-derived NereusSDR file.  Transport lifecycle
// (start/stop/onNewConnection/onClientDisconnected) is adapted from
// AetherSDR src/core/TciServer.{h,cpp} [@0cd4559]; NereusSDR diverges in
// bind address, double-start contract, signal set, and client table type.
// Registered in docs/attribution/aethersdr-reconciliation.md.

// src/core/TciServer.cpp  (NereusSDR)
// NereusSDR-original — TCI WebSocket server implementation.
//
// Transport pattern ported from AetherSDR src/core/TciServer.{h,cpp} [@0cd4559].
// Per-client field set condensed from Thetis TCIServer.cs:684-790 [v2.10.3.13].
//
// Modification history (NereusSDR):
//   2026-05-10 — Phase 3J-1 Task 2.1 by J.J. Boyd (KG4VCF);
//                AI-assisted transformation via Anthropic Claude Code.

#ifdef HAVE_WEBSOCKETS

#include "TciServer.h"
#include "TciClientSession.h"
#include "LogCategories.h"

#include <QHostAddress>
#include <QWebSocket>
#include <QWebSocketServer>
#include <QDateTime>

namespace NereusSDR {

// ── Constructor / destructor ─────────────────────────────────────────────────
//
// Phase 2 Task 2.1: constructor body is intentionally empty — no meter timers,
// no TX-chrono timer, no status-received wiring.  Those arrive in later phases:
//   - Phase 9:  meter broadcast timer (broadcastStatus at 200 ms)
//   - Phase 17: TX_CHRONO timer for WSJT-X
// AetherSDR src/core/TciServer.cpp:53-152 [@0cd4559] shows what the full
// constructor looks like; we port only what Phase 2 needs.

TciServer::TciServer(RadioModel* model, QObject* parent)
    : QObject(parent)
    , m_model(model)
{
}

TciServer::~TciServer()
{
    stop();
}

// ── start() ─────────────────────────────────────────────────────────────────
//
// From AetherSDR src/core/TciServer.cpp:159-181 [@0cd4559] — transport pattern.
// NereusSDR diverges from AetherSDR in two ways:
//   1. Bind address: QHostAddress::LocalHost (AetherSDR uses QHostAddress::Any).
//      Per design doc Q7 lock-in — TCI is a local-process IPC bus; binding to
//      Any would expose the unfinished server to the LAN without auth.
//   2. double-start contract: return false + log warning (AetherSDR returns
//      m_server->isListening(), treating double-start as idempotent-true).
//      NereusSDR rejects double-start so the caller can detect misuse early.

bool TciServer::start(quint16 port)
{
    if (m_server) {
        qCWarning(lcTci) << "TciServer::start called while already listening on port"
                         << m_server->serverPort();
        return false;
    }

    m_server = new QWebSocketServer(
        QStringLiteral("NereusSDR-TCI"),
        QWebSocketServer::NonSecureMode, this);

    // From AetherSDR src/core/TciServer.cpp:168-174 [@0cd4559] — listen + error path.
    // QHostAddress::LocalHost per design doc Q7 (diverges from AetherSDR's ::Any).
    if (!m_server->listen(QHostAddress::LocalHost, port)) {
        qCWarning(lcTci) << "TciServer: failed to listen on port" << port
                         << m_server->errorString();
        const QString errStr = m_server->errorString();
        delete m_server;
        m_server = nullptr;
        emit errorOccurred(errStr);
        return false;
    }

    connect(m_server, &QWebSocketServer::newConnection,
            this, &TciServer::onNewConnection);

    qCInfo(lcTci) << "TciServer: listening on" << m_server->serverPort();
    emit serverStarted(m_server->serverPort());
    return true;
}

// ── stop() ───────────────────────────────────────────────────────────────────
//
// From AetherSDR src/core/TciServer.cpp:184-207 [@0cd4559] — disconnect-and-
// cleanup loop pattern.  NereusSDR uses QHash iteration instead of QList.

void TciServer::stop()
{
    if (!m_server) { return; }

    // Disconnect all connected clients.  We disconnect the socket's signals
    // from this object first to prevent onClientDisconnected() re-entry during
    // the explicit close() calls.
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        QWebSocket* ws = it.key();
        ws->disconnect(this);
        ws->close();
        ws->deleteLater();
    }
    m_clients.clear();

    m_server->close();
    delete m_server;
    m_server = nullptr;

    qCInfo(lcTci) << "TciServer: stopped";
    emit serverStopped();
}

// ── isRunning() / port() ─────────────────────────────────────────────────────
//
// From AetherSDR src/core/TciServer.cpp:209-217 [@0cd4559]

bool TciServer::isRunning() const
{
    return m_server && m_server->isListening();
}

quint16 TciServer::port() const
{
    return m_server ? m_server->serverPort() : 0;
}

// ── onNewConnection() ────────────────────────────────────────────────────────
//
// From AetherSDR src/core/TciServer.cpp:247-273 [@0cd4559] — accept-loop
// pattern adapted to the QHash<QWebSocket*, shared_ptr<TciClientSession>> table.

void TciServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        auto* ws = m_server->nextPendingConnection();
        auto session = std::make_shared<TciClientSession>();
        session->socket = ws;
        session->peer   = ws->peerAddress().toString()
                        + QStringLiteral(":")
                        + QString::number(ws->peerPort());
        // TODO Phase 4: extract userAgent from QWebSocket request header
        // (ws->request() is available after the WebSocket handshake; the
        // User-Agent HTTP header maps to session->userAgent).
        session->connectedAt.start();
        m_clients.insert(ws, session);

        connect(ws, &QWebSocket::textMessageReceived,
                this, &TciServer::onTextMessageReceived);
        connect(ws, &QWebSocket::binaryMessageReceived,
                this, &TciServer::onBinaryMessageReceived);
        connect(ws, &QWebSocket::disconnected,
                this, &TciServer::onClientDisconnected);

        qCInfo(lcTci) << "TciServer: client connected from" << session->peer;
        emit clientConnected(ws);
    }
}

// ── onClientDisconnected() ───────────────────────────────────────────────────
//
// From AetherSDR src/core/TciServer.cpp:275+ [@0cd4559] — sender()-based
// lookup pattern, adapted from QList linear search to QHash O(1) lookup.

void TciServer::onClientDisconnected()
{
    auto* ws = qobject_cast<QWebSocket*>(sender());
    if (!ws) { return; }

    auto it = m_clients.find(ws);
    if (it == m_clients.end()) { return; }

    qCInfo(lcTci) << "TciServer: client disconnected from" << it.value()->peer;
    emit clientDisconnected(ws);

    m_clients.erase(it);
    ws->deleteLater();
}

// ── onTextMessageReceived() ──────────────────────────────────────────────────
//
// Phase 2 stub — records the command for ClientChainApplet display.
// Phase 3 Task 3.2 replaces the TODO below with TciProtocol dispatch +
// per-client broadcast notification forwarding.

void TciServer::onTextMessageReceived(const QString& msg)
{
    auto* ws = qobject_cast<QWebSocket*>(sender());
    if (!ws) { return; }
    auto it = m_clients.find(ws);
    if (it == m_clients.end()) { return; }

    it.value()->lastCommand   = msg;
    it.value()->lastCommandAt = QDateTime::currentMSecsSinceEpoch();

    // TODO Phase 3 Task 3.2: dispatch via TciProtocol::handleCommand and
    // broadcast change notifications to all other connected clients.
}

// ── onBinaryMessageReceived() ────────────────────────────────────────────────
//
// Phase 2 stub.
// Phase 17 wires the full dispatch: parse the 64-byte TCI audio header,
// identify TX_AUDIO_STREAM (type=2) frames, resample 48kHz→native rate,
// and forward to the TX audio pipeline.

void TciServer::onBinaryMessageReceived(const QByteArray& data)
{
    auto* ws = qobject_cast<QWebSocket*>(sender());
    if (!ws) { return; }
    (void)data;
    // TODO Phase 17: dispatch to handleBinaryFrame for TX audio + IQ inbound.
}

} // namespace NereusSDR

#endif // HAVE_WEBSOCKETS
