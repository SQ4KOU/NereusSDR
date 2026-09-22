// no-port-check: NereusSDR-original. Remote telemetry lifecycle/presentation.
#include <QTest>
#include <QTemporaryDir>
#include "core/AppSettings.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/settings/SettingsProxy.h"
#include "gui/RemoteTelemetryController.h"
#include "models/RadioModel.h"
#include "fakes/LoopbackTransport.h"

using namespace NereusSDR;
using Metric = TelemetryHistory::Metric;

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
        QVERIFY(controller.bannerText().contains(QStringLiteral("Core 83 ms")));

        now += 1000;
        guiWire->observation.receivedPayloadBytes += 2000;
        guiWire->observation.acceptedPayloadBytes += 4000;
        playback.decodedPackets = 25;
        playback.deviceConsumedFrames = 48000;
        playback.lastAdmittedPacketAgeMs = 20;
        playback.lastDeviceProgressAgeMs = 5;
        controller.sampleNow();
        QCOMPARE(controller.current().controlRxKbps, std::optional<double>(16.0));
        QCOMPARE(controller.current().controlTxKbps, std::optional<double>(32.0));
        QVERIFY(controller.current().playbackActive);
        QCOMPARE(controller.history().rawObservationCount(Metric::RadioRxMbps), 1);
        QCOMPARE(controller.history().series(Metric::PlaybackDecodedPacketsPerSecond, now, 60).points.last().value, 25.0);
        QVERIFY(controller.detailText().contains(QStringLiteral("excluding media")));
        QVERIFY(controller.detailText().contains(QStringLiteral("measured 20000 ms ago")));

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
