// no-port-check: NereusSDR-original unit-test file.  The "console.cs"
// references below are cite comments documenting which Thetis lines each
// assertion verifies; no Thetis logic is ported in this test file.
// =================================================================
// tests/tst_transmit_model_mic_gain.cpp  (NereusSDR)
// =================================================================
//
// Unit tests for TransmitModel::micGainDb + derived micPreampLinear.
//
// Phase 3M-1b C.1.
//
// Source references (cited for traceability; logic ported in TransmitModel.cpp):
//   console.cs:28805-28817 [v2.10.3.13] — setAudioMicGain dB→linear conversion
//     Audio.MicPreamp = Math.Pow(10.0, gain_db / 20.0);
//   console.cs:19151-19171 [v2.10.3.13] — mic_gain_min (-40) / mic_gain_max (10)
//     private int mic_gain_min = -40;  private int mic_gain_max = 10;
//     (Note: plan §C.1 specifies kMicGainDbMin=-50 / kMicGainDbMax=70 as the
//     NereusSDR model range; setup.designer.cs shows the spin boxes allow
//     -96..0 for min and 1..70 for max — the model uses a conservative subset.)
// =================================================================

#include <QtTest/QtTest>
#include <cmath>
#include "models/TransmitModel.h"

using namespace NereusSDR;

class TstTransmitModelMicGain : public QObject {
    Q_OBJECT
private slots:

    // ── Default value ───────────────────────────────────────────────────────

    void defaultMicGainDb() {
        // Default -6 dB per plan §0 row 11 (NereusSDR-original safety addition).
        TransmitModel t;
        QCOMPARE(t.micGainDb(), -6);
    }

    void defaultMicPreampLinear() {
        // Default micPreampLinear must equal pow(10, -6/20).
        TransmitModel t;
        const double expected = std::pow(10.0, -6.0 / 20.0);
        QVERIFY(std::abs(t.micPreampLinear() - expected) < 1e-10);
    }

    // ── Setter round-trip ────────────────────────────────────────────────────

    void setZeroDb_gainIsZero() {
        TransmitModel t;
        t.setMicGainDb(0);
        QCOMPARE(t.micGainDb(), 0);
    }

    void setZeroDb_preampIsUnity() {
        TransmitModel t;
        t.setMicGainDb(0);
        // pow(10, 0/20) = 1.0
        QVERIFY(std::abs(t.micPreampLinear() - 1.0) < 1e-10);
    }

    void setPositiveDb_preampAboveUnity() {
        TransmitModel t;
        t.setMicGainDb(10);
        const double expected = std::pow(10.0, 10.0 / 20.0);
        QVERIFY(std::abs(t.micPreampLinear() - expected) < 1e-10);
    }

    void setNegativeDb_preampBelowUnity() {
        TransmitModel t;
        t.setMicGainDb(-20);
        const double expected = std::pow(10.0, -20.0 / 20.0);
        QVERIFY(std::abs(t.micPreampLinear() - expected) < 1e-10);
    }

    // ── Signal emission ──────────────────────────────────────────────────────

    void setZeroDb_emitsMicGainChanged() {
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::micGainDbChanged);
        t.setMicGainDb(0);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toInt(), 0);
    }

    void setZeroDb_emitsMicPreampChanged() {
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::micPreampChanged);
        t.setMicGainDb(0);
        QCOMPARE(spy.count(), 1);
        const double emitted = spy.first().at(0).toDouble();
        QVERIFY(std::abs(emitted - 1.0) < 1e-10);
    }

    void bothSignalsEmittedTogether() {
        // Both signals must fire on a single setter call.
        TransmitModel t;
        QSignalSpy gainSpy(&t, &TransmitModel::micGainDbChanged);
        QSignalSpy preampSpy(&t, &TransmitModel::micPreampChanged);
        t.setMicGainDb(0);
        QCOMPARE(gainSpy.count(), 1);
        QCOMPARE(preampSpy.count(), 1);
    }

    // ── Idempotent guard ──────────────────────────────────────────────────────

    void idempotent_defaultValue_noSignals() {
        // setMicGainDb(-6) on a fresh model must NOT emit (already at -6).
        TransmitModel t;
        QSignalSpy gainSpy(&t, &TransmitModel::micGainDbChanged);
        QSignalSpy preampSpy(&t, &TransmitModel::micPreampChanged);
        t.setMicGainDb(-6);  // same as default
        QCOMPARE(gainSpy.count(), 0);
        QCOMPARE(preampSpy.count(), 0);
    }

    void idempotent_explicitSameValue_noSignals() {
        // Set to 0, then set to 0 again — second call must not emit.
        TransmitModel t;
        t.setMicGainDb(0);
        QSignalSpy gainSpy(&t, &TransmitModel::micGainDbChanged);
        QSignalSpy preampSpy(&t, &TransmitModel::micPreampChanged);
        t.setMicGainDb(0);
        QCOMPARE(gainSpy.count(), 0);
        QCOMPARE(preampSpy.count(), 0);
    }

    // ── Range clamping ────────────────────────────────────────────────────────

    void clampBelowMin() {
        // setMicGainDb(-100) must clamp to kMicGainDbMin (-50).
        TransmitModel t;
        t.setMicGainDb(-100);
        QCOMPARE(t.micGainDb(), TransmitModel::kMicGainDbMin);
    }

    void clampAboveMax() {
        // setMicGainDb(100) must clamp to kMicGainDbMax (70).
        TransmitModel t;
        t.setMicGainDb(100);
        QCOMPARE(t.micGainDb(), TransmitModel::kMicGainDbMax);
    }

    void clampAtMinBoundary() {
        TransmitModel t;
        t.setMicGainDb(TransmitModel::kMicGainDbMin);
        QCOMPARE(t.micGainDb(), TransmitModel::kMicGainDbMin);
    }

    void clampAtMaxBoundary() {
        TransmitModel t;
        t.setMicGainDb(TransmitModel::kMicGainDbMax);
        QCOMPARE(t.micGainDb(), TransmitModel::kMicGainDbMax);
    }

    void clampSignalCarriesClampedValue() {
        // Signal must carry the clamped value, not the raw input.
        TransmitModel t;
        QSignalSpy spy(&t, &TransmitModel::micGainDbChanged);
        t.setMicGainDb(-200);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toInt(), TransmitModel::kMicGainDbMin);
    }

    // ── Constants sanity ──────────────────────────────────────────────────────

    void constantsHaveExpectedValues() {
        // kMicGainDbMin = -50, kMicGainDbMax = 70 per plan §C.1.
        QCOMPARE(TransmitModel::kMicGainDbMin, -50);
        QCOMPARE(TransmitModel::kMicGainDbMax,  70);
    }
};

QTEST_APPLESS_MAIN(TstTransmitModelMicGain)
#include "tst_transmit_model_mic_gain.moc"
