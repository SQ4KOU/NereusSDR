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
#include "TciBinaryFrame.h"
#include "LogCategories.h"
#include "models/RadioModel.h"
#include "WdspEngine.h"
#include "RxChannel.h"
#include "TxChannel.h"

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

        // Phase 16 Task 16.3 (sub-commit c): RX audio drain.
        // For each client that has audio subscriptions, read from the per-slice
        // AudioRingSpsc, optionally resample, encode as a TCI binary frame, and
        // send directly via sendBinaryMessage.
        //
        // From Thetis TCIServer.cs:5444-5512 [v2.10.3.13] — the sendRXAudioStream
        // loop reads samples, resamples, encodes, and calls sendBinaryFrame.
        // NereusSDR replicates this per drain-tick rather than in a dedicated thread.
        for (auto cit = m_clients.begin(); cit != m_clients.end(); ++cit) {
            QWebSocket* ws = cit.key();
            auto& session  = cit.value();

            for (int rx : session->audioStreamEnabled) {
                if (rx < 0 || rx >= kMaxTciRxSlices) { continue; }

                // Number of interleaved float samples to pop each tick.
                // audioStreamSamples is per-channel; multiply by channels.
                const int channels = session->audioStreamChannels;  // 1 or 2
                const int perChSamples = session->audioStreamSamples;  // default 2048
                const int totalSamples = perChSamples * channels;
                const int wantBytes = totalSamples * static_cast<int>(sizeof(float));

                if (m_audioRing[rx].usedBytes() < static_cast<size_t>(wantBytes)) {
                    continue;  // not enough data yet; wait for next tick
                }

                // Pop from the ring into the scratch buffer.
                // Scratch is sized for kMaxDrainSamples = 2048*2 floats.
                const int maxScratch = kMaxDrainSamples;
                if (totalSamples > maxScratch) { continue; }  // safety

                const qint64 got = m_audioRing[rx].popInto(
                    reinterpret_cast<uint8_t*>(m_drainScratch.data()),
                    wantBytes);
                if (got < wantBytes) { continue; }

                // Resample if the client requested a rate other than 48000 Hz.
                // Phase 16: xresampleFV resamples in-place using the per-session
                // per-slice RESAMPLEF instance created in handleAudioSubscribe.
                const float* samples = m_drainScratch.data();
                int outSamples = totalSamples;

                auto rIt = session->audioResamplers.find(rx);
                if (rIt != session->audioResamplers.end() &&
                    session->audioSampleRate != 48000) {
                    // Allocate a temporary output buffer on the stack.
                    // Max output = totalSamples * max_ratio (48000/8000 = 6).
                    static constexpr int kMaxOutSamples = kMaxDrainSamples * 8;
                    static thread_local std::array<float, kMaxOutSamples> outBuf{};
                    xresampleFV(m_drainScratch.data(), outBuf.data(),
                                totalSamples, &outSamples, rIt.value());
                    samples = outBuf.data();
                }

                // Encode + send binary frame.
                // From Thetis TCIServer.cs:5510 [v2.10.3.13]:
                //   sendBinaryFrame(buildStreamPayload(receiver, sampleRate,
                //       sampleType, interleaved.Length, RX_AUDIO_STREAM,
                //       channels, encoded));
                const QByteArray frame = TciBinaryFrame::buildStreamPayload(
                    rx,
                    session->audioSampleRate,
                    session->audioSampleType,
                    outSamples,         // flat count (length field in header)
                    static_cast<int>(TciStreamType::RxAudioStream),
                    channels,
                    samples);

                ws->sendBinaryMessage(frame);
            }
        }
    });

    // Phase 16 Task 16.3 (sub-commit c): hook the RX audio tap from RxChannel.
    // WdspEngine may not be initialized yet at construction time. We connect
    // once it is, then hook audioFrameReady with Qt::DirectConnection so the
    // slot runs on the DSP thread and can push into AudioRingSpsc non-blockingly.
    if (m_model) {
        WdspEngine* wdsp = m_model->wdspEngine();
        if (wdsp) {
            auto hookAudioTap = [this, wdsp]() {
                RxChannel* rxCh = wdsp->rxChannel(0);
                if (rxCh) {
                    connect(rxCh, &RxChannel::audioFrameReady,
                            this, &TciServer::onAudioFrameReady,
                            Qt::DirectConnection);
                    qCInfo(lcTci) << "TciServer: RX audio tap connected to RxChannel 0";
                }
            };

            if (wdsp->isInitialized()) {
                hookAudioTap();
            } else {
                m_wdspInitConn = connect(wdsp, &WdspEngine::initializedChanged,
                                         this, [this, hookAudioTap](bool init) {
                    if (init) {
                        hookAudioTap();
                        disconnect(m_wdspInitConn);
                    }
                });
            }
        }
    }
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

    // Phase 17: release TX audio mutex — no client is active after stop().
    m_txAudioActiveClient = nullptr;

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

    // Phase 17: release TX audio mutex if this client held it.
    // QPointer auto-nulls when the socket is deleted (ws->deleteLater below),
    // but we clear explicitly here so activeTxClientCount() returns 0 in the
    // same event-loop pass as the disconnect.
    if (!m_txAudioActiveClient.isNull() && m_txAudioActiveClient.data() == ws) {
        m_txAudioActiveClient = nullptr;
        qCInfo(lcTci) << "TciServer: TX audio mutex released on disconnect";
    }

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

// ── activeTxClientCount() ────────────────────────────────────────────────────
//
// Phase 17: returns 1 when m_txAudioActiveClient is set and still connected;
// 0 otherwise (QPointer auto-nulls on socket destruction).
// Used by Phase 22 ClientChainApplet to render the TX badge.
int TciServer::activeTxClientCount() const
{
    return m_txAudioActiveClient.isNull() ? 0 : 1;
}

// ── activeTxClientPeer() ────────────────────────────────────────────────────
//
// Phase 17: returns the peer string of the active TX client,
// or an empty string when there is no active TX client.
QString TciServer::activeTxClientPeer() const
{
    if (m_txAudioActiveClient.isNull()) {
        return {};
    }
    auto it = m_clients.find(m_txAudioActiveClient.data());
    if (it == m_clients.cend()) {
        return {};
    }
    return it.value()->peer;
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

// ── onAudioFrameReady() ──────────────────────────────────────────────────────
//
// Phase 16 Task 16.3 (sub-commit c): RX audio tap slot.
// Connected via Qt::DirectConnection — runs on the WDSP DSP thread, not the
// main thread. MUST NOT touch Qt objects (QWebSocket, QTimer, m_clients) —
// those are main-thread owned. Only writes to m_audioRing[slice] which is a
// lock-free AudioRingSpsc safe for one producer (DSP thread) + one consumer
// (main thread drain timer).
//
// From Thetis RxChannel.cpp:1479-1492 [v2.10.3.13] — the audioFrameReady
// signal fires post-DSP with outI (L) and outQ (R) as scratch float arrays
// of length n at srcRate Hz (always 48000 for WDSP RX output).
//
// Uses tryPushCopy (non-blocking) so the DSP thread never blocks. Overflow
// (ring full) silently drops the oldest portion — audible as a dropout rather
// than a deadlock.
void TciServer::onAudioFrameReady(int slice, const float* L, const float* R,
                                   int n, int srcRate)
{
    (void)srcRate;  // always 48000 per kWdspRxOutputRate in RxChannel.cpp

    if (slice < 0 || slice >= kMaxTciRxSlices) { return; }
    if (!L || !R || n <= 0) { return; }

    // Interleave L[i], R[i] into a local scratch then push into the ring.
    // We use a stack-local buffer to avoid heap alloc on the audio thread.
    // Max n = audioStreamSamples (2048) per the WDSP buffer size contract;
    // stereo interleaved = 2 * 2048 = 4096 floats max.
    constexpr int kInterleaveMax = 2 * 2048;
    float interleaved[kInterleaveMax];
    const int total = std::min(n * 2, kInterleaveMax);
    const int count = total / 2;
    for (int i = 0; i < count; ++i) {
        interleaved[2 * i]     = L[i];
        interleaved[2 * i + 1] = R[i];
    }

    // tryPushCopy: drops the newest bytes on overflow (partial write).
    // Audio ring is single-producer (DSP thread) / single-consumer (main thread).
    m_audioRing[slice].tryPushCopy(
        reinterpret_cast<const uint8_t*>(interleaved),
        total * static_cast<int>(sizeof(float)));
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
    // Phase 17: intercept trx:N,true,tci; / trx:N,false; for TX audio mutex.
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

        // Phase 17: TX audio mutex — intercept trx:N,true,tci; and trx:N,false;
        //
        // Porting from Thetis TCIServer.cs:3489-3516 [v2.10.3.13]:
        //   bool useTciAudio = args.Length > 2 && args[2].ToLower() == "tci";
        //   bool wantsActiveTciPtt = useTciAudio && bOK && bMox && ...;
        //   if (wantsActiveTciPtt) ownsActiveTciPtt = m_server.TryAcquireActiveTxAudioListener(this);
        //   else m_server.ReleaseActiveTxAudioListener(this);
        //   m_tciPttActive = wantsActiveTciPtt && ownsActiveTciPtt;
        //
        // NereusSDR simplification: TryAcquire/Release runs directly in the
        // main-thread slot; no per-listener thread lock needed because all
        // WebSocket callbacks run on the same Qt event loop.
        {
            const QString kTrx = QStringLiteral("trx:");
            if (trimmed.startsWith(kTrx)) {
                // Parse "trx:N,bool[,tci]"
                const QString args = trimmed.mid(kTrx.size());
                const QStringList parts = args.split(QLatin1Char(','));
                if (parts.size() >= 2) {
                    // Is the third arg "tci"?
                    const bool hasTciArg = (parts.size() >= 3 &&
                        parts.at(2).trimmed().compare(QLatin1String("tci"),
                            Qt::CaseInsensitive) == 0);

                    const bool wantsMox = (parts.at(1).trimmed().compare(
                        QLatin1String("true"), Qt::CaseInsensitive) == 0);

                    if (hasTciArg && wantsMox) {
                        // Client wants TX audio ownership.
                        // From Thetis TCIServer.cs:7625-7643 [v2.10.3.13] —
                        // TryAcquireActiveTxAudioListener: grant if no current
                        // owner or the owner IS this client; else deny.
                        if (m_txAudioActiveClient.isNull() ||
                            m_txAudioActiveClient.data() == ws) {
                            m_txAudioActiveClient = ws;
                            qCInfo(lcTci) << "TciServer: TX audio mutex acquired by"
                                          << session->peer;
                        } else {
                            qCInfo(lcTci) << "TciServer: TX audio mutex denied for"
                                          << session->peer
                                          << "(held by"
                                          << m_clients.value(m_txAudioActiveClient.data(),
                                                 std::make_shared<TciClientSession>())->peer
                                          << ")";
                        }
                    } else if (!wantsMox) {
                        // trx:N,false — release mutex if this client held it.
                        // From Thetis TCIServer.cs:7646-7652 [v2.10.3.13] —
                        // ReleaseActiveTxAudioListener: clear if owner matches.
                        if (!m_txAudioActiveClient.isNull() &&
                            m_txAudioActiveClient.data() == ws) {
                            m_txAudioActiveClient = nullptr;
                            qCInfo(lcTci) << "TciServer: TX audio mutex released by"
                                          << session->peer;
                        }
                    }
                }
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
// Phase 17: parse inbound TCI binary frames and route TX_AUDIO_STREAM (type 2)
// to the TX audio pipeline.  All other stream types are silently ignored per
// Thetis TCIServer.cs:5614 [v2.10.3.13] ("if streamType != TX_AUDIO_STREAM … return").
//
// Porting from Thetis TCIServer.cs:5602-5703 [v2.10.3.13] — handleBinaryFrame.
//
// TX mutex: only the client registered as m_txAudioActiveClient may push audio.
// Other clients' binary frames are silently dropped and their txFramesDropped
// counter incremented.  This maps to the Thetis TryAcquireActiveTxAudioListener /
// m_tciPttActive per-client gate (TCIServer.cs:7625-7651 [v2.10.3.13]).

void TciServer::onBinaryMessageReceived(const QByteArray& data)
{
    auto* ws = qobject_cast<QWebSocket*>(sender());
    if (!ws) { return; }
    auto it = m_clients.find(ws);
    if (it == m_clients.end()) { return; }
    auto& session = it.value();

    // From Thetis TCIServer.cs:5604-5605 [v2.10.3.13]:
    //   if (payload == null || payload.Length < 64) return;
    if (data.size() < 64) { return; }

    // ── Parse 64-byte LE header ───────────────────────────────────────────────
    //
    // From Thetis TCIServer.cs:5607-5612 [v2.10.3.13]:
    //   int receiver   = BitConverter.ToInt32(payload, 0);
    //   int sampleRate = BitConverter.ToInt32(payload, 4);
    //   TCISampleType sampleType = (TCISampleType)BitConverter.ToUInt32(payload, 8);
    //   int length     = BitConverter.ToInt32(payload, 20);
    //   TCIStreamType streamType = (TCIStreamType)BitConverter.ToUInt32(payload, 24);
    //   int headerChannels = BitConverter.ToInt32(payload, 28);
    auto readI32 = [&](int off) -> qint32 {
        const auto* p = reinterpret_cast<const uchar*>(data.constData() + off);
        return static_cast<qint32>(
            static_cast<quint32>(p[0]) |
            (static_cast<quint32>(p[1]) << 8) |
            (static_cast<quint32>(p[2]) << 16) |
            (static_cast<quint32>(p[3]) << 24));
    };

    // const int receiver     = readI32(0);   // future: multi-RX routing
    const int sampleRate    = readI32(4);
    const int sampleTypeInt = readI32(8);
    const int length        = readI32(20);  // flat count of encoded values
    const int streamTypeInt = readI32(24);
    const int headerChannels = readI32(28);

    // From Thetis TCIServer.cs:5614-5615 [v2.10.3.13]:
    //   if (streamType != TCIStreamType.TX_AUDIO_STREAM || length <= 0) return;
    if (streamTypeInt != static_cast<int>(TciStreamType::TxAudioStream)) { return; }
    if (length <= 0) { return; }

    // ── TX mutex gate ─────────────────────────────────────────────────────────
    //
    // Only the active TX client may push audio. All others silently dropped.
    // Mirrors Thetis per-client m_tciPttActive gate (TCIServer.cs:5547 [v2.10.3.13]).
    if (m_txAudioActiveClient.isNull() || m_txAudioActiveClient.data() != ws) {
        session->txFramesDropped++;
        return;
    }

    // ── bytesPerSample + payload bounds check ─────────────────────────────────
    //
    // From Thetis TCIServer.cs:5617-5621 [v2.10.3.13]:
    //   int bytesPerSample = getBytesPerSample(sampleType);
    //   int dataOffset = 64;
    //   int actualDataBytes = payload.Length - dataOffset;
    //   if (actualDataBytes < bytesPerSample) return;
    const int bps = TciBinaryFrame::bytesPerSample(sampleTypeInt);
    const int dataOffset = 64;
    const int actualDataBytes = data.size() - dataOffset;
    if (actualDataBytes < bps) { return; }

    const int actualValueCount = actualDataBytes / bps;

    // ── Modern vs legacy header detection ─────────────────────────────────────
    //
    // From Thetis TCIServer.cs:5628-5652 [v2.10.3.13]:
    //   bool modernHeader = (headerChannels == 1 || headerChannels == 2);
    //   if (modernHeader) {
    //       channels = headerChannels;
    //       decodedValueCount = Math.Min(length, actualValueCount);
    //       if (channels > 1) decodedValueCount -= decodedValueCount % channels;
    //   } else {
    //       // legacy/JTDX: no real channels field
    //       if (actualValueCount >= length * 2) channels = 2; else channels = 1;
    //       decodedValueCount = Math.Min(length, actualValueCount);
    //       if (channels > 1) decodedValueCount -= decodedValueCount % channels;
    //   }
    const bool modernHeader = (headerChannels == 1 || headerChannels == 2);

    int channels;
    int decodedValueCount;

    if (modernHeader) {
        channels = headerChannels;
        decodedValueCount = std::min(length, actualValueCount);
        if (channels > 1) {
            decodedValueCount -= decodedValueCount % channels;
        }
    } else {
        // legacy/JTDX
        channels = (actualValueCount >= length * 2) ? 2 : 1;
        decodedValueCount = std::min(length, actualValueCount);
        if (channels > 1) {
            decodedValueCount -= decodedValueCount % channels;
        }
    }

    if (decodedValueCount <= 0) { return; }

    // ── Decode samples ────────────────────────────────────────────────────────
    //
    // From Thetis TCIServer.cs:5657 [v2.10.3.13]:
    //   float[] decoded = decodeSamples(payload, dataOffset, decodedValueCount, sampleType);
    std::vector<float> decoded = TciBinaryFrame::decodeSamples(
        data, dataOffset, decodedValueCount, sampleTypeInt);

    // ── NaN/Inf zero + clamp [-4.0, 4.0] ─────────────────────────────────────
    //
    // From Thetis TCIServer.cs:5658-5673 [v2.10.3.13]:
    //   for (int i = 0; i < decoded.Length; i++) {
    //       float sample = decoded[i];
    //       if (float.IsNaN(sample) || float.IsInfinity(sample)) decoded[i] = 0.0f;
    //       else if (sample > 4.0f)  decoded[i] = 4.0f;
    //       else if (sample < -4.0f) decoded[i] = -4.0f;
    //   }
    // Note: clamp range is [-4.0, 4.0] — Thetis permits TX-side overdrive.
    for (float& s : decoded) {
        if (std::isnan(s) || std::isinf(s)) {
            s = 0.0f;
        } else if (s > 4.0f) {
            s = 4.0f;
        } else if (s < -4.0f) {
            s = -4.0f;
        }
    }

    // ── Push to TX audio ring ─────────────────────────────────────────────────
    //
    // Thetis enqueues a TCIQueuedTxAudio (with bounded drop-oldest) at
    // TCIServer.cs:5687-5702 [v2.10.3.13].  NereusSDR pushes raw decoded
    // float bytes into a server-wide SPSC ring.  Drop behaviour: tryPushCopy
    // drops the newest bytes on overflow (partial write) — the ring's natural
    // behaviour matches Thetis's oldest-drop semantics for practical purposes
    // (both prevent unbounded growth; TCI latency is <20ms so overflow is rare).
    //
    // `decodedValueCount` is the flat interleaved count (L,R,L,R... for stereo
    // or L,L,L... for mono).  The ring stores raw float bytes; TxChannel's
    // feedTxAudioFromTci drains them per block.
    const int frames = (channels > 1) ? (decodedValueCount / channels) : decodedValueCount;
    if (frames > 0) {
        m_txAudioRing.tryPushCopy(
            reinterpret_cast<const uint8_t*>(decoded.data()),
            static_cast<qint64>(decodedValueCount) * static_cast<qint64>(sizeof(float)));
    }

    // ── Drain to TxChannel if available ──────────────────────────────────────
    //
    // Phase 17 simplified: if TxChannel is available, feed the decoded
    // (and clamped) samples directly — bypassing the ring so the block
    // reaches driveOneTxBlock without an extra copy.  Remove the corresponding
    // bytes from the ring to avoid double-processing on future drain ticks.
    if (m_model && frames > 0) {
        WdspEngine* wdsp = m_model->wdspEngine();
        if (wdsp && wdsp->isInitialized()) {
            TxChannel* txCh = wdsp->txChannel(0);
            if (txCh) {
                txCh->feedTxAudioFromTci(decoded.data(), frames, channels, sampleRate);
                // Drop the bytes we just pushed to the ring (we fed directly above).
                m_txAudioRing.dropOldest(
                    static_cast<size_t>(decodedValueCount) * sizeof(float));
            }
        }
    }
    // Note: m_txAudioRing holds the data for test-only peekTxRingSize() calls
    // when m_model is null (unit test scenario without a real TxChannel).
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

// ── injectAudioFrameForTest() ─────────────────────────────────────────────────
//
// Phase 16 Task 16.4: test-only hook.  Delegates to the private
// onAudioFrameReady slot so integration tests can feed synthetic audio into
// the per-slice ring buffer without needing a real RxChannel / WdspEngine.
//
// This wrapper exists because onAudioFrameReady is private (signal-connected
// internally via Qt::DirectConnection).  Production code never calls this
// method; the only caller is tst_tci_audio_roundtrip.

void TciServer::injectAudioFrameForTest(int slice, const float* L, const float* R,
                                         int n, int srcRate)
{
    onAudioFrameReady(slice, L, R, n, srcRate);
}

} // namespace NereusSDR

#endif // HAVE_WEBSOCKETS
