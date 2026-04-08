#pragma once

#include "RadioConnection.h"

#include <QUdpSocket>
#include <QTimer>
#include <QVector>

#include <array>

namespace NereusSDR {

// Protocol 2 connection for Orion MkII / Saturn (ANAN-G2) radios.
//
// P2 uses UDP-only communication on multiple dedicated ports:
//   PC→Radio: 1024 (General), 1025 (RX config), 1026 (TX config), 1027 (HighPri)
//   Radio→PC: 1025 (HighPri status), 1028 (RX audio), 1035-1041 (DDC I/Q)
//
// Start sequence: CmdGeneral → CmdRx → CmdTx → CmdHighPriority(run=1)
// Stop:           CmdHighPriority(run=0)
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
    void onDataReady();
    void onHighPriorityStatusReady();
    void onHighPriorityTick();
    void onReconnectTimeout();

private:
    // --- P2 Command Packet Builders ---
    // Each returns a fixed-size packet ready to send.
    QByteArray buildCmdGeneral();          // 60 bytes → port 1024
    QByteArray buildCmdRx();               // 1444 bytes → port 1025
    QByteArray buildCmdTx();               // 60 bytes → port 1026
    QByteArray buildCmdHighPriority();     // 1444 bytes → port 1027

    // Send all configuration commands (used during connect and resync)
    void sendAllCommands();

    // --- P2 Data Parsing ---
    void processIqPacket(const QByteArray& data, int ddcIndex);
    void processHighPriorityStatus(const QByteArray& data);

    // 24-bit big-endian signed → float in [-1.0, 1.0]
    static float decodeP2Sample(const unsigned char* p);

    // Write a 32-bit big-endian unsigned value into a byte array at offset
    static void writeBE32(QByteArray& buf, int offset, quint32 value);
    // Write a 16-bit big-endian unsigned value
    static void writeBE16(QByteArray& buf, int offset, quint16 value);

    // --- Port Assignments ---
    // P2 port map (offsets from base port 1024)
    static constexpr quint16 kBasePort = 1024;
    static constexpr quint16 kPortGeneral     = 1024;  // PC→Radio: general config
    static constexpr quint16 kPortRxConfig    = 1025;  // PC→Radio: RX config
    static constexpr quint16 kPortTxConfig    = 1026;  // PC→Radio: TX config
    static constexpr quint16 kPortHighPri     = 1027;  // PC→Radio: high priority
    static constexpr quint16 kPortHighPriFrom = 1025;  // Radio→PC: high priority status
    static constexpr quint16 kPortRxAudio     = 1028;  // Radio→PC: RX audio
    static constexpr quint16 kPortMicData     = 1029;  // PC→Radio: mic data
    static constexpr quint16 kPortDdcBase     = 1035;  // Radio→PC: DDC0 I/Q (1035-1041)

    static constexpr int kMaxDdc = 7;          // DDC0-DDC6
    static constexpr int kSamplesPerPacket = 238;
    static constexpr int kIqBytesPerSample = 6;    // 3 bytes I + 3 bytes Q
    static constexpr int kIqDataOffset = 16;       // I/Q data starts at byte 16 in packet
    static constexpr int kHighPriIntervalMs = 100;  // Resend high-priority every 100ms

    // --- Sockets ---
    QUdpSocket* m_cmdSocket{nullptr};         // Sends commands to radio
    QUdpSocket* m_dataSocket{nullptr};        // Receives I/Q data from radio
    QUdpSocket* m_highPriStatusSocket{nullptr}; // Receives high-priority status from radio

    // --- Timers ---
    QTimer* m_highPriorityTimer{nullptr};  // Periodic high-priority command resend
    QTimer* m_reconnectTimer{nullptr};

    // --- Sequence counters (per command type) ---
    quint32 m_seqGeneral{0};
    quint32 m_seqRx{0};
    quint32 m_seqTx{0};
    quint32 m_seqHighPri{0};

    // --- Hardware state cache ---
    static constexpr int kMaxReceivers = 7;
    std::array<quint64, kMaxReceivers> m_rxFrequencies{};  // Hz per DDC
    quint64 m_txFrequency{14225000};
    int m_activeReceiverCount{1};
    int m_sampleRate{48000};
    int m_attenuatorDb{0};
    bool m_preampOn{false};
    int m_txDriveLevel{0};     // 0-255
    bool m_mox{false};
    bool m_running{false};
    int m_antennaIndex{0};

    bool m_intentionalDisconnect{false};

    // Local port the data socket is bound to (reported to radio in CmdGeneral)
    quint16 m_localDataPort{0};
    quint16 m_localHighPriStatusPort{0};

    // I/Q sample buffers (one per DDC, reused to avoid allocation)
    std::array<QVector<float>, kMaxDdc> m_iqBuffers;

    // Sequence tracking for incoming I/Q packets
    std::array<quint32, kMaxDdc> m_lastIqSeq{};
    std::array<bool, kMaxDdc> m_firstIqPacket{};
    int m_totalIqPackets{0};   // diagnostic counter
};

} // namespace NereusSDR
