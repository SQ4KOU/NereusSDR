// no-port-check: NereusSDR-original test. It cites Thetis obbuffs.c /
// network.c, deskhpsdr new_protocol.c and the P2 gateware only for the
// behaviour and sizes it expects; no upstream logic is ported here.
//
// R-IOS-13, R-R3-42: the Protocol 2 transmit I/Q send path never drops a
// sample on a transient stall.
//
// The Rock's Core, 2026-09-26: 7806 "P2 TX I/Q ring buffer overflow" lines in
// four keys. The drain was a 5 ms timer on the connection thread sending a
// fixed 4 frames a tick; a late tick was never made up, and the ring held
// 42.7 ms. These tests drive the send path on simulated time:
//   - the producer pushes a 256-sample block per radio mic packet
//     (64 mic samples at 48 kHz = 1.333 ms, x4 to 192 kHz), as the TX pump;
//   - the send thread makes one pass a millisecond (serviceTxIqSendForTest).
// A connection-thread stall holds the mic packets, so the producer stops and
// then bursts the backlog; a send-thread stall stops the passes.
//
// Invariants checked in every scenario:
//   - no sample is lost: every produced sample reaches the wire, in order;
//   - the radio's buffer (4096 samples in the P2 gateware, Tx1_IQ_fifo.vhd:106
//     [@8e86a61]) is never filled past the 15 ms target;
//   - after the stall the frame rate returns to 800 a second (192 kHz / 240).
#include <QtTest/QtTest>

#include "core/P2RadioConnection.h"

#include <vector>

using namespace NereusSDR;

namespace {

constexpr qint64 kMs = 1'000'000;
constexpr int kBlock = 256;                 // pairs per pump block at 192 kHz
constexpr int kTargetLead = 2880;           // TxIqPacer::kTargetLeadSamples

qint64 micPacketNs(qint64 k) { return k * 4'000'000 / 3; }  // 1.333 ms each

// The value a sample's I carries on the wire: 1-based index, so silence (0)
// is never mistaken for a sample.
float sampleValue(qint64 index) { return float(index + 2) / 8388608.0f; }

struct Run {
    P2RadioConnection conn;
    P2RadioConnection::TxIqCapture cap;
    qint64 produced{0};        // real samples pushed
    qint64 nextMic{0};         // next mic packet index
    qint64 maxLead{0};
    QList<int> passFrames;     // frames per send pass, in order
    QList<qint64> passAtNs;

    void pushBlock()
    {
        std::vector<float> iq(kBlock * 2, 0.0f);
        for (int i = 0; i < kBlock; ++i) {
            iq[2 * i] = sampleValue(produced + i);
        }
        conn.sendTxIq(iq.data(), kBlock);
        produced += kBlock;
    }

    void pass(qint64 nowNs)
    {
        const int n = conn.serviceTxIqSendForTest(nowNs, &cap);
        passFrames.append(n);
        passAtNs.append(nowNs);
        maxLead = std::max<qint64>(maxLead, qint64(conn.txIqRadioLeadForTest()));
    }

    // Simulates [fromNs, toNs). Producer blocks come with the mic packets
    // unless the producer is stalled; a stalled producer's packets arrive
    // as one burst when the stall ends. The send thread passes once a
    // millisecond unless it is stalled.
    using Windows = QList<QPair<qint64, qint64>>;
    static bool inside(const Windows& w, qint64 t)
    {
        for (const auto& p : w) {
            if (t >= p.first && t < p.second) {
                return true;
            }
        }
        return false;
    }
    void simulate(qint64 fromNs, qint64 toNs,
                  const Windows& producerStalls = {}, const Windows& senderStalls = {})
    {
        for (qint64 t = fromNs; t < toNs; t += kMs / 4) {
            while (!inside(producerStalls, t) && micPacketNs(nextMic) <= t) {
                pushBlock();
                ++nextMic;
            }
            if (!inside(senderStalls, t) && t % kMs == 0) {
                pass(t);
            }
        }
    }

    // Decodes the wire: the real samples in order, and the silent samples.
    void decode(QList<qint64>* real, qint64* silent) const
    {
        *silent = 0;
        for (const QByteArray& f : cap.frames) {
            for (int s = 0; s < 240; ++s) {
                const int o = 4 + s * 6;
                const qint32 v = (qint32(quint8(f[o])) << 24 | qint32(quint8(f[o + 1])) << 16
                                  | qint32(quint8(f[o + 2])) << 8) >> 8;
                if (v == 0) {
                    ++*silent;
                } else {
                    real->append(qint64(v) - 1);
                }
            }
        }
    }

    // Frames sent in [fromNs, toNs).
    int framesIn(qint64 fromNs, qint64 toNs) const
    {
        int n = 0;
        for (int i = 0; i < passAtNs.size(); ++i) {
            if (passAtNs[i] >= fromNs && passAtNs[i] < toNs) {
                n += passFrames[i];
            }
        }
        return n;
    }
};

// Every produced sample is on the wire exactly once, in order, apart from
// what is still queued in the ring.
void verifyNothingLost(const Run& r)
{
    QList<qint64> real;
    qint64 silent = 0;
    r.decode(&real, &silent);
    const qint64 queued = r.conn.txIqRingCountForTest() / 2;
    QCOMPARE(qint64(real.size()) + queued, r.produced);
    for (qint64 i = 0; i < real.size(); ++i) {
        if (real[i] != i) {
            QFAIL(qPrintable(QStringLiteral("sample %1 on the wire where %2 was due").arg(real[i]).arg(i)));
        }
    }
    QCOMPARE(r.conn.txSendStats().overflowSamples, quint64(0));
}

} // namespace

class TestP2TxIqSend : public QObject {
    Q_OBJECT

private slots:
    // Red on 68410c40: the ring held 16384 floats (42.7 ms), so a 200 ms
    // stall's worth of blocks on top of the 20 ms cushion overflowed.
    void ring_holdsTwoHundredMsStallWithCushion()
    {
        P2RadioConnection conn;
        conn.setMox(true);  // arms the 20 ms cushion
        std::vector<float> iq(kBlock * 2, 0.1f);
        const int blocks = (192000 / 5 + kBlock - 1) / kBlock;  // 200 ms
        for (int b = 0; b < blocks; ++b) {
            conn.sendTxIq(iq.data(), kBlock);
        }
        QCOMPARE(conn.txIqRingCountForTest() / 2, 3840 + blocks * kBlock);
        QCOMPARE(conn.txSendStats().overflowSamples, quint64(0));
    }

    void steadyRun_matchesRadioRate()
    {
        Run r;
        r.conn.setMox(true);
        r.simulate(0, 10'000 * kMs);
        verifyNothingLost(r);
        // 192000 / 240 = 800 frames a second, every second after the first.
        for (int sec = 1; sec < 10; ++sec) {
            const int n = r.framesIn(sec * 1000 * kMs, (sec + 1) * 1000 * kMs);
            QVERIFY2(n >= 799 && n <= 801, qPrintable(QString::number(n)));
        }
        QVERIFY(r.maxLead <= kTargetLead);
        const auto st = r.conn.txSendStats();
        QCOMPARE(st.zeroPaddedSamples, quint64(0));
        QCOMPARE(st.radioRanDry, quint64(0));
        QCOMPARE(st.lateWakes, quint64(0));
        QVERIFY(st.framesSent >= 7990);
    }

    void connectionThreadStall_dropsNothing_data()
    {
        QTest::addColumn<int>("stallMs");
        QTest::newRow("50 ms") << 50;
        QTest::newRow("100 ms") << 100;
        QTest::newRow("200 ms") << 200;
    }
    // The radio's mic packets wait behind the stall, so the pump stops and
    // then bursts the backlog. The send thread keeps the radio fed (silence
    // once the cushion is gone) and sends every sample after.
    void connectionThreadStall_dropsNothing()
    {
        QFETCH(int, stallMs);
        Run r;
        r.conn.setMox(true);
        const qint64 stallAt = 2000 * kMs;
        r.simulate(0, 6000 * kMs, {{stallAt, stallAt + stallMs * kMs}});
        verifyNothingLost(r);
        QVERIFY(r.maxLead <= kTargetLead);
        // Cadence back to steady within a second of the stall.
        for (int sec = 4; sec < 6; ++sec) {
            const int n = r.framesIn(sec * 1000 * kMs, (sec + 1) * 1000 * kMs);
            QVERIFY2(n >= 799 && n <= 801, qPrintable(QString::number(n)));
        }
    }

    void sendThreadStall_dropsNothing_data()
    {
        QTest::addColumn<int>("stallMs");
        QTest::newRow("50 ms") << 50;
        QTest::newRow("100 ms") << 100;
        QTest::newRow("200 ms") << 200;
    }
    // The send thread misses its passes; the producer keeps going. The ring
    // holds the backlog, the first pass after refills the radio to its
    // target (a capped burst, never above it), and nothing is dropped.
    void sendThreadStall_dropsNothing()
    {
        QFETCH(int, stallMs);
        Run r;
        r.conn.setMox(true);
        const qint64 stallAt = 2000 * kMs;
        r.simulate(0, 6000 * kMs, {}, {{stallAt, stallAt + stallMs * kMs}});
        verifyNothingLost(r);
        QVERIFY(r.maxLead <= kTargetLead);
        // The first pass after the stall: at most the target lead's frames.
        int firstAfter = -1;
        for (int i = 0; i < r.passAtNs.size(); ++i) {
            if (r.passAtNs[i] >= stallAt + stallMs * kMs) {
                firstAfter = r.passFrames[i];
                break;
            }
        }
        QVERIFY(firstAfter > 4);
        QVERIFY(firstAfter <= kTargetLead / 240);
        const auto st = r.conn.txSendStats();
        QCOMPARE(st.zeroPaddedSamples, quint64(0));
        QCOMPARE(st.lateWakes, quint64(1));
        QCOMPARE(st.catchUpBursts, quint64(1));
        QCOMPARE(st.radioRanDry, quint64(1));
        for (int sec = 4; sec < 6; ++sec) {
            const int n = r.framesIn(sec * 1000 * kMs, (sec + 1) * 1000 * kMs);
            QVERIFY2(n >= 799 && n <= 801, qPrintable(QString::number(n)));
        }
    }

    // The old design's failure: both halves on one stalled thread (the
    // send thread starved as well as the connection thread).
    void bothStall_dropsNothing_data()
    {
        QTest::addColumn<int>("stallMs");
        QTest::newRow("50 ms") << 50;
        QTest::newRow("100 ms") << 100;
        QTest::newRow("200 ms") << 200;
    }
    void bothStall_dropsNothing()
    {
        QFETCH(int, stallMs);
        Run r;
        r.conn.setMox(true);
        const qint64 stallAt = 2000 * kMs;
        const Run::Windows stall{{stallAt, stallAt + stallMs * kMs}};
        r.simulate(0, 6000 * kMs, stall, stall);
        verifyNothingLost(r);
        QVERIFY(r.maxLead <= kTargetLead);
        for (int sec = 4; sec < 6; ++sec) {
            const int n = r.framesIn(sec * 1000 * kMs, (sec + 1) * 1000 * kMs);
            QVERIFY2(n >= 799 && n <= 801, qPrintable(QString::number(n)));
        }
    }

    // Past the cap. Each stall of the send thread longer than the radio's
    // 15 ms lead leaves that much more queued (the air time went by with
    // the radio empty), so repeated long stalls fill the 341 ms ring. Then
    // the ring refuses the newest samples: every one counted, none silent.
    void pastTheCap_lossIsCounted()
    {
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("P2 transmit I/Q buffer full")));
        Run r;
        r.conn.setMox(true);
        Run::Windows stalls;
        for (int k = 0; k < 6; ++k) {
            const qint64 from = 1000 * kMs + k * 500 * kMs;
            stalls.append({from, from + 200 * kMs});
        }
        r.simulate(0, 6000 * kMs, stalls, stalls);
        QList<qint64> real;
        qint64 silent = 0;
        r.decode(&real, &silent);
        const auto st = r.conn.txSendStats();
        QVERIFY(st.overflowSamples > 0);
        QCOMPARE(qint64(real.size()) + r.conn.txIqRingCountForTest() / 2
                     + qint64(st.overflowSamples),
                 r.produced);
        QVERIFY(st.maxRingMs >= 330);
        QVERIFY(r.maxLead <= kTargetLead);
    }

    // Unkeyed, the radio still gets its port-1029 stream: silence at the
    // same 800 frames a second, never counted as a hole.
    void unkeyed_sendsSilenceAtRadioRate()
    {
        Run r;
        for (qint64 t = 0; t < 3000 * kMs; t += kMs) {
            r.pass(t);
        }
        const int n = r.framesIn(1000 * kMs, 2000 * kMs);
        QVERIFY2(n >= 799 && n <= 801, qPrintable(QString::number(n)));
        QCOMPARE(r.conn.txSendStats().zeroPaddedSamples, quint64(0));
        QVERIFY(r.maxLead <= kTargetLead);
    }

    // A socket that refuses a frame (full send buffer): the frame is kept
    // and sent next pass with its sequence number, nothing skipped.
    void refusedFrame_isRetriedInOrder()
    {
        Run r;
        r.conn.setMox(true);
        r.simulate(0, 100 * kMs);
        r.cap.refuse = 3;
        r.simulate(100 * kMs, 1000 * kMs);
        verifyNothingLost(r);
        quint32 expect = 0;
        for (const QByteArray& f : r.cap.frames) {
            const quint32 seq = quint32(quint8(f[0])) << 24 | quint32(quint8(f[1])) << 16
                              | quint32(quint8(f[2])) << 8 | quint32(quint8(f[3]));
            QCOMPARE(seq, expect++);
        }
        QCOMPARE(r.conn.txSendStats().sendErrors, quint64(3));
    }
};

QTEST_MAIN(TestP2TxIqSend)
#include "tst_p2_tx_iq_send.moc"
