// no-port-check: NereusSDR-original.
// =================================================================
// src/core/session/media/DaemonAudioSource.h  (NereusSDR)
// =================================================================
// Bounded bridge from AudioEngine's borrowed post-master-mix callback, or
// (R-R3-43) one receiver's own tap, or (R-R3-45) the speakers' mix alone or
// the headphones mix, to owned 40 ms stereo blocks for the remote audio
// sender.
//
// Modification history (NereusSDR):
//   2026-10-10: R-AUD-SEND: DaemonAudioBlockWake, told on the DSP thread
//               the moment a block is queued, so the thread that sends it
//               wakes then instead of at its next tick. J.J. Boyd
//               (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#pragma once

#include <QObject>
#include <QPointer>
#include <QSemaphore>
#include <QVector>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace NereusSDR {

class AudioEngine;

// One owned Opus-profile capture block. samplePosition is the first 48 kHz
// stereo frame's position since this source was started. It advances across
// every valid-rate ingress callback, including ones the bounded bridge drops.
// Completed blocks resume only on the original 1,920-frame grid after a
// partial ingress loss, making that loss visible as an integer packet gap in
// RTP without exposing an unaligned packet timestamp.
//
// capturedNs (R-R3-35) is the capture clock's reading when the block's last
// frame reached the bridge, that is when the block was complete. Sample
// position samplePosition + kBlockFrames is the one captured at that time.
struct DaemonAudioBlock {
    QVector<float> pcmInterleaved;
    quint64 samplePosition = 0;
    qint64 capturedNs = 0;
};

/// Snapshot of capture-bridge activity for one source lifetime.  Frames count
/// valid-rate master-mix ingress.  sourceDropEvents is the sum of the three
/// loss counters (contentionLosses + ringFullDrops + invalidIngressDrops);
/// contentionRetries records deferred hand-overs that lost no audio.  Drop
/// events are bridge events, deliberately not a packet-loss percentage.
struct DaemonAudioSourceTelemetry {
    std::uint64_t capturedValidRateFrames = 0;
    std::uint64_t sourceDropEvents = 0;
    // Failed non-blocking hand-over attempts of a finished packet because the
    // consumer held the bridge lock. Each one is retried; no audio is lost.
    std::uint64_t contentionRetries = 0;
    // Finished packets lost because the previous one was still waiting for
    // the lock when this one completed.
    std::uint64_t contentionLosses = 0;
    // Finished packets dropped because the bounded ready ring was full.
    std::uint64_t ringFullDrops = 0;
    // Rejected ingress callbacks (null, empty or wrong-rate), plus the
    // injected ingress loss of dropIngressForTest().
    std::uint64_t invalidIngressDrops = 0;
};

/// R-AUD-SEND: wakes the thread that takes a source's blocks
/// (DaemonAudioSendThread) the moment one is queued. Several sources may
/// share one.
///
/// notify() runs on the DSP thread, under the audio tap contract
/// (AudioEngine.h, MasterMixAudioTap: no blocking, no allocation). It is
/// one atomic exchange, and only when no wake is already pending one
/// QSemaphore::release(). Qt 6's QSemaphore is a futex wherever the system
/// has one (qsemaphore.cpp; qfutex_p.h: futex on Linux, __ulock_wake on
/// macOS in qfutex_mac_p.h, WakeByAddress on Windows): release() is an
/// atomic add, and a wake system call only while the waiter is asleep. It
/// takes no lock and allocates nothing. A condition variable was not used:
/// its notify without the waiter's mutex can be lost between the waiter's
/// check and its sleep, and with the mutex the DSP thread could block.
class DaemonAudioBlockWake final {
public:
    DaemonAudioBlockWake() = default;
    DaemonAudioBlockWake(const DaemonAudioBlockWake&) = delete;
    DaemonAudioBlockWake& operator=(const DaemonAudioBlockWake&) = delete;

    /// Any thread; never blocks.
    void notify() noexcept
    {
        if (!m_pending.exchange(true, std::memory_order_acq_rel)) {
            m_semaphore.release();
        }
    }

    /// The waiting thread: returns at a notify(), or after timeoutMs. A
    /// block queued before this returns is there for the caller to take; a
    /// notify() after it makes the next wait return at once.
    bool wait(int timeoutMs)
    {
        const bool notified = m_semaphore.tryAcquire(1, timeoutMs);
        m_pending.store(false, std::memory_order_release);
        return notified;
    }

private:
    QSemaphore m_semaphore;
    std::atomic<bool> m_pending{false};
};

class DaemonAudioSource final : public QObject {
    Q_OBJECT

public:
    static constexpr int kSampleRateHz = 48000;
    static constexpr int kChannels = 2;
    static constexpr int kBlockFrames = 1920;  // 40 ms at 48 kHz
    static constexpr int kBlockSamples = kBlockFrames * kChannels;
    static constexpr int kQueueBlocks = 4;  // 160 ms maximum queued audio

    /// Monotonic nanoseconds. It is read on the DSP thread each time a
    /// block completes, so it must be safe to call from any thread and must
    /// never block.
    using CaptureClock = std::function<qint64()>;

    explicit DaemonAudioSource(QObject* parent = nullptr);
    ~DaemonAudioSource() override;

    /// R-R3-35: the clock DaemonAudioBlock::capturedNs is read from. Default
    /// std::chrono::steady_clock. Refused (false) while running, because the
    /// DSP thread reads it; set it before start(). An empty clock keeps the
    /// current one.
    bool setCaptureClock(CaptureClock clock);

    /// R-AUD-SEND: what to wake each time a block is queued (null, the
    /// default: nothing). Refused (false) while running, because the DSP
    /// thread reads it; set it before start().
    bool setBlockWake(std::shared_ptr<DaemonAudioBlockWake> wake);

    /// R-R3-43: which audio this source captures. kMasterMix (the default)
    /// is the master mix, exactly as before; a slice id >= 0 is that
    /// receiver's own audio from AudioEngine's slice tap (before mute, pan
    /// and the mix, with its AF gain undone as local VAX has it). Frames the
    /// MOX gate withholds advance the position without samples, so the loss
    /// shows as an integral packet gap, as dropped ingress does. Refused
    /// (false) while running, or for a negative id other than kMasterMix,
    /// kSpeakersMix or kHeadphonesMix.
    static constexpr int kMasterMix = -1;
    /// R-R3-45: the speakers' mix alone (the receivers routed to the
    /// speakers), from the master tap; and the headphones mix (the
    /// receivers routed to the headphones), from AudioEngine's
    /// headphones-mix tap. Both before master volume and mute.
    static constexpr int kSpeakersMix = -2;
    static constexpr int kHeadphonesMix = -3;
    bool setSliceSource(int sliceId);
    int sliceSource() const noexcept { return m_sliceId; }
    /// iPhone app Task 76 (ruling 9.2): with a slot >= 0 the mixes
    /// (kMasterMix, kSpeakersMix, kHeadphonesMix) come from that owner mix
    /// of AudioEngine (AudioEngine::acquireOwnerMix) instead of the master
    /// and headphones taps: one device's own slices. -1 (the default) keeps
    /// the master taps. A slice source is unaffected. Refused while running.
    bool setOwnerMix(int slot);
    int ownerMix() const noexcept { return m_ownerMix; }

    // The AudioEngine is non-owning. Changing engines stops capture first,
    // preventing a completed block from a previous station from surviving a
    // reconnect.
    void setAudioEngine(AudioEngine* audioEngine);
    AudioEngine* audioEngine() const noexcept;

    // Control-thread lifecycle. start() discards all prior partial and queued
    // audio before installing the synchronous tap. stop() detaches the tap
    // before clearing state, so no callback can append old audio afterward.
    // A slice source that finds every slice tap slot taken stays stopped.
    void start();
    void stop();
    bool isRunning() const noexcept;

    // Consumer-thread API. Returns one owned interleaved stereo block with its
    // true source frame position, or no block when stopped or the ring is
    // empty.
    std::optional<DaemonAudioBlock> takeBlock();

    // Total audio-loss events: rejected ingress, packets lost to lock
    // contention and completed blocks discarded because the fixed ring was
    // full (telemetry().sourceDropEvents). It is diagnostic only; audio loss
    // is expected under overload and never causes the DSP callback to wait.
    std::uint64_t dropCount() const noexcept;

    /// Read-only capture diagnostics. Values survive stop() and reset only on
    /// the next successful start(). Safe for the control thread to sample.
    DaemonAudioSourceTelemetry telemetry() const noexcept;

    // Test seam: models a valid master-mix callback whose source frames were
    // reserved but whose samples never reached the bridge. It does not
    // inject samples and is used to verify packet-grid recovery. Counted as
    // an invalid-ingress drop.
    void dropIngressForTest(int frames) noexcept;

#ifdef NEREUS_BUILD_TESTS
    // Deterministic race seam: runs on the consumer thread inside
    // takeBlock() while it holds the bridge lock, before the ring is read.
    void setTakeBlockLockedHookForTest(std::function<void()> hook);
    // Test seam, the producer's thread only, no production caller. A block
    // that completed while the consumer held the bridge lock stays with the
    // producer until its next callback (it never waits). A radio's DSP
    // thread calls again within a few milliseconds; a test that feeds a
    // fixed number of blocks and then stops never does, and the last block
    // would stay there. This is that next callback's hand-over, without its
    // samples: it returns once nothing is held back.
    void handOverPendingForTest();
#endif

private:
    class Bridge;

    void detachFromEngine();

    QPointer<AudioEngine> m_audioEngine;
    std::unique_ptr<Bridge> m_bridge;
    int m_sliceId{kMasterMix};
    int m_ownerMix{-1};
};

} // namespace NereusSDR
