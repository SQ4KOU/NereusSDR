#include "RadioConnection.h"

#include <QDebug>
#include <QNetworkDatagram>

namespace NereusSDR {

RadioConnection::RadioConnection(QObject* parent)
    : QObject(parent)
{
    m_reconnectTimer.setInterval(3000);
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, [this]() {
        if (!m_intentionalDisconnect && m_state == ConnectionState::Disconnected) {
            qDebug() << "Attempting reconnect to" << m_radioInfo.displayName();
            connectToRadio(m_radioInfo);
        }
    });
}

RadioConnection::~RadioConnection()
{
    disconnect();
}

void RadioConnection::connectToRadio(const RadioInfo& info)
{
    if (m_state == ConnectionState::Connected || m_state == ConnectionState::Connecting) {
        disconnect();
    }

    m_radioInfo = info;
    m_intentionalDisconnect = false;
    setState(ConnectionState::Connecting);

    if (info.protocol == 1) {
        connectProtocol1();
    } else {
        connectProtocol2();
    }
}

void RadioConnection::disconnect()
{
    m_intentionalDisconnect = true;
    m_reconnectTimer.stop();

    if (m_udpSocket) {
        m_udpSocket->close();
        delete m_udpSocket;
        m_udpSocket = nullptr;
    }

    if (m_tcpSocket) {
        m_tcpSocket->close();
        delete m_tcpSocket;
        m_tcpSocket = nullptr;
    }

    setState(ConnectionState::Disconnected);
}

void RadioConnection::sendUdpData(const QByteArray& data)
{
    if (m_udpSocket && m_state == ConnectionState::Connected) {
        m_udpSocket->writeDatagram(data, m_radioInfo.address, m_radioInfo.port);
    }
}

void RadioConnection::sendCommand(const QByteArray& command)
{
    if (m_radioInfo.protocol == 2 && m_tcpSocket && m_tcpSocket->state() == QAbstractSocket::ConnectedState) {
        m_tcpSocket->write(command);
    }
}

void RadioConnection::setState(ConnectionState newState)
{
    if (m_state != newState) {
        m_state = newState;
        emit connectionStateChanged(m_state);
    }
}

void RadioConnection::connectProtocol1()
{
    m_udpSocket = new QUdpSocket(this);
    if (!m_udpSocket->bind(QHostAddress::Any, 0)) {
        qWarning() << "Failed to bind UDP socket for Protocol 1";
        setState(ConnectionState::Error);
        emit errorOccurred("Failed to bind UDP socket");
        return;
    }

    connect(m_udpSocket, &QUdpSocket::readyRead, this, &RadioConnection::onUdpReadyRead);

    // Send start command (Protocol 1: 0xEF 0xFE 0x04 + 60 zero bytes with start flag)
    QByteArray startPacket(64, 0);
    startPacket[0] = static_cast<char>(0xEF);
    startPacket[1] = static_cast<char>(0xFE);
    startPacket[2] = static_cast<char>(0x04);
    startPacket[3] = static_cast<char>(0x01);  // start

    m_udpSocket->writeDatagram(startPacket, m_radioInfo.address, m_radioInfo.port);

    setState(ConnectionState::Connected);
    qDebug() << "Protocol 1 connection established to" << m_radioInfo.displayName()
             << "at" << m_radioInfo.address.toString();
}

void RadioConnection::connectProtocol2()
{
    // Protocol 2: TCP for commands + UDP for data
    m_tcpSocket = new QTcpSocket(this);
    connect(m_tcpSocket, &QTcpSocket::connected, this, &RadioConnection::onTcpConnected);
    connect(m_tcpSocket, &QTcpSocket::readyRead, this, &RadioConnection::onTcpReadyRead);
    connect(m_tcpSocket, &QTcpSocket::errorOccurred, this, &RadioConnection::onTcpError);

    m_udpSocket = new QUdpSocket(this);
    if (!m_udpSocket->bind(QHostAddress::Any, 0)) {
        qWarning() << "Failed to bind UDP socket for Protocol 2";
    }
    connect(m_udpSocket, &QUdpSocket::readyRead, this, &RadioConnection::onUdpReadyRead);

    // Connect TCP command channel
    m_tcpSocket->connectToHost(m_radioInfo.address, m_radioInfo.port);
}

void RadioConnection::onUdpReadyRead()
{
    while (m_udpSocket && m_udpSocket->hasPendingDatagrams()) {
        QNetworkDatagram datagram = m_udpSocket->receiveDatagram();
        emit dataReceived(datagram.data());
    }
}

void RadioConnection::onTcpConnected()
{
    setState(ConnectionState::Connected);
    qDebug() << "Protocol 2 TCP connection established to" << m_radioInfo.displayName();
}

void RadioConnection::onTcpReadyRead()
{
    if (m_tcpSocket) {
        QByteArray data = m_tcpSocket->readAll();
        emit commandResponse(data);
    }
}

void RadioConnection::onTcpError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error);
    QString msg = m_tcpSocket ? m_tcpSocket->errorString() : "Unknown error";
    qWarning() << "TCP error:" << msg;

    setState(ConnectionState::Error);
    emit errorOccurred(msg);

    if (!m_intentionalDisconnect) {
        m_reconnectTimer.start();
    }
}

} // namespace NereusSDR
