// tests/tst_nb_family.cpp — Phase 3G RX Epic Sub-epic B
//
// Pure C++ unit tests for NbFamily's cycling helper + NbTuning defaults.
// Runs without WDSP — does NOT construct an NbFamily instance (which would
// call create_anbEXT/create_nobEXT on a non-existent WDSP channel).
//
// Companion: tst_rxchannel_nb2_polish covers the RxChannel-facing API.
//            tst_slice_nb_persistence covers per-slice-per-band save/restore.
//
// no-port-check: Not a port of Thetis code. This file cites
// cmaster.c:43-68 line numbers only to anchor default-value assertions
// against the authoritative upstream constants — the values themselves
// are already ported (with full attribution) in src/core/NbFamily.h.
// This file tests that the port's defaults didn't drift.

#include <QtTest/QtTest>

#include "core/NbFamily.h"

using NereusSDR::NbMode;
using NereusSDR::NbTuning;
using NereusSDR::cycleNbMode;

class TestNbFamily : public QObject {
    Q_OBJECT
private slots:

    // ── cycleNbMode rotation ─────────────────────────────────────────────────

    void cycle_off_to_nb()     { QCOMPARE(cycleNbMode(NbMode::Off), NbMode::NB); }
    void cycle_nb_to_nb2()     { QCOMPARE(cycleNbMode(NbMode::NB),  NbMode::NB2); }
    void cycle_nb2_to_off()    { QCOMPARE(cycleNbMode(NbMode::NB2), NbMode::Off); }

    void cycle_three_returns_to_origin() {
        // Three clicks = full rotation; back to Off.
        QCOMPARE(cycleNbMode(cycleNbMode(cycleNbMode(NbMode::Off))), NbMode::Off);
    }

    // ── NbTuning defaults — Thetis cmaster.c:43-68 [v2.10.3.13] ──────────────

    void tuning_nb1_defaults_match_thetis_runtime() {
        // NB1 defaults match Thetis's EFFECTIVE runtime values after
        // the Setup page's udDSPNB* ValueChanged handlers fire, per
        // setup.designer.cs:44459-44604 + setup.cs:8572,16222-16236
        // [v2.10.3.13]. See NbFamily.h for the derivation.
        NbTuning t;
        QCOMPARE(t.nbTauMs,      0.01);   // udDSPNBTransition default 0.01 ms
        QCOMPARE(t.nbHangMs,     0.01);   // udDSPNBLag default 0.01 ms
        QCOMPARE(t.nbAdvMs,      0.01);   // udDSPNBLead default 0.01 ms
        QCOMPARE(t.nbBacktau,    0.05);   // cmaster.c:52 backtau=0.05 s (no Thetis UI override)
        QCOMPARE(t.nbThreshold,  4.95);   // udDSPNB=30 × 0.165 = 4.95
    }

    void tuning_nb2_defaults_match_thetis_cmaster_c() {
        // NB2 defaults — from Thetis ChannelMaster/cmaster.c:61-68 [v2.10.3.13]
        NbTuning t;
        QCOMPARE(t.nb2Mode,      0);       // cmaster.c:61 mode=0 (zero-fill)
        QCOMPARE(t.nb2SlewMs,    0.1);     // cmaster.c:62/64 slew=0.0001 s
        QCOMPARE(t.nb2AdvMs,     0.1);     // cmaster.c:63 advtime=0.0001 s
        QCOMPARE(t.nb2HangMs,    0.1);     // cmaster.c:65 hangtime=0.0001 s
        QCOMPARE(t.nb2MaxImpMs,  25.0);    // cmaster.c:66 max_imp_seq_time=0.025 s
        QCOMPARE(t.nb2Backtau,   0.05);    // cmaster.c:67 backtau=0.05 s
        QCOMPARE(t.nb2Threshold, 30.0);    // cmaster.c:68 threshold=30.0
    }
};

QTEST_APPLESS_MAIN(TestNbFamily)
#include "tst_nb_family.moc"
