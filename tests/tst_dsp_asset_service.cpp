// SPDX-License-Identifier: GPL-2.0-or-later
// NereusSDR-original tests for the local/remote station DSP asset contract.

#include <QtTest/QtTest>

#include "core/AppSettings.h"
#include "core/dsp/DspAssetService.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>

extern "C" {
extern const unsigned char nnr_model_0_data[];
extern const unsigned int nnr_model_0_size;
}

using namespace NereusSDR;

namespace {

QByteArray standardModel()
{
    return QByteArray(reinterpret_cast<const char*>(nnr_model_0_data),
                      qsizetype(nnr_model_0_size));
}

QVariantMap beginArgs(const QByteArray& bytes, const QString& label = QStringLiteral("standard"))
{
    return {
        {QStringLiteral("kind"), 0},
        {QStringLiteral("label"), label},
        {QStringLiteral("size"), qint64(bytes.size())},
        {QStringLiteral("hash"), QString::fromLatin1(
             QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())},
        {QStringLiteral("radioIdentity"), QString()}
    };
}

DspAssetServiceResult upload(DspAssetService& service, const QByteArray& bytes,
                             const QString& owner)
{
    const auto begun = service.execute("dspAssets.beginImport", beginArgs(bytes), owner);
    if (!begun.accepted) return begun;
    const QString transferId = begun.values.value(QStringLiteral("transferId")).toString();
    for (qsizetype offset = 0; offset < bytes.size(); offset += DspAssetStore::kTransferChunkBytes) {
        const QByteArray chunk = bytes.mid(offset, DspAssetStore::kTransferChunkBytes);
        const auto appended = service.execute("dspAssets.chunk", {
            {QStringLiteral("transferId"), transferId},
            {QStringLiteral("offset"), qint64(offset)},
            {QStringLiteral("data"), QString::fromLatin1(chunk.toBase64())}
        }, owner);
        if (!appended.accepted) return appended;
    }
    return service.execute("dspAssets.finishImport",
                           {{QStringLiteral("transferId"), transferId}}, owner);
}

} // namespace

class TestDspAssetService final : public QObject
{
    Q_OBJECT

private slots:
    void defaultsAndQueuedLocalRequest();
    void customSelectionIsPendingUntilMarkedApplied();
    void savedMissingSelectionIsKeptWithBundledFallback();
    void commandsRequireExactKeysAndTypes();
    void transfersEnforceOwnerOffsetSizeHashAndCancellation();
    void exportReturnsBoundedCanonicalChunks();
    void remoteRequestsRetireAndNeverOpenLocalStore();
};

void TestDspAssetService::defaultsAndQueuedLocalRequest()
{
    QTemporaryDir directory;
    AppSettings settings(directory.filePath(QStringLiteral("station.settings")));
    DspAssetService service(settings, true);

    QCOMPARE(service.nnrStandardAsset(), QStringLiteral("bundled:0"));
    QCOMPARE(service.nnrPremiumAsset(), QStringLiteral("bundled:1"));
    QVERIFY(!service.nnrModelSelectionPending());
    QVERIFY(service.selectionRevision() != 0);
    QVERIFY(service.store());
    const auto paths = service.resolveNnrModelPaths();
    QVERIFY(paths[0].isEmpty());
    QVERIFY(paths[1].isEmpty());

    QSignalSpy completed(&service, &DspAssetService::requestCompleted);
    const quint32 requestId = service.request("dspAssets.list", {});
    QVERIFY(requestId != 0);
    QCOMPARE(completed.count(), 0); // completion is queued so callers can register it
    QTRY_COMPARE(completed.count(), 1);
    QCOMPARE(completed.first().at(0).toUInt(), requestId);
    QVERIFY(completed.first().at(1).toBool());
    const QVariantMap values = completed.first().at(3).toMap();
    QVERIFY(values.value(QStringLiteral("assets")).metaType().id() == QMetaType::QString);
    QVERIFY(!QJsonDocument::fromJson(values.value(QStringLiteral("assets")).toString().toUtf8())
                 .isNull());
}

void TestDspAssetService::customSelectionIsPendingUntilMarkedApplied()
{
    QTemporaryDir directory;
    AppSettings settings(directory.filePath(QStringLiteral("station.settings")));
    DspAssetService service(settings, true);
    const auto imported = upload(service, standardModel(), QStringLiteral("local-ui"));
    QVERIFY2(imported.accepted, qPrintable(imported.reason));
    const QString id = imported.values.value(QStringLiteral("id")).toString();
    const quint32 before = service.selectionRevision();

    const auto selected = service.execute("dspAssets.selectNnrModel", {
        {QStringLiteral("slot"), 0}, {QStringLiteral("id"), id}
    }, QStringLiteral("local-ui"));
    QVERIFY2(selected.accepted, qPrintable(selected.reason));
    QCOMPARE(service.nnrStandardAsset(), id);
    QVERIFY(service.nnrModelSelectionPending());
    QVERIFY(service.selectionRevision() > before);
    QCOMPARE(settings.value(QStringLiteral("DspAssets/NnrModel0")).toString(), id);

    QString reason;
    const auto paths = service.resolveNnrModelPaths(&reason);
    QVERIFY2(!paths[0].isEmpty(), qPrintable(reason));
    QVERIFY(QFileInfo::exists(paths[0]));
    service.markNnrModelsApplied();
    QVERIFY(!service.nnrModelSelectionPending());
    QCOMPARE(service.activeNnrModelAssets()[0], id);
}

void TestDspAssetService::savedMissingSelectionIsKeptWithBundledFallback()
{
    QTemporaryDir directory;
    AppSettings settings(directory.filePath(QStringLiteral("station.settings")));
    const QString missing = QStringLiteral("sha256:") + QString(64, QLatin1Char('a'));
    settings.setValue(QStringLiteral("DspAssets/NnrModel0"), missing);
    settings.setValue(QStringLiteral("DspAssets/SelectionRevision"), QStringLiteral("19"));

    DspAssetService service(settings, true);
    QCOMPARE(service.nnrStandardAsset(), missing);
    QCOMPARE(service.selectionRevision(), quint32(19));
    QString reason;
    const auto paths = service.resolveNnrModelPaths(&reason);
    QVERIFY(paths[0].isEmpty());
    QVERIFY(reason.contains(QStringLiteral("fallback"), Qt::CaseInsensitive));
    service.markNnrModelsApplied();
    QCOMPARE(service.activeNnrModelAssets()[0], QStringLiteral("bundled:0"));
    QVERIFY(service.nnrModelSelectionPending());
    QCOMPARE(settings.value(QStringLiteral("DspAssets/NnrModel0")).toString(), missing);
}

void TestDspAssetService::commandsRequireExactKeysAndTypes()
{
    QTemporaryDir directory;
    AppSettings settings(directory.filePath(QStringLiteral("station.settings")));
    DspAssetService service(settings, true);
    const QByteArray bytes = standardModel();

    QVariantMap extra = beginArgs(bytes);
    extra.insert(QStringLiteral("path"), QStringLiteral("/tmp/model"));
    QVERIFY(!service.execute("dspAssets.beginImport", extra, QStringLiteral("owner")).accepted);

    QVariantMap wrongSize = beginArgs(bytes);
    wrongSize.insert(QStringLiteral("size"), QString::number(bytes.size()));
    QVERIFY(!service.execute("dspAssets.beginImport", wrongSize, QStringLiteral("owner")).accepted);
    QVERIFY(!service.execute("dspAssets.selectNnrModel", {
        {QStringLiteral("slot"), QStringLiteral("0")},
        {QStringLiteral("id"), QStringLiteral("bundled:0")}
    }, QStringLiteral("owner")).accepted);
    QVERIFY(!service.execute("dspAssets.list", {{QStringLiteral("unused"), 1}},
                             QStringLiteral("owner")).accepted);
    QVERIFY(!service.execute("dspAssets.unknown", {}, QStringLiteral("owner")).accepted);
    QVERIFY(!service.execute("dspAssets.list", {}, {}).accepted);
}

void TestDspAssetService::transfersEnforceOwnerOffsetSizeHashAndCancellation()
{
    QTemporaryDir directory;
    AppSettings settings(directory.filePath(QStringLiteral("station.settings")));
    DspAssetService service(settings, true);
    const QByteArray bytes = standardModel();

    auto begun = service.execute("dspAssets.beginImport", beginArgs(bytes), QStringLiteral("alice"));
    QVERIFY(begun.accepted);
    const QString token = begun.values.value(QStringLiteral("transferId")).toString();
    const QString data = QString::fromLatin1(bytes.left(32).toBase64());
    QVERIFY(!service.execute("dspAssets.chunk", {
        {QStringLiteral("transferId"), token}, {QStringLiteral("offset"), qint64(0)},
        {QStringLiteral("data"), data}
    }, QStringLiteral("bob")).accepted);
    QVERIFY(!service.execute("dspAssets.chunk", {
        {QStringLiteral("transferId"), token}, {QStringLiteral("offset"), qint64(1)},
        {QStringLiteral("data"), data}
    }, QStringLiteral("alice")).accepted);
    QVERIFY(!service.execute("dspAssets.chunk", {
        {QStringLiteral("transferId"), token}, {QStringLiteral("offset"), qint64(0)},
        {QStringLiteral("data"), QStringLiteral("not base64")}
    }, QStringLiteral("alice")).accepted);
    QVERIFY(service.execute("dspAssets.chunk", {
        {QStringLiteral("transferId"), token}, {QStringLiteral("offset"), qint64(0)},
        {QStringLiteral("data"), data}
    }, QStringLiteral("alice")).accepted);
    QVERIFY(!service.execute("dspAssets.finishImport",
        {{QStringLiteral("transferId"), token}}, QStringLiteral("alice")).accepted);
    QVERIFY(!service.execute("dspAssets.cancelImport",
        {{QStringLiteral("transferId"), token}}, QStringLiteral("alice")).accepted);

    QVariantMap wrongHash = beginArgs(bytes);
    wrongHash.insert(QStringLiteral("hash"), QString(64, QLatin1Char('0')));
    begun = service.execute("dspAssets.beginImport", wrongHash, QStringLiteral("alice"));
    QVERIFY(begun.accepted);
    const QString wrongToken = begun.values.value(QStringLiteral("transferId")).toString();
    for (qsizetype offset = 0; offset < bytes.size(); offset += DspAssetStore::kTransferChunkBytes) {
        const QByteArray chunk = bytes.mid(offset, DspAssetStore::kTransferChunkBytes);
        QVERIFY(service.execute("dspAssets.chunk", {
            {QStringLiteral("transferId"), wrongToken}, {QStringLiteral("offset"), qint64(offset)},
            {QStringLiteral("data"), QString::fromLatin1(chunk.toBase64())}
        }, QStringLiteral("alice")).accepted);
    }
    QVERIFY(!service.execute("dspAssets.finishImport",
        {{QStringLiteral("transferId"), wrongToken}}, QStringLiteral("alice")).accepted);

    const auto first = service.execute("dspAssets.beginImport",
                                       beginArgs(bytes, QStringLiteral("one")),
                                       QStringLiteral("alice"));
    const auto second = service.execute("dspAssets.beginImport",
                                        beginArgs(bytes, QStringLiteral("two")),
                                        QStringLiteral("alice"));
    QVERIFY(first.accepted);
    QVERIFY(second.accepted);
    QVERIFY(!service.execute("dspAssets.beginImport",
                             beginArgs(bytes, QStringLiteral("three")),
                             QStringLiteral("alice")).accepted);
    service.cancelOwner(QStringLiteral("alice"));
    QVERIFY(!service.execute("dspAssets.cancelImport",
        {{QStringLiteral("transferId"),
          first.values.value(QStringLiteral("transferId")).toString()}},
        QStringLiteral("alice")).accepted);
}

void TestDspAssetService::exportReturnsBoundedCanonicalChunks()
{
    QTemporaryDir directory;
    AppSettings settings(directory.filePath(QStringLiteral("station.settings")));
    DspAssetService service(settings, true);
    const QByteArray bytes = standardModel();
    const auto imported = upload(service, bytes, QStringLiteral("alice"));
    QVERIFY2(imported.accepted, qPrintable(imported.reason));
    const QString id = imported.values.value(QStringLiteral("id")).toString();

    QByteArray exported;
    qint64 offset = 0;
    bool eof = false;
    while (!eof) {
        const auto result = service.execute("dspAssets.export", {
            {QStringLiteral("id"), id}, {QStringLiteral("offset"), offset}
        }, QStringLiteral("alice"));
        QVERIFY2(result.accepted, qPrintable(result.reason));
        const QByteArray encoded = result.values.value(QStringLiteral("data")).toString().toLatin1();
        const QByteArray decoded = QByteArray::fromBase64(encoded);
        QVERIFY(decoded.size() <= DspAssetStore::kTransferChunkBytes);
        QCOMPARE(decoded.toBase64(), encoded);
        exported.append(decoded);
        offset += decoded.size();
        eof = result.values.value(QStringLiteral("eof")).toBool();
    }
    QCOMPARE(exported, bytes);
    QVERIFY(!service.execute("dspAssets.export", {
        {QStringLiteral("id"), id}, {QStringLiteral("offset"), qint64(bytes.size() + 1)}
    }, QStringLiteral("alice")).accepted);
}

void TestDspAssetService::remoteRequestsRetireAndNeverOpenLocalStore()
{
    QTemporaryDir directory;
    const QString settingsPath = directory.filePath(QStringLiteral("client/settings.xml"));
    AppSettings settings(settingsPath);
    DspAssetService service(settings, false);
    QVERIFY(!service.store());
    quint32 wireId = 41;
    service.setRemoteRequestHandler([&](const QByteArray&, const QVariantMap&) {
        return ++wireId;
    });
    QSignalSpy completed(&service, &DspAssetService::requestCompleted);

    const quint32 id = service.request("dspAssets.list", {});
    QCOMPARE(id, quint32(42));
    QVERIFY(!service.receiveRemoteResult(id + 1, "dspAssets.list", true, {}, {}));
    QVERIFY(!service.receiveRemoteResult(id, "dspAssets.export", true, {}, {}));
    QVERIFY(service.receiveRemoteResult(id, "dspAssets.list", true, {},
                                        {{QStringLiteral("assets"), QStringLiteral("[]")}}));
    QCOMPARE(completed.count(), 1);
    QVERIFY(!service.receiveRemoteResult(id, "dspAssets.list", true, {}, {}));

    const quint32 retired = service.request("dspAssets.list", {});
    service.resetSession();
    QVERIFY(!service.receiveRemoteResult(retired, "dspAssets.list", true, {}, {}));
    QVERIFY(service.applyRemoteProperty("nnrStandardAsset", QStringLiteral("sha256:remote")));
    QVERIFY(service.applyRemoteProperty("nnrModelSelectionPending", true));
    QCOMPARE(service.nnrStandardAsset(), QStringLiteral("sha256:remote"));
    QVERIFY(service.nnrModelSelectionPending());
    QVERIFY(!service.applyRemoteProperty("selectionRevision", QStringLiteral("7")));
    QVERIFY(!QFileInfo::exists(QFileInfo(settingsPath).absolutePath()
                               + QStringLiteral("/dsp-assets")));
    QVERIFY(!settings.contains(QStringLiteral("DspAssets/NnrModel0")));
}

QTEST_GUILESS_MAIN(TestDspAssetService)
#include "tst_dsp_asset_service.moc"
