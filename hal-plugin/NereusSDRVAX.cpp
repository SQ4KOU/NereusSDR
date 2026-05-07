// =================================================================
// hal-plugin/NereusSDRVAX.cpp  (NereusSDR)
// =================================================================
//
// Ported from AetherSDR source:
//   hal-plugin/AetherSDRDAX.cpp
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
//                 (KG4VCF), with AI-assisted transformation via
//                 Anthropic Claude Code. Rebranded DAX → VAX: device
//                 UIDs com.aethersdr.dax.* → com.nereussdr.vax.*, shm
//                 paths /aethersdr-dax-* → /nereussdr-vax-*, device
//                 names "AetherSDR DAX N" → "NereusSDR VAX N", factory
//                 UUID regenerated.
// =================================================================

// NereusSDR VAX — Core Audio HAL Audio Server Plug-In
//
// Creates 4 virtual audio output devices ("NereusSDR VAX 1" through "NereusSDR VAX 4")
// for receiving VAX audio from the radio, plus 1 virtual input device ("NereusSDR TX")
// for sending TX audio to the radio.
//
// Each device reads/writes PCM audio via a POSIX shared memory ring buffer shared
// with NereusSDR's VirtualAudioBridge.
//
// Format: stereo float32, 48 kHz. NereusSDR uses 48 kHz (not AetherSDR's 24 kHz)
// to align with Thetis DSP rate conventions.

#include <aspl/Driver.hpp>
#include <aspl/DriverRequestHandler.hpp>
#include <aspl/Plugin.hpp>
#include <aspl/Device.hpp>
#include <aspl/Stream.hpp>
#include <aspl/Context.hpp>
#include <aspl/Tracer.hpp>

#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <cerrno>

// ── Diagnostic logging (NereusSDR debug build, 2026-05-06) ──────────────────
//
// Plugin runs inside coreaudiod and has no stdout/stderr; route diagnostics to
// the macOS unified log. Subsystem "com.nereussdr.vax" matches the bundle id;
// view live with:
//   log stream --predicate 'subsystem == "com.nereussdr.vax"'
// or grep historical:
//   log show --predicate 'subsystem == "com.nereussdr.vax"' --last 5m
//
// Per-callback logs are rate-limited so they do not flood the unified log on
// the audio realtime thread (~94 Hz callback rate).
#include <os/log.h>
static os_log_t s_log = os_log_create("com.nereussdr.vax", "plugin");

// ── Shared memory layout — must match src/core/audio/CoreAudioHalBus.h
//    (Sub-Phase 5.3; not yet landed). Any field change here requires the
//    matching change in CoreAudioHalBus or the shm contract breaks.

struct VaxShmBlock {
    std::atomic<uint32_t> writePos;
    std::atomic<uint32_t> readPos;
    uint32_t sampleRate;             // written by producer (CoreAudioHalBus); plugin does not read
    uint32_t channels;               // written by producer (CoreAudioHalBus); plugin does not read
    std::atomic<uint32_t> active;
    uint32_t reserved[3];            // reserved — DO NOT REMOVE; preserves layout alignment with CoreAudioHalBus
    static constexpr uint32_t RING_SIZE = 48000 * 2 * 2;  // ~2 sec @ 48kHz stereo
    float ringBuffer[RING_SIZE];
};

static_assert(
    sizeof(VaxShmBlock) ==
        2 * sizeof(std::atomic<uint32_t>)   // writePos, readPos
      + 1 * sizeof(std::atomic<uint32_t>)   // active (FIX 1: atomic with acquire/release)
      + 2 * sizeof(uint32_t)                // sampleRate, channels
      + 3 * sizeof(uint32_t)                // reserved[3]
      + VaxShmBlock::RING_SIZE * sizeof(float),
    "VaxShmBlock layout changed — update CoreAudioHalBus.h to match");

static_assert(offsetof(VaxShmBlock, writePos)    == 0,  "VaxShmBlock.writePos offset changed");
static_assert(offsetof(VaxShmBlock, readPos)     == 4,  "VaxShmBlock.readPos offset changed");
static_assert(offsetof(VaxShmBlock, sampleRate)  == 8,  "VaxShmBlock.sampleRate offset changed");
static_assert(offsetof(VaxShmBlock, channels)    == 12, "VaxShmBlock.channels offset changed");
static_assert(offsetof(VaxShmBlock, active)      == 16, "VaxShmBlock.active offset changed");
static_assert(offsetof(VaxShmBlock, reserved)    == 20, "VaxShmBlock.reserved offset changed");
static_assert(offsetof(VaxShmBlock, ringBuffer)  == 32, "VaxShmBlock.ringBuffer offset changed");

// ── VAX RX Handler: reads from shared memory → output to apps ───────────────

class VaxRxHandler : public aspl::IORequestHandler {
public:
    explicit VaxRxHandler(int channel)
        : m_channel(channel)
    {
        os_log(s_log, "VaxRxHandler ch=%{public}d constructed", m_channel);
    }

    ~VaxRxHandler() override
    {
        os_log(s_log, "VaxRxHandler ch=%{public}d destructed (m_shmBlock=%{public}p)",
               m_channel, static_cast<void*>(m_shmBlock));
        unmapShm();
    }

    void OnReadClientInput(const std::shared_ptr<aspl::Client>& client,
                           const std::shared_ptr<aspl::Stream>& stream,
                           Float64 zeroTimestamp,
                           Float64 timestamp,
                           void* bytes,
                           UInt32 bytesCount) override
    {
        auto* dst = static_cast<float*>(bytes);
        const UInt32 totalSamples = bytesCount / sizeof(float);

        // First-call log: confirm CoreAudio is invoking our handler at all.
        if (m_callCount == 0) {
            os_log(s_log,
                   "VaxRx[%{public}d] FIRST OnReadClientInput call: bytesCount=%{public}u",
                   m_channel, bytesCount);
        }
        ++m_callCount;

        if (!ensureShm()) {
            std::memset(dst, 0, bytesCount);
            // Rate-limited: log once per ~96 calls (~1 sec) when failing.
            if ((m_callCount % 96) == 1) {
                os_log_error(s_log,
                             "VaxRx[%{public}d] ensureShm() FAILED at call #%{public}llu — returning silence",
                             m_channel, m_callCount);
            }
            return;
        }

        auto* block = m_shmBlock;
        if (!block->active.load(std::memory_order_acquire)) {
            std::memset(dst, 0, bytesCount);
            if ((m_callCount % 96) == 1) {
                os_log(s_log,
                       "VaxRx[%{public}d] active=0 (call #%{public}llu) — returning silence (mapping wp=%{public}u rp=%{public}u)",
                       m_channel, m_callCount,
                       block->writePos.load(std::memory_order_relaxed),
                       block->readPos.load(std::memory_order_relaxed));
            }
            return;
        }

        uint32_t rp = block->readPos.load(std::memory_order_acquire);
        uint32_t wp = block->writePos.load(std::memory_order_acquire);

        uint32_t available = wp - rp;

        // Lap protection: if the app-side producer has written more than
        // RING_SIZE samples without our reader advancing (e.g. app pushing
        // VAX audio before any CoreAudio client connected), jump rp forward
        // to a recent window. Without this, the loop below reads from
        // already-overwritten ring slots and playback stays permanently
        // garbled until the client reconnects. Mirrors the pattern in
        // CoreAudioHalBus::pull().
        if (available > VaxShmBlock::RING_SIZE) {
            rp = wp - VaxShmBlock::RING_SIZE / 2;
            available = wp - rp;
        }

        uint32_t toRead = std::min(available, totalSamples);

        for (uint32_t i = 0; i < toRead; ++i) {
            dst[i] = block->ringBuffer[rp % VaxShmBlock::RING_SIZE];
            ++rp;
        }

        if (toRead < totalSamples) {
            std::memset(dst + toRead, 0, (totalSamples - toRead) * sizeof(float));
        }

        block->readPos.store(rp, std::memory_order_release);

        // Periodic summary so we can see whether the read path is actually
        // doing work over time.  Once per ~96 calls ≈ once per second.
        if ((m_callCount % 96) == 1) {
            os_log(s_log,
                   "VaxRx[%{public}d] read OK: call=%{public}llu bytesCount=%{public}u toRead=%{public}u wp=%{public}u rp=%{public}u",
                   m_channel, m_callCount, bytesCount, toRead, wp, rp);
        }
    }

private:
    // Stale-mmap re-validation interval (in OnReadClientInput call counts).
    // CoreAudio drives reads at ~94 Hz at the typical 1024-sample buffer, so
    // 96*5 ≈ 5 seconds.  Each re-validation costs one shm_open + fstat +
    // mmap + munmap pair; on a healthy mapping the new region maps the same
    // kernel pages so wp/rp keep their values, and on a stale mapping we
    // recover audio within one buffer.
    static constexpr int kReattachIntervalCalls = 96 * 5;

    bool ensureShm()
    {
        if (m_shmBlock != nullptr) {
            // Periodic stale-mmap check (added 2026-05-06, eager-borg-d64bed).
            //
            // macOS POSIX shm has an edge case where the plugin's cached
            // mapping can become disconnected from the live shm — confirmed
            // in the field when NereusSDR is killed and relaunched while
            // the plugin host (coreaudiod helper) is still alive.  After
            // the producer process churn the kernel object the plugin
            // mapped at attach-time can be recycled, leaving m_shmBlock
            // pointing at memory that the new NereusSDR instance does
            // not write to.  Symptom: plugin's writePos is frozen at a
            // large historical value while the live shm's writePos is
            // advancing — both processes have shm_open'd the same name
            // but resolved to different kernel objects.
            //
            // We can't observe this directly via fstat (st_ino is 0 for
            // POSIX shm on macOS), so we periodically force a fresh
            // attach.  If the mapping is healthy the new attach lands on
            // the same kernel pages and is effectively idempotent (cost:
            // a few syscalls per ~5 sec); if it's stale we re-attach to
            // the live shm and reads resume on the next call.
            if (++m_validateCounter < kReattachIntervalCalls) {
                return true;
            }
            m_validateCounter = 0;

            os_log(s_log,
                   "VaxRx[%{public}d] re-validating shm mapping (call=%{public}llu)",
                   m_channel, m_callCount);

            // Drop the cached mapping so the attach path below runs
            // unconditionally.  Skip the m_lastRetry throttle since we
            // know NereusSDR was up the moment we last read from this
            // name — there is no startup race to wait out here.
            unmapShm();
        } else {
            // Initial-attach throttle: avoid hammering shm_open during the
            // first-startup race when NereusSDR hasn't created the segment
            // yet.  Only applies on first attach (m_shmBlock was already
            // null on entry), not on staleness re-attach above.
            auto now = std::chrono::steady_clock::now();
            if (now - m_lastRetry < std::chrono::seconds(1)) return false;
            m_lastRetry = now;
        }

        char name[64];
        snprintf(name, sizeof(name), "/nereussdr-vax-%d", m_channel);

        int fd = shm_open(name, O_RDWR, 0666);
        if (fd < 0) {
            os_log_error(s_log,
                         "VaxRx[%{public}d] shm_open(%{public}s, O_RDWR) FAILED errno=%{public}d (%{public}s)",
                         m_channel, name, errno, strerror(errno));
            return false;
        }

        struct stat st;
        if (fstat(fd, &st) != 0) {
            os_log_error(s_log,
                         "VaxRx[%{public}d] fstat(%{public}s) FAILED errno=%{public}d (%{public}s)",
                         m_channel, name, errno, strerror(errno));
            ::close(fd);
            return false;
        }
        if (static_cast<size_t>(st.st_size) < sizeof(VaxShmBlock)) {
            os_log_error(s_log,
                         "VaxRx[%{public}d] shm size=%{public}lld < required=%{public}zu — refusing mmap",
                         m_channel,
                         static_cast<long long>(st.st_size),
                         sizeof(VaxShmBlock));
            ::close(fd);
            return false;
        }

        void* ptr = mmap(nullptr, sizeof(VaxShmBlock), PROT_READ | PROT_WRITE,
                         MAP_SHARED, fd, 0);
        ::close(fd);

        if (ptr == MAP_FAILED) {
            os_log_error(s_log,
                         "VaxRx[%{public}d] mmap(%{public}s) FAILED errno=%{public}d (%{public}s)",
                         m_channel, name, errno, strerror(errno));
            return false;
        }

        m_shmBlock = static_cast<VaxShmBlock*>(ptr);

        // Snapshot the freshly-attached header for diagnostics. We log st_ino
        // so a later "stale mapping" investigation can compare it against a
        // fresh shm_open's inode to detect divergence.
        const auto wpInit = m_shmBlock->writePos.load(std::memory_order_relaxed);
        const auto rpInit = m_shmBlock->readPos.load(std::memory_order_relaxed);
        const auto actInit = m_shmBlock->active.load(std::memory_order_relaxed);
        os_log(s_log,
               "VaxRx[%{public}d] shm ATTACHED: name=%{public}s ino=%{public}llu size=%{public}lld ptr=%{public}p initial wp=%{public}u rp=%{public}u active=%{public}u",
               m_channel, name,
               static_cast<unsigned long long>(st.st_ino),
               static_cast<long long>(st.st_size),
               ptr,
               wpInit, rpInit, actInit);
        return true;
    }

    void unmapShm()
    {
        if (m_shmBlock) {
            munmap(m_shmBlock, sizeof(VaxShmBlock));
            m_shmBlock = nullptr;
        }
    }

    int m_channel{1};
    VaxShmBlock* m_shmBlock{nullptr};
    std::chrono::steady_clock::time_point m_lastRetry{};
    uint64_t m_callCount{0};
    // Stale-mmap re-validation counter (ticked per OnReadClientInput call;
    // forces a fresh shm_open + mmap when it crosses kReattachIntervalCalls).
    int m_validateCounter{0};
};

// ── VAX TX Handler: receives audio from apps → writes to shared memory ──────

class VaxTxHandler : public aspl::IORequestHandler {
public:
    VaxTxHandler()
    {
        os_log(s_log, "VaxTxHandler constructed");
    }

    ~VaxTxHandler() override
    {
        os_log(s_log, "VaxTxHandler destructed (m_shmBlock=%{public}p)",
               static_cast<void*>(m_shmBlock));
        unmapShm();
    }

    void OnWriteMixedOutput(const std::shared_ptr<aspl::Stream>& stream,
                            Float64 zeroTimestamp,
                            Float64 timestamp,
                            const void* bytes,
                            UInt32 bytesCount) override
    {
        if (m_callCount == 0) {
            os_log(s_log,
                   "VaxTx FIRST OnWriteMixedOutput call: bytesCount=%{public}u",
                   bytesCount);
        }
        ++m_callCount;

        if (!ensureShm()) {
            if ((m_callCount % 96) == 1) {
                os_log_error(s_log,
                             "VaxTx ensureShm() FAILED at call #%{public}llu — dropping audio",
                             m_callCount);
            }
            return;
        }

        auto* block = m_shmBlock;
        const auto* src = static_cast<const float*>(bytes);
        const UInt32 totalSamples = bytesCount / sizeof(float);

        uint32_t wp = block->writePos.load(std::memory_order_acquire);

        for (uint32_t i = 0; i < totalSamples; ++i) {
            block->ringBuffer[wp % VaxShmBlock::RING_SIZE] = src[i];
            ++wp;
        }

        block->writePos.store(wp, std::memory_order_release);
        block->active.store(1, std::memory_order_release);

        // Periodic summary so we can see whether the write path is doing
        // work over time. Once per ~96 calls ≈ once per second.
        if ((m_callCount % 96) == 1) {
            os_log(s_log,
                   "VaxTx write OK: call=%{public}llu bytesCount=%{public}u wp=%{public}u",
                   m_callCount, bytesCount, wp);
        }
    }

private:
    // Stale-mmap re-validation interval (mirrors VaxRxHandler).
    static constexpr int kReattachIntervalCalls = 96 * 5;

    bool ensureShm()
    {
        if (m_shmBlock != nullptr) {
            // Periodic stale-mmap check (added 2026-05-06, eager-borg-d64bed).
            // Same rationale as VaxRxHandler::ensureShm — macOS can disconnect
            // the cached mapping from the live shm when NereusSDR restarts;
            // periodic re-attach keeps writes flowing without manual
            // intervention.
            if (++m_validateCounter < kReattachIntervalCalls) {
                return true;
            }
            m_validateCounter = 0;

            os_log(s_log,
                   "VaxTx re-validating shm mapping (call=%{public}llu)",
                   m_callCount);

            unmapShm();
            // Fall through; skip retry throttle (we just had a healthy mapping).
        } else {
            auto now = std::chrono::steady_clock::now();
            if (now - m_lastRetry < std::chrono::seconds(1)) return false;
            m_lastRetry = now;
        }

        int fd = shm_open("/nereussdr-vax-tx", O_RDWR, 0666);
        if (fd < 0) {
            os_log_error(s_log,
                         "VaxTx shm_open(/nereussdr-vax-tx, O_RDWR) FAILED errno=%{public}d (%{public}s)",
                         errno, strerror(errno));
            return false;
        }

        struct stat st;
        if (fstat(fd, &st) != 0) {
            os_log_error(s_log,
                         "VaxTx fstat(/nereussdr-vax-tx) FAILED errno=%{public}d (%{public}s)",
                         errno, strerror(errno));
            ::close(fd);
            return false;
        }
        if (static_cast<size_t>(st.st_size) < sizeof(VaxShmBlock)) {
            os_log_error(s_log,
                         "VaxTx shm size=%{public}lld < required=%{public}zu — refusing mmap",
                         static_cast<long long>(st.st_size),
                         sizeof(VaxShmBlock));
            ::close(fd);
            return false;
        }

        void* ptr = mmap(nullptr, sizeof(VaxShmBlock), PROT_READ | PROT_WRITE,
                         MAP_SHARED, fd, 0);
        ::close(fd);

        if (ptr == MAP_FAILED) {
            os_log_error(s_log,
                         "VaxTx mmap(/nereussdr-vax-tx) FAILED errno=%{public}d (%{public}s)",
                         errno, strerror(errno));
            return false;
        }

        m_shmBlock = static_cast<VaxShmBlock*>(ptr);

        const auto wpInit = m_shmBlock->writePos.load(std::memory_order_relaxed);
        const auto rpInit = m_shmBlock->readPos.load(std::memory_order_relaxed);
        const auto actInit = m_shmBlock->active.load(std::memory_order_relaxed);
        os_log(s_log,
               "VaxTx shm ATTACHED: name=/nereussdr-vax-tx ino=%{public}llu size=%{public}lld ptr=%{public}p initial wp=%{public}u rp=%{public}u active=%{public}u",
               static_cast<unsigned long long>(st.st_ino),
               static_cast<long long>(st.st_size),
               ptr,
               wpInit, rpInit, actInit);
        return true;
    }

    void unmapShm()
    {
        if (m_shmBlock) {
            munmap(m_shmBlock, sizeof(VaxShmBlock));
            m_shmBlock = nullptr;
        }
    }

    VaxShmBlock* m_shmBlock{nullptr};
    uint64_t m_callCount{0};
    int m_validateCounter{0};
    std::chrono::steady_clock::time_point m_lastRetry{};
};

// ── AudioStreamBasicDescription helper ──────────────────────────────────────

static AudioStreamBasicDescription makePCMFormat(Float64 sampleRate, UInt32 channels)
{
    AudioStreamBasicDescription fmt{};
    fmt.mSampleRate       = sampleRate;
    fmt.mFormatID         = kAudioFormatLinearPCM;
    fmt.mFormatFlags      = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    fmt.mBitsPerChannel   = 32;
    fmt.mChannelsPerFrame = channels;
    fmt.mBytesPerFrame    = channels * sizeof(float);
    fmt.mFramesPerPacket  = 1;
    fmt.mBytesPerPacket   = fmt.mBytesPerFrame;
    return fmt;
}

// ── DriverRequestHandler: defers device creation until host is ready ─────────

class VaxDriverHandler : public aspl::DriverRequestHandler {
public:
    VaxDriverHandler(std::shared_ptr<aspl::Context> ctx,
                     std::shared_ptr<aspl::Plugin> plug)
        : m_context(std::move(ctx))
        , m_plugin(std::move(plug))
    {}

    OSStatus OnInitialize() override
    {
        // Called by HAL after driver is fully initialized and host is available.
        // Safe to add devices here — PropertiesChanged notifications will work.

        os_log(s_log, "VaxDriverHandler::OnInitialize entry — pid=%{public}d uid=%{public}d",
               static_cast<int>(getpid()), static_cast<int>(getuid()));

        // 4 VAX RX input devices (radio → apps receive audio)
        for (int ch = 1; ch <= 4; ++ch) {
            char name[64];
            snprintf(name, sizeof(name), "NereusSDR VAX %d", ch);

            char uid[64];
            snprintf(uid, sizeof(uid), "com.nereussdr.vax.rx.%d", ch);

            auto handler = std::make_shared<VaxRxHandler>(ch);

            aspl::DeviceParameters devParams;
            devParams.Name         = name;
            devParams.Manufacturer = "NereusSDR";
            devParams.DeviceUID    = uid;
            devParams.ModelUID     = "com.nereussdr.vax";
            devParams.SampleRate   = 48000;
            devParams.ChannelCount = 2;
            devParams.EnableMixing = true;

            auto device = std::make_shared<aspl::Device>(m_context, devParams);
            device->SetIOHandler(handler);

            aspl::StreamParameters streamParams;
            streamParams.Direction = aspl::Direction::Input;
            streamParams.Format    = makePCMFormat(48000, 2);

            device->AddStreamWithControlsAsync(streamParams);
            m_plugin->AddDevice(device);

            // Keep shared_ptrs alive
            m_handlers.push_back(handler);
            m_devices.push_back(device);
        }

        // 1 TX output device (apps send audio → radio)
        {
            auto txHandler = std::make_shared<VaxTxHandler>();

            aspl::DeviceParameters txParams;
            txParams.Name         = "NereusSDR TX";
            txParams.Manufacturer = "NereusSDR";
            txParams.DeviceUID    = "com.nereussdr.vax.tx";
            txParams.ModelUID     = "com.nereussdr.vax";
            txParams.SampleRate   = 48000;
            txParams.ChannelCount = 2;
            txParams.EnableMixing = true;

            auto txDevice = std::make_shared<aspl::Device>(m_context, txParams);
            txDevice->SetIOHandler(txHandler);

            aspl::StreamParameters txStreamParams;
            txStreamParams.Direction = aspl::Direction::Output;
            txStreamParams.Format    = makePCMFormat(48000, 2);

            txDevice->AddStreamWithControlsAsync(txStreamParams);
            m_plugin->AddDevice(txDevice);

            m_handlers.push_back(txHandler);
            m_devices.push_back(txDevice);
        }

        return kAudioHardwareNoError;
    }

private:
    std::shared_ptr<aspl::Context> m_context;
    std::shared_ptr<aspl::Plugin> m_plugin;
    std::vector<std::shared_ptr<aspl::IORequestHandler>> m_handlers;
    std::vector<std::shared_ptr<aspl::Device>> m_devices;
};

// ── Driver entry point ──────────────────────────────────────────────────────

extern "C" void* NereusSDRVAX_Create(CFAllocatorRef allocator, CFUUIDRef typeUUID)
{
    if (!CFEqual(typeUUID, kAudioServerPlugInTypeUUID)) {
        return nullptr;
    }

    auto context = std::make_shared<aspl::Context>(
        std::make_shared<aspl::Tracer>());

    auto plugin = std::make_shared<aspl::Plugin>(context);

    // Devices are created in OnInitialize() after host is ready
    auto driverHandler = std::make_shared<VaxDriverHandler>(context, plugin);

    static auto driver = std::make_shared<aspl::Driver>(context, plugin);
    driver->SetDriverHandler(driverHandler);

    // Keep handler alive
    static auto handlerRef = driverHandler;

    return driver->GetReference();
}
