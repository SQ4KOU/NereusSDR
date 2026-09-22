// no-port-check: NereusSDR-original. Remote GUI connection and hydration boundaries.
#include <QTest>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QWebSocketServer>
#include <QSslSocket>
#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/session/IStationLink.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/settings/SettingsProxy.h"
#include "gui/MainWindow.h"
#include "gui/RemoteConnectionController.h"
#include "gui/RemoteMediaController.h"
#include "gui/TitleBar.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "fakes/LoopbackTransport.h"

using namespace NereusSDR;

class RecoveryMediaTransport final : public IMediaTransport {
public:
    explicit RecoveryMediaTransport(QObject* parent) : IMediaTransport(parent) {}
    bool start(const StartOptions&) override { return true; }
    void stop() override { m_ready = false; }
    bool acceptDescription(const QString&, const QString&) override { return true; }
    bool acceptCandidate(const QString&, const QString&) override { return true; }
    bool sendDisplay(const QByteArray&) override { return m_ready; }
    bool sendRtp(const QByteArray&) override { return m_ready; }
    bool isReady() const override { return m_ready; }
    void activate() { m_ready = true; emit ready(); }
    void closeUnexpectedly() { m_ready = false; emit closed(); }
private:
    bool m_ready = false;
};

class RecordingStationLink final : public IStationLink {
public:
    QStringList additions;
    CommandOutcome requestAddSlice(const QString& pan) override
    { additions << pan; return {true, {}}; }
    CommandOutcome requestAddSliceOnPan(const QString& pan) override
    { additions << pan; return {true, {}}; }
    CommandOutcome requestRemoveSlice(int) override { return {}; }
    CommandOutcome requestActiveSlice(int) override { return {}; }
    CommandOutcome requestSliceSampleRate(int, int) override { return {}; }
};

class TestRemoteConnectionControls : public QObject {
    Q_OBJECT
private slots:
    void remoteTitleIsClickableInEverySessionState()
    {
        ConnectionSegment segment;
        QSignalSpy clicked(&segment, &ConnectionSegment::rttClicked);
        for (const auto state : {ConnectionState::Disconnected, ConnectionState::Connecting,
                                 ConnectionState::LinkLost, ConnectionState::Connected}) {
            segment.setState(state);
            segment.setRemoteStatusText(QStringLiteral("Core status"));
            QTest::mouseClick(&segment, Qt::LeftButton, Qt::NoModifier, QPoint(2, 2));
        }
        QCOMPARE(clicked.size(), 4);
        QCOMPARE(segment.accessibleName(), QStringLiteral("Core status"));
    }

    void connectionButtonsControlCoreWhileRadioIsOffline()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        StationServer server(&station, settings, dir.path());
        QWebSocketServer listener(QStringLiteral("control test"), QWebSocketServer::NonSecureMode);
        QVERIFY(listener.listen(QHostAddress::LocalHost, 0));
        connect(&listener, &QWebSocketServer::newConnection, &server, [&] {
            server.acceptTransport(new WebSocketTransport(listener.nextPendingConnection(),
                                                           StationServer::kMaxIncomingMessageBytes));
        });
        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        const RemoteStationOptions options{
            QStringLiteral("ws://127.0.0.1:%1").arg(listener.serverPort()),
            server.token(), {}, true};
        RemoteConnectionController controls(&client, &remote, options);
        RemoteConnectionPanel panel(&controls);
        auto* dial = panel.findChild<QPushButton*>(QStringLiteral("connectCore"));
        auto* stop = panel.findChild<QPushButton*>(QStringLiteral("disconnectCore"));
        auto* details = panel.findChild<QLabel*>(QStringLiteral("coreConnectionDetails"));
        QVERIFY(dial && stop && details);
        QVERIFY(dial->isEnabled());
        QVERIFY(!stop->isEnabled());
        dial->click();
        QTRY_VERIFY(client.isHandshakeComplete());
        QVERIFY(!remote.isConnected());
        QCOMPARE(controls.state(), ConnectionState::Connected);
        QCOMPARE(controls.radioText(), QStringLiteral("Radio offline"));
        QVERIFY(details->text().contains(controls.endpointText()));
        QVERIFY(!dial->isEnabled());
        QVERIFY(stop->isEnabled());
        const QString refused = QStringLiteral("Receiver 2 (pan-1): no DDC is available. The saved layout is retained.");
        QVERIFY(remote.applyStationReceiveLayoutStatus("receiveLayoutRestoreState", "degraded"));
        QVERIFY(remote.applyStationReceiveLayoutStatus("receiveLayoutRestoreMessage", refused));
        QVERIFY(details->text().contains(refused));
        const auto epoch = client.sessionEpoch();
        controls.connectToStation(); // Duplicate surface must not replace the live client.
        QCOMPARE(client.sessionEpoch(), epoch);
        stop->click();
        QVERIFY(!client.isConnectionActive());
        QVERIFY(dial->isEnabled());
        QVERIFY(!stop->isEnabled());
        QCOMPARE(controls.statusText(), QStringLiteral("Core disconnected"));
        QVERIFY(!details->text().contains(refused)); // retained snapshot is stale while disconnected
        dial->click();
        QTRY_VERIFY(client.isHandshakeComplete());
        QVERIFY(client.sessionEpoch() > epoch);
        stop->click();
    }

    void failedConnectShowsReasonAndCancelStopsBackoff()
    {
        QTcpServer port;
        QVERIFY(port.listen(QHostAddress::LocalHost, 0));
        const quint16 unusedPort = port.serverPort();
        port.close();
        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        client.setReconnectBackoffUnitMs(100);
        RemoteConnectionController controls(&client, &remote,
            {QStringLiteral("ws://127.0.0.1:%1").arg(unusedPort), {}, {}, true});
        QSignalSpy retries(&client, &StationClient::reconnectScheduled);
        controls.connectToStation();
        QTRY_VERIFY(client.isReconnectPending());
        QVERIFY(!client.lastError().isEmpty());
        QVERIFY(controls.detailText().contains(client.lastError()));
        QVERIFY(controls.canDisconnect());
        controls.disconnectFromStation();
        const auto epoch = client.sessionEpoch();
        const int scheduled = retries.size();
        // Observe past the cancelled timer's deadline: no stale retry may dial.
        QTest::qWait(150);
        QCOMPARE(client.sessionEpoch(), epoch);
        QCOMPARE(retries.size(), scheduled);
        QVERIFY(!client.isConnectionActive());
        QVERIFY(controls.canConnect());
        QVERIFY(!controls.detailText().contains(QStringLiteral("Last failure:")));
    }

    void mediaRecoveryUsesPinnedCoreReconnectAndRetainsSlice()
    {
        if (!QSslSocket::supportsSsl()) {
            QSKIP("Qt SSL support is unavailable");
        }
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        station.setBoardForTest(HPSDRHW::Saturn);
        station.configureStreamPool(5, 5, 192000);
        station.setConnectionStateForTest(ConnectionState::Connected);
        const int sliceId = station.addSlice(QStringLiteral("pan-0"));
        QVERIFY(sliceId >= 0);
        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);
        QVERIFY2(server.listen(QHostAddress::LocalHost, 0), qPrintable(server.lastError()));

        RadioModel remote(RadioModel::Role::Remote);
        remote.audioEngine()->setMasterMuted(true);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        client.setReconnectBackoffUnitMs(50);
        RemoteConnectionController controls(&client, &remote,
            {QStringLiteral("wss://127.0.0.1:%1").arg(server.serverPort()),
             server.token(), server.certificateFingerprint(), false});
        QPointer<RecoveryMediaTransport> transport;
        RemoteMediaController media(&client, &remote, nullptr, nullptr,
            [&transport](QObject* owner) -> IMediaTransport* {
                transport = new RecoveryMediaTransport(owner);
                return transport;
            });
        connect(&media, &RemoteMediaController::recoveryRequested,
                &controls, &RemoteConnectionController::recoverMediaSession,
                Qt::QueuedConnection);
        QSignalSpy handshakes(&client, &StationClient::handshakeComplete);
        QSignalSpy retries(&client, &StationClient::reconnectScheduled);

        controls.connectToStation();
        QTRY_COMPARE_WITH_TIMEOUT(handshakes.size(), 1, 15000);
        QTRY_VERIFY(transport);
        transport->activate();
        SliceModel* const retained = remote.sliceById(sliceId);
        QVERIFY(retained);
        retained->setFrequency(retained->frequency() + 731.0);
        QTRY_COMPARE(station.sliceById(sliceId)->frequency(), retained->frequency());
        const double retainedFrequency = retained->frequency();
        const quint32 failedEpoch = client.sessionEpoch();

        // Drive the production chain: transport close -> MediaPeer close ->
        // RemoteMediaController recovery -> queued connection controller.
        transport->closeUnexpectedly();
        QTRY_VERIFY_WITH_TIMEOUT(!retries.isEmpty(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(handshakes.size(), 2, 15000);
        QVERIFY(client.sessionEpoch() > failedEpoch);
        QCOMPARE(remote.sliceById(sliceId), retained);
        QCOMPARE(retained->frequency(), retainedFrequency);
        QCOMPARE(station.slices().size(), 1);
        QCOMPARE(remote.slices().size(), 1);

        // A stale close from the retired media epoch must not disturb the
        // newly authenticated session.
        const quint32 secondEpoch = client.sessionEpoch();
        const int retriesAfterRecovery = retries.size();
        emit media.recoveryRequested(failedEpoch,
                                     QStringLiteral("stale media connection closed"));
        QTest::qWait(100);
        QCOMPARE(client.sessionEpoch(), secondEpoch);
        QCOMPARE(retries.size(), retriesAfterRecovery);
        QVERIFY(client.isHandshakeComplete());

        // Peer failure and its trailing close can enqueue the same recovery
        // twice. Only one station teardown/reconnect is admitted per epoch.
        emit media.recoveryRequested(secondEpoch, QStringLiteral("media peer failed"));
        emit media.recoveryRequested(secondEpoch, QStringLiteral("media peer closed"));
        QTRY_COMPARE_WITH_TIMEOUT(retries.size(), retriesAfterRecovery + 1, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(handshakes.size(), 3, 15000);
        QCOMPARE(remote.sliceById(sliceId), retained);
        QCOMPARE(retained->frequency(), retainedFrequency);
        QCOMPARE(station.slices().size(), 1);
        QCOMPARE(remote.slices().size(), 1);

        // The operator can cancel after recovery is queued but before its
        // deferred teardown runs. The operator's intent wins.
        const quint32 thirdEpoch = client.sessionEpoch();
        emit media.recoveryRequested(thirdEpoch,
                                     QStringLiteral("media peer connection failed"));
        controls.disconnectFromStation();
        const int retryCount = retries.size();
        QTest::qWait(100);
        QCOMPARE(retries.size(), retryCount);
        QVERIFY(!client.isConnectionActive());
    }

    void displayedEndpointExcludesCredentials()
    {
        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        RemoteConnectionController controls(&client, &remote,
            {QStringLiteral("wss://name:secret@[::1]:50055/path?token=secret#secret"),
             QStringLiteral("private-token"), {}, false});
        QCOMPARE(controls.endpointText(), QStringLiteral("[::1]:50055"));
        QVERIFY(!controls.detailText().contains(QStringLiteral("secret")));
        QVERIFY(!controls.detailText().contains(QStringLiteral("private-token")));
    }

    void attachingAndReconnectingHydratesOneSliceWithoutAdding()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        station.setBoardForTest(HPSDRHW::Saturn);
        station.configureStreamPool(5, 5, 192000);
        station.setConnectionStateForTest(ConnectionState::Connected);
        const int existing = station.addSlice(QStringLiteral("pan-0"));
        QVERIFY(station.sliceById(existing));
        StationServer server(&station, settings, dir.path());
        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        QSignalSpy additions(&station, &RadioModel::sliceAdded);
        connect(&remote, &RadioModel::connectionStateChanged, &client,
                [&](ConnectionState state) {
            if (state != ConnectionState::Connected) { return; }
            QMetaObject::invokeMethod(&client, [&] {
                MainWindow::populatePanSlices(&remote, {QStringLiteral("pan-0")},
                                              false, client.isHandshakeComplete());
            }, Qt::QueuedConnection);
        });
        for (int attachment = 0; attachment < 2; ++attachment) {
            auto* stationLink = new Test::LoopbackTransport(QStringLiteral("station"));
            auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
            stationLink->linkTo(clientLink);
            client.startSession(clientLink, server.token());
            server.acceptTransport(stationLink);
            QTRY_VERIFY(client.isHandshakeComplete());
            QCoreApplication::processEvents();
            QCOMPARE(station.slices().size(), 1);
            QCOMPARE(remote.slices().size(), 1);
            QCOMPARE(additions.size(), 0);
            QVERIFY(remote.sliceById(existing));
            client.disconnectFromStation(QStringLiteral("operator disconnect"));
        }
    }

    void automaticPanRestorationNeverCreatesStationSlices()
    {
        RecordingStationLink link;
        RadioModel model(RadioModel::Role::Remote);
        model.attachStation(&link);
        model.setConnectionStateForTest(ConnectionState::Connected);
        // Capability exchange can say radio-connected before any slice
        // snapshot arrives. This is the production startup population path.
        MainWindow::populatePanSlices(&model, {QStringLiteral("pan-0")}, false, false);
        QCOMPARE(link.additions.size(), 0);
        MainWindow::populatePanSlices(&model, {QStringLiteral("pan-0")}, false, true);
        QCOMPARE(link.additions.size(), 0);
    }

    void explicitPopulationWaitsForSnapshotThenCreates()
    {
        RecordingStationLink link;
        RadioModel model(RadioModel::Role::Remote);
        model.attachStation(&link);
        model.setConnectionStateForTest(ConnectionState::Connected);
        MainWindow::populatePanSlices(&model, {QStringLiteral("pan-0")}, true, false);
        QCOMPARE(link.additions.size(), 0);
        MainWindow::populatePanSlices(&model, {QStringLiteral("pan-0")}, true, true);
        QCOMPARE(link.additions, QStringList{QStringLiteral("pan-0")});
    }
};

QTEST_MAIN(TestRemoteConnectionControls)
#include "tst_remote_connection_controls.moc"
