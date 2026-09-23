// no-port-check: NereusSDR-original linked-WDSP integration test. It drives a
// real RX channel to prove channel teardown and rebuilds wait for a slow DSP
// worker to leave its loop before freeing its memory (R-R3-39).
#include <QtTest>

#include <array>
#include <chrono>
#include <thread>

#include "core/wdsp_api.h"

namespace {

using Clock = std::chrono::steady_clock;

// A WDSP channel id no application object (or other test) uses.
constexpr int kChannel = 21;
constexpr int kSampleRate = 48000;
constexpr int kInSize = 1024;
constexpr int kDspSize = 4096;

// Simulated per-block DSP overload, busy-waited inside the worker's csDSP.
// Longer than the 25 ms teardown used to allow.
constexpr int kBlockDelayUs = 60000;
// Input buffers queued up front: six worker blocks at 48 kHz (four input
// buffers per 4096-sample block), so the worker always has its next block.
constexpr int kQueuedInputBuffers = 24;
// After the worker lets a control call through between blocks, it starts
// its next block within the 1 ms burst grace. Waiting this long puts the
// teardown call well inside that 60 ms block.
constexpr std::chrono::milliseconds kIntoBlock{5};

// A block longer than teardown's 2000 ms log interval: teardown must still
// wait it out (and log once while it does).
constexpr int kLongBlockDelayUs = 2500000;
constexpr double kLongBlockMinCloseMs = 2000.0;

// An idle worker must exit at once: well under the old fixed 25 ms sleep.
constexpr double kIdleCloseLimitMs = 20.0;
constexpr std::chrono::milliseconds kIdleSettle{100};

double msSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

void openChannel()
{
    OpenChannel(kChannel, kInSize, kDspSize, kSampleRate, kSampleRate, kSampleRate,
                0,       // type: RX
                1,       // state: on
                0.010, 0.025, 0.000, 0.010,
                0);      // bfo off: fexchange2 never waits for output
}

// Queues a backlog of worker blocks, then returns just after the worker has
// started one of them with the given test delay in force.
void putTheWorkerInsideABlock(int blockDelayUs = kBlockDelayUs)
{
    WDSPSetTestBlockDelayUs(kChannel, blockDelayUs);
    std::array<float, kInSize> inI{}, inQ{}, outI{}, outQ{};
    for (int i = 0; i < kInSize; ++i) {
        inI[i] = 0.01f;
    }
    for (int buffer = 0; buffer < kQueuedInputBuffers; ++buffer) {
        int error = 0;
        fexchange2(kChannel, inI.data(), inQ.data(), outI.data(), outQ.data(), &error);
    }
    // Takes csDSP, so it returns between two of the worker's blocks.
    double top = 0.0;
    GetRXAAGCTop(kChannel, &top);
    std::this_thread::sleep_for(kIntoBlock);
}

} // namespace

class TestWdspChannelShutdown : public QObject {
    Q_OBJECT

private slots:
    void cleanup()
    {
        WDSPSetTestBlockDelayUs(kChannel, 0);
    }

    void closeChannelWaitsForABusyWorker()
    {
        openChannel();
        putTheWorkerInsideABlock();

        const int exitsBefore = WDSPGetTestWorkerExitCount(kChannel);
        const auto start = Clock::now();
        CloseChannel(kChannel);
        const double closeMs = msSince(start);
        const int exitsAtReturn = WDSPGetTestWorkerExitCount(kChannel);
        qInfo("CloseChannel with a busy worker took %.2f ms; worker exits %d -> %d",
              closeMs, exitsBefore, exitsAtReturn);
        QVERIFY2(exitsAtReturn == exitsBefore + 1,
                 "CloseChannel returned while the worker was still inside its block");
    }

    void inputSamplerateRebuildWaitsForABusyWorker()
    {
        openChannel();
        putTheWorkerInsideABlock();

        const int exitsBefore = WDSPGetTestWorkerExitCount(kChannel);
        const auto start = Clock::now();
        SetInputSamplerate(kChannel, 2 * kSampleRate);
        const double rebuildMs = msSince(start);
        const int exitsAtReturn = WDSPGetTestWorkerExitCount(kChannel);
        qInfo("SetInputSamplerate with a busy worker took %.2f ms; worker exits %d -> %d",
              rebuildMs, exitsBefore, exitsAtReturn);
        QVERIFY2(exitsAtReturn == exitsBefore + 1,
                 "SetInputSamplerate rebuilt while the old worker was still inside its block");

        WDSPSetTestBlockDelayUs(kChannel, 0);
        CloseChannel(kChannel);
        QCOMPARE(WDSPGetTestWorkerExitCount(kChannel), exitsBefore + 2);
    }

    void inputBuffsizeRebuildWaitsForABusyWorker()
    {
        openChannel();
        putTheWorkerInsideABlock();

        const int exitsBefore = WDSPGetTestWorkerExitCount(kChannel);
        const auto start = Clock::now();
        SetInputBuffsize(kChannel, 2 * kInSize);
        const double rebuildMs = msSince(start);
        const int exitsAtReturn = WDSPGetTestWorkerExitCount(kChannel);
        qInfo("SetInputBuffsize with a busy worker took %.2f ms; worker exits %d -> %d",
              rebuildMs, exitsBefore, exitsAtReturn);
        QVERIFY2(exitsAtReturn == exitsBefore + 1,
                 "SetInputBuffsize rebuilt while the old worker was still inside its block");

        WDSPSetTestBlockDelayUs(kChannel, 0);
        CloseChannel(kChannel);
        QCOMPARE(WDSPGetTestWorkerExitCount(kChannel), exitsBefore + 2);
    }

    void closeChannelWaitsOutABlockLongerThanTheLogInterval()
    {
        openChannel();
        putTheWorkerInsideABlock(kLongBlockDelayUs);

        const int exitsBefore = WDSPGetTestWorkerExitCount(kChannel);
        const auto start = Clock::now();
        CloseChannel(kChannel);
        const double closeMs = msSince(start);
        const int exitsAtReturn = WDSPGetTestWorkerExitCount(kChannel);
        qInfo("CloseChannel with a %d ms block took %.2f ms; worker exits %d -> %d",
              kLongBlockDelayUs / 1000, closeMs, exitsBefore, exitsAtReturn);
        QVERIFY2(exitsAtReturn == exitsBefore + 1,
                 "CloseChannel returned while the worker was still inside its block");
        QVERIFY2(closeMs >= kLongBlockMinCloseMs, "CloseChannel did not wait out the long block");
    }

    void idleWorkerExitsAtOnce()
    {
        openChannel();
        std::this_thread::sleep_for(kIdleSettle);

        const int exitsBefore = WDSPGetTestWorkerExitCount(kChannel);
        const auto start = Clock::now();
        CloseChannel(kChannel);
        const double closeMs = msSince(start);
        const int exitsAtReturn = WDSPGetTestWorkerExitCount(kChannel);
        qInfo("CloseChannel with an idle worker took %.2f ms (limit %.1f); worker exits %d -> %d",
              closeMs, kIdleCloseLimitMs, exitsBefore, exitsAtReturn);
        QCOMPARE(exitsAtReturn, exitsBefore + 1);
        QVERIFY2(closeMs <= kIdleCloseLimitMs, "closing an idle channel waited too long");
    }
};

QTEST_MAIN(TestWdspChannelShutdown)
#include "tst_wdsp_channel_shutdown.moc"
