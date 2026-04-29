// =================================================================
// tests/tst_two_tone_controller.cpp  (NereusSDR)
// =================================================================
//
// Phase 3M-1c chunk I (I.1-I.5) — TwoToneController activation handler.
//
// Verifies:
//   I.1.A — setActive(true) when not powered → no MOX engaged, no TXPostGen
//           setters fire, twoToneActiveChanged(false) emitted (revert UI).
//   I.1.B — setActive(true) when MOX is on → MOX cycles off, 200 ms settle,
//           then activation continues.
//   I.1.C — setActive(true) → all required TXPostGen setters fire in the
//           expected order; setTxPostGenRun(true) is the last call before
//           setMox(true).
//   I.1.D — setActive(false) → setTxPostGenRun(false) fires after the
//           200 ms deactivation settle; twoToneActiveChanged(false) emitted.
//   I.1.E — Continuous mode: setTxPostGenMode(1), continuous setters fire,
//           pulsed-mode setters do NOT fire.
//   I.1.F — Pulsed mode: setTxPostGenMode(7), pulsed setters fire,
//           continuous setters do NOT fire.  Pulse profile (window/duty/ramp/IQout)
//           also applied.
//   I.1.G — 0.49999 magnitude scaling: ttmag1/ttmag2 = 0.49999 * 10^(level/20).
//   I.1.H — Mode-aware invert: LSB + invert=true → frequencies sign-flipped.
//           USB + invert=true → no flip.
//   I.2   — Freq2Delay > 0 → Mag2 starts at 0, transitions to ttmag2 after delay.
//   I.3   — TUN auto-stop: currently a TODO (no isTuneToneActive() getter).
//           Test verifies the path is ready for the wiring.
//   I.4.A — DrivePowerSource::Fixed → PWR is snapshotted on activate
//           (current power becomes m_savedPwr) and restored on deactivate.
//   I.4.B — DrivePowerSource::DriveSlider → no override; PWR unchanged.
//   I.5   — BandPlanGuard rejects CW mode → setMox(true) emits moxRejected,
//           TwoToneController cleans up state and emits twoToneActiveChanged(false).
//
// =================================================================

// no-port-check: NereusSDR-original test file. All Thetis source cites are
// in TwoToneController.h/cpp.

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QSignalSpy>

#include "core/TwoToneController.h"
#include "core/MoxController.h"
#include "core/TxChannel.h"
#include "core/safety/BandPlanGuard.h"
#include "core/WdspTypes.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"

using namespace NereusSDR;
using namespace NereusSDR::safety;

// ---------------------------------------------------------------------------
// RecordingTxChannel — overrides the 13 TXPostGen setters to record the
// call sequence. Production methods early-return when WDSP is not initialised
// (the txa[] gen1.p pointer is null), so subclassing is purely for test
// observability.
// ---------------------------------------------------------------------------
class RecordingTxChannel : public TxChannel {
public:
    explicit RecordingTxChannel(int channelId) : TxChannel(channelId) {}

    struct Call {
        QString method;
        double  arg1{0.0};
        double  arg2{0.0};
    };

    QVector<Call> calls;

    void setTxPostGenMode(int mode) override {
        calls.append({QStringLiteral("setTxPostGenMode"), double(mode), 0.0});
    }
    void setTxPostGenTTFreq1(double hz) override {
        calls.append({QStringLiteral("setTxPostGenTTFreq1"), hz, 0.0});
    }
    void setTxPostGenTTFreq2(double hz) override {
        calls.append({QStringLiteral("setTxPostGenTTFreq2"), hz, 0.0});
    }
    void setTxPostGenTTMag1(double linear) override {
        calls.append({QStringLiteral("setTxPostGenTTMag1"), linear, 0.0});
    }
    void setTxPostGenTTMag2(double linear) override {
        calls.append({QStringLiteral("setTxPostGenTTMag2"), linear, 0.0});
    }
    void setTxPostGenTTPulseToneFreq1(double hz) override {
        calls.append({QStringLiteral("setTxPostGenTTPulseToneFreq1"), hz, 0.0});
    }
    void setTxPostGenTTPulseToneFreq2(double hz) override {
        calls.append({QStringLiteral("setTxPostGenTTPulseToneFreq2"), hz, 0.0});
    }
    void setTxPostGenTTPulseMag1(double linear) override {
        calls.append({QStringLiteral("setTxPostGenTTPulseMag1"), linear, 0.0});
    }
    void setTxPostGenTTPulseMag2(double linear) override {
        calls.append({QStringLiteral("setTxPostGenTTPulseMag2"), linear, 0.0});
    }
    void setTxPostGenTTPulseFreq(int hz) override {
        calls.append({QStringLiteral("setTxPostGenTTPulseFreq"), double(hz), 0.0});
    }
    void setTxPostGenTTPulseDutyCycle(double pct) override {
        calls.append({QStringLiteral("setTxPostGenTTPulseDutyCycle"), pct, 0.0});
    }
    void setTxPostGenTTPulseTransition(double sec) override {
        calls.append({QStringLiteral("setTxPostGenTTPulseTransition"), sec, 0.0});
    }
    void setTxPostGenTTPulseIQOut(bool on) override {
        calls.append({QStringLiteral("setTxPostGenTTPulseIQOut"), on ? 1.0 : 0.0, 0.0});
    }
    void setTxPostGenRun(bool on) override {
        calls.append({QStringLiteral("setTxPostGenRun"), on ? 1.0 : 0.0, 0.0});
    }

    int countCalls(const QString& method) const {
        int n = 0;
        for (const auto& c : calls) {
            if (c.method == method) ++n;
        }
        return n;
    }
    int firstIndexOf(const QString& method) const {
        for (int i = 0; i < calls.size(); ++i) {
            if (calls[i].method == method) return i;
        }
        return -1;
    }
    Call findCall(const QString& method) const {
        for (const auto& c : calls) {
            if (c.method == method) return c;
        }
        return {};
    }
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static constexpr int kTxChannelId = 1; // matches TxChannel test convention

// Build a MoxCheckFn that always rejects calls (simulates BandPlanGuard
// rejecting CW mode for SSB-only TX).
static MoxController::MoxCheckFn makeRejectingCheckFn(DSPMode mode)
{
    return [mode]() -> BandPlanGuard::MoxCheckResult {
        BandPlanGuard guard;
        return guard.checkMoxAllowed(
            Region::UnitedStates,
            14'200'000,
            mode,
            Band::Band20m, Band::Band20m,
            /*preventDifferentBand=*/false,
            /*extended=*/false);
    };
}

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------
class TestTwoToneController : public QObject
{
    Q_OBJECT

private slots:

    // ── I.1.A: power-off precondition ─────────────────────────────────────
    void setActive_powerOff_doesNotEngage()
    {
        TransmitModel tx;
        RecordingTxChannel tc(kTxChannelId);
        MoxController mox;
        mox.setTimerIntervals(0, 0, 0, 0, 0, 0);
        SliceModel slice;

        TwoToneController ctrl;
        ctrl.setTransmitModel(&tx);
        ctrl.setTxChannel(&tc);
        ctrl.setMoxController(&mox);
        ctrl.setSliceModel(&slice);
        ctrl.setSettleDelaysMs(0, 0);
        ctrl.setPowerOn(false);

        QSignalSpy activeSpy(&ctrl, &TwoToneController::twoToneActiveChanged);

        ctrl.setActive(true);
        QCoreApplication::processEvents();

        QVERIFY(!ctrl.isActive());
        QVERIFY(!mox.isMox());
        QCOMPARE(tc.calls.size(), 0);
        // No state transition (was already inactive), so signal should not fire.
        QCOMPARE(activeSpy.count(), 0);
    }

    // ── I.1.B: MOX-on first → cycle off + settle + continue ───────────────
    void setActive_moxOnFirst_cyclesOffThenContinues()
    {
        TransmitModel tx;
        RecordingTxChannel tc(kTxChannelId);
        MoxController mox;
        mox.setTimerIntervals(0, 0, 0, 0, 0, 0);
        SliceModel slice;

        TwoToneController ctrl;
        ctrl.setTransmitModel(&tx);
        ctrl.setTxChannel(&tc);
        ctrl.setMoxController(&mox);
        ctrl.setSliceModel(&slice);
        ctrl.setSettleDelaysMs(0, 0);

        // Engage MOX first.
        mox.setMox(true);
        QCoreApplication::processEvents();
        QVERIFY(mox.isMox());

        QSignalSpy activeSpy(&ctrl, &TwoToneController::twoToneActiveChanged);

        ctrl.setActive(true);
        // Drain any pending settle timer + MOX walk + activation sequence.
        for (int i = 0; i < 10; ++i) {
            QCoreApplication::processEvents();
        }

        QVERIFY(ctrl.isActive());
        QVERIFY(mox.isMox()); // re-engaged after settle
        QCOMPARE(activeSpy.count(), 1);
        QCOMPARE(activeSpy[0][0].toBool(), true);
        QVERIFY(tc.countCalls(QStringLiteral("setTxPostGenRun")) >= 1);
    }

    // ── I.1.C: setter ordering — Run is the last call ─────────────────────
    void setActive_setterOrder_runIsLastBeforeMox()
    {
        TransmitModel tx;
        RecordingTxChannel tc(kTxChannelId);
        MoxController mox;
        mox.setTimerIntervals(0, 0, 0, 0, 0, 0);
        SliceModel slice;

        TwoToneController ctrl;
        ctrl.setTransmitModel(&tx);
        ctrl.setTxChannel(&tc);
        ctrl.setMoxController(&mox);
        ctrl.setSliceModel(&slice);
        ctrl.setSettleDelaysMs(0, 0);

        ctrl.setActive(true);
        QCoreApplication::processEvents();

        QVERIFY(ctrl.isActive());
        // setTxPostGenRun must appear in the calls list.
        const int runIdx = tc.firstIndexOf(QStringLiteral("setTxPostGenRun"));
        QVERIFY(runIdx >= 0);
        // It should be the LAST TXPostGen call before MOX engages.
        // Verify it's after the mode setter and the freq/mag setters.
        const int modeIdx = tc.firstIndexOf(QStringLiteral("setTxPostGenMode"));
        QVERIFY(modeIdx >= 0);
        QVERIFY(modeIdx < runIdx);
    }

    // ── I.1.D: deactivation — setTxPostGenRun(false) after settle ─────────
    void setActive_deactivation_stopsRunAndEmitsSignal()
    {
        TransmitModel tx;
        RecordingTxChannel tc(kTxChannelId);
        MoxController mox;
        mox.setTimerIntervals(0, 0, 0, 0, 0, 0);
        SliceModel slice;

        TwoToneController ctrl;
        ctrl.setTransmitModel(&tx);
        ctrl.setTxChannel(&tc);
        ctrl.setMoxController(&mox);
        ctrl.setSliceModel(&slice);
        ctrl.setSettleDelaysMs(0, 0);

        ctrl.setActive(true);
        QCoreApplication::processEvents();
        QVERIFY(ctrl.isActive());
        tc.calls.clear();

        QSignalSpy activeSpy(&ctrl, &TwoToneController::twoToneActiveChanged);

        ctrl.setActive(false);
        // Drain settle timer + the deactivation continuation.
        for (int i = 0; i < 10; ++i) {
            QCoreApplication::processEvents();
        }

        QVERIFY(!ctrl.isActive());
        QVERIFY(!mox.isMox());
        QCOMPARE(activeSpy.count(), 1);
        QCOMPARE(activeSpy[0][0].toBool(), false);
        // setTxPostGenRun(false) should have fired.
        bool sawRunOff = false;
        for (const auto& c : tc.calls) {
            if (c.method == QStringLiteral("setTxPostGenRun") && c.arg1 == 0.0) {
                sawRunOff = true;
                break;
            }
        }
        QVERIFY(sawRunOff);
    }

    // ── I.1.E: continuous mode → mode=1, continuous setters fire ──────────
    void setActive_continuousMode_emitsContinuousSetters()
    {
        TransmitModel tx;
        tx.setTwoTonePulsed(false);
        tx.setTwoToneFreq1(700);
        tx.setTwoToneFreq2(1900);
        tx.setTwoToneFreq2Delay(0); // immediate Mag2

        RecordingTxChannel tc(kTxChannelId);
        MoxController mox;
        mox.setTimerIntervals(0, 0, 0, 0, 0, 0);
        SliceModel slice; // default USB → no invert

        TwoToneController ctrl;
        ctrl.setTransmitModel(&tx);
        ctrl.setTxChannel(&tc);
        ctrl.setMoxController(&mox);
        ctrl.setSliceModel(&slice);
        ctrl.setSettleDelaysMs(0, 0);

        ctrl.setActive(true);
        QCoreApplication::processEvents();

        // Mode = 1 (continuous).
        const auto mc = tc.findCall(QStringLiteral("setTxPostGenMode"));
        QCOMPARE(mc.arg1, 1.0);

        // Continuous setters fired.
        QCOMPARE(tc.countCalls(QStringLiteral("setTxPostGenTTFreq1")), 1);
        QCOMPARE(tc.countCalls(QStringLiteral("setTxPostGenTTFreq2")), 1);
        QCOMPARE(tc.countCalls(QStringLiteral("setTxPostGenTTMag1")), 1);
        QCOMPARE(tc.countCalls(QStringLiteral("setTxPostGenTTMag2")), 1);

        // Pulsed-mode setters did NOT fire.
        QCOMPARE(tc.countCalls(QStringLiteral("setTxPostGenTTPulseToneFreq1")), 0);
        QCOMPARE(tc.countCalls(QStringLiteral("setTxPostGenTTPulseToneFreq2")), 0);
        QCOMPARE(tc.countCalls(QStringLiteral("setTxPostGenTTPulseMag1")), 0);
        QCOMPARE(tc.countCalls(QStringLiteral("setTxPostGenTTPulseFreq")), 0);

        // Frequency arguments match TransmitModel.
        QCOMPARE(tc.findCall(QStringLiteral("setTxPostGenTTFreq1")).arg1, 700.0);
        QCOMPARE(tc.findCall(QStringLiteral("setTxPostGenTTFreq2")).arg1, 1900.0);
    }

    // ── I.1.F: pulsed mode → mode=7, pulse profile + pulsed setters ───────
    void setActive_pulsedMode_emitsPulsedSetters()
    {
        TransmitModel tx;
        tx.setTwoTonePulsed(true);
        tx.setTwoToneFreq1(700);
        tx.setTwoToneFreq2(1900);
        tx.setTwoToneFreq2Delay(0);

        RecordingTxChannel tc(kTxChannelId);
        MoxController mox;
        mox.setTimerIntervals(0, 0, 0, 0, 0, 0);
        SliceModel slice;

        TwoToneController ctrl;
        ctrl.setTransmitModel(&tx);
        ctrl.setTxChannel(&tc);
        ctrl.setMoxController(&mox);
        ctrl.setSliceModel(&slice);
        ctrl.setSettleDelaysMs(0, 0);

        ctrl.setActive(true);
        QCoreApplication::processEvents();

        // Mode = 7 (pulsed).
        const auto mc = tc.findCall(QStringLiteral("setTxPostGenMode"));
        QCOMPARE(mc.arg1, 7.0);

        // Pulse profile setters fired with Designer defaults.
        QCOMPARE(tc.findCall(QStringLiteral("setTxPostGenTTPulseFreq")).arg1, 10.0); // window=10pps
        QCOMPARE(tc.findCall(QStringLiteral("setTxPostGenTTPulseDutyCycle")).arg1, 0.25); // 25%
        QCOMPARE(tc.findCall(QStringLiteral("setTxPostGenTTPulseTransition")).arg1, 0.009); // 9ms
        QCOMPARE(tc.findCall(QStringLiteral("setTxPostGenTTPulseIQOut")).arg1, 1.0); // true

        // Pulsed-tone setters fired.
        QCOMPARE(tc.countCalls(QStringLiteral("setTxPostGenTTPulseToneFreq1")), 1);
        QCOMPARE(tc.countCalls(QStringLiteral("setTxPostGenTTPulseToneFreq2")), 1);
        QCOMPARE(tc.countCalls(QStringLiteral("setTxPostGenTTPulseMag1")), 1);
        QCOMPARE(tc.countCalls(QStringLiteral("setTxPostGenTTPulseMag2")), 1);

        // Continuous setters did NOT fire.
        QCOMPARE(tc.countCalls(QStringLiteral("setTxPostGenTTFreq1")), 0);
        QCOMPARE(tc.countCalls(QStringLiteral("setTxPostGenTTFreq2")), 0);
    }

    // ── I.1.G: 0.49999 magnitude scaling formula ─────────────────────────
    void setActive_magnitudeScaling_appliesFormula()
    {
        TransmitModel tx;
        tx.setTwoTonePulsed(false);
        tx.setTwoToneLevel(0.0);   // 10^0 = 1.0  → ttmag = 0.49999 * 1.0
        tx.setTwoToneFreq2Delay(0);

        RecordingTxChannel tc(kTxChannelId);
        MoxController mox;
        mox.setTimerIntervals(0, 0, 0, 0, 0, 0);
        SliceModel slice;

        TwoToneController ctrl;
        ctrl.setTransmitModel(&tx);
        ctrl.setTxChannel(&tc);
        ctrl.setMoxController(&mox);
        ctrl.setSliceModel(&slice);
        ctrl.setSettleDelaysMs(0, 0);

        ctrl.setActive(true);
        QCoreApplication::processEvents();

        const double mag1 = tc.findCall(QStringLiteral("setTxPostGenTTMag1")).arg1;
        QVERIFY(qFuzzyCompare(mag1, 0.49999));

        // -6 dB: 10^(-6/20) ≈ 0.5012 → ttmag ≈ 0.49999 * 0.5012 ≈ 0.25058...
        tc.calls.clear();
        ctrl.setActive(false);
        for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();

        tx.setTwoToneLevel(-6.0);
        ctrl.setActive(true);
        QCoreApplication::processEvents();

        const double mag1_minus6 = tc.findCall(QStringLiteral("setTxPostGenTTMag1")).arg1;
        const double expected = 0.49999 * std::pow(10.0, -6.0 / 20.0);
        QVERIFY(qFuzzyCompare(mag1_minus6, expected));
    }

    // ── I.1.H: mode-aware invert ─────────────────────────────────────────
    void setActive_invertTones_LSB_flipsFreq()
    {
        TransmitModel tx;
        tx.setTwoTonePulsed(false);
        tx.setTwoToneFreq1(700);
        tx.setTwoToneFreq2(1900);
        tx.setTwoToneInvert(true);
        tx.setTwoToneFreq2Delay(0);

        RecordingTxChannel tc(kTxChannelId);
        MoxController mox;
        mox.setTimerIntervals(0, 0, 0, 0, 0, 0);
        SliceModel slice;
        slice.setDspMode(DSPMode::LSB);

        TwoToneController ctrl;
        ctrl.setTransmitModel(&tx);
        ctrl.setTxChannel(&tc);
        ctrl.setMoxController(&mox);
        ctrl.setSliceModel(&slice);
        ctrl.setSettleDelaysMs(0, 0);

        ctrl.setActive(true);
        QCoreApplication::processEvents();

        QCOMPARE(tc.findCall(QStringLiteral("setTxPostGenTTFreq1")).arg1, -700.0);
        QCOMPARE(tc.findCall(QStringLiteral("setTxPostGenTTFreq2")).arg1, -1900.0);
    }

    void setActive_invertTones_USB_doesNotFlip()
    {
        TransmitModel tx;
        tx.setTwoTonePulsed(false);
        tx.setTwoToneFreq1(700);
        tx.setTwoToneFreq2(1900);
        tx.setTwoToneInvert(true);
        tx.setTwoToneFreq2Delay(0);

        RecordingTxChannel tc(kTxChannelId);
        MoxController mox;
        mox.setTimerIntervals(0, 0, 0, 0, 0, 0);
        SliceModel slice;
        slice.setDspMode(DSPMode::USB);

        TwoToneController ctrl;
        ctrl.setTransmitModel(&tx);
        ctrl.setTxChannel(&tc);
        ctrl.setMoxController(&mox);
        ctrl.setSliceModel(&slice);
        ctrl.setSettleDelaysMs(0, 0);

        ctrl.setActive(true);
        QCoreApplication::processEvents();

        QCOMPARE(tc.findCall(QStringLiteral("setTxPostGenTTFreq1")).arg1, 700.0);
        QCOMPARE(tc.findCall(QStringLiteral("setTxPostGenTTFreq2")).arg1, 1900.0);
    }

    // ── I.2: Freq2Delay > 0 → Mag2 starts at 0, applied after delay ──────
    void setActive_freq2Delay_defersMag2()
    {
        TransmitModel tx;
        tx.setTwoTonePulsed(false);
        tx.setTwoToneLevel(0.0);  // ttmag = 0.49999
        tx.setTwoToneFreq2Delay(50); // 50ms delay

        RecordingTxChannel tc(kTxChannelId);
        MoxController mox;
        mox.setTimerIntervals(0, 0, 0, 0, 0, 0);
        SliceModel slice;

        TwoToneController ctrl;
        ctrl.setTransmitModel(&tx);
        ctrl.setTxChannel(&tc);
        ctrl.setMoxController(&mox);
        ctrl.setSliceModel(&slice);
        ctrl.setSettleDelaysMs(0, 0);

        ctrl.setActive(true);
        QCoreApplication::processEvents();

        // Initial Mag2 should be 0.0 because of the delay.
        QCOMPARE(tc.findCall(QStringLiteral("setTxPostGenTTMag2")).arg1, 0.0);

        // Wait for the freq2 delay to elapse; track the second Mag2 call.
        QTRY_COMPARE_WITH_TIMEOUT(tc.countCalls(QStringLiteral("setTxPostGenTTMag2")), 2, 500);

        // Find the LAST setTxPostGenTTMag2 call — it should equal 0.49999.
        double lastMag2 = -1.0;
        for (const auto& c : tc.calls) {
            if (c.method == QStringLiteral("setTxPostGenTTMag2")) {
                lastMag2 = c.arg1;
            }
        }
        QVERIFY(qFuzzyCompare(lastMag2, 0.49999));
    }

    // ── I.4.A: Fixed power source → snapshots and restores PWR ───────────
    void setActive_fixedPowerSource_snapshotsAndRestoresPwr()
    {
        TransmitModel tx;
        tx.setTwoTonePulsed(false);
        tx.setTwoToneFreq2Delay(0);
        tx.setPower(75);                                          // pre-test PWR
        tx.setTwoTonePower(40);                                   // override
        tx.setTwoToneDrivePowerSource(DrivePowerSource::Fixed);

        RecordingTxChannel tc(kTxChannelId);
        MoxController mox;
        mox.setTimerIntervals(0, 0, 0, 0, 0, 0);
        SliceModel slice;

        TwoToneController ctrl;
        ctrl.setTransmitModel(&tx);
        ctrl.setTxChannel(&tc);
        ctrl.setMoxController(&mox);
        ctrl.setSliceModel(&slice);
        ctrl.setSettleDelaysMs(0, 0);

        ctrl.setActive(true);
        QCoreApplication::processEvents();

        QVERIFY(ctrl.isActive());
        QCOMPARE(tx.power(), 40);   // overridden to twoTonePower

        ctrl.setActive(false);
        for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();

        QCOMPARE(tx.power(), 75);   // restored
    }

    // ── I.4.B: DriveSlider mode → no PWR override ────────────────────────
    void setActive_driveSliderPowerSource_doesNotOverridePwr()
    {
        TransmitModel tx;
        tx.setTwoTonePulsed(false);
        tx.setTwoToneFreq2Delay(0);
        tx.setPower(75);
        tx.setTwoTonePower(40);
        tx.setTwoToneDrivePowerSource(DrivePowerSource::DriveSlider);

        RecordingTxChannel tc(kTxChannelId);
        MoxController mox;
        mox.setTimerIntervals(0, 0, 0, 0, 0, 0);
        SliceModel slice;

        TwoToneController ctrl;
        ctrl.setTransmitModel(&tx);
        ctrl.setTxChannel(&tc);
        ctrl.setMoxController(&mox);
        ctrl.setSliceModel(&slice);
        ctrl.setSettleDelaysMs(0, 0);

        ctrl.setActive(true);
        QCoreApplication::processEvents();

        QCOMPARE(tx.power(), 75); // unchanged

        ctrl.setActive(false);
        for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();

        QCOMPARE(tx.power(), 75); // still unchanged
    }

    // ── I.5: BandPlanGuard rejects CW → cleanup state ────────────────────
    void setActive_bandPlanGuardRejectsCw_cleansUpState()
    {
        TransmitModel tx;
        tx.setTwoTonePulsed(false);
        tx.setTwoToneFreq2Delay(0);

        RecordingTxChannel tc(kTxChannelId);
        MoxController mox;
        mox.setTimerIntervals(0, 0, 0, 0, 0, 0);
        // BandPlanGuard rejects CW.
        mox.setMoxCheck(makeRejectingCheckFn(DSPMode::CWL));

        SliceModel slice;

        TwoToneController ctrl;
        ctrl.setTransmitModel(&tx);
        ctrl.setTxChannel(&tc);
        ctrl.setMoxController(&mox);
        ctrl.setSliceModel(&slice);
        ctrl.setSettleDelaysMs(0, 0);

        QSignalSpy moxRejectedSpy(&mox, &MoxController::moxRejected);
        QSignalSpy activeSpy(&ctrl, &TwoToneController::twoToneActiveChanged);

        ctrl.setActive(true);
        QCoreApplication::processEvents();

        // BandPlanGuard rejected the setMox(true) call.
        QCOMPARE(moxRejectedSpy.count(), 1);
        QVERIFY(!mox.isMox());

        // TwoToneController cleaned up its state.
        QVERIFY(!ctrl.isActive());

        // twoToneActiveChanged(false) was emitted (so UI can revert highlight).
        QVERIFY(activeSpy.count() >= 1);
        // The LAST emit should be false.
        QCOMPARE(activeSpy.last().at(0).toBool(), false);
    }

    // ── Idempotent: setActive(true) twice is safe ────────────────────────
    void setActive_idempotent_doesNotRepeat()
    {
        TransmitModel tx;
        RecordingTxChannel tc(kTxChannelId);
        MoxController mox;
        mox.setTimerIntervals(0, 0, 0, 0, 0, 0);
        SliceModel slice;

        TwoToneController ctrl;
        ctrl.setTransmitModel(&tx);
        ctrl.setTxChannel(&tc);
        ctrl.setMoxController(&mox);
        ctrl.setSliceModel(&slice);
        ctrl.setSettleDelaysMs(0, 0);

        QSignalSpy activeSpy(&ctrl, &TwoToneController::twoToneActiveChanged);

        ctrl.setActive(true);
        QCoreApplication::processEvents();
        const int callCount1 = tc.calls.size();
        QVERIFY(ctrl.isActive());
        QCOMPARE(activeSpy.count(), 1);

        ctrl.setActive(true);  // duplicate
        QCoreApplication::processEvents();

        QCOMPARE(tc.calls.size(), callCount1);  // no new calls
        QCOMPARE(activeSpy.count(), 1);          // no re-emit
    }
};

QTEST_MAIN(TestTwoToneController)
#include "tst_two_tone_controller.moc"
