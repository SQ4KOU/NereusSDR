// no-port-check: NereusSDR-original remote audio receiver integration tests.
#include <QtTest>
#include "RealtimeTestLoad.h"
#include <QTimer>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <cmath>
#include <chrono>
#include <mutex>
#include <thread>
#include "core/AudioEngine.h"
#include "core/audio/PortAudioBus.h"
#include "core/session/media/AudioJitterBuffer.h"
#include "core/session/media/OpusAudioCodec.h"
#include "core/session/media/PcmAudioCodec.h"
#include "core/session/media/RemoteAudioRateMatcher.h"
#include "core/session/media/RemoteAudioReceiver.h"
#include "fakes/PacedAudioBus.h"
#include "OperatorWording.h"
#include <functional>
using namespace NereusSDR;
namespace {
// One lossless packet of the two-tone test signal: 997 Hz left, 1703 Hz
// right, 192 frames starting at packet * 192.
QByteArray losslessTonePacket(int packet, quint32 ssrc)
{
    QVector<float> pcm(PcmAudioCodecConfig::kPacketFrames * 2);
    for (int i = 0; i < PcmAudioCodecConfig::kPacketFrames; ++i) {
        const double t = double(packet * PcmAudioCodecConfig::kPacketFrames + i) / 48000;
        pcm[2 * i] = float(0.2 * std::sin(t * 2 * 3.141592653589793 * 997));
        pcm[2 * i + 1] = float(0.2 * std::sin(t * 2 * 3.141592653589793 * 1703));
    }
    const PcmRtpEncodeResult encoded = PcmAudioPacketiser{}.encode(
        pcm, quint16(packet), quint32(packet) * quint32(PcmAudioCodecConfig::kPacketFrames), ssrc);
    return encoded.status == OpusAudioCodecStatus::Accepted ? encoded.packet : QByteArray{};
}

// R-R3-43: one lossless packet whose every sample is `value`, so the order a
// sink receives is readable from the samples.
QByteArray losslessLevelPacket(int packet, quint32 ssrc, float value)
{
    const QVector<float> pcm(PcmAudioCodecConfig::kPacketFrames * 2, value);
    const PcmRtpEncodeResult encoded = PcmAudioPacketiser{}.encode(
        pcm, quint16(packet), quint32(packet) * quint32(PcmAudioCodecConfig::kPacketFrames), ssrc);
    return encoded.status == OpusAudioCodecStatus::Accepted ? encoded.packet : QByteArray{};
}

// R-R3-43: what a PCM sink was handed, block by block, from the worker.
struct CollectedPcm {
    mutable std::mutex mutex;
    QList<QVector<float>> blocks;
    RemoteAudioReceiver::PcmSink sink()
    {
        return [this](const float* pcm, int frames) {
            std::lock_guard<std::mutex> lock(mutex);
            blocks.append(QVector<float>(pcm, pcm + qsizetype(frames) * 2));
        };
    }
    QList<QVector<float>> snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return blocks;
    }
    qsizetype count() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return blocks.size();
    }
};

// R-R3-23: one lossless packet whose stereo frame f (counted from the
// stream's start) is `frameAt(f)`.
QByteArray losslessPacketOf(int packet, quint32 ssrc,
                            const std::function<std::pair<float, float>(qint64)>& frameAt)
{
    QVector<float> pcm(PcmAudioCodecConfig::kPacketFrames * 2);
    for (int i = 0; i < PcmAudioCodecConfig::kPacketFrames; ++i) {
        const auto [left, right] =
            frameAt(qint64(packet) * PcmAudioCodecConfig::kPacketFrames + i);
        pcm[2 * i] = left;
        pcm[2 * i + 1] = right;
    }
    const PcmRtpEncodeResult encoded = PcmAudioPacketiser{}.encode(
        pcm, quint16(packet), quint32(packet) * quint32(PcmAudioCodecConfig::kPacketFrames), ssrc);
    return encoded.status == OpusAudioCodecStatus::Accepted ? encoded.packet : QByteArray{};
}

// R-R3-23: the amplitude of a `hz` tone in one channel of audio heard at
// `rateHz` with `channels` interleaved, over `frames` frames from
// `firstFrame`.
double heardToneAmplitude(const QVector<float>& heard, int channels, int channel, double hz,
                          int rateHz, qint64 firstFrame, qint64 frames)
{
    double cosine = 0.0;
    double sine = 0.0;
    qint64 counted = 0;
    for (qint64 frame = firstFrame; frame < firstFrame + frames
         && frame * channels + channel < heard.size(); ++frame) {
        const double phase = 2.0 * 3.141592653589793 * hz * double(frame) / double(rateHz);
        const double sample = heard.at(frame * channels + channel);
        cosine += sample * std::cos(phase);
        sine += sample * std::sin(phase);
        ++counted;
    }
    return counted > 0 ? 2.0 * std::hypot(cosine, sine) / double(counted) : 0.0;
}

double blockRms(const QVector<float>& block)
{
    double sum = 0.0;
    for (float sample : block) { sum += double(sample) * sample; }
    return block.isEmpty() ? 0.0 : std::sqrt(sum / block.size());
}
} // namespace
class TstRemoteAudioReceiver : public QObject {
    Q_OBJECT
private slots:
    // The load when a real-time case failed (R-R3-21, R-R3-40).
    void cleanup() { NereusSDR::RealtimeTestLoad::printLoadAverageIfFailed(); }

    void speakerTrimMuteAndLifecycle()
    {
        AudioEngine engine;
        auto sink = std::make_unique<PacedAudioBus>();
        auto* bus = sink.get();
        engine.setSpeakersBusForTest(std::move(sink));
        QVERIFY(engine.beginRemotePlayback());
        QVERIFY(!engine.isRunning()); // No microphone, VAX, or local DSP startup.
        engine.setVolume(0.25f);
        QVector<float> pcm(960, 0.8f);
        QVERIFY(engine.writeRemotePlayback(pcm));
        QCOMPARE(engine.remotePlaybackPacing()->queuedFrames, 480);
        bus->render(480);
        for (float value : bus->heard) { QVERIFY(std::abs(value - 0.2f) < 0.00001f); }
        QVERIFY(engine.writeRemotePlayback(pcm));
        engine.setMasterMuted(true);
        QCOMPARE(engine.remotePlaybackPacing()->queuedFrames, 0);
        QVERIFY(engine.writeRemotePlayback(pcm));
        QCOMPARE(engine.remotePlaybackPacing()->queuedFrames, 0);
        engine.endRemotePlayback();
        QVERIFY(!engine.remotePlaybackPacing());
        QVERIFY(!engine.writeRemotePlayback(pcm));
    }
    void telemetryTracksActualReceiverActivityAndContext()
    {
        AudioEngine engine;
        engine.setVolume(1.0f);
        auto sink = std::make_unique<PacedAudioBus>();
        auto* bus = sink.get();
        engine.setSpeakersBusForTest(std::move(sink));
        RemoteAudioReceiver receiver(&engine);
        QVERIFY(receiver.start(321, 0));

        const auto started = receiver.telemetry();
        QVERIFY(started.running);
        QCOMPARE(started.acceptedPackets, quint64(0));
        QCOMPARE(started.decodedPackets, quint64(0));
        QCOMPARE(started.rejectedHeaders, quint64(0));
        QCOMPARE(started.deviceConsumedFrames, quint64(0));
        QVERIFY(!started.lastAdmittedPacketAgeMs);
        QVERIFY(!started.lastDeviceProgressAgeMs);

        receiver.submit(QByteArrayLiteral("not an RTP packet"));
        QCOMPARE(receiver.telemetry().rejectedHeaders, quint64(1));

        OpusAudioEncoder encoder;
        const QVector<float> pcm(3840, 0.1f);
        const auto encoded = encoder.encode(pcm, 7, 0, 321);
        QCOMPARE(encoded.status, OpusAudioCodecStatus::Accepted);
        receiver.submit(encoded.packet);
        receiver.submit(encoded.packet); // Actual jitter-buffer duplicate decision.

        QTimer device;
        device.setTimerType(Qt::PreciseTimer);
        device.setInterval(1);
        QElapsedTimer deviceClock;
        deviceClock.start();
        quint64 renderedFrames = 0;
        connect(&device, &QTimer::timeout, this, [&] {
            const quint64 due = quint64(deviceClock.nsecsElapsed()) * 48000 / 1'000'000'000;
            while (due >= renderedFrames + 480) {
                bus->render(480);
                renderedFrames += 480;
            }
        });
        device.start();

        int packet = 1;
        QTimer source;
        source.setTimerType(Qt::PreciseTimer);
        source.setInterval(1);
        QElapsedTimer sourceClock;
        sourceClock.start();
        connect(&source, &QTimer::timeout, this, [&] {
            const int duePackets = int(sourceClock.elapsed() / 40) + 1;
            while (packet < duePackets) {
                const auto next = encoder.encode(pcm, quint16(packet), quint32(packet) * 1920, 321);
                if (next.status == OpusAudioCodecStatus::Accepted) {
                    receiver.submit(next.packet);
                }
                ++packet;
            }
        });
        source.start();
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            const auto snapshot = receiver.telemetry();
            return snapshot.acceptedPackets > 0 && snapshot.duplicatePackets == 1
                && snapshot.decodedPackets > 0 && snapshot.deviceConsumedFrames > 0
                && snapshot.lastAdmittedPacketAgeMs.has_value()
                && snapshot.lastDeviceProgressAgeMs.has_value();
        }(), 1000);

        const auto active = receiver.telemetry();
        QVERIFY(active.running);
        QCOMPARE(active.rejectedHeaders, quint64(1));
        QCOMPARE(active.duplicatePackets, quint64(1));
        source.stop();
        device.stop();
        receiver.stop();

        const auto stopped = receiver.telemetry();
        QVERIFY(!stopped.running);
        QVERIFY(stopped.acceptedPackets > 0);
        QVERIFY(stopped.decodedPackets > 0);
        QCOMPARE(stopped.rejectedHeaders, quint64(1));
        QCOMPARE(stopped.duplicatePackets, quint64(1));
        QVERIFY(stopped.lastAdmittedPacketAgeMs);
        QVERIFY(stopped.lastDeviceProgressAgeMs);

        QVERIFY(receiver.start(321, 1920));
        const auto restarted = receiver.telemetry();
        QVERIFY(restarted.running);
        QVERIFY(restarted.generation > stopped.generation);
        QCOMPARE(restarted.acceptedPackets, quint64(0));
        QCOMPARE(restarted.decodedPackets, quint64(0));
        QCOMPARE(restarted.rejectedHeaders, quint64(0));
        QCOMPARE(restarted.deviceConsumedFrames, quint64(0));
        QVERIFY(!restarted.lastAdmittedPacketAgeMs);
        QVERIFY(!restarted.lastDeviceProgressAgeMs);
        receiver.stop();
    }
    // R-R3-07: the rate matcher's ratio, reported only in fault text before,
    // is a live telemetry gauge: absent before playback, present and moving
    // while audio plays, absent after stop and in a fresh context.
    void telemetryPublishesDriftRatioWhilePlaying()
    {
        AudioEngine engine;
        engine.setVolume(1.0f);
        auto sink = std::make_unique<PacedAudioBus>();
        auto* bus = sink.get();
        engine.setSpeakersBusForTest(std::move(sink));
        RemoteAudioReceiver receiver(&engine);
        QVERIFY(receiver.start(654, 0));
        QVERIFY(!receiver.telemetry().driftRatio);

        OpusAudioEncoder encoder;
        const QVector<float> pcm(3840, 0.1f);
        QTimer device;
        device.setTimerType(Qt::PreciseTimer);
        device.setInterval(1);
        QElapsedTimer deviceClock;
        deviceClock.start();
        quint64 renderedFrames = 0;
        connect(&device, &QTimer::timeout, this, [&] {
            const quint64 due = quint64(deviceClock.nsecsElapsed()) * 48000 / 1'000'000'000;
            while (due >= renderedFrames + 480) {
                bus->render(480);
                renderedFrames += 480;
            }
        });
        int packet = 0;
        QTimer source;
        source.setTimerType(Qt::PreciseTimer);
        source.setInterval(1);
        QElapsedTimer sourceClock;
        sourceClock.start();
        connect(&source, &QTimer::timeout, this, [&] {
            const int duePackets = int(sourceClock.elapsed() / 40) + 1;
            while (packet < duePackets) {
                const auto next = encoder.encode(pcm, quint16(packet), quint32(packet) * 1920, 654);
                if (next.status == OpusAudioCodecStatus::Accepted) {
                    receiver.submit(next.packet);
                }
                ++packet;
            }
        });
        device.start();
        source.start();

        // Review minor 3: the controller holds its initial ratio until
        // create_rmatchV's 3.0 s startup delay of audio has passed, both
        // written and read (rmatch.c:514, 356, 461). Before that there is
        // no measurement, so none is reported, although audio plays.
        constexpr quint64 kStartupFrames = 3 * 48'000;
        bool earlyRatio = false;
        QElapsedTimer early;
        early.start();
        while (receiver.telemetry().deviceConsumedFrames < 2 * 48'000 && early.elapsed() < 10'000) {
            const auto snapshot = receiver.telemetry();
            earlyRatio = earlyRatio || (snapshot.running && snapshot.driftRatio.has_value());
            QTest::qWait(20);
        }
        QVERIFY(receiver.telemetry().deviceConsumedFrames >= 2 * 48'000);
        QVERIFY2(!earlyRatio, "a drift ratio was reported before the matcher measured one");
        QTRY_VERIFY_WITH_TIMEOUT(receiver.telemetry().driftRatio.has_value(), 5000);
        // Device consumption trails matcher reads by at most its queue.
        QVERIFY(receiver.telemetry().deviceConsumedFrames >= kStartupFrames - 24'000);
        // The ratio stays inside WDSP rmatch's own clamp (rmatch.c control():
        // 0.96..1.04) and follows the controller as it adjusts.
        const double first = *receiver.telemetry().driftRatio;
        QVERIFY2(first >= 0.96 && first <= 1.04, qPrintable(QString::number(first, 'f', 7)));
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            const auto snapshot = receiver.telemetry();
            return snapshot.running && snapshot.driftRatio && *snapshot.driftRatio != first;
        }(), 6000);
        const auto playing = receiver.telemetry();
        QVERIFY(*playing.driftRatio >= 0.96 && *playing.driftRatio <= 1.04);
        QCOMPARE(playing.underflows, 0);
        QCOMPARE(playing.overflows, 0);

        source.stop();
        device.stop();
        receiver.stop();
        QVERIFY(!receiver.telemetry().driftRatio);
        QVERIFY(receiver.start(654, 0));
        QVERIFY(!receiver.telemetry().driftRatio);
        receiver.stop();
    }
    void telemetryCountsValidOpusPayloadAndSamplesSpeakerQueue()
    {
        AudioEngine engine;
        engine.setVolume(1.0f);
        auto sink = std::make_unique<PacedAudioBus>();
        auto* bus = sink.get();
        engine.setSpeakersBusForTest(std::move(sink));
        RemoteAudioReceiver receiver(&engine);
        QVERIFY(receiver.start(987, 0));

        OpusAudioEncoder encoder;
        const QVector<float> pcm(3840, 0.1f);
        const auto malformed = encoder.encode(pcm, 0, 0, 987);
        const auto wrongSsrc = encoder.encode(pcm, 1, 1920, 988);
        QCOMPARE(malformed.status, OpusAudioCodecStatus::Accepted);
        QCOMPARE(wrongSsrc.status, OpusAudioCodecStatus::Accepted);
        QByteArray invalid = malformed.packet;
        invalid[0] = static_cast<char>(0x40);
        receiver.submit(invalid);
        receiver.submit(wrongSsrc.packet);
        QCOMPARE(receiver.telemetry().receivedAudioPayloadBytes, quint64(0));
        QCOMPARE(receiver.telemetry().rejectedHeaders, quint64(2));

        QVector<QByteArray> packets;
        quint64 expectedBytes = 0;
        for (int index = 0; index < 3; ++index) {
            const auto encoded = encoder.encode(pcm, quint16(index + 2),
                                                quint32(index) * 1920u, 987);
            QCOMPARE(encoded.status, OpusAudioCodecStatus::Accepted);
            const auto inspected = inspectOpusRtp(encoded.packet, 987);
            QCOMPARE(inspected.status, OpusAudioCodecStatus::Accepted);
            expectedBytes += quint64(inspected.payloadBytes);
            packets.append(encoded.packet);
            receiver.submit(encoded.packet);
        }
        // A duplicate remains received traffic even when jitter ordering later
        // rejects it as non-unique media.
        const auto duplicate = inspectOpusRtp(packets.first(), 987);
        expectedBytes += quint64(duplicate.payloadBytes);
        receiver.submit(packets.first());
        QCOMPARE(receiver.telemetry().receivedAudioPayloadBytes, expectedBytes);

        QTRY_VERIFY_WITH_TIMEOUT([&] {
            const auto snapshot = receiver.telemetry();
            return snapshot.decodedPackets > 0 && snapshot.speakerQueuedMs.has_value()
                && *snapshot.speakerQueuedMs == 20.0;
        }(), 500);
        const auto active = receiver.telemetry();
        QVERIFY(active.speakerQueuedMs);
        const auto pacing = bus->outputPacing();
        QVERIFY(pacing);
        QCOMPARE(*active.speakerQueuedMs, double(pacing->queuedFrames) / 48.0);
        QCOMPARE(*active.speakerQueuedMs, 20.0);

        receiver.stop();
        const auto stopped = receiver.telemetry();
        QCOMPARE(stopped.receivedAudioPayloadBytes, expectedBytes);
        QVERIFY(!stopped.speakerQueuedMs);
        QVERIFY(receiver.start(987, 5760));
        QCOMPARE(receiver.telemetry().receivedAudioPayloadBytes, quint64(0));
        receiver.stop();
    }
    void telemetryCountsValidPayloadBeforeIncomingQueueDrops()
    {
        AudioEngine engine;
        auto sink = std::make_unique<PacedAudioBus>();
        auto* bus = sink.get();
        // beginRemotePlayback() makes one synchronous pacing read. Block the
        // worker's following initial read so every packet in this burst has
        // crossed submit() before the bounded incoming queue is examined.
        bus->blockOutputPacingAfterCallsForTesting(1);
        engine.setSpeakersBusForTest(std::move(sink));
        RemoteAudioReceiver receiver(&engine);
        struct ReleasePacingGate {
            PacedAudioBus* bus;
            ~ReleasePacingGate() { bus->releaseOutputPacingGateForTesting(); }
        } releaseGate{bus};
        QVERIFY(receiver.start(654, 0));
        QVERIFY(bus->waitForOutputPacingGateForTesting(std::chrono::milliseconds(250)));
        OpusAudioEncoder encoder;
        const auto encoded = encoder.encode(QVector<float>(3840, 0.1f), 0, 0, 654);
        QCOMPARE(encoded.status, OpusAudioCodecStatus::Accepted);
        const auto inspected = inspectOpusRtp(encoded.packet, 654);
        QCOMPARE(inspected.status, OpusAudioCodecStatus::Accepted);

        // submit() accounts at the validated RTP boundary. This burst exceeds
        // the local eight-packet queue, so later queue admission cannot turn
        // valid inbound traffic into an undercount.
        constexpr int kBurstPackets = 32;
        for (int index = 0; index < kBurstPackets; ++index) {
            receiver.submit(encoded.packet);
        }
        QCOMPARE(receiver.telemetry().receivedAudioPayloadBytes,
                 quint64(kBurstPackets) * quint64(inspected.payloadBytes));
        releaseGate.bus->releaseOutputPacingGateForTesting();
        receiver.stop();
    }
    void speakerTimingUnavailableFiresThroughTheRealWorkerPath()
    {
        AudioEngine engine;
        engine.setVolume(1.0f);
        auto sink = std::make_unique<PacedAudioBus>();
        auto* bus = sink.get();
        // beginRemotePlayback() makes one synchronous pacing read (call 1).
        // Block the worker's own first pacing read (call 2), so the test
        // knows -- from the real worker thread, not a wall-clock guess --
        // exactly when that read has happened and it is safe to end remote
        // playback without racing the worker's own startup checks.
        bus->blockOutputPacingAfterCallsForTesting(1);
        engine.setSpeakersBusForTest(std::move(sink));
        RemoteAudioReceiver receiver(&engine);
        QSignalSpy errors(&receiver, &RemoteAudioReceiver::errorOccurred);
        QVERIFY(receiver.start(741, 0));
        QVERIFY(bus->waitForOutputPacingGateForTesting(std::chrono::milliseconds(250)));
        bus->releaseOutputPacingGateForTesting();
        // The worker's own initial pacing read (just unblocked above) still
        // observes remote playback as active, so decoder/rate-matcher setup
        // succeeds normally. Ending remote playback now takes effect on the
        // worker's next pacing read -- the "during play" one it reaches once
        // the packet below is decoded -- never by emitting the signal here.
        engine.endRemotePlayback();

        OpusAudioEncoder encoder;
        const auto encoded = encoder.encode(QVector<float>(3840, 0.1f), 0, 0, 741);
        QCOMPARE(encoded.status, OpusAudioCodecStatus::Accepted);
        receiver.submit(encoded.packet);

        QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(), 2000);
        QCOMPARE(errors.count(), 1);
        QCOMPARE(errors.first().at(1).value<RemoteAudioReceiver::Fault>(),
                 RemoteAudioReceiver::Fault::SpeakerTimingUnavailable);
        receiver.stop();
    }
    void telemetryLifetimeInterruptionsSurviveContextRestart()
    {
        AudioEngine engine;
        engine.setVolume(1.0f);
        auto sink = std::make_unique<PacedAudioBus>();
        auto* bus = sink.get();
        engine.setSpeakersBusForTest(std::move(sink));
        RemoteAudioReceiver receiver(&engine);
        QVERIFY(receiver.start(654, 0));

        const auto baseline = receiver.telemetry();
        QVERIFY(baseline.lifetimeUnderflows);
        QVERIFY(baseline.lifetimeOverflows);
        QCOMPARE(*baseline.lifetimeUnderflows, quint64(0));
        QCOMPARE(*baseline.lifetimeOverflows, quint64(0));

        OpusAudioEncoder encoder;
        const auto encoded = encoder.encode(QVector<float>(3840, 0.1f), 0, 0, 654);
        QCOMPARE(encoded.status, OpusAudioCodecStatus::Accepted);
        receiver.submit(encoded.packet);

        // Deliberately outpace 48 kHz while the real worker drains its WDSP
        // matcher. This exercises the same interruption/restart path that a
        // stalled producer would use, rather than changing matcher counters.
        QSignalSpy restarts(&receiver, &RemoteAudioReceiver::restartRequested);
        // QtTest coalesces GUI timers, which can turn a nominal 1 ms timer
        // into normal-rate consumption. Drive the fake device independently,
        // just as the actual device callback is independent of the GUI loop.
        std::jthread device([bus](std::stop_token stop) {
            while (!stop.stop_requested()) {
                bus->render(480);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        QTRY_VERIFY_WITH_TIMEOUT(receiver.rateMatcherUnderflows() > 0 && !restarts.isEmpty(), 1500);
        device.request_stop();
        device.join();

        // Do not sample the retired context. A restart entirely between
        // telemetry polls must still retain the interruption in the lifetime
        // total while the new context's counters start from zero.
        receiver.stop();
        QVERIFY(receiver.start(654, 1920));
        const auto restarted = receiver.telemetry();
        QVERIFY(restarted.running);
        QCOMPARE(restarted.underflows, 0);
        QCOMPARE(restarted.overflows, 0);
        QVERIFY(restarted.lifetimeUnderflows);
        QVERIFY(*restarted.lifetimeUnderflows > 0);
        QVERIFY(restarted.lifetimeOverflows);
        QCOMPARE(*restarted.lifetimeOverflows, quint64(0));
        receiver.stop();
    }
    void rtpPlaybackLossAndFreshGeneration_data()
    {
        QTest::addColumn<int>("quantum");
        QTest::newRow("480 frames") << 480;
        QTest::newRow("1024 frames") << 1024;
        QTest::newRow("2048 frames") << 2048;
    }
    void rtpPlaybackLossAndFreshGeneration()
    {
        QFETCH(int, quantum);
        AudioEngine engine;
        engine.setVolume(1.0f);
        auto sink = std::make_unique<PacedAudioBus>();
        auto* bus = sink.get();
        bus->callbackFrames = quantum;
        engine.setSpeakersBusForTest(std::move(sink));
        RemoteAudioReceiver receiver(&engine);
        QSignalSpy errors(&receiver, &RemoteAudioReceiver::errorOccurred);
        QSignalSpy restarts(&receiver, &RemoteAudioReceiver::restartRequested);
        QVERIFY(receiver.start(123, 0));
        OpusAudioEncoder encoder;
        QTimer device;
        device.setTimerType(Qt::PreciseTimer);
        device.setInterval(1);
        QElapsedTimer deviceClock;
        deviceClock.start();
        quint64 renderedFrames = 0;
        connect(&device, &QTimer::timeout, this, [&] {
            const quint64 due = quint64(deviceClock.nsecsElapsed()) * 48000 / 1'000'000'000;
            while (due >= renderedFrames + quint64(quantum)) {
                bus->render(quantum);
                renderedFrames += quint64(quantum);
            }
        });
        device.start();
        int packet = 0;
        QByteArray retired;
        QTimer source;
        source.setTimerType(Qt::PreciseTimer);
        source.setInterval(1);
        QElapsedTimer sourceClock;
        sourceClock.start();
        connect(&source, &QTimer::timeout, this, [&] {
            // QTest event-loop wakeups can coalesce timer expirations. Keep
            // the producer at 48 kHz instead of slowing it to callback count.
            const int duePackets = int(sourceClock.elapsed() / 40);
            while (packet < duePackets) {
            QVector<float> pcm(3840);
            for (int i = 0; i < 1920; ++i) {
                const double t = double(packet * 1920 + i) / 48000;
                pcm[2*i] = float(0.2 * std::sin(t * 2 * 3.141592653589793 * 997));
                pcm[2*i+1] = float(0.2 * std::sin(t * 2 * 3.141592653589793 * 1703));
            }
            const auto encoded = encoder.encode(pcm, quint16(packet), quint32(packet) * 1920u, 123);
            if (packet != 8) { receiver.submit(encoded.packet); }
            if (packet == 5) { retired = encoded.packet; receiver.submit(encoded.packet); }
            ++packet;
            }
        });
        source.start();
        QTRY_VERIFY_WITH_TIMEOUT(receiver.decodedPackets() >= 24 || !errors.isEmpty()
                                 || !restarts.isEmpty(), 3000);
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.first().first().toString()));
        QVERIFY2(restarts.isEmpty(), restarts.isEmpty() ? "" : qPrintable(restarts.first().first().toString()));
        QVERIFY(receiver.concealedPackets() >= 1);
        // Packet 8 was never submitted: exactly one real gap. Packet 5's
        // duplicate is not a gap, so it must not inflate missingPackets.
        const auto lossTelemetry = receiver.telemetry();
        QCOMPARE(lossTelemetry.missingPackets, quint64(1));
        QVERIFY(lossTelemetry.expectedPackets >= quint64(24));
        QVERIFY(lossTelemetry.arrivalJitterMs.has_value());
        QCOMPARE(errors.count(), 0);
        QCOMPARE(restarts.count(), 0);
        source.stop();
        receiver.stop();
        QCOMPARE(bus->outputPacing()->queuedFrames, 0);
        const int peak = bus->peakQueued;
        const int target = qMax(960, quantum + 480);
        QVERIFY(peak >= target && peak < target + 480);
        QCOMPARE(receiver.rateMatcherUnderflows(), 0);
        QCOMPARE(receiver.rateMatcherOverflows(), 0);
        double l = 0, r = 0, lr = 0;
        for (int i = 0; i + 1 < bus->heard.size(); i += 2) {
            l += double(bus->heard[i]) * bus->heard[i];
            r += double(bus->heard[i+1]) * bus->heard[i+1];
            lr += double(bus->heard[i]) * bus->heard[i+1];
        }
        qInfo() << "Playback stereo energies/correlation" << l << r << lr / std::sqrt(l * r);
        QVERIFY(l > 100 && r > 100);
        // The pinned 24 kbit/s Opus profile couples these two tones at
        // correlation ~0.104 even in direct decode. Measure the contract
        // (left/right placement) instead of imposing lossless correlation.
        const auto toneAmplitude = [&](int channel, double hz) {
            double real = 0, imag = 0;
            for (int i = channel; i < bus->heard.size(); i += 2) {
                const double phase = 2 * 3.141592653589793 * hz * (i / 2) / 48000;
                real += bus->heard[i] * std::cos(phase);
                imag += bus->heard[i] * std::sin(phase);
            }
            return std::hypot(real, imag);
        };
        QVERIFY(toneAmplitude(0, 997) > 8 * toneAmplitude(1, 997));
        QVERIFY(toneAmplitude(1, 1703) > 8 * toneAmplitude(0, 1703));
        QVERIFY(receiver.start(123, quint32(packet) * 1920u));
        const auto restartedTelemetry = receiver.telemetry();
        QCOMPARE(restartedTelemetry.missingPackets, quint64(0));
        QCOMPARE(restartedTelemetry.expectedPackets, quint64(0));
        QVERIFY(!restartedTelemetry.arrivalJitterMs.has_value());
        receiver.submit(retired); // Previous generation cannot be decoded.
        QTest::qWait(100);
        QCOMPARE(receiver.decodedPackets(), quint64(0));
        QCOMPARE(bus->outputPacing()->queuedFrames, 0);
        receiver.stop();
        device.stop();
    }
    void arrivalBurstsRemainBounded_data()
    {
        QTest::addColumn<int>("packets");
        QTest::newRow("three packets") << 3;
        QTest::newRow("eight packets") << 8;
    }
    void arrivalBurstsRemainBounded()
    {
        QFETCH(int, packets);
        AudioEngine engine;
        engine.setVolume(1.0f);
        auto sink = std::make_unique<PacedAudioBus>();
        auto* bus = sink.get();
        engine.setSpeakersBusForTest(std::move(sink));
        RemoteAudioReceiver receiver(&engine);
        QSignalSpy errors(&receiver, &RemoteAudioReceiver::errorOccurred);
        QSignalSpy restarts(&receiver, &RemoteAudioReceiver::restartRequested);
        QVERIFY(receiver.start(12, 0));
        OpusAudioEncoder encoder;
        for (int block = 0; block < packets; ++block) {
            QVector<float> pcm(3840);
            for (int i = 0; i < 1920; ++i) {
                pcm[2*i] = pcm[2*i+1] = 0.2f * float(std::sin(
                    (block * 1920 + i) * 2 * 3.141592653589793 * 997 / 48000));
            }
            const auto encoded = encoder.encode(pcm, quint16(block), quint32(block * 1920), 12);
            QCOMPARE(encoded.status, OpusAudioCodecStatus::Accepted);
            receiver.submit(encoded.packet); // Same arrival burst, no sleeps.
        }
        QTimer device;
        device.setTimerType(Qt::PreciseTimer);
        device.setInterval(10);
        connect(&device, &QTimer::timeout, this, [bus] { bus->render(480); });
        device.start();
        QTRY_COMPARE_WITH_TIMEOUT(receiver.decodedPackets(), quint64(packets), 1000);
        receiver.stop();
        device.stop();
        QCOMPARE(errors.count(), 0);
        QCOMPARE(restarts.count(), 0);
        QCOMPARE(receiver.rateMatcherOverflows(), 0);
        QCOMPARE(receiver.rateMatcherUnderflows(), 0);
        QCOMPARE(bus->outputPacing()->queuedFrames, 0);
    }

    // Live R3 playback showed a production-rate Core (25 packets/s, no send
    // rejection) while the GUI restarted after admitted packets arrived with
    // 100-151 ms maximum gaps and occasional two-packet callback batches.
    // Exercise that reachable arrival shape through the actual Opus decoder,
    // jitter buffer, WDSP rate matcher, AudioEngine, and a 128-frame device
    // clock. The interval cycle still averages exactly 40 ms, so this does
    // not replace the producer-clock contract with a faster test source.
    void delayedCoalescedArrivalsKeepPlaybackContinuous()
    {
        constexpr quint32 kSsrc = 731;
        constexpr int kPackets = 100;
        constexpr int kDroppedPacket = 57;
        constexpr int kCallbackFrames = 128;

        OpusAudioEncoder encoder;
        QVector<QByteArray> packets;
        packets.reserve(kPackets);
        const QVector<float> pcm(3840, 0.1f);
        for (int packet = 0; packet < kPackets; ++packet) {
            const auto encoded = encoder.encode(
                pcm, quint16(packet), quint32(packet) * 1920u, kSsrc);
            QCOMPARE(encoded.status, OpusAudioCodecStatus::Accepted);
            packets.append(encoded.packet);
        }

        AudioEngine engine;
        engine.setVolume(1.0f);
        auto sink = std::make_unique<PacedAudioBus>();
        auto* bus = sink.get();
        bus->callbackFrames = kCallbackFrames;
        engine.setSpeakersBusForTest(std::move(sink));

        RemoteAudioReceiver receiver(&engine);
        QSignalSpy errors(&receiver, &RemoteAudioReceiver::errorOccurred);
        QSignalSpy restarts(&receiver, &RemoteAudioReceiver::restartRequested);
        QVERIFY(receiver.start(kSsrc, 0));

        // The callback is independent of the Qt event loop, as a real audio
        // device is. Integer nanosecond deadlines preserve 48 kHz over the
        // whole run instead of accumulating a rounded 2.667 ms sleep error.
        std::jthread device([bus, callbackFrames = kCallbackFrames](
                                std::stop_token stop) {
            using Clock = std::chrono::steady_clock;
            const auto started = Clock::now();
            quint64 callback = 1;
            while (!stop.stop_requested()) {
                const auto deadline = started + std::chrono::nanoseconds(
                    callback * quint64(callbackFrames) * 1'000'000'000ull
                    / 48'000ull);
                std::this_thread::sleep_until(deadline);
                if (stop.stop_requested()) {
                    break;
                }
                bus->render(callbackFrames);
                ++callback;
            }
        });

        // Every 20 inter-packet intervals are:
        //   17 * 40 ms, 100 ms, 0 ms, 20 ms = 800 ms.
        // This is still exactly 25 packets/s over each cycle, but it includes
        // the live 100 ms gap and coalesced pair. One omitted RTP timestamp
        // additionally proves the existing bounded PLC path remains viable.
        using Clock = std::chrono::steady_clock;
        const auto started = Clock::now();
        qint64 arrivalMs = 0;
        for (int packet = 0; packet < kPackets; ++packet) {
            if (packet > 0) {
                const int phase = packet % 20;
                arrivalMs += phase == 18 ? 100
                    : phase == 19 ? 0
                    : phase == 0 ? 20
                    : 40;
            }
            std::this_thread::sleep_until(
                started + std::chrono::milliseconds(arrivalMs));
            if (packet != kDroppedPacket) {
                receiver.submit(packets.at(packet));
            }
        }

        // Sample as soon as every valid packet and the one deliberate loss
        // have traversed the real decoder. This proves that demand release did
        // not skip a valid frame, without leaving an empty stream running long
        // enough to manufacture additional end-of-test PLC.
        RemoteAudioReceiverTelemetry telemetry;
        bool completed = false;
        const auto completionDeadline = Clock::now() + std::chrono::seconds(1);
        while (Clock::now() < completionDeadline) {
            telemetry = receiver.telemetry();
            if (!receiver.isRunning()) {
                break;
            }
            if (telemetry.acceptedPackets == quint64(kPackets - 1)
                && telemetry.decodedPackets >= quint64(kPackets - 1)
                && telemetry.concealedPackets >= 1) {
                completed = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        device.request_stop();
        device.join();
        QCoreApplication::processEvents();
        const bool runningAtSnapshot = receiver.isRunning();
        receiver.stop();

        const QString restartReason = restarts.isEmpty()
            ? QStringLiteral("none")
            : restarts.first().first().toString();
        const QString evidence = QStringLiteral(
            "running=%1 accepted=%2 decoded=%3 plc=%4 underflows=%5 "
            "overflows=%6 restarts=%7 errors=%8 queued=%9 reason=%10")
            .arg(runningAtSnapshot).arg(telemetry.acceptedPackets)
            .arg(telemetry.decodedPackets).arg(telemetry.concealedPackets)
            .arg(receiver.rateMatcherUnderflows())
            .arg(receiver.rateMatcherOverflows()).arg(restarts.count())
            .arg(errors.count())
            .arg(bus->outputPacing() ? bus->outputPacing()->queuedFrames : -1)
            .arg(restartReason);
        QVERIFY2(completed && runningAtSnapshot, qPrintable(evidence));
        QCOMPARE(telemetry.acceptedPackets, quint64(kPackets - 1));
        QCOMPARE(telemetry.decodedPackets, quint64(kPackets - 1));
        QCOMPARE(telemetry.concealedPackets, quint64(1));
        // The one deliberately withheld packet (index kDroppedPacket) is a
        // real gap; the live 100 ms/0 ms coalesced arrival pattern is
        // spacing, never counted as loss.
        QCOMPARE(telemetry.missingPackets, quint64(1));
        QVERIFY(telemetry.arrivalJitterMs.has_value());
        QVERIFY2(*telemetry.arrivalJitterMs > 5.0,
                 qPrintable(QStringLiteral("arrivalJitterMs=%1").arg(*telemetry.arrivalJitterMs)));
        QCOMPARE(receiver.rateMatcherUnderflows(), 0);
        QCOMPARE(receiver.rateMatcherOverflows(), 0);
        QCOMPARE(restarts.count(), 0);
        QCOMPARE(errors.count(), 0);
    }

    // Live connect evidence (2026-09-22): the GUI owner drained 63 RTP packets
    // in one batch about 2.5 s after the session started, the receiver raised
    // its arrival-queue overflow and audio restarted before it was ever heard.
    // A backlog that arrives before playback begins is trimmed to its newest
    // packet instead, and playback continues from there without a restart.
    void connectBacklogStartsPlaybackWithoutRestart()
    {
        constexpr quint32 kSsrc = 842;
        constexpr int kBacklog = 63;
        constexpr int kFollowing = 25;
        OpusAudioEncoder encoder;
        const QVector<float> pcm(3840, 0.1f);
        QVector<QByteArray> packets;
        for (int packet = 0; packet < kBacklog + kFollowing; ++packet) {
            const auto encoded = encoder.encode(
                pcm, quint16(packet), quint32(packet) * 1920u, kSsrc);
            QCOMPARE(encoded.status, OpusAudioCodecStatus::Accepted);
            packets.append(encoded.packet);
        }

        AudioEngine engine;
        engine.setVolume(1.0f);
        auto sink = std::make_unique<PacedAudioBus>();
        auto* bus = sink.get();
        // Hold the worker at its first pacing read so the whole backlog has
        // crossed submit() before the worker sees any of it, as when the GUI
        // owner drains a connect-time batch in one pass.
        bus->blockOutputPacingAfterCallsForTesting(1);
        engine.setSpeakersBusForTest(std::move(sink));
        RemoteAudioReceiver receiver(&engine);
        struct ReleasePacingGate {
            PacedAudioBus* bus;
            ~ReleasePacingGate() { bus->releaseOutputPacingGateForTesting(); }
        } releaseGate{bus};
        QSignalSpy errors(&receiver, &RemoteAudioReceiver::errorOccurred);
        QSignalSpy restarts(&receiver, &RemoteAudioReceiver::restartRequested);
        QVERIFY(receiver.start(kSsrc, 0));
        QVERIFY(bus->waitForOutputPacingGateForTesting(std::chrono::milliseconds(250)));
        for (int packet = 0; packet < kBacklog; ++packet) {
            receiver.submit(packets.at(packet));
        }

        std::jthread device([bus](std::stop_token stop) {
            using Clock = std::chrono::steady_clock;
            const auto started = Clock::now();
            quint64 callback = 1;
            while (!stop.stop_requested()) {
                std::this_thread::sleep_until(started + std::chrono::nanoseconds(
                    callback * 480ull * 1'000'000'000ull / 48'000ull));
                if (stop.stop_requested()) { break; }
                bus->render(480);
                ++callback;
            }
        });
        bus->releaseOutputPacingGateForTesting();

        // The producer continues at its normal 40 ms cadence after the batch.
        using Clock = std::chrono::steady_clock;
        const auto started = Clock::now();
        for (int packet = kBacklog; packet < kBacklog + kFollowing; ++packet) {
            std::this_thread::sleep_until(
                started + std::chrono::milliseconds(40 * (packet - kBacklog + 1)));
            receiver.submit(packets.at(packet));
        }
        RemoteAudioReceiverTelemetry telemetry;
        const auto deadline = Clock::now() + std::chrono::seconds(1);
        while (Clock::now() < deadline) {
            telemetry = receiver.telemetry();
            if (!receiver.isRunning()
                || telemetry.decodedPackets >= quint64(1 + kFollowing)) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        device.request_stop();
        device.join();
        QCoreApplication::processEvents();
        const bool running = receiver.isRunning();
        receiver.stop();

        const QString evidence = QStringLiteral("running=%1 restarts=%2 reason=%3")
            .arg(running).arg(restarts.count())
            .arg(restarts.isEmpty() ? QStringLiteral("none")
                                    : restarts.first().first().toString());
        QVERIFY2(running && restarts.isEmpty(), qPrintable(evidence));
        QCOMPARE(errors.count(), 0);
        // Only the newest backlog packet is kept; every older one is counted
        // as discarded at start, and the kept packet onward is continuous.
        QCOMPARE(telemetry.startDiscardedPackets, quint64(kBacklog - 1));
        QCOMPARE(telemetry.acceptedPackets, quint64(1 + kFollowing));
        QCOMPARE(telemetry.decodedPackets, quint64(1 + kFollowing));
        QCOMPARE(telemetry.concealedPackets, quint64(0));
        QCOMPARE(telemetry.latePackets, quint64(0));
        QCOMPARE(telemetry.missingPackets, quint64(0));
        QCOMPARE(telemetry.expectedPackets, quint64(1 + kFollowing));
        QCOMPARE(receiver.rateMatcherUnderflows(), 0);
        QCOMPARE(receiver.rateMatcherOverflows(), 0);
    }

    // The owner's connect-time batch can straddle the worker's first wake:
    // a few packets are admitted, then the rest of the backlog arrives before
    // anything has been released for playback. That remainder must neither
    // overflow the arrival queue nor fall outside the jitter window (the
    // "fresh context after a stream gap [jitterPackets=8]" restart).
    void backlogAfterFirstAdmissionStillStartsWithoutRestart()
    {
        constexpr quint32 kSsrc = 843;
        constexpr int kFirst = 3;
        constexpr int kBacklog = 63;
        constexpr int kFollowing = 25;
        OpusAudioEncoder encoder;
        const QVector<float> pcm(3840, 0.1f);
        QVector<QByteArray> packets;
        for (int packet = 0; packet < kBacklog + kFollowing; ++packet) {
            const auto encoded = encoder.encode(
                pcm, quint16(packet), quint32(packet) * 1920u, kSsrc);
            QCOMPARE(encoded.status, OpusAudioCodecStatus::Accepted);
            packets.append(encoded.packet);
        }

        AudioEngine engine;
        engine.setVolume(1.0f);
        auto sink = std::make_unique<PacedAudioBus>();
        auto* bus = sink.get();
        engine.setSpeakersBusForTest(std::move(sink));
        RemoteAudioReceiver receiver(&engine);
        struct ReleasePacingGate {
            PacedAudioBus* bus;
            ~ReleasePacingGate() { bus->releaseOutputPacingGateForTesting(); }
        } releaseGate{bus};
        QSignalSpy errors(&receiver, &RemoteAudioReceiver::errorOccurred);
        QSignalSpy restarts(&receiver, &RemoteAudioReceiver::restartRequested);
        QVERIFY(receiver.start(kSsrc, 0));
        for (int packet = 0; packet < kFirst; ++packet) {
            receiver.submit(packets.at(packet));
        }
        using Clock = std::chrono::steady_clock;
        const auto admitDeadline = Clock::now() + std::chrono::milliseconds(250);
        while (receiver.telemetry().acceptedPackets == 0 && Clock::now() < admitDeadline) {
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        QVERIFY(receiver.telemetry().acceptedPackets > 0);
        // Hold the worker at its next pacing read, well inside the 80 ms
        // jitter hold, so the rest of the batch arrives before any release.
        bus->blockNextOutputPacingForTesting();
        QVERIFY(bus->waitForOutputPacingGateForTesting(std::chrono::milliseconds(250)));
        QCOMPARE(receiver.decodedPackets(), quint64(0));
        for (int packet = kFirst; packet < kBacklog; ++packet) {
            receiver.submit(packets.at(packet));
        }

        std::jthread device([bus](std::stop_token stop) {
            const auto started = Clock::now();
            quint64 callback = 1;
            while (!stop.stop_requested()) {
                std::this_thread::sleep_until(started + std::chrono::nanoseconds(
                    callback * 480ull * 1'000'000'000ull / 48'000ull));
                if (stop.stop_requested()) { break; }
                bus->render(480);
                ++callback;
            }
        });
        bus->releaseOutputPacingGateForTesting();

        const auto started = Clock::now();
        for (int packet = kBacklog; packet < kBacklog + kFollowing; ++packet) {
            std::this_thread::sleep_until(
                started + std::chrono::milliseconds(40 * (packet - kBacklog + 1)));
            receiver.submit(packets.at(packet));
        }
        RemoteAudioReceiverTelemetry telemetry;
        const auto deadline = Clock::now() + std::chrono::seconds(1);
        while (Clock::now() < deadline) {
            telemetry = receiver.telemetry();
            if (!receiver.isRunning()
                || telemetry.decodedPackets >= quint64(1 + kFollowing)) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        device.request_stop();
        device.join();
        QCoreApplication::processEvents();
        const bool running = receiver.isRunning();
        receiver.stop();

        const QString evidence = QStringLiteral("running=%1 restarts=%2 reason=%3")
            .arg(running).arg(restarts.count())
            .arg(restarts.isEmpty() ? QStringLiteral("none")
                                    : restarts.first().first().toString());
        QVERIFY2(running && restarts.isEmpty(), qPrintable(evidence));
        QCOMPARE(errors.count(), 0);
        // Whether an older packet was admitted, still queued or dropped on
        // arrival, everything before the newest backlog packet is discarded
        // at start and never heard; loss accounting restarts with the kept one.
        QCOMPARE(telemetry.startDiscardedPackets, quint64(kBacklog - 1));
        // Fix wave M1: packets admitted and then discarded at start count as
        // discarded, not admitted, so the two counts reconcile.
        QCOMPARE(telemetry.acceptedPackets, quint64(1 + kFollowing));
        QCOMPARE(telemetry.decodedPackets, quint64(1 + kFollowing));
        QCOMPARE(telemetry.concealedPackets, quint64(0));
        QCOMPARE(telemetry.missingPackets, quint64(0));
        QCOMPARE(telemetry.expectedPackets, quint64(1 + kFollowing));
        QCOMPARE(receiver.rateMatcherUnderflows(), 0);
        QCOMPARE(receiver.rateMatcherOverflows(), 0);
    }

    // Fix wave, Important 3: once playback has begun the start phase is over
    // and the normal overflow rule applies again. A burst larger than the
    // arrival queue after the first decoded packet must request a restart
    // (Fault::ArrivalBurst), not be silently trimmed as a connect backlog.
    void burstAfterPlaybackStartsStillRestarts()
    {
        constexpr quint32 kSsrc = 845;
        constexpr int kFirst = 3;
        constexpr int kBurst = AudioJitterBuffer::windowPackets(
            AudioJitterBuffer::kDefaultPacketDurationNs) + 4;
        OpusAudioEncoder encoder;
        const QVector<float> pcm(3840, 0.1f);
        QVector<QByteArray> packets;
        for (int packet = 0; packet < kFirst + kBurst; ++packet) {
            const auto encoded = encoder.encode(
                pcm, quint16(packet), quint32(packet) * 1920u, kSsrc);
            QCOMPARE(encoded.status, OpusAudioCodecStatus::Accepted);
            packets.append(encoded.packet);
        }

        AudioEngine engine;
        engine.setVolume(1.0f);
        auto sink = std::make_unique<PacedAudioBus>();
        auto* bus = sink.get();
        engine.setSpeakersBusForTest(std::move(sink));
        RemoteAudioReceiver receiver(&engine);
        struct ReleasePacingGate {
            PacedAudioBus* bus;
            ~ReleasePacingGate() { bus->releaseOutputPacingGateForTesting(); }
        } releaseGate{bus};
        QSignalSpy errors(&receiver, &RemoteAudioReceiver::errorOccurred);
        QSignalSpy restarts(&receiver, &RemoteAudioReceiver::restartRequested);
        QVERIFY(receiver.start(kSsrc, 0));

        using Clock = std::chrono::steady_clock;
        std::jthread device([bus](std::stop_token stop) {
            const auto started = Clock::now();
            quint64 callback = 1;
            while (!stop.stop_requested()) {
                std::this_thread::sleep_until(started + std::chrono::nanoseconds(
                    callback * 480ull * 1'000'000'000ull / 48'000ull));
                if (stop.stop_requested()) { break; }
                bus->render(480);
                ++callback;
            }
        });
        for (int packet = 0; packet < kFirst; ++packet) {
            receiver.submit(packets.at(packet));
        }
        const auto playDeadline = Clock::now() + std::chrono::seconds(1);
        while (receiver.decodedPackets() == 0 && Clock::now() < playDeadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        QVERIFY(receiver.decodedPackets() >= 1);
        const quint64 startDiscarded = receiver.telemetry().startDiscardedPackets;

        // Hold the worker at its next pacing read so the whole burst lands
        // in the arrival queue before it can drain any of it.
        bus->blockNextOutputPacingForTesting();
        QVERIFY(bus->waitForOutputPacingGateForTesting(std::chrono::milliseconds(250)));
        for (int packet = kFirst; packet < kFirst + kBurst; ++packet) {
            receiver.submit(packets.at(packet));
        }
        bus->releaseOutputPacingGateForTesting();

        QTRY_COMPARE_WITH_TIMEOUT(restarts.count(), 1, 1000);
        device.request_stop();
        device.join();
        const QString reason = restarts.first().at(0).toString();
        QVERIFY2(reason.contains(QStringLiteral("arrival queue exceeded its latency bound")),
                 qPrintable(reason));
        QCOMPARE(restarts.first().at(1).value<RemoteAudioReceiver::Fault>(),
                 RemoteAudioReceiver::Fault::ArrivalBurst);
        QCOMPARE(receiver.telemetry().startDiscardedPackets, startDiscarded);
        QCOMPARE(errors.count(), 0);
        receiver.stop();
    }

    // A backlog paced just slowly enough for the worker to keep up never
    // overflows the arrival queue, but before any release it fills the
    // eight-packet jitter window and the ninth packet used to force a "fresh
    // context after a stream gap [jitterPackets=8]" restart at connect.
    void pacedBacklogBeforePlaybackStaysInsideJitterWindow()
    {
        constexpr quint32 kSsrc = 844;
        constexpr int kBacklog = 30;
        constexpr int kFollowing = 25;
        constexpr int kTotal = kBacklog + kFollowing;
        OpusAudioEncoder encoder;
        const QVector<float> pcm(3840, 0.1f);
        QVector<QByteArray> packets;
        for (int packet = 0; packet < kTotal; ++packet) {
            const auto encoded = encoder.encode(
                pcm, quint16(packet), quint32(packet) * 1920u, kSsrc);
            QCOMPARE(encoded.status, OpusAudioCodecStatus::Accepted);
            packets.append(encoded.packet);
        }

        AudioEngine engine;
        engine.setVolume(1.0f);
        auto sink = std::make_unique<PacedAudioBus>();
        auto* bus = sink.get();
        engine.setSpeakersBusForTest(std::move(sink));
        RemoteAudioReceiver receiver(&engine);
        QSignalSpy errors(&receiver, &RemoteAudioReceiver::errorOccurred);
        QSignalSpy restarts(&receiver, &RemoteAudioReceiver::restartRequested);
        QVERIFY(receiver.start(kSsrc, 0));
        std::jthread device([bus](std::stop_token stop) {
            using Clock = std::chrono::steady_clock;
            const auto started = Clock::now();
            quint64 callback = 1;
            while (!stop.stop_requested()) {
                std::this_thread::sleep_until(started + std::chrono::nanoseconds(
                    callback * 480ull * 1'000'000'000ull / 48'000ull));
                if (stop.stop_requested()) { break; }
                bus->render(480);
                ++callback;
            }
        });

        // 1 ms apart: the whole backlog lands inside the 80 ms jitter hold.
        using Clock = std::chrono::steady_clock;
        const auto started = Clock::now();
        for (int packet = 0; packet < kBacklog; ++packet) {
            std::this_thread::sleep_until(started + std::chrono::milliseconds(packet));
            receiver.submit(packets.at(packet));
        }
        const auto following = Clock::now();
        for (int packet = kBacklog; packet < kTotal; ++packet) {
            std::this_thread::sleep_until(
                following + std::chrono::milliseconds(40 * (packet - kBacklog + 1)));
            receiver.submit(packets.at(packet));
        }
        RemoteAudioReceiverTelemetry telemetry;
        const auto deadline = Clock::now() + std::chrono::seconds(1);
        while (Clock::now() < deadline) {
            telemetry = receiver.telemetry();
            if (!receiver.isRunning()
                || telemetry.decodedPackets + telemetry.startDiscardedPackets
                    >= quint64(kTotal)) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        device.request_stop();
        device.join();
        QCoreApplication::processEvents();
        const bool running = receiver.isRunning();
        receiver.stop();

        const QString evidence = QStringLiteral("running=%1 restarts=%2 reason=%3")
            .arg(running).arg(restarts.count())
            .arg(restarts.isEmpty() ? QStringLiteral("none")
                                    : restarts.first().first().toString());
        QVERIFY2(running && restarts.isEmpty(), qPrintable(evidence));
        QCOMPARE(errors.count(), 0);
        // Every packet is either discarded before playback or played; none
        // is concealed or reported missing.
        QVERIFY(telemetry.startDiscardedPackets > 0);
        QCOMPARE(telemetry.decodedPackets + telemetry.startDiscardedPackets, quint64(kTotal));
        QCOMPARE(telemetry.concealedPackets, quint64(0));
        QCOMPARE(telemetry.missingPackets, quint64(0));
        QCOMPARE(receiver.rateMatcherUnderflows(), 0);
        QCOMPARE(receiver.rateMatcherOverflows(), 0);
    }

    // R-R3-23: a lossless context plays 4 ms L16 packets through the same
    // jitter queue, rate matcher and speaker. A lost packet is 4 ms of
    // silence, counted as a gap; packets of the other profile are refused
    // at the header; the stereo placement is exact.
    void losslessPlaybackLossBecomesSilence()
    {
        constexpr quint32 kSsrc = 432;
        constexpr int kLost = 60;
        AudioEngine engine;
        engine.setVolume(1.0f);
        auto sink = std::make_unique<PacedAudioBus>();
        auto* bus = sink.get();
        engine.setSpeakersBusForTest(std::move(sink));
        RemoteAudioReceiver receiver(&engine);
        QSignalSpy errors(&receiver, &RemoteAudioReceiver::errorOccurred);
        QSignalSpy restarts(&receiver, &RemoteAudioReceiver::restartRequested);
        QVERIFY(receiver.start(kSsrc, 0, RemoteAudioProfile::Lossless));
        QCOMPARE(receiver.profile(), RemoteAudioProfile::Lossless);

        // An Opus packet is not this context's audio.
        OpusAudioEncoder encoder;
        const auto opus = encoder.encode(QVector<float>(3840, 0.1f), 0, 0, kSsrc);
        QCOMPARE(opus.status, OpusAudioCodecStatus::Accepted);
        receiver.submit(opus.packet);
        QCOMPARE(receiver.telemetry().rejectedHeaders, quint64(1));

        QTimer device;
        device.setTimerType(Qt::PreciseTimer);
        device.setInterval(1);
        QElapsedTimer deviceClock;
        deviceClock.start();
        quint64 renderedFrames = 0;
        connect(&device, &QTimer::timeout, this, [&] {
            const quint64 due = quint64(deviceClock.nsecsElapsed()) * 48000 / 1'000'000'000;
            while (due >= renderedFrames + 480) {
                bus->render(480);
                renderedFrames += 480;
            }
        });
        device.start();
        int packet = 0;
        QTimer source;
        source.setTimerType(Qt::PreciseTimer);
        source.setInterval(1);
        QElapsedTimer sourceClock;
        sourceClock.start();
        connect(&source, &QTimer::timeout, this, [&] {
            const int duePackets = int(sourceClock.elapsed() / 4);
            while (packet < duePackets) {
                const QByteArray next = losslessTonePacket(packet, kSsrc);
                if (packet != kLost) { receiver.submit(next); }
                if (packet == 20) { receiver.submit(next); } // a duplicate is no gap
                ++packet;
            }
        });
        source.start();
        QTRY_VERIFY_WITH_TIMEOUT(receiver.decodedPackets() >= 300 || !errors.isEmpty()
                                 || !restarts.isEmpty(), 5000);
        source.stop();
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.first().first().toString()));
        QVERIFY2(restarts.isEmpty(), restarts.isEmpty() ? "" : qPrintable(restarts.first().first().toString()));
        const auto telemetry = receiver.telemetry();
        QCOMPARE(telemetry.missingPackets, quint64(1));
        QCOMPARE(telemetry.duplicatePackets, quint64(1));
        QVERIFY(telemetry.concealedPackets >= 1);
        QCOMPARE(telemetry.rejectedHeaders, quint64(1));
        QVERIFY(telemetry.expectedPackets >= quint64(300));
        // Valid lossless payload is counted: 768 bytes a packet.
        QVERIFY(telemetry.receivedAudioPayloadBytes
                >= telemetry.acceptedPackets * quint64(PcmAudioCodecConfig::kPayloadBytes));
        QCOMPARE(receiver.rateMatcherUnderflows(), 0);
        QCOMPARE(receiver.rateMatcherOverflows(), 0);
        receiver.stop();
        device.stop();

        // Placement: each tone stays on its own side, far more cleanly than
        // Opus allows (arrivalBurstsRemainBounded and the Opus tests use 8x).
        const auto toneAmplitude = [&](int channel, double hz) {
            double real = 0, imag = 0;
            for (int i = channel; i < bus->heard.size(); i += 2) {
                const double phase = 2 * 3.141592653589793 * hz * (i / 2) / 48000;
                real += bus->heard[i] * std::cos(phase);
                imag += bus->heard[i] * std::sin(phase);
            }
            return std::hypot(real, imag);
        };
        QVERIFY(toneAmplitude(0, 997) > 100 * toneAmplitude(1, 997));
        QVERIFY(toneAmplitude(1, 1703) > 100 * toneAmplitude(0, 1703));
        // The lost packet is heard as silence: a run of near-zero samples on
        // both sides far longer than any zero crossing of the tones.
        int longestQuiet = 0;
        int quiet = 0;
        for (int i = 0; i + 1 < bus->heard.size(); i += 2) {
            const bool silent = std::abs(bus->heard[i]) < 0.01f && std::abs(bus->heard[i + 1]) < 0.01f;
            quiet = silent ? quiet + 1 : 0;
            longestQuiet = std::max(longestQuiet, quiet);
        }
        QVERIFY2(longestQuiet >= 100, qPrintable(QString::number(longestQuiet)));

        // An Opus context refuses lossless packets at the header.
        QVERIFY(receiver.start(kSsrc, 0));
        QCOMPARE(receiver.profile(), RemoteAudioProfile::Opus);
        receiver.submit(losslessTonePacket(0, kSsrc));
        QCOMPARE(receiver.telemetry().rejectedHeaders, quint64(1));
        receiver.stop();
    }

    // R-R3-43: a receiver for an app, with no speaker at all (no
    // AudioEngine, so none can be opened). Packets reordered on the network
    // come out in stream order, paced by their arrival (the Core's clock),
    // and a lost packet is concealed (Opus) or silence (lossless). Nothing
    // asks for a restart.
    void pcmSinkPlaysInOrderWithoutASpeaker_data()
    {
        QTest::addColumn<bool>("lossless");
        QTest::newRow("lossless") << true;
        QTest::newRow("opus") << false;
    }
    void pcmSinkPlaysInOrderWithoutASpeaker()
    {
        QFETCH(bool, lossless);
        constexpr quint32 kSsrc = 0x4e520001;
        const int packets = lossless ? 60 : 12;
        const int lost = lossless ? 30 : 7;
        const int packetMs = lossless ? 4 : 40;
        const int packetFrames = lossless ? PcmAudioCodecConfig::kPacketFrames : 1920;
        CollectedPcm collected;
        RemoteAudioReceiver receiver(RemoteAudioReceiver::PcmSinkMode{collected.sink()});
        QVERIFY(receiver.isPcmSink());
        QSignalSpy errors(&receiver, &RemoteAudioReceiver::errorOccurred);
        QSignalSpy restarts(&receiver, &RemoteAudioReceiver::restartRequested);
        QVERIFY(receiver.start(kSsrc, 0, lossless ? RemoteAudioProfile::Lossless
                                                  : RemoteAudioProfile::Opus));
        QVERIFY(receiver.isRunning());

        // Each packet is louder than the last, so the order is audible.
        const auto level = [lossless](int packet) {
            return lossless ? float(packet + 1) / 1024.0f : 0.02f * float(packet + 1);
        };
        OpusAudioEncoder encoder;
        QList<QByteArray> wire;
        for (int packet = 0; packet < packets; ++packet) {
            if (lossless) {
                wire.append(losslessLevelPacket(packet, kSsrc, level(packet)));
            } else {
                QVector<float> pcm(3840);
                for (int i = 0; i < 1920; ++i) {
                    const double t = double(packet * 1920 + i) / 48000;
                    pcm[2 * i] = pcm[2 * i + 1]
                        = float(level(packet) * std::sin(t * 2 * 3.141592653589793 * 1000));
                }
                const auto encoded = encoder.encode(pcm, quint16(packet),
                                                    quint32(packet) * 1920u, kSsrc);
                QCOMPARE(encoded.status, OpusAudioCodecStatus::Accepted);
                wire.append(encoded.packet);
            }
            QVERIFY(!wire.constLast().isEmpty());
        }
        // Sent at the stream's own pace; packets 3 and 4 swap on the way
        // (3 lands a quarter packet after 4, well inside the reorder hold,
        // not on its concealment deadline one packet later), and one packet
        // never arrives.
        using Clock = std::chrono::steady_clock;
        const auto started = Clock::now();
        const auto at = [&](int slot, int quarter = 0) {
            return started + std::chrono::microseconds(1000 * packetMs * slot + 250 * packetMs * quarter);
        };
        for (int packet = 0; packet < packets; ++packet) {
            if (packet == 3) { continue; }
            std::this_thread::sleep_until(at(packet == 4 ? 3 : packet));
            if (packet != lost) { receiver.submit(wire.at(packet)); }
            if (packet == 4) {
                std::this_thread::sleep_until(at(3, 1));
                receiver.submit(wire.at(3));
            }
        }
        QTRY_VERIFY_WITH_TIMEOUT(collected.count() >= packets || !restarts.isEmpty()
                                 || !errors.isEmpty(), 3000);
        QCOMPARE(errors.count(), 0);
        QCOMPARE(restarts.count(), 0);
        const QList<QVector<float>> blocks = collected.snapshot();
        QVERIFY(blocks.size() >= packets);
        for (int packet = 0; packet < packets; ++packet) {
            QCOMPARE(blocks.at(packet).size(), packetFrames * 2);
        }
        const RemoteAudioReceiverTelemetry telemetry = receiver.telemetry();
        QCOMPARE(telemetry.missingPackets, quint64(1));
        QCOMPARE(telemetry.decodedPackets, quint64(packets - 1));
        QVERIFY(telemetry.concealedPackets >= 1);
        QCOMPARE(telemetry.rejectedHeaders, quint64(0));
        // Frames handed to the sink stand in for a speaker's progress.
        QVERIFY(telemetry.deviceConsumedFrames >= quint64(packets) * quint64(packetFrames));
        QVERIFY(telemetry.lastDeviceProgressAgeMs.has_value());
        QVERIFY(!telemetry.speakerQueuedMs.has_value());
        QVERIFY(!telemetry.driftRatio.has_value());
        QVERIFY(!telemetry.playout.has_value());
        QVERIFY(telemetry.release.has_value());
        if (lossless) {
            // Exactly the sent levels, in stream order, and the lost packet
            // as silence.
            for (int packet = 0; packet < packets; ++packet) {
                const float expected = packet == lost ? 0.0f : level(packet);
                for (float sample : blocks.at(packet)) {
                    QVERIFY2(std::abs(sample - expected) < 1.0f / 16384.0f,
                             qPrintable(QStringLiteral("packet %1: %2, expected %3")
                                            .arg(packet).arg(sample).arg(expected)));
                }
            }
        } else {
            // Louder block by block, the swapped pair included, until the
            // concealed one; Opus does not play silence for it.
            for (int packet = 2; packet < lost; ++packet) {
                QVERIFY2(blockRms(blocks.at(packet)) > blockRms(blocks.at(packet - 1)),
                         qPrintable(QString::number(packet)));
            }
            QVERIFY(blockRms(blocks.at(lost)) > 0.0);
        }
        receiver.stop();
        QVERIFY(!receiver.isRunning());
        // Stopped means stopped: nothing more reaches the sink.
        const qsizetype afterStop = collected.count();
        receiver.submit(wire.constLast());
        QTest::qWait(150);
        QCOMPARE(collected.count(), afterStop);
    }

    // R-R3-43 (risk E3): when the Core goes quiet the sink hears silence
    // for the 500 ms the speaker would have waited, then nothing, and it
    // never asks for a restart, so a retired stream cannot loop. When
    // packets come back, far ahead, it plays them on their own timestamps.
    void pcmSinkIdlesOnSilenceAndResumesWithoutARestart()
    {
        constexpr quint32 kSsrc = 0x4e520002;
        CollectedPcm collected;
        RemoteAudioReceiver receiver(RemoteAudioReceiver::PcmSinkMode{collected.sink()});
        QSignalSpy errors(&receiver, &RemoteAudioReceiver::errorOccurred);
        QSignalSpy restarts(&receiver, &RemoteAudioReceiver::restartRequested);
        QVERIFY(receiver.start(kSsrc, 0, RemoteAudioProfile::Lossless));
        using Clock = std::chrono::steady_clock;
        const auto sendRun = [&](int first, int count) {
            const auto started = Clock::now();
            for (int i = 0; i < count; ++i) {
                std::this_thread::sleep_until(started + std::chrono::milliseconds(4 * i));
                receiver.submit(losslessLevelPacket(first + i, kSsrc, 0.25f));
            }
        };
        sendRun(0, 25);
        QTRY_VERIFY_WITH_TIMEOUT(receiver.telemetry().decodedPackets >= 25, 2000);

        // Quiet: silence, then nothing.
        QTest::qWait(900);
        const qsizetype idle = collected.count();
        const quint64 concealed = receiver.telemetry().concealedPackets;
        QVERIFY2(concealed >= 50 && concealed <= 160, qPrintable(QString::number(concealed)));
        QTest::qWait(400);
        QCOMPARE(collected.count(), idle);
        QCOMPARE(receiver.telemetry().concealedPackets, concealed);
        QCOMPARE(restarts.count(), 0);
        QCOMPARE(errors.count(), 0);
        QVERIFY(receiver.isRunning());
        for (const QVector<float>& block : collected.snapshot().mid(25)) {
            for (float sample : block) { QCOMPARE(sample, 0.0f); }
        }

        // The Core sends again, well past the reorder window.
        sendRun(2000, 25);
        QTRY_VERIFY_WITH_TIMEOUT(receiver.telemetry().decodedPackets >= 50
                                 || !restarts.isEmpty(), 2000);
        QCOMPARE(restarts.count(), 0);
        QCOMPARE(errors.count(), 0);
        const QList<QVector<float>> blocks = collected.snapshot();
        QVERIFY(blocks.size() >= idle + 25);
        for (qsizetype block = idle; block < idle + 25; ++block) {
            QVERIFY(std::abs(blocks.at(block).constFirst() - 0.25f) < 1.0f / 16384.0f);
        }
        receiver.stop();
    }

    // R-R3-23: the arrival queue is bounded in time, 320 ms, not in packets.
    // A 160 ms stall of lossless packets (forty, five times the old eight-
    // packet bound that restarted after 32 ms) plays on; a stall past 320 ms
    // still restarts as an arrival burst.
    void losslessStallIsBoundedInTime_data()
    {
        QTest::addColumn<int>("burst");
        QTest::addColumn<bool>("restarts");
        QTest::newRow("160 ms stall plays on") << 40 << false;
        QTest::newRow("340 ms stall restarts") << 85 << true;
    }
    void losslessStallIsBoundedInTime()
    {
        QFETCH(int, burst);
        QFETCH(bool, restarts);
        constexpr quint32 kSsrc = 846;
        constexpr int kFirst = 30;
        AudioEngine engine;
        engine.setVolume(1.0f);
        auto sink = std::make_unique<PacedAudioBus>();
        auto* bus = sink.get();
        engine.setSpeakersBusForTest(std::move(sink));
        RemoteAudioReceiver receiver(&engine);
        struct ReleasePacingGate {
            PacedAudioBus* bus;
            ~ReleasePacingGate() { bus->releaseOutputPacingGateForTesting(); }
        } releaseGate{bus};
        QSignalSpy errors(&receiver, &RemoteAudioReceiver::errorOccurred);
        QSignalSpy restartSpy(&receiver, &RemoteAudioReceiver::restartRequested);
        QVERIFY(receiver.start(kSsrc, 0, RemoteAudioProfile::Lossless));

        using Clock = std::chrono::steady_clock;
        std::jthread device([bus](std::stop_token stop) {
            const auto started = Clock::now();
            quint64 callback = 1;
            while (!stop.stop_requested()) {
                std::this_thread::sleep_until(started + std::chrono::nanoseconds(
                    callback * 480ull * 1'000'000'000ull / 48'000ull));
                if (stop.stop_requested()) { break; }
                bus->render(480);
                ++callback;
            }
        });
        // Playback begins on a steady 4 ms stream.
        const auto firstStart = Clock::now();
        for (int packet = 0; packet < kFirst; ++packet) {
            std::this_thread::sleep_until(firstStart + std::chrono::milliseconds(4 * packet));
            receiver.submit(losslessTonePacket(packet, kSsrc));
        }
        const auto playDeadline = Clock::now() + std::chrono::seconds(1);
        while (receiver.decodedPackets() == 0 && Clock::now() < playDeadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        QVERIFY(receiver.decodedPackets() >= 1);

        // The worker is held at its next pacing read while the whole stall's
        // packets arrive at once.
        bus->blockNextOutputPacingForTesting();
        QVERIFY(bus->waitForOutputPacingGateForTesting(std::chrono::milliseconds(250)));
        for (int packet = kFirst; packet < kFirst + burst; ++packet) {
            receiver.submit(losslessTonePacket(packet, kSsrc));
        }
        bus->releaseOutputPacingGateForTesting();

        if (restarts) {
            QTRY_COMPARE_WITH_TIMEOUT(restartSpy.count(), 1, 1000);
            QCOMPARE(restartSpy.first().at(1).value<RemoteAudioReceiver::Fault>(),
                     RemoteAudioReceiver::Fault::ArrivalBurst);
        } else {
            // Every packet of the stall is played, none of it concealed and
            // no restart, well before the 500 ms no-packet rule.
            QTRY_VERIFY_WITH_TIMEOUT(receiver.decodedPackets() >= quint64(kFirst + burst)
                                     || !restartSpy.isEmpty(), 400);
            QVERIFY2(restartSpy.isEmpty(),
                     restartSpy.isEmpty() ? "" : qPrintable(restartSpy.first().first().toString()));
            QCOMPARE(receiver.telemetry().missingPackets, quint64(0));
            QCOMPARE(receiver.rateMatcherOverflows(), 0);
        }
        device.request_stop();
        device.join();
        QCOMPARE(errors.count(), 0);
        receiver.stop();
    }
    // R-R3-35: the receiver's clock is injected (here the steady clock plus
    // 5 s, as a Core-independent domain) and every time it reports is on
    // it. While lossless audio plays it publishes when a known RTP time
    // will be heard (the newest matched packet's end, behind the matcher
    // fill, the speaker queue and the device's reported latency) and when
    // the newest packet left the reorder buffer. Both vanish with stop().
    void playoutAndReleaseTimesUseTheInjectedClock()
    {
        constexpr quint32 kSsrc = 435;
        constexpr qint64 kOffsetNs = 5'000'000'000;
        constexpr qint64 kDeviceNs = 12'000'000;
        AudioEngine engine;
        engine.setVolume(1.0f);
        auto sink = std::make_unique<PacedAudioBus>();
        auto* bus = sink.get();
        bus->deviceLatencyNs = kDeviceNs;
        engine.setSpeakersBusForTest(std::move(sink));
        QElapsedTimer steady;
        steady.start();
        RemoteAudioReceiver receiver(&engine, nullptr,
                                     [&steady] { return kOffsetNs + steady.nsecsElapsed(); });
        QVERIFY(receiver.nowNs() >= kOffsetNs);
        QSignalSpy errors(&receiver, &RemoteAudioReceiver::errorOccurred);
        QSignalSpy restarts(&receiver, &RemoteAudioReceiver::restartRequested);
        QVERIFY(receiver.start(kSsrc, 0, RemoteAudioProfile::Lossless));
        QVERIFY(!receiver.telemetry().playout);
        QVERIFY(!receiver.telemetry().release);

        QTimer device;
        device.setTimerType(Qt::PreciseTimer);
        device.setInterval(1);
        QElapsedTimer deviceClock;
        deviceClock.start();
        quint64 renderedFrames = 0;
        connect(&device, &QTimer::timeout, this, [&] {
            const quint64 due = quint64(deviceClock.nsecsElapsed()) * 48000 / 1'000'000'000;
            while (due >= renderedFrames + 480) {
                bus->render(480);
                renderedFrames += 480;
            }
        });
        device.start();
        int packet = 0;
        QTimer source;
        source.setTimerType(Qt::PreciseTimer);
        source.setInterval(1);
        QElapsedTimer sourceClock;
        sourceClock.start();
        connect(&source, &QTimer::timeout, this, [&] {
            const int duePackets = int(sourceClock.elapsed() / 4);
            while (packet < duePackets) {
                receiver.submit(losslessTonePacket(packet, kSsrc));
                ++packet;
            }
        });
        source.start();
        QTRY_VERIFY_WITH_TIMEOUT(receiver.decodedPackets() >= 150 || !errors.isEmpty()
                                 || !restarts.isEmpty(), 5000);
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.first().first().toString()));
        QVERIFY2(restarts.isEmpty(), restarts.isEmpty() ? "" : qPrintable(restarts.first().first().toString()));
        const qint64 readNs = receiver.nowNs();
        const auto telemetry = receiver.telemetry();
        const qint64 submittedEnd = qint64(packet) * PcmAudioCodecConfig::kPacketFrames;
        source.stop();

        QVERIFY(telemetry.playout);
        const RemoteAudioPlayoutPoint& playout = *telemetry.playout;
        // On the injected clock, measured within the last second.
        QVERIFY(playout.measuredNs <= readNs);
        QVERIFY(readNs - playout.measuredNs < 1'000'000'000);
        // A packet end on the lossless grid, of audio that was sent.
        QCOMPARE(playout.rtpTimestamp % quint32(PcmAudioCodecConfig::kPacketFrames), 0U);
        QVERIFY(playout.rtpTimestamp > 0);
        QVERIFY(qint64(playout.rtpTimestamp) <= submittedEnd);
        QVERIFY(playout.speakerQueuedFrames >= 0 && playout.speakerQueuedFrames <= 4800);
        QVERIFY(playout.matcherFillFrames >= 0);
        QCOMPARE(playout.deviceLatencyNs, std::optional<qint64>(kDeviceNs));
        // I1: a lossless context has no codec delay; the rate matcher's
        // filter delay is counted, and half the bus's 480-frame callback.
        QCOMPARE(playout.pipelineDelayFrames, RemoteAudioRateMatcher::kFilterDelayFrames);
        QCOMPARE(playout.callbackFrames, bus->callbackFrames);
        QVERIFY(playout.readWindowNs >= 0 && playout.readWindowNs < 1'000'000'000);
        QCOMPARE(playout.playoutNs(), playout.measuredNs
            + (2 * qint64(playout.matcherFillFrames + playout.speakerQueuedFrames
                          + playout.pipelineDelayFrames) + bus->callbackFrames)
                * 1'000'000'000 / 96000 + kDeviceNs);
        QCOMPARE(playout.accuracyNs(), qint64(5'000'000) + (playout.readWindowNs + 1) / 2);

        QVERIFY(telemetry.release);
        const RemoteAudioReleasePoint& release = *telemetry.release;
        QCOMPARE(release.rtpTimestamp % quint32(PcmAudioCodecConfig::kPacketFrames), 0U);
        QVERIFY(release.releasedNs >= kOffsetNs && release.releasedNs <= readNs);
        QVERIFY(qint64(release.rtpTimestamp) <= submittedEnd);
        // Arrival ages are on the same clock, so they stay small.
        QVERIFY(telemetry.lastAdmittedPacketAgeMs && *telemetry.lastAdmittedPacketAgeMs < 1000);

        receiver.stop();
        device.stop();
        const auto stopped = receiver.telemetry();
        QVERIFY(!stopped.playout);
        QVERIFY(!stopped.release);
        // A new context starts with neither.
        QVERIFY(receiver.start(kSsrc, 0, RemoteAudioProfile::Lossless));
        QVERIFY(!receiver.telemetry().playout);
        QVERIFY(!receiver.telemetry().release);
        receiver.stop();
    }

    // R-R3-23: remote playback begins on every rate and channel count the
    // Devices page offers (DeviceCard kSampleRates x kChannels), names the
    // accepted format, and writes blocks in it. A PortAudio output ring
    // holds 100 ms at that format, so even 384 kHz leaves room for the
    // receiver's queue.
    void remotePlaybackAcceptsEveryOfferedSpeakerFormat_data()
    {
        QTest::addColumn<int>("rate");
        QTest::addColumn<int>("channels");
        for (int rate : {44100, 48000, 88200, 96000, 176400, 192000, 384000}) {
            for (int channels : {1, 2}) {
                QTest::addRow("%d Hz, %d ch", rate, channels) << rate << channels;
            }
        }
    }
    void remotePlaybackAcceptsEveryOfferedSpeakerFormat()
    {
        QFETCH(int, rate);
        QFETCH(int, channels);
        const AudioFormat format{rate, channels, AudioFormat::Sample::Float32};
        AudioEngine engine;
        engine.setVolume(0.5f);
        auto sink = std::make_unique<PacedAudioBus>();
        auto* bus = sink.get();
        QVERIFY(bus->open(format));
        engine.setSpeakersBusForTest(std::move(sink));
        QVERIFY(!engine.remotePlaybackFormat());
        QString error;
        QVERIFY2(engine.beginRemotePlayback(&error), qPrintable(error));
        QCOMPARE(engine.remotePlaybackFormat(), std::optional<AudioFormat>(format));

        // The ring a real PortAudio stream of this format gets, which the
        // paced bus reports as its capacity.
        const std::size_t ringSamples = PortAudioBus::outputRingSamples(rate, channels);
        QCOMPARE(ringSamples, std::max<std::size_t>(9600, std::size_t(rate / 10 * channels)));
        const auto pacing = engine.remotePlaybackPacing();
        QVERIFY(pacing);
        QCOMPARE(pacing->capacityFrames, int(ringSamples) / channels);
        // Two 10 ms blocks, a callback and one more block fit (the
        // receiver's queue and write, see playsOnEverySpeakerFormat).
        const int block = rate / 100;
        QVERIFY(4 * block + bus->callbackFrames <= pacing->capacityFrames);

        const QVector<float> pcm(qsizetype(block) * channels, 0.8f);
        QVERIFY(engine.writeRemotePlayback(pcm));
        QCOMPARE(engine.remotePlaybackPacing()->queuedFrames, block);
        bus->render(block);
        QCOMPARE(bus->heard.size(), qsizetype(block) * channels);
        for (float value : bus->heard) { QVERIFY(std::abs(value - 0.4f) < 0.00001f); }
        // Not a whole frame of this format.
        if (channels == 2) { QVERIFY(!engine.writeRemotePlayback(QVector<float>(3, 0.1f))); }
        // At most kMaxRemotePlaybackFrames frames a write.
        QVERIFY(!engine.writeRemotePlayback(
            QVector<float>(qsizetype(AudioEngine::kMaxRemotePlaybackFrames) * 2 + 2, 0.1f)));

        // The device reopens in another format: this playback's pacing and
        // writes stop, so the receiver asks for a fresh start.
        const AudioFormat other{rate == 48000 ? 44100 : 48000, channels,
                                AudioFormat::Sample::Float32};
        QVERIFY(bus->open(other));
        QVERIFY(!engine.remotePlaybackPacing());
        QVERIFY(!engine.writeRemotePlayback(pcm));
        engine.endRemotePlayback();
        QVERIFY(!engine.remotePlaybackFormat());
        QVERIFY(engine.beginRemotePlayback(&error));
        QCOMPARE(engine.remotePlaybackFormat(), std::optional<AudioFormat>(other));
        engine.endRemotePlayback();
    }

    // R-R3-23: what remote playback still refuses, in plain words: a
    // channel count or rate no Devices page setting makes, and a device
    // without playback timing.
    void remotePlaybackRefusesOnlyWhatItCannotPlay()
    {
        const auto refusal = [](const AudioFormat& format, bool timing) {
            AudioEngine engine;
            auto sink = std::make_unique<PacedAudioBus>();
            sink->open(format);
            sink->setOutputPacingAvailableForTesting(timing);
            engine.setSpeakersBusForTest(std::move(sink));
            QString error;
            const bool began = engine.beginRemotePlayback(&error);
            return began ? QString() : error;
        };
        using Sample = AudioFormat::Sample;
        const QString sixChannels = refusal({48000, 6, Sample::Float32}, true);
        QCOMPARE(sixChannels, QStringLiteral(
            "Remote audio cannot play on a speaker device set to 6 channels at 48000 Hz"));
        QVERIFY(!refusal({4000, 2, Sample::Float32}, true).isEmpty());
        QVERIFY(!refusal({768000, 2, Sample::Float32}, true).isEmpty());
        QVERIFY(!refusal({48000, 2, Sample::Int16}, true).isEmpty());
        const QString noTiming = refusal({44100, 2, Sample::Float32}, false);
        QCOMPARE(noTiming,
                 QStringLiteral("The selected speaker device does not report its playback timing"));
        QVERIFY(OperatorWording::isPlain(sixChannels));
        QVERIFY(OperatorWording::isPlain(noTiming));
        QVERIFY(refusal({44100, 1, Sample::Float32}, true).isEmpty());
    }

    // R-R3-23 / R-R3-07: remote audio plays on a speaker at 44.1, 48, 96 or
    // 192 kHz, stereo or mono. The speaker plays on the same steady clock
    // that paces the stream, as a real device plays continuously. For each
    // format: nothing restarts or runs short, the two tones come out at
    // their own pitch and level at the device's rate (a mono device hears
    // them mixed, each at half), and the delay readout places a sharp onset
    // where the speaker actually played it, inside its stated accuracy.
    void playsOnEverySpeakerFormat_data()
    {
        QTest::addColumn<int>("rate");
        QTest::addColumn<int>("channels");
        QTest::newRow("44.1 kHz stereo") << 44100 << 2;
        QTest::newRow("48 kHz stereo") << 48000 << 2;
        QTest::newRow("96 kHz stereo") << 96000 << 2;
        QTest::newRow("192 kHz stereo") << 192000 << 2;
        QTest::newRow("48 kHz mono") << 48000 << 1;
        QTest::newRow("44.1 kHz mono") << 44100 << 1;
    }
    void playsOnEverySpeakerFormat()
    {
        QFETCH(int, rate);
        QFETCH(int, channels);
        constexpr quint32 kSsrc = 623;
        constexpr qint64 kToneEndFrame = 72000;      // 1.5 s of two tones,
        constexpr qint64 kOnsetFrame = 96000 + 177;  // then silence, then
        constexpr double kOnsetHz = 200.0;           // a cosine mid-packet
        constexpr double kOnsetAmplitude = 0.5;
        // What the model leaves: where the half-height crossing falls after
        // the rate matcher's reconstruction, and the frame conventions at
        // each end (as tst_remote_audio_session's delay check allows).
        constexpr double kToleranceMs = 0.5;

        AudioEngine engine;
        engine.setVolume(1.0f);
        auto sink = std::make_unique<PacedAudioBus>();
        auto* bus = sink.get();
        QVERIFY(bus->open(AudioFormat{rate, channels, AudioFormat::Sample::Float32}));
        bus->callbackFrames = rate / 1000; // a 1 ms callback device
        engine.setSpeakersBusForTest(std::move(sink));
        // One steady timeline: the receiver's clock, the speaker's play
        // clock and the source's capture clock.
        QElapsedTimer clock;
        clock.start();
        RemoteAudioReceiver receiver(&engine, nullptr, [&clock] { return clock.nsecsElapsed(); });
        QSignalSpy errors(&receiver, &RemoteAudioReceiver::errorOccurred);
        QSignalSpy restarts(&receiver, &RemoteAudioReceiver::restartRequested);
        QVERIFY(receiver.start(kSsrc, 0, RemoteAudioProfile::Lossless));
        bus->setPlayClockForTesting([&clock] { return clock.nsecsElapsed(); });
        const qint64 playOriginNs = bus->playClockOriginNs();

        const auto frameAt = [](qint64 f) -> std::pair<float, float> {
            if (f < kToneEndFrame) {
                const double t = double(f) / 48000.0;
                return {float(0.2 * std::sin(t * 2 * 3.141592653589793 * 997)),
                        float(0.2 * std::sin(t * 2 * 3.141592653589793 * 1703))};
            }
            if (f < kOnsetFrame) { return {0.0f, 0.0f}; }
            const float v = float(kOnsetAmplitude
                * std::cos(2.0 * 3.141592653589793 * kOnsetHz * double(f - kOnsetFrame) / 48000.0));
            return {v, v};
        };
        // Each packet goes when its last frame is due on the timeline.
        const qint64 sourceOriginNs = clock.nsecsElapsed();
        int packet = 0;
        QVector<float> toneHeard;
        QTimer source;
        source.setTimerType(Qt::PreciseTimer);
        source.setInterval(1);
        connect(&source, &QTimer::timeout, this, [&] {
            const qint64 dueFrames = (clock.nsecsElapsed() - sourceOriginNs) * 48 / 1'000'000;
            while (qint64(packet + 1) * PcmAudioCodecConfig::kPacketFrames <= dueFrames) {
                receiver.submit(losslessPacketOf(packet, kSsrc, frameAt));
                ++packet;
            }
        });
        qint64 roughHeardFrame = -1;
        std::optional<RemoteAudioReceiverTelemetry> atOnset;
        QTimer speaker;
        speaker.setTimerType(Qt::PreciseTimer);
        speaker.setInterval(1);
        connect(&speaker, &QTimer::timeout, this, [&] {
            const qint64 before = bus->heard.size() / channels;
            if (bus->renderDue() <= 0) { return; }
            if (toneHeard.isEmpty()
                && qint64(packet) * PcmAudioCodecConfig::kPacketFrames >= kToneEndFrame) {
                toneHeard = bus->heard; // the source has just sent its last tone
            }
            if (qint64(packet) * PcmAudioCodecConfig::kPacketFrames < kOnsetFrame
                || roughHeardFrame >= 0) {
                return;
            }
            for (qint64 k = before; k < bus->heard.size() / channels; ++k) {
                if (std::abs(bus->heard.at(k * channels)) > 0.02f) {
                    roughHeardFrame = k;
                    atOnset = receiver.telemetry();
                    break;
                }
            }
        });
        source.start();
        speaker.start();
        QTRY_VERIFY_WITH_TIMEOUT(roughHeardFrame >= 0 || !errors.isEmpty() || !restarts.isEmpty(),
                                 10000);
        QTRY_VERIFY_WITH_TIMEOUT(bus->heard.size() / channels >= roughHeardFrame + rate / 50
                                     || !errors.isEmpty() || !restarts.isEmpty(), 5000);
        source.stop();
        speaker.stop();
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.first().first().toString()));
        QVERIFY2(restarts.isEmpty(), restarts.isEmpty() ? "" : qPrintable(restarts.first().first().toString()));

        // Played at the device's rate: the tones at their own pitch and
        // level over the last 250 ms heard before the tones ended.
        const qint64 toneFrames = rate / 4;
        const qint64 toneFirst = toneHeard.size() / channels - toneFrames;
        QVERIFY(toneFirst > rate / 2);
        const double leftLow = heardToneAmplitude(toneHeard, channels, 0, 997, rate, toneFirst, toneFrames);
        const double leftHigh = heardToneAmplitude(toneHeard, channels, 0, 1703, rate, toneFirst, toneFrames);
        if (channels == 2) {
            const double rightHigh = heardToneAmplitude(toneHeard, channels, 1, 1703, rate, toneFirst, toneFrames);
            const double rightLow = heardToneAmplitude(toneHeard, channels, 1, 997, rate, toneFirst, toneFrames);
            QVERIFY2(std::abs(leftLow - 0.2) < 0.01, qPrintable(QString::number(leftLow)));
            QVERIFY2(std::abs(rightHigh - 0.2) < 0.01, qPrintable(QString::number(rightHigh)));
            QVERIFY2(leftHigh < 0.005 && rightLow < 0.005,
                     qPrintable(QStringLiteral("%1 %2").arg(leftHigh).arg(rightLow)));
        } else {
            // (left + right) / 2: each tone at half its level.
            QVERIFY2(std::abs(leftLow - 0.1) < 0.005, qPrintable(QString::number(leftLow)));
            QVERIFY2(std::abs(leftHigh - 0.1) < 0.005, qPrintable(QString::number(leftHigh)));
        }

        // Once playing, the speaker never ran short: any silence it played
        // for want of audio came before the receiver's first write to it.
        // R-R3-21: the baseline is the count at that first write, so the
        // check covers the tones as well as what follows them.
        QVERIFY(bus->playedDryFramesAtFirstPushForTesting() >= 0);
        if (bus->playedDryFramesForTesting() != bus->playedDryFramesAtFirstPushForTesting()) {
            QStringList events;
            for (const auto& e : bus->dryEventsForTesting()) {
                events << QStringLiteral("%1 dry at %2 ms")
                              .arg(e.frames)
                              .arg(double(e.atFrame) * 1000.0 / rate, 0, 'f', 1);
            }
            qInfo().noquote() << QStringLiteral("first push at %1 ms; dry events: %2")
                .arg(double(bus->firstPushDueFrameForTesting()) * 1000.0 / rate, 0, 'f', 1)
                .arg(events.join(QStringLiteral(", ")));
        }
        QCOMPARE(bus->playedDryFramesForTesting(), bus->playedDryFramesAtFirstPushForTesting());

        // The delay readout at the moment the onset was first heard.
        QVERIFY(atOnset && atOnset->playout);
        const RemoteAudioPlayoutPoint& playout = *atOnset->playout;
        QCOMPARE(playout.deviceRateHz, rate);
        QCOMPARE(playout.callbackFrames, bus->callbackFrames);
        QCOMPARE(playout.pipelineDelayFrames, RemoteAudioRateMatcher::filterDelayFrames(rate));
        QVERIFY(atOnset->speakerQueuedMs);
        QVERIFY(*atOnset->speakerQueuedMs > 0.0 && *atOnset->speakerQueuedMs < 100.0);
        QCOMPARE(atOnset->underflows, 0);
        QCOMPARE(atOnset->overflows, 0);
        // The onset frame is heard where the newest matched frame's time
        // says, less the stream time between them (and the matcher's
        // stretch, zero on one steady clock but counted as the app does).
        const qint64 predictedNs = playout.playoutNs() - playout.matcherStretchNs()
            - (qint64(playout.rtpTimestamp) - kOnsetFrame) * 1'000'000'000 / 48000;
        float peak = 0.0f;
        for (qint64 k = roughHeardFrame; k < roughHeardFrame + rate / 50; ++k) {
            peak = std::max(peak, bus->heard.at(k * channels));
        }
        QVERIFY(peak > 0.3f);
        qint64 heardFrame = roughHeardFrame;
        while (bus->heard.at(heardFrame * channels) < peak / 2.0f) { ++heardFrame; }
        const qint64 heardNs = playOriginNs + heardFrame * 1'000'000'000 / rate;
        const double missMs = double(predictedNs - heardNs) / 1e6;
        const double accuracyMs = double(playout.accuracyNs()) / 1e6;
        qInfo().noquote() << QStringLiteral(
            "%1 Hz %2 ch: onset heard %3 ms after capture, readout misses by %4 ms "
            "(accuracy %5 ms); matcher %6 + speaker %7 frames, dry %8 frames")
            .arg(rate).arg(channels)
            .arg(double(heardNs - (sourceOriginNs + kOnsetFrame * 1'000'000'000 / 48000)) / 1e6, 0, 'f', 2)
            .arg(missMs, 0, 'f', 3).arg(accuracyMs, 0, 'f', 3)
            .arg(playout.matcherFillFrames).arg(playout.speakerQueuedFrames)
            .arg(bus->playedDryFramesForTesting());
        QVERIFY2(std::abs(missMs) <= accuracyMs + kToleranceMs,
                 qPrintable(QStringLiteral("missed by %1 ms, accuracy %2 ms").arg(missMs).arg(accuracyMs)));
        receiver.stop();
    }
};
QTEST_GUILESS_MAIN(TstRemoteAudioReceiver)
#include "tst_remote_audio_receiver.moc"
