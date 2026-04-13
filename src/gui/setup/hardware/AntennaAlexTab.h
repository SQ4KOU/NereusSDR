#pragma once
// AntennaAlexTab.h
//
// "Antenna / ALEX" sub-tab of HardwarePage.
//
// Source: Thetis Setup.cs InitAlexAntTables() (line 13393) — per-band
// per-receiver RX antenna radio buttons (radAlexR1_*/R2_*/R3_*), per-band
// TX antenna radio buttons (radAlexT1_*/T2_*/T3_*), per-band RX-only
// checkboxes (chkAlex*R1/R2/XV).
// Also Setup.cs:2892-2898 — chkRxOutOnTx, chkEXT1OutOnTx, chkEXT2OutOnTx,
// chkHFTRRelay, chkBPF2Gnd, chkEnableXVTRHF.

#include <QVariant>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QScrollArea;
class QTableWidget;
class QTableWidgetItem;

namespace NereusSDR {

class RadioModel;
struct RadioInfo;
struct BoardCapabilities;

class AntennaAlexTab : public QWidget {
    Q_OBJECT
public:
    explicit AntennaAlexTab(RadioModel* model, QWidget* parent = nullptr);
    void populate(const RadioInfo& info, const BoardCapabilities& caps);
    void restoreSettings(const QMap<QString, QVariant>& settings);

signals:
    void settingChanged(const QString& key, const QVariant& value);

private slots:
    void onRxAntTableChanged(QTableWidgetItem* item);
    void onTxAntTableChanged(QTableWidgetItem* item);

private:
    void buildAntennaTable(QTableWidget* table,
                           const QStringList& colHeaders,
                           const QString& signalKeyPrefix);

    RadioModel*   m_model{nullptr};

    // Per-band RX antenna: 14 rows × 3 columns (ANT1 / ANT2 / ANT3)
    // Each cell is a radio-button-like exclusive selection stored via
    // Qt::CheckStateRole. We use QTableWidget + exclusive logic in itemChanged.
    QTableWidget* m_rxAntTable{nullptr};

    // Per-band TX antenna: same shape
    QTableWidget* m_txAntTable{nullptr};

    // ALEX bypass / relay checkboxes
    // Source: Thetis Setup.cs:2892-2898
    QCheckBox*    m_rxOutOnTx{nullptr};
    QCheckBox*    m_ext1OutOnTx{nullptr};
    QCheckBox*    m_ext2OutOnTx{nullptr};
    QCheckBox*    m_hfTrRelay{nullptr};
    QCheckBox*    m_bpf2Gnd{nullptr};
    QCheckBox*    m_enableXvtrHf{nullptr};

    bool m_updating{false};
};

} // namespace NereusSDR
