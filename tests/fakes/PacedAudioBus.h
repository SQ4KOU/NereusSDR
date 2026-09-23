#pragma once
// no-port-check: NereusSDR-original deterministic speaker sink for remote audio tests.
#include "core/IAudioBus.h"
#include <QVector>
#include <chrono>
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>

class PacedAudioBus final : public NereusSDR::IAudioBus {
public:
    bool open(const NereusSDR::AudioFormat& f) override { format = f; active = true; return true; }
    void close() override { active = false; }
    bool isOpen() const override { return active; }
    qint64 push(const char* data, qint64 bytes) override {
        std::lock_guard<std::mutex> lock(mutex);
        const auto* samples = reinterpret_cast<const float*>(data);
        const int count = int(bytes / sizeof(float));
        if (int(queue.size()) - 2 * playedAheadFramesLocked() + count > 9600) { return -1; }
        if (playClock) {
            // The device has taken past everything queued: it played silence
            // meanwhile, so these samples start at its next callback, not in
            // the past.
            const qint64 dry = takenFramesLocked() - playedFrames - qint64(queue.size()) / 2;
            if (dry > 0) {
                queue.insert(queue.end(), std::size_t(dry * 2), 0.0f);
                playedDryFrames += dry;
            }
        }
        queue.insert(queue.end(), samples, samples + count);
        peakQueued = qMax(peakQueued, int(queue.size()) / 2);
        return bytes;
    }
    qint64 pull(char*, qint64) override { return 0; }
    void flush() override { std::lock_guard<std::mutex> lock(mutex); queue.clear(); ++flushes; }
    std::optional<OutputPacing> outputPacing() const override {
        std::unique_lock<std::mutex> lock(mutex);
        ++outputPacingCalls;
        if (blockOutputPacingAfterCalls >= 0
            && outputPacingCalls > blockOutputPacingAfterCalls) {
            pacingGateEntered = true;
            pacingGateChanged.notify_all();
            pacingGateChanged.wait(lock, [this] { return releaseOutputPacingGate; });
        }
        if (!outputPacingAvailable) { return std::nullopt; }
        const int ahead = playedAheadFramesLocked();
        return OutputPacing{consumed + quint64(ahead),
                            std::max(0, int(queue.size()) / 2 - ahead), 4800, callbackFrames,
                            deviceLatencyNs};
    }
    // R-R3-35 test device clock. From this call the device plays one frame
    // every 1/48000 s of `clockNs` continuously, as hardware does, and it
    // takes frames from the queue the way the callback device it reports
    // does: `callbackFrames` at a time, each callback at the instant its
    // first frame starts to play (no device latency). So the queue and
    // consumed count it reports drop a callback at a time, following the
    // clock rather than the render() calls, and renderDue() moves exactly
    // the frames played so far into `heard`. Heard frame k (counted from
    // this call) played at playClockOriginNs() + k / 48 kHz, however late
    // the caller's timer runs. Set before any render.
    void setPlayClockForTesting(std::function<qint64()> clockNs)
    {
        std::lock_guard<std::mutex> lock(mutex);
        playClock = std::move(clockNs);
        playOriginNs = playClock();
        playedFrames = 0;
    }
    qint64 playClockOriginNs() const { std::lock_guard<std::mutex> lock(mutex); return playOriginNs; }
    // Frames the play clock found nothing to play for (the queue ran dry).
    qint64 playedDryFramesForTesting() const { std::lock_guard<std::mutex> lock(mutex); return playedDryFrames; }
    // Renders every frame the play clock has reached; returns how many.
    int renderDue()
    {
        int frames = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!playClock) { return 0; }
            frames = int(dueFramesLocked() - playedFrames);
        }
        if (frames > 0) { render(frames); }
        return frames;
    }
    // Test-only worker gate. Configure it before beginRemotePlayback(); the
    // first pacing read belongs to that synchronous setup and the receiver
    // worker blocks on the following read. releaseOutputPacingGateForTesting
    // must run before a receiver stop joins that worker.
    void blockOutputPacingAfterCallsForTesting(int calls)
    {
        std::lock_guard<std::mutex> lock(mutex);
        blockOutputPacingAfterCalls = calls;
        releaseOutputPacingGate = false;
        pacingGateEntered = false;
    }
    // Re-arms the gate so the next pacing read, from whichever thread, blocks.
    void blockNextOutputPacingForTesting()
    {
        std::lock_guard<std::mutex> lock(mutex);
        blockOutputPacingAfterCalls = outputPacingCalls;
        releaseOutputPacingGate = false;
        pacingGateEntered = false;
    }
    bool waitForOutputPacingGateForTesting(std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex);
        return pacingGateChanged.wait_for(lock, timeout, [this] { return pacingGateEntered; });
    }
    void releaseOutputPacingGateForTesting()
    {
        std::lock_guard<std::mutex> lock(mutex);
        releaseOutputPacingGate = true;
        pacingGateChanged.notify_all();
    }
    // Test-only device loss. A speaker that goes away stops reporting its
    // playback timing: while unavailable, outputPacing() answers nullopt, so
    // remote playback cannot begin and a playing receiver loses its device
    // clock through the real AudioEngine path. Callable from any thread.
    void setOutputPacingAvailableForTesting(bool available)
    {
        std::lock_guard<std::mutex> lock(mutex);
        outputPacingAvailable = available;
    }
    void render(int frames) {
        std::lock_guard<std::mutex> lock(mutex);
        for (int i = 0; i < frames * 2; ++i) {
            heard.append(queue.empty() ? 0.0f : queue.front());
            if (!queue.empty()) { queue.pop_front(); }
        }
        consumed += frames;
        playedFrames += frames;
    }
    float rxLevel() const override { return 0; }
    float txLevel() const override { return 0; }
    QString backendName() const override { return QStringLiteral("PacedTest"); }
    NereusSDR::AudioFormat negotiatedFormat() const override { return format; }
    QVector<float> heard;
    int flushes = 0;
    int peakQueued = 0;
    int callbackFrames = 480;
    // R-R3-35: what this bus reports as its device latency (none: unknown).
    std::optional<qint64> deviceLatencyNs;
private:
    bool active = true;
    NereusSDR::AudioFormat format;
    mutable std::mutex mutex;
    mutable std::condition_variable pacingGateChanged;
    std::deque<float> queue;
    quint64 consumed = 0;
    mutable int outputPacingCalls = 0;
    int blockOutputPacingAfterCalls = -1;
    mutable bool pacingGateEntered = false;
    bool releaseOutputPacingGate = false;
    bool outputPacingAvailable = true;
    std::function<qint64()> playClock;
    qint64 playOriginNs = 0;
    qint64 playedFrames = 0;
    qint64 playedDryFrames = 0;
    qint64 dueFramesLocked() const
    {
        return playClock ? (playClock() - playOriginNs) * 48 / 1'000'000 : 0;
    }
    // Frames the device has taken from the queue by now: every callback up
    // to and including the one that holds the frame now playing.
    qint64 takenFramesLocked() const
    {
        const qint64 perCallback = std::max(1, callbackFrames);
        return (dueFramesLocked() / perCallback + 1) * perCallback;
    }
    // Frames the device has taken that render() has not yet moved.
    int playedAheadFramesLocked() const
    {
        return playClock ? int(std::max<qint64>(0, takenFramesLocked() - playedFrames)) : 0;
    }
};
