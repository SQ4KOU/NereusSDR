#pragma once
// no-port-check: NereusSDR-original. The Core's RF-Kit RF2K-S status as the
// mirrored, read-only `rfkit` object.

// SPDX-License-Identifier: GPL-3.0-or-later
//
// =================================================================
// src/models/RfKitModel.h  (NereusSDR)
// =================================================================
//
// NereusSDR-original; no upstream port. R-R3-47 / R-R3-22.
//
// What the Core knows about its RF-Kit RF2K-S: the Tuner Genius's
// connection-state shape plus operate and the amp's meters as its REST
// interface reports them (W, SWR ratio, degrees C, V, A). The Core mirrors
// it as the read-only `rfkit` object (remoteRfKitControlVersion 1); a local
// window reads the same object in-process.
//
// Bound (the Core, a local window) it follows an Rf2ksConnection. Unbound
// (a remote window) it only holds the Core's values as they arrive through
// applyStationValue(); it never opens a connection or sends a request.
//
// The wire contract: docs/architecture/2026-09-23-remote-accessory-control-v1.md.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-23  J.J. Boyd / KG4VCF  Created (R-R3-47, R-R3-22). AI-assisted
//                                    via Anthropic Claude Code.
// =================================================================

#include "models/TunerModel.h"

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariant>

namespace NereusSDR {

class Rf2ksConnection;
struct RfKitPowerSnapshot;

class RfKitModel : public QObject {
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
    // Operate and the meters.
    Q_PROPERTY(bool present READ present NOTIFY statusChanged)
    Q_PROPERTY(bool operate READ operate NOTIFY statusChanged)
    Q_PROPERTY(double forwardPowerW READ forwardPowerW NOTIFY statusChanged)
    Q_PROPERTY(double reflectedPowerW READ reflectedPowerW NOTIFY statusChanged)
    Q_PROPERTY(double swr READ swr NOTIFY statusChanged)
    Q_PROPERTY(double temperatureC READ temperatureC NOTIFY statusChanged)
    Q_PROPERTY(double voltageV READ voltageV NOTIFY statusChanged)
    Q_PROPERTY(double currentA READ currentA NOTIFY statusChanged)

public:
    using ConnectionPhase = TunerModel::ConnectionPhase;
    using StationConnectionState = TunerModel::StationConnectionState;

    /// Why a window cannot change this object: the Core refuses every write.
    static QString readOnlyReason();

    explicit RfKitModel(QObject* parent = nullptr);

    ConnectionPhase connectionPhase() const { return m_connection.phase; }
    QString configuredHost() const { return m_connection.configuredHost; }
    int configuredPort() const { return m_connection.configuredPort; }
    QString connectionError() const { return m_connection.error; }
    QString deviceModel() const { return m_connection.deviceModel; }
    QString deviceSerial() const { return m_connection.deviceSerial; }
    QString deviceVersion() const { return m_connection.deviceVersion; }
    QString deviceNickname() const { return m_connection.deviceNickname; }

    bool present() const { return m_present; }
    bool operate() const { return m_operate; }
    double forwardPowerW() const { return m_forwardPowerW; }
    double reflectedPowerW() const { return m_reflectedPowerW; }
    double swr() const { return m_swr; }
    double temperatureC() const { return m_temperatureC; }
    double voltageV() const { return m_voltageV; }
    double currentA() const { return m_currentA; }

    /// Follow `connection`: its power, operate and info reports, and its
    /// connect, drop and failure as the connection phase. The Core and a
    /// local window only; a remote window never binds.
    void bindConnection(Rf2ksConnection* connection);

    /// Whether the station has the RF-Kit switched on (rfKitEnabled). Off
    /// reads as phase Disabled whenever the amp is not connected.
    void setAccessoryEnabled(bool enabled);

    /// A whole connection state at once (a Core-side controller, or a test).
    void setStationConnectionState(const StationConnectionState& state);

    /// One /power report.
    void applyPower(const RfKitPowerSnapshot& snapshot);
    /// One /operate-mode report ("OPERATE" or "STANDBY").
    void applyOperateMode(const QString& mode);
    /// One /info report.
    void applyInfo(const QString& device, const QString& softwareVersion,
                   const QString& customName);

    /// A remote window: one of the Core's values arriving.
    bool applyStationValue(const QByteArray& propertyName, const QVariant& value);

signals:
    void stationConnectionChanged();
    void statusChanged();

private:
    void publishConnection(const StationConnectionState& next);
    void refreshFromConnection(ConnectionPhase phase, const QString& error);

    QPointer<Rf2ksConnection> m_conn;
    StationConnectionState m_connection;
    bool m_enabled{true};
    bool m_present{false};
    bool m_operate{false};
    double m_forwardPowerW{0.0};
    double m_reflectedPowerW{0.0};
    double m_swr{1.0};
    double m_temperatureC{0.0};
    double m_voltageV{0.0};
    double m_currentA{0.0};
};

} // namespace NereusSDR
