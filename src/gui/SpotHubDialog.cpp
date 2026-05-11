// SPDX-License-Identifier: GPL-3.0-or-later
//
// NereusSDR - SpotHub dialog: see SpotHubDialog.h for the full
// port-citation header and the F1 / F2 / F3 / F4 task breakdown.
//
// Ported from AetherSDR src/gui/DxClusterDialog.cpp [@0cd4559]
// (F2 ships per-source tab content; F3 Spot List and F4 Display
// remain stubs).
//
// Modification history (NereusSDR):
//   2026-05-11  J.J. Boyd / KG4VCF  Phase 3J-2 Task F1. Initial
//                                    shell port; see header for full
//                                    notes. AI tooling: Anthropic
//                                    Claude Code.
//   2026-05-11  J.J. Boyd / KG4VCF  Phase 3J-2 Task F2. Fleshes out
//                                    the seven per-source tab
//                                    builders with the uniform
//                                    template (connection-control
//                                    grid + auto-start toggle +
//                                    start/stop button + status
//                                    label + raw-event console).
//                                    Cluster / RBN / WSJT-X /
//                                    SpotCollector / POTA / FreeDV
//                                    port verbatim from upstream
//                                    `DxClusterDialog.cpp:637-1596
//                                    [@0cd4559]`; PSK Reporter is
//                                    NereusSDR-native and uses the
//                                    same uniform shape with
//                                    callsign + grid identity
//                                    inputs. AppSettings keys and
//                                    widget stylesheets preserved
//                                    verbatim. Each tab builder
//                                    assigns objectName() values
//                                    that the smoke test harness
//                                    uses to assert content shape.
//                                    AI tooling: Anthropic Claude
//                                    Code.

#include "SpotHubDialog.h"

#include "core/AppSettings.h"
#include "core/DxClusterClient.h"
#include "core/FreeDVReporterClient.h"
#include "core/PotaClient.h"
#include "core/PskReporterClient.h"
#include "core/SpotCollectorClient.h"
#include "core/WsjtxClient.h"

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace NereusSDR {

namespace {

// Common stylesheet fragments. Lifted verbatim from upstream so the
// SpotHub dialog visually matches AetherSDR's DxCluster dialog
// (consistent dark panel + cyan accent + monospace console).

constexpr const char* kLineEditStyle =
    "QLineEdit { background: #1a1a2e; color: #c8d8e8; "
    "border: 1px solid #203040; padding: 3px; }";

constexpr const char* kSpinBoxStyle =
    "QSpinBox { background: #1a1a2e; color: #c8d8e8; "
    "border: 1px solid #203040; padding: 3px; }";

constexpr const char* kAutoToggleStyle =
    "QPushButton { background: #206030; color: white; "
    "border: 1px solid #305040; padding: 4px 10px; }"
    "QPushButton:!checked { background: #603020; }";

constexpr const char* kStartBtnStyle =
    "QPushButton { background: #00b4d8; color: #0f0f1a; font-weight: bold; "
    "border: 1px solid #008ba8; padding: 4px; border-radius: 3px; }"
    "QPushButton:hover { background: #00c8f0; }"
    "QPushButton:disabled { background: #404060; color: #808080; }";

constexpr const char* kStatusIdleStyle =
    "QLabel { color: #808080; font-size: 11px; }";

constexpr const char* kConsoleStyle =
    "QPlainTextEdit {"
    "  background: #0a0a14;"
    "  color: #a0b0c0;"
    "  font-family: monospace;"
    "  font-size: 11px;"
    "  border: 1px solid #203040;"
    "  padding: 4px;"
    "}";

constexpr const char* kCmdEditStyle =
    "QLineEdit { background: #1a1a2e; color: #c8d8e8; "
    "border: 1px solid #203040; padding: 3px; font-family: monospace; }";

QString swatchStyle(const QColor& c) {
    return QString(
        "QPushButton { background: %1; border: 2px solid #405060; border-radius: 3px; }"
        "QPushButton:hover { border-color: #c8d8e8; }").arg(c.name());
}

} // namespace

// From AetherSDR src/gui/DxClusterDialog.cpp:230-273 [@0cd4559].
// NereusSDR divergence: AetherSDR upstream takes a trailing
// `RadioModel* radioModel` argument; NereusSDR replaces it with the
// TCI-keyed `SpotModel* spots` (Phase 3J-2 Task D1). Routing of
// `tuneRequested(double)` into the active RadioModel happens in
// MainWindow when the dialog is instantiated. Replaces upstream's
// HAVE_WEBSOCKETS-gated FreeDvClient with the always-built
// FreeDVReporterClient (NereusSDR Task B5). Adds a PSK Reporter
// tab between FreeDV and Spot List (NereusSDR-only). The pane
// stylesheet (panel border + tab colors) follows upstream verbatim.
SpotHubDialog::SpotHubDialog(DxClusterClient* clusterClient,
                             DxClusterClient* rbnClient,
                             WsjtxClient* wsjtxClient,
                             SpotCollectorClient* spotCollectorClient,
                             PotaClient* potaClient,
                             FreeDVReporterClient* freedvClient,
                             PskReporterClient* pskClient,
                             SpotModel* spotModel,
                             DxccColorProvider* dxccProvider,
                             QWidget* parent)
    : QDialog(parent),
      m_clusterClient(clusterClient),
      m_rbnClient(rbnClient),
      m_wsjtxClient(wsjtxClient),
      m_spotCollectorClient(spotCollectorClient),
      m_potaClient(potaClient),
      m_freedvClient(freedvClient),
      m_pskClient(pskClient),
      m_spotModel(spotModel),
      m_dxccProvider(dxccProvider)
{
    setWindowTitle("SpotHub");
    setMinimumSize(680, 560);
    resize(760, 640);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(0);
    root->setContentsMargins(4, 4, 4, 4);

    auto* tabs = new QTabWidget;
    tabs->setStyleSheet(
        "QTabWidget::pane { border: 1px solid #203040; }"
        "QTabBar::tab { background: #1a1a2e; color: #808890; border: 1px solid #203040; "
        "  padding: 6px 16px; margin-right: 2px; }"
        "QTabBar::tab:selected { background: #0f0f1a; color: #00b4d8; border-bottom: none; }");

    // Tab order matches AetherSDR upstream
    // (src/gui/DxClusterDialog.cpp:262-271). NereusSDR adds the PSK
    // Reporter tab between FreeDV and Spot List (Task F2 wires its
    // content). FreeDV tab is unconditional in NereusSDR; upstream
    // gated it on HAVE_WEBSOCKETS.
    buildClusterTab(tabs);
    buildRbnTab(tabs);
    buildWsjtxTab(tabs);
    buildSpotCollectorTab(tabs);
    buildPotaTab(tabs);
    buildFreeDvTab(tabs);
    buildPskTab(tabs);
    buildSpotListTab(tabs);
    buildDisplayTab(tabs);

    root->addWidget(tabs);
}

// From AetherSDR src/gui/DxClusterDialog.cpp:637-803 [@0cd4559].
// Cluster tab: telnet host/port/callsign + auto-connect toggle +
// connect/disconnect button + status label + cluster console +
// command input row. AetherSDR's per-source stylesheets, AppSettings
// key names, default server (`dxc.nc7j.com:7300`), and signal
// emission (`connectRequested(host, port, call)` /
// `disconnectRequested`) preserved verbatim. NereusSDR addition:
// objectName() on every test-relevant widget so smoke tests can
// locate them via findChild().
void SpotHubDialog::buildClusterTab(QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(8);

    auto& s = AppSettings::instance();

    auto* connGroup = new QGroupBox("Connection");
    auto* connLayout = new QVBoxLayout(connGroup);
    connLayout->setSpacing(4);

    auto* grid = new QGridLayout;
    grid->setColumnStretch(1, 1);
    int row = 0;

    grid->addWidget(new QLabel("Server:"), row, 0);
    m_hostEdit = new QLineEdit(s.value("DxClusterHost", "dxc.nc7j.com").toString());
    m_hostEdit->setObjectName("clusterHostEdit");
    m_hostEdit->setPlaceholderText("dxc.nc7j.com");
    m_hostEdit->setStyleSheet(kLineEditStyle);
    grid->addWidget(m_hostEdit, row, 1);
    row++;

    grid->addWidget(new QLabel("Port:"), row, 0);
    m_portSpin = new QSpinBox;
    m_portSpin->setObjectName("clusterPortSpin");
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(s.value("DxClusterPort", 7300).toInt());
    m_portSpin->setStyleSheet(kSpinBoxStyle);
    grid->addWidget(m_portSpin, row, 1);
    row++;

    grid->addWidget(new QLabel("Callsign:"), row, 0);
    m_callEdit = new QLineEdit(s.value("DxClusterCallsign").toString());
    m_callEdit->setObjectName("clusterCallEdit");
    m_callEdit->setPlaceholderText("your callsign");
    m_callEdit->setStyleSheet(kLineEditStyle);
    grid->addWidget(m_callEdit, row, 1);
    row++;

    connLayout->addLayout(grid);

    auto* btnRow = new QHBoxLayout;
    m_autoConnectBtn = new QPushButton(
        s.value("DxClusterAutoConnect", "False").toString() == "True"
            ? "Auto-Connect: ON" : "Auto-Connect: OFF");
    m_autoConnectBtn->setObjectName("clusterAutoConnectBtn");
    m_autoConnectBtn->setCheckable(true);
    m_autoConnectBtn->setChecked(s.value("DxClusterAutoConnect", "False").toString() == "True");
    m_autoConnectBtn->setStyleSheet(kAutoToggleStyle);
    connect(m_autoConnectBtn, &QPushButton::toggled, this, [this](bool on) {
        m_autoConnectBtn->setText(on ? "Auto-Connect: ON" : "Auto-Connect: OFF");
        auto& settings = AppSettings::instance();
        settings.setValue("DxClusterAutoConnect", on ? "True" : "False");
        settings.save();
    });
    btnRow->addWidget(m_autoConnectBtn);
    btnRow->addStretch();

    m_statusLabel = new QLabel("Disconnected");
    m_statusLabel->setObjectName("clusterStatusLabel");
    m_statusLabel->setStyleSheet(kStatusIdleStyle);
    btnRow->addWidget(m_statusLabel);
    btnRow->addStretch();

    m_connectBtn = new QPushButton(m_clusterClient && m_clusterClient->isConnected()
                                       ? "Disconnect" : "Connect");
    m_connectBtn->setObjectName("clusterConnectBtn");
    m_connectBtn->setFixedWidth(100);
    m_connectBtn->setStyleSheet(kStartBtnStyle);
    connect(m_connectBtn, &QPushButton::clicked, this, [this] {
        if (m_clusterClient && m_clusterClient->isConnected()) {
            emit disconnectRequested();
            return;
        }
        QString host = m_hostEdit->text().trimmed();
        QString call = m_callEdit->text().trimmed().toUpper();
        quint16 port = static_cast<quint16>(m_portSpin->value());
        if (host.isEmpty() || call.isEmpty()) {
            m_statusLabel->setText("Server and callsign are required");
            m_statusLabel->setStyleSheet("QLabel { color: #ff4444; font-size: 11px; }");
            return;
        }
        auto& settings = AppSettings::instance();
        settings.setValue("DxClusterHost", host);
        settings.setValue("DxClusterPort", port);
        settings.setValue("DxClusterCallsign", call);
        settings.save();
        emit connectRequested(host, port, call);
    });
    btnRow->addWidget(m_connectBtn);
    connLayout->addLayout(btnRow);

    layout->addWidget(connGroup);

    // Console + spot-color row
    auto* consoleRow = new QHBoxLayout;
    auto* consoleLabel = new QLabel("Cluster Console");
    consoleLabel->setStyleSheet("QLabel { color: #00b4d8; font-weight: bold; }");
    consoleRow->addWidget(consoleLabel);
    consoleRow->addStretch();

    auto* dxcColorLabel = new QLabel("Spot Color:");
    dxcColorLabel->setStyleSheet("QLabel { color: #808080; font-size: 12px; }");
    consoleRow->addWidget(dxcColorLabel);

    QColor dxcColor(s.value("DxClusterSpotColor", "#D2B48C").toString());
    auto* dxcColorBtn = new QPushButton;
    dxcColorBtn->setObjectName("clusterColorBtn");
    dxcColorBtn->setFixedSize(18, 18);
    dxcColorBtn->setStyleSheet(swatchStyle(dxcColor));
    connect(dxcColorBtn, &QPushButton::clicked, this, [this, dxcColorBtn] {
        QColor c = QColorDialog::getColor(
            QColor(AppSettings::instance().value("DxClusterSpotColor", "#D2B48C").toString()),
            this, "DX Cluster Spot Color");
        if (c.isValid()) {
            dxcColorBtn->setStyleSheet(swatchStyle(c));
            AppSettings::instance().setValue("DxClusterSpotColor", c.name());
            AppSettings::instance().save();
        }
    });
    consoleRow->addWidget(dxcColorBtn);
    layout->addLayout(consoleRow);

    m_console = new QPlainTextEdit;
    m_console->setObjectName("clusterConsole");
    m_console->setReadOnly(true);
    m_console->setMaximumBlockCount(2000);
    m_console->setStyleSheet(kConsoleStyle);
    layout->addWidget(m_console, 1);

    // Command input row
    auto* cmdRow = new QHBoxLayout;
    m_cmdEdit = new QLineEdit;
    m_cmdEdit->setObjectName("clusterCmdEdit");
    m_cmdEdit->setPlaceholderText("Type a cluster command (e.g. sh/dx 20, set/filter, bye)");
    m_cmdEdit->setStyleSheet(kCmdEditStyle);
    m_cmdEdit->setEnabled(m_clusterClient && m_clusterClient->isConnected());
    connect(m_cmdEdit, &QLineEdit::returnPressed, this, [this] {
        QString cmd = m_cmdEdit->text().trimmed();
        if (cmd.isEmpty() || !m_clusterClient || !m_clusterClient->isConnected()) {
            return;
        }
        auto* client = m_clusterClient;
        QMetaObject::invokeMethod(client, [client, cmd] { client->sendCommand(cmd); });
        m_console->appendPlainText("> " + cmd);
        m_cmdEdit->clear();
    });
    m_sendBtn = new QPushButton("Send");
    m_sendBtn->setObjectName("clusterSendBtn");
    m_sendBtn->setFixedWidth(60);
    m_sendBtn->setEnabled(m_clusterClient && m_clusterClient->isConnected());
    connect(m_sendBtn, &QPushButton::clicked, this, [this] {
        emit m_cmdEdit->returnPressed();
    });
    cmdRow->addWidget(m_cmdEdit, 1);
    cmdRow->addWidget(m_sendBtn);
    layout->addLayout(cmdRow);

    tabs->addTab(page, "Cluster");
}

// From AetherSDR src/gui/DxClusterDialog.cpp:805-992 [@0cd4559].
// RBN tab: same skeleton as the cluster tab plus a rate-limit
// spinbox row. Defaults to `telnet.reversebeacon.net:7000` and falls
// back to the cluster callsign when the RBN callsign is empty.
// Signals: `rbnConnectRequested(host, port, call)` /
// `rbnDisconnectRequested`. NereusSDR addition: objectName() on
// every test-relevant widget.
void SpotHubDialog::buildRbnTab(QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(8);

    auto& s = AppSettings::instance();
    QString defaultCall = s.value("RbnCallsign").toString();
    if (defaultCall.isEmpty()) {
        defaultCall = s.value("DxClusterCallsign").toString();
    }

    auto* connGroup = new QGroupBox("RBN Connection");
    auto* connLayout = new QVBoxLayout(connGroup);
    connLayout->setSpacing(4);

    auto* grid = new QGridLayout;
    grid->setColumnStretch(1, 1);
    int row = 0;

    grid->addWidget(new QLabel("Server:"), row, 0);
    m_rbnHostEdit = new QLineEdit(s.value("RbnHost", "telnet.reversebeacon.net").toString());
    m_rbnHostEdit->setObjectName("rbnHostEdit");
    m_rbnHostEdit->setPlaceholderText("telnet.reversebeacon.net");
    m_rbnHostEdit->setStyleSheet(kLineEditStyle);
    grid->addWidget(m_rbnHostEdit, row, 1);
    row++;

    grid->addWidget(new QLabel("Port:"), row, 0);
    m_rbnPortSpin = new QSpinBox;
    m_rbnPortSpin->setObjectName("rbnPortSpin");
    m_rbnPortSpin->setRange(1, 65535);
    m_rbnPortSpin->setValue(s.value("RbnPort", 7000).toInt());
    m_rbnPortSpin->setStyleSheet(kSpinBoxStyle);
    grid->addWidget(m_rbnPortSpin, row, 1);
    row++;

    grid->addWidget(new QLabel("Callsign:"), row, 0);
    m_rbnCallEdit = new QLineEdit(defaultCall);
    m_rbnCallEdit->setObjectName("rbnCallEdit");
    m_rbnCallEdit->setPlaceholderText("your callsign");
    m_rbnCallEdit->setStyleSheet(kLineEditStyle);
    grid->addWidget(m_rbnCallEdit, row, 1);
    row++;

    grid->addWidget(new QLabel("Rate Limit:"), row, 0);
    auto* rateRow = new QHBoxLayout;
    auto* rateSpin = new QSpinBox;
    rateSpin->setObjectName("rbnRateSpin");
    rateSpin->setRange(1, 100);
    rateSpin->setValue(s.value("RbnRateLimit", 10).toInt());
    rateSpin->setSuffix(" spots/sec");
    rateSpin->setStyleSheet(kSpinBoxStyle);
    connect(rateSpin, &QSpinBox::valueChanged, this, [](int v) {
        auto& settings = AppSettings::instance();
        settings.setValue("RbnRateLimit", v);
        settings.save();
    });
    rateRow->addWidget(rateSpin);
    rateRow->addStretch();
    grid->addLayout(rateRow, row, 1);
    row++;

    connLayout->addLayout(grid);

    auto* btnRow = new QHBoxLayout;
    m_rbnAutoConnectBtn = new QPushButton(
        s.value("RbnAutoConnect", "False").toString() == "True"
            ? "Auto-Connect: ON" : "Auto-Connect: OFF");
    m_rbnAutoConnectBtn->setObjectName("rbnAutoConnectBtn");
    m_rbnAutoConnectBtn->setCheckable(true);
    m_rbnAutoConnectBtn->setChecked(s.value("RbnAutoConnect", "False").toString() == "True");
    m_rbnAutoConnectBtn->setStyleSheet(kAutoToggleStyle);
    connect(m_rbnAutoConnectBtn, &QPushButton::toggled, this, [this](bool on) {
        m_rbnAutoConnectBtn->setText(on ? "Auto-Connect: ON" : "Auto-Connect: OFF");
        auto& settings = AppSettings::instance();
        settings.setValue("RbnAutoConnect", on ? "True" : "False");
        settings.save();
    });
    btnRow->addWidget(m_rbnAutoConnectBtn);
    btnRow->addStretch();

    m_rbnStatusLabel = new QLabel("Disconnected");
    m_rbnStatusLabel->setObjectName("rbnStatusLabel");
    m_rbnStatusLabel->setStyleSheet(kStatusIdleStyle);
    btnRow->addWidget(m_rbnStatusLabel);
    btnRow->addStretch();

    m_rbnConnectBtn = new QPushButton(m_rbnClient && m_rbnClient->isConnected()
                                          ? "Disconnect" : "Connect");
    m_rbnConnectBtn->setObjectName("rbnConnectBtn");
    m_rbnConnectBtn->setFixedWidth(100);
    m_rbnConnectBtn->setStyleSheet(kStartBtnStyle);
    connect(m_rbnConnectBtn, &QPushButton::clicked, this, [this] {
        if (m_rbnClient && m_rbnClient->isConnected()) {
            emit rbnDisconnectRequested();
            return;
        }
        QString host = m_rbnHostEdit->text().trimmed();
        QString call = m_rbnCallEdit->text().trimmed().toUpper();
        quint16 port = static_cast<quint16>(m_rbnPortSpin->value());
        if (host.isEmpty() || call.isEmpty()) {
            m_rbnStatusLabel->setText("Server and callsign are required");
            m_rbnStatusLabel->setStyleSheet("QLabel { color: #ff4444; font-size: 11px; }");
            return;
        }
        auto& settings = AppSettings::instance();
        settings.setValue("RbnHost", host);
        settings.setValue("RbnPort", port);
        settings.setValue("RbnCallsign", call);
        settings.save();
        emit rbnConnectRequested(host, port, call);
    });
    btnRow->addWidget(m_rbnConnectBtn);
    connLayout->addLayout(btnRow);

    layout->addWidget(connGroup);

    // Console + spot-color row
    auto* rbnConsoleRow = new QHBoxLayout;
    auto* rbnConsoleLabel = new QLabel("RBN Console");
    rbnConsoleLabel->setStyleSheet("QLabel { color: #00b4d8; font-weight: bold; }");
    rbnConsoleRow->addWidget(rbnConsoleLabel);
    rbnConsoleRow->addStretch();

    auto* rbnColorLabel = new QLabel("Spot Color:");
    rbnColorLabel->setStyleSheet("QLabel { color: #808080; font-size: 12px; }");
    rbnConsoleRow->addWidget(rbnColorLabel);

    QColor rbnColor(s.value("RbnSpotColor", "#4488FF").toString());
    auto* rbnColorBtn = new QPushButton;
    rbnColorBtn->setObjectName("rbnColorBtn");
    rbnColorBtn->setFixedSize(18, 18);
    rbnColorBtn->setStyleSheet(swatchStyle(rbnColor));
    connect(rbnColorBtn, &QPushButton::clicked, this, [this, rbnColorBtn] {
        QColor c = QColorDialog::getColor(
            QColor(AppSettings::instance().value("RbnSpotColor", "#4488FF").toString()),
            this, "RBN Spot Color");
        if (c.isValid()) {
            rbnColorBtn->setStyleSheet(swatchStyle(c));
            AppSettings::instance().setValue("RbnSpotColor", c.name());
            AppSettings::instance().save();
        }
    });
    rbnConsoleRow->addWidget(rbnColorBtn);
    layout->addLayout(rbnConsoleRow);

    m_rbnConsole = new QPlainTextEdit;
    m_rbnConsole->setObjectName("rbnConsole");
    m_rbnConsole->setReadOnly(true);
    m_rbnConsole->setMaximumBlockCount(2000);
    m_rbnConsole->setStyleSheet(kConsoleStyle);
    layout->addWidget(m_rbnConsole, 1);

    // Command input row
    auto* cmdRow = new QHBoxLayout;
    m_rbnCmdEdit = new QLineEdit;
    m_rbnCmdEdit->setObjectName("rbnCmdEdit");
    m_rbnCmdEdit->setPlaceholderText("Type an RBN command (e.g. set/skimmer, set/ft8, bye)");
    m_rbnCmdEdit->setStyleSheet(kCmdEditStyle);
    m_rbnCmdEdit->setEnabled(m_rbnClient && m_rbnClient->isConnected());
    connect(m_rbnCmdEdit, &QLineEdit::returnPressed, this, [this] {
        QString cmd = m_rbnCmdEdit->text().trimmed();
        if (cmd.isEmpty() || !m_rbnClient || !m_rbnClient->isConnected()) {
            return;
        }
        auto* client = m_rbnClient;
        QMetaObject::invokeMethod(client, [client, cmd] { client->sendCommand(cmd); });
        m_rbnConsole->appendPlainText("> " + cmd);
        m_rbnCmdEdit->clear();
    });
    m_rbnSendBtn = new QPushButton("Send");
    m_rbnSendBtn->setObjectName("rbnSendBtn");
    m_rbnSendBtn->setFixedWidth(60);
    m_rbnSendBtn->setEnabled(m_rbnClient && m_rbnClient->isConnected());
    connect(m_rbnSendBtn, &QPushButton::clicked, this, [this] {
        emit m_rbnCmdEdit->returnPressed();
    });
    cmdRow->addWidget(m_rbnCmdEdit, 1);
    cmdRow->addWidget(m_rbnSendBtn);
    layout->addLayout(cmdRow);

    tabs->addTab(page, "RBN");
}

// From AetherSDR src/gui/DxClusterDialog.cpp:994-1237 [@0cd4559].
// WSJT-X tab: UDP multicast address + port + auto-start + start/stop
// button + spot filter checkboxes (CQ / CQ POTA / Calling Me) with
// inline color pickers + Default color picker + spot-life slider +
// raw-event console. Signals: `wsjtxStartRequested(addr, port)` /
// `wsjtxStopRequested`. NereusSDR addition: objectName() on every
// test-relevant widget.
void SpotHubDialog::buildWsjtxTab(QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(8);

    auto& s = AppSettings::instance();

    auto* connGroup = new QGroupBox("WSJT-X Listener Address");
    auto* connLayout = new QVBoxLayout(connGroup);
    connLayout->setSpacing(4);

    auto* grid = new QGridLayout;
    grid->setColumnStretch(1, 1);
    int row = 0;

    grid->addWidget(new QLabel("Address:"), row, 0);
    m_wsjtxAddrEdit = new QLineEdit(s.value("WsjtxAddress", "224.0.0.1").toString());
    m_wsjtxAddrEdit->setObjectName("wsjtxAddrEdit");
    m_wsjtxAddrEdit->setPlaceholderText("224.0.0.1 (multicast) or 0.0.0.0 (any)");
    m_wsjtxAddrEdit->setStyleSheet(kLineEditStyle);
    grid->addWidget(m_wsjtxAddrEdit, row, 1);
    row++;

    grid->addWidget(new QLabel("Port:"), row, 0);
    m_wsjtxPortSpin = new QSpinBox;
    m_wsjtxPortSpin->setObjectName("wsjtxPortSpin");
    m_wsjtxPortSpin->setRange(1, 65535);
    m_wsjtxPortSpin->setValue(s.value("WsjtxPort", 2237).toInt());
    m_wsjtxPortSpin->setStyleSheet(kSpinBoxStyle);
    grid->addWidget(m_wsjtxPortSpin, row, 1);
    row++;

    connLayout->addLayout(grid);

    auto* btnRow = new QHBoxLayout;
    m_wsjtxAutoStartBtn = new QPushButton(
        s.value("WsjtxAutoStart", "False").toString() == "True"
            ? "Auto-Start: ON" : "Auto-Start: OFF");
    m_wsjtxAutoStartBtn->setObjectName("wsjtxAutoStartBtn");
    m_wsjtxAutoStartBtn->setCheckable(true);
    m_wsjtxAutoStartBtn->setChecked(s.value("WsjtxAutoStart", "False").toString() == "True");
    m_wsjtxAutoStartBtn->setStyleSheet(kAutoToggleStyle);
    connect(m_wsjtxAutoStartBtn, &QPushButton::toggled, this, [this](bool on) {
        m_wsjtxAutoStartBtn->setText(on ? "Auto-Start: ON" : "Auto-Start: OFF");
        auto& settings = AppSettings::instance();
        settings.setValue("WsjtxAutoStart", on ? "True" : "False");
        settings.save();
    });
    btnRow->addWidget(m_wsjtxAutoStartBtn);
    btnRow->addStretch();

    m_wsjtxStatusLabel = new QLabel("Stopped");
    m_wsjtxStatusLabel->setObjectName("wsjtxStatusLabel");
    m_wsjtxStatusLabel->setStyleSheet(kStatusIdleStyle);
    btnRow->addWidget(m_wsjtxStatusLabel);
    btnRow->addStretch();

    m_wsjtxStartBtn = new QPushButton(m_wsjtxClient && m_wsjtxClient->isListening()
                                          ? "Stop" : "Start");
    m_wsjtxStartBtn->setObjectName("wsjtxStartBtn");
    m_wsjtxStartBtn->setFixedWidth(100);
    m_wsjtxStartBtn->setStyleSheet(kStartBtnStyle);
    connect(m_wsjtxStartBtn, &QPushButton::clicked, this, [this] {
        if (m_wsjtxClient && m_wsjtxClient->isListening()) {
            emit wsjtxStopRequested();
            return;
        }
        quint16 port = static_cast<quint16>(m_wsjtxPortSpin->value());
        QString addr = m_wsjtxAddrEdit->text().trimmed();
        if (addr.isEmpty()) {
            addr = "224.0.0.1";
        }
        auto& settings = AppSettings::instance();
        settings.setValue("WsjtxAddress", addr);
        settings.setValue("WsjtxPort", port);
        settings.save();
        emit wsjtxStartRequested(addr, port);
    });
    btnRow->addWidget(m_wsjtxStartBtn);
    connLayout->addLayout(btnRow);

    layout->addWidget(connGroup);

    // Spot filters with inline color pickers
    auto* filterRow = new QHBoxLayout;
    filterRow->setSpacing(6);
    auto* filterLabel = new QLabel("Spot Filter:");
    filterLabel->setStyleSheet("QLabel { color: #808080; font-size: 14px; }");
    filterRow->addWidget(filterLabel);

    const QString cbStyle =
        "QCheckBox { color: #a0b0c0; font-size: 14px; spacing: 3px; }"
        "QCheckBox::indicator { width: 14px; height: 14px; }";

    // CQ color + checkbox
    QColor cqColor(s.value("WsjtxColorCQ", "#00FF00").toString());
    m_wsjtxColorCQ = new QPushButton;
    m_wsjtxColorCQ->setObjectName("wsjtxColorCQ");
    m_wsjtxColorCQ->setFixedSize(18, 18);
    m_wsjtxColorCQ->setStyleSheet(swatchStyle(cqColor));
    connect(m_wsjtxColorCQ, &QPushButton::clicked, this, [this] {
        QColor c = QColorDialog::getColor(
            QColor(AppSettings::instance().value("WsjtxColorCQ", "#00FF00").toString()),
            this, "CQ Spot Color");
        if (c.isValid()) {
            m_wsjtxColorCQ->setStyleSheet(swatchStyle(c));
            AppSettings::instance().setValue("WsjtxColorCQ", c.name());
            AppSettings::instance().save();
        }
    });
    filterRow->addWidget(m_wsjtxColorCQ);

    m_wsjtxFilterCQ = new QCheckBox("CQ");
    m_wsjtxFilterCQ->setObjectName("wsjtxFilterCQ");
    m_wsjtxFilterCQ->setChecked(s.value("WsjtxFilterCQ", "True").toString() == "True");
    m_wsjtxFilterCQ->setStyleSheet(cbStyle);
    connect(m_wsjtxFilterCQ, &QCheckBox::toggled, this, [this](bool on) {
        if (on) {
            m_wsjtxFilterPOTA->setChecked(false);
        }
        auto& settings = AppSettings::instance();
        settings.setValue("WsjtxFilterCQ", on ? "True" : "False");
        settings.save();
    });
    filterRow->addWidget(m_wsjtxFilterCQ, 1);

    // CQ POTA color + checkbox
    QColor potaColor(s.value("WsjtxColorPOTA", "#00FFFF").toString());
    m_wsjtxColorPOTA = new QPushButton;
    m_wsjtxColorPOTA->setObjectName("wsjtxColorPOTA");
    m_wsjtxColorPOTA->setFixedSize(18, 18);
    m_wsjtxColorPOTA->setStyleSheet(swatchStyle(potaColor));
    connect(m_wsjtxColorPOTA, &QPushButton::clicked, this, [this] {
        QColor c = QColorDialog::getColor(
            QColor(AppSettings::instance().value("WsjtxColorPOTA", "#00FFFF").toString()),
            this, "CQ POTA Spot Color");
        if (c.isValid()) {
            m_wsjtxColorPOTA->setStyleSheet(swatchStyle(c));
            AppSettings::instance().setValue("WsjtxColorPOTA", c.name());
            AppSettings::instance().save();
        }
    });
    filterRow->addWidget(m_wsjtxColorPOTA);

    m_wsjtxFilterPOTA = new QCheckBox("CQ POTA");
    m_wsjtxFilterPOTA->setObjectName("wsjtxFilterPOTA");
    m_wsjtxFilterPOTA->setChecked(s.value("WsjtxFilterPOTA", "True").toString() == "True");
    m_wsjtxFilterPOTA->setStyleSheet(cbStyle);
    connect(m_wsjtxFilterPOTA, &QCheckBox::toggled, this, [this](bool on) {
        if (on) {
            m_wsjtxFilterCQ->setChecked(false);
        }
        auto& settings = AppSettings::instance();
        settings.setValue("WsjtxFilterPOTA", on ? "True" : "False");
        settings.save();
    });
    filterRow->addWidget(m_wsjtxFilterPOTA, 1);

    // Calling Me color + checkbox
    QColor callingMeColor(s.value("WsjtxColorCallingMe", "#FF0000").toString());
    m_wsjtxColorCallingMe = new QPushButton;
    m_wsjtxColorCallingMe->setObjectName("wsjtxColorCallingMe");
    m_wsjtxColorCallingMe->setFixedSize(18, 18);
    m_wsjtxColorCallingMe->setStyleSheet(swatchStyle(callingMeColor));
    connect(m_wsjtxColorCallingMe, &QPushButton::clicked, this, [this] {
        QColor c = QColorDialog::getColor(
            QColor(AppSettings::instance().value("WsjtxColorCallingMe", "#FF0000").toString()),
            this, "Calling Me Spot Color");
        if (c.isValid()) {
            m_wsjtxColorCallingMe->setStyleSheet(swatchStyle(c));
            AppSettings::instance().setValue("WsjtxColorCallingMe", c.name());
            AppSettings::instance().save();
        }
    });
    filterRow->addWidget(m_wsjtxColorCallingMe);

    m_wsjtxFilterCallingMe = new QCheckBox("Calling Me");
    m_wsjtxFilterCallingMe->setObjectName("wsjtxFilterCallingMe");
    m_wsjtxFilterCallingMe->setChecked(s.value("WsjtxFilterCallingMe", "True").toString() == "True");
    m_wsjtxFilterCallingMe->setStyleSheet(cbStyle);
    connect(m_wsjtxFilterCallingMe, &QCheckBox::toggled, this, [](bool on) {
        auto& settings = AppSettings::instance();
        settings.setValue("WsjtxFilterCallingMe", on ? "True" : "False");
        settings.save();
    });
    filterRow->addWidget(m_wsjtxFilterCallingMe, 1);

    // Default color (no checkbox)
    QColor defaultColor(s.value("WsjtxColorDefault", "#FFFFFF").toString());
    m_wsjtxColorDefault = new QPushButton;
    m_wsjtxColorDefault->setObjectName("wsjtxColorDefault");
    m_wsjtxColorDefault->setFixedSize(18, 18);
    m_wsjtxColorDefault->setStyleSheet(swatchStyle(defaultColor));
    connect(m_wsjtxColorDefault, &QPushButton::clicked, this, [this] {
        QColor c = QColorDialog::getColor(
            QColor(AppSettings::instance().value("WsjtxColorDefault", "#FFFFFF").toString()),
            this, "Default Spot Color");
        if (c.isValid()) {
            m_wsjtxColorDefault->setStyleSheet(swatchStyle(c));
            AppSettings::instance().setValue("WsjtxColorDefault", c.name());
            AppSettings::instance().save();
        }
    });
    filterRow->addWidget(m_wsjtxColorDefault);
    auto* defaultLabel = new QLabel("Default");
    defaultLabel->setStyleSheet("QLabel { color: #a0b0c0; font-size: 14px; }");
    filterRow->addWidget(defaultLabel);

    layout->addLayout(filterRow);

    // Decodes label + spot-life slider
    auto* decodeRow = new QHBoxLayout;
    auto* consoleLabel = new QLabel("WSJT-X Decodes");
    consoleLabel->setStyleSheet("QLabel { color: #00b4d8; font-weight: bold; }");
    decodeRow->addWidget(consoleLabel);
    decodeRow->addStretch();

    auto* lifeLabel = new QLabel("Spot Life:");
    lifeLabel->setStyleSheet("QLabel { color: #808080; font-size: 12px; }");
    decodeRow->addWidget(lifeLabel);

    int wsjtxLife = s.value("WsjtxSpotLifetime", 120).toInt();
    auto* wsjtxLifeSlider = new QSlider(Qt::Horizontal);
    wsjtxLifeSlider->setObjectName("wsjtxLifeSlider");
    wsjtxLifeSlider->setRange(30, 300);
    wsjtxLifeSlider->setValue(wsjtxLife);
    wsjtxLifeSlider->setFixedWidth(120);
    decodeRow->addWidget(wsjtxLifeSlider);

    auto* wsjtxLifeValue = new QLabel(QString("%1s").arg(wsjtxLife));
    wsjtxLifeValue->setFixedWidth(35);
    wsjtxLifeValue->setAlignment(Qt::AlignRight);
    wsjtxLifeValue->setStyleSheet("QLabel { color: #a0b0c0; font-size: 12px; }");
    decodeRow->addWidget(wsjtxLifeValue);

    connect(wsjtxLifeSlider, &QSlider::valueChanged, this, [wsjtxLifeValue](int v) {
        wsjtxLifeValue->setText(QString("%1s").arg(v));
        auto& settings = AppSettings::instance();
        settings.setValue("WsjtxSpotLifetime", v);
        settings.save();
    });
    layout->addLayout(decodeRow);

    m_wsjtxConsole = new QPlainTextEdit;
    m_wsjtxConsole->setObjectName("wsjtxConsole");
    m_wsjtxConsole->setReadOnly(true);
    m_wsjtxConsole->setMaximumBlockCount(2000);
    m_wsjtxConsole->setStyleSheet(kConsoleStyle);
    layout->addWidget(m_wsjtxConsole, 1);

    tabs->addTab(page, "WSJT-X");
}

// From AetherSDR src/gui/DxClusterDialog.cpp:1239-1345 [@0cd4559].
// SpotCollector tab: UDP port spinbox + help text + auto-start
// toggle + start/stop button + status label + raw-event console.
// Signals: `spotCollectorStartRequested(port)` /
// `spotCollectorStopRequested`. NereusSDR addition: objectName() on
// every test-relevant widget.
void SpotHubDialog::buildSpotCollectorTab(QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(8);

    auto& s = AppSettings::instance();

    auto* connGroup = new QGroupBox("SpotCollector UDP Listener");
    auto* connLayout = new QVBoxLayout(connGroup);
    connLayout->setSpacing(4);

    auto* grid = new QGridLayout;
    grid->setColumnStretch(1, 1);

    grid->addWidget(new QLabel("UDP Port:"), 0, 0);
    m_scPortSpin = new QSpinBox;
    m_scPortSpin->setObjectName("scPortSpin");
    m_scPortSpin->setRange(1, 65535);
    m_scPortSpin->setValue(s.value("SpotCollectorPort", 9999).toInt());
    m_scPortSpin->setStyleSheet(kSpinBoxStyle);
    grid->addWidget(m_scPortSpin, 0, 1);

    connLayout->addLayout(grid);

    auto* helpLabel = new QLabel(
        "Receives DX spots from DXLab SpotCollector via UDP push.\n"
        "In SpotCollector, enable UDP broadcast to this port (default 9999).\n"
        "Alternatively, use the DX Cluster tab to connect to SpotCollector's telnet interface.");
    helpLabel->setWordWrap(true);
    helpLabel->setStyleSheet("QLabel { color: #8aa8c0; font-size: 11px; }");
    connLayout->addWidget(helpLabel);

    auto* btnRow = new QHBoxLayout;
    m_scAutoStartBtn = new QPushButton(
        s.value("SpotCollectorAutoStart", "False").toString() == "True"
            ? "Auto-Start: ON" : "Auto-Start: OFF");
    m_scAutoStartBtn->setObjectName("scAutoStartBtn");
    m_scAutoStartBtn->setCheckable(true);
    m_scAutoStartBtn->setChecked(s.value("SpotCollectorAutoStart", "False").toString() == "True");
    m_scAutoStartBtn->setStyleSheet(kAutoToggleStyle);
    connect(m_scAutoStartBtn, &QPushButton::toggled, this, [this](bool on) {
        m_scAutoStartBtn->setText(on ? "Auto-Start: ON" : "Auto-Start: OFF");
        auto& settings = AppSettings::instance();
        settings.setValue("SpotCollectorAutoStart", on ? "True" : "False");
        settings.save();
    });
    btnRow->addWidget(m_scAutoStartBtn);
    btnRow->addStretch();

    m_scStatusLabel = new QLabel("Stopped");
    m_scStatusLabel->setObjectName("scStatusLabel");
    m_scStatusLabel->setStyleSheet(kStatusIdleStyle);
    btnRow->addWidget(m_scStatusLabel);
    btnRow->addStretch();

    m_scStartBtn = new QPushButton(m_spotCollectorClient && m_spotCollectorClient->isListening()
                                       ? "Stop" : "Start");
    m_scStartBtn->setObjectName("scStartBtn");
    m_scStartBtn->setFixedWidth(100);
    m_scStartBtn->setStyleSheet(kStartBtnStyle);
    connect(m_scStartBtn, &QPushButton::clicked, this, [this] {
        if (m_spotCollectorClient && m_spotCollectorClient->isListening()) {
            emit spotCollectorStopRequested();
            return;
        }
        quint16 port = static_cast<quint16>(m_scPortSpin->value());
        auto& settings = AppSettings::instance();
        settings.setValue("SpotCollectorPort", port);
        settings.save();
        emit spotCollectorStartRequested(port);
    });
    btnRow->addWidget(m_scStartBtn);
    connLayout->addLayout(btnRow);

    layout->addWidget(connGroup);

    auto* consoleLabel = new QLabel("SpotCollector Spots");
    consoleLabel->setStyleSheet("QLabel { color: #00b4d8; font-weight: bold; }");
    layout->addWidget(consoleLabel);

    m_scConsole = new QPlainTextEdit;
    m_scConsole->setObjectName("scConsole");
    m_scConsole->setReadOnly(true);
    m_scConsole->setMaximumBlockCount(2000);
    m_scConsole->setStyleSheet(kConsoleStyle);
    layout->addWidget(m_scConsole, 1);

    if (m_spotCollectorClient && m_spotCollectorClient->isListening()) {
        m_scStatusLabel->setText(QString("Listening on port %1").arg(m_scPortSpin->value()));
        m_scStatusLabel->setStyleSheet("QLabel { color: #00b4d8; font-size: 11px; }");
        m_scStartBtn->setText("Stop");
    }

    tabs->addTab(page, "SpotCollector");
}

// From AetherSDR src/gui/DxClusterDialog.cpp:1347-1479 [@0cd4559].
// POTA tab: HTTP polling-interval spinbox + auto-start toggle +
// start/stop button + status label + raw-event console + spot
// color picker. Signals: `potaStartRequested(interval)` /
// `potaStopRequested`. NereusSDR addition: objectName() on every
// test-relevant widget.
void SpotHubDialog::buildPotaTab(QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(8);

    auto& s = AppSettings::instance();

    auto* connGroup = new QGroupBox("POTA Spot Feed");
    auto* connLayout = new QVBoxLayout(connGroup);
    connLayout->setSpacing(4);

    auto* grid = new QGridLayout;
    grid->setColumnStretch(1, 1);
    int row = 0;

    grid->addWidget(new QLabel("Server:"), row, 0);
    auto* serverLabel = new QLabel("api.pota.app (HTTP polling)");
    serverLabel->setStyleSheet("QLabel { color: #808890; }");
    grid->addWidget(serverLabel, row, 1);
    row++;

    grid->addWidget(new QLabel("Poll Interval:"), row, 0);
    m_potaIntervalSpin = new QSpinBox;
    m_potaIntervalSpin->setObjectName("potaIntervalSpin");
    m_potaIntervalSpin->setRange(15, 300);
    m_potaIntervalSpin->setValue(s.value("PotaPollInterval", 30).toInt());
    m_potaIntervalSpin->setSuffix(" sec");
    m_potaIntervalSpin->setStyleSheet(kSpinBoxStyle);
    connect(m_potaIntervalSpin, &QSpinBox::valueChanged, this, [](int v) {
        auto& settings = AppSettings::instance();
        settings.setValue("PotaPollInterval", v);
        settings.save();
    });
    grid->addWidget(m_potaIntervalSpin, row, 1);
    row++;

    connLayout->addLayout(grid);

    auto* btnRow = new QHBoxLayout;
    m_potaAutoStartBtn = new QPushButton(
        s.value("PotaAutoStart", "False").toString() == "True"
            ? "Auto-Start: ON" : "Auto-Start: OFF");
    m_potaAutoStartBtn->setObjectName("potaAutoStartBtn");
    m_potaAutoStartBtn->setCheckable(true);
    m_potaAutoStartBtn->setChecked(s.value("PotaAutoStart", "False").toString() == "True");
    m_potaAutoStartBtn->setStyleSheet(kAutoToggleStyle);
    connect(m_potaAutoStartBtn, &QPushButton::toggled, this, [this](bool on) {
        m_potaAutoStartBtn->setText(on ? "Auto-Start: ON" : "Auto-Start: OFF");
        auto& settings = AppSettings::instance();
        settings.setValue("PotaAutoStart", on ? "True" : "False");
        settings.save();
    });
    btnRow->addWidget(m_potaAutoStartBtn);
    btnRow->addStretch();

    m_potaStatusLabel = new QLabel("Stopped");
    m_potaStatusLabel->setObjectName("potaStatusLabel");
    m_potaStatusLabel->setStyleSheet(kStatusIdleStyle);
    btnRow->addWidget(m_potaStatusLabel);
    btnRow->addStretch();

    m_potaStartBtn = new QPushButton(m_potaClient && m_potaClient->isPolling()
                                         ? "Stop" : "Start");
    m_potaStartBtn->setObjectName("potaStartBtn");
    m_potaStartBtn->setFixedWidth(100);
    m_potaStartBtn->setStyleSheet(kStartBtnStyle);
    connect(m_potaStartBtn, &QPushButton::clicked, this, [this] {
        if (m_potaClient && m_potaClient->isPolling()) {
            emit potaStopRequested();
            return;
        }
        int interval = m_potaIntervalSpin->value();
        auto& settings = AppSettings::instance();
        settings.setValue("PotaPollInterval", interval);
        settings.save();
        emit potaStartRequested(interval);
    });
    btnRow->addWidget(m_potaStartBtn);
    connLayout->addLayout(btnRow);

    layout->addWidget(connGroup);

    auto* consoleRow = new QHBoxLayout;
    auto* consoleLabel = new QLabel("POTA Activations");
    consoleLabel->setStyleSheet("QLabel { color: #00b4d8; font-weight: bold; }");
    consoleRow->addWidget(consoleLabel);
    consoleRow->addStretch();

    auto* spotColorLabel = new QLabel("Spot Color:");
    spotColorLabel->setStyleSheet("QLabel { color: #808080; font-size: 12px; }");
    consoleRow->addWidget(spotColorLabel);

    QColor potaColor(s.value("PotaSpotColor", "#FFFF00").toString());
    auto* potaColorBtn = new QPushButton;
    potaColorBtn->setObjectName("potaColorBtn");
    potaColorBtn->setFixedSize(18, 18);
    potaColorBtn->setStyleSheet(swatchStyle(potaColor));
    connect(potaColorBtn, &QPushButton::clicked, this, [this, potaColorBtn] {
        QColor c = QColorDialog::getColor(
            QColor(AppSettings::instance().value("PotaSpotColor", "#FFFF00").toString()),
            this, "POTA Spot Color");
        if (c.isValid()) {
            potaColorBtn->setStyleSheet(swatchStyle(c));
            AppSettings::instance().setValue("PotaSpotColor", c.name());
            AppSettings::instance().save();
        }
    });
    consoleRow->addWidget(potaColorBtn);
    layout->addLayout(consoleRow);

    m_potaConsole = new QPlainTextEdit;
    m_potaConsole->setObjectName("potaConsole");
    m_potaConsole->setReadOnly(true);
    m_potaConsole->setMaximumBlockCount(2000);
    m_potaConsole->setStyleSheet(kConsoleStyle);
    layout->addWidget(m_potaConsole, 1);

    tabs->addTab(page, "POTA");
}

// From AetherSDR src/gui/DxClusterDialog.cpp:1482-1596 [@0cd4559].
// FreeDV tab: server label (qso.freedv.org, WebSocket) + auto-start
// toggle + start/stop button + status label + raw-event console +
// spot color picker. NereusSDR divergence from upstream: AetherSDR
// gates the entire builder on HAVE_WEBSOCKETS; NereusSDR builds
// unconditionally because `FreeDVReporterClient` (Task B5) is a
// native QWebSocket + nlohmann::json port rather than an optional
// dependency. Signals: `freedvStartRequested` / `freedvStopRequested`.
// NereusSDR addition: objectName() on every test-relevant widget.
void SpotHubDialog::buildFreeDvTab(QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(8);

    auto& s = AppSettings::instance();

    auto* connGroup = new QGroupBox("FreeDV QSO Reporter");
    auto* connLayout = new QVBoxLayout(connGroup);
    connLayout->setSpacing(4);

    auto* grid = new QGridLayout;
    grid->setColumnStretch(1, 1);
    int row = 0;

    grid->addWidget(new QLabel("Server:"), row, 0);
    auto* serverLabel = new QLabel("qso.freedv.org (WebSocket)");
    serverLabel->setStyleSheet("QLabel { color: #808890; }");
    grid->addWidget(serverLabel, row, 1);
    row++;

    connLayout->addLayout(grid);

    auto* btnRow = new QHBoxLayout;
    m_freedvAutoStartBtn = new QPushButton(
        s.value("FreeDvAutoStart", "False").toString() == "True"
            ? "Auto-Start: ON" : "Auto-Start: OFF");
    m_freedvAutoStartBtn->setObjectName("freedvAutoStartBtn");
    m_freedvAutoStartBtn->setCheckable(true);
    m_freedvAutoStartBtn->setChecked(s.value("FreeDvAutoStart", "False").toString() == "True");
    m_freedvAutoStartBtn->setStyleSheet(kAutoToggleStyle);
    connect(m_freedvAutoStartBtn, &QPushButton::toggled, this, [this](bool on) {
        m_freedvAutoStartBtn->setText(on ? "Auto-Start: ON" : "Auto-Start: OFF");
        auto& settings = AppSettings::instance();
        settings.setValue("FreeDvAutoStart", on ? "True" : "False");
        settings.save();
    });
    btnRow->addWidget(m_freedvAutoStartBtn);
    btnRow->addStretch();

    m_freedvStatusLabel = new QLabel("Stopped");
    m_freedvStatusLabel->setObjectName("freedvStatusLabel");
    m_freedvStatusLabel->setStyleSheet(kStatusIdleStyle);
    btnRow->addWidget(m_freedvStatusLabel);
    btnRow->addStretch();

    m_freedvStartBtn = new QPushButton(m_freedvClient && m_freedvClient->isConnected()
                                           ? "Stop" : "Start");
    m_freedvStartBtn->setObjectName("freedvStartBtn");
    m_freedvStartBtn->setFixedWidth(100);
    m_freedvStartBtn->setStyleSheet(kStartBtnStyle);
    connect(m_freedvStartBtn, &QPushButton::clicked, this, [this] {
        if (m_freedvClient && m_freedvClient->isConnected()) {
            emit freedvStopRequested();
            return;
        }
        emit freedvStartRequested();
    });
    btnRow->addWidget(m_freedvStartBtn);
    connLayout->addLayout(btnRow);

    layout->addWidget(connGroup);

    auto* consoleRow = new QHBoxLayout;
    auto* consoleLabel = new QLabel("FreeDV Spots");
    consoleLabel->setStyleSheet("QLabel { color: #00b4d8; font-weight: bold; }");
    consoleRow->addWidget(consoleLabel);
    consoleRow->addStretch();

    auto* spotColorLabel = new QLabel("Spot Color:");
    spotColorLabel->setStyleSheet("QLabel { color: #808080; font-size: 12px; }");
    consoleRow->addWidget(spotColorLabel);

    QColor freedvColor(s.value("FreeDvSpotColor", "#FF8C00").toString());
    auto* freedvColorBtn = new QPushButton;
    freedvColorBtn->setObjectName("freedvColorBtn");
    freedvColorBtn->setFixedSize(18, 18);
    freedvColorBtn->setStyleSheet(swatchStyle(freedvColor));
    connect(freedvColorBtn, &QPushButton::clicked, this, [this, freedvColorBtn] {
        QColor c = QColorDialog::getColor(
            QColor(AppSettings::instance().value("FreeDvSpotColor", "#FF8C00").toString()),
            this, "FreeDV Spot Color");
        if (c.isValid()) {
            freedvColorBtn->setStyleSheet(swatchStyle(c));
            AppSettings::instance().setValue("FreeDvSpotColor", c.name());
            AppSettings::instance().save();
        }
    });
    consoleRow->addWidget(freedvColorBtn);
    layout->addLayout(consoleRow);

    m_freedvConsole = new QPlainTextEdit;
    m_freedvConsole->setObjectName("freedvConsole");
    m_freedvConsole->setReadOnly(true);
    m_freedvConsole->setMaximumBlockCount(2000);
    m_freedvConsole->setStyleSheet(kConsoleStyle);
    layout->addWidget(m_freedvConsole, 1);

    tabs->addTab(page, "FreeDV");
}

// NereusSDR-native, no upstream. AetherSDR has no PSK Reporter tab.
// PSK Reporter is a UDP/IPFIX reporting service for digital-mode
// activity; NereusSDR ships the `PskReporterClient` (Phase 3J-2
// Task B6, ported from `freedv-gui src/reporting/pskreporter.cpp`).
// Tab follows the F2 uniform template: identity fields (callsign +
// grid) + auto-start toggle + start/stop button + status label +
// raw-event console. Default server is `report.pskreporter.info:4739`
// (matches PskReporterClient default). Signals:
// `pskStartRequested` / `pskStopRequested`. PSK Reporter is
// primarily a write-only flow (NereusSDR sends decodes to the
// pool); the console shows queued / sent record counts so users
// can confirm reports are flowing.
void SpotHubDialog::buildPskTab(QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setSpacing(8);

    auto& s = AppSettings::instance();

    auto* connGroup = new QGroupBox("PSK Reporter Identity");
    auto* connLayout = new QVBoxLayout(connGroup);
    connLayout->setSpacing(4);

    auto* grid = new QGridLayout;
    grid->setColumnStretch(1, 1);
    int row = 0;

    QString defaultCall = s.value("PskReporterCallsign").toString();
    if (defaultCall.isEmpty()) {
        defaultCall = s.value("DxClusterCallsign").toString();
    }

    grid->addWidget(new QLabel("Callsign:"), row, 0);
    m_pskCallEdit = new QLineEdit(defaultCall);
    m_pskCallEdit->setObjectName("pskCallEdit");
    m_pskCallEdit->setPlaceholderText("your callsign");
    m_pskCallEdit->setStyleSheet(kLineEditStyle);
    grid->addWidget(m_pskCallEdit, row, 1);
    row++;

    grid->addWidget(new QLabel("Grid:"), row, 0);
    m_pskGridEdit = new QLineEdit(s.value("PskReporterGrid").toString());
    m_pskGridEdit->setObjectName("pskGridEdit");
    m_pskGridEdit->setPlaceholderText("4-character or 6-character Maidenhead");
    m_pskGridEdit->setStyleSheet(kLineEditStyle);
    grid->addWidget(m_pskGridEdit, row, 1);
    row++;

    connLayout->addLayout(grid);

    auto* helpLabel = new QLabel(
        "Reports your radio's decodes to the PSK Reporter pool via\n"
        "report.pskreporter.info:4739 (UDP/IPFIX). Sent records are\n"
        "queued and flushed periodically; the console below shows\n"
        "send activity. Callsign and grid are required.");
    helpLabel->setWordWrap(true);
    helpLabel->setStyleSheet("QLabel { color: #8aa8c0; font-size: 11px; }");
    connLayout->addWidget(helpLabel);

    auto* btnRow = new QHBoxLayout;
    m_pskAutoStartBtn = new QPushButton(
        s.value("PskReporterAutoStart", "False").toString() == "True"
            ? "Auto-Start: ON" : "Auto-Start: OFF");
    m_pskAutoStartBtn->setObjectName("pskAutoStartBtn");
    m_pskAutoStartBtn->setCheckable(true);
    m_pskAutoStartBtn->setChecked(s.value("PskReporterAutoStart", "False").toString() == "True");
    m_pskAutoStartBtn->setStyleSheet(kAutoToggleStyle);
    connect(m_pskAutoStartBtn, &QPushButton::toggled, this, [this](bool on) {
        m_pskAutoStartBtn->setText(on ? "Auto-Start: ON" : "Auto-Start: OFF");
        auto& settings = AppSettings::instance();
        settings.setValue("PskReporterAutoStart", on ? "True" : "False");
        settings.save();
    });
    btnRow->addWidget(m_pskAutoStartBtn);
    btnRow->addStretch();

    m_pskStatusLabel = new QLabel("Stopped");
    m_pskStatusLabel->setObjectName("pskStatusLabel");
    m_pskStatusLabel->setStyleSheet(kStatusIdleStyle);
    btnRow->addWidget(m_pskStatusLabel);
    btnRow->addStretch();

    m_pskStartBtn = new QPushButton(m_pskClient && m_pskClient->isListening()
                                        ? "Stop" : "Start");
    m_pskStartBtn->setObjectName("pskStartBtn");
    m_pskStartBtn->setFixedWidth(100);
    m_pskStartBtn->setStyleSheet(kStartBtnStyle);
    connect(m_pskStartBtn, &QPushButton::clicked, this, [this] {
        if (m_pskClient && m_pskClient->isListening()) {
            emit pskStopRequested();
            return;
        }
        QString call = m_pskCallEdit->text().trimmed().toUpper();
        QString gridSquare = m_pskGridEdit->text().trimmed().toUpper();
        if (call.isEmpty() || gridSquare.isEmpty()) {
            m_pskStatusLabel->setText("Callsign and grid are required");
            m_pskStatusLabel->setStyleSheet("QLabel { color: #ff4444; font-size: 11px; }");
            return;
        }
        auto& settings = AppSettings::instance();
        settings.setValue("PskReporterCallsign", call);
        settings.setValue("PskReporterGrid", gridSquare);
        settings.save();
        emit pskStartRequested();
    });
    btnRow->addWidget(m_pskStartBtn);
    connLayout->addLayout(btnRow);

    layout->addWidget(connGroup);

    auto* consoleLabel = new QLabel("PSK Reporter Activity");
    consoleLabel->setStyleSheet("QLabel { color: #00b4d8; font-weight: bold; }");
    layout->addWidget(consoleLabel);

    m_pskConsole = new QPlainTextEdit;
    m_pskConsole->setObjectName("pskConsole");
    m_pskConsole->setReadOnly(true);
    m_pskConsole->setMaximumBlockCount(2000);
    m_pskConsole->setStyleSheet(kConsoleStyle);
    layout->addWidget(m_pskConsole, 1);

    tabs->addTab(page, "PSK Reporter");
}

// F3 stub - Spot List tab. F3 (Task F3) fills in the merged
// 8-column table view (SpotTableModel + BandFilterProxy from Phase
// 3J-2 Task D2) + band filter checkboxes + spot-count display +
// clear-all button.
void SpotHubDialog::buildSpotListTab(QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->addWidget(new QLabel("Spot List tab content lands in Task F3.", page));
    tabs->addTab(page, "Spot List");
}

// F4 stub - Display tab. F4 (Task F4) fills in the DXCC
// color-coding controls + global spot rendering toggles
// (font size, overlay levels, override colors, etc.).
void SpotHubDialog::buildDisplayTab(QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->addWidget(new QLabel("Display tab content lands in Task F4.", page));
    tabs->addTab(page, "Display");
}

} // namespace NereusSDR
