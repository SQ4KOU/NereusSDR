// no-port-check: unit tests for Task 2.9 NF-aware grid + NoiseFloorEstimator::prime().
// NereusSDR-original — no Thetis upstream for the grid tracking logic.
// Thetis source reference: console.cs:46074-46086 [v2.10.3.13] GridMinFollowsNFRX1.
//
// Tests verify:
//   1. Offset shifts grid min below NF; grid max unchanged when maintain-delta=false.
//   2. maintain-delta moves grid max to preserve the original dB range.
//   3. When disabled, NF change does not move grid.
//   4. Deadband (< 2 dB) suppresses grid update.
//   5. NoiseFloorEstimator::prime() stores the seeded value.
//   6. prime() clears to a new value on second call.
//
// No pixel-diff — visual verification deferred to manual bench.

#include <QtTest/QtTest>

#include "gui/SpectrumWidget.h"
#include "core/NoiseFloorEstimator.h"

using namespace NereusSDR;

class TestNFAwareGrid : public QObject
{
    Q_OBJECT

private slots:

    // -----------------------------------------------------------------------
    // NF-aware grid tests
    // -----------------------------------------------------------------------

    void offset_shifts_grid_min_below_nf()
    {
        // From Thetis console.cs:46080 [v2.10.3.13]:
        //   float setPoint = _lastRX1NoiseFloor - _RX1NFoffsetGridFollow;
        //   float fDelta = (float)Math.Abs(...); // abs incase //MW0LGE [2.9.0.7] [console.cs:46081]
        // NereusSDR: proposedMin = nf + offset (addition semantics, offset -10
        // is equivalent to Thetis offset +10 with subtraction).
        SpectrumWidget w;
        // Set up a known range: max=-30, min=-130, delta=100 dB.
        w.setDbmRange(-130.0f, -30.0f);
        w.setAdjustGridMinToNoiseFloor(true);
        w.setNFOffsetGridFollow(-10);
        w.setMaintainNFAdjustDelta(false);

        // NF = -100 dBm → proposedMin = -100 + (-10) = -110
        w.testApplyNoiseFloor(-100.0f);

        QCOMPARE(w.gridMin(), -110);
        QCOMPARE(w.gridMax(), -30);   // unchanged when maintain-delta is false
    }

    void maintain_delta_moves_grid_max_too()
    {
        // From Thetis console.cs:46085 [v2.10.3.13]:
        //   if (_maintainNFAdjustDeltaRX1) SetupForm.DisplayGridMax = setPoint + fDelta;
        // fDelta guarded with abs incase //MW0LGE [2.9.0.7] [original inline comment from console.cs:46081]
        SpectrumWidget w;
        // Range: max=-30, min=-130, delta=100 dB.
        w.setDbmRange(-130.0f, -30.0f);
        w.setAdjustGridMinToNoiseFloor(true);
        w.setNFOffsetGridFollow(0);
        w.setMaintainNFAdjustDelta(true);

        // NF = -90 dBm → proposedMin = -90 + 0 = -90
        // delta = 100 → newMax = -90 + 100 = +10
        w.testApplyNoiseFloor(-90.0f);

        QCOMPARE(w.gridMin(), -90);
        QCOMPARE(w.gridMax(), 10);
    }

    void disabled_does_not_change_grid()
    {
        SpectrumWidget w;
        w.setDbmRange(-130.0f, -30.0f);
        w.setAdjustGridMinToNoiseFloor(false);

        w.testApplyNoiseFloor(-100.0f);

        QCOMPARE(w.gridMin(), -130);
        QCOMPARE(w.gridMax(), -30);
    }

    void deadband_suppresses_small_change()
    {
        // From Thetis console.cs:46082 [v2.10.3.13]:
        //   if (Math.Abs(SetupForm.DisplayGridMin - setPoint) >= 2)
        // fDelta for range: abs incase //MW0LGE [2.9.0.7] [original inline comment from console.cs:46081]
        // Changes < 2 dB from current grid min are ignored.
        SpectrumWidget w;
        // Start with min=-110 (so refLevel - dynamicRange = -110).
        w.setDbmRange(-110.0f, -30.0f);
        w.setAdjustGridMinToNoiseFloor(true);
        w.setNFOffsetGridFollow(0);
        w.setMaintainNFAdjustDelta(false);

        // NF = -111 dBm → proposedMin = -111 + 0 = -111
        // |currentMin(-110) - proposedMin(-111)| = 1.0 < 2.0 → no update
        w.testApplyNoiseFloor(-111.0f);

        QCOMPARE(w.gridMin(), -110);   // unchanged (deadband)
        QCOMPARE(w.gridMax(), -30);
    }

    void deadband_allows_change_at_2db()
    {
        // Exactly 2 dB difference: should update (>= 2 is the Thetis condition).
        SpectrumWidget w;
        w.setDbmRange(-110.0f, -30.0f);
        w.setAdjustGridMinToNoiseFloor(true);
        w.setNFOffsetGridFollow(0);
        w.setMaintainNFAdjustDelta(false);

        // |currentMin(-110) - proposedMin(-112)| = 2.0 → update allowed
        w.testApplyNoiseFloor(-112.0f);

        QCOMPARE(w.gridMin(), -112);
        QCOMPARE(w.gridMax(), -30);
    }

    void positive_offset_raises_grid_min()
    {
        // Positive offset places grid min above the NF.
        SpectrumWidget w;
        w.setDbmRange(-130.0f, -30.0f);
        w.setAdjustGridMinToNoiseFloor(true);
        w.setNFOffsetGridFollow(10);
        w.setMaintainNFAdjustDelta(false);

        // NF = -100 dBm → proposedMin = -100 + 10 = -90
        w.testApplyNoiseFloor(-100.0f);

        QCOMPARE(w.gridMin(), -90);
        QCOMPARE(w.gridMax(), -30);
    }

    // -----------------------------------------------------------------------
    // NoiseFloorEstimator::prime() tests (Task 2.10 dependency)
    // -----------------------------------------------------------------------

    void prime_stores_seeded_value()
    {
        // NereusSDR-original — no Thetis equivalent.
        // prime() seeds the estimator so Task 2.10 per-band priming works.
        NoiseFloorEstimator est;
        QVERIFY(!est.hasPrimedValue());  // starts empty

        est.prime(-115.0);

        QVERIFY(est.hasPrimedValue());
        QCOMPARE(est.primedValue(), -115.0f);
    }

    void prime_updates_on_second_call()
    {
        NoiseFloorEstimator est;
        est.prime(-115.0);
        est.prime(-105.0);

        QCOMPARE(est.primedValue(), -105.0f);
    }

    void prime_does_not_affect_estimate()
    {
        // prime() stores a hint for ClarityController to pre-seed its EWMA;
        // it does NOT change the estimate() result (which is bin-based).
        QVector<float> bins(2048, -130.0f);
        NoiseFloorEstimator est;
        est.prime(-90.0);

        const float result = est.estimate(bins);
        QCOMPARE(result, -130.0f);   // estimate is still bin-based
    }
};

QTEST_MAIN(TestNFAwareGrid)
#include "tst_nf_aware_grid.moc"
