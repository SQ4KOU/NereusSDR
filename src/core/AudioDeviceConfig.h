#pragma once

// =================================================================
// src/core/AudioDeviceConfig.h  (NereusSDR)
// =================================================================
//
// Phase 3O VAX per-endpoint audio device configuration. NereusSDR-
// original. Carried into AudioEngine setSpeakersConfig / setTxInputConfig
// / setVaxConfig, internally translated into a backend-specific
// PortAudioConfig (or CoreAudioHalBus / LinuxPipeBus config in
// Sub-Phases 5/6). Design spec: docs/architecture/2026-04-19-vax-design.md
// §5.3.
// =================================================================

#include <QString>

namespace NereusSDR {

struct AudioDeviceConfig {
    QString deviceName;            // empty = platform default
    int     sampleRate    = 48000;
    int     channels      = 2;
    int     bufferSamples = 256;
    bool    exclusiveMode = false; // WASAPI only
    int     hostApiIndex  = -1;    // -1 = PortAudio default host API
};

} // namespace NereusSDR
