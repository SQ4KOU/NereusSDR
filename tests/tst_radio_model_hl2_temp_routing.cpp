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
//   - non_hl2_keeps_exciter_power
//       Non-HL2 board (Saturn / ANAN_G2) sees setExciterPowerMw fire
//       with the raw value as before, and setPaTemperature is NOT
//       fired by the HL2 branch.
//   - hl2_averaging_window_smooths_noisy_input
//       Alternating high/low raw samples land at a midpoint °C that
//       sits between the two single-sample values (proves the ring
//       buffer averages, not just last-write-wins).
// =================================================================

#include <QtTest/QtTest>
#include <QSignalSpy>

#include "core/AppSettings.h"
#include "core/HpsdrModel.h"
#include "core/RadioStatus.h"
#include "models/RadioModel.h"

using namespace NereusSDR;

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

    // ── 3. Non-HL2 board still publishes exciter mW from the raw value ──────
    // Saturn (ANAN_G2 family) keeps the original semantic: exciter_power
    // C&C bytes carry exciter mW and the temp ring is untouched.
    void non_hl2_keeps_exciter_power()
    {
        RadioModel model;
        model.setBoardForTest(HPSDRHW::Saturn);  // ANAN_G2 family

        QSignalSpy tempSpy(&model.radioStatus(),
                           &RadioStatus::paTemperatureChanged);
        QVERIFY(tempSpy.isValid());

        model.handlePaTelemetryForTest(
            /*fwdRaw*/      0,
            /*revRaw*/      0,
            /*exciterRaw*/  500,   // would map to "500 mW exciter" on G2
            /*userAdc0Raw*/ 0,
            /*userAdc1Raw*/ 0,
            /*supplyRaw*/   0);

        QCOMPARE(model.radioStatus().exciterPowerMw(), 500);
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
};

QTEST_GUILESS_MAIN(TstRadioModelHl2TempRouting)
#include "tst_radio_model_hl2_temp_routing.moc"
