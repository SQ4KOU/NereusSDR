// no-port-check: NereusSDR-original unit-test file.  Thetis cite comments
// below identify which upstream lines each assertion verifies; no Thetis
// logic is ported in this test file.
// =================================================================
// tests/tst_transmit_model_two_tone_properties.cpp  (NereusSDR)
// =================================================================
//
// Unit tests for TransmitModel two-tone test properties (7x), Phase 3M-1c B.2:
//   TwoToneFreq1, TwoToneFreq2, TwoToneLevel, TwoTonePower,
//   TwoToneFreq2Delay, TwoToneInvert, TwoTonePulsed
//
// Source references (for traceability):
//   setup.Designer.cs:62109-62136 [v2.10.3.13]  — udTestIMDFreq1
//     Value=700; Range -20000..20000; Increment=1
//   setup.Designer.cs:62027-62054 [v2.10.3.13]  — udTestIMDFreq2
//     Value=1900 (decoded from decimal{19000,0,0,65536}); Range -20000..20000
//   setup.Designer.cs:61985-62014 [v2.10.3.13]  — udTwoToneLevel
//     Value=0 dB (Designer); Range -96..0; DecimalPlaces=3
//   setup.Designer.cs:62056-62084 [v2.10.3.13]  — udTestIMDPower
//     Value=10 (Designer); Range 0..100; Increment=1
//   setup.Designer.cs:61920-61947 [v2.10.3.13]  — udFreq2Delay
//     Value=0; Range 0..1000; Increment=1
//   setup.Designer.cs:61960-61972 [v2.10.3.13]  — chkInvertTones
//     Checked=true (Designer): Invert ON for LS modes (correct LSB behaviour)
//   setup.Designer.cs:61643-61653 [v2.10.3.13]  — chkPulsed_TwoTone
//     Checked default=false (no Checked= line in Designer)
//
// Pre-code review §2.3 + design spec §4.4 — NereusSDR-original safer
// numeric defaults: TwoToneLevel=-6 dB, TwoTonePower=50% (vs Thetis Designer
// 0 dB / 10%); ranges and TwoToneInvert default match Thetis Designer per
// option C decision.
// =================================================================

#include <QtTest/QtTest>
#include "models/TransmitModel.h"

using namespace NereusSDR;

class TstTransmitModelTwoToneProperties : public QObject {
    Q_OBJECT
private slots:

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // DEFAULT VALUES
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    void default_twoToneFreq1_is700() {
        // Matches Thetis Designer default + btnTwoToneF_defaults preset
        // (setup.cs:34226 [v2.10.3.13]: udTestIMDFreq1.Value = 700).
        TransmitModel t;
        QCOMPARE(t.twoToneFreq1(), 700);
    }

    void default_twoToneFreq2_is1900() {
        // Matches Thetis Designer default + btnTwoToneF_defaults preset
        // (setup.cs:34227 [v2.10.3.13]: udTestIMDFreq2.Value = 1900).
        TransmitModel t;
        QCOMPARE(t.twoToneFreq2(), 1900);
    }

    void default_twoToneLevel_isMinus6() {
        // NereusSDR-original safer default: -6 dB.
        // Thetis Designer udTwoToneLevel.Value = 0 dB at startup (full
        // amplitude).  NereusSDR uses -6 dB as a conservative starting
        // point per design spec §4.4 / pre-code review §2.3 (option C).
        TransmitModel t;
        QCOMPARE(t.twoToneLevel(), -6.0);
    }

    void default_twoTonePower_is50() {
        // NereusSDR-original default: 50%.
        // Thetis Designer udTestIMDPower.Value = 10% at startup.
        // NereusSDR uses 50% per design spec §4.4 / pre-code review §2.3
        // (option C — typical IMD test power level).
        TransmitModel t;
        QCOMPARE(t.twoTonePower(), 50);
    }

    void default_twoToneFreq2Delay_is0() {
        // Matches Thetis Designer default
        // (setup.Designer.cs:61943-61947 [v2.10.3.13]: Value = 0).
        TransmitModel t;
        QCOMPARE(t.twoToneFreq2Delay(), 0);
    }

    void default_twoToneInvert_isTrue() {
        // Matches Thetis Designer default
        // (setup.Designer.cs:61963 [v2.10.3.13]: Checked = true).
        // Functionally correct for LSB/CWL/DIGL: tones land at audio-band
        // positions per setup.cs:11058-11062 conditional sign-flip.
        TransmitModel t;
        QCOMPARE(t.twoToneInvert(), true);
    }

    void default_twoTonePulsed_isFalse() {
        // Matches Thetis Designer default — no Checked= line at
        // setup.Designer.cs:61643-61653 [v2.10.3.13], so Forms default = false.
        TransmitModel t;
        QCOMPARE(t.twoTonePulsed(), false);
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // ROUND-TRIP SETTER + SIGNAL (combined per property)
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    void setTwoToneFreq1_roundTripAndSignal() {
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::twoToneFreq1Changed);
        t.setTwoToneFreq1(800);
        QCOMPARE(t.twoToneFreq1(), 800);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toInt(), 800);
    }

    void setTwoToneFreq2_roundTripAndSignal() {
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::twoToneFreq2Changed);
        t.setTwoToneFreq2(2100);
        QCOMPARE(t.twoToneFreq2(), 2100);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toInt(), 2100);
    }

    void setTwoToneLevel_roundTripAndSignal() {
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::twoToneLevelChanged);
        t.setTwoToneLevel(-12.5);
        QCOMPARE(t.twoToneLevel(), -12.5);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toDouble(), -12.5);
    }

    void setTwoTonePower_roundTripAndSignal() {
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::twoTonePowerChanged);
        t.setTwoTonePower(75);
        QCOMPARE(t.twoTonePower(), 75);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toInt(), 75);
    }

    void setTwoToneFreq2Delay_roundTripAndSignal() {
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::twoToneFreq2DelayChanged);
        t.setTwoToneFreq2Delay(250);
        QCOMPARE(t.twoToneFreq2Delay(), 250);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toInt(), 250);
    }

    void setTwoToneInvert_roundTripAndSignal() {
        // Default true — flip to false to test the change path.
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::twoToneInvertChanged);
        t.setTwoToneInvert(false);
        QCOMPARE(t.twoToneInvert(), false);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toBool(), false);
    }

    void setTwoTonePulsed_roundTripAndSignal() {
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::twoTonePulsedChanged);
        t.setTwoTonePulsed(true);
        QCOMPARE(t.twoTonePulsed(), true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toBool(), true);
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // IDEMPOTENT GUARD (no signal on same-value set at default)
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    void idempotent_twoToneFreq1_atDefault_noSignal() {
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::twoToneFreq1Changed);
        t.setTwoToneFreq1(700);  // matches default
        QCOMPARE(spy.count(), 0);
    }

    void idempotent_twoToneFreq2_atDefault_noSignal() {
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::twoToneFreq2Changed);
        t.setTwoToneFreq2(1900);  // matches default
        QCOMPARE(spy.count(), 0);
    }

    void idempotent_twoToneLevel_atDefault_noSignal() {
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::twoToneLevelChanged);
        t.setTwoToneLevel(-6.0);  // matches default
        QCOMPARE(spy.count(), 0);
    }

    void idempotent_twoTonePower_atDefault_noSignal() {
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::twoTonePowerChanged);
        t.setTwoTonePower(50);  // matches default
        QCOMPARE(spy.count(), 0);
    }

    void idempotent_twoToneFreq2Delay_atDefault_noSignal() {
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::twoToneFreq2DelayChanged);
        t.setTwoToneFreq2Delay(0);  // matches default
        QCOMPARE(spy.count(), 0);
    }

    void idempotent_twoToneInvert_atDefault_noSignal() {
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::twoToneInvertChanged);
        t.setTwoToneInvert(true);  // matches default
        QCOMPARE(spy.count(), 0);
    }

    void idempotent_twoTonePulsed_atDefault_noSignal() {
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::twoTonePulsedChanged);
        t.setTwoTonePulsed(false);  // matches default
        QCOMPARE(spy.count(), 0);
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // RANGE CLAMPING (numeric properties only)
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    void twoToneFreq1_clampBelowMin() {
        // Thetis udTestIMDFreq1 Min = -20000 (setup.Designer.cs:62122-62126).
        TransmitModel t;
        t.setTwoToneFreq1(-99999);
        QCOMPARE(t.twoToneFreq1(), TransmitModel::kTwoToneFreq1HzMin);
    }

    void twoToneFreq1_clampAboveMax() {
        // Thetis udTestIMDFreq1 Max = 20000 (setup.Designer.cs:62117-62121).
        TransmitModel t;
        t.setTwoToneFreq1(99999);
        QCOMPARE(t.twoToneFreq1(), TransmitModel::kTwoToneFreq1HzMax);
    }

    void twoToneFreq2_clampBelowMin() {
        TransmitModel t;
        t.setTwoToneFreq2(-99999);
        QCOMPARE(t.twoToneFreq2(), TransmitModel::kTwoToneFreq2HzMin);
    }

    void twoToneFreq2_clampAboveMax() {
        TransmitModel t;
        t.setTwoToneFreq2(99999);
        QCOMPARE(t.twoToneFreq2(), TransmitModel::kTwoToneFreq2HzMax);
    }

    void twoToneLevel_clampBelowMin() {
        // Thetis udTwoToneLevel Min = -96 (setup.Designer.cs:61999-62003).
        TransmitModel t;
        t.setTwoToneLevel(-200.0);
        QCOMPARE(t.twoToneLevel(), TransmitModel::kTwoToneLevelDbMin);
    }

    void twoToneLevel_clampAboveMax() {
        // Thetis udTwoToneLevel Max = 0 (setup.Designer.cs:61994-61998).
        TransmitModel t;
        t.setTwoToneLevel(50.0);
        QCOMPARE(t.twoToneLevel(), TransmitModel::kTwoToneLevelDbMax);
    }

    void twoTonePower_clampBelowMin() {
        // Thetis udTestIMDPower Min = 0 (setup.Designer.cs:62069-62073).
        TransmitModel t;
        t.setTwoTonePower(-50);
        QCOMPARE(t.twoTonePower(), TransmitModel::kTwoTonePowerMin);
    }

    void twoTonePower_clampAboveMax() {
        // Thetis udTestIMDPower Max = 100 (setup.Designer.cs:62064-62068).
        TransmitModel t;
        t.setTwoTonePower(150);
        QCOMPARE(t.twoTonePower(), TransmitModel::kTwoTonePowerMax);
    }

    void twoToneFreq2Delay_clampBelowMin() {
        // Thetis udFreq2Delay Min = 0 (setup.Designer.cs:61933-61937).
        TransmitModel t;
        t.setTwoToneFreq2Delay(-100);
        QCOMPARE(t.twoToneFreq2Delay(), TransmitModel::kTwoToneFreq2DelayMsMin);
    }

    void twoToneFreq2Delay_clampAboveMax() {
        // Thetis udFreq2Delay Max = 1000 (setup.Designer.cs:61928-61932).
        TransmitModel t;
        t.setTwoToneFreq2Delay(5000);
        QCOMPARE(t.twoToneFreq2Delay(), TransmitModel::kTwoToneFreq2DelayMsMax);
    }

    void twoToneFreq1_clampSignalCarriesClampedValue() {
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::twoToneFreq1Changed);
        t.setTwoToneFreq1(99999);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toInt(), TransmitModel::kTwoToneFreq1HzMax);
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // CONSTANTS SANITY (verify Thetis-Designer-derived ranges)
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

    void constants_twoToneFreq1_expectedValues() {
        // setup.Designer.cs:62117-62126 [v2.10.3.13]:
        //   Maximum = decimal{20000,0,0,0}            =  20000
        //   Minimum = decimal{20000,0,0,-2147483648}  = -20000
        QCOMPARE(TransmitModel::kTwoToneFreq1HzMin, -20000);
        QCOMPARE(TransmitModel::kTwoToneFreq1HzMax,  20000);
    }

    void constants_twoToneFreq2_expectedValues() {
        // setup.Designer.cs:62035-62044 [v2.10.3.13]: same range as Freq1.
        QCOMPARE(TransmitModel::kTwoToneFreq2HzMin, -20000);
        QCOMPARE(TransmitModel::kTwoToneFreq2HzMax,  20000);
    }

    void constants_twoToneLevel_expectedValues() {
        // setup.Designer.cs:61994-62003 [v2.10.3.13]:
        //   Maximum = decimal{0,0,0,65536}            =   0.0  (scale=1)
        //   Minimum = decimal{96,0,0,-2147483648}     = -96.0
        QCOMPARE(TransmitModel::kTwoToneLevelDbMin, -96.0);
        QCOMPARE(TransmitModel::kTwoToneLevelDbMax,   0.0);
    }

    void constants_twoTonePower_expectedValues() {
        // setup.Designer.cs:62064-62073 [v2.10.3.13]:
        //   Maximum = 100, Minimum = 0
        QCOMPARE(TransmitModel::kTwoTonePowerMin,   0);
        QCOMPARE(TransmitModel::kTwoTonePowerMax, 100);
    }

    void constants_twoToneFreq2Delay_expectedValues() {
        // setup.Designer.cs:61928-61937 [v2.10.3.13]:
        //   Maximum = 1000, Minimum = 0
        QCOMPARE(TransmitModel::kTwoToneFreq2DelayMsMin,    0);
        QCOMPARE(TransmitModel::kTwoToneFreq2DelayMsMax, 1000);
    }
};

QTEST_APPLESS_MAIN(TstTransmitModelTwoToneProperties)
#include "tst_transmit_model_two_tone_properties.moc"
