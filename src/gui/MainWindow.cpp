#include "MainWindow.h"
#include "ConnectionPanel.h"
#include "SupportDialog.h"
#include "SpectrumWidget.h"
#include "models/RadioModel.h"
#include "core/AppSettings.h"
#include "core/RadioDiscovery.h"
#include "core/WdspEngine.h"
#include "core/FFTEngine.h"
#include "core/LogCategories.h"

#include <QApplication>
#include <QCloseEvent>
#include <QMenuBar>
#include <QStatusBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QProgressDialog>
#include <QTimer>
#include <QThread>

#include <cstdlib>

namespace NereusSDR {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_radioModel(new RadioModel(this))
{
    buildUI();
    buildMenuBar();
    buildStatusBar();
    applyDarkTheme();

    // Wire connection state changes to status bar
    connect(m_radioModel, &RadioModel::connectionStateChanged,
            this, &MainWindow::onConnectionStateChanged);

    // WDSP wisdom progress dialog — shown as a modal window during first-run
    // wisdom generation. Pattern from AetherSDR MainWindow::enableNr2WithWisdom().
    connect(m_radioModel->wdspEngine(), &WdspEngine::wisdomProgress,
            this, [this](int percent, const QString& status) {
        // Create dialog on first progress signal
        if (!m_wisdomDialog && percent < 100) {
            m_wisdomDialog = new QProgressDialog(this);
            m_wisdomDialog->setWindowTitle(QStringLiteral("NereusSDR — FFTW Wisdom"));
            m_wisdomDialog->setLabelText(
                QStringLiteral("Optimizing FFT plans for DSP engine...\n\n"
                               "This only happens on first run."));
            m_wisdomDialog->setRange(0, 100);
            m_wisdomDialog->setValue(0);
            m_wisdomDialog->setCancelButton(nullptr);
            m_wisdomDialog->setAutoClose(false);
            m_wisdomDialog->setMinimumWidth(500);
            m_wisdomDialog->setMinimumDuration(0);
            m_wisdomDialog->setWindowModality(Qt::ApplicationModal);
            m_wisdomDialog->setStyleSheet(QStringLiteral(
                "QProgressDialog { background: #0f0f1a; }"
                "QLabel { color: #c8d8e8; font-size: 13px; }"
                "QProgressBar {"
                "  text-align: center; font-size: 13px;"
                "  font-weight: bold; color: #c8d8e8;"
                "  background: #1a2a3a; border: 1px solid #2e4e6e;"
                "  border-radius: 3px; min-height: 24px;"
                "}"
                "QProgressBar::chunk { background: #00b4d8; }"));
            m_wisdomDialog->show();
        }

        if (m_wisdomDialog) {
            m_wisdomDialog->setValue(percent);
            if (!status.isEmpty() && percent < 100) {
                m_wisdomDialog->setLabelText(
                    QStringLiteral("Optimizing FFT plans for DSP engine...\n\n%1").arg(status));
            }
            if (percent >= 100) {
                m_wisdomDialog->setLabelText(QStringLiteral("FFTW planning complete!"));
                m_wisdomDialog->setValue(100);
                // Auto-close after brief delay
                QTimer::singleShot(800, this, [this]() {
                    if (m_wisdomDialog) {
                        m_wisdomDialog->close();
                        m_wisdomDialog->deleteLater();
                        m_wisdomDialog = nullptr;
                    }
                });
            }
        }
    });

    // Start discovery in background so radios are found before the user opens the panel
    m_radioModel->discovery()->startDiscovery();

    // Auto-reconnect to last radio if it appears
    tryAutoReconnect();
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUI()
{
    setWindowTitle(QStringLiteral("NereusSDR %1").arg(NEREUSSDR_VERSION));
    setMinimumSize(800, 600);
    resize(1280, 800);

    // Central widget with SpectrumWidget
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);

    m_spectrumWidget = new SpectrumWidget(central);
    m_spectrumWidget->setFrequencyRange(14225000.0, 200000.0);  // 14.225 MHz, 200 kHz BW
    layout->addWidget(m_spectrumWidget);

    setCentralWidget(central);

    // Create FFTEngine on a worker thread (spectrum thread from architecture)
    m_fftEngine = new FFTEngine(0);  // receiver 0
    m_fftEngine->setSampleRate(48000.0);
    m_fftEngine->setFftSize(4096);
    m_fftEngine->setOutputFps(30);

    m_fftThread = new QThread(this);
    m_fftThread->setObjectName(QStringLiteral("SpectrumThread"));
    m_fftEngine->moveToThread(m_fftThread);

    // Clean up FFTEngine when thread finishes
    connect(m_fftThread, &QThread::finished, m_fftEngine, &QObject::deleteLater);

    // Wire: RadioModel raw I/Q → FFTEngine (auto-queued: main → spectrum thread)
    connect(m_radioModel, &RadioModel::rawIqData,
            m_fftEngine, &FFTEngine::feedIQ);

    // Wire: FFTEngine FFT bins → SpectrumWidget (auto-queued: spectrum → main thread)
    connect(m_fftEngine, &FFTEngine::fftReady,
            m_spectrumWidget, &SpectrumWidget::updateSpectrum);

    m_fftThread->start();
}

void MainWindow::buildMenuBar()
{
    // --- File ---
    auto* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    fileMenu->addAction(QStringLiteral("&Quit"), QKeySequence::Quit,
                        qApp, &QApplication::quit);

    // --- Radio ---
    auto* radioMenu = menuBar()->addMenu(QStringLiteral("&Radio"));

    radioMenu->addAction(QStringLiteral("&Connect..."), QKeySequence(Qt::CTRL | Qt::Key_K),
                         this, &MainWindow::showConnectionPanel);

    radioMenu->addAction(QStringLiteral("&Disconnect"), this, [this]() {
        m_radioModel->disconnectFromRadio();
    });

    radioMenu->addSeparator();

    radioMenu->addAction(QStringLiteral("&Protocol Info"), this, [this]() {
        if (m_radioModel->isConnected()) {
            RadioInfo info = m_radioModel->connection()->radioInfo();
            QString msg = QStringLiteral("Radio: %1\nProtocol: P%2\nFirmware: %3\nMAC: %4\nIP: %5")
                .arg(info.displayName())
                .arg(static_cast<int>(info.protocol))
                .arg(info.firmwareVersion)
                .arg(info.macAddress, info.address.toString());
            qCDebug(lcConnection) << msg;
        }
    });

    // --- Help ---
    auto* helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
    helpMenu->addAction(QStringLiteral("&Support..."), this,
                        &MainWindow::showSupportDialog);
    helpMenu->addSeparator();
    helpMenu->addAction(QStringLiteral("&About NereusSDR"), this, [this]() {
        Q_UNUSED(this);
    });
}

void MainWindow::buildStatusBar()
{
    // Connection status indicator (left side)
    m_connStatusLabel = new QLabel(QStringLiteral(" Disconnected "), this);
    m_connStatusLabel->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  color: #8090a0;"
        "  background: #1a2a3a;"
        "  border: 1px solid #203040;"
        "  border-radius: 3px;"
        "  padding: 2px 8px;"
        "  font-size: 12px;"
        "}"));
    statusBar()->addWidget(m_connStatusLabel);

    // Radio info (center)
    m_radioInfoLabel = new QLabel(this);
    m_radioInfoLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: #607080; font-size: 12px; }"));
    statusBar()->addWidget(m_radioInfoLabel, 1);  // stretch factor 1

    // Click status label to open connection panel
    m_connStatusLabel->setCursor(Qt::PointingHandCursor);
    m_connStatusLabel->installEventFilter(this);
}

void MainWindow::applyDarkTheme()
{
    setStyleSheet(QStringLiteral(
        "QMainWindow { background: #0f0f1a; }"
        "QMenuBar {"
        "  background: #1a2a3a;"
        "  color: #c8d8e8;"
        "  border-bottom: 1px solid #203040;"
        "}"
        "QMenuBar::item:selected { background: #00b4d8; }"
        "QMenu {"
        "  background: #1a2a3a;"
        "  color: #c8d8e8;"
        "  border: 1px solid #203040;"
        "}"
        "QMenu::item:selected { background: #00b4d8; }"
        "QLabel { color: #c8d8e8; }"
        "QStatusBar {"
        "  background: #1a2a3a;"
        "  color: #8090a0;"
        "  border-top: 1px solid #203040;"
        "}"));
}

void MainWindow::showConnectionPanel()
{
    if (!m_connectionPanel) {
        m_connectionPanel = new ConnectionPanel(m_radioModel, this);
        m_connectionPanel->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_connectionPanel, &QObject::destroyed, this, [this]() {
            m_connectionPanel = nullptr;
        });
    }
    m_connectionPanel->show();
    m_connectionPanel->raise();
    m_connectionPanel->activateWindow();
}

void MainWindow::showSupportDialog()
{
    if (!m_supportDialog) {
        m_supportDialog = new SupportDialog(m_radioModel, this);
        m_supportDialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_supportDialog, &QObject::destroyed, this, [this]() {
            m_supportDialog = nullptr;
        });
    }
    m_supportDialog->show();
    m_supportDialog->raise();
    m_supportDialog->activateWindow();
}

void MainWindow::onConnectionStateChanged()
{
    if (m_radioModel->isConnected()) {
        m_connStatusLabel->setText(QStringLiteral(" Connected "));
        m_connStatusLabel->setStyleSheet(QStringLiteral(
            "QLabel {"
            "  color: #ffffff;"
            "  background: #007a3d;"
            "  border: 1px solid #00a050;"
            "  border-radius: 3px;"
            "  padding: 2px 8px;"
            "  font-size: 12px;"
            "}"));
        m_radioInfoLabel->setText(QStringLiteral("%1  —  FW %2")
            .arg(m_radioModel->name(), m_radioModel->version()));
        m_radioInfoLabel->setStyleSheet(QStringLiteral(
            "QLabel { color: #c8d8e8; font-size: 12px; }"));
    } else {
        m_connStatusLabel->setText(QStringLiteral(" Disconnected "));
        m_connStatusLabel->setStyleSheet(QStringLiteral(
            "QLabel {"
            "  color: #8090a0;"
            "  background: #1a2a3a;"
            "  border: 1px solid #203040;"
            "  border-radius: 3px;"
            "  padding: 2px 8px;"
            "  font-size: 12px;"
            "}"));
        m_radioInfoLabel->setText(QString());
        m_radioInfoLabel->setStyleSheet(QStringLiteral(
            "QLabel { color: #607080; font-size: 12px; }"));
    }
}

void MainWindow::tryAutoReconnect()
{
    QString lastMac = AppSettings::instance()
        .value(QStringLiteral("LastConnectedRadioMac")).toString();
    if (lastMac.isEmpty()) {
        return;
    }

    // When a radio matching the last MAC is discovered, auto-connect
    connect(m_radioModel->discovery(), &RadioDiscovery::radioDiscovered,
            this, [this, lastMac](const RadioInfo& info) {
        if (info.macAddress == lastMac && !m_radioModel->isConnected() && !info.inUse) {
            qCDebug(lcConnection) << "Auto-reconnecting to" << info.displayName();
            m_radioModel->connectToRadio(info);
        }
    });
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // Stop discovery to prevent new signals during shutdown
    m_radioModel->discovery()->stopDiscovery();

    // Stop FFT thread
    if (m_fftThread && m_fftThread->isRunning()) {
        m_fftThread->quit();
        m_fftThread->wait(2000);
    }

    // Tear down connection (sends stop command, closes sockets, joins thread)
    m_radioModel->disconnectFromRadio();

    AppSettings::instance().save();
    event->accept();

    // Force process exit. Worker threads and active QObjects can prevent
    // the Qt event loop from returning. Settings are saved, sockets closed,
    // and stop command sent to the radio — safe to exit now.
    std::exit(0);
}

} // namespace NereusSDR
