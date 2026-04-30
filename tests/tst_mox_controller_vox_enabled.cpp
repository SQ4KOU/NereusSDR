// =================================================================
// tests/tst_mox_controller_vox_enabled.cpp  (NereusSDR)
// =================================================================
//
// NereusSDR-original test. No Thetis logic is ported in this test
// file. The test exercises:
//   - MoxController::setVoxEnabled(bool)   — H.1 Phase 3M-1b
//   - MoxController::onModeChanged(DSPMode) — H.1 Phase 3M-1b
//   - MoxController::voxRunRequested(bool) — H.1 Phase 3M-1b signal
//
// Source references (for traceability):
//   Thetis Project Files/Source/Console/cmaster.cs:1039-1052 [v2.10.3.13]
//     — CMSetTXAVoxRun: voice-family mode-gate + SetDEXPRunVox callsite.
//   pre-code review §3.2 — voice-family definition and idempotency contract.
//
// Voice family (8 modes): LSB, USB, DSB, AM, SAM, FM, DIGL, DIGU.
// Excluded (4 modes):     CWL, CWU, SPEC, DRM.
// =================================================================

// no-port-check: NereusSDR-original test file — no upstream Thetis port.

#include <QtTest/QtTest>
#include <QSignalSpy>

#include "core/MoxController.h"
#include "core/WdspTypes.h"

using namespace NereusSDR;

class TestMoxControllerVoxEnabled : public QObject {
    Q_OBJECT

private slots:

    // ════════════════════════════════════════════════════════════════════════
    // §1 — Default state
    //
    // Fresh MoxController: m_voxEnabled=false, m_currentMode=USB.
    // Gated = false && isVoiceMode(USB) = false. m_lastVoxRunGated is
    // initialised to false so nothing to emit — no spurious signal at startup.
    // ════════════════════════════════════════════════════════════════════════

    void defaultState_noSpontaneousEmit()
    {
        // Construct MoxController; voxRunRequested must not fire without
        // any input.  No processEvents needed — emission is synchronous.
        MoxController ctrl;
        QSignalSpy spy(&ctrl, &MoxController::voxRunRequested);
        // No trigger — just verify the spy is empty.
        QCOMPARE(spy.count(), 0);
    }

    // ════════════════════════════════════════════════════════════════════════
    // §2 — Voice mode + VOX on → emits voxRunRequested(true)
    // ════════════════════════════════════════════════════════════════════════

    void lsbMode_voxOn_emitsTrue()
    {
        // From Thetis cmaster.cs:1039-1052 [v2.10.3.13]:
        //   mode == DSPMode.LSB → run = true when VOXEnabled
        MoxController ctrl;
        ctrl.onModeChanged(DSPMode::LSB);

        QSignalSpy spy(&ctrl, &MoxController::voxRunRequested);
        ctrl.setVoxEnabled(true);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), true);
    }

    // ════════════════════════════════════════════════════════════════════════
    // §3 — Voice mode + VOX off (after on) → emits voxRunRequested(false)
    // ════════════════════════════════════════════════════════════════════════

    void lsbMode_voxOff_afterOn_emitsFalse()
    {
        MoxController ctrl;
        ctrl.onModeChanged(DSPMode::LSB);
        ctrl.setVoxEnabled(true);  // sets gated=true, emits true

        QSignalSpy spy(&ctrl, &MoxController::voxRunRequested);
        ctrl.setVoxEnabled(false);  // gated → false, must emit false

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), false);
    }

    // ════════════════════════════════════════════════════════════════════════
    // §4 — CW mode + VOX on → does NOT emit voxRunRequested(true)
    //
    // From Thetis cmaster.cs:1039-1052 [v2.10.3.13]:
    //   CWL is not in the voice-family condition; run = false.
    // ════════════════════════════════════════════════════════════════════════

    void cwMode_voxOn_doesNotEmitTrue()
    {
        MoxController ctrl;
        ctrl.onModeChanged(DSPMode::CWL);

        QSignalSpy spy(&ctrl, &MoxController::voxRunRequested);
        ctrl.setVoxEnabled(true);  // gated = true && isVoiceMode(CWL)=false → false
        // m_lastVoxRunGated was already false → no change → no emit.

        QCOMPARE(spy.count(), 0);
    }

    // ════════════════════════════════════════════════════════════════════════
    // §5 — Voice + VOX on, then mode change to CW → emits voxRunRequested(false)
    //
    // Mode-change re-evaluation: crossing from voice to non-voice mode while
    // VOX is enabled drops the gated flag from true to false.
    // ════════════════════════════════════════════════════════════════════════

    void voiceToCwModeChange_voxOn_emitsFalse()
    {
        MoxController ctrl;
        ctrl.onModeChanged(DSPMode::LSB);
        ctrl.setVoxEnabled(true);  // gated=true, emits true (not under spy)

        QSignalSpy spy(&ctrl, &MoxController::voxRunRequested);
        ctrl.onModeChanged(DSPMode::CWL);  // gated → false; must emit false

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), false);
    }

    // ════════════════════════════════════════════════════════════════════════
    // §6 — CW + VOX on (no emit), then mode change to voice → emits true
    //
    // Mode-change re-evaluation in the other direction: switching from a
    // non-voice mode to a voice mode while VOX is enabled raises the gate.
    // ════════════════════════════════════════════════════════════════════════

    void cwToVoiceModeChange_voxOn_emitsTrue()
    {
        MoxController ctrl;
        ctrl.onModeChanged(DSPMode::CWL);
        ctrl.setVoxEnabled(true);  // gated=false (CW), no emit

        QSignalSpy spy(&ctrl, &MoxController::voxRunRequested);
        ctrl.onModeChanged(DSPMode::LSB);  // gated → true; must emit true

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), true);
    }

    // ════════════════════════════════════════════════════════════════════════
    // §7 — Mode change within voice family while VOX on → no double emit
    //
    // Idempotent on GATED state: both LSB and USB are voice modes; switching
    // between them while VOX is enabled leaves gated=true throughout.
    // voxRunRequested must NOT fire again.
    // ════════════════════════════════════════════════════════════════════════

    void withinVoiceModeChange_voxOn_noDoubleEmit()
    {
        MoxController ctrl;
        ctrl.onModeChanged(DSPMode::LSB);
        ctrl.setVoxEnabled(true);  // gated=true, emit true (not under spy)

        QSignalSpy spy(&ctrl, &MoxController::voxRunRequested);
        ctrl.onModeChanged(DSPMode::USB);  // gated still true → no emit

        QCOMPARE(spy.count(), 0);
    }

    // ════════════════════════════════════════════════════════════════════════
    // §8 — Repeat setVoxEnabled(true) → no double emit
    //
    // Idempotent on GATED state: calling setVoxEnabled(true) twice in a
    // voice mode must not emit voxRunRequested a second time.
    // ════════════════════════════════════════════════════════════════════════

    void repeatVoxOn_noDoubleEmit()
    {
        MoxController ctrl;
        ctrl.onModeChanged(DSPMode::USB);
        ctrl.setVoxEnabled(true);  // gated=true, emit true (not under spy)

        QSignalSpy spy(&ctrl, &MoxController::voxRunRequested);
        ctrl.setVoxEnabled(true);  // gated still true → no emit

        QCOMPARE(spy.count(), 0);
    }

    // ════════════════════════════════════════════════════════════════════════
    // §9 — All 8 voice-family modes: each + voxEnabled(true) → emits true
    //
    // From Thetis cmaster.cs:1043-1050 [v2.10.3.13]:
    //   mode == DSPMode.LSB  || mode == DSPMode.USB  || mode == DSPMode.DSB ||
    //   mode == DSPMode.AM   || mode == DSPMode.SAM  || mode == DSPMode.FM  ||
    //   mode == DSPMode.DIGL || mode == DSPMode.DIGU
    // ════════════════════════════════════════════════════════════════════════

    void voiceFamilyCoverage_allEightModes_emitTrue_data()
    {
        QTest::addColumn<int>("mode");

        QTest::newRow("LSB")  << static_cast<int>(DSPMode::LSB);
        QTest::newRow("USB")  << static_cast<int>(DSPMode::USB);
        QTest::newRow("DSB")  << static_cast<int>(DSPMode::DSB);
        QTest::newRow("AM")   << static_cast<int>(DSPMode::AM);
        QTest::newRow("SAM")  << static_cast<int>(DSPMode::SAM);
        QTest::newRow("FM")   << static_cast<int>(DSPMode::FM);
        QTest::newRow("DIGL") << static_cast<int>(DSPMode::DIGL);
        QTest::newRow("DIGU") << static_cast<int>(DSPMode::DIGU);
    }

    void voiceFamilyCoverage_allEightModes_emitTrue()
    {
        QFETCH(int, mode);
        const DSPMode dspMode = static_cast<DSPMode>(mode);

        MoxController ctrl;
        ctrl.onModeChanged(dspMode);

        QSignalSpy spy(&ctrl, &MoxController::voxRunRequested);
        ctrl.setVoxEnabled(true);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), true);
    }

    // ════════════════════════════════════════════════════════════════════════
    // §10 — All 4 excluded modes: each + voxEnabled(true) → no emit
    //
    // From Thetis cmaster.cs:1039-1052 [v2.10.3.13]:
    //   CWL, CWU are CW modes (no mic audio).
    //   SPEC, DRM are special/DRM modes.
    //   None appear in the voice-family condition; run = false.
    // ════════════════════════════════════════════════════════════════════════

    void excludedModes_allFour_noEmit_data()
    {
        QTest::addColumn<int>("mode");

        QTest::newRow("CWL")  << static_cast<int>(DSPMode::CWL);
        QTest::newRow("CWU")  << static_cast<int>(DSPMode::CWU);
        QTest::newRow("SPEC") << static_cast<int>(DSPMode::SPEC);
        QTest::newRow("DRM")  << static_cast<int>(DSPMode::DRM);
    }

    void excludedModes_allFour_noEmit()
    {
        QFETCH(int, mode);
        const DSPMode dspMode = static_cast<DSPMode>(mode);

        MoxController ctrl;
        ctrl.onModeChanged(dspMode);

        QSignalSpy spy(&ctrl, &MoxController::voxRunRequested);
        ctrl.setVoxEnabled(true);  // gated=false (excluded mode); m_lastVoxRunGated was false → no emit

        // voxRunRequested must not have fired with true (or at all,
        // since gated stays false matching initial m_lastVoxRunGated).
        for (int i = 0; i < spy.count(); ++i) {
            QVERIFY2(!spy.at(i).at(0).toBool(),
                     "voxRunRequested(true) must not fire in excluded mode");
        }
    }
};

QTEST_MAIN(TestMoxControllerVoxEnabled)
#include "tst_mox_controller_vox_enabled.moc"
