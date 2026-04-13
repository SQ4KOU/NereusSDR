#pragma once
// DiversityTab.h
//
// "Diversity" sub-tab of HardwarePage.
// Exposes enable toggle, phase/gain sliders, reference ADC selector,
// and a "null signal" preset button.
//
// Source: Thetis DiversityForm.cs — chkLockAngle / chkLockR (lines 1216/1228),
// udGainMulti NumericUpDown (line 1177), labelTS4 "Phase" / labelTS3 "Gain"
// (lines 1240/1293), trackBarR1/trackBarPhase1 commented-out sliders (lines 182-183),
// udR1/udR2 values (lines 170-171).

#include <QVariant>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QSlider;
class QStackedWidget;

namespace NereusSDR {

class RadioModel;
struct RadioInfo;
struct BoardCapabilities;

class DiversityTab : public QWidget {
    Q_OBJECT
public:
    explicit DiversityTab(RadioModel* model, QWidget* parent = nullptr);
    void populate(const RadioInfo& info, const BoardCapabilities& caps);
    void restoreSettings(const QMap<QString, QVariant>& settings);

signals:
    void settingChanged(const QString& key, const QVariant& value);

private:
    RadioModel*      m_model{nullptr};

    // Two pages: 0 = unsupported notice, 1 = controls
    QStackedWidget*  m_stack{nullptr};

    // Controls page widgets
    QCheckBox*       m_enableCheck{nullptr};
    QSlider*         m_phaseSlider{nullptr};
    QLabel*          m_phaseValueLabel{nullptr};
    QSlider*         m_gainSlider{nullptr};
    QLabel*          m_gainValueLabel{nullptr};
    QComboBox*       m_referenceAdcCombo{nullptr};
    QPushButton*     m_nullSignalButton{nullptr};
};

} // namespace NereusSDR
