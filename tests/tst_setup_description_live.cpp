// no-port-check: NereusSDR-original live-apply checks for described settings.
#include <QtTest>

#include "core/AppSettings.h"
#include "core/PaTelemetryScaling.h"
#include "core/FreeDVReporterClient.h"
#include "core/settings/SettingsProxyServer.h"
#include "core/session/SessionCommandDispatcher.h"
#include "models/RadioModel.h"
#include "models/TransmitModel.h"
#include "core/setup/SetupDescriptionService.h"
#include "core/accessories/AlexAntennaFacade.h"
#include "core/accessories/AlexController.h"
#include "core/StepAttenuatorController.h"
#include "core/TxAnalyzer.h"
#include "core/meters/SliceMeterPump.h"
#include "core/session/TransmitStateFacade.h"
#include "MultiDeviceHarness.h"

using namespace NereusSDR;

class SetupDescriptionLiveTest : public QObject {
    Q_OBJECT
private slots:
    void pairedV11PublishesSpectrumPeaksAndKeepsV10Projection()
    {
        Core core;
        Device current(QStringLiteral("Spectrum peaks V11 iPhone"), QStringLiteral("phone"));
        Device older(QStringLiteral("Spectrum peaks V10 iPhone"), QStringLiteral("phone"));
        core.pair(current);
        core.pair(older);
        QHash<QByteArray, int> v11Features = kHolder;
        v11Features.insert("setupDescription", 11);
        auto* v11 = core.signIn(current, v11Features);
        QVERIFY(admitted(v11));
        QCOMPARE(capability(v11->received(), QStringLiteral("setupDescriptionVersion")),
                 std::optional<qint64>(11));
        const QJsonObject display = QJsonDocument::fromJson(latest(v11->received(),
            QStringLiteral("setup"), QStringLiteral("display")).toString().toUtf8()).object();
        QCOMPARE(display.value("version"), QJsonValue(11));
        const QJsonObject peaks = display.value("pages").toArray().at(1).toObject();
        QCOMPARE(peaks.value("id"), QJsonValue("display.spectrumPeaks"));
        int described = 0;
        for (const QJsonValue& section : peaks.value("sections").toArray()) {
            for (const QJsonValue& raw : section.toObject().value("controls").toArray()) {
                QVERIFY(SetupDescriptionService::validateDisplayPhoneBinding(raw.toObject()));
                ++described;
            }
        }
        QCOMPARE(described, 15);
        QHash<QByteArray, int> v10Features = kHolder;
        v10Features.insert("setupDescription", 10);
        auto* v10 = core.signIn(older, v10Features);
        QVERIFY(admitted(v10));
        QCOMPARE(capability(v10->received(), QStringLiteral("setupDescriptionVersion")),
                 std::optional<qint64>(10));
        const QJsonObject old = QJsonDocument::fromJson(latest(v10->received(),
            QStringLiteral("setup"), QStringLiteral("display")).toString().toUtf8()).object();
        QCOMPARE(old.value("version"), QJsonValue(10));
        QCOMPARE(old.value("pages").toArray().size(), 4);
        QCOMPARE(old.value("pages").toArray().at(1).toObject().value("id"),
                 QJsonValue("display.waterfallDefaults"));
    }

    void pairedV10PublishesWaterfallOverlaysAndKeepsV9Projection()
    {
        Core core;
        Device current(QStringLiteral("Waterfall overlays V10 iPhone"), QStringLiteral("phone"));
        Device older(QStringLiteral("Waterfall overlays V9 iPhone"), QStringLiteral("phone"));
        core.pair(current);
        core.pair(older);
        QHash<QByteArray, int> v10Features = kHolder;
        v10Features.insert("setupDescription", 10);
        auto* v10 = core.signIn(current, v10Features);
        QVERIFY(admitted(v10));
        QCOMPARE(capability(v10->received(), QStringLiteral("setupDescriptionVersion")),
                 std::optional<qint64>(10));
        const QJsonObject display = QJsonDocument::fromJson(latest(v10->received(),
            QStringLiteral("setup"), QStringLiteral("display")).toString().toUtf8()).object();
        QCOMPARE(display.value("version"), QJsonValue(10));
        const QJsonArray overlays = display.value("pages").toArray().at(1).toObject()
            .value("sections").toArray().at(1).toObject().value("controls").toArray();
        QCOMPARE(overlays.size(), 4);
        for (const QJsonValue& raw : overlays) {
            QVERIFY(SetupDescriptionService::validateDisplayPhoneBinding(raw.toObject()));
        }
        QHash<QByteArray, int> v9Features = kHolder;
        v9Features.insert("setupDescription", 9);
        auto* v9 = core.signIn(older, v9Features);
        QVERIFY(admitted(v9));
        QCOMPARE(capability(v9->received(), QStringLiteral("setupDescriptionVersion")),
                 std::optional<qint64>(9));
        const QJsonObject old = QJsonDocument::fromJson(latest(v9->received(),
            QStringLiteral("setup"), QStringLiteral("display")).toString().toUtf8()).object();
        QCOMPARE(old.value("version"), QJsonValue(9));
        QCOMPARE(old.value("pages").toArray().at(1).toObject()
                     .value("sections").toArray().size(), 1);
    }

    void pairedV9PublishesLocalRendererDispatchWithoutChangingV8()
    {
        Core core;
        Device current(QStringLiteral("Renderer V9 iPhone"), QStringLiteral("phone"));
        Device older(QStringLiteral("Renderer V8 iPhone"), QStringLiteral("phone"));
        core.pair(current);
        core.pair(older);
        QHash<QByteArray, int> v9Features = kHolder;
        v9Features.insert("setupDescription", 9);
        auto* v9 = core.signIn(current, v9Features);
        QVERIFY(admitted(v9));
        QCOMPARE(capability(v9->received(), QStringLiteral("setupDescriptionVersion")),
                 std::optional<qint64>(9));
        const QJsonObject display = QJsonDocument::fromJson(latest(v9->received(),
            QStringLiteral("setup"), QStringLiteral("display")).toString().toUtf8()).object();
        QCOMPARE(display.value("version"), QJsonValue(9));
        const QJsonArray pages = display.value("pages").toArray();
        const QJsonArray rendering = pages.at(0).toObject().value("sections").toArray()
            .at(1).toObject().value("controls").toArray();
        const QJsonArray waterfall = pages.at(1).toObject().value("sections").toArray()
            .at(0).toObject().value("controls").toArray();
        QCOMPARE(rendering.size(), 10);
        QCOMPARE(waterfall.size(), 6);
        for (int i = 5; i < rendering.size(); ++i) {
            QVERIFY(SetupDescriptionService::validateDisplayPhoneBinding(rendering.at(i).toObject()));
        }
        for (int i = 3; i < waterfall.size(); ++i) {
            QVERIFY(SetupDescriptionService::validateDisplayPhoneBinding(waterfall.at(i).toObject()));
        }
        QHash<QByteArray, int> v8Features = kHolder;
        v8Features.insert("setupDescription", 8);
        auto* v8 = core.signIn(older, v8Features);
        QVERIFY(admitted(v8));
        QCOMPARE(capability(v8->received(), QStringLiteral("setupDescriptionVersion")),
                 std::optional<qint64>(8));
        const QJsonObject old = QJsonDocument::fromJson(latest(v8->received(),
            QStringLiteral("setup"), QStringLiteral("display")).toString().toUtf8()).object();
        QCOMPARE(old.value("version"), QJsonValue(8));
        QCOMPARE(old.value("pages").toArray().at(0).toObject().value("sections").toArray()
                     .at(1).toObject().value("controls").toArray().size(), 5);
        QCOMPARE(old.value("pages").toArray().at(1).toObject().value("sections").toArray()
                     .at(0).toObject().value("controls").toArray().size(), 3);
    }

    void pairedV8PublishesOnlyClosedPhoneRxDispatch()
    {
        Core core;
        Device current(QStringLiteral("RX display V8 iPhone"), QStringLiteral("phone"));
        Device older(QStringLiteral("RX display V7 iPhone"), QStringLiteral("phone"));
        core.pair(current);
        core.pair(older);
        QHash<QByteArray, int> v8Features = kHolder;
        v8Features.insert("setupDescription", 8);
        auto* v8 = core.signIn(current, v8Features);
        QVERIFY(admitted(v8));
        QCOMPARE(capability(v8->received(), QStringLiteral("setupDescriptionVersion")),
                 std::optional<qint64>(8));
        const QJsonObject display = QJsonDocument::fromJson(latest(v8->received(),
            QStringLiteral("setup"), QStringLiteral("display")).toString().toUtf8()).object();
        QCOMPARE(display.value("version"), QJsonValue(8));
        const QJsonArray pages = display.value("pages").toArray();
        QCOMPARE(pages.size(), 4);
        const QJsonArray rendering = pages.at(0).toObject().value("sections").toArray()
            .at(1).toObject().value("controls").toArray();
        QCOMPARE(rendering.size(), 5);
        const QJsonArray waterfall = pages.at(1).toObject().value("sections").toArray()
            .at(0).toObject().value("controls").toArray();
        QCOMPARE(waterfall.size(), 3);
        for (int i = 1; i < rendering.size(); ++i) {
            QVERIFY(SetupDescriptionService::validateDisplayPhoneBinding(rendering.at(i).toObject()));
        }
        for (const QJsonValue& raw : waterfall) {
            QVERIFY(SetupDescriptionService::validateDisplayPhoneBinding(raw.toObject()));
        }
        QHash<QByteArray, int> v7Features = kHolder;
        v7Features.insert("setupDescription", 7);
        auto* v7 = core.signIn(older, v7Features);
        QVERIFY(admitted(v7));
        QCOMPARE(capability(v7->received(), QStringLiteral("setupDescriptionVersion")),
                 std::optional<qint64>(7));
        const QJsonObject oldDisplay = QJsonDocument::fromJson(latest(v7->received(),
            QStringLiteral("setup"), QStringLiteral("display")).toString().toUtf8()).object();
        QCOMPARE(oldDisplay.value("version"), QJsonValue(4));
        QCOMPARE(oldDisplay.value("pages").toArray().size(), 3);
        QCOMPARE(oldDisplay.value("pages").toArray().first().toObject()
                     .value("sections").toArray().at(1).toObject()
                     .value("controls").toArray().size(), 1);
    }

    void pairedV7PublishesOnlyPhoneOwnedMeterStyles()
    {
        Core core;
        Device current(QStringLiteral("Meter styles iPhone"), QStringLiteral("phone"));
        Device older(QStringLiteral("Older meter styles iPhone"), QStringLiteral("phone"));
        core.pair(current);
        core.pair(older);
        QHash<QByteArray, int> v7Features = kHolder;
        v7Features.insert("setupDescription", 7);
        auto* v7 = core.signIn(current, v7Features);
        QVERIFY(admitted(v7));
        QCOMPARE(capability(v7->received(), QStringLiteral("setupDescriptionVersion")),
                 std::optional<qint64>(7));
        const QJsonObject appearance = QJsonDocument::fromJson(latest(v7->received(),
            QStringLiteral("setup"), QStringLiteral("appearance")).toString().toUtf8()).object();
        QCOMPARE(appearance.value("version"), QJsonValue(7));
        const QJsonArray pages = appearance.value("pages").toArray();
        QCOMPARE(pages.size(), 2);
        const QJsonArray styles = pages.at(1).toObject().value("sections").toArray()
            .first().toObject().value("controls").toArray();
        QCOMPARE(styles.size(), 3);
        for (const QJsonValue& raw : styles) {
            const QJsonObject control = raw.toObject();
            QVERIFY(SetupDescriptionService::validateAppearanceMeterStyleBinding(control));
            QCOMPARE(control.value("binding").toObject().size(), 1);
            QVERIFY(control.value("binding").toObject().contains("phone"));
            QVERIFY(!control.contains("gate"));
            QVERIFY(!control.contains("valueEncoding"));
        }
        QHash<QByteArray, int> v6Features = kHolder;
        v6Features.insert("setupDescription", 6);
        auto* v6 = core.signIn(older, v6Features);
        QVERIFY(admitted(v6));
        QCOMPARE(capability(v6->received(), QStringLiteral("setupDescriptionVersion")),
                 std::optional<qint64>(6));
        const QJsonObject olderAppearance = QJsonDocument::fromJson(latest(v6->received(),
            QStringLiteral("setup"), QStringLiteral("appearance")).toString().toUtf8()).object();
        QCOMPARE(olderAppearance.value("version"), QJsonValue(4));
        QCOMPARE(olderAppearance.value("pages").toArray().size(), 1);
        QCOMPARE(olderAppearance.value("pages").toArray().first().toObject()
                     .value("sections").toArray().first().toObject()
                     .value("controls").toArray().size(), 10);
    }

    void pairedV6RowsStayDescribedWhileRadioConnectsWithoutReconnect()
    {
        Core core;
        StepAttenuatorController step;
        step.setTickTimerEnabled(false);
        core.model->setStepAttController(&step);
        core.model->setBoardForTest(HPSDRHW::Hermes);
        RadioInfo info = core.model->currentRadioInfo();
        info.boardType = HPSDRHW::Hermes;
        core.model->setLastRadioInfoForTest(info);
        core.model->setConnectionStateForTest(ConnectionState::Disconnected);
        core.server->setupDescription()->setRadioContext(core.model->boardCapabilities(),
                                                         core.model->hardwareProfile().model);
        const quint32 unchangedDescriptionRevision = core.server->setupDescription()->revision();
        const QString unchangedDescription = core.server->setupDescription()->hardware();
        QVERIFY(!unchangedDescription.isEmpty());
        QVERIFY(core.model->currentRadioMac().isEmpty());

        Device phone(QStringLiteral("Late-radio antenna iPhone"), QStringLiteral("phone"));
        core.pair(phone);
        QHash<QByteArray, int> features = kHolder;
        features.insert("setupDescription", 6);
        features.insert("radioAntennaRows", 1);
        auto* app = core.signIn(phone, features);
        QVERIFY(admitted(app));
        const auto hasRows = [app] {
            const QString hardware = latest(app->received(), QStringLiteral("setup"),
                                            QStringLiteral("hardware")).toString();
            return hardware.contains(QStringLiteral("hardware.antenna.txRows"))
                && hardware.contains(QStringLiteral("hardware.antenna.rxRows"));
        };
        const auto latestRowsCapability = [app] {
            const QList<QJsonObject> messages = ofType(app->received(),
                                                        QStringLiteral("capabilities"));
            for (auto it = messages.crbegin(); it != messages.crend(); ++it) {
                for (const QJsonValue& raw : it->value(QStringLiteral("properties")).toArray()) {
                    const QJsonObject property = raw.toObject();
                    if (property.value(QStringLiteral("name"))
                        == QJsonValue(QStringLiteral("radioAntennaRowsVersion"))) {
                        return property.value(QStringLiteral("value")).toInteger();
                    }
                }
                return qint64(0);
            }
            return qint64(0);
        };
        QCOMPARE(latestRowsCapability(), qint64(0));
        QVERIFY(hasRows());

        const QJsonObject hardware = QJsonDocument::fromJson(latest(app->received(),
            QStringLiteral("setup"), QStringLiteral("hardware")).toString().toUtf8()).object();
        const QJsonArray controls = hardware.value("pages").toArray().first().toObject()
            .value("sections").toArray().first().toObject().value("controls").toArray();
        const QJsonObject rx = controls.last().toObject();
        QVERIFY(SetupDescriptionService::validateAntennaRowsTable(rx,
            core.model->hardwareProfile().model));
        const QJsonObject row = rx.value("rows").toArray().at(3).toObject();
        const QJsonObject column = rx.value("columns").toArray().at(1).toObject();
        QCOMPARE(row.value("label"), QJsonValue("40m"));
        QCOMPARE(column.value("antenna"), QJsonValue(2));

        core.model->setConnectionStateForTest(ConnectionState::Connected);
        core.model->currentRadioChanged(core.model->currentRadioInfo());
        QTRY_COMPARE(latestRowsCapability(), qint64(1));
        QCOMPARE(core.server->setupDescription()->revision(), unchangedDescriptionRevision);
        QCOMPARE(core.server->setupDescription()->hardware(), unchangedDescription);
        QVERIFY(hasRows());
        const QString mac = core.model->currentRadioMac();
        QVERIFY(!mac.isEmpty());
        const QJsonObject result = core.invoke(app, "setAlexRxAntennaForRadio",
            {MirrorUpdate{0, "mac", MirrorWireKind::Utf8, mac},
             MirrorUpdate{0, "band", MirrorWireKind::Int64,
                          qlonglong(row.value("band").toInt())},
             MirrorUpdate{0, "antenna", MirrorWireKind::Int64,
                          qlonglong(column.value("antenna").toInt())},
             MirrorUpdate{0, "rxOnly", MirrorWireKind::Bool, false}});
        QVERIFY2(result.value("accepted").toBool(),
                 qPrintable(result.value("reason").toString()));
        QCOMPARE(core.model->alexController().rxAnt(Band::Band40m), 2);
        QCOMPARE(core.model->alexController().rxAnt(Band::Band20m), 1);
    }

    void pairedV6DisconnectRetainsInertTableUntilSameRadioReturns()
    {
        Core core;
        StepAttenuatorController step;
        step.setTickTimerEnabled(false);
        core.model->setStepAttController(&step);
        core.model->setBoardForTest(HPSDRHW::Hermes);
        core.server->setupDescription()->setRadioContext(core.model->boardCapabilities(),
                                                         core.model->hardwareProfile().model);
        Device phone(QStringLiteral("Reconnect antenna iPhone"), QStringLiteral("phone"));
        core.pair(phone);
        QHash<QByteArray, int> features = kHolder;
        features.insert("setupDescription", 6);
        features.insert("radioAntennaRows", 1);
        auto* app = core.signIn(phone, features);
        QVERIFY(admitted(app));
        QCOMPARE(capability(app->received(), QStringLiteral("radioAntennaRowsVersion")),
                 std::optional<qint64>(1));
        const QString tableDescription = latest(app->received(), QStringLiteral("setup"),
                                                 QStringLiteral("hardware")).toString();
        QVERIFY(tableDescription.contains(QStringLiteral("hardware.antenna.txRows")));
        const int capabilityCount = ofType(app->received(), QStringLiteral("capabilities")).size();
        core.model->setConnectionStateForTest(ConnectionState::Disconnected);
        core.model->currentRadioChanged(core.model->currentRadioInfo());
        QTRY_VERIFY(ofType(app->received(), QStringLiteral("capabilities")).size()
                    > capabilityCount);
        const QJsonObject withdrawn = ofType(app->received(), QStringLiteral("capabilities")).last();
        for (const QJsonValue& raw : withdrawn.value(QStringLiteral("properties")).toArray()) {
            QVERIFY(raw.toObject().value(QStringLiteral("name"))
                    != QJsonValue(QStringLiteral("radioAntennaRowsVersion")));
        }
        QCOMPARE(latest(app->received(), QStringLiteral("setup"),
                        QStringLiteral("hardware")).toString(), tableDescription);
        core.model->setConnectionStateForTest(ConnectionState::Connected);
        core.model->currentRadioChanged(core.model->currentRadioInfo());
        const auto rowCapRestored = [app] {
            const QList<QJsonObject> messages = ofType(app->received(),
                                                        QStringLiteral("capabilities"));
            if (messages.isEmpty()) { return false; }
            for (const QJsonValue& raw : messages.last().value(QStringLiteral("properties")).toArray()) {
                if (raw.toObject().value(QStringLiteral("name"))
                        == QJsonValue(QStringLiteral("radioAntennaRowsVersion"))
                    && raw.toObject().value(QStringLiteral("value")) == QJsonValue(1)) {
                    return true;
                }
            }
            return false;
        };
        QTRY_VERIFY(rowCapRestored());
        QCOMPARE(latest(app->received(), QStringLiteral("setup"),
                        QStringLiteral("hardware")).toString(), tableDescription);
    }

    void pairedV6AntennaTableRowsUseCurrentRadioCommands()
    {
        Core core;
        StepAttenuatorController step;
        step.setTickTimerEnabled(false);
        core.model->setStepAttController(&step);
        core.model->setBoardForTest(HPSDRHW::Hermes);
        core.server->setupDescription()->setRadioContext(core.model->boardCapabilities(),
                                                         core.model->hardwareProfile().model);
        Device a(QStringLiteral("Antenna table iPhone A"), QStringLiteral("phone"));
        Device b(QStringLiteral("Antenna table iPhone B"), QStringLiteral("phone"));
        core.pair(a);
        core.pair(b);
        QHash<QByteArray, int> features = kHolder;
        features.insert("setupDescription", 6);
        features.insert("radioAntennaRows", 1);
        auto* first = core.signIn(a, features);
        QVERIFY(admitted(first));
        QCOMPARE(capability(first->received(), QStringLiteral("setupDescriptionVersion")),
                 std::optional<qint64>(6));
        QCOMPARE(capability(first->received(), QStringLiteral("radioAntennaRowsVersion")),
                 std::optional<qint64>(1));
        const QJsonObject hardware = QJsonDocument::fromJson(latest(first->received(),
            QStringLiteral("setup"), QStringLiteral("hardware")).toString().toUtf8()).object();
        QCOMPARE(hardware.value("version"), QJsonValue(6));
        const QJsonArray controls = hardware.value("pages").toArray().first().toObject()
            .value("sections").toArray().first().toObject().value("controls").toArray();
        const QJsonObject tx = controls.at(controls.size() - 2).toObject();
        const QJsonObject rx = controls.last().toObject();
        QVERIFY(SetupDescriptionService::validateAntennaRowsTable(tx,
            core.model->hardwareProfile().model));
        QVERIFY(SetupDescriptionService::validateAntennaRowsTable(rx,
            core.model->hardwareProfile().model));
        const QString mac = core.model->currentRadioMac();
        QVERIFY(!mac.isEmpty());
        const QJsonObject row = rx.value("rows").toArray().at(3).toObject();
        const int band = row.value("band").toInt();
        const QJsonObject rxColumn = rx.value("columns").toArray().at(1).toObject();
        const QJsonObject onlyColumn = rx.value("columns").toArray().at(4).toObject();
        QCOMPARE(rxColumn.value("field"), QJsonValue("rx"));
        QCOMPARE(onlyColumn.value("field"), QJsonValue("rxOnly"));
        const auto rxArgs = [&mac, band](const QJsonObject& column) {
            return QList<MirrorUpdate>{
                MirrorUpdate{0, "mac", MirrorWireKind::Utf8, mac},
                MirrorUpdate{0, "band", MirrorWireKind::Int64, qlonglong(band)},
                MirrorUpdate{0, "antenna", MirrorWireKind::Int64,
                             qlonglong(column.value("antenna").toInt())},
                MirrorUpdate{0, "rxOnly", MirrorWireKind::Bool,
                             column.value("field") == QJsonValue("rxOnly")}};
        };
        const QJsonObject rxResult = core.invoke(first, "setAlexRxAntennaForRadio",
                                                 rxArgs(rxColumn));
        QVERIFY2(rxResult.value("accepted").toBool(),
                 qPrintable(rxResult.value("reason").toString()));
        QCOMPARE(core.model->alexController().rxAnt(Band::Band40m), 2);
        QCOMPARE(core.model->alexController().rxAnt(Band::Band20m), 1);
        QVERIFY(core.invoke(first, "setAlexRxAntennaForRadio", rxArgs(onlyColumn))
                    .value("accepted").toBool());
        QCOMPARE(core.model->alexController().rxOnlyAnt(Band::Band40m), 2);
        const QJsonObject txColumn = tx.value("columns").toArray().at(2).toObject();
        const auto txArgs = [&mac, band](const QJsonObject& column) {
            return QList<MirrorUpdate>{
                MirrorUpdate{0, "mac", MirrorWireKind::Utf8, mac},
                MirrorUpdate{0, "band", MirrorWireKind::Int64, qlonglong(band)},
                MirrorUpdate{0, "antenna", MirrorWireKind::Int64,
                             qlonglong(column.value("antenna").toInt())}};
        };
        QVERIFY(core.invoke(first, "setAlexTxAntennaForRadio", txArgs(txColumn))
                    .value("accepted").toBool());
        QCOMPARE(core.model->alexController().txAnt(Band::Band40m), 3);
        QCOMPARE(core.model->alexController().txAnt(Band::Band20m), 1);
        auto* second = core.signIn(b, features);
        QVERIFY(admitted(second));
        QCOMPARE(capability(second->received(), QStringLiteral("radioAntennaRowsVersion")),
                 std::optional<qint64>(1));
        QTRY_COMPARE(latest(second->received(), QStringLiteral("alexAntennas"),
                            QStringLiteral("rxAntennas")).toString(),
                     core.model->alexAntennaFacade()->rxAntennas());
        QTRY_COMPARE(latest(second->received(), QStringLiteral("alexAntennas"),
                            QStringLiteral("rxOnlyAntennas")).toString(),
                     core.model->alexAntennaFacade()->rxOnlyAntennas());
        QTRY_COMPARE(latest(second->received(), QStringLiteral("alexAntennas"),
                            QStringLiteral("txAntennas")).toString(),
                     core.model->alexAntennaFacade()->txAntennas());
        // A gesture derived from the published row and column while both
        // paired devices are present follows the real shared-confirmation
        // route, then reaches the already-connected second device's mirror.
        const QJsonObject liveRow = rx.value("rows").toArray().at(5).toObject();
        const QJsonObject liveColumn = rx.value("columns").toArray().at(2).toObject();
        QCOMPARE(liveRow.value("label"), QJsonValue("20m"));
        QCOMPARE(liveColumn.value("field"), QJsonValue("rx"));
        const int asksBefore = ofType(first->received(), QStringLiteral("confirm.request")).size();
        const QJsonObject pending = core.invoke(first, "setAlexRxAntennaForRadio",
            {MirrorUpdate{0, "mac", MirrorWireKind::Utf8, mac},
             MirrorUpdate{0, "band", MirrorWireKind::Int64,
                          qlonglong(liveRow.value("band").toInt())},
             MirrorUpdate{0, "antenna", MirrorWireKind::Int64,
                          qlonglong(liveColumn.value("antenna").toInt())},
             MirrorUpdate{0, "rxOnly", MirrorWireKind::Bool, false}});
        QVERIFY(!pending.value("accepted").toBool(true));
        QTRY_VERIFY(ofType(first->received(), QStringLiteral("confirm.request")).size()
                    > asksBefore);
        const QJsonObject question =
            ofType(first->received(), QStringLiteral("confirm.request")).last();
        const QJsonObject confirmed = core.invoke(first, "confirm.proceed",
            {MirrorUpdate{0, "id", MirrorWireKind::Int64,
                          qlonglong(question.value("id").toInteger())},
             MirrorUpdate{0, "choice", MirrorWireKind::Int64, qlonglong(-1)}});
        QVERIFY2(confirmed.value("accepted").toBool(),
                 qPrintable(confirmed.value("reason").toString()));
        QCOMPARE(core.model->alexController().rxAnt(Band::Band20m), 3);
        QCOMPARE(core.model->alexController().rxAnt(Band::Band40m), 2);
        QTRY_COMPARE(latest(second->received(), QStringLiteral("alexAntennas"),
                            QStringLiteral("rxAntennas")).toString(),
                     core.model->alexAntennaFacade()->rxAntennas());
        core.model->alexControllerMutable().setBlockTxAnt3(true);
        QVERIFY(!core.invoke(first, "setAlexTxAntennaForRadio", txArgs(txColumn))
                     .value("accepted").toBool(true));
        QCOMPARE(core.model->alexController().txAnt(Band::Band40m), 1);
        QList<MirrorUpdate> wrong = rxArgs(rxColumn);
        wrong[0].value = QStringLiteral("AA:BB:CC:DD:EE:99");
        QVERIFY(!core.invoke(first, "setAlexRxAntennaForRadio", wrong)
                     .value("accepted").toBool(true));
        Device withoutRows(QStringLiteral("No row feature"), QStringLiteral("phone"));
        core.pair(withoutRows);
        QHash<QByteArray, int> noRows = kHolder;
        noRows.insert("setupDescription", 6);
        auto* older = core.signIn(withoutRows, noRows);
        QVERIFY(admitted(older));
        const QString noRowsHardware = latest(older->received(), QStringLiteral("setup"),
                                               QStringLiteral("hardware")).toString();
        QVERIFY(!noRowsHardware.isEmpty());
        QVERIFY(!noRowsHardware.contains(QStringLiteral("hardware.antenna.txRows")));
        QVERIFY(!noRowsHardware.contains(QStringLiteral("hardware.antenna.rxRows")));

        const int capabilitiesBefore = ofType(first->received(), QStringLiteral("capabilities")).size();
        core.model->setConnectionStateForTest(ConnectionState::Disconnected);
        QTRY_VERIFY(ofType(first->received(), QStringLiteral("capabilities")).size()
                    > capabilitiesBefore);
        const QJsonObject withdrawn = ofType(first->received(), QStringLiteral("capabilities")).last();
        for (const QJsonValue& value : withdrawn.value(QStringLiteral("properties")).toArray()) {
            QVERIFY(value.toObject().value(QStringLiteral("name"))
                    != QJsonValue(QStringLiteral("radioAntennaRowsVersion")));
        }
        QVERIFY(!core.invoke(first, "setAlexRxAntennaForRadio", rxArgs(rxColumn))
                     .value("accepted").toBool(true));
        QCOMPARE(core.model->alexController().rxAnt(Band::Band40m), 2);
    }
    void pairedV5PaTelemetryDescriptionUsesExistingOptionalWire()
    {
        Core core;
        core.model->setBoardForTest(HPSDRHW::Saturn);
        RadioInfo info = core.model->currentRadioInfo();
        info.boardType = HPSDRHW::Saturn;
        core.model->setLastRadioInfoForTest(info);
        core.server->setupDescription()->setRadioContext(core.model->boardCapabilities(),
                                                         core.model->hardwareProfile().model);
        core.server->setTelemetryEnabled(true);
        Device phone(QStringLiteral("PA telemetry iPhone"), QStringLiteral("phone"));
        core.pair(phone);
        QHash<QByteArray, int> features = kHolder;
        features.insert("setupDescription", 5);
        LoopbackTransport* app = core.signIn(phone, features);
        QVERIFY(admitted(app));
        QCOMPARE(capability(app->received(), QStringLiteral("setupDescriptionVersion")),
                 std::optional<qint64>(5));
        QCOMPARE(capability(app->received(), QStringLiteral("stationTelemetryVersion")),
                 std::optional<qint64>(6));
        const QJsonObject pa = QJsonDocument::fromJson(latest(app->received(),
            QStringLiteral("setup"), QStringLiteral("pa")).toString().toUtf8()).object();
        QCOMPARE(pa.value("version"), QJsonValue(5));
        const QJsonArray telemetry = pa.value("pages").toArray().last().toObject()
            .value("sections").toArray().at(1).toObject().value("controls").toArray();
        QCOMPARE(telemetry.size(), 4);
        QVERIFY(SetupDescriptionService::validatePaTelemetryReadoutBinding(telemetry.at(0).toObject()));
        QVERIFY(SetupDescriptionService::validatePaTelemetryReadoutBinding(telemetry.at(1).toObject()));

        StationTelemetrySnapshot sample;
        sample.sequence = 1;
        sample.radio.connected = true;
        sample.radio.paCurrentAmps = 0.0;
        sample.radio.supplyVolts = 13.8;
        QVERIFY(core.server->sendTelemetry(sample, core.server->sessionEpoch()));
        QJsonObject radio;
        QTRY_VERIFY([&] {
            for (const QJsonObject& message : ofType(app->received(), QStringLiteral("station.metrics.v1"))) {
                if (message.value("payload").toObject().value("sequence") == QJsonValue(1)) {
                    radio = message.value("payload").toObject().value("radio").toObject();
                    return true;
                }
            }
            return false;
        }());
        QCOMPARE(radio.value("paCurrentAmps"), QJsonValue(0.0));
        QCOMPARE(radio.value("supplyVolts"), QJsonValue(13.8));
        QVERIFY(!radio.contains("paVolts"));
        sample.sequence = 2;
        sample.radio.paCurrentAmps.reset();
        sample.radio.supplyVolts.reset();
        QVERIFY(core.server->sendTelemetry(sample, core.server->sessionEpoch()));
        QTRY_VERIFY([&] {
            for (const QJsonObject& message : ofType(app->received(), QStringLiteral("station.metrics.v1"))) {
                if (message.value("payload").toObject().value("sequence") != QJsonValue(2)) {
                    continue;
                }
                const QJsonObject next = message.value("payload").toObject().value("radio").toObject();
                return !next.contains("paCurrentAmps") && !next.contains("supplyVolts");
            }
            return false;
        }());
    }

    void pairedV4DisplaySettingsReachCoreAndNormalizeTracksCurrentDetector()
    {
        TxAnalyzer analyzer(TxAnalyzer::kTxDispId);
        Core core;
        core.model->setTxAnalyzer(&analyzer);
        core.server->setMediaEnabled(true);
        Device phone(QStringLiteral("Display settings iPhone"), QStringLiteral("phone"));
        core.pair(phone);
        QHash<QByteArray, int> features = kHolder;
        features.insert("setupDescription", 4);
        LoopbackTransport* app = core.signIn(phone, features);
        QVERIFY(admitted(app));
        QCOMPARE(capability(app->received(), QStringLiteral("setupDescriptionVersion")),
                 std::optional<qint64>(4));
        QCOMPARE(capability(app->received(), QStringLiteral("txDisplayVersion")),
                 std::optional<qint64>(3));
        const QJsonObject display = QJsonDocument::fromJson(latest(app->received(),
            QStringLiteral("setup"), QStringLiteral("display")).toString().toUtf8()).object();
        QCOMPARE(display.value("version"), QJsonValue(4));
        const QJsonObject normalize = display.value("pages").toArray().at(2).toObject()
            .value("sections").toArray().at(1).toObject().value("controls").toArray().at(3).toObject();
        QCOMPARE(normalize.value("enabledWhen").toObject().value("setting"),
                 QJsonValue("DisplayTxPanDetector"));
        QVERIFY(!normalize.value("gate").toObject().contains("offAir"));
        QVERIFY(!normalize.value("gate").toObject().contains("transmit"));
        const QJsonArray permitted = normalize.value("enabledWhen").toObject().value("oneOf").toArray();
        const auto enabled = [&permitted](const QString& current) {
            return !current.isEmpty() && permitted.contains(QJsonValue(current));
        };
        QVERIFY(!enabled({}));
        QVERIFY(!enabled(QStringLiteral("invalid")));
        QVERIFY(!enabled(QStringLiteral("1")));
        QVERIFY(enabled(QStringLiteral("2")));
        QVERIFY(enabled(QStringLiteral("3")));
        QVERIFY(enabled(QStringLiteral("4")));

        const auto write = [&core, app](const QString& key, const QString& value,
                                        const QString& origin) {
            app->sendText(SessionMessages::encode(SessionMessages::settingsWrite(key, value, origin)));
            const bool observed = QTest::qWaitFor([&core, app, &key, &value, &origin] {
                if (core.settings->value(key).toString() != value) { return false; }
                for (const QJsonObject& item : ofType(app->received(), QStringLiteral("settings.value"))) {
                    const QJsonArray properties = item.value("properties").toArray();
                    if (item.value("key") == QJsonValue(key)
                        && !properties.isEmpty()
                        && properties.first().toObject().value("value") == QJsonValue(value)
                        && item.value("origin") == QJsonValue(origin)) { return true; }
                }
                return false;
            }, 5000);
            if (!observed) {
                qWarning() << "Display write not observed" << key << value
                           << "stored" << core.settings->value(key)
                           << "wire" << app->received().mid(qMax(0, app->received().size() - 5));
            }
            return observed;
        };
        QVERIFY(write(QStringLiteral("DisplayFftSize"), QStringLiteral("8192"), QStringLiteral("rx")));
        QVERIFY(write(QStringLiteral("DisplayTxWindowType"), QStringLiteral("6"), QStringLiteral("tx")));
        SliceMeterPump* pump = core.model->sliceMeterPump();
        QVERIFY(pump != nullptr);
        QVERIFY(write(QStringLiteral("MultimeterDelayMs"), QStringLiteral("180"), QStringLiteral("meter")));
        QTRY_COMPARE(pump->intervalMs(), 180);
        for (const QString& value : {QStringLiteral("0"), QStringLiteral("2"),
                                     QStringLiteral("1")}) {
            QVERIFY(write(QStringLiteral("DisplayTxPanDetector"), value,
                          QStringLiteral("detector-") + value));
            QCOMPARE(enabled(core.settings->value(QStringLiteral("DisplayTxPanDetector")).toString()),
                     value == QLatin1String("2"));
        }
    }
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
        // The Core's own advertised version, not a literal: it rises with
        // each transmit setting the Core learns to take.
        QCOMPARE(capability(app->received(), QStringLiteral("transmitSettingsVersion")),
                 std::optional<qint64>(
                     core.server->buildCapabilities().transmitSettingsVersion));
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
        allowTransmit(core);
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
        // A9 (iPhone app plan Task 39): 3 adds the stage readings.
        QCOMPARE(capability(app->received(), QStringLiteral("txReadingsVersion")),
                 std::optional<qint64>(3));
        const QJsonObject pa = QJsonDocument::fromJson(latest(app->received(),
            QStringLiteral("setup"), QStringLiteral("pa")).toString().toUtf8()).object();
        QVERIFY(!pa.isEmpty());
        QCOMPARE(pa.value("pages").toArray().first().toObject().value("sections").toArray().size(), 3);

        core.model->handlePaTelemetryForTest(2400, 320, 0, 0, 0, 0);
        QVERIFY(core.model->radioStatus().forwardPowerWatts() > 0.0);
        QVERIFY(core.model->radioStatus().reflectedPowerWatts() > 0.0);
        QTRY_COMPARE(latest(app->received(), QStringLiteral("txState"),
                            QStringLiteral("forwardAdcRaw")).toInteger(), qint64(2400));
        QTRY_COMPARE(latest(app->received(), QStringLiteral("txState"),
                            QStringLiteral("reflectedAdcRaw")).toInteger(), qint64(320));
        QTRY_COMPARE(latest(app->received(), QStringLiteral("txState"),
                            QStringLiteral("forwardRawPowerWatts")).toDouble(),
                     scaleFwdPowerWatts(HPSDRModel::ANAN_G2, 2400));
        QTRY_COMPARE(latest(app->received(), QStringLiteral("txState"),
                            QStringLiteral("forwardAdcVolts")).toDouble(),
                     scaleFwdRevVoltage(HPSDRModel::ANAN_G2, 2400));
        QTRY_COMPARE(latest(app->received(), QStringLiteral("txState"),
                            QStringLiteral("reflectedAdcVolts")).toDouble(),
                     scaleFwdRevVoltage(HPSDRModel::ANAN_G2, 320));
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
        QVERIFY(core.invoke(app, "tx.take", {}).value(QStringLiteral("accepted")).toBool());
        QVERIFY(txPermitted(app));
        const qint64 ownerWriteId = 877;
        app->sendText(SessionMessages::encode(SessionMessages::propertyWrite(
            "txState", {MirrorUpdate{34, "forwardRawPowerWatts", MirrorWireKind::Float64, 1.0}},
            static_cast<quint32>(ownerWriteId))));
        QTRY_VERIFY(!propertyResult(app, ownerWriteId).isEmpty());
        QVERIFY(!propertyResult(app, ownerWriteId).value("results").toArray()
                     .first().toObject().value("accepted").toBool(true));
        QCOMPARE(core.server->transmitState()->forwardRawPowerWatts(),
                 scaleFwdPowerWatts(HPSDRModel::ANAN_G2, 2400));
        Device observer(QStringLiteral("PA observer"), QStringLiteral("phone"));
        core.pair(observer);
        LoopbackTransport* other = core.signIn(observer, kHolder);
        QVERIFY(admitted(other));
        QVERIFY(!txPermitted(other));
        const qint64 observerWriteId = 878;
        other->sendText(SessionMessages::encode(SessionMessages::propertyWrite(
            "txState", {MirrorUpdate{35, "forwardAdcVolts", MirrorWireKind::Float64, 1.0}},
            static_cast<quint32>(observerWriteId))));
        QTRY_VERIFY(!propertyResult(other, observerWriteId).isEmpty());
        QVERIFY(!propertyResult(other, observerWriteId).value("results").toArray()
                     .first().toObject().value("accepted").toBool(true));
        QCOMPARE(core.server->transmitState()->forwardAdcVolts(),
                 scaleFwdRevVoltage(HPSDRModel::ANAN_G2, 2400));
    }

    void pairedPaDriveReadoutUsesSelectedPowerMirror()
    {
        Core core;
        core.model->setBoardForTest(HPSDRHW::Saturn);
        RadioInfo info = core.model->currentRadioInfo();
        info.boardType = HPSDRHW::Saturn;
        core.model->setLastRadioInfoForTest(info);
        core.server->setupDescription()->setRadioContext(core.model->boardCapabilities(),
                                                         core.model->hardwareProfile().model);
        Device phone(QStringLiteral("PA Drive phone"), QStringLiteral("phone"));
        core.pair(phone);
        QHash<QByteArray, int> features = kHolder;
        features.insert("setupDescription", 1);
        LoopbackTransport* app = core.signIn(phone, features);
        QVERIFY(admitted(app));
        // The Core's own advertised version, not a literal: it rises with
        // each transmit setting the Core learns to take.
        QCOMPARE(capability(app->received(), QStringLiteral("transmitSettingsVersion")),
                 std::optional<qint64>(
                     core.server->buildCapabilities().transmitSettingsVersion));
        const QJsonObject pa = QJsonDocument::fromJson(latest(app->received(),
            QStringLiteral("setup"), QStringLiteral("pa")).toString().toUtf8()).object();
        const QJsonArray power = pa.value("pages").toArray().last().toObject()
            .value("sections").toArray().first().toObject().value("controls").toArray();
        QCOMPARE(power.size(), 5);
        const QJsonObject drive = power.last().toObject();
        QVERIFY(SetupDescriptionService::validatePaDriveReadoutBinding(drive));
        QCOMPARE(drive.value("kind"), QJsonValue("readout"));
        core.model->transmitModel().setPower(37);
        QTRY_COMPARE(latest(app->received(), QStringLiteral("transmit"),
                            QStringLiteral("power")).toInteger(), qint64(37));
        QCOMPARE(core.model->transmitModel().power(), 37);
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
