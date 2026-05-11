// SPDX-License-Identifier: GPL-3.0-or-later
//
// no-port-check: Test constructs fixture clients and asserts the
// tab-strip contract; no callsigns or wire payloads are involved.
//
// NereusSDR - SpotHubDialog tab-strip smoke tests
//
// Phase 3J-2 Task F1. Pins the contract that SpotHubDialog
// constructs successfully when handed all 6 spot-ingest clients
// + the SpotModel + the DxccColorProvider, that its top-level
// QTabWidget exposes exactly 9 tabs, and that the tab labels
// appear in AetherSDR's upstream order (Cluster, RBN, WSJT-X,
// SpotCollector, POTA, FreeDV, PSK Reporter, Spot List, Display).
//
// Three tests:
//   - dialogConstructs - all clients passed in, dialog non-null
//   - hasNineTabs - QTabWidget has exactly 9 tabs
//   - tabOrderMatchesAetherSdr - tab labels in upstream order

#include <QtTest>
#include <QTabWidget>

#include "gui/SpotHubDialog.h"
#include "core/DxClusterClient.h"
#include "core/WsjtxClient.h"
#include "core/SpotCollectorClient.h"
#include "core/PotaClient.h"
#include "core/FreeDVReporterClient.h"
#include "core/PskReporterClient.h"
#include "core/DxccColorProvider.h"
#include "models/SpotModel.h"

using namespace NereusSDR;

class TestSpotHubDialogSmoke : public QObject {
    Q_OBJECT
private slots:
    void dialogConstructs();
    void hasNineTabs();
    void tabOrderMatchesAetherSdr();
};

static SpotHubDialog* makeDialog() {
    auto cluster = new DxClusterClient;
    auto rbn = new DxClusterClient;
    auto wsjtx = new WsjtxClient;
    auto spotCollector = new SpotCollectorClient;
    auto pota = new PotaClient;
    auto freedv = new FreeDVReporterClient;
    auto psk = new PskReporterClient;
    auto spots = new SpotModel;
    auto dxcc = new DxccColorProvider;

    return new SpotHubDialog(cluster, rbn, wsjtx, spotCollector, pota,
                             freedv, psk, spots, dxcc, nullptr);
}

void TestSpotHubDialogSmoke::dialogConstructs() {
    auto* dlg = makeDialog();
    QVERIFY(dlg != nullptr);
    delete dlg;
}

void TestSpotHubDialogSmoke::hasNineTabs() {
    auto* dlg = makeDialog();
    auto* tabs = dlg->findChild<QTabWidget*>();
    QVERIFY(tabs != nullptr);
    QCOMPARE(tabs->count(), 9);
    delete dlg;
}

void TestSpotHubDialogSmoke::tabOrderMatchesAetherSdr() {
    auto* dlg = makeDialog();
    auto* tabs = dlg->findChild<QTabWidget*>();
    QVERIFY(tabs != nullptr);
    QVERIFY(tabs->tabText(0).contains("Cluster", Qt::CaseInsensitive));
    QVERIFY(tabs->tabText(1).contains("RBN", Qt::CaseInsensitive));
    QVERIFY(tabs->tabText(2).contains("WSJT", Qt::CaseInsensitive));
    QVERIFY(tabs->tabText(3).contains("SpotCollector", Qt::CaseInsensitive));
    QVERIFY(tabs->tabText(4).contains("POTA", Qt::CaseInsensitive));
    QVERIFY(tabs->tabText(5).contains("FreeDV", Qt::CaseInsensitive));
    QVERIFY(tabs->tabText(6).contains("PSK", Qt::CaseInsensitive));
    QVERIFY(tabs->tabText(7).contains("Spot List", Qt::CaseInsensitive));
    QVERIFY(tabs->tabText(8).contains("Display", Qt::CaseInsensitive));
    delete dlg;
}

QTEST_MAIN(TestSpotHubDialogSmoke)
#include "tst_spothub_dialog_smoke.moc"
