#pragma once
// RadioInfoTab.h
//
// "Radio Info" sub-tab of HardwarePage.
// Displays read-only identity info from the connected radio and lets the user
// choose sample rate and active-RX count. All controls are populated via
// populate(info, caps); before a radio is connected the fields are blank.
//
// Source: Thetis Setup.cs Hardware Config / General section — board model
// combo, sample-rate combo, active-RX count (numericUpDownNr), firmware/MAC/IP
// read-only text boxes.

#include <QWidget>
#include <QVariant>

class QLabel;
class QComboBox;
class QSpinBox;
class QPushButton;
class QFormLayout;

namespace NereusSDR {

class RadioModel;
struct RadioInfo;
struct BoardCapabilities;

class RadioInfoTab : public QWidget {
    Q_OBJECT
public:
    explicit RadioInfoTab(RadioModel* model, QWidget* parent = nullptr);
    // Called by HardwarePage when the connected radio changes.
    void populate(const RadioInfo& info, const BoardCapabilities& caps);
    // Restore persisted control values (Phase 3I Task 21).
    void restoreSettings(const QMap<QString, QVariant>& settings);

signals:
    void settingChanged(const QString& key, const QVariant& value);

private slots:
    void onSampleRateChanged(int index);

private:
    RadioModel*  m_model{nullptr};

    QLabel*      m_boardLabel{nullptr};
    QLabel*      m_protocolLabel{nullptr};
    QLabel*      m_adcCountLabel{nullptr};
    QLabel*      m_maxRxLabel{nullptr};
    QLabel*      m_firmwareLabel{nullptr};
    QLabel*      m_macLabel{nullptr};
    QLabel*      m_ipLabel{nullptr};
    QComboBox*   m_sampleRateRx1Combo{nullptr};
    QComboBox*   m_sampleRateRx2Combo{nullptr};   // disabled in PR #35; activates with Phase 3F multi-panadapter.
    QSpinBox*    m_activeRxSpin{nullptr};
    QPushButton* m_copySupportInfoButton{nullptr};

    // Cached for copy-to-clipboard
    QString m_currentInfo;
};

} // namespace NereusSDR
