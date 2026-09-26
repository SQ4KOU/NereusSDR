// Focused transport regressions for bounded, untrusted LAN Core discovery.
// iPhone app Task 71 (R-IOS-02, ruling 10.4): the "Devices connected" byte
// appended after Pairing, its absence, and its bound. J.J. Boyd (KG4VCF),
// 2026-09-25, AI-assisted via Anthropic Claude Code.
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

    void theDeviceCountIsOneByteAfterPairing()
    {
        QString error;
        StationLanAnnouncement counted = announcementV2();
        const QByteArray without = encodeStationLanAnnouncement(counted, &error);
        QVERIFY2(!without.isEmpty(), qPrintable(error));
        counted.devicesConnected = 3;
        const QByteArray with = encodeStationLanAnnouncement(counted, &error);
        QVERIFY2(!with.isEmpty(), qPrintable(error));
        QCOMPARE(with.size(), without.size() + 1);
        QCOMPARE(with.left(without.size()), without);
        QCOMPARE(with.back(), char(3));
        const auto decoded = decodeStationLanAnnouncement(with, &error);
        QVERIFY2(decoded, qPrintable(error));
        QCOMPARE(decoded->devicesConnected, std::optional<int>(3));
        QCOMPARE(*decoded, counted);

        // A datagram from a Core before the count: decoded, count unknown.
        const auto older = decodeStationLanAnnouncement(without, &error);
        QVERIFY2(older, qPrintable(error));
        QVERIFY(!older->devicesConnected);

        // Bytes after the count are ignored, as any appended field's are.
        const auto extended =
            decodeStationLanAnnouncement(with + QByteArray("\x07\x01\x00", 3), &error);
        QVERIFY2(extended, qPrintable(error));
        QCOMPARE(extended->devicesConnected, std::optional<int>(3));

        // 0 to 4 only, either way.
        StationLanAnnouncement tooMany = announcementV2();
        tooMany.devicesConnected = kStationLanMaxDevicesConnected + 1;
        QVERIFY(encodeStationLanAnnouncement(tooMany, &error).isEmpty());
        QByteArray five = with;
        five.back() = char(5);
        QVERIFY(!decodeStationLanAnnouncement(five, &error));
        // Schema 1 has nowhere to put it.
        StationLanAnnouncement schemaOne = announcement();
        schemaOne.devicesConnected = 1;
        QVERIFY(encodeStationLanAnnouncement(schemaOne, &error).isEmpty());

        // The largest datagram, both names and the label at their limits
        // and the count, is 480 bytes, under a listener's 512.
        StationLanAnnouncement largest = announcementV2();
        largest.coreName = QString(kStationLanMaxCoreNameBytes, QLatin1Char('c'));
        largest.radioName = QString(kStationLanMaxRadioNameBytes, QLatin1Char('r'));
        largest.label = QString(32, QLatin1Char('K')) + QLatin1Char('/')
            + QString(32, QLatin1Char('s'));
        largest.devicesConnected = kStationLanMaxDevicesConnected;
        const QByteArray biggest = encodeStationLanAnnouncement(largest, &error);
        QVERIFY2(!biggest.isEmpty(), qPrintable(error));
        QCOMPARE(biggest.size(), kStationLanMaxSchema2DatagramBytes);
        QCOMPARE(kStationLanMaxSchema2DatagramBytes, 480);
        QVERIFY(kStationLanMaxSchema2DatagramBytes <= kStationLanMaxDatagramBytes);
    }

    void aSchemaOneDatagramKeepsTheDeviceCount()
    {
        StationLanDiscovery discovery;
        QVERIFY(discovery.start(0));
        QUdpSocket sender;
        QString error;
        StationLanAnnouncement counted = announcementV2(4);
        counted.devicesConnected = 2;
        sendLoopback(&sender, encodeStationLanAnnouncement(counted, &error), discovery.port());
        QTRY_COMPARE(discovery.endpoints().size(), 1);
        QCOMPARE(discovery.endpoints().first().announcement.devicesConnected,
                 std::optional<int>(2));
        // The same endpoint in schema 1 refreshes what schema 1 carries.
        sendLoopback(&sender, datagram(4), discovery.port());
        QTest::qWait(50);
        QCOMPARE(discovery.endpoints().size(), 1);
        QCOMPARE(discovery.endpoints().first().announcement.devicesConnected,
                 std::optional<int>(2));
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
