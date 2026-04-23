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

    // RxChannel-level NbTuning getter (nbTuning()) removed 2026-04-22 for
    // strict Thetis parity. NbTuning default-value parity is now covered
    // exclusively by tst_nb_family::tuning_nb1_defaults_match_thetis_runtime.

    // ── NB setters compile ───────────────────────────────────────────────────
    //
    // setNbMode is the only per-slice NB entry point RxChannel exposes
    // after the 2026-04-22 strict-Thetis-parity refactor. Tuning setters
    // (setNbTuning / setNbThreshold / setNbLagMs / setNbLeadMs /
    // setNbTransitionMs) were removed from RxChannel — NB tuning is now
    // global per-channel and lives inside NbFamily, live-pushed from
    // DspSetupPages via SetEXTANB* directly on channel 0.

    void setNbModeIsReachable() {
        using ModeFn = void (RxChannel::*)(NbMode);
        ModeFn modePtr = &RxChannel::setNbMode;
        QVERIFY(modePtr != nullptr);
    }
};

QTEST_MAIN(TestRxChannelNb2Polish)
#include "tst_rxchannel_nb2_polish.moc"
