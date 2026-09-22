// no-port-check: NereusSDR-original observational transport regression.
#include <QHostAddress>
#include <QSignalSpy>
#include <QTest>
#include <QWebSocket>
#include <QWebSocketServer>
#include "core/session/SessionTransport.h"

using namespace NereusSDR;

class TestSessionTransportTelemetry : public QObject {
    Q_OBJECT
private slots:
    void countsActualUtf8PayloadAndObservesExistingPong()
    {
        QWebSocketServer listener(QStringLiteral("metrics-test"),
                                  QWebSocketServer::NonSecureMode);
        QVERIFY(listener.listen(QHostAddress::LocalHost, 0));
        auto* socket = new QWebSocket;
        WebSocketTransport client(socket, 16384);
        QVERIFY(!client.telemetry());
        socket->open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(listener.serverPort())));
        QTRY_VERIFY(listener.hasPendingConnections());
        WebSocketTransport server(listener.nextPendingConnection(), 16384);
        QTRY_VERIFY(client.isOpen());
        QVERIFY(client.telemetry());
        QVERIFY(!client.telemetry()->pongRttMs);
        const QByteArray outward = QStringLiteral("Core → GUI: 音声").toUtf8();
        QSignalSpy received(&server, &SessionTransport::textReceived);
        client.sendText(outward);
        QTRY_COMPARE(received.count(), 1);
        QCOMPARE(received.first().first().toByteArray(), outward);
        QCOMPARE(client.telemetry()->acceptedPayloadBytes,
                 static_cast<quint64>(outward.size()));
        QCOMPARE(server.telemetry()->receivedPayloadBytes,
                 static_cast<quint64>(outward.size()));
        QCOMPARE(client.telemetry()->receivedPayloadBytes, quint64(0));

        const QByteArray inward = QByteArrayLiteral("response");
        server.sendText(inward);
        QTRY_COMPARE(client.telemetry()->receivedPayloadBytes,
                     static_cast<quint64>(inward.size()));
        QSignalSpy pong(&client, &SessionTransport::pongReceived);
        client.ping();
        QTRY_COMPARE(pong.count(), 1);
        QVERIFY(client.telemetry()->pongRttMs);
        QVERIFY(client.telemetry()->pongAgeMs);
        // Ping/pong frames are not counted as application text payload.
        QCOMPARE(client.telemetry()->acceptedPayloadBytes,
                 static_cast<quint64>(outward.size()));
        QCOMPARE(client.telemetry()->receivedPayloadBytes,
                 static_cast<quint64>(inward.size()));
        client.closeLink(QStringLiteral("test complete"));
        QTRY_VERIFY(!client.isOpen());
        QVERIFY(!client.telemetry());
    }
};

QTEST_GUILESS_MAIN(TestSessionTransportTelemetry)
#include "tst_session_transport_telemetry.moc"
