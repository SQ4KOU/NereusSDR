// no-port-check: NereusSDR-original tests for AppSettings::saveInBackground().
#include <QtTest>
#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>

#include <atomic>
#include <cstring>
#include <memory>

#include "core/AppSettings.h"
#include "core/SettingsFileWriter.h"

using namespace NereusSDR;

namespace {

constexpr const char* kExitChildArgument = "--exit-while-saving";
const QString kExitProbeProfile = QStringLiteral("background-save-exit-probe");

QString savedValue(const QString& path, const QString& key)
{
    AppSettings onDisk(path);
    onDisk.load();
    return onDisk.value(key, QStringLiteral("<missing>")).toString();
}

// The child half of aSaveStillWritingAsTheProcessEndsReachesTheDisk(): main()
// returns while the process-wide store's save is still on the writer thread.
int runExitWhileSavingChild(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    AppSettings::setProfileOverride(kExitProbeProfile);
    AppSettings& settings = AppSettings::instance();
    settings.setValue(QStringLiteral("ExitProbe"), QStringLiteral("written"));
    settings.setBackgroundSaveStartHookForTesting([] { QThread::msleep(500); });
    settings.saveInBackground();
    return 0;
}

} // namespace

class TestAppSettingsBackgroundSave : public QObject {
    Q_OBJECT
private slots:
    void theFileIsWrittenAwayFromTheCallingThread()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("NereusSDR.settings"));
        AppSettings settings(path);
        settings.setValue(QStringLiteral("Dial"), QStringLiteral("14257000"));
        std::atomic<QThread*> writerThread{nullptr};
        settings.setBackgroundSaveStartHookForTesting(
            [&writerThread] { writerThread = QThread::currentThread(); });
        QSignalSpy finished(settings.backgroundSaveNotifier(), &SettingsFileWriter::finished);

        const quint64 ticket = settings.saveInBackground();

        QVERIFY(ticket != 0);
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(finished.at(0).at(0).toULongLong(), ticket);
        QCOMPARE(finished.at(0).at(1).toBool(), true);
        QCOMPARE(finished.at(0).at(2).toString(), QString());
        QVERIFY(writerThread.load() != nullptr);
        QVERIFY(writerThread.load() != QThread::currentThread());
        QCOMPARE(savedValue(path, QStringLiteral("Dial")), QStringLiteral("14257000"));
    }

    void theValuesWrittenAreTheOnesHeldWhenTheSaveWasAsked()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("NereusSDR.settings"));
        AppSettings settings(path);
        settings.setValue(QStringLiteral("Dial"), QStringLiteral("first"));
        const auto entered = std::make_shared<QSemaphore>();
        const auto release = std::make_shared<QSemaphore>();
        settings.setBackgroundSaveStartHookForTesting([entered, release] {
            entered->release();
            release->acquire();
        });
        // On every way out, so a failed check cannot leave the writer
        // thread held.
        const auto letGo = qScopeGuard([release] { release->release(8); });
        QSignalSpy finished(settings.backgroundSaveNotifier(), &SettingsFileWriter::finished);

        settings.saveInBackground();
        QVERIFY(entered->tryAcquire(1, 5000));
        settings.setValue(QStringLiteral("Dial"), QStringLiteral("second"));
        release->release();

        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(savedValue(path, QStringLiteral("Dial")), QStringLiteral("first"));
    }

    void aBlockingSaveReplacesABackgroundSaveStillWaiting()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("NereusSDR.settings"));
        AppSettings settings(path);
        settings.setValue(QStringLiteral("Dial"), QStringLiteral("older"));
        const auto entered = std::make_shared<QSemaphore>();
        const auto release = std::make_shared<QSemaphore>();
        settings.setBackgroundSaveStartHookForTesting([entered, release] {
            entered->release();
            release->acquire();
        });
        // On every way out, so a failed check cannot leave the writer
        // thread held.
        const auto letGo = qScopeGuard([release] { release->release(8); });
        QSignalSpy finished(settings.backgroundSaveNotifier(), &SettingsFileWriter::finished);

        settings.saveInBackground();
        QVERIFY(entered->tryAcquire(1, 5000));
        settings.setValue(QStringLiteral("Dial"), QStringLiteral("newer"));
        QVERIFY(settings.save());
        QCOMPARE(savedValue(path, QStringLiteral("Dial")), QStringLiteral("newer"));
        // A second background save is the marker that the first has left
        // the writer thread: one thread, in order. Both pass the hook.
        release->release(2);
        const quint64 marker = settings.saveInBackground();
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(finished.at(0).at(0).toULongLong(), marker);
        QCOMPARE(savedValue(path, QStringLiteral("Dial")), QStringLiteral("newer"));
    }

    void aFailedWriteIsReportedWithItsReason()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("NereusSDR.settings"));
        // A directory at the file's path refuses the atomic replacement,
        // including when tests run with elevated rights.
        QVERIFY(QDir().mkpath(path));
        AppSettings settings(path);
        settings.setValue(QStringLiteral("Dial"), QStringLiteral("14257000"));
        QSignalSpy finished(settings.backgroundSaveNotifier(), &SettingsFileWriter::finished);

        const quint64 ticket = settings.saveInBackground();

        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(finished.at(0).at(0).toULongLong(), ticket);
        QCOMPARE(finished.at(0).at(1).toBool(), false);
        QVERIFY(!finished.at(0).at(2).toString().isEmpty());
    }

    void anImportIsNotOverwrittenByABackgroundSaveStillWaiting()
    {
        QTemporaryDir dir;
        const QString importPath = dir.filePath(QStringLiteral("import.xml"));
        {
            AppSettings source(importPath);
            source.setValue(QStringLiteral("Dial"), QStringLiteral("imported"));
            QVERIFY(source.save());
        }
        const QString path = dir.filePath(QStringLiteral("NereusSDR.settings"));
        AppSettings settings(path);
        settings.setValue(QStringLiteral("Dial"), QStringLiteral("running"));
        const auto entered = std::make_shared<QSemaphore>();
        const auto release = std::make_shared<QSemaphore>();
        settings.setBackgroundSaveStartHookForTesting([entered, release] {
            entered->release();
            release->acquire();
        });
        // On every way out, so a failed check cannot leave the writer
        // thread held.
        const auto letGo = qScopeGuard([release] { release->release(8); });
        QSignalSpy finished(settings.backgroundSaveNotifier(), &SettingsFileWriter::finished);

        settings.saveInBackground();
        QVERIFY(entered->tryAcquire(1, 5000));
        QString error;
        QVERIFY2(settings.importFileForNextLaunch(importPath, &error), qPrintable(error));
        release->release();

        // Saves are held from here on; this one writes nothing and reports
        // success after the waiting save has left the writer thread.
        const quint64 held = settings.saveInBackground();
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(finished.at(0).at(0).toULongLong(), held);
        QCOMPARE(finished.at(0).at(1).toBool(), true);
        QCOMPARE(savedValue(path, QStringLiteral("Dial")), QStringLiteral("imported"));
    }

    void aSaveStillWritingAsTheProcessEndsReachesTheDisk()
    {
        // instance() is a static in the Core library. On Windows a DLL's
        // static destructors run after the process has ended every other
        // thread, so a save still on the writer thread then was waited for
        // forever and the process never ended.
        const QString path = AppSettings::resolveSettingsPath(kExitProbeProfile);
        const QString profileDir = QFileInfo(path).absolutePath();
        QDir(profileDir).removeRecursively();
        const auto removeProfile = qScopeGuard([profileDir] {
            QDir(profileDir).removeRecursively();
        });

        QProcess child;
        child.setProcessChannelMode(QProcess::ForwardedChannels);
        child.start(QCoreApplication::applicationFilePath(),
                    {QString::fromLatin1(kExitChildArgument)});
        QVERIFY(child.waitForStarted(5000));
        const bool ended = child.waitForFinished(30000);
        if (!ended) {
            child.kill();
            child.waitForFinished(3000);
        }
        QVERIFY2(ended, "the process did not end after main() returned");
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QCOMPARE(child.exitCode(), 0);
        QCOMPARE(savedValue(path, QStringLiteral("ExitProbe")), QStringLiteral("written"));
    }
};

int main(int argc, char* argv[])
{
    if (argc > 1 && std::strcmp(argv[1], kExitChildArgument) == 0) {
        return runExitWhileSavingChild(argc, argv);
    }
    QCoreApplication app(argc, argv);
    TestAppSettingsBackgroundSave test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}
#include "tst_app_settings_background_save.moc"
