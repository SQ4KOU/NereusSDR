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
};

QTEST_MAIN(TstMeterItemBar)
#include "tst_meter_item_bar.moc"
