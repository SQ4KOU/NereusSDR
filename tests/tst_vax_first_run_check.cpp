// =================================================================
// tests/tst_vax_first_run_check.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original test of the launch check behind the
// VAX first-run dialog and the "new cables" notice (VaxFirstRunCheck,
// R-AUD-03). Fake catalogs only; no device is listed or opened.
//
// What it holds to:
//   1. A catalog that has not listed yet is waited for. Nothing is read
//      or saved until its first list is in, so a system slow to list no
//      longer leaves an empty fingerprint that makes every cable "new" at
//      the next launch.
//   2. The "new cables" notice is asked for only when it has a cable end
//      to offer to a VAX slot. NereusSDR's own VAX devices on a Mac and a
//      cable's recording end alone ask for nothing.
//   3. The first-run dialog asked for is this system's own, with the
//      cables that system's dialog is about.
//
// Modification history (NereusSDR):
//   2026-10-11: written with VaxFirstRunCheck (R-AUD-03). J.J. Boyd
//               (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>

#include <QRegularExpression>

#include "core/AppSettings.h"
#include "core/audio/AudioDeviceCatalog.h"
#include "core/audio/IAudioDeviceCatalog.h"
#include "core/audio/VirtualCableDetector.h"
#include "gui/VaxFirstRunCheck.h"
#include "gui/VaxFirstRunDialog.h"
#include "gui/setup/SoundSystemLine.h"

#include "fakes/FakeAudioEngineBackend.h"

#include <memory>
#include <optional>
#include <vector>

using namespace NereusSDR;

namespace {

using System = SoundSystemLine::System;

const QString kCableAPlay = QStringLiteral("CABLE-A Input (VB-Audio Virtual Cable)");
const QString kCableARecord = QStringLiteral("CABLE-A Output (VB-Audio Virtual Cable)");
const QString kCableBPlay = QStringLiteral("CABLE-B Input (VB-Audio Virtual Cable)");
const QString kCableBRecord = QStringLiteral("CABLE-B Output (VB-Audio Virtual Cable)");
const QString kDante = QStringLiteral("Dante Virtual Soundcard");

const QString kKnownKey = QStringLiteral("audio/LastDetectedCables");
const QString kKnownSourceKey = QStringLiteral("audio/LastDetectedCablesSource");
const QString kFirstRunKey = QStringLiteral("audio/FirstRunComplete");

AudioDeviceInfo device(AudioBackendId backend, AudioDeviceDirection direction, const QString& name)
{
    AudioDeviceInfo info;
    info.backend = backend;
    info.direction = direction;
    info.id = name + QStringLiteral("-uid");
    info.name = name;
    info.channelCount = 2;
    return info;
}

AudioDeviceInfo playback(AudioBackendId backend, const QString& name)
{
    return device(backend, AudioDeviceDirection::Output, name);
}

AudioDeviceInfo recording(AudioBackendId backend, const QString& name)
{
    return device(backend, AudioDeviceDirection::Input, name);
}

QString vaxName(int channel)
{
    return QStringLiteral("NereusSDR VAX %1").arg(channel);
}

// What a Mac lists for NereusSDR's own VAX: four recording devices.
QList<AudioDeviceInfo> ownVaxDevices(AudioBackendId backend)
{
    QList<AudioDeviceInfo> out;
    for (int i = 1; i <= 4; ++i) {
        out << recording(backend, vaxName(i));
    }
    return out;
}

QStringList ownVaxNames()
{
    return {vaxName(1), vaxName(2), vaxName(3), vaxName(4)};
}

// The saved fingerprint of the cables with these names.
QString fingerprintOf(const QStringList& names)
{
    QVector<DetectedCable> cables;
    for (const QString& name : names) {
        cables.push_back({VirtualCableProduct::None, name, false, 0});
    }
    return VirtualCableDetector::fingerprintCsv(cables);
}

QString saved(const QString& key)
{
    return AppSettings::instance().value(key).toString();
}

// The settings of a computer whose first run is done and which knew the
// cables with these names, read from the catalog.
void saveFirstRunDoneKnowing(const QStringList& names)
{
    AppSettings& s = AppSettings::instance();
    s.setValue(kFirstRunKey, QStringLiteral("True"));
    s.setValue(kKnownKey, fingerprintOf(names));
    s.setValue(kKnownSourceKey, VirtualCableDetector::fingerprintSource(true));
}

// A catalog the test holds: one running backend, its devices, and
// whether its first list is in.
class HeldCatalog : public IAudioDeviceCatalog {
public:
    explicit HeldCatalog(AudioBackendId backend, QList<AudioDeviceInfo> listed = {})
        : m_backend(backend)
        , m_devices(std::move(listed))
    {
    }

    QList<AudioBackendId> backends() const override { return {m_backend}; }
    bool backendRunning(AudioBackendId id) const override { return id == m_backend; }
    QList<AudioDeviceInfo> devices(AudioBackendId id, AudioDeviceDirection direction) const override
    {
        QList<AudioDeviceInfo> out;
        for (const AudioDeviceInfo& d : m_devices) {
            if (d.backend == id && d.direction == direction) {
                out.push_back(d);
            }
        }
        return out;
    }
    std::optional<AudioDeviceInfo> defaultDevice(AudioBackendId, AudioDeviceDirection) const override
    {
        return std::nullopt;
    }
    void rescanOlderDrivers() override {}
    bool hasListed() const override { return m_listed; }

    // The state a slow system leaves the engine's catalog in: started,
    // nothing listed yet, so the getters answer as if nothing were
    // installed.
    void holdTheFirstList()
    {
        m_listed = false;
        m_devices.clear();
    }

    // The first list arrives.
    void list(const QList<AudioDeviceInfo>& devices)
    {
        const bool first = !m_listed;
        m_devices = devices;
        m_listed = true;
        emit devicesChanged();
        if (first) {
            emit firstListed();
        }
    }

private:
    AudioBackendId m_backend;
    QList<AudioDeviceInfo> m_devices;
    bool m_listed = true;
};

// What a check asked for.
struct Asked {
    int dialogs = 0;
    FirstRunScenario scenario = FirstRunScenario::WindowsNoCables;
    QStringList cables;
    int finished = 0;
};

void watch(VaxFirstRunCheck& check, Asked& asked)
{
    QObject::connect(&check, &VaxFirstRunCheck::dialogWanted, &check,
                     [&asked](FirstRunScenario scenario, const QVector<DetectedCable>& cables) {
                         ++asked.dialogs;
                         asked.scenario = scenario;
                         asked.cables.clear();
                         for (const DetectedCable& c : cables) {
                             asked.cables << c.deviceName;
                         }
                     });
    QObject::connect(&check, &VaxFirstRunCheck::finished, &check,
                     [&asked]() { ++asked.finished; });
}

} // namespace

class TstVaxFirstRunCheck : public QObject {
    Q_OBJECT

private:
    static void clearAudioKeys()
    {
        AppSettings& s = AppSettings::instance();
        const QStringList keys = s.allKeys();
        for (const QString& k : keys) {
            if (k.startsWith(QStringLiteral("audio/"))) {
                s.remove(k);
            }
        }
    }

private slots:
    void init() { clearAudioKeys(); }
    void cleanup() { clearAudioKeys(); }

    // -- 1. The first list is waited for --------------------------------

    // The defect, as it happened: a launch whose device list was late
    // saved an empty fingerprint, and the next launch, with the list on
    // time, found every cable new.
    void aLateListLeavesTheSavedCablesAlone()
    {
        saveFirstRunDoneKnowing({kCableAPlay});

        // A launch that ends before its list is in.
        {
            HeldCatalog slow(AudioBackendId::Wasapi);
            slow.holdTheFirstList();
            VaxFirstRunCheck check(&slow);
            check.setSystemForTest(System::Windows);
            Asked asked;
            watch(check, asked);
            check.run();
            QVERIFY(check.isWaiting());
            QCOMPARE(asked.dialogs, 0);
            QCOMPARE(asked.finished, 0);
        }
        // Nothing was read, so what was known still is.
        QCOMPARE(saved(kKnownKey), fingerprintOf({kCableAPlay}));

        // The next launch lists on time: the same cable, and it is not new.
        HeldCatalog prompt(AudioBackendId::Wasapi, {playback(AudioBackendId::Wasapi, kCableAPlay)});
        VaxFirstRunCheck check(&prompt);
        check.setSystemForTest(System::Windows);
        Asked asked;
        watch(check, asked);
        check.run();
        QVERIFY(!check.isWaiting());
        QCOMPARE(asked.finished, 1);
        QCOMPARE(asked.dialogs, 0);
        QCOMPARE(saved(kKnownKey), fingerprintOf({kCableAPlay}));
    }

    // The check runs when the list arrives, on what it lists.
    void theCheckRunsWhenTheFirstListArrives()
    {
        saveFirstRunDoneKnowing({kCableAPlay});
        HeldCatalog catalog(AudioBackendId::Wasapi);
        catalog.holdTheFirstList();
        VaxFirstRunCheck check(&catalog);
        check.setSystemForTest(System::Windows);
        Asked asked;
        watch(check, asked);
        check.run();
        QVERIFY(check.isWaiting());
        QCOMPARE(saved(kKnownKey), fingerprintOf({kCableAPlay}));

        catalog.list({playback(AudioBackendId::Wasapi, kCableAPlay),
                        playback(AudioBackendId::Wasapi, kCableBPlay)});
        QVERIFY(!check.isWaiting());
        QCOMPARE(asked.finished, 1);
        QCOMPARE(asked.dialogs, 1);
        QCOMPARE(asked.scenario, FirstRunScenario::RescanNewCables);
        QCOMPARE(asked.cables, QStringList{kCableBPlay});
        QCOMPARE(saved(kKnownKey), fingerprintOf({kCableAPlay, kCableBPlay}));

        // Once: a later list is the device cards' business, not the check's.
        catalog.holdTheFirstList();
        catalog.list({playback(AudioBackendId::Wasapi, kCableAPlay)});
        QCOMPARE(asked.finished, 1);
        QCOMPARE(asked.dialogs, 1);
    }

    // The same through the engine's own catalog over a system that is
    // slow to list: its start gives up after three seconds and goes on
    // with nothing listed.
    void theEnginesCatalogIsWaitedForWhenItsStartGaveUp()
    {
        saveFirstRunDoneKnowing(ownVaxNames());
        auto system = std::make_shared<FakeAudioEngineBackend>(AudioBackendId::CoreAudio);
        system->setDevices(ownVaxDevices(AudioBackendId::CoreAudio));
        std::vector<std::shared_ptr<IAudioEngineBackend>> backends{system};
        AudioDeviceCatalog catalog(std::move(backends));
        system->holdEnumerate();
        QTest::ignoreMessage(
            QtWarningMsg,
            QRegularExpression(QStringLiteral("Audio device list took longer than 3000 ms")));
        catalog.start();
        QVERIFY(!catalog.hasListed());
        QVERIFY(VirtualCableDetector::detect(catalog).isEmpty());

        VaxFirstRunCheck check(&catalog);
        check.setSystemForTest(System::Mac);
        Asked asked;
        watch(check, asked);
        check.run();
        QVERIFY(check.isWaiting());
        QCOMPARE(asked.finished, 0);
        // The empty list was not saved over the four devices known.
        QCOMPARE(saved(kKnownKey), fingerprintOf(ownVaxNames()));

        system->releaseEnumerate();
        QTRY_COMPARE_WITH_TIMEOUT(asked.finished, 1, 5000);
        QVERIFY(catalog.hasListed());
        QCOMPARE(asked.dialogs, 0);
        QCOMPARE(saved(kKnownKey), fingerprintOf(ownVaxNames()));
        catalog.stop();
    }

    // The first-run dialog waits too: asked for at once, it was given the
    // empty list and said no cable was installed.
    void theFirstRunDialogWaitsForTheListToo()
    {
        HeldCatalog catalog(AudioBackendId::Wasapi);
        catalog.holdTheFirstList();
        VaxFirstRunCheck check(&catalog);
        check.setSystemForTest(System::Windows);
        Asked asked;
        watch(check, asked);
        check.run();
        QCOMPARE(asked.dialogs, 0);
        QVERIFY(!AppSettings::instance().contains(kKnownKey));

        catalog.list({playback(AudioBackendId::Wasapi, kCableAPlay)});
        QCOMPARE(asked.dialogs, 1);
        QCOMPARE(asked.scenario, FirstRunScenario::WindowsCablesFound);
        QCOMPARE(asked.cables, QStringList{kCableAPlay});
        QCOMPARE(saved(kKnownKey), fingerprintOf({kCableAPlay}));
        QCOMPARE(saved(kKnownSourceKey), VirtualCableDetector::fingerprintSource(true));
    }

    void aSecondRunDoesNothing()
    {
        saveFirstRunDoneKnowing({});
        HeldCatalog catalog(AudioBackendId::Wasapi, {playback(AudioBackendId::Wasapi, kCableAPlay)});
        VaxFirstRunCheck check(&catalog);
        check.setSystemForTest(System::Windows);
        Asked asked;
        watch(check, asked);
        check.run();
        check.run();
        QCOMPARE(asked.finished, 1);
        QCOMPARE(asked.dialogs, 1);
    }

    // A catalog the engine let go before the check ran: nothing was
    // read, so nothing is saved.
    void aCatalogLetGoSavesNothing()
    {
        saveFirstRunDoneKnowing({kCableAPlay});
        auto catalog = std::make_unique<HeldCatalog>(AudioBackendId::Wasapi);
        VaxFirstRunCheck check(catalog.get());
        check.setSystemForTest(System::Windows);
        Asked asked;
        watch(check, asked);
        catalog.reset();
        check.run();
        QCOMPARE(asked.finished, 1);
        QCOMPARE(asked.dialogs, 0);
        QCOMPARE(saved(kKnownKey), fingerprintOf({kCableAPlay}));
    }

    // -- 2. The notice opens only with something to bind ----------------

    // What a Mac showed: four VAX devices seen for the first time, in a
    // notice that counted "0 new" cables and offered to bind them. They
    // are recording devices NereusSDR feeds directly; there is nothing in
    // them to bind.
    void ownVaxDevicesSeenForTheFirstTimeAskForNoNotice()
    {
        saveFirstRunDoneKnowing({});
        HeldCatalog catalog(AudioBackendId::CoreAudio, ownVaxDevices(AudioBackendId::CoreAudio));
        VaxFirstRunCheck check(&catalog);
        check.setSystemForTest(System::Mac);
        Asked asked;
        watch(check, asked);
        check.run();
        QCOMPARE(asked.finished, 1);
        QCOMPARE(asked.dialogs, 0);
        // They are known from here on.
        QCOMPARE(saved(kKnownKey), fingerprintOf(ownVaxNames()));
    }

    void ownVaxDevicesOnLinuxAskForNoNoticeEither()
    {
        saveFirstRunDoneKnowing({});
        HeldCatalog catalog(AudioBackendId::PipeWire, ownVaxDevices(AudioBackendId::PipeWire));
        VaxFirstRunCheck check(&catalog);
        check.setSystemForTest(System::Linux);
        Asked asked;
        watch(check, asked);
        check.run();
        QCOMPARE(asked.finished, 1);
        QCOMPARE(asked.dialogs, 0);
    }

    // A new third-party cable on Windows is what the notice is for.
    void aNewCableOnWindowsAsksForTheNotice()
    {
        saveFirstRunDoneKnowing({kCableAPlay, kCableARecord});
        HeldCatalog catalog(AudioBackendId::Wasapi,
                                {playback(AudioBackendId::Wasapi, kCableAPlay),
                                 recording(AudioBackendId::Wasapi, kCableARecord),
                                 playback(AudioBackendId::Wasapi, kCableBPlay),
                                 recording(AudioBackendId::Wasapi, kCableBRecord)});
        VaxFirstRunCheck check(&catalog);
        check.setSystemForTest(System::Windows);
        Asked asked;
        watch(check, asked);
        check.run();
        QCOMPARE(asked.dialogs, 1);
        QCOMPARE(asked.scenario, FirstRunScenario::RescanNewCables);
        QCOMPARE(asked.cables, (QStringList{kCableBPlay, kCableBRecord}));
        QCOMPARE(asked.finished, 1);
    }

    // A cable's recording end alone is the other program's side.
    void aNewRecordingEndAloneAsksForNoNotice()
    {
        saveFirstRunDoneKnowing({kCableAPlay});
        HeldCatalog catalog(AudioBackendId::Wasapi,
                                {playback(AudioBackendId::Wasapi, kCableAPlay),
                                 recording(AudioBackendId::Wasapi, kCableARecord)});
        VaxFirstRunCheck check(&catalog);
        check.setSystemForTest(System::Windows);
        Asked asked;
        watch(check, asked);
        check.run();
        QCOMPARE(asked.finished, 1);
        QCOMPARE(asked.dialogs, 0);
    }

    void nothingNewAsksForNothing()
    {
        saveFirstRunDoneKnowing({kCableAPlay});
        HeldCatalog catalog(AudioBackendId::Wasapi, {playback(AudioBackendId::Wasapi, kCableAPlay)});
        VaxFirstRunCheck check(&catalog);
        check.setSystemForTest(System::Windows);
        Asked asked;
        watch(check, asked);
        check.run();
        QCOMPARE(asked.finished, 1);
        QCOMPARE(asked.dialogs, 0);
    }

    // -- 3. The first-run dialog is this system's own -------------------

    void firstRunOnWindowsWithNoCable()
    {
        HeldCatalog catalog(AudioBackendId::Wasapi,
                                {playback(AudioBackendId::Wasapi, QStringLiteral("Desk speakers"))});
        VaxFirstRunCheck check(&catalog);
        check.setSystemForTest(System::Windows);
        Asked asked;
        watch(check, asked);
        check.run();
        QCOMPARE(asked.dialogs, 1);
        QCOMPARE(asked.scenario, FirstRunScenario::WindowsNoCables);
        QVERIFY(asked.cables.isEmpty());
        QCOMPARE(asked.finished, 1);
        // Nothing is marked complete until the operator answers.
        QVERIFY(!AppSettings::instance().contains(kFirstRunKey));
    }

    void firstRunOnWindowsWithCables()
    {
        HeldCatalog catalog(AudioBackendId::Wasapi,
                                {playback(AudioBackendId::Wasapi, kCableAPlay),
                                 recording(AudioBackendId::Wasapi, kCableARecord)});
        VaxFirstRunCheck check(&catalog);
        check.setSystemForTest(System::Windows);
        Asked asked;
        watch(check, asked);
        check.run();
        QCOMPARE(asked.dialogs, 1);
        QCOMPARE(asked.scenario, FirstRunScenario::WindowsCablesFound);
        QCOMPARE(asked.cables, (QStringList{kCableAPlay, kCableARecord}));
    }

    // On a Mac the dialog is about NereusSDR's own devices; a third-party
    // cable is not its business.
    void firstRunOnAMacIsAboutItsOwnDevices()
    {
        QList<AudioDeviceInfo> listed = ownVaxDevices(AudioBackendId::CoreAudio);
        listed << playback(AudioBackendId::CoreAudio, kDante);
        HeldCatalog catalog(AudioBackendId::CoreAudio, listed);
        VaxFirstRunCheck check(&catalog);
        check.setSystemForTest(System::Mac);
        Asked asked;
        watch(check, asked);
        check.run();
        QCOMPARE(asked.dialogs, 1);
        QCOMPARE(asked.scenario, FirstRunScenario::MacNative);
        QCOMPARE(asked.cables, ownVaxNames());
        QCOMPARE(saved(kKnownKey), fingerprintOf(ownVaxNames()));
    }

    void firstRunOnLinux()
    {
        HeldCatalog catalog(AudioBackendId::PipeWire);
        VaxFirstRunCheck check(&catalog);
        check.setSystemForTest(System::Linux);
        Asked asked;
        watch(check, asked);
        check.run();
        QCOMPARE(asked.dialogs, 1);
        QCOMPARE(asked.scenario, FirstRunScenario::LinuxNative);
        QVERIFY(asked.cables.isEmpty());
    }

    void cablesForEachSystem()
    {
        QVector<DetectedCable> all;
        all.push_back({VirtualCableProduct::VbCableA, kCableAPlay, false, 0});
        all.push_back({VirtualCableProduct::NereusSdrVax, vaxName(1), true, 1});
        const auto names = [](const QVector<DetectedCable>& cables) {
            QStringList out;
            for (const DetectedCable& c : cables) {
                out << c.deviceName;
            }
            return out;
        };
        QCOMPARE(names(VaxFirstRunCheck::cablesFor(System::Windows, all)), QStringList{kCableAPlay});
        QCOMPARE(names(VaxFirstRunCheck::cablesFor(System::Mac, all)), QStringList{vaxName(1)});
        QCOMPARE(names(VaxFirstRunCheck::cablesFor(System::Linux, all)), QStringList{vaxName(1)});
    }

    // -- 4. Without a catalog -----------------------------------------

    // No engine catalog (a build without the native engines): the older
    // scan is read at once, and its fingerprint says where it came from.
    void withoutACatalogTheOlderScanIsReadAtOnce()
    {
        VaxFirstRunCheck check(nullptr);
        check.setSystemForTest(System::Windows);
        check.setScanForTest([]() {
            return QVector<DetectedCable>{{VirtualCableProduct::VbCableA, kCableAPlay, false, 0}};
        });
        Asked asked;
        watch(check, asked);
        check.run();
        QVERIFY(!check.isWaiting());
        QCOMPARE(asked.finished, 1);
        QCOMPARE(asked.dialogs, 1);
        QCOMPARE(asked.scenario, FirstRunScenario::WindowsCablesFound);
        QCOMPARE(saved(kKnownKey), fingerprintOf({kCableAPlay}));
        QCOMPARE(saved(kKnownSourceKey), VirtualCableDetector::fingerprintSource(false));
    }

    // A fingerprint from the catalog is not compared with the older
    // scan's names (they differ for the same cable).
    void anotherListsFingerprintFlagsNothing()
    {
        saveFirstRunDoneKnowing({});
        VaxFirstRunCheck check(nullptr);
        check.setSystemForTest(System::Windows);
        check.setScanForTest([]() {
            return QVector<DetectedCable>{{VirtualCableProduct::VbCableA, kCableAPlay, false, 0}};
        });
        Asked asked;
        watch(check, asked);
        check.run();
        QCOMPARE(asked.finished, 1);
        QCOMPARE(asked.dialogs, 0);
    }
};

QTEST_MAIN(TstVaxFirstRunCheck)
#include "tst_vax_first_run_check.moc"
