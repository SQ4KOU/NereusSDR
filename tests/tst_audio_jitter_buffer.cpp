// no-port-check: NereusSDR-original network ordering regression.
#include <QtTest>
#include "core/session/media/AudioJitterBuffer.h"
using namespace NereusSDR;
using Admission = AudioJitterBuffer::Admission;
class TstAudioJitterBuffer : public QObject {
    Q_OBJECT
private slots:
    void reorderLossAndLateArrival()
    {
        AudioJitterBuffer queue;
        queue.reset(100);
        QCOMPARE(queue.insert("second", 2020, 40'000'000), Admission::Accepted);
        QCOMPARE(queue.insert("first", 100, 45'000'000), Admission::Accepted);
        QCOMPARE(queue.insert("duplicate", 100, 46'000'000), Admission::Duplicate);
        QVERIFY(!queue.takeReady(124'999'999));
        const auto first = queue.takeReady(125'000'000);
        QVERIFY(first);
        QCOMPARE(first->packet, QByteArray("first"));
        const auto second = queue.takeReady(125'000'000);
        QVERIFY(second);
        QCOMPARE(second->packet, QByteArray("second"));
        QCOMPARE(queue.insert("fourth", 5860, 120'000'000), Admission::Accepted);
        const auto missing = queue.takeReady(160'000'000);
        QVERIFY(missing && missing->concealed());
        QCOMPARE(missing->timestamp, quint32(3940));
        QCOMPARE(queue.insert("late third", 3940, 161'000'000), Admission::Late);
        QVERIFY(!queue.takeReady(199'999'999));
        QCOMPARE(queue.takeReady(200'000'000)->packet, QByteArray("fourth"));
    }
    void wrapBoundAndReset()
    {
        AudioJitterBuffer queue;
        const quint32 base = 0xfffffe00u;
        queue.reset(base);
        QCOMPARE(queue.maxPackets(), 8);
        for (int i = 0; i < queue.maxPackets(); ++i) {
            QCOMPARE(queue.insert("frame", base + i * 1920u, i * 40'000'000LL),
                     Admission::Accepted);
        }
        QCOMPARE(queue.queuedPackets(), queue.maxPackets());
        QCOMPARE(queue.insert("too far", base + 8 * 1920u, 0), Admission::OutsideWindow);
        for (int i = 0; i < queue.maxPackets(); ++i) {
            const auto frame = queue.takeReady(80'000'000 + i * 40'000'000LL);
            QVERIFY(frame && !frame->concealed());
            QCOMPARE(frame->timestamp, base + i * 1920u);
        }
        queue.reset(500);
        QVERIFY(!queue.takeReady(9'000'000'000));
        QCOMPARE(queue.queuedPackets(), 0);
        QCOMPARE(queue.insert("unaligned", 501, 0), Admission::Invalid);
    }
    void arrivalClockIsPreserved_data()
    {
        QTest::addColumn<int>("ppm");
        QTest::addColumn<int>("packetFrames");
        QTest::addColumn<int>("packets");
        QTest::addColumn<int>("peakLow");
        QTest::addColumn<int>("peakHigh");
        // The 80 ms hold is two or three 40 ms Opus packets, and twenty or
        // twenty-one 4 ms lossless packets.
        QTest::newRow("fast producer") << 500 << 1920 << 90000 << 2 << 3;
        QTest::newRow("slow producer") << -500 << 1920 << 90000 << 2 << 3;
        QTest::newRow("lossless fast producer") << 500 << 192 << 150000 << 20 << 21;
        QTest::newRow("lossless slow producer") << -500 << 192 << 150000 << 20 << 21;
    }
    void arrivalClockIsPreserved()
    {
        QFETCH(int, ppm);
        QFETCH(int, packetFrames);
        QFETCH(int, packets);
        QFETCH(int, peakLow);
        QFETCH(int, peakHigh);
        AudioJitterBuffer queue(packetFrames, qint64(packetFrames) * 1'000'000'000 / 48'000);
        queue.reset(0);
        // Independent producer timing (one simulated hour of Opus, ten
        // minutes of lossless). Every packet remains held for 80 ms; timing
        // does not converge to a local packet-period timer and concealment
        // does not substitute for valid early/late data.
        const double packetNs = (packetFrames * 1'000'000'000.0 / 48'000.0)
            / (1.0 + ppm / 1'000'000.0);
        int produced = 0;
        int consumed = 0;
        int peakQueued = 0;
        while (consumed < packets) {
            const qint64 due = qRound64(consumed * packetNs) + AudioJitterBuffer::kHoldNs;
            const qint64 arrival = qRound64(produced * packetNs);
            if (produced < packets && arrival <= due) {
                QCOMPARE(queue.insert("audio", quint32(produced) * quint32(packetFrames), arrival),
                         Admission::Accepted);
                ++produced;
                peakQueued = qMax(peakQueued, queue.queuedPackets());
            } else {
                QVERIFY(!queue.takeReady(due - 1));
                const auto frame = queue.takeReady(due);
                QVERIFY(frame && !frame->concealed());
                QCOMPARE(frame->timestamp, quint32(consumed) * quint32(packetFrames));
                ++consumed;
            }
        }
        QCOMPARE(produced, packets);
        QVERIFY2(peakQueued >= peakLow && peakQueued <= peakHigh,
                 qPrintable(QString::number(peakQueued)));
        QCOMPARE(queue.queuedPackets(), 0);
    }
    void demandReleaseRequiresExactPresentPacket()
    {
        AudioJitterBuffer queue;
        queue.reset(100);

        // A future packet cannot bypass or conceal the missing head.
        QCOMPARE(queue.insert("future", 2020, 5'000'000), Admission::Accepted);
        QVERIFY(!queue.takeExpectedPresentEarly());
        QCOMPARE(queue.nextTimestamp(), quint32(100));
        QCOMPARE(queue.queuedPackets(), 1);

        QCOMPARE(queue.insert("expected", 100, 10'000'000), Admission::Accepted);
        const auto expected = queue.takeExpectedPresentEarly();
        QVERIFY(expected && !expected->concealed());
        QCOMPARE(expected->packet, QByteArray("expected"));
        QCOMPARE(expected->timestamp, quint32(100));
        QCOMPARE(queue.nextTimestamp(), quint32(2020));

        // The future packet retains its own arrival-plus-hold release time.
        QVERIFY(!queue.takeReady(84'999'999));
        const auto future = queue.takeReady(85'000'000);
        QVERIFY(future && future->packet == QByteArray("future"));
    }
    void demandReleasePreservesMissingDeadlineResetAndWrap()
    {
        AudioJitterBuffer queue;
        const quint32 base = 0xfffffe00u;
        queue.reset(base);
        QCOMPARE(queue.insert("expected", base, 10'000'000), Admission::Accepted);
        const auto expected = queue.takeExpectedPresentEarly();
        QVERIFY(expected && expected->timestamp == base);
        QCOMPARE(queue.nextTimestamp(), base + 1920u);

        // Early release does not move the producer-derived 90+40 ms loss
        // deadline forward to the demand time.
        QVERIFY(!queue.takeReady(129'999'999));
        const auto missing = queue.takeReady(130'000'000);
        QVERIFY(missing && missing->concealed());
        QCOMPARE(missing->timestamp, base + 1920u);

        queue.reset(500);
        QVERIFY(!queue.takeExpectedPresentEarly());
        QVERIFY(!queue.takeReady(9'000'000'000));
        QCOMPARE(queue.nextTimestamp(), quint32(500));
    }
    void demandReleasePreservesFutureAnchorRule()
    {
        AudioJitterBuffer normal;
        AudioJitterBuffer demand;
        normal.reset(100);
        demand.reset(100);
        for (AudioJitterBuffer* queue : {&normal, &demand}) {
            QCOMPARE(queue->insert("expected", 100, 0), Admission::Accepted);
            QCOMPARE(queue->insert("after gap", 3940, 50'000'000),
                     Admission::Accepted);
        }

        const auto normalExpected = normal.takeReady(80'000'000);
        const auto demandExpected = demand.takeExpectedPresentEarly();
        QVERIFY(normalExpected && demandExpected);
        QCOMPARE(normalExpected->packet, demandExpected->packet);

        // The queued N+2 packet anchors missing N+1 at 130-40 = 90 ms in
        // both paths. The stored empty-queue deadlines (120 ms) do not replace
        // this pre-existing reorder rule.
        QVERIFY(!normal.takeReady(89'999'999));
        QVERIFY(!demand.takeReady(89'999'999));
        const auto normalMissing = normal.takeReady(90'000'000);
        const auto demandMissing = demand.takeReady(90'000'000);
        QVERIFY(normalMissing && normalMissing->concealed());
        QVERIFY(demandMissing && demandMissing->concealed());
        QCOMPARE(normalMissing->timestamp, demandMissing->timestamp);
        const auto normalFuture = normal.takeReady(130'000'000);
        const auto demandFuture = demand.takeReady(130'000'000);
        QVERIFY(normalFuture && demandFuture);
        QCOMPARE(normalFuture->packet, QByteArray("after gap"));
        QCOMPARE(demandFuture->packet, QByteArray("after gap"));
    }
    // R-R3-23: the lossless shape. 192-frame, 4 ms packets on their own
    // grid; the window is the same 320 ms, so 80 packets rather than the
    // eight that used to stall a lossless stream after 32 ms.
    void losslessShapeKeepsTheWindowInTime()
    {
        AudioJitterBuffer queue(192, 4'000'000);
        QCOMPARE(queue.packetFrames(), 192);
        QCOMPARE(queue.packetDurationNs(), qint64(4'000'000));
        QCOMPARE(queue.maxPackets(), 80);
        QCOMPARE(AudioJitterBuffer::windowPackets(4'000'000), 80);
        QCOMPARE(AudioJitterBuffer::windowPackets(40'000'000), 8);
        queue.reset(1000);
        QCOMPARE(queue.insert("off grid", 1000 + 96, 0), Admission::Invalid);
        QCOMPARE(queue.insert("next opus block", 1000 + 1920, 0), Admission::Accepted);
        QCOMPARE(queue.insert("last in window", 1000 + 79 * 192, 0), Admission::Accepted);
        QCOMPARE(queue.insert("beyond window", 1000 + 80 * 192, 0), Admission::OutsideWindow);
        QCOMPARE(queue.insert("late", 1000 - 192, 0), Admission::Late);

        // A lost packet is one 4 ms interval, at the anchor of the next
        // queued packet (arrival + hold - distance x 4 ms).
        queue.reset(0);
        QCOMPARE(queue.insert("first", 0, 0), Admission::Accepted);
        QCOMPARE(queue.insert("third", 384, 8'000'000), Admission::Accepted);
        QVERIFY(!queue.takeReady(79'999'999));
        QCOMPARE(queue.takeReady(80'000'000)->packet, QByteArray("first"));
        QVERIFY(!queue.takeReady(83'999'999));
        const auto missing = queue.takeReady(84'000'000);
        QVERIFY(missing && missing->concealed());
        QCOMPARE(missing->timestamp, quint32(192));
        QVERIFY(!queue.takeReady(87'999'999));
        QCOMPARE(queue.takeReady(88'000'000)->packet, QByteArray("third"));
        // An empty queue conceals one packet every 4 ms after the last one.
        QVERIFY(!queue.takeReady(91'999'999));
        const auto next = queue.takeReady(92'000'000);
        QVERIFY(next && next->concealed());
        QCOMPARE(next->timestamp, quint32(576));
        QCOMPARE(queue.nextTimestamp(), quint32(768));
    }
    void invalidShapeKeepsTheOpusDefault()
    {
        const AudioJitterBuffer defaulted(0, 0);
        QCOMPARE(defaulted.packetFrames(), AudioJitterBuffer::kDefaultPacketFrames);
        QCOMPARE(defaulted.packetDurationNs(), AudioJitterBuffer::kDefaultPacketDurationNs);
        QCOMPARE(defaulted.maxPackets(), 8);
    }
};
QTEST_APPLESS_MAIN(TstAudioJitterBuffer)
#include "tst_audio_jitter_buffer.moc"
