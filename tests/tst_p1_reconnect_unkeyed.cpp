// no-port-check: NereusSDR-original test. The Thetis behaviour it guards is
// cited beside the code in P1RadioConnection.cpp; this file translates no C#.
//
// tests/tst_p1_reconnect_unkeyed.cpp
//
// Transmit safety on a Protocol 1 link loss (whole-branch review, protocol
// reviewer, 2026-09-30). The P1 connection reconnects by itself after its
// watchdog declares the link lost. Before this guard it kept m_mox, the
// PureSignal run flag, the T/R relay and the drive across that reconnect,
// so a link dropped while transmitting came back with C0 bit 0 (MOX) set
// on the priming burst and on every ep2 frame after it: the radio keyed
// again with no operator action.
//
// Against the in-process fake P1 radio (never a real radio):
//   1. Keyed, the link drops and comes back: every ep2 frame the radio sees
//      after the loss is unkeyed, the first subframe is bank 0, and bank 10
//      and bank 11 carry zero drive, the relay released and PureSignal off.
//   2. After the loss a key is refused until it is released: setMox(true)
//      leaves the wire unkeyed; setMox(false) then setMox(true) keys.
//   3. A stale key set before connectToRadio does not reach the radio.

#include <QtTest/QtTest>
#include <QElapsedTimer>

#include <memory>

#include "core/P1RadioConnection.h"
#include "core/HpsdrModel.h"
#include "fakes/P1FakeRadio.h"

using namespace NereusSDR;
using NereusSDR::Test::P1FakeRadio;

namespace {

constexpr int kTestWatchdogSilenceMs   = 150;
constexpr int kTestReconnectIntervalMs = 300;

// Bank addresses in C0 bits 1-7 (networkproto1.c WriteMainLoop): bank 0 is
// 0x00, bank 10 is 0x12, bank 11 is 0x14. Bit 0 is MOX.
constexpr quint8 kBank0Address  = 0x00;
constexpr quint8 kBank10Address = 0x12;
constexpr quint8 kBank11Address = 0x14;

RadioInfo makeInfo(const P1FakeRadio& fake, HPSDRHW board)
{
    RadioInfo info;
    info.address         = fake.localAddress();
    info.port            = fake.localPort();
    info.boardType       = board;
    info.protocol        = ProtocolVersion::Protocol1;
    info.firmwareVersion = 72;
    info.macAddress      = QStringLiteral("aa:bb:cc:11:22:33");
    return info;
}

// As tst_reconnect_on_silence: the connect watchdog is a hard 2 s deadline
// that an event loop starved by a loaded machine can trip while the fake is
// healthy, so a torn-down attempt is retried on a fresh object.
std::unique_ptr<P1RadioConnection> bringLinkUp(const P1FakeRadio& fake, HPSDRHW board)
{
    constexpr int kConnectAttempts  = 4;
    constexpr int kPerAttemptWaitMs = 3000;
    for (int attempt = 0; attempt < kConnectAttempts; ++attempt) {
        auto conn = std::make_unique<P1RadioConnection>();
        conn->init();
        conn->setReconnectTimingForTest(kTestWatchdogSilenceMs, kTestReconnectIntervalMs);
        conn->connectToRadio(makeInfo(fake, board));
        QElapsedTimer waited;
        waited.start();
        while (waited.elapsed() < kPerAttemptWaitMs) {
            if (conn->state() == ConnectionState::Connected) {
                return conn;
            }
            if (conn->state() == ConnectionState::Disconnected) {
                break;
            }
            QTest::qWait(10);
        }
    }
    return nullptr;
}

quint8 c0(const QByteArray& cc, int subframe)
{
    return static_cast<quint8>(cc.at(subframe * 5));
}

bool frameKeyed(const QByteArray& cc)
{
    return (c0(cc, 0) & 0x01) != 0 || (c0(cc, 1) & 0x01) != 0;
}

bool anyKeyed(const QList<QByteArray>& log)
{
    for (const QByteArray& cc : log) {
        if (frameKeyed(cc)) {
            return true;
        }
    }
    return false;
}

// Index of the first keyed frame, or -1.
int firstKeyed(const QList<QByteArray>& log)
{
    for (int i = 0; i < log.size(); ++i) {
        if (frameKeyed(log.at(i))) {
            return i;
        }
    }
    return -1;
}

// The first subframe in the log carrying `address`, as five C&C bytes.
QByteArray firstBank(const QList<QByteArray>& log, quint8 address)
{
    for (const QByteArray& cc : log) {
        for (int sub = 0; sub < 2; ++sub) {
            if ((c0(cc, sub) & 0xFE) == address) {
                return cc.mid(sub * 5, 5);
            }
        }
    }
    return {};
}

// Keys the link, drops it, and brings it back. On return the fake's log
// holds only what was sent from the loss onward.
void keyThenDropAndReconnect(P1FakeRadio& fake, P1RadioConnection& conn)
{
    QTRY_VERIFY_WITH_TIMEOUT(fake.isRunning(), 3000);

    conn.setTxDrive(200);
    conn.setTrxRelay(true);
    conn.setPuresignalRun(true);
    conn.setMox(true);

    // The key reached the wire before the loss, so the test can tell an
    // unkeyed reconnect from a key that never went out.
    QTRY_VERIFY_WITH_TIMEOUT(anyKeyed(fake.ep2CcReceived()), 3000);

    // The radio comes back the moment the loss is declared (see
    // tst_reconnect_on_silence for why this rides the transition). The
    // fake clears its log at the reconnect's metis-stop, so frames sent
    // before the loss and still in flight are not read as the reconnect's.
    bool sawLinkLost = false;
    QObject::connect(&conn, &P1RadioConnection::connectionStateChanged, &conn,
                     [&fake, &sawLinkLost](ConnectionState s) {
                         if (s == ConnectionState::LinkLost && !sawLinkLost) {
                             sawLinkLost = true;
                             fake.resume();
                         }
                     });
    fake.goSilent();
    QTRY_VERIFY_WITH_TIMEOUT(sawLinkLost, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(conn.state() == ConnectionState::Connected, 3000);
    QVERIFY(fake.metisStopCount() >= 1);
    // Past the priming bursts and well into the round robin.
    QTRY_VERIFY_WITH_TIMEOUT(fake.ep2CcReceived().size() >= 40, 3000);
}

} // namespace

class TestP1ReconnectUnkeyed : public QObject {
    Q_OBJECT

private slots:
    void reconnectAfterKeyedLossIsUnkeyed_data()
    {
        QTest::addColumn<int>("board");
        QTest::newRow("Hermes") << int(HPSDRHW::Hermes);
        QTest::newRow("HermesLite") << int(HPSDRHW::HermesLite);
    }

    void reconnectAfterKeyedLossIsUnkeyed()
    {
        QFETCH(int, board);
        const auto hw = static_cast<HPSDRHW>(board);
        P1FakeRadio fake;
        fake.start();
        std::unique_ptr<P1RadioConnection> connPtr = bringLinkUp(fake, hw);
        QVERIFY2(connPtr != nullptr, "link never reached Connected");
        P1RadioConnection& conn = *connPtr;

        keyThenDropAndReconnect(fake, conn);
        if (QTest::currentTestFailed()) {
            return;
        }

        const QList<QByteArray> log = fake.ep2CcReceived();
        const int keyed = firstKeyed(log);
        QVERIFY2(keyed < 0,
                 qPrintable(QStringLiteral("ep2 frame %1 of %2 after the reconnect "
                                           "carries C0 MOX = 1")
                                .arg(keyed).arg(log.size())));
        QVERIFY(!conn.moxForTest());

        // The HL2 can put an I2C frame in the first subframe; the other
        // boards have no I2C queue, so the first subframe is bank 0.
        if (hw != HPSDRHW::HermesLite) {
            QCOMPARE(c0(log.first(), 0), kBank0Address);
        }

        const QByteArray bank10 = firstBank(log, kBank10Address);
        QCOMPARE(bank10.size(), 5);
        QCOMPARE(static_cast<quint8>(bank10.at(1)), quint8(0));         // drive
        QVERIFY((static_cast<quint8>(bank10.at(3)) & 0x80) != 0);       // relay released

        const QByteArray bank11 = firstBank(log, kBank11Address);
        QCOMPARE(bank11.size(), 5);
        QCOMPARE(static_cast<quint8>(bank11.at(2)) & 0x40, 0);          // PureSignal off

        conn.disconnect();
        fake.stop();
    }

    void keyAfterLossRefusedUntilReleased()
    {
        P1FakeRadio fake;
        fake.start();
        std::unique_ptr<P1RadioConnection> connPtr = bringLinkUp(fake, HPSDRHW::Hermes);
        QVERIFY2(connPtr != nullptr, "link never reached Connected");
        P1RadioConnection& conn = *connPtr;

        keyThenDropAndReconnect(fake, conn);
        if (QTest::currentTestFailed()) {
            return;
        }

        // A key arriving after the loss, with no release since: refused.
        fake.clearEp2CcLog();
        conn.setMox(true);
        QVERIFY(!conn.moxForTest());
        QTRY_VERIFY_WITH_TIMEOUT(fake.ep2CcReceived().size() >= 20, 3000);
        QVERIFY(!anyKeyed(fake.ep2CcReceived()));

        // The operator releases and keys again: that key goes out.
        conn.setMox(false);
        conn.setMox(true);
        QVERIFY(conn.moxForTest());
        QTRY_VERIFY_WITH_TIMEOUT(anyKeyed(fake.ep2CcReceived()), 3000);

        conn.setMox(false);
        conn.disconnect();
        fake.stop();
    }

    void staleKeyBeforeConnectDoesNotReachTheRadio()
    {
        P1FakeRadio fake;
        fake.start();

        P1RadioConnection conn;
        conn.init();
        conn.setTxDrive(200);
        conn.setTrxRelay(true);
        conn.setMox(true);
        conn.connectToRadio(makeInfo(fake, HPSDRHW::Hermes));

        QTRY_VERIFY_WITH_TIMEOUT(fake.ep2CcReceived().size() >= 20, 3000);
        const QList<QByteArray> log = fake.ep2CcReceived();
        QCOMPARE(firstKeyed(log), -1);
        QCOMPARE(c0(log.first(), 0), kBank0Address);
        const QByteArray bank10 = firstBank(log, kBank10Address);
        QCOMPARE(bank10.size(), 5);
        QCOMPARE(static_cast<quint8>(bank10.at(1)), quint8(0));

        conn.disconnect();
        fake.stop();
    }
};

QTEST_MAIN(TestP1ReconnectUnkeyed)
#include "tst_p1_reconnect_unkeyed.moc"
