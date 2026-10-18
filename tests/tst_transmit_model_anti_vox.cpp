// no-port-check: NereusSDR-original unit-test file.  The Thetis references
// below are cite comments documenting which upstream lines each assertion
// verifies; no Thetis logic is ported in this test file.
// =================================================================
// tests/tst_transmit_model_anti_vox.cpp  (NereusSDR)
// =================================================================
//
// Unit tests for TransmitModel anti-VOX properties:
//   antiVoxGainDb / antiVoxTauMs
//
// Phase 3M-1b C.4 (gain) + Phase 3M-3a-iv Task 8 (tau).
//
// 3M-3a-iv post-bench refactor (Option A): the antiVoxSourceVax property
// and its tests have been removed.  Thetis chkAntiVoxSource (RX vs VAC
// at audio.cs:446-454 / setup.designer.cs:44646-44657 [v2.10.3.13]) does
// not map to NereusSDR's architecture; see the architectural-divergence
// section in docs/architecture/phase3m-3a-iv-antivox-feed-design.md §18.
//
// Source references (cited for traceability; logic ported in TransmitModel.cpp):
//   setup.designer.cs:44699-44728 [v2.10.3.13]  — udAntiVoxGain:
//     Minimum = -60, Maximum = 60 (decoded from C# decimal int[4] format;
//     DecimalPlaces=1 so the display unit is dB×0.1; NereusSDR stores as int dB).
//   setup.designer.cs:44661-44688 [v2.10.3.13]  — udAntiVoxTau:
//     Minimum = 1, Maximum = 500, Increment = 1, Value = 20.
//   setup.cs:18986-18989 [v2.10.3.13]  — udAntiVoxGain_ValueChanged:
//     cmaster.SetAntiVOXGain(0, Math.Pow(10.0, (double)udAntiVoxGain.Value / 20.0));
// =================================================================

#include <QtTest/QtTest>
#include "core/AppSettings.h"
#include "models/TransmitModel.h"

using namespace NereusSDR;

class TstTransmitModelAntiVox : public QObject {
    Q_OBJECT
private slots:

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // DEFAULT VALUES
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    void default_antiVoxGainDb_isZero() {
        // NereusSDR-original default 0 dB (plan §C.4 — safe sane default).
        // Thetis udAntiVoxGain designer default is 1.0 dB
        // (setup.designer.cs:44723-44727 [v2.10.3.13]: Value = {10,0,0,0}
        // with DecimalPlaces=1 → 1.0 dB display), but NereusSDR uses 0 dB
        // as a conservative starting point.
        TransmitModel t;
        QCOMPARE(t.antiVoxGainDb(), 0);
    }

    // 3M-3a-iv post-bench refactor (Option A): default_antiVoxSourceVax_isFalse
    // removed alongside the antiVoxSourceVax property.

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // ROUND-TRIP SETTERS (set → get → matches)
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    void setAntiVoxGainDb_roundTrip() {
        TransmitModel t;
        t.setAntiVoxGainDb(-20);
        QCOMPARE(t.antiVoxGainDb(), -20);
    }

    // 3M-3a-iv post-bench refactor (Option A): setAntiVoxSourceVax_true_roundTrip
    // removed alongside the antiVoxSourceVax property.

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // SIGNAL EMISSION
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    void setAntiVoxGainDb_emitsSignal() {
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::antiVoxGainDbChanged);
        t.setAntiVoxGainDb(-30);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toInt(), -30);
    }

    // 3M-3a-iv post-bench refactor (Option A): setAntiVoxSourceVax_emitsSignal
    // removed alongside the antiVoxSourceVax property.

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // IDEMPOTENT GUARD (no signal on same-value set)
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    void idempotent_antiVoxGainDb_default_noSignal() {
        // setAntiVoxGainDb(0) on fresh model (default = 0) must NOT emit.
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::antiVoxGainDbChanged);
        t.setAntiVoxGainDb(0);
        QCOMPARE(spy.count(), 0);
    }

    // 3M-3a-iv post-bench refactor (Option A): idempotent_antiVoxSourceVax_default_noSignal
    // removed alongside the antiVoxSourceVax property.

    void idempotent_antiVoxGainDb_atMin_noSignal() {
        // Set to kAntiVoxGainDbMin, then set again — must NOT emit second time.
        TransmitModel t;
        t.setAntiVoxGainDb(TransmitModel::kAntiVoxGainDbMin);
        QSignalSpy spy(&t, &TransmitModel::antiVoxGainDbChanged);
        t.setAntiVoxGainDb(TransmitModel::kAntiVoxGainDbMin);
        QCOMPARE(spy.count(), 0);
    }

    void idempotent_antiVoxGainDb_atMax_noSignal() {
        // Set to kAntiVoxGainDbMax, then set again — must NOT emit second time.
        TransmitModel t;
        t.setAntiVoxGainDb(TransmitModel::kAntiVoxGainDbMax);
        QSignalSpy spy(&t, &TransmitModel::antiVoxGainDbChanged);
        t.setAntiVoxGainDb(TransmitModel::kAntiVoxGainDbMax);
        QCOMPARE(spy.count(), 0);
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // RANGE CLAMPING
    // (udAntiVoxGain: Minimum=-60, Maximum=60 per
    //  setup.designer.cs:44708-44717 [v2.10.3.13])
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    void antiVoxGainDb_clampBelowMin() {
        // udAntiVoxGain.Minimum = -60 (setup.designer.cs:44713-44717 [v2.10.3.13]).
        TransmitModel t;
        t.setAntiVoxGainDb(-200);
        QCOMPARE(t.antiVoxGainDb(), TransmitModel::kAntiVoxGainDbMin);
    }

    void antiVoxGainDb_clampAboveMax() {
        // udAntiVoxGain.Maximum = 60 (setup.designer.cs:44708-44712 [v2.10.3.13]).
        TransmitModel t;
        t.setAntiVoxGainDb(200);
        QCOMPARE(t.antiVoxGainDb(), TransmitModel::kAntiVoxGainDbMax);
    }

    void antiVoxGainDb_clampAtMinBoundary() {
        TransmitModel t;
        t.setAntiVoxGainDb(TransmitModel::kAntiVoxGainDbMin);
        QCOMPARE(t.antiVoxGainDb(), TransmitModel::kAntiVoxGainDbMin);
    }

    void antiVoxGainDb_clampAtMaxBoundary() {
        TransmitModel t;
        t.setAntiVoxGainDb(TransmitModel::kAntiVoxGainDbMax);
        QCOMPARE(t.antiVoxGainDb(), TransmitModel::kAntiVoxGainDbMax);
    }

    void antiVoxGainDb_clampSignalCarriesClampedValue() {
        // Signal must carry clamped value (kAntiVoxGainDbMin = -60), not raw input.
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::antiVoxGainDbChanged);
        t.setAntiVoxGainDb(-999);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toInt(), TransmitModel::kAntiVoxGainDbMin);
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // CONSTANTS SANITY
    // (udAntiVoxGain: Minimum=-60, Maximum=60 per
    //  setup.designer.cs:44708-44717 [v2.10.3.13])
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    void constants_antiVoxGainDb_minLessThanMax() {
        QVERIFY(TransmitModel::kAntiVoxGainDbMin < TransmitModel::kAntiVoxGainDbMax);
    }

    void constants_antiVoxGainDb_expectedValues() {
        // Exact values from setup.designer.cs:44708-44717 [v2.10.3.13]:
        //   udAntiVoxGain.Maximum decoded from decimal{60,0,0,0}  = 60
        //   udAntiVoxGain.Minimum decoded from decimal{60,0,0,-2147483648} = -60
        // (DecimalPlaces=1, so display unit is ×0.1 dB; NereusSDR stores as int dB
        //  matching the WDSP SetAntiVOXGain power-of-10 formula in setup.cs:18989.)
        QCOMPARE(TransmitModel::kAntiVoxGainDbMin, -60);
        QCOMPARE(TransmitModel::kAntiVoxGainDbMax,  60);
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // antiVoxTauMs (Phase 3M-3a-iv Task 8)
    // (udAntiVoxTau: Min=1, Max=500, Increment=1, Default=20 per
    //  setup.designer.cs:44661-44688 [v2.10.3.13])
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    void antiVoxTauMs_default20() {
        // From Thetis setup.designer.cs:44682 [v2.10.3.13]: udAntiVoxTau.Value=20.
        TransmitModel m;
        QCOMPARE(m.antiVoxTauMs(), 20);
    }

    void antiVoxTauMs_clampsToRange() {
        // From Thetis setup.designer.cs:44672 / 44667 [v2.10.3.13]: Min=1, Max=500.
        TransmitModel m;
        m.setAntiVoxTauMs(0);     QCOMPARE(m.antiVoxTauMs(), 1);
        m.setAntiVoxTauMs(-100);  QCOMPARE(m.antiVoxTauMs(), 1);
        m.setAntiVoxTauMs(501);   QCOMPARE(m.antiVoxTauMs(), 500);
        m.setAntiVoxTauMs(50000); QCOMPARE(m.antiVoxTauMs(), 500);
    }

    void antiVoxTauMs_changedSignal_idempotentGuard() {
        TransmitModel m;
        QSignalSpy spy(&m, &TransmitModel::antiVoxTauMsChanged);
        m.setAntiVoxTauMs(50);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toInt(), 50);
        m.setAntiVoxTauMs(50);  // no change → no second emission
        QCOMPARE(spy.count(), 1);
    }

    void antiVoxTauMs_persistsAcrossLoad() {
        // Round-trip through AppSettings via loadFromSettings(mac) + auto-persist.
        // Pattern mirrors tst_tx_applet_rf_power_per_band.cpp::
        // slider_writes_roundtripAcrossReload (Phase A write, Phase B reload).
        AppSettings::instance().clear();

        const QString mac = QStringLiteral("aa:bb:cc:dd:ee:ff");

        {
            TransmitModel m1;
            m1.loadFromSettings(mac);  // activates per-MAC auto-persist
            m1.setAntiVoxTauMs(80);    // → persistOne(AntiVox_Tau_Ms, "80")
        }

        TransmitModel m2;
        m2.loadFromSettings(mac);
        QCOMPARE(m2.antiVoxTauMs(), 80);

        AppSettings::instance().clear();
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // antiVoxRun (3M-3a-iv scope-expansion — chkAntiVoxEnable)
    //
    // From Thetis setup.designer.cs:44740-44751 [v2.10.3.13]: chkAntiVoxEnable
    // is initially unchecked.  Persistence + UI default both follow.
    //
    // Handler at setup.cs:18980-18984 [v2.10.3.13]:
    //   private void chkAntiVoxEnable_CheckedChanged(object sender, EventArgs e)
    //   {
    //       if (initializing) return;
    //       cmaster.SetAntiVOXRun(0, chkAntiVoxEnable.Checked);
    //   }
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    void antiVoxRun_defaultFalse() {
        TransmitModel m;
        QCOMPARE(m.antiVoxRun(), false);
    }

    void antiVoxRun_changedSignal_idempotentGuard() {
        TransmitModel m;
        QSignalSpy spy(&m, &TransmitModel::antiVoxRunChanged);
        m.setAntiVoxRun(true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toBool(), true);
        m.setAntiVoxRun(true);  // no change
        QCOMPARE(spy.count(), 1);
        m.setAntiVoxRun(false);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.last().at(0).toBool(), false);
    }

    void antiVoxRun_persistsAcrossLoad() {
        AppSettings::instance().clear();

        const QString mac = QStringLiteral("aa:bb:cc:dd:ee:ff");

        {
            TransmitModel m1;
            m1.loadFromSettings(mac);  // activates per-MAC auto-persist
            m1.setAntiVoxRun(true);    // → persistOne(AntiVox_Enable, "True")
        }

        TransmitModel m2;
        m2.loadFromSettings(mac);
        QCOMPARE(m2.antiVoxRun(), true);

        AppSettings::instance().clear();
    }
};

QTEST_APPLESS_MAIN(TstTransmitModelAntiVox)
#include "tst_transmit_model_anti_vox.moc"
