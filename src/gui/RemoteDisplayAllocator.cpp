// =================================================================
// src/gui/RemoteDisplayAllocator.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original GUI display-quality allocation.
//
// =================================================================

#include "gui/RemoteDisplayAllocator.h"

#include <algorithm>

namespace NereusSDR {
namespace {

constexpr int kMaximumPans = 8;
constexpr int kMaximumPixels = 4096;
constexpr int kMaximumFps = 60;
constexpr int kMaximumWaterfallPeriodMs = 65'535;
constexpr int kUsefulPixels = 256;
constexpr int kUsefulFps = 10;

struct MutableQuality {
    const RemoteDisplayIntent* intent = nullptr;
    int pixels = 0;
    int fps = 0;
    bool suspended = false;
};

bool isValidIntent(const RemoteDisplayIntent& intent)
{
    return !intent.panId.isEmpty()
        && intent.pixels >= 1 && intent.pixels <= kMaximumPixels
        && intent.fps >= 1 && intent.fps <= kMaximumFps
        && intent.waterfallPeriodMs >= 1
        && intent.waterfallPeriodMs <= kMaximumWaterfallPeriodMs;
}

int framesPerLine(const RemoteDisplayIntent& intent, int fps)
{
    if (fps == 0) {
        return 0;
    }
    const quint64 numerator = static_cast<quint64>(intent.waterfallPeriodMs)
        * static_cast<quint64>(fps);
    const quint64 roundedUp = (numerator + 999) / 1'000;
    return static_cast<int>(std::clamp<quint64>(roundedUp, 1, 65'535));
}

std::optional<DisplayBudgetCharge> totalCharge(const QList<MutableQuality>& qualities,
                                                bool ps3Enabled)
{
    QList<DisplayBudgetCharge> charges;
    charges.reserve(qualities.size() + (ps3Enabled ? 1 : 0));
    if (ps3Enabled) {
        charges.append(ps3DisplayCharge());
    }
    for (const MutableQuality& quality : qualities) {
        if (quality.suspended) {
            continue;
        }
        const auto cost = spectrumDisplayCost(quality.pixels, quality.fps,
                                              quality.intent->includeWidePlane);
        if (!cost) {
            return std::nullopt;
        }
        charges.append(cost->charge);
    }
    return sumDisplayCharges(charges);
}

bool fits(const DisplayBudgetLimits& limits, const QList<MutableQuality>& qualities,
          bool ps3Enabled)
{
    const auto charge = totalCharge(qualities, ps3Enabled);
    return charge && displayChargeFits(limits, *charge);
}

bool lowerFps(const DisplayBudgetLimits& limits, QList<MutableQuality>& qualities,
              const QList<int>& indexes, bool ps3Enabled)
{
    bool changed = true;
    while (changed) {
        changed = false;
        for (int index : indexes) {
            MutableQuality& quality = qualities[index];
            const int floor = std::min(quality.intent->fps, kUsefulFps);
            if (quality.suspended || quality.fps <= floor) {
                continue;
            }
            --quality.fps;
            changed = true;
            if (fits(limits, qualities, ps3Enabled)) {
                return true;
            }
        }
    }
    return fits(limits, qualities, ps3Enabled);
}

bool lowerPixels(const DisplayBudgetLimits& limits, QList<MutableQuality>& qualities,
                 const QList<int>& indexes, bool ps3Enabled)
{
    bool changed = true;
    while (changed) {
        changed = false;
        for (int index : indexes) {
            MutableQuality& quality = qualities[index];
            const int floor = std::min(quality.intent->pixels, kUsefulPixels);
            if (quality.suspended || quality.pixels <= floor) {
                continue;
            }
            --quality.pixels;
            changed = true;
            if (fits(limits, qualities, ps3Enabled)) {
                return true;
            }
        }
    }
    return fits(limits, qualities, ps3Enabled);
}

bool reduceToFit(const DisplayBudgetLimits& limits, QList<MutableQuality>& qualities,
                 const QList<int>& backgrounds, const QList<int>& actives,
                 bool ps3Enabled)
{
    return fits(limits, qualities, ps3Enabled)
        || lowerFps(limits, qualities, backgrounds, ps3Enabled)
        || lowerPixels(limits, qualities, backgrounds, ps3Enabled)
        || lowerFps(limits, qualities, actives, ps3Enabled)
        || lowerPixels(limits, qualities, actives, ps3Enabled);
}

RemoteDisplayAllocation makeAllocation(const QList<MutableQuality>& qualities,
                                       bool ps3Enabled)
{
    RemoteDisplayAllocation allocation;
    allocation.pans.reserve(qualities.size());
    for (const MutableQuality& source : qualities) {
        RemoteDisplayQuality quality;
        quality.panId = source.intent->panId;
        quality.suspended = source.suspended;
        if (!source.suspended) {
            quality.pixels = source.pixels;
            quality.fps = source.fps;
            quality.framesPerLine = framesPerLine(*source.intent, source.fps);
            quality.charge = spectrumDisplayCost(source.pixels, source.fps,
                                                 source.intent->includeWidePlane)->charge;
        }
        allocation.pans.append(quality);
    }
    allocation.total = *totalCharge(qualities, ps3Enabled);
    return allocation;
}

} // namespace

std::optional<RemoteDisplayAllocation> allocateRemoteDisplay(
    const DisplayBudgetLimits& limits, const QList<RemoteDisplayIntent>& intents,
    bool ps3Enabled, QString* error)
{
    if (error) {
        error->clear();
    }
    const auto fail = [error](const QString& reason) -> std::optional<RemoteDisplayAllocation> {
        if (error) {
            *error = reason;
        }
        return std::nullopt;
    };

    if (!limits.isValid()) {
        return fail(QStringLiteral("Display budget limits are invalid."));
    }
    if (intents.size() > kMaximumPans) {
        return fail(QStringLiteral("At most eight remote display pans are supported."));
    }

    QList<RemoteDisplayIntent> sorted = intents;
    std::sort(sorted.begin(), sorted.end(), [](const RemoteDisplayIntent& left,
                                                const RemoteDisplayIntent& right) {
        return left.panId < right.panId;
    });

    int activeCount = 0;
    for (int index = 0; index < sorted.size(); ++index) {
        const RemoteDisplayIntent& intent = sorted[index];
        if (!isValidIntent(intent)) {
            return fail(QStringLiteral("Remote display intent '%1' has an invalid range.")
                            .arg(intent.panId));
        }
        if (index != 0 && intent.panId == sorted[index - 1].panId) {
            return fail(QStringLiteral("Remote display pan IDs must be unique."));
        }
        activeCount += intent.active ? 1 : 0;
    }
    if (activeCount > 1) {
        return fail(QStringLiteral("At most one remote display pan may be active."));
    }

    const DisplayBudgetCharge ps3Charge = ps3DisplayCharge();
    if (ps3Enabled && !displayChargeFits(limits, ps3Charge)) {
        return fail(QStringLiteral("PureSignal display reservation does not fit the display budget."));
    }

    QList<bool> suspended(sorted.size(), false);
    while (true) {
        QList<MutableQuality> qualities;
        qualities.reserve(sorted.size());
        QList<int> backgrounds;
        QList<int> actives;
        for (int index = 0; index < sorted.size(); ++index) {
            const RemoteDisplayIntent& intent = sorted[index];
            qualities.append({&intent, intent.pixels, intent.fps, suspended[index]});
            if (!suspended[index]) {
                (intent.active ? actives : backgrounds).append(index);
            }
        }

        if (reduceToFit(limits, qualities, backgrounds, actives, ps3Enabled)) {
            return makeAllocation(qualities, ps3Enabled);
        }

        int suspendIndex = -1;
        for (int index = sorted.size() - 1; index >= 0; --index) {
            if (!suspended[index] && !sorted[index].active) {
                suspendIndex = index;
                break;
            }
        }
        if (suspendIndex >= 0) {
            suspended[suspendIndex] = true;
            continue;
        }

        for (int index = 0; index < sorted.size(); ++index) {
            suspended[index] = true;
        }
        QList<MutableQuality> allSuspended;
        allSuspended.reserve(sorted.size());
        for (const RemoteDisplayIntent& intent : sorted) {
            allSuspended.append({&intent, 0, 0, true});
        }
        if (fits(limits, allSuspended, ps3Enabled)) {
            return makeAllocation(allSuspended, ps3Enabled);
        }
        return fail(QStringLiteral("Display budget cannot reserve requested display demand."));
    }
}

} // namespace NereusSDR
