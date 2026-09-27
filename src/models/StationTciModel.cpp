// no-port-check: NereusSDR-original. See StationTciModel.h.

// SPDX-License-Identifier: GPL-3.0-or-later
//
// =================================================================
// src/models/StationTciModel.cpp  (NereusSDR)
// =================================================================
//
// Modification history (NereusSDR):
//   2026-09-24  J.J. Boyd / KG4VCF  Created (R-R3-48, R-R3-22). AI-assisted
//                                    via Anthropic Claude Code.
//   2026-09-27  J.J. Boyd / KG4VCF  Parity Task 23: the four options and
//                                    the apps. AI-assisted via Anthropic
//                                    Claude Code.
// =================================================================

#include "models/StationTciModel.h"

#include <QJsonArray>
#include <QtGlobal>

namespace NereusSDR {

QJsonObject StationTciClient::toFields() const
{
    return QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), name},
        {QStringLiteral("address"), address},
        {QStringLiteral("subscriptions"), QJsonArray::fromStringList(subscriptions)},
        {QStringLiteral("transmitting"), transmitting},
        {QStringLiteral("lastCommand"), lastCommand},
    };
}

std::optional<StationTciClient> StationTciClient::fromFields(const QString& id,
                                                             const QJsonObject& fields)
{
    if (!fields.value(QStringLiteral("name")).isString()
        || !fields.value(QStringLiteral("address")).isString()
        || !fields.value(QStringLiteral("subscriptions")).isArray()
        || !fields.value(QStringLiteral("transmitting")).isBool()
        || !fields.value(QStringLiteral("lastCommand")).isString()) {
        return std::nullopt;
    }
    StationTciClient client;
    client.id = id;
    client.name = fields.value(QStringLiteral("name")).toString();
    client.address = fields.value(QStringLiteral("address")).toString();
    for (const QJsonValue& value : fields.value(QStringLiteral("subscriptions")).toArray()) {
        if (!value.isString()) {
            return std::nullopt;
        }
        client.subscriptions.append(value.toString());
    }
    client.transmitting = fields.value(QStringLiteral("transmitting")).toBool();
    client.lastCommand = fields.value(QStringLiteral("lastCommand")).toString();
    return client;
}

QString StationTciModel::readOnlyReason()
{
    return QStringLiteral("The Core reports its TCI server here. Turn it on or off with "
                          "this app's TCI switch.");
}

StationTciModel::StationTciModel(QObject* parent)
    : QObject(parent)
{
}

void StationTciModel::setState(const State& state)
{
    if (state == m_state) {
        return;
    }
    m_state = state;
    emit stateChanged();
}

bool StationTciModel::applyStationValue(const QByteArray& propertyName, const QVariant& value)
{
    State next = m_state;
    if (propertyName == "enabled") {
        next.enabled = value.toBool();
    } else if (propertyName == "port") {
        next.port = qBound(0, value.toInt(), 65535);
    } else if (propertyName == "listening") {
        next.listening = value.toBool();
    } else if (propertyName == "stationAddress") {
        next.stationAddress = value.toString();
    } else if (propertyName == "error") {
        next.error = value.toString();
    } else if (propertyName == "emulateExpertSdr3") {
        next.emulateExpertSdr3 = value.toBool();
    } else if (propertyName == "emulateSunSdr2Pro") {
        next.emulateSunSdr2Pro = value.toBool();
    } else if (propertyName == "cwluBecomesCw") {
        next.cwluBecomesCw = value.toBool();
    } else if (propertyName == "sendInitialState") {
        next.sendInitialState = value.toBool();
    } else {
        return false;
    }
    setState(next);
    return true;
}

void StationTciModel::setClients(const QList<StationTciClient>& clients)
{
    if (clients == m_clients) {
        return;
    }
    m_clients = clients;
    emit clientsChanged();
}

} // namespace NereusSDR
