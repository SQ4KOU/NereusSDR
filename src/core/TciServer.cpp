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
#include "TciProtocol.h"
#include "TciSendQueue.h"
#include "LogCategories.h"
#include "models/RadioModel.h"

// Phase 16 Task 16.3 (sub-commit b): WDSP RESAMPLEF lifecycle.
// resample.h declares create_resampleF / destroy_resampleF / xresampleF, and
// the RESAMPLEF typedef.  The void*-opaque FV wrappers (create_resampleFV /
// xresampleFV / destroy_resampleFV) live in resample.c:342-360 [WDSP TAPR v1.29]
// but are NOT declared in resample.h — they are forward-declared here.
//
// create_resampleFV(in_rate, out_rate) calls create_resampleF(1, 0, 0, 0, in_rate,
// out_rate), so size=0 + null buffers are safe at construction time; xresampleFV
// sets in/out/size per-call.  Verified by reading resample.c:342-360.
extern "C" {
#include "resample.h"
// FV wrappers are not declared in resample.h — forward-declare them:
void* create_resampleFV(int in_rate, int out_rate);
void  xresampleFV(float* input, float* output, int numsamps, int* outsamps, void* ptr);
void  destroy_resampleFV(void* ptr);
}

#include <QHostAddress>
#include <QTimer>
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
    // From design doc §1 — TciServer owns one TciProtocol; it is the shared
    // dispatch engine across all clients (single-instance, transport-blind).
    , m_protocol(std::make_unique<TciProtocol>(model, this))
{
    m_pingTimer = new QTimer(this);  // parented — destroyed with server

    // From Thetis TCIServer.cs:6001-6003 [v2.10.3.13] — PingFrameTimer callback
    // fires sendPingFrame("Thetis") for each connected client.
    // Per Thetis TCIServer.cs:2650-2654 inline comment: ping frames are every 20s
    // per RFC 6455; we don't expect a Pong back within any timeout — we use the
    // ping itself to surface a dead socket via Qt's automatic write-error path.
    connect(m_pingTimer, &QTimer::timeout, this, [this]() {
        for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
            // From Thetis TCIServer.cs:2784 [v2.10.3.13] — sendPingFrame enqueue.
            // Qt6's QWebSocket::ping handles RFC 6455 frame construction and
            // socket-state-based error suppression internally.
            it.key()->ping(QByteArrayLiteral("Thetis"));
        }
    });

    // Phase 14: shared outbound drain timer.
    // Each tick drains each client's TciSendQueue in priority order, capped
    // at kDrainMaxPerTick frames per client to avoid starving the event loop
    // when one client is flooded.
    //
    // Interval 5ms: Thetis uses a sender thread blocked on
    // m_outboundFrameEvent.WaitOne(20) (TCIServer.cs:1770 [v2.10.3.13]),
    // which wakes immediately on enqueue or after 20ms.  A 5ms timer drains
    // promptly without the per-thread overhead and stays well within TCI's
    // 20ms latency budget.
    m_drainTimer = new QTimer(this);
    m_drainTimer->setInterval(5);   // 5ms drain tick; see rationale above
    connect(m_drainTimer, &QTimer::timeout, this, [this]() {
        // Phase 15: collapse coalesced VFO updates into pending notifications
        // BEFORE per-client drain so the just-drained frames participate in
        // this tick. From Thetis TCIServer.cs:1722-1727 [v2.10.3.13].
        m_protocol->drainCoalescedNotifications();

        // Broadcast any drained notifications to all clients.
        // Without this, drainCoalescedNotifications() populates
        // m_pendingNotifications but nothing pumps it to the send queues.
        while (m_protocol->hasPendingNotification()) {
            const QString notif = m_protocol->takePendingNotification();
            for (auto sit = m_clients.cbegin(); sit != m_clients.cend(); ++sit) {
                sit.value()->sendQueue.push(TciSendQueue::Priority::Control, notif);
            }
        }

        // Phase 14 per-client send-queue drain (unchanged):
        constexpr int kDrainMaxPerTick = 64;
        for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
            QWebSocket* ws    = it.key();
            auto&       session = it.value();
            QString frame;
            int drained = 0;
            while (drained < kDrainMaxPerTick && session->sendQueue.tryPop(&frame)) {
                ws->sendTextMessage(frame);
                ++drained;
            }
            // Sync the legacy framesDropped field for Phase 22 ClientChainApplet.
            session->framesDropped = session->sendQueue.dropCount();
        }
    });
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

    // From Thetis TCIServer.cs:2650-2654 [v2.10.3.13] — 20s server-driven ping
    // with payload "Thetis", per RFC 6455 keepalive semantics.
    // Thetis: "per websock spec ping frames are every 20 seconds. Ideally we
    // should receive something back within 20 seconds, but just use it to cause
    // exception on socket if client has dc'ed without telling us with a
    // disconnect frame."
    // Detects dead clients via Qt's automatic close-on-write-error path.
    m_pingTimer->start(m_pingIntervalMs);

    // Phase 14: start the outbound drain timer (stops again in stop()).
    m_drainTimer->start();

    return true;
}

// ── stop() ───────────────────────────────────────────────────────────────────
//
// From AetherSDR src/core/TciServer.cpp:184-207 [@0cd4559] — disconnect-and-
// cleanup loop pattern.  NereusSDR uses QHash iteration instead of QList.

void TciServer::stop()
{
    if (!m_server) { return; }

    m_pingTimer->stop();
    m_drainTimer->stop();  // Phase 14: stop drain before disconnecting clients

    // Disconnect all connected clients.  We disconnect the socket's signals
    // from this object first to prevent onClientDisconnected() re-entry during
    // the explicit close() calls.
    //
    // Phase 16 Task 16.3 (sub-commit b): destroy all RESAMPLEF instances for
    // each session before clearing the client table. cleanupResamplers is called
    // here so resamplers are destroyed even if QWebSocket::disconnected never
    // fires (e.g. on forceful server shutdown).
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        cleanupResamplers(it.value());
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

    // Phase 16 Task 16.3 (sub-commit b): destroy all RESAMPLEF instances for
    // this client before removing the session from the map.
    cleanupResamplers(it.value());

    m_clients.erase(it);
    ws->deleteLater();
}

// ── totalResamplerInstances() ─────────────────────────────────────────────────
//
// Phase 16 Task 16.3 (sub-commit b): sums audioResamplers.size() across all
// connected sessions. Exposed for lifecycle test assertions and future
// diagnostic tooling (Phase 22 ClientChainApplet).
int TciServer::totalResamplerInstances() const
{
    int total = 0;
    for (auto it = m_clients.cbegin(); it != m_clients.cend(); ++it) {
        total += it.value()->audioResamplers.size();
    }
    return total;
}

// ── handleAudioSubscribe() ────────────────────────────────────────────────────
//
// Phase 16 Task 16.3 (sub-commit b): creates a RESAMPLEF instance for the
// given (session, rx) pair if one doesn't already exist.  Idempotent.
//
// From Thetis TCIServer.cs — audio_start handler stores the rx in
// m_audioStreamEnabled (a HashSet<int>) and instantiates a Resampler from
// its m_rxAudioResamplers Dictionary [v2.10.3.13]. NereusSDR maps this to
// create_resampleFV (the void*-opaque exported wrapper in resample.c:342-344
// [WDSP TAPR v1.29]) which calls create_resampleF(run=1, size=0, in=0, out=0,
// in_rate, out_rate).  The size=0/null buffers are fine because xresampleFV
// sets in/out/size per-call — verified by reading resample.c:342-360.
void TciServer::handleAudioSubscribe(std::shared_ptr<TciClientSession>& session, int rx)
{
    if (session->audioStreamEnabled.contains(rx)) {
        return;  // idempotent — Thetis HashSet.Add returns false on duplicate
    }
    session->audioStreamEnabled.insert(rx);

    if (!session->audioResamplers.contains(rx)) {
        const int inRate  = 48000;                        // WDSP RX output is always 48 kHz
        const int outRate = session->audioSampleRate;     // negotiated client rate (default 48000)
        // create_resampleFV(in_rate, out_rate) — from resample.c:342-344 [WDSP v1.29]:
        //   return (void *)create_resampleF(1, 0, 0, 0, in_rate, out_rate);
        // size=0 + null buffers are intentional; xresampleFV sets them per-call.
        void* resampler = create_resampleFV(inRate, outRate);
        if (resampler) {
            session->audioResamplers.insert(rx, resampler);
            qCInfo(lcTci) << "TciServer: audio resampler created for rx" << rx
                          << "peer" << session->peer
                          << "in_rate" << inRate << "out_rate" << outRate;
        } else {
            qCWarning(lcTci) << "TciServer: create_resampleFV failed for rx" << rx
                             << "in_rate" << inRate << "out_rate" << outRate;
        }
    }
}

// ── handleAudioUnsubscribe() ──────────────────────────────────────────────────
//
// Phase 16 Task 16.3 (sub-commit b): destroys the RESAMPLEF for the given
// (session, rx) pair and removes it from the subscription set.  Idempotent.
//
// From Thetis TCIServer.cs — audio_stop handler removes the rx from
// m_audioStreamEnabled and disposes the corresponding Resampler [v2.10.3.13].
void TciServer::handleAudioUnsubscribe(std::shared_ptr<TciClientSession>& session, int rx)
{
    if (!session->audioStreamEnabled.contains(rx)) {
        return;  // idempotent
    }
    session->audioStreamEnabled.remove(rx);

    auto rIt = session->audioResamplers.find(rx);
    if (rIt != session->audioResamplers.end()) {
        // destroy_resampleFV — from resample.c:358-360 [WDSP v1.29]:
        //   destroy_resampleF((RESAMPLEF)ptr);
        destroy_resampleFV(rIt.value());
        session->audioResamplers.erase(rIt);
        qCInfo(lcTci) << "TciServer: audio resampler destroyed for rx" << rx
                      << "peer" << session->peer;
    }
}

// ── cleanupResamplers() ───────────────────────────────────────────────────────
//
// Phase 16 Task 16.3 (sub-commit b): destroys all RESAMPLEF instances for the
// given session. Called from onClientDisconnected and stop() to prevent leaks.
void TciServer::cleanupResamplers(std::shared_ptr<TciClientSession>& session)
{
    for (auto rIt = session->audioResamplers.begin();
         rIt != session->audioResamplers.end(); ++rIt) {
        destroy_resampleFV(rIt.value());
    }
    session->audioResamplers.clear();
    session->audioStreamEnabled.clear();
}

// ── onTextMessageReceived() ──────────────────────────────────────────────────

void TciServer::onTextMessageReceived(const QString& msg)
{
    auto* ws = qobject_cast<QWebSocket*>(sender());
    if (!ws) { return; }
    auto it = m_clients.find(ws);
    if (it == m_clients.end()) { return; }

    auto& session = it.value();
    session->lastCommand   = msg;
    session->lastCommandAt = QDateTime::currentMSecsSinceEpoch();

    // Phase 16 Task 16.3 (sub-commit b): intercept audio_start/audio_stop for
    // per-client subscription state and WDSP resampler lifecycle. This runs
    // BEFORE TciProtocol dispatch because TciProtocol is transport-blind and
    // has no concept of per-client sessions.
    //
    // From Thetis TCIServer.cs:4406-4440 [v2.10.3.13] — audio_start / audio_stop
    // parse the rx index and update m_audioStreamEnabled per-listener.
    // NereusSDR mirrors: parse rx from stripped command, delegate to
    // handleAudioSubscribe / handleAudioUnsubscribe which manage the QHash.
    {
        QString trimmed = msg.trimmed();
        if (trimmed.endsWith(QLatin1Char(';'))) {
            trimmed.chop(1);
        }
        const QString kAudioStart = QStringLiteral("audio_start:");
        const QString kAudioStop  = QStringLiteral("audio_stop:");
        if (trimmed.startsWith(kAudioStart)) {
            bool ok = false;
            const int rx = trimmed.mid(kAudioStart.size()).trimmed().toInt(&ok);
            if (ok && rx >= 0 && rx <= 1) {
                handleAudioSubscribe(session, rx);
            }
        } else if (trimmed.startsWith(kAudioStop)) {
            bool ok = false;
            const int rx = trimmed.mid(kAudioStop.size()).trimmed().toInt(&ok);
            if (ok && rx >= 0 && rx <= 1) {
                handleAudioUnsubscribe(session, rx);
            }
        }
    }

    // From design doc §1 + Sweep B silent-error invariant:
    // handleCommand returns the synchronous response (empty for unknown
    // commands per Sweep B; non-empty for queries that have a reply).
    // Response goes only to the originating client (unicast).
    //
    // Phase 14: push into the per-client TciSendQueue instead of calling
    // sendTextMessage directly. The drain timer pumps frames from the queue
    // in priority order. Coalescing (Thetis m_outboundCoalescedFrames at
    // TCIServer.cs:769-774 [v2.10.3.13]) lands in Phase 15.
    const QString response = m_protocol->handleCommand(msg);
    if (!response.isEmpty()) {
        session->sendQueue.push(TciSendQueue::Priority::Control, response);
    }

    // From design doc §1: notifications drain after each handleCommand and
    // broadcast to ALL clients (including the originator), mirroring Thetis's
    // outbound-frame fan-out at TCIServer.cs:1662-1791 [v2.10.3.13].
    // Phase 14: push into each client's queue instead of direct sendTextMessage.
    while (m_protocol->hasPendingNotification()) {
        const QString notif = m_protocol->takePendingNotification();
        for (auto sit = m_clients.cbegin(); sit != m_clients.cend(); ++sit) {
            sit.value()->sendQueue.push(TciSendQueue::Priority::Control, notif);
        }
    }
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

// ── setPingIntervalMs() ──────────────────────────────────────────────────────
//
// From Thetis TCIServer.cs:2650 [v2.10.3.13] — Thetis hardcodes 20000ms
// (1000 * 20); we expose a setter for testability.
// If the ping timer is already running, apply the new interval immediately
// so that test-driven calls to setPingIntervalMs(200) take effect without
// requiring a stop/start cycle.

void TciServer::setPingIntervalMs(int ms)
{
    m_pingIntervalMs = ms;
    if (m_pingTimer->isActive()) {
        m_pingTimer->setInterval(ms);
    }
}

} // namespace NereusSDR

#endif // HAVE_WEBSOCKETS
