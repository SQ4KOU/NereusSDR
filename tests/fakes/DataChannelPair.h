#pragma once
// no-port-check: NereusSDR-original.
// =================================================================
// tests/fakes/DataChannelPair.h  (NereusSDR)
// =================================================================
//
// iPhone app plan Task 28 (R-IOS-16): two DataChannelTransports on this
// computer, joined directly (host candidates, no service): the device's
// Offerer and the Core's Answerer, the Answerer presenting the certificate
// it is given. What tst_data_channel_transport and the session conformance
// runner's data-channel mode run the control channel over.
//
// DataChannelBridge puts such a pair between a LoopbackTransport the
// conformance runner plays a client through and the StationServer, so every
// fixture runs over a real DTLS and SCTP connection unchanged: what the
// client's LoopbackTransport sends crosses the data channel, what the Core
// sends comes back the same way, and a close at either end closes the
// other. settled() says whether both ends have handled everything the other
// sent, which the runner waits for wherever it waits for the event loop.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-26: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QObject>
#include <QPointer>
#include <QString>

#include "core/session/DataChannelTransport.h"
#include "LoopbackTransport.h"

namespace NereusSDR::Test {

/// Joins `offerer` and `answerer` directly and starts both. False when
/// either refuses to start.
inline bool startDataChannelPair(DataChannelTransport* offerer, DataChannelTransport* answerer,
                                 quint64 offererCap, quint64 answererCap,
                                 const QString& certificatePemPath,
                                 const QString& privateKeyPemPath)
{
    QObject::connect(offerer, &DataChannelTransport::localDescription, answerer,
                     [answerer](const QString& sdp, const QString& type) {
        answerer->acceptDescription(sdp, type);
    });
    QObject::connect(answerer, &DataChannelTransport::localDescription, offerer,
                     [offerer](const QString& sdp, const QString& type) {
        offerer->acceptDescription(sdp, type);
    });
    QObject::connect(offerer, &DataChannelTransport::localCandidate, answerer,
                     [answerer](const QString& candidate) { answerer->acceptCandidate(candidate); });
    QObject::connect(answerer, &DataChannelTransport::localCandidate, offerer,
                     [offerer](const QString& candidate) { offerer->acceptCandidate(candidate); });
    DataChannelTransport::Options answer;
    answer.role = DataChannelTransport::Role::Answerer;
    answer.maxIncomingBytes = answererCap;
    answer.certificatePemPath = certificatePemPath;
    answer.privateKeyPemPath = privateKeyPemPath;
    DataChannelTransport::Options offer;
    offer.role = DataChannelTransport::Role::Offerer;
    offer.maxIncomingBytes = offererCap;
    return answerer->start(answer) && offerer->start(offer);
}

/// Both ends have handled everything the other sent: every message
/// delivered, every ping seen, every pong handled.
inline bool dataChannelPairSettled(const DataChannelTransport& a, const DataChannelTransport& b)
{
    const DataChannelTransport::Counts x = a.countsForTest();
    const DataChannelTransport::Counts y = b.countsForTest();
    return x.messagesSent == y.messagesDelivered && y.messagesSent == x.messagesDelivered
        && x.pingsSent == y.pingsReceived && y.pingsSent == x.pingsReceived
        && x.pongsSent == y.pongsReceived && y.pongsSent == x.pongsReceived;
}

/// Runs the event loop until `done` holds or `ms` pass.
template <typename Done>
bool waitFor(Done done, int ms)
{
    const QDeadlineTimer deadline(ms);
    while (!done() && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return done();
}

class DataChannelBridge : public QObject {
public:
    /// `client` is the transport the runner plays; `server`'s end is handed
    /// to `accept` once the channel opens. The Answerer presents the
    /// certificate at the two paths.
    template <typename Accept>
    DataChannelBridge(LoopbackTransport* client, quint64 stationCap, quint64 clientCap,
                      const QString& certificatePemPath, const QString& privateKeyPemPath,
                      Accept accept, QObject* parent = nullptr)
        : QObject(parent)
        , m_client(client)
    {
        m_near = new LoopbackTransport(QStringLiteral("data-channel-bridge"), this);
        m_near->linkTo(client);
        m_offerer = new DataChannelTransport(this);
        m_answerer = new DataChannelTransport();
        // As a LoopbackTransport reports by default: no address of the
        // client's own (DataChannelTransport::setPeerAddressForTest()).
        m_answerer->setPeerAddressForTest(QString());
        QObject::connect(m_near, &SessionTransport::textReceived, this,
                         [this](const QByteArray& wire) { m_offerer->sendText(wire); });
        QObject::connect(m_offerer, &SessionTransport::textReceived, this,
                         [this](const QByteArray& wire) { m_near->sendText(wire); });
        QObject::connect(m_near, &SessionTransport::closed, this,
                         [this]() { m_offerer->closeLink(QStringLiteral("client closed")); });
        QObject::connect(m_offerer, &SessionTransport::closed, this, [this]() {
            m_near->closeLink(QStringLiteral("control channel closed"));
        });
        QObject::connect(m_offerer, &DataChannelTransport::failed, this, [this]() {
            m_near->closeLink(QStringLiteral("control channel failed"));
        });
        QObject::connect(m_offerer, &DataChannelTransport::opened, this,
                         [this]() { m_offererOpened = true; });
        QObject::connect(m_answerer, &DataChannelTransport::opened, this, [this, accept]() {
            m_answererOpened = true;
            accept(m_answerer);
        });
        m_started = startDataChannelPair(m_offerer, m_answerer, clientCap, stationCap,
                                         certificatePemPath, privateKeyPemPath);
    }

    ~DataChannelBridge() override
    {
        // Owned by the server once accepted; otherwise ours.
        if (m_answerer && m_answerer->parent() == nullptr) {
            delete m_answerer.data();
        }
    }

    bool started() const { return m_started; }
    /// Both ends opened (the station may have closed its end again at
    /// once, as it does a connection past its limit).
    bool opened() const { return m_offererOpened && m_answererOpened; }

    /// Everything each end sent has been handled at the other, a close at
    /// either end has reached the other, and the client's choice about
    /// answering pings is the offerer's.
    bool settled()
    {
        if (m_client) {
            m_offerer->setAnswersPingsForTest(m_client->answersPings());
        }
        // A close crossing from one end to the other is not settled yet.
        const bool offererOpen = m_offerer->isOpen();
        const bool answererOpen = m_answerer && m_answerer->isOpen();
        if (offererOpen != answererOpen) {
            return false;
        }
        return !offererOpen || dataChannelPairSettled(*m_offerer, *m_answerer);
    }

    DataChannelTransport* offerer() const { return m_offerer; }
    DataChannelTransport* answerer() const { return m_answerer; }

private:
    QPointer<LoopbackTransport> m_client;
    LoopbackTransport* m_near = nullptr;
    DataChannelTransport* m_offerer = nullptr;
    QPointer<DataChannelTransport> m_answerer;
    bool m_started = false;
    bool m_offererOpened = false;
    bool m_answererOpened = false;
};

} // namespace NereusSDR::Test
