#pragma once

#include <QDialog>
#include <QTreeWidget>
#include <QStackedWidget>
#include <QSplitter>

namespace NereusSDR {

class RadioModel;
class SetupPage;

// Main settings dialog with tree-based navigation.
// Left pane: QTreeWidget with top-level category items.
// Right pane: QStackedWidget showing the selected page.
class SetupDialog : public QDialog {
    Q_OBJECT
public:
    explicit SetupDialog(RadioModel* model, QWidget* parent = nullptr);

    // Navigate to a page by its label text (e.g. "AGC/ALC").
    void selectPage(const QString& label);

signals:
    // Phase 3M-3a-ii Batch 6 (Task 3): forwarded from CfcSetupPage's
    // [Configure CFC bands…] button.  MainWindow connects this to the
    // TxApplet::requestOpenCfcDialog() slot so the modeless TxCfcDialog
    // instance is shared between the Setup page and the [CFC] right-click
    // on the TxApplet.
    void cfcDialogRequested();

    // Task 3.6: forwarded from GeneralOptionsPage — CPU meter rate spinbox.
    // MainWindow::setCpuTimerIntervalHz() is the handler.
    void cpuMeterRateChanged(int hz);

    // Task 3.6: forwarded from RadioInfoTab — ANAN-8000DLE volts/amps toggle.
    // MainWindow::setVoltsAmpsVisible() is the handler.
    void anan8000DleVoltsAmpsChanged(bool visible);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void buildTree();

    RadioModel*      m_model   = nullptr;
    QTreeWidget*     m_tree    = nullptr;
    QStackedWidget*  m_stack   = nullptr;
};

} // namespace NereusSDR
