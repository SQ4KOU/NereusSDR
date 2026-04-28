// =================================================================
// src/core/audio/CompositeTxMicRouter.cpp  (NereusSDR)
// =================================================================
//
// NereusSDR-original file. See CompositeTxMicRouter.h for design notes.
//
// Plan: 3M-1b F.3. Pre-code review §0.3 + master design §5.2.1.
// =================================================================

// no-port-check: NereusSDR-original file; no Thetis logic ported here.

#include "core/audio/CompositeTxMicRouter.h"
#include "core/audio/PcMicSource.h"
#include "core/audio/RadioMicSource.h"

namespace NereusSDR {

CompositeTxMicRouter::CompositeTxMicRouter(PcMicSource*    pcSource,
                                           RadioMicSource* radioSource,
                                           bool            hasMicJack)
    : m_pcSource(pcSource)
    , m_radioSource(radioSource)
    // Collapse: if hasMicJack is false OR radioSource is null, Radio is
    // permanently unavailable. Both conditions map to "no radio mic".
    , m_hasMicJack(hasMicJack && radioSource != nullptr)
{
    // Default to Pc on construction regardless of persisted preference.
    // The caller (RadioModel) will call setActiveSource() with the
    // user's persisted selection after construction.
    m_activeSource.store(MicSource::Pc, std::memory_order_release);
    m_pendingSource.store(MicSource::Pc, std::memory_order_release);
}

void CompositeTxMicRouter::setActiveSource(MicSource source)
{
    // HL2 force-PC: if hasMicJack is false (collapsed at construction),
    // silently drop Radio requests. They can never be applied.
    if (!m_hasMicJack && source == MicSource::Radio) {
        return;
    }

    // MOX-locked: defer source switch if TX is active to prevent audible
    // clicks and mid-TX buffer-state inconsistency.
    if (m_moxActive.load(std::memory_order_acquire)) {
        m_pendingSource.store(source, std::memory_order_release);
        m_hasPendingSwitch.store(true, std::memory_order_release);
        return;
    }

    // Apply immediately.
    m_activeSource.store(source, std::memory_order_release);
}

void CompositeTxMicRouter::setMoxActive(bool active)
{
    const bool wasActive = m_moxActive.exchange(active, std::memory_order_acq_rel);

    // MOX-off transition: apply any pending source switch.
    if (wasActive && !active) {
        if (m_hasPendingSwitch.exchange(false, std::memory_order_acq_rel)) {
            const MicSource pending = m_pendingSource.load(std::memory_order_acquire);
            m_activeSource.store(pending, std::memory_order_release);
        }
    }
}

int CompositeTxMicRouter::pullSamples(float* dst, int n)
{
    if (dst == nullptr || n <= 0) {
        return 0;
    }

    const MicSource source = m_activeSource.load(std::memory_order_acquire);

    if (source == MicSource::Radio && m_radioSource != nullptr) {
        return m_radioSource->pullSamples(dst, n);
    }

    if (m_pcSource != nullptr) {
        return m_pcSource->pullSamples(dst, n);
    }

    // No sources available — zero-fill (defensive; should not occur in
    // normal operation since pcSource is always provided).
    for (int i = 0; i < n; ++i) {
        dst[i] = 0.0f;
    }
    return n;
}

} // namespace NereusSDR
