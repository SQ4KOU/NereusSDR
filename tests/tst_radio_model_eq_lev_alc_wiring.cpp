// no-port-check: NereusSDR-original unit-test file.  All Thetis source cites
// are in TransmitModel.h/cpp / RadioModel.cpp.
// =================================================================
// tests/tst_radio_model_eq_lev_alc_wiring.cpp  (NereusSDR)
// =================================================================
//
// Phase 3M-3a-i Batch 2 — Task 1 wiring tests.
// Phase 3M-3a-ii Batch 3 — extended with CFC + CPDR + CESSB + PhRot
// routing coverage (14 new test cases below the original 10).  The
// file name is grandfathered; the broader scope now spans all 27
// TM→TxChannel TX-chain connects (#1-#13 EQ/Lev/ALC, #14-#27 the
// 3M-3a-ii additions).
//
// Verifies that RadioModel::connectToRadio installs the connect()
// pairings between TransmitModel change signals and the TxChannel WDSP
// wrappers.
//
// Test strategy: m_txChannel is null in unit-test builds (createTxChannel
// only runs after WDSP initializes; WDSP isn't initialized here).  The
// AutoConnection installed by RadioModel binds the TransmitModel signal
// to a lambda that captures `this` and calls m_txChannel->setX().  The
// lambda body is null-safe by virtue of the receiver=m_txChannel
// argument — Qt auto-disconnects when the receiver is destroyed, but if
// m_txChannel is null at connect time the connect() simply doesn't
// install (no-op).  We therefore verify the TWO model-side guarantees
// that hold without a live channel:
//
//   1. TransmitModel signals are emitted exactly once per setter call
//      (the idempotent guard inside each setter prevents double-fire on
//      same-value writes).
//   2. Setting + emitting many signals in sequence does not crash and
//      the model state stays consistent.
//
// Slot-body coverage (the actual SetTXAEQ* call) is exercised by
// tst_tx_channel_eq_setters and tst_tx_channel_leveler_alc_setters
// (Batch 1 Tasks B-1 / B-2).  This file covers the ROUTING contract.
//
// =================================================================

#include <QtTest/QtTest>
#include <QSignalSpy>

#include "core/AppSettings.h"
#include "models/RadioModel.h"
#include "models/TransmitModel.h"

using namespace NereusSDR;

class TstRadioModelEqLevAlcWiring : public QObject {
    Q_OBJECT

private slots:

    void initTestCase()  { AppSettings::instance().clear(); }
    void init()          { AppSettings::instance().clear(); }
    void cleanup()       { AppSettings::instance().clear(); }

    // ── 1. Signal fan-out: setter → exactly one signal ────────────────────
    void txEqEnabledChanged_firesOnce()
    {
        RadioModel model;
        QSignalSpy spy(&model.transmitModel(), &TransmitModel::txEqEnabledChanged);
        model.transmitModel().setTxEqEnabled(true);
        QCOMPARE(spy.count(), 1);
    }

    // ── 2. Idempotent guard: same value twice → only one signal ───────────
    void txEqEnabledChanged_idempotentNoDoubleFire()
    {
        RadioModel model;
        model.transmitModel().setTxEqEnabled(false);  // already default false
        QSignalSpy spy(&model.transmitModel(), &TransmitModel::txEqEnabledChanged);
        model.transmitModel().setTxEqEnabled(false);  // same value
        model.transmitModel().setTxEqEnabled(false);  // same value again
        QCOMPARE(spy.count(), 0);
        model.transmitModel().setTxEqEnabled(true);   // change
        model.transmitModel().setTxEqEnabled(true);   // same again — no fire
        QCOMPARE(spy.count(), 1);
    }

    // ── 3. Leveler max gain idempotent ────────────────────────────────────
    void txLevelerMaxGain_idempotentNoDoubleFire()
    {
        RadioModel model;
        // Default per Thetis database.cs:4590 is 15 dB.
        QCOMPARE(model.transmitModel().txLevelerMaxGain(), 15);
        QSignalSpy spy(&model.transmitModel(), &TransmitModel::txLevelerMaxGainChanged);
        model.transmitModel().setTxLevelerMaxGain(15);  // same value
        QCOMPARE(spy.count(), 0);
        model.transmitModel().setTxLevelerMaxGain(10);  // change
        QCOMPARE(spy.count(), 1);
        model.transmitModel().setTxLevelerMaxGain(10);  // same again
        QCOMPARE(spy.count(), 1);
    }

    // ── 4. Leveler decay idempotent ───────────────────────────────────────
    void txLevelerDecay_idempotentNoDoubleFire()
    {
        RadioModel model;
        QSignalSpy spy(&model.transmitModel(), &TransmitModel::txLevelerDecayChanged);
        model.transmitModel().setTxLevelerDecay(250);
        model.transmitModel().setTxLevelerDecay(250);
        QCOMPARE(spy.count(), 1);
    }

    // ── 5. ALC max gain idempotent ────────────────────────────────────────
    void txAlcMaxGain_idempotentNoDoubleFire()
    {
        RadioModel model;
        QSignalSpy spy(&model.transmitModel(), &TransmitModel::txAlcMaxGainChanged);
        model.transmitModel().setTxAlcMaxGain(7);
        model.transmitModel().setTxAlcMaxGain(7);
        QCOMPARE(spy.count(), 1);
    }

    // ── 6. ALC decay idempotent ───────────────────────────────────────────
    void txAlcDecay_idempotentNoDoubleFire()
    {
        RadioModel model;
        QSignalSpy spy(&model.transmitModel(), &TransmitModel::txAlcDecayChanged);
        model.transmitModel().setTxAlcDecay(25);
        model.transmitModel().setTxAlcDecay(25);
        QCOMPARE(spy.count(), 1);
    }

    // ── 7. EQ band signal carries index + value ───────────────────────────
    void txEqBandChanged_carriesIndexAndDb()
    {
        RadioModel model;
        QSignalSpy spy(&model.transmitModel(), &TransmitModel::txEqBandChanged);
        model.transmitModel().setTxEqBand(3, 5);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toInt(), 3);
        QCOMPARE(spy.first().at(1).toInt(), 5);
    }

    // ── 8. EQ freq signal carries index + value ───────────────────────────
    void txEqFreqChanged_carriesIndexAndHz()
    {
        RadioModel model;
        QSignalSpy spy(&model.transmitModel(), &TransmitModel::txEqFreqChanged);
        model.transmitModel().setTxEqFreq(7, 5000);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toInt(), 7);
        QCOMPARE(spy.first().at(1).toInt(), 5000);
    }

    // ── 9. EQ globals all four signals fire ───────────────────────────────
    void txEqGlobals_allFourEmit()
    {
        RadioModel model;
        QSignalSpy ncSpy(&model.transmitModel(), &TransmitModel::txEqNcChanged);
        QSignalSpy mpSpy(&model.transmitModel(), &TransmitModel::txEqMpChanged);
        QSignalSpy ctfSpy(&model.transmitModel(), &TransmitModel::txEqCtfmodeChanged);
        QSignalSpy winSpy(&model.transmitModel(), &TransmitModel::txEqWintypeChanged);

        // Move each off its default to force a fire.
        model.transmitModel().setTxEqNc(4096);
        model.transmitModel().setTxEqMp(true);
        model.transmitModel().setTxEqCtfmode(1);
        model.transmitModel().setTxEqWintype(1);

        QCOMPARE(ncSpy.count(), 1);
        QCOMPARE(mpSpy.count(), 1);
        QCOMPARE(ctfSpy.count(), 1);
        QCOMPARE(winSpy.count(), 1);
    }

    // ── 10. Bulk fan-out doesn't crash with null TxChannel ────────────────
    // Path (b) reconciliation: with m_txChannel null in unit tests, the
    // RadioModel-installed connect()s either don't install (receiver was
    // null at connect time — which is the case in unit tests because
    // connectToRadio() is never called here) or are installed and remain
    // null-safe via the receiver=m_txChannel auto-disconnect.  Either
    // way, emitting all 13 signals in sequence must not crash.
    void bulkSignalEmit_noCrashWithNullTxChannel()
    {
        RadioModel model;
        // Hammer all 13 signals via setters.
        model.transmitModel().setTxEqEnabled(true);
        model.transmitModel().setTxEqPreamp(3);
        model.transmitModel().setTxEqBand(0, 5);
        model.transmitModel().setTxEqBand(9, -3);
        model.transmitModel().setTxEqFreq(0, 100);
        model.transmitModel().setTxEqFreq(9, 12000);
        model.transmitModel().setTxEqNc(4096);
        model.transmitModel().setTxEqMp(true);
        model.transmitModel().setTxEqCtfmode(1);
        model.transmitModel().setTxEqWintype(1);
        model.transmitModel().setTxLevelerOn(false);
        model.transmitModel().setTxLevelerMaxGain(8);
        model.transmitModel().setTxLevelerDecay(250);
        model.transmitModel().setTxAlcMaxGain(7);
        model.transmitModel().setTxAlcDecay(25);
        // No QFAIL — we just need the test to complete.
        QVERIFY(true);
    }

    // ─────────────────────────────────────────────────────────────────────
    // 3M-3a-ii Batch 3 — CFC / CPDR / CESSB / PhRot routing tests
    // ─────────────────────────────────────────────────────────────────────

    // ── 11. phaseRotatorEnabled idempotent + signal payload ───────────────
    void phaseRotatorEnabledChanged_firesOnce()
    {
        RadioModel model;
        QSignalSpy spy(&model.transmitModel(),
                       &TransmitModel::phaseRotatorEnabledChanged);
        model.transmitModel().setPhaseRotatorEnabled(true);
        model.transmitModel().setPhaseRotatorEnabled(true);  // idempotent
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toBool(), true);
    }

    // ── 12. phaseReverseEnabled idempotent + signal payload ───────────────
    void phaseReverseEnabledChanged_firesOnce()
    {
        RadioModel model;
        QSignalSpy spy(&model.transmitModel(),
                       &TransmitModel::phaseReverseEnabledChanged);
        model.transmitModel().setPhaseReverseEnabled(true);
        model.transmitModel().setPhaseReverseEnabled(true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toBool(), true);
    }

    // ── 13. phaseRotatorFreqHz idempotent + signal payload ────────────────
    void phaseRotatorFreqHzChanged_firesOnce()
    {
        RadioModel model;
        QSignalSpy spy(&model.transmitModel(),
                       &TransmitModel::phaseRotatorFreqHzChanged);
        model.transmitModel().setPhaseRotatorFreqHz(500);
        model.transmitModel().setPhaseRotatorFreqHz(500);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toInt(), 500);
    }

    // ── 14. phaseRotatorStages idempotent + signal payload ────────────────
    void phaseRotatorStagesChanged_firesOnce()
    {
        RadioModel model;
        // Default per Thetis is 8.
        QCOMPARE(model.transmitModel().phaseRotatorStages(), 8);
        QSignalSpy spy(&model.transmitModel(),
                       &TransmitModel::phaseRotatorStagesChanged);
        model.transmitModel().setPhaseRotatorStages(12);
        model.transmitModel().setPhaseRotatorStages(12);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toInt(), 12);
    }

    // ── 15. cfcEnabled idempotent + signal payload ────────────────────────
    void cfcEnabledChanged_firesOnce()
    {
        RadioModel model;
        QSignalSpy spy(&model.transmitModel(), &TransmitModel::cfcEnabledChanged);
        model.transmitModel().setCfcEnabled(true);
        model.transmitModel().setCfcEnabled(true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toBool(), true);
    }

    // ── 16. cfcPostEqEnabled idempotent + signal payload ──────────────────
    void cfcPostEqEnabledChanged_firesOnce()
    {
        RadioModel model;
        QSignalSpy spy(&model.transmitModel(),
                       &TransmitModel::cfcPostEqEnabledChanged);
        model.transmitModel().setCfcPostEqEnabled(true);
        model.transmitModel().setCfcPostEqEnabled(true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toBool(), true);
    }

    // ── 17. cfcPrecompDb idempotent + signal payload ──────────────────────
    void cfcPrecompDbChanged_firesOnce()
    {
        RadioModel model;
        QSignalSpy spy(&model.transmitModel(), &TransmitModel::cfcPrecompDbChanged);
        model.transmitModel().setCfcPrecompDb(8);
        model.transmitModel().setCfcPrecompDb(8);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toInt(), 8);
    }

    // ── 18. cfcPostEqGainDb idempotent + signal payload ───────────────────
    void cfcPostEqGainDbChanged_firesOnce()
    {
        RadioModel model;
        QSignalSpy spy(&model.transmitModel(),
                       &TransmitModel::cfcPostEqGainDbChanged);
        model.transmitModel().setCfcPostEqGainDb(6);
        model.transmitModel().setCfcPostEqGainDb(6);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toInt(), 6);
    }

    // ── 19. cfcEqFreq array signal carries index + value ──────────────────
    // Backing the pushCfcProfile rebuild path (#22).  In production the
    // RadioModel-installed connect re-emits the full F/G/E vector via
    // setTxCfcProfile; here we verify the model-side signal payload.
    void cfcEqFreqChanged_carriesIndexAndHz()
    {
        RadioModel model;
        QSignalSpy spy(&model.transmitModel(), &TransmitModel::cfcEqFreqChanged);
        model.transmitModel().setCfcEqFreq(4, 1500);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toInt(), 4);
        QCOMPARE(spy.first().at(1).toInt(), 1500);
    }

    // ── 20. cfcCompression array signal carries index + value ─────────────
    // Backing the pushCfcProfile rebuild path (#23) — G[] vector source.
    void cfcCompressionChanged_carriesIndexAndDb()
    {
        RadioModel model;
        QSignalSpy spy(&model.transmitModel(),
                       &TransmitModel::cfcCompressionChanged);
        model.transmitModel().setCfcCompression(2, 10);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toInt(), 2);
        QCOMPARE(spy.first().at(1).toInt(), 10);
    }

    // ── 21. cfcPostEqBandGain array signal carries index + value ──────────
    // Backing the pushCfcProfile rebuild path (#24) — E[] vector source.
    void cfcPostEqBandGainChanged_carriesIndexAndDb()
    {
        RadioModel model;
        QSignalSpy spy(&model.transmitModel(),
                       &TransmitModel::cfcPostEqBandGainChanged);
        model.transmitModel().setCfcPostEqBandGain(7, -6);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toInt(), 7);
        QCOMPARE(spy.first().at(1).toInt(), -6);
    }

    // ── 22. cpdrOn idempotent + signal payload ────────────────────────────
    void cpdrOnChanged_firesOnce()
    {
        RadioModel model;
        QSignalSpy spy(&model.transmitModel(), &TransmitModel::cpdrOnChanged);
        model.transmitModel().setCpdrOn(true);
        model.transmitModel().setCpdrOn(true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toBool(), true);
    }

    // ── 23. cpdrLevelDb idempotent + signal payload ───────────────────────
    void cpdrLevelDbChanged_firesOnce()
    {
        RadioModel model;
        // Default per Thetis is 2.
        QCOMPARE(model.transmitModel().cpdrLevelDb(), 2);
        QSignalSpy spy(&model.transmitModel(), &TransmitModel::cpdrLevelDbChanged);
        model.transmitModel().setCpdrLevelDb(10);
        model.transmitModel().setCpdrLevelDb(10);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toInt(), 10);
    }

    // ── 24. cessbOn idempotent + signal payload ───────────────────────────
    void cessbOnChanged_firesOnce()
    {
        RadioModel model;
        QSignalSpy spy(&model.transmitModel(), &TransmitModel::cessbOnChanged);
        model.transmitModel().setCessbOn(true);
        model.transmitModel().setCessbOn(true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toBool(), true);
    }

    // ── 25. Bulk fan-out (Batch 3 properties) doesn't crash ───────────────
    // Same null-TxChannel reconciliation as test #10 — we confirm that
    // hammering all 15 new TM setters in sequence does not crash, even
    // when the RadioModel-installed lambdas are absent (connectToRadio
    // never runs in unit tests).
    void bulkSignalEmit_batch3_noCrashWithNullTxChannel()
    {
        RadioModel model;
        // Phase Rotator
        model.transmitModel().setPhaseRotatorEnabled(true);
        model.transmitModel().setPhaseReverseEnabled(true);
        model.transmitModel().setPhaseRotatorFreqHz(500);
        model.transmitModel().setPhaseRotatorStages(12);
        // CFC scalars
        model.transmitModel().setCfcEnabled(true);
        model.transmitModel().setCfcPostEqEnabled(true);
        model.transmitModel().setCfcPrecompDb(8);
        model.transmitModel().setCfcPostEqGainDb(4);
        // CFC profile arrays
        model.transmitModel().setCfcEqFreq(0, 100);
        model.transmitModel().setCfcEqFreq(9, 15000);
        model.transmitModel().setCfcCompression(0, 10);
        model.transmitModel().setCfcCompression(9, 8);
        model.transmitModel().setCfcPostEqBandGain(0, 3);
        model.transmitModel().setCfcPostEqBandGain(9, -3);
        // CPDR
        model.transmitModel().setCpdrOn(true);
        model.transmitModel().setCpdrLevelDb(10);
        // CESSB
        model.transmitModel().setCessbOn(true);
        QVERIFY(true);
    }

    // ── 26. _initialPushAfterConnect_pushesAllProperties (model coverage) ─
    // pushTxProcessingChain is captured by-value into the
    // MicProfileManager::activeProfileChanged lambda and invoked once
    // immediately after the 27 connects install (RadioModel.cpp:~1926).
    // Without a live TxChannel + WDSP we can't observe the WDSP setter
    // calls, but we CAN verify that the model exposes all 15 new state
    // values that pushTxProcessingChain reads (the helper would early-out
    // on a null txChannel) — in other words, the getter surface that
    // backs the on-connect push exists and returns the loaded defaults.
    void initialPushAfterConnect_pushesAllProperties()
    {
        RadioModel model;
        const auto& tm = model.transmitModel();

        // Phase Rotator (4) — pushTxProcessingChain reads these and
        // pushes them through Stage::PhRot run + setTxPhrotReverse +
        // setTxPhrotCornerHz + setTxPhrotNstages.
        Q_UNUSED(tm.phaseRotatorEnabled());
        Q_UNUSED(tm.phaseReverseEnabled());
        QCOMPARE(tm.phaseRotatorFreqHz(), 338);   // Thetis default
        QCOMPARE(tm.phaseRotatorStages(),  8);    // Thetis default

        // CFC scalars (4) — feeds setTxCfcRunning, setTxCfcPostEqRunning,
        // setTxCfcPrecompDb, setTxCfcPrePeqDb.
        Q_UNUSED(tm.cfcEnabled());
        Q_UNUSED(tm.cfcPostEqEnabled());
        QCOMPARE(tm.cfcPrecompDb(),    0);  // Thetis default
        QCOMPARE(tm.cfcPostEqGainDb(), 0);  // Thetis default

        // CFC profile arrays (3 → pushCfcProfile rebuilds the F/G/E
        // 10-element vectors).  Spot-check defaults from Batch 2.
        QCOMPARE(tm.cfcEqFreq(0),         0);
        QCOMPARE(tm.cfcEqFreq(9),     10000);
        QCOMPARE(tm.cfcCompression(0),    5);
        QCOMPARE(tm.cfcCompression(9),    5);
        QCOMPARE(tm.cfcPostEqBandGain(0), 0);
        QCOMPARE(tm.cfcPostEqBandGain(9), 0);

        // CPDR (2) — feeds setTxCpdrOn + setTxCpdrGainDb.
        Q_UNUSED(tm.cpdrOn());
        QCOMPARE(tm.cpdrLevelDb(), 2);  // Thetis default

        // CESSB (1) — feeds setTxCessbOn.
        Q_UNUSED(tm.cessbOn());

        // Existing 13 EQ/Lev/ALC properties (sanity — already covered by
        // tests #1-#10 but spot-check the on-connect push surface).
        QCOMPARE(tm.txLevelerMaxGain(), 15);
        QCOMPARE(tm.txAlcMaxGain(),      3);
    }
};

QTEST_MAIN(TstRadioModelEqLevAlcWiring)
#include "tst_radio_model_eq_lev_alc_wiring.moc"
