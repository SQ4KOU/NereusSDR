#pragma once
// no-port-check: NereusSDR-original. The Core's station TCI server as the
// mirrored, read-only `stationTci` object.

// SPDX-License-Identifier: GPL-3.0-or-later
//
// =================================================================
// src/models/StationTciModel.h  (NereusSDR)
// =================================================================
//
// NereusSDR-original; no upstream port. R-R3-48 / R-R3-22.
//
// What the Core's station TCI server is doing: whether the station's TCI
// switch is on, its port, whether it listens, and the station network
// address devices like the RF-Kit RF2K-S enter to reach it. The Core
// mirrors it as the read-only `stationTci` object (stationTciVersion 1);
// a window's TCI page shows "Also at the station: <address>, port <port>"
// from it. The switch itself changes only through the setStationTci
// command, never by writing this object.
//
// The wire contract: docs/architecture/2026-09-23-remote-accessory-control-v1.md.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-24  J.J. Boyd / KG4VCF  Created (R-R3-48, R-R3-22). AI-assisted
//                                    via Anthropic Claude Code.
// =================================================================

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVariant>

namespace NereusSDR {

class StationTciModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled NOTIFY stateChanged)
    Q_PROPERTY(int port READ port NOTIFY stateChanged)
    Q_PROPERTY(bool listening READ listening NOTIFY stateChanged)
    Q_PROPERTY(QString stationAddress READ stationAddress NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)

public:
    struct State {
        bool enabled{false};
        int port{0};
        bool listening{false};
        QString stationAddress;
        QString error;
        bool operator==(const State& other) const
        {
            return enabled == other.enabled && port == other.port
                && listening == other.listening && stationAddress == other.stationAddress
                && error == other.error;
        }
    };

    /// Why a window cannot write this object: the Core refuses every write.
    static QString readOnlyReason();

    explicit StationTciModel(QObject* parent = nullptr);

    bool enabled() const { return m_state.enabled; }
    int port() const { return m_state.port; }
    bool listening() const { return m_state.listening; }
    QString stationAddress() const { return m_state.stationAddress; }
    QString error() const { return m_state.error; }
    State state() const { return m_state; }

    /// The Core's controller (or a test): the whole state at once.
    void setState(const State& state);

    /// A remote window: one of the Core's values arriving.
    bool applyStationValue(const QByteArray& propertyName, const QVariant& value);

signals:
    void stateChanged();

private:
    State m_state;
};

} // namespace NereusSDR
