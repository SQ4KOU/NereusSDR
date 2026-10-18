// tests/tst_setup_pages_fit_window.cpp  (NereusSDR)
//
// Every Setup page fits the Settings window (JJ's decision D25): at the
// window's default size (900 x 650) and at its minimum (820 x 600), in
// today's look and in the workspace look, no page or tab scrolls sideways,
// no tab strip scrolls away with its page, and no label, check box, radio
// button, push button or group title is cut short by its own width or
// clipped by a parent, and no group is squeezed below the width its own
// rows need. Widen the window (1440 x 900) and the groups that
// stacked one under the other go back side by side.
//
// Every leaf of the page tree is opened by registry index, and every tab
// path of it is walked (Hardware Config once posed as a Saturn and once as
// a Hermes Lite 2, as the scenes pose it; nothing connects). A table that
// cannot fit keeps a sideways scroll inside its own frame; such a frame
// carries the property SetupFit::kScrollsSidewaysProperty and is listed,
// with its widths, rather than failed.
//
// no-port-check: NereusSDR-original test; no upstream logic is ported.
//
// Modification history (NereusSDR):
//   2026-10-09 - Table, tree and list cells are checked too: a cell whose
//                 text is elided or cut by its own column, or clipped by
//                 the view or a parent where no scroll reaches it, is a
//                 fault (planted-cell case pageFaultsCatchAClippedCell).
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-10 - A row that fits side by side without a vertical scroll
//                 bar sits side by side whatever was shown before: a
//                 planted row from each history, and the Calibration tab
//                 by each route to it.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-10 - The Calibration tab side by side: Volts/Amps sits under
//                 the columns at the page's spacing, each group is as tall
//                 as its content needs, and Level Cal has no empty row; a
//                 planted row of columns leaves the page's spare height
//                 to the page.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-11 - A sideways scroll names the widget that is too wide and
//                 the widths around it. The OC matrix row may sit side by
//                 side at 820 x 600 where the fonts leave it room (the
//                 workspace look on Linux), so that row is required to
//                 show, stacked or not.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.

#include <algorithm>

#include <QtTest/QtTest>
#include <QApplication>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QFrame>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLayout>
#include <QListView>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOptionButton>
#include <QStyleOptionGroupBox>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextDocument>
#include <QTreeView>
#include <QVBoxLayout>

#include "GridHeaderAlignment.h"
#include "core/AppSettings.h"
#include "core/HpsdrModel.h"
#include "core/RadioDiscovery.h"
#include "gui/SetupDialog.h"
#include "gui/setup/HardwarePage.h"
#include "gui/setup/SetupFit.h"
#include "models/RadioModel.h"

using namespace NereusSDR;
using GridHeaderAlignment::AppLook;

namespace {

constexpr int kDefaultWidth = 900;
constexpr int kDefaultHeight = 650;
constexpr int kMinimumWidth = 820;
constexpr int kMinimumHeight = 600;
constexpr int kWideWidth = 1440;
constexpr int kWideHeight = 900;

// The property a table frame carries when it may scroll sideways inside
// itself (src/gui/setup/SetupFit.h, kScrollsSidewaysProperty), and the one
// a group row carries (kFitRowProperty) with its state.
constexpr const char* kScrollsSideways = "nereusSetupScrollsSideways";
constexpr const char* kFitRowStacked = "nereusSetupFitRowStacked";

void settle()
{
    for (int i = 0; i < 4; ++i) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        QCoreApplication::processEvents();
    }
}

// The text Qt draws for a mnemonic-parsed text: "&&" draws one "&", a lone
// "&" draws nothing.
QString shownText(const QString& text)
{
    QString out;
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('&')) {
            if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('&')) {
                out += QLatin1Char('&');
                ++i;
            }
            continue;
        }
        out += text.at(i);
    }
    return out;
}

bool insideSidewaysFrame(const QWidget* w, const QWidget* leaf)
{
    for (const QWidget* p = w->parentWidget(); p != nullptr && p != leaf; p = p->parentWidget()) {
        if (p->property(kScrollsSideways).toBool()) {
            return true;
        }
    }
    return false;
}

bool insideItemView(const QWidget* w, const QWidget* leaf)
{
    for (const QWidget* p = w->parentWidget(); p != nullptr && p != leaf; p = p->parentWidget()) {
        if (qobject_cast<const QAbstractItemView*>(p) != nullptr) {
            return true;
        }
    }
    return false;
}

// False when a page stack between w and leaf shows another page: Qt can
// leave a tab's page visible at no size while another tab is current.
bool onShownStackPage(const QWidget* w, const QWidget* leaf)
{
    const QWidget* child = w;
    for (const QWidget* p = w->parentWidget(); p != nullptr && child != leaf; p = p->parentWidget()) {
        if (auto* stack = qobject_cast<const QStackedWidget*>(p)) {
            if (stack->currentWidget() != child) {
                return false;
            }
        }
        child = p;
    }
    return true;
}

// The part of `r` (in w's coordinates) that the parents up to `leaf` leave
// showing, in w's coordinates. From a scroll area's viewport outwards the
// parents clip sideways only: what lies below is reached by scrolling down.
QRect visiblePart(const QWidget* w, QRect r, const QWidget* leaf)
{
    QRect inW = r;
    QPoint offset(0, 0);  // w's origin in the current ancestor's coordinates
    const QWidget* child = w;
    bool scrollsDown = false;
    for (const QWidget* p = w->parentWidget(); p != nullptr; p = p->parentWidget()) {
        offset += child->pos();
        QRect clip = p->rect().translated(-offset);
        const auto* area = qobject_cast<const QAbstractScrollArea*>(p->parentWidget());
        if (area != nullptr && area->viewport() == p
            && area->verticalScrollBarPolicy() != Qt::ScrollBarAlwaysOff) {
            scrollsDown = true;
        }
        if (scrollsDown) {
            clip.setTop(std::min(clip.top(), inW.top()));
            clip.setBottom(std::max(clip.bottom(), inW.bottom()));
        }
        inW = inW.intersected(clip);
        if (p == leaf) {
            break;
        }
        child = p;
    }
    return inW;
}

QRect alignedTextRect(const QWidget* w, Qt::Alignment align, int textWidth, const QRect& within)
{
    return QStyle::alignedRect(w->layoutDirection(), align & Qt::AlignHorizontal_Mask,
                               QSize(textWidth, within.height()), within);
}

int labelTextWidth(const QLabel* label)
{
    const bool rich = label->textFormat() == Qt::RichText
        || (label->textFormat() == Qt::AutoText && Qt::mightBeRichText(label->text()));
    if (rich) {
        QTextDocument doc;
        doc.setDefaultFont(label->font());
        doc.setDocumentMargin(0);
        doc.setHtml(label->text());
        return static_cast<int>(std::ceil(doc.idealWidth()));
    }
    const QString text = label->buddy() != nullptr ? shownText(label->text()) : label->text();
    int widest = 0;
    for (const QString& line : text.split(QLatin1Char('\n'))) {
        widest = std::max(widest, label->fontMetrics().horizontalAdvance(line));
    }
    return widest;
}

QString describe(const QWidget* w, const QString& text)
{
    QString t = text.simplified();
    if (t.size() > 48) {
        t = t.left(45) + QStringLiteral("...");
    }
    return QStringLiteral("%1 \"%2\"").arg(QString::fromLatin1(w->metaObject()->className()), t);
}

// Text faults of one shown page or tab: cut short by the widget's own
// width, or clipped by a parent.
QStringList textFaults(QWidget* leaf)
{
    QStringList faults;
    auto checkRect = [&](const QWidget* w, const QString& text, const QRect& textRect, int needed,
                         int available) {
        if (needed > available) {
            faults << QStringLiteral("%1 cut short: needs %2 px, has %3")
                          .arg(describe(w, text)).arg(needed).arg(available);
            return;
        }
        const QRect shown = visiblePart(w, textRect, leaf);
        if (shown.width() < textRect.width() || shown.height() < textRect.height()) {
            faults << QStringLiteral("%1 clipped by a parent: %2 of %3 px wide showing")
                          .arg(describe(w, text)).arg(shown.width()).arg(textRect.width());
        }
    };
    auto skip = [leaf](const QWidget* w) {
        return !w->isVisible() || !onShownStackPage(w, leaf) || insideSidewaysFrame(w, leaf)
            || insideItemView(w, leaf);
    };

    for (QLabel* label : leaf->findChildren<QLabel*>()) {
        if (skip(label) || label->text().trimmed().isEmpty() || !label->pixmap().isNull()) {
            continue;
        }
        const int margin = label->margin();
        const QRect cr = label->contentsRect().adjusted(margin, margin, -margin, -margin);
        if (label->wordWrap()) {
            const int needed = label->heightForWidth(label->width());
            if (needed > label->height()) {
                faults << QStringLiteral("%1 cut short: needs %2 px tall, has %3")
                              .arg(describe(label, label->text())).arg(needed).arg(label->height());
            }
            const QRect shown = visiblePart(label, cr, leaf);
            if (shown.width() < cr.width()) {
                faults << QStringLiteral("%1 clipped by a parent: %2 of %3 px wide showing")
                              .arg(describe(label, label->text())).arg(shown.width()).arg(cr.width());
            }
            continue;
        }
        const int tw = labelTextWidth(label);
        const QRect tr = alignedTextRect(label, label->alignment(), tw, cr);
        checkRect(label, label->text(), tr, tw, cr.width());
    }

    for (QAbstractButton* button : leaf->findChildren<QAbstractButton*>()) {
        if (skip(button) || button->text().trimmed().isEmpty()) {
            continue;
        }
        QStyle::SubElement element;
        if (qobject_cast<QCheckBox*>(button) != nullptr) {
            element = QStyle::SE_CheckBoxContents;
        } else if (qobject_cast<QRadioButton*>(button) != nullptr) {
            element = QStyle::SE_RadioButtonContents;
        } else if (qobject_cast<QPushButton*>(button) != nullptr) {
            element = QStyle::SE_PushButtonContents;
        } else {
            continue;
        }
        QStyleOptionButton option;
        option.initFrom(button);
        option.text = button->text();
        option.icon = button->icon();
        option.iconSize = button->iconSize();
        if (auto* push = qobject_cast<QPushButton*>(button)) {
            if (push->isFlat()) {
                option.features |= QStyleOptionButton::Flat;
            }
        }
        const QRect contents = button->style()->subElementRect(element, &option, button)
                                   .intersected(button->rect());
        int tw = button->fontMetrics().horizontalAdvance(shownText(button->text()));
        if (!button->icon().isNull()) {
            tw += button->iconSize().width() + 4;
        }
        const Qt::Alignment align = element == QStyle::SE_PushButtonContents ? Qt::AlignHCenter
                                                                              : Qt::AlignLeft;
        const QRect tr = alignedTextRect(button, align, tw, contents);
        checkRect(button, shownText(button->text()), tr, tw, contents.width());
    }

    for (QGroupBox* box : leaf->findChildren<QGroupBox*>()) {
        if (skip(box) || box->title().trimmed().isEmpty()) {
            continue;
        }
        QStyleOptionGroupBox option;
        option.initFrom(box);
        option.text = box->title();
        option.lineWidth = 1;
        option.midLineWidth = 0;
        option.textAlignment = box->alignment();
        option.subControls = QStyle::SC_GroupBoxFrame | QStyle::SC_GroupBoxLabel;
        if (box->isCheckable()) {
            option.subControls |= QStyle::SC_GroupBoxCheckBox;
        }
        const QRect label = box->style()->subControlRect(QStyle::CC_GroupBox, &option,
                                                         QStyle::SC_GroupBoxLabel, box);
        const int tw = box->fontMetrics().horizontalAdvance(shownText(box->title()));
        const QRect inside = label.intersected(box->rect());
        if (inside.width() < std::min(tw, label.width())) {
            faults << QStringLiteral("%1 title cut short: %2 of %3 px inside the box")
                          .arg(describe(box, shownText(box->title()))).arg(inside.width()).arg(tw);
            continue;
        }
        const QRect shown = visiblePart(box, label, leaf);
        if (shown.width() < label.width()) {
            faults << QStringLiteral("%1 title clipped by a parent: %2 of %3 px wide showing")
                          .arg(describe(box, shownText(box->title()))).arg(shown.width())
                          .arg(label.width());
        }
    }
    return faults;
}

// The text a delegate draws for a cell needs `hint`; the view must give it
// that much, in its own cell and in what the viewport and parents show.
QStyleOptionViewItem cellOption(const QAbstractItemView* view, const QRect& rect)
{
    QStyleOptionViewItem option;
    option.initFrom(view->viewport());
    option.widget = view;
    option.font = view->font();
    option.fontMetrics = view->fontMetrics();
    option.rect = rect;
    option.textElideMode = view->textElideMode();
    const QSize icon = view->iconSize();
    if (icon.isValid()) {
        option.decorationSize = icon;
    } else {
        const int extent = view->style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, view);
        option.decorationSize = QSize(extent, extent);
    }
    bool wrap = false;
    if (auto* table = qobject_cast<const QTableView*>(view)) {
        wrap = table->wordWrap();
    } else if (auto* tree = qobject_cast<const QTreeView*>(view)) {
        wrap = tree->wordWrap();
    } else if (auto* list = qobject_cast<const QListView*>(view)) {
        wrap = list->wordWrap();
    }
    if (wrap) {
        option.features |= QStyleOptionViewItem::WrapText;
    }
    return option;
}

// Cell faults of one shown page or tab, for every table, tree and list:
// a cell whose text its own rect cuts short (elided or cut), or a cell the
// viewport or a parent clips where no scroll bar of the view reaches it.
// A cell holding an index widget draws that widget instead and is left
// out. Inside a frame that scrolls sideways, parents clip sideways by
// design and only the cell's own rect counts.
int cellsChecked = 0;  // cells with text the cell check has measured

QStringList cellFaults(QWidget* leaf)
{
    QStringList faults;
    for (QAbstractItemView* view : leaf->findChildren<QAbstractItemView*>()) {
        if (!view->isVisible() || !onShownStackPage(view, leaf) || view->model() == nullptr) {
            continue;
        }
        QWidget* viewport = view->viewport();
        const bool sideways = insideSidewaysFrame(view, leaf) || view->property(kScrollsSideways).toBool();
        // A bar turned off still has a range; the user cannot reach it.
        const bool scrollsAcross = view->horizontalScrollBarPolicy() != Qt::ScrollBarAlwaysOff
            && view->horizontalScrollBar()->maximum() > 0;
        const bool scrollsDown = view->verticalScrollBarPolicy() != Qt::ScrollBarAlwaysOff
            && view->verticalScrollBar()->maximum() > 0;
        const QRect seen = sideways ? viewport->rect() : visiblePart(viewport, viewport->rect(), leaf);
        QList<QModelIndex> parents{view->rootIndex()};
        while (!parents.isEmpty()) {
            const QModelIndex parent = parents.takeFirst();
            const QAbstractItemModel* model = view->model();
            for (int row = 0; row < model->rowCount(parent); ++row) {
                for (int column = 0; column < model->columnCount(parent); ++column) {
                    const QModelIndex index = model->index(row, column, parent);
                    if (column == 0 && model->hasChildren(index)) {
                        auto* tree = qobject_cast<QTreeView*>(view);
                        if (tree != nullptr && tree->isExpanded(index)) {
                            parents.append(index);
                        }
                    }
                    const QRect rect = view->visualRect(index);
                    const QString text = index.data(Qt::DisplayRole).toString();
                    if (rect.isEmpty() || text.trimmed().isEmpty() || view->indexWidget(index) != nullptr) {
                        continue;
                    }
                    const QStyleOptionViewItem option = cellOption(view, rect);
                    const QSize hint = view->itemDelegateForIndex(index)->sizeHint(option, index);
                    const QString who = QStringLiteral("%1 cell %2,%3 %4")
                                            .arg(QString::fromLatin1(view->metaObject()->className()))
                                            .arg(row).arg(column).arg(describe(view, text));
                    ++cellsChecked;
                    if (hint.width() > rect.width() || hint.height() > rect.height()) {
                        faults << QStringLiteral("%1 cut short: needs %2 x %3 px, has %4 x %5")
                                      .arg(who).arg(hint.width()).arg(hint.height())
                                      .arg(rect.width()).arg(rect.height());
                        continue;
                    }
                    const QRect shown = rect.intersected(seen);
                    const bool clippedAcross = shown.width() < rect.width() && !scrollsAcross;
                    const bool clippedDown = shown.height() < rect.height() && !scrollsDown;
                    if (!shown.isEmpty() || (!scrollsAcross && !scrollsDown)) {
                        if (clippedAcross || clippedDown) {
                            faults << QStringLiteral("%1 clipped: %2 x %3 of %4 x %5 px showing")
                                          .arg(who).arg(shown.width()).arg(shown.height())
                                          .arg(rect.width()).arg(rect.height());
                        }
                    }
                }
            }
        }
    }
    return faults;
}

// What makes a scroll area's content wider than its viewport: the chain of
// shown widgets under it whose least width is more than `available`, widest
// first at each level, as "Class#name (least width)". Names the widget to
// look at when a page scrolls sideways on a platform nobody is sitting at.
QString widerThan(const QWidget* widget, int available)
{
    const auto least = [](const QWidget* w) {
        return std::max(w->minimumSizeHint().width(), w->minimumWidth());
    };
    QStringList chain;
    for (const QWidget* at = widget; at != nullptr;) {
        chain << QStringLiteral("%1#%2 (%3 px)")
                     .arg(QString::fromLatin1(at->metaObject()->className()), at->objectName())
                     .arg(least(at));
        const QWidget* widest = nullptr;
        for (const QWidget* child : at->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
            if (!child->isHidden() && !child->isWindow() && least(child) > available
                && (widest == nullptr || least(child) > least(widest))) {
                widest = child;
            }
        }
        at = widest;
    }
    return chain.join(QStringLiteral(" > "));
}

// The least width the layout a widget sits in keeps for it, or -1 when it
// is in no layout. Less than the widget's own hint means the layout holds
// a size it read before the hint grew.
int keptWidth(const QWidget* widget)
{
    const QWidget* parent = widget->parentWidget();
    QLayout* layout = parent != nullptr ? parent->layout() : nullptr;
    if (layout == nullptr) {
        return -1;
    }
    const int index = layout->indexOf(widget);
    return index >= 0 ? layout->itemAt(index)->minimumSize().width() : -1;
}

// Scroll faults of one shown page or tab: a scroll area that scrolls
// sideways, or a tab strip inside a scroll area that scrolls.
QStringList scrollFaults(QWidget* leaf, QStringList* sideways)
{
    QStringList faults;
    for (QScrollArea* area : leaf->findChildren<QScrollArea*>()) {
        if (!area->isVisible() || !onShownStackPage(area, leaf)) {
            continue;
        }
        const int range = area->horizontalScrollBar()->maximum();
        if (range <= 0) {
            continue;
        }
        const QString who = area->widget() != nullptr
            ? QString::fromLatin1(area->widget()->metaObject()->className())
            : QStringLiteral("(empty)");
        if (area->property(kScrollsSideways).toBool() || insideSidewaysFrame(area, leaf)) {
            if (sideways != nullptr) {
                *sideways << QStringLiteral("%1: content %2 px in a %3 px frame")
                                 .arg(area->objectName().isEmpty() ? who : area->objectName())
                                 .arg(area->widget() != nullptr ? area->widget()->width() : 0)
                                 .arg(area->viewport()->width());
            }
            continue;
        }
        faults << QStringLiteral("scroll area holding %1 scrolls sideways by %2 px").arg(who).arg(range)
                      + (area->widget() != nullptr
                             ? QStringLiteral(": a %1 px viewport (area hint %3 px, content hint %4 px, "
                                              "least width its layout keeps for it %5 px) holds %2")
                                   .arg(area->viewport()->width())
                                   .arg(widerThan(area->widget(), area->viewport()->width()))
                                   .arg(area->sizeHint().width())
                                   .arg(area->widget()->sizeHint().width())
                                   .arg(keptWidth(area))
                             : QString());
    }
    for (QTabBar* bar : leaf->findChildren<QTabBar*>()) {
        if (!bar->isVisible() || !onShownStackPage(bar, leaf)) {
            continue;
        }
        for (QWidget* p = bar->parentWidget(); p != nullptr && p != leaf->parentWidget();
             p = p->parentWidget()) {
            auto* area = qobject_cast<QScrollArea*>(p);
            if (area != nullptr && area->verticalScrollBar()->maximum() > 0) {
                faults << QStringLiteral("tab strip \"%1\" scrolls away with its page (%2 px)")
                              .arg(bar->tabText(0)).arg(area->verticalScrollBar()->maximum());
                break;
            }
        }
    }
    return faults;
}

// Widgets squeezed below what their own layout needs: Qt lets a parent
// that ignores minimums (a page in the dialog stack, a scroll area's
// content) give a group less width than its rows need, and the rows then
// overlap or cut off. A widget inside a frame that scrolls sideways is
// left to that frame.
QStringList squeezeFaults(QWidget* leaf)
{
    QStringList faults;
    for (QWidget* w : leaf->findChildren<QWidget*>()) {
        if (!w->isVisible() || w->layout() == nullptr || !onShownStackPage(w, leaf)
            || insideSidewaysFrame(w, leaf) || insideItemView(w, leaf)) {
            continue;
        }
        const int needed = w->layout()->minimumSize().width();
        if (w->width() < needed) {
            QString text = w->objectName();
            if (auto* box = qobject_cast<QGroupBox*>(w)) {
                text = shownText(box->title());
            }
            faults << QStringLiteral("%1 squeezed below its minimum width: needs %2 px, has %3")
                          .arg(describe(w, text)).arg(needed).arg(w->width());
        }
    }
    return faults;
}

// A tab container under leaf: a QTabWidget, or a bare QTabBar.
struct TabHost {
    QTabWidget* widget = nullptr;
    QTabBar* bar = nullptr;
    int count() const { return widget != nullptr ? widget->count() : bar->count(); }
    bool visibleTab(int i) const
    {
        return widget != nullptr ? widget->isTabVisible(i) : bar->isTabVisible(i);
    }
    QString text(int i) const { return widget != nullptr ? widget->tabText(i) : bar->tabText(i); }
    int current() const { return widget != nullptr ? widget->currentIndex() : bar->currentIndex(); }
    void select(int i) const
    {
        if (widget != nullptr) {
            widget->setCurrentIndex(i);
        } else {
            bar->setCurrentIndex(i);
        }
    }
    const QObject* key() const
    {
        return widget != nullptr ? static_cast<const QObject*>(widget) : bar;
    }
};

// The first showing tab host under leaf that the walk has not taken yet.
bool nextTabHost(QWidget* leaf, const QSet<const QObject*>& done, TabHost* out)
{
    for (QTabWidget* tabs : leaf->findChildren<QTabWidget*>()) {
        if (tabs->isVisible() && !done.contains(tabs)) {
            out->widget = tabs;
            out->bar = nullptr;
            return true;
        }
    }
    for (QTabBar* bar : leaf->findChildren<QTabBar*>()) {
        if (bar->isVisible() && !done.contains(bar)
            && qobject_cast<QTabWidget*>(bar->parentWidget()) == nullptr) {
            out->widget = nullptr;
            out->bar = bar;
            return true;
        }
    }
    return false;
}

struct Walk {
    QStringList faults;
    QStringList sideways;
    QStringList shown;      // every page and tab path checked
    QStringList stacked;    // "<path>: <row>" for each row that stacked
    QStringList sideBySide; // the same for each row side by side
};

void recordRows(QWidget* leaf, const QString& where, Walk* walk)
{
    for (QObject* o : leaf->findChildren<QObject*>()) {
        const QVariant v = o->property(kFitRowStacked);
        if (!v.isValid()) {
            continue;
        }
        auto* layout = qobject_cast<QLayout*>(o);
        if (layout == nullptr || layout->parentWidget() == nullptr || !layout->parentWidget()->isVisible()
            || !onShownStackPage(layout->parentWidget(), leaf) || layout->geometry().isEmpty()) {
            continue;
        }
        const QString name = QStringLiteral("%1: %2").arg(where, o->objectName());
        (v.toBool() ? walk->stacked : walk->sideBySide) << name;
    }
}

// Every fault of one shown page or tab; the frames that scroll sideways
// inside themselves go to `sideways`.
QStringList pageFaults(QWidget* leaf, QStringList* sideways)
{
    QStringList faults = scrollFaults(leaf, sideways);
    faults += textFaults(leaf);
    faults += squeezeFaults(leaf);
    faults += cellFaults(leaf);
    return faults;
}

void checkShown(QWidget* leaf, const QString& where, Walk* walk)
{
    settle();
    walk->shown << where;
    QStringList sideways;
    const QStringList faults = pageFaults(leaf, &sideways);
    for (const QString& f : faults) {
        walk->faults << QStringLiteral("%1: %2").arg(where, f);
    }
    for (const QString& s : sideways) {
        walk->sideways << QStringLiteral("%1: %2").arg(where, s);
    }
    recordRows(leaf, where, walk);
}

void walkTabs(QWidget* leaf, const QString& where, QSet<const QObject*> done, Walk* walk)
{
    settle();
    TabHost host;
    if (!nextTabHost(leaf, done, &host)) {
        checkShown(leaf, where, walk);
        return;
    }
    done.insert(host.key());
    const int was = host.current();
    for (int i = 0; i < host.count(); ++i) {
        if (!host.visibleTab(i)) {
            continue;
        }
        host.select(i);
        walkTabs(leaf, where + QStringLiteral(" > ") + host.text(i), done, walk);
    }
    host.select(was);
    settle();
}

RadioInfo posedRadio(HPSDRHW board)
{
    RadioInfo info;
    info.boardType = board;
    if (board == HPSDRHW::HermesLite) {
        info.name = QStringLiteral("Hermes Lite 2");
        info.macAddress = QStringLiteral("00:1C:C0:7B:90:3C");
        info.address = QHostAddress(QStringLiteral("192.168.1.31"));
        info.protocol = ProtocolVersion::Protocol1;
        info.firmwareVersion = 73;
    } else {
        info.name = QStringLiteral("ANAN-G2");
        info.macAddress = QStringLiteral("00:1C:C0:4E:21:0A");
        info.address = QHostAddress(QStringLiteral("192.168.1.20"));
        info.protocol = ProtocolVersion::Protocol2;
        info.firmwareVersion = 27;
    }
    info.adcCount = RadioInfo::adcCountForBoard(board);
    info.maxReceivers = RadioInfo::maxReceiversForBoard(board);
    return info;
}

// Opens Setup at width x height in the given look and walks every page and
// tab; with onlyHardware, Hardware Config alone (the Hermes Lite 2 pose).
Walk walkDialog(bool workspace, int width, int height, HPSDRHW board, bool onlyHardware)
{
    Walk walk;
    AppLook look(workspace);
    RadioModel model;
    model.setBoardForTest(board);
    SetupDialog dialog(&model);
    dialog.realizeAllPagesForTest();
    look.apply(&dialog);
    dialog.resize(width, height);
    dialog.show();
    if (!QTest::qWaitForWindowExposed(&dialog)) {
        walk.faults << QStringLiteral("the dialog did not show");
        return walk;
    }
    settle();
    if (dialog.width() != width || dialog.height() != height) {
        walk.faults << QStringLiteral("the dialog is %1 x %2, not %3 x %4 (a page is too wide)")
                           .arg(dialog.width()).arg(dialog.height()).arg(width).arg(height);
    }
    if (auto* hardware = dialog.findChild<HardwarePage*>()) {
        hardware->onCurrentRadioChanged(posedRadio(board));
    }
    auto* stack = dialog.findChild<QStackedWidget*>();
    if (stack == nullptr) {
        walk.faults << QStringLiteral("no page stack");
        return walk;
    }
    const QString boardName = board == HPSDRHW::HermesLite ? QStringLiteral("HL2")
                                                           : QStringLiteral("Saturn");
    const QStringList labels = dialog.pageLabelsForTest();
    for (int index = 0; index < labels.size(); ++index) {
        if (onlyHardware && labels.at(index) != QLatin1String("Hardware Config")) {
            continue;
        }
        QWidget* leaf = dialog.realizePageAtForTest(index);
        if (leaf == nullptr) {
            continue;
        }
        stack->setCurrentWidget(leaf);
        settle();
        QString where = labels.at(index);
        if (labels.at(index) == QLatin1String("Hardware Config")) {
            where += QStringLiteral(" (%1)").arg(boardName);
        }
        walkTabs(leaf, where, {}, &walk);
    }
    return walk;
}

// A row of two columns in a tab's scroll area (SetupFit::scrollingTab),
// sized so the columns fit side by side in the area's width without a
// vertical scroll bar and not in the width a bar leaves.
struct PlantedRow {
    QScrollArea* area = nullptr;
    SetupFit::FitRow* row = nullptr;
    int barWidth = 0;
    int sideBySideWidth = 0; // the area width the test settles at
    int columnHeight = 0;

    bool barShows() const { return area->verticalScrollBar()->isVisible(); }
    QString state() const
    {
        return QStringLiteral("%1, %2, viewport %3 x %4, content %5 x %6")
            .arg(row->isStacked() ? QStringLiteral("stacked") : QStringLiteral("side by side"),
                 barShows() ? QStringLiteral("bar") : QStringLiteral("no bar"))
            .arg(area->viewport()->width()).arg(area->viewport()->height())
            .arg(area->widget()->width()).arg(area->widget()->height());
    }
};

PlantedRow plantRow(QWidget* host)
{
    constexpr int kColumnWidth = 300;
    constexpr int kColumnHeight = 260;
    constexpr int kSpacing = 6;
    PlantedRow p;
    auto* content = new QWidget;
    auto* page = new QVBoxLayout(content);
    page->setContentsMargins(0, 0, 0, 0);
    p.row = new SetupFit::FitRow;
    p.row->setContentsMargins(0, 0, 0, 0);
    p.row->setSpacing(kSpacing);
    for (int i = 0; i < 2; ++i) {
        auto* column = new QFrame(content);
        column->setMinimumSize(kColumnWidth, kColumnHeight);
        p.row->addWidget(column, 1);
    }
    page->addLayout(p.row);
    page->addStretch(1);
    p.area = SetupFit::scrollingTab(content);
    p.area->setParent(host);
    p.barWidth = p.area->verticalScrollBar()->sizeHint().width();
    p.sideBySideWidth = p.row->sideBySideMinimumWidth() + p.barWidth / 2;
    p.columnHeight = kColumnHeight;
    return p;
}

// Puts the application font back when a case that changes it ends.
struct FontRestore {
    QFont font{QApplication::font()};
    FontRestore() = default;
    FontRestore(const FontRestore&) = delete;
    FontRestore& operator=(const FontRestore&) = delete;
    ~FontRestore() { QApplication::setFont(font); }
};

// Where the Calibration tab's top row of groups stands.
struct CalibrationState {
    bool found = false;
    bool stacked = false;
    bool bar = false;
    QString text;
};

QTabWidget* tabsHolding(QWidget* leaf, const QString& tabText, int* index)
{
    for (QTabWidget* tabs : leaf->findChildren<QTabWidget*>()) {
        for (int i = 0; i < tabs->count(); ++i) {
            if (tabs->tabText(i) == tabText) {
                *index = i;
                return tabs;
            }
        }
    }
    return nullptr;
}

// The scroll area the Calibration tab's groups scroll in.
QScrollArea* calibrationArea(QWidget* leaf)
{
    auto* row = leaf->findChild<QBoxLayout*>(QStringLiteral("calibrationTopRow"));
    QScrollArea* area = nullptr;
    for (QWidget* w = row != nullptr ? row->parentWidget() : nullptr; w != nullptr && area == nullptr;
         w = w->parentWidget()) {
        area = qobject_cast<QScrollArea*>(w);
    }
    return area;
}

CalibrationState calibrationState(QWidget* leaf)
{
    CalibrationState s;
    auto* row = leaf->findChild<QBoxLayout*>(QStringLiteral("calibrationTopRow"));
    QScrollArea* area = calibrationArea(leaf);
    if (row == nullptr || area == nullptr || !area->isVisible()) {
        s.text = QStringLiteral("the Calibration tab is not showing");
        return s;
    }
    s.found = true;
    s.stacked = row->property(kFitRowStacked).toBool();
    s.bar = area->verticalScrollBar()->isVisible();
    s.text = QStringLiteral("%1, %2, viewport %3 x %4, content %5 x %6")
                 .arg(s.stacked ? QStringLiteral("stacked") : QStringLiteral("side by side"),
                      s.bar ? QStringLiteral("bar") : QStringLiteral("no bar"))
                 .arg(area->viewport()->width()).arg(area->viewport()->height())
                 .arg(area->widget()->width()).arg(area->widget()->height());
    return s;
}

} // namespace

class TstSetupPagesFitWindow : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        m_startFont = QApplication::font();
        AppSettings::instance().clear();
#ifdef Q_OS_MAC
        // The font the window gets on macOS (the scenes' sidecars record
        // .AppleSystemUIFont at 13 px); offscreen would otherwise use a
        // 9 pt fallback and hide text that does not fit.
        QApplication::setFont(QFont(QStringLiteral(".AppleSystemUIFont"), 13));
#endif
        qInfo().noquote() << "font:" << QApplication::font().family()
                          << QApplication::font().pointSizeF() << "pt";
    }

    void cleanup()
    {
        AppSettings::instance().clear();
    }

    void every_page_fits_data()
    {
        QTest::addColumn<bool>("workspace");
        QTest::addColumn<int>("width");
        QTest::addColumn<int>("height");
        QTest::newRow("today 900x650") << false << kDefaultWidth << kDefaultHeight;
        QTest::newRow("today 820x600") << false << kMinimumWidth << kMinimumHeight;
        QTest::newRow("workspace 900x650") << true << kDefaultWidth << kDefaultHeight;
        QTest::newRow("workspace 820x600") << true << kMinimumWidth << kMinimumHeight;
    }

    // Nothing scrolls sideways, no tab strip scrolls away, no text is cut
    // short or clipped, on any page or tab, Saturn and HL2 poses.
    void every_page_fits()
    {
        QFETCH(bool, workspace);
        QFETCH(int, width);
        QFETCH(int, height);
        cellsChecked = 0;
        Walk saturn = walkDialog(workspace, width, height, HPSDRHW::Saturn, false);
        Walk hl2 = walkDialog(workspace, width, height, HPSDRHW::HermesLite, true);
        const QStringList faults = saturn.faults + hl2.faults;
        const QStringList sideways = saturn.sideways + hl2.sideways;
        const int shown = static_cast<int>(saturn.shown.size() + hl2.shown.size());
        qInfo().noquote() << QStringLiteral("%1 pages and tabs checked, %2 table cells measured")
                                 .arg(shown).arg(cellsChecked);
        for (const QString& s : sideways) {
            qInfo().noquote() << "SCROLLS INSIDE ITS FRAME" << s;
        }
        QVERIFY2(shown > 70, qPrintable(QStringLiteral("only %1 pages and tabs checked").arg(shown)));
        for (const QString& f : faults) {
            qInfo().noquote() << "FAULT" << f;
        }
        QVERIFY2(faults.isEmpty(),
                 qPrintable(QStringLiteral("%1 faults, listed above as FAULT lines").arg(faults.size())));
    }

    // A planted table: one cell whose text its 40 px column cuts short,
    // one that fits, and a column the frame clips with no sideways scroll.
    // The page check finds the first and the last and nothing else.
    void pageFaultsCatchAClippedCell()
    {
        QWidget page;
        auto* layout = new QVBoxLayout(&page);
        auto* table = new QTableWidget(1, 3, &page);
        table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        table->horizontalHeader()->setVisible(false);
        table->verticalHeader()->setVisible(false);
        table->setItem(0, 0, new QTableWidgetItem(QStringLiteral("A cell far wider than its column")));
        table->setItem(0, 1, new QTableWidgetItem(QStringLiteral("OK")));
        table->setItem(0, 2, new QTableWidgetItem(QStringLiteral("Off")));
        table->setColumnWidth(0, 40);
        table->setColumnWidth(1, 60);
        table->setColumnWidth(2, 200);
        table->setFixedWidth(160);
        layout->addWidget(table);
        page.resize(400, 200);
        page.show();
        QVERIFY(QTest::qWaitForWindowExposed(&page));
        settle();
        QStringList sideways;
        const QStringList faults = pageFaults(&page, &sideways);
        for (const QString& f : faults) {
            qInfo().noquote() << "FAULT" << f;
        }
        QCOMPARE(faults.size(), 2);
        QVERIFY(faults.at(0).contains(QStringLiteral("A cell far wider")));
        QVERIFY(faults.at(1).contains(QStringLiteral("Off")));
    }

    // The rows that stacked at 820 x 600 are side by side again at
    // 1440 x 900, both looks.
    void widening_restores_side_by_side_data()
    {
        QTest::addColumn<bool>("workspace");
        QTest::newRow("today") << false;
        QTest::newRow("workspace") << true;
    }

    void widening_restores_side_by_side()
    {
        QFETCH(bool, workspace);
        Walk narrow = walkDialog(workspace, kMinimumWidth, kMinimumHeight, HPSDRHW::Saturn, false);
        Walk narrowHl2 = walkDialog(workspace, kMinimumWidth, kMinimumHeight, HPSDRHW::HermesLite, true);
        Walk wide = walkDialog(workspace, kWideWidth, kWideHeight, HPSDRHW::Saturn, false);
        Walk wideHl2 = walkDialog(workspace, kWideWidth, kWideHeight, HPSDRHW::HermesLite, true);
        const QStringList stackedNarrow = narrow.stacked + narrowHl2.stacked;
        const QStringList sideBySideWide = wide.sideBySide + wideHl2.sideBySide;
        for (const QString& row : stackedNarrow) {
            qInfo().noquote() << "STACKED AT 820" << row;
        }
        QVERIFY2(!stackedNarrow.isEmpty(), "no group row stacked at 820 x 600");
        QStringList stillStacked;
        for (const QString& row : stackedNarrow) {
            if (!sideBySideWide.contains(row)) {
                stillStacked << row;
            }
        }
        for (const QString& row : stillStacked) {
            qInfo().noquote() << "STILL STACKED" << row;
        }
        QVERIFY2(stillStacked.isEmpty(),
                 qPrintable(QStringLiteral("%1 rows not side by side at 1440 x 900").arg(stillStacked.size())));
        const bool antenna = std::any_of(stackedNarrow.begin(), stackedNarrow.end(), [](const QString& r) {
            return r.contains(QStringLiteral("Antenna / ALEX"));
        });
        QVERIFY2(antenna, "no Antenna / ALEX row stacked at 820 x 600");
        // The two OC matrices are 558 px side by side whatever the font
        // (their columns are fixed widths), and the tab at 820 px is within
        // a few pixels of that: 557 px in today's look and 571 px in the
        // workspace look with the Linux CI fonts, more or less with others.
        // So the row stacks where it must and sits side by side where it
        // fits (every_page_fits proves it fits); it has to be one of them.
        const auto isOc = [](const QString& r) { return r.contains(QStringLiteral("OC Outputs")); };
        const QStringList sideBySideNarrow = narrow.sideBySide + narrowHl2.sideBySide;
        const bool ocStacked = std::any_of(stackedNarrow.begin(), stackedNarrow.end(), isOc);
        const bool ocSideBySide = std::any_of(sideBySideNarrow.begin(), sideBySideNarrow.end(), isOc);
        qInfo().noquote() << "OC Outputs rows at 820 x 600:"
                          << (ocStacked ? "stacked" : ocSideBySide ? "side by side" : "none shown");
        QVERIFY2(ocStacked || ocSideBySide, "no OC Outputs row shown at 820 x 600");
    }

    void rowThatFitsWithoutABarIsSideBySide_data()
    {
        QTest::addColumn<QString>("history");
        QTest::newRow("shown at its size") << QStringLiteral("direct");
        QTest::newRow("widened from a stacked row with a bar") << QStringLiteral("narrow");
        QTest::newRow("narrowed from a wide row") << QStringLiteral("wide");
        QTest::newRow("grown from a height that needs the bar") << QStringLiteral("short");
    }

    // The columns fit side by side in the area's width and height with no
    // vertical scroll bar, and not in the width a bar would leave: the row
    // settles side by side with no bar, whatever it showed before.
    void rowThatFitsWithoutABarIsSideBySide()
    {
        QFETCH(QString, history);
        QWidget host;
        auto* layout = new QVBoxLayout(&host);
        layout->setContentsMargins(0, 0, 0, 0);
        PlantedRow p = plantRow(&host);
        layout->addWidget(p.area);
        if (p.barWidth < 2) {
            QSKIP("this style's scroll bar takes no width, so a row cannot flip");
        }
        const QSize fits(p.sideBySideWidth, p.columnHeight + 40);
        QSize first = fits;
        if (history == QLatin1String("narrow")) {
            first = QSize(p.sideBySideWidth - 4 * p.barWidth, fits.height());
        } else if (history == QLatin1String("wide")) {
            first = QSize(p.sideBySideWidth * 2, fits.height());
        } else if (history == QLatin1String("short")) {
            first = QSize(p.sideBySideWidth, p.columnHeight - 40);
        }
        host.resize(first);
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        settle();
        qInfo().noquote() << "first:" << p.state();
        if (history == QLatin1String("narrow") || history == QLatin1String("short")) {
            QVERIFY2(p.row->isStacked() && p.barShows(), qPrintable(p.state()));
        }
        host.resize(fits);
        settle();
        qInfo().noquote() << "settled:" << p.state();
        QVERIFY2(!p.row->isStacked(), qPrintable(p.state()));
        QVERIFY2(!p.barShows(), qPrintable(p.state()));
        QCOMPARE(p.area->viewport()->width(), p.sideBySideWidth);
    }

    void rowThatCannotFitStacksAndScrolls_data()
    {
        QTest::addColumn<QString>("history");
        QTest::newRow("shown at its size") << QStringLiteral("direct");
        QTest::newRow("shrunk from a row side by side") << QStringLiteral("tall");
        QTest::newRow("narrowed from a wide row") << QStringLiteral("wide");
    }

    // Side by side the columns are taller than the area, so the bar is
    // needed, and the width it leaves is too narrow for them: the row
    // stacks and the page scrolls down, never sideways, from any history,
    // and a page scrolled part way down stays where it was.
    void rowThatCannotFitStacksAndScrolls()
    {
        QFETCH(QString, history);
        QWidget host;
        auto* layout = new QVBoxLayout(&host);
        layout->setContentsMargins(0, 0, 0, 0);
        PlantedRow p = plantRow(&host);
        layout->addWidget(p.area);
        if (p.barWidth < 2) {
            QSKIP("this style's scroll bar takes no width, so a row cannot flip");
        }
        const QSize tooShort(p.sideBySideWidth, p.columnHeight - 40);
        QSize first = tooShort;
        if (history == QLatin1String("tall")) {
            first = QSize(p.sideBySideWidth, p.columnHeight + 40);
        } else if (history == QLatin1String("wide")) {
            first = QSize(p.sideBySideWidth * 2, p.columnHeight + 40);
        }
        host.resize(first);
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        settle();
        qInfo().noquote() << "first:" << p.state();
        host.resize(tooShort);
        settle();
        qInfo().noquote() << "settled:" << p.state();
        QVERIFY2(p.row->isStacked(), qPrintable(p.state()));
        QVERIFY2(p.barShows(), qPrintable(p.state()));
        QVERIFY2(!p.area->horizontalScrollBar()->isVisible(), qPrintable(p.state()));
        QCOMPARE(p.area->widget()->width(), p.area->viewport()->width());

        // Scrolled part way, a new layout pass leaves the page where it is.
        QScrollBar* bar = p.area->verticalScrollBar();
        QVERIFY(bar->maximum() > 20);
        bar->setValue(20);
        p.area->widget()->layout()->invalidate();
        settle();
        QCOMPARE(bar->value(), 20);
        host.resize(tooShort + QSize(0, 3));
        settle();
        QVERIFY2(p.row->isStacked() && p.barShows(), qPrintable(p.state()));
        QCOMPARE(bar->value(), 20);
    }

    void calibrationSettlesSideBySide_data()
    {
        QTest::addColumn<bool>("workspace");
        QTest::addColumn<QString>("route");
        for (const bool workspace : {false, true}) {
            const QByteArray look = workspace ? "workspace" : "today";
            QTest::newRow(look + ", straight to the tab") << workspace << QStringLiteral("direct");
            QTest::newRow(look + ", through the tabs before it") << workspace << QStringLiteral("tabs");
            QTest::newRow(look + ", opened at the minimum size") << workspace << QStringLiteral("from-min");
            QTest::newRow(look + ", to the minimum size and back") << workspace << QStringLiteral("min-and-back");
            QTest::newRow(look + ", opened wide") << workspace << QStringLiteral("from-wide");
            QTest::newRow(look + ", seen at the minimum size, resized on another tab")
                << workspace << QStringLiteral("min-then-elsewhere");
            QTest::newRow(look + ", seen at the minimum size, resized on another page")
                << workspace << QStringLiteral("min-then-another-page");
        }
    }

    // Setup > Hardware Config > Calibration at the window's own size
    // (900 x 650): its two columns of groups fit side by side with no
    // vertical scroll bar, so that is how the tab settles by every route
    // to it (JJ's decision D25). At the minimum size they cannot fit and
    // stack.
    void calibrationSettlesSideBySide()
    {
        QFETCH(bool, workspace);
        QFETCH(QString, route);
        // The font the process started with, as tst_workspace_colour_sweep
        // runs in, where the tab was seen in both layouts. At the 13 pt the
        // other cases set (larger offscreen, stricter for them) these
        // columns cannot fit side by side at 900 x 650 at all.
        const FontRestore restore;
        QApplication::setFont(m_startFont);
        AppLook look(workspace);
        RadioModel model;
        model.setBoardForTest(HPSDRHW::Saturn);
        SetupDialog dialog(&model);
        dialog.realizeAllPagesForTest();
        look.apply(&dialog);
        QSize first(kDefaultWidth, kDefaultHeight);
        if (route == QLatin1String("from-min")) {
            first = QSize(kMinimumWidth, kMinimumHeight);
        } else if (route == QLatin1String("from-wide")) {
            first = QSize(kWideWidth, kWideHeight);
        }
        dialog.resize(first);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        settle();
        auto* hardware = dialog.findChild<HardwarePage*>();
        QVERIFY(hardware != nullptr);
        hardware->onCurrentRadioChanged(posedRadio(HPSDRHW::Saturn));
        auto* stack = dialog.findChild<QStackedWidget*>();
        QVERIFY(stack != nullptr);
        QWidget* leaf = dialog.realizePageAtForTest(
            static_cast<int>(dialog.pageLabelsForTest().indexOf(QStringLiteral("Hardware Config"))));
        QVERIFY(leaf != nullptr);
        stack->setCurrentWidget(leaf);
        settle();
        int calibration = -1;
        QTabWidget* tabs = tabsHolding(leaf, QStringLiteral("Calibration"), &calibration);
        QVERIFY(tabs != nullptr);
        if (route == QLatin1String("tabs")) {
            for (int i = 0; i < calibration; ++i) {
                if (tabs->isTabVisible(i)) {
                    tabs->setCurrentIndex(i);
                    settle();
                }
            }
        }
        tabs->setCurrentIndex(calibration);
        settle();
        qInfo().noquote() << "first:" << calibrationState(leaf).text;
        if (route == QLatin1String("from-min")) {
            const CalibrationState narrow = calibrationState(leaf);
            QVERIFY2(narrow.found && narrow.stacked, qPrintable(narrow.text));
        }
        if (route == QLatin1String("min-and-back")) {
            dialog.resize(kMinimumWidth, kMinimumHeight);
            settle();
            const CalibrationState narrow = calibrationState(leaf);
            qInfo().noquote() << "at the minimum:" << narrow.text;
            QVERIFY2(narrow.found && narrow.stacked, qPrintable(narrow.text));
        }
        if (route == QLatin1String("min-then-elsewhere") || route == QLatin1String("min-then-another-page")) {
            // As a run of scenes does: the tab at the minimum size, then
            // the window back at its own size while something else shows.
            dialog.resize(kMinimumWidth, kMinimumHeight);
            settle();
            const CalibrationState narrow = calibrationState(leaf);
            QVERIFY2(narrow.found && narrow.stacked, qPrintable(narrow.text));
            if (route == QLatin1String("min-then-elsewhere")) {
                tabs->setCurrentIndex(0);
            } else {
                stack->setCurrentWidget(dialog.realizePageAtForTest(0));
            }
            settle();
            dialog.resize(kDefaultWidth, kDefaultHeight);
            settle();
            stack->setCurrentWidget(leaf);
            tabs->setCurrentIndex(calibration);
            settle();
        }
        dialog.resize(kDefaultWidth, kDefaultHeight);
        settle();
        const CalibrationState state = calibrationState(leaf);
        qInfo().noquote() << "settled:" << state.text;
        QVERIFY2(state.found, qPrintable(state.text));

        // Whether the columns fit without the bar, asked of the page with
        // the bar taken away: side by side and nothing left to scroll.
        QScrollArea* area = calibrationArea(leaf);
        QVERIFY(area != nullptr);
        area->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        settle();
        const CalibrationState noBar = calibrationState(leaf);
        const bool fits = !noBar.stacked && area->verticalScrollBar()->maximum() == 0;
        qInfo().noquote() << "with no bar:" << noBar.text << (fits ? "(fits)" : "(does not fit)");
        area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        settle();
#ifdef Q_OS_MAC
        QVERIFY2(fits, "the columns no longer fit 900 x 650 side by side in this font; the case proves nothing");
#endif
        if (fits) {
            QVERIFY2(!state.stacked, qPrintable(state.text));
            QVERIFY2(!state.bar, qPrintable(state.text));
        } else {
            QVERIFY2(state.stacked, qPrintable(state.text));
            QVERIFY2(state.bar, qPrintable(state.text));
        }
    }

    void sideBySideRowLeavesSpareHeightToThePage_data()
    {
        QTest::addColumn<bool>("widgetAsksForHeight");
        QTest::newRow("columns of groups ending in a stretch") << false;
        QTest::newRow("a widget in a column asks for height") << true;
    }

    // A side-by-side row of columns is as tall as its taller column: the
    // stretch that ends a column is not a request for the page's spare
    // height, so the group under the row follows at the page's spacing
    // and the spare is at the bottom of the page (JJ, 2026-10-10). A row
    // holding a widget that asks for height still takes it.
    void sideBySideRowLeavesSpareHeightToThePage()
    {
        QFETCH(bool, widgetAsksForHeight);
        QWidget page;
        auto* pageLayout = new QVBoxLayout(&page);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        pageLayout->setSpacing(8);
        auto* row = new SetupFit::FitRow;
        auto* left = new QVBoxLayout;
        auto* right = new QVBoxLayout;
        row->addLayout(left, 1);
        row->addLayout(right, 1);
        pageLayout->addLayout(row);
        auto* tall = new QFrame(&page);
        tall->setFixedHeight(120);
        tall->setMinimumWidth(100);
        left->addWidget(tall);
        left->addStretch();
        auto* shortOne = new QFrame(&page);
        shortOne->setMinimumSize(100, 40);
        shortOne->setSizePolicy(QSizePolicy::Preferred,
                                widgetAsksForHeight ? QSizePolicy::Expanding : QSizePolicy::Fixed);
        right->addWidget(shortOne);
        right->addStretch();
        auto* under = new QFrame(&page);
        under->setFixedHeight(50);
        pageLayout->addWidget(under);
        // A plain trailing stretch (factor 0), as HL2 Options ends.
        pageLayout->addStretch();
        page.resize(400, 400);
        page.show();
        QVERIFY(QTest::qWaitForWindowExposed(&page));
        settle();
        QVERIFY(!row->isStacked());
        const int gap = under->y() - (tall->geometry().bottom() + 1);
        if (widgetAsksForHeight) {
            QVERIFY2(gap > 8, qPrintable(QStringLiteral("the row did not grow: %1 px").arg(gap)));
            QVERIFY(shortOne->height() > 40);
        } else {
            QVERIFY2(gap == 8, qPrintable(QStringLiteral("%1 px between the row and the group under it; "
                                                         "the page's spacing is 8").arg(gap)));
            QCOMPARE(shortOne->height(), 40);
            QCOMPARE(shortOne->y(), tall->y());
        }
    }

    void calibrationGroupsSitTogether_data()
    {
        QTest::addColumn<bool>("workspace");
        QTest::newRow("today") << false;
        QTest::newRow("workspace") << true;
    }

    // Side by side, the Calibration tab's spare height is at the bottom of
    // the page (JJ, 2026-10-10): Volts/Amps Calibration sits under the two
    // columns at the page's spacing between groups, each group is as tall
    // as its content needs, and Level Cal ends under its progress bar
    // while it has no message to show.
    void calibrationGroupsSitTogether()
    {
        QFETCH(bool, workspace);
        // The sweep's font, as calibrationSettlesSideBySide() explains.
        const FontRestore restore;
        QApplication::setFont(m_startFont);
        AppLook look(workspace);
        RadioModel model;
        model.setBoardForTest(HPSDRHW::Saturn);
        SetupDialog dialog(&model);
        dialog.realizeAllPagesForTest();
        look.apply(&dialog);
        dialog.resize(kDefaultWidth, kDefaultHeight);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        settle();
        auto* hardware = dialog.findChild<HardwarePage*>();
        QVERIFY(hardware != nullptr);
        hardware->onCurrentRadioChanged(posedRadio(HPSDRHW::Saturn));
        auto* stack = dialog.findChild<QStackedWidget*>();
        QVERIFY(stack != nullptr);
        QWidget* leaf = dialog.realizePageAtForTest(
            static_cast<int>(dialog.pageLabelsForTest().indexOf(QStringLiteral("Hardware Config"))));
        QVERIFY(leaf != nullptr);
        stack->setCurrentWidget(leaf);
        settle();
        int calibration = -1;
        QTabWidget* tabs = tabsHolding(leaf, QStringLiteral("Calibration"), &calibration);
        QVERIFY(tabs != nullptr);
        tabs->setCurrentIndex(calibration);
        settle();
        const CalibrationState state = calibrationState(leaf);
        QVERIFY2(state.found && !state.stacked && !state.bar, qPrintable(state.text));

        QScrollArea* area = calibrationArea(leaf);
        QVERIFY(area != nullptr);
        QWidget* page = area->widget();
        QVERIFY(page != nullptr && page->layout() != nullptr);
        const auto group = [page](const QString& title) -> QGroupBox* {
            for (QGroupBox* g : page->findChildren<QGroupBox*>()) {
                if (g->title() == title) {
                    return g;
                }
            }
            return nullptr;
        };
        QGroupBox* freqCal = group(QStringLiteral("Freq Cal"));
        QGroupBox* levelCal = group(QStringLiteral("Level Cal"));
        QGroupBox* diagnostic = group(QStringLiteral("HPSDR Freq Cal Diagnostic"));
        QGroupBox* txDisplay = group(QStringLiteral("TX Display Cal"));
        QGroupBox* voltsAmps = group(QStringLiteral("Volts/Amps Calibration"));
        QVERIFY(freqCal && levelCal && diagnostic && txDisplay && voltsAmps);

        // Each group is as tall as its content needs at its width.
        const auto need = [](const QGroupBox* g) {
            return g->hasHeightForWidth() ? g->heightForWidth(g->width()) : g->sizeHint().height();
        };
        for (const QGroupBox* g : {freqCal, levelCal, diagnostic, txDisplay, voltsAmps}) {
            QVERIFY2(g->height() == need(g),
                     qPrintable(QStringLiteral("%1 is %2 px tall and needs %3")
                                    .arg(g->title()).arg(g->height()).arg(need(g))));
        }

        // Volts/Amps is under the taller column at the page's spacing.
        const int spacing = page->layout()->spacing();
        const int columnsEnd = std::max(levelCal->geometry().bottom(), txDisplay->geometry().bottom()) + 1;
        QVERIFY2(voltsAmps->y() - columnsEnd == spacing,
                 qPrintable(QStringLiteral("Volts/Amps is %1 px under the columns; the page's spacing is %2")
                                .arg(voltsAmps->y() - columnsEnd).arg(spacing)));
        // The shorter column's spare is under its last group: both
        // columns start at the same height and hold their groups at the
        // same distance.
        QCOMPARE(diagnostic->y(), freqCal->y());
        QCOMPARE(txDisplay->y() - (diagnostic->geometry().bottom() + 1),
                 levelCal->y() - (freqCal->geometry().bottom() + 1));
        // What is left of the page is under Volts/Amps.
        QVERIFY(voltsAmps->geometry().bottom() < page->height());

        // Level Cal ends under its progress bar: no empty row for a
        // message it does not have.
        auto* progress = levelCal->findChild<QWidget*>(QStringLiteral("levelCalProgressBar"));
        auto* status = levelCal->findChild<QLabel*>(QStringLiteral("levelCalStatusLabel"));
        QVERIFY(progress != nullptr && status != nullptr && levelCal->layout() != nullptr);
        QVERIFY(status->text().isEmpty());
        const int under = levelCal->layout()->contentsRect().bottom() - progress->geometry().bottom();
        QVERIFY2(under == 0, qPrintable(QStringLiteral("%1 px of empty space under the progress bar").arg(under)));
    }

private:
    QFont m_startFont;
};

QTEST_MAIN(TstSetupPagesFitWindow)
#include "tst_setup_pages_fit_window.moc"
