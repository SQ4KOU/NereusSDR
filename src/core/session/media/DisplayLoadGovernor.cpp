// =================================================================
// src/core/session/media/DisplayLoadGovernor.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. See DisplayLoadGovernor.h.
//
// =================================================================

#include "core/session/media/DisplayLoadGovernor.h"

#include <algorithm>
#include <cmath>

namespace NereusSDR {
namespace {

quint64 scaled(quint64 value, double scale)
{
    return static_cast<quint64>(std::floor(static_cast<double>(value) * scale));
}

} // namespace

DisplayBudgetCharge DisplayLoadGovernor::floorCharge()
{
    const auto pan = spectrumDisplayCost(kFloorPixels, kFloorFps, true);
    return sumDisplayCharges({ps3DisplayCharge(), pan ? pan->charge : DisplayBudgetCharge{}})
        .value_or(DisplayBudgetCharge{});
}

DisplayBudgetLimits DisplayLoadGovernor::computedCeiling()
{
    const auto pan = spectrumDisplayCost(DisplayCodecEncoder::kMaxSamplesPerPlane,
                                         static_cast<int>(kMaximumSpectrumDisplayFramesPerSecond),
                                         true);
    QList<DisplayBudgetCharge> charges{ps3DisplayCharge()};
    for (int i = 0; i < kCeilingPans; ++i) {
        charges.append(pan ? pan->charge : DisplayBudgetCharge{});
    }
    const DisplayBudgetCharge total = sumDisplayCharges(charges).value_or(DisplayBudgetCharge{});
    return DisplayBudgetLimits{total.applicationBytesPerSecond,
                               total.spectrumSampleUnitsPerSecond, 1};
}

DisplayLoadGovernor::DisplayLoadGovernor(DisplayBudgetLimits ceiling)
    : m_ceiling(ceiling)
    , m_limits(ceiling)
{
}

quint32 DisplayLoadGovernor::nextGeneration() const
{
    const quint32 next = m_limits.generation + 1;
    return next == 0 ? 1 : next;
}

std::optional<DisplayLoadDecision> DisplayLoadGovernor::update(const DisplayLoadReading& reading)
{
    const bool hasLoad = reading.highestReceiverLoad.has_value()
        && std::isfinite(*reading.highestReceiverLoad);
    const bool hasCpu = reading.systemCpuPercent.has_value()
        && std::isfinite(*reading.systemCpuPercent);
    if (!hasLoad && !hasCpu) {
        // No evidence either way: hold, and do not let a gap count toward
        // either hold time.
        m_busySinceMs.reset();
        m_calmSinceMs.reset();
        return std::nullopt;
    }

    const bool busy = (hasLoad && *reading.highestReceiverLoad >= kBusyReceiverLoad)
        || (hasCpu && *reading.systemCpuPercent >= kBusySystemCpuPercent);
    if (busy) {
        m_calmSinceMs.reset();
        if (!m_busySinceMs || reading.nowMs < *m_busySinceMs) {
            m_busySinceMs = reading.nowMs;
        }
        if (reading.nowMs - *m_busySinceMs < kBusyHoldMs) {
            return std::nullopt;
        }
        // The next step needs its own full hold, which also gives apps time
        // to act on this one.
        m_busySinceMs = reading.nowMs;
        return stepDown(reading.acceptedCharge);
    }
    m_busySinceMs.reset();

    const bool calm = (!hasLoad || *reading.highestReceiverLoad < kCalmReceiverLoad)
        && (!hasCpu || *reading.systemCpuPercent < kCalmSystemCpuPercent)
        && reading.lateBlocks == 0 && reading.highestInputDelayMs < kCalmInputDelayMs;
    if (!calm || m_previous.isEmpty()) {
        m_calmSinceMs.reset();
        return std::nullopt;
    }
    if (!m_calmSinceMs || reading.nowMs < *m_calmSinceMs) {
        m_calmSinceMs = reading.nowMs;
    }
    if (reading.nowMs - *m_calmSinceMs < kCalmHoldMs) {
        return std::nullopt;
    }
    m_calmSinceMs = reading.nowMs;
    return restoreStep();
}

std::optional<DisplayLoadDecision> DisplayLoadGovernor::stepDown(
    const DisplayBudgetCharge& accepted)
{
    if (m_previous.size() >= kMaximumSteps) {
        return std::nullopt;
    }
    const DisplayBudgetCharge minimum = floorCharge();
    const quint64 floorBytes = std::min(minimum.applicationBytesPerSecond,
                                        m_ceiling.applicationBytesPerSecond);
    const quint64 floorSamples = std::min(minimum.spectrumSampleUnitsPerSecond,
                                          m_ceiling.spectrumSampleUnitsPerSecond);
    // Nothing above the floor is being sent: lowering the budget would save
    // nothing now and only cap pans that open later.
    if (accepted.applicationBytesPerSecond <= floorBytes
        && accepted.spectrumSampleUnitsPerSecond <= floorSamples) {
        return std::nullopt;
    }
    const quint64 baseBytes = std::min(m_limits.applicationBytesPerSecond,
                                       accepted.applicationBytesPerSecond);
    const quint64 baseSamples = std::min(m_limits.spectrumSampleUnitsPerSecond,
                                         accepted.spectrumSampleUnitsPerSecond);
    const quint64 bytes = std::min(m_limits.applicationBytesPerSecond,
                                   std::max(floorBytes, scaled(baseBytes, kStepDownScale)));
    const quint64 samples = std::min(m_limits.spectrumSampleUnitsPerSecond,
                                     std::max(floorSamples, scaled(baseSamples, kStepDownScale)));
    if (bytes == m_limits.applicationBytesPerSecond
        && samples == m_limits.spectrumSampleUnitsPerSecond) {
        return std::nullopt; // Already at the floor.
    }
    const DisplayBudgetLimits next{bytes, samples, nextGeneration()};
    if (!next.isValid()) {
        return std::nullopt;
    }
    m_previous.append(m_limits);
    m_limits = next;
    return DisplayLoadDecision{m_limits, reason()};
}

std::optional<DisplayLoadDecision> DisplayLoadGovernor::restoreStep()
{
    if (m_previous.isEmpty()) {
        return std::nullopt;
    }
    const DisplayBudgetLimits previous = m_previous.takeLast();
    m_limits = DisplayBudgetLimits{previous.applicationBytesPerSecond,
                                   previous.spectrumSampleUnitsPerSecond, nextGeneration()};
    return DisplayLoadDecision{m_limits, reason()};
}

std::optional<DisplayLoadDecision> DisplayLoadGovernor::reset()
{
    m_busySinceMs.reset();
    m_calmSinceMs.reset();
    if (m_previous.isEmpty()) {
        return std::nullopt;
    }
    m_previous.clear();
    m_limits = DisplayBudgetLimits{m_ceiling.applicationBytesPerSecond,
                                   m_ceiling.spectrumSampleUnitsPerSecond, nextGeneration()};
    return DisplayLoadDecision{m_limits, reason()};
}

} // namespace NereusSDR
