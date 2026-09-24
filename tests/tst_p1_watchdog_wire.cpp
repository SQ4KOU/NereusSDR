// no-port-check: test-only -- HL2 firmware file names and Thetis source
// paths appear only in source-cite comments that document which upstream line
// each assertion verifies.  No Thetis or deskhpsdr logic is ported here;
// this file is NereusSDR-original.
//
// Wire-byte snapshot tests for the RUNSTOP packet byte 3 (3M-1a Task E.5,
// rewritten for R-R3-49).
//
// R-R3-49: the Network Watchdog setting is NOT sent on Protocol 1. Thetis
// and mi0bot-Thetis send the same start and stop commands whatever the
// setting; on P1 the setting only sets how long the read loop waits for data
// (tst_network_watchdog covers that).
//   Thetis networkproto1.c:50 [v2.10.3.15]:  outpacket.packetbuf[3] = 0x01;
//   Thetis networkproto1.c:85 [v2.10.3.15]:  outpacket.packetbuf[3] = 0x00;
//   mi0bot-Thetis networkproto1.c:50, 85 [@c26a8a4]: the same.
//
// Bit 7 of byte 3 is the HL2 gateware's watchdog_disable
// (Hermes-Lite2/gateware/rtl/dsopenhpsdr1.v:399-400). NereusSDR used to set
// it when the watchdog was off; it now stays 0, so the HL2's own watchdog
// stays on as it does under Thetis.
//
// RUNSTOP packet layout (64 bytes):
//   pkt[0] = 0xEF, pkt[1] = 0xFE, pkt[2] = 0x04
//   pkt[3] = 0x01 start IQ only, 0x02 start IQ + mic, 0x00 stop
//   pkt[4..63] = 0x00 (padding)
//
// Modification history (NereusSDR):
//   2026-09-24: R-R3-49, the setting no longer changes byte 3 (Thetis
//               parity), by J.J. Boyd (KG4VCF), with AI-assisted
//               implementation via Anthropic Claude Code.
#include <QtTest/QtTest>
#include "core/P1RadioConnection.h"

using namespace NereusSDR;

class TestP1WatchdogWire : public QObject {
    Q_OBJECT
private slots:

    // Default (watchdog on): start 0x01, stop 0x00.
    void defaultState_startIs0x01_stopIs0x00() {
        P1RadioConnection conn;
        QVERIFY(conn.isWatchdogEnabled());
        const QByteArray start = conn.metisStartPacketForTest(false);
        QCOMPARE(start.size(), 64);
        QCOMPARE(int(quint8(start[3])), 0x01);
        const QByteArray stop = conn.metisStopPacketForTest();
        QCOMPARE(stop.size(), 64);
        QCOMPARE(int(quint8(stop[3])), 0x00);
    }

    // Watchdog off: the same bytes (networkproto1.c:50, 85).
    void watchdogOff_startAndStopUnchanged() {
        P1RadioConnection conn;
        conn.setWatchdogEnabled(false);
        QVERIFY(!conn.isWatchdogEnabled());
        QCOMPARE(int(quint8(conn.metisStartPacketForTest(false)[3])), 0x01);
        QCOMPARE(int(quint8(conn.metisStartPacketForTest(true)[3])), 0x02);
        QCOMPARE(int(quint8(conn.metisStopPacketForTest()[3])), 0x00);
    }

    // Round trip: on, off, on: byte 3 never changes.
    void roundTrip_byte3NeverChanges() {
        P1RadioConnection conn;
        for (bool on : {true, false, true, false}) {
            conn.setWatchdogEnabled(on);
            QCOMPARE(conn.isWatchdogEnabled(), on);
            QCOMPARE(int(quint8(conn.metisStartPacketForTest(false)[3])), 0x01);
            QCOMPARE(int(quint8(conn.metisStopPacketForTest()[3])), 0x00);
        }
    }

    // Header bytes are always correct (networkproto1.c:47-49).
    void packetHeader_isAlwaysCorrect() {
        P1RadioConnection conn;
        conn.setWatchdogEnabled(false);
        for (const QByteArray& pkt : {conn.metisStartPacketForTest(false),
                                      conn.metisStopPacketForTest()}) {
            QCOMPARE(quint8(pkt[0]), quint8(0xEF));
            QCOMPARE(quint8(pkt[1]), quint8(0xFE));
            QCOMPARE(quint8(pkt[2]), quint8(0x04));
        }
    }

    // Padding bytes 4..63 are always zero (networkproto1.c:45 memset).
    void paddingBytes_areAllZero() {
        P1RadioConnection conn;
        conn.setWatchdogEnabled(false);
        const QByteArray pkt = conn.metisStartPacketForTest(false);
        QCOMPARE(pkt.size(), 64);
        for (int i = 4; i < 64; ++i) {
            QCOMPARE(int(quint8(pkt[i])), 0);
        }
    }
};

QTEST_APPLESS_MAIN(TestP1WatchdogWire)
#include "tst_p1_watchdog_wire.moc"
