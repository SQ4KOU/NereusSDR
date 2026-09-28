// no-port-check: NereusSDR-original direct auxiliary watch transport.
#include "core/session/TxWatchClient.h"

#include "core/safety/RemoteTxWatchdog.h"

#include <QCryptographicHash>
#include <QPointer>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslError>
#include <QTimer>
#include <QWebSocket>

namespace NereusSDR {
namespace {
constexpr int kTicketBytes = 32;
constexpr int kAttachBytes = 1 + kTicketBytes;
constexpr int kAckBytes = 2;
constexpr qint64 kMaxOutboundBacklog = 4096;
const QByteArray kAck = QByteArray::fromHex("0100");
}

TxWatchClient::TxWatchClient(QObject* parent)
    : QObject(parent)
    , m_deadline(new QTimer(this))
{
    m_deadline->setSingleShot(true);
    connect(m_deadline, &QTimer::timeout, this, [this]() {
        finish(m_ready ? QStringLiteral("watch deadline")
                       : QStringLiteral("watch attachment timed out"));
    });
}

TxWatchClient::~TxWatchClient()
{
    // Destructor must not invoke a user callback. Detach before abort(),
    // which may synchronously deliver a socket event.
    m_deadline->stop();
    if (QWebSocket* socket = m_socket.data()) {
        m_socket = nullptr;
        QObject::disconnect(socket, nullptr, this, nullptr);
        // The ready handler may delete this client while QWebSocket is
        // emitting binaryMessageReceived. Keep the sender alive until that
        // emission unwinds; deleting a child in QObject's destructor would
        // destroy the sender in the middle of its own signal delivery.
        socket->setParent(nullptr);
        socket->deleteLater();
    }
    m_ticket.fill('\0');
    m_ticket.clear();
    m_pin.clear();
}

void TxWatchClient::setDeadlinesForTesting(int openingMs, int acknowledgementMs)
{
    if (!m_active && openingMs > 0 && openingMs <= 10000
        && acknowledgementMs > 0 && acknowledgementMs <= 5000) {
        m_openingMs = openingMs;
        m_acknowledgementMs = acknowledgementMs;
    }
}

bool TxWatchClient::current(QWebSocket* socket, quint64 generation) const
{
    return m_active && m_socket == socket && m_generation == generation;
}

bool TxWatchClient::freshPinMatches(QWebSocket* socket) const
{
    if (socket == nullptr || m_pin.size() != kTicketBytes) {
        return false;
    }
    const QSslCertificate certificate = socket->sslConfiguration().peerCertificate();
    return !certificate.isNull()
           && certificate.digest(QCryptographicHash::Sha256) == m_pin;
}

bool TxWatchClient::openDirect(const QUrl& verifiedPrimaryUrl,
                               const QByteArray& actualCorePinSha256,
                               const QByteArray& rawTicket, quint64 primaryGeneration)
{
    const QPointer<TxWatchClient> self(this);
    const quint64 revision = ++m_revision;
    if (m_active) {
        finish(QStringLiteral("watch replaced"));
    }
    if (!self || m_revision != revision || m_active) {
        return false;
    }
    m_generation = primaryGeneration;
    const bool valid = verifiedPrimaryUrl.isValid()
                       && verifiedPrimaryUrl.scheme() == QLatin1String("wss")
                       && !verifiedPrimaryUrl.host().isEmpty()
                       && !verifiedPrimaryUrl.authority(QUrl::FullyEncoded).contains('@')
                       && actualCorePinSha256.size() == kTicketBytes
                       && rawTicket.size() == kTicketBytes
                       && primaryGeneration != 0;
    if (!valid) {
        emit closed(primaryGeneration, QStringLiteral("invalid watch attachment inputs"));
        return false;
    }

    QUrl watchUrl;
    watchUrl.setScheme(QStringLiteral("wss"));
    watchUrl.setHost(verifiedPrimaryUrl.host());
    watchUrl.setPort(verifiedPrimaryUrl.port());
    watchUrl.setPath(QStringLiteral("/tx-watch/v1"));
    if (!watchUrl.isValid()) {
        emit closed(primaryGeneration, QStringLiteral("invalid watch address"));
        return false;
    }

    m_pin = actualCorePinSha256;
    m_ticket = rawTicket;
    m_active = true;
    m_ready = false;
    auto* socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    m_socket = socket;
    socket->setMaxAllowedIncomingMessageSize(kAttachBytes);
    socket->setMaxAllowedIncomingFrameSize(kAttachBytes);
    const quint64 generation = m_generation;

    connect(socket, &QWebSocket::sslErrors, this,
            [this, socket, generation](const QList<QSslError>& errors) {
        if (!current(socket, generation)) { return; }
        if (!freshPinMatches(socket)) {
            finish(QStringLiteral("watch certificate mismatch"));
            return;
        }
        QList<QSslError> ignorable;
        for (const QSslError& error : errors) {
            if (error.error() != QSslError::CertificateExpired
                && error.error() != QSslError::CertificateNotYetValid) {
                ignorable.append(error);
            }
        }
        socket->ignoreSslErrors(ignorable);
    });
    connect(socket, &QWebSocket::connected, this, [this, socket, generation]() {
        if (!current(socket, generation)) { return; }
        // A successful TLS handshake may emit no sslErrors at all. Inspect
        // this socket's actual peer certificate before disclosing the ticket.
        if (!freshPinMatches(socket)) {
            finish(QStringLiteral("watch certificate mismatch"));
            return;
        }
        QByteArray attach(kAttachBytes, '\0');
        attach[0] = char(1);
        for (int i = 0; i < kTicketBytes; ++i) { attach[1 + i] = m_ticket.at(i); }
        m_ticket.fill('\0');
        m_ticket.clear();
        const qsizetype attachSize = attach.size();
        const QPointer<TxWatchClient> self(this);
        const quint64 revision = m_revision;
        qint64 sent = -1;
#ifdef NEREUS_BUILD_TESTS
        const BinaryWriterForTesting writer = m_binaryWriterForTesting;
        if (writer) {
            sent = writer(socket, attach);
        } else {
            sent = socket->sendBinaryMessage(attach);
        }
#else
        sent = socket->sendBinaryMessage(attach);
#endif
        attach.fill('\0');
        // sendBinaryMessage may synchronously emit errorOccurred, whose
        // closed handler can delete us or open a new socket. Never finish
        // that newer attempt or arm its acknowledgement deadline here.
        if (!self || self->m_revision != revision
            || !self->current(socket, generation)) {
            return;
        }
        if (sent != attachSize) {
            self->finish(QStringLiteral("watch ticket send failed"));
            return;
        }
        if (!self->m_ready) {
            self->m_deadline->start(self->m_acknowledgementMs);
        }
    });
    connect(socket, &QWebSocket::binaryMessageReceived, this,
            [this, socket, generation](const QByteArray& message) {
        if (!current(socket, generation)) { return; }
        if (m_ready || message.size() != kAckBytes || message != kAck) {
            finish(QStringLiteral("unexpected watch message"));
            return;
        }
        m_deadline->stop();
        m_ready = true;
        emit ready(generation);
    });
    connect(socket, &QWebSocket::textMessageReceived, this,
            [this, socket, generation](const QString&) {
        if (current(socket, generation)) {
            finish(QStringLiteral("unexpected watch text"));
        }
    });
    connect(socket, &QWebSocket::disconnected, this, [this, socket, generation]() {
        if (current(socket, generation)) {
            finish(QStringLiteral("watch disconnected"));
        }
    });
    connect(socket, &QWebSocket::errorOccurred, this,
            [this, socket, generation](QAbstractSocket::SocketError) {
        if (current(socket, generation)) {
            finish(QStringLiteral("watch connection failed"));
        }
    });
    m_deadline->start(m_openingMs);
    socket->open(watchUrl);
    return self && self->current(socket, generation);
}

bool TxWatchClient::sendKeepalive(quint64 sequence, quint32 epoch)
{
    const QPointer<TxWatchClient> self(this);
    QWebSocket* socket = m_socket.data();
    if (!m_ready || socket == nullptr
        || socket->state() != QAbstractSocket::ConnectedState) {
        finish(QStringLiteral("watch unavailable"));
        return false;
    }
    const QByteArray frame = RemoteTxWatchdog::channelKeepalive(sequence, epoch);
    qint64 backlog = socket->bytesToWrite();
#ifdef NEREUS_BUILD_TESTS
    if (m_testBacklogBytes >= 0) { backlog = m_testBacklogBytes; }
#endif
    if (backlog > kMaxOutboundBacklog - frame.size()) {
        finish(QStringLiteral("watch send backlog or failure"));
        return false;
    }
    const quint64 generation = m_generation;
    const quint64 revision = m_revision;
    qint64 sent = -1;
#ifdef NEREUS_BUILD_TESTS
    const BinaryWriterForTesting writer = m_binaryWriterForTesting;
    if (writer) {
        sent = writer(socket, frame);
    } else {
        sent = socket->sendBinaryMessage(frame);
    }
#else
    sent = socket->sendBinaryMessage(frame);
#endif
    if (!self || self->m_revision != revision
        || !self->current(socket, generation) || !self->m_ready) {
        return false;
    }
    if (sent != frame.size()) {
        self->finish(QStringLiteral("watch send backlog or failure"));
        return false;
    }
    return true;
}

void TxWatchClient::close()
{
    finish(QStringLiteral("watch closed"));
}

void TxWatchClient::finish(const QString& reason)
{
    if (!m_active) { return; }
    m_active = false;
    m_ready = false;
    m_deadline->stop();
    m_ticket.fill('\0');
    m_ticket.clear();
    m_pin.clear();
    QWebSocket* socket = m_socket.data();
    m_socket = nullptr;
    if (socket != nullptr) {
        QObject::disconnect(socket, nullptr, this, nullptr);
        // A closed handler can delete this client while the socket is
        // emitting errorOccurred/disconnected. Detach the sender before
        // emitting closed, so QObject's child teardown cannot delete it
        // mid-signal.
        socket->setParent(nullptr);
        socket->abort();
        socket->deleteLater();
    }
    const quint64 generation = m_generation;
    emit closed(generation, reason);
}

} // namespace NereusSDR
