#pragma once
// XvtrTab.h
//
// "XVTR" sub-tab of HardwarePage.
//
// Source: Thetis xvtr.cs XVTRForm class (lines 47-249).
// Columns per row: Enabled (chkEnable*), Name (txtButtonText*),
// RF Start (udFreqBegin*), RF End (udFreqEnd*), LO Offset (udLOOffset*),
// RX-only (chkRXOnly*), Power (udPower*), LO Error (udLOError*).
// Thetis has 16 rows (chkEnable0..15); we expose 5 rows by default
// (caps.xvtrJackCount rows), capped at 16.
//
// "Auto-select active band" checkbox — controls whether the active VFO
// band automatically selects the matching XVTR row.

#include <QVariant>
#include <QWidget>

class QCheckBox;
class QLabel;
class QTableWidget;
class QTableWidgetItem;

namespace NereusSDR {

class RadioModel;
struct RadioInfo;
struct BoardCapabilities;

class XvtrTab : public QWidget {
    Q_OBJECT
public:
    explicit XvtrTab(RadioModel* model, QWidget* parent = nullptr);
    void populate(const RadioInfo& info, const BoardCapabilities& caps);
    void restoreSettings(const QMap<QString, QVariant>& settings);

signals:
    void settingChanged(const QString& key, const QVariant& value);

private slots:
    void onTableItemChanged(QTableWidgetItem* item);

private:
    RadioModel*   m_model{nullptr};

    QCheckBox*    m_autoSelectBand{nullptr};
    QTableWidget* m_table{nullptr};
    QLabel*       m_noXvtrLabel{nullptr};

    // Number of rows currently shown
    int m_visibleRows{0};
};

} // namespace NereusSDR
