// SPDX-License-Identifier: GPL-3.0-or-later
// Modification history (NereusSDR):
// 2026-10-11  J.J. Boyd / KG4VCF; original waveform observation. AI tooling: OpenAI Codex.
// no-port-check: NereusSDR-original full-rate main-thread history.
#include "WaveformHistory.h"
#include <algorithm>
namespace NereusSDR {
void WaveformHistory::append(const WaveformBatch& batch)
{
    if (batch.token == 0 || batch.sampleRate <= 0) {
        return;
    }
    if (m_data.token != batch.token || m_data.sampleRate != batch.sampleRate) {
        clear();
    }
    m_data.kind = batch.kind;
    m_data.token = batch.token;
    m_data.sampleRate = batch.sampleRate;
    m_data.droppedFrames += batch.droppedFrames;
    const int base = m_data.samples.size();
    for (float sample : batch.samples) {
        m_data.samples.append(sample);
    }
    for (WaveformSegment segment : batch.segments) {
        if (segment.offset < 0 || segment.count <= 0 ||
            size_t(segment.offset + segment.count) > batch.samples.size()) {
            continue;
        }
        segment.offset += base;
        bool merged = false;
        if (!m_data.segments.isEmpty()) {
            WaveformSegment& previous = m_data.segments.last();
            const qint64 expectedStart =
                previous.startNs + qint64(previous.count) * 1000000000LL / batch.sampleRate;
            if (previous.offset + previous.count == segment.offset &&
                previous.firstSequence + quint64(previous.count) == segment.firstSequence &&
                std::abs(expectedStart - segment.startNs) <= 1) {
                previous.count += segment.count;
                merged = true;
            }
        }
        if (!merged) {
            m_data.segments.append(segment);
        }
    }
    m_data.availability =
        m_data.samples.isEmpty() ? WaveformAvailability::Unavailable : WaveformAvailability::Available;
    ++m_data.revision;
    m_cached.reset();
    if (!m_data.segments.isEmpty()) {
        const WaveformSegment& last = m_data.segments.last();
        evict(last.startNs + qint64(last.count - 1) * 1000000000LL / m_data.sampleRate - 5000000000LL);
    }
    // Also bound memory when observation timestamps overlap (decoder bursts).
    const int excess = m_data.samples.size() - m_data.sampleRate * 5;
    if (excess > 0) {
        m_data.samples.remove(0, excess);
        QVector<WaveformSegment> kept;
        for (WaveformSegment s : m_data.segments) {
            const int remove = std::clamp(excess - s.offset, 0, s.count);
            s.offset = std::max(0, s.offset - excess);
            s.count -= remove;
            s.firstSequence += remove;
            s.startNs += qint64(remove) * 1000000000LL / m_data.sampleRate;
            if (s.count > 0) {
                kept.append(s);
            }
        }
        m_data.segments = std::move(kept);
    }
}
void WaveformHistory::evict(qint64 beforeNs)
{
    if (m_data.sampleRate <= 0 || m_data.samples.isEmpty()) {
        return;
    }
    QVector<float> samples;
    QVector<WaveformSegment> segments;
    bool changed = false;
    for (WaveformSegment segment : m_data.segments) {
        int skip = 0;
        if (segment.startNs < beforeNs) {
            const qint64 delta = beforeNs - segment.startNs;
            const qint64 spanNs =
                (qint64(segment.count) * 1000000000LL + m_data.sampleRate - 1) / m_data.sampleRate;
            skip = delta >= spanNs ? segment.count
                                   : int((delta * m_data.sampleRate + 999999999LL) / 1000000000LL);
        }
        changed = changed || skip > 0;
        if (skip == segment.count) {
            continue;
        }
        const int base = samples.size();
        for (int i = skip; i < segment.count; ++i) {
            samples.append(m_data.samples[segment.offset + i]);
        }
        segment.offset = base;
        segment.count -= skip;
        segment.firstSequence += skip;
        segment.startNs += qint64(skip) * 1000000000LL / m_data.sampleRate;
        segments.append(segment);
    }
    if (changed) {
        m_data.samples = std::move(samples);
        m_data.segments = std::move(segments);
        ++m_data.revision;
        m_cached.reset();
    }
}
void WaveformHistory::clear()
{
    m_data = {};
    m_cached.reset();
}
WaveformSnapshotPtr WaveformHistory::snapshot(qint64 now)
{
    evict(now - 5000000000LL);
    if (m_cached && m_cached->asOfNs == now) {
        return m_cached;
    }
    m_data.asOfNs = now;
    m_data.availability =
        m_data.samples.isEmpty() ? WaveformAvailability::Unavailable : WaveformAvailability::Available;
    m_cached = std::make_shared<const WaveformSnapshot>(m_data);
    return m_cached;
}
} // namespace NereusSDR
