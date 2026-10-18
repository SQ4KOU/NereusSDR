// =================================================================
// src/core/daemon/DisplayLoadInputs.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original. See DisplayLoadInputs.h.
// =================================================================

#include "core/daemon/DisplayLoadInputs.h"

#include <algorithm>
#include <cmath>

namespace NereusSDR {

bool receiverSharesDisplayCores(const PlacementPlan& placement, int sliceId)
{
    if (!placement.active) {
        return true;
    }
    const auto slowedByDisplay = [&placement](ThreadRole role, int channel) {
        const QList<int> cores = placement.coresFor(role, channel);
        if (cores.isEmpty()) {
            return true; // on the housekeeping cores
        }
        // A spectrum thread on a raised thread's core takes only the time
        // that thread leaves.
        if (placement.raisedFor(role, channel)) {
            return false;
        }
        return std::any_of(cores.cbegin(), cores.cend(),
                           [&placement](int cpu) { return placement.carriesSpectrum(cpu); });
    };
    return slowedByDisplay(ThreadRole::RxWorker, sliceId)
        || slowedByDisplay(ThreadRole::DspThread, -1);
}

DisplayGovernorCpus displayGovernorCpus(const PlacementPlan& placement)
{
    if (!placement.active) {
        return {};
    }
    DisplayGovernorCpus cpus;
    cpus.housekeeping = placement.housekeeping;
    std::sort(cpus.housekeeping.begin(), cpus.housekeeping.end());
    for (const RoleAssignment& a : placement.assignments) {
        if (a.role != ThreadRole::SpectrumThread) {
            continue;
        }
        for (int cpu : placement.coresFor(ThreadRole::SpectrumThread, a.channel)) {
            if (!cpus.housekeeping.contains(cpu)) {
                cpus.spectrumOnly.append(cpu);
            }
        }
    }
    std::sort(cpus.spectrumOnly.begin(), cpus.spectrumOnly.end());
    cpus.spectrumOnly.erase(std::unique(cpus.spectrumOnly.begin(), cpus.spectrumOnly.end()),
                            cpus.spectrumOnly.end());
    return cpus;
}

DisplayLoadReading displayLoadReadingFrom(const DisplayLoadInputs& inputs, qint64 nowMs,
                                          const DisplayBudgetCharge& acceptedCharge)
{
    DisplayLoadReading reading;
    reading.nowMs = nowMs;
    reading.acceptedCharge = acceptedCharge;
    for (const DisplayLoadInputs::Receiver& receiver : inputs.receivers) {
        // Idle is not proof of no load, so it is not a measurement either.
        if (receiver.load.idle || !std::isfinite(receiver.load.load)
            || !receiverSharesDisplayCores(inputs.placement, receiver.sliceId)) {
            continue;
        }
        reading.highestReceiverLoad = std::max(reading.highestReceiverLoad.value_or(0.0),
                                               receiver.load.load);
        reading.lateBlocks += std::max<qint64>(0, receiver.load.lateBlocks);
        reading.highestInputDelayMs = std::max(reading.highestInputDelayMs,
                                               receiver.load.inputDelayMs);
    }
    reading.systemCpuPercent = inputs.placement.active ? inputs.housekeepingCpuPercent
                                                       : inputs.systemCpuPercent;
    if (reading.systemCpuPercent && inputs.cpuSampleBeganMsAgo) {
        reading.systemCpuSampleStartMs = nowMs - std::max<qint64>(0, *inputs.cpuSampleBeganMsAgo);
    }
    return reading;
}

} // namespace NereusSDR
