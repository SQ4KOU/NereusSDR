// =================================================================
// tests/tst_rade_rx_multislice_routing.cpp  (NereusSDR)
// =================================================================
//
// R-R3-31, then RADE threads: NereusSDR-native regression for RADE receive
// on several slices at once. There is no upstream multi-slice orchestration
// equivalent to port (no port check applies).
//
// Fixtures: the real WDSP receive channels (synchronous test init), the real
// RADE codec with its built-in weights (the "dummy" model sentinel,
// rade_api_nopy.c), and a synthetic 64-frame I/Q block. No radio, nothing
// keys a transmitter: MOX edges are the model's signal only.
//
// Modification history (NereusSDR):
//   2026-09-21 -- Added by J.J. Boyd (KG4VCF), with AI-assisted
//                 implementation via OpenAI Codex.
//   2026-09-30 -- RADE threads: every RADE slice decodes at once, each on
//                 its own decoder thread; a stalled decoder plays silence
//                 and never holds the mixer; a removed slice takes its
//                 decoder with it. The single-owner checks this file held
//                 are retired with the single owner. J.J. Boyd (KG4VCF),
//                 with AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include <QRegularExpression>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QThread>

#include "core/AudioEngine.h"
#include "core/MoxController.h"
#include "core/RadeChannel.h"
#include "core/RadeRxWorker.h"
#include "core/RxChannel.h"
#include "core/TxSliceArbiter.h"
#include "core/WdspEngine.h"
#include "fakes/FakeAudioBus.h"
#include "models/RadioModel.h"
#include "models/RxDspWorker.h"
#include "models/SliceModel.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>

using namespace NereusSDR;

namespace {

constexpr int kFrames = 64;

struct DspWorkerDetach {
    RadioModel* radio{nullptr};
    ~DspWorkerDetach()
    {
        if (radio) {
            radio->attachDspWorkerForTest(nullptr);
        }
    }
};

struct BusView {
    std::unique_ptr<FakeAudioBus> owned;
    FakeAudioBus* view{nullptr};
};

BusView makeOpenBus(const QString& name)
{
    BusView result;
    result.owned = std::make_unique<FakeAudioBus>(name);
    AudioFormat format;
    format.sampleRate = 48000;
    format.channels = 2;
    format.sample = AudioFormat::Sample::Float32;
    if (!result.owned->open(format)) {
        return result;
    }
    result.view = result.owned.get();
    return result;
}

QVector<float> makeIqBlock(int frames)
{
    QVector<float> iq(frames * 2);
    for (int i = 0; i < frames; ++i) {
        const float sample = 0.08f * static_cast<float>((i % 11) - 5);
        iq[2 * i] = sample;
        iq[2 * i + 1] = -sample;
    }
    return iq;
}

// A decoder stall a test holds and lets go of.
struct Gate {
    std::mutex mutex;
    std::condition_variable cv;
    bool open{false};
    std::atomic<int> entered{0};

    void hold()
    {
        entered.fetch_add(1);
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [this] { return open; });
    }
    void release()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            open = true;
        }
        cv.notify_all();
    }
};

// Two receivers A (stream 0, VAX 1) and B (stream 1, VAX 2) on a real WDSP
// engine and a worker driven on this thread.
struct TwoSliceRig {
    RadioModel radio;
    int a{-1};
    int b{-1};
    SliceModel* sliceA{nullptr};
    SliceModel* sliceB{nullptr};
    WdspEngine* wdsp{nullptr};
    FakeAudioBus* speakers{nullptr};
    FakeAudioBus* vaxA{nullptr};
    FakeAudioBus* vaxB{nullptr};
    RxDspWorker worker;
    std::unique_ptr<DspWorkerDetach> detach;
    QVector<float> iq{makeIqBlock(kFrames)};

    bool setUp()
    {
        radio.configureStreamPool(/*userDdcCount=*/2, /*maxSlices=*/2,
                                  /*defaultRateHz=*/48000);
        a = radio.addSlice();
        b = radio.addSlice();
        sliceA = radio.sliceById(a);
        sliceB = radio.sliceById(b);
        if (!sliceA || !sliceB) {
            return false;
        }
        sliceA->setVaxChannel(1);
        sliceB->setVaxChannel(2);
        AudioEngine* const audio = radio.audioEngine();
        wdsp = radio.wdspEngine();
        wdsp->setSynchronousInitForTest(true);
        if (!wdsp->initialize(QStandardPaths::writableLocation(
                QStandardPaths::AppConfigLocation))) {
            return false;
        }
        BusView spk = makeOpenBus(QStringLiteral("speakers"));
        BusView va = makeOpenBus(QStringLiteral("vax-a"));
        BusView vb = makeOpenBus(QStringLiteral("vax-b"));
        if (!spk.view || !va.view || !vb.view) {
            return false;
        }
        speakers = spk.view;
        vaxA = va.view;
        vaxB = vb.view;
        audio->setSpeakersBusForTest(std::move(spk.owned));
        audio->setVaxBusForTest(1, std::move(va.owned));
        audio->setVaxBusForTest(2, std::move(vb.owned));
        RxChannel* const rxA = wdsp->createRxChannel(a, kFrames, 4096, 48000, 48000, 48000);
        RxChannel* const rxB = wdsp->createRxChannel(b, kFrames, 4096, 48000, 48000, 48000);
        if (!rxA || !rxB) {
            return false;
        }
        rxA->setActive(true);
        rxB->setActive(true);
        worker.setEngines(wdsp, audio);
        worker.setBufferSizes(kFrames, kFrames);
        worker.setStreamSlices(0, QVector<int>{a});
        worker.setStreamSlices(1, QVector<int>{b});
        radio.attachDspWorkerForTest(&worker);
        detach = std::make_unique<DspWorkerDetach>();
        detach->radio = &radio;
        return true;
    }

    // The app's path into RADE: the slice's own mode change creates, wires
    // and starts its channel through the engine.
    RadeChannel* toRade(SliceModel* slice)
    {
        slice->setDspMode(DSPMode::RADE_U);
        QCoreApplication::processEvents();  // the route reaches the worker
        return wdsp->radeChannel(slice->sliceIndex());
    }
};

}  // namespace

class TestRadeRxMultisliceRouting : public QObject {
    Q_OBJECT

private slots:
    // The late bound is one rade_rx input (rade_nin_max, 1120 samples at
    // 8 kHz = 140 ms) in the mixer's own blocks.
    void lateBoundIsOneRadeFrameOfBlocks()
    {
        QCOMPARE(kRadeNinMax8k, 1120);
        QCOMPARE(radeLateBoundBlocks(64), 105);
        QCOMPARE(radeLateBoundBlocks(1024), 7);
    }

    // The due record plays; one older than its slot, or from a replaced
    // route, is dropped rather than played late; a later one waits.
    void bridgePlaysDueBlockAndDropsLateOnes()
    {
        RadeRxBridge bridge;
        const quint32 stale = bridge.nextEpoch();
        const quint32 epoch = bridge.nextEpoch();
        const float speech[8] = {1, 1, 2, 2, 3, 3, 4, 4};
        std::vector<float> out;

        QVERIFY(bridge.pushOutput({stale, 1, 4}, speech, 4));
        QVERIFY(bridge.pushOutput({epoch, 0, 4}, speech, 4));
        QVERIFY(bridge.pushOutput({epoch, 1, 4}, speech, 4));
        QVERIFY(bridge.pushOutput({epoch, 5, 4}, speech, 4));

        QCOMPARE(bridge.takeDue(epoch, 1, out), 4);
        QCOMPARE(bridge.lateDrops(), quint64(2));  // other epoch, and seq 0
        QCOMPARE(out.size(), size_t(8));
        QCOMPARE(out[6], 4.0f);

        QCOMPARE(bridge.takeDue(epoch, 3, out), 0);  // seq 5 is early: kept
        QCOMPARE(bridge.lateDrops(), quint64(2));
        QCOMPARE(bridge.takeDue(epoch, 5, out), 4);
        QCOMPARE(bridge.takeDue(epoch, 6, out), 0);  // nothing: silence
    }

    // Ruling 1: both RADE slices decode at once, each on its own thread,
    // which is not the main thread, and both reach the speakers.
    void twoRadeSlicesDecodeAtOnceOnTheirOwnThreads()
    {
        TwoSliceRig rig;
        QVERIFY(rig.setUp());
        RadeChannel* const radeA = rig.toRade(rig.sliceA);
        RadeChannel* const radeB = rig.toRade(rig.sliceB);
        QVERIFY(radeA && radeB && radeA != radeB);
        QVERIFY(radeA->isActive() && radeB->isActive());
        QCOMPARE(rig.worker.radeRxRouteCount(), 2);

        // Which thread runs processIq's body, seen from inside it.
        std::atomic<Qt::HANDLE> decodedOnA{nullptr};
        std::atomic<Qt::HANDLE> decodedOnB{nullptr};
        connect(radeA, &RadeChannel::rxSpeechReady, radeA,
                [&decodedOnA](const QByteArray&) {
                    decodedOnA.store(QThread::currentThreadId());
                }, Qt::DirectConnection);
        connect(radeB, &RadeChannel::rxSpeechReady, radeB,
                [&decodedOnB](const QByteArray&) {
                    decodedOnB.store(QThread::currentThreadId());
                }, Qt::DirectConnection);
        QSignalSpy speechA(radeA, &RadeChannel::rxSpeechReady);
        QSignalSpy speechB(radeB, &RadeChannel::rxSpeechReady);

        const int vaxABefore = rig.vaxA->pushCount();
        const int vaxBBefore = rig.vaxB->pushCount();
        const int masterBefore = rig.speakers->pushCount();
        constexpr int kBlocks = 512;  // past the resamplers and one rade_rx
        for (int i = 0; i < kBlocks; ++i) {
            rig.worker.processIqBatch(0, rig.iq);
            rig.worker.processIqBatch(1, rig.iq);
            QVERIFY(radeA->waitRxIdleForTest(5000));
            QVERIFY(radeB->waitRxIdleForTest(5000));
        }

        const QString evidence = QStringLiteral(
            "speechA=%1 speechB=%2 radeRxA=%3 radeRxB=%4 vaxA=%5 vaxB=%6 master=%7")
            .arg(speechA.count()).arg(speechB.count())
            .arg(radeA->radeRxCallCountForTest()).arg(radeB->radeRxCallCountForTest())
            .arg(rig.vaxA->pushCount() - vaxABefore)
            .arg(rig.vaxB->pushCount() - vaxBBefore)
            .arg(rig.speakers->pushCount() - masterBefore);
        // Both got input and ran the codec, both produced output.
        QVERIFY2(radeA->radeRxCallCountForTest() > 0
                     && radeB->radeRxCallCountForTest() > 0, qPrintable(evidence));
        QVERIFY2(speechA.count() > 0 && speechB.count() > 0, qPrintable(evidence));
        QVERIFY2(rig.vaxA->pushCount() > vaxABefore
                     && rig.vaxB->pushCount() > vaxBBefore
                     && rig.speakers->pushCount() > masterBefore,
                 qPrintable(evidence));

        // Each decoded on its own thread, named for its slice.
        const Qt::HANDLE mainThread = QThread::currentThreadId();
        QVERIFY(decodedOnA.load() != nullptr && decodedOnB.load() != nullptr);
        QVERIFY(decodedOnA.load() != mainThread);
        QVERIFY(decodedOnB.load() != mainThread);
        QVERIFY(decodedOnA.load() != decodedOnB.load());
        QCOMPARE(decodedOnA.load(), radeA->rxThreadIdForTest());
        QCOMPARE(decodedOnB.load(), radeB->rxThreadIdForTest());
        QCOMPARE(radeA->rxThreadNameForTest(), QStringLiteral("RadeRx%1").arg(rig.a));
        QCOMPARE(radeB->rxThreadNameForTest(), QStringLiteral("RadeRx%1").arg(rig.b));
        // The tick log counts per channel.
        QCOMPARE(radeA->rxTickCountForTest(), speechA.count());
        QCOMPARE(radeB->rxTickCountForTest(), speechB.count());
    }

    // Ruling 2: a decoder that never returns costs its own slice silence,
    // never the other slices' time, and its late speech is dropped.
    void aStalledDecoderNeverHoldsTheOtherSlices()
    {
        // The gate outlives the rig (whose teardown joins the decoder), and
        // is opened on every exit path so a failed check cannot hang it.
        Gate gate;
        TwoSliceRig rig;
        struct OpenOnExit {
            Gate& gate;
            ~OpenOnExit() { gate.release(); }
        } openOnExit{gate};
        QVERIFY(rig.setUp());
        RadeChannel* const radeB = rig.toRade(rig.sliceB);
        QVERIFY(radeB && radeB->isActive());
        std::shared_ptr<RadeRxBridge> bridge = radeB->rxBridge();
        QVERIFY(bridge);

        // Both slices in the mixer first.
        for (int i = 0; i < 8; ++i) {
            rig.worker.processIqBatch(0, rig.iq);
            rig.worker.processIqBatch(1, rig.iq);
        }
        QVERIFY(radeB->waitRxIdleForTest(5000));

        radeB->setRxStallHookForTest([&gate] { gate.hold(); });
        const int masterBefore = rig.speakers->pushCount();
        const int vaxABefore = rig.vaxA->pushCount();
        const int vaxBBefore = rig.vaxB->pushCount();
        const quint64 silentBefore = bridge->silentSlots();
        const int lateBound = radeLateBoundBlocks(kFrames);
        const int blocks = 2 * lateBound;
        for (int i = 0; i < blocks; ++i) {
            rig.worker.processIqBatch(0, rig.iq);
            rig.worker.processIqBatch(1, rig.iq);
        }
        const int masterDelta = rig.speakers->pushCount() - masterBefore;
        const int vaxADelta = rig.vaxA->pushCount() - vaxABefore;
        const int vaxBDelta = rig.vaxB->pushCount() - vaxBBefore;
        const quint64 silentDelta = bridge->silentSlots() - silentBefore;
        const QString evidence = QStringLiteral(
            "blocks=%1 entered=%2 master=%3 vaxA=%4 vaxB=%5 silentSlots=%6 inputDrops=%7")
            .arg(blocks).arg(gate.entered.load()).arg(masterDelta)
            .arg(vaxADelta).arg(vaxBDelta).arg(silentDelta).arg(bridge->inputDrops());

        // The decoder is held for the whole run...
        QVERIFY2(gate.entered.load() == 1, qPrintable(evidence));
        // ...and the mix and both slices kept the worker's pace: one mixed
        // block per block fed, B contributing silence.
        QVERIFY2(masterDelta == blocks && vaxADelta == blocks && vaxBDelta == blocks,
                 qPrintable(evidence));
        // Every slot after the late bound found nothing due.
        QVERIFY2(silentDelta >= quint64(blocks - lateBound), qPrintable(evidence));

        // Let go: what it decodes now is past its slot and dropped.
        const quint64 lateBefore = bridge->lateDrops();
        gate.release();
        QVERIFY(radeB->waitRxIdleForTest(5000));
        rig.worker.processIqBatch(0, rig.iq);
        rig.worker.processIqBatch(1, rig.iq);
        QVERIFY2(bridge->lateDrops() > lateBefore,
                 qPrintable(QStringLiteral("lateDrops=%1").arg(bridge->lateDrops())));
        radeB->setRxStallHookForTest({});
    }

    // A closed slice takes its decoder; the id's next slice gets a fresh one.
    void removingARadeSliceDestroysItsDecoder()
    {
        TwoSliceRig rig;
        QVERIFY(rig.setUp());
        QVERIFY(rig.toRade(rig.sliceB));
        QCOMPARE(rig.worker.radeRxRouteCount(), 1);

        rig.radio.removeSlice(rig.b);
        QCoreApplication::processEvents();
        QVERIFY(rig.wdsp->radeChannel(rig.b) == nullptr);
        QCOMPARE(rig.worker.radeRxRouteCount(), 0);

        const int reused = rig.radio.addSlice();
        QCOMPARE(reused, rig.b);
        SliceModel* const fresh = rig.radio.sliceById(reused);
        QVERIFY(fresh && fresh != rig.sliceB);
        QTest::failOnWarning(QRegularExpression(QStringLiteral("already exists")));
        RadeChannel* const again = rig.toRade(fresh);
        QVERIFY(again && again->isActive());
        QCOMPARE(rig.worker.radeRxRouteCount(), 1);
    }

    // RADE TX stays on one channel: the TX slice's. The other RADE slice
    // hears the microphone blocks but encodes nothing, and only the keyed
    // slice's decoder stops.
    void onlyTheTxSliceChannelEncodes()
    {
        TwoSliceRig rig;
        QVERIFY(rig.setUp());
        RadeChannel* const radeA = rig.toRade(rig.sliceA);
        RadeChannel* const radeB = rig.toRade(rig.sliceB);
        QVERIFY(radeA && radeB);

        QVERIFY(rig.radio.txSliceArbiter()->requestHandoff(rig.b));
        QVERIFY(!radeA->txSelected());
        QVERIFY(radeB->txSelected());
        QVERIFY(!radeA->rxGated() && !radeB->rxGated());

        // Model signal only; no radio is attached.
        QVERIFY(QMetaObject::invokeMethod(
            rig.radio.moxController(), "moxStateChanged", Qt::DirectConnection,
            Q_ARG(bool, true)));
        QVERIFY(radeB->rxGated());
        QVERIFY(!radeA->rxGated());
        QVERIFY(QMetaObject::invokeMethod(
            rig.radio.moxController(), "moxStateChanged", Qt::DirectConnection,
            Q_ARG(bool, false)));
        QVERIFY(!radeB->rxGated());

        // An unselected channel takes the block and encodes nothing.
        QByteArray speech16k(16000 * int(sizeof(int16_t)), '\0');
        radeA->txEncode(speech16k);
        QCOMPARE(radeA->radeTxCallCountForTest(), 0);
        radeB->txEncode(speech16k);
        QVERIFY(radeB->radeTxCallCountForTest() > 0);

        QVERIFY(rig.radio.txSliceArbiter()->requestHandoff(rig.a));
        QVERIFY(radeA->txSelected());
        QVERIFY(!radeB->txSelected());
    }
};

QTEST_GUILESS_MAIN(TestRadeRxMultisliceRouting)
#include "tst_rade_rx_multislice_routing.moc"
