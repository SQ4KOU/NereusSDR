// =================================================================
// src/core/session/media/RemoteMicReceiver.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. See RemoteMicReceiver.h.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-25: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), iPhone app plan Task 36 (R-IOS-13), with
//               AI-assisted implementation via Anthropic Claude Code.
//   2026-09-26: Transmit group fix wave C2: setFeedWriter, one line
//               writes the transmitter's feed at a time. J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include "core/session/media/RemoteMicReceiver.h"

#include "core/LogCategories.h"
#include "core/session/media/OpusAudioCodec.h"
#include "core/session/media/PcmAudioCodec.h"
#include "core/session/media/RemoteAudioRateMatcher.h"

#include <QPointer>
#include <QTimer>

#include <opus.h>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace NereusSDR {

namespace {

constexpr int kFloatBytes = static_cast<int>(sizeof(float));
constexpr int kPumpBlock = RemoteMicConfig::kPumpBlockFrames;
// libopus decodes at most 120 ms in one packet.
constexpr int kMaxOpusFrames = RemoteMicConfig::kSampleRate / 1000 * 120;
// A gap is concealed only while it fits the buffer's target; a longer one
// inserts nothing (the buffer has run empty meanwhile anyway).
constexpr int kMaxConcealFrames = RemoteMicConfig::kTargetDepthFrames;

qint64 steadyMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool configureMatcher(RemoteAudioRateMatcher& matcher)
{
    // The ring is the transmit jitter buffer: WDSP rmatch holds it half full
    // (its control targets n_ring = rsize / 2), so a 120 ms ring keeps the
    // 60 ms target, and an overflow drops the oldest audio past 120 ms.
    return matcher.configure(kPumpBlock, kPumpBlock, RemoteMicConfig::kMaxDepthFrames,
                             RemoteMicConfig::kSampleRate);
}

} // namespace

bool opusPacketCarriesFec(const QByteArray& payload)
{
    return !payload.isEmpty()
        && opus_packet_has_lbrr(reinterpret_cast<const unsigned char*>(payload.constData()),
                                static_cast<opus_int32>(payload.size()))
            == 1;
}

// ============================================================================
// RemoteMicFeed
// ============================================================================

RemoteMicFeed::RemoteMicFeed()
    : m_matcher(std::make_unique<RemoteAudioRateMatcher>())
    , m_monoScratch(static_cast<size_t>(kPumpBlock))
    , m_stereoScratch(static_cast<size_t>(kPumpBlock) * 2)
{
    if (!configureMatcher(*m_matcher)) {
        qCWarning(lcAudio) << "Remote microphone: the rate matcher could not be built;"
                           << "remote microphone audio will be silent";
    }
}

RemoteMicFeed::~RemoteMicFeed() = default;

void RemoteMicFeed::setInUse(bool inUse)
{
    if (inUse == m_inUse) {
        return;
    }
    m_inUse = inUse;
    m_framesSinceInUse = 0;
    // Everything written so far is dropped by the pump when it sees this
    // change; the buffer starts again empty, from silence.
    m_clearAtBytes.store(m_writtenBytes, std::memory_order_release);
    m_inUseForPump.store(inUse, std::memory_order_release);
    m_change.fetch_add(1, std::memory_order_acq_rel);
}

bool RemoteMicFeed::write(const float* mono, int frames)
{
    if (!m_inUse || mono == nullptr || frames <= 0) {
        return false;
    }
    const qint64 bytes = static_cast<qint64>(frames) * kFloatBytes;
    if (m_input.tryPushCopy(reinterpret_cast<const uint8_t*>(mono), bytes) != bytes) {
        m_droppedFrames.fetch_add(static_cast<quint64>(frames), std::memory_order_relaxed);
        return false;
    }
    m_writtenBytes += static_cast<quint64>(bytes);
    m_framesSinceInUse += frames;
    return true;
}

void RemoteMicFeed::discardInputTo(quint64 bytes)
{
    while (m_readBytes < bytes) {
        const qint64 want = static_cast<qint64>(
            std::min<quint64>(bytes - m_readBytes,
                              static_cast<quint64>(m_monoScratch.size()) * kFloatBytes));
        const qint64 got =
            m_input.popInto(reinterpret_cast<uint8_t*>(m_monoScratch.data()), want);
        if (got <= 0) {
            break;
        }
        m_readBytes += static_cast<quint64>(got);
    }
}

void RemoteMicFeed::discardAllInput()
{
    for (;;) {
        const qint64 got = m_input.popInto(reinterpret_cast<uint8_t*>(m_monoScratch.data()),
                                           static_cast<qint64>(m_monoScratch.size()) * kFloatBytes);
        if (got <= 0) {
            break;
        }
        m_readBytes += static_cast<quint64>(got);
    }
}

bool RemoteMicFeed::pull(float* dst, int frames)
{
    const quint64 change = m_change.load(std::memory_order_acquire);
    if (change != m_seenChange) {
        // A change of use: drop what was written before it and start the
        // buffer again. Rebuilding the matcher is the one allocation on
        // this thread, once per key or VOX change, never per block.
        discardInputTo(m_clearAtBytes.load(std::memory_order_acquire));
        m_matcher->reset();
        // WDSP rmatch starts with its ring half full of silence
        // (calc_rmatch: n_ring = rsize / 2). Read it out, so the buffer
        // counts only the microphone's audio and the target means 60 ms of
        // it. Reads before rmatch's control starts leave its estimate alone
        // (control() runs only once control_flag is set).
        while (m_matcher->stats().ringFillFrames >= kPumpBlock) {
            if (!m_matcher->takeInto(m_stereoScratch.data(), kPumpBlock)) {
                break;
            }
        }
        m_started = false;
        m_underflowsAtStart = 0;
        m_seenChange = change;
        m_statsStarted.store(false, std::memory_order_relaxed);
        m_statsFill.store(0, std::memory_order_relaxed);
        m_statsRatio.store(1.0, std::memory_order_relaxed);
        m_statsUnderflows.store(0, std::memory_order_relaxed);
        m_statsOverflows.store(0, std::memory_order_relaxed);
        m_statsChanges.store(change, std::memory_order_relaxed);
    }
    if (!m_inUseForPump.load(std::memory_order_acquire)) {
        discardAllInput();
        return false;
    }
    if (dst == nullptr || frames != kPumpBlock) {
        return false;
    }

    // Everything that arrived goes into the matcher, a pump block at a time.
    const qint64 blockBytes = static_cast<qint64>(kPumpBlock) * kFloatBytes;
    while (static_cast<qint64>(m_input.usedBytes()) >= blockBytes) {
        const qint64 got =
            m_input.popInto(reinterpret_cast<uint8_t*>(m_monoScratch.data()), blockBytes);
        if (got != blockBytes) {
            break;
        }
        m_readBytes += static_cast<quint64>(got);
        for (int i = 0; i < kPumpBlock; ++i) {
            const float sample = m_monoScratch[static_cast<size_t>(i)];
            m_stereoScratch[static_cast<size_t>(2 * i)] = sample;
            m_stereoScratch[static_cast<size_t>(2 * i + 1)] = sample;
        }
        m_matcher->push(m_stereoScratch.data(), kPumpBlock);
    }

    RemoteAudioRateMatcherStats matcher = m_matcher->stats();
    if (!m_started) {
        if (matcher.ringFillFrames < RemoteMicConfig::kTargetDepthFrames) {
            std::fill(dst, dst + frames, 0.0f);
            m_statsFill.store(matcher.ringFillFrames, std::memory_order_relaxed);
            m_statsOverflows.store(matcher.overflows, std::memory_order_relaxed);
            return true;
        }
        m_started = true;
        m_underflowsAtStart = matcher.underflows;
        m_statsStarted.store(true, std::memory_order_relaxed);
    }
    if (!m_matcher->takeInto(m_stereoScratch.data(), kPumpBlock)) {
        std::fill(dst, dst + frames, 0.0f);
        return true;
    }
    for (int i = 0; i < kPumpBlock; ++i) {
        dst[i] = m_stereoScratch[static_cast<size_t>(2 * i)];
    }
    matcher = m_matcher->stats();
    m_statsFill.store(matcher.ringFillFrames, std::memory_order_relaxed);
    m_statsRatio.store(matcher.currentRatio, std::memory_order_relaxed);
    m_statsUnderflows.store(matcher.underflows - m_underflowsAtStart, std::memory_order_relaxed);
    m_statsOverflows.store(matcher.overflows, std::memory_order_relaxed);
    return true;
}

RemoteMicFeed::Stats RemoteMicFeed::stats() const
{
    Stats stats;
    stats.started = m_statsStarted.load(std::memory_order_relaxed);
    stats.fillFrames = m_statsFill.load(std::memory_order_relaxed);
    stats.ratio = m_statsRatio.load(std::memory_order_relaxed);
    stats.underflows = m_statsUnderflows.load(std::memory_order_relaxed);
    stats.overflows = m_statsOverflows.load(std::memory_order_relaxed);
    stats.droppedFrames = m_droppedFrames.load(std::memory_order_relaxed);
    stats.changes = m_statsChanges.load(std::memory_order_relaxed);
    return stats;
}

// ============================================================================
// RemoteMicEncoder
// ============================================================================

struct RemoteMicEncoder::State {
    OpusEncoder* encoder = nullptr;
    std::vector<unsigned char> payload =
        std::vector<unsigned char>(static_cast<size_t>(OpusAudioCodecConfig::kMaxPayloadBytes));
    ~State()
    {
        if (encoder != nullptr) {
            opus_encoder_destroy(encoder);
        }
    }
};

RemoteMicEncoder::RemoteMicEncoder()
    : m_state(std::make_unique<State>())
{
    reset();
}

RemoteMicEncoder::~RemoteMicEncoder() = default;

void RemoteMicEncoder::reset()
{
    if (m_state->encoder != nullptr) {
        opus_encoder_destroy(m_state->encoder);
        m_state->encoder = nullptr;
    }
    int error = OPUS_OK;
    // Speech from a microphone: the VOIP application, whose SILK and hybrid
    // modes carry in-band FEC (LBRR). FEC is sent only when the encoder
    // expects loss, so it is told to expect some.
    OpusEncoder* encoder = opus_encoder_create(RemoteMicConfig::kSampleRate, 1,
                                               OPUS_APPLICATION_VOIP, &error);
    if (encoder == nullptr || error != OPUS_OK) {
        qCWarning(lcAudio) << "Remote microphone: the Opus encoder could not start:" << error;
        return;
    }
    opus_encoder_ctl(encoder, OPUS_SET_BITRATE(RemoteMicConfig::kOpusBitrate));
    opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(10));
    opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    m_state->encoder = encoder;
}

bool RemoteMicEncoder::isReady() const
{
    return m_state->encoder != nullptr;
}

QByteArray RemoteMicEncoder::encode(const float* mono, quint16 sequence, quint32 timestamp,
                                    quint32 ssrc)
{
    if (m_state->encoder == nullptr || mono == nullptr) {
        return {};
    }
    const opus_int32 bytes =
        opus_encode_float(m_state->encoder, mono, RemoteMicConfig::kOpusFrameSamples,
                          m_state->payload.data(),
                          static_cast<opus_int32>(m_state->payload.size()));
    if (bytes <= 0) {
        return {};
    }
    const QByteArray payload(reinterpret_cast<const char*>(m_state->payload.data()), bytes);
    return buildAudioRtp(RemoteMicConfig::kOpusPayloadType, sequence, timestamp, ssrc, payload);
}

// ============================================================================
// RemoteMicReceiver
// ============================================================================

struct RemoteMicReceiver::Decoder {
    OpusDecoder* opus = nullptr;
    ~Decoder()
    {
        if (opus != nullptr) {
            opus_decoder_destroy(opus);
        }
    }
};

RemoteMicReceiver::RemoteMicReceiver(RemoteMicFeed* feed, QObject* parent, Clock clock,
                                     Scheduler scheduler)
    : QObject(parent)
    , m_feed(feed)
    , m_clock(std::move(clock))
    , m_scheduler(std::move(scheduler))
    , m_decoder(std::make_unique<Decoder>())
    , m_pcm(static_cast<size_t>(kMaxOpusFrames))
{
    if (!m_clock) {
        m_clock = steadyMs;
    }
    if (!m_scheduler) {
        m_scheduler = [this](int ms, std::function<void()> fire) {
            QTimer::singleShot(ms, this, std::move(fire));
        };
    }
}

RemoteMicReceiver::~RemoteMicReceiver() = default;

qint64 RemoteMicReceiver::now() const
{
    return m_clock();
}

bool RemoteMicReceiver::start(quint32 ssrc, bool losslessNegotiated)
{
    stop();
    int error = OPUS_OK;
    m_decoder->opus = opus_decoder_create(RemoteMicConfig::kSampleRate, 1, &error);
    if (m_decoder->opus == nullptr || error != OPUS_OK) {
        m_decoder->opus = nullptr;
        qCWarning(lcAudio) << "Remote microphone: the Opus decoder could not start:" << error;
        return false;
    }
    m_ssrc = ssrc;
    m_lossless = losslessNegotiated;
    m_haveSequence = false;
    m_lastOpusFrames = RemoteMicConfig::kOpusFrameSamples;
    m_stats = {};
    m_running = true;
    m_lastAudioMs = now();
    return true;
}

void RemoteMicReceiver::stop()
{
    // The line ends: a key waiting on it is answered not ready.
    if (m_waitDone) {
        std::function<void(bool)> done = std::move(m_waitDone);
        m_waitDone = nullptr;
        ++m_waitGeneration;
        done(false);
    }
    if (m_starved) {
        m_starved = false;
        emit starved(false);
    }
    m_watching = false;
    ++m_starvationGeneration;
    m_starvationCheckPending = false;
    if (m_decoder->opus != nullptr) {
        opus_decoder_destroy(m_decoder->opus);
        m_decoder->opus = nullptr;
    }
    m_running = false;
    m_ssrc = 0;
}

void RemoteMicReceiver::submit(const QByteArray& packet)
{
    if (!m_running) {
        return;
    }
    const int payloadType = audioRtpPayloadType(packet);
    const bool opus = payloadType == RemoteMicConfig::kOpusPayloadType;
    const bool l16 = payloadType == PcmAudioCodecConfig::kPayloadType && m_lossless;
    AudioRtpPacket parsed;
    if ((!opus && !l16)
        || parseAudioRtp(packet, payloadType, parsed) != OpusAudioCodecStatus::Accepted
        || parsed.ssrc != m_ssrc) {
        ++m_stats.rejectedPackets;
        return;
    }
    // Sequence order: a packet behind the stream (reordered too late, or a
    // repeat) is dropped; a gap ahead is concealed before this packet.
    int missing = 0;
    if (m_haveSequence) {
        const qint16 delta = static_cast<qint16>(parsed.sequence - m_expectedSequence);
        if (delta < 0) {
            ++m_stats.latePackets;
            return;
        }
        missing = delta;
    }
    ++m_stats.accepted;
    const quint64 writtenBefore = m_stats.framesWritten;
    if (opus) {
        decodeOpus(parsed.payload, missing);
    } else {
        decodeL16(packet, missing);
    }
    m_haveSequence = true;
    m_expectedSequence = static_cast<quint16>(parsed.sequence + 1);

    // Audio arrived: a starvation ends, and the next check runs from now.
    m_lastAudioMs = now();
    if (m_starved) {
        m_starved = false;
        emit starved(false);
    }
    if (m_watching && !m_starvationCheckPending) {
        scheduleStarvationCheck(RemoteMicConfig::kStarvationMs);
    }
    if (m_stats.framesWritten != writtenBefore) {
        checkReady();
    }
}

void RemoteMicReceiver::decodeOpus(const QByteArray& payload, int missing)
{
    OpusDecoder* decoder = m_decoder->opus;
    if (decoder == nullptr) {
        return;
    }
    const auto* data = reinterpret_cast<const unsigned char*>(payload.constData());
    const auto size = static_cast<opus_int32>(payload.size());
    if (missing > 0) {
        if (static_cast<qint64>(missing) * m_lastOpusFrames > kMaxConcealFrames) {
            // Longer than the buffer's target: nothing inserted, and the
            // decoder starts again with this packet.
            ++m_stats.longGaps;
            opus_decoder_ctl(decoder, OPUS_RESET_STATE);
        } else {
            // All but the last lost packet by loss concealment...
            for (int lost = 0; lost < missing - 1; ++lost) {
                const int frames = opus_decode_float(decoder, nullptr, 0, m_pcm.data(),
                                                     m_lastOpusFrames, 0);
                if (frames > 0) {
                    ++m_stats.concealedPackets;
                    writeAudio(m_pcm.data(), frames);
                }
            }
            // ...and the one just before this packet from this packet's
            // in-band FEC. The encoder codes FEC only for frames its voice
            // detector calls active; for any other the decoder conceals.
            const bool carriesFec = opusPacketCarriesFec(payload);
            const int frames =
                opus_decode_float(decoder, data, size, m_pcm.data(), m_lastOpusFrames, 1);
            if (frames > 0) {
                if (carriesFec) {
                    ++m_stats.recoveredPackets;
                } else {
                    ++m_stats.concealedPackets;
                }
                writeAudio(m_pcm.data(), frames);
            }
        }
    }
    const int frames = opus_decode_float(decoder, data, size, m_pcm.data(), kMaxOpusFrames, 0);
    if (frames <= 0) {
        ++m_stats.rejectedPackets;
        return;
    }
    m_lastOpusFrames = frames;
    ++m_stats.decodedPackets;
    writeAudio(m_pcm.data(), frames);
}

void RemoteMicReceiver::decodeL16(const QByteArray& packet, int missing)
{
    const PcmRtpDecodeResult decoded = decodeL16Rtp(packet, m_ssrc);
    if (decoded.status != OpusAudioCodecStatus::Accepted) {
        ++m_stats.rejectedPackets;
        return;
    }
    constexpr int kFrames = PcmAudioCodecConfig::kPacketFrames;
    if (missing > 0) {
        if (static_cast<qint64>(missing) * kFrames > kMaxConcealFrames) {
            ++m_stats.longGaps;
        } else {
            // A lost lossless packet is 4 ms of silence.
            std::fill(m_pcm.begin(), m_pcm.begin() + kFrames, 0.0f);
            for (int lost = 0; lost < missing; ++lost) {
                ++m_stats.concealedPackets;
                writeAudio(m_pcm.data(), kFrames);
            }
        }
    }
    // The window sends its microphone in both channels; mono is their mean.
    for (int i = 0; i < kFrames; ++i) {
        m_pcm[static_cast<size_t>(i)] = 0.5f
            * (decoded.pcmInterleaved.at(2 * i) + decoded.pcmInterleaved.at(2 * i + 1));
    }
    ++m_stats.decodedPackets;
    writeAudio(m_pcm.data(), kFrames);
}

void RemoteMicReceiver::writeAudio(const float* mono, int frames)
{
    // Fix wave C2: one writer at a time. Another device's line is decoded
    // (its starvation and its stream stay tracked) but never reaches the
    // transmitter's feed.
    if (!m_feedWriter) {
        return;
    }
    if (m_feed != nullptr && m_feed->write(mono, frames)) {
        m_stats.framesWritten += static_cast<quint64>(frames);
    }
}

void RemoteMicReceiver::awaitReady(std::function<void(bool ready)> done)
{
    m_waitDone = std::move(done);
    const quint64 generation = ++m_waitGeneration;
    checkReady();
    if (!m_waitDone || generation != m_waitGeneration) {
        return;
    }
    QPointer<RemoteMicReceiver> self(this);
    m_scheduler(RemoteMicConfig::kReadyDeadlineMs, [self, generation]() {
        if (self.isNull() || generation != self->m_waitGeneration || !self->m_waitDone) {
            return;
        }
        std::function<void(bool)> done = std::move(self->m_waitDone);
        self->m_waitDone = nullptr;
        done(false);
    });
}

void RemoteMicReceiver::cancelWait()
{
    m_waitDone = nullptr;
    ++m_waitGeneration;
}

void RemoteMicReceiver::checkReady()
{
    if (!m_waitDone || !m_feedWriter || m_feed == nullptr || !m_feed->inUse()
        || m_feed->framesSinceInUse() < RemoteMicConfig::kTargetDepthFrames) {
        return;
    }
    std::function<void(bool)> done = std::move(m_waitDone);
    m_waitDone = nullptr;
    ++m_waitGeneration;
    done(true);
}

void RemoteMicReceiver::setFeedWriter(bool writer)
{
    if (writer == m_feedWriter) {
        return;
    }
    m_feedWriter = writer;
    // A key waiting on this line fills from here.
    checkReady();
}

void RemoteMicReceiver::setWatching(bool watching)
{
    if (watching == m_watching) {
        return;
    }
    m_watching = watching;
    ++m_starvationGeneration;
    m_starvationCheckPending = false;
    if (watching) {
        // Measured from the later of the last audio and the start of the
        // watch.
        m_lastAudioMs = std::max(m_lastAudioMs, now());
        scheduleStarvationCheck(RemoteMicConfig::kStarvationMs);
    } else if (m_starved) {
        m_starved = false;
        emit starved(false);
    }
}

void RemoteMicReceiver::scheduleStarvationCheck(int ms)
{
    m_starvationCheckPending = true;
    const quint64 generation = m_starvationGeneration;
    QPointer<RemoteMicReceiver> self(this);
    m_scheduler(std::max(1, ms), [self, generation]() {
        if (self.isNull() || generation != self->m_starvationGeneration) {
            return;
        }
        self->m_starvationCheckPending = false;
        self->checkStarvation();
    });
}

void RemoteMicReceiver::checkStarvation()
{
    if (!m_watching || !m_running) {
        return;
    }
    const qint64 quiet = now() - m_lastAudioMs;
    if (quiet >= RemoteMicConfig::kStarvationMs) {
        if (!m_starved) {
            m_starved = true;
            qCInfo(lcAudio) << "Remote microphone: no audio for" << quiet << "ms while keyed";
            emit starved(true);
        }
        return;   // the next audio schedules the next check
    }
    scheduleStarvationCheck(static_cast<int>(RemoteMicConfig::kStarvationMs - quiet));
}

} // namespace NereusSDR
