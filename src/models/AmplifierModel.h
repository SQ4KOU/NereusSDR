#pragma once
// no-port-check: NereusSDR-original. The Core's Power Genius XL status as
// the mirrored, read-only `amplifier` object.

// SPDX-License-Identifier: GPL-3.0-or-later
//
// =================================================================
// src/models/AmplifierModel.h  (NereusSDR)
// =================================================================
//
// NereusSDR-original; no upstream port. R-R3-47 / R-R3-22.
//
// What the Core knows about its Power Genius XL: the Tuner Genius's
// connection-state shape (phase, configured address, error, identity) plus
// the amp's state and meters in plain units. The Core mirrors it as the
// read-only `amplifier` object (remotePgxlControlVersion 1); a local window
// reads the same object in-process. Every value comes from the amp through
// PgxlConnection and the one gauge conversion, applyPgxlStatus().
//
// Bound (the Core, a local window) it follows a PgxlConnection. Unbound (a
// remote window) it only holds the Core's values as they arrive through
// applyStationValue(); it never opens a socket or sends a command.
//
// The wire contract, units and fixed enum values:
// docs/architecture/2026-09-23-remote-accessory-control-v1.md.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-23  J.J. Boyd / KG4VCF  Created (R-R3-47, R-R3-22). AI-assisted
//                                    via Anthropic Claude Code.
// =================================================================

#include "core/PgxlStatusGauges.h"
#include "models/TunerModel.h"

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariant>

namespace NereusSDR {

class PgxlConnection;

class AmplifierModel : public QObject {
    Q_OBJECT
    // The Tuner Genius's connection-state shape (TunerModel), same enum.
    Q_PROPERTY(NereusSDR::TunerModel::ConnectionPhase connectionPhase READ connectionPhase NOTIFY stationConnectionChanged)
    Q_PROPERTY(QString configuredHost READ configuredHost NOTIFY stationConnectionChanged)
    Q_PROPERTY(int configuredPort READ configuredPort NOTIFY stationConnectionChanged)
    Q_PROPERTY(QString connectionError READ connectionError NOTIFY stationConnectionChanged)
    Q_PROPERTY(QString deviceModel READ deviceModel NOTIFY stationConnectionChanged)
    Q_PROPERTY(QString deviceSerial READ deviceSerial NOTIFY stationConnectionChanged)
    Q_PROPERTY(QString deviceVersion READ deviceVersion NOTIFY stationConnectionChanged)
    Q_PROPERTY(QString deviceNickname READ deviceNickname NOTIFY stationConnectionChanged)
    // The amp's state and meters.
    Q_PROPERTY(bool present READ present NOTIFY statusChanged)
    Q_PROPERTY(NereusSDR::AmplifierModel::State state READ state NOTIFY statusChanged)
    Q_PROPERTY(QString deviceState READ deviceState NOTIFY statusChanged)
    Q_PROPERTY(bool operate READ operate NOTIFY statusChanged)
    Q_PROPERTY(bool transmitting READ transmitting NOTIFY statusChanged)
    Q_PROPERTY(double forwardPowerW READ forwardPowerW NOTIFY statusChanged)
    Q_PROPERTY(double swr READ swr NOTIFY statusChanged)
    Q_PROPERTY(double temperatureC READ temperatureC NOTIFY statusChanged)
    Q_PROPERTY(double mainsVoltageV READ mainsVoltageV NOTIFY statusChanged)
    Q_PROPERTY(double drainCurrentA READ drainCurrentA NOTIFY statusChanged)
    Q_PROPERTY(QString efficiencyText READ efficiencyText NOTIFY statusChanged)

public:
    /// The amp's state. Wire values are fixed; new ones are only appended.
    enum class State {
        Unknown = 0,    ///< no state yet, or a word this build does not know
        PowerUp = 1,    ///< POWERUP
        Standby = 2,    ///< STANDBY
        Idle = 3,       ///< IDLE: operating, ready to transmit
        Operate = 4,    ///< OPERATE
        TransmitA = 5,  ///< TRANSMIT_A: keyed
        TransmitB = 6,  ///< TRANSMIT_B: keyed
        Fault = 7,      ///< any FAULT... word
    };
    Q_ENUM(State)

    using ConnectionPhase = TunerModel::ConnectionPhase;
    using StationConnectionState = TunerModel::StationConnectionState;

    /// Why a window cannot change this object: the Core refuses every write.
    static QString readOnlyReason();

    explicit AmplifierModel(QObject* parent = nullptr);

    ConnectionPhase connectionPhase() const { return m_connection.phase; }
    QString configuredHost() const { return m_connection.configuredHost; }
    int configuredPort() const { return m_connection.configuredPort; }
    QString connectionError() const { return m_connection.error; }
    QString deviceModel() const { return m_connection.deviceModel; }
    QString deviceSerial() const { return m_connection.deviceSerial; }
    QString deviceVersion() const { return m_connection.deviceVersion; }
    QString deviceNickname() const { return m_connection.deviceNickname; }

    bool present() const { return m_present; }
    State state() const { return m_state; }
    QString deviceState() const { return m_gauges.deviceState; }
    bool operate() const { return m_operate; }
    bool transmitting() const { return m_gauges.transmitting; }
    double forwardPowerW() const { return m_gauges.forwardPowerW; }
    double swr() const { return m_gauges.swr; }
    double temperatureC() const { return m_gauges.temperatureC; }
    double mainsVoltageV() const { return m_gauges.mainsVoltageV; }
    double drainCurrentA() const { return m_gauges.drainCurrentA; }
    QString efficiencyText() const { return m_gauges.efficiencyText; }

    /// The State for an amp's state word.
    static State stateFromWord(const QString& deviceState);

    /// Follow `connection`: its status frames through applyPgxlStatus(), its
    /// connect, drop, retry and failure as the connection phase. The Core
    /// and a local window only; a remote window never binds.
    void bindConnection(PgxlConnection* connection);

    /// Whether the station has the amp switched on (the 4O3A switch). Off
    /// reads as phase Disabled whenever the amp is not connected.
    void setAccessoryEnabled(bool enabled);

    /// A whole connection state at once (a Core-side controller, or a test).
    /// Observational: it never opens a socket or sends a command.
    void setStationConnectionState(const StationConnectionState& state);

    /// One status frame, as PgxlConnection::statusUpdated carries it.
    void applyStatusFrame(const QMap<QString, QString>& kvs);

    /// A remote window: one of the Core's values arriving. False for a
    /// property this object does not have or a value of the wrong kind.
    bool applyStationValue(const QByteArray& propertyName, const QVariant& value);

signals:
    void stationConnectionChanged();
    void statusChanged();

private:
    void publishConnection(const StationConnectionState& next);
    void refreshFromConnection(ConnectionPhase phase, const QString& error);

    QPointer<PgxlConnection> m_conn;
    StationConnectionState m_connection;
    bool m_enabled{true};
    bool m_present{false};
    State m_state{State::Unknown};
    bool m_operate{false};
    PgxlGauges m_gauges;
};

} // namespace NereusSDR
