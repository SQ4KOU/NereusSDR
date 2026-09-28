// no-port-check: NereusSDR-original test of the Setup description wire surface.
#include <QtTest>

#include "core/setup/SetupDescriptionService.h"
#include "core/BoardCapabilities.h"
#include "core/session/MirrorSchema.h"
#include "core/session/MirrorPolicy.h"
#include "core/session/StationCapabilities.h"
#include "core/session/SessionMessages.h"
#include "core/session/StationServer.h"
#include "core/AppSettings.h"
#include "core/ConnectionState.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "fakes/LoopbackTransport.h"
#include "fakes/UpgradedCoreToken.h"

#include <QJsonDocument>
#include <QTemporaryDir>

#include <memory>

using namespace NereusSDR;

namespace {
struct WireCore {
    QTemporaryDir settingsDir;
    QTemporaryDir securityDir;
    std::unique_ptr<AppSettings> settings;
    std::unique_ptr<RadioModel> model;
    std::unique_ptr<StationServer> server;
    std::unique_ptr<Test::LoopbackTransport> app;

    WireCore()
    {
        settings = std::make_unique<AppSettings>(
            settingsDir.filePath(QStringLiteral("NereusSDR.settings")));
        model = std::make_unique<RadioModel>();
        model->setBoardForTest(HPSDRHW::HermesLite);
        RadioInfo info;
        info.macAddress = QStringLiteral("AA:BB:CC:DD:EE:43");
        info.name = QStringLiteral("Setup test HL2");
        info.boardType = HPSDRHW::HermesLite;
        model->setLastRadioInfoForTest(info);
        model->setConnectionStateForTest(ConnectionState::Connected);
        model->addSlice(QStringLiteral("pan-0"));
        server = std::make_unique<StationServer>(
            model.get(), *settings, Test::seedUpgradedCoreToken(securityDir.path()));
    }

    bool connect(const QHash<QByteArray, int>& features = {})
    {
        app = std::make_unique<Test::LoopbackTransport>(QStringLiteral("app"));
        auto* station = new Test::LoopbackTransport(QStringLiteral("station"), server.get());
        station->linkTo(app.get());
        server->acceptTransport(station);
        if (!QTest::qWaitFor([this] { return !app->received().isEmpty(); }, 5000)) {
            return false;
        }
        app->sendText(SessionMessages::encode(SessionMessages::hello(
            kSessionProtocolMajor, kSessionProtocolMinor, 0, QStringLiteral("Setup test"),
            {kSessionProtocolMajor}, features)));
        app->sendText(SessionMessages::encode(SessionMessages::authRequest(server->token())));
        return QTest::qWaitFor([this] {
            return app->receivedKinds().contains(QByteArrayLiteral("snapshot.complete"));
        }, 5000);
    }
};

bool hasSetupTraffic(const Test::LoopbackTransport& app)
{
    for (const QByteArray& wire : app.received()) {
        const QJsonObject message = QJsonDocument::fromJson(wire).object();
        const QString type = message.value(QStringLiteral("type")).toString();
        if (type != QLatin1String("schema") && type != QLatin1String("object.create")
            && type != QLatin1String("delta")) {
            continue;
        }
        if (message.value(QStringLiteral("key")).toString() == QStringLiteral("setup")
            || wire.contains("SetupDescription")) {
            return true;
        }
    }
    return false;
}

// A small stand-in for Task 58's renderer. The Core validates static source
// names and types; the client must materialize them from one live epoch.
QJsonObject resolveTciArguments(const QJsonObject& control, bool changed,
                                const QJsonObject& properties,
                                const QString& sourceSession,
                                const QString& liveSession,
                                quint64 sourceEpoch, quint64 liveEpoch)
{
    if (sourceSession != liveSession || sourceEpoch != liveEpoch) { return {}; }
    QJsonObject result;
    const QJsonObject arguments = control.value("binding").toObject()
        .value("command").toObject().value("arguments").toObject();
    for (auto it = arguments.constBegin(); it != arguments.constEnd(); ++it) {
        const QJsonObject source = it.value().toObject();
        if (source.contains("$controlValue")) {
            result.insert(it.key(), changed);
        } else {
            const QJsonObject ref = source.value("$property").toObject();
            if (ref.value("object") != QJsonValue("stationTci")) { return {}; }
            const QJsonValue value = properties.value(ref.value("name").toString());
            if (!value.isBool()) { return {}; }
            result.insert(it.key(), value);
        }
    }
    return result;
}
} // namespace

class SetupDescriptionServiceTest : public QObject {
    Q_OBJECT
private slots:
    void categoriesLoadAndMirrorAsStrings()
    {
        SetupDescriptionService service;
        for (const QString& id : {QStringLiteral("general"), QStringLiteral("test"),
                                  QStringLiteral("catNetwork"), QStringLiteral("dsp")}) {
            const QJsonObject category = service.category(id);
            QCOMPARE(category.value(QStringLiteral("version")).toInt(), 1);
            QCOMPARE(category.value(QStringLiteral("category")).toObject()
                         .value(QStringLiteral("id")).toString(), id);
            QVERIFY(!category.value(QStringLiteral("pages")).toArray().isEmpty());
            QVERIFY(!service.property(id.toLatin1().constData()).toString().isEmpty());
        }
        // An unfinished category does not claim to be a rendered page.
        QVERIFY(service.category(QStringLiteral("diagnostics")).isEmpty());
        QVERIFY(service.diagnostics().isEmpty());
        QCOMPARE(service.revision(), quint32(1));
        const MirrorSchema& schema = MirrorSchema::forObject(&service);
        QVERIFY(schema.byName("general"));
        QVERIFY(schema.byName("catNetwork"));
        QVERIFY(schema.byName("revision"));
    }

    void boardRefreshOnlyMovesRevisionOnChange()
    {
        SetupDescriptionService service;
        const auto hasStepAtt = [&service]() {
            const QJsonArray pages = service.category(QStringLiteral("general"))
                                         .value(QStringLiteral("pages")).toArray();
            for (const QJsonValue& page : pages) {
                for (const QJsonValue& section : page.toObject()
                         .value(QStringLiteral("sections")).toArray()) {
                    for (const QJsonValue& control : section.toObject()
                             .value(QStringLiteral("controls")).toArray()) {
                        if (control.toObject().value(QStringLiteral("id")).toString()
                            == QStringLiteral("general.options.rx1StepAtt")) {
                            return true;
                        }
                    }
                }
            }
            return false;
        };
        QVERIFY(!hasStepAtt());
        const quint32 first = service.revision();
        service.setBoardCapabilities(BoardCapsTable::forBoard(HPSDRHW::HermesLite));
        QVERIFY(service.revision() > first);
        QVERIFY(hasStepAtt());
        const quint32 second = service.revision();
        service.setBoardCapabilities(BoardCapsTable::forBoard(HPSDRHW::HermesLite));
        QCOMPARE(service.revision(), second);
    }

    void dspPropertiesAreWritableOnTheSelectedSliceAlias()
    {
        SetupDescriptionService service;
        const QJsonArray pages = service.category(QStringLiteral("dsp"))
                                     .value(QStringLiteral("pages")).toArray();
        QCOMPARE(pages.size(), 2);
        const MirrorSchema& sliceSchema = MirrorSchema::forMetaObject(
            &SliceModel::staticMetaObject);
        int count = 0;
        for (const QJsonValue& rawPage : pages) {
            for (const QJsonValue& rawSection : rawPage.toObject()
                     .value(QStringLiteral("sections")).toArray()) {
                for (const QJsonValue& rawControl : rawSection.toObject()
                         .value(QStringLiteral("controls")).toArray()) {
                    if (rawControl.toObject().value("binding").toObject()
                            .contains("command")) {
                        QString error;
                        QVERIFY2(SetupDescriptionService::validateCommandBinding(
                            rawControl.toObject(), &error), qPrintable(error));
                        continue;
                    }
                    const QJsonObject ref = rawControl.toObject().value("binding")
                        .toObject().value("property").toObject();
                    QVERIFY(SetupDescriptionService::validateActiveSlicePropertyBinding(
                        rawControl.toObject()));
                    QCOMPARE(ref.value("object").toString(), QStringLiteral("slice:active"));
                    const QByteArray name = ref.value("name").toString().toUtf8();
                    const MirrorProperty* property = sliceSchema.byName(name);
                    QVERIFY2(property != nullptr, name.constData());
                    QVERIFY(property->isWritable);
                    QVERIFY(MirrorPolicy::inboundAllowed("SliceModel", name));
                    ++count;
                }
            }
        }
        QCOMPARE(count, 50);
        QJsonObject valid = pages.first().toObject().value("sections").toArray().first()
            .toObject().value("controls").toArray().first().toObject();
        QJsonObject bad = valid;
        bad.insert("kind", "toggle");
        QVERIFY(!SetupDescriptionService::validateActiveSlicePropertyBinding(bad));
        QJsonObject binding = valid.value("binding").toObject();
        QJsonObject ref = binding.value("property").toObject();
        ref.insert("name", "nnrStatus"); // outbound-only diagnostic
        binding.insert("property", ref);
        bad = valid;
        bad.insert("binding", binding);
        QVERIFY(!SetupDescriptionService::validateActiveSlicePropertyBinding(bad));
        ref.insert("name", "nr1Taps");
        ref.insert("object", "slice:0"); // must be late-bound to this device's selection
        binding.insert("property", ref);
        bad.insert("binding", binding);
        QVERIFY(!SetupDescriptionService::validateActiveSlicePropertyBinding(bad));
    }

    void dspWritesMutateOnlyTheNamedLiveSlice()
    {
        WireCore core;
        QCOMPARE(core.model->addSlice(QStringLiteral("pan-0")), 1);
        QVERIFY(core.connect({{QByteArrayLiteral("setupDescription"), 1}}));
        SliceModel* first = core.model->sliceById(0);
        SliceModel* second = core.model->sliceById(1);
        QVERIFY(first != nullptr);
        QVERIFY(second != nullptr);
        const int firstTaps = first->nr1Taps();
        const double secondK1 = second->snbK1();
        core.app->sendText(SessionMessages::encode(SessionMessages::propertyWrite(
            "slice:1", {MirrorUpdate{0, "nr1Taps", MirrorWireKind::Int64, qint64(88)}}, 201)));
        core.app->sendText(SessionMessages::encode(SessionMessages::propertyWrite(
            "slice:0", {MirrorUpdate{0, "snbK1", MirrorWireKind::Float64, 7.5}}, 202)));
        QTRY_COMPARE(second->nr1Taps(), 88);
        QTRY_COMPARE(first->snbK1(), 7.5);
        QCOMPARE(first->nr1Taps(), firstTaps);
        QCOMPARE(second->snbK1(), secondK1);
        core.model->removeSlice(1);
        QVERIFY(core.model->sliceById(1) == nullptr);
        core.app->sendText(SessionMessages::encode(SessionMessages::propertyWrite(
            "slice:1", {MirrorUpdate{0, "nr1Taps", MirrorWireKind::Int64, qint64(99)}}, 203)));
        auto retiredResult = [&core]() {
            for (const QByteArray& wire : core.app->received()) {
                const QJsonObject message = QJsonDocument::fromJson(wire).object();
                if (message.value("type") == QJsonValue("property.result")
                    && message.value("writeId").toInt() == 203) {
                    return message;
                }
            }
            return QJsonObject{};
        };
        QTRY_VERIFY(!retiredResult().isEmpty());
        const QJsonArray results = retiredResult().value("results").toArray();
        QCOMPARE(results.size(), 1);
        QVERIFY(!results.first().toObject().value("accepted").toBool());
    }

    void legacyCapabilitiesWireIsUnchanged()
    {
        StationCapabilities older;
        const QList<MirrorUpdate> baseline = older.toUpdates();
        for (const MirrorUpdate& update : baseline) {
            QVERIFY(update.name != "setupDescriptionVersion");
        }
        older.radioIdentityEntries = true;
        const QList<MirrorUpdate> legacyMinor11 = older.toUpdates();
        for (const MirrorUpdate& update : legacyMinor11) {
            QVERIFY(update.name != "setupDescriptionVersion");
        }
        older.setupDescriptionVersion = 1;
        const QList<MirrorUpdate> described = older.toUpdates();
        QCOMPARE(described.size(), legacyMinor11.size() + 1);
        QCOMPARE(described.last().name, QByteArray("setupDescriptionVersion"));
        QCOMPARE(StationCapabilities::fromUpdates(described).setupDescriptionVersion, 1);
    }

    void onlyDeclaringPeerReceivesSetup()
    {
        WireCore legacy;
        QVERIFY(legacy.connect());
        QJsonObject stableHello = QJsonDocument::fromJson(legacy.app->received().first()).object();
        QVERIFY(stableHello.value("challenge").isString());
        QVERIFY(stableHello.value("identity").isObject());
        stableHello.remove("challenge");
        stableHello.remove("identity");
        QCOMPARE(QJsonDocument(stableHello).toJson(QJsonDocument::Compact), QByteArrayLiteral(
            "{\"features\":{\"deviceAuth\":1,\"pairing\":1,\"sessionHolder\":1},"
            "\"major\":1,\"majors\":[1],\"minor\":11,\"peer\":\"nereusd\","
            "\"settingsSchema\":0,\"type\":\"hello\"}"));
        QVERIFY(!hasSetupTraffic(*legacy.app));
        for (const QByteArray& wire : legacy.app->received()) {
            QVERIFY(!wire.contains("setupDescriptionVersion"));
        }
        legacy.app->sendText(SessionMessages::encode(SessionMessages::propertyWrite(
            "setup", {MirrorUpdate{0, "general", MirrorWireKind::Utf8,
                                   QStringLiteral("{}")}}, 43)));
        QTRY_VERIFY(legacy.app->receivedKinds().contains(QByteArrayLiteral("property.result")));
        QVERIFY(!hasSetupTraffic(*legacy.app));
        legacy.app->sendText(SessionMessages::encode(SessionMessages::commandInvoke(
            "tx.twoTonePreset", 44,
            {MirrorUpdate{0, "name", MirrorWireKind::Utf8, QStringLiteral("stealth")}})));
        QTRY_VERIFY(legacy.app->receivedKinds().contains(QByteArrayLiteral("command.result")));
        bool refusedPreset = false;
        for (const QByteArray& wire : legacy.app->received()) {
            const QJsonObject result = QJsonDocument::fromJson(wire).object();
            refusedPreset |= result.value("type") == QJsonValue("command.result")
                && result.value("id").toInt() == 44 && !result.value("accepted").toBool();
        }
        QVERIFY(refusedPreset);

        WireCore current;
        QVERIFY(current.connect({{QByteArrayLiteral("setupDescription"), 1}}));
        QVERIFY(hasSetupTraffic(*current.app));
        bool capability = false;
        for (const QByteArray& wire : current.app->received()) {
            capability |= wire.contains("setupDescriptionVersion");
        }
        QVERIFY(capability);
    }

    void commandSourcesHaveOneTypedSource()
    {
        const QByteArray fixture = R"({
          "kind":"toggle", "gate":{"capability":"stationTciVersion","min":2},
          "binding":{"command":{"verb":"setStationTciOptions",
            "valueProperty":{"object":"stationTci","name":"emulateExpertSdr3"},
            "arguments":{
              "emulateExpertSdr3":{"$controlValue":true},
              "emulateSunSdr2Pro":{"$property":{"object":"stationTci","name":"emulateSunSdr2Pro"}},
              "cwluBecomesCw":{"$property":{"object":"stationTci","name":"cwluBecomesCw"}},
              "sendInitialState":{"$property":{"object":"stationTci","name":"sendInitialState"}}
            }}}})";
        QJsonObject control = QJsonDocument::fromJson(fixture).object();
        QString error;
        QVERIFY2(SetupDescriptionService::validateCommandBinding(control, &error),
                 qPrintable(error));
        const QJsonObject original = control;
        auto mutateArg = [&control, &original](const QJsonValue& replacement) {
            control = original;
            QJsonObject binding = control.value(QStringLiteral("binding")).toObject();
            QJsonObject command = binding.value(QStringLiteral("command")).toObject();
            QJsonObject args = command.value(QStringLiteral("arguments")).toObject();
            args.insert(QStringLiteral("emulateSunSdr2Pro"), replacement);
            command.insert(QStringLiteral("arguments"), args);
            binding.insert(QStringLiteral("command"), command);
            control.insert(QStringLiteral("binding"), binding);
        };
        mutateArg(QJsonObject{{QStringLiteral("$controlValue"), true},
                              {QStringLiteral("$property"), QJsonObject{{"object", "stationTci"},
                                                                        {"name", "emulateSunSdr2Pro"}}}});
        QVERIFY(!SetupDescriptionService::validateCommandBinding(control, &error));
        mutateArg(QJsonObject{{QStringLiteral("$property"), QStringLiteral("stationTci")}});
        QVERIFY(!SetupDescriptionService::validateCommandBinding(control, &error));
        mutateArg(QJsonObject{{QStringLiteral("$property"), QJsonObject{{"object", "stationTci"},
                                                                        {"name", "port"}}}});
        QVERIFY(!SetupDescriptionService::validateCommandBinding(control, &error));
        control = original;
        QJsonObject gate = control.value(QStringLiteral("gate")).toObject();
        gate.insert(QStringLiteral("min"), 1);
        control.insert(QStringLiteral("gate"), gate);
        QVERIFY(!SetupDescriptionService::validateCommandBinding(control, &error));
    }

    void shippedCommandBindingsPassStaticValidation()
    {
        SetupDescriptionService service;
        for (const QString& categoryId : {QStringLiteral("general"), QStringLiteral("test"),
                                          QStringLiteral("diagnostics"), QStringLiteral("catNetwork")}) {
            for (const QJsonValue& page : service.category(categoryId).value("pages").toArray()) {
                for (const QJsonValue& section : page.toObject().value("sections").toArray()) {
                    for (const QJsonValue& raw : section.toObject().value("controls").toArray()) {
                        const QJsonObject control = raw.toObject();
                        if (!control.value("binding").toObject().contains("command")) { continue; }
                        QString error;
                        QVERIFY2(SetupDescriptionService::validateCommandBinding(control, &error),
                                 qPrintable(control.value("id").toString() + ": " + error));
                    }
                }
            }
        }
    }

    void tciEditUsesOneLiveSessionEpochAndPreservesOtherOptions()
    {
        SetupDescriptionService service;
        const QJsonObject page = service.category(QStringLiteral("catNetwork"))
            .value("pages").toArray().first().toObject();
        const QJsonArray controls = page.value("sections").toArray().first()
            .toObject().value("controls").toArray();
        const QJsonObject current{{"emulateExpertSdr3", true}, {"emulateSunSdr2Pro", true},
                                  {"cwluBecomesCw", false}, {"sendInitialState", true}};
        for (const QJsonValue& raw : controls) {
            const QJsonObject control = raw.toObject();
            const QString changedName = control.value("binding").toObject()
                .value("command").toObject().value("valueProperty").toObject()
                .value("name").toString();
            const bool edited = !current.value(changedName).toBool();
            const QJsonObject args = resolveTciArguments(control, edited, current,
                QStringLiteral("phone-A"), QStringLiteral("phone-A"), 7, 7);
            QCOMPARE(args.size(), 4);
            for (auto it = current.constBegin(); it != current.constEnd(); ++it) {
                QCOMPARE(args.value(it.key()).toBool(),
                         it.key() == changedName ? edited : it.value().toBool());
            }
            QVERIFY(resolveTciArguments(control, edited, current,
                QStringLiteral("phone-A"), QStringLiteral("phone-B"), 7, 7).isEmpty());
            QVERIFY(resolveTciArguments(control, edited, current,
                QStringLiteral("phone-A"), QStringLiteral("phone-A"), 7, 8).isEmpty());
            QJsonObject missing = current;
            missing.remove(QStringLiteral("emulateSunSdr2Pro"));
            if (changedName != QLatin1String("emulateSunSdr2Pro")) {
                QVERIFY(resolveTciArguments(control, edited, missing,
                    QStringLiteral("phone-A"), QStringLiteral("phone-A"), 7, 7).isEmpty());
            }
        }
    }
};

QTEST_MAIN(SetupDescriptionServiceTest)
#include "tst_setup_description_service.moc"
