// =================================================================
// src/core/session/SessionMessages.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R2 Task 10.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-05  J.J. Boyd / KG4VCF  Remote daemon R2 Task 10: session
//                                    message shapes and JSON codec. AI-
//                                    assisted transformation via Anthropic
//                                    Claude Code.
//   2026-08-05  J.J. Boyd / KG4VCF  Remote daemon R2 Task 11: CommandInvoke
//                                    / CommandResult codec. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
// =================================================================

#include "core/session/SessionMessages.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace NereusSDR {

// ── Builders ─────────────────────────────────────────────────────────────

SessionMessage SessionMessages::schema(const QByteArray& className,
                                       const QList<SessionSchemaField>& fields)
{
    SessionMessage m;
    m.kind = SessionMessageKind::Schema;
    m.className = className;
    m.fields = fields;
    return m;
}

SessionMessage SessionMessages::objectCreate(const QByteArray& objectKey,
                                             const QByteArray& className,
                                             const QList<MirrorUpdate>& fullBag)
{
    SessionMessage m;
    m.kind = SessionMessageKind::ObjectCreate;
    m.objectKey = objectKey;
    m.className = className;
    m.updates = fullBag;
    return m;
}

SessionMessage SessionMessages::objectDestroy(const QByteArray& objectKey,
                                              const QByteArray& className)
{
    SessionMessage m;
    m.kind = SessionMessageKind::ObjectDestroy;
    m.objectKey = objectKey;
    m.className = className;
    return m;
}

SessionMessage SessionMessages::delta(const QByteArray& objectKey,
                                      const QList<MirrorUpdate>& changed)
{
    SessionMessage m;
    m.kind = SessionMessageKind::Delta;
    m.objectKey = objectKey;
    m.updates = changed;
    return m;
}

SessionMessage SessionMessages::snapshotComplete()
{
    SessionMessage m;
    m.kind = SessionMessageKind::SnapshotComplete;
    return m;
}

SessionMessage SessionMessages::commandInvoke(const QByteArray& verb, quint32 commandId,
                                              const QList<MirrorUpdate>& arguments)
{
    SessionMessage m;
    m.kind = SessionMessageKind::CommandInvoke;
    m.commandVerb = verb;
    m.commandId = commandId;
    m.arguments = arguments;
    return m;
}

SessionMessage SessionMessages::commandResult(const QByteArray& verb, quint32 commandId,
                                              bool accepted, const QString& reason,
                                              const QList<QByteArray>& affectedKeys)
{
    SessionMessage m;
    m.kind = SessionMessageKind::CommandResult;
    m.commandVerb = verb;
    m.commandId = commandId;
    m.accepted = accepted;
    m.reason = reason;
    m.affectedKeys = affectedKeys;
    return m;
}

// ── Kind name tables ─────────────────────────────────────────────────────

namespace {

struct KindName {
    SessionMessageKind kind;
    const char* name;
};
// The dotted style ("object.create") matches ObjectRegistry.h's own wire
// vocabulary comment and the R2 design addendum section 7's terminology,
// rather than inventing a second naming convention for the same concept.
constexpr KindName kKindNames[] = {
    { SessionMessageKind::Schema, "schema" },
    { SessionMessageKind::ObjectCreate, "object.create" },
    { SessionMessageKind::ObjectDestroy, "object.destroy" },
    { SessionMessageKind::Delta, "delta" },
    { SessionMessageKind::SnapshotComplete, "snapshot.complete" },
    { SessionMessageKind::CommandInvoke, "command.invoke" },
    { SessionMessageKind::CommandResult, "command.result" },
};

struct WireKindName {
    MirrorWireKind kind;
    const char* name;
};
constexpr WireKindName kWireKindNames[] = {
    { MirrorWireKind::Bool, "bool" },
    { MirrorWireKind::Int64, "i64" },
    { MirrorWireKind::Float64, "f64" },
    { MirrorWireKind::Utf8, "utf8" },
    { MirrorWireKind::Enum, "enum" },
    { MirrorWireKind::Unsupported, "unsupported" },
};

} // namespace

QByteArray SessionMessages::kindName(SessionMessageKind kind)
{
    for (const KindName& k : kKindNames) {
        if (k.kind == kind) {
            return QByteArray(k.name);
        }
    }
    return QByteArray();
}

bool SessionMessages::kindFromName(const QByteArray& name, SessionMessageKind* out)
{
    for (const KindName& k : kKindNames) {
        if (name == k.name) {
            if (out != nullptr) {
                *out = k.kind;
            }
            return true;
        }
    }
    return false;
}

QByteArray SessionMessages::wireKindName(MirrorWireKind kind)
{
    for (const WireKindName& k : kWireKindNames) {
        if (k.kind == kind) {
            return QByteArray(k.name);
        }
    }
    return QByteArray();
}

bool SessionMessages::wireKindFromName(const QByteArray& name, MirrorWireKind* out)
{
    for (const WireKindName& k : kWireKindNames) {
        if (name == k.name) {
            if (out != nullptr) {
                *out = k.kind;
            }
            return true;
        }
    }
    return false;
}

// ── Value codec: MirrorUpdate.value <-> QJsonValue ──────────────────────

namespace {

// MirrorSchema::encode() (MirrorSchema.cpp) settles every wire value onto
// exactly one of four QVariant runtime types before StateMirror ever hands
// it here: bool, qlonglong (Int64 AND Enum alike -- see its own `case
// MirrorWireKind::Int64: case MirrorWireKind::Enum:` fallthrough), double,
// QString. This mirrors that same choice on the JSON side rather than
// inventing a second one.
QJsonValue toJsonValue(MirrorWireKind kind, const QVariant& value)
{
    switch (kind) {
    case MirrorWireKind::Bool:
        return QJsonValue(value.toBool());
    case MirrorWireKind::Int64:
    case MirrorWireKind::Enum:
        return QJsonValue(static_cast<double>(value.toLongLong()));
    case MirrorWireKind::Float64:
        return QJsonValue(value.toDouble());
    case MirrorWireKind::Utf8:
        return QJsonValue(value.toString());
    case MirrorWireKind::Unsupported:
        break;
    }
    return QJsonValue();
}

bool fromJsonValue(MirrorWireKind kind, const QJsonValue& json, QVariant* out)
{
    if (out == nullptr) {
        return false;
    }
    switch (kind) {
    case MirrorWireKind::Bool:
        if (!json.isBool()) {
            return false;
        }
        *out = QVariant(json.toBool());
        return true;
    case MirrorWireKind::Int64:
    case MirrorWireKind::Enum:
        if (!json.isDouble()) {
            return false;
        }
        *out = QVariant(static_cast<qlonglong>(json.toDouble()));
        return true;
    case MirrorWireKind::Float64:
        if (!json.isDouble()) {
            return false;
        }
        *out = QVariant(json.toDouble());
        return true;
    case MirrorWireKind::Utf8:
        if (!json.isString()) {
            return false;
        }
        *out = QVariant(json.toString());
        return true;
    case MirrorWireKind::Unsupported:
        break;
    }
    return false;
}

QJsonObject updateToJson(const MirrorUpdate& u)
{
    QJsonObject o;
    o.insert(QStringLiteral("ordinal"), static_cast<int>(u.ordinal));
    o.insert(QStringLiteral("name"), QString::fromUtf8(u.name));
    o.insert(QStringLiteral("kind"), QString::fromUtf8(SessionMessages::wireKindName(u.kind)));
    o.insert(QStringLiteral("value"), toJsonValue(u.kind, u.value));
    return o;
}

// False (leaving *out untouched) for a malformed entry: missing/negative/
// overflowing ordinal, an unrecognised kind name, or a value that does not
// match the kind it claims. Untrusted input from the far side of a socket
// gets rejected rather than coerced into something plausible-looking.
bool updateFromJson(const QJsonValue& v, MirrorUpdate* out)
{
    if (!v.isObject() || out == nullptr) {
        return false;
    }
    const QJsonObject o = v.toObject();
    if (!o.contains(QStringLiteral("ordinal")) || !o.value(QStringLiteral("ordinal")).isDouble()) {
        return false;
    }
    const double ordinalRaw = o.value(QStringLiteral("ordinal")).toDouble();
    if (ordinalRaw < 0.0 || ordinalRaw > 65535.0) {
        return false;
    }

    MirrorWireKind kind = MirrorWireKind::Unsupported;
    if (!SessionMessages::wireKindFromName(
            o.value(QStringLiteral("kind")).toString().toUtf8(), &kind)) {
        return false;
    }

    QVariant value;
    if (!fromJsonValue(kind, o.value(QStringLiteral("value")), &value)) {
        return false;
    }

    out->ordinal = static_cast<quint16>(ordinalRaw);
    out->name = o.value(QStringLiteral("name")).toString().toUtf8();
    out->kind = kind;
    out->value = value;
    return true;
}

QJsonObject fieldToJson(const SessionSchemaField& f)
{
    QJsonObject o;
    o.insert(QStringLiteral("ordinal"), static_cast<int>(f.ordinal));
    o.insert(QStringLiteral("name"), QString::fromUtf8(f.name));
    o.insert(QStringLiteral("kind"), QString::fromUtf8(SessionMessages::wireKindName(f.kind)));
    return o;
}

bool fieldFromJson(const QJsonValue& v, SessionSchemaField* out)
{
    if (!v.isObject() || out == nullptr) {
        return false;
    }
    const QJsonObject o = v.toObject();
    if (!o.contains(QStringLiteral("ordinal")) || !o.value(QStringLiteral("ordinal")).isDouble()) {
        return false;
    }
    const double ordinalRaw = o.value(QStringLiteral("ordinal")).toDouble();
    if (ordinalRaw < 0.0 || ordinalRaw > 65535.0) {
        return false;
    }

    MirrorWireKind kind = MirrorWireKind::Unsupported;
    if (!SessionMessages::wireKindFromName(
            o.value(QStringLiteral("kind")).toString().toUtf8(), &kind)) {
        return false;
    }

    out->ordinal = static_cast<quint16>(ordinalRaw);
    out->name = o.value(QStringLiteral("name")).toString().toUtf8();
    out->kind = kind;
    return true;
}

} // namespace

// ── Message codec ────────────────────────────────────────────────────────

QByteArray SessionMessages::encode(const SessionMessage& message)
{
    QJsonObject o;
    o.insert(QStringLiteral("type"), QString::fromUtf8(kindName(message.kind)));

    switch (message.kind) {
    case SessionMessageKind::Schema: {
        o.insert(QStringLiteral("class"), QString::fromUtf8(message.className));
        QJsonArray fields;
        for (const SessionSchemaField& f : message.fields) {
            fields.append(fieldToJson(f));
        }
        o.insert(QStringLiteral("fields"), fields);
        break;
    }
    case SessionMessageKind::ObjectCreate: {
        o.insert(QStringLiteral("key"), QString::fromUtf8(message.objectKey));
        o.insert(QStringLiteral("class"), QString::fromUtf8(message.className));
        QJsonArray props;
        for (const MirrorUpdate& u : message.updates) {
            props.append(updateToJson(u));
        }
        o.insert(QStringLiteral("properties"), props);
        break;
    }
    case SessionMessageKind::ObjectDestroy:
        o.insert(QStringLiteral("key"), QString::fromUtf8(message.objectKey));
        o.insert(QStringLiteral("class"), QString::fromUtf8(message.className));
        break;
    case SessionMessageKind::Delta: {
        o.insert(QStringLiteral("key"), QString::fromUtf8(message.objectKey));
        QJsonArray props;
        for (const MirrorUpdate& u : message.updates) {
            props.append(updateToJson(u));
        }
        o.insert(QStringLiteral("properties"), props);
        break;
    }
    case SessionMessageKind::SnapshotComplete:
        break;
    case SessionMessageKind::CommandInvoke: {
        o.insert(QStringLiteral("verb"), QString::fromUtf8(message.commandVerb));
        o.insert(QStringLiteral("id"), static_cast<double>(message.commandId));
        QJsonArray args;
        for (const MirrorUpdate& u : message.arguments) {
            args.append(updateToJson(u));
        }
        o.insert(QStringLiteral("args"), args);
        break;
    }
    case SessionMessageKind::CommandResult: {
        o.insert(QStringLiteral("verb"), QString::fromUtf8(message.commandVerb));
        o.insert(QStringLiteral("id"), static_cast<double>(message.commandId));
        o.insert(QStringLiteral("accepted"), message.accepted);
        o.insert(QStringLiteral("reason"), message.reason);
        QJsonArray affected;
        for (const QByteArray& key : message.affectedKeys) {
            affected.append(QString::fromUtf8(key));
        }
        o.insert(QStringLiteral("affected"), affected);
        break;
    }
    }

    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

bool SessionMessages::decode(const QByteArray& wire, SessionMessage* out)
{
    if (out == nullptr) {
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(wire, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }
    const QJsonObject o = doc.object();

    SessionMessageKind kind = SessionMessageKind::Delta;
    if (!kindFromName(o.value(QStringLiteral("type")).toString().toUtf8(), &kind)) {
        return false;
    }

    // Structural fields a well-formed message of this kind MUST carry,
    // checked for PRESENCE and TYPE before any of them is read. A missing
    // field decodes to QJsonValue::Undefined, and Undefined's toString()/
    // toArray() silently return an empty QString/QJsonArray -- exactly the
    // shape a genuinely empty-but-present field would have, which is why
    // reading through those conversions without checking first would
    // silently coerce "absent" into "empty" rather than reject it. This is
    // the far side of a socket a remote peer controls.
    const bool needsKey = kind == SessionMessageKind::ObjectCreate
        || kind == SessionMessageKind::ObjectDestroy || kind == SessionMessageKind::Delta;
    if (needsKey && !o.value(QStringLiteral("key")).isString()) {
        return false;
    }
    const bool needsClass = kind == SessionMessageKind::Schema
        || kind == SessionMessageKind::ObjectCreate || kind == SessionMessageKind::ObjectDestroy;
    if (needsClass && !o.value(QStringLiteral("class")).isString()) {
        return false;
    }
    if (kind == SessionMessageKind::Schema && !o.value(QStringLiteral("fields")).isArray()) {
        return false;
    }
    const bool needsProperties =
        kind == SessionMessageKind::ObjectCreate || kind == SessionMessageKind::Delta;
    if (needsProperties && !o.value(QStringLiteral("properties")).isArray()) {
        return false;
    }
    // Task 11: CommandInvoke and CommandResult share "verb" and "id";
    // everything else is kind-specific. Same presence-and-type discipline
    // as every field above -- a missing "accepted" would otherwise decode
    // through QJsonValue::toBool()'s false default, indistinguishable from
    // a genuine, deliberate rejection.
    const bool needsVerb =
        kind == SessionMessageKind::CommandInvoke || kind == SessionMessageKind::CommandResult;
    if (needsVerb && !o.value(QStringLiteral("verb")).isString()) {
        return false;
    }
    if (needsVerb && !o.value(QStringLiteral("id")).isDouble()) {
        return false;
    }
    // Fix round 1 review finding (Important 2): range-check BEFORE the
    // narrowing static_cast<quint32> in the switch below, the same
    // discipline updateFromJson() and fieldFromJson() already apply to
    // MirrorUpdate/SessionSchemaField's quint16 ordinal (this file, above:
    // `ordinalRaw < 0.0 || ordinalRaw > 65535.0`, checked before the cast).
    // Without this, {"id":-1} or {"id":1e30} would both pass the isDouble()
    // gate above and then feed a floating-to-unsigned conversion of an
    // unrepresentable value into static_cast<quint32>, which is undefined
    // behaviour -- exactly what this file's own header comment promises
    // untrusted input never triggers. One check covers both CommandInvoke
    // and CommandResult, mirroring needsVerb's own single-check shape.
    if (needsVerb) {
        const double idRaw = o.value(QStringLiteral("id")).toDouble();
        if (idRaw < 0.0 || idRaw > 4294967295.0) { // quint32 max
            return false;
        }
    }
    if (kind == SessionMessageKind::CommandInvoke && !o.value(QStringLiteral("args")).isArray()) {
        return false;
    }
    if (kind == SessionMessageKind::CommandResult) {
        if (!o.value(QStringLiteral("accepted")).isBool()) {
            return false;
        }
        if (!o.value(QStringLiteral("reason")).isString()) {
            return false;
        }
        if (!o.value(QStringLiteral("affected")).isArray()) {
            return false;
        }
    }

    SessionMessage message;
    message.kind = kind;

    switch (kind) {
    case SessionMessageKind::Schema: {
        message.className = o.value(QStringLiteral("class")).toString().toUtf8();
        const QJsonArray fields = o.value(QStringLiteral("fields")).toArray();
        message.fields.reserve(fields.size());
        for (const QJsonValue& v : fields) {
            SessionSchemaField f;
            if (!fieldFromJson(v, &f)) {
                return false;
            }
            message.fields.append(f);
        }
        break;
    }
    case SessionMessageKind::ObjectCreate:
    case SessionMessageKind::Delta: {
        message.objectKey = o.value(QStringLiteral("key")).toString().toUtf8();
        if (kind == SessionMessageKind::ObjectCreate) {
            message.className = o.value(QStringLiteral("class")).toString().toUtf8();
        }
        const QJsonArray props = o.value(QStringLiteral("properties")).toArray();
        message.updates.reserve(props.size());
        for (const QJsonValue& v : props) {
            MirrorUpdate u;
            if (!updateFromJson(v, &u)) {
                return false;
            }
            message.updates.append(u);
        }
        break;
    }
    case SessionMessageKind::ObjectDestroy:
        message.objectKey = o.value(QStringLiteral("key")).toString().toUtf8();
        message.className = o.value(QStringLiteral("class")).toString().toUtf8();
        break;
    case SessionMessageKind::SnapshotComplete:
        break;
    case SessionMessageKind::CommandInvoke: {
        message.commandVerb = o.value(QStringLiteral("verb")).toString().toUtf8();
        message.commandId = static_cast<quint32>(o.value(QStringLiteral("id")).toDouble());
        const QJsonArray args = o.value(QStringLiteral("args")).toArray();
        message.arguments.reserve(args.size());
        for (const QJsonValue& v : args) {
            MirrorUpdate u;
            if (!updateFromJson(v, &u)) {
                return false;
            }
            message.arguments.append(u);
        }
        break;
    }
    case SessionMessageKind::CommandResult: {
        message.commandVerb = o.value(QStringLiteral("verb")).toString().toUtf8();
        message.commandId = static_cast<quint32>(o.value(QStringLiteral("id")).toDouble());
        message.accepted = o.value(QStringLiteral("accepted")).toBool();
        message.reason = o.value(QStringLiteral("reason")).toString();
        const QJsonArray affected = o.value(QStringLiteral("affected")).toArray();
        message.affectedKeys.reserve(affected.size());
        for (const QJsonValue& v : affected) {
            // Same discipline as every other array element decoded in this
            // file: reject rather than coerce a non-string entry.
            if (!v.isString()) {
                return false;
            }
            message.affectedKeys.append(v.toString().toUtf8());
        }
        break;
    }
    }

    *out = message;
    return true;
}

} // namespace NereusSDR
