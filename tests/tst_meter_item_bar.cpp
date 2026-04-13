// tst_meter_item_bar.cpp — BarItem API parity with Thetis clsBarItem.
//
// Covers the meter-parity-audit gaps in BarItem (Phase A of the port plan):
//   A1: ShowValue / ShowPeakValue / FontColour + peak tracking
//   A2: ShowHistory / HistoryColour / HistoryDuration
//   A3: ShowMarker / MarkerColour / PeakHoldMarkerColour
//   A4: BarStyle::Line + non-linear ScaleCalibration
//
// Thetis source of truth:
//   MeterManager.cs:19917-20278 (clsBarItem class definition)
//   MeterManager.cs:35950-36140 (renderHBar SolidFilled/Line branches)
//   MeterManager.cs:21499-21616 (addSMeterBar — canonical Line-style caller)

#include <QtTest/QtTest>
#include <QColor>

#include "gui/meters/MeterItem.h"

using namespace NereusSDR;

class TstMeterItemBar : public QObject
{
    Q_OBJECT

private slots:
    // ---- Phase A1: ShowValue / ShowPeakValue / FontColour ----

    void showValue_default_is_false()
    {
        BarItem b;
        QCOMPARE(b.showValue(), false);
        QCOMPARE(b.showPeakValue(), false);
    }

    void showValue_and_showPeakValue_roundtrip()
    {
        BarItem b;
        b.setShowValue(true);
        b.setShowPeakValue(true);
        QCOMPARE(b.showValue(), true);
        QCOMPARE(b.showPeakValue(), true);
    }

    void fontColour_roundtrip()
    {
        BarItem b;
        const QColor yellow(0xff, 0xff, 0x00);
        b.setFontColour(yellow);
        QCOMPARE(b.fontColour(), yellow);
    }

    void setValue_tracks_peak_high_water_mark()
    {
        // From Thetis clsBarItem — peak is the highest value seen,
        // used by ShowPeakValue text and the peak-hold marker.
        BarItem b;
        b.setRange(-140.0, 0.0);
        b.setValue(-80.0);
        b.setValue(-50.0);   // new peak
        b.setValue(-70.0);   // lower — peak should hold
        QVERIFY(b.peakValue() >= -50.0);
    }

    // ---- Phase A2: ShowHistory / HistoryColour / HistoryDuration ----

    void showHistory_default_is_false()
    {
        BarItem b;
        QCOMPARE(b.showHistory(), false);
    }

    void showHistory_and_historyColour_roundtrip()
    {
        // Thetis addSMeterBar default: HistoryColour = Color.FromArgb(128, Red)
        // (MeterManager.cs:21545).
        BarItem b;
        b.setShowHistory(true);
        const QColor red128(255, 0, 0, 128);
        b.setHistoryColour(red128);
        QCOMPARE(b.showHistory(), true);
        QCOMPARE(b.historyColour(), red128);
    }

    void historyDuration_default_matches_thetis_4000ms()
    {
        // Thetis addSMeterBar / AddADCMaxMag both set HistoryDuration = 4000
        // (MeterManager.cs:21539, 21633). Use that as the NereusSDR default.
        BarItem b;
        QCOMPARE(b.historyDurationMs(), 4000);
    }

    void historyDuration_roundtrip()
    {
        BarItem b;
        b.setHistoryDurationMs(2500);
        QCOMPARE(b.historyDurationMs(), 2500);
    }

    void history_samples_accumulate_when_enabled()
    {
        // With ShowHistory=true, setValue() should push samples into a
        // ring buffer so the render pass can draw the trailing trace.
        // The buffer length is bounded by HistoryDuration + update rate —
        // we only assert non-zero on first few pushes here.
        BarItem b;
        b.setRange(-140.0, 0.0);
        b.setShowHistory(true);
        b.setValue(-90.0);
        b.setValue(-80.0);
        b.setValue(-70.0);
        QVERIFY(b.historySampleCount() >= 3);
    }

    void history_samples_dont_accumulate_when_disabled()
    {
        BarItem b;
        b.setRange(-140.0, 0.0);
        // ShowHistory defaults to false
        b.setValue(-90.0);
        b.setValue(-80.0);
        QCOMPARE(b.historySampleCount(), 0);
    }

    // ---- Phase A3: ShowMarker / MarkerColour / PeakHoldMarkerColour ----

    void showMarker_default_is_false()
    {
        BarItem b;
        QCOMPARE(b.showMarker(), false);
    }

    void showMarker_roundtrip()
    {
        // addSMeterBar sets ShowMarker = true (MeterManager.cs:21543).
        BarItem b;
        b.setShowMarker(true);
        QCOMPARE(b.showMarker(), true);
    }

    void markerColour_roundtrip()
    {
        BarItem b;
        const QColor orange(0xff, 0xa5, 0x00);
        b.setMarkerColour(orange);
        QCOMPARE(b.markerColour(), orange);
    }

    void peakHoldMarkerColour_roundtrip()
    {
        // addSMeterBar sets PeakHoldMarkerColour = Red
        // (MeterManager.cs:21544).
        BarItem b;
        const QColor red(0xff, 0x00, 0x00);
        b.setPeakHoldMarkerColour(red);
        QCOMPARE(b.peakHoldMarkerColour(), red);
    }

    void peakHold_decays_when_rate_is_nonzero()
    {
        // Thetis clsBarItem applies an independent decay to the peak-hold
        // marker value so it slowly drops back toward the live value.
        // With a decay ratio > 0, a subsequent lower setValue() should
        // nudge peakValue() downward from the prior high.
        BarItem b;
        b.setRange(-140.0, 0.0);
        b.setPeakHoldDecayRatio(0.5f);
        b.setValue(-50.0);           // peak = -50
        const double peakAfterHigh = b.peakValue();
        b.setValue(-80.0);           // decay kicks in
        QVERIFY2(b.peakValue() < peakAfterHigh,
                 "peakValue should decay toward live value when "
                 "PeakHoldDecayRatio > 0");
        QVERIFY2(b.peakValue() > -80.0,
                 "peak should not collapse all the way to the live value "
                 "in one step");
    }

    void peakHold_holds_when_rate_is_zero()
    {
        // Default behavior (A1) — no decay. Rate 0 means the marker sticks
        // at the highest value forever, matching the A1 test.
        BarItem b;
        b.setRange(-140.0, 0.0);
        b.setPeakHoldDecayRatio(0.0f);
        b.setValue(-40.0);
        b.setValue(-70.0);
        QCOMPARE(b.peakValue(), -40.0);
    }

    // ---- Phase A4: BarStyle::Line + ScaleCalibration non-linear map ----

    void barStyle_Line_roundtrip()
    {
        // Thetis clsBarItem.BarStyle enum (MeterManager.cs:19927-19934)
        // has None, Line, SolidFilled, GradientFilled, Segments. addSMeterBar
        // uses BarStyle.Line (MeterManager.cs:21546). NereusSDR already has
        // Filled + Edge; A4 adds Line.
        BarItem b;
        b.setBarStyle(BarItem::BarStyle::Line);
        QCOMPARE(b.barStyle(), BarItem::BarStyle::Line);
    }

    void scaleCalibration_defaults_empty()
    {
        BarItem b;
        QCOMPARE(b.scaleCalibrationSize(), 0);
    }

    void scaleCalibration_add_and_size()
    {
        BarItem b;
        b.addScaleCalibration(-133.0, 0.0f);
        b.addScaleCalibration(-73.0, 0.5f);
        b.addScaleCalibration(-13.0, 0.99f);
        QCOMPARE(b.scaleCalibrationSize(), 3);
    }

    void scaleCalibration_interpolates_thetis_SMeter_curve()
    {
        // From Thetis addSMeterBar (MeterManager.cs:21547-21549):
        //   -133 dBm -> x = 0.0    (S0 / bottom of scale)
        //   -73 dBm  -> x = 0.5    (S9)
        //   -13 dBm  -> x = 0.99   (S9 + 60 dB)
        // These are NON-LINEAR waypoints. A calibrated BarItem must
        // interpolate through them rather than doing linear min..max.
        BarItem b;
        b.addScaleCalibration(-133.0, 0.0f);
        b.addScaleCalibration(-73.0, 0.5f);
        b.addScaleCalibration(-13.0, 0.99f);

        // Exact waypoints
        QCOMPARE(b.valueToNormalizedX(-133.0), 0.0f);
        QCOMPARE(b.valueToNormalizedX(-73.0), 0.5f);
        QCOMPARE(b.valueToNormalizedX(-13.0), 0.99f);

        // Midpoint of first segment: -133 .. -73, halfway is -103 dBm -> 0.25
        const float midSeg1 = b.valueToNormalizedX(-103.0);
        QVERIFY2(std::abs(midSeg1 - 0.25f) < 1e-3f,
                 "midpoint of first calibration segment should lie at x=0.25");

        // Midpoint of second segment: -73 .. -13, halfway is -43 dBm
        // -> midway between 0.5 and 0.99 = 0.745
        const float midSeg2 = b.valueToNormalizedX(-43.0);
        QVERIFY2(std::abs(midSeg2 - 0.745f) < 1e-3f,
                 "midpoint of second calibration segment should lie at x=0.745");

        // Below-range clamp
        QCOMPARE(b.valueToNormalizedX(-200.0), 0.0f);
        // Above-range clamp (use the last waypoint, 0.99)
        QCOMPARE(b.valueToNormalizedX(100.0), 0.99f);
    }

    void scaleCalibration_empty_falls_back_to_linear_range()
    {
        // With no calibration waypoints, BarItem should keep its
        // legacy linear min..max behavior so existing Filled presets
        // don't regress.
        BarItem b;
        b.setRange(-140.0, 0.0);
        QCOMPARE(b.valueToNormalizedX(-140.0), 0.0f);
        QCOMPARE(b.valueToNormalizedX(0.0), 1.0f);
        const float mid = b.valueToNormalizedX(-70.0);
        QVERIFY2(std::abs(mid - 0.5f) < 1e-3f,
                 "linear fallback midpoint should be 0.5");
    }
};

QTEST_MAIN(TstMeterItemBar)
#include "tst_meter_item_bar.moc"
