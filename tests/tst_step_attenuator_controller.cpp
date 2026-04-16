// tst_step_attenuator_controller.cpp
//
// StepAttenuatorController unit tests — ADC overload hysteresis counter
// state machine. Verifies the per-ADC counter increment/decrement logic,
// level capping, severity transitions (None→Yellow→Red and back), and
// multi-ADC independence.
//
// The tick() method is public for testability; tests call it directly
// rather than waiting on QTimer, giving deterministic cycle control.
//
// Porting from Thetis console.cs:21359-21382 per-ADC overload level logic.

#include <QtTest/QtTest>
#include <QSignalSpy>

#include "core/StepAttenuatorController.h"

using namespace NereusSDR;

class TestStepAttenuatorController : public QObject {
    Q_OBJECT
private slots:

    void singleOverflow_emitsYellow()
    {
        // A single ADC overflow event followed by one tick should raise
        // the counter from 0→1, which is above 0 → Yellow severity.
        // From Thetis console.cs:21378: level > 0 triggers warning text.
        StepAttenuatorController ctrl;
        // Stop the internal timer so only manual tick() calls drive state.
        ctrl.setTickTimerEnabled(false);

        QSignalSpy spy(&ctrl, &StepAttenuatorController::overloadStatusChanged);
        qRegisterMetaType<NereusSDR::OverloadLevel>();

        ctrl.onAdcOverflow(0);
        ctrl.tick();

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toInt(), 0);  // ADC index
        QCOMPARE(spy.first().at(1).value<OverloadLevel>(), OverloadLevel::Yellow);
        QCOMPARE(ctrl.overloadCounter(0), 1);
    }

    void noOverflow_levelDecays()
    {
        // After reaching Yellow, if no further overflow events arrive,
        // each tick decrements the counter by 1 until it reaches 0 (None).
        // From Thetis console.cs:21373-21375.
        StepAttenuatorController ctrl;
        ctrl.setTickTimerEnabled(false);

        qRegisterMetaType<NereusSDR::OverloadLevel>();
        QSignalSpy spy(&ctrl, &StepAttenuatorController::overloadStatusChanged);

        // Raise to Yellow.
        ctrl.onAdcOverflow(0);
        ctrl.tick();
        QCOMPARE(ctrl.overloadCounter(0), 1);
        QCOMPARE(spy.count(), 1);  // None→Yellow

        // One tick with no overflow → counter 1→0 → back to None.
        ctrl.tick();
        QCOMPARE(ctrl.overloadCounter(0), 0);
        QCOMPARE(spy.count(), 2);  // Yellow→None
        QCOMPARE(spy.last().at(1).value<OverloadLevel>(), OverloadLevel::None);
    }

    void sustainedOverflow_escalatesToRed()
    {
        // Sustained overflows across >3 ticks push the level past the
        // red threshold (>3). From Thetis console.cs:21369: red when > 3.
        StepAttenuatorController ctrl;
        ctrl.setTickTimerEnabled(false);

        qRegisterMetaType<NereusSDR::OverloadLevel>();
        QSignalSpy spy(&ctrl, &StepAttenuatorController::overloadStatusChanged);

        // 4 ticks with overflow: level goes 0→1→2→3→4.
        // Yellow emitted at tick 1 (level 1), Red at tick 4 (level 4 > 3).
        for (int i = 0; i < 4; ++i) {
            ctrl.onAdcOverflow(0);
            ctrl.tick();
        }

        QCOMPARE(ctrl.overloadCounter(0), 4);
        QCOMPARE(ctrl.overloadLevel(0), OverloadLevel::Red);

        // Should have emitted Yellow on first tick, Red on fourth.
        QVERIFY(spy.count() >= 2);
        QCOMPARE(spy.first().at(1).value<OverloadLevel>(), OverloadLevel::Yellow);
        QCOMPARE(spy.last().at(1).value<OverloadLevel>(), OverloadLevel::Red);
    }

    void levelCapsAtFive()
    {
        // From Thetis console.cs:21366 — counter caps at 5 (despite the
        // comment saying 10). Verify it doesn't exceed kMaxOverloadLevel.
        StepAttenuatorController ctrl;
        ctrl.setTickTimerEnabled(false);

        qRegisterMetaType<NereusSDR::OverloadLevel>();

        // 10 ticks with continuous overflow.
        for (int i = 0; i < 10; ++i) {
            ctrl.onAdcOverflow(0);
            ctrl.tick();
        }

        QCOMPARE(ctrl.overloadCounter(0), 5);
    }

    void redDowngradesToYellow()
    {
        // After reaching Red (level > 3), decay without new overflows
        // should transition Red→Yellow when level drops to 3 (which is
        // not > 3, so Yellow), then Yellow→None when level reaches 0.
        StepAttenuatorController ctrl;
        ctrl.setTickTimerEnabled(false);

        qRegisterMetaType<NereusSDR::OverloadLevel>();
        QSignalSpy spy(&ctrl, &StepAttenuatorController::overloadStatusChanged);

        // Pump to Red (level 5 = capped).
        for (int i = 0; i < 6; ++i) {
            ctrl.onAdcOverflow(0);
            ctrl.tick();
        }
        QCOMPARE(ctrl.overloadCounter(0), 5);
        QCOMPARE(ctrl.overloadLevel(0), OverloadLevel::Red);
        spy.clear();

        // Decay: 5→4→3. At level 3, severity is Yellow (not > 3).
        ctrl.tick();  // 5→4, still Red
        QCOMPARE(ctrl.overloadLevel(0), OverloadLevel::Red);

        ctrl.tick();  // 4→3, now Yellow
        QCOMPARE(ctrl.overloadLevel(0), OverloadLevel::Yellow);
        QVERIFY(spy.count() >= 1);

        // Find the Yellow transition.
        bool foundYellow = false;
        for (const auto& call : spy) {
            if (call.at(1).value<OverloadLevel>() == OverloadLevel::Yellow) {
                foundYellow = true;
                break;
            }
        }
        QVERIFY2(foundYellow, "Expected Red→Yellow transition during decay");
    }

    void multipleAdcsIndependent()
    {
        // Each ADC has its own independent counter. Overflow on ADC 0
        // should not affect ADC 1 or ADC 2.
        StepAttenuatorController ctrl;
        ctrl.setTickTimerEnabled(false);

        qRegisterMetaType<NereusSDR::OverloadLevel>();
        QSignalSpy spy(&ctrl, &StepAttenuatorController::overloadStatusChanged);

        // Only ADC 1 overflows.
        ctrl.onAdcOverflow(1);
        ctrl.tick();

        QCOMPARE(ctrl.overloadCounter(0), 0);
        QCOMPARE(ctrl.overloadCounter(1), 1);
        QCOMPARE(ctrl.overloadCounter(2), 0);

        QCOMPARE(ctrl.overloadLevel(0), OverloadLevel::None);
        QCOMPARE(ctrl.overloadLevel(1), OverloadLevel::Yellow);
        QCOMPARE(ctrl.overloadLevel(2), OverloadLevel::None);

        // Signal should only reference ADC 1.
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toInt(), 1);
    }
};

QTEST_MAIN(TestStepAttenuatorController)
#include "tst_step_attenuator_controller.moc"
