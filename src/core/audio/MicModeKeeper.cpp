// =================================================================
// src/core/audio/MicModeKeeper.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  See MicModeKeeper.h.
//
// Modification history (NereusSDR):
//   2026-10-16: new, for the standing capture helper's warm-up. J.J. Boyd
//               (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#include "core/audio/MicModeKeeper.h"

namespace NereusSDR {

MicModeResult keepMicModeStandard(const QString& bundleId, const MicModeCalls& calls)
{
    MicModeResult result;
    if (bundleId.isEmpty()) {
        result.outcome = MicModeOutcome::NoBundle;
        return result;
    }
    if (!calls.read || !calls.write) {
        result.outcome = MicModeOutcome::Unavailable;
        return result;
    }
    result.before = calls.read(bundleId);
    if (result.before == kMicModeNotRead) {
        // Not read, so not known to be anything but Standard: leave it.
        result.outcome = MicModeOutcome::Refused;
        return result;
    }
    if (result.before == kMicModeStandard) {
        result.outcome = MicModeOutcome::AlreadyStandard;
        return result;
    }
    // Whatever mode it was, NereusSDR wants none of the system's voice
    // processing on its mic.  Read back: a yes that did not stick is no.
    if (!calls.write(kMicModeStandard, bundleId) || calls.read(bundleId) != kMicModeStandard) {
        result.outcome = MicModeOutcome::Refused;
        return result;
    }
    result.outcome = MicModeOutcome::MadeStandard;
    return result;
}

QString micModeName(long mode)
{
    switch (mode) {
    case kMicModeStandard:       return QStringLiteral("Standard");
    case kMicModeWideSpectrum:   return QStringLiteral("Wide Spectrum");
    case kMicModeVoiceIsolation: return QStringLiteral("Voice Isolation");
    default: break;
    }
    return QStringLiteral("mode %1").arg(mode);
}

QString micModeOutcomeText(MicModeOutcome outcome)
{
    switch (outcome) {
    case MicModeOutcome::AlreadyStandard: return QStringLiteral("Mic Mode is Standard");
    case MicModeOutcome::MadeStandard:    return QStringLiteral("Mic Mode set to Standard");
    case MicModeOutcome::Refused:         return QStringLiteral("Mic Mode could not be set to Standard");
    case MicModeOutcome::Unavailable:     return QStringLiteral("Mic Mode is not available on this system");
    case MicModeOutcome::NoBundle:        return QStringLiteral("no bundle id, so no Mic Mode");
    }
    return QStringLiteral("Mic Mode outcome unknown");
}

MicModeLogLine micModeLogLine(const MicModeResult& result)
{
    switch (result.outcome) {
    case MicModeOutcome::AlreadyStandard:
    case MicModeOutcome::NoBundle:
        return {};
    case MicModeOutcome::MadeStandard:
        return {QStringLiteral("Mic Mode: set to Standard for this app; it was %1")
                    .arg(micModeName(result.before)), false};
    case MicModeOutcome::Refused:
        if (result.before == kMicModeNotRead) {
            return {QStringLiteral("Mic Mode: the system could not read this app's Mic Mode; "
                                   "Mic Mode left as it is"), true};
        }
        return {QStringLiteral("Mic Mode: the system did not take Standard for this app; "
                               "it was %1").arg(micModeName(result.before)), true};
    case MicModeOutcome::Unavailable:
        return {QStringLiteral("Mic Mode: this macOS has no Mic Mode call; "
                               "Mic Mode left as it is"), false};
    }
    return {};
}

} // namespace NereusSDR
