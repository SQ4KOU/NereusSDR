// =================================================================
// tests/tst_rxchannel_nb2_polish.cpp  (NereusSDR)
// =================================================================
//
// Ported from Thetis sources:
//   Project Files/Source/Console/HPSDR/specHPSDR.cs, original licence from Thetis source is included below
//   Project Files/Source/ChannelMaster/cmaster.c, original licence from Thetis source is included below
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-17 — Reimplemented in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via Anthropic
//                 Claude Code.
// =================================================================

/*
*
* Copyright (C) 2010-2018  Doug Wigley 
* 
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/*  cmaster.c

This file is part of a program that implements a Software-Defined Radio.

Copyright (C) 2014-2019 Warren Pratt, NR0V

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

The author can be reached by email at  

warren@wpratt.com

*/

#include <QtTest/QtTest>
#include "core/RxChannel.h"
#include "core/NbFamily.h"
#include "core/WdspTypes.h"

using namespace NereusSDR;

static constexpr int kTestChannel  = 99;  // Never opened via OpenChannel
static constexpr int kTestBufSize  = 1024;
static constexpr int kTestRate     = 48000;

// Phase 3G sub-epic B rewrote RxChannel's NB surface around a single
// NbFamily member exposing a tri-state NbMode + an NbTuning struct.
// The old setNb2Enabled / setNb2Mode / setNb2Tau / setNb2LeadTime /
// setNb2HangTime methods were removed. This test is the polish/refactor
// test against the new API.
class TestRxChannelNb2Polish : public QObject {
    Q_OBJECT

private slots:
    // ── nbMode default ───────────────────────────────────────────────────────

    void nbModeDefaultIsOff() {
        // NB starts Off; NbFamily::m_mode atomic default is NbMode::Off.
        RxChannel ch(kTestChannel, kTestBufSize, kTestRate);
        QCOMPARE(ch.nbMode(), NbMode::Off);
    }

    // ── nbMode mutual exclusion ──────────────────────────────────────────────

    void nbModeRotatesAcrossThreeStates() {
        // Off → NB → NB2 → Off via cycleNbMode (design doc §sub-epic B,
        // mirrors Thetis console.cs:43513 chkNB CheckState transition).
        RxChannel ch(kTestChannel, kTestBufSize, kTestRate);

        ch.setNbMode(NbMode::NB);
        QCOMPARE(ch.nbMode(), NbMode::NB);

        // Switching to NB2 leaves ONLY NB2 active (mutual exclusion —
        // the previous two-atomic design couldn't enforce this).
        ch.setNbMode(NbMode::NB2);
        QCOMPARE(ch.nbMode(), NbMode::NB2);

        ch.setNbMode(NbMode::Off);
        QCOMPARE(ch.nbMode(), NbMode::Off);
    }

    void cycleNbModeRotates() {
        // Off → NB → NB2 → Off — matches space-bar and button-click behaviour.
        QCOMPARE(cycleNbMode(NbMode::Off),  NbMode::NB);
        QCOMPARE(cycleNbMode(NbMode::NB),   NbMode::NB2);
        QCOMPARE(cycleNbMode(NbMode::NB2),  NbMode::Off);
    }

    // ── NbTuning defaults via RxChannel ──────────────────────────────────────
    //
    // RxChannel::nbTuning() returns the NbFamily-owned tuning struct.
    // With a default-constructed channel, the struct should hold the
    // cmaster.c:43-68 [v2.10.3.13] byte-for-byte defaults.

    void nbTuningDefaultsMatchThetisCmaster() {
        RxChannel ch(kTestChannel, kTestBufSize, kTestRate);
        const NbTuning& t = ch.nbTuning();
        QCOMPARE(t.nbTauMs,      0.1);
        QCOMPARE(t.nbHangMs,     0.1);
        QCOMPARE(t.nbAdvMs,      0.1);
        QCOMPARE(t.nbBacktau,    0.05);
        QCOMPARE(t.nbThreshold,  30.0);
        QCOMPARE(t.nb2Mode,      0);
        QCOMPARE(t.nb2Threshold, 30.0);
    }

    // ── NB setters compile ───────────────────────────────────────────────────
    //
    // These tests confirm that setNbMode / setNbTuning / setNbThreshold /
    // setNbLagMs / setNbLeadMs / setNbTransitionMs exist as member
    // functions. We take their addresses rather than calling them on live
    // WDSP state — SetEXTANB* / SetEXTNOB* operate on panb[id] / pnob[id]
    // which is unallocated for channel 99 (same constraint as
    // tst_rxchannel_audio_panel re: SetRXAPanelPan on channel 99).

    void nbSubParameterMethodsAreReachable() {
        using ModeFn    = void (RxChannel::*)(NbMode);
        using TuningFn  = void (RxChannel::*)(const NbTuning&);
        using DblFn     = void (RxChannel::*)(double);

        ModeFn   modePtr    = &RxChannel::setNbMode;
        TuningFn tuningPtr  = &RxChannel::setNbTuning;
        DblFn    threshPtr  = &RxChannel::setNbThreshold;
        DblFn    lagPtr     = &RxChannel::setNbLagMs;
        DblFn    leadPtr    = &RxChannel::setNbLeadMs;
        DblFn    tauPtr     = &RxChannel::setNbTransitionMs;

        QVERIFY(modePtr   != nullptr);
        QVERIFY(tuningPtr != nullptr);
        QVERIFY(threshPtr != nullptr);
        QVERIFY(lagPtr    != nullptr);
        QVERIFY(leadPtr   != nullptr);
        QVERIFY(tauPtr    != nullptr);
    }
};

QTEST_MAIN(TestRxChannelNb2Polish)
#include "tst_rxchannel_nb2_polish.moc"
