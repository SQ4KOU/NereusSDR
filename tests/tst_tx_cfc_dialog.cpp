// no-port-check: NereusSDR-original test file.  All Thetis source cites
// for the underlying TransmitModel CFC properties live in TransmitModel.h
// and the dialog source itself.
// =================================================================
// tests/tst_tx_cfc_dialog.cpp  (NereusSDR)
// =================================================================
//
// Phase 3M-3a-ii follow-up sub-PR Batch 8 — TxCfcDialog
// Thetis-verbatim rewrite tests.
//
// TxCfcDialog is the modeless CFC editor launched from the TxApplet's
// [CFC] right-click and from CfcSetupPage's [Configure CFC bands…]
// button.  It now embeds two ParametricEqWidget instances (compression
// curve + post-EQ curve) cross-synced per Thetis frmCFCConfig.cs:218-306
// [v2.10.3.13], with a 50ms QTimer feeding the comp widget's bar chart
// from TxChannel::getCfcDisplayCompression (Task 7 wrapper).
//
// Tests:
//   1. Dialog constructs with two ParametricEqWidget instances + the
//      documented control surface (top + middle edit rows, right column
//      band-count radios / freq spinboxes / checkboxes / reset buttons /
//      OG CFC Guide link).
//   2. Initial values match TransmitModel CFC defaults seeded into the
//      widgets (cfcEqFreq[0]=0 / [9]=10000, cfcCompression[i]=5,
//      cfcPostEqBandGain[i]=0).
//   3. Bar chart timer starts on show, stops on hide.
//   4. closeEvent hides the dialog instead of destroying it.
//   5. Cross-sync: setSelectedIndex on comp widget mirrors to post-EQ
//      widget (and vice versa).
//   6. Band-count radio switching changes both widgets' point counts.
//   7. Freq-range spinbox change clamps both widgets' frequency
//      min/max envelopes.
//   8. Spinbox-driven precomp / post-EQ gain push through to TransmitModel.
//   9. Per-band Comp / Gain spinbox writes update the selected widget point
//      and the TransmitModel array.
//  10. External TM setter updates dialog spinboxes (model → UI sync).
//  11. OG CFC Guide button exists and triggers (we don't open the URL
//      under test; just verify the button is wired).
//  12. Use Q Factors checkbox toggles ParametricEq on both widgets.
//
// =================================================================

#include <QtTest/QtTest>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTimer>

#include "core/AppSettings.h"
#include "gui/applets/TxCfcDialog.h"
#include "gui/widgets/ParametricEqWidget.h"
#include "models/RadioModel.h"
#include "models/TransmitModel.h"

using namespace NereusSDR;

class TestTxCfcDialog : public QObject {
    Q_OBJECT

private slots:

    void initTestCase()
    {
        if (!qApp) {
            static int argc = 0;
            new QApplication(argc, nullptr);
        }
        AppSettings::instance().clear();
    }

    void cleanup()
    {
        AppSettings::instance().clear();
    }

    // ── 1. Dialog constructs with documented control surface ───────────
    void constructsWithExpectedControls()
    {
        RadioModel rm;
        TxCfcDialog dlg(&rm.transmitModel(), nullptr);

        // Two ParametricEqWidget instances embedded.
        const auto widgets = dlg.findChildren<ParametricEqWidget*>();
        QCOMPARE(widgets.size(), 2);
        QVERIFY(dlg.compWidget());
        QVERIFY(dlg.postEqWidget());
        QVERIFY(dlg.compWidget()   != dlg.postEqWidget());

        // Top edit row.
        QVERIFY(dlg.selectedBandSpin());
        QVERIFY(dlg.freqSpin());
        QVERIFY(dlg.precompSpin());
        QVERIFY(dlg.compSpin());
        QVERIFY(dlg.compQSpin());

        // Middle edit row.
        QVERIFY(dlg.postEqGainSpin());
        QVERIFY(dlg.gainSpin());
        QVERIFY(dlg.eqQSpin());

        // Right column.
        QVERIFY(dlg.bands5Radio());
        QVERIFY(dlg.bands10Radio());
        QVERIFY(dlg.bands18Radio());
        QVERIFY(dlg.lowSpin());
        QVERIFY(dlg.highSpin());
        QVERIFY(dlg.useQFactorsChk());
        QVERIFY(dlg.liveUpdateChk());
        QVERIFY(dlg.logScaleChk());
        QVERIFY(dlg.resetCompBtn());
        QVERIFY(dlg.resetEqBtn());
        QVERIFY(dlg.ogGuideLink());

        // Default radio selection: 10-band.
        QVERIFY(dlg.bands10Radio()->isChecked());
        QVERIFY(!dlg.bands5Radio()->isChecked());
        QVERIFY(!dlg.bands18Radio()->isChecked());
        QCOMPARE(dlg.currentBandCount(), 10);

        // Use Q Factors default = checked (Designer.cs:487 [v2.10.3.13]).
        QVERIFY(dlg.useQFactorsChk()->isChecked());

        // Modeless.
        QVERIFY(!dlg.isModal());
    }

    // ── 2. Initial values match TransmitModel CFC defaults ─────────────
    void initialValues_matchTmDefaults()
    {
        RadioModel rm;
        TransmitModel& tx = rm.transmitModel();
        TxCfcDialog dlg(&tx, nullptr);

        // Globals (top + middle edit rows).
        QCOMPARE(dlg.precompSpin()->value(),    static_cast<double>(tx.cfcPrecompDb()));
        QCOMPARE(dlg.postEqGainSpin()->value(), static_cast<double>(tx.cfcPostEqGainDb()));
        QCOMPARE(tx.cfcPrecompDb(),    0);
        QCOMPARE(tx.cfcPostEqGainDb(), 0);

        // The compression widget should hold the TM per-band defaults.
        // TransmitModel.h:1337 [v2.10.3.13] defines:
        //   m_cfcEqFreqHz       = {0, 125, 250, 500, 1000, 2000, 3000, 4000, 5000, 10000};
        //   m_cfcCompressionDb  = {5, 5, 5, 5, 5, 5, 5, 5, 5, 5};
        //   m_cfcPostEqBandGainDb = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        QVector<double> cf, cg, cq;
        dlg.compWidget()->getPointsData(cf, cg, cq);
        QCOMPARE(cf.size(), 10);
        QCOMPARE(static_cast<int>(std::round(cf[0])),    0);
        QCOMPARE(static_cast<int>(std::round(cf[9])),    10000);
        QCOMPARE(static_cast<int>(std::round(cg[0])),    5);

        QVector<double> ef, eg, eq;
        dlg.postEqWidget()->getPointsData(ef, eg, eq);
        QCOMPARE(ef.size(), 10);
        QCOMPARE(static_cast<int>(std::round(eg[0])),    0);
    }

    // ── 3. Bar chart timer starts on show, stops on hide ───────────────
    void barChartTimerStartsOnShow_StopsOnHide()
    {
        RadioModel rm;
        TxCfcDialog dlg(&rm.transmitModel(), nullptr);

        QVERIFY(dlg.barChartTimer());
        QVERIFY(!dlg.barChartTimer()->isActive());

        dlg.show();
        QApplication::processEvents();
        QVERIFY(dlg.barChartTimer()->isActive());
        QCOMPARE(dlg.barChartTimer()->interval(), 50);  // From Thetis cs:447

        dlg.hide();
        QApplication::processEvents();
        QVERIFY(!dlg.barChartTimer()->isActive());
    }

    // ── 4. closeEvent hides instead of destroys ─────────────────────────
    void closeEventHidesInsteadOfDestroying()
    {
        RadioModel rm;
        TxCfcDialog dlg(&rm.transmitModel(), nullptr);
        dlg.show();
        QApplication::processEvents();
        QVERIFY(dlg.isVisible());

        QCloseEvent ev;
        QApplication::sendEvent(&dlg, &ev);
        QApplication::processEvents();

        // Event was consumed (ignored) and the dialog was hidden but
        // not deleted — the QPointer is still valid.
        QVERIFY(!ev.isAccepted());
        QVERIFY(!dlg.isVisible());
    }

    // ── 5a. Cross-sync: comp.setSelectedIndex → post-EQ selects same idx ─
    //
    // Thetis frmCFCConfig.cs:240-246 [v2.10.3.13] — pointSelected
    // handler walks getIndexFromBandId on the other widget.  Since both
    // widgets are seeded with the same bandIds in resetPointsDefault
    // (1..bandCount) the index match is direct.
    void crossSync_compSelect_setsPostEq()
    {
        RadioModel rm;
        TxCfcDialog dlg(&rm.transmitModel(), nullptr);
        dlg.show();
        QApplication::processEvents();

        dlg.compWidget()->setSelectedIndex(3);
        QApplication::processEvents();

        QCOMPARE(dlg.postEqWidget()->selectedIndex(), 3);
    }

    // ── 5b. Cross-sync: post-EQ.setSelectedIndex → comp selects same idx ─
    void crossSync_postEqSelect_setsComp()
    {
        RadioModel rm;
        TxCfcDialog dlg(&rm.transmitModel(), nullptr);
        dlg.show();
        QApplication::processEvents();

        dlg.postEqWidget()->setSelectedIndex(7);
        QApplication::processEvents();

        QCOMPARE(dlg.compWidget()->selectedIndex(), 7);
    }

    // ── 6a. Band-count radio: switch to 5 changes both widgets' point counts ─
    void bandCountRadio_5_switchesBothWidgets()
    {
        RadioModel rm;
        TxCfcDialog dlg(&rm.transmitModel(), nullptr);

        QCOMPARE(dlg.compWidget()->bandCount(),   10);
        QCOMPARE(dlg.postEqWidget()->bandCount(), 10);

        dlg.bands5Radio()->setChecked(true);
        QApplication::processEvents();

        QCOMPARE(dlg.currentBandCount(), 5);
        QCOMPARE(dlg.compWidget()->bandCount(),   5);
        QCOMPARE(dlg.postEqWidget()->bandCount(), 5);
        QCOMPARE(dlg.selectedBandSpin()->maximum(), 5);
    }

    // ── 6b. Band-count radio: switch to 18 changes both widgets' point counts ─
    void bandCountRadio_18_switchesBothWidgets()
    {
        RadioModel rm;
        TxCfcDialog dlg(&rm.transmitModel(), nullptr);

        dlg.bands18Radio()->setChecked(true);
        QApplication::processEvents();

        QCOMPARE(dlg.currentBandCount(), 18);
        QCOMPARE(dlg.compWidget()->bandCount(),   18);
        QCOMPARE(dlg.postEqWidget()->bandCount(), 18);
        QCOMPARE(dlg.selectedBandSpin()->maximum(), 18);
    }

    // ── 7. Freq range spinbox changes clamp both widgets ───────────────
    //
    // Note: the dialog widens the freq envelope at construction to cover
    // the highest TM CFC freq (default cfcEqFreq[9]=10000 Hz).  So initial
    // High is 10000, not Thetis's nominal 4000.
    void freqRangeSpinbox_clampsBothWidgets()
    {
        RadioModel rm;
        TxCfcDialog dlg(&rm.transmitModel(), nullptr);

        // Initial envelope after seed: low=0, high=10000 (max TM freq).
        QCOMPARE(dlg.compWidget()->frequencyMinHz(),    0.0);
        QCOMPARE(dlg.compWidget()->frequencyMaxHz(),    10000.0);
        QCOMPARE(dlg.postEqWidget()->frequencyMinHz(),  0.0);
        QCOMPARE(dlg.postEqWidget()->frequencyMaxHz(),  10000.0);
        QCOMPARE(dlg.lowSpin()->value(),  0);
        QCOMPARE(dlg.highSpin()->value(), 10000);

        // Increase low to 200 Hz — should propagate to both widgets.
        dlg.lowSpin()->setValue(200);
        QApplication::processEvents();
        QCOMPARE(dlg.compWidget()->frequencyMinHz(),   200.0);
        QCOMPARE(dlg.postEqWidget()->frequencyMinHz(), 200.0);

        // Decrease high to 8000 Hz.
        dlg.highSpin()->setValue(8000);
        QApplication::processEvents();
        QCOMPARE(dlg.compWidget()->frequencyMaxHz(),   8000.0);
        QCOMPARE(dlg.postEqWidget()->frequencyMaxHz(), 8000.0);
    }

    // ── 7b. Freq range clamp guard: low cannot exceed high - 1000 Hz ───
    //
    // Thetis frmCFCConfig.cs:122-126 [v2.10.3.13] enforces the 1 kHz
    // minimum spread.  Initial high = 10000 (TM seed widens envelope) —
    // setting low to 10000 should clamp it back to 9000 (high - 1000).
    void freqRangeSpinbox_clampGuardEnforces1kHzSpread()
    {
        RadioModel rm;
        TxCfcDialog dlg(&rm.transmitModel(), nullptr);

        QCOMPARE(dlg.highSpin()->value(), 10000);
        dlg.lowSpin()->setValue(10000);
        QApplication::processEvents();
        QCOMPARE(dlg.lowSpin()->value(), 9000);
    }

    // ── 8a. Precomp spin → setCfcPrecompDb ─────────────────────────────
    void precompSpin_drivesTm()
    {
        RadioModel rm;
        TransmitModel& tx = rm.transmitModel();
        TxCfcDialog dlg(&tx, nullptr);
        QSignalSpy spy(&tx, &TransmitModel::cfcPrecompDbChanged);

        dlg.precompSpin()->setValue(10.0);
        QApplication::processEvents();

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toInt(), 10);
        QCOMPARE(tx.cfcPrecompDb(), 10);
    }

    // ── 8b. Post-EQ gain spin → setCfcPostEqGainDb ─────────────────────
    void postEqGainSpin_drivesTm()
    {
        RadioModel rm;
        TransmitModel& tx = rm.transmitModel();
        TxCfcDialog dlg(&tx, nullptr);
        QSignalSpy spy(&tx, &TransmitModel::cfcPostEqGainDbChanged);

        dlg.postEqGainSpin()->setValue(-12.0);
        QApplication::processEvents();

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toInt(), -12);
        QCOMPARE(tx.cfcPostEqGainDb(), -12);
    }

    // ── 9a. Per-band Comp spin (selected band) → updates TM array ──────
    //
    // Select band 5 on the comp widget, then drive nudCFC_c (compSpin):
    // - widget point 5 gain should reflect the new value
    // - TM cfcCompression(5) should reflect the new value
    void compSpin_drivesSelectedBand()
    {
        RadioModel rm;
        TransmitModel& tx = rm.transmitModel();
        TxCfcDialog dlg(&tx, nullptr);
        dlg.show();
        QApplication::processEvents();

        // Select band index 5.
        dlg.compWidget()->setSelectedIndex(5);
        QApplication::processEvents();

        QSignalSpy spy(&tx, &TransmitModel::cfcCompressionChanged);
        dlg.compSpin()->setValue(12.0);
        QApplication::processEvents();

        // Verify widget point 5 gain.
        double f = 0.0, g = 0.0, q = 0.0;
        dlg.compWidget()->getPointData(5, f, g, q);
        QCOMPARE(g, 12.0);

        // Verify TM band 5 compression.  At least one signal fires for the
        // explicit setCfcCompression(5, 12) call; cross-sync may also fire
        // additional signals from the per-band push, so use >= 1.
        QVERIFY(spy.count() >= 1);
        QCOMPARE(tx.cfcCompression(5), 12);
    }

    // ── 9b. Per-band Gain spin (selected band, post-EQ) → updates TM array ─
    void gainSpin_drivesSelectedBand_postEq()
    {
        RadioModel rm;
        TransmitModel& tx = rm.transmitModel();
        TxCfcDialog dlg(&tx, nullptr);
        dlg.show();
        QApplication::processEvents();

        // Select band index 7.
        dlg.postEqWidget()->setSelectedIndex(7);
        QApplication::processEvents();

        QSignalSpy spy(&tx, &TransmitModel::cfcPostEqBandGainChanged);
        dlg.gainSpin()->setValue(-6.0);
        QApplication::processEvents();

        // Verify widget point 7 gain.
        double f = 0.0, g = 0.0, q = 0.0;
        dlg.postEqWidget()->getPointData(7, f, g, q);
        QCOMPARE(g, -6.0);

        QVERIFY(spy.count() >= 1);
        QCOMPARE(tx.cfcPostEqBandGain(7), -6);
    }

    // ── 10. External TM setter updates dialog (model → UI sync) ────────
    //
    // Note: per-band freq updates re-sort the widget's points by frequency
    // (ParametricEqWidget::enforceOrdering — Thetis ucParametricEq.cs:3223
    // [v2.10.3.13]).  After a freq change the value can land at a different
    // widget index — the bandId stays pinned to the band, but the index
    // position shifts to keep the array sorted.  We verify the value
    // appears SOMEWHERE in the widget rather than at a fixed index.
    void externalSetters_updateDialog()
    {
        RadioModel rm;
        TransmitModel& tx = rm.transmitModel();
        TxCfcDialog dlg(&tx, nullptr);

        tx.setCfcPrecompDb(8);
        QApplication::processEvents();
        QCOMPARE(dlg.precompSpin()->value(), 8.0);

        tx.setCfcPostEqGainDb(-3);
        QApplication::processEvents();
        QCOMPARE(dlg.postEqGainSpin()->value(), -3.0);

        // External per-band freq write — should appear in the widget after
        // the sort.  Default cfcEqFreq[2]=250; setting to 1500 puts it
        // between original indices 4 (1000) and 5 (2000) post-sort.
        tx.setCfcEqFreq(2, 1500);
        QApplication::processEvents();
        QVector<double> cf, cg, cq;
        dlg.compWidget()->getPointsData(cf, cg, cq);
        bool found1500 = false;
        for (double f : cf) {
            if (static_cast<int>(std::round(f)) == 1500) { found1500 = true; break; }
        }
        QVERIFY2(found1500, "External setCfcEqFreq value did not propagate to widget");

        // External per-band post-EQ gain write — gains don't affect ordering,
        // so the value should land at the widget index matching its bandId.
        // Default bandId-to-index for sorted defaults: bandId 5 = index 4.
        tx.setCfcPostEqBandGain(4, 9);
        QApplication::processEvents();
        QVector<double> ef, eg, eq;
        dlg.postEqWidget()->getPointsData(ef, eg, eq);
        bool foundGain9 = false;
        for (double g : eg) {
            if (static_cast<int>(std::round(g)) == 9) { foundGain9 = true; break; }
        }
        QVERIFY2(foundGain9, "External setCfcPostEqBandGain value did not propagate to widget");
    }

    // ── 11. OG CFC Guide button is wired (smoke test — we don't open URL) ─
    void ogGuideLink_isClickable()
    {
        RadioModel rm;
        TxCfcDialog dlg(&rm.transmitModel(), nullptr);

        QVERIFY(dlg.ogGuideLink()->isEnabled());
        QVERIFY(dlg.ogGuideLink()->cursor().shape() == Qt::PointingHandCursor);
        // We don't actually click() to avoid spawning a browser during CI.
    }

    // ── 12. Use Q Factors checkbox toggles ParametricEq on both widgets ─
    void useQFactorsCheckbox_togglesParametricEqOnBothWidgets()
    {
        RadioModel rm;
        TxCfcDialog dlg(&rm.transmitModel(), nullptr);

        QVERIFY(dlg.compWidget()->parametricEq());
        QVERIFY(dlg.postEqWidget()->parametricEq());

        dlg.useQFactorsChk()->setChecked(false);
        QApplication::processEvents();

        QVERIFY(!dlg.compWidget()->parametricEq());
        QVERIFY(!dlg.postEqWidget()->parametricEq());
    }

    // ── 13. Log scale checkbox toggles logScale on both widgets ────────
    void logScaleCheckbox_togglesBothWidgets()
    {
        RadioModel rm;
        TxCfcDialog dlg(&rm.transmitModel(), nullptr);

        QVERIFY(!dlg.compWidget()->logScale());
        QVERIFY(!dlg.postEqWidget()->logScale());

        dlg.logScaleChk()->setChecked(true);
        QApplication::processEvents();

        QVERIFY(dlg.compWidget()->logScale());
        QVERIFY(dlg.postEqWidget()->logScale());
    }

    // ── 14. Reset Comp button restores flat curve to comp widget ──────
    void resetCompButton_restoresFlatComp()
    {
        RadioModel rm;
        TransmitModel& tx = rm.transmitModel();
        TxCfcDialog dlg(&tx, nullptr);

        // Mutate the comp widget away from defaults.
        tx.setCfcPrecompDb(8);
        tx.setCfcCompression(3, 12);
        QApplication::processEvents();

        dlg.resetCompBtn()->click();
        QApplication::processEvents();

        // Comp widget global gain reset to 0.
        QCOMPARE(dlg.compWidget()->globalGainDb(), 0.0);
        // All comp point gains reset to 0.
        QVector<double> cf, cg, cq;
        dlg.compWidget()->getPointsData(cf, cg, cq);
        for (double g : cg) {
            QCOMPARE(g, 0.0);
        }
        // Post-EQ widget untouched (still default flat 0).
        QCOMPARE(dlg.postEqWidget()->globalGainDb(), 0.0);
    }

    // ── 15. Reset EQ button restores flat curve to post-EQ widget ──────
    void resetEqButton_restoresFlatEq()
    {
        RadioModel rm;
        TransmitModel& tx = rm.transmitModel();
        TxCfcDialog dlg(&tx, nullptr);

        tx.setCfcPostEqGainDb(-6);
        tx.setCfcPostEqBandGain(4, 9);
        QApplication::processEvents();

        dlg.resetEqBtn()->click();
        QApplication::processEvents();

        QCOMPARE(dlg.postEqWidget()->globalGainDb(), 0.0);
        QVector<double> ef, eg, eq;
        dlg.postEqWidget()->getPointsData(ef, eg, eq);
        for (double g : eg) {
            QCOMPARE(g, 0.0);
        }
    }

    // ── 16. Selected-band spinbox drives both widget selections ────────
    //
    // Thetis frmCFCConfig.cs:465-475 [v2.10.3.13] — typing in
    // nudCFC_selected_band updates both ucCFC_comp.SelectedIndex and
    // ucCFC_eq.SelectedIndex.
    void selectedBandSpin_drivesBothWidgetSelections()
    {
        RadioModel rm;
        TxCfcDialog dlg(&rm.transmitModel(), nullptr);

        // Default selectedBand value is 10 (1-based) → both widgets at -1
        // until user activates.
        // Force a different value via the spinbox.
        dlg.selectedBandSpin()->setValue(4);
        QApplication::processEvents();

        // 1-based → 0-based: 4 → 3.
        QCOMPARE(dlg.compWidget()->selectedIndex(),   3);
        QCOMPARE(dlg.postEqWidget()->selectedIndex(), 3);
    }
};

QTEST_MAIN(TestTxCfcDialog)
#include "tst_tx_cfc_dialog.moc"
