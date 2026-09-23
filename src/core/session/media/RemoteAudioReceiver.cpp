// no-port-check: NereusSDR-original remote audio lifecycle and worker wiring.
#include "core/session/media/RemoteAudioReceiver.h"
#include "core/AudioEngine.h"
#include "core/session/media/AudioJitterBuffer.h"
#include "core/session/media/OpusAudioCodec.h"
#include "core/session/media/PcmAudioCodec.h"
#include "core/session/media/RemoteAudioRateMatcher.h"
#include "core/session/media/RtpReceptionStats.h"
#include <algorithm>
#include <cmath>
#include <atomic>
#include <bit>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace NereusSDR {
namespace {
// The default receiver clock: steady time since this process first read it,
// so values stay small enough to cross the control channel exactly.
qint64 defaultClockNs()
{
    static const std::chrono::steady_clock::time_point base = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - base).count();
}

// The packet shape each profile's context carries (R-R3-23).
constexpr qint64 kLosslessPacketDurationNs = qint64(PcmAudioCodecConfig::kPacketFrames)
    * 1'000'000'000 / PcmAudioCodecConfig::kSampleRate; // 4 ms
static_assert(kLosslessPacketDurationNs == 4'000'000, "a lossless packet is 4 ms");
static_assert(AudioJitterBuffer::kDefaultPacketFrames == OpusAudioCodecConfig::kFrameSamples,
              "the jitter queue's default shape is the Opus packet");
int packetFramesFor(RemoteAudioProfile profile)
{
    return profile == RemoteAudioProfile::Lossless ? PcmAudioCodecConfig::kPacketFrames
                                                   : AudioJitterBuffer::kDefaultPacketFrames;
}
qint64 packetDurationNsFor(RemoteAudioProfile profile)
{
    return profile == RemoteAudioProfile::Lossless ? kLosslessPacketDurationNs
                                                   : AudioJitterBuffer::kDefaultPacketDurationNs;
}

// The RTP boundary for one packet of the context's profile: what submit()
// needs before the packet may be queued.
struct AudioHeader {
    bool accepted = false;
    quint16 sequence = 0;
    quint32 timestamp = 0;
    qsizetype payloadBytes = 0;
};
AudioHeader inspectAudioRtp(const QByteArray& packet, quint32 ssrc, RemoteAudioProfile profile)
{
    AudioHeader header;
    if (profile == RemoteAudioProfile::Opus) {
        const auto opus = inspectOpusRtp(packet, ssrc);
        header.accepted = opus.status == OpusAudioCodecStatus::Accepted;
        header.sequence = opus.sequence;
        header.timestamp = opus.timestamp;
        header.payloadBytes = opus.payloadBytes;
        return header;
    }
    // Lossless: the payload type first, then the one L16 packet shape. The
    // payload's size is read from the header, not from a copy of it.
    if (audioRtpPayloadType(packet) != PcmAudioCodecConfig::kPayloadType) { return header; }
    AudioRtpView view;
    if (inspectAudioRtpHeader(packet, PcmAudioCodecConfig::kPayloadType, view)
            != OpusAudioCodecStatus::Accepted
        || view.ssrc != ssrc || view.payloadBytes != PcmAudioCodecConfig::kPayloadBytes) {
        return header;
    }
    header.accepted = true;
    header.sequence = view.sequence;
    header.timestamp = view.timestamp;
    header.payloadBytes = view.payloadBytes;
    return header;
}
}
struct RemoteAudioReceiver::Private {
    struct Packet { QByteArray bytes; quint32 timestamp; qint64 arrival; quint16 sequence; };
    AudioEngine* engine = nullptr; // owner stops/joins before engine destruction
    std::jthread worker;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Packet> incoming;
    std::atomic<bool> running{false};
    std::atomic<quint64> decoded{0};
    std::atomic<quint64> concealed{0};
    std::atomic<int> underflows{0};
    std::atomic<int> overflows{0};
    std::atomic<quint64> rejectedHeaders{0};
    std::atomic<quint64> lifetimeUnderflows{0};
    std::atomic<quint64> lifetimeOverflows{0};
    std::atomic<quint64> telemetryGeneration{0};
    std::atomic<quint64> telemetrySequence{0}; // owner lifecycle mutations
    std::atomic<quint64> accepted{0};
    std::atomic<quint64> late{0};
    std::atomic<quint64> invalid{0};
    std::atomic<quint64> duplicate{0};
    std::atomic<quint64> startDiscarded{0};
    std::atomic<quint64> receivedAudioPayloadBytes{0};
    std::atomic<quint64> deviceConsumedFrames{0};
    // -1 is deliberately distinct from a measured empty speaker queue.
    std::atomic<int> speakerQueuedFrames{-1};
    std::atomic<qint64> lastAdmittedPacketNs{0};
    std::atomic<qint64> lastDeviceProgressNs{0};
    std::atomic<bool> hasLastAdmittedPacket{false};
    std::atomic<bool> hasLastDeviceProgress{false};
    // RtpReceptionStats publication. reorderQueuedPackets uses the same -1
    // sentinel/live-gauge pattern as speakerQueuedFrames above.
    std::atomic<quint64> expectedPackets{0};
    std::atomic<quint64> missingPackets{0};
    std::atomic<bool> hasArrivalJitterMs{false};
    std::atomic<double> arrivalJitterMs{0.0};
    std::atomic<int> reorderQueuedPackets{-1};
    // Rate matcher ratio, published by the playback loop (R-R3-07). Like
    // reorderQueuedPackets it is reported only for a running context.
    std::atomic<bool> hasDriftRatio{false};
    std::atomic<double> driftRatio{1.0};
    bool overflow = false; // under mutex
    // Until the first packet is released for playback, a full arrival queue
    // drops its oldest packet instead of raising overflow. startBacklog
    // records such a drop for the worker's next batch. Both under mutex.
    bool startPhase = false;
    bool startBacklog = false;
    // The context's profile and its arrival bound: the jitter window in
    // packets (8 Opus, 80 lossless), so the bound is the same 320 ms for
    // both. Set by start() before the worker runs; read by submit() only
    // while running.
    std::atomic<RemoteAudioProfile> profile{RemoteAudioProfile::Opus};
    std::atomic<int> arrivalBoundPackets{
        AudioJitterBuffer::windowPackets(AudioJitterBuffer::kDefaultPacketDurationNs)};
    std::atomic<qint64> packetDurationNs{AudioJitterBuffer::kDefaultPacketDurationNs};
    quint32 ssrc = 0;
    quint64 generation = 0; // owner thread only
    // R-R3-35. The clock is set once at construction. The playout and
    // release points are published by the worker and read by telemetry();
    // the worker is never the device callback, so a short lock is fine.
    RemoteAudioReceiver::Clock clock;
    qint64 now() const { return clock(); }
    std::mutex pointsMutex;
    std::optional<RemoteAudioPlayoutPoint> playout;
    std::optional<RemoteAudioReleasePoint> release;
    void clearPoints()
    {
        std::lock_guard<std::mutex> lock(pointsMutex);
        playout.reset();
        release.reset();
    }
};

qint64 RemoteAudioPlayoutPoint::playoutNs() const
{
    // Doubled frame counts keep half a callback exact.
    const qint64 halfFrames = 2 * (qint64(matcherFillFrames) + qint64(speakerQueuedFrames)
                                   + qint64(pipelineDelayFrames))
        + qint64(callbackFrames);
    return measuredNs + halfFrames * 1'000'000'000 / (2 * PcmAudioCodecConfig::kSampleRate)
        + deviceLatencyNs.value_or(0);
}

qint64 RemoteAudioPlayoutPoint::matcherStretchNs() const
{
    if (!std::isfinite(matcherRatio) || matcherRatio <= 0.0 || matcherRatio == 1.0) {
        return 0;
    }
    // The play time ahead of measuredNs that the rate matcher made: all of
    // it but the codec's delay, which lies before the matcher.
    const qint64 madeNs = playoutNs() - measuredNs
        - qint64(codecDelayFrames) * 1'000'000'000 / PcmAudioCodecConfig::kSampleRate;
    return std::llround(double(std::max<qint64>(0, madeNs)) * (1.0 - 1.0 / matcherRatio));
}

qint64 RemoteAudioPlayoutPoint::accuracyNs() const
{
    return (qint64(callbackFrames) * 1'000'000'000 + 2 * PcmAudioCodecConfig::kSampleRate - 1)
            / (2 * PcmAudioCodecConfig::kSampleRate)
        + (readWindowNs + 1) / 2;
}

RemoteAudioReceiver::RemoteAudioReceiver(AudioEngine* engine, QObject* parent, Clock clock)
    : QObject(parent), d(std::make_unique<Private>())
{
    d->engine = engine;
    d->clock = clock ? std::move(clock) : Clock(defaultClockNs);
}
qint64 RemoteAudioReceiver::nowNs() const { return d->now(); }
RemoteAudioReceiver::~RemoteAudioReceiver() { stop(); }
bool RemoteAudioReceiver::isRunning() const { return d->running.load(); }
RemoteAudioProfile RemoteAudioReceiver::profile() const { return d->profile.load(); }
quint64 RemoteAudioReceiver::decodedPackets() const { return d->decoded.load(); }
quint64 RemoteAudioReceiver::concealedPackets() const { return d->concealed.load(); }
int RemoteAudioReceiver::rateMatcherUnderflows() const { return d->underflows.load(); }
int RemoteAudioReceiver::rateMatcherOverflows() const { return d->overflows.load(); }

RemoteAudioReceiverTelemetry RemoteAudioReceiver::telemetry() const
{
    const auto ageMs = [this](qint64 eventNs) {
        return std::max<qint64>(0, (d->now() - eventNs) / 1'000'000);
    };
    // A lifecycle change can race a reader. The owner brackets stop and a
    // successful-start reset with an odd sequence. Bounded retries avoid
    // attaching a retired context's counters to the next context without
    // making packet submission, the worker, or the device callback wait.
    for (int attempt = 0; attempt < 3; ++attempt) {
        const quint64 sequence = d->telemetrySequence.load();
        if (sequence & 1) { continue; }
        const quint64 generation = d->telemetryGeneration.load();
        RemoteAudioReceiverTelemetry snapshot;
        snapshot.generation = generation;
        snapshot.running = d->running.load();
        snapshot.acceptedPackets = d->accepted.load();
        snapshot.decodedPackets = d->decoded.load();
        snapshot.concealedPackets = d->concealed.load();
        snapshot.latePackets = d->late.load();
        snapshot.invalidPackets = d->invalid.load();
        snapshot.duplicatePackets = d->duplicate.load();
        snapshot.rejectedHeaders = d->rejectedHeaders.load();
        snapshot.startDiscardedPackets = d->startDiscarded.load();
        snapshot.receivedAudioPayloadBytes = d->receivedAudioPayloadBytes.load();
        snapshot.deviceConsumedFrames = d->deviceConsumedFrames.load();
        snapshot.underflows = d->underflows.load();
        snapshot.overflows = d->overflows.load();
        snapshot.lifetimeUnderflows = d->lifetimeUnderflows.load();
        snapshot.lifetimeOverflows = d->lifetimeOverflows.load();
        if (d->hasLastAdmittedPacket.load()) {
            snapshot.lastAdmittedPacketAgeMs = ageMs(d->lastAdmittedPacketNs.load());
        }
        if (d->hasLastDeviceProgress.load()) {
            snapshot.lastDeviceProgressAgeMs = ageMs(d->lastDeviceProgressNs.load());
        }
        snapshot.expectedPackets = d->expectedPackets.load();
        snapshot.missingPackets = d->missingPackets.load();
        if (d->hasArrivalJitterMs.load()) {
            snapshot.arrivalJitterMs = d->arrivalJitterMs.load();
        }
        if (snapshot.running) {
            const int queuedFrames = d->speakerQueuedFrames.load();
            if (queuedFrames >= 0) {
                snapshot.speakerQueuedMs = double(queuedFrames) / 48.0;
            }
            const int reorderQueued = d->reorderQueuedPackets.load();
            if (reorderQueued >= 0) {
                snapshot.reorderQueuedMs = double(reorderQueued)
                    * (double(d->packetDurationNs.load()) / 1'000'000.0);
            }
            if (d->hasDriftRatio.load()) {
                snapshot.driftRatio = d->driftRatio.load();
            }
            std::lock_guard<std::mutex> lock(d->pointsMutex);
            snapshot.playout = d->playout;
            snapshot.release = d->release;
        }
        if (d->telemetrySequence.load() == sequence) {
            return snapshot;
        }
    }
    // A snapshot sampled in the middle of an owner lifecycle transition is
    // intentionally unavailable instead of combining observations from two
    // contexts. The next 1 Hz collection tick obtains the settled snapshot.
    RemoteAudioReceiverTelemetry unavailable;
    unavailable.generation = d->telemetryGeneration.load();
    return unavailable;
}

void RemoteAudioReceiver::stop()
{
    d->telemetrySequence.fetch_add(1);
    d->running.store(false);
    d->speakerQueuedFrames.store(-1);
    d->reorderQueuedPackets.store(-1);
    ++d->generation;
    if (d->worker.joinable()) {
        d->worker.request_stop();
        d->wake.notify_one();
        d->worker.join();
    }
    // After the join, so a worker's last publication cannot outlive stop().
    d->hasDriftRatio.store(false);
    d->clearPoints();
    {
        std::lock_guard<std::mutex> lock(d->mutex);
        d->incoming.clear();
        d->overflow = false;
        d->startPhase = false;
        d->startBacklog = false;
    }
    if (d->engine) { d->engine->endRemotePlayback(); }
    d->telemetrySequence.fetch_add(1);
}

bool RemoteAudioReceiver::start(quint32 ssrc, quint32 firstTimestamp, RemoteAudioProfile profile)
{
    stop();
    QString error;
    if (!d->engine || !d->engine->beginRemotePlayback(&error)) {
        emit errorOccurred(error.isEmpty() ? QStringLiteral("Speaker playback is unavailable") : error,
                            Fault::SpeakerOpenFailed);
        return false;
    }
    d->telemetrySequence.fetch_add(1);
    d->ssrc = ssrc;
    d->profile.store(profile);
    d->packetDurationNs.store(packetDurationNsFor(profile));
    d->arrivalBoundPackets.store(AudioJitterBuffer::windowPackets(packetDurationNsFor(profile)));
    d->decoded.store(0);
    d->concealed.store(0);
    d->underflows.store(0);
    d->overflows.store(0);
    d->rejectedHeaders.store(0);
    d->accepted.store(0);
    d->late.store(0);
    d->invalid.store(0);
    d->duplicate.store(0);
    d->startDiscarded.store(0);
    d->receivedAudioPayloadBytes.store(0);
    d->deviceConsumedFrames.store(0);
    d->speakerQueuedFrames.store(-1);
    d->hasLastAdmittedPacket.store(false);
    d->hasLastDeviceProgress.store(false);
    d->lastAdmittedPacketNs.store(0);
    d->lastDeviceProgressNs.store(0);
    d->expectedPackets.store(0);
    d->missingPackets.store(0);
    d->hasArrivalJitterMs.store(false);
    d->arrivalJitterMs.store(0.0);
    d->reorderQueuedPackets.store(-1);
    d->hasDriftRatio.store(false);
    d->driftRatio.store(1.0);
    d->clearPoints();
    d->telemetryGeneration.fetch_add(1);
    {
        std::lock_guard<std::mutex> lock(d->mutex);
        d->startPhase = true;
        d->startBacklog = false;
    }
    d->running.store(true);
    d->telemetrySequence.fetch_add(1);
    const quint64 generation = d->generation;
    d->worker = std::jthread([this, ssrc, firstTimestamp, generation, profile](std::stop_token stop) {
        const bool lossless = profile == RemoteAudioProfile::Lossless;
        const int packetFrames = packetFramesFor(profile);
        AudioJitterBuffer jitter(packetFrames, packetDurationNsFor(profile));
        jitter.reset(firstTimestamp);
        // Only an Opus context needs the Opus decoder; a lossless packet is
        // read directly, and a lost one is silence.
        std::optional<OpusAudioDecoder> decoder;
        if (!lossless) { decoder.emplace(); }
        const QVector<float> silence(packetFrames * PcmAudioCodecConfig::kChannels, 0.0f);
        // One packet to PCM: the profile's decoder for a present packet, the
        // Opus concealment or lossless silence for a missing one. Empty on
        // a decode failure.
        const auto decodePacket = [&](const QByteArray& packet) -> QVector<float> {
            if (lossless) {
                if (packet.isEmpty()) { return silence; }
                const PcmRtpDecodeResult audio = decodeL16Rtp(packet, ssrc);
                return audio.status == OpusAudioCodecStatus::Accepted
                    ? audio.pcmInterleaved : QVector<float>{};
            }
            const auto audio = packet.isEmpty() ? decoder->decodeMissing()
                                                : decoder->decodeRtp(packet, ssrc);
            return audio.status == OpusAudioCodecStatus::Accepted
                    || audio.status == OpusAudioCodecStatus::Concealed
                ? audio.pcmInterleaved : QVector<float>{};
        };
        RemoteAudioRateMatcher matcher;
        // Named apart from publishMatcherStats()'s local `stats` below.
        RtpReceptionStats receptionStats;
        const qint64 startedAt = d->now();
        qint64 lastPacket = startedAt;
        qint64 previousArrival = 0;
        qint64 maxArrivalGap = 0;
        qint64 previousWake = startedAt;
        qint64 maxWakeGap = 0;
        quint64 accepted = 0, late = 0, invalid = 0, duplicate = 0;
        quint64 publishedUnderflows = 0, publishedOverflows = 0;
        const auto publishSpeakerQueue = [&](const std::optional<IAudioBus::OutputPacing>& pacing) {
            d->speakerQueuedFrames.store(pacing ? pacing->queuedFrames : -1);
        };
        const auto publishMatcherStats = [&] {
            const auto stats = matcher.stats();
            const quint64 underflows = std::max(0, stats.underflows);
            const quint64 overflows = std::max(0, stats.overflows);
            d->underflows.store(stats.underflows);
            d->overflows.store(stats.overflows);
            if (underflows > publishedUnderflows) {
                d->lifetimeUnderflows.fetch_add(underflows - publishedUnderflows);
                publishedUnderflows = underflows;
            }
            if (overflows > publishedOverflows) {
                d->lifetimeOverflows.fetch_add(overflows - publishedOverflows);
                publishedOverflows = overflows;
            }
            return stats;
        };
        const auto notify = [&](const QString& reason, Fault fault, bool fatal = false) {
            d->speakerQueuedFrames.store(-1);
            d->reorderQueuedPackets.store(-1);
            d->running.store(false);
            const auto stats = publishMatcherStats();
            const auto pacing = d->engine->remotePlaybackPacing();
            const qint64 now = d->now();
            // Bounded, restart-only diagnostics distinguish capture/network
            // loss from a stalled consumer without logging media or secrets.
            const QString detail = reason + QStringLiteral(
                " [ageMs=%1 accepted=%2 decoded=%3 plc=%4 late=%5 invalid=%6 duplicate=%7"
                " rejectedHeaders=%8 lastPacketMs=%9 maxArrivalGapMs=%10 maxWakeGapMs=%11"
                " jitterPackets=%12 ratio=%13 fill=%14 callback=%15 deviceQueued=%16"
                " startDiscarded=%17]")
                .arg((now - startedAt) / 1'000'000).arg(accepted)
                .arg(d->decoded.load()).arg(d->concealed.load()).arg(late).arg(invalid)
                .arg(duplicate).arg(d->rejectedHeaders.load())
                .arg((now - lastPacket) / 1'000'000).arg(maxArrivalGap / 1'000'000)
                .arg(maxWakeGap / 1'000'000).arg(jitter.queuedPackets())
                .arg(stats.currentRatio, 0, 'f', 7).arg(stats.ringFillFrames)
                .arg(pacing ? pacing->callbackFrames : 0)
                .arg(pacing ? pacing->queuedFrames : 0)
                .arg(d->startDiscarded.load());
            QMetaObject::invokeMethod(this, [this, generation, detail, fault, fatal] {
                if (d->generation != generation) { return; }
                if (fatal) { emit errorOccurred(detail, fault); }
                else { emit restartRequested(detail, fault); }
            }, Qt::QueuedConnection);
        };
        const auto initialPacing = d->engine->remotePlaybackPacing();
        if (!initialPacing) {
            notify(QStringLiteral("Speaker device timing is unavailable"),
                   Fault::SpeakerTimingUnavailable, true);
            return;
        }
        publishSpeakerQueue(initialPacing);
        const int initialTarget = std::max(960, initialPacing->callbackFrames + 480);
        if (initialTarget + 480 > initialPacing->capacityFrames) {
            notify(QStringLiteral("Speaker callback exceeds remote playback capacity"),
                   Fault::SpeakerCallbackTooLarge, true);
            return;
        }
        // WDSP starts half full. Preserve the default 90 ms reserve when a
        // larger selected device quantum transfers extra frames into the
        // speaker ring at startup. This is queue sizing, not a change to the
        // resampler feedback or its continuous interpolation.
        const int deviceHighWater = ((initialTarget + 479) / 480) * 480;
        const int matchRingFrames = 8640 + 2 * std::max(0, deviceHighWater - 960);
        if ((decoder && !decoder->isReady())
            || !matcher.configure(packetFrames, 480, matchRingFrames)) {
            notify(QStringLiteral("Could not initialize the remote audio decoder or rate matcher"),
                   Fault::DecoderUnavailable, true);
            return;
        }
        bool playing = false;
        // Connect-time start: nothing has been released for playback yet.
        // The receiver has no starting-fill constant of its own (the jitter
        // hold, AudioJitterBuffer::kHoldNs, is the start buffer), so a backlog
        // keeps only its newest packet. A burst within the arrival bound and
        // the jitter window is kept whole, as before.
        bool released = false;
        bool admittedAny = false;
        const auto trimStartBacklog = [&](std::deque<Private::Packet>& batch, bool backlog) {
            // Packets behind the anchor or off the packet grid keep their
            // normal Late/Invalid classification in insert().
            const auto aheadOf = [&](const Private::Packet& packet) -> std::optional<qint32> {
                const qint32 delta = std::bit_cast<qint32>(
                    quint32(packet.timestamp - jitter.nextTimestamp()));
                if (delta < 0 || delta % jitter.packetFrames() != 0) {
                    return std::nullopt;
                }
                return delta / jitter.packetFrames();
            };
            std::optional<std::size_t> newest, oldest;
            qint32 newestAhead = -1, oldestAhead = 0;
            std::size_t playable = 0;
            for (std::size_t i = 0; i < batch.size(); ++i) {
                const auto ahead = aheadOf(batch[i]);
                if (!ahead) { continue; }
                ++playable;
                if (*ahead > newestAhead) { newest = i; newestAhead = *ahead; }
                if (!oldest || *ahead < oldestAhead) { oldest = i; oldestAhead = *ahead; }
            }
            if (!newest) { return; }
            if (backlog || newestAhead >= jitter.maxPackets()) {
                // Keep only the newest packet and restart the reorder window
                // and its loss accounting from it: the dropped packets were
                // never heard, so they are neither a gap nor a stream break.
                // Packets already in the jitter queue were counted as
                // admitted; they are discarded here instead, so move them to
                // the start-discard count and "admitted" keeps only packets
                // that can still play (fix wave M1).
                const auto queued = quint64(jitter.queuedPackets());
                accepted -= std::min(accepted, queued);
                d->accepted.fetch_sub(std::min(d->accepted.load(), queued));
                d->startDiscarded.fetch_add(queued + (playable - 1));
                const quint32 anchor = batch[*newest].timestamp;
                std::deque<Private::Packet> kept;
                for (std::size_t i = 0; i < batch.size(); ++i) {
                    if (i == *newest || !aheadOf(batch[i])) { kept.push_back(std::move(batch[i])); }
                }
                batch.swap(kept);
                jitter.reset(anchor);
                receptionStats.reset();
            } else if (!admittedAny) {
                jitter.reset(batch[*oldest].timestamp);
            }
        };
        // R-R3-35: the end RTP time of the newest packet in the rate
        // matcher, for the playout point, and the newest release not yet
        // published. Both are published together once per wake, so the
        // points lock is never taken per packet (250 a second lossless).
        std::optional<quint32> pushedEnd;
        std::optional<RemoteAudioReleasePoint> unpublishedRelease;
        const auto noteReleased = [&](quint32 timestamp, bool concealed, qint64 atNs) {
            pushedEnd = timestamp + quint32(packetFrames);
            if (!concealed) {
                unpublishedRelease = RemoteAudioReleasePoint{*pushedEnd, atNs};
            }
        };
        // Fixed delays a sample passes through after the matcher fill and
        // the speaker queue say it is heard: the codec's own delay (Opus
        // only) and the rate matcher's filter.
        const int codecDelayFrames = lossless ? 0 : opusCodecDelayFrames();
        const int pipelineDelayFrames = RemoteAudioRateMatcher::kFilterDelayFrames
            + codecDelayFrames;
        quint64 lastDeviceFrames = 0;
        const quint64 deviceConsumedBase = initialPacing->consumedFrames;
        quint64 telemetryDeviceFrames = deviceConsumedBase;
        qint64 lastDeviceProgress = lastPacket;
        while (!stop.stop_requested()) {
            std::deque<Private::Packet> incoming;
            bool overflow = false;
            bool startBacklog = false;
            {
                std::unique_lock<std::mutex> lock(d->mutex);
                d->wake.wait_for(lock, std::chrono::milliseconds(2), [this, &stop] {
                    return stop.stop_requested() || !d->incoming.empty() || d->overflow;
                });
                incoming.swap(d->incoming);
                overflow = d->overflow;
                d->overflow = false;
                startBacklog = d->startBacklog;
                d->startBacklog = false;
            }
            if (stop.stop_requested()) { break; }
            const qint64 wakeAt = d->now();
            maxWakeGap = std::max(maxWakeGap, wakeAt - previousWake);
            previousWake = wakeAt;
            if (overflow) {
                notify(QStringLiteral("Remote audio arrival queue exceeded its latency bound"),
                       Fault::ArrivalBurst);
                return;
            }
            if (!released && !incoming.empty()) {
                trimStartBacklog(incoming, startBacklog);
            }
            for (const auto& packet : incoming) {
                if (previousArrival != 0) {
                    maxArrivalGap = std::max(maxArrivalGap, packet.arrival - previousArrival);
                }
                previousArrival = packet.arrival;
                const auto admitted = jitter.insert(packet.bytes, packet.timestamp, packet.arrival);
                if (admitted == AudioJitterBuffer::Admission::OutsideWindow) {
                    notify(QStringLiteral("Remote audio needs a fresh context after a stream gap"),
                           Fault::StreamGap);
                    return;
                }
                switch (admitted) {
                case AudioJitterBuffer::Admission::Accepted:
                    admittedAny = true;
                    ++accepted;
                    ++d->accepted;
                    lastPacket = packet.arrival;
                    d->lastAdmittedPacketNs.store(packet.arrival);
                    d->hasLastAdmittedPacket.store(true);
                    receptionStats.observe(packet.sequence, packet.timestamp, packet.arrival);
                    break;
                case AudioJitterBuffer::Admission::Late:
                    ++late;
                    ++d->late;
                    receptionStats.observe(packet.sequence, packet.timestamp, packet.arrival);
                    break;
                case AudioJitterBuffer::Admission::Invalid:
                    ++invalid;
                    ++d->invalid;
                    break;
                case AudioJitterBuffer::Admission::Duplicate:
                    ++duplicate;
                    ++d->duplicate;
                    break;
                case AudioJitterBuffer::Admission::OutsideWindow: break;
                }
            }
            d->expectedPackets.store(receptionStats.expectedPackets());
            d->missingPackets.store(receptionStats.missingPackets());
            if (const auto measuredJitterMs = receptionStats.jitterMs()) {
                d->arrivalJitterMs.store(*measuredJitterMs);
                d->hasArrivalJitterMs.store(true);
            }
            const qint64 now = d->now();
            if (now - lastPacket > 500'000'000) {
                notify(QStringLiteral("Remote audio had no admitted/playable packets for 500 ms"),
                       Fault::NoPackets);
                return;
            }
            // At most the bounded jitter window per wake. A long scheduling
            // stall causes a fresh context, never an unbounded catch-up burst.
            for (int i = 0; i < jitter.maxPackets(); ++i) {
                // A network burst stays in the bounded jitter queue until
                // the device has drained room. Reserve two input packets, a
                // conservative bound above WDSP's maximum resampled block;
                // never let its drop-oldest overflow repair handle a burst.
                const auto room = matcher.stats();
                if (room.ringCapacityFrames - room.ringFillFrames < 2 * packetFrames) { break; }
                const auto frame = jitter.takeReady(now);
                if (!frame) { break; }
                const QVector<float> audio = decodePacket(frame->packet);
                if (audio.isEmpty() || !matcher.push(audio)) {
                    notify(QStringLiteral("Remote audio decode failed"), Fault::DecodeFailed);
                    return;
                }
                if (frame->concealed()) { ++d->concealed; }
                else { ++d->decoded; }
                noteReleased(frame->timestamp, frame->concealed(), now);
                playing = true;
                if (!released) {
                    // Playback has begun; the normal overflow rule applies.
                    released = true;
                    std::lock_guard<std::mutex> lock(d->mutex);
                    d->startPhase = false;
                }
            }
            d->reorderQueuedPackets.store(jitter.queuedPackets());
            if (!playing) {
                publishSpeakerQueue(d->engine->remotePlaybackPacing());
                continue;
            }
            auto pacing = d->engine->remotePlaybackPacing();
            if (!pacing) {
                notify(QStringLiteral("Speaker device timing became unavailable"),
                       Fault::SpeakerTimingUnavailable, true);
                return;
            }
            if (pacing->consumedFrames != lastDeviceFrames) {
                lastDeviceFrames = pacing->consumedFrames;
                lastDeviceProgress = now;
                if (pacing->consumedFrames != telemetryDeviceFrames) {
                    telemetryDeviceFrames = pacing->consumedFrames;
                    d->deviceConsumedFrames.store(telemetryDeviceFrames - deviceConsumedBase);
                    d->lastDeviceProgressNs.store(now);
                    d->hasLastDeviceProgress.store(true);
                }
            } else if (now - lastDeviceProgress > 500'000'000) {
                notify(QStringLiteral("Speaker device stopped consuming audio"),
                       Fault::SpeakerStalled, true);
                return;
            }
            // Cover the selected callback quantum plus one worker block.
            // Default 128-frame callbacks need 20-30 ms queued; a user-selected
            // 2048-frame callback needs more so one callback cannot exhaust it.
            const int targetFrames = std::max(960, pacing->callbackFrames + 480);
            if (targetFrames + 480 > pacing->capacityFrames) {
                notify(QStringLiteral("Speaker callback exceeds remote playback capacity"),
                       Fault::SpeakerCallbackTooLarge, true);
                return;
            }
            // The timer only wakes us. Actual device queue consumption is the
            // output clock; at most one bounded ring can be replenished here.
            for (int i = 0; i < pacing->capacityFrames / 480
                 && pacing->queuedFrames < targetFrames; ++i) {
                // A packet may already be admitted yet remain behind its
                // per-arrival reorder hold while the independently clocked
                // speaker consumes the final usable matcher block. There is
                // no ordering benefit in retaining the exact expected packet
                // at that point. Never use this demand path for a missing head
                // or a future packet: those retain the normal PLC deadline.
                // One 40 ms Opus packet always covers an output block; 4 ms
                // lossless packets may take several, each the exact
                // expected packet, bounded by the jitter window.
                for (int early = 0; early < jitter.maxPackets()
                     && !matcher.canTakeWithoutUnderflow(); ++early) {
                    const auto present = jitter.takeExpectedPresentEarly();
                    if (!present) { break; }
                    const QVector<float> audio = decodePacket(present->packet);
                    if (audio.isEmpty() || !matcher.push(audio)) {
                        notify(QStringLiteral("Remote audio decode failed"), Fault::DecodeFailed);
                        return;
                    }
                    ++d->decoded;
                    noteReleased(present->timestamp, false, d->now());
                }
                const QVector<float> pcm = matcher.take();
                if (pcm.size() != 960 || !d->engine->writeRemotePlayback(pcm)) {
                    notify(QStringLiteral("Could not write remote audio to the speaker device"),
                           Fault::SpeakerWriteFailed, true);
                    return;
                }
                pacing = d->engine->remotePlaybackPacing();
                if (!pacing) { break; }
            }
            const auto stats = publishMatcherStats();
            // The ratio the fault text reports, published every playback
            // iteration so a soak can record it without a fault (R-R3-07).
            // Only once rmatch measures it: before its 3.0 s startup delay
            // the ratio is the initial 1.0, which would read as a measured
            // zero drift, so drift stays absent until then (review minor 3).
            if (stats.controlActive) {
                d->driftRatio.store(stats.currentRatio);
                d->hasDriftRatio.store(true);
            }
            // The final pacing read in every playback iteration observes the
            // queue after bounded replenishment. It remains worker-only and
            // does not participate in device callback scheduling.
            // R-R3-35: the clock is read on both sides of the queue read, so
            // a worker preempted between them widens the accuracy instead of
            // moving the figure.
            const qint64 readStart = d->now();
            const auto finalPacing = d->engine->remotePlaybackPacing();
            const qint64 readEnd = d->now();
            publishSpeakerQueue(finalPacing);
            // R-R3-35: the newest matched sample is heard after the matcher
            // fill and the speaker queue have played, then the device.
            if ((finalPacing && pushedEnd) || unpublishedRelease) {
                std::lock_guard<std::mutex> lock(d->pointsMutex);
                if (finalPacing && pushedEnd) {
                    RemoteAudioPlayoutPoint point;
                    point.rtpTimestamp = *pushedEnd;
                    point.measuredNs = readStart + (readEnd - readStart) / 2;
                    point.matcherFillFrames = std::max(0, stats.ringFillFrames);
                    point.speakerQueuedFrames = std::max(0, finalPacing->queuedFrames);
                    point.deviceLatencyNs = finalPacing->deviceLatencyNs;
                    point.pipelineDelayFrames = pipelineDelayFrames;
                    point.callbackFrames = std::max(0, finalPacing->callbackFrames);
                    point.readWindowNs = readEnd - readStart;
                    // Follow-up item 5: while rmatch corrects, its ratio
                    // is part of the delay (see matcherStretchNs).
                    point.codecDelayFrames = codecDelayFrames;
                    point.matcherRatio = stats.currentRatio;
                    d->playout = point;
                }
                if (unpublishedRelease) { d->release = *unpublishedRelease; }
            }
            unpublishedRelease.reset();
            if (stats.underflows || stats.overflows) {
                notify(QStringLiteral("Remote audio exceeded its continuous clock buffer (%1 underflows, %2 overflows)")
                    .arg(stats.underflows).arg(stats.overflows), Fault::ClockBuffer);
                return;
            }
        }
    });
    return true;
}

void RemoteAudioReceiver::submit(const QByteArray& packet)
{
    if (!isRunning()) { return; }
    const AudioHeader header = inspectAudioRtp(packet, d->ssrc, d->profile.load());
    if (!header.accepted) {
        ++d->rejectedHeaders;
        return;
    }
    // Traffic is measured at the valid RTP/profile boundary, before the
    // bounded process-local arrival queue can discard a burst. A duplicate
    // is still received traffic even if jitter ordering later rejects it.
    d->receivedAudioPayloadBytes.fetch_add(quint64(header.payloadBytes));
    {
        std::lock_guard<std::mutex> lock(d->mutex);
        // Bounded in time: the jitter window's 320 ms of packets.
        const bool full = d->incoming.size()
            >= static_cast<std::size_t>(d->arrivalBoundPackets.load());
        if (full && !d->startPhase) { d->overflow = true; }
        else {
            if (full) {
                // A connect-time backlog: nothing has been heard yet, so the
                // oldest queued packet is the least useful one to keep.
                d->incoming.pop_front();
                ++d->startDiscarded;
                d->startBacklog = true;
            }
            d->incoming.push_back({packet, header.timestamp, d->now(), header.sequence});
        }
    }
    d->wake.notify_one();
}
} // namespace NereusSDR
