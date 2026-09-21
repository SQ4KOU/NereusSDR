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
    d->running.store(true);
    const quint64 generation = d->generation;
    d->worker = std::jthread([this, ssrc, firstTimestamp, generation](std::stop_token stop) {
        AudioJitterBuffer jitter;
        jitter.reset(firstTimestamp);
        OpusAudioDecoder decoder;
        RemoteAudioRateMatcher matcher;
        const auto notify = [this, generation](const QString& reason, bool fatal = false) {
            d->running.store(false);
            QMetaObject::invokeMethod(this, [this, generation, reason, fatal] {
                if (d->generation != generation) { return; }
                if (fatal) { emit errorOccurred(reason); }
                else { emit restartRequested(reason); }
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
        qint64 lastPacket = monotonicNs();
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
            if (overflow) {
                notify(QStringLiteral("Remote audio arrival queue exceeded its latency bound"));
                return;
            }
            for (const auto& packet : incoming) {
                const auto admitted = jitter.insert(packet.bytes, packet.timestamp, packet.arrival);
                if (admitted == AudioJitterBuffer::Admission::OutsideWindow) {
                    notify(QStringLiteral("Remote audio needs a fresh context after a stream gap"));
                    return;
                }
                if (admitted == AudioJitterBuffer::Admission::Accepted) { lastPacket = packet.arrival; }
            }
            const qint64 now = monotonicNs();
            if (now - lastPacket > 500'000'000) {
                notify(QStringLiteral("Remote audio packets stopped arriving"));
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
    if (header.status != OpusAudioCodecStatus::Accepted) { return; }
    {
        std::lock_guard<std::mutex> lock(d->mutex);
        if (d->incoming.size() >= AudioJitterBuffer::kMaxPackets) { d->overflow = true; }
        else { d->incoming.push_back({packet, header.timestamp, monotonicNs()}); }
    }
    d->wake.notify_one();
}
} // namespace NereusSDR
