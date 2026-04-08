#pragma once

#include <QDialog>
#include <QPlainTextEdit>
#include <QLabel>
#include <QCheckBox>
#include <QPushButton>
#include <QMap>

namespace NereusSDR {

class RadioModel;

// Support dialog with diagnostic logging controls, log viewer,
// and support bundle creation for bug reports.
//
// Access via Help → Support... or Tools → Network Diagnostics...
class SupportDialog : public QDialog {
    Q_OBJECT

public:
    explicit SupportDialog(RadioModel* model, QWidget* parent = nullptr);
    ~SupportDialog() override;

private slots:
    void onRefresh();
    void onClearLog();
    void onOpenLogFolder();
    void onEnableAll();
    void onDisableAll();
    void onCreateBundle();
    void onCategoryToggled(const QString& id, bool on);

private:
    void buildUI();
    void refreshLogViewer();
    void updateLogInfo();

    RadioModel* m_radioModel;

    // Category checkboxes keyed by category id
    QMap<QString, QCheckBox*> m_categoryChecks;

    // Log viewer
    QPlainTextEdit* m_logViewer{nullptr};
    QLabel* m_logPathLabel{nullptr};
    QLabel* m_logSizeLabel{nullptr};

    // Status
    QLabel* m_statusLabel{nullptr};

    static constexpr int kMaxLogViewBytes = 200 * 1024;  // 200 KB tail
    static constexpr int kMaxLogViewLines = 2000;
};

} // namespace NereusSDR
