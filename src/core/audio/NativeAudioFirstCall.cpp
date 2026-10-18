// =================================================================
// src/core/audio/NativeAudioFirstCall.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  See NativeAudioFirstCall.h.
//
// Modification history (NereusSDR):
//   2026-10-16: the persistent capture helper.  J.J. Boyd (KG4VCF),
//               AI-assisted via Anthropic Claude Code.
// =================================================================

#include "core/audio/NativeAudioFirstCall.h"

#include "core/LogCategories.h"
#include "core/audio/AudioDelayProbe.h"

#include <atomic>

namespace NereusSDR {

namespace {

// In this .cpp, so the library and every program linking it share one mark.
std::atomic<const char*> s_site{nullptr};
std::atomic<std::int64_t> s_ns{0};

} // namespace

void markNativeAudioCall(const char* site)
{
    if (s_site.load(std::memory_order_acquire) != nullptr) {
        return;
    }
    const std::int64_t now = audioProbeNowNs();
    const char* expected = nullptr;
    if (!s_site.compare_exchange_strong(expected, site, std::memory_order_acq_rel)) {
        return;
    }
    s_ns.store(now, std::memory_order_release);
    qCInfo(lcAudio).nospace() << "native audio: first call (" << site << ") at steady "
                              << now / 1'000'000 << " ms";
}

std::optional<NativeAudioFirstCall> firstNativeAudioCall()
{
    const char* site = s_site.load(std::memory_order_acquire);
    const std::int64_t ns = s_ns.load(std::memory_order_acquire);
    if (site == nullptr || ns == 0) {
        return std::nullopt;
    }
    return NativeAudioFirstCall{ns, site};
}

} // namespace NereusSDR
