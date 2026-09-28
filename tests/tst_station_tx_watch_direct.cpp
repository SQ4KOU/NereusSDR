// no-port-check: NereusSDR-original direct paired watch integration test.
#include <QtTest>

#include <QHostAddress>
#include <QSignalSpy>
#include <QSslSocket>
#include <QTemporaryDir>

#include "core/AppSettings.h"
#include "core/safety/RemoteTxWatchdog.h"
#include "core/security/ClientDeviceIdentity.h"
#include "core/security/DeviceStore.h"
#include "core/session/RemoteTransmitClient.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/settings/SettingsProxy.h"
#include "models/RadioModel.h"
#include "fakes/DataChannelPair.h"
#include "fakes/UpgradedCoreToken.h"

using namespace NereusSDR;

class TestStationTxWatchDirect : public QObject {
    Q_OBJECT
private slots:
    void pairedDataChannelDoesNotRequestDirectWatch()
    {
        QTemporaryDir settingsDir;
        QTemporaryDir securityDir;
        QTemporaryDir deviceDir;
        QVERIFY(settingsDir.isValid());
        QVERIFY(securityDir.isValid());
        QVERIFY(deviceDir.isValid());
        AppSettings settings(settingsDir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        station.setBoardForTest(HPSDRHW::HermesLite);
        station.addSlice(QStringLiteral("pan-0"));
        StationServer server(&station, settings, Test::seedCoreIdentity(securityDir.path()));
        server.setRemoteTransmitAllowed(true);
        const auto device = std::make_shared<const ClientDeviceIdentity>(
            ClientDeviceIdentity::loadOrCreate(deviceDir.path()));
        QVERIFY(device->isValid());
        PairedDevice record;
        record.id = device->fingerprint();
        record.publicKeySpki = device->publicKeySpki();
        record.name = QStringLiteral("Watch desktop");
        record.kind = QStringLiteral("computer");
        QVERIFY(server.deviceStore()->add(record));
        QVERIFY2(server.listen(QHostAddress::LocalHost, 0), qPrintable(server.lastError()));

        auto* offerer = new DataChannelTransport;
        auto* answerer = new DataChannelTransport;
        QSignalSpy inbound(answerer, &SessionTransport::textReceived);
        QVERIFY(Test::startDataChannelPair(offerer, answerer,
                                          StationClient::kMaxIncomingMessageBytes,
                                          StationServer::kMaxIncomingMessageBytes,
                                          server.certificatePemPath(),
                                          server.privateKeyPemPath()));
        QTRY_VERIFY_WITH_TIMEOUT(offerer->isOpen() && answerer->isOpen(), 10000);
        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        client.setDeviceIdentity(device, record.name);
        client.startSession(offerer, QString(), QString(),
                            server.stationIdentity().fingerprint());
        server.acceptTransport(answerer);
        QTRY_VERIFY_WITH_TIMEOUT(client.isHandshakeComplete(), 10000);
        // Even a mistaken or malicious capability claim cannot make a
        // DataChannel primary request a direct WSS watch ticket.
        StationCapabilities claimed = client.capabilities();
        claimed.txWatchPathVersion = 1;
        answerer->sendText(SessionMessages::encode(
            SessionMessages::capabilities(claimed.toUpdates())));
        QTRY_COMPARE(client.capabilities().txWatchPathVersion, 1);
        QTest::qWait(1200); // includes the earliest allowed ticket retry interval
        QVERIFY(!client.directWatchReady());
        for (const QList<QVariant>& received : inbound) {
            SessionMessage message;
            QVERIFY(SessionMessages::decode(received.first().toByteArray(), &message));
            QVERIFY(message.commandVerb != QByteArrayLiteral("tx.watchTicket"));
        }
    }

    void pairedPrimaryAttachesIndependentWatchAndPrimaryCloseStopsIt()
    {
        if (!QSslSocket::supportsSsl()) { QSKIP("TLS backend unavailable"); }
        QTemporaryDir settingsDir;
        QTemporaryDir securityDir;
        QTemporaryDir deviceDir;
        QVERIFY(settingsDir.isValid());
        QVERIFY(securityDir.isValid());
        QVERIFY(deviceDir.isValid());
        AppSettings settings(settingsDir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        station.setBoardForTest(HPSDRHW::HermesLite);
        station.addSlice(QStringLiteral("pan-0"));
        StationServer server(&station, settings, Test::seedCoreIdentity(securityDir.path()));
        server.setRemoteTransmitAllowed(true);
        const auto device = std::make_shared<const ClientDeviceIdentity>(
            ClientDeviceIdentity::loadOrCreate(deviceDir.path()));
        QVERIFY(device->isValid());
        PairedDevice record;
        record.id = device->fingerprint();
        record.publicKeySpki = device->publicKeySpki();
        record.name = QStringLiteral("Watch desktop");
        record.kind = QStringLiteral("computer");
        QVERIFY(server.deviceStore()->add(record));
        QVERIFY2(server.listen(QHostAddress::LocalHost, 0), qPrintable(server.lastError()));

        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        client.setDeviceIdentity(device, record.name);
        const QUrl url(QStringLiteral("wss://127.0.0.1:%1/").arg(server.serverPort()));
        client.connectToStation(url, QString(), QString(), false,
                                server.stationIdentity().fingerprint());
        QTRY_VERIFY_WITH_TIMEOUT(client.isHandshakeComplete(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(client.directWatchReady(), 10000);
        QCOMPARE(client.connectedUrl().host(), QStringLiteral("127.0.0.1"));
        QCOMPARE(server.peerCount(), 1); // auxiliary does not adopt a primary peer

        RemoteTxWatchdog* watchdog = server.txWatchdog();
        QVERIFY(watchdog != nullptr);
        QSignalSpy heard(watchdog, &RemoteTxWatchdog::keepaliveHeard);
        QSignalSpy tripped(watchdog, &RemoteTxWatchdog::tripped);
        watchdog->setVoxArmed(record.id, true); // logical watch; no radio key
        client.remoteTransmit()->setSessionKeepalive([](quint64, quint32) { return false; });
        client.remoteTransmit()->setVoxArmed(true);
        QTRY_VERIFY_WITH_TIMEOUT(!heard.isEmpty(), 2000);
        QCOMPARE(heard.first().at(0).toByteArray(), record.id);

        client.disconnectFromStation(QStringLiteral("test primary close"));
        QTRY_VERIFY_WITH_TIMEOUT(!client.directWatchReady(), 2000);
        QTRY_COMPARE_WITH_TIMEOUT(tripped.size(), 1, 5000);
        QCOMPARE(tripped.first().at(1).toBool(), true);

        // A fresh primary authentication, rather than the retired auxiliary,
        // is the only route back to an independent watch.
        client.connectToStation(url, QString(), QString(), false,
                                server.stationIdentity().fingerprint());
        QTRY_VERIFY_WITH_TIMEOUT(client.isHandshakeComplete(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(client.directWatchReady(), 10000);
        QCOMPARE(server.peerCount(), 1);
    }
};

QTEST_MAIN(TestStationTxWatchDirect)
#include "tst_station_tx_watch_direct.moc"
