// no-port-check: NereusSDR-original integration tests for stable-slice saves.
#include <QtTest>
#include <QDir>
#include <QFile>
#include "core/AppSettings.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

class TestNnrRadioPersistence : public QObject {
    Q_OBJECT
private slots:
    void init() { AppSettings::instance().clear(); }

    void inactiveSliceAndRemovedSliceFlushBeforeTheirIdentityDisappears()
    {
        const QString mac = QStringLiteral("00:1C:2D:03:04:05");
        RadioModel radio;
        const int aId = radio.addSlice();
        const int bId = radio.addSlice();
        const int cId = radio.addSlice();
        QVERIFY(aId >= 0 && bId >= 0 && cId >= 0);
        auto* a = radio.sliceById(aId);
        auto* b = radio.sliceById(bId);
        auto* c = radio.sliceById(cId);
        for (SliceModel* slice : {a, b, c}) {
            slice->setSettingsRadioIdentity(mac);
        }
        radio.setActiveSliceById(aId);
        b->setNnrAlpha(2.25);
        b->setNnrMaskFloorDb(-32.75);
        b->setActiveNr(NrSlot::NNR);
        c->setNnrReleaseMs(125.5);
        c->setActiveNr(NrSlot::NR2);
        const QString bPrefix = b->nnrSettingsPrefix();
        const QString cPrefix = c->nnrSettingsPrefix();
        radio.removeSlice(bId);
        QVERIFY(!radio.sliceById(bId));
        QCOMPARE(radio.sliceById(cId), c);
        radio.flushPendingSettingsSave();

        // Clear memory and reload the actual atomic settings file. Neither
        // operation relies on a dialog close or the active receiver's ID.
        AppSettings::instance().clear();
        AppSettings::instance().load();
        QCOMPARE(AppSettings::instance().value(bPrefix + "NnrAlpha").toDouble(), 2.25);
        QCOMPARE(AppSettings::instance().value(bPrefix + "NnrMaskFloorDb").toDouble(), -32.75);
        QCOMPARE(AppSettings::instance().value(bPrefix + "NrActive").toInt(), 8);
        QCOMPARE(AppSettings::instance().value(cPrefix + "NnrReleaseMs").toDouble(), 125.5);
        QCOMPARE(AppSettings::instance().value(cPrefix + "NrActive").toInt(), 2);

        // Focus changes after the edit cannot resurrect the previous NNR
        // choice. Selection and tuning use the same durable namespace.
        c->setActiveNr(NrSlot::Off);
        radio.setActiveSliceById(cId);
        radio.flushPendingSettingsSave();
        AppSettings::instance().clear();
        AppSettings::instance().load();
        QCOMPARE(AppSettings::instance().value(cPrefix + "NrActive").toInt(), 0);
        QCOMPARE(AppSettings::instance().value(cPrefix + "NnrReleaseMs").toDouble(), 125.5);
    }

    void failedSaveRemainsVisibleAndRetriesTheAcceptedValues()
    {
        auto& settings = AppSettings::instance();
        RadioModel radio;
        const int id = radio.addSlice();
        auto* slice = radio.sliceById(id);
        slice->setSettingsRadioIdentity("AA:BB:CC:DD:EE:08");
        const QString prefix = slice->nnrSettingsPrefix();
        slice->setNnrAlpha(2.75);
        // A directory at the target file path deterministically refuses an
        // atomic file replacement, including when tests run with elevated rights.
        QFile::remove(settings.filePath());
        QVERIFY(QDir().mkpath(settings.filePath()));
        radio.flushPendingSettingsSave();
        QVERIFY(!radio.settingsSaveError().isEmpty());
        QCOMPARE(slice->nnrAlpha(), 2.75);
        QVERIFY(QDir().rmdir(settings.filePath()));
        radio.flushPendingSettingsSave();
        QVERIFY(radio.settingsSaveError().isEmpty());
        settings.clear();
        settings.load();
        QCOMPARE(settings.value(prefix + "NnrAlpha").toDouble(), 2.75);
    }

    void configuredSlicesRestoreTheirOwnSettingsBeforeUse()
    {
        const QString mac = QStringLiteral("00:1C:2D:03:04:05");
        auto& settings = AppSettings::instance();
        settings.setValue("hardware/" + mac + "/slices/0/nnr/NnrAlpha", 2.0);
        settings.setValue("hardware/" + mac + "/slices/1/nnr/NnrAlpha", 3.0);
        settings.setValue("hardware/" + mac + "/slices/1/nnr/NrActive", 8);
        RadioModel radio;
        const int aId = radio.addSlice();
        const int bId = radio.addSlice();
        auto* a = radio.sliceById(aId);
        auto* b = radio.sliceById(bId);
        a->setSettingsRadioIdentity(mac);
        b->setSettingsRadioIdentity(mac);
        radio.loadSliceState(a);
        radio.loadSliceState(b);
        QCOMPARE(a->nnrAlpha(), 2.0);
        QCOMPARE(b->nnrAlpha(), 3.0);
        QCOMPARE(b->activeNr(), NrSlot::NNR);
        QVERIFY(!b->nnrRunning()); // Saved intent is not a live receiver.
    }
};

QTEST_MAIN(TestNnrRadioPersistence)
#include "tst_nnr_radio_persistence.moc"
