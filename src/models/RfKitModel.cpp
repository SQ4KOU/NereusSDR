// no-port-check: NereusSDR-original. See RfKitModel.h.

// SPDX-License-Identifier: GPL-3.0-or-later
//
// =================================================================
// src/models/RfKitModel.cpp  (NereusSDR)
// =================================================================
//
// Modification history (NereusSDR):
//   2026-09-23  J.J. Boyd / KG4VCF  Created (R-R3-47, R-R3-22). AI-assisted
//                                    via Anthropic Claude Code.
// =================================================================

#include "models/RfKitModel.h"

#include "core/Rf2ksConnection.h"

namespace NereusSDR {

QString RfKitModel::readOnlyReason()
{
    return QStringLiteral("The Core reports the RF-Kit amplifier's readings. They "
                          "cannot be changed from this app.");
}

RfKitModel::RfKitModel(QObject* parent)
    : QObject(parent)
{
}

void RfKitModel::bindConnection(Rf2ksConnection* connection)
{
    if (m_conn) {
        disconnect(m_conn, nullptr, this, nullptr);
    }
    m_conn = connection;
    if (!connection) {
        return;
    }
    connect(connection, &Rf2ksConnection::powerUpdated, this, &RfKitModel::applyPower);
    connect(connection, &Rf2ksConnection::operateModeUpdated,
            this, &RfKitModel::applyOperateMode);
    connect(connection, &Rf2ksConnection::infoUpdated, this, &RfKitModel::applyInfo);
    connect(connection, &Rf2ksConnection::connected, this, [this] {
        refreshFromConnection(ConnectionPhase::Connected, {});
    });
    connect(connection, &Rf2ksConnection::disconnected, this, [this] {
        const ConnectionPhase phase = m_connection.phase == ConnectionPhase::Error
            ? ConnectionPhase::Error : ConnectionPhase::Disconnected;
        refreshFromConnection(phase, m_connection.error);
    });
    connect(connection, &Rf2ksConnection::connectionFailed, this,
            [this](const QString& reason) {
        refreshFromConnection(ConnectionPhase::Error, reason);
    });
}

void RfKitModel::refreshFromConnection(ConnectionPhase phase, const QString& error)
{
    StationConnectionState next = m_connection;
    next.phase = phase;
    next.error = phase == ConnectionPhase::Connected ? QString() : error;
    if (m_conn) {
        next.configuredHost = m_conn->peerAddress();
        next.configuredPort = m_conn->peerPort();
        next.peerAddress = phase == ConnectionPhase::Connected ? m_conn->peerAddress()
                                                                : QString();
    }
    setStationConnectionState(next);
}

void RfKitModel::setAccessoryEnabled(bool enabled)
{
    m_enabled = enabled;
    StationConnectionState next = m_connection;
    if (!enabled && next.phase != ConnectionPhase::Connected) {
        next.phase = ConnectionPhase::Disabled;
        next.error.clear();
    } else if (enabled && next.phase == ConnectionPhase::Disabled) {
        next.phase = ConnectionPhase::Disconnected;
    }
    setStationConnectionState(next);
}

void RfKitModel::setStationConnectionState(const StationConnectionState& state)
{
    StationConnectionState next = state;
    if (!m_enabled && next.phase != ConnectionPhase::Connected) {
        next.phase = ConnectionPhase::Disabled;
    }
    // Readings are only live on a connection: off it, present says the
    // values are the last ones read. The values themselves stay.
    if (next.phase != ConnectionPhase::Connected && m_present) {
        m_present = false;
        emit statusChanged();
    }
    publishConnection(next);
}

void RfKitModel::publishConnection(const StationConnectionState& next)
{
    const bool changed = next.phase != m_connection.phase
        || next.configuredHost != m_connection.configuredHost
        || next.configuredPort != m_connection.configuredPort
        || next.error != m_connection.error
        || next.deviceModel != m_connection.deviceModel
        || next.deviceSerial != m_connection.deviceSerial
        || next.deviceVersion != m_connection.deviceVersion
        || next.deviceNickname != m_connection.deviceNickname;
    m_connection = next;
    if (changed) {
        emit stationConnectionChanged();
    }
}

void RfKitModel::applyPower(const RfKitPowerSnapshot& snapshot)
{
    const double forward = snapshot.forwardW;
    const double reflected = snapshot.reflectedW;
    const double swr = static_cast<double>(snapshot.swr);
    const double temperature = static_cast<double>(snapshot.temperatureC);
    const double voltage = static_cast<double>(snapshot.voltageV);
    const double current = static_cast<double>(snapshot.currentA);
    const bool changed = !m_present || forward != m_forwardPowerW
        || reflected != m_reflectedPowerW || swr != m_swr
        || temperature != m_temperatureC || voltage != m_voltageV
        || current != m_currentA;
    m_present = true;
    m_forwardPowerW = forward;
    m_reflectedPowerW = reflected;
    m_swr = swr;
    m_temperatureC = temperature;
    m_voltageV = voltage;
    m_currentA = current;
    if (changed) {
        emit statusChanged();
    }
}

void RfKitModel::applyOperateMode(const QString& mode)
{
    const bool operate = mode == QStringLiteral("OPERATE");
    if (operate != m_operate) {
        m_operate = operate;
        emit statusChanged();
    }
}

void RfKitModel::applyInfo(const QString& device, const QString& softwareVersion,
                           const QString& customName)
{
    StationConnectionState next = m_connection;
    next.deviceModel = device;
    next.deviceVersion = softwareVersion;
    next.deviceNickname = customName;
    publishConnection(next);
}

bool RfKitModel::applyStationValue(const QByteArray& propertyName, const QVariant& value)
{
    StationConnectionState connection = m_connection;
    if (propertyName == "connectionPhase") {
        connection.phase = static_cast<ConnectionPhase>(value.toInt());
    } else if (propertyName == "configuredHost") {
        connection.configuredHost = value.toString();
    } else if (propertyName == "configuredPort") {
        connection.configuredPort = static_cast<quint16>(qBound(0, value.toInt(), 65535));
    } else if (propertyName == "connectionError") {
        connection.error = value.toString();
    } else if (propertyName == "deviceModel") {
        connection.deviceModel = value.toString();
    } else if (propertyName == "deviceSerial") {
        connection.deviceSerial = value.toString();
    } else if (propertyName == "deviceVersion") {
        connection.deviceVersion = value.toString();
    } else if (propertyName == "deviceNickname") {
        connection.deviceNickname = value.toString();
    } else {
        // A status value: a plain state apply, never a request to the amp.
        bool* flag = nullptr;
        double* number = nullptr;
        if (propertyName == "present") { flag = &m_present; }
        else if (propertyName == "operate") { flag = &m_operate; }
        else if (propertyName == "forwardPowerW") { number = &m_forwardPowerW; }
        else if (propertyName == "reflectedPowerW") { number = &m_reflectedPowerW; }
        else if (propertyName == "swr") { number = &m_swr; }
        else if (propertyName == "temperatureC") { number = &m_temperatureC; }
        else if (propertyName == "voltageV") { number = &m_voltageV; }
        else if (propertyName == "currentA") { number = &m_currentA; }
        else { return false; }
        bool changed = false;
        if (flag) {
            changed = *flag != value.toBool();
            *flag = value.toBool();
        } else {
            changed = *number != value.toDouble();
            *number = value.toDouble();
        }
        if (changed) {
            emit statusChanged();
        }
        return true;
    }
    publishConnection(connection);
    return true;
}

} // namespace NereusSDR
