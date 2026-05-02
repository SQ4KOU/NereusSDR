// =================================================================
// tests/tst_multimeter_unit_conversion.cpp  (NereusSDR)
// no-port-check: task 3.2 test file; references Thetis only for formula verification
// =================================================================
//
// Task 3.2 — MeterItem unit-mode fan-out (S / dBm / µV) tests.
//
// Covers:
//   MeterItem::formatValue() static helper — all three units, boundary
//   values derived from the IARU S-meter scale and 50-Ω µV formula.
//   MeterItem::setUnitMode / unitMode round-trip.
//   MeterItem::setShowDecimal / showDecimal round-trip.
//   SignalTextItem::setUnitMode() sync — verifies Units enum stays in
//   lockstep with the broadcast MeterUnit.
//
// Independently implemented from NereusSDR-native unit-conversion helpers.
// S-meter reference: IARU R1/R2/R3 Technical Recommendation T.001,
//   S9 = -73 dBm at HF, 6 dB per S unit.
// µV reference: 50 Ω terminated input; formula verified against Thetis
// Common.UVfromDBM (console.cs) [v2.10.3.13] for compatibility.
//
// Uses QTEST_APPLESS_MAIN (no QApplication, WDSP-free, pure data logic).
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-01 — Created in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via Anthropic
//                 Claude Code.
// =================================================================

#include <QTest>

#include "gui/meters/MeterItem.h"
#include "gui/meters/SignalTextItem.h"

using namespace NereusSDR;

class TestMultimeterUnitConversion : public QObject {
    Q_OBJECT

private slots:

    // ── S-meter unit formatting ─────────────────────────────────────────────��─

    void s_meter_s9_equals_minus73_dbm()
    {
        // IARU: S9 = -73 dBm at HF
        QCOMPARE(MeterItem::formatValue(-73.0f, MeterItem::MeterUnit::S),
                 QStringLiteral("S9"));
    }

    void s_meter_s8_equals_minus79_dbm()
    {
        // S8 = S9 - 1 S-unit = -73 - 6 = -79 dBm
        QCOMPARE(MeterItem::formatValue(-79.0f, MeterItem::MeterUnit::S),
                 QStringLiteral("S8"));
    }

    void s_meter_s0_at_minus127_dbm()
    {
        // S0 = -73 - 9*6 = -127 dBm
        QCOMPARE(MeterItem::formatValue(-127.0f, MeterItem::MeterUnit::S),
                 QStringLiteral("S0"));
    }

    void s_meter_above_s9_shows_s9_plus_offset()
    {
        // -43 dBm = S9 + 30 dB  ((-43) - (-73) = 30)
        QCOMPARE(MeterItem::formatValue(-43.0f, MeterItem::MeterUnit::S),
                 QStringLiteral("S9+30"));
    }

    void s_meter_s9_plus_12()
    {
        // -63 dBm: diff = -63 - (-73) = +10 dB above S9
        // sUnits = qRound(9 + 10/6.0) = qRound(10.667) = 11
        // over = (11 - 9) * 6 = 12 → "S9+12"
        // (The formula rounds to nearest S-unit boundary, not the raw dB offset.)
        QCOMPARE(MeterItem::formatValue(-63.0f, MeterItem::MeterUnit::S),
                 QStringLiteral("S9+12"));
    }

    void s_meter_below_s0_clamped()
    {
        // Very low signal — below S0, should clamp to "S0" not go negative
        const QString s = MeterItem::formatValue(-200.0f, MeterItem::MeterUnit::S);
        QCOMPARE(s, QStringLiteral("S0"));
    }

    // ── dBm unit formatting ───────────────────────────────────────────────────

    void dbm_with_decimal()
    {
        QCOMPARE(MeterItem::formatValue(-73.5f, MeterItem::MeterUnit::dBm, true),
                 QStringLiteral("-73.5"));
    }

    void dbm_without_decimal_rounds_to_int()
    {
        // -73.5 rounds to -74 (qRound(-73.5f) → -74 via "round half away from zero")
        QCOMPARE(MeterItem::formatValue(-73.5f, MeterItem::MeterUnit::dBm, false),
                 QStringLiteral("-74"));
    }

    void dbm_integer_value_with_decimal()
    {
        // -73.0 → "-73.0" (one decimal place)
        QCOMPARE(MeterItem::formatValue(-73.0f, MeterItem::MeterUnit::dBm, true),
                 QStringLiteral("-73.0"));
    }

    // ── µV unit formatting ────────────────────────────────────────────────────

    void uv_contains_mu_symbol()
    {
        const QString s = MeterItem::formatValue(-73.0f, MeterItem::MeterUnit::uV);
        QVERIFY2(s.contains(QStringLiteral("µV")),
                 qPrintable(QStringLiteral("Expected µV in: ") + s));
    }

    void uv_at_s9_is_approximately_50_microvolts()
    {
        // -73 dBm at 50 Ω ≈ 50 µV.
        // Physics: P = 10^(-73/10) * 1e-3 W
        //          V = sqrt(P * 50)
        //          uV = V * 1e6 ≈ 50.12 µV
        const QString s = MeterItem::formatValue(-73.0f, MeterItem::MeterUnit::uV);
        QVERIFY2(s.startsWith(QStringLiteral("50")),
                 qPrintable(QStringLiteral("Expected ~50 µV, got: ") + s));
    }

    void uv_very_small_signal_stays_below_1()
    {
        // -120 dBm → very small µV value (< 1 µV)
        // -120 dBm: P = 1e-12 * 1e-3, V = sqrt(1e-12 * 1e-3 * 50), uV = V * 1e6 ≈ 0.22 µV
        const QString s = MeterItem::formatValue(-120.0f, MeterItem::MeterUnit::uV);
        QVERIFY2(s.contains(QStringLiteral("µV")),
                 qPrintable(QStringLiteral("Expected µV in: ") + s));
        // Value should start with "0." (less than 1 µV)
        QVERIFY2(s.startsWith(QStringLiteral("0.")),
                 qPrintable(QStringLiteral("Expected sub-1 µV, got: ") + s));
    }

    // ── MeterItem accessor round-trips ────────────────────────────────────────

    void unit_mode_defaults_to_dbm()
    {
        BarItem item;
        QCOMPARE(item.unitMode(), MeterItem::MeterUnit::dBm);
    }

    void unit_mode_round_trips_s()
    {
        BarItem item;
        item.setUnitMode(MeterItem::MeterUnit::S);
        QCOMPARE(item.unitMode(), MeterItem::MeterUnit::S);
    }

    void unit_mode_round_trips_uv()
    {
        BarItem item;
        item.setUnitMode(MeterItem::MeterUnit::uV);
        QCOMPARE(item.unitMode(), MeterItem::MeterUnit::uV);
    }

    void show_decimal_defaults_true()
    {
        BarItem item;
        QVERIFY(item.showDecimal());
    }

    void show_decimal_round_trips()
    {
        BarItem item;
        item.setShowDecimal(false);
        QVERIFY(!item.showDecimal());
        item.setShowDecimal(true);
        QVERIFY(item.showDecimal());
    }

    // ── SignalTextItem setUnitMode() sync ─────────────────────────────────────

    void signal_text_item_unit_mode_s_syncs_units_enum()
    {
        SignalTextItem item;
        item.setUnitMode(MeterItem::MeterUnit::S);
        QCOMPARE(item.unitMode(), MeterItem::MeterUnit::S);
        QCOMPARE(item.units(), SignalTextItem::Units::SUnits);
    }

    void signal_text_item_unit_mode_uv_syncs_units_enum()
    {
        SignalTextItem item;
        item.setUnitMode(MeterItem::MeterUnit::uV);
        QCOMPARE(item.unitMode(), MeterItem::MeterUnit::uV);
        QCOMPARE(item.units(), SignalTextItem::Units::Uv);
    }

    void signal_text_item_unit_mode_dbm_syncs_units_enum()
    {
        SignalTextItem item;
        item.setUnitMode(MeterItem::MeterUnit::S);   // set to something else first
        item.setUnitMode(MeterItem::MeterUnit::dBm);
        QCOMPARE(item.unitMode(), MeterItem::MeterUnit::dBm);
        QCOMPARE(item.units(), SignalTextItem::Units::Dbm);
    }
};

QTEST_APPLESS_MAIN(TestMultimeterUnitConversion)
#include "tst_multimeter_unit_conversion.moc"
