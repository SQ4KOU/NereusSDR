// =================================================================
// tests/tst_pgxl_connection_keepalive.cpp  (NereusSDR)
// =================================================================
// NereusSDR-native test. No AetherSDR equivalent (keepalive is a
// NereusSDR Tier 2 addition per design doc §2 and §6.4).
// Wire format from FlexRadio PowerGenius Ethernet API wiki spec.
// =================================================================
// Modification history (NereusSDR):
//   2026-05-19  Created by J.J. Boyd (KG4VCF), with AI-assisted
//                 transformation via Anthropic Claude Code.
//                 Tests: enableKeepalive frame format, keepalive timer
//                 activation via testKeepaliveTimerActive() seam.
//   2026-10-12  J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude
//                 Code. One `status` in flight: the poll and the
//                 keepalive wait for the amplifier's answer, a lost
//                 answer does not stop the poll, and a new connection
//                 asks at once.
//   2026-10-12  J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude
//                 Code. The poll's rate: every 100 ms while the Core or
//                 the amplifier says keyed and for a hold after, every
//                 200 ms otherwise (GeniusPollPacer.h).
// =================================================================

#include <QtTest/QtTest>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include "core/AppSettings.h"
#include "core/PgxlConnection.h"

namespace {

// An amplifier on loopback that answers only when the test tells it to.
// It records every command it is sent, with the time it arrived.
class FakeAmp : public QObject {
public:
    FakeAmp() { QVERIFY(m_server.listen(QHostAddress::LocalHost, 0)); m_clock.start(); }
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
                    m_statusAtMs.push_back(m_clock.elapsed());
                }
            }
        });
        m_peer->write("V3.9.9\n");
        m_peer->flush();
        return true;
    }
    int statusCount() const { return int(m_statusSeqs.size()); }
    // The amplifier's answer to the newest `status`.
    void answerStatus()
    {
        m_peer->write(QStringLiteral("R%1|0|state=STANDBY bandA=80\n")
                          .arg(m_statusSeqs.last()).toUtf8());
        m_peer->flush();
    }
    // The same, saying which state the amplifier is in.
    void answerStatusInState(const QString& state)
    {
        m_peer->write(QStringLiteral("R%1|0|state=%2 bandA=80\n")
                          .arg(m_statusSeqs.last()).arg(state).toUtf8());
        m_peer->flush();
    }
    qint64 msBetweenLastTwoStatus() const
    {
        return m_statusAtMs.at(m_statusAtMs.size() - 1) - m_statusAtMs.at(m_statusAtMs.size() - 2);
    }
    void hangUp() { m_peer->close(); }

private:
    QTcpServer m_server;
    QPointer<QTcpSocket> m_peer;
    QByteArray m_buffer;
    QList<quint32> m_statusSeqs;
    QList<qint64> m_statusAtMs;
    QElapsedTimer m_clock;
};

// Three poll periods (PgxlConnection polls every 200 ms).
constexpr int kThreePollsMs = 700;

// The poll's two intervals.
constexpr int kReceiveMs = NereusSDR::PgxlConnection::kPollReceiveMs;
constexpr int kKeyedMs = NereusSDR::PgxlConnection::kPollKeyedMs;

// Longer than any case runs. As the hold, the keyed rate ends only by
// what a case names; as the reply wait, an unanswered `status` holds the
// next one back for the whole case, so the link's own live poll timer
// puts nothing on the wire.
constexpr int kNeverMs = 60000;

// The poll timer's tick, without the wait.
void poll(NereusSDR::PgxlConnection& conn)
{
    QVERIFY(QMetaObject::invokeMethod(&conn, "pollStatus", Qt::DirectConnection));
}

} // namespace

class PgxlConnectionKeepaliveTest : public QObject {
    Q_OBJECT
private slots:
    void enableKeepaliveSendsKeepaliveEnable();
    void keepaliveTimerStartsAfterEnable();
    void statusWaitsForTheAnswerToTheOneBefore();
    void aLostAnswerDoesNotStopThePoll();
    void aNewConnectionAsksAtOnce();
    void thePollIntervalsAreTheOnesChosen();
    void aConnectionPollsAtTheReceiveInterval();
    void theCoreKeyingPollsFasterAtOnceAndForAHoldAfter();
    void theAmplifiersOwnStateRaisesThePollRate_data();
    void theAmplifiersOwnStateRaisesThePollRate();
    void theAmplifierLeavingTransmitStartsTheHold();
    void keyedAgainDuringTheHoldStaysFast();
    void aNewConnectionAfterADropPollsAtTheReceiveInterval();
    void atTheKeyedIntervalOneStatusIsStillInFlight();
};

void PgxlConnectionKeepaliveTest::enableKeepaliveSendsKeepaliveEnable() {
    NereusSDR::PgxlConnection conn;
    QSignalSpy frameSpy(&conn, &NereusSDR::PgxlConnection::testFrameWrittenForTesting);
    conn.enableKeepalive();
    // At least one frame must have been written (the keepalive enable command).
    QVERIFY(frameSpy.count() >= 1);
    bool foundKeepalive = false;
    for (const auto& call : frameSpy) {
        if (call.at(0).toString().contains("keepalive enable")) {
            foundKeepalive = true;
            break;
        }
    }
    QVERIFY(foundKeepalive);
}

void PgxlConnectionKeepaliveTest::keepaliveTimerStartsAfterEnable() {
    NereusSDR::PgxlConnection conn;
    QVERIFY(!conn.testKeepaliveTimerActive());
    conn.enableKeepalive();
    QVERIFY(conn.testKeepaliveTimerActive());
}

// The bench case (firmware 3.9.9, 2026-10-12): the amplifier closed the
// link each time a `status` reached it before its answer to the one
// before. Three times that was the keepalive's `status` 1 to 23 ms after
// the poll's; once it was the poll, with the amplifier answering slowly.
void PgxlConnectionKeepaliveTest::statusWaitsForTheAnswerToTheOneBefore() {
    FakeAmp amp;
    NereusSDR::PgxlConnection conn;
    conn.connectToPgxl(QStringLiteral("127.0.0.1"), amp.port());
    QVERIFY(amp.greet());
    QTRY_COMPARE(amp.statusCount(), 1);   // the handshake's

    // A slow amplifier: the poll does not ask again on top of it.
    QTest::qWait(kThreePollsMs);
    QCOMPARE(amp.statusCount(), 1);
    // Nor does the keepalive.
    QVERIFY(QMetaObject::invokeMethod(&conn, "onKeepaliveTimeout", Qt::DirectConnection));
    QTest::qWait(50);
    QCOMPARE(amp.statusCount(), 1);

    // Answered: the next poll asks, once.
    amp.answerStatus();
    QTRY_COMPARE(amp.statusCount(), 2);
    // The keepalive coming right behind the poll's `status`, before the
    // amplifier has answered it.
    QVERIFY(QMetaObject::invokeMethod(&conn, "onKeepaliveTimeout", Qt::DirectConnection));
    QTest::qWait(kThreePollsMs);
    QCOMPARE(amp.statusCount(), 2);

    // And the meters keep moving once it answers.
    amp.answerStatus();
    QTRY_COMPARE(amp.statusCount(), 3);
}

void PgxlConnectionKeepaliveTest::aLostAnswerDoesNotStopThePoll() {
    constexpr int kWaitMs = 500;
    FakeAmp amp;
    NereusSDR::PgxlConnection conn;
    conn.testSetStatusReplyWaitMs(kWaitMs);
    conn.connectToPgxl(QStringLiteral("127.0.0.1"), amp.port());
    QVERIFY(amp.greet());
    QTRY_COMPARE(amp.statusCount(), 1);
    // Never answered: the poll asks again, but only after the wait.
    QTRY_COMPARE_WITH_TIMEOUT(amp.statusCount(), 2, 5000);
    QVERIFY2(amp.msBetweenLastTwoStatus() >= kWaitMs - 20,
             qPrintable(QString::number(amp.msBetweenLastTwoStatus())));
}

void PgxlConnectionKeepaliveTest::aNewConnectionAsksAtOnce() {
    NereusSDR::AppSettings::instance().setValue(QStringLiteral("PGXL_AutoReconnect"),
                                                QStringLiteral("True"));
    FakeAmp amp;
    NereusSDR::PgxlConnection conn;
    conn.testSetReconnectBackoffUnitMs(20);
    // Longer than the test: only a new connection can release the wait.
    conn.testSetStatusReplyWaitMs(60000);
    conn.connectToPgxl(QStringLiteral("127.0.0.1"), amp.port());
    QVERIFY(amp.greet());
    QTRY_COMPARE(amp.statusCount(), 1);
    // The amplifier drops the link with that `status` unanswered.
    amp.hangUp();
    QVERIFY(amp.greet());
    QTRY_COMPARE(amp.statusCount(), 2);
}

void PgxlConnectionKeepaliveTest::thePollIntervalsAreTheOnesChosen() {
    QCOMPARE(NereusSDR::PgxlConnection::kPollReceiveMs, 200);
    QCOMPARE(NereusSDR::PgxlConnection::kPollKeyedMs, 100);
}

void PgxlConnectionKeepaliveTest::aConnectionPollsAtTheReceiveInterval() {
    FakeAmp amp;
    NereusSDR::PgxlConnection conn;
    QSignalSpy answers(&conn, &NereusSDR::PgxlConnection::statusUpdated);
    conn.testSetPollHoldMs(kNeverMs);
    QCOMPARE(conn.testPollIntervalMs(), kReceiveMs);
    conn.connectToPgxl(QStringLiteral("127.0.0.1"), amp.port());
    QVERIFY(amp.greet());
    QTRY_COMPARE(amp.statusCount(), 1);
    QCOMPARE(conn.testPollIntervalMs(), kReceiveMs);
    // An amplifier in receive saying so starts no hold.
    amp.answerStatus();
    QTRY_COMPARE(answers.count(), 1);
    QCOMPARE(conn.testPollIntervalMs(), kReceiveMs);
}

void PgxlConnectionKeepaliveTest::theCoreKeyingPollsFasterAtOnceAndForAHoldAfter() {
    FakeAmp amp;
    NereusSDR::PgxlConnection conn;
    conn.testSetPollHoldMs(200);
    conn.testSetStatusReplyWaitMs(kNeverMs);
    conn.connectToPgxl(QStringLiteral("127.0.0.1"), amp.port());
    QVERIFY(amp.greet());
    QTRY_COMPARE(amp.statusCount(), 1);

    conn.setKeyed(true);
    QCOMPARE(conn.testPollIntervalMs(), kKeyedMs);
    // Unkeyed: the keyed interval stays for the hold, then receive's returns.
    conn.setKeyed(false);
    QCOMPARE(conn.testPollIntervalMs(), kKeyedMs);
    QTRY_COMPARE(conn.testPollIntervalMs(), kReceiveMs);
}

void PgxlConnectionKeepaliveTest::theAmplifiersOwnStateRaisesThePollRate_data() {
    QTest::addColumn<QString>("state");
    QTest::addColumn<int>("intervalMs");
    QTest::newRow("TRANSMIT_A") << QStringLiteral("TRANSMIT_A") << kKeyedMs;
    QTest::newRow("TRANSMIT_B") << QStringLiteral("TRANSMIT_B") << kKeyedMs;
    QTest::newRow("IDLE") << QStringLiteral("IDLE") << kReceiveMs;
    QTest::newRow("STANDBY") << QStringLiteral("STANDBY") << kReceiveMs;
}

void PgxlConnectionKeepaliveTest::theAmplifiersOwnStateRaisesThePollRate() {
    QFETCH(QString, state);
    QFETCH(int, intervalMs);
    FakeAmp amp;
    NereusSDR::PgxlConnection conn;
    QSignalSpy answers(&conn, &NereusSDR::PgxlConnection::statusUpdated);
    conn.testSetPollHoldMs(kNeverMs);
    conn.connectToPgxl(QStringLiteral("127.0.0.1"), amp.port());
    QVERIFY(amp.greet());
    QTRY_COMPARE(amp.statusCount(), 1);
    // The Core never said keyed: the amplifier's answer alone decides.
    amp.answerStatusInState(state);
    QTRY_COMPARE(answers.count(), 1);
    QCOMPARE(conn.testPollIntervalMs(), intervalMs);
}

void PgxlConnectionKeepaliveTest::theAmplifierLeavingTransmitStartsTheHold() {
    FakeAmp amp;
    NereusSDR::PgxlConnection conn;
    QSignalSpy answers(&conn, &NereusSDR::PgxlConnection::statusUpdated);
    conn.testSetPollHoldMs(500);
    conn.connectToPgxl(QStringLiteral("127.0.0.1"), amp.port());
    QVERIFY(amp.greet());
    QTRY_COMPARE(amp.statusCount(), 1);
    amp.answerStatusInState(QStringLiteral("TRANSMIT_A"));
    QTRY_COMPARE(answers.count(), 1);
    QCOMPARE(conn.testPollIntervalMs(), kKeyedMs);

    // Back in receive, as a state push: still fast for the hold, then not.
    conn.injectLineForTesting(QStringLiteral("S0|amplifier state=IDLE"));
    QCOMPARE(conn.testPollIntervalMs(), kKeyedMs);
    QTRY_COMPARE(conn.testPollIntervalMs(), kReceiveMs);
}

void PgxlConnectionKeepaliveTest::keyedAgainDuringTheHoldStaysFast() {
    constexpr int kHoldMs = 100;
    FakeAmp amp;
    NereusSDR::PgxlConnection conn;
    conn.testSetPollHoldMs(kHoldMs);
    conn.testSetStatusReplyWaitMs(kNeverMs);
    conn.connectToPgxl(QStringLiteral("127.0.0.1"), amp.port());
    QVERIFY(amp.greet());
    QTRY_COMPARE(amp.statusCount(), 1);

    conn.setKeyed(true);
    conn.setKeyed(false);
    conn.setKeyed(true);
    // Well past the hold the first unkey started.
    QTest::qWait(3 * kHoldMs);
    QCOMPARE(conn.testPollIntervalMs(), kKeyedMs);
}

void PgxlConnectionKeepaliveTest::aNewConnectionAfterADropPollsAtTheReceiveInterval() {
    NereusSDR::AppSettings::instance().setValue(QStringLiteral("PGXL_AutoReconnect"),
                                                QStringLiteral("True"));
    FakeAmp amp;
    NereusSDR::PgxlConnection conn;
    QSignalSpy answers(&conn, &NereusSDR::PgxlConnection::statusUpdated);
    conn.testSetReconnectBackoffUnitMs(20);
    conn.testSetPollHoldMs(kNeverMs);
    conn.testSetStatusReplyWaitMs(kNeverMs);
    conn.connectToPgxl(QStringLiteral("127.0.0.1"), amp.port());
    QVERIFY(amp.greet());
    QTRY_COMPARE(amp.statusCount(), 1);
    amp.answerStatusInState(QStringLiteral("TRANSMIT_A"));
    QTRY_COMPARE(answers.count(), 1);
    QCOMPARE(conn.testPollIntervalMs(), kKeyedMs);

    // The amplifier drops the link while it says it is transmitting.
    amp.hangUp();
    QVERIFY(amp.greet());
    QTRY_COMPARE(amp.statusCount(), 2);   // the new handshake's
    QCOMPARE(conn.testPollIntervalMs(), kReceiveMs);
}

void PgxlConnectionKeepaliveTest::atTheKeyedIntervalOneStatusIsStillInFlight() {
    FakeAmp amp;
    NereusSDR::PgxlConnection conn;
    conn.testSetPollHoldMs(kNeverMs);
    conn.testSetStatusReplyWaitMs(kNeverMs);
    conn.connectToPgxl(QStringLiteral("127.0.0.1"), amp.port());
    QVERIFY(amp.greet());
    QTRY_COMPARE(amp.statusCount(), 1);   // the handshake's, unanswered

    conn.setKeyed(true);
    QCOMPARE(conn.testPollIntervalMs(), kKeyedMs);
    // Two ticks on top of the unanswered `status`, and the live timer's own
    // for three keyed periods: none reaches the amplifier.
    poll(conn);
    poll(conn);
    QTest::qWait(3 * kKeyedMs + 50);
    QCOMPARE(amp.statusCount(), 1);

    // Answered: one more goes, and again only one.
    amp.answerStatus();
    QTRY_COMPARE(amp.statusCount(), 2);
    poll(conn);
    poll(conn);
    QTest::qWait(3 * kKeyedMs + 50);
    QCOMPARE(amp.statusCount(), 2);
}

QTEST_GUILESS_MAIN(PgxlConnectionKeepaliveTest)
#include "tst_pgxl_connection_keepalive.moc"
