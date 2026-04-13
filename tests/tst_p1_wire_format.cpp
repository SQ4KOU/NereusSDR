#include <QtTest/QtTest>
#include "core/P1RadioConnection.h"

using namespace NereusSDR;

class TestP1WireFormat : public QObject {
    Q_OBJECT
private slots:
    void ep2FrameMagicHeader() {
        quint8 frame[1032] = {};
        P1RadioConnection::composeEp2Frame(frame, /*seq=*/0, /*ccAddress=*/0,
                                           /*sampleRate=*/48000, /*mox=*/false);
        // Source: networkproto1.c:223-226 — MetisWriteFrame() magic 0xEFFE 0x01 0x02
        QCOMPARE(frame[0], quint8(0xEF));
        QCOMPARE(frame[1], quint8(0xFE));
        QCOMPARE(frame[2], quint8(0x01));
        QCOMPARE(frame[3], quint8(0x02));
    }

    void ep2FrameSequenceBigEndian32() {
        quint8 frame[1032] = {};
        P1RadioConnection::composeEp2Frame(frame, /*seq=*/0x12345678, 0, 48000, false);
        // Source: networkproto1.c:227-230 — MetisWriteFrame() sequence big-endian
        QCOMPARE(frame[4], quint8(0x12));
        QCOMPARE(frame[5], quint8(0x34));
        QCOMPARE(frame[6], quint8(0x56));
        QCOMPARE(frame[7], quint8(0x78));
    }

    void ep2FrameSyncBytesAtUsbSubframes() {
        quint8 frame[1032] = {};
        P1RadioConnection::composeEp2Frame(frame, 0, 0, 48000, false);
        // Source: networkproto1.c:600-602, 881-883 — WriteMainLoop() sync bytes
        QCOMPARE(frame[8],  quint8(0x7F));
        QCOMPARE(frame[9],  quint8(0x7F));
        QCOMPARE(frame[10], quint8(0x7F));
        // USB subframe 1 at offset 520: 0x7F 0x7F 0x7F
        QCOMPARE(frame[520], quint8(0x7F));
        QCOMPARE(frame[521], quint8(0x7F));
        QCOMPARE(frame[522], quint8(0x7F));
    }

    void ccBank0EncodesSampleRate48k() {
        quint8 out[5] = {};
        P1RadioConnection::composeCcBank0(out, /*sampleRate=*/48000, /*mox=*/false);
        // Source: networkproto1.c:620 — C1 = (SampleRateIn2Bits & 3): 48k=00, 96k=01, 192k=10, 384k=11
        QCOMPARE(int(out[1] & 0x03), 0);
    }

    void ccBank0EncodesSampleRate96k() {
        quint8 out[5] = {};
        P1RadioConnection::composeCcBank0(out, 96000, false);
        // Source: networkproto1.c:620 — SampleRateIn2Bits for 96k = 1
        QCOMPARE(int(out[1] & 0x03), 1);
    }

    void ccBank0EncodesSampleRate192k() {
        quint8 out[5] = {};
        P1RadioConnection::composeCcBank0(out, 192000, false);
        // Source: networkproto1.c:620 — SampleRateIn2Bits for 192k = 2
        QCOMPARE(int(out[1] & 0x03), 2);
    }

    void ccBank0EncodesSampleRate384k() {
        quint8 out[5] = {};
        P1RadioConnection::composeCcBank0(out, 384000, false);
        // Source: networkproto1.c:620 — SampleRateIn2Bits for 384k = 3
        QCOMPARE(int(out[1] & 0x03), 3);
    }

    void ccBank0SetsMoxBit() {
        quint8 out[5] = {};
        P1RadioConnection::composeCcBank0(out, 48000, /*mox=*/true);
        // Source: networkproto1.c:615-616 — C0 = (unsigned char)XmitBit; MOX is bit 0 of C0
        QCOMPARE(int(out[0] & 0x01), 1);
    }

    void ccBank0C0AddressIs0x00() {
        quint8 out[5] = {};
        P1RadioConnection::composeCcBank0(out, 48000, false);
        // Source: networkproto1.c:619 — case 0: no C0 |= address, so C0 bits 7..1 = 0
        QCOMPARE(int((out[0] >> 1) & 0x7F), 0);
    }

    void rxFrequencyEncodedBigEndian32() {
        quint8 out[5] = {};
        quint64 freqHz = 14200000ULL;
        P1RadioConnection::composeCcBankRxFreq(out, /*rxIndex=*/0, freqHz);
        // Source: networkproto1.c:660-663 — C1..C4 = frequency bytes, big-endian
        quint32 readBack = (quint32(out[1]) << 24) | (quint32(out[2]) << 16)
                         | (quint32(out[3]) <<  8) |  quint32(out[4]);
        QCOMPARE(readBack, 14200000U);
    }

    void rxFrequencyC0AddressForRx0() {
        quint8 out[5] = {};
        P1RadioConnection::composeCcBankRxFreq(out, 0, 14200000);
        // Source: networkproto1.c:653-654 — case 2: C0 |= 4; RX1 DDC0 → address bits = 4/2 = 0x02
        QCOMPARE(int((out[0] >> 1) & 0x7F), 0x02);
    }

    void txFrequencyEncodedBigEndian32() {
        quint8 out[5] = {};
        quint64 freqHz = 7100000ULL;
        P1RadioConnection::composeCcBankTxFreq(out, freqHz);
        // Source: networkproto1.c:647-650 — case 1: C0 |= 2; C1..C4 = TX freq big-endian
        quint32 readBack = (quint32(out[1]) << 24) | (quint32(out[2]) << 16)
                         | (quint32(out[3]) <<  8) |  quint32(out[4]);
        QCOMPARE(readBack, 7100000U);
    }

    void txFrequencyC0AddressIs0x01() {
        quint8 out[5] = {};
        P1RadioConnection::composeCcBankTxFreq(out, 7100000);
        // Source: networkproto1.c:645-646 — case 1: C0 |= 2 → address = 2>>1 = 0x01
        QCOMPARE(int((out[0] >> 1) & 0x7F), 0x01);
    }

    void attenuatorEncodedInBank() {
        quint8 out[5] = {};
        P1RadioConnection::composeCcBankAtten(out, /*dB=*/20);
        // Source: networkproto1.c:770 — case 11 (C0 |= 0x14): C4 = adc[0].rx_step_attn & 0b00011111 | 0b00100000
        // Low 5 bits of C4 hold the dB value; bit 5 is the "enable attenuation" flag
        QCOMPARE(int(out[4] & 0x1F), 20);
    }

    void attenuatorC0AddressIs0x0A() {
        quint8 out[5] = {};
        P1RadioConnection::composeCcBankAtten(out, 0);
        // Source: networkproto1.c:763 — case 11: C0 |= 0x14 → address = 0x14>>1 = 0x0A
        QCOMPARE(int((out[0] >> 1) & 0x7F), 0x0A);
    }
};

QTEST_APPLESS_MAIN(TestP1WireFormat)
#include "tst_p1_wire_format.moc"
