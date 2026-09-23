// =================================================================
// tests/tst_remote_diagnostics.cpp  (NereusSDR)
// =================================================================
// NereusSDR-original coverage for the remote telemetry presentation.
// =================================================================

#include <QtTest/QtTest>

#include <QComboBox>
#include <QLabel>
#include <QPixmap>
#include <QTabWidget>
#include <QTemporaryDir>

#include <optional>

#include "core/AppSettings.h"
#include "core/session/SessionTransport.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/settings/SettingsProxy.h"
#include "fakes/LoopbackTransport.h"
#include "gui/RemoteDiagnosticsDialog.h"
#include "gui/RemoteTelemetryController.h"
#include "gui/TimeSeriesGraphWidget.h"
#include "models/RadioModel.h"

using namespace NereusSDR;

namespace {

class ObservedLoopback final : public Test::LoopbackTransport {
public:
    ObservedLoopback() : LoopbackTransport(QStringLiteral("GUI")) {}

    SessionTransportTelemetry observation;

    std::optional<SessionTransportTelemetry> telemetry() const override
    {
        return isOpen() ? std::optional{observation} : std::nullopt;
    }
};

const TimeSeriesGraphWidget::Series* namedSeries(const TimeSeriesGraphWidget* graph,
                                                  const QString& label)
{
    for (const TimeSeriesGraphWidget::Series& series : graph->series()) {
        if (series.label == label) {
            return &series;
        }
    }
    return nullptr;
}

} // namespace

class TstRemoteDiagnostics : public QObject {
    Q_OBJECT

private slots:
    void constructionAndControllerLifecycleAreSafe()
    {
        RemoteDiagnosticsDialog nullDialog(nullptr);
        QVERIFY(nullDialog.findChild<QTabWidget*>(QStringLiteral("remoteDiagnosticsTabs")));
        QCOMPARE(nullDialog.rangeSeconds(), 5 * 60);

        auto* controller = new RemoteTelemetryController(nullptr, nullptr);
        RemoteDiagnosticsDialog dialog(controller);
        dialog.show();
        QTRY_VERIFY(dialog.isVisible());
        delete controller;
        QTRY_VERIFY(dialog.controller() == nullptr);
        auto* detail = dialog.findChild<QLabel*>(QStringLiteral("remoteDiagnosticsDetail"));
        QVERIFY(detail);
        QVERIFY(detail->text().contains(QStringLiteral("unavailable")));
    }

    void authenticatedTelemetryDrivesVisibleProductionGraphs()
    {
        QTemporaryDir directory;
        AppSettings settings(directory.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        StationServer server(&station, settings, directory.path());
        server.setTelemetryEnabled(true);
        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        qint64 now = 10000;
        RemoteAudioReceiverTelemetry playback;
        std::optional<MediaPeerTelemetry> media{MediaPeerTelemetry{1, {}}};
        RemoteTelemetryController controller(&client, nullptr, nullptr,
            [&] { return now; }, [&] { return playback; }, [&] { return media; });

        auto* guiWire = new ObservedLoopback;
        auto* coreWire = new Test::LoopbackTransport(QStringLiteral("Core"));
        guiWire->linkTo(coreWire);
        server.acceptTransport(coreWire);
        client.startSession(guiWire, server.token());
        QTRY_VERIFY(client.isHandshakeComplete());

        guiWire->observation.pongRttMs = 83;
        guiWire->observation.pongAgeMs = 20;
        playback.running = true;
        playback.generation = 4;
        controller.sampleNow(); // Establish transport/playback counter baselines.

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
        sample.audio.sourceFramesPerSecond = 48.0;
        sample.audio.encodedPacketsPerSecond = 25.0;
        sample.audio.sendAcceptedPerSecond = 24.0;
        sample.audio.sendRejectedPerSecond = 1.0;
        sample.audio.sourceDropsPerSecond = 2.0;
        QVERIFY(server.sendTelemetry(sample, server.sessionEpoch()));
        QTRY_COMPARE(controller.current().state, RemoteTelemetryView::State::Current);

        now += 1000;
        guiWire->observation.receivedPayloadBytes += 2000;
        guiWire->observation.acceptedPayloadBytes += 4000;
        media->traffic.receivedDisplayPayloadBytes += 100000;
        media->traffic.receivedRtpBytes += 3500;
        playback.receivedAudioPayloadBytes += 3000;
        playback.speakerQueuedMs = 20.0;
        playback.decodedPackets = 25;
        playback.concealedPackets = 2;
        playback.latePackets = 1;
        playback.underflows = 3;
        playback.overflows = 4;
        playback.lastAdmittedPacketAgeMs = 20;
        playback.lastDeviceProgressAgeMs = 5;
        controller.sampleNow();

        RemoteDiagnosticsDialog dialog(&controller);
        dialog.show();
        QTRY_VERIFY(dialog.isVisible());
        auto* totalGraph = dynamic_cast<TimeSeriesGraphWidget*>(dialog.findChild<QWidget*>(
            QStringLiteral("remoteTotalTrafficGraph")));
        auto* opusGraph = dynamic_cast<TimeSeriesGraphWidget*>(dialog.findChild<QWidget*>(
            QStringLiteral("remoteOpusTrafficGraph")));
        auto* bufferGraph = dynamic_cast<TimeSeriesGraphWidget*>(dialog.findChild<QWidget*>(
            QStringLiteral("remoteSpeakerBufferGraph")));
        QVERIFY(totalGraph && opusGraph && bufferGraph);
        const auto* total = namedSeries(totalGraph, QStringLiteral("Total"));
        const auto* opus = namedSeries(opusGraph, QStringLiteral("Opus payload"));
        const auto* buffer = namedSeries(bufferGraph, QStringLiteral("Speaker buffer"));
        QVERIFY(total && opus && buffer);
        QCOMPARE(total->unitSuffix, QStringLiteral(" kbps"));
        QCOMPARE(opus->unitSuffix, QStringLiteral(" kbps"));
        QCOMPARE(buffer->unitSuffix, QStringLiteral(" ms"));
        QCOMPARE(total->points.last().y(), 876.0);
        QCOMPARE(opus->points.last().y(), 24.0);
        QCOMPARE(namedSeries(opusGraph, QStringLiteral("Audio packets"))->points.last().y(), 28.0);
        QCOMPARE(buffer->points.last().y(), 20.0);
        QVERIFY(totalGraph->toolTip().contains(QStringLiteral("Opus is included")));
        QVERIFY(bufferGraph->toolTip().contains(QStringLiteral("Excludes network")));
        auto* radioGraph = dynamic_cast<TimeSeriesGraphWidget*>(dialog.findChild<QWidget*>(
            QStringLiteral("remoteRadioLinkGraph")));
        auto* controlGraph = dynamic_cast<TimeSeriesGraphWidget*>(dialog.findChild<QWidget*>(
            QStringLiteral("remoteControlPayloadGraph")));
        auto* rttGraph = dynamic_cast<TimeSeriesGraphWidget*>(dialog.findChild<QWidget*>(
            QStringLiteral("remoteRoundTripGraph")));
        QVERIFY(radioGraph);
        QVERIFY(controlGraph);
        QVERIFY(rttGraph);
        auto* selector = dialog.findChild<QComboBox*>(QStringLiteral("remoteDiagnosticsRange"));
        QVERIFY(selector);
        selector->setCurrentIndex(selector->findData(15 * 60));
        QCOMPARE(dialog.rangeSeconds(), 15 * 60);
        // These two readings are only one second apart. Inspect raw history;
        // a 15-minute view legitimately omits their incomplete 5s bucket.
        selector->setCurrentIndex(selector->findData(60));
        QCOMPARE(dialog.rangeSeconds(), 60);
        QTRY_VERIFY(namedSeries(radioGraph, QStringLiteral("Radio RX")) != nullptr);

        const auto* radioRx = namedSeries(radioGraph, QStringLiteral("Radio RX"));
        const auto* radioTx = namedSeries(radioGraph, QStringLiteral("Radio TX"));
        QVERIFY(radioRx && radioTx);
        QVERIFY(!radioRx->points.isEmpty() && !radioTx->points.isEmpty());
        QCOMPARE(radioRx->unitSuffix, QStringLiteral(" Mbps"));
        QCOMPARE(radioRx->points.constLast().y(), 12.5);
        QCOMPARE(radioTx->points.constLast().y(), 0.1);

        const auto* controlRx = namedSeries(controlGraph, QStringLiteral("Control RX"));
        const auto* controlTx = namedSeries(controlGraph, QStringLiteral("Control TX"));
        QVERIFY(controlRx && controlTx);
        QVERIFY(!controlRx->points.isEmpty() && !controlTx->points.isEmpty());
        QCOMPARE(controlRx->unitSuffix, QStringLiteral(" kbit/s"));
        QCOMPARE(controlRx->points.constLast().y(), 16.0);
        QCOMPARE(controlTx->points.constLast().y(), 32.0);

        const auto* radioRtt = namedSeries(rttGraph, QStringLiteral("Last radio RTT"));
        const auto* coreRtt = namedSeries(rttGraph, QStringLiteral("Last Core RTT"));
        QVERIFY(radioRtt && coreRtt);
        QVERIFY(!radioRtt->points.isEmpty() && !coreRtt->points.isEmpty());
        QCOMPARE(radioRtt->unitSuffix, QStringLiteral(" ms"));
        QCOMPARE(radioRtt->points.constLast().y(), 7.0);
        QCOMPARE(coreRtt->points.constLast().y(), 83.0);
        QVERIFY(rttGraph->toolTip().contains(QStringLiteral("hold the last measurement")));

        const QPixmap rendered = dialog.grab();
        QVERIFY(!rendered.isNull());

        now += 1000;
        media->traffic.receivedDisplayPayloadBytes += 200000;
        controller.sampleNow();
        QVERIFY(QMetaObject::invokeMethod(&dialog, "refresh", Qt::DirectConnection));
        total = namedSeries(totalGraph, QStringLiteral("Total"));
        QVERIFY(total);
        QCOMPARE(total->unitSuffix, QStringLiteral(" Mbps"));
        QCOMPARE(total->points.last().y(), 1.6);
        QCOMPARE(namedSeries(totalGraph, QStringLiteral("Core → GUI received"))->unitSuffix, total->unitSuffix);
        QCOMPARE(namedSeries(totalGraph, QStringLiteral("GUI → Core outgoing"))->unitSuffix, total->unitSuffix);

        client.disconnectFromStation(QStringLiteral("test reconnect"));
        QCoreApplication::processEvents();
        ++now;
        auto* replacementGui = new ObservedLoopback;
        auto* replacementCore = new Test::LoopbackTransport(QStringLiteral("replacement"));
        replacementGui->linkTo(replacementCore);
        server.acceptTransport(replacementCore);
        client.startSession(replacementGui, server.token());
        QTRY_VERIFY(client.isHandshakeComplete());
        sample.sequence = 1;
        sample.sampledElapsedMs = 0;
        sample.radio.rxMbps = 14.5;
        QVERIFY(server.sendTelemetry(sample, server.sessionEpoch()));
        QTRY_COMPARE(controller.current().state, RemoteTelemetryView::State::Current);
        QTRY_VERIFY(namedSeries(radioGraph, QStringLiteral("Radio RX")) != nullptr
                     && namedSeries(radioGraph, QStringLiteral("Radio RX"))->points.size() == 2);
        radioRx = namedSeries(radioGraph, QStringLiteral("Radio RX"));
        QVERIFY(radioRx->breakBefore.constLast());
        QCOMPARE(radioRx->points.constLast().y(), 14.5);
        client.disconnectFromStation(QStringLiteral("done"));
    }

    // R-R3-32/33: the Core tab graphs the Core computer's load, or says in
    // one line that this Core does not report it.
    void coreTabGraphsHostLoadOrSaysItIsNotReported()
    {
        RemoteDiagnosticsDialog nullDialog(nullptr);
        auto* nullLabel = nullDialog.findChild<QLabel*>(QStringLiteral("remoteCoreHostUnavailable"));
        QVERIFY(nullLabel);
        QVERIFY(!nullLabel->isHidden());
        QVERIFY(nullDialog.findChild<QWidget*>(QStringLiteral("remoteCoreCpuGraph"))->isHidden());

        QTemporaryDir directory;
        AppSettings settings(directory.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        StationServer server(&station, settings, directory.path());
        server.setTelemetryEnabled(true);
        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        qint64 now = 10000;
        RemoteAudioReceiverTelemetry playback;
        RemoteTelemetryController controller(&client, nullptr, nullptr,
            [&] { return now; }, [&] { return playback; });

        const auto connect = [&](const QString& name) {
            auto* guiWire = new ObservedLoopback;
            auto* coreWire = new Test::LoopbackTransport(name);
            guiWire->linkTo(coreWire);
            server.acceptTransport(coreWire);
            client.startSession(guiWire, server.token());
            QTRY_VERIFY(client.isHandshakeComplete());
        };
        connect(QStringLiteral("Core"));

        RemoteDiagnosticsDialog dialog(&controller);
        dialog.show();
        QTRY_VERIFY(dialog.isVisible());
        auto* tabs = dialog.findChild<QTabWidget*>(QStringLiteral("remoteDiagnosticsTabs"));
        QVERIFY(tabs);
        QCOMPARE(tabs->tabText(tabs->count() - 1), QStringLiteral("Core"));
        auto* label = dialog.findChild<QLabel*>(QStringLiteral("remoteCoreHostUnavailable"));
        auto* cpuGraph = dynamic_cast<TimeSeriesGraphWidget*>(dialog.findChild<QWidget*>(
            QStringLiteral("remoteCoreCpuGraph")));
        auto* memoryGraph = dynamic_cast<TimeSeriesGraphWidget*>(dialog.findChild<QWidget*>(
            QStringLiteral("remoteCoreMemoryGraph")));
        auto* temperatureGraph = dynamic_cast<TimeSeriesGraphWidget*>(dialog.findChild<QWidget*>(
            QStringLiteral("remoteCoreTemperatureGraph")));
        QVERIFY(label && cpuGraph && memoryGraph && temperatureGraph);
        QCOMPARE(label->text(), QStringLiteral("This Core does not report computer load."));
        const auto refresh = [&] {
            QVERIFY(QMetaObject::invokeMethod(&dialog, "refresh", Qt::DirectConnection));
        };
        const auto graphsShown = [&] {
            return !cpuGraph->isHidden() && !memoryGraph->isHidden()
                && !temperatureGraph->isHidden();
        };
        const auto graphsHidden = [&] {
            return cpuGraph->isHidden() && memoryGraph->isHidden()
                && temperatureGraph->isHidden();
        };

        // A Core that measures nothing (not Linux): one line, no graphs.
        StationTelemetrySnapshot sample;
        sample.sequence = 1;
        sample.sampledElapsedMs = 100;
        QVERIFY(server.sendTelemetry(sample, server.sessionEpoch()));
        QTRY_COMPARE(controller.current().state, RemoteTelemetryView::State::Current);
        refresh();
        QVERIFY(!label->isHidden());
        QVERIFY(graphsHidden());

        // A Linux Core: three graphs, no line.
        now += 1000;
        sample.sequence = 2;
        sample.sampledElapsedMs = 1100;
        sample.host.systemCpuPercent = 30.0;
        sample.host.processCpuPercent = 5.0;
        sample.host.memoryAvailableKiB = 800 * 1024;
        sample.host.memoryTotalKiB = 4 * 1024 * 1024;
        sample.host.processResidentKiB = 204800;
        sample.host.hottestZoneCelsius = 54.5;
        sample.host.hottestZoneName = QStringLiteral("soc-thermal");
        QVERIFY(server.sendTelemetry(sample, server.sessionEpoch()));
        QTRY_VERIFY(controller.current().coreHostReported);
        refresh();
        QVERIFY(label->isHidden());
        QVERIFY(graphsShown());
        QCOMPARE(namedSeries(cpuGraph, QStringLiteral("System"))->points.constLast().y(), 30.0);
        QCOMPARE(namedSeries(cpuGraph, QStringLiteral("NereusSDR Core"))->points.constLast().y(), 5.0);
        QCOMPARE(namedSeries(cpuGraph, QStringLiteral("System"))->unitSuffix, QStringLiteral("\u00A0%"));
        const auto* available = namedSeries(memoryGraph, QStringLiteral("Available"));
        const auto* resident = namedSeries(memoryGraph, QStringLiteral("Used by NereusSDR Core"));
        QVERIFY(available && resident);
        QCOMPARE(available->unitSuffix, QStringLiteral("\u00A0MiB"));
        QCOMPARE(resident->unitSuffix, available->unitSuffix);
        QCOMPARE(available->points.constLast().y(), 800.0);
        QCOMPARE(resident->points.constLast().y(), 200.0);
        const auto* hottest = namedSeries(temperatureGraph, QStringLiteral("Hottest sensor"));
        QVERIFY(hottest);
        QCOMPARE(hottest->unitSuffix, QStringLiteral("\u00A0°C"));
        QCOMPARE(hottest->points.constLast().y(), 54.5);
        QVERIFY(temperatureGraph->toolTip().contains(QStringLiteral("soc-thermal")));

        // Memory moves to GiB for both series once a value in range reaches 1 GiB.
        now += 1000;
        sample.sequence = 3;
        sample.sampledElapsedMs = 2100;
        sample.host.memoryAvailableKiB = 2 * 1024 * 1024;
        QVERIFY(server.sendTelemetry(sample, server.sessionEpoch()));
        QTRY_COMPARE(controller.current().coreHost.memoryAvailableKiB,
                     std::optional<qint64>(2 * 1024 * 1024));
        refresh();
        available = namedSeries(memoryGraph, QStringLiteral("Available"));
        resident = namedSeries(memoryGraph, QStringLiteral("Used by NereusSDR Core"));
        QCOMPARE(available->unitSuffix, QStringLiteral("\u00A0GiB"));
        QCOMPARE(resident->unitSuffix, available->unitSuffix);
        QCOMPARE(available->points.constLast().y(), 2.0);
        QCOMPARE(resident->points.constLast().y(), 200.0 / 1024.0);
        QVERIFY(!dialog.grab().isNull());

        // Between sessions the retained history still shows.
        client.disconnectFromStation(QStringLiteral("reconnect"));
        QTRY_COMPARE(controller.current().state, RemoteTelemetryView::State::Disconnected);
        refresh();
        QVERIFY(graphsShown());

        // A different Core reports a temperature without naming its sensor:
        // the tooltip must not carry the previous Core's sensor name.
        now += 1000;
        connect(QStringLiteral("unnamed-sensor Core"));
        StationTelemetrySnapshot unnamed = sample;
        unnamed.sequence = 1;
        unnamed.sampledElapsedMs = 0;
        unnamed.host.hottestZoneName.clear();
        QVERIFY(server.sendTelemetry(unnamed, server.sessionEpoch()));
        QTRY_COMPARE(controller.current().state, RemoteTelemetryView::State::Current);
        QTRY_VERIFY(controller.current().coreHostReported);
        refresh();
        QVERIFY(graphsShown());
        QVERIFY(controller.current().coreHost.hottestZoneName.isEmpty());
        QCOMPARE(temperatureGraph->toolTip(),
                 QStringLiteral("The hottest temperature sensor on the Core computer."));
        QVERIFY(!temperatureGraph->toolTip().contains(QStringLiteral("soc-thermal")));
        client.disconnectFromStation(QStringLiteral("reconnect"));
        QTRY_COMPARE(controller.current().state, RemoteTelemetryView::State::Disconnected);

        // The next Core reports no load: the line again, whatever history holds.
        now += 1000;
        connect(QStringLiteral("older Core"));
        StationTelemetrySnapshot bare;
        bare.sequence = 1;
        bare.sampledElapsedMs = 0;
        QVERIFY(server.sendTelemetry(bare, server.sessionEpoch()));
        QTRY_COMPARE(controller.current().state, RemoteTelemetryView::State::Current);
        refresh();
        QVERIFY(!label->isHidden());
        QVERIFY(graphsHidden());
        client.disconnectFromStation(QStringLiteral("done"));
    }
};

QTEST_MAIN(TstRemoteDiagnostics)
#include "tst_remote_diagnostics.moc"
