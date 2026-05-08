// =================================================================
// tests/tst_active_peak_hold.cpp  (NereusSDR)
// =================================================================
//
// Task 2.5 — ActivePeakHoldTrace unit tests.
//
// Ported from Thetis source:
//   Project Files/Source/Console/display.cs, original licence from Thetis source is included below
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-01 — Created in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via Anthropic
//                 Claude Code.
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
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
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

// Tests run WDSP-free (no QApplication needed for pure data logic).

#include <QtTest/QtTest>
#include "gui/spectrum/ActivePeakHoldTrace.h"

#include <cmath>

using namespace NereusSDR;

class TestActivePeakHold : public QObject {
    Q_OBJECT

private slots:

    // ---- Construction + size ----

    void default_construction_has_zero_size()
    {
        ActivePeakHoldTrace trace;
        QCOMPARE(trace.size(), 0);
        QVERIFY(!trace.enabled());
    }

    void construction_with_nBins_sets_size()
    {
        ActivePeakHoldTrace trace(256);
        QCOMPARE(trace.size(), 256);
        // All bins should start at -infinity
        QVERIFY(!std::isfinite(trace.peak(0)));
        QVERIFY(!std::isfinite(trace.peak(255)));
    }

    void resize_updates_size_and_clears()
    {
        ActivePeakHoldTrace trace(64);
        trace.setEnabled(true);
        QVector<float> bins(64, -40.0f);
        trace.update(bins);
        QCOMPARE(trace.peak(0), -40.0f);

        trace.resize(128);
        QCOMPARE(trace.size(), 128);
        // All peaks reset to -infinity after resize
        QVERIFY(!std::isfinite(trace.peak(0)));
        QVERIFY(!std::isfinite(trace.peak(127)));
    }

    // ---- Disabled does nothing ----

    void disabled_does_not_update()
    {
        ActivePeakHoldTrace trace(256);
        trace.setEnabled(false);

        QVector<float> bins(256, -40.0f);
        trace.update(bins);

        // Peaks must remain -infinity when disabled
        QVERIFY(!std::isfinite(trace.peak(0)));
    }

    void disabled_does_not_decay()
    {
        ActivePeakHoldTrace trace(256);
        trace.setEnabled(true);
        QVector<float> bins(256, -40.0f);
        trace.update(bins);
        QCOMPARE(trace.peak(0), -40.0f);

        trace.setEnabled(false);
        trace.tickFrame(30);

        // Peaks should be unchanged (tickFrame no-ops when disabled)
        QCOMPARE(trace.peak(0), -40.0f);
    }

    // ---- Peak tracking ----

    void peak_value_persists_until_decay()
    {
        ActivePeakHoldTrace trace(256);
        trace.setEnabled(true);
        trace.setDropDbPerSec(6.0);

        QVector<float> bins(256, -100.0f);
        bins[100] = -40.0f;
        trace.update(bins);
        QCOMPARE(trace.peak(100), -40.0f);

        // Next frame: live value drops to -50. Decay 6 dB/s / 30 fps = 0.2 dB/frame.
        bins[100] = -50.0f;
        trace.tickFrame(30);   // peak → -40.2
        trace.update(bins);    // -50 < -40.2 so peak unchanged

        // Peak should still be ~-40.2 (decay only), not -50
        QVERIFY(trace.peak(100) > -40.5f);
        QVERIFY(trace.peak(100) <= -40.0f);
    }

    void peak_raises_on_stronger_signal()
    {
        ActivePeakHoldTrace trace(256);
        trace.setEnabled(true);

        QVector<float> bins(256, -60.0f);
        trace.update(bins);
        QCOMPARE(trace.peak(0), -60.0f);

        bins[0] = -30.0f;
        trace.update(bins);
        QCOMPARE(trace.peak(0), -30.0f);  // raised to stronger value
    }

    void peak_does_not_lower_on_weaker_signal()
    {
        ActivePeakHoldTrace trace(256);
        trace.setEnabled(true);
        trace.setDropDbPerSec(0.0);  // no decay so only update() can change values

        QVector<float> bins(256, -30.0f);
        trace.update(bins);

        bins[0] = -60.0f;
        trace.update(bins);
        QCOMPARE(trace.peak(0), -30.0f);  // stays at stronger previous value
    }

    // ---- Decay ----

    void decay_reduces_peak_by_expected_amount()
    {
        ActivePeakHoldTrace trace(256);
        trace.setEnabled(true);
        trace.setDropDbPerSec(6.0);

        QVector<float> bins(256, -40.0f);
        trace.update(bins);

        // 1 tick at 30 fps → 0.2 dB drop
        trace.tickFrame(30);
        QVERIFY(qAbs(trace.peak(0) - (-40.2f)) < 0.001f);
    }

    void zero_fps_tickFrame_is_noop()
    {
        ActivePeakHoldTrace trace(256);
        trace.setEnabled(true);
        QVector<float> bins(256, -40.0f);
        trace.update(bins);

        trace.tickFrame(0);  // must not divide by zero / crash
        QCOMPARE(trace.peak(0), -40.0f);
    }

    void infinite_peaks_not_decayed()
    {
        // Bins start at -inf; tickFrame must not attempt arithmetic on them.
        ActivePeakHoldTrace trace(256);
        trace.setEnabled(true);
        trace.tickFrame(30);  // must not produce NaN
        QVERIFY(!std::isfinite(trace.peak(0)));
    }

    // ---- TX state gating ----

    void on_tx_false_disables_update_during_tx()
    {
        ActivePeakHoldTrace trace(256);
        trace.setEnabled(true);
        trace.setOnTx(false);   // don't update during TX
        trace.setTxActive(true);

        QVector<float> bins(256, -40.0f);
        trace.update(bins);

        // Peaks should stay at -inf — no update during TX
        QVERIFY(!std::isfinite(trace.peak(0)));
    }

    void on_tx_true_allows_update_during_tx()
    {
        ActivePeakHoldTrace trace(256);
        trace.setEnabled(true);
        trace.setOnTx(true);    // continue updating during TX
        trace.setTxActive(true);

        QVector<float> bins(256, -40.0f);
        trace.update(bins);
        QCOMPARE(trace.peak(0), -40.0f);
    }

    void tx_inactive_always_allows_update()
    {
        // When TX is NOT active, onTx flag is irrelevant — updates always proceed.
        ActivePeakHoldTrace trace(256);
        trace.setEnabled(true);
        trace.setOnTx(false);
        trace.setTxActive(false);  // not transmitting

        QVector<float> bins(256, -40.0f);
        trace.update(bins);
        QCOMPARE(trace.peak(0), -40.0f);
    }

    // ---- clear() ----

    void clear_resets_all_peaks_to_infinity()
    {
        ActivePeakHoldTrace trace(256);
        trace.setEnabled(true);
        QVector<float> bins(256, -40.0f);
        trace.update(bins);
        QCOMPARE(trace.peak(0), -40.0f);

        trace.clear();
        QVERIFY(!std::isfinite(trace.peak(0)));
        QVERIFY(!std::isfinite(trace.peak(255)));
    }

    // ---- Accessors ----

    void peak_out_of_range_returns_neg_infinity()
    {
        ActivePeakHoldTrace trace(16);
        QVERIFY(!std::isfinite(trace.peak(-1)));
        QVERIFY(!std::isfinite(trace.peak(16)));
        QVERIFY(!std::isfinite(trace.peak(1000)));
    }

    void configuration_round_trip()
    {
        ActivePeakHoldTrace trace;
        trace.setEnabled(true);
        trace.setDurationMs(3000);
        trace.setDropDbPerSec(12.0);
        trace.setFill(true);
        trace.setOnTx(true);

        QVERIFY(trace.enabled());
        QCOMPARE(trace.durationMs(), 3000);
        QCOMPARE(trace.dropDbPerSec(), 12.0);
        QVERIFY(trace.fill());
    }
};

QTEST_APPLESS_MAIN(TestActivePeakHold)
#include "tst_active_peak_hold.moc"
