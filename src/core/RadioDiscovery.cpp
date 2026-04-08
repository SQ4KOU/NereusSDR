#include "RadioDiscovery.h"

#include <QDateTime>
#include <QNetworkDatagram>
#include <QDebug>

namespace NereusSDR {

QString RadioInfo::displayName() const
{
    if (!name.isEmpty()) {
        return name;
    }
    return boardTypeName(boardType) + " (" + macAddress + ")";
}

QString RadioInfo::boardTypeName(int type)
{
    switch (type) {
    case 0:  return "Metis";
    case 1:  return "Hermes";
    case 2:  return "Griffin";
    case 4:  return "Angelia";
    case 5:  return "Orion";
    case 6:  return "Hermes Lite";
    case 10: return "Orion MkII";
    default: return QString("Unknown (%1)").arg(type);
    }
}

RadioDiscovery::RadioDiscovery(QObject* parent)
    : QObject(parent)
{
    connect(&m_discoveryTimer, &QTimer::timeout, this, &RadioDiscovery::sendDiscoveryPacket);
    connect(&m_staleTimer, &QTimer::timeout, this, &RadioDiscovery::onStaleCheck);
}

RadioDiscovery::~RadioDiscovery()
{
    stopDiscovery();
}

void RadioDiscovery::startDiscovery()
{
    if (m_socket) {
        return;
    }

    m_socket = new QUdpSocket(this);
    if (!m_socket->bind(QHostAddress::Any, 0)) {
        qWarning() << "RadioDiscovery: failed to bind UDP socket";
        delete m_socket;
        m_socket = nullptr;
        return;
    }

    connect(m_socket, &QUdpSocket::readyRead, this, &RadioDiscovery::onReadyRead);

    sendDiscoveryPacket();
    m_discoveryTimer.start(2000);
    m_staleTimer.start(kStaleTimeoutMs);

    qDebug() << "RadioDiscovery: started on port" << m_socket->localPort();
}

void RadioDiscovery::stopDiscovery()
{
    m_discoveryTimer.stop();
    m_staleTimer.stop();

    if (m_socket) {
        m_socket->close();
        delete m_socket;
        m_socket = nullptr;
    }
}

QList<RadioInfo> RadioDiscovery::discoveredRadios() const
{
    return m_radios.values();
}

void RadioDiscovery::sendDiscoveryPacket()
{
    if (!m_socket) {
        return;
    }

    // OpenHPSDR Protocol 1 discovery packet:
    // 63 bytes: 0xEF 0xFE 0x02 followed by 60 zero bytes
    QByteArray packet(63, 0);
    packet[0] = static_cast<char>(0xEF);
    packet[1] = static_cast<char>(0xFE);
    packet[2] = static_cast<char>(0x02);

    m_socket->writeDatagram(packet, QHostAddress::Broadcast, kDiscoveryPort);
}

void RadioDiscovery::onReadyRead()
{
    while (m_socket && m_socket->hasPendingDatagrams()) {
        QNetworkDatagram datagram = m_socket->receiveDatagram();
        QByteArray data = datagram.data();

        // Minimum discovery response: 60 bytes, starts with 0xEF 0xFE
        if (data.size() < 60) {
            continue;
        }
        if (static_cast<quint8>(data[0]) != 0xEF || static_cast<quint8>(data[1]) != 0xFE) {
            continue;
        }

        // Byte 2: status (0x02 = discovery response, 0x03 = in use)
        quint8 status = static_cast<quint8>(data[2]);
        if (status != 0x02 && status != 0x03) {
            continue;
        }

        // Parse discovery response
        RadioInfo info;
        info.address = datagram.senderAddress();
        info.port = kDiscoveryPort;
        info.inUse = (status == 0x03);

        // MAC address: bytes 3-8
        info.macAddress = QString("%1:%2:%3:%4:%5:%6")
            .arg(static_cast<quint8>(data[3]), 2, 16, QChar('0'))
            .arg(static_cast<quint8>(data[4]), 2, 16, QChar('0'))
            .arg(static_cast<quint8>(data[5]), 2, 16, QChar('0'))
            .arg(static_cast<quint8>(data[6]), 2, 16, QChar('0'))
            .arg(static_cast<quint8>(data[7]), 2, 16, QChar('0'))
            .arg(static_cast<quint8>(data[8]), 2, 16, QChar('0'))
            .toUpper();

        // Board type: byte 10
        info.boardType = static_cast<quint8>(data[10]);
        info.name = RadioInfo::boardTypeName(info.boardType);

        // Firmware version: byte 9
        info.firmwareVersion = static_cast<quint8>(data[9]);

        // Protocol 1 by default; Protocol 2 radios respond differently
        info.protocol = 1;

        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        m_lastSeen[info.macAddress] = now;

        if (!m_radios.contains(info.macAddress)) {
            m_radios.insert(info.macAddress, info);
            emit radioDiscovered(info);
        } else {
            m_radios[info.macAddress] = info;
            emit radioUpdated(info);
        }
    }
}

void RadioDiscovery::onStaleCheck()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QStringList stale;

    for (auto it = m_lastSeen.constBegin(); it != m_lastSeen.constEnd(); ++it) {
        if (now - it.value() > kStaleTimeoutMs) {
            stale.append(it.key());
        }
    }

    for (const QString& mac : stale) {
        m_radios.remove(mac);
        m_lastSeen.remove(mac);
        emit radioLost(mac);
    }
}

} // namespace NereusSDR
