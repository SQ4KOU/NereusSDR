#pragma once
// =================================================================
// src/core/session/SessionMessages.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R2 Task 10.
//
// The wire-message shapes StateMirror::attachSession() (StateMirror.h)
// sends, and a live session (Task 18) will send and receive over the wss
// control channel. Seven kinds exist so far:
//
//   Schema            -- one mirrored class's property table (ordinal,
//                         name, wire kind; no live value), sent once per
//                         class per session, before anything referring to
//                         an instance of it.
//   ObjectCreate       -- one object appeared. Carries the FULL settled
//                         property bag (MirrorSchema.h's MirrorUpdate,
//                         which DOES carry a value).
//   ObjectDestroy      -- one object vanished. No properties: a client
//                         that already has the object needs nothing more
//                         than the key to drop it.
//   Delta              -- a SUBSET of one object's properties, changed
//                         since the last message about it.
//   SnapshotComplete   -- no fields at all. Marks the end of the
//                         connect-time burst; StateMirror::attachSession()
//                         guarantees nothing after this marker was already
//                         implied by something before it, and nothing
//                         before it is missing.
//   CommandInvoke      -- Task 11. A client asking the daemon to run one of
//                         the RadioModel entry points that is not a plain
//                         property write: create a slice, remove one,
//                         re-tune a DDC's rate. `commandVerb` names which;
//                         `arguments` carries its named, typed parameters
//                         (MirrorUpdate reused as a generic {name, kind,
//                         value} triple -- `ordinal` is meaningless here
//                         and always 0). `commandId` is caller-assigned and
//                         echoed verbatim on the matching CommandResult, so
//                         a caller with more than one command in flight can
//                         still tell results apart.
//   CommandResult      -- Task 11. What a CommandInvoke actually did.
//                         `accepted` / `reason` mirror MirrorApplyResult's
//                         own shape. `affectedKeys` names the object(s)
//                         this result is about; its exact meaning is per-
//                         verb, not a blanket "everything that changed"
//                         guarantee -- see SessionMessage::affectedKeys
//                         below for the precise contract and the one verb
//                         (requestSliceSampleRate) where it really is a
//                         full before/after diff.
//
// Task 18 appends eleven more, in three groups, to the SAME table rather
// than starting a second one:
//
//   Hello / AuthRequest / AuthResult / Capabilities / SessionEnd
//                      -- the parent design's section 7.0 connect
//                         sequence, in order: "TLS establish -> protocol
//                         hello carrying a semantic version from both
//                         ends -> authentication -> capability exchange
//                         -> state snapshot -> snapshot-complete marker".
//   PropertyWrite       -- the INBOUND half of the property mirror: a
//                         client asking the daemon to write mirrored
//                         properties (StateMirror::applyInbound). Tasks 7
//                         and 8 built both halves of that apply; until now
//                         nothing named it on the wire.
//   SettingsSnapshot / SettingsWrite / SettingsRemove / SettingsValue /
//   SettingsReject     -- SettingsProxy (client) and SettingsProxyServer
//                         (daemon) talking to each other. Deliberately
//                         separate kinds from the property mirror above:
//                         settings are a flat QString-valued key space
//                         with no object identity and no schema, so
//                         routing them through Delta would mean
//                         overloading `objectKey` with something that is
//                         not an object.
//
// Encodes as JSON text, per the R2 design addendum section 6: "R2 encodes
// as JSON text on the reliable control channel ... because the ordinal
// dictionary [MirrorSchema's dense per-class ordinals] exists from day
// one, R3 swaps the codec without touching the object model." This is a
// deliberate, documented staged simplification, not an oversight: section
// 7.2 of the parent design calls for a compact native envelope, and R2
// does not build it.
//
// JSON numbers are IEEE-754 doubles with no distinct 64-bit integer type,
// so MirrorWireKind::Int64 and ::Enum values are carried as JSON numbers,
// exact up to 2^53. Every currently mirrored integral property
// (filterLow/filterHigh in Hz, sliceIndex, chainIndex, wire ordinals, and
// so on) sits comfortably inside that range -- see MirrorSchema.h's
// section 6 note ("Type surface is small") for the full inventory. A
// future property that genuinely needs the missing bits is an R3 codec
// problem, not a reason to complicate this one early.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-05  J.J. Boyd / KG4VCF  Remote daemon R2 Task 10: session
//                                    message shapes and JSON codec. AI-
//                                    assisted transformation via Anthropic
//                                    Claude Code.
//   2026-08-05  J.J. Boyd / KG4VCF  Remote daemon R2 Task 11: CommandInvoke
//                                    / CommandResult message shapes and
//                                    codec. AI-assisted transformation via
//                                    Anthropic Claude Code.
//   2026-08-08  J.J. Boyd / KG4VCF  Remote daemon R2 Task 18: the section
//                                    7.0 handshake kinds (Hello /
//                                    AuthRequest / AuthResult /
//                                    Capabilities / SessionEnd), the
//                                    inbound PropertyWrite, and the five
//                                    settings kinds. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
//   2026-08-09  J.J. Boyd / KG4VCF  Whole-branch review, Important 4:
//                                    settingsValueAbsent(). AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
// =================================================================

#include <QByteArray>
#include <QList>
#include <QMetaType>
#include <QJsonObject>

#include "core/session/MirrorSchema.h"
#include "core/session/StationTelemetry.h"

namespace NereusSDR {

/// Which kind of session message a SessionMessage carries. Task 11 appends
/// CommandInvoke / CommandResult here; the codec lives in this file, the
/// dispatch onto RadioModel lives in SessionCommandDispatcher.
enum class SessionMessageKind {
    Schema,
    ObjectCreate,
    ObjectDestroy,
    Delta,
    SnapshotComplete,
    CommandInvoke,
    CommandResult,

    // ── Task 18: the section 7.0 connect sequence ───────────────────────
    Hello,
    AuthRequest,
    AuthResult,
    Capabilities,
    SessionEnd,

    // ── Task 18: the inbound half of the property mirror ────────────────
    PropertyWrite,

    // ── Task 18: SettingsProxy / SettingsProxyServer over the wire ──────
    SettingsSnapshot,
    SettingsWrite,
    SettingsRemove,
    SettingsValue,
    SettingsReject,
    // R3: bounded control/signalling for the separate encrypted media peer.
    MediaControl,
    // R3: bounded, capability-gated station observations. Never a command.
    StationTelemetry,
    PropertyResult,
};

/// The session protocol's own semantic version, advertised by BOTH ends in
/// the Hello message and governed by the parent design's section 7.0
/// version policy: "refuse on major mismatch, negotiate down on minor".
///
/// Major is incremented ONLY for a breaking change to framing, identity, or
/// the snapshot contract (section 7.0's own wording). Everything else is a
/// minor bump plus a capability entry, because a desktop GUI several
/// releases ahead of a Pi still running this one is the EXPECTED case, not
/// an error case, and it has to degrade rather than refuse.
///
/// R2 ships 1.0. It is deliberately not 0.x: the JSON control encoding is a
/// documented staged simplification (see this file's header), but the
/// message SHAPES -- object identity, the snapshot contract, the schema
/// table -- are the thing this number governs, and those are not
/// provisional. R3 swapping the codec under the same object model is a
/// minor bump at most.
inline constexpr quint16 kSessionProtocolMajor = 1;
inline constexpr quint16 kSessionProtocolMinor = 11;
inline constexpr quint16 kMediaSessionProtocolMinor = 1;
inline constexpr quint16 kRemoteCtunSessionProtocolMinor = 2;
inline constexpr quint16 kStationTelemetrySessionProtocolMinor = 3;
inline constexpr quint16 kRemoteTgxlConfigSessionProtocolMinor = 4;
// Both R3 accessory controls belong to minor 4. Separate capabilities let
// intermediate development builds expose TGXL configuration before the
// master/listener controls are available.
inline constexpr quint16 kRemoteFourO3AControlSessionProtocolMinor = 4;
inline constexpr quint16 kDspControlSessionProtocolMinor = 5;
inline constexpr quint16 kRemoteWidebandSessionProtocolMinor = 6;
inline constexpr quint16 kRemoteDisplayBudgetSessionProtocolMinor = 7;
// The audio context carries the accepted encoder profile when audio is on and
// the reason it is off otherwise. Minor-7 peers keep the eight-key context.
inline constexpr quint16 kRemoteAudioStatusSessionProtocolMinor = 8;
// The spectrum context reports what Core granted the endpoint: FFT size and
// tier, requested and granted points, and what limited them. Minor-8 peers
// keep the 19-key (20 with wideband) context.
inline constexpr quint16 kRemoteSpectrumGrantSessionProtocolMinor = 9;
// Station telemetry carries the Core computer's CPU, memory and temperature
// in an optional host section (stationTelemetryVersion 2). Minor-9 peers
// receive exactly the radio and audio sections.
inline constexpr quint16 kCoreHostTelemetrySessionProtocolMinor = 10;
// Station telemetry carries each receiver's processing load, input wait and
// skipped input in an optional receivers section (stationTelemetryVersion 3).
// Minor-10 peers receive exactly the radio, audio and host sections.
inline constexpr quint16 kReceiverLoadSessionProtocolMinor = 11;
// A receiver that cannot keep up with neural noise reduction is stepped back
// at runtime: SliceModel's nnrLimit property and the nnr.tryAgain command.
// Same unreleased step as receiver load. Minor-10 peers never see the
// property and cannot send the command.
inline constexpr quint16 kNnrLimitSessionProtocolMinor = 11;
// The capability descriptor names why the display budget is below the
// Core's ceiling (displayBudgetReason: the Core computer is busy). Same
// unreleased step. Minor-10 peers receive exactly the five budget fields.
inline constexpr quint16 kDisplayBudgetReasonSessionProtocolMinor = 11;
// R-R3-46: the capability descriptor names the Core's radio model
// (hpsdrModel), how it talks to the radio (radioProtocol) and the radio's
// LAN address (radioAddress). Same unreleased step. Minor-10 peers receive
// exactly the descriptor they were built for.
inline constexpr quint16 kRadioIdentitySessionProtocolMinor = 11;
inline constexpr qsizetype kMaxMediaControlBytes = 128 * 1024;

// R-R3-16/17: how long either end waits for the connect sequence to finish
// once a link exists. The GUI runs it from attaching a transport until the
// snapshot-complete marker (StationClient::setHandshakeDeadlineMs); Core runs
// it from accepting a peer until that peer's snapshot has been sent
// (StationServer::setAuthDeadlineMs). One value on purpose: a Core whose event
// loop stalls mid-connect must be abandoned by the GUI in the same bound the
// Core uses to clear a peer that stalls, so neither end holds a half-made
// session the other has already given up on for long.
inline constexpr int kStationHandshakeDeadlineMs = 30000;
inline constexpr qsizetype kMaxStationTelemetryBytes = 16 * 1024;

/// One property's WIRE DECLARATION: name, ordinal and kind, carrying no
/// live value. This is what a Schema message announces once per class per
/// session. MirrorUpdate (MirrorSchema.h) is the analogous VALUE-carrying
/// shape ObjectCreate and Delta use.
struct SessionSchemaField {
    quint16 ordinal = 0;
    QByteArray name;
    MirrorWireKind kind = MirrorWireKind::Unsupported;
};

/// One message on the session's reliable control channel (R2 design
/// addendum section 7.3's "Control" envelope: state deltas, lifecycle
/// events, snapshot, commands). A flat, kind-tagged struct rather than a
/// class hierarchy, matching StateMirror.h's MirrorApplyResult: which
/// fields are meaningful depends on `kind`, and callers are expected to
/// go through the builder functions below rather than populate fields by
/// hand.
///
///   Schema            -- className, fields
///   ObjectCreate       -- objectKey, className, updates (the FULL bag)
///   ObjectDestroy       -- objectKey, className
///   Delta               -- objectKey, updates (the CHANGED subset)
///   SnapshotComplete    -- nothing but kind
///   CommandInvoke       -- commandVerb, commandId, arguments
///   CommandResult       -- commandVerb, commandId, accepted, reason,
///                          affectedKeys
struct SessionPropertyResult {
    QByteArray property;
    bool accepted = false;
    QString reason;
    bool hasValue = false;
    MirrorUpdate value;
};

struct SessionMessage {
    SessionMessageKind kind = SessionMessageKind::Delta;

    StationTelemetrySnapshot telemetry;

    QByteArray className;
    QList<SessionSchemaField> fields;
    QByteArray objectKey;
    QList<MirrorUpdate> updates;
    quint32 writeId = 0;
    QList<SessionPropertyResult> propertyResults;

    // ── Task 11: CommandInvoke / CommandResult only ─────────────────────

    /// Which RadioModel entry point (CommandInvoke), or which one this is
    /// the answer to (CommandResult). One of SessionCommandDispatcher's
    /// four known verb names; an unrecognised one is a dispatch-time
    /// rejection, not a decode-time one -- see that class's doc comment.
    QByteArray commandVerb;

    /// Caller-assigned, echoed back verbatim on the CommandResult. Lets a
    /// caller with more than one command in flight -- guaranteed for
    /// requestSliceSampleRate, which SessionCommandDispatcher defers to a
    /// LATER event-loop turn -- match a result to the invoke that produced
    /// it.
    quint32 commandId = 0;

    /// CommandInvoke only: named, typed parameters. Reuses MirrorUpdate as
    /// a generic {name, kind, value} triple rather than inventing a
    /// parallel encode/decode path -- `ordinal` carries no meaning here and
    /// is always 0. Per-verb argument contracts (SessionCommandDispatcher.h):
    ///   addSlice               -- {"initialPanId": Utf8}
    ///   removeSlice            -- {"sliceId": Int64}
    ///   requestSliceSampleRate -- {"sliceId": Int64, "rateHz": Int64}
    ///   addSliceOnPan          -- {"panId": Utf8}
    ///   requestStreamCtunPinned -- {"sliceId": Int64, "pinned": Bool}
    ///   configureTgxl          -- {"host": Utf8, "port": Int64}
    ///   disconnectTgxl         -- {}
    ///   configurePgxl          -- {"host": Utf8, "port": Int64}
    ///   disconnectPgxl         -- {}
    ///   setPgxlConnectionSettings -- {"autoReconnect": Bool,
    ///                               "keepaliveSec": Int64, "pingSec": Int64}
    ///   configureRfKit         -- {"host": Utf8, "port": Int64}
    ///   disconnectRfKit        -- {}
    ///   setRfKitEnabled        -- {"enabled": Bool}
    ///   setStationTci          -- {"enabled": Bool, "port": Int64}
    ///   requestStreamCentre     -- {"sliceId": Int64, "centreHz": Double}
    QList<MirrorUpdate> arguments;

    /// CommandResult only. True iff the command ran; false leaves every
    /// bit of state exactly as it was (same contract as
    /// MirrorApplyResult::accepted).
    bool accepted = false;

    /// CommandResult only. Empty iff accepted.
    QString reason;

    /// AuthResult and SessionEnd only: is the condition that produced this
    /// refusal TRANSIENT, so a client may sensibly back off and try again?
    ///
    /// A free-text `reason` is for the operator; this is for the client's
    /// retry policy, which must not be built on matching English prose.
    /// The distinction is load-bearing in both directions. "Station is at
    /// its concurrent-connection limit" and "too many failed
    /// authentication attempts" both clear on their own, and treating them
    /// as permanent is what let a bad actor lock an operator out with no
    /// automatic recovery. A wrong token and a protocol-major mismatch
    /// never clear, and retrying either forever is how the rate limiter
    /// gets fed.
    ///
    /// Defaults to FALSE, and decode() reads it leniently rather than
    /// requiring it: a peer built before this field existed sends no
    /// "retryable" key, and absent must mean "assume permanent", which is
    /// both the safe direction and exactly the behaviour that peer's own
    /// client half had.
    bool retryable = false;

    /// CommandResult only: which object(s) this result is about. The exact
    /// meaning is PER-VERB, not a blanket mutation-diff guarantee:
    ///
    ///   - requestSliceSampleRate: a full before/after diff -- EVERY slice
    ///     whose sampleRateHz actually changed, which is not always the
    ///     one slice the invoke named (co-hosted slices on the same DDC
    ///     stream, or a Protocol 1 board's radio-wide RadioModel::
    ///     setSampleRateLive escalation -- SessionCommandDispatcher::
    ///     handleRequestSliceSampleRate, SessionCommandDispatcher.cpp).
    ///   - addSlice / addSliceOnPan: the one newly-created slice.
    ///   - removeSlice: the one removed slice. NOT a mutation diff: removal
    ///     can leave RadioModel::requestDdcAssignment() moving a SURVIVING
    ///     slice's ddcIndex/streamIndex, which this list does not name.
    ///     Nothing is lost -- that survivor's change still reaches the
    ///     client normally, as an ordinary Delta through StateMirror's
    ///     existing outbound path -- this field is the command's own
    ///     object-level outcome, not a substitute for that path.
    ///   - setActiveSliceById: the newly-active slice, plus the previously-
    ///     active one when it differs.
    QList<QByteArray> affectedKeys;

    // ── Task 18: Hello only ─────────────────────────────────────────────

    /// The sender's own kSessionProtocolMajor / kSessionProtocolMinor.
    /// Both ends send these; see kSessionProtocolMajor's doc comment for
    /// the policy the receiver applies to them.
    quint16 protocolMajor = 0;
    quint16 protocolMinor = 0;

    /// The sender's AppSettings schema version (AppSettings::
    /// currentSchemaVersion()). Parent design section 7.0 lists it among
    /// the capability descriptor's contents but assigns the comparison to
    /// nothing; StationClient does it at handshake time. Carried on Hello
    /// rather than only in the Capabilities descriptor so BOTH ends learn
    /// it, and so it is available before authentication decides whether a
    /// descriptor is ever sent at all.
    qint32 settingsSchemaVersion = 0;

    /// Free-form identification of the sending process ("nereusd",
    /// "NereusSDR 0.5.2"). Diagnostics only: nothing gates on it.
    QString peerName;

    // ── Task 18: AuthRequest only ───────────────────────────────────────

    /// The pre-shared token (TokenStore). NEVER logged: StationServer logs
    /// the OUTCOME of a verify, never the candidate.
    QString token;

    // ── Task 18: SettingsWrite / SettingsValue only ─────────────────────

    /// SettingsProxy::localOriginTag() -- a per-SESSION identifier, not a
    /// per-write sequence number. The daemon echoes it back verbatim on the
    /// resulting SettingsValue broadcast so a client can tell its own echo
    /// from a third party's change. See SettingsProxy.h's origin-tag
    /// paragraph.
    QString originTag;

    /// R3 MediaControl only. Individual media operations validate their own
    /// fields after the authenticated, snapshot-ready session gate. This
    /// envelope carries signalling/subscriptions, never audio or FFT arrays.
    QJsonObject mediaPayload;
};

/// Builders plus the JSON codec. A static-method utility class with no
/// per-instance state, matching MirrorSchema's own shape.
class SessionMessages {
public:
    static SessionMessage schema(const QByteArray& className,
                                 const QList<SessionSchemaField>& fields);
    static SessionMessage objectCreate(const QByteArray& objectKey,
                                       const QByteArray& className,
                                       const QList<MirrorUpdate>& fullBag);
    static SessionMessage objectDestroy(const QByteArray& objectKey,
                                        const QByteArray& className);
    static SessionMessage delta(const QByteArray& objectKey,
                                const QList<MirrorUpdate>& changed);
    static SessionMessage snapshotComplete();

    /// Task 11. `verb` is one of SessionCommandDispatcher's known verb
    /// names (see SessionMessage::commandVerb's doc comment for the
    /// per-verb argument contract); this builder does not validate it --
    /// an unrecognised verb decodes and encodes just fine, and is rejected
    /// at dispatch time instead, matching decode()'s existing division of
    /// labour between structural validity and semantic validity.
    static SessionMessage commandInvoke(const QByteArray& verb, quint32 commandId,
                                        const QList<MirrorUpdate>& arguments);

    /// `affectedKeys` is the ACTUAL scope, not the requested one -- see
    /// SessionMessage::affectedKeys.
    static SessionMessage commandResult(const QByteArray& verb, quint32 commandId,
                                        bool accepted, const QString& reason,
                                        const QList<QByteArray>& affectedKeys,
                                        const QList<MirrorUpdate>& values = {});

    // ── Task 18 builders ────────────────────────────────────────────────

    /// First message either end sends after the TLS handshake completes.
    static SessionMessage hello(quint16 major, quint16 minor,
                                qint32 settingsSchemaVersion,
                                const QString& peerName);

    /// Client to daemon, once the daemon's Hello has been accepted.
    static SessionMessage authRequest(const QString& token);

    /// Daemon to client. A false `accepted` is always followed by the
    /// daemon closing the socket; `reason` is what the operator sees and
    /// `retryable` is what the client's reconnect policy reads. Both are
    /// required rather than defaulted, so a refusal cannot be added
    /// without someone deciding which kind it is. See
    /// SessionMessage::retryable.
    static SessionMessage authResult(bool accepted, const QString& reason,
                                     bool retryable);

    /// Daemon to client, after a successful AuthResult. `descriptor` is
    /// StationCapabilities::toUpdates() -- MirrorUpdate reused as a generic
    /// {name, kind, value} triple, the same way CommandInvoke reuses it for
    /// arguments, rather than inventing a parallel encode path. `ordinal`
    /// carries no meaning here and is always 0.
    static SessionMessage capabilities(const QList<MirrorUpdate>& descriptor);

    /// Daemon to client: this session is over, and why. Sent for a version
    /// refusal, and for the incumbent session when a second authenticated
    /// connection preempts it (parent design section 7.1: "The displaced
    /// session is told why"). `retryable` is required for the same reason
    /// it is on authResult(); see SessionMessage::retryable.
    static SessionMessage sessionEnd(const QString& reason, bool retryable);

    /// Client to daemon: apply these property values to this object. The
    /// mirror-image of a Delta, deliberately a DISTINCT kind rather than a
    /// reused Delta so direction is explicit on the wire and a daemon can
    /// refuse a Delta outright -- a peer must never be able to tell a
    /// daemon what its own state IS, only what it should be CHANGED to,
    /// and those two readings of one message shape are exactly the
    /// ambiguity worth spending a kind name to avoid.
    static SessionMessage propertyWrite(const QByteArray& objectKey,
                                        const QList<MirrorUpdate>& updates,
                                        quint32 writeId = 0);
    static SessionMessage propertyResult(const QByteArray& objectKey,
                                         quint32 writeId,
                                         const QList<SessionPropertyResult>& results);

    /// Daemon to client: the connect-time settings snapshot
    /// (SettingsProxyServer::buildSnapshot()). Each entry's `name` is the
    /// AppSettings key and its `kind` is always Utf8, matching
    /// AppSettings's own flat QString-valued store.
    static SessionMessage settingsSnapshot(const QList<MirrorUpdate>& entries);

    /// Client to daemon: one Station-classified key changed here, please
    /// apply it there.
    static SessionMessage settingsWrite(const QString& key, const QString& value,
                                        const QString& originTag);

    /// Client to daemon: remove one Station-classified key.
    static SessionMessage settingsRemove(const QString& key);

    /// Daemon to client: this key's current value, broadcast to every
    /// connected client (SettingsProxyServer::outboundValueChanged).
    static SessionMessage settingsValue(const QString& key, const QString& value,
                                        const QString& originTag);

    /// Daemon to client: this key is GONE from the station's store
    /// (SettingsProxyServer::outboundValueRemoved), as distinct from
    /// holding an empty string.
    ///
    /// Whole-branch review, Important 4. The same SettingsValue kind with
    /// an EMPTY entry list, which is exactly how settingsReject() below
    /// already encodes "the daemon has nothing for this key either" --
    /// one absence convention on this wire, not two. A peer built before
    /// this existed decodes the frame fine (the codec requires the
    /// properties array to be present, and empty is present) and its
    /// handler ignores an entry-less settings.value, so it keeps its last
    /// known value rather than acquiring the empty-string ghost this
    /// replaces. Strictly better than the old behaviour, and not a
    /// version break.
    static SessionMessage settingsValueAbsent(const QString& key, const QString& originTag);

    /// Daemon to client: a SettingsWrite was refused. `hasRestoredValue`
    /// false means the daemon has nothing for this key either (proven
    /// unset, which SettingsProxy::applyRejection() distinguishes from a
    /// restored empty string) and is encoded as an EMPTY entry list rather
    /// than an entry carrying an empty value.
    static SessionMessage settingsReject(const QString& key, bool hasRestoredValue,
                                         const QString& restoredValue,
                                         const QString& reason = {});

    /// UTF-8 JSON text. See the file header for the Int64/Enum precision
    /// note.
    static QByteArray encode(const SessionMessage& message);

    /// False (leaving *out untouched) for malformed JSON, an unrecognised
    /// "type", or a value that will not decode against the wire kind it
    /// claims. Never throws and never asserts on untrusted input -- this
    /// is the far side of a socket a remote peer controls.
    static bool decode(const QByteArray& wire, SessionMessage* out);

    /// The wire token for each SessionMessageKind ("object.create", not
    /// "ObjectCreate") and the reverse lookup. Exposed so Task 11 can
    /// extend the same table rather than inventing a second one.
    static QByteArray kindName(SessionMessageKind kind);
    static bool kindFromName(const QByteArray& name, SessionMessageKind* out);

    static QByteArray wireKindName(MirrorWireKind kind);
    static bool wireKindFromName(const QByteArray& name, MirrorWireKind* out);
};

} // namespace NereusSDR

Q_DECLARE_METATYPE(NereusSDR::SessionMessageKind)
Q_DECLARE_METATYPE(NereusSDR::SessionSchemaField)
Q_DECLARE_METATYPE(NereusSDR::SessionMessage)
