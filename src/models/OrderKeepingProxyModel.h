// SPDX-License-Identifier: GPL-3.0-or-later
//
// =================================================================
// src/models/OrderKeepingProxyModel.h  (NereusSDR)
// =================================================================
//
// NereusSDR-original; no upstream port.
//
// A QSortFilterProxyModel that can take a real model reset and come back
// with its rows in the order it showed before. A plain reset makes the
// proxy sort its rows again from scratch, and rows that tie on the sort key
// then come back in source order, while rows that arrived one by one were
// placed by arrival. The Spot List sorts on a key every spot ties on, so a
// plain reset would turn the list over. resetKeepingOrder() keeps the order
// the user sees; the filters and the sort column stay as they are.
//
// Used by the accessibility-safe refresh of the Spot List and the FreeDV
// Reporter station table (resetViewAfterRowChanges in
// gui/QtCocoaAccessibleTableCache.h). It also calls the view's flush
// (setBeforeRowChange) right before each change of its rows, outside any
// begin/end of its own.
//
// Modification history (NereusSDR):
//   2026-10-15  J.J. Boyd (KG4VCF)  Created. AI-assisted via Anthropic
//                                    Claude Code.
//   2026-10-15  J.J. Boyd (KG4VCF)  setBeforeRowChange and
//                                    prepareForRowChange: the flush runs
//                                    before the source's row changes reach
//                                    this proxy. AI-assisted via Anthropic
//                                    Claude Code.
//   2026-10-15  J.J. Boyd (KG4VCF)  setBeforeRowChange takes a gate: while
//                                    it is false the proxy does no extra
//                                    work at all. AI-assisted via
//                                    Anthropic Claude Code.

#pragma once

#include <QHash>
#include <QList>
#include <QMetaObject>
#include <QModelIndex>
#include <QSortFilterProxyModel>

#include <functional>
#include <utility>

namespace NereusSDR {

class OrderKeepingProxyModel : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    // Connects this proxy's hooks to the source before the proxy's own
    // handling, so the flush runs while neither model has begun a change:
    // the source is in its rowsAboutToBe* (it still shows its old rows) and
    // this proxy has not started to follow it. (On a removal Qt removes the
    // proxy rows from inside the source's rowsAboutToBeRemoved,
    // qsortfilterproxymodel.cpp _q_sourceRowsAboutToBeRemoved; on an insert
    // it inserts them at the source's rowsInserted.)
    void setSourceModel(QAbstractItemModel* source) override
    {
        if (source == sourceModel()) {
            return;
        }
        for (const QMetaObject::Connection& hook : std::as_const(m_sourceHooks)) {
            disconnect(hook);
        }
        m_sourceHooks.clear();
        if (source != nullptr) {
            const auto before = [this] { prepareForRowChange(); };
            m_sourceHooks = {
                connect(source, &QAbstractItemModel::rowsAboutToBeInserted, this, before),
                connect(source, &QAbstractItemModel::rowsAboutToBeRemoved, this, before),
                connect(source, &QAbstractItemModel::columnsAboutToBeInserted, this, before),
                connect(source, &QAbstractItemModel::columnsAboutToBeRemoved, this, before),
                connect(source, &QAbstractItemModel::dataChanged, this,
                        [this](const QModelIndex& topLeft, const QModelIndex& bottomRight) {
                            if (filterWillChangeRows(topLeft, bottomRight)) {
                                prepareForRowChange();
                            }
                        }),
            };
        }
        QSortFilterProxyModel::setSourceModel(source);
    }

    // Called right before each change of this proxy's rows (the view's
    // flush from resetViewAfterRowChanges). Subclasses that change their
    // filter call prepareForRowChange() before they change its state.
    // While active (when given) returns false, nothing is called and a
    // data change costs no extra filter pass (the gui side passes
    // itemViewResetsActive, so with accessibility off the proxy works as
    // a plain QSortFilterProxyModel).
    void setBeforeRowChange(std::function<void()> beforeRowChange,
                            std::function<bool()> active = {})
    {
        m_beforeRowChange = std::move(beforeRowChange);
        m_beforeRowChangeActive = std::move(active);
    }

    void prepareForRowChange()
    {
        if (beforeRowChangeActive()) {
            m_beforeRowChange();
        }
    }

    // A real reset (every index this proxy gave out becomes invalid) with
    // the same rows, in the same order.
    void resetKeepingOrder()
    {
        QHash<int, int> shownAt;
        const int rows = rowCount();
        shownAt.reserve(rows);
        for (int row = 0; row < rows; ++row) {
            shownAt.insert(mapToSource(index(row, 0)).row(), row);
        }
        // The views rebuild this proxy's rows from inside endResetModel, so
        // the order is held until it returns.
        m_shownAt = std::move(shownAt);
        m_keepingOrder = true;
        beginResetModel();
        endResetModel();
        static_cast<void>(rowCount());
        m_keepingOrder = false;
        m_shownAt.clear();
    }

protected:
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override
    {
        if (!m_keepingOrder) {
            return QSortFilterProxyModel::lessThan(left, right);
        }
        // Qt sorts descending with lessThan(right, left), so the comparison
        // turns over with the order to give the shown order both ways.
        const int l = m_shownAt.value(left.row(), -1);
        const int r = m_shownAt.value(right.row(), -1);
        return sortOrder() == Qt::AscendingOrder ? l < r : l > r;
    }

private:
    // A data change shows or hides rows here when a row's filter result no
    // longer matches whether it is shown (as Qt decides in
    // _q_sourceDataChanged with dynamic filtering); any other data change
    // moves no row.
    bool filterWillChangeRows(const QModelIndex& topLeft, const QModelIndex& bottomRight) const
    {
        if (!beforeRowChangeActive() || !dynamicSortFilter() || !topLeft.isValid()
            || sourceModel() == nullptr) {
            return false;
        }
        const QModelIndex parent = topLeft.parent();
        for (int row = topLeft.row(); row <= bottomRight.row(); ++row) {
            const bool shown = mapFromSource(sourceModel()->index(row, 0, parent)).isValid();
            if (shown != filterAcceptsRow(row, parent)) {
                return true;
            }
        }
        return false;
    }

    bool beforeRowChangeActive() const
    {
        return m_beforeRowChange && (!m_beforeRowChangeActive || m_beforeRowChangeActive());
    }

    QHash<int, int> m_shownAt;
    bool m_keepingOrder{false};
    std::function<void()> m_beforeRowChange;
    std::function<bool()> m_beforeRowChangeActive;
    QList<QMetaObject::Connection> m_sourceHooks;
};

} // namespace NereusSDR
