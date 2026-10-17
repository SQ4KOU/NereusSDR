// src/gui/containers/EdgeDockStrip.h

// --- From src/gui/applets/AppletGrid.cpp ---
// =================================================================
// src/gui/applets/AppletGrid.cpp  (Longpath)
// =================================================================
// Siehe AppletGrid.h — Schritt 1 des freien Rasters.

// --- From src/gui/StyleConstants.h ---
// =================================================================
// src/gui/StyleConstants.h  (Longpath)
// =================================================================
//
// Source attribution (AetherSDR — GPLv3):
//
//   Copyright (C) 2024-2026  Jeremy (KK7GWY) / AetherSDR contributors
//       — per https://github.com/ten9876/AetherSDR (GPLv3; see LICENSE
//       and About dialog for the live contributor list)
//
//   This file is a port or structural derivative of AetherSDR source.
//   AetherSDR is licensed under the GNU General Public License v3.
//   Longpath is also GPLv3. Attribution follows GPLv3 §5 requirements.
//
// =================================================================
// Modification history (Longpath):
//   2026-04-16 — Ported/adapted in C++20/Qt6 for NereusSDR by
//                 J.J. Boyd (KG4VCF), with AI-assisted transformation
//                 via Anthropic Claude Code.
//                 Theme palette imported from AetherSDR `src/gui/ComboStyle.h` /
//                 `HGauge.h` / `SliceColors.h` and inline QColor calls
//                 in `MainWindow.cpp` / `VfoWidget.cpp`.
// =================================================================

// --- From src/gui/WindowPlacement.h ---
// =================================================================
// src/gui/WindowPlacement.h  (Longpath)
// =================================================================
//
// Longpath-original.
//
// ── Ein Fenster auf einen sichtbaren Schirm holen ────────────────────
//
// Der Rumpf dieser Funktion stand seit 2026-04-17 als
// FloatingContainer::ensureVisiblePosition() im Baum und löste den Fall
// „der Monitor ist beim Start nicht mehr da" bereits vollständig. Er
// hing dort nur an einer Klasse fest, die ein Meter-Fenster ist — er
// fasst nichts von ihr an ausser zwei Grössenkonstanten.
//
// Am 2026-08-16 wurde er hierher gehoben, damit die Applet-Fenster
// dieselbe Lösung BENUTZEN, statt eine zweite halbe zu bekommen.
// Verhalten unverändert; FloatingContainer ruft jetzt hierher.
//
// =================================================================
// Modification history (Longpath):
//   2026-04-17 — Ursprung als FloatingContainer::ensureVisiblePosition,
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude.
//   2026-08-16 — Als freie Funktion herausgezogen, Verhalten
//                 unverändert. Martin Fischer, AI-assisted via
//                 Anthropic Claude (Cowork).
//   2026-09-25 — coveredFraction(): eingeschaltete Applets nach vorne.
//                 Martin Fischer, AI-assisted via Anthropic Claude.
//   2026-09-27 — fitFrameIntoArea(): Werkzeugfenster nie groesser als
//                 der Schirm. Martin Fischer, AI-assisted via Anthropic
//                 Claude.
// =================================================================

// --- From src/gui/WindowPlacement.cpp ---
// =================================================================
// src/gui/WindowPlacement.cpp  (Longpath)
// =================================================================
// Siehe WindowPlacement.h für Herkunft und die zwei bekannten
// Schwächen, die hier bewusst unverändert bleiben.
// =================================================================

// =================================================================
// src/gui/containers/EdgeDockStrip.h  (NereusSDR)
// =================================================================
//
// Ported from Longpath source at revision
//   551576e30c42731c6d51ce005c5f23ec38be391c,
//   https://github.com/oe5sos/Longpath, primary author Martin Fischer
//   (OE5SOS). The upstream banners are copied above:
//   - src/gui/applets/AppletGrid.cpp: the 6 px gap between cells
//     (kCellGap), used here as the strip's padding and gap;
//   - src/gui/StyleConstants.h: the 6 px splitter handle
//     (kSplitterHandlePx);
//   - src/gui/WindowPlacement.h/.cpp: the 8 px alignment grid
//     (kSnapGridPx) and snappedTopLeft().
//
//   Upstream AppletGrid.cpp and WindowPlacement.h/.cpp have no
//   top-of-file copyright or GPL header; the project-level LICENSE
//   (GPLv3) applies. StyleConstants.h carries the AetherSDR
//   attribution above.
//
//   Longpath has no four-edge container docking. The strips, the edge
//   area, the drop preview and the 1 px centre line on the handle are
//   NereusSDR-original (workspace visual review, Task 4). Workspace
//   preview only.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-08 - The right strip has no padding and a 3 px handle on its
//                 spectrum side, so it and its handle take exactly today's
//                 Main Panel and splitter handle width (JJ's choice).
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-08 - Review fixes: clear an edge thickness and reset
//                 strip extents to the default split. J.J. Boyd (KG4VCF),
//                 AI-assisted via Anthropic Claude Code.
//   2026-10-07 - Written for the workspace visual review by J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#pragma once

#include "ContainerTypes.h"

#include <QList>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QRgb>
#include <QSplitter>
#include <QWidget>

class QScrollArea;

namespace NereusSDR {

// Longpath's alignment grid, in logical pixels.
constexpr int kWorkspaceSnapGridPx = 8;  // From Longpath src/gui/WindowPlacement.h:58 [@551576e3]

// Rounds a top-left corner to the nearest grid point. Grid 1 or less is
// a no-op. From Longpath src/gui/WindowPlacement.cpp:140-147 [@551576e3]
QPoint workspaceSnappedTopLeft(const QPoint& pos, int grid = kWorkspaceSnapGridPx);

// A QSplitter whose handles are 6 px (3 px beside the right strip) with a
// 1 px centre line in the border role, the accent role under the pointer.
class EdgeDockSplitter : public QSplitter {
    Q_OBJECT
public:
    explicit EdgeDockSplitter(Qt::Orientation orientation, QWidget* parent = nullptr);

protected:
    QSplitterHandle* createHandle() override;
};

// One edge of the central workspace. Hosts containers in order: left and
// right strips stack them vertically, top and bottom horizontally. The
// strip scrolls when its containers' minima exceed the room, and an
// empty strip is hidden so it takes no space.
class EdgeDockStrip : public QWidget {
    Q_OBJECT
public:
    // The gap between two cells of Longpath's applet grid; the strip uses
    // it as padding and as the gap between containers.
    static constexpr int kPaddingPx = 6;  // From Longpath src/gui/applets/AppletGrid.cpp:24 [@551576e3]
    static constexpr int kGapPx = 6;      // From Longpath src/gui/applets/AppletGrid.cpp:24 [@551576e3]
    static constexpr int kHandlePx = 6;   // From Longpath src/gui/StyleConstants.h:750 [@551576e3]
    // NereusSDR: the right strip, where the Main Panel lives by default,
    // has no padding, and its handle on the spectrum side is today's 3 px
    // splitter handle (MainWindow.cpp m_mainSplitter->setHandleWidth(3)),
    // not Longpath's 6 px. JJ chose today's width for the right strip
    // (2026-10-08): the spectrum and the Main Panel keep today's sizes.
    // The left, top and bottom strips keep Longpath's padding and handle.
    static constexpr int kRightPaddingPx = 0;
    static constexpr int kRightHandlePx = 3;
    // Workspace review defaults (Task 4 brief), not Longpath values.
    static constexpr int kSideThicknessPx = 280;
    static constexpr int kEndThicknessPx = 112;
    // Height (or width) of the destination preview between containers.
    static constexpr int kInsertPreviewPx = 28;

    explicit EdgeDockStrip(DockEdge edge, QWidget* parent = nullptr);

    DockEdge edge() const { return m_edge; }
    // The direction containers stack in.
    Qt::Orientation orientation() const;
    static Qt::Orientation orientationFor(DockEdge edge);
    static int defaultThickness(DockEdge edge);
    // The padding inside a strip on every side: kRightPaddingPx for the
    // right strip, kPaddingPx for the others.
    static int paddingFor(DockEdge edge);
    int padding() const { return paddingFor(m_edge); }
    // The width of the area handle on the spectrum side of a strip:
    // kRightHandlePx for the right strip, kHandlePx for the others.
    static int handleExtentFor(DockEdge edge);

    // Places exactly these widgets, in this order. Widgets already in the
    // strip move; new ones are reparented into it.
    void setContainers(const QList<QWidget*>& widgets);
    QList<QWidget*> containers() const;
    int count() const;
    // True when a container is not explicitly hidden.
    bool hasShownContainer() const;

    // Extents along the strip, one per container (hidden ones are 0).
    QList<int> extents() const;
    void setExtents(const QList<int>& extents);
    // Back to the default split: each container its size hint along the
    // strip, as the splitter lays out containers it has no sizes for.
    void resetExtents();

    QSplitter* splitter() const { return m_splitter; }
    QScrollArea* scrollArea() const { return m_scroll; }

    // The insertion index for a point in strip coordinates, and the
    // destination preview for an index, in strip coordinates.
    int insertionIndexAt(const QPoint& pos) const;
    QRect insertionRect(int index) const;

signals:
    void extentsChanged();

private:
    QList<QWidget*> shownContainers() const;
    QRect containerRect(QWidget* w) const;

    DockEdge m_edge;
    QScrollArea* m_scroll{nullptr};
    QWidget* m_body{nullptr};
    EdgeDockSplitter* m_splitter{nullptr};
};

class EdgeDropOverlay;

// The central workspace: a vertical splitter [top | middle | bottom]
// whose middle is the horizontal splitter [left | centre | right]. Top
// and bottom span the full width; left and right take the height between
// them. The centre (the spectrum pane) is the only stretching pane, so
// strips keep their thickness when the window is resized.
class EdgeDockArea : public QWidget {
    Q_OBJECT
public:
    // Today's first-run split of spectrum and Main Panel,
    // MainWindow.cpp setSizes({1024, 256}): the right strip's width (it
    // has no padding, so the Main Panel's) until one is saved or dragged.
    static constexpr int kMigratedPanelShare = 256;
    static constexpr int kMigratedTotalShare = 1280;
    // That splitter's handle, MainWindow.cpp m_mainSplitter->setHandleWidth(3).
    // The right strip's handle has the same width (kRightHandlePx).
    static constexpr int kMigratedHandlePx = EdgeDockStrip::kRightHandlePx;
    // The edge bands' width and the room in from an edge that targets it.
    static constexpr int kTargetBandPx = 6;
    static constexpr int kEdgeHotZonePx = 48;
    static constexpr double kTargetBandAlpha = 0.35;
    static constexpr double kPreviewFillAlpha = 0.22;
    static constexpr int kPreviewRadiusPx = 6;
    static constexpr QRgb kPreviewBorder = 0xff3d78b0;

    struct DropTarget {
        bool valid{false};
        bool detach{false};
        DockEdge edge{DockEdge::Right};
        int index{-1};
    };

    // `middle` is the horizontal splitter that already holds `center`.
    EdgeDockArea(QSplitter* middle, QWidget* center, QWidget* parent = nullptr);

    EdgeDockStrip* strip(DockEdge edge) const;
    QWidget* center() const { return m_center; }
    QSplitter* verticalSplitter() const { return m_vertical; }
    QSplitter* middleSplitter() const { return m_middle; }

    // Thickness across a strip. Remembered while the strip is hidden.
    int thickness(DockEdge edge) const;
    bool hasThickness(DockEdge edge) const;
    void setThickness(DockEdge edge, int thickness);
    // Forgets a saved or dragged thickness: the strip is back on the
    // default rule (and the right strip on the Main Panel migration).
    void clearThickness(DockEdge edge);
    // What the strip's containers need across it, without the strip's
    // padding: `minimumCross` is the widest container minimum (a side
    // strip is never narrower, so no container is clipped) and
    // `preferredCross` the tallest preferred height (an end strip's
    // default, so no scrollbar shows until the user makes it smaller).
    void setStripContentHint(DockEdge edge, int minimumCross, int preferredCross);
    // The ☰ panel toggle collapses the right strip without emptying it.
    void setUserCollapsed(DockEdge edge, bool collapsed);
    bool userCollapsed(DockEdge edge) const;
    // Shows the strips that hold a shown container, hides the rest, and
    // applies the thicknesses.
    void refreshLayout();

    DropTarget dropTargetAt(const QPoint& globalPos) const;
    void setTargetBandsVisible(bool visible);
    bool targetBandsVisible() const;
    void showDropPreview(DockEdge edge, int index);
    void clearDropPreview();
    bool dropPreviewVisible() const;
    // Area coordinates.
    QRect dropPreviewRect() const;
    QRect targetBandRect(DockEdge edge) const;

signals:
    void thicknessChanged();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    int indexOf(DockEdge edge) const;
    QRect stripRect(DockEdge edge) const;
    QRect previewRectFor(DockEdge edge, int index) const;
    void applySizes();
    void captureThickness();
    int migratedRightThickness() const;

    QSplitter* m_middle{nullptr};
    QWidget* m_center{nullptr};
    EdgeDockSplitter* m_vertical{nullptr};
    EdgeDockStrip* m_strips[4]{};
    int m_thickness[4]{};
    int m_minimumCross[4]{};
    int m_preferredCross[4]{};
    bool m_explicit[4]{};
    bool m_collapsed[4]{};
    bool m_shown[4]{};
    bool m_applying{false};
    EdgeDropOverlay* m_overlay{nullptr};
};

} // namespace NereusSDR
