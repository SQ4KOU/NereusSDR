// =================================================================
// tests/tst_capture_input_watch.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test.  The microphone helper's "the
// input is lost" watch (CaptureInputWatch): it counts the blocks the
// device delivers, so a window that stops reading the shared ring never
// makes a working microphone read as lost.  No audio device is opened;
// the clock is the test's own.
//
// Modification history (NereusSDR):
//   2026-10-12: created.  J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//               Claude Code.
// =================================================================

#include <QtTest/QtTest>

#include "core/audio/CaptureInputWatch.h"
#include "core/audio/DeviceRateMatcher.h"
#include "core/audio/MatcherRing.h"

#include <array>
#include <chrono>
#include <cstdint>

using namespace NereusSDR;

namespace {

using Clock = CaptureInputWatch::Clock;
using Verdict = CaptureInputWatch::Verdict;
using std::chrono::milliseconds;

constexpr auto kLostAfter = milliseconds(500);
constexpr int kBlockFrames = 64;
constexpr std::int64_t kBlockNs = 1'333'333;   // 64 frames at 48 kHz

// The helper's own matcher, as CaptureHelper::finishOpen builds it for a
// 48 kHz microphone with a 64-frame buffer.
DeviceRateMatcher::Config micConfig()
{
    DeviceRateMatcher::Config config;
    config.inRate = 48000;
    config.outRate = 48000;
    config.writeBlockFrames = kBlockFrames;
    config.callbackFrames = kBlockFrames;
    config.delayMs = 0;
    return config;
}

} // namespace

class TstCaptureInputWatch : public QObject {
    Q_OBJECT

private slots:
    void waitsUntilTheFirstBlock()
    {
        CaptureInputWatch watch;
        const Clock::time_point start{};
        watch.restart(start);
        QCOMPARE(watch.check(start + milliseconds(10), kLostAfter), Verdict::Waiting);
        watch.noteBlock();
        QCOMPARE(watch.check(start + milliseconds(20), kLostAfter), Verdict::FirstBlock);
        watch.noteBlock();
        QCOMPARE(watch.check(start + milliseconds(30), kLostAfter), Verdict::Progress);
    }

    void aStreamThatNeverDeliversIsLost()
    {
        CaptureInputWatch watch;
        const Clock::time_point start{};
        watch.restart(start);
        QCOMPARE(watch.check(start + milliseconds(499), kLostAfter), Verdict::Waiting);
        QCOMPARE(watch.check(start + milliseconds(500), kLostAfter), Verdict::Lost);
    }

    void aDeviceThatStopsDeliveringIsLost()
    {
        CaptureInputWatch watch;
        const Clock::time_point start{};
        watch.restart(start);
        watch.noteBlock();
        QCOMPARE(watch.check(start + milliseconds(10), kLostAfter), Verdict::FirstBlock);
        // Blocks keep the watch alive however long the stream has run.
        Clock::time_point now = start + milliseconds(10);
        for (int i = 0; i < 200; ++i) {
            now += milliseconds(10);
            watch.noteBlock();
            QCOMPARE(watch.check(now, kLostAfter), Verdict::Progress);
        }
        // Then the device stops.
        QCOMPARE(watch.check(now + milliseconds(499), kLostAfter), Verdict::Progress);
        QCOMPARE(watch.check(now + milliseconds(500), kLostAfter), Verdict::Lost);
    }

    void aRestartForgetsTheStreamBefore()
    {
        CaptureInputWatch watch;
        const Clock::time_point start{};
        watch.restart(start);
        watch.noteBlock();
        QCOMPARE(watch.check(start + milliseconds(10), kLostAfter), Verdict::FirstBlock);
        // A block of the old stream, counted before the restart, is not the
        // new stream's first block.
        watch.noteBlock();
        const Clock::time_point reopened = start + milliseconds(2000);
        watch.restart(reopened);
        QCOMPARE(watch.check(reopened + milliseconds(10), kLostAfter), Verdict::Waiting);
        watch.noteBlock();
        QCOMPARE(watch.check(reopened + milliseconds(20), kLostAfter), Verdict::FirstBlock);
    }

    // The fault of 2026-10-12.  The window stops reading the ring while the
    // microphone keeps delivering: the ring fills, drops every block after
    // that and stops storing its last-write time, which is what the helper
    // used to watch.  The input is not lost.
    void aReaderThatStopsReadingDoesNotLoseTheInput()
    {
        DeviceRateMatcher matcher(micConfig());
        QVERIFY(matcher.valid());
        MatcherRingHeader* ring = matcher.ring();
        QVERIFY(ring != nullptr);

        CaptureInputWatch watch;
        const Clock::time_point start{};
        watch.restart(start);

        std::array<float, 2 * kBlockFrames> block{};
        block.fill(0.25f);

        // One second of blocks from the device, nobody reading; the helper
        // looks every 10 ms, as its tick does.
        const int blocks = 750;
        std::int64_t nowNs = 1'000'000'000;
        std::int64_t lastWriteSeen = 0;
        std::int64_t lastWriteChangedNs = nowNs;
        bool lost = false;
        Clock::time_point now = start;
        std::int64_t nextTickNs = nowNs;
        for (int i = 0; i < blocks; ++i) {
            // The helper's input callback: the ring first, then the count.
            matcher.write(block.data(), kBlockFrames, nowNs);
            watch.noteBlock();
            if (nowNs >= nextTickNs) {
                nextTickNs += 10'000'000;
                const std::int64_t lastWrite = ring->lastWriteNs.load(std::memory_order_acquire);
                if (lastWrite != lastWriteSeen) {
                    lastWriteSeen = lastWrite;
                    lastWriteChangedNs = nowNs;
                }
                lost = lost || watch.check(now, kLostAfter) == Verdict::Lost;
            }
            nowNs += kBlockNs;
            now += std::chrono::nanoseconds(kBlockNs);
        }

        // The ring did fill and drop: its last-write time stood still for
        // well over the 500 ms that used to mean "lost".
        QVERIFY2(ring->overruns.load(std::memory_order_acquire) > 0,
                 "the ring never filled; the test did not reach the fault");
        QVERIFY2(nowNs - lastWriteChangedNs >= 500'000'000,
                 "the ring's last-write time kept moving; the test did not reach the fault");
        // The device delivered throughout, so the input is not lost.
        QVERIFY2(!lost, "a reader that stopped reading made the input read as lost");
        QCOMPARE(watch.blocks(), static_cast<std::uint64_t>(blocks));
    }
};

QTEST_APPLESS_MAIN(TstCaptureInputWatch)
#include "tst_capture_input_watch.moc"
