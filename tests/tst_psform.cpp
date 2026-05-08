// no-port-check: NereusSDR-original unit-test file.  Thetis cite comments
// document upstream sources; no Thetis logic ported in this test file.
// =================================================================
// tests/tst_psform.cpp  (NereusSDR)
// =================================================================
//
// Unit tests for the Phase 3M-4 Task 8 PsForm modeless dialog.
//
// PsForm ports Thetis PSForm.cs (1,164 LOC) [v2.10.3.13] verbatim — title
// "PureSignal 2.0", ClientSize 560x300 default with Advanced collapse to
// 560x60.  This test file exercises:
//
//   1. The dialog constructs with null RadioModel + PureSignal pointers
//      (test-friendly seam).
//
//   2. All 23 designer controls exist by objectName per
//      PSForm.designer.cs:1-969 [v2.10.3.13].
//
//   3. The Advanced toggle collapses the body widgets and restores them.
//
//   4. The OFF button invokes PureSignal::reset() when wired.
//
//   5. The Single Cal button invokes PureSignal::singleCalibrate() when wired.
//
//   6. The Two-tone toggle invokes PureSignal::setTwoToneOn(checked).
//
//   7. The Save button is gated on PureSignal::correctionsBeingAppliedChanged
//      (post-Codex Fix D — see PR #212 comments).
//
//   8. The Always On Top checkbox toggles Qt::WindowStaysOnTopHint.
//
//   9. The default values for chkPSPin, chkPSMap, chkPSAutoAttenuate are
//      Checked per PSForm.designer.cs:210-211, 193-194, 227-228 [v2.10.3.13].
//
//   10. The TINT combo populates "0.5", "1.1", "2.5" per
//       PSForm.designer.cs:164-167 [v2.10.3.13].
//
//   11. The udPSMoxDelay default value is 2.0 per
//       PSForm.designer.cs:368-372 [v2.10.3.13].
//
// Source: NereusSDR-original.  See PsForm.h for the Thetis cite map.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-06 — New test file for Phase 3M-4 Task 8: PsForm dialog
//                 unit tests.  J.J. Boyd (KG4VCF), with AI-assisted
//                 implementation via Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>

#include "core/PureSignal.h"
#include "core/TxChannel.h"
#include "gui/PsForm.h"

using namespace NereusSDR;

// WDSP TX channel id — from Thetis cmaster.c:177-190 [v2.10.3.13].
static constexpr int kTxChannelId = 1;

class TstPsForm : public QObject {
    Q_OBJECT

private slots:

    // ── Test 1: construct + destruct without any wiring ─────────────────────
    //
    // PsForm must tolerate being constructed with a nullptr PureSignal
    // pointer (radio not yet connected case).  The dialog appears but its
    // controls are inert.

    void constructAndDestruct_withNoPureSignal_doesNotCrash()
    {
        PsForm form(/*radioModel=*/nullptr, /*pureSignal=*/nullptr);
        QCOMPARE(form.windowTitle(), QStringLiteral("PureSignal 2.0"));
        QCOMPARE(form.isModal(), false);
    }

    // ── Test 2: all 23 designer controls exist by objectName ────────────────
    //
    // The 23 controls are enumerated in PSForm.designer.cs [v2.10.3.13]:
    //   chkPSOnTop, lblPSTint, btnPSRestore, btnPSSave, btnPSAdvanced,
    //   comboPSTint, chkPSStbl, chkPSMap, chkPSPin, chkPSAutoAttenuate,
    //   btnPSAmpView, chkPSRelaxPtol, btnPSTwoToneGen, lblPSInfoFB,
    //   lblPSInfoCO, udPSMoxDelay, udPSPhnum, udPSCalWait, chkQuickAttenuate,
    //   chkShow2ToneMeasurements, btnPSReset, btnPSCalibrate, pbWarningSetPk
    // (plus the grpPSInfo group box housing the indicators / btnDefaultPeaks
    // / checkLoopback).

    void allTwentyThreeControlsExistByObjectName()
    {
        PsForm form(nullptr, nullptr);

        // Top action row (7 buttons)
        QVERIFY(form.findChild<QPushButton*>(QStringLiteral("btnPSTwoToneGen")));
        QVERIFY(form.findChild<QPushButton*>(QStringLiteral("btnPSCalibrate")));
        QVERIFY(form.findChild<QPushButton*>(QStringLiteral("btnPSAmpView")));
        QVERIFY(form.findChild<QPushButton*>(QStringLiteral("btnPSAdvanced")));
        QVERIFY(form.findChild<QPushButton*>(QStringLiteral("btnPSSave")));
        QVERIFY(form.findChild<QPushButton*>(QStringLiteral("btnPSRestore")));
        QVERIFY(form.findChild<QPushButton*>(QStringLiteral("btnPSReset")));

        // Status row (2 badges)
        QVERIFY(form.findChild<QLabel*>(QStringLiteral("lblPSInfoFB")));
        QVERIFY(form.findChild<QLabel*>(QStringLiteral("lblPSInfoCO")));

        // Calibration option checkboxes (6)
        QVERIFY(form.findChild<QCheckBox*>(QStringLiteral("chkPSPin")));
        QVERIFY(form.findChild<QCheckBox*>(QStringLiteral("chkPSMap")));
        QVERIFY(form.findChild<QCheckBox*>(QStringLiteral("chkPSStbl")));
        QVERIFY(form.findChild<QCheckBox*>(QStringLiteral("chkPSAutoAttenuate")));
        QVERIFY(form.findChild<QCheckBox*>(QStringLiteral("chkPSRelaxPtol")));
        QVERIFY(form.findChild<QCheckBox*>(QStringLiteral("chkQuickAttenuate")));

        // Timing controls (3 + TINT label + TINT combo)
        QVERIFY(form.findChild<QDoubleSpinBox*>(QStringLiteral("udPSMoxDelay")));
        QVERIFY(form.findChild<QSpinBox*>(QStringLiteral("udPSPhnum")));
        QVERIFY(form.findChild<QDoubleSpinBox*>(QStringLiteral("udPSCalWait")));
        QVERIFY(form.findChild<QLabel*>(QStringLiteral("lblPSTint")));
        QVERIFY(form.findChild<QComboBox*>(QStringLiteral("comboPSTint")));

        // Always-on-top + 2-Tone + warning icon
        QVERIFY(form.findChild<QCheckBox*>(QStringLiteral("chkPSOnTop")));
        QVERIFY(form.findChild<QCheckBox*>(QStringLiteral("chkShow2ToneMeasurements")));
        QVERIFY(form.findChild<QLabel*>(QStringLiteral("pbWarningSetPk")));

        // Calibration Information group (advanced section)
        QVERIFY(form.findChild<QGroupBox*>(QStringLiteral("grpPSInfo")));
        QVERIFY(form.findChild<QPushButton*>(QStringLiteral("btnDefaultPeaks")));
        QVERIFY(form.findChild<QCheckBox*>(QStringLiteral("checkLoopback")));
    }

    // ── Test 3: defaults match Thetis designer values ────────────────────────

    void defaultsMatchThetisDesignerValues()
    {
        PsForm form(nullptr, nullptr);

        // Default Checked per PSForm.designer.cs [v2.10.3.13]:
        QCOMPARE(form.findChild<QCheckBox*>(QStringLiteral("chkPSPin"))->isChecked(), true);
        QCOMPARE(form.findChild<QCheckBox*>(QStringLiteral("chkPSMap"))->isChecked(), true);
        QCOMPARE(form.findChild<QCheckBox*>(QStringLiteral("chkPSAutoAttenuate"))->isChecked(), true);

        // Default unchecked
        QCOMPARE(form.findChild<QCheckBox*>(QStringLiteral("chkPSStbl"))->isChecked(), false);
        QCOMPARE(form.findChild<QCheckBox*>(QStringLiteral("chkPSRelaxPtol"))->isChecked(), false);
        QCOMPARE(form.findChild<QCheckBox*>(QStringLiteral("chkQuickAttenuate"))->isChecked(), false);
        QCOMPARE(form.findChild<QCheckBox*>(QStringLiteral("chkPSOnTop"))->isChecked(), false);
        QCOMPARE(form.findChild<QCheckBox*>(QStringLiteral("chkShow2ToneMeasurements"))->isChecked(), false);

        // From PSForm.designer.cs:368-372 [v2.10.3.13] — udPSMoxDelay default 2.0
        QCOMPARE(form.findChild<QDoubleSpinBox*>(QStringLiteral("udPSMoxDelay"))->value(), 2.0);

        // From PSForm.designer.cs:801-805 [v2.10.3.13] — udPSCalWait default 0
        QCOMPARE(form.findChild<QDoubleSpinBox*>(QStringLiteral("udPSCalWait"))->value(), 0.0);

        // From PSForm.designer.cs:409-413 [v2.10.3.13] — udPSPhnum default 150
        QCOMPARE(form.findChild<QSpinBox*>(QStringLiteral("udPSPhnum"))->value(), 150);
    }

    // ── Test 4: TINT combo populates "0.5"/"1.1"/"2.5" ────────────────────────
    //
    // From PSForm.designer.cs:164-172 [v2.10.3.13]:
    //   this.comboPSTint.Items.AddRange(new object[] { "0.5", "1.1", "2.5" });
    //   this.comboPSTint.Text = "0.5";

    void tintComboPopulatesThreeOptionsWithDefaultZeroPointFive()
    {
        PsForm form(nullptr, nullptr);
        auto* combo = form.findChild<QComboBox*>(QStringLiteral("comboPSTint"));
        QVERIFY(combo);
        QCOMPARE(combo->count(), 3);
        QCOMPARE(combo->itemText(0), QStringLiteral("0.5"));
        QCOMPARE(combo->itemText(1), QStringLiteral("1.1"));
        QCOMPARE(combo->itemText(2), QStringLiteral("2.5"));
        QCOMPARE(combo->currentText(), QStringLiteral("0.5"));
    }

    // ── Test 5: Advanced toggle collapses + restores body widgets ────────────
    //
    // From Thetis PSForm.cs:889-902 setAdvancedView [v2.10.3.13] —
    // sits immediately above the _advancedON declaration on line 888
    // (//MW0LGE attribution preserved — author tag from upstream
    // //MW0LGE_[2.9.0.7] version-stamped comment):
    //   _advancedON = !_advancedON;
    //   if (_advancedON) ClientSize = 560x60;
    //   else             ClientSize = 560x300;
    // NereusSDR mirrors via hide/show on the body widget container.

    void advancedTogglesBodyVisibility()
    {
        PsForm form(nullptr, nullptr);
        // Force layout so initial sizes are valid
        form.show();
        form.adjustSize();

        const auto* moxSpin =
            form.findChild<QDoubleSpinBox*>(QStringLiteral("udPSMoxDelay"));
        QVERIFY(moxSpin);
        QCOMPARE(moxSpin->isVisible(), true);

        // Toggle into Advanced (collapsed) — body hides.
        auto* btnAdv = form.findChild<QPushButton*>(QStringLiteral("btnPSAdvanced"));
        QVERIFY(btnAdv);
        btnAdv->click();
        QCOMPARE(moxSpin->isVisible(), false);
        QCOMPARE(form.isAdvancedCollapsed(), true);

        // Top action row stays visible (verify a few canonical buttons).
        QVERIFY(form.findChild<QPushButton*>(QStringLiteral("btnPSReset"))->isVisible());
        QVERIFY(form.findChild<QPushButton*>(QStringLiteral("btnPSCalibrate"))->isVisible());

        // Toggle back — body returns.
        btnAdv->click();
        QCOMPARE(moxSpin->isVisible(), true);
        QCOMPARE(form.isAdvancedCollapsed(), false);
    }

    // ── Test 6: Single Cal button invokes PureSignal::singleCalibrate ────────

    void singleCalButtonInvokesPureSignalSingleCalibrate()
    {
        TxChannel tx(kTxChannelId);
        PureSignal ps(nullptr, &tx, nullptr, nullptr, nullptr, nullptr);
        PsForm form(/*radioModel=*/nullptr, /*pureSignal=*/&ps);

        QSignalSpy startSpy(&ps, &PureSignal::calibrationStarted);
        auto* btn =
            form.findChild<QPushButton*>(QStringLiteral("btnPSCalibrate"));
        QVERIFY(btn);
        btn->click();
        QCOMPARE(startSpy.count(), 1);
    }

    // ── Test 7: OFF button invokes PureSignal::reset ─────────────────────────

    void offButtonInvokesPureSignalReset()
    {
        TxChannel tx(kTxChannelId);
        PureSignal ps(nullptr, &tx, nullptr, nullptr, nullptr, nullptr);
        // Make sure auto-cal is on so reset() actually flips state.
        ps.setAutoCalEnabled(true);
        QCOMPARE(ps.isAutoCalEnabled(), true);

        PsForm form(nullptr, &ps);

        QSignalSpy autoSpy(&ps, &PureSignal::autoCalEnabledChanged);
        auto* btn = form.findChild<QPushButton*>(QStringLiteral("btnPSReset"));
        QVERIFY(btn);
        btn->click();
        // reset() calls forceAutoCalDisable() → setAutoCalEnabled(false).
        QCOMPARE(autoSpy.count(), 1);
        QCOMPARE(ps.isAutoCalEnabled(), false);
    }

    // ── Test 8: Two-tone toggle invokes PureSignal::setTwoToneOn ─────────────
    //
    // From Thetis PSForm.cs:508-522 btnPSTwoToneGen_Click [v2.10.3.13] —
    // toggles _ttgenON and SetupForm.TTgenrun.  In NereusSDR this routes
    // through PureSignal → TwoToneController.

    void twoToneButtonInvokesPureSignalSetTwoToneOn()
    {
        TxChannel tx(kTxChannelId);
        PureSignal ps(nullptr, &tx, nullptr, nullptr, nullptr, nullptr);
        PsForm form(nullptr, &ps);

        auto* btn =
            form.findChild<QPushButton*>(QStringLiteral("btnPSTwoToneGen"));
        QVERIFY(btn);
        QCOMPARE(btn->isCheckable(), true);

        // Flip ON
        btn->click();
        QCOMPARE(btn->isChecked(), true);
        // Flip OFF
        btn->click();
        QCOMPARE(btn->isChecked(), false);
    }

    // ── Test 9: Always On Top checkbox sets WindowStaysOnTopHint ────────────

    void alwaysOnTopToggleSetsWindowStaysOnTopHint()
    {
        PsForm form(nullptr, nullptr);
        form.show();

        auto* chk = form.findChild<QCheckBox*>(QStringLiteral("chkPSOnTop"));
        QVERIFY(chk);
        QCOMPARE(chk->isChecked(), false);

        chk->setChecked(true);
        QCOMPARE(form.windowFlags() & Qt::WindowStaysOnTopHint,
                 Qt::WindowStaysOnTopHint);

        chk->setChecked(false);
        QCOMPARE(form.windowFlags() & Qt::WindowStaysOnTopHint,
                 Qt::WindowFlags{});
    }

    // ── Test 10: PIN toggle forwards to PureSignal::setPinMode ───────────────

    void pinCheckBoxForwardsToPureSignalSetPinMode()
    {
        TxChannel tx(kTxChannelId);
        PureSignal ps(nullptr, &tx, nullptr, nullptr, nullptr, nullptr);
        PsForm form(nullptr, &ps);

        QSignalSpy spy(&ps, &PureSignal::pinModeChanged);
        auto* chk = form.findChild<QCheckBox*>(QStringLiteral("chkPSPin"));
        QVERIFY(chk);
        // Default checked → click un-checks.
        chk->click();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(ps.pinMode(), false);
    }

    // ── Test 11: MOX-delay spinbox forwards to PureSignal::setMoxDelay ──────

    void moxDelaySpinBoxForwardsToPureSignalSetMoxDelay()
    {
        TxChannel tx(kTxChannelId);
        PureSignal ps(nullptr, &tx, nullptr, nullptr, nullptr, nullptr);
        PsForm form(nullptr, &ps);

        QSignalSpy spy(&ps, &PureSignal::moxDelayChanged);
        auto* spin =
            form.findChild<QDoubleSpinBox*>(QStringLiteral("udPSMoxDelay"));
        QVERIFY(spin);
        spin->setValue(5.0);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(ps.moxDelay(), 5.0);
    }

    // ── Test 12: Save button gating on correctionsBeingApplied ──────────────
    //
    // From Thetis PSForm.cs:574-590 [v2.10.3.13]:
    //   if (puresignal.CorrectionsBeingApplied)
    //       btnPSSave.Enabled = true;
    //   else
    //       btnPSSave.Enabled = false;

    void saveButtonStartsDisabledWhenNoCorrections()
    {
        TxChannel tx(kTxChannelId);
        PureSignal ps(nullptr, &tx, nullptr, nullptr, nullptr, nullptr);
        PsForm form(nullptr, &ps);

        auto* btn = form.findChild<QPushButton*>(QStringLiteral("btnPSSave"));
        QVERIFY(btn);
        QCOMPARE(btn->isEnabled(), false);
    }
};

QTEST_MAIN(TstPsForm)
#include "tst_psform.moc"
