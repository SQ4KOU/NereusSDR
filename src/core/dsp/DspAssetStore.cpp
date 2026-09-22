// =================================================================
// src/core/dsp/DspAssetStore.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original station asset storage.
//
// Modification history (NereusSDR):
//   2026-09-21 — Created for NereusSDR by J.J. Boyd (KG4VCF), with
//                AI-assisted implementation via OpenAI Codex.
// =================================================================

#include "DspAssetStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#include <algorithm>

namespace NereusSDR {
namespace {

constexpr qint64 kMaxManifestBytes = 1024 * 1024;
constexpr qsizetype kMaxLabelCharacters = 256;
constexpr qsizetype kMaxRadioIdentityCharacters = 256;

QString setError(QString* destination, const QString& error)
{
    if (destination) {
        *destination = error;
    }
    return {};
}

QJsonObject recordToJson(const DspAssetRecord& record)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), record.id);
    object.insert(QStringLiteral("kind"), dspAssetKindName(record.kind));
    object.insert(QStringLiteral("hash"), record.hashHex);
    object.insert(QStringLiteral("size"), double(record.size));
    object.insert(QStringLiteral("format"), record.format);
    object.insert(QStringLiteral("version"), record.version);
    object.insert(QStringLiteral("compatibility"), record.compatibility);
    object.insert(QStringLiteral("numericEncoding"), record.numericEncoding);
    object.insert(QStringLiteral("label"), record.label);
    object.insert(QStringLiteral("radioIdentity"), record.radioIdentity);
    object.insert(QStringLiteral("validationState"), QStringLiteral("accepted"));
    return object;
}

bool isAssetId(const QString& id)
{
    static const QRegularExpression expression(
        QStringLiteral("\\Asha256:[0-9a-f]{64}\\z"));
    return expression.match(id).hasMatch();
}

} // namespace

DspAssetStore::DspAssetStore(const QString& stationProfileRoot)
    : m_rootDirectory(QDir(stationProfileRoot).filePath(QStringLiteral("dsp-assets")))
    , m_assetsDirectory(QDir(m_rootDirectory).filePath(QStringLiteral("assets")))
    , m_stagingDirectory(QDir(m_rootDirectory).filePath(QStringLiteral("staging")))
    , m_manifestPath(QDir(m_rootDirectory).filePath(QStringLiteral("assets.json")))
{
    if (stationProfileRoot.trimmed().isEmpty()) {
        m_lastError = QStringLiteral("Station profile root is empty");
        return;
    }
    if (!QDir().mkpath(m_assetsDirectory) || !QDir().mkpath(m_stagingDirectory)) {
        m_lastError = QStringLiteral("Could not create DSP asset directories beneath %1")
                          .arg(stationProfileRoot);
        return;
    }
    cleanInterruptedImports();
    if (!loadManifest()) {
        return;
    }
    m_valid = true;
}

qint64 DspAssetStore::sizeLimit(DspAssetKind kind) const
{
    return kind == DspAssetKind::NnrModel ? DspAssetValidation::kMaxNnrModelBytes
                                          : DspAssetValidation::kMaxPs3CorrectionBytes;
}

QString DspAssetStore::assetPath(DspAssetKind kind, const QString& hashHex) const
{
    const QString suffix = kind == DspAssetKind::NnrModel ? QStringLiteral(".nnr")
                                                           : QStringLiteral(".ps3");
    return QDir(m_assetsDirectory)
        .filePath(QStringLiteral("asset-%1%2").arg(hashHex.first(32), suffix));
}

QString DspAssetStore::assetPath(const DspAssetRecord& record) const
{
    return assetPath(record.kind, record.hashHex);
}

int DspAssetStore::recordIndex(const QString& id) const
{
    for (int i = 0; i < m_records.size(); ++i) {
        if (m_records[i].id == id) {
            return i;
        }
    }
    return -1;
}

bool DspAssetStore::loadManifest()
{
    QFile file(m_manifestPath);
    if (!file.exists()) {
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        m_lastError = QStringLiteral("Could not open DSP asset manifest: %1").arg(file.errorString());
        return false;
    }
    const QByteArray bytes = file.read(kMaxManifestBytes + 1);
    if (bytes.size() > kMaxManifestBytes) {
        m_lastError = QStringLiteral("DSP asset manifest exceeds the 1 MiB limit");
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        m_lastError = QStringLiteral("Invalid DSP asset manifest: %1").arg(parseError.errorString());
        return false;
    }
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schema")).toInt() != 1
        || !root.value(QStringLiteral("assets")).isArray()) {
        m_lastError = QStringLiteral("Unsupported DSP asset manifest schema");
        return false;
    }

    QSet<QString> ids;
    for (const QJsonValue& value : root.value(QStringLiteral("assets")).toArray()) {
        if (!value.isObject()) {
            m_lastError = QStringLiteral("DSP asset manifest contains a non-object record");
            return false;
        }
        const QJsonObject object = value.toObject();
        DspAssetRecord record;
        record.id = object.value(QStringLiteral("id")).toString();
        record.hashHex = object.value(QStringLiteral("hash")).toString();
        record.size = qint64(object.value(QStringLiteral("size")).toDouble(-1));
        record.format = object.value(QStringLiteral("format")).toString();
        record.version = object.value(QStringLiteral("version")).toInt();
        record.compatibility = object.value(QStringLiteral("compatibility")).toString();
        record.numericEncoding = object.value(QStringLiteral("numericEncoding")).toString();
        record.label = object.value(QStringLiteral("label")).toString();
        record.radioIdentity = object.value(QStringLiteral("radioIdentity")).toString();
        if (!dspAssetKindFromName(object.value(QStringLiteral("kind")).toString(), &record.kind)
            || !isAssetId(record.id) || record.id != QStringLiteral("sha256:") + record.hashHex
            || ids.contains(record.id) || record.size < 0 || record.size > sizeLimit(record.kind)
            || record.label.size() > kMaxLabelCharacters
            || record.radioIdentity.size() > kMaxRadioIdentityCharacters) {
            m_lastError = QStringLiteral("DSP asset manifest contains an invalid record");
            return false;
        }
        ids.insert(record.id);
        m_records.append(record);
    }
    return true;
}

bool DspAssetStore::saveManifest(QString* error) const
{
    QJsonArray assets;
    for (const DspAssetRecord& record : m_records) {
        assets.append(recordToJson(record));
    }
    QJsonObject root;
    root.insert(QStringLiteral("schema"), 1);
    root.insert(QStringLiteral("assets"), assets);
    const QByteArray encoded = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (encoded.size() > kMaxManifestBytes) {
        setError(error, QStringLiteral("DSP asset manifest exceeds the 1 MiB limit"));
        return false;
    }

    QSaveFile file(m_manifestPath);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, QStringLiteral("Could not open DSP asset manifest: %1")
                            .arg(file.errorString()));
        return false;
    }
    if (file.write(encoded) != encoded.size() || !file.commit()) {
        file.cancelWriting();
        setError(error, QStringLiteral("Could not publish DSP asset manifest: %1")
                            .arg(file.errorString()));
        return false;
    }
    return true;
}

void DspAssetStore::cleanInterruptedImports()
{
    QDir staging(m_stagingDirectory);
    const QFileInfoList stale = staging.entryInfoList(
        {QStringLiteral("*.part")}, QDir::Files | QDir::Hidden | QDir::NoSymLinks);
    for (const QFileInfo& file : stale) {
        QFile::remove(file.absoluteFilePath());
    }
}

QList<DspAssetRecord> DspAssetStore::assets() const
{
    QList<DspAssetRecord> result = m_records;
    for (DspAssetRecord& record : result) {
        QString error;
        record.valid = verifyRecord(record, nullptr, &error);
        record.validationError = error;
    }
    return result;
}

bool DspAssetStore::verifyRecord(const DspAssetRecord& record, QByteArray* bytes,
                                 QString* error) const
{
    if (!isAssetId(record.id) || record.id != QStringLiteral("sha256:") + record.hashHex) {
        setError(error, QStringLiteral("Invalid DSP asset ID"));
        return false;
    }
    const QString path = assetPath(record);
    const QFileInfo info(path);
    if (!info.isFile() || info.isSymLink()) {
        setError(error, QStringLiteral("DSP asset file is missing or is not a regular file"));
        return false;
    }
    if (info.size() != record.size || info.size() > sizeLimit(record.kind)) {
        setError(error, QStringLiteral("DSP asset size does not match its metadata"));
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("Could not read DSP asset: %1").arg(file.errorString()));
        return false;
    }
    QByteArray stored = file.read(sizeLimit(record.kind) + 1);
    if (stored.size() != record.size) {
        setError(error, QStringLiteral("DSP asset size changed while reading"));
        return false;
    }
    const QString actualHash = QString::fromLatin1(
        QCryptographicHash::hash(stored, QCryptographicHash::Sha256).toHex());
    if (actualHash != record.hashHex) {
        setError(error, QStringLiteral("DSP asset hash verification failed"));
        return false;
    }
    const DspAssetValidationResult validation = DspAssetValidation::validate(record.kind, stored);
    if (!validation.accepted) {
        setError(error, QStringLiteral("Stored DSP asset is invalid: %1").arg(validation.error));
        return false;
    }
    if (validation.format != record.format || validation.version != record.version
        || validation.compatibility != record.compatibility) {
        setError(error, QStringLiteral("DSP asset compatibility metadata does not match its bytes"));
        return false;
    }
    if (bytes) {
        *bytes = stored;
    }
    if (error) {
        error->clear();
    }
    return true;
}

DspAssetImportResult DspAssetStore::importBytes(DspAssetKind kind, const QString& label,
                                                const QByteArray& bytes,
                                                const QString& radioIdentity)
{
    DspAssetImportResult result;
    if (!m_valid) {
        result.error = m_lastError;
        return result;
    }
    const QString cleanLabel = label.trimmed();
    if (cleanLabel.size() > kMaxLabelCharacters) {
        result.error = QStringLiteral("DSP asset label exceeds 256 characters");
        return result;
    }
    if (radioIdentity.size() > kMaxRadioIdentityCharacters) {
        result.error = QStringLiteral("Radio identity exceeds 256 characters");
        return result;
    }
    if (kind == DspAssetKind::NnrModel && !radioIdentity.isEmpty()) {
        result.error = QStringLiteral("NNR model assets are station-scoped, not radio-scoped");
        return result;
    }

    const DspAssetValidationResult validation = DspAssetValidation::validate(kind, bytes);
    if (!validation.accepted) {
        result.error = validation.error;
        return result;
    }

    DspAssetRecord record;
    record.id = QStringLiteral("sha256:") + validation.hashHex;
    record.kind = kind;
    record.hashHex = validation.hashHex;
    record.size = validation.size;
    record.format = validation.format;
    record.version = validation.version;
    record.compatibility = validation.compatibility;
    record.numericEncoding = validation.numericEncoding;
    record.label = cleanLabel.isEmpty() ? dspAssetKindName(kind) : cleanLabel;
    record.radioIdentity = kind == DspAssetKind::Ps3Correction ? radioIdentity : QString();
    record.valid = true;

    const int existingIndex = recordIndex(record.id);
    if (existingIndex >= 0) {
        QString verifyError;
        if (m_records[existingIndex].kind != kind
            || !verifyRecord(m_records[existingIndex], nullptr, &verifyError)) {
            result.error = verifyError.isEmpty()
                ? QStringLiteral("Asset content ID already exists with another kind")
                : verifyError;
            return result;
        }
        if (m_records[existingIndex].radioIdentity != record.radioIdentity) {
            result.error = QStringLiteral(
                "Asset content ID already exists with different radio identity metadata");
            return result;
        }
        result.accepted = true;
        result.record = m_records[existingIndex];
        result.record.valid = true;
        return result;
    }

    const QString path = assetPath(record);
    bool publishedNewFile = false;
    if (QFileInfo::exists(path)) {
        const QFileInfo existingInfo(path);
        if (!existingInfo.isFile() || existingInfo.isSymLink()) {
            result.error = QStringLiteral("Generated DSP asset filename collision");
            return result;
        }
        QFile orphan(path);
        if (existingInfo.size() != record.size || existingInfo.size() > sizeLimit(kind)
            || !orphan.open(QIODevice::ReadOnly)
            || QString::fromLatin1(
                   QCryptographicHash::hash(orphan.read(sizeLimit(kind) + 1),
                                            QCryptographicHash::Sha256)
                       .toHex())
                != record.hashHex) {
            result.error = QStringLiteral("Generated DSP asset filename collision");
            return result;
        }
    } else {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            result.error = QStringLiteral("Could not stage DSP asset: %1").arg(file.errorString());
            return result;
        }
        if (file.write(bytes) != bytes.size() || !file.commit()) {
            file.cancelWriting();
            result.error = QStringLiteral("Could not publish DSP asset: %1").arg(file.errorString());
            return result;
        }
        publishedNewFile = true;
    }

    m_records.append(record);
    QString manifestError;
    if (!saveManifest(&manifestError)) {
        m_records.removeLast();
        if (publishedNewFile) {
            QFile::remove(path);
        }
        result.error = manifestError;
        return result;
    }

    result.accepted = true;
    result.record = record;
    return result;
}

QString DspAssetStore::beginImport(DspAssetKind kind, const QString& label,
                                   const QString& radioIdentity, QString* error)
{
    if (!m_valid) {
        return setError(error, m_lastError);
    }
    if (label.trimmed().size() > kMaxLabelCharacters
        || radioIdentity.size() > kMaxRadioIdentityCharacters) {
        return setError(error, QStringLiteral("DSP asset metadata exceeds its 256-character limit"));
    }
    if (kind == DspAssetKind::NnrModel && !radioIdentity.isEmpty()) {
        return setError(error, QStringLiteral("NNR model assets are station-scoped, not radio-scoped"));
    }

    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    PendingImport pending;
    pending.kind = kind;
    pending.label = label;
    pending.radioIdentity = radioIdentity;
    pending.path = QDir(m_stagingDirectory).filePath(token + QStringLiteral(".part"));
    QFile file(pending.path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        return setError(error, QStringLiteral("Could not create staged DSP import: %1")
                                   .arg(file.errorString()));
    }
    file.close();
    m_pending.insert(token, pending);
    if (error) {
        error->clear();
    }
    return token;
}

bool DspAssetStore::appendImport(const QString& token, const QByteArray& chunk, QString* error)
{
    auto it = m_pending.find(token);
    if (it == m_pending.end()) {
        setError(error, QStringLiteral("Unknown staged DSP import token"));
        return false;
    }
    if (chunk.size() > kTransferChunkBytes) {
        setError(error, QStringLiteral("Decoded DSP transfer chunks are limited to 64 KiB"));
        return false;
    }
    if (chunk.size() > sizeLimit(it->kind) - it->size) {
        const QString message = it->kind == DspAssetKind::NnrModel
            ? QStringLiteral("Staged NNR model exceeds the 64 MiB limit")
            : QStringLiteral("Staged PS3 correction exceeds the 1 MiB limit");
        cancelImport(token);
        setError(error, message);
        return false;
    }
    QFile file(it->path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append)
        || file.write(chunk) != chunk.size()) {
        const QString message = QStringLiteral("Could not append staged DSP import: %1")
                                    .arg(file.errorString());
        cancelImport(token);
        setError(error, message);
        return false;
    }
    it->size += chunk.size();
    if (error) {
        error->clear();
    }
    return true;
}

DspAssetImportResult DspAssetStore::finishImport(const QString& token)
{
    DspAssetImportResult result;
    auto it = m_pending.find(token);
    if (it == m_pending.end()) {
        result.error = QStringLiteral("Unknown staged DSP import token");
        return result;
    }
    const PendingImport pending = it.value();
    m_pending.erase(it);

    QFile file(pending.path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("Could not read staged DSP import: %1")
                           .arg(file.errorString());
        QFile::remove(pending.path);
        return result;
    }
    const QByteArray bytes = file.read(sizeLimit(pending.kind) + 1);
    file.close();
    QFile::remove(pending.path);
    if (bytes.size() != pending.size) {
        result.error = QStringLiteral("Staged DSP import size changed before publication");
        return result;
    }
    return importBytes(pending.kind, pending.label, bytes, pending.radioIdentity);
}

void DspAssetStore::cancelImport(const QString& token)
{
    const auto it = m_pending.find(token);
    if (it == m_pending.end()) {
        return;
    }
    const QString path = it->path;
    m_pending.erase(it);
    QFile::remove(path);
}

QString DspAssetStore::resolvePath(const QString& id, DspAssetKind expectedKind,
                                   const QString& radioIdentity, QString* error) const
{
    if (!isAssetId(id)) {
        return setError(error, QStringLiteral("Invalid DSP asset ID"));
    }
    const int index = recordIndex(id);
    if (index < 0) {
        return setError(error, QStringLiteral("DSP asset ID is not present in this station store"));
    }
    const DspAssetRecord& record = m_records[index];
    if (record.kind != expectedKind) {
        return setError(error, QStringLiteral("DSP asset kind does not match the requested operation"));
    }
    if (!record.radioIdentity.isEmpty() && record.radioIdentity != radioIdentity) {
        return setError(error, QStringLiteral("PS3 correction belongs to a different radio identity"));
    }
    QString verifyError;
    if (!verifyRecord(record, nullptr, &verifyError)) {
        return setError(error, verifyError);
    }
    if (error) {
        error->clear();
    }
    return assetPath(record);
}

bool DspAssetStore::exportAsset(const QString& id, QIODevice* destination, QString* error) const
{
    if (!destination || !destination->isWritable()) {
        setError(error, QStringLiteral("DSP asset export destination is not writable"));
        return false;
    }
    const int index = recordIndex(id);
    if (!isAssetId(id) || index < 0) {
        setError(error, QStringLiteral("Invalid or unknown DSP asset ID"));
        return false;
    }
    QByteArray bytes;
    if (!verifyRecord(m_records[index], &bytes, error)) {
        return false;
    }
    for (qsizetype offset = 0; offset < bytes.size(); offset += kTransferChunkBytes) {
        const qsizetype count = std::min(kTransferChunkBytes, bytes.size() - offset);
        if (destination->write(bytes.constData() + offset, count) != count) {
            setError(error, QStringLiteral("DSP asset export write failed"));
            return false;
        }
    }
    if (error) {
        error->clear();
    }
    return true;
}

} // namespace NereusSDR
