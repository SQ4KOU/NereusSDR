// no-port-check: NereusSDR-original linked-WDSP integration test (R-R3-40,
// R-R3-37). A real receive channel with the Core's settings (192 kHz input in
// chunks of 256, 64-sample DSP buffer at 48 kHz), fed in real time the way
// RxDspWorker feeds it, sampled every 500 ms through the real
// ReceiverDspLoadSampler and NnrLoadGovernor. The reference is the WDSP
// worker's own CPU clock, captured through the thread-start hook.
//
// The frame-like case is the defect found on the Rock 5C: a stage that works
// in frames longer than one block (neural noise reduction: a 768-sample hop at
// 48 kHz is one heavy block in 12 at a 64-sample buffer) makes one long block
// in twelve. A sample that landed inside such a block used to read its time
// so far over one block period (up to several times 1.0), and the step-back
// turned noise reduction off with the worker at about 60% of a core. The load
// is now busy time over wall time between two reads.
//
// Real time: each case runs about 10 s. Opens no audio device.
#include <QtTest>

#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <thread>
#include <vector>

#include <pthread.h>
#include <time.h>
#ifdef Q_OS_MAC
#include <mach/mach.h>
#include <mach/thread_act.h>
#endif

#include "core/dsp/NnrLoadGovernor.h"
#include "core/wdsp_api.h"
#include "models/ReceiverDspLoadSampler.h"

using namespace NereusSDR;

namespace {

using Clock = std::chrono::steady_clock;

// A WDSP channel id no application object uses in this process.
constexpr int kChannel = 21;
constexpr int kSlice = 0;

// The Core's receive settings: 192 kHz input, in_size 64 * rate / 48000
// (RadioModel's bufferSizeForRate), DSP and output at 48 kHz (RadioModel's
// createRxChannel), and the default 64-sample DSP buffer
// (RxChannel's DspOptionsBufferSize default).
constexpr int kInputRate = 192000;
constexpr int kInSize = 256;
constexpr int kDspRate = 48000;
constexpr int kOpenDspSize = 4096;
constexpr int kDspSize = 64;
// The radio's packets: 238 samples each (one every 1.24 ms at 192 kHz),
// drained in whole kInSize chunks, as RxDspWorker does.
constexpr int kPacket = 238;
// A feeder more than this far behind its schedule (after a deliberately
// overloaded or stuck worker) starts again from now rather than bursting,
// as RxDspWorker drops input to keep its wait bounded.
constexpr std::chrono::milliseconds kFeederResync{100};

constexpr auto kSampleInterval = std::chrono::milliseconds(ReceiverDspLoadSampler::kSampleIntervalMs);
// About 10 s of samples per case.
constexpr int kTicks = 20;
// Time for the worker to settle after a delay changes.
constexpr std::chrono::milliseconds kSettle{1000};

// Frame-like load: 9 ms of work in every 12th block (one NNR hop).
constexpr int kFrameDelayUs = 9000;
constexpr int kFrameEvery = 12;
constexpr double kFrameMaxLoad = 0.75;          // the display governor's busy line
constexpr double kFrameCpuTolerance = 0.10;     // absolute
// Uniform load well under real time: 900 us of a 1333 us block.
constexpr int kUniformDelayUs = 900;
constexpr double kUniformCpuTolerance = 0.10;   // relative to the CPU share
// Real overload: 1400 us of a 1333 us block.
constexpr int kOverloadDelayUs = 1400;
constexpr double kOverloadMinLoad = 0.95;
// One stuck block of 1.5 s.
constexpr int kStuckDelayUs = 1500000;
constexpr double kStuckMinLoad = 0.95;

// The channel's current DSP worker, as the thread-start hook reported it.
// SetDSPBuffsize rebuilds the channel, so the worker OpenChannel started
// ends and a new one starts; the old one may report its end after the new
// one has started.
QMutex g_workerMutex;
bool g_haveWorker = false;
int g_workerStarts = 0;
pthread_t g_worker{};

// Runs on a WDSP thread as it starts, and on a channel worker as it ends.
void onWdspThreadStart(int kind, int channel)
{
    if (channel != kChannel) {
        return;
    }
    QMutexLocker lock(&g_workerMutex);
    if (kind == kWdspThreadRxMain) {
        g_worker = pthread_self();
        g_haveWorker = true;
        ++g_workerStarts;
    } else if (kind == kWdspThreadWorkerExit && g_haveWorker
               && pthread_equal(g_worker, pthread_self())) {
        g_haveWorker = false;
    }
}

bool haveWorkerAfter(int starts)
{
    QMutexLocker lock(&g_workerMutex);
    return g_haveWorker && g_workerStarts >= starts;
}

// The current worker's CPU time so far, or -1 when it cannot be read.
qint64 workerCpuNs()
{
    QMutexLocker lock(&g_workerMutex);
    if (!g_haveWorker) {
        return -1;
    }
#ifdef Q_OS_MAC
    thread_basic_info_data_t info{};
    mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
    if (thread_info(pthread_mach_thread_np(g_worker), THREAD_BASIC_INFO,
                    reinterpret_cast<thread_info_t>(&info), &count) != KERN_SUCCESS) {
        return -1;
    }
    return (qint64(info.user_time.seconds) + info.system_time.seconds) * 1'000'000'000LL
        + (qint64(info.user_time.microseconds) + info.system_time.microseconds) * 1000LL;
#else
    clockid_t clock{};
    timespec now{};
    if (pthread_getcpuclockid(g_worker, &clock) != 0 || clock_gettime(clock, &now) != 0) {
        return -1;
    }
    return qint64(now.tv_sec) * 1'000'000'000LL + now.tv_nsec;
#endif
}

qint64 monotonicMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now().time_since_epoch()).count();
}

// One reading the way RadioModel::sampleReceiverDspLoad builds it.
ReceiverDspLoadSampler::Reading readChannel()
{
    WdspChannelLoad load{};
    GetChannelDspLoad(kChannel, &load);
    ReceiverDspLoadSampler::Reading reading;
    reading.blocks = load.blocks;
    reading.busyNs = load.busyNs;
    reading.lateBlocks = load.lateBlocks;
    reading.lifetimeMaxBlockUs = load.maxBlockUs;
    reading.blockPeriodUs = load.blockPeriodUs;
    reading.currentBlockNs = load.currentBlockNs;
    reading.readNs = load.readNs;
    reading.intervalMaxBlockUs = TakeChannelDspIntervalMaxBlockUs(kChannel);
    return reading;
}

QHash<int, ReceiverDspLoadSampler::Reading> one(const ReceiverDspLoadSampler::Reading& r)
{
    QHash<int, ReceiverDspLoadSampler::Reading> readings;
    readings.insert(kSlice, r);
    return readings;
}

struct Sample {
    double atSeconds{0.0};
    double load{0.0};
    bool idle{false};
    double cpuShare{0.0};
    qint64 maxBlockUs{0};
    bool steppedBack{false};
};

struct Run {
    std::vector<Sample> samples;
    int stepBacks{0};
};

// kTicks samples, kSampleInterval apart, through the real sampler and the
// real step-back governor (a receiver running Premium NNR). The first
// reading only seeds the sampler and sets the governor's time base.
Run sampleFor(int ticks)
{
    ReceiverDspLoadSampler sampler;
    NnrLoadGovernor governor;
    NnrLoadGovernor::Receiver receiver;
    receiver.nnrSelected = true;
    receiver.savedModelSlot = 1;

    Run run;
    const auto start = Clock::now();
    auto next = start;
    sampler.update(one(readChannel()));
    qint64 previousCpu = workerCpuNs();
    auto previousWall = Clock::now();
    governor.observe(kSlice, monotonicMs(), receiver);

    for (int tick = 0; tick < ticks; ++tick) {
        next += kSampleInterval;
        std::this_thread::sleep_until(next);
        sampler.update(one(readChannel()));
        const qint64 cpu = workerCpuNs();
        const auto wall = Clock::now();
        const auto snapshot = sampler.snapshot(kSlice);

        Sample sample;
        sample.atSeconds = std::chrono::duration<double>(wall - start).count();
        sample.cpuShare = double(cpu - previousCpu)
            / double(std::chrono::duration_cast<std::chrono::nanoseconds>(wall - previousWall).count());
        if (snapshot) {
            sample.load = snapshot->load;
            sample.idle = snapshot->idle;
            sample.maxBlockUs = snapshot->maxBlockUs;
            if (!snapshot->idle) {
                receiver.load = snapshot->load;
            } else {
                receiver.load.reset();
            }
        } else {
            receiver.load.reset();
        }
        const auto stepped = governor.observe(kSlice, monotonicMs(), receiver);
        if (stepped) {
            sample.steppedBack = true;
            ++run.stepBacks;
            receiver.limit = *stepped;
        }
        run.samples.push_back(sample);
        previousCpu = cpu;
        previousWall = wall;
    }
    return run;
}

void logRun(const char* name, const Run& run)
{
    for (const Sample& s : run.samples) {
        qInfo("%s t=%.1fs load=%.3f cpu=%.3f max_block_us=%lld%s%s", name, s.atSeconds,
              s.load, s.cpuShare, static_cast<long long>(s.maxBlockUs),
              s.idle ? " idle" : "", s.steppedBack ? " STEP-BACK" : "");
    }
}

} // namespace

class TestReceiverDspLoadFrames : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        WDSPSetThreadStartHook(onWdspThreadStart);
        // WdspEngine::createRxChannel's OpenChannel and seeding.
        OpenChannel(kChannel, kInSize, kOpenDspSize, kInputRate, kDspRate, kDspRate,
                    0,       // type: RX
                    0,       // state: off until configured
                    0.010, 0.025, 0.000, 0.010,
                    1);      // bfo: fexchange2 waits for output, as in the Core
        SetRXAMode(kChannel, 1);  // USB
        SetRXABandpassFreqs(kChannel, 150.0, 2850.0);
        RXANBPSetFreqs(kChannel, 150.0, 2850.0);
        SetRXAAGCMode(kChannel, 3);  // Med
        SetRXAAGCTop(kChannel, 80.0);
        SetRXAPanelBinaural(kChannel, 0);
        SetDSPBuffsize(kChannel, kDspSize);
        SetChannelState(kChannel, 1, 0);

        // One worker from OpenChannel, a second from SetDSPBuffsize's rebuild.
        const auto deadline = Clock::now() + std::chrono::seconds(5);
        while (!haveWorkerAfter(2) && Clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        QVERIFY2(haveWorkerAfter(2), "the rebuilt channel's DSP worker never reported its start");
        QVERIFY2(workerCpuNs() >= 0, "the worker's CPU clock cannot be read");
        m_feeder = std::thread([this] { feed(); });
        std::this_thread::sleep_for(kSettle);
    }

    void cleanupTestCase()
    {
        clearDelays();
        m_stop.store(true);
        if (m_feeder.joinable()) {
            m_feeder.join();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CloseChannel(kChannel);
        WDSPSetThreadStartHook(nullptr);
    }

    void cleanup()
    {
        clearDelays();
        std::this_thread::sleep_for(kSettle);
    }

    // R-R3-40 fix wave: when the worker keeps busyNs and the block in
    // progress changing through every attempt, GetChannelDspLoad says so
    // (1) instead of handing back a pair that may be torn, and reads
    // normally again once the pair settles.
    void aReadThatNeverFindsThePairAtRestIsFlagged()
    {
        WdspChannelLoad load{};
        QCOMPARE(GetChannelDspLoad(kChannel, &load), 0);
        // The hold is steady while the worker keeps running its blocks:
        // every read while held is flagged, not only most of them.
        WDSPSetTestHoldLoadPair(kChannel, 1);
        const auto until = Clock::now() + std::chrono::milliseconds(500);
        int reads = 0;
        int unflagged = 0;
        int held = 1;
        while (Clock::now() < until) {
            const int result = GetChannelDspLoad(kChannel, &load);
            ++reads;
            if (result != 1) {
                ++unflagged;
                held = result;
            }
        }
        WDSPSetTestHoldLoadPair(kChannel, 0);
        QVERIFY2(unflagged == 0, qPrintable(QStringLiteral("%1 of %2 reads not flagged")
                                                .arg(unflagged).arg(reads)));
        QCOMPARE(held, 1);
        QVERIFY(load.readNs > 0);
        QCOMPARE(GetChannelDspLoad(kChannel, &load), 0);
    }

    // The defect: one 9 ms block in 12 is about 60% of a core, not overload.
    void aFrameLikeLoadReadsItsCpuShare()
    {
        WDSPSetTestPeriodicDelayUs(kChannel, kFrameDelayUs, kFrameEvery);
        std::this_thread::sleep_for(kSettle);
        const Run run = sampleFor(kTicks);
        logRun("frame-like", run);
        for (const Sample& s : run.samples) {
            QVERIFY2(!s.idle, qPrintable(QStringLiteral("idle at %1 s").arg(s.atSeconds)));
            QVERIFY2(s.load < kFrameMaxLoad,
                     qPrintable(QStringLiteral("load %1 at %2 s (worker CPU %3)")
                                    .arg(s.load).arg(s.atSeconds).arg(s.cpuShare)));
            QVERIFY2(std::abs(s.load - s.cpuShare) <= kFrameCpuTolerance,
                     qPrintable(QStringLiteral("load %1 vs worker CPU %2 at %3 s")
                                    .arg(s.load).arg(s.cpuShare).arg(s.atSeconds)));
            QVERIFY2(!s.steppedBack,
                     qPrintable(QStringLiteral("the step-back tripped at %1 s").arg(s.atSeconds)));
        }
        QCOMPARE(run.stepBacks, 0);
    }

    // Uniform loads read as before: close to the worker's CPU share.
    void aUniformLoadReadsItsCpuShare()
    {
        WDSPSetTestProcessDelayUs(kChannel, kUniformDelayUs);
        std::this_thread::sleep_for(kSettle);
        const Run run = sampleFor(kTicks);
        logRun("uniform-900us", run);
        for (const Sample& s : run.samples) {
            QVERIFY2(!s.idle, qPrintable(QStringLiteral("idle at %1 s").arg(s.atSeconds)));
            QVERIFY2(std::abs(s.load - s.cpuShare) <= kUniformCpuTolerance * s.cpuShare,
                     qPrintable(QStringLiteral("load %1 vs worker CPU %2 at %3 s")
                                    .arg(s.load).arg(s.cpuShare).arg(s.atSeconds)));
        }
        QCOMPARE(run.stepBacks, 0);
    }

    // A worker that really cannot keep up still reads as overloaded, and
    // the step-back still trips.
    void aRealOverloadStillStepsBack()
    {
        WDSPSetTestProcessDelayUs(kChannel, kOverloadDelayUs);
        std::this_thread::sleep_for(kSettle);
        const Run run = sampleFor(kTicks);
        logRun("overload-1400us", run);
        for (const Sample& s : run.samples) {
            QVERIFY2(!s.idle, qPrintable(QStringLiteral("idle at %1 s").arg(s.atSeconds)));
            QVERIFY2(s.load >= kOverloadMinLoad,
                     qPrintable(QStringLiteral("load %1 at %2 s (worker CPU %3)")
                                    .arg(s.load).arg(s.atSeconds).arg(s.cpuShare)));
        }
        QVERIFY2(run.stepBacks >= 1, "a real overload must step back");
    }

    // A worker stuck inside one long block reads as fully busy.
    void aStuckBlockReadsAsFullyBusy()
    {
        WDSPSetTestProcessDelayUs(kChannel, kStuckDelayUs);
        // Wait for the long block to start, then let no other block be long.
        const auto deadline = Clock::now() + std::chrono::seconds(2);
        bool inside = false;
        while (Clock::now() < deadline) {
            WdspChannelLoad load{};
            GetChannelDspLoad(kChannel, &load);
            if (load.currentBlockNs > 20'000'000LL) {
                inside = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        WDSPSetTestProcessDelayUs(kChannel, 0);
        QVERIFY2(inside, "the long block never started");
        // Two full intervals inside the block (it has about 1.47 s left).
        const Run run = sampleFor(2);
        logRun("stuck-1.5s", run);
        for (const Sample& s : run.samples) {
            QVERIFY2(!s.idle, qPrintable(QStringLiteral("idle at %1 s").arg(s.atSeconds)));
            QVERIFY2(s.load >= kStuckMinLoad,
                     qPrintable(QStringLiteral("load %1 at %2 s").arg(s.load).arg(s.atSeconds)));
        }
    }

private:
    static void clearDelays()
    {
        WDSPSetTestPeriodicDelayUs(kChannel, 0, 0);
        WDSPSetTestProcessDelayUs(kChannel, 0);
    }

    // The radio's packets in real time, drained through fexchange2 in whole
    // kInSize chunks (RxDspWorker::processIqBatch).
    void feed()
    {
        std::vector<float> accI(kInSize + kPacket), accQ(kInSize + kPacket);
        std::array<float, kInSize> inI{}, inQ{}, outI{}, outQ{};
        int have = 0;
        double phase = 0.0;
        const double step = 2.0 * std::numbers::pi * 1200.0 / kInputRate;
        const auto packetPeriod = std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(double(kPacket) / kInputRate));
        auto next = Clock::now();
        while (!m_stop.load()) {
            if (Clock::now() - next > kFeederResync) {
                next = Clock::now();
            }
            std::this_thread::sleep_until(next);
            next += packetPeriod;
            for (int i = 0; i < kPacket; ++i) {
                accI[have + i] = static_cast<float>(1e-3 * std::cos(phase));
                accQ[have + i] = static_cast<float>(1e-3 * std::sin(phase));
                phase = std::fmod(phase + step, 2.0 * std::numbers::pi);
            }
            have += kPacket;
            while (have >= kInSize) {
                std::copy_n(accI.begin(), kInSize, inI.begin());
                std::copy_n(accQ.begin(), kInSize, inQ.begin());
                std::copy(accI.begin() + kInSize, accI.begin() + have, accI.begin());
                std::copy(accQ.begin() + kInSize, accQ.begin() + have, accQ.begin());
                have -= kInSize;
                int error = 0;
                fexchange2(kChannel, inI.data(), inQ.data(), outI.data(), outQ.data(), &error);
            }
        }
    }

    std::thread m_feeder;
    std::atomic<bool> m_stop{false};
};

QTEST_GUILESS_MAIN(TestReceiverDspLoadFrames)
#include "tst_receiver_dsp_load_frames.moc"
