// =================================================================
// src/core/TgxlConnection.cpp  (NereusSDR)
// =================================================================
// Source attribution (AetherSDR, GPLv3):
//   Copyright (C) 2024-2026  Jeremy (KK7GWY) / AetherSDR contributors
//       per https://github.com/ten9876/AetherSDR (GPLv3)
//   This file is a port or structural derivative of AetherSDR source.
//   AetherSDR is licensed under the GNU General Public License v3.
//   NereusSDR is also GPLv3. Attribution follows GPLv3 section 5 requirements.
// =================================================================
// Modification history (NereusSDR):
//   2026-05-18  Ported in C++20/Qt6 for NereusSDR by J.J. Boyd (KG4VCF),
//                 with AI-assisted transformation via Anthropic Claude Code.
//                 Layout from AetherSDR src/core/TgxlConnection.{h,cpp} [@0cd4559].
//   2026-05-19  Tier 2 additions (keepalive/ping/setup r/w/ifconf r/w/save +
//                 auto-reconnect). NereusSDR-native; design §4.2.1 + §6.4.
//   2026-09-21  J.J. Boyd (KG4VCF), with AI-assisted implementation via
//                 OpenAI Codex: adopted AetherSDR's owned reconnect-timer
//                 lifecycle [@1e0718ad]. Endpoint/attempt generations,
//                 exponential backoff, and explicit source-bind fallback
//                 are NereusSDR-native.
// =================================================================
#include "TgxlConnection.h"
#include "AppSettings.h"
#include "RouteProbe.h"
#include <QDateTime>
#include <QLoggingCategory>
#include <QPointer>

namespace NereusSDR {

Q_LOGGING_CATEGORY(lcTgxl, "nereus.tgxl")

// Exponential backoff schedule for auto-reconnect, in seconds.
// Parallel to PgxlConnection (design §6.4); cap at 60 s.
static constexpr int kTgxlBackoffSec[] = {1, 2, 5, 10, 30, 60};

// From AetherSDR src/core/TgxlConnection.cpp:6 [@0cd4559]
TgxlConnection::TgxlConnection(QObject* parent)
    : QObject(parent)
{
    m_pollTimer.setInterval(1000);  // 1 Hz per AetherSDR [@0cd4559]
    connect(&m_pollTimer, &QTimer::timeout, this, &TgxlConnection::pollStatus);

    // Keepalive timer: issues a status poke at the configured interval.
    // Disabled until enableKeepalive() is called.
    m_keepaliveTimer.setSingleShot(false);
    connect(&m_keepaliveTimer, &QTimer::timeout, this, &TgxlConnection::onKeepaliveTimeout);

    // Ping timeout checker: every 5 s, evict pings older than 5 s.
    m_pingTimeoutTimer.setInterval(5000);
    m_pingTimeoutTimer.setSingleShot(false);
    connect(&m_pingTimeoutTimer, &QTimer::timeout, this, &TgxlConnection::onPingTimeoutCheck);
    m_pingTimeoutTimer.start();

    // AetherSDR's owned single-shot timer is the structural precedent.
    // Nereus adds endpoint generations because a pending retry can outlive
    // a settings-driven endpoint replacement.
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout,
            this, &TgxlConnection::onReconnectTimeout);
    m_connectTimer.setSingleShot(true);
    connect(&m_connectTimer, &QTimer::timeout,
            this, &TgxlConnection::onConnectTimeout);
}

TgxlConnection::~TgxlConnection()
{
    // QAbstractSocket::~QAbstractSocket may emit disconnected().  Quiesce
    // the socket while all TgxlConnection members are still alive so that
    // destruction cannot re-enter the reconnect machinery.
    m_userInitiatedDisconnect = true;
    ++m_endpointGeneration;
    m_reconnectTimer.stop();
    m_connectTimer.stop();
    m_pollTimer.stop();
    m_keepaliveTimer.stop();
    m_pingTimeoutTimer.stop();
    retireSocketAttempt();
    m_socket.abort();
}

// From AetherSDR src/core/TgxlConnection.cpp:18 [@0cd4559]
void TgxlConnection::connectToTgxl(const QString& host, quint16 port)
{
    const bool sameEndpoint = (host == m_lastHost && port == m_lastPort);
    // Preserve duplicate suppression for the same in-flight/established
    // endpoint. A different endpoint is an intentional replacement and must
    // invalidate both an active socket and any captured retry.
    if (sameEndpoint
        && (m_socket.state() != QAbstractSocket::UnconnectedState
            || m_connectTimer.isActive())) {
        qCDebug(lcTgxl) << "connectToTgxl: socket already in state"
                        << m_socket.state() << "- ignoring duplicate";
        return;
    }

    const bool wasConnected = m_connected;
    ++m_endpointGeneration;
    m_reconnectTimer.stop();
    m_connectTimer.stop();
    retireSocketAttempt();
    m_retryHost.clear();
    m_retryPort = 0;

    // Stash host/port for auto-reconnect and invalidate any earlier request.
    m_lastHost = host;
    m_lastPort = port;

    m_pollTimer.stop();
    m_keepaliveTimer.stop();
    m_connected = false;
    m_gotVersion = false;
    m_version.clear();
    m_readBuf.clear();
    m_seq = 0;
    // PR #279 review #3 (2026-05-23): clear the user-initiated flag on
    // every intentional connect so a manual reconnect re-arms
    // auto-reconnect on subsequent network drops.
    m_userInitiatedDisconnect = false;

    // Always cross one event-loop boundary. Qt documents that a socket may
    // not be ready for another connect while errorOccurred is unwinding, and
    // connectionFailed/reconnectAttempt consumers may replace the endpoint
    // synchronously. The owned timer is cancellable and generation-guarded.
    queueDial(host, port, m_endpointGeneration);
    if (wasConnected) {
        // Retiring A's socket callbacks also suppresses its disconnected
        // signal. Publish the transition so model/snapshot observers cannot
        // retain A's Connected state while B is pending. Emit last: a direct
        // consumer can replace B again, disconnect, or destroy this object.
        emit disconnected();
    }
}

TgxlConnection::SourceBindResult
TgxlConnection::bindSourceForHost(const QString& host)
{
    // 2026-05-26 KG4VCF multi-homed-host source-IP pick.  On a host
    // with overlapping subnets across more than one local interface
    // (e.g. macOS en0 AND a ZeroTier feth/utun overlay both
    // advertising 192.168.x.x), the OS routing picker may bind the
    // socket to an interface that can not actually reach this peer.
    // Re-probe per connect attempt and source-bind the TCP socket to
    // the kernel's ground-truth source for *this* target.  Independent
    // from PGXL's probe so a host can route to PGXL via one NIC and
    // TGXL via another.
    const QHostAddress targetAddr(host);
    if (targetAddr.isNull()) {
        return SourceBindResult::NotRequested;
    }
    // Test-only seam: exercise the real QTcpSocket::bind failure path with
    // an address that cannot belong to the local host.  Production keeps
    // using the route probe below.
    const bool forceBindFailure = m_testForceSourceBindFailure;
    m_testForceSourceBindFailure = false;
    const QHostAddress src = forceBindFailure
        ? QHostAddress(QStringLiteral("192.0.2.123"))
        : probeLocalAddressFor(targetAddr);
    if (src.isNull()) {
        return SourceBindResult::NotRequested;
    }
    QPointer<TgxlConnection> self(this);
    const bool bound = m_socket.bind(src, /*port=*/0);
    if (!self) {
        return SourceBindResult::Failed;
    }
    if (bound) {
        qCInfo(lcTgxl) << "source-bound to" << src.toString()
                       << "for target" << host;
        return SourceBindResult::Bound;
    } else {
        qCWarning(lcTgxl) << "source bind to" << src.toString()
                          << "failed:" << m_socket.errorString()
                          << "-- resetting before OS default routing";
        return SourceBindResult::Failed;
    }
}

bool TgxlConnection::requestIsCurrent(const QString& host, quint16 port,
                                      quint64 generation) const
{
    return generation == m_endpointGeneration
        && host == m_lastHost
        && port == m_lastPort
        && !m_userInitiatedDisconnect;
}

bool TgxlConnection::socketAttemptIsCurrent(quint64 attemptGeneration) const
{
    // attemptGeneration == 0 is reserved for the explicit parser-only test
    // seam and is never admitted here.
    return attemptGeneration != 0
        && attemptGeneration == m_socketAttemptGeneration
        && m_socketAttemptEndpointGeneration == m_endpointGeneration
        && !m_userInitiatedDisconnect;
}

void TgxlConnection::retireSocketAttempt()
{
    QObject::disconnect(m_socketConnectedConnection);
    QObject::disconnect(m_socketDisconnectedConnection);
    QObject::disconnect(m_socketReadyReadConnection);
    QObject::disconnect(m_socketErrorConnection);
    m_socketConnectedConnection = {};
    m_socketDisconnectedConnection = {};
    m_socketReadyReadConnection = {};
    m_socketErrorConnection = {};
    m_socketAttemptGeneration = 0;
    m_socketAttemptEndpointGeneration = 0;
}

void TgxlConnection::beginSocketAttempt(quint64 endpointGeneration)
{
    retireSocketAttempt();
    ++m_nextSocketAttemptGeneration;
    if (m_nextSocketAttemptGeneration == 0) {
        ++m_nextSocketAttemptGeneration; // zero is the offline-parser seam
    }
    const quint64 attempt = m_nextSocketAttemptGeneration;
    m_socketAttemptGeneration = attempt;
    m_socketAttemptEndpointGeneration = endpointGeneration;

    m_socketConnectedConnection = connect(
        &m_socket, &QTcpSocket::connected, this,
        [this, attempt] { onConnected(attempt); });
    m_socketDisconnectedConnection = connect(
        &m_socket, &QTcpSocket::disconnected, this,
        [this, attempt] { onDisconnected(attempt); });
    m_socketReadyReadConnection = connect(
        &m_socket, &QTcpSocket::readyRead, this,
        [this, attempt] { onReadyRead(attempt); });
    m_socketErrorConnection = connect(
        &m_socket, &QTcpSocket::errorOccurred, this,
        [this, attempt](QAbstractSocket::SocketError) { onError(attempt); });
}

void TgxlConnection::queueDial(const QString& host, quint16 port,
                               quint64 generation)
{
    if (!requestIsCurrent(host, port, generation)) {
        return;
    }
    // Retire the prior physical attempt as soon as the next dial is queued,
    // rather than leaving a same-endpoint late V/error callback able to stop
    // this zero-delay timer.
    retireSocketAttempt();
    m_connectHost = host;
    m_connectPort = port;
    m_connectGeneration = generation;
    m_connectTimer.start(0);
}

void TgxlConnection::onConnectTimeout()
{
    const QString host = m_connectHost;
    const quint16 port = m_connectPort;
    const quint64 generation = m_connectGeneration;
    if (!requestIsCurrent(host, port, generation)) {
        return;
    }
    retireSocketAttempt();
    // abort()/bind() can synchronously emit socket signals. Keep their public
    // diagnostics, but let this one controlled dial decide whether a retry is
    // needed after the socket reaches a stable state.
    m_suppressSocketReconnect = true;
    if (m_socket.state() != QAbstractSocket::UnconnectedState) {
        qCDebug(lcTgxl) << "TgxlConnection: resetting socket from state"
                        << m_socket.state() << "before dial";
        QPointer<TgxlConnection> self(this);
        m_socket.abort();
        if (!self) {
            return;
        }
    }

    // Every physical dial, including another attempt for the same endpoint,
    // gets a new token. Queued callbacks from the retired socket can still be
    // delivered by Qt, but their captured token can no longer be admitted.
    beginSocketAttempt(generation);
    m_readBuf.clear();
    m_gotVersion = false;
    if (!requestIsCurrent(host, port, generation)) {
        m_suppressSocketReconnect = false;
        return;
    }
    if (m_socket.state() != QAbstractSocket::UnconnectedState) {
        m_suppressSocketReconnect = false;
        qCWarning(lcTgxl) << "TgxlConnection: socket did not reset; retrying later";
        scheduleReconnect();
        return;
    }

    QPointer<TgxlConnection> self(this);
    const SourceBindResult bindResult = bindSourceForHost(host);
    if (!self) {
        return;
    }
    if (!requestIsCurrent(host, port, generation)) {
        m_suppressSocketReconnect = false;
        return;
    }
    if (bindResult == SourceBindResult::Failed) {
        // A failed bind can leave the native descriptor unusable. Explicitly
        // reset, validate, then make exactly one OS-default-routing dial.
        self = this;
        m_socket.abort();
        if (!self) {
            return;
        }
        if (!requestIsCurrent(host, port, generation)) {
            m_suppressSocketReconnect = false;
            return;
        }
        if (m_socket.state() != QAbstractSocket::UnconnectedState) {
            m_suppressSocketReconnect = false;
            qCWarning(lcTgxl) << "TgxlConnection: failed source bind did not reset;"
                                 " retrying later";
            scheduleReconnect();
            return;
        }
        qCInfo(lcTgxl) << "TgxlConnection: source bind unavailable; using OS default route";
    }
    m_suppressSocketReconnect = false;

    if (!requestIsCurrent(host, port, generation)) {
        return;
    }
    qCDebug(lcTgxl) << "TgxlConnection: connecting to" << host << ":" << port;
    m_socket.connectToHost(host, port);
}

// From AetherSDR src/core/TgxlConnection.cpp:32 [@0cd4559]
void TgxlConnection::disconnect()
{
    // PR #279 review #3 (2026-05-23): mark this disconnect as
    // user-initiated so onDisconnected() does not re-schedule a reconnect.
    // Without this, the Peripherals Disconnect button could not keep TGXL
    // disconnected because TGXL_AutoReconnect defaults true.  Flag is
    // cleared in connectToTgxl() on the next intentional connect.
    m_userInitiatedDisconnect = true;
    ++m_endpointGeneration;
    m_reconnectTimer.stop();
    m_connectTimer.stop();
    m_retryHost.clear();
    m_retryPort = 0;
    m_pollTimer.stop();
    m_keepaliveTimer.stop();
    m_connected = false;
    const bool notifyDisconnected =
        m_socket.state() != QAbstractSocket::UnconnectedState;
    retireSocketAttempt();
    m_socket.abort();
    if (notifyDisconnected) {
        // The real socket callback was deliberately retired before aborting.
        // Preserve the public lifecycle notification and touch no members
        // after it because a direct consumer may delete this object.
        emit disconnected();
    }
}

// From AetherSDR src/core/TgxlConnection.cpp:39 [@0cd4559]
void TgxlConnection::onConnected(quint64 attemptGeneration)
{
    if (!socketAttemptIsCurrent(attemptGeneration)) {
        return;
    }
    qCDebug(lcTgxl) << "TgxlConnection: TCP connected, waiting for version line";
    m_reconnectTimer.stop();
    m_connectTimer.stop();
    m_connectedSinceMs = QDateTime::currentMSecsSinceEpoch();
    // Bench-fix 2026-05-20: reset the version-received latch so the next
    // V-frame arrival re-arms the handshake and re-sets m_connected=true.
    // Without this, m_gotVersion stays sticky from the previous session
    // (TGXL drops + reconnects every ~14 s in the test setup), the V-frame
    // from the new connection is silently dropped by the `if (!m_gotVersion ...)`
    // guard in processLine(), and the Peripherals dialog reports
    // "disconnected" forever despite an ESTABLISHED TCP socket. Same bug
    // pattern existed in PgxlConnection.
    m_gotVersion = false;
    // TGXL sends V<version>\n first, then we send our init commands
}

// From AetherSDR src/core/TgxlConnection.cpp:45 [@0cd4559]
void TgxlConnection::onDisconnected(quint64 attemptGeneration)
{
    if (!socketAttemptIsCurrent(attemptGeneration)) {
        return;
    }
    const quint64 generation = m_endpointGeneration;
    // Phase 3P-II bench-diagnostic logging: record why TGXL dropped so the
    // bench audit trail shows the root cause. errorString() is populated by
    // Qt when the disconnect was caused by a network error; empty string means
    // a clean (operator-initiated) close.
    const QString err = m_socket.errorString();
    if (err.isEmpty()) {
        qCInfo(lcTgxl) << "TGXL disconnected cleanly";
    } else {
        qCWarning(lcTgxl) << "TGXL disconnected with error:" << err;
    }
    m_pollTimer.stop();
    m_keepaliveTimer.stop();
    m_connected = false;
    QPointer<TgxlConnection> self(this);
    emit disconnected();
    if (!self) {
        return;
    }
    // PR #279 review #3 (2026-05-23): only auto-reconnect on network
    // drops, not user-initiated disconnects.  disconnect() (the
    // Peripherals Disconnect button path) sets m_userInitiatedDisconnect
    // before calling m_socket.disconnectFromHost(), and the flag is
    // cleared by connectToTgxl() on the next intentional connect.
    if (m_userInitiatedDisconnect) {
        qCInfo(lcTgxl)
            << "TGXL user-initiated disconnect; auto-reconnect suppressed";
        return;
    }
    if (generation != m_endpointGeneration
        || !socketAttemptIsCurrent(attemptGeneration)
        || m_suppressSocketReconnect) {
        return;
    }
    scheduleReconnect();
}

// From AetherSDR src/core/TgxlConnection.cpp:53 [@0cd4559]
void TgxlConnection::onError(quint64 attemptGeneration)
{
    if (!socketAttemptIsCurrent(attemptGeneration)) {
        return;
    }
    const quint64 generation = m_endpointGeneration;
    const QString err = m_socket.errorString();
    if (m_connected) {
        // Transient socket noise on an established connection (e.g., brief
        // network blip during status polling). Log it; don't overwrite the
        // UI status label via connectionFailed. The connection will either
        // recover silently or onDisconnected will fire and update status
        // cleanly.
        qCWarning(lcTgxl) << "transient socket error while connected:" << err;
        return;
    }
    qCWarning(lcTgxl) << "TgxlConnection: connect-time socket error:" << err;
    QPointer<TgxlConnection> self(this);
    emit connectionFailed(err);
    if (!self) {
        return;
    }
    // 2026-05-20 bench fix: connect-time failures (e.g. TGXL refusing the
    // initial handshake, "Unknown error", "Invalid socket descriptor")
    // do NOT trigger onDisconnected, so without this scheduleReconnect()
    // the exponential backoff stops dead after a single failed retry.
    // Schedule another attempt so the connection keeps trying until
    // TGXL accepts (typically 1-15 s after the device's hardware
    // watchdog reset). Without this fix, TGXL :9010 stays dead for
    // the rest of the session after any single failed retry.
    if (generation == m_endpointGeneration
        && socketAttemptIsCurrent(attemptGeneration)
        && !m_userInitiatedDisconnect
        && !m_suppressSocketReconnect) {
        scheduleReconnect();
    }
}

// From AetherSDR src/core/TgxlConnection.cpp:60 [@0cd4559]
void TgxlConnection::onReadyRead(quint64 attemptGeneration)
{
    if (!socketAttemptIsCurrent(attemptGeneration)) {
        return;
    }
    QByteArray chunk = m_socket.readAll();
    m_bytesIn += quint64(chunk.size());
    m_readBuf.append(chunk);

    while (true) {
        int idx = m_readBuf.indexOf('\n');
        if (idx < 0) { break; }

        QString line = QString::fromUtf8(m_readBuf.left(idx)).trimmed();
        m_readBuf.remove(0, idx + 1);

        if (!line.isEmpty()) {
            ++m_framesIn;
            m_lastFrameMs = QDateTime::currentMSecsSinceEpoch();
            QPointer<TgxlConnection> self(this);
            processLine(line, attemptGeneration);
            if (!self || !socketAttemptIsCurrent(attemptGeneration)) {
                return;
            }
        }
    }
}

// From AetherSDR src/core/TgxlConnection.cpp:76 [@0cd4559]
void TgxlConnection::processLine(const QString& line,
                                 quint64 attemptGeneration)
{
    const bool offlineTest = (attemptGeneration == 0);
    if (!offlineTest && !socketAttemptIsCurrent(attemptGeneration)) {
        return;
    }
    // Version line: V1.2.17
    if (!m_gotVersion && line.startsWith('V')) {
        m_version = line.mid(1);
        m_gotVersion = true;
        qCDebug(lcTgxl) << "TgxlConnection: TGXL version" << m_version;
        // Phase 3P-II bench-diagnostic logging (remove after pairing protocol confirmed)
        qCInfo(lcTgxl) << "RX V-frame version=" << m_version;

        // The version frame, rather than TCP acceptance alone, completes the
        // TGXL handshake and starts a fresh reconnect cycle.
        m_reconnectTimer.stop();
        m_connectTimer.stop();
        m_reconnectAttempts = 0;
        m_retryHost.clear();
        m_retryPort = 0;

        // Send init commands
        QPointer<TgxlConnection> self(this);
        sendCommand("info");
        if (!self
            || (!offlineTest && !socketAttemptIsCurrent(attemptGeneration))) {
            return;
        }
        sendCommand("status");
        if (!self
            || (!offlineTest && !socketAttemptIsCurrent(attemptGeneration))) {
            return;
        }

        m_connected = true;
        m_pollTimer.start();
        emit connected();
        return;
    }

    // Response: R<seq>|<code>|<body>
    // Status poll responses contain fwd/swr meter data as KV pairs.
    // Format: R<seq>|0|key=val key=val ...
    if (line.startsWith('R')) {
        // Extract body after second pipe: R<seq>|<code>|<body>
        int pipe1 = line.indexOf('|');
        int pipe2 = (pipe1 >= 0) ? line.indexOf('|', pipe1 + 1) : -1;
        if (pipe2 >= 0) {
            // Parse sequence number and hex status code.
            quint32 rseq = line.mid(1, pipe1 - 1).toUInt();
            QString hexStr = line.mid(pipe1 + 1, pipe2 - pipe1 - 1).trimmed();
            bool hexOk = false;
            uint hexCode = hexStr.toUInt(&hexOk, 16);

            QString body = line.mid(pipe2 + 1).trimmed();

            // Phase 3P-II bench-diagnostic logging (remove after pairing protocol confirmed)
            if (hexOk && hexCode != 0) {
                qCWarning(lcTgxl) << "RX R-frame seq=" << rseq << "hex=" << QString::number(hexCode, 16) << "body:" << body;
            } else {
                qCInfo(lcTgxl) << "RX R-frame seq=" << rseq << "hex=" << (hexOk ? QString::number(hexCode, 16) : "PARSE_ERROR") << "body:" << body;
            }

            // Pong correlation: any R-frame matching a pending ping seq is a pong.
            if (m_pendingPings.contains(rseq)) {
                PendingPing pp = m_pendingPings.take(rseq);
                qint64 rttMs = QDateTime::currentMSecsSinceEpoch() - pp.sentMs;
                QPointer<TgxlConnection> self(this);
                emit pongReceived(rseq, rttMs, pp.tag);
                if (!self
                    || (!offlineTest
                        && !socketAttemptIsCurrent(attemptGeneration))) {
                    return;
                }
            }

            // Save acknowledgement: R<seq>|0|saving
            if (hexOk && hexCode == 0 && body == "saving") {
                QPointer<TgxlConnection> self(this);
                emit saveAcknowledged();
                if (!self
                    || (!offlineTest
                        && !socketAttemptIsCurrent(attemptGeneration))) {
                    return;
                }
            }

            // Setup read response correlation.
            if (m_pendingSetupSeq != 0 && rseq == m_pendingSetupSeq) {
                m_pendingSetupSeq = 0;
                if (!body.isEmpty()) {
                    QMap<QString, QString> kvs;
                    const auto parts = body.split(' ', Qt::SkipEmptyParts);
                    for (const auto& part : parts) {
                        int eq = part.indexOf('=');
                        if (eq > 0) { kvs.insert(part.left(eq), part.mid(eq + 1)); }
                    }
                    if (!kvs.isEmpty()) { emit setupResponse(kvs); }
                }
                return;
            }

            // Ifconf read response correlation.
            if (m_pendingIfconfSeq != 0 && rseq == m_pendingIfconfSeq) {
                m_pendingIfconfSeq = 0;
                if (!body.isEmpty()) {
                    QMap<QString, QString> kvs;
                    const auto parts = body.split(' ', Qt::SkipEmptyParts);
                    for (const auto& part : parts) {
                        int eq = part.indexOf('=');
                        if (eq > 0) { kvs.insert(part.left(eq), part.mid(eq + 1)); }
                    }
                    if (!kvs.isEmpty()) { emit ifconfResponse(kvs); }
                }
                return;
            }

            // General KV body: emit statusUpdated (covers poll responses, etc.).
            if (!body.isEmpty()) {
                QMap<QString, QString> kvs;
                const auto parts = body.split(' ', Qt::SkipEmptyParts);
                for (const auto& part : parts) {
                    int eq = part.indexOf('=');
                    if (eq > 0)
                        kvs.insert(part.left(eq), part.mid(eq + 1));
                }
                if (!kvs.isEmpty())
                    emit statusUpdated(kvs);
            }
        }
        return;
    }

    // State push: S0|state key=val key=val ...
    // Status poll response: S<seq>|status key=val key=val ...
    // Frame format per 4O3A TGXL API + design §6.1:
    // the <object> prefix is required; drop frames that lack it.
    if (line.startsWith('S')) {
        int pipe = line.indexOf('|');
        if (pipe < 0) { return; }

        QString rest = line.mid(pipe + 1);

        // Find object name -- everything before first key=val
        // "state bypassA=0 ..." or "status fwd=0.0000 ..."
        int firstEq = rest.indexOf('=');
        if (firstEq < 0) { return; }
        int lastSpaceBeforeEq = rest.lastIndexOf(' ', firstEq);
        if (lastSpaceBeforeEq < 0) { return; }  // <object> prefix required; strict per design §6.1

        QString object   = rest.left(lastSpaceBeforeEq).trimmed();
        QString kvString = rest.mid(lastSpaceBeforeEq + 1);

        // Parse key=value pairs
        QMap<QString, QString> kvs;
        const auto parts = kvString.split(' ', Qt::SkipEmptyParts);
        for (const auto& part : parts) {
            int eq = part.indexOf('=');
            if (eq > 0)
                kvs.insert(part.left(eq), part.mid(eq + 1));
        }

        // Phase 3P-II bench-diagnostic logging (remove after pairing protocol confirmed)
        if (!kvs.isEmpty()) {
            qCInfo(lcTgxl) << "RX S-frame object=" << object << "state=" << kvs.value("state", kvs.value("status", "unknown"));
        }

        if (object == "state") {
            emit stateUpdated(kvs);
        } else if (object == "status") {
            emit statusUpdated(kvs);
        }
        return;
    }
}

// From AetherSDR src/core/TgxlConnection.cpp:154 [@0cd4559]
quint32 TgxlConnection::sendCommand(const QString& cmd)
{
    quint32 seq = ++m_seq;
    QString line = QString("C%1|%2\n").arg(seq).arg(cmd);
    m_socket.write(line.toUtf8());
    qCDebug(lcTgxl) << "TgxlConnection: sent" << line.trimmed();
    // Phase 3P-II bench-diagnostic logging (remove after pairing protocol confirmed)
    qCInfo(lcTgxl) << "TX seq=" << seq << "cmd:" << cmd;
    ++m_framesOut;
    m_bytesOut += quint64(line.size());
    // Emit last: a direct test/diagnostic consumer may delete this object.
    emit testFrameWrittenForTesting(line.trimmed());  // test seam
    return seq;
}

// From AetherSDR src/core/TgxlConnection.cpp:163 [@0cd4559]
void TgxlConnection::adjustRelay(int relay, int direction)
{
    if (!m_connected) { return; }
    if (relay < 0 || relay > 2) { return; }
    int move = (direction > 0) ? 1 : -1;
    sendCommand(QString("tune relay=%1 move=%2").arg(relay).arg(move));
}

// From AetherSDR src/core/TgxlConnection.cpp:171 [@0cd4559]
void TgxlConnection::pollStatus()
{
    if (m_connected) {
        sendCommand("status");
    }
}

// -------------------------------------------------------------------------
// Tier 2 NereusSDR-native command surface.
// Wire formats from design §4.2.1 + §6.4 (4O3A TGXL Ethernet API).
// -------------------------------------------------------------------------

// keepalive enable: instructs TGXL to expect periodic status pokes.
// AppSettings key TGXL_KeepaliveSec (default "30").
quint32 TgxlConnection::enableKeepalive()
{
    quint32 seq = sendCommand("keepalive enable");
    auto& s = AppSettings::instance();
    int intervalSec = s.value("TGXL_KeepaliveSec", "30").toInt();
    m_keepaliveTimer.setInterval(intervalSec * 1000);
    m_keepaliveTimer.start();
    return seq;
}

// ping: no-op roundtrip for RTT measurement. Correlates via R-frame seq.
quint32 TgxlConnection::ping(const QString& tag)
{
    quint32 seq = sendCommand("ping");
    m_pendingPings.insert(seq, PendingPing{seq, QDateTime::currentMSecsSinceEpoch(), tag});
    return seq;
}

// setup read: request current device configuration as KV pairs.
quint32 TgxlConnection::readSetup()
{
    quint32 seq = sendCommand("setup read");
    m_pendingSetupSeq = seq;
    return seq;
}

// setup <k=v ...>: write device configuration fields.
quint32 TgxlConnection::writeSetup(const QMap<QString,QString>& fields)
{
    QStringList parts;
    for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
        parts << QString("%1=%2").arg(it.key(), it.value());
    }
    return sendCommand(QString("setup %1").arg(parts.join(' ')));
}

// ifconf read: request current network interface configuration.
quint32 TgxlConnection::readIfconf()
{
    quint32 seq = sendCommand("ifconf read");
    m_pendingIfconfSeq = seq;
    return seq;
}

// ifconf address=<ip> netmask=<mask> gateway=<gw> dhcp=<true|false>
quint32 TgxlConnection::writeIfconf(const QString& ip,
                                    const QString& netmask,
                                    const QString& gateway,
                                    bool dhcp)
{
    return sendCommand(
        QString("ifconf address=%1 netmask=%2 gateway=%3 dhcp=%4")
            .arg(ip)
            .arg(netmask)
            .arg(gateway)
            .arg(dhcp ? "true" : "false"));
}

// save: persist configuration; TGXL will reboot after ack.
quint32 TgxlConnection::save()
{
    return sendCommand("save");
}

void TgxlConnection::onKeepaliveTimeout()
{
    if (m_connected) {
        sendCommand("status");
    }
}

void TgxlConnection::onPingTimeoutCheck()
{
    qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    QList<quint32> stale;
    for (auto it = m_pendingPings.cbegin(); it != m_pendingPings.cend(); ++it) {
        if (nowMs - it.value().sentMs > 5000) {
            stale << it.key();
        }
    }
    for (quint32 seq : stale) {
        emit pingTimedOut(seq);
        m_pendingPings.remove(seq);
    }
}

void TgxlConnection::scheduleReconnect()
{
    auto& s = AppSettings::instance();
    if (s.value("TGXL_AutoReconnect", "True").toString() != "True") {
        qCInfo(lcTgxl) << "scheduleReconnect: TGXL_AutoReconnect=False, NOT retrying";
        return;
    }
    if (m_lastHost.isEmpty()) {
        qCInfo(lcTgxl) << "scheduleReconnect: m_lastHost empty, NOT retrying"
                          " (never had a successful initial connect)";
        return;
    }
    if (m_userInitiatedDisconnect) {
        qCInfo(lcTgxl) << "scheduleReconnect: operator disconnected, NOT retrying";
        return;
    }
    // An owned active timer is the deduplication identity for paired
    // errorOccurred/disconnected delivery. A queued dial likewise already
    // represents the current endpoint's next attempt.
    if (m_reconnectTimer.isActive() || m_connectTimer.isActive()) {
        qCDebug(lcTgxl) << "scheduleReconnect: retry or dial already pending";
        return;
    }

    int idx = std::min(m_reconnectAttempts, int(std::size(kTgxlBackoffSec)) - 1);
    int delayMs = kTgxlBackoffSec[idx] * m_reconnectBackoffUnitMs;
    ++m_reconnectAttempts;
    const int attempt = m_reconnectAttempts;
    m_retryHost = m_lastHost;
    m_retryPort = m_lastPort;
    m_retryGeneration = m_endpointGeneration;
    qCInfo(lcTgxl) << "scheduleReconnect: attempt #" << m_reconnectAttempts
                   << "scheduled in" << delayMs << "ms to"
                   << m_retryHost << ":" << m_retryPort;
    m_reconnectTimer.start(delayMs);

    // Start before notifying observers. A direct observer may replace the
    // endpoint, disconnect, or delete this object; each action can cancel the
    // owned timer, and no member is touched after the emission.
    emit reconnectAttempt(attempt, delayMs);
}

void TgxlConnection::onReconnectTimeout()
{
    if (AppSettings::instance().value("TGXL_AutoReconnect", "True").toString()
        != "True") {
        qCInfo(lcTgxl) << "TgxlConnection: reconnect disabled before timeout fired";
        return;
    }
    const QString host = m_retryHost;
    const quint16 port = m_retryPort;
    const quint64 generation = m_retryGeneration;
    if (!requestIsCurrent(host, port, generation)) {
        qCDebug(lcTgxl) << "TgxlConnection: discarded stale reconnect timeout";
        return;
    }
    queueDial(host, port, generation);
}

void TgxlConnection::testForceDisconnect()
{
    m_connected = false;
    // Each call represents a distinct synthetic drop. Cancel the previous
    // synthetic attempt so back-to-back calls still exercise the full
    // backoff sequence without leaving live callbacks behind.
    m_reconnectTimer.stop();
    m_connectTimer.stop();
    scheduleReconnect();
}

void TgxlConnection::testInjectLineForSocketAttempt(
    const QString& line, quint64 attemptGeneration)
{
    processLine(line, attemptGeneration);
}

void TgxlConnection::testInjectFailureForSocketAttempt(
    quint64 attemptGeneration)
{
    QPointer<TgxlConnection> self(this);
    onError(attemptGeneration);
    if (!self) {
        return;
    }
    onDisconnected(attemptGeneration);
}

void TgxlConnection::testFlushPingTimeouts()
{
    QList<quint32> allSeqs = m_pendingPings.keys();
    for (quint32 seq : allSeqs) {
        emit pingTimedOut(seq);
        m_pendingPings.remove(seq);
    }
}

}  // namespace NereusSDR
