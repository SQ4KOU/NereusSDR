// =================================================================
// tests/tst_mox_controller_anti_vox.cpp  (NereusSDR)
// =================================================================
//
// NereusSDR-original test. No Thetis logic is ported in this test
// file. The test exercises:
//   - MoxController::setVoxHangTime(int ms)              — H.3 Phase 3M-1b
//   - MoxController::setAntiVoxGain(int dB)              — H.3 Phase 3M-1b
//   - MoxController::voxHangTimeRequested(double seconds) — H.3 signal
//   - MoxController::antiVoxGainRequested(double gain)    — H.3 signal
//
// 3M-3a-iv post-bench refactor (Option A): setAntiVoxSourceVax tests
// (formerly §C) and the antiVoxSourceWhatRequested signal tests have
// been removed alongside the slot+signal in MoxController.  Thetis
// chkAntiVoxSource (RX vs VAC at cmaster.cs:912-943 [v2.10.3.13]) does
// not map to NereusSDR's architecture; see the architectural-divergence
// section in docs/architecture/phase3m-3a-iv-antivox-feed-design.md §18.
//
// Source references (for traceability):
//   Thetis Project Files/Source/Console/setup.cs:18896-18900 [v2.10.3.13]
//     — udDEXPHold_ValueChanged: ms→seconds before SetDEXPHoldTime.
//   Thetis Project Files/Source/Console/setup.cs:18986-18990 [v2.10.3.13]
//     — udAntiVoxGain_ValueChanged: dB→linear (/20.0) before SetAntiVOXGain.
//
// Default member values (from Thetis source + TransmitModel):
//   m_voxHangTimeMs      = 500   (udDEXPHold designer default)
//   m_antiVoxGainDb      = 0     (TransmitModel default)
//   m_micBoost           = true  (console.cs:13237 [v2.10.3.13]: mic_boost = true)
//
// Formulae verified against Thetis:
//   Hang time seconds = ms / 1000.0
//   Anti-VOX gain linear = pow(10.0, dB / 20.0)   ← /20.0 voltage scaling
// =================================================================

// no-port-check: NereusSDR-original test file — no upstream Thetis port.

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QLoggingCategory>

#include <cmath>

#include "core/MoxController.h"

using namespace NereusSDR;

class TestMoxControllerAntiVox : public QObject {
    Q_OBJECT

private slots:

    // ════════════════════════════════════════════════════════════════════════
    // § A — setVoxHangTime tests
    // ════════════════════════════════════════════════════════════════════════

    // §A.1 — NaN sentinel: first call always emits (default value, spy first)
    //
    // A fresh MoxController has m_lastVoxHangTimeEmitted = NaN.  Calling
    // setVoxHangTime(500) (= default) must still emit because the NaN sentinel
    // forces the very first call through, priming WDSP at startup.
    void hangTime_nanSentinel_firstCallAlwaysEmits()
    {
        MoxController ctrl;
        // Attach spy before any setter call — fresh NaN state.
        QSignalSpy spy(&ctrl, &MoxController::voxHangTimeRequested);

        ctrl.setVoxHangTime(500);  // default value, NaN sentinel forces emit

        QCOMPARE(spy.count(), 1);
        // 500 ms → 0.5 seconds
        QVERIFY(qFuzzyCompare(spy.at(0).at(0).toDouble(), 0.5));
    }

    // §A.2 — Change emits the converted seconds value
    //
    // setVoxHangTime(1000) must emit exactly 1.0 seconds.
    // Prime NaN sentinel first so the spy only catches the new-value emit.
    void hangTime_change_emitsNewValue()
    {
        MoxController ctrl;
        ctrl.setVoxHangTime(500);  // prime NaN sentinel

        QSignalSpy spy(&ctrl, &MoxController::voxHangTimeRequested);
        ctrl.setVoxHangTime(1000);  // 1000 ms → 1.0 seconds

        QCOMPARE(spy.count(), 1);
        QVERIFY(qFuzzyCompare(spy.at(0).at(0).toDouble(), 1.0));
    }

    // §A.3 — Idempotent: repeat same value does not re-emit
    //
    // After emitting 0.5 s, a second call with 500 ms must be suppressed.
    void hangTime_idempotent_noDoubleEmit()
    {
        MoxController ctrl;
        ctrl.setVoxHangTime(500);  // prime NaN sentinel; emits 0.5

        QSignalSpy spy(&ctrl, &MoxController::voxHangTimeRequested);
        ctrl.setVoxHangTime(500);  // same → no emit

        QCOMPARE(spy.count(), 0);
    }

    // §A.4 — Boundary: 1 ms → 0.001 seconds
    //
    // From Thetis setup.cs:18899 [v2.10.3.13]: Value / 1000.0
    // 1 ms / 1000 = 0.001 — small but non-zero, non-NaN.
    void hangTime_boundary_1ms()
    {
        MoxController ctrl;
        QSignalSpy spy(&ctrl, &MoxController::voxHangTimeRequested);

        ctrl.setVoxHangTime(1);  // 1 ms → 0.001 s, NaN sentinel forces emit

        QCOMPARE(spy.count(), 1);
        const double val = spy.at(0).at(0).toDouble();
        QVERIFY(!std::isnan(val));
        QVERIFY(!std::isinf(val));
        QVERIFY(val > 0.0);
        QVERIFY(qFuzzyCompare(val, 0.001));
    }

    // §A.5 — Boundary: 10000 ms → 10.0 seconds
    void hangTime_boundary_10000ms()
    {
        MoxController ctrl;
        QSignalSpy spy(&ctrl, &MoxController::voxHangTimeRequested);

        ctrl.setVoxHangTime(10000);  // 10000 ms → 10.0 s

        QCOMPARE(spy.count(), 1);
        QVERIFY(qFuzzyCompare(spy.at(0).at(0).toDouble(), 10.0));
    }

    // ════════════════════════════════════════════════════════════════════════
    // § B — setAntiVoxGain tests
    // ════════════════════════════════════════════════════════════════════════

    // §B.1 — Default state: setAntiVoxGain(0) emits linear = 1.0
    //
    // From Thetis setup.cs:18989 [v2.10.3.13]:
    //   Math.Pow(10.0, 0.0 / 20.0) = pow(10, 0) = 1.0
    // NaN sentinel forces first-call emit even at default value.
    void antiVoxGain_defaultZeroDb_emitsUnity()
    {
        MoxController ctrl;
        QSignalSpy spy(&ctrl, &MoxController::antiVoxGainRequested);

        ctrl.setAntiVoxGain(0);  // 0 dB → 1.0 linear; NaN sentinel forces emit

        QCOMPARE(spy.count(), 1);
        QVERIFY(qFuzzyCompare(spy.at(0).at(0).toDouble(), 1.0));
    }

    // §B.2 — Change emits the converted linear value
    //
    // From Thetis setup.cs:18989 [v2.10.3.13]:
    //   Math.Pow(10.0, (double)udAntiVoxGain.Value / 20.0)
    // -20 dB: pow(10, -1) = 0.1
    void antiVoxGain_minus20dB_emitsOneTenth()
    {
        MoxController ctrl;
        ctrl.setAntiVoxGain(0);  // prime NaN sentinel

        QSignalSpy spy(&ctrl, &MoxController::antiVoxGainRequested);
        ctrl.setAntiVoxGain(-20);  // -20 dB → pow(10, -1) = 0.1

        QCOMPARE(spy.count(), 1);
        QVERIFY(qFuzzyCompare(spy.at(0).at(0).toDouble(), 0.1));
    }

    // §B.3 — Idempotent: repeat same dB does not re-emit
    void antiVoxGain_idempotent_noDoubleEmit()
    {
        MoxController ctrl;
        ctrl.setAntiVoxGain(0);  // prime NaN sentinel; emits 1.0

        QSignalSpy spy(&ctrl, &MoxController::antiVoxGainRequested);
        ctrl.setAntiVoxGain(0);  // same → no emit

        QCOMPARE(spy.count(), 0);
    }

    // §B.4 — Boundary: -60 dB → 0.001
    //
    // From Thetis setup.cs [v2.10.3.13]:
    //   udAntiVoxGain.Minimum = -60 (from TransmitModel kAntiVoxGainDbMin)
    // pow(10, -60/20) = pow(10, -3) = 0.001
    void antiVoxGain_boundary_minus60dB()
    {
        MoxController ctrl;
        QSignalSpy spy(&ctrl, &MoxController::antiVoxGainRequested);

        ctrl.setAntiVoxGain(-60);

        QCOMPARE(spy.count(), 1);
        const double val = spy.at(0).at(0).toDouble();
        QVERIFY(!std::isnan(val));
        QVERIFY(!std::isinf(val));
        QVERIFY(val > 0.0);
        QVERIFY(qFuzzyCompare(val, 0.001));
    }

    // §B.5 — Boundary: 0 dB → 1.0 (unity; non-NaN, non-Inf, positive)
    void antiVoxGain_boundary_0dB_unity()
    {
        MoxController ctrl;
        QSignalSpy spy(&ctrl, &MoxController::antiVoxGainRequested);

        ctrl.setAntiVoxGain(0);

        QCOMPARE(spy.count(), 1);
        const double val = spy.at(0).at(0).toDouble();
        QVERIFY(!std::isnan(val));
        QVERIFY(!std::isinf(val));
        QVERIFY(qFuzzyCompare(val, 1.0));
    }

    // §B.6 — NaN sentinel: first call emits even at default
    //
    // Spy attached before any setAntiVoxGain call.
    void antiVoxGain_nanSentinel_firstCallAlwaysEmits()
    {
        MoxController ctrl;
        QSignalSpy spy(&ctrl, &MoxController::antiVoxGainRequested);

        ctrl.setAntiVoxGain(0);  // default; NaN sentinel forces emit

        QCOMPARE(spy.count(), 1);
        QVERIFY(spy.at(0).at(0).toDouble() > 0.0);
    }

    // ════════════════════════════════════════════════════════════════════════
    // § C — setAntiVoxSourceVax tests
    //
    // 3M-3a-iv post-bench refactor (Option A) removed the
    // setAntiVoxSourceVax slot, the antiVoxSourceWhatRequested signal, and
    // their associated state.  Thetis chkAntiVoxSource (RX vs VAC at
    // cmaster.cs:912-943 [v2.10.3.13]) does not map to NereusSDR's
    // architecture; see commit message and DexpVoxPage info-row for the
    // architectural rationale.  All §C test cases dropped.
    // ════════════════════════════════════════════════════════════════════════

    // ════════════════════════════════════════════════════════════════════════
    // § D — setAntiVoxTau tests (Phase 3M-3a-iv Task 7)
    //
    // From Thetis Project Files/Source/Console/setup.cs:18992-18996 [v2.10.3.13]:
    //   private void udAntiVoxTau_ValueChanged(object sender, EventArgs e)
    //   {
    //       if (initializing) return;
    //       cmaster.SetAntiVOXDetectorTau(0, (double)udAntiVoxTau.Value / 1000.0);
    //   }
    //
    // Range from setup.designer.cs:44661-44688 [v2.10.3.13]:
    //   Minimum=1, Maximum=500, Increment=1, default Value=20.
    //
    // ms→seconds (/1000.0) lives in MoxController; TxChannel takes seconds
    // directly.  NaN sentinel forces first-call emit so WDSP DEXP block is
    // primed.
    // ════════════════════════════════════════════════════════════════════════

    // §D.1 — Default 20 ms emits 0.020 seconds
    //
    // From setup.designer.cs:44685-44688 [v2.10.3.13]: udAntiVoxTau.Value = 20.
    // 20 ms / 1000.0 = 0.020 s.  NaN sentinel forces first-call emit.
    void antiVoxTau_default20ms_emits20ThousandthsSecond()
    {
        MoxController ctrl;
        QSignalSpy spy(&ctrl, &MoxController::antiVoxDetectorTauRequested);

        ctrl.setAntiVoxTau(20);  // ms; default; NaN sentinel forces emit

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toDouble(), 0.020);
    }

    // §D.2 — ms→seconds scaling: 80 ms emits 0.080
    //
    // Mirrors the /1000.0 conversion at setup.cs:18995 [v2.10.3.13].
    void antiVoxTau_msToSeconds_scaling()
    {
        MoxController ctrl;
        ctrl.setAntiVoxTau(20);  // prime NaN sentinel
        QSignalSpy spy(&ctrl, &MoxController::antiVoxDetectorTauRequested);

        ctrl.setAntiVoxTau(80);  // ms

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toDouble(), 0.080);
    }

    // §D.3 — Idempotent: repeat same ms does not re-emit
    void antiVoxTau_idempotent_noDoubleEmit()
    {
        MoxController ctrl;
        ctrl.setAntiVoxTau(20);  // prime NaN; emits 0.020
        QSignalSpy spy(&ctrl, &MoxController::antiVoxDetectorTauRequested);

        ctrl.setAntiVoxTau(20);  // same → no emit

        QCOMPARE(spy.count(), 0);
    }

    // §D.4 — Boundary: 1 ms → 0.001 s (Min from setup.designer.cs:44676)
    void antiVoxTau_boundary_1ms()
    {
        MoxController ctrl;
        QSignalSpy spy(&ctrl, &MoxController::antiVoxDetectorTauRequested);
        ctrl.setAntiVoxTau(1);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toDouble(), 0.001);
    }

    // §D.5 — Boundary: 500 ms → 0.500 s (Max from setup.designer.cs:44666)
    void antiVoxTau_boundary_500ms()
    {
        MoxController ctrl;
        QSignalSpy spy(&ctrl, &MoxController::antiVoxDetectorTauRequested);
        ctrl.setAntiVoxTau(500);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toDouble(), 0.500);
    }

    // §D.6 — NaN sentinel: first call emits even at default value
    //
    // Spy attached before any setAntiVoxTau call → fresh NaN state.  Emit
    // primes the WDSP DEXP block at startup regardless of default match.
    void antiVoxTau_nanSentinel_firstCallAlwaysEmits()
    {
        MoxController ctrl;
        QSignalSpy spy(&ctrl, &MoxController::antiVoxDetectorTauRequested);
        ctrl.setAntiVoxTau(20);  // default; sentinel forces emit
        QCOMPARE(spy.count(), 1);
    }

    // ════════════════════════════════════════════════════════════════════════
    // § E — setAntiVoxRun tests (3M-3a-iv scope-expansion)
    //
    // From Thetis setup.cs:18980-18984 [v2.10.3.13]:
    //   private void chkAntiVoxEnable_CheckedChanged(object sender, EventArgs e)
    //   {
    //       if (initializing) return;
    //       cmaster.SetAntiVOXRun(0, chkAntiVoxEnable.Checked);
    //   }
    //
    // 3M-3a-iv post-bench refactor (Option A) removed the
    // setAntiVoxSourceVax source-selector slot.  First-call emit guard
    // (m_antiVoxRunInitialized) ensures TxWorkerThread is primed at startup
    // regardless of default match.
    // ════════════════════════════════════════════════════════════════════════

    // §E.1 — Default state: setAntiVoxRun(false) emits false on first call.
    //
    // m_antiVoxRunInitialized starts false; first accepted call must emit
    // even when run==false matches the default field value.
    void antiVoxRun_default_emitsFalseFirstCall()
    {
        MoxController ctrl;
        QSignalSpy spy(&ctrl, &MoxController::antiVoxRunRequested);

        ctrl.setAntiVoxRun(false);  // default; init guard forces emit

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toBool(), false);
    }

    // §E.2 — Idempotent: once initialized, repeat same value does not re-emit.
    void antiVoxRun_idempotent()
    {
        MoxController ctrl;
        ctrl.setAntiVoxRun(true);  // primes state; emits once
        QSignalSpy spy(&ctrl, &MoxController::antiVoxRunRequested);
        ctrl.setAntiVoxRun(true);  // same → no emit
        QCOMPARE(spy.count(), 0);
        ctrl.setAntiVoxRun(false);  // change → emit
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toBool(), false);
    }

    // §E.3 — First call always emits, even at default value.
    //
    // Spy attached before any setAntiVoxRun call → fresh init guard.
    void antiVoxRun_firstCallAlwaysEmits()
    {
        MoxController ctrl;
        QSignalSpy spy(&ctrl, &MoxController::antiVoxRunRequested);
        ctrl.setAntiVoxRun(false);  // first call; init guard forces emit
        QCOMPARE(spy.count(), 1);
    }
};

QTEST_MAIN(TestMoxControllerAntiVox)
#include "tst_mox_controller_anti_vox.moc"
