#pragma once
// no-port-check: NereusSDR-original.
// =================================================================
// src/core/session/RendezvousDialer.h  (NereusSDR)
// =================================================================
//
// iPhone app plan Task 28 (R-IOS-16): the desktop's side of a control
// connection through the remote access service. It asks the service to
// introduce this computer, a paired device, to the Core registered under
// the Core's rendezvous id, and makes the ICE connection that follows,
// carrying the control channel (DataChannelTransport):
//
//   1. connects to the first server that answers (RendezvousClient);
//   2. resolves the hello's STUN names and chooses the STUN server by this
//      computer's address families (IceConfiguration), then makes its offer
//      and introduces itself, signing with its device key;
//   3. takes the Core's answer, resolves the relay names in it, chooses the
//      relay host the same way and gathers; candidates go both ways through
//      the service;
//   4. once the channel opens, hands the transport over (ready()) and
//      leaves the service: the session runs over the connection, and the
//      service stays out of it (the rendezvous document, section 6.3).
//
// A service that does not answer, a Core that is not there, a connection
// that fails or has not opened within kDialDeadlineMs ends the attempt
// with failed(), in plain words. The certificate is not checked here: the
// session checks the one the Core presents in DTLS against the one its
// identity key binds, at the same gate as a WebSocket's (StationClient).
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-26: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include "core/session/IceConfiguration.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#include <memory>
#include <optional>

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

namespace NereusSDR {

class ClientDeviceIdentity;
class DataChannelTransport;
class RendezvousClient;

class RendezvousDialer : public QObject {
    Q_OBJECT

public:
    /// The whole attempt: the service's hello and the two lookups, then
    /// gathering and the connectivity checks (IceConfiguration::
    /// kConnectDeadlineMs).
    static constexpr int kDialDeadlineMs = 10000 + 2 * IceConfiguration::kHostLookupTimeoutMs
                                           + IceConfiguration::kConnectDeadlineMs;

    explicit RendezvousDialer(QObject* parent = nullptr);
    ~RendezvousDialer() override;

    RendezvousDialer(const RendezvousDialer&) = delete;
    RendezvousDialer& operator=(const RendezvousDialer&) = delete;

    /// Starts an attempt: `servers` in order (RendezvousClient::serverUrls),
    /// the Core's rendezvous id, and this computer's device key. Once only.
    void dial(const QList<QUrl>& servers, const QString& stationId,
              std::shared_ptr<const ClientDeviceIdentity> device);
    /// Stops the attempt; nothing more is signalled.
    void cancel();

    /// Test seam: the attempt's bound.
    void setDialDeadlineMs(int ms) { m_deadlineMs = ms; }

    /// The ICE settings of the connection, the relay included once known.
    std::optional<IceConfiguration> iceConfiguration() const { return m_ice; }

signals:
    /// The control channel opened. The receiver takes `transport` (it has
    /// no parent) and runs the session over it.
    void ready(NereusSDR::DataChannelTransport* transport);
    /// The attempt ended without a connection. `reason` is plain words.
    void failed(const QString& reason);

private:
    void fail(const QString& reason);
    void startOffer();

    RendezvousClient* m_client = nullptr;
    QPointer<DataChannelTransport> m_transport;
    QTimer* m_deadline = nullptr;
    std::shared_ptr<const ClientDeviceIdentity> m_device;
    QString m_stationId;
    std::optional<IceConfiguration> m_ice;
    int m_deadlineMs = kDialDeadlineMs;
    bool m_started = false;
    bool m_done = false;
    bool m_answered = false;
};

} // namespace NereusSDR
