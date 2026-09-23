// no-port-check: NereusSDR-original unit test for the per-receiver DSP load
// sampler (R-R3-40): one periodic sample feeds every reader, a worker stuck
// inside a block reads as overloaded, an idle receiver reads as idle, and the
// longest block is reported per interval. The first reading for a slice only
// seeds its baseline, because the WDSP counters are cumulative for a channel
// id over the whole process.
#include <QtTest>

#include "models/ReceiverDspLoadSampler.h"

using namespace NereusSDR;

namespace {

constexpr int kSlice = 3;
// 4096 samples at 48 kHz.
constexpr int kPeriodUs = 85333;

ReceiverDspLoadSampler::Reading reading(qint64 blocks, qint64 busyNs,
                                        qint64 lateBlocks = 0)
{
    ReceiverDspLoadSampler::Reading r;
    r.blocks = blocks;
    r.busyNs = busyNs;
    r.lateBlocks = lateBlocks;
    r.blockPeriodUs = kPeriodUs;
    return r;
}

QHash<int, ReceiverDspLoadSampler::Reading> one(const ReceiverDspLoadSampler::Reading& r)
{
    QHash<int, ReceiverDspLoadSampler::Reading> readings;
    readings.insert(kSlice, r);
    return readings;
}

} // namespace

class TestReceiverDspLoadSampler : public QObject {
    Q_OBJECT

private slots:
    void severalReadersSeeTheSameSnapshot()
    {
        ReceiverDspLoadSampler sampler;
        sampler.update(one(reading(0, 0)));
        sampler.update(one(reading(10, 10 * 40'000'000LL)));

        // Telemetry and the step-back governor both read between samples;
        // neither may start a new interval for the other.
        const auto telemetry = sampler.snapshot(kSlice);
        const auto governor = sampler.snapshot(kSlice);
        const auto again = sampler.snapshot(kSlice);
        QVERIFY(telemetry && governor && again);
        QCOMPARE(governor->load, telemetry->load);
        QCOMPARE(again->load, telemetry->load);
        QCOMPARE(again->lateBlocks, telemetry->lateBlocks);
        QVERIFY(qAbs(telemetry->load - 40000.0 / kPeriodUs) < 1e-9);
        QVERIFY(!telemetry->idle);

        // The next sample covers only the blocks since the previous one.
        sampler.update(one(reading(20, 10 * 40'000'000LL + 10 * 80'000'000LL)));
        const auto next = sampler.snapshot(kSlice);
        QVERIFY(next);
        QVERIFY(qAbs(next->load - 80000.0 / kPeriodUs) < 1e-9);
    }

    void aWorkerStuckInsideABlockReadsAsOverloaded()
    {
        ReceiverDspLoadSampler sampler;
        sampler.update(one(reading(0, 0)));
        sampler.update(one(reading(10, 10 * 20'000'000LL)));

        // No block finished in this interval; the worker has been inside its
        // current block for 450 ms, over five block periods.
        ReceiverDspLoadSampler::Reading stuck = reading(10, 10 * 20'000'000LL);
        stuck.currentBlockNs = 450'000'000LL;
        sampler.update(one(stuck));
        const auto load = sampler.snapshot(kSlice);
        QVERIFY(load);
        QVERIFY(!load->idle);
        QVERIFY2(load->load >= 450000.0 / kPeriodUs - 1e-9,
                 "a stuck block must read at least its time so far / block period");
        QVERIFY(load->load > 1.0);
        QCOMPARE(load->maxBlockUs, 450000LL);
    }

    void aLateBlockInProgressRaisesTheLoad()
    {
        ReceiverDspLoadSampler sampler;
        sampler.update(one(reading(0, 0)));

        // Two quick blocks finished, then one has run 300 ms so far.
        ReceiverDspLoadSampler::Reading r = reading(2, 2 * 10'000'000LL);
        r.currentBlockNs = 300'000'000LL;
        sampler.update(one(r));
        const auto load = sampler.snapshot(kSlice);
        QVERIFY(load);
        QVERIFY(load->load >= 300000.0 / kPeriodUs - 1e-9);

        // A block in progress that is not yet late does not change the mean.
        ReceiverDspLoadSampler::Reading normal = reading(4, 4 * 10'000'000LL);
        normal.currentBlockNs = 5'000'000LL;
        sampler.update(one(normal));
        const auto steady = sampler.snapshot(kSlice);
        QVERIFY(steady);
        QVERIFY(qAbs(steady->load - 10000.0 / kPeriodUs) < 1e-9);
    }

    void anIdleReceiverReadsAsIdle()
    {
        ReceiverDspLoadSampler sampler;
        sampler.update(one(reading(10, 10 * 20'000'000LL)));
        sampler.update(one(reading(10, 10 * 20'000'000LL)));
        const auto load = sampler.snapshot(kSlice);
        QVERIFY(load);
        QVERIFY(load->idle);
        QCOMPARE(load->load, 0.0);
        QCOMPARE(load->lateBlocks, 0LL);
    }

    void theLongestBlockIsPerInterval()
    {
        ReceiverDspLoadSampler sampler;
        sampler.update(one(reading(0, 0)));
        ReceiverDspLoadSampler::Reading first = reading(5, 5 * 100'000'000LL, 5);
        first.intervalMaxBlockUs = 120000;
        first.lifetimeMaxBlockUs = 120000;
        sampler.update(one(first));
        QCOMPARE(sampler.snapshot(kSlice)->maxBlockUs, 120000LL);
        QCOMPARE(sampler.snapshot(kSlice)->lateBlocks, 5LL);

        ReceiverDspLoadSampler::Reading second = reading(10, 5 * 100'000'000LL + 5 * 20'000'000LL, 5);
        second.intervalMaxBlockUs = 21000;
        second.lifetimeMaxBlockUs = 120000;
        sampler.update(one(second));
        const auto load = sampler.snapshot(kSlice);
        QVERIFY(load);
        QCOMPARE(load->maxBlockUs, 21000LL);
        QCOMPARE(load->lifetimeMaxBlockUs, 120000LL);
        QCOMPARE(load->lateBlocks, 0LL);
    }

    void aReceiverWithoutAReadingHasNoSnapshot()
    {
        ReceiverDspLoadSampler sampler;
        sampler.update(one(reading(0, 0)));
        sampler.update(one(reading(1, 1'000'000)));
        QVERIFY(sampler.snapshot(kSlice));
        QVERIFY(!sampler.snapshot(kSlice + 1));
        sampler.update({});
        QVERIFY(!sampler.snapshot(kSlice));
    }

    // Review finding: a WDSP channel id keeps its counters for the whole
    // process, so a new slice on a reused id starts with its predecessor's
    // history. The first reading must publish nothing rather than report
    // that history as one interval's load.
    void theFirstReadingOnlySeedsTheBaseline()
    {
        ReceiverDspLoadSampler sampler;
        // An earlier slice on this channel id ran 1000 blocks, 900 of them
        // late, at twice the block period.
        ReceiverDspLoadSampler::Reading reused =
            reading(1000, 1000 * 2LL * kPeriodUs * 1000LL, 900);
        reused.intervalMaxBlockUs = 2 * kPeriodUs;
        sampler.update(one(reused));
        QVERIFY(!sampler.snapshot(kSlice));

        // The next reading covers only this slice's own blocks.
        ReceiverDspLoadSampler::Reading next = reading(
            1004, 1000 * 2LL * kPeriodUs * 1000LL + 4 * 40'000'000LL, 900);
        next.intervalMaxBlockUs = 40000;
        sampler.update(one(next));
        const auto load = sampler.snapshot(kSlice);
        QVERIFY(load);
        QVERIFY(qAbs(load->load - 40000.0 / kPeriodUs) < 1e-9);
        QCOMPARE(load->lateBlocks, 0LL);
        QCOMPARE(load->maxBlockUs, 40000LL);
    }

    // A forgotten slice (removed from the radio) starts again from a new
    // baseline: a slice created later with the same ID never inherits the
    // removed one's snapshot.
    void aForgottenSliceStartsOver()
    {
        ReceiverDspLoadSampler sampler;
        sampler.update(one(reading(0, 0)));
        sampler.update(one(reading(10, 10 * 80'000'000LL)));
        QVERIFY(sampler.snapshot(kSlice));

        sampler.forget(kSlice);
        QVERIFY(!sampler.snapshot(kSlice));
        sampler.update(one(reading(12, 10 * 80'000'000LL + 2 * 10'000'000LL)));
        QVERIFY(!sampler.snapshot(kSlice));
        sampler.update(one(reading(14, 10 * 80'000'000LL + 4 * 10'000'000LL)));
        const auto load = sampler.snapshot(kSlice);
        QVERIFY(load);
        QVERIFY(qAbs(load->load - 10000.0 / kPeriodUs) < 1e-9);
    }

    // dsplock.c never resets a channel id's counters, so they do not go
    // backwards in production. If one ever did, the interval is unknown:
    // the sampler publishes nothing and measures from that reading on.
    void countersThatGoBackwardsStartOver()
    {
        ReceiverDspLoadSampler sampler;
        sampler.update(one(reading(0, 0)));
        sampler.update(one(reading(100, 100 * 20'000'000LL)));
        QVERIFY(sampler.snapshot(kSlice));
        sampler.update(one(reading(4, 4 * 40'000'000LL)));
        QVERIFY(!sampler.snapshot(kSlice));
        sampler.update(one(reading(8, 8 * 40'000'000LL)));
        const auto load = sampler.snapshot(kSlice);
        QVERIFY(load);
        QVERIFY(qAbs(load->load - 40000.0 / kPeriodUs) < 1e-9);
    }
};

QTEST_GUILESS_MAIN(TestReceiverDspLoadSampler)
#include "tst_receiver_dsp_load_sampler.moc"
