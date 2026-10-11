// SQ4KOU: RX Speakers and PC microphone TX use the same native cmASIO session.
// Independent opt-in; existing RX Speakers settings are preserved.
#include "AudioCmAsioPage.h"

#include "core/audio/CmAsioBus.h"
#include "core/AppSettings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

namespace NereusSDR {

AudioCmAsioPage::AudioCmAsioPage(RadioModel* model, QWidget* parent)
    : SetupPage(QStringLiteral("cmASIO"), model, parent)
{
    const CmAsioSettings saved = CmAsioSettings::load();
    m_loading = true;

    auto* group = new QGroupBox(QStringLiteral("ChannelMaster ASIO — Speakers RX"), this);
    auto* form = new QFormLayout(group);

    m_enabled = new QCheckBox(QStringLiteral("Use cmASIO for RX Speakers (opt-in)"), group);
    form->addRow(m_enabled);
    m_txEnabled = new QCheckBox(QStringLiteral("Use cmASIO for PC Microphone TX (opt-in)"), group);
    form->addRow(m_txEnabled);

    auto* driverRow = new QWidget(group);
    auto* driverLay = new QHBoxLayout(driverRow);
    driverLay->setContentsMargins(0, 0, 0, 0);
    m_driver = new QComboBox(driverRow);
    auto* rescan = new QPushButton(QStringLiteral("Rescan"), driverRow);
    driverLay->addWidget(m_driver, 1);
    driverLay->addWidget(rescan);
    form->addRow(QStringLiteral("ASIO driver:"), driverRow);

    m_outputPair = new QComboBox(group);
    for (int base = 0; base <= 62; ++base) {
        m_outputPair->addItem(QStringLiteral("Channels %1 + %2")
                                  .arg(base + 1).arg(base + 2), base);
    }
    form->addRow(QStringLiteral("Output pair (RX):"), m_outputPair);

    m_inputPair = new QComboBox(group);
    for (int base = 0; base <= 62; ++base) {
        m_inputPair->addItem(QStringLiteral("Channels %1 + %2")
                                 .arg(base + 1).arg(base + 2), base);
    }
    form->addRow(QStringLiteral("Input pair (TX):"), m_inputPair);

    m_inputMode = new QComboBox(group);
    m_inputMode->addItem(QStringLiteral("Left"), 0);
    m_inputMode->addItem(QStringLiteral("Right"), 1);
    m_inputMode->addItem(QStringLiteral("Both (L+R)/2"), 2);
    form->addRow(QStringLiteral("TX microphone channel:"), m_inputMode);

    m_block = new QComboBox(group);
    for (int frames : {32, 64, 128, 256, 512, 1024, 2048}) {
        m_block->addItem(QStringLiteral("%1 frames (%2 ms @ 48 kHz)")
                             .arg(frames).arg(frames / 48.0, 0, 'f', 2), frames);
    }
    form->addRow(QStringLiteral("ASIO callback block:"), m_block);

    // Unlike callback block size, Block Num controls queued callback blocks.
    auto* blockNumRow = new QWidget(group);
    auto* blockNumLayout = new QHBoxLayout(blockNumRow);
    blockNumLayout->setContentsMargins(0, 0, 0, 0);
    m_blockNum = new QSpinBox(blockNumRow);
    m_blockNum->setRange(2, 128);
    m_blockNum->setToolTip(QStringLiteral("FIFO depth in ASIO callback blocks. Does not change the driver callback size."));
    auto* blockNumDefault = new QPushButton(QStringLiteral("Default"), blockNumRow);
    blockNumLayout->addWidget(m_blockNum);
    blockNumLayout->addWidget(blockNumDefault);
    blockNumLayout->addStretch(1);
    form->addRow(QStringLiteral("Block Num:"), blockNumRow);

    m_dll = new QLabel(group);
    m_dll->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_dll->setWordWrap(true);
    form->addRow(QStringLiteral("cmASIO.dll:"), m_dll);

    auto* info = new QLabel(
        QStringLiteral("RX and PC Mic TX have independent opt-in switches. "
                       "A single x64 cmASIO.dll session is shared for full duplex. "
                       "Select PC as Mic Source under Audio > TX Input for TX audio. "
                       "When TX ASIO cannot open, PC capture uses its existing "
                       "helper (PortAudio/WASAPI). WDSP/TCI/VAX are unchanged. "
                       "A restart is required after changing cmASIO options."), group);
    info->setWordWrap(true);
    form->addRow(info);

    m_status = new QLabel(group);
    m_status->setWordWrap(true);
    form->addRow(QStringLiteral("State:"), m_status);
    contentLayout()->insertWidget(0, group);

    rescanDrivers();
    if (!saved.driverName.isEmpty()
        && m_driver->findText(saved.driverName, Qt::MatchFixedString) < 0) {
        m_driver->insertItem(0, saved.driverName);
    }
    m_driver->setCurrentIndex(m_driver->findText(saved.driverName, Qt::MatchFixedString));
    m_enabled->setChecked(saved.enabled);
    m_txEnabled->setChecked(saved.txEnabled);
    m_inputPair->setCurrentIndex(qBound(0, saved.inputBase, 62));
    m_inputMode->setCurrentIndex(qBound(0, saved.inputMode, 2));
    m_outputPair->setCurrentIndex(qBound(0, saved.outputBase, 62));
    int ix = m_block->findData(saved.blockSamples);
    if (ix < 0) {
        m_block->addItem(QString::number(saved.blockSamples), saved.blockSamples);
        ix = m_block->findData(saved.blockSamples);
    }
    m_block->setCurrentIndex(ix);
    m_blockNum->setValue(saved.blockNum);
    m_loading = false;

    connect(rescan, &QPushButton::clicked, this, [this]() { rescanDrivers(); updateStatus(); });
    connect(m_enabled, &QCheckBox::toggled, this, [this]() { saveSelection(); });
    connect(m_txEnabled, &QCheckBox::toggled, this, [this]() { saveSelection(); });
    connect(m_inputPair, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { saveSelection(); });
    connect(m_inputMode, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { saveSelection(); });
    connect(m_driver, &QComboBox::currentTextChanged, this, [this]() { saveSelection(); });
    connect(m_outputPair, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { saveSelection(); });
    connect(m_block, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { saveSelection(); });
    connect(m_blockNum, qOverload<int>(&QSpinBox::valueChanged),
            this, [this](int) { saveSelection(); });
    connect(blockNumDefault, &QPushButton::clicked, this, [this]() {
        m_blockNum->setValue(16);
        saveSelection();
    });
    updateStatus();
}

void AudioCmAsioPage::rescanDrivers()
{
    if (!m_driver) { return; }
    const QString selected = m_driver->currentText();
    QSignalBlocker blocker(m_driver);
    m_driver->clear();
    for (const auto& info : CmAsioBus::availableDevices()) {
        m_driver->addItem(info.name);
    }
    if (!selected.isEmpty() && m_driver->findText(selected, Qt::MatchFixedString) < 0) {
        m_driver->insertItem(0, selected);
    }
    const int index = m_driver->findText(selected, Qt::MatchFixedString);
    if (index >= 0) { m_driver->setCurrentIndex(index); }
    else if (m_driver->count() > 0) { m_driver->setCurrentIndex(0); }
}

void AudioCmAsioPage::saveSelection()
{
    if (m_loading) { return; }
    auto cfg = CmAsioSettings::load();
    cfg.enabled = m_enabled->isChecked();
    cfg.txEnabled = m_txEnabled->isChecked();
    cfg.inputBase = m_inputPair->currentData().toInt();
    cfg.inputMode = m_inputMode->currentData().toInt();
    cfg.driverName = m_driver->currentText().trimmed().left(31);
    cfg.outputBase = m_outputPair->currentData().toInt();
    cfg.blockSamples = m_block->currentData().toInt();
    cfg.blockNum = m_blockNum->value();
    if ((cfg.enabled || cfg.txEnabled) && cfg.driverName.isEmpty()) {
        cfg.enabled = false;
        cfg.txEnabled = false;
        QSignalBlocker rxBlock(m_enabled);
        QSignalBlocker txBlock(m_txEnabled);
        m_enabled->setChecked(false);
        m_txEnabled->setChecked(false);
    }
    cfg.save();
    QString persistError;
    if (!AppSettings::instance().save(&persistError)) {
        updateStatus(QStringLiteral("CmASIO settings were NOT saved: ") + persistError);
        return;
    }
    updateStatus(QStringLiteral("Saved to disk. Restart NereusSDR to apply."));
}

void AudioCmAsioPage::updateStatus(const QString& note)
{
    const auto cfg = CmAsioSettings::load();
    m_driver->setEnabled(m_enabled->isChecked() || m_txEnabled->isChecked());
    m_inputPair->setEnabled(m_txEnabled->isChecked());
    m_inputMode->setEnabled(m_txEnabled->isChecked());
    m_outputPair->setEnabled(m_enabled->isChecked());
    m_block->setEnabled(m_enabled->isChecked() || m_txEnabled->isChecked());
    m_blockNum->setEnabled(m_enabled->isChecked() || m_txEnabled->isChecked());
    m_dll->setText((CmAsioBus::dllAvailable()
                       ? QStringLiteral("FOUND: ") : QStringLiteral("MISSING: "))
                   + CmAsioBus::dllPath());
    m_status->setText((note.isEmpty() ? QString() : note + QLatin1Char('\n'))
                      + QStringLiteral("RX %1 | TX %2 | %3 | OUT %4+%5 | IN %6+%7 (%8) | %9 frames | Block Num %10")
                            .arg(cfg.enabled ? QStringLiteral("ON") : QStringLiteral("OFF"))
                            .arg(cfg.txEnabled ? QStringLiteral("ON") : QStringLiteral("OFF"))
                            .arg(cfg.driverName.isEmpty() ? QStringLiteral("no driver") : cfg.driverName)
                            .arg(cfg.outputBase + 1).arg(cfg.outputBase + 2)
                            .arg(cfg.inputBase + 1).arg(cfg.inputBase + 2)
                            .arg(cfg.inputMode == 0 ? QStringLiteral("L") :
                                 cfg.inputMode == 1 ? QStringLiteral("R") : QStringLiteral("L+R"))
                            .arg(cfg.blockSamples)
                            .arg(cfg.blockNum));
}

} // namespace NereusSDR
