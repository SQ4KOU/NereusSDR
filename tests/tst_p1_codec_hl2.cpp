// no-port-check: test fixture cites Thetis source for expected values, not a port itself
#include <QtTest/QtTest>
#include <array>
#include "core/codec/P1CodecHl2.h"

using namespace NereusSDR;

class TestP1CodecHl2 : public QObject {
    Q_OBJECT
private slots:
    void maxBank_is_18() {
        P1CodecHl2 codec;
        QCOMPARE(codec.maxBank(), 18);
    }

    void usesI2cIntercept_is_true() {
        P1CodecHl2 codec;
        QVERIFY(codec.usesI2cIntercept());
    }

    // Bank 11 C4 — RX path: 6-bit mask + 0x40 enable WITH (31 - userDb)
    // inversion. HL2 firmware treats higher values as MORE attenuation, so
    // mi0bot inverts at 3 callsites (console.cs:11075/11251/19380); without
    // the inversion, slider value 31 reaches HL2 as zero attenuation.
    // Source: mi0bot networkproto1.c:1102 + console.cs:11075/11251/19380 [@c26a8a4]
    void bank11_rx_att_20dB_hl2_encoding() {
        P1CodecHl2 codec;
        CodecContext ctx;
        ctx.mox = false;
        ctx.rxStepAttn[0] = 20;
        quint8 out[5] = {};
        codec.composeCcForBank(11, ctx, out);
        QCOMPARE(int(out[0]), 0x14);
        // userDb=20 → wire = (31-20) | 0x40 = 11 | 0x40 = 0x4B
        QCOMPARE(int(out[4]), ((31 - 20) & 0x3F) | 0x40);
    }

    // Bank 11 C4 — TX path: uses txStepAttn[0] not rxStepAttn[0]
    // HL2 TX wire byte is INVERTED: wire = (31 - userDb).
    // Source: mi0bot networkproto1.c:1099-1100 + console.cs:10657-10658 [@c26a8a4]
    void bank11_tx_att_uses_tx_value() {
        P1CodecHl2 codec;
        CodecContext ctx;
        ctx.mox = true;
        ctx.rxStepAttn[0] = 0;     // RX-side value ignored
        ctx.txStepAttn[0] = 20;
        quint8 out[5] = {};
        codec.composeCcForBank(11, ctx, out);
        QCOMPARE(int(out[0]), 0x15);  // C0=0x14 | mox
        // userDb=20 → wire = (31-20) | 0x40 = 11 | 0x40 = 0x4B
        QCOMPARE(int(out[4]), ((31 - 20) & 0x3F) | 0x40);
    }

    // Bank 11 C4 — TX path: HL2 firmware treats higher value as MORE
    // attenuation, so user-facing dB is inverted to (31 - dB) on wire.
    // Critical for force-31-dB safety pathway:
    //   userDb = 31 (max protection) → wire = 0 (HL2 max attenuation = -31 dB)
    //   userDb = 0  (no protection)  → wire = 31 (HL2 zero attenuation)
    // Without this inversion, force-31-dB would send wire=31 to HL2 firmware,
    // which interprets that as ZERO attenuation → full PA drive at the moment
    // we are trying to PROTECT the PA.
    // Discovered during 3M-1c chunk 0 desk-review against mi0bot-Thetis.
    // From mi0bot-Thetis console.cs:10657-10658, 19164-19165, 27814-27815 [@c26a8a4]
    // MI0BOT: Greater range for HL2
    void bank11_tx_att_31_minus_n_inversion_for_hl2() {
        P1CodecHl2 codec;
        CodecContext ctx;
        ctx.mox = true;

        // Force-31-dB safety: userDb=31 → wire byte = 0x40 (= 0 | 0x40)
        ctx.txStepAttn[0] = 31;
        quint8 out_max[5] = {};
        codec.composeCcForBank(11, ctx, out_max);
        QCOMPARE(int(out_max[4]), 0x40);

        // Zero protection: userDb=0 → wire byte = 0x5F (= 31 | 0x40)
        ctx.txStepAttn[0] = 0;
        quint8 out_zero[5] = {};
        codec.composeCcForBank(11, ctx, out_zero);
        QCOMPARE(int(out_zero[4]), 0x5F);

        // Mid-range: userDb=15 → wire byte = 0x50 (= 16 | 0x40)
        ctx.txStepAttn[0] = 15;
        quint8 out_mid[5] = {};
        codec.composeCcForBank(11, ctx, out_mid);
        QCOMPARE(int(out_mid[4]), 0x50);

        // Out-of-range high input is clamped to +32 (signed max).
        // userDb=63 → clamped to +32 → wire = (31-32) & 0x3F | 0x40 = 0x7F.
        ctx.txStepAttn[0] = 63;
        quint8 out_clamp[5] = {};
        codec.composeCcForBank(11, ctx, out_clamp);
        QCOMPARE(int(out_clamp[4]), 0x7F);
    }

    // Bank 11 C4 — RX path range. HL2 user-facing slider is signed
    // −28..+32 dB (mi0bot setup.cs:16085-16086 [v2.10.3.13-beta2]
    // udHermesStepAttenuatorData.{Maximum=32, Minimum=-28}). Inputs outside
    // that range clamp to the corner; the (31 - userDb) inversion folds the
    // clamped value into the 6-bit wire field.
    void bank11_rx_att_signed_range_corners() {
        P1CodecHl2 codec;

        // Negative corner: userDb=-28 → wire = (31-(-28)) & 0x3F | 0x40
        //                = 59 & 0x3F | 0x40 = 0x3B | 0x40 = 0x7B.
        {
            CodecContext ctx;
            ctx.rxStepAttn[0] = -28;
            quint8 out[5] = {};
            codec.composeCcForBank(11, ctx, out);
            QCOMPARE(int(out[4]), 0x7B);
        }

        // Zero (mi0bot default): userDb=0 → wire = 31 | 0x40 = 0x5F.
        {
            CodecContext ctx;
            ctx.rxStepAttn[0] = 0;
            quint8 out[5] = {};
            codec.composeCcForBank(11, ctx, out);
            QCOMPARE(int(out[4]), 0x5F);
        }

        // Positive corner: userDb=+32 → wire = (31-32) & 0x3F | 0x40
        //                = -1 & 0x3F | 0x40 = 0x3F | 0x40 = 0x7F.
        {
            CodecContext ctx;
            ctx.rxStepAttn[0] = 32;
            quint8 out[5] = {};
            codec.composeCcForBank(11, ctx, out);
            QCOMPARE(int(out[4]), 0x7F);
        }

        // Out-of-range low: userDb=-100 → clamped to -28 → wire 0x7B.
        {
            CodecContext ctx;
            ctx.rxStepAttn[0] = -100;
            quint8 out[5] = {};
            codec.composeCcForBank(11, ctx, out);
            QCOMPARE(int(out[4]), 0x7B);
        }

        // Out-of-range high: userDb=+100 → clamped to +32 → wire 0x7F.
        {
            CodecContext ctx;
            ctx.rxStepAttn[0] = 100;
            quint8 out[5] = {};
            codec.composeCcForBank(11, ctx, out);
            QCOMPARE(int(out[4]), 0x7F);
        }
    }

    // Bank 12 — HL2 has same MOX behavior as Standard (forces 0x1F under MOX),
    // no RedPitaya-special-case needed since HL2 is never confused with RedPitaya.
    // Source: mi0bot networkproto1.c:1107-1111 [@c26a8a4]
    void bank12_mox_forces_0x1F_standard_behavior() {
        P1CodecHl2 codec;
        CodecContext ctx;
        ctx.mox = true;
        ctx.rxStepAttn[1] = 7;  // ignored under MOX — HL2 forces 0x1F like Standard
        quint8 out[5] = {};
        codec.composeCcForBank(12, ctx, out);
        // Under MOX: C1 = 0x1F | 0x20 = 0x3F (mi0bot networkproto1.c:1107-1109)
        QCOMPARE(int(out[0]), 0x17);  // 0x16 | mox=1
        QCOMPARE(int(out[1]), 0x3F);  // 0x1F | 0x20 enable bit
    }

    // Bank 12 — RX path: uses rxStepAttn[1] unmasked (same as Standard)
    void bank12_rx_uses_user_attn() {
        P1CodecHl2 codec;
        CodecContext ctx;
        ctx.mox = false;
        ctx.rxStepAttn[1] = 7;
        quint8 out[5] = {};
        codec.composeCcForBank(12, ctx, out);
        // RX: C1 = rxStepAttn[1] | 0x20
        QCOMPARE(int(out[1]), 7 | 0x20);
    }

    // Bank 17 — HL2 TX latency / PTT hang (NOT AnvelinaPro3 extra OC)
    // Source: mi0bot networkproto1.c:1162-1168 [@c26a8a4]
    void bank17_hl2_tx_latency_and_ptt_hang() {
        P1CodecHl2 codec;
        CodecContext ctx;
        ctx.hl2PttHang = 0x0A;
        ctx.hl2TxLatency = 0x55;
        quint8 out[5] = {};
        codec.composeCcForBank(17, ctx, out);
        QCOMPARE(int(out[0]), 0x2E);
        QCOMPARE(int(out[1]), 0x00);
        QCOMPARE(int(out[2]), 0x00);
        QCOMPARE(int(out[3]), 0x0A & 0x1F);
        QCOMPARE(int(out[4]), 0x55 & 0x7F);
    }

    // Bank 18 — HL2 reset on disconnect
    // Source: mi0bot networkproto1.c:1170-1176 [@c26a8a4]
    void bank18_hl2_reset_on_disconnect_set() {
        P1CodecHl2 codec;
        CodecContext ctx;
        ctx.hl2ResetOnDisconnect = true;
        quint8 out[5] = {};
        codec.composeCcForBank(18, ctx, out);
        QCOMPARE(int(out[0]), 0x74);
        QCOMPARE(int(out[4]), 0x01);
    }

    void bank18_hl2_reset_on_disconnect_clear() {
        P1CodecHl2 codec;
        CodecContext ctx;
        ctx.hl2ResetOnDisconnect = false;
        quint8 out[5] = {};
        codec.composeCcForBank(18, ctx, out);
        QCOMPARE(int(out[4]), 0x00);
    }

    // HL2 inherits bank0 from Standard — verify RX-only + rxOut encoding
    // matches Standard's byte-lock across all 8 {rxOnlyAnt × rxOut} combinations.
    // Source: Thetis networkproto1.c:453-468 + netInterface.c:479-481
    // [v2.10.3.13 @501e3f5]
    void bank0_c3_rxOnly_and_rxOut_byteLock_hl2_parity() {
        struct Case { int rxOnly; bool rxOut; quint8 expectedMask; };
        const std::array<Case, 8> cases = {{
            {0, false, 0b0000'0000},  // no RX-only path, no bypass
            {1, false, 0b0010'0000},  // _Rx_1_In (bit5)
            {2, false, 0b0100'0000},  // _Rx_2_In (bit6)
            {3, false, 0b0110'0000},  // _XVTR_Rx_In (bits5+6)
            {0, true,  0b1000'0000},  // bypass only (bit7)
            {1, true,  0b1010'0000},  // RX1_In + bypass
            {2, true,  0b1100'0000},  // RX2_In + bypass
            {3, true,  0b1110'0000},  // XVTR + bypass
        }};

        for (const auto& tc : cases) {
            CodecContext ctx{};
            ctx.rxOnlyAnt = tc.rxOnly;
            ctx.rxOut     = tc.rxOut;
            quint8 out[5] = {};
            P1CodecHl2 codec;
            codec.composeCcForBank(0, ctx, out);

            // Mask off bits 0-4 so preamp/dither/random defaults don't contaminate.
            const quint8 rxOnlyMask = out[3] & 0b1110'0000;
            QCOMPARE(int(rxOnlyMask), int(tc.expectedMask));
        }
    }

    // Banks 0-10 unchanged from Standard — spot-check bank 10 (Alex filters)
    void bank10_alex_filter_passthrough() {
        P1CodecHl2 codec;
        CodecContext ctx;
        ctx.alexHpfBits = 0x01;
        ctx.alexLpfBits = 0x01;
        quint8 out[5] = {};
        codec.composeCcForBank(10, ctx, out);
        QCOMPARE(int(out[0]), 0x12);
        QCOMPARE(int(out[3]) & 0x7F, 0x01);
        QCOMPARE(int(out[4]), 0x01);
    }
};

QTEST_APPLESS_MAIN(TestP1CodecHl2)
#include "tst_p1_codec_hl2.moc"
