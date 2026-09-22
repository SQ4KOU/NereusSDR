// no-port-check: NereusSDR-original. Authenticated GUI subscription lifecycle.
#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <algorithm>
#include <cmath>
#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/ClarityController.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/settings/SettingsProxy.h"
#include "core/session/media/DisplayCodec.h"
#include "core/session/media/DaemonMediaController.h"
#include "core/session/PureSignalSessionFacade.h"
#include "core/session/Ps3DisplayCodec.h"
#include "core/FFTEngine.h"
#include "core/StepAttenuatorController.h"
#include "gui/RemoteMediaController.h"
#include "gui/PanadapterStack.h"
#include "gui/PanadapterApplet.h"
#include "gui/SpectrumWidget.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "fakes/LoopbackTransport.h"

using namespace NereusSDR;

class DisplayTransport final : public IMediaTransport {
public:
    explicit DisplayTransport(QObject* parent) : IMediaTransport(parent) {}
    bool start(const StartOptions&) override { return true; }
    void stop() override { active = false; }
    bool acceptDescription(const QString&, const QString&) override { return true; }
    bool acceptCandidate(const QString&, const QString&) override { return true; }
    bool sendDisplay(const QByteArray& packet) override {
        if (!active) { return false; }
        displayPackets.append(packet);
        if (other) { other->deliver(packet); }
        return true;
    }
    bool sendRtp(const QByteArray&) override { return active; }
    bool isReady() const override { return active; }
    void activate() { active = true; emit ready(); }
    void deliver(const QByteArray& packet) { emit displayReceived(packet); }
    void failConnection(const QString& reason) { emit connectionFailed(reason); }
    void closeUnexpectedly() { active = false; emit closed(); }
    void reportGenericError(const QString& reason) { emit errorOccurred(reason); }
    bool active = false;
    QPointer<DisplayTransport> other;
    QList<QByteArray> displayPackets;
};

namespace {
QJsonObject lastControl(const QSignalSpy& spy, const QString& op)
{
    for (auto it = spy.crbegin(); it != spy.crend(); ++it) {
        const QJsonObject control = it->at(0).toJsonObject();
        if (control.value(QStringLiteral("op")) == op) { return control; }
    }
    return {};
}
int countControl(const QSignalSpy& spy, const QString& op)
{
    int count = 0;
    for (const auto& call : spy) {
        if (call.at(0).toJsonObject().value(QStringLiteral("op")) == op) { ++count; }
    }
    return count;
}
} // namespace

class TestRemoteMediaController : public QObject {
    Q_OBJECT
private slots:
    void pureSignalChunksShareMediaWithoutChangingMessageLimit()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath("station.settings"));
        RadioModel station;
        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);
        QPointer<DisplayTransport> coreMedia;
        DaemonMediaController core(&server, &station, nullptr,
            [&coreMedia](QObject* owner) -> IMediaTransport* {
                coreMedia = new DisplayTransport(owner);
                return coreMedia;
            });
        RadioModel remote(RadioModel::Role::Remote);
        remote.audioEngine()->setMasterMuted(true);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        QPointer<DisplayTransport> guiMedia;
        RemoteMediaController gui(&client, &remote, nullptr, nullptr,
            [&guiMedia](QObject* owner) -> IMediaTransport* {
                guiMedia = new DisplayTransport(owner);
                return guiMedia;
            });
        auto* stationLink = new Test::LoopbackTransport("station");
        auto* clientLink = new Test::LoopbackTransport("client");
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        QTRY_VERIFY(client.isHandshakeComplete());
        QTRY_VERIFY(coreMedia && guiMedia);
        coreMedia->other = guiMedia;
        guiMedia->other = coreMedia;
        coreMedia->activate();
        guiMedia->activate();
        QCOMPARE(client.capabilities().psDisplayVersion, 1);
        PureSignalSessionFacade* coreFacade = station.pureSignalFacade();
        PureSignalSessionFacade* guiFacade = remote.pureSignalFacade();
        QTRY_COMPARE(guiFacade->displayGeneration(), coreFacade->displayGeneration());
        Ps3Snapshot frame;
        frame.channelId = 3;
        frame.sessionGeneration = coreFacade->displayGeneration();
        frame.sequence = 1;
        frame.sampleCount = Ps3Snapshot::kMaxSampleCount;
        frame.correctionCount = Ps3Snapshot::kMaxCorrectionCount;
        frame.x.assign(frame.sampleCount, 0.5);
        frame.ym.assign(frame.sampleCount, 0.45);
        frame.yc.assign(frame.sampleCount, 1.0);
        frame.ys.assign(frame.sampleCount, 0.0);
        frame.xmCorrection.assign(frame.correctionCount, 0.6);
        frame.ymCorrection.assign(frame.correctionCount, 0.65);
        frame.xaCorrection.assign(frame.correctionCount, 0.7);
        frame.yaCorrection.assign(frame.correctionCount, 2.0);
        // Only the DSP sample source is synthetic. Authenticated control,
        // subscription, chunk scheduling, peer bounds and GUI assembly are real.
        emit coreFacade->displaySnapshotReady(frame);
        QCoreApplication::processEvents();
        QVERIFY(coreMedia->displayPackets.isEmpty());
        guiFacade->setAmpViewSubscribed(true);
        QTRY_VERIFY(coreFacade->remoteAmpViewSubscribed());
        emit coreFacade->displaySnapshotReady(frame);
        QTRY_VERIFY(guiFacade->displaySnapshot().has_value());
        const Ps3Snapshot received = *guiFacade->displaySnapshot();
        QCOMPARE(received.sequence, 1u);
        QCOMPARE(received.sampleCount, 4096);
        QCOMPARE(received.xaCorrection.at(0), 0.7);
        QCOMPARE(received.xmCorrection.at(0), 0.6);
        const QList<QByteArray> expected = Ps3DisplayCodec::encode(frame);
        QCOMPARE(coreMedia->displayPackets, expected);
        for (const QByteArray& packet : coreMedia->displayPackets) {
            QVERIFY(packet.size() <= 64 * 1024);
        }
        guiFacade->setAmpViewSubscribed(false);
        QTRY_VERIFY(!coreFacade->remoteAmpViewSubscribed());
        ++frame.sequence;
        emit coreFacade->displaySnapshotReady(frame);
        QCoreApplication::processEvents();
        QCOMPARE(coreMedia->displayPackets.size(), expected.size());
        client.disconnectFromStation(QStringLiteral("test completed"));
        QVERIFY(!guiFacade->displaySnapshot());
    }

    void preReadyTypedFailureRequestsRecovery()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);
        RadioModel remote(RadioModel::Role::Remote);
        remote.audioEngine()->setMasterMuted(true);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        QPointer<DisplayTransport> media;
        RemoteMediaController controller(&client, &remote, nullptr, nullptr,
            [&media](QObject* owner) -> IMediaTransport* {
                media = new DisplayTransport(owner);
                return media;
            });
        QSignalSpy recoveries(&controller, &RemoteMediaController::recoveryRequested);
        auto* stationLink = new Test::LoopbackTransport(QStringLiteral("station"));
        auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        QTRY_VERIFY(client.isHandshakeComplete());
        QTRY_VERIFY(media);
        const quint32 epoch = client.sessionEpoch();

        // PeerFailed is transient connectivity even before ICE reaches ready.
        media->failConnection(QStringLiteral("media peer connection failed"));
        QTRY_COMPARE(recoveries.size(), 1);
        QCOMPARE(recoveries.constFirst().at(0).toUInt(), epoch);
    }

    void diagnosticConsumerMayDeleteControllerDuringRecovery()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);
        RadioModel remote(RadioModel::Role::Remote);
        remote.audioEngine()->setMasterMuted(true);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        QPointer<DisplayTransport> media;
        QPointer<RemoteMediaController> controller = new RemoteMediaController(
            &client, &remote, nullptr, nullptr,
            [&media](QObject* owner) -> IMediaTransport* {
                media = new DisplayTransport(owner);
                return media;
            });
        int recoveries = 0;
        connect(controller, &RemoteMediaController::recoveryRequested,
                this, [&recoveries](quint32, const QString&) { ++recoveries; });
        connect(controller, &RemoteMediaController::errorOccurred,
                this, [controller](const QString&) { delete controller.data(); });
        auto* stationLink = new Test::LoopbackTransport(QStringLiteral("station"));
        auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        QTRY_VERIFY(client.isHandshakeComplete());
        QTRY_VERIFY(media);
        media->activate();

        media->closeUnexpectedly();
        QVERIFY(controller.isNull());
        QCOMPARE(recoveries, 0);
    }

    void establishedMediaCloseRequestsOneEpochScopedRecovery()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);

        RadioModel remote(RadioModel::Role::Remote);
        remote.audioEngine()->setMasterMuted(true);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        QPointer<DisplayTransport> media;
        RemoteMediaController controller(&client, &remote, nullptr, nullptr,
            [&media](QObject* owner) -> IMediaTransport* {
                media = new DisplayTransport(owner);
                return media;
            });
        QSignalSpy recoveries(&controller, &RemoteMediaController::recoveryRequested);

        auto* stationLink = new Test::LoopbackTransport(QStringLiteral("station"));
        auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        QTRY_VERIFY(client.isHandshakeComplete());
        QTRY_VERIFY(media);
        media->activate();
        const quint32 epoch = client.sessionEpoch();

        media->closeUnexpectedly();
        QTRY_COMPARE(recoveries.size(), 1);
        QCOMPARE(recoveries.constFirst().at(0).toUInt(), epoch);
        QVERIFY(recoveries.constFirst().at(1).toString().contains(
            QStringLiteral("media"), Qt::CaseInsensitive));

        // A terminal failure may be followed by the backend's closed event.
        // The controller must request one full-session recovery, not two.
        if (media) {
            media->failConnection(QStringLiteral("media peer connection failed"));
            media->closeUnexpectedly();
        }
        QCoreApplication::processEvents();
        QCOMPARE(recoveries.size(), 1);
    }

    void deliberateSessionEndAndGenericMediaErrorDoNotRequestRecovery()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);
        RadioModel remote(RadioModel::Role::Remote);
        remote.audioEngine()->setMasterMuted(true);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        QPointer<DisplayTransport> media;
        RemoteMediaController controller(&client, &remote, nullptr, nullptr,
            [&media](QObject* owner) -> IMediaTransport* {
                media = new DisplayTransport(owner);
                return media;
            });
        QSignalSpy recoveries(&controller, &RemoteMediaController::recoveryRequested);
        auto connectSession = [&] {
            auto* stationLink = new Test::LoopbackTransport(QStringLiteral("station"));
            auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
            stationLink->linkTo(clientLink);
            client.startSession(clientLink, server.token());
            server.acceptTransport(stationLink);
            QTRY_VERIFY(client.isHandshakeComplete());
            QTRY_VERIFY(media);
            media->activate();
        };

        connectSession();
        media->reportGenericError(QStringLiteral("invalid media packet"));
        QCoreApplication::processEvents();
        QCOMPARE(recoveries.size(), 0);

        client.disconnectFromStation(QStringLiteral("reset after generic error"));
        connectSession();
        QVERIFY(media && media->active);
        // This disconnect retires a live current media peer. Its local stop
        // must not be reclassified as an unexpected transport close.
        client.disconnectFromStation(QStringLiteral("operator disconnect"));
        QCoreApplication::processEvents();
        QCOMPARE(recoveries.size(), 0);
    }

    void remoteCtunProjectionPreservesPreferenceWithoutEcho()
    {
        SpectrumWidget widget;
        widget.setCtunEnabled(true);
        QSignalSpy gestures(&widget, &SpectrumWidget::ctunEnabledChanged);
        QSignalSpy centres(&widget, &SpectrumWidget::centerChanged);
        widget.applyRemoteCtunState(false, false);
        QVERIFY(!widget.ctunAvailable());
        QVERIFY(!widget.ctunEnabled());
        QVERIFY(widget.ctunPreference());
        widget.setCtunEnabled(true); // An unsupported Core cannot pretend to pin.
        QVERIFY(!widget.ctunEnabled());
        widget.applyRemoteCtunState(true, true);
        QVERIFY(widget.ctunEnabled());
        QCOMPARE(gestures.size(), 0);
        QCOMPARE(centres.size(), 0);
    }

    void ctunWheelKeepsSourceAndDragMovesCoreCentre()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        station.setBoardForTest(HPSDRHW::Saturn);
        station.configureStreamPool(5, 5, 192000);
        station.setConnectionStateForTest(ConnectionState::Connected);
        const int sliceId = station.addSlice();
        auto* sourceSlice = station.sliceById(sliceId);
        QVERIFY(sourceSlice);
        const int stream = sourceSlice->streamIndex();
        QVERIFY(stream >= 0);
        const double centre = station.streamCentreHz(stream);
        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);
        QPointer<DisplayTransport> sourceMedia;
        DaemonMediaController daemon(&server, &station, nullptr,
            [&sourceMedia](QObject* owner) -> IMediaTransport* {
                sourceMedia = new DisplayTransport(owner);
                return sourceMedia;
            });
        RadioModel remote(RadioModel::Role::Remote);
        remote.audioEngine()->setMasterMuted(true);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        PanadapterStack stack;
        auto* first = stack.addPanadapter(QStringLiteral("first"));
        auto* cohost = stack.addPanadapter(QStringLiteral("cohost"));
        stack.setActivePan(QStringLiteral("first"));
        for (auto* applet : {first, cohost}) {
            applet->setActiveSliceIndex(sliceId);
            applet->spectrumWidget()->setDisplayWindowPreservingHistory(centre, 48000);
            applet->spectrumWidget()->setVfoFrequency(centre);
        }
        auto* widget = first->spectrumWidget();
        widget->setCtunEnabled(true);
        cohost->spectrumWidget()->setCtunEnabled(false);
        // This is the existing MainWindow click/wheel -> mirrored VFO path.
        connect(widget, &SpectrumWidget::frequencyClicked, &remote,
            [&remote, sliceId](double hz) { remote.sliceById(sliceId)->setFrequency(hz); });
        stack.resize(600, 700);
        stack.show();
        QVERIFY(QTest::qWaitForWindowExposed(&stack));
        QPointer<DisplayTransport> sinkMedia;
        RemoteMediaController gui(&client, &remote, &stack, nullptr,
            [&sinkMedia](QObject* owner) -> IMediaTransport* {
                sinkMedia = new DisplayTransport(owner);
                return sinkMedia;
            });
        QSignalSpy inbound(&client, &StationClient::mediaControlReceived);
        const auto connectSession = [&] {
            auto* stationLink = new Test::LoopbackTransport(QStringLiteral("station"));
            auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
            stationLink->linkTo(clientLink);
            client.startSession(clientLink, server.token());
            server.acceptTransport(stationLink);
        };
        connectSession();
        QTRY_VERIFY(sourceMedia && sinkMedia);
        sourceMedia->other = sinkMedia;
        sourceMedia->activate();
        sinkMedia->activate();
        QVector<float> iq(2048, 0.001f);
        const auto feed = [&] {
            QMetaObject::invokeMethod(&station, "rawIqDataForStream", Qt::DirectConnection,
                Q_ARG(int, stream), Q_ARG(QVector<float>, iq));
            return !widget->renderedPixels().isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(feed(), 5000);
        QTRY_VERIFY(sourceSlice->streamCtunPinned());
        QTRY_VERIFY(widget->ctunEnabled());
        QTRY_VERIFY(cohost->spectrumWidget()->ctunEnabled());
        const int contexts = countControl(inbound, QStringLiteral("context"));
        QSignalSpy centreChanges(&station, &RadioModel::streamCentreChanged);
        widget->frequencyClicked(centre + 100);
        QTRY_COMPARE(sourceSlice->frequency(), centre + 100);
        QCOMPARE(station.streamCentreHz(stream), centre);
        QCOMPARE(sourceSlice->shiftOffsetHz(), 100.0);
        QCOMPARE(centreChanges.size(), 0);
        QTest::qWait(150);
        QCOMPARE(countControl(inbound, QStringLiteral("context")), contexts);
        QVERIFY(feed());

        // Pan dragging applies the view then emits centerChanged. The plain
        // setter deliberately does not emit a user gesture.
        widget->setCenterFrequency(centre + 1000.25);
        widget->centerChanged(centre + 1000.25); // Fractional pixel -> whole-Hz DDC.
        QTRY_COMPARE(station.streamCentreHz(stream), centre + 1000);
        QCOMPARE(sourceSlice->frequency(), centre + 100);
        QCOMPARE(sourceSlice->shiftOffsetHz(), -900.0);
        QTRY_VERIFY_WITH_TIMEOUT(feed() && widget->ddcCenterFrequency() == centre + 1000, 5000);
        QVERIFY(countControl(inbound, QStringLiteral("context")) > contexts);
        QVERIFY(widget->ctunEnabled());

        // A cohost near the far edge makes this otherwise valid pan move
        // unsafe. Core refuses it; every affected view returns to Core truth.
        const int cohostId = station.addSlice();
        auto* cohostSlice = station.sliceById(cohostId);
        QVERIFY(cohostSlice);
        cohostSlice->setFrequency(centre + 80000);
        QCOMPARE(cohostSlice->streamIndex(), stream);
        QTRY_VERIFY(remote.sliceById(cohostId));
        // A selection followed immediately by a gesture precedes the next
        // subscription poll. Resolve the pan's current slice for both verbs.
        QSignalSpy pinResults(&client, &StationClient::streamCtunPinFinished);
        QSignalSpy centreResults(&client, &StationClient::streamCentreFinished);
        first->setActiveSliceIndex(cohostId);
        widget->ctunEnabledChanged(true);
        widget->centerChanged(centre + 1000);
        QTRY_VERIFY(!pinResults.isEmpty());
        QTRY_VERIFY(!centreResults.isEmpty());
        QCOMPARE(pinResults.first().at(0).toInt(), cohostId);
        QCOMPARE(centreResults.first().at(0).toInt(), cohostId);
        first->setActiveSliceIndex(sliceId);
        const double rejectedCentre = centre - 50000;
        widget->setCenterFrequency(rejectedCentre);
        widget->centerChanged(rejectedCentre);
        QTRY_VERIFY_WITH_TIMEOUT(feed()
            && std::abs(widget->centerFrequency() - (centre + 1000)) < 50, 5000);
        QCOMPARE(widget->ddcCenterFrequency(), centre + 1000);
        QCOMPARE(station.streamCentreHz(stream), centre + 1000);
        QCOMPARE(sourceSlice->frequency(), centre + 100);
        QCOMPARE(cohostSlice->frequency(), centre + 80000);
        station.removeSlice(cohostId);
        QTRY_VERIFY(!remote.sliceById(cohostId));

        widget->setCtunEnabled(false);
        QTRY_VERIFY(!sourceSlice->streamCtunPinned());
        QTRY_VERIFY(!cohost->spectrumWidget()->ctunEnabled());
        widget->frequencyClicked(centre + 2000);
        QTRY_COMPARE(station.streamCentreHz(stream), centre + 2000);
        QCOMPARE(sourceSlice->shiftOffsetHz(), 0.0);
        widget->setCtunEnabled(true);
        QTRY_VERIFY(sourceSlice->streamCtunPinned());
        client.disconnectFromStation(QStringLiteral("C-Tune reconnect test"));
        QTRY_VERIFY(!sourceSlice->streamCtunPinned());
        QVERIFY(!widget->ctunAvailable());
        QVERIFY(widget->ctunPreference());
        connectSession();
        QTRY_VERIFY(sourceMedia && sinkMedia);
        sourceMedia->other = sinkMedia;
        sourceMedia->activate();
        sinkMedia->activate();
        QTRY_VERIFY_WITH_TIMEOUT(feed(), 5000);
        QTRY_VERIFY(sourceSlice->streamCtunPinned());
        QTRY_VERIFY(widget->ctunEnabled());

        // Retire and reuse stream 0 without an intervening empty GUI poll.
        // A stream index is reusable; its Core lifetime identity is not.
        const quint64 previousEpoch = sourceSlice->streamEpoch();
        const int spareId = station.addSlice();
        station.sliceById(spareId)->setFrequency(7100000);
        station.removeSlice(sliceId);
        const int replacementId = station.addSlice();
        auto* replacement = station.sliceById(replacementId);
        replacement->setFrequency(centre + 2000);
        QCOMPARE(replacement->streamIndex(), stream);
        QVERIFY(replacement->streamEpoch() != previousEpoch);
        QVERIFY(!replacement->streamCtunPinned());
        first->setActiveSliceIndex(replacementId);
        cohost->setActiveSliceIndex(replacementId);
        // Keep producing I/Q across retirement: an old painted frame may
        // still be visible before the new source/context is established.
        QTRY_VERIFY_WITH_TIMEOUT(feed() && replacement->streamCtunPinned(), 5000);
        QTRY_VERIFY(widget->ctunEnabled());
        client.disconnectFromStation(QStringLiteral("test complete"));
    }

    void sharedWindowChangeAndRadioReconnectResumeBothPanes()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        auto& appSettings = AppSettings::instance();
        const QVariant savedWindow = appSettings.value(QStringLiteral("DisplayFftWindow"));
        const QVariant savedFft = appSettings.value(QStringLiteral("DisplayFftSize"));
        appSettings.setValue(QStringLiteral("DisplayFftWindow"), QString::number(int(WindowFunction::Hann)));
        appSettings.setValue(QStringLiteral("DisplayFftSize"), QStringLiteral("4096"));
        const auto restore = qScopeGuard([&] {
            appSettings.setValue(QStringLiteral("DisplayFftWindow"), savedWindow);
            appSettings.setValue(QStringLiteral("DisplayFftSize"), savedFft);
        });
        RadioModel station;
        station.setBoardForTest(HPSDRHW::Saturn);
        StepAttenuatorController stationAttenuator(&station);
        stationAttenuator.setStepAttEnabled(true);
        stationAttenuator.setAttenuation(0);
        station.setStepAttController(&stationAttenuator);
        const auto detachStationAttenuator = qScopeGuard([&] {
            station.setStepAttController(nullptr);
        });
        station.configureStreamPool(5, 5, 192000);
        station.setConnectionStateForTest(ConnectionState::Connected);
        const int sliceId = station.addSlice();
        auto* slice = station.sliceById(sliceId);
        QVERIFY(slice);
        const int stream = slice->streamIndex();
        QVERIFY(stream >= 0);
        const double centre = station.streamCentreHz(stream);
        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);
        QPointer<DisplayTransport> sourceMedia;
        DaemonMediaController daemon(&server, &station, nullptr,
            [&sourceMedia](QObject* owner) -> IMediaTransport* {
                sourceMedia = new DisplayTransport(owner);
                return sourceMedia;
            });
        RadioModel remote(RadioModel::Role::Remote);
        remote.audioEngine()->setMasterMuted(true); // Display fixture opens no speaker.
        ClarityController clarity;
        remote.setClarityController(&clarity);
        const auto detachClarity = qScopeGuard([&] { remote.setClarityController(nullptr); });
        clarity.setEnabled(true);
        QSignalSpy liveFloors(&clarity, &ClarityController::noiseFloorChanged);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        PanadapterStack stack;
        auto* first = stack.addPanadapter(QStringLiteral("first"));
        auto* second = stack.addPanadapter(QStringLiteral("second"));
        stack.setActivePan(QStringLiteral("first"));
        for (auto* applet : {first, second}) {
            applet->setActiveSliceIndex(sliceId);
            applet->spectrumWidget()->setDisplayWindowPreservingHistory(centre, 48000);
        }
        stack.resize(600, 700);
        stack.show();
        QVERIFY(QTest::qWaitForWindowExposed(&stack));
        QPointer<DisplayTransport> sinkMedia;
        RemoteMediaController gui(&client, &remote, &stack, nullptr,
            [&sinkMedia](QObject* owner) -> IMediaTransport* {
                sinkMedia = new DisplayTransport(owner);
                return sinkMedia;
            });
        QSignalSpy outbound(&server, &StationServer::mediaControlReceived);
        QSignalSpy inbound(&client, &StationClient::mediaControlReceived);
        auto* stationLink = new Test::LoopbackTransport(QStringLiteral("station"));
        auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        QTRY_VERIFY(sourceMedia && sinkMedia);
        sourceMedia->other = sinkMedia;
        sourceMedia->activate();
        sinkMedia->activate();
        QTRY_COMPARE(daemon.activeEndpointCount(), 2);
        // Feed real tagged ingress in bounded radio-sized chunks. Repeated
        // calls also wait for the asynchronous source configuration to finish.
        QVector<float> iq(2048);
        for (int i = 0; i < iq.size(); i += 2) {
            // Keep the station-calibration matrix well below the codec's
            // 0 dBm ceiling; otherwise a positive offset can hide a shift.
            iq[i] = 0.01f * std::cos(double(i) * 0.17);
            iq[i + 1] = 0.01f * std::sin(double(i) * 0.17);
        }
        const auto bothHaveFrames = [&] {
            QMetaObject::invokeMethod(&station, "rawIqDataForStream", Qt::DirectConnection,
                Q_ARG(int, stream), Q_ARG(QVector<float>, iq));
            return !first->spectrumWidget()->renderedPixels().isEmpty()
                && !second->spectrumWidget()->renderedPixels().isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(bothHaveFrames(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!liveFloors.isEmpty(), 5000);
        QCOMPARE(countControl(inbound, QStringLiteral("rejected")), 0);

        // DaemonMediaController applies the station offset before reducing
        // and encoding. Exercise all authoritative receiver paths using the
        // same tagged I/Q, rather than relying on the remote client's local
        // DisplayCalOffset preference.
        const auto traceLevels = [&] {
            QVector<float> bins = first->spectrumWidget()->renderedPixels();
            std::sort(bins.begin(), bins.end());
            return qMakePair(bins.last(), bins.at(bins.size() / 2));
        };
        const auto feedAndAwaitOffset = [&](double expectedOffsetDb,
                                            const QPair<float, float>& referenceDbm) {
            return QTest::qWaitFor([&] {
                QMetaObject::invokeMethod(&station, "rawIqDataForStream", Qt::DirectConnection,
                    Q_ARG(int, stream), Q_ARG(QVector<float>, iq));
                const float offset = static_cast<float>(expectedOffsetDb);
                const auto levels = traceLevels();
                return std::abs(levels.first - (referenceDbm.first + offset)) < 1.0f
                    && std::abs(levels.second - (referenceDbm.second + offset)) < 1.0f;
            }, 5000);
        };
        const auto baseTraceDbm = traceLevels();
        const double baseOffsetDb = station.rxMeterOffsetDb();
        stationAttenuator.setAttenuation(10);
        QVERIFY(feedAndAwaitOffset(station.rxMeterOffsetDb() - baseOffsetDb, baseTraceDbm));
        const auto stepAttTraceDbm = traceLevels();
        stationAttenuator.setStepAttEnabled(false);
        stationAttenuator.setPreampMode(PreampMode::Off);
        QVERIFY(feedAndAwaitOffset(station.rxMeterOffsetDb() - baseOffsetDb, baseTraceDbm));
        const auto preampOffTraceDbm = traceLevels();
        stationAttenuator.setPreampMode(PreampMode::On);
        QVERIFY(feedAndAwaitOffset(station.rxMeterOffsetDb() - baseOffsetDb, baseTraceDbm));
        const auto preampOnTraceDbm = traceLevels();
        QVERIFY(std::abs(stepAttTraceDbm.first - baseTraceDbm.first) > 1.0f);
        QVERIFY(std::abs(preampOffTraceDbm.first - stepAttTraceDbm.first) > 1.0f);
        QVERIFY(std::abs(preampOnTraceDbm.first - preampOffTraceDbm.first) > 1.0f);
        outbound.clear();
        inbound.clear();
        appSettings.setValue(QStringLiteral("DisplayFftWindow"), QString::number(int(WindowFunction::BlackmanHarris4)));
        QTRY_COMPARE(countControl(outbound, QStringLiteral("subscribe")), 2);
        QCOMPARE(countControl(outbound, QStringLiteral("unsubscribe")), 2);
        // Both old users of the source must retire before either replacement.
        QStringList operations;
        for (const auto& call : outbound) {
            const QString op = call.at(0).toJsonObject().value(QStringLiteral("op")).toString();
            if (op == QStringLiteral("subscribe") || op == QStringLiteral("unsubscribe")) {
                operations.append(op);
            }
        }
        QCOMPARE(operations, QStringList({QStringLiteral("unsubscribe"), QStringLiteral("unsubscribe"),
                                         QStringLiteral("subscribe"), QStringLiteral("subscribe")}));
        QTRY_VERIFY_WITH_TIMEOUT(bothHaveFrames(), 5000);
        QTRY_COMPARE(countControl(inbound, QStringLiteral("context")), 2);
        QCOMPARE(countControl(inbound, QStringLiteral("rejected")), 0);

        station.setConnectionStateForTest(ConnectionState::LinkLost);
        QTRY_COMPARE(gui.activeEndpointCount(), 0);
        QTRY_COMPARE(daemon.activeEndpointCount(), 0);
        QVERIFY(first->spectrumWidget()->renderedPixels().isEmpty());
        QVERIFY(second->spectrumWidget()->renderedPixels().isEmpty());
        station.setConnectionStateForTest(ConnectionState::Disconnected);
        QCOMPARE(daemon.activeEndpointCount(), 0);
        station.setConnectionStateForTest(ConnectionState::Connected);
        QTRY_COMPARE(daemon.activeEndpointCount(), 2);
        QTRY_VERIFY_WITH_TIMEOUT(bothHaveFrames(), 5000);
        client.disconnectFromStation(QStringLiteral("test complete"));
    }

    void contextMediaAndRetirementStayInAuthenticatedSession()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        station.setBoardForTest(HPSDRHW::HermesLite);
        station.setConnectionStateForTest(ConnectionState::Connected);
        station.addSlice(QStringLiteral("pan-0"));
        QVERIFY(!station.slices().isEmpty());
        SliceModel* stationSlice = station.slices().first();
        stationSlice->setStreamIndex(0);
        stationSlice->setFrequency(14225000);
        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);
        RadioModel remote(RadioModel::Role::Remote);
        remote.audioEngine()->setMasterMuted(true); // Display fixture opens no speaker.
        ClarityController clarity;
        remote.setClarityController(&clarity);
        const auto detachClarity = qScopeGuard([&] { remote.setClarityController(nullptr); });
        clarity.setEnabled(true);
        clarity.setPollIntervalMs(0);
        clarity.setSmoothingTauSec(0);
        clarity.setDeadbandDb(0);
        QSignalSpy floors(&clarity, &ClarityController::noiseFloorChanged);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        PanadapterStack stack;
        auto* applet = stack.addPanadapter(QStringLiteral("pan-0"));
        applet->setActiveSliceIndex(stationSlice->sliceIndex());
        auto* widget = applet->spectrumWidget();
        connect(&clarity, &ClarityController::waterfallThresholdsChanged,
                widget, [widget](float low, float high) {
            widget->setClarityActive(true);
            widget->setClarityWaterfallThresholds(low, high);
        });
        const float savedLow = widget->wfLowThreshold();
        const float savedHigh = widget->wfHighThreshold();
        widget->setDisplayWindowPreservingHistory(14225000, 24000);
        widget->setSpectrumRenderMode(int(SpectrumRenderMode::Mode3D));
        widget->setWfUpdatePeriodMs(20);
        stack.resize(600, 400);
        stack.show();
        QVERIFY(QTest::qWaitForWindowExposed(&stack));
        QPointer<DisplayTransport> media;
        RemoteMediaController controller(&client, &remote, &stack, nullptr,
            [&media](QObject* owner) -> IMediaTransport* {
                media = new DisplayTransport(owner);
                return media;
            });
        QSignalSpy controls(&server, &StationServer::mediaControlReceived);
        QSignalSpy receivedControls(&client, &StationClient::mediaControlReceived);
        QSignalSpy frames(&controller, &RemoteMediaController::displayFrameReceived);
        QVERIFY(!media); // No pre-authentication peer or subscription.
        auto* stationLink = new Test::LoopbackTransport(QStringLiteral("station"));
        auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        QTRY_VERIFY(client.mediaAvailable());
        QTRY_VERIFY(media);
        QTRY_COMPARE(countControl(controls, QStringLiteral("start")), 1);
        QCOMPARE(countControl(controls, QStringLiteral("subscribe")), 0);
        media->activate();
        QTRY_COMPARE(countControl(controls, QStringLiteral("subscribe")), 1);
        const QJsonObject subscription = lastControl(controls, QStringLiteral("subscribe"));
        const quint32 id = quint32(subscription.value(QStringLiteral("endpointId")).toDouble());
        QJsonObject noiseFloor{
            {QStringLiteral("op"), QStringLiteral("noise-floor")},
            {QStringLiteral("connectionId"), subscription.value(QStringLiteral("connectionId"))},
            {QStringLiteral("endpointId"), double(id)},
            {QStringLiteral("revision"), subscription.value(QStringLiteral("revision"))},
            {QStringLiteral("contextGeneration"), 1},
            {QStringLiteral("floorDbm"), -132.375}};
        const auto deliverFloor = [&](const QJsonObject& message) {
            const int before = countControl(receivedControls, QStringLiteral("noise-floor"));
            QVERIFY(server.sendMediaControl(message, server.mediaSessionEpoch()));
            QTRY_COMPARE(countControl(receivedControls, QStringLiteral("noise-floor")), before + 1);
        };
        deliverFloor(noiseFloor); // No accepted display context yet.
        QCOMPARE(floors.size(), 0);
        DisplayCodecFrame frame;
        frame.context = {id, 1, -180, 0, 128, 128, 0};
        frame.traceDbm = QVector<float>(128, -75);
        frame.waterfallDbm = QVector<float>(128, -125);
        frame.waterfallAdvance = true;
        DisplayCodecEncoder encoder;
        const QByteArray packet = encoder.encode(frame);
        QVERIFY(!packet.isEmpty());
        media->deliver(packet); // Media may race its reliable context.
        QCOMPARE(frames.count(), 0);
        QJsonObject context{
            {QStringLiteral("op"), QStringLiteral("context")},
            {QStringLiteral("connectionId"), subscription.value(QStringLiteral("connectionId"))},
            {QStringLiteral("endpointId"), double(id)},
            {QStringLiteral("revision"), subscription.value(QStringLiteral("revision"))},
            {QStringLiteral("contextGeneration"), 1}, {QStringLiteral("sourceStream"), 0},
            {QStringLiteral("sourceCentreHz"), 14225000}, {QStringLiteral("sampleRateHz"), 192000},
            {QStringLiteral("centreHz"), 14225023.4375}, {QStringLiteral("spanHz"), 24046.875},
            {QStringLiteral("wideCentreHz"), 0}, {QStringLiteral("wideSpanHz"), 0},
            {QStringLiteral("traceSamples"), 128}, {QStringLiteral("waterfallSamples"), 128},
            {QStringLiteral("wideSamples"), 0}, {QStringLiteral("minDbm"), -180},
            {QStringLiteral("maxDbm"), 0}, {QStringLiteral("fps"), 30},
            {QStringLiteral("framesPerLine"), 1}};
        QJsonObject invalid = context;
        invalid.insert(QStringLiteral("traceSamples"), 128.5);
        QVERIFY(server.sendMediaControl(invalid, server.mediaSessionEpoch()));
        QTest::qWait(30);
        media->deliver(packet);
        QCOMPARE(frames.count(), 0);
        QVERIFY(server.sendMediaControl(context, server.mediaSessionEpoch()));
        QTRY_COMPARE(countControl(controls, QStringLiteral("keyframe")), 1);
        deliverFloor(noiseFloor);
        QCOMPARE(floors.size(), 1);
        QCOMPARE(clarity.smoothedFloor(), -132.375f);
        QCOMPARE(widget->wfActiveLowThreshold(), -137.375f);
        QCOMPARE(widget->wfActiveHighThreshold(), -77.375f);
        QCOMPARE(widget->wfLowThreshold(), savedLow);
        QCOMPARE(widget->wfHighThreshold(), savedHigh);
        const QList<QPair<QString, QJsonValue>> invalidFields{
            {QStringLiteral("connectionId"), QStringLiteral("00000000-0000-4000-8000-000000000001")},
            {QStringLiteral("endpointId"), double(id + 1000)},
            {QStringLiteral("revision"), subscription.value(QStringLiteral("revision")).toDouble() + 1},
            {QStringLiteral("contextGeneration"), 2},
            {QStringLiteral("contextGeneration"), 1.5},
            {QStringLiteral("floorDbm"), QStringLiteral("-120")},
            {QStringLiteral("floorDbm"), -401},
            {QStringLiteral("floorDbm"), 101},
            {QStringLiteral("floorDbm"), QJsonValue(QJsonValue::Null)},
            {QStringLiteral("extra"), 1}};
        for (const auto& [key, value] : invalidFields) {
            QJsonObject badFloor = noiseFloor;
            badFloor.insert(key, value);
            deliverFloor(badFloor);
            QCOMPARE(floors.size(), 1);
        }
        client.mediaControlReceived(noiseFloor, client.sessionEpoch() - 1);
        QCOMPARE(floors.size(), 1);
        media->deliver(packet);
        QCOMPARE(frames.count(), 1);
        QCOMPARE(widget->renderedPixels().size(), 128);
        QVERIFY(std::abs(widget->renderedPixels().first() + 75) < 0.4);
        QVERIFY(std::abs(widget->wfRenderedPixels().first() + 125) < 0.4);
        QTRY_COMPARE(widget->dssRowsPushedForTest(), 1);
        QTest::qWait(250);
        // Bin-aligned accepted geometry must not create a resubscribe loop.
        QCOMPARE(countControl(controls, QStringLiteral("subscribe")), 1);

        // A regular tune must keep painted history even during the request/ACK
        // gap. Old media is retired immediately; only the new generation paints.
        widget->setCenterFrequency(widget->centerFrequency() + 500);
        QTRY_COMPARE(countControl(controls, QStringLiteral("subscribe")), 2);
        QCOMPARE(widget->dssRowsPushedForTest(), 1);
        QVERIFY(widget->renderedPixels().isEmpty());
        media->deliver(packet);
        QCOMPARE(frames.count(), 1);
        const auto tuned = lastControl(controls, QStringLiteral("subscribe"));
        context.insert(QStringLiteral("revision"), tuned.value(QStringLiteral("revision")));
        context.insert(QStringLiteral("contextGeneration"), 2);
        context.insert(QStringLiteral("centreHz"), widget->centerFrequency());
        context.insert(QStringLiteral("spanHz"), widget->bandwidth());
        const int contextsBefore = countControl(receivedControls, QStringLiteral("context"));
        QVERIFY(server.sendMediaControl(context, server.mediaSessionEpoch()));
        QTRY_COMPARE(countControl(receivedControls, QStringLiteral("context")), contextsBefore + 1);
        QCOMPARE(widget->dssRowsPushedForTest(), 1);
        media->deliver(packet);
        QCOMPARE(frames.count(), 1);
        frame.context.contextGeneration = 2;
        media->deliver(encoder.encode(frame));
        QCOMPARE(frames.count(), 2);
        QTRY_COMPARE(widget->dssRowsPushedForTest(), 2);
        noiseFloor.insert(QStringLiteral("revision"), tuned.value(QStringLiteral("revision")));
        noiseFloor.insert(QStringLiteral("contextGeneration"), 2);
        noiseFloor.insert(QStringLiteral("floorDbm"), -131);
        deliverFloor(noiseFloor);
        QCOMPARE(floors.size(), 2);

        widget->setDisplayWindowPreservingHistory(14425000, 24000);
        deliverFloor(noiseFloor); // A gesture retires old RF meaning before its ACK.
        QCOMPARE(floors.size(), 2);
        applet->hide();
        QTRY_COMPARE(countControl(controls, QStringLiteral("unsubscribe")), 1);
        QCOMPARE(controller.activeEndpointCount(), 0);
        deliverFloor(noiseFloor);
        QCOMPARE(floors.size(), 2);
        media->deliver(packet);
        QCOMPARE(frames.count(), 2);
        client.disconnectFromStation(QStringLiteral("test complete"));
        QVERIFY(!media || !media->active);
        QVERIFY(widget->renderedPixels().isEmpty());
        QCOMPARE(widget->dssRowsPushedForTest(), 0);
    }
};
QTEST_MAIN(TestRemoteMediaController)
#include "tst_remote_media_controller.moc"
