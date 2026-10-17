// =================================================================
// tests/tst_spectrum_widget_display_signals.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. The display change announcements are
// a NereusSDR construct (several views of one pan's display settings: the
// Display pill, Setup, the overlay menu). Thetis has one Setup form that
// owns its controls and nothing to port.
//
// Display pill plan Task 2 (2026-10-10). A control that shows a display
// setting has to hear when something else changes it. Most display setters
// on SpectrumWidget stored the value and told nobody, so a second view
// went stale. Every one of them now emits
// displaySettingChanged(<key base as stored>) once per change, and
// setClarityActive() emits clarityActiveChanged(bool).
//
// What is pinned here:
//   - each listed setter: one emission with its key base on a change, none
//     when the same value is set again;
//   - loadSettings() announces nothing, on this pan or any other;
//   - resetPanDisplayOverrides() announces nothing but panDisplayReset();
//   - a setting stored once for every pan makes each pan announce once;
//   - setClarityActive() has a same-value guard;
//   - "TX filter on the waterfall" is on for a widget that has not loaded
//     settings, as it is for one that has.
//
// Isolation: TestSandboxInit redirects the settings path away from the
// operator's profile, and init()/cleanup() clear the in-process store
// around every slot (as tst_pan_display_settings_inherit does).
// =================================================================

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QWheelEvent>

#include <functional>

#include "core/AppSettings.h"
#include "core/settings/PanDisplayKeys.h"
#include "gui/SpectrumWidget.h"

using namespace NereusSDR;
using namespace Qt::StringLiterals;

namespace {
using Change = std::function<void(SpectrumWidget&)>;

// How many times `spy` (on displaySettingChanged) saw `keyBase`.
int countOf(const QSignalSpy& spy, const QString& keyBase)
{
    int n = 0;
    for (const QList<QVariant>& args : spy) {
        if (args.value(0).toString() == keyBase) { ++n; }
    }
    return n;
}

QString seen(const QSignalSpy& spy)
{
    QStringList keys;
    for (const QList<QVariant>& args : spy) {
        keys.append(args.value(0).toString());
    }
    return keys.join(u", "_s);
}
} // namespace

Q_DECLARE_METATYPE(Change)

class TestSpectrumWidgetDisplaySignals : public QObject
{
    Q_OBJECT

private slots:
    void init()    { AppSettings::instance().clear(); }
    void cleanup() { AppSettings::instance().clear(); }

    // ── 1. Every listed setter announces its key base, once ─────────────
    //
    // Each change moves the setting off the value a widget loads from an
    // empty store, so the first call is a change and the second is not.
    void a_setter_announces_its_key_base_once_data()
    {
        QTest::addColumn<QString>("keyBase");
        QTest::addColumn<Change>("change");

        auto row = [](const char* setter, const QString& keyBase, Change change) {
            QTest::newRow(setter) << keyBase << change;
        };

        row("setFillColor", u"DisplayFillColor"_s,
            [](SpectrumWidget& w) { w.setFillColor(QColor(1, 2, 3)); });
        row("setGridEnabled", u"DisplayGridEnabled"_s,
            [](SpectrumWidget& w) { w.setGridEnabled(false); });
        row("setCursorFreqVisible", u"DisplayShowCursorFreq"_s,
            [](SpectrumWidget& w) { w.setCursorFreqVisible(false); });
        row("setPeakHoldDelayMs", u"DisplayPeakHoldResetMs"_s,
            [](SpectrumWidget& w) { w.setPeakHoldDelayMs(3500); });
        row("setGradientEnabled", u"DisplayGradientEnabled"_s,
            [](SpectrumWidget& w) { w.setGradientEnabled(true); });
        row("setShowNoiseFloor", u"DisplayShowNoiseFloor"_s,
            [](SpectrumWidget& w) { w.setShowNoiseFloor(true); });
        row("setShowBinWidth", u"DisplayShowBinWidth"_s,
            [](SpectrumWidget& w) { w.setShowBinWidth(true); });
        row("setShowPeakValueOverlay", u"DisplayShowPeakValueOverlay"_s,
            [](SpectrumWidget& w) { w.setShowPeakValueOverlay(true); });
        row("setLineWidth", u"DisplayLineWidth"_s,
            [](SpectrumWidget& w) { w.setLineWidth(3.0F); });
        row("setSpectrumDetector", u"DisplaySpectrumDetector"_s,
            [](SpectrumWidget& w) { w.setSpectrumDetector(SpectrumDetector::Average); });
        row("setSpectrumAveraging", u"DisplaySpectrumAveraging"_s,
            [](SpectrumWidget& w) { w.setSpectrumAveraging(SpectrumAveraging::None); });
        row("setSpectrumAverageTimeMs", u"DisplaySpectrumAverageTimeMs"_s,
            [](SpectrumWidget& w) { w.setSpectrumAverageTimeMs(250); });
        row("setDbmCalOffset", u"DisplayCalOffset"_s,
            [](SpectrumWidget& w) { w.setDbmCalOffset(2.5F); });
        row("setWfAgcEnabled", u"DisplayWfAgc"_s,
            [](SpectrumWidget& w) { w.setWfAgcEnabled(false); });
        row("setWaterfallNFAGCEnabled", u"WaterfallNFAGCEnabled"_s,
            [](SpectrumWidget& w) { w.setWaterfallNFAGCEnabled(true); });
        row("setWaterfallAGCOffsetDb", u"WaterfallAGCOffsetDb"_s,
            [](SpectrumWidget& w) { w.setWaterfallAGCOffsetDb(5); });
        row("setWfLowThreshold", u"DisplayWfLowLevel"_s,
            [](SpectrumWidget& w) { w.setWfLowThreshold(-100.0F); });
        row("setWfHighThreshold", u"DisplayWfHighLevel"_s,
            [](SpectrumWidget& w) { w.setWfHighThreshold(-40.0F); });
        row("setWfUseSpectrumMinMax", u"DisplayWfUseSpectrumMinMax"_s,
            [](SpectrumWidget& w) { w.setWfUseSpectrumMinMax(true); });
        row("setWfUpdatePeriodMs", u"DisplayWfUpdatePeriodMs"_s,
            [](SpectrumWidget& w) { w.setWfUpdatePeriodMs(100); });
        row("setWaterfallAveraging", u"DisplayWaterfallAveraging"_s,
            [](SpectrumWidget& w) { w.setWaterfallAveraging(SpectrumAveraging::Recursive); });
        row("setWaterfallAverageTimeMs", u"DisplayWaterfallAverageTimeMs"_s,
            [](SpectrumWidget& w) { w.setWaterfallAverageTimeMs(300); });
        row("setWfOpacity", u"DisplayWfOpacity"_s,
            [](SpectrumWidget& w) { w.setWfOpacity(50); });
        row("setWaterfallStopOnTx", u"WaterfallStopOnTx"_s,
            [](SpectrumWidget& w) { w.setWaterfallStopOnTx(true); });
        row("setShowRxFilterOnWaterfall", u"DisplayShowRxFilterOnWaterfall"_s,
            [](SpectrumWidget& w) { w.setShowRxFilterOnWaterfall(true); });
        row("setShowTxFilterOnRxWaterfall", u"DisplayShowTxFilterOnRxWaterfall"_s,
            [](SpectrumWidget& w) { w.setShowTxFilterOnRxWaterfall(false); });
        row("setShowRxZeroLineOnWaterfall", u"DisplayShowRxZeroLine"_s,
            [](SpectrumWidget& w) { w.setShowRxZeroLineOnWaterfall(true); });
        row("setShowTxZeroLineOnWaterfall", u"DisplayShowTxZeroLine"_s,
            [](SpectrumWidget& w) { w.setShowTxZeroLineOnWaterfall(true); });
        row("setWfTimestampPosition", u"DisplayWfTimestampPos"_s,
            [](SpectrumWidget& w) {
                w.setWfTimestampPosition(SpectrumWidget::TimestampPosition::Left);
            });
        row("setWaterfallHistoryMs", u"DisplayWaterfallHistoryMs"_s,
            [](SpectrumWidget& w) { w.setWaterfallHistoryMs(5 * 60 * 1000); });
        // Both ends move, so both keys are named, once each.
        row("setDbmRange (top)", u"DisplayGridMax"_s,
            [](SpectrumWidget& w) { w.setDbmRange(-130.0F, -30.0F); });
        row("setDbmRange (bottom)", u"DisplayGridMin"_s,
            [](SpectrumWidget& w) { w.setDbmRange(-130.0F, -30.0F); });
    }

    void a_setter_announces_its_key_base_once()
    {
        QFETCH(QString, keyBase);
        QFETCH(Change, change);

        SpectrumWidget pan;
        pan.loadSettings();
        QSignalSpy spy(&pan, &SpectrumWidget::displaySettingChanged);

        change(pan);
        QVERIFY2(countOf(spy, keyBase) == 1,
            qPrintable(u"a change should announce %1 once; saw [%2]"_s
                           .arg(keyBase, seen(spy))));

        change(pan);
        QVERIFY2(countOf(spy, keyBase) == 1,
            qPrintable(u"the same value again should announce nothing; saw [%1]"_s
                           .arg(seen(spy))));
    }

    // Only the end that moved is named.
    void set_dbm_range_names_only_the_end_that_moved()
    {
        SpectrumWidget pan;
        pan.loadSettings();
        const float top = pan.refLevel();
        QSignalSpy spy(&pan, &SpectrumWidget::displaySettingChanged);

        pan.setDbmRange(top - 90.0F, top);

        QCOMPARE(countOf(spy, u"DisplayGridMin"_s), 1);
        QCOMPARE(countOf(spy, u"DisplayGridMax"_s), 0);
    }

    // peakHoldEnabledChanged(bool) is that setting's signal; it does not
    // get a second one.
    void peak_hold_keeps_its_own_signal()
    {
        SpectrumWidget pan;
        pan.loadSettings();
        QSignalSpy own(&pan, &SpectrumWidget::peakHoldEnabledChanged);
        QSignalSpy spy(&pan, &SpectrumWidget::displaySettingChanged);

        pan.setPeakHoldEnabled(true);

        QCOMPARE(own.count(), 1);
        QCOMPARE(spy.count(), 0);
    }

    // ── 2. A load announces nothing ─────────────────────────────────────
    //
    // The stored values include the two a load applies through a setter
    // (peak hold and the peak value readout), and the readout is one of
    // the settings a setter passes on to every other pan.
    void a_load_announces_nothing()
    {
        auto& s = AppSettings::instance();
        s.setValue(u"DisplayGridEnabled"_s, u"False"_s);
        s.setValue(u"DisplayFillColor"_s, u"#FF102030"_s);
        s.setValue(u"DisplayWfAgc"_s, u"False"_s);
        s.setValue(u"WaterfallNFAGCEnabled"_s, u"True"_s);
        s.setValue(u"DisplayWfOpacity"_s, u"40"_s);
        s.setValue(u"DisplayWfTimestampPos"_s, u"1"_s);
        s.setValue(u"DisplayPeakHoldEnabled"_s, u"True"_s);
        s.setValue(u"DisplayShowNoiseFloor"_s, u"True"_s);
        s.setValue(u"DisplayShowPeakValueOverlay"_s, u"True"_s);
        s.setValue(u"DisplayGridMax"_s, u"-30"_s);

        SpectrumWidget other;   // already open, already loaded
        other.setPanIndex(1);
        other.loadSettings();
        QVERIFY(other.showPeakValueOverlay());

        SpectrumWidget pan;
        QSignalSpy spy(&pan, &SpectrumWidget::displaySettingChanged);
        QSignalSpy otherSpy(&other, &SpectrumWidget::displaySettingChanged);

        pan.loadSettings();

        // The values did load.
        QVERIFY(!pan.gridEnabled());
        QVERIFY(!pan.wfAgcEnabled());
        QVERIFY(pan.showNoiseFloor());
        QVERIFY(pan.showPeakValueOverlay());
        QVERIFY(pan.peakHoldEnabled());

        QVERIFY2(spy.isEmpty(), qPrintable(u"the loading pan announced [%1]"_s.arg(seen(spy))));
        QVERIFY2(otherSpy.isEmpty(),
                 qPrintable(u"the other pan announced [%1]"_s.arg(seen(otherSpy))));
        QVERIFY(other.showPeakValueOverlay());
    }

    // A setter still announces after a load: the hold ends with the load.
    void a_setter_announces_again_after_a_load()
    {
        SpectrumWidget pan;
        pan.loadSettings();
        QSignalSpy spy(&pan, &SpectrumWidget::displaySettingChanged);

        pan.setGridEnabled(false);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), u"DisplayGridEnabled"_s);
    }

    // ── 3. A pan reset says panDisplayReset(), nothing else ─────────────
    //
    // The reset puts the timestamp back through its setter, which would
    // otherwise announce.
    void a_pan_reset_announces_only_the_reset()
    {
        SpectrumWidget pan;
        pan.loadSettings();
        pan.setWfTimestampPosition(SpectrumWidget::TimestampPosition::Left);
        pan.setGridEnabled(false);
        pan.setWfOpacity(40);

        QSignalSpy spy(&pan, &SpectrumWidget::displaySettingChanged);
        QSignalSpy reset(&pan, &SpectrumWidget::panDisplayReset);

        pan.resetPanDisplayOverrides();

        QCOMPARE(pan.wfTimestampPosition(), SpectrumWidget::TimestampPosition::None);
        QVERIFY(pan.gridEnabled());
        QCOMPARE(reset.count(), 1);
        QVERIFY2(spy.isEmpty(), qPrintable(u"the reset announced [%1]"_s.arg(seen(spy))));

        // And the hold ended with it.
        pan.setGridEnabled(false);
        QCOMPARE(spy.count(), 1);
    }

    // ── 4. A setting stored once for every pan ──────────────────────────
    //
    // The setter hands the value to every other pan. Each pan announces
    // its own change once, and the equal-value guard ends the fan-out.
    void a_shared_setting_makes_each_pan_announce_once()
    {
        SpectrumWidget a;
        a.loadSettings();
        SpectrumWidget b;
        b.setPanIndex(1);
        b.loadSettings();
        SpectrumWidget c;
        c.setPanIndex(2);
        c.loadSettings();

        QSignalSpy spyA(&a, &SpectrumWidget::displaySettingChanged);
        QSignalSpy spyB(&b, &SpectrumWidget::displaySettingChanged);
        QSignalSpy spyC(&c, &SpectrumWidget::displaySettingChanged);

        const QColor first(17, 34, 51, 68);
        const QColor second(85, 102, 119, 136);
        a.setRxFilterColor(first);

        QCOMPARE(a.rxFilterColor(), first);
        QCOMPARE(b.rxFilterColor(), first);
        QCOMPARE(c.rxFilterColor(), first);
        for (const QSignalSpy* spy : { &spyA, &spyB, &spyC }) {
            QVERIFY2(spy->count() == 1, qPrintable(u"saw [%1]"_s.arg(seen(*spy))));
            QCOMPARE(spy->at(0).at(0).toString(), u"DisplayRxFilterColor"_s);
        }

        // From another pan, back the other way.
        b.setRxFilterColor(second);

        QCOMPARE(a.rxFilterColor(), second);
        QCOMPARE(b.rxFilterColor(), second);
        QCOMPARE(c.rxFilterColor(), second);
        QCOMPARE(spyA.count(), 2);
        QCOMPARE(spyB.count(), 2);
        QCOMPARE(spyC.count(), 2);
    }

    // ── 5. Clarity ──────────────────────────────────────────────────────
    void clarity_active_announces_a_change_only()
    {
        SpectrumWidget pan;
        QSignalSpy spy(&pan, &SpectrumWidget::clarityActiveChanged);

        pan.setClarityActive(true);
        pan.setClarityActive(true);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), true);
        QVERIFY(pan.clarityActive());

        pan.setClarityActive(false);

        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(0).toBool(), false);
    }

    // ── 6. TX filter on the waterfall ───────────────────────────────────
    //
    // The member said off until a load said on, so a widget that had not
    // loaded (and anything reading it before the load) disagreed with one
    // that had.
    void tx_filter_on_waterfall_is_on_before_and_after_a_load()
    {
        SpectrumWidget pan;
        QVERIFY(pan.showTxFilterOnRxWaterfall());

        pan.loadSettings();
        QVERIFY(pan.showTxFilterOnRxWaterfall());
    }

    // ── Fix wave I3: Shift+wheel on the trace moves the ref level ───────
    //
    // The range is the band's, so in receive the pan reports the range the
    // gesture ended on (as the dB strip does) and MainWindow stores it.
    // Without the report the next push from the band store put the old
    // range back. While the pan shows the transmit grid the edit is live
    // only. The step is 5 dB, up for a wheel turned away.
    void shift_wheel_reports_the_range_in_receive_only()
    {
        SpectrumWidget pan;
        pan.resize(800, 400);
        const auto shiftWheel = [&pan](int angleDelta) {
            const QPointF at(200.0, 60.0);   // on the trace, off the dB strip
            QWheelEvent wheel(at, pan.mapToGlobal(at), QPoint(), QPoint(0, angleDelta),
                              Qt::NoButton, Qt::ShiftModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(&pan, &wheel);
        };

        QSignalSpy reported(&pan, &SpectrumWidget::dbmRangeChangeRequested);
        QVERIFY(reported.isValid());
        const float top = pan.refLevel();
        const float span = pan.dynamicRange();

        shiftWheel(120);
        QCOMPARE(pan.refLevel(), top + 5.0f);
        QCOMPARE(pan.dynamicRange(), span);
        QCOMPARE(reported.count(), 1);
        QCOMPARE(reported.at(0).at(0).toFloat(), top + 5.0f - span);
        QCOMPARE(reported.at(0).at(1).toFloat(), top + 5.0f);

        shiftWheel(-120);
        QCOMPARE(pan.refLevel(), top);
        QCOMPARE(reported.count(), 2);
        QCOMPARE(reported.at(1).at(1).toFloat(), top);

        // Keyed: the transmit grid moves, nothing is reported.
        pan.setMoxOverlay(true);
        const float txTop = pan.refLevel();
        shiftWheel(-120);
        QCOMPARE(pan.refLevel(), txTop - 5.0f);
        QCOMPARE(reported.count(), 2);
    }
};

QTEST_MAIN(TestSpectrumWidgetDisplaySignals)
#include "tst_spectrum_widget_display_signals.moc"
