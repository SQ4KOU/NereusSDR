// no-port-check: NereusSDR-original Core receive-layout runtime regressions.
//
// R-R3-34: exercise the DaemonApp-owned lifecycle against the real
// AppSettings file.  The primed-board seam deliberately replaces only radio
// discovery/connection; it still uses the real board capability table and
// stream allocator.  RADE worker acceptance belongs in its worker tests.

#include <QtTest/QtTest>

#include <QFile>
#include <QDir>
#include <QScopeGuard>

#include "core/AppSettings.h"
#include "core/ReceiveLayoutStore.h"
#include "core/daemon/DaemonConfig.h"
#define private public
#include "core/daemon/DaemonApp.h"
#undef private
#include "models/RadioModel.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

namespace {

constexpr auto kLayoutKey = "receiveLayout";
const QString kMacA = QStringLiteral("AA:BB:CC:DD:EE:31");
const QString kMacB = QStringLiteral("AA:BB:CC:DD:EE:32");

QList<ReceiveSliceState> twoReceiverLayout()
{
    return {
        {0, QStringLiteral("pan-0"), 14'293'200.0, DSPMode::USB},
        {2, QStringLiteral("pan-1"), 7'200'000.0, DSPMode::LSB},
    };
}

QList<ReceiveSliceState> sparseLayout()
{
    return {
        {2, QStringLiteral("pan-1"), 7'200'000.0, DSPMode::LSB},
        {4, QStringLiteral("pan-3"), 14'250'000.0, DSPMode::USB},
    };
}

bool saveLayout(const QString& mac, const QList<ReceiveSliceState>& layout)
{
    QString error;
    return ReceiveLayoutStore::stage(AppSettings::instance(), mac, layout, &error)
        && AppSettings::instance().save(&error);
}

ReceiveLayoutStore::LoadResult layoutFromDisk(const QString& mac)
{
    AppSettings reloaded(AppSettings::instance().filePath());
    reloaded.load();
    return ReceiveLayoutStore::load(reloaded, mac);
}

QString rawLayoutFromDisk(const QString& mac)
{
    AppSettings reloaded(AppSettings::instance().filePath());
    reloaded.load();
    return reloaded.hardwareValue(AppSettings::normalizedRadioMac(mac),
                                  QLatin1String(kLayoutKey)).toString();
}

void reloadSingletonFromDisk()
{
    // DaemonApp lifetimes do not reset AppSettings' process singleton.  Clear
    // and reload at the boundary so the next daemon observes its predecessor
    // through the actual file, as a process restart would.
    AppSettings::instance().clear();
    AppSettings::instance().load();
}

RadioModel* model(DaemonApp& app)
{
    return app.m_radioModel.get();
}

bool sliceMatches(RadioModel* radio, int id, double frequencyHz,
                  DSPMode mode, const QString& panKey)
{
    SliceModel* const slice = radio->sliceById(id);
    return slice && slice->frequency() == frequencyHz && slice->dspMode() == mode
        && slice->panKey() == panKey;
}

DaemonConfig configWithCount(int count)
{
    DaemonConfig config = DaemonConfig::defaults();
    config.sliceCount = count;
    config.remotePort = 0;
    return config;
}

} // namespace

class TstReceiveLayoutRuntime : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // AppSettings is a process singleton.  Set the profile before its
        // first use so these file-backed checks cannot touch a user profile.
        AppSettings::setProfileOverride(QStringLiteral("receive-layout-runtime-%1")
                                        .arg(QCoreApplication::applicationPid()));
    }

    void init()
    {
        AppSettings::instance().clear();
        QString error;
        QVERIFY2(AppSettings::instance().save(&error), qPrintable(error));
    }

    void cleanupTestCase()
    {
        QFile::remove(AppSettings::instance().filePath());
    }

    void savedMembershipOverridesConfiguredCountAcrossDaemonLifetimes()
    {
        QVERIFY(saveLayout(kMacA, twoReceiverLayout()));

        {
            DaemonApp app;
            app.primeBoardForTest(HPSDRHW::HermesLite, kMacA);
            QVERIFY(app.start(configWithCount(1)));
            RadioModel* const radio = model(app);
            QVERIFY(radio != nullptr);
            QCOMPARE(app.sliceCount(), 2);
            QVERIFY(radio->receiveLayoutOverridesConfiguredCount());
            QCOMPARE(radio->receiveLayoutRestoreState(), QStringLiteral("accepted"));
            // The same top-up helper runs when the selected radio first
            // connects.  A valid saved manifest must keep it from creating
            // a cfg-count receiver after admission has completed.
            app.createConfiguredSlices(3);
            QCOMPARE(app.sliceCount(), 2);
            QVERIFY(sliceMatches(radio, 0, 14'293'200.0, DSPMode::USB,
                                 QStringLiteral("pan-0")));
            QVERIFY(sliceMatches(radio, 2, 7'200'000.0, DSPMode::LSB,
                                 QStringLiteral("pan-1")));
            app.stop();
        }
        reloadSingletonFromDisk();

        // A second DaemonApp must reload membership from the file, not reuse
        // its prior object's topology or top up to cfg.sliceCount.
        {
            DaemonApp app;
            app.primeBoardForTest(HPSDRHW::HermesLite, kMacA);
            QVERIFY(app.start(configWithCount(1)));
            QCOMPARE(app.sliceCount(), 2);
            QVERIFY(model(app)->receiveLayoutOverridesConfiguredCount());
            QVERIFY(sliceMatches(model(app), 2, 7'200'000.0, DSPMode::LSB,
                                 QStringLiteral("pan-1")));
            app.stop();
        }
    }

    void deletionSurvivesRestartDespiteLargerConfiguredCount()
    {
        QVERIFY(saveLayout(kMacA, twoReceiverLayout()));

        {
            DaemonApp app;
            app.primeBoardForTest(HPSDRHW::HermesLite, kMacA);
            QVERIFY(app.start(configWithCount(2)));
            model(app)->removeSlice(2);
            QCOMPARE(app.sliceCount(), 1);
            app.stop(); // must flush the changed membership synchronously.
        }
        reloadSingletonFromDisk();

        const auto saved = layoutFromDisk(kMacA);
        QCOMPARE(static_cast<int>(saved.state),
                 static_cast<int>(ReceiveLayoutStore::LoadState::Loaded));
        QCOMPARE(saved.slices.size(), 1);
        QCOMPARE(saved.slices.first().id, 0);

        DaemonApp app;
        app.primeBoardForTest(HPSDRHW::HermesLite, kMacA);
        QVERIFY(app.start(configWithCount(2)));
        QCOMPARE(app.sliceCount(), 1);
        QVERIFY(model(app)->sliceById(2) == nullptr);
        QVERIFY(model(app)->receiveLayoutOverridesConfiguredCount());
        app.stop();
    }

    void sparseIdsAndDistinctPansReceiveIndependentResources()
    {
        QVERIFY(saveLayout(kMacA, sparseLayout()));

        DaemonApp app;
        app.primeBoardForTest(HPSDRHW::HermesLite, kMacA);
        QVERIFY(app.start(configWithCount(1)));
        RadioModel* const radio = model(app);
        QCOMPARE(app.sliceCount(), 2);
        QVERIFY(radio->sliceById(0) == nullptr);
        QVERIFY(sliceMatches(radio, 2, 7'200'000.0, DSPMode::LSB,
                             QStringLiteral("pan-1")));
        QVERIFY(sliceMatches(radio, 4, 14'250'000.0, DSPMode::USB,
                             QStringLiteral("pan-3")));
        QVERIFY(radio->sliceById(2)->streamIndex() >= 0);
        QVERIFY(radio->sliceById(4)->streamIndex() >= 0);
        QVERIFY(radio->sliceById(2)->streamIndex()
                != radio->sliceById(4)->streamIndex());
        QCOMPARE(radio->receiveLayoutRestoreState(), QStringLiteral("accepted"));
        app.stop();
    }

    void perMacLayoutsRemainIndependent()
    {
        QVERIFY(saveLayout(kMacA, twoReceiverLayout()));
        const QList<ReceiveSliceState> bLayout{
            {1, QStringLiteral("pan-2"), 10'100'000.0, DSPMode::AM},
        };
        QVERIFY(saveLayout(kMacB, bLayout));

        {
            DaemonApp app;
            app.primeBoardForTest(HPSDRHW::HermesLite, kMacA);
            QVERIFY(app.start(configWithCount(1)));
            QCOMPARE(app.sliceCount(), 2);
            QVERIFY(model(app)->sliceById(2) != nullptr);
            app.stop();
        }
        reloadSingletonFromDisk();
        {
            DaemonApp app;
            app.primeBoardForTest(HPSDRHW::HermesLite, kMacB);
            QVERIFY(app.start(configWithCount(3)));
            QCOMPARE(app.sliceCount(), 1);
            QVERIFY(sliceMatches(model(app), 1, 10'100'000.0, DSPMode::AM,
                                 QStringLiteral("pan-2")));
            QVERIFY(model(app)->sliceById(0) == nullptr);
            app.stop();
        }

        const auto a = layoutFromDisk(kMacA);
        const auto b = layoutFromDisk(kMacB);
        QCOMPARE(a.slices.size(), 2);
        QCOMPARE(a.slices.at(1).id, 2);
        QCOMPARE(b.slices.size(), 1);
        QCOMPARE(b.slices.first().id, 1);
    }

    void offlineStartupDoesNotOverwriteSavedLayoutBeforeIdentityDiscovery()
    {
        QVERIFY(saveLayout(kMacA, twoReceiverLayout()));
        const QString original = rawLayoutFromDisk(kMacA);
        QVERIFY(!original.isEmpty());

        DaemonApp app;
        DaemonConfig config = configWithCount(1);
        // No selected MAC, primed board, or discovery responder: startup must
        // await identity rather than create a provisional bootstrap manifest
        // in any saved radio namespace.
        QVERIFY(app.start(config));
        QVERIFY(model(app) != nullptr);
        QCOMPARE(model(app)->receiveLayoutRestoreState(), QStringLiteral("pending"));
        app.stop();

        QCOMPARE(rawLayoutFromDisk(kMacA), original);
    }

    void invalidAndDegradedLayoutsPreserveTheOriginalManifest()
    {
        const QString invalid = QStringLiteral("{not valid json");
        AppSettings::instance().setHardwareValue(AppSettings::normalizedRadioMac(kMacA),
                                                 QLatin1String(kLayoutKey), invalid);
        QVERIFY(AppSettings::instance().save());
        {
            DaemonApp app;
            app.primeBoardForTest(HPSDRHW::HermesLite, kMacA);
            QVERIFY(app.start(configWithCount(1)));
            QCOMPARE(model(app)->receiveLayoutRestoreState(), QStringLiteral("invalid"));
            app.stop();
        }
        QCOMPARE(rawLayoutFromDisk(kMacA), invalid);

        const QList<ReceiveSliceState> oversizedForHermesII{
            {0, QStringLiteral("pan-0"), 14'293'200.0, DSPMode::USB},
            {1, QStringLiteral("pan-1"), 7'200'000.0, DSPMode::LSB},
            {2, QStringLiteral("pan-2"), 10'100'000.0, DSPMode::AM},
        };
        QVERIFY(saveLayout(kMacA, oversizedForHermesII));
        const QString original = rawLayoutFromDisk(kMacA);
        {
            DaemonApp app;
            app.primeBoardForTest(HPSDRHW::HermesII, kMacA);
            QVERIFY(app.start(configWithCount(1)));
            QCOMPARE(model(app)->receiveLayoutRestoreState(), QStringLiteral("degraded"));
            QVERIFY(model(app)->sliceById(0) != nullptr);
            QVERIFY(model(app)->sliceById(1) != nullptr);
            QVERIFY(model(app)->sliceById(2) == nullptr);
            app.stop();
        }
        QCOMPARE(rawLayoutFromDisk(kMacA), original);
    }

    void immediateStopCapturesTunePanAndMembershipWithoutIdZero()
    {
        {
            DaemonApp app;
            app.primeBoardForTest(HPSDRHW::HermesLite, kMacA);
            QVERIFY(app.start(configWithCount(2)));
            RadioModel* const radio = model(app);
            QVERIFY(radio->sliceById(0) != nullptr);
            QVERIFY(radio->sliceById(1) != nullptr);

            // Add and remove a receiver in the same run, then retire A.
            // B remains as the last valid slice, proving capture does not
            // manufacture stable ID 0 during shutdown.
            QCOMPARE(radio->addSlice(QStringLiteral("pan-4")), 2);
            radio->removeSlice(2);
            radio->removeSlice(0);
            SliceModel* const survivor = radio->sliceById(1);
            QVERIFY(survivor != nullptr);
            survivor->setFrequency(3'850'000.0);
            survivor->setDspMode(DSPMode::LSB);
            survivor->setPanKey(QStringLiteral("pan-3"));
            app.stop();
        }

        const auto saved = layoutFromDisk(kMacA);
        QCOMPARE(static_cast<int>(saved.state),
                 static_cast<int>(ReceiveLayoutStore::LoadState::Loaded));
        QCOMPARE(saved.slices.size(), 1);
        QCOMPARE(saved.slices.first().id, 1);
        QCOMPARE(saved.slices.first().frequencyHz, 3'850'000.0);
        QCOMPARE(saved.slices.first().dspMode, DSPMode::LSB);
        QCOMPARE(saved.slices.first().panKey, QStringLiteral("pan-3"));
    }

    void nearbyDistinctPansCannotSilentlyShareOneReceiver()
    {
        const QList<ReceiveSliceState> layout{
            {0, "pan-0", 14293000, DSPMode::USB},
            {2, "pan-1", 14294000, DSPMode::USB},
            {4, "pan-2", 14295000, DSPMode::USB},
        };
        QVERIFY(saveLayout(kMacA, layout));
        const QString original = rawLayoutFromDisk(kMacA);
        DaemonApp app;
        app.primeBoardForTest(HPSDRHW::HermesLite, kMacA); // two user DDCs
        QVERIFY(app.start(configWithCount(1)));
        RadioModel* radio = model(app);
        QCOMPARE(radio->slices().size(), 2);
        QVERIFY(radio->sliceById(0)->streamIndex() != radio->sliceById(2)->streamIndex());
        QVERIFY(!radio->sliceById(4));
        QCOMPARE(radio->receiveLayoutRestoreState(), QStringLiteral("degraded"));
        QVERIFY(radio->receiveLayoutRestoreMessage().contains("Receiver 4 (pan-2)"));
        radio->sliceById(0)->setFrequency(14296000);
        radio->flushPendingSettingsSave();
        app.stop();
        QCOMPARE(rawLayoutFromDisk(kMacA), original);
    }

    void samePanMayShareWhileOtherPanGetsItsOwnStream()
    {
        QVERIFY(saveLayout(kMacA, {{0, "pan-0", 14293000, DSPMode::USB},
                                   {2, "pan-1", 14294000, DSPMode::USB},
                                   {4, "pan-1", 14295000, DSPMode::USB}}));
        DaemonApp app;
        app.primeBoardForTest(HPSDRHW::HermesLite, kMacA);
        QVERIFY(app.start(configWithCount(1)));
        RadioModel* radio = model(app);
        QCOMPARE(radio->receiveLayoutRestoreState(), QStringLiteral("accepted"));
        QCOMPARE(radio->sliceById(2)->streamIndex(), radio->sliceById(4)->streamIndex());
        QVERIFY(radio->sliceById(0)->streamIndex() != radio->sliceById(4)->streamIndex());
        app.stop();
    }

    void laterPanMemberCannotBorrowAnotherPansWindow()
    {
        QVERIFY(saveLayout(kMacA, {{0, "pan-0", 14293000, DSPMode::USB},
                                   {2, "pan-1", 7200000, DSPMode::LSB},
                                   {4, "pan-0", 7201000, DSPMode::LSB}}));
        const QString original = rawLayoutFromDisk(kMacA);
        DaemonApp app;
        app.primeBoardForTest(HPSDRHW::HermesLite, kMacA);
        QVERIFY(app.start(configWithCount(1)));
        RadioModel* radio = model(app);
        QCOMPARE(radio->slices().size(), 2);
        QVERIFY(!radio->sliceById(4));
        QCOMPARE(radio->receiveLayoutRestoreState(), QStringLiteral("degraded"));
        QVERIFY(radio->receiveLayoutRestoreMessage().contains("Receiver 4 (pan-0)"));
        QVERIFY(radio->receiveLayoutRestoreMessage().contains("outside this pan"));
        app.stop();
        QCOMPARE(rawLayoutFromDisk(kMacA), original);
    }

    void allUnsupportedIdsUseExplicitFallbackWithoutErasingSavedState()
    {
        QVERIFY(saveLayout(kMacA, {{4, "pan-3", 7200000, DSPMode::LSB}}));
        const QString original = rawLayoutFromDisk(kMacA);
        DaemonApp app;
        app.primeBoardForTest(HPSDRHW::HermesII, kMacA); // two channel IDs, not five
        QVERIFY(app.start(configWithCount(2)));
        RadioModel* radio = model(app);
        QCOMPARE(radio->slices().size(), 2);
        QVERIFY(!radio->receiveLayoutOverridesConfiguredCount());
        QCOMPARE(radio->receiveLayoutRestoreState(), QStringLiteral("fallback"));
        QVERIFY(radio->receiveLayoutRestoreMessage().contains("Receiver 4 (pan-3)"));
        QVERIFY(radio->sliceById(0));
        QVERIFY(radio->sliceById(0)->streamIndex() >= 0);
        QVERIFY(!radio->sliceById(4));
        app.stop();
        QCOMPARE(rawLayoutFromDisk(kMacA), original);
    }

    void failedAtomicSaveRetainsLiveLayoutForRetry()
    {
        QVERIFY(saveLayout(kMacA, twoReceiverLayout()));
        DaemonApp app;
        app.primeBoardForTest(HPSDRHW::HermesLite, kMacA);
        QVERIFY(app.start(configWithCount(1)));
        RadioModel* radio = model(app);
        radio->sliceById(2)->setFrequency(7225000);
        const QString path = AppSettings::instance().filePath();
        QVERIFY(QFile::remove(path));
        QVERIFY(QDir().mkpath(path));
        const auto removeBlocker = qScopeGuard([path] { QDir().rmdir(path); });
        radio->flushPendingSettingsSave();
        QVERIFY(!radio->settingsSaveError().isEmpty());
        QCOMPARE(radio->sliceById(2)->frequency(), 7225000.0);
        QVERIFY(QDir().rmdir(path));
        radio->flushPendingSettingsSave();
        QVERIFY(radio->settingsSaveError().isEmpty());
        app.stop();
        const auto saved = layoutFromDisk(kMacA);
        QCOMPARE(saved.slices.size(), 2);
        QCOMPARE(saved.slices.at(1).id, 2);
        QCOMPARE(saved.slices.at(1).frequencyHz, 7225000.0);
    }
};

QTEST_MAIN(TstReceiveLayoutRuntime)
#include "tst_receive_layout_runtime.moc"
