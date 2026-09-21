// no-port-check: NereusSDR-original test coverage for the bounded R3 audio
// source bridge.

#include <QtTest/QtTest>
#include <QSemaphore>

#include "core/AudioEngine.h"
#include "core/session/media/DaemonAudioSource.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <atomic>
#include <cmath>
#include <thread>

using namespace NereusSDR;

namespace {

constexpr int kDspFrames = 64;

QVector<float> stereoBlock(float left, float right)
{
    QVector<float> block(kDspFrames * 2);
    for (int frame = 0; frame < kDspFrames; ++frame) {
        block[frame * 2] = left;
        block[frame * 2 + 1] = right;
    }
    return block;
}

struct Harness {
    RadioModel radio;
    AudioEngine* engine = nullptr;
    int sliceA = -1;
    int sliceB = -1;

    Harness()
    {
        radio.configureStreamPool(/*userDdcCount=*/5, /*maxSlices=*/5,
                                  /*defaultRateHz=*/192000);
        engine = radio.audioEngine();
        Q_ASSERT(engine != nullptr);
        // These tests assert bridge geometry and sums, not the established
        // anti-click fade, so make mixer gain deterministic from sample one.
        engine->masterMixForTest().setRampFrames(1);
        engine->masterMixForTest().setSlewUpFrames(0);
        sliceA = radio.addSlice();
        sliceB = radio.addSlice();
        Q_ASSERT(sliceA >= 0 && sliceB >= 0);
        // Both slices share a DDC in this fixture, so state their mixer
        // membership directly rather than relying on receiver activation.
        engine->setSliceStreaming(sliceA, true);
        engine->setSliceStreaming(sliceB, true);
    }
};

void feedFrames(AudioEngine* engine, int sliceId, int frames,
                float left, float right)
{
    const QVector<float> block = stereoBlock(left, right);
    for (int delivered = 0; delivered < frames; delivered += kDspFrames) {
        engine->rxBlockReady(sliceId, block.constData(), kDspFrames);
    }
}

void feedMixedBlock(Harness& harness, float aLeft, float aRight,
                    float bLeft, float bRight)
{
    const QVector<float> a = stereoBlock(aLeft, aRight);
    const QVector<float> b = stereoBlock(bLeft, bRight);
    for (int delivered = 0; delivered < DaemonAudioSource::kBlockFrames;
         delivered += kDspFrames) {
        harness.engine->rxBlockReady(harness.sliceA, a.constData(), kDspFrames);
        harness.engine->rxBlockReady(harness.sliceB, b.constData(), kDspFrames);
    }
}

void primeMixedBarrier(Harness& harness, float aLeft, float aRight,
                       float bLeft, float bRight)
{
    // The master barrier's first A release may be A-only; leave an actual B
    // block queued while capture is stopped so the first captured A release
    // has both programs. This mirrors the steady state without hiding the
    // mixer's startup boundary.
    const QVector<float> a = stereoBlock(aLeft, aRight);
    const QVector<float> b = stereoBlock(bLeft, bRight);
    harness.engine->rxBlockReady(harness.sliceA, a.constData(), kDspFrames);
    harness.engine->rxBlockReady(harness.sliceB, b.constData(), kDspFrames);
    harness.engine->rxBlockReady(harness.sliceA, a.constData(), kDspFrames);
    harness.engine->rxBlockReady(harness.sliceB, b.constData(), kDspFrames);
}

void verifyStereoConstant(const DaemonAudioBlock& block, float left, float right)
{
    QCOMPARE(block.pcmInterleaved.size(), DaemonAudioSource::kBlockSamples);
    for (int sample = 0; sample < block.pcmInterleaved.size(); sample += 2) {
        if (std::abs(block.pcmInterleaved[sample] - left) >= 0.00001f
            || std::abs(block.pcmInterleaved[sample + 1] - right) >= 0.00001f) {
            QFAIL(qPrintable(QStringLiteral("stereo mismatch at frame %1: got (%2, %3), expected (%4, %5)")
                                  .arg(sample / 2)
                                  .arg(block.pcmInterleaved[sample], 0, 'g', 8)
                                  .arg(block.pcmInterleaved[sample + 1], 0, 'g', 8)
                                  .arg(left, 0, 'g', 8)
                                  .arg(right, 0, 'g', 8)));
        }
    }
}

class BlockingTap final : public MasterMixAudioTap {
public:
    void consume(const float*, int, int) noexcept override
    {
        entered.release();
        release.acquire();
    }

    QSemaphore entered;
    QSemaphore release;
};

} // namespace

class TstDaemonAudioSource : public QObject {
    Q_OBJECT

private slots:
    void capturesTheBarrierMixedStereoBeforeLocalMasterControls()
    {
        Harness harness;
        DaemonAudioSource source;
        source.setAudioEngine(harness.engine);
        primeMixedBarrier(harness, 0.20f, -0.25f, 0.30f, 0.50f);
        source.start();

        // R-R3-06: the source is upstream of local speaker volume/mute.
        harness.engine->setVolume(0.0f);
        harness.engine->setMasterMuted(true);
        feedMixedBlock(harness, 0.20f, -0.25f, 0.30f, 0.50f);

        const auto block = source.takeBlock();
        QVERIFY(block.has_value());
        QCOMPARE(block->samplePosition, quint64{0});
        verifyStereoConstant(*block, 0.50f, 0.25f);
        QVERIFY(!source.takeBlock().has_value());
        QCOMPARE(source.dropCount(), std::uint64_t{0});
    }

    void preservesPerSliceMuteInTheMixedProgram()
    {
        Harness harness;
        DaemonAudioSource source;
        source.setAudioEngine(harness.engine);
        primeMixedBarrier(harness, 0.20f, -0.25f, 0.30f, 0.50f);
        source.start();

        feedMixedBlock(harness, 0.20f, -0.25f, 0.30f, 0.50f);
        const auto beforeMute = source.takeBlock();
        QVERIFY(beforeMute.has_value());
        QCOMPARE(beforeMute->samplePosition, quint64{0});
        verifyStereoConstant(*beforeMute, 0.50f, 0.25f);

        // The just-finished period has one pre-mute B block queued. Drain it
        // while stopped, then queue one muted B block before a fresh source
        // epoch so the asserted capture begins at the mute boundary.
        source.stop();
        harness.radio.sliceById(harness.sliceB)->setMuted(true);
        const QVector<float> a = stereoBlock(0.20f, -0.25f);
        const QVector<float> b = stereoBlock(0.30f, 0.50f);
        harness.engine->rxBlockReady(harness.sliceA, a.constData(), kDspFrames);
        harness.engine->rxBlockReady(harness.sliceB, b.constData(), kDspFrames);
        source.start();
        feedMixedBlock(harness, 0.20f, -0.25f, 0.30f, 0.50f);
        const auto afterMute = source.takeBlock();
        QVERIFY(afterMute.has_value());
        QCOMPARE(afterMute->samplePosition, quint64{0});
        verifyStereoConstant(*afterMute, 0.20f, -0.25f);
    }

    void assemblesExactFortyMillisecondBlocks()
    {
        Harness harness;
        DaemonAudioSource source;
        source.setAudioEngine(harness.engine);
        source.start();

        // A single streaming slice produces one mixer release for each 64
        // frame DSP period.  The source must not expose a partial Opus frame.
        harness.engine->setSliceStreaming(harness.sliceB, false);
        feedFrames(harness.engine, harness.sliceA,
                   DaemonAudioSource::kBlockFrames - kDspFrames,
                   -0.40f, 0.70f);
        QVERIFY(!source.takeBlock().has_value());

        feedFrames(harness.engine, harness.sliceA, kDspFrames, -0.40f, 0.70f);
        const auto block = source.takeBlock();
        QVERIFY(block.has_value());
        QCOMPARE(block->samplePosition, quint64{0});
        verifyStereoConstant(*block, -0.40f, 0.70f);
    }

    void boundedQueueDropsNewestCompletedBlock()
    {
        Harness harness;
        DaemonAudioSource source;
        source.setAudioEngine(harness.engine);
        source.start();
        harness.engine->setSliceStreaming(harness.sliceB, false);

        for (int marker = 1; marker <= DaemonAudioSource::kQueueBlocks + 1; ++marker) {
            feedFrames(harness.engine, harness.sliceA,
                       DaemonAudioSource::kBlockFrames,
                       static_cast<float>(marker), static_cast<float>(-marker));
        }

        QCOMPARE(source.dropCount(), std::uint64_t{1});
        for (int marker = 1; marker <= DaemonAudioSource::kQueueBlocks; ++marker) {
            const auto block = source.takeBlock();
            QVERIFY(block.has_value());
            QCOMPARE(block->samplePosition,
                     static_cast<quint64>(marker - 1)
                         * DaemonAudioSource::kBlockFrames);
            verifyStereoConstant(*block, static_cast<float>(marker),
                                 static_cast<float>(-marker));
        }
        QVERIFY(!source.takeBlock().has_value());

        // The discarded fifth block still consumes 1,920 source frames. The
        // next retained block exposes its true position rather than closing
        // that loss gap in the RTP clock.
        feedFrames(harness.engine, harness.sliceA,
                   DaemonAudioSource::kBlockFrames, 6.0f, -6.0f);
        const auto afterLoss = source.takeBlock();
        QVERIFY(afterLoss.has_value());
        QCOMPARE(afterLoss->samplePosition,
                 quint64{DaemonAudioSource::kQueueBlocks + 1}
                     * DaemonAudioSource::kBlockFrames);
        verifyStereoConstant(*afterLoss, 6.0f, -6.0f);
    }

    void stopAndRestartDiscardOldAndStoppedAudio()
    {
        Harness harness;
        DaemonAudioSource source;
        source.setAudioEngine(harness.engine);
        source.start();
        harness.engine->setSliceStreaming(harness.sliceB, false);

        feedFrames(harness.engine, harness.sliceA,
                   DaemonAudioSource::kBlockFrames, 1.0f, -1.0f);
        source.stop();
        QVERIFY(!source.takeBlock().has_value());

        // An engine callback after stop has no installed bridge and cannot
        // leave a block to be picked up after the next session starts.
        feedFrames(harness.engine, harness.sliceA,
                   DaemonAudioSource::kBlockFrames, 2.0f, -2.0f);
        source.start();
        feedFrames(harness.engine, harness.sliceA,
                   DaemonAudioSource::kBlockFrames, 3.0f, -3.0f);

        const auto block = source.takeBlock();
        QVERIFY(block.has_value());
        QCOMPARE(block->samplePosition, quint64{0});
        verifyStereoConstant(*block, 3.0f, -3.0f);
        QCOMPARE(source.dropCount(), std::uint64_t{0});
    }

    void tapRetirementWaitsForAnAdmittedDspCallback()
    {
        Harness harness;
        harness.engine->setSliceStreaming(harness.sliceB, false);
        BlockingTap tap;
        harness.engine->setMasterMixAudioTap(&tap);

        const QVector<float> samples = stereoBlock(0.25f, -0.25f);
        std::thread producer([&] {
            harness.engine->rxBlockReady(harness.sliceA, samples.constData(), kDspFrames);
        });
        QVERIFY2(tap.entered.tryAcquire(1, 1000),
                 "the DSP callback did not enter the installed tap");

        std::atomic<bool> clearReturned{false};
        std::thread retirement([&] {
            harness.engine->clearMasterMixAudioTap(&tap);
            clearReturned.store(true, std::memory_order_release);
        });

        QTest::qWait(25);
        QVERIFY(!clearReturned.load(std::memory_order_acquire));
        tap.release.release();
        producer.join();
        retirement.join();
        QVERIFY(clearReturned.load(std::memory_order_acquire));
    }
};

QTEST_MAIN(TstDaemonAudioSource)
#include "tst_daemon_audio_source.moc"
