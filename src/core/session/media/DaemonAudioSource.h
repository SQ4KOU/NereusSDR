// no-port-check: NereusSDR-original.
// =================================================================
// src/core/session/media/DaemonAudioSource.h  (NereusSDR)
// =================================================================
// Bounded bridge from AudioEngine's borrowed post-master-mix callback to
// owned 40 ms stereo blocks for the remote Opus sender.
// =================================================================

#pragma once

#include <QObject>
#include <QPointer>
#include <QVector>

#include <cstdint>
#include <memory>
#include <optional>

namespace NereusSDR {

class AudioEngine;

// One owned Opus-profile capture block. samplePosition is the first 48 kHz
// stereo frame's position since this source was started. It advances across
// every valid-rate ingress callback, including ones the bounded bridge drops,
// and is therefore suitable for RTP timestamp progression without hiding loss.
struct DaemonAudioBlock {
    QVector<float> pcmInterleaved;
    quint64 samplePosition = 0;
};

class DaemonAudioSource final : public QObject {
    Q_OBJECT

public:
    static constexpr int kSampleRateHz = 48000;
    static constexpr int kChannels = 2;
    static constexpr int kBlockFrames = 1920;  // 40 ms at 48 kHz
    static constexpr int kBlockSamples = kBlockFrames * kChannels;
    static constexpr int kQueueBlocks = 4;  // 160 ms maximum queued audio

    explicit DaemonAudioSource(QObject* parent = nullptr);
    ~DaemonAudioSource() override;

    // The AudioEngine is non-owning. Changing engines stops capture first,
    // preventing a completed block from a previous station from surviving a
    // reconnect.
    void setAudioEngine(AudioEngine* audioEngine);
    AudioEngine* audioEngine() const noexcept { return m_audioEngine.data(); }

    // Control-thread lifecycle. start() discards all prior partial and queued
    // audio before installing the synchronous tap. stop() detaches the tap
    // before clearing state, so no callback can append old audio afterward.
    void start();
    void stop();
    bool isRunning() const noexcept;

    // Consumer-thread API. Returns one owned interleaved stereo block with its
    // true source frame position, or no block when stopped or the ring is
    // empty.
    std::optional<DaemonAudioBlock> takeBlock();

    // Counts rejected ingress events and completed blocks discarded because
    // the fixed ring was full. It is diagnostic only; audio loss is expected
    // under overload and never causes the DSP callback to wait.
    std::uint64_t dropCount() const noexcept;

private:
    class Bridge;

    QPointer<AudioEngine> m_audioEngine;
    std::unique_ptr<Bridge> m_bridge;
};

} // namespace NereusSDR
