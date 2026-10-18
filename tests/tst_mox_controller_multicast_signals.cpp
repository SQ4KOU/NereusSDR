// =================================================================
// tests/tst_mox_controller_multicast_signals.cpp  (NereusSDR)
// =================================================================
//
// NereusSDR-original test. No Thetis port at this layer.
//
// Verifies the multicast Pre/Post MOX state-change signals added in
// Phase 3M-1c chunks C.2 / C.3 / C.4:
//   moxChanging(int rx, bool oldMox, bool newMox)  — Pre signal
//   moxChanged (int rx, bool oldMox, bool newMox)  — Post signal
//
// Source citations:
//   moxChanging emit point: Thetis console.cs:29324 [v2.10.3.13]
//     MoxPreChangeHandlers?.Invoke(rx2_enabled && VFOBTX ? 2 : 1, _mox,
//                                  chkMOX.Checked); // MW0LGE_21k8
//   moxChanged Post emit point: Thetis console.cs:29677 [v2.10.3.13]
//     if (bOldMox != tx) MoxChangeHandlers?.Invoke(rx2_enabled && VFOBTX
//                                  ? 2 : 1, bOldMox, tx); // MW0LGE_21a
//
// rx-arg semantic (C.4):
//   The argument is the receiver index that owns the TX path:
//     rx == 2  iff (rx2_enabled && vfobTx) — TX comes off VFO-B routed via RX2
//     rx == 1  otherwise (RX2 alone without VFOBTX still routes off VFO-A)
//
// The existing 1-arg moxStateChanged(bool) signal is NOT replaced by C.3 —
// the 3-arg moxChanged is an additive Post overload for future Pre/Post
// observer plug-in points (PS form, TCI server, MeterPoller, recorder…).
// =================================================================

// no-port-check: NereusSDR-original test file.

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QCoreApplication>
#include <QStringList>

#include "core/MoxController.h"

using namespace NereusSDR;

// ---------------------------------------------------------------------------
// Helper: drain TX→RX walk (two 0ms timers — keyUpDelay + pttOutDelay).
// ---------------------------------------------------------------------------
static void drainTxToRxWalk()
{
    QCoreApplication::processEvents(); // keyUpDelayTimer → TxToRxFlush
    QCoreApplication::processEvents(); // pttOutDelayTimer → Rx
}

// ---------------------------------------------------------------------------

class TestMoxControllerMulticastSignals : public QObject {
    Q_OBJECT

private slots:

    // ════════════════════════════════════════════════════════════════════════
    // §1 — Pre/Post emit semantics
    // ════════════════════════════════════════════════════════════════════════

    // moxChanging fires BEFORE m_mox commit: when the slot runs, isMox()
    // must still report the OLD value (false → about to become true).
    // Capture the observed state inside the slot and assert outside.
    void moxChanging_firesOnce_pre_state_change()
    {
        MoxController ctrl;
        ctrl.setTimerIntervals(0, 0, 0, 0, 0, 0);

        QSignalSpy spy(&ctrl, &MoxController::moxChanging);

        // Capture isMox() at the moment moxChanging fires.
        bool observedOldMox = true;  // wrong default to catch missed assignment
        bool slotFired = false;
        connect(&ctrl, &MoxController::moxChanging,
                this, [&](int /*rx*/, bool /*oldMox*/, bool /*newMox*/) {
                    observedOldMox = ctrl.isMox();
                    slotFired = true;
                });

        ctrl.setMox(true);
        QCoreApplication::processEvents();

        QCOMPARE(spy.count(), 1);
        QVERIFY(slotFired);
        // m_mox should still have been false when the slot ran (Pre).
        QCOMPARE(observedOldMox, false);

        // And the signal payload itself: oldMox=false, newMox=true.
        QCOMPARE(spy.at(0).at(1).toBool(), false);  // oldMox
        QCOMPARE(spy.at(0).at(2).toBool(), true);   // newMox
    }

    // moxChanged Post fires after the full timer walk completes, with the
    // direction-correct oldMox / newMox payload.
    void moxChanged_firesOnce_post_state_change()
    {
        MoxController ctrl;
        ctrl.setTimerIntervals(0, 0, 0, 0, 0, 0);

        QSignalSpy spy(&ctrl, &MoxController::moxChanged);

        ctrl.setMox(true);
        QCoreApplication::processEvents();  // rfDelayTimer → Tx

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(1).toBool(), false);  // oldMox = false (was Rx)
        QCOMPARE(spy.at(0).at(2).toBool(), true);   // newMox = true (now Tx)
    }

    // Order: moxChanging (Pre) must fire before moxChanged (Post) for any
    // real RX→TX cycle.  Use a string-log recorder to capture arrival order.
    void pre_then_post_ordering()
    {
        MoxController ctrl;
        ctrl.setTimerIntervals(0, 0, 0, 0, 0, 0);

        QStringList log;
        connect(&ctrl, &MoxController::moxChanging,
                this, [&](int, bool, bool) { log.append(QStringLiteral("pre")); });
        connect(&ctrl, &MoxController::moxChanged,
                this, [&](int, bool, bool) { log.append(QStringLiteral("post")); });

        ctrl.setMox(true);
        QCoreApplication::processEvents();  // drive RX→TX walk to completion

        QCOMPARE(log.size(), 2);
        QCOMPARE(log.at(0), QStringLiteral("pre"));
        QCOMPARE(log.at(1), QStringLiteral("post"));
    }

    // ════════════════════════════════════════════════════════════════════════
    // §2 — rx argument truth table (C.4 semantic)
    //
    //   rx2_enabled  vfobTx   →  rx
    //   ─────────── ────── ─── ──
    //      false     false       1
    //       true     false       1   ← critical: RX2 alone doesn't route TX
    //      false      true       1
    //       true      true       2
    // ════════════════════════════════════════════════════════════════════════

    void rx_argument_default_is_1()
    {
        MoxController ctrl;
        ctrl.setTimerIntervals(0, 0, 0, 0, 0, 0);
        // defaults: rx2Enabled=false, vfobTx=false

        QSignalSpy preSpy(&ctrl, &MoxController::moxChanging);
        QSignalSpy postSpy(&ctrl, &MoxController::moxChanged);

        ctrl.setMox(true);
        QCoreApplication::processEvents();

        QCOMPARE(preSpy.count(), 1);
        QCOMPARE(preSpy.at(0).at(0).toInt(), 1);
        QCOMPARE(postSpy.count(), 1);
        QCOMPARE(postSpy.at(0).at(0).toInt(), 1);
    }

    // RX2 alone without VFOBTX → TX still comes off VFO-A → rx == 1.
    // (Critical: this is the case the resume prompt called out specifically.)
    void rx_argument_with_only_rx2_enabled_is_1()
    {
        MoxController ctrl;
        ctrl.setTimerIntervals(0, 0, 0, 0, 0, 0);
        ctrl.setRx2Enabled(true);
        ctrl.setVfobTx(false);

        QSignalSpy preSpy(&ctrl, &MoxController::moxChanging);
        QSignalSpy postSpy(&ctrl, &MoxController::moxChanged);

        ctrl.setMox(true);
        QCoreApplication::processEvents();

        QCOMPARE(preSpy.count(), 1);
        QCOMPARE(preSpy.at(0).at(0).toInt(), 1);
        QCOMPARE(postSpy.count(), 1);
        QCOMPARE(postSpy.at(0).at(0).toInt(), 1);
    }

    // VFOBTX without rx2_enabled → no RX2 wired, so rx == 1.
    void rx_argument_with_only_vfobTx_is_1()
    {
        MoxController ctrl;
        ctrl.setTimerIntervals(0, 0, 0, 0, 0, 0);
        ctrl.setRx2Enabled(false);
        ctrl.setVfobTx(true);

        QSignalSpy preSpy(&ctrl, &MoxController::moxChanging);
        QSignalSpy postSpy(&ctrl, &MoxController::moxChanged);

        ctrl.setMox(true);
        QCoreApplication::processEvents();

        QCOMPARE(preSpy.count(), 1);
        QCOMPARE(preSpy.at(0).at(0).toInt(), 1);
        QCOMPARE(postSpy.count(), 1);
        QCOMPARE(postSpy.at(0).at(0).toInt(), 1);
    }

    // Both rx2_enabled AND vfobTx → TX routes through RX2 → rx == 2.
    void rx_argument_with_both_is_2()
    {
        MoxController ctrl;
        ctrl.setTimerIntervals(0, 0, 0, 0, 0, 0);
        ctrl.setRx2Enabled(true);
        ctrl.setVfobTx(true);

        QSignalSpy preSpy(&ctrl, &MoxController::moxChanging);
        QSignalSpy postSpy(&ctrl, &MoxController::moxChanged);

        ctrl.setMox(true);
        QCoreApplication::processEvents();

        QCOMPARE(preSpy.count(), 1);
        QCOMPARE(preSpy.at(0).at(0).toInt(), 2);
        QCOMPARE(postSpy.count(), 1);
        QCOMPARE(postSpy.at(0).at(0).toInt(), 2);
    }

    // ════════════════════════════════════════════════════════════════════════
    // §3 — Idempotent guard
    //
    // setMox(false) on a fresh controller (already RX) is idempotent — the
    // existing guard at MoxController::setMox returns early.  Neither Pre
    // nor Post should fire.
    // ════════════════════════════════════════════════════════════════════════

    void idempotent_no_pre_no_post_when_setMox_same_value()
    {
        MoxController ctrl;
        ctrl.setTimerIntervals(0, 0, 0, 0, 0, 0);
        // Fresh controller is already in Rx with m_mox=false.

        QSignalSpy preSpy(&ctrl, &MoxController::moxChanging);
        QSignalSpy postSpy(&ctrl, &MoxController::moxChanged);

        ctrl.setMox(false);  // idempotent — no real transition
        QCoreApplication::processEvents();

        QCOMPARE(preSpy.count(), 0);
        QCOMPARE(postSpy.count(), 0);
    }

    // ════════════════════════════════════════════════════════════════════════
    // §4 — TX→RX direction (oldMox=true, newMox=false)
    //
    // Bonus coverage: verify the Post signal payload on the reverse
    // direction matches the bOldMox stack capture from Thetis.
    // ════════════════════════════════════════════════════════════════════════

    void txToRx_post_carries_correct_oldMox_newMox()
    {
        MoxController ctrl;
        ctrl.setTimerIntervals(0, 0, 0, 0, 0, 0);

        // Drive into TX first.
        ctrl.setMox(true);
        QCoreApplication::processEvents();

        QSignalSpy postSpy(&ctrl, &MoxController::moxChanged);

        // Now toggle back to RX.
        ctrl.setMox(false);
        drainTxToRxWalk();

        QCOMPARE(postSpy.count(), 1);
        QCOMPARE(postSpy.at(0).at(1).toBool(), true);   // oldMox = true (was Tx)
        QCOMPARE(postSpy.at(0).at(2).toBool(), false);  // newMox = false (now Rx)
    }
};

QTEST_MAIN(TestMoxControllerMulticastSignals)
#include "tst_mox_controller_multicast_signals.moc"
