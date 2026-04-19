// =================================================================
// src/core/audio/LinuxPipeBus.h  (NereusSDR)
// =================================================================
//
// Ported from AetherSDR source:
//   src/core/PipeWireAudioBridge.{h,cpp}
//
// AetherSDR is licensed under the GNU General Public License v3; see
// https://github.com/ten9876/AetherSDR for the contributor list and
// project-level LICENSE. NereusSDR is also GPLv3. AetherSDR source
// files carry no per-file GPL header; attribution is at project level
// per AetherSDR convention.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-19 — Ported/adapted in C++20 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via Anthropic
//                 Claude Code. Adapted to NereusSDR IAudioBus contract:
//                 monolithic PipeWireAudioBridge decomposed into per-endpoint
//                 LinuxPipeBus instances (Role enum for Vax1..4 / TxInput),
//                 QObject/signals dropped in favour of atomic metering,
//                 silence-fill + TX poll timers + per-channel/TX gain deferred
//                 to Phase 3M. Pipe paths: /tmp/aethersdr-dax-* →
//                 /tmp/nereussdr-vax-*. Sample rate/format: 24 kHz mono int16
//                 → 48 kHz stereo float32 (spec §8.1).
// =================================================================

#pragma once

#include "core/IAudioBus.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace NereusSDR {

// ── LinuxPipeBus ─────────────────────────────────────────────────────────────
//
// Per-endpoint IAudioBus implementation bridging NereusSDR's DSP pipeline to
// Linux PulseAudio / PipeWire virtual audio nodes via POSIX named FIFOs and
// pactl module-pipe-source / module-pipe-sink. One instance per endpoint;
// the Role enum selects the FIFO path and direction:
//
//   Role::Vax1..4  — producer; push() writes to /tmp/nereussdr-vax-{1..4}.pipe.
//                    PulseAudio exposes these as 4 virtual input devices
//                    ("NereusSDR VAX 1..4") that apps like WSJT-X open for RX.
//   Role::TxInput  — consumer; pull() reads from /tmp/nereussdr-vax-tx.pipe.
//                    PulseAudio exposes this as a virtual output device
//                    ("NereusSDR TX") that apps write TX audio into.
//
// Format is fixed at 48 kHz stereo float32 — open() validates and rejects any
// other format. Linux-only runtime (pactl, mkfifo); the header compiles on all
// platforms so the test build can link against it under an #ifdef Q_OS_LINUX guard.
//
// Lifecycle: open() creates the FIFO + loads the pactl module + opens the fd.
//            close() closes the fd, unloads the module, and unlinks the FIFO.
//            Stale module cleanup runs once per process (std::once_flag).
//
// TODO(phase3M): TX-side poll timer — on Role::TxInput, pull() is driven on
//                demand from the caller. The AetherSDR PipeWireAudioBridge
//                polled the TX pipe every 5 ms via QTimer::timeout. Re-add
//                when AudioEngine is wired to LinuxPipeBus.
//
// TODO(phase3M): silence-fill during TX — keeps pipe-source clock advancing
//                so WSJT-X / VARA don't see a stalled audio source.
//                See AetherSDR PipeWireAudioBridge::feedSilenceToAllPipes().
//
// TODO(phase3M): per-bus gain — AetherSDR had setGain() / setChannelGain() /
//                setTxGain(). Metering here is RMS-only; no gain applied.
//
// TODO(phase3M): Flatpak/Snap sandboxing blocks virtual-audio-node publication.
//                pactl is unreachable inside the sandbox; a D-Bus portal will
//                be required.

class LinuxPipeBus : public IAudioBus {
public:
    enum class Role {
        Vax1 = 1,
        Vax2 = 2,
        Vax3 = 3,
        Vax4 = 4,
        TxInput,
    };

    explicit LinuxPipeBus(Role role);
    ~LinuxPipeBus() override;

    bool open(const AudioFormat& format) override;
    void close() override;
    bool isOpen() const override { return m_open; }

    // Producer side (Role::Vax1..4 only). Returns -1 for TxInput.
    qint64 push(const char* data, qint64 bytes) override;

    // Consumer side (Role::TxInput only). Returns -1 for Vax1..4.
    qint64 pull(char* data, qint64 maxBytes) override;

    float rxLevel() const override { return m_rxLevel.load(std::memory_order_acquire); }
    float txLevel() const override { return m_txLevel.load(std::memory_order_acquire); }

    QString     backendName() const override { return m_backendName; }
    AudioFormat negotiatedFormat() const override { return m_negFormat; }
    QString     errorString() const override { return m_err; }

    // Diagnostics — FIFO path this instance uses (e.g. "/tmp/nereussdr-vax-1.pipe").
    // Stable for the lifetime of the object; computed from Role at construction.
    const char* pipePath() const { return m_pipePath; }
    Role        role() const { return m_role; }

private:
    bool isProducer() const {
        return m_role == Role::Vax1 || m_role == Role::Vax2
            || m_role == Role::Vax3 || m_role == Role::Vax4;
    }

    Role         m_role;
    const char*  m_pipePath;   // pointer into a string-literal table in the .cpp

    bool         m_open{false};
    int          m_pipeFd{-1};
    uint32_t     m_moduleIndex{0};

    AudioFormat  m_negFormat;
    QString      m_backendName;
    QString      m_err;

    std::atomic<float> m_rxLevel{0.0f};
    std::atomic<float> m_txLevel{0.0f};

    // Metering: update RMS every Nth push/pull so the hot path doesn't walk
    // the whole block on every call.
    int m_meterCounter{0};
    static constexpr int kMeterStride = 10;
};

} // namespace NereusSDR
