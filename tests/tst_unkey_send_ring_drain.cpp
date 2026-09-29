// SPDX-License-Identifier: GPL-3.0-or-later
//
// no-port-check: NereusSDR-original test.
//
// tst_unkey_send_ring_drain: at the end of an over, the operator's release
// waits for the transmit I/Q send ring to drain before the hardware is
// released, for no longer than the ring's own length (JJ's ruling
// 2026-09-28, addendum G-05). Thetis's delays and their order are not
// changed: the wait sits after the TX channel's drain and before mox_delay.
//
// MoxController:
//   releaseWaitsUntilTheRingDrains   queued audio holds the hardware keyed
//                                    (no txaFlushed, no hardwareFlipped)
//                                    until the ring reports drained.
//   drainedRingReleasesAtOnce        an empty ring adds no wait.
//   stalledRingReleasesAtTheRingLength  a ring that never drains holds the
//                                    hardware for its own length, no more.
//   ceilingIsTheRingLength           the bound is the probe's length,
//                                    rounded up (P1 84 ms, P2 342 ms).
//   ringWaitFollowsTheTxDrain        the wait starts only after the TX
//                                    channel's drain has reported.
//   notPermittedReleasesAtOnce       a release the owner does not permit
//                                    (the Core's stops) does not wait.
//   blockedReleaseIsImmediate        TX inhibit, the PA trip, receive-only:
//                                    the unkey they force does not wait.
//   blockDuringWaitEndsIt            any of them during a wait ends it.
//   abortEndsTheWait                 abortSendRingWait goes straight on.
//   newKeyDuringWaitCancelsIt        a key during the wait ends it and the
//                                    hardware is never released.
//   noRingLengthMeansNoWait          a connection without a ring adds none.
// Connections:
//   p1RingLengthAndDrain             Protocol 1: 126 x 32 samples at 48 kHz
//                                    (84 ms); drained below one 63-sample
//                                    zone, which is all the sender takes.
//   p2RingLengthAndDrain             Protocol 2: 65536 pairs at 192 kHz
//                                    (341.3 ms); drained when empty.
// RadioModel:
//   modelReleaseWaitsForTheRing      the model's probe reads the connection.
//   stopAllTxIsImmediate             Stop All TX (the holder revoked, the
//                                    time-out, the SWR and amplifier stops)
//                                    never waits, before or during a wait.
//   disconnectIsImmediate            a disconnect never waits.
//
// Modification history (NereusSDR):
//   2026-09-29  J.J. Boyd / KG4VCF  Unkey waits for the send ring (G-05).
//                 AI tooling: Anthropic Claude Code.

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>

#include <atomic>
#include <cmath>
#include <vector>

#include "core/AppSettings.h"
#include "core/MoxController.h"
#include "core/P1RadioConnection.h"
#include "core/P2RadioConnection.h"
#include "core/RadioConnection.h"
#include "models/RadioModel.h"

using namespace NereusSDR;

namespace {

void pump(int passes = 8)
{
    for (int i = 0; i < passes; ++i) {
        QCoreApplication::processEvents();
    }
}

// Pumps events for about ms of wall time.
void pumpFor(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
}

// A controller on zero timers whose send ring the test controls.
struct Ctrl {
    MoxController mox;
    std::atomic<bool> drained{false};
    double lengthMs{60000.0};
    bool permitted{true};
    int drainedAsks{0};

    Ctrl()
    {
        mox.setTimerIntervals(0, 0, 0, 0, 0, 0);
        MoxController::SendRingDrain ring;
        ring.permitted = [this]() { return permitted; };
        ring.drained = [this]() {
            ++drainedAsks;
            return drained.load();
        };
        ring.lengthMs = [this]() { return lengthMs; };
        mox.setSendRingDrain(std::move(ring));
    }
    void key()
    {
        mox.setMox(true);
        pump();
        QVERIFY(mox.isMox());
        QCOMPARE(mox.state(), MoxState::Tx);
    }
};

// A connection whose send ring the test controls.
class RingConnection : public RadioConnection {
    Q_OBJECT
public:
    std::atomic<bool> drained{false};
    double lengthMs{60000.0};
    QStringList log;

    explicit RingConnection(QObject* parent = nullptr) : RadioConnection(parent)
    {
        setState(ConnectionState::Connected);
    }
    void init() override {}
    void connectToRadio(const NereusSDR::RadioInfo&) override {}
    void disconnect() override {}
    void setReceiverFrequency(int, quint64) override {}
    void setTxFrequency(quint64) override {}
    void setActiveReceiverCount(int) override {}
    void setSampleRate(int) override {}
    void setAttenuator(int) override {}
    void setPreamp(bool) override {}
    void setTxDrive(int) override {}
    void sendTxIq(const float*, int) override {}
    void setWatchdogEnabled(bool) override {}
    void setAntennaRouting(AntennaRouting) override {}
    void setMox(bool on) override { log << QStringLiteral("mox=%1").arg(on); }
    void setTrxRelay(bool on) override { log << QStringLiteral("relay=%1").arg(on); }
    void setMicBoost(bool) override {}
    void setLineIn(bool) override {}
    void setMicTipRing(bool) override {}
    void setMicBias(bool) override {}
    void setLineInGain(int) override {}
    void setUserDigOut(quint8) override {}
    void setPuresignalRun(bool) override {}
    void setMicPTTDisabled(bool) override {}
    void setMicXlr(bool) override {}
    bool txIqRingDrained() const override { return drained.load(); }
    double txIqRingLengthMs() const override { return lengthMs; }
};

struct ModelRig {
    RingConnection conn;
    RadioModel model;

    ModelRig()
    {
        AppSettings::instance().clear();
        model.setCapsForTest(/*hasAlex=*/false);
        model.injectConnectionForTest(&conn);
        model.moxController()->setTimerIntervals(0, 0, 0, 0, 0, 0);
        model.addSlice();
    }
    ~ModelRig()
    {
        model.injectConnectionForTest(nullptr);
        AppSettings::instance().clear();
    }
    MoxController* mox() { return model.moxController(); }
    void key()
    {
        mox()->setMox(true);
        pump();
        QCOMPARE(mox()->state(), MoxState::Tx);
    }
};

}  // namespace

class TestUnkeySendRingDrain : public QObject {
    Q_OBJECT

private slots:
    void init() { AppSettings::instance().clear(); }
    void cleanup() { AppSettings::instance().clear(); }

    // ── MoxController ────────────────────────────────────────────────────

    void releaseWaitsUntilTheRingDrains()
    {
        Ctrl c;
        c.key();
        QSignalSpy hardware(&c.mox, &MoxController::hardwareFlipped);
        QSignalSpy flushed(&c.mox, &MoxController::txaFlushed);
        QSignalSpy aboutToEnd(&c.mox, &MoxController::txAboutToEnd);
        QSignalSpy drainReq(&c.mox, &MoxController::txDrainRequested);

        c.mox.setMox(false);
        pumpFor(40);
        // The walk has begun (the TX channel drains) but the hardware stays
        // keyed while the ring holds audio.
        QCOMPARE(aboutToEnd.count(), 1);
        QCOMPARE(drainReq.count(), 1);
        QCOMPARE(flushed.count(), 0);
        QCOMPARE(hardware.count(), 0);
        QCOMPARE(c.mox.state(), MoxState::TxToRxInFlight);
        QVERIFY(c.mox.isSendRingWaitActive());
        QVERIFY(!c.mox.isMox());
        QVERIFY(c.drainedAsks > 1);   // it keeps looking

        c.drained = true;
        QTRY_COMPARE_WITH_TIMEOUT(hardware.count(), 1, 2000);
        QCOMPARE(hardware.at(0).at(0).toBool(), false);
        QCOMPARE(flushed.count(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(c.mox.state(), MoxState::Rx, 2000);
        QVERIFY(!c.mox.isSendRingWaitActive());
    }

    void drainedRingReleasesAtOnce()
    {
        Ctrl c;
        c.drained = true;
        c.key();
        QSignalSpy hardware(&c.mox, &MoxController::hardwareFlipped);
        c.mox.setMox(false);
        QVERIFY(!c.mox.isSendRingWaitActive());
        pump(2);   // keyUpDelay, then pttOutDelay, as without a ring
        QCOMPARE(hardware.count(), 1);
        QCOMPARE(c.mox.state(), MoxState::Rx);
    }

    void stalledRingReleasesAtTheRingLength()
    {
        Ctrl c;
        c.lengthMs = 80.0;
        c.key();
        QSignalSpy hardware(&c.mox, &MoxController::hardwareFlipped);
        QElapsedTimer t;
        t.start();
        c.mox.setMox(false);
        QVERIFY(c.mox.isSendRingWaitActive());
        QTRY_COMPARE_WITH_TIMEOUT(hardware.count(), 1, 5000);
        const qint64 held = t.elapsed();
        // Never shorter than the ring's length (the queued audio it can
        // hold). No upper bound but the QTRY timeout: a loaded machine
        // delivers timers late, which is not a finding about this wait.
        QVERIFY2(held >= 80, qPrintable(QStringLiteral("held %1 ms").arg(held)));
        QCOMPARE(hardware.at(0).at(0).toBool(), false);
        QTRY_COMPARE_WITH_TIMEOUT(c.mox.state(), MoxState::Rx, 2000);
        QVERIFY(!c.mox.isSendRingWaitActive());
    }

    void ceilingIsTheRingLength_data()
    {
        QTest::addColumn<double>("lengthMs");
        QTest::addColumn<int>("ceilingMs");
        QTest::newRow("p1") << (4032.0 * 1000.0 / 48000.0) << 84;
        QTest::newRow("p2") << (65536.0 * 1000.0 / 192000.0) << 342;
        QTest::newRow("short") << 12.5 << 13;
    }
    void ceilingIsTheRingLength()
    {
        QFETCH(double, lengthMs);
        QFETCH(int, ceilingMs);
        Ctrl c;
        c.lengthMs = lengthMs;
        c.key();
        c.mox.setMox(false);
        QVERIFY(c.mox.isSendRingWaitActive());
        QCOMPARE(c.mox.sendRingWaitCeilingMs(), ceilingMs);
        c.drained = true;
        QTRY_COMPARE_WITH_TIMEOUT(c.mox.state(), MoxState::Rx, 2000);
    }

    void ringWaitFollowsTheTxDrain()
    {
        Ctrl c;
        c.mox.setAwaitsTxDrain(true);
        c.mox.setTxDrainTimeoutMsForTest(60000);
        c.key();
        c.mox.setMox(false);
        pumpFor(20);
        // Waiting for the TX channel, not yet for the ring.
        QVERIFY(!c.mox.isSendRingWaitActive());
        QCOMPARE(c.drainedAsks, 0);
        c.mox.onTxDrained();
        QVERIFY(c.mox.isSendRingWaitActive());
        QCOMPARE(c.mox.state(), MoxState::TxToRxInFlight);
        c.drained = true;
        QTRY_COMPARE_WITH_TIMEOUT(c.mox.state(), MoxState::Rx, 2000);
    }

    void notPermittedReleasesAtOnce()
    {
        Ctrl c;
        c.permitted = false;
        c.key();
        QSignalSpy hardware(&c.mox, &MoxController::hardwareFlipped);
        c.mox.setMox(false);
        QVERIFY(!c.mox.isSendRingWaitActive());
        pump(2);
        QCOMPARE(hardware.count(), 1);
        QCOMPARE(c.mox.state(), MoxState::Rx);
    }

    void blockedReleaseIsImmediate_data()
    {
        QTest::addColumn<int>("which");
        QTest::newRow("tx inhibit") << 0;
        QTest::newRow("pa trip") << 1;
        QTest::newRow("receive only") << 2;
    }
    void blockedReleaseIsImmediate()
    {
        QFETCH(int, which);
        Ctrl c;
        c.key();
        QSignalSpy hardware(&c.mox, &MoxController::hardwareFlipped);
        if (which == 0) {
            c.mox.setTxInhibited(true);
        } else if (which == 1) {
            c.mox.setPaTripped(true);
        } else {
            c.mox.setRxOnly(true, QString());
        }
        QVERIFY(!c.mox.isMox());
        QVERIFY(!c.mox.isSendRingWaitActive());
        pump(2);
        QCOMPARE(hardware.count(), 1);
        QCOMPARE(c.mox.state(), MoxState::Rx);
    }

    void blockDuringWaitEndsIt_data() { blockedReleaseIsImmediate_data(); }
    void blockDuringWaitEndsIt()
    {
        QFETCH(int, which);
        Ctrl c;
        c.key();
        c.mox.setMox(false);
        QVERIFY(c.mox.isSendRingWaitActive());
        QSignalSpy hardware(&c.mox, &MoxController::hardwareFlipped);
        if (which == 0) {
            c.mox.setTxInhibited(true);
        } else if (which == 1) {
            c.mox.setPaTripped(true);
        } else {
            c.mox.setRxOnly(true, QString());
        }
        QVERIFY(!c.mox.isSendRingWaitActive());
        pump(2);
        QCOMPARE(hardware.count(), 1);
        QCOMPARE(c.mox.state(), MoxState::Rx);
    }

    void abortEndsTheWait()
    {
        Ctrl c;
        c.key();
        c.mox.setMox(false);
        QVERIFY(c.mox.isSendRingWaitActive());
        QSignalSpy hardware(&c.mox, &MoxController::hardwareFlipped);
        c.mox.abortSendRingWait();
        QVERIFY(!c.mox.isSendRingWaitActive());
        pump(2);
        QCOMPARE(hardware.count(), 1);
        QCOMPARE(c.mox.state(), MoxState::Rx);
        // A second abort, and one with no wait, change nothing.
        c.mox.abortSendRingWait();
        pump();
        QCOMPARE(hardware.count(), 1);
    }

    void newKeyDuringWaitCancelsIt()
    {
        Ctrl c;
        c.key();
        c.mox.setMox(false);
        QVERIFY(c.mox.isSendRingWaitActive());
        QSignalSpy hardware(&c.mox, &MoxController::hardwareFlipped);
        c.mox.setMox(true);
        QVERIFY(!c.mox.isSendRingWaitActive());
        c.drained = true;
        pumpFor(20);
        // Only the new key's flip; the old release never reached the hardware.
        QCOMPARE(hardware.count(), 1);
        QCOMPARE(hardware.at(0).at(0).toBool(), true);
        QCOMPARE(c.mox.state(), MoxState::Tx);
    }

    void noRingLengthMeansNoWait()
    {
        Ctrl c;
        c.lengthMs = -1.0;
        c.key();
        QSignalSpy hardware(&c.mox, &MoxController::hardwareFlipped);
        c.mox.setMox(false);
        QVERIFY(!c.mox.isSendRingWaitActive());
        pump(2);
        QCOMPARE(hardware.count(), 1);
    }

    // ── Connections ──────────────────────────────────────────────────────

    void p1RingLengthAndDrain()
    {
        P1RadioConnection conn;
        QCOMPARE(conn.txIqRingLengthMs(), 4032.0 * 1000.0 / 48000.0);
        QVERIFY(conn.txIqRingDrained());
        // Less than one 63-sample zone is never sent on its own.
        std::vector<float> iq(126 * 2, 0.1f);
        conn.sendTxIq(iq.data(), 62);
        QVERIFY(conn.txIqRingDrained());
        conn.sendTxIq(iq.data(), 1);
        QVERIFY(!conn.txIqRingDrained());
        conn.sendTxIq(iq.data(), 63);
        QVERIFY(!conn.txIqRingDrained());
        // One EP2 frame (two zones) takes 126 of the 126 queued.
        conn.sendTxIqAndCapture(nullptr, 0);
        QVERIFY(conn.txIqRingDrained());
    }

    void p2RingLengthAndDrain()
    {
        P2RadioConnection conn;
        QCOMPARE(conn.txIqRingLengthMs(), 65536.0 * 1000.0 / 192000.0);
        QVERIFY(conn.txIqRingDrained());
        std::vector<float> iq(2 * 2, 0.1f);
        conn.sendTxIq(iq.data(), 1);
        QVERIFY(!conn.txIqRingDrained());
        // A frame takes what there is and pads with silence.
        conn.txIqFrameForTest(nullptr, 0);
        QVERIFY(conn.txIqRingDrained());
    }

    // ── RadioModel ───────────────────────────────────────────────────────

    void modelReleaseWaitsForTheRing()
    {
        ModelRig rig;
        rig.key();
        rig.mox()->setMox(false);
        pumpFor(30);
        QVERIFY(rig.mox()->isSendRingWaitActive());
        QCOMPARE(rig.mox()->state(), MoxState::TxToRxInFlight);
        rig.conn.drained = true;
        QTRY_COMPARE_WITH_TIMEOUT(rig.mox()->state(), MoxState::Rx, 2000);
    }

    void stopAllTxIsImmediate()
    {
        {
            // A stop while keyed: its unkey does not wait.
            ModelRig rig;
            rig.key();
            rig.model.stopAllTx(QStringLiteral("Time Out Timer"));
            QVERIFY(!rig.mox()->isSendRingWaitActive());
            QTRY_COMPARE_WITH_TIMEOUT(rig.mox()->state(), MoxState::Rx, 2000);
        }
        {
            // A stop during a release's wait ends the wait.
            ModelRig rig;
            rig.key();
            rig.mox()->setMox(false);
            QVERIFY(rig.mox()->isSendRingWaitActive());
            rig.model.stopAllTx(QStringLiteral("Time Out Timer"));
            QVERIFY(!rig.mox()->isSendRingWaitActive());
            QVERIFY(rig.conn.log.contains(QStringLiteral("mox=0")));
            QTRY_COMPARE_WITH_TIMEOUT(rig.mox()->state(), MoxState::Rx, 2000);
        }
    }

    void disconnectIsImmediate()
    {
        ModelRig rig;
        rig.key();
        rig.model.disconnectFromRadio();
        QVERIFY(!rig.mox()->isSendRingWaitActive());
        QVERIFY(!rig.mox()->isMox());
    }
};

QTEST_MAIN(TestUnkeySendRingDrain)
#include "tst_unkey_send_ring_drain.moc"
