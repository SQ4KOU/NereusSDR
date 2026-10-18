// no-port-check: NereusSDR-original test; no upstream logic.
// =================================================================
// tests/tst_p2_tx_iq_sender_priority.cpp  (NereusSDR)
// =================================================================
//
// The Protocol 2 transmit I/Q send thread is raised without the Mac's
// audio workgroup lookup (2026-10-15). That lookup is a Core Audio query;
// the first in a process starts the audio server connection, and under
// load it took 0.25 to 1.9 s. Made on the send thread, it held up the
// first frames and stopTxIqSender's join waited for it.
//
// The test makes the lookup slow on purpose: a hook at the start of it
// (setAudioWorkgroupLookupHookForTest) sleeps 3.5 s, past the 3 s teardown
// budget. The sender must still put its first frame on the wire promptly,
// and stopping it must take well under that budget. The bounds are wide
// (a second each) so a loaded machine does not fail them; a sender that
// made the lookup misses both by seconds. Mac only: the workgroup is the
// Mac's.
//
// Modification history (NereusSDR):
//   2026-10-15 - New. J.J. Boyd (KG4VCF), with AI-assisted implementation
//                via Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>

#include <QElapsedTimer>
#include <QHostAddress>
#include <QLoggingCategory>
#include <QUdpSocket>

#include <atomic>
#include <chrono>
#include <thread>

#include "core/P2RadioConnection.h"
#include "core/audio/RealtimeAudioPriority.h"

using namespace NereusSDR;

namespace {

// Longer than the 3 s teardown budget, as the slowest lookups measured.
constexpr int kSlowLookupMs = 3500;
// First frame after the start, and the stop's join: each well inside the
// budget and far short of the slow lookup.
constexpr qint64 kFirstFrameBoundMs = 1000;
constexpr qint64 kStopBoundMs = 1000;

std::atomic<int> g_lookups{0};

void slowLookup()
{
    g_lookups.fetch_add(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(kSlowLookupMs));
}

} // namespace

class TestP2TxIqSenderPriority : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // The connection's own startup lines are not this test's subject.
        QLoggingCategory::setFilterRules(QStringLiteral(
            "nereus.connection.debug=false\nnereussdr.rt_audio.info=false"));
    }

    void cleanup()
    {
        setAudioWorkgroupLookupHookForTest(nullptr);
    }

    void slowWorkgroupLookup_holdsUpNeitherFirstFrameNorStop()
    {
#ifndef Q_OS_MAC
        QSKIP("The audio workgroup lookup is the Mac's; other systems never make it.");
#else
        QUdpSocket radio;
        QVERIFY(radio.bind(QHostAddress::LocalHost, 0));
        const quint16 port = radio.localPort();
        QVERIFY(port > 5);

        P2RadioConnection conn;
        conn.setPortBasesForTest(static_cast<quint16>(port - 5), 20000);
        conn.init();

        g_lookups.store(0);
        setAudioWorkgroupLookupHookForTest(&slowLookup);

        QElapsedTimer clock;
        clock.start();
        conn.startTxIqSenderForTest(QHostAddress::LocalHost);
        // Wait only as long as the bound, then stop at once: a sender still
        // in the lookup makes the stop wait for it too.
        qint64 firstFrameMs = -1;
        while (clock.elapsed() <= kFirstFrameBoundMs) {
            if (conn.txSendStats().framesSent > 0) {
                firstFrameMs = clock.elapsed();
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        QElapsedTimer stopClock;
        stopClock.start();
        conn.stopTxIqSenderForTest();
        const qint64 stopMs = stopClock.elapsed();

        qInfo("first frame at %lld ms (-1: none), stop took %lld ms, workgroup lookups %d",
              static_cast<long long>(firstFrameMs), static_cast<long long>(stopMs),
              g_lookups.load());
        QVERIFY2(firstFrameMs >= 0,
                 qPrintable(QStringLiteral("no frame within %1 ms").arg(kFirstFrameBoundMs)));
        QVERIFY2(stopMs <= kStopBoundMs,
                 qPrintable(QStringLiteral("stop took %1 ms").arg(stopMs)));
        QCOMPARE(g_lookups.load(), 0);
#endif
    }
};

QTEST_MAIN(TestP2TxIqSenderPriority)
#include "tst_p2_tx_iq_sender_priority.moc"
