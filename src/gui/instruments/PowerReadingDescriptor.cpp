// no-port-check: NereusSDR-original. See PowerReadingDescriptor.h.
// Modification history (NereusSDR):
// 2026-10-12 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
#include "gui/instruments/PowerReadingDescriptor.h"
#include "gui/meters/MeterPoller.h"
#include <cmath>
namespace NereusSDR {
ReadingDescriptor powerReadingDescriptor(double limitWatts, double topWatts)
{
    ReadingDescriptor d;
    d.bindingId = MeterBinding::TxPower;
    d.label = QStringLiteral("TX: Forward Power");
    d.unit = QStringLiteral("W");
    d.hasScale = true;
    d.min = 0.0;
    d.max = topWatts;
    d.threshold = limitWatts;
    d.decimals = limitWatts <= 10.0 ? 1 : 0;
    for (int quarter = 0; quarter <= 4; ++quarter) {
        const double at = topWatts * quarter / 4.0;
        const QString label = at <= 1000.0
            ? QString::number(at, 'f', at == std::round(at) ? 0 : 1)
            : QString::number(at / 1000.0, 'g', 3) + QLatin1Char('k');
        d.ticks.append({at, label});
    }
    for (int eighth = 1; eighth < 8; eighth += 2) {
        d.minorTicks.append(topWatts * eighth / 8.0);
    }
    return d;
}
}
