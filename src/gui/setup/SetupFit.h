#pragma once
// no-port-check: NereusSDR-original layout helpers for the Setup pages.

// SPDX-License-Identifier: GPL-3.0-or-later
//
// =================================================================
// src/gui/setup/SetupFit.h  (NereusSDR)
// =================================================================
//
// NereusSDR-original. A Setup page rearranges to fit the Settings window
// (JJ's decision D25): groups that sit side by side stack one under the
// other, in reading order, when the page is narrower than the groups side
// by side, and go back side by side when it is wide enough again. Nothing
// is cut off and the page never scrolls sideways; it scrolls down instead.
//
// FitRow is the row of groups. It is a QBoxLayout that lays its items out
// exactly as a QHBoxLayout does while they fit, so a page that already
// fits looks as it did, and switches to top-to-bottom when they do not.
// Its minimum width is its widest item. Stacked, items keep their own
// height and do not stretch to fill the page. Side by side, the row is as
// tall as its tallest column needs: a column's trailing stretch keeps a
// shorter column's groups at the top and does not ask for the page's
// spare height, which the page's own trailing stretch puts at the bottom.
// The row grows only when one of its widgets asks to. FitGrid does the same for a
// QGridLayout: each widget keeps its cell while the columns fit and takes
// a row of its own, in reading order, when they do not.
//
// widthAtLeast() keeps a fixed-width label's column but lets a longer text
// widen it, so the text is not cut short.
//
// scrollingTab() wraps a tab's content in a scroll area of its own, so the
// tab strip stays at the top while each tab's content scrolls down;
// showWhole() makes a scroll area nested inside one show all of its content.
//
// FitScrollArea is the scroll area a page or tab scrolls down in. A row
// that fits side by side in the area's width and height with no vertical
// scroll bar is side by side, whatever was shown before: the bar, once
// showing, takes the width that makes the row stack, and the stacked rows
// then need the bar, so a plain QScrollArea keeps whichever of the two
// layouts it reached first. A row stacks only when it cannot fit side by
// side, bar or no bar.
//
// SidewaysFrame is the one exception D25 allows: a row that cannot fit the
// window at its narrowest scrolls sideways inside its own frame.
//
// Longpath's FlowLayout (src/gui/widgets/FlowLayout.{h,cpp} @551576e3)
// was studied and not ported: it places each item at its own size hint and
// gives every item in a line the line's height, so the pages that already
// fit would change width and the partial lines (two groups of three) would
// leave ragged gaps.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-09  J.J. Boyd / KG4VCF  Created. AI-assisted via Anthropic
//                                    Claude Code.
//   2026-10-10  J.J. Boyd / KG4VCF  FitScrollArea: a row that fits side
//                                    by side without a vertical scroll bar
//                                    is side by side whatever was shown
//                                    before. AI-assisted via Anthropic
//                                    Claude Code.
//   2026-10-10  J.J. Boyd / KG4VCF  FitRow side by side takes the page's
//                                    spare height only when one of its
//                                    widgets asks for it. AI-assisted via
//                                    Anthropic Claude Code.
// =================================================================

#include <QBoxLayout>
#include <QGridLayout>
#include <QList>
#include <QScrollArea>

class QEvent;
class QLabel;
class QResizeEvent;
class QShowEvent;
class QWidget;

namespace NereusSDR::SetupFit {

// Set (true) on a frame that may scroll sideways inside itself because the
// table it holds cannot fit the window (the page itself never does).
inline constexpr const char* kScrollsSidewaysProperty = "nereusSetupScrollsSideways";

// Set on every FitRow: true while it stacks its items, false side by side.
inline constexpr const char* kFitRowStackedProperty = "nereusSetupFitRowStacked";

class FitRow : public QBoxLayout
{
public:
    explicit FitRow(QWidget* parent = nullptr);

    // True while the items are stacked one under the other.
    bool isStacked() const { return direction() == QBoxLayout::TopToBottom; }

    // The width the items need side by side, margins included.
    int sideBySideMinimumWidth() const;

    QSize minimumSize() const override;
    Qt::Orientations expandingDirections() const override;
    void setGeometry(const QRect& rect) override;

private:
    void arrange(bool stacked);
    int horizontalSpacing() const;

    QList<int> m_sideBySideStretch;  // each item's stretch while side by side
};

class FitGrid : public QGridLayout
{
public:
    explicit FitGrid(QWidget* parent = nullptr);

    // Puts widget at (row, column) spanning rowSpan x columnSpan while the
    // columns fit, and in column 0 at stackedRow when they do not.
    void addFitWidget(QWidget* widget, int row, int column, int rowSpan, int columnSpan,
                      int stackedRow, Qt::Alignment alignment = {});

    bool isStacked() const { return m_stacked; }

    // The width the columns need side by side, margins included.
    int sideBySideMinimumWidth() const;

    QSize minimumSize() const override;
    void setGeometry(const QRect& rect) override;

private:
    struct Placement
    {
        QWidget* widget = nullptr;
        int row = 0;
        int column = 0;
        int rowSpan = 1;
        int columnSpan = 1;
        int stackedRow = 0;
        Qt::Alignment alignment;
    };

    void arrange(bool stacked);
    QLayoutItem* itemFor(QWidget* widget) const;

    QList<Placement> m_placements;
    bool m_stacked = false;
};

// Fixes label at px wide, or wider when its text needs more.
void widthAtLeast(QLabel* label, int px);

// The frame for a row that cannot fit the window at its narrowest (a strip
// of cells that must stay in one line): it scrolls sideways inside itself,
// marked with kScrollsSidewaysProperty, and is always as tall as its
// content plus, while it scrolls, the scroll bar, so it never scrolls down
// and never cuts the content off.
class SidewaysFrame : public QScrollArea
{
public:
    explicit SidewaysFrame(QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    int contentHeight() const;
    bool needsBar() const;

    bool m_hadBar = false;
};

// The scroll area of a page or tab whose rows rearrange to fit (FitRow,
// FitGrid). While a row is stacked and the vertical scroll bar shows, it
// lays the content out once in the width without the bar: if everything
// then fits the height, the bar goes and the rows stay side by side; if
// not, the bar and the stacked rows come back as they were, scrolled to
// where they were. It asks again only when the area's size or the
// content's needs change. Pages with no stacked row are left to
// QScrollArea alone.
class FitScrollArea : public QScrollArea
{
public:
    explicit FitScrollArea(QWidget* parent = nullptr);

protected:
    bool event(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void preferNoBar();
    void layOutNow();

    bool m_trying = false;
    QSize m_triedFor;        // the area's size without bars the list is for
    QList<QSize> m_tried;    // the content's needs that did not fit there
};

// Puts content in a scroll area that fills its tab and scrolls down, with
// no frame and no fill of its own, so a tab that fits looks as it did.
QScrollArea* scrollingTab(QWidget* content);

// A scroll area nested inside a tab that scrolls (a matrix in its group)
// takes the whole of its fixed-size content as its own size, so it never
// scrolls or cuts the content off; the tab around it scrolls instead.
void showWhole(QScrollArea* area);

} // namespace NereusSDR::SetupFit
