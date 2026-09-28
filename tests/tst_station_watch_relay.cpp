// no-port-check: NereusSDR-original Core relay watch integration tests.
#include <QtTest>

#include "core/AppSettings.h"
#include "core/safety/RemoteTxWatchdog.h"
#include "core/security/DeviceAuthenticator.h"
#include "core/security/DeviceStore.h"
#include "core/security/StationIdentity.h"
#include "core/session/DataChannelTransport.h"
#include "core/session/RelayLeg.h"
#include "core/session/SessionMessages.h"
#include "core/session/StationServer.h"
#include "core/session/TxWatchServer.h"
#include "core/session/media/IMediaTransport.h"
#include "fakes/DataChannelPair.h"
#include "fakes/UpgradedCoreToken.h"
#include "models/RadioModel.h"

#include <QDateTime>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QWebSocket>
#include <QWebSocketServer>

using namespace NereusSDR;

class TstStationWatchRelay : public QObject {
    Q_OBJECT
private slots:
    void cleanup()
    {
        RelayLeg::setRelayUrlForTest({});
        DataChannelTransport::setSelectedPathOverrideForTest({});
    }

    void pairedPrimaryIssuesSeparateRelayDtlsWatch()
    {
        QTemporaryDir settingsDir;
        QTemporaryDir securityDir;
        QTemporaryDir deviceDir;
        QVERIFY(settingsDir.isValid() && securityDir.isValid() && deviceDir.isValid());
        AppSettings settings(settingsDir.filePath(QStringLiteral("station.settings")));
        RadioModel model;
        model.setBoardForTest(HPSDRHW::HermesLite);
        model.addSlice(QStringLiteral("pan-0"));
        StationServer station(&model, settings,
            Test::withSharedTlsIdentity(Test::seedCoreIdentity(securityDir.path())));
        station.setRemoteTransmitAllowed(true);
        StationIdentity device = StationIdentity::loadOrCreate(deviceDir.path());
        PairedDevice record;
        record.id = device.fingerprint();
        record.publicKeySpki = device.publicKeySpki();
        record.name = QStringLiteral("Relay watch device");
        record.kind = QStringLiteral("computer");
        QVERIFY(station.deviceStore()->add(record));
        QVERIFY2(station.listen(QHostAddress::LocalHost, 0), qPrintable(station.lastError()));

        QWebSocketServer relay(QStringLiteral("local watch relay"),
                               QWebSocketServer::NonSecureMode);
        QVERIFY(relay.listen(QHostAddress::LocalHost, 0));
        QHash<QString, QPointer<QWebSocket>> legs;
        QList<QByteArray> watchFrames;
        connect(&relay, &QWebSocketServer::newConnection, &relay, [&]() {
            QWebSocket* socket = relay.nextPendingConnection();
            connect(socket, &QWebSocket::binaryMessageReceived, &relay,
                    [&, socket](const QByteArray& frame) {
                if (frame.isEmpty()) {
                    socket->close();
                    return;
                }
                if (quint8(frame.at(0)) == RelayLeg::kTagJoin) {
                    const QString token = QString::fromLatin1(frame.mid(1));
                    if (token != QLatin1String("core-primary")
                        && token != QLatin1String("core-watch")
                        && token != QLatin1String("client-watch")) {
                        socket->close();
                        return;
                    }
                    legs.insert(token, socket);
                    socket->sendBinaryMessage(QByteArray::fromHex("810101"));
                    return;
                }
                if (quint8(frame.at(0)) != RelayLeg::kTagWatch) {
                    return;
                }
                watchFrames.append(frame);
                const QString destination = legs.value(QStringLiteral("core-watch")) == socket
                    ? QStringLiteral("client-watch") : QStringLiteral("core-watch");
                if (QWebSocket* other = legs.value(destination)) {
                    other->sendBinaryMessage(frame);
                }
            });
        });
        RelayLeg::setRelayUrlForTest(QUrl(QStringLiteral("ws://127.0.0.1:%1")
                                            .arg(relay.serverPort())));
        auto primaryLeg = RelayLeg::create();
        QVERIFY(primaryLeg);
        primaryLeg->open(QUrl(QStringLiteral("wss://relay.example/v1/relay")),
                         QStringLiteral("core-primary"));
        QTRY_VERIFY(primaryLeg->state() == RelayLeg::State::Joined && primaryLeg->peerPresent());

        auto* client = new DataChannelTransport;
        auto* primary = new DataChannelTransport;
        connect(primary, &DataChannelTransport::opened, &station,
                [&station, primary]() { station.acceptTransport(primary); });
        QList<SessionMessage> messages;
        connect(client, &SessionTransport::textReceived, client,
                [&messages](const QByteArray& wire) {
            SessionMessage message;
            if (SessionMessages::decode(wire, &message)) {
                messages.append(message);
            }
        });
        QVERIFY(Test::startDataChannelPair(client, primary,
                                           1024 * 1024, StationServer::kMaxIncomingMessageBytes,
                                           station.certificatePemPath(), station.privateKeyPemPath()));
        QTRY_VERIFY(client->isOpen() && primary->isOpen());
        DataChannelTransport::WatchRelayGrant grant{
            QUrl(QStringLiteral("wss://relay.example/v1/relay")),
            QStringLiteral("core-watch"), QDateTime::currentSecsSinceEpoch() + 60, primaryLeg};
        QVERIFY(primary->setWatchRelayGrant(grant));
        DataChannelTransport::setSelectedPathOverrideForTest(
            [primary](const DataChannelTransport* channel) -> std::optional<MediaIcePath> {
            if (channel != primary) {
                return std::nullopt;
            }
            MediaIcePath path;
            path.remoteAddress = QStringLiteral("127.0.0.1");
            return path;
        });
        const auto latest = [&messages](SessionMessageKind kind, quint32 id = 0) {
            for (const SessionMessage& message : messages) {
                if (message.kind == kind && (id == 0 || message.commandId == id)) {
                    return message;
                }
            }
            return SessionMessage{};
        };
        QTRY_VERIFY(latest(SessionMessageKind::Hello).kind == SessionMessageKind::Hello);
        const SessionMessage serverHello = latest(SessionMessageKind::Hello);
        QString fingerprint = station.certificateFingerprint();
        fingerprint.remove(QLatin1Char(':'));
        const QByteArray certHash = QByteArray::fromHex(fingerprint.toLatin1());
        const SessionDeviceBlock proof{
            StationIdentity::toBase64Url(record.id),
            StationIdentity::toBase64Url(device.publicKeySpki()),
            record.name, record.kind,
            StationIdentity::toBase64Url(device.sign(DeviceAuthenticator::transcript(
                StationIdentity::fromBase64Url(serverHello.challenge), certHash,
                station.stationIdentity().publicKeySpki(), device.publicKeySpki())))};
        const auto sendPrimary = [client](const SessionMessage& message) {
            client->sendText(SessionMessages::encode(message));
        };
        sendPrimary(SessionMessages::hello(kSessionProtocolMajor, kSessionProtocolMinor, 0,
            record.name, {kSessionProtocolMajor},
            {{"deviceAuth", 1}, {"remoteTx", 1}, {"txWatchPath", 1}, {"txWatchRelay", 1}}));
        sendPrimary(SessionMessages::authRequest(QString(), proof));
        QTRY_VERIFY(station.authenticatedSessionCount() == 1);
        QTRY_VERIFY(latest(SessionMessageKind::SnapshotComplete).kind
                    == SessionMessageKind::SnapshotComplete);
        QVERIFY(primary->canOpenWatchRelay());
        bool advertised = false;
        for (const SessionMessage& message : messages) {
            if (message.kind != SessionMessageKind::Capabilities) {
                continue;
            }
            for (const MirrorUpdate& update : message.updates) {
                if (update.name == QByteArrayLiteral("txWatchPathVersion")
                    && update.value.toInt() == 1) {
                    advertised = true;
                }
            }
        }
        QVERIFY(advertised);

        sendPrimary(SessionMessages::commandInvoke(QByteArrayLiteral("tx.watchRelay"), 800,
            {{0, "offer", MirrorWireKind::Utf8,
              QString(IMediaTransport::kMaxDescriptionBytes + 1, QLatin1Char('x'))}}));
        QTRY_VERIFY(latest(SessionMessageKind::CommandResult, 800).commandId == 800);
        QVERIFY(!latest(SessionMessageKind::CommandResult, 800).accepted);

        sendPrimary(SessionMessages::commandInvoke(QByteArrayLiteral("tx.watchRelay"), 801,
            {{0, "offer", MirrorWireKind::Utf8, QStringLiteral("bad")}}));
        QTRY_VERIFY(latest(SessionMessageKind::CommandResult, 801).commandId == 801);
        QVERIFY(!latest(SessionMessageKind::CommandResult, 801).accepted);

        auto clientWatchLeg = RelayLeg::createWatch();
        QVERIFY(clientWatchLeg);
        clientWatchLeg->open(grant.url, QStringLiteral("client-watch"));
        QTRY_COMPARE(clientWatchLeg->state(), RelayLeg::State::Joined);
        IceConfiguration ice = IceConfiguration::throughRendezvous({}, true, {}, {});
        ice.setRelay(std::nullopt, 1);
        ice.setCandidateSourceFactory(RelayLeg::factoryFor(clientWatchLeg), true);
        DataChannelTransport watch;
        DataChannelTransport::Options options;
        options.role = DataChannelTransport::Role::Offerer;
        options.purpose = DataChannelTransport::Purpose::TxWatch;
        options.maxIncomingBytes = DataChannelTransport::kMaxWatchFrameBytes;
        options.ice = ice;
        QSignalSpy localOffer(&watch, &DataChannelTransport::localDescription);
        QSignalSpy attached(&watch, &SessionTransport::binaryReceived);
        QVERIFY(watch.start(options));
        QTRY_VERIFY(!localOffer.isEmpty());
        const QString offer = localOffer.first().first().toString();
        sendPrimary(SessionMessages::commandInvoke(QByteArrayLiteral("tx.watchRelay"), 802,
            {{0, "offer", MirrorWireKind::Utf8, offer}}));
        QTRY_VERIFY_WITH_TIMEOUT(latest(SessionMessageKind::CommandResult, 802).commandId == 802,
                                 10000);
        QVERIFY(!latest(SessionMessageKind::CommandResult, 802).accepted);
        QTest::qWait(TxWatchServer::kIssueIntervalMs);
        sendPrimary(SessionMessages::commandInvoke(QByteArrayLiteral("tx.watchRelay"), 803,
            {{0, "offer", MirrorWireKind::Utf8, offer}}));
        QTRY_VERIFY_WITH_TIMEOUT(latest(SessionMessageKind::CommandResult, 803).commandId == 803,
                                 10000);
        const SessionMessage result = latest(SessionMessageKind::CommandResult, 803);
        QVERIFY2(result.accepted, qPrintable(result.reason));
        QCOMPARE(result.updates.size(), 4);
        QCOMPARE(result.updates.at(2).value.toString(), QStringLiteral("relay-dtls-v1"));
        bool canonical = false;
        const QByteArray ticket = StationIdentity::fromBase64Url(
            result.updates.at(0).value.toString(), &canonical);
        QVERIFY(canonical);
        QCOMPARE(ticket.size(), TxWatchServer::kTicketBytes);
        QVERIFY(watch.acceptDescription(result.updates.at(3).value.toString(),
                                        QStringLiteral("answer")));
        QTRY_VERIFY_WITH_TIMEOUT(watch.isOpen(), 10000);
        QVERIFY(watch.sendBinary(QByteArray(1, char(1)) + ticket));
        QTRY_VERIFY(!attached.isEmpty());
        QCOMPARE(attached.first().first().toByteArray(), QByteArray::fromHex("0100"));
        RemoteTxWatchdog* watchdog = station.txWatchdog();
        QVERIFY(watchdog);
        QSignalSpy heard(watchdog, &RemoteTxWatchdog::keepaliveHeard);
        watchdog->setKeyed(record.id, true, 9);
        QVERIFY(watch.sendBinary(RemoteTxWatchdog::channelKeepalive(1, 9)));
        QTRY_COMPARE(heard.size(), 1);
        QVERIFY(!watchFrames.isEmpty());
        QCOMPARE(legs.size(), 3);
        const auto lastWatchVersion = [&messages]() {
            for (auto message = messages.crbegin(); message != messages.crend(); ++message) {
                if (message->kind != SessionMessageKind::Capabilities) {
                    continue;
                }
                for (const MirrorUpdate& update : message->updates) {
                    if (update.name == QByteArrayLiteral("txWatchPathVersion")) {
                        return update.value.toInt();
                    }
                }
                return 0; // a zero route is omitted from the descriptor
            }
            return -1; // no descriptor yet
        };
        QCOMPARE(lastWatchVersion(), 1);
        station.setHeartbeatIntervalMs(100);
        primaryLeg->close();
        QTRY_COMPARE(lastWatchVersion(), 0);
        QVERIFY(watch.sendBinary(RemoteTxWatchdog::channelKeepalive(2, 9)));
        QTRY_VERIFY(!watch.isOpen());
        QCOMPARE(heard.size(), 1); // a vanished primary route cannot refresh the watch
        watchdog->setKeyed(record.id, false);
        client->closeLink(QStringLiteral("primary ended"));
        QTRY_VERIFY(station.authenticatedSessionCount() == 0);
        station.close();
        primaryLeg->close();
        clientWatchLeg->close();
        client->deleteLater();
    }
};

QTEST_MAIN(TstStationWatchRelay)
#include "tst_station_watch_relay.moc"
