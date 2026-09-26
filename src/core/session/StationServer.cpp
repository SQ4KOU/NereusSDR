// =================================================================
// src/core/session/StationServer.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R2 Task 18.
// See StationServer.h for the connect sequence, the one-session topology
// decision, the threading invariant, and the heartbeat's detection model.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-08  J.J. Boyd / KG4VCF  Remote daemon R2 Task 18: the daemon
//                                    half of the wss session. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
//   2026-08-09  J.J. Boyd / KG4VCF  Whole-branch review, Important 4:
//                                    relay a settings removal as an
//                                    absence frame, not as a value of "".
//                                    AI-assisted transformation via
//                                    Anthropic Claude Code.
//   2026-08-09  J.J. Boyd / KG4VCF  Whole-branch review, Minor 4:
//                                    handlePropertyWrite() answers one
//                                    inbound frame with one snapshot and
//                                    one outbound frame, not N of each.
//                                    AI-assisted transformation via
//                                    Anthropic Claude Code.
//   2026-09-23  J.J. Boyd / KG4VCF  R-R3-40: station telemetry carries
//                                    each receiver's processing load only
//                                    for a peer that negotiated minor 11
//                                    and stationTelemetryVersion 3.
//                                    AI-assisted implementation via
//                                    Anthropic Claude Code.
//                                    Later the same day: SliceModel
//                                    nnrLimit and the nnr.tryAgain command
//                                    only for a peer at minor 11, including
//                                    a write's side effects; nnrStatus
//                                    carries the step-back reason in the
//                                    Core wording to every peer.
//                                    Final review fix wave: a message is
//                                    checked read-only for nnrLimit or
//                                    nnrStatus first, and copied only
//                                    when one is present.
//   2026-09-23  J.J. Boyd / KG4VCF  R-R3-08/37/40: a computed display
//                                    budget (adaptation on, nothing
//                                    configured) is in force only for a
//                                    peer at minor 11; older peers keep
//                                    legacy mode. AI-assisted
//                                    implementation via Anthropic Claude
//                                    Code.
//   2026-09-23  J.J. Boyd / KG4VCF  R-R3-21 / R-R3-09: the `notches`
//                                    object, notchControlVersion 1, and the
//                                    plain refusal of raw Notch* writes.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-23  J.J. Boyd / KG4VCF  R-R3-45: headphonesMixVersion 1 with
//                                    media. AI-assisted via Anthropic Claude
//                                    Code.
//   2026-09-23  J.J. Boyd / KG4VCF  R-R3-46: hpsdrModel, radioProtocol and
//                                    radioAddress for a peer at minor 11.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-23  J.J. Boyd / KG4VCF  R-R3-46 / R-R3-11: the `stepAtt`
//                                    object and radioHardwareVersion 1 for a
//                                    peer at minor 11, its settle reasons,
//                                    and the plain refusal of raw attenuator
//                                    and preamp settings. AI-assisted via
//                                    Anthropic Claude Code.
//   2026-09-23  J.J. Boyd / KG4VCF  R-R3-46: radioHardwareVersion 2 with
//                                    the `alexAntennas` object, the
//                                    hardware apply step after a settings
//                                    write, and hardware/<mac>/ writes
//                                    refused for any radio but the
//                                    connected one. AI-assisted via
//                                    Anthropic Claude Code.
//   2026-09-23  J.J. Boyd / KG4VCF  R-R3-47 / R-R3-22: the read-only
//                                    `amplifier` and `rfkit` objects with
//                                    remotePgxlControlVersion 1 and
//                                    remoteRfKitControlVersion 1 for a peer
//                                    at minor 11, and the plain refusal of a
//                                    write to either. AI-assisted via
//                                    Anthropic Claude Code.
//   2026-09-23  J.J. Boyd / KG4VCF  R-R3-46 fix wave: a receive-only Core
//                                    refuses raw writes and removes of the
//                                    transmit-side hardware keys
//                                    (isTransmitHardwareKey). AI-assisted
//                                    via Anthropic Claude Code.
//   2026-09-23  J.J. Boyd / KG4VCF  R-R3-46 fix wave: radioHardwareVersion
//                                    3, the read-only `ioBoard` object.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  R-R3-46 follow-up: the Alex TX
//                                    low-pass table and master TX switches
//                                    and OC hot switching are transmit keys.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  R-R3-47 / R-R3-22 / R-R3-25:
//                                    remotePgxlControlVersion 2 with the
//                                    configurePgxl, disconnectPgxl and
//                                    setPgxlConnectionSettings verbs (minor
//                                    11); a receive-only Core refuses the
//                                    tuner's isOperate, isBypass and antennaA
//                                    writes and the amplifier's operate with
//                                    one plain reason. AI-assisted via
//                                    Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  R-R3-47 / R-R3-48 / R-R3-25:
//                                    remoteRfKitControlVersion 2 with the
//                                    configureRfKit, disconnectRfKit and
//                                    setRfKitEnabled verbs; a raw
//                                    rfKitEnabled write refused in plain
//                                    words; stationTciVersion 1 with the
//                                    read-only `stationTci` object and the
//                                    setStationTci verb. AI-assisted via
//                                    Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  R-IOS-01: a write to any property
//                                    MirrorPolicy marks outbound is refused
//                                    in plain words before anything is
//                                    applied. AI-assisted via Anthropic
//                                    Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  R-IOS-01: a write to a slice's
//                                    `active` is refused with
//                                    SliceModel::activeWriteReason().
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  R-R3-47 / R-R3-22: accessoryDataVersion
//                                    1 with the read-only `accessoryData`
//                                    object and the setTxInterlockPolicy,
//                                    setPgxlPowerCap and clearAccessoryFaults
//                                    verbs; a window's accessory setting
//                                    write reaches the Core's live objects.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Task 4 (R-IOS-01): the
//                                    hello advertises the station's link
//                                    majors and features; a client major
//                                    outside that list is refused in plain
//                                    words; the peer's declared features;
//                                    TLS 1.2 or later set explicitly.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-24 - iPhone app Task 4b (R-IOS-01, R-R3-21): the reasons this
//                file sends an app are in operator words. J.J. Boyd
//                (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-09-24 - iPhone app Part A fix wave (R-IOS-01): the hello's
//                `major` is the oldest supported major, so a client built
//                before `majors` is still served. J.J. Boyd (KG4VCF),
//                AI-assisted via Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  R-R3-47 / R-R3-22:
//                                    remotePgxlControlVersion 3 and
//                                    remoteTgxlControlVersion 1 with the
//                                    read-only `accessorySettings` object
//                                    and the amp's and tuner's own settings
//                                    verbs. AI-assisted via Anthropic Claude
//                                    Code.
//   2026-09-24  J.J. Boyd / KG4VCF  R-R3-47: remoteRfKitControlVersion 3
//                                    with the resetRfKitError verb.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  R-R3-46 / R-R3-21: radioHardwareVersion
//                                    4 with the setAlexBpfMode verb, the
//                                    filter policy from a remote window.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  R-R3-49: a window's Network Watchdog
//                                    change is applied to the Core's radio
//                                    when it arrives. AI-assisted via
//                                    Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  Lane B takes integration (R-IOS-01,
//                                    R-R3-21): the accessory settings and
//                                    RF-Kit reset refusals in plain words.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Task 12 (R-IOS-08,
//                                    R-IOS-02, R-IOS-01): the Core's own
//                                    identity key and paired devices; the
//                                    hello carries the identity, its
//                                    certificate binding and a per-
//                                    connection challenge and declares
//                                    deviceAuth 1; device sign-in with its
//                                    own rate limits; a window signing in
//                                    with the token enrols its device key;
//                                    a Core without a token refuses token
//                                    sign-in; an end code on every
//                                    permanent end; stationIdentityVersion
//                                    1. AI-assisted via Anthropic Claude
//                                    Code.
//   2026-09-24  J.J. Boyd / KG4VCF  R3 completion carry, review I1
//                                    (R-R3-21, R-R3-38, R-IOS-01): the
//                                    takeover and version reasons come from
//                                    SessionEndReasons, which the app parses.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  R-R3-49 / R-R3-47: remoteTgxlControlVersion 2
//                                    (the Tuner Genius's antenna, operate
//                                    and bypass verbs).
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  R-R3-49 fix wave: remoteTgxlControlVersion 3
//                                    (setTgxlOperate on puts the tuner in
//                                    OPERATE whole).
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Task 13 (R-IOS-08): the
//                                    `devices` object and deviceAdminVersion
//                                    1 for a device at minor 11 whose hello
//                                    declares deviceAuth; its four verbs; a
//                                    removed device's connection and every
//                                    token connection after the token is
//                                    retired end at once (the requester's
//                                    own just after its result); the plain
//                                    refusal of a raw StationLabel remove.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Task 14 (R-IOS-08, D37):
//                                    the pairing window; pair.start in LAN
//                                    mode (unclaimed, allowed, on a
//                                    directly connected network) and code
//                                    mode (SPAKE2+EE, the code taken once
//                                    and burned on anything but success);
//                                    features.pairing and pairingVersion;
//                                    pairing.open and pairing.close; the
//                                    code on the console and only to a
//                                    connection signed in with a paired
//                                    device's key. AI-assisted via
//                                    Anthropic Claude Code.
//   2026-09-24: Part C fix wave: the optional device shortName in
//               auth.request, stored with the device. J.J. Boyd (KG4VCF), with
//               AI-assisted implementation via Anthropic Claude Code.
//   2026-09-24: Part C fix wave: the pairing code is never printed
//               to standard output (the journal on a packaged Core). J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic Claude
//               Code.
//   2026-09-24: Part C fix wave (security Minors R1-M1, M2, M4,
//               M5): the confirm-step recheck, the step 1 point check, the
//               per-address handshake cap and 0600 on load. J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic Claude
//               Code.
//   2026-09-24: Part C follow-up (R-IOS-08): IPv6 peers counted per /64
//               in the handshake cap; pairing.open refused to a token
//               session. J.J. Boyd (KG4VCF), with AI-assisted
//               implementation via Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Task 19 (R-IOS-06): the
//                                    `catalog` object for a peer at minor
//                                    11, stationCatalogVersion 1 last in
//                                    the minor-11 block, and a refresh
//                                    when a filter preset or the CW pitch
//                                    changes in the Core's settings.
//                                    AI-assisted via Anthropic Claude
//                                    Code.
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Task 20 (R-IOS-27):
//                                    displayExtrasVersion 1 last in the
//                                    minor-11 block. AI-assisted via
//                                    Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  R-R3-49 (parity Task 1):
//                                    transmitSettingsVersion 1. A
//                                    receive-only Core takes a transmit
//                                    setting (a `transmit` write outside
//                                    the keying set, a DspOptions*Tx key)
//                                    while the radio is off the air and
//                                    refuses it while it is on the air.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  R-R3-49 (parity Task 2):
//                                    transmitSettingsVersion 2: the TX
//                                    and Phone/CW applets' settings on
//                                    `transmit`, a value outside a
//                                    setting's range refused in plain
//                                    words, and setTunePowerForTxBand.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-25  J.J. Boyd / KG4VCF  R-R3-49 (parity Task 3):
//                                    transmitSettingsVersion 3: the radio
//                                    microphone settings and the Core's TX
//                                    profiles on `transmit`, the Line In
//                                    gain range, the txProfile verbs and
//                                    rade.resetVocoder.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-25  J.J. Boyd / KG4VCF  R-R3-49 (parity Task 4):
//                                    transmitSettingsVersion 4: the TX EQ,
//                                    CFC, phase rotator, CESSB, leveler and
//                                    ALC settings on `transmit`.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-25  J.J. Boyd / KG4VCF  R-R3-49 (parity Task 5):
//                                    transmitSettingsVersion 5: Setup >
//                                    Transmit > Power, DEXP/VOX and Test >
//                                    Two-Tone IMD on `transmit`, ATT on TX
//                                    and Force ATT on `stepAtt`; the SWR
//                                    protection and External TX Inhibit keys
//                                    taken off the air, the SWR protection
//                                    applied to the Core's controller at
//                                    once.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-25  J.J. Boyd / KG4VCF  R-R3-49 / R-R3-32 (parity Task 6):
//                                    transmitSettingsVersion 6: Setup > PA's
//                                    pa/ and paCalibration/ keys taken off
//                                    the air and applied at once;
//                                    stationTelemetryVersion 4: the radio's
//                                    PA readings and link quality for a
//                                    peer at minor 11.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-25  J.J. Boyd / KG4VCF  Receiver and transmit gaps plan,
//                                    Task 13: a window's External TX
//                                    Inhibit change reaches the Core's gate.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-25  J.J. Boyd / KG4VCF  Core WebSocket opening (R-IOS-01,
//                                    R-R3-26): StationOpeningGate listens in
//                                    front of the QWebSocketServer, so every
//                                    Host form opens and a request the Core
//                                    cannot read gets 400.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-25: iPhone app Task 71 (R-IOS-02): up to four device
//               sessions (DeviceSessionRegistry), admission and the same-device
//               rule in place of preemption, the away state and its grace timer,
//               session.leave, sessionHolder 1, sessionHolderVersion and
//               `connectedDevices`, per-peer capabilities and mirror sends,
//               command results to the asking session, media and telemetry to
//               one session until Task 76. J.J. Boyd (KG4VCF), with AI-assisted
//               implementation via Anthropic Claude Code.
//   2026-09-25: iPhone app Task 73 (R-IOS-02): slice ownership. Each view
//               receives its own slices and a SliceMarker for every other
//               (none to an older window); a write or verb naming another
//               device's slice is refused with its owner named; admission
//               returns held slices, restores saved ones, adopts unowned
//               ones for the first device alone and gives a slice-less
//               device one; the end of its 180 s, leaving and revocation
//               close, save or hold its slices; listeningOn. J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
//   2026-09-25: iPhone app Task 74 (R-IOS-02, R-IOS-30): the receiver
//               commands and a retune leaving a shared receiver go through
//               the confirm step (StationReceivers.cpp); the property write
//               body is applyPropertyWrite; an older window with no slice
//               at admission is refused; notices after snapshot.complete.
//               J.J. Boyd (KG4VCF), with AI-assisted implementation via
//               Anthropic Claude Code.
//   2026-09-25  J.J. Boyd / KG4VCF  R-IOS-27, R-IOS-06: bandSelectVersion
//                                    1, last in the minor-11 block, and
//                                    slice.selectBand refused below it.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-25  J.J. Boyd / KG4VCF  R-IOS-27, R-IOS-06: notchControlVersion
//                                    2 (notch.addAtSlice).
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-25  J.J. Boyd / KG4VCF  R-IOS-27, R-IOS-06:
//                                    displayExtrasVersion 2 (clarity-retune).
//                                    AI-assisted via Anthropic Claude Code.
// =================================================================

#include "core/session/StationServer.h"

#include "core/AppSettings.h"
#include "core/BoardCapabilities.h"
#include "core/HardwareProfile.h"
#include "core/WdspEngine.h"
#include "core/dsp/NnrSettings.h"
#include "core/security/CertificateStore.h"
#include "core/security/DeviceAuthenticator.h"
#include "core/security/DeviceStore.h"
#include "core/security/PairingCode.h"
#include "core/security/PairingWindow.h"
#include "core/security/SpakeExchange.h"
#include "core/security/StationIdentity.h"
#include "core/security/TokenStore.h"
#include "core/session/MirrorPolicy.h"
#include "core/session/MirrorSchema.h"
#include "core/session/MirrorView.h"
#include "core/session/ObjectRegistry.h"
#include "core/session/SessionCommandDispatcher.h"
#include "core/session/SliceMarker.h"
#include "core/SliceOwnership.h"
#include "core/session/media/DisplayBudgetSplit.h"
#include "core/session/media/DisplayLoadGovernor.h"
#include "core/session/ConfirmStep.h"
#include "core/session/ConnectedDevicesFacade.h"
#include "core/session/DeviceSessionRegistry.h"
#include "core/session/SessionEndReasons.h"
#include "core/session/SessionTransport.h"
#include "core/session/StationOpeningGate.h"
#include "core/session/StateMirror.h"
#include "core/session/StationCatalog.h"
#include "core/session/StationDevicesFacade.h"
#include "core/settings/SettingsProxyServer.h"
#include "core/settings/SettingsScope.h"
#include "models/NotchModel.h"
#include "models/PanadapterModel.h"
#include "models/PureSignalSettings.h"
#include "core/dsp/DspAssetService.h"
#include "PureSignalSessionFacade.h"
#include "core/PureSignal.h"
#include "core/StepAttenuatorFacade.h"
#include "core/accessories/AlexAntennaFacade.h"
#include "core/IoBoardHl2Facade.h"
#include <QScopeGuard>
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"
#include "models/AmplifierModel.h"
#include "models/RfKitModel.h"
#include "models/StationTciModel.h"
#include "models/AccessoryDataModel.h"
#include "models/AccessorySettingsModel.h"
#include "models/TunerModel.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QSet>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QThread>
#include <QTimer>
#include <QWebSocket>
#include <QWebSocketServer>

#include <algorithm>
#include <cstdio>

namespace NereusSDR {

namespace {
Q_LOGGING_CATEGORY(lcStation, "nereus.station")

// The wire identities the five singleton mirrored models are watched
// under. Slices use ObjectRegistry::keyForSlice() instead, which is
// already shared with the daemon's own lifecycle tracking.
constexpr const char* kRadioKey = "radio";
constexpr const char* kTransmitKey = "transmit";
constexpr const char* kTunerKey = "tuner";

// R-R3-40: the Core's step-back reason in a slice's nnrStatus, reworded for
// a remote window, which is not the computer that could not keep up.
QVariant coreWordedNnrStatus(const QVariant& value)
{
    const QString text = value.toString();
    for (const NnrLimit limit : {NnrLimit::StandardOnly, NnrLimit::Off}) {
        if (text == nnrLimitExplanation(static_cast<int>(limit))) {
            return nnrLimitExplanation(static_cast<int>(limit), NnrLimitSite::CoreComputer);
        }
    }
    return value;
}

const QByteArray& nnrLimitName()
{
    static const QByteArray name("nnrLimit");
    return name;
}

const QByteArray& nnrStatusName()
{
    static const QByteArray name("nnrStatus");
    return name;
}

// R-R3-40: whether fitNnrLimitToPeer would change `message` for this peer.
// Read-only, so the common case (no NNR field) neither copies nor detaches.
bool needsNnrFit(const SessionMessage& message, quint16 agreedMinor)
{
    const bool older = agreedMinor < kNnrLimitSessionProtocolMinor;
    const auto touches = [&](const QList<MirrorUpdate>& updates) {
        return std::any_of(updates.cbegin(), updates.cend(), [&](const MirrorUpdate& u) {
            return u.name == nnrStatusName() || (older && u.name == nnrLimitName());
        });
    };
    switch (message.kind) {
    case SessionMessageKind::Schema:
        return older && message.className == "SliceModel"
            && std::any_of(message.fields.cbegin(), message.fields.cend(),
                           [](const SessionSchemaField& field) {
                               return field.name == nnrLimitName();
                           });
    case SessionMessageKind::ObjectCreate:
        return message.className == "SliceModel" && touches(message.updates);
    case SessionMessageKind::Delta:
        return message.objectKey.startsWith("slice:") && touches(message.updates);
    default:
        return false;
    }
}

// Only removing nnrLimit can empty a slice delta; one left empty is not sent.
bool worthSendingAfterNnrFit(const SessionMessage& message)
{
    return message.kind != SessionMessageKind::Delta || !message.objectKey.startsWith("slice:")
        || !message.updates.isEmpty();
}

// R-R3-40: fits a mirror message to one peer. Every peer reads the Core's
// step-back reason (nnrStatus) in the Core wording; a peer below minor 11
// also loses SliceModel's nnrLimit, which it would otherwise log as a schema
// skew. Returns false when nothing is left worth sending.
bool fitNnrLimitToPeer(SessionMessage& message, quint16 agreedMinor)
{
    if (!needsNnrFit(message, agreedMinor)) {
        return worthSendingAfterNnrFit(message);
    }
    const bool older = agreedMinor < kNnrLimitSessionProtocolMinor;
    if (message.kind == SessionMessageKind::Schema) {
        message.fields.removeIf([](const SessionSchemaField& field) {
            return field.name == nnrLimitName();
        });
        return true;
    }
    if (older) {
        message.updates.removeIf([](const MirrorUpdate& update) {
            return update.name == nnrLimitName();
        });
    }
    for (MirrorUpdate& update : message.updates) {
        if (update.name == nnrStatusName()) {
            update.value = coreWordedNnrStatus(update.value);
        }
    }
    return worthSendingAfterNnrFit(message);
}

// R-R3-46: the Core's step attenuator and preamp, mirrored for a peer that
// negotiated kRadioIdentitySessionProtocolMinor. An older peer never sees
// the object, so the burst it receives is exactly the one it was built for.
constexpr const char* kStepAttKey = "stepAtt";

bool isStepAttMessage(const SessionMessage& message)
{
    return message.objectKey == kStepAttKey
        || (message.kind == SessionMessageKind::Schema
            && message.className == "StepAttenuatorFacade");
}

// R-R3-46 (radioHardwareVersion 2): the Core's Alex antenna settings, for a
// peer at kRadioIdentitySessionProtocolMinor, from a Core that also applies
// Hardware Config writes (scheduleRemoteHardwareApply).
constexpr const char* kAlexAntennasKey = "alexAntennas";

bool isAlexAntennasMessage(const SessionMessage& message)
{
    return message.objectKey == kAlexAntennasKey
        || (message.kind == SessionMessageKind::Schema
            && message.className == "AlexAntennaFacade");
}

// R-R3-46 (radioHardwareVersion 3): the Core's HL2 I/O board, read-only,
// for a peer at kRadioIdentitySessionProtocolMinor.
constexpr const char* kIoBoardKey = "ioBoard";

bool isIoBoardMessage(const SessionMessage& message)
{
    return message.objectKey == kIoBoardKey
        || (message.kind == SessionMessageKind::Schema
            && message.className == "IoBoardHl2Facade");
}

// R-R3-47 / R-R3-22: the Core's Power Genius XL and RF-Kit RF2K-S status,
// read-only, for a peer at kRadioIdentitySessionProtocolMinor on a Core
// that owns its accessories. An older peer never sees either object.
constexpr const char* kAmplifierKey = "amplifier";
constexpr const char* kRfKitKey = "rfkit";

bool isAmplifierMessage(const SessionMessage& message)
{
    return message.objectKey == kAmplifierKey
        || (message.kind == SessionMessageKind::Schema
            && message.className == "AmplifierModel");
}

bool isRfKitMessage(const SessionMessage& message)
{
    return message.objectKey == kRfKitKey
        || (message.kind == SessionMessageKind::Schema
            && message.className == "RfKitModel");
}

// R-R3-48 (stationTciVersion 1): the Core's station TCI server, read-only,
// for a peer at kRadioIdentitySessionProtocolMinor on a Core that runs one.
constexpr const char* kStationTciKey = "stationTci";

bool isStationTciMessage(const SessionMessage& message)
{
    return message.objectKey == kStationTciKey
        || (message.kind == SessionMessageKind::Schema
            && message.className == "StationTciModel");
}

// R-R3-47 / R-R3-22 (accessoryDataVersion 1): the Core's accessory records
// and settings, read-only, for a peer at kRadioIdentitySessionProtocolMinor
// on a Core that owns its accessories.
constexpr const char* kAccessoryDataKey = "accessoryData";

bool isAccessoryDataMessage(const SessionMessage& message)
{
    return message.objectKey == kAccessoryDataKey
        || (message.kind == SessionMessageKind::Schema
            && message.className == "AccessoryDataModel");
}

// R-R3-47 / R-R3-22 (remotePgxlControlVersion 3, remoteTgxlControlVersion
// 1): the amp's and tuner's own settings, read-only, for a peer at
// kRadioIdentitySessionProtocolMinor on a Core that owns its accessories.
constexpr const char* kAccessorySettingsKey = "accessorySettings";

// iPhone app Task 13 (R-IOS-08, deviceAdminVersion 1): the Core's paired
// devices and label, read-only, for a device at
// kRadioIdentitySessionProtocolMinor whose hello declares deviceAuth. Any
// other peer never sees the object, so its burst is today's.
constexpr const char* kDevicesKey = "devices";

bool isDevicesMessage(const SessionMessage& message)
{
    return message.objectKey == kDevicesKey
        || (message.kind == SessionMessageKind::Schema
            && message.className == "StationDevicesFacade");
}

bool isDeviceAdminVerb(const QByteArray& verb)
{
    return verb == "devices.revoke" || verb == "station.rename"
        || verb == "station.acknowledgeKeyBackup" || verb == "station.retireToken";
}

// iPhone app Task 14 (R-IOS-08, pairingVersion 1): the pairing window's
// verbs, for the same peers as the device administration verbs.
bool isPairingVerb(const QByteArray& verb)
{
    return verb == "pairing.open" || verb == "pairing.close";
}

// The `devices` property that carries the pairing code.
constexpr const char* kPairingCodeProperty = "pairingCode";

// The settings the devices object reads: its label and key backup.
bool isDevicesSettingsKey(const QString& key)
{
    return key.compare(QLatin1String("StationCallsign"), Qt::CaseInsensitive) == 0
        || isCoreOwnedIdentitySettingsKey(key);
}

// iPhone app Task 19 (R-IOS-06, stationCatalogVersion 1): the values the
// Core owns and an app draws its controls from, read-only, for a peer at
// kRadioIdentitySessionProtocolMinor. An older peer never sees the object.
constexpr const char* kCatalogKey = "catalog";

bool isCatalogMessage(const SessionMessage& message)
{
    return message.objectKey == kCatalogKey
        || (message.kind == SessionMessageKind::Schema
            && message.className == "StationCatalog");
}

// The settings the catalogue reads: the filter presets (FilterPresetStore's
// filters/<mode>/<slot>/...) and the CW pitch the CW presets follow.
bool isCatalogSettingsKey(const QString& key)
{
    return key.startsWith(QLatin1String("filters/"))
        || key.compare(QLatin1String("CWPitch"), Qt::CaseInsensitive) == 0;
}

// iPhone app Task 71 (rulings 4.4 and 4.8): a full Core's refusal, and the
// end of a device's older connection when it connects again.
constexpr const char* kCoreFullReason = "The Core already has four devices connected.";
// The several-devices design, 15.2 (the operator-confirmed wording): an
// older window cannot answer the fifth-device question, so it is told to
// update or try again.
constexpr const char* kOlderWindowCoreFullReason =
    "The Core is full. Update NereusSDR to take a device's place, or try again later.";
constexpr const char* kSameDeviceReason = "This device connected again.";
// iPhone app Task 71 (ruling 4.12): session.leave's close; no session.end
// carries it (the accepted result already told the device).
constexpr const char* kLeftReason = "This device left the Core.";

// The link's maxDeviceSessions is the registry's.
static_assert(StationServer::kMaxDeviceSessions == DeviceSessionRegistry::kMaxDeviceSessions);

// iPhone app Task 71 (sessionHolderVersion 1): who is on the Core, for a
// view at kRadioIdentitySessionProtocolMinor whose hello declared
// sessionHolder 1 with deviceAuth 1. Any other peer never sees the object.
constexpr const char* kConnectedDevicesKey = "connectedDevices";

bool isConnectedDevicesMessage(const SessionMessage& message)
{
    return message.objectKey == kConnectedDevicesKey
        || (message.kind == SessionMessageKind::Schema
            && message.className == "ConnectedDevicesFacade");
}

// iPhone app Task 13: why a connection ends when its device is removed, and
// when the token it signed in with is retired (Task 12's pairing text).
constexpr const char* kDeviceRemovedReason = "This device was removed from the Core.";
constexpr const char* kPairingRequiredReason =
    "This Core uses paired devices. Pair this device first.";

bool isAccessorySettingsMessage(const SessionMessage& message)
{
    return message.objectKey == kAccessorySettingsKey
        || (message.kind == SessionMessageKind::Schema
            && message.className == "AccessorySettingsModel");
}

// R-R3-47 / R-R3-22: the verbs for the amp's and the tuner's own settings.
bool isPgxlDeviceSettingsVerb(const QByteArray& verb)
{
    return verb == "setPgxlName" || verb == "setPgxlHardware" || verb == "setPgxlNetwork"
        || verb == "savePgxlSettings" || verb == "readPgxlSettings";
}

bool isTgxlDeviceSettingsVerb(const QByteArray& verb)
{
    return verb == "setTgxlName" || verb == "setTgxlNetwork" || verb == "saveTgxlSettings"
        || verb == "readTgxlSettings";
}

// R-R3-49 / R-R3-47: the Tuner Genius's antenna, operate and bypass
// (remoteTgxlControlVersion 2).
bool isTgxlControlVerb(const QByteArray& verb)
{
    return verb == "setTgxlAntenna" || verb == "setTgxlOperate" || verb == "setTgxlBypass";
}

// R-R3-47: why a raw write of the RF-Kit switch is refused. A current app
// sends setRfKitEnabled; an older one only ever wrote the value.
constexpr const char* kRfKitSwitchWriteReason =
    "Update this app to turn the RF-Kit amplifier on or off on this Core.";

// The one reason a receive-only Core gives for every transmit
// configuration write it refuses: the keying `transmit` properties and the
// transmit-side hardware keys alike (R-R3-21). R-R3-49 (parity Task 1): the
// other transmit settings are taken while the radio is off the air.
constexpr const char* kReceiveOnlyTransmitReason =
    "Transmit configuration is unavailable on this receive-only Core.";

// R-R3-49 (parity Task 1): the `transmit` properties that key the radio or
// arm it to key. A receive-only Core refuses a write of any of them, on and
// off the air, whether or not it mirrors the property; every other
// `transmit` property is a setting (transmitSettingsVersion 1).
bool isTransmitKeyingProperty(const QByteArray& name)
{
    return name == "mox" || name == "tune" || name == "voxEnabled" || name == "twoToneActive";
}

// R-IOS-01: the one reason for a write to a property MirrorPolicy marks
// outbound (the station's own readings and derived values, and properties
// with a command of their own). Refused before anything is applied, so a
// model's inbound hook, which exists to apply the station's reports on a
// client, never runs on the station for a client's write.
constexpr const char* kOutboundWriteReason =
    "The Core sets this itself; it cannot be changed from here.";

// The tuner properties whose remote write reaches the tuner itself
// (TunerModel::applyMirroredValue sends operate, bypass or antenna
// commands). A receive-only Core never lets a write get there.
bool isTunerTransmitPathProperty(const QByteArray& name)
{
    return name == "isOperate" || name == "isBypass" || name == "antennaA";
}

// DSP > Options TX combos persist to DspOptions<Setting><Mode>Tx
// (DspOptionsPage::buildUI). The DspOptions prefix is Station-scoped, so
// these keys are station transmit settings; their Rx siblings are not.
bool isTransmitDspOptionsKey(const QString& key)
{
    return key.startsWith(QLatin1String("DspOptions"))
        && key.endsWith(QLatin1String("Tx"));
}

// R-R3-46 / R-R3-21: the transmit side of the Hardware Config and PA pages.
// Since the Core's hardware apply step reloads oc/, cal/ and hl2/ into its
// live controllers (RadioModel::scheduleRemoteHardwareApply), a raw write
// of one of these would reach the radio's transmit path, so a receive-only
// Core refuses them as it refuses TransmitModel writes. Covered:
//   hardware/<mac>/oc/tx/...            OC transmit pins (OcMatrix)
//   hardware/<mac>/oc/actions/...       OC pin transmit actions (OcMatrix)
//   hardware/<mac>/cal/{txDisplayOffset,paSens,paOffset}  (CalibrationController)
//   hardware/<mac>/paCalibration/...    PA forward-power table (CalibrationController)
//                                       and the Calibration tab's own copies of
//                                       its transmit fields (paCalibration/cal/...)
//   hardware/<mac>/hl2/{pttHangMs,txLatencyMs}            (Hl2OptionsModel)
//   hardware/<mac>/tx/...               TransmitModel, User Dig Out, mic profiles
//   hardware/<mac>/pa/...               PA profiles (PaProfileManager)
//   hardware/<mac>/powerByBand/..., tunePowerByBand/...   (TransmitModel)
//   any .../oc/extPa/...                the external PA group (OcOutputsHfTab)
//   any .../oc/allowHotSwitching        OC lines switching while transmitting
//   any .../alex/master/{hpfBypassOnTx,hpfBypassOnPs,disable6mLnaOnTx}
//   any .../alex/lpf/...                the Alex TX low-pass table
//                                       (AntennaAlexAlex1Tab, its own keys and
//                                       the Hardware page's copies)
bool isTransmitHardwareKey(const QString& rawKey)
{
    const QString key = rawKey.toLower();
    if (!key.startsWith(QLatin1String("hardware/"))) {
        return false;
    }
    const QStringList parts = key.split(QLatin1Char('/'));
    for (int i = 1; i + 1 < parts.size(); ++i) {
        const QString& here = parts[i];
        const QString& next = parts[i + 1];
        if (here == QLatin1String("oc")
            && (next == QLatin1String("extpa") || next == QLatin1String("allowhotswitching"))) {
            return true;
        }
        if (here == QLatin1String("alex") && next == QLatin1String("lpf")) {
            return true;
        }
        if (here == QLatin1String("alex") && next == QLatin1String("master") && i + 2 < parts.size()) {
            const QString& field = parts[i + 2];
            if (field == QLatin1String("hpfbypassontx") || field == QLatin1String("hpfbypassonps")
                || field == QLatin1String("disable6mlnaontx")) {
                return true;
            }
        }
    }
    if (parts.size() < 4) {
        return false;
    }
    const QString& area = parts[2];
    const QString& item = parts[3];
    if (area == QLatin1String("oc")) {
        return item == QLatin1String("tx") || item == QLatin1String("actions");
    }
    if (area == QLatin1String("cal")) {
        return item == QLatin1String("txdisplayoffset") || item == QLatin1String("pasens")
            || item == QLatin1String("paoffset");
    }
    if (area == QLatin1String("pacalibration")) {
        if (item != QLatin1String("cal")) {
            return true;
        }
        const QString field = parts.size() > 4 ? parts[4] : QString();
        return field == QLatin1String("txdisplayoffset") || field == QLatin1String("pasens")
            || field == QLatin1String("paoffset") || field == QLatin1String("padefaultrestored")
            || field == QLatin1String("logvoltsamps");
    }
    if (area == QLatin1String("hl2")) {
        return item == QLatin1String("ptthangms") || item == QLatin1String("txlatencyms");
    }
    return area == QLatin1String("tx") || area == QLatin1String("pa")
        || area == QLatin1String("powerbyband") || area == QLatin1String("tunepowerbyband");
}

// R-R3-49 (parity Task 5): Setup > Transmit > Power's SWR Protection and
// External TX Inhibit groups, Station keys (SettingsScope.cpp) that gate
// the Core's own transmitting. Taken while the radio is off the air.
bool isPowerPageTransmitKey(const QString& key)
{
    return RadioModel::isSwrProtectionSettingKey(key)
        || key == QLatin1String("TxInhibitMonitorEnabled")
        || key == QLatin1String("TxInhibitMonitorReversed");
}

// R-R3-46 / R-R3-49 (parity Task 6): Setup > PA's keys, the PA profiles
// (hardware/<mac>/pa/..., PaProfileManager: PA Gain's profiles, per-band
// gains, adjust matrix and max power) and the PA forward-power table
// (hardware/<mac>/paCalibration/..., CalibrationController: the Watt Meter
// page). Taken while the radio is off the air and applied at once
// (RadioModel::scheduleRemoteHardwareApply). The Calibration tab's own
// copies of its transmit fields (paCalibration/cal/...) are Hardware
// Config's, not the PA pages', and stay refused.
bool isPaPageTransmitKey(const QString& rawKey)
{
    const QStringList parts = rawKey.toLower().split(QLatin1Char('/'));
    if (parts.size() < 4 || parts[0] != QLatin1String("hardware")) {
        return false;
    }
    if (parts[2] == QLatin1String("pa")) {
        return true;
    }
    return parts[2] == QLatin1String("pacalibration") && parts[3] != QLatin1String("cal");
}

// R-R3-49 (parity Task 5): the plain refusal for a Power page key's value
// the page's own control cannot hold; empty when it can. The boxes write
// "True" or "False"; the page's udTunePowerSwrIgnore spin box holds 5 to
// 50 W (PowerPage::buildSwrProtectionGroup).
// SwrProtectionLimit keeps SettingsProxyServer's own range check.
QString powerPageKeyValueRefusal(const QString& key, const QVariant& value)
{
    if (!isPowerPageTransmitKey(key) || key == QLatin1String("SwrProtectionLimit")) {
        return {};
    }
    const QString text = value.toString();
    if (key == QLatin1String("TunePowerSwrIgnore")) {
        bool ok = false;
        const int watts = text.toInt(&ok);
        return ok && watts >= 5 && watts <= 50
            ? QString()
            : QStringLiteral("Choose a tune power to ignore from 5 to 50 W.");
    }
    return text == QLatin1String("True") || text == QLatin1String("False")
        ? QString()
        : QStringLiteral("The Core expected this box to be on or off.");
}

// R-R3-49 (parity Task 5): true when both values are the same JSON object.
// The Core writes a band map (powerByBandJson, tunePowerByBandJson) back with
// its keys in its own order, so a map it took whole reads back as the same
// object, not the same text.
bool sameJsonObject(const QVariant& a, const QVariant& b)
{
    const QJsonDocument left = QJsonDocument::fromJson(a.toString().toUtf8());
    const QJsonDocument right = QJsonDocument::fromJson(b.toString().toUtf8());
    return left.isObject() && right.isObject() && left == right;
}

// Every settings key a receive-only Core refuses as transmit configuration.
// R-R3-49 (parity Task 1): from a peer offered transmitSettingsVersion 1,
// the keys StationServer::isTransmitSettingKeyAcceptedOffAir lists are
// taken off the air instead (StationServer::receiveOnlyRefusesKey).
bool isReceiveOnlyRefusedKey(const QString& key)
{
    return isTransmitDspOptionsKey(key) || isTransmitHardwareKey(key);
}

QByteArray panKey(int index)
{
    return QByteArrayLiteral("pan:") + QByteArray::number(index);
}

QString peerNameForThisProcess()
{
    return QStringLiteral("nereusd");
}

// Each side's own AppSettings schema version, read by the key name
// AppSettings::ensureSettingsAtVersion() writes it under. Read rather than
// hardcoded: the literal lives at exactly one place today (CoreInit.cpp's
// ensureSettingsAtVersion(8) call), and duplicating it here would create a
// second copy free to drift from the migrations that actually ran.
qint32 settingsSchemaVersionOf(const AppSettings& settings)
{
    return static_cast<qint32>(
        settings.value(QStringLiteral("SettingsSchemaVersion"), QStringLiteral("0"))
            .toString()
            .toInt());
}

// Written straight to stdout with C stdio, deliberately NOT through
// qCInfo() like every other line in this class. That is a correctness fix,
// not a style preference, and it closes two separate defects.
//
// FIRST, the banner was arriving MANGLED. CoreInit::initialize() installs
// a process-wide qInstallMessageHandler whose handler passes every message
// through redactPii() before it reaches stderr or the log file, and
// redactPii's MAC rule used to be a bare six-pair hex body -- which is a
// strict prefix of the 32-pair colon-separated SHA-256 fingerprint printed
// two lines below. It matched five times over inside one fingerprint and
// replaced 25 of its 32 bytes with asterisks, while the banner still said
// the values were printed once, here. StationClient::connectToStation
// refuses to dial without a fingerprint and nereusd has no option to
// reprint one, so that left an operator with no way forward. redactPii is
// now narrowed too (CoreInit.cpp), because a fingerprint logged from
// anywhere else would otherwise still be destroyed; this function is the
// other half, not a substitute for it.
//
// SECOND, and the reason the fix is a different STREAM rather than a
// different regex: that same handler writes every message verbatim into
// ~/.config/NereusSDR/profiles/<profile>/nereussdr-<stamp>.log, kept
// indefinitely and symlinked as nereussdr.log. That is the file
// CONTRIBUTING.md tells operators to attach to a bug report. Routing the
// token through it contradicts TokenStore.h's own stated reason for
// keeping the secret out of AppSettings -- "a secret sitting in the same
// XML the operator backs up, mails to a maintainer with a bug report" --
// against a worse medium than the one that header rejects. Bypassing the
// handler entirely is what keeps the token out of the log file; no
// redaction rule could, because the token is 43 characters of base64url
// with no shape to match on.
//
// STDOUT rather than stderr. Under packaging/nereusd.service.in this
// process sets neither StandardOutput= nor StandardError=, so systemd's
// defaults put both streams in the journal and the banner reaches
// `journalctl -u nereusd` either way; the systemd case does not decide it.
// What decides it is what each stream means. This banner is the run's
// primary output, two values the operator is being asked to copy, not a
// diagnostic. stderr in this process is already owned by the Qt handler,
// so putting the banner there would interleave a copy-paste block with
// redacted diagnostic lines, and an operator debugging by hand with
// `nereusd 2> daemon-errors.log` would lose it off the terminal.
//
// The explicit fflush is load-bearing rather than hygiene: stdout is
// block-buffered whenever it is not a terminal, which is exactly the
// systemd case, so without it the banner would sit in libc's buffer until
// it filled or the daemon exited.
void writePairingBanner(const QString& banner)
{
    const QByteArray bytes = banner.toUtf8();
    std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stdout);
    std::fflush(stdout);
}
} // namespace

// iPhone app Task 14 (R-IOS-08): one connection's pairing, from its
// pair.start until the connection ends.
struct StationServer::PairingAttempt {
    bool codeMode = false;
    /// The device's key as pair.start sent it (base64url) and as SPKI DER.
    QString publicKeyText;
    QByteArray publicKeySpki;
    QString name;
    QString kind;
    /// Code mode: the code this exchange started on, the exchange, and the
    /// device's next message: its step 1, then its step 3, then its box.
    quint64 codeSerial = 0;
    std::unique_ptr<SpakeExchange> exchange;
    int expecting = 1;
    /// Waiting for the code's hash (a worker) before step 0 is sent.
    bool awaitingHash = false;
    /// The window gave this exchange the code: from here the code is
    /// paired with or burned (dropPeer burns it unless `finished`).
    bool codeTaken = false;
    bool finished = false;
};

StationServer::StationServer(RadioModel* radioModel, AppSettings& settings,
                             const QString& securityDirectory, QObject* parent,
                             const QList<quint16>& supportedMajors)
    : QObject(parent)
    , m_radioModel(radioModel)
    , m_settings(settings)
    , m_securityDirectory(securityDirectory.isEmpty() ? CertificateStore::defaultDirectory()
                                                      : securityDirectory)
    , m_supportedMajors(supportedMajors.isEmpty() ? LinkVersion::supportedMajors()
                                                  : supportedMajors)
{
    // Oldest first and each once, as the hello sends them.
    std::sort(m_supportedMajors.begin(), m_supportedMajors.end());
    m_supportedMajors.erase(std::unique(m_supportedMajors.begin(), m_supportedMajors.end()),
                            m_supportedMajors.end());
    // Every capability set this R3 server advertises is receive-only. Make
    // that a persistent property of the hardware-owning model as well, so a
    // standalone StationServer host cannot admit a TX/accessory side effect
    // through a local callback. DaemonApp installs the same policy earlier,
    // before startup; neither owner clears it when a session ends.
    if (m_radioModel) {
        m_radioModel->setReceiveOnlyStationPolicy(true);
    }

    m_certificates = std::make_unique<CertificateStore>(m_securityDirectory);
    // iPhone app Task 12: loaded when an earlier Core left one, never
    // created (TokenStore.h).
    m_tokens = std::make_unique<TokenStore>(m_securityDirectory);
    m_identity = std::make_unique<StationIdentity>(
        StationIdentity::loadOrCreate(m_securityDirectory));
    m_devices = std::make_unique<DeviceStore>(m_securityDirectory, m_tokens.get());
    m_deviceAuth = std::make_unique<DeviceAuthenticator>(*m_devices, *m_identity);
    // iPhone app Task 71 (R-IOS-02): who holds a place, and the mirrored
    // `connectedDevices` object that shows it.
    m_deviceSessions = std::make_unique<DeviceSessionRegistry>();
    // iPhone app Task 74 (R-IOS-30): the questions asked and notices kept.
    m_confirm = std::make_unique<ConfirmStep>();
    m_connectedDevices = std::make_unique<ConnectedDevicesFacade>(*m_deviceSessions, *m_devices);
    // Revoking a device frees its place at once, live or away, and forgets
    // that its time ran out (ruling 4.11). Its live connection ends in the
    // next deviceRemoved handler, with no away state: the place is already
    // free.
    connect(m_devices.get(), &DeviceStore::deviceRemoved, this, [this](const QByteArray& id) {
        m_deviceSessions->remove(id);
    });
    // iPhone app Task 73 (ruling 4.11): revoking a device closes its slices,
    // held ones included, and forgets its saved layout. The Core's last
    // slice is never closed; it stays, owned by nobody.
    connect(m_devices.get(), &DeviceStore::deviceRemoved, this, [this](const QByteArray& id) {
        if (m_radioModel && m_radioModel->role() == RadioModel::Role::Local) {
            // Fix wave: and its C-Tune pins.
            m_radioModel->clearStreamCtunPinsAnchoredBy(id);
            SliceOwnership* ownership = m_radioModel->sliceOwnership();
            QList<int> slices = ownership->ownedBy(id);
            slices += ownership->heldFor(id);
            for (int sliceId : std::as_const(slices)) {
                if (!closeSliceFor(sliceId, QByteArray())) {
                    ownership->setOwner(sliceId, QByteArray());
                }
            }
        }
        DeviceLayoutStore::forgetDevice(AppSettings::instance(), id);
        m_slicesNotRestored.remove(id);
        // iPhone app Task 74 (7.4): its questions and waiting notices go.
        m_confirm->forgetDevice(id);
    });
    // iPhone app Task 73 (ruling 4.11): the end of a device's 180 s.
    connect(m_deviceSessions.get(), &DeviceSessionRegistry::graceEnded, this,
            [this](const QByteArray& deviceId) {
                // Fix wave 2: a slice another device took while this one
                // was away lived only in its Take it back notice; now that
                // Take it back is gone it is saved like the device's own.
                saveTakenSlicesFor(deviceId);
                releaseDeviceSlices(deviceId);
            });
    // iPhone app Task 13 (R-IOS-08): the `devices` object. A device removed
    // by anything (devices.revoke, the console, a reset) loses its
    // connection at once; so does every connection signed in with the token
    // once it is retired.
    // iPhone app Task 14: the pairing window before the devices object, so
    // it follows a change of the device store first and the object then
    // counts the change once.
    m_pairingWindow = std::make_unique<PairingWindow>(*m_devices);
    m_pairingHasher = &SpakeExchange::storedData;
    m_devicesFacade = std::make_unique<StationDevicesFacade>(
        *m_devices, *m_tokens, *m_identity, m_settings, nullptr, m_pairingWindow.get());
    connect(m_devices.get(), &DeviceStore::deviceRemoved, this, [this](const QByteArray& id) {
        endAuthenticatedPeers([&id](const Peer& peer) { return peer.deviceId == id; },
                              QString::fromLatin1(kDeviceRemovedReason),
                              SessionEndCode::kDeviceRemoved);
    });
    // iPhone app Task 19 (R-IOS-06): the catalogue follows the Core's radio,
    // its filter presets and its band plans from here on.
    m_catalog = std::make_unique<StationCatalog>();
    m_catalog->bind(radioModel);

    connect(m_devicesFacade.get(), &StationDevicesFacade::tokenRetired, this, [this]() {
        endAuthenticatedPeers([](const Peer& peer) { return peer.signedInWithToken; },
                              QString::fromLatin1(kPairingRequiredReason),
                              SessionEndCode::kPairingRequired);
    });
    if (m_certificates->isValid()) {
        // The pin is the certificate's SHA-256 (CertificateStore), which is
        // exactly the hash a device signs and the binding covers.
        QString hex = m_certificates->fingerprintSha256();
        hex.remove(QLatin1Char(':'));
        m_certSha256 = QByteArray::fromHex(hex.toLatin1());
    }
    if (m_identity->isValid() && m_certSha256.size() == 32) {
        m_certBinding = m_identity->certBinding(m_certSha256);
        m_declaredFeatures.insert(QByteArrayLiteral("deviceAuth"), 1);
        // iPhone app Task 71 (ruling 10.1): the Core admits up to four
        // devices and may ask the fifth-device question (Task 41). A
        // client uses it only with deviceAuth, so it is declared with it.
        m_declaredFeatures.insert(QByteArrayLiteral("sessionHolder"), 1);
        // iPhone app Task 14: pairing needs the identity key too (the
        // device learns it from the Core's pair.accept or box). Declared by
        // the Core only, and never asked of a client.
        if (SpakeExchange::isAvailable()) {
            m_declaredFeatures.insert(QByteArrayLiteral("pairing"), 1);
        }
    }

    // iPhone app Task 14 (R-IOS-08): the pairing window follows the device
    // store (open with no timer while unclaimed). The `devices` object
    // shows it, and the stored data for the old code is wiped. The code is
    // never printed or logged (Part C fix wave: standard output is the
    // journal on a packaged Core); `nereusd pairing show` gives it.
    connect(m_pairingWindow.get(), &PairingWindow::codeChanged, this,
            [this](const QString& code) {
                if (m_pairingStoredSerial != m_pairingWindow->codeSerial() || code.isEmpty()) {
                    SpakeExchange::wipe(m_pairingStored);
                    m_pairingStoredSerial = 0;
                }
            });

    // The first start of a Core: the TLS pin a window checks and where the
    // identity key lives, with the prompt to back it up (pairing design
    // section 3.2: losing it means every paired device pairs again).
    // Printed ONCE, on the run that creates the key. There is no secret in
    // it any more (a new Core has no token), but it still goes to stdout
    // rather than the log: see writePairingBanner() for what the logging
    // handler used to do to the fingerprint.
    if (m_identity->wasCreatedThisRun()) {
        writePairingBanner(formatFirstRunBanner(m_certificates->fingerprintSha256(),
                                                m_identity->keyPath()));
        qCInfo(lcStation)
            << "First run for this profile: the Core's identity key was created and "
               "its location and TLS certificate fingerprint were printed to stdout.";
    }
    if (!m_identity->isValid()) {
        qCWarning(lcStation) << "The Core's identity key is unavailable:"
                             << m_identity->lastError();
    }
    if (!m_tokens->isValid()) {
        qCWarning(lcStation) << "Pairing token unavailable:" << m_tokens->lastError();
    }

    m_mirror = new StateMirror(this);
    m_registry = new ObjectRegistry(radioModel, m_mirror, this);
    m_dispatcher = new SessionCommandDispatcher(radioModel, this);
    m_dispatcher->setDeviceAdmin(m_devicesFacade.get());
    // iPhone app Task 76 (ruling 9.3 item 4): the PureSignal display goes
    // to, and is charged to, the session that subscribed to it. The
    // dispatcher asks this gate; it names the asking session's media epoch
    // to the handler DaemonMediaHub installs and records the subscriber.
    m_dispatcher->setPs3DisplayAdmissionHandler([this](bool enabled, QString* refusal) {
        const auto asker = m_peers.constFind(m_dispatchingTransport);
        const quint64 epoch = asker != m_peers.cend() ? asker->mediaEpoch : 0;
        if (m_ps3DisplayAdmission && !m_ps3DisplayAdmission(epoch, enabled, refusal)) {
            return false;
        }
        const quint64 before = m_ps3SubscriberEpoch;
        m_ps3SubscriberEpoch = enabled ? epoch : 0;
        if (before != m_ps3SubscriberEpoch) {
            // The display (and its charge) moves between sessions without
            // the facade's subscription changing: split again at once.
            publishDisplayBudgetCapabilities();
        }
        return true;
    });
    m_settingsServer = new SettingsProxyServer(settings, this);
    // R-R3-46: the Core applies hardware settings for its connected radio
    // only; a write naming any other radio's MAC is refused.
    if (radioModel) {
        m_settingsServer->setConnectedMacProvider([model = QPointer<RadioModel>(radioModel)] {
            return model ? model->currentRadioMac() : QString();
        });
    }

    // Outbound: iPhone app Task 72 (ruling 5.6). Each admitted session has
    // its own MirrorView (promoteToSession), which sends the mirror's
    // messages to that session alone, fitted by sendToPeer; nothing goes
    // to a connection that holds no view.
    connect(radioModel, &RadioModel::receiveLayoutHydrated, this, [this] {
        // iPhone app Task 73 (ruling 5.2): a layout restored while devices
        // are on the Core (a radio that arrived late) brings its slices
        // with the owners its manifest names, each held for its device.
        // A device holding a place takes its own back, and a device alone
        // on the Core adopts the slices nobody owns, as at its admission.
        // The bursts below carry the result, so no view is sent the
        // change of owner on its own.
        if (m_radioModel && m_radioModel->role() == RadioModel::Role::Local) {
            m_ownerChangesInBurst = true;
            SliceOwnership* ownership = m_radioModel->sliceOwnership();
            const QList<DeviceSessionRegistry::Entry> entries = m_deviceSessions->entries();
            for (const DeviceSessionRegistry::Entry& entry : entries) {
                ownership->returnHeld(entry.deviceId);
            }
            adoptForLoneDevice();
            m_ownerChangesInBurst = false;
            m_connectedDevices->refresh();
        }
        if (hasAuthenticatedSession() && m_mirrorBuilt) {
            // Shared slice QObjects were restored without individual notify
            // signals. Re-seed their entire settled state on every session,
            // each view its own burst.
            for (const QPointer<MirrorView>& view : attachedViews()) {
                if (!view.isNull()) {
                    view->attach();
                }
            }
        }
    });
    // iPhone app Task 74 (R-IOS-30): confirm.proceed, confirm.cancel and
    // notice.takeBack are answered by the confirm step.
    m_dispatcher->setConfirmAnswer([this](const SessionMessage& invoke, int id, int choice) {
        return answerConfirm(invoke, id, choice);
    });
    // iPhone app Task 71: a command's result goes to the session that asked:
    // the one being dispatched now, or, for a result that arrives on a later
    // turn, the one its verb and id were recorded for. The media session
    // keeps any other (as the one session did before).
    //
    // Fix wave I1: every client counts its command ids from 1, so a result
    // is named by the session it answers (the dispatcher's resultOwner())
    // with its verb and id, never by verb and id alone. A result nobody's
    // route names is dropped, not handed to another session.
    connect(m_dispatcher, &SessionCommandDispatcher::commandResultReady, this,
            [this](const SessionMessage& original) {
                const ResultKey key = resultKeyOf(original);
                // iPhone app Task 75: a proceed whose held command answers
                // later is answered with that command's result.
                if (m_proceedAnsweredLater && *m_proceedAnsweredLater == key) {
                    m_proceedAnsweredLater.reset();
                    return;
                }
                if (finishDeferredProceed(key, original)) {
                    return;
                }
                // iPhone app Task 74: the confirm step reads (and may keep,
                // or reword) a result of a change it is running.
                SessionMessage result = original;
                if (m_resultHook && !m_resultHook(result)) {
                    return;
                }
                SessionTransport* to = nullptr;
                const auto dispatching = m_peers.constFind(m_dispatchingTransport);
                if (dispatching != m_peers.cend() && dispatching->sessionId == key.sessionId) {
                    to = m_dispatchingTransport;
                    m_resultSentInDispatch = true;
                } else {
                    const auto route = m_resultRoutes.find(key);
                    if (route != m_resultRoutes.end()) {
                        to = route->data();
                        if (isLastResult(result)) {
                            m_resultRoutes.erase(route);
                        }
                    }
                }
                if (to != nullptr) {
                    sendToPeer(to, result);
                } else {
                    qCDebug(lcStation) << "No session asked for" << result.commandVerb
                                       << result.commandId << "; dropped";
                }
            });
    // iPhone app Task 71 (ruling 4.12): session.leave was accepted for the
    // connection being dispatched; it ends once its result has gone out.
    connect(m_dispatcher, &SessionCommandDispatcher::sessionLeaveRequested, this, [this]() {
        if (m_dispatchingTransport != nullptr) {
            const auto peer = m_peers.find(m_dispatchingTransport);
            if (peer != m_peers.end()) {
                peer->leaving = true;
            }
        }
    });
    connect(m_settingsServer, &SettingsProxyServer::outboundValueChanged, this,
            [this](const QString& key, const QVariant& value, const QString& originTag) {
                // Ruling 5.8: to every view holding the key (every session
                // holds every Station key it was sent in its snapshot), the
                // writer's origin kept so the writer knows its own echo.
                sendToEveryView(
                    SessionMessages::settingsValue(key, value.toString(), originTag));
            });
    // R-R3-49: the Network Watchdog is a radio setting, applied where the
    // radio is. A window's change lands in the Core's settings above; the
    // Core's radio takes it here, whichever path stored it.
    connect(m_settingsServer, &SettingsProxyServer::outboundValueChanged, this,
            [this](const QString& key, const QVariant& value, const QString&) {
                if (key == QLatin1String("NetworkWatchdogEnabled") && m_radioModel) {
                    m_radioModel->applyNetworkWatchdog(value.toString() == QLatin1String("True"));
                }
            });
    // Task 13: External TX Inhibit gates the Core's own keying, so a
    // window's change reaches the Core's TxInhibitMonitor, whichever path
    // stored it. Thetis setup.cs:16660-16667 [v2.10.3.15] applies each box
    // at once.
    connect(m_settingsServer, &SettingsProxyServer::outboundValueChanged, this,
            [this](const QString& key, const QVariant& value, const QString&) {
                if (!m_radioModel) {
                    return;
                }
                const bool on = value.toString() == QLatin1String("True");
                if (key == QLatin1String("TxInhibitMonitorEnabled")) {
                    m_radioModel->txInhibit().setEnabled(on);
                } else if (key == QLatin1String("TxInhibitMonitorReversed")) {
                    m_radioModel->txInhibit().setReverseLogic(on);
                }
            });
    // Whole-branch review, Important 4. A removal has its own signal and
    // its own frame. It used to arrive here as an outboundValueChanged
    // carrying an INVALID QVariant, and the value.toString() above turned
    // that into "" -- so every client cached an empty string for a key
    // the station no longer had, and a client that had just correctly
    // removed the key itself had it resurrected by the echo.
    connect(m_settingsServer, &SettingsProxyServer::outboundValueRemoved, this,
            [this](const QString& key) {
                sendToEveryView(SessionMessages::settingsValueAbsent(key, QString()));
            });
    // R-R3-49: a removal (a settings reset on the Core while it runs)
    // leaves the settings reading the default, so the radio takes the
    // default too rather than keep the last value it was given. The schema
    // v7 reset is not seen here: it runs in CoreInit before this server
    // exists, and the radio reads the reset value when it connects.
    // iPhone app Task 13: the devices object's label follows StationCallsign
    // until a rename, and its key backup follows its setting.
    connect(m_settingsServer, &SettingsProxyServer::outboundValueChanged, this,
            [this](const QString& key, const QVariant&, const QString&) {
                if (isDevicesSettingsKey(key)) {
                    m_devicesFacade->refresh();
                }
                // iPhone app Task 19: a preset or the CW pitch changed,
                // from this computer or a window. Coalesced, so the three
                // settings of one preset move the revision once.
                if (isCatalogSettingsKey(key)) {
                    m_catalog->scheduleRefresh();
                }
            });
    connect(m_settingsServer, &SettingsProxyServer::outboundValueRemoved, this,
            [this](const QString& key) {
                if (isDevicesSettingsKey(key)) {
                    m_devicesFacade->refresh();
                }
                if (isCatalogSettingsKey(key)) {
                    m_catalog->scheduleRefresh();
                }
            });
    connect(m_settingsServer, &SettingsProxyServer::outboundValueRemoved, this,
            [this](const QString& key) {
                if (key == QLatin1String("NetworkWatchdogEnabled") && m_radioModel) {
                    m_radioModel->applyNetworkWatchdog(RadioModel::kNetworkWatchdogDefault);
                }
                // Task 13: both External TX Inhibit boxes default off
                // (console.cs:15336-15337 [v2.10.3.15]).
                if (key == QLatin1String("TxInhibitMonitorEnabled") && m_radioModel) {
                    m_radioModel->txInhibit().setEnabled(false);
                } else if (key == QLatin1String("TxInhibitMonitorReversed") && m_radioModel) {
                    m_radioModel->txInhibit().setReverseLogic(false);
                }
            });

    // A daemon can authenticate a GUI before its configured radio is
    // discoverable.  currentRadioChanged is emitted only after RadioModel's
    // Connected handlers have populated the live identity and profile, but
    // defer the wire update one event turn so every other observer of that
    // signal has finished too.  Capture the current session now: a later
    // authenticated replacement already received its own initial snapshot,
    // and an old callback must never refresh it.
    if (m_radioModel) {
        // iPhone app Task 71: every admitted session, each with its own
        // capabilities. A session that ended (its transport gone) or a
        // connection no longer admitted is skipped.
        connect(m_radioModel, &RadioModel::currentRadioChanged, this,
                [this](const NereusSDR::RadioInfo&) {
                    QList<QPointer<SessionTransport>> sessions;
                    for (const Peer& peer : std::as_const(m_peers)) {
                        if (!peer.sessionDeviceId.isEmpty()) {
                            sessions.append(QPointer<SessionTransport>(peer.transport));
                        }
                    }
                    if (sessions.isEmpty()) {
                        return;
                    }
                    QTimer::singleShot(0, this, [this, sessions]() {
                        for (const QPointer<SessionTransport>& session : sessions) {
                            if (!session.isNull()) {
                                sendCapabilitiesAndSettingsSnapshot(session.data());
                            }
                        }
                    });
                });
        // iPhone app Task 75 (ruling 5.11a): the receive antenna stayed put
        // on a band crossing; the person tuning is told.
        connect(m_radioModel, &RadioModel::receiveAntennaKept, this,
                [this](int sliceId, const QString& antenna, const QList<QByteArray>& listeners) {
                    onReceiveAntennaKept(sliceId, antenna, listeners);
                });
    }

    // ObjectRegistry's create/destroy events are the lifecycle half of the
    // mirror; StateMirror only carries property deltas for objects it
    // already knows about.
    connect(m_registry, &ObjectRegistry::objectCreated, this,
            [this](const QByteArray& objectKey, const QByteArray& className, int,
                   const QList<MirrorUpdate>& snapshot) {
                sendToEveryView(
                    SessionMessages::objectCreate(objectKey, className, snapshot));
            });
    connect(m_registry, &ObjectRegistry::objectDestroyed, this,
            [this](const QByteArray& objectKey, const QByteArray& className, int) {
                sendToEveryView(SessionMessages::objectDestroy(objectKey, className));
            });

    // iPhone app Task 73 (ruling 5.4): a marker per slice, made after
    // ObjectRegistry's connections so a slice's create and destroy go out
    // before its marker's. Its owner fields name the device the slice is
    // held for when held, else its owner, one device named the same way on
    // every page (ruling 4.3).
    m_markers = new SliceMarkerSet(radioModel, m_mirror, [this](int sliceId) {
        SliceMarker::Owner owner;
        if (!m_radioModel) {
            return owner;
        }
        const SliceOwnership::Mark mark = m_radioModel->sliceOwnership()->mark(sliceId);
        const QByteArray subject = mark.subject();
        if (subject.isEmpty() || subject == SliceOwnership::stationDevice()) {
            owner.kind = QStringLiteral("station");
            return owner;
        }
        if (const auto words = m_connectedDevices->describe(subject)) {
            owner.deviceId = words->wireId;
            owner.name = words->name;
            owner.shortName = words->shortName;
            owner.kind = words->kind;
        } else {
            owner.deviceId = StationIdentity::toBase64Url(subject);
        }
        const std::optional<DeviceSessionRegistry::Entry> entry = m_deviceSessions->entry(subject);
        owner.away = mark.isHeld()
            || (entry && entry->state == DeviceSessionRegistry::State::Away);
        return owner;
    }, this);
    connect(m_markers, &SliceMarkerSet::markerCreated, this,
            [this](const QByteArray& key, const QByteArray& className,
                   const QList<MirrorUpdate>& snapshot) {
                sendToEveryView(SessionMessages::objectCreate(key, className, snapshot));
            });
    connect(m_markers, &SliceMarkerSet::markerDestroyed, this,
            [this](const QByteArray& key, const QByteArray& className) {
                sendToEveryView(SessionMessages::objectDestroy(key, className));
            });
    // Away, back, a name or a short name changed: the markers say so.
    connect(m_deviceSessions.get(), &DeviceSessionRegistry::changed, m_markers,
            &SliceMarkerSet::refreshOwners);
    connect(m_devices.get(), &DeviceStore::devicesChanged, m_markers,
            &SliceMarkerSet::refreshOwners);
    // connectedDevices.listeningOn: a device's slices, their bands and
    // modes.
    m_connectedDevices->setListeningProvider(
        [this](const QByteArray& deviceId) { return listeningOn(deviceId); });
    m_dispatcher->setSliceAccess([this](const QByteArray& requester, int sliceId) {
        return sliceRefusal(requester, sliceId);
    });
    if (radioModel) {
        connect(radioModel->sliceOwnership(), &SliceOwnership::markChanged, this,
                &StationServer::onSliceOwnerChanged);
        const auto followSlice = [this](SliceModel* slice) {
            if (slice == nullptr) {
                return;
            }
            connect(slice, &SliceModel::bandChanged, m_connectedDevices.get(),
                    &ConnectedDevicesFacade::refresh, Qt::UniqueConnection);
            connect(slice, &SliceModel::dspModeChanged, m_connectedDevices.get(),
                    &ConnectedDevicesFacade::refresh, Qt::UniqueConnection);
        };
        for (SliceModel* slice : radioModel->slices()) {
            followSlice(slice);
        }
        connect(radioModel, &RadioModel::sliceAdded, this, [this, followSlice](int sliceId) {
            if (m_radioModel) {
                followSlice(m_radioModel->sliceById(sliceId));
                // A slice the Core made for nobody (a radio that arrived
                // after the device, the Core's own top-up) goes to a device
                // alone on the Core, as the slices it found at admission
                // did. A turn later, so a layout being restored settles its
                // owners first.
                if (m_radioModel->sliceOwnership()->mark(sliceId).owner.isEmpty()) {
                    QTimer::singleShot(0, this, [this]() { adoptForLoneDevice(); });
                }
            }
            m_connectedDevices->refresh();
        });
        connect(radioModel, &RadioModel::sliceRemoved, m_connectedDevices.get(),
                &ConnectedDevicesFacade::refresh);
        // Fix wave I2: a question naming a slice that closed can no longer
        // be proceeded (its id may soon be another device's).
        connect(radioModel, &RadioModel::sliceRemoved, this,
                [this](int sliceId) { dropQuestionsNaming(sliceId); });
    }

    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(m_heartbeatIntervalMs);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &StationServer::onHeartbeatTick);

    m_deltaFlushTimer = new QTimer(this);
    m_deltaFlushTimer->setInterval(kDefaultDeltaFlushMs);
    connect(m_deltaFlushTimer, &QTimer::timeout, this, [this]() {
        // iPhone app Task 72: each session's own view, its own pending.
        for (const QPointer<MirrorView>& view : attachedViews()) {
            if (!view.isNull()) {
                view->flush();
            }
        }
    });

    // iPhone app Task 71 (ruling 4.11): an away device's place frees when
    // its 180 s end. One timer for the next end, re-armed on every change.
    m_graceTimer = new QTimer(this);
    m_graceTimer->setSingleShot(true);
    connect(m_graceTimer, &QTimer::timeout, this, [this]() {
        m_deviceSessions->expireAway();
        scheduleGraceCheck();
    });
    connect(m_deviceSessions.get(), &DeviceSessionRegistry::changed, this,
            &StationServer::scheduleGraceCheck);
}

StationServer::~StationServer()
{
    // iPhone app Task 14: a pairing code being hashed finishes first; its
    // result is dropped with this object.
    if (m_pairingHashThread) {
        m_pairingHashThread->wait();
    }
    close();
}

// ── Listener lifecycle ───────────────────────────────────────────────────

bool StationServer::listen(const QHostAddress& address, quint16 port)
{
    m_lastError.clear();

    // Idempotent. A second call used to re-apply the SSL configuration and
    // then fail the bind into lastError(), so a caller that could not
    // cheaply tell whether it had already started ended up with a working
    // listener AND an error string describing it as broken. Rebinding
    // somewhere else is close() then listen() again, deliberately explicit.
    if (isListening()) {
        qCDebug(lcStation) << "listen() ignored: already listening on port"
                            << m_openingGate->serverPort();
        return true;
    }

    if (!QSslSocket::supportsSsl()) {
        m_lastError = CertificateStore::tlsBackendDiagnostic();
        if (m_lastError.isEmpty()) {
            m_lastError = QStringLiteral("Qt reports no working TLS backend");
        }
        qCWarning(lcStation) << "Refusing to listen:" << m_lastError;
        return false;
    }
    if (!m_certificates->isValid()) {
        m_lastError = m_certificates->lastError();
        qCWarning(lcStation) << "Refusing to listen:" << m_lastError;
        return false;
    }
    if (!m_identity->isValid()) {
        // iPhone app Task 12: listening without the Core's identity would
        // accept no device, forever, while looking healthy (and a new Core
        // has no token either). Refuse loudly instead.
        m_lastError = m_identity->lastError().isEmpty()
                          ? QStringLiteral("The Core's own key is unavailable")
                          : m_identity->lastError();
        qCWarning(lcStation) << "Refusing to listen:" << m_lastError;
        return false;
    }

    if (m_wsServer == nullptr) {
        m_wsServer = new QWebSocketServer(QStringLiteral("NereusSDR station"),
                                          QWebSocketServer::SecureMode, this);
        connect(m_wsServer, &QWebSocketServer::newConnection, this,
                &StationServer::onNewWebSocketConnection);
    }
    if (m_openingGate == nullptr) {
        // The listening socket is the gate's, not Qt's: the gate runs TLS,
        // reads the opening request, fixes a Host Qt cannot read or
        // answers 400, and hands the connection to m_wsServer for the 101
        // (StationOpeningGate.h has the cause). m_wsServer itself never
        // listens.
        m_openingGate = new StationOpeningGate(m_wsServer, m_maxOpenings,
                                               m_maxOpeningsPerAddress,
                                               &StationServer::addressKey, this);
    }
    m_openingGate->setOpeningLimits(m_maxOpenings, m_maxOpeningsPerAddress);
    m_openingGate->setOpeningDeadlineMs(m_openingDeadlineMs);

    QSslConfiguration tls = QSslConfiguration::defaultConfiguration();
    tls.setLocalCertificate(m_certificates->certificate());
    tls.setPrivateKey(m_certificates->privateKey());
    // The client pins this certificate's fingerprint (parent design
    // section 10.5), so it is the client's job to decide whether to trust
    // it. Asking for a client certificate here would be a second,
    // unimplemented identity mechanism.
    tls.setPeerVerifyMode(QSslSocket::VerifyNone);
    // iPhone app Task 4 (R-IOS-01): the minimum is set here rather than
    // left to Qt's default (QSsl::SecureProtocols, which is TLS 1.2 or
    // later today but may change with Qt). The link document's section 2
    // states it; tst_link_version reads it back from the listener.
    tls.setProtocol(QSsl::TlsV1_2OrLater);
    // The gate's copy is the one every connection uses (it runs TLS;
    // m_wsServer never listens), so it is the only copy, and
    // tlsConfiguration() reads it back from the gate.
    m_openingGate->setTlsConfiguration(tls);

    if (!m_openingGate->listen(address, port)) {
        m_lastError = m_openingGate->errorString();
        qCWarning(lcStation) << "Listen failed:" << m_lastError;
        return false;
    }

    qCInfo(lcStation) << "Station listening on wss://" << address.toString() << ":"
                      << m_openingGate->serverPort();
    emit listeningChanged(true);
    return true;
}

void StationServer::close()
{
    const bool wasListening = isListening();
    const QList<SessionTransport*> transports = m_peers.keys();
    // Task 76: the sessions still to be ended are not sent new shares.
    m_closing = true;
    for (SessionTransport* transport : transports) {
        dropPeer(transport, QStringLiteral("The Core is shutting down."), true,
                 /*retryable=*/true);
    }
    m_closing = false;
    if (m_openingGate != nullptr) {
        m_openingGate->closeAll();
    }
    if (m_heartbeatTimer != nullptr) {
        m_heartbeatTimer->stop();
    }
    if (m_deltaFlushTimer != nullptr) {
        m_deltaFlushTimer->stop();
    }
    if (wasListening) { emit listeningChanged(false); }
}

bool StationServer::isListening() const
{
    return m_openingGate != nullptr && m_openingGate->isListening();
}

QSslConfiguration StationServer::tlsConfiguration() const
{
    // The gate's copy: the one its connections actually run TLS with.
    return m_openingGate != nullptr ? m_openingGate->tlsConfiguration() : QSslConfiguration();
}

quint16 StationServer::peerAgreedMajor(SessionTransport* peer) const
{
    const auto it = m_peers.constFind(peer);
    return it != m_peers.constEnd() ? it->agreedMajor : quint16(0);
}

bool StationServer::peerDeclares(SessionTransport* peer, const QByteArray& feature,
                                 int minVersion) const
{
    const auto it = m_peers.constFind(peer);
    if (it == m_peers.constEnd() || !it->features.contains(feature)) {
        return false;
    }
    return it->features.value(feature) >= minVersion;
}

quint16 StationServer::serverPort() const
{
    return m_openingGate != nullptr ? m_openingGate->serverPort() : 0;
}

QHostAddress StationServer::serverAddress() const
{
    return isListening() ? m_openingGate->serverAddress() : QHostAddress{};
}

QString StationServer::token() const
{
    return m_tokens != nullptr ? m_tokens->token() : QString();
}

QString StationServer::certificateFingerprint() const
{
    return m_certificates != nullptr ? m_certificates->fingerprintSha256() : QString();
}

DeviceStore* StationServer::deviceStore() const
{
    return m_devices.get();
}

const StationIdentity& StationServer::stationIdentity() const
{
    return *m_identity;
}

StationDevicesFacade* StationServer::devicesFacade() const
{
    return m_devicesFacade.get();
}

StationCatalog* StationServer::catalog() const
{
    return m_catalog.get();
}

int StationServer::stationCatalogVersion() const
{
    return 1;
}

int StationServer::displayExtrasVersion() const
{
    // The extras travel on the media display channel, so they come with it.
    // 2 (R-IOS-27, R-IOS-06): also the clarity-retune operation.
    return m_mediaEnabled ? 2 : 0;
}

int StationServer::deviceAdminVersion() const
{
    // The same condition as stationIdentityVersion: a Core that signs
    // devices in by key can list and administer them.
    return m_certBinding.isEmpty() ? 0 : 1;
}

PairingWindow* StationServer::pairingWindow() const
{
    return m_pairingWindow.get();
}

int StationServer::pairingVersion() const
{
    return m_declaredFeatures.value(QByteArrayLiteral("pairing"), 0) >= 1 ? 1 : 0;
}

void StationServer::printToConsole(const QString& text)
{
    writePairingBanner(text);
}

QString StationServer::addressKey(const QString& address)
{
    if (address.isEmpty()) {
        return {};
    }
    QHostAddress host(address);
    if (host.isNull()) {
        return address;
    }
    bool mapped = false;
    const quint32 ipv4 = host.toIPv4Address(&mapped);
    if (mapped) {
        // IPv4, and IPv4-mapped IPv6, by the full address.
        return QHostAddress(ipv4).toString();
    }
    if (host.protocol() != QAbstractSocket::IPv6Protocol) {
        return host.toString();
    }
    // Part C follow-up (R-IOS-08): an IPv6 host is handed a whole /64 and
    // can dial from any address in it, so IPv6 peers are counted by their
    // /64 prefix. A household on one /64 then shares the two connecting
    // slots the way one behind IPv4 NAT does; signed-in sessions are not
    // counted, and the refusal is retryable.
    Q_IPV6ADDR bytes = host.toIPv6Address();
    for (int i = 8; i < 16; ++i) {
        bytes[i] = 0;
    }
    return QHostAddress(bytes).toString() + QStringLiteral("/64");
}

bool StationServer::isOnDirectNetwork(const QString& address)
{
    if (address.isEmpty()) {
        return false;
    }
    QHostAddress peer(address);
    if (peer.isNull()) {
        return false;
    }
    if (peer.isLoopback()) {
        return true;
    }
    bool mapped = false;
    const quint32 ipv4 = peer.toIPv4Address(&mapped);
    if (mapped) {
        peer = QHostAddress(ipv4);
    }
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& interface : interfaces) {
        const auto flags = interface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp)
            || !flags.testFlag(QNetworkInterface::IsRunning)) {
            continue;
        }
        const QList<QNetworkAddressEntry> entries = interface.addressEntries();
        for (const QNetworkAddressEntry& entry : entries) {
            const int prefix = entry.prefixLength();
            if (prefix < 0 || entry.ip().protocol() != peer.protocol()) {
                continue;
            }
            // Scope ids are not part of the subnet question.
            QHostAddress ip = entry.ip();
            ip.setScopeId(QString());
            QHostAddress candidate = peer;
            candidate.setScopeId(QString());
            if (candidate.isInSubnet(ip, prefix)) {
                return true;
            }
        }
    }
    return false;
}

void StationServer::publishConnectedDevices()
{
    if (!m_devicesFacade) {
        return;
    }
    // iPhone app Task 71: admitted sessions only; a sign-in turned away from
    // a full Core never shows as connected.
    QSet<QByteArray> connected;
    for (const Peer& peer : std::as_const(m_peers)) {
        if (!peer.sessionDeviceId.isEmpty() && !peer.deviceId.isEmpty()) {
            connected.insert(peer.deviceId);
        }
    }
    m_devicesFacade->setConnectedDevices(connected);
}

void StationServer::endAuthenticatedPeers(const std::function<bool(const Peer&)>& matches,
                                          const QString& reason, const char* endCode)
{
    // Copied: dropPeer() erases from m_peers.
    const QList<SessionTransport*> transports = m_peers.keys();
    for (SessionTransport* transport : transports) {
        const auto it = m_peers.constFind(transport);
        if (it == m_peers.cend() || !it->authenticated || !matches(*it)) {
            continue;
        }
        if (transport == m_dispatchingTransport) {
            // Its own request did this: the result goes out first.
            m_pendingEnd = std::make_pair(reason, QString::fromLatin1(endCode));
            continue;
        }
        dropPeer(transport, reason, true, /*retryable=*/false, QString::fromLatin1(endCode));
    }
}

QString StationServer::formatFirstRunBanner(const QString& fingerprint,
                                            const QString& identityKeyPath)
{
    return QStringLiteral(
               "\n"
               "  ============================================================\n"
               "  NereusSDR Core: first run\n"
               "  ------------------------------------------------------------\n"
               "  TLS SHA-256:  %1\n"
               "  Identity key: %2\n"
               "  ------------------------------------------------------------\n"
               "  Back up the identity key file. It is this Core's identity:\n"
               "  if it is lost, every paired device has to pair again.\n"
               "  ============================================================\n")
        .arg(fingerprint, identityKeyPath);
}

bool StationServer::hasAuthenticatedSession() const
{
    return authenticatedSessionCount() > 0;
}

int StationServer::authenticatedSessionCount() const
{
    int count = 0;
    for (const Peer& peer : std::as_const(m_peers)) {
        if (!peer.sessionDeviceId.isEmpty()) {
            ++count;
        }
    }
    return count;
}

int StationServer::devicesConnectedForDiscovery() const
{
    // Ruling 10.4: a Core no device has claimed has no devices on it and
    // sends 0; the count is a number only, never who.
    return m_devices->isClaimed() ? m_deviceSessions->placesTaken() : 0;
}

bool StationServer::peerHoldsSessions(SessionTransport* transport) const
{
    return peerDeclares(transport, QByteArrayLiteral("sessionHolder"), 1)
        && peerDeclares(transport, QByteArrayLiteral("deviceAuth"), 1);
}

bool StationServer::peerHasSessionHolderVersion(SessionTransport* transport) const
{
    const auto it = m_peers.constFind(transport);
    return it != m_peers.cend() && it->agreedMinor >= kRadioIdentitySessionProtocolMinor
        && peerHoldsSessions(transport) && sessionHolderVersion() >= 1;
}

void StationServer::noteActivity(SessionTransport* transport)
{
    const auto it = m_peers.constFind(transport);
    if (it != m_peers.cend() && !it->sessionDeviceId.isEmpty()) {
        m_deviceSessions->noteActivity(it->sessionDeviceId);
    }
}

void StationServer::scheduleGraceCheck()
{
    if (m_graceTimer == nullptr) {
        return;
    }
    const std::optional<qint64> next = m_deviceSessions->nextExpiryMs();
    if (!next) {
        m_graceTimer->stop();
        return;
    }
    const qint64 remaining = std::max<qint64>(0, *next - m_deviceSessions->now());
    m_graceTimer->start(static_cast<int>(std::min<qint64>(remaining, 24LL * 3600 * 1000)));
}

// ── Peer lifecycle ───────────────────────────────────────────────────────

void StationServer::onNewWebSocketConnection()
{
    while (m_wsServer != nullptr && m_wsServer->hasPendingConnections()) {
        QWebSocket* socket = m_wsServer->nextPendingConnection();
        if (socket == nullptr) {
            break;
        }
        // Opened: it stops counting among the openings still in progress,
        // and from here the peer caps below govern it.
        if (m_openingGate != nullptr) {
            m_openingGate->markOpened(socket);
        }
        // The cap goes on inside WebSocketTransport's constructor, which
        // runs here, inside the newConnection slot, before control returns
        // to the event loop -- so no frame on this socket has been
        // processed yet. See kMaxIncomingMessageBytes for the arithmetic
        // and for why an uncapped accepted socket is a pre-authentication
        // memory-exhaustion path rather than a theoretical one.
        acceptTransport(
            new WebSocketTransport(socket, kMaxIncomingMessageBytes));
    }
}

void StationServer::acceptTransport(SessionTransport* transport)
{
    if (transport == nullptr) {
        return;
    }
    transport->setParent(this);

    // Socket cap. Refused BEFORE any state is allocated for it, and with a
    // reason on the wire so a legitimate client that hits this knows why
    // rather than seeing an unexplained close. iPhone app Task 71: every
    // socket counts, signed in or not (kMaxConcurrentPeers' comment has the
    // arithmetic); who holds a device's place is the registry's.
    if (m_peers.size() >= kMaxConcurrentPeers) {
        qCWarning(lcStation) << "Refusing connection from" << transport->peerDescription()
                             << ": already at" << kMaxConcurrentPeers << "peers";
        // RETRYABLE, and this is the one that mattered most. The header
        // sizes kMaxConcurrentPeers for four devices each reconnecting with
        // an old socket and racing attempts, so this cap is expected to be
        // hit BY a reconnecting client, transiently, while its own dead
        // sockets are still draining. Sent as permanent, it told exactly
        // that client to stop trying forever.
        transport->sendText(SessionMessages::encode(SessionMessages::sessionEnd(
            QStringLiteral("The Core already has as many connections as it allows. Try again shortly."),
            /*retryable=*/true)));
        transport->closeLink(QStringLiteral("The Core already has as many connections as it allows. Try again shortly."));
        transport->deleteLater();
        return;
    }

    // Part C fix wave (R1-M4): one address holds at most
    // kMaxHandshakesPerAddress of the slots while it is still connecting,
    // so a host on the internet cannot keep the phone out by holding every
    // one of them. The same retryable refusal as the cap above.
    const QString address = addressKey(transport->peerAddress());
    if (!address.isEmpty()) {
        int connecting = 0;
        for (const Peer& other : std::as_const(m_peers)) {
            if (!other.snapshotComplete && other.transport != nullptr
                && addressKey(other.transport->peerAddress()) == address) {
                ++connecting;
            }
        }
        if (connecting >= kMaxHandshakesPerAddress) {
            qCWarning(lcStation) << "Refusing connection from" << transport->peerDescription()
                                 << ": that address already has" << connecting
                                 << "connections still connecting";
            const QString reason = QStringLiteral(
                "The Core already has as many connections as it allows. Try again shortly.");
            transport->sendText(SessionMessages::encode(
                SessionMessages::sessionEnd(reason, /*retryable=*/true)));
            transport->closeLink(reason);
            transport->deleteLater();
            return;
        }
    }

    Peer peer;
    peer.transport = transport;
    peer.description = transport->peerDescription();
    // iPhone app Task 12: this connection's own challenge, so a signature
    // made for another connection never verifies on this one.
    peer.challenge = m_deviceAuth->newChallenge();

    // Finish-the-handshake-or-drop. Parented to the transport so it cannot
    // outlive the peer it is about, and stopped once this peer's snapshot
    // has been sent (promoteToSession()), not merely at authentication:
    // R-R3-16/17 bounds the whole connect sequence with the same
    // kStationHandshakeDeadlineMs the GUI uses. An OWNED single-shot timer,
    // not static QTimer::singleShot: cancellability is the whole point.
    //
    // One log line per expiry: dropPeer()'s "Peer detached" line names the
    // peer and this reason, and dropPeer() is also what retires a media
    // context the peer holds (mediaSessionEnded when it is the session).
    if (m_authDeadlineMs > 0) {
        auto* deadline = new QTimer(transport);
        deadline->setSingleShot(true);
        deadline->setInterval(m_authDeadlineMs);
        connect(deadline, &QTimer::timeout, this, [this, transport]() {
            auto it = m_peers.find(transport);
            if (it == m_peers.end() || it->snapshotComplete) {
                return;
            }
            dropPeer(transport, QStringLiteral("This app did not finish connecting to the Core in time."), true,
                     /*retryable=*/true);
        });
        deadline->start();
        peer.authDeadline = deadline;
    }

    m_peers.insert(transport, peer);

    connect(transport, &SessionTransport::textReceived, this,
            [this, transport](const QByteArray& wire) { onTransportText(transport, wire); });
    connect(transport, &SessionTransport::pongReceived, this, [this, transport]() {
        auto it = m_peers.find(transport);
        if (it != m_peers.end()) {
            it->pingsAwaitingPong = 0;
        }
    });
    connect(transport, &SessionTransport::closed, this,
            [this, transport]() { onTransportClosed(transport); });

    if (!m_heartbeatTimer->isActive() && m_heartbeatIntervalMs > 0) {
        m_heartbeatTimer->start();
    }

    // The daemon greets first, so a client can refuse on a major version
    // mismatch without ever having sent its token. Section 7.0's sequence
    // does not fix which end speaks first; sending it in the direction
    // that avoids exposing a secret to an incompatible peer is this
    // task's own choice, recorded here.
    //
    // iPhone app Task 4 (R-IOS-01): the hello names every major this
    // station accepts, and declares its features, so the client can pick
    // the highest shared major before it answers. `major` is the OLDEST of
    // them: a client built before `majors` existed reads only `major` and
    // leaves unless it is the one major it speaks, so the newest there would
    // turn away every such client this station could still serve (spec D39).
    // A client that reads `majors` ignores `major`.
    SessionMessage hello =
        SessionMessages::hello(m_supportedMajors.first(), kSessionProtocolMinor,
                               settingsSchemaVersionOf(m_settings), peerNameForThisProcess(),
                               m_supportedMajors, m_declaredFeatures);
    // iPhone app Task 12 (R-IOS-08): the Core's identity key, its binding
    // to the certificate this connection presents, and the challenge a
    // paired device signs. An older app ignores all three.
    if (!m_certBinding.isEmpty()) {
        hello.stationIdentity = SessionStationIdentity{
            StationIdentity::toBase64Url(m_identity->publicKeySpki()),
            StationIdentity::toBase64Url(m_certBinding),
        };
        hello.challenge = StationIdentity::toBase64Url(peer.challenge);
    }
    send(transport, hello);

    qCDebug(lcStation) << "Peer attached:" << peer.description;
}

void StationServer::onTransportClosed(SessionTransport* transport)
{
    dropPeer(transport, QStringLiteral("The app closed the connection."), false,
             /*retryable=*/true);
}

void StationServer::dropPeer(SessionTransport* transport, const QString& reason,
                             bool sendSessionEnd, bool retryable, const QString& endCode)
{
    auto it = m_peers.find(transport);
    if (it == m_peers.end()) {
        return;
    }
    const QString description = it->description;

    if (sendSessionEnd) {
        send(transport, SessionMessages::sessionEnd(reason, retryable, endCode));
    }
    // iPhone app Task 14: a code this connection took and did not pair
    // with is burned, however the connection ends (a wrong code, a device
    // that gave up after its own step 3, a dropped link, the deadline).
    const std::shared_ptr<PairingAttempt> pairing = it->pairing;
    // iPhone app Task 71: an admitted session's end reaches the registry. A
    // paired device that did not leave on purpose is away for 180 s,
    // keeping its place (ruling 4.10); a token window, and a device that
    // left, frees its place at once. A connection replaced by its own
    // device's newer one, or whose device was revoked, settled its place
    // already.
    const QByteArray sessionDevice = it->sessionDeviceId;
    const bool placeSettled = it->placeSettled;
    const bool leaving = it->leaving;
    // iPhone app Task 72: the session's view stops at once (a burst in
    // progress sends nothing more) and goes with the event loop; the
    // DSP-asset jobs this session started are cancelled, and only those
    // (ruling 5.8).
    const QPointer<MirrorView> view = it->view;
    const QString owner = sessionOwner(it->sessionId);
    // iPhone app Task 76: this session's own media ends with it.
    const quint64 mediaEpoch = it->mediaEpoch;
    m_peers.erase(it);
    if (!view.isNull()) {
        view->close();
        view->deleteLater();
    }
    if (!owner.isEmpty()) {
        m_dispatcher->endSessionOwner(owner);
    }
    for (auto route = m_resultRoutes.begin(); route != m_resultRoutes.end();) {
        route = route->isNull() || route->data() == transport ? m_resultRoutes.erase(route)
                                                              : std::next(route);
    }
    if (pairing && pairing->codeTaken && !pairing->finished) {
        pairing->finished = true;
        m_pairingWindow->pairingFailed();
    }
    // iPhone app Task 74 (ruling 7.5): a session's open question ends with
    // it (a newer connection of the same device asks afresh).
    if (!sessionDevice.isEmpty()) {
        m_confirm->dropQuestion(sessionDevice);
    }
    if (!sessionDevice.isEmpty() && !placeSettled) {
        m_deviceSessions->sessionEnded(sessionDevice, transport,
                                       leaving ? DeviceSessionRegistry::EndKind::Left
                                               : DeviceSessionRegistry::EndKind::Dropped);
        // iPhone app Task 73 (ruling 4.12): a device that left, and a token
        // window (no 180 s: it cannot be recognised when it comes back),
        // give up their slices now. A paired device that dropped keeps them
        // for its 180 s (graceEnded).
        if (leaving || sessionDevice.startsWith("token:")) {
            releaseDeviceSlices(sessionDevice);
        }
    }
    publishConnectedDevices();

    if (mediaEpoch != 0) {
        // iPhone app Task 76: this session's media and telemetry end; every
        // other session's go on. The session state the old one-media-
        // session rule reset at its end is the Core's again once no session
        // is left; the PureSignal display leaves with its subscriber.
        const bool lastSession = primaryMediaSession() == nullptr;
        if (lastSession) {
            m_ps3SubscriberEpoch = 0;
            m_dispatcher->resetSessionState();
            if (m_radioModel) {
                m_radioModel->pureSignalFacade()->resetSession();
            }
            // Fix wave: the C-Tune pins are not the session's. They end
            // when their device leaves for good (releaseDeviceSlices and
            // revocation), so a device coming back keeps them (ruling 4.8).
        } else if (m_ps3SubscriberEpoch == mediaEpoch) {
            m_ps3SubscriberEpoch = 0;
            if (m_radioModel) {
                m_radioModel->pureSignalFacade()->setRemoteAmpViewSubscribed(false);
            }
        }
        emit mediaSessionEnded(mediaEpoch);
        emit telemetrySessionEnded(mediaEpoch);
        // The others' shares grow back (new generations to each), unless
        // the Core is ending every session.
        if (!m_closing) {
            if (recomputeDisplayBudgetShares()) {
                emit displayBudgetChanged();
            }
            publishBudgetToChangedSessions();
        }
    }
    if (!hasAuthenticatedSession()) {
        // Stop draining deltas into nothing. StateMirror keeps watching --
        // the daemon's own state is not the sessions' to tear down -- and
        // the next attachSession() clears whatever the coalescer holds
        // anyway, because a fresh burst already carries every watched
        // object's current value (each view clears its own at attach).
        m_deltaFlushTimer->stop();
    }

    transport->closeLink(reason);
    transport->deleteLater();

    if (m_peers.isEmpty() && m_heartbeatTimer != nullptr) {
        m_heartbeatTimer->stop();
    }

    qCInfo(lcStation) << "Peer detached:" << description << "reason:" << reason;
    emit peerDisconnected(description, reason);
}

// ── Heartbeat ────────────────────────────────────────────────────────────

void StationServer::setHeartbeatIntervalMs(int ms)
{
    m_heartbeatIntervalMs = ms;
    if (ms <= 0) {
        qCWarning(lcStation)
            << "Heartbeat disabled. A peer that dies without closing the TCP "
               "connection will not be detected.";
        m_heartbeatTimer->stop();
        return;
    }
    m_heartbeatTimer->setInterval(ms);
    if (!m_peers.isEmpty()) {
        m_heartbeatTimer->start();
    }
}

void StationServer::setMaxMissedPongs(int misses)
{
    m_maxMissedPongs = misses < 1 ? 1 : misses;
}

static_assert(StationServer::kDefaultOpeningDeadlineMs
                  == StationOpeningGate::kDefaultOpeningDeadlineMs,
              "the Core and its opening gate state one opening deadline");

#ifdef NEREUS_BUILD_TESTS
void StationServer::setOpeningLimitsForTest(int total, int perAddress)
{
    m_maxOpenings = total > 0 ? total : kMaxUnfinishedOpenings;
    m_maxOpeningsPerAddress = perAddress > 0 ? perAddress : kMaxHandshakesPerAddress;
}
#endif

void StationServer::setOpeningDeadlineMs(int ms)
{
    if (ms > 0) {
        m_openingDeadlineMs = ms;
    }
}

int StationServer::openingCount() const
{
    return m_openingGate != nullptr ? m_openingGate->pendingCount() : 0;
}

void StationServer::setAuthDeadlineMs(int ms)
{
    m_authDeadlineMs = ms;
    if (ms < 1) {
        qCWarning(lcStation)
            << "Handshake deadline disabled. A peer that connects and answers pings "
               "but never authenticates will hold its slot indefinitely.";
    }
}

void StationServer::setAuthRateLimit(int maxFailures, int lockoutMs)
{
    if (m_tokens != nullptr) {
        m_tokens->setRateLimit(maxFailures, lockoutMs);
    }
}

void StationServer::onHeartbeatTick()
{
    // Copied deliberately: dropPeer() mutates m_peers, and a peer declared
    // dead here is dropped inside this loop.
    const QList<SessionTransport*> transports = m_peers.keys();
    for (SessionTransport* transport : transports) {
        auto it = m_peers.find(transport);
        if (it == m_peers.end()) {
            continue;
        }
        if (it->pingsAwaitingPong >= m_maxMissedPongs) {
            const QString description = it->description;
            qCWarning(lcStation)
                << "Peer" << description << "missed" << it->pingsAwaitingPong
                << "consecutive pongs; declaring the link dead";
            emit peerHeartbeatTimeout(description);
            dropPeer(transport, QStringLiteral("This app stopped answering, so the Core closed the connection."), true,
                     /*retryable=*/true);
            continue;
        }
        ++it->pingsAwaitingPong;
        transport->ping();
    }
}

// ── Inbound dispatch ─────────────────────────────────────────────────────

void StationServer::onTransportText(SessionTransport* transport, const QByteArray& wire)
{
    auto it = m_peers.find(transport);
    if (it == m_peers.end()) {
        return;
    }

    SessionMessage message;
    if (!SessionMessages::decode(wire, &message)) {
        dropPeer(transport, QStringLiteral("The Core could not read a message from this app."), true,
                 /*retryable=*/false, QString::fromLatin1(SessionEndCode::kProtocolError));
        return;
    }

    switch (message.kind) {
    case SessionMessageKind::Hello:
        handleHello(transport, message);
        return;
    case SessionMessageKind::AuthRequest:
        handleAuthRequest(transport, message);
        return;
    // iPhone app Task 14: pairing runs in place of a sign-in.
    case SessionMessageKind::PairStart:
        handlePairStart(transport, message);
        return;
    case SessionMessageKind::PairSpake:
        handlePairSpake(transport, message);
        return;
    case SessionMessageKind::PairConfirm:
        handlePairConfirm(transport, message);
        return;
    case SessionMessageKind::PairFail:
        if (!it->authenticated) {
            handlePairFailFromDevice(transport);
            return;
        }
        break;
    default:
        break;
    }

    if (!it->authenticated) {
        // Everything below this line moves radio or settings state. A peer
        // that has not proved it holds the token gets exactly one answer.
        dropPeer(transport, QStringLiteral("This app sent a request before the Core had accepted its pairing token."), true,
                 /*retryable=*/false, QString::fromLatin1(SessionEndCode::kProtocolError));
        return;
    }

    // iPhone app Task 71: a command, property write or settings write is the
    // device's activity (heartbeats and media control are not).
    if (message.kind == SessionMessageKind::CommandInvoke
        || message.kind == SessionMessageKind::PropertyWrite
        || message.kind == SessionMessageKind::SettingsWrite
        || message.kind == SessionMessageKind::SettingsRemove) {
        noteActivity(transport);
    }

    switch (message.kind) {
    case SessionMessageKind::CommandInvoke:
        // iPhone app Task 71 (ruling 10.1): session.leave came with
        // sessionHolderVersion 1; from any other peer it is a verb this
        // Core does not route, answered in the same words.
        if ((message.commandVerb == "session.leave" || message.commandVerb == "confirm.proceed"
             || message.commandVerb == "confirm.cancel"
             || message.commandVerb == "notice.takeBack")
            && !peerHasSessionHolderVersion(transport)) {
            // SessionCommandDispatcher's own words for a verb it does not
            // route.
            send(transport, SessionMessages::commandResult(
                message.commandVerb, message.commandId, false,
                QStringLiteral("The Core does not know this request. Updating the Core may help."),
                {}));
            break;
        }
        if (message.commandVerb == "setFourO3AEnabled"
            && it->agreedMinor < kRemoteFourO3AControlSessionProtocolMinor) {
            send(transport, SessionMessages::commandResult(
                message.commandVerb, message.commandId, false,
                QStringLiteral("Update this app to control 4O3A on this Core."), {}));
            break;
        }
        if ((message.commandVerb.startsWith("nnr.") || message.commandVerb.startsWith("ps3.")
             || message.commandVerb.startsWith("dspAssets."))
            && it->agreedMinor < kDspControlSessionProtocolMinor) {
            send(transport, SessionMessages::commandResult(
                message.commandVerb, message.commandId, false,
                QStringLiteral("Update this app to use this control on this Core."), {}));
            break;
        }
        if (message.commandVerb == "nnr.tryAgain"
            && it->agreedMinor < kNnrLimitSessionProtocolMinor) {
            send(transport, SessionMessages::commandResult(
                message.commandVerb, message.commandId, false,
                QStringLiteral("Update this app to try noise reduction again on this Core."), {}));
            break;
        }
        if (message.commandVerb.startsWith("notch.")
            && it->agreedMinor < kDspControlSessionProtocolMinor) {
            send(transport, SessionMessages::commandResult(
                message.commandVerb, message.commandId, false,
                QStringLiteral("Update this app to change notches on this Core."), {}));
            break;
        }
        // R-R3-47: the Power Genius verbs came with remotePgxlControlVersion
        // 2, in the minor-11 capability block.
        if ((message.commandVerb == "configurePgxl" || message.commandVerb == "disconnectPgxl"
             || message.commandVerb == "setPgxlConnectionSettings")
            && it->agreedMinor < kRadioIdentitySessionProtocolMinor) {
            send(transport, SessionMessages::commandResult(
                message.commandVerb, message.commandId, false,
                QStringLiteral("Update this app to set up the Power Genius on this Core."), {}));
            break;
        }
        // R-R3-47 / R-R3-48: the RF-Kit verbs came with
        // remoteRfKitControlVersion 2 and the station TCI verb with
        // stationTciVersion 1, in the same minor-11 block.
        if ((message.commandVerb == "configureRfKit" || message.commandVerb == "disconnectRfKit"
             || message.commandVerb == "setRfKitEnabled")
            && it->agreedMinor < kRadioIdentitySessionProtocolMinor) {
            send(transport, SessionMessages::commandResult(
                message.commandVerb, message.commandId, false,
                QStringLiteral("Update this app to set up the RF-Kit amplifier on this Core."), {}));
            break;
        }
        // R-R3-46 / R-R3-21: the filter policy verb came with
        // radioHardwareVersion 4, in the minor-11 block.
        if (message.commandVerb == "setAlexBpfMode"
            && (it->agreedMinor < kRadioIdentitySessionProtocolMinor
                || radioHardwareVersion() < 4)) {
            send(transport, SessionMessages::commandResult(
                message.commandVerb, message.commandId, false,
                it->agreedMinor < kRadioIdentitySessionProtocolMinor
                    ? QStringLiteral("Update this app to change the filter policy on this Core.")
                    : QStringLiteral("The Core has no filter settings ready."), {}));
            break;
        }
        // I4 (R-R3-47): Reset amp error came with remoteRfKitControlVersion 3.
        if (message.commandVerb == "resetRfKitError"
            && (it->agreedMinor < kRadioIdentitySessionProtocolMinor
                || rfKitControlVersion() < 3)) {
            send(transport, SessionMessages::commandResult(
                message.commandVerb, message.commandId, false,
                it->agreedMinor < kRadioIdentitySessionProtocolMinor
                    ? QStringLiteral("Update this app to reset the RF-Kit amplifier's error on "
                                     "this Core.")
                    : QStringLiteral("This Core cannot reset its RF-Kit amplifier's error."), {}));
            break;
        }
        if (message.commandVerb == "setStationTci"
            && (it->agreedMinor < kRadioIdentitySessionProtocolMinor
                || stationTciVersion() < 1)) {
            send(transport, SessionMessages::commandResult(
                message.commandVerb, message.commandId, false,
                it->agreedMinor < kRadioIdentitySessionProtocolMinor
                    ? QStringLiteral("Update this app to turn the Core's TCI server on or off.")
                    : QStringLiteral("This Core has no TCI server."), {}));
            break;
        }
        // R-R3-47 / R-R3-22: the accessory record verbs came with
        // accessoryDataVersion 1, in the same minor-11 block.
        if ((message.commandVerb == "setTxInterlockPolicy"
             || message.commandVerb == "setPgxlPowerCap"
             || message.commandVerb == "clearAccessoryFaults")
            && (it->agreedMinor < kRadioIdentitySessionProtocolMinor
                || accessoryDataVersion() < 1)) {
            send(transport, SessionMessages::commandResult(
                message.commandVerb, message.commandId, false,
                it->agreedMinor < kRadioIdentitySessionProtocolMinor
                    ? QStringLiteral("Update this app to change the station's amplifier and "
                                     "tuner settings on this Core.")
                    : QStringLiteral("This Core cannot change its amplifier and tuner settings."), {}));
            break;
        }
        // R-R3-47 / R-R3-22: the amp's own settings came with
        // remotePgxlControlVersion 3 and the tuner's with
        // remoteTgxlControlVersion 1, in the same minor-11 block.
        if (isPgxlDeviceSettingsVerb(message.commandVerb)
            && (it->agreedMinor < kRadioIdentitySessionProtocolMinor
                || pgxlControlVersion() < 3)) {
            send(transport, SessionMessages::commandResult(
                message.commandVerb, message.commandId, false,
                it->agreedMinor < kRadioIdentitySessionProtocolMinor
                    ? QStringLiteral("Update this app to change the Power Genius's own settings "
                                     "on this Core.")
                    : QStringLiteral("This Core cannot change its amplifier and tuner settings."), {}));
            break;
        }
        if (isTgxlDeviceSettingsVerb(message.commandVerb)
            && (it->agreedMinor < kRadioIdentitySessionProtocolMinor
                || tgxlControlVersion() < 1)) {
            send(transport, SessionMessages::commandResult(
                message.commandVerb, message.commandId, false,
                it->agreedMinor < kRadioIdentitySessionProtocolMinor
                    ? QStringLiteral("Update this app to change the Tuner Genius's own settings "
                                     "on this Core.")
                    : QStringLiteral("This Core cannot change its amplifier and tuner settings."), {}));
            break;
        }
        // R-R3-49 / R-R3-47: the tuner's antenna, operate and bypass came
        // with remoteTgxlControlVersion 2, in the same minor-11 block.
        if (isTgxlControlVerb(message.commandVerb)
            && (it->agreedMinor < kRadioIdentitySessionProtocolMinor
                || tgxlControlVersion() < 2)) {
            send(transport, SessionMessages::commandResult(
                message.commandVerb, message.commandId, false,
                it->agreedMinor < kRadioIdentitySessionProtocolMinor
                    ? QStringLiteral("Update this app to switch the Tuner Genius on this Core.")
                    : QStringLiteral("This Core cannot change its amplifier and tuner settings."), {}));
            break;
        }
        // R-R3-49 (parity Task 2): the Tune Power slider's command came
        // with transmitSettingsVersion 2, in the same minor-11 block.
        if (message.commandVerb == "setTunePowerForTxBand"
            && (it->agreedMinor < kRadioIdentitySessionProtocolMinor
                || transmitSettingsVersion() < 2)) {
            send(transport, SessionMessages::commandResult(
                message.commandVerb, message.commandId, false,
                it->agreedMinor < kRadioIdentitySessionProtocolMinor
                    ? QStringLiteral("Update this app to change the tune power on this Core.")
                    : QStringLiteral("This Core cannot change its transmit settings."), {}));
            break;
        }
        // R-IOS-27, R-IOS-06: a slice's band buttons came with
        // bandSelectVersion 1, in the minor-11 block.
        if (message.commandVerb == "slice.selectBand"
            && (it->agreedMinor < kRadioIdentitySessionProtocolMinor
                || bandSelectVersion() < 1)) {
            send(transport, SessionMessages::commandResult(
                message.commandVerb, message.commandId, false,
                it->agreedMinor < kRadioIdentitySessionProtocolMinor
                    ? QStringLiteral("Update this app to change bands on this Core.")
                    : QStringLiteral("This Core cannot change bands for an app."), {}));
            break;
        }
        // R-R3-49 (parity Task 3): the TX profile verbs and the RADE vocoder
        // reset came with transmitSettingsVersion 3, in the minor-11 block.
        if ((message.commandVerb.startsWith("txProfile.")
             || message.commandVerb == "rade.resetVocoder")
            && (it->agreedMinor < kRadioIdentitySessionProtocolMinor
                || transmitSettingsVersion() < 3)) {
            send(transport, SessionMessages::commandResult(
                message.commandVerb, message.commandId, false,
                it->agreedMinor < kRadioIdentitySessionProtocolMinor
                    ? QStringLiteral("Update this app to change transmit profiles on this Core.")
                    : QStringLiteral("This Core cannot change its transmit settings."), {}));
            break;
        }
        if ((message.commandVerb == "configureTgxl" || message.commandVerb == "disconnectTgxl")
            && it->agreedMinor < kRemoteTgxlConfigSessionProtocolMinor) {
            send(transport, SessionMessages::commandResult(
                message.commandVerb, message.commandId, false,
                QStringLiteral("Update this app to set up the Tuner Genius XL on this Core."), {}));
            break;
        }
        if ((message.commandVerb == "requestStreamCtunPinned"
             || message.commandVerb == "requestStreamCentre")
            && it->agreedMinor < kRemoteCtunSessionProtocolMinor) {
            send(transport, SessionMessages::commandResult(
                message.commandVerb, message.commandId, false,
                QStringLiteral("Update this app to use C-Tune on this Core."), {}));
            break;
        }
        // iPhone app Task 13 (R-IOS-08): the device administration verbs
        // came with deviceAdminVersion 1, for a device at minor 11 that
        // declares deviceAuth (the peers the `devices` object goes to).
        if (isDeviceAdminVerb(message.commandVerb)
            && (it->agreedMinor < kRadioIdentitySessionProtocolMinor
                || !peerDeclares(transport, QByteArrayLiteral("deviceAuth"), 1)
                || deviceAdminVersion() < 1)) {
            send(transport, SessionMessages::commandResult(
                message.commandVerb, message.commandId, false,
                deviceAdminVersion() < 1
                    ? QStringLiteral("This Core cannot manage its paired devices.")
                    : QStringLiteral("Update this app to manage this Core's paired devices."),
                {}));
            break;
        }
        // iPhone app Task 14 (R-IOS-08): the pairing window's verbs came
        // with pairingVersion 1, for the same peers.
        if (isPairingVerb(message.commandVerb)
            && (it->agreedMinor < kRadioIdentitySessionProtocolMinor
                || !peerDeclares(transport, QByteArrayLiteral("deviceAuth"), 1)
                || pairingVersion() < 1)) {
            send(transport, SessionMessages::commandResult(
                message.commandVerb, message.commandId, false,
                pairingVersion() < 1
                    ? QStringLiteral("This Core cannot pair new devices.")
                    : QStringLiteral("Update this app to pair new devices with this Core."),
                {}));
            break;
        }
        // Part C follow-up (R-IOS-08): only a device signed in with its own
        // key reopens pairing. A window signed in with the pairing token is
        // refused before the dispatcher sees the verb; pairing.close stays
        // open to it, since closing only narrows who can pair.
        if (message.commandVerb == "pairing.open" && !peerSeesPairingCode(transport)) {
            send(transport, SessionMessages::commandResult(
                message.commandVerb, message.commandId, false,
                QStringLiteral("Open pairing from a paired device or from the Core's console."),
                {}));
            break;
        }
        {
            // A revoke of the requester's own device, or a token session
            // retiring the token, ends this connection only after its
            // result (sent synchronously by dispatch()) has gone out.
            m_dispatchingTransport = transport;
            m_pendingEnd.reset();
            m_resultSentInDispatch = false;
            // iPhone app Task 72 (ruling 5.8): the command acts for this
            // session, so a DSP-asset job it starts is this device's.
            m_dispatcher->setSessionOwner(sessionOwner(m_peers.value(transport).sessionId));
            // iPhone app Task 73 (rulings 5.9, 5.10): and for this device,
            // whose slices it may name and whose active slice it sets.
            m_dispatcher->setRequester(m_peers.value(transport).sessionDeviceId);
            // iPhone app Task 74: a receiver change may be the anchor's
            // (rulings 6.3, 6.4, 6.6) or a take (section 6.4).
            // iPhone app Task 75: a setting that affects every device
            // (the several-devices design, 7.1) is asked first.
            if (!handleSharedSetting(transport, message)
                && !handleReceiverCommand(transport, message)) {
                m_dispatcher->dispatch(message);
            }
            sendHeldQuestions();
            m_dispatcher->setRequester({});
            m_dispatcher->setSessionOwner({});
            m_dispatchingTransport = nullptr;
            // iPhone app Task 71: a result still owed (it arrives on a later
            // turn), or a PureSignal action's later phases, goes to this
            // session, not to whichever session holds media.
            if (!m_resultSentInDispatch || message.commandVerb.startsWith("ps3.")) {
                m_resultRoutes.insert(
                    ResultKey{m_peers.value(transport).sessionId, message.commandVerb,
                              message.commandId},
                    QPointer<SessionTransport>(transport));
            }
            if (m_pendingEnd) {
                const auto [reason, code] = *m_pendingEnd;
                m_pendingEnd.reset();
                dropPeer(transport, reason, true, /*retryable=*/false, code);
                return;
            }
            // Ruling 4.12: the device left on purpose. Its place is free at
            // once, with no away state; the accepted result has gone out,
            // and the Core closes the connection (no session.end: the
            // result already said it).
            const auto leaver = m_peers.constFind(transport);
            if (leaver != m_peers.cend() && leaver->leaving) {
                dropPeer(transport, QString::fromLatin1(kLeftReason), false,
                         /*retryable=*/false);
                return;
            }
        }
        break;
    case SessionMessageKind::MediaControl: {
        // iPhone app Task 76 (the link, section 11): from each admitted
        // session, for its own media.
        const quint64 epoch = m_peers.value(transport).mediaEpoch;
        if (epoch != 0 && mediaAvailable(epoch)) {
            emit mediaControlReceived(message.mediaPayload, epoch);
        }
        break;
    }
    case SessionMessageKind::PropertyWrite:
        handlePropertyWrite(transport, message);
        break;
    case SessionMessageKind::SettingsWrite:
        handleSettingsWrite(transport, message);
        break;
    case SessionMessageKind::SettingsRemove:
        handleSettingsRemove(transport, message);
        break;
    default:
        // Every remaining kind is daemon-to-client. A client sending one
        // is confused rather than hostile, so it is logged and ignored
        // rather than being grounds to close a working session.
        qCWarning(lcStation) << "Ignoring client message of daemon-only kind:"
                             << SessionMessages::kindName(message.kind);
        break;
    }
}

void StationServer::handleHello(SessionTransport* transport, const SessionMessage& message)
{
    auto it = m_peers.find(transport);
    if (it == m_peers.end()) {
        return;
    }
    if (it->helloReceived) {
        dropPeer(transport, QStringLiteral("This app started connecting twice on one connection."), true, /*retryable=*/false,
                 QString::fromLatin1(SessionEndCode::kProtocolError));
        return;
    }
    it->helloReceived = true;

    // Parent design section 7.0's version policy, both halves, with the
    // iPhone app spec's D23 and D39 (R-IOS-01): the client's hello names
    // the major it chose from this station's list, and the station accepts
    // exactly that. A client that shares no major with this station (two
    // or more apart) has chosen one outside the list, or is an older app
    // on another major, and is refused naming both sides' versions.
    if (!m_supportedMajors.contains(message.protocolMajor)) {
        const QString reason =
            SessionEndReasons::versionRefused(m_supportedMajors, message.supportedMajors);
        qCWarning(lcStation) << "Refusing a client on link major" << message.protocolMajor
                             << "(it supports" << message.supportedMajors
                             << "; this station supports" << m_supportedMajors << "):"
                             << reason;
        // NOT retryable: an incompatible wire contract does not become
        // compatible by being dialed again. The operator has to upgrade
        // one end.
        dropPeer(transport, reason, true, /*retryable=*/false,
                 QString::fromLatin1(SessionEndCode::kLinkVersion));
        return;
    }
    it->agreedMajor = message.protocolMajor;
    it->features = message.features;

    // Equal major, differing minor: negotiate DOWN to the lower of the
    // two. A desktop GUI several releases ahead of a Pi still running this
    // one is the EXPECTED case, and it degrades rather than refusing.
    it->agreedMinor = std::min(kSessionProtocolMinor, message.protocolMinor);

    if (message.settingsSchemaVersion != settingsSchemaVersionOf(m_settings)) {
        // Not a refusal. The settings schema governs how each side's own
        // local store is shaped, not the wire contract, and the client is
        // the side that has to decide what to do about it (see
        // StationClient's own skew check). Logged here so a bench session
        // shows the skew from both ends.
        qCWarning(lcStation) << "Settings schema skew: station is at"
                             << settingsSchemaVersionOf(m_settings) << "client is at"
                             << message.settingsSchemaVersion;
    }

    qCDebug(lcStation) << "Hello from" << message.peerName << "version"
                       << message.protocolMajor << "." << message.protocolMinor
                       << "agreed major" << it->agreedMajor << "agreed minor"
                       << it->agreedMinor << "declares" << it->features.keys();
}

void StationServer::handleAuthRequest(SessionTransport* transport,
                                      const SessionMessage& message)
{
    auto it = m_peers.find(transport);
    if (it == m_peers.end()) {
        return;
    }
    if (!it->helloReceived) {
        dropPeer(transport, QStringLiteral("This app sent its pairing token out of order."), true, /*retryable=*/false,
                 QString::fromLatin1(SessionEndCode::kProtocolError));
        return;
    }
    // iPhone app Task 14: a pairing connection never signs in; the device
    // signs in on a new connection once paired.
    if (it->authenticated || it->pairing) {
        dropPeer(transport, QStringLiteral("This app sent its pairing token out of order."), true, /*retryable=*/false,
                 QString::fromLatin1(SessionEndCode::kProtocolError));
        return;
    }

    const QString description = it->description;
    const QByteArray challenge = it->challenge;
    const QString address = transport->peerAddress();

    // One refusal shape for every path below: auth.result carrying the
    // reason, the retry advice and the end code, then the close. Nothing
    // here ever logs a candidate token, a signature or a key.
    const auto refuse = [this, transport, &description](const QString& reason, bool retryable,
                                                        const char* code) {
        const QString endCode = code != nullptr ? QString::fromLatin1(code) : QString();
        send(transport, SessionMessages::authResult(false, reason, retryable, endCode));
        qCWarning(lcStation) << "Authentication refused for" << description << ":" << reason;
        dropPeer(transport, reason, false, retryable);
    };

    // iPhone app Task 12 (R-IOS-08): what a device sends, as the
    // authenticator reads it. The address is the peer's own; over the
    // relay (Part E) it is empty and the limits key on the introduction.
    DeviceAuthRequest request;
    if (message.device) {
        request.id = message.device->id;
        request.publicKey = message.device->publicKey;
        request.name = message.device->name;
        request.kind = message.device->kind;
        request.signature = message.device->signature;
        request.sourceAddress = address;
    }

    QByteArray deviceId;
    if (message.device && message.token.isEmpty()) {
        // ── A paired device signing in with its own key ──
        //
        // The device limiter only: the token's limiter is never consulted,
        // so wrong tokens from anyone cannot lock out a device's key, and
        // this path's failures never count against the token.
        const AuthOutcome outcome = m_deviceAuth->verify(request, challenge, m_certSha256);
        switch (outcome.result) {
        case AuthOutcome::Result::Admitted:
            deviceId = outcome.deviceId;
            break;
        case AuthOutcome::Result::RateLimited:
            // Retryable: it clears by itself after the lockout.
            refuse(QStringLiteral("The Core is refusing sign-ins from this device for a while after too many failed ones. Try again later."),
                   /*retryable=*/true, nullptr);
            return;
        case AuthOutcome::Result::NotPaired:
            refuse(QStringLiteral("This device is not paired with this Core. Pair it first."),
                   /*retryable=*/false, SessionEndCode::kDeviceNotPaired);
            return;
        case AuthOutcome::Result::Proved:
        case AuthOutcome::Result::ProofFailed:
            refuse(QStringLiteral("This device could not prove it is paired with this Core."),
                   /*retryable=*/false, SessionEndCode::kDeviceProofFailed);
            return;
        }
    } else {
        // ── The pairing token (a window from before paired devices) ──
        //
        // A Core with no token (a new one, or one whose token was retired)
        // has nothing to check it against: the window has to pair. Refused
        // before the token's limiter, which a token that cannot succeed
        // must not feed.
        if (!m_tokens->isActive()) {
            refuse(QStringLiteral("This Core uses paired devices. Pair this device first."),
                   /*retryable=*/false, SessionEndCode::kPairingRequired);
            return;
        }
        const TokenStore::VerifyResult result = m_tokens->verify(message.token);
        if (result != TokenStore::VerifyResult::Accepted) {
            // THE distinction TokenStore.h says the two results exist to
            // preserve, carried through to the client's retry policy.
            //
            // RateLimited is retryable: it is transient BY CONSTRUCTION --
            // the lockout expires on TokenStore's own timer, and the
            // refusal text literally says "try again later". Crucially, the
            // rate limiter is global rather than per-peer (TokenStore.h:44-
            // 48 says so outright: a lockout refuses a connection
            // "including one carrying the correct token"), so five bad
            // guesses from anyone who can reach the port refuse the
            // OPERATOR's token too. Marked permanent, that turned somebody
            // else's failed guesses into the operator being locked out of
            // their own station with no automatic recovery. (A paired
            // device's key is not refused by it: see above.)
            //
            // Rejected is NOT retryable: the token is simply wrong,
            // redialing cannot make it right, and a client that retried
            // forever would feed the very rate limiter above and keep the
            // station locked out on the operator's own behalf.
            if (result == TokenStore::VerifyResult::RateLimited) {
                refuse(QStringLiteral("The Core is refusing pairing tokens for a while after too many wrong ones. Try again later."),
                       /*retryable=*/true, nullptr);
            } else {
                refuse(QStringLiteral("The Core did not accept this app's pairing token. Check the token saved for this Core."),
                       /*retryable=*/false, SessionEndCode::kWrongToken);
            }
            return;
        }
        if (message.device) {
            // The token vouched for the connection; the device block has
            // to prove its key signed THIS connection's transcript before
            // the key is enrolled, so a token holder cannot enrol a key it
            // does not hold. Enrolled once, as a computer, and from then on
            // the window signs in with its key, typing nothing.
            const AuthOutcome proof =
                m_deviceAuth->verifyPossession(request, challenge, m_certSha256);
            if (proof.result != AuthOutcome::Result::Proved) {
                refuse(QStringLiteral("This device could not prove it is paired with this Core."),
                       /*retryable=*/false, SessionEndCode::kDeviceProofFailed);
                return;
            }
            deviceId = proof.deviceId;
            if (!m_devices->find(deviceId)) {
                PairedDevice device;
                device.id = proof.deviceId;
                device.publicKeySpki = proof.publicKeySpki;
                device.name = DeviceStore::isValidName(message.device->name)
                                  ? message.device->name
                                  : QStringLiteral("Computer");
                device.kind = QStringLiteral("computer");
                device.enrolledThroughToken = true;
                // Part C fix wave: its short name, when it sent a usable one.
                if (DeviceStore::isValidShortName(message.device->shortName)) {
                    device.shortName = message.device->shortName;
                }
                if (m_devices->add(device)) {
                    qCInfo(lcStation) << "Enrolled the device key of" << description
                                      << "signing in with the pairing token";
                } else {
                    // The token already admitted the window; not being able
                    // to write the list must not lock it out. It enrols on
                    // a later sign-in.
                    qCWarning(lcStation) << "Could not enrol the device key of" << description;
                    deviceId.clear();
                }
            }
        }
    }

    it = m_peers.find(transport);
    if (it == m_peers.end()) {
        return;
    }
    it->authenticated = true;
    it->deviceId = deviceId;
    // iPhone app Task 13: a token sign-in, enrolled or not, ends when the
    // token is retired.
    it->signedInWithToken = !(message.device && message.token.isEmpty());
    // One change to the devices object for this sign-in, not two; and one
    // to connectedDevices (a new short name, then the admission).
    m_devicesFacade->holdRefresh();
    m_connectedDevices->holdRefresh();
    QString name;
    QString shortName;
    QString kind = QStringLiteral("computer");
    if (!deviceId.isEmpty()) {
        // lastSeen and lastAddress, on every authenticated connection, and
        // the short name the device sent this time (Part C fix wave: it
        // replaces the stored one when usable; outside the signed transcript).
        m_devices->touch(deviceId, address,
                         message.device ? message.device->shortName : QString());
        if (const std::optional<PairedDevice> paired = m_devices->find(deviceId)) {
            name = paired->name;
            shortName = paired->shortName;
            kind = paired->kind;
        }
    } else {
        // Ruling 4.1: a window signed in with the older token and no key is
        // a device for the life of its session, named by its address.
        name = DeviceSessionRegistry::tokenWindowName(address);
    }
    send(transport, SessionMessages::authResult(true, QString(), /*retryable=*/false));
    // admit() ends the hold (resumeRefresh) once the device's connected
    // flag is set too, before the snapshot goes out.
    admit(transport, name, shortName, kind);
}

void StationServer::admit(SessionTransport* transport, const QString& name,
                          const QString& shortName, const QString& kind)
{
    // The devices object's hold from handleAuthRequest ends here on every
    // path: one change for a sign-in, as before Task 71.
    auto resumeDevices = qScopeGuard([this]() {
        m_devicesFacade->resumeRefresh();
        m_connectedDevices->resumeRefresh();
    });
    auto it = m_peers.find(transport);
    if (it == m_peers.end()) {
        return;
    }
    // iPhone app Task 71 (rulings 4.4 and 4.8), after auth.result accepted.
    DeviceSessionRegistry::Entry device;
    if (!it->deviceId.isEmpty()) {
        device.deviceId = it->deviceId;
        device.kind = DeviceSessionRegistry::Kind::Paired;
    } else {
        device.deviceId = m_deviceSessions->nextTokenDeviceId();
        device.kind = DeviceSessionRegistry::Kind::Token;
    }
    device.name = name;
    device.shortName = shortName;
    device.deviceKind = kind;
    const QString description = it->description;
    const DeviceSessionRegistry::AdmitResult result = m_deviceSessions->admit(device, transport);

    switch (result.admission) {
    case DeviceSessionRegistry::Admission::Full:
        // Every place is taken. Retryable, with no code: the device tries
        // again on its own backoff, and a place may free meanwhile. From
        // Task 41 a device that declared sessionHolder is asked the
        // fifth-device question instead; an older window keeps this.
        qCInfo(lcStation) << "Turning away" << description << ": every place on the Core is taken";
        if (peerHoldsSessions(transport)) {
            dropPeer(transport, QString::fromLatin1(kCoreFullReason), true, /*retryable=*/true);
        } else {
            dropPeer(transport, QString::fromLatin1(kOlderWindowCoreFullReason), true,
                     /*retryable=*/true);
        }
        return;
    case DeviceSessionRegistry::Admission::SameDevice: {
        // Ruling 4.8: the device's own older connection ends at once, with
        // no question; its place stays the device's. No other session is
        // touched.
        auto* older = const_cast<SessionTransport*>(
            qobject_cast<const SessionTransport*>(result.replacedSession));
        const auto olderPeer = older != nullptr ? m_peers.find(older) : m_peers.end();
        if (olderPeer != m_peers.end()) {
            olderPeer->placeSettled = true;
            qCInfo(lcStation) << "The device of" << olderPeer->description
                              << "connected again as" << description;
            // NOT retryable: a client that redialled would replace the newer
            // connection of its own device, and the two would trade places.
            dropPeer(older, QString::fromLatin1(kSameDeviceReason), true, /*retryable=*/false,
                     QString::fromLatin1(SessionEndCode::kSameDevice));
        }
        break;
    }
    case DeviceSessionRegistry::Admission::Admitted:
        break;
    }

    it = m_peers.find(transport);
    if (it == m_peers.end()) {
        return;
    }
    it->sessionDeviceId = device.deviceId;
    // iPhone app Task 73 (ruling 4.8): a device back to its own place keeps
    // its slices, pans and active slice as they are.
    it->returning = result.admission == DeviceSessionRegistry::Admission::SameDevice;
    publishConnectedDevices();
    // iPhone app Task 73 (ruling 5.2): the device's slices, before its own
    // burst so the burst carries them, and within the held refresh so its
    // sign-in is still one change to `connectedDevices`. Other views see
    // them as markers.
    if (!it->returning) {
        placeSlicesForAdmission(device.deviceId);
        if (!m_peers.contains(transport)) {
            return;
        }
        // iPhone app Task 74 (ruling 10.2): an older window needs a slice
        // of its own; with none to give it, it is refused, retryable, in
        // words that name the limit that is full.
        const QString noSlice = olderWindowWithoutSliceReason(transport);
        if (!noSlice.isEmpty()) {
            m_peers[transport].leaving = true;
            dropPeer(transport, noSlice, true, /*retryable=*/true);
            return;
        }
    }
    resumeDevices.dismiss();
    m_devicesFacade->resumeRefresh();
    m_connectedDevices->resumeRefresh();
    promoteToSession(transport);
    // iPhone app Task 74 (7.4): after snapshot.complete, graceEnded or
    // slicesNotRestored, then the notices that waited while it was away.
    if (m_peers.contains(transport)) {
        deliverAdmissionNotices(transport, result.timeRanOutAtMs);
    }
}

// ── Pairing (iPhone app Task 14, R-IOS-08) ───────────────────────────────
//
// A device that is not paired sends pair.start after its hello instead of
// auth.request (the link document's Pairing section). Nothing on a pairing
// connection signs in: it ends after pair.accept, the Core's pair.confirm,
// or pair.fail, and the device then signs in by key on a new connection.
//
//   lan   one tap: only while the Core is unclaimed, only when
//         pairing_lan_click allows it, and only from an address on one of
//         the Core's directly connected networks. The device is added and
//         pair.accept carries the Core's identity and label.
//   code  SPAKE2+EE over the window's current code. The Core sends step 0,
//         takes the code when the device's step 1 arrives (from there it
//         is paired with or burned), answers step 2, checks step 3 (step
//         4), opens the device's box (its key, name and kind, which win
//         over pair.start's name and kind, and its key must be the one
//         pair.start named), adds the device and answers with its own box
//         (identity and label).
//
// Nothing here logs the code, a key or a box.

void StationServer::sendPairFail(SessionTransport* transport, const QString& reason,
                                 qint64 retryAfterMs)
{
    send(transport, SessionMessages::pairFail(reason, std::max<qint64>(0, retryAfterMs)));
    qCInfo(lcStation) << "Pairing refused for" << m_peers.value(transport).description << ":"
                      << reason;
    // A code this connection took is burned here (dropPeer).
    dropPeer(transport, reason, false, /*retryable=*/false);
}

void StationServer::startPairingHash()
{
    // One hash at a time; a finished one for an old code starts the next.
    if (m_pairingHashThread) {
        return;
    }
    const QString code = m_pairingWindow->currentCode();
    if (code.isEmpty()) {
        return;
    }
    const quint64 serial = m_pairingWindow->codeSerial();
    m_pairingHashSerial = serial;
    auto result = std::make_shared<QByteArray>();
    // The code and the hash cross to the worker and back only in memory,
    // never in a log line; the worker's copy of the code is overwritten
    // once it is hashed.
    auto codeCopy = std::make_shared<QString>(code);
    auto* worker = QThread::create([result, codeCopy, hasher = m_pairingHasher]() {
        *result = hasher(*codeCopy);
        codeCopy->fill(QChar(u'\0'));
    });
    worker->setObjectName(QStringLiteral("StationPairingHash"));
    m_pairingHashThread.reset(worker);
    connect(worker, &QThread::finished, this, [this, worker, serial, result]() {
        if (m_pairingHashThread.get() != worker) {
            return;
        }
        worker->wait();
        m_pairingHashThread.reset();
        finishPairingHash(serial, *result);
        SpakeExchange::wipe(*result);
    });
    worker->start();
}

void StationServer::finishPairingHash(quint64 serial, const QByteArray& stored)
{
    // Kept only while its code is still the window's current one.
    if (serial == m_pairingWindow->codeSerial() && !m_pairingWindow->currentCode().isEmpty()
        && !stored.isEmpty()) {
        SpakeExchange::wipe(m_pairingStored);
        m_pairingStored = stored;
        m_pairingStoredSerial = serial;
    }
    bool anotherNeeded = false;
    const QList<SessionTransport*> transports = m_peers.keys();
    for (SessionTransport* transport : transports) {
        const auto it = m_peers.constFind(transport);
        if (it == m_peers.cend() || !it->pairing || !it->pairing->awaitingHash) {
            continue;
        }
        const std::shared_ptr<PairingAttempt> attempt = it->pairing;
        if (attempt->codeSerial == m_pairingStoredSerial && !m_pairingStored.isEmpty()) {
            attempt->awaitingHash = false;
            beginCodeExchange(transport);
        } else if (attempt->codeSerial != m_pairingWindow->codeSerial()) {
            attempt->awaitingHash = false;
            sendPairFail(transport,
                         QStringLiteral("The pairing code changed. Enter the code the Core "
                                        "shows now."),
                         m_pairingWindow->retryAfterMs());
        } else if (serial == attempt->codeSerial) {
            // Its own code's hash came back empty.
            attempt->awaitingHash = false;
            sendPairFail(transport, QStringLiteral("This Core cannot pair new devices."), 0);
        } else {
            anotherNeeded = true;
        }
    }
    if (anotherNeeded) {
        startPairingHash();
    }
}

void StationServer::beginCodeExchange(SessionTransport* transport)
{
    const auto it = m_peers.constFind(transport);
    if (it == m_peers.cend() || !it->pairing) {
        return;
    }
    const std::shared_ptr<PairingAttempt> attempt = it->pairing;
    attempt->exchange = std::make_unique<SpakeExchange>(SpakeExchange::Role::Station);
    const QByteArray step0 = attempt->exchange->stationStep0(m_pairingStored);
    if (step0.isEmpty()) {
        sendPairFail(transport, QStringLiteral("This Core cannot pair new devices."), 0);
        return;
    }
    attempt->expecting = 1;
    send(transport, SessionMessages::pairSpake(0, StationIdentity::toBase64Url(step0)));
}

#ifdef NEREUS_BUILD_TESTS
void StationServer::setPairingHasherForTest(std::function<QByteArray(const QString&)> hasher)
{
    m_pairingHasher = hasher ? std::move(hasher) : &SpakeExchange::storedData;
}

bool StationServer::isHashingPairingCodeForTest() const
{
    return m_pairingHashThread != nullptr;
}
#endif

void StationServer::handlePairStart(SessionTransport* transport, const SessionMessage& message)
{
    auto it = m_peers.find(transport);
    if (it == m_peers.end()) {
        return;
    }
    if (!it->helloReceived || it->authenticated || it->pairing) {
        dropPeer(transport, QStringLiteral("This app started pairing out of order."), true,
                 /*retryable=*/false, QString::fromLatin1(SessionEndCode::kProtocolError));
        return;
    }
    auto attempt = std::make_shared<PairingAttempt>();
    it->pairing = attempt;
    const QString address = transport->peerAddress();

    if (pairingVersion() < 1) {
        sendPairFail(transport, QStringLiteral("This Core cannot pair new devices."), 0);
        return;
    }
    // The device, as pair.start names it. In code mode the name and kind
    // inside its box win; its key must be this one.
    const SessionPairDevice device = message.pairDevice.value_or(SessionPairDevice{});
    bool keyOk = false;
    attempt->publicKeyText = device.publicKey;
    attempt->publicKeySpki = StationIdentity::fromBase64Url(device.publicKey, &keyOk);
    attempt->name = device.name;
    attempt->kind = device.kind;
    if (!keyOk || !StationIdentity::isP256Spki(attempt->publicKeySpki)
        || !DeviceStore::isValidName(attempt->name) || !DeviceStore::isKnownKind(attempt->kind)) {
        sendPairFail(transport,
                     QStringLiteral("The Core could not read this device's details. Update "
                                    "this app."),
                     0);
        return;
    }
    if (m_devices->find(StationIdentity::fingerprintOf(attempt->publicKeySpki))) {
        sendPairFail(transport,
                     QStringLiteral("This device is already paired with this Core. Connect "
                                    "to it instead."),
                     0);
        return;
    }
    if (!m_pairingWindow->isOpen()) {
        sendPairFail(transport,
                     QStringLiteral("This Core is not taking new devices. Open pairing on the "
                                    "Core or on a paired device first."),
                     0);
        return;
    }

    if (message.pairMode == QLatin1String("lan")) {
        // ── One tap ──
        if (m_pairingWindow->state() != PairingWindow::State::OpenUnclaimed) {
            sendPairFail(transport,
                         QStringLiteral("One tap pairs only a Core with no paired devices. Use "
                                        "the pairing code the Core shows."),
                         0);
            return;
        }
        if (!m_pairingLanClickAllowed) {
            sendPairFail(transport,
                         QStringLiteral("This Core pairs only with its code. Use the pairing "
                                        "code the Core shows."),
                         0);
            return;
        }
        if (!isOnDirectNetwork(address)) {
            sendPairFail(transport,
                         QStringLiteral("One tap works only on the Core's own network. Use the "
                                        "pairing code the Core shows."),
                         0);
            return;
        }
        PairedDevice paired;
        paired.id = StationIdentity::fingerprintOf(attempt->publicKeySpki);
        paired.publicKeySpki = attempt->publicKeySpki;
        paired.name = attempt->name;
        paired.kind = attempt->kind;
        paired.lastAddress = address;
        if (!m_devices->add(paired)) {
            sendPairFail(transport,
                         QStringLiteral("The Core could not save this device. Try again."), 0);
            return;
        }
        attempt->finished = true;
        m_pairingWindow->pairingSucceeded();
        qCInfo(lcStation) << "Paired a device by one tap from" << it->description;
        send(transport,
             SessionMessages::pairAccept(
                 SessionStationIdentity{StationIdentity::toBase64Url(m_identity->publicKeySpki()),
                                        StationIdentity::toBase64Url(m_certBinding)},
                 m_devicesFacade->stationLabel()));
        dropPeer(transport, QStringLiteral("This device is paired with the Core."), false,
                 /*retryable=*/false);
        return;
    }

    // ── The code ──
    attempt->codeMode = true;
    if (m_pairingWindow->codeInUse()) {
        sendPairFail(transport,
                     QStringLiteral("Another device is pairing with this Core right now. Try "
                                    "again shortly."),
                     m_pairingWindow->retryAfterMs());
        return;
    }
    if (m_pairingWindow->currentCode().isEmpty()) {
        sendPairFail(transport,
                     QStringLiteral("The Core is waiting before it shows a new pairing code. "
                                    "Try again when the new code appears."),
                     m_pairingWindow->retryAfterMs());
        return;
    }
    attempt->codeSerial = m_pairingWindow->codeSerial();
    if (m_pairingStoredSerial == attempt->codeSerial && !m_pairingStored.isEmpty()) {
        beginCodeExchange(transport);
        return;
    }
    // The code is hashed once (Argon2id) on a worker, never on this event
    // loop, so no other device's session waits on it; step 0 follows when
    // the hash is back (finishPairingHash).
    attempt->awaitingHash = true;
    startPairingHash();
}

void StationServer::handlePairSpake(SessionTransport* transport, const SessionMessage& message)
{
    auto it = m_peers.find(transport);
    if (it == m_peers.end()) {
        return;
    }
    const std::shared_ptr<PairingAttempt> attempt = it->pairing;
    if (!attempt || !attempt->codeMode || !attempt->exchange || attempt->finished
        || attempt->awaitingHash || message.pairStep != attempt->expecting) {
        dropPeer(transport, QStringLiteral("This app sent a pairing step out of order."), true,
                 /*retryable=*/false, QString::fromLatin1(SessionEndCode::kProtocolError));
        return;
    }
    bool ok = false;
    const QByteArray data = StationIdentity::fromBase64Url(message.pairData, &ok);

    if (attempt->expecting == 1) {
        // The Core commits to the code here: the window gives it to this
        // exchange once, and from now on it is paired with or burned.
        if (m_pairingStoredSerial != attempt->codeSerial || m_pairingStored.isEmpty()) {
            sendPairFail(transport,
                         QStringLiteral("The pairing code changed. Enter the code the Core "
                                        "shows now."),
                         m_pairingWindow->retryAfterMs());
            return;
        }
        QByteArray stored = m_pairingStored;
        if (!m_pairingWindow->takeCode(attempt->codeSerial)) {
            SpakeExchange::wipe(stored);
            sendPairFail(transport,
                         QStringLiteral("Another device is pairing with this Core right now. "
                                        "Try again shortly."),
                         m_pairingWindow->retryAfterMs());
            return;
        }
        attempt->codeTaken = true;
        const QByteArray step2 =
            ok ? attempt->exchange->stationStep2(stored, data) : QByteArray();
        SpakeExchange::wipe(stored);
        if (step2.isEmpty()) {
            attempt->finished = true;
            m_pairingWindow->pairingFailed();
            sendPairFail(transport,
                         QStringLiteral("The pairing code was not accepted. A new code will "
                                        "appear on the Core."),
                         m_pairingWindow->retryAfterMs());
            return;
        }
        attempt->expecting = 3;
        send(transport, SessionMessages::pairSpake(2, StationIdentity::toBase64Url(step2)));
        return;
    }

    // Step 3: the device shows it held the same code (step 4).
    if (!ok || !attempt->exchange->stationStep4(data)) {
        attempt->finished = true;
        m_pairingWindow->pairingFailed();
        sendPairFail(transport,
                     QStringLiteral("The pairing code was not right. A new code will appear on "
                                    "the Core."),
                     m_pairingWindow->retryAfterMs());
        return;
    }
    attempt->expecting = 4;   // its box
}

void StationServer::handlePairConfirm(SessionTransport* transport, const SessionMessage& message)
{
    auto it = m_peers.find(transport);
    if (it == m_peers.end()) {
        return;
    }
    const std::shared_ptr<PairingAttempt> attempt = it->pairing;
    if (!attempt || !attempt->codeMode || !attempt->exchange || attempt->finished
        || attempt->expecting != 4) {
        dropPeer(transport, QStringLiteral("This app sent a pairing step out of order."), true,
                 /*retryable=*/false, QString::fromLatin1(SessionEndCode::kProtocolError));
        return;
    }
    const auto refuse = [this, transport, &attempt](const QString& reason) {
        attempt->finished = true;
        m_pairingWindow->pairingFailed();
        sendPairFail(transport, reason, m_pairingWindow->retryAfterMs());
    };
    // Part C fix wave (R1-M1): a window that closed (the operator's close,
    // its ten minutes, or a close and reopen) since this exchange took the
    // code pairs nothing; the code is burned.
    if (!m_pairingWindow->holdsCode(attempt->codeSerial)) {
        refuse(QStringLiteral("This Core is not taking new devices. Open pairing on the Core or "
                              "on a paired device first."));
        return;
    }
    bool ok = false;
    const QByteArray box = StationIdentity::fromBase64Url(message.pairBox, &ok);
    const std::optional<QByteArray> plain =
        ok ? attempt->exchange->openConfirmation(box) : std::nullopt;
    const QJsonObject contents =
        plain ? QJsonDocument::fromJson(*plain).object() : QJsonObject{};
    const QString boxKey = contents.value(QStringLiteral("publicKey")).toString();
    const QString name = contents.value(QStringLiteral("name")).toString();
    const QString kind = contents.value(QStringLiteral("kind")).toString();
    // The key the device pairs is the one pair.start named, now vouched for
    // by the code; the name and kind are the box's.
    if (!plain || boxKey != attempt->publicKeyText || !DeviceStore::isValidName(name)
        || !DeviceStore::isKnownKind(kind)) {
        refuse(QStringLiteral("The Core could not read this device's details. Update this "
                              "app."));
        return;
    }
    PairedDevice paired;
    paired.id = StationIdentity::fingerprintOf(attempt->publicKeySpki);
    paired.publicKeySpki = attempt->publicKeySpki;
    paired.name = name;
    paired.kind = kind;
    paired.lastAddress = transport->peerAddress();
    if (!m_devices->add(paired)) {
        refuse(QStringLiteral("The Core could not save this device. Try again."));
        return;
    }
    attempt->finished = true;
    m_pairingWindow->pairingSucceeded();
    qCInfo(lcStation) << "Paired a device by code from" << it->description;
    const QJsonObject station{
        {QStringLiteral("identity"),
         QJsonObject{
             {QStringLiteral("publicKey"),
              StationIdentity::toBase64Url(m_identity->publicKeySpki())},
             {QStringLiteral("certBinding"), StationIdentity::toBase64Url(m_certBinding)},
         }},
        {QStringLiteral("label"), m_devicesFacade->stationLabel()},
    };
    const QByteArray sealed = attempt->exchange->sealConfirmation(
        QJsonDocument(station).toJson(QJsonDocument::Compact));
    send(transport, SessionMessages::pairConfirm(StationIdentity::toBase64Url(sealed)));
    dropPeer(transport, QStringLiteral("This device is paired with the Core."), false,
             /*retryable=*/false);
}

void StationServer::handlePairFailFromDevice(SessionTransport* transport)
{
    auto it = m_peers.find(transport);
    if (it == m_peers.end()) {
        return;
    }
    const std::shared_ptr<PairingAttempt> attempt = it->pairing;
    if (!attempt || !attempt->codeMode || attempt->finished) {
        dropPeer(transport, QStringLiteral("This app sent a pairing step out of order."), true,
                 /*retryable=*/false, QString::fromLatin1(SessionEndCode::kProtocolError));
        return;
    }
    // The device's step 3 failed: the codes differ. Its reason is not read
    // (it is the device's own text). The code is burned, and the device is
    // told when the next one appears.
    if (attempt->codeTaken) {
        attempt->finished = true;
        m_pairingWindow->pairingFailed();
    }
    sendPairFail(transport,
                 QStringLiteral("The pairing code was not right. A new code will appear on "
                                "the Core."),
                 m_pairingWindow->retryAfterMs());
}

void StationServer::promoteToSession(SessionTransport* transport)
{
    const QString description = m_peers.value(transport).description;

    // iPhone app Task 71: no other session is ended here, ever. The remote
    // design's section 7.1 preemption is gone (the several-devices design,
    // section 4): admit() has already decided this device holds a place.

    // BEFORE any session is attached for the first time, deliberately.
    // buildMirror() ends in ObjectRegistry::backfillExistingSlices(), which
    // emits objectCreated for every slice the daemon already holds -- and
    // those are wired straight to sendToEveryView(). With no view yet
    // attached they go nowhere, which is exactly right: attachSession()
    // below sends an object.create for every watched object anyway, AFTER
    // the schema messages that a client needs in order to make sense of
    // one.
    buildMirror();

    // iPhone app Task 76: every admitted session has media, with its own
    // epoch. The first session on a Core with none starts the session
    // state afresh, as the one media session did before.
    if (primaryMediaSession() == nullptr) {
        m_ps3SubscriberEpoch = 0;
        m_radioModel->pureSignalFacade()->resetSession();
        m_dispatcher->resetSessionState();
    }
    ++m_mediaSessionEpoch;
    if (m_mediaSessionEpoch == 0) {
        ++m_mediaSessionEpoch;
    }
    m_peers[transport].mediaEpoch = m_mediaSessionEpoch;
    // Its share of the display budget, in its first capabilities; the
    // others' shrink (new generations, published below once it is in).
    const bool sharesChanged = recomputeDisplayBudgetShares();
    // iPhone app Task 72: the session's own id, for the dispatcher's owner
    // string (station:<sessionId>).
    m_peers[transport].sessionId = ++m_nextSessionId;

    if (!sendCapabilitiesAndSettingsSnapshot(transport)) {
        return;
    }

    // State snapshot, model half, ending in the snapshot-complete marker.
    // iPhone app Task 72 (ruling 5.6): this session's own view, with its
    // own coalescer, so the sessions already admitted keep what they have
    // pending; the burst goes to this session alone, and a change the
    // burst itself causes reaches every view.
    const QPointer<MirrorView> view(new MirrorView(
        m_mirror, [this, transport](const SessionMessage& message) { sendToPeer(transport, message); },
        this));
    m_peers[transport].view = view;
    view->attach();
    if (!m_peers.contains(transport)) {
        return;
    }
    m_peers[transport].snapshotComplete = true;
    // R-R3-16/17: the connect sequence is finished; the deadline stands down.
    if (QTimer* deadline = m_peers[transport].authDeadline) {
        deadline->stop();
    }

    if (!m_deltaFlushTimer->isActive()) {
        m_deltaFlushTimer->start();
    }

    qCInfo(lcStation) << "Session established with" << description;
    emit clientAuthenticated(description);
    const quint64 epoch = m_peers.value(transport).mediaEpoch;
    if (sharesChanged) {
        emit displayBudgetChanged();
    }
    publishBudgetToChangedSessions();
    if (!m_peers.contains(transport)) {
        return;
    }
    if (mediaAvailable(epoch)) {
        emit mediaSessionStarted(epoch);
    }
    if (m_peers.contains(transport) && telemetryAvailable(epoch)) {
        emit telemetrySessionStarted(epoch);
    }
}

// ── Mirror wiring ────────────────────────────────────────────────────────

void StationServer::buildMirror()
{
    if (m_mirrorBuilt || m_radioModel.isNull()) {
        return;
    }
    m_mirrorBuilt = true;

    m_mirror->watch(QByteArray(kRadioKey), m_radioModel.data());
    m_mirror->watch("pureSignalSettings", m_radioModel->pureSignalSettings());
    m_mirror->watch("dspAssets", m_radioModel->dspAssets());
    // R-R3-21 / R-R3-09 (notchControlVersion 1): the Core's notch list. An
    // older app has no object for this key; it records the schema as skew
    // and drops the object and its deltas, as with any newer object.
    m_mirror->watch("notches", m_radioModel->notchModel());
    // R-R3-46 (radioHardwareVersion 1): the Core's step attenuator and
    // preamp. Sent only to a peer at minor 11 (sendToPeer).
    m_mirror->watch(QByteArray(kStepAttKey), m_radioModel->stepAttFacade());
    // R-R3-46 (radioHardwareVersion 2): the Core's Alex antenna settings.
    // Sent only to a peer at minor 11 (sendToPeer).
    m_mirror->watch(QByteArray(kAlexAntennasKey), m_radioModel->alexAntennaFacade());
    // R-R3-46 (radioHardwareVersion 3): the Core's HL2 I/O board, read-only.
    // Sent only to a peer at minor 11 (sendToPeer).
    m_mirror->watch(QByteArray(kIoBoardKey), m_radioModel->ioBoardFacade());
    m_mirror->watch("pureSignal", m_radioModel->pureSignalFacade());
    m_mirror->watch(QByteArray(kTransmitKey), &m_radioModel->transmitModel());
    if (m_radioModel->tunerModel() != nullptr) {
        m_mirror->watch(QByteArray(kTunerKey), m_radioModel->tunerModel());
    }
    // R-R3-47 / R-R3-22 (remotePgxlControlVersion 1,
    // remoteRfKitControlVersion 1): the Core's amplifier and RF-Kit status.
    // Sent only to a peer at minor 11 (sendToPeer).
    m_mirror->watch(QByteArray(kAmplifierKey), m_radioModel->amplifierModel());
    m_mirror->watch(QByteArray(kRfKitKey), m_radioModel->rfKitModel());
    // R-R3-48 (stationTciVersion 1): the Core's station TCI server.
    // Sent only to a peer at minor 11 on a Core that runs one.
    m_mirror->watch(QByteArray(kStationTciKey), m_radioModel->stationTciModel());
    // R-R3-47 / R-R3-22 (accessoryDataVersion 1): the Core's accessory
    // records and settings. Sent only to a peer at minor 11 on a Core that
    // owns its accessories.
    m_mirror->watch(QByteArray(kAccessoryDataKey), m_radioModel->accessoryDataModel());
    // R-R3-47 / R-R3-22 (remotePgxlControlVersion 3, remoteTgxlControlVersion
    // 1): the amp's and tuner's own settings. Sent only to a peer at minor
    // 11 on a Core that owns its accessories.
    m_mirror->watch(QByteArray(kAccessorySettingsKey), m_radioModel->accessorySettingsModel());
    // iPhone app Task 13 (deviceAdminVersion 1): the Core's paired devices.
    // Sent only to a device at minor 11 that declares deviceAuth.
    m_mirror->watch(QByteArray(kDevicesKey), m_devicesFacade.get());
    // iPhone app Task 71 (sessionHolderVersion 1): who is on the Core. Sent
    // only to a view at minor 11 that declared sessionHolder with
    // deviceAuth.
    m_mirror->watch(QByteArray(kConnectedDevicesKey), m_connectedDevices.get());
    // iPhone app Task 19 (stationCatalogVersion 1): the Core's catalogue,
    // read again now so the snapshot carries the radio as it is. Sent only
    // to a peer at minor 11 (sendToPeer).
    m_catalog->refresh();
    m_mirror->watch(QByteArray(kCatalogKey), m_catalog.get());
    const QList<PanadapterModel*> pans = m_radioModel->panadapters();
    for (int i = 0; i < pans.size(); ++i) {
        m_mirror->watch(panKey(i), pans.at(i));
    }

    // The third step of ObjectRegistry's three-step, and the one that is
    // easy to forget: the constructor only wires sliceAdded/sliceRemoved,
    // so every slice DaemonApp::start() already created is invisible until
    // this runs. StateMirror::snapshotAll() is not a substitute -- it walks
    // its own watch list and cannot discover an object nobody watched.
    // Called after the objectCreated/objectDestroyed wiring in the
    // constructor, which is the ordering its own doc comment requires.
    m_registry->backfillExistingSlices();
    // iPhone app Task 73: and a marker for each.
    m_markers->backfill();
}

bool StationServer::sendCapabilitiesAndSettingsSnapshot(SessionTransport* transport)
{
    // Do not let a queued radio callback target a disconnected or merely
    // handshaking peer, or one turned away. promoteToSession()
    // intentionally calls this while snapshotComplete is still false, so
    // admission is the boundary here rather than readiness of the full
    // mirror snapshot.
    const auto stillAdmitted = [this, transport]() {
        if (transport == nullptr) {
            return false;
        }
        const auto peer = m_peers.constFind(transport);
        return peer != m_peers.cend() && peer->authenticated && !peer->sessionDeviceId.isEmpty();
    };
    if (!stillAdmitted()) {
        return false;
    }

    // Capability exchange (section 7.0 step 4).  On the initial path this
    // remains before every model message; on the late-radio path it updates
    // only identity, board, effective limits, and connection state.
    // iPhone app Task 76: what these capabilities say of the budget, so a
    // later change is published once.
    m_peers[transport].publishedBudget = budgetEntriesFor(transport);
    send(transport, SessionMessages::capabilities(buildCapabilitiesFor(transport).toUpdates()));
    if (!stillAdmitted()) {
        return false;
    }

    // Station settings are scoped to the radio that is live *now*.  A late
    // radio therefore needs a fresh snapshot even though the mirror objects
    // already exist on the GUI; SettingsProxy merges this authoritative scope
    // into its in-memory cache without touching local GUI storage.
    const QMap<QString, QString> snapshot = m_settingsServer->buildSnapshot(
        m_radioModel.isNull() ? QString() : m_radioModel->currentRadioMac());
    QList<MirrorUpdate> entries;
    entries.reserve(snapshot.size());
    for (auto it = snapshot.cbegin(); it != snapshot.cend(); ++it) {
        entries.append(
            MirrorUpdate{0, it.key().toUtf8(), MirrorWireKind::Utf8, QVariant(it.value())});
    }
    send(transport, SessionMessages::settingsSnapshot(entries));
    return stillAdmitted();
}

// ── Inbound state and settings ───────────────────────────────────────────

void StationServer::handlePropertyWrite(SessionTransport* transport,
                                        const SessionMessage& message)
{
    // iPhone app Task 73 (ruling 5.9): a device writes only its own slices,
    // and nobody writes a marker. Refused before anything is read or
    // applied, with no value of the other device's slice in the answer.
    {
        QString refusal;
        const QByteArray requester = m_peers.value(transport).sessionDeviceId;
        if (message.objectKey.startsWith("slice:")) {
            bool ok = false;
            const int sliceId = message.objectKey.mid(6).toInt(&ok);
            if (ok) {
                refusal = sliceRefusal(requester, sliceId);
            }
        } else if (message.objectKey.startsWith("marker:")) {
            const int sliceId = SliceMarkerSet::sliceIdOf(message.objectKey);
            if (m_radioModel && sliceId >= 0 && m_radioModel->sliceOwnership()->isLive(sliceId)) {
                refusal = ownedElsewhereReason(sliceId);
            }
        }
        if (!refusal.isEmpty()) {
            QList<SessionPropertyResult> results;
            QSet<QByteArray> reported;
            for (const MirrorUpdate& update : message.updates) {
                if (reported.contains(update.name)) {
                    continue;
                }
                reported.insert(update.name);
                SessionPropertyResult result;
                result.property = update.name;
                result.accepted = false;
                result.reason = refusal;
                results.append(result);
            }
            if (m_peers.value(transport).agreedMinor >= kDspControlSessionProtocolMinor
                && message.writeId != 0) {
                send(transport,
                     SessionMessages::propertyResult(message.objectKey, message.writeId, results));
            }
            return;
        }
    }

    // iPhone app Task 75 (the several-devices design, 7.1; ruling 6.1): a
    // change that reaches another device's slices is asked first.
    if (handleSharedSetting(transport, message)) {
        return;
    }
    // iPhone app Task 74 (rulings 6.5 and 6.5a): a retune of a slice that
    // leaves its shared receiver's window may be a pan move, asked first,
    // or a take once refused.
    if (handleSliceRetune(transport, message)) {
        return;
    }
    applyPropertyWrite(transport, message, true, {});
}

QList<SessionPropertyResult> StationServer::applyPropertyWrite(
    SessionTransport* transport, const SessionMessage& message, bool answer,
    const std::function<void(QList<SessionPropertyResult>&)>& adjust)
{
    // Persist accepted PS preferences without replaying transmit operations.
    // This session advertises txPermitted=false until the R4 transmit path.
    const QPointer<PureSignal> hydrating = message.objectKey == "pureSignalSettings"
        && m_radioModel ? m_radioModel->pureSignal() : nullptr;
    if (hydrating) {
        hydrating->beginSettingsHydration();
    }
    const auto hydrationGuard = qScopeGuard([hydrating]() {
        if (hydrating) {
            hydrating->endSettingsHydration();
        }
    });
    const QList<MirrorUpdate> before = m_mirror->snapshot(message.objectKey);
    QHash<QByteArray, MirrorUpdate> previous;
    for (const auto& value : before) {
        previous.insert(value.name, value);
    }
    QHash<QByteArray, QString> refusals;
    const bool negotiated = m_peers.value(transport).agreedMinor >= kDspControlSessionProtocolMinor;
    // R-R3-46: only a peer that was offered the object may change it, and
    // only while the Core's controller is behind it.
    const bool stepAttWrite = message.objectKey == kStepAttKey;
    const bool alexWrite = message.objectKey == kAlexAntennasKey;
    QString stepAttRefusal;
    if (stepAttWrite
        && m_peers.value(transport).agreedMinor < kRadioIdentitySessionProtocolMinor) {
        stepAttRefusal =
            QStringLiteral("Update this app to change the radio's attenuator on this Core.");
    } else if (stepAttWrite
               && (m_radioModel.isNull() || !m_radioModel->stepAttFacade()->isBound())) {
        stepAttRefusal = QStringLiteral("The Core has no attenuator ready.");
    } else if (alexWrite
               && m_peers.value(transport).agreedMinor < kRadioIdentitySessionProtocolMinor) {
        stepAttRefusal =
            QStringLiteral("Update this app to change the radio's antennas on this Core.");
    } else if (alexWrite && radioHardwareVersion() < 2) {
        stepAttRefusal = QStringLiteral("The Core has no antenna settings ready.");
    } else if (message.objectKey == kIoBoardKey) {
        // R-R3-46: the I/O board's readings are the Core's to report.
        stepAttRefusal = IoBoardHl2Facade::readOnlyReason();
    } else if (message.objectKey == kAmplifierKey) {
        // R-R3-47: the amp's readings are the Core's to report.
        stepAttRefusal = AmplifierModel::readOnlyReason();
    } else if (message.objectKey == kRfKitKey) {
        stepAttRefusal = RfKitModel::readOnlyReason();
    } else if (message.objectKey == kStationTciKey) {
        // R-R3-48: the switch changes only through setStationTci.
        stepAttRefusal = StationTciModel::readOnlyReason();
    } else if (message.objectKey == kAccessoryDataKey) {
        // R-R3-47: changed only through its commands.
        stepAttRefusal = AccessoryDataModel::readOnlyReason();
    } else if (message.objectKey == kAccessorySettingsKey) {
        // R-R3-47 / R-R3-22: the devices' own settings change only through
        // their commands, which the Core sends to the device.
        stepAttRefusal = AccessorySettingsModel::readOnlyReason();
    }
    // R-R3-47: the RF-Kit switch is the Core's, changed by setRfKitEnabled.
    const bool radioWrite = message.objectKey == QByteArray(kRadioKey);
    const bool receiveOnlyTransmitWrite = message.objectKey == QByteArray(kTransmitKey)
        && !m_radioModel.isNull() && m_radioModel->receiveOnlyStationPolicy();
    // R-R3-49 (parity Task 1): a peer offered transmitSettingsVersion 1 may
    // change a transmit setting on a receive-only Core; it waits while the
    // radio is on the air (the Core's one on-air refusal), read once for the
    // batch before anything in it is applied. Any other peer is refused
    // every `transmit` write, as before.
    const bool transmitSettingsWrite = receiveOnlyTransmitWrite
        && transmitSettingsOffered(transport);
    QString onAirRefusal;
    if (transmitSettingsWrite) {
        m_radioModel->stationOnAirRefusal(&onAirRefusal);
    }
    // R-R3-25: the tuner's operate, bypass and antenna, and the amplifier's
    // operate, on a receive-only Core.
    const bool receiveOnlyStation = !m_radioModel.isNull()
        && m_radioModel->receiveOnlyStationPolicy();
    const bool tunerWrite = message.objectKey == QByteArray(kTunerKey);
    const bool amplifierWrite = message.objectKey == QByteArray(kAmplifierKey);
    // R-R3-49 (parity Task 5): `stepAtt`'s ATT on TX, its value and Force
    // ATT are transmit settings (transmitSettingsVersion 5). A receive-only
    // Core takes them from a peer offered the transmit settings, off the
    // air only; the on-air check is read once for the batch.
    QString stepAttOnAir;
    if (stepAttWrite && receiveOnlyStation) {
        m_radioModel->stationOnAirRefusal(&stepAttOnAir);
    }
    // R-IOS-01: the class MirrorPolicy's direction table is keyed by.
    QByteArray outboundClass;
    if (const QObject* target = m_mirror->watchedObject(message.objectKey)) {
        outboundClass = MirrorSchema::shortClassName(target->metaObject()->className());
    }
    QSet<QByteArray> requested;
    for (const MirrorUpdate& update : message.updates) {
        if (requested.contains(update.name)) {
            refusals.insert(update.name, QStringLiteral("This change named the same setting twice."));
            continue;
        }
        requested.insert(update.name);
        // R-R3-49 (parity Task 1): the keying set is refused first, whether
        // or not this Core mirrors the property, on and off the air.
        if (receiveOnlyTransmitWrite
            && (!transmitSettingsWrite || isTransmitKeyingProperty(update.name))) {
            refusals.insert(update.name, QString::fromLatin1(kReceiveOnlyTransmitReason));
            continue;
        }
        const auto known = previous.constFind(update.name);
        if (known == previous.cend() || known->kind != update.kind) {
            refusals.insert(update.name, QStringLiteral("The Core does not have this setting, or not in this form."));
            continue;
        }
        if (transmitSettingsWrite && !onAirRefusal.isEmpty()) {
            refusals.insert(update.name, onAirRefusal);
            continue;
        }
        if (receiveOnlyStation
            && ((tunerWrite && isTunerTransmitPathProperty(update.name))
                || (amplifierWrite && update.name == "operate"))) {
            // R-R3-25 / R-R3-47: these wait for remote transmit.
            refusals.insert(update.name, AmplifierModel::receiveOnlyOperateReason());
            continue;
        }
        if (!stepAttRefusal.isEmpty()) {
            refusals.insert(update.name, stepAttRefusal);
            continue;
        }
        if (stepAttWrite && StepAttenuatorFacade::isTransmitSetting(update.name)) {
            if (receiveOnlyStation && !transmitSettingsOffered(transport)) {
                refusals.insert(update.name, QString::fromLatin1(kReceiveOnlyTransmitReason));
                continue;
            }
            if (!stepAttOnAir.isEmpty()) {
                refusals.insert(update.name, stepAttOnAir);
                continue;
            }
            const QString range = m_radioModel->stepAttFacade()
                ->transmitSettingRefusal(update.name, update.value);
            if (!range.isEmpty()) {
                refusals.insert(update.name, range);
                continue;
            }
        }
        if (radioWrite && update.name == "rfKitEnabled") {
            refusals.insert(update.name, QString::fromLatin1(kRfKitSwitchWriteReason));
            continue;
        }
        if (!negotiated && (message.objectKey == "pureSignalSettings"
            || update.name.startsWith("nnr")
            || (update.name == "activeNr" && update.value.toInt() == static_cast<int>(NrSlot::NNR)))) {
            refusals.insert(update.name, QStringLiteral("Update this app to change these settings on this Core."));
            continue;
        }
        if (!outboundClass.isEmpty()
            && !MirrorPolicy::inboundAllowed(outboundClass, update.name)) {
            // `active` has a way in of its own: selecting the slice.
            refusals.insert(update.name,
                            outboundClass == "SliceModel" && update.name == "active"
                                ? SliceModel::activeWriteReason()
                                : QString::fromLatin1(kOutboundWriteReason));
            continue;
        }
        // R-R3-49 (parity Task 2): a transmit setting outside its setter's
        // range is refused with the range, rather than clamped silently.
        if (message.objectKey == QByteArray(kTransmitKey) && !m_radioModel.isNull()) {
            const QString range = m_radioModel->transmitModel()
                .settingRangeRefusal(update.name, update.value);
            if (!range.isEmpty()) {
                refusals.insert(update.name, range);
                continue;
            }
        }
        // Ruling 5.7: this session's write. Its changes, here and on other
        // objects, are withheld from its own view and reach every other.
        const MirrorApplyResult result = m_mirror->applyInbound(
            message.objectKey, update.name, update.value, m_peers.value(transport).view.data());
        if (!result.accepted) {
            refusals.insert(update.name, result.reason);
        }
    }

    // Read once after the WHOLE batch: a later setter may change an earlier
    // property's final value. Qt's successful WRITE invocation alone cannot
    // prove that a validating model accepted the requested configuration.
    const QList<MirrorUpdate> settled = m_mirror->snapshot(message.objectKey);
    QHash<QByteArray, MirrorUpdate> actual;
    for (const auto& value : settled) {
        actual.insert(value.name, value);
    }
    QList<SessionPropertyResult> results;
    QSet<QByteArray> reported;
    for (const auto& update : message.updates) {
        if (reported.contains(update.name)) {
            continue;
        }
        reported.insert(update.name);
        SessionPropertyResult result;
        result.property = update.name;
        result.hasValue = actual.contains(update.name);
        if (result.hasValue) {
            result.value = actual.value(update.name);
        }
        result.reason = refusals.value(update.name);
        const bool sameMap = message.objectKey == QByteArray(kTransmitKey) && result.hasValue
            && sameJsonObject(result.value.value, update.value);
        if (result.reason.isEmpty()
            && (!result.hasValue || (result.value.value != update.value && !sameMap))) {
            // R-R3-46: the attenuator says in plain words why it kept
            // another value (its range, what this radio offers).
            if (stepAttWrite && !m_radioModel.isNull()) {
                result.reason = m_radioModel->stepAttFacade()->settleReason(update.name);
            } else if (alexWrite && !m_radioModel.isNull()) {
                result.reason = m_radioModel->alexAntennaFacade()->settleReason(update.name);
            }
            if (result.reason.isEmpty()) {
                result.reason = QStringLiteral("The Core checked this change and kept the value shown.");
            }
        }
        result.accepted = result.reason.isEmpty();
        results.append(result);
    }
    if (adjust) {
        adjust(results);
    }
    if (answer && negotiated && message.writeId != 0) {
        send(transport, SessionMessages::propertyResult(message.objectKey, message.writeId, results));
    }

    // Inbound setters suppress their notifications to avoid echo loops.
    // Publish their settled side effects, and give legacy peers accepted or
    // clamped readback too. New peers get requested fields in the sequenced
    // result above so an older answer cannot overwrite a newer local edit.
    QList<MirrorUpdate> corrections;
    for (const auto& value : settled) {
        const bool changed = !previous.contains(value.name)
            || previous.value(value.name).value != value.value;
        if ((requested.contains(value.name) && (!negotiated || message.writeId == 0))
            || (changed && !requested.contains(value.name))) {
            corrections.append(value);
        }
    }
    // An older peer was never offered `stepAtt`; it gets nothing back for it.
    const bool olderStepAttPeer = (stepAttWrite || alexWrite)
        && m_peers.value(transport).agreedMinor < kRadioIdentitySessionProtocolMinor;
    if (!corrections.isEmpty() && !olderStepAttPeer) {
        // A write's side effects can change nnrLimit (turning NNR off or
        // choosing a model clears it), so they are fitted to this peer too.
        SessionMessage delta = SessionMessages::delta(message.objectKey, corrections);
        if (fitNnrLimitToPeer(delta, m_peers.value(transport).agreedMinor)) {
            send(transport, delta);
        }
    }
    return results;
}

void StationServer::handleSettingsWrite(SessionTransport* transport,
                                        const SessionMessage& message)
{
    if (message.updates.isEmpty()) {
        return;
    }
    const QString key = QString::fromUtf8(message.objectKey);
    // A receive-only Core refuses DSP > Options TX writes exactly as it
    // refuses direct TransmitModel writes (handlePropertyWrite above), and
    // hands back its own value so the remote combo settles on it. R-R3-46:
    // so it does for the transmit side of Hardware Config and the PA pages.
    if (receiveOnlyRefusesKey(transport, key)) {
        const QString reason = QString::fromLatin1(kReceiveOnlyTransmitReason);
        const QVariant restored = m_settings.value(key);
        qCWarning(lcStation) << "Refused remote settings write" << key << ":" << reason;
        send(transport, SessionMessages::settingsReject(key, restored.isValid(),
                                                        restored.toString(), reason));
        return;
    }
    // R-R3-49 (parity Task 1): a transmit setting a receive-only Core takes
    // off the air (DSP > Options TX) waits while the radio is on the air,
    // and the Core hands back its own value so the combo settles on it.
    if (const QString onAir = transmitSettingOnAirRefusal(key); !onAir.isEmpty()) {
        const QVariant restored = m_settings.value(key);
        qCWarning(lcStation) << "Refused remote settings write" << key << ":" << onAir;
        send(transport, SessionMessages::settingsReject(key, restored.isValid(),
                                                        restored.toString(), onAir));
        return;
    }
    // R-R3-49 (parity Task 5): a Power page key the page's own control
    // could not have written is refused, and the Core's value handed back.
    if (const QString range = powerPageKeyValueRefusal(key, message.updates.first().value);
        !range.isEmpty()) {
        const QVariant restored = m_settings.value(key);
        qCWarning(lcStation) << "Refused remote settings write" << key << ":" << range;
        send(transport, SessionMessages::settingsReject(key, restored.isValid(),
                                                        restored.toString(), range));
        return;
    }
    // Fix wave I3: a slice's own settings keys are its owner's alone.
    if (const QString refusal = sliceSettingsRefusal(transport, key); !refusal.isEmpty()) {
        const QVariant kept = m_settings.value(key);
        send(transport, SessionMessages::settingsReject(key, kept.isValid(), kept.toString(),
                                                        refusal));
        return;
    }
    // iPhone app Task 75 (link section 8.1): a write that reaches another
    // device is held and asked first.
    if (handleSharedSetting(transport, message)) {
        return;
    }
    applySettingsWrite(transport, message, nullptr);
}

QString StationServer::sliceSettingsRefusal(SessionTransport* transport, const QString& key) const
{
    // Slice<N>/... is SliceModel's per-slice, per-band state (Station
    // scope). A slice's NNR keys, hardware/<mac>/slices/<N>/nnr/..., are
    // model-owned and refused to every writer already.
    static const QRegularExpression kSliceKey(QStringLiteral("^Slice(\\d+)/"));
    const QRegularExpressionMatch match = kSliceKey.match(key);
    if (!match.hasMatch() || m_radioModel.isNull()) {
        return {};
    }
    const QByteArray requester = m_peers.value(transport).sessionDeviceId;
    if (requester.isEmpty()) {
        return {};
    }
    bool ok = false;
    const int sliceId = match.captured(1).toInt(&ok);
    if (!ok) {
        return {};
    }
    // Only the slice's owner; a slice that is not live has none, so its
    // keys (which would seed the next slice under that id) are nobody's.
    const SliceOwnership* ownership = m_radioModel->sliceOwnership();
    if (ownership->isLive(sliceId) && ownership->mark(sliceId).owner == requester) {
        return {};
    }
    return ownedElsewhereReason(sliceId);
}

bool StationServer::applySettingsWrite(SessionTransport* transport, const SessionMessage& message,
                                       QString* refusal)
{
    const QString key = QString::fromUtf8(message.objectKey);
    const SettingsApplyResult result =
        m_settingsServer->applyInboundWrite(key, message.updates.first().value,
                                            message.originTag);
    if (!result.accepted) {
        qCWarning(lcStation) << "Refused remote settings write" << key << ":"
                             << result.reason;
        if (refusal != nullptr) {
            *refusal = result.reason;
        } else {
            send(transport,
                 SessionMessages::settingsReject(key, result.restoredValue.isValid(),
                                                 result.restoredValue.toString(), result.reason));
        }
        return false;
    }
    // R-R3-21: a DSP > Options RX setting takes effect now, not at the next
    // mode change. R-R3-46: a Hardware Config setting reaches the Core's
    // own controllers (the radio, and their later saves) now too. RadioModel
    // ignores every other key.
    if (!m_radioModel.isNull()) {
        m_radioModel->scheduleRemoteDspOptionsApply(key);
        m_radioModel->scheduleRemoteHardwareApply(key);
        // R-R3-47 / R-R3-22: an accessory setting (interlock, output limit,
        // tune memory, antenna names, a fault history) reaches the Core's
        // live objects now, not at the next restart.
        m_radioModel->applyRemoteAccessorySetting(key);
        // R-R3-49 (parity Task 5): so does an SWR protection setting, to the
        // Core's SwrProtectionController, as the local page's change does.
        m_radioModel->applySwrProtectionSetting(key, m_settings.value(key));
    }
    return true;
}

void StationServer::handleSettingsRemove(SessionTransport* transport, const SessionMessage& message)
{
    const QString key = QString::fromUtf8(message.objectKey);
    if (isModelOwnedDspSettingsKey(key)) {
        const QVariant value = m_settings.value(key);
        const bool nr3Path =
            key.compare(QLatin1String("Nr3ModelPath"), Qt::CaseInsensitive) == 0;
        // R-R3-21: the notch keys, like Nr3ModelPath, carry their own plain
        // reason; every other model-owned key keeps its existing wire string.
        // R-R3-46: so do the step attenuator and preamp keys.
        const bool plainReason = nr3Path || isModelOwnedNotchSettingsKey(key)
            || isModelOwnedStepAttenuatorSettingsKey(key)
            || isModelOwnedAlexAntennaSettingsKey(key)
            || isCoreOwnedIdentitySettingsKey(key);
        // iPhone app Task 71: to the session that asked (the only one
        // before).
        send(transport, SessionMessages::settingsReject(key, value.isValid(), value.toString(),
            plainReason ? modelOwnedSettingsRefusal(key)
                    : QStringLiteral("Change these settings with their own controls on this Core.")));
        return;
    }
    // A remove would reset a DSP > Options TX setting to its default, so a
    // receive-only Core refuses it exactly as it refuses a write to the same
    // key (handleSettingsWrite above) and hands back its own value (R-R3-21).
    // R-R3-46: the same for a transmit-side hardware key.
    if (receiveOnlyRefusesKey(transport, key)) {
        const QString reason = QString::fromLatin1(kReceiveOnlyTransmitReason);
        const QVariant restored = m_settings.value(key);
        qCWarning(lcStation) << "Refused remote settings remove" << key << ":" << reason;
        send(transport, SessionMessages::settingsReject(key, restored.isValid(),
                                                      restored.toString(), reason));
        return;
    }
    // R-R3-49 (parity Task 1): the same wait for a remove while the radio
    // is on the air.
    if (const QString onAir = transmitSettingOnAirRefusal(key); !onAir.isEmpty()) {
        const QVariant restored = m_settings.value(key);
        qCWarning(lcStation) << "Refused remote settings remove" << key << ":" << onAir;
        send(transport, SessionMessages::settingsReject(key, restored.isValid(),
                                                      restored.toString(), onAir));
        return;
    }
    // SettingsProxyServer has no remove path of its own: AppSettings::
    // remove() fires the same Task 13 change hook a setValue() does, so
    // the broadcast that reaches every client is produced by the same
    // generic path, with an empty origin tag. Routed through the daemon's
    // own store directly, and gated on the same Station classification
    // applyInboundWrite() enforces so a client cannot reach an
    // OperatorLocal key by removing it instead of writing it.
    if (classifySettingsKey(key) != SettingsScope::Station) {
        qCWarning(lcStation) << "Refused remote settings remove of non-station key" << key;
        return;
    }
    // R-R3-46: as for a write, only the connected radio's hardware keys.
    if (const QString refusal = m_settingsServer->otherRadioRefusal(key); !refusal.isEmpty()) {
        const QVariant value = m_settings.value(key);
        qCWarning(lcStation) << "Refused remote settings remove" << key << ":" << refusal;
        send(transport, SessionMessages::settingsReject(key, value.isValid(), value.toString(),
                                                      refusal));
        return;
    }
    // Fix wave I3: a slice's own settings keys are its owner's alone.
    if (const QString refusal = sliceSettingsRefusal(transport, key); !refusal.isEmpty()) {
        const QVariant kept = m_settings.value(key);
        send(transport, SessionMessages::settingsReject(key, kept.isValid(), kept.toString(),
                                                        refusal));
        return;
    }
    // Fix wave I3: a removal is a write of the default, so it goes through
    // the shared-setting check exactly as a write does.
    if (handleSharedSetting(transport, message)) {
        return;
    }
    applySettingsRemove(message);
}

void StationServer::applySettingsRemove(const SessionMessage& message)
{
    const QString key = QString::fromUtf8(message.objectKey);
    m_settings.remove(key);
    // R-R3-21: removing a DSP > Options RX setting returns it to its
    // default, which takes effect now as a write does. R-R3-46: so does a
    // Hardware Config setting. R-R3-47: and an accessory setting.
    if (!m_radioModel.isNull()) {
        m_radioModel->scheduleRemoteDspOptionsApply(key);
        m_radioModel->scheduleRemoteHardwareApply(key);
        m_radioModel->applyRemoteAccessorySetting(key);
        // R-R3-49 (parity Task 5): the SWR protection default, at once.
        m_radioModel->applySwrProtectionSetting(key, QVariant());
    }
}

// ── Send helpers ─────────────────────────────────────────────────────────

void StationServer::send(SessionTransport* transport, const SessionMessage& message)
{
    if (transport == nullptr) {
        return;
    }
    transport->sendText(SessionMessages::encode(message));
}

bool StationServer::peerSeesPairingCode(SessionTransport* transport) const
{
    const auto peer = m_peers.constFind(transport);
    return peer != m_peers.cend() && peer->authenticated && !peer->signedInWithToken
        && !peer->deviceId.isEmpty();
}

SessionMessage StationServer::withPairingCodeFor(SessionTransport* transport,
                                                 const SessionMessage& message) const
{
    const bool devicesProperties =
        (message.kind == SessionMessageKind::ObjectCreate
         || message.kind == SessionMessageKind::Delta)
        && isDevicesMessage(message);
    const bool pairingOpenResult = message.kind == SessionMessageKind::CommandResult
        && message.commandVerb == "pairing.open";
    if ((!devicesProperties && !pairingOpenResult) || peerSeesPairingCode(transport)) {
        return message;
    }
    // Any other connection (a window signed in with the old pairing token,
    // whatever its hello declares) gets the code blanked.
    SessionMessage blanked = message;
    const QByteArray name = pairingOpenResult ? QByteArrayLiteral("code")
                                              : QByteArray(kPairingCodeProperty);
    for (MirrorUpdate& update : blanked.updates) {
        if (update.name == name) {
            update.value = QVariant(QString());
        }
    }
    return blanked;
}

void StationServer::sendToEveryView(const SessionMessage& message)
{
    // iPhone app Task 72 (ruling 5.8): an object.create, object.destroy or
    // settings.value goes to every view that holds the object or key: each
    // admitted session whose view has attached, fitted to what it
    // negotiated (sendToPeer). A view still in its burst is attached and so
    // receives it too; a connection with no view receives nothing.
    for (const QPointer<MirrorView>& view : attachedViews()) {
        if (!view.isNull()) {
            view->deliver(message);
        }
    }
}

QList<QPointer<MirrorView>> StationServer::attachedViews() const
{
    // Copied: a send can end a session, which erases its peer.
    QList<QPointer<MirrorView>> views;
    for (const Peer& peer : std::as_const(m_peers)) {
        if (!peer.sessionDeviceId.isEmpty() && !peer.view.isNull() && peer.view->isAttached()) {
            views.append(peer.view);
        }
    }
    return views;
}

QString StationServer::sessionOwner(quint64 sessionId)
{
    return sessionId == 0 ? QString() : QStringLiteral("station:%1").arg(sessionId);
}

quint64 StationServer::sessionIdOfOwner(const QString& owner)
{
    static const QString kPrefix = QStringLiteral("station:");
    if (!owner.startsWith(kPrefix)) {
        return 0;
    }
    bool ok = false;
    const quint64 id = QStringView(owner).mid(kPrefix.size()).toULongLong(&ok);
    return ok ? id : 0;
}

StationServer::ResultKey StationServer::resultKeyOf(const SessionMessage& result) const
{
    return ResultKey{sessionIdOfOwner(m_dispatcher->resultOwner()), result.commandVerb,
                     result.commandId};
}

bool StationServer::isLastResult(const SessionMessage& result)
{
    // A PureSignal action answers in phases; its route stays until the
    // completed or failed one. Any other command answers once.
    if (!result.commandVerb.startsWith("ps3.") || !result.accepted) {
        return true;
    }
    for (const MirrorUpdate& update : result.updates) {
        if (update.name == "phase") {
            const QString phase = update.value.toString();
            return phase == QLatin1String("completed") || phase == QLatin1String("failed");
        }
    }
    return true;
}

void StationServer::sendToPeer(SessionTransport* transport, const SessionMessage& original)
{
    if (transport == nullptr || !m_peers.contains(transport)) {
        return;
    }
    // iPhone app Task 14: the pairing code only to a connection signed in
    // with a paired device's key.
    const SessionMessage message = withPairingCodeFor(transport, original);
    // iPhone app Task 73 (ruling 5.6): a device's own slices, and markers
    // for the others to a view with the feature.
    if (!ownershipAllows(transport, message)) {
        return;
    }
    switch (message.kind) {
    case SessionMessageKind::Schema:
    case SessionMessageKind::ObjectCreate:
    case SessionMessageKind::Delta: {
        const auto peer = m_peers.constFind(transport);
        const quint16 minor = peer != m_peers.cend() ? peer->agreedMinor
                                                     : kSessionProtocolMinor;
        // A Core without its controller behind the object does not offer
        // it (radioHardwareVersion 0), so it does not send it either.
        if (isStepAttMessage(message)
            && (minor < kRadioIdentitySessionProtocolMinor || radioHardwareVersion() < 1)) {
            return;
        }
        if (isAlexAntennasMessage(message)
            && (minor < kRadioIdentitySessionProtocolMinor || radioHardwareVersion() < 2)) {
            return;
        }
        if (isIoBoardMessage(message)
            && (minor < kRadioIdentitySessionProtocolMinor || radioHardwareVersion() < 3)) {
            return;
        }
        // R-R3-47: a Core that does not own its accessories does not offer
        // the amplifier and RF-Kit objects, so it does not send them either.
        if ((isAmplifierMessage(message) || isRfKitMessage(message))
            && (minor < kRadioIdentitySessionProtocolMinor
                || accessoryStatusVersion() < 1)) {
            return;
        }
        // R-R3-48: nor the station TCI object without a station server.
        if (isStationTciMessage(message)
            && (minor < kRadioIdentitySessionProtocolMinor || stationTciVersion() < 1)) {
            return;
        }
        // R-R3-47 / R-R3-22: nor the accessory records to an older app, or
        // from a Core that does not own its accessories.
        if (isAccessoryDataMessage(message)
            && (minor < kRadioIdentitySessionProtocolMinor || accessoryDataVersion() < 1)) {
            return;
        }
        // R-R3-47 / R-R3-22: nor the amp's and tuner's own settings.
        if (isAccessorySettingsMessage(message)
            && (minor < kRadioIdentitySessionProtocolMinor
                || (pgxlControlVersion() < 3 && tgxlControlVersion() < 1))) {
            return;
        }
        // iPhone app Task 13: nor the devices object to anyone but a device
        // that declares deviceAuth (today's desktop declares nothing).
        if (isDevicesMessage(message)
            && (minor < kRadioIdentitySessionProtocolMinor
                || !peerDeclares(transport, QByteArrayLiteral("deviceAuth"), 1)
                || deviceAdminVersion() < 1)) {
            return;
        }
        // iPhone app Task 71: nor who is on the Core to anyone but a view at
        // minor 11 that declared sessionHolder with deviceAuth
        // (sessionHolderVersion 1).
        if (isConnectedDevicesMessage(message) && !peerHasSessionHolderVersion(transport)) {
            return;
        }
        // iPhone app Task 19: nor the catalogue to an older app.
        if (isCatalogMessage(message)
            && (minor < kRadioIdentitySessionProtocolMinor || stationCatalogVersion() < 1)) {
            return;
        }
        if (!needsNnrFit(message, minor)) {
            // Most messages carry no NNR field: send them as they are.
            if (worthSendingAfterNnrFit(message)) {
                transport->sendText(SessionMessages::encode(message));
            }
            return;
        }
        SessionMessage fitted = message;
        if (fitNnrLimitToPeer(fitted, minor)) {
            transport->sendText(SessionMessages::encode(fitted));
        }
        return;
    }
    default:
        transport->sendText(SessionMessages::encode(message));
        return;
    }
}

// ── Slice ownership (iPhone app Task 73) ─────────────────────────────────

bool StationServer::ownershipAllows(SessionTransport* transport,
                                    const SessionMessage& message) const
{
    switch (message.kind) {
    case SessionMessageKind::Schema:
        // A marker's class only to a view with the feature.
        return message.className != "SliceMarker" || peerHasSessionHolderVersion(transport);
    case SessionMessageKind::ObjectCreate:
    case SessionMessageKind::ObjectDestroy:
    case SessionMessageKind::Delta:
        break;
    default:
        return true;
    }
    const bool slice = message.objectKey.startsWith("slice:");
    const bool marker = message.objectKey.startsWith("marker:");
    if ((!slice && !marker) || m_radioModel.isNull()) {
        return true;
    }
    const auto peer = m_peers.constFind(transport);
    if (peer == m_peers.cend()) {
        return false;
    }
    const SliceOwnership* ownership = m_radioModel->sliceOwnership();
    if (slice) {
        bool ok = false;
        const int sliceId = message.objectKey.mid(6).toInt(&ok);
        // Its own: owned by this device (a held slice is the station
        // device's, so never here).
        return ok && !peer->sessionDeviceId.isEmpty()
            && ownership->mark(sliceId).owner == peer->sessionDeviceId;
    }
    // A marker: to a view with the feature, and never to the device whose
    // slice it is (the one it is held for, when held).
    if (!peerHasSessionHolderVersion(transport)) {
        return false;
    }
    const int sliceId = SliceMarkerSet::sliceIdOf(message.objectKey);
    return sliceId >= 0 && ownership->mark(sliceId).subject() != peer->sessionDeviceId;
}

QString StationServer::ownedElsewhereReason(int sliceId) const
{
    QString owner;
    if (m_radioModel) {
        const QByteArray subject = m_radioModel->sliceOwnership()->mark(sliceId).subject();
        if (const auto words = m_connectedDevices->describe(subject)) {
            owner = words->name;
        }
    }
    // The owner's name is the operator's own word (ruling 4.3), never held
    // to the wording rules; the sentence around it is.
    if (owner.isEmpty()) {
        return QStringLiteral("That slice belongs to the Core. It can be changed only there.");
    }
    return QStringLiteral("That slice belongs to %1. It can be changed only there.").arg(owner);
}

QString StationServer::sliceRefusal(const QByteArray& requester, int sliceId) const
{
    if (m_radioModel.isNull() || requester.isEmpty()) {
        return {};
    }
    const SliceOwnership* ownership = m_radioModel->sliceOwnership();
    // A slice that is not there is the verb's own refusal ("no longer on
    // the Core"), not this one.
    if (!ownership->isLive(sliceId) || ownership->mark(sliceId).owner == requester) {
        return {};
    }
    return ownedElsewhereReason(sliceId);
}

QJsonArray StationServer::listeningOn(const QByteArray& deviceId) const
{
    QJsonArray slices;
    if (m_radioModel.isNull() || deviceId.isEmpty()) {
        return slices;
    }
    for (int sliceId : m_radioModel->sliceOwnership()->ownedBy(deviceId)) {
        const SliceModel* slice = m_radioModel->sliceById(sliceId);
        if (slice == nullptr) {
            continue;
        }
        slices.append(QJsonObject{
            {QStringLiteral("sliceId"), sliceId},
            {QStringLiteral("letter"), QString(QChar(QLatin1Char('A').unicode() + sliceId))},
            {QStringLiteral("band"), static_cast<int>(slice->band())},
            {QStringLiteral("mode"), static_cast<int>(slice->dspMode())},
        });
    }
    return slices;
}

int StationServer::deviceLayoutLimit() const
{
    // The board's own cap, not maxSlices(), which reads 1 while the radio
    // is disconnected.
    const int board = m_radioModel ? m_radioModel->boardCapabilities().maxSlices : 0;
    return board > 0 ? std::min(board, WdspEngine::kMaxSliceChannels)
                     : WdspEngine::kMaxSliceChannels;
}

void StationServer::adoptForLoneDevice()
{
    if (m_radioModel.isNull() || m_radioModel->role() != RadioModel::Role::Local) {
        return;
    }
    const QList<DeviceSessionRegistry::Entry> entries = m_deviceSessions->entries();
    if (entries.size() == 1
        && !m_radioModel->sliceOwnership()->adoptUnowned(entries.first().deviceId).isEmpty()) {
        m_connectedDevices->refresh();
    }
}

bool StationServer::anotherDeviceHoldsAPlace(const QByteArray& deviceId) const
{
    const QList<DeviceSessionRegistry::Entry> entries = m_deviceSessions->entries();
    return std::any_of(entries.cbegin(), entries.cend(),
                       [&deviceId](const DeviceSessionRegistry::Entry& entry) {
                           return entry.deviceId != deviceId;
                       });
}

void StationServer::placeSlicesForAdmission(const QByteArray& deviceId)
{
    if (m_radioModel.isNull() || deviceId.isEmpty()
        || m_radioModel->role() != RadioModel::Role::Local) {
        return;
    }
    SliceOwnership* ownership = m_radioModel->sliceOwnership();
    AppSettings& store = AppSettings::instance();

    // 1. Slices the station device held for it are its own again.
    ownership->returnHeld(deviceId);

    // 2. Its saved slices, where they fit: its old letter when free, else
    //    the lowest free, each with its own settings; what does not fit is
    //    kept in its store and recorded for the notice (Task 74). A token
    //    window has none.
    const QString mac = m_radioModel->currentRadioMac();
    if (!deviceId.startsWith("token:") && !mac.isEmpty()) {
        const QList<SavedSlice> saved = DeviceLayoutStore::load(store, mac, deviceId);
        QList<SavedSlice> notRestored;
        for (const SavedSlice& entry : saved) {
            const int sliceId = m_radioModel->sliceById(entry.id) == nullptr
                    && entry.id < WdspEngine::kMaxSliceChannels
                ? entry.id
                : m_radioModel->lowestFreeSliceId();
            if (sliceId < 0) {
                notRestored.append(entry);
                continue;
            }
            DeviceLayoutStore::writeSliceSettings(store, mac, sliceId, entry.settings);
            const ReceiveSliceState state{sliceId, entry.panKey, entry.frequencyHz, entry.dspMode,
                                          {}, {}};
            QString reason;
            if (m_radioModel->restoreSliceFor(deviceId, sliceId, state, &reason) < 0) {
                qCInfo(lcStation) << "A saved slice did not fit:" << reason;
                DeviceLayoutStore::clearSliceSettings(store, mac, sliceId);
                notRestored.append(entry);
            }
        }
        if (!saved.isEmpty()) {
            DeviceLayoutStore::replace(store, mac, deviceId, notRestored, deviceLayoutLimit());
            m_radioModel->requestSettingsSave();
        }
        if (notRestored.isEmpty()) {
            m_slicesNotRestored.remove(deviceId);
        } else {
            m_slicesNotRestored.insert(deviceId, notRestored);
        }
    }

    // 3. The first device admitted while no other is on the Core adopts
    //    every slice nobody owns (never one held for another device).
    if (!anotherDeviceHoldsAPlace(deviceId)) {
        ownership->adoptUnowned(deviceId);
    }

    // 4. A device that still owns no slice gets one on the station-level
    //    active slice's frequency, on its receiver; with the slice cap full
    //    it starts with none.
    if (ownership->ownedBy(deviceId).isEmpty()) {
        const SliceOwnership::CreatorScope creator(ownership, deviceId);
        if (m_radioModel->addSlice(QString()) < 0) {
            qCInfo(lcStation) << "A device was admitted with no slice: the slice cap is full";
        }
    }
    m_connectedDevices->refresh();
}

bool StationServer::closeSliceFor(int sliceId, const QByteArray& saveFor, SavedSlice* closed)
{
    if (m_radioModel.isNull()) {
        return false;
    }
    const SliceModel* slice = m_radioModel->sliceById(sliceId);
    // The Core keeps one slice: its last is never closed for another
    // device's reason.
    if (slice == nullptr || m_radioModel->slices().size() <= 1) {
        return false;
    }
    SavedSlice saved;
    saved.id = sliceId;
    saved.panKey = slice->panKey();
    saved.frequencyHz = slice->frequency();
    saved.dspMode = slice->dspMode();
    const QString mac = m_radioModel->currentRadioMac();
    // Removing saves the slice's own settings to its keys first.
    m_radioModel->removeSlice(sliceId);
    if (m_radioModel->sliceById(sliceId) != nullptr) {
        return false;
    }
    AppSettings& store = AppSettings::instance();
    if ((!saveFor.isEmpty() || closed != nullptr) && !mac.isEmpty()) {
        saved.settings = DeviceLayoutStore::captureSliceSettings(store, mac, sliceId);
    }
    if (!saveFor.isEmpty() && !mac.isEmpty()) {
        DeviceLayoutStore::append(store, mac, saveFor, saved, deviceLayoutLimit());
    }
    // iPhone app Task 74: a slice another device took is kept by its
    // notice for Take it back, not in the device's saved layout.
    if (closed != nullptr) {
        *closed = saved;
    }
    // The letter's keys go with it, so another device's new slice there
    // starts from defaults (ruling 5.3).
    DeviceLayoutStore::clearSliceSettings(store, mac, sliceId);
    m_radioModel->requestSettingsSave();
    return true;
}

QByteArray StationServer::saveForAbsentSubject(int sliceId) const
{
    if (m_radioModel.isNull()) {
        return {};
    }
    const QByteArray subject = m_radioModel->sliceOwnership()->mark(sliceId).subject();
    if (subject.isEmpty() || subject == SliceOwnership::stationDevice()
        || subject.startsWith("token:") || m_deviceSessions->entry(subject)) {
        return {};
    }
    return subject;
}

void StationServer::saveTakenSlicesFor(const QByteArray& deviceId)
{
    if (m_radioModel.isNull() || deviceId.isEmpty()) {
        return;
    }
    if (deviceId.startsWith("token:") || deviceId == SliceOwnership::stationDevice()) {
        // Nothing is saved for these, and no device returns to take back.
        m_confirm->endTakeBacks(deviceId);
        return;
    }
    // Fix wave 3 (the re-review's Minor 2): check it can save before Take
    // it back ends. With no radio connected there is no layout store to
    // save in, so Take it back is kept (it arrives with its notice) and
    // ends, the slice saved, at the end of the device's next away period.
    const QString mac = m_radioModel->currentRadioMac();
    if (mac.isEmpty()) {
        return;
    }
    const QList<ConfirmStep::Notice> taken = m_confirm->endTakeBacks(deviceId);
    AppSettings& store = AppSettings::instance();
    bool saved = false;
    for (const ConfirmStep::Notice& notice : taken) {
        for (const SavedSlice& slice : notice.closed) {
            DeviceLayoutStore::append(store, mac, deviceId, slice, deviceLayoutLimit());
            saved = true;
        }
    }
    if (saved) {
        m_radioModel->requestSettingsSave();
    }
}

void StationServer::releaseDeviceSlices(const QByteArray& deviceId)
{
    if (m_radioModel.isNull() || deviceId.isEmpty()
        || m_radioModel->role() != RadioModel::Role::Local) {
        return;
    }
    // Fix wave: a device gone for good takes its C-Tune pins with it; the
    // receivers it anchored keep their windows, unpinned.
    m_radioModel->clearStreamCtunPinsAnchoredBy(deviceId);
    SliceOwnership* ownership = m_radioModel->sliceOwnership();
    const QList<int> owned = ownership->ownedBy(deviceId);
    const bool token = deviceId.startsWith("token:");
    // Ruling 4.11: with another device on the Core they close (saved for a
    // paired device); with none they keep running, the station device's,
    // held for their owner, as a Core with one client keeps its slices. A
    // token window cannot be recognised again: its slices pass to nobody.
    const bool others = anotherDeviceHoldsAPlace(deviceId);
    for (int sliceId : owned) {
        if (others && closeSliceFor(sliceId, token ? QByteArray() : deviceId)) {
            continue;
        }
        if (token) {
            ownership->setOwner(sliceId, QByteArray());
        } else {
            ownership->hold(sliceId, deviceId);
        }
    }
    m_connectedDevices->refresh();
}

void StationServer::onSliceOwnerChanged(int sliceId, const QByteArray& oldOwner,
                                        const QByteArray& oldHeldFor)
{
    if (m_radioModel.isNull()) {
        return;
    }
    // Fix wave I2: a question naming a slice that changed owner can no
    // longer be proceeded.
    dropQuestionsNaming(sliceId);
    // The markers name the new owner before any view is given one.
    m_markers->refreshOwners();
    m_connectedDevices->refresh();
    if (m_ownerChangesInBurst) {
        // Every view is about to receive its whole burst again.
        return;
    }
    const SliceOwnership::Mark before{oldOwner, oldHeldFor};
    const SliceOwnership::Mark after = m_radioModel->sliceOwnership()->mark(sliceId);
    const QByteArray sliceKey = ObjectRegistry::keyForSlice(sliceId);
    const QByteArray markerKey = SliceMarkerSet::keyFor(sliceId);
    // Ruling 5.8: a change of owner reaches each view as object.destroy of
    // the form it had and object.create of the form it has now.
    const QList<SessionTransport*> transports = m_peers.keys();
    for (SessionTransport* transport : transports) {
        const auto peer = m_peers.constFind(transport);
        if (peer == m_peers.cend() || peer->sessionDeviceId.isEmpty() || peer->view.isNull()
            || !peer->view->isAttached()) {
            continue;
        }
        const QByteArray device = peer->sessionDeviceId;
        // A device that no longer holds a place (revoked a moment ago; its
        // connection ends next) is told nothing more about slices.
        if (!m_deviceSessions->entry(device)) {
            continue;
        }
        const bool markers = peerHasSessionHolderVersion(transport);
        const bool hadSlice = before.owner == device;
        const bool hasSlice = after.owner == device;
        const bool hadMarker = markers && before.subject() != device;
        const bool hasMarker = markers && after.subject() != device;
        const QPointer<MirrorView> view = peer->view;
        if (hadSlice && !hasSlice) {
            send(transport, SessionMessages::objectDestroy(sliceKey, QByteArrayLiteral("SliceModel")));
        }
        if (hadMarker && !hasMarker) {
            send(transport, SessionMessages::objectDestroy(markerKey, QByteArrayLiteral("SliceMarker")));
        }
        if (!hadSlice && hasSlice && !view.isNull()) {
            if (const QObject* object = m_mirror->watchedObject(sliceKey)) {
                view->deliver(SessionMessages::objectCreate(
                    sliceKey, MirrorSchema::shortClassName(object->metaObject()->className()),
                    m_mirror->snapshot(sliceKey)));
            }
        }
        if (!hadMarker && hasMarker && !view.isNull()
            && m_mirror->watchedObject(markerKey) != nullptr) {
            view->deliver(SessionMessages::objectCreate(markerKey, QByteArrayLiteral("SliceMarker"),
                                                        m_mirror->snapshot(markerKey)));
        }
    }
}

void StationServer::setMediaEnabled(bool enabled)
{
    // A live session negotiated its capability already. Do not advertise a
    // different contract midway through it.
    if (!hasAuthenticatedSession()) {
        m_mediaEnabled = enabled;
    }
}

void StationServer::setTelemetryEnabled(bool enabled)
{
    if (!hasAuthenticatedSession()) { m_telemetryEnabled = enabled; }
}

bool StationServer::setDisplayBudgetLimits(const DisplayBudgetLimits& limits,
                                           DisplayBudgetReason reason)
{
    if (!limits.isValid()) { return false; }
    if (m_displayBudget) {
        if (*m_displayBudget == limits && reason == m_displayBudgetReason) { return true; }
        const quint32 delta = limits.generation - m_displayBudget->generation;
        if (delta == 0 || delta >= 0x80000000u) { return false; }
    }
    m_displayBudget = limits;
    m_displayBudgetReason = reason;
    // iPhone app Task 76: the new total is split among the media sessions.
    recomputeDisplayBudgetShares();
    const QPointer<StationServer> self(this);
    emit displayBudgetChanged(); // The sender sees new limits before publication.
    // A peer the computed budget does not reach heard of no budget, so a
    // change to it is nothing to that peer (legacy mode exactly).
    if (self) {
        self->publishBudgetToChangedSessions();
    }
    return true;
}

void StationServer::setDisplayBudgetEnforcementEnabled(bool enabled)
{
    m_displayBudgetEnforcementEnabled = enabled;
    publishDisplayBudgetCapabilities();
}

void StationServer::setPs3DisplayAdmissionHandler(Ps3DisplayAdmissionHandler handler)
{
    if (!handler) {
        m_ps3DisplayAdmission = {};
        return;
    }
    m_ps3DisplayAdmission = [handler = std::move(handler)](quint64, bool enabled,
                                                           QString* refusal) {
        return handler(enabled, refusal);
    };
}

void StationServer::setSessionPs3DisplayAdmissionHandler(SessionPs3DisplayAdmissionHandler handler)
{
    m_ps3DisplayAdmission = std::move(handler);
}

void StationServer::publishDisplayBudgetCapabilities()
{
    // iPhone app Task 76: split again (the PureSignal display's charge may
    // have moved) and tell each media session whose budget entries changed.
    if (recomputeDisplayBudgetShares()) {
        emit displayBudgetChanged();
    }
    publishBudgetToChangedSessions();
}

void StationServer::publishBudgetToChangedSessions()
{
    const QList<quint64> epochs = mediaSessionEpochs();
    for (quint64 epoch : epochs) {
        SessionTransport* transport = mediaSessionFor(epoch);
        if (transport == nullptr || !mediaAvailable(epoch)) {
            continue;
        }
        const QByteArray entries = budgetEntriesFor(transport);
        if (entries == m_peers.value(transport).publishedBudget) {
            continue;
        }
        m_peers[transport].publishedBudget = entries;
        send(transport,
             SessionMessages::capabilities(buildCapabilitiesFor(transport).toUpdates()));
    }
}

QByteArray StationServer::budgetEntriesFor(SessionTransport* transport) const
{
    // Every budget entry the capabilities carry, as one comparable string.
    const StationCapabilities caps = buildCapabilitiesFor(transport);
    QByteArray key;
    key += QByteArray::number(caps.remoteDisplayBudgetVersion);
    if (caps.displayBudget) {
        key += ':' + QByteArray::number(caps.displayBudget->applicationBytesPerSecond);
        key += ':' + QByteArray::number(caps.displayBudget->spectrumSampleUnitsPerSecond);
        key += ':' + QByteArray::number(caps.displayBudget->generation);
    }
    key += caps.remotePs3DisplaySubscribed ? ":ps" : ":-";
    if (caps.displayBudgetReason) {
        key += ':' + QByteArray::number(static_cast<int>(*caps.displayBudgetReason));
    }
    return key;
}

std::optional<DisplayBudgetLimits> StationServer::displayBudgetTotalFor(
    SessionTransport* transport) const
{
    if (m_displayBudget && m_displayBudgetForReasonPeersOnly) {
        // R-R3-08/37: a computed ceiling is only for an app that can be told
        // why it is lowered. An older app keeps legacy mode exactly: no
        // budget, no pacing, no allocation results.
        const auto peer = m_peers.constFind(transport);
        if (peer == m_peers.cend()
            || peer->agreedMinor < kDisplayBudgetReasonSessionProtocolMinor) {
            return std::nullopt;
        }
    }
    return m_displayBudget;
}

void StationServer::setDisplayBudgetHolderForTest(DisplayBudgetHolderKind kind,
                                                  quint64 holderEpoch, bool away)
{
    m_testBudgetHolderKind = kind;
    m_testBudgetHolderEpoch = holderEpoch;
    m_testBudgetHolderAway = away;
    publishDisplayBudgetCapabilities();
}

int StationServer::displayBudgetSharingCount() const
{
    if (!m_displayBudget || !m_mediaEnabled) {
        return 0;
    }
    int count = 0;
    for (quint64 epoch : mediaSessionEpochs()) {
        SessionTransport* transport = mediaSessionFor(epoch);
        const auto peer = m_peers.constFind(transport);
        if (peer != m_peers.cend() && peer->agreedMinor >= kMediaSessionProtocolMinor
            && displayBudgetTotalFor(transport)) {
            ++count;
        }
    }
    return count;
}

QList<QPair<SessionTransport*, DisplayBudgetShare>> StationServer::splitDisplayBudget(
    std::optional<quint64> ps3Subscriber) const
{
    // iPhone app Task 76 (ruling 9.3): every media session the total
    // reaches shares it, in admission order.
    QList<QPair<SessionTransport*, DisplayBudgetShare>> result;
    if (!m_displayBudget) {
        return result;
    }
    DisplayBudgetSplitInput input;
    input.total = *m_displayBudget;
    input.governorCut = m_displayBudgetReason == DisplayBudgetReason::CoreBusy;
    // Fix wave 2 (Critical 1): every admitted network device counts as
    // asking for at least one useful pan, the governor's own floor pan, so
    // a device that has not subscribed yet (or an older client planning
    // inside its share) keeps at least the smaller of one useful pan and
    // an equal part beside a device asking for the whole total, unless
    // that device is a present holder (rule 1; fix wave 3, design 9.3
    // "What the floor guarantees"). The link carries nothing that says a
    // device is sound only, so every device is floored.
    input.minimumRequest = DisplayLoadGovernor::floorPanCharge();
    QList<SessionTransport*> sharing;
    for (quint64 epoch : mediaSessionEpochs()) {
        SessionTransport* transport = mediaSessionFor(epoch);
        const Peer& peer = m_peers.find(transport).value();
        if (!m_mediaEnabled || peer.agreedMinor < kMediaSessionProtocolMinor
            || !displayBudgetTotalFor(transport)) {
            continue;
        }
        DisplayBudgetSplitDevice device;
        device.id = QByteArray::number(epoch);
        // Fix wave I5 (ruling 9.3): a device's request is its demand, what
        // its displays ask for as subscribed, not its grant, so max-min
        // fair shares (rules 2 and 3) and the holder's whole request (rule
        // 1, with Task 34) mean what they say. Without the media hub's
        // provider every device asks for the whole total, as before.
        if (m_displayDemand) {
            const DisplayBudgetCharge demand =
                m_displayDemand(epoch).value_or(DisplayBudgetCharge{});
            device.request = {demand.applicationBytesPerSecond,
                              demand.spectrumSampleUnitsPerSecond, 0};
        } else {
            device.request = {m_displayBudget->applicationBytesPerSecond,
                              m_displayBudget->spectrumSampleUnitsPerSecond, 0};
        }
        device.previous = peer.budgetShare;
        device.previousReason = peer.budgetShareReason;
        input.devices.append(device);
        sharing.append(transport);
    }
    // Transmit joins here (Task 34): TransmitHolder's holder, as
    // DisplayBudgetHolderKind::Station for the station device, or ::Device
    // with the holder's media epoch as holderId and holderAway while it is
    // away. Until then transmit is unheld (rule 3), except for a holder a
    // test sets (setDisplayBudgetHolderForTest, fix wave 3), which the
    // merge with Task 34 replaces with TransmitHolder's.
    input.holderKind = m_testBudgetHolderKind;
    if (m_testBudgetHolderKind == DisplayBudgetHolderKind::Device) {
        input.holderId = QByteArray::number(m_testBudgetHolderEpoch);
        input.holderAway = m_testBudgetHolderAway;
    }
    if (ps3Subscriber) {
        input.ps3Subscriber = QByteArray::number(*ps3Subscriber);
    }
    const QList<DisplayBudgetShare> shares = DisplayBudgetSplit::split(input);
    for (qsizetype i = 0; i < sharing.size() && i < shares.size(); ++i) {
        result.append(qMakePair(sharing.at(i), shares.at(i)));
    }
    return result;
}

bool StationServer::recomputeDisplayBudgetShares()
{
    std::optional<quint64> subscriber;
    if (m_radioModel && m_radioModel->pureSignalFacade()->remoteAmpViewSubscribed()) {
        subscriber = m_ps3SubscriberEpoch != 0 ? m_ps3SubscriberEpoch : mediaSessionEpoch();
    }
    const QList<QPair<SessionTransport*, DisplayBudgetShare>> shares =
        splitDisplayBudget(subscriber);
    bool changed = false;
    QSet<SessionTransport*> sharing;
    for (const auto& [transport, share] : shares) {
        Peer& peer = m_peers[transport];
        if (peer.budgetShare != share.limits || peer.budgetShareReason != share.reason) {
            changed = true;
        }
        peer.budgetShare = share.limits;
        peer.budgetShareReason = share.reason;
        sharing.insert(transport);
    }
    // A session the total does not reach has no share.
    for (quint64 epoch : mediaSessionEpochs()) {
        SessionTransport* transport = mediaSessionFor(epoch);
        if (sharing.contains(transport)) {
            continue;
        }
        Peer& peer = m_peers[transport];
        if (peer.budgetShare) {
            changed = true;
        }
        peer.budgetShare.reset();
        peer.budgetShareReason = DisplayBudgetReason::None;
    }
    return changed;
}

std::optional<DisplayBudgetLimits> StationServer::displayBudgetLimitsAsPs3Subscriber(
    quint64 epoch) const
{
    SessionTransport* transport = mediaSessionFor(epoch);
    for (const auto& [sharing, share] : splitDisplayBudget(epoch)) {
        if (sharing == transport) {
            return share.limits;
        }
    }
    return displayBudgetLimits(epoch);
}

SessionTransport* StationServer::mediaSessionFor(quint64 epoch) const
{
    if (epoch == 0) {
        return nullptr;
    }
    for (auto it = m_peers.cbegin(); it != m_peers.cend(); ++it) {
        if (it->mediaEpoch == epoch && it->authenticated) {
            return it.key();
        }
    }
    return nullptr;
}

SessionTransport* StationServer::primaryMediaSession() const
{
    SessionTransport* primary = nullptr;
    quint64 lowest = 0;
    for (auto it = m_peers.cbegin(); it != m_peers.cend(); ++it) {
        if (it->mediaEpoch != 0 && it->authenticated
            && (primary == nullptr || it->mediaEpoch < lowest)) {
            primary = it.key();
            lowest = it->mediaEpoch;
        }
    }
    return primary;
}

QList<quint64> StationServer::mediaSessionEpochs() const
{
    QList<quint64> epochs;
    for (auto it = m_peers.cbegin(); it != m_peers.cend(); ++it) {
        if (it->mediaEpoch != 0 && it->authenticated) {
            epochs.append(it->mediaEpoch);
        }
    }
    std::sort(epochs.begin(), epochs.end());
    return epochs;
}

quint64 StationServer::mediaSessionEpoch() const
{
    SessionTransport* primary = primaryMediaSession();
    return primary != nullptr ? m_peers.value(primary).mediaEpoch : 0;
}

QByteArray StationServer::mediaSessionDevice(quint64 epoch) const
{
    SessionTransport* transport = mediaSessionFor(epoch);
    return transport != nullptr ? m_peers.value(transport).sessionDeviceId : QByteArray();
}

bool StationServer::mediaSessionOwnsSlice(quint64 epoch, int sliceId) const
{
    // Ruling 9.1: a device's own slices, as SliceOwnership records them.
    const QByteArray device = mediaSessionDevice(epoch);
    if (device.isEmpty() || m_radioModel.isNull()
        || m_radioModel->sliceOwnership() == nullptr) {
        return false;
    }
    return m_radioModel->sliceOwnership()->mark(sliceId).owner == device;
}

bool StationServer::mediaAvailableFor(SessionTransport* transport) const
{
    const auto it = m_peers.constFind(transport);
    return m_mediaEnabled && it != m_peers.cend() && it->authenticated && it->mediaEpoch != 0
        && it->snapshotComplete && it->agreedMinor >= kMediaSessionProtocolMinor;
}

bool StationServer::telemetryAvailable() const
{
    return telemetryAvailable(mediaSessionEpoch());
}

bool StationServer::telemetryAvailable(quint64 epoch) const
{
    const auto it = m_peers.constFind(mediaSessionFor(epoch));
    return m_telemetryEnabled && it != m_peers.cend() && it->authenticated
        && it->snapshotComplete && it->agreedMinor >= kStationTelemetrySessionProtocolMinor;
}

bool StationServer::sendTelemetry(const StationTelemetrySnapshot& snapshot,
                                  quint64 expectedEpoch)
{
    if (!telemetryAvailable(expectedEpoch)) { return false; }
    SessionTransport* transport = mediaSessionFor(expectedEpoch);
    SessionMessage message;
    message.kind = SessionMessageKind::StationTelemetry;
    message.telemetry = snapshot;
    // A peer from before host telemetry receives exactly the radio and audio
    // sections it was built for.
    const auto peer = m_peers.constFind(transport);
    if (peer == m_peers.cend() || peer->agreedMinor < kCoreHostTelemetrySessionProtocolMinor) {
        message.telemetry.host = {};
    }
    // Likewise a peer from before receiver load receives exactly the
    // sections it negotiated, without "receivers".
    if (peer == m_peers.cend() || peer->agreedMinor < kReceiverLoadSessionProtocolMinor) {
        message.telemetry.receivers.reset();
        // R-R3-32 (parity Task 6): and without the radio's PA readings and
        // link quality (stationTelemetryVersion 4, minor 11).
        message.telemetry.radio.clearRadioStatus();
    }
    const QByteArray wire = SessionMessages::encode(message);
    if (wire.isEmpty()) { return false; }
    transport->sendText(wire);
    return true;
}

bool StationServer::mediaAvailable() const
{
    return mediaAvailable(mediaSessionEpoch());
}

bool StationServer::mediaAvailable(quint64 epoch) const
{
    return mediaAvailableFor(mediaSessionFor(epoch));
}

bool StationServer::remoteWidebandAvailable() const
{
    return remoteWidebandAvailable(mediaSessionEpoch());
}

bool StationServer::remoteWidebandAvailable(quint64 epoch) const
{
    const auto it = m_peers.constFind(mediaSessionFor(epoch));
    return mediaAvailable(epoch) && it != m_peers.cend()
        && it->agreedMinor >= kRemoteWidebandSessionProtocolMinor;
}

bool StationServer::remoteAudioStatusAvailable() const
{
    return remoteAudioStatusAvailable(mediaSessionEpoch());
}

bool StationServer::remoteAudioStatusAvailable(quint64 epoch) const
{
    const auto it = m_peers.constFind(mediaSessionFor(epoch));
    return mediaAvailable(epoch) && it != m_peers.cend()
        && it->agreedMinor >= kRemoteAudioStatusSessionProtocolMinor;
}

bool StationServer::spectrumGrantAvailable() const
{
    return spectrumGrantAvailable(mediaSessionEpoch());
}

bool StationServer::spectrumGrantAvailable(quint64 epoch) const
{
    const auto it = m_peers.constFind(mediaSessionFor(epoch));
    return mediaAvailable(epoch) && it != m_peers.cend()
        && it->agreedMinor >= kRemoteSpectrumGrantSessionProtocolMinor;
}

bool StationServer::displayExtrasAvailable() const
{
    return displayExtrasAvailable(mediaSessionEpoch());
}

bool StationServer::displayExtrasAvailable(quint64 epoch) const
{
    // Advertised in the minor-11 capabilities block only, so only a peer
    // that agreed minor 11 was told it may ask.
    const auto it = m_peers.constFind(mediaSessionFor(epoch));
    return mediaAvailable(epoch) && it != m_peers.cend()
        && it->agreedMinor >= kRadioIdentitySessionProtocolMinor
        && displayExtrasVersion() >= 1;
}

void StationServer::setDisplayBudgetForReasonPeersOnly(bool reasonPeersOnly)
{
    m_displayBudgetForReasonPeersOnly = reasonPeersOnly;
    recomputeDisplayBudgetShares();
}

std::optional<DisplayBudgetLimits> StationServer::displayBudgetLimits() const
{
    SessionTransport* primary = primaryMediaSession();
    if (primary == nullptr) {
        // What a first session would be given: the whole total.
        return displayBudgetTotalFor(nullptr);
    }
    return displayBudgetLimits(m_peers.value(primary).mediaEpoch);
}

std::optional<DisplayBudgetLimits> StationServer::displayBudgetLimits(quint64 epoch) const
{
    const auto it = m_peers.constFind(mediaSessionFor(epoch));
    if (it == m_peers.cend()) {
        return std::nullopt;
    }
    return it->budgetShare;
}

DisplayBudgetReason StationServer::displayBudgetShareReason(quint64 epoch) const
{
    const auto it = m_peers.constFind(mediaSessionFor(epoch));
    return it != m_peers.cend() ? it->budgetShareReason : DisplayBudgetReason::None;
}

bool StationServer::displayBudgetAvailable() const
{
    return displayBudgetAvailable(mediaSessionEpoch());
}

bool StationServer::displayBudgetAvailable(quint64 epoch) const
{
    const auto it = m_peers.constFind(mediaSessionFor(epoch));
    return mediaAvailable(epoch) && m_displayBudgetEnforcementEnabled
        && displayBudgetLimits(epoch) && it != m_peers.cend()
        && it->agreedMinor >= kRemoteDisplayBudgetSessionProtocolMinor;
}

bool StationServer::sendMediaControl(const QJsonObject& payload, quint64 expectedEpoch)
{
    if (!mediaAvailable(expectedEpoch)) {
        return false;
    }
    SessionMessage message;
    message.kind = SessionMessageKind::MediaControl;
    message.mediaPayload = payload;
    const QByteArray wire = SessionMessages::encode(message);
    if (wire.isEmpty()) {
        return false;
    }
    mediaSessionFor(expectedEpoch)->sendText(wire);
    return true;
}

// ── Capability descriptor ────────────────────────────────────────────────

void StationServer::setSustainableSliceLimit(int slices)
{
    if (slices < 1) {
        return;
    }
    m_sustainableSliceLimit = slices;
}

int StationServer::accessoryStatusVersion() const
{
    return !m_radioModel.isNull() && m_radioModel->stationAccessoryIdentityEnabled() ? 1 : 0;
}

int StationServer::pgxlControlVersion() const
{
    // 3: the amp's own settings (`accessorySettings` and its verbs), sent
    // by the Core's station controller (R-R3-47 / R-R3-22).
    return accessoryStatusVersion() >= 1 ? 3 : 0;
}

// R-R3-49 (parity Task 1): the one list of transmit settings keys a
// receive-only Core takes while its radio is off the air. Today the
// DSP > Options TX combos (DspOptions<Setting><Mode>Tx); later tasks add
// their keys here.
bool StationServer::isTransmitSettingKeyAcceptedOffAir(const QString& key)
{
    // R-R3-49 (parity Task 5): and Setup > Transmit > Power's SWR Protection
    // and External TX Inhibit keys.
    // R-R3-46 / R-R3-49 (parity Task 6): and Setup > PA's PA profiles and
    // PA forward-power table.
    return isTransmitDspOptionsKey(key) || isPowerPageTransmitKey(key)
        || isPaPageTransmitKey(key);
}

bool StationServer::transmitSettingsOffered(SessionTransport* transport) const
{
    // Sent only at agreed minor 11 (the minor-11 capabilities block).
    return transport != nullptr
        && m_peers.value(transport).agreedMinor >= kRadioIdentitySessionProtocolMinor
        && transmitSettingsVersion() >= 1;
}

bool StationServer::receiveOnlyRefusesKey(SessionTransport* transport,
                                          const QString& key) const
{
    if (m_radioModel.isNull() || !m_radioModel->receiveOnlyStationPolicy()
        || !isReceiveOnlyRefusedKey(key)) {
        return false;
    }
    // R-R3-49 (parity Task 1): a key on the off-air list, from a peer that
    // was offered it, is the on-air check's (transmitSettingOnAirRefusal).
    return !(isTransmitSettingKeyAcceptedOffAir(key) && transmitSettingsOffered(transport));
}

QString StationServer::transmitSettingOnAirRefusal(const QString& key) const
{
    QString reason;
    if (m_radioModel.isNull() || !m_radioModel->receiveOnlyStationPolicy()
        || !isTransmitSettingKeyAcceptedOffAir(key)) {
        return reason;
    }
    m_radioModel->stationOnAirRefusal(&reason);
    return reason;
}

int StationServer::bandSelectVersion() const
{
    // The desktop's band button on a slice (RadioModel::onBandButtonClicked),
    // for a band the catalogue's `bands` lists.
    return m_radioModel ? 1 : 0;
}

int StationServer::transmitSettingsVersion() const
{
    // 1: `transmit` writes outside the keying set and the DspOptions*Tx
    // keys, taken off the air and refused on it (R-R3-49, parity Task 1).
    // 2: the TX and Phone/CW applets' settings on `transmit` (tunePower,
    // the VOX level and delay, MON and its level, LEV, EQ, CFC, PROC and
    // its level, AM carrier, DEXP, mic level), tunePowerForTxBand and
    // tuneDrivePowerSource, and setTunePowerForTxBand (parity Task 2).
    // 3: the radio microphone settings (micBoost, micXlr, micTipRing,
    // micBias, micPttDisabled, lineIn, lineInBoost), the Core's TX profiles
    // (activeTxProfile, txProfilesJson), txProfile.select / save / delete
    // and rade.resetVocoder (parity Task 3).
    // 4: the TX EQ (txEqUseLegacy, txEqPreamp, txEqBandsJson,
    // txEqFreqsJson, txEqNc, txEqMp, txEqCtfmode, txEqWintype,
    // txEqParaEqData), CFC (cfcCompressionJson, cfcEqFreqJson,
    // cfcPostEqBandGainJson, cfcPostEqEnabled, cfcPostEqGainDb,
    // cfcPrecompDb, cfcParaEqData), phase rotator, CESSB, leveler and ALC
    // settings; a band array is refused whole (parity Task 4).
    // 5: Setup > Transmit > Power (tuneDrivePowerSource writable,
    // powerByBandJson, tunePowerByBandJson; ATT on TX, its value and Force
    // ATT on `stepAtt`; the SWR Protection and External TX Inhibit keys
    // taken off the air), DEXP/VOX (the DEXP timings, look-ahead,
    // side-channel filter and antiVoxGainDb) and Test > Two-Tone IMD (the
    // two-tone settings) (parity Task 5).
    // 6: Setup > PA (PA Gain's profiles, per-band gains, adjust matrix and
    // max power; the Watt Meter's PA forward-power table): the
    // hardware/<mac>/pa/... and hardware/<mac>/paCalibration/... keys taken
    // off the air and applied to the Core's PA profiles and calibration at
    // once (parity Task 6).
    return m_radioModel.isNull() ? 0 : 6;
}

int StationServer::tgxlControlVersion() const
{
    // 2: the tuner's antenna, operate and bypass (setTgxlAntenna,
    // setTgxlOperate, setTgxlBypass), R-R3-49 / R-R3-47.
    // 3: setTgxlOperate on puts the tuner in OPERATE whole (bypass off and
    // operate on, one command), R-R3-49 fix wave.
    return accessoryStatusVersion() >= 1 ? 3 : 0;
}

int StationServer::rfKitControlVersion() const
{
    // 3: Reset amp error (resetRfKitError), and the Core applies a window's
    // RF-Kit auto-reconnect and poll interval at once (R-R3-47 fix wave).
    return accessoryStatusVersion() >= 1 ? 3 : 0;
}

int StationServer::stationTciVersion() const
{
    return !m_radioModel.isNull() && m_radioModel->stationTciController() != nullptr ? 1 : 0;
}

int StationServer::accessoryDataVersion() const
{
    return accessoryStatusVersion() >= 1 && !m_radioModel.isNull()
            && m_radioModel->stationAccessoryData() != nullptr
        ? 1 : 0;
}

int StationServer::radioHardwareVersion() const
{
    if (m_radioModel.isNull() || !m_radioModel->stepAttFacade()->isBound()) {
        return 0;
    }
    if (!m_radioModel->alexAntennaFacade()->isBound()) {
        return 1;
    }
    // 3: the `ioBoard` object and the per-band antenna verb
    // (setAlexRxAntenna), R-R3-46 fix wave. 4: the filter policy verb
    // (setAlexBpfMode), R-R3-46 / R-R3-21, applied through the same
    // `alexAntennas` facade.
    return m_radioModel->ioBoardFacade()->isBound() ? 4 : 2;
}

StationCapabilities StationServer::buildCapabilities() const
{
    // What the primary media session is told (with none, what a first
    // session would be told), as the one session was before Task 71.
    return buildCapabilitiesFor(primaryMediaSession());
}

StationCapabilities StationServer::buildCapabilitiesFor(SessionTransport* transport) const
{
    StationCapabilities caps;
    caps.settingsSchemaVersion = settingsSchemaVersionOf(m_settings);
    // iPhone app Task 76: every admitted session has media and telemetry
    // (the topology note). Before any session, what a first one is told.
    const auto self = m_peers.constFind(transport);
    const bool admitted = transport == nullptr
        || (self != m_peers.cend() && self->authenticated && self->mediaEpoch != 0);
    const bool media = m_mediaEnabled && admitted;
    const bool telemetry = m_telemetryEnabled && admitted;
    if (m_radioModel.isNull()) {
        return caps;
    }

    const BoardCapabilities& board = m_radioModel->boardCapabilities();

    caps.stationName = m_radioModel->name();
    caps.radioModelName = m_radioModel->model();
    caps.firmwareVersion = m_radioModel->version();
    caps.macAddress = m_radioModel->currentRadioMac();
    caps.board = board.board;
    caps.radioConnected = m_radioModel->isConnected();

    // R-R3-46: which radio, how the Core talks to it, and where it is, for a
    // window that negotiated them; an older one gets today's descriptor.
    // The model is the Core's own profile (an operator's model choice
    // included). With no radio yet the profile has no row and the model is
    // not reported; with no radio ever selected neither is the rest.
    {
        const auto peer = m_peers.constFind(transport);
        if (peer != m_peers.cend()
            && peer->agreedMinor >= kRadioIdentitySessionProtocolMinor) {
            caps.radioIdentityEntries = true;
            // R-R3-46 / R-R3-11: 1 once the Core's controller is behind the
            // `stepAtt` object (DaemonApp binds it before the server starts);
            // 2 once its Alex antennas are behind `alexAntennas` too, with
            // the hardware apply step and the I/O board probe; 3 with the
            // read-only `ioBoard` object and the per-band antenna verb; 4
            // with the filter policy verb.
            caps.radioHardwareVersion = radioHardwareVersion();
            // R-R3-47 / R-R3-22: 1 on a Core that owns its accessories: the
            // read-only `amplifier` and `rfkit` objects. The Power Genius is 2
            // there: also configurePgxl, disconnectPgxl and
            // setPgxlConnectionSettings.
            caps.remotePgxlControlVersion = pgxlControlVersion();
            // R-R3-47: the RF-Kit is 2 there too: its interface, antenna,
            // tuner and band-follow rows, and configureRfKit,
            // disconnectRfKit and setRfKitEnabled.
            caps.remoteRfKitControlVersion = rfKitControlVersion();
            // R-R3-48: the Core's own station TCI server.
            caps.stationTciVersion = stationTciVersion();
            // R-R3-47 / R-R3-22: the Core's accessory records and settings.
            caps.accessoryDataVersion = accessoryDataVersion();
            // R-R3-47 / R-R3-22: the Tuner Genius's own settings.
            caps.remoteTgxlControlVersion = tgxlControlVersion();
            // iPhone app Task 12 (R-IOS-08): device sign-in by key, last.
            caps.stationIdentityVersion = m_certBinding.isEmpty() ? 0 : 1;
            // iPhone app Task 13: the devices object and its verbs.
            caps.deviceAdminVersion = deviceAdminVersion();
            // iPhone app Task 14: pairing and the pairing window, last.
            caps.pairingVersion = pairingVersion();
            // iPhone app Task 19: the catalogue.
            caps.stationCatalogVersion = stationCatalogVersion();
            // iPhone app Task 20: display extras.
            caps.displayExtrasVersion = media ? displayExtrasVersion() : 0;
            // R-R3-49 (parity Task 1): the transmit settings.
            caps.transmitSettingsVersion = transmitSettingsVersion();
            // R-IOS-27, R-IOS-06: slice.selectBand.
            caps.bandSelectVersion = bandSelectVersion();
            // iPhone app Task 71 (ruling 10.1): several devices at once, for
            // a peer that declared sessionHolder with deviceAuth; any other
            // peer is sent no entry, so its capabilities are today's.
            if (peerHoldsSessions(transport)) {
                caps.sessionHolderEntry = true;
                caps.sessionHolderVersion = sessionHolderVersion();
            }
            const HardwareProfile& profile = m_radioModel->hardwareProfile();
            caps.hpsdrModel = profile.caps != nullptr ? profile.model : HPSDRModel::FIRST;
            const RadioInfo& radio = m_radioModel->currentRadioInfo();
            if (!radio.macAddress.isEmpty()) {
                caps.radioProtocol = static_cast<int>(radio.protocol);
                caps.radioAddress = radio.address.isNull() ? QString()
                                                           : radio.address.toString();
            }
        }
    }

    caps.boardMaxSlices = board.maxSlices > 0 ? board.maxSlices : 1;
    // Plan Task 11: the stream count for the protocol the Core runs (four
    // on Protocol 1), the same number its own stream pool uses.
    caps.userDdcCount = m_radioModel->userStreamCount();
    caps.pureSignalPresent = board.hasPureSignal;

    // EFFECTIVE, not board (parent section 4.5). R2 has no PerfMonitor to
    // compute a sustainable number, so the effective value is whatever an
    // operator configured, clamped to what the radio can actually do --
    // advertising more slices than the board has would be a worse failure
    // than advertising fewer.
    caps.effectiveMaxSlices = m_sustainableSliceLimit > 0
                                  ? std::min(m_sustainableSliceLimit, caps.boardMaxSlices)
                                  : caps.boardMaxSlices;

    // Always false in R2: TX is R4 in its entirety.
    caps.txPermitted = false;
    caps.remoteMediaVersion = media ? 1 : 0;
    caps.remoteWidebandDisplayVersion = media ? 1 : 0;
    caps.remoteAudioStatusVersion = media ? 1 : 0;
    caps.spectrumGrantVersion = media ? 1 : 0;
    // R-R3-23: lossless audio beside Opus. Advertised with media whatever
    // nereusd.conf audio_lossless says, so a GUI can be told plainly when
    // the Core's own setting refuses it.
    caps.audioProfileVersion = media ? 1 : 0;
    // R-R3-35: the Core answers audio clock probes whenever media is on.
    caps.audioClockVersion = media ? 1 : 0;
    // R-R3-43: a receiver's own audio on its own stream, whenever media is on.
    caps.receiverAudioVersion = media ? 1 : 0;
    // R-R3-45: the headphones mix on its own stream, whenever media is on.
    caps.headphonesMixVersion = media ? 1 : 0;
    // iPhone app Task 76 (ruling 9.3): this session's own share of the
    // budget, with its own generation and reason.
    const std::optional<DisplayBudgetLimits> budget = transport == nullptr
        ? displayBudgetTotalFor(nullptr)
        : (self != m_peers.cend() ? self->budgetShare : std::nullopt);
    if (media && m_displayBudgetEnforcementEnabled && budget) {
        caps.remoteDisplayBudgetVersion = 1;
        caps.displayBudget = budget;
        // Subscribed for this session: the PureSignal display goes to its
        // subscriber alone (ruling 9.3 item 4).
        const quint64 epoch = self != m_peers.cend() ? self->mediaEpoch : 0;
        const quint64 subscriber = m_ps3SubscriberEpoch != 0 ? m_ps3SubscriberEpoch
                                                             : mediaSessionEpoch();
        caps.remotePs3DisplaySubscribed =
            m_radioModel->pureSignalFacade()->remoteAmpViewSubscribed()
            && (transport == nullptr || epoch == subscriber);
        // R-R3-08/37: only a peer that negotiated the reason receives it; an
        // older one gets exactly the five budget fields it was built for.
        // Task 76 (design ruling 9.3a): the shared reasons only to a peer
        // that declared sessionHolder; another hears coreBusy or none.
        if (self != m_peers.cend()
            && self->agreedMinor >= kDisplayBudgetReasonSessionProtocolMinor) {
            const DisplayBudgetReason reason = self->budgetShareReason;
            caps.displayBudgetReason = peerHoldsSessions(transport)
                ? reason : displayBudgetReasonForOlderDevice(reason);
        }
    }
    caps.remoteCtunVersion = 1;
    // 4 (R-R3-32, parity Task 6): the radio section also carries the Core's
    // PA readings and radio link quality, for a peer at minor 11.
    caps.stationTelemetryVersion = telemetry ? 4 : 0;
    caps.remoteTgxlConfigVersion = m_radioModel->stationAccessoryIdentityEnabled() ? 1 : 0;
    caps.remoteFourO3AControlVersion = m_radioModel->stationAccessoryIdentityEnabled() ? 1 : 0;
    caps.propertyResultVersion = 1;
    // R-R3-21 / R-R3-09: the Core owns the notch list (the `notches` object
    // and the notch.* commands). Independent of WDSP: the list lives on
    // NotchModel whether or not channels exist. 2 (R-IOS-27, R-IOS-06):
    // also notch.addAtSlice, the desktop's +TNF on a slice.
    caps.notchControlVersion = 2;
#ifdef HAVE_WDSP
    caps.wdspVersion = 210;
    caps.wdspCompatibilityVersion = 1;
    caps.nnrVersion = 1;
    caps.psAlgorithmVersion = 3;
    // 2 (R-R3-21): NR3 models are Core assets (kind 2, selectNr3Model).
    caps.dspAssetVersion = 2;
    caps.psDisplayVersion = media ? 1 : 0;
#endif

    return caps;
}

} // namespace NereusSDR
