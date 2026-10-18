// no-port-check: NereusSDR-original test; no upstream logic.
//
// R-IOS-13, R-R3-42: the Protocol 2 transmit I/Q send thread, for real: its
// own thread, the native send on the connection's socket, one pass a
// millisecond. A loopback receiver stands in for the radio's port 1029 and
// counts the frames against the wall clock, so this is a REALTIME test (a
// loaded machine can fail it; run it alone).
//
// Modification history (NereusSDR):
//   2026-10-15 - The 300 ms settle counts from the first frame sent, not
//                from the thread's start, and the time to that frame is
//                printed. J.J. Boyd (KG4VCF), with AI-assisted
//                implementation via Anthropic Claude Code.
#include <QtTest/QtTest>

#include <QElapsedTimer>
#include <QHostAddress>
#include <QLoggingCategory>
#include <QNetworkDatagram>
#include <QUdpSocket>

#include <optional>

#include "core/P2RadioConnection.h"

using namespace NereusSDR;

class TestP2TxIqSendRealtime : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // The connection's own startup lines are not this test's subject.
        QLoggingCategory::setFilterRules(QStringLiteral(
            "nereus.connection.debug=false\nnereussdr.rt_audio.info=false"));
    }

    // Load findings 4: the rate is the send thread's own count of frames
    // over a measured interval after a 300 ms settle, read from its
    // counter at each end, not the frames this reader happened to take
    // inside one wall-clock second: under load the reader, polling on the
    // wall clock too, was starved and counted 659 (720..880 expected). The
    // wire is checked separately: every frame the thread sent arrived, in
    // sequence, once the socket is drained after the stop (the socket gets
    // room for a starved reader).
    //
    // The settle counts from the first frame sent, not from the thread's
    // start. Under load the first frame once went out about 580 ms late
    // (334 of 334 sent fell inside the measured second, 334.6 a second),
    // when the Mac sender's first act was a Core Audio workgroup lookup,
    // and the pacer never makes up lost time. The rate bound is unchanged;
    // the wall-clock assumption is now "sending has started".
    void sendThread_keepsRadioRateOnTheWire()
    {
        QUdpSocket radio;
        QVERIFY(radio.bind(QHostAddress::LocalHost, 0));
        radio.setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 4 * 1024 * 1024);
        const quint16 port = radio.localPort();
        QVERIFY(port > 5);

        P2RadioConnection conn;
        conn.setPortBasesForTest(static_cast<quint16>(port - 5), 20000);
        conn.init();

        quint32 lastSeq = 0;
        bool haveSeq = false;
        int received = 0;
        int seqGaps = 0;
        const auto drain = [&]() {
            while (radio.hasPendingDatagrams()) {
                const QNetworkDatagram d = radio.receiveDatagram();
                QCOMPARE(d.data().size(), 1444);
                const QByteArray f = d.data();
                const quint32 seq = quint32(quint8(f[0])) << 24 | quint32(quint8(f[1])) << 16
                                  | quint32(quint8(f[2])) << 8 | quint32(quint8(f[3]));
                if (haveSeq && seq != lastSeq + 1) {
                    ++seqGaps;
                }
                lastSeq = seq;
                haveSeq = true;
                ++received;
            }
        };

        QElapsedTimer clock;
        clock.start();
        conn.startTxIqSenderForTest(QHostAddress::LocalHost);
        // Sending has started: the first frame is out (5 s at most).
        qint64 firstFrameMs = -1;
        while (clock.elapsed() < 5000) {
            if (conn.txSendStats().framesSent > 0) {
                firstFrameMs = clock.elapsed();
                break;
            }
            radio.waitForReadyRead(1);
            drain();
        }
        qInfo("first frame sent %lld ms after the start", static_cast<long long>(firstFrameMs));
        if (firstFrameMs < 0) {
            conn.stopTxIqSenderForTest();
            QFAIL("the send thread sent no frame within 5 s of its start");
        }
        const qint64 settleAtMs = firstFrameMs + 300;
        const qint64 endAtMs = settleAtMs + 1000;
        std::optional<quint64> sentAtSettle;
        qint64 settleNs = 0;
        while (clock.elapsed() < endAtMs) {
            radio.waitForReadyRead(20);
            drain();
            if (!sentAtSettle && clock.elapsed() >= settleAtMs) {
                sentAtSettle = conn.txSendStats().framesSent;
                settleNs = clock.nsecsElapsed();
            }
        }
        const quint64 sentAtEnd = conn.txSendStats().framesSent;
        const qint64 endNs = clock.nsecsElapsed();
        conn.stopTxIqSenderForTest();
        const quint64 sentTotal = conn.txSendStats().framesSent;
        // What was on the way when the thread stopped.
        while (radio.waitForReadyRead(200)) {
            drain();
        }
        drain();

        QVERIFY(sentAtSettle.has_value());
        // 192000 / 240 = 800 frames a second; allow the clock's slop.
        const double frames = double(sentAtEnd - *sentAtSettle);
        const double perSecond = frames * 1e9 / double(endNs - settleNs);
        qInfo("%.0f frames in %lld ms: %.1f a second; %d of %llu received", frames,
              static_cast<long long>((endNs - settleNs) / 1'000'000), perSecond, received,
              static_cast<unsigned long long>(sentTotal));
        QVERIFY2(perSecond >= 720.0 && perSecond <= 880.0,
                 qPrintable(QString::number(perSecond, 'f', 1)));
        QCOMPARE(quint64(received), sentTotal);
        QCOMPARE(seqGaps, 0);
    }
};

QTEST_MAIN(TestP2TxIqSendRealtime)
#include "tst_p2_tx_iq_send_realtime.moc"
