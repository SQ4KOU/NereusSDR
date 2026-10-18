// =================================================================
// src/core/PaTempUnit.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original.  See PaTempUnit.h for the full
// rationale (Thetis hard-codes °C; NereusSDR adds a click-to-toggle
// preference shared across all PA-temperature surfaces).
// =================================================================

#include "core/PaTempUnit.h"

#include "core/AppSettings.h"

#include <QGlobalStatic>

namespace NereusSDR {

namespace {

constexpr auto kSettingsKey = "PaTempUnit";
constexpr auto kCelsiusToken = "C";
constexpr auto kFahrenheitToken = "F";

PaTempUnit decodeToken(const QString& token) noexcept
{
    return token == QLatin1String(kFahrenheitToken)
                   ? PaTempUnit::Fahrenheit
                   : PaTempUnit::Celsius;
}

QLatin1String encodeUnit(PaTempUnit unit) noexcept
{
    return unit == PaTempUnit::Fahrenheit
                   ? QLatin1String(kFahrenheitToken)
                   : QLatin1String(kCelsiusToken);
}

}  // namespace

Q_GLOBAL_STATIC(PaTempUnitNotifier, kPaTempUnitNotifier)

PaTempUnitNotifier::PaTempUnitNotifier() = default;

PaTempUnitNotifier& PaTempUnitNotifier::instance()
{
    return *kPaTempUnitNotifier;
}

PaTempUnit PaTempUnitNotifier::currentUnit()
{
    const QString token =
        AppSettings::instance()
            .value(QLatin1String(kSettingsKey),
                   QLatin1String(kCelsiusToken))
            .toString();
    return decodeToken(token);
}

void PaTempUnitNotifier::setUnit(PaTempUnit unit)
{
    if (currentUnit() == unit) { return; }
    AppSettings::instance().setValue(QLatin1String(kSettingsKey),
                                     encodeUnit(unit));
    emit instance().unitChanged(unit);
}

double PaTempUnitNotifier::celsiusToFahrenheit(double celsius) noexcept
{
    return celsius * 9.0 / 5.0 + 32.0;
}

QString PaTempUnitNotifier::format(double celsius)
{
    if (currentUnit() == PaTempUnit::Fahrenheit) {
        return QString::asprintf("%.1f°F", celsiusToFahrenheit(celsius));
    }
    return QString::asprintf("%.1f°C", celsius);
}

}  // namespace NereusSDR
