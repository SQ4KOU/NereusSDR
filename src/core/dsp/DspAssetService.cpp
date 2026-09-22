// SPDX-License-Identifier: GPL-2.0-or-later
// NereusSDR-original local/remote station DSP asset contract.

#include "DspAssetService.h"

#include "core/AppSettings.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QRegularExpression>
#include <QTimer>

#include <algorithm>
#include <limits>
#include <utility>

namespace NereusSDR {

namespace {

constexpr auto kSelection0 = "DspAssets/NnrModel0";
constexpr auto kSelection1 = "DspAssets/NnrModel1";
constexpr auto kRevision = "DspAssets/SelectionRevision";
constexpr int kMaxRecords = 128;
constexpr int kMaxPendingRequests = 128;
constexpr int kMaxImportsPerOwner = 2;
constexpr int kMaxImportsTotal = 4;

QString bundledId(int slot)
{
    return QStringLiteral("bundled:%1").arg(slot);
}

bool hasOnlyKeys(const QVariantMap& args, std::initializer_list<const char*> keys)
{
    if (args.size() != static_cast<qsizetype>(keys.size())) return false;
    for (const char* key : keys) {
        if (!args.contains(QString::fromLatin1(key))) return false;
    }
    return true;
}

bool exactString(const QVariant& value, QString* result)
{
    if (value.metaType().id() != QMetaType::QString) return false;
    if (result) *result = value.toString();
    return true;
}

bool exactBool(const QVariant& value, bool* result)
{
    if (value.metaType().id() != QMetaType::Bool) return false;
    if (result) *result = value.toBool();
    return true;
}

bool exactInteger(const QVariant& value, qint64* result)
{
    qint64 converted = 0;
    switch (value.metaType().id()) {
    case QMetaType::Int: converted = value.toInt(); break;
    case QMetaType::UInt: converted = value.toUInt(); break;
    case QMetaType::LongLong: converted = value.toLongLong(); break;
    case QMetaType::ULongLong: {
        const qulonglong input = value.toULongLong();
        if (input > static_cast<qulonglong>(std::numeric_limits<qint64>::max())) return false;
        converted = static_cast<qint64>(input);
        break;
    }
    default: return false;
    }
    if (result) *result = converted;
    return true;
}

qint64 kindLimit(DspAssetKind kind)
{
    return kind == DspAssetKind::NnrModel ? DspAssetValidation::kMaxNnrModelBytes
                                          : DspAssetValidation::kMaxPs3CorrectionBytes;
}

QJsonObject recordJson(const DspAssetRecord& record)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), record.id);
    object.insert(QStringLiteral("kind"), static_cast<int>(record.kind));
    object.insert(QStringLiteral("hash"), record.hashHex);
    object.insert(QStringLiteral("size"), double(record.size));
    object.insert(QStringLiteral("format"), record.format);
    object.insert(QStringLiteral("version"), record.version);
    object.insert(QStringLiteral("compatibility"), record.compatibility);
    object.insert(QStringLiteral("numericEncoding"), record.numericEncoding);
    object.insert(QStringLiteral("label"), record.label);
    object.insert(QStringLiteral("radioIdentity"), record.radioIdentity);
    object.insert(QStringLiteral("valid"), record.valid);
    object.insert(QStringLiteral("validationError"), record.validationError);
    return object;
}

QString compactJson(const QJsonValue& value)
{
    if (value.isArray()) return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
}

} // namespace

struct DspAssetService::ActiveImport {
    QString owner;
    QString storeToken;
    DspAssetKind kind{DspAssetKind::NnrModel};
    qint64 expectedSize{0};
    qint64 received{0};
    QString expectedHash;
    QCryptographicHash hash{QCryptographicHash::Sha256};
};

DspAssetService::DspAssetService(AppSettings& settings, bool local, QObject* parent)
    : QObject(parent), m_settings(settings), m_local(local)
{
    m_selected = {bundledId(0), bundledId(1)};
    m_active = m_selected;
    m_lastResolved = m_active;
    if (m_local) {
        m_store = std::make_unique<DspAssetStore>(QFileInfo(settings.filePath()).absolutePath());
        const QString saved0 = settings.value(QString::fromLatin1(kSelection0), bundledId(0)).toString();
        const QString saved1 = settings.value(QString::fromLatin1(kSelection1), bundledId(1)).toString();
        if (!saved0.isEmpty()) m_selected[0] = saved0;
        if (!saved1.isEmpty()) m_selected[1] = saved1;
        bool revisionOk = false;
        const qulonglong savedRevision = settings.value(QString::fromLatin1(kRevision),
                                                         QStringLiteral("1"))
                                                .toString().toULongLong(&revisionOk);
        m_revision = revisionOk && savedRevision > 0
                         ? static_cast<quint32>(std::min<qulonglong>(savedRevision,
                               std::numeric_limits<quint32>::max()))
                         : 1;
        settings.setValue(QString::fromLatin1(kRevision), QString::number(m_revision));
    }
    refreshSelectionStatus();
}

DspAssetService::~DspAssetService()
{
    resetSession();
}

bool DspAssetService::nnrModelSelectionPending() const
{
    return m_local ? m_selected != m_active : m_remotePending;
}

void DspAssetService::setRadioIdentity(const QString& mac)
{
    if (!m_local) return;
    m_radioIdentity = AppSettings::normalizedRadioMac(mac);
}

DspAssetServiceResult DspAssetService::reject(const QString& reason) const
{
    return {false, reason, {}};
}

DspAssetServiceResult DspAssetService::execute(const QByteArray& verb,
                                                const QVariantMap& args,
                                                const QString& owner)
{
    if (!m_local || !m_store) return reject(QStringLiteral("DSP asset actions are station-local."));
    if (owner.isEmpty()) return reject(QStringLiteral("DSP asset action owner is required."));

    if (verb == "dspAssets.list") {
        if (!args.isEmpty()) return reject(QStringLiteral("dspAssets.list accepts no arguments."));
        QJsonArray assets;
        const QList<DspAssetRecord> records = m_store->assets();
        const qsizetype recordCount = std::min(records.size(), qsizetype(kMaxRecords));
        for (qsizetype i = 0; i < recordCount; ++i)
            assets.append(recordJson(records.at(i)));
        QVariantMap values;
        values.insert(QStringLiteral("assets"), compactJson(assets));
        values.insert(QStringLiteral("selection0"), m_selected[0]);
        values.insert(QStringLiteral("selection1"), m_selected[1]);
        values.insert(QStringLiteral("revision"), m_revision);
        values.insert(QStringLiteral("pending"), nnrModelSelectionPending());
        values.insert(QStringLiteral("status"), m_status);
        return {true, {}, values};
    }

    if (verb == "dspAssets.beginImport") {
        if (!hasOnlyKeys(args, {"kind", "label", "size", "hash", "radioIdentity"}))
            return reject(QStringLiteral("dspAssets.beginImport has invalid or missing fields."));
        qint64 kindValue = -1;
        qint64 size = 0;
        QString label, hash, radioIdentity;
        if (!exactInteger(args.value(QStringLiteral("kind")), &kindValue)
            || (kindValue != 0 && kindValue != 1)
            || !exactString(args.value(QStringLiteral("label")), &label)
            || label.size() > 128
            || !exactInteger(args.value(QStringLiteral("size")), &size)
            || !exactString(args.value(QStringLiteral("hash")), &hash)
            || !exactString(args.value(QStringLiteral("radioIdentity")), &radioIdentity)) {
            return reject(QStringLiteral("dspAssets.beginImport field types or ranges are invalid."));
        }
        const DspAssetKind kind = kindValue == 0 ? DspAssetKind::NnrModel
                                                  : DspAssetKind::Ps3Correction;
        if (size <= 0 || size > kindLimit(kind))
            return reject(QStringLiteral("Advertised DSP asset size is outside the format limit."));
        static const QRegularExpression hex64(QStringLiteral("^[0-9A-Fa-f]{64}$"));
        if (!hex64.match(hash).hasMatch())
            return reject(QStringLiteral("Advertised DSP asset hash must be 64 hexadecimal characters."));
        const QString normalizedRadio = AppSettings::normalizedRadioMac(radioIdentity);
        if (kind == DspAssetKind::NnrModel && !radioIdentity.isEmpty())
            return reject(QStringLiteral("NNR model imports cannot carry a radio identity."));
        if (kind == DspAssetKind::Ps3Correction
            && (normalizedRadio.isEmpty() || m_radioIdentity.isEmpty()
                || normalizedRadio != m_radioIdentity)) {
            return reject(QStringLiteral("PS3 correction radio identity does not match the current radio."));
        }
        if (m_imports.size() >= kMaxImportsTotal)
            return reject(QStringLiteral("At most four DSP asset imports may be active."));
        int owned = 0;
        for (const auto& transfer : std::as_const(m_imports))
            if (transfer->owner == owner) ++owned;
        if (owned >= kMaxImportsPerOwner)
            return reject(QStringLiteral("At most two DSP asset imports may be active for one owner."));
        QString error;
        const QString storeToken = m_store->beginImport(kind, label, normalizedRadio, &error);
        if (storeToken.isEmpty()) return reject(error);
        auto transfer = std::make_shared<ActiveImport>();
        transfer->owner = owner;
        transfer->storeToken = storeToken;
        transfer->kind = kind;
        transfer->expectedSize = size;
        transfer->expectedHash = hash.toLower();
        m_imports.insert(storeToken, transfer);
        return {true, {}, {{QStringLiteral("transferId"), storeToken}}};
    }

    if (verb == "dspAssets.chunk") {
        if (!hasOnlyKeys(args, {"transferId", "offset", "data"}))
            return reject(QStringLiteral("dspAssets.chunk has invalid or missing fields."));
        QString transferId, encoded;
        qint64 offset = -1;
        if (!exactString(args.value(QStringLiteral("transferId")), &transferId)
            || transferId.size() > 128
            || !exactInteger(args.value(QStringLiteral("offset")), &offset) || offset < 0
            || !exactString(args.value(QStringLiteral("data")), &encoded)
            || encoded.size() > 4 * ((DspAssetStore::kTransferChunkBytes + 2) / 3)) {
            return reject(QStringLiteral("dspAssets.chunk field types or ranges are invalid."));
        }
        const auto transfer = m_imports.value(transferId);
        if (!transfer || transfer->owner != owner)
            return reject(QStringLiteral("DSP import transfer is missing, retired, or belongs to another owner."));
        if (offset != transfer->received)
            return reject(QStringLiteral("DSP import chunk offset is not the next expected offset."));
        const QByteArray ascii = encoded.toLatin1();
        if (QString::fromLatin1(ascii) != encoded)
            return reject(QStringLiteral("DSP import chunk data is not canonical bounded base64."));
        const auto decodedResult = QByteArray::fromBase64Encoding(
            ascii, QByteArray::AbortOnBase64DecodingErrors);
        if (!decodedResult || decodedResult.decoded.size() > DspAssetStore::kTransferChunkBytes
            || decodedResult.decoded.toBase64() != ascii) {
            return reject(QStringLiteral("DSP import chunk data is not canonical bounded base64."));
        }
        if (decodedResult.decoded.size() > transfer->expectedSize - transfer->received) {
            m_store->cancelImport(transfer->storeToken);
            m_imports.remove(transferId);
            return reject(QStringLiteral("DSP import exceeds its advertised size."));
        }
        QString error;
        if (!m_store->appendImport(transfer->storeToken, decodedResult.decoded, &error)) {
            m_imports.remove(transferId);
            return reject(error);
        }
        transfer->hash.addData(decodedResult.decoded);
        transfer->received += decodedResult.decoded.size();
        return {true, {}, {{QStringLiteral("offset"), transfer->received}}};
    }

    if (verb == "dspAssets.finishImport") {
        if (!hasOnlyKeys(args, {"transferId"}))
            return reject(QStringLiteral("dspAssets.finishImport has invalid or missing fields."));
        QString transferId;
        if (!exactString(args.value(QStringLiteral("transferId")), &transferId)
            || transferId.size() > 128)
            return reject(QStringLiteral("DSP import transfer ID is invalid."));
        const auto transfer = m_imports.value(transferId);
        if (!transfer || transfer->owner != owner)
            return reject(QStringLiteral("DSP import transfer is missing, retired, or belongs to another owner."));
        m_imports.remove(transferId);
        if (transfer->received != transfer->expectedSize
            || QString::fromLatin1(transfer->hash.result().toHex()) != transfer->expectedHash) {
            m_store->cancelImport(transfer->storeToken);
            return reject(QStringLiteral("DSP import size or SHA-256 does not match its advertisement."));
        }
        const DspAssetImportResult imported = m_store->finishImport(transfer->storeToken);
        if (!imported.accepted) return reject(imported.error);
        QVariantMap values;
        values.insert(QStringLiteral("id"), imported.record.id);
        values.insert(QStringLiteral("kind"), static_cast<int>(imported.record.kind));
        values.insert(QStringLiteral("hash"), imported.record.hashHex);
        values.insert(QStringLiteral("size"), imported.record.size);
        values.insert(QStringLiteral("format"), imported.record.format);
        values.insert(QStringLiteral("version"), imported.record.version);
        values.insert(QStringLiteral("compatibility"), imported.record.compatibility);
        values.insert(QStringLiteral("numericEncoding"), imported.record.numericEncoding);
        values.insert(QStringLiteral("label"), imported.record.label);
        values.insert(QStringLiteral("radioIdentity"), imported.record.radioIdentity);
        return {true, {}, values};
    }

    if (verb == "dspAssets.cancelImport") {
        if (!hasOnlyKeys(args, {"transferId"}))
            return reject(QStringLiteral("dspAssets.cancelImport has invalid or missing fields."));
        QString transferId;
        if (!exactString(args.value(QStringLiteral("transferId")), &transferId)
            || transferId.size() > 128)
            return reject(QStringLiteral("DSP import transfer ID is invalid."));
        const auto transfer = m_imports.value(transferId);
        if (!transfer || transfer->owner != owner)
            return reject(QStringLiteral("DSP import transfer is missing, retired, or belongs to another owner."));
        m_store->cancelImport(transfer->storeToken);
        m_imports.remove(transferId);
        return {true, {}, {}};
    }

    if (verb == "dspAssets.export") {
        if (!hasOnlyKeys(args, {"id", "offset"}))
            return reject(QStringLiteral("dspAssets.export has invalid or missing fields."));
        QString id;
        qint64 offset = -1;
        if (!exactString(args.value(QStringLiteral("id")), &id) || id.size() > 128
            || !exactInteger(args.value(QStringLiteral("offset")), &offset) || offset < 0)
            return reject(QStringLiteral("DSP asset export fields are invalid."));
        DspAssetRecord record;
        bool found = false;
        for (const auto& candidate : m_store->assets()) {
            if (candidate.id == id) { record = candidate; found = true; break; }
        }
        if (!found) return reject(QStringLiteral("DSP asset ID is not present in this station store."));
        QString error;
        const QString path = m_store->resolvePath(id, record.kind,
            record.kind == DspAssetKind::Ps3Correction ? m_radioIdentity : QString(), &error);
        if (path.isEmpty()) return reject(error);
        if (offset > record.size) return reject(QStringLiteral("DSP asset export offset exceeds its size."));
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || !file.seek(offset))
            return reject(QStringLiteral("Could not read the validated DSP asset."));
        const QByteArray chunk = file.read(DspAssetStore::kTransferChunkBytes);
        QVariantMap values;
        values.insert(QStringLiteral("data"), QString::fromLatin1(chunk.toBase64()));
        values.insert(QStringLiteral("offset"), offset);
        values.insert(QStringLiteral("size"), record.size);
        values.insert(QStringLiteral("hash"), record.hashHex);
        values.insert(QStringLiteral("eof"), offset + chunk.size() >= record.size);
        return {true, {}, values};
    }

    if (verb == "dspAssets.selectNnrModel") {
        if (!hasOnlyKeys(args, {"slot", "id"}))
            return reject(QStringLiteral("dspAssets.selectNnrModel has invalid or missing fields."));
        qint64 slot = -1;
        QString id;
        if (!exactInteger(args.value(QStringLiteral("slot")), &slot) || slot < 0 || slot > 1
            || !exactString(args.value(QStringLiteral("id")), &id) || id.size() > 128)
            return reject(QStringLiteral("NNR model selection fields are invalid."));
        QString error;
        if (!setSelection(static_cast<int>(slot), id, &error)) return reject(error);
        return {true, {}, {{QStringLiteral("revision"), m_revision},
                           {QStringLiteral("status"), m_status}}};
    }

    return reject(QStringLiteral("Unknown DSP asset action."));
}

quint32 DspAssetService::allocateRequestId()
{
    for (int tries = 0; tries <= kMaxPendingRequests; ++tries) {
        quint32 candidate = m_nextRequestId++;
        if (candidate == 0) candidate = m_nextRequestId++;
        if (!m_pendingRequests.contains(candidate)) return candidate;
    }
    return 0;
}

quint32 DspAssetService::request(const QByteArray& verb, const QVariantMap& args)
{
    if (m_pendingRequests.size() >= kMaxPendingRequests) return 0;
    if (!m_local) {
        if (!m_remoteRequest) return 0;
        const quint32 id = m_remoteRequest(verb, args);
        if (id == 0 || m_pendingRequests.contains(id)) return 0;
        m_pendingRequests.insert(id, {verb});
        return id;
    }
    const quint32 id = allocateRequestId();
    if (id == 0) return 0;
    m_pendingRequests.insert(id, {verb});
    QTimer::singleShot(0, this, [this, id, verb, args] {
        const auto it = m_pendingRequests.find(id);
        if (it == m_pendingRequests.end() || it->verb != verb) return;
        m_pendingRequests.erase(it);
        const DspAssetServiceResult result = execute(verb, args, QStringLiteral("local"));
        emit requestCompleted(id, result.accepted, result.reason, result.values);
    });
    return id;
}

void DspAssetService::setRemoteRequestHandler(RemoteRequestHandler handler)
{
    if (!m_local) m_remoteRequest = std::move(handler);
}

bool DspAssetService::receiveRemoteResult(quint32 id, const QByteArray& verb,
                                          bool accepted, const QString& reason,
                                          const QVariantMap& values)
{
    if (m_local) return false;
    const auto it = m_pendingRequests.find(id);
    if (it == m_pendingRequests.end() || it->verb != verb) return false;
    m_pendingRequests.erase(it);
    emit requestCompleted(id, accepted, reason, values);
    return true;
}

void DspAssetService::resetSession()
{
    if (m_store) {
        for (const auto& transfer : std::as_const(m_imports))
            m_store->cancelImport(transfer->storeToken);
    }
    m_imports.clear();
    m_pendingRequests.clear();
}

void DspAssetService::cancelOwner(const QString& owner)
{
    if (!m_store) return;
    QList<QString> cancelled;
    for (auto it = m_imports.cbegin(); it != m_imports.cend(); ++it) {
        if (it.value()->owner == owner) {
            m_store->cancelImport(it.value()->storeToken);
            cancelled.append(it.key());
        }
    }
    for (const QString& token : cancelled) m_imports.remove(token);
}

void DspAssetService::refreshSelectionStatus()
{
    QStringList problems;
    if (m_local && m_store) {
        for (int slot = 0; slot < 2; ++slot) {
            if (m_selected[slot] == bundledId(slot)) continue;
            QString error;
            if (m_store->resolvePath(m_selected[slot], DspAssetKind::NnrModel, {}, &error).isEmpty())
                problems.append(tr("Model %1 selection is missing or invalid; bundled fallback will be used.")
                                    .arg(slot));
        }
    }
    m_status = problems.isEmpty() ? tr("NNR model selections are available.")
                                  : problems.join(QLatin1Char(' '));
}

bool DspAssetService::setSelection(int slot, const QString& id, QString* reason)
{
    if (id != bundledId(slot)) {
        QString error;
        if (!m_store || m_store->resolvePath(id, DspAssetKind::NnrModel, {}, &error).isEmpty()) {
            if (reason) *reason = error.isEmpty() ? QStringLiteral("Selected NNR model asset is unavailable.") : error;
            return false;
        }
    }
    if (m_selected[slot] == id) {
        if (reason) reason->clear();
        return true;
    }
    m_selected[slot] = id;
    ++m_revision;
    if (m_revision == 0) ++m_revision;
    m_settings.setValue(slot == 0 ? QString::fromLatin1(kSelection0)
                                  : QString::fromLatin1(kSelection1), id);
    m_settings.setValue(QString::fromLatin1(kRevision), QString::number(m_revision));
    refreshSelectionStatus();
    emit selectionChanged();
    emit configurationChanged();
    if (reason) reason->clear();
    return true;
}

std::array<QString, 2> DspAssetService::resolveNnrModelPaths(QString* reason)
{
    std::array<QString, 2> paths;
    std::array<QString, 2> resolved{bundledId(0), bundledId(1)};
    QStringList problems;
    if (m_local && m_store) {
        for (int slot = 0; slot < 2; ++slot) {
            if (m_selected[slot] == bundledId(slot)) continue;
            QString error;
            const QString path = m_store->resolvePath(m_selected[slot], DspAssetKind::NnrModel,
                                                      {}, &error);
            if (path.isEmpty()) {
                problems.append(tr("Model %1 selection is missing or invalid; bundled fallback will be used.")
                                    .arg(slot));
            } else {
                paths[slot] = path;
                resolved[slot] = m_selected[slot];
            }
        }
    }
    m_lastResolved = resolved;
    const QString newStatus = problems.isEmpty() ? tr("NNR model selections are available.")
                                                  : problems.join(QLatin1Char(' '));
    if (m_status != newStatus) {
        m_status = newStatus;
        emit selectionChanged();
    }
    if (reason) *reason = problems.join(QLatin1Char(' '));
    return paths;
}

void DspAssetService::markNnrModelsApplied()
{
    resolveNnrModelPaths();
    if (m_active == m_lastResolved) return;
    m_active = m_lastResolved;
    emit selectionChanged();
}

bool DspAssetService::applyRemoteProperty(const QByteArray& name, const QVariant& value)
{
    if (m_local) return false;
    bool changed = false;
    if (name == "nnrStandardAsset" || name == "nnrPremiumAsset") {
        QString text;
        if (!exactString(value, &text)) return false;
        const int slot = name == "nnrStandardAsset" ? 0 : 1;
        if (m_selected[slot] != text) { m_selected[slot] = text; changed = true; }
    } else if (name == "nnrModelSelectionPending") {
        bool pending = false;
        if (!exactBool(value, &pending)) return false;
        if (m_remotePending != pending) { m_remotePending = pending; changed = true; }
    } else if (name == "nnrModelStatus") {
        QString text;
        if (!exactString(value, &text)) return false;
        if (m_status != text) { m_status = text; changed = true; }
    } else if (name == "selectionRevision") {
        qint64 revision = 0;
        if (!exactInteger(value, &revision) || revision <= 0
            || revision > std::numeric_limits<quint32>::max()) return false;
        if (m_revision != static_cast<quint32>(revision)) {
            m_revision = static_cast<quint32>(revision);
            changed = true;
        }
    } else {
        return false;
    }
    if (changed) emit selectionChanged();
    return true;
}

} // namespace NereusSDR
