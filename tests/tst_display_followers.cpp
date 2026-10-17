// no-port-check: NereusSDR-original. Tests of the display computations
// moved into src/core/spectrum/DisplayFollowers (iPhone app Task 20,
// R-IOS-27); the logic under test is the Thetis port that file carries.
// =================================================================
// tests/tst_display_followers.cpp  (NereusSDR)
// =================================================================
//
// The noise floor, the waterfall's automatic levels, the normalise shift,
// the averaging constant, the calibration offset's range and the peak-blob
// passband, as the desktop's SpectrumWidget ran them before the move and
// as the Core now runs them for an app. Each expectation is worked from
// the formula by hand, not by calling a second copy of it.
//
// =================================================================

#include <QtTest>

#include "core/ControlRanges.h"
#include "core/spectrum/DisplayFollowers.h"
#include "core/spectrum/MoxDisplayHold.h"

#include <cmath>
#include <limits>
#include <optional>

using namespace NereusSDR;

namespace {

QVector<float> flatRow(int width, float dbm)
{
    return QVector<float>(width, dbm);
}

} // namespace

class TstDisplayFollowers : public QObject {
    Q_OBJECT
private slots:
    // ── Averaging constant ──────────────────────────────────────────────

    void averageAlphaIsExpOfMinusOneOverFramesTimesTau()
    {
        // 30 ms at 30 frames a second: exp(-1 / (30 * 0.030)) = exp(-1/0.9).
        QCOMPARE(averageAlphaForTimeMs(30, 30),
                 static_cast<float>(std::exp(-1.0 / 0.9)));
        // 120 ms at 60 fps: exp(-1 / 7.2).
        QCOMPARE(averageAlphaForTimeMs(120, 60),
                 static_cast<float>(std::exp(-1.0 / 7.2)));
        // A time under 1 ms counts as 1 ms.
        QCOMPARE(averageAlphaForTimeMs(0, 30), averageAlphaForTimeMs(1, 30));
        QCOMPARE(averageAlphaForTimeMs(-50, 30), averageAlphaForTimeMs(1, 30));
    }

    void averageTimeClampsToTheSetupRange()
    {
        QCOMPARE(clampAverageTimeMs(-3), 1);
        QCOMPARE(clampAverageTimeMs(0), 1);
        QCOMPARE(clampAverageTimeMs(1), 1);
        QCOMPARE(clampAverageTimeMs(5), 5);
        QCOMPARE(clampAverageTimeMs(10), 10);
        QCOMPARE(clampAverageTimeMs(500), 500);
        QCOMPARE(clampAverageTimeMs(20000), 9999);
        // The same range as the desktop's controls.
        QCOMPARE(kAverageTimeMinMs, ControlRanges::kDisplayAvgTimeMinMs);
        QCOMPARE(kAverageTimeMaxMs, ControlRanges::kDisplayAvgTimeMaxMs);
        // A display request is taken over the same range
        // (tst_display_extras: averageTimesAreTakenFromOneMs).
        QCOMPARE(kAverageTimeMinMs, 1);
    }

    // The floor of 1 ms: the constant is a number between 0 and 1 at every
    // frame rate on offer, next to no averaging, and the times just above
    // it average more, in order.
    void averageAlphaAtOneMsIsAlmostNoAveraging()
    {
        for (const int fps : {1, 10, 30, 60, 144}) {
            const float one = averageAlphaForTimeMs(1, fps);
            QVERIFY2(std::isfinite(one), qPrintable(QString::number(fps)));
            QVERIFY(one >= 0.0f);
            QVERIFY(one < 0.01f);
            QCOMPARE(one, static_cast<float>(std::exp(-1.0 / (fps * 0.001))));
            float previous = one;
            for (const int ms : {2, 5, 9, 10, 30}) {
                const float alpha = averageAlphaForTimeMs(ms, fps);
                QVERIFY(std::isfinite(alpha));
                QVERIFY(alpha >= previous);
                QVERIFY(alpha <= 1.0f);
                previous = alpha;
            }
        }
        // 144 fps keeps the constant above zero, so the order is strict
        // there: 1 ms averages less than 10 ms.
        QVERIFY(averageAlphaForTimeMs(1, 144) < averageAlphaForTimeMs(10, 144));
    }

    // ── Normalise and calibration ───────────────────────────────────────

    void normalizeShiftIsMinusTenLogBinWidth()
    {
        QCOMPARE(normalizeShiftDb(false, 100.0), 0.0f);
        QCOMPARE(normalizeShiftDb(true, 100.0), -20.0f);
        QCOMPARE(normalizeShiftDb(true, 1.0), 0.0f);
        QCOMPARE(normalizeShiftDb(true, 0.0), 0.0f);
        QCOMPARE(normalizeShiftDb(true, -5.0), 0.0f);
        // 192 kHz over 4096 bins: 46.875 Hz a bin.
        QCOMPARE(normalizeShiftDb(true, 192000.0 / 4096.0),
                 -10.0f * std::log10(46.875f));
    }

    void calibrationOffsetClampsToThirtyDb()
    {
        QCOMPARE(clampCalibrationOffsetDb(-45.0f), -30.0f);
        QCOMPARE(clampCalibrationOffsetDb(12.5f), 12.5f);
        QCOMPARE(clampCalibrationOffsetDb(31.0f), 30.0f);
    }

    // ── Passband ────────────────────────────────────────────────────────

    void passbandPixelsFloorTheLowEdgeAndCeilTheHigh()
    {
        // 100 pixels over 0 +/- 50 Hz: 1 Hz a pixel, left edge -50 Hz.
        auto [lo, hi] = passbandPixels(100, 0.0, 100.0, -10.5, 10.5);
        QCOMPARE(lo, 39);   // floor(39.5)
        QCOMPARE(hi, 61);   // ceil(60.5)
        // Clamped to the row.
        std::tie(lo, hi) = passbandPixels(100, 0.0, 100.0, -500.0, 500.0);
        QCOMPARE(lo, 0);
        QCOMPARE(hi, 99);
        // No span: the whole row.
        std::tie(lo, hi) = passbandPixels(100, 0.0, 0.0, -10.0, 10.0);
        QCOMPARE(lo, 0);
        QCOMPARE(hi, 99);
    }

    // ── Noise floor ─────────────────────────────────────────────────────

    void noiseFloorShiftClampsToTwelveDb()
    {
        QCOMPARE(NoiseFloorFollower::clampShiftDb(-20.0f), -12.0f);
        QCOMPARE(NoiseFloorFollower::clampShiftDb(3.0f), 3.0f);
        QCOMPARE(NoiseFloorFollower::clampShiftDb(13.0f), 12.0f);
    }

    void firstFrameDriftsUpOneDbAndTheLineLags()
    {
        NoiseFloorFollower nf;
        QCOMPARE(nf.fftBinAverage(), -200.0f);
        QCOMPARE(nf.lerpAverage(), -200.0f);
        // No pixel is below -200, so the estimate drifts up 1 dB.
        QVERIFY(!nf.process(flatRow(100, -120.0f), 30, 0));
        QCOMPARE(nf.fftBinAverage(), -199.0f);
        // framesInAttack = int(30 / 1000 * 2000) + 1 = 61.
        QCOMPARE(nf.lerpAverage(), -200.0f + 1.0f / 61.0f);
    }

    void quietPixelsBlendInTheLinearDomain()
    {
        NoiseFloorFollower nf;
        // Drift the estimate above the row: 81 frames reach -119 dBm.
        for (int frame = 0; frame < 81; ++frame) {
            nf.process(flatRow(100, -120.0f), 30, 0);
        }
        QCOMPARE(nf.fftBinAverage(), -119.0f);
        // Every pixel (-120) is now below it: the average of -120 and -119
        // taken in power, 10*log10((10^-12 + 10^-11.9) / 2).
        nf.process(flatRow(100, -120.0f), 30, 0);
        const double expected = 10.0 * std::log10(
            (std::pow(10.0, -12.0) + static_cast<double>(std::pow(10.0f, -11.9f))) * 0.5);
        QVERIFY(std::abs(nf.fftBinAverage() - static_cast<float>(expected)) < 1.0e-3f);
    }

    void tooFewQuietPixelsKeepsDrifting()
    {
        NoiseFloorFollower nf;
        for (int frame = 0; frame < 81; ++frame) {
            nf.process(flatRow(100, -120.0f), 30, 0);
        }
        // 14 quiet pixels of 100 is under the 15 that sensitivity 3 needs.
        QVector<float> row = flatRow(100, -100.0f);
        for (int i = 0; i < 14; ++i) { row[i] = -130.0f; }
        nf.process(row, 30, 0);
        QCOMPARE(nf.fftBinAverage(), -118.0f);
    }

    void fastAttackDriftsThreeDbAndTheLineFollowsAtOnce()
    {
        NoiseFloorFollower nf;
        QVERIFY(nf.setFastAttack(true, 1000));
        QVERIFY(!nf.setFastAttack(true, 1000));
        QVERIFY(nf.fastAttack());
        nf.process(flatRow(100, -120.0f), 30, 1000);
        QCOMPARE(nf.fftBinAverage(), -197.0f);
        QCOMPARE(nf.lerpAverage(), -197.0f);
    }

    void fastAttackClearsOnlyOnceSettledAndASecondHasPassed()
    {
        NoiseFloorFollower nf;
        nf.setFastAttack(true, 10'000);
        // Converged at once (the line follows in one frame), but only
        // 1000 ms have passed: the flag stays.
        QVERIFY(!nf.process(flatRow(100, -120.0f), 30, 11'000));
        QVERIFY(nf.fastAttack());
        // Past a second: it clears, and says so once.
        QVERIFY(nf.process(flatRow(100, -120.0f), 30, 11'001));
        QVERIFY(!nf.fastAttack());
        QVERIFY(!nf.process(flatRow(100, -120.0f), 30, 11'002));
    }

    void retriggeringFastAttackRestartsItsSecond()
    {
        NoiseFloorFollower nf;
        nf.setFastAttack(true, 0);
        nf.setFastAttack(true, 5'000);
        QVERIFY(!nf.process(flatRow(100, -120.0f), 30, 5'500));
        QVERIFY(nf.fastAttack());
    }

    // R-DSP-56: a MOX edge passes the un-key hold length as the minimum.
    void fastAttackStaysOnForTheMinimumItWasGiven()
    {
        NoiseFloorFollower nf;
        nf.setFastAttack(true, 0, 5000);
        // Converged at once, but the 5000 ms have not passed.
        QVERIFY(!nf.process(flatRow(100, -120.0f), 30, 1001));
        QVERIFY(nf.fastAttack());
        QVERIFY(!nf.process(flatRow(100, -120.0f), 30, 4999));
        QVERIFY(nf.fastAttack());
        QVERIFY(!nf.process(flatRow(100, -120.0f), 30, 5000));
        QVERIFY(nf.fastAttack());
        QVERIFY(nf.process(flatRow(100, -120.0f), 30, 5001));
        QVERIFY(!nf.fastAttack());
    }

    void aShorterTriggerDoesNotCutALongerFastAttackShort()
    {
        NoiseFloorFollower nf;
        nf.setFastAttack(true, 0, 5000);
        // A band change at 2 s asks for the default second: 5 s still holds.
        nf.setFastAttack(true, 2000);
        QVERIFY(!nf.process(flatRow(100, -120.0f), 30, 3500));
        QVERIFY(nf.fastAttack());
        QVERIFY(nf.process(flatRow(100, -120.0f), 30, 5001));
        // One that runs longer does restart it.
        nf.setFastAttack(true, 6000, 1000);
        nf.setFastAttack(true, 6500, 3000);
        QVERIFY(!nf.process(flatRow(100, -120.0f), 30, 9500));
        QVERIFY(nf.fastAttack());
        QVERIFY(nf.process(flatRow(100, -120.0f), 30, 9501));
    }

    void theFloorIsSettledOnceAFrameIsTakenOutOfFastAttack()
    {
        NoiseFloorFollower nf;
        QVERIFY(!nf.settled());
        nf.process(flatRow(100, -120.0f), 30, 0);
        QVERIFY(nf.settled());
        nf.setFastAttack(true, 100);
        QVERIFY(!nf.settled());
        nf.process(flatRow(100, -120.0f), 30, 200);
        QVERIFY(!nf.settled());
        // The frame that clears the fast attack settles it.
        QVERIFY(nf.process(flatRow(100, -120.0f), 30, 1101));
        QVERIFY(nf.settled());
        // Switching it off by hand waits for the next frame.
        nf.setFastAttack(true, 2000);
        nf.setFastAttack(false, 2000);
        QVERIFY(!nf.settled());
        nf.process(flatRow(100, -120.0f), 30, 2040);
        QVERIFY(nf.settled());
    }

    // ── Waterfall levels ────────────────────────────────────────────────

    void manualLevelsAreTheOperatorsOwn()
    {
        WaterfallLevelFollower follower;
        WaterfallLevelSettings settings;
        settings.lowDbm = -130.0f;
        settings.highDbm = -70.0f;
        float low = 0.0f;
        float high = 0.0f;
        follower.compose(flatRow(10, -100.0f), settings, low, high, 0);
        QCOMPARE(low, -130.0f);
        QCOMPARE(high, -70.0f);
    }

    // R-DSP-20 (D15): with AGC on only the low edge follows; High stays the
    // operator's. Rewritten from agcPrimesOnTheRowThenFollowsAtFivePercent,
    // which asserted a high edge of run-max plus 12.
    void agcFollowsTheRowMinimumAndLeavesHighAlone()
    {
        WaterfallLevelFollower follower;
        WaterfallLevelSettings settings;
        settings.agc = true;
        settings.highDbm = -62.0f;
        float low = 0.0f;
        float high = 0.0f;
        QVERIFY(!follower.agcRunLevel().has_value());
        QVector<float> row = flatRow(10, -110.0f);
        row[3] = -60.0f;
        follower.compose(row, settings, low, high, 0);
        QVERIFY(follower.agcPrimed());
        QCOMPARE(*follower.agcRunLevel(), -110.0f);
        QCOMPARE(low, -110.0f - 12.0f);
        QCOMPARE(high, -62.0f);
        QVector<float> next = flatRow(10, -100.0f);
        next[5] = -40.0f;
        follower.compose(next, settings, low, high, 0);
        QCOMPARE(low, 0.05f * -100.0f + 0.95f * -110.0f - 12.0f);
        QCOMPARE(high, -62.0f);
        follower.resetAgc();
        QVERIFY(!follower.agcPrimed());
        QVERIFY(!follower.agcRunLevel().has_value());
        follower.compose(next, settings, low, high, 0);
        QCOMPARE(low, -100.0f - 12.0f);
        QCOMPARE(high, -62.0f);

        // 200 rows with a minimum of -118 and a maximum of -40: the low
        // settles 12 under the minimum, and the high has not moved.
        QVector<float> steady = flatRow(10, -118.0f);
        steady[7] = -40.0f;
        for (int i = 0; i < 200; ++i) {
            follower.compose(steady, settings, low, high, 0);
        }
        QVERIFY2(std::abs(low - -130.0f) < 0.01f, qPrintable(QString::number(low)));
        QCOMPARE(high, -62.0f);
    }

    // S7: NF-AGC is an option under AGC, on the display's own noise floor.
    // Rewritten from noiseFloorAgcTakesTheTenthPercentileAndWinsOverAgc,
    // which asserted the row's 10th percentile plus the offset and a high
    // edge 60 dB above it.
    void noiseFloorAgcPutsLowOnTheSettledFloorPlusTheOffset()
    {
        WaterfallLevelFollower follower;
        WaterfallLevelSettings settings;
        settings.agc = true;
        settings.noiseFloorAgc = true;
        settings.noiseFloorAgcOffsetDb = -3.0f;
        settings.noiseFloorDbm = -115.0f;
        settings.noiseFloorSettled = true;
        settings.highDbm = -58.0f;
        QVector<float> row;
        for (int i = 0; i < 20; ++i) { row.append(-140.0f + static_cast<float>(i)); }
        float low = 0.0f;
        float high = 0.0f;
        follower.compose(row, settings, low, high, 0);
        QCOMPARE(low, -118.0f);
        QCOMPARE(high, -58.0f);
        // A fractional offset is used as it is.
        settings.noiseFloorAgcOffsetDb = -3.5f;
        follower.compose(row, settings, low, high, 0);
        QCOMPARE(low, -118.5f);
        QCOMPARE(high, -58.0f);
        QCOMPARE(WaterfallLevelFollower::clampNoiseFloorAgcOffsetDb(-90), -60);
        QCOMPARE(WaterfallLevelFollower::clampNoiseFloorAgcOffsetDb(70), 60);
    }

    void noiseFloorAgcNotSettledBlendsTheRunLevelTowardTheTarget()
    {
        WaterfallLevelFollower follower;
        WaterfallLevelSettings settings;
        settings.agc = true;
        settings.noiseFloorAgc = true;
        settings.noiseFloorAgcOffsetDb = -3.0f;
        settings.noiseFloorDbm = -115.0f;
        settings.noiseFloorSettled = false;
        settings.highDbm = -58.0f;
        follower.restoreAgcRunLevel(std::nullopt);
        QCOMPARE(*follower.agcRunLevel(), -150.0f);
        float low = 0.0f;
        float high = 0.0f;
        // -150 * 0.6 + -118 * 0.4 = -137.2, then -129.52, whatever the row.
        follower.compose(flatRow(10, -90.0f), settings, low, high, 0);
        QVERIFY2(std::abs(low - -137.2f) < 1.0e-3f, qPrintable(QString::number(low)));
        QCOMPARE(*follower.agcRunLevel(), low);
        QCOMPARE(high, -58.0f);
        follower.compose(flatRow(10, -90.0f), settings, low, high, 0);
        QVERIFY2(std::abs(low - -129.52f) < 1.0e-3f, qPrintable(QString::number(low)));
        // Settled: the target itself, and the run level keeps closing on it.
        settings.noiseFloorSettled = true;
        follower.compose(flatRow(10, -90.0f), settings, low, high, 0);
        QCOMPARE(low, -118.0f);
        QVERIFY(std::abs(*follower.agcRunLevel() - -124.912f) < 1.0e-3f);
        // A follower that was never primed starts at the restart floor too.
        WaterfallLevelFollower fresh;
        fresh.compose(flatRow(10, -90.0f), settings, low, high, 0);
        QVERIFY(std::abs(*fresh.agcRunLevel() - -137.2f) < 1.0e-3f);
    }

    void noiseFloorAgcDoesNothingWithAgcOff()
    {
        WaterfallLevelFollower follower;
        WaterfallLevelSettings settings;
        settings.agc = false;
        settings.noiseFloorAgc = true;
        settings.noiseFloorAgcOffsetDb = -3.0f;
        settings.noiseFloorDbm = -115.0f;
        settings.noiseFloorSettled = true;
        settings.lowDbm = -127.0f;
        settings.highDbm = -58.0f;
        float low = 0.0f;
        float high = 0.0f;
        follower.compose(flatRow(10, -100.0f), settings, low, high, 0);
        QCOMPARE(low, -127.0f);
        QCOMPARE(high, -58.0f);
        QVERIFY(!follower.agcPrimed());
    }

    // R-DSP-28: while keyed every display's AGC is frozen.
    void aKeyedFollowerEstimatesNothing()
    {
        WaterfallLevelFollower follower;
        WaterfallLevelSettings settings;
        settings.agc = true;
        settings.highDbm = -62.0f;
        float low = 0.0f;
        float high = 0.0f;
        QVector<float> row = flatRow(10, -118.0f);
        row[2] = -40.0f;
        for (int i = 0; i < 20; ++i) {
            follower.compose(row, settings, low, high, i * 40);
        }
        const float lowBefore = low;
        const float runBefore = *follower.agcRunLevel();
        follower.setKeyed(true);
        QVERIFY(follower.keyed());
        QVector<float> hot = flatRow(10, -78.0f);
        hot[2] = 0.0f;
        for (int i = 0; i < 50; ++i) {
            follower.compose(hot, settings, low, high, 800 + i * 40);
            QCOMPARE(low, lowBefore);
            QCOMPARE(high, -62.0f);
            QCOMPARE(*follower.agcRunLevel(), runBefore);
        }
        // Receive again: the follower moves from where it was.
        follower.setKeyed(false);
        follower.compose(hot, settings, low, high, 3000);
        QCOMPARE(*follower.agcRunLevel(), 0.05f * -78.0f + 0.95f * runBefore);
    }

    void aKeyedFollowerWithNoLevelStartsAtTheRestartFloor()
    {
        WaterfallLevelFollower follower;
        WaterfallLevelSettings settings;
        settings.agc = true;
        settings.highDbm = -62.0f;
        float low = 0.0f;
        float high = 0.0f;
        follower.setKeyed(true);
        follower.compose(flatRow(10, -20.0f), settings, low, high, 0);
        QCOMPARE(*follower.agcRunLevel(), -150.0f);
        QCOMPARE(low, -162.0f);
        QCOMPARE(high, -62.0f);
        // The noise floor is not a receive floor while keyed.
        WaterfallLevelFollower nf;
        settings.noiseFloorAgc = true;
        settings.noiseFloorDbm = -60.0f;
        settings.noiseFloorSettled = true;
        nf.setKeyed(true);
        nf.compose(flatRow(10, -20.0f), settings, low, high, 0);
        QCOMPARE(low, -150.0f);
    }

    // R-DSP-24: after an un-key the AGC run level holds for the hold length.
    void theHoldKeepsTheRunLevelUntilItsLengthHasRun()
    {
        WaterfallLevelFollower follower;
        WaterfallLevelSettings settings;
        settings.agc = true;
        settings.highDbm = -62.0f;
        float low = 0.0f;
        float high = 0.0f;
        follower.restoreAgcRunLevel(-121.0f);
        follower.beginHold(2000, 0);
        QVERIFY(follower.inHold(0));
        QVERIFY(follower.inHold(1999));
        QVERIFY(!follower.inHold(2000));
        const QVector<float> hot = flatRow(10, -80.0f);
        for (qint64 now : {qint64(0), qint64(1), qint64(1000), qint64(1999)}) {
            follower.compose(hot, settings, low, high, now);
            QCOMPARE(*follower.agcRunLevel(), -121.0f);
            QCOMPARE(low, -133.0f);
            QCOMPARE(high, -62.0f);
        }
        follower.compose(hot, settings, low, high, 2000);
        QCOMPARE(*follower.agcRunLevel(), 0.05f * -80.0f + 0.95f * -121.0f);

        // A cancelled hold stops holding at once.
        WaterfallLevelFollower cancelled;
        cancelled.restoreAgcRunLevel(-121.0f);
        cancelled.beginHold(2000, 0);
        cancelled.cancelHold();
        QVERIFY(!cancelled.inHold(10));
        cancelled.compose(hot, settings, low, high, 10);
        QCOMPARE(*cancelled.agcRunLevel(), 0.05f * -80.0f + 0.95f * -121.0f);

        // A hold with nothing saved does not prime on a held row.
        WaterfallLevelFollower unprimed;
        unprimed.beginHold(2000, 0);
        unprimed.compose(hot, settings, low, high, 500);
        QCOMPARE(*unprimed.agcRunLevel(), -150.0f);

        // With NF-AGC the run level holds too; a settled floor is used.
        WaterfallLevelFollower nf;
        settings.noiseFloorAgc = true;
        settings.noiseFloorAgcOffsetDb = -3.0f;
        settings.noiseFloorDbm = -115.0f;
        settings.noiseFloorSettled = false;
        nf.restoreAgcRunLevel(-121.0f);
        nf.beginHold(2000, 0);
        nf.compose(hot, settings, low, high, 100);
        QCOMPARE(*nf.agcRunLevel(), -121.0f);
        QCOMPARE(low, -121.0f);
        settings.noiseFloorSettled = true;
        nf.compose(hot, settings, low, high, 200);
        QCOMPARE(*nf.agcRunLevel(), -121.0f);
        QCOMPARE(low, -118.0f);
    }

    void aSavedRunLevelIsRestored()
    {
        WaterfallLevelFollower follower;
        WaterfallLevelSettings settings;
        settings.agc = true;
        settings.highDbm = -62.0f;
        float low = 0.0f;
        float high = 0.0f;
        follower.restoreAgcRunLevel(-121.0f);
        QVERIFY(follower.agcPrimed());
        QCOMPARE(*follower.agcRunLevel(), -121.0f);
        // One row with the same minimum: 12 under it.
        follower.compose(flatRow(10, -121.0f), settings, low, high, 0);
        QVERIFY2(std::abs(low - -133.0f) < 1.0e-3f, qPrintable(QString::number(low)));
        QCOMPARE(high, -62.0f);
        follower.restoreAgcRunLevel(std::nullopt);
        QCOMPARE(*follower.agcRunLevel(), -150.0f);
        QCOMPARE(WaterfallLevelFollower::kAgcRestartFloorDbm, -150.0f);
        // Restored while keyed (a band change during the key): the rows of
        // the key draw from the restored level, not the old band's.
        follower.compose(flatRow(10, -121.0f), settings, low, high, 0);
        follower.setKeyed(true);
        follower.restoreAgcRunLevel(-140.0f);
        follower.compose(flatRow(10, -60.0f), settings, low, high, 0);
        QCOMPARE(low, -152.0f);
    }

    void theOperatorsLevelsStayLiveWhileKeyed()
    {
        WaterfallLevelFollower follower;
        WaterfallLevelSettings settings;
        settings.lowDbm = -120.0f;
        settings.highDbm = -62.0f;
        float low = 0.0f;
        float high = 0.0f;
        follower.setKeyed(true);
        follower.compose(flatRow(10, -100.0f), settings, low, high, 0);
        QCOMPARE(low, -120.0f);
        settings.lowDbm = -110.0f;
        settings.highDbm = -50.0f;
        follower.compose(flatRow(10, -100.0f), settings, low, high, 0);
        QCOMPARE(low, -110.0f);
        QCOMPARE(high, -50.0f);
    }

    void clarityLeavesTheLevelsItSet()
    {
        WaterfallLevelFollower follower;
        WaterfallLevelSettings settings;
        settings.agc = true;
        settings.noiseFloorAgc = true;
        settings.clarityActive = true;
        float low = -125.0f;
        float high = -70.0f;
        follower.compose(flatRow(10, -100.0f), settings, low, high, 0);
        QCOMPARE(low, -125.0f);
        QCOMPARE(high, -70.0f);
        QVERIFY(!follower.agcPrimed());
        follower.setKeyed(true);
        follower.compose(flatRow(10, -100.0f), settings, low, high, 0);
        QCOMPARE(low, -125.0f);
        QCOMPARE(high, -70.0f);
    }

    // ── The un-key hold length ──────────────────────────────────────────

    void theHoldIsTwiceTheFillTimeAndNoShorterThanASecond()
    {
        QCOMPARE(kMoxHoldMinMs, qint64(1000));
        // 4096 at 192 kHz fills in 21 ms: the second.
        QCOMPARE(moxHoldLengthMs(4096, 1, 192000.0), qint64(1000));
        // 262144 at 48 kHz fills in 5461 ms.
        QCOMPARE(moxHoldLengthMs(262144, 1, 48000.0), qint64(10922));
        // Decimation counts: 65536 x 2 at 96 kHz fills in 1365 ms.
        QCOMPARE(moxHoldLengthMs(65536, 2, 96000.0), qint64(2730));
        // Nothing to compute a fill from: the second.
        QCOMPARE(moxHoldLengthMs(4096, 1, 0.0), qint64(1000));
        QCOMPARE(moxHoldLengthMs(4096, 1, -48000.0), qint64(1000));
        QCOMPARE(moxHoldLengthMs(4096, 1, std::numeric_limits<double>::quiet_NaN()),
                 qint64(1000));
        QCOMPARE(moxHoldLengthMs(4096, 1, std::numeric_limits<double>::infinity()),
                 qint64(1000));
        QCOMPARE(moxHoldLengthMs(0, 1, 48000.0), qint64(1000));
        QCOMPARE(moxHoldLengthMs(4096, 0, 48000.0), qint64(1000));
        // A rate too small to be real does not overflow.
        QVERIFY(moxHoldLengthMs(262144, 8, 1.0e-300) > 0);
    }

    void anEmptyRowChangesNothing()
    {
        WaterfallLevelFollower follower;
        WaterfallLevelSettings settings;
        settings.agc = true;
        float low = -1.0f;
        float high = -2.0f;
        follower.compose({}, settings, low, high, 0);
        QCOMPARE(low, -1.0f);
        QCOMPARE(high, -2.0f);
        NoiseFloorFollower nf;
        QVERIFY(!nf.process({}, 30, 0));
        QCOMPARE(nf.fftBinAverage(), -200.0f);
    }
};

QTEST_APPLESS_MAIN(TstDisplayFollowers)
#include "tst_display_followers.moc"
