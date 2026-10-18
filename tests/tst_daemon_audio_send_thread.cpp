// =================================================================
// tests/tst_daemon_audio_send_thread.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  R-AUD-SEND: the Core's receive audio
// is encoded and sent by a thread of its own.  These tests drive the real
// AudioEngine/MasterMixer capture path into a DaemonAudioSender drained by
// a DaemonAudioSendThread, and the controller's AudioSendRoute.
//
// Every wait here is for a condition (a packet recorded, a pass of the
// thread completed, an owner seen waiting).  The millisecond figures are
// upper bounds for a wait that would otherwise never end; nothing measured
// depends on them, and no sleep stands in for proof.
//
// Modification history (NereusSDR):
//   2026-10-10: created.  J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//               Claude Code.
// =================================================================

#include <QtTest>

#include "core/AudioEngine.h"
#include "core/session/media/DaemonAudioSendThread.h"
#include "core/session/media/DaemonAudioSender.h"
#include "core/session/media/DaemonAudioSource.h"
#include "core/session/media/DaemonMediaController.h"
#include "core/session/media/LibDataChannelMediaTransport.h"
#include "core/session/media/OpusAudioCodec.h"
#include "core/session/media/PcmAudioCodec.h"
#include "fakes/RecordingRtpSink.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <QSemaphore>
#include <QtEndian>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <set>
#include <thread>

using namespace NereusSDR;

namespace {

constexpr int kDspFrames = 64;
constexpr quint32 kSsrc = 0x6e657265U;
// The longest any wait for a condition may last before the test fails.
constexpr int kUpperBoundMs = 20'000;

QVector<float> stereoBlock(float left, float right)
{
    QVector<float> block(kDspFrames * 2);
    for (int frame = 0; frame < kDspFrames; ++frame) {
        block[frame * 2] = left;
        block[frame * 2 + 1] = right;
    }
    return block;
}

struct Harness {
    RadioModel radio;
    AudioEngine* engine{nullptr};
    int sliceA{-1};

    Harness()
    {
        radio.configureStreamPool(/*userDdcCount=*/5, /*maxSlices=*/5,
                                  /*defaultRateHz=*/192000);
        engine = radio.audioEngine();
        Q_ASSERT(engine != nullptr);
        engine->masterMixForTest().setRampFrames(1);
        engine->masterMixForTest().setSlewUpFrames(0);
        sliceA = radio.addSlice();
        Q_ASSERT(sliceA >= 0);
        engine->setSliceStreaming(sliceA, true);
    }

    // The DSP thread's part: one caller at a time.
    void feed(int frames, float level = 0.25f)
    {
        const QVector<float> a = stereoBlock(level, -level);
        for (int delivered = 0; delivered < frames; delivered += kDspFrames) {
            engine->rxBlockReady(sliceA, a.constData(), kDspFrames);
        }
    }
};

quint16 rtpSequence(const QByteArray& packet)
{
    return qFromBigEndian<quint16>(packet.constData() + 2);
}
quint32 rtpTimestamp(const QByteArray& packet)
{
    return qFromBigEndian<quint32>(packet.constData() + 4);
}
quint32 rtpSsrc(const QByteArray& packet)
{
    return qFromBigEndian<quint32>(packet.constData() + 8);
}

// A twelve-byte RTP header and four payload bytes.
QByteArray rtpPacket(quint32 ssrc, quint16 sequence, quint32 timestamp)
{
    QByteArray packet(16, '\0');
    packet[0] = char(0x80);
    packet[1] = char(111);
    qToBigEndian<quint16>(sequence, packet.data() + 2);
    qToBigEndian<quint32>(timestamp, packet.data() + 4);
    qToBigEndian<quint32>(ssrc, packet.data() + 8);
    return packet;
}

// What a sender's sink received, in order, with the thread of each packet.
class PacketLog final {
public:
    void add(const QByteArray& packet)
    {
        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            m_packets.append(packet);
            m_threads.append(std::this_thread::get_id());
        }
        m_changed.notify_all();
    }
    DaemonAudioSender::PacketSink sink()
    {
        return [this](const QByteArray& packet) { add(packet); };
    }
    int count() const
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return int(m_packets.size());
    }
    QList<QByteArray> packets() const
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_packets;
    }
    QList<std::thread::id> threads() const
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_threads;
    }
    // Returns as soon as `wanted` packets are there; false at the bound.
    bool waitForCount(int wanted, int boundMs = kUpperBoundMs) const
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_changed.wait_for(lock, std::chrono::milliseconds(boundMs),
                                  [&] { return m_packets.size() >= wanted; });
    }
    // Returns as soon as a packet with this SSRC is there.
    bool waitForSsrc(quint32 ssrc, int boundMs = kUpperBoundMs) const
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_changed.wait_for(lock, std::chrono::milliseconds(boundMs), [&] {
            return !m_packets.isEmpty() && rtpSsrc(m_packets.constLast()) == ssrc;
        });
    }

private:
    mutable std::mutex m_mutex;
    mutable std::condition_variable m_changed;
    QList<QByteArray> m_packets;
    QList<std::thread::id> m_threads;
};

// Two whole passes of the thread have completed since the call began, so a
// drain that began after it has returned.
bool twoPassesComplete(const DaemonAudioSendThread& thread)
{
    const quint64 before = thread.passes();
    QDeadlineTimer deadline(kUpperBoundMs);
    while (thread.passes() < before + 2) {
        if (deadline.hasExpired()) {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}

} // namespace

class TstDaemonAudioSendThread final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        OpusAudioEncoder encoder;
        if (!encoder.isReady()) {
            QSKIP("Opus encoder is unavailable in this build");
        }
    }

    // The wake itself: nothing pending times out; a notify is kept for the
    // next wait; many notifies before one wait are one wake.
    void aNotifyBeforeTheWaitIsNotLost()
    {
        DaemonAudioBlockWake wake;
        QVERIFY(!wake.wait(0));
        wake.notify();
        QVERIFY(wake.wait(0));
        QVERIFY(!wake.wait(0));
        for (int i = 0; i < 5; ++i) {
            wake.notify();
        }
        QVERIFY(wake.wait(0));
        QVERIFY(!wake.wait(0));
    }

    // The capture bridge tells the wake when it queues a block, and only
    // then: a block's worth of audio less one DSP callback wakes nothing.
    void theSourceWakesWhenItQueuesABlock()
    {
        Harness h;
        const auto wake = std::make_shared<DaemonAudioBlockWake>();
        DaemonAudioSource source;
        source.setAudioEngine(h.engine);
        QVERIFY(source.setBlockWake(wake));
        source.start();
        QVERIFY(source.isRunning());
        // Refused while running: the DSP thread reads it.
        QVERIFY(!source.setBlockWake(nullptr));

        h.feed(DaemonAudioSource::kBlockFrames - kDspFrames);
        QVERIFY(!wake->wait(0));
        h.feed(kDspFrames);
        QVERIFY(wake->wait(0));
        QVERIFY(source.takeBlock().has_value());
        QVERIFY(!wake->wait(0));
        source.stop();
    }

    // The fault this lane fixes. The owner's thread does nothing but wait
    // for the feeder (it runs no event loop), the feeder completes twelve
    // blocks, three times what the capture ring holds, and every one is
    // sent, in order, with nothing dropped. The feeder gives each block
    // time only until it has been sent, which is a condition the owner's
    // thread cannot bring about while it is held.
    void aHeldOwnerThreadLosesNoBlock()
    {
        constexpr int kBlocks = 12;
        constexpr quint16 kFirstSequence = 65530;   // wraps on the way
        constexpr quint32 kFirstTimestamp = 0xffffe000U;
        Harness h;
        PacketLog log;
        DaemonAudioSender sender(h.engine);
        DaemonAudioSendThread thread;
        sender.setPacketSink(log.sink());
        QVERIFY(sender.setBlockWake(thread.wake()));
        thread.add(&sender);
        QVERIFY(sender.start(kSsrc, kFirstSequence, kFirstTimestamp));

        std::atomic<bool> keptUp{true};
        std::thread feeder([&] {
            for (int block = 0; block < kBlocks; ++block) {
                h.feed(DaemonAudioSource::kBlockFrames);
                // Once the sender has fallen behind, the rest is fed
                // without waiting, as a DSP thread would.
                if (keptUp.load() && !log.waitForCount(block + 1, 2'000)) {
                    keptUp.store(false);
                }
            }
        });
        // Held: the owner's thread is here until the feeder is done.
        feeder.join();

        QVERIFY2(keptUp.load(), "a block was not sent while the owner's thread was held");
        QCOMPARE(log.count(), kBlocks);
        const QList<QByteArray> packets = log.packets();
        for (int index = 0; index < kBlocks; ++index) {
            QCOMPARE(rtpSsrc(packets.at(index)), kSsrc);
            QCOMPARE(rtpSequence(packets.at(index)), static_cast<quint16>(kFirstSequence + index));
            QCOMPARE(rtpTimestamp(packets.at(index)),
                     static_cast<quint32>(kFirstTimestamp
                                          + quint32(index) * DaemonAudioSource::kBlockFrames));
        }
        for (const std::thread::id id : log.threads()) {
            QVERIFY(id != std::this_thread::get_id());
        }
        const DaemonAudioSenderTelemetry telemetry = sender.telemetry();
        QCOMPARE(telemetry.source.ringFullDrops, std::uint64_t{0});
        QCOMPARE(telemetry.source.sourceDropEvents, std::uint64_t{0});
        QCOMPARE(telemetry.consumedBlocks, std::uint64_t{kBlocks});
        QCOMPARE(telemetry.encodedPackets, std::uint64_t{kBlocks});
        sender.stop();
        thread.remove(&sender);
    }

    // The thread wakes at the block, not at its tick. The tick is set far
    // beyond anything this test waits for, so the one way a queued block
    // can reach the sink within the bound is the capture bridge's wake.
    void aQueuedBlockWakesTheThreadWithoutItsTick()
    {
        constexpr int kTickMs = 60'000;
        static_assert(kUpperBoundMs * 3 <= kTickMs);
        Harness h;
        PacketLog log;
        DaemonAudioSender sender(h.engine);
        DaemonAudioSendThread thread;
        thread.setTickMsForTest(kTickMs);
        sender.setPacketSink(log.sink());
        QVERIFY(sender.setBlockWake(thread.wake()));
        thread.add(&sender);
        QVERIFY(sender.start(kSsrc, 1, 0));
        // The pass add() itself asks for is over: the thread now waits for
        // a block or for its tick, a minute away.
        QDeadlineTimer firstPass(kUpperBoundMs);
        while (thread.passes() == 0 && !firstPass.hasExpired()) {
            std::this_thread::yield();
        }
        QCOMPARE(thread.passes(), quint64{1});
        QCOMPARE(log.count(), 0);

        h.feed(DaemonAudioSource::kBlockFrames);
        QVERIFY2(log.waitForCount(1), "the queued block did not wake the sender thread");
        QCOMPARE(log.count(), 1);
        sender.stop();
        thread.remove(&sender);
    }

    // stop() while the thread is inside a drain: it waits for the block
    // under way and no more (two further blocks are queued), nothing is
    // delivered after it returns, and the next sequence and timestamp are
    // those after the last packet delivered.
    void stopDuringADrainEndsTheEpochAtTheBlockUnderWay()
    {
        constexpr quint16 kFirstSequence = 40;
        constexpr quint32 kFirstTimestamp = 7'000;
        Harness h;
        PacketLog log;
        QSemaphore entered;
        QSemaphore gate;
        DaemonAudioSender sender(h.engine);
        DaemonAudioSendThread thread;
        sender.setPacketSink([&](const QByteArray& packet) {
            entered.release();
            gate.acquire();
            log.add(packet);
        });
        QVERIFY(sender.setBlockWake(thread.wake()));
        thread.add(&sender);
        QVERIFY(sender.start(kSsrc, kFirstSequence, kFirstTimestamp));
        h.feed(DaemonAudioSource::kBlockFrames * 3);

        // The thread is inside its drain, in the first block's send.
        QVERIFY(entered.tryAcquire(1, kUpperBoundMs));
        std::atomic<bool> stopReturned{false};
        std::atomic<int> deliveredAtStop{-1};
        std::thread stopper([&] {
            sender.stop();
            deliveredAtStop.store(log.count());
            stopReturned.store(true);
        });
        // The owner is waiting in stop() before the send is let go.
        QDeadlineTimer waiting(kUpperBoundMs);
        while (sender.ownersWaitingForTest() == 0 && !waiting.hasExpired()) {
            std::this_thread::yield();
        }
        QCOMPARE(sender.ownersWaitingForTest(), 1);
        QVERIFY(!stopReturned.load());

        // One send is let go: the block under way. stop() returns on that
        // alone; a drain that went on to the next block would wait here for
        // a second release that only comes after the bound.
        gate.release(1);
        QDeadlineTimer prompt(kUpperBoundMs);
        while (!stopReturned.load() && !prompt.hasExpired()) {
            std::this_thread::yield();
        }
        const bool returnedOnOneBlock = stopReturned.load();
        gate.release(100);
        stopper.join();
        QVERIFY2(returnedOnOneBlock, "stop() waited for more than the block under way");
        QCOMPARE(deliveredAtStop.load(), 1);

        // Nothing follows, however many passes the thread makes.
        QVERIFY(twoPassesComplete(thread));
        QCOMPARE(log.count(), 1);
        QVERIFY(!sender.isRunning());
        const QByteArray last = log.packets().constLast();
        QCOMPARE(rtpSequence(last), kFirstSequence);
        QCOMPARE(rtpTimestamp(last), kFirstTimestamp);
        QCOMPARE(sender.nextSequence(), static_cast<quint16>(kFirstSequence + 1));
        QCOMPARE(sender.nextTimestamp(),
                 static_cast<quint32>(kFirstTimestamp + DaemonAudioSource::kBlockFrames));
        const DaemonAudioSenderTelemetry telemetry = sender.telemetry();
        QCOMPARE(telemetry.consumedBlocks, std::uint64_t{1});
        QCOMPARE(telemetry.encodedPackets, std::uint64_t{1});
        thread.remove(&sender);
    }

    // After remove() the thread never touches the sender: it can be
    // destroyed while the thread goes on serving another.
    void aRemovedSenderIsLeftAlone()
    {
        Harness h;
        PacketLog kept;
        PacketLog removed;
        DaemonAudioSendThread thread;
        DaemonAudioSender keptSender(h.engine);
        keptSender.setPacketSink(kept.sink());
        QVERIFY(keptSender.setBlockWake(thread.wake()));
        thread.add(&keptSender);
        {
            DaemonAudioSender removedSender(h.engine);
            removedSender.setSliceSource(h.sliceA);
            removedSender.setPacketSink(removed.sink());
            QVERIFY(removedSender.setBlockWake(thread.wake()));
            thread.add(&removedSender);
            QVERIFY(removedSender.start(kSsrc + 1, 1, 0));
            QVERIFY(keptSender.start(kSsrc, 1, 0));
            h.feed(DaemonAudioSource::kBlockFrames);
            QVERIFY(kept.waitForCount(1));
            QVERIFY(removed.waitForCount(1));
            removedSender.stop();
            thread.remove(&removedSender);
        }
        h.feed(DaemonAudioSource::kBlockFrames);
        QVERIFY(kept.waitForCount(2));
        QVERIFY(twoPassesComplete(thread));
        QCOMPARE(removed.count(), 1);
        keptSender.stop();
        thread.remove(&keptSender);
    }

    // Lossless pacing keeps its meaning on the thread. The thread ticks on
    // its own; what a tick may send follows the injected clock alone: none
    // while it stands still, three for 10 ms, the cap of six for a tick
    // that came late (counted), and what is left after.
    void losslessPacingFollowsTheInjectedClockOnTheThread()
    {
        constexpr quint16 kFirstSequence = 500;
        constexpr quint32 kFirstTimestamp = 90'000;
        Harness h;
        PacketLog log;
        std::atomic<qint64> nowNs{1'000'000'000};
        DaemonAudioSender sender(h.engine);
        DaemonAudioSendThread thread;
        QVERIFY(sender.setProfile(RemoteAudioProfile::Lossless));
        sender.setPacingClockForTest([&nowNs] { return nowNs.load(); });
        sender.setPacketSink(log.sink());
        QVERIFY(sender.setBlockWake(thread.wake()));
        thread.add(&sender);
        QVERIFY(sender.start(kSsrc, kFirstSequence, kFirstTimestamp));
        h.feed(DaemonAudioSource::kBlockFrames);

        // No time has passed: nothing is earned, the block stays queued.
        QVERIFY(twoPassesComplete(thread));
        QCOMPARE(log.count(), 0);
        QCOMPARE(sender.telemetry().consumedBlocks, std::uint64_t{0});

        nowNs.fetch_add(10'000'000);
        QVERIFY(log.waitForCount(3));
        QVERIFY(twoPassesComplete(thread));
        QCOMPARE(log.count(), 3);
        QCOMPARE(sender.pendingLosslessPackets(), 7);
        QCOMPARE(sender.telemetry().losslessCappedTicks, std::uint64_t{0});

        // 30 ms at once would earn nine: the cap sends six and counts it.
        nowNs.fetch_add(30'000'000);
        QVERIFY(log.waitForCount(9));
        QVERIFY(twoPassesComplete(thread));
        QCOMPARE(log.count(), 9);
        QCOMPARE(sender.pendingLosslessPackets(), 1);
        QCOMPARE(sender.telemetry().losslessCappedTicks, std::uint64_t{1});

        nowNs.fetch_add(10'000'000);
        QVERIFY(log.waitForCount(10));
        QVERIFY(twoPassesComplete(thread));
        QCOMPARE(log.count(), 10);
        QCOMPARE(sender.pendingLosslessPackets(), 0);
        QCOMPARE(sender.telemetry().losslessCappedTicks, std::uint64_t{1});

        const QList<QByteArray> packets = log.packets();
        for (int index = 0; index < packets.size(); ++index) {
            QCOMPARE(packets.at(index).size(), PcmAudioCodecConfig::kRtpPacketBytes);
            QCOMPARE(rtpSequence(packets.at(index)), static_cast<quint16>(kFirstSequence + index));
            QCOMPARE(rtpTimestamp(packets.at(index)),
                     static_cast<quint32>(kFirstTimestamp
                                          + quint32(index) * PcmAudioCodecConfig::kPacketFrames));
        }
        QCOMPARE(sender.nextSequence(), static_cast<quint16>(kFirstSequence + 10));
        QCOMPARE(sender.nextTimestamp(),
                 static_cast<quint32>(kFirstTimestamp + DaemonAudioSource::kBlockFrames));
        sender.stop();
        thread.remove(&sender);
    }

    // Three threads at once: a feeder in the DSP thread's place, the sender
    // thread draining, and the owner reading telemetry, stopping and
    // starting. Each start is an epoch with its own SSRC. Within an epoch
    // the packets are in order, none arrives after its stop() returned,
    // and the sender's own counts agree with what the sink received.
    void ownerStartsAndStopsWhileTheThreadDrains()
    {
        constexpr int kEpochs = 60;
        struct Epoch {
            quint32 ssrc{0};
            quint16 firstSequence{0};
            quint32 firstTimestamp{0};
            int deliveredAtStop{0};
            quint16 nextSequence{0};
            quint32 nextTimestamp{0};
            DaemonAudioSenderTelemetry telemetry;
        };
        Harness h;
        PacketLog log;
        DaemonAudioSender sender(h.engine);
        DaemonAudioSendThread thread;
        sender.setPacketSink(log.sink());
        QVERIFY(sender.setBlockWake(thread.wake()));
        thread.add(&sender);

        std::atomic<bool> feeding{true};
        std::thread feeder([&] {
            while (feeding.load()) {
                h.feed(kDspFrames);
                std::this_thread::yield();
            }
        });
        QList<Epoch> epochs;
        bool everyEpochSent = true;
        bool everyStartTaken = true;
        for (int index = 0; index < kEpochs; ++index) {
            Epoch epoch;
            epoch.ssrc = kSsrc + quint32(index);
            epoch.firstSequence = static_cast<quint16>(65'000 + index * 37);
            epoch.firstTimestamp = 0xfff00000U + quint32(index) * 1'000'003U;
            everyStartTaken = sender.start(epoch.ssrc, epoch.firstSequence, epoch.firstTimestamp)
                && everyStartTaken;
            // At least one packet of this epoch before it is stopped.
            everyEpochSent = log.waitForSsrc(epoch.ssrc) && everyEpochSent;
            (void)sender.telemetry();
            (void)sender.isRunning();
            sender.stop();
            epoch.deliveredAtStop = log.count();
            epoch.nextSequence = sender.nextSequence();
            epoch.nextTimestamp = sender.nextTimestamp();
            epoch.telemetry = sender.telemetry();
            epochs.append(epoch);
        }
        feeding.store(false);
        feeder.join();
        QVERIFY(twoPassesComplete(thread));
        thread.remove(&sender);
        QVERIFY(everyStartTaken);
        QVERIFY(everyEpochSent);

        const QList<QByteArray> packets = log.packets();
        // Nothing arrived after the last stop() returned.
        QCOMPARE(int(packets.size()), epochs.constLast().deliveredAtStop);
        int position = 0;
        for (const Epoch& epoch : epochs) {
            // Every packet up to this epoch's stop() is this epoch's: none
            // of it follows the stop, and none of the next precedes it.
            int count = 0;
            quint32 lastTimestamp = 0;
            for (; position < epoch.deliveredAtStop; ++position, ++count) {
                const QByteArray& packet = packets.at(position);
                QCOMPARE(rtpSsrc(packet), epoch.ssrc);
                QCOMPARE(rtpSequence(packet), static_cast<quint16>(epoch.firstSequence + count));
                // The capture position is the RTP clock: whole blocks on
                // from the epoch's base, later each time (a full ring's
                // dropped block is a gap, never a step back).
                const quint32 offset = rtpTimestamp(packet) - epoch.firstTimestamp;
                QCOMPARE(offset % quint32(DaemonAudioSource::kBlockFrames), quint32{0});
                if (count > 0) {
                    QVERIFY(static_cast<qint32>(rtpTimestamp(packet) - lastTimestamp)
                            >= DaemonAudioSource::kBlockFrames);
                }
                lastTimestamp = rtpTimestamp(packet);
            }
            QVERIFY(count >= 1);
            QCOMPARE(epoch.telemetry.encodedPackets, std::uint64_t(count));
            QCOMPARE(epoch.telemetry.encodeFailures, std::uint64_t{0});
            QCOMPARE(epoch.telemetry.consumedBlocks, std::uint64_t(count));
            QCOMPARE(epoch.nextSequence, static_cast<quint16>(epoch.firstSequence + count));
            QCOMPARE(epoch.nextTimestamp,
                     static_cast<quint32>(lastTimestamp + DaemonAudioSource::kBlockFrames));
            QVERIFY(epoch.telemetry.hasLastEmittedPacket);
            QCOMPARE(epoch.telemetry.lastEmittedSequence,
                     static_cast<quint16>(epoch.firstSequence + count - 1));
            QCOMPARE(epoch.telemetry.lastEmittedTimestamp, lastTimestamp);
        }
        QCOMPARE(position, int(packets.size()));
        for (const std::thread::id id : log.threads()) {
            QVERIFY(id != std::this_thread::get_id());
        }
    }

    // ── The controller's route ──────────────────────────────────────────

    // A route changed to another peer's sink between two packets sends
    // each exactly once, to the sink that was current for it.
    void routeSwapBetweenPacketsSendsEachOnceToTheRightSink()
    {
        const auto first = std::make_shared<Test::RecordingRtpSink>();
        const auto second = std::make_shared<Test::RecordingRtpSink>();
        first->ready = true;
        second->ready = true;
        DaemonMediaController::AudioSendRoute route;
        const QByteArray one = rtpPacket(0x10, 1, 100);
        const QByteArray two = rtpPacket(0x10, 2, 200);
        {
            const std::lock_guard<std::mutex> lock(route.mutex);
            route.current = first;
        }
        route.deliver(one);
        {
            const std::lock_guard<std::mutex> lock(route.mutex);
            route.current = second;
        }
        route.deliver(two);
        QCOMPARE(first->packets.snapshot(), QList<QByteArray>{one});
        QCOMPARE(second->packets.snapshot(), QList<QByteArray>{two});
        QCOMPARE(route.attempts.load(), std::uint64_t{2});
        QCOMPARE(route.accepted.load(), std::uint64_t{2});
        QCOMPARE(route.rejected.load(), std::uint64_t{0});
    }

    // The same with the two threads really running: the sender thread
    // delivers a run of packets while the owner swaps the sink back and
    // forth. Every packet reaches exactly one sink, in order within it.
    void routeSwapUnderDeliveryLosesAndRepeatsNothing()
    {
        constexpr int kPackets = 4'000;
        const auto first = std::make_shared<Test::RecordingRtpSink>();
        const auto second = std::make_shared<Test::RecordingRtpSink>();
        first->ready = true;
        second->ready = true;
        DaemonMediaController::AudioSendRoute route;
        {
            const std::lock_guard<std::mutex> lock(route.mutex);
            route.current = first;
        }
        std::atomic<bool> delivering{true};
        std::thread deliverer([&] {
            for (int index = 0; index < kPackets; ++index) {
                route.deliver(rtpPacket(0x10, static_cast<quint16>(index), quint32(index)));
            }
            delivering.store(false);
        });
        int swaps = 0;
        while (delivering.load()) {
            const std::lock_guard<std::mutex> lock(route.mutex);
            route.current = (++swaps % 2 == 0) ? first : second;
        }
        deliverer.join();

        std::set<int> seen;
        for (const auto& sink : {first, second}) {
            int previous = -1;
            for (const QByteArray& packet : sink->packets.snapshot()) {
                const int sequence = rtpSequence(packet);
                QVERIFY(sequence > previous);
                previous = sequence;
                QVERIFY(seen.insert(sequence).second);
            }
        }
        QCOMPARE(int(seen.size()), kPackets);
        QCOMPARE(route.attempts.load(), std::uint64_t{kPackets});
        QCOMPARE(route.accepted.load(), std::uint64_t{kPackets});
        QCOMPARE(route.rejected.load(), std::uint64_t{0});
    }

    // A closed route sends nothing and counts nothing.
    void aClosedRouteSendsNothing()
    {
        const auto sink = std::make_shared<Test::RecordingRtpSink>();
        const auto replacement = std::make_shared<Test::RecordingRtpSink>();
        sink->ready = true;
        replacement->ready = true;
        DaemonMediaController::AudioSendRoute route;
        // Never opened.
        route.deliver(rtpPacket(0x10, 1, 100));
        QCOMPARE(route.attempts.load(), std::uint64_t{0});
        {
            const std::lock_guard<std::mutex> lock(route.mutex);
            route.current = sink;
            route.replacement = replacement;
            route.replacementFrom = {0x10};
            route.replacementTo = {0x30};
        }
        route.deliver(rtpPacket(0x10, 2, 200));
        QCOMPARE(sink->packets.size(), 1);
        QCOMPARE(replacement->packets.size(), 1);
        route.close();
        route.deliver(rtpPacket(0x10, 3, 300));
        QCOMPARE(sink->packets.size(), 1);
        QCOMPARE(replacement->packets.size(), 1);
        QCOMPARE(sink->attempts.load(), 1);
        QCOMPARE(replacement->attempts.load(), 1);
        QCOMPARE(route.attempts.load(), std::uint64_t{1});
        QCOMPARE(route.accepted.load(), std::uint64_t{1});
        route.resetCounters();
        QCOMPARE(route.attempts.load(), std::uint64_t{0});
        QCOMPARE(route.accepted.load(), std::uint64_t{0});
        QCOMPARE(route.rejected.load(), std::uint64_t{0});
    }

    // A transport that has stopped leaves its sink valid, and the sink
    // answers "not ready": the route counts nothing, as when the
    // controller's thread skipped a peer that was not ready. The real
    // transport, never connected, and stopped.
    void aStoppedTransportsSinkIsNotReadyAndIsNotCounted()
    {
        using Result = MediaRtpSink::RtpSendResult;
        std::shared_ptr<MediaRtpSink> sink;
        DaemonMediaController::AudioSendRoute route;
        {
            LibDataChannelMediaTransport transport;
            sink = transport.audioRtpSink();
            QVERIFY(sink != nullptr);
            // Never connected: not ready before it stops, too.
            QCOMPARE(sink->sendRtp(rtpPacket(0x10, 0, 0)), Result::NotReady);
            transport.stop();
            QCOMPARE(sink->sendRtp(rtpPacket(0x10, 0, 0)), Result::NotReady);
            {
                const std::lock_guard<std::mutex> lock(route.mutex);
                route.current = sink;
            }
            route.deliver(rtpPacket(0x10, 1, 100));
            QCOMPARE(route.attempts.load(), std::uint64_t{0});
            QCOMPARE(route.accepted.load(), std::uint64_t{0});
            QCOMPARE(route.rejected.load(), std::uint64_t{0});
        }
        // The transport is gone; the sink the route still holds is valid.
        QCOMPARE(sink->sendRtp(rtpPacket(0x10, 0, 0)), Result::NotReady);
        route.deliver(rtpPacket(0x10, 2, 200));
        QCOMPARE(route.attempts.load(), std::uint64_t{0});
        QCOMPARE(route.accepted.load(), std::uint64_t{0});
        QCOMPARE(route.rejected.load(), std::uint64_t{0});
    }

    // A ready transport that will not take a packet is a refusal: an
    // attempt, and rejected. One that is not ready is neither.
    void aReadyTransportsRefusalIsCountedRejected()
    {
        const auto sink = std::make_shared<Test::RecordingRtpSink>();
        DaemonMediaController::AudioSendRoute route;
        {
            const std::lock_guard<std::mutex> lock(route.mutex);
            route.current = sink;
        }
        sink->ready = true;
        sink->accept = false;
        route.deliver(rtpPacket(0x10, 1, 100));
        QCOMPARE(sink->attempts.load(), 1);
        QCOMPARE(sink->packets.size(), 0);
        QCOMPARE(route.attempts.load(), std::uint64_t{1});
        QCOMPARE(route.accepted.load(), std::uint64_t{0});
        QCOMPARE(route.rejected.load(), std::uint64_t{1});

        sink->ready = false;
        route.deliver(rtpPacket(0x10, 2, 200));
        QCOMPARE(sink->attempts.load(), 2);
        QCOMPARE(route.attempts.load(), std::uint64_t{1});
        QCOMPARE(route.accepted.load(), std::uint64_t{0});
        QCOMPARE(route.rejected.load(), std::uint64_t{1});
    }

    // During a replacement the new peer gets a copy with the same sequence
    // number and timestamp under its own SSRC for that stream; the current
    // peer gets the packet under its own (a sender started with an earlier
    // peer still stamps that peer's). The count is the current peer's.
    void theReplacementCopyCarriesTheMappedSsrc()
    {
        constexpr quint32 kStamped = 0x1111;      // the peer the sender started with
        constexpr quint32 kCurrentMain = 0x2221;
        constexpr quint32 kCurrentHeadphones = 0x2226;
        constexpr quint32 kNewMain = 0x3331;
        constexpr quint32 kNewHeadphones = 0x3336;
        const auto current = std::make_shared<Test::RecordingRtpSink>();
        const auto replacement = std::make_shared<Test::RecordingRtpSink>();
        current->ready = true;
        replacement->ready = true;
        DaemonMediaController::AudioSendRoute route;
        {
            const std::lock_guard<std::mutex> lock(route.mutex);
            route.current = current;
            route.rewrite = {{kStamped, kCurrentMain}};
            route.replacement = replacement;
            // Stream order: main, receivers 0 to 3 (none here), headphones.
            route.replacementFrom = {kCurrentMain, 0, 0, 0, 0, kCurrentHeadphones};
            route.replacementTo = {kNewMain, 0, 0, 0, 0, kNewHeadphones};
        }
        route.deliver(rtpPacket(kStamped, 77, 123'456));
        route.deliver(rtpPacket(kCurrentHeadphones, 9, 4'242));
        // A stream the new peer has no SSRC for is not copied.
        route.deliver(rtpPacket(0x7777, 5, 55));

        const QList<QByteArray> sent = current->packets.snapshot();
        QCOMPARE(sent.size(), 3);
        QCOMPARE(rtpSsrc(sent.at(0)), kCurrentMain);
        QCOMPARE(rtpSequence(sent.at(0)), quint16{77});
        QCOMPARE(rtpTimestamp(sent.at(0)), quint32{123'456});
        QCOMPARE(rtpSsrc(sent.at(1)), kCurrentHeadphones);
        QCOMPARE(rtpSsrc(sent.at(2)), quint32{0x7777});

        const QList<QByteArray> copied = replacement->packets.snapshot();
        QCOMPARE(copied.size(), 2);
        QCOMPARE(rtpSsrc(copied.at(0)), kNewMain);
        QCOMPARE(rtpSequence(copied.at(0)), quint16{77});
        QCOMPARE(rtpTimestamp(copied.at(0)), quint32{123'456});
        QCOMPARE(copied.at(0).mid(12), sent.at(0).mid(12));
        QCOMPARE(rtpSsrc(copied.at(1)), kNewHeadphones);
        QCOMPARE(rtpSequence(copied.at(1)), quint16{9});
        QCOMPARE(rtpTimestamp(copied.at(1)), quint32{4'242});

        QCOMPARE(route.attempts.load(), std::uint64_t{3});
        QCOMPARE(route.accepted.load(), std::uint64_t{3});
    }
};

QTEST_MAIN(TstDaemonAudioSendThread)
#include "tst_daemon_audio_send_thread.moc"
