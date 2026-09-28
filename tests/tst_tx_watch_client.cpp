// no-port-check: NereusSDR-original direct watch transport regression.
#include <QtTest/QtTest>

#include <QAbstractSocket>
#include <QCryptographicHash>
#include <QFile>
#include <QHash>
#include <QHostAddress>
#include <QPointer>
#include <QSignalSpy>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QWebSocket>
#include <QWebSocketServer>

#include "core/safety/RemoteTxWatchdog.h"
#include "core/security/CertificateStore.h"
#include "core/session/TxWatchClient.h"

using namespace NereusSDR;

namespace {
class TlsWatchPeer {
public:
    TlsWatchPeer()
        : identity(directory.path())
        , listener(QStringLiteral("watch-test"), QWebSocketServer::SecureMode)
    {
        QFile key(directory.filePath(QStringLiteral("tls-key.pem")));
        if (identity.isValid() && key.open(QIODevice::ReadOnly)) {
            QSslConfiguration tls = QSslConfiguration::defaultConfiguration();
            tls.setLocalCertificate(identity.certificate());
            tls.setPrivateKey(QSslKey(key.readAll(), QSsl::Rsa));
            listener.setSslConfiguration(tls);
            listening = listener.listen(QHostAddress::LocalHost, 0);
            QObject::connect(&listener, &QWebSocketServer::newConnection, &listener,
                             [this]() {
                QWebSocket* socket = listener.nextPendingConnection();
                sockets.append(socket);
                QObject::connect(socket, &QWebSocket::binaryMessageReceived,
                                 &listener, [this, socket](const QByteArray& bytes) {
                    messages[socket].append(bytes);
                });
            });
        }
    }

    QUrl url() const
    {
        return QUrl(QStringLiteral("wss://127.0.0.1:%1/primary?ignored=1")
                        .arg(listener.serverPort()));
    }

    QByteArray pin() const
    {
        return identity.certificate().digest(QCryptographicHash::Sha256);
    }

    QTemporaryDir directory;
    CertificateStore identity;
    QWebSocketServer listener;
    bool listening = false;
    QList<QWebSocket*> sockets;
    QHash<QWebSocket*, QList<QByteArray>> messages;
};

QByteArray ticket()
{
    return QByteArray(32, '\x5a');
}
}

class TestTxWatchClient : public QObject {
    Q_OBJECT
private slots:
    void correctFreshTlsPinAttachesAndSendsExistingFrame()
    {
        if (!QSslSocket::supportsSsl()) { QSKIP("TLS backend unavailable"); }
        TlsWatchPeer peer;
        QVERIFY(peer.listening);
        TxWatchClient client;
        QSignalSpy ready(&client, &TxWatchClient::ready);
        QVERIFY(client.openDirect(peer.url(), peer.pin(), ticket(), 71));
        QTRY_COMPARE_WITH_TIMEOUT(peer.sockets.size(), 1, 5000);
        QWebSocket* socket = peer.sockets.at(0);
        QCOMPARE(socket->requestUrl().path(), QStringLiteral("/tx-watch/v1"));
        QVERIFY(!socket->requestUrl().hasQuery());
        QTRY_COMPARE_WITH_TIMEOUT(peer.messages[socket].size(), 1, 5000);
        QCOMPARE(peer.messages[socket].at(0), QByteArray(1, '\x01') + ticket());
        QVERIFY(!client.isReady());
        socket->sendBinaryMessage(QByteArray::fromHex("0100"));
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 5000);
        QCOMPARE(ready.at(0).at(0).toULongLong(), quint64(71));
        QVERIFY(client.isReady());
        QVERIFY(client.sendKeepalive(123, 45));
        QTRY_COMPARE_WITH_TIMEOUT(peer.messages[socket].size(), 2, 5000);
        QCOMPARE(peer.messages[socket].at(1),
                 RemoteTxWatchdog::channelKeepalive(123, 45));
    }

    void wrongActualPinNeverSendsTicket()
    {
        if (!QSslSocket::supportsSsl()) { QSKIP("TLS backend unavailable"); }
        TlsWatchPeer peer;
        QVERIFY(peer.listening);
        TxWatchClient client;
        QSignalSpy closed(&client, &TxWatchClient::closed);
        QByteArray wrong = peer.pin();
        wrong[0] = char(wrong.at(0) ^ 1);
        QVERIFY(client.openDirect(peer.url(), wrong, ticket(), 72));
        // A TLS handshake may be rejected before the listener upgrades the socket.
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 5000);
        if (!peer.sockets.isEmpty()) {
            QTest::qWait(100);
            QCOMPARE(peer.messages[peer.sockets.at(0)].size(), 0);
        }
        QVERIFY(!client.isReady());
    }

    void badInputsAreRefusedBeforeDial()
    {
        TxWatchClient client;
        const QByteArray pin(32, 'p');
        QVERIFY(!client.openDirect(QUrl(QStringLiteral("ws://127.0.0.1:5")), pin, ticket(), 1));
        QVERIFY(!client.openDirect(QUrl(QStringLiteral("wss://u:p@127.0.0.1:5")), pin, ticket(), 2));
        QVERIFY(!client.openDirect(QUrl(QStringLiteral("wss://@127.0.0.1:5")), pin, ticket(), 6));
        QVERIFY(!client.openDirect(QUrl(QStringLiteral("wss:///nohost")), pin, ticket(), 3));
        QVERIFY(!client.openDirect(QUrl(QStringLiteral("wss://127.0.0.1:5")), pin.left(31), ticket(), 4));
        QVERIFY(!client.openDirect(QUrl(QStringLiteral("wss://127.0.0.1:5")), pin, ticket().left(31), 5));
        QVERIFY(client.findChildren<QWebSocket*>().isEmpty());
    }

    void earlyDataAndDuplicateAckCloseOnlyWatch()
    {
        if (!QSslSocket::supportsSsl()) { QSKIP("TLS backend unavailable"); }
        TlsWatchPeer peer;
        QVERIFY(peer.listening);
        TxWatchClient client;
        QSignalSpy closed(&client, &TxWatchClient::closed);
        QVERIFY(client.openDirect(peer.url(), peer.pin(), ticket(), 10));
        QTRY_COMPARE_WITH_TIMEOUT(peer.sockets.size(), 1, 5000);
        QWebSocket* first = peer.sockets.at(0);
        first->sendBinaryMessage(QByteArray(13, 'x'));
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 5000);
        QVERIFY(!client.isReady());

        QVERIFY(client.openDirect(peer.url(), peer.pin(), ticket(), 11));
        QTRY_COMPARE_WITH_TIMEOUT(peer.sockets.size(), 2, 5000);
        QWebSocket* second = peer.sockets.at(1);
        second->sendBinaryMessage(QByteArray::fromHex("0100"));
        QTRY_VERIFY_WITH_TIMEOUT(client.isReady(), 5000);
        second->sendBinaryMessage(QByteArray::fromHex("0100"));
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 2, 5000);
        QVERIFY(!client.isReady());
    }

    void missingAckExpiresAndTextIsNeverAccepted()
    {
        if (!QSslSocket::supportsSsl()) { QSKIP("TLS backend unavailable"); }
        TlsWatchPeer peer;
        QVERIFY(peer.listening);
        TxWatchClient client;
        client.setDeadlinesForTesting(1000, 100);
        QSignalSpy closed(&client, &TxWatchClient::closed);
        QVERIFY(client.openDirect(peer.url(), peer.pin(), ticket(), 30));
        QTRY_COMPARE_WITH_TIMEOUT(peer.sockets.size(), 1, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(peer.messages[peer.sockets.at(0)].size(), 1, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 2000);
        QVERIFY(closed.at(0).at(1).toString().contains(QStringLiteral("timed out")));
        QVERIFY(!client.isReady());

        QVERIFY(client.openDirect(peer.url(), peer.pin(), ticket(), 31));
        QTRY_COMPARE_WITH_TIMEOUT(peer.sockets.size(), 2, 5000);
        peer.sockets.at(1)->sendTextMessage(QStringLiteral("ok"));
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 2, 2000);
        QVERIFY(!client.isReady());
    }

    void oversizedAndMalformedAcknowledgementsAreRefused()
    {
        if (!QSslSocket::supportsSsl()) { QSKIP("TLS backend unavailable"); }
        TlsWatchPeer peer;
        QVERIFY(peer.listening);
        TxWatchClient client;
        QSignalSpy closed(&client, &TxWatchClient::closed);
        QVERIFY(client.openDirect(peer.url(), peer.pin(), ticket(), 41));
        QTRY_COMPARE_WITH_TIMEOUT(peer.sockets.size(), 1, 5000);
        peer.sockets.at(0)->sendBinaryMessage(QByteArray::fromHex("0101"));
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 5000);
        QVERIFY(client.openDirect(peer.url(), peer.pin(), ticket(), 42));
        QTRY_COMPARE_WITH_TIMEOUT(peer.sockets.size(), 2, 5000);
        peer.sockets.at(1)->sendBinaryMessage(QByteArray(34, 'x'));
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 2, 5000);
        QVERIFY(!client.isReady());
    }

    void closedHandlerMayReplaceGenerationWithoutOldCallbacks()
    {
        if (!QSslSocket::supportsSsl()) { QSKIP("TLS backend unavailable"); }
        TlsWatchPeer peer;
        QVERIFY(peer.listening);
        TxWatchClient client;
        QSignalSpy ready(&client, &TxWatchClient::ready);
        QObject::connect(&client, &TxWatchClient::closed, &client,
                         [&](quint64 generation, const QString&) {
            if (generation == 51) {
                QVERIFY(client.openDirect(peer.url(), peer.pin(), ticket(), 52));
            }
        });
        QVERIFY(client.openDirect(peer.url(), peer.pin(), ticket(), 51));
        QTRY_COMPARE_WITH_TIMEOUT(peer.sockets.size(), 1, 5000);
        peer.sockets.at(0)->sendTextMessage(QStringLiteral("bad"));
        QTRY_COMPARE_WITH_TIMEOUT(peer.sockets.size(), 2, 5000);
        peer.sockets.at(1)->sendBinaryMessage(QByteArray::fromHex("0100"));
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 5000);
        QCOMPARE(ready.at(0).at(0).toULongLong(), quint64(52));
        QCOMPARE(client.generation(), quint64(52));
        QVERIFY(client.isReady());
    }

    void readyHandlerMayDeleteClient()
    {
        if (!QSslSocket::supportsSsl()) { QSKIP("TLS backend unavailable"); }
        TlsWatchPeer peer;
        QVERIFY(peer.listening);
        QPointer<TxWatchClient> client = new TxWatchClient;
        QObject::connect(client, &TxWatchClient::ready, client,
                         [&client](quint64) { delete client.data(); });
        QVERIFY(client->openDirect(peer.url(), peer.pin(), ticket(), 61));
        QTRY_COMPARE_WITH_TIMEOUT(peer.sockets.size(), 1, 5000);
        peer.sockets.at(0)->sendBinaryMessage(QByteArray::fromHex("0100"));
        QTRY_VERIFY_WITH_TIMEOUT(client.isNull(), 5000);
    }

    void fullOutboundBacklogClosesWithoutSendingFrame()
    {
        if (!QSslSocket::supportsSsl()) { QSKIP("TLS backend unavailable"); }
        TlsWatchPeer peer;
        QVERIFY(peer.listening);
        TxWatchClient client;
        QSignalSpy closed(&client, &TxWatchClient::closed);
        QVERIFY(client.openDirect(peer.url(), peer.pin(), ticket(), 70));
        QTRY_COMPARE_WITH_TIMEOUT(peer.sockets.size(), 1, 5000);
        QWebSocket* socket = peer.sockets.at(0);
        QTRY_COMPARE_WITH_TIMEOUT(peer.messages[socket].size(), 1, 5000);
        socket->sendBinaryMessage(QByteArray::fromHex("0100"));
        QTRY_VERIFY_WITH_TIMEOUT(client.isReady(), 5000);
        client.setBacklogBytesForTesting(4084);
        QVERIFY(!client.sendKeepalive(9, 2));
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 5000);
        QCOMPARE(peer.messages[socket].size(), 1);
    }

    void stalledLocalTlsOpeningExpires()
    {
        QTcpServer silent;
        QVERIFY(silent.listen(QHostAddress::LocalHost, 0));
        TxWatchClient client;
        client.setDeadlinesForTesting(100, 100);
        QSignalSpy closed(&client, &TxWatchClient::closed);
        const QUrl url(QStringLiteral("wss://127.0.0.1:%1/primary")
                           .arg(silent.serverPort()));
        QVERIFY(client.openDirect(url, QByteArray(32, 'p'), ticket(), 80));
        QTRY_COMPARE_WITH_TIMEOUT(closed.size(), 1, 2000);
        QVERIFY(closed.at(0).at(1).toString().contains(QStringLiteral("timed out")));
        QVERIFY(!client.isReady());
    }

    void sendingBeforeAckClosesTheWatch()
    {
        QTcpServer silent;
        QVERIFY(silent.listen(QHostAddress::LocalHost, 0));
        TxWatchClient client;
        QSignalSpy closed(&client, &TxWatchClient::closed);
        const QUrl url(QStringLiteral("wss://127.0.0.1:%1/").arg(silent.serverPort()));
        QVERIFY(client.openDirect(url, QByteArray(32, 'p'), ticket(), 81));
        QVERIFY(!client.sendKeepalive(1, 1));
        QCOMPARE(closed.size(), 1);
        QCOMPARE(closed.at(0).at(0).toULongLong(), quint64(81));
        QVERIFY(!client.isReady());
    }

    void synchronousInitialSendFailureMayDeleteClient()
    {
        if (!QSslSocket::supportsSsl()) { QSKIP("TLS backend unavailable"); }
        TlsWatchPeer peer;
        QVERIFY(peer.listening);
        QPointer<TxWatchClient> client = new TxWatchClient;
        QObject::connect(client, &TxWatchClient::closed, client,
                         [&client](quint64, const QString&) { delete client.data(); });
        client->setBinaryWriterForTesting(
            [](QWebSocket* socket, const QByteArray&) -> qint64 {
                socket->errorOccurred(QAbstractSocket::NetworkError);
                return -1;
            });
        QVERIFY(client->openDirect(peer.url(), peer.pin(), ticket(), 90));
        QTRY_VERIFY_WITH_TIMEOUT(client.isNull(), 5000);
        if (!peer.sockets.isEmpty()) {
            QCOMPARE(peer.messages[peer.sockets.at(0)].size(), 0);
        }
    }

    void synchronousInitialSendFailureCannotCloseReplacement()
    {
        if (!QSslSocket::supportsSsl()) { QSKIP("TLS backend unavailable"); }
        TlsWatchPeer peer;
        QVERIFY(peer.listening);
        TxWatchClient client;
        QSignalSpy ready(&client, &TxWatchClient::ready);
        QObject::connect(&client, &TxWatchClient::closed, &client,
                         [&](quint64 generation, const QString&) {
            if (generation == 94) {
                QVERIFY(client.openDirect(peer.url(), peer.pin(), ticket(), 95));
            }
        });
        client.setBinaryWriterForTesting(
            [&client](QWebSocket* socket, const QByteArray&) -> qint64 {
                client.setBinaryWriterForTesting({});
                socket->errorOccurred(QAbstractSocket::NetworkError);
                return -1;
            });
        QVERIFY(client.openDirect(peer.url(), peer.pin(), ticket(), 94));
        QTRY_COMPARE_WITH_TIMEOUT(peer.sockets.size(), 2, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(peer.messages[peer.sockets.at(1)].size(), 1, 5000);
        peer.sockets.at(1)->sendBinaryMessage(QByteArray::fromHex("0100"));
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 5000);
        QCOMPARE(ready.at(0).at(0).toULongLong(), quint64(95));
        QVERIFY(client.isReady());
    }

    void synchronousKeepaliveSendFailureCannotCloseReplacement()
    {
        if (!QSslSocket::supportsSsl()) { QSKIP("TLS backend unavailable"); }
        TlsWatchPeer peer;
        QVERIFY(peer.listening);
        TxWatchClient client;
        QSignalSpy ready(&client, &TxWatchClient::ready);
        QObject::connect(&client, &TxWatchClient::closed, &client,
                         [&](quint64 generation, const QString&) {
            if (generation == 91) {
                QVERIFY(client.openDirect(peer.url(), peer.pin(), ticket(), 92));
            }
        });
        QVERIFY(client.openDirect(peer.url(), peer.pin(), ticket(), 91));
        QTRY_COMPARE_WITH_TIMEOUT(peer.sockets.size(), 1, 5000);
        peer.sockets.at(0)->sendBinaryMessage(QByteArray::fromHex("0100"));
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 5000);
        client.setBinaryWriterForTesting(
            [&client](QWebSocket* socket, const QByteArray&) -> qint64 {
                client.setBinaryWriterForTesting({});
                socket->errorOccurred(QAbstractSocket::NetworkError);
                return -1;
            });
        QVERIFY(!client.sendKeepalive(2, 3));
        QTRY_COMPARE_WITH_TIMEOUT(peer.sockets.size(), 2, 5000);
        peer.sockets.at(1)->sendBinaryMessage(QByteArray::fromHex("0100"));
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 2, 5000);
        QCOMPARE(ready.at(1).at(0).toULongLong(), quint64(92));
        QCOMPARE(client.generation(), quint64(92));
        QVERIFY(client.isReady());
    }

    void synchronousKeepaliveSendFailureMayDeleteClient()
    {
        if (!QSslSocket::supportsSsl()) { QSKIP("TLS backend unavailable"); }
        TlsWatchPeer peer;
        QVERIFY(peer.listening);
        QPointer<TxWatchClient> client = new TxWatchClient;
        QVERIFY(client->openDirect(peer.url(), peer.pin(), ticket(), 93));
        QTRY_COMPARE_WITH_TIMEOUT(peer.sockets.size(), 1, 5000);
        peer.sockets.at(0)->sendBinaryMessage(QByteArray::fromHex("0100"));
        QTRY_VERIFY_WITH_TIMEOUT(client->isReady(), 5000);
        QObject::connect(client, &TxWatchClient::closed, client,
                         [&client](quint64, const QString&) { delete client.data(); });
        client->setBinaryWriterForTesting(
            [](QWebSocket* socket, const QByteArray&) -> qint64 {
                socket->errorOccurred(QAbstractSocket::NetworkError);
                return -1;
            });
        QVERIFY(!client->sendKeepalive(4, 5));
        QVERIFY(client.isNull());
    }
};

QTEST_GUILESS_MAIN(TestTxWatchClient)
#include "tst_tx_watch_client.moc"
