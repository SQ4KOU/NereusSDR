#pragma once
// =================================================================
// src/gui/widgets/ProfileRailDock.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original collapsing wrapper around the ported
// ProfileRail for the workspace preview's top status strip. It ports no
// code; the rail inside it is the Longpath port, unchanged.
//
// At rest only the active profile's badge shows, as one checked pill,
// at a fixed spot just left of the centre title (kTitleGap). Hovering
// it (after kShowDelayMs), clicking it or giving it Tab focus fans
// out the full rail (every badge plus the dashed +) as an overlay
// popover that opens leftward: its right edge lines up with the
// collapsed badge's right edge, so it never covers the title, and it
// never reflows the bar. The popover closes kHideDelayMs after the
// pointer leaves, on Escape, or right after a profile is picked.
// "The pointer leaves" is read from the cursor position against the
// badge and the popover, not from Enter/Leave: on macOS each badge is its
// own native view, and moving between them can deliver a Leave with no
// matching Enter.
// Right-clicking any badge, collapsed or fanned out, gives the rail's
// context menu.
//
// The badge is not in the title bar's layout: it is placed against the
// title label, so a change in the network readout's text width never
// moves it and the bar's layout is the same as without it.
//
// Workspace preview only.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-08 - Written for the workspace visual review by J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-08 - Keep-open decided from the cursor position while open
//                 (kPointerSampleMs), with the nereus.gui.profilerail
//                 debug category, by J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
//   2026-10-11 - kShowDelayMs and kHideDelayMs live here: the container
//                 headers they were shared with no longer open on hover.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#include <QPointer>
#include <QTimer>
#include <QWidget>

class QFrame;
class QPushButton;

namespace NereusSDR {

class ProfileRail;
class WorkspaceProfiles;

class ProfileRailDock : public QWidget {
    Q_OBJECT

public:
    // Gap between the collapsed badge and the title text, logical px.
    static constexpr int kTitleGap = 16;
    static constexpr int kPopoverRadius = 8;
    static constexpr int kPopoverPadH = 6;
    static constexpr int kPopoverPadV = 4;
    // While open, how often the cursor position is sampled against the
    // badge and the popover, ms.
    static constexpr int kPointerSampleMs = 50;
    // Hover on the badge opens the rail after kShowDelayMs; the pointer
    // leaving closes it after kHideDelayMs.
    static constexpr int kShowDelayMs = 120;
    static constexpr int kHideDelayMs = 600;

    // `anchor` is the centre title label (a child of `parent`, the title
    // bar). `overlayParent` is the main window: the popover is its child
    // so it can overlap the bar and the content below without a window
    // of its own.
    ProfileRailDock(WorkspaceProfiles* profiles, QWidget* anchor, QWidget* overlayParent,
                    QWidget* parent);
    ~ProfileRailDock() override;

    bool isOpen() const;
    // Fans the rail out. With `focusRail` the active badge in the rail
    // takes keyboard focus (Tab arrived here).
    void open(bool focusRail = false);
    void close();
    void reposition();
    // True when the global point is on the collapsed badge or, while
    // open, on the popover.
    bool pointerInside(const QPoint& global) const;

    QPushButton* collapsedBadge() const { return m_badge; }
    ProfileRail* rail() const { return m_rail; }
    QFrame* popover() const { return m_popover; }

signals:
    void openChanged(bool open);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void refreshBadge();
    void hookRailBadges();
    void placePopover();
    void hideSoon();
    void closeBecause(const char* reason);
    void samplePointer();
    void startHideTimer(const char* reason);
    void stopHideTimer(const char* reason);

    QPointer<WorkspaceProfiles> m_profiles;
    QPointer<QWidget> m_anchor;
    QPointer<QWidget> m_overlayParent;
    QPushButton* m_badge = nullptr;
    QPointer<QFrame> m_popover;
    ProfileRail* m_rail = nullptr;
    QTimer m_showTimer;
    QTimer m_hideTimer;
    QTimer m_pointerTimer;
    bool m_pointerWasInside = false;
    const QEvent* m_lastLoggedEvent = nullptr;
    bool m_appFilter = false;
};

} // namespace NereusSDR
