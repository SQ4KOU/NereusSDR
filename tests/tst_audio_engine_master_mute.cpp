// =================================================================
// tests/tst_audio_engine_master_mute.cpp  (NereusSDR)
// =================================================================
//
// Exercises AudioEngine's master-mute API — Phase 3O Sub-Phase 10
// Task 10a.
//
// Coverage: see private-slot test methods below — each verifies one
// contract scenario for the setMasterMuted / masterMuted /
// masterMutedChanged trio added in Sub-Phase 10.
//
// Uses FakeAudioBus injected via the NEREUS_BUILD_TESTS-only
// AudioEngine::setVaxBusForTest / setSpeakersBusForTest seam so the
// test doesn't need a real CoreAudioHalBus / LinuxPipeBus / PortAudio
// backend. Cross-platform. Modeled on tst_audio_engine_vax_tee.cpp.
//
// Design spec: docs/architecture/2026-04-19-vax-design.md §5.4
// (audio/Master/Muted key) + §6.3 (MasterOutputWidget mute button
// wiring row).
// =================================================================

#include <QtTest/QtTest>
#include <QSignalSpy>

#include "core/AudioEngine.h"
#include "core/IAudioBus.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include "fakes/FakeAudioBus.h"

#include <array>
#include <memory>

using namespace NereusSDR;

namespace {

// Standard test block: 2 frames of stereo float (4 floats / 16 bytes).
constexpr int kTestFrames = 2;
constexpr int kTestStereoFloats = kTestFrames * 2;

const std::array<float, kTestStereoFloats> kTestSamples = {
    0.10f, 0.20f,   // frame 0  L,R
    -0.30f, -0.40f, // frame 1  L,R
};

} // namespace

class TstAudioEngineMasterMute : public QObject {
    Q_OBJECT

private:
    // Mirrors the Harness used by tst_audio_engine_vax_tee.cpp. Builds
    // a RadioModel + AudioEngine pair and injects a fake speakers bus
    // so the speaker push() in rxBlockReady is observable.
    struct Harness {
        std::unique_ptr<RadioModel> radio;
        AudioEngine* engine;        // non-owning view
        FakeAudioBus* speakers;     // non-owning view (engine owns it)

        // Add a slice with the given vaxChannel value; returns its index.
        int addSlice(int vaxChannel) {
            const int idx = radio->addSlice();
            SliceModel* slice = radio->sliceAt(idx);
            slice->setVaxChannel(vaxChannel);
            return idx;
        }
    };

    Harness makeHarness() {
        Harness h;
        h.radio = std::make_unique<RadioModel>();
        h.engine = h.radio->audioEngine();

        auto speakers = std::make_unique<FakeAudioBus>(QStringLiteral("FakeSpeakers"));
        AudioFormat fmt;
        fmt.sampleRate = 48000;
        fmt.channels = 2;
        fmt.sample = AudioFormat::Sample::Float32;
        speakers->open(fmt);
        h.speakers = speakers.get();
        h.engine->setSpeakersBusForTest(std::move(speakers));

        return h;
    }

    FakeAudioBus* injectFakeVax(AudioEngine* engine, int channel) {
        auto bus = std::make_unique<FakeAudioBus>(
            QStringLiteral("FakeVax%1").arg(channel));
        AudioFormat fmt;
        fmt.sampleRate = 48000;
        fmt.channels = 2;
        fmt.sample = AudioFormat::Sample::Float32;
        bus->open(fmt);
        FakeAudioBus* view = bus.get();
        engine->setVaxBusForTest(channel, std::move(bus));
        return view;
    }

private slots:

    // ── 1. Fresh AudioEngine: masterMuted() defaults to false ──────────────

    void defaultsToUnmuted() {
        AudioEngine engine;
        QCOMPARE(engine.masterMuted(), false);
    }

    // ── 2. setMasterMuted(true) gates the speakers push ────────────────────

    void setMutedGatesSpeakersPush() {
        Harness h = makeHarness();
        const int s = h.addSlice(/*vaxChannel=*/0);

        h.engine->setMasterMuted(true);
        h.engine->rxBlockReady(s, kTestSamples.data(), kTestFrames);

        QCOMPARE(h.speakers->pushCount(), 0);
    }

    // ── 3. Mute must NOT gate the VAX tap — 3rd-party apps consuming VAX
    //      (WSJT-X, DAWs, etc.) expect audio regardless of the local
    //      monitor mute state. Per-channel VAX mute is the applet's
    //      concern, not AudioEngine's. ──────────────────────────────────────

    void setMutedDoesNotGateVaxTap() {
        Harness h = makeHarness();
        FakeAudioBus* vax1 = injectFakeVax(h.engine, 1);

        const int s = h.addSlice(/*vaxChannel=*/1);

        h.engine->setMasterMuted(true);
        h.engine->rxBlockReady(s, kTestSamples.data(), kTestFrames);

        QCOMPARE(vax1->pushCount(), 1);
        // Sanity: speakers is still gated.
        QCOMPARE(h.speakers->pushCount(), 0);
    }

    // ── 4. Clearing mute restores the speakers push path ───────────────────

    void clearMuteRestoresPush() {
        Harness h = makeHarness();
        const int s = h.addSlice(/*vaxChannel=*/0);

        h.engine->setMasterMuted(true);
        h.engine->rxBlockReady(s, kTestSamples.data(), kTestFrames);
        QCOMPARE(h.speakers->pushCount(), 0);

        h.engine->setMasterMuted(false);
        h.engine->rxBlockReady(s, kTestSamples.data(), kTestFrames);
        QCOMPARE(h.speakers->pushCount(), 1);
    }

    // ── 5. masterMutedChanged emits once per distinct state change ─────────

    void emitsSignalOnChange() {
        AudioEngine engine;
        QSignalSpy spy(&engine, &AudioEngine::masterMutedChanged);

        engine.setMasterMuted(true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toBool(), true);

        // Redundant set — same value, no emission.
        engine.setMasterMuted(true);
        QCOMPARE(spy.count(), 0);
    }

    // ── 6. setMasterMuted(false) when already false is a no-op signal ──────

    void noSignalOnNoChange() {
        AudioEngine engine;
        QCOMPARE(engine.masterMuted(), false);

        QSignalSpy spy(&engine, &AudioEngine::masterMutedChanged);
        engine.setMasterMuted(false);
        QCOMPARE(spy.count(), 0);
    }
};

QTEST_MAIN(TstAudioEngineMasterMute)
#include "tst_audio_engine_master_mute.moc"
