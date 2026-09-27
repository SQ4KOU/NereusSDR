// no-port-check: NereusSDR-original.
// =================================================================
// tests/tst_path_racer.cpp  (NereusSDR)
// =================================================================
//
// iPhone app plan Task 29 (R-IOS-16, R-IOS-08; the pairing design, section
// 5.4; the link document, section 21.1): a paired Core is raced on every
// path at once, the first path whose hello proves the Core wins, and a
// better path found later takes the session over.
//
//   - PathRacer with rungs of its own: the first ready rung wins; a better
//     one ready later is kept for the session; another computer answering
//     ends only its rung; with every rung ended the race says why, in the
//     rung's own words; an upgrade takes only a better rank; IPv6
//     addresses start at once and IPv4 ones kIpv4DelayMs later.
//   - A window and a Core on this computer, with the remote access
//     service (the real Python service and the fake STUN/TURN server):
//       * with every path open the direct path wins, and the record says
//         the service's path was not needed;
//       * with the Core's address closed the service's path wins, and the
//         record says what the address met;
//       * the session then moves to the Core's address once it opens,
//         with no new sign-in or snapshot;
//       * a Core that has the relay turned off is raced without it, and
//         the record says the Core turned it off;
//       * a Core that never answers its introduction ends the service's
//         path in plain words ("This Core can't be reached through the
//         internet service. Updating the Core may help."), and one that
//         recorded controlChannelVersion 0 is not tried there at all.
//
// Nothing here reaches beyond this computer: the service and the Core are
// on loopback, and the client's service list is the local service alone.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-27: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include <QtTest>

#include <QPointer>
#include <QSignalSpy>
#include <QTimer>

#include <memory>

#include "core/security/ClientDeviceIdentity.h"
#include "core/security/StationIdentity.h"
#include "core/session/PathRacer.h"
#include "core/session/RendezvousDialer.h"
#include "core/session/SessionMessages.h"
#include "core/session/StationClient.h"
#include "core/session/StationRendezvous.h"
#include "core/settings/SettingsProxy.h"
#include "fakes/LoopbackTransport.h"
#include "models/RadioModel.h"
#include "RendezvousTestHarness.h"

using namespace NereusSDR;
using namespace NereusSDR::Test::Rendezvous;
using NereusSDR::Test::LoopbackTransport;

namespace {

// A rung of the test's own: after `delayMs` it opens an in-process link
// whose far end sends a hello naming `peer` (the vetter below takes only
// "right core"), or it ends with `outcome` and `reason`.
class FakeRung final : public PathRung {
public:
    FakeRung(int rank, PathKind kind, const QString& address, int delayMs, const QString& peer,
             PathOutcome outcome = PathOutcome::Ready, const QString& reason = QString())
        : m_rank(rank), m_kind(kind), m_address(address), m_delayMs(delayMs), m_peer(peer),
          m_outcome(outcome), m_reason(reason)
    {
    }
    void start() override
    {
        started = true;
        QTimer::singleShot(m_delayMs, this, [this] {
            if (stopped) {
                return;
            }
            if (m_outcome != PathOutcome::Ready) {
                emit ended(m_outcome, m_reason);
                return;
            }
            auto* near = new LoopbackTransport(m_address);
            farEnd = new LoopbackTransport(QStringLiteral("core at ") + m_address, this);
            near->linkTo(farEnd);
            emit opened(near);
            farEnd->sendText(SessionMessages::encode(SessionMessages::hello(
                kSessionProtocolMajor, kSessionProtocolMinor, 0, m_peer,
                {kSessionProtocolMajor}, {})));
        });
    }
    void stop() override { stopped = true; }
    int rank() const override { return m_rank; }
    PathKind kind() const override { return m_kind; }
    QString address() const override { return m_address; }

    bool started = false;
    bool stopped = false;
    QPointer<LoopbackTransport> farEnd;

private:
    int m_rank;
    PathKind m_kind;
    QString m_address;
    int m_delayMs;
    QString m_peer;
    PathOutcome m_outcome;
    QString m_reason;
};

PathRacer::Vetter vetByPeerName()
{
    return [](const SessionMessage& hello, SessionTransport*) {
        return hello.peerName == QStringLiteral("right core");
    };
}

PathRacer::Outcome outcomeAt(const PathRacer& racer, const QString& address)
{
    for (const PathRacer::Line& line : racer.lines()) {
        if (line.address == address) {
            return line.outcome;
        }
    }
    return PathRacer::Outcome::Trying;
}

QByteArray identityOf(const Core& core)
{
    return StationIdentity::fingerprintOf(core.server->stationIdentity().publicKeySpki());
}

// A paired window of its own: a device key paired with `core`, and the
// service on this computer as its route to it.
struct Window {
    QTemporaryDir keyDir;
    std::shared_ptr<const ClientDeviceIdentity> key;
    RadioModel remote{RadioModel::Role::Remote};
    SettingsProxy proxy;
    std::unique_ptr<StationClient> client;

    explicit Window(Core& core)
    {
        key = std::make_shared<const ClientDeviceIdentity>(
            ClientDeviceIdentity::loadOrCreate(keyDir.path()));
        core.pairComputer(*key);
        client = std::make_unique<StationClient>(&remote, &proxy);
        client->setDeviceIdentity(key, QStringLiteral("Shack MacBook"));
        client->setHeartbeatIntervalMs(0);
    }

    void route(const LocalService& service, const QString& stationId, bool relayAllowed = true,
               int controlChannelVersion = 1)
    {
        StationClient::ServiceRoute route;
        route.servers = {service.url()};
        route.rendezvousId = stationId;
        route.relayAllowed = relayAllowed;
        route.controlChannelVersion = controlChannelVersion;
        client->setServiceRoute(route);
    }

    StationConnectionAttempt::Outcome outcomeFor(StationConnectionAttempt::Path path) const
    {
        for (const StationConnectionAttempt::Try& attempt : client->connectionAttempt().tries) {
            if (attempt.path == path) {
                return attempt.outcome;
            }
        }
        return StationConnectionAttempt::Outcome::Trying;
    }

    bool hasOutcome(StationConnectionAttempt::Outcome outcome) const
    {
        for (const StationConnectionAttempt::Try& attempt : client->connectionAttempt().tries) {
            if (attempt.outcome == outcome) {
                return true;
            }
        }
        return false;
    }
};

// A port nothing listens on, on this computer.
quint16 closedPort()
{
    return freeTcpPort();
}

} // namespace

class TstPathRacer final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        qRegisterMetaType<NereusSDR::PathOutcome>();
    }

    // ── The race alone ────────────────────────────────────────────────

    // The first rung to prove the Core wins, whatever its rank; a better
    // rung ready later is kept for the session to move to; one that never
    // answers ends as no answer.
    void theFirstReadyRungWinsAndABetterOneIsKept()
    {
        PathRacer racer;
        racer.setVetter(vetByPeerName());
        auto* relayed = new FakeRung(PathRacer::ServiceRelayed, PathKind::Relay,
                                     QStringLiteral("rv"), 10, QStringLiteral("right core"));
        auto* direct = new FakeRung(PathRacer::Direct, PathKind::Direct,
                                    QStringLiteral("203.0.113.7:47910"), 80,
                                    QStringLiteral("right core"));
        auto* lan = new FakeRung(PathRacer::ThisNetwork, PathKind::ThisNetwork,
                                 QStringLiteral("192.168.1.20:47910"), 20, QString(),
                                 PathOutcome::NoAnswer);
        racer.addRung(relayed);
        racer.addRung(direct);
        racer.addRung(lan);
        QSignalSpy won(&racer, &PathRacer::won);
        QSignalSpy better(&racer, &PathRacer::better);
        racer.start();
        QTRY_COMPARE(won.size(), 1);
        const auto winner = won.at(0).at(0).value<PathRacer::Ready>();
        QCOMPARE(winner.rank, int(PathRacer::ServiceRelayed));
        QVERIFY(winner.transport != nullptr);
        SessionMessage hello;
        QVERIFY(SessionMessages::decode(winner.hello, &hello));
        QCOMPARE(hello.kind, SessionMessageKind::Hello);
        QTRY_COMPARE(better.size(), 1);
        QCOMPARE(outcomeAt(racer, QStringLiteral("192.168.1.20:47910")),
                 PathRacer::Outcome::NoAnswer);
        racer.finish();
        const std::optional<PathRacer::Ready> standby = racer.takeStandby();
        QVERIFY(standby.has_value());
        QCOMPARE(standby->rank, int(PathRacer::Direct));
        QCOMPARE(standby->address, QStringLiteral("203.0.113.7:47910"));
        QVERIFY(standby->transport != nullptr);
        delete winner.transport.data();
        delete standby->transport.data();
    }

    // Another computer answering at an address ends that rung alone.
    void anotherComputerEndsOnlyItsRung()
    {
        PathRacer racer;
        racer.setVetter(vetByPeerName());
        racer.addRung(new FakeRung(PathRacer::ThisNetwork, PathKind::ThisNetwork,
                                   QStringLiteral("192.168.1.9:47910"), 5,
                                   QStringLiteral("some other core")));
        racer.addRung(new FakeRung(PathRacer::Direct, PathKind::Direct,
                                   QStringLiteral("203.0.113.7:47910"), 40,
                                   QStringLiteral("right core")));
        QSignalSpy won(&racer, &PathRacer::won);
        racer.start();
        QTRY_COMPARE(won.size(), 1);
        QCOMPARE(won.at(0).at(0).value<PathRacer::Ready>().address,
                 QStringLiteral("203.0.113.7:47910"));
        QCOMPARE(outcomeAt(racer, QStringLiteral("192.168.1.9:47910")),
                 PathRacer::Outcome::NotThisCore);
        delete won.at(0).at(0).value<PathRacer::Ready>().transport.data();
    }

    // Every rung ended: the race says why, in the most telling rung's own
    // words (a Core too old for the service), and the record keeps each.
    void everyRungEndedSaysWhy()
    {
        PathRacer racer;
        racer.setVetter(vetByPeerName());
        racer.addRung(new FakeRung(PathRacer::Direct, PathKind::Direct,
                                   QStringLiteral("203.0.113.7:47910"), 5, QString(),
                                   PathOutcome::NoAnswer));
        racer.addRung(new FakeRung(PathRacer::ServiceDirect, PathKind::Service,
                                   QStringLiteral("rv.example"), 10, QString(),
                                   PathOutcome::CoreTooOld,
                                   QString::fromLatin1(RendezvousDialer::kCoreTooOldReason)));
        racer.addNote(PathKind::Relay, QStringLiteral("rv.example"), PathOutcome::RelayOff,
                      QStringLiteral("The Core has the relay turned off."));
        QSignalSpy failed(&racer, &PathRacer::failed);
        racer.start();
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(failed.at(0).at(0).toString(),
                 QStringLiteral("This Core can't be reached through the internet service. "
                                "Updating the Core may help."));
        QCOMPARE(racer.lines().size(), 3);
        QCOMPARE(outcomeAt(racer, QStringLiteral("203.0.113.7:47910")),
                 PathRacer::Outcome::NoAnswer);
    }

    // An upgrade takes only a rank better than the session's: a rung no
    // better closes ("another path connected first").
    void anUpgradeTakesOnlyABetterRank()
    {
        PathRacer racer;
        racer.setVetter(vetByPeerName());
        racer.setBetterThan(PathRacer::ServiceDirect);
        racer.addRung(new FakeRung(PathRacer::ServiceRelayed, PathKind::Relay,
                                   QStringLiteral("rv"), 5, QStringLiteral("right core")));
        racer.addRung(new FakeRung(PathRacer::Direct, PathKind::Direct,
                                   QStringLiteral("203.0.113.7:47910"), 40,
                                   QStringLiteral("right core")));
        QSignalSpy won(&racer, &PathRacer::won);
        racer.start();
        QTRY_COMPARE(won.size(), 1);
        QCOMPARE(won.at(0).at(0).value<PathRacer::Ready>().rank, int(PathRacer::Direct));
        QCOMPARE(outcomeAt(racer, QStringLiteral("rv")), PathRacer::Outcome::Stopped);
        delete won.at(0).at(0).value<PathRacer::Ready>().transport.data();
    }

    // IPv6 first: an IPv6 address starts at once and an IPv4 one
    // kIpv4DelayMs later; with no IPv6 address, IPv4 starts at once; a
    // name's addresses are ordered the same way once it resolves.
    void ipv6StartsAtOnceAndIpv4Later()
    {
        {
            PathRacer racer;
            racer.addDirectUrls({QUrl(QStringLiteral("wss://192.0.2.1:9")),
                                 QUrl(QStringLiteral("wss://[2001:db8::1]:9"))},
                                StationClient::kMaxIncomingMessageBytes);
            const auto planned = racer.plannedStartsForTest();
            QCOMPARE(planned.size(), 2);
            QCOMPARE(planned.at(0), qMakePair(QStringLiteral("192.0.2.1:9"),
                                              PathRacer::kIpv4DelayMs));
            QCOMPARE(planned.at(1), qMakePair(QStringLiteral("[2001:db8::1]:9"), 0));
        }
        {
            PathRacer racer;
            racer.addDirectUrls({QUrl(QStringLiteral("wss://192.0.2.1:9"))},
                                StationClient::kMaxIncomingMessageBytes);
            QCOMPARE(racer.plannedStartsForTest().value(0).second, 0);
        }
        {
            PathRacer racer;
            QSignalSpy lines(&racer, &PathRacer::linesChanged);
            racer.addDirectUrls({QUrl(QStringLiteral("wss://localhost:9"))},
                                StationClient::kMaxIncomingMessageBytes);
            racer.start();
            QTRY_VERIFY(!racer.plannedStartsForTest().isEmpty());
            bool sawV4 = false;
            bool sawV6 = false;
            for (const auto& [address, delay] : racer.plannedStartsForTest()) {
                if (address.startsWith(QLatin1Char('['))) {
                    sawV6 = true;
                    QCOMPARE(delay, 0);
                } else {
                    sawV4 = true;
                    QCOMPARE(delay, sawV6 || racer.plannedStartsForTest().size() > 1
                                        ? PathRacer::kIpv4DelayMs
                                        : 0);
                }
            }
            QVERIFY(sawV4 || sawV6);
            racer.cancel();
        }
    }

    // ── A window and a Core on this computer ──────────────────────────

    // Every path open: the Core's address wins, and the service's path is
    // recorded as not needed.
    void withEveryPathOpenTheDirectPathWins()
    {
        LocalService service;
        QVERIFY(service.start());
        Core core;
        QVERIFY(core.server->listen(QHostAddress::LocalHost, 0));
        StationRendezvous rendezvous(core.server.get(), {service.url()}, /*relayAllowed=*/true);
        QSignalSpy registered(rendezvous.client(), &RendezvousClient::registered);
        QVERIFY(rendezvous.start());
        QTRY_COMPARE_WITH_TIMEOUT(registered.size(), 1, 10000);
        Window window(core);
        window.route(service, rendezvous.client()->stationId());
        window.client->connectToStation(core.url(), QString(), QString(), false,
                                        identityOf(core));
        QTRY_VERIFY_WITH_TIMEOUT(window.client->isHandshakeComplete(), 20000);
        QCOMPARE(window.client->pathRank(), int(PathRacer::ThisNetwork));
        QCOMPARE(window.outcomeFor(StationConnectionAttempt::Path::ThisNetwork),
                 StationConnectionAttempt::Outcome::Connected);
        // The service's path stopped once the Core's address won (or was
        // still on its way when it did).
        const StationConnectionAttempt::Outcome service_ =
            window.outcomeFor(StationConnectionAttempt::Path::Service);
        QVERIFY2(service_ == StationConnectionAttempt::Outcome::AnotherPathFirst,
                 qPrintable(window.client->connectionAttempt().summary()));
        QCOMPARE(window.client->stationRendezvousId(), rendezvous.client()->stationId());
        window.client->disconnectFromStation(QStringLiteral("test done"));
    }

    // The Core's address closed: the service's path wins, and the record
    // says what the address met. Then the Core's address opens, and the
    // session moves to it: no new sign-in, no snapshot.
    void withOnlyTheServiceTheServiceWinsThenMovesToTheAddress()
    {
        LocalService service;
        QVERIFY(service.start());
        Core core;
        const quint16 port = closedPort();
        StationRendezvous rendezvous(core.server.get(), {service.url()}, /*relayAllowed=*/true);
        QSignalSpy registered(rendezvous.client(), &RendezvousClient::registered);
        QVERIFY(rendezvous.start());
        QTRY_COMPARE_WITH_TIMEOUT(registered.size(), 1, 10000);
        Window window(core);
        window.route(service, rendezvous.client()->stationId());
        window.client->setUpgradeScheduleForTest({300});
        QSignalSpy handshakes(window.client.get(), &StationClient::handshakeComplete);
        QSignalSpy moved(window.client.get(), &StationClient::pathChanged);
        QSignalSpy authenticated(core.server.get(), &StationServer::clientAuthenticated);
        window.client->connectToStation(
            QUrl(QStringLiteral("wss://127.0.0.1:%1").arg(port)), QString(), QString(), false,
            identityOf(core));
        QTRY_VERIFY_WITH_TIMEOUT(window.client->isHandshakeComplete(), 60000);
        QVERIFY(window.client->pathRank() == int(PathRacer::ServiceDirect)
                || window.client->pathRank() == int(PathRacer::ServiceRelayed));
        QCOMPARE(window.outcomeFor(StationConnectionAttempt::Path::ThisNetwork),
                 StationConnectionAttempt::Outcome::NoAnswer);
        QVERIFY2(window.hasOutcome(StationConnectionAttempt::Outcome::Connected),
                 qPrintable(window.client->connectionAttempt().summary()));
        QCOMPARE(handshakes.size(), 1);
        QCOMPARE(authenticated.size(), 1);

        // The Core's address opens; the next look finds it.
        QVERIFY(core.server->listen(QHostAddress::LocalHost, port));
        QTRY_COMPARE_WITH_TIMEOUT(moved.size(), 1, 30000);
        QCOMPARE(window.client->pathRank(), int(PathRacer::ThisNetwork));
        QCOMPARE(window.client->pathSwitches(), 1);
        QCOMPARE(core.server->sessionsMoved(), 1);
        QVERIFY(window.client->isHandshakeComplete());
        QCOMPARE(handshakes.size(), 1);
        QCOMPARE(authenticated.size(), 1);
        QCOMPARE(window.client->connectedUrl(),
                 QUrl(QStringLiteral("wss://127.0.0.1:%1").arg(port)));
        window.client->disconnectFromStation(QStringLiteral("test done"));
    }

    // A Core with the relay turned off: the service's path runs without
    // it, and the record says the Core turned it off.
    void aCoreWithTheRelayOffIsRacedWithoutIt()
    {
        LocalService service;
        QVERIFY(service.start());
        Core core;
        StationRendezvous rendezvous(core.server.get(), {service.url()}, /*relayAllowed=*/false);
        core.server->setRelayAllowed(false);
        QSignalSpy registered(rendezvous.client(), &RendezvousClient::registered);
        QVERIFY(rendezvous.start());
        QTRY_COMPARE_WITH_TIMEOUT(registered.size(), 1, 10000);
        Window window(core);
        window.route(service, rendezvous.client()->stationId(), /*relayAllowed=*/false);
        window.client->connectToStation(
            QUrl(QStringLiteral("wss://127.0.0.1:%1").arg(closedPort())), QString(), QString(),
            false, identityOf(core));
        QTRY_VERIFY_WITH_TIMEOUT(window.client->isHandshakeComplete(), 60000);
        QCOMPARE(window.client->pathRank(), int(PathRacer::ServiceDirect));
        QVERIFY2(window.hasOutcome(StationConnectionAttempt::Outcome::RelayOff),
                 qPrintable(window.client->connectionAttempt().summary()));
        QVERIFY(window.client->connectionAttempt().summary().contains(
            QStringLiteral("the Core has the relay turned off")));
        // And the Core says so in its capabilities.
        QVERIFY(window.client->capabilities().relayAllowedEntry);
        QVERIFY(!window.client->capabilities().relayAllowed);
        window.client->disconnectFromStation(QStringLiteral("test done"));
    }

    // A Core that never answers its introduction ends the service's path
    // in plain words, and the race fails with them.
    void anOlderCoreIsToldInPlainWords()
    {
        LocalService service;
        QVERIFY(service.start());
        Core core;
        StationRendezvous rendezvous(core.server.get(), {service.url()}, /*relayAllowed=*/true);
        rendezvous.setAnswersIntroductionsForTest(false);
        QSignalSpy registered(rendezvous.client(), &RendezvousClient::registered);
        QVERIFY(rendezvous.start());
        QTRY_COMPARE_WITH_TIMEOUT(registered.size(), 1, 10000);
        Window window(core);
        window.route(service, rendezvous.client()->stationId());
        window.client->setServiceRungDeadlinesForTest(0, 1500);
        QSignalSpy ended(window.client.get(), &StationClient::sessionEnded);
        window.client->connectToStation(
            QUrl(QStringLiteral("wss://127.0.0.1:%1").arg(closedPort())), QString(), QString(),
            false, identityOf(core));
        QTRY_VERIFY_WITH_TIMEOUT(!ended.isEmpty(), 20000);
        QCOMPARE(ended.first().first().toString(),
                 QStringLiteral("This Core can't be reached through the internet service. "
                                "Updating the Core may help."));
        QVERIFY(window.hasOutcome(StationConnectionAttempt::Outcome::CoreTooOld));
        window.client->disconnectFromStation(QStringLiteral("test done"));
    }

    // A Core whose last session declared controlChannelVersion 0 is not
    // tried through the service at all; the record says why.
    void aCoreThatDeclaredNoControlChannelIsNotTriedThere()
    {
        Core core;
        LocalService service;
        QVERIFY(service.start());
        Window window(core);
        window.route(service, RendezvousWire::rendezvousId(
                                  core.server->stationIdentity().publicKeySpki()),
                     true, /*controlChannelVersion=*/0);
        QSignalSpy ended(window.client.get(), &StationClient::sessionEnded);
        window.client->connectToStation(
            QUrl(QStringLiteral("wss://127.0.0.1:%1").arg(closedPort())), QString(), QString(),
            false, identityOf(core));
        QTRY_VERIFY_WITH_TIMEOUT(!ended.isEmpty(), 20000);
        QCOMPARE(ended.first().first().toString(),
                 QString::fromLatin1(RendezvousDialer::kCoreTooOldReason));
        QCOMPARE(window.outcomeFor(StationConnectionAttempt::Path::Service),
                 StationConnectionAttempt::Outcome::CoreTooOld);
        window.client->disconnectFromStation(QStringLiteral("test done"));
    }
};

QTEST_MAIN(TstPathRacer)
#include "tst_path_racer.moc"
