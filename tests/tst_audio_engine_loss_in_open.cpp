// =================================================================
// tests/tst_audio_engine_loss_in_open.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  AudioEngine with fake engines, the
// device catalogue and the stream supervisor (R-AUD-03, R-AUD-08): a
// chosen device that goes away while its bus opens leaves Playing.  The
// engine gives the bus its stream event sink before open(), so the loss
// its device thread posts then reaches the supervisor; most buses drop
// an event they have no sink for.  A failed open's events never reach
// the bus that opens after it.
//
// Fake engines only (FakeAudioEngineBackend, FakeMatcherAudioBus).  No
// device is touched.
//
// Modification history (NereusSDR):
//   2026-10-17: new. J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude
//               Code.
// =================================================================

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QStandardPaths>

#include "core/AppSettings.h"
#include "core/AudioDeviceConfig.h"
#include "core/AudioEngine.h"
#include "core/audio/IAudioStreamHost.h"

#include "fakes/FakeAudioEngineBackend.h"

#include <algorithm>
#include <memory>

using namespace NereusSDR;

namespace {

constexpr int kWaitMs = 5000;

const QString kDeskId = QStringLiteral("desk-uid");
const QString kDeskName = QStringLiteral("Desk speakers");
const QString kBuiltInId = QStringLiteral("built-in-uid");

using NoSinkEvent = FakeMatcherAudioBus::NoSinkEvent;

AudioDeviceInfo output(AudioBackendId backend, const QString& id, const QString& name)
{
    AudioDeviceInfo info;
    info.backend = backend;
    info.direction = AudioDeviceDirection::Output;
    info.id = id;
    info.name = name;
    return info;
}

// One engine with the desk speakers and a built-in output (the system
// default when `hasDefault`), the speakers saved on the desk speakers.
// The engine reads the saved choices when it is built.
struct Rig {
    std::shared_ptr<FakeAudioEngineBackend> backend;
    std::unique_ptr<AudioEngine> engine;

    Rig(AudioBackendId id, AudioEngineKind kind, bool hasDefault)
        : backend(std::make_shared<FakeAudioEngineBackend>(id))
    {
        backend->setDevices({output(id, kDeskId, kDeskName),
                             output(id, kBuiltInId, QStringLiteral("Built-in speakers"))});
        if (hasDefault) {
            backend->setDefault(AudioDeviceDirection::Output, kBuiltInId);
        }
        AudioDeviceConfig speakers;
        speakers.engine = kind;
        speakers.deviceId = kDeskId;
        speakers.deviceName = kDeskName;
        speakers.saveToSettings(QStringLiteral("audio/Speakers"));
        engine = std::make_unique<AudioEngine>();
        engine->setVaxOutputsAllowed(false);   // VAX is not under test here
        engine->setAudioBackendsForTest({backend});
    }

    // The device has gone from the list, as the system reports it.
    void unlistDesk()
    {
        backend->removeDevice(kDeskId, AudioDeviceDirection::Output);
        backend->postNotice(AudioNotice::DevicesChanged);
    }

    int opensOn(const QString& deviceId) const
    {
        const std::vector<AudioStreamRequest> requests = backend->outputRequests();
        return int(std::count_if(requests.begin(), requests.end(),
                                 [&](const AudioStreamRequest& r) { return r.deviceId == deviceId; }));
    }
};

// Returns once every event posted to this thread before the call has run.
void runPostedEvents()
{
    bool ran = false;
    QMetaObject::invokeMethod(
        qApp, [&ran]() { ran = true; }, Qt::QueuedConnection);
    QTRY_VERIFY_WITH_TIMEOUT(ran, kWaitMs);
}

} // namespace

class TstAudioEngineLossInOpen : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(QStandardPaths::isTestModeEnabled());
    }

    void init()
    {
        AppSettings::instance().clear();
    }

    void cleanup()
    {
        AppSettings::instance().clear();
    }

    // The speakers' chosen device goes away while its bus opens.  Each
    // engine's bus posts the loss from its own thread: Core Audio's
    // stream watch, PipeWire's and PulseAudio's loss watches and the
    // Windows audio stream thread drop it with no sink set, so before the
    // sink was set ahead of the open these rows stayed Playing on a
    // device that was gone.  ALSA keeps a loss for a sink set later, and
    // ASIO's arrive on this thread once the open has returned (the
    // helper's state, through the capture supervisor's queued signal):
    // those two rows pass with the sink set before the open or after it.
    // With it set before, the ALSA row's bus has its sink and keeps
    // nothing; the row stands for the order the engine used before.
    void chosenDeviceLostAsItOpensLeavesPlaying_data()
    {
        QTest::addColumn<AudioBackendId>("backend");
        QTest::addColumn<AudioEngineKind>("kind");
        QTest::addColumn<bool>("lostInOpen");   // else posted here after the open
        QTest::addColumn<NoSinkEvent>("noSink");
        QTest::addColumn<bool>("hasDefault");
        QTest::addColumn<AudioRoleState>("after");

        QTest::newRow("Core Audio")
            << AudioBackendId::CoreAudio << AudioEngineKind::CoreAudio << true
            << NoSinkEvent::Dropped << true << AudioRoleState::PlayingOnDefault;
        QTest::newRow("PipeWire")
            << AudioBackendId::PipeWire << AudioEngineKind::PipeWire << true
            << NoSinkEvent::Dropped << true << AudioRoleState::PlayingOnDefault;
        QTest::newRow("PulseAudio")
            << AudioBackendId::PulseAudio << AudioEngineKind::PulseAudio << true
            << NoSinkEvent::Dropped << true << AudioRoleState::PlayingOnDefault;
        QTest::newRow("Windows audio")
            << AudioBackendId::Wasapi << AudioEngineKind::WindowsShared << true
            << NoSinkEvent::Dropped << true << AudioRoleState::PlayingOnDefault;
        QTest::newRow("ALSA")
            << AudioBackendId::AlsaDirect << AudioEngineKind::AlsaDirect << true
            << NoSinkEvent::KeptForTheSink << true << AudioRoleState::PlayingOnDefault;
        // An ASIO driver is no system default: the speakers go silent.
        QTest::newRow("ASIO")
            << AudioBackendId::Asio << AudioEngineKind::Asio << false
            << NoSinkEvent::Dropped << false << AudioRoleState::Silent;
    }

    void chosenDeviceLostAsItOpensLeavesPlaying()
    {
        QFETCH(AudioBackendId, backend);
        QFETCH(AudioEngineKind, kind);
        QFETCH(bool, lostInOpen);
        QFETCH(NoSinkEvent, noSink);
        QFETCH(bool, hasDefault);
        QFETCH(AudioRoleState, after);

        Rig rig(backend, kind, hasDefault);
        if (lostInOpen) {
            rig.backend->setOutputLostInOpen(kDeskId, noSink);
        }
        rig.engine->start();
        QCOMPARE(rig.opensOn(kDeskId), 1);
        if (!lostInOpen) {
            rig.backend->setFailingOutputs({kDeskId});   // a reopen finds it gone
            FakeMatcherAudioBus* desk = rig.backend->lastOutput();
            QVERIFY(desk != nullptr);
            AudioStreamEvent lost;
            lost.kind = AudioStreamEvent::Kind::DeviceLost;
            desk->emitEventForTest(lost);
        }
        rig.unlistDesk();

        QTRY_COMPARE_WITH_TIMEOUT(rig.engine->roleStatus(AudioRole::Speakers).state, after, kWaitMs);
        QCOMPARE(rig.engine->roleStatus(AudioRole::Speakers).reason, AudioRoleReason::NotConnected);
        QCOMPARE(rig.opensOn(QString()), hasDefault ? 1 : 0);
        rig.engine->stop();
    }

    // A bus whose open fails posts an event in it (Windows audio posts
    // DeviceBusy there); the speakers fall back to the system default at
    // once, and that event never reaches the default's bus: each bus has
    // a generation of its own, and the role takes it only once its bus
    // has opened.
    void aFailedOpensEventNeverReachesTheNextBus()
    {
        Rig rig(AudioBackendId::CoreAudio, AudioEngineKind::CoreAudio, true);
        rig.backend->setFailingOutputs({kDeskId});
        rig.backend->setOutputLostInOpen(kDeskId, NoSinkEvent::Dropped);
        // The failed open is the case under test.
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("open failed")));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("did not open")));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("could not open")));
        rig.engine->start();
        const std::vector<AudioStreamRequest> opens = rig.backend->outputRequests();
        QCOMPARE(opens.size(), std::size_t(2));
        QCOMPARE(opens.at(0).deviceId, kDeskId);
        QVERIFY(opens.at(1).deviceId.isEmpty());
        QCOMPARE(rig.engine->roleStatus(AudioRole::Speakers).state, AudioRoleState::PlayingOnDefault);

        // Taken by the default's bus, the failed open's loss would reopen
        // the default.
        runPostedEvents();
        QCOMPARE(rig.opensOn(QString()), 1);
        QVERIFY(rig.backend->outputAlive(1));
        QCOMPARE(rig.engine->roleStatus(AudioRole::Speakers).state, AudioRoleState::PlayingOnDefault);
        rig.engine->stop();
    }
};

QTEST_MAIN(TstAudioEngineLossInOpen)
#include "tst_audio_engine_loss_in_open.moc"
