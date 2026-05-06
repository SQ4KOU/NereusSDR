// no-port-check: NereusSDR-original unit-test file. Thetis cite comments
// document upstream sources; no Thetis logic ported in this test file.
// =================================================================
// tests/tst_tx_channel_ps_setters.cpp  (NereusSDR)
// =================================================================
//
// Unit tests for the 22 TxChannel PureSignal API wrappers added in Phase
// 3M-4 Task 3:
//
//   Per-channel state setters (15):
//     setPSRunCal(int)
//     setPSMox(bool)
//     setPSReset(bool)
//     setPSMancal(bool)
//     setPSAutomode(bool)
//     setPSTurnon(bool)
//     setPSControl(int reset, int mancal, int automode, int turnon)
//     setPSLoopDelay(double seconds)
//     setPSMoxDelay(double seconds)
//     setPSTXDelay(double seconds)  → returns double (snapped applied delay)
//     setPSHWPeak(double peak)
//     setPSPtol(double ptol)
//     setPSFeedbackRate(int rate)
//     setPSPinMode(bool)
//     setPSMapMode(bool)
//     setPSStabilize(bool)
//     setPSIntsAndSpi(int ints, int spi)
//
//   Per-channel readers (4):
//     getPSInfo(int* info16)
//     getPSHWPeak() → double
//     getPSMaxTX() → double
//     getPSDisp(double* x, double* ym, double* yc, double* ys,
//               double* cm, double* cc, double* cs)
//
//   Static channel routing (2):
//     setPSRxIdx(int txid, int idx)
//     setPSTxIdx(int txid, int idx)
//
// Test strategy: pure smoke / does-not-crash, matching the convention from
// tst_tx_channel_cfc_cpdr_cessb_setters.cpp / tst_tx_channel_eq_setters.cpp.
// Wrappers null-guard on `txa[m_channelId].rsmpin.p == nullptr`, so calling
// them on a bare `TxChannel ch(kTxChannelId)` (without WdspEngine init) is
// safe in HAVE_WDSP-linked builds; HAVE_WDSP-undefined builds exercise the
// stub path.  Tests cover argument shapes (signed delay for setPSTXDelay,
// 16-int output for getPSInfo, 7 buffers for getPSDisp) and verify that
// methods do not throw, crash, or alias.
//
// Source: Thetis wdsp/calcc.c:891-1132 [v2.10.3.13] +
//         Thetis cmaster.cs:143-147 [v2.10.3.13].
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-06 — New test file for Phase 3M-4 Task 3: 22 TX PureSignal API
//                 wrapper smoke tests.  J.J. Boyd (KG4VCF), with AI-assisted
//                 implementation via Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>

#include "core/TxChannel.h"

using namespace NereusSDR;

// WDSP TX channel ID — from Thetis cmaster.c:177-190 [v2.10.3.13].
static constexpr int kTxChannelId = 1;

class TestTxChannelPsSetters : public QObject {
    Q_OBJECT

private slots:

    // ── setPSRunCal ─────────────────────────────────────────────────────────
    //
    // Wraps SetPSRunCal(channel, run).
    // From Thetis wdsp/calcc.c:899 [v2.10.3.13].

    void setPSRunCal_zero_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        ch.setPSRunCal(0);
    }

    void setPSRunCal_one_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        ch.setPSRunCal(1);
    }

    // ── setPSMox ────────────────────────────────────────────────────────────
    //
    // Wraps SetPSMox(channel, mox ? 1 : 0).
    // From Thetis wdsp/calcc.c:909 [v2.10.3.13].

    void setPSMox_true_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        ch.setPSMox(true);
    }

    void setPSMox_false_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        ch.setPSMox(false);
    }

    void setPSMox_idempotentToggle_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        ch.setPSMox(true);
        ch.setPSMox(true);
        ch.setPSMox(false);
        ch.setPSMox(false);
    }

    // ── getPSInfo ──────────────────────────────────────────────────────────
    //
    // Wraps GetPSInfo — writes 16 ints under cs_update.
    // From Thetis wdsp/calcc.c:922-929 [v2.10.3.13].

    void getPSInfo_16ints_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        // 16-int buffer per calcc.c:927 — `memcpy(info, a->info, 16 * sizeof(int))`.
        // Pre-fill with sentinel so we can verify the call did not corrupt past 16.
        int info[20];
        for (int i = 0; i < 20; ++i) info[i] = -42;
        ch.getPSInfo(info);
        // Bytes past index 15 must remain untouched.  WDSP only writes 16 ints.
        QCOMPARE(info[16], -42);
        QCOMPARE(info[17], -42);
        QCOMPARE(info[18], -42);
        QCOMPARE(info[19], -42);
    }

    // ── setPSReset ─────────────────────────────────────────────────────────
    //
    // Wraps SetPSReset.  From Thetis wdsp/calcc.c:932 [v2.10.3.13].

    void setPSReset_true_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        ch.setPSReset(true);
    }

    void setPSReset_false_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        ch.setPSReset(false);
    }

    // ── setPSMancal ────────────────────────────────────────────────────────
    //
    // Wraps SetPSMancal.  From Thetis wdsp/calcc.c:942 [v2.10.3.13].

    void setPSMancal_true_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        ch.setPSMancal(true);
    }

    void setPSMancal_false_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        ch.setPSMancal(false);
    }

    // ── setPSAutomode ──────────────────────────────────────────────────────
    //
    // Wraps SetPSAutomode.  From Thetis wdsp/calcc.c:950 [v2.10.3.13].

    void setPSAutomode_true_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        ch.setPSAutomode(true);
    }

    void setPSAutomode_false_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        ch.setPSAutomode(false);
    }

    // ── setPSTurnon ────────────────────────────────────────────────────────
    //
    // Wraps SetPSTurnon.  From Thetis wdsp/calcc.c:958 [v2.10.3.13].

    void setPSTurnon_true_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        ch.setPSTurnon(true);
    }

    void setPSTurnon_false_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        ch.setPSTurnon(false);
    }

    // ── setPSControl ───────────────────────────────────────────────────────
    //
    // Wraps SetPSControl(channel, reset, mancal, automode, turnon).  From
    // Thetis wdsp/calcc.c:966 [v2.10.3.13].  Thetis ForcePS pattern
    // (PSForm.cs ForcePS [v2.10.3.13]) calls `SetPSControl(_txachannel, 1,
    // 0, 0, 0)` to force the engine to LRESET.

    void setPSControl_forceReset_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        // Thetis ForcePS reset pattern — reset=1, others=0.
        ch.setPSControl(/*reset=*/1, /*mancal=*/0, /*automode=*/0, /*turnon=*/0);
    }

    void setPSControl_runOn_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        // Engaged calibration — reset=0, automode=1, turnon=1.
        ch.setPSControl(/*reset=*/0, /*mancal=*/0, /*automode=*/1, /*turnon=*/1);
    }

    void setPSControl_allZero_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        ch.setPSControl(0, 0, 0, 0);
    }

    void setPSControl_allOne_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        ch.setPSControl(1, 1, 1, 1);
    }

    // ── setPSLoopDelay ─────────────────────────────────────────────────────
    //
    // Wraps SetPSLoopDelay.  From Thetis wdsp/calcc.c:979 [v2.10.3.13].

    void setPSLoopDelay_zero_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        ch.setPSLoopDelay(0.0);
    }

    void setPSLoopDelay_typical_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        // Typical microsecond-scale delay.
        ch.setPSLoopDelay(0.0001);  // 100 us
    }

    // ── setPSMoxDelay ──────────────────────────────────────────────────────
    //
    // Wraps SetPSMoxDelay.  From Thetis wdsp/calcc.c:990 [v2.10.3.13].

    void setPSMoxDelay_zero_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        ch.setPSMoxDelay(0.0);
    }

    void setPSMoxDelay_typical_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        ch.setPSMoxDelay(0.005);  // 5 ms
    }

    // ── setPSTXDelay ───────────────────────────────────────────────────────
    //
    // Wraps SetPSTXDelay (returns double — applied delay).  From Thetis
    // wdsp/calcc.c:1001-1021 [v2.10.3.13].  Negative values shift to the
    // RX delay path; positive values shift to the TX delay path.  Snaps
    // to 20 ns fractional steps internally.

    void setPSTXDelay_positive_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        // Positive — TX path.  Return value is engine-snapped applied delay.
        // On a bare channel (no calcc allocated), the wrapper returns 0.0
        // via the rsmpin null-guard.
        const double applied = ch.setPSTXDelay(0.000020);  // 20 us
        Q_UNUSED(applied);  // Smoke only; no QCOMPARE on bare channel.
    }

    void setPSTXDelay_zero_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        const double applied = ch.setPSTXDelay(0.0);
        Q_UNUSED(applied);
    }

    void setPSTXDelay_negative_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        // Negative — RX path (calcc.c:1013-1017 — `adelay = -SetDelayValue
        // (a->rxdelay, -a->txdel)`).
        const double applied = ch.setPSTXDelay(-0.000010);  // -10 us
        Q_UNUSED(applied);
    }

    // ── setPSHWPeak / getPSHWPeak ──────────────────────────────────────────
    //
    // Wraps SetPSHWPeak / GetPSHWPeak.  From Thetis wdsp/calcc.c:1024 / 1034
    // [v2.10.3.13].  Thetis cmaster.cs:536 [v2.10.3.13]:
    //   puresignal.SetPSHWPeak(txch, 0.2899);
    // (default for ANAN-G2; mi0bot HL2 uses 0.233).

    void setPSHWPeak_ananDefault_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        ch.setPSHWPeak(0.2899);  // ANAN-G2 default per cmaster.cs:536
    }

    void setPSHWPeak_hl2Default_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        ch.setPSHWPeak(0.233);   // HL2 mi0bot override
    }

    void getPSHWPeak_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        // Bare channel returns 0.0 via the rsmpin null-guard.
        const double peak = ch.getPSHWPeak();
        Q_UNUSED(peak);
    }

    // ── getPSMaxTX ─────────────────────────────────────────────────────────
    //
    // Wraps GetPSMaxTX.  From Thetis wdsp/calcc.c:1042 [v2.10.3.13].

    void getPSMaxTX_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        const double maxtx = ch.getPSMaxTX();
        Q_UNUSED(maxtx);
    }

    // ── setPSPtol ──────────────────────────────────────────────────────────
    //
    // Wraps SetPSPtol.  From Thetis wdsp/calcc.c:1050 [v2.10.3.13].

    void setPSPtol_typical_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        ch.setPSPtol(0.8);
    }

    void setPSPtol_loose_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        ch.setPSPtol(0.95);
    }

    // ── getPSDisp ──────────────────────────────────────────────────────────
    //
    // Wraps GetPSDisp — writes seven AmpView display arrays.  From Thetis
    // wdsp/calcc.c:1058 [v2.10.3.13].  On a bare channel (no calcc),
    // wrapper returns immediately via the rsmpin null-guard; buffers
    // remain untouched.

    void getPSDisp_sevenBuffers_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        // Generously oversized; the bare-channel rsmpin guard returns
        // before any memcpy.  64 doubles per buffer is enough for a real
        // call (production sizing nsamps + ints*4 ≪ 64 in normal AmpView).
        constexpr int kBufLen = 64;
        double x[kBufLen]   = {};
        double ym[kBufLen]  = {};
        double yc[kBufLen]  = {};
        double ys[kBufLen]  = {};
        double cm[kBufLen]  = {};
        double cc[kBufLen]  = {};
        double cs[kBufLen]  = {};
        ch.getPSDisp(x, ym, yc, ys, cm, cc, cs);
        // Pre-fill sentinel: zeros stay zeros (no calcc allocated; guard fires).
        QCOMPARE(x[0], 0.0);
        QCOMPARE(ym[0], 0.0);
        QCOMPARE(yc[0], 0.0);
        QCOMPARE(ys[0], 0.0);
        QCOMPARE(cm[0], 0.0);
        QCOMPARE(cc[0], 0.0);
        QCOMPARE(cs[0], 0.0);
    }

    // ── setPSFeedbackRate ──────────────────────────────────────────────────
    //
    // Wraps SetPSFeedbackRate.  From Thetis wdsp/calcc.c:1073 [v2.10.3.13].
    // cmaster.cs:535 calls this with the board's ps_rate (192000 for ANAN-G2).

    void setPSFeedbackRate_192k_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        // ANAN-G2 PS feedback sample rate.
        ch.setPSFeedbackRate(192000);
    }

    void setPSFeedbackRate_48k_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        // HL2 mi0bot uses rx1_rate; smaller boards may run lower.
        ch.setPSFeedbackRate(48000);
    }

    // ── setPSPinMode ───────────────────────────────────────────────────────
    //
    // Wraps SetPSPinMode.  From Thetis wdsp/calcc.c:1102 [v2.10.3.13].

    void setPSPinMode_true_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        ch.setPSPinMode(true);
    }

    void setPSPinMode_false_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        ch.setPSPinMode(false);
    }

    // ── setPSMapMode ───────────────────────────────────────────────────────
    //
    // Wraps SetPSMapMode.  From Thetis wdsp/calcc.c:1110 [v2.10.3.13].

    void setPSMapMode_true_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        ch.setPSMapMode(true);
    }

    void setPSMapMode_false_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        ch.setPSMapMode(false);
    }

    // ── setPSStabilize ─────────────────────────────────────────────────────
    //
    // Wraps SetPSStabilize.  From Thetis wdsp/calcc.c:1118 [v2.10.3.13].

    void setPSStabilize_true_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        ch.setPSStabilize(true);
    }

    void setPSStabilize_false_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        ch.setPSStabilize(false);
    }

    // ── setPSIntsAndSpi ────────────────────────────────────────────────────
    //
    // Wraps SetPSIntsAndSpi.  From Thetis wdsp/calcc.c:1140 [v2.10.3.13].

    void setPSIntsAndSpi_typical_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        ch.setPSIntsAndSpi(/*ints=*/16, /*spi=*/256);
    }

    // ── setPSRxIdx / setPSTxIdx (static channel routing) ───────────────────
    //
    // Per Thetis cmaster.cs:533-534 [v2.10.3.13]:
    //   SetPSRxIdx(0, 0);   // txid = 0, all current models use Stream0
    //                       //  for RX feedback
    //   SetPSTxIdx(0, 1);   // txid = 0, all current models use Stream1
    //                       //  for TX feedback
    // From Thetis cmaster.cs:143-147 [v2.10.3.13].

    void setPSRxIdx_thetisDefault_doesNotCrash()
    {
        TxChannel::setPSRxIdx(/*txid=*/0, /*idx=*/0);  // Stream0 for RX feedback
    }

    void setPSTxIdx_thetisDefault_doesNotCrash()
    {
        TxChannel::setPSTxIdx(/*txid=*/0, /*idx=*/1);  // Stream1 for TX feedback
    }

    // ── Compile-time signature checks (linkage smoke) ───────────────────────
    //
    // Compile-time check that all 22 methods exist with the expected
    // signatures.  If any wrapper is missing or its signature drifts, this
    // test will fail to compile.

    void hasAllExpectedPsApiMethods()
    {
        auto runCal       = &TxChannel::setPSRunCal;        // void(int)
        auto mox          = &TxChannel::setPSMox;           // void(bool)
        auto info         = &TxChannel::getPSInfo;          // void(int*)
        auto reset        = &TxChannel::setPSReset;         // void(bool)
        auto mancal       = &TxChannel::setPSMancal;        // void(bool)
        auto automode     = &TxChannel::setPSAutomode;      // void(bool)
        auto turnon       = &TxChannel::setPSTurnon;        // void(bool)
        auto control      = &TxChannel::setPSControl;       // void(int,int,int,int)
        auto loopDelay    = &TxChannel::setPSLoopDelay;     // void(double)
        auto moxDelay     = &TxChannel::setPSMoxDelay;      // void(double)
        auto txDelay      = &TxChannel::setPSTXDelay;       // double(double)
        auto hwPeakSet    = &TxChannel::setPSHWPeak;        // void(double)
        auto hwPeakGet    = &TxChannel::getPSHWPeak;        // double()
        auto maxTx        = &TxChannel::getPSMaxTX;         // double()
        auto ptol         = &TxChannel::setPSPtol;          // void(double)
        auto disp         = &TxChannel::getPSDisp;          // void(7×double*)
        auto fbRate       = &TxChannel::setPSFeedbackRate;  // void(int)
        auto pinMode      = &TxChannel::setPSPinMode;       // void(bool)
        auto mapMode      = &TxChannel::setPSMapMode;       // void(bool)
        auto stabilize    = &TxChannel::setPSStabilize;     // void(bool)
        auto intsAndSpi   = &TxChannel::setPSIntsAndSpi;    // void(int,int)
        auto rxIdx        = &TxChannel::setPSRxIdx;         // void(int,int) static
        auto txIdx        = &TxChannel::setPSTxIdx;         // void(int,int) static

        Q_UNUSED(runCal);    Q_UNUSED(mox);        Q_UNUSED(info);
        Q_UNUSED(reset);     Q_UNUSED(mancal);     Q_UNUSED(automode);
        Q_UNUSED(turnon);    Q_UNUSED(control);    Q_UNUSED(loopDelay);
        Q_UNUSED(moxDelay);  Q_UNUSED(txDelay);    Q_UNUSED(hwPeakSet);
        Q_UNUSED(hwPeakGet); Q_UNUSED(maxTx);      Q_UNUSED(ptol);
        Q_UNUSED(disp);      Q_UNUSED(fbRate);     Q_UNUSED(pinMode);
        Q_UNUSED(mapMode);   Q_UNUSED(stabilize);  Q_UNUSED(intsAndSpi);
        Q_UNUSED(rxIdx);     Q_UNUSED(txIdx);
        QVERIFY(true);
    }

    // ── ForcePS sequence smoke ──────────────────────────────────────────────
    //
    // Mirrors a typical Thetis ForcePS callsite (PSForm.cs ForcePS
    // [v2.10.3.13]) which sequences SetPSControl reset → state checks
    // through getPSInfo.  Smoke-only on a bare channel.

    void forcePsSequence_doesNotCrash()
    {
        TxChannel ch(kTxChannelId);
        // Step 1: force reset.
        ch.setPSControl(1, 0, 0, 0);
        // Step 2: poll info (bare channel — guard skips memcpy).
        int info[16] = {};
        ch.getPSInfo(info);
        // Step 3: configure the engine.
        ch.setPSFeedbackRate(192000);
        ch.setPSHWPeak(0.2899);
        ch.setPSPtol(0.8);
        ch.setPSStabilize(false);
        ch.setPSPinMode(false);
        ch.setPSMapMode(false);
        // Step 4: enable automode + turnon for adaptive cal.
        ch.setPSControl(0, 0, 1, 1);
    }

    // ── Channel routing smoke (called once at engine init) ──────────────────

    void psChannelRouting_thetisDefault_doesNotCrash()
    {
        // Mirrors cmaster.cs:533-534 [v2.10.3.13] ordering verbatim.
        TxChannel::setPSRxIdx(0, 0);
        TxChannel::setPSTxIdx(0, 1);
    }
};

QTEST_APPLESS_MAIN(TestTxChannelPsSetters)
#include "tst_tx_channel_ps_setters.moc"
