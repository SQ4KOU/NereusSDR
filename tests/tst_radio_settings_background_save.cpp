// no-port-check: NereusSDR-original tests for RadioModel's timed settings
// save, which must not write the file on the thread that sends the Core's
// receive audio.
//
// Modification history (NereusSDR):
// 2026-10-15 - A value written from outside (a remote window through the
//              Core's settings proxy) to a held per-band key survives the
//              next band click. J.J. Boyd (KG4VCF), AI-assisted via
//              Anthropic Claude Code.
#include <QtTest>
#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSignalSpy>
#include <QThread>

#include <atomic>
#include <memory>

#include "core/AppSettings.h"
#include "core/SettingsFileWriter.h"
#include "core/settings/SettingsProxyServer.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

namespace {

constexpr int kTimedSaveWaitMs = 10000;

double savedDouble(const QString& key)
{
    AppSettings onDisk(AppSettings::instance().filePath());
    onDisk.load();
    return onDisk.value(key, QStringLiteral("-1")).toDouble();
}

} // namespace

class TestRadioSettingsBackgroundSave : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
        AppSettings::instance().clear();
        QFile::remove(AppSettings::instance().filePath());
    }

    void cleanup() { AppSettings::instance().setBackgroundSaveStartHookForTesting({}); }

    // A dirty identity alone loses the outgoing band's DSP if its live
    // slice crosses bands before the coalesced writer captures values.
    void pendingDspEditsSurviveFrequencyBandCrossing()
    {
        RadioModel radio;
        SliceModel* slice = radio.sliceById(radio.addSlice());
        QVERIFY(slice);
        QVERIFY(radio.setActiveSliceById(slice->sliceIndex()));
        const QString prefix = QStringLiteral("Slice%1/").arg(slice->sliceIndex());
        slice->setFrequency(14225000.0);
        slice->setFilter(200, 2800);
        slice->setAgcThreshold(-20);
        radio.requestSettingsSave();
        radio.flushPendingSettingsSave();

        slice->setFilter(350, 2450);
        slice->setAgcThreshold(-37);
        radio.requestSettingsSave();
        // No event processing: both bands are edited inside the debounce.
        slice->setFrequency(7100000.0);
        radio.requestSettingsSave();
        radio.flushPendingSettingsSave();

        AppSettings onDisk(AppSettings::instance().filePath());
        onDisk.load();
        QCOMPARE(onDisk.value(prefix + "Band20m/FilterLow").toInt(), 350);
        QCOMPARE(onDisk.value(prefix + "Band20m/FilterHigh").toInt(), 2450);
        QCOMPARE(onDisk.value(prefix + "Band20m/AgcThreshold").toInt(), -37);
        QCOMPARE(onDisk.value(prefix + "Band20m/ModeUSB/FilterLow").toInt(), 350);
        QCOMPARE(onDisk.value(prefix + "Band20m/ModeUSB/FilterHigh").toInt(), 2450);
        QCOMPARE(onDisk.value(prefix + "Band20m/Frequency").toDouble(), 14225000.0);
        QCOMPARE(onDisk.value(prefix + "Band40m/Frequency").toDouble(), 7100000.0);
        QCOMPARE(onDisk.value(prefix + "LastBand").toString(), QStringLiteral("40m"));
        QCOMPARE(slice->frequency(), 7100000.0);
        QCOMPARE(slice->filterLow(), 350);
        QCOMPARE(slice->filterHigh(), 2450);
        QCOMPARE(slice->agcThreshold(), -37);
    }

    void rapidBandAndSliceEditsKeepTheLatestValueForEachBand()
    {
        RadioModel radio;
        SliceModel* first = radio.sliceById(radio.addSlice());
        SliceModel* second = radio.sliceById(radio.addSlice());
        QVERIFY(first);
        QVERIFY(second);
        const QString firstPrefix = QStringLiteral("Slice%1/").arg(first->sliceIndex());
        const QString secondPrefix = QStringLiteral("Slice%1/").arg(second->sliceIndex());
        QVERIFY(radio.setActiveSliceById(first->sliceIndex()));
        first->setFrequency(14225000.0);
        first->setFilter(200, 2800);
        radio.requestSettingsSave();
        radio.flushPendingSettingsSave();
        const auto before = AppSettings::instance().snapshot({QString()});

        first->setFilter(300, 2600);
        radio.requestSettingsSave();
        first->setFilter(400, 2400);
        radio.requestSettingsSave();
        first->setFrequency(7100000.0);
        first->setFilter(500, 2200);
        radio.requestSettingsSave();
        first->setFrequency(21250000.0);
        first->setFilter(600, 2000);
        first->setMuted(true);
        radio.requestSettingsSave();
        QVERIFY(radio.setActiveSliceById(second->sliceIndex()));
        second->setFrequency(10125000.0);
        second->setAgcThreshold(-43);
        radio.requestSettingsSave();
        // Capturing is inert until the existing coalesced flush: the map
        // change hook (and its station broadcasts) must not run per edit.
        QCOMPARE(AppSettings::instance().snapshot({QString()}), before);
        radio.flushPendingSettingsSave();

        AppSettings onDisk(AppSettings::instance().filePath());
        onDisk.load();
        QCOMPARE(onDisk.value(firstPrefix + "Band20m/FilterLow").toInt(), 400);
        QCOMPARE(onDisk.value(firstPrefix + "Band20m/Frequency").toDouble(), 14225000.0);
        QCOMPARE(onDisk.value(firstPrefix + "Band40m/FilterLow").toInt(), 500);
        QCOMPARE(onDisk.value(firstPrefix + "Band40m/Frequency").toDouble(), 7100000.0);
        QCOMPARE(onDisk.value(firstPrefix + "Band15m/FilterLow").toInt(), 600);
        QCOMPARE(onDisk.value(firstPrefix + "Band15m/Frequency").toDouble(), 21250000.0);
        QCOMPARE(onDisk.value(firstPrefix + "LastBand").toString(), QStringLiteral("15m"));
        QCOMPARE(onDisk.value(firstPrefix + "Muted").toString(), QStringLiteral("True"));
        QVERIFY(!onDisk.contains(firstPrefix + "Band20m/Muted"));
        QVERIFY(!onDisk.contains(firstPrefix + "Band20m/RxAntenna"));
        QVERIFY(!onDisk.contains(firstPrefix + "Band20m/NnrAlpha"));
        QCOMPARE(onDisk.value(secondPrefix + "Band30m/AgcThreshold").toInt(), -43);
        QCOMPARE(onDisk.value(secondPrefix + "LastBand").toString(), QStringLiteral("30m"));
    }

    // The held copy of a band the slice left must not overwrite a newer
    // value someone else wrote to one of its keys, here a remote window
    // writing through the Core's settings proxy. The slice's own edits on
    // that band, in the keys nobody else wrote, are still saved.
    void outsideWriteToAHeldBandKeySurvivesTheNextBandClick()
    {
        SettingsProxyServer proxy(AppSettings::instance());
        RadioModel radio;
        SliceModel* slice = radio.sliceById(radio.addSlice());
        QVERIFY(slice);
        QVERIFY(radio.setActiveSliceById(slice->sliceIndex()));
        const QString prefix = QStringLiteral("Slice%1/").arg(slice->sliceIndex());
        slice->setFrequency(14225000.0);
        slice->setFilter(200, 2800);
        slice->setAgcThreshold(-20);
        radio.requestSettingsSave();
        radio.flushPendingSettingsSave();

        slice->setFilter(350, 2450);
        slice->setAgcThreshold(-37);
        radio.requestSettingsSave();
        slice->setFrequency(7100000.0);
        radio.requestSettingsSave();

        const SettingsApplyResult result = proxy.applyInboundWrite(
            prefix + QStringLiteral("Band20m/FilterLow"), 500, QStringLiteral("remote-window"));
        QVERIFY2(result.accepted, qPrintable(result.reason));
        radio.onBandButtonClicked(Band::Band15m);

        auto& settings = AppSettings::instance();
        QCOMPARE(settings.value(prefix + "Band20m/FilterLow").toInt(), 500);
        QCOMPARE(settings.value(prefix + "Band20m/FilterHigh").toInt(), 2450);
        QCOMPARE(settings.value(prefix + "Band20m/AgcThreshold").toInt(), -37);
        radio.requestSettingsSave();
        radio.flushPendingSettingsSave();

        AppSettings onDisk(settings.filePath());
        onDisk.load();
        QCOMPARE(onDisk.value(prefix + "Band20m/FilterLow").toInt(), 500);
        QCOMPARE(onDisk.value(prefix + "Band20m/FilterHigh").toInt(), 2450);
        QCOMPARE(onDisk.value(prefix + "Band20m/AgcThreshold").toInt(), -37);
        QCOMPARE(onDisk.value(prefix + "Band40m/Frequency").toDouble(), 7100000.0);
    }

    void explicitBandRecallConsumesPendingValuesBeforeSavingOutgoingState()
    {
        RadioModel radio;
        SliceModel* slice = radio.sliceById(radio.addSlice());
        QVERIFY(slice);
        QVERIFY(radio.setActiveSliceById(slice->sliceIndex()));
        const QString prefix = QStringLiteral("Slice%1/").arg(slice->sliceIndex());
        slice->setFrequency(14225000.0);
        slice->setFilter(200, 2800);
        radio.requestSettingsSave();
        radio.flushPendingSettingsSave();

        slice->setFilter(350, 2450);
        radio.requestSettingsSave();
        slice->setFrequency(7100000.0);
        slice->setFilter(450, 2350);
        radio.requestSettingsSave();
        // Band-button save must remain authoritative over an older capture.
        slice->setFilter(550, 2250);
        radio.onBandButtonClicked(Band::Band20m);
        QCOMPARE(slice->frequency(), 14225000.0);
        QCOMPARE(slice->filterLow(), 350);
        radio.requestSettingsSave();
        radio.flushPendingSettingsSave();

        AppSettings onDisk(AppSettings::instance().filePath());
        onDisk.load();
        QCOMPARE(onDisk.value(prefix + "Band40m/FilterLow").toInt(), 550);
        QCOMPARE(onDisk.value(prefix + "Band40m/Frequency").toDouble(), 7100000.0);
        QCOMPARE(onDisk.value(prefix + "LastBand").toString(), QStringLiteral("20m"));
    }

    void failedSaveAndRemovedIdentityKeepOutgoingValuesWithoutStaleReplay()
    {
        auto& settings = AppSettings::instance();
        RadioModel radio;
        const int id = radio.addSlice();
        SliceModel* slice = radio.sliceById(id);
        QVERIFY(slice);
        QVERIFY(radio.addSlice() >= 0);
        QVERIFY(radio.setActiveSliceById(id));
        const QString prefix = QStringLiteral("Slice%1/").arg(id);
        slice->setFrequency(14225000.0);
        slice->setFilter(200, 2800);
        radio.requestSettingsSave();
        radio.flushPendingSettingsSave();
        slice->setFilter(350, 2450);
        radio.requestSettingsSave();
        slice->setFrequency(7100000.0);
        radio.requestSettingsSave();
        QVERIFY(QFile::remove(settings.filePath()));
        QVERIFY(QDir().mkpath(settings.filePath()));
        const auto tidy = qScopeGuard([&settings] { QDir().rmdir(settings.filePath()); });
        radio.flushPendingSettingsSave();
        QVERIFY(!radio.settingsSaveError().isEmpty());
        QVERIFY(QDir().rmdir(settings.filePath()));
        radio.removeSlice(id);
        QVERIFY(!radio.sliceById(id));
        QCOMPARE(radio.addSlice(), id);
        SliceModel* replacement = radio.sliceById(id);
        QVERIFY(replacement);
        QVERIFY(radio.setActiveSliceById(id));
        replacement->setFrequency(7105000.0);
        replacement->setFilter(650, 2150);
        radio.requestSettingsSave();
        radio.flushPendingSettingsSave();

        AppSettings onDisk(settings.filePath());
        onDisk.load();
        QCOMPARE(onDisk.value(prefix + "Band20m/FilterLow").toInt(), 350);
        QCOMPARE(onDisk.value(prefix + "Band20m/Frequency").toDouble(), 14225000.0);
        QCOMPARE(onDisk.value(prefix + "Band40m/FilterLow").toInt(), 650);
        QCOMPARE(onDisk.value(prefix + "Band40m/Frequency").toDouble(), 7105000.0);
        QVERIFY(radio.settingsSaveError().isEmpty());
    }

    void theTimedSaveWritesTheFileAwayFromTheMainThread()
    {
        auto& settings = AppSettings::instance();
        RadioModel radio;
        auto* slice = radio.sliceById(radio.addSlice());
        QVERIFY(slice);
        slice->setSettingsRadioIdentity("AA:BB:CC:DD:EE:21");
        const QString prefix = slice->nnrSettingsPrefix();
        std::atomic<QThread*> writerThread{nullptr};
        settings.setBackgroundSaveStartHookForTesting(
            [&writerThread] { writerThread = QThread::currentThread(); });
        QSignalSpy finished(settings.backgroundSaveNotifier(), &SettingsFileWriter::finished);

        slice->setNnrAlpha(2.75);

        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, kTimedSaveWaitMs);
        QCOMPARE(finished.at(0).at(1).toBool(), true);
        QVERIFY(writerThread.load() != nullptr);
        QVERIFY(writerThread.load() != QThread::currentThread());
        QCOMPARE(savedDouble(prefix + "NnrAlpha"), 2.75);
        QVERIFY(radio.settingsSaveError().isEmpty());
    }

    void aFailedTimedSaveIsShownAndALaterSaveClearsIt()
    {
        auto& settings = AppSettings::instance();
        RadioModel radio;
        auto* slice = radio.sliceById(radio.addSlice());
        QVERIFY(slice);
        slice->setSettingsRadioIdentity("AA:BB:CC:DD:EE:22");
        const QString prefix = slice->nnrSettingsPrefix();
        // A directory at the target file path deterministically refuses an
        // atomic file replacement, including when tests run with elevated rights.
        QVERIFY(QDir().mkpath(settings.filePath()));
        QSignalSpy errorChanged(&radio, &RadioModel::settingsSaveErrorChanged);

        slice->setNnrAlpha(2.75);

        QTRY_COMPARE_WITH_TIMEOUT(errorChanged.size(), 1, kTimedSaveWaitMs);
        QVERIFY(!radio.settingsSaveError().isEmpty());
        QCOMPARE(slice->nnrAlpha(), 2.75);
        QVERIFY(QDir().rmdir(settings.filePath()));
        radio.flushPendingSettingsSave();
        QVERIFY(radio.settingsSaveError().isEmpty());
        QCOMPARE(savedDouble(prefix + "NnrAlpha"), 2.75);
    }

    // A control change elsewhere on the Core saves in the background too
    // and replaces the timed save before it reaches the disk. Its result is
    // the timed save's result: a failure is shown and tried again.
    void aFailedLaterSaveAnswersForTheTimedSaveItReplaced()
    {
        auto& settings = AppSettings::instance();
        RadioModel radio;
        auto* slice = radio.sliceById(radio.addSlice());
        QVERIFY(slice);
        slice->setSettingsRadioIdentity("AA:BB:CC:DD:EE:24");
        const QString prefix = slice->nnrSettingsPrefix();
        const auto entered = std::make_shared<QSemaphore>();
        const auto release = std::make_shared<QSemaphore>();
        settings.setBackgroundSaveStartHookForTesting([entered, release] {
            entered->release();
            release->acquire();
        });
        const auto letGo = qScopeGuard([release] { release->release(8); });
        QSignalSpy errorChanged(&radio, &RadioModel::settingsSaveErrorChanged);

        slice->setNnrAlpha(2.75);
        QTRY_VERIFY_WITH_TIMEOUT(entered->available() > 0, kTimedSaveWaitMs);
        // A directory at the file's path refuses the write that follows.
        QVERIFY(QDir().mkpath(settings.filePath()));
        // Also on a failed check, so the next test finds no directory there.
        const auto tidy = qScopeGuard([&settings] { QDir().rmdir(settings.filePath()); });
        settings.saveInBackground();
        release->release(8);

        QTRY_COMPARE_WITH_TIMEOUT(errorChanged.size(), 1, kTimedSaveWaitMs);
        QVERIFY(!radio.settingsSaveError().isEmpty());
        QVERIFY(QDir().rmdir(settings.filePath()));
        radio.flushPendingSettingsSave();
        QVERIFY(radio.settingsSaveError().isEmpty());
        QCOMPARE(savedDouble(prefix + "NnrAlpha"), 2.75);
    }

    void aFlushWhileTheTimedSaveIsWithTheWriterWritesTheFileItself()
    {
        auto& settings = AppSettings::instance();
        RadioModel radio;
        auto* slice = radio.sliceById(radio.addSlice());
        QVERIFY(slice);
        slice->setSettingsRadioIdentity("AA:BB:CC:DD:EE:23");
        const QString prefix = slice->nnrSettingsPrefix();
        // Shared with the hook, and let go on every way out of this test,
        // so a failed check cannot leave the writer thread held.
        const auto entered = std::make_shared<QSemaphore>();
        const auto release = std::make_shared<QSemaphore>();
        settings.setBackgroundSaveStartHookForTesting([entered, release] {
            entered->release();
            release->acquire();
        });
        const auto letGo = qScopeGuard([release] { release->release(8); });
        QSignalSpy finished(settings.backgroundSaveNotifier(), &SettingsFileWriter::finished);

        slice->setNnrAlpha(2.75);
        QTRY_VERIFY_WITH_TIMEOUT(entered->available() > 0, kTimedSaveWaitMs);
        // The quit and disconnect paths: the file is on disk when the
        // flush returns, with the timed save still held on its thread.
        radio.flushPendingSettingsSave();
        QCOMPARE(savedDouble(prefix + "NnrAlpha"), 2.75);

        // Let the held save go (it was replaced, so it reports nothing),
        // then a marker save to know the writer thread is idle again.
        release->release(2);
        const quint64 marker = settings.saveInBackground();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, kTimedSaveWaitMs);
        QCOMPARE(finished.at(0).at(0).toULongLong(), marker);
        QCOMPARE(savedDouble(prefix + "NnrAlpha"), 2.75);
    }
};

QTEST_MAIN(TestRadioSettingsBackgroundSave)
#include "tst_radio_settings_background_save.moc"
