// no-port-check: NereusSDR-original. R-R3-47 / R-R3-48 / R-R3-22 station-owned RF2K-S.
// Structure from StationPgxlController.cpp. J.J. Boyd (KG4VCF), September
// 2026; AI-assisted via Anthropic Claude Code.
#include "core/StationRfKitController.h"

namespace NereusSDR {
using Phase = RfKitModel::ConnectionPhase;

namespace {
// The amp's word for TCI on /operational-interface (design doc
// 2026-05-24-rfkit-rf2ks-applet-design.md section 6.1).
const QString kTciInterface = QStringLiteral("TCI");
} // namespace

StationRfKitController::StationRfKitController(Rf2ksConnection* connection,
                                               RfKitModel* model, QObject* parent)
    : QObject(parent), m_connection(connection), m_model(model)
{
    connection->setIdentityAdmissionRequired(true);
    if (model) {
        // This controller, not the raw connection signals, reports phases.
        model->setConnectionStateOwnedByController(true);
    }
    connect(connection, &Rf2ksConnection::connected, this, [this] {
        if (!m_running) { return; }
        m_tciModeRequested = false;
        publish(Phase::Connected);
        maybeRequestTciMode();
    });
    connect(connection, &Rf2ksConnection::disconnected, this, [this] {
        if (!m_running) { return; }
        m_tciModeRequested = false;
        publish(m_connection && m_connection->reconnectPending() ? Phase::Retrying
                                                                 : Phase::Disconnected);
    });
    connect(connection, &Rf2ksConnection::reconnectScheduled, this, [this](int, int) {
        if (!m_running) { return; }
        publish(Phase::Retrying);
    });
    connect(connection, &Rf2ksConnection::connectionFailed, this,
            [this](const QString& reason) {
        if (!m_running) { return; }
        publish(Phase::Error, reason);
    });
    connect(connection, &Rf2ksConnection::operationalInterfaceUpdated, this,
            [this](const QString&, const QString&) { maybeRequestTciMode(); });
}

StationRfKitController::~StationRfKitController() = default;

void StationRfKitController::publish(Phase phase, const QString& error)
{
    if (!m_model) { return; }
    RfKitModel::StationConnectionState next = m_model->stationConnectionState();
    next.phase = phase;
    next.error = error;
    next.configuredHost = m_host;
    next.configuredPort = m_port;
    next.peerAddress = phase == Phase::Connected ? m_host : QString();
    m_model->setStationConnectionState(next);
}

void StationRfKitController::resetScope(const QString& host, quint16 port, bool enabled)
{
    const auto generation = ++m_generation;
    m_running = false;
    m_tciModeRequested = false;
    QPointer<StationRfKitController> self(this);
    if (m_connection) { m_connection->disconnect(); }
    if (!self || m_generation != generation) { return; }
    m_host = host;
    m_port = port;
    if (m_model) {
        RfKitModel::StationConnectionState cleared;
        cleared.configuredHost = host;
        cleared.configuredPort = port;
        cleared.phase = enabled ? Phase::Disconnected : Phase::Disabled;
        m_model->setStationConnectionState(cleared);
    }
}

void StationRfKitController::start(const QString& host, quint16 port)
{
    cancel();
    const auto generation = ++m_generation;
    m_running = true;
    m_host = host;
    m_port = port;
    if (m_model) {
        // A new address: the previous amp's identity no longer applies.
        RfKitModel::StationConnectionState next;
        next.configuredHost = host;
        next.configuredPort = port;
        next.phase = Phase::Connecting;
        m_model->setStationConnectionState(next);
    }
    QPointer<StationRfKitController> self(this);
    if (self && m_generation == generation && m_running && m_connection) {
        m_connection->connectToAmp(host, port);
    }
}

void StationRfKitController::cancel(bool disabled)
{
    ++m_generation;
    m_running = false;
    m_tciModeRequested = false;
    QPointer<StationRfKitController> self(this);
    if (m_connection) { m_connection->disconnect(); }
    if (!self) { return; }
    if (m_model) {
        RfKitModel::StationConnectionState next;
        next.configuredHost = m_host;
        next.configuredPort = m_port;
        next.phase = disabled ? Phase::Disabled : Phase::Disconnected;
        m_model->setStationConnectionState(next);
    }
}

void StationRfKitController::setBandFollowWanted(bool wanted)
{
    if (wanted == m_bandFollowWanted) { return; }
    m_bandFollowWanted = wanted;
    maybeRequestTciMode();
}

void StationRfKitController::maybeRequestTciMode()
{
    // Only an admitted amp, only once its interface is known, only once per
    // connection, and only while the station's TCI server is on.
    if (!m_running || !m_bandFollowWanted || m_tciModeRequested || !m_connection
        || !m_connection->isConnected()) {
        return;
    }
    const QString current = m_connection->operationalInterface();
    if (current.isEmpty() || current == kTciInterface) {
        return;
    }
    m_tciModeRequested = true;
    m_connection->setOperationalInterface(kTciInterface);
}

} // namespace NereusSDR
