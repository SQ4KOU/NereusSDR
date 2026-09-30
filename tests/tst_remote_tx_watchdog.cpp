// =================================================================
// tests/tst_remote_tx_watchdog.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original test file.
//
// iPhone app plan Task 37 (R-IOS-13; remote design section 12.1, pairing
// design section 9.7, spec section 4.6 item 1): the transmit watchdog's
// rules on an injected clock and timer. Keepalives every 100 ms hold a key
// for 10 minutes; one missed at the end stops it between 400 and 410 ms
// after the last; the link closing stops it at once; VOX armed is watched
// too; copies, overtaken keepalives and older epochs do not count; the
// device's own release ends the watch at once. And why the keepalives
// travel on their own unordered channel: at 5 % loss that channel never
// trips the watchdog in 10 minutes, while the same loss on a reliable,
// in-order channel (retransmission after a timeout, later keepalives held
// behind the lost one) does.
//
// Timing uses an injected clock and timer, never sleeps.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-25: original test for NereusSDR by J.J. Boyd (KG4VCF), iPhone
//               app plan Task 37 (R-IOS-13), with AI-assisted
//               implementation via Anthropic Claude Code.
//   2026-09-29: a trip's warning names the device id in hex, by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include "core/safety/RemoteTxWatchdog.h"

#include "OperatorWording.h"

#include <QRandomGenerator>
#include <QtTest>

#include <algorithm>
#include <optional>
#include <vector>

using namespace NereusSDR;

namespace {

const QByteArray kPhone = QByteArrayLiteral("phone-1");
const QByteArray kTablet = QByteArrayLiteral("tablet-2");

// The watchdog with a clock and one timer the test drives.
struct Rig {
    qint64 nowMs = 0;
    std::optional<qint64> timerDue;
    struct Stop {
        QByteArray device;
        QString message;
        qint64 atMs;
    };
    std::vector<Stop> stops;
    RemoteTxWatchdog watchdog;

    Rig()
    {
        RemoteTxWatchdog::Hooks hooks;
        hooks.clock = [this] { return nowMs; };
        hooks.startTimer = [this](int ms) { timerDue = nowMs + ms; };
        hooks.stopTimer = [this] { timerDue.reset(); };
        hooks.stop = [this](const QByteArray& device, const QString& message) {
            stops.push_back({device, message, nowMs});
        };
        hooks.deviceName = [](const QByteArray& device) {
            return device == kPhone ? QStringLiteral("JJ's iPhone") : QStringLiteral("Shack iPad");
        };
        watchdog.setHooks(std::move(hooks));
    }

    // Moves the clock to `ms`, firing the timer whenever it comes due.
    void advanceTo(qint64 ms)
    {
        while (timerDue && *timerDue <= ms) {
            nowMs = std::max(nowMs, *timerDue);
            timerDue.reset();
            watchdog.onTimer();
        }
        nowMs = std::max(nowMs, ms);
    }
};

} // namespace

class TestRemoteTxWatchdog : public QObject {
    Q_OBJECT

private slots:
    void theNumbersAreStatedTogether();
    void keepalivesHoldTenMinutesAndOneMissedStopsBetween400And410();
    void theLinkClosingStopsAtOnce();
    void aTripLogsTheDeviceIdAsHex();
    void voxArmedIsWatchedAndStops();
    void watchingStartsWithAFresh400ms();
    void copiesAndOvertakenKeepalivesDoNotCount();
    void lateKeepaliveCannotRescueExpiredWatch_data();
    void lateKeepaliveCannotRescueExpiredWatch();
    void anOlderEpochDoesNotCount();
    void aKeyTheDeviceWasNeverAnsweredForTakesAnyEpoch();
    void theDevicesReleaseEndsTheWatchAtOnce();
    void eachDeviceIsWatchedOnItsOwn();
    void aKeepaliveWhileNothingIsWatchedChangesNothing();
    void theStopSentenceIsPlain();
    void theChannelMessageRoundTrips();
    void fivePercentLossOnTheTxChannelNeverTrips();
    void theSameLossOnAReliableChannelStalls();
};

void TestRemoteTxWatchdog::theNumbersAreStatedTogether()
{
    // Remote design sections 8.3 and 12.3: buffer < starvation < link loss
    // < first reconnect, all together.
    QCOMPARE(RemoteTxWatchdog::kKeepaliveIntervalMs, 100);
    QCOMPARE(RemoteTxWatchdog::kLinkLossDeadlineMs, 400);
    QCOMPARE(RemoteTxWatchdog::kFirstReconnectMs, 1000);
    QCOMPARE(RemoteMicConfig::kStarvationMs, 250);
    QCOMPARE(RemoteMicConfig::kMaxDepthMs, 120);
    QVERIFY(RemoteMicConfig::kMaxDepthMs < RemoteMicConfig::kStarvationMs);
    QVERIFY(RemoteMicConfig::kStarvationMs < RemoteTxWatchdog::kLinkLossDeadlineMs);
    QVERIFY(RemoteTxWatchdog::kLinkLossDeadlineMs < RemoteTxWatchdog::kFirstReconnectMs);
}

void TestRemoteTxWatchdog::keepalivesHoldTenMinutesAndOneMissedStopsBetween400And410()
{
    Rig rig;
    rig.watchdog.setKeyed(kPhone, true, 7);
    QVERIFY(rig.watchdog.isWatching(kPhone));

    constexpr qint64 kTenMinutesMs = 10 * 60 * 1000;
    quint64 sequence = 0;
    qint64 lastMs = 0;
    for (qint64 t = 100; t <= kTenMinutesMs; t += RemoteTxWatchdog::kKeepaliveIntervalMs) {
        rig.advanceTo(t);
        QVERIFY(rig.watchdog.keepalive(kPhone, ++sequence, 7, RemoteTxWatchdog::Path::Session));
        lastMs = t;
    }
    QVERIFY2(rig.stops.empty(), "keepalives every 100 ms stopped the key");
    QCOMPARE(sequence, quint64(6000));

    // The next one never comes.
    rig.advanceTo(lastMs + 399);
    QVERIFY(rig.stops.empty());
    rig.advanceTo(lastMs + 2000);
    QCOMPARE(rig.stops.size(), size_t(1));
    const qint64 after = rig.stops.front().atMs - lastMs;
    QVERIFY2(after >= 400 && after <= 410, qPrintable(QStringLiteral("stopped %1 ms after the last").arg(after)));
    QCOMPARE(rig.stops.front().device, kPhone);
    QCOMPARE(rig.stops.front().message,
             QStringLiteral("The link to JJ's iPhone went quiet, so the Core stopped transmitting."));
    QVERIFY(!rig.watchdog.isWatching(kPhone));
    QVERIFY(!rig.timerDue.has_value());
}

void TestRemoteTxWatchdog::theLinkClosingStopsAtOnce()
{
    Rig rig;
    rig.watchdog.setKeyed(kPhone, true, 3);
    rig.advanceTo(100);
    QVERIFY(rig.watchdog.keepalive(kPhone, 1, 3, RemoteTxWatchdog::Path::TxChannel));
    rig.advanceTo(150);
    QSignalSpy tripped(&rig.watchdog, &RemoteTxWatchdog::tripped);
    rig.watchdog.linkClosed(kPhone);
    QCOMPARE(rig.stops.size(), size_t(1));
    QCOMPARE(rig.stops.front().atMs, qint64(150));
    QCOMPARE(tripped.count(), 1);
    QCOMPARE(tripped.at(0).at(1).toBool(), true);
    QVERIFY(!rig.watchdog.isWatching(kPhone));

    // A device that was not watched: nothing.
    rig.watchdog.linkClosed(kTablet);
    QCOMPARE(rig.stops.size(), size_t(1));
}

// A device id is raw bytes (the link's binary id), so the trip's warning
// names it in hex; the raw bytes read as garbage in a log.
void TestRemoteTxWatchdog::aTripLogsTheDeviceIdAsHex()
{
    Rig rig;
    const QByteArray binaryId = QByteArray::fromHex("00ff10a1c3");
    rig.watchdog.setKeyed(binaryId, true, 1);
    QTest::ignoreMessage(QtWarningMsg,
                         QRegularExpression(QStringLiteral("^Transmit watchdog: the link closed +"
                                                           "from 00ff10a1c3 - ")));
    rig.watchdog.linkClosed(binaryId);
    QCOMPARE(rig.stops.size(), size_t(1));
    QCOMPARE(rig.stops.front().device, binaryId);
}

void TestRemoteTxWatchdog::voxArmedIsWatchedAndStops()
{
    Rig rig;
    rig.advanceTo(1000);
    rig.watchdog.setVoxArmed(kTablet, true);
    QVERIFY(rig.watchdog.isWatching(kTablet));
    for (quint64 s = 1; s <= 20; ++s) {
        rig.advanceTo(1000 + static_cast<qint64>(s) * 100);
        // VOX has no key of its own: any epoch counts.
        QVERIFY(rig.watchdog.keepalive(kTablet, s, 0, RemoteTxWatchdog::Path::Session));
    }
    QVERIFY(rig.stops.empty());
    rig.advanceTo(3000 + 400);
    QVERIFY(rig.stops.empty());
    rig.advanceTo(3000 + 401);
    QCOMPARE(rig.stops.size(), size_t(1));
    QCOMPARE(rig.stops.front().message,
             QStringLiteral("The link to Shack iPad went quiet, so the Core stopped transmitting."));

    // Disarmed by the device: nothing is watched.
    Rig other;
    other.watchdog.setVoxArmed(kTablet, true);
    other.watchdog.setVoxArmed(kTablet, false);
    QVERIFY(!other.watchdog.isWatching(kTablet));
    other.advanceTo(5000);
    QVERIFY(other.stops.empty());
}

void TestRemoteTxWatchdog::watchingStartsWithAFresh400ms()
{
    Rig rig;
    rig.advanceTo(5000);
    rig.watchdog.setKeyed(kPhone, true, 1);
    rig.advanceTo(5400);
    QVERIFY(rig.stops.empty());
    rig.advanceTo(5401);
    QCOMPARE(rig.stops.size(), size_t(1));

    // Watching again later starts afresh, sequences included.
    rig.advanceTo(9000);
    rig.watchdog.setKeyed(kPhone, true, 2);
    rig.advanceTo(9100);
    QVERIFY(rig.watchdog.keepalive(kPhone, 1, 2, RemoteTxWatchdog::Path::Session));
    rig.advanceTo(9500);
    QCOMPARE(rig.stops.size(), size_t(1));
}

void TestRemoteTxWatchdog::copiesAndOvertakenKeepalivesDoNotCount()
{
    Rig rig;
    rig.watchdog.setKeyed(kPhone, true, 4);
    rig.advanceTo(100);
    QVERIFY(rig.watchdog.keepalive(kPhone, 5, 4, RemoteTxWatchdog::Path::TxChannel));
    rig.advanceTo(300);
    // The same keepalive by the other path, and one overtaken by it.
    QVERIFY(!rig.watchdog.keepalive(kPhone, 5, 4, RemoteTxWatchdog::Path::Session));
    QVERIFY(!rig.watchdog.keepalive(kPhone, 4, 4, RemoteTxWatchdog::Path::TxChannel));
    // Neither moved the deadline: 400 ms after the one at 100 ms.
    rig.advanceTo(501);
    QCOMPARE(rig.stops.size(), size_t(1));
}

void TestRemoteTxWatchdog::lateKeepaliveCannotRescueExpiredWatch_data()
{
    QTest::addColumn<int>("path");
    QTest::addColumn<qint64>("gapMs");
    QTest::addColumn<quint64>("sequence");
    QTest::addColumn<quint32>("epoch");
    for (const auto path : {RemoteTxWatchdog::Path::Session,
                            RemoteTxWatchdog::Path::TxChannel,
                            RemoteTxWatchdog::Path::Auxiliary}) {
        const QByteArray name = QByteArray::number(static_cast<int>(path));
        QTest::newRow((name + "-at-deadline").constData())
            << static_cast<int>(path) << qint64(400) << quint64(6) << quint32(4);
        QTest::newRow((name + "-expired-new").constData())
            << static_cast<int>(path) << qint64(401) << quint64(6) << quint32(4);
        QTest::newRow((name + "-expired-copy").constData())
            << static_cast<int>(path) << qint64(401) << quint64(5) << quint32(4);
        QTest::newRow((name + "-expired-old-key").constData())
            << static_cast<int>(path) << qint64(401) << quint64(6) << quint32(3);
    }
}

void TestRemoteTxWatchdog::lateKeepaliveCannotRescueExpiredWatch()
{
    QFETCH(int, path);
    QFETCH(qint64, gapMs);
    QFETCH(quint64, sequence);
    QFETCH(quint32, epoch);
    Rig rig;
    rig.watchdog.setKeyed(kPhone, true, 4);
    rig.advanceTo(100);
    QVERIFY(rig.watchdog.keepalive(kPhone, 5, 4, RemoteTxWatchdog::Path::Session));
    QSignalSpy heard(&rig.watchdog, &RemoteTxWatchdog::keepaliveHeard);
    QSignalSpy tripped(&rig.watchdog, &RemoteTxWatchdog::tripped);

    // A busy event loop can dispatch socket input before its overdue timer.
    // Move the clock without delivering that timer: arrival must check age.
    rig.nowMs = 100 + gapMs;
    const bool accepted = rig.watchdog.keepalive(
        kPhone, sequence, epoch, static_cast<RemoteTxWatchdog::Path>(path));
    const bool withinDeadline = gapMs <= RemoteTxWatchdog::kLinkLossDeadlineMs;
    QCOMPARE(accepted, withinDeadline);
    QCOMPARE(heard.count(), withinDeadline ? 1 : 0);
    QCOMPARE(rig.stops.size(), withinDeadline ? size_t(0) : size_t(1));
    QCOMPARE(rig.watchdog.isWatching(kPhone), withinDeadline);
    if (!withinDeadline) {
        QCOMPARE(tripped.count(), 1);
        QCOMPARE(tripped.at(0).at(1).toBool(), false);
        QCOMPARE(tripped.at(0).at(2).toLongLong(), gapMs);
        QVERIFY(!rig.timerDue.has_value());
        rig.watchdog.onTimer();
        QVERIFY(!rig.watchdog.keepalive(kPhone, 7, 4, RemoteTxWatchdog::Path::Session));
        QCOMPARE(rig.stops.size(), size_t(1));
    }
}

void TestRemoteTxWatchdog::anOlderEpochDoesNotCount()
{
    Rig rig;
    rig.watchdog.setKeyed(kPhone, true, 9);
    rig.advanceTo(100);
    QVERIFY(!rig.watchdog.keepalive(kPhone, 1, 8, RemoteTxWatchdog::Path::Session));
    QVERIFY(!rig.watchdog.keepalive(kPhone, 2, 0, RemoteTxWatchdog::Path::Session));
    rig.advanceTo(200);
    QVERIFY(rig.watchdog.keepalive(kPhone, 3, 9, RemoteTxWatchdog::Path::Session));
    rig.advanceTo(300);
    // Before its key's answer the device knows no epoch and sends the
    // highest: never older.
    QVERIFY(rig.watchdog.keepalive(kPhone, 4, 0xFFFFFFFFu, RemoteTxWatchdog::Path::Session));
    rig.advanceTo(700);
    QVERIFY(rig.stops.empty());
    rig.advanceTo(701);
    QCOMPARE(rig.stops.size(), size_t(1));
}

void TestRemoteTxWatchdog::aKeyTheDeviceWasNeverAnsweredForTakesAnyEpoch()
{
    Rig rig;
    // A VOX key attributed to the device: epoch 0.
    rig.watchdog.setKeyed(kPhone, true, 0);
    rig.advanceTo(100);
    QVERIFY(rig.watchdog.keepalive(kPhone, 1, 0, RemoteTxWatchdog::Path::Session));
    rig.advanceTo(200);
    QVERIFY(rig.watchdog.keepalive(kPhone, 2, 12, RemoteTxWatchdog::Path::Session));
}

void TestRemoteTxWatchdog::theDevicesReleaseEndsTheWatchAtOnce()
{
    Rig rig;
    rig.watchdog.setKeyed(kPhone, true, 5);
    rig.advanceTo(100);
    QVERIFY(rig.watchdog.keepalive(kPhone, 1, 5, RemoteTxWatchdog::Path::Session));
    // The release: the key's keepalives stop with it, while MOX may still
    // read on (a tail); that is never a lost link.
    rig.watchdog.released(kPhone);
    QVERIFY(!rig.watchdog.isWatching(kPhone));
    rig.advanceTo(60000);
    QVERIFY(rig.stops.empty());

    // VOX armed is still watched after the key's release.
    Rig vox;
    vox.watchdog.setVoxArmed(kPhone, true);
    vox.watchdog.setKeyed(kPhone, true, 6);
    vox.watchdog.released(kPhone);
    QVERIFY(vox.watchdog.isWatching(kPhone));
    vox.advanceTo(401);
    QCOMPARE(vox.stops.size(), size_t(1));
}

void TestRemoteTxWatchdog::eachDeviceIsWatchedOnItsOwn()
{
    Rig rig;
    rig.watchdog.setKeyed(kPhone, true, 1);
    rig.watchdog.setVoxArmed(kTablet, true);
    for (quint64 s = 1; s <= 10; ++s) {
        rig.advanceTo(static_cast<qint64>(s) * 100);
        QVERIFY(rig.watchdog.keepalive(kPhone, s, 1, RemoteTxWatchdog::Path::Session));
    }
    // The tablet went quiet at 0; the phone kept talking.
    QCOMPARE(rig.stops.size(), size_t(1));
    QCOMPARE(rig.stops.front().device, kTablet);
    QCOMPARE(rig.stops.front().atMs, qint64(401));
    QVERIFY(rig.watchdog.isWatching(kPhone));
    // Another device's keepalives never hold the phone's key.
    QVERIFY(!rig.watchdog.keepalive(kTablet, 99, 1, RemoteTxWatchdog::Path::Session));
    rig.advanceTo(1401);
    QCOMPARE(rig.stops.size(), size_t(2));
    QCOMPARE(rig.stops.back().device, kPhone);
}

void TestRemoteTxWatchdog::aKeepaliveWhileNothingIsWatchedChangesNothing()
{
    Rig rig;
    QVERIFY(!rig.watchdog.keepalive(kPhone, 1, 1, RemoteTxWatchdog::Path::Session));
    QVERIFY(!rig.watchdog.isWatchingAny());
    QVERIFY(!rig.timerDue.has_value());
    rig.advanceTo(5000);
    QVERIFY(rig.stops.empty());
}

void TestRemoteTxWatchdog::theStopSentenceIsPlain()
{
    const QString sentence = RemoteTxWatchdog::stopMessage(QStringLiteral("JJ's iPhone"));
    QCOMPARE(sentence,
             QStringLiteral("The link to JJ's iPhone went quiet, so the Core stopped transmitting."));
    QVERIFY2(OperatorWording::isPlain(sentence), qPrintable(sentence));
    QVERIFY(OperatorWording::coreCalledStationIn(sentence).isEmpty());
}

void TestRemoteTxWatchdog::theChannelMessageRoundTrips()
{
    const QByteArray message = RemoteTxWatchdog::channelKeepalive(0x0102030405060708ULL, 0xA1B2C3D4u);
    QCOMPARE(message.size(), 13);
    QCOMPARE(message.toHex(), QByteArray("010102030405060708a1b2c3d4"));
    quint64 sequence = 0;
    quint32 epoch = 0;
    QVERIFY(RemoteTxWatchdog::readChannelKeepalive(message, &sequence, &epoch));
    QCOMPARE(sequence, 0x0102030405060708ULL);
    QCOMPARE(epoch, 0xA1B2C3D4u);

    QVERIFY(!RemoteTxWatchdog::readChannelKeepalive(message.left(12), &sequence, &epoch));
    QVERIFY(!RemoteTxWatchdog::readChannelKeepalive(message + QByteArray(1, '\0'), &sequence, &epoch));
    QByteArray otherKind = message;
    otherKind[0] = 2;
    QVERIFY(!RemoteTxWatchdog::readChannelKeepalive(otherKind, &sequence, &epoch));
    // Sequences start at 1.
    QVERIFY(!RemoteTxWatchdog::readChannelKeepalive(RemoteTxWatchdog::channelKeepalive(0, 1),
                                                    &sequence, &epoch));
}

namespace {

// Ten minutes of keepalives, one every 100 ms, each lost with probability
// `loss`, carried either unordered and never retransmitted (the "tx"
// channel) or reliably in order (a TCP-like channel: a lost keepalive is
// sent again after a retransmission timeout of 200 ms, doubling for each
// time it is lost again, Linux's TCP_RTO_MIN and RFC 6298's backoff; the
// ones after it are held until it arrives). One-way delay 20 ms. Returns
// how many times the watchdog stopped the key (the key is taken again at
// once after each stop) and the longest silence it saw.
struct LossRun {
    int stops = 0;
    qint64 longestGapMs = 0;
    int lost = 0;
};

LossRun runWithLoss(double loss, bool reliable, quint32 seed)
{
    constexpr qint64 kTenMinutesMs = 10 * 60 * 1000;
    constexpr qint64 kDelayMs = 20;
    constexpr qint64 kRtoMs = 200;
    QRandomGenerator random(seed);
    LossRun run;

    // Each keepalive that arrives: when, and its sequence.
    std::vector<std::pair<qint64, quint64>> ordered;
    qint64 headOfLine = 0;
    quint64 sequence = 0;
    for (qint64 sent = 100; sent <= kTenMinutesMs; sent += RemoteTxWatchdog::kKeepaliveIntervalMs) {
        ++sequence;
        if (!reliable) {
            if (random.generateDouble() < loss) {
                ++run.lost;
                continue;
            }
            ordered.emplace_back(sent + kDelayMs, sequence);
            continue;
        }
        qint64 delivered = sent + kDelayMs;
        qint64 rto = kRtoMs;
        while (random.generateDouble() < loss) {
            ++run.lost;
            delivered += rto;
            rto *= 2;
        }
        // In order: nothing is handed over before the one ahead of it.
        delivered = std::max(delivered, headOfLine);
        headOfLine = delivered;
        ordered.emplace_back(delivered, sequence);
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });

    Rig rig;
    rig.watchdog.setKeyed(kPhone, true, 1);
    size_t stopsSeen = 0;
    qint64 last = 0;
    for (const auto& [at, seq] : ordered) {
        rig.advanceTo(at);
        if (rig.stops.size() != stopsSeen) {
            stopsSeen = rig.stops.size();
            // Keyed again at once, so the run counts every stall.
            rig.watchdog.setKeyed(kPhone, true, 1);
        }
        run.longestGapMs = std::max(run.longestGapMs, at - last);
        last = at;
        rig.watchdog.keepalive(kPhone, seq, 1, RemoteTxWatchdog::Path::TxChannel);
    }
    rig.advanceTo(last + 1);
    run.stops = static_cast<int>(rig.stops.size());
    return run;
}

constexpr quint32 kLossSeed = 20260925;

} // namespace

void TestRemoteTxWatchdog::fivePercentLossOnTheTxChannelNeverTrips()
{
    const LossRun run = runWithLoss(0.05, /*reliable=*/false, kLossSeed);
    qInfo("tx channel, 5%% loss, 10 min: %d of 6000 lost, longest silence %lld ms, %d stops",
          run.lost, static_cast<long long>(run.longestGapMs), run.stops);
    QVERIFY(run.lost > 200);  // about 300 of 6000
    QCOMPARE(run.stops, 0);
    // Three lost in a row is a 400 ms silence, which is not more than 400.
    QVERIFY(run.longestGapMs <= RemoteTxWatchdog::kLinkLossDeadlineMs);
}

void TestRemoteTxWatchdog::theSameLossOnAReliableChannelStalls()
{
    // Why the keepalives have their own channel: on a reliable in-order
    // channel a lost keepalive holds every later one behind its
    // retransmission, and a keepalive lost twice holds them for 600 ms.
    const LossRun run = runWithLoss(0.05, /*reliable=*/true, kLossSeed);
    qInfo("reliable channel, 5%% loss, 10 min: %d losses, longest silence %lld ms, %d stops",
          run.lost, static_cast<long long>(run.longestGapMs), run.stops);
    QVERIFY(run.longestGapMs > RemoteTxWatchdog::kLinkLossDeadlineMs);
    QVERIFY2(run.stops > 0, "the reliable channel never stalled past the deadline");
}

QTEST_GUILESS_MAIN(TestRemoteTxWatchdog)
#include "tst_remote_tx_watchdog.moc"
