// =================================================================
// tests/tst_genius_poll_pacer.cpp  (NereusSDR)
// =================================================================
// NereusSDR-native test. The pacer that sets how often the Power Genius
// XL and Tuner Genius XL links ask for `status` (GeniusPollPacer.h): the
// keyed interval while the Core or the device says keyed and for a hold
// after, the receive interval otherwise. The links' own cases are in
// tst_pgxl_connection_keepalive and tst_tgxl_connection_status_pacing.
// =================================================================
// Modification history (NereusSDR):
//   2026-10-12  Created by J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>
#include <QTimer>
#include "core/GeniusPollPacer.h"

namespace {

constexpr int kReceiveMs = 250;
constexpr int kKeyedMs = 62;

// Longer than any case runs: with it as the hold, or as an interval, only
// the thing a case names can change the rate or fire the timer.
constexpr int kNeverMs = 60000;

} // namespace

class GeniusPollPacerTest : public QObject {
    Q_OBJECT
private slots:
    void startsAtTheReceiveInterval();
    void keyedIsFastAndTheHoldEndsIt();
    void theDeviceBeingBusyIsFastToo();
    void notKeyedWhenNeverKeyedStartsNoHold();
    void theHoldRunsFromTheLastOfTheTwo();
    void keyedAgainDuringTheHoldStaysFast();
    void aNewConnectionKeepsOnlyTheCoresState();
    void theKeyedIntervalAppliesAtOnce();
    void anUnchangedIntervalDoesNotRestartThePoll();
    void aStoppedPollIsNotStarted();
};

void GeniusPollPacerTest::startsAtTheReceiveInterval()
{
    QTimer poll;
    NereusSDR::GeniusPollPacer pacer(&poll, kReceiveMs, kKeyedMs);
    QVERIFY(!pacer.fast());
    QCOMPARE(poll.interval(), kReceiveMs);
    QCOMPARE(NereusSDR::GeniusPollPacer::kHoldMs, 2000);
}

void GeniusPollPacerTest::keyedIsFastAndTheHoldEndsIt()
{
    QTimer poll;
    NereusSDR::GeniusPollPacer pacer(&poll, kReceiveMs, kKeyedMs);
    pacer.setHoldMsForTesting(150);
    pacer.setKeyed(true);
    QVERIFY(pacer.fast());
    QCOMPARE(poll.interval(), kKeyedMs);
    // Unkeyed: still fast for the hold, then the receive interval.
    pacer.setKeyed(false);
    QVERIFY(pacer.fast());
    QCOMPARE(poll.interval(), kKeyedMs);
    QTRY_COMPARE(poll.interval(), kReceiveMs);
    QVERIFY(!pacer.fast());
}

void GeniusPollPacerTest::theDeviceBeingBusyIsFastToo()
{
    QTimer poll;
    NereusSDR::GeniusPollPacer pacer(&poll, kReceiveMs, kKeyedMs);
    pacer.setHoldMsForTesting(150);
    pacer.setDeviceBusy(true);
    QCOMPARE(poll.interval(), kKeyedMs);
    pacer.setDeviceBusy(false);
    QCOMPARE(poll.interval(), kKeyedMs);
    QTRY_COMPARE(poll.interval(), kReceiveMs);
}

// Every status answer in receive says "not busy". That alone starts
// nothing.
void GeniusPollPacerTest::notKeyedWhenNeverKeyedStartsNoHold()
{
    QTimer poll;
    NereusSDR::GeniusPollPacer pacer(&poll, kReceiveMs, kKeyedMs);
    pacer.setHoldMsForTesting(kNeverMs);
    pacer.setDeviceBusy(false);
    pacer.setKeyed(false);
    QVERIFY(!pacer.fast());
    QCOMPARE(poll.interval(), kReceiveMs);
}

void GeniusPollPacerTest::theHoldRunsFromTheLastOfTheTwo()
{
    constexpr int kHoldMs = 1000;
    constexpr int kMostOfTheHoldMs = 600;
    QTimer poll;
    NereusSDR::GeniusPollPacer pacer(&poll, kReceiveMs, kKeyedMs);
    pacer.setHoldMsForTesting(kHoldMs);
    pacer.setKeyed(true);
    pacer.setDeviceBusy(true);
    // The Core unkeys; the device still says keyed.
    pacer.setKeyed(false);
    QTest::qWait(kMostOfTheHoldMs);
    QCOMPARE(poll.interval(), kKeyedMs);
    // The device unkeys: the hold starts here, not at the Core's unkey.
    pacer.setDeviceBusy(false);
    QTest::qWait(kMostOfTheHoldMs);
    QCOMPARE(poll.interval(), kKeyedMs);
    QTRY_COMPARE(poll.interval(), kReceiveMs);
}

void GeniusPollPacerTest::keyedAgainDuringTheHoldStaysFast()
{
    constexpr int kHoldMs = 100;
    QTimer poll;
    NereusSDR::GeniusPollPacer pacer(&poll, kReceiveMs, kKeyedMs);
    pacer.setHoldMsForTesting(kHoldMs);
    pacer.setKeyed(true);
    pacer.setKeyed(false);
    pacer.setKeyed(true);
    // Well past the hold the first unkey started.
    QTest::qWait(3 * kHoldMs);
    QVERIFY(pacer.fast());
    QCOMPARE(poll.interval(), kKeyedMs);
}

void GeniusPollPacerTest::aNewConnectionKeepsOnlyTheCoresState()
{
    QTimer poll;
    NereusSDR::GeniusPollPacer pacer(&poll, kReceiveMs, kKeyedMs);
    pacer.setHoldMsForTesting(kNeverMs);

    // What the last connection's device said is gone.
    pacer.setDeviceBusy(true);
    pacer.reset();
    QCOMPARE(poll.interval(), kReceiveMs);

    // So is a hold that was running.
    pacer.setKeyed(true);
    pacer.setKeyed(false);
    QCOMPARE(poll.interval(), kKeyedMs);
    pacer.reset();
    QVERIFY(!pacer.fast());
    QCOMPARE(poll.interval(), kReceiveMs);

    // The Core on the air still counts.
    pacer.setKeyed(true);
    pacer.reset();
    QVERIFY(pacer.fast());
    QCOMPARE(poll.interval(), kKeyedMs);
}

void GeniusPollPacerTest::theKeyedIntervalAppliesAtOnce()
{
    constexpr int kShortMs = 20;
    QTimer poll;
    NereusSDR::GeniusPollPacer pacer(&poll, kNeverMs, kShortMs);
    QSignalSpy ticks(&poll, &QTimer::timeout);
    poll.start();
    // The receive tick under way would run for a minute.
    pacer.setKeyed(true);
    QVERIFY(ticks.wait(3000));
}

void GeniusPollPacerTest::anUnchangedIntervalDoesNotRestartThePoll()
{
    constexpr int kLongKeyedMs = 4000;
    constexpr int kPartMs = 400;
    QTimer poll;
    NereusSDR::GeniusPollPacer pacer(&poll, kNeverMs, kLongKeyedMs);
    poll.start();
    pacer.setKeyed(true);
    QTest::qWait(kPartMs);
    // Already fast: the device saying keyed too, and saying it again on
    // every status answer, leaves the tick under way alone.
    pacer.setDeviceBusy(true);
    pacer.setDeviceBusy(true);
    pacer.setKeyed(true);
    QVERIFY(poll.isActive());
    QVERIFY2(poll.remainingTime() <= kLongKeyedMs - kPartMs / 2,
             qPrintable(QString::number(poll.remainingTime())));
}

void GeniusPollPacerTest::aStoppedPollIsNotStarted()
{
    QTimer poll;
    NereusSDR::GeniusPollPacer pacer(&poll, kReceiveMs, kKeyedMs);
    pacer.setHoldMsForTesting(kNeverMs);
    // No connection: the interval follows, and nothing polls.
    pacer.setKeyed(true);
    QCOMPARE(poll.interval(), kKeyedMs);
    QVERIFY(!poll.isActive());
    pacer.reset();
    QVERIFY(!poll.isActive());
}

QTEST_GUILESS_MAIN(GeniusPollPacerTest)
#include "tst_genius_poll_pacer.moc"
