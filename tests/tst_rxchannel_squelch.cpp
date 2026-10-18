// =================================================================
// tests/tst_rxchannel_squelch.cpp  (NereusSDR)
// =================================================================
//
// Ported from Thetis sources:
//   Project Files/Source/Console/radio.cs, original licence from Thetis source is included below
//   Project Files/Source/Console/console.cs, original licence from Thetis source is included below
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-17 — Reimplemented in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via Anthropic
//                 Claude Code.
//   2026-10-14 - FM squelch threshold values sent to WDSP, from Thetis
//                 console.cs:47295-47304 [v2.10.3.15], by J.J. Boyd
//                 (KG4VCF), with AI-assisted implementation via Anthropic
//                 Claude Code.
// =================================================================

//=================================================================
// radio.cs
//=================================================================
// PowerSDR is a C# implementation of a Software Defined Radio.
// Copyright (C) 2004-2009  FlexRadio Systems
// Copyright (C) 2010-2020  Doug Wigley
// Copyright (C) 2019-2026  Richard Samphire
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

//=================================================================
// console.cs
//=================================================================
// Thetis is a C# implementation of a Software Defined Radio.
// Copyright (C) 2004-2009  FlexRadio Systems 
// Copyright (C) 2010-2020  Doug Wigley
// Credit is given to Sizenko Alexander of Style-7 (http://www.styleseven.com/) for the Digital-7 font.
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
// Modifications to support the Behringer Midi controllers
// by Chris Codella, W2PA, May 2017.  Indicated by //-W2PA comment lines. 
// Modifications for using the new database import function.  W2PA, 29 May 2017
// Support QSK, possible with Protocol-2 firmware v1.7 (Orion-MkI and Orion-MkII), and later.  W2PA, 5 April 2019 
// Modfied heavily - Copyright (C) 2019-2026 Richard Samphire (MW0LGE)
// ApacheLabs G2E support added throughout Thetis in various files, all changes marked  //N1GP G2E added
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
//
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Final modifictions by MW0LGE Richard Samphire - 19th April 2026
// Nothing further added by him after this date, and his repo is now in archive https://github.com/ramdor/Thetis
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <QtTest/QtTest>
#include "core/RxChannel.h"

using namespace NereusSDR;

// Use a channel id that definitely has no WDSP channel allocated.
// All paths tested here must not invoke any WDSP API.
static constexpr int kTestChannel  = 99;  // Never opened via OpenChannel
static constexpr int kTestBufSize  = 1024;
static constexpr int kTestRate     = 48000;

class TestRxChannelSquelch : public QObject {
    Q_OBJECT

private slots:
    // ── setSsqlEnabled ───────────────────────────────────────────────────────

    void ssqlEnabledDefaultIsFalse() {
        // From Thetis radio.cs:1185 — _bSSqlOn default = false
        // Upstream inline attribution preserved verbatim (radio.cs:1183):
        //   // MW0LGE [2.9.0.8]
        //   // Voice Squeltch - SSQL from 1.21 WDSP
        RxChannel ch(kTestChannel, kTestBufSize, kTestRate);
        QCOMPARE(ch.ssqlEnabled(), false);
    }

    void setSsqlEnabledEarlyReturnOnSameValue() {
        // Same value as default (false) — early return, no WDSP call.
        RxChannel ch(kTestChannel, kTestBufSize, kTestRate);
        ch.setSsqlEnabled(false);   // same as default → early return
        QCOMPARE(ch.ssqlEnabled(), false);
    }

    // ── setAmsqEnabled ───────────────────────────────────────────────────────

    void amsqEnabledDefaultIsFalse() {
        // From Thetis radio.cs:1293 — rx_am_squelch_on default = false
        RxChannel ch(kTestChannel, kTestBufSize, kTestRate);
        QCOMPARE(ch.amsqEnabled(), false);
    }

    void setAmsqEnabledEarlyReturnOnSameValue() {
        // Same value as default (false) — early return, no WDSP call.
        RxChannel ch(kTestChannel, kTestBufSize, kTestRate);
        ch.setAmsqEnabled(false);   // same as default → early return
        QCOMPARE(ch.amsqEnabled(), false);
    }

    // ── setFmsqEnabled ───────────────────────────────────────────────────────

    void fmsqEnabledDefaultIsFalse() {
        // From Thetis radio.cs:1312 — rx_fm_squelch_on default = false
        RxChannel ch(kTestChannel, kTestBufSize, kTestRate);
        QCOMPARE(ch.fmsqEnabled(), false);
    }

    void setFmsqEnabledEarlyReturnOnSameValue() {
        // Same value as default (false) — early return, no WDSP call.
        RxChannel ch(kTestChannel, kTestBufSize, kTestRate);
        ch.setFmsqEnabled(false);   // same as default → early return
        QCOMPARE(ch.fmsqEnabled(), false);
    }

    // ── fmsqThresholdForWdsp: the value setFmsqThresh sends to WDSP ──────────
    //
    // From Thetis console.cs:47295-47304 [v2.10.3.15]
    //   //[2.10.3.5]MW0LGE convert to a 0-100 scale from a -160 to 0 scale
    // The -160..0 slider becomes n = (int)(((value + 160) / 160f) * 100f), and
    // FMSquelchThreshold = (float)Math.Pow(10.0, -2.0 * n / 100.0) goes to
    // SetRXAFMSQThreshold (radio.cs:1278-1291). WDSP unmutes while the noise
    // average is below 0.9 * threshold (fmsq.c:162,189,247-251), so a larger
    // threshold opens the squelch more easily.

    void fmsqThresholdForWdsp_data() {
        QTest::addColumn<double>("slider");
        QTest::addColumn<double>("expected");
        // Expected values are Thetis's float results widened to double.
        QTest::newRow("-160 opens most easily, n=0")
            << -160.0 << static_cast<double>(1.0f);
        QTest::newRow("-150 old NereusSDR default, n=6")
            << -150.0 << static_cast<double>(static_cast<float>(0.7585775750291838));
        QTest::newRow("-80 midpoint, n=50")
            << -80.0 << static_cast<double>(0.1f);
        QTest::newRow("-1 truncates to n=99")
            << -1.0 << static_cast<double>(static_cast<float>(0.010471285480508996));
        // These two rows tell (int) truncation from rounding: -2 gives 98.75
        // (truncation n=98, rounding 99) and -159 gives 0.625 (truncation
        // n=0, rounding 1).
        QTest::newRow("-2 truncates to n=98, not 99")
            << -2.0 << static_cast<double>(static_cast<float>(0.01096478196143185));
        QTest::newRow("-159 truncates to n=0, not 1")
            << -159.0 << static_cast<double>(1.0f);
        QTest::newRow("0 is the tightest and the default, n=100")
            << 0.0 << static_cast<double>(0.01f);
    }

    void fmsqThresholdForWdsp() {
        QFETCH(double, slider);
        QFETCH(double, expected);
        QCOMPARE(RxChannel::fmsqThresholdForWdsp(slider), expected);
    }

    void fmsqThresholdForWdspClampsToSliderRange() {
        // ptbSquelch spans -160..0 (console.Designer.cs:7572-7573), so a
        // value past either end sends that end's threshold.
        QCOMPARE(RxChannel::fmsqThresholdForWdsp(-200.0),
                 RxChannel::fmsqThresholdForWdsp(-160.0));
        QCOMPARE(RxChannel::fmsqThresholdForWdsp(20.0),
                 RxChannel::fmsqThresholdForWdsp(0.0));
    }
};

QTEST_MAIN(TestRxChannelSquelch)
#include "tst_rxchannel_squelch.moc"
