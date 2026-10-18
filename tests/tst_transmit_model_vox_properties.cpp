// no-port-check: NereusSDR-original unit-test file.  The Thetis references
// below are cite comments documenting which upstream lines each assertion
// verifies; no Thetis logic is ported in this test file.
// =================================================================
// tests/tst_transmit_model_vox_properties.cpp  (NereusSDR)
// =================================================================
//
// Unit tests for TransmitModel VOX properties (4x):
//   voxEnabled / voxThresholdDb / voxGainScalar / voxHangTimeMs
//
// Phase 3M-1b C.3.
//
// Source references (cited for traceability; logic ported in TransmitModel.cpp):
//   audio.cs:167-192 [v2.10.3.13]  — Audio.VOXEnabled: private bool vox_enabled = false;
//   audio.cs:194-202 [v2.10.3.13]  — Audio.VOXGain: private static float vox_gain = 1.0f;
//   console.Designer.cs:6018-6019 [v2.10.3.13]  — ptbVOX.Maximum=0, ptbVOX.Minimum=-80
//     (VOX threshold trackbar range; display unit is dB).
//   console.cs:14707-14716 [v2.10.3.13]  — Console.VOXHangTime property;
//     setup.cs:4865-4876 [v2.10.3.13]     — VOXHangTime mapped to udDEXPHold;
//     setup.designer.cs:45005-45024 [v2.10.3.13] — udDEXPHold.Minimum=1,
//       udDEXPHold.Maximum=2000, udDEXPHold.Value=500 ms.
//   phase3m-1b-thetis-pre-code-review.md §8.5 — NereusSDR VOX property mapping.
// =================================================================

#include <QtTest/QtTest>
#include "models/TransmitModel.h"

using namespace NereusSDR;

class TstTransmitModelVoxProperties : public QObject {
    Q_OBJECT
private slots:

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // DEFAULT VALUES
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    void default_voxEnabled_isFalse() {
        // Safety: VOX always loads OFF (plan §0 row 8).
        // Matches Thetis audio.cs:167 [v2.10.3.13]:
        //   private static bool vox_enabled = false;
        TransmitModel t;
        QCOMPARE(t.voxEnabled(), false);
    }

    void default_voxThresholdDb_isMinusForty() {
        // NereusSDR-original default -40 dB (plan §8.5).
        // Thetis ptbVOX default is -20 (console.Designer.cs:6024 [v2.10.3.13]:
        //   ptbVOX.Value = -20), but NereusSDR uses -40 as a more conservative
        // starting point.  Phase L.2 wires persistence; initial value is -40.
        TransmitModel t;
        QCOMPARE(t.voxThresholdDb(), -40);
    }

    void default_voxGainScalar_isOne() {
        // From Thetis audio.cs:194 [v2.10.3.13]:
        //   private static float vox_gain = 1.0f;
        // mic-boost-aware threshold scaler; 1.0 = no scaling.
        TransmitModel t;
        QVERIFY(qFuzzyCompare(t.voxGainScalar(), 1.0f));
    }

    void default_voxHangTimeMs_is500() {
        // NereusSDR-original default 500 ms (plan §8.5).
        // Matches Thetis udDEXPHold.Value = 500 ms per
        //   setup.designer.cs:45020-45024 [v2.10.3.13].
        TransmitModel t;
        QCOMPARE(t.voxHangTimeMs(), 500);
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // ROUND-TRIP SETTERS (set → get → matches)
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    void setVoxEnabled_true_roundTrip() {
        TransmitModel t;
        t.setVoxEnabled(true);
        QCOMPARE(t.voxEnabled(), true);
    }

    void setVoxThresholdDb_roundTrip() {
        TransmitModel t;
        t.setVoxThresholdDb(-20);
        QCOMPARE(t.voxThresholdDb(), -20);
    }

    void setVoxGainScalar_roundTrip() {
        TransmitModel t;
        t.setVoxGainScalar(2.5f);
        QVERIFY(qFuzzyCompare(t.voxGainScalar(), 2.5f));
    }

    void setVoxHangTimeMs_roundTrip() {
        TransmitModel t;
        t.setVoxHangTimeMs(1000);
        QCOMPARE(t.voxHangTimeMs(), 1000);
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // SIGNAL EMISSION
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    void setVoxEnabled_emitsSignal() {
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::voxEnabledChanged);
        t.setVoxEnabled(true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toBool(), true);
    }

    void setVoxThresholdDb_emitsSignal() {
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::voxThresholdDbChanged);
        t.setVoxThresholdDb(-60);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toInt(), -60);
    }

    void setVoxGainScalar_emitsSignal() {
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::voxGainScalarChanged);
        t.setVoxGainScalar(3.0f);
        QCOMPARE(spy.count(), 1);
        QVERIFY(qFuzzyCompare(spy.first().at(0).toFloat(), 3.0f));
    }

    void setVoxHangTimeMs_emitsSignal() {
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::voxHangTimeMsChanged);
        t.setVoxHangTimeMs(750);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toInt(), 750);
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // IDEMPOTENT GUARD (no signal on same-value set)
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    void idempotent_voxEnabled_default_noSignal() {
        // setVoxEnabled(false) on fresh model (default = false) must NOT emit.
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::voxEnabledChanged);
        t.setVoxEnabled(false);
        QCOMPARE(spy.count(), 0);
    }

    void idempotent_voxThresholdDb_default_noSignal() {
        // setVoxThresholdDb(-40) on fresh model (default = -40) must NOT emit.
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::voxThresholdDbChanged);
        t.setVoxThresholdDb(-40);
        QCOMPARE(spy.count(), 0);
    }

    void idempotent_voxGainScalar_default_noSignal() {
        // setVoxGainScalar(1.0f) on fresh model (default = 1.0f) must NOT emit.
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::voxGainScalarChanged);
        t.setVoxGainScalar(1.0f);
        QCOMPARE(spy.count(), 0);
    }

    void idempotent_voxHangTimeMs_default_noSignal() {
        // setVoxHangTimeMs(500) on fresh model (default = 500) must NOT emit.
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::voxHangTimeMsChanged);
        t.setVoxHangTimeMs(500);
        QCOMPARE(spy.count(), 0);
    }

    void idempotent_voxGainScalar_atMin_noSignal() {
        // qFuzzyCompare(0.0f, 0.0f) zero-boundary: evaluates 0 <= 0 = true,
        // so the guard correctly suppresses emission at kVoxGainScalarMin.
        // Locks in the zero-boundary behavior explicitly.
        TransmitModel t;
        t.setVoxGainScalar(TransmitModel::kVoxGainScalarMin);  // 0.0f
        QSignalSpy spy(&t, &TransmitModel::voxGainScalarChanged);
        t.setVoxGainScalar(TransmitModel::kVoxGainScalarMin);  // 0.0f again
        QCOMPARE(spy.count(), 0);
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // RANGE CLAMPING
    // (ptbVOX: Minimum=-80, Maximum=0 per console.Designer.cs:6018-6019 [v2.10.3.13])
    // (udDEXPHold: Minimum=1, Maximum=2000 per setup.designer.cs:45010-45008 [v2.10.3.13])
    // (voxGainScalar: [0.0f, 100.0f] NereusSDR sane upper guard; Thetis unguarded)
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    void voxThresholdDb_clampBelowMin() {
        // ptbVOX.Minimum = -80 (console.Designer.cs:6019 [v2.10.3.13]).
        TransmitModel t;
        t.setVoxThresholdDb(-200);
        QCOMPARE(t.voxThresholdDb(), TransmitModel::kVoxThresholdDbMin);
    }

    void voxThresholdDb_clampAboveMax() {
        // ptbVOX.Maximum = 0 (console.Designer.cs:6018 [v2.10.3.13]).
        TransmitModel t;
        t.setVoxThresholdDb(10);
        QCOMPARE(t.voxThresholdDb(), TransmitModel::kVoxThresholdDbMax);
    }

    void voxThresholdDb_clampAtMinBoundary() {
        TransmitModel t;
        t.setVoxThresholdDb(TransmitModel::kVoxThresholdDbMin);
        QCOMPARE(t.voxThresholdDb(), TransmitModel::kVoxThresholdDbMin);
    }

    void voxThresholdDb_clampAtMaxBoundary() {
        TransmitModel t;
        t.setVoxThresholdDb(TransmitModel::kVoxThresholdDbMax);
        QCOMPARE(t.voxThresholdDb(), TransmitModel::kVoxThresholdDbMax);
    }

    void voxThresholdDb_clampSignalCarriesClampedValue() {
        // Signal must carry clamped value, not raw input.
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::voxThresholdDbChanged);
        t.setVoxThresholdDb(-999);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toInt(), TransmitModel::kVoxThresholdDbMin);
    }

    void voxGainScalar_clampSignalCarriesClampedValue() {
        // Signal must carry clamped value (kVoxGainScalarMax = 100.0f), not raw input.
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::voxGainScalarChanged);
        t.setVoxGainScalar(200.0f);  // exceeds kVoxGainScalarMax = 100.0f
        QCOMPARE(spy.count(), 1);
        const QList<QVariant> args = spy.takeFirst();
        QVERIFY(qFuzzyCompare(args.at(0).value<float>(), TransmitModel::kVoxGainScalarMax));
    }

    void voxHangTimeMs_clampSignalCarriesClampedValue() {
        // Signal must carry clamped value (kVoxHangTimeMsMax = 2000), not raw input.
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::voxHangTimeMsChanged);
        t.setVoxHangTimeMs(5000);  // exceeds kVoxHangTimeMsMax = 2000
        QCOMPARE(spy.count(), 1);
        const QList<QVariant> args = spy.takeFirst();
        QCOMPARE(args.at(0).value<int>(), TransmitModel::kVoxHangTimeMsMax);
    }

    void voxGainScalar_clampBelowMin() {
        // kVoxGainScalarMin = 0.0f (NereusSDR sane guard).
        TransmitModel t;
        t.setVoxGainScalar(-5.0f);
        QVERIFY(qFuzzyCompare(t.voxGainScalar(), TransmitModel::kVoxGainScalarMin));
    }

    void voxGainScalar_clampAboveMax() {
        // kVoxGainScalarMax = 100.0f (NereusSDR sane guard).
        TransmitModel t;
        t.setVoxGainScalar(999.0f);
        QVERIFY(qFuzzyCompare(t.voxGainScalar(), TransmitModel::kVoxGainScalarMax));
    }

    void voxHangTimeMs_clampBelowMin() {
        // udDEXPHold.Minimum = 1 ms (setup.designer.cs:45010-45013 [v2.10.3.13]).
        TransmitModel t;
        t.setVoxHangTimeMs(0);
        QCOMPARE(t.voxHangTimeMs(), TransmitModel::kVoxHangTimeMsMin);
    }

    void voxHangTimeMs_clampAboveMax() {
        // udDEXPHold.Maximum = 2000 ms (setup.designer.cs:45005-45008 [v2.10.3.13]).
        TransmitModel t;
        t.setVoxHangTimeMs(9999);
        QCOMPARE(t.voxHangTimeMs(), TransmitModel::kVoxHangTimeMsMax);
    }

    void voxHangTimeMs_clampAtMinBoundary() {
        TransmitModel t;
        t.setVoxHangTimeMs(TransmitModel::kVoxHangTimeMsMin);
        QCOMPARE(t.voxHangTimeMs(), TransmitModel::kVoxHangTimeMsMin);
    }

    void voxHangTimeMs_clampAtMaxBoundary() {
        TransmitModel t;
        t.setVoxHangTimeMs(TransmitModel::kVoxHangTimeMsMax);
        QCOMPARE(t.voxHangTimeMs(), TransmitModel::kVoxHangTimeMsMax);
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // CONSTANTS SANITY
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    void constants_voxThresholdDb_minLessThanMax() {
        // ptbVOX: Minimum=-80 < Maximum=0 (console.Designer.cs:6018-6019 [v2.10.3.13]).
        QVERIFY(TransmitModel::kVoxThresholdDbMin < TransmitModel::kVoxThresholdDbMax);
    }

    void constants_voxThresholdDb_expectedValues() {
        // Exact values from console.Designer.cs:6018-6019 [v2.10.3.13]:
        //   ptbVOX.Maximum = 0, ptbVOX.Minimum = -80
        QCOMPARE(TransmitModel::kVoxThresholdDbMin, -80);
        QCOMPARE(TransmitModel::kVoxThresholdDbMax,   0);
    }

    void constants_voxGainScalar_minLessThanMax() {
        QVERIFY(TransmitModel::kVoxGainScalarMin < TransmitModel::kVoxGainScalarMax);
    }

    void constants_voxGainScalar_expectedValues() {
        // NereusSDR sane guard: [0.0f, 100.0f]; Thetis has no explicit clamp.
        QVERIFY(qFuzzyCompare(TransmitModel::kVoxGainScalarMin, 0.0f));
        QVERIFY(qFuzzyCompare(TransmitModel::kVoxGainScalarMax, 100.0f));
    }

    void constants_voxHangTimeMs_minLessThanMax() {
        // udDEXPHold: Minimum=1 < Maximum=2000 (setup.designer.cs:45010-45008 [v2.10.3.13]).
        QVERIFY(TransmitModel::kVoxHangTimeMsMin < TransmitModel::kVoxHangTimeMsMax);
    }

    void constants_voxHangTimeMs_expectedValues() {
        // Exact values from setup.designer.cs:45005-45013 [v2.10.3.13]:
        //   udDEXPHold.Maximum = 2000, udDEXPHold.Minimum = 1
        QCOMPARE(TransmitModel::kVoxHangTimeMsMin,    1);
        QCOMPARE(TransmitModel::kVoxHangTimeMsMax, 2000);
    }
};

QTEST_APPLESS_MAIN(TstTransmitModelVoxProperties)
#include "tst_transmit_model_vox_properties.moc"
