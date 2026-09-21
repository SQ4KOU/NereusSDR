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
};
QTEST_APPLESS_MAIN(TstAudioJitterBuffer)
#include "tst_audio_jitter_buffer.moc"
