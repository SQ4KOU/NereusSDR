// =================================================================
// src/core/audio/RemoteVaxFeeder.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. R-R3-44 (R3 receiver audio plan,
// Task 5): VAX in a remote window. The rate matching it drives is the
// existing RemoteAudioRateMatcher (WDSP rmatch, attributed there); this
// file adds no ported logic.
//
// One feeder per VAX channel. It is a receiver-stream sink
// (IReceiverPcmSink): the remote window asks the Core for the assigned
// slice's audio and the stream's blocks land here on the receive worker
// thread. receiverAudioBlock() only copies the block into a bounded
// hand-off ring and returns (no allocation, no lock, no device or network
// work), because it runs under the stream's lock (R3 receiver audio plan,
// Task 3's finding).
//
// A worker of its own (pump(), every 5 ms) moves the audio from the ring
// through a rate matcher into the VAX output, topping the output's queue
// up to a small target as the output reports consuming it. The output's
// own clock (the app reading the VAX device) therefore paces the audio,
// not the Core's: a difference between the two clocks is absorbed by the
// rate matcher instead of growing or starving the delay.
//
// Nothing reading the output is not a fault. When the output reports no
// progress with audio queued for longer than an app's read step (at least
// 60 ms), the feeder stops feeding the rate matcher and drops what
// arrives; after 500 ms it reports that nothing reads the output (a VAX
// device no app has open). It plays again as soon as the output moves.
// When the Core goes quiet for 250 ms, it restarts the rate matcher and
// waits for audio.
//
// An output that reports no playback timing (the Linux pactl pipe) is
// written as the audio arrives, paced by the Core's stream alone.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-23: Written for NereusSDR by J.J. Boyd (KG4VCF), with
//                 AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#pragma once

#include "core/IAudioBus.h"
#include "core/audio/AudioRingSpsc.h"
#include "core/session/media/IReceiverPcmSink.h"
#include "core/session/media/RemoteAudioRateMatcher.h"

#include <QString>
#include <QVector>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace NereusSDR {

class AudioEngine;

/// Where a feeder writes: one VAX output's playback timing and its write.
/// Both are called on the feeder's worker only.
struct VaxOutputPort {
    std::function<std::optional<IAudioBus::OutputPacing>()> pacing;
    /// Interleaved 48 kHz stereo, `frames` frames. False when nothing was
    /// written (the output is closed).
    std::function<bool(const float* stereo, int frames)> write;

    /// The engine's VAX output for `channel` (1..4), with the channel's
    /// VAX gain and mute applied (AudioEngine::writeVaxOutput).
    static VaxOutputPort forEngine(AudioEngine* engine, int channel);
};

struct RemoteVaxFeederStats {
    enum class State {
        Idle,            // no slice assigned
        WaitingForAudio, // assigned; nothing arriving from the Core
        Playing,
        NoReader,        // the output stopped taking audio; nothing is reading it
    };
    State state = State::Idle;
    int sourceSliceId = -1;
    // Frames the stream delivered for the assigned slice (blocks for any
    // other slice are ignored), and frames dropped because the hand-off
    // ring was full.
    quint64 receivedFrames = 0;
    quint64 droppedFrames = 0;
    quint64 writtenFrames = 0;
    // The rate matcher ran dry or over while the output was reading, and
    // was restarted: a gap the app heard (the Core's audio ran late or
    // stopped). An output that stops reading never counts here.
    int restarts = 0;
    // The delay ahead of the output, in frames at 48 kHz: its own queue,
    // the rate matcher's fill and the hand-off ring.
    int queuedFrames = 0;
    int matcherFillFrames = 0;
    int handoffFrames = 0;
    // The rate matcher's output-per-input ratio, once it has measured one.
    std::optional<double> ratio;
    // The output reports playback timing, so the feeder paces by it.
    bool paced = false;
    int delayFrames() const { return queuedFrames + matcherFillFrames + handoffFrames; }
};

class RemoteVaxFeeder final : public IReceiverPcmSink {
public:
    /// Monotonic nanoseconds. Empty: std::chrono::steady_clock.
    using Clock = std::function<qint64()>;

    // One lossless packet (4 ms); an Opus packet is ten of them.
    static constexpr int kInputChunkFrames = 192;
    static constexpr int kOutputBlockFrames = 480;
    // The rate matcher's ring: 180 ms, the remote speaker's default, so it
    // starts with 90 ms of reserve (WDSP rmatch starts half full).
    static constexpr int kMatcherRingFrames = 8640;
    // Hand-off ring from the receive worker: 32768 stereo frames, 683 ms.
    static constexpr std::size_t kHandoffBytes = std::size_t(1) << 18;
    static constexpr qint64 kPumpIntervalNs = 5'000'000;
    // The output took nothing for this long with audio queued: it paused
    // (at least; twice its largest read step when that is longer).
    static constexpr qint64 kPausedNs = 60'000'000;
    // ... and for this long: no reader.
    static constexpr qint64 kNoReaderNs = 500'000'000;
    // No audio from the Core for this long: wait for it afresh.
    static constexpr qint64 kQuietNs = 250'000'000;

    RemoteVaxFeeder(int channel, VaxOutputPort port, Clock clock = {});
    ~RemoteVaxFeeder() override;

    RemoteVaxFeeder(const RemoteVaxFeeder&) = delete;
    RemoteVaxFeeder& operator=(const RemoteVaxFeeder&) = delete;

    int channel() const { return m_channel; }

    /// The slice whose stream feeds this channel, -1 for none. Anything
    /// queued for the previous slice is dropped. GUI thread, before the
    /// stream is requested (and after it is released).
    void setSourceSlice(int sliceId);
    int sourceSlice() const { return m_sourceSlice.load(std::memory_order_acquire); }

    // IReceiverPcmSink
    /// Receive worker thread: copies the block into the hand-off ring and
    /// returns. A block for another slice, or one that does not fit, is
    /// dropped.
    void receiverAudioBlock(int sliceId, const float* interleavedStereo, int frames) override;
    /// GUI thread: records the reason; the feeder waits for audio again.
    void receiverAudioStopped(int sliceId, const QString& reason) override;
    /// The last stop reason for the assigned slice (a wire reason or a
    /// sentence), empty once audio has flowed again since. GUI thread.
    QString lastStopReason() const;

    /// Runs pump() every kPumpIntervalNs on a thread of its own until
    /// stopWorker() or destruction. Tests drive pump() themselves instead.
    void startWorker();
    void stopWorker();
    bool workerRunning() const;

    /// One step: move what arrived into the output as far as its queue
    /// target. Called by the worker, or by a test with its own clock; never
    /// from two threads at once.
    void pump();

    RemoteVaxFeederStats stats() const;

private:
    using State = RemoteVaxFeederStats::State;

    void dropHandoff();
    void popHandoff(void* into, std::size_t bytes);
    qint64 pausedNs() const;
    void restartMatcher();
    // Moves whole input chunks from the hand-off ring into the matcher (or,
    // unpaced, straight to the output). Returns the frames moved.
    int drainHandoff(bool paced);
    void publish(State state, int queuedFrames);

    const int m_channel;
    VaxOutputPort m_port;
    Clock m_clock;

    // Written by the GUI thread, read by the receive worker and the pump.
    std::atomic<int> m_sourceSlice{-1};
    std::atomic<quint32> m_generation{0};
    std::atomic<quint64> m_receivedFrames{0};
    std::atomic<quint64> m_droppedFrames{0};
    // Bytes the receive worker has pushed, and how many of them belonged to
    // the previous slice when the source last changed (setSourceSlice runs
    // after that slice's release, so none of its blocks follow).
    std::atomic<quint64> m_pushedBytes{0};
    std::atomic<quint64> m_dropUntilBytes{0};
    AudioRingSpsc<kHandoffBytes> m_handoff;

    // Pump only.
    RemoteAudioRateMatcher m_matcher;
    bool m_matcherReady = false;
    bool m_matcherTried = false;
    QVector<float> m_chunk;
    QVector<float> m_direct;
    quint32 m_seenGeneration = 0;
    quint64 m_poppedBytes = 0;
    State m_state = State::Idle;
    bool m_haveConsumed = false;
    quint64 m_lastConsumed = 0;
    qint64 m_lastProgressNs = 0;
    qint64 m_lastInputNs = 0;
    int m_largestStep = 0;
    int m_restarts = 0;
    quint64 m_writtenFrames = 0;

    mutable std::mutex m_statsMutex;
    RemoteVaxFeederStats m_stats;
    QString m_lastStopReason;  // GUI thread
    quint64 m_stopAtFrames = 0;  // GUI thread: receivedFrames at the stop

    std::mutex m_workerMutex;
    std::condition_variable m_workerWake;
    bool m_workerStop = false;
    std::thread m_worker;
};

} // namespace NereusSDR
