// src/core/P1RadioConnection.h
//
// Protocol 1 connection for Atlas/Hermes/HermesII/Angelia/Orion/HermesLite.
// Faithfully ported from Thetis networkproto1.c + NetworkIO.cs +
// mi0bot IoBoardHl2.cs + bandwidth_monitor.{c,h}.
//
// Follows the same pattern as P2RadioConnection (which ports network.c).
// Instances live on the Connection worker thread.
// Call init() after moveToThread() to create sockets/timers on the worker.

#pragma once

#include "RadioConnection.h"
#include "BoardCapabilities.h"
#include "HpsdrModel.h"

#include <QUdpSocket>
#include <QTimer>

namespace NereusSDR {

class P1RadioConnection : public RadioConnection {
    Q_OBJECT
public:
    explicit P1RadioConnection(QObject* parent = nullptr);
    ~P1RadioConnection() override;

    // Wire-format compose helpers — static, testable in isolation.
    // Each implementation cites its Thetis source line.
    static void composeEp2Frame(quint8 out[1032], quint32 seq, int ccAddress,
                                int sampleRate, bool mox) noexcept;
    static void composeCcBank0(quint8 out[5], int sampleRate, bool mox) noexcept;
    static void composeCcBankRxFreq(quint8 out[5], int rxIndex, quint64 freqHz) noexcept;
    static void composeCcBankTxFreq(quint8 out[5], quint64 freqHz) noexcept;
    static void composeCcBankAtten(quint8 out[5], int dB) noexcept;
    static void composeCcBankAlexRx(quint8 out[5], quint32 alexRxMask) noexcept;
    static void composeCcBankAlexTx(quint8 out[5], quint32 alexTxMask) noexcept;
    static void composeCcBankOcOutputs(quint8 out[5], quint8 ocMask) noexcept;

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
    void onWatchdogTick();
    void onReconnectTimeout();

private:
    // --- Wire format (networkproto1.c) — implemented in Tasks 7 & 8 ---
    void sendMetisStart(bool iqAndMic);
    void sendMetisStop();
    void sendCommandFrame();
    void parseEp6Frame(const QByteArray& pkt);

    // --- Command & Control banks (NetworkIO.cs SetC0..SetC4) ---
    void composeCcBank0(quint8* out);
    void composeCcBank1(quint8* out);
    void composeCcBank2(quint8* out);
    void composeCcBank3(quint8* out);
    void composeCcTxFreq(quint8* out);
    void composeCcAlexRx(quint8* out);
    void composeCcAlexTx(quint8* out);
    void composeCcOcOutputs(quint8* out);

    // --- Per-board quirks — implemented in Tasks 11 & 12 ---
    void applyBoardQuirks(HPSDRHW board);
    void hl2SendIoBoardTlv(const QByteArray& tlv);
    void hl2CheckBandwidthMonitor();
    void checkFirmwareMinimum(int fw);

    static float scaleSample24(const quint8* be24);

    // --- State ---
    QUdpSocket* m_socket{nullptr};
    QTimer*     m_watchdogTimer{nullptr};
    QTimer*     m_reconnectTimer{nullptr};

    quint32 m_epSendSeq{0};
    quint32 m_epRecvSeqExpected{0};
    int     m_ccRoundRobinIdx{0};

    int     m_sampleRate{48000};
    int     m_activeRxCount{1};
    quint64 m_rxFreqHz[7]{};
    quint64 m_txFreqHz{0};
    int     m_atten{0};
    bool    m_preamp{false};
    bool    m_mox{false};
    int     m_antennaIdx{0};

    const BoardCapabilities* m_caps{nullptr};
};

} // namespace NereusSDR
