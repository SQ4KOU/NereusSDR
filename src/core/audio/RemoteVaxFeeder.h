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
// (IReceiverPcmSink): the remote window asks the Core for the audio of
// every slice assigned to the channel and the streams' blocks land here on
// the receive worker thread. receiverAudioBlock() only copies the block
// into that slice's bounded hand-off ring and returns (no allocation, no
// lock, no device or network work), because it runs under the stream's
// lock (R3 receiver audio plan, Task 3's finding).
//
// Slices sharing a channel are mixed, as the local VAX tee mixes them
// (AudioEngine, VaxChannelMixer): the pump sums the slices' rings in
// 192-frame chunks before the rate matcher. A chunk waits until every
// slice still sending has one queued; a slice whose stream stopped, or
// that has sent nothing for kQuietNs, counts as silence until its audio
// flows again. A slice that falls behind the others (a stall, then a
// burst) keeps at most kMaxLagFrames more queued than the least-queued
// slice; the oldest beyond that is dropped, so it cannot play late from
// then on.
//
// A worker of its own (pump(), every 5 ms) moves the audio from the rings
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
// The rate matcher starts with 90 ms of reserve. A link that delivers in
// clumps (a pause of 80 to 115 ms, then the backlog at once) runs that dry,
// and each time the app hears a gap. So a dropout the link's audio comes
// back from within kQuietNs doubles the reserve, up to 250 ms; a source
// that stops (a link outage, the Core ending the slice) grows nothing. The
// pause's backlog past one Opus packet is dropped when it comes back, so
// the delay is the reserve and not the reserve plus the pause. The same
// trim follows every restart, so an outage longer than kQuietNs that
// delivers its backlog afterwards neither adds that backlog to the delay
// nor overflows the new matcher. (When the output side underflows with
// the input steady, the trim can drop up to one input chunk, 191 frames,
// of audio that was on time.) An overflow grows the reserve only once the
// matcher has played since its restart.
// The reserve shrinks a step only where playback restarts anyway (the Core
// went quiet) and no dropout came for kShrinkAfterNs: rebuilding the
// matcher mid-stream would itself be a gap.
//
// The matcher's ratio is held, not left to WDSP rmatch's own control: that
// control answers each pause and burst with a ratio swing of up to 1.2
// percent, which moved a 1579 Hz tone by up to 25 Hz (FT8's tones are
// 6.25 Hz apart). As the remote microphone's matcher does (RemoteMicFeed),
// the ratio moves at 50 ppm per ms of deviation, at most 500 ppm (under a
// cent of pitch), once per 2 s window. What it measures is the window's
// mean fill with what has played since the last arrival added back: that
// stays put through a pause and its burst, where the plain mean falls by
// about p^2/(2 * 2000) ms for a p ms pause. The level held is the first
// window's after a restart, so the reserve the start gave is kept; the
// P-only control settles 1 ms from it per 50 ppm of clock difference.
// The held ratio survives a rebuilt matcher, so it keeps the clock rate
// it learned, as Thetis IVAC keeps its forced var across
// create_rmatchV (ivac.c:38-44 [v2.10.3.15]).
//
// An output that reports no playback timing (the Linux pactl pipe) is
// written as the audio arrives, paced by the Core's stream alone.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-23: Written for NereusSDR by J.J. Boyd (KG4VCF), with
//                 AI-assisted implementation via Anthropic Claude Code.
//   2026-09-23: R-R3-44 fix wave: a set of slices per channel, one hand-off
//                 ring each, summed before the rate matcher. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-09-23: R-R3-44 fix wave follow-up: stop reasons kept per slice;
//                 a slice's lag behind the others capped at 85 ms; a slice
//                 leaving no longer trims the others. J.J. Boyd (KG4VCF),
//                 AI-assisted via Anthropic Claude Code.
//   2026-10-09: native audio plan Task 6 (R-AUD-15): an output that takes
//                 the stereo mix into its own clock matcher is written as
//                 the audio arrives, without this feeder's rate matcher.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-16: The rate matcher's reserve grows after a dropout, 90 ms up
//                 to 250 ms; its ratio is held and moved slowly toward the
//                 ring's middle, kept across a rebuilt matcher. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#pragma once

#include "core/IAudioBus.h"
#include "core/audio/AudioRingSpsc.h"
#include "core/session/media/IReceiverPcmSink.h"
#include "core/session/media/RemoteAudioRateMatcher.h"

#include <QList>
#include <QString>
#include <QVector>

#include <array>
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
    /// R-AUD-15: true when the output takes the 48 kHz stereo mix into its
    /// own clock matcher (IAudioBus::takesStereoMix). The feeder then
    /// writes what arrives as it arrives and runs no rate matcher of its
    /// own; restartClockMatch() restarts the output's matcher when playback
    /// starts afresh. Either may be empty (an output without one).
    std::function<bool()> takesStereoMix;
    std::function<void()> restartClockMatch;

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
    // The lowest assigned slice (-1 for none), and all of them.
    int sourceSliceId = -1;
    QList<int> sourceSliceIds;
    // Frames the streams delivered for the assigned slices (blocks for any
    // other slice are ignored), and frames dropped because a hand-off ring
    // was full.
    quint64 receivedFrames = 0;
    quint64 droppedFrames = 0;
    // Frames dropped from a slice's ring to keep the slices in step (a
    // slice lagging the others, or the backlog behind one that went quiet).
    quint64 trimmedFrames = 0;
    quint64 writtenFrames = 0;
    // The rate matcher ran dry or over while the output was reading, and
    // was restarted: a gap the app heard (the Core's audio ran late or
    // stopped). An output that stops reading never counts here.
    int restarts = 0;
    // The delay ahead of the output, in frames at 48 kHz: its own queue,
    // the rate matcher's fill and the fullest hand-off ring.
    int queuedFrames = 0;
    int matcherFillFrames = 0;
    int handoffFrames = 0;
    // The rate matcher's output-per-input ratio, once its 3.0 s startup has
    // passed (WDSP rmatch's control flag).
    std::optional<double> ratio;
    // The rate matcher's ring; it starts half full, so half of it is the
    // reserve. Grows after a dropout (RemoteVaxFeeder::kMaxMatcherRingFrames).
    int matcherRingFrames = 0;
    // Ratio windows that wanted more than kMaxRatioPpm: clocks further
    // apart than the held ratio corrects.
    int ratioWindowsAtCap = 0;
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
    // Each dropout doubles the ring, up to 500 ms: 250 ms of reserve, which
    // rides out the 80 to 115 ms pauses a busy link shows. A pause past
    // kQuietNs restarts playback whatever the reserve.
    static constexpr int kMaxMatcherRingFrames = 24000;
    // No dropout for this long: the next quiet restart halves the ring,
    // never below kMatcherRingFrames.
    static constexpr qint64 kShrinkAfterNs = 600'000'000'000;
    // The held ratio: each window's level (the mean matcher fill, with what
    // played since the last arrival added back) moves it toward the first
    // window's, at kRatioPpmPerMs per ms of deviation and at most
    // kMaxRatioPpm, the remote microphone's figures
    // (RemoteMicConfig::kRatioPpmPerMs, kMaxRatioPpm).
    static constexpr qint64 kRatioWindowNs = 2'000'000'000;
    static constexpr double kRatioPpmPerMs = 50.0;
    static constexpr double kMaxRatioPpm = 500.0;
    // Hand-off ring from the receive worker, one per slice: 32768 stereo
    // frames, 683 ms.
    static constexpr std::size_t kHandoffBytes = std::size_t(1) << 18;
    // Slices one channel can carry: a Core's slice limit
    // (WdspEngine::kMaxSliceChannels).
    static constexpr int kMaxSources = 5;
    static constexpr qint64 kPumpIntervalNs = 5'000'000;
    // The output took nothing for this long with audio queued: it paused
    // (at least; twice its largest read step when that is longer).
    static constexpr qint64 kPausedNs = 60'000'000;
    // ... and for this long: no reader.
    static constexpr qint64 kNoReaderNs = 500'000'000;
    // No audio from the Core for this long: wait for it afresh.
    static constexpr qint64 kQuietNs = 250'000'000;
    // How far one slice may run behind the others: 4096 frames, 85 ms at
    // 48 kHz, the ring the local VAX mix keeps per slice
    // (VaxChannelMixer::kRingFrames).
    static constexpr int kMaxLagFrames = 4096;

    RemoteVaxFeeder(int channel, VaxOutputPort port, Clock clock = {});
    ~RemoteVaxFeeder() override;

    RemoteVaxFeeder(const RemoteVaxFeeder&) = delete;
    RemoteVaxFeeder& operator=(const RemoteVaxFeeder&) = delete;

    int channel() const { return m_channel; }

    /// The slices whose streams feed this channel (at most kMaxSources;
    /// empty for none). Anything queued for a slice no longer in the set is
    /// dropped; a slice that stays keeps playing. GUI thread, after the
    /// streams of the slices leaving are released and before those of the
    /// slices joining are requested.
    void setSourceSlices(const QList<int>& sliceIds);
    /// One slice, or none for -1.
    void setSourceSlice(int sliceId);
    QList<int> sourceSlices() const;
    /// The lowest assigned slice, -1 for none.
    int sourceSlice() const;

    // IReceiverPcmSink
    /// Receive worker thread: copies the block into its slice's hand-off
    /// ring and returns. A block for a slice not assigned here, or one that
    /// does not fit, is dropped.
    void receiverAudioBlock(int sliceId, const float* interleavedStereo, int frames) override;
    /// GUI thread: records the reason; that slice counts as silence until
    /// its audio flows again.
    void receiverAudioStopped(int sliceId, const QString& reason) override;
    /// Why an assigned slice's stream stopped, per slice, oldest first:
    /// each stays until that slice's audio flows again or it leaves the
    /// channel. GUI thread.
    struct Stop {
        int sliceId = -1;
        QString reason;   // a wire reason or a sentence
    };
    QList<Stop> stops() const;
    /// The most recent of stops(), empty when there is none. GUI thread.
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

    struct Source {
        // Written by the GUI thread, read by the receive worker and pump.
        std::atomic<int> sliceId{-1};
        std::atomic<quint32> generation{0};
        // Bytes the receive worker has pushed, and how many of them
        // belonged to the previous slice in this slot when it last changed
        // (the change follows that slice's release, so none of its blocks
        // follow).
        std::atomic<quint64> pushedBytes{0};
        std::atomic<quint64> dropUntilBytes{0};
        // The Core stopped this slice's stream; silence until audio flows.
        std::atomic<bool> stopped{false};
        // Frames this slot's slice delivered since it was assigned.
        std::atomic<quint64> receivedFrames{0};
        std::unique_ptr<AudioRingSpsc<kHandoffBytes>> handoff;

        // GUI thread: why the slice's stream stopped, the frames it had
        // delivered then (the stop is news until it delivers more), and
        // when (for the order of stops()).
        QString stopReason;
        quint64 stopAtFrames = 0;
        quint64 stopOrder = 0;

        // Pump only.
        quint32 seenGeneration = 0;
        quint64 poppedBytes = 0;
        quint64 seenPushedBytes = 0;
        qint64 lastArrivalNs = 0;
        bool heard = false;
        bool wasLive = false;
    };

    Source* sourceFor(int sliceId);
    void dropHandoff();
    void dropHandoff(Source& source);
    void popHandoff(Source& source, void* into, std::size_t bytes);
    // A slice counted in the mix: sending, and heard within kQuietNs.
    bool isLive(const Source& source, qint64 now) const;
    // Drops the oldest `frames` of a slice's ring and counts them.
    void trimHandoff(Source& source, std::size_t frames);
    qint64 pausedNs() const;
    // Restarts the rate matcher, at the held ratio, with a ring of
    // `ringFrames` (the same: a reset).
    void restartMatcher(int ringFrames);
    void restartMatcher() { restartMatcher(m_matcherRingFrames); }
    // The Core went quiet: a restart, and a step smaller after
    // kShrinkAfterNs with no dropout.
    void quietRestart(qint64 now);
    // A live slice has a chunk waiting for the matcher.
    bool audioWaiting(qint64 now) const;
    unsigned liveSlots(qint64 now) const;
    // Takes this pump's matcher fill into the window; at the window's end,
    // moves the held ratio. Not during rmatch's startup.
    void holdRatio(const RemoteAudioRateMatcherStats& matcherStats, qint64 now);
    // Sums whole chunks from the live slices' rings into the matcher (or,
    // unpaced, straight to the output). Returns the frames moved.
    int drainHandoff(bool paced, qint64 now);
    void publish(State state, int queuedFrames);

    const int m_channel;
    VaxOutputPort m_port;
    Clock m_clock;

    std::array<Source, kMaxSources> m_sources;
    // Written by the GUI thread, read by the pump.
    std::atomic<quint32> m_generation{0};
    std::atomic<quint64> m_receivedFrames{0};
    std::atomic<quint64> m_droppedFrames{0};

    // Pump only.
    RemoteAudioRateMatcher m_matcher;
    bool m_matcherReady = false;
    bool m_matcherTried = false;
    int m_matcherRingFrames = kMatcherRingFrames;
    // An underflow waits to see whether the audio comes back within
    // kQuietNs (grow) or not (the source stopped).
    bool m_growPending = false;
    qint64 m_underflowNs = 0;
    // The slices live at that underflow (bit per m_sources slot): one that
    // has dropped out by the time audio is back stopped; it did not pause.
    unsigned m_liveAtUnderflow = 0;
    // Set by every restart: the first audio back is cut to one Opus packet.
    bool m_trimOnReturn = false;
    // The matcher has played since its last restart: only then is an
    // overflow a burst bigger than the ring rather than a first fill.
    bool m_playedSinceRestart = false;
    // The last dropout the reserve grew for (or was at its cap for).
    qint64 m_lastDropoutNs = 0;
    double m_ratio = 1.0;
    qint64 m_ratioWindowStartNs = -1;
    double m_ratioLevelSum = 0.0;
    int m_ratioLevelSamples = 0;
    int m_ratioFillReference = -1;
    int m_ratioWindowsAtCap = 0;
    QVector<float> m_chunk;
    QVector<float> m_part;
    QVector<float> m_matcherChunk;
    quint32 m_seenGeneration = 0;
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
    quint64 m_stopCount = 0;  // GUI thread: orders the stops
    quint64 m_trimmedFrames = 0;  // pump only

    std::mutex m_workerMutex;
    std::condition_variable m_workerWake;
    bool m_workerStop = false;
    std::thread m_worker;
};

} // namespace NereusSDR
