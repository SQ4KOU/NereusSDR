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
//   2026-08-08  J.J. Boyd / KG4VCF  Remote daemon R2 Task 18: handshake,
//                                    property-write and settings codec.
//                                    AI-assisted transformation via
//                                    Anthropic Claude Code.
//   2026-08-09  J.J. Boyd / KG4VCF  Whole-branch review, Important 2:
//                                    range-check the Int64/Enum value
//                                    before narrowing it to qlonglong.
//                                    AI-assisted transformation via
//                                    Anthropic Claude Code.
//   2026-08-09  J.J. Boyd / KG4VCF  Whole-branch review, Important 4:
//                                    settingsValueAbsent(), the distinct
//                                    absence encoding for a removed key.
//                                    AI-assisted transformation via
//                                    Anthropic Claude Code.
// =================================================================

#include "core/session/SessionMessages.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>

#include <cmath>
#include <limits>

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
                                              const QList<QByteArray>& affectedKeys,
                                              const QList<MirrorUpdate>& values)
{
    SessionMessage m;
    m.kind = SessionMessageKind::CommandResult;
    m.commandVerb = verb;
    m.commandId = commandId;
    m.accepted = accepted;
    m.reason = reason;
    m.affectedKeys = affectedKeys;
    m.updates = values;
    return m;
}

// ── Task 18 builders ─────────────────────────────────────────────────────

namespace {
/// One settings key/value pair in the generic {name, kind, value} shape
/// the settings kinds reuse. `ordinal` is meaningless for settings (there
/// is no schema and no ordinal dictionary for a flat key space) and is
/// always 0, exactly as it is for CommandInvoke arguments.
MirrorUpdate settingsEntry(const QString& key, const QString& value)
{
    return MirrorUpdate{ 0, key.toUtf8(), MirrorWireKind::Utf8, QVariant(value) };
}
} // namespace

SessionMessage SessionMessages::hello(quint16 major, quint16 minor,
                                      qint32 settingsSchemaVersion,
                                      const QString& peerName)
{
    SessionMessage m;
    m.kind = SessionMessageKind::Hello;
    m.protocolMajor = major;
    m.protocolMinor = minor;
    m.settingsSchemaVersion = settingsSchemaVersion;
    m.peerName = peerName;
    return m;
}

SessionMessage SessionMessages::authRequest(const QString& token)
{
    SessionMessage m;
    m.kind = SessionMessageKind::AuthRequest;
    m.token = token;
    return m;
}

SessionMessage SessionMessages::authResult(bool accepted, const QString& reason,
                                           bool retryable)
{
    SessionMessage m;
    m.kind = SessionMessageKind::AuthResult;
    m.accepted = accepted;
    m.reason = reason;
    m.retryable = retryable;
    return m;
}

SessionMessage SessionMessages::capabilities(const QList<MirrorUpdate>& descriptor)
{
    SessionMessage m;
    m.kind = SessionMessageKind::Capabilities;
    m.updates = descriptor;
    return m;
}

SessionMessage SessionMessages::sessionEnd(const QString& reason, bool retryable)
{
    SessionMessage m;
    m.kind = SessionMessageKind::SessionEnd;
    m.reason = reason;
    m.retryable = retryable;
    return m;
}

SessionMessage SessionMessages::propertyWrite(const QByteArray& objectKey,
                                              const QList<MirrorUpdate>& updates,
                                              quint32 writeId)
{
    SessionMessage m;
    m.kind = SessionMessageKind::PropertyWrite;
    m.objectKey = objectKey;
    m.updates = updates;
    m.writeId = writeId;
    return m;
}

SessionMessage SessionMessages::propertyResult(const QByteArray& objectKey,
                                               quint32 writeId,
                                               const QList<SessionPropertyResult>& results)
{
    SessionMessage m;
    m.kind = SessionMessageKind::PropertyResult;
    m.objectKey = objectKey;
    m.writeId = writeId;
    m.propertyResults = results;
    return m;
}

SessionMessage SessionMessages::settingsSnapshot(const QList<MirrorUpdate>& entries)
{
    SessionMessage m;
    m.kind = SessionMessageKind::SettingsSnapshot;
    m.updates = entries;
    return m;
}

SessionMessage SessionMessages::settingsWrite(const QString& key, const QString& value,
                                              const QString& originTag)
{
    SessionMessage m;
    m.kind = SessionMessageKind::SettingsWrite;
    m.objectKey = key.toUtf8();
    m.updates = { settingsEntry(key, value) };
    m.originTag = originTag;
    return m;
}

SessionMessage SessionMessages::settingsRemove(const QString& key)
{
    SessionMessage m;
    m.kind = SessionMessageKind::SettingsRemove;
    m.objectKey = key.toUtf8();
    return m;
}

SessionMessage SessionMessages::settingsValue(const QString& key, const QString& value,
                                              const QString& originTag)
{
    SessionMessage m;
    m.kind = SessionMessageKind::SettingsValue;
    m.objectKey = key.toUtf8();
    m.updates = { settingsEntry(key, value) };
    m.originTag = originTag;
    return m;
}

SessionMessage SessionMessages::settingsValueAbsent(const QString& key,
                                                    const QString& originTag)
{
    // No entry at all, deliberately: see the header. An entry carrying an
    // empty string is the exact thing this exists to stop being sent.
    SessionMessage m;
    m.kind = SessionMessageKind::SettingsValue;
    m.objectKey = key.toUtf8();
    m.originTag = originTag;
    return m;
}

SessionMessage SessionMessages::settingsReject(const QString& key, bool hasRestoredValue,
                                               const QString& restoredValue,
                                               const QString& reason)
{
    SessionMessage m;
    m.kind = SessionMessageKind::SettingsReject;
    m.reason = reason;
    m.objectKey = key.toUtf8();
    if (hasRestoredValue) {
        m.updates = { settingsEntry(key, restoredValue) };
    }
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
    { SessionMessageKind::Hello, "hello" },
    { SessionMessageKind::AuthRequest, "auth.request" },
    { SessionMessageKind::AuthResult, "auth.result" },
    { SessionMessageKind::Capabilities, "capabilities" },
    { SessionMessageKind::SessionEnd, "session.end" },
    { SessionMessageKind::PropertyWrite, "property.write" },
    { SessionMessageKind::PropertyResult, "property.result" },
    { SessionMessageKind::SettingsSnapshot, "settings.snapshot" },
    { SessionMessageKind::SettingsWrite, "settings.write" },
    { SessionMessageKind::SettingsRemove, "settings.remove" },
    { SessionMessageKind::SettingsValue, "settings.value" },
    { SessionMessageKind::SettingsReject, "settings.reject" },
    { SessionMessageKind::MediaControl, "media.control" },
    { SessionMessageKind::StationTelemetry, "station.metrics.v1" },
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
// JSON has no NaN and no Infinity. QJsonDocument::toJson() silently
// serialises a non-finite double as `null`, and a `null` fails
// fromJsonValue()'s isDouble() gate, so the WHOLE message is rejected --
// not just the one property.
//
// This is not hypothetical and it is not rare. SliceModel::snrDb defaults
// to std::numeric_limits<double>::quiet_NaN() (SliceModel.h, the RADE
// SNR row, which is genuinely unknown until RADE syncs), so EVERY slice
// object.create carried a NaN and every one of them was discarded on
// arrival -- the client saw no slices at all. Found by Task 18's own wss
// slot, which is the first thing in this plan to put a real slice snapshot
// through the codec end to end.
//
// So non-finite doubles travel as one of three explicit string tokens.
// Lossless (all three round-trip exactly, and +Inf stays distinct from
// -Inf), self-describing in a capture, and impossible to confuse with a
// real value because a Float64 property's ordinary encoding is a JSON
// number and never a string.
constexpr const char* kFloatNan = "nan";
constexpr const char* kFloatPosInf = "inf";
constexpr const char* kFloatNegInf = "-inf";

QJsonValue toJsonValue(MirrorWireKind kind, const QVariant& value)
{
    switch (kind) {
    case MirrorWireKind::Bool:
        return QJsonValue(value.toBool());
    case MirrorWireKind::Int64:
    case MirrorWireKind::Enum:
        return QJsonValue(static_cast<double>(value.toLongLong()));
    case MirrorWireKind::Float64: {
        const double d = value.toDouble();
        if (std::isnan(d)) {
            return QJsonValue(QString::fromLatin1(kFloatNan));
        }
        if (std::isinf(d)) {
            return QJsonValue(QString::fromLatin1(d > 0.0 ? kFloatPosInf : kFloatNegInf));
        }
        return QJsonValue(d);
    }
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
    case MirrorWireKind::Enum: {
        if (!json.isDouble()) {
            return false;
        }
        // Whole-branch review, Important 2: range-checked BEFORE the
        // narrowing static_cast<qlonglong>, the same discipline this file
        // already applies to the ordinal (updateFromJson / fieldFromJson,
        // below), to the CommandInvoke/CommandResult id, and to Hello's
        // major/minor/settingsSchema. This was the one place the header
        // comment's promise about untrusted input was not kept.
        //
        // {"value":1e300} parses cleanly and passes the isDouble() gate,
        // and casting it to qlonglong is a floating-to-integer conversion
        // of an unrepresentable value: undefined behaviour, and one that
        // diverges by platform (a saturating result on arm64, the
        // indefinite value on x86-64), so a developer's Mac and the Pi 4
        // target would not even agree on the wrong answer. Any UBSan
        // build trips on it.
        //
        // qlonglong's MINIMUM converts to double exactly (it is -2^63);
        // its MAXIMUM does not (2^63 - 1 rounds UP to 2^63), so the upper
        // bound has to be the exclusive 2^63 rather than an inexact
        // max(). Written as the negated minimum so both bounds come from
        // the type rather than from a transcribed digit string. The
        // comparison is spelt as a rejection of everything OUTSIDE the
        // range, which also refuses a NaN (every comparison against one
        // is false).
        constexpr double kMinAsDouble =
            static_cast<double>(std::numeric_limits<qlonglong>::min());
        constexpr double kOnePastMaxAsDouble = -kMinAsDouble;
        const double raw = json.toDouble();
        if (!(raw >= kMinAsDouble && raw < kOnePastMaxAsDouble)) {
            return false;
        }
        *out = QVariant(static_cast<qlonglong>(raw));
        return true;
    }
    case MirrorWireKind::Float64:
        if (json.isDouble()) {
            *out = QVariant(json.toDouble());
            return true;
        }
        // The three non-finite tokens toJsonValue() emits. Anything else
        // that is not a number is still rejected outright: this is the far
        // side of a socket, and "some other string" is not a double.
        if (json.isString()) {
            const QString token = json.toString();
            if (token == QLatin1String(kFloatNan)) {
                *out = QVariant(std::numeric_limits<double>::quiet_NaN());
                return true;
            }
            if (token == QLatin1String(kFloatPosInf)) {
                *out = QVariant(std::numeric_limits<double>::infinity());
                return true;
            }
            if (token == QLatin1String(kFloatNegInf)) {
                *out = QVariant(-std::numeric_limits<double>::infinity());
                return true;
            }
        }
        return false;
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
    case SessionMessageKind::StationTelemetry: {
        const auto payload = StationTelemetryCodec::encode(message.telemetry);
        if (!payload) { return {}; }
        o.insert(QStringLiteral("payload"), *payload);
        break;
    }
    case SessionMessageKind::MediaControl:
        o.insert(QStringLiteral("payload"), message.mediaPayload);
        break;
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
        if (!message.updates.isEmpty()) {
            QJsonArray values;
            for (const auto& value : message.updates) {
                values.append(updateToJson(value));
            }
            o.insert(QStringLiteral("values"), values);
        }
        break;
    }
    case SessionMessageKind::Hello:
        o.insert(QStringLiteral("major"), static_cast<int>(message.protocolMajor));
        o.insert(QStringLiteral("minor"), static_cast<int>(message.protocolMinor));
        o.insert(QStringLiteral("settingsSchema"),
                 static_cast<double>(message.settingsSchemaVersion));
        o.insert(QStringLiteral("peer"), message.peerName);
        break;
    case SessionMessageKind::AuthRequest:
        o.insert(QStringLiteral("token"), message.token);
        break;
    case SessionMessageKind::AuthResult:
        o.insert(QStringLiteral("accepted"), message.accepted);
        o.insert(QStringLiteral("reason"), message.reason);
        o.insert(QStringLiteral("retryable"), message.retryable);
        break;
    case SessionMessageKind::Capabilities:
    case SessionMessageKind::SettingsSnapshot: {
        QJsonArray entries;
        for (const MirrorUpdate& u : message.updates) {
            entries.append(updateToJson(u));
        }
        o.insert(QStringLiteral("properties"), entries);
        break;
    }
    case SessionMessageKind::SessionEnd:
        o.insert(QStringLiteral("reason"), message.reason);
        o.insert(QStringLiteral("retryable"), message.retryable);
        break;
    case SessionMessageKind::PropertyWrite: {
        o.insert(QStringLiteral("key"), QString::fromUtf8(message.objectKey));
        if (message.writeId != 0) {
            o.insert(QStringLiteral("writeId"), static_cast<double>(message.writeId));
        }
        QJsonArray props;
        for (const MirrorUpdate& u : message.updates) {
            props.append(updateToJson(u));
        }
        o.insert(QStringLiteral("properties"), props);
        break;
    }
    case SessionMessageKind::PropertyResult: {
        o.insert(QStringLiteral("key"), QString::fromUtf8(message.objectKey));
        o.insert(QStringLiteral("writeId"), static_cast<double>(message.writeId));
        QJsonArray results;
        for (const auto& result : message.propertyResults) {
            QJsonObject entry;
            entry.insert(QStringLiteral("property"), QString::fromUtf8(result.property));
            entry.insert(QStringLiteral("accepted"), result.accepted);
            entry.insert(QStringLiteral("reason"), result.reason);
            entry.insert(QStringLiteral("hasValue"), result.hasValue);
            if (result.hasValue) {
                entry.insert(QStringLiteral("value"), updateToJson(result.value));
            }
            results.append(entry);
        }
        o.insert(QStringLiteral("results"), results);
        break;
    }
    case SessionMessageKind::SettingsWrite:
    case SessionMessageKind::SettingsValue:
    case SessionMessageKind::SettingsRemove:
    case SessionMessageKind::SettingsReject: {
        o.insert(QStringLiteral("key"), QString::fromUtf8(message.objectKey));
        QJsonArray entries;
        for (const MirrorUpdate& u : message.updates) {
            entries.append(updateToJson(u));
        }
        o.insert(QStringLiteral("properties"), entries);
        if (message.kind == SessionMessageKind::SettingsReject && !message.reason.isEmpty()) {
            o.insert(QStringLiteral("reason"), message.reason);
        }
        if (message.kind == SessionMessageKind::SettingsWrite
            || message.kind == SessionMessageKind::SettingsValue) {
            o.insert(QStringLiteral("origin"), message.originTag);
        }
        break;
    }
    }

    const QByteArray wire = QJsonDocument(o).toJson(QJsonDocument::Compact);
    if (message.kind == SessionMessageKind::StationTelemetry
        && wire.size() > kMaxStationTelemetryBytes) {
        return {};
    }
    if (message.kind == SessionMessageKind::MediaControl
        && wire.size() > kMaxMediaControlBytes) {
        return {};
    }
    return wire;
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
    if (kind == SessionMessageKind::MediaControl
        && (wire.size() > kMaxMediaControlBytes
            || !o.value(QStringLiteral("payload")).isObject())) {
        return false;
    }
    if (kind == SessionMessageKind::StationTelemetry
        && (wire.size() > kMaxStationTelemetryBytes
            || !o.value(QStringLiteral("payload")).isObject())) {
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
        || kind == SessionMessageKind::ObjectDestroy || kind == SessionMessageKind::Delta
        || kind == SessionMessageKind::PropertyWrite
        || kind == SessionMessageKind::PropertyResult
        || kind == SessionMessageKind::SettingsWrite
        || kind == SessionMessageKind::SettingsRemove
        || kind == SessionMessageKind::SettingsValue
        || kind == SessionMessageKind::SettingsReject;
    if (needsKey && !o.value(QStringLiteral("key")).isString()) {
        return false;
    }
    if (kind == SessionMessageKind::PropertyResult
        || (kind == SessionMessageKind::PropertyWrite && o.contains(QStringLiteral("writeId")))) {
        const QJsonValue id = o.value(QStringLiteral("writeId"));
        const double raw = id.toDouble(-1);
        if (!id.isDouble() || !(raw >= 1.0 && raw <= 4294967295.0)
            || std::floor(raw) != raw) {
            return false;
        }
    }
    if (kind == SessionMessageKind::PropertyResult
        && !o.value(QStringLiteral("results")).isArray()) {
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
        kind == SessionMessageKind::ObjectCreate || kind == SessionMessageKind::Delta
        || kind == SessionMessageKind::PropertyWrite
        || kind == SessionMessageKind::Capabilities
        || kind == SessionMessageKind::SettingsSnapshot
        || kind == SessionMessageKind::SettingsWrite
        || kind == SessionMessageKind::SettingsRemove
        || kind == SessionMessageKind::SettingsValue
        || kind == SessionMessageKind::SettingsReject;
    if (needsProperties && !o.value(QStringLiteral("properties")).isArray()) {
        return false;
    }
    // Task 18: the section 7.0 handshake kinds. Same presence-and-type
    // discipline as every field above, for the same reason: this is the
    // far side of a socket, and these three kinds run BEFORE the peer has
    // authenticated, so they are the most exposed surface in the protocol.
    if (kind == SessionMessageKind::Hello) {
        if (!o.value(QStringLiteral("major")).isDouble()
            || !o.value(QStringLiteral("minor")).isDouble()
            || !o.value(QStringLiteral("settingsSchema")).isDouble()
            || !o.value(QStringLiteral("peer")).isString()) {
            return false;
        }
        // Range-checked before the narrowing casts below, the same
        // discipline the CommandInvoke id check above applies.
        const double majorRaw = o.value(QStringLiteral("major")).toDouble();
        const double minorRaw = o.value(QStringLiteral("minor")).toDouble();
        const double schemaRaw = o.value(QStringLiteral("settingsSchema")).toDouble();
        if (majorRaw < 0.0 || majorRaw > 65535.0 || minorRaw < 0.0 || minorRaw > 65535.0) {
            return false;
        }
        if (schemaRaw < -2147483648.0 || schemaRaw > 2147483647.0) {
            return false;
        }
    }
    if (kind == SessionMessageKind::AuthRequest
        && !o.value(QStringLiteral("token")).isString()) {
        return false;
    }
    if (kind == SessionMessageKind::AuthResult) {
        if (!o.value(QStringLiteral("accepted")).isBool()
            || !o.value(QStringLiteral("reason")).isString()) {
            return false;
        }
    }
    if (kind == SessionMessageKind::SessionEnd
        && !o.value(QStringLiteral("reason")).isString()) {
        return false;
    }
    if ((kind == SessionMessageKind::SettingsWrite
         || kind == SessionMessageKind::SettingsValue)
        && !o.value(QStringLiteral("origin")).isString()) {
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
    case SessionMessageKind::StationTelemetry:
        if (!StationTelemetryCodec::decode(o.value(QStringLiteral("payload")).toObject(),
                                           &message.telemetry)) {
            return false;
        }
        break;
    case SessionMessageKind::MediaControl:
        message.mediaPayload = o.value(QStringLiteral("payload")).toObject();
        break;
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
        const bool dspCommand = message.commandVerb.startsWith("nnr.")
            || message.commandVerb.startsWith("ps3.") || message.commandVerb.startsWith("dspAssets.");
        const double commandId = o.value(QStringLiteral("id")).toDouble(0.0);
        if (dspCommand && (!o.value(QStringLiteral("id")).isDouble()
            || !std::isfinite(commandId) || commandId < 1.0 || commandId > 4294967295.0
            || std::floor(commandId) != commandId)) {
            return false;
        }
        message.commandId = static_cast<quint32>(commandId);
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
        const bool dspCommand = message.commandVerb.startsWith("nnr.")
            || message.commandVerb.startsWith("ps3.") || message.commandVerb.startsWith("dspAssets.");
        const double commandId = o.value(QStringLiteral("id")).toDouble(0.0);
        if (dspCommand && (!o.value(QStringLiteral("id")).isDouble()
            || !std::isfinite(commandId) || commandId < 1.0 || commandId > 4294967295.0
            || std::floor(commandId) != commandId)) {
            return false;
        }
        message.commandId = static_cast<quint32>(commandId);
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
        if (o.contains(QStringLiteral("values"))) {
            if (!o.value(QStringLiteral("values")).isArray()) {
                return false;
            }
            const QJsonArray values = o.value(QStringLiteral("values")).toArray();
            if (values.size() > 128) {
                return false;
            }
            QSet<QByteArray> names;
            for (const QJsonValue& value : values) {
                MirrorUpdate update;
                if (!updateFromJson(value, &update) || update.name.isEmpty()
                    || names.contains(update.name)) {
                    return false;
                }
                names.insert(update.name);
                message.updates.append(update);
            }
        }
        break;
    }
    case SessionMessageKind::Hello:
        message.protocolMajor =
            static_cast<quint16>(o.value(QStringLiteral("major")).toDouble());
        message.protocolMinor =
            static_cast<quint16>(o.value(QStringLiteral("minor")).toDouble());
        message.settingsSchemaVersion =
            static_cast<qint32>(o.value(QStringLiteral("settingsSchema")).toDouble());
        message.peerName = o.value(QStringLiteral("peer")).toString();
        break;
    case SessionMessageKind::AuthRequest:
        message.token = o.value(QStringLiteral("token")).toString();
        break;
    case SessionMessageKind::AuthResult:
        message.accepted = o.value(QStringLiteral("accepted")).toBool();
        message.reason = o.value(QStringLiteral("reason")).toString();
        // Read LENIENTLY, unlike "accepted" and "reason" above, which the
        // validator requires. See SessionMessage::retryable: a peer built
        // before this field existed sends no such key, and absent has to
        // mean "assume permanent" -- the safe direction, and exactly what
        // that peer's own client half did.
        message.retryable = o.value(QStringLiteral("retryable")).toBool();
        break;
    case SessionMessageKind::SessionEnd:
        message.reason = o.value(QStringLiteral("reason")).toString();
        message.retryable = o.value(QStringLiteral("retryable")).toBool();
        break;
    case SessionMessageKind::PropertyResult: {
        message.objectKey = o.value(QStringLiteral("key")).toString().toUtf8();
        message.writeId = static_cast<quint32>(o.value(QStringLiteral("writeId")).toDouble());
        const QJsonArray entries = o.value(QStringLiteral("results")).toArray();
        if (entries.size() > 512) {
            return false;
        }
        QSet<QByteArray> seen;
        for (const QJsonValue& v : entries) {
            if (!v.isObject()) {
                return false;
            }
            const QJsonObject entry = v.toObject();
            if (!entry.value(QStringLiteral("property")).isString()
                || !entry.value(QStringLiteral("accepted")).isBool()
                || !entry.value(QStringLiteral("reason")).isString()
                || !entry.value(QStringLiteral("hasValue")).isBool()) {
                return false;
            }
            SessionPropertyResult result;
            result.property = entry.value(QStringLiteral("property")).toString().toUtf8();
            if (result.property.isEmpty() || seen.contains(result.property)) {
                return false;
            }
            seen.insert(result.property);
            result.accepted = entry.value(QStringLiteral("accepted")).toBool();
            result.reason = entry.value(QStringLiteral("reason")).toString();
            result.hasValue = entry.value(QStringLiteral("hasValue")).toBool();
            if (result.hasValue
                && (!updateFromJson(entry.value(QStringLiteral("value")), &result.value)
                    || result.value.name != result.property)) {
                return false;
            }
            if (result.accepted && (!result.hasValue || !result.reason.isEmpty())) {
                return false;
            }
            message.propertyResults.append(result);
        }
        break;
    }
    case SessionMessageKind::Capabilities:
    case SessionMessageKind::SettingsSnapshot:
    case SessionMessageKind::PropertyWrite:
    case SessionMessageKind::SettingsWrite:
    case SessionMessageKind::SettingsRemove:
    case SessionMessageKind::SettingsValue:
    case SessionMessageKind::SettingsReject: {
        if (kind != SessionMessageKind::Capabilities
            && kind != SessionMessageKind::SettingsSnapshot) {
            message.objectKey = o.value(QStringLiteral("key")).toString().toUtf8();
        }
        if (kind == SessionMessageKind::PropertyWrite) {
            message.writeId = static_cast<quint32>(o.value(QStringLiteral("writeId")).toDouble());
        }
        if (kind == SessionMessageKind::SettingsReject) {
            message.reason = o.value(QStringLiteral("reason")).toString();
        }
        if (kind == SessionMessageKind::SettingsWrite
            || kind == SessionMessageKind::SettingsValue) {
            message.originTag = o.value(QStringLiteral("origin")).toString();
        }
        const QJsonArray entries = o.value(QStringLiteral("properties")).toArray();
        message.updates.reserve(entries.size());
        for (const QJsonValue& v : entries) {
            MirrorUpdate u;
            if (!updateFromJson(v, &u)) {
                return false;
            }
            message.updates.append(u);
        }
        break;
    }
    }

    *out = message;
    return true;
}

} // namespace NereusSDR
