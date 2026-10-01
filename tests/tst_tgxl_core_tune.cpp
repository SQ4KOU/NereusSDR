// no-port-check: NereusSDR-original.
// =================================================================
// tests/tst_tgxl_core_tune.cpp  (NereusSDR)
// =================================================================
//
// The Tuner Genius XL tune cycle on a Core (bench 2026-09-30): the tuner's
// own LAN PTT (`transmit tune on/off` on the SmartSDR API listener) and a
// device's tx.tunerTune, against a real Core over loopback links.
//
// The tuner here does only what the bench log (Core 01d797e5,
// 2026-09-30 23:30) and captures/flex-tgxl-direct-CONTROL.pcapng show a
// real TGXL doing: `transmit tune on` from its front panel, `transmit tune
// off` when it ends or gives up a tune, `S0|... tuning=1/0` on :9010, and
// `M|<text>` messages such as `M|Tuned SWR: 1.05:1`.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-30: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), TGXL tune lane, with AI-assisted implementation
//               via Anthropic Claude Code.
// =================================================================

#include "MultiDeviceHarness.h"

#include "core/SmartSdrApiListener.h"
#include "core/TgxlConnection.h"

#include <QRegularExpression>
#include <QSignalSpy>

using namespace NereusSDR;


namespace {

const MirrorUpdate kOn{0, "on", MirrorWireKind::Bool, QVariant(true)};
const MirrorUpdate kOff{0, "on", MirrorWireKind::Bool, QVariant(false)};

void tunerConnected(Core& core)
{
    core.model->tgxlConnection()->injectLineForTesting(QStringLiteral("V1.2.17"));
}

} // namespace

class TgxlCoreTuneTest : public QObject {
    Q_OBJECT

private slots:
    // Bug 2 replay (23:30:16.249): the tuner's front-panel TUNE arrives as
    // `transmit tune on` while a device holds transmit after its own tune.
    // The station's tune carrier is refused by the holder rules (ruling
    // 8.9a), nothing keys, the cycle ends, and the log says why.
    void hardwareTuneRefusedWhileADeviceHoldsTransmitSaysWhy()
    {
        Core core;
        allowTransmit(core);
        Device a;
        core.pair(a);
        LoopbackTransport* appA = core.signIn(a, kTransmitter);
        QVERIFY(admitted(appA));
        tunerConnected(core);
        MoxController* mox = core.model->moxController();
        QVERIFY(core.invoke(appA, "tx.tunerTune", {kOn}).value(QStringLiteral("accepted")).toBool());
        QTRY_VERIFY(mox->isMox());
        QVERIFY(core.invoke(appA, "tx.tunerTune", {kOff}).value(QStringLiteral("accepted")).toBool());
        QTRY_COMPARE(mox->state(), MoxState::Rx);
        QTRY_VERIFY_WITH_TIMEOUT(!core.model->isTune(), 5000);
        QVERIFY(core.server->transmitHolder()->isHeldBy(a.key.fingerprint()));

        QTest::ignoreMessage(QtInfoMsg,
                             QRegularExpression(QStringLiteral(
                                 "the tune carrier was refused; ending the cycle\\. Reason: "
                                 "\"iPhone has the transmitter\\.\"")));
        emit core.model->smartSdrListener()->tuneRequested(true);

        QVERIFY(!core.model->isTune());
        QVERIFY(!mox->isMox());
        QVERIFY(!core.model->isTgxlAutotuneInProgress());
        QVERIFY(core.server->transmitHolder()->isHeldBy(a.key.fingerprint()));
    }

    // The tuner's front-panel TUNE with transmit unheld keys the station's
    // tune carrier, and the tuner's `transmit tune off` drops it.
    void hardwareTuneKeysAndDropsOnTheTunersTuneOff()
    {
        Core core;
        allowTransmit(core);
        tunerConnected(core);
        MoxController* mox = core.model->moxController();
        emit core.model->smartSdrListener()->tuneRequested(true);
        QVERIFY(core.model->isTune());
        QTRY_VERIFY(mox->isMox());
        QVERIFY(core.model->isTgxlAutotuneInProgress());

        emit core.model->smartSdrListener()->tuneRequested(false);
        QTRY_COMPARE(mox->state(), MoxState::Rx);
        QTRY_VERIFY(!core.model->isTune());
        QVERIFY(!core.model->isTgxlAutotuneInProgress());
    }

    // Bug 1 replay (23:30:30.091): the tuner answers a device's autotune
    // with `transmit tune off` before its sweep starts. The pcap shows that
    // as the tuner giving the tune up (tune off with interlock not_ready,
    // then `M|LOW RF POWER`), so the carrier drops and the cycle ends.
    void appTuneDropsWhenTheTunerGivesUp()
    {
        Core core;
        allowTransmit(core);
        Device a;
        core.pair(a);
        LoopbackTransport* appA = core.signIn(a, kTransmitter);
        QVERIFY(admitted(appA));
        tunerConnected(core);
        MoxController* mox = core.model->moxController();
        QVERIFY(core.invoke(appA, "tx.tunerTune", {kOn}).value(QStringLiteral("accepted")).toBool());
        QTRY_VERIFY(mox->isMox());
        QVERIFY(core.model->isTgxlAutotuneInProgress());

        emit core.model->smartSdrListener()->tuneRequested(false);
        QTRY_COMPARE(mox->state(), MoxState::Rx);
        QTRY_VERIFY(!core.model->isTune());
        QVERIFY(!core.model->isTgxlAutotuneInProgress());
    }

    // A device's cycle ends with the tuner's sweep: tuning 1 then 0.
    void deviceCycleDropsWhenTheSweepEnds()
    {
        Core core;
        allowTransmit(core);
        Device a;
        core.pair(a);
        LoopbackTransport* appA = core.signIn(a, kTransmitter);
        QVERIFY(admitted(appA));
        tunerConnected(core);
        MoxController* mox = core.model->moxController();
        QVERIFY(core.invoke(appA, "tx.tunerTune", {kOn}).value(QStringLiteral("accepted")).toBool());
        QTRY_VERIFY(mox->isMox());
        core.model->tgxlConnection()->injectLineForTesting(QStringLiteral("S0|state tuning=1"));
        QTest::qWait(100);
        QVERIFY(mox->isMox());
        core.model->tgxlConnection()->injectLineForTesting(QStringLiteral("S0|state tuning=0"));
        QTRY_COMPARE(mox->state(), MoxState::Rx);
        QTRY_VERIFY(!core.model->isTune());
        QVERIFY(!core.model->isTgxlAutotuneInProgress());
    }

    // A device's cycle whose tuner never starts its sweep drops after the
    // bounded start watchdog (3 s).
    void deviceCycleDropsOnTheSafetyTimeout()
    {
        Core core;
        allowTransmit(core);
        Device a;
        core.pair(a);
        LoopbackTransport* appA = core.signIn(a, kTransmitter);
        QVERIFY(admitted(appA));
        tunerConnected(core);
        MoxController* mox = core.model->moxController();
        QVERIFY(core.invoke(appA, "tx.tunerTune", {kOn}).value(QStringLiteral("accepted")).toBool());
        QTRY_VERIFY(mox->isMox());
        QTest::qWait(1000);
        QVERIFY(mox->isMox());
        QTRY_COMPARE_WITH_TIMEOUT(mox->state(), MoxState::Rx, 5000);
        QTRY_VERIFY(!core.model->isTune());
        QVERIFY(!core.model->isTgxlAutotuneInProgress());
    }

    // The tuner's link dropping mid-sweep drops the carrier: no tune off or
    // tuning=0 will come from a lost tuner.
    void tunerDisconnectDropsTheCarrier()
    {
        Core core;
        allowTransmit(core);
        Device a;
        core.pair(a);
        LoopbackTransport* appA = core.signIn(a, kTransmitter);
        QVERIFY(admitted(appA));
        tunerConnected(core);
        MoxController* mox = core.model->moxController();
        QVERIFY(core.invoke(appA, "tx.tunerTune", {kOn}).value(QStringLiteral("accepted")).toBool());
        QTRY_VERIFY(mox->isMox());
        core.model->tgxlConnection()->injectLineForTesting(QStringLiteral("S0|state tuning=1"));

        emit core.model->tgxlConnection()->disconnected();
        QTRY_COMPARE(mox->state(), MoxState::Rx);
        QTRY_VERIFY(!core.model->isTune());
        QVERIFY(!core.model->isTgxlAutotuneInProgress());
    }

    // The same for the tuner's own front-panel cycle, which has no watchdog.
    void tunerDisconnectDropsAHardwareCycle()
    {
        Core core;
        allowTransmit(core);
        tunerConnected(core);
        MoxController* mox = core.model->moxController();
        emit core.model->smartSdrListener()->tuneRequested(true);
        QTRY_VERIFY(mox->isMox());

        emit core.model->tgxlConnection()->disconnected();
        QTRY_COMPARE(mox->state(), MoxState::Rx);
        QTRY_VERIFY(!core.model->isTune());
        QVERIFY(!core.model->isTgxlAutotuneInProgress());
    }

    // The tuner's `M|` messages (why a tune ended) reach the log and a signal.
    void tunerMessageLineIsReported()
    {
        Core core;
        tunerConnected(core);
        QSignalSpy spy(core.model->tgxlConnection(), &TgxlConnection::messageReceived);
        core.model->tgxlConnection()->injectLineForTesting(QStringLiteral("M|Tuned SWR: 1.05:1"));
        core.model->tgxlConnection()->injectLineForTesting(QStringLiteral("M|LOW RF POWER"));
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("Tuned SWR: 1.05:1"));
        QCOMPARE(spy.at(1).at(0).toString(), QStringLiteral("LOW RF POWER"));
    }
};

QTEST_MAIN(TgxlCoreTuneTest)
#include "tst_tgxl_core_tune.moc"
