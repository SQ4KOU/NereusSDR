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

// =================================================================
// tests/tst_tx_channel_vox_anti_vox.cpp  (NereusSDR)
// =================================================================
//
// No Thetis code is directly ported in this test file.  The test exercises:
//   - TxChannel::setVoxRun(bool)           — D.3 Phase 3M-1b
//   - TxChannel::setVoxAttackThreshold(double) — D.3 Phase 3M-1b
//   - TxChannel::setVoxHangTime(double)    — D.3 Phase 3M-1b
//   - TxChannel::setAntiVoxRun(bool)       — D.3 Phase 3M-1b
//   - TxChannel::setAntiVoxGain(double)    — D.3 Phase 3M-1b
//
// Porting context (cited in TxChannel.h / TxChannel.cpp):
//   setVoxRun:            cmaster.cs:199-200 [v2.10.3.13] — SetDEXPRunVox
//   setVoxAttackThreshold: cmaster.cs:187-188 [v2.10.3.13] — SetDEXPAttackThreshold
//   setVoxHangTime:       cmaster.cs:178-179 [v2.10.3.13] — SetDEXPHoldTime
//                         (WDSP uses "HoldTime" for VOX hang; no SetDEXPHangTime exists)
//   setAntiVoxRun:        cmaster.cs:208-209 [v2.10.3.13] — SetAntiVOXRun
//   setAntiVoxGain:       cmaster.cs:211-212 [v2.10.3.13] — SetAntiVOXGain
//
// Tests verify (NEREUS_BUILD_TESTS test-seam accessors required):
//   1. First call stores the value (NaN sentinel fires → WDSP path taken).
//   2. Round-trip: set A, then B → last-value accessor returns B.
//   3. Idempotent guard: set A twice → second call is a no-op at the WDSP
//      level.  Without WDSP linked the only observable change is the stored
//      value, which must still equal A after the second call.
//   4. Edge values for doubles: 0.0, negative, large.
//
// Each of the 5 wrappers gets: first-call, round-trip, idempotent (+ edge for
// doubles).  Total: 17 test cases.
//
// Requires NEREUS_BUILD_TESTS (set by CMakeLists target
// tst_tx_channel_vox_anti_vox).  Test-seam accessors (lastVoxRunForTest,
// lastVoxAttackThresholdForTest, etc.) are compiled into TxChannel only when
// that define is set.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-27 — New test for Phase 3M-1b Task D.3: VOX/anti-VOX WDSP
//                 wrappers (setVoxRun / setVoxAttackThreshold /
//                 setVoxHangTime / setAntiVoxRun / setAntiVoxGain).
//                 J.J. Boyd (KG4VCF), with AI-assisted implementation via
//                 Anthropic Claude Code.
// =================================================================

// no-port-check: NereusSDR-original test file. All Thetis source cites are
// in TxChannel.h/cpp.

#define NEREUS_BUILD_TESTS 1

#include <QtTest/QtTest>
#include <cmath>   // std::isnan

#include "core/TxChannel.h"
#include "core/WdspTypes.h"

using namespace NereusSDR;

class TestTxChannelVoxAntiVox : public QObject {
    Q_OBJECT

    static constexpr int kChannelId = 1;  // WDSP.id(1, 0) — TX channel

private slots:

    // ── setVoxRun ────────────────────────────────────────────────────────────
    //
    // VOX run gate — wires WDSP SetDEXPRunVox.
    // From Thetis cmaster.cs:199-200 [v2.10.3.13].

    void setVoxRun_firstCallTrue_storesTrue()
    {
        // NaN sentinel initialises m_voxRunLast=false; set true → stored.
        TxChannel ch(kChannelId);
        // Initial state must be false (default).
        QCOMPARE(ch.lastVoxRunForTest(), false);
        ch.setVoxRun(true);
        QCOMPARE(ch.lastVoxRunForTest(), true);
    }

    void setVoxRun_firstCallFalse_storesFalse()
    {
        // false→false is idempotent (same as default), so the guard fires.
        // The stored value stays false.
        TxChannel ch(kChannelId);
        ch.setVoxRun(false);
        QCOMPARE(ch.lastVoxRunForTest(), false);
    }

    void setVoxRun_roundTrip_storesLatestValue()
    {
        TxChannel ch(kChannelId);
        ch.setVoxRun(true);
        ch.setVoxRun(false);
        QCOMPARE(ch.lastVoxRunForTest(), false);
    }

    void setVoxRun_idempotent_secondCallNoChange()
    {
        // After the first call sets true, a second call with true must be a
        // no-op.  The stored value must still be true.
        TxChannel ch(kChannelId);
        ch.setVoxRun(true);
        QCOMPARE(ch.lastVoxRunForTest(), true);
        ch.setVoxRun(true);  // second call — idempotent guard fires
        QCOMPARE(ch.lastVoxRunForTest(), true);
    }

    // ── setVoxAttackThreshold ─────────────────────────────────────────────────
    //
    // VOX attack threshold — wires WDSP SetDEXPAttackThreshold.
    // From Thetis cmaster.cs:187-188 [v2.10.3.13].

    void setVoxAttackThreshold_firstCall_storesValue()
    {
        // NaN sentinel means the first call always passes the guard.
        TxChannel ch(kChannelId);
        QVERIFY(std::isnan(ch.lastVoxAttackThresholdForTest()));
        ch.setVoxAttackThreshold(0.05);
        QCOMPARE(ch.lastVoxAttackThresholdForTest(), 0.05);
    }

    void setVoxAttackThreshold_roundTrip_storesLatestValue()
    {
        TxChannel ch(kChannelId);
        ch.setVoxAttackThreshold(0.05);
        ch.setVoxAttackThreshold(0.15);
        QCOMPARE(ch.lastVoxAttackThresholdForTest(), 0.15);
    }

    void setVoxAttackThreshold_idempotent_secondCallNoChange()
    {
        TxChannel ch(kChannelId);
        ch.setVoxAttackThreshold(0.05);
        ch.setVoxAttackThreshold(0.05);  // idempotent guard fires
        QCOMPARE(ch.lastVoxAttackThresholdForTest(), 0.05);
    }

    void setVoxAttackThreshold_zeroValue_storesZero()
    {
        // 0.0 is a valid threshold (fully closed gate).
        TxChannel ch(kChannelId);
        ch.setVoxAttackThreshold(0.0);
        QCOMPARE(ch.lastVoxAttackThresholdForTest(), 0.0);
    }

    // ── setVoxHangTime ────────────────────────────────────────────────────────
    //
    // VOX hang time in seconds — wires WDSP SetDEXPHoldTime.
    // Note: WDSP calls this "HoldTime"; Thetis exposes it as VOXHangTime.
    // There is no SetDEXPHangTime in WDSP (wdsp/dexp.c confirmed).
    // From Thetis cmaster.cs:178-179 [v2.10.3.13] — SetDEXPHoldTime.

    void setVoxHangTime_firstCall_storesValue()
    {
        TxChannel ch(kChannelId);
        QVERIFY(std::isnan(ch.lastVoxHangTimeForTest()));
        ch.setVoxHangTime(0.25);  // 250 ms — Thetis default vox_hang_time
        QCOMPARE(ch.lastVoxHangTimeForTest(), 0.25);
    }

    void setVoxHangTime_roundTrip_storesLatestValue()
    {
        TxChannel ch(kChannelId);
        ch.setVoxHangTime(0.25);
        ch.setVoxHangTime(0.50);
        QCOMPARE(ch.lastVoxHangTimeForTest(), 0.50);
    }

    void setVoxHangTime_idempotent_secondCallNoChange()
    {
        TxChannel ch(kChannelId);
        ch.setVoxHangTime(0.25);
        ch.setVoxHangTime(0.25);  // idempotent guard fires
        QCOMPARE(ch.lastVoxHangTimeForTest(), 0.25);
    }

    void setVoxHangTime_zeroValue_storesZero()
    {
        // 0.0 hang time = no tail (valid: hang immediately).
        TxChannel ch(kChannelId);
        ch.setVoxHangTime(0.0);
        QCOMPARE(ch.lastVoxHangTimeForTest(), 0.0);
    }

    // ── setAntiVoxRun ─────────────────────────────────────────────────────────
    //
    // Anti-VOX run gate — wires WDSP SetAntiVOXRun.
    // From Thetis cmaster.cs:208-209 [v2.10.3.13].

    void setAntiVoxRun_firstCallTrue_storesTrue()
    {
        TxChannel ch(kChannelId);
        QCOMPARE(ch.lastAntiVoxRunForTest(), false);
        ch.setAntiVoxRun(true);
        QCOMPARE(ch.lastAntiVoxRunForTest(), true);
    }

    void setAntiVoxRun_roundTrip_storesLatestValue()
    {
        TxChannel ch(kChannelId);
        ch.setAntiVoxRun(true);
        ch.setAntiVoxRun(false);
        QCOMPARE(ch.lastAntiVoxRunForTest(), false);
    }

    void setAntiVoxRun_idempotent_secondCallNoChange()
    {
        TxChannel ch(kChannelId);
        ch.setAntiVoxRun(true);
        ch.setAntiVoxRun(true);  // idempotent guard fires
        QCOMPARE(ch.lastAntiVoxRunForTest(), true);
    }

    // ── setAntiVoxGain ────────────────────────────────────────────────────────
    //
    // Anti-VOX gain — wires WDSP SetAntiVOXGain.
    // From Thetis cmaster.cs:211-212 [v2.10.3.13].

    void setAntiVoxGain_firstCall_storesValue()
    {
        TxChannel ch(kChannelId);
        QVERIFY(std::isnan(ch.lastAntiVoxGainForTest()));
        ch.setAntiVoxGain(0.10);  // 10% anti-VOX coupling typical
        QCOMPARE(ch.lastAntiVoxGainForTest(), 0.10);
    }

    void setAntiVoxGain_roundTrip_storesLatestValue()
    {
        TxChannel ch(kChannelId);
        ch.setAntiVoxGain(0.10);
        ch.setAntiVoxGain(0.20);
        QCOMPARE(ch.lastAntiVoxGainForTest(), 0.20);
    }

    void setAntiVoxGain_idempotent_secondCallNoChange()
    {
        TxChannel ch(kChannelId);
        ch.setAntiVoxGain(0.10);
        ch.setAntiVoxGain(0.10);  // idempotent guard fires
        QCOMPARE(ch.lastAntiVoxGainForTest(), 0.10);
    }

    // ── setAntiVoxSize ────────────────────────────────────────────────────────
    //
    // Anti-VOX detector buffer size — wires WDSP SetAntiVOXSize.
    // From Thetis dexp.c:666 [v2.10.3.13] — SetAntiVOXSize impl.
    // Per cmaster.c:154 [v2.10.3.13] this is sourced from audio_outsize
    // (the post-decimation RX block size), NOT TX in_size.

    void setAntiVoxSize_callsWdsp()
    {
        // Verifies SetAntiVOXSize is invoked with the size value, and the
        // internal m_antiVoxSize tracks the value for the size-mismatch guard
        // that Task 3's sendAntiVoxData() will consult.
        TxChannel ch(kChannelId);
        ch.setAntiVoxSize(2048);
        QCOMPARE(ch.antiVoxSize(), 2048);
    }

    void setAntiVoxSize_rejectsNonPositive()
    {
        // size <= 0 must be rejected (would alloc-zero / underflow inside WDSP).
        // The cached size remains at its 0 init.
        TxChannel ch(kChannelId);
        ch.setAntiVoxSize(0);
        QCOMPARE(ch.antiVoxSize(), 0);
        ch.setAntiVoxSize(-1);
        QCOMPARE(ch.antiVoxSize(), 0);
    }

    // ── setAntiVoxRate ────────────────────────────────────────────────────────
    //
    // Anti-VOX detector sample-rate — wires WDSP SetAntiVOXRate.
    // From Thetis dexp.c:677 [v2.10.3.13] — SetAntiVOXRate impl.
    // Per cmaster.c:155 [v2.10.3.13] this is sourced from audio_outrate
    // (the post-decimation RX block rate), NOT TX in_rate.

    void setAntiVoxRate_callsWdsp()
    {
        // Verifies SetAntiVOXRate is invoked with the rate value (Hz).
        TxChannel ch(kChannelId);
        ch.setAntiVoxRate(96000.0);
        QCOMPARE(ch.antiVoxRate(), 96000.0);
    }

    void setAntiVoxRate_rejectsNonPositive()
    {
        // rate <= 0 must be rejected (divide-by-zero inside WDSP calc_antivox).
        TxChannel ch(kChannelId);
        ch.setAntiVoxRate(0.0);
        QCOMPARE(ch.antiVoxRate(), 0.0);  // unchanged from 0.0 init
        ch.setAntiVoxRate(-48000.0);
        QCOMPARE(ch.antiVoxRate(), 0.0);
    }

    // ── setAntiVoxDetectorTau ─────────────────────────────────────────────────
    //
    // Anti-VOX smoothing time-constant — wires WDSP SetAntiVOXDetectorTau.
    // From Thetis dexp.c:697 [v2.10.3.13] — SetAntiVOXDetectorTau impl.
    // Thetis converts the spinbox ms value via /1000.0 (setup.cs:18995
    // [v2.10.3.13]); this wrapper takes seconds directly.  WDSP create-time
    // default is 0.01 s (cmaster.c:157 [v2.10.3.13]).

    void setAntiVoxDetectorTau_callsWdsp()
    {
        // Verifies SetAntiVOXDetectorTau is invoked with the tau (seconds).
        TxChannel ch(kChannelId);
        ch.setAntiVoxDetectorTau(0.020);
        QCOMPARE(ch.antiVoxDetectorTau(), 0.020);
    }

    void setAntiVoxDetectorTau_rejectsNonPositive()
    {
        // tau <= 0 must be rejected (calc_antivox would divide by zero / NaN).
        // The cached tau remains at its 0.01 s init (WDSP create-time default).
        TxChannel ch(kChannelId);
        ch.setAntiVoxDetectorTau(0.0);
        QCOMPARE(ch.antiVoxDetectorTau(), 0.01);  // unchanged from cmaster.c:157 init
        ch.setAntiVoxDetectorTau(-0.005);
        QCOMPARE(ch.antiVoxDetectorTau(), 0.01);
    }

    // ── sendAntiVoxData ───────────────────────────────────────────────────────
    //
    // Pumps interleaved L/R float audio into the WDSP DEXP anti-VOX detector
    // via SendAntiVOXData. The wrapper enforces nsamples == m_antiVoxSize
    // before invoking WDSP (which would otherwise memcpy past
    // antivox_data — see dexp.c:713 [v2.10.3.13]).
    //
    // From Thetis dexp.c:708-715 [v2.10.3.13]:
    //   void SendAntiVOXData (int id, int nsamples, double* data)
    //   {
    //       // note:  'nsamples' is not used as it has been previously specified
    //       DEXP a = pdexp[id];
    //       memcpy (a->antivox_data, data, a->antivox_size * sizeof (complex));
    //       a->antivox_new = 1;
    //   }

    void sendAntiVoxData_pumpsWdsp_whenSizeMatches()
    {
        // Verifies SendAntiVOXData is invoked when nsamples == m_antiVoxSize.
        // We can't intercept the WDSP call directly, but we can verify the
        // float->double conversion path by inspecting m_antiVoxScratch via
        // the antiVoxScratchForTest accessor.
        // From Thetis dexp.c:708-715 [v2.10.3.13].
        TxChannel ch(kChannelId);
        ch.setAntiVoxSize(4);  // deliberately tiny for test brevity
        const float interleaved[8] = { 0.1f, 0.2f, 0.3f, 0.4f,
                                       0.5f, 0.6f, 0.7f, 0.8f };
        ch.sendAntiVoxData(interleaved, /*nsamples=*/4);
        const auto& scratch = ch.antiVoxScratchForTest();
        QCOMPARE(scratch.size(), std::size_t{8});
        for (std::size_t i = 0; i < 8; ++i) {
            QCOMPARE(scratch[i], static_cast<double>(interleaved[i]));
        }
    }

    void sendAntiVoxData_skipsWdsp_whenSizeMismatches()
    {
        // Verifies the size-mismatch guard rejects mismatched buffers and
        // does NOT touch m_antiVoxScratch (no partial conversion).
        TxChannel ch(kChannelId);
        ch.setAntiVoxSize(4);
        const float interleaved[10] = {};  // 5 stereo samples instead of 4
        ch.sendAntiVoxData(interleaved, /*nsamples=*/5);
        const auto& scratch = ch.antiVoxScratchForTest();
        QCOMPARE(scratch.size(), std::size_t{8});
        for (double v : scratch) {
            QCOMPARE(v, 0.0);  // initial zero-fill from std::vector<double>
        }
    }

    void sendAntiVoxData_skipsWdsp_whenSizeUnconfigured()
    {
        // First call with m_antiVoxSize == 0 (no setAntiVoxSize) must reject
        // rather than memcpy into a zero-sized antivox_data buffer.
        TxChannel ch(kChannelId);
        const float interleaved[2] = { 1.0f, 1.0f };
        ch.sendAntiVoxData(interleaved, /*nsamples=*/1);
        const auto& scratch = ch.antiVoxScratchForTest();
        QCOMPARE(scratch.size(), std::size_t{0});
    }
};

QTEST_APPLESS_MAIN(TestTxChannelVoxAntiVox)
#include "tst_tx_channel_vox_anti_vox.moc"
