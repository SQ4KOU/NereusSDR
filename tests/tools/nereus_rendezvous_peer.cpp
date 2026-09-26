// no-port-check: NereusSDR-original.
// =================================================================
// tests/tools/nereus_rendezvous_peer.cpp  (NereusSDR)
// =================================================================
//
// iPhone app plan Task 27 (R-IOS-16): one end of a connection through the
// remote access service, for the traversal harness
// (tests/scripts/traversal-harness.sh), which runs it in network namespaces
// behind different NATs and firewalls. It uses the Core's own pieces:
// RendezvousClient, IceConfiguration and LibDataChannelMediaTransport.
//
//   nereus_rendezvous_peer key --dir DIR
//       Prints the public key (base64url SPKI) of the key in DIR, making
//       it on first use.
//   nereus_rendezvous_peer station --dir DIR --server URL --paired KEY
//                          [--relay deny] [--id-file FILE] [--ca FILE]
//       Registers with the service under DIR's key and answers
//       introductions from the device whose public key is KEY (the only
//       paired device); echoes every message back. Writes its rendezvous id
//       to FILE once registered. Runs until killed.
//   nereus_rendezvous_peer client --dir DIR --server URL --station-id ID
//                          [--send-bytes N] [--timeout-ms T] [--ca FILE]
//       Introduces itself with DIR's key, connects, sends one message of N
//       bytes (default 60000, many 1000-byte datagrams) and waits for it to
//       come back. Prints one JSON line
//       {"connected":bool,"echoed":bool,"relayed":bool,"localType",
//        "remoteType","localAddress","remoteAddress","ms","reason"} and
//       exits 0 when the message came back, 1 otherwise.
//
// --ca adds a certificate authority the harness made at run time, so the
// service's TLS (a test certificate for its test name) verifies. Nothing
// secret is printed; keys stay in DIR.
//
// It is not linked with the test sandbox: it talks to a service in another
// namespace, which a test-mode RendezvousClient refuses to reach.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-26: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
//   2026-09-26: Task 27 follow-up (new Minor 2): the station answers an
//               introduction only once its STUN names are resolved. J.J.
//               Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QTextStream>
#include <QTimer>

#include <cstdio>
#include <memory>

#include "core/security/StationIdentity.h"
#include "core/session/IceConfiguration.h"
#include "core/session/RendezvousClient.h"
#include "core/session/RendezvousWire.h"
#include "core/session/media/LibDataChannelMediaTransport.h"

#include "StunLookupGate.h"

using namespace NereusSDR;

namespace {

QString option(const QStringList& args, const QString& name, const QString& fallback = QString())
{
    const qsizetype index = args.indexOf(name);
    return index >= 0 && index + 1 < args.size() ? args.at(index + 1) : fallback;
}

void printLine(const QJsonObject& object)
{
    std::fputs(QJsonDocument(object).toJson(QJsonDocument::Compact).constData(), stdout);
    std::fputs("\n", stdout);
    std::fflush(stdout);
}

bool addAuthority(const QString& path)
{
    if (path.isEmpty()) {
        return true;
    }
    const QList<QSslCertificate> authorities = QSslCertificate::fromPath(path);
    if (authorities.isEmpty()) {
        return false;
    }
    QSslConfiguration configuration = QSslConfiguration::defaultConfiguration();
    configuration.addCaCertificates(authorities);
    QSslConfiguration::setDefaultConfiguration(configuration);
    return true;
}

QJsonObject pathObject(const IMediaTransport& transport)
{
    QJsonObject object;
    if (const auto path = transport.selectedPath()) {
        object.insert(QStringLiteral("relayed"), path->relayed());
        object.insert(QStringLiteral("localType"), path->localType);
        object.insert(QStringLiteral("remoteType"), path->remoteType);
        object.insert(QStringLiteral("localAddress"), path->localAddress);
        object.insert(QStringLiteral("remoteAddress"), path->remoteAddress);
    }
    return object;
}

int runStation(const QStringList& args)
{
    const StationIdentity key = StationIdentity::loadOrCreate(option(args, QStringLiteral("--dir")));
    bool ok = false;
    const QByteArray paired = StationIdentity::fromBase64Url(option(args, QStringLiteral("--paired")), &ok);
    if (!key.isValid() || !ok || !StationIdentity::isP256Spki(paired)) {
        std::fputs("station: a key is not usable\n", stderr);
        return 2;
    }
    const bool relayAllowed = option(args, QStringLiteral("--relay"), QStringLiteral("allow"))
                              != QLatin1String("deny");
    auto* client = new RendezvousClient(QCoreApplication::instance());
    client->setServers(RendezvousClient::serverUrls({option(args, QStringLiteral("--server"))}));
    client->setRelayAllowed(relayAllowed);
    const QString idFile = option(args, QStringLiteral("--id-file"));
    QObject::connect(client, &RendezvousClient::registered, client, [client, idFile] {
        if (!idFile.isEmpty()) {
            QFile file(idFile + QStringLiteral(".part"));
            if (file.open(QIODevice::WriteOnly)) {
                file.write(client->stationId().toLatin1());
                file.close();
                QFile::remove(idFile);
                file.rename(idFile);
            }
        }
        printLine({{QStringLiteral("event"), QStringLiteral("registered")}});
    });
    // Each introduction is answered once the STUN names the hello lists
    // are resolved, so it chooses its STUN server by this end's address
    // families (IceConfiguration, fix wave I2). RendezvousClient registers
    // as the hello arrives, so an introduction that comes during the lookup
    // waits for it (StunLookupGate, the follow-up to the re-review, new
    // Minor 2) rather than choosing from families not yet known.
    auto gate = std::make_shared<Test::StunLookupGate>(
        [client, relayAllowed](const RendezvousIntroduction& introduction,
                               const HostFamilies& stunFamilies) {
        const QByteArray id = introduction.id;
        auto ice = std::make_shared<IceConfiguration>(IceConfiguration::throughRendezvous(
            client->stunUrls(), relayAllowed, IceConfiguration::localAddressFamilies(),
            stunFamilies));
        auto* answerer = new LibDataChannelMediaTransport(client);
        QObject::connect(answerer, &IMediaTransport::localDescription, client,
                         [client, answerer, id, relayAllowed](const QString& sdp, const QString&) {
            client->answer(id, sdp);
            if (!relayAllowed) {
                answerer->gatherCandidates({});
            }
        });
        QObject::connect(answerer, &IMediaTransport::localCandidate, client,
                         [client, id](const QString& candidate, const QString&) {
            client->sendCandidate(id, candidate);
        });
        QObject::connect(answerer, &IMediaTransport::gatheringComplete, client,
                         [client, id] { client->sendCandidate(id, QString()); });
        QObject::connect(client, &RendezvousClient::credentialsReceived, answerer,
                         [answerer, ice, id](const QByteArray& from, bool offered,
                                             const RendezvousWire::Turn& turn) {
            if (from != id) {
                return;
            }
            const std::optional<RendezvousWire::Turn> relay =
                offered ? std::optional<RendezvousWire::Turn>(turn) : std::nullopt;
            IceConfiguration::resolveHostFamilies(
                relay ? IceConfiguration::hostNames(relay->urls) : QStringList(), answerer,
                [answerer, ice, relay](const HostFamilies& families) {
                    ice->addHostFamilies(families);
                    ice->setRelay(relay, 1);
                    answerer->gatherCandidates(ice->relayServers());
                });
        });
        QObject::connect(client, &RendezvousClient::candidateReceived, answerer,
                         [answerer, id](const QByteArray& from, const QString& candidate) {
            if (from == id && !candidate.isEmpty()) {
                answerer->acceptCandidate(candidate, QString());
            }
        });
        QObject::connect(answerer, &IMediaTransport::ready, client, [answerer] {
            QJsonObject event = pathObject(*answerer);
            event.insert(QStringLiteral("event"), QStringLiteral("connected"));
            printLine(event);
        });
        QObject::connect(answerer, &IMediaTransport::displayReceived, answerer,
                         [answerer](const QByteArray& message) { answerer->sendDisplay(message); });
        IMediaTransport::StartOptions options{IMediaTransport::Role::Answerer, 0x5a5a};
        options.ice = *ice;
        if (!answerer->start(options)
            || !answerer->acceptDescription(introduction.offer, QStringLiteral("offer"))) {
            printLine({{QStringLiteral("event"), QStringLiteral("refused")}});
        }
    });
    QObject::connect(client, &RendezvousClient::connected, client, [client, gate] {
        const quint64 lookup = gate->lookupStarted();
        IceConfiguration::resolveHostFamilies(
            IceConfiguration::hostNames(client->stunUrls()), client,
            [gate, lookup](const HostFamilies& families) {
                gate->lookupFinished(lookup, families);
            });
    });
    QObject::connect(client, &RendezvousClient::introduced, client,
                     [gate](const RendezvousIntroduction& introduction) {
        gate->introduce(introduction);
    });
    client->registerStation(key.publicKeySpki(),
                            [key](const QByteArray& message) { return key.sign(message); },
                            [paired](const QByteArray& deviceId) {
                                return StationIdentity::fingerprintOf(paired) == deviceId
                                           ? paired
                                           : QByteArray();
                            });
    return QCoreApplication::exec();
}

int runClient(const QStringList& args)
{
    const StationIdentity key = StationIdentity::loadOrCreate(option(args, QStringLiteral("--dir")));
    const QString stationId = option(args, QStringLiteral("--station-id"));
    const int sendBytes = option(args, QStringLiteral("--send-bytes"), QStringLiteral("60000")).toInt();
    const int timeoutMs = option(args, QStringLiteral("--timeout-ms"), QStringLiteral("90000")).toInt();
    if (!key.isValid() || !RendezvousWire::isRendezvousId(stationId) || sendBytes < 1
        || sendBytes > IMediaTransport::kMaxDisplayMessageBytes) {
        std::fputs("client: an argument is not usable\n", stderr);
        return 2;
    }
    QElapsedTimer clock;
    clock.start();
    auto* client = new RendezvousClient(QCoreApplication::instance());
    client->setServers(RendezvousClient::serverUrls({option(args, QStringLiteral("--server"))}));
    auto ice = std::make_shared<IceConfiguration>();
    auto* offerer = new LibDataChannelMediaTransport(client);
    QByteArray message(sendBytes, Qt::Uninitialized);
    for (int index = 0; index < sendBytes; ++index) {
        message[index] = static_cast<char>(index * 31 + 7);
    }
    auto done = std::make_shared<bool>(false);
    const auto finish = [offerer, &clock, done](bool connected, bool echoed, const QString& reason) {
        if (*done) {
            return;
        }
        *done = true;
        QJsonObject result = pathObject(*offerer);
        result.insert(QStringLiteral("connected"), connected);
        result.insert(QStringLiteral("echoed"), echoed);
        result.insert(QStringLiteral("ms"), static_cast<double>(clock.elapsed()));
        if (!reason.isEmpty()) {
            result.insert(QStringLiteral("reason"), reason);
        }
        if (!result.contains(QStringLiteral("relayed"))) {
            result.insert(QStringLiteral("relayed"), false);
        }
        printLine(result);
        QCoreApplication::exit(echoed ? 0 : 1);
    };
    QObject::connect(client, &RendezvousClient::connected, client, [client, offerer, ice, &key,
                                                                      stationId] {
        // The STUN server chosen by this end's address families, once the
        // names are resolved (fix wave I2).
        IceConfiguration::resolveHostFamilies(
            IceConfiguration::hostNames(client->stunUrls()), client,
            [client, offerer, ice, &key, stationId](const HostFamilies& families) {
                *ice = IceConfiguration::throughRendezvous(
                    client->stunUrls(), true, IceConfiguration::localAddressFamilies(), families);
                QObject::connect(offerer, &IMediaTransport::localDescription, client,
                                 [client, &key, stationId](const QString& sdp, const QString&) {
                    client->introduce(stationId, key.publicKeySpki(),
                                      [&key](const QByteArray& m) { return key.sign(m); }, sdp);
                });
                IMediaTransport::StartOptions options{IMediaTransport::Role::Offerer, 0xa5a5};
                options.ice = *ice;
                offerer->start(options);
            });
    });
    QObject::connect(client, &RendezvousClient::answerReceived, offerer,
                     [offerer, ice](const QString& sdp, bool offered, const RendezvousWire::Turn& turn) {
        offerer->acceptDescription(sdp, QStringLiteral("answer"));
        const std::optional<RendezvousWire::Turn> relay =
            offered ? std::optional<RendezvousWire::Turn>(turn) : std::nullopt;
        IceConfiguration::resolveHostFamilies(
            relay ? IceConfiguration::hostNames(relay->urls) : QStringList(), offerer,
            [offerer, ice, relay](const HostFamilies& families) {
                ice->addHostFamilies(families);
                ice->setRelay(relay, 1);
                offerer->gatherCandidates(ice->relayServers());
            });
    });
    QObject::connect(offerer, &IMediaTransport::localCandidate, client,
                     [client](const QString& candidate, const QString&) {
        client->sendCandidate(candidate);
    });
    QObject::connect(offerer, &IMediaTransport::gatheringComplete, client,
                     [client] { client->sendCandidate(QString()); });
    QObject::connect(client, &RendezvousClient::candidateReceived, offerer,
                     [offerer](const QByteArray&, const QString& candidate) {
        if (!candidate.isEmpty()) {
            offerer->acceptCandidate(candidate, QString());
        }
    });
    QObject::connect(client, &RendezvousClient::unreachable, offerer,
                     [finish](const QString& reason) { finish(false, false, reason); });
    QObject::connect(offerer, &IMediaTransport::connectionFailed, offerer,
                     [finish](const QString&) {
        finish(false, false, QStringLiteral("The connection could not be made."));
    });
    QObject::connect(offerer, &IMediaTransport::ready, offerer, [offerer, message] {
        offerer->sendDisplay(message);
    });
    QObject::connect(offerer, &IMediaTransport::displayReceived, offerer,
                     [finish, message](const QByteArray& echoed) {
        finish(true, echoed == message, echoed == message ? QString()
                                                          : QStringLiteral("The echo differed."));
    });
    QTimer::singleShot(timeoutMs, offerer, [finish, offerer] {
        finish(offerer->isReady(), false, QStringLiteral("No echo in time."));
    });
    client->connectToService();
    return QCoreApplication::exec();
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    const QString mode = args.value(1);
    if (!addAuthority(option(args, QStringLiteral("--ca")))) {
        std::fputs("the certificate authority could not be read\n", stderr);
        return 2;
    }
    if (mode == QLatin1String("key")) {
        const StationIdentity key = StationIdentity::loadOrCreate(option(args, QStringLiteral("--dir")));
        if (!key.isValid()) {
            return 2;
        }
        QTextStream(stdout) << StationIdentity::toBase64Url(key.publicKeySpki()) << "\n";
        return 0;
    }
    if (mode == QLatin1String("station")) {
        return runStation(args);
    }
    if (mode == QLatin1String("client")) {
        return runClient(args);
    }
    std::fputs("usage: nereus_rendezvous_peer key|station|client ...\n", stderr);
    return 2;
}
