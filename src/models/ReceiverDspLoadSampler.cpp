// =================================================================
// src/models/ReceiverDspLoadSampler.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original. Measurement bookkeeping for the R3 DSP
// overload work; no Thetis counterpart. See ReceiverDspLoadSampler.h.
//
// Modification history (NereusSDR):
//   2026-09-23 - Created by J.J. Boyd (KG4VCF), with AI-assisted
//                 implementation via Anthropic Claude Code (R-R3-40).
// =================================================================

#include "models/ReceiverDspLoadSampler.h"

#include <algorithm>

namespace NereusSDR {

ReceiverDspLoad ReceiverDspLoadSampler::compute(const Reading& now,
                                                const Baseline& previous)
{
    ReceiverDspLoad out;
    const qint64 blocks = now.blocks - previous.blocks;
    const qint64 currentBlockUs = now.currentBlockNs / 1000;
    const double periodUs = static_cast<double>(now.blockPeriodUs);

    out.lateBlocks = now.lateBlocks - previous.lateBlocks;
    out.maxBlockUs = std::max(now.intervalMaxBlockUs, currentBlockUs);
    out.lifetimeMaxBlockUs = std::max(now.lifetimeMaxBlockUs, currentBlockUs);
    out.inputDelayMs = now.inputDelayMs;
    out.droppedInputMs = now.droppedInputMs;

    if (blocks <= 0 && now.currentBlockNs <= 0) {
        out.idle = true;
        return out;
    }
    if (periodUs <= 0.0) {
        return out;
    }

    if (blocks > 0) {
        const double meanBlockUs =
            static_cast<double>(now.busyNs - previous.busyNs) / 1000.0 / blocks;
        out.load = meanBlockUs / periodUs;
    }
    // A block still running counts once it is already late, or when it is
    // the only work in the interval: its time so far is a floor on its
    // length, so a stuck worker reads as overloaded, never as unloaded.
    if (currentBlockUs > 0 && (blocks <= 0 || currentBlockUs > now.blockPeriodUs)) {
        out.load = std::max(out.load, currentBlockUs / periodUs);
    }
    return out;
}

void ReceiverDspLoadSampler::update(const QHash<int, Reading>& readings)
{
    QHash<int, Baseline> baselines;
    QHash<int, ReceiverDspLoad> snapshots;
    baselines.reserve(readings.size());
    snapshots.reserve(readings.size());

    for (auto it = readings.constBegin(); it != readings.constEnd(); ++it) {
        const Reading& now = it.value();
        Baseline previous = m_baselines.value(it.key());
        // The counters only grow for one WDSP channel id; a smaller value
        // means the baseline belongs to an earlier channel, so measure from
        // zero.
        if (now.blocks < previous.blocks || now.busyNs < previous.busyNs
            || now.lateBlocks < previous.lateBlocks) {
            previous = Baseline{};
        }
        snapshots.insert(it.key(), compute(now, previous));
        baselines.insert(it.key(), Baseline{now.blocks, now.busyNs, now.lateBlocks});
    }

    m_baselines = std::move(baselines);
    m_snapshots = std::move(snapshots);
}

std::optional<ReceiverDspLoad> ReceiverDspLoadSampler::snapshot(int sliceId) const
{
    const auto it = m_snapshots.constFind(sliceId);
    if (it == m_snapshots.constEnd()) {
        return std::nullopt;
    }
    return it.value();
}

void ReceiverDspLoadSampler::clear()
{
    m_baselines.clear();
    m_snapshots.clear();
}

} // namespace NereusSDR
