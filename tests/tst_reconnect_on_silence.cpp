// tests/tst_reconnect_on_silence.cpp
//
// Phase 3I Task 10 — reconnection state machine test for P1RadioConnection.
//
// Tests the §3.6 state machine:
//   Connected → (2s silence) → LinkLost → (5s × up to 3 retries) → Connected or LinkLost
//
// Phase 3Q-1 note: ConnectionState::LinkLost was removed from the 5-value enum
// (Disconnected/Probing/Connecting/Connected/LinkLost). A watchdog timeout is
// semantically "link lost" (was connected; frames stopped), so Error → LinkLost.
//
// Uses P1FakeRadio auto-streaming (Task 10 addition: goSilent() stops ep6 output
// and resume() re-enables it, simulating a transient vs permanent radio failure).

#include <QtTest/QtTest>
#include <QSignalSpy>
#include "core/P1RadioConnection.h"
#include "core/HpsdrModel.h"
#include "fakes/P1FakeRadio.h"

using namespace NereusSDR;
using NereusSDR::Test::P1FakeRadio;

class TestReconnectOnSilence : public QObject {
    Q_OBJECT
private:
    static RadioInfo makeInfo(const P1FakeRadio& fake) {
        RadioInfo info;
        info.address         = fake.localAddress();
        info.port            = fake.localPort();
        info.boardType       = HPSDRHW::HermesLite;
        info.protocol        = ProtocolVersion::Protocol1;
        info.firmwareVersion = 72;
        info.macAddress      = QStringLiteral("aa:bb:cc:11:22:33");
        return info;
    }

private slots:
    void silenceTriggersErrorThenRecoversOnResume() {
        P1FakeRadio fake;
        fake.start();

        P1RadioConnection conn;
        conn.init();
        conn.connectToRadio(makeInfo(fake));
        QTRY_VERIFY_WITH_TIMEOUT(
            conn.state() == ConnectionState::Connected, 3000);

        // Wait for the fake to see the metis-start and begin streaming.
        QTRY_VERIFY_WITH_TIMEOUT(fake.isRunning(), 3000);

        // Kick the fake into silence — it stops replying to ep2 and stops ep6.
        fake.goSilent();

        // Watchdog should trip after 2s → LinkLost.
        QTRY_VERIFY_WITH_TIMEOUT(
            conn.state() == ConnectionState::LinkLost, 5000);

        // Resume the fake before the reconnect window closes.
        // The reconnect timer fires after 5s — resume immediately so the next
        // attempt (metis-stop + metis-start) gets a response.
        fake.resume();

        // Within the reconnect cycle (5s × up to 3 = 15s max), should recover.
        QTRY_VERIFY_WITH_TIMEOUT(
            conn.state() == ConnectionState::Connected, 18000);

        conn.disconnect();
        fake.stop();
    }

    void boundedRetriesExhaustStayInLinkLost() {
        P1FakeRadio fake;
        fake.start();

        P1RadioConnection conn;
        conn.init();
        conn.connectToRadio(makeInfo(fake));
        QTRY_VERIFY_WITH_TIMEOUT(
            conn.state() == ConnectionState::Connected, 3000);

        // Wait for the fake to process metis-start.
        QTRY_VERIFY_WITH_TIMEOUT(fake.isRunning(), 3000);

        // Fake stops completely — no discovery reply, no nothing.
        fake.stop();

        // Timeline (all times relative to fake.stop()):
        //   ~2s:  watchdog trips → LinkLost (attempt 0)
        //   ~7s:  reconnect timeout → attempt 1, Connecting
        //   ~9s:  watchdog trips → LinkLost
        //   ~14s: reconnect timeout → attempt 2, Connecting
        //   ~16s: watchdog trips → LinkLost
        //   ~21s: reconnect timeout → attempt 3, Connecting
        //   ~23s: watchdog trips → LinkLost
        //   ~28s: reconnect timeout → retries exhausted, stays in LinkLost
        //
        // Wait 35s total — well past the 28s exhaust point — then verify LinkLost.
        QTest::qWait(35000);
        QCOMPARE(conn.state(), ConnectionState::LinkLost);

        // Wait another 7s to confirm no further state changes (reconnect timer
        // would fire at 33s from exhaust point if unbounded — rules it out).
        QTest::qWait(7000);
        QCOMPARE(conn.state(), ConnectionState::LinkLost);

        conn.disconnect();
    }
};

QTEST_MAIN(TestReconnectOnSilence)
#include "tst_reconnect_on_silence.moc"
