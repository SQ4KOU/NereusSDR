// no-port-check: tests for the NereusSDR-owned station DSP asset store.

#include <QtTest/QtTest>

#include "core/dsp/DspAssetStore.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

using namespace NereusSDR;

class TestDspAssetStore : public QObject
{
    Q_OBJECT

    QByteArray validCorrection() const
    {
        const QString path = QFINDTESTDATA("fixtures/dsp/ps3-v2-source-writer.txt");
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        return file.readAll();
    }

private slots:
    void importsListsResolvesExportsAndRestarts()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QString error;
        DspAssetStore store(root.path());
        QVERIFY2(store.isValid(), qPrintable(store.lastError()));

        const QByteArray bytes = validCorrection();
        const DspAssetImportResult imported = store.importBytes(
            DspAssetKind::Ps3Correction, QStringLiteral("20 metre bench"), bytes,
            QStringLiteral("radio:001122334455"));
        QVERIFY2(imported.accepted, qPrintable(imported.error));
        QVERIFY(imported.record.id.startsWith(QStringLiteral("sha256:")));
        QCOMPARE(imported.record.hashHex.size(), 64);
        QCOMPARE(imported.record.size, qint64(bytes.size()));

        const QList<DspAssetRecord> records = store.assets();
        QCOMPARE(records.size(), 1);
        QVERIFY(records.first().valid);

        const QString path = store.resolvePath(imported.record.id, DspAssetKind::Ps3Correction,
                                               QStringLiteral("radio:001122334455"), &error);
        QVERIFY2(!path.isEmpty(), qPrintable(error));
        QVERIFY(QFileInfo(path).fileName().size() < 64);
        QVERIFY(!path.contains(imported.record.label));

        QBuffer exported;
        QVERIFY(exported.open(QIODevice::WriteOnly));
        QVERIFY2(store.exportAsset(imported.record.id, &exported, &error), qPrintable(error));
        QCOMPARE(exported.data(), bytes);

        DspAssetStore restarted(root.path());
        QVERIFY2(restarted.isValid(), qPrintable(restarted.lastError()));
        QCOMPARE(restarted.assets().size(), 1);
        QCOMPARE(restarted.assets().first().id, imported.record.id);
    }

    void refusesIdentityMismatchTraversalAndTampering()
    {
        QTemporaryDir root;
        DspAssetStore store(root.path());
        const DspAssetImportResult imported = store.importBytes(
            DspAssetKind::Ps3Correction, QStringLiteral("fixture"), validCorrection(),
            QStringLiteral("radio:alpha"));
        QVERIFY(imported.accepted);

        QString error;
        QVERIFY(store.resolvePath(imported.record.id, DspAssetKind::Ps3Correction,
                                  QStringLiteral("radio:beta"), &error).isEmpty());
        QVERIFY(error.contains(QStringLiteral("radio"), Qt::CaseInsensitive));

        QVERIFY(store.resolvePath(QStringLiteral("../../etc/passwd"),
                                  DspAssetKind::Ps3Correction, {}, &error).isEmpty());
        QVERIFY(error.contains(QStringLiteral("asset ID"), Qt::CaseInsensitive));

        const QString path = store.resolvePath(imported.record.id, DspAssetKind::Ps3Correction,
                                               QStringLiteral("radio:alpha"), &error);
        QVERIFY(!path.isEmpty());
        QFile tamper(path);
        QVERIFY(tamper.open(QIODevice::Append));
        QCOMPARE(tamper.write("x", 1), qint64(1));
        tamper.close();
        QVERIFY(store.resolvePath(imported.record.id, DspAssetKind::Ps3Correction,
                                  QStringLiteral("radio:alpha"), &error).isEmpty());
        QVERIFY(error.contains(QStringLiteral("size"), Qt::CaseInsensitive));

        // A same-size mutation reaches the independent SHA-256 invariant.
        QVERIFY(tamper.open(QIODevice::ReadWrite));
        QVERIFY(tamper.resize(imported.record.size));
        QVERIFY(tamper.seek(imported.record.size / 2));
        const QByteArray original = tamper.read(1);
        QCOMPARE(original.size(), 1);
        QVERIFY(tamper.seek(imported.record.size / 2));
        const char changed = static_cast<char>(original.at(0) ^ 0x01);
        QCOMPARE(tamper.write(&changed, 1), qint64(1));
        tamper.close();
        QVERIFY(store.resolvePath(imported.record.id, DspAssetKind::Ps3Correction,
                                  QStringLiteral("radio:alpha"), &error).isEmpty());
        QVERIFY(error.contains(QStringLiteral("hash"), Qt::CaseInsensitive));
    }

    void failedImportLeavesExistingAssetAndNoTemporaryPublication()
    {
        QTemporaryDir root;
        DspAssetStore store(root.path());
        const DspAssetImportResult good = store.importBytes(
            DspAssetKind::Ps3Correction, QStringLiteral("good"), validCorrection());
        QVERIFY(good.accepted);

        const DspAssetImportResult bad = store.importBytes(
            DspAssetKind::Ps3Correction, QStringLiteral("bad"), QByteArray("not a correction"));
        QVERIFY(!bad.accepted);
        QCOMPARE(store.assets().size(), 1);

        const QDir assetsDir(root.path() + QStringLiteral("/dsp-assets/assets"));
        const QStringList residues = assetsDir.entryList(
            {QStringLiteral("*.tmp"), QStringLiteral(".*.tmp"), QStringLiteral("*.part")},
            QDir::Files | QDir::Hidden);
        QVERIFY2(residues.isEmpty(), qPrintable(residues.join(',')));
    }

    void stagedImportEnforcesChunkLimitAndCleansInterruption()
    {
        QTemporaryDir root;
        QString error;
        {
            DspAssetStore store(root.path());
            const QString token = store.beginImport(DspAssetKind::Ps3Correction,
                                                    QStringLiteral("staged"), {}, &error);
            QVERIFY2(!token.isEmpty(), qPrintable(error));
            QVERIFY(!store.appendImport(token, QByteArray(DspAssetStore::kTransferChunkBytes + 1, 'x'),
                                        &error));
            QVERIFY(error.contains(QStringLiteral("64 KiB"), Qt::CaseInsensitive));

            const QString interrupted = store.beginImport(DspAssetKind::Ps3Correction,
                                                          QStringLiteral("interrupted"), {}, &error);
            QVERIFY(!interrupted.isEmpty());
            QVERIFY(store.appendImport(interrupted, QByteArray("partial"), &error));
        }

        DspAssetStore restarted(root.path());
        QVERIFY(restarted.isValid());
        const QDir staging(root.path() + QStringLiteral("/dsp-assets/staging"));
        QCOMPARE(staging.entryList({QStringLiteral("*.part")}, QDir::Files | QDir::Hidden).size(), 0);
    }

    void validatesFixedWdspPathCapacitiesInEncodedBytes()
    {
        QString error;
        QVERIFY(DspAssetValidation::validateEncodedPath(QString(510, QLatin1Char('a')), 512,
                                                        &error));
        QVERIFY(!DspAssetValidation::validateEncodedPath(QString(512, QLatin1Char('a')), 512,
                                                         &error));
        QVERIFY(error.contains(QStringLiteral("terminator"), Qt::CaseInsensitive));

        // UTF-8 bytes, rather than UTF-16 code units, are the upstream C-buffer
        // boundary. U+00E9 encodes as two bytes.
        QVERIFY(!DspAssetValidation::validateEncodedPath(QString(128, QChar(0x00e9)), 256,
                                                         &error));
    }
};

QTEST_GUILESS_MAIN(TestDspAssetStore)
#include "tst_dsp_asset_store.moc"
