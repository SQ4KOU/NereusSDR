// no-port-check: NereusSDR-original layout helpers for the Setup pages.

// SPDX-License-Identifier: GPL-3.0-or-later
//
// =================================================================
// src/gui/setup/SetupFit.cpp  (NereusSDR)
// =================================================================
//
// NereusSDR-original. See SetupFit.h.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-09  J.J. Boyd / KG4VCF  Created. AI-assisted via Anthropic
//                                    Claude Code.
//   2026-10-10  J.J. Boyd / KG4VCF  FitScrollArea, used by scrollingTab.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-10-10  J.J. Boyd / KG4VCF  FitRow: a column's trailing spacer no
//                                    longer makes the row take the page's
//                                    spare height. AI-assisted via Anthropic
//                                    Claude Code.
//   2026-10-11  J.J. Boyd / KG4VCF  showWhole: the area is at least as
//                                    large as its content, past the 24
//                                    lines QScrollArea::sizeHint() stops
//                                    at. AI-assisted via Anthropic Claude
//                                    Code.
// =================================================================

#include "SetupFit.h"

#include <algorithm>

#include <QEvent>
#include <QFrame>
#include <QLabel>
#include <QResizeEvent>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QWidget>

namespace NereusSDR::SetupFit {

namespace {

// The spacing Qt uses when a layout leaves it to the style and the style
// leaves it to the controls (QStyle::PM_LayoutHorizontalSpacing of -1).
constexpr int kFallbackSpacingPx = 6;

// A hidden widget takes no room and no spacing; a spacer takes its own
// size and no spacing, as QBoxLayout counts them.
bool takesNoRoom(QLayoutItem* item)
{
    return item->isEmpty() && item->widget() != nullptr;
}

bool isSpacer(QLayoutItem* item)
{
    return item->spacerItem() != nullptr;
}

// True when a widget under item, through the layouts nested in it, asks
// for more height than it needs. A spacer does not: the stretch that ends
// a column of groups keeps the groups at the top of the row and is no
// request for the page's spare height.
bool wantsHeight(QLayoutItem* item)
{
    if (item == nullptr || isSpacer(item)) {
        return false;
    }
    if (QLayout* nested = item->layout()) {
        for (int i = 0; i < nested->count(); ++i) {
            if (wantsHeight(nested->itemAt(i))) {
                return true;
            }
        }
        return false;
    }
    return !item->isEmpty() && item->expandingDirections().testFlag(Qt::Vertical);
}

// A layout pass is bounded: each one settles one more level of nesting.
constexpr int kMaxLayoutPasses = 8;

// True when a showing row or grid under content is stacked.
bool hasStackedRow(const QWidget* content)
{
    const QList<QLayout*> layouts = content->findChildren<QLayout*>();
    for (const QLayout* layout : layouts) {
        if (layout->property(kFitRowStackedProperty).toBool() && layout->parentWidget() != nullptr
            && layout->parentWidget()->isVisible()) {
            return true;
        }
    }
    return false;
}

// Brings the layouts under w up to date now, children first, so each
// parent sees its children's new needs. Qt does the same over several
// posted layout requests; a decision made in between would read old sizes.
// True when a layout had work to do.
bool activateLayouts(QWidget* w)
{
    bool worked = false;
    const QList<QWidget*> children = w->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget* child : children) {
        if (!child->isHidden() && !child->isWindow()) {
            worked = activateLayouts(child) || worked;
        }
    }
    if (QLayout* layout = w->layout()) {
        worked = layout->activate() || worked;
    }
    return worked;
}

} // namespace

FitRow::FitRow(QWidget* parent)
    : QBoxLayout(QBoxLayout::LeftToRight, parent)
{
    setProperty(kFitRowStackedProperty, false);
}

int FitRow::horizontalSpacing() const
{
    // Stacked, spacing() answers for the vertical direction; an explicit
    // setSpacing() is the same both ways.
    const int own = spacing();
    return own >= 0 ? own : kFallbackSpacingPx;
}

int FitRow::sideBySideMinimumWidth() const
{
    int total = 0;
    int shown = 0;
    for (int i = 0; i < count(); ++i) {
        QLayoutItem* item = itemAt(i);
        if (item == nullptr || takesNoRoom(item)) {
            continue;
        }
        total += item->minimumSize().width();
        if (!isSpacer(item)) {
            ++shown;
        }
    }
    const QMargins m = contentsMargins();
    return total + horizontalSpacing() * std::max(0, shown - 1) + m.left() + m.right();
}

QSize FitRow::minimumSize() const
{
    int widest = 0;
    for (int i = 0; i < count(); ++i) {
        QLayoutItem* item = itemAt(i);
        if (item == nullptr || takesNoRoom(item) || isSpacer(item)) {
            continue;
        }
        widest = std::max(widest, item->minimumSize().width());
    }
    const QMargins m = contentsMargins();
    return QSize(widest + m.left() + m.right(), QBoxLayout::minimumSize().height());
}

Qt::Orientations FitRow::expandingDirections() const
{
    // Stacked, the groups keep their own height: what is left of the page
    // goes to its bottom, not into the gaps between groups.
    const Qt::Orientations own = QBoxLayout::expandingDirections();
    if (isStacked()) {
        return own & ~Qt::Vertical;
    }
    // Side by side, the row is as tall as its tallest column needs unless
    // one of its widgets asks for more: the spare height of a shorter
    // column is under that column's last group, and the page's own spare
    // height goes to the bottom of the page, not between this row and the
    // group under it (JJ, 2026-10-10).
    for (int i = 0; i < count(); ++i) {
        if (wantsHeight(itemAt(i))) {
            return own;
        }
    }
    return own & ~Qt::Vertical;
}

void FitRow::setGeometry(const QRect& rect)
{
    const bool stacked = rect.width() < sideBySideMinimumWidth();
    if (stacked != isStacked()) {
        arrange(stacked);
    }
    QBoxLayout::setGeometry(rect);
}

void FitRow::arrange(bool stacked)
{
    if (stacked) {
        m_sideBySideStretch.clear();
        for (int i = 0; i < count(); ++i) {
            m_sideBySideStretch.append(stretch(i));
        }
        setDirection(QBoxLayout::TopToBottom);
        for (int i = 0; i < count(); ++i) {
            setStretch(i, 0);
        }
    } else {
        setDirection(QBoxLayout::LeftToRight);
        for (int i = 0; i < count() && i < m_sideBySideStretch.size(); ++i) {
            setStretch(i, m_sideBySideStretch.at(i));
        }
    }
    setProperty(kFitRowStackedProperty, stacked);
}

FitGrid::FitGrid(QWidget* parent)
    : QGridLayout(parent)
{
    setProperty(kFitRowStackedProperty, false);
}

void FitGrid::addFitWidget(QWidget* widget, int row, int column, int rowSpan, int columnSpan,
                           int stackedRow, Qt::Alignment alignment)
{
    Placement p;
    p.widget = widget;
    p.row = row;
    p.column = column;
    p.rowSpan = rowSpan;
    p.columnSpan = columnSpan;
    p.stackedRow = stackedRow;
    p.alignment = alignment;
    m_placements.append(p);
    if (m_stacked) {
        addWidget(widget, stackedRow, 0, 1, 1, alignment);
    } else {
        addWidget(widget, row, column, rowSpan, columnSpan, alignment);
    }
}

QLayoutItem* FitGrid::itemFor(QWidget* widget) const
{
    const int index = indexOf(widget);
    return index >= 0 ? itemAt(index) : nullptr;
}

int FitGrid::sideBySideMinimumWidth() const
{
    int columns = 0;
    for (const Placement& p : m_placements) {
        columns = std::max(columns, p.column + p.columnSpan);
    }
    int total = 0;
    int shown = 0;
    for (int c = 0; c < columns; ++c) {
        int widest = columnMinimumWidth(c);
        bool any = false;
        for (const Placement& p : m_placements) {
            QLayoutItem* item = itemFor(p.widget);
            if (p.column != c || p.columnSpan != 1 || item == nullptr || takesNoRoom(item)) {
                continue;
            }
            widest = std::max(widest, item->minimumSize().width());
            any = true;
        }
        total += widest;
        if (any) {
            ++shown;
        }
    }
    const int own = horizontalSpacing();
    const int gap = own >= 0 ? own : kFallbackSpacingPx;
    const QMargins m = contentsMargins();
    return total + gap * std::max(0, shown - 1) + m.left() + m.right();
}

QSize FitGrid::minimumSize() const
{
    int widest = 0;
    for (const Placement& p : m_placements) {
        QLayoutItem* item = itemFor(p.widget);
        if (item != nullptr && !takesNoRoom(item)) {
            widest = std::max(widest, item->minimumSize().width());
        }
    }
    const QMargins m = contentsMargins();
    return QSize(widest + m.left() + m.right(), QGridLayout::minimumSize().height());
}

void FitGrid::setGeometry(const QRect& rect)
{
    const bool stacked = rect.width() < sideBySideMinimumWidth();
    if (stacked != m_stacked) {
        arrange(stacked);
    }
    QGridLayout::setGeometry(rect);
}

void FitGrid::arrange(bool stacked)
{
    m_stacked = stacked;
    for (const Placement& p : m_placements) {
        const int index = indexOf(p.widget);
        if (index < 0) {
            continue;
        }
        QLayoutItem* item = takeAt(index);
        if (stacked) {
            addItem(item, p.stackedRow, 0, 1, 1, p.alignment);
        } else {
            addItem(item, p.row, p.column, p.rowSpan, p.columnSpan, p.alignment);
        }
    }
    setProperty(kFitRowStackedProperty, stacked);
}

void widthAtLeast(QLabel* label, int px)
{
    // The label's own style sheet sets its font; measure with it.
    label->ensurePolished();
    label->setFixedWidth(std::max(px, label->sizeHint().width()));
}

FitScrollArea::FitScrollArea(QWidget* parent)
    : QScrollArea(parent)
{
}

bool FitScrollArea::event(QEvent* event)
{
    const bool handled = QScrollArea::event(event);
    if (event->type() == QEvent::LayoutRequest) {
        preferNoBar();
    }
    return handled;
}

void FitScrollArea::resizeEvent(QResizeEvent* event)
{
    QScrollArea::resizeEvent(event);
    preferNoBar();
}

void FitScrollArea::showEvent(QShowEvent* event)
{
    QScrollArea::showEvent(event);
    preferNoBar();
}

void FitScrollArea::layOutNow()
{
    for (int pass = 0; pass < kMaxLayoutPasses; ++pass) {
        const bool worked = activateLayouts(widget());
        // QScrollArea sizes the content and the bars' ranges on a layout
        // request; this is that step, taken now.
        QEvent request(QEvent::LayoutRequest);
        QScrollArea::event(&request);
        if (!worked) {
            break;
        }
    }
}

void FitScrollArea::preferNoBar()
{
    QWidget* content = widget();
    QScrollBar* bar = verticalScrollBar();
    if (m_trying || content == nullptr || !isVisible() || !widgetResizable()
        || verticalScrollBarPolicy() != Qt::ScrollBarAsNeeded) {
        return;
    }
    if (!bar->isVisible()) {
        // Nothing to decide: the rows have the whole width already.
        m_tried.clear();
        return;
    }
    if (!hasStackedRow(content)) {
        return;
    }
    const QSize whole = maximumViewportSize();
    if (whole != m_triedFor) {
        m_triedFor = whole;
        m_tried.clear();
    }
    const QSize needs = content->minimumSizeHint();
    if (m_tried.contains(needs)) {
        return;
    }
    m_tried.append(needs);

    const QScopedValueRollback<bool> trying(m_trying, true);
    const int scrolledTo = bar->value();
    // The whole width, as it is with no bar: the rows that fit it go side
    // by side, and the bar's range then says whether the page fits.
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layOutNow();
    const bool fits = bar->maximum() <= bar->minimum();
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    layOutNow();
    if (fits) {
        m_tried.clear();
        return;
    }
    bar->setValue(scrolledTo);
    const QSize stackedNeeds = content->minimumSizeHint();
    if (!m_tried.contains(stackedNeeds)) {
        m_tried.append(stackedNeeds);
    }
}

QScrollArea* scrollingTab(QWidget* content)
{
    auto* area = new FitScrollArea;
    area->setObjectName(QStringLiteral("setupTabScroll"));
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    area->setWidget(content);
    // QScrollArea::setWidget() turns the content's own fill on; the tab
    // keeps the look it had, drawn by the tab pane behind it.
    content->setAutoFillBackground(false);
    area->setAutoFillBackground(false);
    area->viewport()->setAutoFillBackground(false);
    return area;
}

namespace {

// Keeps a scroll area at least as large as its content asks for.
// QScrollArea::sizeHint() stops at 36 by 24 font heights, so the size
// policy alone leaves a taller matrix with a scroll bar of its own, and
// that bar's width then makes it scroll sideways too (seen in the
// workspace look with the Linux fonts, where 13 rows of check boxes are
// taller than 24 lines of text). The content's layout request is the
// moment its needs change. Owned by the area (Qt parent).
class WholeContent : public QObject
{
public:
    explicit WholeContent(QScrollArea* area)
        : QObject(area)
        , m_area(area)
    {
        if (QWidget* content = area->widget()) {
            content->installEventFilter(this);
        }
        area->installEventFilter(this);
        fit();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        const QEvent::Type type = event->type();
        if (watched == m_area->widget()) {
            if (type == QEvent::LayoutRequest || type == QEvent::Polish) {
                fit();
            }
        } else if (watched == m_area && (type == QEvent::Show || type == QEvent::StyleChange)) {
            fit();
        }
        return QObject::eventFilter(watched, event);
    }

private:
    void fit()
    {
        const QWidget* content = m_area->widget();
        if (content == nullptr) {
            return;
        }
        const int frame = 2 * m_area->frameWidth();
        const QSize needed = content->sizeHint().expandedTo(content->minimumSizeHint())
                             + QSize(frame, frame);
        if (needed.isValid() && m_area->minimumSize() != needed) {
            m_area->setMinimumSize(needed);
        }
    }

    QScrollArea* m_area;
};

} // namespace

void showWhole(QScrollArea* area)
{
    area->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    area->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
    new WholeContent(area); // owned by area (Qt parent)
}

SidewaysFrame::SidewaysFrame(QWidget* parent)
    : QScrollArea(parent)
{
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setProperty(kScrollsSidewaysProperty, true);
    setAutoFillBackground(false);
    viewport()->setAutoFillBackground(false);
}

bool SidewaysFrame::needsBar() const
{
    return widget() != nullptr && widget()->minimumSizeHint().width() > viewport()->width();
}

int SidewaysFrame::contentHeight() const
{
    QWidget* content = widget();
    if (content == nullptr) {
        return 0;
    }
    const int width = std::max(viewport()->width(), content->minimumSizeHint().width());
    int height = content->hasHeightForWidth() ? content->heightForWidth(width)
                                              : content->sizeHint().height();
    height = std::max(height, content->minimumSizeHint().height());
    if (needsBar()) {
        height += horizontalScrollBar()->sizeHint().height();
    }
    return height;
}

QSize SidewaysFrame::sizeHint() const
{
    const int width = widget() != nullptr ? widget()->sizeHint().width() : 0;
    return QSize(width, contentHeight());
}

QSize SidewaysFrame::minimumSizeHint() const
{
    return QSize(QScrollArea::minimumSizeHint().width(), contentHeight());
}

void SidewaysFrame::resizeEvent(QResizeEvent* event)
{
    QScrollArea::resizeEvent(event);
    // The height follows the width: the content wraps differently and the
    // scroll bar comes and goes.
    if (event->oldSize().width() != event->size().width() || needsBar() != m_hadBar) {
        m_hadBar = needsBar();
        updateGeometry();
    }
}

} // namespace NereusSDR::SetupFit
