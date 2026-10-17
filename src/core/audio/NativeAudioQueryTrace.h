// =================================================================
// src/core/audio/NativeAudioQueryTrace.h  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original diagnostic query markers.
// Modification history (NereusSDR):
//   2026-10-12: J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
//               Trace native enumeration queries without logging device data.
//   2026-10-16: tracedNativeAudioQuery() marks the process's first native
//               audio call (NativeAudioFirstCall).  J.J. Boyd (KG4VCF),
//               AI-assisted via Anthropic Claude Code.
// =================================================================
#pragma once

#include "core/LogCategories.h"
#include "core/audio/NativeAudioFirstCall.h"

#include <QCoreApplication>
#include <QDebug>

#include <atomic>
#include <chrono>
#include <utility>

namespace NereusSDR {

// Callers supply constant stage names and enumeration ordinals only. Never
// pass device names, UIDs, object addresses or query results into this trace.
class NativeAudioQueryTrace final {
public:
    explicit NativeAudioQueryTrace(const char* stage, int ordinal = -1)
        : m_stage(stage), m_ordinal(ordinal), m_enabled(lcAudio().isDebugEnabled())
    {
        if (m_enabled) {
            m_sequence = s_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
            m_parent = s_currentSequence;
            s_currentSequence = m_sequence;
            m_started = std::chrono::steady_clock::now();
            qCDebug(lcAudio).nospace() << "native-audio-query begin stage=" << m_stage
                                     << " ordinal=" << m_ordinal
                                     << " pid=" << QCoreApplication::applicationPid()
                                     << " query=" << m_sequence
                                     << " parent=" << m_parent;
        }
    }

    ~NativeAudioQueryTrace() { finish(); }
    NativeAudioQueryTrace(const NativeAudioQueryTrace&) = delete;
    NativeAudioQueryTrace& operator=(const NativeAudioQueryTrace&) = delete;

    // End acquisition markers immediately after the original lock acquires;
    // the lock's lifetime and scope remain entirely with its caller.
    void finish()
    {
        if (m_enabled && !m_finished) {
            m_finished = true;
            s_currentSequence = m_parent;
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - m_started).count();
            qCDebug(lcAudio).nospace() << "native-audio-query end stage=" << m_stage
                                     << " ordinal=" << m_ordinal
                                     << " pid=" << QCoreApplication::applicationPid()
                                     << " query=" << m_sequence
                                     << " parent=" << m_parent
                                     << " elapsed-us=" << elapsed;
        }
    }

private:
    inline static std::atomic<quint64> s_sequence{0};
    inline static thread_local quint64 s_currentSequence = 0;
    const char* m_stage;
    int m_ordinal;
    bool m_enabled;
    bool m_finished = false;
    quint64 m_sequence = 0;
    quint64 m_parent = 0;
    std::chrono::steady_clock::time_point m_started;
};

template<typename Callable>
decltype(auto) tracedNativeAudioQuery(const char* stage, int ordinal, Callable&& callable)
{
    markNativeAudioCall(stage);
    NativeAudioQueryTrace trace(stage, ordinal);
    return std::forward<Callable>(callable)();
}

} // namespace NereusSDR
