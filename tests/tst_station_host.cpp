// no-port-check: NereusSDR-original. Task 48, step 1. No radio hardware is opened here.
#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/ConnectionState.h"
#include "core/daemon/DaemonTelemetryController.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/settings/SettingsProxy.h"
#include "core/station/StationHost.h"
#include "fakes/LoopbackTransport.h"
#include "fakes/UpgradedCoreToken.h"
#include "models/RadioModel.h"

#include <QPointer>
#include <QSslSocket>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace NereusSDR;

namespace {
StationHostOptions listenerOptions()
{
    StationHostOptions options;
    options.settings = &AppSettings::instance();
    options.remoteBind = QStringLiteral("127.0.0.1");
    options.statusPage = false;
    options.rendezvousServers.clear();
    QTcpServer probe;
    if (probe.listen(QHostAddress::LocalHost, 0)) {
        options.remotePort = probe.serverPort();
        probe.close();
    }
    return options;
}
}

class TstStationHost : public QObject {
    Q_OBJECT
private slots:
    void borrowedModelSurvivesStopAndRestart()
    {
        RadioModel model;
        QPointer<RadioModel> borrowed(&model);
        StationHostOptions options;
        options.settings = &AppSettings::instance();
        options.remotePort = 0;
        options.remoteTransmitAllowed = false;
        StationHost host(&model, options);
        QVERIFY(host.server() == nullptr); // construction has no server/profile work
        QVERIFY(host.start());
        QVERIFY(host.server() == nullptr);
        QVERIFY(model.pcCaptureAllowed());
        QVERIFY(model.audioEngine()->vaxOutputsAllowed());
        QVERIFY(!model.receiveOnlyStationPolicy());
        host.stop();
        QVERIFY(borrowed == &model);
        QVERIFY(host.start());
        host.stop();
        QVERIFY(borrowed == &model);
    }

    void syntheticConnectionChangesAnnouncement()
    {
        if (!QSslSocket::supportsSsl()) {
            QSKIP("Qt reports no working TLS backend.");
        }
        RadioModel model;
        StationHostOptions options = listenerOptions();
        QVERIFY(options.remotePort > 0);
        StationHost host(&model, options);
        QVERIFY(host.start());
        QVERIFY(host.listenerReady());
        QVERIFY(!host.stationAnnouncementForTest().radioConnected);
        model.setConnectionStateForTest(ConnectionState::Connected);
        QVERIFY(host.stationAnnouncementForTest().radioConnected);
        model.setConnectionStateForTest(ConnectionState::Disconnected);
        QVERIFY(!host.stationAnnouncementForTest().radioConnected);
        host.stop();
        QVERIFY(host.server() == nullptr);
    }

    void earlyBorrowedModelDestructionClosesHost()
    {
        if (!QSslSocket::supportsSsl()) {
            QSKIP("Qt reports no working TLS backend.");
        }
        auto model = std::make_unique<RadioModel>();
        StationHostOptions options = listenerOptions();
        StationHost host(model.get(), options);
        QVERIFY(host.start());
        QVERIFY(host.server() != nullptr);
        model.reset();
        QVERIFY(host.server() == nullptr);
        QVERIFY(!host.listenerReady());
        host.stop();
    }

    void stopReenteredFromListenerCloseFinishes()
    {
        if (!QSslSocket::supportsSsl()) {
            QSKIP("Qt reports no working TLS backend.");
        }
        RadioModel model;
        StationHostOptions options = listenerOptions();
        StationHost host(&model, options);
        QVERIFY(host.start());
        StationServer* server = host.server();
        QVERIFY(server && server->isListening());
        int callbacks = 0;
        connect(server, &StationServer::listeningChanged, &host, [&](bool listening) {
            if (!listening) {
                ++callbacks;
                host.stop();
            }
        });
        host.stop();
        QCOMPARE(callbacks, 1);
        QVERIFY(host.server() == nullptr);
        QVERIFY(!host.listenerRetryPending());
        QVERIFY(host.start());
        host.stop();
    }

    void quiesceDuringListenDoesNotStartRendezvous()
    {
        if (!QSslSocket::supportsSsl()) {
            QSKIP("Qt reports no working TLS backend.");
        }
        RadioModel model;
        StationHostOptions options = listenerOptions();
        StationHost host(&model, options);
        int callbacks = 0;
        host.setServerCreatedForTest([&](StationServer* server) {
            connect(server, &StationServer::listeningChanged, &host, [&](bool listening) {
                if (listening) {
                    ++callbacks;
                    host.quiesce();
                }
            });
        });
        QVERIFY(!host.start());
        QCOMPARE(callbacks, 1);
        QVERIFY(host.server() != nullptr); // quiesce retains borrowed dependencies
        QVERIFY(!host.listenerReady());
        QVERIFY(!host.listenerRetryPending());
        host.stop();
        QVERIFY(host.server() == nullptr);
    }

    void endedTelemetryPendingDeletionSurvivesBorrowedModelDeath()
    {
        if (!QSslSocket::supportsSsl()) {
            QSKIP("Qt reports no working TLS backend.");
        }
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        AppSettings settings(directory.filePath(QStringLiteral("station.settings")));
        Test::seedUpgradedCoreToken(directory.path());
        auto station = std::make_unique<RadioModel>();
        StationHostOptions options = listenerOptions();
        options.settings = &settings;
        options.securityDirectory = directory.path();
        StationHost host(station.get(), options);
        QVERIFY(host.start());
        StationServer* server = host.server();
        QVERIFY(server && server->isListening());
        const QString token = server->token();
        QVERIFY(!token.isEmpty());

        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        auto* stationLink = new Test::LoopbackTransport(QStringLiteral("host-station"), &host);
        auto* clientLink = new Test::LoopbackTransport(QStringLiteral("host-client"), &host);
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, token);
        server->acceptTransport(stationLink);
        QTRY_VERIFY(client.telemetryAvailable());
        QTRY_VERIFY(!host.findChildren<DaemonTelemetryController*>().isEmpty());
        DaemonTelemetryController* controller = host.findChild<DaemonTelemetryController*>();
        QVERIFY(controller && controller->isCollecting());
        const quint64 epoch = controller->sessionEpoch();
        QVERIFY(epoch != 0);

        // Simulate the session-ended emission: Host releases the controller
        // from its map and schedules deleteLater, while its own slot stops
        // sampling. Do not process DeferredDelete before removing the model.
        emit server->telemetrySessionEnded(epoch);
        QPointer<DaemonTelemetryController> pending(controller);
        QVERIFY(pending);
        QVERIFY(!controller->isCollecting());
        station.reset(); // Host closes and destroys its server, never the model.
        QVERIFY(host.server() == nullptr);
        controller->sampleNow(); // inert with both borrowed pointers retired
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(pending.isNull());
    }
};

QTEST_MAIN(TstStationHost)
#include "tst_station_host.moc"
