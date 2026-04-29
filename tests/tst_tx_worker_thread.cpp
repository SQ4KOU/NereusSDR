// =================================================================
// tests/tst_tx_worker_thread.cpp  (NereusSDR)
// =================================================================
//
// Unit tests for TxWorkerThread (Phase 3M-1c TX pump architecture
// redesign v3 — semaphore-driven, sourced from TxMicSource).  See
// src/core/TxWorkerThread.{h,cpp} for the class and
// docs/architecture/phase3m-1c-tx-pump-architecture-plan.md for the
// architectural rationale.
//
// Test surface:
//   1. Construct/destruct cleanly without start.
//   2. startPump without dependencies — warning + stays stopped.
//   3. startPump + stopPump lifecycle — isRunning tracks thread.
//   4. tickForTest with one block in the mic source — exactly one
//      sendTxIq fires.
//   5. tickForTest with no inbound() — no sendTxIq fires (test seam
//      timeout returns false).
//   6. PC mic override path — when isPcMicOverrideActive() is true,
//      the I-channel data passed to fexchange0 comes from PC mic, not
//      radio mic.
//   7. Real worker thread fires fexchange0 in response to inbound().
//   8. Cross-thread setter race — setMicPreamp from main thread
//      while worker is pumping does not crash and the value lands.
//   9. Cross-thread setter race — setVoxRun from main thread (the
//      MoxController-routed lambda-connect form).
//
// =================================================================
//
// Modification history (NereusSDR):
//   2026-04-29 — New test for Phase 3M-1c TX pump architecture redesign.
//                 Rewritten same day for v3 semaphore-driven design.
//                 J.J. Boyd (KG4VCF), with AI-assisted implementation
//                 via Anthropic Claude Code.
// =================================================================

// no-port-check: NereusSDR-original test file.  No Thetis logic ported.
// NEREUS_BUILD_TESTS must be defined (see tests/CMakeLists.txt).

#include <QtTest/QtTest>
#include <QObject>
#include <QSignalSpy>

#include <atomic>
#include <cstring>
#include <vector>

#include "core/AudioEngine.h"
#include "core/TxChannel.h"
#include "core/TxWorkerThread.h"
#include "core/RadioConnection.h"
#include "core/audio/TxMicSource.h"

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

    void sendTxIq(const float* iq, int n) override
    {
        callCount.fetch_add(1, std::memory_order_relaxed);
        lastN.store(n, std::memory_order_relaxed);
        // Stash first I sample for override-path verification (atomic
        // double via reinterpret).  Use float, exact-bit-compare via
        // std::atomic<int32_t>.
        if (iq != nullptr && n > 0) {
            int32_t bits = 0;
            std::memcpy(&bits, &iq[0], sizeof(int32_t));
            firstISampleBits.store(bits, std::memory_order_relaxed);
        }
    }

    std::atomic<int> callCount{0};
    std::atomic<int> lastN{0};
    std::atomic<int32_t> firstISampleBits{0};
};

// ── Fixture ─────────────────────────────────────────────────────────────────
class TestTxWorkerThread : public QObject {
    Q_OBJECT

    static constexpr int kChannelId = 1;
    // Use the Thetis-faithful 64-block size end-to-end.
    static constexpr int kBufSize   = TxWorkerThread::kBlockFrames;

private slots:

    // ── 1. Construct + destruct without start ───────────────────────────────
    void constructDestruct_clean()
    {
        TxWorkerThread w;
        QCOMPARE(w.isRunning(), false);
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
    void startStop_lifecycle_isRunningTracksThread()
    {
        AudioEngine engine;
        TxChannel ch(kChannelId, kBufSize, kBufSize);
        MockConnection conn;
        ch.setConnection(&conn);

        TxMicSource src;
        src.start();

        TxWorkerThread w;
        w.setTxChannel(&ch);
        w.setAudioEngine(&engine);
        w.setMicSource(&src);

        QCOMPARE(w.isRunning(), false);
        w.startPump();
        QTRY_COMPARE_WITH_TIMEOUT(w.isRunning(), true, 500);

        w.stopPump();
        QCOMPARE(w.isRunning(), false);
    }

    // ── 4. One inbound block → tickForTest fires exactly one fexchange0 ─────
    void tickForTest_drivesOneSendTxIq_whenBlockAvailable()
    {
        AudioEngine engine;
        TxChannel ch(kChannelId, kBufSize, kBufSize);
        MockConnection conn;
        ch.setConnection(&conn);
        ch.setRunning(true);

        TxMicSource src;
        src.start();

        TxWorkerThread w;
        w.setTxChannel(&ch);
        w.setAudioEngine(&engine);
        w.setMicSource(&src);

        // Push exactly one block of mic samples.
        std::vector<float> samples(kBufSize, 0.5f);
        src.inbound(samples.data(), kBufSize);

        // Drive one synchronous tick (waitForBlock + drain + dispatch).
        w.tickForTest();

        QCOMPARE(conn.callCount.load(), 1);
        QCOMPARE(conn.lastN.load(), kBufSize);

        src.stop();
    }

    // ── 5. No inbound → tickForTest is a no-op (semaphore times out) ────────
    void tickForTest_noBlock_doesNotDispatch()
    {
        AudioEngine engine;
        TxChannel ch(kChannelId, kBufSize, kBufSize);
        MockConnection conn;
        ch.setConnection(&conn);
        ch.setRunning(true);

        TxMicSource src;
        src.start();

        TxWorkerThread w;
        w.setTxChannel(&ch);
        w.setAudioEngine(&engine);
        w.setMicSource(&src);

        // No inbound() → tickForTest's waitForBlock(100) times out.
        w.tickForTest();

        QCOMPARE(conn.callCount.load(), 0);
        src.stop();
    }

    // ── 6. PC mic override — PC samples replace radio samples in m_in ──────
    //
    // Push radio mic samples = 0.3.  PC mic bus has 0.7.  After tickForTest,
    // the I channel of m_in (and therefore the I sample of m_outInterleavedFloat
    // sent via sendTxIq) reflects the PC mic value, not 0.3.
    //
    // We can't easily inspect m_outInterleavedFloat, so we verify via
    // TxChannel::inForTest() that m_in's I channel was overwritten with PC
    // samples before fexchange0 ran.
    void pcMicOverride_overwritesRadioSamplesInIChannel()
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

        // Tell engine the user picked PC mic.
        engine.onMicSourceChanged(/*selectedSourceIsPc=*/true);
        QVERIFY(engine.isPcMicOverrideActive());

        TxChannel ch(kChannelId, kBufSize, kBufSize);
        MockConnection conn;
        ch.setConnection(&conn);
        ch.setRunning(true);

        TxMicSource src;
        src.start();

        TxWorkerThread w;
        w.setTxChannel(&ch);
        w.setAudioEngine(&engine);
        w.setMicSource(&src);

        // Radio mic = 0.3.  PC mic bus = 0.7 (interleaved L/R floats).
        std::vector<float> radio(kBufSize, 0.3f);
        src.inbound(radio.data(), kBufSize);

        QByteArray pcm;
        pcm.resize(kBufSize * 2 * 4);
        float* p = reinterpret_cast<float*>(pcm.data());
        for (int i = 0; i < kBufSize; ++i) {
            p[i * 2 + 0] = 0.7f;  // left
            p[i * 2 + 1] = 0.7f;  // right (ignored)
        }
        bus->setPullData(pcm);

        w.tickForTest();
        QCOMPARE(conn.callCount.load(), 1);

        // After dispatch, m_in's I channel should be 0.7 (PC mic), not 0.3.
        const auto& in = ch.inForTest();
        QCOMPARE(static_cast<int>(in.size()), 2 * kBufSize);
        for (int i = 0; i < kBufSize; ++i) {
            // Float→double promotion: compare against the same promotion.
            QCOMPARE(in[2 * i + 0], static_cast<double>(0.7f));
            QCOMPARE(in[2 * i + 1], 0.0);  // Q always zero
        }

        src.stop();
    }

    // ── 7. Real worker thread fires fexchange0 in response to inbound() ────
    //
    // Start the worker as a real QThread.  Push a few blocks via inbound().
    // The worker should wake on each semaphore release and produce one
    // sendTxIq call per block.
    void realWorker_inboundDrivesFexchange0()
    {
        AudioEngine engine;
        TxChannel ch(kChannelId, kBufSize, kBufSize);
        MockConnection conn;
        ch.setConnection(&conn);
        ch.setRunning(true);

        TxMicSource src;
        src.start();

        TxWorkerThread w;
        w.setTxChannel(&ch);
        w.setAudioEngine(&engine);
        w.setMicSource(&src);

        w.startPump();
        QTRY_COMPARE_WITH_TIMEOUT(w.isRunning(), true, 500);

        // Push 5 blocks.
        const int kBlocks = 5;
        for (int blk = 0; blk < kBlocks; ++blk) {
            std::vector<float> samples(kBufSize, 0.1f);
            src.inbound(samples.data(), kBufSize);
        }

        // Wait for the worker to drain all 5 blocks.
        QTRY_COMPARE_WITH_TIMEOUT(conn.callCount.load(), kBlocks, 1000);

        w.stopPump();
        ch.moveToThread(this->thread());
    }

    // ── 8. Cross-thread setter race — setMicPreamp from main while pumping ─
    //
    // While the worker is pumping, hammer setMicPreamp from main.  Verify
    // (a) no crash and (b) the final value lands.  Identical to the v2
    // race test but using the new mic-source-driven cadence.
    void crossThreadSetter_micPreamp_noCrash_finalValueLands()
    {
        AudioEngine engine;
        TxChannel ch(kChannelId, kBufSize, kBufSize);
        MockConnection conn;
        ch.setConnection(&conn);
        ch.setRunning(true);

        TxMicSource src;
        src.start();

        TxWorkerThread w;
        w.setTxChannel(&ch);
        w.setAudioEngine(&engine);
        w.setMicSource(&src);

        w.startPump();
        QTRY_COMPARE_WITH_TIMEOUT(w.isRunning(), true, 500);

        // Hammer setMicPreamp from main thread while worker is pumping.
        // Push some inbound blocks too so the worker actually runs.
        constexpr int kIterations = 100;
        for (int i = 0; i < kIterations; ++i) {
            ch.setMicPreamp(static_cast<double>(i) * 0.01);
            if ((i % 10) == 0) {
                std::vector<float> samples(kBufSize, 0.0f);
                src.inbound(samples.data(), kBufSize);
            }
        }

        QTest::qWait(20);

        w.stopPump();
        ch.moveToThread(this->thread());

        QCOMPARE(ch.lastMicPreampForTest(), 0.99);
    }

    // ── 9. Cross-thread setter race — setVoxRun (MoxController lambda form) ─
    void crossThreadSetter_voxRun_noCrash_finalValueLands()
    {
        AudioEngine engine;
        TxChannel ch(kChannelId, kBufSize, kBufSize);
        MockConnection conn;
        ch.setConnection(&conn);
        ch.setRunning(true);

        TxMicSource src;
        src.start();

        TxWorkerThread w;
        w.setTxChannel(&ch);
        w.setAudioEngine(&engine);
        w.setMicSource(&src);

        w.startPump();
        QTRY_COMPARE_WITH_TIMEOUT(w.isRunning(), true, 500);

        constexpr int kIterations = 100;
        for (int i = 0; i < kIterations; ++i) {
            ch.setVoxRun((i % 2) == 0);
            if ((i % 10) == 0) {
                std::vector<float> samples(kBufSize, 0.0f);
                src.inbound(samples.data(), kBufSize);
            }
        }

        QTest::qWait(20);

        w.stopPump();
        ch.moveToThread(this->thread());

        // Final value: i==99 is odd → setVoxRun(false) was the last call.
        QCOMPARE(ch.lastVoxRunForTest(), false);
    }
};

QTEST_MAIN(TestTxWorkerThread)
#include "tst_tx_worker_thread.moc"
