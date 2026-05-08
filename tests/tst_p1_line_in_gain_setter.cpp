// no-port-check: test-only — Thetis file names appear only in source-cite
// comments that document which upstream line each assertion verifies.
// No Thetis logic is ported here; this file is NereusSDR-original.
//
// Wire-byte snapshot tests for P1RadioConnection::setLineInGain()
// (Task 2.1 of the P1 full-parity epic).
//
// line_in_gain bit positions: bank 11 (C0=0x14) C2 byte, low 5 bits
// (mask 0x1F).  Values clamp to [0, 31] at the API boundary; values above
// 31 would otherwise spill into bit 5+ which the codec already masks via
// `(ctx.p1LineInGain & 0x1F)`, but the API contract still clamps so the
// stored state matches the wire bytes 1:1.
//
// Source cite:
//   Thetis ChannelMaster/networkproto1.c:600 [v2.10.3.13]
//     case 11:
//       C2 = (prn->mic.line_in_gain & 0b00011111) | ((prn->puresignal_run & 1) << 6);
//     → line_in_gain occupies low 5 bits of C2 in bank 11.
//
// Bank 11 byte layout for C0=0x14:
//   out[0] = 0x14 (C0 — address ORed with MOX bit 0)
//   out[1] = preamp bits + mic_trs/bias/ptt
//   out[2] = line_in_gain (low 5 bits) + puresignal_run (bit 6)
//   out[3] = user digital outputs (low 4 bits)
//   out[4] = ADC0 RX step ATT (5-bit + 0x20 enable)
//
// Test seam: captureBank11ForTest() calls composeCcForBankForTest(11, ...)
// which routes through composeCcForBank().  The codec path (when
// setBoardForTest() is called) reads ctx.p1LineInGain and emits the low 5
// bits.  The legacy compose path emits 0 for C2 unconditionally — this is
// pre-existing behaviour predating Task 2.1; tests here therefore use
// setBoardForTest() to exercise the codec path.

#include <QtTest/QtTest>
#include "core/P1RadioConnection.h"

using namespace NereusSDR;

class TestP1LineInGainSetter : public QObject {
    Q_OBJECT
private slots:

    // ── 1. Default state: line_in_gain low 5 bits are CLEAR ──────────────────
    // m_lineInGain defaults 0, so C2 low 5 bits must be 0.
    // Source: Thetis networkproto1.c:600 [v2.10.3.13]
    //   C2 = (prn->mic.line_in_gain & 0b00011111) | ... → 0 when line_in_gain = 0.
    void defaultState_lineInGainBitsAreClear() {
        P1RadioConnection conn;
        conn.setBoardForTest(HPSDRHW::HermesII);  // → P1CodecStandard (codec path)

        const QByteArray bank11 = conn.captureBank11ForTest();
        QCOMPARE(bank11.size(), 5);
        // C2 = bank11[2], low 5 bits (0x1F) must be 0 by default.
        QCOMPARE(int(quint8(bank11[2]) & 0x1F), 0);
    }

    // ── 2. setLineInGain(15) → C2 low 5 bits = 0x0F ──────────────────────────
    // Mid-range value — every relevant bit toggled, no clamping.
    // Source: Thetis networkproto1.c:600 [v2.10.3.13]
    void setLineInGainMidRange_landsInLow5Bits() {
        P1RadioConnection conn;
        conn.setBoardForTest(HPSDRHW::HermesII);
        conn.setLineInGain(15);

        const QByteArray bank11 = conn.captureBank11ForTest();
        QCOMPARE(bank11.size(), 5);
        QCOMPARE(int(quint8(bank11[2]) & 0x1F), 0x0F);
    }

    // ── 3. setLineInGain(31) → C2 low 5 bits = 0x1F (max) ────────────────────
    // Max value — all 5 bits set.
    // Source: Thetis networkproto1.c:600 [v2.10.3.13]
    void setLineInGainMax_allFiveBitsSet() {
        P1RadioConnection conn;
        conn.setBoardForTest(HPSDRHW::HermesII);
        conn.setLineInGain(31);

        const QByteArray bank11 = conn.captureBank11ForTest();
        QCOMPARE(int(quint8(bank11[2]) & 0x1F), 0x1F);
    }

    // ── 4. Negative input clamps to 0 ────────────────────────────────────────
    // qBound(0, -5, 31) = 0.  Wire bits stay clear.
    void setLineInGainNegative_clampsToZero() {
        P1RadioConnection conn;
        conn.setBoardForTest(HPSDRHW::HermesII);

        // First put a non-zero value in so we can see the clamp result clears.
        conn.setLineInGain(7);
        QCOMPARE(int(quint8(conn.captureBank11ForTest()[2]) & 0x1F), 7);

        conn.setLineInGain(-5);
        QCOMPARE(int(quint8(conn.captureBank11ForTest()[2]) & 0x1F), 0);
    }

    // ── 5. Above-max input clamps to 31 ──────────────────────────────────────
    // qBound(0, 50, 31) = 31.  All 5 bits set.
    void setLineInGainAbove31_clampsTo31() {
        P1RadioConnection conn;
        conn.setBoardForTest(HPSDRHW::HermesII);

        conn.setLineInGain(50);
        QCOMPARE(int(quint8(conn.captureBank11ForTest()[2]) & 0x1F), 0x1F);
    }

    // ── 6. setLineInGain does NOT touch C2 bit 6 (puresignal_run) ────────────
    // Cross-bit guard: line_in_gain (low 5 bits) and puresignal_run (bit 6 =
    // 0x40) are independent OR'd fields in the same C2 byte.
    // Source: Thetis networkproto1.c:600 [v2.10.3.13]
    //   C2 = (prn->mic.line_in_gain & 0b00011111) | ((prn->puresignal_run & 1) << 6);
    void setLineInGain_doesNotTouchPuresignalBit() {
        P1RadioConnection conn;
        conn.setBoardForTest(HPSDRHW::HermesII);

        // Default p1PuresignalRun = false → bit 6 = 0.
        conn.setLineInGain(31);

        const QByteArray bank11 = conn.captureBank11ForTest();
        // Low 5 bits = 0x1F.
        QCOMPARE(int(quint8(bank11[2]) & 0x1F), 0x1F);
        // Bit 6 (puresignal_run) must remain 0.
        QCOMPARE(int(quint8(bank11[2]) & 0x40), 0);
        // Bits 5 and 7 must also be 0 (only bits 0-4 + 6 are defined for C2).
        QCOMPARE(int(quint8(bank11[2]) & 0xA0), 0);
    }

    // ── 7. Bank 11 C0 address must remain 0x14 ───────────────────────────────
    // Source: Thetis networkproto1.c:594 [v2.10.3.13] — case 11 sets C0 |= 0x14.
    void setLineInGain_c0AddressIs0x14() {
        P1RadioConnection conn;
        conn.setBoardForTest(HPSDRHW::HermesII);
        conn.setLineInGain(7);

        const QByteArray bank11 = conn.captureBank11ForTest();
        // C0 = bank11[0]. Bits 7..1 = address 0x14; bit 0 = MOX.
        // MOX defaults false → C0 should be exactly 0x14.
        QCOMPARE(int(quint8(bank11[0]) & 0xFE), 0x14);
    }

    // ── 8. Round-trip: 0 → 23 → 0 ─────────────────────────────────────────────
    void roundTrip_zeroNonZeroZero_bitsTrack() {
        P1RadioConnection conn;
        conn.setBoardForTest(HPSDRHW::HermesII);

        conn.setLineInGain(0);
        QCOMPARE(int(quint8(conn.captureBank11ForTest()[2]) & 0x1F), 0);

        conn.setLineInGain(23);
        QCOMPARE(int(quint8(conn.captureBank11ForTest()[2]) & 0x1F), 23);

        conn.setLineInGain(0);
        QCOMPARE(int(quint8(conn.captureBank11ForTest()[2]) & 0x1F), 0);
    }

    // ── 9. Flush flag: setLineInGain sets m_forceBank11Next (Codex P2) ──────
    // Reuses m_forceBank11Next from G.3 (same bank 11 byte family).  Flush
    // flag must be set BEFORE the idempotent guard (Codex P2 pattern).
    void setLineInGain_setsForceBank11Next() {
        P1RadioConnection conn;

        // Initially the flush flag must be false.
        QCOMPARE(conn.forceBank11NextForTest(), false);

        conn.setLineInGain(7);
        QCOMPARE(conn.forceBank11NextForTest(), true);
    }

    // ── 10. Flush flag set even when value doesn't change (Codex P2) ─────────
    // Codex P2: even when the stored value doesn't change, the safety effect
    // (flush flag) must still fire.
    void setLineInGainRepeat_stillSetsForceFlag() {
        P1RadioConnection conn;
        conn.setLineInGain(0);  // no state change (default is 0)
        QCOMPARE(conn.forceBank11NextForTest(), true);
    }

    // ── 11. Idempotent: setLineInGain(15) twice doesn't crash ────────────────
    void setLineInGainTwice_doesNotCrash() {
        P1RadioConnection conn;
        conn.setBoardForTest(HPSDRHW::HermesII);
        conn.setLineInGain(15);
        conn.setLineInGain(15);

        const QByteArray bank11 = conn.captureBank11ForTest();
        QCOMPARE(int(quint8(bank11[2]) & 0x1F), 15);
    }

    // ── 12. Codec path (HL2): setLineInGain propagates to bank 11 C2 ─────────
    // PR #212 follow-up bench fix (J.J. KG4VCF, 2026-05-07): the previous
    // version of P1CodecHl2 emitted out[2] = 0 unconditionally and this test
    // pinned that buggy behaviour.  Per mi0bot networkproto1.c:1097
    // [v2.10.3.13-beta2]:
    //   C2 = (mic.line_in_gain & 0b00011111) | ((puresignal_run & 1) << 6);
    // HL2 firmware does honour the field (the mic jack is repurposed for
    // line-in on the N2ADR add-on, and the field also carries the
    // puresignal_run bit which is critical to PS calibration).  This test
    // verifies the correct propagation.
    void setLineInGain_hl2CodecPath_propagatesToC2() {
        P1RadioConnection conn;
        conn.setBoardForTest(HPSDRHW::HermesLite);  // → P1CodecHl2
        conn.setLineInGain(7);

        const QByteArray bank11 = conn.captureBank11ForTest();
        QCOMPARE(bank11.size(), 5);
        // HL2 codec emits out[2] = (line_in_gain & 0x1F) | (ps_run << 6).
        // ps_run defaults to 0 (no PS active), so C2 = 7 (line_in_gain only).
        QCOMPARE(int(quint8(bank11[2]) & 0x1F), 7);
        // Flush flag still fires — Codex P2 safety contract is independent
        // of the codec's emission decision.
        QCOMPARE(conn.forceBank11NextForTest(), true);
    }
};

QTEST_APPLESS_MAIN(TestP1LineInGainSetter)
#include "tst_p1_line_in_gain_setter.moc"
