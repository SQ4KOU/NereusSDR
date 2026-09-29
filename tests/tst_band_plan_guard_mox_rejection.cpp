// =================================================================
// tests/tst_band_plan_guard_mox_rejection.cpp  (NereusSDR)
// =================================================================
//
// NereusSDR-original test. No Thetis port at this layer.
// Phase 3M-1b Task K.2: MoxController::setMox(true) rejection path.
//
// Covers:
//   1. MoxCheckFn callback: CW mode → moxRejected("CW transmit is not available yet");
//      MOX state stays Rx.
//   2. AM mode → accepted (AM/SAM/DSB TX via WDSP ammod);
//      MOX state stays Rx.
//   3. LSB mode (allowed) → MOX engages normally; moxRejected NOT emitted.
//   4. No MoxCheckFn installed → setMox(true) succeeds (backwards-compat).
//   5. setMox(false) is never rejected — release path bypasses BandPlanGuard.
//   6. SPEC mode → moxRejected("This mode cannot transmit."); MOX stays Rx.
//   7. Rejection: no state advance, no phase signals (txAboutToBegin not emitted).
//   8. After rejection, setMox(true) with CW can be re-attempted; still rejects.
//
// Additionally tests TxApplet::tooltipForMode static helper:
//   9.  USB → normal tooltip.
//  10.  LSB → normal tooltip.
//  11.  DIGL → normal tooltip.
//  12.  DIGU → normal tooltip.
//  13.  CWL → CW deferred tooltip.
//  14.  CWU → CW deferred tooltip.
//  15.  AM  → audio modes deferred tooltip.
//  16.  FM  → audio modes deferred tooltip.
//  17.  SAM → audio modes deferred tooltip.
//  18.  DSB → audio modes deferred tooltip.
//  19.  DRM → audio modes deferred tooltip.
//  20.  SPEC → not-supported tooltip.
// =================================================================
//
// Modification history (NereusSDR):
//   2026-04-28 — Original test for NereusSDR by J.J. Boyd (KG4VCF),
//                 with AI-assisted implementation via Anthropic Claude Code.
//                 Task: Phase 3M-1b Task K.2 — MOX rejection signal +
//                 status-bar toast + TxApplet tooltip override. Closes Phase K.
//   2026-09-22 : R-R3-36 Task 7 by J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code. The RadioModel band-plan case keys
//                 from the radio mic; new cases pin the pre-check order
//                 (remote, band plan, PC microphone).
//   2026-09-28 : Addendum G-42 by J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code. Extended is the Core's
//                 ExtendedTransmit setting; the old ExtendedTxAllowed is
//                 ignored. Item 4: band plan refusals in operator words.
// =================================================================

// no-port-check: NereusSDR-original test file.

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QSignalSpy>

#include "core/MoxController.h"
#include "core/AppSettings.h"
#include "core/TxSliceArbiter.h"
#include "core/safety/BandPlanGuard.h"
#include "core/WdspTypes.h"
#include "gui/applets/TxApplet.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"

using namespace NereusSDR;
using namespace NereusSDR::safety;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static constexpr Region   kRegion  = Region::UnitedStates;
static constexpr std::int64_t kFreqHz = 14'200'000; // US 20m, well in-band
static constexpr Band     kBand20m = Band::Band20m;

/// Build a MoxCheckFn that always returns {ok=false, reason} for the given mode.
/// Simulates RadioModel's lambda: calls checkMoxAllowed with the given mode.
static MoxController::MoxCheckFn makeCheckFn(DSPMode mode)
{
    return [mode]() -> BandPlanGuard::MoxCheckResult {
        BandPlanGuard guard;
        return guard.checkMoxAllowed(
            kRegion, kFreqHz, mode,
            kBand20m, kBand20m,
            /*preventDifferentBand=*/false,
            /*extended=*/false);
    };
}

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class TestBandPlanGuardMoxRejection : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        AppSettings::instance().clear();
        AppSettings::instance().setValue(
            QStringLiteral("BandPlanRegion"),
            QString::number(static_cast<int>(Region::UnitedStates)));
    }

    void cleanup()
    {
        AppSettings::instance().clear();
        AppSettings::instance().setValue(
            QStringLiteral("BandPlanRegion"),
            QString::number(static_cast<int>(Region::UnitedStates)));
    }

    // ── 1. CW mode + setMox(true) → moxRejected("CW transmit is not available yet") ─

    void cwl_setMox_emitsMoxRejected()
    {
        MoxController ctrl;
        ctrl.setTimerIntervals(0, 0, 0, 0, 0, 0);
        ctrl.setMoxCheck(makeCheckFn(DSPMode::CWL));

        QSignalSpy rejectedSpy(&ctrl, &MoxController::moxRejected);
        QSignalSpy stateChangedSpy(&ctrl, &MoxController::moxStateChanged);

        ctrl.setMox(true);
        QCoreApplication::processEvents();

        QCOMPARE(rejectedSpy.count(), 1);
        QCOMPARE(rejectedSpy.at(0).at(0).toString(),
                 QStringLiteral("CW transmit is not available yet"));
        // MOX state must NOT have advanced.
        QVERIFY(!ctrl.isMox());
        QCOMPARE(ctrl.state(), MoxState::Rx);
        QCOMPARE(stateChangedSpy.count(), 0);
    }

    void cwu_setMox_emitsMoxRejected()
    {
        MoxController ctrl;
        ctrl.setTimerIntervals(0, 0, 0, 0, 0, 0);
        ctrl.setMoxCheck(makeCheckFn(DSPMode::CWU));

        QSignalSpy rejectedSpy(&ctrl, &MoxController::moxRejected);

        ctrl.setMox(true);
        QCoreApplication::processEvents();

        QCOMPARE(rejectedSpy.count(), 1);
        QCOMPARE(rejectedSpy.at(0).at(0).toString(),
                 QStringLiteral("CW transmit is not available yet"));
        QVERIFY(!ctrl.isMox());
    }

    // ── 2. AM mode → accepted (AM/SAM/DSB TX via WDSP ammod) ─────────────────

    void am_setMox_isAccepted()
    {
        MoxController ctrl;
        ctrl.setTimerIntervals(0, 0, 0, 0, 0, 0);
        ctrl.setMoxCheck(makeCheckFn(DSPMode::AM));

        QSignalSpy rejectedSpy(&ctrl, &MoxController::moxRejected);

        ctrl.setMox(true);
        QCoreApplication::processEvents();

        QCOMPARE(rejectedSpy.count(), 0);
        QVERIFY(ctrl.isMox());
    }

    void fm_setMox_emitsMoxRejected()
    {
        MoxController ctrl;
        ctrl.setTimerIntervals(0, 0, 0, 0, 0, 0);
        ctrl.setMoxCheck(makeCheckFn(DSPMode::FM));

        QSignalSpy rejectedSpy(&ctrl, &MoxController::moxRejected);

        ctrl.setMox(true);
        QCoreApplication::processEvents();

        QCOMPARE(rejectedSpy.count(), 1);
        QCOMPARE(rejectedSpy.at(0).at(0).toString(),
                 QStringLiteral("FM transmit is not available yet"));
        QVERIFY(!ctrl.isMox());
    }

    // ── 3. LSB mode (allowed) → MOX engages; moxRejected NOT emitted ───────────

    void lsb_setMox_engagesNormally()
    {
        MoxController ctrl;
        ctrl.setTimerIntervals(0, 0, 0, 0, 0, 0);
        ctrl.setMoxCheck(makeCheckFn(DSPMode::LSB));

        QSignalSpy rejectedSpy(&ctrl, &MoxController::moxRejected);
        QSignalSpy moxChangedSpy(&ctrl, &MoxController::moxStateChanged);

        ctrl.setMox(true);
        QCoreApplication::processEvents();

        QCOMPARE(rejectedSpy.count(), 0);
        QCOMPARE(moxChangedSpy.count(), 1);
        QVERIFY(ctrl.isMox());
    }

    void usb_setMox_engagesNormally()
    {
        MoxController ctrl;
        ctrl.setTimerIntervals(0, 0, 0, 0, 0, 0);
        ctrl.setMoxCheck(makeCheckFn(DSPMode::USB));

        QSignalSpy rejectedSpy(&ctrl, &MoxController::moxRejected);

        ctrl.setMox(true);
        QCoreApplication::processEvents();

        QCOMPARE(rejectedSpy.count(), 0);
        QVERIFY(ctrl.isMox());
    }

    // ── 4. No MoxCheckFn installed → setMox(true) succeeds (backwards-compat) ──

    void noCheckFn_setMox_succeedsByDefault()
    {
        MoxController ctrl;
        ctrl.setTimerIntervals(0, 0, 0, 0, 0, 0);
        // No setMoxCheck() call — m_moxCheck is empty/null.

        QSignalSpy rejectedSpy(&ctrl, &MoxController::moxRejected);
        QSignalSpy moxChangedSpy(&ctrl, &MoxController::moxStateChanged);

        ctrl.setMox(true);
        QCoreApplication::processEvents();

        QCOMPARE(rejectedSpy.count(), 0);
        QCOMPARE(moxChangedSpy.count(), 1);
        QVERIFY(ctrl.isMox());
    }

    // ── 5. setMox(false) never rejected — release path bypasses BandPlanGuard ──

    void setMoxFalse_neverRejected()
    {
        MoxController ctrl;
        ctrl.setTimerIntervals(0, 0, 0, 0, 0, 0);

        // Engage MOX without a check (no check fn installed yet).
        ctrl.setMox(true);
        QCoreApplication::processEvents();
        QVERIFY(ctrl.isMox());

        // Now install a CW check fn (which would reject setMox(true) if re-engaged).
        ctrl.setMoxCheck(makeCheckFn(DSPMode::CWL));

        QSignalSpy rejectedSpy(&ctrl, &MoxController::moxRejected);

        // setMox(false) must succeed regardless of the installed check fn.
        ctrl.setMox(false);
        QCoreApplication::processEvents();

        QCOMPARE(rejectedSpy.count(), 0);
        QVERIFY(!ctrl.isMox());
    }

    // ── 6. SPEC mode → moxRejected("This mode cannot transmit.") ──────────────

    void spec_setMox_emitsMoxRejected()
    {
        MoxController ctrl;
        ctrl.setTimerIntervals(0, 0, 0, 0, 0, 0);
        ctrl.setMoxCheck(makeCheckFn(DSPMode::SPEC));

        QSignalSpy rejectedSpy(&ctrl, &MoxController::moxRejected);

        ctrl.setMox(true);
        QCoreApplication::processEvents();

        QCOMPARE(rejectedSpy.count(), 1);
        QCOMPARE(rejectedSpy.at(0).at(0).toString(),
                 QStringLiteral("This mode cannot transmit."));
        QVERIFY(!ctrl.isMox());
    }

    // ── 7. Rejection: no phase signals (txAboutToBegin NOT emitted) ─────────────

    void rejection_doesNotEmitPhaseSignals()
    {
        MoxController ctrl;
        ctrl.setTimerIntervals(0, 0, 0, 0, 0, 0);
        ctrl.setMoxCheck(makeCheckFn(DSPMode::CWL));

        QSignalSpy txAboutToBeginSpy(&ctrl, &MoxController::txAboutToBegin);
        QSignalSpy hardwareFlippedSpy(&ctrl, &MoxController::hardwareFlipped);
        QSignalSpy stateChangedSpy(&ctrl,   &MoxController::stateChanged);

        ctrl.setMox(true);
        QCoreApplication::processEvents();

        QCOMPARE(txAboutToBeginSpy.count(), 0);
        QCOMPARE(hardwareFlippedSpy.count(), 0);
        QCOMPARE(stateChangedSpy.count(), 0);
    }

    // ── 8. After rejection, re-attempting still rejects ─────────────────────────

    void rejection_isRepeatable()
    {
        MoxController ctrl;
        ctrl.setTimerIntervals(0, 0, 0, 0, 0, 0);
        ctrl.setMoxCheck(makeCheckFn(DSPMode::CWL));

        QSignalSpy rejectedSpy(&ctrl, &MoxController::moxRejected);

        ctrl.setMox(true);
        QCoreApplication::processEvents();
        QCOMPARE(rejectedSpy.count(), 1);

        ctrl.setMox(true);
        QCoreApplication::processEvents();
        QCOMPARE(rejectedSpy.count(), 2);  // second attempt also rejected

        QVERIFY(!ctrl.isMox());
    }

    void radioModelMoxCheckUsesTheTxBoundSliceInBothLegalityDirections()
    {
        RadioModel model;
        model.configureStreamPool(/*userDdcCount=*/5, /*maxSlices=*/5, 192000);
        model.moxController()->setTimerIntervals(0, 0, 0, 0, 0, 0);
        model.installBandPlanMoxCheckForTest();
        // This case is about the band plan: key from the radio mic so the
        // R-R3-36 PC-microphone admission (capture is Closed here) stays
        // out of the way.
        model.transmitModel().setMicSource(MicSource::Radio);

        const int aId = model.addSlice();
        SliceModel* const a = model.sliceById(aId);
        QVERIFY(a);
        a->setDspMode(DSPMode::USB);
        a->setFrequency(14'200'000.0);

        model.addSlice();
        const int cId = model.addSlice();
        SliceModel* const c = model.sliceById(cId);
        QVERIFY(c);
        c->setDspMode(DSPMode::USB);
        c->setFrequency(4'500'000.0);

        QVERIFY(model.setActiveSliceById(aId));
        QVERIFY(model.txSliceArbiter()->requestHandoff(cId));

        QSignalSpy rejectedSpy(model.moxController(), &MoxController::moxRejected);
        model.moxController()->setMox(true);
        QCoreApplication::processEvents();

        QCOMPARE(rejectedSpy.count(), 1);
        QCOMPARE(rejectedSpy.at(0).at(0).toString(),
                 QStringLiteral("4.500000 MHz with the transmit filter from 100 to 2900 Hz "
                                "reaches outside the transmit bands for your region "
                                "(United States)."));
        QVERIFY(!model.moxController()->isMox());

        a->setFrequency(4'500'000.0);
        c->setFrequency(7'100'000.0);
        rejectedSpy.clear();

        model.moxController()->setMox(true);
        QCoreApplication::processEvents();

        QCOMPARE(rejectedSpy.count(), 0);
        QVERIFY(model.moxController()->isMox());
        model.moxController()->setMox(false);
        QCoreApplication::processEvents();
    }

    // ── R-R3-36: PC-microphone admission follows the band plan ─────────────
    //
    // The pre-check keeps its order: remote refusal, then band plan, then
    // the PC microphone. With PC mic selected and capture not Ready, an
    // out-of-band request is refused for the band plan, an in-band one for
    // the microphone, and neither advances the state machine.
    void radioModelMoxCheckRefusesPcMicAfterTheBandPlan()
    {
        RadioModel model;
        model.configureStreamPool(/*userDdcCount=*/5, /*maxSlices=*/5, 192000);
        model.moxController()->setTimerIntervals(0, 0, 0, 0, 0, 0);
        model.installBandPlanMoxCheckForTest();
        QCOMPARE(model.transmitModel().micSource(), MicSource::Pc);
        QVERIFY(model.pcCaptureRequired());

        const int aId = model.addSlice();
        SliceModel* const a = model.sliceById(aId);
        QVERIFY(a);
        a->setDspMode(DSPMode::USB);
        a->setFrequency(4'500'000.0);
        QVERIFY(model.setActiveSliceById(aId));

        QSignalSpy rejectedSpy(model.moxController(), &MoxController::moxRejected);
        QSignalSpy txAboutToBeginSpy(model.moxController(), &MoxController::txAboutToBegin);
        QSignalSpy hardwareFlippedSpy(model.moxController(), &MoxController::hardwareFlipped);
        QSignalSpy stateChangedSpy(model.moxController(), &MoxController::stateChanged);

        model.moxController()->setMox(true);
        QCoreApplication::processEvents();
        QCOMPARE(rejectedSpy.count(), 1);
        QCOMPARE(rejectedSpy.at(0).at(0).toString(),
                 QStringLiteral("4.500000 MHz with the transmit filter from 100 to 2900 Hz "
                                "reaches outside the transmit bands for your region "
                                "(United States)."));

        a->setFrequency(14'200'000.0);
        model.moxController()->setMox(true);
        QCoreApplication::processEvents();
        QCOMPARE(rejectedSpy.count(), 2);
        QCOMPARE(rejectedSpy.at(1).at(0).toString(),
                 QStringLiteral("Microphone is not ready. Check Audio settings and retry."));

        QVERIFY(!model.moxController()->isMox());
        QCOMPARE(model.moxController()->state(), MoxState::Rx);
        QCOMPARE(txAboutToBeginSpy.count(), 0);
        QCOMPARE(hardwareFlippedSpy.count(), 0);
        QCOMPARE(stateChangedSpy.count(), 0);
    }

    void radioModelChecksTransmitFilterEdges_data()
    {
        QTest::addColumn<int>("mode");
        QTest::addColumn<double>("carrier");
        QTest::addColumn<bool>("allowed");
        QTest::newRow("usb-upper-crossing") << int(DSPMode::USB) << 14349000.0 << false;
        QTest::newRow("usb-upper-inside") << int(DSPMode::USB) << 14347000.0 << true;
        QTest::newRow("lsb-lower-crossing") << int(DSPMode::LSB) << 14001000.0 << false;
        QTest::newRow("lsb-lower-inside") << int(DSPMode::LSB) << 14003000.0 << true;
        QTest::newRow("am-upper-crossing") << int(DSPMode::AM) << 14349000.0 << false;
        QTest::newRow("am-lower-crossing") << int(DSPMode::AM) << 14001000.0 << false;
        QTest::newRow("rade-upper-crossing") << int(DSPMode::RADE_U) << 14349000.0 << false;
        QTest::newRow("rade-lower-crossing") << int(DSPMode::RADE_L) << 14001000.0 << false;
    }

    void radioModelChecksTransmitFilterEdges()
    {
        QFETCH(int, mode);
        QFETCH(double, carrier);
        QFETCH(bool, allowed);
        RadioModel model;
        model.configureStreamPool(5, 5, 192000);
        model.moxController()->setTimerIntervals(0, 0, 0, 0, 0, 0);
        model.installBandPlanMoxCheckForTest();
        model.transmitModel().setMicSource(MicSource::Radio);
        SliceModel* slice = model.sliceById(model.addSlice());
        QVERIFY(slice);
        slice->setDspMode(static_cast<DSPMode>(mode));
        slice->setFrequency(carrier);
        model.transmitModel().setFilterLow(100);
        model.transmitModel().setFilterHigh(2900);
        QSignalSpy rejected(model.moxController(), &MoxController::moxRejected);
        model.moxController()->setMox(true);
        QCoreApplication::processEvents();
        QCOMPARE(model.moxController()->isMox(), allowed);
        QCOMPARE(rejected.size(), allowed ? 0 : 1);
        model.moxController()->setMox(false);
        QCoreApplication::processEvents();
    }

    // Addendum G-42 (JJ's ruling 2026-09-28): Extended is one Core
    // setting, ExtendedTransmit, read at every key. Thetis's
    // CheckValidTXFreq returns true while it is on (console.cs:6780
    // [v2.10.3.15]), past the band edges, the filter edges and the US 60 m
    // mode rule. An old saved ExtendedTxAllowed never turns it on, and
    // only exactly "True" does.
    void extendedTransmitIsTheCoresSettingAndTheOldKeyIsIgnored_data()
    {
        QTest::addColumn<int>("mode");
        QTest::addColumn<double>("carrier");
        QTest::newRow("usb-carrier-above-20m") << int(DSPMode::USB) << 14360000.0;
        QTest::newRow("usb-filter-edge-above-20m") << int(DSPMode::USB) << 14349000.0;
        QTest::newRow("lsb-filter-edge-below-20m") << int(DSPMode::LSB) << 14001000.0;
        QTest::newRow("am-on-us-60m") << int(DSPMode::AM) << 5357000.0;
    }

    void extendedTransmitIsTheCoresSettingAndTheOldKeyIsIgnored()
    {
        QFETCH(int, mode);
        QFETCH(double, carrier);
        RadioModel model;
        model.configureStreamPool(5, 5, 192000);
        model.moxController()->setTimerIntervals(0, 0, 0, 0, 0, 0);
        model.installBandPlanMoxCheckForTest();
        model.transmitModel().setMicSource(MicSource::Radio);
        SliceModel* slice = model.sliceById(model.addSlice());
        QVERIFY(slice);
        slice->setDspMode(static_cast<DSPMode>(mode));
        slice->setFrequency(carrier);
        model.transmitModel().setFilterLow(100);
        model.transmitModel().setFilterHigh(2900);

        const auto keyed = [&model]() {
            model.moxController()->setMox(true);
            QCoreApplication::processEvents();
            const bool on = model.moxController()->isMox();
            model.moxController()->setMox(false);
            QCoreApplication::processEvents();
            return on;
        };
        auto& settings = AppSettings::instance();
        // Off by default.
        QVERIFY(!keyed());
        // The old per-computer key is ignored.
        settings.setValue(QStringLiteral("ExtendedTxAllowed"), QStringLiteral("True"));
        QVERIFY(!keyed());
        settings.setValue(QStringLiteral("ExtendedTransmit"), QStringLiteral("true"));
        QVERIFY(!keyed());
        settings.setValue(QStringLiteral("ExtendedTransmit"), QStringLiteral("True"));
        QVERIFY(keyed());
        settings.setValue(QStringLiteral("ExtendedTransmit"), QStringLiteral("False"));
        QVERIFY(!keyed());
        settings.remove(QStringLiteral("ExtendedTransmit"));
        QVERIFY(!keyed());
    }

    void invalidStoredRegionCannotWrapIntoAnAllowedRegion_data()
    {
        QTest::addColumn<QString>("region");
        QTest::newRow("not-an-integer") << QStringLiteral("invalid");
        QTest::newRow("fraction") << QStringLiteral("8.5");
        QTest::newRow("positive-wrap") << QStringLiteral("264");
        QTest::newRow("negative-wrap") << QStringLiteral("-248");
        QTest::newRow("empty") << QString();
    }

    void invalidStoredRegionCannotWrapIntoAnAllowedRegion()
    {
        QFETCH(QString, region);
        AppSettings::instance().setValue(QStringLiteral("BandPlanRegion"), region);
        RadioModel model;
        model.configureStreamPool(5, 5, 192000);
        model.moxController()->setTimerIntervals(0, 0, 0, 0, 0, 0);
        model.installBandPlanMoxCheckForTest();
        model.transmitModel().setMicSource(MicSource::Radio);
        SliceModel* slice = model.sliceById(model.addSlice());
        QVERIFY(slice);
        slice->setDspMode(DSPMode::USB);
        slice->setFrequency(14200000.0);
        QSignalSpy rejected(model.moxController(), &MoxController::moxRejected);
        model.moxController()->setMox(true);
        QCoreApplication::processEvents();
        QVERIFY(!model.moxController()->isMox());
        QCOMPARE(rejected.size(), 1);
        QCOMPARE(rejected.at(0).at(0).toString(), QStringLiteral("The transmit region setting is invalid."));
    }

    // The remote refusal is unchanged and still comes first.
    void remoteRefusalPrecedesPcMicAdmission()
    {
        RadioModel model(RadioModel::Role::Remote);
        model.moxController()->setTimerIntervals(0, 0, 0, 0, 0, 0);
        QCOMPARE(model.transmitModel().micSource(), MicSource::Pc);

        QSignalSpy rejectedSpy(model.moxController(), &MoxController::moxRejected);
        model.moxController()->setMox(true);
        QCoreApplication::processEvents();

        QCOMPARE(rejectedSpy.count(), 1);
        QCOMPARE(rejectedSpy.at(0).at(0).toString(),
                 QStringLiteral("Remote transmit controls are not available "
                                "from this Core yet."));
        QVERIFY(!model.moxController()->isMox());
    }

    // ── 9-20: TxApplet::tooltipForMode static helper ────────────────────────────
    // These tests exercise the helper directly without constructing a full
    // TxApplet (which requires a RadioModel + Qt widgets).

    void tooltipForMode_usb_returnsNormal()
    {
        const QString tip = TxApplet::tooltipForMode(DSPMode::USB);
        QCOMPARE(tip, QStringLiteral("Manual transmit (MOX)"));
    }

    void tooltipForMode_lsb_returnsNormal()
    {
        const QString tip = TxApplet::tooltipForMode(DSPMode::LSB);
        QCOMPARE(tip, QStringLiteral("Manual transmit (MOX)"));
    }

    void tooltipForMode_digl_returnsNormal()
    {
        const QString tip = TxApplet::tooltipForMode(DSPMode::DIGL);
        QCOMPARE(tip, QStringLiteral("Manual transmit (MOX)"));
    }

    void tooltipForMode_digu_returnsNormal()
    {
        const QString tip = TxApplet::tooltipForMode(DSPMode::DIGU);
        QCOMPARE(tip, QStringLiteral("Manual transmit (MOX)"));
    }

    void tooltipForMode_cwl_returnsCwPhase()
    {
        const QString tip = TxApplet::tooltipForMode(DSPMode::CWL);
        QCOMPARE(tip, QStringLiteral("CW transmit is not available yet"));
    }

    void tooltipForMode_cwu_returnsCwPhase()
    {
        const QString tip = TxApplet::tooltipForMode(DSPMode::CWU);
        QCOMPARE(tip, QStringLiteral("CW transmit is not available yet"));
    }

    void tooltipForMode_am_returnsManualMox()
    {
        const QString tip = TxApplet::tooltipForMode(DSPMode::AM);
        QCOMPARE(tip, QStringLiteral("Manual transmit (MOX)"));
    }

    void tooltipForMode_fm_returnsAudioPhase()
    {
        const QString tip = TxApplet::tooltipForMode(DSPMode::FM);
        QCOMPARE(tip, QStringLiteral("FM transmit is not available yet"));
    }

    void tooltipForMode_sam_returnsManualMox()
    {
        const QString tip = TxApplet::tooltipForMode(DSPMode::SAM);
        QCOMPARE(tip, QStringLiteral("Manual transmit (MOX)"));
    }

    void tooltipForMode_dsb_returnsManualMox()
    {
        const QString tip = TxApplet::tooltipForMode(DSPMode::DSB);
        QCOMPARE(tip, QStringLiteral("Manual transmit (MOX)"));
    }

    void tooltipForMode_drm_returnsAudioPhase()
    {
        const QString tip = TxApplet::tooltipForMode(DSPMode::DRM);
        QCOMPARE(tip, QStringLiteral("DRM transmit is not available yet"));
    }

    void tooltipForMode_spec_returnsNotSupported()
    {
        const QString tip = TxApplet::tooltipForMode(DSPMode::SPEC);
        QCOMPARE(tip, QStringLiteral("This mode cannot transmit."));
    }
};

QTEST_GUILESS_MAIN(TestBandPlanGuardMoxRejection)
#include "tst_band_plan_guard_mox_rejection.moc"
