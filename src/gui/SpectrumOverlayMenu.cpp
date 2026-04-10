#include "SpectrumOverlayMenu.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>

namespace NereusSDR {

SpectrumOverlayMenu::SpectrumOverlayMenu(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_StyledBackground, true);
    buildUI();
}

void SpectrumOverlayMenu::buildUI()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);
    layout->setSpacing(4);

    // Dark theme matching NereusSDR STYLEGUIDE
    setStyleSheet(QStringLiteral(
        "SpectrumOverlayMenu {"
        "  background: #1a2a3a;"
        "  border: 1px solid #2e4e6e;"
        "  border-radius: 4px;"
        "}"
        "QLabel { color: #c8d8e8; font-size: 11px; }"
        "QSlider::groove:horizontal {"
        "  height: 4px; background: #203040; border-radius: 2px;"
        "}"
        "QSlider::handle:horizontal {"
        "  width: 12px; height: 12px; margin: -4px 0;"
        "  background: #00b4d8; border-radius: 6px;"
        "}"
        "QComboBox {"
        "  background: #203040; color: #c8d8e8; border: 1px solid #2e4e6e;"
        "  border-radius: 3px; padding: 2px 6px; font-size: 11px;"
        "}"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView {"
        "  background: #1a2a3a; color: #c8d8e8; selection-background-color: #00b4d8;"
        "}"
        "QCheckBox { color: #c8d8e8; font-size: 11px; }"
        "QCheckBox::indicator { width: 14px; height: 14px; }"));

    // --- Waterfall section ---
    auto* wfLabel = new QLabel(QStringLiteral("Waterfall"), this);
    wfLabel->setStyleSheet(QStringLiteral("font-weight: bold; color: #00b4d8;"));
    layout->addWidget(wfLabel);

    // Color scheme
    auto* schemeRow = new QHBoxLayout;
    schemeRow->addWidget(new QLabel(QStringLiteral("Color Scheme"), this));
    m_schemeCombo = new QComboBox(this);
    m_schemeCombo->addItems({
        QStringLiteral("Default"),
        QStringLiteral("Enhanced"),
        QStringLiteral("Spectran"),
        QStringLiteral("Black & White")
    });
    schemeRow->addWidget(m_schemeCombo);
    layout->addLayout(schemeRow);

    connect(m_schemeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SpectrumOverlayMenu::wfColorSchemeChanged);

    // Color gain
    auto* gainRow = new QHBoxLayout;
    gainRow->addWidget(new QLabel(QStringLiteral("Color Gain"), this));
    m_wfGainSlider = new QSlider(Qt::Horizontal, this);
    m_wfGainSlider->setRange(0, 100);
    m_wfGainSlider->setToolTip(QStringLiteral("Waterfall color intensity — higher values increase contrast"));
    m_wfGainLabel = new QLabel(this);
    gainRow->addWidget(m_wfGainSlider);
    gainRow->addWidget(m_wfGainLabel);
    layout->addLayout(gainRow);

    connect(m_wfGainSlider, &QSlider::valueChanged, this, [this](int v) {
        m_wfGainLabel->setText(QString::number(v));
        emit wfColorGainChanged(v);
    });

    // Black level
    auto* blackRow = new QHBoxLayout;
    blackRow->addWidget(new QLabel(QStringLiteral("Black Level"), this));
    m_wfBlackSlider = new QSlider(Qt::Horizontal, this);
    m_wfBlackSlider->setRange(0, 125);
    m_wfBlackSlider->setToolTip(QStringLiteral("Noise floor suppression — higher hides more noise"));
    m_wfBlackLabel = new QLabel(this);
    blackRow->addWidget(m_wfBlackSlider);
    blackRow->addWidget(m_wfBlackLabel);
    layout->addLayout(blackRow);

    connect(m_wfBlackSlider, &QSlider::valueChanged, this, [this](int v) {
        m_wfBlackLabel->setText(QString::number(v));
        emit wfBlackLevelChanged(v);
    });

    // --- Spectrum section ---
    auto* specLabel = new QLabel(QStringLiteral("Spectrum"), this);
    specLabel->setStyleSheet(QStringLiteral("font-weight: bold; color: #00b4d8; margin-top: 6px;"));
    layout->addWidget(specLabel);

    // Pan fill
    m_panFillCheck = new QCheckBox(QStringLiteral("Fill spectrum trace"), this);
    m_panFillCheck->setToolTip(QStringLiteral("Fill the area below the spectrum trace"));
    layout->addWidget(m_panFillCheck);

    connect(m_panFillCheck, &QCheckBox::toggled, this, &SpectrumOverlayMenu::panFillChanged);

    // Fill alpha
    auto* alphaRow = new QHBoxLayout;
    alphaRow->addWidget(new QLabel(QStringLiteral("Fill Alpha"), this));
    m_fillAlphaSlider = new QSlider(Qt::Horizontal, this);
    m_fillAlphaSlider->setRange(0, 100);
    m_fillAlphaSlider->setToolTip(QStringLiteral("Transparency of the spectrum fill area"));
    m_fillAlphaLabel = new QLabel(this);
    alphaRow->addWidget(m_fillAlphaSlider);
    alphaRow->addWidget(m_fillAlphaLabel);
    layout->addLayout(alphaRow);

    connect(m_fillAlphaSlider, &QSlider::valueChanged, this, [this](int v) {
        m_fillAlphaLabel->setText(QStringLiteral("%1%").arg(v));
        emit fillAlphaChanged(static_cast<float>(v) / 100.0f);
    });

    // --- Display Range section ---
    auto* rangeLabel = new QLabel(QStringLiteral("Display Range"), this);
    rangeLabel->setStyleSheet(QStringLiteral("font-weight: bold; color: #00b4d8; margin-top: 6px;"));
    layout->addWidget(rangeLabel);

    // Ref level
    auto* refRow = new QHBoxLayout;
    refRow->addWidget(new QLabel(QStringLiteral("Ref Level"), this));
    m_refLevelSlider = new QSlider(Qt::Horizontal, this);
    m_refLevelSlider->setRange(-160, 20);
    m_refLevelSlider->setToolTip(QStringLiteral("Maximum signal level shown at top of display (dBm)"));
    m_refLevelLabel = new QLabel(this);
    refRow->addWidget(m_refLevelSlider);
    refRow->addWidget(m_refLevelLabel);
    layout->addLayout(refRow);

    connect(m_refLevelSlider, &QSlider::valueChanged, this, [this](int v) {
        m_refLevelLabel->setText(QStringLiteral("%1 dBm").arg(v));
        emit refLevelChanged(static_cast<float>(v));
    });

    // Dynamic range
    auto* dynRow = new QHBoxLayout;
    dynRow->addWidget(new QLabel(QStringLiteral("Dyn Range"), this));
    m_dynRangeSlider = new QSlider(Qt::Horizontal, this);
    m_dynRangeSlider->setRange(20, 160);
    m_dynRangeSlider->setToolTip(QStringLiteral("Visible dB range from ref level to bottom of display"));
    m_dynRangeLabel = new QLabel(this);
    dynRow->addWidget(m_dynRangeSlider);
    dynRow->addWidget(m_dynRangeLabel);
    layout->addLayout(dynRow);

    connect(m_dynRangeSlider, &QSlider::valueChanged, this, [this](int v) {
        m_dynRangeLabel->setText(QStringLiteral("%1 dB").arg(v));
        emit dynRangeChanged(static_cast<float>(v));
    });

    // --- Tuning Mode section ---
    auto* tuneLabel = new QLabel(QStringLiteral("Tuning"), this);
    tuneLabel->setStyleSheet(QStringLiteral("font-weight: bold; color: #00b4d8; margin-top: 6px;"));
    layout->addWidget(tuneLabel);

    m_ctunCheck = new QCheckBox(QStringLiteral("CTUN (independent pan)"), this);
    m_ctunCheck->setToolTip(QStringLiteral(
        "SmartSDR-style: pan stays fixed, VFO moves within it.\n"
        "Off: traditional — pan follows VFO."));
    layout->addWidget(m_ctunCheck);

    connect(m_ctunCheck, &QCheckBox::toggled, this, &SpectrumOverlayMenu::ctunChanged);

    setFixedWidth(280);
}

void SpectrumOverlayMenu::setValues(int wfColorGain, int wfBlackLevel, bool autoBlack,
                                     int wfScheme, float fillAlpha, bool panFill,
                                     bool heatMap, float refLevel, float dynRange,
                                     bool ctunEnabled)
{
    Q_UNUSED(autoBlack);
    Q_UNUSED(heatMap);

    // Block signals to avoid feedback loops while setting initial values
    m_wfGainSlider->blockSignals(true);
    m_wfGainSlider->setValue(wfColorGain);
    m_wfGainLabel->setText(QString::number(wfColorGain));
    m_wfGainSlider->blockSignals(false);

    m_wfBlackSlider->blockSignals(true);
    m_wfBlackSlider->setValue(wfBlackLevel);
    m_wfBlackLabel->setText(QString::number(wfBlackLevel));
    m_wfBlackSlider->blockSignals(false);

    m_schemeCombo->blockSignals(true);
    m_schemeCombo->setCurrentIndex(qBound(0, wfScheme, m_schemeCombo->count() - 1));
    m_schemeCombo->blockSignals(false);

    m_fillAlphaSlider->blockSignals(true);
    m_fillAlphaSlider->setValue(static_cast<int>(fillAlpha * 100.0f));
    m_fillAlphaLabel->setText(QStringLiteral("%1%").arg(static_cast<int>(fillAlpha * 100.0f)));
    m_fillAlphaSlider->blockSignals(false);

    m_panFillCheck->blockSignals(true);
    m_panFillCheck->setChecked(panFill);
    m_panFillCheck->blockSignals(false);

    m_refLevelSlider->blockSignals(true);
    m_refLevelSlider->setValue(static_cast<int>(refLevel));
    m_refLevelLabel->setText(QStringLiteral("%1 dBm").arg(static_cast<int>(refLevel)));
    m_refLevelSlider->blockSignals(false);

    m_dynRangeSlider->blockSignals(true);
    m_dynRangeSlider->setValue(static_cast<int>(dynRange));
    m_dynRangeLabel->setText(QStringLiteral("%1 dB").arg(static_cast<int>(dynRange)));
    m_dynRangeSlider->blockSignals(false);

    m_ctunCheck->blockSignals(true);
    m_ctunCheck->setChecked(ctunEnabled);
    m_ctunCheck->blockSignals(false);
}

} // namespace NereusSDR
