// no-port-check: NereusSDR-original unit-test file.  RADE audio
// routing is NereusSDR-native; no Thetis equivalent.
// =================================================================
// tests/tst_audio_engine_rade.cpp  (NereusSDR)
// =================================================================
//
// Phase 3R Task J4 unit tests: RadeChannel::rxSpeechReady is routed
// into AudioEngine via the same speakers bus path as the WDSP
// RxChannel.  The connection is set up by
// RadioModel::wireRadeChannel(sliceId, channel, slice) (which now
// adds the audio connection on top of the I5 signal graph) and
// emits float32 stereo PCM into AudioEngine::rxBlockReady through
// a per-slice adapter slot.
//
// RadeChannel emits QByteArray of interleaved float32 stereo at
// 24 kHz; AudioEngine::rxBlockReady takes (const float*, int frames)
// for interleaved stereo float.  The adapter slot reinterprets the
// byte buffer as a float pointer and calls through.
//
// Tests use a small RadeChannel test subclass exposing an
// emit-helper for rxSpeechReady so we can drive the audio path
// without standing up the full I2 RX pipeline (which would require
// a live RADE codec + I/Q feed).
//
// Test case (1):
//   1. switchToRadeRoutesAudioToAudioEngine - emitting rxSpeechReady
//      with a small PCM block after wireRadeChannel must push to the
//      FakeAudioBus speakers stand-in.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-11 - New test file for Phase 3R Task J4.  J.J. Boyd
//                 (KG4VCF), with AI-assisted implementation via
//                 Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>

#include "core/AudioEngine.h"
#include "core/IAudioBus.h"
#include "core/RadeChannel.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include "fakes/FakeAudioBus.h"

#include <memory>

using namespace NereusSDR;

namespace {

// Test seam: RadeChannel subclass exposing a thin wrapper around
// emit rxSpeechReady so the test can drive the audio path without
// the I2 RX pipeline standing up a live librade decoder.
class TestableRadeChannel : public RadeChannel {
    Q_OBJECT
public:
    using RadeChannel::RadeChannel;

    void emitRxSpeechReadyForTest(const QByteArray& pcm) {
        emit rxSpeechReady(pcm);
    }
};

// Pre-built 2-frame interleaved-stereo float32 PCM block (16 bytes).
// Matches the kTestSamples shape in tst_audio_engine_rx_leak_during_mox.
QByteArray make2FrameStereoFloatPcm() {
    const float samples[4] = { 0.10f, 0.20f, -0.30f, -0.40f };
    return QByteArray(reinterpret_cast<const char*>(samples),
                      static_cast<int>(sizeof(samples)));
}

} // namespace

class TstAudioEngineRade : public QObject {
    Q_OBJECT

private slots:

    // ── switchToRadeRoutesAudioToAudioEngine ───────────────────────────
    //
    // Wiring contract for J4: after RadioModel::wireRadeChannel(0,
    // channel, slice), emitting RadeChannel::rxSpeechReady with a
    // valid float32 stereo block must reach the speakers bus through
    // AudioEngine::rxBlockReady.  Verified by FakeAudioBus push count.
    void switchToRadeRoutesAudioToAudioEngine() {
        // Standard harness mirrors tst_audio_engine_rx_leak_during_mox:
        // RadioModel + audioEngine + FakeSpeakers injected via
        // setSpeakersBusForTest.
        auto radio = std::make_unique<RadioModel>();
        AudioEngine* engine = radio->audioEngine();

        auto speakers = std::make_unique<FakeAudioBus>(
            QStringLiteral("FakeSpeakers"));
        AudioFormat fmt;
        fmt.sampleRate = 48000;
        fmt.channels   = 2;
        fmt.sample     = AudioFormat::Sample::Float32;
        speakers->open(fmt);
        FakeAudioBus* speakersRaw = speakers.get();
        engine->setSpeakersBusForTest(std::move(speakers));

        const int sliceId = radio->addSlice();
        SliceModel* slice = radio->sliceAt(sliceId);
        QVERIFY(slice != nullptr);

        // Wire the RADE channel.  J4's audio connection is added inside
        // RadioModel::wireRadeChannel alongside the I5 signal graph.
        TestableRadeChannel channel;
        radio->wireRadeChannel(sliceId, &channel, slice);

        // Baseline: speakers bus has not been pushed yet.
        const int baselinePushes = speakersRaw->pushCount();
        QCOMPARE(baselinePushes, 0);

        // Drive the audio path.
        const QByteArray pcm = make2FrameStereoFloatPcm();
        channel.emitRxSpeechReadyForTest(pcm);

        QVERIFY2(speakersRaw->pushCount() > baselinePushes,
                 "wireRadeChannel must connect RadeChannel::rxSpeechReady "
                 "through AudioEngine::rxBlockReady so emitting a valid "
                 "PCM block reaches the speakers bus");
    }
};

QTEST_GUILESS_MAIN(TstAudioEngineRade)
#include "tst_audio_engine_rade.moc"
