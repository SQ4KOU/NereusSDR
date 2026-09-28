// no-port-check: NereusSDR-original test of the Setup description wire surface.
#include <QtTest>
#include <QRegularExpression>

#include "core/setup/SetupDescriptionService.h"
#include "core/BoardCapabilities.h"
#include "core/session/MirrorSchema.h"
#include "core/session/MirrorPolicy.h"
#include "core/session/StationCapabilities.h"
#include "core/session/SessionMessages.h"
#include "core/session/StationServer.h"
#include "core/AppSettings.h"
#include "core/settings/SettingsProxyServer.h"
#include "core/ConnectionState.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"
#include "models/NotchModel.h"
#include "fakes/LoopbackTransport.h"
#include "fakes/UpgradedCoreToken.h"

#include <QJsonDocument>
#include <QScopeGuard>
#include <QSet>
#include <QTemporaryDir>

#include <memory>
#include <cmath>
#include <utility>

using namespace NereusSDR;

namespace {
struct WireCore {
    QTemporaryDir settingsDir;
    QTemporaryDir securityDir;
    std::unique_ptr<AppSettings> settings;
    std::unique_ptr<RadioModel> model;
    std::unique_ptr<StationServer> server;
    std::unique_ptr<Test::LoopbackTransport> app;

    explicit WireCore(HPSDRHW board = HPSDRHW::HermesLite)
    {
        settings = std::make_unique<AppSettings>(
            settingsDir.filePath(QStringLiteral("NereusSDR.settings")));
        model = std::make_unique<RadioModel>();
        model->setBoardForTest(board);
        RadioInfo info;
        info.macAddress = QStringLiteral("AA:BB:CC:DD:EE:43");
        info.name = QStringLiteral("Setup test HL2");
        info.boardType = board;
        model->setLastRadioInfoForTest(info);
        model->setConnectionStateForTest(ConnectionState::Connected);
        model->addSlice(QStringLiteral("pan-0"));
        server = std::make_unique<StationServer>(
            model.get(), *settings, Test::seedUpgradedCoreToken(securityDir.path()));
    }

    bool connect(const QHash<QByteArray, int>& features = {},
                 quint16 minor = kSessionProtocolMinor)
    {
        app = std::make_unique<Test::LoopbackTransport>(QStringLiteral("app"));
        auto* station = new Test::LoopbackTransport(QStringLiteral("station"), server.get());
        station->linkTo(app.get());
        server->acceptTransport(station);
        if (!QTest::qWaitFor([this] { return !app->received().isEmpty(); }, 5000)) {
            return false;
        }
        app->sendText(SessionMessages::encode(SessionMessages::hello(
            kSessionProtocolMajor, minor, 0, QStringLiteral("Setup test"),
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

QString setupCategoryOnWire(const Test::LoopbackTransport& app, const QByteArray& name,
                           SessionMessageKind kind)
{
    for (const QByteArray& wire : app.received()) {
        SessionMessage message;
        if (!SessionMessages::decode(wire, &message) || message.kind != kind
            || message.objectKey != "setup") { continue; }
        for (const MirrorUpdate& update : message.updates) {
            if (update.name == name) { return update.value.toString(); }
        }
    }
    return {};
}

int setupCapabilityOnWire(const Test::LoopbackTransport& app)
{
    for (const QByteArray& wire : app.received()) {
        SessionMessage message;
        if (SessionMessages::decode(wire, &message)
            && message.kind == SessionMessageKind::Capabilities) {
            return StationCapabilities::fromUpdates(message.updates).setupDescriptionVersion;
        }
    }
    return -1;
}

// A renderer stand-in: it rejects a stale gesture before resolving the
// table's typed sources. Production clients must also apply these checks.
QList<MirrorUpdate> materializeTnfRowAction(const QJsonObject& action,
                                            const QString& listJson, quint32 listRevision,
                                            quint32 gestureRevision, int rowId,
                                            const QJsonObject& edits,
                                            const QString& gestureSession,
                                            const QString& liveSession,
                                            quint64 gestureEpoch, quint64 liveEpoch)
{
    if (gestureSession != liveSession || gestureEpoch != liveEpoch
        || listRevision != gestureRevision) { return {}; }
    const QJsonDocument document = QJsonDocument::fromJson(listJson.toUtf8());
    if (!document.isArray() || document.array().size() > NotchModel::kMaxNotches) { return {}; }
    QSet<int> seen;
    bool found = false;
    for (const QJsonValue& raw : document.array()) {
        if (!raw.isObject()) { return {}; }
        const QJsonObject row = raw.toObject();
        const QJsonValue id = row.value("id");
        const QJsonValue centre = row.value("centreHz");
        const QJsonValue width = row.value("widthHz");
        if (!id.isDouble() || id.toInteger(-1) < 0
            || !centre.isDouble() || !std::isfinite(centre.toDouble())
            || centre.toDouble() < NotchModel::kMinNotchCentreHz
            || centre.toDouble() > NotchModel::kMaxNotchCentreHz
            || !width.isDouble() || !std::isfinite(width.toDouble())
            || width.toDouble() < 0 || width.toDouble() > NotchModel::kMaxNotchWidthHz
            || !row.value("active").isBool() || seen.contains(int(id.toInteger()))) {
            return {};
        }
        seen.insert(int(id.toInteger()));
        found |= id.toInteger() == rowId;
    }
    if (!found) { return {}; }
    const QJsonObject command = action.value("command").toObject();
    if (command.value("verb").toString().isEmpty()) { return {}; }
    const QJsonObject arguments = command.value("arguments").toObject();
    QList<MirrorUpdate> resolved;
    for (auto it = arguments.constBegin(); it != arguments.constEnd(); ++it) {
        const QJsonObject source = it.value().toObject();
        if (source.size() != 1) { return {}; }
        if (source.value("$row") == QJsonValue("id") && it.key() == QLatin1String("id")) {
            resolved.append({0, "id", MirrorWireKind::Int64, qlonglong(rowId)});
        } else if (source.value("$edit") == QJsonValue(it.key())
                   && (it.key() == QLatin1String("centreHz")
                       || it.key() == QLatin1String("widthHz"))) {
            const QJsonValue value = edits.value(it.key());
            const double min = it.key() == QLatin1String("centreHz")
                ? NotchModel::kMinNotchCentreHz : 0;
            const double max = it.key() == QLatin1String("centreHz")
                ? NotchModel::kMaxNotchCentreHz : NotchModel::kMaxNotchWidthHz;
            if (!value.isDouble() || !std::isfinite(value.toDouble())
                || value.toDouble() < min || value.toDouble() > max) { return {}; }
            resolved.append({0, it.key().toUtf8(), MirrorWireKind::Float64, value.toDouble()});
        } else if (source.value("$edit") == QJsonValue("active")
                   && it.key() == QLatin1String("active")
                   && edits.value("active").isBool()) {
            resolved.append({0, "active", MirrorWireKind::Bool, edits.value("active").toBool()});
        } else { return {}; }
    }
    return resolved;
}

QList<MirrorUpdate> materializeTnfAdd(const QJsonObject& control,
                                     int capturedSliceId, int selectedSliceId,
                                     bool selectedOwned, const QString& gestureSession,
                                     const QString& liveSession,
                                     quint64 gestureEpoch, quint64 liveEpoch)
{
    const QJsonObject source = control.value("binding").toObject()
        .value("command").toObject().value("arguments").toObject()
        .value("sliceId").toObject();
    if (source != QJsonObject{{"$selectedOwnedSliceId", true}}
        || capturedSliceId < 0 || capturedSliceId != selectedSliceId || !selectedOwned
        || gestureSession != liveSession || gestureEpoch != liveEpoch) { return {}; }
    return {{0, "sliceId", MirrorWireKind::Int64, qlonglong(selectedSliceId)}};
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
    void antennaRowsRequireVersionSixAndAlex()
    {
        SetupDescriptionService service;
        service.setRadioContext(BoardCapsTable::forBoard(HPSDRHW::Hermes),
                                HPSDRModel::HERMES);
        const QJsonObject hardware = service.category(QStringLiteral("hardware"));
        QCOMPARE(hardware.value("version"), QJsonValue(6));
        const QJsonArray sections = hardware.value("pages").toArray().first().toObject()
            .value("sections").toArray();
        QCOMPARE(sections.size(), 1);
        const QJsonArray controls = sections.first().toObject().value("controls").toArray();
        QCOMPARE(controls.size(), 8);
        const QJsonObject tx = controls.at(6).toObject();
        const QJsonObject rx = controls.at(7).toObject();
        QCOMPARE(tx.value("id"), QJsonValue("hardware.antenna.txRows"));
        QCOMPARE(rx.value("id"), QJsonValue("hardware.antenna.rxRows"));
        QVERIFY(SetupDescriptionService::validateAntennaRowsTable(tx, HPSDRModel::HERMES));
        QVERIFY(SetupDescriptionService::validateAntennaRowsTable(rx, HPSDRModel::HERMES));
        for (int version = 1; version <= 5; ++version) {
            const QJsonObject older = QJsonDocument::fromJson(
                SetupDescriptionService::fitCategoryForVersion(service.hardware(), version).toUtf8())
                .object();
            const QJsonArray oldControls = older.value("pages").toArray().first().toObject()
                .value("sections").toArray().first().toObject().value("controls").toArray();
            QCOMPARE(oldControls.size(), 6);
        }
        service.setRadioContext(BoardCapsTable::forBoard(HPSDRHW::HermesLite),
                                HPSDRModel::HERMESLITE);
        QVERIFY(service.hardware().isEmpty());
    }

    void antennaRowsRejectChangedEnvelopeAndProjectSkuLabels()
    {
        SetupDescriptionService service;
        service.setRadioContext(BoardCapsTable::forBoard(HPSDRHW::Hermes),
                                HPSDRModel::HERMES);
        const auto tables = [&service] {
            const QJsonArray controls = service.category(QStringLiteral("hardware"))
                .value("pages").toArray().first().toObject().value("sections").toArray()
                .first().toObject().value("controls").toArray();
            return std::pair{controls.at(controls.size() - 2).toObject(),
                             controls.last().toObject()};
        };
        const auto [tx, rx] = tables();
        QCOMPARE(tx.value("rows").toArray().size(), 14);
        QCOMPARE(rx.value("rows").toArray().size(), 14);
        QCOMPARE(tx.value("columns").toArray().size(), 3);
        QCOMPARE(rx.value("columns").toArray().size(), 6);
        QVERIFY(!tx.contains("columnGroups"));
        QCOMPARE(rx.value("columnGroups").toArray().size(), 2);
        const auto reject = [](QJsonObject changed, HPSDRModel model) {
            QVERIFY(!SetupDescriptionService::validateAntennaRowsTable(changed, model));
        };
        QJsonObject bad = tx;
        bad.insert("command", QJsonObject{{"verb", "setAlexTxAntennaForRadio"}});
        reject(bad, HPSDRModel::HERMES);
        bad = tx;
        bad.insert("columnGroups", rx.value("columnGroups"));
        reject(bad, HPSDRModel::HERMES);
        bad = rx;
        QJsonArray duplicateColumns = bad.value("columns").toArray();
        duplicateColumns.append(duplicateColumns.first());
        bad.insert("columns", duplicateColumns);
        reject(bad, HPSDRModel::HERMES);
        bad = tx;
        QJsonObject gate = bad.value("gate").toObject();
        gate.remove("offAir");
        bad.insert("gate", gate);
        reject(bad, HPSDRModel::HERMES);
        bad = rx;
        bad.remove("columnGroups");
        reject(bad, HPSDRModel::HERMES);
        bad = rx;
        QJsonArray groups = bad.value("columnGroups").toArray();
        groups[0] = QJsonObject{{"label", "RX-only"},
                                {"columns", QJsonArray{"rx1", "rx2", "rx3"}}};
        bad.insert("columnGroups", groups);
        reject(bad, HPSDRModel::HERMES);
        bad = rx;
        QJsonObject binding = bad.value("binding").toObject();
        binding.insert("antennaRows", QJsonObject{{"object", "alexAntennas"},
                                                   {"mode", "tx"}});
        bad.insert("binding", binding);
        reject(bad, HPSDRModel::HERMES);
        bad = rx;
        QJsonArray rows = bad.value("rows").toArray();
        QJsonObject row = rows.at(0).toObject();
        row.insert("band", 1);
        rows[0] = row;
        bad.insert("rows", rows);
        reject(bad, HPSDRModel::HERMES);
        bad = rx;
        rows = bad.value("rows").toArray();
        rows.append(rows.first());
        bad.insert("rows", rows);
        reject(bad, HPSDRModel::HERMES);
        bad = rx;
        rows = bad.value("rows").toArray();
        row = rows.at(0).toObject();
        QJsonArray cells = row.value("cells").toArray();
        QJsonObject cell = cells.at(0).toObject();
        cell.insert("tooltip", "Wrong port");
        cells[0] = cell;
        row.insert("cells", cells);
        rows[0] = row;
        bad.insert("rows", rows);
        reject(bad, HPSDRModel::HERMES);

        service.setRadioContext(BoardCapsTable::forBoard(HPSDRHW::Hermes),
                                HPSDRModel::ANAN100);
        const auto [classicTx, classicRx] = tables();
        QVERIFY(SetupDescriptionService::validateAntennaRowsTable(classicTx,
                                                                   HPSDRModel::ANAN100));
        QVERIFY(SetupDescriptionService::validateAntennaRowsTable(classicRx,
                                                                   HPSDRModel::ANAN100));
        QVERIFY(classicRx.value("columns") != rx.value("columns"));
        QVERIFY(!SetupDescriptionService::validateAntennaRowsTable(classicRx,
                                                                    HPSDRModel::HERMES));
        service.setRadioContext(BoardCapsTable::forBoard(HPSDRHW::Saturn),
                                HPSDRModel::ANAN_G2);
        const auto [g2Tx, g2Rx] = tables();
        QVERIFY(SetupDescriptionService::validateAntennaRowsTable(g2Tx,
                                                                   HPSDRModel::ANAN_G2));
        QVERIFY(SetupDescriptionService::validateAntennaRowsTable(g2Rx,
                                                                   HPSDRModel::ANAN_G2));
        QVERIFY(g2Rx.value("columns") != classicRx.value("columns"));
    }
    void appearancePublishesTenPhoneColoursWithAlpha()
    {
        SetupDescriptionService service;
        const QJsonObject appearance = service.category(QStringLiteral("appearance"));
        QCOMPARE(appearance.value("version"), QJsonValue(7));
        QCOMPARE(appearance.value("category").toObject().value("where"), QJsonValue("phone"));
        const QJsonArray pages = appearance.value("pages").toArray();
        QCOMPARE(pages.size(), 2);
        const QJsonArray sections = pages.first().toObject().value("sections").toArray();
        QCOMPARE(sections.size(), 1);
        const QJsonArray controls = sections.first().toObject().value("controls").toArray();
        QCOMPARE(controls.size(), 10);
        for (const QJsonValue& raw : controls) {
            const QJsonObject control = raw.toObject();
            QCOMPARE(control.value("kind"), QJsonValue("colour"));
            QCOMPARE(control.value("applies"), QJsonValue("live"));
            const QString colour = control.value("default").toString();
            QVERIFY(QRegularExpression(QStringLiteral("^#[0-9A-F]{8}$")).match(colour).hasMatch());
        }
    }

    void meterStylesPublishedInVersionSeven()
    {
        SetupDescriptionService service;
        const QJsonObject appearance = service.category(QStringLiteral("appearance"));
        QCOMPARE(appearance.value("version"), QJsonValue(7));
        const QJsonArray pages = appearance.value("pages").toArray();
        QCOMPARE(pages.size(), 2);
        const QJsonArray controls = pages.at(1).toObject().value("sections").toArray()
            .first().toObject().value("controls").toArray();
        QCOMPARE(controls.size(), 3);
        QCOMPARE(controls.at(0).toObject().value("id"), QJsonValue("appearance.meterStyles.face"));
        QCOMPARE(controls.at(1).toObject().value("id"), QJsonValue("appearance.meterStyles.peakHold"));
        QCOMPARE(controls.at(2).toObject().value("id"), QJsonValue("appearance.meterStyles.peakDecay"));
    }

    void meterStyleBindingRejectsMutatedGrammar()
    {
        SetupDescriptionService service;
        const QJsonArray controls = service.category(QStringLiteral("appearance"))
            .value("pages").toArray().at(1).toObject().value("sections").toArray()
            .first().toObject().value("controls").toArray();
        QCOMPARE(controls.size(), 3);
        for (const QJsonValue& raw : controls) {
            const QJsonObject original = raw.toObject();
            QVERIFY(SetupDescriptionService::validateAppearanceMeterStyleBinding(original));
            const auto rejects = [&original](const QString& field, const QJsonValue& value) {
                QJsonObject changed = original;
                changed.insert(field, value);
                QVERIFY(!SetupDescriptionService::validateAppearanceMeterStyleBinding(changed));
            };
            rejects(QStringLiteral("id"), QStringLiteral("appearance.meterStyles.other"));
            rejects(QStringLiteral("label"), QStringLiteral("Different"));
            rejects(QStringLiteral("tooltip"), QStringLiteral("Different"));
            rejects(QStringLiteral("kind"), QStringLiteral("button"));
            rejects(QStringLiteral("binding"), QJsonObject{{"phone", "Other"}});
            rejects(QStringLiteral("binding"), QJsonObject{{"setting", "SMeter_FaceStyle"}});
            rejects(QStringLiteral("binding"), QJsonObject{{"phone", original.value("binding").toObject().value("phone")},
                                                            {"setting", "SMeter_FaceStyle"}});
            rejects(QStringLiteral("applies"), QStringLiteral("subscription"));
            rejects(QStringLiteral("requiresDescriptionVersion"), 6);
            rejects(QStringLiteral("gate"), QJsonObject{{"transmit", true}});
            rejects(QStringLiteral("valueEncoding"), QJsonObject{});
            if (original.value("kind") == QJsonValue("toggle")) {
                rejects(QStringLiteral("default"), 1);
                rejects(QStringLiteral("options"), QJsonArray{});
            } else {
                rejects(QStringLiteral("default"), true);
                rejects(QStringLiteral("default"), 42);
                rejects(QStringLiteral("default"), QStringLiteral("0"));
                rejects(QStringLiteral("options"), QJsonArray{});
                QJsonArray options = original.value("options").toArray();
                QJsonObject first = options.first().toObject();
                first.insert(QStringLiteral("value"), 99);
                options[0] = first;
                rejects(QStringLiteral("options"), options);
                options = original.value("options").toArray();
                first = options.first().toObject();
                first.insert(QStringLiteral("value"), QStringLiteral("0"));
                options[0] = first;
                rejects(QStringLiteral("options"), options);
                options = original.value("options").toArray();
                first = options.first().toObject();
                first.insert(QStringLiteral("label"), QStringLiteral("Different"));
                options[0] = first;
                rejects(QStringLiteral("options"), options);
                options = original.value("options").toArray();
                first = options.first().toObject();
                first.insert(QStringLiteral("extra"), 1);
                options[0] = first;
                rejects(QStringLiteral("options"), options);
                options = original.value("options").toArray();
                const QJsonValue firstOption = options.at(0);
                options[0] = options.at(1);
                options[1] = firstOption;
                rejects(QStringLiteral("options"), options);
            }
        }
    }

    void appearanceRejectsOtherBindingsAndMalformedRgba()
    {
        SetupDescriptionService service;
        const QJsonArray controls = service.category(QStringLiteral("appearance"))
            .value("pages").toArray().first().toObject().value("sections").toArray()
            .first().toObject().value("controls").toArray();
        QCOMPARE(controls.size(), 10);
        for (const QJsonValue& raw : controls) {
            QVERIFY(SetupDescriptionService::validateAppearanceColourBinding(raw.toObject()));
        }
        const QJsonObject original = controls.first().toObject();
        const auto rejects = [&original](const QString& field, const QJsonValue& value) {
            QJsonObject changed = original;
            changed.insert(field, value);
            QVERIFY(!SetupDescriptionService::validateAppearanceColourBinding(changed));
        };
        rejects(QStringLiteral("id"), QStringLiteral("appearance.colorsTheme.unbuilt"));
        rejects(QStringLiteral("kind"), QStringLiteral("text"));
        rejects(QStringLiteral("applies"), QStringLiteral("subscription"));
        rejects(QStringLiteral("default"), QStringLiteral("#00E5FF"));
        rejects(QStringLiteral("default"), QStringLiteral("#FF00E5FF")); // wrong channel order
        rejects(QStringLiteral("default"), QStringLiteral("#00E5FFFG"));
        rejects(QStringLiteral("default"), QStringLiteral("#00e5ffff"));
        rejects(QStringLiteral("gate"), QJsonObject{{"transmit", true}});
        rejects(QStringLiteral("valueEncoding"), QJsonObject{});
        rejects(QStringLiteral("binding"), QJsonObject{{"setting", "DisplayFillColor"}});
        rejects(QStringLiteral("binding"), QJsonObject{{"phone", "DisplayGridColor"}});
    }
    void paBypassIsAClosedVersionSixSettingAndSkuProjected()
    {
        RadioModel radio;
        radio.setHpsdrModelForTest(HPSDRModel::ANAN_G2E);
        SetupDescriptionService service;
        service.setRadioContext(radio.boardCapabilities(), radio.hardwareProfile().model);
        const QJsonObject pa = service.category(QStringLiteral("pa"));
        const QJsonArray pages = pa.value("pages").toArray();
        QCOMPARE(pages.size(), 2);
        QCOMPARE(pages.first().toObject().value("id"), QJsonValue("pa.gain"));
        const QJsonArray controls = pages.first().toObject().value("sections").toArray()
            .first().toObject().value("controls").toArray();
        QCOMPARE(controls.size(), 1);
        const QJsonObject valid = controls.first().toObject();
        QVERIFY(SetupDescriptionService::validatePaBypassBinding(valid));
        QCOMPARE(valid.value("id"), QJsonValue("pa.gain.bypassPaSettings"));
        QCOMPARE(valid.value("binding").toObject().value("property").toObject()
                     .value("name"), QJsonValue("paSettingsBypass"));
        QCOMPARE(valid.value("gate"), QJsonValue(QJsonObject{
            {"capability", "transmitSettingsVersion"}, {"min", 6}, {"offAir", true}}));
        QVERIFY(!valid.value("gate").toObject().contains("transmit"));
        const auto reject = [&valid](const QString& field, const QJsonValue& value) {
            QJsonObject bad = valid;
            bad.insert(field, value);
            QVERIFY(!SetupDescriptionService::validatePaBypassBinding(bad));
        };
        reject(QStringLiteral("kind"), QStringLiteral("readout"));
        reject(QStringLiteral("label"), QStringLiteral("Bypass PA"));
        reject(QStringLiteral("tooltip"), QStringLiteral("Wrong tooltip"));
        reject(QStringLiteral("gate"), QJsonObject{{"capability", "transmitSettingsVersion"},
                                                   {"min", 5}, {"offAir", true}});
        reject(QStringLiteral("gate"), QJsonObject{{"capability", "transmitSettingsVersion"},
                                                   {"min", 6}, {"offAir", true}, {"transmit", true}});
        reject(QStringLiteral("gate"), QJsonObject{{"capability", "transmitSettingsVersion"},
                                                   {"min", 6}});
        QJsonObject bad = valid;
        QJsonObject binding = bad.value("binding").toObject();
        QJsonObject ref = binding.value("property").toObject();
        ref.insert("name", "mox");
        binding.insert("property", ref);
        bad.insert("binding", binding);
        QVERIFY(!SetupDescriptionService::validatePaBypassBinding(bad));
        ref.insert("name", "paSettingsBypass");
        ref.insert("object", "txState");
        binding.insert("property", ref);
        bad.insert("binding", binding);
        QVERIFY(!SetupDescriptionService::validatePaBypassBinding(bad));

        const quint32 withGain = service.revision();
        BoardCapabilities sameBoard = radio.boardCapabilities();
        sameBoard.showsBypassPaSettingsUi = false;
        service.setRadioContext(sameBoard, radio.hardwareProfile().model);
        QVERIFY(service.revision() > withGain);
        const QJsonArray withoutGain = service.category(QStringLiteral("pa"))
            .value("pages").toArray();
        QCOMPARE(withoutGain.size(), 1);
        QCOMPARE(withoutGain.first().toObject().value("id"), QJsonValue("pa.values"));
        sameBoard.showsBypassPaSettingsUi = true;
        service.setRadioContext(sameBoard, radio.hardwareProfile().model);
        QCOMPARE(service.category(QStringLiteral("pa")).value("pages").toArray().size(), 2);
        sameBoard.isRxOnlySku = true;
        service.setRadioContext(sameBoard, radio.hardwareProfile().model);
        QVERIFY(service.pa().isEmpty());
        sameBoard.isRxOnlySku = false;
        sameBoard.hasPaProfile = false;
        service.setRadioContext(sameBoard, radio.hardwareProfile().model);
        QVERIFY(service.pa().isEmpty());
    }

    void paReadoutsAreClosedTypedBindingsAndAppendSchema()
    {
        RadioModel radio;
        radio.setBoardForTest(HPSDRHW::Saturn);
        SetupDescriptionService service;
        service.setRadioContext(radio.boardCapabilities(), radio.hardwareProfile().model);
        const QJsonObject pa = QJsonDocument::fromJson(
            SetupDescriptionService::fitCategoryForVersion(service.pa(), 4).toUtf8()).object();
        QCOMPARE(pa.value("version"), QJsonValue(4));
        QCOMPARE(pa.value("category").toObject().value("where"), QJsonValue("station"));
        QCOMPARE(pa.value("category").toObject().value("coverage"), QJsonValue("partial"));
        const QJsonArray sections = pa.value("pages").toArray().first().toObject()
            .value("sections").toArray();
        QCOMPARE(sections.size(), 3);
        QCOMPARE(sections.at(0).toObject().value("title"), QJsonValue("Power"));
        QCOMPARE(sections.at(1).toObject().value("title"), QJsonValue("PA Telemetry"));
        QCOMPARE(sections.at(2).toObject().value("title"), QJsonValue("Raw ADC Values"));
        QJsonArray controls = sections.at(0).toObject().value("controls").toArray();
        for (const QJsonValue& value : sections.at(2).toObject().value("controls").toArray()) {
            controls.append(value);
        }
        QCOMPARE(controls.size(), 7);
        const QStringList names{QStringLiteral("forwardPowerWatts"),
                                QStringLiteral("forwardRawPowerWatts"),
                                QStringLiteral("reflectedPowerWatts"), QStringLiteral("swr"),
                                QStringLiteral("power"), QStringLiteral("forwardAdcRaw"),
                                QStringLiteral("reflectedAdcRaw")};
        for (int i = 0; i < controls.size(); ++i) {
            const QJsonObject valid = controls.at(i).toObject();
            if (i == 4) {
                QVERIFY(SetupDescriptionService::validatePaDriveReadoutBinding(valid));
                QVERIFY(!SetupDescriptionService::validatePaReadoutBinding(valid));
                QCOMPARE(valid.value("binding").toObject().value("property").toObject()
                             .value("object"), QJsonValue("transmit"));
                QCOMPARE(valid.value("binding").toObject().value("property").toObject()
                             .value("name"), QJsonValue("power"));
                QCOMPARE(valid.value("gate").toObject(),
                         (QJsonObject{{"capability", "transmitSettingsVersion"}, {"min", 1}}));
                QCOMPARE(valid.value("decimals"), QJsonValue(0));
                QCOMPARE(valid.value("unit"), QJsonValue("W"));
                auto rejectDrive = [&valid](const QString& key, const QJsonValue& value) {
                    QJsonObject bad = valid;
                    bad.insert(key, value);
                    QVERIFY(!SetupDescriptionService::validatePaDriveReadoutBinding(bad));
                };
                rejectDrive("kind", "integer");
                rejectDrive("kind", "toggle");
                rejectDrive("decimals", 1);
                rejectDrive("unit", "dB");
                rejectDrive("gate", QJsonObject{{"capability", "transmitSettingsVersion"},
                                                {"min", 1}, {"offAir", true}});
                rejectDrive("gate", QJsonObject{{"capability", "transmitSettingsVersion"},
                                                {"min", 1}, {"transmit", true}});
                rejectDrive("binding", QJsonObject{{"property", QJsonObject{{"object", "transmit"},
                                                                      {"name", "paSettingsBypass"}}}});
                rejectDrive("binding", QJsonObject{{"property", QJsonObject{{"object", "txState"},
                                                                      {"name", "power"}}}});
                rejectDrive("binding", QJsonObject{{"setting", QJsonObject{{"key", "power"}}}});
                rejectDrive("binding", QJsonObject{{"property", QJsonObject{{"object", "transmit"},
                                                                      {"name", "power"}}},
                                                      {"write", true}});
                continue;
            }
            QVERIFY(SetupDescriptionService::validatePaReadoutBinding(valid));
            QCOMPARE(valid.value("binding").toObject().value("property").toObject()
                         .value("name").toString(), names.at(i));
            const bool raw = i >= 5;
            const int minimum = i == 1 ? 2 : 1;
            QCOMPARE(valid.value("decimals").toInt(), raw ? 0 : 2);
            QCOMPARE(valid.value("unit"), QJsonValue(i <= 2 ? "W" : ""));
            QCOMPARE(valid.value("gate"), QJsonValue(QJsonObject{
                {"capability", "txReadingsVersion"}, {"min", minimum}}));
            auto rejected = [&valid](const QString& field, const QJsonValue& value) {
                QJsonObject bad = valid;
                bad.insert(field, value);
                QVERIFY(!SetupDescriptionService::validatePaReadoutBinding(bad));
            };
            rejected(QStringLiteral("kind"), QStringLiteral("decimal"));
            rejected(QStringLiteral("decimals"), raw ? QJsonValue(2) : QJsonValue(0));
            rejected(QStringLiteral("decimals"), 7);
            rejected(QStringLiteral("decimals"), 1.5);
            rejected(QStringLiteral("unit"), QStringLiteral("dB"));
            rejected(QStringLiteral("gate"), QJsonObject{{"capability", "txStateVersion"}, {"min", 1}});
            rejected(QStringLiteral("gate"), QJsonObject{{"capability", "txReadingsVersion"},
                                                          {"min", minimum == 1 ? 2 : 1}});
            rejected(QStringLiteral("gate"), QJsonObject{{"capability", "txReadingsVersion"}, {"min", 1}, {"transmit", true}});
            rejected(QStringLiteral("gate"), QJsonObject{{"capability", "txReadingsVersion"}, {"min", 1}, {"offAir", true}});
            QJsonObject bad = valid;
            QJsonObject binding = bad.value("binding").toObject();
            QJsonObject ref = binding.value("property").toObject();
            ref.insert("object", "transmit");
            binding.insert("property", ref);
            bad.insert("binding", binding);
            QVERIFY(!SetupDescriptionService::validatePaReadoutBinding(bad));
            ref.insert("object", "txState");
            ref.insert("name", "alcDb");
            binding.insert("property", ref);
            bad.insert("binding", binding);
            QVERIFY(!SetupDescriptionService::validatePaReadoutBinding(bad));
        }
        const MirrorSchema& schema = MirrorSchema::forObject(&service);
        const MirrorProperty* paField = schema.byName("pa");
        const MirrorProperty* revision = schema.byName("revision");
        QVERIFY(paField != nullptr);
        QVERIFY(revision != nullptr);
        QCOMPARE(paField->ordinal, revision->ordinal + 1);
        QCOMPARE(paField->kind, MirrorWireKind::Utf8);
        QVERIFY(!paField->isWritable);
        QCOMPARE(MirrorPolicy::directionFor("SetupDescription", "pa"), MirrorDirection::Outbound);

        const quint32 presentRevision = service.revision();
        BoardCapabilities absent = radio.boardCapabilities();
        absent.hasPaProfile = false;
        service.setRadioContext(absent, radio.hardwareProfile().model);
        QVERIFY(service.pa().isEmpty());
        QVERIFY(service.revision() > presentRevision);
        absent.hasPaProfile = true;
        absent.isRxOnlySku = true;
        service.setRadioContext(absent, radio.hardwareProfile().model);
        QVERIFY(service.pa().isEmpty());
    }

    void paTelemetryRequiresV5AndKeepsOlderProjection()
    {
        RadioModel radio;
        radio.setBoardForTest(HPSDRHW::Saturn);
        SetupDescriptionService service;
        service.setRadioContext(radio.boardCapabilities(), radio.hardwareProfile().model);
        const QString source = service.pa();
        QVERIFY(!source.isEmpty());
        const auto findControls = [](const QJsonObject& category) {
            QJsonArray controls;
            for (const QJsonValue& page : category.value("pages").toArray()) {
                if (page.toObject().value("id") != QJsonValue("pa.values")) { continue; }
                for (const QJsonValue& section : page.toObject().value("sections").toArray()) {
                    for (const QJsonValue& control : section.toObject().value("controls").toArray()) {
                        controls.append(control);
                    }
                }
            }
            return controls;
        };
        for (int version = 1; version <= 4; ++version) {
            const QJsonObject older = QJsonDocument::fromJson(
                SetupDescriptionService::fitCategoryForVersion(source, version).toUtf8()).object();
            QCOMPARE(findControls(older).size(), 9);
        }
        const QJsonObject current = QJsonDocument::fromJson(
            SetupDescriptionService::fitCategoryForVersion(source, 5).toUtf8()).object();
        QCOMPARE(current.value("version"), QJsonValue(5));
        QCOMPARE(findControls(current).size(), 11);
        const QJsonArray telemetry = current.value("pages").toArray().last().toObject()
            .value("sections").toArray().at(1).toObject().value("controls").toArray();
        QCOMPARE(telemetry.size(), 4);
        const QStringList scaledNames{QStringLiteral("forwardAdcVolts"),
                                      QStringLiteral("reflectedAdcVolts")};
        for (int i = 2; i < 4; ++i) {
            const QJsonObject valid = telemetry.at(i).toObject();
            QVERIFY(SetupDescriptionService::validatePaReadoutBinding(valid));
            QCOMPARE(valid.value("binding").toObject().value("property").toObject()
                         .value("name"), QJsonValue(scaledNames.at(i - 2)));
            QCOMPARE(valid.value("gate"), QJsonValue(QJsonObject{
                {"capability", "txReadingsVersion"}, {"min", 2}}));
            QCOMPARE(valid.value("decimals"), QJsonValue(2));
            QCOMPARE(valid.value("unit"), QJsonValue("V"));
            for (const QJsonObject& wrong : {
                     QJsonObject{{"capability", "txReadingsVersion"}, {"min", 1}},
                     QJsonObject{{"capability", "txReadingsVersion"}, {"min", 2}, {"offAir", true}}}) {
                QJsonObject bad = valid;
                bad.insert("gate", wrong);
                QVERIFY(!SetupDescriptionService::validatePaReadoutBinding(bad));
            }
            QJsonObject bad = valid;
            bad.insert("binding", QJsonObject{{"property", QJsonObject{
                {"object", "txState"}, {"name", "alcDb"}}}});
            QVERIFY(!SetupDescriptionService::validatePaReadoutBinding(bad));
            bad = valid;
            bad.insert("binding", QJsonObject{{"property", QJsonObject{
                {"object", "txState"}, {"name", scaledNames.at(i - 2)}}}, {"write", true}});
            QVERIFY(!SetupDescriptionService::validatePaReadoutBinding(bad));
        }
        const QStringList names{QStringLiteral("paCurrentAmps"), QStringLiteral("supplyVolts")};
        for (int i = 0; i < 2; ++i) {
            const QJsonObject valid = telemetry.at(i).toObject();
            QVERIFY(SetupDescriptionService::validatePaTelemetryReadoutBinding(valid));
            QCOMPARE(valid.value("binding").toObject().value("telemetry").toObject()
                         .value("name"), QJsonValue(names.at(i)));
            const auto rejects = [&valid](const QString& key, const QJsonValue& value) {
                QJsonObject bad = valid;
                bad.insert(key, value);
                QVERIFY(!SetupDescriptionService::validatePaTelemetryReadoutBinding(bad));
            };
            rejects("kind", "integer");
            rejects("kind", "toggle");
            rejects("id", "pa.values.other");
            rejects("label", "Other:");
            rejects("decimals", i == 0 ? 1 : 2);
            rejects("decimals", QJsonValue(QJsonValue::Undefined));
            rejects("unit", "W");
            rejects("unit", QJsonValue(QJsonValue::Undefined));
            rejects("requiresDescriptionVersion", 4);
            rejects("requiresDescriptionVersion", QJsonValue(QJsonValue::Undefined));
            rejects("gate", QJsonObject{{"capability", "stationTelemetryVersion"}, {"min", 3}});
            rejects("gate", QJsonValue(QJsonValue::Undefined));
            rejects("gate", QJsonObject{{"capability", "stationTelemetryVersion"},
                                     {"min", 4}, {"offAir", true}});
            rejects("binding", QJsonObject{{"telemetry", QJsonObject{{"object", "radio"},
                                                                        {"name", "paVolts"}}}});
            rejects("binding", QJsonValue(QJsonValue::Undefined));
            rejects("binding", QJsonObject{{"telemetry", QJsonObject{{"object", "radio"},
                                                                        {"name", names.at(i)},
                                                                        {"write", true}}}});
            rejects("binding", QJsonObject{{"telemetry", QJsonObject{{"object", "host"},
                                                                        {"name", names.at(i)}}}});
            rejects("binding", QJsonObject{{"telemetry", QJsonObject{{"object", "radio"},
                                                                        {"name", names.at(i)}}},
                                             {"write", true}});
        }
        BoardCapabilities ampsAbsent = radio.boardCapabilities();
        ampsAbsent.hasPaAmpsTelemetry = false;
        service.setRadioContext(ampsAbsent, radio.hardwareProfile().model);
        const QJsonObject noAmps = QJsonDocument::fromJson(
            SetupDescriptionService::fitCategoryForVersion(service.pa(), 5).toUtf8()).object();
        QCOMPARE(findControls(noAmps).size(), 10);
        BoardCapabilities voltsAbsent = radio.boardCapabilities();
        voltsAbsent.hasPaVoltsTelemetry = false;
        service.setRadioContext(voltsAbsent, radio.hardwareProfile().model);
        const QJsonObject noVolts = QJsonDocument::fromJson(
            SetupDescriptionService::fitCategoryForVersion(service.pa(), 5).toUtf8()).object();
        QCOMPARE(findControls(noVolts).size(), 10);
    }

    void categoriesLoadAndMirrorAsStrings()
    {
        SetupDescriptionService service;
        for (const QString& id : {QStringLiteral("general"), QStringLiteral("test"),
                                  QStringLiteral("catNetwork"), QStringLiteral("dsp")}) {
            const QJsonObject category = service.category(id);
            QCOMPARE(category.value(QStringLiteral("version")).toInt(),
                     id == QLatin1String("dsp") ? 2 : 1);
            QCOMPARE(category.value(QStringLiteral("category")).toObject()
                         .value(QStringLiteral("id")).toString(), id);
            QVERIFY(!category.value(QStringLiteral("pages")).toArray().isEmpty());
            QVERIFY(!service.property(id.toLatin1().constData()).toString().isEmpty());
        }
        const QJsonObject diagnostics = service.category(QStringLiteral("diagnostics"));
        QCOMPARE(diagnostics.value(QStringLiteral("version")), QJsonValue(3));
        QCOMPARE(diagnostics.value(QStringLiteral("pages")).toArray().size(), 1);
        QVERIFY(!service.diagnostics().isEmpty());
        QCOMPARE(service.revision(), quint32(1));
        const MirrorSchema& schema = MirrorSchema::forObject(&service);
        QVERIFY(schema.byName("general"));
        QVERIFY(schema.byName("catNetwork"));
        QVERIFY(schema.byName("revision"));
    }

    void settingsHygienePanelIsClosedAndOnlyV3()
    {
        SetupDescriptionService service;
        const QJsonObject diagnostics = service.category(QStringLiteral("diagnostics"));
        QCOMPARE(diagnostics.value("category").toObject().value("where"), QJsonValue("station"));
        QCOMPARE(diagnostics.value("category").toObject().value("coverage"), QJsonValue("partial"));
        const QJsonObject page = diagnostics.value("pages").toArray().first().toObject();
        QCOMPARE(page.value("id"), QJsonValue("diagnostics.settingsValidation"));
        QCOMPARE(page.value("title"), QJsonValue("Settings Validation"));
        const QJsonObject section = page.value("sections").toArray().first().toObject();
        QCOMPARE(section.value("title"), QJsonValue("Validation Issues"));
        const QJsonArray controls = section.value("controls").toArray();
        QCOMPARE(controls.size(), 1);
        const QJsonObject panel = controls.first().toObject();
        QVERIFY(SetupDescriptionService::validateSettingsHygienePanel(panel));
        QCOMPARE(panel.value("id"), QJsonValue("diagnostics.settingsValidation.health"));
        QCOMPARE(panel.value("label"), QJsonValue("Validation Issues"));
        QCOMPARE(panel.value("tooltip"), QJsonValue(""));
        QCOMPARE(panel.value("kind"), QJsonValue("settingsHygiene"));
        QCOMPARE(panel.value("requiresDescriptionVersion"), QJsonValue(3));
        QCOMPARE(panel.value("gate").toObject(),
                 (QJsonObject{{"capability", "settingsHygieneVersion"}, {"min", 1}}));
        QCOMPARE(panel.value("binding").toObject(),
                 (QJsonObject{{"settingsHygiene", QJsonObject{{"version", 1}}}}));
        QCOMPARE(panel.value("actions").toArray().size(), 3);
        QCOMPARE(panel.value("actions").toArray().at(0).toObject().value("label"),
                 QJsonValue("Re-validate"));
        QCOMPARE(panel.value("actions").toArray().at(1).toObject().value("enabled"),
                 QJsonValue(false));
        QCOMPARE(panel.value("actions").toArray().at(2).toObject().value("label"),
                 QJsonValue("Forget This Radio"));

        const auto reject = [&panel](const QString& key, const QJsonValue& value) {
            QJsonObject changed = panel;
            changed.insert(key, value);
            QVERIFY(!SetupDescriptionService::validateSettingsHygienePanel(changed));
        };
        reject("extra", true);
        reject("kind", "button");
        reject("requiresDescriptionVersion", 2);
        reject("binding", QJsonObject{{"command", QJsonObject{{"verb", "station.forgetSettings"}}}});
        reject("binding", QJsonObject{{"settingsHygiene", QJsonObject{{"version", 2}}}});
        reject("gate", QJsonObject{{"capability", "settingsHygieneVersion"}, {"min", 0}});
        reject("gate", QJsonObject{{"capability", "settingsHygieneVersion"},
                                    {"min", 1}, {"transmit", true}});
        QJsonArray actions = panel.value("actions").toArray();
        QJsonObject reset = actions.at(1).toObject();
        reset.insert("enabled", true);
        actions[1] = reset;
        reject("actions", actions);
        actions = panel.value("actions").toArray();
        QJsonObject validate = actions.at(0).toObject();
        validate.insert("verb", "station.forgetSettings");
        actions[0] = validate;
        reject("actions", actions);
        actions = panel.value("actions").toArray();
        const QJsonValue firstAction = actions.at(0);
        actions[0] = actions.at(2);
        actions[2] = firstAction;
        reject("actions", actions);
        actions = panel.value("actions").toArray();
        QJsonObject forget = actions.at(2).toObject();
        forget.remove("paired");
        actions[2] = forget;
        reject("actions", actions);
        forget = panel.value("actions").toArray().at(2).toObject();
        forget.remove("offAir");
        actions[2] = forget;
        reject("actions", actions);
        forget = panel.value("actions").toArray().at(2).toObject();
        forget.remove("confirmation");
        actions[2] = forget;
        reject("actions", actions);
        forget = panel.value("actions").toArray().at(2).toObject();
        forget.insert("verb", "station.forgetRadio");
        actions[2] = forget;
        reject("actions", actions);
        actions = panel.value("actions").toArray();
        forget = actions.at(2).toObject();
        QJsonObject confirmation = forget.value("confirmation").toObject();
        confirmation.insert("default", "confirm");
        forget.insert("confirmation", confirmation);
        actions[2] = forget;
        reject("actions", actions);

        QVERIFY(SetupDescriptionService::fitCategoryForVersion(service.diagnostics(), 1).isEmpty());
        QVERIFY(SetupDescriptionService::fitCategoryForVersion(service.diagnostics(), 2).isEmpty());
        const QJsonObject v3 = QJsonDocument::fromJson(
            SetupDescriptionService::fitCategoryForVersion(service.diagnostics(), 3).toUtf8())
            .object();
        QCOMPARE(v3.value("version"), QJsonValue(3));
        QCOMPARE(v3.value("pages").toArray().size(), 1);
    }

    void settingToggleEncodingIsExactAndPersistsAsReaderStrings()
    {
        SetupDescriptionService service;
        const auto findControl = [&service](const QString& category, const QString& id) {
            const QJsonObject root = service.category(category);
            for (const QJsonValue& page : root.value(QStringLiteral("pages")).toArray()) {
                for (const QJsonValue& section : page.toObject()
                         .value(QStringLiteral("sections")).toArray()) {
                    for (const QJsonValue& raw : section.toObject()
                             .value(QStringLiteral("controls")).toArray()) {
                        const QJsonObject control = raw.toObject();
                        if (control.value(QStringLiteral("id")) == QJsonValue(id)) {
                            return control;
                        }
                    }
                }
            }
            return QJsonObject{};
        };
        const QStringList generalKeys = {
            QStringLiteral("RxOnly"), QStringLiteral("NetworkWatchdogEnabled"),
            QStringLiteral("MoxTimeOutEnabled"), QStringLiteral("PingTimeOutEnabled"),
            QStringLiteral("RemoteMoxTimeOutEnabled")};
        int generalCount = 0;
        for (const QJsonValue& page : service.category(QStringLiteral("general"))
                 .value(QStringLiteral("pages")).toArray()) {
            for (const QJsonValue& section : page.toObject()
                     .value(QStringLiteral("sections")).toArray()) {
                for (const QJsonValue& raw : section.toObject()
                         .value(QStringLiteral("controls")).toArray()) {
                    const QJsonObject control = raw.toObject();
                    if (control.value(QStringLiteral("kind")) != QJsonValue(QStringLiteral("toggle"))
                        || !control.value(QStringLiteral("binding")).toObject()
                                .contains(QStringLiteral("setting"))) {
                        continue;
                    }
                    QVERIFY(generalKeys.contains(control.value(QStringLiteral("binding"))
                        .toObject().value(QStringLiteral("setting")).toString()));
                    QVERIFY(SetupDescriptionService::validateSettingToggleEncoding(control));
                    ++generalCount;
                }
            }
        }
        QCOMPARE(generalCount, 5);

        const QJsonObject watchdog = findControl(QStringLiteral("general"),
            QStringLiteral("general.options.networkWatchdog"));
        QVERIFY(!watchdog.isEmpty());
        QJsonObject bad = watchdog;
        bad.remove(QStringLiteral("valueEncoding"));
        QVERIFY(!SetupDescriptionService::validateSettingToggleEncoding(bad));
        bad = watchdog;
        bad.insert(QStringLiteral("valueEncoding"), QJsonObject{
            {QStringLiteral("true"), QStringLiteral("True")}});
        QVERIFY(!SetupDescriptionService::validateSettingToggleEncoding(bad));
        bad = watchdog;
        bad.insert(QStringLiteral("valueEncoding"), QJsonObject{
            {QStringLiteral("true"), QStringLiteral("true")},
            {QStringLiteral("false"), QStringLiteral("False")}});
        QVERIFY(!SetupDescriptionService::validateSettingToggleEncoding(bad));
        bad = watchdog;
        bad.insert(QStringLiteral("valueEncoding"), QJsonObject{
            {QStringLiteral("true"), QStringLiteral("True")},
            {QStringLiteral("false"), QStringLiteral("False")},
            {QStringLiteral("fallback"), QStringLiteral("False")}});
        QVERIFY(!SetupDescriptionService::validateSettingToggleEncoding(bad));
        bad = findControl(QStringLiteral("dsp"),
            QStringLiteral("dsp.agcAlc.autoAgcEnabled"));
        QVERIFY(!bad.isEmpty());
        bad.insert(QStringLiteral("valueEncoding"), watchdog.value(QStringLiteral("valueEncoding")));
        QVERIFY(!SetupDescriptionService::validateSettingToggleEncoding(bad));

        AppSettings& store = AppSettings::instance();
        const QStringList keys = {QStringLiteral("RxOnly"),
            QStringLiteral("MoxTimeOutEnabled"), QStringLiteral("PingTimeOutEnabled"),
            QStringLiteral("RemoteMoxTimeOutEnabled"), QStringLiteral("NetworkWatchdogEnabled"),
            QStringLiteral("DspOptionsCacheImpulse"),
            QStringLiteral("DspOptionsCacheImpulseSaveRestore")};
        QHash<QString, QVariant> oldValues;
        for (const QString& key : keys) {
            if (store.contains(key)) {
                oldValues.insert(key, store.value(key));
            }
        }
        const auto restore = qScopeGuard([&store, &keys, &oldValues] {
            for (const QString& key : keys) {
                if (oldValues.contains(key)) {
                    store.setValue(key, oldValues.value(key));
                } else {
                    store.remove(key);
                }
            }
        });
        SettingsProxyServer server(store);
        QSignalSpy changed(&server, &SettingsProxyServer::outboundValueChanged);
        const QStringList ids = {QStringLiteral("general.options.rxOnly"),
            QStringLiteral("general.options.moxTimeoutEnabled"),
            QStringLiteral("general.options.pingTimeoutEnabled"),
            QStringLiteral("general.options.remoteTimeoutEnabled"),
            QStringLiteral("general.options.networkWatchdog"),
            QStringLiteral("dsp.options.DspOptionsCacheImpulse"),
            QStringLiteral("dsp.options.DspOptionsCacheImpulseSaveRestore")};
        for (const QString& id : ids) {
            const QJsonObject control = findControl(id.startsWith(QStringLiteral("general."))
                ? QStringLiteral("general") : QStringLiteral("dsp"), id);
            QVERIFY2(!control.isEmpty(), qPrintable(id));
            QVERIFY(SetupDescriptionService::validateSettingToggleEncoding(control));
            const QString key = control.value(QStringLiteral("binding"))
                .toObject().value(QStringLiteral("setting")).toString();
            const QJsonObject encoding = control.value(QStringLiteral("valueEncoding")).toObject();
            for (const bool on : {true, false}) {
                const QString token = encoding.value(on ? QStringLiteral("true")
                    : QStringLiteral("false")).toString();
                QVERIFY(server.applyInboundWrite(key, token, QStringLiteral("phone-test")).accepted);
                QCOMPARE(store.value(key).toString(), token);
                QVERIFY(!changed.isEmpty());
                const QList<QVariant> broadcast = changed.takeLast();
                QCOMPARE(broadcast.at(0).toString(), key);
                QCOMPARE(broadcast.at(1).metaType(), QMetaType::fromType<QString>());
                QCOMPARE(broadcast.at(1).toString(), token);
                if (key == QLatin1String("NetworkWatchdogEnabled")) {
                    QCOMPARE(RadioModel::networkWatchdogSetting(), on);
                } else if (key == QLatin1String("RxOnly")) {
                    QCOMPARE(RadioModel::rxOnlySetting(), on);
                } else if (key == QLatin1String("MoxTimeOutEnabled")) {
                    QCOMPARE(RadioModel::txTimeOutSettingsFor(QStringLiteral("station")).moxEnabled, on);
                } else if (key == QLatin1String("PingTimeOutEnabled")) {
                    QCOMPARE(RadioModel::txTimeOutSettingsFor(QStringLiteral("station")).pingEnabled, on);
                } else if (key == QLatin1String("RemoteMoxTimeOutEnabled")) {
                    QCOMPARE(RadioModel::txTimeOutSettingsFor(QStringLiteral("phone")).moxEnabled, on);
                    QCOMPARE(RadioModel::txTimeOutSettingsFor(QStringLiteral("tablet")).moxEnabled, on);
                } else {
                    // WdspEngine::finishInitialization/shutdown read both
                    // cache keys with this exact string comparison.
                    QCOMPARE(store.value(key, QStringLiteral("False")).toString()
                                 == QStringLiteral("True"), on);
                }
            }
        }
    }

    void transmitDexpBindingsAreExactMirroredSettings()
    {
        SetupDescriptionService service;
        const QJsonObject transmit = service.category(QStringLiteral("transmit"));
        QVERIFY(!transmit.isEmpty());
        int count = 0;
        for (const QJsonValue& rawPage : transmit.value(QStringLiteral("pages")).toArray()) {
            for (const QJsonValue& rawSection : rawPage.toObject().value(QStringLiteral("sections")).toArray()) {
                for (const QJsonValue& raw : rawSection.toObject().value(QStringLiteral("controls")).toArray()) {
                    const QJsonObject control = raw.toObject();
                    QVERIFY(SetupDescriptionService::validateTransmitPropertyBinding(control)
                        || SetupDescriptionService::validateTransmitSettingBinding(control));
                    QVERIFY(control.value(QStringLiteral("gate")).toObject()
                        .value(QStringLiteral("transmit")) == QJsonValue(true));
                    ++count;
                }
            }
        }
        QCOMPARE(count, 24);
        QJsonObject invalid = transmit.value(QStringLiteral("pages")).toArray().last()
            .toObject().value(QStringLiteral("sections")).toArray().first()
            .toObject().value(QStringLiteral("controls")).toArray().first().toObject();
        QJsonObject gate = invalid.value(QStringLiteral("gate")).toObject();
        gate.remove(QStringLiteral("transmit"));
        invalid.insert(QStringLiteral("gate"), gate);
        QVERIFY(!SetupDescriptionService::validateTransmitPropertyBinding(invalid));
        gate.insert(QStringLiteral("transmit"), true);
        gate.remove(QStringLiteral("offAir"));
        invalid.insert(QStringLiteral("gate"), gate);
        QVERIFY(!SetupDescriptionService::validateTransmitPropertyBinding(invalid));
    }

    void audioTxFilterBindingsAreExactMirroredProperties()
    {
        SetupDescriptionService service;
        const QJsonObject audio = service.category(QStringLiteral("audio"));
        QVERIFY(!audio.isEmpty());
        const QJsonArray described = audio.value(QStringLiteral("pages")).toArray()
            .first().toObject().value(QStringLiteral("sections")).toArray()
            .first().toObject().value(QStringLiteral("controls")).toArray();
        QCOMPARE(described.size(), 3);
        for (const QJsonValue& raw : described) {
            QVERIFY(SetupDescriptionService::validateAudioPropertyBinding(raw.toObject()));
        }
        QJsonObject bad = described.first().toObject();
        QJsonObject gate = bad.value(QStringLiteral("gate")).toObject();
        gate.remove(QStringLiteral("transmit"));
        bad.insert(QStringLiteral("gate"), gate);
        QVERIFY(!SetupDescriptionService::validateAudioPropertyBinding(bad));
        gate.insert(QStringLiteral("transmit"), true);
        gate.remove(QStringLiteral("offAir"));
        bad.insert(QStringLiteral("gate"), gate);
        QVERIFY(!SetupDescriptionService::validateAudioPropertyBinding(bad));
        bad = described.first().toObject();
        QJsonObject binding = bad.value(QStringLiteral("binding")).toObject();
        QJsonObject property = binding.value(QStringLiteral("property")).toObject();
        property.insert(QStringLiteral("name"), QStringLiteral("mox"));
        binding.insert(QStringLiteral("property"), property);
        bad.insert(QStringLiteral("binding"), binding);
        QVERIFY(!SetupDescriptionService::validateAudioPropertyBinding(bad));
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

    void hardwareScalarsProjectAndRejectUnsafeBindings()
    {
        SetupDescriptionService service;
        QVERIFY(service.hardware().isEmpty());
        service.setBoardCapabilities(BoardCapsTable::forBoard(HPSDRHW::Hermes));
        const auto v1Hardware = [&service] {
            return QJsonDocument::fromJson(SetupDescriptionService::fitCategoryForVersion(
                service.hardware(), 1).toUtf8()).object();
        };
        const QJsonObject hardware = v1Hardware();
        QCOMPARE(hardware.value("version").toInt(), 1);
        QCOMPARE(hardware.value("category").toObject().value("coverage"), QJsonValue("partial"));
        const QJsonArray pages = hardware.value("pages").toArray();
        QCOMPARE(pages.size(), 1);
        QCOMPARE(pages.first().toObject().value("id"), QJsonValue("hardware.antennaAlex"));
        const QJsonArray controls = pages.first().toObject().value("sections").toArray()
            .first().toObject().value("controls").toArray();
        QCOMPARE(controls.size(), 3);
        QCOMPARE(controls.at(0).toObject().value("id"), QJsonValue("hardware.antennaAlex.blockTxAnt2"));
        QCOMPARE(controls.at(1).toObject().value("id"), QJsonValue("hardware.antennaAlex.blockTxAnt3"));
        QCOMPARE(controls.at(2).toObject().value("id"), QJsonValue("hardware.antennaAlex.useTxAntennaForRx"));
        for (const QJsonValue& raw : controls) {
            QVERIFY(SetupDescriptionService::validateHardwarePropertyBinding(raw.toObject()));
        }
        const QJsonObject tx = controls.first().toObject();
        const QJsonObject rx = controls.last().toObject();
        const auto rejectGate = [](const QJsonObject& source, const QString& key,
                                   const QJsonValue& value) {
            QJsonObject changed = source;
            QJsonObject gate = changed.value("gate").toObject();
            gate.insert(key, value);
            changed.insert("gate", gate);
            QVERIFY(!SetupDescriptionService::validateHardwarePropertyBinding(changed));
        };
        rejectGate(tx, QStringLiteral("offAir"), false);
        rejectGate(tx, QStringLiteral("min"), 5);
        rejectGate(tx, QStringLiteral("transmit"), true);
        rejectGate(rx, QStringLiteral("offAir"), true);
        rejectGate(rx, QStringLiteral("board"), QStringLiteral("hasAlexFilters"));
        QJsonObject mislabeled = tx;
        mislabeled.insert("label", "Use TX antenna for RX");
        QVERIFY(!SetupDescriptionService::validateHardwarePropertyBinding(mislabeled));
        QJsonObject wrongProperty = tx;
        QJsonObject binding = wrongProperty.value("binding").toObject();
        QJsonObject ref = binding.value("property").toObject();
        ref.insert("name", "rxOutOnTx");
        binding.insert("property", ref);
        wrongProperty.insert("binding", binding);
        QVERIFY(!SetupDescriptionService::validateHardwarePropertyBinding(wrongProperty));

        service.setRadioContext(BoardCapsTable::forBoard(HPSDRHW::Hermes),
                                HPSDRModel::ANAN100);
        const QJsonArray classic = v1Hardware()
            .value("pages").toArray().first().toObject().value("sections").toArray()
            .first().toObject().value("controls").toArray();
        QCOMPARE(classic.size(), 7);
        QCOMPARE(classic.at(2).toObject().value("id"), QJsonValue("hardware.antennaAlex.rxOutOnTx"));
        QCOMPARE(classic.at(3).toObject().value("id"), QJsonValue("hardware.antennaAlex.ext1OutOnTx"));
        QCOMPARE(classic.at(4).toObject().value("id"), QJsonValue("hardware.antennaAlex.ext2OutOnTx"));
        QCOMPARE(classic.at(5).toObject().value("id"), QJsonValue("hardware.antennaAlex.rxOutOverride"));
        for (int index = 2; index <= 5; ++index) {
            const QJsonObject relay = classic.at(index).toObject();
            QVERIFY(SetupDescriptionService::validateHardwarePropertyBinding(relay,
                                                                               HPSDRModel::ANAN100));
            QVERIFY(!SetupDescriptionService::validateHardwarePropertyBinding(relay,
                                                                                HPSDRModel::ANAN10));
            QJsonObject unsafe = relay;
            QJsonObject gate = unsafe.value("gate").toObject();
            gate.remove("offAir");
            unsafe.insert("gate", gate);
            QVERIFY(!SetupDescriptionService::validateHardwarePropertyBinding(unsafe,
                                                                                HPSDRModel::ANAN100));
            unsafe = relay;
            unsafe.insert("label", "Wrong relay");
            QVERIFY(!SetupDescriptionService::validateHardwarePropertyBinding(unsafe,
                                                                                HPSDRModel::ANAN100));
        }
        QCOMPARE(classic.at(2).toObject().value("gate").toObject().value("min"), QJsonValue(5));
        QCOMPARE(classic.at(3).toObject().value("gate").toObject().value("min"), QJsonValue(6));

        const quint32 classicRevision = service.revision();
        service.setRadioContext(BoardCapsTable::forBoard(HPSDRHW::Hermes),
                                HPSDRModel::ANAN10);
        QVERIFY(service.revision() > classicRevision);
        QVERIFY(!service.hardware().contains(QStringLiteral("ext1OutOnTx")));
        const quint32 hiddenRevision = service.revision();
        service.setRadioContext(BoardCapsTable::forBoard(HPSDRHW::Hermes),
                                HPSDRModel::ANAN10E);
        QCOMPARE(service.revision(), hiddenRevision);
        service.setRadioContext(BoardCapsTable::forBoard(HPSDRHW::Hermes),
                                HPSDRModel::ANAN100);
        QCOMPARE(v1Hardware().value("pages").toArray()
                     .first().toObject().value("sections").toArray().first().toObject()
                     .value("controls").toArray(), classic);
        service.setBoardCapabilities(BoardCapsTable::forBoard(HPSDRHW::Hermes));
        QCOMPARE(v1Hardware(), hardware);

        const quint32 beforeRetire = service.revision();
        service.setBoardCapabilities(BoardCapsTable::forBoard(HPSDRHW::HermesLite));
        QVERIFY(service.hardware().isEmpty());
        QVERIFY(service.category(QStringLiteral("hardware")).isEmpty());
        QVERIFY(service.revision() > beforeRetire);
        service.setBoardCapabilities(BoardCapsTable::forBoard(HPSDRHW::Hermes));
        QCOMPARE(v1Hardware(), hardware);

        WireCore described(HPSDRHW::Hermes);
        QVERIFY(described.connect({{QByteArrayLiteral("setupDescription"), 1}}));
        QVERIFY(!setupCategoryOnWire(*described.app, "hardware",
                                     SessionMessageKind::ObjectCreate).isEmpty());
        WireCore oldMinor(HPSDRHW::Hermes);
        QVERIFY(oldMinor.connect({{QByteArrayLiteral("setupDescription"), 1}},
                                 quint16(kRadioIdentitySessionProtocolMinor - 1)));
        QVERIFY(!hasSetupTraffic(*oldMinor.app));
        WireCore undeclared(HPSDRHW::Hermes);
        QVERIFY(undeclared.connect());
        QVERIFY(!hasSetupTraffic(*undeclared.app));
    }

    void sameBoardSkuChangePublishesRelayDelta()
    {
        WireCore core(HPSDRHW::Hermes);
        QVERIFY(core.connect({{QByteArrayLiteral("setupDescription"), 1}}));
        core.app->clearReceived();
        core.model->setHpsdrModelForTest(HPSDRModel::ANAN100);
        core.model->currentRadioChanged(core.model->currentRadioInfo());
        QString published;
        QTRY_VERIFY(!(published = setupCategoryOnWire(
            *core.app, "hardware", SessionMessageKind::Delta)).isEmpty());
        QVERIFY(published.contains(QStringLiteral("rxOutOverride")));
        const quint32 classicRevision = core.server->setupDescription()->revision();

        core.app->clearReceived();
        core.model->setHpsdrModelForTest(HPSDRModel::ANAN10);
        core.model->currentRadioChanged(core.model->currentRadioInfo());
        QTRY_VERIFY(!(published = setupCategoryOnWire(
            *core.app, "hardware", SessionMessageKind::Delta)).isEmpty());
        QVERIFY(!published.contains(QStringLiteral("rxOutOnTx")));
        QVERIFY(!published.contains(QStringLiteral("ext1OutOnTx")));
        QVERIFY(core.server->setupDescription()->revision() > classicRevision);
        const quint32 hiddenRevision = core.server->setupDescription()->revision();

        core.app->clearReceived();
        core.model->setHpsdrModelForTest(HPSDRModel::ANAN10E);
        core.model->currentRadioChanged(core.model->currentRadioInfo());
        QCoreApplication::processEvents();
        QCOMPARE(core.server->setupDescription()->revision(), hiddenRevision);
        QVERIFY(setupCategoryOnWire(*core.app, "hardware", SessionMessageKind::Delta).isEmpty());
    }

    void dspPropertiesAreWritableOnTheSelectedSliceAlias()
    {
        SetupDescriptionService service;
        const QJsonArray pages = service.category(QStringLiteral("dsp"))
                                     .value(QStringLiteral("pages")).toArray();
        QCOMPARE(pages.size(), 9);
        const MirrorSchema& sliceSchema = MirrorSchema::forMetaObject(
            &SliceModel::staticMetaObject);
        const MirrorSchema& transmitSchema = MirrorSchema::forMetaObject(
            &TransmitModel::staticMetaObject);
        const MirrorSchema& notchSchema = MirrorSchema::forMetaObject(
            &NotchModel::staticMetaObject);
        int count = 0;
        int transmitCount = 0;
        int settingCount = 0;
        int notchCount = 0;
        int readoutCount = 0;
        for (const QJsonValue& rawPage : pages) {
            for (const QJsonValue& rawSection : rawPage.toObject()
                     .value(QStringLiteral("sections")).toArray()) {
                for (const QJsonValue& rawControl : rawSection.toObject()
                         .value(QStringLiteral("controls")).toArray()) {
                    if (rawControl.toObject().value("kind") == QJsonValue("table")) {
                        QString error;
                        QVERIFY2(SetupDescriptionService::validateTnfTable(
                            rawControl.toObject(), &error), qPrintable(error));
                        continue;
                    }
                    if (rawControl.toObject().value("binding").toObject()
                            .contains("command")) {
                        QString error;
                        QVERIFY2(SetupDescriptionService::validateCommandBinding(
                            rawControl.toObject(), &error), qPrintable(error));
                        continue;
                    }
                    if (rawControl.toObject().value("binding").toObject()
                            .contains("setting")) {
                        QVERIFY(SetupDescriptionService::validateDspSettingBinding(
                            rawControl.toObject()));
                        ++settingCount;
                        continue;
                    }
                    const QJsonObject ref = rawControl.toObject().value("binding")
                        .toObject().value("property").toObject();
                    QVERIFY(SetupDescriptionService::validateActiveSlicePropertyBinding(
                        rawControl.toObject()));
                    const QString object = ref.value("object").toString();
                    QVERIFY(object == QLatin1String("slice:active")
                            || object == QLatin1String("transmit")
                            || object == QLatin1String("notches"));
                    const QByteArray name = ref.value("name").toString().toUtf8();
                    const bool transmit = object == QLatin1String("transmit");
                    const bool notch = object == QLatin1String("notches");
                    const MirrorProperty* property = transmit ? transmitSchema.byName(name)
                        : notch ? notchSchema.byName(name) : sliceSchema.byName(name);
                    QVERIFY2(property != nullptr, name.constData());
                    if (rawControl.toObject().value("kind") == QJsonValue("readout")) {
                        QVERIFY(!property->isWritable);
                        ++readoutCount;
                        continue;
                    }
                    QVERIFY(property->isWritable);
                    QVERIFY(MirrorPolicy::inboundAllowed(
                        transmit ? "TransmitModel" : notch ? "NotchModel" : "SliceModel", name));
                    if (transmit) { ++transmitCount; }
                    else if (notch) { ++notchCount; }
                    else { ++count; }
                }
            }
        }
        QCOMPARE(count, 63);
        QCOMPARE(transmitCount, 14);
        QCOMPARE(settingCount, 23);
        QCOMPARE(notchCount, 1);
        QCOMPARE(readoutCount, 1);
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
        QJsonObject options = pages.at(8).toObject().value("sections").toArray()
            .first().toObject().value("controls").toArray().first().toObject();
        QVERIFY(SetupDescriptionService::validateDspSettingBinding(options));
        bad = options;
        bad.insert("kind", "toggle");
        QVERIFY(!SetupDescriptionService::validateDspSettingBinding(bad));
        bad = options;
        QJsonObject settingBinding = bad.value("binding").toObject();
        settingBinding.insert("setting", "DspOptionsBufferSizeCwTx");
        bad.insert("binding", settingBinding);
        QVERIFY(!SetupDescriptionService::validateDspSettingBinding(bad));
        bad = options;
        QJsonArray wrongChoices = bad.value("choices").toArray();
        wrongChoices[0] = "2048";
        bad.insert("choices", wrongChoices);
        QVERIFY(!SetupDescriptionService::validateDspSettingBinding(bad));
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
        QCOMPARE(described.at(described.size() - 2).name, QByteArray("setupDescriptionVersion"));
        QCOMPARE(described.last().name, QByteArray("accessoryTxVersion"));
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

    void negotiatedVersionsFitInitialSnapshotAndLaterDelta()
    {
        const auto check = [](int declared, quint16 minor, int expected) {
            WireCore core(HPSDRHW::HermesC10);
            QHash<QByteArray, int> features;
            if (declared > 0) { features.insert("setupDescription", declared); }
            QVERIFY(core.connect(features, minor));
            QCOMPARE(setupCapabilityOnWire(*core.app), expected);
            if (expected == 0) {
                QVERIFY(!hasSetupTraffic(*core.app));
                return;
            }
            const QString dsp = setupCategoryOnWire(
                *core.app, "dsp", SessionMessageKind::ObjectCreate);
            QVERIFY(!dsp.isEmpty());
            const QJsonObject display = QJsonDocument::fromJson(setupCategoryOnWire(
                *core.app, "display", SessionMessageKind::ObjectCreate).toUtf8()).object();
            QCOMPARE(display.value("version"),
                     QJsonValue(expected >= 10 ? 10 : expected >= 9 ? 9 : expected >= 8 ? 8 : qMin(expected, 4)));
            int displayControls = 0;
            for (const QJsonValue& page : display.value("pages").toArray()) {
                for (const QJsonValue& section : page.toObject().value("sections").toArray()) {
                    displayControls += section.toObject().value("controls").toArray().size();
                }
            }
            QCOMPARE(displayControls, expected >= 10 ? 33 : expected >= 9 ? 29 : expected >= 8 ? 21 : expected >= 4 ? 14 : 11);
            QCOMPARE(display.value("pages").toArray().size(), expected >= 8 ? 4 : 3);
            const QJsonObject appearance = QJsonDocument::fromJson(setupCategoryOnWire(
                *core.app, "appearance", SessionMessageKind::ObjectCreate).toUtf8()).object();
            QCOMPARE(appearance.value("version"), QJsonValue(expected >= 7 ? 7 : qMin(expected, 4)));
            QCOMPARE(appearance.value("pages").toArray().size(), expected >= 7 ? 2 : 1);
            QCOMPARE(appearance.value("pages").toArray().first().toObject()
                         .value("sections").toArray().first().toObject()
                         .value("controls").toArray().size(), 10);
            if (expected < 4) {
                for (const QJsonObject& projected : {display, appearance}) {
                    for (const QJsonValue& page : projected.value("pages").toArray()) {
                        for (const QJsonValue& section : page.toObject().value("sections").toArray()) {
                            for (const QJsonValue& raw : section.toObject().value("controls").toArray()) {
                                const QJsonObject control = raw.toObject();
                                QVERIFY(!control.contains("default"));
                                if (control.value("kind") == QJsonValue("decimal")) {
                                    QVERIFY(!control.contains("decimals"));
                                }
                            }
                        }
                    }
                }
            }
            const QString pa = setupCategoryOnWire(
                *core.app, "pa", SessionMessageKind::ObjectCreate);
            QVERIFY(!pa.isEmpty());
            const QJsonObject paObject = QJsonDocument::fromJson(pa.toUtf8()).object();
            QCOMPARE(paObject.value("version").toInt(),
                     qMin(expected, 5));
            QCOMPARE(paObject.value("pages").toArray().size(), 2);
            for (const QJsonValue& page : paObject.value("pages").toArray()) {
                for (const QJsonValue& section : page.toObject().value("sections").toArray()) {
                    for (const QJsonValue& raw : section.toObject().value("controls").toArray()) {
                        const QJsonObject control = raw.toObject();
                        if (control.value("kind") == QJsonValue("readout")) {
                            QVERIFY(control.value("decimals").isDouble());
                        }
                    }
                }
            }
            const QString diagnostics = setupCategoryOnWire(
                *core.app, "diagnostics", SessionMessageKind::ObjectCreate);
            if (expected < 3) {
                QVERIFY(diagnostics.isEmpty());
            } else {
                const QJsonObject panel = QJsonDocument::fromJson(diagnostics.toUtf8()).object();
                QCOMPARE(panel.value("version"), QJsonValue(3));
                QCOMPARE(panel.value("pages").toArray().size(), 1);
            }
            const QJsonObject category = QJsonDocument::fromJson(dsp.toUtf8()).object();
            QCOMPARE(category.value("version").toInt(), qMin(expected, 3));
            bool hasTable = false;
            bool hasAdd = false;
            for (const QJsonValue& page : category.value("pages").toArray()) {
                for (const QJsonValue& section : page.toObject().value("sections").toArray()) {
                    for (const QJsonValue& raw : section.toObject().value("controls").toArray()) {
                        const QString id = raw.toObject().value("id").toString();
                        hasTable |= id == QLatin1String("dsp.tnf.list");
                        hasAdd |= id == QLatin1String("dsp.tnf.add");
                    }
                }
            }
            QCOMPARE(hasTable, expected >= 2);
            QCOMPARE(hasAdd, expected >= 2);
            const QString v1 = SetupDescriptionService::fitCategoryForVersion(
                core.server->setupDescription()->dsp(), 1);
            QVERIFY(!v1.contains(QStringLiteral("requiresDescriptionVersion")));
            QCOMPARE(QJsonDocument::fromJson(v1.toUtf8()).object().value("version").toInt(), 1);
            BoardCapabilities changed = core.model->boardCapabilities();
            changed.attenuator.present = !changed.attenuator.present;
            core.server->setupDescription()->setBoardCapabilities(changed);
            QString general;
            QTRY_VERIFY(!(general = setupCategoryOnWire(
                *core.app, "general", SessionMessageKind::Delta)).isEmpty());
            QCOMPARE(QJsonDocument::fromJson(general.toUtf8()).object()
                         .value("version").toInt(), qMin(expected, 3));
        };
        check(0, kSessionProtocolMinor, 0);
        check(1, kSessionProtocolMinor, 1);
        check(2, kSessionProtocolMinor, 2);
        check(3, kSessionProtocolMinor, 3);
        check(4, kSessionProtocolMinor, 4);
        check(5, kSessionProtocolMinor, 5);
        check(6, kSessionProtocolMinor, 6);
        check(7, kSessionProtocolMinor, 7);
        check(8, kSessionProtocolMinor, 8);
        check(9, kSessionProtocolMinor, 9);
        check(10, kSessionProtocolMinor, 10);
        check(11, kSessionProtocolMinor, 10);
        check(2, quint16(kRadioIdentitySessionProtocolMinor - 1), 0);
        check(3, quint16(kRadioIdentitySessionProtocolMinor - 1), 0);
    }

    void v4MetadataDoesNotLeakIntoOlderProjectedControls()
    {
        SetupDescriptionService service;
        const auto controlsOf = [](const QJsonObject& category) {
            QJsonArray controls;
            for (const QJsonValue& page : category.value("pages").toArray()) {
                for (const QJsonValue& section : page.toObject().value("sections").toArray()) {
                    for (const QJsonValue& control : section.toObject().value("controls").toArray()) {
                        controls.append(control);
                    }
                }
            }
            return controls;
        };
        for (int version = 1; version <= 10; ++version) {
            const QJsonObject display = QJsonDocument::fromJson(
                SetupDescriptionService::fitCategoryForVersion(service.display(), version).toUtf8()).object();
            const QJsonObject appearance = QJsonDocument::fromJson(
                SetupDescriptionService::fitCategoryForVersion(service.appearance(), version).toUtf8()).object();
            QCOMPARE(display.value("version"),
                     QJsonValue(version >= 10 ? 10 : version >= 9 ? 9 : version >= 8 ? 8 : qMin(version, 4)));
            QCOMPARE(appearance.value("version"), QJsonValue(version >= 7 ? 7 : qMin(version, 4)));
            QCOMPARE(controlsOf(display).size(), version < 4 ? 11 : version < 8 ? 14 : version < 9 ? 21 : version < 10 ? 29 : 33);
            QCOMPARE(display.value("pages").toArray().size(), version < 8 ? 3 : 4);
            QCOMPARE(controlsOf(appearance).size(), version < 7 ? 10 : 13);
            QCOMPARE(appearance.value("pages").toArray().size(), version < 7 ? 1 : 2);
            for (const QJsonObject& category : {display, appearance}) {
                for (const QJsonValue& raw : controlsOf(category)) {
                    const QJsonObject control = raw.toObject();
                    const QString kind = control.value("kind").toString();
                    if (version < 4) {
                        QVERIFY2(!control.contains("default"), qPrintable(control.value("id").toString()));
                        if (kind == QLatin1String("decimal")) {
                            QVERIFY(!control.contains("decimals"));
                        }
                    } else if (kind == QLatin1String("colour")) {
                        const QString rgba = control.value("default").toString();
                        QVERIFY(QRegularExpression(QStringLiteral("^#[0-9A-F]{8}$")).match(rgba).hasMatch());
                    } else if (kind == QLatin1String("toggle")) {
                        QVERIFY(control.value("default").isBool());
                    } else {
                        QVERIFY(control.value("default").isDouble());
                        if (kind == QLatin1String("decimal")) {
                            QCOMPARE(control.value("decimals"), QJsonValue(2));
                        } else {
                            const double ordinal = control.value("default").toDouble();
                            QCOMPARE(std::floor(ordinal), ordinal);
                        }
                    }
                }
            }
        }
    }

    void displayV10PublishesFourClosedWaterfallOverlays()
    {
        SetupDescriptionService service;
        const QJsonObject display = service.category(QStringLiteral("display"));
        QCOMPARE(display.value("version"), QJsonValue(10));
        const QJsonArray sections = display.value("pages").toArray().at(1).toObject()
            .value("sections").toArray();
        QCOMPARE(sections.size(), 2);
        QCOMPARE(sections.at(1).toObject().value("title"), QJsonValue("Overlays"));
        const QJsonArray overlays = sections.at(1).toObject().value("controls").toArray();
        QCOMPARE(overlays.size(), 4);
        const QStringList ids{"display.waterfallDefaults.showRxFilter",
                              "display.waterfallDefaults.showTxFilter",
                              "display.waterfallDefaults.showRxZeroLine",
                              "display.waterfallDefaults.showTxZeroLine"};
        const QStringList keys{"DisplayShowRxFilterOnWaterfall",
                               "DisplayShowTxFilterOnRxWaterfall",
                               "DisplayShowRxZeroLine", "DisplayShowTxZeroLine"};
        for (int i = 0; i < overlays.size(); ++i) {
            const QJsonObject control = overlays.at(i).toObject();
            QCOMPARE(control.value("id"), QJsonValue(ids.at(i)));
            QCOMPARE(control.value("binding"), QJsonValue(QJsonObject{{"phone", keys.at(i)}}));
            QCOMPARE(control.value("kind"), QJsonValue("toggle"));
            QCOMPARE(control.value("applies"), QJsonValue("live"));
            QCOMPARE(control.value("default"), QJsonValue(i == 1));
            QCOMPARE(control.value("requiresDescriptionVersion"), QJsonValue(10));
            QVERIFY(SetupDescriptionService::validateDisplayPhoneBinding(control));
            const auto rejects = [&control](const QString& field, const QJsonValue& value) {
                QJsonObject changed = control;
                changed.insert(field, value);
                QVERIFY(!SetupDescriptionService::validateDisplayPhoneBinding(changed));
            };
            rejects("binding", QJsonObject{{"setting", keys.at(i)}});
            rejects("binding", QJsonObject{{"phone", "Unknown"}});
            rejects("default", i != 1);
            rejects("requiresDescriptionVersion", 9);
            rejects("kind", "integer");
            rejects("applies", "subscription");
            rejects("min", 0);
        }
        const QJsonObject v9 = QJsonDocument::fromJson(
            SetupDescriptionService::fitCategoryForVersion(service.display(), 9).toUtf8()).object();
        QCOMPARE(v9.value("version"), QJsonValue(9));
        QCOMPARE(v9.value("pages").toArray().at(1).toObject()
                     .value("sections").toArray().size(), 1);
        const auto countControls = [](const QJsonObject& category) {
            int count = 0;
            for (const QJsonValue& page : category.value("pages").toArray()) {
                for (const QJsonValue& section : page.toObject().value("sections").toArray()) {
                    count += section.toObject().value("controls").toArray().size();
                }
            }
            return count;
        };
        QCOMPARE(countControls(v9), 29);
        QCOMPARE(countControls(display), 33);
        QJsonObject prior = display;
        prior.insert("version", 9);
        QJsonArray priorPages = prior.value("pages").toArray();
        QJsonObject priorWaterfall = priorPages.at(1).toObject();
        QJsonArray priorSections = priorWaterfall.value("sections").toArray();
        priorSections.removeAt(1);
        priorWaterfall.insert("sections", priorSections);
        priorPages[1] = priorWaterfall;
        prior.insert("pages", priorPages);
        QCOMPARE(v9, prior);
    }

    void displayV9PublishesClosedLocalRendererControls()
    {
        SetupDescriptionService service;
        const QJsonObject display = QJsonDocument::fromJson(
            SetupDescriptionService::fitCategoryForVersion(service.display(), 9).toUtf8()).object();
        QCOMPARE(display.value("version"), QJsonValue(9));
        const QJsonArray pages = display.value("pages").toArray();
        QCOMPARE(pages.size(), 4);
        const QJsonArray spectrum = pages.at(0).toObject().value("sections").toArray()
            .at(1).toObject().value("controls").toArray();
        const QJsonArray waterfall = pages.at(1).toObject().value("sections").toArray()
            .at(0).toObject().value("controls").toArray();
        QCOMPARE(spectrum.size(), 10);
        QCOMPARE(waterfall.size(), 6);
        const QStringList ids{
            "display.spectrumDefaults.panFill", "display.spectrumDefaults.fillAlpha",
            "display.spectrumDefaults.gradient", "display.spectrumDefaults.peakHold",
            "display.spectrumDefaults.peakDelay", "display.waterfallDefaults.updatePeriod",
            "display.waterfallDefaults.stopOnTx", "display.waterfallDefaults.opacity"};
        const QStringList keys{
            "DisplayPanFill", "DisplayFftFillAlpha", "DisplayGradientEnabled",
            "DisplayPeakHoldEnabled", "DisplayPeakHoldResetMs", "DisplayWfUpdatePeriodMs",
            "WaterfallStopOnTx", "DisplayWfOpacity"};
        QJsonArray controls;
        for (int i = 5; i < spectrum.size(); ++i) { controls.append(spectrum.at(i)); }
        for (int i = 3; i < waterfall.size(); ++i) { controls.append(waterfall.at(i)); }
        QCOMPARE(controls.size(), ids.size());
        for (int i = 0; i < controls.size(); ++i) {
            const QJsonObject control = controls.at(i).toObject();
            QCOMPARE(control.value("id"), QJsonValue(ids.at(i)));
            QCOMPARE(control.value("binding"), QJsonValue(QJsonObject{{"phone", keys.at(i)}}));
            QCOMPARE(control.value("requiresDescriptionVersion"), QJsonValue(9));
            QVERIFY(SetupDescriptionService::validateDisplayPhoneBinding(control));
            const auto rejects = [&control, &keys, i](const QString& field, const QJsonValue& value) {
                QJsonObject changed = control;
                changed.insert(field, value);
                QVERIFY(!SetupDescriptionService::validateDisplayPhoneBinding(changed));
            };
            rejects(QStringLiteral("binding"), QJsonObject{{"setting", keys.at(i)}});
            rejects(QStringLiteral("binding"), QJsonObject{{"phone", "Unknown"}});
            rejects(QStringLiteral("default"), -1);
            rejects(QStringLiteral("requiresDescriptionVersion"), 8);
            if (control.contains("min")) {
                rejects(QStringLiteral("min"), -1);
                rejects(QStringLiteral("max"), 10001);
                rejects(QStringLiteral("step"), 2);
            } else {
                rejects(QStringLiteral("min"), 0);
            }
        }
        QCOMPARE(controls.at(1).toObject().value("step"), QJsonValue(1));
        QCOMPARE(controls.at(5).toObject().value("step"), QJsonValue(1));
        for (int version = 1; version <= 8; ++version) {
            const QJsonObject older = QJsonDocument::fromJson(
                SetupDescriptionService::fitCategoryForVersion(service.display(), version).toUtf8()).object();
            QCOMPARE(older.value("version"), QJsonValue(version >= 8 ? 8 : qMin(version, 4)));
            QCOMPARE(older.value("pages").toArray().size(), version >= 8 ? 4 : 3);
            const QJsonArray oldSpectrum = older.value("pages").toArray().at(0).toObject()
                .value("sections").toArray().at(1).toObject().value("controls").toArray();
            QCOMPARE(oldSpectrum.size(), version >= 8 ? 5 : 1);
            if (version >= 8) {
                const QJsonArray oldWaterfall = older.value("pages").toArray().at(1).toObject()
                    .value("sections").toArray().at(0).toObject().value("controls").toArray();
                QCOMPARE(oldWaterfall.size(), 3);
            }
        }
    }

    void displayV8PublishesSevenPhoneOwnedRxControls()
    {
        SetupDescriptionService service;
        const QJsonObject source = QJsonDocument::fromJson(
            SetupDescriptionService::fitCategoryForVersion(service.display(), 8).toUtf8()).object();
        QCOMPARE(source.value("version"), QJsonValue(8));
        const QJsonArray pages = source.value("pages").toArray();
        QCOMPARE(pages.size(), 4);
        const QJsonArray spectrum = pages.at(0).toObject().value("sections").toArray()
            .at(1).toObject().value("controls").toArray();
        QCOMPARE(spectrum.size(), 5);
        const QJsonArray waterfall = pages.at(1).toObject().value("sections").toArray()
            .at(0).toObject().value("controls").toArray();
        QCOMPARE(waterfall.size(), 3);
        QJsonArray newControls;
        for (int i = 1; i < spectrum.size(); ++i) { newControls.append(spectrum.at(i)); }
        for (const QJsonValue& raw : waterfall) { newControls.append(raw); }
        const QStringList expectedIds{
            QStringLiteral("display.spectrumDefaults.detector"),
            QStringLiteral("display.spectrumDefaults.averaging"),
            QStringLiteral("display.spectrumDefaults.averageTime"),
            QStringLiteral("display.spectrumDefaults.decimation"),
            QStringLiteral("display.waterfallDefaults.detector"),
            QStringLiteral("display.waterfallDefaults.averaging"),
            QStringLiteral("display.waterfallDefaults.averageTime")};
        QCOMPARE(newControls.size(), expectedIds.size());
        int index = 0;
        for (const QJsonValue& raw : newControls) {
            const QJsonObject control = raw.toObject();
            QCOMPARE(control.value("id"), QJsonValue(expectedIds.at(index++)));
            QCOMPARE(control.value("requiresDescriptionVersion"), QJsonValue(8));
            QCOMPARE(control.value("binding").toObject().size(), 1);
            QVERIFY(control.value("binding").toObject().contains("phone"));
            QVERIFY(SetupDescriptionService::validateDisplayPhoneBinding(control));
            const auto rejects = [&control](const QString& field, const QJsonValue& value) {
                QJsonObject changed = control;
                changed.insert(field, value);
                QVERIFY(!SetupDescriptionService::validateDisplayPhoneBinding(changed));
            };
            rejects(QStringLiteral("binding"), QJsonObject{{"setting", "DisplaySpectrumDetector"}});
            rejects(QStringLiteral("binding"), QJsonObject{{"phone", "UnknownDisplayControl"}});
            rejects(QStringLiteral("kind"), QStringLiteral("toggle"));
            rejects(QStringLiteral("applies"), QStringLiteral("live"));
            rejects(QStringLiteral("requiresDescriptionVersion"), 7);
            rejects(QStringLiteral("default"), -1);
            rejects(QStringLiteral("gate"), QJsonObject{{"capability", "txDisplayVersion"}, {"min", 1}});
            if (control.value("kind") == QJsonValue("choice")) {
                rejects(QStringLiteral("options"), QJsonArray{});
                rejects(QStringLiteral("min"), 0);
                QJsonArray reordered = control.value("options").toArray();
                const QJsonValue first = reordered.at(0);
                reordered[0] = reordered.at(1);
                reordered[1] = first;
                rejects(QStringLiteral("options"), reordered);
            } else {
                rejects(QStringLiteral("min"), -1);
                rejects(QStringLiteral("max"), 10000);
                rejects(QStringLiteral("step"), 2);
                rejects(QStringLiteral("unit"), QStringLiteral("W"));
                rejects(QStringLiteral("options"), QJsonArray{});
            }
        }
        QCOMPARE(pages.at(1).toObject().value("id"), QJsonValue("display.waterfallDefaults"));
        QCOMPARE(pages.at(1).toObject().value("where"), QJsonValue("phone"));
        for (int version = 1; version <= 7; ++version) {
            const QJsonObject older = QJsonDocument::fromJson(
                SetupDescriptionService::fitCategoryForVersion(service.display(), version).toUtf8()).object();
            QCOMPARE(older.value("pages").toArray().size(), 3);
            QCOMPARE(older.value("pages").toArray().at(0).toObject().value("sections")
                         .toArray().at(1).toObject().value("controls").toArray().size(), 1);
        }
    }

    void displayDescriptionKeepsVersionFourFftOptions()
    {
        SetupDescriptionService service;
        const QJsonObject display = service.category(QStringLiteral("display"));
        QCOMPARE(display.value("version"), QJsonValue(10));
        QCOMPARE(display.value("pages").toArray().size(), 4);
        const QString source = service.display();
        for (int version = 1; version <= 10; ++version) {
            const QJsonObject fitted = QJsonDocument::fromJson(
                SetupDescriptionService::fitCategoryForVersion(source, version).toUtf8()).object();
            QCOMPARE(fitted.value("version"),
                     QJsonValue(version >= 10 ? 10 : version >= 9 ? 9 : version >= 8 ? 8 : qMin(version, 4)));
            int count = 0;
            int optionSliders = 0;
            for (const QJsonValue& page : fitted.value("pages").toArray()) {
                for (const QJsonValue& section : page.toObject().value("sections").toArray()) {
                    for (const QJsonValue& raw : section.toObject().value("controls").toArray()) {
                        const QJsonObject control = raw.toObject();
                        ++count;
                        if (control.value("kind") == QJsonValue("slider")
                            && control.contains("options")) {
                            ++optionSliders;
                            QCOMPARE(control.value("kind"), QJsonValue("slider"));
                            QVERIFY(!control.contains("min"));
                            QVERIFY(!control.contains("max"));
                            QVERIFY(!control.contains("step"));
                        }
                    }
                }
            }
            QCOMPARE(count, version >= 10 ? 33 : version >= 9 ? 29 : version >= 8 ? 21 : version >= 4 ? 14 : 11);
            QCOMPARE(optionSliders, version >= 4 ? 2 : 0);
        }
    }

    void displayV4RejectsMalformedOptionsAndNormalizeDependency()
    {
        SetupDescriptionService service;
        const QJsonArray pages = service.category(QStringLiteral("display")).value("pages").toArray();
        const QJsonObject rxFft = pages.at(0).toObject().value("sections").toArray().at(0)
            .toObject().value("controls").toArray().at(0).toObject();
        QVERIFY(SetupDescriptionService::validateDisplaySettingBinding(rxFft));
        const QJsonArray original = rxFft.value("options").toArray();
        const auto rejects = [&rxFft](const QJsonArray& options) {
            QJsonObject changed = rxFft;
            changed.insert("options", options);
            QVERIFY(!SetupDescriptionService::validateDisplaySettingBinding(changed));
        };
        rejects({});
        QJsonArray duplicate = original;
        duplicate[1] = duplicate[0];
        rejects(duplicate);
        QJsonArray reversed = original;
        reversed[0] = original[1];
        reversed[1] = original[0];
        rejects(reversed);
        QJsonArray unexpected = original;
        unexpected[6] = QJsonObject{{"value", 524288}, {"label", "524288"}};
        rejects(unexpected);
        QJsonArray nonfinite = original;
        nonfinite[0] = QJsonObject{{"value", QJsonValue()}, {"label", "4096"}};
        rejects(nonfinite);
        QJsonObject wrongDefault = rxFft;
        wrongDefault.insert("default", 1024);
        QVERIFY(!SetupDescriptionService::validateDisplaySettingBinding(wrongDefault));
        QJsonObject numericRange = rxFft;
        numericRange.insert("min", 4096);
        QVERIFY(!SetupDescriptionService::validateDisplaySettingBinding(numericRange));

        const QJsonObject normalize = pages.at(3).toObject().value("sections").toArray().at(1)
            .toObject().value("controls").toArray().at(3).toObject();
        QVERIFY(SetupDescriptionService::validateDisplaySettingBinding(normalize));
        for (const QJsonObject& dependency : {
                 QJsonObject{},
                 QJsonObject{{"setting", "DisplayTxWfDetector"},
                             {"oneOf", QJsonArray{"2", "3", "4"}}},
                 QJsonObject{{"setting", "DisplayTxPanDetector"},
                             {"oneOf", QJsonArray{"2", "4"}}},
                 QJsonObject{{"setting", "DisplayTxPanDetector"},
                             {"oneOf", QJsonArray{"2", "3", "4"}}, {"extra", true}}}) {
            QJsonObject changed = normalize;
            changed.insert("enabledWhen", dependency);
            QVERIFY(!SetupDescriptionService::validateDisplaySettingBinding(changed));
        }
    }

    void tnfTableRejectsMalformedSourcesAndActions()
    {
        SetupDescriptionService service;
        QJsonObject table;
        QJsonObject add;
        for (const QJsonValue& page : service.category(QStringLiteral("dsp"))
                 .value("pages").toArray()) {
            for (const QJsonValue& section : page.toObject().value("sections").toArray()) {
                for (const QJsonValue& raw : section.toObject().value("controls").toArray()) {
                    const QJsonObject control = raw.toObject();
                    if (control.value("id") == QJsonValue("dsp.tnf.list")) { table = control; }
                    if (control.value("id") == QJsonValue("dsp.tnf.add")) { add = control; }
                }
            }
        }
        QVERIFY(!table.isEmpty());
        QVERIFY(!add.isEmpty());
        QString error;
        QVERIFY2(SetupDescriptionService::validateTnfTable(table, &error), qPrintable(error));
        QVERIFY2(SetupDescriptionService::validateCommandBinding(add, &error), qPrintable(error));
        const auto rejects = [&](const QJsonObject& bad) {
            QVERIFY(!SetupDescriptionService::validateTnfTable(bad, &error));
        };
        QJsonObject bad = table;
        QJsonObject binding = bad.value("binding").toObject();
        QJsonObject source = binding.value("table").toObject();
        source.insert("format", "json-path");
        binding.insert("table", source);
        bad.insert("binding", binding);
        rejects(bad);
        bad = table;
        QJsonArray columns = bad.value("columns").toArray();
        QJsonObject column = columns.at(0).toObject();
        column.insert("min", 0);
        columns[0] = column;
        bad.insert("columns", columns);
        rejects(bad);
        bad = table;
        QJsonArray actions = bad.value("rowActions").toArray();
        QJsonObject action = actions.at(0).toObject();
        QJsonObject command = action.value("command").toObject();
        QJsonObject args = command.value("arguments").toObject();
        args.insert("id", QJsonObject{{"$row", "id"}, {"fallback", 0}});
        command.insert("arguments", args);
        action.insert("command", command);
        actions[0] = action;
        bad.insert("rowActions", actions);
        rejects(bad);
        bad = table;
        actions = bad.value("rowActions").toArray();
        action = actions.at(0).toObject();
        command = action.value("command").toObject();
        command.insert("verb", "notch.add");
        action.insert("command", command);
        actions[0] = action;
        bad.insert("rowActions", actions);
        rejects(bad);
        bad = table;
        QJsonObject gate = bad.value("gate").toObject();
        gate.insert("min", 0);
        bad.insert("gate", gate);
        rejects(bad);
        bad = add;
        binding = bad.value("binding").toObject();
        command = binding.value("command").toObject();
        args = command.value("arguments").toObject();
        args.insert("sliceId", QJsonObject{{"$selectedOwnedSliceId", true}, {"fallback", 0}});
        command.insert("arguments", args);
        binding.insert("command", command);
        bad.insert("binding", binding);
        QVERIFY(!SetupDescriptionService::validateCommandBinding(bad, &error));
        args.insert("sliceId", QJsonObject{{"$selectedOwnedSliceId", false}});
        command.insert("arguments", args);
        binding.insert("command", command);
        bad.insert("binding", binding);
        QVERIFY(!SetupDescriptionService::validateCommandBinding(bad, &error));
    }

    void tnfDescribedVerbsExecuteThroughCoreSession()
    {
        WireCore core;
        QVERIFY(core.connect({{QByteArrayLiteral("setupDescription"), 2}}));
        QCOMPARE(setupCapabilityOnWire(*core.app), 2);
        const QJsonObject dsp = QJsonDocument::fromJson(setupCategoryOnWire(
            *core.app, "dsp", SessionMessageKind::ObjectCreate).toUtf8()).object();
        QJsonObject table;
        QJsonObject add;
        for (const QJsonValue& page : dsp.value("pages").toArray()) {
            for (const QJsonValue& section : page.toObject().value("sections").toArray()) {
                for (const QJsonValue& raw : section.toObject().value("controls").toArray()) {
                    const QJsonObject control = raw.toObject();
                    if (control.value("id") == QJsonValue("dsp.tnf.list")) { table = control; }
                    if (control.value("id") == QJsonValue("dsp.tnf.add")) { add = control; }
                }
            }
        }
        QVERIFY(!table.isEmpty());
        QVERIFY(!add.isEmpty());
        quint32 nextId = 100;
        const auto invoke = [&](const QByteArray& verb, const QList<MirrorUpdate>& args) {
            const quint32 id = ++nextId;
            core.app->sendText(SessionMessages::encode(
                SessionMessages::commandInvoke(verb, id, args)));
            SessionMessage result;
            result.reason = QStringLiteral("no command.result arrived");
            (void)QTest::qWaitFor([&] {
                for (const QByteArray& wire : core.app->received()) {
                    SessionMessage message;
                    if (SessionMessages::decode(wire, &message)
                        && message.kind == SessionMessageKind::CommandResult
                        && message.commandId == id) {
                        result = message;
                        return true;
                    }
                }
                return false;
            }, 3000);
            return result;
        };
        const auto i64 = [](const QByteArray& name, int value) {
            return MirrorUpdate{0, name, MirrorWireKind::Int64, qlonglong(value)};
        };
        const QByteArray addVerb = add.value("binding").toObject()
            .value("command").toObject().value("verb").toString().toUtf8();
        const QString session = QStringLiteral("phone-A");
        const quint64 epoch = 7;
        const QList<MirrorUpdate> addArgs = materializeTnfAdd(
            add, 0, 0, true, session, session, epoch, epoch);
        QCOMPARE(addArgs.size(), 1);
        QVERIFY(materializeTnfAdd(add, 0, 1, true, session, session, epoch, epoch).isEmpty());
        QVERIFY(materializeTnfAdd(add, 0, 0, false, session, session, epoch, epoch).isEmpty());
        QVERIFY(materializeTnfAdd(add, 0, 0, true, session, QStringLiteral("phone-B"),
                                 epoch, epoch).isEmpty());
        QVERIFY(materializeTnfAdd(add, 0, 0, true, session, session,
                                 epoch, epoch + 1).isEmpty());
        SessionMessage result = invoke(addVerb, addArgs);
        QVERIFY2(result.accepted, qPrintable(result.reason));
        QCOMPARE(core.model->notchModel()->notches().size(), 1);
        const int rowId = core.model->notchModel()->notches().first().id;
        const QJsonArray rowActions = table.value("rowActions").toArray();
        const QByteArray moveVerb = rowActions.at(0).toObject().value("command")
            .toObject().value("verb").toString().toUtf8();
        const QByteArray activeVerb = rowActions.at(1).toObject().value("command")
            .toObject().value("verb").toString().toUtf8();
        const QByteArray deleteVerb = rowActions.at(2).toObject().value("command")
            .toObject().value("verb").toString().toUtf8();
        const quint32 revision = core.model->notchModel()->revision();
        const QString list = core.model->notchModel()->listJson();
        const QJsonObject edits{{"centreHz", 14075000.0}, {"widthHz", 300.0}};
        const QJsonObject move = rowActions.at(0).toObject();
        const QList<MirrorUpdate> moveArgs = materializeTnfRowAction(
            move, list, revision, revision, rowId, edits, session, session, epoch, epoch);
        QCOMPARE(moveArgs.size(), 3);
        QVERIFY(materializeTnfRowAction(move, list, revision + 1, revision, rowId,
                edits, session, session, epoch, epoch).isEmpty());
        QVERIFY(materializeTnfRowAction(move, list, revision, revision, rowId,
                edits, session, QStringLiteral("phone-B"), epoch, epoch).isEmpty());
        QVERIFY(materializeTnfRowAction(move, list, revision, revision, rowId,
                edits, session, session, epoch, epoch + 1).isEmpty());
        QVERIFY(materializeTnfRowAction(move, list, revision, revision, rowId + 500,
                edits, session, session, epoch, epoch).isEmpty());
        QVERIFY(materializeTnfRowAction(move,
                QStringLiteral("[{\"id\":1,\"centreHz\":14074000,\"widthHz\":200,\"active\":true},"
                               "{\"id\":1,\"centreHz\":14074000,\"widthHz\":200,\"active\":true}]"),
                revision, revision, 1, edits, session, session, epoch, epoch).isEmpty());
        QVERIFY(materializeTnfRowAction(move, QStringLiteral("not-an-array"),
                revision, revision, rowId, edits, session, session, epoch, epoch).isEmpty());
        QVERIFY(materializeTnfRowAction(move,
                QStringLiteral("[{\"id\":1,\"centreHz\":\"14074000\",\"widthHz\":200,\"active\":true}]"),
                revision, revision, 1, edits, session, session, epoch, epoch).isEmpty());
        QVERIFY(materializeTnfRowAction(move, list, revision, revision, rowId,
                QJsonObject{{"centreHz", QStringLiteral("14075000")}, {"widthHz", 300.0}},
                session, session, epoch, epoch).isEmpty());
        result = invoke(moveVerb, moveArgs);
        QVERIFY2(result.accepted, qPrintable(result.reason));
        QCOMPARE(core.model->notchModel()->notchById(rowId)->centerHz, 14075000.0);
        QCOMPARE(core.model->notchModel()->notchById(rowId)->widthHz, 300.0);
        const QJsonObject active = rowActions.at(1).toObject();
        const quint32 movedRevision = core.model->notchModel()->revision();
        result = invoke(activeVerb, materializeTnfRowAction(active,
            core.model->notchModel()->listJson(), movedRevision, movedRevision, rowId,
            QJsonObject{{"active", false}}, session, session, epoch, epoch));
        QVERIFY2(result.accepted, qPrintable(result.reason));
        QVERIFY(!core.model->notchModel()->notchById(rowId)->active);
        const QJsonObject remove = rowActions.at(2).toObject();
        const quint32 activeRevision = core.model->notchModel()->revision();
        result = invoke(deleteVerb, materializeTnfRowAction(remove,
            core.model->notchModel()->listJson(), activeRevision, activeRevision, rowId,
            {}, session, session, epoch, epoch));
        QVERIFY2(result.accepted, qPrintable(result.reason));
        QVERIFY(core.model->notchModel()->notchById(rowId) == nullptr);
        // The client cancels the old row; a stale direct command still gets
        // the Core's missing-row refusal.
        QVERIFY(materializeTnfRowAction(move, core.model->notchModel()->listJson(),
            core.model->notchModel()->revision(), revision, rowId, edits,
            session, session, epoch, epoch).isEmpty());
        result = invoke(moveVerb, {i64("id", rowId),
            MirrorUpdate{0, "centreHz", MirrorWireKind::Float64, 14076000.0},
            MirrorUpdate{0, "widthHz", MirrorWireKind::Float64, 200.0}});
        QVERIFY(!result.accepted);
        QCOMPARE(result.reason, QStringLiteral("That notch is no longer on this Core."));
        result = invoke(addVerb, {i64("sliceId", 999)});
        QVERIFY(!result.accepted);
        QVERIFY(core.model->notchModel()->notches().isEmpty());
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
