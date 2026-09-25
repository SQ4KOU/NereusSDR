// Focused transport regressions for bounded, untrusted LAN Core discovery.
#include <QtTest>

#include <QSignalSpy>
#include <QUdpSocket>

#include "core/session/StationLanAnnouncer.h"
#include "core/session/StationLanDiscovery.h"

using namespace NereusSDR;

namespace {

QString pin(int seed = 0)
{
    QString result;
    for (int index = 0; index < 32; ++index) {
        if (index != 0) {
            result += QLatin1Char(':');
        }
        result += QString::number((seed + index) & 0xff, 16)
            .rightJustified(2, QLatin1Char('0')).toUpper();
    }
    return result;
}

StationLanAnnouncement announcement(int seed = 0)
{
    return {47910, pin(seed), QStringLiteral("Core"), QStringLiteral("Radio"),
            QStringLiteral("AA:BB:CC:DD:EE:FF"), true};
}

// iPhone app Task 16: what a Core sends now.
StationLanAnnouncement announcementV2(int seed = 0)
{
    StationLanAnnouncement value = announcement(seed);
    value.schema = kStationLanAnnouncementSchema;
    value.identity = QByteArray(kStationLanIdentityBytes, static_cast<char>(0x30 + seed));
    value.label = QStringLiteral("KG4VCF/shack");
    value.pairing = StationLanPairing::Click;
    return value;
}

QByteArray datagram(int seed = 0)
{
    QString error;
    const QByteArray result = encodeStationLanAnnouncement(announcement(seed), &error);
    Q_ASSERT(!result.isEmpty());
    Q_ASSERT(error.isEmpty());
    return result;
}

void sendLoopback(QUdpSocket* sender, const QByteArray& bytes, quint16 port)
{
    QCOMPARE(sender->writeDatagram(bytes, QHostAddress::LocalHost, port), bytes.size());
}

} // namespace

class TstStationLanTransport : public QObject {
    Q_OBJECT

private slots:
    void listenerFamilyPredicate()
    {
        const QHostAddress ipv4(QStringLiteral("192.0.2.10"));
        const QHostAddress anotherIpv4(QStringLiteral("192.0.2.11"));
        const QHostAddress ipv6(QStringLiteral("2001:db8::10"));

        QVERIFY(stationLanListenerServesAddress(QHostAddress::Any, ipv4));
        QVERIFY(stationLanListenerServesAddress(QHostAddress::Any, ipv6));
        QVERIFY(stationLanListenerServesAddress(QHostAddress::AnyIPv4, ipv4));
        QVERIFY(!stationLanListenerServesAddress(QHostAddress::AnyIPv4, ipv6));
        QVERIFY(stationLanListenerServesAddress(QHostAddress::AnyIPv6, ipv6));
        QVERIFY(!stationLanListenerServesAddress(QHostAddress::AnyIPv6, ipv4));
        QVERIFY(stationLanListenerServesAddress(ipv4, ipv4));
        QVERIFY(!stationLanListenerServesAddress(ipv4, anotherIpv4));
        QVERIFY(!stationLanListenerServesAddress(QHostAddress::LocalHost, ipv4));
        QVERIFY(!stationLanListenerServesAddress(QHostAddress::AnyIPv4,
                                                 QHostAddress::LocalHost));
        QVERIFY(!stationLanListenerServesAddress(QHostAddress::AnyIPv4,
                                                 QHostAddress::AnyIPv4));
    }

    void changedAddressRebindsWithoutLosingPortOrCache()
    {
        StationLanDiscovery discovery;
        QVERIFY(discovery.start(0));
        const quint16 port = discovery.port();
        QUdpSocket sender;
        sendLoopback(&sender, datagram(), port);
        QTRY_COMPARE(discovery.endpoints().size(), 1);
        const auto generation = discovery.m_generation;
        // Simulate the previous eligible address snapshot for the same
        // interface. Production refresh compares it with current OS addresses.
        discovery.m_ipv4EligibleIdentities.insert(QStringLiteral("7") + QChar(0x1f) + QStringLiteral("192.0.2.10"));
        discovery.m_ipv4JoinedInterfaces.insert(0x7fffffff);
        QVERIFY(discovery.refreshMulticastMembership());
        QVERIFY(discovery.m_generation > generation);
        QCOMPARE(discovery.port(), port);
        QCOMPARE(discovery.m_ipv4Socket->localPort(), port);
        QVERIFY(!discovery.m_ipv4JoinedInterfaces.contains(0x7fffffff));
        QCOMPARE(discovery.endpoints().size(), 1);
        sendLoopback(&sender, datagram(1), port);
        QTRY_COMPARE(discovery.endpoints().size(), 2);

        // A lost family socket is retried even without a further topology
        // change, and rejoins replace any stale down-link membership state.
        discovery.m_ipv4Socket->close();
        discovery.m_ipv4JoinedInterfaces.insert(0x7fffffff);
        QVERIFY(discovery.refreshMulticastMembership());
        QCOMPARE(discovery.m_ipv4Socket->localPort(), port);
        QVERIFY(!discovery.m_ipv4JoinedInterfaces.contains(0x7fffffff));
        sendLoopback(&sender, datagram(2), port);
        QTRY_COMPARE(discovery.endpoints().size(), 3);
    }

    void loopbackDatagramIsIngested()
    {
        StationLanDiscovery discovery;
        QSignalSpy changed(&discovery, &StationLanDiscovery::changed);
        QVERIFY(discovery.start(0));
        QVERIFY(discovery.port() != 0);
        const QString membershipWarning = discovery.lastError();
        const quint16 port = discovery.port();
        QVERIFY(discovery.start(0));
        QCOMPARE(discovery.port(), port);

        QUdpSocket sender;
        sendLoopback(&sender, datagram(), discovery.port());
        QTRY_COMPARE(discovery.endpoints().size(), 1);
        QCOMPARE(discovery.endpoints().first().announcement, announcement());
        if (!membershipWarning.isEmpty()) {
            QCOMPARE(discovery.lastError(), membershipWarning);
        }
        QVERIFY(changed.count() >= 2);
    }

    void oversizedDatagramIsDiscardedBeforeDecode()
    {
        StationLanDiscovery discovery;
        QVERIFY(discovery.start(0));
        const QString membershipWarning = discovery.lastError();

        QUdpSocket sender;
        sendLoopback(&sender, QByteArray(kStationLanMaxDatagramBytes + 1, 'x'), discovery.port());
        if (membershipWarning.isEmpty()) {
            QTRY_COMPARE(discovery.lastError(),
                         QStringLiteral("Station LAN discovery ignored an oversized datagram."));
        } else {
            QTest::qWait(20);
            QCOMPARE(discovery.lastError(), membershipWarning);
        }
        QVERIFY(discovery.endpoints().isEmpty());
    }

    void deferredDrainCanStopThenRebindAndIngest()
    {
        StationLanDiscovery discovery;
        QVERIFY(discovery.start(0));
        const quint16 selectedPort = discovery.port();
        bool stoppedFromDeferredDrain = false;
        const QMetaObject::Connection stopOnEndpoint = connect(
            &discovery, &StationLanDiscovery::changed, &discovery, [&discovery, &stoppedFromDeferredDrain]() {
                if (!discovery.endpoints().isEmpty()) {
                    stoppedFromDeferredDrain = true;
                    discovery.stop();
                }
            });

        QUdpSocket sender;
        sendLoopback(&sender, datagram(), discovery.port());
        QTRY_VERIFY(stoppedFromDeferredDrain);
        QCOMPARE(discovery.port(), 0);
        QVERIFY(discovery.endpoints().isEmpty());
        disconnect(stopOnEndpoint);

        QVERIFY(discovery.start(selectedPort));
        QCOMPARE(discovery.port(), selectedPort);
        sendLoopback(&sender, datagram(), discovery.port());
        QTRY_COMPARE(discovery.endpoints().size(), 1);
    }

    void oneDrainBatchesEndpointNotifications()
    {
        StationLanDiscovery discovery;
        QSignalSpy changed(&discovery, &StationLanDiscovery::changed);
        QVERIFY(discovery.start(0));
        QCOMPARE(changed.count(), 1);

        QUdpSocket sender;
        sendLoopback(&sender, datagram(1), discovery.port());
        sendLoopback(&sender, datagram(2), discovery.port());
        QTRY_COMPARE(discovery.endpoints().size(), 2);
        QCOMPARE(changed.count(), 2);
    }

    void schemaOneAndSchemaTwoAreBothRead()
    {
        // A Core from before Task 16 (schema 1) and one from after it
        // (schema 2), heard on the same socket.
        StationLanDiscovery discovery;
        QVERIFY(discovery.start(0));
        QUdpSocket sender;
        QString error;
        const StationLanAnnouncement newer = announcementV2(1);
        sendLoopback(&sender, encodeStationLanAnnouncement(newer, &error), discovery.port());
        sendLoopback(&sender, datagram(2), discovery.port());
        QTRY_COMPARE(discovery.endpoints().size(), 2);
        QList<StationLanAnnouncement> heard;
        for (const StationLanEndpoint& endpoint : discovery.endpoints()) {
            heard.append(endpoint.announcement);
        }
        QVERIFY(heard.contains(newer));
        QVERIFY(heard.contains(announcement(2)));
    }

    void announcerRejectsInvalidState()
    {
        StationLanAnnouncer announcer;
        announcer.update(QHostAddress::LocalHost, announcement());
        QVERIFY(!announcer.isActive());
        // A listener on loopback only is never announced, in either schema.
        announcer.update(QHostAddress::LocalHost, announcementV2());
        QVERIFY(!announcer.isActive());
        announcer.update(QHostAddress::LocalHostIPv6, announcementV2());
        QVERIFY(!announcer.isActive());

        // A station sends schema 2 only: a schema-1 announcement is refused
        // before anything is sent.
        announcer.update(QHostAddress(QStringLiteral("192.0.2.200")), announcement());
        QVERIFY(!announcer.isActive());
        // Schema 2 is accepted. The listener address is TEST-NET-1, which no
        // interface holds, so nothing reaches a network.
        announcer.update(QHostAddress(QStringLiteral("192.0.2.200")), announcementV2());
        QVERIFY(announcer.isActive());
        QCOMPARE(announcer.announcement().schema, kStationLanAnnouncementSchema2);
        announcer.stop();
        QVERIFY(!announcer.isActive());

        StationLanAnnouncement invalid = announcement();
        invalid.controlPort = 0;
        announcer.update(QHostAddress::AnyIPv4, invalid);
        QVERIFY(!announcer.isActive());
    }
};

QTEST_GUILESS_MAIN(TstStationLanTransport)
#include "tst_station_lan_transport.moc"
