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
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cmath>

// ── Shared memory layout — must match VirtualAudioBridge.h ──────────────────

struct VaxShmBlock {
    std::atomic<uint32_t> writePos;
    std::atomic<uint32_t> readPos;
    uint32_t sampleRate;
    uint32_t channels;
    uint32_t active;
    uint32_t reserved[3];
    static constexpr uint32_t RING_SIZE = 48000 * 2 * 2;  // ~2 sec @ 48kHz stereo
    float ringBuffer[RING_SIZE];
};

// ── VAX RX Handler: reads from shared memory → output to apps ───────────────

class VaxRxHandler : public aspl::IORequestHandler {
public:
    explicit VaxRxHandler(int channel)
        : m_channel(channel)
    {}

    ~VaxRxHandler() override
    {
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

        if (!ensureShm()) {
            std::memset(dst, 0, bytesCount);
            return;
        }

        auto* block = m_shmBlock;
        if (!block->active) {
            std::memset(dst, 0, bytesCount);
            return;
        }

        uint32_t rp = block->readPos.load(std::memory_order_acquire);
        uint32_t wp = block->writePos.load(std::memory_order_acquire);

        uint32_t available = wp - rp;
        uint32_t toRead = std::min(available, totalSamples);

        for (uint32_t i = 0; i < toRead; ++i) {
            dst[i] = block->ringBuffer[rp % VaxShmBlock::RING_SIZE];
            ++rp;
        }

        if (toRead < totalSamples) {
            std::memset(dst + toRead, 0, (totalSamples - toRead) * sizeof(float));
        }

        block->readPos.store(rp, std::memory_order_release);
    }

private:
    bool ensureShm()
    {
        if (m_shmBlock) return true;

        // Retry periodically — NereusSDR may not have created the segment yet.
        auto now = std::chrono::steady_clock::now();
        if (now - m_lastRetry < std::chrono::seconds(1)) return false;
        m_lastRetry = now;

        char name[64];
        snprintf(name, sizeof(name), "/nereussdr-vax-%d", m_channel);

        int fd = shm_open(name, O_RDWR, 0666);
        if (fd < 0) return false;

        void* ptr = mmap(nullptr, sizeof(VaxShmBlock), PROT_READ | PROT_WRITE,
                         MAP_SHARED, fd, 0);
        ::close(fd);

        if (ptr == MAP_FAILED) return false;

        m_shmBlock = static_cast<VaxShmBlock*>(ptr);
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
};

// ── VAX TX Handler: receives audio from apps → writes to shared memory ──────

class VaxTxHandler : public aspl::IORequestHandler {
public:
    VaxTxHandler() = default;

    ~VaxTxHandler() override
    {
        unmapShm();
    }

    void OnWriteMixedOutput(const std::shared_ptr<aspl::Stream>& stream,
                            Float64 zeroTimestamp,
                            Float64 timestamp,
                            const void* bytes,
                            UInt32 bytesCount) override
    {
        if (!ensureShm()) return;

        auto* block = m_shmBlock;
        const auto* src = static_cast<const float*>(bytes);
        const UInt32 totalSamples = bytesCount / sizeof(float);

        uint32_t wp = block->writePos.load(std::memory_order_acquire);

        for (uint32_t i = 0; i < totalSamples; ++i) {
            block->ringBuffer[wp % VaxShmBlock::RING_SIZE] = src[i];
            ++wp;
        }

        block->writePos.store(wp, std::memory_order_release);
        block->active = 1;
    }

private:
    bool ensureShm()
    {
        if (m_shmBlock) return true;

        auto now = std::chrono::steady_clock::now();
        if (now - m_lastRetry < std::chrono::seconds(1)) return false;
        m_lastRetry = now;

        int fd = shm_open("/nereussdr-vax-tx", O_RDWR, 0666);
        if (fd < 0) return false;

        void* ptr = mmap(nullptr, sizeof(VaxShmBlock), PROT_READ | PROT_WRITE,
                         MAP_SHARED, fd, 0);
        ::close(fd);

        if (ptr == MAP_FAILED) return false;

        m_shmBlock = static_cast<VaxShmBlock*>(ptr);
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
