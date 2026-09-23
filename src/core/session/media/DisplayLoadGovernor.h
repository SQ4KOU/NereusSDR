// =================================================================
// src/core/session/media/DisplayLoadGovernor.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. When the Core computer runs short of
// processing time, lower the remote display budget before receive
// processing has to give way (R-R3-08, R-R3-37, R-R3-40). No upstream logic
// is involved; the thresholds are first estimates to revisit with
// measurements on the Core hardware.
//
// =================================================================

#pragma once

#include "core/session/media/DisplayBudget.h"

#include <QList>

#include <optional>

namespace NereusSDR {

/// One observation of the Core's load, gathered by the caller from caches
/// that were already sampled (RadioModel::receiverDspLoad and the shared
/// host sampler). Nothing here is read under a DSP lock.
struct DisplayLoadReading {
    qint64 nowMs = 0;
    /// Highest load among receivers that processed input in their latest
    /// interval (a block's time over its period). Idle receivers are left
    /// out: idle is not proof of no load. nullopt: no receiver measured.
    std::optional<double> highestReceiverLoad;
    /// Late blocks summed over the measured receivers' latest intervals.
    qint64 lateBlocks = 0;
    /// Longest input wait among the measured receivers' latest batches.
    qint64 highestInputDelayMs = 0;
    /// The Core computer's CPU use since the previous host sample, 0..100.
    /// nullopt where the host cannot be measured (macOS, Windows).
    std::optional<double> systemCpuPercent;
    /// Display traffic the Core has accepted now: every spectrum endpoint
    /// plus the PureSignal display when it is subscribed.
    DisplayBudgetCharge acceptedCharge;
};

struct DisplayLoadDecision {
    DisplayBudgetLimits limits;
    DisplayBudgetReason reason = DisplayBudgetReason::None;
};

/// Turns load readings into display-budget limits with hysteresis.
///
/// Busy (highest receiver load >= kBusyReceiverLoad or system CPU >=
/// kBusySystemCpuPercent) held for kBusyHoldMs steps the limits down to
/// kStepDownScale of what is accepted now, never below floorCharge(). Calm
/// (every measurement under the calm thresholds, no late block, input wait
/// under kCalmInputDelayMs) held for kCalmHoldMs undoes one step. Readings
/// in between hold. A reading without any measurement changes nothing and
/// restarts both holds. Every change carries the next limits generation;
/// the reason is CoreBusy while any step is in force.
///
/// The busy threshold sits below the NNR step-back's 0.90 (held 2 s), so
/// spectrum yields first.
class DisplayLoadGovernor {
public:
    static constexpr double kBusyReceiverLoad = 0.75;
    static constexpr double kBusySystemCpuPercent = 85.0;
    static constexpr qint64 kBusyHoldMs = 2'000;
    static constexpr double kCalmReceiverLoad = 0.60;
    static constexpr double kCalmSystemCpuPercent = 70.0;
    static constexpr qint64 kCalmInputDelayMs = 100;
    static constexpr qint64 kCalmHoldMs = 10'000;
    static constexpr double kStepDownScale = 0.5;
    static constexpr int kMaximumSteps = 32;
    /// The floor keeps one active pan useful: the app's own reduction floors
    /// (RemoteDisplayAllocator.cpp kUsefulPixels, kUsefulFps).
    static constexpr int kFloorPixels = 256;
    static constexpr int kFloorFps = 10;
    /// The app plans at most eight remote pans (RemoteDisplayAllocator.cpp
    /// kMaximumPans).
    static constexpr int kCeilingPans = 8;

    /// PureSignal's display plus one pan at kFloorPixels and kFloorFps with
    /// its wide plane.
    static DisplayBudgetCharge floorCharge();
    /// Eight pans at the codec's largest plane, highest frame rate and a
    /// wide plane, plus PureSignal's display, at generation 1. What the Core
    /// advertises when adaptation is on and no limits are configured, so
    /// apps plan in budget mode from the start.
    static DisplayBudgetLimits computedCeiling();

    explicit DisplayLoadGovernor(DisplayBudgetLimits ceiling);

    /// A new decision when the limits change, otherwise nullopt.
    std::optional<DisplayLoadDecision> update(const DisplayLoadReading& reading);
    /// Back to the ceiling (the session ended). A decision only when a step
    /// was in force.
    std::optional<DisplayLoadDecision> reset();

    DisplayBudgetLimits ceiling() const { return m_ceiling; }
    DisplayBudgetLimits limits() const { return m_limits; }
    DisplayBudgetReason reason() const
    {
        return m_previous.isEmpty() ? DisplayBudgetReason::None
                                    : DisplayBudgetReason::CoreBusy;
    }
    int steps() const { return static_cast<int>(m_previous.size()); }

private:
    std::optional<DisplayLoadDecision> stepDown(const DisplayBudgetCharge& accepted);
    std::optional<DisplayLoadDecision> restoreStep();
    quint32 nextGeneration() const;

    DisplayBudgetLimits m_ceiling;
    DisplayBudgetLimits m_limits;
    QList<DisplayBudgetLimits> m_previous;
    std::optional<qint64> m_busySinceMs;
    std::optional<qint64> m_calmSinceMs;
};

} // namespace NereusSDR
