#include "MainWindow.h"
#include "models/RadioModel.h"
#include "core/AppSettings.h"
#include "core/RadioDiscovery.h"

#include <QApplication>
#include <QCloseEvent>
#include <QMenuBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QDebug>

namespace NereusSDR {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_radioModel(new RadioModel(this))
{
    buildUI();
    buildMenuBar();
    applyDarkTheme();
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUI()
{
    setWindowTitle(QString("NereusSDR %1").arg(NEREUSSDR_VERSION));
    setMinimumSize(800, 600);
    resize(1280, 800);

    // Central widget with placeholder
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* placeholder = new QLabel("NereusSDR", central);
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setStyleSheet("font-size: 24px; color: #00b4d8; font-weight: bold;");
    layout->addWidget(placeholder);

    setCentralWidget(central);
}

void MainWindow::buildMenuBar()
{
    auto* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction("&Quit", QKeySequence::Quit, qApp, &QApplication::quit);

    auto* radioMenu = menuBar()->addMenu("&Radio");
    radioMenu->addAction("&Discover...", this, [this]() {
        m_radioModel->discovery()->startDiscovery();
        qDebug() << "Discovery started";
    });
    radioMenu->addAction("D&isconnect", this, [this]() {
        m_radioModel->disconnectFromRadio();
    });

    auto* helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("&About NereusSDR", this, [this]() {
        Q_UNUSED(this);
        // TODO: About dialog
    });
}

void MainWindow::applyDarkTheme()
{
    // Dark theme matching STYLEGUIDE.md color palette
    setStyleSheet(R"(
        QMainWindow {
            background: #0f0f1a;
        }
        QMenuBar {
            background: #1a2a3a;
            color: #c8d8e8;
            border-bottom: 1px solid #203040;
        }
        QMenuBar::item:selected {
            background: #00b4d8;
        }
        QMenu {
            background: #1a2a3a;
            color: #c8d8e8;
            border: 1px solid #203040;
        }
        QMenu::item:selected {
            background: #00b4d8;
        }
        QLabel {
            color: #c8d8e8;
        }
        QStatusBar {
            background: #1a2a3a;
            color: #8090a0;
        }
    )");
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    AppSettings::instance().save();
    m_radioModel->disconnectFromRadio();
    event->accept();
}

} // namespace NereusSDR
