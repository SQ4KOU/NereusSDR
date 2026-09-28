#pragma once
// no-port-check: NereusSDR-original presentation for the local Core.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/SetupPage.h"

#include <QByteArray>
#include <QString>
#include <QVector>

class QCheckBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QVBoxLayout;
class QWidget;

namespace NereusSDR {

class RemoteStationPage : public SetupPage {
    Q_OBJECT
public:
    struct Device {
        QByteArray id;
        QString name;
        QString pairedText;
        QString lastSeenText;
        bool revocable = true;
    };
    struct State {
        bool available = false;
        QString unavailableReason;
        bool runCore = false;
        bool keepRunning = false;
        bool startWithComputer = false;
        bool busy = false;
        bool transmitting = false;
        QString stationName;
        QString reachabilityText;
        QString pairingCode;
        QString keyBackupPath;
        bool pairingOpen = false;
        bool keyBackupAcknowledged = false;
        QVector<Device> devices;
    };

    explicit RemoteStationPage(QWidget* parent = nullptr);
    void setState(const State& state);
    const State& state() const { return m_state; }

signals:
    void runCoreRequested(bool enabled);
    void keepRunningRequested(bool enabled);
    void startWithComputerRequested(bool enabled);
    void renameRequested(const QString& name);
    void revokeRequested(const QByteArray& id);
    void addDeviceRequested();
    void keyBackupAcknowledgedRequested();
    // Compatibility with SetupDialog until its production bridge is wired.
    void connectionsRequested();

private:
    void refresh();
    void rebuildDevices();
    void applyGate(QWidget* control, bool allowed, const QString& reason);
    State m_state;
    QCheckBox* m_runCore = nullptr;
    QCheckBox* m_keepRunning = nullptr;
    QCheckBox* m_startWithComputer = nullptr;
    QLabel* m_reason = nullptr;
    QLabel* m_name = nullptr;
    QPushButton* m_rename = nullptr;
    QLabel* m_reachability = nullptr;
    QLabel* m_pairingCode = nullptr;
    QLabel* m_pairingInstruction = nullptr;
    QVBoxLayout* m_devicesLayout = nullptr;
    QWidget* m_deviceRows = nullptr;
    QPushButton* m_addDevice = nullptr;
    QLabel* m_backupPath = nullptr;
    QGroupBox* m_backupGroup = nullptr;
    QPushButton* m_backupAcknowledged = nullptr;
    quint64 m_deviceGeneration = 0;
};

} // namespace NereusSDR
