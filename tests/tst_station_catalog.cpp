// no-port-check: NereusSDR-original.
// =================================================================
// tests/tst_station_catalog.cpp  (NereusSDR)
// =================================================================
//
// iPhone app plan, Task 19 (R-IOS-06, R-IOS-27, D40): the Core's catalogue.
//
//   - The G2 and HL2 catalogue fixtures (tests/data/link/v1/sessions/
//     catalog-*.json) hold the desktop's own values for each radio, read
//     from the places the desktop reads them (ControlRanges.h,
//     BoardCapabilities, SkuUiProfile, SampleRateCatalog, paMaxWattsFor),
//     and differ exactly where the radios differ.
//   - A change to a preset, the step list or the band plan data moves the
//     revision once, and the new catalogue reaches a connected client.
//   - The catalogue stays within 256 KiB for every radio.
//
//   cmake --build build --target tst_station_catalog
//   QT_QPA_PLATFORM=offscreen ctest --test-dir build -R '^tst_station_catalog$' \
//       --output-on-failure
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-24: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
//   2026-09-25: each band-plan segment's lowestClass (R-IOS-27,
//               R-IOS-11). J.J. Boyd (KG4VCF), with AI-assisted
//               implementation via Anthropic Claude Code.
//   2026-09-25: the `bands` key is the desktop's BAND grid, in its order
//               (R-IOS-27, R-IOS-06); bandSelectVersion now follows
//               transmitSettingsVersion. J.J. Boyd (KG4VCF), with
//               AI-assisted implementation via Anthropic Claude Code.
//   2026-09-26  J.J. Boyd / KG4VCF  Parity Task 19 (R-IOS-25):
//                                    recordStreamVersion and the record
//                                    streams. AI-assisted via Anthropic
//                                    Claude Code.
// =================================================================

#include <QtTest>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <memory>

#include "core/AppSettings.h"
#include "core/BoardCapabilities.h"
#include "core/ConnectionState.h"
#include "core/ControlRanges.h"
#include "core/HpsdrModel.h"
#include "core/SampleRateCatalog.h"
#include "core/SkuUiProfile.h"
#include "core/session/SessionMessages.h"
#include "core/session/StationCapabilities.h"
#include "core/session/StationCatalog.h"
#include "core/session/StationServer.h"
#include "core/spectrum/WaterfallPalettes.h"
#include "models/BandGrid.h"
#include "models/BandPlanManager.h"
#include "models/FilterPresetStore.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include "fakes/LoopbackTransport.h"
#include "fakes/UpgradedCoreToken.h"

using namespace NereusSDR;
using NereusSDR::Test::LoopbackTransport;

namespace {

const QSet<QString> kTopLevelKeys{
    QStringLiteral("modes"),       QStringLiteral("filterPresets"), QStringLiteral("tuneSteps"),
    QStringLiteral("agc"),         QStringLiteral("receive"),       QStringLiteral("meters"),
    QStringLiteral("board"),       QStringLiteral("bands"),
    QStringLiteral("bandPlans"),   QStringLiteral("palettes"),      QStringLiteral("sliceColours"),
    QStringLiteral("tools"),       QStringLiteral("radioItems"),    QStringLiteral("audio"),
};

// The catalogue a fixture's `catalog` object.create carries.
QJsonObject catalogInFixture(const QString& file)
{
    QFile f(QStringLiteral(NEREUS_LINK_DATA_DIR "/sessions/") + file);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonArray steps =
        QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("steps")).toArray();
    for (const QJsonValue& step : steps) {
        const QJsonObject message = step.toObject().value(QStringLiteral("message")).toObject();
        if (message.value(QStringLiteral("type")).toString() != QStringLiteral("object.create")
            || message.value(QStringLiteral("key")).toString() != QStringLiteral("catalog")) {
            continue;
        }
        for (const QJsonValue& p : message.value(QStringLiteral("properties")).toArray()) {
            const QJsonObject property = p.toObject();
            if (property.value(QStringLiteral("name")).toString() == QStringLiteral("json")) {
                return QJsonDocument::fromJson(
                           property.value(QStringLiteral("value")).toString().toUtf8())
                    .object();
            }
        }
    }
    return {};
}

QJsonObject range(double min, double max, double step)
{
    return QJsonObject{{QStringLiteral("min"), min},
                       {QStringLiteral("max"), max},
                       {QStringLiteral("step"), step}};
}

QJsonArray strings(const QStringList& list)
{
    QJsonArray out;
    for (const QString& s : list) {
        out.append(s);
    }
    return out;
}

// Inputs for one radio as the desktop would describe it: the board's
// capabilities, the Core's (default) presets, the step list and the plans.
StationCatalog::Inputs inputsFor(HPSDRModel model, ProtocolVersion protocol,
                                 const BandPlanManager& plans)
{
    StationCatalog::Inputs inputs;
    inputs.model = model;
    inputs.board = BoardCapsTable::forModel(model);
    inputs.protocol = protocol;
    for (int id = 0; id <= static_cast<int>(DSPMode::RADE_L); ++id) {
        const auto mode = static_cast<DSPMode>(id);
        inputs.filterPresets.append({mode, FilterPresetStore().presetsForMode(mode)});
    }
    for (int i = 0; i < kStageOneStepLadderSize; ++i) {
        inputs.tuneStepsHz.append(kStageOneStepLadder[i]);
    }
    for (const BandPlanManager::PlanData& plan : plans.plans()) {
        inputs.bandPlans.append(StationCatalog::BandPlan{plan.id, plan.name, plan.segments});
    }
    inputs.defaultBandPlanName = QString::fromLatin1(BandPlanManager::kDefaultPlanName);
    return inputs;
}

// The desktop's values for `model`, from the headers its widgets read.
void checkDesktopValues(const QJsonObject& catalog, HPSDRModel model, ProtocolVersion protocol)
{
    using namespace ControlRanges;
    const BoardCapabilities& caps = BoardCapsTable::forModel(model);

    const QStringList keys = catalog.keys();
    QCOMPARE(QSet<QString>(keys.cbegin(), keys.cend()), kTopLevelKeys);

    // Bands: the desktop's per-pan BAND grid (kBandGrid), in its order, each
    // with its Band value and its button's text.
    const QJsonArray bands = catalog.value(QStringLiteral("bands")).toArray();
    QCOMPARE(bands.size(), kBandGridCount);
    for (int i = 0; i < kBandGridCount; ++i) {
        const QJsonObject band = bands.at(i).toObject();
        QCOMPARE(band.keys(), (QStringList{QStringLiteral("id"), QStringLiteral("label")}));
        QCOMPARE(band.value(QStringLiteral("id")).toInt(), static_cast<int>(kBandGrid[i].band));
        QCOMPARE(band.value(QStringLiteral("label")).toString(),
                 QString::fromLatin1(kBandGrid[i].label));
    }

    // Modes: DSPMode 0 to 13 with SliceModel's names.
    const QJsonArray modes = catalog.value(QStringLiteral("modes")).toArray();
    QCOMPARE(modes.size(), 14);
    for (int id = 0; id < 14; ++id) {
        const QJsonObject mode = modes.at(id).toObject();
        QCOMPARE(mode.value(QStringLiteral("id")).toInt(), id);
        QCOMPARE(mode.value(QStringLiteral("label")).toString(),
                 SliceModel::modeName(static_cast<DSPMode>(id)));
    }
    QCOMPARE(modes.at(0).toObject().value(QStringLiteral("sideband")).toString(),
             QStringLiteral("lower"));
    QCOMPARE(modes.at(1).toObject().value(QStringLiteral("sideband")).toString(),
             QStringLiteral("upper"));
    QCOMPARE(modes.at(6).toObject().value(QStringLiteral("sideband")).toString(),
             QStringLiteral("both"));

    // Filter presets: the store's, per mode label, F1 onwards.
    const QJsonObject presets = catalog.value(QStringLiteral("filterPresets")).toObject();
    QCOMPARE(presets.size(), 14);
    const QJsonArray usb = presets.value(QStringLiteral("USB")).toArray();
    const QList<FilterPreset> usbDefaults = FilterPresetStore().presetsForMode(DSPMode::USB);
    QCOMPARE(usb.size(), usbDefaults.size());
    for (int slot = 0; slot < usb.size(); ++slot) {
        const QJsonObject entry = usb.at(slot).toObject();
        QCOMPARE(entry.value(QStringLiteral("slot")).toInt(), slot);
        QCOMPARE(entry.value(QStringLiteral("label")).toString(), usbDefaults.at(slot).name);
        QCOMPARE(entry.value(QStringLiteral("lowHz")).toInt(), usbDefaults.at(slot).low);
        QCOMPARE(entry.value(QStringLiteral("highHz")).toInt(), usbDefaults.at(slot).high);
    }
    QCOMPARE(presets.value(QStringLiteral("RADE-U")).toArray().size(), 1);

    // Tune steps: SliceModel's list.
    const QJsonArray steps = catalog.value(QStringLiteral("tuneSteps")).toArray();
    QCOMPARE(steps.size(), kStageOneStepLadderSize);
    for (int i = 0; i < kStageOneStepLadderSize; ++i) {
        QCOMPARE(steps.at(i).toObject().value(QStringLiteral("hz")).toInt(),
                 kStageOneStepLadder[i]);
    }
    QCOMPARE(steps.at(0).toObject().value(QStringLiteral("label")).toString(),
             QStringLiteral("1 Hz"));
    QCOMPARE(steps.at(4).toObject().value(QStringLiteral("label")).toString(),
             QStringLiteral("1 kHz"));

    // AGC: the flag's five modes and its AGC-T range.
    const QJsonObject agc = catalog.value(QStringLiteral("agc")).toObject();
    const QJsonArray agcModes = agc.value(QStringLiteral("modes")).toArray();
    QCOMPARE(agcModes.size(), int(kAgcModes.size()));
    for (int i = 0; i < agcModes.size(); ++i) {
        QCOMPARE(agcModes.at(i).toObject().value(QStringLiteral("id")).toInt(),
                 kAgcModes[static_cast<std::size_t>(i)].id);
        QCOMPARE(agcModes.at(i).toObject().value(QStringLiteral("label")).toString(),
                 QString::fromLatin1(kAgcModes[static_cast<std::size_t>(i)].label));
    }
    QCOMPARE(agc.value(QStringLiteral("thresholdDb")).toObject(),
             range(kAgcThresholdMinDb, kAgcThresholdMaxDb, kAgcThresholdStepDb));
    // Thetis's clamp in setAGCThresholdPoint, console.cs:46048-46049 [v2.10.3.15].
    QCOMPARE(kAgcThresholdMinDb, -160);
    QCOMPARE(kAgcThresholdMaxDb, 2);
    QCOMPARE(kAgcThresholdStepDb, 1);

    // Receive: the AF and SQL sliders in slider units, the AM and FM squelch
    // thresholds in dB, each as the desktop's control holds it.
    const QJsonObject receive = catalog.value(QStringLiteral("receive")).toObject();
    const QStringList receiveKeys = receive.keys();
    QCOMPARE(QSet<QString>(receiveKeys.cbegin(), receiveKeys.cend()),
             (QSet<QString>{QStringLiteral("afGain"), QStringLiteral("ssqlThresh"),
                            QStringLiteral("amsqThresh"), QStringLiteral("fmsqThresh")}));
    QCOMPARE(receive.value(QStringLiteral("afGain")).toObject(),
             range(kAfGainMin, kAfGainMax, kAfGainStep));
    QCOMPARE(receive.value(QStringLiteral("ssqlThresh")).toObject(),
             range(kSsqlThreshMin, kSsqlThreshMax, kSsqlThreshStep));
    QCOMPARE(receive.value(QStringLiteral("amsqThresh")).toObject(),
             range(kAmsqThreshMinDb, kAmsqThreshMaxDb, kAmsqThreshStepDb));
    QCOMPARE(receive.value(QStringLiteral("fmsqThresh")).toObject(),
             range(kFmsqThreshMinDb, kFmsqThreshMaxDb, kFmsqThreshStepDb));
    // Thetis's AF slider (console.Designer.cs:3729-3730) and squelch slider
    // (console.Designer.cs:7572-7573) [v2.10.3.15]; the SQL slider's 0..100.
    QCOMPARE(kAfGainMin, 0);
    QCOMPARE(kAfGainMax, 100);
    QCOMPARE(kSsqlThreshMin, 0);
    QCOMPARE(kSsqlThreshMax, 100);
    QCOMPARE(kAmsqThreshMinDb, -160);
    QCOMPARE(kAmsqThreshMaxDb, 0);
    QCOMPARE(kFmsqThreshMinDb, -160);
    QCOMPARE(kFmsqThreshMaxDb, 0);

    // Meters: the S-meter's scale and the three transmit gauges.
    const QJsonObject meters = catalog.value(QStringLiteral("meters")).toObject();
    const QJsonObject sMeter = meters.value(QStringLiteral("sMeter")).toObject();
    QCOMPARE(sMeter.value(QStringLiteral("minDbm")).toDouble(), double(kSMeterS0Dbm));
    QCOMPARE(sMeter.value(QStringLiteral("s9Dbm")).toDouble(), double(kSMeterS9Dbm));
    QCOMPARE(sMeter.value(QStringLiteral("maxDbm")).toDouble(), double(kSMeterMaxDbm));
    QCOMPARE(sMeter.value(QStringLiteral("dbPerSUnit")).toDouble(), double(kSMeterDbPerSUnit));
    const QJsonArray sUnits = sMeter.value(QStringLiteral("sUnits")).toArray();
    QCOMPARE(sUnits.size(), 10);
    QCOMPARE(sUnits.at(9).toObject().value(QStringLiteral("dbm")).toDouble(),
             double(kSMeterS9Dbm));
    const QJsonArray overS9 = sMeter.value(QStringLiteral("overS9")).toArray();
    QCOMPARE(overS9.size(), 6);
    QCOMPARE(overS9.last().toObject().value(QStringLiteral("dbm")).toDouble(),
             double(kSMeterMaxDbm));
    QCOMPARE(meters.value(QStringLiteral("micLevel")).toObject(),
             (QJsonObject{{QStringLiteral("minDb"), kMicLevelMinDb},
                          {QStringLiteral("maxDb"), kMicLevelMaxDb},
                          {QStringLiteral("yellowFromDb"), kMicLevelYellowFromDb},
                          {QStringLiteral("redFromDb"), kMicLevelRedFromDb}}));
    QCOMPARE(meters.value(QStringLiteral("swr")).toObject(),
             (QJsonObject{{QStringLiteral("min"), kSwrGaugeMin},
                          {QStringLiteral("max"), kSwrGaugeMax},
                          {QStringLiteral("redFrom"), kSwrGaugeRedFrom}}));
    const double ratedW = paMaxWattsFor(model);
    const QJsonObject rfPower = meters.value(QStringLiteral("rfPower")).toObject();
    QCOMPARE(rfPower.value(QStringLiteral("ratedW")).toDouble(), ratedW);
    QCOMPARE(rfPower.value(QStringLiteral("redFromW")).toDouble(), ratedW);
    QCOMPARE(rfPower.value(QStringLiteral("maxW")).toDouble(), ratedW * kRfPowerGaugeHeadroom);

    // Board: as the RX applet, the antenna popups and Radio Info have it.
    const QJsonObject board = catalog.value(QStringLiteral("board")).toObject();
    QCOMPARE(board.value(QStringLiteral("model")).toInt(), static_cast<int>(model));
    QCOMPARE(board.value(QStringLiteral("productLabel")).toString(),
             QString::fromLatin1(displayName(model)));
    QCOMPARE(board.value(QStringLiteral("maxSlices")).toInt(), caps.maxSlices);
    QCOMPARE(board.value(QStringLiteral("attenuator")).toObject(),
             range(caps.attenuator.minDb, BoardCapsTable::stepAttMaxDb(caps.board, caps.hasAlexFilters),
                   caps.attenuator.stepDb));
    QJsonArray preampItems;
    for (const auto& item : BoardCapsTable::preampItemsForBoard(caps.board, caps.hasAlexFilters)) {
        preampItems.append(QJsonObject{{QStringLiteral("id"), item.modeInt},
                                       {QStringLiteral("label"), QString::fromLatin1(item.label)}});
    }
    QCOMPARE(board.value(QStringLiteral("preampItems")).toArray(), preampItems);
    QJsonArray rates;
    for (int rate : allowedSampleRates(protocol, caps, model)) {
        rates.append(rate);
    }
    QCOMPARE(board.value(QStringLiteral("sampleRates")).toArray(), rates);
    QCOMPARE(board.value(QStringLiteral("pureSignal")).toBool(), caps.hasPureSignal);
    QCOMPARE(board.value(QStringLiteral("paRatingW")).toInt(), paMaxWattsFor(model));
    QCOMPARE(board.value(QStringLiteral("micJack")).toBool(), caps.hasMicJack);
    QCOMPARE(board.value(QStringLiteral("rxAntennas")).toArray().size(), caps.antennaInputCount);
    QCOMPARE(board.value(QStringLiteral("txAntennas")).toArray().size(), caps.antennaInputCount);
    const SkuUiProfile sku = skuUiProfileFor(model);
    const QJsonArray rxOnly = board.value(QStringLiteral("rxOnlyInputs")).toArray();
    QCOMPARE(rxOnly.size(), caps.rxOnlyAntennaCount);
    for (int i = 0; i < rxOnly.size(); ++i) {
        QCOMPARE(rxOnly.at(i).toString(), sku.rxOnlyLabels.at(static_cast<std::size_t>(i)));
    }

    // Band plans: the five bundled plans, ARRL the default.
    const QJsonArray plans = catalog.value(QStringLiteral("bandPlans")).toArray();
    QCOMPARE(plans.size(), 5);
    int defaults = 0;
    for (const QJsonValue& p : plans) {
        const QJsonObject plan = p.toObject();
        if (plan.value(QStringLiteral("default")).toBool()) {
            ++defaults;
            QCOMPARE(plan.value(QStringLiteral("id")).toString(), QStringLiteral("arrl-us"));
            QCOMPARE(plan.value(QStringLiteral("name")).toString(), QStringLiteral("ARRL (US)"));
            const QJsonObject first =
                plan.value(QStringLiteral("segments")).toArray().first().toObject();
            QCOMPARE(first.value(QStringLiteral("lowHz")).toInteger(), 135700);
            QCOMPARE(first.value(QStringLiteral("highHz")).toInteger(), 137800);
            QCOMPARE(first.value(QStringLiteral("label")).toString(), QStringLiteral("CW"));
            QCOMPARE(first.value(QStringLiteral("licence")).toString(), QStringLiteral("E,G"));
            QCOMPARE(first.value(QStringLiteral("colour")).toString(), QStringLiteral("#3060FF"));
            QCOMPARE(first.value(QStringLiteral("lowestClass")).toString(),
                     QStringLiteral("General"));
            // The lowest licence class, by the band-plan strip's rule: the
            // 80 m phone segments, one General and up, one Extra only.
            int phoneGeneral = 0;
            int phoneExtra = 0;
            for (const QJsonValue& s : plan.value(QStringLiteral("segments")).toArray()) {
                const QJsonObject seg = s.toObject();
                QVERIFY2(seg.contains(QStringLiteral("lowestClass")),
                         "a band-plan segment has no lowestClass");
                QCOMPARE(seg.value(QStringLiteral("lowestClass")).toString(),
                         lowestLicenceClass(seg.value(QStringLiteral("licence")).toString()));
                if (seg.value(QStringLiteral("label")).toString() != QStringLiteral("PHONE")) {
                    continue;
                }
                if (seg.value(QStringLiteral("lowHz")).toInteger() == 3800000) {
                    QCOMPARE(seg.value(QStringLiteral("licence")).toString(), QStringLiteral("E,G"));
                    QCOMPARE(seg.value(QStringLiteral("lowestClass")).toString(),
                             QStringLiteral("General"));
                    ++phoneGeneral;
                }
                if (seg.value(QStringLiteral("lowHz")).toInteger() == 3600000) {
                    QCOMPARE(seg.value(QStringLiteral("licence")).toString(), QStringLiteral("E"));
                    QCOMPARE(seg.value(QStringLiteral("lowestClass")).toString(),
                             QStringLiteral("Extra"));
                    ++phoneExtra;
                }
            }
            QCOMPARE(phoneGeneral, 1);
            QCOMPARE(phoneExtra, 1);
        }
    }
    QCOMPARE(defaults, 1);

    // Palettes: every scheme but Custom, with the desktop's stops.
    const QJsonArray palettes = catalog.value(QStringLiteral("palettes")).toArray();
    QCOMPARE(palettes.size(), static_cast<int>(WfColorScheme::Count) - 1);
    for (const QJsonValue& p : palettes) {
        const QJsonObject palette = p.toObject();
        const auto scheme = static_cast<WfColorScheme>(palette.value(QStringLiteral("id")).toInt());
        QVERIFY(scheme != WfColorScheme::Custom);
        QCOMPARE(palette.value(QStringLiteral("name")).toString(),
                 QString::fromLatin1(wfSchemeName(scheme)));
        int count = 0;
        const WfGradientStop* stops = wfSchemeStops(scheme, count);
        const QJsonArray stopArray = palette.value(QStringLiteral("stops")).toArray();
        QCOMPARE(stopArray.size(), count);
        const QJsonObject last = stopArray.last().toObject();
        QCOMPARE(last.value(QStringLiteral("at")).toDouble(), 1.0);
        QCOMPARE(last.value(QStringLiteral("colour")).toString(),
                 QColor(stops[count - 1].r, stops[count - 1].g, stops[count - 1].b)
                     .name().toUpper());
    }

    // Slice colours: the flag's, one per slice the board allows.
    const QJsonArray colours = catalog.value(QStringLiteral("sliceColours")).toArray();
    QCOMPARE(colours.size(), caps.maxSlices);
    for (int i = 0; i < colours.size(); ++i) {
        QCOMPARE(colours.at(i).toString(),
                 QColor(static_cast<QRgb>(sliceColour(i))).name().toUpper());
    }

    // Tools and the Radio menu's items, in the desktop's order, without
    // MIDI Mapping and Macro Buttons (D42).
    QStringList tools;
    for (const QJsonValue& t : catalog.value(QStringLiteral("tools")).toArray()) {
        const QJsonObject tool = t.toObject();
        tools << tool.value(QStringLiteral("label")).toString();
        const QString where = tool.value(QStringLiteral("where")).toString();
        QVERIFY(where == QStringLiteral("station") || where == QStringLiteral("both"));
        QVERIFY(tool.value(QStringLiteral("offered")).isBool());
    }
    QCOMPARE(tools, (QStringList{QStringLiteral("Spot Hub"), QStringLiteral("FreeDV Reporter"),
                                 QStringLiteral("TX Equalizer"), QStringLiteral("PureSignal"),
                                 QStringLiteral("Diversity"), QStringLiteral("CWX"),
                                 QStringLiteral("Memory Manager"), QStringLiteral("CAT Control"),
                                 QStringLiteral("TCI Server"), QStringLiteral("VAX Audio"),
                                 QStringLiteral("Network Diagnostics"),
                                 QStringLiteral("Support Bundle")}));
    QStringList radioItems;
    for (const QJsonValue& r : catalog.value(QStringLiteral("radioItems")).toArray()) {
        radioItems << r.toObject().value(QStringLiteral("label")).toString();
    }
    QCOMPARE(radioItems, (QStringList{QStringLiteral("Manage Radios"),
                                      QStringLiteral("Antenna Setup"),
                                      QStringLiteral("Transverters"),
                                      QStringLiteral("Protocol Info")}));

    QCOMPARE(catalog.value(QStringLiteral("audio")).toObject(), QJsonObject{});
}

// The keys of two objects whose values differ.
QSet<QString> differingKeys(const QJsonObject& a, const QJsonObject& b)
{
    QSet<QString> keys;
    for (const QString& key : a.keys()) {
        if (a.value(key) != b.value(key)) {
            keys.insert(key);
        }
    }
    for (const QString& key : b.keys()) {
        if (!a.contains(key)) {
            keys.insert(key);
        }
    }
    return keys;
}

// One Core over the loopback, its HL2 static, in scratch directories.
struct Core {
    QTemporaryDir settingsDir;
    QTemporaryDir securityDir;
    std::unique_ptr<AppSettings> settings;
    std::unique_ptr<RadioModel> model;
    std::unique_ptr<StationServer> server;
    std::unique_ptr<LoopbackTransport> app;

    Core()
    {
        settings = std::make_unique<AppSettings>(
            settingsDir.filePath(QStringLiteral("NereusSDR.settings")));
        model = std::make_unique<RadioModel>();
        model->setBoardForTest(HPSDRHW::HermesLite);
        RadioInfo info;
        info.macAddress = QStringLiteral("AA:BB:CC:DD:EE:01");
        info.name = QStringLiteral("Bench HL2");
        info.boardType = HPSDRHW::HermesLite;
        model->setLastRadioInfoForTest(info);
        model->setConnectionStateForTest(ConnectionState::Connected);
        model->addSlice(QStringLiteral("pan-0"));
        server = std::make_unique<StationServer>(
            model.get(), *settings, NereusSDR::Test::seedUpgradedCoreToken(securityDir.path()));
    }

    ~Core()
    {
        server.reset();
        app.reset();
    }

    // A window at `minor` signed in with the token; true once its snapshot
    // is complete.
    bool connect(quint16 minor)
    {
        app = std::make_unique<LoopbackTransport>(QStringLiteral("app"));
        auto* station = new LoopbackTransport(QStringLiteral("station"), server.get());
        station->linkTo(app.get());
        server->acceptTransport(station);
        if (!QTest::qWaitFor([this]() { return !app->received().isEmpty(); }, 5000)) {
            return false;
        }
        app->sendText(SessionMessages::encode(SessionMessages::hello(
            kSessionProtocolMajor, minor, 0, QStringLiteral("NereusSDR iPhone"))));
        app->sendText(SessionMessages::encode(SessionMessages::authRequest(server->token())));
        return QTest::qWaitFor(
            [this]() {
                return app->receivedKinds().contains(QByteArrayLiteral("snapshot.complete"));
            },
            5000);
    }

    // The catalogue's revision as the client last heard it (object.create,
    // then each delta); -1 if it never heard of it.
    qint64 heardRevision(QString* json = nullptr) const
    {
        qint64 revision = -1;
        for (const QByteArray& wire : app->received()) {
            const QJsonObject o = QJsonDocument::fromJson(wire).object();
            const QString type = o.value(QStringLiteral("type")).toString();
            if ((type != QStringLiteral("object.create") && type != QStringLiteral("delta"))
                || o.value(QStringLiteral("key")).toString() != QStringLiteral("catalog")) {
                continue;
            }
            for (const QJsonValue& p : o.value(QStringLiteral("properties")).toArray()) {
                const QJsonObject property = p.toObject();
                const QString name = property.value(QStringLiteral("name")).toString();
                if (name == QStringLiteral("revision")) {
                    revision = property.value(QStringLiteral("value")).toInteger();
                } else if (name == QStringLiteral("json") && json != nullptr) {
                    *json = property.value(QStringLiteral("value")).toString();
                }
            }
        }
        return revision;
    }
};

} // namespace

class TstStationCatalog : public QObject {
    Q_OBJECT

private slots:
    void init() { AppSettings::instance().clear(); }
    void cleanup() { AppSettings::instance().clear(); }

    // The grid the desktop draws, in the order it draws it: 160 to 6 m, then
    // WWV, each id the Band enum's value.
    void theBandsAreTheDesktopsGrid()
    {
        BandPlanManager plans;
        plans.loadPlans();
        const QJsonObject catalog = StationCatalog::build(
            inputsFor(HPSDRModel::HERMESLITE, ProtocolVersion::Protocol1, plans));
        const QJsonArray bands = catalog.value(QStringLiteral("bands")).toArray();
        QStringList labels;
        QList<int> ids;
        for (const QJsonValue& value : bands) {
            labels.append(value.toObject().value(QStringLiteral("label")).toString());
            ids.append(value.toObject().value(QStringLiteral("id")).toInt());
        }
        QCOMPARE(labels, (QStringList{QStringLiteral("160"), QStringLiteral("80"),
                                      QStringLiteral("60"), QStringLiteral("40"),
                                      QStringLiteral("30"), QStringLiteral("20"),
                                      QStringLiteral("17"), QStringLiteral("15"),
                                      QStringLiteral("12"), QStringLiteral("10"),
                                      QStringLiteral("6"), QStringLiteral("WWV")}));
        QCOMPARE(ids, (QList<int>{static_cast<int>(Band::Band160m), static_cast<int>(Band::Band80m),
                                  static_cast<int>(Band::Band60m), static_cast<int>(Band::Band40m),
                                  static_cast<int>(Band::Band30m), static_cast<int>(Band::Band20m),
                                  static_cast<int>(Band::Band17m), static_cast<int>(Band::Band15m),
                                  static_cast<int>(Band::Band12m), static_cast<int>(Band::Band10m),
                                  static_cast<int>(Band::Band6m), static_cast<int>(Band::WWV)}));
        QCOMPARE(ids.first(), 0);
        QCOMPARE(ids.last(), 12);
    }

    // The two fixtures hold the desktop's own values for their radio.
    void fixturesHoldTheDesktopsValues()
    {
        const QJsonObject g2 = catalogInFixture(QStringLiteral("catalog-anan-g2.json"));
        const QJsonObject hl2 = catalogInFixture(QStringLiteral("catalog-hermes-lite-2.json"));
        QVERIFY2(!g2.isEmpty(), "the G2 catalogue fixture has no catalogue");
        QVERIFY2(!hl2.isEmpty(), "the HL2 catalogue fixture has no catalogue");
        checkDesktopValues(g2, HPSDRModel::ANAN_G2, ProtocolVersion::Protocol2);
        if (QTest::currentTestFailed()) {
            return;
        }
        checkDesktopValues(hl2, HPSDRModel::HERMESLITE, ProtocolVersion::Protocol1);
    }

    // ...and differ exactly where the radios do: the board and the RF power
    // gauge its PA rating scales.
    void fixturesDifferOnlyWhereTheRadiosDo()
    {
        const QJsonObject g2 = catalogInFixture(QStringLiteral("catalog-anan-g2.json"));
        const QJsonObject hl2 = catalogInFixture(QStringLiteral("catalog-hermes-lite-2.json"));
        QCOMPARE(differingKeys(g2, hl2),
                 (QSet<QString>{QStringLiteral("board"), QStringLiteral("meters")}));
        QCOMPARE(differingKeys(g2.value(QStringLiteral("meters")).toObject(),
                               hl2.value(QStringLiteral("meters")).toObject()),
                 (QSet<QString>{QStringLiteral("rfPower")}));
        // The preamp items are the same four on both: the RX applet offers
        // the ANAN-100D set for the Saturn board and the HL2 alike
        // (BoardCapsTable::preampItemsForBoard). Both have PureSignal and
        // five slices.
        QCOMPARE(differingKeys(g2.value(QStringLiteral("board")).toObject(),
                               hl2.value(QStringLiteral("board")).toObject()),
                 (QSet<QString>{QStringLiteral("model"), QStringLiteral("productLabel"),
                                QStringLiteral("attenuator"), QStringLiteral("rxAntennas"),
                                QStringLiteral("rxOnlyInputs"), QStringLiteral("txAntennas"),
                                QStringLiteral("sampleRates"), QStringLiteral("paRatingW"),
                                QStringLiteral("micJack")}));

        const QJsonObject g2Board = g2.value(QStringLiteral("board")).toObject();
        const QJsonObject hl2Board = hl2.value(QStringLiteral("board")).toObject();
        QCOMPARE(g2Board.value(QStringLiteral("attenuator")).toObject(), range(0, 31, 1));
        QCOMPARE(hl2Board.value(QStringLiteral("attenuator")).toObject(), range(-28, 31, 1));
        QCOMPARE(g2Board.value(QStringLiteral("sampleRates")).toArray(),
                 (QJsonArray{48000, 96000, 192000, 384000, 768000, 1536000}));
        QCOMPARE(hl2Board.value(QStringLiteral("sampleRates")).toArray(),
                 (QJsonArray{48000, 96000, 192000, 384000}));
        QCOMPARE(g2Board.value(QStringLiteral("rxAntennas")).toArray(),
                 strings({QStringLiteral("ANT1"), QStringLiteral("ANT2"), QStringLiteral("ANT3")}));
        QCOMPARE(g2Board.value(QStringLiteral("rxOnlyInputs")).toArray(),
                 strings({QStringLiteral("BYPS"), QStringLiteral("EXT1"), QStringLiteral("XVTR")}));
        QCOMPARE(hl2Board.value(QStringLiteral("rxAntennas")).toArray(),
                 strings({QStringLiteral("ANT1")}));
        QCOMPARE(hl2Board.value(QStringLiteral("rxOnlyInputs")).toArray(), QJsonArray{});
        QCOMPARE(g2Board.value(QStringLiteral("micJack")).toBool(), true);
        QCOMPARE(hl2Board.value(QStringLiteral("micJack")).toBool(), false);
    }

    // A change to a preset, the step list or the band plan data moves the
    // revision once; the same inputs again move nothing.
    void revisionMovesOncePerChange()
    {
        BandPlanManager plans;
        plans.loadPlans();
        StationCatalog catalog;
        QSignalSpy changed(&catalog, &StationCatalog::catalogChanged);
        StationCatalog::Inputs inputs =
            inputsFor(HPSDRModel::HERMESLITE, ProtocolVersion::Protocol1, plans);

        catalog.setInputs(inputs);
        QCOMPARE(catalog.revision(), 1u);
        catalog.setInputs(inputs);
        QCOMPARE(catalog.revision(), 1u);
        QCOMPARE(changed.count(), 1);

        inputs.filterPresets[1].second[0].name = QStringLiteral("Wide");
        catalog.setInputs(inputs);
        QCOMPARE(catalog.revision(), 2u);

        inputs.tuneStepsHz.append(100000);
        catalog.setInputs(inputs);
        QCOMPARE(catalog.revision(), 3u);
        QVERIFY(catalog.json().contains(QStringLiteral("\"100 kHz\"")));

        inputs.bandPlans[0].segments[0].label = QStringLiteral("CW ONLY");
        catalog.setInputs(inputs);
        QCOMPARE(catalog.revision(), 4u);
        QCOMPARE(changed.count(), 4);
    }

    // The catalogue follows the Core's store: one preset (three settings)
    // moves the revision once, and the delta reaches the connected client.
    void aPresetChangeReachesTheClient()
    {
        Core core;
        QVERIFY(core.connect(kSessionProtocolMinor));
        const quint32 before = core.server->catalog()->revision();
        QCOMPARE(core.heardRevision(), qint64(before));
        QSignalSpy changed(core.server->catalog(), &StationCatalog::catalogChanged);

        FilterPreset preset;
        preset.name = QStringLiteral("Rag chew");
        preset.low = 150;
        preset.high = 2850;
        core.model->filterPresetStore()->setPreset(DSPMode::USB, 0, preset);

        QTRY_COMPARE(changed.count(), 1);
        QCOMPARE(core.server->catalog()->revision(), before + 1);
        QString json;
        QTRY_COMPARE(core.heardRevision(&json), qint64(before + 1));
        const QJsonObject catalog = QJsonDocument::fromJson(json.toUtf8()).object();
        const QJsonObject slot0 = catalog.value(QStringLiteral("filterPresets")).toObject()
                                      .value(QStringLiteral("USB")).toArray().first().toObject();
        QCOMPARE(slot0.value(QStringLiteral("label")).toString(), QStringLiteral("Rag chew"));
        QCOMPARE(slot0.value(QStringLiteral("lowHz")).toInt(), 150);
        QCOMPARE(slot0.value(QStringLiteral("highHz")).toInt(), 2850);
        // Settled: nothing else moved it.
        QTest::qWait(100);
        QCOMPARE(changed.count(), 1);
    }

    // A window below minor 11 hears of neither the object nor its version.
    void anOlderWindowIsNotSentTheCatalogue()
    {
        Core core;
        QVERIFY(core.connect(quint16(kRadioIdentitySessionProtocolMinor - 1)));
        QCOMPARE(core.heardRevision(), qint64(-1));
        for (const QByteArray& wire : core.app->received()) {
            QVERIFY(!wire.contains("StationCatalog"));
            QVERIFY(!wire.contains("stationCatalogVersion"));
        }
    }

    // The capability travels last in the minor-11 block and comes back.
    void theCapabilityIsLastInTheMinor11Block()
    {
        StationCapabilities caps;
        caps.radioIdentityEntries = true;
        caps.stationCatalogVersion = 1;
        const QList<MirrorUpdate> updates = caps.toUpdates();
        // iPhone app Task 20's displayExtrasVersion follows it, then
        // R-R3-49's transmitSettingsVersion, then bandSelectVersion, then
        // parity Task 15's meterReadingsVersion, then parity Task 16's
        // dspInfoVersion.
        // Then parity Task 19's recordStreamVersion.
        QCOMPARE(updates.at(updates.size() - 7).name, QByteArray("stationCatalogVersion"));
        QCOMPARE(updates.at(updates.size() - 6).name, QByteArray("displayExtrasVersion"));
        QCOMPARE(updates.at(updates.size() - 5).name, QByteArray("transmitSettingsVersion"));
        QCOMPARE(updates.at(updates.size() - 4).name, QByteArray("bandSelectVersion"));
        QCOMPARE(updates.at(updates.size() - 3).name, QByteArray("meterReadingsVersion"));
        QCOMPARE(updates.at(updates.size() - 2).name, QByteArray("dspInfoVersion"));
        QCOMPARE(updates.last().name, QByteArray("recordStreamVersion"));
        QCOMPARE(StationCapabilities::fromUpdates(updates).stationCatalogVersion, 1);
        StationCapabilities older;
        older.stationCatalogVersion = 1;
        for (const MirrorUpdate& update : older.toUpdates()) {
            QVERIFY(update.name != QByteArray("stationCatalogVersion"));
        }
    }

    // At most 256 KiB for every radio, on either protocol.
    void everyRadiosCatalogueFitsTheLimit()
    {
        BandPlanManager plans;
        plans.loadPlans();
        QCOMPARE(plans.plans().size(), 5);
        int largest = 0;
        for (int m = static_cast<int>(HPSDRModel::FIRST) + 1;
             m < static_cast<int>(HPSDRModel::LAST); ++m) {
            for (ProtocolVersion protocol : {ProtocolVersion::Protocol1,
                                             ProtocolVersion::Protocol2}) {
                const QByteArray json =
                    QJsonDocument(StationCatalog::build(
                                      inputsFor(static_cast<HPSDRModel>(m), protocol, plans)))
                        .toJson(QJsonDocument::Compact);
                largest = std::max(largest, int(json.size()));
            }
        }
        QVERIFY2(largest <= StationCatalog::kMaxJsonBytes,
                 qPrintable(QStringLiteral("largest catalogue %1 bytes").arg(largest)));
        QCOMPARE(StationCatalog::kMaxJsonBytes, 256 * 1024);
    }
};

QTEST_MAIN(TstStationCatalog)
#include "tst_station_catalog.moc"
