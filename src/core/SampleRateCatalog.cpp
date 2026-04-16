// src/core/SampleRateCatalog.cpp
//
// See header for Thetis source citations.

#include "SampleRateCatalog.h"

#include "AppSettings.h"
#include "LogCategories.h"

#include <QString>
#include <QVariant>

#include <algorithm>
#include <array>
#include <span>

namespace NereusSDR {

namespace {

std::span<const int> masterListFor(ProtocolVersion proto, HPSDRModel model) noexcept
{
    if (proto == ProtocolVersion::Protocol1) {
        if (model == HPSDRModel::REDPITAYA) {
            return {kP1RatesRedPitaya, std::size(kP1RatesRedPitaya)};
        }
        return {kP1RatesBase, std::size(kP1RatesBase)};
    }
    // Protocol 2 — every ETH board gets the full list.
    return {kP2Rates, std::size(kP2Rates)};
}

} // namespace

std::vector<int> allowedSampleRates(ProtocolVersion proto,
                                     const BoardCapabilities& caps,
                                     HPSDRModel model)
{
    const auto master = masterListFor(proto, model);
    std::vector<int> out;
    out.reserve(master.size());
    for (int rate : master) {
        // Skip zero-sentinel slots in caps.sampleRates, include only rates
        // the board actually supports.
        const bool supported = std::any_of(caps.sampleRates.begin(),
                                            caps.sampleRates.end(),
                                            [rate](int r) { return r == rate; });
        if (supported) {
            out.push_back(rate);
        }
    }
    return out;
}

int defaultSampleRate(ProtocolVersion proto,
                      const BoardCapabilities& caps,
                      HPSDRModel model)
{
    const auto allowed = allowedSampleRates(proto, caps, model);
    if (allowed.empty()) {
        // Board registry is broken — caller should never hit this, but
        // returning 0 is safer than reading past end. Log and let the
        // caller decide.
        qCWarning(lcConnection) << "defaultSampleRate: no allowed rates for"
                                 << static_cast<int>(proto) << static_cast<int>(model);
        return 0;
    }
    if (std::find(allowed.begin(), allowed.end(), kDefaultSampleRate) != allowed.end()) {
        return kDefaultSampleRate;
    }
    return allowed.front();
}

int resolveSampleRate(const AppSettings& settings,
                      const QString& mac,
                      ProtocolVersion proto,
                      const BoardCapabilities& caps,
                      HPSDRModel model)
{
    const int persisted = settings.hardwareValue(
        mac, QStringLiteral("radioInfo/sampleRate")).toInt();
    if (persisted <= 0) {
        return defaultSampleRate(proto, caps, model);
    }
    const auto allowed = allowedSampleRates(proto, caps, model);
    if (std::find(allowed.begin(), allowed.end(), persisted) == allowed.end()) {
        qCWarning(lcConnection) << "Persisted sample rate" << persisted
                                 << "not valid for" << mac
                                 << "— falling back to default";
        return defaultSampleRate(proto, caps, model);
    }
    return persisted;
}

int resolveActiveRxCount(const AppSettings& settings,
                         const QString& mac,
                         const BoardCapabilities& caps)
{
    const int persisted = settings.hardwareValue(
        mac, QStringLiteral("radioInfo/activeRxCount")).toInt();
    if (persisted < 1) {
        return 1;
    }
    return std::min(persisted, caps.maxReceivers);
}

} // namespace NereusSDR
