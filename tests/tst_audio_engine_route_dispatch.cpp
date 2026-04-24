// =================================================================
// tests/tst_audio_engine_route_dispatch.cpp  (NereusSDR)
// =================================================================
//
// Exercises AudioEngine's per-slice sinkNodeName → m_routedBuses dispatch
// logic added in Phase 3O Task 16.
//
// Coverage:
//   1. emptyKeySkipsRoutedPush  — slice with empty sinkNodeName: a fake
//      installed at key "" receives 0 bytes (empty-key fast path fires
//      before the cache lookup).
//   2. namedKeyPushesToCachedBus — slice with sinkNodeName="X": fake at
//      key "X" receives frames*2*sizeof(float) bytes.
//   3. changedKeyRoutesToNewBus — slice starts at key "A", then switches
//      to "B": fake A gets first push only, fake B gets second push only.
//
// Uses the NEREUS_BUILD_TESTS-only AudioEngine::installFakeBusForTest seam
// and the existing FakeAudioBus from tests/fakes/. No real PipeWire daemon
// required — cross-platform.
// =================================================================

#include <QtTest/QtTest>

#include "core/AudioEngine.h"
#include "core/IAudioBus.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include "fakes/FakeAudioBus.h"

#include <array>
#include <memory>

using namespace NereusSDR;

namespace {

// Standard test block: 4 frames of stereo float (8 floats / 32 bytes).
constexpr int kTestFrames = 4;
constexpr int kTestStereoFloats = kTestFrames * 2;
constexpr qint64 kExpectedPushBytes =
    static_cast<qint64>(kTestFrames) * 2 * sizeof(float);

const std::array<float, kTestStereoFloats> kTestSamples = {
    0.10f,  0.20f,  // frame 0 L,R
    -0.30f, -0.40f, // frame 1 L,R
    0.50f,  0.60f,  // frame 2 L,R
    -0.70f, -0.80f, // frame 3 L,R
};

} // namespace

class TstAudioEngineRouteDispatch : public QObject {
    Q_OBJECT

private:
    // Minimal harness: RadioModel + AudioEngine + fake speakers bus so the
    // speakers push in rxBlockReady doesn't crash on a null/closed bus.
    struct Harness {
        std::unique_ptr<RadioModel> radio;
        AudioEngine* engine{nullptr};   // non-owning
        FakeAudioBus* speakers{nullptr}; // non-owning (engine owns it)

        // Add a slice with the given sinkNodeName; returns its index.
        int addSlice(const QString& sinkNodeName = {}) {
            const int idx = radio->addSlice();
            SliceModel* slice = radio->sliceAt(idx);
            slice->setSinkNodeName(sinkNodeName);
            return idx;
        }
    };

    Harness makeHarness() {
        Harness h;
        h.radio = std::make_unique<RadioModel>();
        h.engine = h.radio->audioEngine();

        // Inject fake speakers so the speakers-push step runs safely
        // without a real PortAudio device. We do NOT call engine->start()
        // to avoid constructing platform-native VAX buses in CI.
        auto speakers = std::make_unique<FakeAudioBus>(
            QStringLiteral("FakeSpeakers"));
        AudioFormat fmt;
        fmt.sampleRate = 48000;
        fmt.channels = 2;
        fmt.sample = AudioFormat::Sample::Float32;
        speakers->open(fmt);
        h.speakers = speakers.get();
        h.engine->setSpeakersBusForTest(std::move(speakers));

        return h;
    }

    // Install a pre-opened fake bus at `key` in m_routedBuses.
    // Returns a non-owning pointer for inspection.
    FakeAudioBus* injectRoutedBus(AudioEngine* engine, const QString& key) {
        auto bus = std::make_unique<FakeAudioBus>(key);
        AudioFormat fmt;
        fmt.sampleRate = 48000;
        fmt.channels = 2;
        fmt.sample = AudioFormat::Sample::Float32;
        bus->open(fmt);
        FakeAudioBus* view = bus.get();
        engine->installFakeBusForTest(key, std::move(bus));
        return view;
    }

private slots:
    // Test 1: slice with empty sinkNodeName — the empty-key fast path fires
    // before the cache lookup, so a fake installed at key "" must receive 0
    // bytes (if we could even install one at ""; the fast path means the key
    // is never looked up, so we install at "" as a canary and verify silence).
    void emptyKeySkipsRoutedPush() {
        Harness h = makeHarness();
        // Install a canary fake at "". The empty-key fast path exits before
        // any cache lookup, so this bus must never be pushed to.
        FakeAudioBus* canary = injectRoutedBus(h.engine, QString());
        const int sliceIdx = h.addSlice(); // default empty sinkNodeName
        SliceModel* slice = h.radio->sliceAt(sliceIdx);
        QCOMPARE(slice->sinkNodeName(), QString());

        h.engine->rxBlockReady(sliceIdx, kTestSamples.data(), kTestFrames);

        QCOMPARE(canary->pushCount(), 0);
    }

    // Test 2: slice with sinkNodeName="X" — the routed bus at "X" must
    // receive exactly frames*2*sizeof(float) bytes.
    void namedKeyPushesToCachedBus() {
        Harness h = makeHarness();
        FakeAudioBus* busX = injectRoutedBus(h.engine,
                                              QStringLiteral("X"));
        const int sliceIdx = h.addSlice(QStringLiteral("X"));

        h.engine->rxBlockReady(sliceIdx, kTestSamples.data(), kTestFrames);

        QCOMPARE(busX->pushCount(), 1);
        QCOMPARE(busX->lastPushBytes(), kExpectedPushBytes);
    }

    // Test 3: slice starts at key "A", is pushed, then sinkNodeName switches
    // to "B", and is pushed again. Bus A receives only the first push; bus B
    // receives only the second.
    void changedKeyRoutesToNewBus() {
        Harness h = makeHarness();
        FakeAudioBus* busA = injectRoutedBus(h.engine,
                                              QStringLiteral("A"));
        FakeAudioBus* busB = injectRoutedBus(h.engine,
                                              QStringLiteral("B"));

        const int sliceIdx = h.addSlice(QStringLiteral("A"));
        SliceModel* slice = h.radio->sliceAt(sliceIdx);

        // First push — should land on bus A only.
        h.engine->rxBlockReady(sliceIdx, kTestSamples.data(), kTestFrames);
        QCOMPARE(busA->pushCount(), 1);
        QCOMPARE(busB->pushCount(), 0);

        // Switch to "B".
        slice->setSinkNodeName(QStringLiteral("B"));

        // Second push — should land on bus B only.
        h.engine->rxBlockReady(sliceIdx, kTestSamples.data(), kTestFrames);
        QCOMPARE(busA->pushCount(), 1); // unchanged
        QCOMPARE(busB->pushCount(), 1);
        QCOMPARE(busB->lastPushBytes(), kExpectedPushBytes);
    }
};

QTEST_GUILESS_MAIN(TstAudioEngineRouteDispatch)
#include "tst_audio_engine_route_dispatch.moc"
