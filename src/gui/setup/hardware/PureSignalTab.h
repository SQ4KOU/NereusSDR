#pragma once
// PureSignalTab.h
//
// "Pure Signal" sub-tab of HardwarePage.
// Exposes the PureSignal enable toggle, feedback source selector,
// auto-calibrate options, and RX feedback attenuator.
//
// Source: Thetis PSForm.cs — chkPSAutoAttenuate (line 841),
// checkLoopback (line 846), AutoCalEnabled property (line 272),
// _restoreON / _autoON state flags (lines 93-96), AutoAttenuate
// property (lines 291-311).
//
// Phase 3I: PureSignal is COLD — controls persist state via
// settingChanged(key, value) but do NOT start the PS feedback loop.
// DSP hookup is deferred to Phase 3I-4.

#include <QVariant>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QSlider;
class QStackedWidget;

namespace NereusSDR {

class RadioModel;
struct RadioInfo;
struct BoardCapabilities;

class PureSignalTab : public QWidget {
    Q_OBJECT
public:
    explicit PureSignalTab(RadioModel* model, QWidget* parent = nullptr);
    void populate(const RadioInfo& info, const BoardCapabilities& caps);
    void restoreSettings(const QMap<QString, QVariant>& settings);

signals:
    void settingChanged(const QString& key, const QVariant& value);

private:
    RadioModel*      m_model{nullptr};

    // Two pages: 0 = unsupported notice, 1 = controls
    QStackedWidget*  m_stack{nullptr};

    // Controls page widgets (only valid when stack page == 1)
    QCheckBox*       m_enableCheck{nullptr};
    QComboBox*       m_feedbackSourceCombo{nullptr};
    QCheckBox*       m_autoCalOnBandChangeCheck{nullptr};
    QCheckBox*       m_preserveCalCheck{nullptr};
    QSlider*         m_rxFeedbackAttenSlider{nullptr};
    QLabel*          m_rxAttenValueLabel{nullptr};
};

} // namespace NereusSDR
