#pragma once
// no-port-check: NereusSDR-original remote audio lifecycle and worker wiring.
#include <QObject>
#include <QByteArray>
#include <memory>
#include <optional>

namespace NereusSDR {
class AudioEngine;

// Read-only diagnostics for one remote playback context. generation advances
// only after a successful start. Packet counters stay available after stop for
// diagnostics; a later successful start resets them. deviceConsumedFrames and
// device progress are published only after the worker observes the speaker
// pacing source.
struct RemoteAudioReceiverTelemetry {
    quint64 generation = 0;
    bool running = false;
    quint64 acceptedPackets = 0;
    quint64 decodedPackets = 0;
    quint64 concealedPackets = 0;
    quint64 latePackets = 0;
    quint64 invalidPackets = 0;
    quint64 duplicatePackets = 0;
    quint64 rejectedHeaders = 0;
    quint64 deviceConsumedFrames = 0;
    int underflows = 0;
    int overflows = 0;
    // Lifetime interruption totals remain available across receiver contexts.
    // They are absent only in the bounded transition-unavailable snapshot.
    std::optional<quint64> lifetimeUnderflows;
    std::optional<quint64> lifetimeOverflows;
    std::optional<qint64> lastAdmittedPacketAgeMs;
    std::optional<qint64> lastDeviceProgressAgeMs;
};

// One generation of bounded RTP receive, Opus decoding and WDSP rate matching.
// All codec/resampler work runs off the GUI and device callback threads.
class RemoteAudioReceiver final : public QObject {
    Q_OBJECT
public:
    explicit RemoteAudioReceiver(AudioEngine* engine, QObject* parent = nullptr);
    ~RemoteAudioReceiver() override;
    bool start(quint32 ssrc, quint32 firstTimestamp);
    void stop();
    void submit(const QByteArray& packet);
    bool isRunning() const;
    quint64 decodedPackets() const;
    quint64 concealedPackets() const;
    int rateMatcherUnderflows() const;
    int rateMatcherOverflows() const;
    // Thread-safe observational snapshot.  It neither reads worker-owned
    // jitter/resampler state nor synchronizes with the audio callback.
    RemoteAudioReceiverTelemetry telemetry() const;
signals:
    void restartRequested(const QString& reason);
    void errorOccurred(const QString& reason);
private:
    struct Private;
    std::unique_ptr<Private> d;
};
} // namespace NereusSDR
