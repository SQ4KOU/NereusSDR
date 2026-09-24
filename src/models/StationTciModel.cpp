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
// =================================================================

#include "models/StationTciModel.h"

#include <QtGlobal>

namespace NereusSDR {

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
    } else {
        return false;
    }
    setState(next);
    return true;
}

} // namespace NereusSDR
