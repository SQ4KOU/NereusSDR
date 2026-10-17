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
//   4. The transmit cut (2026-10-15), Hermes and HL2: keyed, the frames
//      the EP2 sender thread sends after the cut are unkeyed while m_mox is
//      still set (the connection's thread is held, so the cut's queued
//      unkey has not run), every ep2 frame after the unkey is unkeyed, a key
//      is refused while it is set, and after clearTransmitCut a key goes
//      out. The setters are posted, as RadioModel posts them, so they hold
//      the frame lock.
//   5. The transmit cut on every composer (no socket): the Hermes and HL2
//      codecs, the HL2 I2C frame and the legacy compose path all compose
//      C0 MOX = 0 while cut with m_mox still set, and both ep2 TX I/Q zones
//      stay zero although the ring holds samples.
//
// Modification history (NereusSDR):
//   2026-10-15: transmitCutUnkeysTheWireAndRefusesAKey. J.J. Boyd (KG4VCF),
//               AI-assisted via Anthropic Claude Code.
//   2026-10-15: review fix wave: transmitCutUnkeysEveryComposer (HL2 I2C
//               frame, legacy compose, I/Q zones). J.J. Boyd (KG4VCF),
//               AI-assisted via Anthropic Claude Code.
//   2026-10-15: re-review: corrected the networkproto1.c cite for the
//               MOX bit line. J.J. Boyd (KG4VCF), AI-assisted via
//               Anthropic Claude Code.
//   2026-10-15: merge with the EP2 sender thread: the wire test proves the
//               sender thread's frames are unkeyed while the cut's unkey
//               is still queued, on Hermes and HL2, and posts its setters
//               so they hold the frame lock. J.J. Boyd (KG4VCF),
//               AI-assisted via Anthropic Claude Code.

#include <QtTest/QtTest>
#include <QElapsedTimer>
#include <QThread>

#include <functional>
#include <memory>
#include <vector>

#include "core/P1RadioConnection.h"
#include "core/HpsdrModel.h"
#include "core/IoBoardHl2.h"
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

// Drops the link and brings it back. `onLinkLost` runs on the LinkLost
// transition, after the fake has been told to answer again and before the
// reconnect's first attempt. On return the fake's log holds only what was
// sent from the reconnect's metis-stop onward.
void dropAndReconnect(P1FakeRadio& fake, P1RadioConnection& conn,
                      std::function<void()> onLinkLost = {})
{
    // The radio comes back the moment the loss is declared (see
    // tst_reconnect_on_silence for why this rides the transition). The
    // fake clears its log at the reconnect's metis-stop, so frames sent
    // before the loss and still in flight are not read as the reconnect's.
    bool sawLinkLost = false;
    const QMetaObject::Connection watch = QObject::connect(
        &conn, &P1RadioConnection::connectionStateChanged, &conn,
        [&fake, &sawLinkLost, &onLinkLost](ConnectionState s) {
            if (s == ConnectionState::LinkLost && !sawLinkLost) {
                sawLinkLost = true;
                fake.resume();
                if (onLinkLost) {
                    onLinkLost();
                }
            }
        });
    fake.goSilent();
    QTRY_VERIFY_WITH_TIMEOUT(sawLinkLost, 3000);
    QObject::disconnect(watch);
    QTRY_VERIFY_WITH_TIMEOUT(conn.state() == ConnectionState::Connected, 3000);
    QVERIFY(fake.metisStopCount() >= 1);
    // Past the priming bursts and well into the round robin.
    QTRY_VERIFY_WITH_TIMEOUT(fake.ep2CcReceived().size() >= 40, 3000);
}

// The silence deadline while the transmit cut test holds the connection's
// thread; far longer than the hold, so the hold is never a lost link.
constexpr int kCutHoldWatchdogMs = 3000;
// How long the cut test may hold the connection's thread waiting for frames.
constexpr int kCutHoldBoundMs = 1500;

// Runs `work` as a queued call on the connection, as RadioModel posts its
// setters, so it holds the frame lock (P1RadioConnection::event) and never
// changes a frame input while the EP2 sender builds a frame. Returns once
// it has run.
template <typename F>
void postAndWait(P1RadioConnection& conn, F work)
{
    bool ran = false;
    QMetaObject::invokeMethod(&conn, [&ran, work] {
        work();
        ran = true;
    }, Qt::QueuedConnection);
    QTRY_VERIFY_WITH_TIMEOUT(ran, 3000);
}

// Waits, without running this thread's event loop, until the fake has
// logged at least `count` ep2 frames. The fake and the EP2 sender run on
// threads of their own, so frames keep coming while queued calls on this
// thread wait. False if kCutHoldBoundMs passes first.
bool waitForFramesHoldingThisThread(const P1FakeRadio& fake, qsizetype count)
{
    QElapsedTimer held;
    held.start();
    while (fake.ep2CcReceived().size() < count) {
        if (held.elapsed() > kCutHoldBoundMs) {
            return false;
        }
        QThread::msleep(1);
    }
    return true;
}

// Keys the link, drops it, and brings it back (dropAndReconnect).
void keyThenDropAndReconnect(P1FakeRadio& fake, P1RadioConnection& conn,
                             std::function<void()> onLinkLost = {})
{
    QTRY_VERIFY_WITH_TIMEOUT(fake.isRunning(), 3000);

    conn.setTxDrive(200);
    conn.setTrxRelay(true);
    conn.setPuresignalRun(true);
    conn.setMox(true);

    // The key reached the wire before the loss, so the test can tell an
    // unkeyed reconnect from a key that never went out.
    QTRY_VERIFY_WITH_TIMEOUT(anyKeyed(fake.ep2CcReceived()), 3000);

    dropAndReconnect(fake, conn, std::move(onLinkLost));
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

    // Fix round 1 (replaces keyAfterLossRefusedUntilReleased, which keyed
    // after Connected with no release, a state the model never sends: its
    // stopAllTx on LinkLost releases, and its key block refuses every key
    // until the link is back). A key that reaches the connection during
    // the outage of a link lost keyed is refused; it does not resume when
    // the link is back, and the next key after the reconnect goes out.
    void keyDuringOutageDoesNotResume()
    {
        P1FakeRadio fake;
        fake.start();
        std::unique_ptr<P1RadioConnection> connPtr = bringLinkUp(fake, HPSDRHW::Hermes);
        QVERIFY2(connPtr != nullptr, "link never reached Connected");
        P1RadioConnection& conn = *connPtr;

        bool latchedAtLoss = false;
        bool refusedInOutage = false;
        keyThenDropAndReconnect(fake, conn, [&conn, &latchedAtLoss, &refusedInOutage]() {
            latchedAtLoss = conn.linkLossLatchedForTest();
            conn.setMox(true);
            refusedInOutage = !conn.moxForTest();
        });
        if (QTest::currentTestFailed()) {
            return;
        }
        QVERIFY(latchedAtLoss);
        QVERIFY(refusedInOutage);

        // Back unkeyed: no keyed frame, and the latch has done its work.
        QCOMPARE(firstKeyed(fake.ep2CcReceived()), -1);
        QVERIFY(!conn.moxForTest());
        QVERIFY(!conn.linkLossLatchedForTest());

        // The operator's next key goes out.
        conn.setMox(true);
        QVERIFY(conn.moxForTest());
        QTRY_VERIFY_WITH_TIMEOUT(anyKeyed(fake.ep2CcReceived()), 3000);

        conn.setMox(false);
        conn.disconnect();
        fake.stop();
    }

    // Fix round 1, I1: a link lost while unkeyed latches nothing, so the
    // first key after the reconnect reaches the radio (C0 MOX = 1 on ep2).
    void unkeyedLossThenReconnectKeys()
    {
        P1FakeRadio fake;
        fake.start();
        std::unique_ptr<P1RadioConnection> connPtr = bringLinkUp(fake, HPSDRHW::Hermes);
        QVERIFY2(connPtr != nullptr, "link never reached Connected");
        P1RadioConnection& conn = *connPtr;
        QTRY_VERIFY_WITH_TIMEOUT(fake.isRunning(), 3000);

        bool latchedAtLoss = true;
        dropAndReconnect(fake, conn, [&conn, &latchedAtLoss]() {
            latchedAtLoss = conn.linkLossLatchedForTest();
        });
        if (QTest::currentTestFailed()) {
            return;
        }
        QVERIFY(!latchedAtLoss);
        QCOMPARE(firstKeyed(fake.ep2CcReceived()), -1);

        conn.setMox(true);
        QVERIFY(conn.moxForTest());
        QTRY_VERIFY_WITH_TIMEOUT(anyKeyed(fake.ep2CcReceived()), 3000);

        conn.setMox(false);
        conn.disconnect();
        fake.stop();
    }

    void transmitCutUnkeysTheWireAndRefusesAKey_data()
    {
        QTest::addColumn<int>("board");
        QTest::newRow("Hermes") << int(HPSDRHW::Hermes);
        QTest::newRow("HermesLite") << int(HPSDRHW::HermesLite);
    }

    void transmitCutUnkeysTheWireAndRefusesAKey()
    {
        QFETCH(int, board);
        P1FakeRadio fake;
        fake.start();
        std::unique_ptr<P1RadioConnection> connPtr =
            bringLinkUp(fake, static_cast<HPSDRHW>(board));
        QVERIFY2(connPtr != nullptr, "link never reached Connected");
        P1RadioConnection& conn = *connPtr;
        QTRY_VERIFY_WITH_TIMEOUT(fake.isRunning(), 3000);
        // The test holds this thread below while the EP2 sender keeps
        // sending; a short silence deadline would read that as a lost link.
        conn.setReconnectTimingForTest(kCutHoldWatchdogMs, kTestReconnectIntervalMs);

        postAndWait(conn, [&conn] { conn.setMox(true); });
        QTRY_VERIFY_WITH_TIMEOUT(anyKeyed(fake.ep2CcReceived()), 3000);

        // The cut, from this thread, which is the connection's. Its unkey is
        // queued here and cannot run until this thread returns to its event
        // loop, so m_mox stays set while the EP2 sender thread builds and
        // sends frames. Those frames must already be unkeyed: the sender
        // reads the cut itself.
        conn.cutTransmit();
        QVERIFY(conn.transmitCut());
        // Frames built before the cut may still be on their way.
        QVERIFY2(waitForFramesHoldingThisThread(fake, fake.ep2CcReceived().size() + 3),
                 "the EP2 sender sent nothing after the cut");
        const int fromFrame = int(fake.ep2CcReceived().size());
        QVERIFY2(waitForFramesHoldingThisThread(fake, fromFrame + 20),
                 "the EP2 sender sent nothing after the cut");
        QVERIFY2(conn.moxForTest(), "the cut's unkey ran before the check");
        const QList<QByteArray> held = fake.ep2CcReceived().mid(fromFrame);
        const int keyedHeld = firstKeyed(held);
        QVERIFY2(keyedHeld < 0,
                 qPrintable(QStringLiteral("ep2 frame %1 of %2 sent after the cut carries "
                                           "C0 MOX = 1").arg(keyedHeld).arg(held.size())));

        // The unkey runs once this thread is back in its event loop, and
        // every frame after it is unkeyed.
        QTRY_VERIFY_WITH_TIMEOUT(!conn.moxForTest(), 3000);
        fake.clearEp2CcLog();
        QTRY_VERIFY_WITH_TIMEOUT(fake.ep2CcReceived().size() >= 20, 3000);
        QCOMPARE(firstKeyed(fake.ep2CcReceived()), -1);

        // Refused while cut.
        postAndWait(conn, [&conn] { conn.setMox(true); });
        QVERIFY(!conn.moxForTest());
        fake.clearEp2CcLog();
        QTRY_VERIFY_WITH_TIMEOUT(fake.ep2CcReceived().size() >= 20, 3000);
        QCOMPARE(firstKeyed(fake.ep2CcReceived()), -1);

        // Cleared: the next key goes out.
        conn.clearTransmitCut();
        QVERIFY(!conn.transmitCut());
        postAndWait(conn, [&conn] { conn.setMox(true); });
        QVERIFY(conn.moxForTest());
        QTRY_VERIFY_WITH_TIMEOUT(anyKeyed(fake.ep2CcReceived()), 3000);

        postAndWait(conn, [&conn] { conn.setMox(false); });
        conn.disconnect();
        fake.stop();
    }

    void transmitCutUnkeysEveryComposer_data()
    {
        QTest::addColumn<int>("board");
        QTest::addColumn<bool>("legacy");
        QTest::addColumn<bool>("i2c");
        QTest::newRow("hermes codec") << int(HPSDRHW::Hermes) << false << false;
        QTest::newRow("hl2 codec") << int(HPSDRHW::HermesLite) << false << false;
        QTest::newRow("hl2 i2c frame") << int(HPSDRHW::HermesLite) << false << true;
        QTest::newRow("legacy compose") << int(HPSDRHW::Hermes) << true << false;
    }

    void transmitCutUnkeysEveryComposer()
    {
        QFETCH(int, board);
        QFETCH(bool, legacy);
        QFETCH(bool, i2c);

        // The legacy compose path is chosen when the codec is selected
        // (selectCodec, from setBoardForTest).
        if (legacy) {
            qputenv("NEREUS_USE_LEGACY_P1_CODEC", "1");
        }
        IoBoardHl2 io;   // outlives the connection that points at it
        P1RadioConnection conn;
        conn.setBoardForTest(static_cast<HPSDRHW>(board));
        qunsetenv("NEREUS_USE_LEGACY_P1_CODEC");
        conn.setIoBoard(&io);

        // One I2C write queued before each compose: the I2C frame replaces
        // the bank's payload. Its C0 is the MOX bit with the chip select:
        //   From mi0bot networkproto1.c:896 [@c26a8a4]: C0 = (unsigned char)XmitBit;
        //   From mi0bot networkproto1.c:914 [@c26a8a4]: C0 |= (0x3c << 1) | (prn->i2c.ctrl_request << 7);    //I2C1 0x3c
        const auto queueI2c = [&io, i2c]() {
            if (!i2c) {
                return;
            }
            IoBoardHl2::I2cTxn txn;
            txn.bus = 0;
            txn.address = 0x1D;
            txn.control = 0x05;
            txn.writeData = 0x42;
            txn.isRead = false;
            txn.needsResponse = false;
            io.enqueueI2c(txn);
        };
        const auto c0Of = [&conn, &queueI2c](int bank) {
            queueI2c();
            quint8 out[5] = {};
            conn.composeCcForBankForTest(bank, out);
            return out[0];
        };

        // Keyed: bank 0 and bank 1 (or the I2C frame) carry MOX.
        conn.setMox(true);
        QVERIFY(conn.moxForTest());
        if (i2c) {
            QCOMPARE(int(c0Of(0)), 0x79);
        }
        // Bank 0 and bank 10 carry MOX on every composer; bank 1 (the TX
        // frequency) carries it on the codecs only (the legacy path's
        // frequency banks never set it, composeCcBankTxFreq).
        QCOMPARE(int(c0Of(0) & 0x01), 1);
        QCOMPARE(int(c0Of(10) & 0x01), 1);
        if (!legacy) {
            QCOMPARE(int(c0Of(1) & 0x01), 1);
        }

        // TX I/Q queued (I 0.5, Q 0.25) and drained past the key's zero
        // cushion until a zone carries it: the zone check below can fail.
        std::vector<float> iq(2 * 2000);
        for (std::size_t i = 0; i < iq.size(); i += 2) {
            iq[i] = 0.5f;
            iq[i + 1] = 0.25f;
        }
        const auto iqZonesZero = [](const QByteArray& frame) {
            for (const int zone : {16, 528}) {
                for (int slot = 0; slot < 63; ++slot) {
                    for (int b = 4; b < 8; ++b) {
                        if (frame.at(zone + slot * 8 + b) != 0) {
                            return false;
                        }
                    }
                }
            }
            return true;
        };
        bool carried = iqZonesZero(conn.sendTxIqAndCapture(iq.data(), 2000)) == false;
        for (int i = 0; i < 12 && !carried; ++i) {
            carried = !iqZonesZero(conn.sendTxIqAndCapture(nullptr, 0));
        }
        QVERIFY2(carried, "no TX I/Q reached a zone before the cut");
        QVERIFY(conn.txIqBufferedSamplesForTest() >= 126);

        // Cut: the unkey is queued, m_mox still set, yet every composer
        // reads unkeyed and the zones stay zero with samples in the ring.
        conn.cutTransmit();
        QVERIFY(conn.transmitCut());
        QVERIFY(conn.moxForTest());
        if (i2c) {
            QCOMPARE(int(c0Of(0)), 0x78);
        }
        QCOMPARE(int(c0Of(0) & 0x01), 0);
        QCOMPARE(int(c0Of(10) & 0x01), 0);
        QCOMPARE(int(c0Of(1) & 0x01), 0);
        QVERIFY(conn.txIqBufferedSamplesForTest() >= 126);
        QVERIFY(iqZonesZero(conn.sendTxIqAndCapture(iq.data(), 126)));
        QVERIFY(iqZonesZero(conn.sendTxIqAndCapture(nullptr, 0)));

        // The queued unkey runs; the cut stays until it is cleared.
        QTRY_VERIFY_WITH_TIMEOUT(!conn.moxForTest(), 2000);
        QVERIFY(conn.transmitCut());
        conn.clearTransmitCut();
        conn.setMox(true);
        QCOMPARE(int(c0Of(0) & 0x01), 1);
        conn.setMox(false);
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
