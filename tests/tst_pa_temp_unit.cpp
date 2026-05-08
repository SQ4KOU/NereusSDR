// =================================================================
// tests/tst_pa_temp_unit.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original test file. PaTempUnit is a
// NereusSDR-native UX module (mi0bot-Thetis only formats °C; the
// click-to-toggle preference does not exist upstream).
//
// Verifies:
//   1. Default unit is Celsius when AppSettings has no PaTempUnit key.
//   2. setUnit() round-trips through AppSettings.
//   3. setUnit() emits unitChanged exactly once on a real change, and
//      not at all on idempotent set-to-same-value.
//   4. format() produces "%.1f°C" / "%.1f°F" with the canonical Celsius
//      input, switching according to currentUnit().
//   5. celsiusToFahrenheit() pins the standard formula on three points.
// =================================================================

#include <QtTest/QtTest>
#include <QSignalSpy>

#include "core/AppSettings.h"
#include "core/PaTempUnit.h"

using namespace NereusSDR;

class TestPaTempUnit : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { AppSettings::instance().clear(); }
    void init()         { AppSettings::instance().clear(); }
    void cleanup()      { AppSettings::instance().clear(); }

    // ── 1. Default is Celsius ───────────────────────────────────────────────
    void default_unit_is_celsius()
    {
        QCOMPARE(PaTempUnitNotifier::currentUnit(), PaTempUnit::Celsius);
    }

    // ── 2. setUnit round-trips through AppSettings ──────────────────────────
    void set_unit_round_trips()
    {
        PaTempUnitNotifier::setUnit(PaTempUnit::Fahrenheit);
        QCOMPARE(PaTempUnitNotifier::currentUnit(), PaTempUnit::Fahrenheit);
        QCOMPARE(AppSettings::instance().value(QStringLiteral("PaTempUnit"))
                     .toString(),
                 QStringLiteral("F"));

        PaTempUnitNotifier::setUnit(PaTempUnit::Celsius);
        QCOMPARE(PaTempUnitNotifier::currentUnit(), PaTempUnit::Celsius);
        QCOMPARE(AppSettings::instance().value(QStringLiteral("PaTempUnit"))
                     .toString(),
                 QStringLiteral("C"));
    }

    // ── 3. unitChanged fires only on real change ────────────────────────────
    void unit_changed_signal_fires_once_per_real_change()
    {
        QSignalSpy spy(&PaTempUnitNotifier::instance(),
                       &PaTempUnitNotifier::unitChanged);
        QVERIFY(spy.isValid());

        // Start at default Celsius (cleared by init()).
        PaTempUnitNotifier::setUnit(PaTempUnit::Celsius);  // no-op
        QCOMPARE(spy.count(), 0);

        PaTempUnitNotifier::setUnit(PaTempUnit::Fahrenheit);  // real change
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.last().at(0).value<PaTempUnit>(), PaTempUnit::Fahrenheit);

        PaTempUnitNotifier::setUnit(PaTempUnit::Fahrenheit);  // idempotent
        QCOMPARE(spy.count(), 1);

        PaTempUnitNotifier::setUnit(PaTempUnit::Celsius);  // real change back
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.last().at(0).value<PaTempUnit>(), PaTempUnit::Celsius);
    }

    // ── 4. format() honours currentUnit ─────────────────────────────────────
    void format_celsius_default()
    {
        QCOMPARE(PaTempUnitNotifier::format(25.0),
                 QString::fromUtf8("25.0\xC2\xB0""C"));
        QCOMPARE(PaTempUnitNotifier::format(0.0),
                 QString::fromUtf8("0.0\xC2\xB0""C"));
        QCOMPARE(PaTempUnitNotifier::format(-12.5),
                 QString::fromUtf8("-12.5\xC2\xB0""C"));
    }

    void format_fahrenheit_after_toggle()
    {
        PaTempUnitNotifier::setUnit(PaTempUnit::Fahrenheit);
        // 25°C → 77°F, 0°C → 32°F, 100°C → 212°F.
        QCOMPARE(PaTempUnitNotifier::format(25.0),
                 QString::fromUtf8("77.0\xC2\xB0""F"));
        QCOMPARE(PaTempUnitNotifier::format(0.0),
                 QString::fromUtf8("32.0\xC2\xB0""F"));
        QCOMPARE(PaTempUnitNotifier::format(100.0),
                 QString::fromUtf8("212.0\xC2\xB0""F"));
    }

    // ── 5. celsiusToFahrenheit math ─────────────────────────────────────────
    void celsius_to_fahrenheit_math()
    {
        QCOMPARE(PaTempUnitNotifier::celsiusToFahrenheit(0.0), 32.0);
        QCOMPARE(PaTempUnitNotifier::celsiusToFahrenheit(100.0), 212.0);
        QCOMPARE(PaTempUnitNotifier::celsiusToFahrenheit(-40.0), -40.0);  // intersection
    }
};

QTEST_GUILESS_MAIN(TestPaTempUnit)
#include "tst_pa_temp_unit.moc"
