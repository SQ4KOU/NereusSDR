// =================================================================
// tests/tst_noise_floor_tracker.cpp  (NereusSDR)
// =================================================================
//
// Ported from Thetis source:
//   Project Files/Source/Console/display.cs, original licence from Thetis source is included below
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-16 — Reimplemented in C++20/Qt6 for NereusSDR by J.J. Boyd
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

// Auto AGC-T — NoiseFloorTracker unit tests. Lerp-based noise floor
// tracker ported from Thetis display.cs:4628-4693. Strict TDD: each
// test was written before the corresponding code path exists (RED),
// verified failing, then the minimal code to pass lands.

#include <QtTest/QtTest>

#include "core/NoiseFloorTracker.h"

using namespace NereusSDR;

class TestNoiseFloorTracker : public QObject {
    Q_OBJECT
private slots:

    void defaultsAreCorrect()
    {
        // Fresh tracker: floor at sentinel -200, not converged, 2s attack.
        NoiseFloorTracker t;
        QCOMPARE(t.noiseFloor(), -200.0f);
        QCOMPARE(t.isGood(), false);
        QCOMPARE(t.attackTimeMs(), 2000.0f);
    }

    void singleFeedUpdatesFloor()
    {
        // After one feed at -90 dBm, the floor moves from -200 toward
        // -90 but hasn't arrived (fast attack snaps to bin average on
        // first feed since m_fastAttackPending starts true).
        NoiseFloorTracker t;
        QVector<float> bins(2048, -90.0f);
        t.feed(bins, 100.0f);
        // Fast attack snaps to -90 on first feed
        QCOMPARE(t.noiseFloor(), -90.0f);
    }

    void convergesAfterManyFeeds()
    {
        // After 50 feeds at 100ms each (5s total, > 2s attack time)
        // with -95 dBm bins, the tracker should converge within 1 dB
        // and report isGood.
        NoiseFloorTracker t;
        QVector<float> bins(2048, -95.0f);
        for (int i = 0; i < 50; ++i) {
            t.feed(bins, 100.0f);
        }
        QVERIFY(std::abs(t.noiseFloor() - (-95.0f)) < 1.0f);
        QVERIFY(t.isGood());
    }

    void fastAttackResetsLerp()
    {
        // Converge to -80, then trigger fast attack and feed -110.
        // The tracker should jump close to -110 (within 5 dB).
        NoiseFloorTracker t;
        QVector<float> bins80(2048, -80.0f);
        for (int i = 0; i < 50; ++i) {
            t.feed(bins80, 100.0f);
        }
        QVERIFY(std::abs(t.noiseFloor() - (-80.0f)) < 1.0f);

        t.triggerFastAttack();
        QVector<float> bins110(2048, -110.0f);
        t.feed(bins110, 100.0f);
        // Fast attack snaps directly to bin average
        QVERIFY(std::abs(t.noiseFloor() - (-110.0f)) < 5.0f);
    }

    void isGoodAfterAttackPeriod()
    {
        // isGood() is false until elapsed exceeds attackTimeMs (2000ms).
        NoiseFloorTracker t;
        QVector<float> bins(2048, -95.0f);

        // First feed triggers fast attack — elapsed resets to 0
        t.feed(bins, 100.0f);
        QVERIFY(!t.isGood());

        // Feed 19 more frames at 100ms each = 1900ms elapsed (< 2000)
        for (int i = 0; i < 19; ++i) {
            t.feed(bins, 100.0f);
        }
        QVERIFY(!t.isGood());

        // One more frame = 2000ms elapsed (>= 2000)
        t.feed(bins, 100.0f);
        QVERIFY(t.isGood());
    }

    void emptyBinsNoOp()
    {
        // Empty bins vector doesn't crash and leaves floor at sentinel.
        NoiseFloorTracker t;
        QVector<float> empty;
        t.feed(empty, 100.0f);
        QCOMPARE(t.noiseFloor(), -200.0f);
        QCOMPARE(t.isGood(), false);
    }
};

QTEST_MAIN(TestNoiseFloorTracker)
#include "tst_noise_floor_tracker.moc"
