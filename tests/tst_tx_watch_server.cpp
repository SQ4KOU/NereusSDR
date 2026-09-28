// no-port-check: NereusSDR-original auxiliary watch protocol tests.
#include "core/session/TxWatchServer.h"
#include "core/session/SessionTransport.h"
#include "core/safety/RemoteTxWatchdog.h"
#include "core/session/StationServer.h"
#include "core/AppSettings.h"
#include "models/RadioModel.h"
#include "fakes/UpgradedCoreToken.h"

#include <QtTest>
#include <QHostAddress>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QTemporaryDir>
#include <QWebSocket>

using namespace NereusSDR;

namespace {
class Link final : public SessionTransport {
public:
    bool open = true;
    QByteArray lastBinary;
    int closes = 0;
    qint64 backlog = 0;
    void sendText(const QByteArray&) override {}
    void ping() override {}
    void closeLink(const QString&) override
    {
        if (open) {
            open = false;
            ++closes;
            emit closed(); // deliberately synchronous, to catch reentrant erasure
        }
    }
    bool isOpen() const override { return open; }
    QString peerDescription() const override { return QStringLiteral("test"); }
    bool sendBinary(const QByteArray& message) override
    {
        lastBinary = message;
        return open;
    }
    qint64 backlogBytes() const override { return backlog; }
    void binary(const QByteArray& message) { emit binaryReceived(message); }
    void text() { emit textReceived(QByteArrayLiteral("wrong")); }
};

QByteArray ticket(char fill)
{
    return QByteArray(TxWatchServer::kTicketBytes, fill);
}

QByteArray attach(const QByteArray& raw)
{
    return QByteArray(1, '\x01') + raw;
}

struct Rig {
    qint64 time = 100000;
    quint64 generation = 1;
    bool permitted = true;
    int delivered = 0;
    Link primary;
    TxWatchServer server;

    Rig() : server(
        [this](SessionTransport* link, quint64 id, const QByteArray& device, quint64 gen) {
            return permitted && link == &primary && primary.open && id == 7
                && device == QByteArrayLiteral("paired") && gen == generation;
        },
        [this](const QByteArray&, quint64, quint32) { ++delivered; }, nullptr,
        [this]() { return time; }) {}

    std::optional<TxWatchServer::Ticket> issue(char fill = 'a')
    {
        return server.issue(&primary, 7, QByteArrayLiteral("paired"), generation, ticket(fill));
    }
    Link* socket(const QString& address = QStringLiteral("shared-nat"))
    {
        auto* link = new Link;
        server.acceptTransport(link, address);
        return link;
    }
};
} // namespace

class TestTxWatchServer : public QObject {
    Q_OBJECT
private slots:
    void oneUseTicketAndStrictFrames()
    {
        Rig rig;
        QVERIFY(rig.issue());
        QVERIFY(!rig.issue('b'));
        Link* text = rig.socket();
        text->text();
        QCOMPARE(text->closes, 1);
        Link* malformed = rig.socket();
        malformed->binary(RemoteTxWatchdog::channelKeepalive(1, 9));
        QCOMPARE(malformed->closes, 1);
        Link* wrong = rig.socket();
        wrong->binary(attach(ticket('x')));
        QCOMPARE(wrong->closes, 1);
        Link* watch = rig.socket();
        watch->binary(attach(ticket('a')));
        QCOMPARE(watch->lastBinary, QByteArray::fromHex("0100"));
        QCOMPARE(rig.server.bindingCount(), 1);
        watch->binary(RemoteTxWatchdog::channelKeepalive(1, 9));
        QCOMPARE(rig.delivered, 1);
        watch->binary(QByteArray(13, '\0'));
        QCOMPARE(watch->closes, 1);
        Link* reused = rig.socket();
        reused->binary(attach(ticket('a')));
        QCOMPARE(reused->closes, 1);
        QCOMPARE(rig.server.bindingCount(), 0);
    }

    void expiryGenerationAndPermission()
    {
        Rig rig;
        QVERIFY(rig.issue());
        rig.time += TxWatchServer::kTicketLifetimeMs;
        Link* late = rig.socket();
        late->binary(attach(ticket('a')));
        QCOMPARE(late->closes, 1);
        QVERIFY(rig.issue('b'));
        ++rig.generation;
        Link* stale = rig.socket();
        stale->binary(attach(ticket('b')));
        QCOMPARE(stale->closes, 1);
        QVERIFY(!rig.issue('c')); // consumed, but still within the issue interval
        rig.time += TxWatchServer::kIssueIntervalMs;
        QVERIFY(rig.issue('c'));
        Link* watch = rig.socket();
        watch->binary(attach(ticket('c')));
        QCOMPARE(watch->lastBinary, QByteArray::fromHex("0100"));
        rig.permitted = false;
        watch->binary(RemoteTxWatchdog::channelKeepalive(1, 9));
        QCOMPARE(rig.delivered, 0);
        QCOMPARE(watch->closes, 1);
    }

    void pendingAndRateBounds()
    {
        Rig rig;
        QList<Link*> pending;
        for (int i = 0; i < TxWatchServer::kMaxPendingSockets; ++i) {
            pending.append(rig.socket());
            QVERIFY(pending.last()->open);
        }
        Link* ninth = rig.socket();
        QCOMPARE(ninth->closes, 1);
        QCOMPARE(rig.server.pendingSocketCount(), TxWatchServer::kMaxPendingSockets);
        rig.time += TxWatchServer::kAttachDeadlineMs;
        pending.first()->binary(attach(ticket('x')));
        QCOMPARE(pending.first()->closes, 1);
        for (Link* link : pending) {
            link->closeLink({});
        }
        QVERIFY(rig.issue());
        Link* watch = rig.socket();
        watch->binary(attach(ticket('a')));
        for (int i = 0; i < TxWatchServer::kBurstFrames; ++i) {
            watch->binary(RemoteTxWatchdog::channelKeepalive(i + 1, 9));
        }
        QCOMPARE(rig.delivered, TxWatchServer::kBurstFrames);
        watch->binary(RemoteTxWatchdog::channelKeepalive(21, 9));
        QCOMPARE(watch->closes, 1);
    }

    void retireBeforeLateCallback()
    {
        Rig rig;
        QVERIFY(rig.issue());
        Link* watch = rig.socket();
        watch->binary(attach(ticket('a')));
        rig.server.retire(&rig.primary);
        QCOMPARE(watch->closes, 1);
        watch->binary(RemoteTxWatchdog::channelKeepalive(1, 9));
        QCOMPARE(rig.delivered, 0);
        QCOMPARE(rig.server.bindingCount(), 0);
    }

    void fourBindingsAndOneActivePerPrimary()
    {
        Link primaries[5];
        qint64 time = 100000;
        TxWatchServer server(
            [&primaries](SessionTransport* link, quint64 id, const QByteArray& device,
                         quint64 generation) {
                for (int i = 0; i < 5; ++i) {
                    if (link == &primaries[i]) {
                        return primaries[i].open && id == static_cast<quint64>(i + 1)
                            && device == QByteArray::number(i + 1) && generation == 1;
                    }
                }
                return false;
            }, [](const QByteArray&, quint64, quint32) {}, nullptr,
            [&time]() { return time; });
        for (int i = 0; i < 4; ++i) {
            QVERIFY(server.issue(&primaries[i], i + 1, QByteArray::number(i + 1), 1,
                                 ticket(static_cast<char>('a' + i))));
        }
        QVERIFY(!server.issue(&primaries[4], 5, QByteArrayLiteral("5"), 1, ticket('e')));
        for (int i = 0; i < 4; ++i) {
            auto* auxiliary = new Link;
            QVERIFY(server.acceptTransport(auxiliary, QStringLiteral("one-nat")));
            auxiliary->binary(attach(ticket(static_cast<char>('a' + i))));
            QCOMPARE(auxiliary->lastBinary, QByteArray::fromHex("0100"));
            QVERIFY(!server.issue(&primaries[i], i + 1, QByteArray::number(i + 1), 1,
                                  ticket('z')));
        }
        QCOMPARE(server.bindingCount(), 4);
        server.retire(&primaries[0]);
        ++time;
        QVERIFY(server.issue(&primaries[4], 5, QByteArrayLiteral("5"), 1, ticket('e')));
    }

    void realStationDirectRouteDoesNotAdoptAnAuxiliary()
    {
        if (!QSslSocket::supportsSsl()) {
            QSKIP("Qt TLS backend unavailable");
        }
        QTemporaryDir settingsDir;
        QTemporaryDir securityDir;
        QVERIFY(settingsDir.isValid());
        QVERIFY(securityDir.isValid());
        AppSettings settings(settingsDir.filePath(QStringLiteral("station.settings")));
        RadioModel model;
        StationServer station(&model, settings,
            NereusSDR::Test::seedUpgradedCoreToken(securityDir.path()));
        QVERIFY2(station.listen(QHostAddress::LocalHost, 0), qPrintable(station.lastError()));

        QWebSocket auxiliary;
        QSslConfiguration tls = auxiliary.sslConfiguration();
        tls.setPeerVerifyMode(QSslSocket::VerifyNone);
        auxiliary.setSslConfiguration(tls);
        const QString authority = QStringLiteral("wss://127.0.0.1:%1").arg(station.serverPort());
        auxiliary.open(QUrl(authority + QLatin1String(TxWatchServer::kPath)));
        QVERIFY(QTest::qWaitFor([&]() { return auxiliary.state() == QAbstractSocket::ConnectedState; }, 5000));
        QCOMPARE(station.peerCount(), 0);
        auxiliary.sendBinaryMessage(attach(ticket('x')));
        QVERIFY(QTest::qWaitFor([&]() { return auxiliary.state() == QAbstractSocket::UnconnectedState; }, 5000));
        QCOMPARE(station.peerCount(), 0);

        QWebSocket query;
        query.setSslConfiguration(tls);
        QSignalSpy queryClosed(&query, &QWebSocket::disconnected);
        query.open(QUrl(authority + QLatin1String(TxWatchServer::kPath) + QStringLiteral("?ticket=x")));
        QVERIFY(QTest::qWaitFor([&]() { return !queryClosed.isEmpty(); }, 5000));
        QCOMPARE(station.peerCount(), 0);
        station.close();
    }
};

QTEST_MAIN(TestTxWatchServer)
#include "tst_tx_watch_server.moc"
