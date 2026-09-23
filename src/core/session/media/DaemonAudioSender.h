// =================================================================
// src/core/session/media/DaemonAudioSender.h  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  Owner-thread packetisation for the
// bounded daemon audio capture bridge; no transport or session policy.
// =================================================================

#pragma once

#include "core/session/media/DaemonAudioSource.h"
#include "core/session/media/OpusAudioCodec.h"
#include "core/session/media/PcmAudioCodec.h"

#include <QObject>
#include <QTimer>

#include <cstdint>
#include <memory>
#include <optional>

namespace NereusSDR {

class AudioEngine;
class OpusAudioEncoder;

/// Owner-thread packetisation diagnostics for one successful sender start.
/// `encodedPackets` means RTP construction succeeded (one per Opus block, ten
/// per lossless block); it says nothing about later transport acceptance or
/// network delivery. `encodeFailures` counts blocks.
struct DaemonAudioSenderTelemetry {
    DaemonAudioSourceTelemetry source;
    std::uint64_t consumedBlocks = 0;
    std::uint64_t encodedPackets = 0;
    std::uint64_t encodeFailures = 0;
    bool hasLastEmittedPacket = false;
    quint16 lastEmittedSequence = 0;
    quint32 lastEmittedTimestamp = 0;
};

/// Turns bounded post-master-mix blocks into RTP packets: one Opus packet per
/// 1920-frame block, or (R-R3-23 lossless) ten L16 packets of 192 frames.
///
/// Capture stays in DaemonAudioSource's DSP-safe bridge.  This QObject runs
/// the encoder only on its owning control thread, through a 10 ms precise
/// timer or the explicit drain() test seam.  It deliberately does not know
/// peers, session epochs, mute policy, or transport. The Opus encoder stays
/// built while the lossless profile runs, so a return to Opus needs no new
/// encoder.
class DaemonAudioSender final : public QObject {
    Q_OBJECT
public:
    static constexpr int kDrainIntervalMs = 10;
    static constexpr int kMaxBlocksPerDrain = 4;

    explicit DaemonAudioSender(AudioEngine* audioEngine, QObject* parent = nullptr);
    /// Encodes with `codecConfig` (R-R3-23: the Core's configured
    /// audio_bitrate). An unsupported bitrate leaves the encoder unready, so
    /// start() fails and encoderProfile() is empty, as for any encoder that
    /// cannot initialise.
    DaemonAudioSender(AudioEngine* audioEngine, const OpusAudioCodecConfig& codecConfig,
                      QObject* parent = nullptr);
    ~DaemonAudioSender() override;

    bool start(quint32 ssrc, quint16 firstSequence, quint32 firstTimestamp);

    /// The profile the next start() runs (R-R3-23). Refused (false) while
    /// running: a profile change is a new capture epoch, so the caller stops,
    /// sets the profile and starts again, which flushes queued audio and
    /// begins at the next block boundary. Default Opus.
    bool setProfile(RemoteAudioProfile profile);
    RemoteAudioProfile profile() const noexcept { return m_profile; }
    void stop();
    bool isRunning() const noexcept;

    quint16 nextSequence() const noexcept { return m_nextSequence; }
    quint32 nextTimestamp() const noexcept { return m_nextTimestamp; }

    /// The profile this sender's encoder runs. Empty only when the encoder
    /// failed to initialise, in which case start() fails too, so a
    /// successful start() always has a profile to announce.
    std::optional<OpusEncoderProfile> encoderProfile() const;
    /// The lossless profile the packetiser produces; always available.
    PcmEncoderProfile losslessProfile() const { return m_packetiser.profile(); }
    /// True when the selected profile's encoder can run.
    bool profileReady() const;

    /// Read-only diagnostics. The snapshot remains available after stop() and
    /// resets only after a later successful start().
    DaemonAudioSenderTelemetry telemetry() const noexcept;

    /// Bounded owner-thread consumer tick.  Public only so tests and an
    /// explicit host loop can drain without waiting for the QTimer.
    void drain();

signals:
    void packetReady(const QByteArray& packet);

private:
    std::unique_ptr<DaemonAudioSource> m_source;
    std::unique_ptr<OpusAudioEncoder> m_encoder;
    PcmAudioPacketiser m_packetiser;
    RemoteAudioProfile m_profile{RemoteAudioProfile::Opus};
    QTimer m_drainTimer;
    quint32 m_ssrc{0};
    quint32 m_baseTimestamp{0};
    quint16 m_nextSequence{0};
    quint32 m_nextTimestamp{0};
    quint64 m_lifecycleGeneration{0};
    DaemonAudioSenderTelemetry m_telemetry;
    bool m_running{false};
};

} // namespace NereusSDR
