// =================================================================
// tests/tst_mox_controller_basic.cpp  (NereusSDR)
// =================================================================
//
// NereusSDR-original test. No Thetis port at this layer.
// =================================================================
//
// Modification history (NereusSDR):
//   2026-04-25 — Original test for NereusSDR by J.J. Boyd (KG4VCF),
//                 with AI-assisted implementation via Anthropic Claude Code.
//                 Task: Phase 3M-1a Task B.2 — MoxController skeleton
//                 basic state-transition tests + Codex P2 ordering lock.
// =================================================================

// no-port-check: NereusSDR-original test file.

#include <QtTest/QtTest>
#include <QSignalSpy>

#include "core/MoxController.h"

using namespace NereusSDR;

// ---------------------------------------------------------------------------
// TestMoxControllerBasic — state transitions, no timers (timers come in B.3)
// ---------------------------------------------------------------------------

// Subclass that counts runMoxSafetyEffects calls so the Codex P2 ordering
// invariant can be verified mechanically:
//   "safety effects run on EVERY setMox call, including idempotent ones."
class SpyMoxController : public MoxController {
public:
    explicit SpyMoxController(QObject* parent = nullptr)
        : MoxController(parent)
    {
    }

    int safetyCallCount() const { return m_safetyCallCount; }
    bool lastSafetyArg()  const { return m_lastSafetyArg; }

protected:
    void runMoxSafetyEffects(bool newMox) override
    {
        ++m_safetyCallCount;
        m_lastSafetyArg = newMox;
        MoxController::runMoxSafetyEffects(newMox); // call base (no-op in B.2)
    }

private:
    int  m_safetyCallCount{0};
    bool m_lastSafetyArg{false};
};

// ---------------------------------------------------------------------------

class TestMoxControllerBasic : public QObject {
    Q_OBJECT

private slots:

    // ── Default state ──────────────────────────────────────────────────────

    void defaultState_isMoxFalse()
    {
        MoxController ctrl;
        QVERIFY(!ctrl.isMox());
    }

    void defaultState_isRx()
    {
        MoxController ctrl;
        QCOMPARE(ctrl.state(), MoxState::Rx);
    }

    void defaultState_pttModeIsNone()
    {
        MoxController ctrl;
        QCOMPARE(ctrl.pttMode(), PttMode::None);
    }

    // ── setMox(true): initial TX engage ───────────────────────────────────

    void setMoxTrue_isMoxBecomesTrue()
    {
        MoxController ctrl;
        ctrl.setMox(true);
        QVERIFY(ctrl.isMox());
    }

    void setMoxTrue_stateBecomesToTx()
    {
        MoxController ctrl;
        ctrl.setMox(true);
        QCOMPARE(ctrl.state(), MoxState::Tx);
    }

    void setMoxTrue_emitsMoxStateChangedTrue_exactlyOnce()
    {
        MoxController ctrl;
        QSignalSpy spy(&ctrl, &MoxController::moxStateChanged);
        ctrl.setMox(true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), true);
    }

    // ── setMox(false) after setMox(true): TX release ──────────────────────

    void setMoxFalse_afterTrue_isMoxBecomesFalse()
    {
        MoxController ctrl;
        ctrl.setMox(true);
        ctrl.setMox(false);
        QVERIFY(!ctrl.isMox());
    }

    void setMoxFalse_afterTrue_stateBecomesRx()
    {
        MoxController ctrl;
        ctrl.setMox(true);
        ctrl.setMox(false);
        QCOMPARE(ctrl.state(), MoxState::Rx);
    }

    void setMoxFalse_afterTrue_emitsMoxStateChangedFalse_exactlyOnce()
    {
        MoxController ctrl;
        ctrl.setMox(true);
        QSignalSpy spy(&ctrl, &MoxController::moxStateChanged);
        ctrl.setMox(false);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), false);
    }

    // ── Idempotent guard: repeated setMox(true) emits only once ──────────

    void setMoxTrue_calledTwice_emitsOnlyOnce()
    {
        MoxController ctrl;
        QSignalSpy spy(&ctrl, &MoxController::moxStateChanged);
        ctrl.setMox(true);
        ctrl.setMox(true); // second call — idempotent, must not re-emit
        QCOMPARE(spy.count(), 1);
    }

    // ── stateChanged signal traces Rx → Tx → Rx ───────────────────────────

    void stateChanged_tracksRxTxRxSequence()
    {
        MoxController ctrl;
        QSignalSpy spy(&ctrl, &MoxController::stateChanged);

        ctrl.setMox(true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).value<MoxState>(), MoxState::Tx);

        ctrl.setMox(false);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(0).value<MoxState>(), MoxState::Rx);
    }

    // ── setPttMode: idempotent guard + signal ─────────────────────────────

    void setPttMode_manual_emitsPttModeChanged()
    {
        MoxController ctrl;
        QSignalSpy spy(&ctrl, &MoxController::pttModeChanged);
        ctrl.setPttMode(PttMode::Manual);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).value<PttMode>(), PttMode::Manual);
    }

    void setPttMode_sameModeTwice_emitsOnlyOnce()
    {
        MoxController ctrl;
        QSignalSpy spy(&ctrl, &MoxController::pttModeChanged);
        ctrl.setPttMode(PttMode::Manual);
        ctrl.setPttMode(PttMode::Manual); // idempotent — must not re-emit
        QCOMPARE(spy.count(), 1);
    }

    void setPttMode_roundTrips()
    {
        MoxController ctrl;
        ctrl.setPttMode(PttMode::Mic);
        QCOMPARE(ctrl.pttMode(), PttMode::Mic);
    }

    // ── Codex P2 verification: runMoxSafetyEffects fires on EVERY call ────
    //
    // Critical invariant: safety effects run BEFORE the idempotent guard,
    // so they cannot be skipped — not even on a repeated (idempotent) call.
    // This test locks in the ordering so F.1 cannot accidentally regress it.

    void codexP2_safetyEffect_firesOnEveryCall_includingIdempotent()
    {
        SpyMoxController ctrl;

        // First call: real transition (Rx → Tx).
        ctrl.setMox(true);
        QCOMPARE(ctrl.safetyCallCount(), 1);
        QVERIFY(ctrl.lastSafetyArg());

        // Second call: idempotent (Tx → Tx, no state change).
        // Safety effect MUST still fire even though there is no transition.
        ctrl.setMox(true);
        QCOMPARE(ctrl.safetyCallCount(), 2);
        QVERIFY(ctrl.lastSafetyArg());

        // Third call: real transition (Tx → Rx).
        ctrl.setMox(false);
        QCOMPARE(ctrl.safetyCallCount(), 3);
        QVERIFY(!ctrl.lastSafetyArg());
    }

    void codexP2_safetyEffect_firesBefore_stateTransition()
    {
        // Verify that when runMoxSafetyEffects is called, m_mox has NOT
        // yet been updated (it still reflects the old state).
        // This confirms the "safety-effect-THEN-commit" ordering in setMox.

        // We do this by overriding runMoxSafetyEffects in a subclass that
        // captures isMox() at call time.
        class OrderProbeController : public MoxController {
        public:
            bool moxAtSafetyCallTime{false};
        protected:
            void runMoxSafetyEffects(bool /*newMox*/) override
            {
                moxAtSafetyCallTime = isMox(); // capture current committed state
            }
        };

        OrderProbeController ctrl;

        // Before setMox(true): isMox() == false.
        // At runMoxSafetyEffects call time (before commit), isMox() must
        // still be false — the new value has not been written to m_mox yet.
        ctrl.setMox(true);
        QVERIFY(!ctrl.moxAtSafetyCallTime); // safety fired while m_mox was still false

        // Before setMox(false): isMox() == true.
        ctrl.setMox(false);
        QVERIFY(ctrl.moxAtSafetyCallTime); // safety fired while m_mox was still true
    }

    // ── 7 MoxState values all exist and are distinct ──────────────────────

    void moxState_allSevenValuesDistinct()
    {
        // Verify each enumerator can be constructed and compared.
        QList<MoxState> values = {
            MoxState::Rx,
            MoxState::RxToTxRfDelay,
            MoxState::RxToTxMoxDelay,
            MoxState::Tx,
            MoxState::TxToRxKeyUpDelay,
            MoxState::TxToRxBreakIn,
            MoxState::TxToRxFlush,
        };
        QCOMPARE(values.size(), 7);
        // All must be pairwise distinct (set-uniqueness check).
        for (int i = 0; i < values.size(); ++i) {
            for (int j = i + 1; j < values.size(); ++j) {
                QVERIFY2(values[i] != values[j],
                         "duplicate MoxState enumerator value detected");
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestMoxControllerBasic)
#include "tst_mox_controller_basic.moc"
