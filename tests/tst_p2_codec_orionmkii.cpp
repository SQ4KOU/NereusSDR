// no-port-check: test fixture cites Thetis source for expected values, not a port itself
#include <QtTest/QtTest>
#include "core/codec/P2CodecOrionMkII.h"
#include "core/codec/CodecContext.h"

using namespace NereusSDR;

class TestP2CodecOrionMkII : public QObject {
    Q_OBJECT
private slots:
    // CmdGeneral byte 4 — command byte always 0x00
    // Source: network.c:826 [@501e3f5]
    void cmdGeneral_byte4_command_zero() {
        P2CodecOrionMkII codec;
        CodecContext ctx;
        quint8 buf[60] = {};
        codec.composeCmdGeneral(ctx, buf);
        QCOMPARE(int(buf[4]), 0x00);
    }

    // CmdGeneral bytes 0-3 — sequence number NOT stamped by codec
    // Source: network.c note — seqno stamped by caller [@501e3f5]
    void cmdGeneral_seqno_zero() {
        P2CodecOrionMkII codec;
        CodecContext ctx;
        quint8 buf[60] = {};
        codec.composeCmdGeneral(ctx, buf);
        QCOMPARE(int(buf[0]), 0);
        QCOMPARE(int(buf[1]), 0);
        QCOMPARE(int(buf[2]), 0);
        QCOMPARE(int(buf[3]), 0);
    }

    // CmdGeneral port bytes 5-6 — custom port base default 1025
    // Source: network.c:839 [@501e3f5]
    void cmdGeneral_portBase_default() {
        P2CodecOrionMkII codec;
        CodecContext ctx;  // default p2CustomPortBase = 1025
        quint8 buf[60] = {};
        codec.composeCmdGeneral(ctx, buf);
        // Port base 1025 big-endian: 0x04 0x01
        QCOMPARE(int(buf[5]), 0x04);
        QCOMPARE(int(buf[6]), 0x01);
    }

    // CmdGeneral byte 37 — freq/phase-word mode bit (always 0x08)
    // Source: network.c:896 [@501e3f5]
    void cmdGeneral_byte37_phaseWordBit() {
        P2CodecOrionMkII codec;
        CodecContext ctx;
        quint8 buf[60] = {};
        codec.composeCmdGeneral(ctx, buf);
        QCOMPARE(int(buf[37]), 0x08);
    }

    // CmdGeneral byte 59 — Alex enable (both boards enabled = 0x03)
    // Source: network.c:906 [@501e3f5]
    void cmdGeneral_byte59_alexEnable() {
        P2CodecOrionMkII codec;
        CodecContext ctx;
        quint8 buf[60] = {};
        codec.composeCmdGeneral(ctx, buf);
        QCOMPARE(int(buf[59]), 0x03);
    }

    // CmdHighPriority byte 4 — PTT off (not running, no PTT)
    // Source: network.c:924-925 [@501e3f5]
    void cmdHighPriority_byte4_ptt_off() {
        P2CodecOrionMkII codec;
        CodecContext ctx;
        quint8 buf[1444] = {};
        codec.composeCmdHighPriority(ctx, buf);
        QCOMPARE(int(buf[4]) & 0x02, 0);  // PTT bit clear
        QCOMPARE(int(buf[4]) & 0x01, 0);  // run bit clear
    }

    // CmdHighPriority byte 4 — PTT on (p2Running + p2PttOut)
    // Source: network.c:924-925 [@501e3f5]
    void cmdHighPriority_byte4_ptt_on() {
        P2CodecOrionMkII codec;
        CodecContext ctx;
        ctx.p2Running = true;
        ctx.p2PttOut  = 1;
        quint8 buf[1444] = {};
        codec.composeCmdHighPriority(ctx, buf);
        QCOMPARE(int(buf[4]) & 0x02, 0x02);  // PTT bit set
        QCOMPARE(int(buf[4]) & 0x01, 0x01);  // run bit set
    }

    // CmdHighPriority bytes 1432-1435 — Alex0 HPF 13 MHz (bit 1 in reg = 0x02)
    // Source: network.c:1040-1050 [@501e3f5]
    void cmdHighPriority_alex0_hpf_13MHz() {
        P2CodecOrionMkII codec;
        CodecContext ctx;
        ctx.alexHpfBits = 0x01;  // 13 MHz HPF
        ctx.alexLpfBits = 0x01;  // 30/20m LPF
        ctx.p2AlexRxAnt = 1;     // ANT1
        quint8 buf[1444] = {};
        codec.composeCmdHighPriority(ctx, buf);
        // Alex0 bytes 1432-1435 big-endian. HPF 13MHz → bit 1 → contributes to byte 1435.
        // ANT1 → bit 24 → byte 1432 bit 0. LPF 30/20m → bit 20 → byte 1432 bit 4.
        // Verify register is non-zero (at minimum HPF bit is set)
        QVERIFY(buf[1432] != 0 || buf[1433] != 0 || buf[1434] != 0 || buf[1435] != 0);
        // HPF 13MHz (0x01) → bit 1 → byte 1435 bit 1
        QVERIFY(buf[1435] & 0x02);
    }

    // CmdHighPriority byte 1443 — RX ATT for ADC0
    // Source: network.c:1055-1057 [@501e3f5]
    void cmdHighPriority_rxAtt_20dB() {
        P2CodecOrionMkII codec;
        CodecContext ctx;
        ctx.rxStepAttn[0] = 20;
        quint8 buf[1444] = {};
        codec.composeCmdHighPriority(ctx, buf);
        QCOMPARE(int(buf[1443]), 20);  // ADC0 attenuation raw value
    }

    // CmdHighPriority byte 1442 — RX ATT for ADC1
    // Source: network.c:1055-1057 [@501e3f5]
    void cmdHighPriority_rxAtt_adc1() {
        P2CodecOrionMkII codec;
        CodecContext ctx;
        ctx.rxStepAttn[1] = 10;
        quint8 buf[1444] = {};
        codec.composeCmdHighPriority(ctx, buf);
        QCOMPARE(int(buf[1442]), 10);  // ADC1 attenuation raw value
    }

    // CmdTx bytes 57-59 — TX ATT per ADC
    // Source: network.c:1238-1242 [@501e3f5]
    void cmdTx_txAtt_per_adc() {
        P2CodecOrionMkII codec;
        CodecContext ctx;
        ctx.txStepAttn[0] = 5;
        ctx.txStepAttn[1] = 7;
        ctx.txStepAttn[2] = 9;
        quint8 buf[60] = {};
        codec.composeCmdTx(ctx, buf);
        QCOMPARE(int(buf[59]), 5);  // ADC0
        QCOMPARE(int(buf[58]), 7);  // ADC1
        QCOMPARE(int(buf[57]), 9);  // ADC2
    }

    // CmdRx bytes 5-6 — dither + random bits per ADC, defaults on
    // Source: network.c:1080-1090 [@501e3f5]
    void cmdRx_dither_random_default_on() {
        P2CodecOrionMkII codec;
        CodecContext ctx;  // defaults: dither[*]=true, random[*]=true
        quint8 buf[1444] = {};
        codec.composeCmdRx(ctx, buf);
        QVERIFY(buf[5] != 0);  // some dither bits set
        QVERIFY(buf[6] != 0);  // some random bits set
    }

    // CmdRx bytes 5-6 — dither + random off when all disabled
    // Source: network.c:1080-1090 [@501e3f5]
    void cmdRx_dither_random_off() {
        P2CodecOrionMkII codec;
        CodecContext ctx;
        for (int i = 0; i < 3; ++i) {
            ctx.dither[i] = false;
            ctx.random[i] = false;
        }
        quint8 buf[1444] = {};
        codec.composeCmdRx(ctx, buf);
        QCOMPARE(int(buf[5]), 0);
        QCOMPARE(int(buf[6]), 0);
    }

    // CmdRx byte 7 — DDC enable bitmask: enable DDC0 only
    // Source: network.c:1097-1103 [@501e3f5]
    void cmdRx_enableMask_ddc0_only() {
        P2CodecOrionMkII codec;
        CodecContext ctx;
        ctx.p2RxEnable[0] = 1;  // DDC0 only
        quint8 buf[1444] = {};
        codec.composeCmdRx(ctx, buf);
        QCOMPARE(int(buf[7]), 0x01);
    }

    // CmdHighPriority RX0 phase word — unity correction factor matches
    // raw Thetis formula (freq_hz * 2^32 / 122880000).
    // Source: network.c:936-1005 [@501e3f5]
    void cmdHighPriority_rx0_phaseWord_unity_factor() {
        P2CodecOrionMkII codec;
        CodecContext ctx;
        ctx.rxFreqHz[0] = 14'200'000ULL;  // 20m
        ctx.freqCorrectionFactor = 1.0;
        quint8 buf[1444] = {};
        codec.composeCmdHighPriority(ctx, buf);
        const quint32 expected =
            static_cast<quint32>((14'200'000.0 * 4294967296.0) / 122880000.0);
        const quint32 got =
            (quint32(buf[9]) << 24) | (quint32(buf[10]) << 16) |
            (quint32(buf[11]) << 8) |  quint32(buf[12]);
        QCOMPARE(got, expected);
    }

    // CmdHighPriority RX0 phase word — non-unity correction factor changes
    // the emitted phase word. Phase 3P-G: codec cutover path must apply
    // CalibrationController::effectiveFreqCorrectionFactor() via ctx.
    // Source: setup.cs:14036-14050 udHPSDRFreqCorrectFactor_ValueChanged
    // → NetworkIO.FreqCorrectionFactor [@501e3f5]
    void cmdHighPriority_phaseWord_applies_correction_factor() {
        P2CodecOrionMkII codec;
        CodecContext ctxA;
        CodecContext ctxB;
        ctxA.rxFreqHz[0] = 14'200'000ULL;
        ctxA.txFreqHz    = 14'200'000ULL;
        ctxA.freqCorrectionFactor = 1.0;
        ctxB.rxFreqHz[0] = 14'200'000ULL;
        ctxB.txFreqHz    = 14'200'000ULL;
        ctxB.freqCorrectionFactor = 0.9999995;  // ~0.5 ppm trim
        quint8 bufA[1444] = {};
        quint8 bufB[1444] = {};
        codec.composeCmdHighPriority(ctxA, bufA);
        codec.composeCmdHighPriority(ctxB, bufB);

        // RX0 phase word (bytes 9-12) must differ between unity and trimmed factor.
        const quint32 rxA = (quint32(bufA[9]) << 24) | (quint32(bufA[10]) << 16) |
                            (quint32(bufA[11]) << 8) |  quint32(bufA[12]);
        const quint32 rxB = (quint32(bufB[9]) << 24) | (quint32(bufB[10]) << 16) |
                            (quint32(bufB[11]) << 8) |  quint32(bufB[12]);
        QVERIFY(rxA != rxB);

        // TX phase word (bytes 329-332) must likewise differ.
        const quint32 txA = (quint32(bufA[329]) << 24) | (quint32(bufA[330]) << 16) |
                            (quint32(bufA[331]) << 8) |  quint32(bufA[332]);
        const quint32 txB = (quint32(bufB[329]) << 24) | (quint32(bufB[330]) << 16) |
                            (quint32(bufB[331]) << 8) |  quint32(bufB[332]);
        QVERIFY(txA != txB);

        // ctxB factor is below 1.0 → phase word should be strictly smaller.
        QVERIFY(rxB < rxA);
        QVERIFY(txB < txA);
    }

    // CmdHighPriority — sequence bytes 0-3 NOT stamped by codec
    // Source: caller responsibility note [@501e3f5]
    void cmdHighPriority_seqno_zero() {
        P2CodecOrionMkII codec;
        CodecContext ctx;
        quint8 buf[1444] = {};
        codec.composeCmdHighPriority(ctx, buf);
        QCOMPARE(int(buf[0]), 0);
        QCOMPARE(int(buf[1]), 0);
        QCOMPARE(int(buf[2]), 0);
        QCOMPARE(int(buf[3]), 0);
    }
};

QTEST_APPLESS_MAIN(TestP2CodecOrionMkII)
#include "tst_p2_codec_orionmkii.moc"
