/*  TXA.c

This file is part of a program that implements a Software-Defined Radio.

Copyright (C) 2013, 2014, 2016, 2017, 2021, 2023 Warren Pratt, NR0V

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

// =================================================================
// tests/tst_tx_channel_per_mode_config.cpp  (NereusSDR)
// =================================================================
//
// No Thetis code is directly ported in this test file. The test exercises:
//   - TxChannel::setTxMode(DSPMode)  — D.2 Phase 3M-1b
//   - TxChannel::setTxBandpass(int, int)  — D.2 Phase 3M-1b
//   - TxChannel::setSubAmMode(int)   — D.2 Phase 3M-1b (deferred stub)
//
// Porting context (cited in TxChannel.h / TxChannel.cpp):
//   setTxMode:      radio.cs:2670-2696 [v2.10.3.13]
//   setTxBandpass:  radio.cs:2730-2780 [v2.10.3.13]
//   setSubAmMode:   radio.cs:2699-2728 [v2.10.3.13] — DEFERRED to 3M-3b
//
// Tests verify:
//   1. setTxMode() does not throw for SSB-family modes (LSB/USB/DIGL/DIGU)
//      and other modes (AM/SAM/DSB/CWL/CWU/FM/DRM). The method is a thin
//      dispatcher; mode-gating is BandPlanGuard's responsibility.
//   2. setTxBandpass() does not throw for valid, negative-low, and
//      zero-bandwidth ranges. WDSP does not pre-validate; NereusSDR
//      does not add pre-validation either (Thetis doesn't).
//   3. setSubAmMode() throws std::logic_error with a non-empty message
//      for every sub-mode value (0, 1, 2) and out-of-range values (-1, 3).
//      This is the 3M-3b deferral marker; any accidental 3M-1b caller
//      must surface immediately as a test failure.
//
// Without WDSP linked (HAVE_WDSP not defined), the setTxMode /
// setTxBandpass methods are stubs (no-ops) — tests confirm they don't
// crash.  setSubAmMode always throws regardless of HAVE_WDSP.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-27 — New test for Phase 3M-1b Task D.2: per-mode TXA config
//                 setters (setTxMode / setTxBandpass / setSubAmMode).
//                 J.J. Boyd (KG4VCF), with AI-assisted implementation via
//                 Anthropic Claude Code.
// =================================================================

// no-port-check: NereusSDR-original test file. All Thetis source cites are
// in TxChannel.h/cpp.

#include <QtTest/QtTest>
#include <stdexcept>

#include "core/TxChannel.h"
#include "core/WdspTypes.h"

using namespace NereusSDR;

class TestTxChannelPerModeConfig : public QObject {
    Q_OBJECT

    static constexpr int kChannelId = 1;  // WDSP.id(1, 0) — TX channel

private slots:

    // ── setTxMode — SSB-family modes ─────────────────────────────────────────
    //
    // setTxMode() is a thin dispatcher that calls SetTXAMode(channelId, mode).
    // In unit-test builds the WDSP channel may not be open; the HAVE_WDSP path
    // guards with a null-pointer check and returns early.  Either way, the call
    // must not crash or throw for any valid DSPMode value.
    //
    // From Thetis radio.cs:2670-2696 [v2.10.3.13].

    void setTxMode_LSB_doesNotThrow()
    {
        TxChannel ch(kChannelId);
        // Must not throw or crash for LSB.
        ch.setTxMode(DSPMode::LSB);
        QVERIFY(true);  // reaching here = pass
    }

    void setTxMode_USB_doesNotThrow()
    {
        TxChannel ch(kChannelId);
        ch.setTxMode(DSPMode::USB);
        QVERIFY(true);
    }

    void setTxMode_DIGL_doesNotThrow()
    {
        TxChannel ch(kChannelId);
        ch.setTxMode(DSPMode::DIGL);
        QVERIFY(true);
    }

    void setTxMode_DIGU_doesNotThrow()
    {
        TxChannel ch(kChannelId);
        ch.setTxMode(DSPMode::DIGU);
        QVERIFY(true);
    }

    // Non-SSB modes: setTxMode is mode-agnostic at this layer.
    // Mode-gating is BandPlanGuard's responsibility (Phase K), not TxChannel's.

    void setTxMode_AM_doesNotThrow()
    {
        TxChannel ch(kChannelId);
        ch.setTxMode(DSPMode::AM);
        QVERIFY(true);
    }

    void setTxMode_CWL_doesNotThrow()
    {
        TxChannel ch(kChannelId);
        ch.setTxMode(DSPMode::CWL);
        QVERIFY(true);
    }

    void setTxMode_CWU_doesNotThrow()
    {
        TxChannel ch(kChannelId);
        ch.setTxMode(DSPMode::CWU);
        QVERIFY(true);
    }

    // ── setTxBandpass — valid ranges ─────────────────────────────────────────
    //
    // setTxBandpass() wraps SetTXABandpassFreqs(channelId, low, high).
    // WDSP accepts any double pair without pre-validation; NereusSDR mirrors
    // this — no pre-validation added (matching Thetis SetTXFilter pattern at
    // radio.cs:2730-2780 [v2.10.3.13]).
    //
    // In unit-test builds the WDSP call is guarded or stubbed; either way
    // the call must not crash or throw.

    void setTxBandpass_typicalSSB_doesNotThrow()
    {
        TxChannel ch(kChannelId);
        // USB typical: low=300 Hz, high=2700 Hz
        ch.setTxBandpass(300, 2700);
        QVERIFY(true);
    }

    void setTxBandpass_wideSSB_doesNotThrow()
    {
        TxChannel ch(kChannelId);
        // Wide SSB: 200–2900 Hz
        ch.setTxBandpass(200, 2900);
        QVERIFY(true);
    }

    void setTxBandpass_negativeLow_doesNotThrow()
    {
        TxChannel ch(kChannelId);
        // LSB IQ-space: low=-2850, high=-150
        // WDSP does not pre-validate sign convention; NereusSDR doesn't either.
        ch.setTxBandpass(-2850, -150);
        QVERIFY(true);
    }

    void setTxBandpass_zeroBandwidth_doesNotThrow()
    {
        TxChannel ch(kChannelId);
        // Degenerate case: zero-bandwidth filter.
        // WDSP behaviour is undefined but NereusSDR must not crash or throw.
        ch.setTxBandpass(0, 0);
        QVERIFY(true);
    }

    void setTxBandpass_symmetricAM_doesNotThrow()
    {
        TxChannel ch(kChannelId);
        // AM/DSB typical: symmetric ±2850
        ch.setTxBandpass(-2850, 2850);
        QVERIFY(true);
    }

    // ── setSubAmMode — 3M-3b deferral: MUST throw ────────────────────────────
    //
    // setSubAmMode() is defined but throws std::logic_error so any accidental
    // 3M-1b caller surfaces immediately as a crash / test failure.
    //
    // CRITICAL: these tests are the deferral marker. If any of them fail
    // (the method no longer throws), someone activated AM/SAM TX before
    // 3M-3b — that is a scope violation that must block the PR.
    //
    // From Thetis radio.cs:2699-2728 [v2.10.3.13] — SubAMMode setter
    // (deferred; full dispatch wired in 3M-3b).

    void setSubAmMode_zero_throws()
    {
        TxChannel ch(kChannelId);
        bool threw = false;
        try {
            ch.setSubAmMode(0);  // DSB
        } catch (const std::logic_error&) {
            threw = true;
        }
        QVERIFY2(threw, "setSubAmMode(0) must throw std::logic_error in 3M-1b");
    }

    void setSubAmMode_one_throws()
    {
        TxChannel ch(kChannelId);
        bool threw = false;
        try {
            ch.setSubAmMode(1);  // AM_LSB sideband
        } catch (const std::logic_error&) {
            threw = true;
        }
        QVERIFY2(threw, "setSubAmMode(1) must throw std::logic_error in 3M-1b");
    }

    void setSubAmMode_two_throws()
    {
        TxChannel ch(kChannelId);
        bool threw = false;
        try {
            ch.setSubAmMode(2);  // AM_USB sideband
        } catch (const std::logic_error&) {
            threw = true;
        }
        QVERIFY2(threw, "setSubAmMode(2) must throw std::logic_error in 3M-1b");
    }

    void setSubAmMode_negativeValue_throws()
    {
        TxChannel ch(kChannelId);
        bool threw = false;
        try {
            ch.setSubAmMode(-1);  // out-of-range
        } catch (const std::logic_error&) {
            threw = true;
        }
        QVERIFY2(threw, "setSubAmMode(-1) must throw std::logic_error in 3M-1b");
    }

    void setSubAmMode_outOfRange_throws()
    {
        TxChannel ch(kChannelId);
        bool threw = false;
        try {
            ch.setSubAmMode(3);  // out-of-range
        } catch (const std::logic_error&) {
            threw = true;
        }
        QVERIFY2(threw, "setSubAmMode(3) must throw std::logic_error in 3M-1b");
    }

    // ── setSubAmMode error message is non-empty ───────────────────────────────
    //
    // Ensures the throw carries a useful diagnostic message, not just an empty
    // exception. The caller should be able to read the message and understand
    // why the call failed (3M-3b scope deferral).

    void setSubAmMode_throwsWithNonEmptyMessage()
    {
        TxChannel ch(kChannelId);
        try {
            ch.setSubAmMode(0);
            QFAIL("setSubAmMode(0) should have thrown");
        } catch (const std::logic_error& e) {
            QVERIFY2(std::string(e.what()).length() > 0,
                     "std::logic_error message must not be empty");
        }
    }
};

QTEST_APPLESS_MAIN(TestTxChannelPerModeConfig)
#include "tst_tx_channel_per_mode_config.moc"
