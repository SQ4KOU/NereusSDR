// no-port-check: test-only — deskhpsdr file names appear only in source-cite
// comments that document which upstream line each assertion verifies.
// No deskhpsdr logic is ported here; this file is NereusSDR-original.
//
// Storage-only tests for P1RadioConnection::setMicXlr() (3M-1b Task G.6).
//
// P1 is STORAGE-ONLY. "Saturn G2 P2-only feature; P1 hardware has no XLR jack."
// The setter stores m_micXlr for cross-board API consistency but does NOT
// emit any wire bytes. P1 case-10 and case-11 C&C bytes are UNCHANGED
// regardless of m_micXlr value.
//
// P2 source (documented here for cross-reference only):
//   deskhpsdr/src/new_protocol.c:1500-1502 [@120188f]
//     if (mic_input_xlr) { transmit_specific_buffer[50] |= 0x20; }
//   Wire byte: P2 CmdTx byte 50, bit 5 (mask 0x20). Polarity 1=XLR (no inversion).
//
// Bank 10 byte layout for C0=0x12 (case 10):
//   out[0] = 0x12 (C0 — address ORed with MOX bit 0)
//   out[1] = C1: [unused in most boards for mic XLR]
//   out[2] = C2: mic_boost (bit 0), line_in (bit 1) — G.1/G.2
//   out[3] = user digital outputs
//   out[4] = ADC0 RX step ATT (5-bit + 0x20 enable)
//
// Bank 11 byte layout for C0=0x14 (case 11):
//   out[0] = 0x14 (C0 — address ORed with MOX bit 0)
//   out[1] = C1: preamp bits 0-3 | mic_trs bit 4 (INVERTED) | mic_bias bit 5
//              | mic_ptt bit 6 (INVERTED)
//   out[2] = line_in_gain + puresignal
//   out[3] = user digital outputs
//   out[4] = ADC0 RX step ATT (5-bit + 0x20 enable)
//
// Test seam: captureBank10ForTest() and captureBank11ForTest() call
// composeCcForBankForTest() which routes through composeCcForBank().
// Both the codec path (when setBoardForTest() is called) and the legacy
// path are exercised.
//
// Default value: m_micXlr defaults true (Saturn G2 ships with XLR enabled).
// Storage-only: after any setMicXlr call, bank10 AND bank11 must be
// byte-identical to their pre-call state.
#include <QtTest/QtTest>
#include "core/P1RadioConnection.h"

using namespace NereusSDR;

class TestP1MicXlrStorage : public QObject {
    Q_OBJECT
private slots:

    // ── 1. Default: m_micXlr is true (XLR enabled by default) ────────────────
    // The base-class default is m_micXlr{true}. For P1 this is storage-only,
    // so we can only verify via round-trip: setMicXlr(false) must change the
    // stored state, and setMicXlr(true) must restore it, without touching
    // any wire bytes.
    // Source: pre-code review §2.7; TransmitModel::micXlr default in C.2.
    void defaultState_noWireChange_bank10Unchanged() {
        P1RadioConnection conn;
        // Capture baseline bank 10 (before any XLR setter call).
        const QByteArray bank10_before = conn.captureBank10ForTest();
        // Calling setMicXlr with the default value (true) must be a no-op.
        conn.setMicXlr(true);
        const QByteArray bank10_after = conn.captureBank10ForTest();
        QCOMPARE(bank10_after, bank10_before);
    }

    // ── 2. Default: bank11 unchanged when setMicXlr(true) is called ───────────
    void defaultState_noWireChange_bank11Unchanged() {
        P1RadioConnection conn;
        const QByteArray bank11_before = conn.captureBank11ForTest();
        conn.setMicXlr(true);
        const QByteArray bank11_after = conn.captureBank11ForTest();
        QCOMPARE(bank11_after, bank11_before);
    }

    // ── 3. setMicXlr(false): bank10 ALL FIVE BYTES unchanged ────────────────
    // Storage-only assertion (the critical test). After setMicXlr(false),
    // all 5 bytes of bank 10 must be byte-identical to the pre-call state.
    // P1 has no XLR jack — no wire emission is permitted.
    // Source: Saturn G2 P2-only feature — P1 case-10 C&C bytes are unchanged.
    void setMicXlrFalse_bank10AllFiveBytesUnchanged() {
        P1RadioConnection conn;
        const QByteArray bank10_before = conn.captureBank10ForTest();
        QCOMPARE(bank10_before.size(), 5);

        conn.setMicXlr(false);

        const QByteArray bank10_after = conn.captureBank10ForTest();
        QCOMPARE(bank10_after.size(), 5);
        // All 5 bytes must be identical — no wire emission for P1 XLR.
        QCOMPARE(bank10_after[0], bank10_before[0]);
        QCOMPARE(bank10_after[1], bank10_before[1]);
        QCOMPARE(bank10_after[2], bank10_before[2]);
        QCOMPARE(bank10_after[3], bank10_before[3]);
        QCOMPARE(bank10_after[4], bank10_before[4]);
    }

    // ── 4. setMicXlr(false): bank11 ALL FIVE BYTES unchanged ────────────────
    // Same critical assertion for bank 11 (C0=0x14, mic control).
    // The XLR flag must not touch any C1 bit of bank 11.
    // Source: Saturn G2 P2-only feature — P1 case-11 C&C bytes are unchanged.
    void setMicXlrFalse_bank11AllFiveBytesUnchanged() {
        P1RadioConnection conn;
        const QByteArray bank11_before = conn.captureBank11ForTest();
        QCOMPARE(bank11_before.size(), 5);

        conn.setMicXlr(false);

        const QByteArray bank11_after = conn.captureBank11ForTest();
        QCOMPARE(bank11_after.size(), 5);
        // All 5 bytes must be identical — no wire emission for P1 XLR.
        QCOMPARE(bank11_after[0], bank11_before[0]);
        QCOMPARE(bank11_after[1], bank11_before[1]);
        QCOMPARE(bank11_after[2], bank11_before[2]);
        QCOMPARE(bank11_after[3], bank11_before[3]);
        QCOMPARE(bank11_after[4], bank11_before[4]);
    }

    // ── 5. setMicXlr(true): bank10 unchanged (setting the default value) ─────
    // Even setting to the default (true) after a false must leave bank10 clean.
    void setMicXlrFalseThenTrue_bank10Unchanged() {
        P1RadioConnection conn;
        conn.setMicXlr(false);
        const QByteArray bank10_before = conn.captureBank10ForTest();
        conn.setMicXlr(true);
        const QByteArray bank10_after = conn.captureBank10ForTest();
        QCOMPARE(bank10_after, bank10_before);
    }

    // ── 6. setMicXlr(true): bank11 unchanged (round-trip from false) ─────────
    void setMicXlrFalseThenTrue_bank11Unchanged() {
        P1RadioConnection conn;
        conn.setMicXlr(false);
        const QByteArray bank11_before = conn.captureBank11ForTest();
        conn.setMicXlr(true);
        const QByteArray bank11_after = conn.captureBank11ForTest();
        QCOMPARE(bank11_after, bank11_before);
    }

    // ── 7. Idempotent: setMicXlr(false) twice → no crash, same bytes ─────────
    void idempotency_setMicXlrFalseTwice_bank11Stable() {
        P1RadioConnection conn;
        conn.setMicXlr(false);
        const QByteArray bank11_first = conn.captureBank11ForTest();
        conn.setMicXlr(false);  // second call — idempotent guard fires
        const QByteArray bank11_second = conn.captureBank11ForTest();
        QCOMPARE(bank11_second, bank11_first);
    }

    // ── 8. Codec path (Standard): bank11 still unchanged by setMicXlr ────────
    // setBoardForTest(HermesII) installs P1CodecStandard. The codec path
    // must also show no change to bank11 from any XLR setter call.
    // This verifies that CodecContext does NOT propagate an XLR flag to P1.
    void setMicXlrFalse_standardCodecPath_bank11Unchanged() {
        P1RadioConnection conn;
        conn.setBoardForTest(HPSDRHW::HermesII);  // → P1CodecStandard
        const QByteArray bank11_before = conn.captureBank11ForTest();
        conn.setMicXlr(false);
        const QByteArray bank11_after = conn.captureBank11ForTest();
        QCOMPARE(bank11_after, bank11_before);
    }
};

QTEST_APPLESS_MAIN(TestP1MicXlrStorage)
#include "tst_p1_mic_xlr_storage.moc"
