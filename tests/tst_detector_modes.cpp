// =================================================================
// tests/tst_detector_modes.cpp  (NereusSDR)
// =================================================================
//
// Task 2.1 — Detector + Averaging split (handwave fix from 3G-8).
// Tests for SpectrumWidget::applyDetector() static helper.
//
// Ported from Thetis source:
//   Project Files/Source/Console/HPSDR/specHPSDR.cs:302-321, original licence from Thetis source is included below
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

class TestDetectorModes : public QObject {
    Q_OBJECT
private slots:

    // ── Peak detector ──────────────────────────────────────────────────────

    void peak_detector_returns_max_in_window()
    {
        // 8 input bins, reduce to 4 pixels. Peak detector must take the
        // maximum value in each 2-bin window.
        // From Thetis specHPSDR.cs:302-321 [v2.10.3.13] DetTypePan = 0 ("Peak").
        QVector<float> input = {-80.0f, -60.0f, -50.0f, -90.0f,
                                -70.0f, -40.0f, -85.0f, -75.0f};
        QVector<float> output;
        SpectrumWidget::applyDetector(input, output, SpectrumDetector::Peak, 4);

        QCOMPARE(output.size(), 4);
        QCOMPARE(output[0], -60.0f);   // max(-80, -60)
        QCOMPARE(output[1], -50.0f);   // max(-50, -90)
        QCOMPARE(output[2], -40.0f);   // max(-70, -40)
        QCOMPARE(output[3], -75.0f);   // max(-85, -75)
    }

    void peak_detector_passthrough_at_1to1()
    {
        // When input and output sizes are equal, every window is 1 bin,
        // so Peak is identity.
        QVector<float> input = {-100.0f, -90.0f, -80.0f};
        QVector<float> output;
        SpectrumWidget::applyDetector(input, output, SpectrumDetector::Peak, 3);

        QCOMPARE(output.size(), 3);
        QCOMPARE(output[0], -100.0f);
        QCOMPARE(output[1],  -90.0f);
        QCOMPARE(output[2],  -80.0f);
    }

    // ── Rosenfell detector ─────────────────────────────────────────────────

    void rosenfell_even_pixels_take_max()
    {
        // Even output pixels (0, 2, …) must contain the window max.
        // From Thetis specHPSDR.cs:302 [v2.10.3.13] DetTypePan = 1 ("Rosenfell").
        QVector<float> input = {-80.0f, -60.0f, -50.0f, -90.0f};
        QVector<float> output;
        SpectrumWidget::applyDetector(input, output, SpectrumDetector::Rosenfell, 2);

        QCOMPARE(output.size(), 2);
        QCOMPARE(output[0], -60.0f);   // even → max(-80, -60)
        QCOMPARE(output[1], -90.0f);   // odd  → min(-50, -90)
    }

    void rosenfell_odd_pixels_take_min()
    {
        QVector<float> input = {-80.0f, -60.0f, -50.0f, -90.0f,
                                -40.0f, -70.0f, -55.0f, -45.0f};
        QVector<float> output;
        SpectrumWidget::applyDetector(input, output, SpectrumDetector::Rosenfell, 4);

        QCOMPARE(output.size(), 4);
        // pixel 0 (even): max
        QCOMPARE(output[0], -60.0f);
        // pixel 1 (odd): min
        QCOMPARE(output[1], -90.0f);
        // pixel 2 (even): max
        QCOMPARE(output[2], -40.0f);
        // pixel 3 (odd): min
        QCOMPARE(output[3], -55.0f);
    }

    // ── Average detector ──────────────────────────────────────────────────

    void average_detector_computes_arithmetic_mean()
    {
        // From Thetis specHPSDR.cs:302 [v2.10.3.13] DetTypePan = 2 ("Average").
        QVector<float> input = {-80.0f, -60.0f, -50.0f, -90.0f};
        QVector<float> output;
        SpectrumWidget::applyDetector(input, output, SpectrumDetector::Average, 2);

        QCOMPARE(output.size(), 2);
        QCOMPARE(output[0], (-80.0f + -60.0f) / 2.0f);   // = -70
        QCOMPARE(output[1], (-50.0f + -90.0f) / 2.0f);   // = -70
    }

    void average_detector_single_bin_is_identity()
    {
        QVector<float> input = {-55.0f};
        QVector<float> output;
        SpectrumWidget::applyDetector(input, output, SpectrumDetector::Average, 1);
        QCOMPARE(output.size(), 1);
        QCOMPARE(output[0], -55.0f);
    }

    // ── Sample detector ───────────────────────────────────────────────────

    void sample_detector_takes_first_bin_in_window()
    {
        // From Thetis specHPSDR.cs:302 [v2.10.3.13] DetTypePan = 3 ("Sample").
        QVector<float> input = {-80.0f, -60.0f, -50.0f, -90.0f};
        QVector<float> output;
        SpectrumWidget::applyDetector(input, output, SpectrumDetector::Sample, 2);

        QCOMPARE(output.size(), 2);
        QCOMPARE(output[0], -80.0f);   // first bin of window 0
        QCOMPARE(output[1], -50.0f);   // first bin of window 1
    }

    // ── RMS detector ──────────────────────────────────────────────────────

    void rms_detector_single_bin_round_trips_dbm()
    {
        // For a single bin the RMS result must equal the input bin value.
        // Power path: dBm → linear → squared → sum → sqrt → dBm.
        // For a 1-element window: sqrt((lin^2) / 1) = lin → same dBm.
        // From Thetis specHPSDR.cs:302 [v2.10.3.13] DetTypePan = 4 ("RMS").
        // Pan only — not present in WF detector combo.
        QVector<float> input = {-60.0f};
        QVector<float> output;
        SpectrumWidget::applyDetector(input, output, SpectrumDetector::RMS, 1);

        QCOMPARE(output.size(), 1);
        // Tolerance 0.01 dB — float rounding in pow/log chain.
        QVERIFY(std::abs(output[0] - (-60.0f)) < 0.01f);
    }

    void rms_detector_two_equal_bins_returns_same_level()
    {
        // If both bins are at the same level, RMS must equal that level.
        QVector<float> input = {-80.0f, -80.0f};
        QVector<float> output;
        SpectrumWidget::applyDetector(input, output, SpectrumDetector::RMS, 1);

        QCOMPARE(output.size(), 1);
        QVERIFY(std::abs(output[0] - (-80.0f)) < 0.01f);
    }

    // ── Edge cases ────────────────────────────────────────────────────────

    void empty_input_produces_empty_output()
    {
        QVector<float> input;
        QVector<float> output;
        SpectrumWidget::applyDetector(input, output, SpectrumDetector::Peak, 4);
        QVERIFY(output.isEmpty());
    }

    void zero_output_bins_produces_empty_output()
    {
        QVector<float> input = {-80.0f, -60.0f};
        QVector<float> output;
        SpectrumWidget::applyDetector(input, output, SpectrumDetector::Peak, 0);
        QVERIFY(output.isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestDetectorModes)
#include "tst_detector_modes.moc"
