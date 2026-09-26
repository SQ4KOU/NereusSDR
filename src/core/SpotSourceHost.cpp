// no-port-check: NereusSDR-original. Starts and stops the spot sources.

// SPDX-License-Identifier: GPL-3.0-or-later
//
// =================================================================
// src/core/SpotSourceHost.cpp  (NereusSDR)
// =================================================================
//
// NereusSDR-original; no upstream port. See SpotSourceHost.h.
//
// The start calls and the settings they read are the ones the local
// window used before (RadioModel::restoreSpotClientAutoStartState and
// MainWindow::openSpotHub's per-tab wiring), moved here unchanged, so a
// window running its own radio behaves exactly as it did.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-26  J.J. Boyd / KG4VCF  Created (parity Task 19, R-IOS-25,
//                                    R-R3-49). AI-assisted via Anthropic
//                                    Claude Code.
// =================================================================

#include "core/SpotSourceHost.h"

#include "core/AppSettings.h"
#include "core/DxClusterClient.h"
#include "core/DxccColorProvider.h"
#include "core/LogCategories.h"
#include "core/PotaClient.h"
#include "core/PskReporterClient.h"
#include "core/SpotCollectorClient.h"
#include "core/WsjtxClient.h"
#include "models/Band.h"
#include "models/SpotModel.h"

#include <QDateTime>
#include <QTimeZone>
#include <cmath>

namespace NereusSDR {

const QString SpotSourceHost::kDxCluster = QStringLiteral("dxCluster");
const QString SpotSourceHost::kRbn = QStringLiteral("rbn");
const QString SpotSourceHost::kPota = QStringLiteral("pota");
const QString SpotSourceHost::kPskReporter = QStringLiteral("pskReporter");
const QString SpotSourceHost::kWsjtx = QStringLiteral("wsjtx");
const QString SpotSourceHost::kSpotCollector = QStringLiteral("spotCollector");

const QString SpotSourceHost::kOff = QStringLiteral("off");
const QString SpotSourceHost::kConnecting = QStringLiteral("connecting");
const QString SpotSourceHost::kConnected = QStringLiteral("connected");
const QString SpotSourceHost::kError = QStringLiteral("error");

namespace {

QString versionString()
{
    return QStringLiteral("NereusSDR ") + QStringLiteral(NEREUSSDR_VERSION);
}

bool settingIsTrue(const QString& key)
{
    return AppSettings::instance().value(key, QStringLiteral("False")).toString()
        == QStringLiteral("True");
}

// The identity fall-back chain the Spot Hub's Settings tab set up: a
// source's own key, then the canonical User/Callsign.
QString resolveCall(const QString& perSourceKey)
{
    auto& s = AppSettings::instance();
    QString v = s.value(perSourceKey).toString();
    if (v.isEmpty()) {
        v = s.value(QStringLiteral("User/Callsign")).toString();
    }
    return v;
}

QString resolveGrid(const QString& perSourceKey)
{
    auto& s = AppSettings::instance();
    QString v = s.value(perSourceKey).toString();
    if (v.isEmpty()) {
        v = s.value(QStringLiteral("User/GridSquare")).toString();
    }
    return v;
}

} // namespace

QStringList SpotSourceHost::stationSources()
{
    return {kDxCluster, kRbn, kPota, kPskReporter};
}

QStringList SpotSourceHost::windowSources()
{
    return {kWsjtx, kSpotCollector};
}

bool SpotSourceHost::isStationSource(const QString& source)
{
    return stationSources().contains(source);
}

bool SpotSourceHost::isKnownSource(const QString& source)
{
    return isStationSource(source) || windowSources().contains(source);
}

QString SpotSourceHost::consoleStream(const QString& source)
{
    return QStringLiteral("spotConsole:") + source;
}

QJsonObject SpotSourceHost::spotRecordFields(const SpotData& spot, const DxccColorProvider* dxcc)
{
    // The heard-on frequency first, as the panadapter overlay places it.
    const double mhz = spot.rxFreqMhz > 0.0 ? spot.rxFreqMhz : spot.txFreqMhz;
    const qint64 hz = static_cast<qint64>(std::llround(mhz * 1.0e6));
    const QDateTime when = spot.timestamp.isValid()
        ? spot.timestamp
        : QDateTime::fromMSecsSinceEpoch(spot.addedMs, QTimeZone::UTC);
    QString colour;
    int priority = 0;
    if (dxcc != nullptr && dxcc->isEnabled() && !spot.callsign.isEmpty() && mhz > 0.0) {
        switch (dxcc->statusForSpot(spot.callsign, mhz, spot.mode)) {
        case DxccStatus::NewDxcc: priority = 4; break;
        case DxccStatus::NewBand: priority = 3; break;
        case DxccStatus::NewMode: priority = 2; break;
        case DxccStatus::Worked:  priority = 1; break;
        case DxccStatus::Unknown: priority = 0; break;
        }
        const QColor c = dxcc->colorForSpot(spot.callsign, mhz, spot.mode);
        if (c.isValid()) {
            colour = c.name();
        }
    }
    return QJsonObject{
        {QStringLiteral("timeUtc"), when.toUTC().toString(Qt::ISODate)},
        {QStringLiteral("frequencyHz"), static_cast<double>(hz)},
        {QStringLiteral("call"), spot.callsign},
        {QStringLiteral("mode"), spot.mode},
        {QStringLiteral("source"), spot.source},
        {QStringLiteral("spotter"), spot.spotterCallsign},
        {QStringLiteral("comment"), spot.comment},
        {QStringLiteral("band"), static_cast<int>(bandFromFrequency(static_cast<double>(hz)))},
        {QStringLiteral("dxccColour"), colour},
        {QStringLiteral("dxccPriority"), priority},
    };
}

QString SpotSourceHost::readOnlyReason()
{
    return QStringLiteral("The Core's spot sources change only from their Connect and Start "
                          "buttons.");
}

SpotSourceHost::SpotSourceHost(DxClusterClient* dxCluster, DxClusterClient* rbn,
                               WsjtxClient* wsjtx, SpotCollectorClient* spotCollector,
                               PotaClient* pota, PskReporterClient* pskReporter,
                               SpotModel* spots, QObject* parent)
    : QObject(parent)
    , m_dxCluster(dxCluster)
    , m_rbn(rbn)
    , m_wsjtx(wsjtx)
    , m_spotCollector(spotCollector)
    , m_pota(pota)
    , m_pskReporter(pskReporter)
    , m_spots(spots)
{
    // Follow each client, so the state is what the client does, whoever
    // started it.
    const auto followCluster = [this](DxClusterClient* client, const QString& source) {
        if (client == nullptr) {
            return;
        }
        connect(client, &DxClusterClient::connected, this, [this, source]() {
            setSource(source, kConnected);
        });
        connect(client, &DxClusterClient::disconnected, this, [this, source]() {
            setSource(source, kOff);
        });
        connect(client, &DxClusterClient::connectionError, this,
                [this, source](const QString& error) { setSource(source, kError, error); });
        connect(client, &DxClusterClient::rawLineReceived, this,
                [this, source](const QString& line) { emit consoleLine(source, line); });
    };
    followCluster(m_dxCluster, kDxCluster);
    followCluster(m_rbn, kRbn);
    if (m_wsjtx) {
        connect(m_wsjtx, &WsjtxClient::listening, this, [this]() { setSource(kWsjtx, kConnected); });
        connect(m_wsjtx, &WsjtxClient::stopped, this, [this]() { setSource(kWsjtx, kOff); });
        connect(m_wsjtx, &WsjtxClient::rawLineReceived, this,
                [this](const QString& line) { emit consoleLine(kWsjtx, line); });
    }
    if (m_spotCollector) {
        connect(m_spotCollector, &SpotCollectorClient::listening, this,
                [this]() { setSource(kSpotCollector, kConnected); });
        connect(m_spotCollector, &SpotCollectorClient::stopped, this,
                [this]() { setSource(kSpotCollector, kOff); });
        connect(m_spotCollector, &SpotCollectorClient::rawLineReceived, this,
                [this](const QString& line) { emit consoleLine(kSpotCollector, line); });
    }
    if (m_pota) {
        connect(m_pota, &PotaClient::started, this,
                [this]() { setSource(kPota, kConnected, QStringLiteral("Polling api.pota.app")); });
        connect(m_pota, &PotaClient::stopped, this, [this]() { setSource(kPota, kOff); });
        connect(m_pota, &PotaClient::pollError, this,
                [this](const QString& error) { setSource(kPota, kError, error); });
        connect(m_pota, &PotaClient::rawLineReceived, this,
                [this](const QString& line) { emit consoleLine(kPota, line); });
    }
    if (m_pskReporter) {
        connect(m_pskReporter, &PskReporterClient::errorOccurred, this,
                [this](const QString& error) { setSource(kPskReporter, kError, error); });
    }
}

void SpotSourceHost::restoreAutoStart(Placement placement)
{
    const bool station = placement != Placement::WindowSources;
    const bool window = placement != Placement::StationSources;
    auto& s = AppSettings::instance();

    // DxCluster
    if (station && m_dxCluster && settingIsTrue(QStringLiteral("DxClusterAutoConnect"))) {
        setSource(kDxCluster, kConnecting);
        m_dxCluster->connectToCluster(
            s.value(QStringLiteral("DxClusterHost"), QStringLiteral("dxc.nc7j.com")).toString(),
            static_cast<quint16>(s.value(QStringLiteral("DxClusterPort"), 7300).toInt()),
            resolveCall(QStringLiteral("DxClusterCallsign")));
    }

    // RBN (same DxClusterClient class, different keys / default host).
    if (station && m_rbn && settingIsTrue(QStringLiteral("RbnAutoConnect"))) {
        setSource(kRbn, kConnecting);
        m_rbn->connectToCluster(
            s.value(QStringLiteral("RbnHost"),
                    QStringLiteral("telnet.reversebeacon.net")).toString(),
            static_cast<quint16>(s.value(QStringLiteral("RbnPort"), 7000).toInt()),
            resolveCall(QStringLiteral("RbnCallsign")));
    }

    // WSJT-X (UDP bind on the configured address / port).
    if (window && m_wsjtx && settingIsTrue(QStringLiteral("WsjtxAutoStart"))) {
        m_wsjtx->startListening(
            s.value(QStringLiteral("WsjtxAddress"), QStringLiteral("224.0.0.1")).toString(),
            static_cast<quint16>(s.value(QStringLiteral("WsjtxPort"), 2237).toInt()));
    }

    // SpotCollector (UDP bind).
    if (window && m_spotCollector && settingIsTrue(QStringLiteral("SpotCollectorAutoStart"))) {
        m_spotCollector->startListening(
            static_cast<quint16>(s.value(QStringLiteral("SpotCollectorPort"), 9999).toInt()));
    }

    // POTA (HTTPS poll loop).
    if (station && m_pota && settingIsTrue(QStringLiteral("PotaAutoStart"))) {
        m_pota->startPolling(s.value(QStringLiteral("PotaPollInterval"), 30).toInt());
    }

    // PSK Reporter: send-only. Identity refreshed from the User/* fall-back
    // chain; when PskReporterAutoStart is on, the client's own reporting
    // interval is armed, as RadioModel's restore did.
    if (station && m_pskReporter) {
        const QString pskCall = resolveCall(QStringLiteral("PskReporter/Callsign"));
        const QString pskGrid = resolveGrid(QStringLiteral("PskReporter/GridSquare"));
        if (!pskCall.isEmpty()) {
            m_pskReporter->setIdentity(pskCall, pskGrid, versionString());
            if (settingIsTrue(QStringLiteral("PskReporterAutoStart"))) {
                m_pskReporter->setAutoSendIntervalSec(PskReporterClient::kReportingIntervalSec);
                setSource(kPskReporter, kConnected,
                          QStringLiteral("Reporting every 5 minutes"));
                qCInfo(lcDsp) << "PskReporter: auto-start armed (5-min interval)"
                              << "callsign=" << pskCall;
            }
        }
    }
}

bool SpotSourceHost::connectSource(const QString& source, QString* reason)
{
    const auto refuse = [reason](const QString& why) {
        if (reason != nullptr) {
            *reason = why;
        }
        return false;
    };
    if (!isKnownSource(source)) {
        return refuse(QStringLiteral("The Core does not run that spot source."));
    }
    if (!isStationSource(source)) {
        return refuse(QStringLiteral("WSJT-X and SpotCollector listen on each computer, not on "
                                     "the Core."));
    }
    auto& s = AppSettings::instance();
    if (source == kDxCluster || source == kRbn) {
        const bool cluster = source == kDxCluster;
        DxClusterClient* client = cluster ? m_dxCluster.data() : m_rbn.data();
        if (client == nullptr) {
            return refuse(QStringLiteral("The Core does not run that spot source."));
        }
        const QString call = resolveCall(cluster ? QStringLiteral("DxClusterCallsign")
                                                 : QStringLiteral("RbnCallsign"));
        if (call.isEmpty()) {
            return refuse(QStringLiteral("Enter your callsign in Spot Hub first."));
        }
        if (client->isConnected()) {
            return true;
        }
        const QString host = cluster
            ? s.value(QStringLiteral("DxClusterHost"), QStringLiteral("dxc.nc7j.com")).toString()
            : s.value(QStringLiteral("RbnHost"),
                      QStringLiteral("telnet.reversebeacon.net")).toString();
        const int port = cluster ? s.value(QStringLiteral("DxClusterPort"), 7300).toInt()
                                 : s.value(QStringLiteral("RbnPort"), 7000).toInt();
        setSource(source, kConnecting);
        client->connectToCluster(host, static_cast<quint16>(port), call);
        return true;
    }
    if (source == kPota) {
        if (m_pota == nullptr) {
            return refuse(QStringLiteral("The Core does not run that spot source."));
        }
        m_pota->startPolling(s.value(QStringLiteral("PotaPollInterval"), 30).toInt());
        return true;
    }
    // PSK Reporter.
    if (m_pskReporter == nullptr) {
        return refuse(QStringLiteral("The Core does not run that spot source."));
    }
    const QString call = resolveCall(QStringLiteral("PskReporter/Callsign"));
    const QString grid = resolveGrid(QStringLiteral("PskReporter/GridSquare"));
    if (call.isEmpty() || grid.isEmpty()) {
        return refuse(QStringLiteral("Enter your callsign and grid square in Spot Hub first."));
    }
    startPskReporterWith(call, grid);
    return true;
}

bool SpotSourceHost::disconnectSource(const QString& source, QString* reason)
{
    if (!isStationSource(source)) {
        if (reason != nullptr) {
            *reason = isKnownSource(source)
                ? QStringLiteral("WSJT-X and SpotCollector listen on each computer, not on the "
                                 "Core.")
                : QStringLiteral("The Core does not run that spot source.");
        }
        return false;
    }
    if (source == kDxCluster && m_dxCluster) {
        m_dxCluster->disconnect();
        setSource(kDxCluster, kOff);
    } else if (source == kRbn && m_rbn) {
        m_rbn->disconnect();
        setSource(kRbn, kOff);
    } else if (source == kPota && m_pota) {
        m_pota->stopPolling();
        setSource(kPota, kOff);
    } else if (source == kPskReporter && m_pskReporter) {
        m_pskReporter->setAutoSendIntervalSec(0);
        setSource(kPskReporter, kOff);
    }
    return true;
}

bool SpotSourceHost::sendCommand(const QString& source, const QString& text, QString* reason)
{
    const auto refuse = [reason](const QString& why) {
        if (reason != nullptr) {
            *reason = why;
        }
        return false;
    };
    DxClusterClient* client = source == kDxCluster ? m_dxCluster.data()
        : source == kRbn                            ? m_rbn.data()
                                                    : nullptr;
    if (client == nullptr) {
        return refuse(QStringLiteral("Only the DX cluster and the Reverse Beacon Network take "
                                     "typed commands."));
    }
    const QString command = text.trimmed();
    if (command.isEmpty()) {
        return refuse(QStringLiteral("Type a command first."));
    }
    if (!client->isConnected()) {
        return refuse(source == kDxCluster
                          ? QStringLiteral("The DX cluster is not connected.")
                          : QStringLiteral("The Reverse Beacon Network is not connected."));
    }
    client->sendCommand(command);
    // What the local console shows for a typed command, for every window.
    emit consoleLine(source, QStringLiteral("> ") + command);
    return true;
}

void SpotSourceHost::clearAll()
{
    if (m_spots) {
        m_spots->clear();
    }
}

QString SpotSourceHost::state(const QString& source) const
{
    const bool station = forwardsStationSources() && isStationSource(source);
    const auto& table = station ? m_station : m_local;
    const auto it = table.constFind(source);
    return it == table.cend() || it->state.isEmpty() ? kOff : it->state;
}

QString SpotSourceHost::text(const QString& source) const
{
    const bool station = forwardsStationSources() && isStationSource(source);
    const auto& table = station ? m_station : m_local;
    return table.value(source).text;
}

bool SpotSourceHost::isRunning(const QString& source) const
{
    const QString s = state(source);
    return s == kConnected || s == kConnecting;
}

void SpotSourceHost::setStationForwarder(StationForwarder forwarder)
{
    m_forwarder = std::move(forwarder);
    emit sourcesChanged();
    for (const QString& source : stationSources()) {
        emit sourceChanged(source);
    }
}

bool SpotSourceHost::applyStationValue(const QByteArray& propertyName, const QVariant& value)
{
    if (value.typeId() != QMetaType::QString) {
        return false;
    }
    for (const QString& source : stationSources()) {
        const QByteArray stateName = source.toUtf8() + "State";
        const QByteArray textName = source.toUtf8() + "Text";
        if (propertyName != stateName && propertyName != textName) {
            continue;
        }
        SourceState& entry = m_station[source];
        QString& field = propertyName == stateName ? entry.state : entry.text;
        if (field != value.toString()) {
            field = value.toString();
            emit sourcesChanged();
            emit sourceChanged(source);
        }
        return true;
    }
    return false;
}

void SpotSourceHost::clearStationValues()
{
    if (m_station.isEmpty()) {
        return;
    }
    m_station.clear();
    emit sourcesChanged();
    for (const QString& source : stationSources()) {
        emit sourceChanged(source);
    }
}

void SpotSourceHost::appendStationConsole(const QString& source, const QStringList& lines)
{
    for (const QString& line : lines) {
        emit consoleLine(source, line);
    }
}

void SpotSourceHost::reportStationRefusal(const QString& source, const QString& reason)
{
    emit sourceRefused(source, reason);
}

// ── The Spot Hub's buttons ───────────────────────────────────────────────

void SpotSourceHost::connectCluster(const QString& host, quint16 port, const QString& callsign)
{
    if (forward("spots.connect", kDxCluster) || !m_dxCluster) {
        return;
    }
    setSource(kDxCluster, kConnecting);
    m_dxCluster->connectToCluster(host, port, callsign);
}

void SpotSourceHost::disconnectCluster()
{
    if (forward("spots.disconnect", kDxCluster) || !m_dxCluster) {
        return;
    }
    m_dxCluster->disconnect();
}

void SpotSourceHost::connectRbn(const QString& host, quint16 port, const QString& callsign)
{
    if (forward("spots.connect", kRbn) || !m_rbn) {
        return;
    }
    setSource(kRbn, kConnecting);
    m_rbn->connectToCluster(host, port, callsign);
}

void SpotSourceHost::disconnectRbn()
{
    if (forward("spots.disconnect", kRbn) || !m_rbn) {
        return;
    }
    m_rbn->disconnect();
}

void SpotSourceHost::startWsjtx(const QString& address, quint16 port)
{
    if (m_wsjtx) {
        m_wsjtx->startListening(address, port);
    }
}

void SpotSourceHost::stopWsjtx()
{
    if (m_wsjtx) {
        m_wsjtx->stopListening();
    }
}

void SpotSourceHost::startSpotCollector(quint16 port)
{
    if (m_spotCollector) {
        m_spotCollector->startListening(port);
    }
}

void SpotSourceHost::stopSpotCollector()
{
    if (m_spotCollector) {
        m_spotCollector->stopListening();
    }
}

void SpotSourceHost::startPota(int intervalSec)
{
    if (forward("spots.connect", kPota) || !m_pota) {
        return;
    }
    m_pota->startPolling(intervalSec);
}

void SpotSourceHost::stopPota()
{
    if (forward("spots.disconnect", kPota) || !m_pota) {
        return;
    }
    m_pota->stopPolling();
}

void SpotSourceHost::startPskReporter(const QString& callsign, const QString& gridSquare)
{
    if (forward("spots.connect", kPskReporter) || !m_pskReporter) {
        return;
    }
    startPskReporterWith(callsign, gridSquare);
}

void SpotSourceHost::stopPskReporter()
{
    if (forward("spots.disconnect", kPskReporter) || !m_pskReporter) {
        return;
    }
    m_pskReporter->setAutoSendIntervalSec(0);
    setSource(kPskReporter, kOff);
}

void SpotSourceHost::typeCommand(const QString& source, const QString& text)
{
    if (forward("spots.sendCommand", source, text)) {
        return;
    }
    QString reason;
    if (!sendCommand(source, text, &reason)) {
        emit sourceRefused(source, reason);
    }
}

void SpotSourceHost::clearAllSpots()
{
    if (m_forwarder) {
        QString reason;
        if (!m_forwarder("spots.clearAll", QString(), QString(), &reason)) {
            emit sourceRefused(QString(), reason);
        }
    }
    clearAll();
}

// ── Private ──────────────────────────────────────────────────────────────

void SpotSourceHost::setSource(const QString& source, const QString& state, const QString& text)
{
    SourceState& entry = m_local[source];
    const QString current = entry.state.isEmpty() ? kOff : entry.state;
    if (current == state && entry.text == text) {
        entry.state = state;
        return;
    }
    entry.state = state;
    entry.text = text;
    emit sourcesChanged();
    emit sourceChanged(source);
}

bool SpotSourceHost::forward(const QByteArray& verb, const QString& source, const QString& text)
{
    if (!m_forwarder || !isStationSource(source)) {
        return false;
    }
    QString reason;
    if (!m_forwarder(verb, source, text, &reason)) {
        emit sourceRefused(source, reason);
    }
    return true;
}

void SpotSourceHost::startPskReporterWith(const QString& callsign, const QString& gridSquare)
{
    // What the Spot Hub's Start did in MainWindow: the freshly checked
    // identity reaches the client before its reporting interval is armed,
    // so no report goes out with an empty receiver.
    m_pskReporter->setIdentity(callsign, gridSquare, versionString());
    m_pskReporter->setAutoSendIntervalSec(PskReporterClient::kReportingIntervalSec);
    setSource(kPskReporter, kConnected, QStringLiteral("Reporting every 5 minutes"));
}

} // namespace NereusSDR
