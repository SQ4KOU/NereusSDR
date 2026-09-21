#pragma once
// no-port-check: NereusSDR-original deterministic speaker sink for remote audio tests.
#include "core/IAudioBus.h"
#include <QVector>
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
        std::lock_guard<std::mutex> lock(mutex);
        return OutputPacing{consumed, int(queue.size()) / 2, 4800, callbackFrames};
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
    std::deque<float> queue;
    quint64 consumed = 0;
};
