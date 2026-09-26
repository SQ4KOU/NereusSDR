// =================================================================
// src/gui/diagnostics/DiagnosticsPhaseHPages.cpp  (NereusSDR)
// =================================================================
//
// NereusSDR-original. Implementation for the four sibling Diagnostics
// sub-tabs added in Phase 3P-H. See header for scope.
// =================================================================
//
// Modification history (NereusSDR):
//   2026-04-20 — Original implementation for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted implementation via
//                 Anthropic Claude Code.
//   2026-09-23 - R-R3-21 / R-R3-10: Settings Validation's Reset and Forget change
//                 the radio's settings, which a remote window holds on
//                 the Core; they are disabled while it does not have them.
//                 J.J. Boyd (KG4VCF), with AI-assisted implementation via
//                 Anthropic Claude Code.
//   2026-09-23 - R3 Setup fix wave (R-R3-21, R-R3-10): Reset and Forget
//                 re-check that availability after their question
//                 returns. J.J. Boyd (KG4VCF), with AI-assisted
//                 implementation via Anthropic Claude Code.
//   2026-09-23 - R-R3-21: Connection Quality's "EP6 sequence gaps" row
//                 shows the EP6 sequence error count, not the throttle
//                 event count. J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
//   2026-09-24 - R-R3-49: Connection Quality's 60 s history group is
//                 hidden until the history graph is built. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-09-26 - R-R3-32 (remote-window parity Task 14): the Connection
//                Quality figures from RadioModel::hl2LinkFigures(), the
//                Core's HL2 link in a remote window and said so;
//                unavailable, never 0, when absent. J.J. Boyd (KG4VCF),
//                AI-assisted via Anthropic Claude Code.
// =================================================================

#include "DiagnosticsPhaseHPages.h"

#include "core/AppSettings.h"
#include "core/BoardCapabilities.h"
#include "core/HermesLiteBandwidthMonitor.h"
#include "core/SettingsHygiene.h"
#include "models/RadioModel.h"
#include "gui/SupportDialog.h"
#include "gui/UnbuiltFeatures.h"

#include <QFile>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>

namespace NereusSDR {

// ── ConnectionQualityPage ────────────────────────────────────────────────────

ConnectionQualityPage::ConnectionQualityPage(RadioModel* model, QWidget* parent)
    : SetupPage(QStringLiteral("Connection Quality"), model, parent)
    , m_model(model)
{
    buildUI();
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &ConnectionQualityPage::onTick);
    timer->start(500);
    onTick();
}

void ConnectionQualityPage::buildUI()
{
    // addSection() already installs a QVBoxLayout on `group` (and on
    // `histGroup` below). Creating a second `new QVBoxLayout(group)` here
    // triggers the QLayout "already has a layout" runtime warning — #272
    // captured 7 of these in the W4ORS May 16 log, all from this file.
    // Reuse the layout addSection() already installed instead.
    auto* group = addSection(QStringLiteral("Live Counters"));
    m_liveGroup = group;
    auto* form = qobject_cast<QVBoxLayout*>(group->layout());

    auto addRow = [&](const QString& label, QLabel*& out) {
        auto* row = new QHBoxLayout();
        auto* lab = new QLabel(label);
        lab->setMinimumWidth(180);
        out = new QLabel(QStringLiteral("–"));
        row->addWidget(lab);
        row->addWidget(out, 1);
        form->addLayout(row);
    };

    addRow(QStringLiteral("EP6 bytes received:"),  m_ep6BytesLabel);
    addRow(QStringLiteral("EP2 bytes sent:"),      m_ep2BytesLabel);
    addRow(QStringLiteral("LAN PHY throttle:"),    m_throttleLabel);
    addRow(QStringLiteral("EP6 sequence gaps:"),   m_seqGapLabel);

    auto* histGroup = addSection(QStringLiteral("60 s History"));
    auto* histLayout = qobject_cast<QVBoxLayout*>(histGroup->layout());
    m_historyPlaceholder = new QLabel(
        QStringLiteral("The 60 s history graph is not shown yet."));
    m_historyPlaceholder->setStyleSheet(QStringLiteral("color: #888;"));
    histLayout->addWidget(m_historyPlaceholder);
    histGroup->setObjectName(QStringLiteral("connectionHistoryGroup"));
    UnbuiltFeatures::hideUnlessBuilt(histGroup, UnbuiltFeature::ConnectionHistory);

    contentLayout()->addStretch();
}

void ConnectionQualityPage::onTick()
{
    if (m_model == nullptr) { return; }
    // R-R3-32 (parity Task 14): this window's HL2 link, or in a remote
    // window the Core's; one the Core has not sent shows as unavailable.
    const RadioModel::Hl2LinkFigures figures = m_model->hl2LinkFigures();
    const bool fromCore = m_model->hl2LinkFiguresFromCore();
    const QString unavailable = tr("Unavailable");
    if (m_liveGroup) {
        m_liveGroup->setTitle(fromCore ? tr("Live Counters, from the Core")
                                       : tr("Live Counters"));
    }
    const auto bytes = [&unavailable](std::optional<double> bps) {
        return bps ? QString::number(*bps, 'f', 0) + QStringLiteral(" B/s") : unavailable;
    };
    m_ep6BytesLabel->setText(bytes(figures.rxBytesPerSecond));
    m_ep2BytesLabel->setText(bytes(figures.txBytesPerSecond));
    m_throttleLabel->setText(!figures.throttled ? unavailable
                             : *figures.throttled ? QStringLiteral("THROTTLED")
                                                  : QStringLiteral("ok"));
    // R-R3-21: the row names EP6 sequence gaps; it showed the LAN throttle
    // event count (the row above already reports throttling).
    m_seqGapLabel->setText(figures.sequenceGaps ? QString::number(*figures.sequenceGaps)
                                                : unavailable);
    const QString source = fromCore ? tr("From the Core") : QString();
    for (QLabel* label : {m_ep6BytesLabel, m_ep2BytesLabel, m_throttleLabel, m_seqGapLabel}) {
        label->setToolTip(source);
    }
}

// ── SettingsValidationPage ───────────────────────────────────────────────────

SettingsValidationPage::SettingsValidationPage(RadioModel* model, QWidget* parent)
    : SetupPage(QStringLiteral("Settings Validation"), model, parent)
    , m_model(model)
{
    buildUI();
    refresh();
    if (m_model != nullptr) {
        connect(&m_model->settingsHygiene(), &SettingsHygiene::issuesChanged,
                this, &SettingsValidationPage::refresh);
    }
}

void SettingsValidationPage::buildUI()
{
    // Reuse the layout addSection() installs (see ConnectionQualityPage::buildUI
    // for context — #272).
    auto* group = addSection(QStringLiteral("Validation Issues"));
    auto* layout = qobject_cast<QVBoxLayout*>(group->layout());

    m_issueList = new QListWidget;
    m_issueList->setStyleSheet(
        QStringLiteral("QListWidget { background: #0a0a18; color: #c8d8e8; "
                       "border: 1px solid #304050; }"));
    m_issueList->setMinimumHeight(220);
    layout->addWidget(m_issueList);

    auto* btnRow = new QHBoxLayout;
    m_refreshBtn = new QPushButton(QStringLiteral("Re-validate"));
    m_resetBtn   = new QPushButton(QStringLiteral("Reset to Defaults"));
    m_forgetBtn  = new QPushButton(QStringLiteral("Forget This Radio"));
    btnRow->addWidget(m_refreshBtn);
    btnRow->addWidget(m_resetBtn);
    btnRow->addWidget(m_forgetBtn);
    btnRow->addStretch();
    layout->addLayout(btnRow);

    connect(m_refreshBtn, &QPushButton::clicked, this, &SettingsValidationPage::refresh);
    connect(m_resetBtn,   &QPushButton::clicked, this, &SettingsValidationPage::onResetClicked);
    connect(m_forgetBtn,  &QPushButton::clicked, this, &SettingsValidationPage::onForgetClicked);

    contentLayout()->addStretch();
}

void SettingsValidationPage::refresh()
{
    m_issueList->clear();
    if (m_model == nullptr) {
        m_issueList->addItem(QStringLiteral("(no model)"));
        return;
    }
    const auto issues = m_model->settingsHygiene().issues();
    if (issues.isEmpty()) {
        m_issueList->addItem(QStringLiteral("✓ No issues: every setting is within this radio's range."));
        return;
    }
    for (const auto& issue : issues) {
        const QString sev =
            issue.severity == SettingsHygiene::Severity::Critical ? QStringLiteral("CRIT") :
            issue.severity == SettingsHygiene::Severity::Warning  ? QStringLiteral("WARN") :
                                                                    QStringLiteral("INFO");
        m_issueList->addItem(QStringLiteral("[%1] %2: %3")
                                 .arg(sev, issue.summary, issue.detail));
    }
}

void SettingsValidationPage::setStationSettingsAvailable(bool available, const QString& reason)
{
    m_stationSettingsAvailable = available;
    gateStationControls({m_resetBtn, m_forgetBtn}, available, reason);
}

void SettingsValidationPage::onResetClicked()
{
    if (m_model == nullptr) { return; }
    const auto reply = QMessageBox::question(
        this, QStringLiteral("Reset Settings"),
        QStringLiteral("Reset all per-board settings to defaults for this radio?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    // The Core's settings can go away while the question is open; Yes then
    // changes nothing (R3 Setup fix wave, final review M2).
    if (reply == QMessageBox::Yes && m_stationSettingsAvailable) {
        m_model->settingsHygiene().resetSettingsToDefaults(
            QString{}, m_model->boardCapabilities());
        refresh();
    }
}

void SettingsValidationPage::onForgetClicked()
{
    if (m_model == nullptr) { return; }
    const auto reply = QMessageBox::question(
        this, QStringLiteral("Forget Radio"),
        QStringLiteral("Forget all settings for this radio?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    // See onResetClicked(): re-checked after the question returns.
    if (reply == QMessageBox::Yes && m_stationSettingsAvailable) {
        m_model->settingsHygiene().forgetRadio(QString{});
        refresh();
    }
}

// ── ExportImportConfigPage ───────────────────────────────────────────────────

ExportImportConfigPage::ExportImportConfigPage(RadioModel* model, QWidget* parent)
    : SetupPage(QStringLiteral("Export / Import Config"), model, parent)
    , m_model(model)
{
    buildUI();
}

void ExportImportConfigPage::buildUI()
{
    // Reuse the layout addSection() installs (see ConnectionQualityPage::buildUI
    // for context — #272). Same pattern applies to allGroup + radioGroup below.
    auto* fileGroup = addSection(QStringLiteral("Settings File"));
    auto* fileLayout = qobject_cast<QVBoxLayout*>(fileGroup->layout());
    m_settingsPathLabel = new QLabel(
        QStringLiteral("Path: %1").arg(AppSettings::instance().filePath()));
    m_settingsPathLabel->setStyleSheet(QStringLiteral("color: #c8d8e8;"));
    m_settingsPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    fileLayout->addWidget(m_settingsPathLabel);

    auto* allGroup = addSection(QStringLiteral("Full Configuration (XML)"));
    auto* allLayout = qobject_cast<QVBoxLayout*>(allGroup->layout());
    auto* btnRow = new QHBoxLayout;
    m_exportAllBtn = new QPushButton(QStringLiteral("Export All Settings…"));
    m_importAllBtn = new QPushButton(QStringLiteral("Import All Settings…"));
    btnRow->addWidget(m_exportAllBtn);
    btnRow->addWidget(m_importAllBtn);
    btnRow->addStretch();
    allLayout->addLayout(btnRow);

    auto* radioGroup = addSection(QStringLiteral("Per-Radio Configuration"));
    auto* radioLayout = qobject_cast<QVBoxLayout*>(radioGroup->layout());
    m_radioSummaryLabel = new QLabel(
        QStringLiteral("Per-radio export will copy only the hardware/<mac>/* "
                       "subtree for the connected radio."));
    m_radioSummaryLabel->setWordWrap(true);
    m_radioSummaryLabel->setStyleSheet(QStringLiteral("color: #888;"));
    radioLayout->addWidget(m_radioSummaryLabel);
    m_exportRadioBtn = new QPushButton(QStringLiteral("Export Connected Radio…"));
    radioLayout->addWidget(m_exportRadioBtn);
    // R-R3-49 (export-radio): exporting one radio's settings is not built;
    // the group is hidden until it is. The button's code stays.
    radioGroup->setObjectName(QStringLiteral("exportRadioGroup"));
    UnbuiltFeatures::hideUnlessBuilt(radioGroup, UnbuiltFeature::ExportRadio);

    connect(m_exportAllBtn,   &QPushButton::clicked, this,
            &ExportImportConfigPage::onExportAllClicked);
    connect(m_importAllBtn,   &QPushButton::clicked, this,
            &ExportImportConfigPage::onImportAllClicked);
    connect(m_exportRadioBtn, &QPushButton::clicked, this,
            &ExportImportConfigPage::onExportRadioClicked);

    contentLayout()->addStretch();
}

void ExportImportConfigPage::onExportAllClicked()
{
    AppSettings::instance().save();
    const QString src = AppSettings::instance().filePath();
    const QString dst = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export Settings"),
        QStringLiteral("NereusSDR.settings.xml"),
        QStringLiteral("XML (*.xml *.settings)"));
    if (dst.isEmpty()) { return; }
    QFile::remove(dst);
    if (!QFile::copy(src, dst)) {
        QMessageBox::warning(this, QStringLiteral("Export Failed"),
                             QStringLiteral("Could not write to %1").arg(dst));
        return;
    }
    QMessageBox::information(this, QStringLiteral("Export Complete"),
                             QStringLiteral("Settings exported to:\n%1").arg(dst));
}

void ExportImportConfigPage::onImportAllClicked()
{
    const QString src = QFileDialog::getOpenFileName(
        this, QStringLiteral("Import Settings"), {},
        QStringLiteral("XML (*.xml *.settings)"));
    if (src.isEmpty()) { return; }
    const auto reply = QMessageBox::question(
        this, QStringLiteral("Replace Settings"),
        QStringLiteral("Replace current settings with the contents of\n%1?\n\n"
                       "A restart is required for changes to take effect.").arg(src),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes) { return; }
    const QString dst = AppSettings::instance().filePath();
    QFile::remove(dst);
    if (!QFile::copy(src, dst)) {
        QMessageBox::warning(this, QStringLiteral("Import Failed"),
                             QStringLiteral("Could not write to %1").arg(dst));
        return;
    }
    QMessageBox::information(
        this, QStringLiteral("Import Complete"),
        QStringLiteral("Settings imported. Please restart NereusSDR."));
}

void ExportImportConfigPage::onExportRadioClicked()
{
    QMessageBox::information(
        this, QStringLiteral("Per-Radio Export"),
        QStringLiteral("Exporting one radio's settings is not available yet. "
                       "Use 'Export All Settings' for now."));
}

// ── LogsPage ─────────────────────────────────────────────────────────────────

LogsPage::LogsPage(QWidget* parent)
    : SetupPage(QStringLiteral("Logs"), parent)
{
    buildUI();
}

void LogsPage::buildUI()
{
    // Reuse the layout addSection() installs (see ConnectionQualityPage::buildUI
    // for context — #272).
    auto* group = addSection(QStringLiteral("Recent Log"));
    auto* layout = qobject_cast<QVBoxLayout*>(group->layout());
    m_logView = new QPlainTextEdit;
    m_logView->setObjectName(QStringLiteral("logsView"));
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(2000);  // SupportDialog::kMaxLogViewLines
    m_logView->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background: #0a0a18; color: #c8d8e8; "
        "border: 1px solid #304050; font-family: 'Monaco','Menlo',monospace; }"));
    m_logView->setToolTip(QStringLiteral("The most recent lines of the NereusSDR log file"));
    m_logView->setMinimumHeight(280);
    layout->addWidget(m_logView);

    auto* buttons = new QHBoxLayout;
    m_refreshBtn = new QPushButton(QStringLiteral("Refresh"));
    m_refreshBtn->setToolTip(QStringLiteral("Read the log file again"));
    buttons->addWidget(m_refreshBtn);
    m_clearBtn = new QPushButton(QStringLiteral("Clear"));
    m_clearBtn->setToolTip(QStringLiteral("Clear this view (the log file is kept)"));
    buttons->addWidget(m_clearBtn);
    buttons->addStretch(1);
    layout->addLayout(buttons);
    connect(m_refreshBtn, &QPushButton::clicked, this, &LogsPage::refresh);
    connect(m_clearBtn, &QPushButton::clicked, m_logView, &QPlainTextEdit::clear);

    contentLayout()->addStretch();
    refresh();
}

void LogsPage::refresh()
{
    m_logView->setPlainText(SupportDialog::logTailText());
    m_logView->moveCursor(QTextCursor::End);
}

void LogsPage::showEvent(QShowEvent* event)
{
    SetupPage::showEvent(event);
    refresh();
}

} // namespace NereusSDR
