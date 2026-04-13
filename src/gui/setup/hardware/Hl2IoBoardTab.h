#pragma once
// Hl2IoBoardTab.h
//
// "HL2 I/O Board" sub-tab of HardwarePage.
// Surfaces the Hermes Lite 2 I/O board GPIO/PTT/aux-output configuration.
//
// Source: mi0bot Thetis-HL2 HPSDR/IoBoardHl2.cs —
//   GPIO_DIRECT_BASE = 170 (line 79), I2C address 0x1d (line 139),
//   register REG_TX_FREQ_BYTE* (lines 194-198).
//
// NOTE: IoBoardHl2.cs wraps closed-source I2C register code. The pin names
// and GPIO counts here (0-3) are reasonable defaults based on the HL2 open
// hardware spec (Hermes Lite 2 BOM/schematic). Pending real HL2 smoke test.

#include <QVariant>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QGroupBox;
class QLabel;

namespace NereusSDR {

class RadioModel;
struct RadioInfo;
struct BoardCapabilities;

class Hl2IoBoardTab : public QWidget {
    Q_OBJECT
public:
    explicit Hl2IoBoardTab(RadioModel* model, QWidget* parent = nullptr);
    void populate(const RadioInfo& info, const BoardCapabilities& caps);
    void restoreSettings(const QMap<QString, QVariant>& settings);

signals:
    void settingChanged(const QString& key, const QVariant& value);

private:
    RadioModel*  m_model{nullptr};

    // I/O board present indicator (read-only once connected)
    QLabel*      m_ioBoardPresentLabel{nullptr};

    // GPIO pin combo boxes
    QComboBox*   m_extPttInputCombo{nullptr};
    QComboBox*   m_cwKeyInputCombo{nullptr};

    // Aux output assignment combo boxes
    QComboBox*   m_auxOut1Combo{nullptr};
    QComboBox*   m_auxOut2Combo{nullptr};
};

} // namespace NereusSDR
