// no-port-check: NereusSDR-original test.
//
// tst_session_end_save.cpp
//
// A logoff, shutdown or forced close saves the settings (settings restore
// audit 2026-10-11, gap 8). SessionEndSave listens to the session manager's
// request (QGuiApplication::commitDataRequest) and runs the save at once.
//
// Qt gives a test no way to raise that request (a QSessionManager cannot be
// made outside Qt), so this checks the two halves it can: the request is
// wired, and what it runs writes a change the 500 ms save timer has not
// written yet, without quitting the application.
//
// The settings store is the test sandbox's (TestSandboxInit).

#include <QtTest/QtTest>

#include <QDir>
#include <QStandardPaths>

#include <QFile>
#include <QSignalSpy>

#include "core/AppSettings.h"
#include "gui/SessionEndSave.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

namespace {

double savedDouble(const QString& key)
{
    AppSettings onDisk(AppSettings::instance().filePath());
    onDisk.load();
    return onDisk.value(key, QStringLiteral("-1")).toDouble();
}

} // namespace

class TestSessionEndSave : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        AppSettings::instance().clear();
        // The backup too: with only the file gone, the read in
        // savedDouble recovers the last run's value from it.
        QFile::remove(AppSettings::instance().filePath());
        QFile::remove(AppSettings::instance().filePath() + QStringLiteral(".bak"));
    }

    // The settings these tests write are the test sandbox's (Qt's test
    // mode, set up by TestSandboxInit), never the operator's.
    void theSettingsFileIsTheSandboxsNotTheOperators()
    {
        QVERIFY(QStandardPaths::isTestModeEnabled());
        const QString path = AppSettings::instance().filePath();
        const QString home = QDir::homePath();
        QVERIFY2(!path.startsWith(home + QStringLiteral("/.config/NereusSDR")), qPrintable(path));
        QVERIFY2(!path.startsWith(home + QStringLiteral("/Library/Preferences/NereusSDR")),
                 qPrintable(path));
    }

    void cleanup() { AppSettings::instance().clear(); }

    void theSessionManagersRequestIsWired()
    {
#ifdef QT_NO_SESSIONMANAGER
        QSKIP("This Qt has no session management");
#endif
        int saves = 0;
        SessionEndSave sessionEnd([&saves] { ++saves; });
        QVERIFY(sessionEnd.listening());
        QCOMPARE(saves, 0);
    }

    void aSessionEndRunsTheSaveOncePerRequest()
    {
        int saves = 0;
        SessionEndSave sessionEnd([&saves] { ++saves; });
        sessionEnd.commit();
        QCOMPARE(saves, 1);
        sessionEnd.commit();
        QCOMPARE(saves, 2);
        QCOMPARE(sessionEnd.commitCount(), 2);
    }

    // The save the window hands over: a change made a moment ago, which
    // the 500 ms timer has not written, is on disk when the request
    // returns, and the application is not asked to quit.
    void aChangeNotYetWrittenIsOnDiskWhenTheRequestReturns()
    {
        RadioModel radio;
        SliceModel* slice = radio.sliceById(radio.addSlice());
        QVERIFY(slice);
        slice->setSettingsRadioIdentity(QStringLiteral("AA:BB:CC:DD:EE:31"));
        const QString key = slice->nnrSettingsPrefix() + QStringLiteral("NnrAlpha");

        SessionEndSave sessionEnd([&radio] {
            radio.flushPendingSettingsSave();
            AppSettings::instance().save();
        });
        QSignalSpy quitting(qApp, &QCoreApplication::aboutToQuit);

        slice->setNnrAlpha(2.75);
        QCOMPARE(savedDouble(key), -1.0);   // not written yet

        sessionEnd.commit();

        QCOMPARE(savedDouble(key), 2.75);
        QCOMPARE(quitting.size(), 0);
        QVERIFY(radio.settingsSaveError().isEmpty());
    }
};

QTEST_MAIN(TestSessionEndSave)
#include "tst_session_end_save.moc"
