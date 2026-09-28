// no-port-check: NereusSDR-original live-apply checks for described settings.
#include <QtTest>

#include "core/AppSettings.h"
#include "core/FreeDVReporterClient.h"
#include "core/settings/SettingsProxyServer.h"
#include "core/session/SessionCommandDispatcher.h"
#include "models/RadioModel.h"
#include "models/TransmitModel.h"
#include "core/setup/SetupDescriptionService.h"
#include "core/accessories/AlexAntennaFacade.h"
#include "core/accessories/AlexController.h"
#include "core/StepAttenuatorController.h"
#include "core/session/TransmitStateFacade.h"
#include "MultiDeviceHarness.h"

using namespace NereusSDR;

class SetupDescriptionLiveTest : public QObject {
    Q_OBJECT
private slots:
    void pairedV3SettingsValidationPanelUsesExistingHygieneCapability()
    {
        Core core;
        Device phone(QStringLiteral("Settings Validation iPhone"), QStringLiteral("phone"));
        core.pair(phone);
        QHash<QByteArray, int> features = kHolder;
        features.insert("setupDescription", 3);
        features.insert("settingsHygiene", 1);
        LoopbackTransport* app = core.signIn(phone, features);
        QVERIFY(admitted(app));
        QCOMPARE(capability(app->received(), QStringLiteral("setupDescriptionVersion")),
                 std::optional<qint64>(3));
        QCOMPARE(capability(app->received(), QStringLiteral("settingsHygieneVersion")),
                 std::optional<qint64>(1));
        const QJsonObject diagnostics = QJsonDocument::fromJson(latest(app->received(),
            QStringLiteral("setup"), QStringLiteral("diagnostics")).toString().toUtf8()).object();
        QCOMPARE(diagnostics.value("version"), QJsonValue(3));
        const QJsonObject panel = diagnostics.value("pages").toArray().first().toObject()
            .value("sections").toArray().first().toObject()
            .value("controls").toArray().first().toObject();
        QVERIFY(SetupDescriptionService::validateSettingsHygienePanel(panel));
        const QJsonObject validated = core.invoke(app, "station.validateSettings",
            {MirrorUpdate{0, "mac", MirrorWireKind::Utf8, core.model->currentRadioMac()}});
        QVERIFY2(validated.value("accepted").toBool(),
                 qPrintable(validated.value("reason").toString()));
        QCOMPARE(validated.value("values").toArray().size(), 2);
        const QJsonObject noReset = core.invoke(app, "station.resetSettings",
            {MirrorUpdate{0, "mac", MirrorWireKind::Utf8, core.model->currentRadioMac()}});
        QVERIFY(!noReset.value("accepted").toBool(true));

        Device withoutHygiene(QStringLiteral("Older Settings iPhone"), QStringLiteral("phone"));
        core.pair(withoutHygiene);
        QHash<QByteArray, int> descriptionOnly = kHolder;
        descriptionOnly.insert("setupDescription", 3);
        LoopbackTransport* older = core.signIn(withoutHygiene, descriptionOnly);
        QVERIFY(admitted(older));
        QVERIFY(!capability(older->received(), QStringLiteral("settingsHygieneVersion"))
                     .value_or(0));
        const QJsonObject gated = core.invoke(older, "station.validateSettings",
            {MirrorUpdate{0, "mac", MirrorWireKind::Utf8, core.model->currentRadioMac()}});
        QVERIFY(!gated.value("accepted").toBool(true));
    }

    void pairedPaBypassSettingUsesExistingCoreAuthority()
    {
        Core core;
        core.model->setHpsdrModelForTest(HPSDRModel::ANAN_G2E);
        RadioInfo info = core.model->currentRadioInfo();
        info.boardType = HPSDRHW::HermesC10;
        core.model->setLastRadioInfoForTest(info);
        core.model->setReceiveOnlyStationPolicy(true);
        core.server->setupDescription()->setRadioContext(core.model->boardCapabilities(),
                                                         core.model->hardwareProfile().model);
        Device settingsPhone(QStringLiteral("PA settings phone"), QStringLiteral("phone"));
        core.pair(settingsPhone);
        QHash<QByteArray, int> features = kHolder;
        features.insert("setupDescription", 1);
        LoopbackTransport* app = core.signIn(settingsPhone, features);
        QVERIFY(admitted(app));
        QCOMPARE(capability(app->received(), QStringLiteral("transmitSettingsVersion")),
                 std::optional<qint64>(9));
        const QJsonObject pa = QJsonDocument::fromJson(latest(app->received(),
            QStringLiteral("setup"), QStringLiteral("pa")).toString().toUtf8()).object();
        QCOMPARE(pa.value("pages").toArray().size(), 2);
        const QJsonObject control = pa.value("pages").toArray().first().toObject()
            .value("sections").toArray().first().toObject()
            .value("controls").toArray().first().toObject();
        QVERIFY(SetupDescriptionService::validatePaBypassBinding(control));
        QVERIFY(!txPermitted(app));
        QCOMPARE(core.model->transmitModel().paSettingsBypass(), false);
        Device txPhone(QStringLiteral("Denied TX phone"), QStringLiteral("phone"));
        core.pair(txPhone);
        QHash<QByteArray, int> txFeatures = kTransmitter;
        txFeatures.insert("setupDescription", 1);
        LoopbackTransport* denied = core.signIn(txPhone, txFeatures);
        QVERIFY(admitted(denied));
        QVERIFY(!txPermitted(denied));

        qint64 writeId = 970;
        const auto write = [&writeId](LoopbackTransport* peer, bool bypass) {
            const qint64 id = ++writeId;
            peer->sendText(SessionMessages::encode(SessionMessages::propertyWrite(
                "transmit", {MirrorUpdate{0, "paSettingsBypass", MirrorWireKind::Bool, bypass}},
                static_cast<quint32>(id))));
            if (!QTest::qWaitFor([peer, id] { return !propertyResult(peer, id).isEmpty(); }, 5000)) {
                return QJsonObject{};
            }
            const QJsonArray results = propertyResult(peer, id).value("results").toArray();
            return results.isEmpty() ? QJsonObject{} : results.first().toObject();
        };
        const QJsonObject accepted = write(app, true);
        QVERIFY2(accepted.value("accepted").toBool(),
                 qPrintable(accepted.value("reason").toString()));
        QCOMPARE(core.model->transmitModel().paSettingsBypass(), true);
        // The writer gets settled readback in property.result; its own
        // requested delta is intentionally withheld by the Core. The other
        // paired session receives the changed mirror value.
        QTRY_COMPARE(latest(denied->received(), QStringLiteral("transmit"),
                            QStringLiteral("paSettingsBypass")), QJsonValue(true));
        core.model->transmitModel().setPaSettingsBypass(false);
        QTRY_COMPARE(latest(denied->received(), QStringLiteral("transmit"),
                            QStringLiteral("paSettingsBypass")), QJsonValue(false));
        QVERIFY(write(app, true).value("accepted").toBool());
        QTRY_COMPARE(latest(denied->received(), QStringLiteral("transmit"),
                            QStringLiteral("paSettingsBypass")), QJsonValue(true));

        // A client asking for remote transmit remains subject to the Core's
        // ordinary transmit decision when the receive-only settings exception
        // is absent. The description changes no permission policy.
        core.model->setReceiveOnlyStationPolicy(false);
        const QJsonObject refused = write(denied, false);
        QVERIFY(!refused.value("accepted").toBool(true));
        QVERIFY(!refused.value("reason").toString().isEmpty());
        QCOMPARE(core.model->transmitModel().paSettingsBypass(), true);

        allowTransmit(core);
        // allowTransmit configures a logical no-socket key but also turns
        // off receive-only station policy. Restore the daemon-style settings
        // exception before checking its on-air refusal.
        core.model->setReceiveOnlyStationPolicy(true);
        MoxController* mox = core.model->moxController();
        mox->setMoxCheck({});
        mox->setMox(true); // logical test state, no radio transport
        QTRY_COMPARE(mox->state(), MoxState::Tx);
        const QJsonObject onAir = write(app, false);
        QVERIFY(!onAir.value("accepted").toBool(true));
        QCOMPARE(onAir.value("reason"), QStringLiteral("The radio is on the air. Try again when it stops."));
        QCOMPARE(core.model->transmitModel().paSettingsBypass(), true);
        mox->setMox(false);
        QTRY_COMPARE(mox->state(), MoxState::Rx);
        QVERIFY(!core.model->tune());
        QVERIFY(!core.model->transmitModel().isTwoToneActive());
    }

    void pairedPaReadoutsMirrorMetersAndRawCountsWithoutWriteAuthority()
    {
        Core core;
        core.model->setBoardForTest(HPSDRHW::Saturn);
        RadioInfo info = core.model->currentRadioInfo();
        info.boardType = HPSDRHW::Saturn;
        core.model->setLastRadioInfoForTest(info);
        core.server->setupDescription()->setRadioContext(core.model->boardCapabilities(),
                                                         core.model->hardwareProfile().model);
        Device phone(QStringLiteral("PA iPhone"), QStringLiteral("phone"));
        core.pair(phone);
        QHash<QByteArray, int> features = kTransmitter;
        features.insert("setupDescription", 1);
        LoopbackTransport* app = core.signIn(phone, features);
        QVERIFY(admitted(app));
        QCOMPARE(capability(app->received(), QStringLiteral("txReadingsVersion")),
                 std::optional<qint64>(1));
        const QJsonObject pa = QJsonDocument::fromJson(latest(app->received(),
            QStringLiteral("setup"), QStringLiteral("pa")).toString().toUtf8()).object();
        QVERIFY(!pa.isEmpty());
        QCOMPARE(pa.value("pages").toArray().first().toObject().value("sections").toArray().size(), 2);

        core.model->handlePaTelemetryForTest(2400, 320, 0, 0, 0, 0);
        QVERIFY(core.model->radioStatus().forwardPowerWatts() > 0.0);
        QVERIFY(core.model->radioStatus().reflectedPowerWatts() > 0.0);
        QTRY_COMPARE(latest(app->received(), QStringLiteral("txState"),
                            QStringLiteral("forwardAdcRaw")).toInteger(), qint64(2400));
        QTRY_COMPARE(latest(app->received(), QStringLiteral("txState"),
                            QStringLiteral("reflectedAdcRaw")).toInteger(), qint64(320));
        QTRY_COMPARE(latest(app->received(), QStringLiteral("txState"),
                            QStringLiteral("forwardPowerWatts")).toDouble(),
                     core.model->radioStatus().forwardPowerWatts());
        QTRY_COMPARE(latest(app->received(), QStringLiteral("txState"),
                            QStringLiteral("reflectedPowerWatts")).toDouble(),
                     core.model->radioStatus().reflectedPowerWatts());
        QTRY_COMPARE(latest(app->received(), QStringLiteral("txState"),
                            QStringLiteral("swr")).toDouble(),
                     core.model->radioStatus().swrRatio());

        const qint64 writeId = 876;
        app->sendText(SessionMessages::encode(SessionMessages::propertyWrite(
            "txState", {MirrorUpdate{0, "forwardAdcRaw", MirrorWireKind::Int64, qint64(1)}},
            static_cast<quint32>(writeId))));
        QTRY_VERIFY(!propertyResult(app, writeId).isEmpty());
        const QJsonArray results = propertyResult(app, writeId).value("results").toArray();
        QVERIFY(!results.isEmpty());
        QVERIFY(!results.first().toObject().value("accepted").toBool(true));
        QCOMPARE(core.server->transmitState()->forwardAdcRaw(), qint64(2400));
    }

    void pairedHardwareDescriptionWritesReachBoundAlexAndRetireOnSwap()
    {
        Core core;
        core.model->setBoardForTest(HPSDRHW::Hermes);
        RadioInfo info = core.model->currentRadioInfo();
        info.boardType = HPSDRHW::Hermes;
        core.model->setLastRadioInfoForTest(info);
        StepAttenuatorController attenuator;
        attenuator.setTickTimerEnabled(false);
        core.model->setStepAttController(&attenuator);
        core.server->setupDescription()->setRadioContext(core.model->boardCapabilities(),
                                                         core.model->hardwareProfile().model);
        Device phone(QStringLiteral("Setup iPhone"), QStringLiteral("phone"));
        core.pair(phone);
        QHash<QByteArray, int> features = kHolder;
        features.insert("setupDescription", 1);
        LoopbackTransport* app = core.signIn(phone, features);
        QVERIFY(admitted(app));
        QCOMPARE(capability(app->received(), QStringLiteral("setupDescriptionVersion")),
                 std::optional<qint64>(1));
        const QString description = latest(app->received(), QStringLiteral("setup"),
                                           QStringLiteral("hardware")).toString();
        const QJsonObject hardware = QJsonDocument::fromJson(description.toUtf8()).object();
        QVERIFY(!hardware.isEmpty());
        const QJsonArray controls = hardware.value("pages").toArray().first().toObject()
            .value("sections").toArray().first().toObject().value("controls").toArray();
        QCOMPARE(controls.size(), 6);
        const auto find = [&controls](const QString& id) {
            for (const QJsonValue& raw : controls) {
                const QJsonObject control = raw.toObject();
                if (control.value("id") == QJsonValue(id)) { return control; }
            }
            return QJsonObject{};
        };
        const QJsonObject rx = find(QStringLiteral("hardware.antennaAlex.useTxAntennaForRx"));
        const QJsonObject tx = find(QStringLiteral("hardware.antennaAlex.blockTxAnt2"));
        const QJsonObject relay = find(QStringLiteral("hardware.antennaAlex.ext1OutOnTx"));
        QVERIFY(SetupDescriptionService::validateHardwarePropertyBinding(rx));
        QVERIFY(SetupDescriptionService::validateHardwarePropertyBinding(tx));
        QVERIFY(SetupDescriptionService::validateHardwarePropertyBinding(
            relay, core.model->hardwareProfile().model));
        QCOMPARE(relay.value("gate").toObject().value("offAir"), QJsonValue(true));
        QVERIFY(!tx.value("gate").toObject().contains("transmit"));
        QCOMPARE(tx.value("gate").toObject().value("offAir"), QJsonValue(true));

        qint64 writeId = 700;
        const auto write = [&](const QJsonObject& control, bool value) {
            const QByteArray name = control.value("binding").toObject()
                .value("property").toObject().value("name").toString().toUtf8();
            const qint64 id = ++writeId;
            app->sendText(SessionMessages::encode(SessionMessages::propertyWrite(
                "alexAntennas", {MirrorUpdate{0, name, MirrorWireKind::Bool, value}},
                static_cast<quint32>(id))));
            const bool answered = QTest::qWaitFor(
                [app, id] { return !propertyResult(app, id).isEmpty(); }, 5000);
            if (!answered) { return QJsonObject{}; }
            const QJsonArray results = propertyResult(app, id).value("results").toArray();
            return results.isEmpty() ? QJsonObject{} : results.first().toObject();
        };
        MoxController* mox = core.model->moxController();
        QVERIFY(mox != nullptr);
        QSignalSpy keying(mox, &MoxController::stateChanged);
        const QJsonObject rxResult = write(rx, true);
        QVERIFY2(rxResult.value("accepted").toBool(),
                 qPrintable(rxResult.value("reason").toString()));
        const QJsonObject txResult = write(tx, true);
        QVERIFY2(txResult.value("accepted").toBool(),
                 qPrintable(txResult.value("reason").toString()));
        const QJsonObject relayResult = write(relay, true);
        QVERIFY2(relayResult.value("accepted").toBool(),
                 qPrintable(relayResult.value("reason").toString()));
        QVERIFY(core.model->alexController().useTxAntForRx());
        QVERIFY(core.model->alexController().blockTxAnt2());
        QVERIFY(core.model->alexController().ext1OutOnTx());
        QCOMPARE(mox->state(), MoxState::Rx);
        QCOMPARE(keying.count(), 0);

        // These property writes are supported even for a paired receive-only
        // session while off air; the Core's Alex policy has no TX permission
        // requirement. An unavailable hardware controller is still refused.
        core.model->alexAntennaFacade()->bindController(nullptr);
        const QJsonObject unavailable = write(tx, false);
        QVERIFY(!unavailable.value("accepted").toBool(true));
        QVERIFY(!unavailable.value("reason").toString().isEmpty());
        QVERIFY(core.model->alexController().blockTxAnt2());
        core.model->alexAntennaFacade()->bindController(&core.model->alexControllerMutable());

        allowTransmit(core);
        mox->setMox(true); // station's own holder; no radio connection exists
        QTRY_COMPARE(mox->state(), MoxState::Tx);
        const QJsonObject onAir = write(tx, false);
        QVERIFY(!onAir.value("accepted").toBool(true));
        QCOMPARE(onAir.value("reason").toString(),
                 QStringLiteral("The radio is on the air. Try again when it stops."));
        QVERIFY(core.model->alexController().blockTxAnt2());
        const QJsonObject relayOnAir = write(relay, false);
        QVERIFY(!relayOnAir.value("accepted").toBool(true));
        QCOMPARE(relayOnAir.value("reason").toString(),
                 QStringLiteral("The radio is on the air. Try again when it stops."));
        QVERIFY(core.model->alexController().ext1OutOnTx());
        mox->setMox(false);
        QTRY_COMPARE(mox->state(), MoxState::Rx);

        // A connected-board replacement removes the whole partial category.
        // Its new revision invalidates a renderer's old gesture; even a
        // forced stale wire write is refused after the controller retires.
        const quint32 capturedRevision = core.server->setupDescription()->revision();
        core.model->setBoardForTest(HPSDRHW::HermesLite);
        info.boardType = HPSDRHW::HermesLite;
        core.model->setLastRadioInfoForTest(info);
        core.model->alexAntennaFacade()->bindController(nullptr);
        core.server->setupDescription()->setRadioContext(core.model->boardCapabilities(),
                                                         core.model->hardwareProfile().model);
        QVERIFY(core.server->setupDescription()->revision() > capturedRevision);
        QTRY_VERIFY(latest(app->received(), QStringLiteral("setup"),
                           QStringLiteral("hardware")).toString().isEmpty());
        QVERIFY(core.server->setupDescription()->category(QStringLiteral("hardware")).isEmpty());
        const QJsonObject stale = write(rx, false);
        QVERIFY(!stale.value("accepted").toBool(true));
        QVERIFY(!stale.value("reason").toString().isEmpty());
        QVERIFY(core.model->alexController().useTxAntForRx());
    }

    void twoTonePresetHonoursHolderAndOnAirGuards()
    {
        RadioModel model;
        model.setBoardForTest(HPSDRHW::HermesLiteRxOnly);
        SessionCommandDispatcher dispatcher(&model);
        QList<SessionMessage> results;
        connect(&dispatcher, &SessionCommandDispatcher::commandResultReady, this,
                [&results](const SessionMessage& result) { results.append(result); });
        bool anotherHolds = false;
        SessionCommandDispatcher::TransmitAccess access;
        access.transmitter = [&anotherHolds](const QByteArray&) -> TxRefusal {
            return anotherHolds ? TxRefusals::otherDeviceHolds(QStringLiteral("Another phone"))
                                : TxRefusal{};
        };
        dispatcher.setTransmitAccess(access);
        dispatcher.setRequester("phone");
        auto invoke = [&dispatcher, &results](const QString& name) {
            results.clear();
            dispatcher.dispatch(SessionMessages::commandInvoke("tx.twoTonePreset", 1,
                {MirrorUpdate{0, "name", MirrorWireKind::Utf8, name}}));
            return results.isEmpty() ? SessionMessage{} : results.last();
        };
        QVERIFY(invoke(QStringLiteral("stealth")).accepted);
        QCOMPARE(model.transmitModel().twoToneFreq1(), 70);
        QCOMPARE(model.transmitModel().twoToneFreq2(), 190);
        anotherHolds = true;
        QVERIFY(!invoke(QStringLiteral("defaults")).accepted);
        QCOMPARE(model.transmitModel().twoToneFreq1(), 70);
        anotherHolds = false;
        model.transmitModel().setMox(true);
        QVERIFY(!invoke(QStringLiteral("defaults")).accepted);
        model.transmitModel().setMox(false);
        model.transmitModel().setTune(true);
        QVERIFY(!invoke(QStringLiteral("defaults")).accepted);
        model.transmitModel().setTune(false);
        QVERIFY(!invoke(QStringLiteral("unknown")).accepted);
        QCOMPARE(model.transmitModel().twoToneFreq1(), 70);
        QVERIFY(invoke(QStringLiteral("defaults")).accepted);
        QCOMPARE(model.transmitModel().twoToneFreq1(), 700);
        QCOMPARE(model.transmitModel().twoToneFreq2(), 1900);
    }

    void twoTonePresetPublishesACompletePairBeforePropertySignals()
    {
        TransmitModel model;
        int otherFrequencyAtFirstSignal = 0;
        int combinedSignals = 0;
        connect(&model, &TransmitModel::twoToneFrequenciesChanged, this,
                [&combinedSignals](int, int) { ++combinedSignals; });
        connect(&model, &TransmitModel::twoToneFreq1Changed, this,
                [&model, &otherFrequencyAtFirstSignal](int) {
                    otherFrequencyAtFirstSignal = model.twoToneFreq2();
                });
        model.setTwoToneFrequencies(70, 190);
        QCOMPARE(model.twoToneFreq1(), 70);
        QCOMPARE(model.twoToneFreq2(), 190);
        QCOMPARE(otherFrequencyAtFirstSignal, 190);
        QCOMPARE(combinedSignals, 1);
    }

    void remoteIdentityWritesReachRunningReporter()
    {
        AppSettings& settings = AppSettings::instance();
        settings.clear();
        RadioModel model;
        SettingsProxyServer proxy(settings);
        auto* reporter = model.freeDvReporter();
        QVERIFY(reporter != nullptr);

        const auto call = proxy.applyInboundWrite(
            QStringLiteral("User/Callsign"), QStringLiteral("KG4VCF"), QStringLiteral("phone"));
        QVERIFY2(call.accepted, qPrintable(call.reason));
        model.applyRemoteFreedvSetting(QStringLiteral("User/Callsign"), settings);
        QCOMPARE(reporter->callsignForTest(), QStringLiteral("KG4VCF"));
        QCOMPARE(settings.value(QStringLiteral("PskReporter/Callsign")).toString(),
                 QStringLiteral("KG4VCF"));

        const auto grid = proxy.applyInboundWrite(
            QStringLiteral("User/GridSquare"), QStringLiteral("EM73"), QStringLiteral("phone"));
        QVERIFY2(grid.accepted, qPrintable(grid.reason));
        model.applyRemoteFreedvSetting(QStringLiteral("User/GridSquare"), settings);
        QCOMPARE(reporter->gridSquareForTest(), QStringLiteral("EM73"));
        QCOMPARE(settings.value(QStringLiteral("PskReporter/GridSquare")).toString(),
                 QStringLiteral("EM73"));
        settings.clear();
    }
};

QTEST_MAIN(SetupDescriptionLiveTest)
#include "tst_setup_description_live.moc"
