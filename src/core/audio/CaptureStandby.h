// =================================================================
// src/core/audio/CaptureStandby.h  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  The application's standing capture
// helper on macOS; no Thetis logic.
//
// On macOS a bundled app's first Core Audio call reads its stored Mic Mode,
// and when that mode is Voice Isolation the system then re-applies it for
// 40 to 100 s, during which device lists and microphone opens wait on it,
// in every audio process.  The helper has no bundle id, so it never reads
// the Mic Mode; its warm-up first keeps the window's stored Mic Mode at
// Standard (Options::micModeBundle), so the window's own first call finds
// nothing to re-apply.
// Design: docs/architecture/2026-09-22-optional-microphone-capture-design.md,
// addendum of 2026-10-16.
//
// Modification history (NereusSDR):
//   2026-10-16: the persistent capture helper, held for the whole
//               application.  J.J. Boyd (KG4VCF), AI-assisted via
//               Anthropic Claude Code.
//   2026-10-16: work is held at most 1 s (was 3 s), and the warm-up keeps
//               the window's Mic Mode at Standard.  J.J. Boyd (KG4VCF),
//               AI-assisted via Anthropic Claude Code.
// =================================================================

#pragma once

#include "core/NereusCoreExport.h"
#include "core/audio/CaptureSupervisor.h"

#include <QObject>
#include <QPointer>

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

class QTimer;

namespace NereusSDR {

// Owns one CaptureSupervisor for the whole application and holds its
// Standby demand from construction, so the helper starts, says hello and
// lists the audio devices (Warm) before anything else in the application
// becomes a Core Audio client.  Every window's AudioEngine shares the
// supervisor (AudioEngine::setSharedCaptureSupervisor), so the helper
// outlives a window and serves the next one after a Core switch.
//
// whenWarm() holds work, the start of the first window, until the helper
// has warmed or warmWaitMs have passed, whichever comes first, and logs how
// long it held it.  It never waits in the call: the work runs later from
// the event loop.  The bound
// only stops the holding; the helper keeps its own deadlines.  When no
// helper can be started (none installed) nothing is held.
//
// Owner thread only (the GUI thread).
class NEREUS_CORE_EXPORT CaptureStandby final : public QObject {
    Q_OBJECT

public:
    // How long whenWarm() holds work for a helper that has not warmed: the
    // first window waits no more than this for it.
    static constexpr int kWarmWaitMs = 1000;

    explicit CaptureStandby(CaptureSupervisor::Options options = {},
                            int warmWaitMs = kWarmWaitMs, QObject* parent = nullptr);
    ~CaptureStandby() override;            // the supervisor's bounded shutdown()

    CaptureStandby(const CaptureStandby&) = delete;
    CaptureStandby& operator=(const CaptureStandby&) = delete;

    CaptureSupervisor* supervisor() const { return m_supervisor.get(); }

    // True once a helper has answered Warm.
    bool warmed() const { return m_warmed; }

    // Runs work once on context's thread from the event loop: when the
    // helper has warmed, after warmWaitMs, or at once (queued) when either
    // has already happened or no helper can be started.  Dropped if context
    // is destroyed first.
    void whenWarm(QObject* context, std::function<void()> work);

private:
    void release(const QString& why);

    std::unique_ptr<CaptureSupervisor> m_supervisor;
    CaptureSupervisor::Lease m_standby;
    QTimer* m_bound;                       // Qt parent ownership
    std::int64_t m_startNs = 0;            // audioProbeNowNs() at construction
    std::int64_t m_heldSinceNs = 0;        // audioProbeNowNs() when work was first held
    bool m_warmed = false;
    bool m_released = false;
    std::vector<std::pair<QPointer<QObject>, std::function<void()>>> m_held;
};

} // namespace NereusSDR
