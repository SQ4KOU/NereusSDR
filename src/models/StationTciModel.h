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
//   2026-09-27  J.J. Boyd / KG4VCF  Parity Task 23 (R-R3-48, R-R3-42,
//                                    R-R3-49): stationTciVersion 2, the
//                                    server's four options as Outbound
//                                    properties and its apps (the
//                                    `tciClients` record stream).
//                                    AI-assisted via Anthropic Claude Code.
// =================================================================

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <optional>

namespace NereusSDR {

/// Parity Task 23 (stationTciVersion 2): one app connected to the Core's
/// station TCI server, one record of the `tciClients` stream (the iPhone
/// plan's Task 25 fields).
struct StationTciClient {
    QString id;            // the stream id: a number the Core gives each connection
    QString name;          // what the app calls itself, "(unknown)" when it does not say
    QString address;       // where it connected from, "host:port"
    QStringList subscriptions;   // "audio:N", "iq:N", "rxSensors", "txSensors"
    bool transmitting{false};    // holds the server's transmit audio
    QString lastCommand;   // the last line it sent, empty before any

    QJsonObject toFields() const;
    static std::optional<StationTciClient> fromFields(const QString& id,
                                                      const QJsonObject& fields);
    bool operator==(const StationTciClient& other) const = default;
};

class StationTciModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled NOTIFY stateChanged)
    Q_PROPERTY(int port READ port NOTIFY stateChanged)
    Q_PROPERTY(bool listening READ listening NOTIFY stateChanged)
    Q_PROPERTY(QString stationAddress READ stationAddress NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    // Parity Task 23 (stationTciVersion 2): the Core's server options, the
    // Core's TciEmulateExpertSDR3Protocol, TciEmulateSunSDR2Pro,
    // TciCwluBecomesCw and TciSendInitialFrequencyStateOnConnect. Changed
    // only through setStationTciOptions.
    Q_PROPERTY(bool emulateExpertSdr3 READ emulateExpertSdr3 NOTIFY stateChanged)
    Q_PROPERTY(bool emulateSunSdr2Pro READ emulateSunSdr2Pro NOTIFY stateChanged)
    Q_PROPERTY(bool cwluBecomesCw READ cwluBecomesCw NOTIFY stateChanged)
    Q_PROPERTY(bool sendInitialState READ sendInitialState NOTIFY stateChanged)

public:
    struct State {
        bool enabled{false};
        int port{0};
        bool listening{false};
        QString stationAddress;
        QString error;
        // Parity Task 23: the readers' defaults (TciProtocol::buildInitBurst).
        bool emulateExpertSdr3{true};
        bool emulateSunSdr2Pro{true};
        bool cwluBecomesCw{false};
        bool sendInitialState{true};
        bool operator==(const State& other) const = default;
    };

    /// Why a window cannot write this object: the Core refuses every write.
    static QString readOnlyReason();
    /// Parity Task 23: the `tciClients` stream's capacity (and a window's
    /// backlog when it subscribes).
    static constexpr int kClientsCapacity = 64;

    explicit StationTciModel(QObject* parent = nullptr);

    bool enabled() const { return m_state.enabled; }
    int port() const { return m_state.port; }
    bool listening() const { return m_state.listening; }
    QString stationAddress() const { return m_state.stationAddress; }
    QString error() const { return m_state.error; }
    bool emulateExpertSdr3() const { return m_state.emulateExpertSdr3; }
    bool emulateSunSdr2Pro() const { return m_state.emulateSunSdr2Pro; }
    bool cwluBecomesCw() const { return m_state.cwluBecomesCw; }
    bool sendInitialState() const { return m_state.sendInitialState; }
    State state() const { return m_state; }

    /// The Core's controller (or a test): the whole state at once.
    void setState(const State& state);

    /// A remote window: one of the Core's values arriving.
    bool applyStationValue(const QByteArray& propertyName, const QVariant& value);

    /// Parity Task 23: the apps on the Core's server, in the order they
    /// connected. The Core's controller sets them; a remote window's come
    /// from the `tciClients` stream. Not a mirrored property.
    QList<StationTciClient> clients() const { return m_clients; }
    void setClients(const QList<StationTciClient>& clients);

signals:
    void stateChanged();
    /// Parity Task 23: the apps, or one app's fields, changed.
    void clientsChanged();

private:
    State m_state;
    QList<StationTciClient> m_clients;
};

} // namespace NereusSDR
