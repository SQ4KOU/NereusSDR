// no-port-check: NereusSDR-original unit-test file.  The Thetis cite
// comments below identify which upstream lines each assertion verifies;
// no Thetis logic is ported in this test file.
// =================================================================
// tests/tst_audio_engine_mic_block_ready.cpp  (NereusSDR)
// =================================================================
//
// Unit tests for AudioEngine::micBlockReady signal + clearMicBuffer slot
// (Phase 3M-1c D.1 / D.2).
//
// Source references:
//   cmaster.cs:493-497 [v2.10.3.13] — int[] cmInboundSize defines the mic
//     stream as 720 samples per block at 48 kHz (= 15 ms).  NereusSDR
//     matches this exactly per pre-code review §0.5 lock.
//   cmaster.cs:514-518 [v2.10.3.13] — mic input rate hardcoded to 48 kHz.
//
// The signal is the foundation that Phase E uses to convert TxChannel from
// pull-driven (QTimer + pullTxMic) to push-driven (slot connected to
// micBlockReady via Qt::DirectConnection).  D adds the plumbing; E will
// wire the consumer.
// =================================================================

#include <QtTest/QtTest>

#include "core/AudioEngine.h"
#include "core/IAudioBus.h"

#include "fakes/FakeAudioBus.h"

#include <cstring>
#include <vector>

using namespace NereusSDR;

namespace {

// 720-sample mono buffer in Float32 stereo on the wire (1440 floats).
// Each frame is (sample, sample) so left-channel pull returns the input.
QByteArray makeFloat32StereoBytes_nFrames(int frames, float startValue, float step)
{
    QByteArray out;
    out.resize(frames * 2 * 4);  // 2 channels × 4 bytes
    float* p = reinterpret_cast<float*>(out.data());
    float v = startValue;
    for (int i = 0; i < frames; ++i) {
        p[i * 2 + 0] = v;  // left
        p[i * 2 + 1] = v;  // right
        v += step;
    }
    return out;
}

}  // namespace

class TstAudioEngineMicBlockReady : public QObject {
    Q_OBJECT

private:
    struct Harness {
        std::unique_ptr<AudioEngine> engine;
        FakeAudioBus* bus;  // non-owning
    };

    static Harness makeHarness() {
        Harness h;
        h.engine = std::make_unique<AudioEngine>();
        AudioFormat fmt{};
        fmt.sample = AudioFormat::Sample::Float32;
        fmt.channels = 2;
        fmt.sampleRate = 48000;

        auto fakeBus = std::make_unique<FakeAudioBus>(QStringLiteral("FakeMic"));
        fakeBus->open(fmt);
        h.bus = fakeBus.get();
        h.engine->setTxInputBusForTest(std::move(fakeBus));
        return h;
    }

private slots:

    // ── Constant sanity ────────────────────────────────────────────────────

    void constants_micBlockFrames_is720() {
        // From Thetis cmaster.cs:495 [v2.10.3.13]:
        //   int[] cmInboundSize = new int[8] { 240, 240, 240, 240, 240, 720, 240, 240 };
        // Index 5 (mic stream) = 720 samples.
        QCOMPARE(AudioEngine::kMicBlockFrames, 720);
    }

    // ── Single block emit ──────────────────────────────────────────────────

    void emitsOnce_after_720_samples_pulled() {
        Harness h = makeHarness();
        QSignalSpy spy(h.engine.get(), &AudioEngine::micBlockReady);

        h.bus->setPullData(makeFloat32StereoBytes_nFrames(720, 0.0f, 1.0f));

        std::vector<float> dst(720);
        const int got = h.engine->pullTxMic(dst.data(), 720);
        QCOMPARE(got, 720);
        QCOMPARE(spy.count(), 1);
    }

    // ── Partial fill should NOT emit ───────────────────────────────────────

    void doesNotEmit_on_partial_fill() {
        Harness h = makeHarness();
        QSignalSpy spy(h.engine.get(), &AudioEngine::micBlockReady);

        h.bus->setPullData(makeFloat32StereoBytes_nFrames(360, 0.0f, 1.0f));

        std::vector<float> dst(360);
        const int got = h.engine->pullTxMic(dst.data(), 360);
        QCOMPARE(got, 360);
        QCOMPARE(spy.count(), 0);  // accumulator only at 360 / 720
    }

    // ── Two blocks emit ────────────────────────────────────────────────────

    void emitsTwice_after_1440_samples_pulled() {
        Harness h = makeHarness();
        QSignalSpy spy(h.engine.get(), &AudioEngine::micBlockReady);

        h.bus->setPullData(makeFloat32StereoBytes_nFrames(1440, 0.0f, 1.0f));

        std::vector<float> dst(1440);
        const int got = h.engine->pullTxMic(dst.data(), 1440);
        QCOMPARE(got, 1440);
        QCOMPARE(spy.count(), 2);
    }

    // ── Partial then top-up emits ──────────────────────────────────────────

    void emitsAfter_two_partial_pulls_total_720() {
        Harness h = makeHarness();
        QSignalSpy spy(h.engine.get(), &AudioEngine::micBlockReady);

        // First pull: 360 frames — should not emit.
        h.bus->setPullData(makeFloat32StereoBytes_nFrames(360, 0.0f, 1.0f));
        std::vector<float> dst1(360);
        h.engine->pullTxMic(dst1.data(), 360);
        QCOMPARE(spy.count(), 0);

        // Second pull: another 360 frames — total accumulator = 720, should emit.
        h.bus->setPullData(makeFloat32StereoBytes_nFrames(360, 360.0f, 1.0f));
        std::vector<float> dst2(360);
        h.engine->pullTxMic(dst2.data(), 360);
        QCOMPARE(spy.count(), 1);
    }

    // ── clearMicBuffer drops partial fill ──────────────────────────────────

    void clearMicBuffer_drops_partial_accumulation() {
        Harness h = makeHarness();
        QSignalSpy spy(h.engine.get(), &AudioEngine::micBlockReady);

        // Partial fill: 360 frames in.
        h.bus->setPullData(makeFloat32StereoBytes_nFrames(360, 0.0f, 1.0f));
        std::vector<float> dst1(360);
        h.engine->pullTxMic(dst1.data(), 360);
        QCOMPARE(spy.count(), 0);

        // Clear — accumulator goes back to 0.
        h.engine->clearMicBuffer();

        // Now pull 360 more — should still NOT emit (total is 360 post-clear).
        h.bus->setPullData(makeFloat32StereoBytes_nFrames(360, 0.0f, 1.0f));
        std::vector<float> dst2(360);
        h.engine->pullTxMic(dst2.data(), 360);
        QCOMPARE(spy.count(), 0);

        // One more 360 to reach 720 post-clear.
        h.bus->setPullData(makeFloat32StereoBytes_nFrames(360, 360.0f, 1.0f));
        std::vector<float> dst3(360);
        h.engine->pullTxMic(dst3.data(), 360);
        QCOMPARE(spy.count(), 1);
    }

    // ── Slot receives correct sample count ─────────────────────────────────

    void slot_receives_kMicBlockFrames_count() {
        Harness h = makeHarness();

        int receivedFrames = -1;
        QObject::connect(h.engine.get(), &AudioEngine::micBlockReady,
                         h.engine.get(),
                         [&receivedFrames](const float* samples, int frames) {
                             Q_UNUSED(samples);
                             receivedFrames = frames;
                         },
                         Qt::DirectConnection);

        h.bus->setPullData(makeFloat32StereoBytes_nFrames(720, 0.0f, 1.0f));
        std::vector<float> dst(720);
        h.engine->pullTxMic(dst.data(), 720);

        QCOMPARE(receivedFrames, AudioEngine::kMicBlockFrames);
    }

    // ── Slot receives accumulated samples in order ─────────────────────────

    void slot_receives_samples_in_order() {
        Harness h = makeHarness();

        std::vector<float> captured;
        QObject::connect(h.engine.get(), &AudioEngine::micBlockReady,
                         h.engine.get(),
                         [&captured](const float* samples, int frames) {
                             captured.assign(samples, samples + frames);
                         },
                         Qt::DirectConnection);

        // Inject samples 0.0, 1.0, 2.0, ... 719.0 (left channel).
        h.bus->setPullData(makeFloat32StereoBytes_nFrames(720, 0.0f, 1.0f));
        std::vector<float> dst(720);
        h.engine->pullTxMic(dst.data(), 720);

        QCOMPARE(static_cast<int>(captured.size()), 720);
        QCOMPARE(captured[0],   0.0f);
        QCOMPARE(captured[1],   1.0f);
        QCOMPARE(captured[359], 359.0f);
        QCOMPARE(captured[719], 719.0f);
    }
};

QTEST_APPLESS_MAIN(TstAudioEngineMicBlockReady)
#include "tst_audio_engine_mic_block_ready.moc"
