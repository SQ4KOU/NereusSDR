// no-port-check: NereusSDR-original no-reading regressions (R-R3-13).
//
// Every numeric container meter item follows the rule the S-meter and the
// slice flag level bar already use: a reading at or below -400 dBm, or a
// non-finite one, is no reading.  Needles rest at the scale minimum, text
// shows "--" (with the unit where the item shows one), and history graphs
// skip the sample.  A real -140 dBm floor reading still shows as a number.

#include <QTest>

#include "gui/meters/HistoryGraphItem.h"
#include "gui/meters/MeterItem.h"
#include "gui/meters/SignalTextItem.h"
#include "gui/meters/TextOverlayItem.h"

#include <cmath>
#include <limits>

using namespace NereusSDR;

class TestMeterItemNoReading : public QObject {
    Q_OBJECT

private slots:
    void predicateMatchesTheSentinelRule()
    {
        QVERIFY(isNoMeterReading(-400.0));
        QVERIFY(isNoMeterReading(-1000.0));
        QVERIFY(isNoMeterReading(std::numeric_limits<double>::quiet_NaN()));
        QVERIFY(isNoMeterReading(std::numeric_limits<double>::infinity()));
        QVERIFY(isNoMeterReading(-std::numeric_limits<double>::infinity()));
        QVERIFY(!isNoMeterReading(-399.9));
        QVERIFY(!isNoMeterReading(-140.0));
        QVERIFY(!isNoMeterReading(-73.0));
        QVERIFY(!isNoMeterReading(0.0));
    }

    void formatValueShowsNoReadingPerUnit()
    {
        QCOMPARE(MeterItem::formatValue(-400.0f, MeterItem::MeterUnit::dBm),
                 QStringLiteral("--"));
        QCOMPARE(MeterItem::formatValue(-400.0f, MeterItem::MeterUnit::S),
                 QStringLiteral("--"));
        QCOMPARE(MeterItem::formatValue(-400.0f, MeterItem::MeterUnit::uV),
                 QStringLiteral("-- ") + QChar(0x00B5) + QStringLiteral("V"));
        QCOMPARE(MeterItem::formatValue(-140.0f, MeterItem::MeterUnit::dBm),
                 QStringLiteral("-140.0"));
    }

    void needleRestsAtScaleMinimumAndShowsDashes()
    {
        NeedleItem needle;
        for (int i = 0; i < 30; ++i) {
            needle.setValue(-73.0);
        }
        QVERIFY(needle.smoothedValue() > -80.0f);
        QCOMPARE(needle.sUnitsReadout(), QStringLiteral("S9"));
        QCOMPARE(needle.valueReadout(), QStringLiteral("-73 dBm"));

        needle.setValue(-400.0);
        QCOMPARE(needle.smoothedValue(), NeedleItem::kS0Dbm);
        QCOMPARE(needle.sUnitsReadout(), QStringLiteral("--"));
        QCOMPARE(needle.valueReadout(), QStringLiteral("-- dBm"));
        needle.setUnitMode(MeterItem::MeterUnit::S);
        QCOMPARE(needle.valueReadout(), QStringLiteral("--"));
        needle.setUnitMode(MeterItem::MeterUnit::dBm);

        // A real floor reading is a number again (the needle clamps to S0).
        needle.setValue(-140.0);
        QCOMPARE(needle.sUnitsReadout(), QStringLiteral("S0"));
        QCOMPARE(needle.valueReadout(), QStringLiteral("-127 dBm"));

        // Calibrated needles rest at the first calibration point.
        NeedleItem calibrated;
        QMap<float, QPointF> cal;
        cal.insert(0.0f, QPointF(0.1, 0.5));
        cal.insert(100.0f, QPointF(0.9, 0.5));
        calibrated.setScaleCalibration(cal);
        for (int i = 0; i < 30; ++i) {
            calibrated.setValue(60.0);
        }
        QVERIFY(calibrated.smoothedValue() > 50.0f);
        calibrated.setValue(-400.0);
        QCOMPARE(calibrated.smoothedValue(), 0.0f);
    }

    void textItemShowsDashesWithItsUnit()
    {
        TextItem text;
        text.setBindingId(0);
        text.setValue(-73.0);
        QCOMPARE(text.displayText(), QStringLiteral("-73.0 dBm"));
        text.setValue(-400.0);
        QCOMPARE(text.displayText(), QStringLiteral("-- dBm"));
        text.setValue(std::numeric_limits<double>::quiet_NaN());
        QCOMPARE(text.displayText(), QStringLiteral("-- dBm"));
        text.setUnitMode(MeterItem::MeterUnit::S);
        QCOMPARE(text.displayText(), QStringLiteral("--"));
        text.setUnitMode(MeterItem::MeterUnit::uV);
        QCOMPARE(text.displayText(), QStringLiteral("-- ") + QChar(0x00B5) + QStringLiteral("V"));
        text.setUnitMode(MeterItem::MeterUnit::dBm);
        text.setValue(-140.0);
        QCOMPARE(text.displayText(), QStringLiteral("-140.0 dBm"));

        TextItem watts;
        watts.setBindingId(0);
        watts.setSuffix(QStringLiteral(" W"));
        watts.setDecimals(0);
        watts.setValue(-400.0);
        QCOMPARE(watts.displayText(), QStringLiteral("-- W"));
    }

    void barValueAndPeakShowDashes()
    {
        BarItem bar;
        bar.setRange(-140.0, 0.0);
        bar.setShowValue(true);
        bar.setShowPeakValue(true);
        bar.setAttackRatio(1.0f);
        bar.setDecayRatio(1.0f);
        bar.setValue(-60.0);
        QCOMPARE(bar.valueText(), QStringLiteral("-60.0"));
        QCOMPARE(bar.peakValueText(), QStringLiteral("-60.0"));

        bar.setValue(-400.0);
        QCOMPARE(bar.valueText(), QStringLiteral("--"));
        QCOMPARE(bar.peakValueText(), QStringLiteral("--"));
        QCOMPARE(bar.smoothedValue(), bar.minVal());

        bar.setValue(-140.0);
        QCOMPARE(bar.valueText(), QStringLiteral("-140.0"));
        QCOMPARE(bar.peakValueText(), QStringLiteral("-140.0"));
    }

    void signalTextShowsDashesWithItsUnit()
    {
        SignalTextItem sig;
        sig.setPeakHold(true);
        sig.setShowPeakValue(true);
        for (int i = 0; i < 40; ++i) {
            sig.setValue(-73.0);
        }
        QCOMPARE(sig.valueText(), QStringLiteral("-73.0 dBm"));

        sig.setValue(-400.0);
        QCOMPARE(sig.valueText(), QStringLiteral("-- dBm"));
        QCOMPARE(sig.peakValueText(), QStringLiteral("-- dBm"));
        sig.setUnits(SignalTextItem::Units::SUnits);
        QCOMPARE(sig.valueText(), QStringLiteral("--"));
        sig.setUnits(SignalTextItem::Units::Uv);
        QCOMPARE(sig.valueText(), QStringLiteral("-- uV"));
        sig.setUnits(SignalTextItem::Units::Dbm);

        for (int i = 0; i < 5; ++i) {
            sig.setValue(-140.0);
        }
        QCOMPARE(sig.valueText(), QStringLiteral("-140.0 dBm"));
        QCOMPARE(sig.peakValueText(), QStringLiteral("-140.0 dBm"));
    }

    void textOverlayValueTokenShowsDashes()
    {
        TextOverlayItem overlay;
        overlay.setText1(QStringLiteral("Sig %VALUE% dBm"));
        overlay.setText2(QStringLiteral("%PRECIS=0%%VALUE%"));
        overlay.setValue(-73.26);
        QCOMPARE(overlay.resolvedText1(), QStringLiteral("Sig -73.3 dBm"));
        QCOMPARE(overlay.resolvedText2(), QStringLiteral("-73"));
        overlay.setValue(-400.0);
        QCOMPARE(overlay.resolvedText1(), QStringLiteral("Sig -- dBm"));
        QCOMPARE(overlay.resolvedText2(), QStringLiteral("--"));
        overlay.setValue(-140.0);
        QCOMPARE(overlay.resolvedText2(), QStringLiteral("-140"));
    }

    void historyGraphSkipsNoReadingSamples()
    {
        HistoryGraphItem history;
        history.setBindingId1(1);
        history.setValue(-73.0);
        history.setValue1(-80.0);
        QCOMPARE(history.sampleCount0(), 1);
        QCOMPARE(history.sampleCount1(), 1);

        history.setValue(-400.0);
        history.setValue1(-400.0);
        history.setValue(std::numeric_limits<double>::quiet_NaN());
        QCOMPARE(history.sampleCount0(), 1);
        QCOMPARE(history.sampleCount1(), 1);

        history.setValue(-140.0);
        history.setValue1(-140.0);
        QCOMPARE(history.sampleCount0(), 2);
        QCOMPARE(history.sampleCount1(), 2);
    }
};

QTEST_MAIN(TestMeterItemNoReading)
#include "tst_meter_item_no_reading.moc"
