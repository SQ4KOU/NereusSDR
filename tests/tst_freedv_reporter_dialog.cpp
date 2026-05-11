// SPDX-License-Identifier: GPL-2.0-or-later
//
// no-port-check: Test wires synthetic FreeDVStationModel fixtures into the
// dialog; no callsigns or wire payloads are involved.
//
// NereusSDR - FreeDVReporterDialog G1 shell smoke tests
//
// Phase 3J-2 Task G1. Pins the contract that FreeDVReporterDialog (the
// freedv-gui freedv_reporter.{h,cpp} port) constructs successfully when
// handed a FreeDVStationModel, that its embedded QTableView reports the
// 14-column layout drawn from upstream's `createColumn_` switch (column
// labels, alignment, exact column count), and that the per-row TX-red /
// RX-green highlight tones flip on / off in response to
// FreeDVStationModel::stationUpdated. The 6-second clear interval is
// drained via a test seam (`setHighlightClearMsForTest`) so the suite
// does not block on real wall-clock time. G2 (filter / QSY / message
// bar) and G3 (menu bar with Show / Filter / Idle menus) are out of
// scope; G1 only leaves the placeholder hooks where those wire in.
//
// Source ports cited inline next to each port.
//   freedv-gui src/gui/dialogs/freedv_reporter.cpp:47-65 (column index
//     macros + UNKNOWN_SNR_VAL) [@77e793a]
//   freedv-gui src/gui/dialogs/freedv_reporter.cpp:75-182 (createColumn_
//     column-build table) [@77e793a]
//   freedv-gui src/gui/dialogs/freedv_reporter.cpp:3580-3737
//     (onTransmitUpdateFn_ / onReceiveUpdateFn_ TX-RX state hooks) [@77e793a]
//   freedv-gui src/gui/dialogs/freedv_reporter.cpp:1289-1322
//     (TX-red / RX-green / msg-color row-tint priority chain) [@77e793a]

#include <QtTest>
#include <QApplication>
#include <QTableView>
#include <QHeaderView>
#include <QMenuBar>
#include <QSignalSpy>
#include <QDateTime>
#include <QColor>

#include "gui/FreeDVReporterDialog.h"
#include "models/FreeDVStationModel.h"
#include "core/FreeDVStation.h"

using namespace NereusSDR;

class TestFreeDVReporterDialog : public QObject {
    Q_OBJECT
private slots:
    void dialogConstructs();
    void tableHasFourteenColumns();
    void tableColumnHeadersMatchUpstream();
    void tableColumnAlignmentsMatchUpstream();
    void populatesFiveStationsToFiveRows();
    void snrColumnFormatsMinusNinetyNineAsDash();
    void txReportTurnsRowRed();
    void rxReportTurnsRowGreen();
    void highlightClearsAfterSixSeconds();
    void stationRemovedDeletesRow();
};

// Build a FreeDVStation with sensible defaults and let callers override.
static FreeDVStation makeStation(const QString& sid, const QString& callsign) {
    FreeDVStation s;
    s.sid = sid;
    s.callsign = callsign;
    s.gridSquare = "EM00";
    s.version = "1.9.0";
    s.frequencyHz = 14236000;
    s.txMode = "700D";
    s.status = "Active";
    s.lastUpdate = QDateTime::currentDateTime();
    return s;
}

void TestFreeDVReporterDialog::dialogConstructs() {
    auto* model = new FreeDVStationModel;
    auto* dlg = new FreeDVReporterDialog(model, nullptr, nullptr);
    QVERIFY(dlg != nullptr);
    QVERIFY(dlg->findChild<QTableView*>() != nullptr);
    QVERIFY(dlg->findChild<QMenuBar*>() != nullptr);
    delete dlg;
    delete model;
}

void TestFreeDVReporterDialog::tableHasFourteenColumns() {
    auto* model = new FreeDVStationModel;
    auto* dlg = new FreeDVReporterDialog(model, nullptr, nullptr);
    auto* table = dlg->findChild<QTableView*>();
    QVERIFY(table != nullptr);
    QCOMPARE(table->model()->columnCount(), 14);
    delete dlg;
    delete model;
}

void TestFreeDVReporterDialog::tableColumnHeadersMatchUpstream() {
    auto* model = new FreeDVStationModel;
    auto* dlg = new FreeDVReporterDialog(model, nullptr, nullptr);
    auto* table = dlg->findChild<QTableView*>();
    QVERIFY(table != nullptr);
    auto* tm = table->model();

    // Headers per freedv-gui createColumn_ switch, column indices 0..13
    // [freedv_reporter.cpp:75-153 @77e793a]. G1 hard-codes MHz for col 5;
    // the unit toggle (kHz) lands in G3.
    QCOMPARE(tm->headerData(0, Qt::Horizontal).toString(), QStringLiteral("Callsign"));
    QCOMPARE(tm->headerData(1, Qt::Horizontal).toString(), QStringLiteral("Locator"));
    QCOMPARE(tm->headerData(2, Qt::Horizontal).toString(), QStringLiteral("km"));
    QCOMPARE(tm->headerData(3, Qt::Horizontal).toString(), QStringLiteral("Hdg"));
    QCOMPARE(tm->headerData(4, Qt::Horizontal).toString(), QStringLiteral("Version"));
    QCOMPARE(tm->headerData(5, Qt::Horizontal).toString(), QStringLiteral("MHz"));
    QCOMPARE(tm->headerData(6, Qt::Horizontal).toString(), QStringLiteral("Mode"));
    QCOMPARE(tm->headerData(7, Qt::Horizontal).toString(), QStringLiteral("Status"));
    QCOMPARE(tm->headerData(8, Qt::Horizontal).toString(), QStringLiteral("Msg"));
    QCOMPARE(tm->headerData(9, Qt::Horizontal).toString(), QStringLiteral("Last TX"));
    QCOMPARE(tm->headerData(10, Qt::Horizontal).toString(), QStringLiteral("RX Call"));
    QCOMPARE(tm->headerData(11, Qt::Horizontal).toString(), QStringLiteral("Mode (RX)"));
    QCOMPARE(tm->headerData(12, Qt::Horizontal).toString(), QStringLiteral("SNR"));
    QCOMPARE(tm->headerData(13, Qt::Horizontal).toString(), QStringLiteral("Last Update"));

    delete dlg;
    delete model;
}

void TestFreeDVReporterDialog::tableColumnAlignmentsMatchUpstream() {
    auto* model = new FreeDVStationModel;
    auto* dlg = new FreeDVReporterDialog(model, nullptr, nullptr);
    auto* table = dlg->findChild<QTableView*>();
    QVERIFY(table != nullptr);
    auto* tm = table->model();

    // CALLSIGN_COL renderer SetAlignment(wxALIGN_LEFT | wxALIGN_CENTRE_VERTICAL)
    //   [freedv-gui freedv_reporter.cpp:79+86-88 @77e793a]
    auto callsignAlign = tm->headerData(0, Qt::Horizontal, Qt::TextAlignmentRole)
                            .toInt() & Qt::AlignHorizontal_Mask;
    QCOMPARE(callsignAlign, int(Qt::AlignLeft));

    // DISTANCE_COL renderer SetAlignment(wxALIGN_RIGHT | wxALIGN_CENTRE_VERTICAL)
    //   [freedv-gui freedv_reporter.cpp:93-97 @77e793a]
    auto kmAlign = tm->headerData(2, Qt::Horizontal, Qt::TextAlignmentRole)
                      .toInt() & Qt::AlignHorizontal_Mask;
    QCOMPARE(kmAlign, int(Qt::AlignRight));

    // USER_MESSAGE_COL builds the column with wxALIGN_CENTER
    //   [freedv-gui freedv_reporter.cpp:120-133 @77e793a]
    auto msgAlign = tm->headerData(8, Qt::Horizontal, Qt::TextAlignmentRole)
                       .toInt() & Qt::AlignHorizontal_Mask;
    QCOMPARE(msgAlign, int(Qt::AlignHCenter));

    delete dlg;
    delete model;
}

void TestFreeDVReporterDialog::populatesFiveStationsToFiveRows() {
    auto* model = new FreeDVStationModel;
    auto* dlg = new FreeDVReporterDialog(model, nullptr, nullptr);
    auto* table = dlg->findChild<QTableView*>();
    QVERIFY(table != nullptr);

    for (int i = 0; i < 5; ++i) {
        QString sid = QStringLiteral("sid-%1").arg(i);
        QString call = QStringLiteral("CALL%1").arg(i);
        model->onStationAdded(sid, makeStation(sid, call));
    }
    QCOMPARE(table->model()->rowCount(), 5);

    delete dlg;
    delete model;
}

void TestFreeDVReporterDialog::snrColumnFormatsMinusNinetyNineAsDash() {
    auto* model = new FreeDVStationModel;
    auto* dlg = new FreeDVReporterDialog(model, nullptr, nullptr);
    auto* table = dlg->findChild<QTableView*>();
    QVERIFY(table != nullptr);

    auto sUnknown = makeStation("sid-unknown", "AA0AA");
    sUnknown.snrVal = -99;
    model->onStationAdded("sid-unknown", sUnknown);

    auto sPos = makeStation("sid-pos", "BB0BB");
    sPos.snrVal = 12;
    model->onStationAdded("sid-pos", sPos);

    // SNR is column 12 per freedv_reporter.cpp:59 @77e793a
    auto tm = table->model();
    QString unknownText, posText;
    for (int r = 0; r < tm->rowCount(); ++r) {
        QString call = tm->index(r, 0).data(Qt::DisplayRole).toString();
        QString snr = tm->index(r, 12).data(Qt::DisplayRole).toString();
        if (call == "AA0AA") unknownText = snr;
        if (call == "BB0BB") posText = snr;
    }
    QCOMPARE(unknownText, QStringLiteral(" - "));
    QCOMPARE(posText, QStringLiteral("+12"));

    delete dlg;
    delete model;
}

void TestFreeDVReporterDialog::txReportTurnsRowRed() {
    auto* model = new FreeDVStationModel;
    auto* dlg = new FreeDVReporterDialog(model, nullptr, nullptr);
    auto* table = dlg->findChild<QTableView*>();
    QVERIFY(table != nullptr);

    auto s = makeStation("sid-tx", "K1TX");
    model->onStationAdded("sid-tx", s);
    s.transmitting = true;
    model->onStationUpdated("sid-tx", s);

    QColor bg = dlg->rowHighlightColorForTest("sid-tx");
    // Red-dominant tone per freedv_reporter.cpp:1313-1317 @77e793a
    // (`backgroundColor = parent_->txRowBackgroundColor`).
    QVERIFY(bg.isValid());
    QVERIFY(bg.red() > bg.green());
    QVERIFY(bg.red() > bg.blue());

    delete dlg;
    delete model;
}

void TestFreeDVReporterDialog::rxReportTurnsRowGreen() {
    auto* model = new FreeDVStationModel;
    auto* dlg = new FreeDVReporterDialog(model, nullptr, nullptr);
    auto* table = dlg->findChild<QTableView*>();
    QVERIFY(table != nullptr);

    auto s = makeStation("sid-rx", "K1RX");
    model->onStationAdded("sid-rx", s);
    s.lastRxCallsign = "K1NEAR";
    s.lastRxMode = "700D";
    s.snrVal = 5;
    s.lastRxDate = QDateTime::currentDateTime();
    model->onStationUpdated("sid-rx", s);

    QColor bg = dlg->rowHighlightColorForTest("sid-rx");
    // Green-dominant tone per freedv_reporter.cpp:1318-1322 @77e793a
    // (`backgroundColor = parent_->rxRowBackgroundColor`).
    QVERIFY(bg.isValid());
    QVERIFY(bg.green() > bg.red());
    QVERIFY(bg.green() > bg.blue());

    delete dlg;
    delete model;
}

void TestFreeDVReporterDialog::highlightClearsAfterSixSeconds() {
    auto* model = new FreeDVStationModel;
    auto* dlg = new FreeDVReporterDialog(model, nullptr, nullptr);
    // Drop the clear interval to keep the test fast. Production wires
    // 6000 ms per task spec; G3 will surface this as a per-radio setting
    // matching upstream's freedv_reporter_tx_rx_highlight_time.
    dlg->setHighlightClearMsForTest(50);

    auto s = makeStation("sid-fade", "K1FADE");
    model->onStationAdded("sid-fade", s);
    s.transmitting = true;
    model->onStationUpdated("sid-fade", s);
    QVERIFY(dlg->rowHighlightColorForTest("sid-fade").isValid());

    QTest::qWait(120);
    QColor bg = dlg->rowHighlightColorForTest("sid-fade");
    QVERIFY(!bg.isValid());

    delete dlg;
    delete model;
}

void TestFreeDVReporterDialog::stationRemovedDeletesRow() {
    auto* model = new FreeDVStationModel;
    auto* dlg = new FreeDVReporterDialog(model, nullptr, nullptr);
    auto* table = dlg->findChild<QTableView*>();
    QVERIFY(table != nullptr);

    model->onStationAdded("sid-go", makeStation("sid-go", "K1GO"));
    QCOMPARE(table->model()->rowCount(), 1);
    model->onStationRemoved("sid-go");
    QCOMPARE(table->model()->rowCount(), 0);

    delete dlg;
    delete model;
}

QTEST_MAIN(TestFreeDVReporterDialog)
#include "tst_freedv_reporter_dialog.moc"
