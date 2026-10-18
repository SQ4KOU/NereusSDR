// =================================================================
// src/core/session/media/DaemonAudioSender.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  See DaemonAudioSender.h.
//
// Modification history (NereusSDR):
//   2026-10-10: R-AUD-SEND: no timer; drain() is called by the Core's
//               sender thread, and the state it shares with the owner is
//               under m_mutex.  J.J. Boyd (KG4VCF), AI-assisted via
//               Anthropic Claude Code.
// =================================================================

#include "DaemonAudioSender.h"
#include "core/AudioEngine.h"

#include "core/session/media/DaemonAudioSource.h"
#include "core/session/media/OpusAudioCodec.h"

#include <QElapsedTimer>

#include <algorithm>
#include <memory>
#include <utility>

namespace NereusSDR {

static_assert(PcmAudioCodecConfig::kBlockFrames == DaemonAudioSource::kBlockFrames,
              "a lossless block must be the capture block the source delivers");

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
    m_pacingClock = [clock = std::make_shared<QElapsedTimer>()] {
        if (!clock->isValid()) { clock->start(); }
        return clock->nsecsElapsed();
    };
}

DaemonAudioSender::~DaemonAudioSender()
{
    stop();
}

bool DaemonAudioSender::runningLocked() const noexcept
{
    return m_running && m_source && m_source->isRunning();
}

bool DaemonAudioSender::profileReadyLocked() const
{
    return m_profile == RemoteAudioProfile::Lossless
        ? m_packetiser.isReady() : (m_encoder && m_encoder->isReady());
}

bool DaemonAudioSender::start(quint32 ssrc, quint16 firstSequence,
                              quint32 firstTimestamp)
{
    // A start always defines a new capture/codec epoch, including a caller
    // reusing this object after an interrupted client connection.
    stop();
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        if (ssrc == 0 || !profileReadyLocked() || m_source->audioEngine() == nullptr) {
            return false;
        }
    }
    // The tap goes in outside the mutex: installing it waits for the DSP
    // thread, and no drain takes from the source while m_running is false.
    m_source->start();

    const std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_source->isRunning()) {
        m_ssrc = 0;
        return false;
    }
    m_encoder->reset();
    m_pendingLossless.clear();
    m_lastPacingNs = m_pacingClock();
    m_pacingAllowanceUnits = 0;
    m_ssrc = ssrc;
    m_nextSequence = firstSequence;
    m_baseTimestamp = firstTimestamp;
    m_nextTimestamp = firstTimestamp;
    m_telemetry = {};
    m_hasLastHandOver = false;
    m_lastHandOverNs = 0;
    ++m_lifecycleGeneration;
    m_running = true;
    return true;
}

bool DaemonAudioSender::stop()
{
    bool wasRunning = false;
    {
        // A drain under way sees the waiting owner and gives the mutex up
        // after the block it is on. From here no packet of this epoch
        // reaches the sink, and the next sequence and timestamp are final.
        m_ownerWaiting.fetch_add(1, std::memory_order_acq_rel);
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_ownerWaiting.fetch_sub(1, std::memory_order_acq_rel);
        wasRunning = runningLocked();
        ++m_lifecycleGeneration;
        m_running = false;
        m_ssrc = 0;
        m_pendingLossless.clear();
    }
    // The tap comes out outside the mutex: removing it waits for the DSP
    // thread, and m_running is false, so no drain touches the source.
    if (m_source) {
        m_source->stop();
    }
    return wasRunning;
}

bool DaemonAudioSender::isRunning() const noexcept
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return runningLocked();
}

void DaemonAudioSender::setPacketSink(PacketSink sink)
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    m_sink = std::move(sink);
}

bool DaemonAudioSender::setBlockWake(std::shared_ptr<DaemonAudioBlockWake> wake)
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    if (m_running) {
        return false;
    }
    return m_source->setBlockWake(std::move(wake));
}

bool DaemonAudioSender::setProfile(RemoteAudioProfile profile)
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    if (m_running) {
        return false;
    }
    m_profile = profile;
    return true;
}

RemoteAudioProfile DaemonAudioSender::profile() const noexcept
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_profile;
}

bool DaemonAudioSender::setSliceSource(int sliceId)
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    if (m_running) {
        return false;
    }
    return m_source->setSliceSource(sliceId);
}

bool DaemonAudioSender::setOwnerMix(int slot)
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    if (m_running) {
        return false;
    }
    return m_source->setOwnerMix(slot);
}

int DaemonAudioSender::sliceSource() const noexcept
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_source->sliceSource();
}

quint16 DaemonAudioSender::nextSequence() const noexcept
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_nextSequence;
}

quint32 DaemonAudioSender::nextTimestamp() const noexcept
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_nextTimestamp;
}

int DaemonAudioSender::pendingLosslessPackets() const noexcept
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return int(m_pendingLossless.size());
}

void DaemonAudioSender::setPacingClockForTest(PacingClock clock)
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    if (clock) { m_pacingClock = std::move(clock); }
}

bool DaemonAudioSender::setCaptureClock(DaemonAudioSource::CaptureClock clock)
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_source->setCaptureClock(std::move(clock));
}

void DaemonAudioSender::noteCaptured(const DaemonAudioBlock& block, quint32 timestamp)
{
    m_telemetry.hasCaptureStamp = true;
    m_telemetry.captureTimestamp = timestamp + static_cast<quint32>(DaemonAudioSource::kBlockFrames);
    m_telemetry.captureNs = block.capturedNs;
}

bool DaemonAudioSender::profileReady() const
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return profileReadyLocked();
}

std::optional<OpusEncoderProfile> DaemonAudioSender::encoderProfile() const
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return m_encoder ? m_encoder->profile() : std::nullopt;
}

DaemonAudioSenderTelemetry DaemonAudioSender::telemetry() const noexcept
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    DaemonAudioSenderTelemetry snapshot = m_telemetry;
    if (m_source) {
        snapshot.source = m_source->telemetry();
    }
    return snapshot;
}

qint64 DaemonAudioSender::takeLongestSendGapSinceTakenUs() noexcept
{
    const std::lock_guard<std::mutex> lock(m_mutex);
    return std::exchange(m_telemetry.longestSendGapSinceTakenUs, qint64{-1});
}

// The draining thread, with the mutex held, as a packet is handed to the
// sink: one reading of the sender's monotonic clock per packet.
void DaemonAudioSender::noteHandedOverLocked()
{
    constexpr qint64 kNsPerUs = 1000;
    const qint64 now = m_pacingClock();
    if (m_hasLastHandOver) {
        const qint64 gapUs = (now - m_lastHandOverNs) / kNsPerUs;
        m_telemetry.longestSendGapUs = std::max(m_telemetry.longestSendGapUs, gapUs);
        m_telemetry.longestSendGapSinceTakenUs =
            std::max(m_telemetry.longestSendGapSinceTakenUs, gapUs);
    }
    m_hasLastHandOver = true;
    m_lastHandOverNs = now;
}

void DaemonAudioSender::drain()
{
    quint64 drainGeneration = 0;
    bool lossless = false;
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        if (!runningLocked()) {
            return;
        }
        drainGeneration = m_lifecycleGeneration;
        lossless = m_profile == RemoteAudioProfile::Lossless;
    }
    if (lossless) {
        drainLossless(drainGeneration);
        return;
    }

    for (int count = 0; count < kMaxBlocksPerDrain; ++count) {
        // An owner in stop() goes first: this drain holds the mutex for one
        // block at most.
        if (m_ownerWaiting.load(std::memory_order_acquire) > 0) {
            return;
        }
        QByteArray announced;
        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            // The owner, or a direct packetReady recipient, may have stopped
            // the sender or started a new capture epoch. Do not consume old
            // queued PCM into that new epoch.
            if (m_lifecycleGeneration != drainGeneration || !runningLocked()) {
                return;
            }
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
            m_nextTimestamp = timestamp + DaemonAudioSource::kBlockFrames;
            noteCaptured(*block, timestamp);
            const OpusRtpEncodeResult encoded = m_encoder->encode(
                block->pcmInterleaved, m_nextSequence, timestamp, m_ssrc);
            if (encoded.status != OpusAudioCodecStatus::Accepted) {
                ++m_telemetry.encodeFailures;
                continue;
            }
            ++m_telemetry.encodedPackets;
            m_telemetry.hasLastEmittedPacket = true;
            m_telemetry.lastEmittedSequence = m_nextSequence;
            m_telemetry.lastEmittedTimestamp = timestamp;
            ++m_nextSequence;
            // Delivered under the mutex: stop() takes it, so nothing of
            // this epoch is delivered once stop() has returned.
            noteHandedOverLocked();
            if (m_sink) {
                m_sink(encoded.packet);
            }
            announced = encoded.packet;
        }
        emit packetReady(announced);
    }
}

void DaemonAudioSender::drainLossless(quint64 drainGeneration)
{
    constexpr qint64 kUnitsPerPacket = 10'000'000;
    constexpr qint64 kCapUnits = qint64(kMaxLosslessPacketsPerDrain) * kUnitsPerPacket;
    int allowed = 0;
    bool lateTick = false;
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        if (m_lifecycleGeneration != drainGeneration || !runningLocked()) {
            return;
        }
        // The allowance this tick has earned since the last one, capped.
        const qint64 now = m_pacingClock();
        const qint64 elapsed = std::max<qint64>(0, now - m_lastPacingNs);
        m_lastPacingNs = now;
        // Whole numbers: one packet is 10 ms (1e7 ns) of allowance per
        // kLosslessPacketsPer10Ms, so each elapsed nanosecond earns that many
        // units and a packet costs 1e7. Bounded well inside qint64: the cap is
        // applied to each tick and a tick's elapsed time is added only up to it.
        const qint64 earned = std::min<qint64>(elapsed, kCapUnits) * kLosslessPacketsPer10Ms;
        lateTick = earned > kCapUnits;
        m_pacingAllowanceUnits = std::min<qint64>(kCapUnits, m_pacingAllowanceUnits + earned);
        allowed = static_cast<int>(m_pacingAllowanceUnits / kUnitsPerPacket);
    }
    for (int sent = 0; sent < allowed;) {
        // An owner in stop() goes first: this drain holds the mutex for one
        // packet, or one block's packetising and its first packet, at most.
        if (m_ownerWaiting.load(std::memory_order_acquire) > 0) {
            return;
        }
        QByteArray announced;
        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            // The owner, or a direct packetReady recipient, may have stopped
            // the sender or started a new capture epoch (which discards what
            // is still waiting). Send nothing more of this block into that
            // epoch.
            if (m_lifecycleGeneration != drainGeneration || !runningLocked()) {
                return;
            }
            if (m_pendingLossless.isEmpty()) {
                const std::optional<DaemonAudioBlock> block = m_source->takeBlock();
                if (!block.has_value()) {
                    return;
                }
                ++m_telemetry.consumedBlocks;
                // As for Opus, the capture position is the RTP clock.
                const quint32 timestamp = m_baseTimestamp
                    + static_cast<quint32>(block->samplePosition);
                m_nextTimestamp = timestamp + DaemonAudioSource::kBlockFrames;
                noteCaptured(*block, timestamp);
                // R-R3-23: ten 192-frame packets per block, sequence +1 and
                // timestamp +192 each, so the next block continues the clock.
                // Sequence numbers are given out here, so nextSequence() is the
                // first number after the block even while some of it waits.
                const QList<QByteArray> packets = m_packetiser.packetiseBlock(
                    block->pcmInterleaved, m_nextSequence, timestamp, m_ssrc);
                if (packets.isEmpty()) {
                    ++m_telemetry.encodeFailures;
                    continue;
                }
                for (qsizetype index = 0; index < packets.size(); ++index) {
                    m_pendingLossless.append({packets.at(index), m_nextSequence,
                        timestamp + static_cast<quint32>(index * PcmAudioCodecConfig::kPacketFrames)});
                    ++m_nextSequence;
                    ++m_telemetry.encodedPackets;
                }
            }
            const PendingPacket next = m_pendingLossless.takeFirst();
            m_telemetry.hasLastEmittedPacket = true;
            m_telemetry.lastEmittedSequence = next.sequence;
            m_telemetry.lastEmittedTimestamp = next.timestamp;
            ++sent;
            m_pacingAllowanceUnits -= kUnitsPerPacket;
            // Delivered under the mutex, as for Opus.
            noteHandedOverLocked();
            if (m_sink) {
                m_sink(next.packet);
            }
            announced = next.packet;
        }
        emit packetReady(announced);
    }
    // The tick earned more than the cap by itself (it came over 20 ms after
    // the last) and sent the whole cap: the sender thread ran late with audio
    // waiting. An on-time tick never counts, even when idle ticks have let
    // the allowance reach the cap.
    if (lateTick && allowed == kMaxLosslessPacketsPerDrain) {
        const std::lock_guard<std::mutex> lock(m_mutex);
        if (m_lifecycleGeneration == drainGeneration && m_running) {
            ++m_telemetry.losslessCappedTicks;
        }
    }
}

} // namespace NereusSDR
