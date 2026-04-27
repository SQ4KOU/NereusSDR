// =================================================================
// src/gui/TitleBar.cpp  (NereusSDR)
// =================================================================
//
// Ported from AetherSDR source:
//   src/gui/TitleBar.cpp (especially lines 27-34, 94-104, 282-295)
//
// AetherSDR is licensed under the GNU General Public License v3; see
// https://github.com/ten9876/AetherSDR for the contributor list and
// project-level LICENSE. NereusSDR is also GPLv3. AetherSDR source
// files carry no per-file GPL header; attribution is at project level
// per docs/attribution/HOW-TO-PORT.md rule 6.
//
// Upstream reference: AetherSDR v0.8.16 (2026-04).
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-20 — Ported/adapted in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via Anthropic
//                 Claude Code. Phase 3O Sub-Phase 10 Task 10c.
//                 Scoped-down port: master-output strip only. AetherSDR's
//                 heartbeat / multiFLEX / PC-audio / headphone /
//                 minimal-mode / feature-request widgets intentionally
//                 omitted (deferred to separate phases).
//                 Constructor preserves AetherSDR's 32 px fixed height,
//                 dark background (#0a0a14) with bottom-border (#203040),
//                 and the [menu][stretch][app-name][stretch][master]
//                 layout pattern. `setMenuBar()` is a line-for-line port
//                 of AetherSDR TitleBar.cpp:282-295 (restyle QMenuBar,
//                 `m_hbox->insertWidget(0, mb)`). App-name label: text
//                 "AetherSDR" swapped to "NereusSDR", accent colour
//                 (#00b4d8), font (14 px bold), and QLabel::AlignCenter
//                 preserved verbatim.
//                 Design spec: docs/architecture/2026-04-19-vax-design.md
//                 §6.3 + §7.3.
//   2026-04-20 — Task 10d: added the 💡 feature-request button as the
//                 rightmost element (past MasterOutputWidget, 6 px
//                 spacing). Button construction (lightbulb painter,
//                 28×28 sizing, #3a2a00/#806020 dark-amber style) moved
//                 verbatim from the now-deleted featureBar QToolBar in
//                 MainWindow.cpp. Emits featureRequestClicked(); MainWindow
//                 wires that to showFeatureRequestDialog. Matches
//                 AetherSDR's pattern of the feature button being the
//                 rightmost strip element.
//   2026-04-27 — Phase 3Q-6: implemented ConnectionSegment — state dot,
//                 radio name/IP text, ▲▼ Mbps readout, and 10 Hz
//                 throttled activity LED. Inserted at position 1 in the
//                 hbox (just after the menu bar). Design §4.1.
// =================================================================

#include "TitleBar.h"

#include "widgets/MasterOutputWidget.h"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>
#include <QPushButton>
#include <QSize>
#include <QSizePolicy>
#include <QTimer>

namespace NereusSDR {

namespace {

// Strip background + bottom border. From AetherSDR TitleBar.cpp:31.
constexpr auto kStripStyle =
    "TitleBar { background: #0a0a14; border-bottom: 1px solid #203040; }";

// App-name label. From AetherSDR TitleBar.cpp:102.
constexpr auto kAppNameStyle =
    "QLabel { color: #00b4d8; font-size: 14px; font-weight: bold; }";

// Menu-bar restyle. From AetherSDR TitleBar.cpp:285-290.
constexpr auto kMenuBarStyle =
    "QMenuBar { background: transparent; color: #8aa8c0; font-size: 12px; }"
    "QMenuBar::item { padding: 4px 8px; }"
    "QMenuBar::item:selected { background: #203040; color: #ffffff; }"
    "QMenu { background: #0f0f1a; color: #c8d8e8; border: 1px solid #304050; }"
    "QMenu::item:selected { background: #0070c0; }";

// Content-margins + spacing from AetherSDR TitleBar.cpp:34-35.
constexpr int kMarginLeft   = 4;
constexpr int kMarginTop    = 2;
constexpr int kMarginRight  = 8;
constexpr int kMarginBottom = 2;
constexpr int kSpacing      = 6;

// Fixed strip height. From AetherSDR TitleBar.cpp:30.
constexpr int kStripHeight = 32;

} // namespace

// =========================================================================
// ConnectionSegment implementation
// =========================================================================

ConnectionSegment::ConnectionSegment(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(32);
    setMinimumWidth(280);

    // LED throttle: one-shot 100 ms timer that turns the LED off after a
    // pulse. frameTick() drops calls that arrive while the timer is active,
    // giving ≤10 Hz visible refresh on high-rate frame streams.
    m_ledThrottle.setSingleShot(true);
    m_ledThrottle.setInterval(100);  // 10 Hz max
    connect(&m_ledThrottle, &QTimer::timeout, this, [this]() {
        m_ledOn = false;
        update();
    });
}

void ConnectionSegment::setState(ConnectionState s)
{
    m_state = s;
    update();
}

void ConnectionSegment::setRadio(const QString& name, const QHostAddress& ip)
{
    m_name = name;
    m_ip   = ip;
    update();
}

void ConnectionSegment::setRates(double rxMbps, double txMbps)
{
    m_rxMbps = rxMbps;
    m_txMbps = txMbps;
    update();
}

void ConnectionSegment::frameTick()
{
    if (!m_ledThrottle.isActive()) {
        m_ledOn = true;
        m_ledThrottle.start();
        update();
    }
    // Otherwise drop — we already pulsed within the last 100 ms.
}

void ConnectionSegment::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit clicked();
    }
    QWidget::mousePressEvent(event);
}

void ConnectionSegment::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // ── State dot (left edge) ─────────────────────────────────────────────
    const QRect dotRect(8, height() / 2 - 5, 9, 9);
    QColor dotColor;
    switch (m_state) {
        case ConnectionState::Disconnected: dotColor = QColor("#445566"); break;
        case ConnectionState::Probing:
        case ConnectionState::Connecting:   dotColor = QColor("#d39c2a"); break;
        case ConnectionState::Connected:    dotColor = QColor("#39c167"); break;
        case ConnectionState::LinkLost:     dotColor = QColor("#c14848"); break;
    }
    p.setBrush(dotColor);
    p.setPen(Qt::NoPen);
    p.drawEllipse(dotRect);

    // ── Text (depends on state) ───────────────────────────────────────────
    int x = dotRect.right() + 8;
    p.setPen(QColor("#a0b0c0"));

    if (m_state == ConnectionState::Disconnected) {
        p.drawText(QRect(x, 0, width() - x, height()),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("Disconnected — click to connect"));
        return;
    }
    if (m_state == ConnectionState::Probing || m_state == ConnectionState::Connecting) {
        p.drawText(QRect(x, 0, width() - x, height()),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("%1 %2…").arg(connectionStateName(m_state), m_name));
        return;
    }

    // ── Connected: name (bold) · IP · ▲ rx ▼ tx · activity LED ──────────
    QFont nameFont = p.font();
    nameFont.setBold(true);
    p.setFont(nameFont);
    p.setPen(QColor("#e0eef8"));
    p.drawText(QRect(x, 0, width() - x, height()),
               Qt::AlignVCenter | Qt::AlignLeft, m_name);
    QFontMetrics fm(nameFont);
    x += fm.horizontalAdvance(m_name) + 10;

    nameFont.setBold(false);
    p.setFont(nameFont);
    p.setPen(QColor("#7088a0"));
    const QString ipStr = m_ip.toString();
    p.drawText(QRect(x, 0, width() - x, height()),
               Qt::AlignVCenter | Qt::AlignLeft, ipStr);
    x += fm.horizontalAdvance(ipStr) + 12;

    p.setPen(QColor("#90a0b0"));
    const QString rates = QStringLiteral("▲ %1 Mb/s   ▼ %2 kb/s")
        .arg(m_rxMbps, 0, 'f', 1).arg(int(m_txMbps * 1000));
    p.drawText(QRect(x, 0, width() - x, height()),
               Qt::AlignVCenter | Qt::AlignLeft, rates);
    x += fm.horizontalAdvance(rates) + 8;

    // ── Activity LED ──────────────────────────────────────────────────────
    if (m_ledOn) {
        p.setBrush(QColor("#5fff8a"));
        p.setPen(Qt::NoPen);
        p.drawEllipse(QRect(x, height() / 2 - 3, 6, 6));
    }
}

// =========================================================================
// TitleBar implementation
// =========================================================================

TitleBar::TitleBar(AudioEngine* audio, QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(kStripHeight);
    setStyleSheet(QLatin1String(kStripStyle));

    m_hbox = new QHBoxLayout(this);
    m_hbox->setContentsMargins(kMarginLeft, kMarginTop, kMarginRight, kMarginBottom);
    m_hbox->setSpacing(kSpacing);

    // Position 0 is reserved for the menu bar (inserted via setMenuBar()).
    // The ConnectionSegment sits at position 1 (or 0 before the menu is
    // inserted), between the menu bar and the centre label stretch.

    // ── ConnectionSegment — Phase 3Q-6 ─────────────────────────────────
    // Inserted as the first item. setMenuBar() will prepend the menu bar
    // at index 0 pushing this to index 1. Until setMenuBar() runs the
    // segment sits at index 0 — acceptable; it just moves right once the
    // menu arrives.
    m_connectionSegment = new ConnectionSegment(this);
    m_hbox->addWidget(m_connectionSegment);

    // ── Left stretch ───────────────────────────────────────────────────────
    m_hbox->addStretch(1);

    // ── App-name label ─────────────────────────────────────────────────────
    // From AetherSDR TitleBar.cpp:101-104 — text swapped to "NereusSDR".
    auto* appName = new QLabel(QStringLiteral("NereusSDR"), this);
    appName->setStyleSheet(QLatin1String(kAppNameStyle));
    appName->setAlignment(Qt::AlignCenter);
    m_hbox->addWidget(appName);

    // ── Right stretch ──────────────────────────────────────────────────────
    m_hbox->addStretch(1);

    // ── MasterOutputWidget — Task 10b composite ────────────────────────────
    m_master = new MasterOutputWidget(audio, this);
    m_hbox->addWidget(m_master);

    // ── 💡 Feature-request button — Task 10d ───────────────────────────────
    // Construction moved verbatim from the now-deleted featureBar QToolBar
    // in MainWindow.cpp (Phase 3G-14). The button lives at the far right
    // of the TitleBar strip, past the MasterOutputWidget — this matches
    // AetherSDR's pattern where the feature button is the rightmost strip
    // element.
    m_hbox->addSpacing(6);

    // Paint a lightbulb icon so it renders cleanly at any DPI.
    auto makeBulbIcon = [](QColor bulbColor, QColor baseColor) -> QIcon {
        constexpr int sz = 64;  // paint large, Qt scales down
        QPixmap pm(sz, sz);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);

        // Bulb (circle)
        p.setPen(Qt::NoPen);
        p.setBrush(bulbColor);
        p.drawEllipse(QRectF(14, 4, 36, 36));

        // Neck (trapezoid connecting bulb to base)
        QPolygonF neck;
        neck << QPointF(22, 36) << QPointF(42, 36)
             << QPointF(40, 44) << QPointF(24, 44);
        p.drawPolygon(neck);

        // Base (screw threads — 3 thin lines)
        p.setPen(QPen(baseColor, 2.5));
        p.drawLine(QPointF(24, 46), QPointF(40, 46));
        p.drawLine(QPointF(25, 50), QPointF(39, 50));
        p.drawLine(QPointF(27, 54), QPointF(37, 54));

        // Tip
        p.setPen(Qt::NoPen);
        p.setBrush(baseColor);
        p.drawEllipse(QRectF(29, 56, 6, 4));

        // Filament lines inside bulb
        p.setPen(QPen(baseColor, 1.5));
        p.drawLine(QPointF(28, 34), QPointF(28, 22));
        p.drawLine(QPointF(28, 22), QPointF(32, 16));
        p.drawLine(QPointF(32, 16), QPointF(36, 22));
        p.drawLine(QPointF(36, 22), QPointF(36, 34));

        p.end();
        return QIcon(pm);
    };

    QIcon bulbIcon = makeBulbIcon(QColor(0xFF, 0xD0, 0x60), QColor(0x80, 0x60, 0x20));

    m_featureBtn = new QPushButton(this);
    m_featureBtn->setObjectName(QStringLiteral("featureButton"));
    m_featureBtn->setIcon(bulbIcon);
    m_featureBtn->setIconSize(QSize(22, 22));
    m_featureBtn->setFixedSize(28, 28);
    m_featureBtn->setToolTip(QStringLiteral("Submit a feature request or bug report"));
    m_featureBtn->setAccessibleName(QStringLiteral("Feature request"));
    m_featureBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: #3a2a00; border: 1px solid #806020; "
        "border-radius: 4px; padding: 0; }"
        "QPushButton:hover { background: #504000; border-color: #a08030; }"));
    connect(m_featureBtn, &QPushButton::clicked,
            this, &TitleBar::featureRequestClicked);
    m_hbox->addWidget(m_featureBtn);
}

void TitleBar::setMenuBar(QMenuBar* mb)
{
    // Ported line-for-line from AetherSDR TitleBar.cpp:282-295.
    if (!mb) {
        return;
    }
    mb->setStyleSheet(QLatin1String(kMenuBarStyle));
    mb->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    m_menuBar = mb;
    // Insert at position 0 (before the first stretch).
    m_hbox->insertWidget(0, mb);
}

} // namespace NereusSDR
