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
    // Admitted to the jitter queue and not later discarded at start: a packet
    // trimmed with a connect-time backlog moves to startDiscardedPackets.
    quint64 acceptedPackets = 0;
    quint64 decodedPackets = 0;
    quint64 concealedPackets = 0;
    quint64 latePackets = 0;
    quint64 invalidPackets = 0;
    quint64 duplicatePackets = 0;
    quint64 rejectedHeaders = 0;
    // Packets dropped before playback began because a connect-time backlog
    // overran the arrival bound or the jitter window. Only the newest packet
    // of such a backlog is kept; nothing counted here was ever heard.
    quint64 startDiscardedPackets = 0;
    // Valid RTP/profile payload bytes received in this context. This counts
    // duplicates and packets later dropped by the bounded local queue.
    quint64 receivedOpusPayloadBytes = 0;
    quint64 deviceConsumedFrames = 0;
    int underflows = 0;
    int overflows = 0;
    // Lifetime interruption totals remain available across receiver contexts.
    // They are absent only in the bounded transition-unavailable snapshot.
    std::optional<quint64> lifetimeUnderflows;
    std::optional<quint64> lifetimeOverflows;
    std::optional<qint64> lastAdmittedPacketAgeMs;
    std::optional<qint64> lastDeviceProgressAgeMs;
    // Worker-observed speaker queue duration. It is unavailable before a
    // pacing sample, and for stopped or failed contexts.
    std::optional<double> speakerQueuedMs;
    // RFC 3550 interarrival jitter and extended-sequence expected/missing
    // accounting for this computer's admitted RTP in this context, from
    // RtpReceptionStats. Like the packet counters above, the last measured
    // value stays available after stop(); a later successful start() resets
    // it. arrivalJitterMs is absent until a second packet has been observed.
    std::optional<double> arrivalJitterMs;
    quint64 expectedPackets = 0;
    quint64 missingPackets = 0;
    // Packets currently held in the jitter buffer for reordering, in ms
    // (queued count x the buffer's 40 ms packet duration). Unlike the
    // fields above, this is a live gauge: unavailable before a measurement
    // and for stopped or failed contexts, like speakerQueuedMs.
    std::optional<double> reorderQueuedMs;
};

// One generation of bounded RTP receive, Opus decoding and WDSP rate matching.
// All codec/resampler work runs off the GUI and device callback threads.
class RemoteAudioReceiver final : public QObject {
    Q_OBJECT
public:
    // The fault a worker notify() site observed. Q_ENUM registers it as a
    // real meta-type so QSignalSpy and the cross-thread Qt::QueuedConnection
    // delivery in notify() carry it, not an opaque int.
    enum class Fault {
        SpeakerOpenFailed, SpeakerTimingUnavailable, SpeakerCallbackTooLarge,
        SpeakerStalled, SpeakerWriteFailed, DecoderUnavailable,
        ArrivalBurst, StreamGap, NoPackets, DecodeFailed, ClockBuffer,
    };
    Q_ENUM(Fault)

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
    void restartRequested(const QString& reason, NereusSDR::RemoteAudioReceiver::Fault fault);
    void errorOccurred(const QString& reason, NereusSDR::RemoteAudioReceiver::Fault fault);
private:
    struct Private;
    std::unique_ptr<Private> d;
};
} // namespace NereusSDR
