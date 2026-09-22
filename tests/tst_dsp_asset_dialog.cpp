// SPDX-License-Identifier: GPL-2.0-or-later
// NereusSDR-original tests for the bounded station DSP asset manager.

#include <QtTest/QtTest>

#include "core/AppSettings.h"
#include "core/dsp/DspAssetService.h"
#include "core/session/PureSignalSessionFacade.h"
#include "models/RadioModel.h"
#include "gui/DspAssetDialog.h"

#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QPixmap>
#include <QSignalSpy>
#include <QTableWidget>
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

QString writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return {};
    file.close();
    return path;
}

DspAssetServiceResult upload(DspAssetService& service, DspAssetKind kind,
                             const QByteArray& bytes, const QString& label,
                             const QString& radioIdentity = {})
{
    const QString owner = QStringLiteral("asset-dialog-fixture");
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    auto result = service.execute("dspAssets.beginImport",
                                  {{QStringLiteral("kind"), static_cast<int>(kind)},
                                   {QStringLiteral("label"), label},
                                   {QStringLiteral("size"), qint64(bytes.size())},
                                   {QStringLiteral("hash"), hash},
                                   {QStringLiteral("radioIdentity"), radioIdentity}},
                                  owner);
    if (!result.accepted) return result;
    const QString token = result.values.value(QStringLiteral("transferId")).toString();
    for (qsizetype offset = 0; offset < bytes.size();
         offset += DspAssetStore::kTransferChunkBytes) {
        const QByteArray chunk = bytes.mid(offset, DspAssetStore::kTransferChunkBytes);
        result = service.execute("dspAssets.chunk",
                                 {{QStringLiteral("transferId"), token},
                                  {QStringLiteral("offset"), qint64(offset)},
                                  {QStringLiteral("data"),
                                   QString::fromLatin1(chunk.toBase64())}},
                                 owner);
        if (!result.accepted) return result;
    }
    return service.execute("dspAssets.finishImport",
                           {{QStringLiteral("transferId"), token}}, owner);
}

bool hasSuccessfulSignal(const QSignalSpy& spy, int firstIndex)
{
    for (int index = firstIndex; index < spy.size(); ++index)
        if (spy.at(index).at(0).toBool()) return true;
    return false;
}

bool captureIfRequested(QWidget& widget, const QString& fileName)
{
    const QString directory = qEnvironmentVariable("NEREUS_DSP_UI_CAPTURE_DIR");
    if (directory.isEmpty()) {
        return true;
    }
    if (!QDir().mkpath(directory)) {
        return false;
    }
    widget.resize(qMax(widget.width(), 760), qMax(widget.height(), 520));
    widget.show();
    QApplication::processEvents();
    return widget.grab().save(QDir(directory).filePath(fileName), "PNG");
}

} // namespace

class TestDspAssetDialog final : public QObject
{
    Q_OBJECT

private slots:
    void importAndVerifiedExportUseBoundedServiceRequests();
    void rejectedBadFileIsNotPublished();
    void missingDesiredSelectionRemainsVisibleAndPending();
    void refusedSelectionRestoresCoreState();
    void importingDoesNotApplyOrReconnect();
    void restoreEmitsOnlySelectedCorrectionIdentity();
    void geometryAndCollapsedDetailsRoundTrip();
};

void TestDspAssetDialog::importAndVerifiedExportUseBoundedServiceRequests()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    AppSettings settings(directory.filePath(QStringLiteral("station.settings")));
    DspAssetService service(settings, true);
    DspAssetDialog dialog(nullptr, &service, DspAssetKind::NnrModel);
    QSignalSpy operations(&dialog, &DspAssetDialog::operationFinished);
    auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("dspAssetTable"));
    QVERIFY(table);
    QTRY_VERIFY(dialog.findChild<QPushButton*>(QStringLiteral("dspAssetImportButton"))
                    ->isEnabled());

    const QByteArray bytes = standardModel();
    const QString source = writeFile(directory.filePath(QStringLiteral("model.bin")), bytes);
    QVERIFY(!source.isEmpty());
    const int beforeImport = operations.size();
    QVERIFY(dialog.importFile(source, QStringLiteral("Station model")));
    QTRY_VERIFY_WITH_TIMEOUT(hasSuccessfulSignal(operations, beforeImport), 15000);
    QTRY_COMPARE_WITH_TIMEOUT(table->rowCount(), 1, 15000);
    const QString id = dialog.selectedAssetId();
    QVERIFY(id.startsWith(QStringLiteral("sha256:")));
    QVERIFY2(captureIfRequested(dialog, QStringLiteral("dsp-assets-nnr.png")),
             "Could not save opt-in NNR asset manager capture");

    const QString exported = directory.filePath(QStringLiteral("roundtrip.bin"));
    const int beforeExport = operations.size();
    QVERIFY(dialog.exportAssetToFile(id, exported));
    QTRY_VERIFY_WITH_TIMEOUT(hasSuccessfulSignal(operations, beforeExport), 15000);
    QFile output(exported);
    QVERIFY(output.open(QIODevice::ReadOnly));
    QCOMPARE(output.readAll(), bytes);
}

void TestDspAssetDialog::rejectedBadFileIsNotPublished()
{
    QTemporaryDir directory;
    AppSettings settings(directory.filePath(QStringLiteral("station.settings")));
    DspAssetService service(settings, true);
    DspAssetDialog dialog(nullptr, &service, DspAssetKind::NnrModel);
    QSignalSpy operations(&dialog, &DspAssetDialog::operationFinished);
    QTRY_VERIFY(dialog.findChild<QPushButton*>(QStringLiteral("dspAssetImportButton"))
                    ->isEnabled());
    const QString path = writeFile(directory.filePath(QStringLiteral("bad.bin")),
                                   QByteArrayLiteral("not a WDSPNN model"));
    QVERIFY(dialog.importFile(path, QStringLiteral("Bad model")));
    QTRY_VERIFY_WITH_TIMEOUT(!operations.isEmpty()
                                 && !operations.last().at(0).toBool(), 5000);
    QCOMPARE(service.store()->assets().size(), 0);
}

void TestDspAssetDialog::missingDesiredSelectionRemainsVisibleAndPending()
{
    QTemporaryDir directory;
    AppSettings settings(directory.filePath(QStringLiteral("station.settings")));
    const QString missing = QStringLiteral("sha256:") + QString(64, QLatin1Char('a'));
    settings.setValue(QStringLiteral("DspAssets/NnrModel0"), missing);
    DspAssetService service(settings, true);
    DspAssetDialog dialog(nullptr, &service, DspAssetKind::NnrModel);
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("nnrStandardAssetCombo"));
    auto* status = dialog.findChild<QLabel*>(QStringLiteral("nnrAssetSelectionStatus"));
    QVERIFY(combo && status);
    QTRY_COMPARE(combo->currentData().toString(), missing);
    QVERIFY(combo->currentText().contains(QStringLiteral("Missing")));
    QVERIFY(service.nnrModelSelectionPending());
    QVERIFY(status->text().contains(QStringLiteral("Pending")));
}

void TestDspAssetDialog::refusedSelectionRestoresCoreState()
{
    QTemporaryDir directory;
    AppSettings settings(directory.filePath(QStringLiteral("station.settings")));
    DspAssetService service(settings, true);
    DspAssetDialog dialog(nullptr, &service, DspAssetKind::NnrModel);
    QSignalSpy operations(&dialog, &DspAssetDialog::operationFinished);
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("nnrStandardAssetCombo"));
    QVERIFY(combo);
    QTRY_VERIFY(combo->isEnabled());
    const QString accepted = service.nnrStandardAsset();
    const QString unavailable = QStringLiteral("sha256:") + QString(64, QLatin1Char('f'));
    combo->addItem(QStringLiteral("Unavailable fixture"), unavailable);
    const int invalidIndex = combo->count() - 1;
    combo->setCurrentIndex(invalidIndex);
    QVERIFY(QMetaObject::invokeMethod(combo, "activated", Qt::DirectConnection,
                                      Q_ARG(int, invalidIndex)));
    QTRY_VERIFY_WITH_TIMEOUT(!operations.isEmpty()
                                 && !operations.last().at(0).toBool(), 5000);
    QCOMPARE(service.nnrStandardAsset(), accepted);
    QCOMPARE(combo->currentData().toString(), accepted);
}

void TestDspAssetDialog::importingDoesNotApplyOrReconnect()
{
    QTemporaryDir directory;
    AppSettings settings(directory.filePath(QStringLiteral("station.settings")));
    DspAssetService service(settings, true);
    DspAssetDialog dialog(nullptr, &service, DspAssetKind::NnrModel);
    QSignalSpy operations(&dialog, &DspAssetDialog::operationFinished);
    QTRY_VERIFY(dialog.findChild<QPushButton*>(QStringLiteral("dspAssetImportButton"))
                    ->isEnabled());
    const QString path = writeFile(directory.filePath(QStringLiteral("passive.bin")),
                                   standardModel());
    const quint32 revision = service.selectionRevision();
    QVERIFY(dialog.importFile(path, QStringLiteral("Passive import")));
    QTRY_VERIFY_WITH_TIMEOUT(hasSuccessfulSignal(operations, 0), 15000);
    QCOMPARE(service.selectionRevision(), revision);
    QVERIFY(!service.nnrModelSelectionPending());
    QVERIFY(!dialog.findChild<QPushButton*>(QStringLiteral("applyNnrAssetsButton"))->isEnabled());
}

void TestDspAssetDialog::restoreEmitsOnlySelectedCorrectionIdentity()
{
    QTemporaryDir directory;
    AppSettings settings(directory.filePath(QStringLiteral("station.settings")));
    DspAssetService service(settings, true);
    const QString radioIdentity = QStringLiteral("AA:BB:CC:DD:EE:99");
    service.setRadioIdentity(radioIdentity);
    QFile fixture(QFINDTESTDATA("fixtures/dsp/ps3-v2-source-writer.txt"));
    QVERIFY2(fixture.open(QIODevice::ReadOnly), qPrintable(fixture.fileName()));
    const auto imported = upload(service, DspAssetKind::Ps3Correction, fixture.readAll(),
                                 QStringLiteral("Bench correction"), radioIdentity);
    QVERIFY2(imported.accepted, qPrintable(imported.reason));
    const QString id = imported.values.value(QStringLiteral("id")).toString();

    // Exercise the presentation contract with an explicitly authorized future
    // station; current R4 refusal is covered at the real session boundary.
    RadioModel radio(RadioModel::Role::Remote);
    radio.pureSignalFacade()->setRemoteCapabilities(true, true);
    radio.pureSignalFacade()->applyRemoteProperty("available", true);
    radio.pureSignalFacade()->applyRemoteProperty("canActuate", true);
    DspAssetDialog dialog(&radio, &service, DspAssetKind::Ps3Correction);
    QSignalSpy restored(&dialog, &DspAssetDialog::restoreCorrectionRequested);
    auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("dspAssetTable"));
    auto* button = dialog.findChild<QPushButton*>(QStringLiteral("restoreCorrectionAssetButton"));
    QVERIFY(table && button);
    QTRY_COMPARE(table->rowCount(), 1);
    table->selectRow(0);
    QTRY_VERIFY(button->isEnabled());
    QVERIFY2(captureIfRequested(dialog, QStringLiteral("dsp-assets-ps3.png")),
             "Could not save opt-in PS3 asset manager capture");
    button->click();
    QCOMPARE(restored.size(), 1);
    QCOMPARE(restored.first().at(0).toString(), id);
    QCOMPARE(service.store()->assets().size(), 1);
    radio.pureSignalFacade()->setRemoteCapabilities(true, false);
    QVERIFY(!button->isEnabled());
    button->click();
    QCOMPARE(restored.size(), 1);
}

void TestDspAssetDialog::geometryAndCollapsedDetailsRoundTrip()
{
    AppSettings& settings = AppSettings::instance();
    const QString geometryKey = QStringLiteral("DspAssetDialog/NnrModel/Geometry");
    const QString detailsKey = QStringLiteral("DspAssetDialog/NnrModel/DetailsExpanded");
    const bool hadGeometry = settings.contains(geometryKey);
    const bool hadDetails = settings.contains(detailsKey);
    const QVariant oldGeometry = settings.value(geometryKey);
    const QVariant oldDetails = settings.value(detailsKey);
    settings.remove(geometryKey);
    settings.remove(detailsKey);

    QSize savedSize;
    {
        DspAssetDialog dialog(nullptr, static_cast<DspAssetService*>(nullptr),
                              DspAssetKind::NnrModel);
        dialog.resize(690, 470);
        auto* details = dialog.findChild<QGroupBox*>(QStringLiteral("dspAssetDetails"));
        auto* detailsText = dialog.findChild<QLabel*>(QStringLiteral("dspAssetDetailsText"));
        QVERIFY(details && detailsText);
        QVERIFY(!details->isChecked());
        QVERIFY(detailsText->isHidden());
        details->setChecked(true);
        QVERIFY(!detailsText->isHidden());
        savedSize = dialog.size();
        dialog.close();
    }

    {
        DspAssetDialog restored(nullptr, static_cast<DspAssetService*>(nullptr),
                                DspAssetKind::NnrModel);
        auto* details = restored.findChild<QGroupBox*>(QStringLiteral("dspAssetDetails"));
        auto* detailsText = restored.findChild<QLabel*>(QStringLiteral("dspAssetDetailsText"));
        QVERIFY(details && detailsText);
        QCOMPARE(restored.size(), savedSize);
        QVERIFY(details->isChecked());
        QVERIFY(!detailsText->isHidden());
    }

    if (hadGeometry) {
        settings.setValue(geometryKey, oldGeometry);
    } else {
        settings.remove(geometryKey);
    }
    if (hadDetails) {
        settings.setValue(detailsKey, oldDetails);
    } else {
        settings.remove(detailsKey);
    }
}

QTEST_MAIN(TestDspAssetDialog)
#include "tst_dsp_asset_dialog.moc"
