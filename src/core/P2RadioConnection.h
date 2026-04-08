#pragma once

#include "RadioConnection.h"

#include <QUdpSocket>
#include <QTimer>
#include <QVector>

#include <array>

namespace NereusSDR {

// Protocol 2 connection for Orion MkII / Saturn (ANAN-G2) radios.
//
// Ported from Thetis ChannelMaster/network.c.
// Uses a SINGLE UDP socket for all communication (matching Thetis listenSock).
// Commands sent TO radio ports 1024-1027, radio responds FROM ports 1025-1041.
// Dispatch based on source port of incoming packets.
//
// Startup: SendStart() = set run=1, CmdGeneral+CmdRx+CmdTx+CmdHighPriority
// Shutdown: SendStop() = set run=0, CmdHighPriority
// Keepalive: CmdGeneral every 500ms (Thetis KeepAliveLoop)
class P2RadioConnection : public RadioConnection {
    Q_OBJECT

public:
    explicit P2RadioConnection(QObject* parent = nullptr);
    ~P2RadioConnection() override;

public slots:
    void init() override;
    void connectToRadio(const NereusSDR::RadioInfo& info) override;
    void disconnect() override;

    void setReceiverFrequency(int receiverIndex, quint64 frequencyHz) override;
    void setTxFrequency(quint64 frequencyHz) override;
    void setActiveReceiverCount(int count) override;
    void setSampleRate(int sampleRate) override;
    void setAttenuator(int dB) override;
    void setPreamp(bool enabled) override;
    void setTxDrive(int level) override;
    void setMox(bool enabled) override;
    void setAntenna(int antennaIndex) override;

private slots:
    void onReadyRead();
    void onKeepAliveTick();
    void onReconnectTimeout();

private:
    // --- P2 Command Senders (match Thetis CmdGeneral/CmdRx/CmdTx/CmdHighPriority) ---
    // Returns bytes sent, or -1 on error.
    qint64 sendCmdGeneral();          // 60 bytes -> radio:1024
    qint64 sendCmdRx();               // 1444 bytes -> radio:1025
    qint64 sendCmdTx();               // 60 bytes -> radio:1026
    qint64 sendCmdHighPriority();     // 1444 bytes -> radio:1027

    // --- P2 Data Parsing ---
    void processIqPacket(const QByteArray& data, int ddcIndex);
    void processHighPriorityStatus(const QByteArray& data);

    // 24-bit big-endian signed → float (matches Thetis conversion exactly)
    static float decodeP2Sample(const unsigned char* p);
    static void writeBE32(QByteArray& buf, int offset, quint32 value);
    static void writeBE16(QByteArray& buf, int offset, quint16 value);

    // --- Constants (from Thetis network.c) ---
    static constexpr quint16 kBasePort = 1024;
    static constexpr int kMaxDdc = 7;              // DDC0-DDC6
    static constexpr int kSamplesPerPacket = 238;  // Thetis: prn->rx[0].spp
    static constexpr int kIqBytesPerSample = 6;    // 3 bytes I + 3 bytes Q
    static constexpr int kIqDataOffset = 16;       // I/Q data starts at byte 16
    static constexpr int kKeepAliveIntervalMs = 500; // Thetis KeepAliveLoop interval

    // --- Single socket (matches Thetis listenSock) ---
    QUdpSocket* m_socket{nullptr};

    // --- Timers ---
    QTimer* m_keepAliveTimer{nullptr};   // Thetis KeepAliveLoop (500ms CmdGeneral)
    QTimer* m_reconnectTimer{nullptr};

    // --- Sequence counters (per command type) ---
    quint32 m_seqGeneral{0};
    quint32 m_seqRx{0};
    quint32 m_seqTx{0};
    quint32 m_seqHighPri{0};

    // --- Hardware state cache ---
    static constexpr int kMaxReceivers = 7;
    std::array<quint64, kMaxReceivers> m_rxFrequencies{};
    quint64 m_txFrequency{14225000};
    int m_activeReceiverCount{1};
    int m_sampleRate{48000};
    int m_attenuatorDb{0};
    bool m_preampOn{false};
    int m_txDriveLevel{0};
    bool m_mox{false};
    bool m_running{false};
    int m_antennaIndex{0};
    bool m_intentionalDisconnect{false};

    // I/Q sample buffers (one per DDC, reused to avoid allocation)
    std::array<QVector<float>, kMaxDdc> m_iqBuffers;

    // Sequence tracking for incoming I/Q packets
    std::array<quint32, kMaxDdc> m_lastIqSeq{};
    std::array<bool, kMaxDdc> m_firstIqPacket{};
    int m_totalIqPackets{0};
};

} // namespace NereusSDR
