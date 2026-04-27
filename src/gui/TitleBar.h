#pragma once

// =================================================================
// src/gui/TitleBar.h  (NereusSDR)
// =================================================================
//
// Ported from AetherSDR source:
//   src/gui/TitleBar.h
//   src/gui/TitleBar.cpp
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
//                 heartbeat / multiFLEX / PC-audio / headphone / minimal-
//                 mode / feature-request widgets are intentionally omitted
//                 (deferred to separate NereusSDR phases — 3G-14 plans the
//                 💡 feature-request widget; headphone devices land in
//                 Sub-Phase 12 Setup → Audio → Devices; connection-state
//                 UI is already served elsewhere).
//                 Hosts `MasterOutputWidget` (Task 10b) on the right edge;
//                 `setMenuBar()` pattern copied from AetherSDR
//                 `src/gui/TitleBar.cpp:282-295` (re-style QMenuBar,
//                 `m_hbox->insertWidget(0, mb)`). Strip geometry and
//                 background (#0a0a14, 32 px, `border-bottom: 1px solid
//                 #203040`) follow AetherSDR `TitleBar.cpp:30-31`. App-
//                 name label colour and font follow AetherSDR
//                 `TitleBar.cpp:101-103` with the literal "AetherSDR"
//                 replaced by "NereusSDR".
//                 Design spec: docs/architecture/2026-04-19-vax-design.md
//                 §6.3 + §7.3.
//   2026-04-20 — Task 10d: consolidated the 💡 feature-request button
//                 (previously hosted by a separate `featureBar` QToolBar
//                 in MainWindow) into TitleBar's far-right slot, past the
//                 MasterOutputWidget. Emits `featureRequestClicked()`;
//                 MainWindow wires that to its existing
//                 showFeatureRequestDialog slot. Matches AetherSDR's
//                 pattern of the feature button as the rightmost element.
//   2026-04-27 — Phase 3Q-6: added ConnectionSegment — always-visible
//                 connection state widget (dot + radio name/IP + Mbps +
//                 activity LED). Inserted between menu bar and the
//                 centre stretch. Design §4.1.
// =================================================================

#include "core/ConnectionState.h"

#include <QHostAddress>
#include <QTimer>
#include <QWidget>

class QHBoxLayout;
class QMenuBar;
class QPushButton;

namespace NereusSDR {

class AudioEngine;
class MasterOutputWidget;

// ConnectionSegment — always-visible connection-state indicator living
// in the TitleBar, between the menu bar and the centre "NereusSDR" label.
//
// Visual layout (left → right within the segment):
//   [9 px state dot]  [radio name (bold, white) · IP · ▲▼ Mbps · LED]
//   — or when not connected:
//   [9 px state dot]  ["Disconnected — click to connect"]
//   — or when probing/connecting:
//   [9 px state dot]  ["Probing 192.168.x.x…" / "Connecting to ANAN-G2…"]
//
// Click anywhere on the segment emits clicked() → MainWindow wires that
// to showConnectionPanel().
//
// LED: pulses green on each frameReceived() tick, throttled to 10 Hz
// max visible refresh so high-rate EP6/DDC streams pulse cleanly
// instead of seizing.
class ConnectionSegment : public QWidget {
    Q_OBJECT

public:
    explicit ConnectionSegment(QWidget* parent = nullptr);

    void setState(ConnectionState s);
    void setRadio(const QString& name, const QHostAddress& ip);
    void setRates(double rxMbps, double txMbps);

public slots:
    // Pulse the activity LED. Throttled to 10 Hz — calls that arrive
    // within the 100 ms window after a pulse are silently dropped.
    void frameTick();

signals:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    ConnectionState m_state{ConnectionState::Disconnected};
    QString         m_name;
    QHostAddress    m_ip;
    double          m_rxMbps{0.0};
    double          m_txMbps{0.0};
    bool            m_ledOn{false};
    QTimer          m_ledThrottle;
};

// TitleBar — thin 32 px host strip at the top of the main window.
//
// Layout (left → right):
//   [menuBar, inserted at position 0 via setMenuBar()]
//   [ConnectionSegment — state dot + radio info + activity LED]
//   [stretch]
//   [NereusSDR app-name label]
//   [stretch]
//   [MasterOutputWidget — speaker button + master slider + readout]
//   [6 px spacing]
//   [💡 feature-request button]
//
// macOS caveat: embedding the QMenuBar inside a custom widget prevents Qt
// from promoting it to the native global menu bar at the top of the
// screen. The menus render in-window instead. This is the explicit design
// choice per user-approved Sub-Phase 10 decision (option D); the
// MasterOutputWidget master-volume controls MUST be visible at all times
// alongside the menus, and the native global bar cannot host them.
class TitleBar : public QWidget {
    Q_OBJECT

public:
    explicit TitleBar(AudioEngine* audio, QWidget* parent = nullptr);

    // Re-style `mb` and insert it at position 0 of the hbox layout.
    // Pattern from AetherSDR TitleBar.cpp:282. Caller must ensure `mb`
    // is fully populated with actions BEFORE invoking this — otherwise
    // the menu bar's size policy negotiation happens on an empty widget.
    void setMenuBar(QMenuBar* mb);

    // Non-owning accessor so MainWindow can wire the device-picker
    // signal into AudioEngine::setSpeakersConfig.
    MasterOutputWidget* masterOutput() const { return m_master; }

    // Non-owning accessor so MainWindow can wire connection-state signals
    // and the activity LED. Phase 3Q-6.
    ConnectionSegment* connectionSegment() const { return m_connectionSegment; }

signals:
    // Emitted when the 💡 feature-request button is clicked. MainWindow
    // connects this to its showFeatureRequestDialog slot.
    void featureRequestClicked();

private:
    QHBoxLayout*        m_hbox{nullptr};
    QMenuBar*           m_menuBar{nullptr};
    ConnectionSegment*  m_connectionSegment{nullptr};
    MasterOutputWidget* m_master{nullptr};
    QPushButton*        m_featureBtn{nullptr};
};

} // namespace NereusSDR
