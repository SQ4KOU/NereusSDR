// PureSignalTab.cpp
//
// Source: Thetis PSForm.cs
//   - chkPSAutoAttenuate_CheckedChanged (line 841): AutoAttenuate property
//   - checkLoopback_CheckedChanged (line 846): PSLoopback / feedback source
//   - AutoCalEnabled property (lines 272-278): auto-calibrate on band change
//   - _restoreON flag (line 93): preserve calibration across restarts
//   - AutoAttenuate / _autoattenuate (lines 291-311): RX feedback attenuation
//
// Phase 3I: ALL controls are cold — settingChanged is emitted but no DSP or
// PureSignal feedback loop is started. DSP hookup deferred to Phase 3I-4.

#include "PureSignalTab.h"

#include "core/BoardCapabilities.h"
#include "core/RadioDiscovery.h"
#include "models/RadioModel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSlider>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace NereusSDR {

PureSignalTab::PureSignalTab(RadioModel* model, QWidget* parent)
    : QWidget(parent), m_model(model)
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(8, 8, 8, 8);

    m_stack = new QStackedWidget(this);
    outerLayout->addWidget(m_stack);

    // ── Page 0: unsupported notice ────────────────────────────────────────────
    auto* unsupportedPage = new QWidget(m_stack);
    auto* unsupportedLayout = new QVBoxLayout(unsupportedPage);
    unsupportedLayout->addWidget(
        new QLabel(tr("Board does not support PureSignal."), unsupportedPage));
    unsupportedLayout->addStretch();
    m_stack->addWidget(unsupportedPage);  // index 0

    // ── Page 1: PureSignal controls ───────────────────────────────────────────
    auto* controlsPage = new QWidget(m_stack);
    auto* controlsLayout = new QVBoxLayout(controlsPage);
    controlsLayout->setSpacing(8);

    // ── Enable + Feedback source group ────────────────────────────────────────
    auto* mainGroup = new QGroupBox(tr("PureSignal"), controlsPage);
    auto* mainForm  = new QFormLayout(mainGroup);
    mainForm->setLabelAlignment(Qt::AlignRight);

    // Enable PureSignal checkbox — cold this phase
    // Source: PSForm.cs state flag _OFF (line 94); no PS start fired in Phase 3I
    m_enableCheck = new QCheckBox(
        tr("Enable PureSignal (inactive until Phase 3I-4)"), mainGroup);
    mainForm->addRow(m_enableCheck);

    // Feedback source combo
    // Source: PSForm.cs checkLoopback_CheckedChanged (line 846) —
    //   cmaster.PSLoopback = checkLoopback.Checked
    m_feedbackSourceCombo = new QComboBox(mainGroup);
    m_feedbackSourceCombo->addItem(tr("Internal"),           0);
    m_feedbackSourceCombo->addItem(tr("External loopback"),  1);
    mainForm->addRow(tr("Feedback source:"), m_feedbackSourceCombo);

    controlsLayout->addWidget(mainGroup);

    // ── Calibration options group ─────────────────────────────────────────────
    auto* calGroup = new QGroupBox(tr("Calibration"), controlsPage);
    auto* calForm  = new QFormLayout(calGroup);
    calForm->setLabelAlignment(Qt::AlignRight);

    // Auto-calibrate on band change
    // Source: PSForm.cs AutoCalEnabled property (lines 272-278)
    m_autoCalOnBandChangeCheck = new QCheckBox(
        tr("Auto-calibrate on band change"), calGroup);
    calForm->addRow(m_autoCalOnBandChangeCheck);

    // Preserve calibration across restarts
    // Source: PSForm.cs _restoreON flag (line 93) used by btnPSRestore_Click
    m_preserveCalCheck = new QCheckBox(
        tr("Preserve calibration across restarts"), calGroup);
    calForm->addRow(m_preserveCalCheck);

    controlsLayout->addWidget(calGroup);

    // ── RX feedback attenuator group ──────────────────────────────────────────
    auto* attenGroup  = new QGroupBox(tr("RX Feedback"), controlsPage);
    auto* attenLayout = new QVBoxLayout(attenGroup);

    auto* attenRow   = new QWidget(attenGroup);
    auto* attenHBox  = new QHBoxLayout(attenRow);
    attenHBox->setContentsMargins(0, 0, 0, 0);

    // RX feedback attenuator slider 0..31 dB
    // Source: PSForm.cs AutoAttenuate / _autoattenuate (lines 291-311) —
    //   console.ATTOnTX driven by attenuate state; range 0..31 from HPSDR
    //   step-attenuator spec (31 dB max in 1 dB steps).
    m_rxFeedbackAttenSlider = new QSlider(Qt::Horizontal, attenRow);
    m_rxFeedbackAttenSlider->setRange(0, 31);
    m_rxFeedbackAttenSlider->setValue(0);
    m_rxFeedbackAttenSlider->setTickInterval(5);
    m_rxFeedbackAttenSlider->setTickPosition(QSlider::TicksBelow);

    m_rxAttenValueLabel = new QLabel(QStringLiteral("0 dB"), attenRow);
    m_rxAttenValueLabel->setMinimumWidth(48);

    attenHBox->addWidget(m_rxFeedbackAttenSlider);
    attenHBox->addWidget(m_rxAttenValueLabel);

    attenLayout->addWidget(new QLabel(tr("Attenuator (0–31 dB):"), attenGroup));
    attenLayout->addWidget(attenRow);

    controlsLayout->addWidget(attenGroup);
    controlsLayout->addStretch();

    m_stack->addWidget(controlsPage);  // index 1

    // ── Connections ───────────────────────────────────────────────────────────
    connect(m_enableCheck, &QCheckBox::toggled, this, [this](bool checked) {
        emit settingChanged(QStringLiteral("pureSignal/enabled"), checked);
    });

    connect(m_feedbackSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        emit settingChanged(QStringLiteral("pureSignal/feedbackSource"),
                            m_feedbackSourceCombo->itemData(idx));
    });

    connect(m_autoCalOnBandChangeCheck, &QCheckBox::toggled, this, [this](bool checked) {
        emit settingChanged(QStringLiteral("pureSignal/autoCalOnBandChange"), checked);
    });

    connect(m_preserveCalCheck, &QCheckBox::toggled, this, [this](bool checked) {
        emit settingChanged(QStringLiteral("pureSignal/preserveCalibration"), checked);
    });

    connect(m_rxFeedbackAttenSlider, &QSlider::valueChanged, this, [this](int value) {
        m_rxAttenValueLabel->setText(QStringLiteral("%1 dB").arg(value));
        emit settingChanged(QStringLiteral("pureSignal/rxFeedbackAtten"), value);
    });
}

// ── populate ──────────────────────────────────────────────────────────────────

void PureSignalTab::populate(const RadioInfo& /*info*/, const BoardCapabilities& caps)
{
    if (!caps.hasPureSignal) {
        m_stack->setCurrentIndex(0);
        return;
    }

    m_stack->setCurrentIndex(1);

    // Cap the slider range to the board attenuator range if present.
    // Source: PSForm.cs AutoAttenuate range — hardware attenuator 0..31 dB.
    if (caps.attenuator.present) {
        QSignalBlocker blocker(m_rxFeedbackAttenSlider);
        int maxDb = qBound(0, caps.attenuator.maxDb, 31);
        m_rxFeedbackAttenSlider->setRange(0, maxDb);
        m_rxAttenValueLabel->setText(
            QStringLiteral("%1 dB").arg(m_rxFeedbackAttenSlider->value()));
    }
}

// ── restoreSettings ───────────────────────────────────────────────────────────

void PureSignalTab::restoreSettings(const QMap<QString, QVariant>& settings)
{
    auto it = settings.constFind(QStringLiteral("enabled"));
    if (it != settings.constEnd()) {
        QSignalBlocker blocker(m_enableCheck);
        m_enableCheck->setChecked(it.value().toBool());
    }

    it = settings.constFind(QStringLiteral("feedbackSource"));
    if (it != settings.constEnd()) {
        const int src = it.value().toInt();
        QSignalBlocker blocker(m_feedbackSourceCombo);
        for (int i = 0; i < m_feedbackSourceCombo->count(); ++i) {
            if (m_feedbackSourceCombo->itemData(i).toInt() == src) {
                m_feedbackSourceCombo->setCurrentIndex(i);
                break;
            }
        }
    }

    it = settings.constFind(QStringLiteral("autoCalOnBandChange"));
    if (it != settings.constEnd()) {
        QSignalBlocker blocker(m_autoCalOnBandChangeCheck);
        m_autoCalOnBandChangeCheck->setChecked(it.value().toBool());
    }

    it = settings.constFind(QStringLiteral("preserveCalibration"));
    if (it != settings.constEnd()) {
        QSignalBlocker blocker(m_preserveCalCheck);
        m_preserveCalCheck->setChecked(it.value().toBool());
    }

    it = settings.constFind(QStringLiteral("rxFeedbackAtten"));
    if (it != settings.constEnd()) {
        QSignalBlocker blocker(m_rxFeedbackAttenSlider);
        const int val = it.value().toInt();
        m_rxFeedbackAttenSlider->setValue(val);
        m_rxAttenValueLabel->setText(QStringLiteral("%1 dB").arg(val));
    }
}

} // namespace NereusSDR
