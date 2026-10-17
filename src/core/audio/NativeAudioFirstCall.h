// =================================================================
// src/core/audio/NativeAudioFirstCall.h  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  The time of this process's first
// call into the operating system's audio services; no Thetis logic.
//
// On macOS a bundled app's first Core Audio call reads its stored Mic Mode,
// and when that mode is Voice Isolation the system then re-applies it for
// 40 to 100 s, during which a new Core Audio client cannot start.  The
// capture helper is started, and has listed its devices, before that first
// call (CaptureStandby), and the window logs when the helper listed them
// against this mark
// (docs/architecture/2026-09-22-optional-microphone-capture-design.md,
// addendum of 2026-10-16).  The mark is a measurement: nothing waits on it.
//
// Modification history (NereusSDR):
//   2026-10-16: the persistent capture helper.  J.J. Boyd (KG4VCF),
//               AI-assisted via Anthropic Claude Code.
// =================================================================

#pragma once

#include "core/NereusCoreExport.h"

#include <cstdint>
#include <optional>

namespace NereusSDR {

struct NativeAudioFirstCall {
    std::int64_t ns = 0;            // audioProbeNowNs() when it was made
    const char* site = "";          // the constant name of the call site
};

// Marks a call into the native audio services about to be made.  The first
// mark of the process is kept and logged once; later marks only read an
// atomic.  site must be a string literal.  Any thread.
NEREUS_CORE_EXPORT void markNativeAudioCall(const char* site);

// The process's first mark, or nullopt before any.  Any thread.
NEREUS_CORE_EXPORT std::optional<NativeAudioFirstCall> firstNativeAudioCall();

} // namespace NereusSDR
