// =================================================================
// src/core/session/SessionCommandDispatcher.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R2 Task 11.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-05  J.J. Boyd / KG4VCF  Remote daemon R2 Task 11: command
//                                    dispatch (addSlice / removeSlice /
//                                    requestSliceSampleRate /
//                                    addSliceOnPan). AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
//   2026-08-05  J.J. Boyd / KG4VCF  Remote daemon R2 Task 11 fix round 1:
//                                    added setActiveSliceById (review
//                                    Important 1) plus same-thread-
//                                    invariant notes on the by-reference
//                                    lambda captures (Minor 7). AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
//   2026-08-09  J.J. Boyd / KG4VCF  Whole-branch review, Important 1:
//                                    findIntArgument() replaces four bare
//                                    QVariant::toInt() narrows that
//                                    silently truncated an out-of-range
//                                    id to 32 bits. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
//   2026-09-23  J.J. Boyd / KG4VCF  R-R3-40: nnr.tryAgain (a slice ID)
//                                    clears the runtime NNR limit; the
//                                    server admits it from minor 11.
//                                    AI-assisted implementation via
//                                    Anthropic Claude Code.
//   2026-09-23  J.J. Boyd / KG4VCF  R-R3-21 / R-R3-09: notch.add,
//                                    notch.move, notch.setActive and
//                                    notch.delete on the Core's notch list.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-23  J.J. Boyd / KG4VCF  R-R3-46: requestIoBoardProbe, the HL2
//                                    I/O board probe for a remote window.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-23  J.J. Boyd / KG4VCF  R-R3-46 fix wave (radioHardwareVersion
//                                    3): setAlexRxAntenna, one band's RX or
//                                    RX-only antenna. AI-assisted via
//                                    Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  R-R3-46 / R-R3-21 (radioHardwareVersion
//                                    4): setAlexBpfMode, one receive filter
//                                    chain's filter policy. AI-assisted via
//                                    Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  R-R3-47 / R-R3-22: configurePgxl,
//                                    disconnectPgxl and
//                                    setPgxlConnectionSettings for the
//                                    Core's Power Genius XL. AI-assisted via
//                                    Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Task 1 (R-IOS-01):
//                                    verbSpecs(), the declared verb table.
//                                    Routing unchanged. AI-assisted via
//                                    Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  R-R3-47 / R-R3-48: configureRfKit,
//                                    disconnectRfKit and setRfKitEnabled
//                                    for the Core's RF-Kit RF2K-S, and
//                                    setStationTci for the station's TCI
//                                    server. AI-assisted via Anthropic
//                                    Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Task 2 (R-IOS-01): the
//                                    RF-Kit and station TCI verbs in
//                                    verbSpecs(). AI-assisted via
//                                    Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  R-R3-47 / R-R3-22: setTxInterlockPolicy,
//                                    setPgxlPowerCap and clearAccessoryFaults
//                                    (accessoryDataVersion 1). AI-assisted
//                                    via Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Task 4 (R-IOS-01): the
//                                    accessory record verbs in
//                                    verbSpecs(). AI-assisted via
//                                    Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Task 4b (R-IOS-01,
//                                    R-R3-21): every refusal reason in
//                                    operator words. AI-assisted via
//                                    Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Part A fix wave (R-IOS-01):
//                                    a PureSignal request's arguments are
//                                    read before the transmit gate.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  R-R3-47 / R-R3-22: the amp's and
//                                    tuner's own settings (setPgxlName,
//                                    setPgxlHardware, setPgxlNetwork,
//                                    savePgxlSettings, readPgxlSettings and
//                                    the four setTgxl* / *TgxlSettings
//                                    verbs). AI-assisted via Anthropic
//                                    Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  R-R3-47: resetRfKitError (the RF-Kit
//                                    page's Reset amp error). AI-assisted
//                                    via Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  Lane B takes integration (R-IOS-01,
//                                    R-R3-21): the filter policy request's
//                                    unreadable-request reason in plain
//                                    words.
//                                    AI-assisted via Anthropic Claude Code.
// =================================================================

#include "core/session/SessionCommandDispatcher.h"

#include "core/session/ObjectRegistry.h"
#include "core/dsp/DspAssetService.h"
#include "DspCommandValues.h"
#include "PureSignalSessionFacade.h"
#include "core/accessories/AlexAntennaFacade.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <QHash>
#include <QMetaObject>
#include <QMetaType>
#include <QSet>
#include <QVariant>

#include <limits>
#include <initializer_list>
#include <cmath>

namespace NereusSDR {

namespace {

// Looks up one named argument out of a CommandInvoke's arguments list.
// `arguments` reuses MirrorUpdate as a generic {name, kind, value} triple
// (SessionMessage::arguments' own doc comment) -- `kind` and `ordinal` are
// not consulted here, only `name` and `value`. Every INTEGER argument goes
// through findIntArgument() below rather than narrowing the QVariant at
// the call site; only the string arguments (initialPanId, panId) still
// read this directly, and QVariant::toString() has no range to fall off.
bool findArgument(const QList<MirrorUpdate>& arguments, const QByteArray& name, QVariant* out)
{
    for (const MirrorUpdate& arg : arguments) {
        if (arg.name == name) {
            *out = arg.value;
            return true;
        }
    }
    return false;
}

bool findUtf8Argument(const QList<MirrorUpdate>& arguments, const QByteArray& name,
                      QString* out)
{
    for (const MirrorUpdate& arg : arguments) {
        if (arg.name == name) {
            if (arg.kind != MirrorWireKind::Utf8 || arg.value.typeId() != QMetaType::QString) {
                return false;
            }
            *out = arg.value.toString();
            return true;
        }
    }
    return false;
}

bool hasWireKind(const QList<MirrorUpdate>& arguments, const QByteArray& name,
                 MirrorWireKind kind)
{
    for (const MirrorUpdate& arg : arguments) {
        if (arg.name == name) {
            return arg.kind == kind;
        }
    }
    return false;
}

// What findIntArgument() found. Three states rather than a bool, because
// "you did not send sliceId" and "the sliceId you sent is not a number
// this station can act on" are different things: the peer is told the
// Core could not read the request, or could not use one of its values.
// Both are in operator words (R-IOS-01).
enum class ArgumentStatus {
    Ok,
    Missing,
    NotRepresentable,
};

// Every id and rate argument in this file is an `int` on RadioModel's
// side, and every one of them arrives from the far side of a socket.
//
// Fix round 5 review finding (Important 1): each of these used to be a
// bare QVariant::toInt() with the `ok` flag discarded. Measured on this
// tree's Qt, QVariant(qlonglong 4294967296).toInt() returns 0 with
// ok == true and 4294967297 returns 1, so a peer asking to remove slice
// 4294967296 removed slice 0 and got back accepted with affected
// ["slice:0"] -- a request naming an object that does not exist
// destroying a DIFFERENT object that does. One helper rather than four
// checked narrows at four call sites: the four sites want identical
// semantics, the next verb added to this file gets the safe behaviour by
// construction, and the wording a peer sees stays in one place.
//
// The runtime type is checked, not the declared MirrorWireKind. The two
// carry the same information -- SessionMessages' decoder collapses each
// wire kind onto exactly one QVariant runtime type (fromJsonValue:
// bool / qlonglong for Int64 and Enum / double / QString) -- but the
// runtime type is the stronger statement, since it also holds for an
// in-process caller that built the MirrorUpdate directly. Refusing a
// double outright also keeps this code out of QVariant's own
// floating-to-integral conversion, which is not a narrowing this layer
// should be performing on untrusted input.
ArgumentStatus findIntArgument(const QList<MirrorUpdate>& arguments,
                               const QByteArray& name, int* out)
{
    QVariant raw;
    if (!findArgument(arguments, name, &raw)) {
        return ArgumentStatus::Missing;
    }
    switch (raw.typeId()) {
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::Long:
    case QMetaType::ULong:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        break;
    default:
        return ArgumentStatus::NotRepresentable;
    }
    bool ok = false;
    const qlonglong wide = raw.toLongLong(&ok);
    if (!ok || wide < static_cast<qlonglong>(std::numeric_limits<int>::min())
        || wide > static_cast<qlonglong>(std::numeric_limits<int>::max())) {
        return ArgumentStatus::NotRepresentable;
    }
    *out = static_cast<int>(wide);
    return ArgumentStatus::Ok;
}

bool hasExactlyArguments(const QList<MirrorUpdate>& arguments,
                         std::initializer_list<QByteArray> expected)
{
    if (arguments.size() != static_cast<qsizetype>(expected.size())) {
        return false;
    }
    QSet<QByteArray> expectedNames(expected.begin(), expected.end());
    QSet<QByteArray> seen;
    for (const MirrorUpdate& argument : arguments) {
        if (!expectedNames.contains(argument.name) || seen.contains(argument.name)) {
            return false;
        }
        seen.insert(argument.name);
    }
    return true;
}

bool findFiniteDoubleArgument(const QList<MirrorUpdate>& arguments,
                              const QByteArray& name, double* out)
{
    QVariant raw;
    if (!findArgument(arguments, name, &raw) || raw.typeId() != QMetaType::Double) {
        return false;
    }
    const double value = raw.toDouble();
    if (!std::isfinite(value)) {
        return false;
    }
    *out = value;
    return true;
}

QString notRepresentableReason()
{
    return QStringLiteral("The Core could not use one of the values in this request.");
}

} // namespace

// ── The declared verb table (R-IOS-01) ───────────────────────────────────
//
// Each row is what dispatch() and the handler behind it accept: argument
// names and wire kinds as the handler checks them (every handler requires
// every argument it names, so none is optional today), and the gate a
// client applies before sending the verb, as StationClient applies it:
//
//   slice verbs            none: they predate capability gating
//   requestStreamCtun*     remoteCtunAvailable()          (StationClient.cpp)
//   configure/disconnectTgxl remoteTgxlConfigAvailable()
//   setFourO3AEnabled      remoteFourO3AControlAvailable()
//   *Pgxl*                 remotePgxlControlAvailable() (version 2)
//   *RfKit*                remoteRfKitControlAvailable() (version 2)
//   setStationTci          stationTciAvailable() (version 1)
//   setTxInterlockPolicy, setPgxlPowerCap, clearAccessoryFaults
//                          accessoryDataAvailable() (version 1)
//   requestIoBoardProbe    remoteHardwareConfigAvailable() (version 2)
//   setAlexRxAntenna       radioHardwareVersion 3 (requestAlexRxAntenna)
//   nnr.*                  nnrControlAvailable(); nnr.tryAgain adds
//                          kNnrLimitSessionProtocolMinor
//   nnr.applyModelSelection dspAssetVersion 1 (requestApplyNnrModels)
//   dspAssets.*            dspAssetVersion 1; selectNr3Model version 2
//   ps3.subscribeDisplay   psDisplayVersion 1 with media
//   ps3.<action>           psAlgorithmVersion 3
//   notch.*                remoteNotchControlAvailable()
//
// tst_link_surface_manifest keeps this table and the routing in step: a
// source scan of dispatch() and of each prefix family's handler, and a
// dispatch of every row on a live RadioModel.
const QList<CommandVerbSpec>& SessionCommandDispatcher::verbSpecs()
{
    constexpr MirrorWireKind kInt = MirrorWireKind::Int64;
    constexpr MirrorWireKind kUtf8 = MirrorWireKind::Utf8;
    constexpr MirrorWireKind kBool = MirrorWireKind::Bool;
    constexpr MirrorWireKind kDouble = MirrorWireKind::Float64;
    const auto arg = [](const char* name, MirrorWireKind kind) {
        return CommandArgumentSpec{QByteArray(name), kind, false};
    };
    const auto optionalArg = [](const char* name, MirrorWireKind kind) {
        return CommandArgumentSpec{QByteArray(name), kind, true};
    };
    static const QList<CommandVerbSpec> specs{
        // Slices (R2 Task 11).
        {"addSlice", {arg("initialPanId", kUtf8)}, {}, 0, 0},
        {"removeSlice", {arg("sliceId", kInt)}, {}, 0, 0},
        {"requestSliceSampleRate", {arg("sliceId", kInt), arg("rateHz", kInt)}, {}, 0, 0},
        {"addSliceOnPan", {arg("panId", kUtf8)}, {}, 0, 0},
        {"setActiveSliceById", {arg("sliceId", kInt)}, {}, 0, 0},
        // C-Tune.
        {"requestStreamCtunPinned", {arg("sliceId", kInt), arg("pinned", kBool)},
         "remoteCtunVersion", 1, kRemoteCtunSessionProtocolMinor},
        {"requestStreamCentre", {arg("sliceId", kInt), arg("centreHz", kDouble)},
         "remoteCtunVersion", 1, kRemoteCtunSessionProtocolMinor},
        // 4O3A accessories.
        {"configureTgxl", {arg("host", kUtf8), arg("port", kInt)},
         "remoteTgxlConfigVersion", 1, kRemoteTgxlConfigSessionProtocolMinor},
        {"disconnectTgxl", {}, "remoteTgxlConfigVersion", 1,
         kRemoteTgxlConfigSessionProtocolMinor},
        {"setFourO3AEnabled", {arg("enabled", kBool)}, "remoteFourO3AControlVersion", 1,
         kRemoteFourO3AControlSessionProtocolMinor},
        {"configurePgxl", {arg("host", kUtf8), arg("port", kInt)},
         "remotePgxlControlVersion", 2, kRadioIdentitySessionProtocolMinor},
        {"disconnectPgxl", {}, "remotePgxlControlVersion", 2,
         kRadioIdentitySessionProtocolMinor},
        {"setPgxlConnectionSettings",
         {arg("autoReconnect", kBool), arg("keepaliveSec", kInt), arg("pingSec", kInt)},
         "remotePgxlControlVersion", 2, kRadioIdentitySessionProtocolMinor},
        // The amp's and the tuner's own settings (R-R3-47, R-R3-22).
        // setPgxlHardware takes exactly one of its three arguments.
        {"setPgxlName", {arg("name", kUtf8)}, "remotePgxlControlVersion", 3,
         kRadioIdentitySessionProtocolMinor},
        {"setPgxlHardware",
         {optionalArg("biasMode", kUtf8), optionalArg("fanMode", kUtf8),
          optionalArg("ledIntensity", kInt)},
         "remotePgxlControlVersion", 3, kRadioIdentitySessionProtocolMinor},
        {"setPgxlNetwork",
         {arg("dhcp", kBool), arg("address", kUtf8), arg("netmask", kUtf8),
          arg("gateway", kUtf8)},
         "remotePgxlControlVersion", 3, kRadioIdentitySessionProtocolMinor},
        {"savePgxlSettings", {}, "remotePgxlControlVersion", 3,
         kRadioIdentitySessionProtocolMinor},
        {"readPgxlSettings", {}, "remotePgxlControlVersion", 3,
         kRadioIdentitySessionProtocolMinor},
        {"setTgxlName", {arg("name", kUtf8)}, "remoteTgxlControlVersion", 1,
         kRadioIdentitySessionProtocolMinor},
        {"setTgxlNetwork",
         {arg("dhcp", kBool), arg("address", kUtf8), arg("netmask", kUtf8),
          arg("gateway", kUtf8)},
         "remoteTgxlControlVersion", 1, kRadioIdentitySessionProtocolMinor},
        {"saveTgxlSettings", {}, "remoteTgxlControlVersion", 1,
         kRadioIdentitySessionProtocolMinor},
        {"readTgxlSettings", {}, "remoteTgxlControlVersion", 1,
         kRadioIdentitySessionProtocolMinor},
        // The Core's RF-Kit RF2K-S and the station TCI server (R-R3-47,
        // R-R3-48).
        {"configureRfKit", {arg("host", kUtf8), arg("port", kInt)},
         "remoteRfKitControlVersion", 2, kRadioIdentitySessionProtocolMinor},
        {"disconnectRfKit", {}, "remoteRfKitControlVersion", 2,
         kRadioIdentitySessionProtocolMinor},
        {"setRfKitEnabled", {arg("enabled", kBool)}, "remoteRfKitControlVersion", 2,
         kRadioIdentitySessionProtocolMinor},
        {"resetRfKitError", {}, "remoteRfKitControlVersion", 3,
         kRadioIdentitySessionProtocolMinor},
        {"setStationTci", {arg("enabled", kBool), arg("port", kInt)}, "stationTciVersion", 1,
         kRadioIdentitySessionProtocolMinor},
        // The Core's accessory records and settings (R-R3-47, R-R3-22).
        {"setTxInterlockPolicy",
         {arg("mode", kInt), arg("graceMs", kInt), arg("swrGateEnabled", kBool),
          arg("swrGateMax", kDouble)},
         "accessoryDataVersion", 1, kRadioIdentitySessionProtocolMinor},
        {"setPgxlPowerCap", {arg("enabled", kBool), arg("watts", kInt)},
         "accessoryDataVersion", 1, kRadioIdentitySessionProtocolMinor},
        {"clearAccessoryFaults", {arg("device", kUtf8)}, "accessoryDataVersion", 1,
         kRadioIdentitySessionProtocolMinor},
        // The Core's radio hardware (R-R3-46).
        {"requestIoBoardProbe", {}, "radioHardwareVersion", 2,
         kRadioIdentitySessionProtocolMinor},
        {"setAlexRxAntenna", {arg("band", kInt), arg("antenna", kInt), arg("rxOnly", kBool)},
         "radioHardwareVersion", 3, kRadioIdentitySessionProtocolMinor},
        {"setAlexBpfMode", {arg("chain", kInt), arg("mode", kInt)}, "radioHardwareVersion", 4,
         kRadioIdentitySessionProtocolMinor},
        // Neural noise reduction.
        {"nnr.setDiagnostics",
         {arg("sliceId", kInt), arg("testMode", kInt), arg("outputMode", kInt)},
         "nnrVersion", 1, kDspControlSessionProtocolMinor},
        {"nnr.resetTuning", {arg("sliceId", kInt)}, "nnrVersion", 1,
         kDspControlSessionProtocolMinor},
        {"nnr.tryAgain", {arg("sliceId", kInt)}, "nnrVersion", 1,
         kNnrLimitSessionProtocolMinor},
        {"nnr.applyModelSelection", {arg("revision", kInt)}, "dspAssetVersion", 1,
         kDspControlSessionProtocolMinor},
        // DSP assets (DspAssetService::execute).
        {"dspAssets.list", {}, "dspAssetVersion", 1, kDspControlSessionProtocolMinor},
        {"dspAssets.beginImport",
         {arg("kind", kInt), arg("label", kUtf8), arg("size", kInt), arg("hash", kUtf8),
          arg("radioIdentity", kUtf8)},
         "dspAssetVersion", 1, kDspControlSessionProtocolMinor},
        {"dspAssets.chunk",
         {arg("transferId", kUtf8), arg("offset", kInt), arg("data", kUtf8)},
         "dspAssetVersion", 1, kDspControlSessionProtocolMinor},
        {"dspAssets.finishImport", {arg("transferId", kUtf8)}, "dspAssetVersion", 1,
         kDspControlSessionProtocolMinor},
        {"dspAssets.cancelImport", {arg("transferId", kUtf8)}, "dspAssetVersion", 1,
         kDspControlSessionProtocolMinor},
        {"dspAssets.export", {arg("id", kUtf8), arg("offset", kInt)}, "dspAssetVersion", 1,
         kDspControlSessionProtocolMinor},
        {"dspAssets.selectNnrModel", {arg("slot", kInt), arg("id", kUtf8)},
         "dspAssetVersion", 1, kDspControlSessionProtocolMinor},
        {"dspAssets.selectNr3Model", {arg("id", kUtf8)}, "dspAssetVersion", 2,
         kDspControlSessionProtocolMinor},
        // PureSignal (PureSignalSessionFacade::actionVerb and the display
        // subscription handled here).
        {"ps3.subscribeDisplay", {arg("enabled", kBool)}, "psDisplayVersion", 1,
         kMediaSessionProtocolMinor},
        {"ps3.off", {}, "psAlgorithmVersion", 3, kDspControlSessionProtocolMinor},
        {"ps3.single", {}, "psAlgorithmVersion", 3, kDspControlSessionProtocolMinor},
        {"ps3.automatic", {}, "psAlgorithmVersion", 3, kDspControlSessionProtocolMinor},
        {"ps3.applyCurrent", {}, "psAlgorithmVersion", 3, kDspControlSessionProtocolMinor},
        {"ps3.twoTone", {arg("enabled", kBool)}, "psAlgorithmVersion", 3,
         kDspControlSessionProtocolMinor},
        {"ps3.saveCorrection", {arg("label", kUtf8)}, "psAlgorithmVersion", 3,
         kDspControlSessionProtocolMinor},
        {"ps3.restoreCorrection", {arg("assetId", kUtf8)}, "psAlgorithmVersion", 3,
         kDspControlSessionProtocolMinor},
        // Notches (R-R3-21 / R-R3-09).
        {"notch.add", {arg("sliceId", kInt), arg("centreHz", kDouble), arg("widthHz", kDouble)},
         "notchControlVersion", 1, kDspControlSessionProtocolMinor},
        {"notch.move", {arg("id", kInt), arg("centreHz", kDouble), arg("widthHz", kDouble)},
         "notchControlVersion", 1, kDspControlSessionProtocolMinor},
        {"notch.setActive", {arg("id", kInt), arg("active", kBool)}, "notchControlVersion", 1,
         kDspControlSessionProtocolMinor},
        {"notch.delete", {arg("id", kInt)}, "notchControlVersion", 1,
         kDspControlSessionProtocolMinor},
    };
    return specs;
}

SessionCommandDispatcher::SessionCommandDispatcher(RadioModel* radioModel, QObject* parent)
    : QObject(parent)
    , m_radioModel(radioModel)
{
    if (radioModel) {
        connect(radioModel->pureSignalFacade(), &PureSignalSessionFacade::actionResult,
                this, [this](quint32 id, Ps3ActionPhase phase, const QString& reason,
                             QVariantMap values) {
            const auto found = m_pureSignalCommands.constFind(id);
            if (found == m_pureSignalCommands.cend()) {
                return;
            }
            const PendingPureSignalCommand command = *found;
            QString state;
            switch (phase) {
            case Ps3ActionPhase::Accepted: state = "accepted"; break;
            case Ps3ActionPhase::Pending: state = "pending"; break;
            case Ps3ActionPhase::Completed: state = "completed"; break;
            case Ps3ActionPhase::Failed: state = "failed"; break;
            }
            if (phase == Ps3ActionPhase::Completed || phase == Ps3ActionPhase::Failed) {
                m_pureSignalCommands.remove(id);
            }
            values.insert("phase", state);
            emit commandResultReady(SessionMessages::commandResult(command.verb, command.commandId,
                phase != Ps3ActionPhase::Failed, reason, {"pureSignal"},
                dspCommandValues(values).value_or(QList<MirrorUpdate>{})));
        });
    }
}

void SessionCommandDispatcher::setSessionOwner(const QString& owner)
{
    m_pureSignalCommands.clear();
    if (m_radioModel && !m_sessionOwner.isEmpty()) {
        m_radioModel->dspAssets()->cancelOwner(m_sessionOwner);
        for (SliceModel* slice : m_radioModel->slices()) {
            // Diagnostic modes are operator actions. A new session always
            // starts on normal audio and never replays a prior test signal.
            m_radioModel->setNnrDiagnosticMode(slice->sliceIndex(), 0, 1);
        }
    }
    m_sessionOwner = owner;
}

void SessionCommandDispatcher::dispatch(const SessionMessage& invoke)
{
    if (invoke.kind != SessionMessageKind::CommandInvoke) {
        // Not this class's concern -- a caller routing error, not a
        // command failure worth reporting back.
        return;
    }
    if (m_radioModel.isNull()) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("The Core has no radio ready."), {});
        return;
    }

    if (invoke.commandVerb.startsWith("ps3.")) {
        handlePureSignalAction(invoke);
        return;
    }

    if (invoke.commandVerb.startsWith("dspAssets.")) {
        const auto arguments = dspCommandValues(invoke.arguments);
        const auto result = arguments && !m_sessionOwner.isEmpty()
            ? m_radioModel->dspAssets()->execute(invoke.commandVerb, *arguments, m_sessionOwner)
            : DspAssetServiceResult{false, QStringLiteral("The Core could not read this request."), {}};
        const auto values = dspCommandValues(result.values);
        emit commandResultReady(SessionMessages::commandResult(invoke.commandVerb,
            invoke.commandId, result.accepted, result.reason, {}, values.value_or(QList<MirrorUpdate>{})));
        return;
    }
    if (invoke.commandVerb == "nnr.applyModelSelection") {
        quint32 revision = 0;
        QString reason;
        const bool shape = hasExactlyArguments(invoke.arguments, {"revision"});
        const QVariant value = shape ? invoke.arguments.first().value : QVariant();
        const bool integer = value.typeId() == QMetaType::Int || value.typeId() == QMetaType::UInt
            || value.typeId() == QMetaType::LongLong || value.typeId() == QMetaType::ULongLong;
        bool converted = false;
        const qlonglong wide = value.toLongLong(&converted);
        const bool valid = shape && integer && converted
            && invoke.arguments.first().kind == MirrorWireKind::Int64
            && wide > 0 && wide <= std::numeric_limits<quint32>::max();
        if (valid) {
            revision = static_cast<quint32>(wide);
        }
        const bool accepted = valid && m_radioModel->applyNnrModelSelection(revision, &reason);
        if (!valid) {
            reason = QStringLiteral("The model choice changed on the Core before this request arrived. Try again.");
        }
        emitResult(invoke.commandVerb, invoke.commandId, accepted, reason, {"dspAssets"});
        return;
    }

    if (invoke.commandVerb.startsWith("notch.")) {
        handleNotchAction(invoke);
        return;
    }

    if (invoke.commandVerb == "addSlice") {
        handleAddSlice(invoke);
    } else if (invoke.commandVerb == "removeSlice") {
        handleRemoveSlice(invoke);
    } else if (invoke.commandVerb == "requestSliceSampleRate") {
        handleRequestSliceSampleRate(invoke);
    } else if (invoke.commandVerb == "addSliceOnPan") {
        handleAddSliceOnPan(invoke);
    } else if (invoke.commandVerb == "setActiveSliceById") {
        handleSetActiveSliceById(invoke);
    } else if (invoke.commandVerb == "requestStreamCtunPinned") {
        handleRequestStreamCtunPinned(invoke);
    } else if (invoke.commandVerb == "requestStreamCentre") {
        handleRequestStreamCentre(invoke);
    } else if (invoke.commandVerb == "configureTgxl") {
        handleConfigureTgxl(invoke);
    } else if (invoke.commandVerb == "disconnectTgxl") {
        handleDisconnectTgxl(invoke);
    } else if (invoke.commandVerb == "setFourO3AEnabled") {
        handleSetFourO3AEnabled(invoke);
    } else if (invoke.commandVerb == "configurePgxl") {
        handleConfigurePgxl(invoke);
    } else if (invoke.commandVerb == "disconnectPgxl") {
        handleDisconnectPgxl(invoke);
    } else if (invoke.commandVerb == "setPgxlConnectionSettings") {
        handleSetPgxlConnectionSettings(invoke);
    } else if (invoke.commandVerb == "configureRfKit") {
        handleConfigureRfKit(invoke);
    } else if (invoke.commandVerb == "disconnectRfKit") {
        handleDisconnectRfKit(invoke);
    } else if (invoke.commandVerb == "setRfKitEnabled") {
        handleSetRfKitEnabled(invoke);
    } else if (invoke.commandVerb == "resetRfKitError") {
        handleResetRfKitError(invoke);
    } else if (invoke.commandVerb == "setStationTci") {
        handleSetStationTci(invoke);
    } else if (invoke.commandVerb == "setTxInterlockPolicy") {
        handleSetTxInterlockPolicy(invoke);
    } else if (invoke.commandVerb == "setPgxlPowerCap") {
        handleSetPgxlPowerCap(invoke);
    } else if (invoke.commandVerb == "clearAccessoryFaults") {
        handleClearAccessoryFaults(invoke);
    } else if (invoke.commandVerb == "setPgxlName" || invoke.commandVerb == "setPgxlHardware"
               || invoke.commandVerb == "setPgxlNetwork"
               || invoke.commandVerb == "savePgxlSettings"
               || invoke.commandVerb == "readPgxlSettings"
               || invoke.commandVerb == "setTgxlName" || invoke.commandVerb == "setTgxlNetwork"
               || invoke.commandVerb == "saveTgxlSettings"
               || invoke.commandVerb == "readTgxlSettings") {
        handleAccessoryDeviceSettings(invoke);
    } else if (invoke.commandVerb == "requestIoBoardProbe") {
        handleRequestIoBoardProbe(invoke);
    } else if (invoke.commandVerb == "setAlexRxAntenna") {
        handleSetAlexRxAntenna(invoke);
    } else if (invoke.commandVerb == "setAlexBpfMode") {
        handleSetAlexBpfMode(invoke);
    } else if (invoke.commandVerb == "nnr.setDiagnostics" || invoke.commandVerb == "nnr.resetTuning"
               || invoke.commandVerb == "nnr.tryAgain") {
        handleNnrAction(invoke);
    } else {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("The Core does not know this request. Updating the Core may help."), {});
    }
}

void SessionCommandDispatcher::handlePureSignalAction(const SessionMessage& invoke)
{
    PureSignalSessionFacade* facade = m_radioModel->pureSignalFacade();
    const auto arguments = dspCommandValues(invoke.arguments);
    if (m_sessionOwner.isEmpty() || invoke.commandId == 0 || !arguments) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("The Core could not use this PureSignal request."), {});
        return;
    }
    if (invoke.commandVerb == "ps3.subscribeDisplay") {
        if (arguments->size() != 1 || !arguments->contains("enabled")
            || arguments->value("enabled").typeId() != QMetaType::Bool) {
            emitResult(invoke.commandVerb, invoke.commandId, false,
                       QStringLiteral("The Core could not read the PureSignal display request."), {});
            return;
        }
        const bool enabled = arguments->value("enabled").toBool();
        QString refusal;
        const QPointer<SessionCommandDispatcher> self(this);
        const QPointer<PureSignalSessionFacade> currentFacade(facade);
        const QString owner = m_sessionOwner;
        const Ps3DisplayAdmissionHandler admission = m_ps3DisplayAdmission;
        if (admission && !admission(enabled, &refusal)) {
            if (self && owner == m_sessionOwner) {
                emitResult(invoke.commandVerb, invoke.commandId, false, refusal, {});
            }
            return;
        }
        if (!self || !currentFacade || owner != m_sessionOwner) { return; }
        facade->setRemoteAmpViewSubscribed(enabled);
        if (!self || owner != m_sessionOwner) { return; }
        emitResult(invoke.commandVerb, invoke.commandId, true, {}, {"pureSignal"});
        return;
    }
    const auto action = PureSignalSessionFacade::actionForVerb(invoke.commandVerb);
    if (!action) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("The Core does not know this PureSignal action."), {});
        return;
    }
    // Read the request before deciding on it: a request that is not one
    // this action takes is refused as unreadable, not with the transmit
    // gate's reason below, so the app learns what it sent was wrong.
    if (!PureSignalSessionFacade::argumentsFit(*action, *arguments)) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("The Core could not read this PureSignal request."), {});
        return;
    }
    const bool stop = *action == Ps3Action::OffReset
        || (*action == Ps3Action::SetTwoTone && arguments->size() == 1
            && arguments->value("enabled").typeId() == QMetaType::Bool
            && !arguments->value("enabled").toBool());
    // The session currently advertises txPermitted=false. Keep the same hard
    // gate here even when a client bypasses its disabled controls. R4 owns
    // replacing this gate with negotiated, station-authorized transmit.
    if (!stop && *action != Ps3Action::SaveCorrection) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("PureSignal cannot be run from a remote window yet."), {});
        return;
    }
    for (const PendingPureSignalCommand& command : std::as_const(m_pureSignalCommands)) {
        if (command.commandId == invoke.commandId) {
            emitResult(invoke.commandVerb, invoke.commandId, false,
                       QStringLiteral("This PureSignal request is already in progress."), {});
            return;
        }
    }
    const quint32 id = facade->requestAction(*action, *arguments);
    if (!id) {
        emitResult(invoke.commandVerb, invoke.commandId, false, facade->lastActionError(), {});
        return;
    }
    m_pureSignalCommands.insert(id, {invoke.commandId, invoke.commandVerb});
    emit commandResultReady(SessionMessages::commandResult(invoke.commandVerb, invoke.commandId,
        true, {}, {}, {{0, "phase", MirrorWireKind::Utf8, QStringLiteral("accepted")}}));
}

void SessionCommandDispatcher::handleNnrAction(const SessionMessage& invoke)
{
    const bool diagnostics = invoke.commandVerb == "nnr.setDiagnostics";
    const bool shapeValid = diagnostics
        ? hasExactlyArguments(invoke.arguments, {"sliceId", "testMode", "outputMode"})
        : hasExactlyArguments(invoke.arguments, {"sliceId"});
    int sliceId = -1;
    int testMode = 0;
    int outputMode = 1;
    bool typesValid = shapeValid && findIntArgument(invoke.arguments, "sliceId", &sliceId) == ArgumentStatus::Ok;
    for (const auto& argument : invoke.arguments) {
        typesValid = typesValid && argument.kind == MirrorWireKind::Int64;
    }
    if (diagnostics) {
        typesValid = typesValid && findIntArgument(invoke.arguments, "testMode", &testMode) == ArgumentStatus::Ok
            && findIntArgument(invoke.arguments, "outputMode", &outputMode) == ArgumentStatus::Ok;
    }
    SliceModel* slice = typesValid ? m_radioModel->sliceById(sliceId) : nullptr;
    if (!slice) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("The Core could not apply this NNR change to that receiver."), {});
        return;
    }
    QString reason;
    bool accepted = false;
    if (diagnostics) {
        accepted = m_radioModel->setNnrDiagnosticMode(sliceId, testMode, outputMode, &reason);
    } else if (invoke.commandVerb == "nnr.tryAgain") {
        // R-R3-40: the operator asks for the saved NNR choice back. The
        // Core's RadioModel clears the runtime limit synchronously.
        slice->requestNnrRetry();
        accepted = slice->nnrLimit() == static_cast<int>(NnrLimit::None);
        if (!accepted) {
            reason = QStringLiteral("Noise reduction could not try again.");
        }
    } else {
        slice->resetNnrTuning();
        reason = slice->nnrLastError();
        accepted = reason.isEmpty();
    }
    emitResult(invoke.commandVerb, invoke.commandId, accepted, reason,
               accepted ? QList<QByteArray>{ObjectRegistry::keyForSlice(sliceId)} : QList<QByteArray>{});
}

void SessionCommandDispatcher::handleNotchAction(const SessionMessage& invoke)
{
    // Exact shapes, exact wire kinds: a remote window's notch edit either
    // names one notch (or one receiver, for an add) in the expected form or
    // changes nothing.
    const QByteArray& verb = invoke.commandVerb;
    const auto kindIs = [&invoke](const QByteArray& name, MirrorWireKind kind) {
        return hasWireKind(invoke.arguments, name, kind);
    };
    int id = -1;
    int sliceId = -1;
    double centreHz = 0.0;
    double widthHz = 0.0;
    bool active = false;
    bool valid = false;
    if (verb == "notch.add") {
        valid = hasExactlyArguments(invoke.arguments, {"sliceId", "centreHz", "widthHz"})
            && kindIs("sliceId", MirrorWireKind::Int64)
            && findIntArgument(invoke.arguments, "sliceId", &sliceId) == ArgumentStatus::Ok
            && kindIs("centreHz", MirrorWireKind::Float64)
            && kindIs("widthHz", MirrorWireKind::Float64)
            && findFiniteDoubleArgument(invoke.arguments, "centreHz", &centreHz)
            && findFiniteDoubleArgument(invoke.arguments, "widthHz", &widthHz);
    } else if (verb == "notch.move") {
        valid = hasExactlyArguments(invoke.arguments, {"id", "centreHz", "widthHz"})
            && kindIs("id", MirrorWireKind::Int64)
            && findIntArgument(invoke.arguments, "id", &id) == ArgumentStatus::Ok
            && kindIs("centreHz", MirrorWireKind::Float64)
            && kindIs("widthHz", MirrorWireKind::Float64)
            && findFiniteDoubleArgument(invoke.arguments, "centreHz", &centreHz)
            && findFiniteDoubleArgument(invoke.arguments, "widthHz", &widthHz);
    } else if (verb == "notch.setActive") {
        QVariant raw;
        valid = hasExactlyArguments(invoke.arguments, {"id", "active"})
            && kindIs("id", MirrorWireKind::Int64)
            && findIntArgument(invoke.arguments, "id", &id) == ArgumentStatus::Ok
            && kindIs("active", MirrorWireKind::Bool)
            && findArgument(invoke.arguments, "active", &raw)
            && raw.typeId() == QMetaType::Bool;
        active = raw.toBool();
    } else if (verb == "notch.delete") {
        valid = hasExactlyArguments(invoke.arguments, {"id"})
            && kindIs("id", MirrorWireKind::Int64)
            && findIntArgument(invoke.arguments, "id", &id) == ArgumentStatus::Ok;
    } else {
        emitResult(verb, invoke.commandId, false,
                   QStringLiteral("The Core does not know this request. Updating the Core may help."), {});
        return;
    }
    if (!valid) {
        emitResult(verb, invoke.commandId, false,
                   QStringLiteral("This notch change is not one this Core understands."), {});
        return;
    }

    QString reason;
    bool accepted = false;
    int addedId = -1;
    if (verb == "notch.add") {
        accepted = m_radioModel->addNotchFromStation(sliceId, centreHz, widthHz, &addedId, &reason);
    } else if (verb == "notch.move") {
        accepted = m_radioModel->moveNotchFromStation(id, centreHz, widthHz, &reason);
    } else if (verb == "notch.setActive") {
        accepted = m_radioModel->setNotchActiveFromStation(id, active, &reason);
    } else {
        accepted = m_radioModel->deleteNotchFromStation(id, &reason);
    }

    QList<MirrorUpdate> values;
    if (accepted) {
        // The list revision after this change, so the window can hold its
        // own view of the edit until the mirror has caught up with it.
        values.append({0, "revision", MirrorWireKind::Int64,
                       static_cast<qlonglong>(m_radioModel->notchListRevision())});
        if (verb == "notch.add") {
            values.append({0, "id", MirrorWireKind::Int64, static_cast<qlonglong>(addedId)});
        }
    }
    emit commandResultReady(SessionMessages::commandResult(
        verb, invoke.commandId, accepted, accepted ? QString() : reason,
        accepted ? QList<QByteArray>{"notches"} : QList<QByteArray>{}, values));
}

void SessionCommandDispatcher::emitResult(const QByteArray& verb, quint32 commandId,
                                          bool accepted, const QString& reason,
                                          const QList<QByteArray>& affectedKeys)
{
    emit commandResultReady(
        SessionMessages::commandResult(verb, commandId, accepted, reason, affectedKeys));
}

// ── addSlice ─────────────────────────────────────────────────────────────

void SessionCommandDispatcher::handleAddSlice(const SessionMessage& invoke)
{
    QVariant panIdArg;
    if (!findArgument(invoke.arguments, "initialPanId", &panIdArg)) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("The Core could not read this request."), {});
        return;
    }

    // RadioModel::addSlice() (RadioModel.cpp) can reject a placement after
    // partial construction, rolling back and returning -1 -- but only
    // after already emitting sliceAddRejected with the real, human-
    // readable reason (design addendum: "Rejected creation is first-class
    // ... The command result carries the reason"). A temporary connection
    // captures it; the call is synchronous, so the emission (if any)
    // happens before addSlice() returns and before this connection is torn
    // down. The by-reference capture below is correct only under the
    // class-level same-thread invariant: it relies on the connected signal
    // firing synchronously, inside this call, before `rejectionReason`
    // goes out of scope. A future thread split that made this connection
    // cross-thread would auto-queue it and turn this into a dangling read.
    QString rejectionReason;
    const QMetaObject::Connection conn = connect(
        m_radioModel, &RadioModel::sliceAddRejected, this,
        [&rejectionReason](const QString& reason) { rejectionReason = reason; });
    const int id = m_radioModel->addSlice(panIdArg.toString());
    QObject::disconnect(conn);

    if (id < 0) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   rejectionReason.isEmpty()
                       ? QStringLiteral("The Core could not add another receiver.")
                       : rejectionReason,
                   {});
        return;
    }
    emitResult(invoke.commandVerb, invoke.commandId, true, QString(),
               { ObjectRegistry::keyForSlice(id) });
}

// ── removeSlice ──────────────────────────────────────────────────────────

void SessionCommandDispatcher::handleRemoveSlice(const SessionMessage& invoke)
{
    int sliceId = 0;
    switch (findIntArgument(invoke.arguments, "sliceId", &sliceId)) {
    case ArgumentStatus::Missing:
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("The Core could not read this request."), {});
        return;
    case ArgumentStatus::NotRepresentable:
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   notRepresentableReason(), {});
        return;
    case ArgumentStatus::Ok:
        break;
    }

    if (m_radioModel->sliceById(sliceId) == nullptr) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("That receiver is no longer on the Core."), {});
        return;
    }
    if (m_radioModel->slices().size() <= 1) {
        // RadioModel::removeSlice() (RadioModel.cpp) silently no-ops rather
        // than remove the last remaining slice -- no signal marks this
        // rejection (there is nothing wrong with the request itself, only
        // with the station's state), so it has to be caught here, before
        // the call, or the result would wrongly claim success.
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("The last receiver cannot be removed."), {});
        return;
    }

    const QByteArray key = ObjectRegistry::keyForSlice(sliceId);
    m_radioModel->removeSlice(sliceId);
    emitResult(invoke.commandVerb, invoke.commandId, true, QString(), { key });
}

// ── addSliceOnPan ────────────────────────────────────────────────────────

void SessionCommandDispatcher::handleAddSliceOnPan(const SessionMessage& invoke)
{
    QVariant panIdArg;
    if (!findArgument(invoke.arguments, "panId", &panIdArg)) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("The Core could not read this request."), {});
        return;
    }

    // addSliceOnPan() returns void (RadioModel.h), unlike addSlice(), so
    // the outcome has to be read off the two signals its own cap check and
    // its addSlice() delegate can each produce: sliceAdded(id) on success,
    // sliceAddRejected(reason) either from the cap check itself or from
    // addSlice()'s own allocator rollback. Both connections are torn down
    // immediately after the synchronous call returns. As in handleAddSlice
    // above, the by-reference captures below depend on the class-level
    // same-thread invariant -- a cross-thread connection would auto-queue
    // and read `newId`/`rejectionReason` after they are gone.
    int newId = -1;
    QString rejectionReason;
    const QMetaObject::Connection addedConn = connect(
        m_radioModel, &RadioModel::sliceAdded, this,
        [&newId](int id) { newId = id; });
    const QMetaObject::Connection rejectedConn = connect(
        m_radioModel, &RadioModel::sliceAddRejected, this,
        [&rejectionReason](const QString& reason) { rejectionReason = reason; });

    m_radioModel->addSliceOnPan(panIdArg.toString());

    QObject::disconnect(addedConn);
    QObject::disconnect(rejectedConn);

    if (newId < 0) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   rejectionReason.isEmpty()
                       ? QStringLiteral("The Core could not add another receiver.")
                       : rejectionReason,
                   {});
        return;
    }
    emitResult(invoke.commandVerb, invoke.commandId, true, QString(),
               { ObjectRegistry::keyForSlice(newId) });
}

// ── requestSliceSampleRate ───────────────────────────────────────────────

void SessionCommandDispatcher::handleRequestSliceSampleRate(const SessionMessage& invoke)
{
    int sliceId = 0;
    int rateHz = 0;
    const ArgumentStatus sliceIdStatus =
        findIntArgument(invoke.arguments, "sliceId", &sliceId);
    const ArgumentStatus rateHzStatus = findIntArgument(invoke.arguments, "rateHz", &rateHz);
    // One combined message for the missing case, as before -- naming both
    // is what tells a peer this verb needs the pair. The out-of-range
    // case names the offending argument specifically, because there the
    // peer sent something and needs to know WHICH one was refused.
    if (sliceIdStatus == ArgumentStatus::Missing || rateHzStatus == ArgumentStatus::Missing) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("The Core could not read this request."), {});
        return;
    }
    if (sliceIdStatus == ArgumentStatus::NotRepresentable) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   notRepresentableReason(), {});
        return;
    }
    if (rateHzStatus == ArgumentStatus::NotRepresentable) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   notRepresentableReason(), {});
        return;
    }

    if (m_radioModel->sliceById(sliceId) == nullptr) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("That receiver is no longer on the Core."), {});
        return;
    }

    // Deferred to a LATER turn of RadioModel's own event loop -- see the
    // class comment for why this is the one verb that cannot run inline.
    // self/radioModel are QPointer copies so the daemon tearing either one
    // down before this queued call runs leaves nothing dangling; the
    // by-value capture of verb/commandId/sliceId/rateHz keeps this
    // self-contained once dispatch() (and `invoke`, which is a reference to
    // a caller-owned temporary) has returned.
    const QByteArray verb = invoke.commandVerb;
    const quint32 commandId = invoke.commandId;
    const QPointer<SessionCommandDispatcher> self(this);
    const QPointer<RadioModel> radioModel(m_radioModel);

    QMetaObject::invokeMethod(
        m_radioModel,
        [self, radioModel, verb, commandId, sliceId, rateHz]() {
            if (self.isNull() || radioModel.isNull()) {
                return;
            }

            // Actual scope, not requested scope (see the class comment):
            // snapshot every slice's rate before, act, then report
            // whichever slices' rates actually differ afterward. Correct
            // regardless of WHY more than one moved -- co-hosted slices
            // sharing requestSliceSampleRate's target DDC stream, or (on a
            // Protocol 1 board) the request escalating all the way to
            // RadioModel::setSampleRateLive's radio-wide sequence.
            QHash<int, int> before;
            for (SliceModel* slice : radioModel->slices()) {
                if (slice != nullptr) {
                    before.insert(slice->sliceIndex(), slice->sampleRateHz());
                }
            }

            // setStreamSampleRate (RadioModel.cpp) can refuse the retune
            // outright -- every slice stays exactly where it was -- and
            // reports that through sliceRetuneRejected with a human-
            // readable reason, the same pattern handleAddSlice's
            // sliceAddRejected capture uses. requestSliceSampleRate() is
            // synchronous, so the emission (if any) lands before it
            // returns and before this connection is torn down. This
            // by-reference capture is ALREADY running inside a queued
            // lambda on RadioModel's thread (see this method's own
            // deferral above), so it depends on the same same-thread
            // invariant as handleAddSlice's capture, one level further in.
            QString rejectionReason;
            const QMetaObject::Connection conn = connect(
                radioModel, &RadioModel::sliceRetuneRejected, self,
                [&rejectionReason](int, const QString& reason) { rejectionReason = reason; });
            radioModel->requestSliceSampleRate(sliceId, rateHz);
            QObject::disconnect(conn);

            if (!rejectionReason.isEmpty()) {
                self->emitResult(verb, commandId, false, rejectionReason, {});
                return;
            }

            QList<QByteArray> affected;
            for (SliceModel* slice : radioModel->slices()) {
                if (slice == nullptr) {
                    continue;
                }
                const int id = slice->sliceIndex();
                if (before.value(id, -1) != slice->sampleRateHz()) {
                    affected.append(ObjectRegistry::keyForSlice(id));
                }
            }
            // An empty `affected` here is a legitimate no-op (the slice was
            // not yet bound to a stream, or was already at this rate --
            // requestSliceSampleRate()'s own idempotent-check paths,
            // RadioModel.cpp), not a failure: RadioModel raised no
            // rejection, so nothing here second-guesses that.
            self->emitResult(verb, commandId, true, QString(), affected);
        },
        Qt::QueuedConnection);
}

// ── setActiveSliceById ───────────────────────────────────────────────────

// Fix round 1 review finding (Important 1): before this verb existed, a
// remote operator's active-slice click had no path to the daemon at all.
// SliceModel::active carries no WRITE (SliceModel.h), so StateMirror::
// applyInbound() always fell through to applyMirroredValue(), which
// refused it outright (SliceModel.cpp) -- both inbound doors were shut.
// This verb is the one that was missing.
//
// Mechanically identical to handleRemoveSlice above: resolve the id,
// check the RadioModel entry point's own success/failure signal (here a
// bool return rather than an existence probe plus a separate guard), and
// report the resulting scope. RadioModel::setActiveSliceById() already
// does its own "no such slice" check internally (sliceById(sliceId) ==
// nullptr) and returns false, so this handler does not duplicate it --
// unlike handleRemoveSlice, which has a SECOND rejection RadioModel
// signals nothing about (the last-slice guard) and therefore does have to
// duplicate.
void SessionCommandDispatcher::handleSetActiveSliceById(const SessionMessage& invoke)
{
    int sliceId = 0;
    switch (findIntArgument(invoke.arguments, "sliceId", &sliceId)) {
    case ArgumentStatus::Missing:
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("The Core could not read this request."), {});
        return;
    case ArgumentStatus::NotRepresentable:
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   notRepresentableReason(), {});
        return;
    case ArgumentStatus::Ok:
        break;
    }

    // Captured BEFORE the call: this is the slice that is ABOUT to stop
    // being active, and setActiveSliceById() (RadioModel.cpp) reassigns
    // m_activeSlice as its very first side effect on success, so reading
    // this afterward would already show the NEW slice.
    SliceModel* const previouslyActive = m_radioModel->activeSlice();
    const int previouslyActiveId =
        (previouslyActive != nullptr) ? previouslyActive->sliceIndex() : -1;

    if (!m_radioModel->setActiveSliceById(sliceId)) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("That receiver is no longer on the Core."), {});
        return;
    }

    // The newly-active key, plus the previously-active one when it is a
    // DIFFERENT slice -- requesting the slice that was already active is a
    // legitimate no-op accept (RadioModel::setActiveSlice()'s own
    // change-guard makes it one), and reporting the same key twice would
    // not describe two objects moving, just one.
    QList<QByteArray> affected{ ObjectRegistry::keyForSlice(sliceId) };
    if (previouslyActiveId >= 0 && previouslyActiveId != sliceId) {
        affected.append(ObjectRegistry::keyForSlice(previouslyActiveId));
    }
    emitResult(invoke.commandVerb, invoke.commandId, true, QString(), affected);
}

void SessionCommandDispatcher::handleRequestStreamCtunPinned(const SessionMessage& invoke)
{
    int sliceId = 0;
    QVariant pinned;
    if (!hasExactlyArguments(invoke.arguments, { "sliceId", "pinned" })
        || findIntArgument(invoke.arguments, "sliceId", &sliceId) != ArgumentStatus::Ok
        || !findArgument(invoke.arguments, "pinned", &pinned)
        || pinned.typeId() != QMetaType::Bool) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("The Core could not read this request."), {});
        return;
    }
    if (!m_radioModel->requestStreamCtunPinned(sliceId, pinned.toBool())) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("That receiver is not running on the Core."), {});
        return;
    }
    QList<QByteArray> affected;
    if (SliceModel* slice = m_radioModel->sliceById(sliceId)) {
        for (int id : m_radioModel->slicesOnStream(slice->streamIndex())) {
            affected.append(ObjectRegistry::keyForSlice(id));
        }
    }
    emitResult(invoke.commandVerb, invoke.commandId, true, QString(), affected);
}

void SessionCommandDispatcher::handleRequestStreamCentre(const SessionMessage& invoke)
{
    int sliceId = 0;
    double centreHz = 0.0;
    if (!hasExactlyArguments(invoke.arguments, { "sliceId", "centreHz" })
        || findIntArgument(invoke.arguments, "sliceId", &sliceId) != ArgumentStatus::Ok
        || !findFiniteDoubleArgument(invoke.arguments, "centreHz", &centreHz)) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("The Core could not read this request."), {});
        return;
    }
    SliceModel* const slice = m_radioModel->sliceById(sliceId);
    const int stream = slice ? slice->streamIndex() : -1;
    if (!m_radioModel->requestStreamCentre(sliceId, centreHz)) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("C-Tune cannot centre there while other receivers share this spectrum."), {});
        return;
    }
    QList<QByteArray> affected;
    for (int id : m_radioModel->slicesOnStream(stream)) {
        affected.append(ObjectRegistry::keyForSlice(id));
    }
    emitResult(invoke.commandVerb, invoke.commandId, true, QString(), affected);
}

void SessionCommandDispatcher::handleConfigureTgxl(const SessionMessage& invoke)
{
    QString host;
    int port = 0;
    if (!hasExactlyArguments(invoke.arguments, { "host", "port" })
        || !findUtf8Argument(invoke.arguments, "host", &host)
        || !hasWireKind(invoke.arguments, "port", MirrorWireKind::Int64)
        || findIntArgument(invoke.arguments, "port", &port) != ArgumentStatus::Ok
        || port < 1 || port > 65535) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("The Core could not read this request."), {});
        return;
    }

    QString reason;
    if (!m_radioModel->configureTgxlForStation(host, static_cast<quint16>(port), &reason)) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   reason.isEmpty() ? QStringLiteral("The Core did not set up the Tuner Genius XL.") : reason,
                   {});
        return;
    }
    emitResult(invoke.commandVerb, invoke.commandId, true, QString(), {});
}

void SessionCommandDispatcher::handleDisconnectTgxl(const SessionMessage& invoke)
{
    if (!hasExactlyArguments(invoke.arguments, {})) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("The Core could not read this request."), {});
        return;
    }

    QString reason;
    if (!m_radioModel->disconnectTgxlForStation(&reason)) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   reason.isEmpty() ? QStringLiteral("The Core did not disconnect the Tuner Genius XL.") : reason,
                   {});
        return;
    }
    emitResult(invoke.commandVerb, invoke.commandId, true, QString(), {});
}

// R-R3-47 / R-R3-22: the Power Genius's address, as configureTgxl's.
// Accepted means saved and identifying; `amplifier`.connectionPhase says
// whether it connected.
void SessionCommandDispatcher::handleConfigurePgxl(const SessionMessage& invoke)
{
    QString host;
    int port = 0;
    if (!hasExactlyArguments(invoke.arguments, { "host", "port" })
        || !findUtf8Argument(invoke.arguments, "host", &host)
        || !hasWireKind(invoke.arguments, "port", MirrorWireKind::Int64)
        || findIntArgument(invoke.arguments, "port", &port) != ArgumentStatus::Ok
        || port < 1 || port > 65535) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("The Core could not read this request."), {});
        return;
    }
    QString reason;
    if (!m_radioModel->configurePgxlForStation(host, static_cast<quint16>(port), &reason)) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   reason.isEmpty() ? QStringLiteral("The Core did not set up the Power Genius.") : reason,
                   {});
        return;
    }
    emitResult(invoke.commandVerb, invoke.commandId, true, QString(), {});
}

// R-R3-47 / R-R3-22: the RF-Kit's configure rule, as the Power Genius's.
void SessionCommandDispatcher::handleConfigureRfKit(const SessionMessage& invoke)
{
    QString host;
    int port = 0;
    if (!hasExactlyArguments(invoke.arguments, { "host", "port" })
        || !findUtf8Argument(invoke.arguments, "host", &host)
        || !hasWireKind(invoke.arguments, "port", MirrorWireKind::Int64)
        || findIntArgument(invoke.arguments, "port", &port) != ArgumentStatus::Ok
        || port < 1 || port > 65535) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("The Core could not read this request."), {});
        return;
    }
    QString reason;
    if (!m_radioModel->configureRfKitForStation(host, static_cast<quint16>(port), &reason)) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   reason.isEmpty() ? QStringLiteral("The Core did not set up the RF-Kit amplifier.")
                                    : reason,
                   {});
        return;
    }
    emitResult(invoke.commandVerb, invoke.commandId, true, QString(), {});
}

void SessionCommandDispatcher::handleDisconnectRfKit(const SessionMessage& invoke)
{
    if (!hasExactlyArguments(invoke.arguments, {})) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("The disconnect request for the RF-Kit amplifier was not "
                                  "understood."), {});
        return;
    }
    QString reason;
    if (!m_radioModel->disconnectRfKitForStation(&reason)) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   reason.isEmpty() ? QStringLiteral("The Core did not disconnect the RF-Kit "
                                                     "amplifier.")
                                    : reason,
                   {});
        return;
    }
    emitResult(invoke.commandVerb, invoke.commandId, true, QString(), {});
}

// I4 (R-R3-47, remoteRfKitControlVersion 3): the local page's Reset amp
// error, sent by the Core to its admitted amp.
void SessionCommandDispatcher::handleResetRfKitError(const SessionMessage& invoke)
{
    if (!hasExactlyArguments(invoke.arguments, {})) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("The request to reset the RF-Kit amplifier's error was not "
                                  "understood."), {});
        return;
    }
    QString reason;
    if (!m_radioModel->resetRfKitErrorForStation(&reason)) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   reason.isEmpty() ? QStringLiteral("The Core did not reset the RF-Kit "
                                                     "amplifier's error.")
                                    : reason,
                   {});
        return;
    }
    emitResult(invoke.commandVerb, invoke.commandId, true, QString(), {});
}

void SessionCommandDispatcher::handleSetRfKitEnabled(const SessionMessage& invoke)
{
    QVariant enabled;
    if (!hasExactlyArguments(invoke.arguments, { "enabled" })
        || !findArgument(invoke.arguments, "enabled", &enabled)
        || enabled.typeId() != QMetaType::Bool) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("The request to turn the RF-Kit amplifier on or off was not "
                                  "understood."), {});
        return;
    }
    QString reason;
    if (!m_radioModel->setRfKitEnabledForStation(enabled.toBool(), &reason)) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   reason.isEmpty() ? QStringLiteral("The Core did not change its RF-Kit "
                                                     "amplifier switch.")
                                    : reason, {});
        return;
    }
    emitResult(invoke.commandVerb, invoke.commandId, true, QString(), {});
}

// R-R3-48: the one TCI switch and port, kept by the Core.
void SessionCommandDispatcher::handleSetStationTci(const SessionMessage& invoke)
{
    QVariant enabled;
    int port = 0;
    if (!hasExactlyArguments(invoke.arguments, { "enabled", "port" })
        || !findArgument(invoke.arguments, "enabled", &enabled)
        || enabled.typeId() != QMetaType::Bool
        || !hasWireKind(invoke.arguments, "port", MirrorWireKind::Int64)
        || findIntArgument(invoke.arguments, "port", &port) != ArgumentStatus::Ok) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("The request to turn the Core's TCI server on or off was "
                                  "not understood."), {});
        return;
    }
    QString reason;
    if (!m_radioModel->setStationTciForStation(enabled.toBool(), port, &reason)) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   reason.isEmpty() ? QStringLiteral("The Core did not change its TCI server.")
                                    : reason, {});
        return;
    }
    emitResult(invoke.commandVerb, invoke.commandId, true, QString(), {});
}

// R-R3-47 / R-R3-22 (accessoryDataVersion 1): the transmit interlock policy.
// The Core applies it and mirrors it back on `accessoryData`; its
// enforcement stays on the Core and a change keys nothing.
void SessionCommandDispatcher::handleSetTxInterlockPolicy(const SessionMessage& invoke)
{
    int mode = -1;
    int graceMs = -1;
    QVariant gate;
    double gateMax = 0.0;
    if (!hasExactlyArguments(invoke.arguments,
                             { "mode", "graceMs", "swrGateEnabled", "swrGateMax" })
        || !hasWireKind(invoke.arguments, "mode", MirrorWireKind::Int64)
        || findIntArgument(invoke.arguments, "mode", &mode) != ArgumentStatus::Ok
        || !hasWireKind(invoke.arguments, "graceMs", MirrorWireKind::Int64)
        || findIntArgument(invoke.arguments, "graceMs", &graceMs) != ArgumentStatus::Ok
        || !findArgument(invoke.arguments, "swrGateEnabled", &gate)
        || gate.typeId() != QMetaType::Bool
        || !hasWireKind(invoke.arguments, "swrGateMax", MirrorWireKind::Float64)
        || !findFiniteDoubleArgument(invoke.arguments, "swrGateMax", &gateMax)) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("The request to change the transmit interlock was not "
                                  "understood."), {});
        return;
    }
    QString reason;
    if (!m_radioModel->setTxInterlockPolicyForStation(mode, graceMs, gate.toBool(), gateMax,
                                                      &reason)) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   reason.isEmpty() ? QStringLiteral("The Core did not change the transmit "
                                                     "interlock.")
                                    : reason, {});
        return;
    }
    emitResult(invoke.commandVerb, invoke.commandId, true, QString(), {});
}

// R-R3-47 / R-R3-22: the Power Genius output limit that raises the alert.
void SessionCommandDispatcher::handleSetPgxlPowerCap(const SessionMessage& invoke)
{
    QVariant enabled;
    int watts = 0;
    if (!hasExactlyArguments(invoke.arguments, { "enabled", "watts" })
        || !findArgument(invoke.arguments, "enabled", &enabled)
        || enabled.typeId() != QMetaType::Bool
        || !hasWireKind(invoke.arguments, "watts", MirrorWireKind::Int64)
        || findIntArgument(invoke.arguments, "watts", &watts) != ArgumentStatus::Ok) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("The request to change the Power Genius output limit was "
                                  "not understood."), {});
        return;
    }
    QString reason;
    if (!m_radioModel->setPgxlPowerCapForStation(enabled.toBool(), watts, &reason)) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   reason.isEmpty() ? QStringLiteral("The Core did not change the Power Genius "
                                                     "output limit.")
                                    : reason, {});
        return;
    }
    emitResult(invoke.commandVerb, invoke.commandId, true, QString(), {});
}

// R-R3-47 / R-R3-22: one device's fault history, cleared on the Core.
void SessionCommandDispatcher::handleClearAccessoryFaults(const SessionMessage& invoke)
{
    QString device;
    if (!hasExactlyArguments(invoke.arguments, { "device" })
        || !findUtf8Argument(invoke.arguments, "device", &device)) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("The request to clear the fault history was not understood."),
                   {});
        return;
    }
    QString reason;
    if (!m_radioModel->clearAccessoryFaultsForStation(device, &reason)) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   reason.isEmpty() ? QStringLiteral("The Core did not clear the fault history.")
                                    : reason, {});
        return;
    }
    emitResult(invoke.commandVerb, invoke.commandId, true, QString(), {});
}

// R-R3-47 / R-R3-22 (remotePgxlControlVersion 3, remoteTgxlControlVersion
// 1): the amp's and the tuner's own settings. The Core sends each to the
// device as the local Advanced page's own command (StationDeviceSettings);
// accepted means it left for the device, and the device's answer comes
// back on `accessorySettings`. None keys a transmitter or operates the amp.
void SessionCommandDispatcher::handleAccessoryDeviceSettings(const SessionMessage& invoke)
{
    const QByteArray& verb = invoke.commandVerb;
    const bool pgxl = verb.contains("Pgxl");
    const QString device = pgxl ? QStringLiteral("Power Genius") : QStringLiteral("Tuner Genius");
    const auto notUnderstood = [&](const QString& what) {
        emitResult(verb, invoke.commandId, false,
                   QStringLiteral("The request to %1 was not understood.").arg(what), {});
    };
    QString reason;
    bool sent = false;
    if (verb == "setPgxlName" || verb == "setTgxlName") {
        QString name;
        if (!hasExactlyArguments(invoke.arguments, { "name" })
            || !findUtf8Argument(invoke.arguments, "name", &name)) {
            notUnderstood(QStringLiteral("rename the %1").arg(device));
            return;
        }
        sent = pgxl ? m_radioModel->setPgxlNameForStation(name, &reason)
                    : m_radioModel->setTgxlNameForStation(name, &reason);
    } else if (verb == "setPgxlHardware") {
        // Exactly one of biasMode (utf8), fanMode (utf8), ledIntensity (i64).
        const MirrorUpdate* only = invoke.arguments.size() == 1 ? &invoke.arguments.first()
                                                                : nullptr;
        QVariant value;
        bool shape = false;
        if (only && (only->name == "biasMode" || only->name == "fanMode")) {
            QString text;
            shape = findUtf8Argument(invoke.arguments, only->name, &text);
            value = text;
        } else if (only && only->name == "ledIntensity") {
            int led = 0;
            shape = hasWireKind(invoke.arguments, "ledIntensity", MirrorWireKind::Int64)
                && findIntArgument(invoke.arguments, "ledIntensity", &led) == ArgumentStatus::Ok;
            value = led;
        }
        if (!shape) {
            notUnderstood(QStringLiteral("change the Power Genius hardware"));
            return;
        }
        sent = m_radioModel->setPgxlHardwareForStation(QString::fromUtf8(only->name), value,
                                                       &reason);
    } else if (verb == "setPgxlNetwork" || verb == "setTgxlNetwork") {
        QVariant dhcp;
        QString address;
        QString netmask;
        QString gateway;
        if (!hasExactlyArguments(invoke.arguments, { "dhcp", "address", "netmask", "gateway" })
            || !findArgument(invoke.arguments, "dhcp", &dhcp)
            || dhcp.typeId() != QMetaType::Bool
            || !findUtf8Argument(invoke.arguments, "address", &address)
            || !findUtf8Argument(invoke.arguments, "netmask", &netmask)
            || !findUtf8Argument(invoke.arguments, "gateway", &gateway)) {
            notUnderstood(QStringLiteral("change the %1 network settings").arg(device));
            return;
        }
        sent = pgxl ? m_radioModel->setPgxlNetworkForStation(dhcp.toBool(), address, netmask,
                                                             gateway, &reason)
                    : m_radioModel->setTgxlNetworkForStation(dhcp.toBool(), address, netmask,
                                                             gateway, &reason);
    } else if (verb == "savePgxlSettings" || verb == "saveTgxlSettings") {
        if (!hasExactlyArguments(invoke.arguments, {})) {
            notUnderstood(QStringLiteral("save and restart the %1").arg(device));
            return;
        }
        sent = pgxl ? m_radioModel->savePgxlSettingsForStation(&reason)
                    : m_radioModel->saveTgxlSettingsForStation(&reason);
    } else {
        if (!hasExactlyArguments(invoke.arguments, {})) {
            notUnderstood(QStringLiteral("read the %1 settings").arg(device));
            return;
        }
        sent = pgxl ? m_radioModel->readPgxlSettingsForStation(&reason)
                    : m_radioModel->readTgxlSettingsForStation(&reason);
    }
    if (!sent) {
        emitResult(verb, invoke.commandId, false,
                   reason.isEmpty()
                       ? QStringLiteral("The Core did not send the request to the %1.").arg(device)
                       : reason,
                   {});
        return;
    }
    emitResult(verb, invoke.commandId, true, QString(), {});
}

void SessionCommandDispatcher::handleDisconnectPgxl(const SessionMessage& invoke)
{
    if (!hasExactlyArguments(invoke.arguments, {})) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("The Core could not read this request."), {});
        return;
    }
    QString reason;
    if (!m_radioModel->disconnectPgxlForStation(&reason)) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   reason.isEmpty() ? QStringLiteral("The Core did not disconnect the Power Genius.") : reason,
                   {});
        return;
    }
    emitResult(invoke.commandVerb, invoke.commandId, true, QString(), {});
}

// R-R3-47: autoReconnect (bool), keepaliveSec (i64, 1 to 3600), pingSec
// (i64, 0 to 3600), saved together on the Core and applied at once.
void SessionCommandDispatcher::handleSetPgxlConnectionSettings(const SessionMessage& invoke)
{
    QVariant autoReconnect;
    int keepaliveSec = 0;
    int pingSec = 0;
    if (!hasExactlyArguments(invoke.arguments, { "autoReconnect", "keepaliveSec", "pingSec" })
        || !findArgument(invoke.arguments, "autoReconnect", &autoReconnect)
        || autoReconnect.typeId() != QMetaType::Bool
        || !hasWireKind(invoke.arguments, "keepaliveSec", MirrorWireKind::Int64)
        || !hasWireKind(invoke.arguments, "pingSec", MirrorWireKind::Int64)
        || findIntArgument(invoke.arguments, "keepaliveSec", &keepaliveSec) != ArgumentStatus::Ok
        || findIntArgument(invoke.arguments, "pingSec", &pingSec) != ArgumentStatus::Ok) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("The Core could not read this request."), {});
        return;
    }
    QString reason;
    if (!m_radioModel->setPgxlConnectionSettingsForStation(autoReconnect.toBool(), keepaliveSec,
                                                           pingSec, &reason)) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   reason.isEmpty() ? QStringLiteral("The Core did not change the Power Genius connection settings.") : reason,
                   {});
        return;
    }
    emitResult(invoke.commandVerb, invoke.commandId, true, QString(), {});
}

// R-R3-46 (radioHardwareVersion 2): Setup's Probe button on the HL2 I/O
// board tab, for a remote window. The Core makes the same call a local
// window's button makes (RadioModel::requestIoBoardProbe).
void SessionCommandDispatcher::handleRequestIoBoardProbe(const SessionMessage& invoke)
{
    if (!invoke.arguments.isEmpty()) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("The Core could not read this request."), {});
        return;
    }
    const RadioModel::IoBoardProbeOutcome outcome = m_radioModel->requestIoBoardProbe();
    emitResult(invoke.commandVerb, invoke.commandId, outcome.sent, outcome.reason, {});
}

// R-R3-46 fix wave (radioHardwareVersion 3): one band's receive antenna
// from a remote window. The window used to send the whole 14-band list, so
// a list built before a change the Core made to another band (the VFO
// flag, another window) put that band back. The Core changes only the band
// named, through its own AlexAntennaFacade and AlexController, and every
// window follows the `alexAntennas` delta.
void SessionCommandDispatcher::handleSetAlexRxAntenna(const SessionMessage& invoke)
{
    int band = 0;
    int antenna = 0;
    QVariant rxOnly;
    if (!hasExactlyArguments(invoke.arguments, { "band", "antenna", "rxOnly" })
        || findIntArgument(invoke.arguments, "band", &band) != ArgumentStatus::Ok
        || findIntArgument(invoke.arguments, "antenna", &antenna) != ArgumentStatus::Ok
        || !findArgument(invoke.arguments, "rxOnly", &rxOnly)
        || rxOnly.typeId() != QMetaType::Bool) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("The Core could not read this request."), {});
        return;
    }
    AlexAntennaFacade* const alex = m_radioModel->alexAntennaFacade();
    if (alex == nullptr || !alex->isBound()) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("The Core has no antenna settings ready."), {});
        return;
    }
    if (band < 0 || band >= AlexAntennaFacade::kBandCount) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("The Core keeps antennas for 14 bands."), {});
        return;
    }
    const bool receiveOnly = rxOnly.toBool();
    const QString reason = receiveOnly ? alex->setRxOnlyAntForBand(Band(band), antenna)
                                       : alex->setRxAntForBand(Band(band), antenna);
    emitResult(invoke.commandVerb, invoke.commandId, reason.isEmpty(), reason, {});
}

// R-R3-46 / R-R3-21 (radioHardwareVersion 4): one receive filter chain's
// filter policy from a remote window's filter policy dialog. The Core makes
// the call its own dialog's Apply makes (AlexController::setBpfMode, through
// its AlexAntennaFacade), saves it for its radio and publishes the chain's
// state (rxFilter<N>Mode) to every window. The policy picks the receive
// band-pass filter only, so a receive-only Core applies it too.
void SessionCommandDispatcher::handleSetAlexBpfMode(const SessionMessage& invoke)
{
    int chain = 0;
    int mode = 0;
    if (!hasExactlyArguments(invoke.arguments, { "chain", "mode" })
        || findIntArgument(invoke.arguments, "chain", &chain) != ArgumentStatus::Ok
        || findIntArgument(invoke.arguments, "mode", &mode) != ArgumentStatus::Ok) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("The Core could not read this request."), {});
        return;
    }
    AlexAntennaFacade* const alex = m_radioModel->alexAntennaFacade();
    if (alex == nullptr || !alex->isBound()) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("The Core has no filter settings ready."), {});
        return;
    }
    const QString reason = alex->setBpfModeForChain(chain, mode);
    emitResult(invoke.commandVerb, invoke.commandId, reason.isEmpty(), reason, {});
}

void SessionCommandDispatcher::handleSetFourO3AEnabled(const SessionMessage& invoke)
{
    QVariant enabled;
    if (!hasExactlyArguments(invoke.arguments, { "enabled" })
        || !findArgument(invoke.arguments, "enabled", &enabled)
        || enabled.typeId() != QMetaType::Bool) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("The Core could not read this request."), {});
        return;
    }

    QString reason;
    if (!m_radioModel->setFourO3AEnabledForStation(enabled.toBool(), &reason)) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   reason.isEmpty() ? QStringLiteral("The Core did not turn 4O3A on or off.") : reason, {});
        return;
    }
    emitResult(invoke.commandVerb, invoke.commandId, true, QString(), {});
}

} // namespace NereusSDR
