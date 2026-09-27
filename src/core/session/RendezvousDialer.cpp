// no-port-check: NereusSDR-original.
// =================================================================
// src/core/session/RendezvousDialer.cpp  (NereusSDR)
// =================================================================
//
// iPhone app plan Task 28 (R-IOS-16). See RendezvousDialer.h.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-26: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
//   2026-09-26: Task 28 fix wave (review Minor 1): an ended introduction
//               or a lost service connection ends the attempt at once.
//               J.J. Boyd (KG4VCF), with AI-assisted implementation via
//               Anthropic Claude Code.
// =================================================================

#include "core/session/RendezvousDialer.h"

#include "core/security/ClientDeviceIdentity.h"
#include "core/session/DataChannelTransport.h"
#include "core/session/RendezvousClient.h"
#include "core/session/StationClient.h"

#include <QLoggingCategory>
#include <QTimer>

Q_LOGGING_CATEGORY(lcRendezvousDialer, "nereus.session.rendezvousdialer")

namespace NereusSDR {

namespace {

// Plain words for the operator (OperatorWording::isPlain); where they came
// from is in the log.
constexpr const char* kNotReached = "The Core could not be reached from here.";
constexpr const char* kNoDeviceKey = "This computer has no key to connect with.";

} // namespace

RendezvousDialer::RendezvousDialer(QObject* parent)
    : QObject(parent)
{
    m_deadline = new QTimer(this);
    m_deadline->setSingleShot(true);
    connect(m_deadline, &QTimer::timeout, this, [this] {
        qCInfo(lcRendezvousDialer) << "The connection through the remote access service did "
                                      "not open in time";
        fail(QString::fromLatin1(kNotReached));
    });
}

RendezvousDialer::~RendezvousDialer()
{
    cancel();
}

void RendezvousDialer::dial(const QList<QUrl>& servers, const QString& stationId,
                            std::shared_ptr<const ClientDeviceIdentity> device)
{
    if (m_started) {
        return;
    }
    m_started = true;
    m_stationId = stationId;
    m_device = std::move(device);
    if (!m_device || !m_device->isValid()) {
        QTimer::singleShot(0, this, [this] { fail(QString::fromLatin1(kNoDeviceKey)); });
        return;
    }
    m_client = new RendezvousClient(this);
    m_client->setServers(servers);
    connect(m_client, &RendezvousClient::connected, this, [this] {
        // The STUN server by this computer's families, once the names are
        // resolved (IceConfiguration).
        IceConfiguration::resolveHostFamilies(
            IceConfiguration::hostNames(m_client->stunUrls()), this,
            [this](const HostFamilies& families) {
                if (m_done || m_transport) {
                    return;
                }
                m_ice = IceConfiguration::throughRendezvous(
                    m_client->stunUrls(), true, IceConfiguration::localAddressFamilies(),
                    families);
                startOffer();
            });
    });
    connect(m_client, &RendezvousClient::unreachable, this,
            [this](const QString& reason) { fail(reason); });
    connect(m_client, &RendezvousClient::answerReceived, this,
            [this](const QString& sdp, bool offered, const RendezvousWire::Turn& turn) {
        if (m_done || !m_transport || m_answered) {
            return;
        }
        m_answered = true;
        if (!m_transport->acceptDescription(sdp, QStringLiteral("answer"))) {
            qCInfo(lcRendezvousDialer) << "The Core's answer could not be used";
            fail(QString::fromLatin1(kNotReached));
            return;
        }
        const std::optional<RendezvousWire::Turn> relay =
            offered ? std::optional<RendezvousWire::Turn>(turn) : std::nullopt;
        // The relay host by this computer's families: its names first.
        IceConfiguration::resolveHostFamilies(
            relay ? IceConfiguration::hostNames(relay->urls) : QStringList(), this,
            [this, relay](const HostFamilies& families) {
                if (m_done || !m_transport || !m_ice) {
                    return;
                }
                m_ice->addHostFamilies(families);
                m_ice->setRelay(relay, 1);
                m_transport->gatherCandidates(*m_ice);
            });
    });
    connect(m_client, &RendezvousClient::candidateReceived, this,
            [this](const QByteArray&, const QString& candidate) {
        if (!m_done && m_transport && !candidate.isEmpty()) {
            m_transport->acceptCandidate(candidate);
        }
    });
    // Task 28 fix wave (review Minor 1): once the introduction has ended
    // (the Core left the service, it expired, or this computer's
    // connection to the service was lost) no answer or candidate can come
    // any more, so the attempt ends now rather than at the deadline, and
    // the next try goes through the service again.
    connect(m_client, &RendezvousClient::introductionEnded, this,
            [this](const QByteArray&, const QString& code) {
        if (m_done) {
            return;
        }
        qCInfo(lcRendezvousDialer) << "The introduction ended before the connection opened:"
                                   << code;
        fail(QString::fromLatin1(kNotReached));
    });
    connect(m_client, &RendezvousClient::connectionLost, this, [this] {
        if (m_done) {
            return;
        }
        qCInfo(lcRendezvousDialer) << "The connection to the remote access service ended "
                                      "before the Core's connection opened";
        fail(QString::fromLatin1(kNotReached));
    });
    connect(m_client, &RendezvousClient::serviceError, this,
            [](const QString& code, const QString&, qint64) {
        qCInfo(lcRendezvousDialer) << "The remote access service answered" << code;
    });
    m_deadline->start(m_deadlineMs);
    m_client->connectToService();
}

void RendezvousDialer::startOffer()
{
    auto* transport = new DataChannelTransport(this);
    m_transport = transport;
    connect(transport, &DataChannelTransport::localDescription, this,
            [this](const QString& sdp, const QString&) {
        if (m_done) {
            return;
        }
        const std::shared_ptr<const ClientDeviceIdentity> device = m_device;
        m_client->introduce(m_stationId, device->publicKeySpki(),
                            [device](const QByteArray& message) { return device->sign(message); },
                            sdp);
    });
    connect(transport, &DataChannelTransport::localCandidate, this,
            [this](const QString& candidate) {
        if (!m_done) {
            m_client->sendCandidate(candidate);
        }
    });
    connect(transport, &DataChannelTransport::gatheringComplete, this, [this] {
        if (!m_done) {
            m_client->sendCandidate(QString());
        }
    });
    connect(transport, &DataChannelTransport::failed, this, [this](const QString& reason) {
        qCInfo(lcRendezvousDialer) << "The connection through the remote access service failed:"
                                   << reason;
        fail(QString::fromLatin1(kNotReached));
    });
    connect(transport, &DataChannelTransport::opened, this, [this, transport] {
        if (m_done) {
            return;
        }
        m_done = true;
        m_deadline->stop();
        transport->disconnect(this);
        transport->setParent(nullptr);
        m_transport = nullptr;
        // The session runs over the connection from here; the service has
        // nothing more to do in it.
        m_client->stop();
        emit ready(transport);
    });
    DataChannelTransport::Options options;
    options.role = DataChannelTransport::Role::Offerer;
    options.maxIncomingBytes = StationClient::kMaxIncomingMessageBytes;
    options.ice = m_ice;
    if (!transport->start(options)) {
        fail(QString::fromLatin1(kNotReached));
    }
}

void RendezvousDialer::fail(const QString& reason)
{
    if (m_done) {
        return;
    }
    m_done = true;
    m_deadline->stop();
    if (m_transport) {
        m_transport->disconnect(this);
        m_transport->closeLink(QStringLiteral("not connected"));
        m_transport->deleteLater();
        m_transport = nullptr;
    }
    if (m_client != nullptr) {
        m_client->stop();
    }
    emit failed(reason);
}

void RendezvousDialer::cancel()
{
    if (m_done) {
        return;
    }
    m_done = true;
    m_deadline->stop();
    if (m_transport) {
        m_transport->disconnect(this);
        m_transport->closeLink(QStringLiteral("cancelled"));
        m_transport->deleteLater();
        m_transport = nullptr;
    }
    if (m_client != nullptr) {
        m_client->stop();
    }
}

} // namespace NereusSDR
