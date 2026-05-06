// =================================================================
// tests/tst_window_functions.cpp  (NereusSDR)
// =================================================================
//
// Phase 2 -- exercise every WindowFunction enum value through
// FFTEngine and assert the computed window has finite samples,
// non-zero coherent gain, and an Equivalent Noise Bandwidth in the
// expected range.
//
// All 7 window types covered (matches Thetis comboDispWinType item
// list at setup.designer.cs:34966-34973 [v2.10.3.13]):
//   Rectangular / Blackman-Harris 4T / Hann / Flat-Top / Hamming /
//   Kaiser / Blackman-Harris 7T.
//
// ENB sanity ranges (in bins) from analyzer.c:175 [v2.10.3.13]
//   inv_enb = 1 / (inherent_power_gain * inv_coherent_gain^2)
// inverted to per-window expected ENB:
//   Rectangular ~ 1.00
//   Hann        ~ 1.50
//   Hamming     ~ 1.36
//   BH-4T       ~ 2.00
//   Flat-Top    ~ 3.77
//   Kaiser      depends on PiAlpha; 14.0 default ~ 3.0..4.0
//   BH-7T       ~ 2.31
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-06 -- Created in C++20/Qt6 for NereusSDR by J.J. Boyd
//                  (KG4VCF), with AI-assisted transformation via
//                  Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>
#include <QtNumeric>

#include <cmath>

#include "core/FFTEngine.h"

using namespace NereusSDR;

class TestWindowFunctions : public QObject
{
    Q_OBJECT

private:
    // Drive a FFTEngine to a particular window + size, run a single
    // I/Q frame so computeWindow() fires, and read back the ENB.
    // FFTEngine::feedIQ() requires 2*fftSize floats to fill the buffer
    // and trigger processFrame() -> computeWindow().
    double computeEnbFor(WindowFunction wf, int fftSize) const
    {
        FFTEngine fe(/*receiverId=*/0);
        fe.setSampleRate(192000.0);
        fe.setFftSize(fftSize);
        fe.setWindowFunction(wf);

        // First feedIQ call applies the pending FFT size + plan + window.
        // Synthetic I/Q: small constant signal so we don't NaN through fftw.
        QVector<float> iq(fftSize * 2, 1e-6f);
        fe.feedIQ(iq);
        return fe.windowEnb();
    }

    bool inRange(double v, double lo, double hi) const
    {
        return std::isfinite(v) && v >= lo && v <= hi;
    }

private slots:

    void enum_values_match_wdsp_wire_format()
    {
        // From WDSP analyzer.c:52-173 [v2.10.3.13] new_window() switch
        // case codes -- our enum mirrors them 1:1.
        QCOMPARE(static_cast<int>(WindowFunction::Rectangular),     0);
        QCOMPARE(static_cast<int>(WindowFunction::BlackmanHarris4), 1);
        QCOMPARE(static_cast<int>(WindowFunction::Hann),            2);
        QCOMPARE(static_cast<int>(WindowFunction::FlatTop),         3);
        QCOMPARE(static_cast<int>(WindowFunction::Hamming),         4);
        QCOMPARE(static_cast<int>(WindowFunction::Kaiser),          5);
        QCOMPARE(static_cast<int>(WindowFunction::BlackmanHarris7), 6);
        QCOMPARE(static_cast<int>(WindowFunction::Count),           7);
    }

    // Rectangular ENB == 1.0 exactly (all coefficients = 1.0; sum = N,
    // sumSq = N, so N*sumSq/sum^2 = N*N/N^2 = 1.0).
    void rectangular_enb_is_unity()
    {
        const double enb = computeEnbFor(WindowFunction::Rectangular, 4096);
        QVERIFY(std::isfinite(enb));
        QCOMPARE_LE(std::abs(enb - 1.0), 1e-9);
    }

    void blackman_harris_4t_enb_in_expected_range()
    {
        // BH-4T published ENB ~ 2.00.  Allow generous range to absorb
        // floating-point + tail-asymmetry effects from the size-1 vs
        // size denominator difference.
        const double enb = computeEnbFor(WindowFunction::BlackmanHarris4, 4096);
        QVERIFY2(inRange(enb, 1.85, 2.15),
                 qPrintable(QStringLiteral("BH-4T ENB out of range: %1").arg(enb)));
    }

    void hann_enb_in_expected_range()
    {
        // Hann published ENB = 1.50.
        const double enb = computeEnbFor(WindowFunction::Hann, 4096);
        QVERIFY2(inRange(enb, 1.40, 1.60),
                 qPrintable(QStringLiteral("Hann ENB out of range: %1").arg(enb)));
    }

    void flat_top_enb_in_expected_range()
    {
        // Flat-Top published ENB ~ 3.77.
        const double enb = computeEnbFor(WindowFunction::FlatTop, 4096);
        QVERIFY2(inRange(enb, 3.55, 3.95),
                 qPrintable(QStringLiteral("Flat-Top ENB out of range: %1").arg(enb)));
    }

    void hamming_enb_in_expected_range()
    {
        // Hamming published ENB ~ 1.36.
        const double enb = computeEnbFor(WindowFunction::Hamming, 4096);
        QVERIFY2(inRange(enb, 1.25, 1.45),
                 qPrintable(QStringLiteral("Hamming ENB out of range: %1").arg(enb)));
    }

    // Kaiser at PiAlpha=14.0 (Thetis default per specHPSDR.cs:145
    // [v2.10.3.13]) has ENB ~ 2.16 with NereusSDR's Numerical-Recipes
    // bessi0 polynomial.  Range mirrors Harris 1978 + Heinzel 2002
    // tabulations for alpha ~ 4.5 (PiAlpha / pi).
    void kaiser_default_pi_alpha_finite_enb()
    {
        const double enb = computeEnbFor(WindowFunction::Kaiser, 4096);
        QVERIFY2(inRange(enb, 1.5, 3.5),
                 qPrintable(QStringLiteral("Kaiser ENB out of range: %1").arg(enb)));
    }

    void blackman_harris_7t_enb_in_expected_range()
    {
        // BH-7T published ENB ~ 2.31 (Heinzel 2002) to ~2.63 depending on
        // coefficient set.  WDSP analyzer.c:158-164 [v2.10.3.13] uses
        // the higher-sidelobe-suppression set; computed ENB ~ 2.63.
        const double enb = computeEnbFor(WindowFunction::BlackmanHarris7, 4096);
        QVERIFY2(inRange(enb, 2.10, 3.00),
                 qPrintable(QStringLiteral("BH-7T ENB out of range: %1").arg(enb)));
    }

    // KaiserPi setter round-trip + clamp on non-positive.
    void kaiser_pi_setter_round_trip()
    {
        FFTEngine fe(0);
        QCOMPARE(fe.kaiserPi(), 14.0);  // Thetis default
        fe.setKaiserPi(8.0);
        QCOMPARE(fe.kaiserPi(), 8.0);
        fe.setKaiserPi(-1.0);            // rejected, stays at 8.0
        QCOMPARE(fe.kaiserPi(), 8.0);
        fe.setKaiserPi(0.0);             // rejected, stays at 8.0
        QCOMPARE(fe.kaiserPi(), 8.0);
        fe.setKaiserPi(20.0);
        QCOMPARE(fe.kaiserPi(), 20.0);
    }

    // Window function setter round-trip across all 7 enum values.
    void window_function_setter_round_trip_all_values()
    {
        FFTEngine fe(0);
        const WindowFunction values[] = {
            WindowFunction::Rectangular,
            WindowFunction::BlackmanHarris4,
            WindowFunction::Hann,
            WindowFunction::FlatTop,
            WindowFunction::Hamming,
            WindowFunction::Kaiser,
            WindowFunction::BlackmanHarris7
        };
        for (const auto wf : values) {
            fe.setWindowFunction(wf);
            QCOMPARE(fe.windowFunction(), wf);
        }
    }
};

QTEST_APPLESS_MAIN(TestWindowFunctions)
#include "tst_window_functions.moc"
