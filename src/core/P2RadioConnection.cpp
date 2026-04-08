#include "P2RadioConnection.h"
#include "LogCategories.h"

#include <QNetworkDatagram>
#include <QVariant>
#include <QtEndian>

namespace NereusSDR {

P2RadioConnection::P2RadioConnection(QObject* parent)
    : RadioConnection(parent)
{
    m_rxFrequencies.fill(14225000);
    m_firstIqPacket.fill(true);
    m_lastIqSeq.fill(0);

    for (auto& buf : m_iqBuffers) {
        buf.resize(kSamplesPerPacket * 2);  // I + Q per sample
    }
}

P2RadioConnection::~P2RadioConnection()
{
    if (m_running) {
        disconnect();
    }
}

// --- Thread Lifecycle ---

void P2RadioConnection::init()
{
    // Thetis uses a SINGLE UDP socket for ALL P2 communication.
    // Commands are sent TO radio ports 1024-1027.
    // Radio sends responses BACK to this socket's bound port.
    // Dispatch is based on the source port of incoming packets.
    m_socket = new QUdpSocket(this);

    // Bind to any available port — radio responds to wherever we send from
    if (!m_socket->bind(QHostAddress::Any, 0)) {
        qCWarning(lcConnection) << "P2: Failed to bind UDP socket";
        return;
    }

    // Match Thetis socket buffer sizing: 0xfa000 = 1,024,000 bytes
    m_socket->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, QVariant(0xfa000));
    m_socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, QVariant(0xfa000));

    connect(m_socket, &QUdpSocket::readyRead, this, &P2RadioConnection::onReadyRead);

    // High-priority timer — Thetis KeepAlive sends CmdGeneral every 500ms
    m_keepAliveTimer = new QTimer(this);
    m_keepAliveTimer->setInterval(kKeepAliveIntervalMs);
    connect(m_keepAliveTimer, &QTimer::timeout, this, &P2RadioConnection::onKeepAliveTick);

    // Reconnect timer
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setInterval(3000);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &P2RadioConnection::onReconnectTimeout);

    qCDebug(lcConnection) << "P2: init() on worker thread, socket port:"
                          << m_socket->localPort();
}

// --- Connection Lifecycle ---
// Matches Thetis: SendStart() = set run=1, send CmdGeneral+CmdRx+CmdTx+CmdHighPriority

void P2RadioConnection::connectToRadio(const RadioInfo& info)
{
    if (m_running) {
        disconnect();
    }

    m_radioInfo = info;
    m_intentionalDisconnect = false;
    m_totalIqPackets = 0;

    // Reset sequence counters
    m_seqGeneral = 0;
    m_seqRx = 0;
    m_seqTx = 0;
    m_seqHighPri = 0;
    m_firstIqPacket.fill(true);

    setState(ConnectionState::Connecting);

    qCDebug(lcConnection) << "P2: Connecting to" << info.displayName()
                          << "at" << info.address.toString();

    // Thetis SendStart() sequence: set run=1 then send all 4 command packets
    m_running = true;

    qCDebug(lcConnection) << "P2: Sending commands to" << m_radioInfo.address.toString()
                          << "from socket port" << m_socket->localPort();

    qint64 r1 = sendCmdGeneral();
    qint64 r2 = sendCmdRx();
    qint64 r3 = sendCmdTx();
    qint64 r4 = sendCmdHighPriority();

    qCDebug(lcConnection) << "P2: SendStart results: General=" << r1
                          << "Rx=" << r2 << "Tx=" << r3 << "HighPri=" << r4;

    // Start keepalive timer (Thetis KeepAliveLoop sends CmdGeneral every 500ms)
    m_keepAliveTimer->start();

    setState(ConnectionState::Connected);

    qCDebug(lcConnection) << "P2: Connected, streaming started";
}

// Matches Thetis: SendStop() = set run=0, send CmdHighPriority
void P2RadioConnection::disconnect()
{
    m_intentionalDisconnect = true;

    if (m_keepAliveTimer) {
        m_keepAliveTimer->stop();
    }
    if (m_reconnectTimer) {
        m_reconnectTimer->stop();
    }

    if (m_running && m_socket && !m_radioInfo.address.isNull()) {
        // Thetis SendStop(): set run=0, send CmdHighPriority
        m_running = false;
        sendCmdHighPriority();
        qCDebug(lcConnection) << "P2: Stop command sent";
    }

    m_running = false;

    // Close socket so the event loop can exit
    if (m_socket) {
        m_socket->close();
    }

    setState(ConnectionState::Disconnected);

    qCDebug(lcConnection) << "P2: Disconnected. Total I/Q packets received:"
                          << m_totalIqPackets;
}

// --- Hardware Control Slots ---

void P2RadioConnection::setReceiverFrequency(int receiverIndex, quint64 frequencyHz)
{
    if (receiverIndex < 0 || receiverIndex >= kMaxReceivers) {
        return;
    }
    m_rxFrequencies[receiverIndex] = frequencyHz;

    if (m_running) {
        sendCmdHighPriority();
    }
}

void P2RadioConnection::setTxFrequency(quint64 frequencyHz)
{
    m_txFrequency = frequencyHz;
    if (m_running) {
        sendCmdHighPriority();
    }
}

void P2RadioConnection::setActiveReceiverCount(int count)
{
    m_activeReceiverCount = qBound(1, count, kMaxReceivers);
    if (m_running) {
        sendCmdRx();
    }
}

void P2RadioConnection::setSampleRate(int sampleRate)
{
    m_sampleRate = sampleRate;
    if (m_running) {
        sendCmdRx();
    }
}

void P2RadioConnection::setAttenuator(int dB)
{
    m_attenuatorDb = qBound(0, dB, 31);
    if (m_running) {
        sendCmdHighPriority();
    }
}

void P2RadioConnection::setPreamp(bool enabled)
{
    m_preampOn = enabled;
    if (m_running) {
        sendCmdHighPriority();
    }
}

void P2RadioConnection::setTxDrive(int level)
{
    m_txDriveLevel = qBound(0, level, 255);
    if (m_running) {
        sendCmdHighPriority();
    }
}

void P2RadioConnection::setMox(bool enabled)
{
    m_mox = enabled;
    if (m_running) {
        sendCmdHighPriority();
    }
}

void P2RadioConnection::setAntenna(int antennaIndex)
{
    m_antennaIndex = antennaIndex;
    if (m_running) {
        sendCmdHighPriority();
    }
}

// --- UDP Reception ---
// Matches Thetis ReadUDPFrame(): single socket, dispatch by source port

void P2RadioConnection::onReadyRead()
{
    while (m_socket && m_socket->hasPendingDatagrams()) {
        QNetworkDatagram datagram = m_socket->receiveDatagram();
        QByteArray data = datagram.data();
        quint16 sourcePort = datagram.senderPort();

        // Dispatch based on source port offset from base (matching Thetis)
        int portIdx = sourcePort - (kBasePort + 1);  // offset from 1025

        switch (portIdx) {
        case 0:  // 1025: High Priority status feedback (60 bytes)
            processHighPriorityStatus(data);
            break;
        case 1:  // 1026: Mic samples (not used yet)
            break;
        case 10: // 1035: DDC0 I/Q
        case 11: // 1036: DDC1 I/Q
        case 12: // 1037: DDC2 I/Q
        case 13: // 1038: DDC3 I/Q
        case 14: // 1039: DDC4 I/Q
        case 15: // 1040: DDC5 I/Q
        case 16: // 1041: DDC6 I/Q
            processIqPacket(data, portIdx - 10);
            break;
        default:
            if (data.size() > 4) {
                qCDebug(lcProtocol) << "P2: Packet from port" << sourcePort
                                    << "size:" << data.size();
            }
            break;
        }
    }
}

void P2RadioConnection::onKeepAliveTick()
{
    // Thetis KeepAliveLoop: send CmdGeneral every 500ms when running
    if (m_running && !m_radioInfo.address.isNull()) {
        sendCmdGeneral();
    }
}

void P2RadioConnection::onReconnectTimeout()
{
    if (!m_intentionalDisconnect && !m_radioInfo.address.isNull()) {
        qCDebug(lcConnection) << "P2: Attempting reconnect to"
                              << m_radioInfo.displayName();
        connectToRadio(m_radioInfo);
    }
}

// --- Command Senders ---
// Each sends via the single socket (listenSock equivalent) to the radio's
// specific port. Matches Thetis sendPacket() which uses sendto() with
// destination = radio IP + port.

qint64 P2RadioConnection::sendCmdGeneral()
{
    QByteArray pkt(60, 0);
    writeBE32(pkt, 0, m_seqGeneral++);

    pkt[4] = 0x00;  // Command type: general

    // Port assignments — matches Thetis CmdGeneral() exactly
    // PC outbound source ports (radio receives FROM these)
    quint16 base = kBasePort + 1;  // 1025
    writeBE16(pkt, 5, base);          // Rx Specific #1025
    writeBE16(pkt, 7, base + 1);      // Tx Specific #1026
    writeBE16(pkt, 9, base + 2);      // High Priority from PC #1027
    writeBE16(pkt, 13, base + 3);     // Rx Audio #1028
    writeBE16(pkt, 15, base + 4);     // Tx0 IQ #1029

    // Radio outbound source ports (radio sends FROM these)
    writeBE16(pkt, 11, base);         // High Priority to PC #1025
    writeBE16(pkt, 17, base + 10);    // Rx0 DDC IQ #1035
    writeBE16(pkt, 19, base + 1);     // Mic Samples #1026
    writeBE16(pkt, 21, base + 2);     // Wideband ADC0 #1027

    // Watchdog timer (byte 38) — Thetis sets prn->wdt
    pkt[38] = 0;  // 0 = no watchdog timeout

    return m_socket->writeDatagram(pkt, m_radioInfo.address, kBasePort);
}

qint64 P2RadioConnection::sendCmdRx()
{
    QByteArray pkt(1444, 0);
    writeBE32(pkt, 0, m_seqRx++);

    // Byte 4: Number of ADCs
    pkt[4] = static_cast<char>(m_radioInfo.adcCount);

    // Byte 7: RX enable bitmask (bits 0-6 for DDC0-DDC6)
    quint8 rxEnable = 0;
    for (int i = 0; i < m_activeReceiverCount && i < kMaxReceivers; ++i) {
        rxEnable |= (1 << i);
    }
    pkt[7] = static_cast<char>(rxEnable);

    // Sample rate encoding: value in kHz
    quint16 rateCode = static_cast<quint16>(m_sampleRate / 1000);

    // Per-RX configuration (6 bytes each, starting at byte 17)
    for (int i = 0; i < m_activeReceiverCount && i < kMaxReceivers; ++i) {
        int offset = 17 + (i * 6);
        pkt[offset] = 0;                          // ADC selection (0 = ADC0)
        writeBE16(pkt, offset + 1, rateCode);     // Sampling rate
        pkt[offset + 3] = 0;                      // Reserved
        pkt[offset + 4] = 24;                     // Bit depth (24-bit)
        pkt[offset + 5] = 0;                      // Reserved
    }

    return m_socket->writeDatagram(pkt, m_radioInfo.address, kBasePort + 1);
}

qint64 P2RadioConnection::sendCmdTx()
{
    QByteArray pkt(60, 0);
    writeBE32(pkt, 0, m_seqTx++);

    pkt[4] = 1;  // Number of DACs

    // TX sampling rate
    quint16 txRate = static_cast<quint16>(m_sampleRate / 1000);
    writeBE16(pkt, 14, txRate);

    return m_socket->writeDatagram(pkt, m_radioInfo.address, kBasePort + 2);
}

qint64 P2RadioConnection::sendCmdHighPriority()
{
    QByteArray pkt(1444, 0);
    writeBE32(pkt, 0, m_seqHighPri++);

    // Byte 4: Run/PTT — matches Thetis: (ptt << 1 | run) & 0xff
    quint8 runPtt = 0;
    if (m_running) {
        runPtt |= 0x01;  // bit 0: run
    }
    if (m_mox) {
        runPtt |= 0x02;  // bit 1: PTT
    }
    pkt[4] = static_cast<char>(runPtt);

    // RX Frequencies: 32-bit big-endian Hz values
    // Bytes 9-12: RX0, 13-16: RX1, etc. (4 bytes each)
    for (int i = 0; i < kMaxReceivers; ++i) {
        int offset = 9 + (i * 4);
        writeBE32(pkt, offset, static_cast<quint32>(m_rxFrequencies[i]));
    }

    // TX0 Frequency: bytes 329-332
    writeBE32(pkt, 329, static_cast<quint32>(m_txFrequency));

    // TX0 Drive Level: byte 345
    pkt[345] = static_cast<char>(m_txDriveLevel);

    // Step attenuators: bytes 1441-1443 (ADC2, ADC1, ADC0)
    pkt[1443] = static_cast<char>(m_attenuatorDb);  // ADC0

    return m_socket->writeDatagram(pkt, m_radioInfo.address, kBasePort + 3);
}

// --- Data Parsing ---
// Matches Thetis ReadUDPFrame() DDC processing

void P2RadioConnection::processIqPacket(const QByteArray& data, int ddcIndex)
{
    if (ddcIndex < 0 || ddcIndex >= kMaxDdc) {
        return;
    }

    // P2 I/Q packet: 1444 bytes (matching Thetis BUFLEN)
    // Bytes 0-3: sequence number
    // Bytes 4-15: reserved/metadata
    // Bytes 16-1443: I/Q data (238 samples x 6 bytes = 1428 bytes)
    if (data.size() != 1444) {
        qCDebug(lcProtocol) << "P2: Wrong I/Q packet size for DDC" << ddcIndex
                            << "size:" << data.size() << "(expected 1444)";
        return;
    }

    const auto* raw = reinterpret_cast<const unsigned char*>(data.constData());

    // Sequence tracking — matches Thetis rx_in_seq_no / rx_in_seq_err
    quint32 seq = qFromBigEndian<quint32>(raw);
    if (m_firstIqPacket[ddcIndex]) {
        m_firstIqPacket[ddcIndex] = false;
    } else if (seq != m_lastIqSeq[ddcIndex] + 1 && seq != 0) {
        int dropped = static_cast<int>(seq - m_lastIqSeq[ddcIndex] - 1);
        if (dropped > 0 && dropped < 1000) {
            qCDebug(lcProtocol) << "P2: DDC" << ddcIndex
                                << "seq error: this" << seq
                                << "last" << m_lastIqSeq[ddcIndex];
        }
    }
    m_lastIqSeq[ddcIndex] = seq;

    // Parse I/Q samples — matches Thetis ReadThreadMainLoop() conversion
    // Thetis: prn->RxReadBufp[2*i+0] = const_1_div_2147483648_ *
    //           (double)(readbuf[k+0]<<24 | readbuf[k+1]<<16 | readbuf[k+2]<<8)
    QVector<float>& buf = m_iqBuffers[ddcIndex];
    if (buf.size() != kSamplesPerPacket * 2) {
        buf.resize(kSamplesPerPacket * 2);
    }

    const unsigned char* iqStart = raw + kIqDataOffset;
    for (int i = 0; i < kSamplesPerPacket; ++i) {
        const unsigned char* samp = iqStart + (i * kIqBytesPerSample);
        buf[i * 2]     = decodeP2Sample(samp);       // I
        buf[i * 2 + 1] = decodeP2Sample(samp + 3);   // Q
    }

    ++m_totalIqPackets;

    // Log first packet and periodically
    if (m_totalIqPackets == 1) {
        qCDebug(lcConnection) << "P2: First I/Q packet! DDC" << ddcIndex
                              << "seq:" << seq << "- radio is streaming";
    } else if (m_totalIqPackets % 10000 == 0) {
        qCDebug(lcProtocol) << "P2: I/Q packets:" << m_totalIqPackets;
    }

    emit iqDataReceived(ddcIndex, buf);
}

void P2RadioConnection::processHighPriorityStatus(const QByteArray& data)
{
    if (data.size() < 60) {
        return;
    }

    const auto* raw = reinterpret_cast<const unsigned char*>(data.constData());

    // Bytes 49-50: forward power
    quint16 fwdRaw = qFromBigEndian<quint16>(raw + 49);
    // Bytes 51-52: reverse power
    quint16 revRaw = qFromBigEndian<quint16>(raw + 51);
    // Bytes 57-58: supply voltage
    quint16 voltRaw = qFromBigEndian<quint16>(raw + 57);
    // Bytes 59-60: PA current
    quint16 paRaw = (data.size() > 60) ? qFromBigEndian<quint16>(raw + 59) : 0;

    float fwdPower = static_cast<float>(fwdRaw) / 4095.0f;
    float revPower = static_cast<float>(revRaw) / 4095.0f;
    float supplyVoltage = static_cast<float>(voltRaw) * 3.3f / 4095.0f * 11.0f;
    float paCurrent = static_cast<float>(paRaw) * 3.3f / 4095.0f / 0.04f;

    emit meterDataReceived(fwdPower, revPower, supplyVoltage, paCurrent);

    // ADC overflow: byte 5
    if (data.size() > 5 && (raw[5] & 0x01)) {
        emit adcOverflow(0);
    }
    if (data.size() > 5 && (raw[5] & 0x02)) {
        emit adcOverflow(1);
    }
}

// --- Utility ---

float P2RadioConnection::decodeP2Sample(const unsigned char* p)
{
    // Matches Thetis exactly:
    // (double)(readbuf[k+0]<<24 | readbuf[k+1]<<16 | readbuf[k+2]<<8)
    //   * const_1_div_2147483648_
    qint32 val = (static_cast<qint32>(p[0]) << 24)
               | (static_cast<qint32>(p[1]) << 16)
               | (static_cast<qint32>(p[2]) << 8);
    return static_cast<float>(val) / 2147483648.0f;
}

void P2RadioConnection::writeBE32(QByteArray& buf, int offset, quint32 value)
{
    buf[offset]     = static_cast<char>((value >> 24) & 0xFF);
    buf[offset + 1] = static_cast<char>((value >> 16) & 0xFF);
    buf[offset + 2] = static_cast<char>((value >> 8)  & 0xFF);
    buf[offset + 3] = static_cast<char>( value        & 0xFF);
}

void P2RadioConnection::writeBE16(QByteArray& buf, int offset, quint16 value)
{
    buf[offset]     = static_cast<char>((value >> 8) & 0xFF);
    buf[offset + 1] = static_cast<char>( value       & 0xFF);
}

} // namespace NereusSDR
