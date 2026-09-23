// no-port-check: NereusSDR-original linked-WDSP integration test. It drives a
// real RX channel to prove control calls get a bounded turn at the channel's
// DSP lock while the DSP worker is overloaded (R-R3-39).
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
