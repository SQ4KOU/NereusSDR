// tst_meter_presets.cpp — ItemGroup preset composition parity tests.
//
// Phase D / E of the meter-parity port plan. Each factory test builds the
// preset, asserts it has the right number of items in the right z-order
// with bindings + ranges matching the Thetis source referenced in the
// factory's source comment.

#include <QtTest/QtTest>

#include "gui/meters/ItemGroup.h"
#include "gui/meters/MeterItem.h"
#include "gui/meters/MeterPoller.h"  // MeterBinding

using namespace NereusSDR;

class TstMeterPresets : public QObject
{
    Q_OBJECT

private:
    template <typename T>
    static T* findOfType(ItemGroup* group, int nth = 0)
    {
        int seen = 0;
        for (MeterItem* mi : group->items()) {
            if (T* cast = qobject_cast<T*>(mi)) {
                if (seen == nth) { return cast; }
                ++seen;
            }
        }
        return nullptr;
    }

    template <typename T>
    static int countOfType(ItemGroup* group)
    {
        int n = 0;
        for (MeterItem* mi : group->items()) {
            if (qobject_cast<T*>(mi)) { ++n; }
        }
        return n;
    }

private slots:
    // ---- Phase D1: createSMeterPreset rebuilt from addSMeterBar ----

    void SMeter_preset_is_now_a_bar_not_a_needle()
    {
        // Pre-D1 createSMeterPreset built a single bare NeedleItem.
        // After D1 it builds the Thetis addSMeterBar composition:
        // SolidColour bg (z=1), BarItem Line-style with calibration
        // (z=2), ScaleItem ShowType + GeneralScale (z=3), ClickBoxItem
        // (z=4). Needle count should be zero.
        ItemGroup* g = ItemGroup::createSMeterPreset(
            MeterBinding::SignalPeak, QStringLiteral("S-Meter"), nullptr);
        QVERIFY(g != nullptr);

        QCOMPARE(countOfType<NeedleItem>(g), 0);
        QCOMPARE(countOfType<BarItem>(g),    1);
        QCOMPARE(countOfType<ScaleItem>(g),  1);
        QCOMPARE(countOfType<SolidColourItem>(g), 1);

        delete g;
    }

    void SMeter_bar_matches_Thetis_addSMeterBar_calibration()
    {
        // Thetis addSMeterBar (MeterManager.cs:21529-21555):
        //   Style               = BarStyle.Line
        //   AttackRatio         = 0.8f
        //   DecayRatio          = 0.2f
        //   HistoryDuration     = 4000
        //   ShowHistory         = true
        //   ShowValue           = true
        //   Colour              = CadetBlue
        //   ShowMarker          = true
        //   PeakHoldMarkerColour = Red
        //   HistoryColour       = Color.FromArgb(128, Red)
        //   FontColour          = Yellow
        //   ScaleCalibration    = { -133->0.00, -73->0.50, -13->0.99 }
        ItemGroup* g = ItemGroup::createSMeterPreset(
            MeterBinding::SignalPeak, QStringLiteral("S-Meter"), nullptr);
        BarItem* bar = findOfType<BarItem>(g);
        QVERIFY(bar != nullptr);

        QCOMPARE(bar->barStyle(),            BarItem::BarStyle::Line);
        QCOMPARE(bar->attackRatio(),         0.8f);
        QCOMPARE(bar->decayRatio(),          0.2f);
        QCOMPARE(bar->historyDurationMs(),   4000);
        QCOMPARE(bar->showHistory(),         true);
        QCOMPARE(bar->showValue(),           true);
        QCOMPARE(bar->showMarker(),          true);
        QCOMPARE(bar->peakHoldMarkerColour(), QColor(Qt::red));
        QCOMPARE(bar->historyColour(),       QColor(255, 0, 0, 128));
        QCOMPARE(bar->fontColour(),          QColor(Qt::yellow));

        // Non-linear calibration — the core of the S-meter port.
        QCOMPARE(bar->scaleCalibrationSize(), 3);
        QCOMPARE(bar->valueToNormalizedX(-133.0), 0.0f);
        QCOMPARE(bar->valueToNormalizedX(-73.0),  0.5f);
        QCOMPARE(bar->valueToNormalizedX(-13.0),  0.99f);

        QCOMPARE(bar->bindingId(), MeterBinding::SignalPeak);

        delete g;
    }

    void SMeter_scale_has_showType_and_GeneralScale()
    {
        // Thetis addSMeterBar (MeterManager.cs:21557-21564):
        //   cs.ShowType = true
        //   cs.ReadingSource matches the bar's binding
        //   cs.ZOrder = 3
        // Plus the renderScale dispatch at MeterManager.cs:31911-31916
        // uses generalScale(6, 3, -1, 60, 2, 20, ..., 0.5f, true, true)
        // for SIGNAL_STRENGTH — we port that to the NereusSDR
        // GeneralScale params exactly.
        ItemGroup* g = ItemGroup::createSMeterPreset(
            MeterBinding::SignalPeak, QStringLiteral("S-Meter"), nullptr);
        ScaleItem* scale = findOfType<ScaleItem>(g);
        QVERIFY(scale != nullptr);

        QCOMPARE(scale->showType(),   true);
        QCOMPARE(scale->bindingId(),  MeterBinding::SignalPeak);
        QCOMPARE(scale->scaleStyle(), ScaleItem::ScaleStyle::GeneralScale);
        QCOMPARE(scale->lowLongTicks(),  6);
        QCOMPARE(scale->highLongTicks(), 3);

        delete g;
    }

    void SMeter_accepts_alternate_bindings()
    {
        // addSMeterBar is parameterised on the reading so the same
        // composition works for SIGNAL_STRENGTH, AVG_SIGNAL_STRENGTH,
        // and SIGNAL_MAX_BIN (MeterManager.cs:21499-21510).
        for (int b : {MeterBinding::SignalPeak,
                      MeterBinding::SignalAvg,
                      MeterBinding::SignalMaxBin}) {
            ItemGroup* g = ItemGroup::createSMeterPreset(
                b, QStringLiteral("SMeter"), nullptr);
            QVERIFY(g);
            BarItem* bar = findOfType<BarItem>(g);
            QVERIFY(bar);
            QCOMPARE(bar->bindingId(), b);
            QCOMPARE(bar->scaleCalibrationSize(), 3);
            delete g;
        }
    }
};

QTEST_MAIN(TstMeterPresets)
#include "tst_meter_presets.moc"
