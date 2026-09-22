// =================================================================
// src/gui/CoreTargetStore.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R3 Task 4g.
// =================================================================

#include "gui/CoreTargetStore.h"

#include "core/AppSettings.h"

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

constexpr auto kStorageKey = "ConnectionTargets/V1";
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
        setError(error, QStringLiteral("Saved Core target has an invalid station address."));
        return false;
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

QJsonObject toJson(const SavedCoreTarget& target)
{
    return {
        {QStringLiteral("id"), target.id},
        {QStringLiteral("label"), target.label},
        {QStringLiteral("url"), target.connection.url},
        {QStringLiteral("token"), target.connection.token},
        {QStringLiteral("fingerprint"), target.connection.fingerprint},
        {QStringLiteral("allowUnpinned"), target.connection.allowUnpinned},
        {QStringLiteral("lastRadioName"), target.lastRadioName},
        {QStringLiteral("lastRadioMac"), target.lastRadioMac},
    };
}

QString serialize(const QList<SavedCoreTarget>& targets, const QString& selectedId)
{
    QJsonArray cores;
    for (const SavedCoreTarget& target : targets) {
        cores.append(toJson(target));
    }
    const QJsonObject document{
        {QStringLiteral("version"), 1},
        {QStringLiteral("selectedId"), selectedId},
        {QStringLiteral("cores"), cores},
    };
    return QString::fromUtf8(QJsonDocument(document).toJson(QJsonDocument::Compact));
}

bool parseDocument(const QString& text, QList<SavedCoreTarget>* targets,
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
    if (!version.isDouble() || version.toDouble() != 1.0 || !cores.isArray()
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
        if (!parseDocument(m_settings.value(key).toString(), &parsedTargets,
                           &parsedSelectedId, error)) {
            return false;
        }
        m_targets = std::move(parsedTargets);
        m_selectedId = std::move(parsedSelectedId);
        m_loaded = true;
        clearError(error);
        return true;
    }

    const QString legacyUrl =
        m_settings.value(QStringLiteral("RemoteStationUrl"), QString()).toString();
    if (!legacyUrl.isEmpty() && !RemoteStationOptions::isValidStationUrl(legacyUrl)) {
        setError(error, QStringLiteral("Legacy station address is invalid; correct it before migrating."));
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

    const QString key = QLatin1String(kStorageKey);
    const bool hadPreviousValue = m_settings.contains(key);
    const QVariant previousValue = m_settings.value(key);
    m_settings.setValue(key, serialized);

    QString saveError;
    if (m_settings.save(&saveError)) {
        return true;
    }

    if (hadPreviousValue) {
        m_settings.setValue(key, previousValue);
    } else {
        m_settings.remove(key);
    }
    setError(error, QStringLiteral("Saved Core targets could not be saved."));
    return false;
}

} // namespace NereusSDR
