#pragma once

#include <QMainWindow>
#include <QLabel>

namespace NereusSDR {

class RadioModel;
class ConnectionPanel;
class SupportDialog;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onConnectionStateChanged();
    void showConnectionPanel();
    void showSupportDialog();

private:
    void buildUI();
    void buildMenuBar();
    void buildStatusBar();
    void applyDarkTheme();
    void tryAutoReconnect();

    RadioModel* m_radioModel{nullptr};
    ConnectionPanel* m_connectionPanel{nullptr};
    SupportDialog* m_supportDialog{nullptr};

    // Status bar widgets
    QLabel* m_connStatusLabel{nullptr};
    QLabel* m_radioInfoLabel{nullptr};
};

} // namespace NereusSDR
