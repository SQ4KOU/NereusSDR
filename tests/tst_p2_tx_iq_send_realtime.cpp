// no-port-check: NereusSDR-original test; no upstream logic.
//
// R-IOS-13, R-R3-42: the Protocol 2 transmit I/Q send thread, for real: its
// own thread, the native send on the connection's socket, one pass a
// millisecond. A loopback receiver stands in for the radio's port 1029 and
// counts the frames against the wall clock, so this is a REALTIME test (a
// loaded machine can fail it; run it alone).
#include <QtTest/QtTest>

#include <QElapsedTimer>
#include <QHostAddress>
#include <QLoggingCategory>
#include <QNetworkDatagram>
#include <QUdpSocket>

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

    void sendThread_keepsRadioRateOnTheWire()
    {
        QUdpSocket radio;
        QVERIFY(radio.bind(QHostAddress::LocalHost, 0));
        const quint16 port = radio.localPort();
        QVERIFY(port > 5);

        P2RadioConnection conn;
        conn.setPortBasesForTest(static_cast<quint16>(port - 5), 20000);
        conn.init();
        conn.startTxIqSenderForTest(QHostAddress::LocalHost);

        QElapsedTimer clock;
        clock.start();
        int counted = 0;
        quint32 lastSeq = 0;
        bool haveSeq = false;
        int seqGaps = 0;
        while (clock.elapsed() < 1300) {
            radio.waitForReadyRead(20);
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
                // Count one second after a 300 ms settle.
                if (clock.elapsed() >= 300) {
                    ++counted;
                }
            }
        }
        conn.stopTxIqSenderForTest();

        // 192000 / 240 = 800 frames a second; allow the wall clock's slop.
        QVERIFY2(counted >= 720 && counted <= 880, qPrintable(QString::number(counted)));
        QCOMPARE(seqGaps, 0);
    }
};

QTEST_MAIN(TestP2TxIqSendRealtime)
#include "tst_p2_tx_iq_send_realtime.moc"
