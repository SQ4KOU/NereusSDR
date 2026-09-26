// no-port-check: NereusSDR-original.
// =================================================================
// tests/tst_ice_configuration.cpp  (NereusSDR)
// =================================================================
//
// iPhone app plan Task 27 (R-IOS-16): the ICE settings of a connection
// that came through the remote access service, shaped by the pinned
// libdatachannel v0.24.5 and libjuice @3c40a354 (IceConfiguration.h).
//
//   - One STUN server: the first the service's hello lists that this build
//     can use (the IPv4-only name on rv.nereussdr.com).
//   - One relay server by default, the first host the service lists, since
//     each allocation takes one of the relay's four slots for the Core; two
//     (one for each of the first two hosts, so the IPv4-only and the
//     IPv6-only name each take a slot) only when asked for; TURN over TCP
//     or TLS is never picked (libjuice speaks UDP only).
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

#include <QSignalSpy>

#include "core/session/IceConfiguration.h"
#include "core/session/media/LibDataChannelMediaTransport.h"

using namespace NereusSDR;

namespace {

// The NereusSDR server's lists (the rendezvous document, section 8), the
// IPv4-only name first as rv.nereussdr.com lists it.
const QStringList kStun{QStringLiteral("stun:rv4.nereussdr.com:3478"),
                        QStringLiteral("stun:rv6.nereussdr.com:3478")};
const QStringList kTurn{QStringLiteral("turn:rv4.nereussdr.com:3478?transport=udp"),
                        QStringLiteral("turn:rv4.nereussdr.com:443?transport=udp"),
                        QStringLiteral("turn:rv6.nereussdr.com:3478?transport=udp"),
                        QStringLiteral("turn:rv6.nereussdr.com:443?transport=udp")};

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
        const IceConfiguration ice = IceConfiguration::throughRendezvous(kStun, true);
        QCOMPARE(ice.stunServer(),
                 std::optional<IceServerAddress>(IceServerAddress{QStringLiteral("rv4.nereussdr.com"), 3478}));
        QVERIFY(!ice.relayKnown());
        QVERIFY(ice.relayServers().isEmpty());

        const IceConfiguration skipped = IceConfiguration::throughRendezvous(
            {QStringLiteral("stuns:bad.example"), QStringLiteral("stun:good.example:3479")}, true);
        QCOMPARE(skipped.stunServer(),
                 std::optional<IceServerAddress>(IceServerAddress{QStringLiteral("good.example"), 3479}));
        QVERIFY(!IceConfiguration::throughRendezvous({}, true).stunServer());
    }

    void oneRelayServerUnlessBothFamiliesAreAskedFor()
    {
        // One allocation by default: the first host the service lists.
        IceConfiguration one = IceConfiguration::throughRendezvous(kStun, true);
        QCOMPARE(one.setRelay(turnWith(kTurn)), 1);
        QVERIFY(one.relayKnown());
        QCOMPARE(one.relayServers().at(0).host, QStringLiteral("rv4.nereussdr.com"));
        QCOMPARE(one.relayServers().at(0).port, quint16(3478));
        QCOMPARE(one.setRelay(turnWith(kTurn), 0), 1);

        IceConfiguration ice = IceConfiguration::throughRendezvous(kStun, true);
        QCOMPARE(ice.setRelay(turnWith(kTurn), 2), 2);
        QCOMPARE(ice.setRelay(turnWith(kTurn), 3), 2);
        const QList<IceRelayServer> relays = ice.relayServers();
        QCOMPARE(relays.size(), 2);
        // One slot each for the IPv4-only and the IPv6-only name, never two
        // ports of one name.
        QCOMPARE(relays.at(0).host, QStringLiteral("rv4.nereussdr.com"));
        QCOMPARE(relays.at(0).port, quint16(3478));
        QCOMPARE(relays.at(1).host, QStringLiteral("rv6.nereussdr.com"));
        QCOMPARE(relays.at(1).port, quint16(3478));
        for (const IceRelayServer& relay : relays) {
            QCOMPARE(relay.username, turnWith(kTurn).username);
            QCOMPARE(relay.password, turnWith(kTurn).password);
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
