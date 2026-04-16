// RadioInfoTab.cpp
//
// Source: Thetis Setup.cs Hardware Config / General — board model label,
// sample-rate combo (comboGeneralSampleRate), active-RX count spinbox
// (numericUpDownNr), firmware/MAC/IP read-only text fields.
// Setup.cs:847-850 — sample rate list filtered by model.
// Setup.cs:135 — HardwareSpecific.Model set from detected board.

#include "RadioInfoTab.h"

#include "core/BoardCapabilities.h"
#include "core/HardwareProfile.h"
#include "core/HpsdrModel.h"
#include "core/RadioDiscovery.h"
#include "core/SampleRateCatalog.h"
#include "models/RadioModel.h"

#include <QClipboard>
#include <QComboBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace NereusSDR {

RadioInfoTab::RadioInfoTab(RadioModel* model, QWidget* parent)
    : QWidget(parent), m_model(model)
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(8, 8, 8, 8);

    // ── Identity group ────────────────────────────────────────────────────────
    auto* identGroup = new QGroupBox(tr("Board Identity"), this);
    auto* form = new QFormLayout(identGroup);
    form->setLabelAlignment(Qt::AlignRight);

    m_boardLabel    = new QLabel(QStringLiteral("—"), identGroup);
    m_protocolLabel = new QLabel(QStringLiteral("—"), identGroup);
    m_adcCountLabel = new QLabel(QStringLiteral("—"), identGroup);
    m_maxRxLabel    = new QLabel(QStringLiteral("—"), identGroup);
    m_firmwareLabel = new QLabel(QStringLiteral("—"), identGroup);
    m_macLabel      = new QLabel(QStringLiteral("—"), identGroup);
    m_ipLabel       = new QLabel(QStringLiteral("—"), identGroup);

    for (QLabel* lbl : {m_boardLabel, m_protocolLabel, m_adcCountLabel,
                        m_maxRxLabel, m_firmwareLabel, m_macLabel, m_ipLabel}) {
        lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    }

    form->addRow(tr("Board:"),      m_boardLabel);
    form->addRow(tr("Protocol:"),   m_protocolLabel);
    form->addRow(tr("ADC count:"),  m_adcCountLabel);
    form->addRow(tr("Max RX:"),     m_maxRxLabel);
    form->addRow(tr("Firmware:"),   m_firmwareLabel);
    form->addRow(tr("MAC:"),        m_macLabel);
    form->addRow(tr("IP address:"), m_ipLabel);

    outerLayout->addWidget(identGroup);

    // ── Operating parameters group ────────────────────────────────────────────
    auto* paramGroup = new QGroupBox(tr("Operating Parameters"), this);
    auto* paramForm  = new QFormLayout(paramGroup);
    paramForm->setLabelAlignment(Qt::AlignRight);

    // RX1 sample rate combo — entries populated in populate() from
    // allowedSampleRates(proto, caps, model). Matches Thetis setup.cs:847-852.
    m_sampleRateRx1Combo = new QComboBox(paramGroup);
    m_sampleRateRx1Combo->setMinimumWidth(120);

    // RX2 sample rate combo — disabled stub in PR #35. Thetis exposes an
    // independent RX2 rate (setup.cs comboAudioSampleRateRX2). When Phase 3F
    // multi-panadapter lands, this combo becomes live with these gating rules
    // (from setup.cs:7065-7073 and 7155-7156):
    //   • P1 (all boards): RX2 forced equal to RX1, combo disabled.
    //   • P2 ANAN-10E / ANAN-100B: RX2 forced equal to RX1 (single-ADC).
    //   • P2 other boards: RX2 independent.
    m_sampleRateRx2Combo = new QComboBox(paramGroup);
    m_sampleRateRx2Combo->setMinimumWidth(120);
    m_sampleRateRx2Combo->setEnabled(false);
    m_sampleRateRx2Combo->setToolTip(
        tr("Enabled when Phase 3F multi-panadapter support lands."));

    // Active RX count spinbox — capped at caps.maxReceivers in populate()
    // Source: Thetis Setup.cs numericUpDownNr, range 1..board_max_rx
    m_activeRxSpin = new QSpinBox(paramGroup);
    m_activeRxSpin->setRange(1, 1);
    m_activeRxSpin->setValue(1);
    m_activeRxSpin->setEnabled(false); // enabled when a radio is present

    paramForm->addRow(tr("RX1 sample rate (Hz):"), m_sampleRateRx1Combo);
    paramForm->addRow(tr("RX2 sample rate (Hz):"), m_sampleRateRx2Combo);
    paramForm->addRow(tr("Active RX count:"),  m_activeRxSpin);

    outerLayout->addWidget(paramGroup);

    // ── Support info button ───────────────────────────────────────────────────
    m_copySupportInfoButton = new QPushButton(tr("Copy Support Info to Clipboard"), this);
    m_copySupportInfoButton->setToolTip(
        tr("Copies board identity and firmware version to the clipboard for bug reports."));
    outerLayout->addWidget(m_copySupportInfoButton);
    outerLayout->addStretch();

    // ── Connections ───────────────────────────────────────────────────────────
    connect(m_sampleRateRx1Combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RadioInfoTab::onSampleRateChanged);

    connect(m_activeRxSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &RadioInfoTab::onActiveRxCountChanged);

    connect(m_copySupportInfoButton, &QPushButton::clicked, this, [this]() {
        QGuiApplication::clipboard()->setText(m_currentInfo);
    });
}

// ── populate ──────────────────────────────────────────────────────────────────

void RadioInfoTab::populate(const RadioInfo& info, const BoardCapabilities& caps)
{
    // Fill read-only identity labels
    m_boardLabel->setText(info.name.isEmpty()
        ? QString::fromLatin1(caps.displayName)
        : info.name);
    m_protocolLabel->setText(
        info.protocol == ProtocolVersion::Protocol2
            ? QStringLiteral("Protocol 2")
            : QStringLiteral("Protocol 1"));
    m_adcCountLabel->setText(QString::number(caps.adcCount));
    m_maxRxLabel->setText(QString::number(caps.maxReceivers));
    m_firmwareLabel->setText(
        info.firmwareVersion > 0
            ? QString::number(info.firmwareVersion)
            : QStringLiteral("—"));
    m_macLabel->setText(info.macAddress.isEmpty() ? QStringLiteral("—") : info.macAddress);
    m_ipLabel->setText(info.address.isNull()
        ? QStringLiteral("—")
        : info.address.toString());

    // Rebuild RX1 combo from allowedSampleRates(proto, caps, model) — matches
    // Thetis setup.cs:847-852 filtering (per-protocol list ∩ caps.sampleRates,
    // with the RedPitaya extra-384k exception). Default selection is 192000
    // per setup.cs:866; if absent, first allowed entry.
    HPSDRModel model = HPSDRModel::HERMES;
    if (m_model) {
        model = m_model->hardwareProfile().model;
    }
    const auto allowed = allowedSampleRates(info.protocol, caps, model);
    const int fallbackRate = defaultSampleRate(info.protocol, caps, model);
    {
        QSignalBlocker blocker(m_sampleRateRx1Combo);
        m_sampleRateRx1Combo->clear();
        for (int rate : allowed) {
            m_sampleRateRx1Combo->addItem(QStringLiteral("%1").arg(rate), rate);
        }
        // Default selection — 192000 (setup.cs:866).
        int idx = -1;
        for (int i = 0; i < m_sampleRateRx1Combo->count(); ++i) {
            if (m_sampleRateRx1Combo->itemData(i).toInt() == fallbackRate) {
                idx = i;
                break;
            }
        }
        if (idx < 0 && m_sampleRateRx1Combo->count() > 0) {
            idx = 0;
        }
        if (idx >= 0) {
            m_sampleRateRx1Combo->setCurrentIndex(idx);
        }
    }

    // RX2 combo mirrors RX1 items and selection (disabled stub).
    {
        QSignalBlocker blocker(m_sampleRateRx2Combo);
        m_sampleRateRx2Combo->clear();
        for (int rate : allowed) {
            m_sampleRateRx2Combo->addItem(QStringLiteral("%1").arg(rate), rate);
        }
        m_sampleRateRx2Combo->setCurrentIndex(m_sampleRateRx1Combo->currentIndex());
    }

    // Active-RX spinbox range 1..caps.maxReceivers
    // Source: Thetis Setup.cs numericUpDownNr
    {
        QSignalBlocker blocker(m_activeRxSpin);
        m_activeRxSpin->setRange(1, caps.maxReceivers);
        m_activeRxSpin->setValue(1);
        m_activeRxSpin->setEnabled(true);
    }

    // Build clipboard text for Copy Support Info button
    m_currentInfo = QStringLiteral(
        "Board: %1\n"
        "Protocol: %2\n"
        "ADC count: %3\n"
        "Max RX: %4\n"
        "Firmware: %5\n"
        "MAC: %6\n"
        "IP: %7\n"
        "Max sample rate: %8 Hz\n")
        .arg(m_boardLabel->text())
        .arg(m_protocolLabel->text())
        .arg(caps.adcCount)
        .arg(caps.maxReceivers)
        .arg(m_firmwareLabel->text())
        .arg(m_macLabel->text())
        .arg(m_ipLabel->text())
        .arg(caps.maxSampleRate);
}

// ── private slots ─────────────────────────────────────────────────────────────

void RadioInfoTab::onSampleRateChanged(int index)
{
    if (index < 0) { return; }
    int rate = m_sampleRateRx1Combo->itemData(index).toInt();
    if (rate > 0) {
        emit settingChanged(QStringLiteral("radioInfo/sampleRate"), rate);
    }
}

void RadioInfoTab::onActiveRxCountChanged(int count)
{
    if (count < 1) { return; }
    emit settingChanged(QStringLiteral("radioInfo/activeRxCount"), count);
}

// ── restoreSettings ───────────────────────────────────────────────────────────

void RadioInfoTab::restoreSettings(const QMap<QString, QVariant>& settings)
{
    // sampleRate — match combo item data
    auto srIt = settings.constFind(QStringLiteral("sampleRate"));
    if (srIt != settings.constEnd()) {
        const int rate = srIt.value().toInt();
        QSignalBlocker blocker(m_sampleRateRx1Combo);
        for (int i = 0; i < m_sampleRateRx1Combo->count(); ++i) {
            if (m_sampleRateRx1Combo->itemData(i).toInt() == rate) {
                m_sampleRateRx1Combo->setCurrentIndex(i);
                // Mirror into RX2 stub so it stays visually aligned.
                QSignalBlocker b2(m_sampleRateRx2Combo);
                m_sampleRateRx2Combo->setCurrentIndex(i);
                break;
            }
        }
    }

    // activeRxCount
    auto rxIt = settings.constFind(QStringLiteral("activeRxCount"));
    if (rxIt != settings.constEnd()) {
        QSignalBlocker blocker(m_activeRxSpin);
        m_activeRxSpin->setValue(rxIt.value().toInt());
    }
}

} // namespace NereusSDR
