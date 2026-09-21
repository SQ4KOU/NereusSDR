// no-port-check: NereusSDR-original remote audio lifecycle and worker wiring.
#include "core/session/media/RemoteAudioReceiver.h"
#include "core/AudioEngine.h"
#include "core/session/media/AudioJitterBuffer.h"
#include "core/session/media/OpusAudioCodec.h"
#include "core/session/media/RemoteAudioRateMatcher.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace NereusSDR {
namespace {
qint64 monotonicNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
}
struct RemoteAudioReceiver::Private {
    struct Packet { QByteArray bytes; quint32 timestamp; qint64 arrival; };
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
    bool overflow = false; // under mutex
    quint32 ssrc = 0;
    quint64 generation = 0; // owner thread only
};
RemoteAudioReceiver::RemoteAudioReceiver(AudioEngine* engine, QObject* parent)
    : QObject(parent), d(std::make_unique<Private>()) { d->engine = engine; }
RemoteAudioReceiver::~RemoteAudioReceiver() { stop(); }
bool RemoteAudioReceiver::isRunning() const { return d->running.load(); }
quint64 RemoteAudioReceiver::decodedPackets() const { return d->decoded.load(); }
quint64 RemoteAudioReceiver::concealedPackets() const { return d->concealed.load(); }
int RemoteAudioReceiver::rateMatcherUnderflows() const { return d->underflows.load(); }
int RemoteAudioReceiver::rateMatcherOverflows() const { return d->overflows.load(); }

void RemoteAudioReceiver::stop()
{
    ++d->generation;
    d->running.store(false);
    if (d->worker.joinable()) {
        d->worker.request_stop();
        d->wake.notify_one();
        d->worker.join();
    }
    {
        std::lock_guard<std::mutex> lock(d->mutex);
        d->incoming.clear();
        d->overflow = false;
    }
    if (d->engine) { d->engine->endRemotePlayback(); }
}

bool RemoteAudioReceiver::start(quint32 ssrc, quint32 firstTimestamp)
{
    stop();
    QString error;
    if (!d->engine || !d->engine->beginRemotePlayback(&error)) {
        emit errorOccurred(error.isEmpty() ? QStringLiteral("Speaker playback is unavailable") : error);
        return false;
    }
    d->ssrc = ssrc;
    d->decoded.store(0);
    d->concealed.store(0);
    d->underflows.store(0);
    d->overflows.store(0);
    d->rejectedHeaders.store(0);
    d->running.store(true);
    const quint64 generation = d->generation;
    d->worker = std::jthread([this, ssrc, firstTimestamp, generation](std::stop_token stop) {
        AudioJitterBuffer jitter;
        jitter.reset(firstTimestamp);
        OpusAudioDecoder decoder;
        RemoteAudioRateMatcher matcher;
        const qint64 startedAt = monotonicNs();
        qint64 lastPacket = startedAt;
        qint64 previousArrival = 0;
        qint64 maxArrivalGap = 0;
        qint64 previousWake = startedAt;
        qint64 maxWakeGap = 0;
        quint64 accepted = 0, late = 0, invalid = 0, duplicate = 0;
        const auto notify = [&](const QString& reason, bool fatal = false) {
            d->running.store(false);
            const auto stats = matcher.stats();
            const auto pacing = d->engine->remotePlaybackPacing();
            const qint64 now = monotonicNs();
            // Bounded, restart-only diagnostics distinguish capture/network
            // loss from a stalled consumer without logging media or secrets.
            const QString detail = reason + QStringLiteral(
                " [ageMs=%1 accepted=%2 decoded=%3 plc=%4 late=%5 invalid=%6 duplicate=%7"
                " rejectedHeaders=%8 lastPacketMs=%9 maxArrivalGapMs=%10 maxWakeGapMs=%11"
                " jitterPackets=%12 ratio=%13 fill=%14 callback=%15 deviceQueued=%16]")
                .arg((now - startedAt) / 1'000'000).arg(accepted)
                .arg(d->decoded.load()).arg(d->concealed.load()).arg(late).arg(invalid)
                .arg(duplicate).arg(d->rejectedHeaders.load())
                .arg((now - lastPacket) / 1'000'000).arg(maxArrivalGap / 1'000'000)
                .arg(maxWakeGap / 1'000'000).arg(jitter.queuedPackets())
                .arg(stats.currentRatio, 0, 'f', 7).arg(stats.ringFillFrames)
                .arg(pacing ? pacing->callbackFrames : 0)
                .arg(pacing ? pacing->queuedFrames : 0);
            QMetaObject::invokeMethod(this, [this, generation, detail, fatal] {
                if (d->generation != generation) { return; }
                if (fatal) { emit errorOccurred(detail); }
                else { emit restartRequested(detail); }
            }, Qt::QueuedConnection);
        };
        const auto initialPacing = d->engine->remotePlaybackPacing();
        if (!initialPacing) {
            notify(QStringLiteral("Speaker device timing is unavailable"), true);
            return;
        }
        const int initialTarget = std::max(960, initialPacing->callbackFrames + 480);
        if (initialTarget + 480 > initialPacing->capacityFrames) {
            notify(QStringLiteral("Speaker callback exceeds remote playback capacity"), true);
            return;
        }
        // WDSP starts half full. Preserve the default 90 ms reserve when a
        // larger selected device quantum transfers extra frames into the
        // speaker ring at startup. This is queue sizing, not a change to the
        // resampler feedback or its continuous interpolation.
        const int deviceHighWater = ((initialTarget + 479) / 480) * 480;
        const int matchRingFrames = 8640 + 2 * std::max(0, deviceHighWater - 960);
        if (!decoder.isReady() || !matcher.configure(1920, 480, matchRingFrames)) {
            notify(QStringLiteral("Could not initialize the remote audio decoder or rate matcher"), true);
            return;
        }
        bool playing = false;
        quint64 lastDeviceFrames = 0;
        qint64 lastDeviceProgress = lastPacket;
        while (!stop.stop_requested()) {
            std::deque<Private::Packet> incoming;
            bool overflow = false;
            {
                std::unique_lock<std::mutex> lock(d->mutex);
                d->wake.wait_for(lock, std::chrono::milliseconds(2), [this, &stop] {
                    return stop.stop_requested() || !d->incoming.empty() || d->overflow;
                });
                incoming.swap(d->incoming);
                overflow = d->overflow;
                d->overflow = false;
            }
            if (stop.stop_requested()) { break; }
            const qint64 wakeAt = monotonicNs();
            maxWakeGap = std::max(maxWakeGap, wakeAt - previousWake);
            previousWake = wakeAt;
            if (overflow) {
                notify(QStringLiteral("Remote audio arrival queue exceeded its latency bound"));
                return;
            }
            for (const auto& packet : incoming) {
                if (previousArrival != 0) {
                    maxArrivalGap = std::max(maxArrivalGap, packet.arrival - previousArrival);
                }
                previousArrival = packet.arrival;
                const auto admitted = jitter.insert(packet.bytes, packet.timestamp, packet.arrival);
                if (admitted == AudioJitterBuffer::Admission::OutsideWindow) {
                    notify(QStringLiteral("Remote audio needs a fresh context after a stream gap"));
                    return;
                }
                switch (admitted) {
                case AudioJitterBuffer::Admission::Accepted:
                    ++accepted;
                    lastPacket = packet.arrival;
                    break;
                case AudioJitterBuffer::Admission::Late: ++late; break;
                case AudioJitterBuffer::Admission::Invalid: ++invalid; break;
                case AudioJitterBuffer::Admission::Duplicate: ++duplicate; break;
                case AudioJitterBuffer::Admission::OutsideWindow: break;
                }
            }
            const qint64 now = monotonicNs();
            if (now - lastPacket > 500'000'000) {
                notify(QStringLiteral("Remote audio had no admitted/playable packets for 500 ms"));
                return;
            }
            // At most the bounded jitter window per wake. A long scheduling
            // stall causes a fresh context, never an unbounded catch-up burst.
            for (int i = 0; i < AudioJitterBuffer::kMaxPackets; ++i) {
                // A network burst stays in the bounded jitter queue until
                // the device has drained room. Reserve two input blocks, a
                // conservative bound above WDSP's maximum resampled block;
                // never let its drop-oldest overflow repair handle a burst.
                const auto room = matcher.stats();
                if (room.ringCapacityFrames - room.ringFillFrames < 2 * 1920) { break; }
                const auto frame = jitter.takeReady(now);
                if (!frame) { break; }
                const auto audio = frame->concealed() ? decoder.decodeMissing()
                    : decoder.decodeRtp(frame->packet, ssrc);
                if ((audio.status != OpusAudioCodecStatus::Accepted
                     && audio.status != OpusAudioCodecStatus::Concealed)
                    || !matcher.push(audio.pcmInterleaved)) {
                    notify(QStringLiteral("Remote audio decode failed"));
                    return;
                }
                if (frame->concealed()) { ++d->concealed; }
                else { ++d->decoded; }
                playing = true;
            }
            if (!playing) { continue; }
            auto pacing = d->engine->remotePlaybackPacing();
            if (!pacing) {
                notify(QStringLiteral("Speaker device timing became unavailable"), true);
                return;
            }
            if (pacing->consumedFrames != lastDeviceFrames) {
                lastDeviceFrames = pacing->consumedFrames;
                lastDeviceProgress = now;
            } else if (now - lastDeviceProgress > 500'000'000) {
                notify(QStringLiteral("Speaker device stopped consuming audio"), true);
                return;
            }
            // Cover the selected callback quantum plus one worker block.
            // Default 128-frame callbacks need 20-30 ms queued; a user-selected
            // 2048-frame callback needs more so one callback cannot exhaust it.
            const int targetFrames = std::max(960, pacing->callbackFrames + 480);
            if (targetFrames + 480 > pacing->capacityFrames) {
                notify(QStringLiteral("Speaker callback exceeds remote playback capacity"), true);
                return;
            }
            // The timer only wakes us. Actual device queue consumption is the
            // output clock; at most one bounded ring can be replenished here.
            for (int i = 0; i < pacing->capacityFrames / 480
                 && pacing->queuedFrames < targetFrames; ++i) {
                const QVector<float> pcm = matcher.take();
                if (pcm.size() != 960 || !d->engine->writeRemotePlayback(pcm)) {
                    notify(QStringLiteral("Could not write remote audio to the speaker device"), true);
                    return;
                }
                pacing = d->engine->remotePlaybackPacing();
                if (!pacing) { break; }
            }
            const auto stats = matcher.stats();
            d->underflows.store(stats.underflows);
            d->overflows.store(stats.overflows);
            if (stats.underflows || stats.overflows) {
                notify(QStringLiteral("Remote audio exceeded its continuous clock buffer (%1 underflows, %2 overflows)")
                    .arg(stats.underflows).arg(stats.overflows));
                return;
            }
        }
    });
    return true;
}

void RemoteAudioReceiver::submit(const QByteArray& packet)
{
    if (!isRunning()) { return; }
    const auto header = inspectOpusRtp(packet, d->ssrc);
    if (header.status != OpusAudioCodecStatus::Accepted) {
        ++d->rejectedHeaders;
        return;
    }
    {
        std::lock_guard<std::mutex> lock(d->mutex);
        if (d->incoming.size() >= AudioJitterBuffer::kMaxPackets) { d->overflow = true; }
        else { d->incoming.push_back({packet, header.timestamp, monotonicNs()}); }
    }
    d->wake.notify_one();
}
} // namespace NereusSDR
