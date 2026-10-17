// SPDX-License-Identifier: GPL-3.0-or-later
// Modification history (NereusSDR):
// 2026-10-11  J.J. Boyd / KG4VCF; original waveform observation. AI tooling: OpenAI Codex.
// no-port-check: NereusSDR-original observer; no codec/DSP processing.
#include "WaveformPublisher.h"
#include <algorithm>
#include <chrono>
#include <type_traits>
namespace NereusSDR {
qint64 waveformNowNs() noexcept
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
WaveformPublisher::WaveformPublisher(WaveformKind kind, int rate, QObject* parent)
    : QObject(parent), m_kind(kind), m_rate(0), m_capacity(0)
{
    const int nativeRate = kind == WaveformKind::RawMicrophone           ? 48000
                           : kind == WaveformKind::ConditionedMicrophone ? 16000
                                                                         : 24000;
    const bool knownKind = kind == WaveformKind::RawMicrophone ||
                           kind == WaveformKind::ConditionedMicrophone || kind == WaveformKind::DecodedSpeech;
    if (knownKind && rate == nativeRate) {
        m_rate = rate;
        m_capacity = rate * 5;
        m_batchCapacity = rate / 10;
    }
    m_pending.resize(m_capacity);
    qRegisterMetaType<WaveformBatchPtr>();
    for (auto& slot : m_pool) {
        slot = std::make_shared<WaveformBatch>();
        slot->samples.reserve(m_batchCapacity);
        slot->segments.reserve(kSegmentCapacity);
    }
}
void WaveformPublisher::discardPrefix(int frames) noexcept
{
    frames = std::min(frames, m_count);
    m_head = (m_head + frames) % m_capacity;
    m_count -= frames;
    m_dropped += frames;
    while (frames > 0 && m_segmentCount > 0) {
        WaveformSegment& segment = m_pendingSegments[m_segmentHead];
        const int skip = std::min(frames, segment.count);
        segment.count -= skip;
        segment.firstSequence += skip;
        segment.startNs += qint64(skip) * 1000000000LL / m_rate;
        frames -= skip;
        if (segment.count == 0) {
            m_segmentHead = (m_segmentHead + 1) % kSegmentCapacity;
            --m_segmentCount;
        }
    }
}
template <class T>
bool WaveformPublisher::append(quint64 token, const T* pcm, int frames, int stride, qint64 atNs,
                               bool valid) noexcept
{
    if (m_rate == 0 || token == 0 || token != bindingToken() || frames <= 0 || frames > m_capacity ||
        stride <= 0 || stride > 16 || !pcm) {
        return false;
    }
    if (token != m_producerToken) {
        m_producerToken = token;
        m_head = 0;
        m_count = 0;
        m_segmentHead = 0;
        m_segmentCount = 0;
        m_sequence = 0;
        m_dropped = 0;
    }
    const quint64 first = m_sequence;
    m_sequence += frames;
    if (!valid) {
        m_dropped += frames;
        return false;
    }
    if (m_count + frames > m_capacity) {
        discardPrefix(m_count + frames - m_capacity);
    }
    if (m_segmentCount == kSegmentCapacity) {
        discardPrefix(m_pendingSegments[m_segmentHead].count);
    }
    WaveformSegment& segment = m_pendingSegments[(m_segmentHead + m_segmentCount++) % kSegmentCapacity];
    segment = {0, frames, first, atNs - qint64(frames) * 1000000000LL / m_rate};
    for (int i = 0; i < frames; ++i) {
        const int index = (m_head + m_count++) % m_capacity;
        if constexpr (std::is_same_v<T, int16_t>) {
            m_pending[index] = float(pcm[i * stride]) / 32768.0f;
        } else {
            m_pending[index] = static_cast<float>(pcm[i * stride]);
        }
    }
    if (token != bindingToken()) {
        m_count = 0;
        m_segmentCount = 0;
        return false;
    }
    publish(atNs);
    return true;
}
void WaveformPublisher::publish(qint64 atNs) noexcept
{
    if (m_inFlight != 0) {
        bool released = m_ack.load(std::memory_order_acquire) == m_inFlight;
        if (!released) {
            for (const auto& slot : m_pool) {
                if (slot->serial == m_inFlight && slot.use_count() == 1) {
                    released = true;
                    break;
                }
            }
        }
        if (!released) {
            return;
        }
        m_inFlight = 0;
    }
    if (m_count == 0 || (m_lastPublishNs != 0 && atNs - m_lastPublishNs < 33000000LL)) {
        return;
    }
    for (auto& slot : m_pool) {
        // Ack releases the event gate, never a retained immutable reader lease.
        if (slot.use_count() != 1) {
            continue;
        }
        // GUI recovery cannot make this producer copy a five-second backlog.
        if (m_count > m_batchCapacity) {
            discardPrefix(m_count - m_batchCapacity);
        }
        slot->samples.clear();
        slot->segments.clear();
        slot->kind = m_kind;
        slot->sampleRate = m_rate;
        slot->token = m_producerToken;
        slot->serial = ++m_serial;
        slot->droppedFrames = m_dropped;
        for (int i = 0; i < m_count; ++i) {
            slot->samples.push_back(m_pending[(m_head + i) % m_capacity]);
        }
        int offset = 0;
        for (int i = 0; i < m_segmentCount; ++i) {
            WaveformSegment segment = m_pendingSegments[(m_segmentHead + i) % kSegmentCapacity];
            segment.offset = offset;
            offset += segment.count;
            slot->segments.push_back(segment);
        }
        m_head = 0;
        m_count = 0;
        m_segmentHead = 0;
        m_segmentCount = 0;
        m_dropped = 0;
        if (slot->token != bindingToken()) {
            return;
        }
        m_inFlight = slot->serial;
        m_lastPublishNs = atNs;
        emit batchReady(WaveformBatchPtr(slot));
        return;
    }
}
bool WaveformPublisher::appendFloat(quint64 t, const float* p, int n, int s, qint64 ns, bool v) noexcept
{
    return append(t, p, n, s, ns, v);
}
bool WaveformPublisher::appendDouble(quint64 t, const double* p, int n, int s, qint64 ns, bool v) noexcept
{
    return append(t, p, n, s, ns, v);
}
bool WaveformPublisher::appendInt16(quint64 t, const int16_t* p, int n, int s, qint64 ns, bool v) noexcept
{
    return append(t, p, n, s, ns, v);
}
} // namespace NereusSDR
