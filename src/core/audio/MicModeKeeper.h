// =================================================================
// src/core/audio/MicModeKeeper.h  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  Keeps the window's stored macOS
// Mic Mode at Standard; no upstream logic.
//
// macOS keeps a Mic Mode for each app (Standard, Wide Spectrum, Voice
// Isolation; Control Center's Mic Mode panel).  On macOS 27, when an app
// whose stored mode is Voice Isolation makes its first audio call, the
// system's voice processing settings go round every audio process for a
// minute or two, and device lists and mic opens wait on it.  NereusSDR
// never wants voice processing on its mic, so the window's stored mode is
// set to Standard before the window's first audio call, by the standing
// capture helper as the first step of its warm-up (Warm carries the
// window's bundle id).  The helper has no bundle id, so its own audio
// calls take no part in that.
//
// This file is the rule alone, over two calls a test can stand in for:
// read our stored mode; if it is anything but Standard, set Standard and
// read it back.  It also words the window's one log line for the helper's
// answer.  MacMicMode.mm supplies the system's calls.
//
// Modification history (NereusSDR):
//   2026-10-16: new, for the standing capture helper's warm-up. J.J. Boyd
//               (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#pragma once

#include "core/NereusCoreExport.h"

#include <QString>

#include <functional>

namespace NereusSDR {

// The stored Mic Mode, numbered as AVCaptureMicrophoneMode numbers it:
// AVCaptureMicrophoneModeStandard = 0, AVCaptureMicrophoneModeWideSpectrum
// = 1, AVCaptureMicrophoneModeVoiceIsolation = 2, macOS 12 and later
// (MacOSX27.0.sdk, AVFoundation.framework/Headers/AVCaptureDevice.h:2722-2726).
constexpr long kMicModeStandard = 0;
constexpr long kMicModeWideSpectrum = 1;
constexpr long kMicModeVoiceIsolation = 2;
// No mode read (the calls are missing, or the read failed).
constexpr long kMicModeNotRead = -1;

enum class MicModeOutcome {
    AlreadyStandard,   // nothing to do
    MadeStandard,      // it was another mode; it is Standard now
    Refused,           // the system did not take the change, or the read failed
    Unavailable,       // this system has no such setting
    NoBundle,          // no bundle id to keep (never affected)
};

struct MicModeResult {
    MicModeOutcome outcome = MicModeOutcome::Unavailable;
    // The mode read before any change; kMicModeNotRead when none was.
    long before = kMicModeNotRead;
};

struct MicModeCalls {
    // The stored mode for the bundle; kMicModeNotRead when the read failed.
    std::function<long(const QString& bundleId)> read;
    // Sets the stored mode; true when the system took it.
    std::function<bool(long mode, const QString& bundleId)> write;
};

NEREUS_CORE_EXPORT MicModeResult keepMicModeStandard(const QString& bundleId,
                                                     const MicModeCalls& calls);

// "Standard", "Wide Spectrum", "Voice Isolation", else "mode N".
NEREUS_CORE_EXPORT QString micModeName(long mode);
// One plain line for the helper's own log.
NEREUS_CORE_EXPORT QString micModeOutcomeText(MicModeOutcome outcome);

// The window's one log line for the helper's answer, and whether it is a
// warning.  Empty for a mode found Standard (nothing to say).
struct MicModeLogLine {
    QString text;
    bool warning = false;
};
NEREUS_CORE_EXPORT MicModeLogLine micModeLogLine(const MicModeResult& result);

} // namespace NereusSDR
