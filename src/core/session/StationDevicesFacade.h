#pragma once
// no-port-check: NereusSDR-original.
// =================================================================
// src/core/session/StationDevicesFacade.h  (NereusSDR)
// =================================================================
//
// The mirrored `devices` object (iPhone app plan Task 13, R-IOS-08; spec
// section 5.2 item 8; the pairing design, docs/architecture/2026-08-02-
// remote-station-identity-and-pairing-design.md section 7, "Devices and
// revocation").
//
// What a paired device's Devices page shows about the Core, all station to
// client: the paired devices (listJson), the Core's label, whether it is
// claimed, whether the old pairing token still works, whether the Core's
// identity key backup was acknowledged, and where that key file is. Every
// change moves `revision` once (serial-number arithmetic, as NotchModel's).
//
// And the four things a device asks of it (SessionCommandDispatcher routes
// the verbs here; deviceAdminVersion 1):
//
//   devices.revoke {id}            remove a paired device. StationServer
//                                  ends that device's connection on
//                                  DeviceStore::deviceRemoved, whatever
//                                  removed it.
//   station.rename {label}         store the label under the Core-owned
//                                  StationLabel setting.
//   station.acknowledgeKeyBackup   the operator has backed up the Core's
//                                  identity key. Stored as the key's
//                                  fingerprint under the Core-owned
//                                  StationKeyBackupAcknowledged setting, so
//                                  a replaced key asks again.
//   station.retireToken            stop accepting the old pairing token,
//                                  once a device is paired. StationServer
//                                  ends every connection signed in by token
//                                  on tokenRetired().
//
// The facade knows nothing about connections: StationServer tells it which
// devices are connected (setConnectedDevices) and ends connections itself.
// Several devices may be connected at once (a later session design); the
// facade keeps a set for that reason.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-24: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include <QByteArray>
#include <QObject>
#include <QSet>
#include <QString>

namespace NereusSDR {

class AppSettings;
class DeviceStore;
class StationIdentity;
class TokenStore;

/// What a device-administration request came to.
struct DeviceAdminResult {
    bool accepted = false;
    /// Plain operator words; empty when accepted.
    QString reason;
};

class StationDevicesFacade final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString listJson READ listJson NOTIFY devicesStateChanged)
    Q_PROPERTY(quint32 revision READ revision NOTIFY devicesStateChanged)
    Q_PROPERTY(QString stationLabel READ stationLabel NOTIFY devicesStateChanged)
    Q_PROPERTY(bool claimed READ claimed NOTIFY devicesStateChanged)
    Q_PROPERTY(bool tokenActive READ tokenActive NOTIFY devicesStateChanged)
    Q_PROPERTY(bool keyBackupAcknowledged READ keyBackupAcknowledged
                   NOTIFY devicesStateChanged)
    Q_PROPERTY(QString keyPath READ keyPath NOTIFY devicesStateChanged)

public:
    /// The Core-owned setting that holds the acknowledged key's fingerprint.
    static constexpr const char* kKeyBackupSettingsKey = "StationKeyBackupAcknowledged";

    /// None of the four is owned; each must outlive this object.
    StationDevicesFacade(DeviceStore& devices, TokenStore& tokens,
                         const StationIdentity& identity, AppSettings& settings,
                         QObject* parent = nullptr);

    /// A JSON array of {id, name, kind, pairedAt, lastSeen, connected}, in
    /// pairing order. `id` is the device's key fingerprint in base64url;
    /// the times are ISO 8601 UTC ("" when never seen).
    QString listJson() const { return m_state.listJson; }
    quint32 revision() const { return m_revision; }
    /// The Core's label as displayed; "" when it has none yet (no rename
    /// and no StationCallsign).
    QString stationLabel() const { return m_state.stationLabel; }
    bool claimed() const { return m_state.claimed; }
    bool tokenActive() const { return m_state.tokenActive; }
    bool keyBackupAcknowledged() const { return m_state.keyBackupAcknowledged; }
    QString keyPath() const { return m_state.keyPath; }

    DeviceAdminResult revoke(const QString& id);
    DeviceAdminResult rename(const QString& label);
    DeviceAdminResult acknowledgeKeyBackup();
    DeviceAdminResult retireToken();

    /// The paired devices (by id) that hold an authenticated connection.
    void setConnectedDevices(const QSet<QByteArray>& ids);

    /// Re-reads everything; a change moves revision once and notifies.
    /// StationServer calls it when a setting the label follows changes.
    void refresh();

    /// Holds refresh() back until the matching resumeRefresh(), which
    /// refreshes once, so one event is one change.
    void holdRefresh();
    void resumeRefresh();

signals:
    void devicesStateChanged();
    /// The displayed label changed (a rename, or StationCallsign while no
    /// rename is stored). Task 16's announcement follows it.
    void stationLabelChanged(const QString& label);
    /// The token was retired through retireToken().
    void tokenRetired();

private:
    struct State {
        QString listJson;
        QString stationLabel;
        bool claimed = false;
        bool tokenActive = false;
        bool keyBackupAcknowledged = false;
        QString keyPath;

        bool operator==(const State&) const = default;
    };
    State compute() const;

    DeviceStore& m_devices;
    TokenStore& m_tokens;
    const StationIdentity& m_identity;
    AppSettings& m_settings;
    QSet<QByteArray> m_connected;
    State m_state;
    quint32 m_revision = 0;
    int m_hold = 0;
    bool m_refreshWanted = false;
};

} // namespace NereusSDR
