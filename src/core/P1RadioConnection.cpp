// src/core/P1RadioConnection.cpp
//
// Protocol 1 wire-format compose helpers and stub implementations.
// Porting from Thetis ChannelMaster/networkproto1.c (WriteMainLoop,
// MetisWriteFrame) and Console/HPSDR/NetworkIO.cs (thin C# wrapper over
// the same native DLL).

#include "P1RadioConnection.h"
#include "LogCategories.h"

#include <cstring>    // memset
#include <QtEndian>

namespace NereusSDR {

// ---------------------------------------------------------------------------
// composeEp2Frame
//
// Builds the 1032-byte Metis ep2 UDP payload that is sent TO the radio.
// Layout (source: networkproto1.c:216-236 MetisWriteFrame, and :597-864
// WriteMainLoop which populates the two 512-byte USB subframes):
//
//   [0..3]    Magic header: EF FE 01 02
//   [4..7]    Sequence number, big-endian uint32
//   [8..519]  USB subframe 0 (512 bytes):
//               [8..10]   sync 7F 7F 7F  (networkproto1.c:600-602)
//               [11..15]  C0..C4 command/control bytes
//               [16..519] TX I/Q + mic samples (zeros for RX-only)
//   [520..1031] USB subframe 1 (512 bytes):
//               [520..522] sync 7F 7F 7F  (networkproto1.c:881-883)
//               [523..527] C0..C4 for second subframe (same bank for now)
//               [528..1031] TX I/Q + mic samples (zeros for RX-only)
// ---------------------------------------------------------------------------
void P1RadioConnection::composeEp2Frame(quint8 out[1032], quint32 seq,
                                         int /*ccAddress*/,
                                         int sampleRate, bool mox) noexcept
{
    // Source: networkproto1.c:223-230 — MetisWriteFrame() header + sequence
    out[0] = 0xEF;
    out[1] = 0xFE;
    out[2] = 0x01;
    out[3] = 0x02;  // endpoint 2

    // Sequence number big-endian
    out[4] = static_cast<quint8>((seq >> 24) & 0xFF);
    out[5] = static_cast<quint8>((seq >> 16) & 0xFF);
    out[6] = static_cast<quint8>((seq >>  8) & 0xFF);
    out[7] = static_cast<quint8>( seq        & 0xFF);

    // Source: networkproto1.c:597-602 — WriteMainLoop() USB subframe 0 sync
    out[8]  = 0x7F;
    out[9]  = 0x7F;
    out[10] = 0x7F;

    // C0..C4 for subframe 0 — compose bank 0 (general settings)
    quint8 cc[5] = {};
    composeCcBank0(cc, sampleRate, mox);
    out[11] = cc[0];
    out[12] = cc[1];
    out[13] = cc[2];
    out[14] = cc[3];
    out[15] = cc[4];

    // Bytes 16..519: TX I/Q + mic data — zeros (RX-only; TX producer added in TX phase)

    // Source: networkproto1.c:878-883 — WriteMainLoop_HL2() USB subframe 1 sync
    // (same layout in WriteMainLoop at :880-883)
    out[520] = 0x7F;
    out[521] = 0x7F;
    out[522] = 0x7F;

    // C0..C4 for subframe 1 — repeat bank 0 (round-robin comes in later task)
    out[523] = cc[0];
    out[524] = cc[1];
    out[525] = cc[2];
    out[526] = cc[3];
    out[527] = cc[4];

    // Bytes 528..1031: TX I/Q + mic data — zeros (RX-only)
}

// ---------------------------------------------------------------------------
// composeCcBank0
//
// Source: networkproto1.c:619-641 — WriteMainLoop() case 0 (general settings)
//   C0 = XmitBit (MOX, bit 0); address bits 7..1 = 0x00 (no C0 |= address)
//   C1 = SampleRateIn2Bits & 3  (48k=0, 96k=1, 192k=2, 384k=3)
//   C2 = OC output mask (bits 7..1) | EER bit (bit 0) — zero for stub
//   C3 = BPF/atten/preamp flags — zero for stub
//   C4 = antenna, duplex, NDDCs — zero for stub
// ---------------------------------------------------------------------------
void P1RadioConnection::composeCcBank0(quint8 out[5], int sampleRate, bool mox) noexcept
{
    // Source: networkproto1.c:615 — C0 = (unsigned char)XmitBit
    out[0] = mox ? 0x01 : 0x00;

    // Source: networkproto1.c:620 — C1 = (SampleRateIn2Bits & 3)
    // 48000→0, 96000→1, 192000→2, 384000→3
    quint8 srBits = 0;
    if      (sampleRate >= 384000) { srBits = 3; }
    else if (sampleRate >= 192000) { srBits = 2; }
    else if (sampleRate >= 96000)  { srBits = 1; }
    else                            { srBits = 0; }
    out[1] = srBits & 0x03;

    // C2..C4: OC outputs, filter bits, antenna — zero for Task 7 scope.
    // Source: networkproto1.c:621-640 — full encoding in later tasks
    out[2] = 0;
    out[3] = 0;
    out[4] = 0;
}

// ---------------------------------------------------------------------------
// composeCcBankRxFreq
//
// Source: networkproto1.c:653-663 — case 2 (RX1/DDC0 frequency)
//   rxIndex 0 → C0 |= 4 (address 0x02)
//   rxIndex 1 → C0 |= 6 (address 0x03)  [case 3]
//   rxIndex 2 → C0 |= 8 (address 0x04)  [case 5]
//   C1..C4 = frequency bytes big-endian
//
// Note: Thetis actually uses a phase-word (Freq2PhaseWord) for Ethernet P1,
// but the NetworkIO.cs:222 path for P1-USB sends raw Hz. For NereusSDR Task 7
// scope we store raw Hz exactly as the case 2 source shows; phase-word
// conversion is a TODO for the send-path wiring in Task 9.
// ---------------------------------------------------------------------------
void P1RadioConnection::composeCcBankRxFreq(quint8 out[5], int rxIndex, quint64 freqHz) noexcept
{
    // Address assignments from networkproto1.c:
    //   rxIndex 0 → case 2: C0 |= 4   (networkproto1.c:654)
    //   rxIndex 1 → case 3: C0 |= 6   (networkproto1.c:667)
    //   rxIndex 2 → case 5: C0 |= 8   (networkproto1.c:694)
    //   rxIndex 3 → case 7: C0 |= 0x0c (networkproto1.c:718)
    //   rxIndex 4 → case 8: C0 |= 0x0e (networkproto1.c:728)
    //   rxIndex 5 → case 9: C0 |= 0x10 (networkproto1.c:738)
    static const quint8 kRxC0Address[] = { 4, 6, 8, 0x0c, 0x0e, 0x10, 0x12 };
    quint8 addrBits = (rxIndex >= 0 && rxIndex < 7) ? kRxC0Address[rxIndex] : 4;
    out[0] = addrBits;  // MOX=0; address is already left-shifted in the table

    quint32 freq32 = static_cast<quint32>(freqHz & 0xFFFFFFFF);
    out[1] = static_cast<quint8>((freq32 >> 24) & 0xFF);
    out[2] = static_cast<quint8>((freq32 >> 16) & 0xFF);
    out[3] = static_cast<quint8>((freq32 >>  8) & 0xFF);
    out[4] = static_cast<quint8>( freq32        & 0xFF);
}

// ---------------------------------------------------------------------------
// composeCcBankTxFreq
//
// Source: networkproto1.c:645-651 — case 1 (TX VFO frequency)
//   C0 |= 2 → address 0x01
//   C1..C4 = frequency bytes big-endian
// ---------------------------------------------------------------------------
void P1RadioConnection::composeCcBankTxFreq(quint8 out[5], quint64 freqHz) noexcept
{
    // Source: networkproto1.c:646 — C0 |= 2
    out[0] = 0x02;  // address 0x01, MOX=0

    quint32 freq32 = static_cast<quint32>(freqHz & 0xFFFFFFFF);
    out[1] = static_cast<quint8>((freq32 >> 24) & 0xFF);
    out[2] = static_cast<quint8>((freq32 >> 16) & 0xFF);
    out[3] = static_cast<quint8>((freq32 >>  8) & 0xFF);
    out[4] = static_cast<quint8>( freq32        & 0xFF);
}

// ---------------------------------------------------------------------------
// composeCcBankAtten
//
// Source: networkproto1.c:762-771 — case 11 (Preamp control / step attenuator)
//   C0 |= 0x14 → address 0x0A (networkproto1.c:763)
//   C4 = (adc[0].rx_step_attn & 0b00011111) | 0b00100000
//        Low 5 bits = dB value; bit 5 = "enable step attenuator" flag
// ---------------------------------------------------------------------------
void P1RadioConnection::composeCcBankAtten(quint8 out[5], int dB) noexcept
{
    // Source: networkproto1.c:763 — C0 |= 0x14 → address 0x0A
    out[0] = 0x14;  // MOX=0; address bits = 0x14

    // C1..C3: preamp/mic/dig-out flags — zero for Task 7 scope
    out[1] = 0;
    out[2] = 0;
    out[3] = 0;

    // Source: networkproto1.c:770 — C4 = (rx_step_attn & 0b00011111) | 0b00100000
    out[4] = static_cast<quint8>((dB & 0x1F) | 0x20);
}

// ---------------------------------------------------------------------------
// composeCcBankAlexRx
//
// Source: networkproto1.c:826-835 — case 16 (BPF2 / ALEX RX filter mask)
//   C0 |= 0x24 → address 0x12
//   C1 = BPF HPF filter bits (low 7 bits of alexRxMask)
//   C2 = xvtr_enable + puresignal flags (bits 0 + 6 of upper byte)
//   C3, C4 = 0
//
// TODO(3I-T7): The "ALEX RX antenna routing" (RX1 IN, RX2 IN, XVTR) is
// encoded in bank 0 C3 bits 5..6 (networkproto1.c:625-630), not a separate
// bank. The alexRxMask here is treated as the BPF2 HPF filter bitmap
// (case 16). Full ALEX RX antenna routing requires bank 0 state.
// ---------------------------------------------------------------------------
void P1RadioConnection::composeCcBankAlexRx(quint8 out[5], quint32 alexRxMask) noexcept
{
    // Source: networkproto1.c:827 — C0 |= 0x24
    out[0] = 0x24;
    out[1] = static_cast<quint8>(alexRxMask & 0x7F);  // HPF filter bits
    out[2] = static_cast<quint8>((alexRxMask >> 8) & 0x41); // xvtr + puresignal flags
    out[3] = 0;
    out[4] = 0;
}

// ---------------------------------------------------------------------------
// composeCcBankAlexTx
//
// Source: networkproto1.c:747-760 — case 10 (TX drive level / ALEX TX LPF)
//   C0 |= 0x12 → address 0x09
//   C3 = HPF filter bits (bits from alexTxMask low byte)
//   C4 = LPF filter bits (bits from alexTxMask high byte)
//   C1 = drive level (0 for stub), C2 = mic/apollo flags (0 for stub)
//
// TODO(3I-T7): Drive level and mic/apollo flags are separate state fields not
// carried in alexTxMask. Full encoding requires those fields; this stub
// encodes only the filter mask portion as mapped from Task 7 scope.
// ---------------------------------------------------------------------------
void P1RadioConnection::composeCcBankAlexTx(quint8 out[5], quint32 alexTxMask) noexcept
{
    // Source: networkproto1.c:748 — C0 |= 0x12
    out[0] = 0x12;
    out[1] = 0;  // drive level — TODO(3I-T7): wire from state
    out[2] = 0;  // mic/apollo flags — TODO(3I-T7): wire from state
    out[3] = static_cast<quint8>(alexTxMask & 0xFF);        // HPF bits
    out[4] = static_cast<quint8>((alexTxMask >> 8) & 0x7F); // LPF bits
}

// ---------------------------------------------------------------------------
// composeCcBankOcOutputs
//
// Source: networkproto1.c:621 — case 0: C2 = (cw.eer & 1) | ((oc_output << 1) & 0xFE)
//   OC output bits live in bank 0 C2, bits 7..1. Not a separate bank.
//   For Task 7 scope this helper encodes only the OC mask into C2 of a
//   bank-0-shaped output buffer (C0 address = 0x00, C1 = default 48k).
//
// TODO(3I-T7): In Thetis the OC mask is always sent as part of bank 0 C2
// (networkproto1.c:621). This standalone helper is for the test interface;
// in the real send path it will be merged into composeCcBank0 state.
// ---------------------------------------------------------------------------
void P1RadioConnection::composeCcBankOcOutputs(quint8 out[5], quint8 ocMask) noexcept
{
    // Source: networkproto1.c:621 — C2 = (cw.eer & 1) | ((oc_output << 1) & 0xFE)
    out[0] = 0;  // address 0x00, MOX=0
    out[1] = 0;
    out[2] = static_cast<quint8>((ocMask << 1) & 0xFE);  // OC bits in C2 bits 7..1
    out[3] = 0;
    out[4] = 0;
}



P1RadioConnection::P1RadioConnection(QObject* parent)
    : RadioConnection(parent)
{
}

P1RadioConnection::~P1RadioConnection() = default;

void P1RadioConnection::init()
{
    // Sockets and timers created lazily on connect; keep init empty for now.
    // Tasks 9 & 10 wire up m_socket, m_watchdogTimer, m_reconnectTimer here.
}

void P1RadioConnection::connectToRadio(const RadioInfo& info)
{
    m_radioInfo = info;
    m_caps = &BoardCapsTable::forBoard(info.boardType);
    qCInfo(lcConnection) << "P1RadioConnection::connectToRadio STUB for"
                         << m_caps->displayName
                         << "- real wire format coming in Phase 3I Tasks 7-9";
    setState(ConnectionState::Error);
    emit errorOccurred(QStringLiteral("P1 connection not yet implemented (Phase 3I Task 6 stub)"));
}

void P1RadioConnection::disconnect()
{
    setState(ConnectionState::Disconnected);
}

void P1RadioConnection::setReceiverFrequency(int receiverIndex, quint64 frequencyHz)
{
    if (receiverIndex >= 0 && receiverIndex < 7) {
        m_rxFreqHz[receiverIndex] = frequencyHz;
    }
}

void P1RadioConnection::setTxFrequency(quint64 frequencyHz)  { m_txFreqHz = frequencyHz; }
void P1RadioConnection::setActiveReceiverCount(int count)    { m_activeRxCount = count; }
void P1RadioConnection::setSampleRate(int sampleRate)        { m_sampleRate = sampleRate; }
void P1RadioConnection::setAttenuator(int dB)                { m_atten = dB; }
void P1RadioConnection::setPreamp(bool enabled)              { m_preamp = enabled; }
void P1RadioConnection::setTxDrive(int /*level*/)            { /* stub — Task 7 */ }
void P1RadioConnection::setMox(bool enabled)                 { m_mox = enabled; }
void P1RadioConnection::setAntenna(int antennaIndex)         { m_antennaIdx = antennaIndex; }

void P1RadioConnection::onReadyRead()        { /* stub — Task 8 */ }
void P1RadioConnection::onWatchdogTick()     { /* stub — Task 9 */ }
void P1RadioConnection::onReconnectTimeout() { /* stub — Task 10 */ }

void P1RadioConnection::sendMetisStart(bool) { /* Task 7 */ }
void P1RadioConnection::sendMetisStop()      { /* Task 7 */ }
void P1RadioConnection::sendCommandFrame()   { /* Task 7 */ }
void P1RadioConnection::parseEp6Frame(const QByteArray&) { /* Task 8 */ }

void P1RadioConnection::composeCcBank0(quint8*) { /* Task 7 */ }
void P1RadioConnection::composeCcBank1(quint8*) { /* Task 7 */ }
void P1RadioConnection::composeCcBank2(quint8*) { /* Task 7 */ }
void P1RadioConnection::composeCcBank3(quint8*) { /* Task 7 */ }
void P1RadioConnection::composeCcTxFreq(quint8*)  { /* Task 7 */ }
void P1RadioConnection::composeCcAlexRx(quint8*)  { /* Task 7 */ }
void P1RadioConnection::composeCcAlexTx(quint8*)  { /* Task 7 */ }
void P1RadioConnection::composeCcOcOutputs(quint8*) { /* Task 7 */ }

void P1RadioConnection::applyBoardQuirks(HPSDRHW) { /* Task 11 */ }
void P1RadioConnection::hl2SendIoBoardTlv(const QByteArray&) { /* Task 12 */ }
void P1RadioConnection::hl2CheckBandwidthMonitor() { /* Task 12 */ }
void P1RadioConnection::checkFirmwareMinimum(int)  { /* Task 11 */ }

// From Thetis networkproto1.c — scale 24-bit big-endian sample to [-1, 1].
// Task 8 will implement this properly.
float P1RadioConnection::scaleSample24(const quint8* /*be24*/) { return 0.0f; /* Task 8 */ }

} // namespace NereusSDR
