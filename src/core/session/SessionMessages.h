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
//                         own shape. `affectedKeys` is the object keys
//                         ACTUALLY mutated, which is not always the same
//                         set the invoke named:
//                         SessionCommandDispatcher::handleRequestSliceSampleRate
//                         (SessionCommandDispatcher.cpp) can retune every
//                         slice on the radio for a request naming one, so
//                         the result reports what happened, not what was
//                         asked for.
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
// =================================================================

#include <QByteArray>
#include <QList>
#include <QMetaType>

#include "core/session/MirrorSchema.h"

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
};

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
struct SessionMessage {
    SessionMessageKind kind = SessionMessageKind::Delta;

    QByteArray className;
    QList<SessionSchemaField> fields;
    QByteArray objectKey;
    QList<MirrorUpdate> updates;

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
    QList<MirrorUpdate> arguments;

    /// CommandResult only. True iff the command ran; false leaves every
    /// bit of state exactly as it was (same contract as
    /// MirrorApplyResult::accepted).
    bool accepted = false;

    /// CommandResult only. Empty iff accepted.
    QString reason;

    /// CommandResult only: the object keys ACTUALLY mutated, which is not
    /// always what the invoke named as its target -- see the CommandResult
    /// case in this file's header comment for why requestSliceSampleRate
    /// specifically can name more keys than it was asked to touch.
    QList<QByteArray> affectedKeys;
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
                                        const QList<QByteArray>& affectedKeys);

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
