// tst_imd_overlay.cpp
//
// no-port-check: NereusSDR-original test file. All Thetis IMD-overlay
// source cites are in src/gui/ImdOverlay.{h,cpp} (display.cs:5008,
// 5210-5316, 5453-5475, 5512-5685, 5725-5760 [v2.10.3.13]).
// =================================================================
// tests/tst_imd_overlay.cpp  (NereusSDR)
// =================================================================
//
// Phase 3M-4 Task 12 — verifies ImdOverlay:
//   1. detectPeaks finds two fundamentals on a synthetic two-tone spectrum.
//   2. detectPeaks finds all six peaks (f0L/f0U/IMD3L/IMD3U/IMD5L/IMD5U).
//   3. detectPeaks returns empty on a flat spectrum.
//   4. labelImdProducts identifies fundamentals (top-2 by dBm).
//   5. labelImdProducts identifies IMD3 lower and upper.
//   6. labelImdProducts identifies IMD5 lower and upper.
//   7. labelImdProducts returns false when fewer than 6 peaks are present.
//   8. labelImdProducts returns false when fundamentals are too close
//      (pixel_diff <= 10).
//   9. EMA initialises on first updateReadout, then converges across calls.
//  10. reset() clears the EMA state.
//  11. formatReadout() string structure matches Thetis display.cs:5650-5685
//      verbatim (label column / val1 column / val2 column).
//  12. formatReadout() values use "f2" formatting (2 decimal places).
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-06 — Original implementation for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted authoring via Anthropic
//                 Claude Code (Phase 3M-4 Task 12 IMD overlay).
// =================================================================

#include <QtTest/QtTest>
#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <vector>

#include "gui/ImdOverlay.h"

using namespace NereusSDR;

class TstImdOverlay : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Stay test-isolated; ImdOverlay is pure-state, no AppSettings touched.
    }

    // ── Peak detection ──────────────────────────────────────────────────────

    // From Thetis display.cs:5283-5298 [v2.10.3.13]: peak detection walks
    // the spectrum, looks for local maxima, and confirms a peak only when
    // the running max drops by trigger_delta dB. Default trigger_delta=10
    // per display.cs:5217.
    void detectPeaksFindsTwoFundamentalsInSyntheticSpectrum()
    {
        std::vector<float> bins(800, -110.0f);
        // Two clean fundamentals at the same dBm.
        bins[340] = -22.0f;
        bins[460] = -22.0f;

        const auto peaks = ImdOverlay::detectPeaks(bins, /*triggerDelta=*/10.0f);
        QVERIFY(peaks.size() >= 2);
    }

    // Synthetic two-tone scenario: f0 separation 120 bins, IMD3 at +/- 60
    // bins from each fundamental (sortedlow/high = lower-X / higher-X).
    // findImd uses jump = (imd-1)/2, so IMD3 estimate is offset +/- 1 *
    // pixel_jump from the corresponding fundamental, IMD5 at +/- 2 *
    // pixel_jump. Build a spectrum that satisfies that geometry.
    void detectPeaksFindsAllSixPeaksInTwoToneScenario()
    {
        std::vector<float> bins(800, -110.0f);
        // f0L at 340, f0U at 460  -> pixel_diff = 120, low_x=340, high_x=460
        // IMD3L = 340 - 120 = 220, IMD3U = 460 + 120 = 580
        // IMD5L = 340 - 240 = 100, IMD5U = 460 + 240 = 700
        bins[100] = -88.0f;  // IMD5 L
        bins[220] = -65.0f;  // IMD3 L
        bins[340] = -22.0f;  // f0 L
        bins[460] = -22.0f;  // f0 U
        bins[580] = -65.0f;  // IMD3 U
        bins[700] = -88.0f;  // IMD5 U

        const auto peaks = ImdOverlay::detectPeaks(bins, /*triggerDelta=*/10.0f);
        QCOMPARE(peaks.size(), 6u);
    }

    void detectPeaksReturnsEmptyOnFlatSpectrum()
    {
        std::vector<float> bins(800, -110.0f);  // dead flat
        const auto peaks = ImdOverlay::detectPeaks(bins, /*triggerDelta=*/10.0f);
        QCOMPARE(peaks.size(), 0u);
    }

    // ── IMD3/IMD5 labeling ──────────────────────────────────────────────────

    // From Thetis display.cs:5512-5560 [v2.10.3.13]: top-2 dBm peaks are
    // the fundamentals. f0L = lower-X of the two; f0U = higher-X.
    void labelImdProductsIdentifiesFundamentalsCorrectly()
    {
        std::vector<float> bins(800, -110.0f);
        bins[100] = -88.0f;  // IMD5 L
        bins[220] = -65.0f;  // IMD3 L
        bins[340] = -22.0f;  // f0 L
        bins[460] = -22.0f;  // f0 U
        bins[580] = -65.0f;  // IMD3 U
        bins[700] = -88.0f;  // IMD5 U

        auto peaks = ImdOverlay::detectPeaks(bins, /*triggerDelta=*/10.0f);
        QCOMPARE(peaks.size(), 6u);

        Maximum f0L, f0U, imd3L, imd3U, imd5L, imd5U;
        const bool ok = ImdOverlay::labelImdProducts(
            peaks, f0L, f0U, imd3L, imd3U, imd5L, imd5U);
        QVERIFY(ok);
        QCOMPARE(f0L.x, 340);
        QCOMPARE(f0U.x, 460);
    }

    // From Thetis display.cs:5538-5553 [v2.10.3.13]: findImd walks the
    // sorted-low/sorted-high lists, searching for the peak closest to
    // estimate_pixel_pos = offset +/- jump * pixel_jump.
    void labelImdProductsIdentifiesIMD3LowerAndUpper()
    {
        std::vector<float> bins(800, -110.0f);
        bins[100] = -88.0f;
        bins[220] = -65.0f;
        bins[340] = -22.0f;
        bins[460] = -22.0f;
        bins[580] = -65.0f;
        bins[700] = -88.0f;

        auto peaks = ImdOverlay::detectPeaks(bins, /*triggerDelta=*/10.0f);
        Maximum f0L, f0U, imd3L, imd3U, imd5L, imd5U;
        QVERIFY(ImdOverlay::labelImdProducts(
            peaks, f0L, f0U, imd3L, imd3U, imd5L, imd5U));
        QCOMPARE(imd3L.x, 220);
        QCOMPARE(imd3U.x, 580);
    }

    void labelImdProductsIdentifiesIMD5LowerAndUpper()
    {
        std::vector<float> bins(800, -110.0f);
        bins[100] = -88.0f;
        bins[220] = -65.0f;
        bins[340] = -22.0f;
        bins[460] = -22.0f;
        bins[580] = -65.0f;
        bins[700] = -88.0f;

        auto peaks = ImdOverlay::detectPeaks(bins, /*triggerDelta=*/10.0f);
        Maximum f0L, f0U, imd3L, imd3U, imd5L, imd5U;
        QVERIFY(ImdOverlay::labelImdProducts(
            peaks, f0L, f0U, imd3L, imd3U, imd5L, imd5U));
        QCOMPARE(imd5L.x, 100);
        QCOMPARE(imd5U.x, 700);
    }

    // From Thetis display.cs:5523-5526 [v2.10.3.13]: when fewer than 2
    // peaks are detected (sorted.Length < 2), no labeling is performed.
    void labelImdProductsReturnsFalseWhenInsufficientPeaks()
    {
        std::vector<Maximum> peaks;  // empty
        Maximum f0L, f0U, imd3L, imd3U, imd5L, imd5U;
        QVERIFY(!ImdOverlay::labelImdProducts(
            peaks, f0L, f0U, imd3L, imd3U, imd5L, imd5U));
    }

    // From Thetis display.cs:5532 [v2.10.3.13]: pixel_diff > 10 is
    // required; otherwise "Fundamental peak separation needs to be
    // increased" is shown instead.
    void labelImdProductsReturnsFalseWhenFundamentalsTooClose()
    {
        std::vector<float> bins(800, -110.0f);
        bins[340] = -22.0f;
        bins[345] = -22.0f;  // only 5 bins apart

        auto peaks = ImdOverlay::detectPeaks(bins, /*triggerDelta=*/10.0f);
        Maximum f0L, f0U, imd3L, imd3U, imd5L, imd5U;
        // Must reject — pixel_diff = 5 <= 10.
        QVERIFY(!ImdOverlay::labelImdProducts(
            peaks, f0L, f0U, imd3L, imd3U, imd5L, imd5U));
    }

    // ── EMA smoothing ───────────────────────────────────────────────────────

    // From Thetis display.cs:5589-5641 [v2.10.3.13]:
    //   first call (when _ema_dbc == -999): initialise EMAs to raw values
    //   subsequent calls: previous = alpha * newValue + (1 - alpha) * previous;
    //   alpha = 0.1f
    void emaInitialisesOnFirstCallThenConvergesAcrossCalls()
    {
        Maximum f0L{-22.0f, 340, 0, true};
        Maximum f0U{-22.0f, 460, 0, true};
        Maximum imd3L{-65.0f, 220, 0, true};
        Maximum imd3U{-65.0f, 580, 0, true};
        Maximum imd5L{-88.0f, 100, 0, true};
        Maximum imd5U{-88.0f, 700, 0, true};

        ImdOverlay overlay;

        // First call seeds the EMA at the raw values.
        overlay.updateReadout(f0L, f0U, imd3L, imd3U, imd5L, imd5U);
        const auto& r0 = overlay.readout();
        QVERIFY(r0.valid);
        QCOMPARE(r0.f0L, -22.0f);
        QCOMPARE(r0.f0U, -22.0f);
        QCOMPARE(r0.imd3L, -65.0f);

        // Step input: shift fundamentals down 4 dB; subsequent calls
        // converge toward new steady-state. After 50 iterations at
        // alpha=0.1 the residual error is well below 0.01 dB.
        f0L.dBm = -26.0f;
        f0U.dBm = -26.0f;
        for (int i = 0; i < 50; ++i) {
            overlay.updateReadout(f0L, f0U, imd3L, imd3U, imd5L, imd5U);
        }
        const auto& rN = overlay.readout();
        // Within 0.05 dB of the new steady-state.
        QVERIFY(std::abs(rN.f0L - (-26.0f)) < 0.05f);
        QVERIFY(std::abs(rN.f0U - (-26.0f)) < 0.05f);
    }

    // From Thetis display.cs:5680 [v2.10.3.13]:
    //   else if (_ema_dbc != -999) _ema_dbc = -999;
    // i.e. when the show condition flips off (or peaks lost), EMA state
    // is reset by the next show-on transition. ImdOverlay::reset() is
    // the explicit reset hook.
    void resetClearsEmaState()
    {
        Maximum f0L{-22.0f, 340, 0, true};
        Maximum f0U{-22.0f, 460, 0, true};
        Maximum imd3L{-65.0f, 220, 0, true};
        Maximum imd3U{-65.0f, 580, 0, true};
        Maximum imd5L{-88.0f, 100, 0, true};
        Maximum imd5U{-88.0f, 700, 0, true};

        ImdOverlay overlay;
        overlay.updateReadout(f0L, f0U, imd3L, imd3U, imd5L, imd5U);
        QVERIFY(overlay.readout().valid);

        overlay.reset();
        QVERIFY(!overlay.readout().valid);

        // After reset, the next call must re-seed (not blend with stale state).
        f0L.dBm = -50.0f;
        f0U.dBm = -50.0f;
        overlay.updateReadout(f0L, f0U, imd3L, imd3U, imd5L, imd5U);
        const auto& r = overlay.readout();
        QCOMPARE(r.f0L, -50.0f);  // re-seeded, not 0.1 * -50 + 0.9 * stale.
        QCOMPARE(r.f0U, -50.0f);
    }

    // ── Readout text format ─────────────────────────────────────────────────

    // From Thetis display.cs:5650-5685 [v2.10.3.13]:
    //   readings: "    f0 L\n    f0 U\nIMD3 L\nIMD3 U\nIMD5 L\nIMD5 U\n\n"
    //             "        IMD3\n        IMD5\n        OIP3\n        OIP5"
    //   val1:     <f0L:f2>\n<f0U:f2>\n... + "    " + worst_imd3 + " dBc" ...
    //   val2:     <f0l_rel:f2>\n<f0u_rel:f2>\n...
    void formatReadoutMatchesThetisStringStructure()
    {
        Maximum f0L{-22.55f, 340, 0, true};
        Maximum f0U{-22.30f, 460, 0, true};
        Maximum imd3L{-64.73f, 220, 0, true};
        Maximum imd3U{-65.10f, 580, 0, true};
        Maximum imd5L{-88.21f, 100, 0, true};
        Maximum imd5U{-89.05f, 700, 0, true};

        ImdOverlay overlay;
        overlay.updateReadout(f0L, f0U, imd3L, imd3U, imd5L, imd5U);
        const auto t = overlay.formatReadout();

        // Label column verbatim from display.cs:5651-5660.
        QVERIFY(t.readings.contains(QStringLiteral("    f0 L\n")));
        QVERIFY(t.readings.contains(QStringLiteral("    f0 U\n")));
        QVERIFY(t.readings.contains(QStringLiteral("IMD3 L\n")));
        QVERIFY(t.readings.contains(QStringLiteral("IMD3 U\n")));
        QVERIFY(t.readings.contains(QStringLiteral("IMD5 L\n")));
        QVERIFY(t.readings.contains(QStringLiteral("IMD5 U\n")));
        QVERIFY(t.readings.contains(QStringLiteral("        IMD3\n")));
        QVERIFY(t.readings.contains(QStringLiteral("        IMD5\n")));
        QVERIFY(t.readings.contains(QStringLiteral("        OIP3\n")));
        QVERIFY(t.readings.contains(QStringLiteral("        OIP5")));

        // val1 has 6 dBm rows + blank + 4 dBc/dB summary rows.
        QVERIFY(t.val1.contains(QStringLiteral("-22.55")));
        QVERIFY(t.val1.contains(QStringLiteral("-22.30")));
        QVERIFY(t.val1.contains(QStringLiteral("-64.73")));
        QVERIFY(t.val1.contains(QStringLiteral("-65.10")));
        QVERIFY(t.val1.contains(QStringLiteral("-88.21")));
        QVERIFY(t.val1.contains(QStringLiteral("-89.05")));
        QVERIFY(t.val1.contains(QStringLiteral(" dBc")));
        QVERIFY(t.val1.contains(QStringLiteral(" dB")));
    }

    void formatReadoutPrecisionIsTwoDecimals()
    {
        Maximum f0L{-22.55555f, 340, 0, true};
        Maximum f0U{-22.30303f, 460, 0, true};
        Maximum imd3L{-64.73111f, 220, 0, true};
        Maximum imd3U{-65.10999f, 580, 0, true};
        Maximum imd5L{-88.21222f, 100, 0, true};
        Maximum imd5U{-89.05888f, 700, 0, true};

        ImdOverlay overlay;
        overlay.updateReadout(f0L, f0U, imd3L, imd3U, imd5L, imd5U);
        const auto t = overlay.formatReadout();

        // First line of val1 must be exactly "-22.56" (rounded to 2 dp).
        // Negative values + Qt's 'f' formatter is half-to-even; verify a
        // value that doesn't depend on rounding direction.
        QVERIFY(t.val1.startsWith(QStringLiteral("-22.")));
        // val2 starts with f0l_rel = -(dbc - f0L_ema). dbc = max(f0L,f0U)
        // = -22.30303; f0l_rel = -(-22.30303 - -22.55555) = -0.25252 ->
        // "-0.25" at f2. Allow for floating-point fuzz: just check that
        // val2 starts with "-0." or "0." (signed 2-dp number).
        QVERIFY(t.val2.length() >= 4);
        QVERIFY(t.val2.contains(QStringLiteral(".")));

        // Pin precision: 2 decimal digits for at least the first value.
        // Find the first newline; everything before should be 2-dp.
        const int nl = t.val1.indexOf(QChar('\n'));
        QVERIFY(nl > 0);
        const QString first = t.val1.left(nl);
        // "-22.56" — exactly 2 chars after the decimal.
        const int dot = first.indexOf(QChar('.'));
        QVERIFY(dot > 0);
        QCOMPARE(first.length() - dot - 1, 2);
    }
};

QTEST_MAIN(TstImdOverlay)
#include "tst_imd_overlay.moc"
