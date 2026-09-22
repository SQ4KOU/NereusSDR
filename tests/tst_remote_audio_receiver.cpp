// no-port-check: NereusSDR-original remote audio receiver integration tests.
#include <QtTest>
#include <QTimer>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <cmath>
#include <chrono>
#include <thread>
#include "core/AudioEngine.h"
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

};
QTEST_GUILESS_MAIN(TstRemoteAudioReceiver)
#include "tst_remote_audio_receiver.moc"
