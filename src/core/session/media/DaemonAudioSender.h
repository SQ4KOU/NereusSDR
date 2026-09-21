// =================================================================
// src/core/session/media/DaemonAudioSender.h  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  Owner-thread packetisation for the
// bounded daemon audio capture bridge; no transport or session policy.
// =================================================================

#pragma once

#include <QObject>
#include <QTimer>

#include <cstdint>
#include <memory>

namespace NereusSDR {

class AudioEngine;
class DaemonAudioSource;
class OpusAudioEncoder;

/// Turns bounded post-master-mix blocks into RTP/Opus packets.
///
/// Capture stays in DaemonAudioSource's DSP-safe bridge.  This QObject runs
/// the encoder only on its owning control thread, through a 10 ms precise
/// timer or the explicit drain() test seam.  It deliberately does not know
/// peers, session epochs, mute policy, or transport.
class DaemonAudioSender final : public QObject {
    Q_OBJECT
public:
    static constexpr int kDrainIntervalMs = 10;
    static constexpr int kMaxBlocksPerDrain = 4;

    explicit DaemonAudioSender(AudioEngine* audioEngine, QObject* parent = nullptr);
    ~DaemonAudioSender() override;

    bool start(quint32 ssrc, quint16 firstSequence, quint32 firstTimestamp);
    void stop();
    bool isRunning() const noexcept;

    quint16 nextSequence() const noexcept { return m_nextSequence; }
    quint32 nextTimestamp() const noexcept { return m_nextTimestamp; }

    /// Bounded owner-thread consumer tick.  Public only so tests and an
    /// explicit host loop can drain without waiting for the QTimer.
    void drain();

signals:
    void packetReady(const QByteArray& packet);

private:
    std::unique_ptr<DaemonAudioSource> m_source;
    std::unique_ptr<OpusAudioEncoder> m_encoder;
    QTimer m_drainTimer;
    quint32 m_ssrc{0};
    quint32 m_baseTimestamp{0};
    quint16 m_nextSequence{0};
    quint32 m_nextTimestamp{0};
    quint64 m_lifecycleGeneration{0};
    bool m_running{false};
};

} // namespace NereusSDR
