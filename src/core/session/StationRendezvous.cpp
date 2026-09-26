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
// =================================================================

#include "core/session/StationRendezvous.h"

#include "core/security/DeviceStore.h"
#include "core/security/PairingWindow.h"
#include "core/security/StationIdentity.h"
#include "core/session/RendezvousClient.h"
#include "core/session/RendezvousMailboxTransport.h"
#include "core/session/StationServer.h"

namespace NereusSDR {

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
