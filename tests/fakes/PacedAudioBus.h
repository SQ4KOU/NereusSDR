#pragma once
// no-port-check: NereusSDR-original deterministic speaker sink for remote audio tests.
#include "core/IAudioBus.h"
#include <QVector>
#include <chrono>
#include <condition_variable>
#include <deque>
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
        if (queue.size() + count > 9600) { return -1; }
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
        return OutputPacing{consumed, int(queue.size()) / 2, 4800, callbackFrames};
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
    }
    float rxLevel() const override { return 0; }
    float txLevel() const override { return 0; }
    QString backendName() const override { return QStringLiteral("PacedTest"); }
    NereusSDR::AudioFormat negotiatedFormat() const override { return format; }
    QVector<float> heard;
    int flushes = 0;
    int peakQueued = 0;
    int callbackFrames = 480;
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
};
