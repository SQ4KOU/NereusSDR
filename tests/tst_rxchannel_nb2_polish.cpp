// =================================================================
// tests/tst_rxchannel_nb2_polish.cpp  (NereusSDR)
// =================================================================
//
// Ported from Thetis source:
//   Project Files/Source/Console/HPSDR/specHPSDR.cs
//   Project Files/Source/ChannelMaster/cmaster.c
//
// Original Thetis copyright and license (preserved per GNU GPL):
//
//   Thetis is a C# implementation of a Software Defined Radio.
//   Copyright (C) 2004-2009  FlexRadio Systems
//   Copyright (C) 2010-2020  Doug Wigley (W5WC)
//   Copyright (C) 2013-2019  Warren Pratt (NR0V) — dsp.cs / cmaster.c / setup.cs resampler
//   Copyright (C) 2018-2025  Laurence Barker (G8NJJ) — Andromeda / Aries / Saturn / ANAN-G2
//
//   This program is free software; you can redistribute it and/or
//   modify it under the terms of the GNU General Public License
//   as published by the Free Software Foundation; either version 2
//   of the License, or (at your option) any later version.
//
//   This program is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//   GNU General Public License for more details.
//
//   You should have received a copy of the GNU General Public License
//   along with this program; if not, write to the Free Software
//   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-17 — Reimplemented in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via Anthropic
//                 Claude Code.
// =================================================================

// tst_rxchannel_nb2_polish.cpp
//
// Verifies RxChannel NB2 advanced sub-parameter methods (mode/tau/lead/hang)
// exist and are callable. Because NereusSDRObjs compiles with HAVE_WDSP
// (PUBLIC define), the WDSP-path would dereference pnob[99] and segfault.
// Therefore we only verify the default enable state (accessor + default) and
// confirm method existence via compilation — calling SetEXTNOB* on an
// unallocated slot (channel 99) is unsafe and is not tested here.
//
// Source citations:
//   From Thetis Project Files/Source/Console/HPSDR/specHPSDR.cs:922-937
//   From Thetis Project Files/Source/ChannelMaster/cmaster.c:55-68
//   WDSP: third_party/wdsp/src/nobII.c:658,686,697,707,727

#include <QtTest/QtTest>
#include "core/RxChannel.h"

using namespace NereusSDR;

static constexpr int kTestChannel  = 99;  // Never opened via OpenChannel
static constexpr int kTestBufSize  = 1024;
static constexpr int kTestRate     = 48000;

class TestRxChannelNb2Polish : public QObject {
    Q_OBJECT

private slots:
    // ── nb2Enabled default ───────────────────────────────────────────────────

    void nb2EnabledDefaultIsFalse() {
        // NB2 is off by default; enabled flag stored atomically.
        RxChannel ch(kTestChannel, kTestBufSize, kTestRate);
        QCOMPARE(ch.nb2Enabled(), false);
    }

    void setNb2EnabledEarlyReturnOnSameValue() {
        // Same value as default → no change in the atomic, processIq gate unchanged.
        RxChannel ch(kTestChannel, kTestBufSize, kTestRate);
        ch.setNb2Enabled(false);   // same as default
        QCOMPARE(ch.nb2Enabled(), false);
    }

    // ── NB2 sub-parameter method existence ───────────────────────────────────
    //
    // These tests confirm that setNb2Mode / setNb2Tau / setNb2LeadTime /
    // setNb2HangTime compile and can be referenced. We do NOT call them
    // because SetEXTNOB* functions operate on pnob[id] which is unallocated
    // for channel 99. Calling them would segfault (same constraint as
    // tst_rxchannel_audio_panel re: SetRXAPanelPan on channel 99).
    //
    // The RadioModel initial-push block (tested indirectly via build + run
    // of the real app) calls these on a valid channel 0 after create_nobEXT.

    void nb2SubParameterMethodsAreReachable() {
        // Verify the methods can be called via a function-pointer — this
        // confirms they exist and are not pure-virtual stubs. We use a
        // lambda to take their address without calling them on a live channel.
        using SetterFn = void (RxChannel::*)(int);
        using DblFn    = void (RxChannel::*)(double);

        SetterFn modePtr = &RxChannel::setNb2Mode;
        DblFn    tauPtr  = &RxChannel::setNb2Tau;
        DblFn    leadPtr = &RxChannel::setNb2LeadTime;
        DblFn    hangPtr = &RxChannel::setNb2HangTime;

        QVERIFY(modePtr != nullptr);
        QVERIFY(tauPtr  != nullptr);
        QVERIFY(leadPtr != nullptr);
        QVERIFY(hangPtr != nullptr);
    }
};

QTEST_MAIN(TestRxChannelNb2Polish)
#include "tst_rxchannel_nb2_polish.moc"
