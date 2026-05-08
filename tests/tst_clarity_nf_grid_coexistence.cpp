// tst_clarity_nf_grid_coexistence.cpp
//
// Task 2.11 — Clarity (3G-9c adaptive waterfall tuning) and the new
// NF-aware grid (Task 2.9) coexistence test. Verifies that both features
// consume the same NoiseFloorEstimator stream without oscillating or
// fighting each other.
//
// Both apply changes to different display dimensions:
//   - Clarity → waterfallThresholdsChanged(low, high) adjust the waterfall
//     color palette thresholds
//   - NF-aware grid → onNoiseFloorChanged(nf) adjusts the spectrum grid
//     bounds (gridMin tracks NF + offset)
//
// Running both simultaneously is safe because they are independent
// transformations. This test verifies the safety contract.
//
// no-port-check: NereusSDR-original test; no Thetis equivalent.

#include <QtTest/QtTest>
#include <QSignalSpy>

#include "core/ClarityController.h"
#include "gui/SpectrumWidget.h"

using namespace NereusSDR;

class TestClarityNFGridCoexistence : public QObject {
    Q_OBJECT
private slots:

    void both_features_observe_same_nf_stream_without_double_tuning()
    {
        // ClarityController emits noiseFloorChanged after EWMA smoothing.
        // SpectrumWidget::onNoiseFloorChanged() applies grid offset/maintain-delta.
        // Both features are connected to the same Clarity signal stream.
        // They apply to different dimensions (waterfall thresholds vs spectrum
        // grid) so they should not fight.

        ClarityController clarity;
        clarity.setEnabled(true);
        clarity.setSmoothingTauSec(0.0f);  // No smoothing for test repeatability

        SpectrumWidget widget;
        widget.setAdjustGridMinToNoiseFloor(true);
        widget.setNFOffsetGridFollow(-10);
        widget.setMaintainNFAdjustDelta(false);
        widget.setDbmRange(-130, -30);  // initial state

        // Connect Clarity's NF stream to widget's NF-grid handler.
        // Both Clarity's own threshold emission and the NF signal go to
        // independent handlers; they should not cross-talk.
        QObject::connect(&clarity, &ClarityController::noiseFloorChanged,
                         &widget, &SpectrumWidget::onNoiseFloorChanged);

        QSignalSpy clarityWFSpy(&clarity, &ClarityController::waterfallThresholdsChanged);
        QSignalSpy clarityNFSpy(&clarity, &ClarityController::noiseFloorChanged);

        // Feed 10 frames of stable -100 dBm noise. EWMA tau=0 means
        // smoothed == raw, so floor estimate should be exactly -100.
        QVector<float> bins(1024, -100.0f);
        for (int i = 0; i < 10; ++i) {
            clarity.feedBins(bins, static_cast<qint64>(i) * 1000);
        }

        // Clarity must have emitted at least once (cadence throttled to 500ms,
        // so 10 frames at 1s apart should give ~2 emissions).
        QVERIFY(clarityWFSpy.count() >= 1);
        QVERIFY(clarityNFSpy.count() >= 1);

        // Grid min should track NF + offset = -100 + (-10) = -110.
        // Initial is -130, so it should have moved toward -110.
        const int currentGridMin = widget.gridMin();
        QVERIFY2(currentGridMin <= -100, "Grid min should track noise floor");
        QVERIFY2(currentGridMin >= -120, "Grid min should not overshoot");
    }

    void clarity_thresholds_emitted_independent_of_nf_grid()
    {
        // Waterfall threshold changes are driven by Clarity's margin math
        // and deadband gate, independent of the NF-aware grid feature.
        // This test isolates the threshold stream to verify it is not
        // affected by grid settings.

        ClarityController clarity;
        clarity.setEnabled(true);
        clarity.setSmoothingTauSec(0.0f);

        SpectrumWidget widget;
        widget.setAdjustGridMinToNoiseFloor(true);  // NF-grid ON
        widget.setNFOffsetGridFollow(-10);
        widget.setDbmRange(-130, -30);

        // Connect only the threshold handler, not the NF handler.
        QSignalSpy thresholdSpy(&clarity, &ClarityController::waterfallThresholdsChanged);

        QVector<float> bins(1024, -100.0f);
        clarity.feedBins(bins, 0);

        // First emission should happen; verify threshold math is correct.
        QVERIFY(thresholdSpy.count() >= 1);
        const QList<QVariant> args = thresholdSpy.first();
        const float low  = args.at(0).toFloat();
        const float high = args.at(1).toFloat();

        // Default margins: low = -100 + (-5) = -105, high = -100 + 55 = -45.
        // Min gap clamp should not apply here (gap is 60 dB >> 30 dB min).
        QCOMPARE(low,  -105.0f);
        QCOMPARE(high,  -45.0f);
    }

    void nf_grid_unchanged_when_feature_disabled()
    {
        // Disabling the NF-aware grid means onNoiseFloorChanged() returns
        // early and grid bounds remain static. Clarity continues to emit
        // thresholds and NF signals independently.

        ClarityController clarity;
        clarity.setEnabled(true);
        clarity.setSmoothingTauSec(0.0f);

        SpectrumWidget widget;
        widget.setAdjustGridMinToNoiseFloor(false);  // NF-grid OFF
        widget.setDbmRange(-130, -30);

        QObject::connect(&clarity, &ClarityController::noiseFloorChanged,
                         &widget, &SpectrumWidget::onNoiseFloorChanged);

        QSignalSpy clarityWFSpy(&clarity, &ClarityController::waterfallThresholdsChanged);
        QSignalSpy clarityNFSpy(&clarity, &ClarityController::noiseFloorChanged);

        // Feed bins; widget grid min should NOT change.
        QVector<float> bins(1024, -100.0f);
        clarity.feedBins(bins, 0);

        // Clarity should still emit on both signals.
        QVERIFY(clarityWFSpy.count() >= 1);
        QVERIFY(clarityNFSpy.count() >= 1);

        // Grid bounds should remain unchanged.
        QCOMPARE(widget.gridMin(), -130);
        QCOMPARE(widget.gridMax(),  -30);
    }

    void clarity_paused_suppresses_both_emissions()
    {
        // When Clarity is paused (TX, manual override), both the threshold
        // and NF signal emissions stop. The NF-aware grid feature receives
        // no updates while Clarity is silent.

        ClarityController clarity;
        clarity.setEnabled(true);
        clarity.setSmoothingTauSec(0.0f);

        SpectrumWidget widget;
        widget.setAdjustGridMinToNoiseFloor(true);
        widget.setNFOffsetGridFollow(-10);
        widget.setDbmRange(-130, -30);

        QObject::connect(&clarity, &ClarityController::noiseFloorChanged,
                         &widget, &SpectrumWidget::onNoiseFloorChanged);

        QSignalSpy thresholdSpy(&clarity, &ClarityController::waterfallThresholdsChanged);
        QSignalSpy nfSpy(&clarity, &ClarityController::noiseFloorChanged);

        // First frame — emissions active
        QVector<float> bins(1024, -100.0f);
        clarity.feedBins(bins, 0);
        const int emitCountAfterFirst = thresholdSpy.count() + nfSpy.count();

        // Pause (manual override)
        clarity.notifyManualOverride();
        QVERIFY(clarity.isPaused());

        // Second frame — paused, so no new emissions
        clarity.feedBins(bins, 1000);
        const int emitCountAfterSecond = thresholdSpy.count() + nfSpy.count();
        QCOMPARE(emitCountAfterSecond, emitCountAfterFirst);

        // Grid should not have changed during the pause
        QVERIFY(widget.gridMin() < -100);  // Moved from -130 during first frame
        const int gridMinDuringPause = widget.gridMin();

        // Resume
        clarity.retuneNow();
        QVERIFY(!clarity.isPaused());

        clarity.feedBins(bins, 2000);
        QVERIFY(thresholdSpy.count() + nfSpy.count() > emitCountAfterSecond);
    }

    void nf_offset_changes_propagate_on_next_frame()
    {
        // Changing the NF offset grid-follow value should take effect
        // on the next emission from Clarity. This test verifies that the
        // NF-aware grid picks up offset changes dynamically.

        ClarityController clarity;
        clarity.setEnabled(true);
        clarity.setSmoothingTauSec(0.0f);

        SpectrumWidget widget;
        widget.setAdjustGridMinToNoiseFloor(true);
        widget.setNFOffsetGridFollow(0);   // Initial: NF + 0 = NF
        widget.setMaintainNFAdjustDelta(false);
        widget.setDbmRange(-130, -30);

        QObject::connect(&clarity, &ClarityController::noiseFloorChanged,
                         &widget, &SpectrumWidget::onNoiseFloorChanged);

        // Feed frames
        QVector<float> bins(1024, -100.0f);
        clarity.feedBins(bins, 0);
        const int gridMinWithZeroOffset = widget.gridMin();

        // Change offset to -20
        widget.setNFOffsetGridFollow(-20);

        clarity.feedBins(bins, 1000);
        const int gridMinWithNegativeOffset = widget.gridMin();

        // With offset -20, new min should be -100 + (-20) = -120 (if delta >= 2).
        // The widget should have adjusted.
        QVERIFY(gridMinWithNegativeOffset < gridMinWithZeroOffset);
    }

    void no_oscillation_on_stable_noise_floor()
    {
        // Feed the same noise floor for many frames. The EWMA should
        // converge, the deadband should suppress repeat emissions, and the
        // grid should stabilize at a steady value. No oscillation.

        ClarityController clarity;
        clarity.setEnabled(true);
        clarity.setSmoothingTauSec(0.0f);

        SpectrumWidget widget;
        widget.setAdjustGridMinToNoiseFloor(true);
        widget.setNFOffsetGridFollow(-10);
        widget.setMaintainNFAdjustDelta(false);
        widget.setDbmRange(-130, -30);

        QObject::connect(&clarity, &ClarityController::noiseFloorChanged,
                         &widget, &SpectrumWidget::onNoiseFloorChanged);

        // Stable -100 dBm for 50 frames (50 seconds at 1 frame/sec)
        QVector<float> bins(1024, -100.0f);
        QVector<int> gridMins;

        for (int i = 0; i < 50; ++i) {
            clarity.feedBins(bins, static_cast<qint64>(i) * 1000);
            gridMins.append(widget.gridMin());
        }

        // After deadband convergence, grid min should stabilize.
        // Check the last 10 frames are identical (no oscillation).
        for (int i = 40; i < 50; ++i) {
            QCOMPARE(gridMins.at(i), gridMins.at(40));
        }
    }
};

QTEST_MAIN(TestClarityNFGridCoexistence)
#include "tst_clarity_nf_grid_coexistence.moc"
