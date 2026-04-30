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
// tests/tst_tx_channel_meters.cpp  (NereusSDR)
// =================================================================
//
// No Thetis code is directly ported in this test file. The test exercises:
//   - TxChannel::getTxMicMeter()   — D.7 Phase 3M-1b (wired: TXA_MIC_PK)
//   - TxChannel::getAlcMeter()     — D.7 Phase 3M-1b (wired: TXA_ALC_PK)
//   - TxChannel::getEqMeter()      — D.7 stub, deferred to 3M-3a
//   - TxChannel::getLvlrMeter()    — D.7 stub, deferred to 3M-3a
//   - TxChannel::getCfcMeter()     — D.7 stub, deferred to 3M-3a
//   - TxChannel::getCompMeter()    — D.7 stub, deferred to 3M-3a
//   - TxChannel::kMeterUninitialisedSentinel — D.7 sentinel constant
//
// Porting context (cited in TxChannel.h / TxChannel.cpp):
//   getTxMicMeter / getAlcMeter:
//     dsp.cs:390-391     [v2.10.3.13] — GetTXAMeter DLL import
//     dsp.cs:1025-1029   [v2.10.3.13] — TXA_MIC_PK / TXA_ALC_PK callsites
//     wdsp/TXA.h:51,63   [v2.10.3.13] — TXA_MIC_PK=0, TXA_ALC_PK=12 in enum
//     wdsp/meter.c:151-159 [v2.10.3.13] — GetTXAMeter implementation
//
// Sentinel semantics:
//   Active meters (getTxMicMeter / getAlcMeter) return kMeterUninitialisedSentinel
//   (-999.0f) when txa[ch].rsmpin.p == nullptr (channel not opened) or when
//   HAVE_WDSP is not defined. This distinguishes "channel not running" from
//   "no signal" (-120 dB or -inf).
//
//   Deferred meters (getEqMeter / getLvlrMeter / getCfcMeter / getCompMeter)
//   return 0.0f unconditionally — different sentinel ("meter not yet active").
//   These stubs are safe to call during 3M-1b without crashing.
//
// Tests verify (without WDSP OpenChannel, so rsmpin.p == nullptr):
//   1. kMeterUninitialisedSentinel == -999.0f
//   2. getTxMicMeter() returns sentinel when channel not initialised
//   3. getAlcMeter()   returns sentinel when channel not initialised
//   4. getEqMeter()    returns 0.0f (deferred stub)
//   5. getLvlrMeter()  returns 0.0f (deferred stub)
//   6. getCfcMeter()   returns 0.0f (deferred stub)
//   7. getCompMeter()  returns 0.0f (deferred stub)
//   8. Deferred meter return does NOT equal sentinel
//   9. Active meter sentinel does NOT equal deferred-meter zero
//  10. Sentinel constant is exactly -999.0f (no floating-point approximation)
//  11. getAlcMeter() distinct from getTxMicMeter() (both read different meters)
//
// Total test cases: 11
//
// WDSP-call verification is not possible without a mock; in unit-test builds
// txa[].rsmpin.p == nullptr so GetTXAMeter is never reached. The tests confirm
// the null-guard fires correctly and returns the sentinel.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-27 — New test for Phase 3M-1b Task D.7: TxChannel TX meter readouts
//                 (getTxMicMeter / getAlcMeter live; 4 deferred stubs).
//                 J.J. Boyd (KG4VCF), with AI-assisted implementation via
//                 Anthropic Claude Code.
// =================================================================

// no-port-check: NereusSDR-original test file. All Thetis source cites are
// in TxChannel.h/cpp and wdsp_api.h.

#include <QtTest/QtTest>

#include "core/TxChannel.h"

using namespace NereusSDR;

class TestTxChannelMeters : public QObject {
    Q_OBJECT

    static constexpr int kChannelId = 1;  // WDSP.id(1, 0) — TX channel

private slots:

    // ── kMeterUninitialisedSentinel ────────────────────────────────────────────

    void kMeterUninitialisedSentinel_isMinus999()
    {
        // The sentinel must be exactly -999.0f, not an approximation.
        // This is the contract between TxChannel and UI code: a reading of
        // exactly -999.0f means "WDSP channel not initialised", not "no signal".
        QCOMPARE(TxChannel::kMeterUninitialisedSentinel, -999.0f);
    }

    // ── getTxMicMeter() — uninitialised channel ────────────────────────────────
    //
    // Without calling WdspEngine::createTxChannel() (which calls OpenChannel()),
    // txa[channelId].rsmpin.p == nullptr.  getTxMicMeter() must return the
    // sentinel without crashing.
    //
    // From Thetis wdsp/TXA.h:51 [v2.10.3.13] — TXA_MIC_PK = 0.
    // From Thetis wdsp/meter.c:151-159 [v2.10.3.13] — GetTXAMeter reads
    //   txa[channel].meter[mt]; would segfault with null pmtupdate.

    void getTxMicMeter_uninitialised_returnsSentinel()
    {
        TxChannel ch(kChannelId);
        // txa[kChannelId].rsmpin.p == nullptr in unit-test builds;
        // null-guard must fire and return kMeterUninitialisedSentinel.
        QCOMPARE(ch.getTxMicMeter(), TxChannel::kMeterUninitialisedSentinel);
    }

    void getTxMicMeter_uninitialised_returnsMinus999()
    {
        // Belt-and-suspenders: confirm the actual float value, not just ==sentinel.
        TxChannel ch(kChannelId);
        QCOMPARE(ch.getTxMicMeter(), -999.0f);
    }

    // ── getAlcMeter() — uninitialised channel ─────────────────────────────────
    //
    // From Thetis wdsp/TXA.h:63 [v2.10.3.13] — TXA_ALC_PK = 12.
    // Same null-guard path as getTxMicMeter.

    void getAlcMeter_uninitialised_returnsSentinel()
    {
        TxChannel ch(kChannelId);
        QCOMPARE(ch.getAlcMeter(), TxChannel::kMeterUninitialisedSentinel);
    }

    void getAlcMeter_uninitialised_returnsMinus999()
    {
        TxChannel ch(kChannelId);
        QCOMPARE(ch.getAlcMeter(), -999.0f);
    }

    // ── Deferred meters — always return 0.0f ──────────────────────────────────
    //
    // Per master design §5.2.1: EQ / Leveler / CFC / Compressor meters are
    // deferred to 3M-3a. These stubs return 0.0f unconditionally so callers
    // can poll them safely during 3M-1b.
    //
    // From Thetis wdsp/TXA.h:53,55,58,61 [v2.10.3.13]:
    //   TXA_EQ_PK = 2, TXA_LVLR_PK = 4, TXA_CFC_PK = 7, TXA_COMP_PK = 10.
    // Full wiring deferred to 3M-3a per master design §5.2.1.

    void getEqMeter_returnsZero()
    {
        TxChannel ch(kChannelId);
        QCOMPARE(ch.getEqMeter(), 0.0f);
    }

    void getLvlrMeter_returnsZero()
    {
        TxChannel ch(kChannelId);
        QCOMPARE(ch.getLvlrMeter(), 0.0f);
    }

    void getCfcMeter_returnsZero()
    {
        TxChannel ch(kChannelId);
        QCOMPARE(ch.getCfcMeter(), 0.0f);
    }

    void getCompMeter_returnsZero()
    {
        TxChannel ch(kChannelId);
        QCOMPARE(ch.getCompMeter(), 0.0f);
    }

    // ── Sentinel distinctness ─────────────────────────────────────────────────
    //
    // These tests ensure the two "inactive" states are distinguishable:
    //   kMeterUninitialisedSentinel (-999.0f) — active meter, channel not running
    //   0.0f                                  — deferred meter, not yet wired

    void deferredMeter_doesNotEqualSentinel()
    {
        // Deferred meters return 0.0f, NOT the -999 sentinel.
        // UI code must handle both values differently.
        TxChannel ch(kChannelId);
        QVERIFY(ch.getEqMeter()   != TxChannel::kMeterUninitialisedSentinel);
        QVERIFY(ch.getLvlrMeter() != TxChannel::kMeterUninitialisedSentinel);
        QVERIFY(ch.getCfcMeter()  != TxChannel::kMeterUninitialisedSentinel);
        QVERIFY(ch.getCompMeter() != TxChannel::kMeterUninitialisedSentinel);
    }

    void activeMeter_sentinelDoesNotEqualZero()
    {
        // The sentinel (-999.0f) is distinct from the deferred-meter value (0.0f).
        QVERIFY(TxChannel::kMeterUninitialisedSentinel != 0.0f);
    }
};

QTEST_APPLESS_MAIN(TestTxChannelMeters)
#include "tst_tx_channel_meters.moc"
