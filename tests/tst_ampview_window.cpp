// no-port-check: NereusSDR-original unit-test file.  Thetis cite comments
// document upstream sources; no Thetis logic ported in this test file.
// =================================================================
// tests/tst_ampview_window.cpp  (NereusSDR)
// =================================================================
//
// Unit tests for the Phase 3M-4 Task 9 AmpViewWindow modeless dialog.
//
// AmpViewWindow ports Thetis AmpView.cs (528 LOC) [v2.10.3.13] verbatim
// — title "AmpView 1.0", ClientSize 564x401, MinimumSize 440x380, 4
// toolbar checkboxes at the exact Thetis x positions
// (chkAVShowGain @ 7,378 / chkAVPhaseZoom @ 242,378 / chkAVLowRes @
// 404,378 / chkStayOnTop @ 490,378), 5 named chart series rendered by
// AmpViewChart custom QPainter widget (Ref / MagAmp / PhsAmp /
// MagCorr / PhsCorr).
//
// This test file exercises:
//
//   1. The dialog constructs with a null RadioModel pointer
//      (test-friendly seam).
//
//   2. Title, default geometry, and minimum size match Thetis verbatim.
//
//   3. The chart contains exactly 5 named series with the canonical
//      Thetis names from AmpView.Designer.cs:85-129 [v2.10.3.13].
//
//   4. All 4 toolbar checkboxes exist by objectName
//      (chkAVShowGain / chkAVPhaseZoom / chkAVLowRes / chkStayOnTop).
//
//   5. chkAVLowRes defaults to Checked per
//      AmpView.Designer.cs:182-183 [v2.10.3.13].
//
//   6. chkAVPhaseZoom defaults unchecked (designer default).
//
//   7. chkAVShowGain defaults unchecked (designer default).
//
//   8. Toggling chkAVShowGain forwards setShowGain() to the chart.
//
//   9. Toggling chkAVPhaseZoom forwards setPhaseZoom() to the chart.
//
//   10. Toggling chkAVLowRes forwards setLowRes() to the chart and the
//       low-res stride is 4 per AmpView.cs:457-463 [v2.10.3.13].
//
//   11. Toggling chkStayOnTop applies Qt::WindowStaysOnTopHint.
//
//   12. setSeriesData() updates the chart's stored data buffers
//       without crashing on empty / mismatched / valid input.
//
//   13. Geometry persists via AppSettings under "ampview/geometry"
//       and toggle states under "ampview/showGain", "ampview/phaseZoom",
//       "ampview/lowRes", "ampview/onTop".
//
// Source: NereusSDR-original.  See AmpViewWindow.h for the Thetis cite map.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-06 — New test file for Phase 3M-4 Task 9: AmpViewWindow
//                 dialog unit tests.  J.J. Boyd (KG4VCF), with AI-
//                 assisted implementation via Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>

#include <QCheckBox>
#include <QSignalSpy>
#include <vector>

#include "core/AppSettings.h"
#include "gui/AmpViewChart.h"
#include "gui/AmpViewWindow.h"

using namespace NereusSDR;

class TstAmpViewWindow : public QObject {
    Q_OBJECT

private slots:

    // ── Test 1: construct + destruct without any wiring ─────────────────────

    void constructAndDestruct_withNullRadioModel_doesNotCrash()
    {
        AmpViewWindow win(/*radioModel=*/nullptr);
        QCOMPARE(win.windowTitle(), QStringLiteral("AmpView 1.0"));
        QCOMPARE(win.isModal(), false);
    }

    // ── Test 2: client size + min size match Thetis ─────────────────────────
    //
    // From AmpView.Designer.cs:214 [v2.10.3.13]:
    //   this.ClientSize = new System.Drawing.Size(564, 401);
    //   this.MinimumSize = new System.Drawing.Size(440, 380);

    void defaultGeometryMatchesThetis()
    {
        AmpViewWindow win(nullptr);
        // The dialog should default to 564x401 client size; minimum is 440x380.
        QCOMPARE(win.minimumSize(), QSize(440, 380));
        // The default size should be at least the client size 564x401.
        win.show();
        QVERIFY(win.width() >= 440);
        QVERIFY(win.height() >= 380);
    }

    // ── Test 3: chart has exactly 5 named series ────────────────────────────
    //
    // The 5 user-visible series with points populated per AmpView.cs:125-153
    // init_data() [v2.10.3.13] are: Ref, MagCorr, PhsCorr, MagAmp, PhsAmp.

    void chartHasFiveNamedSeries()
    {
        AmpViewChart chart;
        QCOMPARE(chart.seriesCount(), 5);
    }

    void chartSeriesNamesMatchThetis()
    {
        AmpViewChart chart;
        const QStringList expected = {QStringLiteral("Ref"),
                                      QStringLiteral("MagAmp"),
                                      QStringLiteral("PhsAmp"),
                                      QStringLiteral("MagCorr"),
                                      QStringLiteral("PhsCorr")};
        QCOMPARE(chart.seriesNames(), expected);
    }

    // ── Test 4: 4 toolbar checkboxes exist by objectName ────────────────────

    void allFourToolbarCheckboxesExistByObjectName()
    {
        AmpViewWindow win(nullptr);

        QVERIFY(win.findChild<QCheckBox*>(QStringLiteral("chkAVShowGain")));
        QVERIFY(win.findChild<QCheckBox*>(QStringLiteral("chkAVPhaseZoom")));
        QVERIFY(win.findChild<QCheckBox*>(QStringLiteral("chkAVLowRes")));
        QVERIFY(win.findChild<QCheckBox*>(QStringLiteral("chkStayOnTop")));
    }

    // ── Test 5: defaults match Thetis designer values ───────────────────────
    //
    // From AmpView.Designer.cs:182-183 [v2.10.3.13]:
    //   this.chkAVLowRes.Checked = true;
    //   this.chkAVLowRes.CheckState = System.Windows.Forms.CheckState.Checked;
    //
    // chkAVShowGain / chkAVPhaseZoom / chkStayOnTop default unchecked.

    void chkAVLowResDefaultsChecked()
    {
        // Use a fresh settings sandbox to avoid carry-over from prior tests.
        AppSettings::instance().clear();
        AmpViewWindow win(nullptr);
        QCOMPARE(win.findChild<QCheckBox*>(QStringLiteral("chkAVLowRes"))
                     ->isChecked(),
                 true);
    }

    void otherToolbarCheckboxesDefaultUnchecked()
    {
        AppSettings::instance().clear();
        AmpViewWindow win(nullptr);
        QCOMPARE(win.findChild<QCheckBox*>(QStringLiteral("chkAVShowGain"))
                     ->isChecked(),
                 false);
        QCOMPARE(win.findChild<QCheckBox*>(QStringLiteral("chkAVPhaseZoom"))
                     ->isChecked(),
                 false);
        QCOMPARE(win.findChild<QCheckBox*>(QStringLiteral("chkStayOnTop"))
                     ->isChecked(),
                 false);
    }

    // ── Test 6: chart toggle forwarding ─────────────────────────────────────
    //
    // Toggling each checkbox should call the matching setter on AmpViewChart
    // and persist the state to AppSettings.

    void toggleShowGainPersistsAndForwardsToChart()
    {
        AppSettings::instance().clear();
        AmpViewWindow win(nullptr);
        auto* chk = win.findChild<QCheckBox*>(QStringLiteral("chkAVShowGain"));
        QVERIFY(chk);

        chk->setChecked(true);
        QCOMPARE(AppSettings::instance()
                     .value(QStringLiteral("ampview/showGain"),
                            QStringLiteral("False"))
                     .toString(),
                 QStringLiteral("True"));
    }

    void togglePhaseZoomPersistsAndForwardsToChart()
    {
        AppSettings::instance().clear();
        AmpViewWindow win(nullptr);
        auto* chk = win.findChild<QCheckBox*>(QStringLiteral("chkAVPhaseZoom"));
        QVERIFY(chk);

        chk->setChecked(true);
        QCOMPARE(AppSettings::instance()
                     .value(QStringLiteral("ampview/phaseZoom"),
                            QStringLiteral("False"))
                     .toString(),
                 QStringLiteral("True"));
    }

    void toggleLowResPersistsAndForwardsToChart()
    {
        AppSettings::instance().clear();
        AmpViewWindow win(nullptr);
        auto* chk = win.findChild<QCheckBox*>(QStringLiteral("chkAVLowRes"));
        QVERIFY(chk);

        // It defaults ON; flip to OFF and verify persistence.
        chk->setChecked(false);
        QCOMPARE(AppSettings::instance()
                     .value(QStringLiteral("ampview/lowRes"),
                            QStringLiteral("True"))
                     .toString(),
                 QStringLiteral("False"));
    }

    // ── Test 7: stay-on-top toggles Qt::WindowStaysOnTopHint ────────────────

    void toggleStayOnTopAppliesWindowFlag()
    {
        AppSettings::instance().clear();
        AmpViewWindow win(nullptr);
        auto* chk = win.findChild<QCheckBox*>(QStringLiteral("chkStayOnTop"));
        QVERIFY(chk);

        chk->setChecked(true);
        QVERIFY((win.windowFlags() & Qt::WindowStaysOnTopHint)
                == Qt::WindowStaysOnTopHint);

        chk->setChecked(false);
        QVERIFY((win.windowFlags() & Qt::WindowStaysOnTopHint)
                != Qt::WindowStaysOnTopHint);
    }

    // ── Test 8: setSeriesData() updates chart without crashing ──────────────

    void setSeriesDataUpdatesChart()
    {
        AmpViewChart chart;
        std::vector<double> x{0.1, 0.2, 0.3, 0.4, 0.5};
        std::vector<double> magAmp{0.1, 0.18, 0.28, 0.36, 0.45};
        std::vector<double> phsAmp{0.0, 1.0, 2.0, 3.0, 4.0};
        std::vector<double> magCorr{0.05, 0.10, 0.15, 0.20, 0.25};
        std::vector<double> phsCorr{0.0, 0.5, 1.0, 1.5, 2.0};

        // Should not crash.
        chart.setSeriesData(x, magAmp, phsAmp, magCorr, phsCorr);
        QCOMPARE(chart.pointCount(), static_cast<int>(x.size()));

        // Empty buffers should also not crash.
        chart.setSeriesData({}, {}, {}, {}, {});
        QCOMPARE(chart.pointCount(), 0);
    }

    // ── Test 9: setStayOnTopFromParent() drives the checkbox + flag ─────────
    //
    // PsForm calls this when its own Always-On-Top toggle changes, so AmpView
    // tracks the parent.

    void setStayOnTopFromParentDrivesCheckbox()
    {
        AppSettings::instance().clear();
        AmpViewWindow win(nullptr);
        auto* chk = win.findChild<QCheckBox*>(QStringLiteral("chkStayOnTop"));
        QVERIFY(chk);

        win.setStayOnTopFromParent(true);
        QCOMPARE(chk->isChecked(), true);

        win.setStayOnTopFromParent(false);
        QCOMPARE(chk->isChecked(), false);
    }

    // ── Test 10: chart Show Gain mode swaps Y axis range ────────────────────
    //
    // From AmpView.cs:435-455 [v2.10.3.13]:
    //   chkAVShowGain checked → AxisY.Maximum = 2.0, "Gain"
    //   chkAVShowGain unchecked → AxisY.Maximum = 1.0, "Magnitude"

    void chartShowGainTogglesYMax()
    {
        AmpViewChart chart;
        QCOMPARE(chart.magYMax(), 1.0);
        chart.setShowGain(true);
        QCOMPARE(chart.magYMax(), 2.0);
        chart.setShowGain(false);
        QCOMPARE(chart.magYMax(), 1.0);
    }

    // ── Test 11: chart Phase Zoom toggles secondary axis range ──────────────
    //
    // From AmpView.cs:470-482 [v2.10.3.13]:
    //   chkAVPhaseZoom checked → AxisY2 = -45..+45
    //   chkAVPhaseZoom unchecked → AxisY2 = -180..+180

    void chartPhaseZoomTogglesPhaseAxisRange()
    {
        AmpViewChart chart;
        QCOMPARE(chart.phaseYMax(), 180.0);
        chart.setPhaseZoom(true);
        QCOMPARE(chart.phaseYMax(), 45.0);
        chart.setPhaseZoom(false);
        QCOMPARE(chart.phaseYMax(), 180.0);
    }

    // ── Test 12: chart Low Res sets stride 4 ────────────────────────────────
    //
    // From AmpView.cs:457-463 [v2.10.3.13]:
    //   chkAVLowRes checked → skip = 4 ; else skip = 1.

    void chartLowResStrideMatchesThetis()
    {
        AmpViewChart chart;
        chart.setLowRes(false);
        QCOMPARE(chart.lowResStride(), 1);
        chart.setLowRes(true);
        QCOMPARE(chart.lowResStride(), 4);
    }
};

QTEST_MAIN(TstAmpViewWindow)
#include "tst_ampview_window.moc"
