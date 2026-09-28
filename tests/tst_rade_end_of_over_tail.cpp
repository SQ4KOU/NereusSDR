// SPDX-License-Identifier: GPL-3.0-or-later
//
// no-port-check: NereusSDR-original test.
//
// tst_rade_end_of_over_tail: the transmit safety boundary around the RADE
// end-of-over tail (FreeDV's end-of-over frame after an operator's
// release).
//
// MoxController:
//   tailRunsBeforeTeardown        a release asks for a tail once; while it
//                                 runs MOX is off, the hardware stays keyed
//                                 (no txAboutToEnd, no drain, no
//                                 hardwareFlipped(false)); once it is done
//                                 the walk ends in Rx.
//   tailIsBounded                 kEndOfOverTailMaxMs is 1 s, and a tail
//                                 that never reports releases the radio at
//                                 its bound.
//   tailNeverKeys                 a key never asks for a tail; during one
//                                 nothing keys, and a repeated release does
//                                 not ask again.
//   blockedUnkeysSkipTheTail      TX inhibit, the PA trip and receive-only
//                                 unkey at once without asking.
//   blockDuringTailEndsIt         any of the three asserted during a tail
//                                 ends it at once.
//   newKeyEndsTheTail             a key during the tail ends it and keys;
//                                 a late "done" changes nothing.
//   abortEndsTheTailAtOnce        abortEndOfOverTail goes straight on.
//   noTailWalksAsBefore           a function that starts no tail leaves the
//                                 walk as it was (txAboutToEnd at once).
// RadioModel:
//   permittedOnlyForRadeRelease   radeEndOfOverTailPermitted: keyed in RADE
//                                 yes; USB, TUNE and after a stop no.
//   tailKeepsRadioKeyedThenReleases  during the tail the radio's MOX stays
//                                 on, the Core reports keyed and txEnding;
//                                 when it is done MOX goes off.
//   stopAllTxSkipsTheTail         a stop during the tail stops at once
//                                 (MOX off before it returns) and ends the
//                                 tail; txEnding clears.
//
// Modification history (NereusSDR):
//   2026-09-28  J.J. Boyd / KG4VCF  RADE end-of-over callsigns. AI tooling:
//                 Anthropic Claude Code.

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>

#include "core/AppSettings.h"
#include "core/MoxController.h"
#include "core/RadioConnection.h"
#include "core/TxChannel.h"
#include "core/WdspEngine.h"
#include "core/session/TransmitStateFacade.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"

using namespace NereusSDR;

namespace {

void pump(int passes = 8)
{
    for (int i = 0; i < passes; ++i) {
        QCoreApplication::processEvents();
    }
}

class MockConnection : public RadioConnection {
    Q_OBJECT
public:
    QStringList log;

    explicit MockConnection(QObject* parent = nullptr)
        : RadioConnection(parent)
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
    void setMox(bool on) override
    {
        log.append(on ? QStringLiteral("MOX on") : QStringLiteral("MOX off"));
    }
    void setTrxRelay(bool on) override
    {
        log.append(on ? QStringLiteral("relay on") : QStringLiteral("relay off"));
    }
    void setMicBoost(bool) override {}
    void setLineIn(bool) override {}
    void setMicTipRing(bool) override {}
    void setMicBias(bool) override {}
    void setLineInGain(int) override {}
    void setUserDigOut(quint8) override {}
    void setPuresignalRun(bool) override {}
    void setMicPTTDisabled(bool) override {}
    void setMicXlr(bool) override {}
};

// A bare controller on zero timers with a tail function that counts its
// asks and starts a tail when `start` is set.
struct Ctrl {
    MoxController mox;
    int asks{0};
    bool start{true};
    Ctrl()
    {
        mox.setTimerIntervals(0, 0, 0, 0, 0, 0);
        mox.setEndOfOverTail([this]() {
            ++asks;
            return start;
        });
    }
    void key()
    {
        mox.setMox(true);
        pump();
        QVERIFY(mox.isMox());
        QCOMPARE(mox.state(), MoxState::Tx);
    }
};

// A local model on 20 m with a mock connection and a TX channel wrapper
// with no WDSP channel behind it (as tst_radio_model_stop_all_tx), and the
// Core's transmit state bound to it.
struct Rig {
    RadioModel model;
    MockConnection conn;
    TxChannel tx{WdspEngine::kTxChannelId};
    TransmitState state;

    explicit Rig(DSPMode mode)
    {
        AppSettings::instance().clear();
        model.setCapsForTest(/*hasAlex=*/false);
        model.injectConnectionForTest(&conn);
        model.moxController()->setTimerIntervals(0, 0, 0, 0, 0, 0);
        model.setTuneOffSettleMsForTest(0);
        model.addSlice();
        if (SliceModel* slice = model.activeSlice()) {
            slice->setDspMode(mode);
            slice->setFrequency(14'236'000.0);
        }
        model.injectTxChannelForTest(&tx);
        model.wireTxChannelKeyingForTest();
        state.bind(&model);
    }
    ~Rig()
    {
        model.injectTxChannelForTest(nullptr);
        model.injectConnectionForTest(nullptr);
        AppSettings::instance().clear();
    }
};

}  // namespace

class TestRadeEndOfOverTail : public QObject {
    Q_OBJECT

private slots:
    void init() { AppSettings::instance().clear(); }
    void cleanup() { AppSettings::instance().clear(); }

    void tailRunsBeforeTeardown()
    {
        Ctrl c;
        c.key();
        QCOMPARE(c.asks, 0);

        QSignalSpy aboutToEnd(&c.mox, &MoxController::txAboutToEnd);
        QSignalSpy drain(&c.mox, &MoxController::txDrainRequested);
        QSignalSpy flipped(&c.mox, &MoxController::hardwareFlipped);
        QSignalSpy tail(&c.mox, &MoxController::endOfOverTailChanged);

        c.mox.setMox(false);
        QCOMPARE(c.asks, 1);
        QVERIFY(!c.mox.isMox());
        QVERIFY(c.mox.isEndOfOverTailActive());
        QCOMPARE(tail.count(), 1);
        QCOMPARE(tail.at(0).at(0).toBool(), true);

        pump();
        QCOMPARE(c.mox.state(), MoxState::TxToRxInFlight);
        QCOMPARE(aboutToEnd.count(), 0);
        QCOMPARE(drain.count(), 0);
        QCOMPARE(flipped.count(), 0);

        c.mox.onEndOfOverTailDone();
        QVERIFY(!c.mox.isEndOfOverTailActive());
        QCOMPARE(tail.count(), 2);
        QCOMPARE(tail.at(1).at(0).toBool(), false);
        QCOMPARE(aboutToEnd.count(), 1);
        QCOMPARE(drain.count(), 1);
        pump();
        QCOMPARE(c.mox.state(), MoxState::Rx);
        QCOMPARE(flipped.count(), 1);
        QCOMPARE(flipped.at(0).at(0).toBool(), false);
    }

    void tailIsBounded()
    {
        QCOMPARE(MoxController::kEndOfOverTailMaxMs, 1000);

        Ctrl c;
        c.mox.setEndOfOverTailMaxMsForTest(60);
        c.key();
        QSignalSpy flipped(&c.mox, &MoxController::hardwareFlipped);
        QElapsedTimer t;
        t.start();
        c.mox.setMox(false);
        QVERIFY(c.mox.isEndOfOverTailActive());
        QTRY_COMPARE_WITH_TIMEOUT(c.mox.state(), MoxState::Rx, 5000);
        QVERIFY2(t.elapsed() >= 50,
                 qPrintable(QStringLiteral("released after %1 ms").arg(t.elapsed())));
        QVERIFY(!c.mox.isEndOfOverTailActive());
        QCOMPARE(flipped.count(), 1);
        QCOMPARE(flipped.at(0).at(0).toBool(), false);
    }

    void tailNeverKeys()
    {
        Ctrl c;
        QSignalSpy aboutToBegin(&c.mox, &MoxController::txAboutToBegin);
        c.key();
        QCOMPARE(aboutToBegin.count(), 1);
        QCOMPARE(c.asks, 0);

        QSignalSpy flipped(&c.mox, &MoxController::hardwareFlipped);
        c.mox.setMox(false);
        c.mox.setMox(false);  // a repeated release is not a release
        pump();
        QCOMPARE(c.asks, 1);
        QCOMPARE(aboutToBegin.count(), 1);
        QCOMPARE(flipped.count(), 0);
        QVERIFY(!c.mox.isMox());
        c.mox.onEndOfOverTailDone();
        pump();
        QCOMPARE(aboutToBegin.count(), 1);
        QCOMPARE(c.mox.state(), MoxState::Rx);
    }

    void blockedUnkeysSkipTheTail()
    {
        {
            Ctrl c;
            c.key();
            c.mox.setTxInhibited(true);
            QCOMPARE(c.asks, 0);
            QVERIFY(!c.mox.isEndOfOverTailActive());
            pump();
            QCOMPARE(c.mox.state(), MoxState::Rx);
        }
        {
            Ctrl c;
            c.key();
            c.mox.setPaTripped(true);
            QCOMPARE(c.asks, 0);
            pump();
            QCOMPARE(c.mox.state(), MoxState::Rx);
        }
        {
            Ctrl c;
            c.key();
            c.mox.setRxOnly(true, QString());
            QCOMPARE(c.asks, 0);
            pump();
            QCOMPARE(c.mox.state(), MoxState::Rx);
        }
    }

    void blockDuringTailEndsIt()
    {
        for (int which = 0; which < 3; ++which) {
            Ctrl c;
            c.key();
            c.mox.setMox(false);
            QVERIFY(c.mox.isEndOfOverTailActive());
            QSignalSpy aboutToEnd(&c.mox, &MoxController::txAboutToEnd);
            if (which == 0) {
                c.mox.setTxInhibited(true);
            } else if (which == 1) {
                c.mox.setPaTripped(true);
            } else {
                c.mox.setRxOnly(true);
            }
            QVERIFY(!c.mox.isEndOfOverTailActive());
            QCOMPARE(aboutToEnd.count(), 1);
            pump();
            QCOMPARE(c.mox.state(), MoxState::Rx);
        }
    }

    void newKeyEndsTheTail()
    {
        Ctrl c;
        c.key();
        c.mox.setMox(false);
        QVERIFY(c.mox.isEndOfOverTailActive());

        QSignalSpy tail(&c.mox, &MoxController::endOfOverTailChanged);
        QSignalSpy aboutToEnd(&c.mox, &MoxController::txAboutToEnd);
        c.mox.setMox(true);
        QVERIFY(!c.mox.isEndOfOverTailActive());
        QCOMPARE(tail.count(), 1);
        QCOMPARE(tail.at(0).at(0).toBool(), false);
        pump();
        QVERIFY(c.mox.isMox());
        QCOMPARE(c.mox.state(), MoxState::Tx);

        c.mox.onEndOfOverTailDone();  // late: ignored
        pump();
        QCOMPARE(aboutToEnd.count(), 0);
        QVERIFY(c.mox.isMox());
        QCOMPARE(c.mox.state(), MoxState::Tx);
    }

    void abortEndsTheTailAtOnce()
    {
        Ctrl c;
        c.key();
        c.mox.setMox(false);
        QSignalSpy aboutToEnd(&c.mox, &MoxController::txAboutToEnd);
        c.mox.abortEndOfOverTail();
        QVERIFY(!c.mox.isEndOfOverTailActive());
        QCOMPARE(aboutToEnd.count(), 1);
        pump();
        QCOMPARE(c.mox.state(), MoxState::Rx);
        c.mox.abortEndOfOverTail();  // nothing to abort: no second walk
        QCOMPARE(aboutToEnd.count(), 1);
    }

    void noTailWalksAsBefore()
    {
        Ctrl c;
        c.start = false;
        c.key();
        QSignalSpy aboutToEnd(&c.mox, &MoxController::txAboutToEnd);
        QSignalSpy tail(&c.mox, &MoxController::endOfOverTailChanged);
        c.mox.setMox(false);
        QCOMPARE(c.asks, 1);
        QCOMPARE(aboutToEnd.count(), 1);
        QCOMPARE(tail.count(), 0);
        pump();
        QCOMPARE(c.mox.state(), MoxState::Rx);
    }

    void permittedOnlyForRadeRelease()
    {
        {
            Rig rig(DSPMode::USB);
            rig.model.moxController()->setMox(true);
            pump();
            QVERIFY(rig.model.mox());
            QVERIFY(!rig.model.radeEndOfOverTailPermitted());
        }
        {
            Rig rig(DSPMode::RADE_U);
            QVERIFY(!rig.model.radeEndOfOverTailPermitted());  // not keyed
            rig.model.moxController()->setMox(true);
            pump();
            QVERIFY(rig.model.mox());
            QVERIFY(rig.tx.isRfGateOpen());
            QVERIFY(rig.model.radeEndOfOverTailPermitted());
            rig.model.stopTransmitNow(QStringLiteral("test"));
            QVERIFY(!rig.model.radeEndOfOverTailPermitted());
        }
        {
            Rig rig(DSPMode::RADE_L);
            rig.model.setTune(true);
            pump();
            QVERIFY(rig.model.mox());
            QVERIFY(!rig.model.radeEndOfOverTailPermitted());
        }
    }

    void tailKeepsRadioKeyedThenReleases()
    {
        Rig rig(DSPMode::RADE_U);
        // Stand in for the RADE channel: a tail starts whenever RadioModel
        // would allow one.
        MoxController* mox = rig.model.moxController();
        mox->setEndOfOverTail([&rig]() { return rig.model.radeEndOfOverTailPermitted(); });
        QSignalSpy tailChanged(&rig.model, &RadioModel::endOfOverTailChanged);

        mox->setMox(true);
        pump();
        QVERIFY(rig.state.keyed());
        rig.conn.log.clear();

        mox->setMox(false);
        pump();
        QVERIFY(rig.model.endOfOverTailActive());
        QCOMPARE(tailChanged.count(), 1);
        QVERIFY(rig.state.keyed());
        QVERIFY(rig.state.txEnding());
        QVERIFY2(!rig.conn.log.contains(QStringLiteral("MOX off")),
                 "the radio unkeyed before the end-of-over frame went out");
        QVERIFY(rig.tx.isRfGateOpen());

        mox->onEndOfOverTailDone();
        QTRY_COMPARE_WITH_TIMEOUT(mox->state(), MoxState::Rx, 5000);
        pump();
        QVERIFY(!rig.model.endOfOverTailActive());
        QVERIFY(!rig.state.txEnding());
        QVERIFY(!rig.state.keyed());
        QVERIFY(rig.conn.log.contains(QStringLiteral("MOX off")));
        QVERIFY(!rig.conn.log.contains(QStringLiteral("MOX on")));
    }

    void stopAllTxSkipsTheTail()
    {
        Rig rig(DSPMode::RADE_U);
        MoxController* mox = rig.model.moxController();
        mox->setEndOfOverTail([&rig]() { return rig.model.radeEndOfOverTailPermitted(); });

        mox->setMox(true);
        pump();
        mox->setMox(false);
        pump();
        QVERIFY(rig.model.endOfOverTailActive());
        QVERIFY(rig.state.txEnding());
        rig.conn.log.clear();

        rig.model.stopAllTx(QStringLiteral("Time Out Timer"));
        // RF stops before stopAllTx returns, and the tail is over.
        QVERIFY(rig.conn.log.size() >= 2);
        QCOMPARE(rig.conn.log.at(0), QStringLiteral("MOX off"));
        QVERIFY(!rig.tx.isRfGateOpen());
        QVERIFY(!rig.model.endOfOverTailActive());
        QTRY_COMPARE_WITH_TIMEOUT(mox->state(), MoxState::Rx, 5000);
        pump();
        QVERIFY(!rig.state.txEnding());
        QVERIFY(!rig.conn.log.contains(QStringLiteral("MOX on")));
    }
};

QTEST_GUILESS_MAIN(TestRadeEndOfOverTail)
#include "tst_rade_end_of_over_tail.moc"
