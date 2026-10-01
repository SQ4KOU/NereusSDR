// no-port-check: NereusSDR-original. Thetis's loss-of-sync power-off
// (console.cs) is cited in RadioModel.cpp beside the code; this test
// translates no C#.
//
// TX safety (whole-branch review 2026-09-30): a lost radio link ends every
// transmission in the model, so the MOX button and the windows read unkeyed
// and nothing is left keyed for the connection's reconnect to resume.
//
//   1. MOX keyed: LinkLost leaves MOX, manual MOX, TUNE and two-tone off,
//      MOX and the relay go off to the connection, transmitStopped fires
//      once, and nothing keys the connection again.
//   2. TUNE keyed: the same.
//   3. Nothing keyed: LinkLost emits no stop.
//   4. The link back (Connected) does not key anything.

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QSignalSpy>

#include "core/AppSettings.h"
#include "core/MoxController.h"
#include "core/RadioConnection.h"
#include "core/TwoToneController.h"
#include "core/TxChannel.h"
#include "core/WdspEngine.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"

using namespace NereusSDR;

namespace {

// Records MOX and relay writes in order.
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
    void setStateForTest(ConnectionState s) { setState(s); }
};

void pump(int passes = 8)
{
    for (int i = 0; i < passes; ++i) {
        QCoreApplication::processEvents();
    }
}

// A local model with a connected slice on 20 m USB, a mock connection, a TX
// channel wrapper with no WDSP channel behind it, two-tone ready to run, and
// MoxController's walk driven by processEvents.
struct Rig {
    RadioModel model;
    MockConnection conn;
    TxChannel tx{WdspEngine::kTxChannelId};

    Rig()
    {
        AppSettings::instance().clear();
        model.setCapsForTest(/*hasAlex=*/false);
        model.injectConnectionForTest(&conn);
        model.moxController()->setTimerIntervals(0, 0, 0, 0, 0, 0);
        model.setTuneOffSettleMsForTest(0);
        model.addSlice();
        if (SliceModel* slice = model.activeSlice()) {
            slice->setDspMode(DSPMode::USB);
            slice->setFrequency(14'200'000.0);
        }
        model.injectTxChannelForTest(&tx);
        TwoToneController* twoTone = model.twoToneController();
        twoTone->setTxChannel(&tx);
        twoTone->setPowerOn(true);
        twoTone->setSettleDelaysMs(0, 0);
    }
    ~Rig()
    {
        model.twoToneController()->setTxChannel(nullptr);
        model.injectTxChannelForTest(nullptr);
        model.injectConnectionForTest(nullptr);
        AppSettings::instance().clear();
    }

    bool allOff()
    {
        return !model.mox()
            && !model.moxController()->isManualMox()
            && !model.isTune()
            && !model.twoToneController()->isActive()
            && !model.twoToneController()->isActivationInFlight();
    }
};

} // namespace

class TestRadioModelLinkLostUnkey : public QObject {
    Q_OBJECT

    static void checkLinkLossUnkeys(Rig& rig)
    {
        QSignalSpy stopped(&rig.model, &RadioModel::transmitStopped);
        rig.conn.log.clear();

        rig.conn.setStateForTest(ConnectionState::LinkLost);
        rig.model.onConnectionStateChangedForTest(ConnectionState::LinkLost);

        QVERIFY(rig.conn.log.size() >= 2);
        QCOMPARE(rig.conn.log.at(0), QStringLiteral("MOX off"));
        QCOMPARE(rig.conn.log.at(1), QStringLiteral("relay off"));
        QTRY_VERIFY_WITH_TIMEOUT(rig.allOff(), 5000);
        pump();
        QVERIFY(rig.allOff());
        QCOMPARE(rig.model.moxController()->state(), MoxState::Rx);
        QCOMPARE(stopped.count(), 1);
        QVERIFY(!stopped.at(0).at(0).toString().isEmpty());

        // The link comes back: still unkeyed, and nothing keyed the radio.
        rig.conn.setStateForTest(ConnectionState::Connected);
        rig.model.onConnectionStateChangedForTest(ConnectionState::Connected);
        pump();
        QVERIFY(rig.allOff());
        QVERIFY2(!rig.conn.log.contains(QStringLiteral("MOX on")),
                 "nothing keyed the radio again after the link loss");
    }

private slots:
    void init() { AppSettings::instance().clear(); }
    void cleanup() { AppSettings::instance().clear(); }

    void linkLossUnkeysMox()
    {
        Rig rig;
        rig.model.moxController()->setMox(true);
        pump();
        QVERIFY(rig.model.mox());
        checkLinkLossUnkeys(rig);
    }

    void linkLossUnkeysTune()
    {
        Rig rig;
        rig.model.setTune(true);
        pump();
        QVERIFY(rig.model.isTune());
        QVERIFY(rig.model.mox());
        checkLinkLossUnkeys(rig);
    }

    void linkLossUnkeyedEmitsNoStop()
    {
        Rig rig;
        QSignalSpy stopped(&rig.model, &RadioModel::transmitStopped);
        rig.conn.log.clear();
        rig.conn.setStateForTest(ConnectionState::LinkLost);
        rig.model.onConnectionStateChangedForTest(ConnectionState::LinkLost);
        pump();
        QCOMPARE(stopped.count(), 0);
        QVERIFY(!rig.conn.log.contains(QStringLiteral("MOX on")));
    }
};

QTEST_MAIN(TestRadioModelLinkLostUnkey)
#include "tst_radio_model_link_lost_unkey.moc"
