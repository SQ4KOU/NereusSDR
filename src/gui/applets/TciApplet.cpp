// =================================================================
// src/gui/applets/TciApplet.cpp  (NereusSDR)
// =================================================================
//
// NereusSDR-original — TCI status applet for Container #0 stack.
//
// This file contains no ported Thetis logic; it is a new UI surface
// built to NereusSDR design conventions (Template C, plain English
// Qt strings, AppSettings persistence, StyleConstants palette).
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-10 — Phase 3J-1 Task 21.1 by J.J. Boyd (KG4VCF);
//                AI-assisted transformation via Anthropic Claude Code.
//                Header row (status dot + label + port + client count
//                + Setup button), Slice A row (level meter + gain
//                slider), TX row (level meter + gain slider), footer
//                (client count + Show clients link).  Phase 21 STUB:
//                level meters use placeholder values; real meter wiring
//                + setup/show-clients navigation in Phase 23.
//                AppSettings keys: TciSliceAGain, TciTxGain.
// =================================================================

#ifdef HAVE_WEBSOCKETS

#include "TciApplet.h"

#include "core/AppSettings.h"
#include "core/LogCategories.h"
#include "core/TciServer.h"
#include "gui/HGauge.h"
#include "gui/StyleConstants.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

#include <cmath>

namespace NereusSDR {

// ── File-local helpers ────────────────────────────────────────────────────────

namespace {

// Status dot color tokens.
constexpr auto kDotOff      = "#cc2222";   // red  — server stopped
constexpr auto kDotRunning  = "#00cc66";   // green — running, no clients
constexpr auto kDotClients  = "#00e0e0";   // cyan  — running with clients

// Gain slider range in dB (post-DSP TCI stream volume).
constexpr int kGainMin = -60;
constexpr int kGainMax =  0;

// AppSettings persistence keys.
constexpr auto kKeySliceAGain = "TciSliceAGain";
constexpr auto kKeyTxGain     = "TciTxGain";

// Small dot widget: a fixed-size label rendered as a filled circle.
QLabel* makeStatusDot(QWidget* parent)
{
    auto* dot = new QLabel(parent);
    dot->setFixedSize(10, 10);
    dot->setStyleSheet(QStringLiteral(
        "QLabel { background: %1; border-radius: 5px; }"
    ).arg(kDotOff));
    return dot;
}

// Compact secondary label.
QLabel* makeSecondaryLabel(const QString& text, QWidget* parent)
{
    auto* lbl = new QLabel(text, parent);
    lbl->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 10px; }"
    ).arg(Style::kTextSecondary));
    return lbl;
}

// Gain slider — horizontal, range [-60, 0].
QSlider* makeGainSlider(int savedDb, QWidget* parent)
{
    auto* s = new QSlider(Qt::Horizontal, parent);
    s->setRange(kGainMin, kGainMax);
    s->setValue(savedDb);
    s->setFixedHeight(18);
    s->setStyleSheet(Style::sliderHStyle());
    return s;
}

} // namespace

// ── Constructor ───────────────────────────────────────────────────────────────

TciApplet::TciApplet(TciServer* server, QWidget* parent)
    : AppletWidget(nullptr, parent)
    , m_server(server)
{
    buildUI();

    // Connect TciServer state-change signals.
    if (m_server) {
        connect(m_server, &TciServer::serverStarted,
                this, &TciApplet::onServerStarted);
        connect(m_server, &TciServer::serverStopped,
                this, &TciApplet::onServerStopped);
        connect(m_server, &TciServer::clientConnected,
                this, &TciApplet::onClientConnected);
        connect(m_server, &TciServer::clientDisconnected,
                this, &TciApplet::onClientDisconnected);
    }

    // 200 ms refresh timer for level meter polling.
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(200);
    connect(m_refreshTimer, &QTimer::timeout, this, &TciApplet::refresh);

    // Reflect initial server state.
    applyEnabledState(m_server && m_server->isRunning());
    if (m_server && m_server->isRunning()) {
        m_refreshTimer->start();
    }

    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

// ── UI construction ───────────────────────────────────────────────────────────

void TciApplet::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(appletTitleBar(QStringLiteral("TCI Server")));

    // ── Main content (header + slice + tx + footer) ───────────────────────────
    m_mainContent = new QWidget(this);
    {
        auto* vbox = new QVBoxLayout(m_mainContent);
        vbox->setContentsMargins(4, 2, 4, 4);
        vbox->setSpacing(3);
        buildHeaderRow(vbox);
        vbox->addWidget(divider());
        buildSliceRow(vbox);
        buildTxRow(vbox);
        vbox->addWidget(divider());
        buildFooter(vbox);
        vbox->addStretch();
    }
    root->addWidget(m_mainContent);

    // ── Disabled placeholder ──────────────────────────────────────────────────
    m_disabledContent = new QWidget(this);
    {
        auto* vbox = new QVBoxLayout(m_disabledContent);
        vbox->setContentsMargins(4, 6, 4, 6);
        vbox->setSpacing(4);
        buildDisabledState(vbox);
    }
    root->addWidget(m_disabledContent);
}

void TciApplet::buildHeaderRow(QVBoxLayout* vbox)
{
    auto* row = new QHBoxLayout;
    row->setSpacing(4);

    m_statusDot = makeStatusDot(this);
    row->addWidget(m_statusDot);

    auto* titleLbl = new QLabel(QStringLiteral("TCI Server"), this);
    titleLbl->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 10px; font-weight: bold; }"
    ).arg(Style::kTextPrimary));
    row->addWidget(titleLbl);

    m_portLabel = makeSecondaryLabel(QStringLiteral("port: -"), this);
    row->addWidget(m_portLabel);

    m_clientCountLabel = makeSecondaryLabel(QStringLiteral("0 clients"), this);
    row->addWidget(m_clientCountLabel);

    row->addStretch();

    m_setupButton = new QPushButton(QStringLiteral("Setup"), this);
    m_setupButton->setFixedSize(44, 20);
    m_setupButton->setToolTip(
        QStringLiteral("Open Setup to configure TCI Server options"));
    m_setupButton->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background: %1; border: 1px solid %2; border-radius: 3px;"
        "  color: %3; font-size: 9px; font-weight: bold; padding: 1px 3px;"
        "}"
        "QPushButton:hover { background: %4; }"
    ).arg(Style::kButtonBg, Style::kBorder, Style::kTextPrimary,
          Style::kButtonAltHover));
    connect(m_setupButton, &QPushButton::clicked,
            this, &TciApplet::onSetupClicked);
    row->addWidget(m_setupButton);

    vbox->addLayout(row);
}

void TciApplet::buildSliceRow(QVBoxLayout* vbox)
{
    // Row: [Slice A] [HGauge] [dB label]
    auto* row = new QHBoxLayout;
    row->setSpacing(4);

    auto* sliceLbl = makeSecondaryLabel(QStringLiteral("Slice A"), this);
    sliceLbl->setFixedWidth(44);
    row->addWidget(sliceLbl);

    m_sliceAGauge = new HGauge(this);
    m_sliceAGauge->setRange(-60.0, 0.0);
    m_sliceAGauge->setYellowStart(-12.0);
    m_sliceAGauge->setRedStart(-3.0);
    m_sliceAGauge->setValue(-60.0);  // Phase 21 placeholder: silent
    m_sliceAGauge->setTitle(QString());
    m_sliceAGauge->setUnit(QStringLiteral("dB"));
    row->addWidget(m_sliceAGauge, 1);

    vbox->addLayout(row);

    // Gain slider row (below the gauge)
    auto* gainRow = new QHBoxLayout;
    gainRow->setSpacing(4);

    auto* gainLbl = makeSecondaryLabel(QStringLiteral("Gain"), this);
    gainLbl->setFixedWidth(44);
    gainRow->addWidget(gainLbl);

    const int savedSliceAGain =
        AppSettings::instance()
            .value(QLatin1String(kKeySliceAGain), QStringLiteral("0"))
            .toString().toInt();
    m_sliceAGain = makeGainSlider(savedSliceAGain, this);
    m_sliceAGain->setToolTip(
        QStringLiteral("TCI audio output level for Slice A (-60 to 0 dB)"));
    connect(m_sliceAGain, &QSlider::valueChanged,
            this, &TciApplet::onSliceAGainChanged);
    gainRow->addWidget(m_sliceAGain, 1);

    m_sliceAGainLabel = insetValue(
        QStringLiteral("%1").arg(savedSliceAGain), 32);
    gainRow->addWidget(m_sliceAGainLabel);

    vbox->addLayout(gainRow);
}

void TciApplet::buildTxRow(QVBoxLayout* vbox)
{
    // Row: [TX] [HGauge] [dB label]
    auto* row = new QHBoxLayout;
    row->setSpacing(4);

    auto* txLbl = makeSecondaryLabel(QStringLiteral("TX"), this);
    txLbl->setFixedWidth(44);
    row->addWidget(txLbl);

    m_txGauge = new HGauge(this);
    m_txGauge->setRange(-60.0, 0.0);
    m_txGauge->setYellowStart(-12.0);
    m_txGauge->setRedStart(-3.0);
    m_txGauge->setValue(-60.0);  // Phase 21 placeholder: silent when not in MOX
    m_txGauge->setTitle(QString());
    m_txGauge->setUnit(QStringLiteral("dB"));
    row->addWidget(m_txGauge, 1);

    vbox->addLayout(row);

    // TX gain slider row
    auto* gainRow = new QHBoxLayout;
    gainRow->setSpacing(4);

    auto* gainLbl = makeSecondaryLabel(QStringLiteral("Gain"), this);
    gainLbl->setFixedWidth(44);
    gainRow->addWidget(gainLbl);

    const int savedTxGain =
        AppSettings::instance()
            .value(QLatin1String(kKeyTxGain), QStringLiteral("0"))
            .toString().toInt();
    m_txGain = makeGainSlider(savedTxGain, this);
    m_txGain->setToolTip(
        QStringLiteral("TCI audio output level for TX (-60 to 0 dB)"));
    connect(m_txGain, &QSlider::valueChanged,
            this, &TciApplet::onTxGainChanged);
    gainRow->addWidget(m_txGain, 1);

    m_txGainLabel = insetValue(
        QStringLiteral("%1").arg(savedTxGain), 32);
    gainRow->addWidget(m_txGainLabel);

    vbox->addLayout(gainRow);
}

void TciApplet::buildFooter(QVBoxLayout* vbox)
{
    auto* row = new QHBoxLayout;
    row->setSpacing(4);

    m_footerCount = makeSecondaryLabel(QStringLiteral("0 clients connected"), this);
    row->addWidget(m_footerCount);

    row->addStretch();

    m_showClientsLink = new QPushButton(QStringLiteral("Show clients ->"), this);
    m_showClientsLink->setFlat(true);
    m_showClientsLink->setToolTip(
        QStringLiteral("View the list of connected TCI clients"));
    m_showClientsLink->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  border: none; background: transparent;"
        "  color: %1; font-size: 9px; padding: 0; text-decoration: underline;"
        "}"
        "QPushButton:hover { color: %2; }"
    ).arg(Style::kAccent, Style::kTextPrimary));
    connect(m_showClientsLink, &QPushButton::clicked,
            this, &TciApplet::onShowClientsClicked);
    row->addWidget(m_showClientsLink);

    vbox->addLayout(row);
}

void TciApplet::buildDisabledState(QVBoxLayout* vbox)
{
    auto* hintLbl = new QLabel(
        QStringLiteral("TCI server is not running."), this);
    hintLbl->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 10px; }"
    ).arg(Style::kTextSecondary));
    hintLbl->setWordWrap(true);
    vbox->addWidget(hintLbl);

    auto* row = new QHBoxLayout;
    row->setSpacing(4);

    m_enableBtn = new QPushButton(QStringLiteral("Enable Server"), this);
    m_enableBtn->setFixedHeight(24);
    m_enableBtn->setToolTip(
        QStringLiteral("Start the TCI WebSocket server"));
    m_enableBtn->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background: %1; border: 1px solid %2; border-radius: 3px;"
        "  color: %3; font-size: 10px; font-weight: bold; padding: 2px 6px;"
        "}"
        "QPushButton:hover { background: %4; }"
    ).arg(Style::kButtonBg, Style::kBorder, Style::kTextPrimary,
          Style::kButtonAltHover));
    connect(m_enableBtn, &QPushButton::clicked, this,
            [this]() { onEnableToggled(true); });
    row->addWidget(m_enableBtn);
    row->addStretch();

    vbox->addLayout(row);

    auto* setupHintLbl = new QLabel(
        QStringLiteral("Configure in Setup -> Network -> TCI Server"), this);
    setupHintLbl->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 9px; }"
    ).arg(Style::kTextTertiary));
    vbox->addWidget(setupHintLbl);
}

// ── State management ──────────────────────────────────────────────────────────

void TciApplet::applyEnabledState(bool serverRunning)
{
    if (m_mainContent) {
        m_mainContent->setVisible(serverRunning);
    }
    if (m_disabledContent) {
        m_disabledContent->setVisible(!serverRunning);
    }
    updateStatusWidgets();
}

void TciApplet::updateStatusWidgets()
{
    if (!m_server) {
        return;
    }

    const bool running = m_server->isRunning();
    const int  count   = m_server->clientCount();

    // Status dot: red=off, green=running no clients, cyan=running with clients.
    if (m_statusDot) {
        const char* color = !running ? kDotOff
                          : (count > 0 ? kDotClients : kDotRunning);
        m_statusDot->setStyleSheet(QStringLiteral(
            "QLabel { background: %1; border-radius: 5px; }"
        ).arg(QLatin1String(color)));
    }

    if (m_portLabel) {
        if (running) {
            m_portLabel->setText(
                QStringLiteral("port: %1").arg(m_server->port()));
        } else {
            m_portLabel->setText(QStringLiteral("port: -"));
        }
    }

    if (m_clientCountLabel) {
        m_clientCountLabel->setText(
            count == 1 ? QStringLiteral("1 client")
                       : QStringLiteral("%1 clients").arg(count));
    }

    if (m_footerCount) {
        m_footerCount->setText(
            count == 1 ? QStringLiteral("1 client connected")
                       : QStringLiteral("%1 clients connected").arg(count));
    }
}

// ── AppletWidget override ─────────────────────────────────────────────────────

void TciApplet::syncFromModel()
{
    if (!m_server) {
        return;
    }
    applyEnabledState(m_server->isRunning());
}

// ── Periodic refresh ──────────────────────────────────────────────────────────

void TciApplet::refresh()
{
    // Update client count in case it changed via signals we missed while hidden.
    updateStatusWidgets();

    // Phase 21 placeholder: animate a small mock level on the gauges when
    // the server is running.  Real meter wiring deferred to Phase 24+.
    if (!m_server || !m_server->isRunning()) {
        return;
    }

    const int clientCount = m_server->clientCount();
    if (clientCount > 0) {
        // Produce a gentle sine-like oscillation in [-20, -8] dBFS so the
        // gauge isn't a static bar.  m_mockLevelPhase increments every tick.
        ++m_mockLevelPhase;
        const double angle = m_mockLevelPhase * 0.25;  // ~1 Hz at 200 ms tick
        const double mockDb = -14.0 + 6.0 * std::sin(angle);
        if (m_sliceAGauge) {
            m_sliceAGauge->setValue(mockDb);
        }
    } else {
        // No clients — hold gauges at floor.
        if (m_sliceAGauge) {
            m_sliceAGauge->setValue(-60.0);
        }
        if (m_txGauge) {
            m_txGauge->setValue(-60.0);
        }
    }
}

// ── Slot implementations ──────────────────────────────────────────────────────

void TciApplet::onServerStarted(quint16 port)
{
    Q_UNUSED(port)
    applyEnabledState(true);
    if (m_refreshTimer) {
        m_refreshTimer->start();
    }
}

void TciApplet::onServerStopped()
{
    if (m_refreshTimer) {
        m_refreshTimer->stop();
    }
    // Reset gauges to floor.
    if (m_sliceAGauge) {
        m_sliceAGauge->setValue(-60.0);
    }
    if (m_txGauge) {
        m_txGauge->setValue(-60.0);
    }
    m_mockLevelPhase = 0;
    applyEnabledState(false);
}

void TciApplet::onClientConnected(QWebSocket* socket)
{
    Q_UNUSED(socket)
    updateStatusWidgets();
}

void TciApplet::onClientDisconnected(QWebSocket* socket)
{
    Q_UNUSED(socket)
    updateStatusWidgets();
}

void TciApplet::onEnableToggled(bool on)
{
    if (!m_server) {
        return;
    }
    // Phase 3J-1 review P2.4: handle both enable (on=true → start) and
    // disable (on=false → stop) so the TciApplet enable button is a
    // true live toggle — not a one-way latch.
    if (on) {
        // Read the persisted port preference; default 50001.
        const quint16 port = static_cast<quint16>(
            AppSettings::instance()
                .value(QStringLiteral("TciServerPort"), QStringLiteral("50001"))
                .toString().toUShort());
        if (!m_server->start(port)) {
            qCWarning(lcTci) << "TciApplet: failed to start TCI server on port" << port;
        }
    } else {
        m_server->stop();
    }
}

void TciApplet::onSetupClicked()
{
    // Phase 21 stub: emit signal; Phase 23 wires MainWindow -> Setup nav.
    qCInfo(lcTci) << "TciApplet: setup button clicked (Phase 23 will wire navigation)";
    emit setupRequested();
}

void TciApplet::onShowClientsClicked()
{
    // Phase 21 stub: emit signal; Phase 22 wires navigation to ClientChainApplet.
    qCInfo(lcTci) << "TciApplet: show clients clicked (Phase 22 will wire navigation)";
    emit showClientsRequested();
}

void TciApplet::onSliceAGainChanged(int dB)
{
    if (m_sliceAGainLabel) {
        m_sliceAGainLabel->setText(QStringLiteral("%1").arg(dB));
    }
    auto& s = AppSettings::instance();
    s.setValue(QLatin1String(kKeySliceAGain), QString::number(dB));
    s.save();
    // Phase 22+: wire gain value into TCI audio pipeline.
}

void TciApplet::onTxGainChanged(int dB)
{
    if (m_txGainLabel) {
        m_txGainLabel->setText(QStringLiteral("%1").arg(dB));
    }
    auto& s = AppSettings::instance();
    s.setValue(QLatin1String(kKeyTxGain), QString::number(dB));
    s.save();
    // Phase 22+: wire TX gain into TCI audio pipeline.
}

} // namespace NereusSDR

#endif // HAVE_WEBSOCKETS
