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
        for (int i = 0; i < AudioJitterBuffer::kMaxPackets; ++i) {
            QCOMPARE(queue.insert("frame", base + i * 1920u, i * 40'000'000LL),
                     Admission::Accepted);
        }
        QCOMPARE(queue.queuedPackets(), AudioJitterBuffer::kMaxPackets);
        QCOMPARE(queue.insert("too far", base + 8 * 1920u, 0), Admission::OutsideWindow);
        for (int i = 0; i < AudioJitterBuffer::kMaxPackets; ++i) {
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
        QTest::newRow("fast producer") << 500;
        QTest::newRow("slow producer") << -500;
    }
    void arrivalClockIsPreserved()
    {
        QFETCH(int, ppm);
        AudioJitterBuffer queue;
        queue.reset(0);
        // One simulated hour with independent producer timing. Every block
        // remains held for 80 ms; timing does not converge to a local 40 ms
        // timer and concealment does not substitute for valid early/late data.
        const double packetNs = 40'000'000.0 / (1.0 + ppm / 1'000'000.0);
        int produced = 0;
        int consumed = 0;
        int peakQueued = 0;
        while (consumed < 90000) {
            const qint64 due = qRound64(consumed * packetNs) + AudioJitterBuffer::kHoldNs;
            const qint64 arrival = qRound64(produced * packetNs);
            if (produced < 90000 && arrival <= due) {
                QCOMPARE(queue.insert("audio", quint32(produced) * 1920u, arrival),
                         Admission::Accepted);
                ++produced;
                peakQueued = qMax(peakQueued, queue.queuedPackets());
            } else {
                QVERIFY(!queue.takeReady(due - 1));
                const auto frame = queue.takeReady(due);
                QVERIFY(frame && !frame->concealed());
                QCOMPARE(frame->timestamp, quint32(consumed) * 1920u);
                ++consumed;
            }
        }
        QCOMPARE(produced, 90000);
        QVERIFY(peakQueued >= 2 && peakQueued <= 3);
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
};
QTEST_APPLESS_MAIN(TstAudioJitterBuffer)
#include "tst_audio_jitter_buffer.moc"
