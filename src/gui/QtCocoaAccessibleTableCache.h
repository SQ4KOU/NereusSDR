// SPDX-License-Identifier: GPL-3.0-or-later
//
// NereusSDR - accessibility-safe row changes for item views whose rows
// change while the app runs. NereusSDR-native; studies qtbase v6.11.0 (see
// docs/attribution/QT-PROVENANCE.md), no Qt code copied.
//
// Modification history (NereusSDR):
//   2026-10-08  J.J. Boyd / KG4VCF  Shared from the Spot List crash fix so
//                                    every live table, list, tree and
//                                    combo popup takes the same repair.
//                                    AI tooling: Anthropic Claude Code.
//   2026-10-15  J.J. Boyd (KG4VCF)  resetItemsBeforeRowChange: a real model
//                                    reset, kept items and all, before a
//                                    view inserts or removes rows, so
//                                    accessibility cannot read a deleted
//                                    cell. Promoted from DeviceCard's
//                                    popup repair; every platform and Qt.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-10-15  J.J. Boyd (KG4VCF)  resetViewKeepingPlace and
//                                    resetViewAfterRowChanges for the Spot
//                                    List and FreeDV Reporter tables; the
//                                    cache-only repair is removed.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-10-15  J.J. Boyd (KG4VCF)  Review fixes: macOS only (any Qt),
//                                    with accessibility active; a pending
//                                    reset flushed before the next row
//                                    change; no reset for data or layout
//                                    changes; double-click kept across a
//                                    reset; the place kept by a persistent
//                                    index into the model below the
//                                    proxies. AI-assisted via Anthropic
//                                    Claude Code.
//   2026-10-15  J.J. Boyd (KG4VCF)  Data changes reset again, at most
//                                    once a second per view, so an
//                                    assistive client never keeps reading
//                                    drained cells. AI-assisted via
//                                    Anthropic Claude Code.

#pragma once

#include "gui/NereusGuiExport.h"

#include <functional>

class QAbstractItemView;
class QComboBox;
class QListWidget;
class QTableWidget;
class QTreeWidget;

namespace NereusSDR {

// With accessibility on (VoiceOver, or any accessibility query), Qt's
// QAccessibleTable caches the cell interfaces of a view's rows, and the
// macOS bridge deletes those interfaces when its own native rows expire:
// every table change it hears of (rows, data or a reset) releases the
// native rows it held (qcocoaaccessibility.mm:78-80, updateTableModel in
// qcocoaaccessibilityelement.mm:342-360), and the cells go when the
// autorelease pool drains. The next row insertion or removal walks the
// cached ids and reads a deleted cell (Qt 6.11 QAccessibleTable::modelChange,
// itemviews.cpp:645-741). Clearing only that cache is not safe either:
// deleting still-valid cells lets the bridge delete an interface again from
// inside the deletion (qaccessiblecache.cpp:193-208; DeviceCard native
// report, 2026-10-04). A real model reset is safe: the model invalidates
// every cell's index first, then the view drops its whole accessibility
// cache, and rows inserted or removed right after it find the cache empty.
//
// The bridge is macOS only, so every helper here acts only on macOS (any Qt
// version) and only while accessibility is active. Anywhere else each one
// is a pure pass-through: it changes nothing and returns false.
NEREUS_GUI_EXPORT bool itemViewResetsActive();
// Test only: lets the helpers act on every platform (still only while
// accessibility is active), so the offscreen tests cover them on Linux CI.
NEREUS_GUI_EXPORT void setItemViewResetsOnEveryPlatformForTest(bool everyPlatform);

// resetItemsBeforeRowChange gives the view that reset with the same
// content, so call it right before the code that inserts or removes rows,
// and leave that code as it is. Kept through the reset: every row, every
// item with all its roles and flags, the current index and the selection
// (and a combo's edit text; a tree's expanded, hidden and spanned rows).
// The widget and its selection model are blocked while it runs, so no
// currentIndexChanged, activated, itemChanged or selection signal comes
// from it; the model itself announces the reset and the restored rows.
// Returns true when it reset the model (itemViewResetsActive()); otherwise
// it does nothing.
//
// A combo: its model must be the default QStandardItemModel at the top
// level, as every combo in the app has; otherwise nothing is done.
NEREUS_GUI_EXPORT bool resetItemsBeforeRowChange(QComboBox* combo);
// A table keeps its QTableWidgetItem objects. Cell widgets are not kept:
// Qt's reset releases them, so a caller that has any rebuilds them when
// this returns true.
NEREUS_GUI_EXPORT bool resetItemsBeforeRowChange(QTableWidget* table);
// A list or tree keeps copies (QListWidgetItem::clone,
// QTreeWidgetItem::clone): the old item pointers are gone. A list whose
// items are all plain text (a log) is put back in one insert.
NEREUS_GUI_EXPORT bool resetItemsBeforeRowChange(QListWidget* list);
NEREUS_GUI_EXPORT bool resetItemsBeforeRowChange(QTreeWidget* tree);

// A view over a proxy of the app's own model (the Spot List and FreeDV
// Reporter stations). Rows there change from the model's own signals, not
// from code that can call a reset first. The view's model must be a
// QAbstractProxyModel (or a chain of them): rows are known by a persistent
// index into the model below every proxy, which the reset leaves alone, so
// the place survives it and two rows with the same text stay apart.
//
// A real reset of the view's model through resetModel (which must reset
// that model, keeping its rows and their order, for example
// OrderKeepingProxyModel::resetKeepingOrder), then the selection, the
// current item and the scroll position (the same top row, at the same
// offset) put back. The view and its selection model are blocked
// meanwhile, so no selection or current signal comes from it. Returns true
// when it reset (itemViewResetsActive()).
NEREUS_GUI_EXPORT bool resetViewKeepingPlace(QAbstractItemView* view,
                                             const std::function<void()>& resetModel);

// Call once after the view has its model. While itemViewResetsActive():
//
// - Any table change the view announces (rows inserted or removed, data
//   changed) marks the accessibility cache stale: the bridge drains the
//   cells for each, and QAccessibleTable answers later queries for a
//   drained cell from its cache, with nothing (itemviews.cpp:593-595).
//   A reset, a layout change
//   or a move (which the view announces as a reset, qabstractitemview.cpp:
//   3835-3848) marks it clean.
// - The returned function is the flush: called right before the next row
//   change, outside any begin/end of the view's model, it resets at once
//   when the cache is stale, so no row change ever reads a cell the pool
//   drained, however the drain falls between events. Wire it with
//   OrderKeepingProxyModel::setBeforeRowChange.
// - A row insert or removal also queues one reset for the next event-loop
//   pass, so an assistive client walking the table meanwhile is answered
//   from fresh cells.
// - A data change starts one reset kItemViewDataResetIntervalMs later
//   (a single-shot timer per view, not restarted by later data changes),
//   so a steady stream of updates resets at most once per interval and
//   an assistive client's cells come back within it.
// - A double-click whose press came before a reset still double-clicks the
//   row pressed (the reset dropped the view's pressed index, which would
//   turn it into a press: qabstractitemview.cpp:2034-2046).
//
// The top row put back is the one shown before the first change of a pass,
// so a row landing above does not push the user's place down. The model's
// own signals go out as before.
[[nodiscard]] NEREUS_GUI_EXPORT std::function<void()>
resetViewAfterRowChanges(QAbstractItemView* view, std::function<void()> resetModel);

// How long after the first data change of a quiet view its reset comes.
// About a second: VoiceOver's place moves at most that often under a
// stream of FreeDV reports or bearing updates.
inline constexpr int kItemViewDataResetIntervalMs = 1000;
// Test only: another interval for the views set up after this call (0 or
// less puts kItemViewDataResetIntervalMs back).
NEREUS_GUI_EXPORT void setItemViewDataResetIntervalForTest(int ms);

} // namespace NereusSDR
