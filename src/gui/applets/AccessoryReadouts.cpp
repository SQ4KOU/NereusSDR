// src/gui/applets/AccessoryReadouts.cpp (NereusSDR)
//
// See AccessoryReadouts.h.
//
// no-port-check: NereusSDR-original file; what its values rest on is in
// the header.
//
// Modification history (NereusSDR):
//   2026-10-12 - Shared accessory display readings and independent legal
//                 marker / display scale by J.J. Boyd (KG4VCF),
//                 AI-assisted via OpenAI Codex.
//   2026-10-12 - Created for the tuner and amplifier panels.
//                J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.

#include "gui/applets/AccessoryReadouts.h"

#include "gui/instruments/BarInstrument.h"
#include "gui/instruments/PowerReadingDescriptor.h"

#include <QWidget>

namespace NereusSDR::AccessoryReadouts {

QString forwardName()     { return QStringLiteral("Forward"); }
QString reflectedName()   { return QStringLiteral("Reflected"); }
QString swrName()         { return QStringLiteral("SWR"); }
QString temperatureName() { return QStringLiteral("Temp"); }

ReadingDescriptor forwardPower(double maxWatts, double limitWatts)
{
    ReadingDescriptor d = powerReadingDescriptor(limitWatts, maxWatts);
    d.bindingId = -1;
    d.label = QStringLiteral("Accessory: Forward Power");
    // Accessory panels retain whole watts, including their barefoot scale.
    d.decimals = 0;
    return d;
}

ReadingDescriptor reflectedPower()
{
    ReadingDescriptor d;
    d.label    = QStringLiteral("Accessory: Reflected Power");
    d.unit     = QStringLiteral("W");
    d.hasScale = true;
    d.min      = 0.0;
    d.max      = 500.0;
    d.decimals = 0;
    d.ticks = {{0.0, QStringLiteral("0")},     {125.0, QStringLiteral("125")},
               {250.0, QStringLiteral("250")}, {375.0, QStringLiteral("375")},
               {500.0, QStringLiteral("500")}};
    return d;
}

ReadingDescriptor swr()
{
    ReadingDescriptor d;
    d.label     = QStringLiteral("Accessory: SWR");
    d.hasScale  = true;
    d.min       = 1.0;
    d.max       = 3.0;
    d.threshold = 2.5;
    d.decimals  = 2;
    d.ticks = {{1.0, QStringLiteral("1")}, {1.5, QStringLiteral("1.5")},
               {2.0, QStringLiteral("2")}, {2.5, QStringLiteral("2.5")},
               {3.0, QStringLiteral("3")}};
    d.minorTicks = {1.25, 1.75, 2.25, 2.75};
    d.format = &swrText;
    return d;
}

ReadingDescriptor temperature()
{
    ReadingDescriptor d;
    d.label     = QStringLiteral("Accessory: Temperature");
    d.unit      = QStringLiteral("°C");
    d.hasScale  = true;
    d.min       = 0.0;
    d.max       = 100.0;
    d.threshold = 80.0;
    d.decimals  = 0;
    d.ticks = {{0.0, QStringLiteral("0")},   {30.0, QStringLiteral("30")},
               {55.0, QStringLiteral("55")}, {80.0, QStringLiteral("80")},
               {100.0, QStringLiteral("100")}};
    return d;
}

QString swrText(double swr)
{
    if (swr < 9.995) {
        return QString::number(swr, 'f', 2);
    }
    if (swr < 99.95) {
        return QString::number(swr, 'f', 1);
    }
    return QStringLiteral(">99");
}

double reflectedWatts(double forwardWatts, double swr)
{
    if (forwardWatts <= 0.0 || swr <= 1.0) {
        return 0.0;
    }
    if (swr >= kSwrCap) {
        return forwardWatts;
    }
    // The definition of SWR: the reflection coefficient's magnitude is
    // (swr - 1) / (swr + 1), and power goes with its square.
    const double g = (swr - 1.0) / (swr + 1.0);
    return forwardWatts * g * g;
}

BarInstrument* makeLargeBar(const ReadingDescriptor& d, const QString& name,
                            QWidget* parent)
{
    auto* bar = new BarInstrument(parent);
    bar->setReading(d, name);
    bar->setFooterDetailVisible(false);
    // The footer's height is known once the style sheets above it have
    // been applied; below that height the instrument hides its footer.
    bar->ensurePolished();
    bar->setFixedHeight(bar->sizeHint().height());
    return bar;
}

BarInstrument* makeSlimBar(const ReadingDescriptor& d, const QString& name,
                           QWidget* parent)
{
    auto* bar = new BarInstrument(parent);
    bar->setReading(d, name);
    bar->setCompact(true);
    bar->setCompactValuePx(kSlimValuePx);
    bar->setFixedHeight(kSlimRowHeight);
    return bar;
}

} // namespace NereusSDR::AccessoryReadouts
