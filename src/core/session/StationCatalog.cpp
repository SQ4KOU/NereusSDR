// no-port-check: NereusSDR-original.
// =================================================================
// src/core/session/StationCatalog.cpp  (NereusSDR)
// =================================================================
//
// See StationCatalog.h. The values come from where the desktop reads them;
// this file only arranges them as the link document's Catalogue section
// describes.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-24: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
//   2026-09-24: the `receive` key (AF gain, SSQL, AM and FM squelch
//               ranges). J.J. Boyd (KG4VCF), with AI-assisted
//               implementation via Anthropic Claude Code.
//   2026-09-25: each band-plan segment's `lowestClass`, from the band-plan
//               strip's own rule (lowestLicenceClass, models/BandPlan.h).
//               J.J. Boyd (KG4VCF), with AI-assisted implementation via
//               Anthropic Claude Code.
//   2026-09-25: the `bands` key, the desktop's per-pan BAND grid from its
//               own table (kBandGrid, models/BandGrid.h), for an app's band
//               buttons (R-IOS-27, R-IOS-06). J.J. Boyd (KG4VCF), with
//               AI-assisted implementation via Anthropic Claude Code.
//   2026-09-26: each band plan's `active` (the Core's own plan, from
//               BandPlanManager::activePlanName()) and `spots` (D79;
//               R-IOS-11, R-R3-49). J.J. Boyd (KG4VCF), with AI-assisted
//               implementation via Anthropic Claude Code.
// =================================================================

#include "core/session/StationCatalog.h"

#include "core/ControlRanges.h"
#include "core/HardwareProfile.h"
#include "core/SampleRateCatalog.h"
#include "core/SkuUiProfile.h"
#include "core/spectrum/WaterfallPalettes.h"
#include "models/BandGrid.h"
#include "models/BandPlanManager.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace NereusSDR {

namespace {

Q_LOGGING_CATEGORY(lcCatalog, "nereus.station.catalog")

// The 14 modes, DSPMode 0 to 13.
constexpr int kModeCount = static_cast<int>(DSPMode::RADE_L) + 1;

QString colourText(std::uint32_t rgb)
{
    return QColor(static_cast<QRgb>(rgb)).name(QColor::HexRgb).toUpper();
}

QString colourText(const QColor& colour)
{
    return colour.name(QColor::HexRgb).toUpper();
}

// Which side of the carrier a mode's passband sits on: the sign of its
// filters (SliceModel::presetsForMode). The lower three are the ones
// Thetis turns the two-tone round for (setup.cs:11058 [v2.10.3.13], as
// TwoToneController::isLowerSidebandMode reads it), and RADE-L mirrors
// RADE-U.
QString sidebandOf(DSPMode mode)
{
    switch (mode) {
    case DSPMode::LSB:
    case DSPMode::CWL:
    case DSPMode::DIGL:
    case DSPMode::RADE_L:
        return QStringLiteral("lower");
    case DSPMode::USB:
    case DSPMode::CWU:
    case DSPMode::DIGU:
    case DSPMode::RADE_U:
        return QStringLiteral("upper");
    case DSPMode::DSB:
    case DSPMode::FM:
    case DSPMode::AM:
    case DSPMode::SPEC:
    case DSPMode::SAM:
    case DSPMode::DRM:
        return QStringLiteral("both");
    }
    return QStringLiteral("both");
}

// "1 Hz", "500 Hz", "1 kHz", "2.5 kHz", "1 MHz".
QString tuneStepLabel(int hz)
{
    if (hz >= 1000000) {
        return QStringLiteral("%1 MHz").arg(QString::number(hz / 1.0e6, 'g', 10));
    }
    if (hz >= 1000) {
        return QStringLiteral("%1 kHz").arg(QString::number(hz / 1.0e3, 'g', 10));
    }
    return QStringLiteral("%1 Hz").arg(hz);
}

QJsonObject rangeObject(double min, double max, double step)
{
    return QJsonObject{{QStringLiteral("min"), min},
                       {QStringLiteral("max"), max},
                       {QStringLiteral("step"), step}};
}

QJsonArray modesArray()
{
    QJsonArray modes;
    for (int id = 0; id < kModeCount; ++id) {
        const auto mode = static_cast<DSPMode>(id);
        modes.append(QJsonObject{{QStringLiteral("id"), id},
                                 {QStringLiteral("label"), SliceModel::modeName(mode)},
                                 {QStringLiteral("sideband"), sidebandOf(mode)}});
    }
    return modes;
}

QJsonObject filterPresetsObject(const StationCatalog::Inputs& inputs)
{
    QJsonObject presets;
    for (const auto& [mode, list] : inputs.filterPresets) {
        QJsonArray entries;
        for (int slot = 0; slot < list.size(); ++slot) {
            const FilterPreset& preset = list.at(slot);
            entries.append(QJsonObject{{QStringLiteral("slot"), slot},
                                       {QStringLiteral("label"), preset.name},
                                       {QStringLiteral("lowHz"), preset.low},
                                       {QStringLiteral("highHz"), preset.high}});
        }
        presets.insert(SliceModel::modeName(mode), entries);
    }
    return presets;
}

QJsonArray tuneStepsArray(const StationCatalog::Inputs& inputs)
{
    QJsonArray steps;
    for (int hz : inputs.tuneStepsHz) {
        steps.append(QJsonObject{{QStringLiteral("hz"), hz},
                                 {QStringLiteral("label"), tuneStepLabel(hz)}});
    }
    return steps;
}

QJsonObject agcObject()
{
    QJsonArray modes;
    for (const ControlRanges::AgcModeItem& item : ControlRanges::kAgcModes) {
        modes.append(QJsonObject{{QStringLiteral("id"), item.id},
                                 {QStringLiteral("label"), QString::fromLatin1(item.label)}});
    }
    // The Modes tab's AGC section shows the five modes, AGC-T and AUTO (an
    // on/off switch), so AGC-T is the one range it needs.
    return QJsonObject{
        {QStringLiteral("modes"), modes},
        {QStringLiteral("thresholdDb"),
         rangeObject(ControlRanges::kAgcThresholdMinDb, ControlRanges::kAgcThresholdMaxDb,
                     ControlRanges::kAgcThresholdStepDb)},
    };
}

// The receive controls' ranges, each as the desktop's own control holds it:
// the AF slider and the SQL slider in slider units, the AM and FM squelch
// thresholds in dB (ControlRanges.h).
QJsonObject receiveObject()
{
    using namespace ControlRanges;
    return QJsonObject{
        {QStringLiteral("afGain"), rangeObject(kAfGainMin, kAfGainMax, kAfGainStep)},
        {QStringLiteral("ssqlThresh"),
         rangeObject(kSsqlThreshMin, kSsqlThreshMax, kSsqlThreshStep)},
        {QStringLiteral("amsqThresh"),
         rangeObject(kAmsqThreshMinDb, kAmsqThreshMaxDb, kAmsqThreshStepDb)},
        {QStringLiteral("fmsqThresh"),
         rangeObject(kFmsqThreshMinDb, kFmsqThreshMaxDb, kFmsqThreshStepDb)},
    };
}

QJsonObject metersObject(const StationCatalog::Inputs& inputs)
{
    using namespace ControlRanges;

    QJsonArray sUnits;
    for (int s = 0; s <= 9; ++s) {
        sUnits.append(QJsonObject{
            {QStringLiteral("label"), QStringLiteral("S%1").arg(s)},
            {QStringLiteral("dbm"), double(kSMeterS0Dbm + float(s) * kSMeterDbPerSUnit)}});
    }
    QJsonArray overS9;
    const int overMax = static_cast<int>(kSMeterMaxDbm - kSMeterS9Dbm);
    for (int over = kSMeterOverS9StepDb; over <= overMax; over += kSMeterOverS9StepDb) {
        overS9.append(QJsonObject{{QStringLiteral("label"), QStringLiteral("+%1").arg(over)},
                                  {QStringLiteral("dbm"), double(kSMeterS9Dbm) + over}});
    }
    const QJsonObject sMeter{
        {QStringLiteral("minDbm"), double(kSMeterS0Dbm)},
        {QStringLiteral("s9Dbm"), double(kSMeterS9Dbm)},
        {QStringLiteral("maxDbm"), double(kSMeterMaxDbm)},
        {QStringLiteral("dbPerSUnit"), double(kSMeterDbPerSUnit)},
        {QStringLiteral("redFromDbm"), double(kSMeterS9Dbm)},
        {QStringLiteral("sUnits"), sUnits},
        {QStringLiteral("overS9"), overS9},
    };
    const QJsonObject micLevel{
        {QStringLiteral("minDb"), kMicLevelMinDb},
        {QStringLiteral("maxDb"), kMicLevelMaxDb},
        {QStringLiteral("yellowFromDb"), kMicLevelYellowFromDb},
        {QStringLiteral("redFromDb"), kMicLevelRedFromDb},
    };
    // As the TX applet scales its gauge (rescaleFwdGaugeForModel): red from
    // the PA rating, full scale past it by the headroom.
    const double ratedW = paMaxWattsFor(inputs.model);
    const QJsonObject rfPower{
        {QStringLiteral("minW"), 0.0},
        {QStringLiteral("maxW"), ratedW * kRfPowerGaugeHeadroom},
        {QStringLiteral("ratedW"), ratedW},
        {QStringLiteral("redFromW"), ratedW},
    };
    const QJsonObject swr{
        {QStringLiteral("min"), kSwrGaugeMin},
        {QStringLiteral("max"), kSwrGaugeMax},
        {QStringLiteral("redFrom"), kSwrGaugeRedFrom},
    };
    return QJsonObject{{QStringLiteral("sMeter"), sMeter},
                       {QStringLiteral("micLevel"), micLevel},
                       {QStringLiteral("rfPower"), rfPower},
                       {QStringLiteral("swr"), swr}};
}

int boardMaxSlices(const BoardCapabilities& caps)
{
    return caps.maxSlices > 0 ? caps.maxSlices : 1;
}

QJsonObject boardObject(const StationCatalog::Inputs& inputs)
{
    const BoardCapabilities& caps = inputs.board;

    // The step attenuator as the RX applet ranges its S-ATT box
    // (rebuildPreampAndAttRangeForBoard): the board's floor, and
    // stepAttMaxDb for its ceiling.
    QJsonValue attenuator = QJsonValue::Null;
    if (caps.attenuator.present) {
        attenuator = rangeObject(caps.attenuator.minDb,
                                 BoardCapsTable::stepAttMaxDb(caps.board, caps.hasAlexFilters),
                                 caps.attenuator.stepDb);
    }

    // The preamp items the RX applet's combo lists for this board.
    QJsonArray preampItems;
    for (const auto& item : BoardCapsTable::preampItemsForBoard(caps.board, caps.hasAlexFilters)) {
        preampItems.append(QJsonObject{{QStringLiteral("id"), item.modeInt},
                                       {QStringLiteral("label"), QString::fromLatin1(item.label)}});
    }

    // The main antenna ports, ANT1 upwards (AntennaLabels' names), which
    // both receive and transmit, and the product's receive-only inputs
    // (SkuUiProfile's labels, as many as the board has).
    QJsonArray mainAntennas;
    for (int i = 1; i <= caps.antennaInputCount; ++i) {
        mainAntennas.append(QStringLiteral("ANT%1").arg(i));
    }
    QJsonArray rxOnlyInputs;
    const SkuUiProfile sku = skuUiProfileFor(inputs.model);
    const int rxOnlyCount = std::min<int>(caps.rxOnlyAntennaCount,
                                          static_cast<int>(sku.rxOnlyLabels.size()));
    for (int i = 0; i < rxOnlyCount; ++i) {
        rxOnlyInputs.append(sku.rxOnlyLabels.at(static_cast<std::size_t>(i)));
    }

    // The rates the radio offers on the protocol it runs, as Setup >
    // Hardware > Radio Info lists them.
    QJsonArray sampleRates;
    for (int rate : allowedSampleRates(inputs.protocol, caps, inputs.model)) {
        sampleRates.append(rate);
    }

    return QJsonObject{
        {QStringLiteral("model"), static_cast<int>(inputs.model)},
        {QStringLiteral("productLabel"), QString::fromLatin1(displayName(inputs.model))},
        {QStringLiteral("maxSlices"), boardMaxSlices(caps)},
        {QStringLiteral("attenuator"), attenuator},
        {QStringLiteral("preampItems"), preampItems},
        {QStringLiteral("rxAntennas"), mainAntennas},
        {QStringLiteral("rxOnlyInputs"), rxOnlyInputs},
        {QStringLiteral("txAntennas"), mainAntennas},
        {QStringLiteral("sampleRates"), sampleRates},
        {QStringLiteral("pureSignal"), caps.hasPureSignal},
        {QStringLiteral("paRatingW"), paMaxWattsFor(inputs.model)},
        {QStringLiteral("micJack"), caps.hasMicJack},
    };
}

QJsonArray bandPlansArray(const StationCatalog::Inputs& inputs)
{
    QJsonArray plans;
    for (const StationCatalog::BandPlan& plan : inputs.bandPlans) {
        QJsonArray segments;
        for (const BandSegment& segment : plan.segments) {
            segments.append(QJsonObject{
                {QStringLiteral("lowHz"), static_cast<qint64>(std::llround(segment.lowMhz * 1.0e6))},
                {QStringLiteral("highHz"), static_cast<qint64>(std::llround(segment.highMhz * 1.0e6))},
                {QStringLiteral("label"), segment.label},
                {QStringLiteral("licence"), segment.license},
                {QStringLiteral("lowestClass"), lowestLicenceClass(segment.license)},
                {QStringLiteral("colour"), colourText(segment.color)},
            });
        }
        // R-IOS-11 (D79): the plan's spots, as its file lists them, and
        // whether it is the Core's own plan.
        QJsonArray spots;
        for (const BandSpot& spot : plan.spots) {
            spots.append(QJsonObject{
                {QStringLiteral("hz"), static_cast<qint64>(std::llround(spot.freqMhz * 1.0e6))},
                {QStringLiteral("label"), spot.label},
            });
        }
        plans.append(QJsonObject{{QStringLiteral("id"), plan.id},
                                 {QStringLiteral("name"), plan.name},
                                 {QStringLiteral("default"), plan.name == inputs.defaultBandPlanName},
                                 {QStringLiteral("active"), plan.name == inputs.activeBandPlanName},
                                 {QStringLiteral("segments"), segments},
                                 {QStringLiteral("spots"), spots}});
    }
    return plans;
}

QJsonArray palettesArray()
{
    QJsonArray palettes;
    for (int id = 0; id < static_cast<int>(WfColorScheme::Count); ++id) {
        const auto scheme = static_cast<WfColorScheme>(id);
        // Custom is each computer's own gradient (DisplayWfCustomStops, a
        // setting that stays on that computer), so the Core has none to send.
        if (scheme == WfColorScheme::Custom) {
            continue;
        }
        int count = 0;
        const WfGradientStop* stops = wfSchemeStops(scheme, count);
        QJsonArray stopArray;
        for (int i = 0; i < count; ++i) {
            const WfGradientStop& stop = stops[i];
            // Positions to three places: the tables are written that way,
            // and a float's binary tail is not part of the value.
            const double at = std::round(double(stop.pos) * 1000.0) / 1000.0;
            stopArray.append(QJsonObject{
                {QStringLiteral("at"), at},
                {QStringLiteral("colour"), colourText(QColor(stop.r, stop.g, stop.b))}});
        }
        palettes.append(QJsonObject{{QStringLiteral("id"), id},
                                    {QStringLiteral("name"), QString::fromLatin1(wfSchemeName(scheme))},
                                    {QStringLiteral("stops"), stopArray}});
    }
    return palettes;
}

QJsonArray sliceColoursArray(const StationCatalog::Inputs& inputs)
{
    QJsonArray colours;
    for (int i = 0; i < boardMaxSlices(inputs.board); ++i) {
        colours.append(colourText(ControlRanges::sliceColour(i)));
    }
    return colours;
}

// The desktop's Tools menu, in its order, without MIDI Mapping and Macro
// Buttons (D42). `where` is where the tool works: at the Core, or on both
// the Core and the device. Offered means built on the desktop: CWX, the
// Memory Manager and CAT Control are hidden there until built
// (UnbuiltFeature::Cwx, Memories, Cat). iPhone app Task 25 adds the rules
// that follow the radio and the Core (PureSignal present, a second
// receiver, a running TCI server, VAX devices).
struct ToolEntry {
    const char* id;
    const char* label;
    const char* where;
    bool offered;
};
constexpr ToolEntry kTools[] = {
    {"spotHub", "Spot Hub", "both", true},
    {"freedvReporter", "FreeDV Reporter", "station", true},
    {"txEqualizer", "TX Equalizer", "station", true},
    {"pureSignal", "PureSignal", "station", true},
    {"diversity", "Diversity", "station", true},
    {"cwx", "CWX", "station", false},
    {"memoryManager", "Memory Manager", "station", false},
    {"catControl", "CAT Control", "station", false},
    {"tciServer", "TCI Server", "station", true},
    {"vaxAudio", "VAX Audio", "station", true},
    {"networkDiagnostics", "Network Diagnostics", "both", true},
    {"supportBundle", "Support Bundle", "both", true},
};

// The desktop's Radio menu items an app lists, in its order. Transverters
// is hidden there until built (UnbuiltFeature::Transverters).
struct RadioItemEntry {
    const char* id;
    const char* label;
    bool offered;
};
constexpr RadioItemEntry kRadioItems[] = {
    {"manageRadios", "Manage Radios", true},
    {"antennaSetup", "Antenna Setup", true},
    {"transverters", "Transverters", false},
    {"protocolInfo", "Protocol Info", true},
};

QJsonArray toolsArray()
{
    QJsonArray tools;
    for (const ToolEntry& tool : kTools) {
        tools.append(QJsonObject{{QStringLiteral("id"), QString::fromLatin1(tool.id)},
                                 {QStringLiteral("label"), QString::fromLatin1(tool.label)},
                                 {QStringLiteral("where"), QString::fromLatin1(tool.where)},
                                 {QStringLiteral("offered"), tool.offered}});
    }
    return tools;
}

QJsonArray radioItemsArray()
{
    QJsonArray items;
    for (const RadioItemEntry& item : kRadioItems) {
        items.append(QJsonObject{{QStringLiteral("id"), QString::fromLatin1(item.id)},
                                 {QStringLiteral("label"), QString::fromLatin1(item.label)},
                                 {QStringLiteral("offered"), item.offered}});
    }
    return items;
}

// The desktop's per-pan BAND grid, in its order: each button's Band (the
// value slice.selectBand takes) and its text.
QJsonArray bandsArray()
{
    QJsonArray bands;
    for (const BandGridEntry& entry : kBandGrid) {
        bands.append(QJsonObject{{QStringLiteral("id"), static_cast<int>(entry.band)},
                                 {QStringLiteral("label"), QString::fromLatin1(entry.label)}});
    }
    return bands;
}

// SliceModel's tune-step list (today the six-entry stand-in the STEP
// buttons cycle; the catalogue follows whatever SliceModel holds).
QList<int> sliceTuneSteps()
{
    QList<int> steps;
    for (int i = 0; i < kStageOneStepLadderSize; ++i) {
        steps.append(kStageOneStepLadder[i]);
    }
    return steps;
}

} // namespace

StationCatalog::StationCatalog(QObject* parent)
    : QObject(parent)
    , m_refreshTimer(new QTimer(this))
{
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(0);
    connect(m_refreshTimer, &QTimer::timeout, this, &StationCatalog::refresh);
}

StationCatalog::~StationCatalog() = default;

QJsonObject StationCatalog::build(const Inputs& inputs)
{
    return QJsonObject{
        {QStringLiteral("modes"), modesArray()},
        {QStringLiteral("filterPresets"), filterPresetsObject(inputs)},
        {QStringLiteral("tuneSteps"), tuneStepsArray(inputs)},
        {QStringLiteral("agc"), agcObject()},
        {QStringLiteral("receive"), receiveObject()},
        {QStringLiteral("meters"), metersObject(inputs)},
        {QStringLiteral("board"), boardObject(inputs)},
        {QStringLiteral("bandPlans"), bandPlansArray(inputs)},
        {QStringLiteral("bands"), bandsArray()},
        {QStringLiteral("palettes"), palettesArray()},
        {QStringLiteral("sliceColours"), sliceColoursArray(inputs)},
        {QStringLiteral("tools"), toolsArray()},
        {QStringLiteral("radioItems"), radioItemsArray()},
        // Filled by iPhone app Task 23.
        {QStringLiteral("audio"), QJsonObject{}},
    };
}

StationCatalog::Inputs StationCatalog::inputsFrom(const RadioModel& model)
{
    Inputs inputs;
    const HardwareProfile& profile = model.hardwareProfile();
    inputs.model = profile.caps != nullptr ? profile.model : HPSDRModel::FIRST;
    inputs.board = model.boardCapabilities();
    // The protocol the Core's radio runs; the board's own before a radio
    // has been seen.
    const RadioInfo& radio = model.currentRadioInfo();
    inputs.protocol = radio.macAddress.isEmpty() ? inputs.board.protocol : radio.protocol;

    const FilterPresetStore* store = model.filterPresetStore();
    for (int id = 0; id < kModeCount; ++id) {
        const auto mode = static_cast<DSPMode>(id);
        QList<FilterPreset> presets;
        if (store != nullptr) {
            presets = store->presetsForMode(mode);
        } else {
            const auto defaults = SliceModel::presetsForMode(mode);
            for (int slot = 0; slot < defaults.size(); ++slot) {
                presets.append(FilterPresetStore::defaultPreset(mode, slot));
            }
        }
        inputs.filterPresets.append({mode, presets});
    }

    inputs.tuneStepsHz = sliceTuneSteps();

    for (const BandPlanManager::PlanData& plan : model.bandPlanManager().plans()) {
        inputs.bandPlans.append(BandPlan{plan.id, plan.name, plan.segments, plan.spots});
    }
    inputs.defaultBandPlanName = QString::fromLatin1(BandPlanManager::kDefaultPlanName);
    inputs.activeBandPlanName = model.bandPlanManager().activePlanName();
    return inputs;
}

void StationCatalog::bind(RadioModel* model)
{
    if (m_model) {
        disconnect(m_model, nullptr, this, nullptr);
        if (FilterPresetStore* store = m_model->filterPresetStore()) {
            disconnect(store, nullptr, this, nullptr);
        }
        disconnect(&m_model->bandPlanManagerMutable(), nullptr, this, nullptr);
    }
    m_model = model;
    if (model == nullptr) {
        return;
    }
    // A preset changed on this computer, the band plan data was read again
    // or the Core's plan changed (planChanged, R-IOS-11), or the radio (and with it the board) changed. A preset written from
    // another window reaches StationServer as a settings change, which
    // schedules a refresh the same way.
    if (FilterPresetStore* store = model->filterPresetStore()) {
        connect(store, &FilterPresetStore::presetsChanged, this,
                &StationCatalog::scheduleRefresh);
    }
    connect(&model->bandPlanManagerMutable(), &BandPlanManager::planChanged, this,
            &StationCatalog::scheduleRefresh);
    connect(model, &RadioModel::currentRadioChanged, this, &StationCatalog::scheduleRefresh);
    refresh();
}

void StationCatalog::setInputs(const Inputs& inputs)
{
    const QString json = QString::fromUtf8(
        QJsonDocument(build(inputs)).toJson(QJsonDocument::Compact));
    if (json.toUtf8().size() > kMaxJsonBytes) {
        qCWarning(lcCatalog) << "The Core's catalogue is" << json.toUtf8().size()
                             << "bytes, over its" << kMaxJsonBytes << "byte limit";
    }
    if (json == m_json) {
        return;
    }
    m_json = json;
    ++m_revision;
    emit catalogChanged();
}

void StationCatalog::refresh()
{
    m_refreshTimer->stop();
    if (!m_model) {
        return;
    }
    setInputs(inputsFrom(*m_model));
}

void StationCatalog::scheduleRefresh()
{
    if (!m_refreshTimer->isActive()) {
        m_refreshTimer->start();
    }
}

} // namespace NereusSDR
