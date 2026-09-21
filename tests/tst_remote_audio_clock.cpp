// =================================================================
// tests/tst_remote_audio_clock.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original deterministic acceptance test. It drives
// the already-attributed RemoteAudioRateMatcher through independent logical
// producer and consumer clocks; no device, timer, transport, or source port
// is involved.
// =================================================================

#include <QtTest>

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/session/media/RemoteAudioRateMatcher.h"

using namespace NereusSDR;

namespace {

constexpr int kInputFrames = 1'920;
constexpr int kOutputFrames = 480;
constexpr int kRingFrames = 8'640;
constexpr int kSimulatedSeconds = 60 * 60;
constexpr qint64 kPostStartupTicks = 5'000'000;
constexpr double kToneAmplitude = 0.20;
constexpr double kLeftHz = 701.0;
constexpr double kRightHz = 1703.0;

QVector<float> stereoBlock(quint64 firstFrame)
{
    QVector<float> pcm(kInputFrames * RemoteAudioRateMatcher::kChannels);
    for (int frame = 0; frame < kInputFrames; ++frame) {
        const double seconds = static_cast<double>(firstFrame + frame)
            / RemoteAudioRateMatcher::kSampleRateHz;
        pcm[frame * 2] = static_cast<float>(kToneAmplitude
            * std::sin(2.0 * M_PI * kLeftHz * seconds));
        pcm[frame * 2 + 1] = static_cast<float>(kToneAmplitude
            * std::sin(2.0 * M_PI * kRightHz * seconds));
    }
    return pcm;
}

struct ClockRun {
    RemoteAudioRateMatcherStats stats;
    double maximumRatioDeviation = 0.0;
    double maximumPostStartupAdjacentDelta = 0.0;
    double leftSquareSum = 0.0;
    double rightSquareSum = 0.0;
    double crossProductSum = 0.0;
    quint64 postStartupFrames = 0;
    bool valid = true;
};

ClockRun runOneHourAtPpm(int ppm)
{
    RemoteAudioRateMatcher matcher;
    if (!matcher.configure(kInputFrames, kOutputFrames, kRingFrames)) {
        return {.valid = false};
    }

    // Integer phase accumulators keep both clocks deterministic. The network
    // producer is exactly 48 kHz; consumer is 48 kHz * (1 + ppm / 1e6).
    const qint64 ticksPerSecond = 1'000'000;
    const qint64 producerPeriodTicks = 40'000;
    const qint64 consumerPeriodNumerator = 10'000'000'000LL;
    const qint64 consumerRateDenominator = ticksPerSecond + ppm;
    const qint64 endTicks = static_cast<qint64>(kSimulatedSeconds) * ticksPerSecond;

    qint64 nextProducer = 0;
    qint64 nextConsumerNumerator = 0;
    quint64 sourceFrame = 0;
    bool hasPrevious = false;
    float previousLeft = 0.0f;
    float previousRight = 0.0f;
    qint64 nextDiagnosticTicks = ticksPerSecond;
    ClockRun run;

    while (nextProducer < endTicks
           || nextConsumerNumerator / consumerRateDenominator < endTicks) {
        const qint64 nextConsumer = nextConsumerNumerator / consumerRateDenominator;
        if (nextProducer < endTicks && nextProducer <= nextConsumer) {
            if (!matcher.push(stereoBlock(sourceFrame))) {
                run.valid = false;
                break;
            }
            sourceFrame += kInputFrames;
            nextProducer += producerPeriodTicks;
            if (nextProducer >= nextDiagnosticTicks) {
                run.maximumRatioDeviation = std::max(run.maximumRatioDeviation,
                    std::abs(matcher.stats().currentRatio - 1.0));
                nextDiagnosticTicks += ticksPerSecond;
            }
            continue;
        }

        const QVector<float> output = matcher.take();
        if (output.size() != kOutputFrames * RemoteAudioRateMatcher::kChannels) {
            run.valid = false;
            break;
        }
        for (int sample = 0; sample < output.size(); sample += 2) {
            if (!std::isfinite(output.at(sample)) || !std::isfinite(output.at(sample + 1))) {
                run.valid = false;
                break;
            }
            const bool postStartup = nextConsumer > kPostStartupTicks;
            if (postStartup) {
                const double left = output.at(sample);
                const double right = output.at(sample + 1);
                if (hasPrevious) {
                    run.maximumPostStartupAdjacentDelta = std::max(
                        run.maximumPostStartupAdjacentDelta,
                        std::abs(left - static_cast<double>(previousLeft)));
                    run.maximumPostStartupAdjacentDelta = std::max(
                        run.maximumPostStartupAdjacentDelta,
                        std::abs(right - static_cast<double>(previousRight)));
                }
                run.leftSquareSum += left * left;
                run.rightSquareSum += right * right;
                run.crossProductSum += left * right;
                ++run.postStartupFrames;
            }
            previousLeft = output.at(sample);
            previousRight = output.at(sample + 1);
            hasPrevious = true;
        }
        if (!run.valid) {
            break;
        }
        nextConsumerNumerator += consumerPeriodNumerator;
        if (nextConsumer >= nextDiagnosticTicks) {
            run.maximumRatioDeviation = std::max(run.maximumRatioDeviation,
                std::abs(matcher.stats().currentRatio - 1.0));
            nextDiagnosticTicks += ticksPerSecond;
        }
    }

    run.stats = matcher.stats();
    return run;
}

} // namespace

class TstRemoteAudioClock : public QObject
{
    Q_OBJECT

private slots:
    void rejectsUndersizedRing()
    {
        RemoteAudioRateMatcher matcher;
        QVERIFY(!matcher.configure(kInputFrames, kOutputFrames, 4'032));
        QVERIFY(matcher.configure(kInputFrames, kOutputFrames, kRingFrames));
    }

    void rejectsMalformedPublicInput()
    {
        RemoteAudioRateMatcher matcher;
        QVERIFY(!matcher.configure(RemoteAudioRateMatcher::kMaxFramesPerCall + 1,
                                   kOutputFrames, kRingFrames));
        QVERIFY(!matcher.configure(kInputFrames,
                                   RemoteAudioRateMatcher::kMaxFramesPerCall + 1,
                                   kRingFrames));
        QVERIFY(!matcher.configure(kInputFrames, kOutputFrames,
                                   RemoteAudioRateMatcher::kMaxRingFrames + 1));
        QVERIFY(matcher.configure(kInputFrames, kOutputFrames, kRingFrames));

        QVector<float> malformed(kInputFrames * RemoteAudioRateMatcher::kChannels, 0.0f);
        malformed[0] = std::numeric_limits<float>::quiet_NaN();
        QVERIFY(!matcher.push(malformed));
        malformed[0] = std::numeric_limits<float>::infinity();
        QVERIFY(!matcher.push(malformed));
    }

    void resetRecreatesFreshConfiguration()
    {
        RemoteAudioRateMatcher matcher;
        QVERIFY(matcher.configure(kInputFrames, kOutputFrames, kRingFrames));
        QVERIFY(matcher.push(stereoBlock(0)));
        QCOMPARE(matcher.take().size(), kOutputFrames * RemoteAudioRateMatcher::kChannels);
        matcher.reset();

        const RemoteAudioRateMatcherStats stats = matcher.stats();
        QCOMPARE(stats.underflows, 0);
        QCOMPARE(stats.overflows, 0);
        QCOMPARE(stats.ringCapacityFrames, kRingFrames);
        QCOMPARE(matcher.take().size(), kOutputFrames * RemoteAudioRateMatcher::kChannels);
    }

    void carriesArbitraryPublicDimensionsThroughNativeBlocks()
    {
        RemoteAudioRateMatcher matcher;
        constexpr int inputFrames = 65;
        constexpr int outputFrames = 63;
        QVERIFY(matcher.configure(inputFrames, outputFrames, 640));
        QVector<float> pcm(inputFrames * RemoteAudioRateMatcher::kChannels, 0.0f);
        for (int frame = 0; frame < inputFrames; ++frame) {
            pcm[frame * 2] = 0.2f;
            pcm[frame * 2 + 1] = -0.2f;
        }
        for (int call = 0; call < 130; ++call) {
            QVERIFY(matcher.push(pcm));
            const QVector<float> output = matcher.take();
            QCOMPARE(output.size(), outputFrames * RemoteAudioRateMatcher::kChannels);
            for (float sample : output) {
                QVERIFY(std::isfinite(sample));
            }
        }
        const RemoteAudioRateMatcherStats stats = matcher.stats();
        QCOMPARE(stats.underflows, 0);
        QCOMPARE(stats.overflows, 0);
    }

    void reportsUnconsumedNativeOutputCarryInFill()
    {
        RemoteAudioRateMatcher matcher;
        QVERIFY(matcher.configure(64, 63, 640));
        const RemoteAudioRateMatcherStats before = matcher.stats();
        QCOMPARE(matcher.take().size(), 63 * RemoteAudioRateMatcher::kChannels);
        const RemoteAudioRateMatcherStats after = matcher.stats();
        QCOMPARE(after.ringFillFrames, before.ringFillFrames - 63);
        QCOMPARE(after.underflows, 0);
    }

    void adaptiveClockStaysBoundedAtPlusAndMinus500Ppm()
    {
#ifndef HAVE_WDSP
        QSKIP("WDSP is disabled");
#else
        for (const int ppm : {-500, 500}) {
            const ClockRun run = runOneHourAtPpm(ppm);
            QVERIFY(run.valid);
            QCOMPARE(run.stats.underflows, 0);
            QCOMPARE(run.stats.overflows, 0);
            QCOMPARE(run.stats.ringCapacityFrames, kRingFrames);
            QVERIFY(run.stats.ringFillFrames >= 0);
            QVERIFY(run.stats.ringFillFrames <= run.stats.ringCapacityFrames);
            QVERIFY(run.postStartupFrames > 0);

            // The staged 64-frame calls expose WDSP's native call cadence.
            // Its instantaneous ratio follows the two carry phases, so its
            // final sign is not a reliable clock-direction measurement. The
            // nonzero observed deviation verifies that force == 0 left the
            // existing feedback active; zero under/overflows above verify it
            // absorbed both independent clock drifts without repair.
            QVERIFY(run.maximumRatioDeviation > 0.00001);

            const double leftRms = std::sqrt(run.leftSquareSum / run.postStartupFrames);
            const double rightRms = std::sqrt(run.rightSquareSum / run.postStartupFrames);
            const double normalizedCrossCorrelation = run.crossProductSum
                / std::sqrt(run.leftSquareSum * run.rightSquareSum);
            QVERIFY(leftRms > 0.05 && leftRms < 0.30);
            QVERIFY(rightRms > 0.05 && rightRms < 0.30);
            QVERIFY(std::abs(normalizedCrossCorrelation) < 0.10);

            // The 1703 Hz, 0.2-amplitude channel has the largest normal
            // sample-to-sample slope: 2A sin(pi*f/48000) is about 0.0445.
            // 0.08 permits normal filter interpolation while rejecting a
            // post-startup discontinuity or periodic repair splice.
            QVERIFY(run.maximumPostStartupAdjacentDelta < 0.08);
        }
#endif
    }
};

QTEST_GUILESS_MAIN(TstRemoteAudioClock)
#include "tst_remote_audio_clock.moc"
