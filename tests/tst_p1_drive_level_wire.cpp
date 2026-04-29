// no-port-check: test-only — Thetis and mi0bot file names appear only in
// source-cite comments that document which upstream line each assertion
// verifies.  No upstream logic is ported here; this file is NereusSDR-original.
//
// Wire-byte snapshot tests for P1RadioConnection::setTxDrive() — drive
// level lands in bank 10 C1 byte (0..255).
//
// Bug history (3M-1c HL2 bench triage 2026-04-29):
//   `setTxDrive(int)` was a no-op stub from 3M-1a (intended Task 7 fill-in
//   that never happened).  The codec correctly read `ctx.txDrive = m_txDrive`
//   into bank 10 C1, but `m_txDrive` was initialised to 0 and never written.
//   Result: every P1 family member (Atlas / Hermes / HermesII / Angelia /
//   Orion / HL2) shipped silent on TUN and SSB voice TX because the radio
//   received drive level = 0.  Latent because PR #149 (3M-1b SSB voice)
//   bench-tested only on ANAN-G2 (P2), whose own `setTxDrive` works.
//
// Wire layout cite:
//   Thetis ChannelMaster/networkproto1.c:579 [v2.10.3.13]:
//     C1 = prn->tx[0].drive_level & 0xFF;
//   mi0bot networkproto1.c:1061 [@c26a8a4]: identical encoding.
//
// Standard and HL2 codec paths both write drive level identically; both
// codec paths are exercised below to lock both in.
#include <QtTest/QtTest>
#include "core/P1RadioConnection.h"
#include "core/HpsdrModel.h"

using namespace NereusSDR;

class TestP1DriveLevelWire : public QObject {
    Q_OBJECT
private slots:

    // ── 1. Default state: drive level is zero ──────────────────────────────
    // Fresh P1RadioConnection has m_txDrive = 0 → bank 10 C1 = 0.
    void defaultState_driveLevelZero() {
        P1RadioConnection conn;
        conn.setBoardForTest(HPSDRHW::HermesII);  // P1CodecStandard
        const QByteArray bank10 = conn.captureBank10ForTest();
        QCOMPARE(bank10.size(), 5);
        QCOMPARE(quint8(bank10[1]), quint8(0));
    }

    // ── 2. setTxDrive(128) lands in C1 (Standard codec) ────────────────────
    void setTxDrive_midRange_landsInC1_standard() {
        P1RadioConnection conn;
        conn.setBoardForTest(HPSDRHW::HermesII);
        conn.setTxDrive(128);
        const QByteArray bank10 = conn.captureBank10ForTest();
        QCOMPARE(quint8(bank10[1]), quint8(128));
    }

    // ── 3. setTxDrive(255) lands in C1 (Standard codec, max) ───────────────
    void setTxDrive_max_landsInC1_standard() {
        P1RadioConnection conn;
        conn.setBoardForTest(HPSDRHW::HermesII);
        conn.setTxDrive(255);
        const QByteArray bank10 = conn.captureBank10ForTest();
        QCOMPARE(quint8(bank10[1]), quint8(255));
    }

    // ── 4. setTxDrive(0) explicit → C1 = 0 (Standard codec) ────────────────
    void setTxDrive_zero_landsInC1_standard() {
        P1RadioConnection conn;
        conn.setBoardForTest(HPSDRHW::HermesII);
        conn.setTxDrive(99);
        conn.setTxDrive(0);
        const QByteArray bank10 = conn.captureBank10ForTest();
        QCOMPARE(quint8(bank10[1]), quint8(0));
    }

    // ── 5. Out-of-range above clamps to 255 (Standard codec) ───────────────
    // Per setTxDrive contract: qBound(0, level, 255).
    void setTxDrive_aboveRange_clampsTo255_standard() {
        P1RadioConnection conn;
        conn.setBoardForTest(HPSDRHW::HermesII);
        conn.setTxDrive(500);
        const QByteArray bank10 = conn.captureBank10ForTest();
        QCOMPARE(quint8(bank10[1]), quint8(255));
    }

    // ── 6. Out-of-range below clamps to 0 (Standard codec) ─────────────────
    void setTxDrive_belowRange_clampsTo0_standard() {
        P1RadioConnection conn;
        conn.setBoardForTest(HPSDRHW::HermesII);
        conn.setTxDrive(-10);
        const QByteArray bank10 = conn.captureBank10ForTest();
        QCOMPARE(quint8(bank10[1]), quint8(0));
    }

    // ── 7. setTxDrive forces bank-10 flush on next frame (Standard codec) ──
    // Codex P2 safety-effect pattern: changing drive level must reach the
    // wire within ≤1 EP2 frame, not at the next round-robin pass.
    void setTxDrive_forcesBank10NextFlag_standard() {
        P1RadioConnection conn;
        conn.setBoardForTest(HPSDRHW::HermesII);
        QVERIFY(!conn.forceBank10NextForTest());
        conn.setTxDrive(50);
        QVERIFY(conn.forceBank10NextForTest());
    }

    // ── 8. Idempotent setTxDrive does NOT toggle the force-bank-10 flag ────
    // After a no-op setTxDrive(same value), the flag should not be touched.
    // (We can't easily clear the flag mid-test without sendCommandFrame, so
    // we verify the second call is a no-op for state — drive level
    // unchanged, no warnings.)
    void setTxDrive_idempotent_noStateChange_standard() {
        P1RadioConnection conn;
        conn.setBoardForTest(HPSDRHW::HermesII);
        conn.setTxDrive(77);
        const QByteArray bank10a = conn.captureBank10ForTest();
        conn.setTxDrive(77);
        const QByteArray bank10b = conn.captureBank10ForTest();
        QCOMPARE(quint8(bank10a[1]), quint8(77));
        QCOMPARE(quint8(bank10b[1]), quint8(77));
    }

    // ── 9. setTxDrive(128) lands in C1 (HL2 codec) ─────────────────────────
    // HL2 path through P1CodecHl2::composeCcForBank case 10.  Drive byte
    // encoding is identical to Standard codec (mi0bot networkproto1.c:1061
    // [@c26a8a4] = upstream Thetis networkproto1.c:579 [v2.10.3.13]).
    // This is THE critical regression path — without setTxDrive working,
    // HL2 hardware never produces RF.
    void setTxDrive_midRange_landsInC1_hl2() {
        P1RadioConnection conn;
        conn.setBoardForTest(HPSDRHW::HermesLite);  // P1CodecHl2
        conn.setTxDrive(128);
        const QByteArray bank10 = conn.captureBank10ForTest();
        QCOMPARE(quint8(bank10[1]), quint8(128));
    }

    // ── 10. setTxDrive(0) explicit → C1 = 0 (HL2 codec) ────────────────────
    void setTxDrive_zero_landsInC1_hl2() {
        P1RadioConnection conn;
        conn.setBoardForTest(HPSDRHW::HermesLite);
        conn.setTxDrive(50);
        conn.setTxDrive(0);
        const QByteArray bank10 = conn.captureBank10ForTest();
        QCOMPARE(quint8(bank10[1]), quint8(0));
    }

    // ── 11. setTxDrive(255) max (HL2 codec) ────────────────────────────────
    void setTxDrive_max_landsInC1_hl2() {
        P1RadioConnection conn;
        conn.setBoardForTest(HPSDRHW::HermesLite);
        conn.setTxDrive(255);
        const QByteArray bank10 = conn.captureBank10ForTest();
        QCOMPARE(quint8(bank10[1]), quint8(255));
    }

    // ── 12. setTxDrive forces bank-10 flush on next frame (HL2 codec) ──────
    void setTxDrive_forcesBank10NextFlag_hl2() {
        P1RadioConnection conn;
        conn.setBoardForTest(HPSDRHW::HermesLite);
        QVERIFY(!conn.forceBank10NextForTest());
        conn.setTxDrive(75);
        QVERIFY(conn.forceBank10NextForTest());
    }
};

QTEST_APPLESS_MAIN(TestP1DriveLevelWire)
#include "tst_p1_drive_level_wire.moc"
