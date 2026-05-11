// SPDX-License-Identifier: GPL-3.0-or-later
//
// no-port-check: Test constructs fixture clients and asserts the
// tab-strip contract; no callsigns or wire payloads are involved.
//
// NereusSDR - SpotHubDialog tab-strip smoke tests
//
// Phase 3J-2 Task F1 + F2. Pins the contract that SpotHubDialog
// constructs successfully when handed all 6 spot-ingest clients
// + the SpotModel + the DxccColorProvider, that its top-level
// QTabWidget exposes exactly 9 tabs, that the tab labels appear
// in AetherSDR's upstream order (Cluster, RBN, WSJT-X,
// SpotCollector, POTA, FreeDV, PSK Reporter, Spot List, Display),
// and (F2) that each per-source tab carries the uniform template:
// connection-control widgets, auto-start toggle, start/stop button,
// status line, and a raw-event console. WSJT-X carries the three
// filter checkboxes and four color picker buttons. PSK Reporter
// is NereusSDR-native and uses the same uniform shape.

#include <QtTest>
#include <QTabWidget>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QSlider>

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

    // F2: per-source tab content (uniform template).
    void clusterTabHasHostPortCall();
    void rbnTabHasHostPortCallRate();
    void wsjtxTabHasAddrPort();
    void wsjtxTabHasFilterCheckboxes();
    void wsjtxTabHasFourColorPickers();
    void spotCollectorTabHasPortSpin();
    void potaTabHasIntervalSpin();
    void freedvTabHasAutoStartAndConsole();
    void pskTabHasCallsignField();

    // F2 cross-cutting: every per-source tab carries the uniform
    // skeleton (auto-start toggle, start/stop button, status label,
    // raw-event console).
    void everySourceTabHasUniformTemplate();
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

void TestSpotHubDialogSmoke::clusterTabHasHostPortCall() {
    auto* dlg = makeDialog();
    QVERIFY(dlg->findChild<QLineEdit*>("clusterHostEdit") != nullptr);
    QVERIFY(dlg->findChild<QSpinBox*>("clusterPortSpin") != nullptr);
    QVERIFY(dlg->findChild<QLineEdit*>("clusterCallEdit") != nullptr);
    QVERIFY(dlg->findChild<QLineEdit*>("clusterCmdEdit") != nullptr);
    QVERIFY(dlg->findChild<QPushButton*>("clusterSendBtn") != nullptr);
    delete dlg;
}

void TestSpotHubDialogSmoke::rbnTabHasHostPortCallRate() {
    auto* dlg = makeDialog();
    QVERIFY(dlg->findChild<QLineEdit*>("rbnHostEdit") != nullptr);
    QVERIFY(dlg->findChild<QSpinBox*>("rbnPortSpin") != nullptr);
    QVERIFY(dlg->findChild<QLineEdit*>("rbnCallEdit") != nullptr);
    QVERIFY(dlg->findChild<QSpinBox*>("rbnRateSpin") != nullptr);
    QVERIFY(dlg->findChild<QLineEdit*>("rbnCmdEdit") != nullptr);
    QVERIFY(dlg->findChild<QPushButton*>("rbnSendBtn") != nullptr);
    delete dlg;
}

void TestSpotHubDialogSmoke::wsjtxTabHasAddrPort() {
    auto* dlg = makeDialog();
    QVERIFY(dlg->findChild<QLineEdit*>("wsjtxAddrEdit") != nullptr);
    QVERIFY(dlg->findChild<QSpinBox*>("wsjtxPortSpin") != nullptr);
    QVERIFY(dlg->findChild<QSlider*>("wsjtxLifeSlider") != nullptr);
    delete dlg;
}

void TestSpotHubDialogSmoke::wsjtxTabHasFilterCheckboxes() {
    auto* dlg = makeDialog();
    QVERIFY(dlg->findChild<QCheckBox*>("wsjtxFilterCQ") != nullptr);
    QVERIFY(dlg->findChild<QCheckBox*>("wsjtxFilterPOTA") != nullptr);
    QVERIFY(dlg->findChild<QCheckBox*>("wsjtxFilterCallingMe") != nullptr);
    delete dlg;
}

void TestSpotHubDialogSmoke::wsjtxTabHasFourColorPickers() {
    auto* dlg = makeDialog();
    QVERIFY(dlg->findChild<QPushButton*>("wsjtxColorCQ") != nullptr);
    QVERIFY(dlg->findChild<QPushButton*>("wsjtxColorPOTA") != nullptr);
    QVERIFY(dlg->findChild<QPushButton*>("wsjtxColorCallingMe") != nullptr);
    QVERIFY(dlg->findChild<QPushButton*>("wsjtxColorDefault") != nullptr);
    delete dlg;
}

void TestSpotHubDialogSmoke::spotCollectorTabHasPortSpin() {
    auto* dlg = makeDialog();
    QVERIFY(dlg->findChild<QSpinBox*>("scPortSpin") != nullptr);
    delete dlg;
}

void TestSpotHubDialogSmoke::potaTabHasIntervalSpin() {
    auto* dlg = makeDialog();
    QVERIFY(dlg->findChild<QSpinBox*>("potaIntervalSpin") != nullptr);
    QVERIFY(dlg->findChild<QPushButton*>("potaColorBtn") != nullptr);
    delete dlg;
}

void TestSpotHubDialogSmoke::freedvTabHasAutoStartAndConsole() {
    auto* dlg = makeDialog();
    QVERIFY(dlg->findChild<QPushButton*>("freedvAutoStartBtn") != nullptr);
    QVERIFY(dlg->findChild<QPlainTextEdit*>("freedvConsole") != nullptr);
    QVERIFY(dlg->findChild<QPushButton*>("freedvColorBtn") != nullptr);
    delete dlg;
}

void TestSpotHubDialogSmoke::pskTabHasCallsignField() {
    auto* dlg = makeDialog();
    QVERIFY(dlg->findChild<QLineEdit*>("pskCallEdit") != nullptr);
    QVERIFY(dlg->findChild<QLineEdit*>("pskGridEdit") != nullptr);
    QVERIFY(dlg->findChild<QPushButton*>("pskAutoStartBtn") != nullptr);
    QVERIFY(dlg->findChild<QPushButton*>("pskStartBtn") != nullptr);
    QVERIFY(dlg->findChild<QLabel*>("pskStatusLabel") != nullptr);
    QVERIFY(dlg->findChild<QPlainTextEdit*>("pskConsole") != nullptr);
    delete dlg;
}

void TestSpotHubDialogSmoke::everySourceTabHasUniformTemplate() {
    // For each per-source tab the uniform template requires:
    //   * an auto-start toggle button
    //   * a start/stop button
    //   * a status label
    //   * a raw-event console (QPlainTextEdit)
    static constexpr struct {
        const char* prefix;
    } sources[] = {
        {"cluster"},
        {"rbn"},
        {"wsjtx"},
        {"sc"},
        {"pota"},
        {"freedv"},
        {"psk"},
    };

    auto* dlg = makeDialog();
    for (const auto& src : sources) {
        QString autoBtn  = QString("%1AutoStartBtn").arg(src.prefix);
        QString autoBtn2 = QString("%1AutoConnectBtn").arg(src.prefix);
        // Cluster and RBN use "AutoConnect"; UDP / WebSocket sources use
        // "AutoStart". Accept either name to keep the uniform-template
        // assertion source-agnostic.
        QPushButton* autoButton =
            dlg->findChild<QPushButton*>(autoBtn);
        if (!autoButton)
            autoButton = dlg->findChild<QPushButton*>(autoBtn2);
        QVERIFY2(autoButton != nullptr,
                 qPrintable(QString("missing auto-toggle for %1").arg(src.prefix)));

        // Start/Stop button: name is `<prefix>StartBtn` for UDP/WS or
        // `<prefix>ConnectBtn` for telnet (cluster/rbn).
        QString startName = QString("%1StartBtn").arg(src.prefix);
        QString connName = QString("%1ConnectBtn").arg(src.prefix);
        QPushButton* startBtn = dlg->findChild<QPushButton*>(startName);
        if (!startBtn) {
            startBtn = dlg->findChild<QPushButton*>(connName);
        }
        QVERIFY2(startBtn != nullptr,
                 qPrintable(QString("missing start/connect button for %1").arg(src.prefix)));

        QString statusName = QString("%1StatusLabel").arg(src.prefix);
        QVERIFY2(dlg->findChild<QLabel*>(statusName) != nullptr,
                 qPrintable(QString("missing status label for %1").arg(src.prefix)));

        QString consoleName = QString("%1Console").arg(src.prefix);
        QVERIFY2(dlg->findChild<QPlainTextEdit*>(consoleName) != nullptr,
                 qPrintable(QString("missing console for %1").arg(src.prefix)));
    }
    delete dlg;
}

QTEST_MAIN(TestSpotHubDialogSmoke)
#include "tst_spothub_dialog_smoke.moc"
