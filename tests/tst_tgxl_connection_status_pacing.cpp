// =================================================================
// tests/tst_tgxl_connection_status_pacing.cpp  (NereusSDR)
// =================================================================
// NereusSDR-native test. One `status` in flight on the Tuner Genius XL
// link: the poll does not ask again while the tuner has not answered
// the one before (GeniusStatusGate.h). The tuner's answer shape,
// `S<seq>|status ...`, is from captures/flex-tgxl-direct-NOTES.md:73.
// =================================================================
// Modification history (NereusSDR):
//   2026-10-12  Created by J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
//   2026-10-12  J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude
//                 Code. The poll's rate: every 62 ms while the Core or
//                 the tuner says keyed or tuning and for a hold after,
//                 every 250 ms otherwise (GeniusPollPacer.h).
// =================================================================

#include <QtTest/QtTest>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include "core/AppSettings.h"
#include "core/TgxlConnection.h"

namespace {

// A tuner on loopback that answers only when the test tells it to. It
// records every `status` it is sent.
class FakeTuner : public QObject {
public:
    FakeTuner() { QVERIFY(m_server.listen(QHostAddress::LocalHost, 0)); }
    quint16 port() const { return m_server.serverPort(); }
    // Accepts the next connection and sends the version banner.
    bool greet()
    {
        if (!QTest::qWaitFor([this] { return m_server.hasPendingConnections(); }, 3000)) {
            return false;
        }
        m_peer = m_server.nextPendingConnection();
        m_buffer.clear();
        connect(m_peer, &QTcpSocket::readyRead, this, [this] {
            m_buffer += m_peer->readAll();
            int end = 0;
            while ((end = m_buffer.indexOf('\n')) >= 0) {
                const QString line = QString::fromUtf8(m_buffer.left(end)).trimmed();
                m_buffer.remove(0, end + 1);
                if (line.endsWith(QStringLiteral("|status"))) {
                    m_statusSeqs.push_back(line.mid(1, line.indexOf('|') - 1).toUInt());
                }
            }
        });
        m_peer->write("V1.2.17\n");
        m_peer->flush();
        return true;
    }
    int statusCount() const { return int(m_statusSeqs.size()); }
    quint32 newestStatusSeq() const { return m_statusSeqs.last(); }
    // The tuner's answer to the newest `status`.
    void answerStatus() { sendStatusLine(m_statusSeqs.last()); }
    // A status line carrying this sequence number.
    void sendStatusLine(quint32 seq)
    {
        send(QStringLiteral("S%1|status fwd=0.00 peak=0.00 max=0.00 swr=-60.0000 pttA=0 "
                            "state=1 active=1 tuning=0 bypass=0 relayC1=0 relayL=0 relayC2=0")
                 .arg(seq));
    }
    // The tuner's answer to the newest `status`, with these flags.
    void answerStatusWith(int pttA, int tuning)
    {
        send(QStringLiteral("S%1|status fwd=0.00 peak=0.00 max=0.00 swr=-60.0000 pttA=%2 "
                            "state=1 active=1 tuning=%3 bypass=0 relayC1=0 relayL=0 relayC2=0")
                 .arg(m_statusSeqs.last()).arg(pttA).arg(tuning));
    }
    // A state push the tuner sends unasked. It carries no pttA.
    void pushState(int tuning)
    {
        send(QStringLiteral("S0|state bypassA=0 state=1 tuning=%1 relayC1=16 relayL=16 relayC2=0")
                 .arg(tuning));
    }
    // The tuner refusing the newest `status`.
    void refuseStatus() { send(QStringLiteral("R%1|50000015|").arg(m_statusSeqs.last())); }
    void send(const QString& line)
    {
        m_peer->write(line.toUtf8() + '\n');
        m_peer->flush();
    }
    void hangUp() { m_peer->close(); }

private:
    QTcpServer m_server;
    QPointer<QTcpSocket> m_peer;
    QByteArray m_buffer;
    QList<quint32> m_statusSeqs;
};

// The poll timer's tick, without the wait.
void poll(NereusSDR::TgxlConnection& conn)
{
    QVERIFY(QMetaObject::invokeMethod(&conn, "pollStatus", Qt::DirectConnection));
}

// Long enough for a command written on loopback to reach the fake tuner.
constexpr int kArriveMs = 80;

// Longer than any case runs. With it, only the thing a case names can let
// the next `status` go: not the wait running out, and not the link's own
// poll timer, which is live throughout. As the hold, the keyed rate ends
// only by what a case names.
constexpr int kNeverMs = 60000;

// The poll's two intervals.
constexpr int kReceiveMs = NereusSDR::TgxlConnection::kPollReceiveMs;
constexpr int kKeyedMs = NereusSDR::TgxlConnection::kPollKeyedMs;

} // namespace

class TgxlConnectionStatusPacingTest : public QObject {
    Q_OBJECT
private slots:
    void init();
    void thePollWaitsForTheAnswerToTheOneBefore();
    void aRefusalAlsoLetsTheNextPollGo();
    void anAnswerToSomethingElseDoesNotLetItGo();
    void aStatusLineWithAnotherNumberDoesNotLetItGo();
    void aLostAnswerDoesNotStopThePoll();
    void aNewConnectionAsksAtOnce();
    void thePollIntervalsAreTheOnesChosen();
    void aConnectionPollsAtTheReceiveInterval();
    void theCoreKeyingPollsFasterAtOnceAndForAHoldAfter();
    void theTunersOwnFlagsRaiseThePollRate_data();
    void theTunersOwnFlagsRaiseThePollRate();
    void aStatePushSayingTuningRaisesIt();
    void theTunersKeyEndingStartsTheHold();
    void aStatePushWithoutThePttFlagLeavesItKeyed();
    void keyedAgainDuringTheHoldStaysFast();
    void aNewConnectionAfterADropPollsAtTheReceiveInterval();
    void atTheKeyedIntervalOneStatusIsStillInFlight();
};

void TgxlConnectionStatusPacingTest::init()
{
    NereusSDR::AppSettings::instance().setValue(QStringLiteral("TGXL_AutoReconnect"),
                                                QStringLiteral("True"));
}

void TgxlConnectionStatusPacingTest::thePollWaitsForTheAnswerToTheOneBefore()
{
    FakeTuner tuner;
    NereusSDR::TgxlConnection conn;
    QSignalSpy answers(&conn, &NereusSDR::TgxlConnection::statusUpdated);
    conn.testSetStatusReplyWaitMs(kNeverMs);
    conn.connectToTgxl(QStringLiteral("127.0.0.1"), tuner.port());
    QVERIFY(tuner.greet());
    QTRY_COMPARE(tuner.statusCount(), 1);   // the handshake's

    // A slow tuner: the poll does not ask again on top of it.
    poll(conn);
    poll(conn);
    // Nor does the keepalive.
    QVERIFY(QMetaObject::invokeMethod(&conn, "onKeepaliveTimeout", Qt::DirectConnection));
    QTest::qWait(kArriveMs);
    QCOMPARE(tuner.statusCount(), 1);

    // Answered: the next poll asks, once.
    tuner.answerStatus();
    QTRY_COMPARE(answers.count(), 1);
    poll(conn);
    poll(conn);
    QTest::qWait(kArriveMs);
    QCOMPARE(tuner.statusCount(), 2);

    // And the meters keep moving once it answers.
    tuner.answerStatus();
    QTRY_COMPARE(answers.count(), 2);
    poll(conn);
    QTRY_COMPARE(tuner.statusCount(), 3);
}

void TgxlConnectionStatusPacingTest::aRefusalAlsoLetsTheNextPollGo()
{
    FakeTuner tuner;
    NereusSDR::TgxlConnection conn;
    QSignalSpy replies(&conn, &NereusSDR::TgxlConnection::replyReceived);
    conn.testSetStatusReplyWaitMs(kNeverMs);
    conn.connectToTgxl(QStringLiteral("127.0.0.1"), tuner.port());
    QVERIFY(tuner.greet());
    QTRY_COMPARE(tuner.statusCount(), 1);

    tuner.refuseStatus();
    QTRY_COMPARE(replies.count(), 1);
    poll(conn);
    QTRY_COMPARE(tuner.statusCount(), 2);
}

void TgxlConnectionStatusPacingTest::anAnswerToSomethingElseDoesNotLetItGo()
{
    FakeTuner tuner;
    NereusSDR::TgxlConnection conn;
    QSignalSpy replies(&conn, &NereusSDR::TgxlConnection::replyReceived);
    QSignalSpy states(&conn, &NereusSDR::TgxlConnection::stateUpdated);
    conn.testSetStatusReplyWaitMs(kNeverMs);
    conn.connectToTgxl(QStringLiteral("127.0.0.1"), tuner.port());
    QVERIFY(tuner.greet());
    QTRY_COMPARE(tuner.statusCount(), 1);   // sequence 2; `info` was 1

    // The answer to `info`, and a state push the tuner sends unasked.
    tuner.send(QStringLiteral("R1|0|info serial=1 version=1.2.17 nickname=Tuner"));
    tuner.send(QStringLiteral("S0|state bypassA=0 state=1 tuning=0 relayC1=16 relayL=16 relayC2=0"));
    QTRY_COMPARE(replies.count(), 1);
    QTRY_COMPARE(states.count(), 1);
    poll(conn);
    QTest::qWait(kArriveMs);
    QCOMPARE(tuner.statusCount(), 1);
}

void TgxlConnectionStatusPacingTest::aStatusLineWithAnotherNumberDoesNotLetItGo()
{
    FakeTuner tuner;
    NereusSDR::TgxlConnection conn;
    QSignalSpy answers(&conn, &NereusSDR::TgxlConnection::statusUpdated);
    conn.testSetStatusReplyWaitMs(kNeverMs);
    conn.connectToTgxl(QStringLiteral("127.0.0.1"), tuner.port());
    QVERIFY(tuner.greet());
    QTRY_COMPARE(tuner.statusCount(), 1);

    // A status line that is not the answer to the one in flight: read for
    // its values, but the poll still waits for its own.
    tuner.sendStatusLine(tuner.newestStatusSeq() + 40);
    QTRY_COMPARE(answers.count(), 1);
    poll(conn);
    QTest::qWait(kArriveMs);
    QCOMPARE(tuner.statusCount(), 1);

    tuner.answerStatus();
    QTRY_COMPARE(answers.count(), 2);
    poll(conn);
    QTRY_COMPARE(tuner.statusCount(), 2);
}

void TgxlConnectionStatusPacingTest::aLostAnswerDoesNotStopThePoll()
{
    // Shorter than the time the case has already spent when it is set.
    constexpr int kWaitedOutMs = kArriveMs / 2;
    FakeTuner tuner;
    NereusSDR::TgxlConnection conn;
    QSignalSpy sent(&conn, &NereusSDR::TgxlConnection::testFrameWrittenForTesting);
    conn.testSetStatusReplyWaitMs(kNeverMs);
    conn.connectToTgxl(QStringLiteral("127.0.0.1"), tuner.port());
    QVERIFY(tuner.greet());
    QTRY_COMPARE(tuner.statusCount(), 1);

    // Never answered: held back inside the wait. The link's live poll
    // timer is held back with it, so nothing here turns on one of its
    // ticks.
    poll(conn);
    QTest::qWait(kArriveMs);
    QCOMPARE(tuner.statusCount(), 1);

    // The wait has run out (the `status` is kArriveMs old, the wait now
    // half that): this poll sends, inside the call and before any timer
    // can tick.
    const int sentBefore = int(sent.count());
    conn.testSetStatusReplyWaitMs(kWaitedOutMs);
    poll(conn);
    QCOMPARE(int(sent.count()), sentBefore + 1);
    QVERIFY(sent.last().at(0).toString().endsWith(QStringLiteral("|status")));
    // And the one it sent is waited for in its turn.
    conn.testSetStatusReplyWaitMs(kNeverMs);
    poll(conn);
    QCOMPARE(int(sent.count()), sentBefore + 1);
    QTRY_COMPARE(tuner.statusCount(), 2);
    QTest::qWait(kArriveMs);
    QCOMPARE(tuner.statusCount(), 2);
}

void TgxlConnectionStatusPacingTest::aNewConnectionAsksAtOnce()
{
    FakeTuner tuner;
    NereusSDR::TgxlConnection conn;
    QSignalSpy answers(&conn, &NereusSDR::TgxlConnection::statusUpdated);
    conn.testSetReconnectBackoffUnitMs(20);
    conn.testSetStatusReplyWaitMs(kNeverMs);
    conn.connectToTgxl(QStringLiteral("127.0.0.1"), tuner.port());
    QVERIFY(tuner.greet());
    QTRY_COMPARE(tuner.statusCount(), 1);
    // The tuner drops the link with that `status` unanswered.
    tuner.hangUp();
    QVERIFY(tuner.greet());
    QTRY_COMPARE(tuner.statusCount(), 2);   // the new handshake's
    tuner.answerStatus();
    QTRY_COMPARE(answers.count(), 1);
    poll(conn);
    QTRY_COMPARE(tuner.statusCount(), 3);
}

void TgxlConnectionStatusPacingTest::thePollIntervalsAreTheOnesChosen()
{
    QCOMPARE(NereusSDR::TgxlConnection::kPollReceiveMs, 250);
    QCOMPARE(NereusSDR::TgxlConnection::kPollKeyedMs, 62);
}

void TgxlConnectionStatusPacingTest::aConnectionPollsAtTheReceiveInterval()
{
    FakeTuner tuner;
    NereusSDR::TgxlConnection conn;
    QSignalSpy answers(&conn, &NereusSDR::TgxlConnection::statusUpdated);
    conn.testSetPollHoldMs(kNeverMs);
    QCOMPARE(conn.testPollIntervalMs(), kReceiveMs);
    conn.connectToTgxl(QStringLiteral("127.0.0.1"), tuner.port());
    QVERIFY(tuner.greet());
    QTRY_COMPARE(tuner.statusCount(), 1);
    QCOMPARE(conn.testPollIntervalMs(), kReceiveMs);
    // A tuner in receive saying so starts no hold.
    tuner.answerStatus();
    QTRY_COMPARE(answers.count(), 1);
    QCOMPARE(conn.testPollIntervalMs(), kReceiveMs);
}

void TgxlConnectionStatusPacingTest::theCoreKeyingPollsFasterAtOnceAndForAHoldAfter()
{
    FakeTuner tuner;
    NereusSDR::TgxlConnection conn;
    conn.testSetPollHoldMs(200);
    conn.testSetStatusReplyWaitMs(kNeverMs);
    conn.connectToTgxl(QStringLiteral("127.0.0.1"), tuner.port());
    QVERIFY(tuner.greet());
    QTRY_COMPARE(tuner.statusCount(), 1);

    conn.setKeyed(true);
    QCOMPARE(conn.testPollIntervalMs(), kKeyedMs);
    // Unkeyed: the keyed interval stays for the hold, then receive's returns.
    conn.setKeyed(false);
    QCOMPARE(conn.testPollIntervalMs(), kKeyedMs);
    QTRY_COMPARE(conn.testPollIntervalMs(), kReceiveMs);
}

void TgxlConnectionStatusPacingTest::theTunersOwnFlagsRaiseThePollRate_data()
{
    QTest::addColumn<QString>("flags");
    QTest::addColumn<int>("intervalMs");
    QTest::newRow("pttA=1") << QStringLiteral("pttA=1 pttB=0 tuning=0") << kKeyedMs;
    QTest::newRow("pttB=1") << QStringLiteral("pttA=0 pttB=1 tuning=0") << kKeyedMs;
    QTest::newRow("tuning=1") << QStringLiteral("pttA=0 pttB=0 tuning=1") << kKeyedMs;
    QTest::newRow("none") << QStringLiteral("pttA=0 pttB=0 tuning=0") << kReceiveMs;
}

void TgxlConnectionStatusPacingTest::theTunersOwnFlagsRaiseThePollRate()
{
    QFETCH(QString, flags);
    QFETCH(int, intervalMs);
    FakeTuner tuner;
    NereusSDR::TgxlConnection conn;
    QSignalSpy answers(&conn, &NereusSDR::TgxlConnection::statusUpdated);
    conn.testSetPollHoldMs(kNeverMs);
    conn.connectToTgxl(QStringLiteral("127.0.0.1"), tuner.port());
    QVERIFY(tuner.greet());
    QTRY_COMPARE(tuner.statusCount(), 1);
    // The Core never said keyed: the tuner's answer alone decides.
    tuner.send(QStringLiteral("S%1|status fwd=0.00 swr=-60.0000 %2 state=1")
                   .arg(tuner.newestStatusSeq()).arg(flags));
    QTRY_COMPARE(answers.count(), 1);
    QCOMPARE(conn.testPollIntervalMs(), intervalMs);
}

void TgxlConnectionStatusPacingTest::aStatePushSayingTuningRaisesIt()
{
    FakeTuner tuner;
    NereusSDR::TgxlConnection conn;
    QSignalSpy states(&conn, &NereusSDR::TgxlConnection::stateUpdated);
    conn.testSetPollHoldMs(kNeverMs);
    conn.testSetStatusReplyWaitMs(kNeverMs);
    conn.connectToTgxl(QStringLiteral("127.0.0.1"), tuner.port());
    QVERIFY(tuner.greet());
    QTRY_COMPARE(tuner.statusCount(), 1);
    tuner.pushState(1);
    QTRY_COMPARE(states.count(), 1);
    QCOMPARE(conn.testPollIntervalMs(), kKeyedMs);
}

void TgxlConnectionStatusPacingTest::theTunersKeyEndingStartsTheHold()
{
    FakeTuner tuner;
    NereusSDR::TgxlConnection conn;
    QSignalSpy answers(&conn, &NereusSDR::TgxlConnection::statusUpdated);
    conn.testSetPollHoldMs(500);
    conn.connectToTgxl(QStringLiteral("127.0.0.1"), tuner.port());
    QVERIFY(tuner.greet());
    QTRY_COMPARE(tuner.statusCount(), 1);
    tuner.answerStatusWith(1, 0);
    QTRY_COMPARE(answers.count(), 1);
    QCOMPARE(conn.testPollIntervalMs(), kKeyedMs);

    // The key ends: still fast for the hold, then not.
    conn.injectLineForTesting(QStringLiteral("S0|status fwd=0.00 pttA=0 tuning=0"));
    QCOMPARE(answers.count(), 2);
    QCOMPARE(conn.testPollIntervalMs(), kKeyedMs);
    QTRY_COMPARE(conn.testPollIntervalMs(), kReceiveMs);
}

void TgxlConnectionStatusPacingTest::aStatePushWithoutThePttFlagLeavesItKeyed()
{
    constexpr int kHoldMs = 100;
    FakeTuner tuner;
    NereusSDR::TgxlConnection conn;
    QSignalSpy answers(&conn, &NereusSDR::TgxlConnection::statusUpdated);
    QSignalSpy states(&conn, &NereusSDR::TgxlConnection::stateUpdated);
    conn.testSetPollHoldMs(kHoldMs);
    conn.testSetStatusReplyWaitMs(kNeverMs);
    conn.connectToTgxl(QStringLiteral("127.0.0.1"), tuner.port());
    QVERIFY(tuner.greet());
    QTRY_COMPARE(tuner.statusCount(), 1);
    // Keyed, with the next `status` held back so nothing says otherwise.
    tuner.send(QStringLiteral("S0|status fwd=50.00 pttA=1 tuning=0"));
    QTRY_COMPARE(answers.count(), 1);
    QCOMPARE(conn.testPollIntervalMs(), kKeyedMs);

    // A state push has `tuning` and no `pttA`: the key stands, and no hold
    // runs out under it.
    tuner.pushState(0);
    QTRY_COMPARE(states.count(), 1);
    QTest::qWait(3 * kHoldMs);
    QCOMPARE(conn.testPollIntervalMs(), kKeyedMs);
}

void TgxlConnectionStatusPacingTest::keyedAgainDuringTheHoldStaysFast()
{
    constexpr int kHoldMs = 100;
    FakeTuner tuner;
    NereusSDR::TgxlConnection conn;
    conn.testSetPollHoldMs(kHoldMs);
    conn.testSetStatusReplyWaitMs(kNeverMs);
    conn.connectToTgxl(QStringLiteral("127.0.0.1"), tuner.port());
    QVERIFY(tuner.greet());
    QTRY_COMPARE(tuner.statusCount(), 1);

    conn.setKeyed(true);
    conn.setKeyed(false);
    conn.setKeyed(true);
    // Well past the hold the first unkey started.
    QTest::qWait(3 * kHoldMs);
    QCOMPARE(conn.testPollIntervalMs(), kKeyedMs);
}

void TgxlConnectionStatusPacingTest::aNewConnectionAfterADropPollsAtTheReceiveInterval()
{
    FakeTuner tuner;
    NereusSDR::TgxlConnection conn;
    QSignalSpy answers(&conn, &NereusSDR::TgxlConnection::statusUpdated);
    conn.testSetReconnectBackoffUnitMs(20);
    conn.testSetPollHoldMs(kNeverMs);
    conn.testSetStatusReplyWaitMs(kNeverMs);
    conn.connectToTgxl(QStringLiteral("127.0.0.1"), tuner.port());
    QVERIFY(tuner.greet());
    QTRY_COMPARE(tuner.statusCount(), 1);
    tuner.answerStatusWith(1, 1);
    QTRY_COMPARE(answers.count(), 1);
    QCOMPARE(conn.testPollIntervalMs(), kKeyedMs);

    // The tuner drops the link while it says it is keyed and tuning.
    tuner.hangUp();
    QVERIFY(tuner.greet());
    QTRY_COMPARE(tuner.statusCount(), 2);   // the new handshake's
    QCOMPARE(conn.testPollIntervalMs(), kReceiveMs);
    // And the new connection's first flag stands alone: a push without
    // pttA does not bring the old key back.
    QSignalSpy states(&conn, &NereusSDR::TgxlConnection::stateUpdated);
    tuner.pushState(0);
    QTRY_COMPARE(states.count(), 1);
    QCOMPARE(conn.testPollIntervalMs(), kReceiveMs);
}

void TgxlConnectionStatusPacingTest::atTheKeyedIntervalOneStatusIsStillInFlight()
{
    FakeTuner tuner;
    NereusSDR::TgxlConnection conn;
    QSignalSpy answers(&conn, &NereusSDR::TgxlConnection::statusUpdated);
    conn.testSetPollHoldMs(kNeverMs);
    conn.testSetStatusReplyWaitMs(kNeverMs);
    conn.connectToTgxl(QStringLiteral("127.0.0.1"), tuner.port());
    QVERIFY(tuner.greet());
    QTRY_COMPARE(tuner.statusCount(), 1);   // the handshake's, unanswered

    conn.setKeyed(true);
    QCOMPARE(conn.testPollIntervalMs(), kKeyedMs);
    // Two ticks on top of the unanswered `status`, and the live timer's own
    // for several keyed periods: none reaches the tuner.
    poll(conn);
    poll(conn);
    QTest::qWait(4 * kKeyedMs);
    QCOMPARE(tuner.statusCount(), 1);

    // Answered: one more goes, and again only one.
    tuner.answerStatus();
    QTRY_COMPARE(answers.count(), 1);
    QTRY_COMPARE(tuner.statusCount(), 2);
    poll(conn);
    poll(conn);
    QTest::qWait(4 * kKeyedMs);
    QCOMPARE(tuner.statusCount(), 2);
}

QTEST_GUILESS_MAIN(TgxlConnectionStatusPacingTest)
#include "tst_tgxl_connection_status_pacing.moc"
