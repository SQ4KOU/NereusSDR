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
#include <utility>

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

std::optional<double> DisplayLoadGovernor::pressure(const DisplayLoadReading& reading)
{
    std::optional<double> result;
    if (reading.highestReceiverLoad && std::isfinite(*reading.highestReceiverLoad)) {
        result = *reading.highestReceiverLoad / kBusyReceiverLoad;
    }
    if (reading.systemCpuPercent && std::isfinite(*reading.systemCpuPercent)) {
        result = std::max(result.value_or(0.0), *reading.systemCpuPercent / kBusySystemCpuPercent);
    }
    return result;
}

DisplayLoadGovernor::DisplayLoadGovernor(DisplayBudgetLimits ceiling)
    : m_ceiling(ceiling)
{
    m_state.limits = ceiling;
}

quint32 DisplayLoadGovernor::nextGeneration(const DisplayBudgetLimits& limits)
{
    const quint32 next = limits.generation + 1;
    return next == 0 ? 1 : next;
}

std::optional<DisplayLoadDecision> DisplayLoadGovernor::update(const DisplayLoadReading& reading)
{
    m_proposal.reset();
    const qint64 nowMs = reading.nowMs;
    State next = m_state;
    const std::optional<double> pressureNow = pressure(reading);
    if (!pressureNow) {
        // No evidence of load: never a reason to cut, and a gap does not
        // count toward the busy hold.
        m_busySinceMs.reset();
        if (!m_gapSinceMs || nowMs < *m_gapSinceMs) {
            m_gapSinceMs = nowMs;
            m_calmSinceMs.reset();
        }
        if (nowMs - *m_gapSinceMs < kCalmHoldMs) {
            return std::nullopt;
        }
        // A sustained gap counts as calm for restoring only, so a cut
        // cannot outlive the measurements that justified it.
        if (!m_calmSinceMs) {
            m_calmSinceMs = *m_gapSinceMs;
        }
        // A step that settled with nothing to judge it by is simply over.
        if (next.settle && nowMs >= next.settle->untilMs) {
            next.settle.reset();
        }
        return calmReading(std::move(next), nowMs, *m_calmSinceMs);
    }
    m_gapSinceMs.reset();

    const bool hasLoad = reading.highestReceiverLoad.has_value()
        && std::isfinite(*reading.highestReceiverLoad);
    const bool hasCpu = reading.systemCpuPercent.has_value()
        && std::isfinite(*reading.systemCpuPercent);
    const bool busy = (hasLoad && *reading.highestReceiverLoad >= kBusyReceiverLoad)
        || (hasCpu && *reading.systemCpuPercent >= kBusySystemCpuPercent);
    const bool calm = (!hasLoad || *reading.highestReceiverLoad < kCalmReceiverLoad)
        && (!hasCpu || *reading.systemCpuPercent < kCalmSystemCpuPercent)
        && reading.lateBlocks == 0 && reading.highestInputDelayMs < kCalmInputDelayMs;

    if (next.settle) {
        if (nowMs < next.settle->untilMs) {
            // The app is still acting on the last step: judge nothing yet.
            m_busySinceMs.reset();
            m_calmSinceMs.reset();
            return std::nullopt;
        }
        const Settle judged = *next.settle;
        next.settle.reset();
        m_busySinceMs.reset();
        m_calmSinceMs.reset();
        if (judged.pressureAtStep - *pressureNow < kReliefMargin && !next.previous.isEmpty()) {
            // The step saved nothing: give the quality back and cut no more
            // until the load changes clearly.
            next.heldPressure = *pressureNow;
            return restoreStep(std::move(next));
        }
        if (busy) {
            m_busySinceMs = nowMs;
            return stepDown(std::move(next), reading, *pressureNow);
        }
    }

    if (next.heldPressure) {
        if (calm) {
            next.heldPressure.reset();
        } else if (*pressureNow >= *next.heldPressure + kClearRiseMargin) {
            // A clearly heavier load: cutting may help again, after a full
            // busy hold of its own.
            next.heldPressure.reset();
            m_busySinceMs.reset();
        }
    }

    if (busy) {
        m_calmSinceMs.reset();
        if (next.heldPressure) {
            m_busySinceMs.reset();
            m_state = std::move(next);
            return std::nullopt;
        }
        if (!m_busySinceMs || nowMs < *m_busySinceMs) {
            m_busySinceMs = nowMs;
        }
        if (nowMs - *m_busySinceMs < kBusyHoldMs) {
            m_state = std::move(next);
            return std::nullopt;
        }
        m_busySinceMs = nowMs;
        return stepDown(std::move(next), reading, *pressureNow);
    }
    m_busySinceMs.reset();

    if (!calm) {
        m_calmSinceMs.reset();
        m_state = std::move(next);
        return std::nullopt;
    }
    if (!m_calmSinceMs || nowMs < *m_calmSinceMs) {
        m_calmSinceMs = nowMs;
    }
    return calmReading(std::move(next), nowMs, *m_calmSinceMs);
}

std::optional<DisplayLoadDecision> DisplayLoadGovernor::calmReading(State next, qint64 nowMs,
                                                                    qint64 calmStartMs)
{
    if (next.previous.isEmpty() || next.settle || nowMs - calmStartMs < kCalmHoldMs) {
        if (next.previous.isEmpty()) {
            m_calmSinceMs.reset();
        }
        m_state = std::move(next);
        return std::nullopt;
    }
    m_calmSinceMs = nowMs;
    return restoreStep(std::move(next));
}

std::optional<DisplayLoadDecision> DisplayLoadGovernor::propose(State next)
{
    const DisplayLoadDecision decision{next.limits, reasonFor(next)};
    m_proposal = Proposal{decision, std::move(next)};
    return decision;
}

void DisplayLoadGovernor::accept(const DisplayLoadDecision& decision)
{
    if (!m_proposal || m_proposal->decision.limits != decision.limits
        || m_proposal->decision.reason != decision.reason) {
        return;
    }
    m_state = std::move(m_proposal->next);
    m_proposal.reset();
}

void DisplayLoadGovernor::syncGeneration(quint32 generation)
{
    m_proposal.reset();
    const quint32 delta = generation - m_state.limits.generation;
    if (delta != 0 && delta < 0x80000000u) {
        m_state.limits.generation = generation;
    }
}

std::optional<DisplayLoadDecision> DisplayLoadGovernor::stepDown(
    State next, const DisplayLoadReading& reading, double pressureNow)
{
    const DisplayBudgetCharge& accepted = reading.acceptedCharge;
    const DisplayBudgetLimits current = next.limits;
    const DisplayBudgetCharge minimum = floorCharge();
    const quint64 floorBytes = std::min(minimum.applicationBytesPerSecond,
                                        m_ceiling.applicationBytesPerSecond);
    const quint64 floorSamples = std::min(minimum.spectrumSampleUnitsPerSecond,
                                          m_ceiling.spectrumSampleUnitsPerSecond);
    const quint64 baseBytes = std::min(current.applicationBytesPerSecond,
                                       accepted.applicationBytesPerSecond);
    const quint64 baseSamples = std::min(current.spectrumSampleUnitsPerSecond,
                                         accepted.spectrumSampleUnitsPerSecond);
    const quint64 bytes = std::min(current.applicationBytesPerSecond,
                                   std::max(floorBytes, scaled(baseBytes, kStepDownScale)));
    const quint64 samples = std::min(current.spectrumSampleUnitsPerSecond,
                                     std::max(floorSamples, scaled(baseSamples, kStepDownScale)));
    const DisplayBudgetLimits lowered{bytes, samples, nextGeneration(current)};
    // Nothing above the floor is being sent (lowering the budget would save
    // nothing now and only cap pans that open later), the limits are
    // already at the floor, or the step count is spent.
    const bool nothingToCut = accepted.applicationBytesPerSecond <= floorBytes
        && accepted.spectrumSampleUnitsPerSecond <= floorSamples;
    const bool atFloor = bytes == current.applicationBytesPerSecond
        && samples == current.spectrumSampleUnitsPerSecond;
    if (nothingToCut || atFloor || next.previous.size() >= kMaximumSteps || !lowered.isValid()) {
        m_state = std::move(next);
        return std::nullopt;
    }
    next.previous.append(current);
    next.limits = lowered;
    next.settle = Settle{reading.nowMs + kSettleMs, pressureNow};
    return propose(std::move(next));
}

std::optional<DisplayLoadDecision> DisplayLoadGovernor::restoreStep(State next)
{
    if (next.previous.isEmpty()) {
        m_state = std::move(next);
        return std::nullopt;
    }
    const DisplayBudgetLimits previous = next.previous.takeLast();
    next.limits = DisplayBudgetLimits{previous.applicationBytesPerSecond,
                                      previous.spectrumSampleUnitsPerSecond,
                                      nextGeneration(next.limits)};
    return propose(std::move(next));
}

std::optional<DisplayLoadDecision> DisplayLoadGovernor::reset()
{
    m_proposal.reset();
    m_busySinceMs.reset();
    m_calmSinceMs.reset();
    m_gapSinceMs.reset();
    m_state.settle.reset();
    m_state.heldPressure.reset();
    if (m_state.previous.isEmpty()) {
        return std::nullopt;
    }
    m_state.previous.clear();
    m_state.limits = DisplayBudgetLimits{m_ceiling.applicationBytesPerSecond,
                                         m_ceiling.spectrumSampleUnitsPerSecond,
                                         nextGeneration(m_state.limits)};
    return DisplayLoadDecision{m_state.limits, reason()};
}

} // namespace NereusSDR
