#pragma once
// no-port-check: NereusSDR-original.
// =================================================================
// src/core/session/StationRendezvous.h  (NereusSDR)
// =================================================================
//
// The Core's use of the remote access service (iPhone app plan Task 27,
// R-IOS-08 and R-IOS-16; the pairing design, sections 4.3 and 5.3): what
// DaemonApp starts beside its StationServer, in one place so a test runs
// exactly what nereusd runs.
//
//   - Registers the Core under the rendezvous id of its station identity
//     key (StationServer::stationIdentity(), never the TLS certificate's
//     key) with the first of the configured servers that answers, and keeps
//     it registered.
//   - Introductions: only a paired device (StationServer::deviceStore())
//     whose signature verifies is reported (RendezvousClient::introduced);
//     every other is dropped without a reply and counted. Answering one
//     with a connection is the control session's (plan Task 28).
//   - Holds a nameplate while the pairing window is open, gives it back
//     when it closes (after the mailbox of the pairing that closed it has
//     closed, since releasing a nameplate ends its mailbox), and hands the
//     number to the window, so the code the Core shows is the one a device
//     types from anywhere.
//   - A device opening the mailbox on that nameplate pairs by the code
//     through it: StationServer::acceptPairingMailbox() runs the same
//     exchange as on a direct connection.
//
// A session already running is never touched by anything here: the service
// can stop and restart while devices stay connected.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-26: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include <QList>
#include <QObject>
#include <QPointer>
#include <QUrl>

namespace NereusSDR {

class RendezvousClient;
class StationServer;

class StationRendezvous : public QObject {
    Q_OBJECT

public:
    /// `server` must outlive this object. `servers` is the ordered list
    /// (RendezvousClient::serverUrls); `relayAllowed` nereusd.conf's
    /// `relay`.
    StationRendezvous(StationServer* server, const QList<QUrl>& servers, bool relayAllowed,
                      QObject* parent = nullptr);
    ~StationRendezvous() override;

    StationRendezvous(const StationRendezvous&) = delete;
    StationRendezvous& operator=(const StationRendezvous&) = delete;

    /// Registers, and follows the pairing window. False when the Core has
    /// no usable identity key or no server is configured.
    bool start();

    RendezvousClient* client() const { return m_client; }

private:
    void followPairingWindow();

    QPointer<StationServer> m_server;
    RendezvousClient* m_client = nullptr;
    /// The window closed while a device's pairing mailbox was still open
    /// (the pairing that just claimed the Core): the nameplate goes back
    /// once that mailbox has closed, so the Core's last pairing message is
    /// not cut off by the release.
    bool m_releaseAfterMailbox = false;
};

} // namespace NereusSDR
