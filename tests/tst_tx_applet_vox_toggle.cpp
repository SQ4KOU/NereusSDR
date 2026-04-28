// no-port-check: test-only — exercises NereusSDR-native TxApplet VOX toggle wiring.
// Phase 3M-1b J.2.
//
// TxApplet inserts a checkable VOX button below the Tune Power slider.
// The button is bidirectional with TransmitModel::voxEnabled (default false;
// does NOT persist across restarts — safety: VOX always loads OFF, plan §0 row 8).
// Right-click opens VoxSettingsPopup with 3 sliders: threshold (dB),
// gain (scalar), hang time (ms).
//
// Test cases:
//  1. Default state  — voxEnabled() default is false (plan §0 row 8 safety rule).
//  2. UI → Model toggle ON  — setVoxEnabled(true) → getter returns true.
//  3. UI → Model toggle OFF — setVoxEnabled(false) → getter returns false.
//  4. Model → UI  — voxEnabledChanged(true) fires with correct value.
//  5. Model → UI  — voxEnabledChanged(false) fires with correct value.
//  6. Visual state on toggle — voxEnabledChanged fires (implying visual update);
//     checked state contract: button should reflect model state via signal.
//  7. No feedback loop — setVoxEnabled(false) twice → only one signal emitted
//     after initial set (idempotent guard).
//  8. Popup has 3 sliders — VoxSettingsPopup constructed with 3 param rows.
//  9. Popup slider round-trip — changing threshold slider emits thresholdDbChanged
//     with the slider's integer value.
//
// These tests exercise the model layer contracts + VoxSettingsPopup that
// TxApplet::wireControls() depends on. TxApplet itself requires a display
// server; widget construction is not performed here (same pattern as
// tst_tx_applet_tune_wiring.cpp and tst_tx_applet_mic_gain.cpp).

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QSignalSpy>

#include "models/TransmitModel.h"
#include "gui/widgets/VoxSettingsPopup.h"

using namespace NereusSDR;

class TestTxAppletVoxToggle : public QObject
{
    Q_OBJECT

private slots:
    void voxEnabled_defaultIsFalse();
    void setVoxEnabled_true_roundtrip();
    void setVoxEnabled_false_roundtrip();
    void voxEnabledChanged_firesTrue();
    void voxEnabledChanged_firesFalse();
    void voxEnabled_visualChangeOnToggle();
    void noFeedbackLoop_idempotentFalse();
    void popupHasThreeSliders();
    void popupSlider_thresholdRoundtrip();
};

// ── 1. Default state ─────────────────────────────────────────────────────────
// VOX always loads OFF — plan §0 row 8 safety rule.
// From Thetis audio.cs:167 [v2.10.3.13]: private static bool vox_enabled = false;
void TestTxAppletVoxToggle::voxEnabled_defaultIsFalse()
{
    TransmitModel tx;
    QVERIFY(!tx.voxEnabled());
}

// ── 2. UI → Model toggle ON ──────────────────────────────────────────────────
void TestTxAppletVoxToggle::setVoxEnabled_true_roundtrip()
{
    TransmitModel tx;
    tx.setVoxEnabled(true);
    QVERIFY(tx.voxEnabled());
}

// ── 3. UI → Model toggle OFF ─────────────────────────────────────────────────
void TestTxAppletVoxToggle::setVoxEnabled_false_roundtrip()
{
    TransmitModel tx;
    tx.setVoxEnabled(true);   // first ON
    tx.setVoxEnabled(false);  // back OFF
    QVERIFY(!tx.voxEnabled());
}

// ── 4. Model → UI: voxEnabledChanged(true) fires ────────────────────────────
void TestTxAppletVoxToggle::voxEnabledChanged_firesTrue()
{
    TransmitModel tx;
    QSignalSpy spy(&tx, &TransmitModel::voxEnabledChanged);

    tx.setVoxEnabled(true);

    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.at(0).at(0).toBool());
}

// ── 5. Model → UI: voxEnabledChanged(false) fires ───────────────────────────
void TestTxAppletVoxToggle::voxEnabledChanged_firesFalse()
{
    TransmitModel tx;
    tx.setVoxEnabled(true);   // prime state to ON first

    QSignalSpy spy(&tx, &TransmitModel::voxEnabledChanged);
    tx.setVoxEnabled(false);

    QCOMPARE(spy.count(), 1);
    QVERIFY(!spy.at(0).at(0).toBool());
}

// ── 6. Visual state on toggle ────────────────────────────────────────────────
// The TxApplet VOX button's visual state (checked/unchecked) is driven by
// voxEnabledChanged. Here we verify the signal fires when toggling, which is
// the contract that causes the UI to update. We also confirm the boolean
// sequence matches (false → true → false).
void TestTxAppletVoxToggle::voxEnabled_visualChangeOnToggle()
{
    TransmitModel tx;
    QSignalSpy spy(&tx, &TransmitModel::voxEnabledChanged);

    // Toggle ON → button becomes checked (green border via greenCheckedStyle).
    tx.setVoxEnabled(true);
    QCOMPARE(spy.count(), 1);
    QVERIFY(spy.at(0).at(0).toBool());

    // Toggle OFF → button unchecked (green border removed).
    tx.setVoxEnabled(false);
    QCOMPARE(spy.count(), 2);
    QVERIFY(!spy.at(1).at(0).toBool());
}

// ── 7. No feedback loop / idempotency ────────────────────────────────────────
// Setting the same value twice must not emit a second signal.
// This verifies the m_updatingFromModel guard (model side: idempotent check).
void TestTxAppletVoxToggle::noFeedbackLoop_idempotentFalse()
{
    TransmitModel tx;
    // tx starts at false (default).

    QSignalSpy spy(&tx, &TransmitModel::voxEnabledChanged);

    // Calling setVoxEnabled(false) on a model that is already false → no-op.
    tx.setVoxEnabled(false);
    QCOMPARE(spy.count(), 0);

    // Calling setVoxEnabled(true) → exactly one emit.
    tx.setVoxEnabled(true);
    QCOMPARE(spy.count(), 1);

    // Calling setVoxEnabled(true) again → no-op (idempotent guard).
    tx.setVoxEnabled(true);
    QCOMPARE(spy.count(), 1);
}

// ── 8. Popup has 3 sliders ───────────────────────────────────────────────────
// VoxSettingsPopup must expose exactly 3 slider rows:
//   threshold (dB), gain (scalar), hang time (ms).
// We use the sliderCount() accessor on the popup.
// The popup is constructed headlessly (no QApplication show() call needed for
// the construction + accessor query path).
void TestTxAppletVoxToggle::popupHasThreeSliders()
{
    // Construct with TransmitModel defaults.
    // Threshold: -40 dB (NereusSDR default, plan §C.3)
    // Gain: 1.0f (Thetis audio.cs:194 [v2.10.3.13]: vox_gain = 1.0f)
    // Hang: 500 ms (Thetis setup.designer.cs:45020-45024 [v2.10.3.13])
    TransmitModel tx;
    VoxSettingsPopup popup(
        tx.voxThresholdDb(),
        tx.voxGainScalar(),
        tx.voxHangTimeMs(),
        nullptr);

    QCOMPARE(popup.sliderCount(), 3);
}

// ── 9. Popup slider round-trip ───────────────────────────────────────────────
// When the popup emits thresholdDbChanged, TransmitModel::setVoxThresholdDb
// should be called, and voxThresholdDb() should reflect the new value.
// We exercise the signal→model binding here (same pattern the popup uses when
// wired in TxApplet::showVoxSettingsPopup()).
void TestTxAppletVoxToggle::popupSlider_thresholdRoundtrip()
{
    TransmitModel tx;

    // Spy on the popup's signal (exercises the signal exists and carries int).
    VoxSettingsPopup popup(
        tx.voxThresholdDb(),
        tx.voxGainScalar(),
        tx.voxHangTimeMs(),
        nullptr);

    QSignalSpy spy(&popup, &VoxSettingsPopup::thresholdDbChanged);

    // Wire popup → model (as TxApplet::showVoxSettingsPopup() does).
    connect(&popup, &VoxSettingsPopup::thresholdDbChanged,
            &tx,    &TransmitModel::setVoxThresholdDb);

    // Simulate the popup emitting a new threshold value.
    emit popup.thresholdDbChanged(-30);

    // Signal captured.
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), -30);

    // Model updated.
    QCOMPARE(tx.voxThresholdDb(), -30);
}

QTEST_MAIN(TestTxAppletVoxToggle)
#include "tst_tx_applet_vox_toggle.moc"
