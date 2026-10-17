// =================================================================
// tests/tst_device_rate_matcher_link.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original deterministic test for the
// already-attributed DeviceRateMatcher's packet writer (R-AUD-15).  It
// drives the matcher's writer and reader on two simulated clocks, with no
// timer and no device, as tst_device_rate_matcher does, and gives the
// packets the arrival times a link gives them.  Each simulation is
// single-threaded and deterministic.
//
// Modification history (NereusSDR):
//   2026-10-12: a stream on clocks that agree, with a link's arrival
//               jitter, keeps no backlog and loses no packet (the held
//               ratio seen on a Mac window, 2026-10-11). J.J. Boyd
//               (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-12: permanent callback-size and changing-hold rows for the
//               packet-arrival clock fit. J.J. Boyd (KG4VCF), AI-assisted
//               via OpenAI Codex.
// =================================================================

#include <QtTest>

#include "core/audio/AudioDelayParts.h"
#include "core/audio/DeviceRateMatcher.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <vector>

using namespace NereusSDR;

namespace {

constexpr int kRate = 48000;
// One Opus packet of remote audio: 40 ms.
constexpr int kPacketFrames = 1920;
// The device's callback: as a Mac's output runs it, and a 10 ms period.
constexpr int kMacCallbackFrames = 64;
constexpr int kTenMsCallbackFrames = 480;
// What stands behind the writer in the receiver: the jitter queue's 320 ms
// window (AudioJitterBuffer::kWindowNs), eight packets.  A ninth pushes the
// oldest out (AudioJitterBuffer::advanceToFit), which the receiver counts
// as a re-anchor and a link interruption.
constexpr int kQueuePackets = 8;
constexpr double kSeconds = 240.0;
// The end of the run, where the stream has long settled.
constexpr double kSettledSeconds = 60.0;
// The start, which the control has by then steered out of.
constexpr double kStartSeconds = 30.0;
// Where the device stops asking for audio, in the rows where it does.
constexpr double kStallAt = 60.0;
// Where the jitter queue's hold starts to ease, in the rows where it was
// deepened, and its step (AudioJitterBuffer::kShrinkStepNs, one every
// kShrinkIntervalNs).
constexpr double kEaseAt = 60.0;
constexpr double kEaseStepSeconds = 0.020;
constexpr double kEaseIntervalSeconds = 1.0;
// The most a standing backlog may speed the audio up (DeviceRateMatcher's
// kPacketDrainMaxRatio), and how far from the clocks' ratio a steady stream
// may be steered.
constexpr double kDrainMaxRatio = 0.005;
constexpr double kSteadyMaxRatio = 0.001;

// The lateness of each packet, the same on every platform: a linear
// congruential generator (Numerical Recipes' constants), its top 24 bits.
class Lateness {
public:
    Lateness(int seed, int spreadMs)
        : m_state(static_cast<std::uint32_t>(seed) * 2654435761u + 12345u)
        , m_spreadSeconds(spreadMs * 1.0e-3)
    {
    }

    double next()
    {
        m_state = m_state * 1664525u + 1013904223u;
        return m_spreadSeconds * static_cast<double>(m_state >> 8) / static_cast<double>(1u << 24);
    }

private:
    std::uint32_t m_state;
    double m_spreadSeconds;
};

} // namespace

class TestDeviceRateMatcherLink : public QObject {
    Q_OBJECT

private slots:
    void aJitteredStreamOnMatchedClocksKeepsNoBacklog_data();
    void aJitteredStreamOnMatchedClocksKeepsNoBacklog();
};

// A stream whose clock and the device's agree (and 30 ppm apart, as two
// real clocks are), whose packets reach the writer up to 16 or 20 ms late,
// each by its own amount.  RtpReceptionStats reports a spread like that as
// a third of it, 5 to 7 ms of arrival jitter.
//
// The writer follows the receiver's rules (RemoteAudioReceiver's
// passIntoMatcher and matcherTakesPacket): a packet is written once it has
// come and the matcher has room for it, and is said to have waited when it
// was kept back.
//
// With the clocks in step nothing should stand behind the writer for
// longer than a packet takes to fit, and nothing should be dropped.  The
// control can instead take a wrong ratio from its first seconds of
// measuring and hold it.  Once a backlog stands every packet waits; the
// feed-forward is measured only from packets that did not, so it stays
// where it was, and the deviation applied in its place is a fixed few
// hundred ppm.  A feed-forward further off than that plays the audio slow
// from then on: the queue fills, and a packet is dropped each time 40 ms
// more has built up, at exactly the held ratio.
void TestDeviceRateMatcherLink::aJitteredStreamOnMatchedClocksKeepsNoBacklog_data()
{
    QTest::addColumn<int>("callbackFrames");
    QTest::addColumn<int>("spreadMs");
    QTest::addColumn<int>("seed");
    QTest::addColumn<int>("ppm");
    QTest::addColumn<int>("stallMs");
    QTest::addColumn<int>("easeMs");
    QTest::addColumn<int>("shedPackets");
    for (int spreadMs : {16, 20}) {
        for (int seed : {1, 2, 3, 4, 5, 6}) {
            QTest::addRow("up to %d ms late, seed %d", spreadMs, seed)
                << kMacCallbackFrames << spreadMs << seed << 0 << 0 << 0 << 0;
        }
    }
    for (int ppm : {30, -30}) {
        QTest::addRow("up to 20 ms late, seed 3, %d ppm", ppm)
            << kMacCallbackFrames << 20 << 3 << ppm << 0 << 0 << 0;
    }
    // Representative 256- and 480-frame device callbacks. The latter used
    // to finish playing fast with repeated dry runs, even with no drop.
    QTest::addRow("256-frame callback, up to 20 ms late")
        << 256 << 20 << 3 << 0 << 0 << 0 << 0;
    QTest::addRow("480-frame callback, up to 16 ms late")
        << kTenMsCallbackFrames << 16 << 1 << 0 << 0 << 0 << 0;
    // A changing jitter hold and skipped intervals must not look like a
    // change in the source's clock. With release time in place of arrival
    // time this row crossed the 0.5% speed bound in the control run.
    QTest::addRow("hold eases 200 ms and sheds three intervals")
        << kMacCallbackFrames << 16 << 1 << 0 << 0 << 200 << 3;
}

void TestDeviceRateMatcherLink::aJitteredStreamOnMatchedClocksKeepsNoBacklog()
{
    QFETCH(int, callbackFrames);
    QFETCH(int, spreadMs);
    QFETCH(int, seed);
    QFETCH(int, ppm);
    QFETCH(int, stallMs);
    QFETCH(int, easeMs);
    QFETCH(int, shedPackets);
    DeviceRateMatcher::Config config;
    config.callbackFrames = callbackFrames;
    DeviceRateMatcher matcher(config);
    QVERIFY(matcher.valid());
    MatcherReader reader = matcher.makeReader();
    Lateness lateness(seed, spreadMs);
    const std::vector<float> packet(static_cast<std::size_t>(kPacketFrames) * 2, 0.25f);
    std::vector<float> out(static_cast<std::size_t>(callbackFrames) * 2);

    // The source's clock is the time line; the device's runs ppm fast.
    const double clocksRatio = 1.0 + ppm * 1.0e-6;
    const double packetSeconds = static_cast<double>(kPacketFrames) / kRate;
    const double readSeconds = static_cast<double>(callbackFrames) / (kRate * clocksRatio);
    const double stallSeconds = stallMs * 1.0e-3;
    // A packet the queue has released and the writer has not yet written:
    // its place in the stream, when it arrived and when it was released.
    struct Ready {
        std::int64_t index;
        double arrivedAt;
        double readyAt;
    };
    std::deque<Ready> waiting;
    std::int64_t sent = 0;      // packets the link has handed to the queue
    std::int64_t written = 0;
    std::int64_t dropped = 0;
    std::int64_t shed = 0;
    bool refused = false;
    std::int64_t mostWaitingSettled = 0;
    std::int64_t mostWaiting = 0;
    bool started = false;
    std::uint64_t dryAtStart = 0;
    double lowestRatio = 2.0;
    double highestRatio = 0.0;
    bool stalled = false;
    double backlogUntil = 0.0;   // the last time more than one packet waited

    // The jitter queue ahead of the writer releases each packet its own
    // arrival plus the hold after it (AudioJitterBuffer).  A hold that
    // never changes shifts every packet alike and is left out; a deepened
    // one is deeper by easeMs until kEaseAt and then eases a step an
    // interval, as the queue's does once the link has been quiet.
    const int easeSteps
        = static_cast<int>(std::ceil(easeMs * 1.0e-3 / kEaseStepSeconds - 1.0e-9));
    const auto deepenedAt = [&](double arrival) {
        const double steps
            = arrival < kEaseAt ? 0.0 : std::floor((arrival - kEaseAt) / kEaseIntervalSeconds) + 1.0;
        return std::max(0.0, easeMs * 1.0e-3 - steps * kEaseStepSeconds);
    };
    // The queue sheds what its eased hold left standing, an interval at a
    // time (AudioJitterBuffer::shedExcess): here the given count, one an
    // interval from the end of the easing.
    const double shedFrom = kEaseAt + (easeSteps + 1) * kEaseIntervalSeconds;

    const auto writeWhatFits = [&](double now) {
        // A packet written in the same pass as one that waited stood
        // behind it, and waited with it.
        bool behindWaited = false;
        while (!waiting.empty()) {
            const DeviceRateMatcherStats stats = matcher.stats();
            const bool room = stats.packetFrames != kPacketFrames || stats.queuedFrames <= 0
                || stats.queuedFrames + stats.packetOutFrames <= stats.packetHighWaterFrames;
            if (!room) {
                refused = true;
                break;
            }
            // What the receiver says of it: how long it stood released
            // while the matcher had no room (at least 1 ns, which says
            // that it waited), how long ago it arrived, and its place in
            // the stream.
            const Ready& next = waiting.front();
            MatcherPacketTiming timing;
            timing.waitedNs = refused || behindWaited
                ? std::max<std::int64_t>(1, static_cast<std::int64_t>((now - next.readyAt) * 1.0e9))
                : 0;
            timing.arrivedAgoNs = static_cast<std::int64_t>((now - next.arrivedAt) * 1.0e9);
            timing.sourceFrame = next.index * kPacketFrames;
            matcher.setWritePacketFrames(kPacketFrames, timing);
            refused = false;
            behindWaited = timing.waitedNs > 0;
            matcher.write(packet.data(), kPacketFrames, static_cast<std::int64_t>(now * 1.0e9));
            waiting.pop_front();
            ++written;
        }
        mostWaiting = std::max(mostWaiting, static_cast<std::int64_t>(waiting.size()));
        if (waiting.size() > 1) {
            backlogUntil = now;
        }
        if (now >= kSeconds - kSettledSeconds) {
            mostWaitingSettled = std::max(mostWaitingSettled,
                                          static_cast<std::int64_t>(waiting.size()));
        }
    };

    double nextArrival = lateness.next();
    double nextReady = nextArrival + deepenedAt(nextArrival);
    double nextRead = readSeconds / 2;
    while (std::min(nextReady, nextRead) < kSeconds) {
        if (nextReady <= nextRead) {
            const double now = nextReady;
            if (shed < shedPackets && now >= shedFrom + shed * kEaseIntervalSeconds) {
                // Skipped unheard.
                ++shed;
            } else {
                if (static_cast<int>(waiting.size()) >= kQueuePackets) {
                    waiting.pop_front();
                    ++dropped;
                }
                waiting.push_back({sent, nextArrival, now});
            }
            ++sent;
            // A link hands packets over in order, and the queue releases
            // them in order.
            nextArrival
                = std::max(nextArrival, static_cast<double>(sent) * packetSeconds + lateness.next());
            nextReady = std::max(now, nextArrival + deepenedAt(nextArrival));
            writeWhatFits(now);
        } else {
            const double now = nextRead;
            nextRead += readSeconds;
            if (stallMs > 0 && !stalled && now >= kStallAt) {
                // The device asks for nothing for a while (its thread was
                // held up), and then goes on as it was.
                stalled = true;
                nextRead += stallSeconds;
            }
            reader.read(out.data(), callbackFrames);
            writeWhatFits(now);
            if (now >= kStartSeconds) {
                const DeviceRateMatcherStats stats = matcher.stats();
                if (!started) {
                    started = true;
                    dryAtStart = stats.dryRuns;
                }
                lowestRatio = std::min(lowestRatio, stats.ratio);
                highestRatio = std::max(highestRatio, stats.ratio);
            }
        }
    }

    const DeviceRateMatcherStats stats = matcher.stats();
    const std::uint64_t dryAfterStart = stats.dryRuns - dryAtStart;
    qInfo("ratio %.1f ppm from the clocks', most packets waiting %lld, in the last %.0f s %lld, "
          "dropped %lld and shed %lld of %lld, dry runs %llu, after %.0f s %llu, "
          "ratio then %.0f to %.0f ppm, a backlog until %.1f s",
          (stats.ratio - clocksRatio) * 1.0e6, static_cast<long long>(mostWaiting), kSettledSeconds,
          static_cast<long long>(mostWaitingSettled), static_cast<long long>(dropped),
          static_cast<long long>(shed),
          static_cast<long long>(sent), static_cast<unsigned long long>(stats.dryRuns),
          kStartSeconds, static_cast<unsigned long long>(dryAfterStart),
          (lowestRatio - clocksRatio) * 1.0e6, (highestRatio - clocksRatio) * 1.0e6, backlogUntil);
    QCOMPARE(stats.overruns, std::uint64_t(0));
    QVERIFY(stats.controlActive);
    // The queue never filled.
    QCOMPARE(dropped, std::int64_t(0));
    // One packet may wait a moment for room after a late one; no more
    // stand behind it.
    QVERIFY2(mostWaitingSettled <= 1,
             qPrintable(QStringLiteral("%1 packets waiting").arg(mostWaitingSettled)));
    // The clocks' ratio, within what the spread of arrivals leaves of it:
    // 20 ms over the run is 83 ppm, and the bound is a few times that.
    QVERIFY2(std::abs(stats.ratio - clocksRatio) < 3.0e-4,
             qPrintable(QStringLiteral("ratio %1").arg(stats.ratio, 0, 'f', 7)));
    // Once it has started the stream never runs dry.
    QCOMPARE(dryAfterStart, std::uint64_t(0));
    // Nor is its speed moved by more than a listener would hear: a steady
    // stream's stays close to the clocks' ratio, and a backlog is played
    // down no faster than the matcher's limit.
    const double mostFast
        = stallMs > 0 || easeMs > 0 ? kDrainMaxRatio + kSteadyMaxRatio : kSteadyMaxRatio;
    QVERIFY2(lowestRatio > clocksRatio - mostFast && highestRatio < clocksRatio + kSteadyMaxRatio,
             qPrintable(QStringLiteral("ratio %1 to %2")
                            .arg(lowestRatio, 0, 'f', 7).arg(highestRatio, 0, 'f', 7)));
}

QTEST_GUILESS_MAIN(TestDeviceRateMatcherLink)
#include "tst_device_rate_matcher_link.moc"
