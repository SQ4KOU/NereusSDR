// no-port-check: NereusSDR-original.
// =================================================================
// tests/tst_amp_changeover_gate.cpp  (NereusSDR)
// =================================================================
//
// iPhone app plan Task 77 fix round 2 (R-IOS-02, R-IOS-03, R-IOS-13): the
// Power Genius is never switched around a key, and a key never puts RF
// into it while it switches.
//
//   1. From any operate=0 or operate=1 written to the amplifier until its
//      status reports the commanded state, a key's RF waits at the RF-flow
//      gate (a MOX click, VOX, TUNE): the TX channel's RF gate stays shut
//      until the injected state edge, then opens.
//   2. Still waiting 1.5 s after the command, the key is stopped with
//      plain words, its RF never started.
//   3. With no Power Genius connected (or its link gone), nothing changes.
//   4. The Core's own Tuner Genius autotune: refused on the air (a TUNE
//      click and a tuner's hardware TUNE alike), ended without keying when
//      MOX comes on during its standby wait; the amplifier's restore is
//      sent only with nothing keyed, no PTT down and nothing unconfirmed,
//      and dropped when the amplifier changes state on its own.
//
// A local RadioModel with a mock radio, a TX channel wired as the connect
// path wires it (no WDSP channel: the RF gate is the observable), and the
// amplifier and tuner connections fed status lines.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-26: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), iPhone app plan Task 77 fix round 2, with
//               AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QSignalSpy>

#include <atomic>

#include "core/AppSettings.h"
#include "core/MoxController.h"
#include "core/PgxlConnection.h"
#include "core/RadioConnection.h"
#include "core/TgxlConnection.h"
#include "core/TxChannel.h"
#include "core/WdspEngine.h"
#include "core/safety/TxRefusal.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"

using namespace NereusSDR;

namespace {

// The amplifier and tuner connections here have no socket: their writes
// are refused by Qt ("device not open"), which is noise, not a finding.
QtMessageHandler g_previousHandler = nullptr;
void quietUnopenedSockets(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    if (msg.startsWith(QLatin1String("QIODevice::write")) && msg.contains(QLatin1String("device not open"))) {
        return;
    }
    if (g_previousHandler != nullptr) {
        g_previousHandler(type, context, msg);
    }
}

const QString kAmpNotSwitched = QStringLiteral("The amplifier did not finish switching. Try again.");

class MockConnection : public RadioConnection {
    Q_OBJECT
public:
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
    void setMox(bool) override {}
    void setTrxRelay(bool) override {}
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

struct Rig {
    RadioModel model;
    MockConnection conn;
    TxChannel tx{WdspEngine::kTxChannelId};
    std::atomic<int> rfOpens{0};
    // Every operate command the amplifier was sent: whether RF could flow
    // (MOX on or walking) and whether the test held a PTT down then.
    struct Sent {
        QString command;
        bool rf{false};
        bool pttDown{false};
    };
    QList<Sent> sent;
    bool pttDown{false};

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
        model.wireTxChannelKeyingForTest();
        tx.setRfGateObserverForTest([this](bool open) {
            if (open) {
                ++rfOpens;
            }
        });
        MoxController* mox = model.moxController();
        QObject::connect(model.pgxlConnection(), &PgxlConnection::testFrameWrittenForTesting,
                         [this, mox](const QString& frame) {
                             const int bar = frame.indexOf(QLatin1Char('|'));
                             const QString command = frame.mid(bar + 1);
                             if (command.startsWith(QLatin1String("operate="))) {
                                 sent.append({command,
                                              mox->isMox() || mox->state() != MoxState::Rx,
                                              pttDown});
                             }
                         });
    }
    ~Rig()
    {
        tx.setRfGateObserverForTest({});
        model.injectTxChannelForTest(nullptr);
        model.injectConnectionForTest(nullptr);
        AppSettings::instance().clear();
    }

    PgxlConnection* amp() { return model.pgxlConnection(); }

    // The Power Genius on the air-side of this computer, operating.
    void connectAmp(const QString& state = QStringLiteral("OPERATE"))
    {
        amp()->injectLineForTesting(QStringLiteral("V3.8.9"));
        amp()->injectLineForTesting(QStringLiteral("R1|0|state=%1").arg(state));
    }
    void ampReports(const QString& state)
    {
        amp()->injectLineForTesting(QStringLiteral("S0|status state=%1").arg(state));
    }
    void connectTuner()
    {
        model.tgxlConnection()->injectLineForTesting(QStringLiteral("V1.2.17"));
    }

    int sentCount(const QString& command) const
    {
        int n = 0;
        for (const Sent& s : sent) {
            if (s.command == command) {
                ++n;
            }
        }
        return n;
    }

    bool keyed() { return model.moxController()->isMox(); }
    bool atRx() { return model.moxController()->state() == MoxState::Rx && !keyed(); }
};

enum class Key { Mox, Vox, Tune };

void keyWith(Rig& rig, Key key)
{
    MoxController* mox = rig.model.moxController();
    switch (key) {
    case Key::Mox:
        mox->setMox(true);
        break;
    case Key::Vox:
        rig.model.transmitModel().setVoxEnabled(true);
        rig.pttDown = true;
        mox->onVoxActive(true);
        break;
    case Key::Tune:
        rig.model.setTune(true);
        break;
    }
}

void unkeyWith(Rig& rig, Key key)
{
    MoxController* mox = rig.model.moxController();
    switch (key) {
    case Key::Mox:
        mox->setMox(false);
        break;
    case Key::Vox:
        rig.pttDown = false;
        mox->onVoxActive(false);
        rig.model.transmitModel().setVoxEnabled(false);
        break;
    case Key::Tune:
        rig.model.setTune(false);
        break;
    }
}

const char* keyName(Key key)
{
    switch (key) {
    case Key::Mox: return "MOX";
    case Key::Vox: return "VOX";
    case Key::Tune: return "TUNE";
    }
    return "?";
}

} // namespace

class TestAmpChangeoverGate : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        g_previousHandler = qInstallMessageHandler(quietUnopenedSockets);
        QLoggingCategory::setFilterRules(QStringLiteral("nereus.pgxl.debug=false\n"
                                                        "nereus.tgxl.debug=false"));
    }
    void cleanupTestCase() { qInstallMessageHandler(g_previousHandler); }
    void init() { AppSettings::instance().clear(); }
    void cleanup() { AppSettings::instance().clear(); }

    // 1. Every key source holds its RF while the amplifier switches, and
    //    not a moment after the amplifier reports the commanded state.
    void aKeysRfWaitsForTheAmplifierToReportTheCommandedState()
    {
        for (const Key key : {Key::Mox, Key::Vox, Key::Tune}) {
            Rig rig;
            rig.connectAmp();
            QVERIFY(rig.amp()->isConnected());
            QVERIFY(rig.model.ampOperate());
            // A command from anyone (here the connection itself, as the
            // operator's own STANDBY sends it).
            rig.amp()->sendCommand(QStringLiteral("operate=0"));
            keyWith(rig, key);
            QTRY_VERIFY2(rig.keyed(), keyName(key));
            QTest::qWait(250);
            QVERIFY2(rig.keyed(), keyName(key));
            QVERIFY2(rig.rfOpens.load() == 0, keyName(key));
            QVERIFY2(!rig.tx.isRfGateOpen(), keyName(key));
            // The amplifier's standby arrives: the RF starts now.
            rig.ampReports(QStringLiteral("STANDBY"));
            QTRY_VERIFY2(rig.tx.isRfGateOpen(), keyName(key));
            QVERIFY(rig.keyed());
            unkeyWith(rig, key);
            QTRY_VERIFY2(rig.atRx(), keyName(key));
        }
    }

    // 1b. The same for operate=1 (the restore direction): RF waits for an
    //     operate-family state.
    void aKeysRfWaitsForTheAmplifiersOperate()
    {
        Rig rig;
        rig.connectAmp(QStringLiteral("STANDBY"));
        QVERIFY(!rig.model.ampOperate());
        rig.amp()->sendCommand(QStringLiteral("operate=1"));
        rig.model.moxController()->setMox(true);
        QTRY_VERIFY(rig.keyed());
        QTest::qWait(250);
        QCOMPARE(rig.rfOpens.load(), 0);
        // A status still saying standby changes nothing.
        rig.ampReports(QStringLiteral("STANDBY"));
        QTest::qWait(100);
        QCOMPARE(rig.rfOpens.load(), 0);
        rig.ampReports(QStringLiteral("IDLE"));
        QTRY_VERIFY(rig.tx.isRfGateOpen());
        rig.model.moxController()->setMox(false);
        QTRY_VERIFY(rig.atRx());
    }

    // 2. No report within 1.5 s of the command: the key is stopped, in
    //    plain words, and its RF never started.
    void aKeyStillHeldAfterTheBoundIsStoppedWithoutRf()
    {
        Rig rig;
        rig.connectAmp();
        QSignalSpy stopped(&rig.model, &RadioModel::transmitStopped);
        QElapsedTimer since;
        rig.amp()->sendCommand(QStringLiteral("operate=0"));
        since.start();
        rig.model.moxController()->setMox(true);
        QTRY_VERIFY(rig.keyed());
        QTRY_VERIFY_WITH_TIMEOUT(!rig.keyed(), 4000);
        QVERIFY2(since.elapsed() >= 1400, qPrintable(QString::number(since.elapsed())));
        QTRY_VERIFY(rig.atRx());
        QCOMPARE(rig.rfOpens.load(), 0);
        QCOMPARE(stopped.count(), 1);
        QCOMPARE(stopped.at(0).at(0).toString(), kAmpNotSwitched);
        QCOMPARE(rig.model.lastTransmitStopReason().code, QByteArray("ampNotSwitched"));
        // A late report changes nothing: nothing keys again by itself.
        rig.ampReports(QStringLiteral("STANDBY"));
        QTest::qWait(100);
        QVERIFY(!rig.keyed());
        QCOMPARE(rig.rfOpens.load(), 0);
    }

    // 3. No Power Genius, a Power Genius with nothing commanded, or one
    //    whose link went down: the gate is as it always was.
    void withNoPowerGeniusChangingOverNothingChanges()
    {
        for (int kind = 0; kind < 3; ++kind) {
            Rig rig;
            if (kind >= 1) {
                rig.connectAmp();
            }
            if (kind == 2) {
                rig.amp()->sendCommand(QStringLiteral("operate=0"));
                rig.amp()->disconnect();
                QVERIFY(!rig.amp()->isConnected());
            }
            rig.model.moxController()->setMox(true);
            QTRY_VERIFY_WITH_TIMEOUT(rig.tx.isRfGateOpen(), 1000);
            rig.model.moxController()->setMox(false);
            QTRY_VERIFY(rig.atRx());
        }
    }

    // 4a. The Core's own autotune on the air (a TUNE click, the tuner's
    //     hardware TUNE): refused with the on-air words; nothing reaches
    //     the amplifier.
    void theLocalAutotuneIsRefusedOnTheAir()
    {
        for (const bool fromHardware : {false, true}) {
            Rig rig;
            rig.connectAmp();
            rig.connectTuner();
            QSignalSpy refused(&rig.model, &RadioModel::tuneRefused);
            rig.model.moxController()->setMox(true);
            QTRY_VERIFY(rig.keyed());
            rig.model.startTgxlAutotune(fromHardware);
            QVERIFY(!rig.model.isTgxlAutotuneInProgress());
            QCOMPARE(refused.count(), 1);
            QCOMPARE(refused.at(0).at(0).toString(), TxRefusals::radioOnAir().text);
            QCOMPARE(rig.sent.size(), 0);
            rig.model.moxController()->setMox(false);
            QTRY_VERIFY(rig.atRx());
            QCOMPARE(rig.sent.size(), 0);
        }
    }

    // 4b. MOX comes on while the Core's own cycle waits for the standby:
    //     the cycle ends without keying the tune carrier (the key's RF
    //     waits for the standby too), and the amplifier goes back only
    //     once the radio is in receive.
    void theLocalCycleEndsUnkeyedWhenMoxComesOnDuringItsWait()
    {
        Rig rig;
        rig.connectAmp();
        rig.connectTuner();
        rig.model.startTgxlAutotune(/*fromHardware=*/false);
        QVERIFY(rig.model.isTgxlAutotuneInProgress());
        QCOMPARE(rig.sentCount(QStringLiteral("operate=0")), 1);
        rig.model.moxController()->setMox(true);
        QTRY_VERIFY(rig.keyed());
        QTest::qWait(100);
        QCOMPARE(rig.rfOpens.load(), 0);
        rig.ampReports(QStringLiteral("STANDBY"));
        QTRY_VERIFY(!rig.model.isTgxlAutotuneInProgress());
        QVERIFY(!rig.model.isTune());
        QTRY_VERIFY(rig.tx.isRfGateOpen());   // barefoot, the amplifier in standby
        QCOMPARE(rig.sentCount(QStringLiteral("operate=1")), 0);
        rig.model.moxController()->setMox(false);
        QTRY_VERIFY(rig.atRx());
        QTRY_COMPARE(rig.sentCount(QStringLiteral("operate=1")), 1);
        for (const Rig::Sent& s : std::as_const(rig.sent)) {
            QVERIFY2(!s.rf, qPrintable(s.command));
        }
    }

    // 4c. The restore after a cycle waits for every PTT source, not only
    //     for MOX to read receive: a mic held through the tune carrier
    //     keys when the carrier ends, and the amplifier stays in standby
    //     (barefoot) for it; it goes back once the mic is released.
    void theRestoreWaitsForEveryPttSource()
    {
        Rig rig;
        rig.connectAmp();
        rig.connectTuner();
        MoxController* mox = rig.model.moxController();
        rig.model.startTgxlAutotune(/*fromHardware=*/false);
        rig.ampReports(QStringLiteral("STANDBY"));
        QTRY_VERIFY(rig.model.isTune() && rig.keyed());
        QTRY_VERIFY(rig.tx.isRfGateOpen());
        rig.pttDown = true;
        mox->onMicPttFromRadio(true);   // held off under the tune carrier
        rig.model.setTune(false);       // the cycle ends
        QTRY_VERIFY(!rig.model.isTgxlAutotuneInProgress());
        QTest::qWait(200);
        QCOMPARE(rig.sentCount(QStringLiteral("operate=1")), 0);
        rig.pttDown = false;
        mox->onMicPttFromRadio(false);
        QTRY_VERIFY(rig.atRx());
        QTRY_COMPARE(rig.sentCount(QStringLiteral("operate=1")), 1);
        for (const Rig::Sent& s : std::as_const(rig.sent)) {
            QVERIFY2(!s.rf && !s.pttDown, qPrintable(s.command));
        }
    }

    // 4d. An owed restore is dropped when the amplifier changes state on
    //     its own (its front panel): the operator's choice stands.
    void anEdgeNobodyCommandedDropsTheOwedRestore()
    {
        Rig rig;
        rig.connectAmp();
        rig.connectTuner();
        MoxController* mox = rig.model.moxController();
        rig.model.startTgxlAutotune(/*fromHardware=*/false);
        rig.ampReports(QStringLiteral("STANDBY"));
        QTRY_VERIFY(rig.model.isTune() && rig.keyed());
        rig.pttDown = true;
        mox->onMicPttFromRadio(true);
        rig.model.setTune(false);
        QTRY_VERIFY(!rig.model.isTgxlAutotuneInProgress());
        // The front panel: operate, then standby again.
        rig.ampReports(QStringLiteral("OPERATE"));
        rig.ampReports(QStringLiteral("STANDBY"));
        rig.pttDown = false;
        mox->onMicPttFromRadio(false);
        QTRY_VERIFY(rig.atRx());
        QTest::qWait(200);
        QCOMPARE(rig.sentCount(QStringLiteral("operate=1")), 0);
    }
};

QTEST_MAIN(TestAmpChangeoverGate)
#include "tst_amp_changeover_gate.moc"
