// tests/fakes/LoopbackTransport.cpp
//
// no-port-check: NereusSDR-original test fixture.
// See LoopbackTransport.h.

#include "LoopbackTransport.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QTimer>

namespace NereusSDR::Test {

LoopbackTransport::LoopbackTransport(const QString& description, QObject* parent)
    : NereusSDR::SessionTransport(parent)
    , m_description(description)
{
}

void LoopbackTransport::linkTo(LoopbackTransport* peer)
{
    m_peer = peer;
    if (peer != nullptr) {
        peer->m_peer = this;
    }
}

void LoopbackTransport::sendText(const QByteArray& wire)
{
    if (!m_open || m_peer.isNull()) {
        return;
    }
    // Queued, not direct. A real socket never delivers inside the send
    // call, and a direct hop here would let a handshake reply run inside
    // the middle of the send that provoked it -- reentrancy the production
    // code is not written for and a real transport never produces.
    LoopbackTransport* peer = m_peer.data();
    QMetaObject::invokeMethod(
        peer, [peer, wire]() { peer->deliver(wire); }, Qt::QueuedConnection);
}

void LoopbackTransport::deliver(const QByteArray& wire)
{
    if (!m_open) {
        return;
    }
    m_received.append(wire);
    emit textReceived(wire);
}

void LoopbackTransport::ping()
{
    if (!m_open || m_peer.isNull()) {
        return;
    }
    LoopbackTransport* peer = m_peer.data();
    QMetaObject::invokeMethod(
        peer, [peer]() { peer->receivePing(); }, Qt::QueuedConnection);
}

void LoopbackTransport::receivePing()
{
    ++m_pingsSeen;
    if (!m_answersPings || m_peer.isNull() || !m_open) {
        // The whole point of this fake: the link is still nominally up and
        // the ping arrived, but nothing comes back. That is what a dead
        // laptop looks like from the other end, and a real loopback
        // QWebSocket cannot produce it.
        return;
    }
    LoopbackTransport* peer = m_peer.data();
    QMetaObject::invokeMethod(
        peer, [peer]() { emit peer->pongReceived(); }, Qt::QueuedConnection);
}

void LoopbackTransport::closeLink(const QString& reason)
{
    if (!m_open) {
        return;
    }
    m_open = false;
    m_closeReason = reason;
    emit closed();
    if (!m_peer.isNull()) {
        LoopbackTransport* peer = m_peer.data();
        QMetaObject::invokeMethod(
            peer, [peer, reason]() { peer->closeLink(reason); }, Qt::QueuedConnection);
    }
}

bool LoopbackTransport::isOpen() const
{
    return m_open;
}

QString LoopbackTransport::peerDescription() const
{
    return m_description;
}

QList<QByteArray> LoopbackTransport::receivedKinds() const
{
    QList<QByteArray> kinds;
    kinds.reserve(m_received.size());
    for (const QByteArray& wire : m_received) {
        const QJsonDocument doc = QJsonDocument::fromJson(wire);
        kinds.append(doc.object().value(QStringLiteral("type")).toString().toUtf8());
    }
    return kinds;
}

} // namespace NereusSDR::Test
