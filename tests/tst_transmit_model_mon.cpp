// no-port-check: NereusSDR-original unit-test file.  The Thetis references
// below are cite comments documenting which upstream lines each assertion
// verifies; no Thetis logic is ported in this test file.
// =================================================================
// tests/tst_transmit_model_mon.cpp  (NereusSDR)
// =================================================================
//
// Unit tests for TransmitModel MON properties (2x):
//   monEnabled / monitorVolume
//
// Phase 3M-1b C.5.
//
// Source references (cited for traceability; logic ported in TransmitModel.cpp):
//   audio.cs:406 [v2.10.3.13]  — mon default off:
//     private bool mon = false;
//   audio.cs:417 [v2.10.3.13]  — monitorVolume literal mix coefficient 0.5f:
//     cmaster.SetAAudioMixVol((void*)0, 0, WDSP.id(1, 0), 0.5);
//   phase3m-1b-thetis-pre-code-review.md §4.2 + §12.5 (defaults rationale).
//
// monEnabled does NOT persist — plan §0 row 9: safety, loads OFF at startup.
// monitorVolume persists (Phase L.2).
// AudioEngine integration (setTxMonitorEnabled / setTxMonitorVolume) in Phase E.2-E.3.
// =================================================================

#include <QtTest/QtTest>
#include "models/TransmitModel.h"

using namespace NereusSDR;

class TstTransmitModelMon : public QObject {
    Q_OBJECT
private slots:

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // DEFAULT VALUES
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    void default_monEnabled_isFalse() {
        // Matches Thetis audio.cs:406 [v2.10.3.13]:
        //   private bool mon = false;
        // Safety rule (plan §0 row 9): MON always loads OFF so the user
        // does not hear unexpected headphone audio on startup.
        TransmitModel t;
        QCOMPARE(t.monEnabled(), false);
    }

    void default_monitorVolume_isPointFive() {
        // Matches Thetis audio.cs:417 [v2.10.3.13] literal mix coefficient:
        //   cmaster.SetAAudioMixVol((void*)0, 0, WDSP.id(1, 0), 0.5);
        // NereusSDR repurposes the 0.5 literal as the user-visible volume default.
        TransmitModel t;
        QCOMPARE(t.monitorVolume(), 0.5f);
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // ROUND-TRIP SETTERS (set → get → matches)
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    void setMonEnabled_true_roundTrip() {
        TransmitModel t;
        t.setMonEnabled(true);
        QCOMPARE(t.monEnabled(), true);
    }

    void setMonitorVolume_roundTrip() {
        TransmitModel t;
        t.setMonitorVolume(0.75f);
        QCOMPARE(t.monitorVolume(), 0.75f);
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // SIGNAL EMISSION
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    void setMonEnabled_emitsSignal() {
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::monEnabledChanged);
        t.setMonEnabled(true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toBool(), true);
    }

    void setMonitorVolume_emitsSignal() {
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::monitorVolumeChanged);
        t.setMonitorVolume(0.8f);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toFloat(), 0.8f);
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // IDEMPOTENT GUARD (no signal on same-value set)
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    void idempotent_monEnabled_default_noSignal() {
        // setMonEnabled(false) on fresh model (default = false) must NOT emit.
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::monEnabledChanged);
        t.setMonEnabled(false);
        QCOMPARE(spy.count(), 0);
    }

    void idempotent_monitorVolume_default_noSignal() {
        // setMonitorVolume(0.5f) on fresh model (default = 0.5f) must NOT emit.
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::monitorVolumeChanged);
        t.setMonitorVolume(0.5f);
        QCOMPARE(spy.count(), 0);
    }

    void idempotent_monitorVolume_atMax_noSignal() {
        // Set to kMonitorVolumeMax (1.0f), then set again — must NOT emit second time.
        TransmitModel t;
        t.setMonitorVolume(TransmitModel::kMonitorVolumeMax);
        QSignalSpy spy(&t, &TransmitModel::monitorVolumeChanged);
        t.setMonitorVolume(TransmitModel::kMonitorVolumeMax);
        QCOMPARE(spy.count(), 0);
    }

    void idempotent_monitorVolume_atMin_noSignal() {
        // Set to kMonitorVolumeMin (0.0f), then set again — must NOT emit second time.
        // Exercises the qFuzzyIsNull(diff) zero-boundary case (C.3 fix-up pattern).
        TransmitModel t;
        t.setMonitorVolume(TransmitModel::kMonitorVolumeMin);
        QSignalSpy spy(&t, &TransmitModel::monitorVolumeChanged);
        t.setMonitorVolume(TransmitModel::kMonitorVolumeMin);
        QCOMPARE(spy.count(), 0);
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // RANGE CLAMPING for monitorVolume [0.0f, 1.0f]
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    void monitorVolume_clampBelowMin() {
        TransmitModel t;
        t.setMonitorVolume(-0.5f);
        QCOMPARE(t.monitorVolume(), TransmitModel::kMonitorVolumeMin);
    }

    void monitorVolume_clampAboveMax() {
        TransmitModel t;
        t.setMonitorVolume(2.0f);
        QCOMPARE(t.monitorVolume(), TransmitModel::kMonitorVolumeMax);
    }

    void monitorVolume_clampAtMinBoundary() {
        TransmitModel t;
        t.setMonitorVolume(TransmitModel::kMonitorVolumeMin);
        QCOMPARE(t.monitorVolume(), TransmitModel::kMonitorVolumeMin);
    }

    void monitorVolume_clampAtMaxBoundary() {
        TransmitModel t;
        t.setMonitorVolume(TransmitModel::kMonitorVolumeMax);
        QCOMPARE(t.monitorVolume(), TransmitModel::kMonitorVolumeMax);
    }

    void monitorVolume_clampSignalCarriesClampedValue() {
        // Signal must carry clamped value (kMonitorVolumeMax = 1.0f), not raw input.
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::monitorVolumeChanged);
        t.setMonitorVolume(99.0f);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toFloat(), TransmitModel::kMonitorVolumeMax);
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // CONSTANTS SANITY
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    void constants_monitorVolume_minLessThanMax() {
        QVERIFY(TransmitModel::kMonitorVolumeMin < TransmitModel::kMonitorVolumeMax);
    }

    void constants_monitorVolume_expectedValues() {
        // Normalized volume scalar range [0.0f, 1.0f].
        // 0.5f default matches Thetis audio.cs:417 [v2.10.3.13] literal coefficient.
        QCOMPARE(TransmitModel::kMonitorVolumeMin, 0.0f);
        QCOMPARE(TransmitModel::kMonitorVolumeMax, 1.0f);
    }
};

QTEST_APPLESS_MAIN(TstTransmitModelMon)
#include "tst_transmit_model_mon.moc"
