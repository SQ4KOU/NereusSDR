// tst_clarity_controller.cpp
//
// Phase 3G-9c — ClarityController unit tests. Covers the stateful wrapper
// around NoiseFloorEstimator: enable/disable gating, EWMA smoothing,
// emission on every changed floor, poll-rate throttling, TX pause, the
// level kept through a key, the hold after it, the calibration offset,
// manual override, threshold margin math, and the 30 dB min-gap clamp.
//
// Strict TDD: every test written first, verified RED, then minimal code
// to green. QSignalSpy is used to capture emissions.

#include <QtTest/QtTest>
#include <QSignalSpy>

#include "core/ClarityController.h"

#include <cmath>

using namespace NereusSDR;

class TestClarityController : public QObject {
    Q_OBJECT
private slots:

    void remoteFloorMatchesLocalEstimatorAndOperatorGates()
    {
        ClarityController local;
        ClarityController remote;
        QSignalSpy localThresholds(&local, &ClarityController::waterfallThresholdsChanged);
        QSignalSpy remoteThresholds(&remote, &ClarityController::waterfallThresholdsChanged);
        QSignalSpy localFloors(&local, &ClarityController::noiseFloorChanged);
        QSignalSpy remoteFloors(&remote, &ClarityController::noiseFloorChanged);
        const auto feed = [&](float floor, qint64 time) {
            // Strong signals occupy less than the percentile cutoff. Core
            // supplies the same full-band estimate, not the visible peak.
            QVector<float> bins(1024, floor);
            for (int i = 800; i < bins.size(); ++i) { bins[i] = -20.0f; }
            local.feedBins(bins, time);
            remote.feedNoiseFloor(floor, time);
            QCOMPARE(remoteThresholds.size(), localThresholds.size());
            QCOMPARE(remoteFloors.size(), localFloors.size());
            for (int i = 0; i < localThresholds.size(); ++i) {
                QCOMPARE(remoteThresholds.at(i), localThresholds.at(i));
            }
            for (int i = 0; i < localFloors.size(); ++i) {
                QCOMPARE(remoteFloors.at(i), localFloors.at(i));
            }
        };
        feed(-130.0f, 0);
        QCOMPARE(remoteThresholds.size(), 0);
        local.setEnabled(true);
        remote.setEnabled(true);
        feed(-130.0f, 0);
        feed(-100.0f, 100); // Same cadence suppression as the local path.
        QCOMPARE(remoteFloors.size(), 1);
        feed(-130.0f, 500); // An unchanged floor emits no second pair.
        QCOMPARE(remoteThresholds.size(), 1);
        feed(-100.0f, 1000); // Same EWMA response to a changed noise floor.
        local.notifyManualOverride();
        remote.notifyManualOverride();
        const int beforeOverride = remoteFloors.size();
        feed(-80.0f, 2000);
        QCOMPARE(remoteFloors.size(), beforeOverride);
        local.retuneNow();
        remote.retuneNow();
        feed(-110.0f, 2500);
        QCOMPARE(remote.smoothedFloor(), -110.0f);
        local.setTransmitting(true);
        remote.setTransmitting(true);
        const int beforeTx = remoteFloors.size();
        feed(-50.0f, 3500);
        QCOMPARE(remoteFloors.size(), beforeTx);
        local.setTransmitting(false);
        remote.setTransmitting(false);
        feed(-110.0f, 4000);
        remote.feedNoiseFloor(qQNaN(), 5000);
        remote.feedNoiseFloor(qInf(), 5000);
        remote.feedNoiseFloor(-qInf(), 5000);
        QCOMPARE(remoteFloors.size(), localFloors.size());
        QCOMPARE(remote.smoothedFloor(), local.smoothedFloor());
    }

    void enabled_emitsThresholdsOnFirstFrame()
    {
        // Smoke path: enabled controller, fed a flat noise frame,
        // emits one threshold update whose low/high bracket the floor
        // with the configured margins. Default margins (-5, +55) around
        // a -130 floor should give (-135, -75) — 60 dB gap, well above
        // the 30 dB min clamp.
        ClarityController ctrl;
        QSignalSpy spy(&ctrl, &ClarityController::waterfallThresholdsChanged);
        ctrl.setEnabled(true);

        QVector<float> bins(2048, -130.0f);
        ctrl.feedBins(bins, 0);

        QCOMPARE(spy.count(), 1);
        const QList<QVariant> args = spy.takeFirst();
        const float low  = args.at(0).toFloat();
        const float high = args.at(1).toFloat();
        QCOMPARE(low,  -135.0f);
        QCOMPARE(high, -75.0f);
    }

    void disabled_doesNotEmit()
    {
        // Master-off means no emission regardless of frame content.
        // Default constructor leaves the controller disabled — the
        // smooth-defaults stay in charge until the user opts in.
        ClarityController ctrl;
        QSignalSpy spy(&ctrl, &ClarityController::waterfallThresholdsChanged);

        QVector<float> bins(2048, -130.0f);
        ctrl.feedBins(bins, 0);

        QCOMPARE(spy.count(), 0);
    }

    void secondFrameWithinPollInterval_isSuppressed()
    {
        // Cadence gate: at 2 Hz polling (500 ms), two frames fed within
        // the same poll window collapse to a single emission. Design
        // spec §6.2.2 locks 2 Hz; we also keep the UI thread quiet.
        ClarityController ctrl;
        ctrl.setEnabled(true);
        QSignalSpy spy(&ctrl, &ClarityController::waterfallThresholdsChanged);

        QVector<float> noise(2048, -130.0f);
        QVector<float> hotter(2048, -100.0f);  // deliberately different

        ctrl.feedBins(noise,  0);
        ctrl.feedBins(hotter, 100);  // 100 ms later — inside 500 ms window

        QCOMPARE(spy.count(), 1);
    }

    void transmitting_suppressesEmission()
    {
        // MOX engaged: the RX path is likely distorted by feedback and
        // the noise floor reading is meaningless. Design spec §6.2.4:
        // "Pause Clarity during TX — we already have the MOX signal".
        ClarityController ctrl;
        ctrl.setEnabled(true);
        ctrl.setTransmitting(true);
        QSignalSpy spy(&ctrl, &ClarityController::waterfallThresholdsChanged);

        QVector<float> bins(2048, -130.0f);
        ctrl.feedBins(bins, 0);

        QCOMPARE(spy.count(), 0);
    }

    void transmitRelease_resumesEmission()
    {
        // After MOX releases and the first post-TX frame arrives past
        // the poll window, Clarity emits again.
        ClarityController ctrl;
        ctrl.setEnabled(true);
        QSignalSpy spy(&ctrl, &ClarityController::waterfallThresholdsChanged);

        QVector<float> bins(2048, -130.0f);
        ctrl.setTransmitting(true);
        ctrl.feedBins(bins, 0);        // suppressed (TX)
        ctrl.setTransmitting(false);
        ctrl.feedBins(bins, 1000);     // resumed (post-poll)

        QCOMPARE(spy.count(), 1);
    }

    void transmitRelease_keepsThePreKeyThresholdsThenFollowsTheNewFloor()
    {
        // Was transmitRelease_reEmitsAtTheUnchangedFloor (bench 2026-10-06,
        // #359). The un-key no longer re-anchors: the pre-key thresholds stay
        // the last emitted pair, and a different floor after the key is
        // smoothed from the kept one.
        ClarityController ctrl;
        ctrl.setEnabled(true);
        QSignalSpy spy(&ctrl, &ClarityController::waterfallThresholdsChanged);

        ctrl.feedBins(QVector<float>(2048, -130.0f), 0);
        QCOMPARE(spy.count(), 1);
        const float lowBefore = ctrl.lastLow();
        const float highBefore = ctrl.lastHigh();

        ctrl.setTransmitting(true);
        ctrl.feedBins(QVector<float>(2048, -110.0f), 500);   // rejected while keyed
        ctrl.feedBins(QVector<float>(2048, -110.0f), 2500);
        ctrl.setTransmitting(false);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(ctrl.lastLow(), lowBefore);
        QCOMPARE(ctrl.lastHigh(), highBefore);

        ctrl.feedBins(QVector<float>(2048, -110.0f), 3000);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(0).at(0).toFloat(), lowBefore);
        QCOMPARE(spy.at(0).at(1).toFloat(), highBefore);
        const float lowAfter = spy.at(1).at(0).toFloat();
        QVERIFY2(lowAfter > lowBefore, "the floor moves toward the post-key reading");
        QVERIFY2(lowAfter < -110.0f - 5.0f, "the post-key reading is not taken whole");
    }

    void transmitRelease_restoresThePreKeyFloor()
    {
        // Was transmitRelease_takesTheFirstPostKeyFloorWhole. Steady floor
        // before a key: the key, the un-key and the hold change nothing and
        // emit no level at either edge.
        ClarityController ctrl;
        ctrl.setEnabled(true);
        ctrl.setSmoothingTauSec(3.0f);
        ctrl.feedBins(QVector<float>(2048, -120.0f), 0);
        ctrl.feedBins(QVector<float>(2048, -120.0f), 500);
        QCOMPARE(ctrl.smoothedFloor(), -120.0f);
        const float lowBefore = ctrl.lastLow();
        const float highBefore = ctrl.lastHigh();

        QSignalSpy thresholds(&ctrl, &ClarityController::waterfallThresholdsChanged);
        QSignalSpy floors(&ctrl, &ClarityController::noiseFloorChanged);
        ctrl.setTransmitting(true);
        QCOMPARE(thresholds.count(), 0);
        QCOMPARE(floors.count(), 0);
        ctrl.setTransmitting(false);
        ctrl.beginHold(1000, 2000);
        QVERIFY(ctrl.inHold(2000));
        QVERIFY(ctrl.inHold(2999));
        QVERIFY(!ctrl.inHold(3000));
        QCOMPARE(ctrl.smoothedFloor(), -120.0f);
        QCOMPARE(ctrl.lastLow(), lowBefore);
        QCOMPARE(ctrl.lastHigh(), highBefore);
        QCOMPARE(thresholds.count(), 0);
        QCOMPARE(floors.count(), 0);

        // The kept floor is what the next reading is smoothed from.
        ctrl.feedBins(QVector<float>(2048, -100.0f), 3500);
        QVERIFY(ctrl.smoothedFloor() > -120.0f);
        QVERIFY(ctrl.smoothedFloor() < -110.0f);
    }

    void hold_takesInNothingAndEmitsNothing()
    {
        ClarityController ctrl;
        ctrl.setEnabled(true);
        ctrl.feedBins(QVector<float>(2048, -120.0f), 0);
        ctrl.setTransmitting(true);
        ctrl.setTransmitting(false);
        ctrl.beginHold(1000, 2000);

        QSignalSpy thresholds(&ctrl, &ClarityController::waterfallThresholdsChanged);
        QSignalSpy floors(&ctrl, &ClarityController::noiseFloorChanged);
        ctrl.feedBins(QVector<float>(2048, -100.0f), 2000);
        ctrl.feedBins(QVector<float>(2048, -100.0f), 2500);
        ctrl.feedNoiseFloor(-100.0f, 2999);
        QCOMPARE(ctrl.smoothedFloor(), -120.0f);
        QCOMPARE(thresholds.count(), 0);
        QCOMPARE(floors.count(), 0);
    }

    void hold_restartsTheSmoothingClock()
    {
        // A key 10 s long is longer than 3 x tau. Smoothed over the time
        // since the last pre-key reading, the first reading after it would
        // move the floor 97 percent of the way. The clock restarts at the
        // hold's end, so it moves by the 500 ms step.
        ClarityController ctrl;
        ctrl.setEnabled(true);
        ctrl.setSmoothingTauSec(3.0f);
        ctrl.feedBins(QVector<float>(2048, -120.0f), 0);
        ctrl.feedBins(QVector<float>(2048, -120.0f), 500);
        ctrl.feedBins(QVector<float>(2048, -120.0f), 1000);
        QCOMPARE(ctrl.smoothedFloor(), -120.0f);

        ctrl.setTransmitting(true);                           // keyed at 1000
        ctrl.feedBins(QVector<float>(2048, -40.0f), 6000);
        ctrl.setTransmitting(false);                          // un-keyed at 11000
        ctrl.beginHold(1000, 11000);

        QSignalSpy thresholds(&ctrl, &ClarityController::waterfallThresholdsChanged);
        const QVector<float> after(2048, -100.0f);
        ctrl.feedBins(after, 11500);                          // in the hold
        ctrl.feedBins(after, 12000);                          // the hold's end: no time has run
        QCOMPARE(ctrl.smoothedFloor(), -120.0f);
        QCOMPARE(thresholds.count(), 0);

        ctrl.feedBins(after, 12500);
        const float step = 1.0f - std::exp(-0.5f / 3.0f);     // about 0.154
        const float expected = -120.0f + step * 20.0f;        // about -116.9
        QVERIFY2(qAbs(ctrl.smoothedFloor() - expected) < 0.01f,
                 qPrintable(QString::number(ctrl.smoothedFloor())));
        QCOMPARE(thresholds.count(), 1);

        // The cadence gate still applies after an emission.
        ctrl.feedBins(after, 12600);
        QCOMPARE(thresholds.count(), 1);
        ctrl.feedBins(after, 13000);
        QCOMPARE(thresholds.count(), 2);
        QVERIFY(ctrl.smoothedFloor() > expected);
        QVERIFY(ctrl.smoothedFloor() < -100.0f);
    }

    void keyDuringHold_cancelsItAndKeepsTheLevel()
    {
        ClarityController ctrl;
        ctrl.setEnabled(true);
        ctrl.feedBins(QVector<float>(2048, -120.0f), 0);
        ctrl.setTransmitting(true);
        ctrl.setTransmitting(false);
        ctrl.beginHold(1000, 2000);
        QVERIFY(ctrl.inHold(2200));

        QSignalSpy thresholds(&ctrl, &ClarityController::waterfallThresholdsChanged);
        QSignalSpy floors(&ctrl, &ClarityController::noiseFloorChanged);
        ctrl.setTransmitting(true);                           // re-key at 2200
        QVERIFY(!ctrl.inHold(2200));
        QCOMPARE(ctrl.smoothedFloor(), -120.0f);
        ctrl.feedBins(QVector<float>(2048, -100.0f), 2400);   // keyed
        QCOMPARE(ctrl.smoothedFloor(), -120.0f);

        ctrl.setTransmitting(false);                          // un-key at 2500
        QVERIFY(!ctrl.inHold(2500));
        ctrl.beginHold(1000, 2500);                           // a fresh hold
        QVERIFY(ctrl.inHold(3400));                           // past the first hold's end
        ctrl.feedBins(QVector<float>(2048, -100.0f), 3400);
        QCOMPARE(ctrl.smoothedFloor(), -120.0f);
        QVERIFY(!ctrl.inHold(3500));
        QCOMPARE(thresholds.count(), 0);
        QCOMPARE(floors.count(), 0);
    }

    void cancelHold_endsTheHold()
    {
        ClarityController ctrl;
        ctrl.setEnabled(true);
        ctrl.setSmoothingTauSec(0.0f);
        ctrl.feedBins(QVector<float>(2048, -120.0f), 0);
        ctrl.beginHold(1000, 1000);
        QVERIFY(ctrl.inHold(1500));
        ctrl.cancelHold();
        QVERIFY(!ctrl.inHold(1500));
        ctrl.feedBins(QVector<float>(2048, -100.0f), 1500);
        QCOMPARE(ctrl.smoothedFloor(), -100.0f);
    }

    void enabledWhileKeyed_takesTheFirstReadingAfterTheHoldWhole()
    {
        // No level before the key, so there is nothing to restore.
        ClarityController ctrl;
        ctrl.setTransmitting(true);
        ctrl.setEnabled(true);
        QSignalSpy thresholds(&ctrl, &ClarityController::waterfallThresholdsChanged);
        QSignalSpy floors(&ctrl, &ClarityController::noiseFloorChanged);
        ctrl.feedBins(QVector<float>(2048, -90.0f), 0);       // keyed
        ctrl.setTransmitting(false);
        ctrl.beginHold(1000, 5000);
        ctrl.feedBins(QVector<float>(2048, -90.0f), 5500);    // in the hold
        QCOMPARE(thresholds.count(), 0);
        QCOMPARE(floors.count(), 0);

        ctrl.feedBins(QVector<float>(2048, -100.0f), 6000);
        QCOMPARE(ctrl.smoothedFloor(), -100.0f);
        QCOMPARE(floors.count(), 1);
        QCOMPARE(thresholds.count(), 1);
        QCOMPARE(thresholds.first().at(0).toFloat(), -105.0f);
        QCOMPARE(thresholds.first().at(1).toFloat(), -45.0f);
    }

    void afterTheHold_emitsWithoutRetuneOrBandChange()
    {
        // R-DSP-25 (#359): Clarity is active again after the un-key with no
        // retuneNow() and no snapToFloor().
        ClarityController ctrl;
        ctrl.setEnabled(true);
        ctrl.feedBins(QVector<float>(2048, -120.0f), 0);
        ctrl.setTransmitting(true);
        ctrl.setTransmitting(false);
        ctrl.beginHold(1000, 3000);

        QSignalSpy thresholds(&ctrl, &ClarityController::waterfallThresholdsChanged);
        QSignalSpy floors(&ctrl, &ClarityController::noiseFloorChanged);
        ctrl.feedBins(QVector<float>(2048, -119.0f), 4500);
        ctrl.feedBins(QVector<float>(2048, -119.0f), 5000);
        QCOMPARE(floors.count(), 2);
        QCOMPARE(thresholds.count(), 2);
        QVERIFY(ctrl.smoothedFloor() > -120.0f);
        QVERIFY(ctrl.smoothedFloor() < -119.0f);
    }

    void calibrationOffset_isAddedToBinsOnly()
    {
        // Bins whose 30th percentile is -100, with strong signals above it.
        QVector<float> bins(1000, -100.0f);
        for (int i = 800; i < bins.size(); ++i) { bins[i] = -20.0f; }

        ClarityController local;
        local.setEnabled(true);
        local.setCalibrationOffsetDb(-12.0f);
        local.feedBins(bins, 0);
        QCOMPARE(local.smoothedFloor(), -112.0f);

        // A floor from the Core is calibrated already.
        ClarityController remote;
        remote.setEnabled(true);
        remote.setCalibrationOffsetDb(-12.0f);
        remote.feedNoiseFloor(-112.0f, 0);
        QCOMPARE(remote.smoothedFloor(), -112.0f);
    }

    void smallFloorSteps_emitOnEveryReading()
    {
        // No 2 dB steps: the consumer slews, so every changed floor goes out.
        ClarityController ctrl;
        ctrl.setEnabled(true);
        ctrl.setSmoothingTauSec(0.0f);
        QSignalSpy spy(&ctrl, &ClarityController::waterfallThresholdsChanged);
        for (int i = 0; i < 6; ++i) {
            const float floor = -130.0f + 0.5f * static_cast<float>(i);
            ctrl.feedBins(QVector<float>(2048, floor), 500 * i);
            QCOMPARE(spy.count(), i + 1);
            QCOMPARE(spy.last().at(0).toFloat(), floor - 5.0f);
            QCOMPARE(spy.last().at(1).toFloat(), floor + 55.0f);
        }
    }

    void manualOverride_suppressesUntilRetune()
    {
        // User dragged the Low/High slider while Clarity was running.
        // Clarity pauses — it would undo the user's tune on the next
        // frame otherwise. Stays paused until retuneNow() is called
        // (or the master toggle cycles).
        ClarityController ctrl;
        ctrl.setEnabled(true);
        QSignalSpy spy(&ctrl, &ClarityController::waterfallThresholdsChanged);

        QVector<float> bins(2048, -130.0f);
        ctrl.feedBins(bins, 0);           // emits (count=1)
        ctrl.notifyManualOverride();      // slider drag
        QVERIFY(ctrl.isPaused());
        ctrl.feedBins(bins, 1000);        // paused → no emission
        ctrl.feedBins(bins, 2000);        // still paused
        QCOMPARE(spy.count(), 1);

        ctrl.retuneNow();                 // Re-tune button
        QVERIFY(!ctrl.isPaused());
        ctrl.feedBins(bins, 3000);        // resumes
        QCOMPARE(spy.count(), 2);
    }

    void manualOverride_emitsPausedChanged()
    {
        // UI reactivity: the status badge in SpectrumOverlayPanel watches
        // pausedChanged to flip green→amber. Must fire exactly once on
        // state transition, not on every feedBins call.
        ClarityController ctrl;
        QSignalSpy pausedSpy(&ctrl, &ClarityController::pausedChanged);

        ctrl.notifyManualOverride();
        QCOMPARE(pausedSpy.count(), 1);
        QCOMPARE(pausedSpy.first().at(0).toBool(), true);

        // Second override call should be idempotent — already paused.
        ctrl.notifyManualOverride();
        QCOMPARE(pausedSpy.count(), 1);
    }

    void retuneNow_emitsPausedChangedFalse()
    {
        ClarityController ctrl;
        ctrl.notifyManualOverride();

        QSignalSpy pausedSpy(&ctrl, &ClarityController::pausedChanged);
        ctrl.retuneNow();

        QCOMPARE(pausedSpy.count(), 1);
        QCOMPARE(pausedSpy.first().at(0).toBool(), false);
    }

    void stableFloor_emitsOnce()
    {
        // Was stableFloor_deadbandSuppressesAfterFirstEmission. On a
        // stationary noise floor Clarity emits once, then stays quiet until
        // the floor moves: a pair goes out only when the floor differs from
        // the last emitted one.
        // (τ set to 0 so raw-floor == smoothed for this isolation test.)
        ClarityController ctrl;
        ctrl.setEnabled(true);
        ctrl.setSmoothingTauSec(0.0f);
        QSignalSpy spy(&ctrl, &ClarityController::waterfallThresholdsChanged);

        QVector<float> bins(2048, -130.0f);
        ctrl.feedBins(bins, 0);      // emits
        ctrl.feedBins(bins, 1000);   // past poll, floor unchanged → suppressed
        ctrl.feedBins(bins, 2000);   // still unchanged → suppressed
        ctrl.feedBins(bins, 3000);
        QCOMPARE(spy.count(), 1);
    }

    void floorDrift_emitsAgain()
    {
        // Was floorDriftBeyondDeadband_emitsAgain. Same setup, but between
        // frames the floor moves 10 dB, so a second emission must fire.
        ClarityController ctrl;
        ctrl.setEnabled(true);
        ctrl.setSmoothingTauSec(0.0f);
        QSignalSpy spy(&ctrl, &ClarityController::waterfallThresholdsChanged);

        QVector<float> noise (2048, -130.0f);
        QVector<float> louder(2048, -120.0f);
        ctrl.feedBins(noise,  0);
        ctrl.feedBins(louder, 1000);
        QCOMPARE(spy.count(), 2);
    }

    void ewmaSmoothing_dampensStepChange()
    {
        // Verify EWMA is active and dampens. With τ=3s, a step from
        // -130 to -100 after 1 second should NOT snap instantly:
        //   alpha = 1 - exp(-1/3) ≈ 0.28
        //   smoothed ≈ -121.5 dBm (not -100)
        // So the emitted low should be between the anchored and settled
        // values, proving the exponential filter is in the path.
        ClarityController ctrl;
        ctrl.setEnabled(true);
        ctrl.setSmoothingTauSec(3.0f);
        QSignalSpy spy(&ctrl, &ClarityController::waterfallThresholdsChanged);

        QVector<float> cold(2048, -130.0f);
        QVector<float> hot (2048, -100.0f);

        ctrl.feedBins(cold, 0);
        ctrl.feedBins(hot,  1000);
        QCOMPARE(spy.count(), 2);

        const float settledLow = -100.0f + (-5.0f);   // -105 if snap
        const float anchoredLow = -130.0f + (-5.0f);   // -135 if stationary
        const float actualLow = spy.last().at(0).toFloat();
        QVERIFY2(actualLow < settledLow,
                 "EWMA must not snap instantaneously");
        QVERIFY2(actualLow > anchoredLow,
                 "EWMA must begin moving toward new floor");
    }

    void minGapClamp_expandsNarrowGap()
    {
        // Pathological margin config that produces < 30 dB gap. The
        // empty-band clamp (spec §6.2.4) must expand the gap to 30 dB.
        ClarityController ctrl;
        ctrl.setEnabled(true);
        ctrl.setSmoothingTauSec(0.0f);
        ctrl.setLowMarginDb(-5.0f);
        ctrl.setHighMarginDb(10.0f);   // gap = 15 dB (< 30 min)
        ctrl.setMinGapDb(30.0f);
        QSignalSpy spy(&ctrl, &ClarityController::waterfallThresholdsChanged);

        QVector<float> bins(2048, -130.0f);
        ctrl.feedBins(bins, 0);
        QCOMPARE(spy.count(), 1);
        const float low  = spy.first().at(0).toFloat();
        const float high = spy.first().at(1).toFloat();
        QVERIFY2((high - low) >= 30.0f,
                 "Min gap clamp must enforce ≥ 30 dB palette range");
    }

    void emptyBins_doesNotEmitOrCrash()
    {
        // Cold-start before FFTEngine sends the first frame.
        ClarityController ctrl;
        ctrl.setEnabled(true);
        QSignalSpy spy(&ctrl, &ClarityController::waterfallThresholdsChanged);

        QVector<float> empty;
        ctrl.feedBins(empty, 0);
        QCOMPARE(spy.count(), 0);
    }

    void snapToFloor_emitsImmediately()
    {
        // Band-switch path: PanadapterModel feeds the stored floor for
        // the new band into ClarityController::snapToFloor(). The
        // controller sets its EWMA anchor and emits thresholds right
        // away so the waterfall snaps to the remembered state, then
        // Clarity refines from there (spec §6.2.4 "band switch").
        ClarityController ctrl;
        ctrl.setEnabled(true);
        QSignalSpy spy(&ctrl, &ClarityController::waterfallThresholdsChanged);

        ctrl.snapToFloor(-100.0f);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toFloat(), -105.0f);  // -100 + (-5)
        QCOMPARE(spy.first().at(1).toFloat(),  -45.0f);  // -100 + 55
    }

    void snapToFloor_nan_isIgnored()
    {
        // Band has no stored Clarity data — snap with NaN does nothing.
        ClarityController ctrl;
        ctrl.setEnabled(true);
        QSignalSpy spy(&ctrl, &ClarityController::waterfallThresholdsChanged);

        ctrl.snapToFloor(qQNaN());
        QCOMPARE(spy.count(), 0);
    }
};

QTEST_MAIN(TestClarityController)
#include "tst_clarity_controller.moc"
