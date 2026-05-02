// no-port-check: NereusSDR-original test fixture; cites Thetis source for
// expected wire bit polarity but does not port any Thetis code.
//
// P1 full-parity Task 1.1 — codec-level direct polarity test for mic_ptt.
//
// Asserts that P1CodecStandard::composeCcForBank(11, ctx, out) emits bank 11
// C1 bit 6 (0x40) using DIRECT polarity per Thetis networkproto1.c:597-598
// [v2.10.3.13 @501e3f51]:
//   C1 = ... | ((prn->mic.mic_ptt & 1) << 6);
//
// Pre-fix the codec wrote `!ctx.p1MicPTT` (inverted), mirroring the same bug
// PR #161 (commit ca8cd73) fixed in P1CodecHl2 for HL2.  With p1MicPTT
// default false, the inverted code emitted bit 6 = 1 every CC frame; Hermes-
// class firmware reads bit 6 as "track mic-jack tip as PTT source", so the
// floating mic tip caused phantom PTT signals fighting software MOX → rapid
// T/R relay flutter on TUNE/TX (ANAN-10E bench symptom).
//
// This test mirrors the existing P1CodecHl2 bank 11 mic_ptt test pattern and
// closes the parity gap PR #161 left open for the Standard codec (used by
// Atlas / Hermes / HermesII / Angelia / Orion / OrionMKII / AnvelinaPro3 /
// RedPitaya — every non-HL2 P1 board).

#include <QtTest/QtTest>
#include "core/codec/P1CodecStandard.h"
#include "core/codec/CodecContext.h"

using namespace NereusSDR;

class TestP1CodecStandardBank11Polarity : public QObject {
    Q_OBJECT
private slots:

    // ── 1. Default p1MicPTT=false → C1 bit 6 CLEAR (direct polarity) ─────────
    // Pre-fix the codec emitted bit 6 = 1 here (inverted: !false = 1).  After
    // the P1 full-parity Task 1.1 fix, bit 6 = 0 (direct: false → 0).
    // Source: Thetis networkproto1.c:597-598 [v2.10.3.13 @501e3f51]
    //   C1 = ... | ((prn->mic.mic_ptt & 1) << 6);
    void mic_ptt_direct_polarity_default_false_emits_bit_clear() {
        P1CodecStandard codec;
        CodecContext ctx{};
        ctx.p1MicPTT = false;  // default
        quint8 out[5] = {};
        codec.composeCcForBank(11, ctx, out);
        QCOMPARE(int(out[0]), 0x14);  // C0: bank 11 address with MOX=0
        QCOMPARE(int(out[1] & 0x40), 0x00);  // mic_ptt=false → bit 6 = 0
    }

    // ── 2. p1MicPTT=true → C1 bit 6 SET (direct polarity) ───────────────────
    // Mirror of the HL2 fix in PR #161 — the Standard codec now matches.
    // Source: Thetis networkproto1.c:597-598 [v2.10.3.13 @501e3f51]
    //   C1 = ... | ((prn->mic.mic_ptt & 1) << 6);
    void mic_ptt_direct_polarity_true_emits_bit_set() {
        P1CodecStandard codec;
        CodecContext ctx{};
        ctx.p1MicPTT = true;
        quint8 out[5] = {};
        codec.composeCcForBank(11, ctx, out);
        QCOMPARE(int(out[0]), 0x14);
        QCOMPARE(int(out[1] & 0x40), 0x40);  // mic_ptt=true → bit 6 = 1
    }
};

QTEST_APPLESS_MAIN(TestP1CodecStandardBank11Polarity)
#include "tst_p1_codec_standard_bank11_polarity.moc"
