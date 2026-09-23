// no-port-check: NereusSDR-original. Remote telemetry lifecycle/presentation.
#include <QTest>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTimer>
#include "core/AppSettings.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/session/media/DaemonMediaController.h"
#include "core/settings/SettingsProxy.h"
#include "gui/RemoteAudioStatus.h"
#include "gui/RemoteMediaController.h"
#include "gui/RemoteTelemetryController.h"
#include "models/RadioModel.h"
#include "fakes/LoopbackTransport.h"
#include "fakes/RemoteAudioSessionHarness.h"

using namespace NereusSDR;
using Metric = TelemetryHistory::Metric;

namespace {
// What the controller logs when a playing speaker stops reporting timing.
// Mirrors tst_remote_media_controller.cpp's speakerTimingLostLog(): the
// worker's pacing check or its next write notices first.
QRegularExpression remoteAudioSpeakerTimingLostLog()
{
    return QRegularExpression(QStringLiteral(
        "^Remote audio playback failed: (Speaker device timing became unavailable"
        "|Could not write remote audio to the speaker device) \\[ageMs="));
}
} // namespace

class ObservedLoopback final : public Test::LoopbackTransport {
public:
    ObservedLoopback() : LoopbackTransport(QStringLiteral("GUI")) {}
    SessionTransportTelemetry observation;
    std::optional<SessionTransportTelemetry> telemetry() const override
    { return isOpen() ? std::optional{observation} : std::nullopt; }
};

class TestRemoteTelemetry : public QObject {
    Q_OBJECT
private slots:
    void authenticSessionSeparatesSourcesAgesAndHistory()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        StationServer server(&station, settings, dir.path());
        server.setTelemetryEnabled(true);
        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        qint64 now = 10000;
        RemoteAudioReceiverTelemetry playback;
        RemoteTelemetryController controller(&client, nullptr, nullptr,
            [&] { return now; }, [&] { return playback; });
        QCOMPARE(controller.current().state, RemoteTelemetryView::State::Disconnected);
        QVERIFY(controller.bannerText().isEmpty());

        auto* guiWire = new ObservedLoopback;
        auto* coreWire = new Test::LoopbackTransport(QStringLiteral("Core"));
        guiWire->linkTo(coreWire);
        server.acceptTransport(coreWire);
        client.startSession(guiWire, server.token());
        QTRY_VERIFY(client.isHandshakeComplete());
        QCOMPARE(controller.current().state, RemoteTelemetryView::State::Waiting);
        QVERIFY(controller.bannerText().contains(QStringLiteral("waiting for telemetry")));

        guiWire->observation.pongRttMs = 83;
        guiWire->observation.pongAgeMs = 20000; // older than station freshness, still valid RTT
        playback.running = true;
        playback.generation = 4;
        playback.lifetimeUnderflows = 0;
        playback.lifetimeOverflows = 0;
        controller.sampleNow(); // baseline: no invented zero rate
        QVERIFY(!controller.current().controlRxKbps);

        StationTelemetrySnapshot sample;
        sample.sequence = 1;
        sample.sampledElapsedMs = 200;
        sample.radio.connected = true;
        sample.radio.rxMbps = 12.5;
        sample.radio.txMbps = 0.1;
        sample.radio.rttMs = 7;
        sample.radio.rttAgeMs = 10;
        sample.audio.active = true;
        sample.audio.contextGeneration = 2;
        sample.audio.encodedPacketsPerSecond = 25;
        QVERIFY(server.sendTelemetry(sample, server.sessionEpoch()));
        QTRY_COMPARE(controller.current().state, RemoteTelemetryView::State::Current);
        QCOMPARE(controller.current().radio.rxMbps, std::optional<double>(12.5));
        QCOMPARE(controller.current().radio.rttMs, std::optional<qint64>(7));
        QCOMPARE(controller.current().coreRttMs, std::optional<quint64>(83));
        QVERIFY(!controller.current().playbackActive); // running alone is not playback
        QVERIFY(controller.bannerText().contains(QStringLiteral("Radio ↓12.5 ↑0.1 Mbps")));
        QVERIFY(controller.bannerText().contains(QStringLiteral("Core RTT 83 ms")));
        // R-R3-23 Task 4: unmeasured wording before any packet/health values.
        QVERIFY(controller.detailText().contains(QStringLiteral("Arrival jitter: not measured yet.")));
        QVERIFY(controller.detailText().contains(QStringLiteral("Missing packets: none received yet.")));
        QVERIFY(controller.detailText().contains(QStringLiteral("Gaps filled: 0, concealed 40\u00A0ms intervals.")));
        QVERIFY(controller.detailText().contains(QStringLiteral("Speaker buffer: not measured yet.")));
        QVERIFY(controller.detailText().contains(QStringLiteral("Reorder buffer: not measured yet.")));

        now += 1000;
        guiWire->observation.receivedPayloadBytes += 2000;
        guiWire->observation.acceptedPayloadBytes += 4000;
        playback.decodedPackets = 25;
        playback.deviceConsumedFrames = 48000;
        playback.lastAdmittedPacketAgeMs = 20;
        playback.lastDeviceProgressAgeMs = 5;
        playback.arrivalJitterMs = 3.7;
        playback.missingPackets = 2;
        playback.expectedPackets = 100;
        playback.speakerQueuedMs = 41.2;
        playback.reorderQueuedMs = 80.0;
        controller.sampleNow();
        QCOMPARE(controller.current().controlRxKbps, std::optional<double>(16.0));
        QCOMPARE(controller.current().controlTxKbps, std::optional<double>(32.0));
        QVERIFY(controller.current().playbackActive);
        QCOMPARE(controller.history().rawObservationCount(Metric::RadioRxMbps), 1);
        QCOMPARE(controller.history().series(Metric::PlaybackDecodedPacketsPerSecond, now, 60).points.last().value, 25.0);
        QVERIFY(controller.detailText().contains(QStringLiteral("excluding media")));
        QVERIFY(controller.detailText().contains(QStringLiteral("measured 20000 ms ago")));
        // R-R3-23 Task 4: measured values, rounded, labelled with what they
        // are, no RTP/generation words.
        // U+00A0 keeps each number on the same line as its unit.
        QVERIFY(controller.detailText().contains(QStringLiteral("Arrival jitter: 4\u00A0ms, measured on this computer.")));
        QVERIFY(controller.detailText().contains(QStringLiteral("Missing packets: 2 of 100, sequence numbers never received.")));
        QVERIFY(controller.detailText().contains(QStringLiteral("Gaps filled: 0, concealed 40\u00A0ms intervals.")));
        QVERIFY(controller.detailText().contains(QStringLiteral("Speaker buffer: 41\u00A0ms, audio queued for this computer's speaker, not total delay.")));
        QVERIFY(controller.detailText().contains(QStringLiteral("Reorder buffer: 80\u00A0ms on this computer, packets held so that late arrivals play in order.")));

        // The previous context failed and restarted entirely between polls.
        // Its per-context counters have reset; the actual interruption remains.
        now += 1000;
        playback.generation = 5;
        playback.decodedPackets = 0;
        playback.underflows = 0;
        playback.lifetimeUnderflows = 1;
        controller.sampleNow();
        QCOMPARE(controller.history().series(Metric::PlaybackUnderflowsPerSecond, now, 60).points.last().value, 1.0);
        QVERIFY(controller.history().series(Metric::PlaybackUnderflowsPerSecond, now, 60).points.last().breakBefore);
        // A bounded unavailable lifecycle read cannot replace that baseline
        // with zero or invent a second event on the next stable observation.
        now += 100;
        playback.lifetimeUnderflows.reset(); playback.lifetimeOverflows.reset();
        controller.sampleNow();
        now += 900;
        playback.lifetimeUnderflows = 1; playback.lifetimeOverflows = 0;
        controller.sampleNow();
        QCOMPARE(controller.history().series(Metric::PlaybackUnderflowsPerSecond, now, 60).points.last().value, 0.0);

        // A timer tick must not append the old Core measurement again.
        now += 201;
        controller.sampleNow();
        QCOMPARE(controller.current().state, RemoteTelemetryView::State::Stale);
        QVERIFY(!controller.current().radio.rxMbps);
        QCOMPARE(controller.history().rawObservationCount(Metric::RadioRxMbps), 1);
        QVERIFY(controller.bannerText().contains(QStringLiteral("telemetry stale")));
        QCOMPARE(controller.current().coreRttMs, std::optional<quint64>(83));
        guiWire->observation.pongAgeMs = 60001;
        controller.sampleNow();
        QVERIFY(!controller.current().coreRttMs);

        client.disconnectFromStation(QStringLiteral("operator disconnect"));
        QCOMPARE(controller.current().state, RemoteTelemetryView::State::Disconnected);
        QVERIFY(!controller.current().controlRxKbps);
        QVERIFY(!controller.current().playbackActive);
        QCoreApplication::processEvents();
        ++now; // short reconnect must still break history
        auto* nextGui = new ObservedLoopback;
        auto* nextCore = new Test::LoopbackTransport(QStringLiteral("replacement"));
        nextGui->linkTo(nextCore);
        server.acceptTransport(nextCore);
        client.startSession(nextGui, server.token());
        QTRY_VERIFY(client.isHandshakeComplete());
        sample.sequence = 1;
        sample.sampledElapsedMs = 0;
        QVERIFY(server.sendTelemetry(sample, server.sessionEpoch()));
        QTRY_COMPARE(controller.current().state, RemoteTelemetryView::State::Current);
        const auto series = controller.history().series(Metric::RadioRxMbps, now, 60);
        QCOMPARE(series.points.size(), 2);
        QVERIFY(series.points.last().breakBefore);
        QVERIFY(!controller.current().controlRxKbps); // new transport baseline
        client.disconnectFromStation(QStringLiteral("done"));
    }

    // R-R3-23 Task 4: with a real media controller, bannerText()'s audio
    // word is the GUI's own persistent remote audio status, not the
    // running/decoding heuristic. Mute and a real speaker fault (PacedAudioBus
    // withdrawing its device timing, never a receiver signal emitted
    // directly) both drive it through a real session.
    void bannerWordTracksMuteAndARealSpeakerFaultThroughMediaController()
    {
        using State = RemoteAudioStatus::State;
        Test::RemoteAudioSessionHarness h;
        RemoteMediaController remoteMedia(&h.client, &h.remote, nullptr);
        DaemonMediaController daemonMedia(&h.server, &h.station);
        RemoteTelemetryController controller(&h.client, &remoteMedia);

        QTimer sourceTimer;
        sourceTimer.setInterval(10);
        sourceTimer.setTimerType(Qt::PreciseTimer);
        connect(&sourceTimer, &QTimer::timeout, &sourceTimer, [&h] { h.feedMixedTone(); });
        QTimer speakerTimer;
        speakerTimer.setInterval(10);
        speakerTimer.setTimerType(Qt::PreciseTimer);
        connect(&speakerTimer, &QTimer::timeout, &speakerTimer, [&h] {
            h.remoteBus->render(Test::RemoteAudioSessionHarness::kFrames);
        });
        sourceTimer.start();
        speakerTimer.start();

        h.connectSession();
        QTRY_COMPARE_WITH_TIMEOUT(remoteMedia.audioStatus().state, State::Playing, 15000);
        controller.sampleNow();
        QVERIFY(controller.bannerText().contains(QStringLiteral("Audio playing")));

        h.remote.audioEngine()->setMasterMuted(true);
        QCOMPARE(remoteMedia.audioStatus().state, State::MutedHere);
        QVERIFY(controller.bannerText().contains(QStringLiteral("Audio muted")));

        h.remote.audioEngine()->setMasterMuted(false);
        QTRY_COMPARE_WITH_TIMEOUT(remoteMedia.audioStatus().state, State::Playing, 15000);

        QTest::ignoreMessage(QtWarningMsg, remoteAudioSpeakerTimingLostLog());
        h.remoteBus->setOutputPacingAvailableForTesting(false);
        QTRY_COMPARE_WITH_TIMEOUT(remoteMedia.audioStatus().state, State::PlaybackProblem, 5000);
        QVERIFY(controller.bannerText().contains(QStringLiteral("Audio unavailable")));

        sourceTimer.stop();
        speakerTimer.stop();
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }

    void trafficSeparatesOpusAndResetsEachLifetime()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        StationServer server(&station, settings, dir.path());
        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        qint64 now = 10000;
        RemoteAudioReceiverTelemetry playback;
        playback.running = true;
        playback.generation = 7;
        playback.speakerQueuedMs = 25.0;
        std::optional<MediaPeerTelemetry> media{MediaPeerTelemetry{3, {}}};
        RemoteTelemetryController controller(&client, nullptr, nullptr,
            [&] { return now; }, [&] { return playback; }, [&] { return media; });
        auto* gui = new ObservedLoopback;
        auto* core = new Test::LoopbackTransport(QStringLiteral("Core"));
        gui->linkTo(core);
        server.acceptTransport(core);
        client.startSession(gui, server.token());
        QTRY_VERIFY(client.isHandshakeComplete());
        QVERIFY(!controller.current().coreGuiTotalKbps);
        QVERIFY(!controller.current().opusRxKbps);

        now += 2000; // actual elapsed time, not a presumed one-second tick
        gui->observation.receivedPayloadBytes = 1000;
        gui->observation.acceptedPayloadBytes = 500;
        media->traffic.receivedDisplayPayloadBytes = 100000;
        media->traffic.receivedRtpBytes = 10000;
        media->traffic.submittedDisplayPayloadBytes = 200;
        media->traffic.submittedRtpBytes = 300;
        playback.receivedOpusPayloadBytes = 9000; // subset, never add twice
        controller.sampleNow();
        QCOMPARE(controller.current().coreGuiRxKbps, std::optional<double>(444.0));
        QCOMPARE(controller.current().coreGuiTxKbps, std::optional<double>(4.0));
        QCOMPARE(controller.current().coreGuiTotalKbps, std::optional<double>(448.0));
        QCOMPARE(controller.current().opusRxKbps, std::optional<double>(36.0));
        QCOMPARE(controller.current().audioRtpRxKbps, std::optional<double>(40.0));
        QVERIFY(controller.bannerText().contains(QStringLiteral("Core ↓444.0 ↑4.0 total 448.0 kbps")));
        QCOMPARE(controller.history().series(Metric::SpeakerBufferMs, now, 60).points.last().value, 25.0);
        QVERIFY(controller.detailText().contains(QStringLiteral("End-to-end Opus latency is not measured")));
        QVERIFY(controller.detailText().contains(QStringLiteral("already included in total")));

        now += 1000;
        media->traffic.receivedDisplayPayloadBytes += 200000;
        controller.sampleNow();
        QVERIFY(controller.bannerText().contains(QStringLiteral("Core ↓1.6 ↑0.0 total 1.6 Mbps")));

        now += 1000;
        controller.sampleNow();
        QCOMPARE(controller.current().coreGuiTotalKbps, std::optional<double>(0.0));
        QCOMPARE(controller.current().opusRxKbps, std::optional<double>(0.0));

        // An audio restart gaps Opus but does not discard media totals.
        now += 1000;
        ++playback.generation;
        playback.receivedOpusPayloadBytes = 0;
        playback.speakerQueuedMs.reset();
        media->traffic.receivedRtpBytes += 500;
        controller.sampleNow();
        QCOMPARE(controller.current().coreGuiTotalKbps, std::optional<double>(4.0));
        QVERIFY(!controller.current().opusRxKbps);
        now += 1000;
        playback.receivedOpusPayloadBytes = 500;
        controller.sampleNow();
        QCOMPARE(controller.current().opusRxKbps, std::optional<double>(4.0));
        QVERIFY(controller.history().series(Metric::OpusPayloadRxKbps, now, 60).points.last().breakBefore);

        // A peer replacement gaps total and Opus, with independent baselines.
        now += 1000;
        media = MediaPeerTelemetry{4, {}};
        playback.receivedOpusPayloadBytes += 500;
        controller.sampleNow();
        QVERIFY(!controller.current().coreGuiTotalKbps);
        QVERIFY(!controller.current().opusRxKbps);
        now += 1000;
        controller.sampleNow();
        QCOMPARE(controller.current().coreGuiTotalKbps, std::optional<double>(0.0));
        QVERIFY(controller.history().series(Metric::CoreGuiTotalKbps, now, 60).points.last().breakBefore);

        now += 1000;
        media.reset();
        controller.sampleNow();
        QVERIFY(!controller.current().coreGuiTotalKbps); // unknown is not control-only zero
        QVERIFY(!controller.current().opusRxKbps);
        now += 1000;
        media = MediaPeerTelemetry{4, {}};
        controller.sampleNow();
        QVERIFY(!controller.current().coreGuiTotalKbps);
        now += 1000;
        media->traffic.receivedDisplayPayloadBytes = 1000;
        controller.sampleNow();
        QCOMPARE(controller.current().coreGuiTotalKbps, std::optional<double>(8.0));
        now += 1000;
        media->traffic.receivedDisplayPayloadBytes = 1; // counter reset, no negative rate
        controller.sampleNow();
        QVERIFY(!controller.current().coreGuiTotalKbps);
        now += 1000;
        controller.sampleNow();
        QCOMPARE(controller.current().coreGuiTotalKbps, std::optional<double>(0.0));
        QVERIFY(controller.history().series(Metric::CoreGuiTotalKbps, now, 60).points.last().breakBefore);
        client.disconnectFromStation(QStringLiteral("done"));
        QVERIFY(!controller.current().coreGuiTotalKbps);
        QVERIFY(!controller.current().opusRxKbps);
    }

    void olderCoreIsExplicitlyUnsupported()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        StationServer server(&station, settings, dir.path()); // capability disabled
        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        RemoteTelemetryController controller(&client, nullptr);
        auto* gui = new Test::LoopbackTransport(QStringLiteral("GUI"));
        auto* core = new Test::LoopbackTransport(QStringLiteral("Core"));
        gui->linkTo(core);
        server.acceptTransport(core);
        client.startSession(gui, server.token());
        QTRY_VERIFY(client.isHandshakeComplete());
        QCOMPARE(controller.current().state, RemoteTelemetryView::State::Unsupported);
        QVERIFY(controller.bannerText().contains(QStringLiteral("telemetry unsupported")));
        QVERIFY(!controller.current().radio.rxMbps);
        client.disconnectFromStation(QStringLiteral("done"));
    }
};

QTEST_MAIN(TestRemoteTelemetry)
#include "tst_remote_telemetry.moc"
