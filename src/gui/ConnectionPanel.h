#pragma once

#include "core/RadioDiscovery.h"
#include "core/RadioConnection.h"

#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>

namespace NereusSDR {

class RadioModel;

// Connection panel dialog for discovering and connecting to OpenHPSDR radios.
// Shows a list of discovered radios on the local network and provides
// Connect / Disconnect controls with live connection status.
class ConnectionPanel : public QDialog {
    Q_OBJECT

public:
    explicit ConnectionPanel(RadioModel* model, QWidget* parent = nullptr);
    ~ConnectionPanel() override;

    // Update the connection status display.
    void setStatusText(const QString& text);

public slots:
    void onRadioDiscovered(const NereusSDR::RadioInfo& info);
    void onRadioUpdated(const NereusSDR::RadioInfo& info);
    void onRadioLost(const QString& macAddress);
    void onConnectionStateChanged();

private slots:
    void onConnectClicked();
    void onDisconnectClicked();
    void onRadioSelectionChanged();

private:
    void buildUI();
    void updateButtonStates();

    // Get the RadioInfo for the currently selected list item
    RadioInfo selectedRadio() const;

    RadioModel* m_radioModel;

    // Widgets
    QListWidget* m_radioList{nullptr};
    QPushButton* m_connectBtn{nullptr};
    QPushButton* m_disconnectBtn{nullptr};
    QPushButton* m_refreshBtn{nullptr};
    QLabel* m_statusLabel{nullptr};

    // Track discovered radios by MAC (row data role = MAC string)
    QMap<QString, RadioInfo> m_discoveredRadios;

    static constexpr int kMacRole = Qt::UserRole + 1;
};

} // namespace NereusSDR
