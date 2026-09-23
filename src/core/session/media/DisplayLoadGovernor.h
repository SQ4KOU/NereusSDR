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
/// in between hold. Every change carries the next limits generation; the
/// reason is CoreBusy while any step is in force.
///
/// Settling: after each step down nothing more is cut for kSettleMs, long
/// enough for the app to act on the new limits (its allocation
/// acknowledgement timeout) plus one load interval to measure the result.
/// Then the step is judged against the reading that caused it:
///  - relief of at least kReliefMargin, still busy: step again at once;
///  - relief of at least kReliefMargin, no longer busy: keep the step;
///  - less relief: the step saved nothing, so it is undone (the reason
///    clears when no other step remains) and the governor holds, cutting
///    nothing more, until the load falls to calm or rises kClearRiseMargin
///    above where it stood when the step was undone.
/// Loads are compared as a fraction of their busy threshold (pressure()),
/// so a receiver load and a CPU percentage share one scale.
///
/// No measurement at all (macOS, no receivers, every receiver idle) holds
/// and restarts both holds. Once such a gap has lasted kCalmHoldMs,
/// absence counts as calm for restoring only: a cut never outlives the
/// measurements that justified it, and absence never causes one.
///
/// Two-phase: update() only proposes a decision. The caller publishes it
/// and calls accept() once the publication is accepted; a proposal that is
/// not accepted changes nothing, and syncGeneration() lets the next one
/// follow whatever generation is published.
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
    /// The Core's load readings refresh this often
    /// (ReceiverDspLoadSampler::kSampleIntervalMs; DaemonApp checks they
    /// agree).
    static constexpr qint64 kLoadIntervalMs = 500;
    /// Time a step down gets before it is judged: the app's allocation
    /// acknowledgement timeout plus one load interval.
    static constexpr qint64 kSettleMs = kDisplayAllocationAckTimeoutMs + kLoadIntervalMs;
    /// Hardware-pending tuning value: the least drop in pressure, as a
    /// fraction of the busy threshold, that counts as relief from a step.
    static constexpr double kReliefMargin = 0.05;
    /// Hardware-pending tuning value: how far pressure must rise above its
    /// level at an undone step before the governor cuts again.
    static constexpr double kClearRiseMargin = 0.10;
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
    /// The reading's load as a fraction of its busy threshold: the larger of
    /// receiver load over kBusyReceiverLoad and CPU over
    /// kBusySystemCpuPercent. nullopt without a measurement.
    static std::optional<double> pressure(const DisplayLoadReading& reading);

    explicit DisplayLoadGovernor(DisplayBudgetLimits ceiling);

    /// A proposed decision when the limits should change, otherwise
    /// nullopt. Nothing it proposes is in force until accept().
    std::optional<DisplayLoadDecision> update(const DisplayLoadReading& reading);
    /// The proposal from the latest update() was published: put it in
    /// force. A decision that is not that proposal is ignored.
    void accept(const DisplayLoadDecision& decision);
    /// Publication refused because `generation` is already published:
    /// the next proposal follows it.
    void syncGeneration(quint32 generation);
    /// Back to the ceiling (the session ended), in force at once. A
    /// decision only when a step was in force.
    std::optional<DisplayLoadDecision> reset();

    DisplayBudgetLimits ceiling() const { return m_ceiling; }
    DisplayBudgetLimits limits() const { return m_state.limits; }
    DisplayBudgetReason reason() const { return reasonFor(m_state); }
    int steps() const { return static_cast<int>(m_state.previous.size()); }
    /// A step is waiting to be judged.
    bool settling() const { return m_state.settle.has_value(); }
    /// An undone step is holding further cuts.
    bool holding() const { return m_state.heldPressure.has_value(); }

private:
    struct Settle {
        qint64 untilMs = 0;
        double pressureAtStep = 0.0;
    };
    /// Everything a published decision changes.
    struct State {
        DisplayBudgetLimits limits;
        QList<DisplayBudgetLimits> previous;
        std::optional<Settle> settle;
        std::optional<double> heldPressure;
    };
    struct Proposal {
        DisplayLoadDecision decision;
        State next;
    };

    static DisplayBudgetReason reasonFor(const State& state)
    {
        return state.previous.isEmpty() ? DisplayBudgetReason::None
                                        : DisplayBudgetReason::CoreBusy;
    }
    std::optional<DisplayLoadDecision> propose(State next);
    std::optional<DisplayLoadDecision> stepDown(State next, const DisplayLoadReading& reading,
                                                double pressureNow);
    std::optional<DisplayLoadDecision> restoreStep(State next);
    std::optional<DisplayLoadDecision> calmReading(State next, qint64 nowMs, qint64 calmStartMs);
    static quint32 nextGeneration(const DisplayBudgetLimits& limits);

    DisplayBudgetLimits m_ceiling;
    State m_state;
    std::optional<Proposal> m_proposal;
    std::optional<qint64> m_busySinceMs;
    std::optional<qint64> m_calmSinceMs;
    std::optional<qint64> m_gapSinceMs;
};

} // namespace NereusSDR
