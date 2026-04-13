#pragma once
// OcOutputsTab.h
//
// "OC Outputs" sub-tab of HardwarePage.
//
// Source: Thetis Setup.cs UpdateOCBits() lines 12877-12934.
// Per-band RX mask: chkPenOCrcv{band}{1-7} — 7 OC output bits per band.
// Per-band TX mask: chkPenOCxmit{band}{1-7} — same shape.
// Also Penny.getBandABitMask/setBandABitMask pattern — 7-bit mask per band.
//
// NereusSDR represents each grid as a QTableWidget:
//   Rows = 14 bands (Band::Count), Columns = 7 OC outputs.
// Each cell is a checkable QTableWidgetItem.

#include <QVariant>
#include <QWidget>

class QLabel;
class QSpinBox;
class QTableWidget;
class QTableWidgetItem;

namespace NereusSDR {

class RadioModel;
struct RadioInfo;
struct BoardCapabilities;

class OcOutputsTab : public QWidget {
    Q_OBJECT
public:
    explicit OcOutputsTab(RadioModel* model, QWidget* parent = nullptr);
    void populate(const RadioInfo& info, const BoardCapabilities& caps);
    void restoreSettings(const QMap<QString, QVariant>& settings);

signals:
    void settingChanged(const QString& key, const QVariant& value);

private slots:
    void onRxMaskChanged(QTableWidgetItem* item);
    void onTxMaskChanged(QTableWidgetItem* item);

private:
    static QTableWidget* makeGrid(QWidget* parent);

    RadioModel*   m_model{nullptr};

    QTableWidget* m_rxGrid{nullptr};
    QTableWidget* m_txGrid{nullptr};
    QSpinBox*     m_settleDelaySpin{nullptr};
    QLabel*       m_noOcLabel{nullptr};
};

} // namespace NereusSDR
