// =================================================================
// src/core/session/media/DaemonAudioSender.h  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  Packetisation for the bounded
// daemon audio capture bridge; no transport or session policy.
//
// Modification history (NereusSDR):
//   2026-10-10: R-AUD-SEND: drained by a thread of its own
//               (DaemonAudioSendThread), not by a timer on the owner's
//               thread; what the two threads share is under one mutex, and
//               packets go to a sink the owner installs.  J.J. Boyd
//               (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#pragma once

#include "core/session/media/DaemonAudioSource.h"
#include "core/session/media/OpusAudioCodec.h"
#include "core/session/media/PcmAudioCodec.h"
#include "core/NereusCoreExport.h"

#include <QList>
#include <QObject>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

namespace NereusSDR {

class AudioEngine;
class OpusAudioEncoder;

/// Packetisation diagnostics for one successful sender start.
/// `encodedPackets` means RTP construction succeeded (one per Opus block, ten
/// per lossless block); it says nothing about later transport acceptance or
/// network delivery. `encodeFailures` counts blocks.
struct DaemonAudioSenderTelemetry {
    DaemonAudioSourceTelemetry source;
    std::uint64_t consumedBlocks = 0;
    std::uint64_t encodedPackets = 0;
    std::uint64_t encodeFailures = 0;
    // Lossless ticks that came late enough to earn more than the per-tick
    // cap (over 20 ms after the previous tick) and sent the whole cap,
    // kMaxLosslessPacketsPerDrain. The Core's sender thread ran late with
    // audio waiting: a slow Core, which a bad link cannot cause (the sender
    // never waits for the network).
    std::uint64_t losslessCappedTicks = 0;
    // R-AUD-SEND: how evenly the packets left. The longest interval, in
    // microseconds, between two consecutive packets handed to the sink
    // since start(), read on the draining thread from the sender's
    // monotonic clock; -1 until two packets have gone (the first packet
    // after start() opens no gap). A late sender thread shows here; network
    // jitter cannot. longestSendGapSinceTakenUs is the same maximum since
    // takeLongestSendGapSinceTakenUs() last took it.
    qint64 longestSendGapUs = -1;
    qint64 longestSendGapSinceTakenUs = -1;
    bool hasLastEmittedPacket = false;
    quint16 lastEmittedSequence = 0;
    quint32 lastEmittedTimestamp = 0;
    // R-R3-35: the newest block taken from capture. captureTimestamp is the
    // RTP time at the block's end (its timestamp plus 1920), and
    // captureNs the capture clock's reading when that end was captured. A
    // GUI maps its playback onto the Core's clock with this pair.
    bool hasCaptureStamp = false;
    quint32 captureTimestamp = 0;
    qint64 captureNs = 0;
};

/// Turns bounded post-master-mix (or, R-R3-43, one receiver's) blocks into
/// RTP packets: one Opus packet per
/// 1920-frame block, or (R-R3-23 lossless) ten L16 packets of 192 frames,
/// paced by elapsed time (kLosslessPacketsPer10Ms) and never more than
/// kMaxLosslessPacketsPerDrain in one tick.
///
/// Capture stays in DaemonAudioSource's DSP-safe bridge.  It deliberately
/// does not know peers, session epochs, mute policy, or transport. The Opus
/// encoder stays built while the lossless profile runs, so a return to Opus
/// needs no new encoder.
///
/// Threads (R-AUD-SEND). Two threads use one sender: its owner (start, stop,
/// the setters and the read-outs) and the one that calls drain(), in the
/// Core a DaemonAudioSendThread. The sender has no timer of its own: nothing
/// is encoded or sent unless drain() is called. Everything the two share is
/// under m_mutex. The contract:
///   - After stop() returns, no packet of that epoch reaches the sink, and
///     nextSequence() and nextTimestamp() are final for it.
///   - stop() waits at most for one block's encode and its sends: a drain
///     under way gives the mutex up after the block it is on.
///   - The mutex is never held while the capture tap is installed or
///     removed (which waits for the DSP thread), nor while packetReady is
///     emitted.
///   - The sink is called with the mutex held, on the draining thread. It
///     must not call back into this sender and must not wait for the owner.
///   - packetReady is a tap: emitted after the sink took the packet, with
///     the mutex released, on the draining thread. A tap from a drain on
///     another thread may therefore be seen after stop() returned; the sink
///     is the delivery.
class NEREUS_CORE_EXPORT DaemonAudioSender final : public QObject {
    Q_OBJECT
public:
    static constexpr int kDrainIntervalMs = 10;
    static constexpr int kMaxBlocksPerDrain = 4;
    /// R-R3-23 lossless pacing. The steady stream is 250 packets/s, 2.5 per
    /// 10 ms. Sending is allowed at three packets per 10 ms of elapsed time
    /// (300 packets/s), which leaves 20% headroom to catch up after a stall.
    /// The allowance follows the clock, not the tick count, because a timer
    /// tick may come late on a busy Core: a 16 ms tick earns 4.8 packets,
    /// and the fraction carries to the next tick, so pacing never falls
    /// behind the 250 packets/s the audio needs.
    static constexpr int kLosslessPacketsPer10Ms = 3;
    /// The most one tick may put on the wire, however late it is or however
    /// much is queued: 6 x 780 bytes, where an unpaced tick released up to
    /// 40 packets (four queued blocks) after a capture stall. Unused
    /// allowance never grows beyond it. Packets past the allowance wait, in
    /// order and without loss, for later ticks; the next block leaves the
    /// bounded capture queue only once the previous block has gone. A new
    /// capture epoch (stop, then start) discards what is still waiting, as
    /// it discards queued capture. Opus is not paced: one packet per block.
    static constexpr int kMaxLosslessPacketsPerDrain = 6;
    /// Monotonic nanoseconds for the lossless pacing allowance.
    using PacingClock = std::function<qint64()>;

    explicit DaemonAudioSender(AudioEngine* audioEngine, QObject* parent = nullptr);
    /// Encodes with `codecConfig` (R-R3-23: the Core's configured
    /// audio_bitrate for the speakers' and headphones mixes; R-R3-43: 48
    /// kbit/s for a receiver stream). An unsupported bitrate leaves the encoder unready, so
    /// start() fails and encoderProfile() is empty, as for any encoder that
    /// cannot initialise.
    DaemonAudioSender(AudioEngine* audioEngine, const OpusAudioCodecConfig& codecConfig,
                      QObject* parent = nullptr);
    ~DaemonAudioSender() override;

    bool start(quint32 ssrc, quint16 firstSequence, quint32 firstTimestamp);

    /// The profile the next start() runs (R-R3-23). Refused (false) while
    /// running: a profile change is a new capture epoch, so the caller stops,
    /// sets the profile and starts again, which flushes queued audio and
    /// begins at the next block boundary. Default Opus.
    bool setProfile(RemoteAudioProfile profile);
    RemoteAudioProfile profile() const noexcept;
    /// R-R3-43: capture one receiver's own audio instead of the master mix
    /// (DaemonAudioSource::setSliceSource; kMasterMix is the default).
    /// Refused while running. Blocks, packets and pacing are the same.
    bool setSliceSource(int sliceId);
    int sliceSource() const noexcept;
    /// iPhone app Task 76: the mixes from one owner mix of AudioEngine
    /// (DaemonAudioSource::setOwnerMix). Refused while running.
    bool setOwnerMix(int slot);
    /// Ends the capture epoch. Returns whether the sender was running, read
    /// under the same lock that stops it: an owner that needs to know asks
    /// here, not isRunning() first. isRunning() and the other read-outs take
    /// the mutex without telling a drain an owner is waiting, so a drain
    /// under way could send every block it has queued before such a call
    /// got in; stop() waits for one block at most.
    bool stop();
    bool isRunning() const noexcept;

    /// R-AUD-SEND: where each packet goes, called on the draining thread
    /// with this sender's mutex held (see the contract above). Null: packets
    /// are only announced by packetReady. May be set at any time; a packet
    /// in delivery finishes with the sink it began with.
    using PacketSink = std::function<void(const QByteArray& packet)>;
    void setPacketSink(PacketSink sink);
    /// R-AUD-SEND: what the capture bridge wakes when it queues a block
    /// (DaemonAudioSource::setBlockWake). Refused while running.
    bool setBlockWake(std::shared_ptr<DaemonAudioBlockWake> wake);

    quint16 nextSequence() const noexcept;
    quint32 nextTimestamp() const noexcept;

    /// The profile this sender's encoder runs. Empty only when the encoder
    /// failed to initialise, in which case start() fails too, so a
    /// successful start() always has a profile to announce.
    std::optional<OpusEncoderProfile> encoderProfile() const;
    /// The lossless profile the packetiser produces; always available.
    PcmEncoderProfile losslessProfile() const { return m_packetiser.profile(); }
    /// True when the selected profile's encoder can run.
    bool profileReady() const;

    /// Read-only diagnostics. The snapshot remains available after stop() and
    /// resets only after a later successful start().
    DaemonAudioSenderTelemetry telemetry() const noexcept;
    /// R-AUD-SEND: the longest send gap since this was last called (or
    /// since start()), which it then begins again; -1 when no gap was
    /// measured. For the one reader that reports it periodically.
    /// telemetry() shows the same figure without taking it.
    qint64 takeLongestSendGapSinceTakenUs() noexcept;

    /// One bounded consumer tick: the unit of work DaemonAudioSendThread
    /// repeats, and directly callable (tests, an explicit host loop). One
    /// caller at a time.
    void drain();
    /// Lossless packets built and not yet emitted (at most one block's ten
    /// less those already sent). Always zero for Opus.
    int pendingLosslessPackets() const noexcept;
    /// Test seam: the clock the lossless allowance reads. Takes effect at
    /// the next start(). Default: a monotonic QElapsedTimer.
    void setPacingClockForTest(PacingClock clock);
    /// R-R3-35: the clock capture times are read from (see
    /// DaemonAudioSource::setCaptureClock). Refused while running.
    bool setCaptureClock(DaemonAudioSource::CaptureClock clock);
#ifdef NEREUS_BUILD_TESTS
    /// Test seam, no production caller: owners now waiting in stop() for a
    /// drain to give way.
    int ownersWaitingForTest() const noexcept
    {
        return m_ownerWaiting.load(std::memory_order_acquire);
    }
    /// Test seam, the feeding thread only, no production caller: see
    /// DaemonAudioSource::handOverPendingForTest().
    void handOverPendingCaptureForTest()
    {
        if (m_source) {
            m_source->handOverPendingForTest();
        }
    }
#endif

signals:
    void packetReady(const QByteArray& packet);

private:
    struct PendingPacket {
        QByteArray packet;
        quint16 sequence{0};
        quint32 timestamp{0};
    };
    void drainLossless(quint64 drainGeneration);
    void noteCaptured(const DaemonAudioBlock& block, quint32 timestamp);
    void noteHandedOverLocked();
    bool runningLocked() const noexcept;
    bool profileReadyLocked() const;

    // Guards every member below it, between the owner and the draining
    // thread. Lock order: this, then whatever the sink takes; the owner
    // never holds it while taking another lock of its own.
    mutable std::mutex m_mutex;
    // Owners waiting in stop() or start(): a drain gives way to them after
    // the block it is on, and does not begin another.
    std::atomic<int> m_ownerWaiting{0};
    PacketSink m_sink;
    // The source's tap is installed and removed by the owner outside the
    // mutex, only while m_running is false; its ring is taken only by a
    // drain, with the mutex held and m_running true.
    std::unique_ptr<DaemonAudioSource> m_source;
    std::unique_ptr<OpusAudioEncoder> m_encoder;
    PcmAudioPacketiser m_packetiser;
    QList<PendingPacket> m_pendingLossless;
    PacingClock m_pacingClock;
    qint64 m_lastPacingNs{0};
    // When the previous packet of this run was handed to the sink.
    bool m_hasLastHandOver{false};
    qint64 m_lastHandOverNs{0};
    // The allowance in 1e-7 packet units (one packet is 10'000'000), so the
    // fraction a late tick earns carries to the next tick exactly.
    qint64 m_pacingAllowanceUnits{0};
    RemoteAudioProfile m_profile{RemoteAudioProfile::Opus};
    quint32 m_ssrc{0};
    quint32 m_baseTimestamp{0};
    quint16 m_nextSequence{0};
    quint32 m_nextTimestamp{0};
    quint64 m_lifecycleGeneration{0};
    DaemonAudioSenderTelemetry m_telemetry;
    bool m_running{false};
};

} // namespace NereusSDR
