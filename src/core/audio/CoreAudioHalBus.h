// =================================================================
// src/core/audio/CoreAudioHalBus.h  (NereusSDR)
// =================================================================
//
// Ported from AetherSDR source:
//   src/core/VirtualAudioBridge.h
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
//                 monolithic VirtualAudioBridge decomposed into per-endpoint
//                 CoreAudioHalBus instances (Role enum for Vax1..4 / TxInput),
//                 QObject/signals dropped in favor of atomic metering,
//                 silence-fill + TX poll timers deferred to Phase 3M.
//                 Shm paths: /aethersdr-dax-* → /nereussdr-vax-*.
//                 Sample rate: 24 kHz → 48 kHz (spec §8.1).
// =================================================================

#pragma once

#include "core/IAudioBus.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace NereusSDR {

// ── Shared memory layout — canonical definition ─────────────────────────────
//
// MUST match byte-for-byte the mirrored struct in hal-plugin/NereusSDRVAX.cpp.
// Any change to field order, size, alignment, or ring size here requires the
// matching change in the HAL plugin or the shm contract breaks. Both sides
// carry an equivalent static_assert pinning the total size.
//
// Field semantics:
//   writePos — producer monotonic counter (CoreAudioHalBus Vax1..4 / plugin TX).
//   readPos  — consumer monotonic counter (plugin RX / CoreAudioHalBus TxInput).
//   active   — producer→consumer liveness flag; 1 while producer is feeding.
//   sampleRate / channels — producer-written diagnostic fields; consumer does
//                           not read them (format is fixed at 48 kHz stereo).
//   reserved[3] — layout padding; DO NOT REMOVE.
//
// Ring size: 48000 * 2 * 2 = 192000 floats = ~2 sec @ 48 kHz stereo.
//
// sizeof(VaxShmBlock) evaluates to 768032 bytes with natural alignment on all
// supported platforms; the static_assert below makes a layout drift a compile
// error rather than a runtime shm-format mismatch.

struct VaxShmBlock {
    // Field ORDER is the shm wire contract with hal-plugin/NereusSDRVAX.cpp.
    // Both sides must declare these fields in the same sequence so offsetof
    // values agree. DO NOT REORDER without updating the plugin in lockstep.
    // The static_asserts below pin the layout at compile time on both sides.
    std::atomic<uint32_t> writePos;   // offset 0
    std::atomic<uint32_t> readPos;    // offset 4
    uint32_t sampleRate;              // offset 8  — producer-written; consumer does not read
    uint32_t channels;                // offset 12 — producer-written; consumer does not read
    std::atomic<uint32_t> active;     // offset 16 — SPSC gate; writer stores release, reader loads acquire
    uint32_t reserved[3];             // offset 20 — reserved; DO NOT REMOVE (preserves layout)
    static constexpr uint32_t RING_SIZE = 48000 * 2 * 2;  // ~2 sec @ 48 kHz stereo
    float ringBuffer[RING_SIZE];      // offset 32
};

static_assert(
    sizeof(VaxShmBlock) ==
        2 * sizeof(std::atomic<uint32_t>)   // writePos, readPos
      + 1 * sizeof(std::atomic<uint32_t>)   // active (atomic with acquire/release)
      + 2 * sizeof(uint32_t)                // sampleRate, channels
      + 3 * sizeof(uint32_t)                // reserved[3]
      + VaxShmBlock::RING_SIZE * sizeof(float),
    "VaxShmBlock layout changed — update hal-plugin/NereusSDRVAX.cpp to match");

static_assert(offsetof(VaxShmBlock, writePos)    == 0,  "VaxShmBlock.writePos offset changed");
static_assert(offsetof(VaxShmBlock, readPos)     == 4,  "VaxShmBlock.readPos offset changed");
static_assert(offsetof(VaxShmBlock, sampleRate)  == 8,  "VaxShmBlock.sampleRate offset changed");
static_assert(offsetof(VaxShmBlock, channels)    == 12, "VaxShmBlock.channels offset changed");
static_assert(offsetof(VaxShmBlock, active)      == 16, "VaxShmBlock.active offset changed");
static_assert(offsetof(VaxShmBlock, reserved)    == 20, "VaxShmBlock.reserved offset changed");
static_assert(offsetof(VaxShmBlock, ringBuffer)  == 32, "VaxShmBlock.ringBuffer offset changed");

// ── CoreAudioHalBus ─────────────────────────────────────────────────────────
//
// Per-endpoint IAudioBus implementation bridging NereusSDR's DSP pipeline to
// the macOS CoreAudio HAL plugin (NereusSDRVAX.driver) via POSIX shared memory.
// One instance per endpoint; the Role enum selects the shm segment and the
// direction:
//
//   Role::Vax1..4  — producer; push() writes to /nereussdr-vax-{1..4}.
//                    The HAL plugin exposes these as 4 virtual input devices
//                    ("NereusSDR VAX 1..4") that apps like WSJT-X open for RX.
//   Role::TxInput  — consumer; pull() reads from /nereussdr-vax-tx. The HAL
//                    plugin exposes this as a virtual output device
//                    ("NereusSDR TX") that apps write TX audio into.
//
// Format is fixed at 48 kHz stereo float32 — open() validates and rejects any
// other format. macOS-only runtime; the header compiles on all platforms so
// the test build can link against it under an #ifdef Q_OS_MAC guard.

class CoreAudioHalBus : public IAudioBus {
public:
    enum class Role {
        Vax1 = 1,
        Vax2 = 2,
        Vax3 = 3,
        Vax4 = 4,
        TxInput,
    };

    explicit CoreAudioHalBus(Role role);
    ~CoreAudioHalBus() override;

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

    // Diagnostics — shm path this instance uses (e.g. "/nereussdr-vax-1").
    // Stable for the lifetime of the object; computed from Role at construction.
    const char* shmName() const { return m_shmName; }
    Role        role() const { return m_role; }

private:
    bool isProducer() const {
        return m_role == Role::Vax1 || m_role == Role::Vax2
            || m_role == Role::Vax3 || m_role == Role::Vax4;
    }

    Role         m_role;
    const char*  m_shmName;   // pointer into a string-literal table in the .cpp

    bool            m_open{false};
    int             m_shmFd{-1};
    VaxShmBlock*    m_block{nullptr};

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
