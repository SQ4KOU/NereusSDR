#pragma once
// no-port-check: NereusSDR-original remote audio lifecycle and worker wiring.
#include "core/session/media/PcmAudioCodec.h"
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
    // Valid RTP/profile payload bytes received in this context, Opus or
    // lossless (whichever the context runs). This counts duplicates and
    // packets later dropped by the bounded local queue.
    quint64 receivedAudioPayloadBytes = 0;
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
    // (queued count x the context's packet duration: 40 ms Opus, 4 ms
    // lossless). Unlike the
    // fields above, this is a live gauge: unavailable before a measurement
    // and for stopped or failed contexts, like speakerQueuedMs.
    std::optional<double> reorderQueuedMs;
    // The continuous clock correction's current resample ratio (WDSP rmatch
    // `var`, read through RemoteAudioRateMatcherStats::currentRatio), the
    // same value restart fault text reports as `ratio=`. It is a ratio near
    // 1.0; (ratio - 1) x 1e6 is the correction in parts per million. WDSP
    // holds the initial 1.0 until its 3.0 s startup delay of audio has passed
    // (third_party/wdsp/src/rmatch.c create_rmatchV), then adjusts it. A live
    // gauge like speakerQueuedMs: absent until rmatch has measured it (its
    // startup delay has passed during playback), and for stopped or failed
    // contexts.
    std::optional<double> driftRatio;
};

// One generation of bounded RTP receive, decoding and WDSP rate matching.
// All codec/resampler work runs off the GUI and device callback threads.
// R-R3-23: a context runs one profile, named at start(): Opus (40 ms
// packets, payload type 111) or lossless (L16, 4 ms packets, payload type
// 96). submit() reads each packet's payload type and hands only the
// context's own type to its decoder; the other type is a rejected header.
// A lost lossless packet plays as 4 ms of silence. The jitter window and
// the arrival bound are the same 320 ms for both profiles.
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
    bool start(quint32 ssrc, quint32 firstTimestamp,
               RemoteAudioProfile profile = RemoteAudioProfile::Opus);
    /// The profile of the current (or last) context.
    RemoteAudioProfile profile() const;
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
