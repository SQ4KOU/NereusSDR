// =================================================================
// src/core/session/SessionTransport.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R2 Task 18.
// See SessionTransport.h for the design rationale.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-08  J.J. Boyd / KG4VCF  Remote daemon R2 Task 18: session
//                                    transport seam and its QWebSocket
//                                    implementation. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
// =================================================================

#include "core/session/SessionTransport.h"

#include <QWebSocket>

namespace NereusSDR {

WebSocketTransport::WebSocketTransport(QWebSocket* socket, QObject* parent)
    : SessionTransport(parent)
    , m_socket(socket)
{
    if (m_socket == nullptr) {
        return;
    }
    m_socket->setParent(this);

    connect(m_socket, &QWebSocket::textMessageReceived, this,
            [this](const QString& message) { emit textReceived(message.toUtf8()); });

    // QWebSocket::pong carries the round-trip time and the payload we sent.
    // Neither is used: the fact that it arrived at all is the entire
    // signal, and reading elapsedTime here would invite treating a slow
    // link as a dead one, which is exactly the false positive the
    // miss-count design avoids.
    connect(m_socket, &QWebSocket::pong, this,
            [this](quint64, const QByteArray&) { emit pongReceived(); });

    connect(m_socket, &QWebSocket::disconnected, this,
            [this]() { emit closed(); });
}

WebSocketTransport::~WebSocketTransport() = default;

void WebSocketTransport::sendText(const QByteArray& wire)
{
    if (!isOpen()) {
        return;
    }
    m_socket->sendTextMessage(QString::fromUtf8(wire));
}

void WebSocketTransport::ping()
{
    if (!isOpen()) {
        return;
    }
    // Qt6's QWebSocket::ping builds the RFC 6455 frame; the payload is
    // echoed back verbatim in the pong. Kept short and constant: it is
    // never inspected (see the pong connect above), and a payload that
    // varied per ping would only invite someone to start matching them up
    // and rebuilding a round-trip timer this design deliberately does not
    // have.
    m_socket->ping(QByteArrayLiteral("nereus"));
}

void WebSocketTransport::closeLink(const QString& reason)
{
    if (m_socket == nullptr || m_closing) {
        return;
    }
    m_closing = true;
    // CloseCodeNormal, not an error code: every close this class issues is
    // a deliberate protocol decision (version refusal, failed auth,
    // preemption, heartbeat timeout) that has already been explained to
    // the peer in a SessionEnd or AuthResult message. The reason string
    // rides along for a peer that is reading close frames rather than
    // messages.
    m_socket->close(QWebSocketProtocol::CloseCodeNormal, reason);
}

bool WebSocketTransport::isOpen() const
{
    return m_socket != nullptr && m_socket->state() == QAbstractSocket::ConnectedState;
}

QString WebSocketTransport::peerDescription() const
{
    if (m_socket == nullptr) {
        return QStringLiteral("<detached>");
    }
    return QStringLiteral("%1:%2")
        .arg(m_socket->peerAddress().toString())
        .arg(m_socket->peerPort());
}

} // namespace NereusSDR
