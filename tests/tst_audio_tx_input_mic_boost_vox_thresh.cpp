// tests/tst_audio_tx_input_mic_boost_vox_thresh.cpp  (NereusSDR)
//
// Phase 3M-1b Task I.5 — Integration: chk20dbMicBoost → VOX threshold scaling.
//
// no-port-check: test fixture — no Thetis attribution required.
//
// Verifies the full end-to-end chain:
//   AudioTxInputPage checkbox toggle
//     → TransmitModel::setMicBoost(bool)
//     → TransmitModel::micBoostChanged signal
//     → MoxController::onMicBoostChanged  [H.2 wiring]
//     → MoxController::recomputeVoxThreshold
//     → MoxController::voxThresholdRequested(double) signal
//
// Test matrix (~7 cases):
//   1.  Default state — after construction, MoxController emits initial threshold
//       (NAN sentinel ensures at least one emit at startup).
//   2.  UI checkbox toggle ON — check Hermes +20 dB Mic Boost checkbox;
//       voxThresholdRequested fires with scaled value (thresh * voxGainScalar).
//   3.  UI checkbox toggle OFF — uncheck; fires with unscaled value.
//   4.  Model setMicBoost(true) → UI checkbox reflects (bidirectional sync).
//   5.  Idempotency — toggling boost ON twice emits only once for the second
//       identical state (no double-emit when value is unchanged).
//   6.  Cross-family parity (Orion) — Orion +20 dB Mic Boost checkbox also
//       triggers recomputeVoxThreshold via the same RadioModel wiring.
//   7.  Cross-family parity (Saturn) — Saturn +20 dB Mic Boost checkbox.
//
// Math reference (from Thetis cmaster.cs:1054-1059 [v2.10.3.13]):
//   unscaled: thresh = pow(10, dB / 20)
//   scaled:   thresh = pow(10, dB / 20) * voxGainScalar   (when micBoost==true)
//
// At the test fixture baseline: dB = -40, voxGainScalar = 2.0
//   unscaled = pow(10, -40/20) = pow(10, -2) = 0.01
//   scaled   = 0.01 * 2.0 = 0.02
//
// QSignalSpy is used to observe MoxController::voxThresholdRequested(double)
// without instantiating a TxChannel.  The signal fires regardless of whether
// a TxChannel exists — MoxController emits and RadioModel forwards via a lambda.

#include <QtTest/QtTest>
#include <QApplication>
#include <QCheckBox>
#include <QGroupBox>
#include <QRadioButton>
#include <QSignalSpy>

#include "core/AppSettings.h"
#include "core/MoxController.h"
#include "core/HpsdrModel.h"
#include "gui/setup/AudioTxInputPage.h"
#include "models/RadioModel.h"
#include "models/TransmitModel.h"

using namespace NereusSDR;

namespace {

// Helper: find the first QCheckBox with the given text inside a parent widget.
QCheckBox* findCheckBox(QWidget* parent, const QString& text)
{
    const auto boxes = parent->findChildren<QCheckBox*>();
    for (QCheckBox* cb : boxes) {
        if (cb->text() == text) { return cb; }
    }
    return nullptr;
}

// Helper: find the first QRadioButton with the given text inside a parent widget.
QRadioButton* findRadioButton(QWidget* parent, const QString& text)
{
    const auto buttons = parent->findChildren<QRadioButton*>();
    for (QRadioButton* btn : buttons) {
        if (btn->text() == text) { return btn; }
    }
    return nullptr;
}

// Fixture baseline: -40 dB threshold, 2.0 gain scalar.
// With boost off:  pow(10, -40/20)       = 0.01
// With boost on:   pow(10, -40/20) * 2.0 = 0.02
constexpr int   kTestThresholdDb  = -40;
constexpr float kTestGainScalar   = 2.0f;
constexpr double kExpectedUnscaled = 0.01;   // pow(10, -40/20)
constexpr double kExpectedScaled   = 0.02;   // 0.01 * 2.0

// Setup a RadioModel with Hermes family (has mic jack, has Hermes Mic Boost)
// and override VOX threshold + gain scalar to the test baseline.
// Selects Radio Mic source so the Hermes group is visible.
void setupHermesBaselineForMicBoostTest(RadioModel& model, AudioTxInputPage& page)
{
    // Set test-fixture VOX parameters on the model BEFORE the page is shown.
    // These propagate to MoxController via the H.2 connections in RadioModel.
    model.transmitModel().setVoxThresholdDb(kTestThresholdDb);
    model.transmitModel().setVoxGainScalar(kTestGainScalar);

    // Start with micBoost=false so the two states (off→on) are distinct.
    model.transmitModel().setMicBoost(false);
    QApplication::processEvents();

    // Switch to Radio Mic so the Hermes group box is visible.
    QRadioButton* radioBtn = findRadioButton(&page, QStringLiteral("Radio Mic"));
    if (radioBtn) {
        radioBtn->setChecked(true);
        QApplication::processEvents();
    }
}

} // anonymous namespace

class TestAudioTxInputMicBoostVoxThresh : public QObject
{
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

    // ── 1. Default state: MoxController emits initial threshold at startup ────────
    //
    // The NAN sentinel (m_lastVoxThresholdEmitted = NaN) guarantees at least
    // one voxThresholdRequested emit on the first recomputeVoxThreshold() call,
    // which happens when setVoxThreshold / setVoxGainScalar are called on the
    // MoxController.  Verifies the spy receives at least one value before any
    // checkbox interaction.

    void defaultState_initialEmit_primesMoxController()
    {
        RadioModel model;
        model.setCapsHasMicJackForTest(true);
        model.setCapsHwForTest(HPSDRHW::Hermes);

        MoxController* mox = model.moxController();
        QVERIFY2(mox, "MoxController must be non-null");

        QSignalSpy spy(mox, &MoxController::voxThresholdRequested);

        // Setting the threshold triggers recomputeVoxThreshold → emit (NAN sentinel).
        model.transmitModel().setVoxThresholdDb(kTestThresholdDb);
        model.transmitModel().setVoxGainScalar(kTestGainScalar);
        QApplication::processEvents();

        // At least one emit must have occurred (NAN sentinel → first-call emit).
        QVERIFY2(!spy.isEmpty(),
                 "voxThresholdRequested must fire at least once on initial parameter set");

        // The last emitted value must be the scaled one because MoxController
        // starts with m_micBoost=true (default: console.cs:13237 [v2.10.3.13]).
        const double lastValue = spy.last().at(0).toDouble();
        QVERIFY2(qFuzzyCompare(lastValue, kExpectedScaled),
                 qPrintable(QString("Last emitted threshold must be %1 (scaled), got %2")
                            .arg(kExpectedScaled).arg(lastValue)));
    }

    // ── 2. UI checkbox toggle ON (boost off → on): voxThresholdRequested fires ────
    //
    // Arrangement: micBoost=false (unscaled). Check the Hermes +20 dB Mic Boost
    // checkbox via the page widget.
    // Expectation: voxThresholdRequested emits with the scaled value (0.02).

    void hermes_checkboxToggleOn_emitsScaledThreshold()
    {
        RadioModel model;
        model.setCapsHasMicJackForTest(true);
        model.setCapsHwForTest(HPSDRHW::Hermes);
        AudioTxInputPage page(&model);

        setupHermesBaselineForMicBoostTest(model, page);

        MoxController* mox = model.moxController();
        QVERIFY2(mox, "MoxController must be non-null");

        QGroupBox* grp = page.hermesRadioMicGroup();
        QVERIFY2(grp, "Hermes radio mic group must exist");
        QCheckBox* boostChk = findCheckBox(grp, QStringLiteral("+20 dB Mic Boost"));
        QVERIFY2(boostChk, "+20 dB Mic Boost checkbox not found in Hermes group");
        QVERIFY2(!boostChk->isChecked(), "Checkbox must start unchecked (micBoost=false)");

        // Spy after baseline is established.
        QSignalSpy spy(mox, &MoxController::voxThresholdRequested);

        // Toggle ON via the UI checkbox.
        boostChk->setChecked(true);
        QApplication::processEvents();

        QVERIFY2(!spy.isEmpty(),
                 "voxThresholdRequested must fire when Hermes Mic Boost checkbox is checked");

        const double emitted = spy.last().at(0).toDouble();
        QVERIFY2(qFuzzyCompare(emitted, kExpectedScaled),
                 qPrintable(QString("Expected scaled threshold %1, got %2")
                            .arg(kExpectedScaled).arg(emitted)));
    }

    // ── 3. UI checkbox toggle OFF (boost on → off): voxThresholdRequested fires ──
    //
    // Arrangement: micBoost=true (scaled). Uncheck the Hermes +20 dB Mic Boost
    // checkbox via the page widget.
    // Expectation: voxThresholdRequested emits with the unscaled value (0.01).

    void hermes_checkboxToggleOff_emitsUnscaledThreshold()
    {
        RadioModel model;
        model.setCapsHasMicJackForTest(true);
        model.setCapsHwForTest(HPSDRHW::Hermes);
        AudioTxInputPage page(&model);

        // Start with boost ON.
        model.transmitModel().setVoxThresholdDb(kTestThresholdDb);
        model.transmitModel().setVoxGainScalar(kTestGainScalar);
        model.transmitModel().setMicBoost(true);
        QApplication::processEvents();

        // Switch to Radio Mic so the group box is visible.
        QRadioButton* radioBtn = findRadioButton(&page, QStringLiteral("Radio Mic"));
        if (radioBtn) {
            radioBtn->setChecked(true);
            QApplication::processEvents();
        }

        QGroupBox* grp = page.hermesRadioMicGroup();
        QVERIFY2(grp, "Hermes radio mic group must exist");
        QCheckBox* boostChk = findCheckBox(grp, QStringLiteral("+20 dB Mic Boost"));
        QVERIFY2(boostChk, "+20 dB Mic Boost checkbox not found in Hermes group");
        QVERIFY2(boostChk->isChecked(), "Checkbox must start checked (micBoost=true)");

        // Spy after baseline is established.
        MoxController* mox = model.moxController();
        QVERIFY2(mox, "MoxController must be non-null");
        QSignalSpy spy(mox, &MoxController::voxThresholdRequested);

        // Toggle OFF via the UI checkbox.
        boostChk->setChecked(false);
        QApplication::processEvents();

        QVERIFY2(!spy.isEmpty(),
                 "voxThresholdRequested must fire when Hermes Mic Boost checkbox is unchecked");

        const double emitted = spy.last().at(0).toDouble();
        QVERIFY2(qFuzzyCompare(emitted, kExpectedUnscaled),
                 qPrintable(QString("Expected unscaled threshold %1, got %2")
                            .arg(kExpectedUnscaled).arg(emitted)));
    }

    // ── 4. Model setMicBoost(true) → UI checkbox reflects the change ─────────────
    //
    // Verifies the bidirectional sync: model → UI (already covered by I.3 test 10
    // for no-feedback-loop, re-verified here in integration context).

    void modelSetMicBoostTrue_hermesCheckboxChecked()
    {
        RadioModel model;
        model.setCapsHasMicJackForTest(true);
        model.setCapsHwForTest(HPSDRHW::Hermes);
        AudioTxInputPage page(&model);

        // Ensure boost is off first.
        model.transmitModel().setMicBoost(false);
        QApplication::processEvents();

        // Switch to Radio Mic so the group is visible.
        QRadioButton* radioBtn = findRadioButton(&page, QStringLiteral("Radio Mic"));
        if (radioBtn) {
            radioBtn->setChecked(true);
            QApplication::processEvents();
        }

        QGroupBox* grp = page.hermesRadioMicGroup();
        QVERIFY2(grp, "Hermes radio mic group must exist");
        QCheckBox* boostChk = findCheckBox(grp, QStringLiteral("+20 dB Mic Boost"));
        QVERIFY2(boostChk, "+20 dB Mic Boost checkbox not found");
        QVERIFY2(!boostChk->isChecked(), "Checkbox must start unchecked");

        // Set via model.
        model.transmitModel().setMicBoost(true);
        QApplication::processEvents();

        QVERIFY2(boostChk->isChecked(),
                 "Hermes Mic Boost checkbox must be checked after setMicBoost(true)");
    }

    // ── 5. Idempotency: toggle boost ON twice → only one voxThresholdRequested ───
    //
    // The MoxController::recomputeVoxThreshold() uses qFuzzyCompare to suppress
    // spurious re-emits when the computed value is unchanged.
    // If micBoost is already true and setMicBoost(true) is called again, the
    // computed threshold is identical → no second emit.

    void hermesBoostToggleOnTwice_idempotent_singleEmit()
    {
        RadioModel model;
        model.setCapsHasMicJackForTest(true);
        model.setCapsHwForTest(HPSDRHW::Hermes);
        AudioTxInputPage page(&model);

        setupHermesBaselineForMicBoostTest(model, page);

        MoxController* mox = model.moxController();
        QVERIFY2(mox, "MoxController must be non-null");

        QGroupBox* grp = page.hermesRadioMicGroup();
        QVERIFY2(grp, "Hermes radio mic group must exist");
        QCheckBox* boostChk = findCheckBox(grp, QStringLiteral("+20 dB Mic Boost"));
        QVERIFY2(boostChk, "+20 dB Mic Boost checkbox not found");

        // First toggle ON.
        boostChk->setChecked(true);
        QApplication::processEvents();

        // Spy after first toggle so we count only subsequent emits.
        QSignalSpy spy(mox, &MoxController::voxThresholdRequested);

        // Second toggle ON (already checked; TransmitModel setMicBoost is idempotent
        // so micBoostChanged won't emit if value is unchanged, meaning
        // onMicBoostChanged won't be called and no second voxThresholdRequested fires).
        boostChk->setChecked(true);
        QApplication::processEvents();

        QCOMPARE(spy.count(), 0);
    }

    // ── 6. Cross-family parity (Orion): Orion checkbox triggers recompute ─────────
    //
    // The RadioModel H.2 wiring (micBoostChanged → onMicBoostChanged) is shared
    // across all families — the checkbox is different but the signal chain is
    // identical. Verifies the integration works regardless of family group.

    void orion_checkboxToggle_triggersVoxThresholdRecompute()
    {
        RadioModel model;
        model.setCapsHasMicJackForTest(true);
        model.setCapsHwForTest(HPSDRHW::OrionMKII);
        AudioTxInputPage page(&model);

        // Set test-fixture VOX parameters; start with boost off.
        model.transmitModel().setVoxThresholdDb(kTestThresholdDb);
        model.transmitModel().setVoxGainScalar(kTestGainScalar);
        model.transmitModel().setMicBoost(false);
        QApplication::processEvents();

        // Switch to Radio Mic so the Orion group is visible.
        QRadioButton* radioBtn = findRadioButton(&page, QStringLiteral("Radio Mic"));
        if (radioBtn) {
            radioBtn->setChecked(true);
            QApplication::processEvents();
        }

        MoxController* mox = model.moxController();
        QVERIFY2(mox, "MoxController must be non-null");

        QGroupBox* grp = page.orionRadioMicGroup();
        QVERIFY2(grp, "Orion radio mic group must exist");
        QCheckBox* boostChk = findCheckBox(grp, QStringLiteral("+20 dB Mic Boost"));
        QVERIFY2(boostChk, "+20 dB Mic Boost checkbox not found in Orion group");
        QVERIFY2(!boostChk->isChecked(), "Orion Mic Boost checkbox must start unchecked");

        QSignalSpy spy(mox, &MoxController::voxThresholdRequested);

        // Toggle ON via the Orion checkbox.
        boostChk->setChecked(true);
        QApplication::processEvents();

        QVERIFY2(!spy.isEmpty(),
                 "voxThresholdRequested must fire when Orion Mic Boost checkbox is checked");

        const double emitted = spy.last().at(0).toDouble();
        QVERIFY2(qFuzzyCompare(emitted, kExpectedScaled),
                 qPrintable(QString("Orion: expected scaled threshold %1, got %2")
                            .arg(kExpectedScaled).arg(emitted)));
    }

    // ── 7. Cross-family parity (Saturn): Saturn checkbox triggers recompute ───────
    //
    // Same as case 6 but for the Saturn G2 family group box.

    void saturn_checkboxToggle_triggersVoxThresholdRecompute()
    {
        RadioModel model;
        model.setCapsHasMicJackForTest(true);
        model.setCapsHwForTest(HPSDRHW::Saturn);
        AudioTxInputPage page(&model);

        // Set test-fixture VOX parameters; start with boost off.
        model.transmitModel().setVoxThresholdDb(kTestThresholdDb);
        model.transmitModel().setVoxGainScalar(kTestGainScalar);
        model.transmitModel().setMicBoost(false);
        QApplication::processEvents();

        // Switch to Radio Mic so the Saturn group is visible.
        QRadioButton* radioBtn = findRadioButton(&page, QStringLiteral("Radio Mic"));
        if (radioBtn) {
            radioBtn->setChecked(true);
            QApplication::processEvents();
        }

        MoxController* mox = model.moxController();
        QVERIFY2(mox, "MoxController must be non-null");

        QGroupBox* grp = page.saturnRadioMicGroup();
        QVERIFY2(grp, "Saturn radio mic group must exist");
        QCheckBox* boostChk = findCheckBox(grp, QStringLiteral("+20 dB Mic Boost"));
        QVERIFY2(boostChk, "+20 dB Mic Boost checkbox not found in Saturn group");
        QVERIFY2(!boostChk->isChecked(), "Saturn Mic Boost checkbox must start unchecked");

        QSignalSpy spy(mox, &MoxController::voxThresholdRequested);

        // Toggle ON via the Saturn checkbox.
        boostChk->setChecked(true);
        QApplication::processEvents();

        QVERIFY2(!spy.isEmpty(),
                 "voxThresholdRequested must fire when Saturn Mic Boost checkbox is checked");

        const double emitted = spy.last().at(0).toDouble();
        QVERIFY2(qFuzzyCompare(emitted, kExpectedScaled),
                 qPrintable(QString("Saturn: expected scaled threshold %1, got %2")
                            .arg(kExpectedScaled).arg(emitted)));
    }
};

QTEST_MAIN(TestAudioTxInputMicBoostVoxThresh)
#include "tst_audio_tx_input_mic_boost_vox_thresh.moc"
