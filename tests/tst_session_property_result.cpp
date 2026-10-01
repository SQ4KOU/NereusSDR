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
#include "models/Band.h"
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
// PropertyWrite messages among what a transport's outboundText spy saw.
int propertyWrites(const QSignalSpy& sent)
{
    int count = 0;
    for (const auto& args : sent) {
        SessionMessage message;
        if (SessionMessages::decode(args.at(0).toByteArray(), &message)
            && message.kind == SessionMessageKind::PropertyWrite) {
            ++count;
        }
    }
    return count;
}
// A Core with one USB slice and a window on it, write flush paused.
struct SliceSession {
    QTemporaryDir security;
    RadioModel station;
    SliceModel* coreSlice = nullptr;
    std::unique_ptr<StationServer> server;
    RadioModel gui{RadioModel::Role::Remote};
    SettingsProxy proxy;
    std::unique_ptr<StationClient> client;
    LoopbackTransport* guiEnd = nullptr;
    SliceModel* slice = nullptr;
};
// The per-(band, mode) LastFilter key prefix SliceModel::setDspMode uses.
QString lastFilterPrefix(const SliceModel* slice, DSPMode mode)
{
    return QStringLiteral("Slice%1/Band%2/Mode%3/")
        .arg(slice->sliceIndex())
        .arg(bandKeyName(bandFromFrequency(slice->frequency())))
        .arg(SliceModel::modeName(mode));
}
MirrorUpdate real(const char* name, double value)
{
    return {0, name, MirrorWireKind::Float64, value};
}
}

class TestSessionPropertyResult : public QObject {
    Q_OBJECT
private:
    void startSliceSession(SliceSession& s)
    {
        QVERIFY(s.security.isValid());
        s.station.setBoardForTest(HPSDRHW::HermesLite);
        RadioInfo info;
        info.macAddress = "AA:BB:CC:DD:EE:01";
        info.boardType = HPSDRHW::HermesLite;
        s.station.setLastRadioInfoForTest(info);
        const int id = s.station.addSlice();
        s.coreSlice = s.station.sliceById(id);
        s.coreSlice->setDspMode(DSPMode::USB);
        s.server = std::make_unique<StationServer>(
            &s.station, AppSettings::instance(),
            NereusSDR::Test::seedUpgradedCoreToken(s.security.path()));
        s.client = std::make_unique<StationClient>(&s.gui, &s.proxy);
        auto* coreEnd = new HoldingTransport;
        s.guiEnd = new LoopbackTransport("gui");
        coreEnd->linkTo(s.guiEnd);
        s.client->startSession(s.guiEnd, s.server->token());
        s.server->acceptTransport(coreEnd);
        QTRY_VERIFY(s.client->isHandshakeComplete());
        s.slice = s.gui.sliceById(id);
        QVERIFY(s.slice);
        QCOMPARE(s.slice->dspMode(), DSPMode::USB);
        s.client->pauseWriteFlushForTest();
    }

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

    // Inbound sibling lane, fix round 1: a Core mode change lands while
    // the operator's filter edge is still waiting for the window's flush.
    // Thetis keeps filter edges per mode (SetRX1Mode loads the new mode's
    // own LastFilter, console.cs:34513 [v2.10.3.15]), so the unsent edge
    // from the old mode is dropped: the window shows the new mode's
    // filter and sends no write for it.
    void coreModeChangeDropsTheOperatorsUnsentFilterEdge()
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
        slice->setFilterLow(operatorLow);
        QCOMPARE(slice->filterLow(), operatorLow);

        // The Core changes mode before the window's next flush.
        coreSlice->setDspMode(DSPMode::CWU);
        // The "differs" guard relies on the Core and the window sharing one settings store in-process.
        QVERIFY(coreSlice->filterLow() != operatorLow);
        const int coreLow = coreSlice->filterLow();
        const int coreHigh = coreSlice->filterHigh();
        QTRY_COMPARE(slice->dspMode(), DSPMode::CWU);

        // The window shows the new mode's filter, not the old mode's edge.
        QCOMPARE(slice->filterLow(), coreLow);
        QCOMPARE(slice->filterHigh(), coreHigh);

        // And the flush sends no write for it: the edit was dropped.
        QSignalSpy sent(guiEnd, &LoopbackTransport::outboundText);
        client.flushWritesForTest();
        QCOMPARE(propertyWrites(sent), 0);
        QCOMPARE(coreSlice->dspMode(), DSPMode::CWU);
        QCOMPARE(coreSlice->filterLow(), coreLow);
        QCOMPARE(coreSlice->filterHigh(), coreHigh);
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
        // The "differs" guard relies on the Core and the window sharing one settings store in-process.
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
    // the operator's own line-in gain is a change from elsewhere, so the
    // unsent gain is dropped (fix round 1): the window shows the gain the
    // Core's boost set and sends no write for it.
    void coreLineInBoostDropsTheOperatorsUnsentLineInGain()
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
        const int coreGain = coreTx.lineInGain();
        QTRY_COMPARE(windowTx.lineInBoost(), 6.0);

        // The window shows the Core's gain, not the operator's.
        QCOMPARE(windowTx.lineInGain(), coreGain);

        // And the flush sends no write for it.
        QSignalSpy sent(guiEnd, &LoopbackTransport::outboundText);
        client.flushWritesForTest();
        QCOMPARE(propertyWrites(sent), 0);
        QCOMPARE(coreTx.lineInGain(), coreGain);
        QCOMPARE(coreTx.lineInBoost(), 6.0);
    }

    // Fix round 2, I1: the operator drags, the drag is sent (W1, still on
    // its way), drags again (unsent), and then a Core mode change cancels
    // the unsent drag. The window falls back to W1, whose answer applies,
    // so the window ends where the Core is. The Core never sends its
    // writer a correction of its own write.
    void coreModeChangeAfterAReEditEndsWhereTheCoreIs()
    {
        SliceSession s;
        startSliceSession(s);
        if (QTest::currentTestFailed()) { return; }

        const int firstLow = s.slice->filterLow() + 170;
        s.slice->setFilterLow(firstLow);
        s.guiEnd->setHoldsOutgoing(true);
        s.client->flushWritesForTest();
        s.slice->setFilterLow(firstLow + 50);

        s.coreSlice->setDspMode(DSPMode::CWU);
        QTRY_COMPARE(s.slice->dspMode(), DSPMode::CWU);
        // W1 is what the window now shows and still holds.
        QCOMPARE(s.slice->filterLow(), firstLow);

        s.guiEnd->clearReceived();
        s.guiEnd->setHoldsOutgoing(false);
        QTRY_COMPARE(s.coreSlice->filterLow(), firstLow);
        QTRY_VERIFY(s.guiEnd->receivedKinds().contains("property.result"));
        QCOMPARE(s.slice->filterLow(), s.coreSlice->filterLow());
        QCOMPARE(s.slice->filterHigh(), s.coreSlice->filterHigh());

        QSignalSpy sent(s.guiEnd, &LoopbackTransport::outboundText);
        s.client->flushWritesForTest();
        QCOMPARE(propertyWrites(sent), 0);
    }

    // Fix round 2, I3: the delta that cancels the unsent edge carries an
    // edge of its own that is not the window's CW memory. That value is
    // what the window shows.
    void coreModeChangeShowsTheDeltasOwnFilterEdge()
    {
        SliceSession s;
        startSliceSession(s);
        if (QTest::currentTestFailed()) { return; }

        const int operatorLow = s.slice->filterLow() + 170;
        s.slice->setFilterLow(operatorLow);

        // Mode and edge change before the Core's flush: one delta.
        s.coreSlice->setDspMode(DSPMode::CWU);
        const int memoryLow = s.coreSlice->filterLow();
        const int coreLow = memoryLow - 60;
        QVERIFY(coreLow != operatorLow);
        s.coreSlice->setFilterLow(coreLow);
        QTRY_COMPARE(s.slice->dspMode(), DSPMode::CWU);

        QCOMPARE(s.slice->filterLow(), coreLow);
        QCOMPARE(s.slice->filterHigh(), s.coreSlice->filterHigh());
        QSignalSpy sent(s.guiEnd, &LoopbackTransport::outboundText);
        s.client->flushWritesForTest();
        QCOMPARE(propertyWrites(sent), 0);
        QCOMPARE(s.coreSlice->filterLow(), coreLow);
    }

    // Fix round 2, I2: the Core's edges do not change with its mode, so
    // its delta carries none, and the window's own CW memory differs (as
    // on another machine: here the shared store is rewritten between the
    // Core's change and the window's). The window shows the Core's edges,
    // and the cancelled edge is not saved as the window's USB memory.
    void coreModeChangeWithoutEdgesShowsTheCoresEdges()
    {
        SliceSession s;
        startSliceSession(s);
        if (QTest::currentTestFailed()) { return; }
        auto& settings = AppSettings::instance();
        const QString usb = lastFilterPrefix(s.coreSlice, DSPMode::USB);
        const QString cw = lastFilterPrefix(s.coreSlice, DSPMode::CWU);
        const int usbLow = s.coreSlice->filterLow();
        const int usbHigh = s.coreSlice->filterHigh();
        // The Core's CW memory is its USB edges: its mode change moves none.
        settings.setValue(cw + QStringLiteral("FilterLow"), usbLow);
        settings.setValue(cw + QStringLiteral("FilterHigh"), usbHigh);

        const int operatorLow = usbLow + 170;
        s.slice->setFilterLow(operatorLow);

        s.coreSlice->setDspMode(DSPMode::CWU);
        QCOMPARE(s.coreSlice->filterLow(), usbLow);
        QCOMPARE(s.coreSlice->filterHigh(), usbHigh);
        // The window's own CW memory.
        settings.setValue(cw + QStringLiteral("FilterLow"), usbLow + 300);
        settings.setValue(cw + QStringLiteral("FilterHigh"), usbHigh + 300);
        QTRY_COMPARE(s.slice->dspMode(), DSPMode::CWU);

        QCOMPARE(s.slice->filterLow(), usbLow);
        QCOMPARE(s.slice->filterHigh(), usbHigh);
        QCOMPARE(settings.value(usb + QStringLiteral("FilterLow")).toInt(), usbLow);
        QSignalSpy sent(s.guiEnd, &LoopbackTransport::outboundText);
        s.client->flushWritesForTest();
        QCOMPARE(propertyWrites(sent), 0);
        QCOMPARE(s.coreSlice->filterLow(), usbLow);
    }

    // Fix round 2, Minor 1: the operator moved the low edge only. The
    // window's CW memory keeps the old high edge, so the high hold does
    // not move, but it is behind the same notifier as the cancelled low
    // edge and the delta carries the Core's high: it is cancelled too,
    // and the old mode's high is not sent into the new mode.
    void coreModeChangeCancelsTheUnmovedSiblingEdge()
    {
        SliceSession s;
        startSliceSession(s);
        if (QTest::currentTestFailed()) { return; }
        auto& settings = AppSettings::instance();
        const QString cw = lastFilterPrefix(s.coreSlice, DSPMode::CWU);
        const int usbLow = s.coreSlice->filterLow();
        const int usbHigh = s.coreSlice->filterHigh();
        const int cwLow = usbLow + 100;
        const int cwHigh = usbHigh + 200;
        settings.setValue(cw + QStringLiteral("FilterLow"), cwLow);
        settings.setValue(cw + QStringLiteral("FilterHigh"), cwHigh);

        s.slice->setFilterLow(usbLow + 170);

        s.coreSlice->setDspMode(DSPMode::CWU);
        QCOMPARE(s.coreSlice->filterLow(), cwLow);
        QCOMPARE(s.coreSlice->filterHigh(), cwHigh);
        // The window's own CW memory keeps the USB high edge.
        settings.setValue(cw + QStringLiteral("FilterLow"), usbLow + 300);
        settings.setValue(cw + QStringLiteral("FilterHigh"), usbHigh);
        QTRY_COMPARE(s.slice->dspMode(), DSPMode::CWU);

        QCOMPARE(s.slice->filterLow(), cwLow);
        QCOMPARE(s.slice->filterHigh(), cwHigh);
        QSignalSpy sent(s.guiEnd, &LoopbackTransport::outboundText);
        s.client->flushWritesForTest();
        QCOMPARE(propertyWrites(sent), 0);
        QCOMPARE(s.coreSlice->filterHigh(), cwHigh);
    }
};

QTEST_MAIN(TestSessionPropertyResult)
#include "tst_session_property_result.moc"
