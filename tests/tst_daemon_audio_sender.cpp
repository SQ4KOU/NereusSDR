// =================================================================
// tests/tst_daemon_audio_sender.cpp  (NereusSDR)
// =================================================================
// DaemonAudioSender owns only capture-to-Opus packetisation.  These tests
// drive the real AudioEngine/MasterMixer path and decode the emitted RTP.
// =================================================================

#include <QtTest>

#include "core/AudioEngine.h"
#include "core/session/media/DaemonAudioSender.h"
#include "core/session/media/DaemonAudioSource.h"
#include "core/session/media/OpusAudioCodec.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <cmath>

using namespace NereusSDR;

namespace {

constexpr int kDspFrames = 64;
constexpr quint32 kSsrc = 0x6e657265U;

QVector<float> stereoBlock(float left, float right)
{
    QVector<float> block(kDspFrames * 2);
    for (int frame = 0; frame < kDspFrames; ++frame) {
        block[frame * 2] = left;
        block[frame * 2 + 1] = right;
    }
    return block;
}

struct Harness {
    RadioModel radio;
    AudioEngine* engine{nullptr};
    int sliceA{-1};
    int sliceB{-1};

    Harness()
    {
        radio.configureStreamPool(/*userDdcCount=*/5, /*maxSlices=*/5,
                                  /*defaultRateHz=*/192000);
        engine = radio.audioEngine();
        Q_ASSERT(engine != nullptr);
        engine->masterMixForTest().setRampFrames(1);
        engine->masterMixForTest().setSlewUpFrames(0);
        sliceA = radio.addSlice();
        sliceB = radio.addSlice();
        Q_ASSERT(sliceA >= 0 && sliceB >= 0);
        engine->setSliceStreaming(sliceA, true);
        engine->setSliceStreaming(sliceB, true);
    }

    void feedMixed(int frames, float aLeft, float aRight, float bLeft, float bRight)
    {
        const QVector<float> a = stereoBlock(aLeft, aRight);
        const QVector<float> b = stereoBlock(bLeft, bRight);
        for (int delivered = 0; delivered < frames; delivered += kDspFrames) {
            engine->rxBlockReady(sliceA, a.constData(), kDspFrames);
            engine->rxBlockReady(sliceB, b.constData(), kDspFrames);
        }
    }

    void primeBarrier()
    {
        feedMixed(kDspFrames * 2, 0.40f, 0.40f, 0.40f, 0.40f);
    }
};

double energy(const QVector<float>& pcm, int channel)
{
    double total = 0.0;
    for (int frame = 0; frame < OpusAudioCodecConfig::kFrameSamples; ++frame) {
        const double sample = pcm.at(frame * 2 + channel);
        total += sample * sample;
    }
    return total;
}

QByteArray packetAt(const QSignalSpy& packets, int index)
{
    return packets.at(index).at(0).toByteArray();
}

} // namespace

class TstDaemonAudioSender final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        OpusAudioEncoder encoder;
        if (!encoder.isReady()) {
            QSKIP("Opus encoder is unavailable in this build");
        }
    }

    void encodesRealMasterMixWithPerSliceGainPanAndMute()
    {
        Harness h;
        // A supplies left only; B supplies right only.  Both are real slice
        // programs, mixed before DaemonAudioSource captures the station bus.
        h.engine->masterMixForTest().setSliceGain(h.sliceA, 0.5f, -1.0f);
        h.engine->masterMixForTest().setSliceGain(h.sliceB, 0.5f, 1.0f);
        h.primeBarrier();

        DaemonAudioSender sender(h.engine);
        QSignalSpy packets(&sender, &DaemonAudioSender::packetReady);
        QVERIFY(sender.start(kSsrc, 9, 10'000));
        h.feedMixed(DaemonAudioSource::kBlockFrames, 0.40f, 0.40f, 0.40f, 0.40f);
        sender.drain();
        QCOMPARE(packets.count(), 1);

        OpusAudioDecoder decoder;
        const auto mixed = decoder.decodeRtp(packetAt(packets, 0), kSsrc);
        QCOMPARE(mixed.status, OpusAudioCodecStatus::Accepted);
        QVERIFY(energy(mixed.pcmInterleaved, 0) > 1.0);
        QVERIFY(energy(mixed.pcmInterleaved, 1) > 1.0);

        // Keep B in the barrier while muted: mute belongs in the mixer and
        // must not cause the station sender to fall back to B's old program.
        sender.stop();
        h.radio.sliceById(h.sliceB)->setMuted(true);
        h.feedMixed(kDspFrames, 0.40f, 0.40f, 0.40f, 0.40f);
        QVERIFY(sender.start(kSsrc, 10, 11'920));
        h.feedMixed(DaemonAudioSource::kBlockFrames, 0.40f, 0.40f, 0.40f, 0.40f);
        sender.drain();
        QCOMPARE(packets.count(), 2);
        const auto muted = decoder.decodeRtp(packetAt(packets, 1), kSsrc);
        QCOMPARE(muted.status, OpusAudioCodecStatus::Accepted);
        QVERIFY(energy(muted.pcmInterleaved, 0) > energy(muted.pcmInterleaved, 1) * 8.0);
    }

    void tracksSequenceTimestampAndWrapFromCapturePositions()
    {
        Harness h;
        h.engine->setSliceStreaming(h.sliceB, false);
        DaemonAudioSender sender(h.engine);
        QSignalSpy packets(&sender, &DaemonAudioSender::packetReady);
        constexpr quint32 firstTimestamp = 0xfffffff0U;
        QVERIFY(sender.start(kSsrc, 65535, firstTimestamp));
        h.feedMixed(DaemonAudioSource::kBlockFrames * 2, 0.25f, -0.25f, 0.0f, 0.0f);
        sender.drain();
        QCOMPARE(packets.count(), 2);

        OpusAudioDecoder decoder;
        const auto first = decoder.decodeRtp(packetAt(packets, 0), kSsrc);
        const auto second = decoder.decodeRtp(packetAt(packets, 1), kSsrc);
        QCOMPARE(first.status, OpusAudioCodecStatus::Accepted);
        QCOMPARE(second.status, OpusAudioCodecStatus::Accepted);
        QCOMPARE(first.sequence, quint16(65535));
        QCOMPARE(second.sequence, quint16(0));
        QCOMPARE(first.timestamp, firstTimestamp);
        QCOMPARE(second.timestamp,
                 static_cast<quint32>(firstTimestamp + DaemonAudioSource::kBlockFrames));
        QCOMPARE(sender.nextSequence(), quint16(1));
        QCOMPARE(sender.nextTimestamp(), static_cast<quint32>(
            firstTimestamp + 2 * DaemonAudioSource::kBlockFrames));
    }

    void telemetryTracksSourceEncodingAndRestart()
    {
        Harness h;
        h.engine->setSliceStreaming(h.sliceB, false);
        DaemonAudioSender sender(h.engine);
        QSignalSpy packets(&sender, &DaemonAudioSender::packetReady);
        QVERIFY(sender.start(kSsrc, 11, 2'000));
        h.feedMixed(DaemonAudioSource::kBlockFrames * 2,
                    0.25f, -0.25f, 0.0f, 0.0f);
        sender.drain();
        QCOMPARE(packets.count(), 2);

        const auto active = sender.telemetry();
        QCOMPARE(active.source.capturedValidRateFrames,
                 std::uint64_t{2 * DaemonAudioSource::kBlockFrames});
        QCOMPARE(active.source.sourceDropEvents, std::uint64_t{0});
        QCOMPARE(active.consumedBlocks, std::uint64_t{2});
        QCOMPARE(active.encodedPackets, std::uint64_t{2});
        QCOMPARE(active.encodeFailures, std::uint64_t{0});
        QVERIFY(active.hasLastEmittedPacket);
        QCOMPARE(active.lastEmittedSequence, quint16{12});
        QCOMPARE(active.lastEmittedTimestamp,
                 quint32{2'000 + DaemonAudioSource::kBlockFrames});

        sender.stop();
        const auto stopped = sender.telemetry();
        QCOMPARE(stopped.consumedBlocks, active.consumedBlocks);
        QCOMPARE(stopped.encodedPackets, active.encodedPackets);
        QCOMPARE(stopped.lastEmittedSequence, active.lastEmittedSequence);
        QCOMPARE(stopped.lastEmittedTimestamp, active.lastEmittedTimestamp);

        QVERIFY(sender.start(kSsrc, 99, 99'000));
        const auto restarted = sender.telemetry();
        QCOMPARE(restarted.source.capturedValidRateFrames, std::uint64_t{0});
        QCOMPARE(restarted.source.sourceDropEvents, std::uint64_t{0});
        QCOMPARE(restarted.consumedBlocks, std::uint64_t{0});
        QCOMPARE(restarted.encodedPackets, std::uint64_t{0});
        QCOMPARE(restarted.encodeFailures, std::uint64_t{0});
        QVERIFY(!restarted.hasLastEmittedPacket);
    }

    void stopRestartFlushesCaptureAndUsesTheNewCallerBases()
    {
        Harness h;
        h.engine->setSliceStreaming(h.sliceB, false);
        DaemonAudioSender sender(h.engine);
        QSignalSpy packets(&sender, &DaemonAudioSender::packetReady);
        QVERIFY(sender.start(kSsrc, 1, 100));
        h.feedMixed(DaemonAudioSource::kBlockFrames, 0.10f, 0.10f, 0.0f, 0.0f);
        sender.stop();
        QVERIFY(!sender.isRunning());

        // This post-stop PCM cannot survive the new source epoch.
        h.feedMixed(DaemonAudioSource::kBlockFrames, 0.20f, 0.20f, 0.0f, 0.0f);
        QVERIFY(sender.start(kSsrc, 77, 50'000));
        h.feedMixed(DaemonAudioSource::kBlockFrames, 0.30f, 0.30f, 0.0f, 0.0f);
        sender.drain();
        QCOMPARE(packets.count(), 1);

        OpusAudioDecoder decoder;
        const auto packet = decoder.decodeRtp(packetAt(packets, 0), kSsrc);
        QCOMPARE(packet.status, OpusAudioCodecStatus::Accepted);
        QCOMPARE(packet.sequence, quint16(77));
        QCOMPARE(packet.timestamp, quint32(50'000));
    }

    void packetReadyLifecycleChangeCannotContinueTheOldDrain()
    {
        Harness h;
        h.engine->setSliceStreaming(h.sliceB, false);
        DaemonAudioSender sender(h.engine);
        QSignalSpy packets(&sender, &DaemonAudioSender::packetReady);
        QVERIFY(sender.start(kSsrc, 4, 400));
        h.feedMixed(DaemonAudioSource::kBlockFrames * 2, 0.20f, 0.20f, 0.0f, 0.0f);

        bool restarted = false;
        bool restartAccepted = false;
        connect(&sender, &DaemonAudioSender::packetReady, &sender,
                [&sender, &restarted, &restartAccepted](const QByteArray&) {
            if (!restarted) {
                restarted = true;
                restartAccepted = sender.start(kSsrc + 1, 900, 90'000);
            }
        });
        sender.drain();
        QCOMPARE(packets.count(), 1);
        QVERIFY(restarted);
        QVERIFY(restartAccepted);

        // The second old block was flushed by start() in the recipient. A
        // newly captured block must use only the recipient's supplied bases.
        h.feedMixed(DaemonAudioSource::kBlockFrames, 0.30f, 0.30f, 0.0f, 0.0f);
        sender.drain();
        QCOMPARE(packets.count(), 2);
        OpusAudioDecoder decoder;
        const auto first = decoder.decodeRtp(packetAt(packets, 0), kSsrc);
        const auto restartedPacket = decoder.decodeRtp(packetAt(packets, 1), kSsrc + 1);
        QCOMPARE(first.status, OpusAudioCodecStatus::Accepted);
        QCOMPARE(restartedPacket.status, OpusAudioCodecStatus::Accepted);
        QCOMPARE(restartedPacket.sequence, quint16(900));
        QCOMPARE(restartedPacket.timestamp, quint32(90'000));
    }

    void packetReadyStopCannotEmitAnotherQueuedOldPacket()
    {
        Harness h;
        h.engine->setSliceStreaming(h.sliceB, false);
        DaemonAudioSender sender(h.engine);
        QSignalSpy packets(&sender, &DaemonAudioSender::packetReady);
        QVERIFY(sender.start(kSsrc, 4, 400));
        h.feedMixed(DaemonAudioSource::kBlockFrames * 2, 0.20f, 0.20f, 0.0f, 0.0f);

        connect(&sender, &DaemonAudioSender::packetReady, &sender,
                [&sender](const QByteArray&) { sender.stop(); });
        sender.drain();
        QCOMPARE(packets.count(), 1);
        QVERIFY(!sender.isRunning());
    }
};

QTEST_MAIN(TstDaemonAudioSender)
#include "tst_daemon_audio_sender.moc"
