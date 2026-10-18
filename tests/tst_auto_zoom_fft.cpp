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
// NB: the per-stream replan lives in MainWindow::replanFftForStream
// (cannot test in headless mode without instantiating MainWindow +
// signal wiring).  This test covers the FFTEngine-side state API; the
// sizing rule is the shared ControlRanges::planAutoZoomFft and
// largestPlannedFftSize, which replanStreamFftSize() below calls as
// replanFftForStream does.  Only the [0.66, 1.5] hysteresis is
// duplicated here, so a change to it in either place MUST be kept in
// sync.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-06 -- Created in C++20/Qt6 for NereusSDR by J.J. Boyd
//                  (KG4VCF), with AI-assisted transformation via
//                  Anthropic Claude Code.
//   2026-10-10 -- The remote planner's rows call the shared
//                  ControlRanges::planAutoZoomFft in place of a
//                  test-local copy of the per-pixel rule. J.J. Boyd
//                  (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-10 -- The local mirror is per stream, as
//                  MainWindow::replanFftForStream is: every pan of the
//                  stream plans through ControlRanges::planAutoZoomFft
//                  and the engine takes the largest (display pill plan
//                  Task 7). J.J. Boyd (KG4VCF), AI-assisted via
//                  Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>

#include <algorithm>
#include <array>
#include <initializer_list>
#include <limits>
#include <vector>

#include "core/ControlRanges.h"
#include "core/FFTEngine.h"

using namespace NereusSDR;

namespace {

// Mirror of MainWindow::replanFftForStream.  Keep in sync with
// src/gui/MainWindow.cpp.
struct AutoZoomResult {
    int  size;       // the size the engine runs afterwards
    bool replanned;  // true when ratio fell outside hysteresis band
};

// Auto-zoom cap (NereusSDR-original): a time per transform
// (ControlRanges::kAutoZoomMaxTransformSeconds, 0.7 s), so the largest
// power of two <= sampleRate * 0.7 s, within the engine's 1024..262144.
// Slider may go higher manually; auto-zoom won't push above the cap unless
// the slider already did.  Shared with the remote planner.
using ControlRanges::autoZoomMaxFftSize;

// One pan showing the stream: its widget's sample rate and span.
struct PanView {
    double sampleRate;
    double spanHz;
};

AutoZoomResult replanStreamFftSize(int baseline,
                                   int currentSize,
                                   std::initializer_list<PanView> pans,
                                   double hzPerBinTarget = 0.0)
{
    AutoZoomResult r{currentSize, false};
    std::vector<ControlRanges::AutoZoomPlan> plans;
    for (const PanView& pan : pans) {
        // A pan with no rate or span yet has no need to state.
        if (!(pan.sampleRate > 0.0) || !(pan.spanHz > 0.0)) { continue; }
        plans.push_back(ControlRanges::planAutoZoomFft(baseline, pan.sampleRate, pan.spanHz,
                                                       hzPerBinTarget));
    }
    if (plans.empty()) { return r; }
    // Pans that share a stream share its engine: the finest need wins.
    const int targetSize = ControlRanges::largestPlannedFftSize(plans, baseline);
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

// One pan on the stream, the Hz/bin target off.
AutoZoomResult computeAutoZoomFftSize(int baseline,
                                      int currentSize,
                                      double sampleRate,
                                      double bwHz)
{
    return replanStreamFftSize(baseline, currentSize, {{sampleRate, bwHz}});
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

    // Slider=4096 zoomed until the target reaches the cap for the rate
    // (16x at 96k, 32x at 192k, 64x at 768k).  Past this point K starts
    // dropping (auto-zoom can't push higher; user must move slider manually
    // for more).  The cap is about 0.7 s per transform, so it grows with
    // the rate up to the engine's 262144.
    void formula_caps_at_auto_zoom_max_data()
    {
        QTest::addColumn<double>("sampleRate");
        QTest::addColumn<double>("bwHz");
        QTest::addColumn<int>("size");
        QTest::newRow("96k, 6 kHz visible") << 96000.0 << 6000.0 << 65536;
        QTest::newRow("192k, 6 kHz visible") << 192000.0 << 6000.0 << 131072;
        QTest::newRow("768k, 12 kHz visible") << 768000.0 << 12000.0 << 262144;
    }

    void formula_caps_at_auto_zoom_max()
    {
        QFETCH(double, sampleRate);
        QFETCH(double, bwHz);
        QFETCH(int, size);
        const auto r = computeAutoZoomFftSize(4096, 0, sampleRate, bwHz);
        QCOMPARE(r.size, size);
    }

    // Four times deeper than the cap's zoom: the formula would request
    // four times the cap (or past the engine's 262144); auto-zoom holds at
    // the rate's cap, accepting graceful K degradation past this point.
    // 2026-10-09: the 768k row was 65536 under the fixed cap of 2026-05-08;
    // the time-per-transform cap is 262144 (the engine max) at 768k.
    void formula_holds_at_cap_at_deeper_zoom_data()
    {
        QTest::addColumn<double>("sampleRate");
        QTest::addColumn<double>("bwHz");
        QTest::addColumn<int>("size");
        QTest::newRow("96k, 1.5 kHz visible") << 96000.0 << 1500.0 << 65536;
        QTest::newRow("192k, 1.5 kHz visible") << 192000.0 << 1500.0 << 131072;
        QTest::newRow("768k, 3 kHz visible") << 768000.0 << 3000.0 << 262144;
    }

    void formula_holds_at_cap_at_deeper_zoom()
    {
        QFETCH(double, sampleRate);
        QFETCH(double, bwHz);
        QFETCH(int, size);
        const auto r = computeAutoZoomFftSize(4096, 0, sampleRate, bwHz);
        QCOMPARE(r.size, size);
    }

    void formula_baseline_above_cap_is_authoritative()
    {
        // Slider=131072 (manually set above the auto-zoom cap of 65536):
        // auto-zoom respects baseline and never reduces below it.  This
        // is the "user opted in to large FFT" path -- they accepted the
        // longer one-time pause when they moved the slider.
        const auto r = computeAutoZoomFftSize(131072, 0, 192000.0, 768000.0);
        QCOMPARE(r.size, 131072);
    }

    void formula_baseline_above_cap_grows_with_zoom()
    {
        // Slider=131072, zoom in (bwHz < sampleRate): target ramps above
        // baseline up to the implied K, with the *baseline* defining the
        // upper bound (not the cap, since baseline > cap).
        // sampleRate=96k, bwHz=24k -> scale=4 -> desired=524288.  The
        // loop stops at the 96k cap (65536); upper bound = max(
        // baseline=131072, autoZoomCap(96k)=65536) = 131072.  Final=131072.
        // 2026-10-09: this slot ran at 768k under the fixed cap of
        // 2026-05-08; the 768k cap is now 262144, above this baseline, so
        // the premise (baseline > cap) holds at 96k, and 768k is the
        // next slot.
        const auto r = computeAutoZoomFftSize(131072, 0, 96000.0, 24000.0);
        QCOMPARE(r.size, 131072);
    }

    void formula_baseline_below_rate_cap_grows_to_it()
    {
        // Slider=131072 at 768k: the cap there (262144) is above the
        // baseline, so a 4x zoom (desired 524288) grows to 262144.
        const auto r = computeAutoZoomFftSize(131072, 0, 768000.0, 192000.0);
        QCOMPARE(r.size, 262144);
    }

    // The cap per rate: the largest power of two <= rate * 0.7 s, within
    // [kDisplayFftPlanMinSize, kDisplayFftPlanMaxSize].  A rate that is not
    // finite or not positive gives the minimum.
    void max_fft_size_follows_the_rate_data()
    {
        QTest::addColumn<double>("sampleRate");
        QTest::addColumn<int>("cap");
        QTest::newRow("48 kHz") << 48000.0 << 32768;
        QTest::newRow("96 kHz") << 96000.0 << 65536;
        QTest::newRow("192 kHz") << 192000.0 << 131072;
        QTest::newRow("384 kHz") << 384000.0 << 262144;
        QTest::newRow("768 kHz (engine max)") << 768000.0 << 262144;
        QTest::newRow("1.536 MHz (engine max)") << 1536000.0 << 262144;
        QTest::newRow("1 kHz (plan min)") << 1000.0 << 1024;
        QTest::newRow("zero") << 0.0 << 1024;
        QTest::newRow("negative") << -96000.0 << 1024;
        QTest::newRow("NaN") << std::numeric_limits<double>::quiet_NaN() << 1024;
        QTest::newRow("infinity") << std::numeric_limits<double>::infinity() << 1024;
    }

    void max_fft_size_follows_the_rate()
    {
        QFETCH(double, sampleRate);
        QFETCH(int, cap);
        QCOMPARE(autoZoomMaxFftSize(sampleRate), cap);
    }

    // The shared helper: floor at the baseline, cap at the rate's cap
    // unless the baseline is above it.
    void shared_helper_floors_and_caps()
    {
        QCOMPARE(ControlRanges::kAutoZoomMaxTransformSeconds, 0.7);
        QCOMPARE(ControlRanges::autoZoomFftSize(1024, 4096, 96000.0), 4096);
        QCOMPARE(ControlRanges::autoZoomFftSize(32768, 4096, 96000.0), 32768);
        QCOMPARE(ControlRanges::autoZoomFftSize(65536, 4096, 96000.0), 65536);
        QCOMPARE(ControlRanges::autoZoomFftSize(262144, 4096, 96000.0), 65536);
        QCOMPARE(ControlRanges::autoZoomFftSize(262144, 131072, 96000.0), 131072);
        QCOMPARE(ControlRanges::autoZoomFftSize(1024, 262144, 96000.0), 262144);
        QCOMPARE(ControlRanges::autoZoomFftSize(262144, 4096, 192000.0), 131072);
        QCOMPARE(ControlRanges::autoZoomFftSize(262144, 4096, 768000.0), 262144);
        QCOMPARE(ControlRanges::autoZoomFftSize(262144, 131072, 768000.0), 262144);
        QCOMPARE(ControlRanges::autoZoomFftSize(262144, 4096, 48000.0), 32768);
        static_assert(ControlRanges::autoZoomFftSize(262144, 4096, 96000.0) == 65536);
        static_assert(ControlRanges::autoZoomMaxFftSize(768000.0) == 262144);
    }

    // The one sizing rule a local and a remote pan share
    // (ControlRanges::planAutoZoomFft): the points wanted are
    // baseline x rate / span, or rate / target when the Hz/bin target is
    // on, rounded up to a power of two, then floored at the baseline and
    // capped at about 0.7 s per transform. A remote pan's tier is "fine"
    // when the size is above the baseline. The pan's pixel width is not an
    // input.
    // 2026-10-10: these rows called a test-local copy of the remote
    // planner's per-pixel rule (rate x pixels / span on a 1068 px pan);
    // under it the 5549 Hz row was 32768 (96000 x 1068 / 5549 = 18477).
    // The shared rule gives 4096 x 96000 / 5549 = 70861, up to 131072,
    // capped at 65536.
    void remote_planner_caps_zoom_like_local_data()
    {
        QTest::addColumn<double>("sampleRate");
        QTest::addColumn<double>("spanHz");
        QTest::addColumn<double>("hzPerBin");
        QTest::addColumn<int>("baseline");
        QTest::addColumn<int>("size");
        QTest::addColumn<bool>("fine");
        QTest::addColumn<bool>("inCharge");
        QTest::newRow("300 Hz span: the 0.7 s cap") << 96000.0 << 300.0 << 0.0 << 4096 << 65536 << true << false;
        QTest::newRow("96 kHz span: baseline, wide") << 96000.0 << 96000.0 << 0.0 << 4096 << 4096 << false << false;
        QTest::newRow("5549 Hz span: capped, was 32768") << 96000.0 << 5549.0 << 0.0 << 4096 << 65536 << true << false;
        QTest::newRow("24 kHz span: four times the baseline") << 96000.0 << 24000.0 << 0.0 << 4096 << 16384 << true << false;
        QTest::newRow("baseline 131072 wins over the cap") << 96000.0 << 300.0 << 0.0 << 131072 << 131072 << false << false;
        QTest::newRow("Hz/bin 0.5: capped, in charge") << 96000.0 << 96000.0 << 0.5 << 4096 << 65536 << true << true;
        QTest::newRow("Hz/bin 50: baseline, not in charge") << 96000.0 << 96000.0 << 50.0 << 4096 << 4096 << false << false;
        QTest::newRow("Hz/bin 50 replaces the zoom term") << 96000.0 << 300.0 << 50.0 << 4096 << 4096 << false << false;
        QTest::newRow("Hz/bin 0.5, baseline 131072: not in charge") << 96000.0 << 96000.0 << 0.5 << 131072 << 131072 << false << false;
        QTest::newRow("768k, 3 kHz span: engine max") << 768000.0 << 3000.0 << 0.0 << 4096 << 262144 << true << false;
        QTest::newRow("192k, 300 Hz span: 131072") << 192000.0 << 300.0 << 0.0 << 4096 << 131072 << true << false;
        QTest::newRow("rate 0") << 0.0 << 300.0 << 0.5 << 4096 << 4096 << false << false;
        QTest::newRow("span 0") << 96000.0 << 0.0 << 0.5 << 4096 << 4096 << false << false;
        QTest::newRow("negative span") << 96000.0 << -300.0 << 0.0 << 8192 << 8192 << false << false;
        QTest::newRow("rate NaN") << std::numeric_limits<double>::quiet_NaN() << 300.0 << 0.0 << 4096 << 4096 << false << false;
        QTest::newRow("span infinite") << 96000.0 << std::numeric_limits<double>::infinity() << 0.5 << 4096 << 4096 << false << false;
    }

    void remote_planner_caps_zoom_like_local()
    {
        QFETCH(double, sampleRate);
        QFETCH(double, spanHz);
        QFETCH(double, hzPerBin);
        QFETCH(int, baseline);
        QFETCH(int, size);
        QFETCH(bool, fine);
        QFETCH(bool, inCharge);
        const ControlRanges::AutoZoomPlan plan =
            ControlRanges::planAutoZoomFft(baseline, sampleRate, spanHz, hzPerBin);
        QCOMPARE(plan.size, size);
        QCOMPARE(plan.size > baseline, fine);
        QCOMPARE(plan.hzPerBinInCharge, inCharge);
    }

    void plan_is_usable_at_compile_time()
    {
        static_assert(ControlRanges::planAutoZoomFft(4096, 96000.0, 300.0, 0.0).size == 65536);
        static_assert(ControlRanges::planAutoZoomFft(4096, 96000.0, 96000.0, 0.5).hzPerBinInCharge);
        static_assert(!ControlRanges::planAutoZoomFft(4096, 96000.0, 96000.0, 50.0).hzPerBinInCharge);
    }

    // Pans that share a stream run the largest size any of them plans.
    void largest_planned_size_of_several_pans()
    {
        const std::array<ControlRanges::AutoZoomPlan, 2> plans{{{4096, false}, {65536, true}}};
        QCOMPARE(ControlRanges::largestPlannedFftSize(plans, 4096), 65536);
        const std::array<ControlRanges::AutoZoomPlan, 2> reversed{{{65536, false}, {4096, false}}};
        QCOMPARE(ControlRanges::largestPlannedFftSize(reversed, 4096), 65536);
        QCOMPARE(ControlRanges::largestPlannedFftSize({}, 4096), 4096);
        QCOMPARE(ControlRanges::largestPlannedFftSize({}, 16384), 16384);
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

    // Display pill plan Task 7: two pans on one stream at 96 kHz, baseline
    // 4096, spans 96 kHz and 3 kHz.  The engine runs the narrow pan's need
    // (4096 x 96000 / 3000 = 131072, capped at 65536 for 96 kHz), and when
    // the narrow pan zooms back out it returns to 4096.
    void two_pans_on_one_stream_run_the_finest_need()
    {
        const auto zoomed = replanStreamFftSize(4096, 4096,
                                                {{96000.0, 96000.0}, {96000.0, 3000.0}});
        QVERIFY(zoomed.replanned);
        QCOMPARE(zoomed.size, 65536);
        // The order of the pans does not matter.
        QCOMPARE(replanStreamFftSize(4096, 4096, {{96000.0, 3000.0}, {96000.0, 96000.0}}).size,
                 65536);

        const auto zoomedOut = replanStreamFftSize(4096, zoomed.size,
                                                   {{96000.0, 96000.0}, {96000.0, 96000.0}});
        QVERIFY(zoomedOut.replanned);
        QCOMPARE(zoomedOut.size, 4096);
    }

    // The wide pan zooming does not shrink the engine under the narrow
    // pan's need.
    void a_wide_pan_does_not_take_resolution_from_a_narrow_one()
    {
        const auto r = replanStreamFftSize(4096, 65536,
                                           {{96000.0, 48000.0}, {96000.0, 3000.0}});
        QVERIFY(!r.replanned);
        QCOMPARE(r.size, 65536);
    }

    // The Size slider's choice is the floor: baseline 8192 at full span
    // runs 8192 (from the 4096 it ran), half span 16384, back to full span
    // 8192 again.
    void new_baseline_is_the_floor_through_a_zoom()
    {
        const double rate = 192000.0;
        const auto applied = replanStreamFftSize(8192, 4096, {{rate, rate}});
        QVERIFY(applied.replanned);
        QCOMPARE(applied.size, 8192);
        const auto half = replanStreamFftSize(8192, applied.size, {{rate, rate / 2.0}});
        QCOMPARE(half.size, 16384);
        const auto back = replanStreamFftSize(8192, half.size, {{rate, rate}});
        QCOMPARE(back.size, 8192);
    }

    // A lower slider choice shrinks an engine that ran the higher one.
    void lower_baseline_shrinks_at_full_span()
    {
        const auto r = replanStreamFftSize(4096, 16384, {{192000.0, 192000.0}});
        QVERIFY(r.replanned);
        QCOMPARE(r.size, 4096);
    }

    // Baseline 262144 at 48 kHz is above that rate's zoom cap (32768) and
    // is honoured exactly, at full span and zoomed.
    void largest_baseline_at_48k_is_honoured_exactly()
    {
        QCOMPARE(replanStreamFftSize(262144, 4096, {{48000.0, 48000.0}}).size, 262144);
        QCOMPARE(replanStreamFftSize(262144, 4096, {{48000.0, 3000.0}}).size, 262144);
    }

    // The Hz/bin target sizes every pan of the stream alike, whatever its
    // span: 96000 / 5 = 19200 points, up to 32768.
    void hz_per_bin_target_sizes_the_stream()
    {
        const auto r = replanStreamFftSize(4096, 4096,
                                           {{96000.0, 96000.0}, {96000.0, 3000.0}}, 5.0);
        QCOMPARE(r.size, 32768);
    }

    // A pan with no rate or span yet states no need; with no pan that
    // does, the engine keeps its size.
    void pans_without_a_rate_or_span_state_no_need()
    {
        QCOMPARE(replanStreamFftSize(4096, 4096, {{0.0, 96000.0}, {96000.0, 24000.0}}).size,
                 16384);
        const auto none = replanStreamFftSize(4096, 32768, {{0.0, 96000.0}, {96000.0, 0.0}});
        QVERIFY(!none.replanned);
        QCOMPARE(none.size, 32768);
        const auto noPans = replanStreamFftSize(4096, 32768, {});
        QVERIFY(!noPans.replanned);
        QCOMPARE(noPans.size, 32768);
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
