// no-port-check: NereusSDR-original accepted-value and ordering integration tests.
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QFile>
#include "core/AppSettings.h"
#include "core/session/SessionMessages.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/settings/SettingsProxy.h"
#include "core/settings/SettingsProxyServer.h"
#include "models/RadioModel.h"
#include "models/PureSignalSettings.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"
#include "fakes/LoopbackTransport.h"
#include "fakes/UpgradedCoreToken.h"

using namespace NereusSDR;
using Test::LoopbackTransport;

namespace {
class HoldingTransport : public LoopbackTransport {
public:
    explicit HoldingTransport() : LoopbackTransport(QStringLiteral("station")) {}
    bool hold = false;
    bool advertisePropertyResults = true;
    QList<QByteArray> held;
    void sendText(const QByteArray& wire) override
    {
        SessionMessage message;
        if (!advertisePropertyResults && SessionMessages::decode(wire, &message)
            && message.kind == SessionMessageKind::Capabilities) {
            for (auto& update : message.updates) {
                if (update.name == "propertyResultVersion") {
                    update.value = 0;
                }
            }
            LoopbackTransport::sendText(SessionMessages::encode(message));
            return;
        }
        if (hold && SessionMessages::decode(wire, &message)
            && message.kind == SessionMessageKind::PropertyResult) {
            held.append(wire);
            return;
        }
        LoopbackTransport::sendText(wire);
    }
    void releaseFirst() { LoopbackTransport::sendText(held.takeFirst()); }
};
MirrorUpdate real(const char* name, double value)
{
    return {0, name, MirrorWireKind::Float64, value};
}
}

class TestSessionPropertyResult : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        // This test deliberately reloads its atomic settings file. Other
        // executables run in parallel and share the default Qt test sandbox.
        AppSettings::setProfileOverride(QStringLiteral("property-result-%1")
            .arg(QCoreApplication::applicationPid()));
    }
    void init() { AppSettings::instance().clear(); }
    void cleanupTestCase()
    {
        const QString path = AppSettings::instance().filePath();
        QFile::remove(path);
        QFile::remove(path + QStringLiteral(".bak"));
    }

    void codecPreservesReadbackAndRejectsFractionalSequences()
    {
        const SessionPropertyResult value{"nnrAlpha", false, "Model unavailable", true, real("nnrAlpha", 1.25)};
        const auto source = SessionMessages::propertyResult("slice:7", 4294967295u, {value});
        SessionMessage decoded;
        QVERIFY(SessionMessages::decode(SessionMessages::encode(source), &decoded));
        QCOMPARE(decoded.writeId, 4294967295u);
        QCOMPARE(decoded.propertyResults.size(), 1);
        QCOMPARE(decoded.propertyResults[0].value.value.toDouble(), 1.25);
        QVERIFY(!decoded.propertyResults[0].accepted);
        for (double invalid : {-1.0, 0.0, 1.5, 4294967296.0}) {
            auto json = QJsonDocument::fromJson(SessionMessages::encode(source)).object();
            json["writeId"] = invalid;
            QVERIFY(!SessionMessages::decode(QJsonDocument(json).toJson(), &decoded));
        }
        auto json = QJsonDocument::fromJson(SessionMessages::encode(source)).object();
        QJsonArray duplicated = json["results"].toArray();
        duplicated.append(duplicated.first());
        json["results"] = duplicated;
        QVERIFY(!SessionMessages::decode(QJsonDocument(json).toJson(), &decoded));
        QVERIFY(SessionMessages::decode(SessionMessages::encode(
            SessionMessages::propertyWrite("slice:7", {real("nnrAlpha", 1.5)})), &decoded));
        QCOMPARE(decoded.writeId, 0u); // Older peers remain decodable.
    }

    void delayedResultCannotReplaceANewerEditAndRefusalReturnsCoreState()
    {
        QTemporaryDir security;
        QVERIFY(security.isValid());
        RadioModel station;
        station.setBoardForTest(HPSDRHW::HermesLite);
        RadioInfo info;
        info.macAddress = "AA:BB:CC:DD:EE:01";
        info.boardType = HPSDRHW::HermesLite;
        station.setLastRadioInfoForTest(info);
        const int id = station.addSlice();
        auto* coreSlice = station.sliceById(id);
        StationServer server(&station, AppSettings::instance(), NereusSDR::Test::seedUpgradedCoreToken(security.path()));
        RadioModel gui(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient client(&gui, &proxy);
        auto* coreEnd = new HoldingTransport;
        auto* guiEnd = new LoopbackTransport("gui");
        coreEnd->linkTo(guiEnd);
        client.startSession(guiEnd, server.token());
        server.acceptTransport(coreEnd);
        QTRY_VERIFY(client.isHandshakeComplete());
        auto* slice = gui.sliceById(id);
        QVERIFY(slice);
        coreEnd->hold = true;
        slice->setNnrAlpha(1.75);
        QTRY_COMPARE(coreEnd->held.size(), 1);
        QCOMPARE(coreSlice->nnrAlpha(), 1.75);
        slice->setNnrAlpha(2.25);
        coreEnd->releaseFirst();
        QTRY_COMPARE(coreSlice->nnrAlpha(), 2.25);
        QCOMPARE(slice->nnrAlpha(), 2.25);
        QTRY_COMPARE(coreEnd->held.size(), 1);
        coreEnd->releaseFirst();
        QTRY_COMPARE(slice->nnrAlpha(), 2.25);

        coreSlice->setNnrSettingsApplier([](const NnrSettings&, QString* reason) -> std::optional<NnrSettings> {
            *reason = "Injected unavailable model";
            return std::nullopt;
        });
        coreEnd->hold = false;
        QSignalSpy completed(&client, &StationClient::propertyWriteCompleted);
        slice->setNnrAlpha(3.0);
        QTRY_COMPARE(slice->nnrAlpha(), 2.25);
        QTRY_VERIFY(!completed.isEmpty());
        QVERIFY(!completed.last().at(3).toBool());
        QVERIFY(!slice->nnrLastError().isEmpty());
        station.flushPendingSettingsSave();
        QCOMPARE(AppSettings::instance().value(coreSlice->nnrSettingsPrefix() + "NnrAlpha").toDouble(), 2.25);
    }

    void protocolMinorAloneDoesNotEnablePropertyResults()
    {
        QTemporaryDir security;
        QVERIFY(security.isValid());
        RadioModel station;
        station.setBoardForTest(HPSDRHW::HermesLite);
        const int id = station.addSlice();
        StationServer server(&station, AppSettings::instance(), NereusSDR::Test::seedUpgradedCoreToken(security.path()));
        RadioModel gui(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient client(&gui, &proxy);
        auto* coreEnd = new HoldingTransport;
        coreEnd->advertisePropertyResults = false;
        auto* guiEnd = new LoopbackTransport("gui");
        coreEnd->linkTo(guiEnd);
        client.startSession(guiEnd, server.token());
        server.acceptTransport(coreEnd);
        QTRY_VERIFY(client.isHandshakeComplete());
        QVERIFY(!client.nnrControlAvailable());
        auto* slice = gui.sliceById(id);
        QVERIFY(slice);
        slice->setAfGain(37);
        QTRY_COMPARE(station.sliceById(id)->afGain(), 37);
        station.sliceById(id)->setAfGain(54);
        QTRY_COMPARE(slice->afGain(), 54);
    }

    void acceptedRemoteSettingsSurviveCoreModelRestartAndStaleGuiHydration()
    {
        QTemporaryDir security;
        QVERIFY(security.isValid());
        auto& settings = AppSettings::instance();
        RadioInfo info;
        info.macAddress = "AA:BB:CC:DD:EE:09";
        info.boardType = HPSDRHW::HermesLite;
        settings.setLastConnected(info.macAddress);

        for (int epoch = 0; epoch < 2; ++epoch) {
            if (epoch == 1) {
                // Recreate Core's settings/model boundary from its atomic file,
                // after the first station and GUI have both been destroyed.
                settings.clear();
                settings.load();
            }
            RadioModel station;
            station.setBoardForTest(info.boardType);
            station.setLastRadioInfoForTest(info);
            station.setConnectionStateForTest(ConnectionState::Connected);
            const int aId = station.addSlice();
            const int bId = station.addSlice();
            QVERIFY(aId >= 0 && bId >= 0 && aId != bId);
            auto* coreA = station.sliceById(aId);
            auto* coreB = station.sliceById(bId);
            StationServer server(&station, settings, NereusSDR::Test::seedUpgradedCoreToken(security.path()));
            RadioModel gui(RadioModel::Role::Remote);
            // Stale client preferences must lose to the startup snapshot.
            gui.pureSignalSettings()->setMoxDelaySeconds(9.0);
            gui.pureSignalSettings()->setLoopDelaySeconds(80.0);
            SettingsProxy proxy;
            StationClient client(&gui, &proxy);
            auto* coreEnd = new LoopbackTransport("station");
            auto* guiEnd = new LoopbackTransport("gui");
            coreEnd->linkTo(guiEnd);
            client.startSession(guiEnd, server.token());
            server.acceptTransport(coreEnd);
            QTRY_VERIFY(client.isHandshakeComplete());
            auto* a = gui.sliceById(aId);
            auto* b = gui.sliceById(bId);
            QVERIFY(a && b);
            if (epoch == 0) {
                a->setNnrAlpha(1.75);
                b->setNnrAlpha(2.25);
                b->setNnrReleaseMs(83.5);
                gui.pureSignalSettings()->setMoxDelaySeconds(0.4);
                gui.pureSignalSettings()->setLoopDelaySeconds(17.25);
                gui.pureSignalSettings()->setAutoCalEnabled(true);
                QTRY_COMPARE(coreA->nnrAlpha(), 1.75);
                QTRY_COMPARE(coreB->nnrAlpha(), 2.25);
                QTRY_COMPARE(coreB->nnrReleaseMs(), 83.5);
                QTRY_COMPARE(station.pureSignalSettings()->moxDelaySeconds(), 0.4);
                QTRY_COMPARE(station.pureSignalSettings()->loopDelaySeconds(), 17.25);
                QTRY_VERIFY(station.pureSignalSettings()->autoCalEnabled());
                coreB->setNnrSettingsApplier([](const NnrSettings&, QString* reason)
                    -> std::optional<NnrSettings> {
                    *reason = "Injected unavailable model";
                    return std::nullopt;
                });
                b->setNnrAlpha(3.75);
                QTRY_COMPARE(b->nnrAlpha(), 2.25);
                // This is the same synchronous flush used by daemon shutdown;
                // no debounce timer or dialog close is needed to keep the edit.
                station.flushPendingSettingsSave();
                QVERIFY(station.settingsSaveError().isEmpty());
            } else {
                QCOMPARE(coreA->nnrAlpha(), 1.75);
                QCOMPARE(coreB->nnrAlpha(), 2.25);
                QCOMPARE(coreB->nnrReleaseMs(), 83.5);
                QCOMPARE(a->nnrAlpha(), 1.75);
                QCOMPARE(b->nnrAlpha(), 2.25);
                QCOMPARE(b->nnrReleaseMs(), 83.5);
                QCOMPARE(station.pureSignalSettings()->moxDelaySeconds(), 0.4);
                QCOMPARE(station.pureSignalSettings()->loopDelaySeconds(), 17.25);
                QCOMPARE(gui.pureSignalSettings()->moxDelaySeconds(), 0.4);
                QCOMPARE(gui.pureSignalSettings()->loopDelaySeconds(), 17.25);
                QVERIFY(gui.pureSignalSettings()->autoCalEnabled());
                for (const auto& wire : coreEnd->received()) {
                    SessionMessage message;
                    QVERIFY(SessionMessages::decode(wire, &message));
                    // Record subscriptions and the automatic settings
                    // validation only read Core state. Neither can hydrate
                    // stale client preferences back into the Core.
                    QVERIFY2(message.kind != SessionMessageKind::CommandInvoke
                                 || message.commandVerb == "records.subscribe"
                                 || message.commandVerb == "station.validateSettings",
                             message.commandVerb.constData());
                    QVERIFY(message.kind != SessionMessageKind::PropertyWrite);
                }
            }
            client.disconnectFromStation("Restart fixture complete");
        }
    }

    void rawSettingsCannotBypassNormalModelValidation()
    {
        auto& settings = AppSettings::instance();
        const QString key = "hardware/AA:BB:CC:DD:EE:01/slices/7/nnr/NnrAlpha";
        settings.setValue(key, 1.75);
        SettingsProxyServer server(settings);
        const auto result = server.applyInboundWrite(key, 99.0, "test");
        QVERIFY(!result.accepted);
        QVERIFY2(result.reason.contains("their own controls"), qPrintable(result.reason));
        QCOMPARE(settings.value(key).toDouble(), 1.75);
    }

    // Inbound sibling lane: a Core mode change lands while the operator's
    // filter edge is still waiting for the window's flush. The mode's
    // side effect (SliceModel::setDspMode moves both edges) must not
    // replace the operator's edge, and the flush must send the operator's.
    void coreModeChangeKeepsTheOperatorsUnsentFilterEdge()
    {
        QTemporaryDir security;
        QVERIFY(security.isValid());
        RadioModel station;
        station.setBoardForTest(HPSDRHW::HermesLite);
        RadioInfo info;
        info.macAddress = "AA:BB:CC:DD:EE:01";
        info.boardType = HPSDRHW::HermesLite;
        station.setLastRadioInfoForTest(info);
        const int id = station.addSlice();
        auto* coreSlice = station.sliceById(id);
        coreSlice->setDspMode(DSPMode::USB);
        StationServer server(&station, AppSettings::instance(), NereusSDR::Test::seedUpgradedCoreToken(security.path()));
        RadioModel gui(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient client(&gui, &proxy);
        auto* coreEnd = new HoldingTransport;
        auto* guiEnd = new LoopbackTransport("gui");
        coreEnd->linkTo(guiEnd);
        client.startSession(guiEnd, server.token());
        server.acceptTransport(coreEnd);
        QTRY_VERIFY(client.isHandshakeComplete());
        auto* slice = gui.sliceById(id);
        QVERIFY(slice);
        QCOMPARE(slice->dspMode(), DSPMode::USB);
        client.pauseWriteFlushForTest();

        // The operator's edge, not yet sent.
        const int operatorLow = slice->filterLow() + 170;
        const int operatorHigh = slice->filterHigh();
        slice->setFilterLow(operatorLow);
        QCOMPARE(slice->filterLow(), operatorLow);

        // The Core changes mode before the window's next flush.
        coreSlice->setDspMode(DSPMode::CWU);
        QVERIFY(coreSlice->filterLow() != operatorLow);
        QTRY_COMPARE(slice->dspMode(), DSPMode::CWU);

        // The flush sends the operator's edges, and the Core keeps its mode.
        client.flushWritesForTest();
        QTRY_COMPARE(coreSlice->filterLow(), operatorLow);
        QCOMPARE(coreSlice->filterHigh(), operatorHigh);
        QCOMPARE(coreSlice->dspMode(), DSPMode::CWU);
        // And the window showed them throughout.
        QCOMPARE(slice->dspMode(), DSPMode::CWU);
        QCOMPARE(slice->filterLow(), operatorLow);
        QCOMPARE(slice->filterHigh(), operatorHigh);
    }

    // The same side effect on an edge already SENT and not yet answered.
    // The two edges share filterChanged, so the operator's next edge
    // re-reads both: the one in flight must still be the operator's, not
    // the edge the Core's mode change left on the window.
    void coreModeChangeKeepsTheOperatorsUnansweredFilterEdge()
    {
        QTemporaryDir security;
        QVERIFY(security.isValid());
        RadioModel station;
        station.setBoardForTest(HPSDRHW::HermesLite);
        RadioInfo info;
        info.macAddress = "AA:BB:CC:DD:EE:01";
        info.boardType = HPSDRHW::HermesLite;
        station.setLastRadioInfoForTest(info);
        const int id = station.addSlice();
        auto* coreSlice = station.sliceById(id);
        coreSlice->setDspMode(DSPMode::USB);
        StationServer server(&station, AppSettings::instance(), NereusSDR::Test::seedUpgradedCoreToken(security.path()));
        RadioModel gui(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient client(&gui, &proxy);
        auto* coreEnd = new HoldingTransport;
        auto* guiEnd = new LoopbackTransport("gui");
        coreEnd->linkTo(guiEnd);
        client.startSession(guiEnd, server.token());
        server.acceptTransport(coreEnd);
        QTRY_VERIFY(client.isHandshakeComplete());
        auto* slice = gui.sliceById(id);
        QVERIFY(slice);
        QCOMPARE(slice->dspMode(), DSPMode::USB);
        client.pauseWriteFlushForTest();

        // The operator's edge leaves the window and is still on its way.
        const int operatorLow = slice->filterLow() + 170;
        slice->setFilterLow(operatorLow);
        guiEnd->setHoldsOutgoing(true);
        client.flushWritesForTest();

        // The Core changes mode first; its delta reaches the window.
        coreSlice->setDspMode(DSPMode::CWU);
        QVERIFY(coreSlice->filterLow() != operatorLow);
        QTRY_COMPARE(slice->dspMode(), DSPMode::CWU);

        // The operator's next edge re-reads both edges.
        const int operatorHigh = slice->filterHigh() + 230;
        slice->setFilterHigh(operatorHigh);
        guiEnd->setHoldsOutgoing(false);
        client.flushWritesForTest();

        QTRY_COMPARE(coreSlice->filterHigh(), operatorHigh);
        QCOMPARE(coreSlice->filterLow(), operatorLow);
        QCOMPARE(coreSlice->dspMode(), DSPMode::CWU);
        QCOMPARE(slice->filterLow(), operatorLow);
        QCOMPARE(slice->filterHigh(), operatorHigh);
    }

    // Another row: TransmitModel::setLineInBoost sets lineInGain to the
    // boost's index. A Core boost change landing before the window sends
    // the operator's own line-in gain must not replace that gain.
    void coreLineInBoostKeepsTheOperatorsUnsentLineInGain()
    {
        QTemporaryDir security;
        QVERIFY(security.isValid());
        RadioModel station;
        station.setBoardForTest(HPSDRHW::HermesLite);
        RadioInfo info;
        info.macAddress = "AA:BB:CC:DD:EE:01";
        info.boardType = HPSDRHW::HermesLite;
        station.setLastRadioInfoForTest(info);
        station.addSlice();
        TransmitModel& coreTx = station.transmitModel();
        coreTx.setLineInBoost(0.0);
        StationServer server(&station, AppSettings::instance(), NereusSDR::Test::seedUpgradedCoreToken(security.path()));
        RadioModel gui(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient client(&gui, &proxy);
        auto* coreEnd = new HoldingTransport;
        auto* guiEnd = new LoopbackTransport("gui");
        coreEnd->linkTo(guiEnd);
        client.startSession(guiEnd, server.token());
        server.acceptTransport(coreEnd);
        QTRY_VERIFY(client.isHandshakeComplete());
        TransmitModel& windowTx = gui.transmitModel();
        QCOMPARE(windowTx.lineInGain(), coreTx.lineInGain());
        client.pauseWriteFlushForTest();

        // The operator's gain, not yet sent.
        const int operatorGain = 7;
        QVERIFY(windowTx.lineInGain() != operatorGain);
        windowTx.setLineInGain(operatorGain);

        // The Core's boost changes before the window's next flush.
        coreTx.setLineInBoost(6.0);
        QVERIFY(coreTx.lineInGain() != operatorGain);
        QTRY_COMPARE(windowTx.lineInBoost(), 6.0);

        client.flushWritesForTest();
        QTRY_COMPARE(coreTx.lineInGain(), operatorGain);
        QCOMPARE(coreTx.lineInBoost(), 6.0);
        QCOMPARE(windowTx.lineInGain(), operatorGain);
        QCOMPARE(windowTx.lineInBoost(), 6.0);
    }
};

QTEST_MAIN(TestSessionPropertyResult)
#include "tst_session_property_result.moc"
