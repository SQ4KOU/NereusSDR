// SPDX-License-Identifier: GPL-3.0-or-later
//
// no-port-check: Test uses callsign-shaped fixture strings only.
//
// Spot List under live spot traffic with accessibility on.
//
// The macOS bridge deletes the cell interfaces it holds when its
// autorelease pool drains after a table change
// (qcocoaaccessibilityelement.mm:219-226,257-267,342-362), while the view's
// QAccessibleTable keeps their IDs; the next RowsInserted or RowsRemoved
// then reads them (itemviews.cpp:645-741). A spot arriving from the Core
// crashed inside SpotTableModel::addSpot.
//
// On macOS with accessibility on (tests turn it on for every platform),
// SpotHubDialog resets the proxy for real (resetViewAfterRowChanges) right
// before a row change whenever a change since the last reset left the
// accessibility cache stale, and once more after each burst of row changes.
// Offscreen there is no bridge, so each crash case walks the cells, changes
// rows, deletes the walked cells itself through
// QAccessible::deleteAccessibleInterface (what the drain does), then
// changes rows again. The drain can fall before the queued reset runs (a
// row change inside one event, the pool drained after it, another event
// first), so the timing cases delete the cells before the event loop runs.
// Without the reset that last change crashes.
//
// The place cases check what the user sees: the selected spot, the current
// spot and the top visible spot stay on the same spot across a new spot, a
// burst of spots, a trimmed spot and an updated row; two spots that read
// the same stay apart; a double-click to tune survives a reset between its
// press and its second click. Data updates reset once per interval
// (kItemViewDataResetIntervalMs), after which an assistive client's cells
// are back. With accessibility off nothing is reset, exactly as before.

#include <QtTest>
#include <QAccessible>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTableView>

#include "ExpiredAccessibleCells.h"
#include "core/DxClusterClient.h"
#include "core/DxSpot.h"
#include "core/DxccColorProvider.h"
#include "core/FreeDVReporterClient.h"
#include "core/PotaClient.h"
#include "core/PskReporterClient.h"
#include "core/SpotCollectorClient.h"
#include "core/WsjtxClient.h"
#include "gui/SpotHubDialog.h"
#include "models/SpotModel.h"
#include "models/SpotTableModel.h"

#include <memory>

using namespace NereusSDR;
namespace Cells = NereusSDR::ExpiredAccessibleCells;

namespace {

// Spot n is heard at minute n, so the list (newest first) is in a known
// order and every spot's key is its own.
DxSpot makeSpot(int n, const QString& source = QStringLiteral("Cluster"))
{
    DxSpot s;
    s.dxCall = QStringLiteral("K%1ABC").arg(n);
    s.freqMhz = 14.000 + 0.001 * n;
    s.spotterCall = QStringLiteral("TEST");
    s.comment = QStringLiteral("fixture");
    s.utcTime = QTime(0, 0).addSecs(60 * n);
    s.source = source;
    return s;
}

// The clients, models and dialog the app wires together, owned here.
struct SpotListFixture {
    DxClusterClient cluster;
    DxClusterClient rbn;
    WsjtxClient wsjtx;
    SpotCollectorClient collector;
    PotaClient pota;
    FreeDVReporterClient freedv;
    PskReporterClient psk;
    SpotModel spots;
    SpotTableModel table;
    DxccColorProvider dxcc;
    std::unique_ptr<SpotHubDialog> dialog;
    QTableView* view{nullptr};

    SpotListFixture()
    {
        dialog = std::make_unique<SpotHubDialog>(&cluster, &rbn, &wsjtx, &collector, &pota,
                                                 &freedv, &psk, &spots, &table, &dxcc);
        view = dialog->findChild<QTableView*>(QStringLiteral("spotListTable"));
    }

    void addSpots(int from, int to)
    {
        for (int n = from; n <= to; ++n) {
            table.addSpot(makeSpot(n));
        }
    }

    QString callAt(int row) const
    {
        return view->model()->index(row, SpotTableModel::ColDxCall).data().toString();
    }

    int rowOf(const QString& call) const
    {
        for (int row = 0; row < view->model()->rowCount(); ++row) {
            if (callAt(row) == call) {
                return row;
            }
        }
        return -1;
    }

    QString selectedCall() const
    {
        const QModelIndexList rows = view->selectionModel()->selectedRows();
        return rows.size() == 1 ? callAt(rows.first().row()) : QString();
    }

    QString currentCall() const
    {
        const QModelIndex current = view->currentIndex();
        return current.isValid() ? callAt(current.row()) : QString();
    }

    QString topCall() const
    {
        const QModelIndex top = view->indexAt(QPoint(0, 0));
        return top.isValid() ? callAt(top.row()) : QString();
    }

    // Shows the list scrolled to `topSpot` with `selectedSpot` selected and
    // current.
    void placeOn(const QString& topSpot, const QString& selectedSpot)
    {
        dialog->resize(900, 600);
        dialog->show();
        QCoreApplication::processEvents();
        const QModelIndex selected = view->model()->index(rowOf(selectedSpot), SpotTableModel::ColDxCall);
        view->selectionModel()->setCurrentIndex(
            selected, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        view->scrollTo(view->model()->index(rowOf(topSpot), 0), QAbstractItemView::PositionAtTop);
        QCoreApplication::processEvents();
    }
};

// Text of the accessible cell at (row, column) of the view.
QString accessibleCellText(QTableView* view, int row, int column)
{
    QAccessibleInterface* table = QAccessible::queryAccessibleInterface(view);
    if (table == nullptr || table->tableInterface() == nullptr) {
        return {};
    }
    QAccessibleInterface* cell = table->tableInterface()->cellAt(row, column);
    return cell != nullptr ? cell->text(QAccessible::Name) : QString();
}

} // namespace

class TestSpotListAccessibility : public QObject {
    Q_OBJECT

private slots:
    void cleanup() { Cells::setActive(false); }

    // The crash in the field: a spot arrives after the bridge released the
    // cells it held at the previous spot.
    void spotArrivesAfterBridgeDrain()
    {
        Cells::ActiveScope active;
        QVERIFY2(QAccessible::isActive(), "accessibility must be active for this case");
        SpotListFixture f;
        QVERIFY(f.view);
        f.dialog->resize(900, 600);
        f.dialog->show();
        f.addSpots(1, 2);
        QCoreApplication::processEvents();

        const QList<QAccessible::Id> walked = Cells::walk(f.view);
        QVERIFY(!walked.isEmpty());
        f.table.addSpot(makeSpot(3));
        QCoreApplication::processEvents();
        Cells::drop(walked);
        f.table.addSpot(makeSpot(4));
        QCoreApplication::processEvents();

        QCOMPARE(f.view->model()->rowCount(), 4);
        QCOMPARE(accessibleCellText(f.view, f.rowOf(QStringLiteral("K4ABC")),
                                    SpotTableModel::ColDxCall),
                 QStringLiteral("K4ABC"));
    }

    // The drain falls between a spot and the queued reset: the next spot,
    // in the same event-loop pass, must not read the drained cells.
    void spotAfterDrainBeforeQueuedReset()
    {
        Cells::ActiveScope active;
        SpotListFixture f;
        QVERIFY(f.view);
        f.dialog->resize(900, 600);
        f.dialog->show();
        f.addSpots(1, 2);
        QCoreApplication::processEvents();

        const QList<QAccessible::Id> walked = Cells::walk(f.view);
        QVERIFY(!walked.isEmpty());
        f.table.addSpot(makeSpot(3));
        Cells::drop(walked);
        f.table.addSpot(makeSpot(4));
        QCoreApplication::processEvents();

        QCOMPARE(f.view->model()->rowCount(), 4);
        QCOMPARE(accessibleCellText(f.view, f.rowOf(QStringLiteral("K4ABC")),
                                    SpotTableModel::ColDxCall),
                 QStringLiteral("K4ABC"));
    }

    // The same with the cap trimming a row, and after a data update alone
    // (the bridge drops its cells for that too, though nothing resets).
    void trimAndDataUpdateAfterDrainBeforeQueuedReset()
    {
        Cells::ActiveScope active;
        SpotListFixture f;
        QVERIFY(f.view);
        f.table.setMaxSpots(3);
        f.addSpots(1, 3);
        QCoreApplication::processEvents();

        QList<QAccessible::Id> walked = Cells::walk(f.view);
        QVERIFY(!walked.isEmpty());
        f.table.addSpot(makeSpot(4));
        Cells::drop(walked);
        f.table.addSpot(makeSpot(5));
        QCoreApplication::processEvents();
        QCOMPARE(f.view->model()->rowCount(), 3);

        walked = Cells::walk(f.view);
        QVERIFY(!walked.isEmpty());
        f.table.setBearingResolver([](const QString&) { return 42.0; });
        QCoreApplication::processEvents();
        Cells::drop(walked);
        f.table.addSpot(makeSpot(6));
        QCoreApplication::processEvents();
        QCOMPARE(f.view->model()->rowCount(), 3);
        QVERIFY(f.rowOf(QStringLiteral("K6ABC")) >= 0);
    }

    // The 500-spot cap trims the oldest row: RowsRemoved walks the cache too.
    void trimmedSpotAfterBridgeDrain()
    {
        Cells::ActiveScope active;
        SpotListFixture f;
        QVERIFY(f.view);
        f.table.setMaxSpots(2);
        f.addSpots(1, 2);
        QCoreApplication::processEvents();

        const QList<QAccessible::Id> walked = Cells::walk(f.view);
        QVERIFY(!walked.isEmpty());
        f.table.addSpot(makeSpot(3));
        QCoreApplication::processEvents();
        Cells::drop(walked);
        f.table.addSpot(makeSpot(4));
        QCoreApplication::processEvents();

        QCOMPARE(f.view->model()->rowCount(), 2);
        QVERIFY(f.rowOf(QStringLiteral("K4ABC")) >= 0);
        QVERIFY(f.rowOf(QStringLiteral("K3ABC")) >= 0);
    }

    // A spot (or a data update) and a band or source pill change in the
    // same event, the cells drained between them: the pill's row change
    // must not read them.
    void pillChangesAfterDrainSameEvent()
    {
        Cells::ActiveScope active;
        SpotListFixture f;
        QVERIFY(f.view);
        f.dialog->resize(900, 600);
        f.dialog->show();
        f.table.addSpot(makeSpot(1));
        DxSpot forty = makeSpot(2);
        forty.freqMhz = 7.030;
        f.table.addSpot(forty);
        DxSpot rbn = makeSpot(3, QStringLiteral("RBN"));
        f.table.addSpot(rbn);
        auto* band = f.dialog->findChild<QPushButton*>(QStringLiteral("spotListBandPill_20m"));
        auto* source = f.dialog->findChild<QPushButton*>(QStringLiteral("spotListSourcePill_RBN"));
        QVERIFY(band);
        QVERIFY(source);
        const bool bandWas = band->isChecked();
        const bool sourceWas = source->isChecked();
        band->setChecked(true);
        source->setChecked(true);
        QCoreApplication::processEvents();
        QCOMPARE(f.view->model()->rowCount(), 3);

        // A spot, the drain, the band pill hides the 20 m rows.
        QList<QAccessible::Id> walked = Cells::walk(f.view);
        QVERIFY(!walked.isEmpty());
        f.table.addSpot(makeSpot(4));
        Cells::drop(walked);
        band->setChecked(false);
        QCOMPARE(f.view->model()->rowCount(), 1);

        // A spot, the drain, the band pill shows them again.
        walked = Cells::walk(f.view);
        QVERIFY(!walked.isEmpty());
        DxSpot forty2 = makeSpot(5);
        forty2.freqMhz = 7.031;
        f.table.addSpot(forty2);
        Cells::drop(walked);
        band->setChecked(true);
        QCOMPARE(f.view->model()->rowCount(), 5);

        // A data update, the drain, the source pill hides the RBN spot.
        walked = Cells::walk(f.view);
        QVERIFY(!walked.isEmpty());
        f.table.setBearingResolver([](const QString&) { return 42.0; });
        Cells::drop(walked);
        source->setChecked(false);
        QCOMPARE(f.view->model()->rowCount(), 4);

        // A spot, the drain, the source pill shows it again.
        walked = Cells::walk(f.view);
        QVERIFY(!walked.isEmpty());
        f.table.addSpot(makeSpot(6));
        Cells::drop(walked);
        source->setChecked(true);
        QCOMPARE(f.view->model()->rowCount(), 6);

        QCoreApplication::processEvents();
        const int row = f.rowOf(QStringLiteral("K3ABC"));
        QVERIFY(row >= 0);
        QCOMPARE(accessibleCellText(f.view, row, SpotTableModel::ColDxCall),
                 QStringLiteral("K3ABC"));
        band->setChecked(bandWas);
        source->setChecked(sourceWas);
    }

    // A band pill hides and shows rows through the proxy.
    void bandFilterAfterBridgeDrain()
    {
        Cells::ActiveScope active;
        SpotListFixture f;
        QVERIFY(f.view);
        f.table.addSpot(makeSpot(1));
        DxSpot forty = makeSpot(2);
        forty.freqMhz = 7.030;
        f.table.addSpot(forty);
        auto* pill = f.dialog->findChild<QPushButton*>(QStringLiteral("spotListBandPill_20m"));
        QVERIFY(pill);
        const bool wasChecked = pill->isChecked();
        if (!wasChecked) {
            pill->setChecked(true);
        }
        QCoreApplication::processEvents();

        QList<QAccessible::Id> walked = Cells::walk(f.view);
        QVERIFY(!walked.isEmpty());
        pill->setChecked(false);
        QCoreApplication::processEvents();
        Cells::drop(walked);
        QCOMPARE(f.view->model()->rowCount(), 1);

        walked = Cells::walk(f.view);
        QVERIFY(!walked.isEmpty());
        pill->setChecked(true);
        QCoreApplication::processEvents();
        Cells::drop(walked);
        QCOMPARE(f.view->model()->rowCount(), 2);
        f.table.addSpot(makeSpot(3));
        QCoreApplication::processEvents();
        QCOMPARE(f.view->model()->rowCount(), 3);

        pill->setChecked(wasChecked);
    }

    // Clear resets the model; a hidden dialog still keeps its interface.
    void clearAndHiddenAfterBridgeDrain()
    {
        Cells::ActiveScope active;
        SpotListFixture f;
        QVERIFY(f.view);
        f.dialog->show();
        f.table.addSpot(makeSpot(1));
        QCoreApplication::processEvents();
        f.dialog->hide();

        QList<QAccessible::Id> walked = Cells::walk(f.view);
        QVERIFY(!walked.isEmpty());
        f.table.clear();
        QCoreApplication::processEvents();
        Cells::drop(walked);
        QCOMPARE(f.view->model()->rowCount(), 0);

        f.table.addSpot(makeSpot(2));
        QCoreApplication::processEvents();
        walked = Cells::walk(f.view);
        QVERIFY(!walked.isEmpty());
        f.table.addSpot(makeSpot(3));
        QCoreApplication::processEvents();
        Cells::drop(walked);
        f.table.addSpot(makeSpot(4));
        QCoreApplication::processEvents();
        QCOMPARE(f.view->model()->rowCount(), 3);
    }

    // A new spot arrives: the selected spot, the current spot and the top
    // visible spot are the ones the user had, and the list keeps its order.
    void newSpotKeepsPlace()
    {
        Cells::ActiveScope active;
        SpotListFixture f;
        QVERIFY(f.view);
        f.addSpots(1, 60);
        QCoreApplication::processEvents();
        f.placeOn(QStringLiteral("K20ABC"), QStringLiteral("K25ABC"));
        QCOMPARE(f.topCall(), QStringLiteral("K20ABC"));
        QCOMPARE(f.selectedCall(), QStringLiteral("K25ABC"));
        QCOMPARE(f.currentCall(), QStringLiteral("K25ABC"));
        QStringList order;
        for (int row = 0; row < f.view->model()->rowCount(); ++row) {
            order.append(f.callAt(row));
        }
        QSignalSpy resets(f.view->model(), &QAbstractItemModel::modelReset);
        QSignalSpy selectionChanges(f.view->selectionModel(),
                                    &QItemSelectionModel::selectionChanged);

        f.table.addSpot(makeSpot(61));
        QCoreApplication::processEvents();

        QCOMPARE(resets.count(), 1);
        QStringList after;
        for (int row = 0; row < f.view->model()->rowCount(); ++row) {
            if (f.callAt(row) != QStringLiteral("K61ABC")) {
                after.append(f.callAt(row));
            }
        }
        QCOMPARE(after, order);
        QVERIFY(f.rowOf(QStringLiteral("K61ABC")) >= 0);
        QCOMPARE(f.topCall(), QStringLiteral("K20ABC"));
        QCOMPARE(f.selectedCall(), QStringLiteral("K25ABC"));
        QCOMPARE(f.currentCall(), QStringLiteral("K25ABC"));
        QCOMPARE(selectionChanges.count(), 0);
    }

    // Several spots in one event-loop pass: each insert still goes out,
    // the proxy is reset before every insert after the first (the cells
    // may have drained between any two), once more after the burst, and
    // the place is kept.
    void burstOfSpotsResetsBeforeEachInsert()
    {
        Cells::ActiveScope active;
        SpotListFixture f;
        QVERIFY(f.view);
        f.addSpots(1, 60);
        QCoreApplication::processEvents();
        f.placeOn(QStringLiteral("K30ABC"), QStringLiteral("K28ABC"));
        QSignalSpy resets(f.view->model(), &QAbstractItemModel::modelReset);
        QSignalSpy inserts(f.view->model(), &QAbstractItemModel::rowsInserted);
        const QList<QAccessible::Id> walked = Cells::walk(f.view);
        QVERIFY(!walked.isEmpty());

        f.table.addSpot(makeSpot(61));
        Cells::drop(walked);
        f.addSpots(62, 65);
        QCOMPARE(inserts.count(), 5);
        QCOMPARE(resets.count(), 4);
        QCoreApplication::processEvents();

        QCOMPARE(resets.count(), 5);
        QCOMPARE(f.view->model()->rowCount(), 65);
        QCOMPARE(f.topCall(), QStringLiteral("K30ABC"));
        QCOMPARE(f.selectedCall(), QStringLiteral("K28ABC"));
        QCOMPARE(f.currentCall(), QStringLiteral("K28ABC"));
        f.table.addSpot(makeSpot(66));
        QCoreApplication::processEvents();
        QCOMPARE(f.view->model()->rowCount(), 66);
    }

    // Data updates (a bearing for every row, three times in one interval)
    // reset once, an interval after the first: the bridge drained the
    // cells an assistive client walked, and QAccessibleTable answers for
    // them from its cache with nothing until the reset. After it every
    // cell answers again, and the place is the user's.
    void dataUpdatesResetOncePerInterval()
    {
        constexpr int kIntervalMs = 100;
        setItemViewDataResetIntervalForTest(kIntervalMs);
        Cells::ActiveScope active;
        SpotListFixture f;
        setItemViewDataResetIntervalForTest(0);
        QVERIFY(f.view);
        f.addSpots(1, 60);
        QCoreApplication::processEvents();
        f.placeOn(QStringLiteral("K20ABC"), QStringLiteral("K22ABC"));
        QSignalSpy resets(f.view->model(), &QAbstractItemModel::modelReset);
        QSignalSpy changes(f.view->model(), &QAbstractItemModel::dataChanged);
        const int topRow = f.rowOf(QStringLiteral("K20ABC"));

        const QList<QAccessible::Id> walked = Cells::walk(f.view);
        QVERIFY(!walked.isEmpty());
        QCOMPARE(accessibleCellText(f.view, topRow, SpotTableModel::ColDxCall),
                 QStringLiteral("K20ABC"));
        for (int i = 0; i < 3; ++i) {
            f.table.setBearingResolver([i](const QString&) { return 10.0 * i; });
            QCoreApplication::processEvents();
        }
        Cells::drop(walked);
        QCOMPARE(changes.count(), 3);
        QCOMPARE(resets.count(), 0);
        // The window the reset closes: the walked cell is gone.
        QCOMPARE(accessibleCellText(f.view, topRow, SpotTableModel::ColDxCall), QString());

        QTRY_COMPARE_WITH_TIMEOUT(resets.count(), 1, 20 * kIntervalMs);
        QTest::qWait(3 * kIntervalMs);
        QCOMPARE(resets.count(), 1);
        QCOMPARE(f.topCall(), QStringLiteral("K20ABC"));
        QCOMPARE(f.selectedCall(), QStringLiteral("K22ABC"));
        QCOMPARE(f.currentCall(), QStringLiteral("K22ABC"));
        for (int row = 0; row < f.view->model()->rowCount(); ++row) {
            QCOMPARE(accessibleCellText(f.view, row, SpotTableModel::ColDxCall), f.callAt(row));
        }
    }

    // Two spots that read the same in every column (same call, frequency,
    // spotter and source in the same minute): the selection stays on the
    // one the user picked across a reset.
    void twinSpotsKeepTheChosenOne()
    {
        Cells::ActiveScope active;
        SpotListFixture f;
        QVERIFY(f.view);
        f.addSpots(1, 10);
        DxSpot twin = makeSpot(20);
        twin.dxCall = QStringLiteral("K9TWIN");
        f.table.addSpot(twin);
        twin.utcTime = twin.utcTime.addSecs(30);
        f.table.addSpot(twin);
        f.addSpots(11, 12);
        QCoreApplication::processEvents();
        f.dialog->resize(900, 600);
        f.dialog->show();
        QCoreApplication::processEvents();

        auto* proxy = qobject_cast<QSortFilterProxyModel*>(f.view->model());
        QVERIFY(proxy);
        QList<int> twins;
        for (int row = 0; row < proxy->rowCount(); ++row) {
            if (f.callAt(row) == QStringLiteral("K9TWIN")) {
                twins.append(row);
            }
        }
        QCOMPARE(twins.size(), 2);
        QCOMPARE(proxy->index(twins.at(0), SpotTableModel::ColTime).data(),
                 proxy->index(twins.at(1), SpotTableModel::ColTime).data());
        // The second twin shown: a key by text would find the first one.
        f.view->selectionModel()->setCurrentIndex(
            proxy->index(twins.at(1), SpotTableModel::ColDxCall),
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        const QPersistentModelIndex chosen =
            proxy->mapToSource(proxy->index(twins.at(1), SpotTableModel::ColDxCall));
        QSignalSpy resets(proxy, &QAbstractItemModel::modelReset);

        f.table.addSpot(makeSpot(13));
        QCoreApplication::processEvents();

        QCOMPARE(resets.count(), 1);
        const QModelIndexList selected = f.view->selectionModel()->selectedRows();
        QCOMPARE(selected.size(), 1);
        QCOMPARE(proxy->mapToSource(selected.first()).row(), chosen.row());
        QCOMPARE(proxy->mapToSource(f.view->currentIndex()).row(), chosen.row());
    }

    // A spot lands between the press and the second click of a
    // double-click: the reset drops the view's pressed index, yet the
    // double-click still tunes the spot under the pointer.
    void doubleClickAcrossResetTunesThatSpot()
    {
        Cells::ActiveScope active;
        SpotListFixture f;
        QVERIFY(f.view);
        f.addSpots(1, 30);
        QCoreApplication::processEvents();
        f.placeOn(QStringLiteral("K10ABC"), QStringLiteral("K12ABC"));
        QSignalSpy tunes(f.dialog.get(), &SpotHubDialog::tuneRequested);
        QSignalSpy resets(f.view->model(), &QAbstractItemModel::modelReset);

        const QPoint at =
            f.view->visualRect(f.view->model()->index(f.rowOf(QStringLiteral("K15ABC")),
                                                      SpotTableModel::ColDxCall))
                .center();
        const auto send = [&](QEvent::Type type, Qt::MouseButtons buttons) {
            QMouseEvent event(type, QPointF(at), QPointF(f.view->viewport()->mapToGlobal(at)),
                              Qt::LeftButton, buttons, Qt::NoModifier);
            QCoreApplication::sendEvent(f.view->viewport(), &event);
        };
        send(QEvent::MouseButtonPress, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, Qt::NoButton);
        f.table.addSpot(makeSpot(31));
        QCoreApplication::processEvents();
        QCOMPARE(resets.count(), 1);
        QCOMPARE(f.view->indexAt(at).siblingAtColumn(SpotTableModel::ColDxCall).data().toString(),
                 QStringLiteral("K15ABC"));
        send(QEvent::MouseButtonDblClick, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, Qt::NoButton);

        QCOMPARE(tunes.count(), 1);
        QCOMPARE(tunes.first().first().toDouble(), makeSpot(15).freqMhz);
    }

    // The cap trims the oldest spot and a bearing update changes every
    // row's data: the place is kept through both.
    void trimmedAndUpdatedRowsKeepPlace()
    {
        Cells::ActiveScope active;
        SpotListFixture f;
        QVERIFY(f.view);
        f.table.setMaxSpots(60);
        f.addSpots(1, 60);
        QCoreApplication::processEvents();
        f.placeOn(QStringLiteral("K20ABC"), QStringLiteral("K15ABC"));
        QSignalSpy resets(f.view->model(), &QAbstractItemModel::modelReset);

        // The insert leaves the cache stale, so the trim right after it
        // resets first; the queued reset follows.
        f.table.addSpot(makeSpot(61));
        QCoreApplication::processEvents();
        QCOMPARE(f.view->model()->rowCount(), 60);
        QCOMPARE(f.rowOf(QStringLiteral("K1ABC")), -1);
        QCOMPARE(resets.count(), 2);
        QCOMPARE(f.topCall(), QStringLiteral("K20ABC"));
        QCOMPARE(f.selectedCall(), QStringLiteral("K15ABC"));
        QCOMPARE(f.currentCall(), QStringLiteral("K15ABC"));

        const QList<QAccessible::Id> walked = Cells::walk(f.view);
        f.table.setBearingResolver([](const QString&) { return 42.0; });
        QCoreApplication::processEvents();
        Cells::drop(walked);
        QCOMPARE(resets.count(), 2);  // its reset is an interval away
        QCOMPARE(f.topCall(), QStringLiteral("K20ABC"));
        QCOMPARE(f.selectedCall(), QStringLiteral("K15ABC"));
        QCOMPARE(f.currentCall(), QStringLiteral("K15ABC"));
        f.table.addSpot(makeSpot(62));
        QCoreApplication::processEvents();
        QCOMPARE(f.view->model()->rowCount(), 60);
    }

    // The selected spot is trimmed away: what is selected and current
    // after, and the order of the list, are what Qt leaves without
    // accessibility.
    void trimmedSelectedSpotMatchesSightedUse()
    {
        const auto trimSelected = [](bool accessible) {
            Cells::setActive(accessible);
            SpotListFixture f;
            f.table.setMaxSpots(30);
            f.addSpots(1, 30);
            QCoreApplication::processEvents();
            f.placeOn(QStringLiteral("K10ABC"), QStringLiteral("K1ABC"));
            f.table.addSpot(makeSpot(31));
            QCoreApplication::processEvents();
            QStringList seen{f.selectedCall(), f.currentCall(),
                             QString::number(f.rowOf(QStringLiteral("K1ABC")))};
            for (int row = 0; row < f.view->model()->rowCount(); ++row) {
                seen.append(f.callAt(row));
            }
            return seen;
        };
        const QStringList sighted = trimSelected(false);
        const QStringList accessible = trimSelected(true);
        Cells::setActive(false);
        QCOMPARE(sighted.at(2), QStringLiteral("-1"));
        QCOMPARE(accessible, sighted);
    }

    // Accessibility off: no reset at all, persistent indexes keep their
    // spots, as before this change.
    void accessibilityOffLeavesModelAlone()
    {
        Cells::setActive(false);
        QVERIFY(!QAccessible::isActive());
        SpotListFixture f;
        QVERIFY(f.view);
        f.addSpots(1, 2);
        QAbstractItemModel* proxy = f.view->model();
        const QPersistentModelIndex kept =
            proxy->index(f.rowOf(QStringLiteral("K1ABC")), SpotTableModel::ColDxCall);
        QSignalSpy resets(proxy, &QAbstractItemModel::modelReset);

        f.addSpots(3, 4);
        QCoreApplication::processEvents();

        QCOMPARE(resets.count(), 0);
        QVERIFY(kept.isValid());
        QCOMPARE(kept.data().toString(), QStringLiteral("K1ABC"));
    }
};

QTEST_MAIN(TestSpotListAccessibility)
#include "tst_spot_list_accessibility.moc"
