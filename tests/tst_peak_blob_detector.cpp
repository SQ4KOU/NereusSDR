// =================================================================
// tests/tst_peak_blob_detector.cpp  (NereusSDR)
// =================================================================
//
// Task 2.6 — PeakBlobDetector unit tests.
//
// Ported from Thetis source:
//   Project Files/Source/Console/display.cs, original licence from
//   Thetis source is included below.
//
// Tests run WDSP-free (no QApplication needed for pure data logic).
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-01 — Created in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via
//                 Anthropic Claude Code.
//                 Ported from Thetis display.cs v2.10.3.13
//                 (commit 501e3f5).
// =================================================================

//=================================================================
// display.cs
//=================================================================
// Thetis is a C# implementation of a Software Defined Radio.
// Copyright (C) 2004-2009  FlexRadio Systems
// Copyright (C) 2010-2020  Doug Wigley (W5WC)
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
// 02111-1307, USA.
//
// You may contact us via email at: sales@flex-radio.com.
// Paper mail may be sent to:
//    FlexRadio Systems
//    8900 Marybank Dr.
//    Austin, TX 78750
//    USA
//
//=================================================================
// Waterfall AGC Modifications Copyright (C) 2013 Phil Harman (VK6APH)
// Transitions to directX and continual modifications Copyright (C) 2020-2025 Richard Samphire (MW0LGE)
//=================================================================
//
//============================================================================================//
// Dual-Licensing Statement (Applies Only to Author's Contributions, Richard Samphire MW0LGE) //
// ------------------------------------------------------------------------------------------ //
// For any code originally written by Richard Samphire MW0LGE, or for any modifications       //
// made by him, the copyright holder for those portions (Richard Samphire) reserves the       //
// right to use, license, and distribute such code under different terms, including           //
// closed-source and proprietary licences, in addition to the GNU General Public License      //
// granted above. Nothing in this statement restricts any rights granted to recipients under  //
// the GNU GPL. Code contributed by others (not Richard Samphire) remains licensed under      //
// its original terms and is not affected by this dual-licensing statement in any way.        //
// Richard Samphire can be reached by email at :  mw0lge@grange-lane.co.uk                    //
//============================================================================================//

#include <QtTest/QtTest>
#include "gui/spectrum/PeakBlobDetector.h"

using namespace NereusSDR;

class TestPeakBlobDetector : public QObject {
    Q_OBJECT

private slots:

    // ── Default construction ─────────────────────────────────────────────────

    void default_construction_has_no_blobs()
    {
        PeakBlobDetector det;
        QVERIFY(!det.enabled());
        QCOMPARE(det.count(), 3);   // Thetis Display.cs:4407 default 3
        QCOMPARE(det.blobs().size(), 0);
    }

    // ── Disabled ─────────────────────────────────────────────────────────────

    void disabled_clears_blobs()
    {
        QVector<float> bins(256, -100.0f);
        bins[100] = -30.0f;

        PeakBlobDetector det;
        det.setEnabled(true);
        det.setCount(1);
        det.update(bins, 0, 255);
        QCOMPARE(det.blobs().size(), 1);

        det.setEnabled(false);
        QCOMPARE(det.blobs().size(), 0);
    }

    void disabled_update_keeps_empty()
    {
        QVector<float> bins(256, -100.0f);
        bins[100] = -30.0f;

        PeakBlobDetector det;
        // enabled = false (default)
        det.update(bins, 0, 255);
        QCOMPARE(det.blobs().size(), 0);
    }

    // ── Empty bins ───────────────────────────────────────────────────────────

    void empty_bins_clears_blobs()
    {
        PeakBlobDetector det;
        det.setEnabled(true);
        det.update(QVector<float>{}, 0, 0);
        QCOMPARE(det.blobs().size(), 0);
    }

    // ── Top-N selection ──────────────────────────────────────────────────────

    void finds_top_n_peaks_in_synthetic_spectrum()
    {
        // Three clear local maxima at bins 100, 300, 500
        QVector<float> bins(1024, -100.0f);
        bins[100] = -30.0f;   // strongest
        bins[300] = -50.0f;   // third
        bins[500] = -40.0f;   // second

        PeakBlobDetector det;
        det.setEnabled(true);
        det.setCount(3);
        det.update(bins, 0, 1023);

        const auto& blobs = det.blobs();
        QCOMPARE(blobs.size(), 3);
        // Sorted descending by dBm
        QCOMPARE(blobs[0].binIndex, 100);   // -30 dBm
        QCOMPARE(blobs[1].binIndex, 500);   // -40 dBm
        QCOMPARE(blobs[2].binIndex, 300);   // -50 dBm
    }

    void count_limits_results()
    {
        QVector<float> bins(256, -100.0f);
        bins[10]  = -30.0f;
        bins[50]  = -40.0f;
        bins[100] = -50.0f;
        bins[150] = -60.0f;

        PeakBlobDetector det;
        det.setEnabled(true);
        det.setCount(2);
        det.update(bins, 0, 255);

        QCOMPARE(det.blobs().size(), 2);
        // Only top 2 returned
        QCOMPARE(det.blobs()[0].binIndex, 10);
        QCOMPARE(det.blobs()[1].binIndex, 50);
    }

    void count_clamped_when_fewer_peaks_than_n()
    {
        // Only 2 local maxima exist — requesting 5 should return 2
        QVector<float> bins(256, -100.0f);
        bins[50]  = -40.0f;
        bins[150] = -50.0f;

        PeakBlobDetector det;
        det.setEnabled(true);
        det.setCount(5);
        det.update(bins, 0, 255);

        QVERIFY(det.blobs().size() <= 2);
    }

    // ── Inside-filter constraint ─────────────────────────────────────────────

    void inside_filter_excludes_outside_bins()
    {
        QVector<float> bins(1024, -100.0f);
        bins[50]  = -30.0f;   // outside passband (below filterLowBin=100)
        bins[600] = -40.0f;   // inside [100, 900]

        PeakBlobDetector det;
        det.setEnabled(true);
        det.setCount(2);
        det.setInsideFilterOnly(true);
        det.update(bins, /*filterLowBin=*/100, /*filterHighBin=*/900);

        QCOMPARE(det.blobs().size(), 1);
        QCOMPARE(det.blobs()[0].binIndex, 600);
    }

    void inside_filter_false_finds_all_bins()
    {
        QVector<float> bins(1024, -100.0f);
        bins[50]  = -30.0f;   // would be excluded by filter, but insideOnly=false
        bins[600] = -40.0f;

        PeakBlobDetector det;
        det.setEnabled(true);
        det.setCount(2);
        det.setInsideFilterOnly(false);
        det.update(bins, 100, 900);

        QCOMPARE(det.blobs().size(), 2);
    }

    // ── Hold / decay ─────────────────────────────────────────────────────────

    void hold_persists_max_within_hold_window()
    {
        QVector<float> bins(256, -100.0f);
        bins[100] = -40.0f;

        PeakBlobDetector det;
        det.setEnabled(true);
        det.setCount(1);
        det.setHoldEnabled(true);
        det.setHoldMs(500);
        det.setHoldDrop(true);
        det.setFallDbPerSec(6.0);
        det.update(bins, 0, 255);
        QCOMPARE(det.blobs()[0].max_dBm, -40.0f);

        // After 100 ms (within hold window) — value should be unchanged
        det.tickFrame(30, 100);
        // feed lower signal to force merge path
        bins[100] = -60.0f;
        det.update(bins, 0, 255);
        QCOMPARE(det.blobs()[0].max_dBm, -40.0f);
    }

    void hold_decays_after_hold_window_expires()
    {
        QVector<float> bins(256, -100.0f);
        bins[100] = -40.0f;

        PeakBlobDetector det;
        det.setEnabled(true);
        det.setCount(1);
        det.setHoldEnabled(true);
        det.setHoldMs(500);
        det.setHoldDrop(true);
        det.setFallDbPerSec(6.0);
        det.update(bins, 0, 255);
        QCOMPARE(det.blobs()[0].max_dBm, -40.0f);

        // Advance past hold window (600 ms total) so decay fires
        // From Thetis Display.cs:5483 [v2.10.3.13]:
        //   entry.max_dBm -= m_dBmPerSecondPeakBlobFall / (float)m_nFps;
        for (int i = 0; i < 20; ++i) {
            det.tickFrame(30, 30);   // 20 * 30 ms = 600 ms total
        }
        // 10+ frames after hold expired → some decay must have occurred
        QVERIFY(det.blobs()[0].max_dBm < -40.0f);
    }

    void hold_disabled_does_not_merge()
    {
        QVector<float> bins(256, -100.0f);
        bins[100] = -40.0f;

        PeakBlobDetector det;
        det.setEnabled(true);
        det.setCount(1);
        det.setHoldEnabled(false);   // hold OFF
        det.update(bins, 0, 255);
        QCOMPARE(det.blobs()[0].max_dBm, -40.0f);

        // Update with weaker signal — no hold, so new (lower) value wins
        bins[100] = -60.0f;
        det.update(bins, 0, 255);
        QCOMPARE(det.blobs()[0].max_dBm, -60.0f);
    }

    // ── tick without update ───────────────────────────────────────────────────

    void tick_frame_advances_time_even_with_empty_blobs()
    {
        PeakBlobDetector det;
        det.setEnabled(true);
        det.setHoldEnabled(true);
        det.setHoldDrop(true);
        // Should not crash with empty blob list
        det.tickFrame(30, 33);
        QCOMPARE(det.blobs().size(), 0);
    }

    // ── Decay math exact value check ─────────────────────────────────────────

    void decay_rate_matches_thetis_formula()
    {
        // From Thetis Display.cs:5483 [v2.10.3.13]:
        //   entry.max_dBm -= m_dBmPerSecondPeakBlobFall / (float)m_nFps;
        // At 6 dB/s and 30 fps, each frame beyond hold = -0.2 dB.
        QVector<float> bins(256, -100.0f);
        bins[100] = -40.0f;

        PeakBlobDetector det;
        det.setEnabled(true);
        det.setCount(1);
        det.setHoldEnabled(true);
        det.setHoldMs(0);   // immediate decay (hold = 0 ms)
        det.setHoldDrop(true);
        det.setFallDbPerSec(6.0);
        det.update(bins, 0, 255);

        // Tick one frame at 30 fps, 33 ms elapsed — hold is already past (0 ms)
        det.tickFrame(30, 33);

        // Expected decay: 6.0 / 30 = 0.2 dB
        const float expected = -40.0f - 6.0f / 30.0f;
        QVERIFY(qFuzzyCompare(det.blobs()[0].max_dBm, expected));
    }
};

QTEST_APPLESS_MAIN(TestPeakBlobDetector)
#include "tst_peak_blob_detector.moc"
