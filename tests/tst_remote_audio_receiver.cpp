// no-port-check: NereusSDR-original remote audio receiver integration tests.
#include <QtTest>
#include <QTimer>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <cmath>
#include <chrono>
#include <thread>
#include "core/AudioEngine.h"
#include "core/session/media/AudioJitterBuffer.h"
#include "core/session/media/OpusAudioCodec.h"
#include "core/session/media/RemoteAudioReceiver.h"
#include "fakes/PacedAudioBus.h"
using namespace NereusSDR;
class TstRemoteAudioReceiver : public QObject {
    Q_OBJECT
private slots:
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

        QTRY_VERIFY_WITH_TIMEOUT(receiver.telemetry().driftRatio.has_value(), 1000);
        // The ratio stays inside WDSP rmatch's own clamp (rmatch.c control():
        // 0.96..1.04) and follows the controller as it adjusts. The
        // controller holds its initial ratio until create_rmatchV's 3.0 s
        // startup delay of audio has passed (rmatch.c:514); allow twice that.
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
        QCOMPARE(receiver.telemetry().receivedOpusPayloadBytes, quint64(0));
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
        QCOMPARE(receiver.telemetry().receivedOpusPayloadBytes, expectedBytes);

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
        QCOMPARE(stopped.receivedOpusPayloadBytes, expectedBytes);
        QVERIFY(!stopped.speakerQueuedMs);
        QVERIFY(receiver.start(987, 5760));
        QCOMPARE(receiver.telemetry().receivedOpusPayloadBytes, quint64(0));
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
        QCOMPARE(receiver.telemetry().receivedOpusPayloadBytes,
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
        constexpr int kBurst = AudioJitterBuffer::kMaxPackets + 4;
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

};
QTEST_GUILESS_MAIN(TstRemoteAudioReceiver)
#include "tst_remote_audio_receiver.moc"
