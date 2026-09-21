// no-port-check: NereusSDR-original.
// =================================================================
// src/core/session/media/DaemonAudioSource.cpp  (NereusSDR)
// =================================================================

#include "DaemonAudioSource.h"

#include "core/AudioEngine.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>

namespace NereusSDR {

class DaemonAudioSource::Bridge final : public MasterMixAudioTap {
public:
    void start()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        resetLocked();
        m_dropCount.store(0, std::memory_order_relaxed);
        m_nextFramePosition.store(0, std::memory_order_relaxed);
        m_running = true;
    }

    void stop()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_running = false;
        resetLocked();
    }

    bool isRunning() const noexcept
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_running;
    }

    std::optional<DaemonAudioBlock> takeBlock()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_running || m_readyCount == 0) {
                return std::nullopt;
            }
        }

        DaemonAudioBlock block;
        block.pcmInterleaved.resize(DaemonAudioSource::kBlockSamples);
        {
            // QVector allocation is deliberately outside the shared bridge
            // mutex. The DSP callback only contends with this fixed-size copy
            // and ring bookkeeping.
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_running || m_readyCount == 0) {
                return std::nullopt;
            }
            block.samplePosition = m_readyPositions[m_readIndex];
            std::memcpy(block.pcmInterleaved.data(), m_ready[m_readIndex].data(),
                        sizeof(float) * DaemonAudioSource::kBlockSamples);
            m_readIndex = (m_readIndex + 1) % DaemonAudioSource::kQueueBlocks;
            --m_readyCount;
        }
        return block;
    }

    std::uint64_t dropCount() const noexcept
    {
        return m_dropCount.load(std::memory_order_relaxed);
    }

    std::uint64_t capturedValidRateFrames() const noexcept
    {
        return m_nextFramePosition.load(std::memory_order_relaxed);
    }

    void dropIngressForTest(int frames) noexcept
    {
        if (frames <= 0) {
            return;
        }

        // This is the same externally observable state left by a valid
        // callback that reserved its source position but lost the try-lock.
        m_nextFramePosition.fetch_add(static_cast<quint64>(frames),
                                      std::memory_order_relaxed);
        m_dropCount.fetch_add(1, std::memory_order_relaxed);
        m_discontinuity.store(true, std::memory_order_release);
    }

    void consume(const float* samples, int frames, int sampleRateHz) noexcept override
    {
        if (samples == nullptr || frames <= 0 || sampleRateHz != DaemonAudioSource::kSampleRateHz) {
            m_dropCount.fetch_add(1, std::memory_order_relaxed);
            m_discontinuity.store(true, std::memory_order_release);
            return;
        }
        const quint64 callbackFirstFrame =
            m_nextFramePosition.fetch_add(static_cast<quint64>(frames),
                                          std::memory_order_relaxed);

        std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
        if (!lock.owns_lock()) {
            // Do not wait for the consumer. The next accepted callback drops
            // a partial block so it cannot join samples across this gap.
            m_dropCount.fetch_add(1, std::memory_order_relaxed);
            m_discontinuity.store(true, std::memory_order_release);
            return;
        }
        if (!m_running) {
            return;
        }
        const bool hadDiscontinuity =
            m_discontinuity.exchange(false, std::memory_order_acq_rel);
        // Multiple producers are not assumed to arrive in reservation order.
        // An older callback that wins the mutex after a newer one would make
        // packet contents run backward in time, so reject it and let the next
        // accepted ingress establish a clean block boundary.
        if (callbackFirstFrame < m_lastAcceptedEndFrame) {
            discardPartialAndRealignLocked(m_lastAcceptedEndFrame);
            m_dropCount.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (hadDiscontinuity || callbackFirstFrame > m_lastAcceptedEndFrame) {
            // A valid-rate callback reserved earlier may have lost the
            // try-lock before it could publish the discontinuity flag. Do
            // not append this later reservation to the old partial block.
            // Discard through the next original 40 ms boundary so the next
            // emitted RTP packet can represent the loss as an integral packet
            // gap rather than an unaligned timestamp step.
            discardPartialAndRealignLocked(callbackFirstFrame);
        }
        m_lastAcceptedEndFrame = callbackFirstFrame + static_cast<quint64>(frames);

        int consumedFrames = 0;
        while (consumedFrames < frames) {
            const quint64 currentFrame = callbackFirstFrame
                + static_cast<quint64>(consumedFrames);
            if (currentFrame < m_discardBeforeFrame) {
                const quint64 skippedFrames = std::min(
                    static_cast<quint64>(frames - consumedFrames),
                    m_discardBeforeFrame - currentFrame);
                consumedFrames += static_cast<int>(skippedFrames);
                continue;
            }
            if (m_fillSamples == 0) {
                m_assemblingFirstFrame = currentFrame;
            }
            const int freeFrames = DaemonAudioSource::kBlockFrames
                - (m_fillSamples / DaemonAudioSource::kChannels);
            const int copyFrames = std::min(freeFrames, frames - consumedFrames);
            const int copySamples = copyFrames * DaemonAudioSource::kChannels;
            std::memcpy(m_assembling.data() + m_fillSamples,
                        samples + static_cast<size_t>(consumedFrames)
                            * DaemonAudioSource::kChannels,
                        static_cast<size_t>(copySamples) * sizeof(float));
            m_fillSamples += copySamples;
            consumedFrames += copyFrames;

            if (m_fillSamples == DaemonAudioSource::kBlockSamples) {
                if (m_readyCount == DaemonAudioSource::kQueueBlocks) {
                    // Drop the newest completed block. Keeping queued blocks
                    // gives the consumer a continuous prefix and avoids any
                    // producer-side allocation or blocking.
                    m_dropCount.fetch_add(1, std::memory_order_relaxed);
                } else {
                    std::memcpy(m_ready[m_writeIndex].data(), m_assembling.data(),
                                sizeof(float) * DaemonAudioSource::kBlockSamples);
                    m_readyPositions[m_writeIndex] = m_assemblingFirstFrame;
                    m_writeIndex = (m_writeIndex + 1) % DaemonAudioSource::kQueueBlocks;
                    ++m_readyCount;
                }
                m_fillSamples = 0;
            }
        }
    }

private:
    static quint64 nextBlockBoundary(quint64 frame) noexcept
    {
        const quint64 remainder = frame % DaemonAudioSource::kBlockFrames;
        return remainder == 0
            ? frame
            : frame + (DaemonAudioSource::kBlockFrames - remainder);
    }

    void discardPartialAndRealignLocked(quint64 firstAvailableFrame) noexcept
    {
        m_fillSamples = 0;
        m_discardBeforeFrame = nextBlockBoundary(firstAvailableFrame);
    }

    void resetLocked()
    {
        m_fillSamples = 0;
        m_readIndex = 0;
        m_writeIndex = 0;
        m_readyCount = 0;
        m_assemblingFirstFrame = 0;
        m_lastAcceptedEndFrame = 0;
        m_discardBeforeFrame = 0;
        m_discontinuity.store(false, std::memory_order_release);
    }

    mutable std::mutex m_mutex;
    std::array<float, DaemonAudioSource::kBlockSamples> m_assembling{};
    std::array<std::array<float, DaemonAudioSource::kBlockSamples>,
               DaemonAudioSource::kQueueBlocks> m_ready{};
    std::array<quint64, DaemonAudioSource::kQueueBlocks> m_readyPositions{};
    int m_fillSamples = 0;
    int m_readIndex = 0;
    int m_writeIndex = 0;
    int m_readyCount = 0;
    quint64 m_assemblingFirstFrame = 0;
    quint64 m_lastAcceptedEndFrame = 0;
    quint64 m_discardBeforeFrame = 0;
    bool m_running = false;
    std::atomic<bool> m_discontinuity{false};
    std::atomic<std::uint64_t> m_dropCount{0};
    std::atomic<quint64> m_nextFramePosition{0};
};

DaemonAudioSource::DaemonAudioSource(QObject* parent)
    : QObject(parent)
    , m_bridge(std::make_unique<Bridge>())
{
}

DaemonAudioSource::~DaemonAudioSource()
{
    stop();
}

void DaemonAudioSource::setAudioEngine(AudioEngine* audioEngine)
{
    if (m_audioEngine == audioEngine) {
        return;
    }
    stop();
    m_audioEngine = audioEngine;
}

AudioEngine* DaemonAudioSource::audioEngine() const noexcept
{
    return m_audioEngine.data();
}

void DaemonAudioSource::start()
{
    if (m_audioEngine.isNull()) {
        return;
    }

    // A repeated start is another capture epoch. Detach and quiesce first so
    // no prior callback can append between the reset and the new install.
    m_audioEngine->clearMasterMixAudioTap(m_bridge.get());
    // Start/reset before publishing the bridge. AudioEngine's install gate
    // means the DSP thread cannot enter consume() until after this returns.
    m_bridge->start();
    m_audioEngine->setMasterMixAudioTap(m_bridge.get());
}

void DaemonAudioSource::stop()
{
    if (!m_audioEngine.isNull()) {
        // The engine's clear gate waits for an admitted borrowed-pointer call
        // to return before this bridge's state is reset.
        m_audioEngine->clearMasterMixAudioTap(m_bridge.get());
    }
    m_bridge->stop();
}

bool DaemonAudioSource::isRunning() const noexcept
{
    return m_bridge->isRunning();
}

std::optional<DaemonAudioBlock> DaemonAudioSource::takeBlock()
{
    return m_bridge->takeBlock();
}

std::uint64_t DaemonAudioSource::dropCount() const noexcept
{
    return m_bridge->dropCount();
}

DaemonAudioSourceTelemetry DaemonAudioSource::telemetry() const noexcept
{
    return {m_bridge->capturedValidRateFrames(), m_bridge->dropCount()};
}

void DaemonAudioSource::dropIngressForTest(int frames) noexcept
{
    m_bridge->dropIngressForTest(frames);
}

} // namespace NereusSDR
