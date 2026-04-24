// =================================================================
// src/core/audio/LinuxAudioBackend.cpp  (NereusSDR)
// =================================================================
//  Copyright (C) 2026 J.J. Boyd (KG4VCF)
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 2
//  of the License, or (at your option) any later version.
// =================================================================
// Modification history (NereusSDR):
//   2026-04-23 — Created for the Linux PipeWire-native bridge.
//                J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//                Claude Code.
// =================================================================
#include "core/audio/LinuxAudioBackend.h"

namespace NereusSDR {

LinuxAudioBackend detectLinuxBackend(const LinuxAudioBackendProbes& probes)
{
    // Forced override (AppSettings debug key).
    const QString forced = probes.forcedBackendOverride
                             ? probes.forcedBackendOverride() : QString();
    if (forced == QLatin1String("pipewire")) { return LinuxAudioBackend::PipeWire; }
    if (forced == QLatin1String("pactl"))    { return LinuxAudioBackend::Pactl; }
    if (forced == QLatin1String("none"))     { return LinuxAudioBackend::None; }
    // Any other value (empty or garbage) falls through to probes.

    if (probes.pipewireSocketReachable && probes.pipewireSocketReachable(500)) {
        return LinuxAudioBackend::PipeWire;
    }
    if (probes.pactlBinaryRunnable && probes.pactlBinaryRunnable(500)) {
        return LinuxAudioBackend::Pactl;
    }
    return LinuxAudioBackend::None;
}

LinuxAudioBackend detectLinuxBackend()
{
    return detectLinuxBackend(defaultProbes());
}

QString toString(LinuxAudioBackend b)
{
    switch (b) {
        case LinuxAudioBackend::PipeWire: return QStringLiteral("PipeWire");
        case LinuxAudioBackend::Pactl:    return QStringLiteral("Pactl");
        case LinuxAudioBackend::None:     return QStringLiteral("None");
    }
    return QStringLiteral("None");
}

// defaultProbes() — stub implementation returns always-false callbacks.
// Task 4 replaces this body with real QFile/QProcess/AppSettings probes.
LinuxAudioBackendProbes defaultProbes()
{
    LinuxAudioBackendProbes p;
    p.pipewireSocketReachable = [](int) { return false; };
    p.pactlBinaryRunnable     = [](int) { return false; };
    p.forcedBackendOverride   = []() { return QString(); };
    return p;
}

}  // namespace NereusSDR
