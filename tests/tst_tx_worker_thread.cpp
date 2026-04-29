// =================================================================
// tests/tst_tx_worker_thread.cpp  (NereusSDR)
// =================================================================
//
// Unit tests for TxWorkerThread (Phase 3M-1c TX pump architecture
// redesign).  See src/core/TxWorkerThread.{h,cpp} for the class and
// docs/architecture/phase3m-1c-tx-pump-architecture-plan.md for the
// architectural rationale.
//
// Test surface:
//   1. Construct/destruct cleanly without start.
//   2. startPump fires onPumpTick at ~5 ms cadence (cadence test).
//   3. onPumpTick pulls from AudioEngine and pushes to TxChannel
//      with the correct block size.
//   4. Bus returning < kBlockFrames → zero-fill in driveOneTxBlock
//      (silence-falls-out-for-free behaviour).
//   5. Cross-thread setter race — main thread setMicPreamp while
//      worker is pumping does not crash and the value eventually
//      lands.
//   6. stopPump while a tick is in flight completes the current
//      tick before exiting.
//
// =================================================================
//
// Modification history (NereusSDR):
//   2026-04-29 — New test for Phase 3M-1c TX pump architecture redesign.
//                 J.J. Boyd (KG4VCF), with AI-assisted implementation
//                 via Anthropic Claude Code.
// =================================================================

// no-port-check: NereusSDR-original test file.  No Thetis logic ported.
// NEREUS_BUILD_TESTS must be defined (see tests/CMakeLists.txt).

#include <QtTest/QtTest>
#include <QObject>
#include <QSignalSpy>
#include <QElapsedTimer>

#include <atomic>
#include <vector>

#include "core/AudioEngine.h"
#include "core/TxChannel.h"
#include "core/TxWorkerThread.h"
#include "core/RadioConnection.h"

#include "fakes/FakeAudioBus.h"

using namespace NereusSDR;

// ── MockConnection — counts sendTxIq calls + records lastN ──────────────────
class MockConnection : public RadioConnection {
    Q_OBJECT
public:
    explicit MockConnection(QObject* parent = nullptr)
        : RadioConnection(parent)
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
    void setWatchdogEnabled(bool) override {}
    void setAntennaRouting(AntennaRouting) override {}
    void setMox(bool) override {}
    void setTrxRelay(bool) override {}
    void setMicBoost(bool) override {}
    void setLineIn(bool) override {}
    void setMicTipRing(bool) override {}
    void setMicBias(bool) override {}
    void setMicPTT(bool) override {}
    void setMicXlr(bool) override {}

    void sendTxIq(const float*, int n) override
    {
        // sendTxIq runs on the worker thread (TxWorkerThread).
        // Use atomics so the main-thread test code can read counters.
        callCount.fetch_add(1, std::memory_order_relaxed);
        lastN.store(n, std::memory_order_relaxed);
    }

    std::atomic<int> callCount{0};
    std::atomic<int> lastN{0};
};

// ── Fixture ─────────────────────────────────────────────────────────────────
class TestTxWorkerThread : public QObject {
    Q_OBJECT

    static constexpr int kChannelId = 1;
    static constexpr int kBufSize   = 256;  // == TxWorkerThread::kBlockFrames

    // Float32 stereo bus payload helper.  Mirrors the helper in
    // tst_audio_engine_pull_tx_mic.cpp — produces `frames` interleaved
    // L/R samples where L == R == startValue + i*step.
    static QByteArray makeFloat32StereoBytes(int frames, float startValue = 0.0f,
                                             float step = 0.0f)
    {
        QByteArray out;
        out.resize(frames * 2 * 4);  // 2 channels × 4 bytes
        float* p = reinterpret_cast<float*>(out.data());
        float v = startValue;
        for (int i = 0; i < frames; ++i) {
            p[i * 2 + 0] = v;  // left
            p[i * 2 + 1] = v;  // right
            v += step;
        }
        return out;
    }

private slots:

    // ── 1. Construct + destruct without start ───────────────────────────────
    //
    // The dtor should not block waiting for a thread that was never started.
    void constructDestruct_clean()
    {
        TxWorkerThread w;
        QCOMPARE(w.isRunning(), false);
        // Falling out of scope destroys w; should not hang or crash.
    }

    // ── 2. startPump without dependencies is a warning + no-op ──────────────
    void startPump_withoutDeps_warns_andStaysStopped()
    {
        TxWorkerThread w;
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression("startPump: missing dependencies"));
        w.startPump();
        QCOMPARE(w.isRunning(), false);
    }

    // ── 3. startPump + stopPump lifecycle ───────────────────────────────────
    //
    // With deps wired, startPump → isRunning() == true; stopPump → false.
    void startStop_lifecycle_isRunningTracksThread()
    {
        AudioEngine engine;
        TxChannel ch(kChannelId, kBufSize, kBufSize);
        MockConnection conn;
        ch.setConnection(&conn);

        TxWorkerThread w;
        w.setTxChannel(&ch);
        w.setAudioEngine(&engine);

        QCOMPARE(w.isRunning(), false);
        w.startPump();
        // QThread::start() returns before the new thread has actually
        // started running; isRunning() may briefly be false on some
        // platforms.  Wait briefly for the transition.
        QTRY_COMPARE_WITH_TIMEOUT(w.isRunning(), true, 500);

        w.stopPump();
        QCOMPARE(w.isRunning(), false);
    }

    // ── 4. onPumpTick pulls and drives a 256-sample block ───────────────────
    //
    // Synchronous test using tickForTest seam: bus has 256 frames available;
    // tickForTest pulls them and pushes through TxChannel → MockConnection.
    void tickForTest_drivesOneSendTxIq_whenBusHasFullBlock()
    {
        AudioEngine engine;
        AudioFormat fmt{};
        fmt.sample = AudioFormat::Sample::Float32;
        fmt.channels = 2;
        fmt.sampleRate = 48000;
        auto fakeBus = std::make_unique<FakeAudioBus>(QStringLiteral("FakeMic"));
        fakeBus->open(fmt);
        FakeAudioBus* bus = fakeBus.get();
        engine.setTxInputBusForTest(std::move(fakeBus));

        TxChannel ch(kChannelId, kBufSize, kBufSize);
        MockConnection conn;
        ch.setConnection(&conn);
        ch.setRunning(true);

        TxWorkerThread w;
        w.setTxChannel(&ch);
        w.setAudioEngine(&engine);

        // Bus has exactly kBufSize frames available.
        bus->setPullData(makeFloat32StereoBytes(kBufSize, 0.5f, 0.0f));

        // Drive one synchronous tick.
        w.tickForTest();

        QCOMPARE(conn.callCount.load(), 1);
        QCOMPARE(conn.lastN.load(), kBufSize);
    }

    // ── 5. Empty bus → tickForTest still drives fexchange2 (silence path) ──
    //
    // When AudioEngine::pullTxMic returns 0 (bus empty / null), the worker
    // zero-fills the buffer and dispatches anyway.  TUNE-tone PostGen +
    // fexchange2 idle output still produce sendTxIq.  This is the
    // "silence-falls-out-for-free" property documented in plan §4.3.
    void tickForTest_drivesFexchange2_onEmptyBus_zeroFilled()
    {
        AudioEngine engine;  // no bus configured → pullTxMic returns 0
        TxChannel ch(kChannelId, kBufSize, kBufSize);
        MockConnection conn;
        ch.setConnection(&conn);
        ch.setRunning(true);

        TxWorkerThread w;
        w.setTxChannel(&ch);
        w.setAudioEngine(&engine);

        // No bus configured at all — pullTxMic returns 0.
        w.tickForTest();

        // sendTxIq should still fire (silence path).
        QCOMPARE(conn.callCount.load(), 1);
        QCOMPARE(conn.lastN.load(), kBufSize);

        // Verify the m_inI buffer is all zeros (zero-fill happened).
        const auto& inI = ch.inIForTest();
        QCOMPARE(static_cast<int>(inI.size()), kBufSize);
        for (int i = 0; i < kBufSize; ++i) {
            QCOMPARE(inI[i], 0.0f);
        }
    }

    // ── 6. Partial bus → tickForTest zero-fills the gap ─────────────────────
    //
    // Bus has only 100 frames available.  TxWorkerThread pulls those 100
    // and zero-fills the remaining 156, pushing exactly kBufSize=256 to
    // driveOneTxBlock.  This exercises the "got < kBlockFrames" branch.
    void tickForTest_zeroFillsGap_whenBusPartial()
    {
        AudioEngine engine;
        AudioFormat fmt{};
        fmt.sample = AudioFormat::Sample::Float32;
        fmt.channels = 2;
        fmt.sampleRate = 48000;
        auto fakeBus = std::make_unique<FakeAudioBus>(QStringLiteral("FakeMic"));
        fakeBus->open(fmt);
        FakeAudioBus* bus = fakeBus.get();
        engine.setTxInputBusForTest(std::move(fakeBus));

        TxChannel ch(kChannelId, kBufSize, kBufSize);
        MockConnection conn;
        ch.setConnection(&conn);
        ch.setRunning(true);

        TxWorkerThread w;
        w.setTxChannel(&ch);
        w.setAudioEngine(&engine);

        // Bus has 100 frames; TxWorkerThread should zero-fill bytes 100-255.
        // Use a constant 0.7f payload so we can distinguish the 100 pulled
        // samples from the zero-filled tail.
        bus->setPullData(makeFloat32StereoBytes(100, 0.7f, 0.0f));

        w.tickForTest();

        // Exactly one sendTxIq call (one block dispatched).
        QCOMPARE(conn.callCount.load(), 1);
        QCOMPARE(conn.lastN.load(), kBufSize);

        // Verify zero-fill: inI[0..99] = 0.7, inI[100..255] = 0.0.
        const auto& inI = ch.inIForTest();
        QCOMPARE(static_cast<int>(inI.size()), kBufSize);
        // The first 100 mono samples should be 0.7 (left channel of the
        // float32 stereo payload).
        for (int i = 0; i < 100; ++i) {
            QCOMPARE(inI[i], 0.7f);
        }
        // The remaining samples (100..255) should be zero-filled.
        for (int i = 100; i < kBufSize; ++i) {
            QCOMPARE(inI[i], 0.0f);
        }
    }

    // ── 7. Real worker thread — pump cadence ───────────────────────────────
    //
    // Start the worker, let it run for ~100 ms, verify sendTxIq fires
    // multiple times.  Wide tolerance (≥ 2 ticks in 100 ms) absorbs
    // QTimer scheduling jitter on busy CI without leaving the test
    // useless — under -j8 load the worker sometimes only manages 3-4
    // ticks per 100 ms instead of the ideal ~20.  Asserting ≥ 2 still
    // catches the regression "no ticks at all" (the actual bench
    // failure mode pre-redesign).
    void realWorker_pumpCadence_atLeastTwoTicksIn100ms()
    {
        AudioEngine engine;
        TxChannel ch(kChannelId, kBufSize, kBufSize);
        MockConnection conn;
        ch.setConnection(&conn);
        ch.setRunning(true);

        TxWorkerThread w;
        w.setTxChannel(&ch);
        w.setAudioEngine(&engine);

        QElapsedTimer elapsed;
        elapsed.start();
        w.startPump();
        QTest::qWait(100);  // 100 ms — wider window to absorb CI jitter
        const qint64 elapsedMs = elapsed.elapsed();
        const int callsObserved = conn.callCount.load();
        w.stopPump();

        // Move TxChannel back so destructor runs cleanly on this thread.
        ch.moveToThread(this->thread());

        // Expected ideal: ~20 ticks (100 / 5).  Lower bound 2 catches
        // "no ticks at all" while tolerating heavy CI load.
        QVERIFY2(callsObserved >= 2,
                 qPrintable(QStringLiteral("pump did not tick — got %1 calls in %2 ms")
                                .arg(callsObserved)
                                .arg(elapsedMs)));
    }

    // ── 8. Cross-thread setter race — setMicPreamp from main while pumping ─
    //
    // While the worker pump is running on the worker thread, repeatedly
    // call setMicPreamp from the main thread.  Verify:
    //   (a) No crash.
    //   (b) The final value lands (visible via lastMicPreampForTest()).
    //
    // setMicPreamp's m_micPreampLast field is plain double, but per the
    // setter audit (TxChannel.h §"Thread safety") only the setter itself
    // touches it, and the setter is called from a single thread (here
    // the test main thread).  WDSP's csDSP critical section serializes
    // setter↔fexchange2.  This test verifies the practical absence of
    // observable corruption under contention.
    void crossThreadSetter_noCrash_finalValueLands()
    {
        AudioEngine engine;
        TxChannel ch(kChannelId, kBufSize, kBufSize);
        MockConnection conn;
        ch.setConnection(&conn);
        ch.setRunning(true);

        TxWorkerThread w;
        w.setTxChannel(&ch);
        w.setAudioEngine(&engine);

        w.startPump();

        // Hammer setMicPreamp from main thread while worker is pumping.
        constexpr int kIterations = 100;
        for (int i = 0; i < kIterations; ++i) {
            ch.setMicPreamp(static_cast<double>(i) * 0.01);
        }

        // Allow a few pump ticks to run.
        QTest::qWait(20);

        w.stopPump();
        ch.moveToThread(this->thread());

        // Final value should be 0.99 (99 * 0.01) after the loop above.
        QCOMPARE(ch.lastMicPreampForTest(), 0.99);
    }
};

QTEST_MAIN(TestTxWorkerThread)
#include "tst_tx_worker_thread.moc"
