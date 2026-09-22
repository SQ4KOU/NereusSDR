// =================================================================
// tests/tst_p2_wideband_enable_byte.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test infrastructure.
//
// Phase 3F Sub-Epic F Task 1: P2 composeCmdGeneral writes packetbuf[23]
// wideband per-ADC enable mask per Thetis network.c:879 [v2.10.3.15].
//
// Source-first correction note: the original Sub-Epic F plan targeted
// composeCmdRx byte 23. That was wrong: CmdRx byte 23 is rx[1].rx_adc
// (RX1 ADC selector) per Thetis network.c:1118, not the wideband enable
// mask. The mask lives in CmdGeneral byte 23 per Thetis network.c:879.
// See plan revision note at
// docs/architecture/2026-05-26-phase3f-sub-epic-f-wideband-plan.md
// (Task 1) for full rationale.
// =================================================================
#include <QtTest/QtTest>
#include "core/P2RadioConnection.h"
#include "core/WidebandFrameAccumulator.h"

using namespace NereusSDR;

class TestP2WidebandEnableByte : public QObject {
    Q_OBJECT
private slots:
    void compose_cmd_general_writes_packetbuf_23_when_wideband_enabled()
    {
        P2RadioConnection conn;
        conn.setWidebandEnabled(0, true);  // enable ADC0 wideband

        quint8 buf[60] = {0};
        conn.composeCmdGeneralForTest(buf);

        // packetbuf[23] should have bit 0 set (ADC0 enabled).
        QCOMPARE(quint8(buf[23] & 0x01), quint8(0x01));
    }

    void compose_cmd_general_writes_0_when_no_wideband()
    {
        P2RadioConnection conn;
        quint8 buf[60] = {0};
        conn.composeCmdGeneralForTest(buf);
        QCOMPARE(quint8(buf[23]), quint8(0x00));
    }

    void per_adc_enable_independent()
    {
        P2RadioConnection conn;
        conn.setWidebandEnabled(0, true);
        conn.setWidebandEnabled(1, true);
        quint8 buf[60] = {0};
        conn.composeCmdGeneralForTest(buf);
        QCOMPARE(quint8(buf[23] & 0x03), quint8(0x03));  // both bits set
    }

    void restarting_capture_discards_the_old_partial_frame()
    {
        P2RadioConnection conn;
        const auto accumulators = conn.findChildren<WidebandFrameAccumulator*>();
        QCOMPARE(accumulators.size(), 8);
        WidebandFrameAccumulator* adc0 = accumulators.at(0);
        QSignalSpy frames(&conn, &P2RadioConnection::widebandFrameReady);
        conn.setWidebandEnabled(0, true);
        const QByteArray oldPayload(1024, char(0x10));
        for (int seq = 0; seq < 16; ++seq) {
            adc0->pushPacket(seq, oldPayload);
        }
        conn.setWidebandEnabled(0, false);
        conn.setWidebandEnabled(0, true);
        const QByteArray newPayload(1024, char(0x20));
        for (int seq = 16; seq < 32; ++seq) {
            adc0->pushPacket(seq, newPayload);
        }
        // Feed the owned assembler directly: the boundary under test is
        // setWidebandEnabled's lifetime reset, not UDP parsing. Trailing
        // packets from an old burst cannot complete old data in a new view.
        QCOMPARE(frames.count(), 0);
        for (int seq = 0; seq < 32; ++seq) {
            adc0->pushPacket(seq, newPayload);
        }
        QCOMPARE(frames.count(), 1);
        const QVector<float> samples = frames.first().at(1).value<QVector<float>>();
        QCOMPARE(samples.size(), 16384);
        QCOMPARE(samples.first(), float(0x2020) / 32768.0f);
        QCOMPARE(samples.last(), samples.first());
    }

    void duplicate_enable_and_other_adc_do_not_discard_live_capture()
    {
        P2RadioConnection conn;
        const auto accumulators = conn.findChildren<WidebandFrameAccumulator*>();
        QCOMPARE(accumulators.size(), 8);
        QSignalSpy frames(&conn, &P2RadioConnection::widebandFrameReady);
        conn.setWidebandEnabled(0, true);
        conn.setWidebandEnabled(1, true);
        const QByteArray payload(1024, char(0x20));
        for (int seq = 0; seq < 16; ++seq) {
            accumulators.at(1)->pushPacket(seq, payload);
        }
        conn.setWidebandEnabled(1, true);
        conn.setWidebandEnabled(0, false);
        for (int seq = 16; seq < 32; ++seq) {
            accumulators.at(1)->pushPacket(seq, payload);
        }
        QCOMPARE(frames.count(), 1);
        QCOMPARE(frames.first().at(0).toInt(), 1);
    }

    void disconnect_discards_partial_capture()
    {
        P2RadioConnection conn;
        const auto accumulators = conn.findChildren<WidebandFrameAccumulator*>();
        QCOMPARE(accumulators.size(), 8);
        QSignalSpy frames(&conn, &P2RadioConnection::widebandFrameReady);
        conn.setWidebandEnabled(0, true);
        const QByteArray payload(1024, char(0x20));
        accumulators.at(0)->pushPacket(0, payload);
        conn.disconnect();
        for (int seq = 1; seq < 32; ++seq) {
            accumulators.at(0)->pushPacket(seq, payload);
        }
        QCOMPARE(frames.count(), 0);
    }
};

QTEST_MAIN(TestP2WidebandEnableByte)
#include "tst_p2_wideband_enable_byte.moc"
