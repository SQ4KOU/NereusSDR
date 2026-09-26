// no-port-check: NereusSDR-original.
// =================================================================
// src/core/session/StationRendezvous.cpp  (NereusSDR)
// =================================================================
//
// See StationRendezvous.h.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-26: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
//   2026-09-26: iPhone app plan Task 28 (R-IOS-16): introductions answered
//               with a control connection. J.J. Boyd (KG4VCF), with
//               AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include "core/session/StationRendezvous.h"

#include "core/session/DataChannelTransport.h"

#include "core/security/DeviceStore.h"
#include "core/security/PairingWindow.h"
#include "core/security/StationIdentity.h"
#include "core/session/RendezvousClient.h"
#include "core/session/RendezvousMailboxTransport.h"
#include "core/session/StationServer.h"

#include <QLoggingCategory>
#include <QTimer>

Q_LOGGING_CATEGORY(lcStationRendezvous, "nereus.session.stationrendezvous")

namespace NereusSDR {

// One answered introduction: its control connection until it opens, and
// the ICE settings that grow as the credentials arrive.
struct StationRendezvous::Answer {
    QByteArray id;
    DataChannelTransport* transport = nullptr;  // parented to the StationRendezvous
    IceConfiguration ice;
    QTimer* credentialsTimer = nullptr;
    QTimer* deadline = nullptr;
    bool gathering = false;
};

StationRendezvous::StationRendezvous(StationServer* server, const QList<QUrl>& servers,
                                     bool relayAllowed, QObject* parent)
    : QObject(parent)
    , m_server(server)
{
    m_client = new RendezvousClient(this);
    m_client->setServers(servers);
    m_client->setRelayAllowed(relayAllowed);
}

StationRendezvous::~StationRendezvous()
{
    for (Answer* answer : std::as_const(m_answers)) {
        delete answer;
    }
    m_answers.clear();
    if (m_client != nullptr) {
        m_client->stop();
    }
}

bool StationRendezvous::start()
{
    if (!m_server || m_client->servers().isEmpty()) {
        return false;
    }
    StationServer* server = m_server;
    const StationIdentity& identity = server->stationIdentity();
    DeviceStore* devices = server->deviceStore();
    PairingWindow* window = server->pairingWindow();
    if (!identity.isValid() || devices == nullptr || window == nullptr) {
        return false;
    }

    // The number in the pairing code is a nameplate held on the service
    // while the pairing window is open and given back when it closes, so a
    // claimed Core holds none (the rendezvous document, section 6.5).
    connect(window, &PairingWindow::stateChanged, this, &StationRendezvous::followPairingWindow);
    connect(m_client, &RendezvousClient::nameplateClaimed, window,
            [window](int nameplate) { window->setNameplate(nameplate); });
    // A device pairing by code from anywhere: the server runs the same
    // exchange as on a direct connection, over the mailbox.
    RendezvousClient* client = m_client;
    connect(m_client, &RendezvousClient::mailboxOpened, server, [server, client] {
        server->acceptPairingMailbox(new RendezvousMailboxTransport(client));
    });
    connect(m_client, &RendezvousClient::mailboxClosed, this, [this] {
        if (m_releaseAfterMailbox) {
            m_releaseAfterMailbox = false;
            followPairingWindow();
        }
    });
    followPairingWindow();

    // Task 28: a paired device's introduction is answered with a control
    // connection. The STUN server is chosen by this computer's address
    // families, so the hello's names are resolved first; an introduction
    // that comes during the lookup waits for it.
    connect(m_client, &RendezvousClient::connected, this, [this] {
        const quint64 lookup = ++m_lookup;
        m_stunResolved = false;
        m_stunFamilies.clear();
        IceConfiguration::resolveHostFamilies(
            IceConfiguration::hostNames(m_client->stunUrls()), this,
            [this, lookup](const HostFamilies& families) {
                if (lookup != m_lookup) {
                    return;
                }
                m_stunResolved = true;
                m_stunFamilies = families;
                const QList<RendezvousIntroduction> waiting = m_waiting;
                m_waiting.clear();
                for (const RendezvousIntroduction& introduction : waiting) {
                    answerIntroduction(introduction);
                }
            });
    });
    connect(m_client, &RendezvousClient::connectionLost, this, [this] {
        // What waited for this connection's lookup goes with it.
        ++m_lookup;
        m_stunResolved = false;
        m_waiting.clear();
    });
    connect(m_client, &RendezvousClient::introduced, this,
            [this](const RendezvousIntroduction& introduction) {
        if (!m_answersIntroductions) {
            return;
        }
        if (!m_stunResolved) {
            m_waiting.append(introduction);
            return;
        }
        answerIntroduction(introduction);
    });
    connect(m_client, &RendezvousClient::credentialsReceived, this,
            [this](const QByteArray& id, bool offered, const RendezvousWire::Turn& turn) {
        Answer* answer = m_answers.value(id);
        if (answer == nullptr || answer->gathering) {
            return;
        }
        answer->credentialsTimer->stop();
        const std::optional<RendezvousWire::Turn> relay =
            offered ? std::optional<RendezvousWire::Turn>(turn) : std::nullopt;
        // The relay host by this computer's families too: its names first.
        const QPointer<DataChannelTransport> transport(answer->transport);
        IceConfiguration::resolveHostFamilies(
            relay ? IceConfiguration::hostNames(relay->urls) : QStringList(), this,
            [this, id, relay, transport](const HostFamilies& families) {
                Answer* current = m_answers.value(id);
                if (current == nullptr || current->transport != transport || current->gathering) {
                    return;
                }
                current->ice.addHostFamilies(families);
                current->ice.setRelay(relay, 1);
                current->gathering = true;
                current->transport->gatherCandidates(current->ice);
            });
    });
    connect(m_client, &RendezvousClient::candidateReceived, this,
            [this](const QByteArray& id, const QString& candidate) {
        Answer* answer = m_answers.value(id);
        if (answer != nullptr && !candidate.isEmpty()) {
            answer->transport->acceptCandidate(candidate);
        }
    });

    // Signed with the station identity key, never the TLS certificate's
    // (CertificateStore.h: the two keys differ). Only a paired device is
    // introduced; a revoked one is no longer in the store.
    const QPointer<StationServer> guard(server);
    m_client->registerStation(
        identity.publicKeySpki(),
        [guard](const QByteArray& message) {
            return guard ? guard->stationIdentity().sign(message) : QByteArray();
        },
        [guard](const QByteArray& deviceId) {
            if (!guard || guard->deviceStore() == nullptr) {
                return QByteArray();
            }
            const std::optional<PairedDevice> device = guard->deviceStore()->find(deviceId);
            return device ? device->publicKeySpki : QByteArray();
        });
    return true;
}

void StationRendezvous::answerIntroduction(const RendezvousIntroduction& introduction)
{
    if (!m_server || m_answers.contains(introduction.id)) {
        return;
    }
    const QString certificate = m_server->certificatePemPath();
    const QString key = m_server->privateKeyPemPath();
    if (certificate.isEmpty() || key.isEmpty()) {
        qCWarning(lcStationRendezvous) << "No certificate to answer a connection with";
        m_client->retireIntroduction(introduction.id);
        return;
    }
    const QByteArray id = introduction.id;
    auto* answer = new Answer;
    answer->id = id;
    answer->ice = IceConfiguration::throughRendezvous(m_client->stunUrls(),
                                                      m_client->relayAllowed(),
                                                      IceConfiguration::localAddressFamilies(),
                                                      m_stunFamilies);
    answer->transport = new DataChannelTransport(this);
    answer->credentialsTimer = new QTimer(answer->transport);
    answer->credentialsTimer->setSingleShot(true);
    answer->deadline = new QTimer(answer->transport);
    answer->deadline->setSingleShot(true);
    m_answers.insert(id, answer);

    DataChannelTransport* transport = answer->transport;
    connect(transport, &DataChannelTransport::localDescription, this,
            [this, id](const QString& sdp, const QString&) {
        Answer* current = m_answers.value(id);
        if (current == nullptr) {
            return;
        }
        if (!m_client->answer(id, sdp)) {
            finishAnswer(id, false);
            return;
        }
        if (!current->ice.relayAllowed()) {
            // relay = deny: no credentials follow; gather now.
            current->gathering = true;
            current->transport->gatherCandidates(current->ice);
        } else {
            current->credentialsTimer->start(m_credentialsTimeoutMs);
        }
    });
    connect(answer->credentialsTimer, &QTimer::timeout, this, [this, id] {
        Answer* current = m_answers.value(id);
        if (current == nullptr || current->gathering) {
            return;
        }
        // The credentials never came: direct paths only.
        qCInfo(lcStationRendezvous) << "No relay credentials came; connecting without the relay";
        current->ice.setRelay(std::nullopt, 1);
        current->gathering = true;
        current->transport->gatherCandidates(current->ice);
    });
    connect(transport, &DataChannelTransport::localCandidate, this,
            [this, id](const QString& candidate) { m_client->sendCandidate(id, candidate); });
    connect(transport, &DataChannelTransport::gatheringComplete, this,
            [this, id] { m_client->sendCandidate(id, QString()); });
    connect(transport, &DataChannelTransport::opened, this,
            [this, id] { finishAnswer(id, true); });
    connect(transport, &DataChannelTransport::failed, this, [this, id](const QString& reason) {
        qCInfo(lcStationRendezvous) << "A connection through the remote access service failed:"
                                    << reason;
        finishAnswer(id, false);
    });
    connect(answer->deadline, &QTimer::timeout, this, [this, id] {
        qCInfo(lcStationRendezvous)
            << "A connection through the remote access service did not open in time";
        finishAnswer(id, false);
    });
    answer->deadline->start(m_answerDeadlineMs);

    DataChannelTransport::Options options;
    options.role = DataChannelTransport::Role::Answerer;
    options.maxIncomingBytes = StationServer::kMaxIncomingMessageBytes;
    options.ice = answer->ice;
    options.certificatePemPath = certificate;
    options.privateKeyPemPath = key;
    if (!transport->start(options)
        || !transport->acceptDescription(introduction.offer, QStringLiteral("offer"))) {
        qCInfo(lcStationRendezvous) << "An introduction's offer could not be answered";
        finishAnswer(id, false);
    }
}

void StationRendezvous::finishAnswer(const QByteArray& id, bool opened)
{
    Answer* answer = m_answers.take(id);
    if (answer == nullptr) {
        return;
    }
    // Retired here, whatever happened, so it holds no place for the rest of
    // its lifetime at the service.
    m_client->retireIntroduction(id);
    DataChannelTransport* transport = answer->transport;
    delete answer->credentialsTimer;
    delete answer->deadline;
    delete answer;
    transport->disconnect(this);
    if (opened && m_server) {
        // One of the Core's connections from here on, as a WebSocket's is.
        m_server->acceptTransport(transport);
        return;
    }
    transport->closeLink(QStringLiteral("not connected"));
    transport->deleteLater();
}

void StationRendezvous::followPairingWindow()
{
    const PairingWindow* window = m_server ? m_server->pairingWindow() : nullptr;
    if (window != nullptr && window->isOpen()) {
        m_releaseAfterMailbox = false;
        m_client->claimNameplate();
    } else if (m_client->isMailboxOpen()) {
        // Releasing the nameplate would end the mailbox before the Core's
        // answer to the pairing that closed the window goes through it.
        m_releaseAfterMailbox = true;
    } else {
        m_client->releaseNameplate();
    }
}

} // namespace NereusSDR
