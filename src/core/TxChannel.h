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
// src/core/TxChannel.h  (NereusSDR)
// =================================================================
//
// Ported from Thetis sources:
//   Project Files/Source/wdsp/TXA.c    — TXA pipeline construction (create_txa),
//                                         licence above (Warren Pratt, NR0V)
//   Project Files/Source/ChannelMaster/cmaster.c — channel lifecycle,
//                                         licence above (Warren Pratt, NR0V)
//
// Ported from Thetis wdsp/TXA.c:31-479 [v2.10.3.13]
// Ported from Thetis wdsp/cmaster.c:177-190 [v2.10.3.13]
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-25 — Stub created by J.J. Boyd (KG4VCF) during 3M-1a Task C.1.
//   2026-04-25 — Full class body (31-stage TXA pipeline wrapper, stageRunning
//                 introspection) added by J.J. Boyd (KG4VCF) during 3M-1a
//                 Task C.2, with AI-assisted transformation via Anthropic
//                 Claude Code.
//   2026-04-26 — kMaxToneMag constant + setTuneTone() declaration added
//                 by J.J. Boyd (KG4VCF) during 3M-1a Task C.3.
//                 AI-assisted transformation via Anthropic Claude Code.
// =================================================================

#pragma once

#include <QObject>

namespace NereusSDR {

// Per-transmitter WDSP channel wrapper.
//
// Wraps the TXA DSP pipeline constructed by WDSP's create_txa() when
// OpenChannel(... type=1 ...) is called in WdspEngine::createTxChannel().
// The 31 pipeline stages are already in WDSP-managed memory when this
// constructor runs; this class provides a typed C++ facade over them.
//
// Thread safety:
//   - Main thread: create/destroy, all stage-run-state queries (stageRunning)
//   - Audio thread (future, C.4+): setRunning / processTx
//
// Ported from Thetis wdsp/TXA.c:31-479 [v2.10.3.13] — create_txa() signal
// flow order determines the Stage enum ordinal values.
class TxChannel : public QObject {
    Q_OBJECT

public:
    // ── Stage identity ──────────────────────────────────────────────────────
    //
    // Ordinal values MUST match the signal-flow order from create_txa() in
    // wdsp/TXA.c:31-479 [v2.10.3.13].  Do not reorder.
    //
    // Default Run values per create_txa() first-argument ("run" parameter):
    //   ON  (1): Panel, MicMeter, EqMeter, LvlrMeter, CfcMeter, Bp0, CompMeter,
    //            Alc, AlcMeter, Sip1, Calcc, OutMeter
    //   OFF (0): RsmpIn, Gen0, PhRot, AmSq, Eqp, PreEmph, Leveler, CfComp,
    //            Compressor, Bp1, OsCtrl, Bp2, AmMod, FmMod, Gen1, UsLew*,
    //            Iqc, Cfir, RsmpOut
    //
    // * UsLew: create_uslew() takes no explicit "run" parameter — the uslew
    //   ramp engages via the channel upslew flag (ch[].iob.ch_upslew), not a
    //   stage-level run bit.  stageRunning(UsLew) always returns false when
    //   WDSP is live; the test documents this as EXPECTED.
    //
    // NOTE: The pre-code review §8.1 table listed "25 stages (indices 0-25)".
    // The authoritative source — wdsp/TXA.c create_txa() — constructs 31
    // distinct stages (verified by counting create_* calls at lines 40-475).
    // Five stages were omitted from the pre-code table: PreEmph, Leveler,
    // LvlrMeter, CfComp, CfcMeter (all appear between EqMeter and Bp0 in
    // the signal chain).  This is a pre-code review correction; use the 31-
    // stage list here as authoritative.
    enum class Stage : int {
        // From Thetis wdsp/TXA.c:40-49   [v2.10.3.13] — rsmpin (input resampler)
        RsmpIn     =  0,
        // From Thetis wdsp/TXA.c:51-57   [v2.10.3.13] — gen0 (PreGen, mode=2 noise)
        Gen0       =  1,
        // From Thetis wdsp/TXA.c:59-69   [v2.10.3.13] — panel (audio panel/level)
        Panel      =  2,
        // From Thetis wdsp/TXA.c:71-78   [v2.10.3.13] — phrot (phase rotator)
        PhRot      =  3,
        // From Thetis wdsp/TXA.c:80-93   [v2.10.3.13] — micmeter
        MicMeter   =  4,
        // From Thetis wdsp/TXA.c:95-109  [v2.10.3.13] — amsq (AM squelch)
        AmSq       =  5,
        // From Thetis wdsp/TXA.c:115-128 [v2.10.3.13] — eqp (parametric EQ)
        Eqp        =  6,
        // From Thetis wdsp/TXA.c:130-143 [v2.10.3.13] — eqmeter
        EqMeter    =  7,
        // From Thetis wdsp/TXA.c:145-156 [v2.10.3.13] — preemph (pre-emphasis)
        PreEmph    =  8,
        // From Thetis wdsp/TXA.c:158-181 [v2.10.3.13] — leveler (wcpagc, mode=5)
        Leveler    =  9,
        // From Thetis wdsp/TXA.c:183-196 [v2.10.3.13] — lvlrmeter
        LvlrMeter  = 10,
        // From Thetis wdsp/TXA.c:198-222 [v2.10.3.13] — cfcomp (multiband compander)
        CfComp     = 11,
        // From Thetis wdsp/TXA.c:224-237 [v2.10.3.13] — cfcmeter
        CfcMeter   = 12,
        // From Thetis wdsp/TXA.c:239-251 [v2.10.3.13] — bp0 (mandatory BPF)
        Bp0        = 13,
        // From Thetis wdsp/TXA.c:253-258 [v2.10.3.13] — compressor
        Compressor = 14,
        // From Thetis wdsp/TXA.c:260-272 [v2.10.3.13] — bp1 (post-comp BPF)
        Bp1        = 15,
        // From Thetis wdsp/TXA.c:274-280 [v2.10.3.13] — osctrl (output soft clip)
        OsCtrl     = 16,
        // From Thetis wdsp/TXA.c:282-294 [v2.10.3.13] — bp2 (post-clip BPF)
        Bp2        = 17,
        // From Thetis wdsp/TXA.c:296-309 [v2.10.3.13] — compmeter
        CompMeter  = 18,
        // From Thetis wdsp/TXA.c:311-334 [v2.10.3.13] — alc (ALC, always-on wcpagc)
        Alc        = 19,
        // From Thetis wdsp/TXA.c:336-342 [v2.10.3.13] — ammod (AM modulation)
        AmMod      = 20,
        // From Thetis wdsp/TXA.c:345-359 [v2.10.3.13] — fmmod (FM modulation)
        FmMod      = 21,
        // From Thetis wdsp/TXA.c:361-367 [v2.10.3.13] — gen1 (PostGen, mode=0 tone)
        Gen1       = 22,
        // From Thetis wdsp/TXA.c:369-377 [v2.10.3.13] — uslew (5 ms upslew ramp)
        UsLew      = 23,
        // From Thetis wdsp/TXA.c:379-392 [v2.10.3.13] — alcmeter
        AlcMeter   = 24,
        // From Thetis wdsp/TXA.c:394-403 [v2.10.3.13] — sip1 (siphon / spectrum)
        Sip1       = 25,
        // From Thetis wdsp/TXA.c:405-422 [v2.10.3.13] — calcc (PureSignal calibration)
        Calcc      = 26,
        // From Thetis wdsp/TXA.c:424-432 [v2.10.3.13] — iqc (IQ correction)
        Iqc        = 27,
        // From Thetis wdsp/TXA.c:434-449 [v2.10.3.13] — cfir (custom CIC FIR)
        Cfir       = 28,
        // From Thetis wdsp/TXA.c:451-460 [v2.10.3.13] — rsmpout (output resampler)
        RsmpOut    = 29,
        // From Thetis wdsp/TXA.c:462-475 [v2.10.3.13] — outmeter
        OutMeter   = 30,

        // Sentinel — equals the number of TXA stages in wdsp/TXA.c [v2.10.3.13].
        // Verified by counting create_* calls in create_txa(): 31 stages total.
        kStageCount = 31
    };

    // ── TUNE-tone magnitude constant ─────────────────────────────────────────
    //
    // From Thetis console.cs:29954 [v2.10.3.13]:
    //   private const double MAX_TONE_MAG = 0.99999f; // why not 1?  clipping?
    //
    // NereusSDR mirrors the Thetis declaration byte-exactly: `0.99999f` widens
    // to `double` on assignment, producing the same bit pattern Thetis stores
    // at runtime (~0.99998999641968).  The C# `f` suffix forces float precision
    // first then widens to double; using a bare double literal `0.99999` would
    // store `0.99999000…`, differing from Thetis by ~1.4e-8.  Using `0.99999f`
    // here reproduces the identical widening and keeps the values byte-exact.
    //
    // NOTE: If 3M-3a adds 2-TONE support and needs this value, move it to a
    // shared WdspTuneConstants.h and include from both TxChannel.h and any
    // new 2-TONE source. Today the constant has one owner; do not duplicate.
    static constexpr double kMaxToneMag = 0.99999f;  // why not 1?  clipping?

    explicit TxChannel(int channelId, QObject* parent = nullptr);
    ~TxChannel() override;

    int channelId() const noexcept { return m_channelId; }

    // ── Stage introspection (3M-1a C.2) ─────────────────────────────────────
    //
    // Returns whether the given TXA stage currently has its run flag set.
    //
    // Implementation reads the WDSP struct field directly:
    //   txa[m_channelId].stagename.p->run
    // via the extern struct _txa txa[] declared in wdsp/TXA.h.
    //
    // If txa[channelId].rsmpin.p is null (channel not yet opened via
    // OpenChannel — typical in unit-test builds that link WDSP but don't
    // call WdspEngine::initialize), falls through to compile-time defaults.
    //
    // Without HAVE_WDSP, always returns compile-time defaults per create_txa().
    //
    // Thread safety: call only from the main thread.
    //
    // From Thetis wdsp/TXA.c:31-479 [v2.10.3.13] — run values are the first
    // argument to each create_*() call in create_txa().
    bool stageRunning(Stage s) const;

    // ── TUNE-tone PostGen (3M-1a C.3) ───────────────────────────────────────
    //
    // Enable / disable the TUNE-tone PostGen (gen1) on this TX channel.
    //
    // Wires WDSP's `SetTXAPostGen*` API per the TUNE pattern from Thetis
    // `chkTUN_CheckedChanged` (console.cs:30031-30040 [v2.10.3.13]).
    //
    // Call order matches Thetis exactly (freq → mode → mag → run):
    //   - Frequency = signed Hz; caller is responsible for sign-flipping per
    //     the current DSP mode (LSB/CWL/DIGL → -cw_pitch; everything else
    //     → +cw_pitch). See pre-code review §3.5. Default 0.0 (caller passes
    //     signed cw_pitch; G.4 TUNE function port computes the sign).
    //   - Mode = 0 (sine tone — per `wdsp/gen.c:144-193 [v2.10.3.13]`)
    //   - Magnitude = kMaxToneMag (= 0.99999) by default; the `magnitude`
    //     parameter exists so future callers (e.g., 2-TONE test in 3M-3a)
    //     can override.
    //   - Run = 1 (on) or 0 (off)
    //
    // 3M-1a callers (Task G.4 TUNE function port + TxApplet) drive this from
    // `MoxController::txAboutToBegin` for TUN-on and `txAboutToEnd` for TUN-off.
    //
    // **Scope (3M-1a C.3):** this method only touches the gen1 PostGen API.
    // It does NOT engage MOX (that is MoxController), does NOT configure the
    // TXA channel's DSP mode (that is C.4 / G.4), and does NOT compute the
    // cw_pitch sign (that is G.4's responsibility).
    //
    // From Thetis console.cs:30031-30040 [v2.10.3.13] — chkTUN_CheckedChanged.
    // From Thetis wdsp/gen.c:783-813 [v2.10.3.13] — SetTXAPostGen* API.
    void setTuneTone(bool on,
                     double freqHz   = 0.0,
                     double magnitude = kMaxToneMag);

    // (C.4 adds setRunning — do not add here.)

private:
    const int m_channelId;
};

} // namespace NereusSDR
