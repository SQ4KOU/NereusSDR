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
//   nereus_rendezvous_peer device-key --dir DIR
//       Prints the public key (base64url SPKI) of a desktop's device key in
//       DIR (ClientDeviceIdentity), making it on first use.
//   nereus_rendezvous_peer core --dir DIR --server URL --paired KEY
//                          [--relay deny] [--id-file FILE] [--ca FILE]
//       Plan Task 28: a Core, as nereusd runs it (a StationServer with no
//       radio, and StationRendezvous), with the desktop whose device key is
//       KEY paired. It answers introductions with a control connection and
//       runs the session over it. Writes "<rendezvous id> <identity
//       fingerprint, base64url>" to FILE once registered. Runs until
//       killed.
//   nereus_rendezvous_peer session --dir DIR --server URL --core FILE
//                          [--timeout-ms T] [--ca FILE]
//       Plan Task 28: a desktop (StationClient::connectThroughService) with
//       DIR's device key reaches the Core named in FILE through the service
//       and runs the whole connect sequence. Prints one JSON line
//       {"connected":bool,"relayed":bool,"localType","remoteType",
//        "localAddress","remoteAddress","ms","reason"} and
//       exits 0 when the session was established, 1 otherwise.
//
// Plan Task 29 (R-IOS-16; the link document, section 21):
//   core ... [--listen PORT] [--media]
//       --listen: the Core's WebSocket on every address at PORT, for the
//       direct path. --media: media on (a DaemonMediaController), a tone in
//       the Core's audio and synthetic I/Q in its display source, so a
//       session's media carries real Opus audio and real display frames.
//   session ... [--direct URL] [--upgrade-schedule-ms "A,B"]
//               [--wait-upgrade-ms T] [--media-ms M]
//       --direct: the race (StationClient::connectToStation for a paired
//       Core) with URL and the service at once, instead of the service
//       alone. Prints {"event":"connected",...} as soon as the session is
//       up. --wait-upgrade-ms: then waits up to T for the session to move
//       to a better path (--upgrade-schedule-ms sets the looks).
//       --media-ms: then runs media for M ms (a MediaPeer answering the
//       Core's offer, audio on, one display) and counts what decoded. The
//       last line adds "rank", "path", "attempt", "moved", "rankAfter",
//       "handshakes", "switches", and with media "audioPackets",
//       "audioDecoded", "displayMessages", "displayDecoded".
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
//   2026-09-26: iPhone app plan Task 28 (R-IOS-16): the device-key, core and
//               session modes, a whole session over an introduced control
//               connection. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//               Claude Code.
//   2026-09-27: iPhone app plan Task 29 (R-IOS-16): the race, a move to a
//               better path, and real media through the service (core
//               --listen and --media; session --direct, --wait-upgrade-ms
//               and --media-ms). J.J. Boyd (KG4VCF), AI-assisted via
//               Anthropic Claude Code.
// =================================================================

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QUuid>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QTextStream>
#include <QTimer>

#include <cmath>
#include <cstdio>
#include <memory>

#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/ConnectionState.h"
#include "core/HpsdrModel.h"
#include "core/security/ClientDeviceIdentity.h"
#include "core/security/DeviceStore.h"
#include "core/security/StationIdentity.h"
#include "core/session/DataChannelTransport.h"
#include "core/session/IceConfiguration.h"
#include "core/session/RendezvousClient.h"
#include "core/session/RendezvousWire.h"
#include "core/session/StationClient.h"
#include "core/session/StationRendezvous.h"
#include "core/session/StationServer.h"
#include "core/session/media/DaemonMediaController.h"
#include "core/session/media/DisplayCodec.h"
#include "core/session/media/LibDataChannelMediaTransport.h"
#include "core/session/media/MediaPeer.h"
#include "core/session/media/OpusAudioCodec.h"
#include "models/SliceModel.h"
#include "core/settings/SettingsProxy.h"
#include "models/RadioModel.h"

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

// Plan Task 28: a Core as nereusd runs it, for a session over an introduced
// control connection.
int runCore(const QStringList& args)
{
    const QString dir = option(args, QStringLiteral("--dir"));
    bool ok = false;
    const QByteArray paired = StationIdentity::fromBase64Url(option(args, QStringLiteral("--paired")), &ok);
    if (dir.isEmpty() || !ok || !StationIdentity::isP256Spki(paired)) {
        std::fputs("core: an argument is not usable\n", stderr);
        return 2;
    }
    const bool relayAllowed = option(args, QStringLiteral("--relay"), QStringLiteral("allow"))
                              != QLatin1String("deny");
    auto* settings = new AppSettings(dir + QStringLiteral("/NereusSDR.settings"));
    settings->setValue(QStringLiteral("StationCallsign"), QStringLiteral("N0CALL"));
    auto* model = new RadioModel();
    auto* server = new StationServer(model, *settings, dir + QStringLiteral("/security"),
                                     QCoreApplication::instance());
    PairedDevice device;
    device.id = StationIdentity::fingerprintOf(paired);
    device.publicKeySpki = paired;
    device.name = QStringLiteral("Harness desktop");
    device.kind = QStringLiteral("computer");
    if (server->deviceStore() == nullptr
        || (!server->deviceStore()->find(device.id) && !server->deviceStore()->add(device))) {
        std::fputs("core: the desktop could not be paired\n", stderr);
        return 2;
    }
    auto* rendezvous = new StationRendezvous(
        server, RendezvousClient::serverUrls({option(args, QStringLiteral("--server"))}),
        relayAllowed, QCoreApplication::instance());
    const QString idFile = option(args, QStringLiteral("--id-file"));
    QObject::connect(rendezvous->client(), &RendezvousClient::registered, server,
                     [rendezvous, server, idFile] {
        if (!idFile.isEmpty()) {
            QFile file(idFile + QStringLiteral(".part"));
            if (file.open(QIODevice::WriteOnly)) {
                file.write(rendezvous->client()->stationId().toLatin1() + ' '
                           + StationIdentity::toBase64Url(server->stationIdentity().fingerprint())
                                 .toLatin1());
                file.close();
                QFile::remove(idFile);
                file.rename(idFile);
            }
        }
        printLine({{QStringLiteral("event"), QStringLiteral("registered")}});
    });
    QObject::connect(server, &StationServer::clientAuthenticated, server,
                     [](const QString& peer) {
        printLine({{QStringLiteral("event"), QStringLiteral("session")},
                   {QStringLiteral("peer"), peer}});
    });
    if (!rendezvous->start()) {
        std::fputs("core: the remote access service could not be used\n", stderr);
        return 2;
    }
    // Plan Task 29: the direct path, and real media.
    server->setRelayAllowed(relayAllowed);
    const QString listen = option(args, QStringLiteral("--listen"));
    if (!listen.isEmpty()
        && !server->listen(QHostAddress::Any, static_cast<quint16>(listen.toUInt()))) {
        std::fputs("core: could not listen\n", stderr);
        return 2;
    }
    if (args.contains(QStringLiteral("--media"))) {
        model->setBoardForTest(HPSDRHW::Saturn);
        model->configureStreamPool(/*userDdcCount=*/5, /*maxSlices=*/5,
                                   /*defaultRateHz=*/192000);
        model->setConnectionStateForTest(ConnectionState::Connected);
        const int slice = model->addSlice();
        AudioEngine* engine = model->audioEngine();
        engine->setSliceStreaming(slice, true);
        server->setMediaEnabled(true);
        new DaemonMediaController(server, model, QCoreApplication::instance());
        // A 700 Hz tone in the Core's audio, 480 frames every 10 ms.
        auto* tone = new QTimer(QCoreApplication::instance());
        tone->setTimerType(Qt::PreciseTimer);
        auto frames = std::make_shared<qint64>(0);
        QObject::connect(tone, &QTimer::timeout, engine, [engine, slice, frames] {
            constexpr int kFrames = 480;
            QVector<float> block(kFrames * 2);
            for (int frame = 0; frame < kFrames; ++frame) {
                const double time = static_cast<double>(*frames + frame) / 48000.0;
                const float value = static_cast<float>(0.25 * std::sin(2.0 * M_PI * 700.0 * time));
                block[frame * 2] = block[frame * 2 + 1] = value;
            }
            *frames += kFrames;
            engine->rxBlockReady(slice, block.constData(), kFrames);
        });
        tone->start(10);
        // Synthetic I/Q for the display, through RadioModel's own tap.
        auto* iq = new QTimer(QCoreApplication::instance());
        QObject::connect(iq, &QTimer::timeout, model, [model, slice] {
            const SliceModel* s = model->sliceById(slice);
            if (s == nullptr || s->streamIndex() < 0) {
                return;
            }
            QVector<float> samples;
            samples.reserve(1026 * 2);
            for (int n = 0; n < 1026; ++n) {
                const double phase = 2.0 * M_PI * 0.125 * n;
                samples.append(static_cast<float>(std::cos(phase)));
                samples.append(static_cast<float>(std::sin(phase)));
            }
            QMetaObject::invokeMethod(model, "rawIqDataForStream", Qt::DirectConnection,
                                      Q_ARG(int, s->streamIndex()),
                                      Q_ARG(QVector<float>, samples));
        });
        iq->start(5);
    }
    return QCoreApplication::exec();
}

// Plan Task 29: the path the session runs on, in the harness's words.
QString pathName(int rank)
{
    switch (rank) {
    case PathRacer::ThisNetwork: return QStringLiteral("thisNetwork");
    case PathRacer::Direct: return QStringLiteral("direct");
    case PathRacer::ServiceDirect: return QStringLiteral("service");
    case PathRacer::ServiceRelayed: return QStringLiteral("relay");
    default: return QStringLiteral("none");
    }
}

// Plan Task 29: real media over a session, counting what decodes: a
// MediaPeer answering the Core's offer, audio on, one display endpoint.
class MediaCounter : public QObject {
public:
    MediaCounter(StationClient* window, RadioModel* model, QObject* parent)
        : QObject(parent), m_window(window), m_model(model)
    {
        m_connectionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_peer = new MediaPeer(this);
        QObject::connect(m_peer, &MediaPeer::controlReady, this, [this](const QJsonObject& c) {
            m_window->sendMediaControl(c, m_window->sessionEpoch());
        });
        QObject::connect(m_window, &StationClient::mediaControlReceived, this,
                         [this](const QJsonObject& payload, quint32) { receive(payload); });
        QObject::connect(m_peer, &MediaPeer::ready, this, [this] { onReady(); });
        QObject::connect(m_peer, &MediaPeer::rtpReceived, this, [this](const QByteArray& packet) {
            ++audioPackets;
            if (m_audioSsrc != 0
                && m_decoder.decodeRtp(packet, m_audioSsrc).status
                    == OpusAudioCodecStatus::Accepted) {
                ++audioDecoded;
            }
        });
        QObject::connect(m_peer, &MediaPeer::displayReceived, this,
                         [this](const QByteArray& packet) {
            ++displayMessages;
            if (packet.startsWith("NSDC")
                && m_display.decode(packet).disposition == DisplayCodecDisposition::Accepted) {
                ++displayDecoded;
            }
        });
    }

    bool start()
    {
        m_peer->setIceConfiguration(m_window->sessionIceConfiguration());
        if (!m_peer->start(IMediaTransport::Role::Answerer, m_connectionId)) {
            return false;
        }
        return m_window->sendMediaControl({{QStringLiteral("op"), QStringLiteral("start")},
                                           {QStringLiteral("connectionId"), m_connectionId}},
                                          m_window->sessionEpoch());
    }

    quint64 audioPackets = 0;
    quint64 audioDecoded = 0;
    quint64 displayMessages = 0;
    quint64 displayDecoded = 0;

private:
    void receive(const QJsonObject& payload)
    {
        if (payload.value(QStringLiteral("connectionId")).toString() != m_connectionId) {
            return;
        }
        const QString op = payload.value(QStringLiteral("op")).toString();
        if (op == QLatin1String("description") || op == QLatin1String("candidate")) {
            m_peer->acceptControl(payload);
        } else if (op == QLatin1String("audio-context")
                   && payload.value(QStringLiteral("enabled")).toBool()) {
            m_audioSsrc = static_cast<quint32>(payload.value(QStringLiteral("ssrc")).toDouble());
        }
    }

    void onReady()
    {
        const auto send = [this](QJsonObject control) {
            control.insert(QStringLiteral("connectionId"), m_connectionId);
            m_window->sendMediaControl(control, m_window->sessionEpoch());
        };
        send({{QStringLiteral("op"), QStringLiteral("audio")},
              {QStringLiteral("revision"), 1},
              {QStringLiteral("enabled"), true}});
        const QList<SliceModel*> slices = m_model->slices();
        if (slices.isEmpty()) {
            return;
        }
        const QJsonObject plane{{QStringLiteral("detector"), 0},
                                {QStringLiteral("averageMode"), -1},
                                {QStringLiteral("averageAlpha"), 0.0}};
        send({{QStringLiteral("op"), QStringLiteral("subscribe")},
              {QStringLiteral("endpointId"), 1},
              {QStringLiteral("revision"), 1},
              {QStringLiteral("sliceId"), slices.first()->sliceIndex()},
              {QStringLiteral("tier"), QStringLiteral("wide")},
              {QStringLiteral("fftSize"), 1024},
              {QStringLiteral("windowType"), 0},
              {QStringLiteral("centreHz"), slices.first()->frequency()},
              {QStringLiteral("spanHz"), 48000.0},
              {QStringLiteral("pixels"), 128},
              {QStringLiteral("fps"), 30},
              {QStringLiteral("framesPerLine"), 1},
              {QStringLiteral("trace"), plane},
              {QStringLiteral("waterfall"), plane},
              {QStringLiteral("minDbm"), -180.0},
              {QStringLiteral("maxDbm"), 0.0},
              {QStringLiteral("wideSpanFactor"), 0.0}});
    }

    StationClient* m_window;
    RadioModel* m_model;
    MediaPeer* m_peer = nullptr;
    QString m_connectionId;
    quint32 m_audioSsrc = 0;
    OpusAudioDecoder m_decoder;
    DisplayCodecDecoder m_display;
};

// Plan Task 28: a desktop reaching the Core through the service.
int runSession(const QStringList& args)
{
    const QString dir = option(args, QStringLiteral("--dir"));
    const int timeoutMs = option(args, QStringLiteral("--timeout-ms"), QStringLiteral("120000")).toInt();
    QFile coreFile(option(args, QStringLiteral("--core")));
    const QStringList core = coreFile.open(QIODevice::ReadOnly)
        ? QString::fromLatin1(coreFile.readAll()).split(QLatin1Char(' '))
        : QStringList();
    bool ok = false;
    const QByteArray fingerprint =
        core.size() == 2 ? StationIdentity::fromBase64Url(core.at(1).trimmed(), &ok) : QByteArray();
    auto key = std::make_shared<const ClientDeviceIdentity>(ClientDeviceIdentity::loadOrCreate(dir));
    if (!key->isValid() || !ok || fingerprint.size() != 32
        || !RendezvousWire::isRendezvousId(core.value(0))) {
        std::fputs("session: an argument is not usable\n", stderr);
        return 2;
    }
    QElapsedTimer clock;
    clock.start();
    auto* model = new RadioModel(RadioModel::Role::Remote);
    auto* proxy = new SettingsProxy();
    auto* window = new StationClient(model, proxy, QCoreApplication::instance());
    window->setDeviceIdentity(key, QStringLiteral("Harness desktop"));
    // One attempt: a failure is reported, not retried.
    window->setReconnectBackoffUnitMs(3600 * 1000);
    auto done = std::make_shared<bool>(false);
    // Plan Task 29: the race, a move, media.
    const QString direct = option(args, QStringLiteral("--direct"));
    const int waitUpgradeMs = option(args, QStringLiteral("--wait-upgrade-ms"), QStringLiteral("0")).toInt();
    const int mediaMs = option(args, QStringLiteral("--media-ms"), QStringLiteral("0")).toInt();
    const QString schedule = option(args, QStringLiteral("--upgrade-schedule-ms"));
    if (!schedule.isEmpty()) {
        QList<int> delays;
        for (const QString& part : schedule.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            delays.append(part.toInt());
        }
        window->setUpgradeScheduleForTest(delays);
    }
    auto handshakes = std::make_shared<int>(0);
    auto rankBefore = std::make_shared<int>(-1);
    auto moved = std::make_shared<bool>(false);
    auto media = std::make_shared<QPointer<MediaCounter>>();
    const auto finish = [window, &clock, done, handshakes, rankBefore, moved,
                         media](bool connected, const QString& reason) {
        if (*done) {
            return;
        }
        *done = true;
        QJsonObject result;
        result.insert(QStringLiteral("rank"), *rankBefore >= 0 ? *rankBefore : window->pathRank());
        result.insert(QStringLiteral("path"),
                      pathName(*rankBefore >= 0 ? *rankBefore : window->pathRank()));
        result.insert(QStringLiteral("attempt"), window->connectionAttempt().summary());
        result.insert(QStringLiteral("moved"), *moved);
        result.insert(QStringLiteral("rankAfter"), window->pathRank());
        result.insert(QStringLiteral("handshakes"), *handshakes);
        result.insert(QStringLiteral("switches"), window->pathSwitches());
        if (*media) {
            result.insert(QStringLiteral("audioPackets"), static_cast<double>((*media)->audioPackets));
            result.insert(QStringLiteral("audioDecoded"), static_cast<double>((*media)->audioDecoded));
            result.insert(QStringLiteral("displayMessages"),
                          static_cast<double>((*media)->displayMessages));
            result.insert(QStringLiteral("displayDecoded"),
                          static_cast<double>((*media)->displayDecoded));
        }
        result.insert(QStringLiteral("relayed"), false);
        if (const auto* transport = qobject_cast<const DataChannelTransport*>(window->transport())) {
            if (const auto path = transport->selectedPath()) {
                result.insert(QStringLiteral("relayed"), path->relayed());
                result.insert(QStringLiteral("localType"), path->localType);
                result.insert(QStringLiteral("remoteType"), path->remoteType);
                result.insert(QStringLiteral("localAddress"), path->localAddress);
                result.insert(QStringLiteral("remoteAddress"), path->remoteAddress);
            }
        }
        result.insert(QStringLiteral("connected"), connected);
        result.insert(QStringLiteral("ms"), static_cast<double>(clock.elapsed()));
        if (!reason.isEmpty()) {
            result.insert(QStringLiteral("reason"), reason);
        }
        printLine(result);
        window->disconnectFromStation(QStringLiteral("harness done"));
        // Let the close (and the relay's release) go out before leaving.
        QTimer::singleShot(1500, QCoreApplication::instance(),
                           [connected] { QCoreApplication::exit(connected ? 0 : 1); });
    };
    QObject::connect(window, &StationClient::handshakeComplete, window,
                     [window, model, finish, handshakes, rankBefore, moved, media, waitUpgradeMs,
                      mediaMs] {
        ++*handshakes;
        if (*handshakes > 1) {
            return;
        }
        *rankBefore = window->pathRank();
        if (waitUpgradeMs <= 0 && mediaMs <= 0) {
            finish(true, QString());
            return;
        }
        // Tell the harness the session is up, then carry on.
        printLine({{QStringLiteral("event"), QStringLiteral("connected")},
                   {QStringLiteral("rank"), window->pathRank()},
                   {QStringLiteral("path"), pathName(window->pathRank())}});
        if (waitUpgradeMs > 0) {
            QObject::connect(window, &StationClient::pathChanged, window, [window, finish, moved,
                                                                           mediaMs] {
                *moved = true;
                printLine({{QStringLiteral("event"), QStringLiteral("moved")},
                           {QStringLiteral("rank"), window->pathRank()}});
                if (mediaMs <= 0) {
                    finish(true, QString());
                }
            });
            QTimer::singleShot(waitUpgradeMs, window, [finish, mediaMs] {
                if (mediaMs <= 0) {
                    finish(true, QString());
                }
            });
        }
        if (mediaMs > 0) {
            *media = new MediaCounter(window, model, window);
            if (!(*media)->start()) {
                finish(true, QStringLiteral("Media could not start."));
                return;
            }
            QTimer::singleShot(mediaMs + std::max(0, waitUpgradeMs), window,
                               [finish] { finish(true, QString()); });
        }
    });
    QObject::connect(window, &StationClient::sessionEnded, window,
                     [finish](const QString& reason) { finish(false, reason); });
    QTimer::singleShot(timeoutMs, window, [finish] {
        finish(false, QStringLiteral("No session in time."));
    });
    const QList<QUrl> servers =
        RendezvousClient::serverUrls({option(args, QStringLiteral("--server"))});
    if (direct.isEmpty()) {
        window->connectThroughService(servers, core.value(0), fingerprint);
    } else {
        // Plan Task 29: the race, the Core's address and the service at
        // once.
        StationClient::ServiceRoute route;
        route.servers = servers;
        route.rendezvousId = core.value(0);
        route.relayAllowed = true;
        route.controlChannelVersion = 1;
        window->setServiceRoute(route);
        window->connectToStation(QUrl(direct), QString(), QString(), false, fingerprint);
    }
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
    if (mode == QLatin1String("device-key")) {
        const ClientDeviceIdentity key =
            ClientDeviceIdentity::loadOrCreate(option(args, QStringLiteral("--dir")));
        if (!key.isValid()) {
            return 2;
        }
        QTextStream(stdout) << StationIdentity::toBase64Url(key.publicKeySpki()) << "\n";
        return 0;
    }
    if (mode == QLatin1String("core")) {
        return runCore(args);
    }
    if (mode == QLatin1String("session")) {
        return runSession(args);
    }
    if (mode == QLatin1String("station")) {
        return runStation(args);
    }
    if (mode == QLatin1String("client")) {
        return runClient(args);
    }
    std::fputs("usage: nereus_rendezvous_peer key|device-key|station|client|core|session ...\n",
               stderr);
    return 2;
}
