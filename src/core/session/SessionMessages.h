#pragma once
// =================================================================
// src/core/session/SessionMessages.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R2 Task 10.
//
// The wire-message shapes StateMirror::attachSession() (StateMirror.h)
// sends, and a live session (Task 18) will send and receive over the wss
// control channel. Five kinds exist so far:
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
//
// Task 11 appends CommandInvoke / CommandResult to SessionMessageKind and
// to SessionMessage. Nothing here anticipates their shape; do not add
// placeholder fields for them.
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
// =================================================================

#include <QByteArray>
#include <QList>
#include <QMetaType>

#include "core/session/MirrorSchema.h"

namespace NereusSDR {

/// Which kind of session message a SessionMessage carries. Task 11 appends
/// CommandInvoke / CommandResult here; nothing in this file dispatches
/// either.
enum class SessionMessageKind {
    Schema,
    ObjectCreate,
    ObjectDestroy,
    Delta,
    SnapshotComplete,
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
struct SessionMessage {
    SessionMessageKind kind = SessionMessageKind::Delta;

    QByteArray className;
    QList<SessionSchemaField> fields;
    QByteArray objectKey;
    QList<MirrorUpdate> updates;
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
