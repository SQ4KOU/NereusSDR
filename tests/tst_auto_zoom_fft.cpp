// no-port-check: unit tests for NereusSDR-original auto-zoom math;
// no Thetis logic ported (auto-zoom has no Thetis equivalent).
//
// =================================================================
// tests/tst_auto_zoom_fft.cpp  (NereusSDR)
// =================================================================
//
// Phase 2-polish-3 -- exercise the auto-zoom math that scales FFT
// size to maintain constant bins-per-pixel across zoom levels.
//
// Asserts:
//   1. setFftSizeBaseline / fftSizeBaseline round-trip across all
//      slider-valid values (1024..262144 power-of-two).
//   2. Invalid sizes (out of range, non-power-of-two) rejected.
//   3. Default baseline matches default fftSize (4096).
//
// NB: the auto-zoom formula and hysteresis live in MainWindow's
// bandwidthChangeRequested lambda (cannot test in headless mode
// without instantiating MainWindow + signal wiring).  This test
// covers the FFTEngine-side state API; the math is tested via the
// helper computeAutoZoomFftSize() defined inline below, which
// duplicates the lambda's algorithm so changes to either MUST be
// kept in sync.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-06 -- Created in C++20/Qt6 for NereusSDR by J.J. Boyd
//                  (KG4VCF), with AI-assisted transformation via
//                  Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>

#include <algorithm>

#include "core/FFTEngine.h"

using namespace NereusSDR;

namespace {

// Mirror of the auto-zoom math in MainWindow's bandwidthChangeRequested
// lambda.  Keep in sync with src/gui/MainWindow.cpp.
struct AutoZoomResult {
    int  size;       // computed target FFT size
    bool replanned;  // true when ratio fell outside hysteresis band
};

AutoZoomResult computeAutoZoomFftSize(int baseline,
                                      int currentSize,
                                      double sampleRate,
                                      double bwHz)
{
    AutoZoomResult r{currentSize, false};
    if (sampleRate <= 0.0 || bwHz <= 0.0) { return r; }
    const double scale = sampleRate / bwHz;
    double desired = static_cast<double>(baseline) * scale;
    int targetSize = 1024;
    while (targetSize < desired && targetSize < 262144) {
        targetSize *= 2;
    }
    targetSize = std::max(targetSize, baseline);
    targetSize = std::clamp(targetSize, 1024, 262144);
    if (currentSize > 0) {
        const double ratio = static_cast<double>(targetSize)
                             / static_cast<double>(currentSize);
        if (ratio > 0.66 && ratio < 1.5) {
            return r;  // hysteresis: no replan
        }
    }
    r.size = targetSize;
    r.replanned = (targetSize != currentSize);
    return r;
}

}  // namespace

class TestAutoZoomFft : public QObject
{
    Q_OBJECT

private slots:

    void baseline_default_matches_initial_fft_size()
    {
        FFTEngine fe(/*receiverId=*/0);
        QCOMPARE(fe.fftSizeBaseline(), 4096);
        QCOMPARE(fe.fftSize(),         4096);
    }

    void baseline_round_trip_for_all_slider_values()
    {
        FFTEngine fe(0);
        for (int v = 0; v <= 6; ++v) {
            const int size = 4096 << v;
            fe.setFftSizeBaseline(size);
            QCOMPARE(fe.fftSizeBaseline(), size);
        }
    }

    void baseline_rejects_out_of_range()
    {
        FFTEngine fe(0);
        fe.setFftSizeBaseline(8192);
        QCOMPARE(fe.fftSizeBaseline(), 8192);
        fe.setFftSizeBaseline(512);            // below kMinFftSize
        QCOMPARE(fe.fftSizeBaseline(), 8192);  // unchanged
        fe.setFftSizeBaseline(524288);         // above kMaxFftSize
        QCOMPARE(fe.fftSizeBaseline(), 8192);  // unchanged
    }

    void baseline_rejects_non_power_of_two()
    {
        FFTEngine fe(0);
        fe.setFftSizeBaseline(8192);
        QCOMPARE(fe.fftSizeBaseline(), 8192);
        fe.setFftSizeBaseline(7000);
        QCOMPARE(fe.fftSizeBaseline(), 8192);  // unchanged
        fe.setFftSizeBaseline(12288);
        QCOMPARE(fe.fftSizeBaseline(), 8192);  // unchanged
    }

    // Auto-zoom formula: targetFftSize = baseline * (sampleRate/bwHz),
    // clamped to [baseline, 262144], next pow2.  At full DDC bandwidth
    // (bwHz == sampleRate) the target equals the baseline exactly.
    void formula_at_full_bandwidth_returns_baseline()
    {
        const auto r = computeAutoZoomFftSize(/*baseline=*/4096,
                                              /*current=*/0,
                                              /*sampleRate=*/768000.0,
                                              /*bwHz=*/768000.0);
        QCOMPARE(r.size, 4096);
    }

    void formula_at_half_bandwidth_doubles()
    {
        const auto r = computeAutoZoomFftSize(4096, 0, 768000.0, 384000.0);
        QCOMPARE(r.size, 8192);
    }

    void formula_at_quarter_bandwidth_quadruples()
    {
        // Default zoom (768k / 192k = 4x): slider=4096 -> FFT=16384.
        const auto r = computeAutoZoomFftSize(4096, 0, 768000.0, 192000.0);
        QCOMPARE(r.size, 16384);
    }

    void formula_caps_at_max_fft_size()
    {
        // Slider=4096 + 64x zoom (12 kHz visible at 768k DDC) -> 262144 cap.
        const auto r = computeAutoZoomFftSize(4096, 0, 768000.0, 12000.0);
        QCOMPARE(r.size, 262144);
    }

    void formula_floors_at_baseline_when_baseline_high()
    {
        // Slider=131072 at full bw: formula gives 131072, no change.
        // Slider=131072 at half bw: formula computes 262144, capped, fine.
        // Slider=131072 at QUADRUPLE bw (e.g., panning past DDC): scale
        // is 0.25 so bare formula gives 32768.  Floor at baseline brings
        // it back to 131072.
        const auto r = computeAutoZoomFftSize(131072, 0, 192000.0, 768000.0);
        QCOMPARE(r.size, 131072);
    }

    // Hysteresis: small bandwidth change relative to current FFT size
    // does not trigger replan.  Bracket: ratio in (0.66, 1.5).
    void hysteresis_skips_replan_when_target_close_to_current()
    {
        // baseline=4096, current=16384, sampleRate=768k.  bwHz=192k
        // gives target=16384 (same as current); zero replan needed.
        const auto r = computeAutoZoomFftSize(4096, 16384, 768000.0, 192000.0);
        QVERIFY(!r.replanned);
        QCOMPARE(r.size, 16384);
    }

    void hysteresis_replans_when_target_doubles()
    {
        // current=16384, bwHz=96k -> target=32768.  Ratio 2.0, outside
        // (0.66, 1.5), so replan.
        const auto r = computeAutoZoomFftSize(4096, 16384, 768000.0, 96000.0);
        QVERIFY(r.replanned);
        QCOMPARE(r.size, 32768);
    }

    void hysteresis_replans_when_target_halves()
    {
        // current=32768, bwHz=192k -> target=16384.  Ratio 0.5, outside
        // band, so replan.
        const auto r = computeAutoZoomFftSize(4096, 32768, 768000.0, 192000.0);
        QVERIFY(r.replanned);
        QCOMPARE(r.size, 16384);
    }

    // Defensive: zero / negative inputs short-circuit without crashing.
    void defensive_zero_sample_rate_is_noop()
    {
        const auto r = computeAutoZoomFftSize(4096, 8192, 0.0, 192000.0);
        QVERIFY(!r.replanned);
        QCOMPARE(r.size, 8192);
    }

    void defensive_zero_bandwidth_is_noop()
    {
        const auto r = computeAutoZoomFftSize(4096, 8192, 768000.0, 0.0);
        QVERIFY(!r.replanned);
        QCOMPARE(r.size, 8192);
    }
};

QTEST_APPLESS_MAIN(TestAutoZoomFft)
#include "tst_auto_zoom_fft.moc"
