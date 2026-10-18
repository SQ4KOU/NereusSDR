// no-port-check: NereusSDR-original tests. They cite Thetis only to say
// which behaviour the code under test (SpectrumWidget, DisplayFollowers)
// ports; no Thetis code is in this file.
// Regression tests for issue #230 — "Waterfall gain seems unstable and not
// saving settings in 4.0".
//
// Source-first context: Thetis splits persistent user thresholds
// (waterfall_low/high_threshold, display.cs:2522-2548 [v2.10.3.13]) from
// runtime AGC state (_RX1waterfallPreviousMinValue) and renders into
// per-draw locals at display.cs:6575-6594 [v2.10.3.13]. NereusSDR
// previously collapsed both roles into m_wfLow/HighThreshold, so AGC /
// Clarity silently overwrote the user's saved values; the next
// scheduleSettingsSave() debounce persisted the overwrite. The Display
// rebuild's Scale mode likewise reads the live spectrum range without
// overwriting Manual's saved endpoints. These tests verify that split.

#include <QtTest/QtTest>
#include <QRegularExpression>
#include <QVector>

#include "gui/SpectrumWidget.h"

using namespace NereusSDR;

class TestWaterfallThresholdPersistence : public QObject
{
    Q_OBJECT

private slots:
    // Clarity is the only external writer that previously called the
    // user setters (setWfLow/HighThreshold). After the fix it goes
    // through setClarityWaterfallThresholds which writes the active
    // mirror only — user fields stay put.
    void clarityWritesDoNotMutateUserThresholds();

    // AGC's per-row composition writes m_wfActive*, never m_wf*.
    // composeWaterfallActiveThresholds is the testable seam pulled out
    // of pushWaterfallRow().
    void agcComposeDoesNotMutateUserThresholds();

    // NF-AGC override goes through the same compose path.
    void nfAgcComposeDoesNotMutateUserThresholds();

    // Fix wave I1: the noise floor NF-AGC colours against is measured on
    // uncalibrated pixels, and the row it is compared with is calibrated.
    // The floor takes the same calibration, so the low level lands under
    // the floor that is drawn.
    void nfAgcLowTakesTheCalibratedNoiseFloor();

    // Scale mode reads the live spectrum range while setDbmRange() leaves
    // Manual's saved endpoints intact.
    void scaleModeSetDbmRangeUsesLiveRangeAndPreservesManualThresholds();

    // When useSpectrumMinMax is OFF, setDbmRange() must not touch the
    // user's waterfall thresholds.
    void useSpectrumMinMaxOffSetDbmRangeLeavesThresholdsAlone();

    // Toggling Scale mode on uses the current dB range immediately while
    // preserving the endpoints Manual will restore.
    void enablingScaleModeUsesCurrentRangeAndPreservesManualThresholds();
};

// Active getters mirror the user thresholds on construction so the
// renderer always has a valid value before the first row push.
void TestWaterfallThresholdPersistence::clarityWritesDoNotMutateUserThresholds()
{
    SpectrumWidget w;
    w.setWfLowThreshold(-100.0f);
    w.setWfHighThreshold(-40.0f);
    QCOMPARE(w.wfLowThreshold(),  -100.0f);
    QCOMPARE(w.wfHighThreshold(), -40.0f);

    // Clarity drives a wider span — analogous to AGC RunMin/RunMax
    // settling far from the user's preferred display window.
    w.setClarityWaterfallThresholds(-150.0f, -10.0f);

    // Persistent values must be unchanged. The active layer (what the
    // renderer uses) reflects Clarity's output.
    QCOMPARE(w.wfLowThreshold(),  -100.0f);
    QCOMPARE(w.wfHighThreshold(), -40.0f);
    QCOMPARE(w.wfActiveLowThreshold(),  -150.0f);
    QCOMPARE(w.wfActiveHighThreshold(), -10.0f);
}

void TestWaterfallThresholdPersistence::agcComposeDoesNotMutateUserThresholds()
{
    SpectrumWidget w;
    w.setWfLowThreshold(-100.0f);
    w.setWfHighThreshold(-40.0f);
    w.setWfAgcEnabled(true);

    // Build a synthetic row that drives AGC RunMin/RunMax to
    // values well outside the user-configured window.
    QVector<float> row;
    row.reserve(128);
    for (int i = 0; i < 128; ++i) {
        const float v = (i < 64) ? -130.0f : -20.0f;
        row.append(v);
    }

    // Run the compose pass multiple times to let the one-pole filter
    // converge near the actual min/max.
    for (int i = 0; i < 200; ++i) {
        w.composeWaterfallActiveThresholds(row);
    }

    // Persistent user values must NOT have been clobbered.
    QCOMPARE(w.wfLowThreshold(),  -100.0f);
    QCOMPARE(w.wfHighThreshold(), -40.0f);

    // The active low reflects AGC's settled minimum (RunMin - margin; the
    // 12 dB margin is WaterfallLevelFollower's). The active high is the
    // user's High: AGC moves only the low edge (R-DSP-20).
    QVERIFY(w.wfActiveLowThreshold()  < -100.0f);  // AGC pushed lower
    QCOMPARE(w.wfActiveHighThreshold(), -40.0f);
}

void TestWaterfallThresholdPersistence::nfAgcComposeDoesNotMutateUserThresholds()
{
    SpectrumWidget w;
    w.setWfLowThreshold(-100.0f);
    w.setWfHighThreshold(-40.0f);
    w.setWfAgcEnabled(true);            // NF-AGC is an option under AGC
    w.setWaterfallNFAGCEnabled(true);   // engage NF-AGC override
    w.setWaterfallAGCOffsetDb(-10);

    QVector<float> row;
    row.reserve(128);
    for (int i = 0; i < 128; ++i) {
        row.append(-110.0f + static_cast<float>(i) * 0.1f);
    }

    w.composeWaterfallActiveThresholds(row);

    QCOMPARE(w.wfLowThreshold(),  -100.0f);
    QCOMPARE(w.wfHighThreshold(), -40.0f);
    // NF-AGC writes the active low from the pan's noise floor plus the
    // offset (the run level until the floor is settled) and leaves the
    // active high at the user's High.
    QVERIFY(w.wfActiveLowThreshold()  != -100.0f);
    QCOMPARE(w.wfActiveHighThreshold(), -40.0f);

    // With AGC off NF-AGC does nothing: the levels are the user's.
    w.setWfAgcEnabled(false);
    w.composeWaterfallActiveThresholds(row);
    QCOMPARE(w.wfActiveLowThreshold(),  -100.0f);
    QCOMPARE(w.wfActiveHighThreshold(), -40.0f);
}

void TestWaterfallThresholdPersistence::nfAgcLowTakesTheCalibratedNoiseFloor()
{
    constexpr float kMeasuredFloorDbm = -120.0f;   // on uncalibrated pixels
    constexpr float kStationCalDb = -12.5f;
    constexpr float kUserCalDb = 3.0f;
    constexpr int kOffsetDb = -10;
    // -120 - 12.5 + 3 - 10: the calibrated floor plus the stored offset.
    constexpr float kExpectedLowDbm = -139.5f;

    SpectrumWidget w;
    w.setWfLowThreshold(-100.0f);
    w.setWfHighThreshold(-40.0f);
    w.setWfAgcEnabled(true);
    w.setWaterfallNFAGCEnabled(true);
    w.setWaterfallAGCOffsetDb(kOffsetDb);
    w.setStationCalOffsetDb(kStationCalDb);
    w.setDbmCalOffset(kUserCalDb);
    w.setMeasuredNoiseFloorForTest(kMeasuredFloorDbm);

    // The calibrated row pushWaterfallRow hands to the composition. NF-AGC
    // does not read its values; it is here because an empty row composes
    // nothing.
    const QVector<float> row(128, kMeasuredFloorDbm + kStationCalDb + kUserCalDb);

    // Until the floor is settled the low level is the run level, which
    // closes on the same target by 0.4 a row: 200 rows is the target.
    for (int i = 0; i < 200; ++i) {
        w.composeWaterfallActiveThresholds(row);
    }

    QVERIFY2(qAbs(w.wfActiveLowThreshold() - kExpectedLowDbm) < 0.01f,
             qPrintable(QString::number(w.wfActiveLowThreshold())));
    QCOMPARE(w.wfActiveHighThreshold(), -40.0f);
    // The user's own levels are still theirs.
    QCOMPARE(w.wfLowThreshold(), -100.0f);

    // A non-finite station offset is ignored (fix wave M2): the floor and
    // the row keep the offset they had.
    const QRegularExpression refused(QStringLiteral("calibration offset is not finite"));
    QTest::ignoreMessage(QtWarningMsg, refused);
    w.setStationCalOffsetDb(qQNaN());
    QCOMPARE(w.stationCalOffsetDb(), kStationCalDb);
    QTest::ignoreMessage(QtWarningMsg, refused);
    w.setStationCalOffsetDb(qInf());
    QCOMPARE(w.stationCalOffsetDb(), kStationCalDb);
    w.composeWaterfallActiveThresholds(row);
    QVERIFY2(qAbs(w.wfActiveLowThreshold() - kExpectedLowDbm) < 0.01f,
             qPrintable(QString::number(w.wfActiveLowThreshold())));
}

void TestWaterfallThresholdPersistence::scaleModeSetDbmRangeUsesLiveRangeAndPreservesManualThresholds()
{
    SpectrumWidget w;
    // The chooser makes its five modes exclusive. Drive that same state
    // explicitly: the widget defaults request Clarity and Auto until the
    // chooser applies Scale.
    w.setClarityEnabled(false);
    w.setWfAgcEnabled(false);
    w.setWaterfallNFAGCEnabled(false);
    w.setWfLowThreshold(-122.0f);
    w.setWfHighThreshold(-62.0f);
    w.setWfUseSpectrumMinMax(true);

    // Mirrors a user dragging the spectrum dB range while Scale owns the
    // effective levels.
    w.setDbmRange(-150.0f, -30.0f);

    // Scale follows the live range without destroying Manual's saved pair.
    const std::pair<float, float> effective = w.effectiveWaterfallLevelsForTest();
    QCOMPARE(effective.first, -150.0f);
    QCOMPARE(effective.second, -30.0f);
    QCOMPARE(w.wfLowThreshold(), -122.0f);
    QCOMPARE(w.wfHighThreshold(), -62.0f);
}

void TestWaterfallThresholdPersistence::useSpectrumMinMaxOffSetDbmRangeLeavesThresholdsAlone()
{
    SpectrumWidget w;
    // Match the chooser's Manual state rather than the widget's requested
    // Clarity/Auto defaults.
    w.setClarityEnabled(false);
    w.setWfAgcEnabled(false);
    w.setWaterfallNFAGCEnabled(false);
    w.setWfLowThreshold(-122.0f);
    w.setWfHighThreshold(-62.0f);
    w.setWfUseSpectrumMinMax(false);

    w.setDbmRange(-150.0f, -30.0f);

    QCOMPARE(w.wfLowThreshold(),  -122.0f);
    QCOMPARE(w.wfHighThreshold(), -62.0f);
    const std::pair<float, float> effective = w.effectiveWaterfallLevelsForTest();
    QCOMPARE(effective.first, -122.0f);
    QCOMPARE(effective.second, -62.0f);
}

void TestWaterfallThresholdPersistence::enablingScaleModeUsesCurrentRangeAndPreservesManualThresholds()
{
    SpectrumWidget w;
    // Begin in the chooser's Manual state, then select Scale below.
    w.setClarityEnabled(false);
    w.setWfAgcEnabled(false);
    w.setWaterfallNFAGCEnabled(false);
    w.setWfLowThreshold(-122.0f);
    w.setWfHighThreshold(-62.0f);
    w.setWfUseSpectrumMinMax(false);

    // Pre-set a different dBm range while the flag is off — should
    // leave thresholds alone (verified by the test above).
    w.setDbmRange(-140.0f, -20.0f);
    QCOMPARE(w.wfLowThreshold(),  -122.0f);
    QCOMPARE(w.wfHighThreshold(), -62.0f);
    std::pair<float, float> effective = w.effectiveWaterfallLevelsForTest();
    QCOMPARE(effective.first, -122.0f);
    QCOMPARE(effective.second, -62.0f);

    // Flipping Scale on must use the current range immediately and must not
    // overwrite the pair Manual restores.
    w.setWfUseSpectrumMinMax(true);
    effective = w.effectiveWaterfallLevelsForTest();
    QCOMPARE(effective.first, -140.0f);
    QCOMPARE(effective.second, -20.0f);
    QCOMPARE(w.wfLowThreshold(), -122.0f);
    QCOMPARE(w.wfHighThreshold(), -62.0f);
}

QTEST_MAIN(TestWaterfallThresholdPersistence)
#include "tst_waterfall_threshold_persistence.moc"
