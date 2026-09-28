// no-port-check: NereusSDR-original. Task 42 accessory transmit commands.
#include <QtTest>
#include <algorithm>

#include "MultiDeviceHarness.h"
#include "core/session/SessionCommandDispatcher.h"

using namespace NereusSDR;

class AccessoryTxCommandsTest : public QObject {
    Q_OBJECT
private slots:
    void everyVerbHasTheTask42Capability()
    {
        const QList<QByteArray> verbs{"amp.operate", "amp.standby", "tuner.tune",
                                      "tuner.operate", "tuner.bypass", "tuner.antenna",
                                      "rfkit.operate", "rfkit.standby", "rfkit.antenna"};
        for (const QByteArray& verb : verbs) {
            const auto it = std::find_if(SessionCommandDispatcher::verbSpecs().cbegin(),
                                         SessionCommandDispatcher::verbSpecs().cend(),
                                         [&verb](const CommandVerbSpec& spec) {
                                             return spec.verb == verb;
                                         });
            QVERIFY2(it != SessionCommandDispatcher::verbSpecs().cend(), verb.constData());
            QCOMPARE(it->capability, QByteArray("accessoryTxVersion"));
            QCOMPARE(it->capabilityVersion, 1);
        }
    }

    void missingSessionPermissionRefusesBeforeAnyAccessoryAction()
    {
        RadioModel model;
        SessionCommandDispatcher dispatcher(&model);
        QList<SessionMessage> answers;
        QObject::connect(&dispatcher, &SessionCommandDispatcher::commandResultReady,
                         &dispatcher, [&answers](const SessionMessage& message) {
                             answers.append(message);
                         });
        dispatcher.setRequester("phone");
        // The gate is absent here, so no accessory operation is authorized.
        dispatcher.dispatch(SessionMessages::commandInvoke("amp.operate", 1, {}));
        QCOMPARE(answers.size(), 1);
        QVERIFY(!answers.first().accepted);
        QVERIFY(!answers.first().reason.isEmpty());
        QVERIFY(std::any_of(answers.first().updates.cbegin(), answers.first().updates.cend(),
                            [](const MirrorUpdate& update) {
                                return update.name == "refusalCode";
                            }));
    }

    void idleHolderIsAskedBeforeAnotherDeviceSwitchesTheAmp()
    {
        Core core(true);
        core.model->enableStationAccessoryIdentity();
        Device a(QStringLiteral("iPhone"), QStringLiteral("phone"));
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        allowTransmit(core);
        LoopbackTransport* appA = core.signIn(a, kTransmitter);
        LoopbackTransport* appB = core.signIn(b, kTransmitter);
        QVERIFY(admitted(appA));
        QVERIFY(admitted(appB));
        QVERIFY(capability(appB->received(), QStringLiteral("accessoryTxVersion")).has_value());
        QCOMPARE(*capability(appB->received(), QStringLiteral("accessoryTxVersion")), qint64(1));
        const QJsonObject take = core.invoke(appA, "tx.take");
        QVERIFY2(take.value(QStringLiteral("accepted")).toBool(),
                 qPrintable(take.value(QStringLiteral("reason")).toString()));
        const QJsonObject change = core.invoke(appB, "amp.operate");
        QVERIFY(!change.value(QStringLiteral("accepted")).toBool());
        QCOMPARE(change.value(QStringLiteral("reason")).toString(),
                 QStringLiteral("Waiting for you to confirm."));
        const QJsonObject question = firstOfType(appB->received(), QStringLiteral("confirm.request"));
        QVERIFY(!question.isEmpty());
    }

    void oldAndNewAccessoryVerbsRequireRemoteTransmitPermission()
    {
        Core core(true);
        core.model->enableStationAccessoryIdentity();
        Device app(QStringLiteral("iPhone"), QStringLiteral("phone"));
        core.pair(app);
        LoopbackTransport* peer = core.signIn(app, kTransmitter);
        QVERIFY(admitted(peer));
        const QList<MirrorUpdate> on{{0, "on", MirrorWireKind::Bool, true}};
        const QList<MirrorUpdate> port{{0, "port", MirrorWireKind::Int64, qint64(1)}};
        const QList<QPair<QByteArray, QList<MirrorUpdate>>> commands{
            {"amp.operate", {}}, {"setPgxlOperate", on},
            {"tuner.operate", on}, {"setTgxlOperate", on},
            {"tuner.bypass", on}, {"setTgxlBypass", on},
            {"tuner.antenna", port}, {"setTgxlAntenna", port},
            {"rfkit.operate", {}}, {"setRfKitOperate", on},
            {"rfkit.antenna", port}, {"setRfKitAntenna", port}};
        for (const auto& [verb, args] : commands) {
            const QJsonObject result = core.invoke(peer, verb, args);
            QVERIFY2(!result.value(QStringLiteral("accepted")).toBool(), verb.constData());
            QCOMPARE(result.value(QStringLiteral("reason")).toString(),
                     QStringLiteral("This Core is set to receive only."));
            if (!verb.contains('.')) {
                QVERIFY(result.value(QStringLiteral("values")).toArray().isEmpty());
            }
        }
    }

    void legacyAliasesReachAccessoryChecksWhenRemoteTransmitIsAllowed()
    {
        Core core(true);
        core.model->enableStationAccessoryIdentity();
        Device app(QStringLiteral("iPhone"), QStringLiteral("phone"));
        core.pair(app);
        allowTransmit(core);
        LoopbackTransport* peer = core.signIn(app, kTransmitter);
        QVERIFY(admitted(peer));
        const QList<MirrorUpdate> on{{0, "on", MirrorWireKind::Bool, true}};
        const QList<MirrorUpdate> port{{0, "port", MirrorWireKind::Int64, qint64(1)}};
        const QList<QPair<QByteArray, QList<MirrorUpdate>>> commands{
            {"setPgxlOperate", on}, {"setTgxlOperate", on},
            {"setTgxlBypass", on}, {"setTgxlAntenna", port},
            {"setRfKitOperate", on}, {"setRfKitAntenna", port}};
        for (const auto& [verb, args] : commands) {
            const QJsonObject result = core.invoke(peer, verb, args);
            QVERIFY2(!result.value(QStringLiteral("accepted")).toBool(), verb.constData());
            QVERIFY2(result.value(QStringLiteral("reason")).toString()
                         != QStringLiteral("This Core is set to receive only."), verb.constData());
        }
    }

    void anOnAirHolderRefusesAnotherDevicesSwitch()
    {
        Core core(true);
        core.model->enableStationAccessoryIdentity();
        Device a(QStringLiteral("iPhone"), QStringLiteral("phone"));
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        allowTransmit(core);
        LoopbackTransport* holder = core.signIn(a, kTransmitter);
        LoopbackTransport* other = core.signIn(b, kTransmitter);
        QVERIFY(admitted(holder) && admitted(other));
        const QJsonObject key = core.invoke(holder, "tx.key",
                                            {MirrorUpdate{0, "trigger", MirrorWireKind::Utf8,
                                                          QStringLiteral("screen")}});
        QVERIFY2(key.value(QStringLiteral("accepted")).toBool(),
                 qPrintable(key.value(QStringLiteral("reason")).toString()));
        const QJsonObject refused = core.invoke(other, "amp.operate");
        QVERIFY(!refused.value(QStringLiteral("accepted")).toBool());
        QVERIFY(refused.value(QStringLiteral("reason")).toString().contains(
            QStringLiteral("on the air")));
        const QJsonObject oldRefused = core.invoke(
            other, "setPgxlOperate", {{0, "on", MirrorWireKind::Bool, true}});
        QVERIFY(!oldRefused.value(QStringLiteral("accepted")).toBool());
        QVERIFY(oldRefused.value(QStringLiteral("reason")).toString().contains(
            QStringLiteral("on the air")));
        QVERIFY(ofType(other->received(), QStringLiteral("confirm.request")).isEmpty());
    }

    void receiveOnlyEnabledBeforeProceedRefusesHeldSwitch()
    {
        Core core(true);
        core.model->enableStationAccessoryIdentity();
        Device a(QStringLiteral("iPhone"), QStringLiteral("phone"));
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        allowTransmit(core);
        LoopbackTransport* holder = core.signIn(a, kTransmitter);
        LoopbackTransport* other = core.signIn(b, kTransmitter);
        QVERIFY(admitted(holder) && admitted(other));
        QVERIFY(core.invoke(holder, "tx.take").value(QStringLiteral("accepted")).toBool());
        QCOMPARE(core.invoke(other, "amp.operate").value(QStringLiteral("reason")).toString(),
                 QStringLiteral("Waiting for you to confirm."));
        const QJsonObject question = firstOfType(other->received(), QStringLiteral("confirm.request"));
        QVERIFY(!question.isEmpty());
        core.server->setRemoteTransmitAllowed(false);
        const QJsonObject refused = core.invoke(
            other, "confirm.proceed",
            {MirrorUpdate{0, "id", MirrorWireKind::Int64,
                          question.value(QStringLiteral("id")).toInteger()},
             MirrorUpdate{0, "choice", MirrorWireKind::Int64, qint64(-1)}});
        QVERIFY(!refused.value(QStringLiteral("accepted")).toBool());
        QCOMPARE(refused.value(QStringLiteral("reason")).toString(),
                 QStringLiteral("This Core is set to receive only."));
    }

    void tunerTuneWithoutAConnectedTunerDoesNotTakeOrKeyTransmit()
    {
        Core core(true);
        core.model->enableStationAccessoryIdentity();
        Device app(QStringLiteral("iPhone"), QStringLiteral("phone"));
        core.pair(app);
        allowTransmit(core);
        LoopbackTransport* peer = core.signIn(app, kTransmitter);
        QVERIFY(admitted(peer));
        QVERIFY(!core.server->transmitHolder()->holder().has_value());
        const QJsonObject refused = core.invoke(peer, "tuner.tune");
        QVERIFY(!refused.value(QStringLiteral("accepted")).toBool());
        QCOMPARE(refused.value(QStringLiteral("reason")).toString(),
                 QStringLiteral("No Tuner Genius is connected to the Core."));
        QVERIFY(!core.server->transmitHolder()->holder().has_value());
        QVERIFY(!core.model->moxController()->isMox());
    }

    void holderKeyingAfterQuestionRefusesProceed()
    {
        Core core(true);
        core.model->enableStationAccessoryIdentity();
        Device a(QStringLiteral("iPhone"), QStringLiteral("phone"));
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        allowTransmit(core);
        LoopbackTransport* holder = core.signIn(a, kTransmitter);
        LoopbackTransport* other = core.signIn(b, kTransmitter);
        QVERIFY(admitted(holder) && admitted(other));
        QVERIFY(core.invoke(holder, "tx.take").value(QStringLiteral("accepted")).toBool());
        QCOMPARE(core.invoke(other, "amp.operate").value(QStringLiteral("reason")).toString(),
                 QStringLiteral("Waiting for you to confirm."));
        const QJsonObject question = firstOfType(other->received(), QStringLiteral("confirm.request"));
        QVERIFY(!question.isEmpty());
        QVERIFY(core.invoke(holder, "tx.key",
                            {{0, "trigger", MirrorWireKind::Utf8, QStringLiteral("screen")}})
                    .value(QStringLiteral("accepted")).toBool());
        const QJsonObject refused = core.invoke(
            other, "confirm.proceed",
            {{0, "id", MirrorWireKind::Int64, question.value(QStringLiteral("id")).toInteger()},
             {0, "choice", MirrorWireKind::Int64, qint64(-1)}});
        QVERIFY(!refused.value(QStringLiteral("accepted")).toBool());
        QVERIFY(refused.value(QStringLiteral("reason")).toString().contains(
            QStringLiteral("on the air")));
    }
};

QTEST_GUILESS_MAIN(AccessoryTxCommandsTest)
#include "tst_accessory_tx_commands.moc"
