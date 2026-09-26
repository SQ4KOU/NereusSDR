// no-port-check: NereusSDR-original.
// =================================================================
// tests/tst_ice_configuration.cpp  (NereusSDR)
// =================================================================
//
// iPhone app plan Task 27 (R-IOS-16): the ICE settings of a connection
// that came through the remote access service, shaped by the pinned
// libdatachannel v0.24.5 and libjuice @3c40a354 (IceConfiguration.h).
//
//   - One STUN server and, by default, one relay host (each allocation
//     takes one of the relay's four slots for the Core), chosen by the
//     address families this end has and never by the order the service
//     lists them in (fix wave I2): an IPv4-only end takes the IPv4-only
//     name and an IPv6-only end the IPv6-only one, whichever comes first; a
//     dual-stack end, or one that cannot tell, takes the first. Two relay
//     hosts (the first two) only when asked for; TURN over TCP or TLS is
//     never picked (libjuice speaks UDP only).
//   - `relay = deny`: no relay servers, and the far end's relay candidates
//     refused.
//   - The MTU and deadlines: 996 bytes, 23.5 s of gathering and 39.5 s of
//     connectivity checks.
//   - A media transport started with these settings offers no candidates
//     and gathers nothing until it is asked to, then gathers.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-26: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include <QtTest>

#include <QHostAddress>
#include <QSignalSpy>

#include "core/session/IceConfiguration.h"
#include "core/session/media/LibDataChannelMediaTransport.h"

using namespace NereusSDR;

namespace {

// The NereusSDR server's lists (the rendezvous document, section 8), in
// either order: nothing here may depend on which the service puts first.
const QStringList kStun4First{QStringLiteral("stun:rv4.nereussdr.com:3478"),
                              QStringLiteral("stun:rv6.nereussdr.com:3478")};
const QStringList kStun6First{QStringLiteral("stun:rv6.nereussdr.com:3478"),
                              QStringLiteral("stun:rv4.nereussdr.com:3478")};
const QStringList kTurn4First{QStringLiteral("turn:rv4.nereussdr.com:3478?transport=udp"),
                              QStringLiteral("turn:rv4.nereussdr.com:443?transport=udp"),
                              QStringLiteral("turn:rv6.nereussdr.com:3478?transport=udp"),
                              QStringLiteral("turn:rv6.nereussdr.com:443?transport=udp")};
const QStringList kTurn6First{QStringLiteral("turn:rv6.nereussdr.com:3478?transport=udp"),
                              QStringLiteral("turn:rv6.nereussdr.com:443?transport=udp"),
                              QStringLiteral("turn:rv4.nereussdr.com:3478?transport=udp"),
                              QStringLiteral("turn:rv4.nereussdr.com:443?transport=udp")};
const QStringList kStun = kStun4First;
const QStringList kTurn = kTurn4First;

// What each name resolves to, as this end's resolver would say.
const HostFamilies kResolved{{QStringLiteral("rv4.nereussdr.com"), AddressFamilies{true, false}},
                             {QStringLiteral("rv6.nereussdr.com"), AddressFamilies{false, true}}};
const AddressFamilies kIpv4Only{true, false};
const AddressFamilies kIpv6Only{false, true};
const AddressFamilies kDualStack{true, true};

RendezvousWire::Turn turnWith(const QStringList& urls)
{
    RendezvousWire::Turn turn;
    // Made up at run time for the test; never a real credential.
    turn.username = QStringLiteral("1800086400:abcdefghijklmnopqrstuvwxyz");
    turn.password = QStringLiteral("test-password");
    turn.expires = 1800086400;
    turn.urls = urls;
    return turn;
}

} // namespace

class TstIceConfiguration : public QObject {
    Q_OBJECT

private slots:
    void theLimitsFollowThePinnedLibraries()
    {
        QCOMPARE(IceConfiguration::kMaxRelayServers, 2);
        // 1000 bytes on the wire less TURN's 4-byte ChannelData header.
        QCOMPARE(IceConfiguration::kMtuBytes, 996);
        QCOMPARE(IceConfiguration::kMtuBytes + 4, IMediaTransport::kConfiguredMtuBytes);
        QCOMPARE(IceConfiguration::kGatheringDeadlineMs, 23500);
        QCOMPARE(IceConfiguration::kConnectivityTimeoutMs, 39500);
        QCOMPARE(IceConfiguration::kConnectDeadlineMs, 63000);
    }

    void stunUrlsParse()
    {
        QCOMPARE(IceConfiguration::parseStunUrl(QStringLiteral("stun:rv4.nereussdr.com:3478")),
                 std::optional<IceServerAddress>(IceServerAddress{QStringLiteral("rv4.nereussdr.com"), 3478}));
        QCOMPARE(IceConfiguration::parseStunUrl(QStringLiteral("stun:example.net")),
                 std::optional<IceServerAddress>(IceServerAddress{QStringLiteral("example.net"), 3478}));
        QCOMPARE(IceConfiguration::parseStunUrl(QStringLiteral("stun:[2001:db8::7]:5349")),
                 std::optional<IceServerAddress>(IceServerAddress{QStringLiteral("2001:db8::7"), 5349}));
        QVERIFY(!IceConfiguration::parseStunUrl(QStringLiteral("stun:2001:db8::7")));
        QVERIFY(!IceConfiguration::parseStunUrl(QStringLiteral("stun:host:0")));
        QVERIFY(!IceConfiguration::parseStunUrl(QStringLiteral("stun:host:70000")));
        QVERIFY(!IceConfiguration::parseStunUrl(QStringLiteral("turn:host:3478")));
        QVERIFY(!IceConfiguration::parseStunUrl(QStringLiteral("stun:")));
    }

    void turnUrlsParseUdpOnly()
    {
        QCOMPARE(IceConfiguration::parseTurnUrl(QStringLiteral("turn:rv6.nereussdr.com:443?transport=udp")),
                 std::optional<IceServerAddress>(IceServerAddress{QStringLiteral("rv6.nereussdr.com"), 443}));
        QCOMPARE(IceConfiguration::parseTurnUrl(QStringLiteral("turn:relay.example.net")),
                 std::optional<IceServerAddress>(IceServerAddress{QStringLiteral("relay.example.net"), 3478}));
        QCOMPARE(IceConfiguration::parseTurnUrl(QStringLiteral("turn:[2001:db8::1]:3478?transport=udp")),
                 std::optional<IceServerAddress>(IceServerAddress{QStringLiteral("2001:db8::1"), 3478}));
        // libjuice speaks TURN over UDP only.
        QVERIFY(!IceConfiguration::parseTurnUrl(QStringLiteral("turn:relay.example.net:443?transport=tcp")));
        QVERIFY(!IceConfiguration::parseTurnUrl(QStringLiteral("turns:relay.example.net:443")));
        QVERIFY(!IceConfiguration::parseTurnUrl(QStringLiteral("stun:relay.example.net")));
    }

    void oneStunServerTheFirstUsable()
    {
        // Nothing known about this end: the first usable entry.
        const IceConfiguration ice = IceConfiguration::throughRendezvous(kStun6First, true);
        QCOMPARE(ice.stunServer(),
                 std::optional<IceServerAddress>(IceServerAddress{QStringLiteral("rv6.nereussdr.com"), 3478}));
        QVERIFY(!ice.relayKnown());
        QVERIFY(ice.relayServers().isEmpty());

        const IceConfiguration skipped = IceConfiguration::throughRendezvous(
            {QStringLiteral("stuns:bad.example"), QStringLiteral("stun:good.example:3479")}, true);
        QCOMPARE(skipped.stunServer(),
                 std::optional<IceServerAddress>(IceServerAddress{QStringLiteral("good.example"), 3479}));
        QVERIFY(!IceConfiguration::throughRendezvous({}, true).stunServer());
    }

    // Fix wave I2: the STUN server and the one relay host follow this end's
    // address families, whichever order the service lists them in.
    void theServerFollowsThisEndsFamilies_data()
    {
        QTest::addColumn<QStringList>("stun");
        QTest::addColumn<QStringList>("turn");
        QTest::addColumn<bool>("ipv4");
        QTest::addColumn<bool>("ipv6");
        QTest::addColumn<QString>("expected");
        const QString rv4 = QStringLiteral("rv4.nereussdr.com");
        const QString rv6 = QStringLiteral("rv6.nereussdr.com");
        QTest::newRow("rv6 first, IPv4-only end") << kStun6First << kTurn6First << true << false << rv4;
        QTest::newRow("rv4 first, IPv4-only end") << kStun4First << kTurn4First << true << false << rv4;
        QTest::newRow("rv6 first, IPv6-only end") << kStun6First << kTurn6First << false << true << rv6;
        QTest::newRow("rv4 first, IPv6-only end") << kStun4First << kTurn4First << false << true << rv6;
        QTest::newRow("rv6 first, dual-stack end") << kStun6First << kTurn6First << true << true << rv6;
        QTest::newRow("rv4 first, dual-stack end") << kStun4First << kTurn4First << true << true << rv4;
        QTest::newRow("rv6 first, end that cannot tell") << kStun6First << kTurn6First << false << false << rv6;
        QTest::newRow("rv4 first, end that cannot tell") << kStun4First << kTurn4First << false << false << rv4;
    }

    void theServerFollowsThisEndsFamilies()
    {
        QFETCH(QStringList, stun);
        QFETCH(QStringList, turn);
        QFETCH(bool, ipv4);
        QFETCH(bool, ipv6);
        QFETCH(QString, expected);
        IceConfiguration ice =
            IceConfiguration::throughRendezvous(stun, true, AddressFamilies{ipv4, ipv6}, kResolved);
        QVERIFY(ice.stunServer().has_value());
        QCOMPARE(ice.stunServer()->host, expected);
        QCOMPARE(ice.setRelay(turnWith(turn)), 1);
        QCOMPARE(ice.relayServers().at(0).host, expected);
        // The first URL for that host.
        QCOMPARE(ice.relayServers().at(0).port, quint16(3478));
    }

    // Names that did not resolve cannot be told: the first entry, as
    // before. The relay's names may resolve later than the STUN names.
    void unresolvedNamesKeepTheFirstEntry()
    {
        IceConfiguration ice = IceConfiguration::throughRendezvous(kStun6First, true, kIpv4Only);
        QCOMPARE(ice.stunServer()->host, QStringLiteral("rv6.nereussdr.com"));
        QCOMPARE(ice.setRelay(turnWith(kTurn6First)), 1);
        QCOMPARE(ice.relayServers().at(0).host, QStringLiteral("rv6.nereussdr.com"));
        ice.addHostFamilies(kResolved);
        QCOMPARE(ice.setRelay(turnWith(kTurn6First)), 1);
        QCOMPARE(ice.relayServers().at(0).host, QStringLiteral("rv4.nereussdr.com"));

        // An IP literal needs no lookup.
        IceConfiguration literal = IceConfiguration::throughRendezvous(
            {QStringLiteral("stun:[2001:db8::7]:3478"), QStringLiteral("stun:203.0.113.7:3478")},
            true, kIpv4Only);
        QCOMPARE(literal.stunServer()->host, QStringLiteral("203.0.113.7"));
        // A resolver that answered with an IPv4-mapped address: IPv4.
        QCOMPARE(IceConfiguration::familiesOf({QHostAddress(QStringLiteral("::ffff:203.0.113.7"))}),
                 kIpv4Only);
    }

    void oneRelayServerUnlessBothFamiliesAreAskedFor()
    {
        IceConfiguration one = IceConfiguration::throughRendezvous(kStun, true, kIpv4Only, kResolved);
        QCOMPARE(one.setRelay(turnWith(kTurn)), 1);
        QVERIFY(one.relayKnown());
        QCOMPARE(one.relayServers().at(0).host, QStringLiteral("rv4.nereussdr.com"));
        QCOMPARE(one.setRelay(turnWith(kTurn), 0), 1);

        for (const QStringList& list : {kTurn4First, kTurn6First}) {
            IceConfiguration ice = IceConfiguration::throughRendezvous(kStun, true, kDualStack, kResolved);
            QCOMPARE(ice.setRelay(turnWith(list), 2), 2);
            QCOMPARE(ice.setRelay(turnWith(list), 3), 2);
            const QList<IceRelayServer> relays = ice.relayServers();
            QCOMPARE(relays.size(), 2);
            // One slot each for the IPv4-only and the IPv6-only name, never
            // two ports of one name, in either order.
            QStringList hosts{relays.at(0).host, relays.at(1).host};
            hosts.sort();
            QCOMPARE(hosts, (QStringList{QStringLiteral("rv4.nereussdr.com"),
                                         QStringLiteral("rv6.nereussdr.com")}));
            for (const IceRelayServer& relay : relays) {
                QCOMPARE(relay.port, quint16(3478));
                QCOMPARE(relay.username, turnWith(list).username);
                QCOMPARE(relay.password, turnWith(list).password);
            }
        }

        // Unusable URLs are passed over; a third host gets no slot.
        IceConfiguration mixed = IceConfiguration::throughRendezvous(kStun, true);
        QCOMPARE(mixed.setRelay(turnWith({QStringLiteral("turns:a.example:443"),
                                          QStringLiteral("turn:b.example:3478?transport=tcp"),
                                          QStringLiteral("turn:c.example:3478?transport=udp"),
                                          QStringLiteral("turn:d.example:443"),
                                          QStringLiteral("turn:e.example:3478")}),
                                 2),
                 2);
        QCOMPARE(mixed.relayServers().at(0).host, QStringLiteral("c.example"));
        QCOMPARE(mixed.relayServers().at(1).host, QStringLiteral("d.example"));
        QCOMPARE(mixed.relayServers().at(1).port, quint16(443));
    }

    // Which of this end's addresses count, and the names a list uses.
    void usableAddressesAndNames()
    {
        QVERIFY(IceConfiguration::isUsableLocalAddress(QHostAddress(QStringLiteral("192.168.1.20"))));
        QVERIFY(IceConfiguration::isUsableLocalAddress(QHostAddress(QStringLiteral("10.2.0.2"))));
        QVERIFY(IceConfiguration::isUsableLocalAddress(QHostAddress(QStringLiteral("2001:db8:2::2"))));
        QVERIFY(!IceConfiguration::isUsableLocalAddress(QHostAddress(QStringLiteral("127.0.0.1"))));
        QVERIFY(!IceConfiguration::isUsableLocalAddress(QHostAddress(QStringLiteral("169.254.3.4"))));
        QVERIFY(!IceConfiguration::isUsableLocalAddress(QHostAddress(QStringLiteral("::1"))));
        QVERIFY(!IceConfiguration::isUsableLocalAddress(QHostAddress(QStringLiteral("fe80::1"))));
        QVERIFY(!IceConfiguration::isUsableLocalAddress(QHostAddress(QStringLiteral("fd00::7"))));
        QVERIFY(!IceConfiguration::isUsableLocalAddress(QHostAddress()));

        QCOMPARE(IceConfiguration::hostNames(kStun6First + kTurn6First
                                             + QStringList{QStringLiteral("turn:[2001:db8::1]:3478"),
                                                           QStringLiteral("stun:198.51.100.2")}),
                 (QStringList{QStringLiteral("rv6.nereussdr.com"),
                              QStringLiteral("rv4.nereussdr.com")}));
    }

    // The resolver reports once; a test run asks it only about this
    // computer's own names, never rv.nereussdr.com.
    void namesAreResolvedOnThisComputerOnlyInATest()
    {
        QObject context;
        std::optional<HostFamilies> result;
        IceConfiguration::resolveHostFamilies(
            {QStringLiteral("localhost"), QStringLiteral("rv4.nereussdr.com"),
             QStringLiteral("203.0.113.7")},
            &context, [&result](const HostFamilies& families) {
                QVERIFY(!result.has_value());
                result = families;
            });
        QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), IceConfiguration::kHostLookupTimeoutMs + 2000);
        QVERIFY(!result->contains(QStringLiteral("rv4.nereussdr.com")));
        QVERIFY(!result->contains(QStringLiteral("203.0.113.7")));
        QVERIFY(result->value(QStringLiteral("localhost")).known());

        std::optional<HostFamilies> none;
        IceConfiguration::resolveHostFamilies(
            {QStringLiteral("rv6.nereussdr.com")}, &context,
            [&none](const HostFamilies& families) { none = families; });
        QVERIFY(!none.has_value());
        QTRY_VERIFY(none.has_value());
        QVERIFY(none->isEmpty());
    }

    void noRelayOfferedMeansKnownAndNone()
    {
        IceConfiguration ice = IceConfiguration::throughRendezvous(kStun, true);
        QCOMPARE(ice.setRelay(std::nullopt), 0);
        QVERIFY(ice.relayKnown());
        QVERIFY(ice.relayServers().isEmpty());
    }

    void relayDeniedMeansDirectOrNothing()
    {
        IceConfiguration ice = IceConfiguration::throughRendezvous(kStun, false);
        QVERIFY(!ice.relayAllowed());
        QCOMPARE(ice.setRelay(turnWith(kTurn), 2), 0);
        QVERIFY(ice.relayServers().isEmpty());
        const QString relay =
            QStringLiteral("candidate:3 1 UDP 16777215 203.0.113.9 50000 typ relay raddr 0.0.0.0 rport 0");
        const QString host = QStringLiteral("candidate:1 1 UDP 2122317823 2001:db8::7 50123 typ host");
        const QString srflx =
            QStringLiteral("candidate:2 1 UDP 1686052607 198.51.100.4 40000 typ srflx raddr 10.0.0.2 rport 40000");
        QVERIFY(!ice.acceptsRemoteCandidate(relay));
        QVERIFY(ice.acceptsRemoteCandidate(host));
        QVERIFY(ice.acceptsRemoteCandidate(srflx));

        const IceConfiguration allowed = IceConfiguration::throughRendezvous(kStun, true);
        QVERIFY(allowed.acceptsRemoteCandidate(relay));
        QVERIFY(!allowed.acceptsRemoteCandidate(QString()));
        QVERIFY(!allowed.acceptsRemoteCandidate(QStringLiteral("a=") + host));
        QCOMPARE(IceConfiguration::candidateType(relay), QStringLiteral("relay"));
        QCOMPARE(IceConfiguration::candidateType(srflx), QStringLiteral("srflx"));
        QCOMPARE(IceConfiguration::candidateType(QStringLiteral("candidate:1 1 UDP 1 h 1")), QString());
    }

    // A transport started through the remote access service offers no
    // candidates and gathers nothing until the relay is known; then it
    // gathers once, and says when it is done.
    void aTransportThroughTheServiceGathersOnlyWhenAsked()
    {
        LibDataChannelMediaTransport transport;
        QSignalSpy candidates(&transport, &IMediaTransport::localCandidate);
        QSignalSpy descriptions(&transport, &IMediaTransport::localDescription);
        QSignalSpy complete(&transport, &IMediaTransport::gatheringComplete);
        IMediaTransport::StartOptions options{IMediaTransport::Role::Offerer, 0x1234};
        // No STUN server: nothing may leave this computer.
        options.ice = IceConfiguration::throughRendezvous({}, true);
        QVERIFY(transport.start(options));
        QTRY_COMPARE(descriptions.size(), 1);
        QVERIFY(!descriptions.at(0).at(0).toString().contains(QLatin1String("a=candidate")));
        QTest::qWait(300);
        QCOMPARE(candidates.size(), 0);
        QCOMPARE(complete.size(), 0);

        QVERIFY(transport.gatherCandidates({}));
        QVERIFY(!transport.gatherCandidates({}));
        QTRY_VERIFY_WITH_TIMEOUT(complete.size() == 1, 10000);
        QVERIFY(candidates.size() >= 1);
        for (const QList<QVariant>& candidate : std::as_const(candidates)) {
            QCOMPARE(IceConfiguration::candidateType(candidate.at(0).toString()),
                     QStringLiteral("host"));
        }
        transport.stop();
    }

    // Without the settings, nothing changes: gathering starts by itself and
    // gatherCandidates() is refused.
    void aDirectTransportIsUnchanged()
    {
        LibDataChannelMediaTransport transport;
        QSignalSpy candidates(&transport, &IMediaTransport::localCandidate);
        QVERIFY(transport.start({IMediaTransport::Role::Offerer, 0x1234}));
        QVERIFY(!transport.gatherCandidates({}));
        QTRY_VERIFY_WITH_TIMEOUT(candidates.size() >= 1, 10000);
        transport.stop();
    }
};

QTEST_GUILESS_MAIN(TstIceConfiguration)
#include "tst_ice_configuration.moc"
