// =================================================================
// tests/tst_averaging_modes.cpp  (NereusSDR)
// =================================================================
//
// Task 2.1 — Detector + Averaging split (handwave fix from 3G-8).
// Tests for SpectrumWidget::applyAveraging() static helper.
//
// Ported from Thetis source:
//   Project Files/Source/Console/HPSDR/specHPSDR.cs:383-415, original licence from Thetis source is included below
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-01 — Ported in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via Anthropic
//                 Claude Code.
// =================================================================

//=================================================================
// specHPSDR.cs
//=================================================================
// Copyright (C) 2010-2018  Doug Wigley
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//=================================================================

// Tests call the static helper directly (no QApplication needed — WDSP-free).

#include <QtTest/QtTest>
#include <QtNumeric>
#include <cmath>

#include "gui/SpectrumWidget.h"

using namespace NereusSDR;

class TestAveragingModes : public QObject {
    Q_OBJECT
private slots:

    // ── None mode ─────────────────────────────────────────────────────────

    void none_mode_passes_through_unchanged()
    {
        // None (av_mode 0) must copy newFrame → state byte-for-byte.
        // From Thetis specHPSDR.cs:383 [v2.10.3.13] AverageMode=0.
        QVector<float> state = {-100.0f, -100.0f, -100.0f};
        QVector<float> newFrame = {-80.0f, -70.0f, -60.0f};
        SpectrumWidget::applyAveraging(newFrame, state, SpectrumAveraging::None, 0.5f);

        QCOMPARE(state.size(), 3);
        QCOMPARE(state[0], -80.0f);
        QCOMPARE(state[1], -70.0f);
        QCOMPARE(state[2], -60.0f);
    }

    void none_mode_alpha_is_irrelevant()
    {
        // Alpha should have no effect in None mode.
        QVector<float> state = {-100.0f};
        QVector<float> newFrame = {-50.0f};
        SpectrumWidget::applyAveraging(newFrame, state, SpectrumAveraging::None, 0.1f);
        QCOMPARE(state[0], -50.0f);

        SpectrumWidget::applyAveraging(newFrame, state, SpectrumAveraging::None, 0.9f);
        QCOMPARE(state[0], -50.0f);
    }

    // ── Recursive mode ────────────────────────────────────────────────────

    void recursive_mode_first_call_bootstraps_state()
    {
        // When state is empty (first frame), applyAveraging bootstraps from
        // newFrame. After the call state == newFrame.
        // From Thetis specHPSDR.cs:383-396 [v2.10.3.13] AverageMode=1.
        QVector<float> state;
        QVector<float> newFrame = {-80.0f, -70.0f};
        SpectrumWidget::applyAveraging(newFrame, state, SpectrumAveraging::Recursive, 0.5f);

        QCOMPARE(state.size(), 2);
        QCOMPARE(state[0], -80.0f);
        QCOMPARE(state[1], -70.0f);
    }

    void recursive_mode_smooths_toward_new_value()
    {
        // With alpha=0.5: state[i] = 0.5 * new + 0.5 * old.
        // After one frame: state = (0.5 * -60) + (0.5 * -100) = -80.
        // From Thetis specHPSDR.cs:383-396 [v2.10.3.13] AverageMode=1.
        QVector<float> state = {-100.0f};
        QVector<float> newFrame = {-60.0f};
        SpectrumWidget::applyAveraging(newFrame, state, SpectrumAveraging::Recursive, 0.5f);

        QCOMPARE(state[0], -80.0f);
    }

    void recursive_mode_alpha_zero_holds_state()
    {
        // Alpha=0 → beta=1 → state stays unchanged (infinite memory).
        QVector<float> state = {-100.0f};
        QVector<float> newFrame = {-60.0f};
        SpectrumWidget::applyAveraging(newFrame, state, SpectrumAveraging::Recursive, 0.0f);
        QCOMPARE(state[0], -100.0f);
    }

    void recursive_mode_alpha_one_passes_through()
    {
        // Alpha=1 → beta=0 → state = newFrame exactly.
        QVector<float> state = {-100.0f};
        QVector<float> newFrame = {-60.0f};
        SpectrumWidget::applyAveraging(newFrame, state, SpectrumAveraging::Recursive, 1.0f);
        QCOMPARE(state[0], -60.0f);
    }

    // ── TimeWindow mode ───────────────────────────────────────────────────

    void time_window_smooths_slower_than_recursive()
    {
        // TimeWindow uses alpha * 0.33 internally, so given the same
        // alpha the blend is heavier toward the old state.
        // After one frame: Recursive state should be CLOSER to newFrame
        // than TimeWindow state.
        QVector<float> stateR = {-100.0f};
        QVector<float> stateTW = {-100.0f};
        QVector<float> newFrame = {-60.0f};
        const float alpha = 0.5f;

        SpectrumWidget::applyAveraging(newFrame, stateR,  SpectrumAveraging::Recursive,   alpha);
        SpectrumWidget::applyAveraging(newFrame, stateTW, SpectrumAveraging::TimeWindow,   alpha);

        // Recursive: -80. TimeWindow: -0.165 * 60 + 0.835 * 100 ≈ -93.5 (still
        // in old-state territory).  Key assertion: TW stays closer to -100.
        QVERIFY(stateTW[0] < stateR[0]);   // TW value is more negative ← closer to old
    }

    void time_window_first_call_bootstraps_state()
    {
        // From Thetis specHPSDR.cs:383-415 [v2.10.3.13] AverageMode=2.
        QVector<float> state;
        QVector<float> newFrame = {-70.0f};
        SpectrumWidget::applyAveraging(newFrame, state, SpectrumAveraging::TimeWindow, 0.5f);
        QCOMPARE(state.size(), 1);
        QCOMPARE(state[0], -70.0f);
    }

    // ── LogRecursive mode ─────────────────────────────────────────────────

    void log_recursive_first_call_bootstraps_state()
    {
        // From Thetis specHPSDR.cs:383-415 [v2.10.3.13] AverageMode=3.
        QVector<float> state;
        QVector<float> newFrame = {-80.0f};
        SpectrumWidget::applyAveraging(newFrame, state, SpectrumAveraging::LogRecursive, 0.5f);
        QCOMPARE(state.size(), 1);
        QCOMPARE(state[0], -80.0f);
    }

    void log_recursive_blends_in_linear_power_domain()
    {
        // LogRecursive converts both frames to linear power, blends, then
        // converts back. Verify the result is between old and new in dB.
        // From Thetis specHPSDR.cs:383-415 [v2.10.3.13] av_sum init to -160.0
        // (analyzer.c:1803 [v2.10.3.13]).
        QVector<float> state = {-80.0f};
        QVector<float> newFrame = {-60.0f};
        SpectrumWidget::applyAveraging(newFrame, state, SpectrumAveraging::LogRecursive, 0.5f);

        // Result should be between -80 and -60 in dB.
        QVERIFY(state[0] > -80.0f);
        QVERIFY(state[0] < -60.0f);
    }

    void log_recursive_alpha_one_passes_through()
    {
        // With alpha=1, result = 10*log10(1 * linNew + 0 * linOld) = newFrame.
        QVector<float> state = {-100.0f};
        QVector<float> newFrame = {-60.0f};
        SpectrumWidget::applyAveraging(newFrame, state, SpectrumAveraging::LogRecursive, 1.0f);
        QVERIFY(std::abs(state[0] - (-60.0f)) < 0.01f);
    }

    void log_recursive_equal_frames_are_stable()
    {
        // If both frames are the same value, log-domain blend returns that value.
        QVector<float> state = {-70.0f};
        QVector<float> newFrame = {-70.0f};
        SpectrumWidget::applyAveraging(newFrame, state, SpectrumAveraging::LogRecursive, 0.5f);
        QVERIFY(std::abs(state[0] - (-70.0f)) < 0.01f);
    }

    // ── Edge cases ────────────────────────────────────────────────────────

    void empty_new_frame_is_no_op()
    {
        QVector<float> state = {-80.0f, -70.0f};
        QVector<float> newFrame;
        // Must not crash or modify state.
        SpectrumWidget::applyAveraging(newFrame, state, SpectrumAveraging::Recursive, 0.5f);
        // State unchanged.
        QCOMPARE(state.size(), 2);
        QCOMPARE(state[0], -80.0f);
    }

    void size_mismatch_resets_state_to_new_frame()
    {
        // When bin count changes (e.g. FFT size change), state is replaced.
        QVector<float> state = {-80.0f, -70.0f, -60.0f};  // 3 bins
        QVector<float> newFrame = {-50.0f, -40.0f};        // 2 bins
        SpectrumWidget::applyAveraging(newFrame, state, SpectrumAveraging::Recursive, 0.5f);

        QCOMPARE(state.size(), 2);
        QCOMPARE(state[0], -50.0f);
        QCOMPARE(state[1], -40.0f);
    }
};

QTEST_APPLESS_MAIN(TestAveragingModes)
#include "tst_averaging_modes.moc"
