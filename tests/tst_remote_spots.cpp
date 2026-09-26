// no-port-check: NereusSDR-original.
// =================================================================
// tests/tst_remote_spots.cpp  (NereusSDR)
// =================================================================
//
// Parity Task 19 (R-IOS-25, R-R3-49; acceptance B7.1 and B7.2): the Core's
// spots and spot sources in a remote window.
//
//   - A window subscribes after its snapshot: it gets the Core's spots
//     (the backlog), then new spots and removals, in its SpotModel (the
//     panadapter overlay) and its Spot List; a reset clears them.
//   - An unsubscribed window is sent nothing more.
//   - The Core's console lines reach the window's Spot Hub consoles; a
//     window's Connect reaches the Core, which connects from its own
//     settings, and the Core's refusals reach the window's tab.
//   - The window never opens its own cluster login; its own WSJT-X
//     listener runs.
//   - The Core starts its station sources with no window (nereusd's
//     restore) and never listens for WSJT-X itself.
//   - In a real remote window the Spot Hub's Core settings are disabled
//     with "Connect to the Core to change these." until the session is up,
//     while the WSJT-X and SpotCollector ports stay editable.
//
// The in-process loopback and a local WebSocket only; the cluster points at
// 127.0.0.1 on a port nothing listens on. No RF, no audio device.
//
//   cmake --build build --target tst_remote_spots
//   QT_QPA_PLATFORM=offscreen ctest --test-dir build -R '^tst_remote_spots$' \
//       --output-on-failure
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-26: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include <QtTest>

#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>

#include "core/AppSettings.h"
#include "core/DxClusterClient.h"
#include "core/SpotSourceHost.h"
#include "core/WsjtxClient.h"
#include "core/session/SessionMessages.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/settings/SettingsProxy.h"
#include "fakes/LoopbackTransport.h"
#include "fakes/RemoteWindowHarness.h"
#include "fakes/UpgradedCoreToken.h"
#include "gui/SpotHubDialog.h"
#include "models/RadioModel.h"
#include "models/SpotModel.h"
#include "models/SpotTableModel.h"

using namespace NereusSDR;
using NereusSDR::Test::LoopbackTransport;
using NereusSDR::Test::RemoteWindowHarness;

namespace {

std::unique_ptr<RadioModel> makeCore()
{
    auto model = std::make_unique<RadioModel>();
    model->setBoardForTest(HPSDRHW::HermesLite);
    RadioInfo info;
    info.macAddress = QStringLiteral("AA:BB:CC:DD:EE:19");
    info.name = QStringLiteral("Bench HL2");
    info.boardType = HPSDRHW::HermesLite;
    model->setLastRadioInfoForTest(info);
    model->setConnectionStateForTest(ConnectionState::Connected);
    model->addSlice(QStringLiteral("pan-0"));
    return model;
}

void addCoreSpot(RadioModel& core, int index, const QString& call, double mhz)
{
    QMap<QString, QString> kvs;
    kvs[QStringLiteral("callsign")] = call;
    kvs[QStringLiteral("rx_freq")] = QString::number(mhz, 'f', 4);
    kvs[QStringLiteral("source")] = QStringLiteral("Cluster");
    kvs[QStringLiteral("spotter_callsign")] = QStringLiteral("W3LPL");
    kvs[QStringLiteral("comment")] = QStringLiteral("CW");
    core.spotModel()->applySpotStatus(index, kvs);
}

QStringList calls(const SpotModel& model)
{
    QStringList out;
    for (const SpotData& spot : model.spots()) {
        out.append(spot.callsign);
    }
    out.sort();
    return out;
}

struct Session {
    Session(RadioModel* core, const QString& securityDir, QObject* parent)
        : settings(settingsDir.filePath(QStringLiteral("NereusSDR.settings")))
        , coreModel(core)
    {
        server = std::make_unique<StationServer>(
            coreModel, settings, NereusSDR::Test::seedUpgradedCoreToken(securityDir));
        client = std::make_unique<StationClient>(&window, &proxy);
        coreEnd = new LoopbackTransport(QStringLiteral("station-end"), parent);
        windowEnd = new LoopbackTransport(QStringLiteral("client-end"), parent);
        coreEnd->linkTo(windowEnd);
    }
    bool connect()
    {
        QSignalSpy completed(client.get(), &StationClient::handshakeComplete);
        client->startSession(windowEnd, server->token());
        server->acceptTransport(coreEnd);
        return completed.wait(5000) || completed.count() == 1;
    }
    int batchesFor(const QString& stream) const
    {
        int n = 0;
        for (const QByteArray& wire : windowEnd->received()) {
            SessionMessage message;
            if (SessionMessages::decode(wire, &message)
                && message.kind == SessionMessageKind::RecordBatch
                && message.recordBatch.stream == stream) {
                ++n;
            }
        }
        return n;
    }

    QTemporaryDir settingsDir;
    AppSettings settings;
    RadioModel* coreModel = nullptr;
    std::unique_ptr<StationServer> server;
    RadioModel window{RadioModel::Role::Remote};
    SettingsProxy proxy;
    std::unique_ptr<StationClient> client;
    LoopbackTransport* coreEnd = nullptr;
    LoopbackTransport* windowEnd = nullptr;
};

} // namespace

class TstRemoteSpots : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QLoggingCategory::setFilterRules(QStringLiteral("nereus.*.debug=false"));
        QVERIFY(m_securityDir.isValid());
        QVERIFY(RemoteWindowHarness::useIsolatedProfile(QStringLiteral("remote-spots")));
    }

    void init() { QVERIFY(RemoteWindowHarness::clearIsolatedProfile()); }

    void cleanupTestCase() { QVERIFY(RemoteWindowHarness::removeIsolatedProfile()); }

    void theWindowShowsTheCoresSpots()
    {
        std::unique_ptr<RadioModel> core = makeCore();
        addCoreSpot(*core, 1, QStringLiteral("JA1ABC"), 14.025);
        addCoreSpot(*core, 2, QStringLiteral("VK2XYZ"), 7.010);
        Session s(core.get(), m_securityDir.path(), this);
        QCOMPARE(s.server->recordStreamVersion(), 1);
        QVERIFY(s.connect());
        QCOMPARE(s.client->capabilities().recordStreamVersion, 1);

        // The backlog, then a new spot, a change and a removal.
        QTRY_COMPARE(calls(*s.window.spotModel()), (QStringList{"JA1ABC", "VK2XYZ"}));
        QCOMPARE(s.window.spotTableModel()->rowCount(), 2);
        const SpotData first = *s.window.spotModel()->spots().cbegin();
        QCOMPARE(first.source, QStringLiteral("Cluster"));
        addCoreSpot(*core, 3, QStringLiteral("G4ABC"), 3.510);
        QTRY_COMPARE(calls(*s.window.spotModel()),
                     (QStringList{"G4ABC", "JA1ABC", "VK2XYZ"}));
        QCOMPARE(s.window.spotTableModel()->rowCount(), 3);
        core->spotModel()->removeSpot(1);
        QTRY_COMPARE(calls(*s.window.spotModel()), (QStringList{"G4ABC", "VK2XYZ"}));
        // A spot the window holds at an index of its own stays beside the
        // Core's: its own WSJT-X decode.
        const int own = s.window.spotModel()->mintIndex();
        s.window.spotModel()->applySpotStatus(
            own, {{QStringLiteral("callsign"), QStringLiteral("K1JT")},
                  {QStringLiteral("rx_freq"), QStringLiteral("14.0740")},
                  {QStringLiteral("source"), QStringLiteral("WSJT-X")}});
        core->spotModel()->clear();
        QTRY_COMPARE(calls(*s.window.spotModel()), QStringList{QStringLiteral("K1JT")});
    }

    void anUnsubscribedWindowIsSentNothingMore()
    {
        std::unique_ptr<RadioModel> core = makeCore();
        Session s(core.get(), m_securityDir.path(), this);
        QVERIFY(s.connect());
        QTRY_VERIFY(s.batchesFor(QStringLiteral("spots")) >= 1);
        s.windowEnd->sendText(SessionMessages::encode(SessionMessages::commandInvoke(
            "records.unsubscribe", 777,
            {MirrorUpdate{0, "stream", MirrorWireKind::Utf8, QStringLiteral("spots")}})));
        QTRY_VERIFY(!s.server->recordStreamForTest(QStringLiteral("spots"))
                         ->isSubscribed(s.coreEnd));
        const int before = s.batchesFor(QStringLiteral("spots"));
        addCoreSpot(*core, 5, QStringLiteral("JA1ABC"), 14.025);
        QTest::qWait(200);
        QCOMPARE(s.batchesFor(QStringLiteral("spots")), before);
        QVERIFY(s.window.spotModel()->spots().isEmpty());
    }

    void theCoresConsoleAndSourcesReachTheWindow()
    {
        std::unique_ptr<RadioModel> core = makeCore();
        Session s(core.get(), m_securityDir.path(), this);
        QVERIFY(s.connect());
        SpotSourceHost* windowHost = s.window.spotSourceHost();
        QVERIFY(windowHost->forwardsStationSources());
        QSignalSpy lines(windowHost, &SpotSourceHost::consoleLine);
        QSignalSpy refused(windowHost, &SpotSourceHost::sourceRefused);
        QTRY_VERIFY(s.batchesFor(QStringLiteral("spotConsole:dxCluster")) >= 1);

        emit core->dxCluster()->rawLineReceived(QStringLiteral("DX de W3LPL: 14025.0 JA1ABC"));
        QTRY_COMPARE(lines.count(), 1);
        QCOMPARE(lines.first().at(0).toString(), SpotSourceHost::kDxCluster);
        QCOMPARE(lines.first().at(1).toString(), QStringLiteral("DX de W3LPL: 14025.0 JA1ABC"));

        // No callsign on the Core: refused there, shown here.
        QSignalSpy windowDx(s.window.dxCluster(), &DxClusterClient::connectionError);
        windowHost->connectCluster(QStringLiteral("127.0.0.1"), 18491, QStringLiteral("KG4VCF"));
        QTRY_COMPARE(refused.count(), 1);
        QCOMPARE(refused.first().at(1).toString(),
                 QStringLiteral("Enter your callsign in Spot Hub first."));

        // With the Core's callsign and address, the Core connects: its
        // state reaches the window's tab. The window never dials.
        auto& settings = AppSettings::instance();
        settings.setValue(QStringLiteral("DxClusterHost"), QStringLiteral("127.0.0.1"));
        settings.setValue(QStringLiteral("DxClusterPort"), 18492);
        settings.setValue(QStringLiteral("DxClusterCallsign"), QStringLiteral("KG4VCF"));
        QSignalSpy coreDx(core->dxCluster(), &DxClusterClient::connectionError);
        windowHost->connectCluster(QStringLiteral("127.0.0.1"), 18492, QStringLiteral("KG4VCF"));
        QTRY_VERIFY_WITH_TIMEOUT(coreDx.count() > 0, 5000);
        QTRY_COMPARE(windowHost->dxClusterState(), SpotSourceHost::kError);
        QCOMPARE(windowDx.count(), 0);
        QVERIFY(!s.window.dxCluster()->isConnected());

        // A typed command to a cluster the Core has not connected.
        windowHost->typeCommand(SpotSourceHost::kDxCluster, QStringLiteral("sh/dx"));
        QTRY_COMPARE(refused.count(), 2);
        QCOMPARE(refused.last().at(1).toString(), QStringLiteral("The DX cluster is not connected."));
    }

    void theWindowRunsOnlyItsOwnListeners()
    {
        auto& settings = AppSettings::instance();
        settings.setValue(QStringLiteral("DxClusterAutoConnect"), QStringLiteral("True"));
        settings.setValue(QStringLiteral("DxClusterHost"), QStringLiteral("127.0.0.1"));
        settings.setValue(QStringLiteral("DxClusterPort"), 18493);
        settings.setValue(QStringLiteral("DxClusterCallsign"), QStringLiteral("KG4VCF"));
        settings.setValue(QStringLiteral("WsjtxAutoStart"), QStringLiteral("True"));
        settings.setValue(QStringLiteral("WsjtxAddress"), QStringLiteral("127.0.0.1"));
        settings.setValue(QStringLiteral("WsjtxPort"), 28493);

        RadioModel window{RadioModel::Role::Remote};
        QSignalSpy dx(window.dxCluster(), &DxClusterClient::connectionError);
        window.restoreSpotClientAutoStartState();
        QVERIFY(window.wsjtx()->isListening());
        window.wsjtx()->stopListening();
        QTest::qWait(300);
        QCOMPARE(dx.count(), 0);
        QVERIFY(!window.dxCluster()->isConnected());

        // The Core: the cluster, never WSJT-X.
        std::unique_ptr<RadioModel> core = makeCore();
        QSignalSpy coreDx(core->dxCluster(), &DxClusterClient::connectionError);
        core->restoreStationSpotSources();
        QTRY_VERIFY_WITH_TIMEOUT(coreDx.count() > 0, 5000);
        QVERIFY(!core->wsjtx()->isListening());
    }

    // B7.2 in a real remote window.
    void theSpotHubsCoreSettingsWaitForTheCore()
    {
        RemoteWindowHarness h;
        QVERIFY(h.start());
        QAction* spotHub = h.menuAction(QStringLiteral("&Tools"), QStringLiteral("Spot &Hub..."));
        QVERIFY(spotHub);
        spotHub->trigger();
        auto* hub = h.window()->findChild<SpotHubDialog*>();
        QVERIFY(hub);
        auto* host = hub->findChild<QLineEdit*>(QStringLiteral("clusterHostEdit"));
        auto* connectBtn = hub->findChild<QPushButton*>(QStringLiteral("clusterConnectBtn"));
        auto* wsjtxPort = hub->findChild<QSpinBox*>(QStringLiteral("wsjtxPortSpin"));
        auto* scPort = hub->findChild<QSpinBox*>(QStringLiteral("scPortSpin"));
        QVERIFY(host && connectBtn && wsjtxPort && scPort);
        const QString why = QStringLiteral("Connect to the Core to change these.");
        QVERIFY(!host->isEnabled());
        QCOMPARE(host->toolTip(), why);
        QVERIFY(!connectBtn->isEnabled());
        QCOMPARE(connectBtn->toolTip(), why);
        QVERIFY(wsjtxPort->isEnabled());
        QVERIFY(scPort->isEnabled());

        QAction* connect = h.menuAction(QStringLiteral("&Radio"), QStringLiteral("&Connect"));
        QVERIFY(connect && connect->isEnabled());
        connect->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(h.client()->isHandshakeComplete(), 10000);
        QTRY_VERIFY(host->isEnabled());
        QTRY_VERIFY(connectBtn->isEnabled());
        QCOMPARE(host->toolTip(), QString());
    }

private:
    QTemporaryDir m_securityDir;
};

QTEST_MAIN(TstRemoteSpots)
#include "tst_remote_spots.moc"
