// =================================================================
// tests/tst_radio_model_hl2_temp_routing.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original test file.  The Thetis citation
// below documents which upstream behaviour is exercised; no C# is
// translated in this file.  The math kernel under test
// (NereusSDR::scaleHermesLiteTempCelsius) is registered in
// THETIS-PROVENANCE.md as a port from mi0bot console.cs:25079
// [v2.10.3.13-beta2 @c26a8a4].
//
// Verifies that RadioModel::handlePaTelemetry routes the HL2-specific
// exciter_power C&C field through the temperature averaging ring and
// publishes the °C reading to RadioStatus, while suppressing the
// exciter-power semantic that other boards keep on the same wire bytes.
//
// Coverage:
//   - hl2_publishes_temp_from_exciter_raw
//       Single sample at raw=942 → ≈25 °C published via
//       RadioStatus::paTemperatureChanged.
//   - hl2_suppresses_exciter_power
//       After 1 HL2 sample, RadioStatus.exciterPowerMw() == 0 even
//       when the test seam forces TX state.
//   - non_hl2_scales_exciter_via_orionmkii_curve
//       Non-HL2 board (Saturn / ANAN_G2) sees setExciterPowerMw fire
//       with the Thetis-faithful OrionMKII piecewise-linear scaled
//       value (not a raw passthrough), and setPaTemperature is NOT
//       fired by the HL2 branch.
//   - hl2_averaging_window_smooths_noisy_input
//       Alternating high/low raw samples land at a midpoint °C that
//       sits between the two single-sample values (proves the ring
//       buffer averages, not just last-write-wins).
//   - hl2_window_spans_800ms_at_idle_rate / _at_keyed_rate
//       A step from one reading to another takes mi0bot's 800 ms
//       (100 polls, 8 ms apart) to settle whether telemetry arrives
//       every 200 ms (idle) or every 1 ms (keyed).
//   - hl2_first_reading_shows_at_once
//   - hl2_reading_after_disconnect_starts_fresh
// =================================================================

#include <QtTest/QtTest>
#include <QSignalSpy>

#include "core/AppSettings.h"
#include "core/HpsdrModel.h"
#include "core/PaTelemetryScaling.h"
#include "core/RadioStatus.h"
#include "models/RadioModel.h"

using namespace NereusSDR;

namespace {

constexpr qint64 kMs = 1000000;  // nanoseconds per millisecond

// One HL2 telemetry call at test time nowNs carrying temperature raw.
void feedHl2Temp(RadioModel& model, qint64 nowNs, quint16 raw)
{
    model.setPaTelemetryNowNsForTest(nowNs);
    model.handlePaTelemetryForTest(0, 0, raw, 0, 0, 0);
}

// Feeds raw every periodMs from fromMs up to and including toMs.
void feedHl2TempEvery(RadioModel& model, qint64 fromMs, qint64 toMs,
                      qint64 periodMs, quint16 raw)
{
    for (qint64 t = fromMs; t <= toMs; t += periodMs) {
        feedHl2Temp(model, t * kMs, raw);
    }
}

// Steady reading A, then a step to B; checks the average is between the
// two 400 ms after the step and has fully reached B 800 ms after the step
// plus one call. mi0bot console.cs:24927-24928, 24976, 24987
// [v2.10.3.13-beta2 @c26a8a4]: 100 samples, one every 8 ms, so 800 ms.
void checkStepSettlesIn800Ms(qint64 periodMs)
{
    constexpr quint16 kRawA = 628;   // about 0 C
    constexpr quint16 kRawB = 2048;  // about 113 C
    const double tempA = scaleHermesLiteTempCelsius(kRawA);
    const double tempB = scaleHermesLiteTempCelsius(kRawB);

    RadioModel model;
    model.setBoardForTest(HPSDRHW::HermesLite);

    // Two seconds of A fills the window with A at either rate.
    constexpr qint64 kStepMs = 2000;
    feedHl2TempEvery(model, 0, kStepMs - periodMs, periodMs, kRawA);
    QVERIFY2(qAbs(model.radioStatus().paTemperatureCelsius() - tempA) < 1e-6,
             qPrintable(QString::number(model.radioStatus().paTemperatureCelsius())));

    feedHl2TempEvery(model, kStepMs, kStepMs + 400, periodMs, kRawB);
    const double mid = model.radioStatus().paTemperatureCelsius();
    QVERIFY2(mid > tempA + 10.0 && mid < tempB - 10.0,
             qPrintable(QStringLiteral("400 ms after the step: %1 C, expected between %2 and %3")
                            .arg(mid).arg(tempA).arg(tempB)));

    feedHl2TempEvery(model, kStepMs + 400 + periodMs, kStepMs + 800 + periodMs,
                     periodMs, kRawB);
    const double settled = model.radioStatus().paTemperatureCelsius();
    QVERIFY2(qAbs(settled - tempB) < 1e-6,
             qPrintable(QStringLiteral("800 ms after the step: %1 C, expected %2")
                            .arg(settled).arg(tempB)));
}

} // namespace

class TstRadioModelHl2TempRouting : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { AppSettings::instance().clear(); }
    void init()         { AppSettings::instance().clear(); }
    void cleanup()      { AppSettings::instance().clear(); }

    // ── 1. HL2 board → exciter raw routes through scaleHermesLiteTempCelsius ──
    void hl2_publishes_temp_from_exciter_raw()
    {
        RadioModel model;
        model.setBoardForTest(HPSDRHW::HermesLite);

        QSignalSpy tempSpy(&model.radioStatus(),
                           &RadioStatus::paTemperatureChanged);
        QVERIFY(tempSpy.isValid());

        // raw=942 → ~25 °C per scaleHermesLiteTempCelsius pinned tests.
        model.handlePaTelemetryForTest(
            /*fwdRaw*/      0,
            /*revRaw*/      0,
            /*exciterRaw*/  942,
            /*userAdc0Raw*/ 0,
            /*userAdc1Raw*/ 0,
            /*supplyRaw*/   0);

        QCOMPARE(tempSpy.count(), 1);
        const double publishedC = model.radioStatus().paTemperatureCelsius();
        QVERIFY2(publishedC > 24.0 && publishedC < 26.0,
                 qPrintable(QStringLiteral("HL2 temp %1 °C — expected ~25").arg(publishedC)));
    }

    // ── HL2 PA current from user ADC0 (mi0bot HL2 current branch) ──────────
    // From mi0bot console.cs:24937-24941 [v2.10.3.13-beta2]:
    //   _ampsQueue.Enqueue(NetworkIO.getUserADC0());   // MI0BOT: HL2 temperature & current
    // and console.cs:25121-25131 convertToAmps (MI0BOT: HL2 current).
    // raw=1000 -> 0.5054 A; user ADC1 is not the HL2's current.
    void hl2_publishes_pa_current_from_user_adc0()
    {
        RadioModel model;
        model.setBoardForTest(HPSDRHW::HermesLite);
        QVERIFY(model.boardCapabilities().hasPaAmpsTelemetry);
        model.handlePaTelemetryForTest(
            /*fwdRaw*/      0,
            /*revRaw*/      0,
            /*exciterRaw*/  942,
            /*userAdc0Raw*/ 1000,
            /*userAdc1Raw*/ 3000,
            /*supplyRaw*/   0);
        const double amps = model.radioStatus().paCurrentAmps();
        QVERIFY2(qAbs(amps - 0.5054) < 1e-3, qPrintable(QString::number(amps, 'f', 6)));
    }

    // Non-HL2 current applies the saved volt calibration (Thetis AmpVoff /
    // AmpSens, console.cs:24937-24975 [v2.10.3.15]).
    void calibrated_pa_current_uses_the_volt_calibration()
    {
        RadioModel model;
        model.setHpsdrModelForTest(HPSDRModel::ANAN7000D);
        model.calibrationControllerMutable().setPaCurrentSensitivity(100.0);
        model.calibrationControllerMutable().setPaCurrentOffset(200.0);
        model.handlePaTelemetryForTest(0, 0, 0, /*userAdc0Raw*/ 0,
                                       /*userAdc1Raw*/ 2048, 0);
        const double amps = model.radioStatus().paCurrentAmps();
        QVERIFY2(qAbs(amps - 23.0061) < 1e-3, qPrintable(QString::number(amps, 'f', 6)));

        // The model's defaults when nothing was saved: 340 mV / 88.
        RadioModel fresh;
        fresh.setHpsdrModelForTest(HPSDRModel::ANAN7000D);
        fresh.handlePaTelemetryForTest(0, 0, 0, 0, 2048, 0);
        const double def = fresh.radioStatus().paCurrentAmps();
        QVERIFY2(qAbs(def - (2500.6105 - 340.0) / 88.0) < 1e-3,
                 qPrintable(QString::number(def, 'f', 6)));
    }

    // ── 2. HL2 board suppresses setExciterPowerMw with the temp ADC ─────────
    // The test seam force-engages TX (m_forceTxForTest=true), which would
    // otherwise let setExciterPowerMw(static_cast<int>(exciterRaw)) fire on
    // non-HL2 boards. On HL2, we should still publish 0 because the bytes
    // mean temperature, not exciter power.
    void hl2_suppresses_exciter_power()
    {
        RadioModel model;
        model.setBoardForTest(HPSDRHW::HermesLite);

        model.handlePaTelemetryForTest(
            /*fwdRaw*/      0,
            /*revRaw*/      0,
            /*exciterRaw*/  942,
            /*userAdc0Raw*/ 0,
            /*userAdc1Raw*/ 0,
            /*supplyRaw*/   0);

        // setExciterPowerMw must NOT have been called with 942.
        // On HL2 we publish 0 (matches mi0bot's intent — the field is
        // not surfaced as exciter power for HL2).
        QCOMPARE(model.radioStatus().exciterPowerMw(), 0);
    }

    // ── 3. Non-HL2 board applies Thetis OrionMKII piecewise curve ───────────
    // Saturn (ANAN_G2 family) routes raw exciter ADC counts through
    // scaleExciterPowerMw() → computeOrionMkIIExciterPower(), NOT a raw
    // passthrough.  The pre-F1 NereusSDR behaviour of publishing raw counts
    // as milliwatts was a bug; Thetis ALWAYS scales through the piecewise
    // curve for the ANAN_G2 / OrionMKII family.
    //
    // From Thetis console.cs:26001-26014 [v2.10.3.15] — exciter dispatch
    // (verbatim, inline tags preserved):
    //   case HPSDRModel.ORIONMKII:
    //   case HPSDRModel.ANAN7000D:
    //   case HPSDRModel.ANAN8000D:
    //   case HPSDRModel.ANAN_G2E: //N1GP G2E added
    //   case HPSDRModel.ANAN_G2:
    //   case HPSDRModel.ANAN_G2_1K:
    //   case HPSDRModel.ANVELINAPRO3:
    //   case HPSDRModel.REDPITAYA: //DH1KLM
    //     drivepwr = computeOrionMkIIExciterPower();  // piecewise-linear curve
    //
    // Math for raw=500 via computeOrionMkIIExciterPower
    // (PaTelemetryScaling.cpp:205-231 [v2.10.3.15]):
    //   adcCounts=500 <= 1340, <= 580, > 60
    //   result = (500.0 - 60.0) * 0.097656 = 42.96864
    //   static_cast<int>(42.96864f) = 42
    void non_hl2_scales_exciter_via_orionmkii_curve()
    {
        RadioModel model;
        model.setBoardForTest(HPSDRHW::Saturn);  // ANAN_G2 family

        QSignalSpy tempSpy(&model.radioStatus(),
                           &RadioStatus::paTemperatureChanged);
        QVERIFY(tempSpy.isValid());

        model.handlePaTelemetryForTest(
            /*fwdRaw*/      0,
            /*revRaw*/      0,
            /*exciterRaw*/  500,   // raw ADC count, NOT mW; curve yields 42 mW
            /*userAdc0Raw*/ 0,
            /*userAdc1Raw*/ 0,
            /*supplyRaw*/   0);

        // From Thetis console.cs:26001-26014 [v2.10.3.15]:
        // //N1GP G2E added — //DH1KLM (verbatim inline tags from upstream)
        // ANAN_G2 family always applies computeOrionMkIIExciterPower():
        // raw=500 → 42 mW (see math above).
        QCOMPARE(model.radioStatus().exciterPowerMw(), 42);
        // No HL2 temp publish on non-HL2 boards.
        QCOMPARE(tempSpy.count(), 0);
    }

    // ── 4. Averaging window smooths alternating raw samples ─────────────────
    // Feed raw=628 (≈0 °C) and raw=2048 (=113 °C) alternately; the ring
    // average lands between them.  Without averaging, the latest sample
    // would dominate.  This pins the ring-buffer behaviour against a
    // regression to last-write-wins.
    void hl2_averaging_window_smooths_noisy_input()
    {
        RadioModel model;
        model.setBoardForTest(HPSDRHW::HermesLite);

        // Single sample at raw=628 → ~0 °C.
        model.setPaTelemetryNowNsForTest(0);
        model.handlePaTelemetryForTest(
            /*fwdRaw*/      0,
            /*revRaw*/      0,
            /*exciterRaw*/  628,
            /*userAdc0Raw*/ 0,
            /*userAdc1Raw*/ 0,
            /*supplyRaw*/   0);
        const double tempAfterFirst = model.radioStatus().paTemperatureCelsius();
        QVERIFY2(tempAfterFirst > -2.0 && tempAfterFirst < 2.0,
                 qPrintable(QStringLiteral("first-sample temp %1 °C — expected ~0")
                                .arg(tempAfterFirst)));

        // Add a hot sample (raw=2048 → 113 °C). The ring now contains
        // {628, 2048}; the average is 1338, which scales to ~57 °C.
        // Pin the published temp to that midpoint so a regression that
        // skipped averaging would land at 113 °C and fail the bounds.
        // The hot sample arrives one 8 ms poll later, so it is sampled.
        model.setPaTelemetryNowNsForTest(8 * kMs);
        model.handlePaTelemetryForTest(
            /*fwdRaw*/      0,
            /*revRaw*/      0,
            /*exciterRaw*/  2048,
            /*userAdc0Raw*/ 0,
            /*userAdc1Raw*/ 0,
            /*supplyRaw*/   0);
        const double tempAfterSecond = model.radioStatus().paTemperatureCelsius();
        QVERIFY2(tempAfterSecond > 50.0 && tempAfterSecond < 65.0,
                 qPrintable(QStringLiteral("averaged temp %1 °C — expected ~57 (midpoint)")
                                .arg(tempAfterSecond)));
    }

    // ── 5. The window is 800 ms at the idle status rate (200 ms) ────────────
    void hl2_window_spans_800ms_at_idle_rate() { checkStepSettlesIn800Ms(200); }

    // ── 6. The window is 800 ms at the keyed status rate (1 ms) ─────────────
    void hl2_window_spans_800ms_at_keyed_rate() { checkStepSettlesIn800Ms(1); }

    // ── 7. The first reading shows at once ──────────────────────────────────
    void hl2_first_reading_shows_at_once()
    {
        RadioModel model;
        model.setBoardForTest(HPSDRHW::HermesLite);
        feedHl2Temp(model, 123456789, 2048);
        QVERIFY(qAbs(model.radioStatus().paTemperatureCelsius()
                     - scaleHermesLiteTempCelsius(2048)) < 1e-6);
    }

    // ── 8. Leaving Connected clears the window ──────────────────────────────
    // A reconnect, perhaps to another radio, shows its own first reading
    // at once, with nothing averaged in from before.
    void hl2_reading_after_disconnect_starts_fresh()
    {
        RadioModel model;
        model.setBoardForTest(HPSDRHW::HermesLite);
        model.setConnectionStateForTest(ConnectionState::Connected);
        feedHl2TempEvery(model, 0, 1000, 200, 628);
        model.setConnectionStateForTest(ConnectionState::Disconnected);
        model.setConnectionStateForTest(ConnectionState::Connected);
        // 100 ms later: by the old clock that is 12 polls of the old reading.
        feedHl2Temp(model, 1100 * kMs, 2048);
        const double temp = model.radioStatus().paTemperatureCelsius();
        QVERIFY2(qAbs(temp - scaleHermesLiteTempCelsius(2048)) < 1e-6,
                 qPrintable(QString::number(temp)));
    }
};

QTEST_GUILESS_MAIN(TstRadioModelHl2TempRouting)
#include "tst_radio_model_hl2_temp_routing.moc"
