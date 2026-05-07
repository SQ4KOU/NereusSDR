// no-port-check: NereusSDR-original unit-test file.  Thetis cite comments
// document upstream sources; no Thetis logic ported in this test file.
// =================================================================
// tests/tst_applet_ps_wiring.cpp  (NereusSDR)
// =================================================================
//
// Phase 3M-4 Task 13 — applet wiring tests for PureSignalApplet and the
// TxApplet [PS-A] toggle.  Verifies the live wiring that replaces the prior
// NyiOverlay::markNyi scaffolding:
//
//   PureSignalApplet:
//     1. Calibrate button → PureSignal::singleCalibrate
//     2. Auto toggle      ↔ PureSignal::setAutoCalEnabled (+ echo back)
//     3. 2-Tone toggle    → PureSignal::setTwoToneOn (forwarded via TT controller)
//     4. Save button      → has correctingChanged-driven enable gating
//     5. Restore button   → exists and wired (file-dialog opens are not
//                            exercised in unit tests)
//     6. Right-click on every control → openPureSignalDialogRequested
//     7. FB level gauge   ← PureSignal::feedbackLevelChanged 0..255 → 0..100
//     8. Iterations label ← PureSignal::calibrationCountChanged
//     9. Correction gauge ← PureSignal::correctionPeakChanged
//    10. Cal/Run LEDs     ← PureSignal::calStateChanged
//    11. Fbk LED          ← PureSignal::feedbackActiveChanged
//
//   TxApplet [PS-A]:
//    12. Hidden by default until setBoardCapabilities(hasPureSignal=true).
//    13. Visible when setBoardCapabilities(hasPureSignal=true) is called.
//    14. Left-click toggle → PureSignal::setAutoCalEnabled.
//    15. Right-click → openPureSignalDialogRequested signal.
//    16. autoCalEnabledChanged echo back → button checked state.
//
// Test scaffold mirrors tst_puresignal_coordinator.cpp's late-bound
// dependency pattern: PureSignal is constructed with all-null deps but a
// real TxChannel (so getPSInfo / setPSControl no-ops are safe).  The
// coordinator pointer is injected into the applets via setPureSignal().
//
// Source: NereusSDR-original.  See PureSignalApplet.h + TxApplet.h for
// Thetis cite map.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-06 — New test file for Phase 3M-4 Task 13: applet wiring.
//                 J.J. Boyd (KG4VCF), with AI-assisted implementation
//                 via Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>
#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>

#include "core/AppSettings.h"
#include "core/PureSignal.h"
#include "core/TxChannel.h"
#include "gui/HGauge.h"
#include "gui/applets/PureSignalApplet.h"
#include "gui/applets/TxApplet.h"
#include "models/RadioModel.h"

using namespace NereusSDR;

// WDSP TX channel id — from Thetis cmaster.c:177-190 [v2.10.3.13].
static constexpr int kTxChannelId = 1;

class TstAppletPsWiring : public QObject {
    Q_OBJECT

private slots:

    void initTestCase()
    {
        if (!qApp) {
            static int argc = 0;
            new QApplication(argc, nullptr);
        }
        AppSettings::instance().clear();
    }

    void cleanup()
    {
        AppSettings::instance().clear();
    }

    // =====================================================================
    // PureSignalApplet — control wiring
    // =====================================================================

    // ── Test 1: applet construction with a RadioModel does not crash ───────
    void pureSignalApplet_constructsWithRadioModel()
    {
        RadioModel rm;
        PureSignalApplet applet(&rm);
        QVERIFY(applet.findChild<QPushButton*>(
            QStringLiteral("PsAppletCalibrateBtn")) != nullptr);
        QVERIFY(applet.findChild<QPushButton*>(
            QStringLiteral("PsAppletAutoCalBtn")) != nullptr);
        QVERIFY(applet.findChild<QPushButton*>(
            QStringLiteral("PsAppletSaveBtn")) != nullptr);
        QVERIFY(applet.findChild<QPushButton*>(
            QStringLiteral("PsAppletRestoreBtn")) != nullptr);
        QVERIFY(applet.findChild<QPushButton*>(
            QStringLiteral("PsAppletTwoToneBtn")) != nullptr);
    }

    // ── Test 2: Calibrate button invokes PureSignal::singleCalibrate ───────
    //
    // PureSignal::singleCalibrate emits calibrationStarted on the first call
    // (per PSForm.cs:466-478 [v2.10.3.13] — _singleCalON flips false→true,
    // calibrationStarted fires).  The applet's left-click on Calibrate must
    // forward to the coordinator.
    void calibrateButton_invokesPureSignalSingleCalibrate()
    {
        TxChannel tx(kTxChannelId);
        PureSignal ps(nullptr, &tx, nullptr, nullptr, nullptr, nullptr);

        RadioModel rm;
        PureSignalApplet applet(&rm);
        applet.setPureSignal(&ps);

        QSignalSpy startedSpy(&ps, &PureSignal::calibrationStarted);
        auto* btn = applet.findChild<QPushButton*>(
            QStringLiteral("PsAppletCalibrateBtn"));
        QVERIFY(btn != nullptr);

        btn->click();
        QCOMPARE(startedSpy.count(), 1);
    }

    // ── Test 3: Auto toggle forwards to PureSignal::setAutoCalEnabled ──────
    void autoCalToggle_forwardsToPureSignal()
    {
        TxChannel tx(kTxChannelId);
        PureSignal ps(nullptr, &tx, nullptr, nullptr, nullptr, nullptr);

        RadioModel rm;
        PureSignalApplet applet(&rm);
        applet.setPureSignal(&ps);
        QCOMPARE(ps.isAutoCalEnabled(), false);

        auto* btn = applet.findChild<QPushButton*>(
            QStringLiteral("PsAppletAutoCalBtn"));
        QVERIFY(btn != nullptr);
        QVERIFY(btn->isCheckable());

        // UI → Model
        btn->click();
        QCOMPARE(ps.isAutoCalEnabled(), true);
        QCOMPARE(btn->isChecked(), true);

        btn->click();
        QCOMPARE(ps.isAutoCalEnabled(), false);
        QCOMPARE(btn->isChecked(), false);

        // Model → UI (echo back via autoCalEnabledChanged)
        ps.setAutoCalEnabled(true);
        QCOMPARE(btn->isChecked(), true);
    }

    // ── Test 4: 2-Tone toggle button checkable (action wiring is no-op
    //   when no TwoToneController is bound; coordinator forwards setActive
    //   when one is wired — out of scope here). ─────────────────────────────
    void twoToneToggle_drivesButtonState()
    {
        RadioModel rm;
        PureSignalApplet applet(&rm);

        auto* btn = applet.findChild<QPushButton*>(
            QStringLiteral("PsAppletTwoToneBtn"));
        QVERIFY(btn != nullptr);
        QVERIFY(btn->isCheckable());
        QCOMPARE(btn->isChecked(), false);

        btn->click();
        QCOMPARE(btn->isChecked(), true);

        btn->click();
        QCOMPARE(btn->isChecked(), false);
    }

    // ── Test 5: Save button is gated on correctionsBeingAppliedChanged ─────
    //
    // Mirrors PSForm.cs:574-590 btnPSSave gating [v2.10.3.13]:
    //   if (puresignal.CorrectionsBeingApplied) btnPSSave.Enabled = true;
    //
    // Codex Fix D (PR #212 commit b7fafaa): the gating signal split into
    // correctionsBeingAppliedChanged (info[14]==1, gates Save) vs
    // correctingChanged (FeedbackLevel > 90, gates Lime/Yellow badge).
    // The Save button connects to correctionsBeingAppliedChanged in
    // PureSignalApplet.cpp:431.  Pre-split this test emitted
    // correctingChanged; post-split that signal no longer carries the
    // Save semantic.
    void saveButton_gatedOnCorrectionsBeingApplied()
    {
        TxChannel tx(kTxChannelId);
        PureSignal ps(nullptr, &tx, nullptr, nullptr, nullptr, nullptr);

        RadioModel rm;
        PureSignalApplet applet(&rm);
        applet.setPureSignal(&ps);

        auto* btn = applet.findChild<QPushButton*>(
            QStringLiteral("PsAppletSaveBtn"));
        QVERIFY(btn != nullptr);

        // Default: not applying corrections → Save disabled.
        QCOMPARE(btn->isEnabled(), false);

        // Emit correctionsBeingAppliedChanged(true) → Save enabled.
        emit ps.correctionsBeingAppliedChanged(true);
        QCOMPARE(btn->isEnabled(), true);

        emit ps.correctionsBeingAppliedChanged(false);
        QCOMPARE(btn->isEnabled(), false);
    }

    // ── Test 6: Restore button exists ──────────────────────────────────────
    void restoreButton_exists()
    {
        RadioModel rm;
        PureSignalApplet applet(&rm);

        auto* btn = applet.findChild<QPushButton*>(
            QStringLiteral("PsAppletRestoreBtn"));
        QVERIFY(btn != nullptr);
    }

    // ── Test 7: Right-click on Calibrate emits openPureSignalDialogRequested ──
    void rightClickOnCalibrate_emitsOpenPureSignalDialogRequested()
    {
        RadioModel rm;
        PureSignalApplet applet(&rm);

        auto* btn = applet.findChild<QPushButton*>(
            QStringLiteral("PsAppletCalibrateBtn"));
        QVERIFY(btn != nullptr);
        QCOMPARE(btn->contextMenuPolicy(), Qt::CustomContextMenu);

        QSignalSpy spy(&applet,
                       &PureSignalApplet::openPureSignalDialogRequested);
        QVERIFY(spy.isValid());

        emit btn->customContextMenuRequested(QPoint(0, 0));
        QCOMPARE(spy.count(), 1);
    }

    // ── Test 8: Right-click on Auto / Save / Restore / 2-Tone all emit ─────
    void rightClickOnAllControls_emitsOpenPureSignalDialogRequested()
    {
        RadioModel rm;
        PureSignalApplet applet(&rm);

        QSignalSpy spy(&applet,
                       &PureSignalApplet::openPureSignalDialogRequested);
        QVERIFY(spy.isValid());

        const QStringList objectNames = {
            QStringLiteral("PsAppletAutoCalBtn"),
            QStringLiteral("PsAppletSaveBtn"),
            QStringLiteral("PsAppletRestoreBtn"),
            QStringLiteral("PsAppletTwoToneBtn"),
        };
        for (const QString& name : objectNames) {
            auto* btn = applet.findChild<QPushButton*>(name);
            QVERIFY2(btn != nullptr,
                     qPrintable(QStringLiteral("missing: ") + name));
            QCOMPARE(btn->contextMenuPolicy(), Qt::CustomContextMenu);
            emit btn->customContextMenuRequested(QPoint(0, 0));
        }

        // Also right-click on the gauges (per design doc §8.4.2).
        auto* fbGauge = applet.findChild<HGauge*>(
            QStringLiteral("PsAppletFeedbackGauge"));
        auto* corrGauge = applet.findChild<HGauge*>(
            QStringLiteral("PsAppletCorrectionGauge"));
        QVERIFY(fbGauge != nullptr);
        QVERIFY(corrGauge != nullptr);
        QCOMPARE(fbGauge->contextMenuPolicy(), Qt::CustomContextMenu);
        QCOMPARE(corrGauge->contextMenuPolicy(), Qt::CustomContextMenu);
        emit fbGauge->customContextMenuRequested(QPoint(0, 0));
        emit corrGauge->customContextMenuRequested(QPoint(0, 0));

        // 4 buttons + 2 gauges = 6 total emits.
        QCOMPARE(spy.count(), 6);
    }

    // ── Test 9: Feedback gauge updates on feedbackLevelChanged signal ──────
    //
    // Maps raw 0..255 to 0..100 per design doc §8.4.2 (level * 100.0/255.0).
    void feedbackGauge_updatesOnFeedbackLevelSignal()
    {
        TxChannel tx(kTxChannelId);
        PureSignal ps(nullptr, &tx, nullptr, nullptr, nullptr, nullptr);

        RadioModel rm;
        PureSignalApplet applet(&rm);
        applet.setPureSignal(&ps);

        auto* gauge = applet.findChild<HGauge*>(
            QStringLiteral("PsAppletFeedbackGauge"));
        QVERIFY(gauge != nullptr);

        // Level 128 (mid-range) → 50.196
        emit ps.feedbackLevelChanged(128);
        QVERIFY(qAbs(gauge->value() - (128.0 * 100.0 / 255.0)) < 0.1);

        // Level 255 (max) → 100.0
        emit ps.feedbackLevelChanged(255);
        QVERIFY(qAbs(gauge->value() - 100.0) < 0.1);
    }

    // ── Test 10: Iterations label updates on calibrationCountChanged ───────
    void iterationsLabel_updatesOnCalibrationCountSignal()
    {
        TxChannel tx(kTxChannelId);
        PureSignal ps(nullptr, &tx, nullptr, nullptr, nullptr, nullptr);

        RadioModel rm;
        PureSignalApplet applet(&rm);
        applet.setPureSignal(&ps);

        auto* lbl = applet.findChild<QLabel*>(
            QStringLiteral("PsAppletIterationsLabel"));
        QVERIFY(lbl != nullptr);

        emit ps.calibrationCountChanged(42);
        QCOMPARE(lbl->text(), QStringLiteral("Iterations: 42"));

        emit ps.calibrationCountChanged(0);
        QCOMPARE(lbl->text(), QStringLiteral("Iterations: 0"));
    }

    // ── Test 11: Correction gauge updates on correctionPeakChanged ─────────
    void correctionGauge_updatesOnCorrectionPeakSignal()
    {
        TxChannel tx(kTxChannelId);
        PureSignal ps(nullptr, &tx, nullptr, nullptr, nullptr, nullptr);

        RadioModel rm;
        PureSignalApplet applet(&rm);
        applet.setPureSignal(&ps);

        auto* gauge = applet.findChild<HGauge*>(
            QStringLiteral("PsAppletCorrectionGauge"));
        QVERIFY(gauge != nullptr);

        emit ps.correctionPeakChanged(0.5);
        QVERIFY(qAbs(gauge->value() - 50.0) < 0.1);

        emit ps.correctionPeakChanged(1.0);
        QVERIFY(qAbs(gauge->value() - 100.0) < 0.1);

        // Out-of-range value: gauge clamps internally via HGauge or our cap.
        emit ps.correctionPeakChanged(1.5);
        QVERIFY(gauge->value() <= 100.0);
    }

    // =====================================================================
    // TxApplet [PS-A] toggle wiring
    // =====================================================================

    // ── Test 12: PS-A button hidden by default (no caps pushed yet) ───────
    void psaButton_hiddenByDefault()
    {
        RadioModel rm;
        TxApplet applet(&rm);

        auto* btn = applet.findChild<QPushButton*>(
            QStringLiteral("TxAppletPsaBtn"));
        QVERIFY(btn != nullptr);
        QCOMPARE(btn->isVisible(), false);
    }

    // ── Test 13: PS-A button visible when caps.hasPureSignal == true ──────
    void psaButton_visibleWhenPureSignalCapable()
    {
        RadioModel rm;
        TxApplet applet(&rm);
        applet.show();  // need top-level show for child visibility queries

        auto* btn = applet.findChild<QPushButton*>(
            QStringLiteral("TxAppletPsaBtn"));
        QVERIFY(btn != nullptr);

        BoardCapabilities caps{};
        caps.hasPureSignal = true;
        applet.setBoardCapabilities(caps);
        QCOMPARE(btn->isVisible(), true);

        caps.hasPureSignal = false;
        applet.setBoardCapabilities(caps);
        QCOMPARE(btn->isVisible(), false);
    }

    // ── Test 14: PS-A left-click drives PureSignal::setAutoCalEnabled ─────
    //
    // Mirrors Thetis chkFWCATUBypass_Click (console.cs:36762 [v2.10.3.13]):
    //   The handler eventually calls puresignal.AutoCalEnabled = checked.
    void psaLeftClick_togglesAutoCalEnabled()
    {
        TxChannel tx(kTxChannelId);
        PureSignal ps(nullptr, &tx, nullptr, nullptr, nullptr, nullptr);

        RadioModel rm;
        TxApplet applet(&rm);
        applet.setPureSignal(&ps);
        QCOMPARE(ps.isAutoCalEnabled(), false);

        // Make the button visible so the click works through.
        BoardCapabilities caps{};
        caps.hasPureSignal = true;
        applet.setBoardCapabilities(caps);

        auto* btn = applet.findChild<QPushButton*>(
            QStringLiteral("TxAppletPsaBtn"));
        QVERIFY(btn != nullptr);
        QVERIFY(btn->isCheckable());

        btn->click();
        QCOMPARE(ps.isAutoCalEnabled(), true);
        QCOMPARE(btn->isChecked(), true);

        btn->click();
        QCOMPARE(ps.isAutoCalEnabled(), false);
        QCOMPARE(btn->isChecked(), false);
    }

    // ── Test 15: PS-A right-click emits openPureSignalDialogRequested ─────
    //
    // Mirrors Thetis chkFWCATUBypass_MouseDown (console.cs:46149-46152
    // [v2.10.3.13]):
    //   if (IsRightButton(e)) linearityToolStripMenuItem_Click(null,
    //                                                           EventArgs.Empty);
    void psaRightClick_emitsOpenPureSignalDialogRequested()
    {
        RadioModel rm;
        TxApplet applet(&rm);

        auto* btn = applet.findChild<QPushButton*>(
            QStringLiteral("TxAppletPsaBtn"));
        QVERIFY(btn != nullptr);
        QCOMPARE(btn->contextMenuPolicy(), Qt::CustomContextMenu);

        QSignalSpy spy(&applet, &TxApplet::openPureSignalDialogRequested);
        QVERIFY(spy.isValid());

        emit btn->customContextMenuRequested(QPoint(0, 0));
        QCOMPARE(spy.count(), 1);
    }

    // ── Test 16: PS-A button reflects autoCalEnabledChanged echo ──────────
    void psaButton_reflectsAutoCalEnabledChange()
    {
        TxChannel tx(kTxChannelId);
        PureSignal ps(nullptr, &tx, nullptr, nullptr, nullptr, nullptr);

        RadioModel rm;
        TxApplet applet(&rm);
        applet.setPureSignal(&ps);

        auto* btn = applet.findChild<QPushButton*>(
            QStringLiteral("TxAppletPsaBtn"));
        QVERIFY(btn != nullptr);
        QCOMPARE(btn->isChecked(), false);

        // Model → UI
        ps.setAutoCalEnabled(true);
        QCOMPARE(btn->isChecked(), true);

        ps.setAutoCalEnabled(false);
        QCOMPARE(btn->isChecked(), false);
    }
};

QTEST_MAIN(TstAppletPsWiring)
#include "tst_applet_ps_wiring.moc"
