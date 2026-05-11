// SPDX-License-Identifier: GPL-3.0-or-later
//
// NereusSDR - FreeDVReporterClient implementation.
//
// Ported from freedv-gui src/reporting/FreeDVReporter.cpp [@77e793a].
//   Wire-protocol logic, Socket.IO event names, JSON field shapes,
//   bulk_update fan-out, and the view-only auth payload all come from
//   the freedv-gui source.
//
// Structural pattern from AetherSDR src/core/FreeDvClient.cpp
// [@0cd4559].
//   Qt6 lifecycle (QWebSocket member + slots, QTimer ping/reconnect,
//   exponential-backoff reconnect math) follow the AetherSDR client,
//   which itself targeted the same Engine.IO / Socket.IO wire format.
//
// License (upstream):
//   - freedv-gui carries an LGPLv2.1+ root license (`freedv-gui/COPYING`).
//     The specific `FreeDVReporter.{h,cpp}` files carry a permissive
//     BSD-2-Clause-style file header (Copyright Mooneer Salem, no per-
//     file project copyright line); the BSD permission block is
//     reproduced verbatim below per the upstream redistribution clause.
//   - AetherSDR is GPL-3.0-or-later
//     (https://github.com/ten9876/AetherSDR/blob/main/LICENSE).
//
// LGPL is upgrade-compatible to GPL-3 (LGPL §3 conversion clause); the
// BSD-2-Clause file-header carve-out is GPL-compatible by its own terms.
//
// --- From freedv-gui src/reporting/FreeDVReporter.cpp [@77e793a] (verbatim header) ---
//
// =========================================================================
//  Name:            FreeDVReporter.h
//  Purpose:         Implementation of interface to freedv-reporter
//
//  Authors:         Mooneer Salem
//  License:
//
//  All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions
//  are met:
//
//  - Redistributions of source code must retain the above copyright
//  notice, this list of conditions and the following disclaimer.
//
//  - Redistributions in binary form must reproduce the above copyright
//  notice, this list of conditions and the following disclaimer in the
//  documentation and/or other materials provided with the distribution.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
//  ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
//  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
//  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
//  OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
//  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
//  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
//  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
//  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
//  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
//  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// =========================================================================
//
// Modification history (NereusSDR):
//   2026-05-10  J.J. Boyd / KG4VCF  Phase 3J-2 Task B5. See
//                                    FreeDVReporterClient.h for the full
//                                    attribution block. Implementation
//                                    follows the freedv-gui dispatch
//                                    table (connect_() :361-369) for
//                                    the on(...) handlers, AetherSDR's
//                                    Qt-side parser (handleEngineIO /
//                                    handleSocketIO at FreeDvClient.cpp
//                                    :141-219 [@0cd4559]) for the
//                                    Engine.IO / Socket.IO state
//                                    machine, and the freedv-gui per-
//                                    event handler bodies (:372-651)
//                                    for the JSON field extraction.
//                                    Dual-feed onFreqChange /
//                                    onRxReport spot synthesis
//                                    (`emitSpotFromFreqChange` /
//                                    `emitSpotFromRxReport`) is a
//                                    NereusSDR architectural decision
//                                    per design doc Section 4 Flow 2;
//                                    the spot field mapping mirrors
//                                    AetherSDR FreeDvClient.cpp
//                                    :233-370 [@0cd4559]. Logging
//                                    routes through NereusSDR's
//                                    `lcSpots` category instead of
//                                    AetherSDR's `lcDxCluster`; log
//                                    file path uses Qt's
//                                    AppConfigLocation (already lands
//                                    under `NereusSDR/`) instead of
//                                    AetherSDR's GenericConfigLocation
//                                    + "AetherSDR/freedv.log". AI
//                                    tooling: Anthropic Claude Code.

#include "FreeDVReporterClient.h"
#include "LogCategories.h"
#include "AppSettings.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <cmath>

namespace NereusSDR {

FreeDVReporterClient::FreeDVReporterClient(QObject* parent)
    : QObject(parent)
    , m_pingTimer(new QTimer(this))
    , m_reconnectTimer(new QTimer(this))
    , m_serverUrl(QString::fromLatin1(DefaultWsUrl))
{
#ifdef HAVE_WEBSOCKETS
    // From AetherSDR src/core/FreeDvClient.cpp:14-39 [@0cd4559]
    m_ws = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(m_ws, &QWebSocket::connected, this, &FreeDVReporterClient::onWsConnected);
    connect(m_ws, &QWebSocket::disconnected, this, &FreeDVReporterClient::onWsDisconnected);
    connect(m_ws, &QWebSocket::textMessageReceived, this, &FreeDVReporterClient::onWsTextMessage);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(m_ws, &QWebSocket::errorOccurred, this, &FreeDVReporterClient::onWsError);
#else
    connect(m_ws, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
            this, &FreeDVReporterClient::onWsError);
#endif
#endif

    m_pingTimer->setSingleShot(false);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &FreeDVReporterClient::onReconnectTimer);

    // Engine.IO ping keepalive. Reply handled in handleEngineIO.
    // From AetherSDR src/core/FreeDvClient.cpp:34-38 [@0cd4559]
    connect(m_pingTimer, &QTimer::timeout, this, [this] {
#ifdef HAVE_WEBSOCKETS
        if (m_ws && m_ws->state() == QAbstractSocket::ConnectedState) {
            sendText(QStringLiteral("2"));  // client-side Engine.IO Ping
        }
#endif
    });
}

FreeDVReporterClient::~FreeDVReporterClient()
{
    stopConnection();
    m_logFile.close();
}

void FreeDVReporterClient::setIdentity(const QString& callsign, const QString& gridSquare,
                                       const QString& message, const QString& version)
{
    m_callsign = callsign;
    m_gridSquare = gridSquare;
    m_statusMessage = message;
    m_version = version;
}

void FreeDVReporterClient::setServerUrl(const QString& url)
{
    m_serverUrl = url;
}

QString FreeDVReporterClient::logFilePath() const
{
    // Qt's AppConfigLocation already lands under "NereusSDR/" when
    // QCoreApplication::organizationName / applicationName are set
    // (mirrors SpotCollectorClient / PotaClient / DxClusterClient /
    // WsjtxClient B1-B4 ports).
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
           + "/freedv.log";
}

void FreeDVReporterClient::startConnection()
{
    if (m_connected.load()) return;

    qCDebug(lcSpots) << "FreeDVReporterClient: connecting to" << m_serverUrl;

    // Open log file (truncate on each start; same pattern as the other
    // spot-ingest clients).
    m_logFile.close();
    m_logFile.setFileName(logFilePath());
    QDir().mkpath(QFileInfo(m_logFile).absolutePath());
    if (m_logFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        m_logFile.write(QString("--- FreeDV Reporter connected at %1 ---\n")
            .arg(QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd HH:mm:ss UTC")).toUtf8());
        m_logFile.flush();
    }

    m_stations.clear();
    m_intentionalDisconnect = false;
    m_reconnectAttempts = 0;

#ifdef HAVE_WEBSOCKETS
    if (m_ws) {
        m_ws->open(QUrl(m_serverUrl));
    }
#else
    qCWarning(lcSpots) << "FreeDVReporterClient: built without Qt6::WebSockets;"
                       << "live connections are disabled.";
    emit connectionError(QStringLiteral("Qt6::WebSockets not available"));
#endif
}

void FreeDVReporterClient::stopConnection()
{
    m_intentionalDisconnect = true;
    m_pingTimer->stop();
    m_reconnectTimer->stop();

#ifdef HAVE_WEBSOCKETS
    if (m_ws && m_ws->state() != QAbstractSocket::UnconnectedState) {
        m_ws->close();
    }
#endif

    m_connected.store(false);
    m_stations.clear();
    emit disconnected();
}

void FreeDVReporterClient::requestQSY(const QString& targetSid, quint64 freqHz,
                                      const QString& message)
{
    // From freedv-gui src/reporting/FreeDVReporter.cpp:104-119 [@77e793a]
    if (!m_connected.load()) return;

    QJsonObject obj;
    obj["dest_sid"] = targetSid;
    obj["message"] = message;
    obj["frequency"] = qint64(freqHz);

    QJsonArray arr;
    arr.append(QStringLiteral("qsy_request"));
    arr.append(obj);
    sendText(QStringLiteral("42")
             + QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

void FreeDVReporterClient::updateMessage(const QString& message)
{
    // From freedv-gui src/reporting/FreeDVReporter.cpp:122-130 [@77e793a]
    // (the inner sendMessageImpl_ at :688-702).
    m_statusMessage = message;
    if (!m_connected.load()) return;

    QJsonObject obj;
    obj["message"] = message;

    QJsonArray arr;
    arr.append(QStringLiteral("message_update"));
    arr.append(obj);
    sendText(QStringLiteral("42")
             + QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

// ── WebSocket slots ─────────────────────────────────────────────────────

void FreeDVReporterClient::onWsConnected()
{
    // Engine.IO open packet will arrive as the first text message;
    // we send the Socket.IO Connect from handleEngineIO type '0'
    // (handshake completes on receipt, not on TCP up).
    // From AetherSDR src/core/FreeDvClient.cpp:92-96 [@0cd4559]
    qCDebug(lcSpots) << "FreeDVReporterClient: WebSocket connected";
}

void FreeDVReporterClient::onWsDisconnected()
{
    // From AetherSDR src/core/FreeDvClient.cpp:98-115 [@0cd4559]
    m_connected.store(false);
    m_pingTimer->stop();
    m_stations.clear();

    if (m_intentionalDisconnect) {
        qCDebug(lcSpots) << "FreeDVReporterClient: disconnected (intentional)";
        return;
    }

    // Auto-reconnect with exponential backoff.
    // From AetherSDR src/core/FreeDvClient.cpp:107-113 [@0cd4559] — original
    // pattern is `1 << m_reconnectAttempts`. NereusSDR fix: clamp shift count
    // to avoid signed-int UB. The backoff saturates at MaxReconnectDelayMs
    // well before 30 attempts, so capping has no behavioral effect on a
    // healthy connection.
    const int shiftBits = std::min(m_reconnectAttempts, 30);
    int delay = std::min(InitialReconnectDelayMs * (1 << shiftBits),
                         MaxReconnectDelayMs);
    qCDebug(lcSpots) << "FreeDVReporterClient: disconnected, reconnecting in" << delay << "ms";
    emit rawLineReceived(QString("--- Disconnected, reconnecting in %1s ---").arg(delay / 1000));
    m_reconnectTimer->start(delay);
}

void FreeDVReporterClient::onWsError(QAbstractSocket::SocketError err)
{
    // From AetherSDR src/core/FreeDvClient.cpp:117-123 [@0cd4559]
    Q_UNUSED(err);
#ifdef HAVE_WEBSOCKETS
    QString msg = m_ws ? m_ws->errorString() : QStringLiteral("(unknown)");
#else
    QString msg = QStringLiteral("(WebSockets disabled)");
#endif
    qCWarning(lcSpots) << "FreeDVReporterClient: WebSocket error:" << msg;
    emit connectionError(msg);
}

void FreeDVReporterClient::onReconnectTimer()
{
    // From AetherSDR src/core/FreeDvClient.cpp:125-131 [@0cd4559]
    if (m_intentionalDisconnect) return;
    m_reconnectAttempts++;
    qCDebug(lcSpots) << "FreeDVReporterClient: reconnect attempt" << m_reconnectAttempts;
#ifdef HAVE_WEBSOCKETS
    if (m_ws) {
        m_ws->open(QUrl(m_serverUrl));
    }
#endif
}

void FreeDVReporterClient::onWsTextMessage(const QString& message)
{
    // From AetherSDR src/core/FreeDvClient.cpp:135-139 [@0cd4559]
    if (message.isEmpty()) return;
    handleEngineIO(message);
}

void FreeDVReporterClient::sendText(const QString& msg)
{
    m_lastSentForTest = msg;
#ifdef HAVE_WEBSOCKETS
    if (m_ws && m_ws->state() == QAbstractSocket::ConnectedState) {
        m_ws->sendTextMessage(msg);
    }
#endif
}

// ── Engine.IO / Socket.IO framing ───────────────────────────────────────
//
// Wire protocol authority: freedv-gui ships its own SocketIoClient
// (src/util/SocketIoClient.cpp [@77e793a]) which we do not port (it
// uses the websocketpp 3rd-party library and its own thread). Qt
// structure: AetherSDR src/core/FreeDvClient.cpp:141-219 [@0cd4559]
// already implemented the same Engine.IO v4 / Socket.IO v4 state
// machine on top of QWebSocket. We follow the AetherSDR shape with
// freedv-gui-faithful event dispatch.

void FreeDVReporterClient::handleEngineIO(const QString& raw)
{
    // From AetherSDR src/core/FreeDvClient.cpp:141-179 [@0cd4559]
    if (raw.isEmpty()) return;
    QChar type = raw.at(0);

    if (type == '0') {
        // Engine.IO Open: server sends JSON with sid, pingInterval,
        // pingTimeout. Parse pingInterval and start the keepalive
        // timer, then respond with Socket.IO Connect.
        QJsonDocument doc = QJsonDocument::fromJson(raw.mid(1).toUtf8());
        QJsonObject obj = doc.object();
        m_pingIntervalMs = obj.value(QStringLiteral("pingInterval")).toInt(25000);
        m_pingTimer->start(m_pingIntervalMs);

        // Send Socket.IO Connect with view-only auth.
        // "40" = Engine.IO Message (4) + Socket.IO Connect (0).
        // Wire payload from freedv-gui FreeDVReporter.cpp:295-309
        // [@77e793a] view-mode branch:
        //   {"role":"view","protocol_version":2}
        // (NereusSDR is always view-only at present; reporter mode is
        // a 3M-x follow-up.)
        QJsonObject auth;
        auth["role"] = QStringLiteral("view");
        auth["protocol_version"] = 2;
        sendText(QStringLiteral("40") + QString::fromUtf8(
            QJsonDocument(auth).toJson(QJsonDocument::Compact)));
        return;
    }

    if (type == '2') {
        // Engine.IO Ping -> reply with Pong
        sendText(QStringLiteral("3"));
        return;
    }

    if (type == '3') {
        // Engine.IO Pong -- response to our keepalive ping
        return;
    }

    if (type == '4') {
        // Engine.IO Message -- contains Socket.IO payload
        handleSocketIO(raw.mid(1));
        return;
    }
    // type '6' = noop, others = ignore
}

void FreeDVReporterClient::handleSocketIO(const QString& payload)
{
    // From AetherSDR src/core/FreeDvClient.cpp:181-219 [@0cd4559]
    if (payload.isEmpty()) return;

    QChar sioType = payload.at(0);

    if (sioType == '0') {
        // Socket.IO Connect ACK -- "0{"sid":"..."}"
        m_connected.store(true);
        m_reconnectAttempts = 0;
        qCDebug(lcSpots) << "FreeDVReporterClient: Socket.IO connected";
        emit rawLineReceived(QStringLiteral("Connected to ") + m_serverUrl);
        emit connected();
        return;
    }

    if (sioType == '2') {
        // Socket.IO Event -- "2["event_name",{...}]"
        QString json = payload.mid(1);
        QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        if (!doc.isArray()) return;
        QJsonArray arr = doc.array();
        if (arr.size() < 2) return;

        QString eventName = arr[0].toString();
        if (eventName == QStringLiteral("bulk_update")) {
            onBulkUpdate(arr[1].toArray());
        } else {
            handleEvent(eventName, arr[1].toObject());
        }
        return;
    }

    if (sioType == '1') {
        // Socket.IO Disconnect
        qCDebug(lcSpots) << "FreeDVReporterClient: server disconnected us";
        m_connected.store(false);
        return;
    }
}

// ── Event dispatch ──────────────────────────────────────────────────────
//
// Dispatch table mirrors freedv-gui src/reporting/FreeDVReporter.cpp
// :361-369 [@77e793a] (the on() registrations inside connect_()).

void FreeDVReporterClient::handleEvent(const QString& eventName, const QJsonObject& data)
{
    if      (eventName == QStringLiteral("new_connection"))         onNewConnection(data);
    else if (eventName == QStringLiteral("freq_change"))            onFreqChange(data);
    else if (eventName == QStringLiteral("rx_report"))              onRxReport(data);
    else if (eventName == QStringLiteral("tx_report"))              onTxReport(data);
    else if (eventName == QStringLiteral("remove_connection"))      onRemoveConnection(data);
    else if (eventName == QStringLiteral("message_update"))         onMessageUpdate(data);
    else if (eventName == QStringLiteral("connection_successful"))  onConnectionSuccessful(data);
    // Silently ignore: chat_*, qsy_request (we are view-only), etc.
}

void FreeDVReporterClient::onNewConnection(const QJsonObject& data)
{
    // From freedv-gui src/reporting/FreeDVReporter.cpp:372-406 [@77e793a]
    // (onFreeDVReporterNewConnection_).
    QString sid = data.value(QStringLiteral("sid")).toString();
    if (sid.isEmpty()) return;

    FreeDVStation info;
    info.sid          = sid;
    info.callsign     = data.value(QStringLiteral("callsign")).toString();
    info.gridSquare   = data.value(QStringLiteral("grid_square")).toString();
    info.version      = data.value(QStringLiteral("version")).toString();
    info.rxOnly       = data.value(QStringLiteral("rx_only")).toBool();
    info.status       = info.rxOnly ? QStringLiteral("RX Only") : QStringLiteral("Active");
    info.connectTime  = QDateTime::fromString(
        data.value(QStringLiteral("connect_time")).toString(), Qt::ISODate);
    info.lastUpdate   = QDateTime::fromString(
        data.value(QStringLiteral("last_update")).toString(), Qt::ISODate);

    m_stations[sid] = info;
    emit stationAdded(sid, info);
}

void FreeDVReporterClient::onFreqChange(const QJsonObject& data)
{
    // From freedv-gui src/reporting/FreeDVReporter.cpp:555-583 [@77e793a]
    // (onFreeDVReporterFrequencyChange_).
    QString sid = data.value(QStringLiteral("sid")).toString();
    if (sid.isEmpty()) return;

    // Create the entry if new_connection hasn't arrived yet (bulk_update
    // can re-fire freq_change before the connect packet is replayed).
    // From AetherSDR src/core/FreeDvClient.cpp:243-262 [@0cd4559]
    if (!m_stations.contains(sid)) {
        FreeDVStation info;
        info.sid       = sid;
        info.callsign  = data.value(QStringLiteral("callsign")).toString();
        info.gridSquare = data.value(QStringLiteral("grid_square")).toString();
        m_stations.insert(sid, info);
    }

    auto& info = m_stations[sid];
    info.frequencyHz = data.value(QStringLiteral("freq")).toVariant().toULongLong();

    // Pick up callsign / grid if present (bulk_update freq_change
    // includes them).
    if (info.callsign.isEmpty()) {
        info.callsign = data.value(QStringLiteral("callsign")).toString();
    }
    if (info.gridSquare.isEmpty()) {
        info.gridSquare = data.value(QStringLiteral("grid_square")).toString();
    }
    info.lastUpdate = QDateTime::fromString(
        data.value(QStringLiteral("last_update")).toString(), Qt::ISODate);

    emit stationUpdated(sid, info);

    // NereusSDR dual-feed: also synthesize a DxSpot.
    emitSpotFromFreqChange(sid);
}

void FreeDVReporterClient::onRxReport(const QJsonObject& data)
{
    // From freedv-gui src/reporting/FreeDVReporter.cpp:505-553 [@77e793a]
    // (onFreeDVReporterReceiveReport_).
    QString sid = data.value(QStringLiteral("sid")).toString();
    if (sid.isEmpty()) return;

    if (m_stations.contains(sid)) {
        auto& info = m_stations[sid];
        info.lastRxCallsign = data.value(QStringLiteral("callsign")).toString();
        info.lastRxMode     = data.value(QStringLiteral("mode")).toString();
        // SNR may be int or real (freedv-gui :518-530); QJsonValue::toDouble
        // covers both.
        info.snrVal         = static_cast<int>(std::round(
                                  data.value(QStringLiteral("snr")).toDouble()));
        info.snrText        = QString::number(info.snrVal);
        info.lastRxDate     = QDateTime::currentDateTimeUtc();
        info.lastUpdate     = QDateTime::fromString(
            data.value(QStringLiteral("last_update")).toString(), Qt::ISODate);
        emit stationUpdated(sid, info);
    }

    // NereusSDR dual-feed: synthesize a DxSpot for the reported
    // transmitter.
    emitSpotFromRxReport(data);
}

void FreeDVReporterClient::onTxReport(const QJsonObject& data)
{
    // From freedv-gui src/reporting/FreeDVReporter.cpp:470-503 [@77e793a]
    // (onFreeDVReporterTransmitReport_).
    QString sid = data.value(QStringLiteral("sid")).toString();
    if (sid.isEmpty()) return;

    if (!m_stations.contains(sid)) return;
    auto& info = m_stations[sid];
    info.txMode       = data.value(QStringLiteral("mode")).toString();
    info.transmitting = data.value(QStringLiteral("transmitting")).toBool();
    if (info.transmitting) {
        info.lastTxDate = QDateTime::currentDateTimeUtc();
    }
    info.lastUpdate = QDateTime::fromString(
        data.value(QStringLiteral("last_update")).toString(), Qt::ISODate);
    emit stationUpdated(sid, info);
}

void FreeDVReporterClient::onRemoveConnection(const QJsonObject& data)
{
    // From freedv-gui src/reporting/FreeDVReporter.cpp:436-468 [@77e793a]
    // (onFreeDVReporterRemoveConnection_).
    QString sid = data.value(QStringLiteral("sid")).toString();
    if (sid.isEmpty()) return;
    m_stations.remove(sid);
    emit stationRemoved(sid);
}

void FreeDVReporterClient::onMessageUpdate(const QJsonObject& data)
{
    // From freedv-gui src/reporting/FreeDVReporter.cpp:585-607 [@77e793a]
    // (onFreeDVReporterMessageUpdate_).
    QString sid = data.value(QStringLiteral("sid")).toString();
    if (sid.isEmpty() || !m_stations.contains(sid)) return;
    auto& info = m_stations[sid];
    info.userMessage = data.value(QStringLiteral("message")).toString();
    info.lastUpdate  = QDateTime::fromString(
        data.value(QStringLiteral("last_update")).toString(), Qt::ISODate);
    emit stationUpdated(sid, info);
}

void FreeDVReporterClient::onConnectionSuccessful(const QJsonObject& data)
{
    // From freedv-gui src/reporting/FreeDVReporter.cpp:408-434 [@77e793a]
    // (onFreeDVReporterConnectionSuccessful_).
    Q_UNUSED(data);
    qCDebug(lcSpots) << "FreeDVReporterClient: connection_successful";
}

void FreeDVReporterClient::onBulkUpdate(const QJsonArray& pairs)
{
    // From freedv-gui src/reporting/FreeDVReporter.cpp:633-651 [@77e793a]
    // (onFreeDVReporterBulkUpdate_) and AetherSDR FreeDvClient.cpp:313-320
    // [@0cd4559]. Each pair is [event_name, payload]; fan out through
    // handleEvent so per-event handlers run uniformly.
    for (const auto& item : pairs) {
        QJsonArray pair = item.toArray();
        if (pair.size() < 2) continue;
        handleEvent(pair[0].toString(), pair[1].toObject());
    }
}

// ── Dual-feed spot synthesis ────────────────────────────────────────────
//
// Dual-feed: NereusSDR architectural decision per design doc Section 4
// Flow 2. freedv-gui's onFrequencyChange / onRxReport update the
// station map only. AetherSDR's FreeDvClient also synthesizes a DxSpot
// for the panadapter overlay. We do both: the station map drives
// FreeDVStationModel (Reporter dialog), the synthesized spot drives
// SpotModel (panadapter overlay).

void FreeDVReporterClient::emitSpotFromFreqChange(const QString& sid)
{
    // From AetherSDR src/core/FreeDvClient.cpp:243-295 [@0cd4559]
    if (!m_stations.contains(sid)) return;
    const auto& info = m_stations[sid];

    double freqMhz = static_cast<double>(info.frequencyHz) / 1.0e6;
    if (freqMhz <= 0.0 || info.callsign.isEmpty()) return;

    // Emit a spot for every station with a known frequency. This is
    // the primary spot source; rx_report only fires when one station
    // actually decodes another, which is rare. freq_change fires on
    // connect (bulk_update) and whenever a station retunes.
    DxSpot spot;
    spot.dxCall      = info.callsign;
    spot.spotterCall = info.callsign;  // self-reported
    spot.freqMhz     = freqMhz;
    spot.source      = QStringLiteral("FreeDV");
    spot.comment     = info.txMode.isEmpty() ? QStringLiteral("FreeDV") : info.txMode;
    if (!info.gridSquare.isEmpty()) {
        spot.comment += QStringLiteral(" ") + info.gridSquare;
    }
    spot.utcTime     = QDateTime::currentDateTimeUtc().time();
    spot.lifetimeSec = 0;

    QString freedvColor = AppSettings::instance()
        .value(QStringLiteral("FreeDvSpotColor"), QStringLiteral("#FF8C00")).toString();
    if (freedvColor.length() == 7) {
        freedvColor = QStringLiteral("#FF") + freedvColor.mid(1);
    }
    spot.color = freedvColor;

    QString logLine = QString("%1  %2  %3 MHz  %4")
        .arg(spot.utcTime.toString("HH:mm"), info.callsign,
             QString::number(freqMhz, 'f', 4), spot.comment);
    if (m_logFile.isOpen()) {
        m_logFile.write((logLine + "\n").toUtf8());
        m_logFile.flush();
    }
    emit rawLineReceived(logLine);
    emit spotReceived(spot);
}

void FreeDVReporterClient::emitSpotFromRxReport(const QJsonObject& data)
{
    // From AetherSDR src/core/FreeDvClient.cpp:322-370 [@0cd4559]
    QString sid = data.value(QStringLiteral("sid")).toString();

    // Look up the receiving station's frequency from our state map
    // (freedv-gui rx_report payload doesn't carry the receiver's freq;
    // it's the station's own state, captured from its earlier
    // freq_change).
    double freqMhz = 0.0;
    if (m_stations.contains(sid)) {
        freqMhz = static_cast<double>(m_stations[sid].frequencyHz) / 1.0e6;
    }
    if (freqMhz <= 0.0) {
        return;  // cannot spot without a frequency
    }

    QString receiverCall = data.value(QStringLiteral("receiver_callsign")).toString();
    QString txCall       = data.value(QStringLiteral("callsign")).toString();
    QString mode         = data.value(QStringLiteral("mode")).toString();
    double  snr          = data.value(QStringLiteral("snr")).toDouble();
    QString grid         = data.value(QStringLiteral("receiver_grid_square")).toString();

    if (txCall.isEmpty()) return;

    DxSpot spot;
    spot.dxCall      = txCall;
    spot.spotterCall = receiverCall;
    spot.freqMhz     = freqMhz;
    spot.snr         = static_cast<int>(std::round(snr));
    spot.source      = QStringLiteral("FreeDV");
    spot.comment     = mode;
    if (!grid.isEmpty()) {
        spot.comment += QStringLiteral(" ") + grid;
    }
    spot.utcTime     = QDateTime::currentDateTimeUtc().time();
    spot.lifetimeSec = 0;  // use source default from AppSettings

    QString freedvColor = AppSettings::instance()
        .value(QStringLiteral("FreeDvSpotColor"), QStringLiteral("#FF8C00")).toString();
    if (freedvColor.length() == 7) {
        freedvColor = QStringLiteral("#FF") + freedvColor.mid(1);
    }
    spot.color = freedvColor;

    QString logLine = QString("%1  %2 heard %3  %4 MHz  SNR %5 dB  %6")
        .arg(spot.utcTime.toString("HH:mm"), receiverCall, txCall,
             QString::number(freqMhz, 'f', 4),
             QString::number(snr, 'f', 1), mode);
    if (m_logFile.isOpen()) {
        m_logFile.write((logLine + "\n").toUtf8());
        m_logFile.flush();
    }
    emit rawLineReceived(logLine);
    emit spotReceived(spot);
}

} // namespace NereusSDR
