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
// =================================================================

#include "core/session/StationServer.h"

#include "core/AppSettings.h"
#include "core/BoardCapabilities.h"
#include "core/HardwareProfile.h"
#include "core/dsp/NnrSettings.h"
#include "core/security/CertificateStore.h"
#include "core/security/TokenStore.h"
#include "core/session/MirrorPolicy.h"
#include "core/session/MirrorSchema.h"
#include "core/session/ObjectRegistry.h"
#include "core/session/SessionCommandDispatcher.h"
#include "core/session/SessionTransport.h"
#include "core/session/StateMirror.h"
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
#include "models/TunerModel.h"

#include <QLoggingCategory>
#include <QSet>
#include <QSslConfiguration>
#include <QSslSocket>
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

// R-R3-47: why a raw write of the RF-Kit switch is refused. A current app
// sends setRfKitEnabled; an older one only ever wrote the value.
constexpr const char* kRfKitSwitchWriteReason =
    "Update this app to turn the RF-Kit amplifier on or off on this Core.";

// The one reason a receive-only Core gives for every transmit
// configuration write it refuses: direct TransmitModel property writes and
// the DSP > Options TX settings keys alike (R-R3-21).
constexpr const char* kReceiveOnlyTransmitReason =
    "Transmit configuration is unavailable on this receive-only station.";

// R-IOS-01: the one reason for a write to a property MirrorPolicy marks
// outbound (the station's own readings and derived values, and properties
// with a command of their own). Refused before anything is applied, so a
// model's inbound hook, which exists to apply the station's reports on a
// client, never runs on the station for a client's write.
constexpr const char* kOutboundWriteReason =
    "The station sets this itself; it cannot be changed from here.";

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

// Every settings key a receive-only Core refuses as transmit configuration.
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
// ensureSettingsAtVersion(6) call), and duplicating it here would create a
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

StationServer::StationServer(RadioModel* radioModel, AppSettings& settings,
                             const QString& securityDirectory, QObject* parent)
    : QObject(parent)
    , m_radioModel(radioModel)
    , m_settings(settings)
    , m_securityDirectory(securityDirectory.isEmpty() ? CertificateStore::defaultDirectory()
                                                      : securityDirectory)
{
    // Every capability set this R3 server advertises is receive-only. Make
    // that a persistent property of the hardware-owning model as well, so a
    // standalone StationServer host cannot admit a TX/accessory side effect
    // through a local callback. DaemonApp installs the same policy earlier,
    // before startup; neither owner clears it when a session ends.
    if (m_radioModel) {
        m_radioModel->setReceiveOnlyStationPolicy(true);
    }

    m_certificates = std::make_unique<CertificateStore>(m_securityDirectory);
    m_tokens = std::make_unique<TokenStore>(m_securityDirectory);

    // Step 4: the token distribution mechanism parent section 7.1 requires
    // be specified before R2. Printed ONCE, on the run that generates it,
    // beside the certificate fingerprint the client pins (section 10.5) --
    // the two things an operator has to carry to the client by hand, in
    // one place, at the one moment they are new. Deliberately not repeated
    // on later starts: a secret echoed into every log file forever is a
    // different problem from a secret nobody can find.
    if (m_tokens->wasGeneratedThisRun()) {
        writePairingBanner(formatPairingBanner(m_tokens->token(),
                                               m_certificates->fingerprintSha256(),
                                               m_securityDirectory));
        // The LOG gets a pointer, never either secret. Without this line a
        // first run leaves no trace at all in the file an operator goes
        // looking in, which is its own support problem; with it, the log
        // says what happened and where the values went without carrying
        // them. See writePairingBanner() for why they went to stdout.
        qCInfo(lcStation)
            << "First run for this profile: a pairing token and a TLS certificate "
               "fingerprint were generated and printed to stdout. Both are "
               "deliberately kept out of this log file.";
    }
    if (!m_tokens->isValid()) {
        qCWarning(lcStation) << "Auth token unavailable:" << m_tokens->lastError();
    }

    m_mirror = new StateMirror(this);
    m_registry = new ObjectRegistry(radioModel, m_mirror, this);
    m_dispatcher = new SessionCommandDispatcher(radioModel, this);
    m_settingsServer = new SettingsProxyServer(settings, this);
    // R-R3-46: the Core applies hardware settings for its connected radio
    // only; a write naming any other radio's MAC is refused.
    if (radioModel) {
        m_settingsServer->setConnectedMacProvider([model = QPointer<RadioModel>(radioModel)] {
            return model ? model->currentRadioMac() : QString();
        });
    }

    // Outbound: everything the daemon has to say goes to whichever
    // transport currently holds the session, and to nothing at all when
    // there is none.
    connect(m_mirror, &StateMirror::sessionMessageReady, this,
            [this](const SessionMessage& message) { sendToSession(message); });
    connect(radioModel, &RadioModel::receiveLayoutHydrated, this, [this] {
        if (m_session && m_mirrorBuilt) {
            // Shared slice QObjects were restored without individual notify
            // signals. Re-seed their entire settled state on the same session.
            m_mirror->attachSession();
        }
    });
    connect(m_dispatcher, &SessionCommandDispatcher::commandResultReady, this,
            [this](const SessionMessage& result) { sendToSession(result); });
    connect(m_settingsServer, &SettingsProxyServer::outboundValueChanged, this,
            [this](const QString& key, const QVariant& value, const QString& originTag) {
                sendToSession(
                    SessionMessages::settingsValue(key, value.toString(), originTag));
            });
    // Whole-branch review, Important 4. A removal has its own signal and
    // its own frame. It used to arrive here as an outboundValueChanged
    // carrying an INVALID QVariant, and the value.toString() above turned
    // that into "" -- so every client cached an empty string for a key
    // the station no longer had, and a client that had just correctly
    // removed the key itself had it resurrected by the echo.
    connect(m_settingsServer, &SettingsProxyServer::outboundValueRemoved, this,
            [this](const QString& key) {
                sendToSession(SessionMessages::settingsValueAbsent(key, QString()));
            });

    // A daemon can authenticate a GUI before its configured radio is
    // discoverable.  currentRadioChanged is emitted only after RadioModel's
    // Connected handlers have populated the live identity and profile, but
    // defer the wire update one event turn so every other observer of that
    // signal has finished too.  Capture the current session now: a later
    // authenticated replacement already received its own initial snapshot,
    // and an old callback must never refresh it.
    if (m_radioModel) {
        connect(m_radioModel, &RadioModel::currentRadioChanged, this,
                [this](const NereusSDR::RadioInfo&) {
                    const QPointer<SessionTransport> session(m_session);
                    const quint64 sessionEpoch = m_mediaSessionEpoch;
                    if (session.isNull()) {
                        return;
                    }
                    QTimer::singleShot(0, this, [this, session, sessionEpoch]() {
                        if (session.isNull()) {
                            return;
                        }
                        sendCapabilitiesAndSettingsSnapshot(session.data(), sessionEpoch);
                    });
                });
    }

    // ObjectRegistry's create/destroy events are the lifecycle half of the
    // mirror; StateMirror only carries property deltas for objects it
    // already knows about.
    connect(m_registry, &ObjectRegistry::objectCreated, this,
            [this](const QByteArray& objectKey, const QByteArray& className, int,
                   const QList<MirrorUpdate>& snapshot) {
                sendToSession(
                    SessionMessages::objectCreate(objectKey, className, snapshot));
            });
    connect(m_registry, &ObjectRegistry::objectDestroyed, this,
            [this](const QByteArray& objectKey, const QByteArray& className, int) {
                sendToSession(SessionMessages::objectDestroy(objectKey, className));
            });

    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(m_heartbeatIntervalMs);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &StationServer::onHeartbeatTick);

    m_deltaFlushTimer = new QTimer(this);
    m_deltaFlushTimer->setInterval(kDefaultDeltaFlushMs);
    connect(m_deltaFlushTimer, &QTimer::timeout, this, [this]() {
        if (m_session != nullptr && m_mirror != nullptr) {
            m_mirror->flushCoalescedDeltas();
        }
    });
}

StationServer::~StationServer()
{
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
                            << m_wsServer->serverPort();
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
    if (!m_tokens->isValid()) {
        // Listening with no token would accept nobody, forever, while
        // looking healthy. Refuse loudly instead.
        m_lastError = m_tokens->lastError().isEmpty()
                          ? QStringLiteral("No authentication token available")
                          : m_tokens->lastError();
        qCWarning(lcStation) << "Refusing to listen:" << m_lastError;
        return false;
    }

    if (m_wsServer == nullptr) {
        m_wsServer = new QWebSocketServer(QStringLiteral("NereusSDR station"),
                                          QWebSocketServer::SecureMode, this);
        connect(m_wsServer, &QWebSocketServer::newConnection, this,
                &StationServer::onNewWebSocketConnection);
    }

    QSslConfiguration tls = QSslConfiguration::defaultConfiguration();
    tls.setLocalCertificate(m_certificates->certificate());
    tls.setPrivateKey(m_certificates->privateKey());
    // The client pins this certificate's fingerprint (parent design
    // section 10.5), so it is the client's job to decide whether to trust
    // it. Asking for a client certificate here would be a second,
    // unimplemented identity mechanism.
    tls.setPeerVerifyMode(QSslSocket::VerifyNone);
    m_wsServer->setSslConfiguration(tls);

    if (!m_wsServer->listen(address, port)) {
        m_lastError = m_wsServer->errorString();
        qCWarning(lcStation) << "Listen failed:" << m_lastError;
        return false;
    }

    qCInfo(lcStation) << "Station listening on wss://" << address.toString() << ":"
                      << m_wsServer->serverPort();
    emit listeningChanged(true);
    return true;
}

void StationServer::close()
{
    const bool wasListening = isListening();
    const QList<SessionTransport*> transports = m_peers.keys();
    for (SessionTransport* transport : transports) {
        dropPeer(transport, QStringLiteral("station shutting down"), true,
                 /*retryable=*/true);
    }
    if (m_wsServer != nullptr) {
        m_wsServer->close();
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
    return m_wsServer != nullptr && m_wsServer->isListening();
}

quint16 StationServer::serverPort() const
{
    return m_wsServer != nullptr ? m_wsServer->serverPort() : 0;
}

QHostAddress StationServer::serverAddress() const
{
    return isListening() ? m_wsServer->serverAddress() : QHostAddress{};
}

QString StationServer::token() const
{
    return m_tokens != nullptr ? m_tokens->token() : QString();
}

QString StationServer::certificateFingerprint() const
{
    return m_certificates != nullptr ? m_certificates->fingerprintSha256() : QString();
}

QString StationServer::formatPairingBanner(const QString& token,
                                           const QString& fingerprint,
                                           const QString& storedIn)
{
    return QStringLiteral(
               "\n"
               "  ============================================================\n"
               "  NereusSDR station: first run, pairing details\n"
               "  ------------------------------------------------------------\n"
               "  Token:       %1\n"
               "  TLS SHA-256: %2\n"
               "  Stored in:   %3\n"
               "  ------------------------------------------------------------\n"
               "  Give both to the client. They are printed once, here, on\n"
               "  stdout, and are deliberately kept out of the log file.\n"
               "  ============================================================\n")
        .arg(token, fingerprint, storedIn);
}

bool StationServer::hasAuthenticatedSession() const
{
    return m_session != nullptr;
}

// ── Peer lifecycle ───────────────────────────────────────────────────────

void StationServer::onNewWebSocketConnection()
{
    while (m_wsServer != nullptr && m_wsServer->hasPendingConnections()) {
        QWebSocket* socket = m_wsServer->nextPendingConnection();
        if (socket == nullptr) {
            break;
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

    // Peer cap. Refused BEFORE any state is allocated for it, and with a
    // reason on the wire so a legitimate client that hits this knows why
    // rather than seeing an unexplained close. Only ever one session is
    // authenticated (see the topology decision in the header), so this
    // bounds peers that are mid-handshake.
    if (m_peers.size() >= kMaxConcurrentPeers) {
        qCWarning(lcStation) << "Refusing connection from" << transport->peerDescription()
                             << ": already at" << kMaxConcurrentPeers << "peers";
        // RETRYABLE, and this is the one that mattered most. The header
        // sizes kMaxConcurrentPeers for "one client, and a couple of stale
        // sockets from a reconnecting client", so this cap is expected to
        // be hit BY a reconnecting client, transiently, while its own dead
        // sockets are still draining. Sent as permanent, it told exactly
        // that client to stop trying forever.
        transport->sendText(SessionMessages::encode(SessionMessages::sessionEnd(
            QStringLiteral("Station is at its concurrent-connection limit"),
            /*retryable=*/true)));
        transport->closeLink(QStringLiteral("peer limit reached"));
        transport->deleteLater();
        return;
    }

    Peer peer;
    peer.transport = transport;
    peer.description = transport->peerDescription();

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
            dropPeer(transport, QStringLiteral("handshake deadline expired"), true,
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
    send(transport,
         SessionMessages::hello(kSessionProtocolMajor, kSessionProtocolMinor,
                                settingsSchemaVersionOf(m_settings),
                                peerNameForThisProcess()));

    qCDebug(lcStation) << "Peer attached:" << peer.description;
}

void StationServer::onTransportClosed(SessionTransport* transport)
{
    dropPeer(transport, QStringLiteral("peer closed the link"), false,
             /*retryable=*/true);
}

void StationServer::dropPeer(SessionTransport* transport, const QString& reason,
                             bool sendSessionEnd, bool retryable)
{
    auto it = m_peers.find(transport);
    if (it == m_peers.end()) {
        return;
    }
    const QString description = it->description;

    if (sendSessionEnd) {
        send(transport, SessionMessages::sessionEnd(reason, retryable));
    }
    m_peers.erase(it);

    if (m_session == transport) {
        m_session = nullptr;
        m_dispatcher->setSessionOwner({});
        if (m_radioModel) {
            m_radioModel->pureSignalFacade()->resetSession();
        }
        if (!m_radioModel.isNull()) {
            m_radioModel->clearStreamCtunPins();
        }
        emit mediaSessionEnded(m_mediaSessionEpoch);
        emit telemetrySessionEnded(m_mediaSessionEpoch);
        // Stop draining deltas into nothing. StateMirror keeps watching --
        // the daemon's own state is not the session's to tear down -- and
        // the next attachSession() clears whatever the coalescer holds
        // anyway, because a fresh burst already carries every watched
        // object's current value.
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
            dropPeer(transport, QStringLiteral("heartbeat timeout"), true,
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
        dropPeer(transport, QStringLiteral("undecodable message"), true,
                 /*retryable=*/false);
        return;
    }

    switch (message.kind) {
    case SessionMessageKind::Hello:
        handleHello(transport, message);
        return;
    case SessionMessageKind::AuthRequest:
        handleAuthRequest(transport, message);
        return;
    default:
        break;
    }

    if (!it->authenticated) {
        // Everything below this line moves radio or settings state. A peer
        // that has not proved it holds the token gets exactly one answer.
        dropPeer(transport, QStringLiteral("message sent before authentication"), true,
                 /*retryable=*/false);
        return;
    }

    switch (message.kind) {
    case SessionMessageKind::CommandInvoke:
        if (message.commandVerb == "setFourO3AEnabled"
            && it->agreedMinor < kRemoteFourO3AControlSessionProtocolMinor) {
            send(transport, SessionMessages::commandResult(
                message.commandVerb, message.commandId, false,
                QStringLiteral("Remote 4O3A control requires a newer station protocol."), {}));
            break;
        }
        if ((message.commandVerb.startsWith("nnr.") || message.commandVerb.startsWith("ps3.")
             || message.commandVerb.startsWith("dspAssets."))
            && it->agreedMinor < kDspControlSessionProtocolMinor) {
            send(transport, SessionMessages::commandResult(
                message.commandVerb, message.commandId, false,
                QStringLiteral("This DSP action requires a newer station protocol."), {}));
            break;
        }
        if (message.commandVerb == "nnr.tryAgain"
            && it->agreedMinor < kNnrLimitSessionProtocolMinor) {
            send(transport, SessionMessages::commandResult(
                message.commandVerb, message.commandId, false,
                QStringLiteral("This station cannot try noise reduction again. "
                               "Update the station software."), {}));
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
        if (message.commandVerb == "setStationTci"
            && (it->agreedMinor < kRadioIdentitySessionProtocolMinor
                || stationTciVersion() < 1)) {
            send(transport, SessionMessages::commandResult(
                message.commandVerb, message.commandId, false,
                it->agreedMinor < kRadioIdentitySessionProtocolMinor
                    ? QStringLiteral("Update this app to turn the station's TCI server on or off.")
                    : QStringLiteral("This Core has no TCI server for the station."), {}));
            break;
        }
        if ((message.commandVerb == "configureTgxl" || message.commandVerb == "disconnectTgxl")
            && it->agreedMinor < kRemoteTgxlConfigSessionProtocolMinor) {
            send(transport, SessionMessages::commandResult(
                message.commandVerb, message.commandId, false,
                QStringLiteral("Remote TGXL configuration requires a newer station protocol."), {}));
            break;
        }
        if ((message.commandVerb == "requestStreamCtunPinned"
             || message.commandVerb == "requestStreamCentre")
            && it->agreedMinor < kRemoteCtunSessionProtocolMinor) {
            send(transport, SessionMessages::commandResult(
                message.commandVerb, message.commandId, false,
                QStringLiteral("Remote C-Tune requires a newer station protocol."), {}));
            break;
        }
        m_dispatcher->dispatch(message);
        break;
    case SessionMessageKind::MediaControl:
        if (transport == m_session && mediaAvailable()) {
            emit mediaControlReceived(message.mediaPayload, m_mediaSessionEpoch);
        }
        break;
    case SessionMessageKind::PropertyWrite:
        handlePropertyWrite(transport, message);
        break;
    case SessionMessageKind::SettingsWrite:
        handleSettingsWrite(transport, message);
        break;
    case SessionMessageKind::SettingsRemove:
        handleSettingsRemove(message);
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
        dropPeer(transport, QStringLiteral("duplicate hello"), true, /*retryable=*/false);
        return;
    }
    it->helloReceived = true;

    // Parent design section 7.0's version policy, both halves.
    if (message.protocolMajor != kSessionProtocolMajor) {
        const QString reason =
            QStringLiteral("Protocol major version mismatch: station speaks %1.%2, "
                           "client speaks %3.%4. A differing major means an "
                           "incompatible wire contract.")
                .arg(kSessionProtocolMajor)
                .arg(kSessionProtocolMinor)
                .arg(message.protocolMajor)
                .arg(message.protocolMinor);
        qCWarning(lcStation) << reason;
        // NOT retryable: an incompatible wire contract does not become
        // compatible by being dialed again. The operator has to upgrade
        // one end.
        dropPeer(transport, reason, true, /*retryable=*/false);
        return;
    }

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
                       << "agreed minor" << it->agreedMinor;
}

void StationServer::handleAuthRequest(SessionTransport* transport,
                                      const SessionMessage& message)
{
    auto it = m_peers.find(transport);
    if (it == m_peers.end()) {
        return;
    }
    if (!it->helloReceived) {
        dropPeer(transport, QStringLiteral("auth before hello"), true, /*retryable=*/false);
        return;
    }
    if (it->authenticated) {
        dropPeer(transport, QStringLiteral("duplicate auth"), true, /*retryable=*/false);
        return;
    }

    const QString description = it->description;

    // The candidate token is never logged, at any level, on any path.
    const TokenStore::VerifyResult result = m_tokens->verify(message.token);
    if (result != TokenStore::VerifyResult::Accepted) {
        // THE distinction TokenStore.h says the two results exist to
        // preserve, carried through to the client's retry policy.
        //
        // RateLimited is retryable: it is transient BY CONSTRUCTION -- the
        // lockout expires on TokenStore's own timer, and the refusal text
        // literally says "try again later". Crucially, the rate limiter is
        // global rather than per-peer (TokenStore.h:44-48 says so outright:
        // a lockout refuses a connection "including one carrying the
        // correct token"), so five bad guesses from anyone who can reach
        // the port refuse the OPERATOR too. Marked permanent, that turned
        // somebody else's failed guesses into the operator being locked
        // out of their own station with no automatic recovery.
        //
        // Rejected is NOT retryable: the token is simply wrong, redialing
        // cannot make it right, and a client that retried forever would
        // feed the very rate limiter above and keep the station locked out
        // on the operator's own behalf.
        const bool rateLimited = result == TokenStore::VerifyResult::RateLimited;
        const QString reason =
            rateLimited
                ? QStringLiteral("Too many failed authentication attempts; try again later")
                : QStringLiteral("Authentication failed");
        send(transport, SessionMessages::authResult(false, reason, rateLimited));
        qCWarning(lcStation) << "Authentication refused for" << description << ":" << reason;
        dropPeer(transport, reason, false, rateLimited);
        return;
    }

    it->authenticated = true;
    send(transport, SessionMessages::authResult(true, QString(), /*retryable=*/false));
    promoteToSession(transport);
}

void StationServer::promoteToSession(SessionTransport* transport)
{
    const QString description = m_peers.value(transport).description;

    // Parent design section 7.1: "A second authenticated connection
    // preempts the existing session ... The displaced session is told
    // why." The token is the authority, and the realistic sequence is the
    // same operator reconnecting after a link drop from a different
    // device. Leaving the stale session in place, or refusing the second
    // connection, locks the operator out of their own transmitter for an
    // undefined interval.
    if (m_session != nullptr && m_session != transport) {
        const QString displaced = m_peers.contains(m_session)
                                      ? m_peers.value(m_session).description
                                      : QStringLiteral("<unknown>");
        const QString reason =
            QStringLiteral("Displaced by a newer authenticated connection from %1")
                .arg(description);
        qCInfo(lcStation) << "Preempting session" << displaced << "for" << description;
        // NOT retryable, and deliberately so even though the CONDITION is
        // transient. Another authenticated peer has deliberately taken the
        // session; a displaced client that redialed on a backoff would
        // preempt the newcomer straight back, and the two would trade the
        // radio between them indefinitely. Section 7.1 makes the token the
        // authority, so the most recent authenticated connection wins and
        // the displaced operator reconnects by hand.
        dropPeer(m_session, reason, true, /*retryable=*/false);
        emit sessionPreempted(displaced);
    }

    // BEFORE m_session is assigned, deliberately. buildMirror() ends in
    // ObjectRegistry::backfillExistingSlices(), which emits objectCreated
    // for every slice the daemon already holds -- and those are wired
    // straight to sendToSession(). With m_session still null they are
    // dropped, which is exactly right: attachSession() below sends an
    // object.create for every watched object anyway, AFTER the schema
    // messages that a client needs in order to make sense of one. Assign
    // first and the backfill's creates go out ahead of any schema, and
    // then get sent a second time by the burst.
    buildMirror();

    m_session = transport;
    ++m_mediaSessionEpoch;
    m_radioModel->pureSignalFacade()->resetSession();
    m_dispatcher->setSessionOwner(QStringLiteral("station:%1").arg(m_mediaSessionEpoch));

    if (!sendCapabilitiesAndSettingsSnapshot(transport, m_mediaSessionEpoch)) {
        return;
    }

    // State snapshot, model half, ending in the snapshot-complete marker.
    // attachSession() emits the whole burst synchronously through
    // sessionMessageReady before it returns, which reaches sendToSession()
    // above -- and m_session is already set by now, which is what makes
    // the burst go anywhere at all.
    m_mirror->attachSession();
    if (m_session != transport || !m_peers.contains(transport)) {
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
    if (m_session == transport && mediaAvailable()) {
        emit mediaSessionStarted(m_mediaSessionEpoch);
    }
    if (m_session == transport && telemetryAvailable()) {
        emit telemetrySessionStarted(m_mediaSessionEpoch);
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
    // preamp. Sent only to a peer at minor 11 (sendToSession).
    m_mirror->watch(QByteArray(kStepAttKey), m_radioModel->stepAttFacade());
    // R-R3-46 (radioHardwareVersion 2): the Core's Alex antenna settings.
    // Sent only to a peer at minor 11 (sendToSession).
    m_mirror->watch(QByteArray(kAlexAntennasKey), m_radioModel->alexAntennaFacade());
    // R-R3-46 (radioHardwareVersion 3): the Core's HL2 I/O board, read-only.
    // Sent only to a peer at minor 11 (sendToSession).
    m_mirror->watch(QByteArray(kIoBoardKey), m_radioModel->ioBoardFacade());
    m_mirror->watch("pureSignal", m_radioModel->pureSignalFacade());
    m_mirror->watch(QByteArray(kTransmitKey), &m_radioModel->transmitModel());
    if (m_radioModel->tunerModel() != nullptr) {
        m_mirror->watch(QByteArray(kTunerKey), m_radioModel->tunerModel());
    }
    // R-R3-47 / R-R3-22 (remotePgxlControlVersion 1,
    // remoteRfKitControlVersion 1): the Core's amplifier and RF-Kit status.
    // Sent only to a peer at minor 11 (sendToSession).
    m_mirror->watch(QByteArray(kAmplifierKey), m_radioModel->amplifierModel());
    m_mirror->watch(QByteArray(kRfKitKey), m_radioModel->rfKitModel());
    // R-R3-48 (stationTciVersion 1): the Core's station TCI server.
    // Sent only to a peer at minor 11 on a Core that runs one.
    m_mirror->watch(QByteArray(kStationTciKey), m_radioModel->stationTciModel());
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
}

bool StationServer::sendCapabilitiesAndSettingsSnapshot(SessionTransport* transport,
                                                         quint64 expectedEpoch)
{
    // Do not let a queued radio callback target a disconnected, preempted,
    // or merely handshaking peer.  promoteToSession() intentionally calls
    // this while snapshotComplete is still false, so authentication is the
    // boundary here rather than readiness of the full mirror snapshot.
    const auto stillOwnsSession = [this, transport, expectedEpoch]() {
        if (transport == nullptr || transport != m_session
            || expectedEpoch != m_mediaSessionEpoch) {
            return false;
        }
        const auto peer = m_peers.constFind(transport);
        return peer != m_peers.cend() && peer->authenticated;
    };
    if (!stillOwnsSession()) {
        return false;
    }

    // Capability exchange (section 7.0 step 4).  On the initial path this
    // remains before every model message; on the late-radio path it updates
    // only identity, board, effective limits, and connection state.
    send(transport, SessionMessages::capabilities(buildCapabilities().toUpdates()));
    if (!stillOwnsSession()) {
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
    return stillOwnsSession();
}

// ── Inbound state and settings ───────────────────────────────────────────

void StationServer::handlePropertyWrite(SessionTransport* transport,
                                        const SessionMessage& message)
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
    }
    // R-R3-47: the RF-Kit switch is the Core's, changed by setRfKitEnabled.
    const bool radioWrite = message.objectKey == QByteArray(kRadioKey);
    const bool receiveOnlyTransmitWrite = message.objectKey == QByteArray(kTransmitKey)
        && !m_radioModel.isNull() && m_radioModel->receiveOnlyStationPolicy();
    // R-R3-25: the tuner's operate, bypass and antenna, and the amplifier's
    // operate, on a receive-only Core.
    const bool receiveOnlyStation = !m_radioModel.isNull()
        && m_radioModel->receiveOnlyStationPolicy();
    const bool tunerWrite = message.objectKey == QByteArray(kTunerKey);
    const bool amplifierWrite = message.objectKey == QByteArray(kAmplifierKey);
    // R-IOS-01: the class MirrorPolicy's direction table is keyed by.
    QByteArray outboundClass;
    if (const QObject* target = m_mirror->watchedObject(message.objectKey)) {
        outboundClass = MirrorSchema::shortClassName(target->metaObject()->className());
    }
    QSet<QByteArray> requested;
    for (const MirrorUpdate& update : message.updates) {
        if (requested.contains(update.name)) {
            refusals.insert(update.name, QStringLiteral("Duplicate property in one write."));
            continue;
        }
        requested.insert(update.name);
        const auto known = previous.constFind(update.name);
        if (known == previous.cend() || known->kind != update.kind) {
            refusals.insert(update.name, QStringLiteral("Unknown property or incompatible value type."));
            continue;
        }
        if (receiveOnlyTransmitWrite) {
            refusals.insert(update.name, QString::fromLatin1(kReceiveOnlyTransmitReason));
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
        if (radioWrite && update.name == "rfKitEnabled") {
            refusals.insert(update.name, QString::fromLatin1(kRfKitSwitchWriteReason));
            continue;
        }
        if (!negotiated && (message.objectKey == "pureSignalSettings"
            || update.name.startsWith("nnr")
            || (update.name == "activeNr" && update.value.toInt() == static_cast<int>(NrSlot::NNR)))) {
            refusals.insert(update.name, QStringLiteral("This client has not negotiated DSP settings control."));
            continue;
        }
        if (!outboundClass.isEmpty()
            && !MirrorPolicy::inboundAllowed(outboundClass, update.name)) {
            refusals.insert(update.name, QString::fromLatin1(kOutboundWriteReason));
            continue;
        }
        const MirrorApplyResult result = m_mirror->applyInbound(message.objectKey, update.name, update.value);
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
        if (result.reason.isEmpty() && (!result.hasValue || result.value.value != update.value)) {
            // R-R3-46: the attenuator says in plain words why it kept
            // another value (its range, what this radio offers).
            if (stepAttWrite && !m_radioModel.isNull()) {
                result.reason = m_radioModel->stepAttFacade()->settleReason(update.name);
            } else if (alexWrite && !m_radioModel.isNull()) {
                result.reason = m_radioModel->alexAntennaFacade()->settleReason(update.name);
            }
            if (result.reason.isEmpty()) {
                result.reason = QStringLiteral("The station retained the returned value after validating this edit.");
            }
        }
        result.accepted = result.reason.isEmpty();
        results.append(result);
    }
    if (negotiated && message.writeId != 0) {
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
    if (isReceiveOnlyRefusedKey(key) && !m_radioModel.isNull()
        && m_radioModel->receiveOnlyStationPolicy()) {
        const QString reason = QString::fromLatin1(kReceiveOnlyTransmitReason);
        const QVariant restored = m_settings.value(key);
        qCWarning(lcStation) << "Refused remote settings write" << key << ":" << reason;
        send(transport, SessionMessages::settingsReject(key, restored.isValid(),
                                                        restored.toString(), reason));
        return;
    }
    const SettingsApplyResult result =
        m_settingsServer->applyInboundWrite(key, message.updates.first().value,
                                            message.originTag);
    if (!result.accepted) {
        qCWarning(lcStation) << "Refused remote settings write" << key << ":"
                             << result.reason;
        send(transport,
             SessionMessages::settingsReject(key, result.restoredValue.isValid(),
                                             result.restoredValue.toString(), result.reason));
        return;
    }
    // R-R3-21: a DSP > Options RX setting takes effect now, not at the next
    // mode change. R-R3-46: a Hardware Config setting reaches the Core's
    // own controllers (the radio, and their later saves) now too. RadioModel
    // ignores every other key.
    if (!m_radioModel.isNull()) {
        m_radioModel->scheduleRemoteDspOptionsApply(key);
        m_radioModel->scheduleRemoteHardwareApply(key);
    }
}

void StationServer::handleSettingsRemove(const SessionMessage& message)
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
            || isModelOwnedAlexAntennaSettingsKey(key);
        sendToSession(SessionMessages::settingsReject(key, value.isValid(), value.toString(),
            plainReason ? modelOwnedSettingsRefusal(key)
                    : QStringLiteral("Use the validated DSP controls to change these settings.")));
        return;
    }
    // A remove would reset a DSP > Options TX setting to its default, so a
    // receive-only Core refuses it exactly as it refuses a write to the same
    // key (handleSettingsWrite above) and hands back its own value (R-R3-21).
    // R-R3-46: the same for a transmit-side hardware key.
    if (isReceiveOnlyRefusedKey(key) && !m_radioModel.isNull()
        && m_radioModel->receiveOnlyStationPolicy()) {
        const QString reason = QString::fromLatin1(kReceiveOnlyTransmitReason);
        const QVariant restored = m_settings.value(key);
        qCWarning(lcStation) << "Refused remote settings remove" << key << ":" << reason;
        sendToSession(SessionMessages::settingsReject(key, restored.isValid(),
                                                      restored.toString(), reason));
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
        sendToSession(SessionMessages::settingsReject(key, value.isValid(), value.toString(),
                                                      refusal));
        return;
    }
    m_settings.remove(key);
    // R-R3-21: removing a DSP > Options RX setting returns it to its
    // default, which takes effect now as a write does. R-R3-46: so does a
    // Hardware Config setting.
    if (!m_radioModel.isNull()) {
        m_radioModel->scheduleRemoteDspOptionsApply(key);
        m_radioModel->scheduleRemoteHardwareApply(key);
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

void StationServer::sendToSession(const SessionMessage& message)
{
    if (m_session == nullptr) {
        return;
    }
    switch (message.kind) {
    case SessionMessageKind::Schema:
    case SessionMessageKind::ObjectCreate:
    case SessionMessageKind::Delta: {
        const auto peer = m_peers.constFind(m_session);
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
        if (!needsNnrFit(message, minor)) {
            // Most messages carry no NNR field: send them as they are.
            if (worthSendingAfterNnrFit(message)) {
                m_session->sendText(SessionMessages::encode(message));
            }
            return;
        }
        SessionMessage fitted = message;
        if (fitNnrLimitToPeer(fitted, minor)) {
            m_session->sendText(SessionMessages::encode(fitted));
        }
        return;
    }
    default:
        m_session->sendText(SessionMessages::encode(message));
        return;
    }
}

void StationServer::setMediaEnabled(bool enabled)
{
    // A live session negotiated its capability already. Do not advertise a
    // different contract midway through it.
    if (!m_session) {
        m_mediaEnabled = enabled;
    }
}

void StationServer::setTelemetryEnabled(bool enabled)
{
    if (!m_session) { m_telemetryEnabled = enabled; }
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
    const QPointer<StationServer> self(this);
    emit displayBudgetChanged(); // The sender sees new limits before publication.
    // A peer the computed budget does not reach heard of no budget, so a
    // change to it is nothing to that peer (legacy mode exactly).
    if (self && (!self->m_displayBudgetForReasonPeersOnly || self->displayBudgetLimits())) {
        self->publishDisplayBudgetCapabilities();
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
    m_dispatcher->setPs3DisplayAdmissionHandler(std::move(handler));
}

void StationServer::publishDisplayBudgetCapabilities()
{
    if (mediaAvailable()) {
        sendToSession(SessionMessages::capabilities(buildCapabilities().toUpdates()));
    }
}

bool StationServer::telemetryAvailable() const
{
    const auto it = m_peers.constFind(m_session);
    return m_telemetryEnabled && it != m_peers.cend() && it->authenticated
        && it->snapshotComplete && it->agreedMinor >= kStationTelemetrySessionProtocolMinor;
}

bool StationServer::sendTelemetry(const StationTelemetrySnapshot& snapshot,
                                  quint64 expectedEpoch)
{
    if (!telemetryAvailable() || expectedEpoch != m_mediaSessionEpoch) { return false; }
    SessionMessage message;
    message.kind = SessionMessageKind::StationTelemetry;
    message.telemetry = snapshot;
    // A peer from before host telemetry receives exactly the radio and audio
    // sections it was built for.
    const auto peer = m_peers.constFind(m_session);
    if (peer == m_peers.cend() || peer->agreedMinor < kCoreHostTelemetrySessionProtocolMinor) {
        message.telemetry.host = {};
    }
    // Likewise a peer from before receiver load receives exactly the
    // sections it negotiated, without "receivers".
    if (peer == m_peers.cend() || peer->agreedMinor < kReceiverLoadSessionProtocolMinor) {
        message.telemetry.receivers.reset();
    }
    const QByteArray wire = SessionMessages::encode(message);
    if (wire.isEmpty()) { return false; }
    m_session->sendText(wire);
    return true;
}

bool StationServer::mediaAvailable() const
{
    const auto it = m_peers.constFind(m_session);
    return m_mediaEnabled && it != m_peers.cend() && it->authenticated
        && it->snapshotComplete && it->agreedMinor >= kMediaSessionProtocolMinor;
}

bool StationServer::remoteWidebandAvailable() const
{
    const auto it = m_peers.constFind(m_session);
    return mediaAvailable() && it != m_peers.cend()
        && it->agreedMinor >= kRemoteWidebandSessionProtocolMinor;
}

bool StationServer::remoteAudioStatusAvailable() const
{
    const auto it = m_peers.constFind(m_session);
    return mediaAvailable() && it != m_peers.cend()
        && it->agreedMinor >= kRemoteAudioStatusSessionProtocolMinor;
}

bool StationServer::spectrumGrantAvailable() const
{
    const auto it = m_peers.constFind(m_session);
    return mediaAvailable() && it != m_peers.cend()
        && it->agreedMinor >= kRemoteSpectrumGrantSessionProtocolMinor;
}

void StationServer::setDisplayBudgetForReasonPeersOnly(bool reasonPeersOnly)
{
    m_displayBudgetForReasonPeersOnly = reasonPeersOnly;
}

std::optional<DisplayBudgetLimits> StationServer::displayBudgetLimits() const
{
    if (m_displayBudget && m_displayBudgetForReasonPeersOnly) {
        // R-R3-08/37: a computed ceiling is only for an app that can be told
        // why it is lowered. An older app keeps legacy mode exactly: no
        // budget, no pacing, no allocation results.
        const auto peer = m_peers.constFind(m_session);
        if (peer == m_peers.cend()
            || peer->agreedMinor < kDisplayBudgetReasonSessionProtocolMinor) {
            return std::nullopt;
        }
    }
    return m_displayBudget;
}

bool StationServer::displayBudgetAvailable() const
{
    const auto it = m_peers.constFind(m_session);
    return mediaAvailable() && m_displayBudgetEnforcementEnabled && displayBudgetLimits()
        && it != m_peers.cend() && it->agreedMinor >= kRemoteDisplayBudgetSessionProtocolMinor;
}

bool StationServer::sendMediaControl(const QJsonObject& payload, quint64 expectedEpoch)
{
    if (!mediaAvailable() || expectedEpoch != m_mediaSessionEpoch) {
        return false;
    }
    SessionMessage message;
    message.kind = SessionMessageKind::MediaControl;
    message.mediaPayload = payload;
    const QByteArray wire = SessionMessages::encode(message);
    if (wire.isEmpty()) {
        return false;
    }
    m_session->sendText(wire);
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
    return accessoryStatusVersion() >= 1 ? 2 : 0;
}

int StationServer::rfKitControlVersion() const
{
    return accessoryStatusVersion() >= 1 ? 2 : 0;
}

int StationServer::stationTciVersion() const
{
    return !m_radioModel.isNull() && m_radioModel->stationTciController() != nullptr ? 1 : 0;
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
    // (setAlexRxAntenna), R-R3-46 fix wave.
    return m_radioModel->ioBoardFacade()->isBound() ? 3 : 2;
}

StationCapabilities StationServer::buildCapabilities() const
{
    StationCapabilities caps;
    caps.settingsSchemaVersion = settingsSchemaVersionOf(m_settings);
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
        const auto peer = m_peers.constFind(m_session);
        if (peer != m_peers.cend()
            && peer->agreedMinor >= kRadioIdentitySessionProtocolMinor) {
            caps.radioIdentityEntries = true;
            // R-R3-46 / R-R3-11: 1 once the Core's controller is behind the
            // `stepAtt` object (DaemonApp binds it before the server starts);
            // 2 once its Alex antennas are behind `alexAntennas` too, with
            // the hardware apply step and the I/O board probe; 3 with the
            // read-only `ioBoard` object and the per-band antenna verb.
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
    caps.userDdcCount = board.userDdcCount;
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
    caps.remoteMediaVersion = m_mediaEnabled ? 1 : 0;
    caps.remoteWidebandDisplayVersion = m_mediaEnabled ? 1 : 0;
    caps.remoteAudioStatusVersion = m_mediaEnabled ? 1 : 0;
    caps.spectrumGrantVersion = m_mediaEnabled ? 1 : 0;
    // R-R3-23: lossless audio beside Opus. Advertised with media whatever
    // nereusd.conf audio_lossless says, so a GUI can be told plainly when
    // the Core's own setting refuses it.
    caps.audioProfileVersion = m_mediaEnabled ? 1 : 0;
    // R-R3-35: the Core answers audio clock probes whenever media is on.
    caps.audioClockVersion = m_mediaEnabled ? 1 : 0;
    // R-R3-43: a receiver's own audio on its own stream, whenever media is on.
    caps.receiverAudioVersion = m_mediaEnabled ? 1 : 0;
    // R-R3-45: the headphones mix on its own stream, whenever media is on.
    caps.headphonesMixVersion = m_mediaEnabled ? 1 : 0;
    const std::optional<DisplayBudgetLimits> budget = displayBudgetLimits();
    if (m_mediaEnabled && m_displayBudgetEnforcementEnabled && budget) {
        caps.remoteDisplayBudgetVersion = 1;
        caps.displayBudget = budget;
        caps.remotePs3DisplaySubscribed = m_radioModel->pureSignalFacade()->remoteAmpViewSubscribed();
        // R-R3-08/37: only a peer that negotiated the reason receives it; an
        // older one gets exactly the five budget fields it was built for.
        const auto peer = m_peers.constFind(m_session);
        if (peer != m_peers.cend()
            && peer->agreedMinor >= kDisplayBudgetReasonSessionProtocolMinor) {
            caps.displayBudgetReason = m_displayBudgetReason;
        }
    }
    caps.remoteCtunVersion = 1;
    caps.stationTelemetryVersion = m_telemetryEnabled ? 3 : 0;
    caps.remoteTgxlConfigVersion = m_radioModel->stationAccessoryIdentityEnabled() ? 1 : 0;
    caps.remoteFourO3AControlVersion = m_radioModel->stationAccessoryIdentityEnabled() ? 1 : 0;
    caps.propertyResultVersion = 1;
    // R-R3-21 / R-R3-09: the Core owns the notch list (the `notches` object
    // and the notch.* commands). Independent of WDSP: the list lives on
    // NotchModel whether or not channels exist.
    caps.notchControlVersion = 1;
#ifdef HAVE_WDSP
    caps.wdspVersion = 210;
    caps.wdspCompatibilityVersion = 1;
    caps.nnrVersion = 1;
    caps.psAlgorithmVersion = 3;
    // 2 (R-R3-21): NR3 models are Core assets (kind 2, selectNr3Model).
    caps.dspAssetVersion = 2;
    caps.psDisplayVersion = m_mediaEnabled ? 1 : 0;
#endif

    return caps;
}

} // namespace NereusSDR
