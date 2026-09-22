// =================================================================
// tests/tst_rade_rx_multislice_routing.cpp  (NereusSDR)
// =================================================================
//
// R-R3-31: NereusSDR-native regression for selecting one RADE RX owner in a
// two-slice worker. There is no upstream multi-slice orchestration equivalent
// to port (no port check applies).
//
// Modification history (NereusSDR):
//   2026-09-21 -- Added by J.J. Boyd (KG4VCF), with AI-assisted
//                 implementation via OpenAI Codex.
// =================================================================

#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

#include "core/AudioEngine.h"
#include "core/MoxController.h"
#include "core/RadeChannel.h"
#include "core/RxChannel.h"
#include "core/WdspEngine.h"
#include "fakes/FakeAudioBus.h"
#include "models/RadioModel.h"
#include "models/RxDspWorker.h"
#include "models/SliceModel.h"
#include "core/TxSliceArbiter.h"

#include <memory>

using namespace NereusSDR;

namespace {

class TestRadeChannel final : public RadeChannel {
public:
    using RadeChannel::RadeChannel;

    void emitLateSpeechForTest(const QByteArray& pcm)
    {
        emit rxSpeechReady(pcm);
    }
};

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

}  // namespace

class TestRadeRxMultisliceRouting : public QObject {
    Q_OBJECT

private slots:
    void selectingRadeOnBLeavesAOrdinaryAndRoutesOnlyBToRade()
    {
        RadioModel radio;
        radio.configureStreamPool(/*userDdcCount=*/2, /*maxSlices=*/2,
                                  /*defaultRateHz=*/48000);
        const int sliceAId = radio.addSlice();
        const int sliceBId = radio.addSlice();
        QCOMPARE(sliceAId, 0);
        QCOMPARE(sliceBId, 1);

        SliceModel* const sliceA = radio.sliceById(sliceAId);
        SliceModel* const sliceB = radio.sliceById(sliceBId);
        QVERIFY(sliceA);
        QVERIFY(sliceB);
        sliceA->setVaxChannel(1);
        sliceB->setVaxChannel(2);

        AudioEngine* const audio = radio.audioEngine();
        WdspEngine* const wdsp = radio.wdspEngine();
        QVERIFY(audio);
        QVERIFY(wdsp);

        // Use the repository's real, synchronous WDSP test initialization
        // seam. It skips only the minutes-long wisdom worker; channel open,
        // RxChannel::processIq and every assertion below remain production
        // code (same pattern as tst_connectable_radio_model).
        wdsp->setSynchronousInitForTest(true);
        QVERIFY(wdsp->initialize(
            QStandardPaths::writableLocation(
                QStandardPaths::AppConfigLocation)));
        QVERIFY(wdsp->isInitialized());

        BusView speakers = makeOpenBus(QStringLiteral("speakers"));
        BusView vaxA = makeOpenBus(QStringLiteral("vax-a"));
        BusView vaxB = makeOpenBus(QStringLiteral("vax-b"));
        QVERIFY(speakers.view);
        QVERIFY(vaxA.view);
        QVERIFY(vaxB.view);
        FakeAudioBus* const speakersView = speakers.view;
        FakeAudioBus* const vaxAView = vaxA.view;
        FakeAudioBus* const vaxBView = vaxB.view;
        audio->setSpeakersBusForTest(std::move(speakers.owned));
        audio->setVaxBusForTest(1, std::move(vaxA.owned));
        audio->setVaxBusForTest(2, std::move(vaxB.owned));

        constexpr int kFrames = 64;
        RxChannel* const rxA = wdsp->createRxChannel(
            sliceAId, kFrames, 4096, 48000, 48000, 48000);
        RxChannel* const rxB = wdsp->createRxChannel(
            sliceBId, kFrames, 4096, 48000, 48000, 48000);
        QVERIFY(rxA);
        QVERIFY(rxB);
        rxA->setActive(true);
        rxB->setActive(true);

        RxDspWorker worker;
        worker.setEngines(wdsp, audio);
        worker.setBufferSizes(kFrames, kFrames);
        worker.setStreamSlices(0, QVector<int>{sliceAId});
        worker.setStreamSlices(1, QVector<int>{sliceBId});
        RxDspWorker replacement;
        radio.attachDspWorkerForTest(&worker);
        DspWorkerDetach detach{&radio};

        const QVector<float> iq = makeIqBlock(kFrames);

        // Establish the real precondition from the incident: both ordinary
        // slices have produced and are members of MasterMixer's no-timeout
        // readiness barrier before B changes to RADE.
        worker.processIqBatch(0, iq);
        worker.processIqBatch(1, iq);
        QVERIFY(vaxAView->pushCount() > 0);
        QVERIFY(vaxBView->pushCount() > 0);
        QVERIFY(speakersView->pushCount() > 0);

        const int aOrdinaryBefore = vaxAView->pushCount();
        const int bOrdinaryBefore = vaxBView->pushCount();
        const int masterBefore = speakersView->pushCount();

        auto radeB = std::make_unique<TestRadeChannel>();
        QSignalSpy radeFeed(&worker, &RxDspWorker::radeIqReady);
        QVERIFY(radeFeed.isValid());
        radio.wireRadeChannel(sliceBId, radeB.get(), sliceB);
        QCoreApplication::processEvents(); // apply worker binding

        // Software-only authoritative MOX edge: AudioEngine restores its
        // captured TX slice on unkey, then RadioModel must immediately keep
        // a warming, unadmitted RADE owner withdrawn. A remains the sole
        // mixer member and advances without waiting for B.
        QVERIFY(radio.txSliceArbiter()->requestHandoff(sliceBId));
        QVERIFY(QMetaObject::invokeMethod(
            radio.moxController(), "moxStateChanged", Qt::DirectConnection,
            Q_ARG(bool, true)));
        QVERIFY(QMetaObject::invokeMethod(
            radio.moxController(), "moxStateChanged", Qt::DirectConnection,
            Q_ARG(bool, false)));
        const int masterBeforeUnkeyA = speakersView->pushCount();
        const int bBeforeUnkeyA = vaxBView->pushCount();
        worker.processIqBatch(0, iq);
        QVERIFY(speakersView->pushCount() > masterBeforeUnkeyA);
        QCOMPARE(vaxBView->pushCount(), bBeforeUnkeyA);

        // Feed enough real WDSP blocks to clear the 48 -> 24 kHz RADE
        // resampler warm-up. Keep the streams separate so the signal counts
        // prove which physical slice was selected rather than merely that a
        // RADE block appeared eventually.
        for (int i = 0; i < 96; ++i) {
            worker.processIqBatch(0, iq);
        }
        QCoreApplication::processEvents();
        const int radeAfterA = radeFeed.count();
        for (int i = 0; i < 96; ++i) {
            worker.processIqBatch(1, iq);
            QCoreApplication::processEvents();
        }
        QCoreApplication::processEvents();
        const int radeAfterB = radeFeed.count();

        const bool aStillOrdinary =
            vaxAView->pushCount() > aOrdinaryBefore;
        const bool bWithheldFromOrdinary =
            vaxBView->pushCount() == bOrdinaryBefore;
        const bool aNeverFedRade = (radeAfterA == 0);
        const bool bFedRade = (radeAfterB > radeAfterA);
        const bool masterContinued =
            speakersView->pushCount() > masterBefore;

        const QString evidence = QStringLiteral(
            "Aordinary=%1 Bordinary=%2 radeAfterA=%3 radeAfterB=%4 "
            "masterBefore=%5 masterAfter=%6")
            .arg(vaxAView->pushCount() - aOrdinaryBefore)
            .arg(vaxBView->pushCount() - bOrdinaryBefore)
            .arg(radeAfterA)
            .arg(radeAfterB)
            .arg(masterBefore)
            .arg(speakersView->pushCount());
        QVERIFY2(aStillOrdinary && bWithheldFromOrdinary
                     && aNeverFedRade && bFedRade && masterContinued,
                 qPrintable(evidence));

        // Now activate the real codec. Unsynchronised input must produce
        // source-authoritative quiet padding at input cadence; RadioModel
        // re-admits B only when that current block exists, and A+B then keep
        // advancing the no-timeout MasterMixer together.
        QSignalSpy speech(radeB.get(), &RadeChannel::rxSpeechReady);
        QVERIFY(speech.isValid());
        QVERIFY(radeB->start(QStringLiteral("dummy")));
        const int masterBeforePadding = speakersView->pushCount();
        const int bBeforePadding = vaxBView->pushCount();
        for (int i = 0; i < 96; ++i) {
            worker.processIqBatch(0, iq);
            worker.processIqBatch(1, iq);
            QCoreApplication::processEvents();
        }
        QVERIFY(!radeB->isSynced());
        QVERIFY(speech.count() > 0);
        QVERIFY(vaxBView->pushCount() > bBeforePadding);
        QVERIFY(speakersView->pushCount() > masterBeforePadding);

        // Same RadeChannel, new DSP worker: leave one old-worker IQ event
        // queued at RadioModel, replace the worker, then drain the event.
        // The old generation must not run the codec or re-admit B. This is
        // the production boundary that also rejects an old decode submitted
        // before a reconnect from completing into the replacement epoch.
        const int speechBeforeReplacement = speech.count();
        const int oldFeedBefore = radeFeed.count();
        for (int i = 0; i < 8 && radeFeed.count() == oldFeedBefore; ++i) {
            worker.processIqBatch(1, iq);
        }
        QVERIFY(radeFeed.count() > oldFeedBefore);

        replacement.setEngines(wdsp, audio);
        replacement.setBufferSizes(kFrames, kFrames);
        replacement.setStreamSlices(0, QVector<int>{sliceAId});
        replacement.setStreamSlices(1, QVector<int>{sliceBId});
        radio.attachDspWorkerForTest(&replacement);
        QCoreApplication::processEvents();
        QCOMPARE(speech.count(), speechBeforeReplacement);

        // Model an asynchronous completion from work submitted in the old
        // worker epoch. It has the same live channel and slice identities,
        // so only the codec-input generation guard can reject it.
        const int bBeforeLateSpeech = vaxBView->pushCount();
        const QByteArray staleSpeech(1024 * 2 * sizeof(float), '\0');
        for (int i = 0; i < 8; ++i) {
            radeB->emitLateSpeechForTest(staleSpeech);
        }
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        QCOMPARE(vaxBView->pushCount(), bBeforeLateSpeech);

        const int speechBeforeReplacementOutput = speech.count();
        const int bBeforeReplacementOutput = vaxBView->pushCount();
        const int masterBeforeReplacementOutput = speakersView->pushCount();
        // attachRadeRxWorker intentionally resets both the replacement's
        // 48->24 input resampler and RadioModel's 24->48 speech resamplers.
        // The focused tst_audio_engine_rade run established that 96 blocks
        // (~3,040 returned 24-kHz frames) can remain inside r8brain's fresh
        // high-attenuation filter, while the bounded 256-block window clears
        // it. Keep all three acceptance conditions and stop as soon as the
        // replacement has proved codec, VAX and shared-master delivery.
        constexpr int kReplacementWarmupBlocks = 256;
        int replacementBlocks = 0;
        for (; replacementBlocks < kReplacementWarmupBlocks
               && !(speech.count() > speechBeforeReplacementOutput
                    && vaxBView->pushCount() > bBeforeReplacementOutput
                    && speakersView->pushCount()
                           > masterBeforeReplacementOutput);
             ++replacementBlocks) {
            replacement.processIqBatch(0, iq);
            replacement.processIqBatch(1, iq);
            QCoreApplication::processEvents();
        }
        const QString replacementEvidence = QStringLiteral(
            "blocks=%1 speechDelta=%2 vaxBDelta=%3 masterDelta=%4")
            .arg(replacementBlocks)
            .arg(speech.count() - speechBeforeReplacementOutput)
            .arg(vaxBView->pushCount() - bBeforeReplacementOutput)
            .arg(speakersView->pushCount() - masterBeforeReplacementOutput);
        QVERIFY2(speech.count() > speechBeforeReplacementOutput
                     && vaxBView->pushCount() > bBeforeReplacementOutput
                     && speakersView->pushCount()
                            > masterBeforeReplacementOutput,
                 qPrintable(replacementEvidence));

        // Replace the target on the same slice. Destroying the old QObject
        // afterward must not clear the replacement's owner serial.
        auto newer = std::make_unique<RadeChannel>();
        QVERIFY(newer->start(QStringLiteral("dummy")));
        QSignalSpy newerSpeech(newer.get(), &RadeChannel::rxSpeechReady);
        QVERIFY(newerSpeech.isValid());
        radio.wireRadeChannel(sliceBId, newer.get(), sliceB);
        QCoreApplication::processEvents();
        radeB.reset();
        QCoreApplication::processEvents();
        for (int i = 0; i < 96; ++i) {
            replacement.processIqBatch(1, iq);
            QCoreApplication::processEvents();
        }
        QVERIFY(newerSpeech.count() > 0);

        // Destroying the current target crosses the worker clear boundary
        // before restoring ordinary B membership. A reused/stale RADE block
        // therefore cannot enroll B; its next real WDSP block does.
        newer.reset();
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        const int bBeforeClear = vaxBView->pushCount();
        replacement.processIqBatch(1, iq);
        QVERIFY(vaxBView->pushCount() > bBeforeClear);

        // A removed slice can release its numeric ID before deleteLater runs.
        // A late event from that slice's still-live channel must not target
        // the new QObject that receives the same ID.
        auto staleTarget = std::make_unique<TestRadeChannel>();
        QVERIFY(staleTarget->start(QStringLiteral("dummy")));
        radio.wireRadeChannel(sliceBId, staleTarget.get(), sliceB);
        QCoreApplication::processEvents();
        radio.removeSlice(sliceBId);
        QCoreApplication::processEvents();
        const int reusedId = radio.addSlice();
        QCOMPARE(reusedId, sliceBId);
        SliceModel* const replacementSlice = radio.sliceById(reusedId);
        QVERIFY(replacementSlice);
        QVERIFY(replacementSlice != sliceB);
        replacementSlice->setVaxChannel(2);
        const int bBeforeReusedEvent = vaxBView->pushCount();
        const QByteArray reusedStaleSpeech(1024 * 2 * sizeof(float), '\0');
        for (int i = 0; i < 8; ++i) {
            staleTarget->emitLateSpeechForTest(reusedStaleSpeech);
        }
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        QCOMPARE(vaxBView->pushCount(), bBeforeReusedEvent);
    }
};

QTEST_GUILESS_MAIN(TestRadeRxMultisliceRouting)
#include "tst_rade_rx_multislice_routing.moc"
