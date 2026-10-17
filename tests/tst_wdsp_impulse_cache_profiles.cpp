// =================================================================
// tests/tst_wdsp_impulse_cache_profiles.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test. Impulse caches of desktop profiles.
//
// Every desktop profile (--profile) keeps its saved FFT plans and its
// impulse cache in one shared folder, and a background Core is handed the
// same folder. Each profile has settings of its own, and at close a
// profile saves its impulse cache when its caching and save-restore are
// both on and deletes it otherwise, as Thetis does in its single data
// folder. One profile's close must never delete the cache another profile
// saved; a profile still deletes its own when its own save-restore is off.
//
// Each case runs the real WdspEngine startup and teardown (only the FFT
// planning is left out) against one shared folder, one profile after
// another, with that profile's settings.
//
// Modification history (NereusSDR):
//   2026-10-15  J.J. Boyd / KG4VCF  Created. AI-assisted via Anthropic
//                                    Claude Code.

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <memory>

#include "core/AppSettings.h"
#include "core/DspCacheLocation.h"
#include "core/WdspEngine.h"

using namespace NereusSDR;

namespace {

// Starts and closes one engine in the shared folder as the given profile,
// with that profile's caching settings.
void runProfile(const QString& folder, const QString& profile, bool cacheImpulse,
                bool saveRestore)
{
    AppSettings::setProfileOverride(profile);
    AppSettings& settings = AppSettings::instance();
    settings.setValue(QStringLiteral("DspOptionsCacheImpulse"),
                      cacheImpulse ? QStringLiteral("True") : QStringLiteral("False"));
    settings.setValue(QStringLiteral("DspOptionsCacheImpulseSaveRestore"),
                      saveRestore ? QStringLiteral("True") : QStringLiteral("False"));
    auto engine = std::make_unique<WdspEngine>();
    engine->setSynchronousInitForTest(true);
    QVERIFY(engine->initialize(folder + QLatin1Char('/')));
    engine->shutdown();
}

QStringList cacheFiles(const QString& folder)
{
    QStringList files = QDir(folder).entryList({QStringLiteral("impulse_cache*")}, QDir::Files);
    files.sort();
    return files;
}

} // namespace

class TstWdspImpulseCacheProfiles : public QObject {
    Q_OBJECT
private slots:
    void cleanup()
    {
        AppSettings::setProfileOverride(QString());
        AppSettings::instance().remove(QStringLiteral("DspOptionsCacheImpulse"));
        AppSettings::instance().remove(QStringLiteral("DspOptionsCacheImpulseSaveRestore"));
    }

    // The bug: a profile with save-restore off deleted the one shared file
    // another profile had just saved.
    void aProfileWithoutSaveRestoreLeavesAnotherProfilesCache()
    {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());

        runProfile(folder.path(), QStringLiteral("alpha"), true, true);
        if (QTest::currentTestFailed()) { return; }
        const QStringList saved = cacheFiles(folder.path());
        QCOMPARE(saved.size(), 1);

        runProfile(folder.path(), QStringLiteral("bravo"), true, false);
        if (QTest::currentTestFailed()) { return; }
        QVERIFY2(cacheFiles(folder.path()) == saved,
                 qPrintable(QStringLiteral("after bravo closed: [%1], alpha had saved [%2]")
                                .arg(cacheFiles(folder.path()).join(QStringLiteral(", ")),
                                     saved.join(QStringLiteral(", ")))));

        // Turning save-restore off still deletes the profile's own cache.
        runProfile(folder.path(), QStringLiteral("alpha"), true, false);
        if (QTest::currentTestFailed()) { return; }
        QVERIFY2(cacheFiles(folder.path()).isEmpty(),
                 qPrintable(cacheFiles(folder.path()).join(QStringLiteral(", "))));
    }

    // Two profiles that both save keep a cache each; neither one's save
    // takes the other's place.
    void eachSavingProfileKeepsACacheOfItsOwn()
    {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());

        runProfile(folder.path(), QStringLiteral("alpha"), true, true);
        if (QTest::currentTestFailed()) { return; }
        runProfile(folder.path(), QStringLiteral("bravo"), true, true);
        if (QTest::currentTestFailed()) { return; }
        QVERIFY2(cacheFiles(folder.path()).size() == 2,
                 qPrintable(cacheFiles(folder.path()).join(QStringLiteral(", "))));
    }

    // The default profile keeps the file name it always had, so the cache
    // an existing install saved is still its own.
    void theDefaultProfileKeepsItsFileName()
    {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());

        runProfile(folder.path(), QString(), true, true);
        if (QTest::currentTestFailed()) { return; }
        QCOMPARE(cacheFiles(folder.path()), QStringList{QStringLiteral("impulse_cache.bin")});
    }

    // A rebuild of the shared FFT plans clears every profile's cache file,
    // since they were all built on those plans, and nothing else in the
    // folder. (The rebuild itself takes minutes of planning, so this checks
    // the list the engine clears.)
    void aPlansRebuildClearsEveryProfilesCache()
    {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QStringList names{
            QStringLiteral("impulse_cache.bin"),
            QStringLiteral("impulse_cache-alpha.bin"),
            QStringLiteral("impulse_cache-radxa_5c_r3.bin"),
            QStringLiteral("impulse_cache-.bin"),
            QStringLiteral("impulse_cache-a b.bin"),
            QStringLiteral("impulse_cache.bin.tmp"),
            QStringLiteral("impulse_cache_notes.bin"),
            QStringLiteral("wdspWisdom00"),
        };
        for (const QString& name : names) {
            QFile file(folder.filePath(name));
            QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(name));
        }

        QStringList cleared = DspCacheLocation::impulseCacheFileNames(folder.path());
        cleared.sort();
        QCOMPARE(cleared, (QStringList{QStringLiteral("impulse_cache-alpha.bin"),
                                       QStringLiteral("impulse_cache-radxa_5c_r3.bin"),
                                       QStringLiteral("impulse_cache.bin")}));
        QCOMPARE(DspCacheLocation::impulseCacheFileName(QStringLiteral("radxa_5c_r3")),
                 QStringLiteral("impulse_cache-radxa_5c_r3.bin"));
        QCOMPARE(DspCacheLocation::impulseCacheFileName(QStringLiteral("../x")),
                 QStringLiteral("impulse_cache.bin"));
    }
};

QTEST_GUILESS_MAIN(TstWdspImpulseCacheProfiles)
#include "tst_wdsp_impulse_cache_profiles.moc"
