#include "SupportDialog.h"
#include "StyleConstants.h"
#include "core/LogCategories.h"
#include "core/SupportBundle.h"
#include "models/RadioModel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFile>
#include <QFileInfo>
#include <QDesktopServices>
#include <QUrl>
#include <QFont>
#include <QApplication>
#include <QClipboard>
#include <QMessageBox>

namespace NereusSDR {

SupportDialog::SupportDialog(RadioModel* model, QWidget* parent)
    : QDialog(parent)
    , m_radioModel(model)
{
    setWindowTitle(QStringLiteral("Support & Diagnostics"));
    setMinimumSize(650, 550);
    resize(700, 650);

    buildUI();
    refreshLogViewer();
}

SupportDialog::~SupportDialog() = default;

void SupportDialog::buildUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);

    // --- Diagnostic Logging Categories ---
    auto* catGroup = new QGroupBox(QStringLiteral("Diagnostic Logging"), this);
    auto* catGrid = new QGridLayout(catGroup);
    catGrid->setSpacing(6);

    const auto& mgr = LogManager::instance();
    const auto cats = mgr.categories();
    int col = 0;
    int row = 0;
    for (const auto& cat : cats) {
        auto* cb = new QCheckBox(cat.label, catGroup);
        cb->setToolTip(cat.description);
        cb->setChecked(cat.enabled);
        cb->setStyleSheet(QStringLiteral("QCheckBox { color: %1; }").arg(Style::kTextPrimary));

        connect(cb, &QCheckBox::toggled, this, [this, id = cat.id](bool on) {
            onCategoryToggled(id, on);
        });

        m_categoryChecks.insert(cat.id, cb);
        catGrid->addWidget(cb, row, col);

        ++col;
        if (col >= 3) {
            col = 0;
            ++row;
        }
    }

    // Enable All / Disable All buttons
    auto* catBtnLayout = new QHBoxLayout();
    catBtnLayout->addStretch();

    auto* enableAllBtn = new QPushButton(QStringLiteral("Enable All"), catGroup);
    enableAllBtn->setAutoDefault(false);
    connect(enableAllBtn, &QPushButton::clicked, this, &SupportDialog::onEnableAll);
    catBtnLayout->addWidget(enableAllBtn);

    auto* disableAllBtn = new QPushButton(QStringLiteral("Disable All"), catGroup);
    disableAllBtn->setAutoDefault(false);
    connect(disableAllBtn, &QPushButton::clicked, this, &SupportDialog::onDisableAll);
    catBtnLayout->addWidget(disableAllBtn);

    catBtnLayout->addStretch();
    catGrid->addLayout(catBtnLayout, row + 1, 0, 1, 3);
    mainLayout->addWidget(catGroup);

    // --- Log File Info ---
    auto* logInfoLayout = new QHBoxLayout();
    m_logPathLabel = new QLabel(this);
    m_logPathLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; font-family: monospace; font-size: 11px; }")
        .arg(Style::kTextScale));
    logInfoLayout->addWidget(m_logPathLabel, 1);

    m_logSizeLabel = new QLabel(this);
    m_logSizeLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; font-size: 11px; }")
        .arg(Style::kTextScale));
    logInfoLayout->addWidget(m_logSizeLabel);
    mainLayout->addLayout(logInfoLayout);

    // --- Log Viewer ---
    m_logViewer = new QPlainTextEdit(this);
    m_logViewer->setReadOnly(true);
    m_logViewer->setMaximumBlockCount(kMaxLogViewLines);
    m_logViewer->setFont(QFont(QStringLiteral("Consolas"), 9));
    // §D: #203040 = Style::kBorderSubtle, #00b4d8 = Style::kAccent.
    // §D exceptions: bg #0a0a14 (≈ kPanelBg #0a0a18 but 4-blue different — kept exact);
    //   fg #a0b0c0 (off-palette warm-blue for log text readability).
    m_logViewer->setStyleSheet(
        QStringLiteral(
            "QPlainTextEdit {"
            "  background: #0a0a14;"             // §D exception: log bg (≈ kPanelBg)
            "  color: #a0b0c0;"                  // §D exception: log text warm-blue
            "  border: 1px solid %1;"            // Style::kBorderSubtle
            "  selection-background-color: %2;"  // Style::kAccent
            "}")
        .arg(Style::kBorderSubtle, Style::kAccent));
    mainLayout->addWidget(m_logViewer, 1);  // stretch factor 1

    // --- Action Buttons ---
    auto* btnLayout = new QHBoxLayout();

    auto* refreshBtn = new QPushButton(QStringLiteral("Refresh"), this);
    refreshBtn->setAutoDefault(false);
    connect(refreshBtn, &QPushButton::clicked, this, &SupportDialog::onRefresh);
    btnLayout->addWidget(refreshBtn);

    auto* clearBtn = new QPushButton(QStringLiteral("Clear Log"), this);
    clearBtn->setAutoDefault(false);
    connect(clearBtn, &QPushButton::clicked, this, &SupportDialog::onClearLog);
    btnLayout->addWidget(clearBtn);

    auto* openFolderBtn = new QPushButton(QStringLiteral("Open Log Folder"), this);
    openFolderBtn->setAutoDefault(false);
    connect(openFolderBtn, &QPushButton::clicked, this, &SupportDialog::onOpenLogFolder);
    btnLayout->addWidget(openFolderBtn);

    btnLayout->addStretch();

    auto* bundleBtn = new QPushButton(QStringLiteral("Create Support Bundle"), this);
    bundleBtn->setAutoDefault(false);
    // §D: bg = Style::kAccent. Hover #0096b7 = accent-dark; no canonical match.
    bundleBtn->setStyleSheet(
        QStringLiteral(
            "QPushButton {"
            "  background: %1;"          // Style::kAccent
            "  color: #ffffff;"
            "  border: none;"
            "  border-radius: 4px;"
            "  padding: 6px 14px;"
            "  font-weight: bold;"
            "}"
            "QPushButton:hover { background: #0096b7; }")  // §D exception: accent-dark hover
        .arg(Style::kAccent));
    connect(bundleBtn, &QPushButton::clicked, this, &SupportDialog::onCreateBundle);
    btnLayout->addWidget(bundleBtn);

    mainLayout->addLayout(btnLayout);

    // --- Status ---
    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; font-size: 11px; }")
        .arg(Style::kTextSecondary));
    mainLayout->addWidget(m_statusLabel);

    // Dialog theme. §D: #0f0f1a = kAppBg, #8090a0 = kTextSecondary,
    // #203040 = kBorderSubtle, #304050 = kOverlayBorder, #c8d8e8 = kTextPrimary.
    // §D exception: #405060 — button border/hover; off-palette (no canonical match).
    setStyleSheet(
        QStringLiteral(
            "QDialog { background: %1; }"
            "QGroupBox {"
            "  color: %2;"
            "  border: 1px solid %3;"
            "  border-radius: 4px;"
            "  margin-top: 8px;"
            "  padding-top: 14px;"
            "}"
            "QGroupBox::title {"
            "  subcontrol-origin: margin;"
            "  left: 10px;"
            "  padding: 0 3px;"
            "}"
            "QPushButton {"
            "  background: %4;"
            "  color: %5;"
            "  border: 1px solid #405060;"    // §D exception: off-palette border
            "  border-radius: 3px;"
            "  padding: 5px 12px;"
            "}"
            "QPushButton:hover { background: #405060; }")  // §D exception
        .arg(Style::kAppBg, Style::kTextSecondary, Style::kBorderSubtle,
             Style::kOverlayBorder, Style::kTextPrimary));
}

void SupportDialog::refreshLogViewer()
{
    updateLogInfo();

    QString path = LogManager::instance().logFilePath();
    if (path.isEmpty()) {
        m_logViewer->setPlainText(QStringLiteral("No log file found."));
        return;
    }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_logViewer->setPlainText(QStringLiteral("Could not open log file."));
        return;
    }

    // Read tail if file is large
    qint64 size = f.size();
    if (size > kMaxLogViewBytes) {
        f.seek(size - kMaxLogViewBytes);
        f.readLine();  // Skip partial first line
    }

    QString content = QString::fromUtf8(f.readAll());
    m_logViewer->setPlainText(content);

    // Scroll to bottom
    auto cursor = m_logViewer->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_logViewer->setTextCursor(cursor);
}

void SupportDialog::updateLogInfo()
{
    const auto& mgr = LogManager::instance();
    QString path = mgr.logFilePath();

    if (path.isEmpty()) {
        m_logPathLabel->setText(QStringLiteral("Log: (none)"));
        m_logSizeLabel->setText(QString());
    } else {
        // Show just the filename, not full path
        m_logPathLabel->setText(QStringLiteral("Log: %1").arg(QFileInfo(path).fileName()));

        qint64 size = mgr.logFileSize();
        if (size < 1024) {
            m_logSizeLabel->setText(QStringLiteral("%1 B").arg(size));
        } else if (size < 1024 * 1024) {
            m_logSizeLabel->setText(QStringLiteral("%1 KB").arg(size / 1024));
        } else {
            m_logSizeLabel->setText(QStringLiteral("%1 MB").arg(size / (1024 * 1024)));
        }
    }
}

// --- Slots ---

void SupportDialog::onRefresh()
{
    refreshLogViewer();
    m_statusLabel->setText(QStringLiteral("Log refreshed."));
}

void SupportDialog::onClearLog()
{
    LogManager::instance().clearLog();
    refreshLogViewer();
    m_statusLabel->setText(QStringLiteral("Log cleared."));
}

void SupportDialog::onOpenLogFolder()
{
    QString dir = LogManager::instance().logDirPath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void SupportDialog::onEnableAll()
{
    LogManager::instance().setAllEnabled(true);
    for (auto it = m_categoryChecks.begin(); it != m_categoryChecks.end(); ++it) {
        it.value()->blockSignals(true);
        it.value()->setChecked(true);
        it.value()->blockSignals(false);
    }
    m_statusLabel->setText(QStringLiteral("All diagnostic logging enabled."));
}

void SupportDialog::onDisableAll()
{
    LogManager::instance().setAllEnabled(false);
    for (auto it = m_categoryChecks.begin(); it != m_categoryChecks.end(); ++it) {
        it.value()->blockSignals(true);
        it.value()->setChecked(false);
        it.value()->blockSignals(false);
    }
    m_statusLabel->setText(QStringLiteral("All diagnostic logging disabled."));
}

void SupportDialog::onCreateBundle()
{
    m_statusLabel->setText(QStringLiteral("Creating support bundle..."));
    QApplication::processEvents();

    QString path = SupportBundle::createBundle(m_radioModel);

    if (path.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("Failed to create support bundle."));
        return;
    }

    m_statusLabel->setText(QStringLiteral("Bundle created: %1").arg(QFileInfo(path).fileName()));

    // Ask user what to do
    QMessageBox msgBox(this);
    msgBox.setWindowTitle(QStringLiteral("Support Bundle Created"));
    msgBox.setText(QStringLiteral("Support bundle saved to:\n%1").arg(path));
    msgBox.setInformativeText(QStringLiteral(
        "Attach this file when filing a bug report at\n"
        "github.com/boydsoftprez/NereusSDR/issues"));
    // §D: #0f0f1a = kAppBg, #c8d8e8 = kTextPrimary, #304050 = kOverlayBorder.
    // §D exception: #405060 — button border; off-palette (matches dialog theme above).
    msgBox.setStyleSheet(
        QStringLiteral(
            "QMessageBox { background: %1; color: %2; }"
            "QLabel { color: %2; }"
            "QPushButton { background: %3; color: %2; border: 1px solid #405060;"  // §D exception
            "  border-radius: 3px; padding: 5px 12px; }")
        .arg(Style::kAppBg, Style::kTextPrimary, Style::kOverlayBorder));

    auto* openBtn = msgBox.addButton(QStringLiteral("Open Folder"), QMessageBox::ActionRole);
    auto* issueBtn = msgBox.addButton(QStringLiteral("File an Issue"), QMessageBox::ActionRole);
    msgBox.addButton(QMessageBox::Close);
    msgBox.exec();

    if (msgBox.clickedButton() == openBtn) {
        SupportBundle::openBundleFolder();
    } else if (msgBox.clickedButton() == issueBtn) {
        QDesktopServices::openUrl(
            QUrl(QStringLiteral("https://github.com/boydsoftprez/NereusSDR/issues/new")));
        SupportBundle::openBundleFolder();
    }
}

void SupportDialog::onCategoryToggled(const QString& id, bool on)
{
    LogManager::instance().setEnabled(id, on);
    m_statusLabel->setText(QStringLiteral("%1 logging %2.")
        .arg(id, on ? QStringLiteral("enabled") : QStringLiteral("disabled")));
}

} // namespace NereusSDR
