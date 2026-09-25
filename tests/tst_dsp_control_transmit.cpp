// no-port-check: NereusSDR-original.
// Transmit DSP off the event loop (R-R3-39, Task 32): with the transmit lane
// set, the TX channel's WDSP calls (keying, tune, two-tone, PureSignal, its
// meters) run on the lane; keying keeps its rf_delay order (MOX and relay,
// then the channel, then the RF gate); twenty key and unkey cycles make no
// WDSP call on the event loop and leave its 10 ms timer running; setters
// posted before a rebuild never run; and the unkey drain is measured with
// and without microphone blocks arriving.
//
// REALTIME: the timer-gap bound is wall-clock time while a real TX channel
// and a real RX channel run 200 ms blocks and feeder threads drive them.
#include <QtTest/QtTest>
#include "RealtimeTestLoad.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <numbers>
#include <thread>
#include <vector>

#include "core/DspControlThread.h"
#include "core/MoxController.h"
#include "core/PureSignal.h"
#include "core/RadioConnection.h"
#include "core/RxChannel.h"
#include "core/SampleRateCatalog.h"
#include "core/TwoToneController.h"
#include "core/TxAnalyzer.h"
#include "core/TxChannel.h"
#include "core/WdspEngine.h"
#include "core/WdspThreadCheck.h"
#include "core/wdsp_api.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kTxId = WdspEngine::kTxChannelId;
constexpr int kRxId = 0;
constexpr int kRateHz = 48000;
constexpr int kTxInSize = 64;
constexpr int kSlowBlockUs = 200000;
constexpr int kTimerIntervalMs = 10;
constexpr double kMaxTimerGapMs = 25.0;
constexpr int kKeyCycles = 20;
constexpr double kRfDelayMs = 30.0;   // MoxController's rf_delay default
// MoxController's rf_delay is a coarse QTimer; Qt rounds a coarse deadline
// under 50 ms to an even millisecond, so it can fire up to 1 ms early. The
// order is checked against txReady itself; the time from the key allows
// that rounding and nothing more.
constexpr double kCoarseTimerRoundingMs = 1.0;
// The first OpenChannel in a process plans its FFTs with no wisdom: about
// 30 s on a quiet machine, over 100 s under a heavy build.
constexpr int kLaneIdleTimeoutMs = 600000;

double msBetween(Clock::time_point a, Clock::time_point b)
{
    return std::chrono::duration<double, std::milli>(b - a).count();
}

// What happened, when, from any thread.
class TimedLog {
public:
    void add(const QString& what)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_entries.push_back({what, Clock::now()});
    }
    // The time of the first entry named `what` at or after `from`, or
    // nothing.
    std::optional<Clock::time_point> first(const QString& what,
                                           Clock::time_point from = {}) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& e : m_entries) {
            if (e.what == what && e.at >= from) {
                return e.at;
            }
        }
        return std::nullopt;
    }
    QStringList namesSince(Clock::time_point from) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        QStringList out;
        for (const auto& e : m_entries) {
            if (e.at >= from) {
                out << e.what;
            }
        }
        return out;
    }

private:
    struct Entry {
        QString what;
        Clock::time_point at;
    };
    mutable std::mutex m_mutex;
    std::vector<Entry> m_entries;
};

// WDSP's caller hook, recording the transmit channel's SetChannelState
// calls and (while armed) its lock entries made on the lane.
TimedLog* g_log = nullptr;
std::atomic<bool> g_recordTxLocks{false};

void recordingHook(int channel, int kind)
{
    if (g_log == nullptr || channel != kTxId) {
        return;
    }
    if (kind == kWdspCallerSetChannelState) {
        g_log->add(QStringLiteral("SetChannelState"));
    } else if (kind == kWdspCallerEnterCs && g_recordTxLocks.load()) {
        g_log->add(QStringLiteral("EnterCs"));
    }
}

class MockConnection : public RadioConnection {
    Q_OBJECT
public:
    explicit MockConnection(TimedLog* log, QObject* parent = nullptr)
        : RadioConnection(parent), m_log(log)
    {
        setState(ConnectionState::Connected);
    }

    void init() override {}
    void connectToRadio(const NereusSDR::RadioInfo&) override {}
    void disconnect() override {}
    void setReceiverFrequency(int, quint64) override {}
    void setTxFrequency(quint64) override {}
    void setActiveReceiverCount(int) override {}
    void setSampleRate(int) override {}
    void setAttenuator(int) override {}
    void setPreamp(bool) override {}
    void setTxDrive(int) override {}
    void sendTxIq(const float*, int) override { m_txBlocks.fetch_add(1); }
    void setWatchdogEnabled(bool) override {}
    void setAntennaRouting(AntennaRouting) override {}
    void setMox(bool on) override
    {
        if (m_log) {
            m_log->add(on ? QStringLiteral("MOX on") : QStringLiteral("MOX off"));
        }
    }
    void setTrxRelay(bool on) override
    {
        if (m_log) {
            m_log->add(on ? QStringLiteral("relay on") : QStringLiteral("relay off"));
        }
    }
    void setMicBoost(bool) override {}
    void setLineIn(bool) override {}
    void setMicTipRing(bool) override {}
    void setMicBias(bool) override {}
    void setLineInGain(int) override {}
    void setUserDigOut(quint8) override {}
    void setPuresignalRun(bool) override {}
    void setMicPTTDisabled(bool) override {}
    void setMicXlr(bool) override {}

    int txBlocks() const { return m_txBlocks.load(); }

private:
    TimedLog* m_log{nullptr};
    std::atomic<int> m_txBlocks{0};
};

// A thread that calls `tick` about once a millisecond until stopped.
class Feeder {
public:
    explicit Feeder(std::function<void()> tick) : m_tick(std::move(tick))
    {
        m_thread = std::thread([this] {
            while (!m_quit.load()) {
                m_tick();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }
    ~Feeder() { stop(); }
    void stop()
    {
        m_quit.store(true);
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

private:
    std::function<void()> m_tick;
    std::atomic<bool> m_quit{false};
    std::thread m_thread;
};

// The TX worker's per-block calls, as TxWorkerThread::dispatchOneBlock
// makes them: a 1 kHz tone into DEXP, then fexchange0.
std::function<void()> txBlockTick(TxChannel* tx)
{
    auto block = std::make_shared<std::vector<double>>(2 * kTxInSize, 0.0);
    auto n = std::make_shared<long>(0);
    return [tx, block, n]() {
        for (int i = 0; i < kTxInSize; ++i, ++*n) {
            (*block)[static_cast<std::size_t>(2 * i)] =
                0.1 * std::sin(2.0 * std::numbers::pi * 1000.0
                               * static_cast<double>(*n) / kRateHz);
        }
        tx->pumpDexp(block->data());
        tx->driveOneTxBlockFromInterleaved(block->data());
    };
}

std::function<void()> rxBlockTick(RxChannel* rx)
{
    auto n = std::make_shared<long>(0);
    return [rx, n]() {
        const int inSize = rx->bufferSize();
        std::vector<float> inI(static_cast<std::size_t>(inSize));
        std::vector<float> inQ(static_cast<std::size_t>(inSize), 0.0f);
        std::vector<float> outI(static_cast<std::size_t>(inSize), 0.0f);
        std::vector<float> outQ(static_cast<std::size_t>(inSize), 0.0f);
        for (int i = 0; i < inSize; ++i, ++*n) {
            inI[static_cast<std::size_t>(i)] = static_cast<float>(
                0.01 * std::cos(2.0 * std::numbers::pi * 1000.0
                                * static_cast<double>(*n) / kRateHz));
        }
        rx->processIq(inI.data(), inQ.data(), outI.data(), outQ.data(), inSize, 64);
    };
}

} // namespace

class TestDspControlTransmit : public QObject {
    Q_OBJECT

    // A local RadioModel (with both lanes) whose TX channel is real and
    // wired to MoxController as the connect path wires it, plus a real RX
    // channel, a mock connection and PureSignal.
    struct Rig {
        std::unique_ptr<RadioModel> model;
        std::unique_ptr<MockConnection> conn;
        // The TX siphon feeds this analyzer (disp 5) once the channel runs,
        // as the desktop's does; its calls run on the transmit lane.
        std::unique_ptr<TxAnalyzer> analyzer;
        WdspEngine* engine{nullptr};
        TxChannel* tx{nullptr};
        RxChannel* rx{nullptr};
        ~Rig() { tearDownRig(*this); }
    };

    static bool buildRig(Rig& rig, TimedLog* log)
    {
        rig.model = std::make_unique<RadioModel>();
        RadioModel& model = *rig.model;
        if (model.transmitLane() == nullptr || model.receiveLane() == nullptr) {
            return false;
        }
        model.setCapsForTest(/*hasAlex=*/false);
        rig.conn = std::make_unique<MockConnection>(log);
        model.injectConnectionForTest(rig.conn.get());
        model.setTuneOffSettleMsForTest(0);
        model.addSlice();
        if (SliceModel* slice = model.activeSlice()) {
            slice->setDspMode(DSPMode::USB);
            slice->setFrequency(14'200'000.0);
        }

        rig.analyzer = std::make_unique<TxAnalyzer>(TxAnalyzer::kTxDispId, nullptr,
                                                    model.transmitLane());
        rig.analyzer->setSampleRate(96000.0);
        rig.analyzer->setOutputFps(15);
        rig.analyzer->start();

        rig.engine = model.wdspEngine();
        rig.engine->m_initialized = true;   // friend access (NEREUS_BUILD_TESTS)
        rig.rx = rig.engine->createRxChannel(kRxId, bufferSizeForRate(kRateHz), 4096,
                                             kRateHz, kRateHz, kRateHz);
        rig.tx = rig.engine->createTxChannel(kTxId, kTxInSize,
                                             WdspEngine::kTxDspBufferSize, kRateHz,
                                             WdspEngine::kTxDspSampleRate, kRateHz);
        if (rig.rx == nullptr || rig.tx == nullptr) {
            return false;
        }
        if (!model.waitForReceiveLaneForTest(kLaneIdleTimeoutMs)
            || !model.waitForTransmitLaneForTest(kLaneIdleTimeoutMs)) {
            return false;
        }
        rig.rx->setActive(true);
        rig.tx->setConnection(rig.conn.get());
        model.injectTxChannelForTest(rig.tx);
        model.wireTxChannelKeyingForTest();
        return model.waitForReceiveLaneForTest(kLaneIdleTimeoutMs)
            && model.waitForTransmitLaneForTest(kLaneIdleTimeoutMs);
    }

    static void tearDownRig(Rig& rig)
    {
        WDSPSetTestBlockDelayUs(kTxId, 0);
        WDSPSetTestBlockDelayUs(kRxId, 0);
        if (!rig.model) {
            rig.conn.reset();
            return;
        }
        RadioModel& model = *rig.model;
        if (rig.tx) {
            rig.tx->setConnection(nullptr);
        }
        model.injectTxChannelForTest(nullptr);
        if (rig.engine) {
            rig.engine->shutdown();
        }
        // After the TX channel is closed: nothing feeds the analyzer now.
        rig.analyzer.reset();
        model.injectConnectionForTest(nullptr);
        rig.model.reset();
        rig.conn.reset();
    }

private slots:
    void cleanup()
    {
        WDSPSetCallerCheckHook(nullptr);
        g_log = nullptr;
        g_recordTxLocks.store(false);
        RealtimeTestLoad::printLoadAverageIfFailed();
    }

#ifdef HAVE_WDSP
    // Keying keeps the rf_delay order with 200 ms blocks on both lanes'
    // channels: MOX and the relay first, then (at least rf_delay after the
    // key) the lane switches the TX channel on, then the RF gate opens.
    void keyingKeepsTheRfDelayOrder()
    {
        TimedLog log;
        Rig rig;
        QVERIFY2(buildRig(rig, &log), "the rig did not come up");

        Feeder rxFeeder(rxBlockTick(rig.rx));
        Feeder txFeeder(txBlockTick(rig.tx));
        WDSPSetTestBlockDelayUs(kRxId, kSlowBlockUs);
        WDSPSetTestBlockDelayUs(kTxId, kSlowBlockUs);
        rig.tx->setRfGateObserverForTest([&log](bool open) {
            log.add(open ? QStringLiteral("gate open") : QStringLiteral("gate closed"));
        });
        g_log = &log;
        WDSPSetCallerCheckHook(&recordingHook);

        connect(rig.model->moxController(), &MoxController::txReady, this,
                [&log]() { log.add(QStringLiteral("txReady")); });
        const Clock::time_point keyAt = Clock::now();
        rig.model->moxController()->setMox(true);
        QTRY_VERIFY_WITH_TIMEOUT(rig.tx->isRunning(), kLaneIdleTimeoutMs);
        WDSPSetCallerCheckHook(nullptr);

        const auto moxOn = log.first(QStringLiteral("MOX on"), keyAt);
        const auto relayOn = log.first(QStringLiteral("relay on"), keyAt);
        const auto txReady = log.first(QStringLiteral("txReady"), keyAt);
        const auto channelOn = log.first(QStringLiteral("SetChannelState"), keyAt);
        const auto gateOpen = log.first(QStringLiteral("gate open"), keyAt);
        qInfo("key: MOX on +%.2f ms, relay on +%.2f ms, txReady +%.2f ms, "
              "SetChannelState(tx, 1, 0) +%.2f ms, RF gate open +%.2f ms (rf_delay %.0f ms)",
              moxOn ? msBetween(keyAt, *moxOn) : -1.0,
              relayOn ? msBetween(keyAt, *relayOn) : -1.0,
              txReady ? msBetween(keyAt, *txReady) : -1.0,
              channelOn ? msBetween(keyAt, *channelOn) : -1.0,
              gateOpen ? msBetween(keyAt, *gateOpen) : -1.0, kRfDelayMs);
        QVERIFY(moxOn && relayOn && txReady && channelOn && gateOpen);
        QVERIFY2(*moxOn <= *relayOn, "the relay came before MOX");
        QVERIFY2(*relayOn < *channelOn, "the channel came on before the relay");
        QVERIFY2(*txReady <= *channelOn, "the channel came on before rf_delay ended");
        QVERIFY2(msBetween(keyAt, *channelOn) >= kRfDelayMs - kCoarseTimerRoundingMs,
                 "the channel came on inside rf_delay");
        QVERIFY2(*channelOn <= *gateOpen, "the RF gate opened before the channel");

        // Unkey: MoxController's walk reaches txaFlushed after key_up_delay;
        // the RF gate closes then, at once.
        rig.model->moxController()->setMox(false);
        QTRY_VERIFY_WITH_TIMEOUT(!rig.tx->isRunning(), 5000);
        QVERIFY(rig.model->waitForTransmitLaneForTest(kLaneIdleTimeoutMs));
        txFeeder.stop();
        rxFeeder.stop();
    }

    // Twenty key and unkey cycles (MOX, TUNE and two-tone in turn) with
    // PureSignal's 100 ms poll running and 200 ms blocks on both lanes'
    // channels: no WDSP call on the event loop, and its 10 ms timer never
    // gaps more than 25 ms.
    void keyCyclesLeaveTheEventLoopAlone()
    {
        TimedLog log;
        Rig rig;
        QVERIFY2(buildRig(rig, &log), "the rig did not come up");
        RadioModel& model = *rig.model;
        PureSignal* ps = model.installPureSignalForTest(rig.tx);
        QVERIFY(ps);
        TwoToneController* twoTone = model.twoToneController();
        QVERIFY(twoTone);
        twoTone->setTxChannel(rig.tx);
        twoTone->setPowerOn(true);
        twoTone->setSettleDelaysMs(20, 20);
        QVERIFY(model.waitForTransmitLaneForTest(kLaneIdleTimeoutMs));

        Feeder rxFeeder(rxBlockTick(rig.rx));
        Feeder txFeeder(txBlockTick(rig.tx));
        WDSPSetTestBlockDelayUs(kRxId, kSlowBlockUs);
        WDSPSetTestBlockDelayUs(kTxId, kSlowBlockUs);
        // Let both workers get into their slow blocks.
        QTest::qWait(3 * kSlowBlockUs / 1000);

        WdspThreadCheck::install(QThread::currentThread());

        constexpr int kKeyedTicks = 12;    // 120 ms keyed
        constexpr int kSettleTicks = 15;   // 150 ms between cycles
        int ticks = 0;
        int cycle = 0;
        int phaseTick = 0;
        bool keyed = false;
        int keysTaken = 0;
        double worstGapMs = 0.0;
        Clock::time_point lastTick = Clock::now();
        QEventLoop loop;
        QTimer ticker;
        ticker.setTimerType(Qt::PreciseTimer);
        ticker.setInterval(kTimerIntervalMs);
        const auto key = [&](bool on) {
            switch (cycle % 3) {
            case 0:
                model.moxController()->setMox(on);
                break;
            case 1:
                model.setTune(on);
                break;
            default:
                twoTone->setActive(on);
                break;
            }
        };
        connect(&ticker, &QTimer::timeout, &loop, [&] {
            const Clock::time_point now = Clock::now();
            if (ticks > 0) {
                worstGapMs = std::max(worstGapMs, msBetween(lastTick, now));
            }
            lastTick = now;
            ++ticks;
            ++phaseTick;
            if (!keyed && phaseTick >= kSettleTicks) {
                if (cycle >= kKeyCycles) {
                    loop.quit();
                    return;
                }
                key(true);
                keyed = true;
                ++keysTaken;
                phaseTick = 0;
            } else if (keyed && phaseTick >= kKeyedTicks) {
                key(false);
                keyed = false;
                ++cycle;
                phaseTick = 0;
            }
        });
        QTimer deadline;
        deadline.setSingleShot(true);
        connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
        deadline.start(120000);
        ticker.start();
        loop.exec();
        ticker.stop();
        const quint64 eventLoopCalls = WdspThreadCheck::eventLoopEntries();
        WdspThreadCheck::uninstall();

        txFeeder.stop();
        rxFeeder.stop();
        WDSPSetTestBlockDelayUs(kRxId, 0);
        WDSPSetTestBlockDelayUs(kTxId, 0);
        QVERIFY(model.waitForTransmitLaneForTest(kLaneIdleTimeoutMs));

        qInfo("%d key and unkey cycles (MOX, TUNE, two-tone) with the PureSignal poll: "
              "%d ticks; worst %d ms timer gap %.2f ms (limit %.1f); TX blocks sent %d; "
              "WDSP calls on the event loop %llu",
              keysTaken, ticks, kTimerIntervalMs, worstGapMs, kMaxTimerGapMs,
              rig.conn->txBlocks(), static_cast<unsigned long long>(eventLoopCalls));
        QCOMPARE(keysTaken, kKeyCycles);
        QCOMPARE(eventLoopCalls, quint64(0));
        QVERIFY2(worstGapMs <= kMaxTimerGapMs, "the event loop's 10 ms timer gapped");
        QVERIFY2(rig.conn->txBlocks() > 0, "no TX block reached the connection");
        QVERIFY(!rig.tx->isRunning());

        twoTone->setTxChannel(nullptr);
        // The coordinator outlives the channel in this rig (the connect
        // path resets it first); detach it before the channel closes.
        ps->setTxChannel(nullptr);
    }

    // The rebuild's generation check: a setter still queued for the old
    // wrapper when the rebuild starts never runs, nor does one posted
    // through the old pointer afterwards; the new wrapper's setters do.
    // Settings apply with no microphone audio arriving (no feeder here).
    void settersQueuedBeforeARebuildAreSkipped()
    {
        DspControlThread lane(DspLane::Transmit);
        lane.start();
        WdspEngine engine;
        engine.m_initialized = true;   // friend access (NEREUS_BUILD_TESTS)
        engine.setTransmitLane(&lane);
        TxChannel* tx = engine.createTxChannel(kTxId, kTxInSize,
                                               WdspEngine::kTxDspBufferSize, kRateHz,
                                               WdspEngine::kTxDspSampleRate, kRateHz);
        QVERIFY(tx);
        QVERIFY(lane.waitIdleForTest(kLaneIdleTimeoutMs));
        QVERIFY(tx->isWdspReady());

        const auto readOnLane = [&lane](TxChannel* channel, TxChannel::Stage stage) {
            auto value = std::make_shared<std::atomic<bool>>(false);
            lane.post([channel, stage, value]() { value->store(channel->stageRunning(stage)); });
            lane.waitIdleForTest(kLaneIdleTimeoutMs);
            return value->load();
        };

        // A setter applies on the lane with no mic block arriving.
        tx->setStageRunning(TxChannel::Stage::Eqp, true);
        QVERIFY(lane.waitIdleForTest(kLaneIdleTimeoutMs));
        QVERIFY(readOnLane(tx, TxChannel::Stage::Eqp));
        QVERIFY2(tx->stageRunning(TxChannel::Stage::Eqp), "the stage cache missed the lane's value");
        tx->setStageRunning(TxChannel::Stage::Eqp, false);
        QVERIFY(lane.waitIdleForTest(kLaneIdleTimeoutMs));

        TimedLog log;
        g_log = &log;
        WDSPSetCallerCheckHook(&recordingHook);

        // Control: a setter queued behind a held lane runs once released.
        std::mutex gateMutex;
        std::condition_variable gateCv;
        bool released = false;
        const auto holdLane = [&]() {
            lane.post([&]() {
                std::unique_lock<std::mutex> lk(gateMutex);
                gateCv.wait(lk, [&] { return released; });
            });
        };
        const auto releaseLane = [&]() {
            {
                std::lock_guard<std::mutex> lk(gateMutex);
                released = true;
            }
            gateCv.notify_all();
        };
        holdLane();
        tx->setStageRunning(TxChannel::Stage::Eqp, true);
        Clock::time_point armedAt = Clock::now();
        g_recordTxLocks.store(true);
        releaseLane();
        QVERIFY(lane.waitIdleForTest(kLaneIdleTimeoutMs));
        g_recordTxLocks.store(false);
        const QStringList controlCalls = log.namesSince(armedAt);
        QVERIFY2(controlCalls.contains(QStringLiteral("EnterCs")),
                 "the control setter made no WDSP call on the lane");
        tx->setStageRunning(TxChannel::Stage::Eqp, false);
        QVERIFY(lane.waitIdleForTest(kLaneIdleTimeoutMs));

        // Now: hold the lane, queue setters for the old wrapper, rebuild,
        // post through the old pointer, release.
        released = false;
        holdLane();
        tx->setStageRunning(TxChannel::Stage::Eqp, true);
        tx->setTxPostGenTTFreq1(1234.0);
        ChannelConfig cfg;
        cfg.bufferSize = kTxInSize;
        cfg.filterSize = WdspEngine::kTxDspBufferSize;
        cfg.sampleRate = kRateHz;
        QVERIFY(engine.rebuildTxChannel(kTxId, cfg) >= 0);
        TxChannel* rebuilt = engine.txChannel(kTxId);
        QVERIFY(rebuilt && rebuilt != tx);
        QVERIFY(tx->isRetired());
        tx->setTxPostGenRun(true);   // through the stale pointer
        rebuilt->setStageRunning(TxChannel::Stage::Compressor, true);
        armedAt = Clock::now();
        g_recordTxLocks.store(true);
        releaseLane();
        QVERIFY(lane.waitIdleForTest(kLaneIdleTimeoutMs));
        g_recordTxLocks.store(false);
        WDSPSetCallerCheckHook(nullptr);

        // Nothing touched the channel between the release and the rebuild's
        // close (its first call is SetChannelState).
        const QStringList calls = log.namesSince(armedAt);
        qInfo("after release: %s", qPrintable(calls.mid(0, 6).join(QStringLiteral(", "))));
        QVERIFY(!calls.isEmpty());
        QCOMPARE(calls.first(), QStringLiteral("SetChannelState"));

        QVERIFY(!readOnLane(rebuilt, TxChannel::Stage::Eqp));
        QVERIFY(!readOnLane(rebuilt, TxChannel::Stage::Gen1));
        QVERIFY(readOnLane(rebuilt, TxChannel::Stage::Compressor));
        QVERIFY(rebuilt->isWdspReady());

        engine.shutdown();
        engine.setTransmitLane(nullptr);
        lane.stop();
    }

    // How long the unkey drain (SetChannelState with dmode=1) takes on the
    // lane, with microphone blocks arriving and without. The RF gate
    // closes first, so the worker stops calling fexchange0 either way; the
    // numbers go to the task's ledger.
    void unkeyDrainIsMeasured()
    {
        DspControlThread lane(DspLane::Transmit);
        lane.start();
        WdspEngine engine;
        engine.m_initialized = true;   // friend access (NEREUS_BUILD_TESTS)
        engine.setTransmitLane(&lane);
        TxChannel* tx = engine.createTxChannel(kTxId, kTxInSize,
                                               WdspEngine::kTxDspBufferSize, kRateHz,
                                               WdspEngine::kTxDspSampleRate, kRateHz);
        QVERIFY(tx);
        TimedLog log;
        MockConnection conn(&log);
        tx->setConnection(&conn);
        auto analyzer = std::make_unique<TxAnalyzer>(TxAnalyzer::kTxDispId, nullptr, &lane);
        analyzer->setSampleRate(96000.0);
        analyzer->start();
        QVERIFY(lane.waitIdleForTest(kLaneIdleTimeoutMs));

        std::mutex drainMutex;
        std::vector<double> drains;
        tx->setDrainObserverForTest([&](double ms) {
            std::lock_guard<std::mutex> lock(drainMutex);
            drains.push_back(ms);
        });
        const auto lastDrain = [&]() {
            std::lock_guard<std::mutex> lock(drainMutex);
            return drains.empty() ? -1.0 : drains.back();
        };

        // With mic blocks arriving.
        double withBlocks = -1.0;
        {
            Feeder txFeeder(txBlockTick(tx));
            tx->setRunningAsync(true);
            QTRY_VERIFY_WITH_TIMEOUT(tx->isRunning(), kLaneIdleTimeoutMs);
            QTRY_VERIFY_WITH_TIMEOUT(conn.txBlocks() > 50, 10000);
            tx->setRunningAsync(false);
            QVERIFY(!tx->isRunning());
            QVERIFY(lane.waitIdleForTest(kLaneIdleTimeoutMs));
            withBlocks = lastDrain();
            txFeeder.stop();
        }

        // With none.
        tx->setRunningAsync(true);
        QTRY_VERIFY_WITH_TIMEOUT(tx->isRunning(), kLaneIdleTimeoutMs);
        QTest::qWait(100);
        tx->setRunningAsync(false);
        QVERIFY(lane.waitIdleForTest(kLaneIdleTimeoutMs));
        const double withoutBlocks = lastDrain();

        qInfo("unkey drain on the transmit lane: %.1f ms with mic blocks arriving, "
              "%.1f ms without (SetChannelState's timeout is 100 ms)",
              withBlocks, withoutBlocks);
        {
            std::lock_guard<std::mutex> lock(drainMutex);
            QCOMPARE(drains.size(), std::size_t(2));
        }

        tx->setDrainObserverForTest({});
        tx->setConnection(nullptr);
        engine.shutdown();
        analyzer.reset();
        QVERIFY(lane.waitIdleForTest(kLaneIdleTimeoutMs));
        engine.setTransmitLane(nullptr);
        lane.stop();
    }
#endif // HAVE_WDSP
};

QTEST_MAIN(TestDspControlTransmit)
#include "tst_dsp_control_transmit.moc"
