// no-port-check: test-only — deskhpsdr file names appear only in source-cite
// comments that document which upstream line each assertion verifies.
// No deskhpsdr logic is ported here; this file is NereusSDR-original.
//
// Wire-byte snapshot tests for P2RadioConnection::setMicPTT() (3M-1b Task G.5).
//
// mic_ptt bit position: CmdTx buffer byte 50, bit 2 (mask 0x04).
// POLARITY INVERSION: parameter enabled = true means "PTT is enabled" (intuitive).
// Wire bit is INVERTED: 0 = PTT-enabled (enabled=true), 1 = PTT-disabled (enabled=false).
//
// Source cite:
//   deskhpsdr/src/new_protocol.c:1488-1490 [@120188f]
//     if (mic_ptt_enabled == 0) { // set if disabled
//       transmit_specific_buffer[50] |= 0x04;
//     }
//
//   Thetis console.cs:19758-19764 [v2.10.3.13]:
//     MicPTTDisabled property → NetworkIO.SetMicPTT(Convert.ToInt32(value))
//     confirming the wire convention (bit set = PTT disabled).
//
// Note: P2 bit position (bit 2 = 0x04) differs from P1 bit position
// (bit 6 = 0x40 in C1 of bank 11). Both carry the same inverted semantics.
//
// CmdTx buffer layout (60 bytes, relevant bytes only):
//   byte 50: mic control byte (m_mic.micControl)
//             bit 0 (0x01): line_in       (G.2)
//             bit 1 (0x02): mic_boost     (G.1)
//             bit 2 (0x04): mic_ptt_disabled (INVERTED — G.5)
//             bit 3 (0x08): mic_ptt_tip_bias_ring (INVERTED — G.3)
//             bit 4 (0x10): mic_bias      (G.4)
//             bit 5 (0x20): mic_xlr (Saturn/XLR only)
//   byte 51: line_in gain
//   bytes 57-59: TX step attenuators
//
// Test seam: composeCmdTxForTest() in P2RadioConnection.h (NEREUS_BUILD_TESTS)
// exposes the CmdTx buffer composition without needing a live socket.
#include <QtTest/QtTest>
#include "core/P2RadioConnection.h"

using namespace NereusSDR;

class TestP2MicPttWire : public QObject {
    Q_OBJECT
private slots:

    // ── 1. Default state: byte 50 bit 2 is SET (PTT disabled by default) ─────
    // m_micPTT defaults false (PTT not enabled), so CmdTx byte 50 bit 2 must be 1
    // (PTT-disabled-flag set — polarity inverted).
    // Source: deskhpsdr/src/new_protocol.c:1488-1490 [@120188f]
    void defaultState_byte50Bit2IsSet() {
        P2RadioConnection conn;
        quint8 buf[60] = {};
        conn.composeCmdTxForTest(buf);
        // Default m_micPTT=false → wire bit 2 = 1 (PTT disabled).
        QCOMPARE(int(buf[50] & 0x04), 0x04);
    }

    // ── 2. setMicPTT(true) [enabled] → byte 50 bit 2 clear ───────────────────
    // enabled=true → PTT enabled → bit 2 = 0 (PTT-disabled-flag cleared).
    // Source: deskhpsdr/src/new_protocol.c:1488-1490 [@120188f]
    //   if (mic_ptt_enabled == 0) { transmit_specific_buffer[50] |= 0x04; }
    void setMicPTTTrue_byte50Bit2IsClear() {
        P2RadioConnection conn;
        conn.setMicPTT(true);
        quint8 buf[60] = {};
        conn.composeCmdTxForTest(buf);
        QCOMPARE(int(buf[50] & 0x04), 0);
    }

    // ── 3. setMicPTT(false) [disabled] → byte 50 bit 2 set ───────────────────
    void setMicPTTFalse_byte50Bit2IsSet() {
        P2RadioConnection conn;
        conn.setMicPTT(true);   // enable first
        conn.setMicPTT(false);  // then disable
        quint8 buf[60] = {};
        conn.composeCmdTxForTest(buf);
        QCOMPARE(int(buf[50] & 0x04), 0x04);
    }

    // ── 4. Round-trip: true → false → true ────────────────────────────────────
    void roundTrip_trueToFalseToTrue() {
        P2RadioConnection conn;

        conn.setMicPTT(true);
        quint8 buf1[60] = {};
        conn.composeCmdTxForTest(buf1);
        QCOMPARE(int(buf1[50] & 0x04), 0);

        conn.setMicPTT(false);
        quint8 buf2[60] = {};
        conn.composeCmdTxForTest(buf2);
        QCOMPARE(int(buf2[50] & 0x04), 0x04);

        conn.setMicPTT(true);
        quint8 buf3[60] = {};
        conn.composeCmdTxForTest(buf3);
        QCOMPARE(int(buf3[50] & 0x04), 0);
    }

    // ── 5. setMicPTT(true) does NOT touch byte-50 bits 0-1 (G.1/G.2 bits) ───
    // Cross-bit guard: mic_ptt (bit 2 = 0x04) must not collide with
    // line_in (bit 0 = 0x01) or mic_boost (bit 1 = 0x02).
    // Source: deskhpsdr/src/new_protocol.c:1480-1490 [@120188f]
    void micBoostAndLineInBits_unaffectedByMicPTT() {
        P2RadioConnection conn;
        conn.setMicPTT(true);
        quint8 buf[60] = {};
        conn.composeCmdTxForTest(buf);
        // Bit 2 (mic_ptt enabled) must be 0.
        QCOMPARE(int(buf[50] & 0x04), 0);
        // Bits 0 (line_in) and 1 (mic_boost) must be 0 — not set by setMicPTT.
        QCOMPARE(int(buf[50] & 0x01), 0);
        QCOMPARE(int(buf[50] & 0x02), 0);
    }

    // ── 6. setMicPTT(true) does NOT touch byte-50 bit 3 (G.3 mic_trs) ────────
    // Cross-bit guard for G.3: mic_trs (bit 3 = 0x08) must stay at its
    // default value when only mic_ptt changes.
    void setMicPTTTrue_doesNotTouchBit3() {
        P2RadioConnection conn;
        conn.setMicPTT(true);
        quint8 buf[60] = {};
        conn.composeCmdTxForTest(buf);
        // Bit 3 (mic_ptt_tip_bias_ring, default: m_micTipRing=true → inverted → bit 0).
        QCOMPARE(int(buf[50] & 0x08), 0);
    }

    // ── 7. Idempotent: setMicPTT(true) twice → bit stays 0 ──────────────────
    void idempotency_setMicPTTTrueTwice_bitStays0() {
        P2RadioConnection conn;
        conn.setMicPTT(true);
        conn.setMicPTT(true);
        quint8 buf[60] = {};
        conn.composeCmdTxForTest(buf);
        QCOMPARE(int(buf[50] & 0x04), 0);
    }

    // ── 8. Codec path (OrionMKII): mic_ptt bit lands at byte 50 bit 2 ────────
    // setBoardForTest(OrionMKII) installs P2CodecOrionMkII which reads
    // ctx.p2MicControl for byte 50.  setMicPTT must set the same bit
    // in m_mic.micControl so the codec path agrees with the legacy path.
    // Source: deskhpsdr/src/new_protocol.c:1488-1490 [@120188f]
    void codecPath_orionMkII_micPttBitLandsAtByte50Bit2() {
        P2RadioConnection conn;
        conn.setBoardForTest(HPSDRHW::OrionMKII);

        conn.setMicPTT(true);
        quint8 buf[60] = {};
        conn.composeCmdTxForTest(buf);
        QCOMPARE(int(buf[50] & 0x04), 0);

        conn.setMicPTT(false);
        memset(buf, 0, sizeof(buf));
        conn.composeCmdTxForTest(buf);
        QCOMPARE(int(buf[50] & 0x04), 0x04);
    }

    // ── 9. Codec path (Saturn/ANAN-G2): same mic_ptt behaviour ──────────────
    void codecPath_saturn_micPttBitLandsAtByte50Bit2() {
        P2RadioConnection conn;
        conn.setBoardForTest(HPSDRHW::Saturn);

        conn.setMicPTT(true);
        quint8 buf[60] = {};
        conn.composeCmdTxForTest(buf);
        QCOMPARE(int(buf[50] & 0x04), 0);

        conn.setMicPTT(false);
        memset(buf, 0, sizeof(buf));
        conn.composeCmdTxForTest(buf);
        QCOMPARE(int(buf[50] & 0x04), 0x04);
    }

    // ── 10. Byte 51 (line_in gain) unaffected by setMicPTT ──────────────────
    // Byte 51 = line_in gain (m_mic.lineInGain). Must stay at its
    // default-encoded value when only the mic_ptt bit changes.
    void byte51_unaffectedByMicPTT() {
        P2RadioConnection conn;
        conn.setMicPTT(true);
        quint8 buf[60] = {};
        conn.composeCmdTxForTest(buf);
        // Compare byte 51 with default state (no mic_ptt change).
        quint8 buf_off[60] = {};
        P2RadioConnection conn2;
        conn2.composeCmdTxForTest(buf_off);
        QCOMPARE(int(buf[51]), int(buf_off[51]));
    }
};

QTEST_APPLESS_MAIN(TestP2MicPttWire)
#include "tst_p2_mic_ptt_wire.moc"
