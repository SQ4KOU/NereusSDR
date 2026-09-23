// no-port-check: NereusSDR-original linked-WDSP integration test. It drives a
// real RX channel to prove control calls get a bounded turn at the channel's
// DSP lock while the DSP worker is overloaded (R-R3-39), and that the
// worker's per-block load counters measure that overload (R-R3-40).
#include <QtTest>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <thread>

#include "core/wdsp_api.h"

namespace {

using Clock = std::chrono::steady_clock;

// A WDSP channel id no application object uses in this process.
constexpr int kChannel = 20;
constexpr int kSampleRate = 48000;
constexpr int kInSize = 1024;
constexpr int kDspSize = 4096;

// Simulated per-block DSP overload, busy-waited inside the worker's csDSP.
constexpr int kBlockDelayUs = 20000;
// Feed input ten times faster than real time with bfo off, so the worker
// always has its next block ready and never idles.
constexpr int kFeedSpeedup = 10;

constexpr int kSingleCalls = 50;
// Not a multiple of the block time, so calls land at every block phase.
constexpr std::chrono::milliseconds kSingleCallSpacing{37};
constexpr int kBurstCalls = 14;

constexpr std::chrono::milliseconds kWarmup{500};
constexpr std::chrono::milliseconds kBaselineWindow{2000};

// Acceptance bounds.
constexpr double kBlockDelayMs = kBlockDelayUs / 1000.0;
// min(block period / 4, 20 ms): the worker's per-block hold-off budget.
constexpr double kBudgetMs = std::min(1000.0 * kDspSize / kSampleRate / 4.0, 20.0);
constexpr double kSingleLimitMs = 30.0;
constexpr double kBurstLimitMs = kBlockDelayMs + kBudgetMs + 10.0;
constexpr double kMinThroughputRatio = 0.90;

// Load counters (R-R3-40): block period is dsp_size / dsp_rate.
constexpr int kBlockPeriodUs = static_cast<int>(1000000LL * kDspSize / kSampleRate);
// A delay that is a large share of the period, so the chain's own work is a
// small part of each measured block.
constexpr int kLoadDelayUs = 40000;
// A delay longer than the period: every block is late.
constexpr int kLateDelayUs = 100000;
constexpr std::chrono::milliseconds kLoadWindow{2000};
constexpr double kLoadTolerance = 0.10;
// Reading never waits for the worker's lock.
constexpr double kReadLimitMs = 5.0;

double msSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

} // namespace

class TestWdspDspTurnTaking : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        OpenChannel(kChannel, kInSize, kDspSize, kSampleRate, kSampleRate, kSampleRate,
                    0,       // type: RX
                    1,       // state: on
                    0.010, 0.025, 0.000, 0.010,
                    0);      // bfo off: fexchange2 never waits for output
        WDSPSetTestBlockDelayUs(kChannel, kBlockDelayUs);
        m_feeder = std::thread([this] { feed(); });
    }

    void cleanupTestCase()
    {
        WDSPSetTestBlockDelayUs(kChannel, 0);
        m_stop.store(true);
        if (m_feeder.joinable()) {
            m_feeder.join();
        }
        // Let the worker drain its backlog at full speed before the channel
        // is torn down; CloseChannel only waits 25 ms for the worker.
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        CloseChannel(kChannel);
    }

    void controlCallsGetABoundedTurnWhileTheWorkerIsOverloaded()
    {
        std::this_thread::sleep_for(kWarmup);

        // Baseline: worker throughput with no control calls.
        const double baselineRate = outputRateOver([] {
            std::this_thread::sleep_for(kBaselineWindow);
        });

        double worstSingleMs = 0.0;
        double burstMs = 0.0;
        const double loadedRate = outputRateOver([&] {
            for (int call = 0; call < kSingleCalls; ++call) {
                double top = 0.0;
                const auto start = Clock::now();
                GetRXAAGCTop(kChannel, &top);
                worstSingleMs = std::max(worstSingleMs, msSince(start));
                std::this_thread::sleep_for(kSingleCallSpacing);
            }
            const auto start = Clock::now();
            for (int call = 0; call < kBurstCalls; ++call) {
                double value = 0.0;
                if (call % 2 == 0) {
                    GetRXAAGCTop(kChannel, &value);
                } else {
                    GetRXAAGCThresh(kChannel, &value, 4096.0, kSampleRate);
                }
            }
            burstMs = msSince(start);
            std::this_thread::sleep_for(kSingleCallSpacing);
        });

        const double ratio = baselineRate > 0.0 ? loadedRate / baselineRate : 0.0;
        qInfo("worst single call %.2f ms (limit %.1f); %d-call burst %.2f ms (limit %.1f); "
              "worker throughput %.1f vs baseline %.1f outputs/s, ratio %.3f (min %.2f)",
              worstSingleMs, kSingleLimitMs, kBurstCalls, burstMs, kBurstLimitMs,
              loadedRate, baselineRate, ratio, kMinThroughputRatio);

        QVERIFY(baselineRate > 0.0);
        QVERIFY2(worstSingleMs <= kSingleLimitMs, "a single control call waited too long");
        QVERIFY2(burstMs <= kBurstLimitMs, "the control-call burst waited too long");
        QVERIFY2(ratio >= kMinThroughputRatio, "control calls cost the worker too much throughput");
    }

    void readerRejectsABadChannelOrOutput()
    {
        WdspChannelLoad load{};
        QCOMPARE(GetChannelDspLoad(-1, &load), -1);
        QCOMPARE(GetChannelDspLoad(1000, &load), -1);
        QCOMPARE(GetChannelDspLoad(kChannel, nullptr), -1);
    }

    void measuredLoadMatchesTheBlockDelay()
    {
        WDSPSetTestBlockDelayUs(kChannel, kLoadDelayUs);
        // Let any block started under the previous delay finish.
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        WdspChannelLoad before{};
        QCOMPARE(GetChannelDspLoad(kChannel, &before), 0);
        double worstReadMs = 0.0;
        const auto windowStart = Clock::now();
        while (Clock::now() - windowStart < kLoadWindow) {
            WdspChannelLoad sample{};
            const auto start = Clock::now();
            QCOMPARE(GetChannelDspLoad(kChannel, &sample), 0);
            worstReadMs = std::max(worstReadMs, msSince(start));
            std::this_thread::sleep_for(std::chrono::milliseconds(7));
        }
        WdspChannelLoad after{};
        QCOMPARE(GetChannelDspLoad(kChannel, &after), 0);

        const long long blocks = after.blocks - before.blocks;
        QVERIFY2(blocks > 0, "the worker completed no blocks in the window");
        const double meanBlockUs = (after.busyNs - before.busyNs) / 1000.0 / blocks;
        const double load = meanBlockUs / after.blockPeriodUs;
        const double expected = double(kLoadDelayUs) / kBlockPeriodUs;
        qInfo("%lld blocks, mean block %.1f us, period %d us: load %.3f vs expected %.3f; "
              "worst read %.3f ms",
              blocks, meanBlockUs, after.blockPeriodUs, load, expected, worstReadMs);

        QCOMPARE(after.blockPeriodUs, kBlockPeriodUs);
        QVERIFY2(std::abs(load - expected) <= kLoadTolerance * expected,
                 "measured load is not within 10% of delay / block period");
        QCOMPARE(after.lateBlocks - before.lateBlocks, 0LL);
        QVERIFY(after.maxBlockUs >= kLoadDelayUs);
        QVERIFY2(worstReadMs <= kReadLimitMs, "reading the load waited for the worker");
    }

    void blocksLongerThanThePeriodCountAsLate()
    {
        WDSPSetTestBlockDelayUs(kChannel, kLateDelayUs);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        WdspChannelLoad before{};
        QCOMPARE(GetChannelDspLoad(kChannel, &before), 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        WdspChannelLoad after{};
        QCOMPARE(GetChannelDspLoad(kChannel, &after), 0);
        WDSPSetTestBlockDelayUs(kChannel, kBlockDelayUs);

        const long long blocks = after.blocks - before.blocks;
        const long long late = after.lateBlocks - before.lateBlocks;
        qInfo("%lld blocks, %lld late, longest %lld us", blocks, late, after.maxBlockUs);
        QVERIFY(blocks > 0);
        QCOMPARE(late, blocks);
        QVERIFY(after.maxBlockUs >= kLateDelayUs);
    }

private:
    // Runs body and returns the worker's output rate (successful fexchange2
    // outputs per second) over that time. Each worker block yields a fixed
    // number of outputs, so this tracks worker blocks per second.
    template <typename Body>
    double outputRateOver(Body body)
    {
        const long long before = m_outputs.load();
        const auto start = Clock::now();
        body();
        const double seconds = msSince(start) / 1000.0;
        return static_cast<double>(m_outputs.load() - before) / seconds;
    }

    void feed()
    {
        std::array<float, kInSize> inI{}, inQ{}, outI{}, outQ{};
        const auto period = std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(double(kInSize) / kSampleRate / kFeedSpeedup));
        double phase = 0.0;
        const double step = 2.0 * std::numbers::pi * 1000.0 / kSampleRate;
        auto next = Clock::now();
        while (!m_stop.load()) {
            for (int i = 0; i < kInSize; ++i) {
                inI[i] = static_cast<float>(0.01 * std::cos(phase));
                inQ[i] = static_cast<float>(0.01 * std::sin(phase));
                phase = std::fmod(phase + step, 2.0 * std::numbers::pi);
            }
            int error = 0;
            fexchange2(kChannel, inI.data(), inQ.data(), outI.data(), outQ.data(), &error);
            if (error == 0) {
                m_outputs.fetch_add(1);
            }
            next += period;
            std::this_thread::sleep_until(next);
        }
    }

    std::thread m_feeder;
    std::atomic<bool> m_stop{false};
    std::atomic<long long> m_outputs{0};
};

QTEST_MAIN(TestWdspDspTurnTaking)
#include "tst_wdsp_dsp_turn_taking.moc"
