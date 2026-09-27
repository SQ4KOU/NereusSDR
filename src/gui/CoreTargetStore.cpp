// =================================================================
// src/gui/CoreTargetStore.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R3 Task 4g.
//
// iPhone app Task 18 (R-IOS-08): the saved Cores live under
// ConnectionTargets/V2, which adds each Core's identity fingerprint. A
// ConnectionTargets/V1 document is migrated once, every record's trust
// details carried over exactly and its identity left empty, and is never
// read again while V2 exists. V1 itself stays, so a build from before V2
// still finds its own list, but it follows V2's forgets and edits (Part C
// fix wave, R2-M3): a forgotten Core's record, token and all, leaves V1
// too, and an edited one is edited there.
//
// iPhone app plan Task 27 (R-IOS-16), 2026-09-26: `lastAddresses` on a V2
// record, the Core's last good addresses (rememberAddress()). J.J. Boyd
// (KG4VCF), AI-assisted via Anthropic Claude Code.
//
// iPhone app plan Task 28 fix wave (R-IOS-16), 2026-09-26:
// `controlChannelVersion` on a V2 record, what the Core declared at the last
// sign-in (rememberControlChannelVersion()); absent until then. J.J. Boyd
// (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#include "gui/CoreTargetStore.h"

#include "core/AppSettings.h"
#include "core/security/StationIdentity.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLatin1String>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace NereusSDR {
namespace {

constexpr auto kStorageKey = "ConnectionTargets/V2";
constexpr auto kV1StorageKey = "ConnectionTargets/V1";
constexpr int kVersion = 2;
constexpr int kV1Version = 1;
constexpr qsizetype kIdentityBytes = 32;
constexpr auto kLocalId = "local";
constexpr qsizetype kMaxDocumentBytes = 1024 * 1024;
constexpr qsizetype kMaxRecords = 128;

const QRegularExpression kIdPattern(
    QStringLiteral("^[A-Za-z0-9_-]{1,64}$"));

void setError(QString* error, const QString& text)
{
    if (error != nullptr) {
        *error = text;
    }
}

void clearError(QString* error)
{
    if (error != nullptr) {
        error->clear();
    }
}

bool hasString(const QJsonObject& object, const char* key, QString* value)
{
    const QJsonValue jsonValue = object.value(QLatin1String(key));
    if (!jsonValue.isString()) {
        return false;
    }
    *value = jsonValue.toString();
    return true;
}

bool isSavedId(const QString& id)
{
    return id != QLatin1String(kLocalId) && kIdPattern.match(id).hasMatch();
}

bool isBounded(const QString& text, qsizetype maximum)
{
    return text.size() <= maximum;
}

bool validateTarget(const SavedCoreTarget& target, QString* error)
{
    if (!isSavedId(target.id)) {
        setError(error, QStringLiteral("Saved Core target has an invalid identifier."));
        return false;
    }
    if (!isBounded(target.label, 512)
        || !isBounded(target.connection.url, 4096)
        || !isBounded(target.connection.token, 8192)
        || !isBounded(target.connection.fingerprint, 256)
        || !isBounded(target.lastRadioName, 512)
        || !isBounded(target.lastRadioMac, 64)) {
        setError(error, QStringLiteral("Saved Core target contains text that is too long."));
        return false;
    }
    if (!RemoteStationOptions::isValidStationUrl(target.connection.url)) {
        setError(error, QStringLiteral("Saved Core target has an invalid Core address."));
        return false;
    }
    if (!target.connection.identityFingerprint.isEmpty()
        && target.connection.identityFingerprint.size() != kIdentityBytes) {
        setError(error, QStringLiteral("Saved Core target has an invalid Core identity."));
        return false;
    }
    // iPhone app plan Task 27: the Core's last good addresses.
    if (target.connection.cachedAddresses.size() > RemoteStationOptions::kMaxCachedAddresses) {
        setError(error, QStringLiteral("Saved Core target has too many saved addresses."));
        return false;
    }
    for (const QString& address : target.connection.cachedAddresses) {
        if (!isBounded(address, 4096) || !RemoteStationOptions::isValidStationUrl(address)) {
            setError(error, QStringLiteral("Saved Core target has an invalid Core address."));
            return false;
        }
    }
    return true;
}

bool validateDocument(const QList<SavedCoreTarget>& targets, const QString& selectedId,
                      QString* error)
{
    if (targets.size() > kMaxRecords) {
        setError(error, QStringLiteral("Saved Core targets document has too many records."));
        return false;
    }

    QSet<QString> ids;
    for (const SavedCoreTarget& target : targets) {
        if (!validateTarget(target, error)) {
            return false;
        }
        if (ids.contains(target.id)) {
            setError(error, QStringLiteral("Saved Core targets document has duplicate identifiers."));
            return false;
        }
        ids.insert(target.id);
    }

    if (selectedId != QLatin1String(kLocalId) && !ids.contains(selectedId)) {
        setError(error, QStringLiteral("Saved Core targets document has an invalid selection."));
        return false;
    }
    return true;
}

// A V1 record (kV1Version) carries no identity.
QJsonObject toJson(const SavedCoreTarget& target, int version)
{
    QJsonObject object{
        {QStringLiteral("id"), target.id},
        {QStringLiteral("label"), target.label},
        {QStringLiteral("url"), target.connection.url},
        {QStringLiteral("token"), target.connection.token},
        {QStringLiteral("fingerprint"), target.connection.fingerprint},
        {QStringLiteral("allowUnpinned"), target.connection.allowUnpinned},
        {QStringLiteral("lastRadioName"), target.lastRadioName},
        {QStringLiteral("lastRadioMac"), target.lastRadioMac},
    };
    if (version == kVersion) {
        object.insert(QStringLiteral("identity"),
                      StationIdentity::toBase64Url(target.connection.identityFingerprint));
        // iPhone app plan Task 27: absent when there are none, so a record
        // with none is written exactly as before.
        if (!target.connection.cachedAddresses.isEmpty()) {
            object.insert(QStringLiteral("lastAddresses"),
                          QJsonArray::fromStringList(target.connection.cachedAddresses));
        }
        // Task 28 fix wave: absent until a sign-in recorded it.
        if (target.connection.controlChannelVersion >= 0) {
            object.insert(QStringLiteral("controlChannelVersion"),
                          target.connection.controlChannelVersion);
        }
    }
    return object;
}

QString serialize(const QList<SavedCoreTarget>& targets, const QString& selectedId,
                  int version = kVersion)
{
    QJsonArray cores;
    for (const SavedCoreTarget& target : targets) {
        cores.append(toJson(target, version));
    }
    const QJsonObject document{
        {QStringLiteral("version"), version},
        {QStringLiteral("selectedId"), selectedId},
        {QStringLiteral("cores"), cores},
    };
    return QString::fromUtf8(QJsonDocument(document).toJson(QJsonDocument::Compact));
}

// `version` is the document's own: kVersion reads V2 records (with their
// identity), kV1Version reads V1 records (with none).
bool parseDocument(const QString& text, int expectedVersion, QList<SavedCoreTarget>* targets,
                   QString* selectedId, QString* error)
{
    if (text.size() > kMaxDocumentBytes) {
        setError(error, QStringLiteral("Saved Core targets document is too large."));
        return false;
    }
    const QByteArray utf8 = text.toUtf8();
    if (utf8.size() > kMaxDocumentBytes) {
        setError(error, QStringLiteral("Saved Core targets document is too large."));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(utf8, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, QStringLiteral("Saved Core targets document is malformed."));
        return false;
    }

    const QJsonObject root = document.object();
    const QJsonValue version = root.value(QStringLiteral("version"));
    const QJsonValue cores = root.value(QStringLiteral("cores"));
    if (!version.isDouble() || version.toDouble() != double(expectedVersion) || !cores.isArray()
        || !hasString(root, "selectedId", selectedId)) {
        setError(error, QStringLiteral("Saved Core targets document has an unsupported schema."));
        return false;
    }
    if (cores.toArray().size() > kMaxRecords) {
        setError(error, QStringLiteral("Saved Core targets document has too many records."));
        return false;
    }

    QList<SavedCoreTarget> parsed;
    parsed.reserve(cores.toArray().size());
    for (const QJsonValue& value : cores.toArray()) {
        if (!value.isObject()) {
            setError(error, QStringLiteral("Saved Core targets document has an invalid record."));
            return false;
        }
        const QJsonObject object = value.toObject();
        SavedCoreTarget target;
        if (!hasString(object, "id", &target.id)
            || !hasString(object, "label", &target.label)
            || !hasString(object, "url", &target.connection.url)
            || !hasString(object, "token", &target.connection.token)
            || !hasString(object, "fingerprint", &target.connection.fingerprint)
            || !hasString(object, "lastRadioName", &target.lastRadioName)
            || !hasString(object, "lastRadioMac", &target.lastRadioMac)
            || !object.value(QStringLiteral("allowUnpinned")).isBool()) {
            setError(error, QStringLiteral("Saved Core targets document has an invalid record."));
            return false;
        }
        target.connection.allowUnpinned =
            object.value(QStringLiteral("allowUnpinned")).toBool();
        if (expectedVersion == kVersion) {
            QString identity;
            bool decoded = true;
            if (!hasString(object, "identity", &identity)) {
                setError(error, QStringLiteral("Saved Core targets document has an invalid record."));
                return false;
            }
            target.connection.identityFingerprint =
                identity.isEmpty() ? QByteArray()
                                   : StationIdentity::fromBase64Url(identity, &decoded);
            if (!decoded || (!identity.isEmpty()
                             && target.connection.identityFingerprint.size() != kIdentityBytes)) {
                setError(error, QStringLiteral("Saved Core targets document has an invalid record."));
                return false;
            }
            // iPhone app plan Task 27: optional; a list of addresses.
            const QJsonValue addresses = object.value(QStringLiteral("lastAddresses"));
            if (!addresses.isUndefined()) {
                if (!addresses.isArray()) {
                    setError(error, QStringLiteral("Saved Core targets document has an invalid record."));
                    return false;
                }
                for (const QJsonValue& address : addresses.toArray()) {
                    if (!address.isString()) {
                        setError(error, QStringLiteral("Saved Core targets document has an invalid record."));
                        return false;
                    }
                    target.connection.cachedAddresses.append(address.toString());
                }
            }
            // Task 28 fix wave: optional; a whole number, at least 0.
            const QJsonValue channel = object.value(QStringLiteral("controlChannelVersion"));
            if (!channel.isUndefined()) {
                const double value = channel.toDouble(-1.0);
                if (!channel.isDouble() || value < 0.0 || value > 65535.0
                    || value != static_cast<double>(static_cast<int>(value))) {
                    setError(error, QStringLiteral("Saved Core targets document has an invalid record."));
                    return false;
                }
                target.connection.controlChannelVersion = static_cast<int>(value);
            }
        }
        parsed.append(target);
    }

    if (!validateDocument(parsed, *selectedId, error)) {
        return false;
    }
    *targets = std::move(parsed);
    return true;
}

} // namespace

CoreTargetStore::CoreTargetStore(AppSettings& settings)
    : m_settings(settings)
{
}

bool CoreTargetStore::load(QString* error)
{
    // Keep the last successfully loaded contents visible for presentation,
    // but never let a failed reload authorize a subsequent destructive write.
    m_loaded = false;
    const QString key = QLatin1String(kStorageKey);
    if (m_settings.contains(key)) {
        QList<SavedCoreTarget> parsedTargets;
        QString parsedSelectedId;
        if (!parseDocument(m_settings.value(key).toString(), kVersion, &parsedTargets,
                           &parsedSelectedId, error)) {
            return false;
        }
        m_targets = std::move(parsedTargets);
        m_selectedId = std::move(parsedSelectedId);
        m_loaded = true;
        clearError(error);
        return true;
    }

    // iPhone app Task 18: the V1 list moves to V2 once, each record as it
    // was (address, token, pin, bench flag, label, last radio) and with no
    // identity yet; the key is enrolled on the Core's next token sign-in.
    // A V1 document that cannot be read writes nothing, like a bad V2.
    const QString v1Key = QLatin1String(kV1StorageKey);
    if (m_settings.contains(v1Key)) {
        QList<SavedCoreTarget> v1Targets;
        QString v1SelectedId;
        if (!parseDocument(m_settings.value(v1Key).toString(), kV1Version, &v1Targets,
                           &v1SelectedId, error)) {
            return false;
        }
        if (!persist(v1Targets, v1SelectedId, error)) {
            return false;
        }
        m_targets = std::move(v1Targets);
        m_selectedId = std::move(v1SelectedId);
        m_loaded = true;
        clearError(error);
        return true;
    }

    const QString legacyUrl =
        m_settings.value(QStringLiteral("RemoteStationUrl"), QString()).toString();
    if (!legacyUrl.isEmpty() && !RemoteStationOptions::isValidStationUrl(legacyUrl)) {
        setError(error, QStringLiteral("Legacy Core address is invalid; correct it before migrating."));
        return false;
    }

    QList<SavedCoreTarget> migratedTargets;
    QString migratedSelectedId = QLatin1String(kLocalId);
    if (!legacyUrl.isEmpty()) {
        SavedCoreTarget migrated;
        migrated.id = QStringLiteral("legacy-core");
        migrated.label = QUrl(legacyUrl, QUrl::StrictMode).host();
        migrated.connection.url = legacyUrl;
        migrated.connection.token =
            m_settings.value(QStringLiteral("RemoteStationToken"), QString()).toString();
        migrated.connection.fingerprint =
            m_settings.value(QStringLiteral("RemoteStationFingerprint"), QString()).toString();
        migrated.connection.allowUnpinned =
            m_settings.value(QStringLiteral("RemoteStationAllowUnpinned"),
                             QStringLiteral("False")).toString()
            == QStringLiteral("True");
        migratedTargets.append(migrated);
        migratedSelectedId = migrated.id;
    }

    if (!persist(migratedTargets, migratedSelectedId, error)) {
        return false;
    }
    m_targets = std::move(migratedTargets);
    m_selectedId = std::move(migratedSelectedId);
    m_loaded = true;
    clearError(error);
    return true;
}

QList<SavedCoreTarget> CoreTargetStore::targets() const
{
    return m_targets;
}

std::optional<SavedCoreTarget> CoreTargetStore::target(const QString& id) const
{
    for (const SavedCoreTarget& candidate : m_targets) {
        if (candidate.id == id) {
            return candidate;
        }
    }
    return std::nullopt;
}

QString CoreTargetStore::selectedId() const
{
    return m_selectedId;
}

bool CoreTargetStore::upsert(const SavedCoreTarget& target, QString* error)
{
    if (!m_loaded) {
        setError(error, QStringLiteral("Saved Core targets must be loaded before changes can be made."));
        return false;
    }
    if (!validateTarget(target, error)) {
        return false;
    }

    QList<SavedCoreTarget> updated = m_targets;
    bool replaced = false;
    for (SavedCoreTarget& existing : updated) {
        if (existing.id == target.id) {
            existing = target;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        if (updated.size() >= kMaxRecords) {
            setError(error, QStringLiteral("Saved Core target limit has been reached."));
            return false;
        }
        updated.append(target);
    }

    if (!persist(updated, m_selectedId, error)) {
        return false;
    }
    m_targets = std::move(updated);
    clearError(error);
    return true;
}

bool CoreTargetStore::rememberAddress(const QString& id, const QString& url, QString* error)
{
    const std::optional<SavedCoreTarget> found = target(id);
    if (!found) {
        setError(error, QStringLiteral("Saved Core target was not found."));
        return false;
    }
    if (!RemoteStationOptions::isValidStationUrl(url)) {
        setError(error, QStringLiteral("Saved Core target has an invalid Core address."));
        return false;
    }
    SavedCoreTarget updated = *found;
    QStringList& addresses = updated.connection.cachedAddresses;
    if (!addresses.isEmpty() && addresses.first() == url) {
        clearError(error);
        return true;
    }
    addresses.removeAll(url);
    addresses.prepend(url);
    while (addresses.size() > RemoteStationOptions::kMaxCachedAddresses) {
        addresses.removeLast();
    }
    return upsert(updated, error);
}

bool CoreTargetStore::rememberControlChannelVersion(const QString& id, int version,
                                                   QString* error)
{
    const std::optional<SavedCoreTarget> found = target(id);
    if (!found) {
        setError(error, QStringLiteral("Saved Core target was not found."));
        return false;
    }
    const int recorded = std::max(0, version);
    if (found->connection.controlChannelVersion == recorded) {
        clearError(error);
        return true;
    }
    SavedCoreTarget updated = *found;
    updated.connection.controlChannelVersion = recorded;
    return upsert(updated, error);
}

bool CoreTargetStore::remove(const QString& id, QString* error)
{
    if (!m_loaded) {
        setError(error, QStringLiteral("Saved Core targets must be loaded before changes can be made."));
        return false;
    }
    QList<SavedCoreTarget> updated = m_targets;
    auto it = std::find_if(updated.begin(), updated.end(), [&id](const SavedCoreTarget& target) {
        return target.id == id;
    });
    if (it == updated.end()) {
        setError(error, QStringLiteral("Saved Core target does not exist."));
        return false;
    }
    updated.erase(it);
    const QString updatedSelectedId = id == m_selectedId
        ? QLatin1String(kLocalId) : m_selectedId;

    if (!persist(updated, updatedSelectedId, error)) {
        return false;
    }
    m_targets = std::move(updated);
    m_selectedId = updatedSelectedId;
    clearError(error);
    return true;
}

bool CoreTargetStore::select(const QString& id, QString* error)
{
    if (!m_loaded) {
        setError(error, QStringLiteral("Saved Core targets must be loaded before changes can be made."));
        return false;
    }
    if (id != QLatin1String(kLocalId) && !target(id).has_value()) {
        setError(error, QStringLiteral("Saved Core target does not exist."));
        return false;
    }
    if (!persist(m_targets, id, error)) {
        return false;
    }
    m_selectedId = id;
    clearError(error);
    return true;
}

QString CoreTargetStore::createId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool CoreTargetStore::persist(const QList<SavedCoreTarget>& targets,
                              const QString& selectedId, QString* error)
{
    if (!validateDocument(targets, selectedId, error)) {
        return false;
    }

    const QString serialized = serialize(targets, selectedId);
    if (serialized.size() > kMaxDocumentBytes
        || serialized.toUtf8().size() > kMaxDocumentBytes) {
        setError(error, QStringLiteral("Saved Core targets document is too large."));
        return false;
    }

    // Part C fix wave (R2-M3): V1 stays for a build from before V2, but it
    // follows V2's forgets and edits: each V1 record is V2's record of the
    // same id (with no identity), or gone when V2 no longer has it, so a
    // forgotten Core's token, or a replaced one, does not stay behind. V1
    // gains no record V2 added. A V1 document that cannot be read is
    // removed, since nothing could filter it.
    const QString v1Key = QLatin1String(kV1StorageKey);
    const bool hadV1 = m_settings.contains(v1Key);
    const QVariant previousV1 = m_settings.value(v1Key);
    bool rewriteV1 = false;
    bool removeV1 = false;
    QString v1Serialized;
    if (hadV1) {
        QList<SavedCoreTarget> v1Targets;
        QString v1SelectedId;
        if (parseDocument(previousV1.toString(), kV1Version, &v1Targets, &v1SelectedId,
                          nullptr)) {
            QList<SavedCoreTarget> kept;
            for (const SavedCoreTarget& old : std::as_const(v1Targets)) {
                const auto current = std::find_if(
                    targets.cbegin(), targets.cend(),
                    [&old](const SavedCoreTarget& target) { return target.id == old.id; });
                if (current != targets.cend()) {
                    SavedCoreTarget carried = *current;
                    carried.connection.identityFingerprint.clear();
                    kept.append(carried);
                }
            }
            const bool selectionKept = std::any_of(
                kept.cbegin(), kept.cend(),
                [&v1SelectedId](const SavedCoreTarget& target) { return target.id == v1SelectedId; });
            const QString keptSelectedId =
                selectionKept ? v1SelectedId : QString::fromLatin1(kLocalId);
            v1Serialized = serialize(kept, keptSelectedId, kV1Version);
            rewriteV1 = v1Serialized != serialize(v1Targets, v1SelectedId, kV1Version);
        } else {
            removeV1 = true;
        }
    }

    const QString key = QLatin1String(kStorageKey);
    const bool hadPreviousValue = m_settings.contains(key);
    const QVariant previousValue = m_settings.value(key);
    m_settings.setValue(key, serialized);
    if (rewriteV1) {
        m_settings.setValue(v1Key, v1Serialized);
    } else if (removeV1) {
        m_settings.remove(v1Key);
    }

    QString saveError;
    if (m_settings.save(&saveError)) {
        return true;
    }

    if (hadPreviousValue) {
        m_settings.setValue(key, previousValue);
    } else {
        m_settings.remove(key);
    }
    if (rewriteV1 || removeV1) {
        m_settings.setValue(v1Key, previousV1);
    }
    setError(error, QStringLiteral("Saved Core targets could not be saved."));
    return false;
}

} // namespace NereusSDR
