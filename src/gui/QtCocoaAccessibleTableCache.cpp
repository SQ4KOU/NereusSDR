// SPDX-License-Identifier: GPL-3.0-or-later
//
// NereusSDR - see QtCocoaAccessibleTableCache.h.
//
// Modification history (NereusSDR):
//   2026-10-08  J.J. Boyd / KG4VCF  Shared from the Spot List crash fix.
//                                    AI tooling: Anthropic Claude Code.
//   2026-10-15  J.J. Boyd (KG4VCF)  resetItemsBeforeRowChange for combos,
//                                    tables, lists and trees.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-10-15  J.J. Boyd (KG4VCF)  resetViewKeepingPlace and
//                                    resetViewAfterRowChanges; the
//                                    cache-only repair is removed.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-10-15  J.J. Boyd (KG4VCF)  Review fixes: macOS-only gate, flush
//                                    before the next row change, no reset
//                                    for data changes, double-click across
//                                    a reset, persistent-index place.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-10-15  J.J. Boyd (KG4VCF)  A data change resets once, at most
//                                    every kItemViewDataResetIntervalMs.
//                                    AI-assisted via Anthropic Claude Code.

#include "QtCocoaAccessibleTableCache.h"

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QAbstractProxyModel>
#include <QAccessible>
#include <QComboBox>
#include <QCoreApplication>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QMouseEvent>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QTimer>
#include <QTableWidget>
#include <QTreeWidget>

#include <algorithm>
#include <atomic>
#include <memory>
#include <utility>
#include <vector>

namespace NereusSDR {

namespace {

// Set by tests: the helpers act on every platform, not only macOS.
std::atomic<bool> g_everyPlatform{false};
// Set by tests: the data-change reset interval for views set up next.
std::atomic<int> g_dataResetIntervalMs{kItemViewDataResetIntervalMs};

} // namespace

bool itemViewResetsActive()
{
#if defined(Q_OS_MAC)
    const bool platform = true;
#else
    const bool platform = g_everyPlatform.load(std::memory_order_relaxed);
#endif
    return platform && QAccessible::isActive();
}

void setItemViewResetsOnEveryPlatformForTest(bool everyPlatform)
{
    g_everyPlatform.store(everyPlatform, std::memory_order_relaxed);
}

void setItemViewDataResetIntervalForTest(int ms)
{
    g_dataResetIntervalMs.store(ms > 0 ? ms : kItemViewDataResetIntervalMs,
                                std::memory_order_relaxed);
}

namespace {

// The (row, column) of each selected index, to select again after a reset.
QList<std::pair<int, int>> selectedCells(const QItemSelectionModel* selection)
{
    QList<std::pair<int, int>> cells;
    if (selection == nullptr) {
        return cells;
    }
    const QModelIndexList indexes = selection->selectedIndexes();
    for (const QModelIndex& index : indexes) {
        cells.append({index.row(), index.column()});
    }
    return cells;
}

// Selects the cells again and makes (currentRow, currentColumn) current,
// without the selection model announcing either (its signals are blocked by
// the caller).
void restoreSelection(QAbstractItemView* view, const QList<std::pair<int, int>>& cells,
                      int currentRow, int currentColumn)
{
    QItemSelectionModel* selection = view->selectionModel();
    QAbstractItemModel* model = view->model();
    if (selection == nullptr || model == nullptr) {
        return;
    }
    QItemSelection restored;
    for (const auto& [row, column] : cells) {
        const QModelIndex index = model->index(row, column, view->rootIndex());
        if (index.isValid()) {
            restored.select(index, index);
        }
    }
    if (!restored.isEmpty()) {
        selection->select(restored, QItemSelectionModel::Select);
    }
    const QModelIndex current = model->index(currentRow, currentColumn, view->rootIndex());
    if (current.isValid()) {
        selection->setCurrentIndex(current, QItemSelectionModel::NoUpdate);
    }
    view->viewport()->update();
}

// A tree item's view state, recorded in pre-order so a deep clone can take
// it back in the same order.
struct TreeRowState {
    bool expanded{false};
    bool hidden{false};
    bool spanned{false};
    bool selected{false};
    bool current{false};
};

void recordTreeRows(const QTreeWidgetItem* item, const QTreeWidgetItem* current,
                    std::vector<TreeRowState>& out)
{
    out.push_back({item->isExpanded(), item->isHidden(), item->isFirstColumnSpanned(),
                   item->isSelected(), item == current});
    for (int i = 0; i < item->childCount(); ++i) {
        recordTreeRows(item->child(i), current, out);
    }
}

void restoreTreeRows(QTreeWidgetItem* item, const std::vector<TreeRowState>& states,
                     std::size_t& at, QTreeWidgetItem*& current)
{
    if (at >= states.size()) {
        return;
    }
    const TreeRowState& state = states.at(at++);
    if (item->isFirstColumnSpanned() != state.spanned) {
        item->setFirstColumnSpanned(state.spanned);
    }
    if (item->isHidden() != state.hidden) {
        item->setHidden(state.hidden);
    }
    if (item->isExpanded() != state.expanded) {
        item->setExpanded(state.expanded);
    }
    if (state.selected) {
        item->setSelected(true);
    }
    if (state.current) {
        current = item;
    }
    for (int i = 0; i < item->childCount(); ++i) {
        restoreTreeRows(item->child(i), states, at, current);
    }
}

} // namespace

bool resetItemsBeforeRowChange(QComboBox* combo)
{
    if (combo == nullptr || !itemViewResetsActive()) {
        return false;
    }
    auto* model = qobject_cast<QStandardItemModel*>(combo->model());
    if (model == nullptr || combo->rootModelIndex().isValid()) {
        return false;
    }
    const QSignalBlocker comboBlock(combo);
    QLineEdit* const edit = combo->lineEdit();
    const QSignalBlocker editBlock(edit);
    const QString editText = edit != nullptr ? edit->text() : QString();
    const int current = combo->currentIndex();
    const int rows = model->rowCount();
    const int columns = model->columnCount();

    std::vector<std::vector<std::unique_ptr<QStandardItem>>> items(static_cast<std::size_t>(rows));
    for (int row = 0; row < rows; ++row) {
        auto& cells = items[static_cast<std::size_t>(row)];
        for (int column = 0; column < columns; ++column) {
            const QStandardItem* item = model->item(row, column);
            cells.emplace_back(item != nullptr ? item->clone() : nullptr);
        }
    }

    // The reset (qstandarditemmodel.cpp:2264-2275): the cells' indexes are
    // invalid before the popup list drops its accessibility cache.
    model->clear();
    model->setColumnCount(columns);
    for (auto& cells : items) {
        QList<QStandardItem*> row;
        for (auto& cell : cells) {
            row.append(cell.release());
        }
        model->appendRow(row);
    }
    combo->setCurrentIndex(current);
    if (edit != nullptr && edit->text() != editText) {
        edit->setText(editText);
    }
    return true;
}

bool resetItemsBeforeRowChange(QTableWidget* table)
{
    if (table == nullptr || !itemViewResetsActive()) {
        return false;
    }
    const QSignalBlocker tableBlock(table);
    QItemSelectionModel* const selection = table->selectionModel();
    const QSignalBlocker selectionBlock(selection);
    const QModelIndex current = table->currentIndex();
    const QList<std::pair<int, int>> selected = selectedCells(selection);
    // Items set back while sorting is on would move to their sorted rows.
    const bool sorting = table->isSortingEnabled();
    if (sorting) {
        table->setSortingEnabled(false);
    }

    const int rows = table->rowCount();
    const int columns = table->columnCount();
    std::vector<std::unique_ptr<QTableWidgetItem>> items;
    items.reserve(static_cast<std::size_t>(rows) * static_cast<std::size_t>(columns));
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            items.emplace_back(table->takeItem(row, column));
        }
    }
    // QTableWidget::clearContents is the reset (qtablewidget.cpp:768-779);
    // the row and column counts and the header items stay.
    table->clearContents();
    std::size_t at = 0;
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            if (std::unique_ptr<QTableWidgetItem>& item = items[at++]) {
                table->setItem(row, column, item.release());
            }
        }
    }

    if (sorting) {
        // Sorts again by the same column, which leaves the sorted rows as
        // they were.
        table->setSortingEnabled(true);
    }
    restoreSelection(table, selected, current.row(), current.column());
    return true;
}

bool resetItemsBeforeRowChange(QListWidget* list)
{
    if (list == nullptr || !itemViewResetsActive()) {
        return false;
    }
    const QSignalBlocker listBlock(list);
    QItemSelectionModel* const selection = list->selectionModel();
    const QSignalBlocker selectionBlock(selection);
    const QModelIndex current = list->currentIndex();
    const QList<std::pair<int, int>> selected = selectedCells(selection);
    const bool sorting = list->isSortingEnabled();
    list->setSortingEnabled(false);

    const int rows = list->count();
    QAbstractItemModel* const model = list->model();
    std::vector<bool> hidden;
    hidden.reserve(static_cast<std::size_t>(rows));
    // A list of plain text items (a log) goes back in one insert: one table
    // change for the view and the bridge instead of one per row.
    const QListWidgetItem plainItem;
    bool plain = true;
    QStringList texts;
    for (int row = 0; row < rows; ++row) {
        const QListWidgetItem* item = list->item(row);
        hidden.push_back(item->isHidden());
        if (plain) {
            const QMap<int, QVariant> roles = model->itemData(model->index(row, 0));
            const auto text = roles.constFind(Qt::DisplayRole);
            plain = item->type() == QListWidgetItem::Type && item->flags() == plainItem.flags()
                    && roles.size() == 1 && text != roles.constEnd()
                    && text->metaType() == QMetaType::fromType<QString>();
            if (plain) {
                texts.append(text->toString());
            }
        }
    }
    std::vector<std::unique_ptr<QListWidgetItem>> items;
    if (!plain) {
        items.reserve(static_cast<std::size_t>(rows));
        for (int row = 0; row < rows; ++row) {
            items.emplace_back(list->item(row)->clone());
        }
    }
    // QListWidget::clear is the reset (qlistwidget.cpp:36-48).
    list->clear();
    if (plain) {
        list->addItems(texts);
    } else {
        for (std::unique_ptr<QListWidgetItem>& item : items) {
            list->addItem(item.release());
        }
    }
    for (int row = 0; row < rows; ++row) {
        if (hidden[static_cast<std::size_t>(row)]) {
            list->item(row)->setHidden(true);
        }
    }

    list->setSortingEnabled(sorting);
    restoreSelection(list, selected, current.row(), current.column());
    return true;
}

bool resetItemsBeforeRowChange(QTreeWidget* tree)
{
    if (tree == nullptr || !itemViewResetsActive()) {
        return false;
    }
    const QSignalBlocker treeBlock(tree);
    QItemSelectionModel* const selection = tree->selectionModel();
    const QSignalBlocker selectionBlock(selection);
    const bool sorting = tree->isSortingEnabled();
    if (sorting) {
        tree->setSortingEnabled(false);
    }

    const QTreeWidgetItem* const currentItem = tree->currentItem();
    const int currentColumn = tree->currentColumn();
    std::vector<TreeRowState> states;
    std::vector<std::unique_ptr<QTreeWidgetItem>> items;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        const QTreeWidgetItem* item = tree->topLevelItem(i);
        recordTreeRows(item, currentItem, states);
        items.emplace_back(item->clone());
    }
    // QTreeWidget::clear is the reset (qtreewidget.cpp:120-133).
    tree->clear();
    QList<QTreeWidgetItem*> topLevel;
    for (std::unique_ptr<QTreeWidgetItem>& item : items) {
        topLevel.append(item.release());
    }
    tree->addTopLevelItems(topLevel);

    std::size_t at = 0;
    QTreeWidgetItem* current = nullptr;
    for (QTreeWidgetItem* item : std::as_const(topLevel)) {
        restoreTreeRows(item, states, at, current);
    }
    if (current != nullptr) {
        tree->setCurrentItem(current, std::max(currentColumn, 0), QItemSelectionModel::NoUpdate);
    }
    if (sorting) {
        tree->setSortingEnabled(true);
    }
    tree->viewport()->update();
    return true;
}

namespace {

// A row of the view's model, known by the matching index in the model below
// every proxy: a reset of the proxies leaves it valid, and it follows its
// row through the source model's own inserts and removals.
QPersistentModelIndex identityOf(const QModelIndex& index)
{
    QModelIndex at = index.siblingAtColumn(0);
    while (at.isValid()) {
        const auto* proxy = qobject_cast<const QAbstractProxyModel*>(at.model());
        if (proxy == nullptr) {
            break;
        }
        at = proxy->mapToSource(at);
    }
    return QPersistentModelIndex(at);
}

// The row of `model` (the view's model, or a proxy chain above the bottom
// model) that shows `bottom`, or an invalid index when it is not shown.
QModelIndex shownIndexOf(const QAbstractItemModel* model, const QModelIndex& bottom)
{
    if (model == nullptr || !bottom.isValid()) {
        return {};
    }
    if (bottom.model() == model) {
        return bottom;
    }
    const auto* proxy = qobject_cast<const QAbstractProxyModel*>(model);
    if (proxy == nullptr) {
        return {};
    }
    const QModelIndex source = shownIndexOf(proxy->sourceModel(), bottom);
    return source.isValid() ? proxy->mapFromSource(source) : QModelIndex();
}

// A cell of the view, kept across a reset: its row's identity and column.
struct KeptCell {
    QPersistentModelIndex row;
    int column{0};
};

KeptCell keep(const QModelIndex& index)
{
    return {identityOf(index), index.column()};
}

QModelIndex shownCell(const QAbstractItemModel* model, const KeptCell& cell)
{
    const QModelIndex row = shownIndexOf(model, cell.row);
    return row.isValid() ? row.siblingAtColumn(cell.column) : QModelIndex();
}

// The row at the top of the view's viewport, with how far its top sits
// above the viewport's top edge, and the horizontal scroll.
struct TopPlace {
    KeptCell cell;
    int offset{0};
    int horizontal{0};
};

TopPlace topPlaceOf(const QAbstractItemView* view)
{
    TopPlace place;
    place.horizontal = view->horizontalScrollBar()->value();
    const QModelIndex top = view->indexAt(QPoint(0, 0));
    if (top.isValid()) {
        place.cell = keep(top);
        place.offset = view->visualRect(top).top();
    }
    return place;
}

bool resetKeepingPlace(QAbstractItemView* view, const std::function<void()>& resetModel,
                       const TopPlace& topPlace)
{
    QAbstractItemModel* const model = view->model();

    // The selection and current item as they are now: the model's own row
    // signals have already moved them with their rows.
    QItemSelectionModel* const selection = view->selectionModel();
    QList<KeptCell> selected;
    if (selection != nullptr) {
        const QModelIndexList indexes = selection->selectedIndexes();
        selected.reserve(indexes.size());
        for (const QModelIndex& index : indexes) {
            selected.append(keep(index));
        }
    }
    const QModelIndex current = view->currentIndex();
    const KeptCell currentCell = current.isValid() ? keep(current) : KeptCell{};

    {
        const QSignalBlocker viewBlock(view);
        const QSignalBlocker selectionBlock(selection);
        resetModel();

        if (selection != nullptr) {
            QItemSelection restored;
            for (const KeptCell& cell : std::as_const(selected)) {
                const QModelIndex index = shownCell(model, cell);
                if (index.isValid()) {
                    restored.select(index, index);
                }
            }
            if (!restored.isEmpty()) {
                selection->select(restored, QItemSelectionModel::Select);
            }
            const QModelIndex restoredCurrent = shownCell(model, currentCell);
            if (restoredCurrent.isValid()) {
                selection->setCurrentIndex(restoredCurrent, QItemSelectionModel::NoUpdate);
            }
        }
    }

    // The scroll range follows the new row count, then the same row goes
    // back to the top at the same offset. When that row is gone, the
    // scroll value stays as it is.
    view->doItemsLayout();
    // The column that was at the left edge: shown, so scrollTo acts.
    const QModelIndex top = shownCell(model, topPlace.cell);
    if (top.isValid()) {
        view->scrollTo(top, QAbstractItemView::PositionAtTop);
        if (view->verticalScrollMode() == QAbstractItemView::ScrollPerPixel) {
            QScrollBar* const vbar = view->verticalScrollBar();
            vbar->setValue(vbar->value() + view->visualRect(top).top() - topPlace.offset);
        }
    }
    view->horizontalScrollBar()->setValue(topPlace.horizontal);
    view->viewport()->update();
    return true;
}

} // namespace

bool resetViewKeepingPlace(QAbstractItemView* view, const std::function<void()>& resetModel)
{
    if (view == nullptr || !resetModel || !itemViewResetsActive() || view->model() == nullptr) {
        return false;
    }
    return resetKeepingPlace(view, resetModel, topPlaceOf(view));
}

namespace {

// Watches one view's accessibility cache (stale after a table change the
// bridge hears of, clean after a reset), flushes it on request, queues one
// reset per event-loop pass after row changes, resets at most once per
// interval after data changes, and keeps a double-click whose press came
// before a reset.
class RowChangeReset : public QObject {
public:
    RowChangeReset(QAbstractItemView* view, std::function<void()> resetModel)
        : QObject(view), m_view(view), m_resetModel(std::move(resetModel))
    {
        m_view->viewport()->installEventFilter(this);
        m_dataTimer.setSingleShot(true);
        m_dataTimer.setInterval(g_dataResetIntervalMs.load(std::memory_order_relaxed));
        QObject::connect(&m_dataTimer, &QTimer::timeout, this, [this] { runDataReset(); });
    }

    // Before rows move: note the top row once per pass.
    void noteTop()
    {
        if (m_topNoted || !itemViewResetsActive()) {
            return;
        }
        m_top = topPlaceOf(m_view);
        m_topNoted = true;
    }

    // Rows inserted or removed: the cache is stale, and one reset is queued
    // for this pass.
    void rowsChanged()
    {
        m_stale = true;
        if (m_queued || !itemViewResetsActive()) {
            return;
        }
        noteTop();
        m_queued = true;
        QMetaObject::invokeMethod(this, [this] { runQueued(); }, Qt::QueuedConnection);
    }

    // Data changed: the bridge releases its native rows all the same
    // (qcocoaaccessibility.mm:78-80), so the cache is stale, and
    // QAccessibleTable answers a client's next query for a drained cell
    // from its cache, with nothing (itemviews.cpp:593-595). Its own
    // handling reads no cell (itemviews.cpp:745-747), so nothing resets
    // now: one reset follows within the interval, which later data
    // changes do not push back. The next row change still flushes first.
    void dataChanged()
    {
        m_stale = true;
        if (m_dataTimer.isActive() || !itemViewResetsActive()) {
            return;
        }
        m_dataTimer.start();
    }

    // The view announced a reset (a model reset, a layout change or a
    // move): its accessibility cache is empty.
    void cacheCleared() { m_stale = false; }

    // Right before a row change, outside any begin/end of the view's model.
    void flush()
    {
        if (!m_stale || m_resetting || !itemViewResetsActive() || m_view->model() == nullptr) {
            return;
        }
        reset();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::MouseButtonPress) {
            const auto* mouse = static_cast<const QMouseEvent*>(event);
            const QModelIndex pressed = m_view->indexAt(mouse->position().toPoint());
            m_pressed = itemViewResetsActive() && pressed.isValid() ? keep(pressed) : KeptCell{};
            m_resetSincePress = false;
        } else if (event->type() == QEvent::MouseButtonDblClick && m_resetSincePress) {
            m_resetSincePress = false;
            // The reset dropped the view's pressed index, and Qt treats a
            // double-click off the pressed index as a press
            // (qabstractitemview.cpp:2034-2046). The same cell is pressed
            // again here, as it was, so the double-click goes on as usual.
            const auto* mouse = static_cast<const QMouseEvent*>(event);
            const QModelIndex at = m_view->indexAt(mouse->position().toPoint());
            if (at.isValid() && m_pressed.row.isValid() && at.column() == m_pressed.column
                && identityOf(at) == m_pressed.row) {
                QMouseEvent press(QEvent::MouseButtonPress, mouse->position(),
                                  mouse->scenePosition(), mouse->globalPosition(),
                                  mouse->button(), mouse->buttons(), mouse->modifiers(),
                                  mouse->pointingDevice());
                QCoreApplication::sendEvent(watched, &press);
            }
        }
        return QObject::eventFilter(watched, event);
    }

private:
    void runQueued()
    {
        m_queued = false;
        if (m_stale && itemViewResetsActive() && m_view->model() != nullptr) {
            reset();
        } else {
            m_topNoted = false;
        }
    }

    void runDataReset()
    {
        if (m_stale && !m_resetting && itemViewResetsActive() && m_view->model() != nullptr) {
            reset();
        }
    }

    void reset()
    {
        m_resetting = true;
        const TopPlace top = m_topNoted ? m_top : topPlaceOf(m_view);
        m_topNoted = false;
        resetKeepingPlace(m_view, m_resetModel, top);
        m_stale = false;
        m_resetSincePress = true;
        m_resetting = false;
    }

    QAbstractItemView* m_view;
    std::function<void()> m_resetModel;
    QTimer m_dataTimer;
    TopPlace m_top;
    KeptCell m_pressed;
    bool m_topNoted{false};
    bool m_queued{false};
    bool m_stale{false};
    bool m_resetting{false};
    bool m_resetSincePress{false};
};

} // namespace

std::function<void()> resetViewAfterRowChanges(QAbstractItemView* view,
                                               std::function<void()> resetModel)
{
    if (view == nullptr || view->model() == nullptr || !resetModel) {
        return [] {};
    }
    QAbstractItemModel* const model = view->model();
    auto* reset = new RowChangeReset(view, std::move(resetModel));
    const auto noteTop = [reset] { reset->noteTop(); };
    const auto rowsChanged = [reset] { reset->rowsChanged(); };
    const auto dataChanged = [reset] { reset->dataChanged(); };
    const auto cacheCleared = [reset] { reset->cacheCleared(); };
    // The reset object is the context: the connections end with the view.
    // They are made after the view's own, so the view has announced each
    // change to accessibility before these run.
    QObject::connect(model, &QAbstractItemModel::rowsAboutToBeInserted, reset, noteTop);
    QObject::connect(model, &QAbstractItemModel::rowsAboutToBeRemoved, reset, noteTop);
    QObject::connect(model, &QAbstractItemModel::columnsAboutToBeInserted, reset, noteTop);
    QObject::connect(model, &QAbstractItemModel::columnsAboutToBeRemoved, reset, noteTop);
    QObject::connect(model, &QAbstractItemModel::rowsInserted, reset, rowsChanged);
    QObject::connect(model, &QAbstractItemModel::rowsRemoved, reset, rowsChanged);
    QObject::connect(model, &QAbstractItemModel::columnsInserted, reset, rowsChanged);
    QObject::connect(model, &QAbstractItemModel::columnsRemoved, reset, rowsChanged);
    QObject::connect(model, &QAbstractItemModel::dataChanged, reset, dataChanged);
    QObject::connect(model, &QAbstractItemModel::modelReset, reset, cacheCleared);
    QObject::connect(model, &QAbstractItemModel::layoutChanged, reset, cacheCleared);
    QObject::connect(model, &QAbstractItemModel::rowsMoved, reset, cacheCleared);
    QObject::connect(model, &QAbstractItemModel::columnsMoved, reset, cacheCleared);
    return [guard = QPointer<RowChangeReset>(reset)] {
        if (guard) {
            guard->flush();
        }
    };
}

} // namespace NereusSDR
