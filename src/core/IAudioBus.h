// =================================================================
// src/core/IAudioBus.h  (NereusSDR)
// =================================================================
//
// Phase 3O abstract audio bus. Concrete implementations live in
// src/core/audio/: CoreAudioHalBus (macOS), LinuxPipeBus (Linux),
// PortAudioBus (Windows + Mac/Linux fallback).
//
// Design spec: docs/architecture/2026-04-19-vax-design.md §3.2
// =================================================================

#pragma once

#include <QString>

namespace NereusSDR {

struct AudioFormat {
    int sampleRate = 48000;  // Hz
    int channels   = 2;       // 1 or 2
    enum class Sample { Float32, Int16, Int24, Int32 } sample = Sample::Float32;

    bool operator==(const AudioFormat& o) const {
        return sampleRate == o.sampleRate && channels == o.channels && sample == o.sample;
    }
    bool operator!=(const AudioFormat& o) const { return !(*this == o); }
};

class IAudioBus {
public:
    virtual ~IAudioBus() = default;

    // Lifecycle. open() returns false on failure; errorString() has details.
    virtual bool open(const AudioFormat& format) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

    // Producer side (RX taps). Interleaved PCM bytes. Returns bytes actually
    // written, or -1 on error. Must be callable from the audio thread.
    virtual qint64 push(const char* data, qint64 bytes) = 0;

    // Consumer side (TX). Returns bytes read, or -1 on error. Audio-thread safe.
    virtual qint64 pull(char* data, qint64 maxBytes) = 0;

    // Drop any samples queued in the bus's internal buffer that have been
    // pushed but not yet consumed (or pulled but not yet read).  Used by
    // AudioEngine::setMasterMuted to stop already-buffered pre-mute audio
    // from draining out the speakers device after the mute click — see
    // issue #201.  Default no-op for buses without an internal ring (HAL
    // shm, PipeWire, FIFO).  PortAudioBus overrides to atomically equalize
    // its ring read/write cursors.  Safe to call from any thread.
    virtual void flush() {}

    // Metering (RMS of last block). 0.0–1.0. Published atomically for UI.
    virtual float rxLevel() const = 0;
    virtual float txLevel() const = 0;

    // Diagnostics.
    virtual QString backendName() const = 0;
    virtual AudioFormat negotiatedFormat() const = 0;
    virtual QString errorString() const { return {}; }
};

} // namespace NereusSDR
