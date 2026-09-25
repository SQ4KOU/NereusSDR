// =================================================================
// src/core/session/StationOpeningGate.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. See StationOpeningGate.h.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-25  J.J. Boyd / KG4VCF  Original implementation (R-IOS-01,
//                                    R-R3-26). AI-assisted via Anthropic
//                                    Claude Code.
// =================================================================

#include "core/session/StationOpeningGate.h"

#include <QLoggingCategory>
#include <QSslSocket>
#include <QTimer>
#include <QWebSocket>
#include <QWebSocketServer>

Q_LOGGING_CATEGORY(lcStationOpening, "nereus.station.opening")

namespace NereusSDR {

namespace {

// What a request the Core cannot read is told. The body is for a person
// reading it in a browser or a log; clients act on the status alone.
const QByteArray kBadRequestBody =
    QByteArrayLiteral("The Core could not read this connection request.\n");

QByteArray badRequest()
{
    return QByteArrayLiteral("HTTP/1.1 400 Bad Request\r\n"
                             "Content-Type: text/plain; charset=utf-8\r\n"
                             "Content-Length: ")
           + QByteArray::number(kBadRequestBody.size())
           + QByteArrayLiteral("\r\nConnection: close\r\n\r\n") + kBadRequestBody;
}

bool isPort(const QString& text)
{
    if (text.isEmpty() || text.size() > 5) {
        return false;
    }
    for (const QChar c : text) {
        if (c < QLatin1Char('0') || c > QLatin1Char('9')) {
            return false;
        }
    }
    return text.toInt() <= 65535;
}

// A host name as DNS and mDNS write them: labels of letters, digits,
// hyphens and underscores, split by dots. Nothing that could end the
// header line or start a path, a user name or a port.
bool isHostName(const QString& text)
{
    if (text.isEmpty() || text.size() > 253 || text.startsWith(QLatin1Char('.'))
        || text.contains(QStringLiteral(".."))) {
        return false;
    }
    for (const QChar c : text) {
        const bool ok = (c >= QLatin1Char('a') && c <= QLatin1Char('z'))
                        || (c >= QLatin1Char('A') && c <= QLatin1Char('Z'))
                        || (c >= QLatin1Char('0') && c <= QLatin1Char('9'))
                        || c == QLatin1Char('-') || c == QLatin1Char('_')
                        || c == QLatin1Char('.');
        if (!ok) {
            return false;
        }
    }
    return true;
}

// An IPv6 literal, written without its zone: the Core reads nothing from
// it, and "fe80::1%en0" is not something QUrl reads inside brackets.
std::optional<QString> ipv6Literal(const QString& text)
{
    if (!text.contains(QLatin1Char(':'))) {
        return std::nullopt;
    }
    QHostAddress address;
    if (!address.setAddress(text) || address.protocol() != QAbstractSocket::IPv6Protocol) {
        return std::nullopt;
    }
    address.setScopeId(QString());
    return address.toString();
}

QString withPort(const QString& host, const QString& port)
{
    return port.isEmpty() ? host : host + QLatin1Char(':') + port;
}

// Case-insensitive comma list membership, for Connection: keep-alive, Upgrade.
bool listHasToken(const QByteArray& value, const QByteArray& token)
{
    const QList<QByteArray> items = value.split(',');
    for (const QByteArray& item : items) {
        if (item.trimmed().compare(token, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

QString StationOpeningGate::canonicalHost(const QString& raw)
{
    const QString value = raw.trimmed();
    if (value.isEmpty()) {
        return {};
    }
    // Bracketed IPv6, with or without a port.
    if (value.startsWith(QLatin1Char('['))) {
        const qsizetype close = value.indexOf(QLatin1Char(']'));
        if (close < 0) {
            return {};
        }
        const std::optional<QString> address = ipv6Literal(value.mid(1, close - 1));
        const QString rest = value.mid(close + 1);
        if (!address.has_value()) {
            return {};
        }
        if (rest.isEmpty()) {
            return QLatin1Char('[') + *address + QLatin1Char(']');
        }
        if (!rest.startsWith(QLatin1Char(':')) || !isPort(rest.mid(1))) {
            return {};
        }
        return withPort(QLatin1Char('[') + *address + QLatin1Char(']'), rest.mid(1));
    }
    const qsizetype colons = value.count(QLatin1Char(':'));
    if (colons >= 2) {
        // Unbracketed IPv6, as Apple's WebSocket API sends it: the whole
        // value when it is an address ("::1", "2001:db8::1"), otherwise an
        // address and a port after the last colon ("2001:db8::1:47910").
        // A value that reads both ways ("::1:8080") is taken whole; the
        // Core routes nothing by Host, so either reading serves.
        if (const std::optional<QString> whole = ipv6Literal(value)) {
            return QLatin1Char('[') + *whole + QLatin1Char(']');
        }
        const qsizetype last = value.lastIndexOf(QLatin1Char(':'));
        const std::optional<QString> address = ipv6Literal(value.left(last));
        const QString port = value.mid(last + 1);
        if (!address.has_value() || !isPort(port)) {
            return {};
        }
        return withPort(QLatin1Char('[') + *address + QLatin1Char(']'), port);
    }
    QString host = value;
    QString port;
    if (colons == 1) {
        const qsizetype colon = value.indexOf(QLatin1Char(':'));
        host = value.left(colon);
        port = value.mid(colon + 1);
        if (!isPort(port)) {
            return {};
        }
    }
    // An IPv4 literal or a host name; an IPv4 literal is also a valid
    // host name by these rules, so one check serves both.
    if (!isHostName(host)) {
        return {};
    }
    return withPort(host, port);
}

StationOpeningGate::Rewrite StationOpeningGate::rewriteRequestHead(const QByteArray& head)
{
    Rewrite result;
    const qsizetype end = head.indexOf("\r\n\r\n");
    if (end < 0 || end + 4 != head.size() || head.size() > kMaxRequestHeadBytes) {
        return result;
    }
    const QList<QByteArray> lines = head.left(end).split('\n');
    const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
    if (requestLine.size() != 3 || requestLine.at(0) != "GET"
        || requestLine.at(2) != "HTTP/1.1" || requestLine.at(1).isEmpty()) {
        return result;
    }

    int hostCount = 0;
    int hostLine = -1;
    QString hostValue;
    bool upgrade = false;
    bool connection = false;
    bool key = false;
    bool version = false;
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).endsWith('\r') ? lines.at(i).chopped(1) : lines.at(i);
        const qsizetype colon = line.indexOf(':');
        if (colon <= 0) {
            return result; // a line that is not a header
        }
        const QByteArray name = line.left(colon).trimmed().toLower();
        const QByteArray value = line.mid(colon + 1).trimmed();
        if (name == "host") {
            ++hostCount;
            hostLine = static_cast<int>(i);
            hostValue = QString::fromLatin1(value);
        } else if (name == "upgrade") {
            upgrade = upgrade || listHasToken(value, "websocket");
        } else if (name == "connection") {
            connection = connection || listHasToken(value, "upgrade");
        } else if (name == "sec-websocket-key") {
            const auto decoded = QByteArray::fromBase64Encoding(value, QByteArray::AbortOnBase64DecodingErrors);
            key = decoded.decodingStatus == QByteArray::Base64DecodingStatus::Ok
                  && decoded.decoded.size() == 16;
        } else if (name == "sec-websocket-version") {
            version = !value.isEmpty();
        }
    }
    // RFC 9112 section 3.2: a request with no Host, or more than one, is
    // answered 400.
    if (hostCount != 1 || !upgrade || !connection || !key || !version) {
        return result;
    }
    const QString canonical = canonicalHost(hostValue);
    if (canonical.isEmpty()) {
        return result;
    }

    QList<QByteArray> rewritten = lines;
    rewritten[hostLine] = QByteArrayLiteral("Host: ") + canonical.toLatin1() + '\r';
    result.head = rewritten.join('\n') + QByteArrayLiteral("\r\n\r\n");
    result.ok = true;
    return result;
}

StationOpeningGate::StationOpeningGate(QWebSocketServer* target, int maxOpenings,
                                       int maxOpeningsPerAddress, AddressKey addressKey,
                                       QObject* parent)
    : QTcpServer(parent)
    , m_target(target)
    , m_maxOpenings(maxOpenings)
    , m_maxOpeningsPerAddress(maxOpeningsPerAddress)
    , m_addressKey(std::move(addressKey))
{
}

StationOpeningGate::~StationOpeningGate()
{
    closeAll();
}

void StationOpeningGate::closeAll()
{
    close();
    // Every socket still here (opening, or refused and closing) is cut
    // loose from this object's handlers first, so none of them runs
    // against a half-destroyed gate, then closed.
    m_pending.clear();
    const QList<QSslSocket*> sockets = findChildren<QSslSocket*>(Qt::FindDirectChildrenOnly);
    for (QSslSocket* socket : sockets) {
        socket->disconnect(this);
        socket->abort();
        delete socket;
    }
    const QList<QTimer*> timers = findChildren<QTimer*>(Qt::FindDirectChildrenOnly);
    for (QTimer* timer : timers) {
        timer->stop();
    }
}

void StationOpeningGate::setOpeningDeadlineMs(int ms)
{
    if (ms > 0) {
        m_deadlineMs = ms;
    }
}

QSslSocket* StationOpeningGate::oldestPending(const QString& key) const
{
    QSslSocket* oldest = nullptr;
    quint64 oldestSerial = 0;
    int count = 0;
    for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it) {
        if (!key.isEmpty() && it->addressKey != key) {
            continue;
        }
        ++count;
        if (oldest == nullptr || it->serial < oldestSerial) {
            oldest = it.key();
            oldestSerial = it->serial;
        }
    }
    const int limit = key.isEmpty() ? m_maxOpenings : m_maxOpeningsPerAddress;
    return count >= limit ? oldest : nullptr;
}

void StationOpeningGate::incomingConnection(qintptr descriptor)
{
    auto* socket = new QSslSocket(this);
    if (!socket->setSocketDescriptor(descriptor)) {
        socket->deleteLater();
        return;
    }
    const QString key = m_addressKey ? m_addressKey(socket->peerAddress().toString()) : QString();

    // At a limit, the OLDEST unfinished opening makes room, never the new
    // one: first that address's oldest, then the oldest of all. A client
    // that redials while its own abandoned dials are still opening gets
    // its newest dial through (that is the one it is waiting on), and
    // openings that never finish cannot keep a device out at all: each
    // new connection pushes the oldest of them out, long before the
    // deadline would.
    if (!key.isEmpty()) {
        if (QSslSocket* oldest = oldestPending(key)) {
            qCInfo(lcStationOpening) << "Closed an unfinished connection from"
                                     << oldest->peerAddress().toString()
                                     << "to make room for a newer one from the same address";
            release(oldest, /*close=*/true);
        }
    }
    if (QSslSocket* oldest = oldestPending(QString())) {
        qCInfo(lcStationOpening) << "Closed the oldest unfinished connection, from"
                                 << oldest->peerAddress().toString()
                                 << ", to make room for a newer one";
        release(oldest, /*close=*/true);
    }

    Pending pending;
    pending.socket = socket;
    pending.addressKey = key;
    pending.serial = ++m_serial;
    pending.deadline = new QTimer(this);
    pending.deadline->setSingleShot(true);
    pending.deadline->setInterval(m_deadlineMs);
    connect(pending.deadline, &QTimer::timeout, this, [this, socket]() {
        if (m_pending.contains(socket)) {
            qCInfo(lcStationOpening) << "A connection did not finish opening in time; closed.";
            release(socket, /*close=*/true);
        }
    });
    m_pending.insert(socket, pending);
    pending.deadline->start();

    connect(socket, &QAbstractSocket::disconnected, this, [this, socket]() {
        if (m_pending.contains(socket)) {
            release(socket, /*close=*/true);
        }
    });
    connect(socket, &QAbstractSocket::errorOccurred, this, [this, socket]() {
        if (m_pending.contains(socket) && !m_pending.value(socket).handedToQt) {
            release(socket, /*close=*/true);
        }
    });
    connect(socket, &QSslSocket::encrypted, this, [this, socket]() { onReadable(socket); });
    connect(socket, &QIODevice::readyRead, this, [this, socket]() {
        if (socket->isEncrypted()) {
            onReadable(socket);
        }
    });

    socket->setSslConfiguration(m_tls);
    socket->startServerEncryption();
}

void StationOpeningGate::onReadable(QSslSocket* socket)
{
    auto it = m_pending.find(socket);
    if (it == m_pending.end() || it->handedToQt) {
        return;
    }
    it->head += socket->readAll();
    const qsizetype end = it->head.indexOf("\r\n\r\n");
    if (end < 0) {
        if (it->head.size() > kMaxRequestHeadBytes) {
            refuse(socket);
        }
        return;
    }
    const QByteArray leftover = it->head.mid(end + 4);
    const Rewrite rewrite = rewriteRequestHead(it->head.left(end + 4));
    if (!rewrite.ok) {
        refuse(socket);
        return;
    }

    // Hand Qt the request as if it had just arrived: the rewritten head,
    // then anything the client sent after it, put back in front of
    // whatever is still unread. QIODevice::ungetChar prepends to the
    // socket's read buffer, so the bytes go back last-first.
    const QByteArray replay = rewrite.head + leftover;
    it->handedToQt = true;
    it->head.clear();
    disconnect(socket, &QIODevice::readyRead, this, nullptr);
    disconnect(socket, &QSslSocket::encrypted, this, nullptr);
    for (qsizetype i = replay.size() - 1; i >= 0; --i) {
        socket->ungetChar(replay.at(i));
    }
    // handleConnection connects its own reader and, finding bytes already
    // buffered, emits readyRead for them (QWebSocketServerPrivate::
    // handleConnection, Qt 6.11). Qt then writes 101 and emits
    // newConnection, or closes a request it will not upgrade; the deadline
    // above still runs until the owner calls markOpened().
    m_target->handleConnection(socket);
}

void StationOpeningGate::refuse(QSslSocket* socket)
{
    release(socket, /*close=*/false);
    socket->write(badRequest());
    socket->disconnectFromHost();
    // Refused and no longer counted, but not left open either: a peer
    // that never lets the close finish is cut off at the deadline.
    QPointer<QSslSocket> guarded(socket);
    QTimer::singleShot(m_deadlineMs, this, [guarded]() {
        if (guarded) {
            guarded->abort();
            guarded->deleteLater();
        }
    });
    connect(socket, &QAbstractSocket::disconnected, socket, &QObject::deleteLater);
}

void StationOpeningGate::release(QSslSocket* socket, bool close)
{
    const auto it = m_pending.find(socket);
    if (it == m_pending.end()) {
        return;
    }
    QTimer* deadline = it->deadline;
    m_pending.erase(it);
    if (deadline != nullptr) {
        deadline->stop();
        deadline->deleteLater();
    }
    if (close) {
        socket->abort();
        socket->deleteLater();
    }
}

void StationOpeningGate::markOpened(const QWebSocket* webSocket)
{
    if (webSocket == nullptr) {
        return;
    }
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
        QSslSocket* socket = it.key();
        if (it->handedToQt && it->socket && socket->peerPort() == webSocket->peerPort()
            && socket->localPort() == webSocket->localPort()
            && socket->peerAddress() == webSocket->peerAddress()) {
            // Opened: Qt now owns the socket (it is the QWebSocket's), so
            // it is only released from the count and from this object's
            // handlers, never closed here.
            socket->disconnect(this);
            release(socket, /*close=*/false);
            return;
        }
    }
}

} // namespace NereusSDR
