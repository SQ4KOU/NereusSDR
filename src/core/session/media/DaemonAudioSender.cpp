// =================================================================
// src/core/session/media/DaemonAudioSender.cpp  (NereusSDR)
// =================================================================

#include "DaemonAudioSender.h"
#include "core/AudioEngine.h"

#include "core/session/media/DaemonAudioSource.h"
#include "core/session/media/OpusAudioCodec.h"

#include <memory>

namespace NereusSDR {

DaemonAudioSender::DaemonAudioSender(AudioEngine* audioEngine, QObject* parent)
    : DaemonAudioSender(audioEngine, OpusAudioCodecConfig{}, parent)
{
}

DaemonAudioSender::DaemonAudioSender(AudioEngine* audioEngine,
                                     const OpusAudioCodecConfig& codecConfig,
                                     QObject* parent)
    : QObject(parent)
    , m_source(std::make_unique<DaemonAudioSource>())
    , m_encoder(std::make_unique<OpusAudioEncoder>(codecConfig))
{
    m_source->setAudioEngine(audioEngine);
    m_drainTimer.setInterval(kDrainIntervalMs);
    m_drainTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_drainTimer, &QTimer::timeout, this, &DaemonAudioSender::drain);
}

DaemonAudioSender::~DaemonAudioSender()
{
    stop();
}

bool DaemonAudioSender::start(quint32 ssrc, quint16 firstSequence,
                              quint32 firstTimestamp)
{
    // A start always defines a new capture/codec epoch, including a caller
    // reusing this object after an interrupted client connection.
    stop();
    if (ssrc == 0 || !m_encoder->isReady() || m_source->audioEngine() == nullptr) {
        return false;
    }

    m_encoder->reset();
    m_ssrc = ssrc;
    m_nextSequence = firstSequence;
    m_baseTimestamp = firstTimestamp;
    m_nextTimestamp = firstTimestamp;
    m_source->start();
    if (!m_source->isRunning()) {
        m_ssrc = 0;
        return false;
    }
    m_telemetry = {};
    ++m_lifecycleGeneration;
    m_running = true;
    m_drainTimer.start();
    return true;
}

void DaemonAudioSender::stop()
{
    ++m_lifecycleGeneration;
    m_drainTimer.stop();
    m_running = false;
    m_ssrc = 0;
    if (m_source) {
        m_source->stop();
    }
}

bool DaemonAudioSender::isRunning() const noexcept
{
    return m_running && m_source && m_source->isRunning();
}

std::optional<OpusEncoderProfile> DaemonAudioSender::encoderProfile() const
{
    return m_encoder ? m_encoder->profile() : std::nullopt;
}

DaemonAudioSenderTelemetry DaemonAudioSender::telemetry() const noexcept
{
    DaemonAudioSenderTelemetry snapshot = m_telemetry;
    if (m_source) {
        snapshot.source = m_source->telemetry();
    }
    return snapshot;
}

void DaemonAudioSender::drain()
{
    if (!isRunning()) {
        return;
    }
    const quint64 drainGeneration = m_lifecycleGeneration;

    for (int count = 0; count < kMaxBlocksPerDrain; ++count) {
        const std::optional<DaemonAudioBlock> block = m_source->takeBlock();
        if (!block.has_value()) {
            return;
        }
        ++m_telemetry.consumedBlocks;

        // The capture position, rather than timer cadence, is the RTP clock.
        // Thus source queue loss remains visible as a timestamp gap.  The
        // unsigned addition intentionally supplies normal RTP wraparound.
        const quint32 timestamp = m_baseTimestamp
            + static_cast<quint32>(block->samplePosition);
        const OpusRtpEncodeResult encoded = m_encoder->encode(
            block->pcmInterleaved, m_nextSequence, timestamp, m_ssrc);
        m_nextTimestamp = timestamp + DaemonAudioSource::kBlockFrames;
        if (encoded.status != OpusAudioCodecStatus::Accepted) {
            ++m_telemetry.encodeFailures;
            continue;
        }
        ++m_telemetry.encodedPackets;
        m_telemetry.hasLastEmittedPacket = true;
        m_telemetry.lastEmittedSequence = m_nextSequence;
        m_telemetry.lastEmittedTimestamp = timestamp;
        ++m_nextSequence;
        emit packetReady(encoded.packet);
        // A direct packetReady recipient may stop the sender or start a new
        // capture epoch.  Do not consume old queued PCM into that new epoch.
        if (m_lifecycleGeneration != drainGeneration || !isRunning()) {
            return;
        }
    }
}

} // namespace NereusSDR
