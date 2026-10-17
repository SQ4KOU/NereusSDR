// =================================================================
// tests/tst_port_audio_on_demand.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  PortAudio starts on first need,
// never at launch.  Pa_Initialize walks every audio device; on a system
// that is slow to list them (macOS 27 with Voice Isolation re-applying)
// that held the main thread for many seconds before the window showed,
// for a library most setups never use.  Building an AudioEngine starts
// nothing; the first real need starts it once; the last engine to go
// gives it back.  The catalog lists an on-demand engine (the older
// drivers) only once something asks for its devices.
//
// PortAudio's own Pa_Initialize and Pa_Terminate are replaced with
// counters (PortAudioLibrary::setDriverForTest), so no test here calls
// into PortAudio or touches an audio device.
//
// Modification history (NereusSDR):
//   2026-10-12: J.J. Boyd (KG4VCF), with AI-assisted implementation via
//               Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>
#include <QSignalSpy>

#include "core/AudioEngine.h"
#include "core/audio/AudioDeviceCatalog.h"
#include "core/audio/PortAudioBackend.h"
#include "core/audio/PortAudioBus.h"
#include "core/audio/PortAudioLibrary.h"
#include "core/audio/VirtualCableDetector.h"
#include "fakes/FakeAudioEngineBackend.h"

#include <atomic>
#include <memory>
#include <vector>

using namespace NereusSDR;

namespace {

std::atomic<int> g_starts{0};
std::atomic<int> g_stops{0};

AudioDeviceInfo olderOutput(const QString& name)
{
    AudioDeviceInfo info;
    info.backend = AudioBackendId::PortAudio;
    info.id = portAudioDeviceId(QStringLiteral("MME"), name);
    info.name = name;
    info.hostApi = QStringLiteral("MME");
    info.direction = AudioDeviceDirection::Output;
    info.channelCount = 2;
    return info;
}

AudioDeviceInfo nativeOutput(const QString& name)
{
    AudioDeviceInfo info;
    info.backend = AudioBackendId::Wasapi;
    info.id = name;
    info.name = name;
    info.direction = AudioDeviceDirection::Output;
    info.channelCount = 2;
    return info;
}

// A native engine and the older drivers, the latter listed on demand.
struct Engines {
    std::shared_ptr<FakeAudioEngineBackend> native =
        std::make_shared<FakeAudioEngineBackend>(AudioBackendId::Wasapi);
    std::shared_ptr<FakeAudioEngineBackend> older =
        std::make_shared<FakeAudioEngineBackend>(AudioBackendId::PortAudio);

    Engines()
    {
        native->setDevices({nativeOutput(QStringLiteral("Speakers"))});
        older->setDevices({olderOutput(QStringLiteral("CABLE Input"))});
        older->setListsOnDemand(true);
    }
    std::vector<std::shared_ptr<IAudioEngineBackend>> list() const { return {native, older}; }
};

} // namespace

class TstPortAudioOnDemand : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        PortAudioLibrary::setDriverForTest([] { g_starts.fetch_add(1); return 0; },
                                           [] { g_stops.fetch_add(1); return 0; });
    }

    void cleanupTestCase()
    {
        PortAudioBus::allowPortAudioLibraryForTest(false);
        PortAudioLibrary::setDriverForTest({}, {});
    }

    void init()
    {
        g_starts.store(0);
        g_stops.store(0);
        // As a real run: PortAudio is not barred.  Its start is the
        // counter above, and no device opens (R-AUD-32 still applies).
        PortAudioBus::allowPortAudioLibraryForTest(true);
    }

    void cleanup()
    {
        PortAudioBus::allowPortAudioLibraryForTest(false);
        QVERIFY(!PortAudioLibrary::startedOnDemand());
    }

    void buildingTheEngineStartsNothing()
    {
        {
            AudioEngine engine;
            QCOMPARE(g_starts.load(), 0);
            QVERIFY(!PortAudioLibrary::startedOnDemand());
        }
        QCOMPARE(g_starts.load(), 0);
        QCOMPARE(g_stops.load(), 0);
    }

    void theFirstNeedStartsItOnceAndTheLastEngineGivesItBack()
    {
        {
            AudioEngine first;
            {
                AudioEngine second;
                QVERIFY(PortAudioLibrary::ensureStarted());
                QVERIFY(PortAudioLibrary::ensureStarted());
                QCOMPARE(g_starts.load(), 1);
                QVERIFY(PortAudioLibrary::startedOnDemand());
            }
            // One engine is left: PortAudio stays for it.
            QCOMPARE(g_stops.load(), 0);
            QVERIFY(PortAudioLibrary::startedOnDemand());
        }
        QCOMPARE(g_starts.load(), 1);
        QCOMPARE(g_stops.load(), 1);
        QVERIFY(!PortAudioLibrary::startedOnDemand());
    }

    void aTestRunNeverStartsIt()
    {
        PortAudioBus::allowPortAudioLibraryForTest(false);
        AudioEngine engine;
        QVERIFY(!PortAudioLibrary::ensureStarted());
        QCOMPARE(g_starts.load(), 0);
    }

    void theOlderDriversAreNotListedUntilAsked()
    {
        const Engines engines;
        AudioDeviceCatalog catalog(engines.list());
        catalog.start();
        QVERIFY(catalog.hasListed());
        QCOMPARE(engines.native->enumerateCalls(), 1);
        QCOMPARE(engines.older->enumerateCalls(), 0);
        // Still offered as an engine, with nothing listed and not running,
        // so a reader of every running engine (the cable finder) does not
        // ask for it.
        QVERIFY(catalog.backends().contains(AudioBackendId::PortAudio));
        QVERIFY(!catalog.backendRunning(AudioBackendId::PortAudio));
        QVERIFY(VirtualCableDetector::detect(catalog).isEmpty());
        QTest::qWait(50);
        QCOMPARE(engines.older->enumerateCalls(), 0);
    }

    void readingTheOlderDriversDevicesListsThem()
    {
        const Engines engines;
        AudioDeviceCatalog catalog(engines.list());
        catalog.start();
        QSignalSpy changed(&catalog, &IAudioDeviceCatalog::devicesChanged);

        // The first read finds nothing and asks; the list follows.
        QVERIFY(catalog.devices(AudioBackendId::PortAudio, AudioDeviceDirection::Output).isEmpty());
        QVERIFY(changed.wait(5000));
        const QList<AudioDeviceInfo> listed =
            catalog.devices(AudioBackendId::PortAudio, AudioDeviceDirection::Output);
        QCOMPARE(listed.size(), 1);
        QCOMPARE(listed.first().name, QStringLiteral("CABLE Input"));
        QVERIFY(catalog.backendRunning(AudioBackendId::PortAudio));
        QCOMPARE(engines.older->enumerateCalls(), 1);

        // Reading a native engine's devices never asks.
        const Engines others;
        AudioDeviceCatalog second(others.list());
        second.start();
        QCOMPARE(second.devices(AudioBackendId::Wasapi, AudioDeviceDirection::Output).size(), 1);
        QTest::qWait(50);
        QCOMPARE(others.older->enumerateCalls(), 0);
    }

    void askedBeforeTheStartTheFirstListHasThem()
    {
        const Engines engines;
        AudioDeviceCatalog catalog(engines.list());
        catalog.wantOlderDrivers();
        catalog.start();
        QVERIFY(catalog.hasListed());
        QCOMPARE(catalog.devices(AudioBackendId::PortAudio, AudioDeviceDirection::Output).size(), 1);
        QCOMPARE(engines.older->enumerateCalls(), 1);
    }

    void rescanListsTheOlderDrivers()
    {
        const Engines engines;
        AudioDeviceCatalog catalog(engines.list());
        catalog.start();
        QSignalSpy rescanned(&catalog, &IAudioDeviceCatalog::olderDriversRescanned);
        catalog.rescanOlderDrivers();
        QVERIFY(rescanned.wait(5000));
        QCOMPARE(catalog.devices(AudioBackendId::PortAudio, AudioDeviceDirection::Output).size(), 1);
    }

    void listingTheOlderDriversIsWhatStartsPortAudio()
    {
        int lists = 0;
        const PortAudioListFn list = [&lists] {
            ++lists;
            return QList<PortAudioDeviceRecord>{};
        };
        // The mic helper's engine: listed with the rest, starts nothing.
        PortAudioBackend helper(list, OlderDriverPlatform::Windows, false);
        QVERIFY(!helper.listsOnDemand());
        helper.enumerate();
        QCOMPARE(lists, 1);
        QCOMPARE(g_starts.load(), 0);

        AudioEngine engine;   // the user that gives PortAudio back
        PortAudioBackend onDemand(list, OlderDriverPlatform::Windows, false);
        onDemand.setListsOnDemand(true);
        QVERIFY(onDemand.listsOnDemand());
        onDemand.enumerate();
        onDemand.enumerate();
        QCOMPARE(lists, 3);
        QCOMPARE(g_starts.load(), 1);
    }
};

QTEST_MAIN(TstPortAudioOnDemand)
#include "tst_port_audio_on_demand.moc"
