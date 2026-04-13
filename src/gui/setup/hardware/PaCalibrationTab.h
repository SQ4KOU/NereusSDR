#pragma once
// PaCalibrationTab.h
//
// "PA Calibration" sub-tab of HardwarePage.
// Surfaces per-band target power + gain correction tables, a PA profile
// selector, a step-attenuator calibration button (gated on caps), and an
// auto-calibrate trigger (cold this phase).
//
// Source: Thetis Setup.cs:
//   - grpGainByBandPA (line 1796): per-band PA gain group box
//   - nudPAProfileGain_ValueChanged (line 24135): per-band gain correction
//   - nudMaxPowerForBandPA (line 24048): per-band max/target power
//   - comboPAProfile / updatePAProfileCombo() (lines 1973, 23784-23786)
//   - chkPA160..chkPA10 enable flags (line 9969)
//   - udPACalPower (line 12278): cal power spinbox

#include <QVariant>
#include <QWidget>

class QComboBox;
class QGroupBox;
class QPushButton;
class QScrollArea;
class QTableWidget;
class QVBoxLayout;

namespace NereusSDR {

class RadioModel;
struct RadioInfo;
struct BoardCapabilities;

class PaCalibrationTab : public QWidget {
    Q_OBJECT
public:
    explicit PaCalibrationTab(RadioModel* model, QWidget* parent = nullptr);
    void populate(const RadioInfo& info, const BoardCapabilities& caps);
    void restoreSettings(const QMap<QString, QVariant>& settings);

signals:
    void settingChanged(const QString& key, const QVariant& value);

private:
    RadioModel*  m_model{nullptr};

    // Per-band target power table — 14 rows × 1 column "Target (W)"
    QTableWidget* m_targetPowerTable{nullptr};

    // Per-band gain correction table — 14 rows × 1 column "Correction (dB)"
    QTableWidget* m_gainCorrectionTable{nullptr};

    // PA profile combo
    QComboBox*    m_paProfileCombo{nullptr};

    // Step attenuator cal button — hidden unless caps.hasStepAttenuatorCal
    QPushButton*  m_stepAttenCalButton{nullptr};

    // Auto-calibrate trigger (cold)
    QPushButton*  m_autoCalibrateButton{nullptr};
};

} // namespace NereusSDR
