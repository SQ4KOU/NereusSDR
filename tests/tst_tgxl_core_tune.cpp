// no-port-check: NereusSDR-original.
// =================================================================
// tests/tst_tgxl_core_tune.cpp  (NereusSDR)
// =================================================================
//
// The Tuner Genius XL tune cycle on a Core (bench 2026-09-30): the tuner's
// own LAN PTT (`transmit tune on/off` on the SmartSDR API listener) and a
// device's tx.tunerTune, against a real Core over loopback links.
//
// The fake tuner does only what the bench log (Core 01d797e5,
// 2026-09-30 23:30) and captures/flex-tgxl-direct-CONTROL.pcapng show a
// real TGXL doing. On :9010 it sends its `V` banner, answers every command
// `R<seq>|0|`, and pushes `S0|state tuning=1/0` and `M|<text>` lines. On
// the Core's SmartSDR API listener it sends `C<seq>|transmit tune on/off`
// from its own address: on a front-panel press, as its answer to an
// `autotune`, and when it ends or gives up a tune. Every test drives those
// lines over real sockets: the fake tuner is at ::1, and any other client
// connects from 127.0.0.1.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-30: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), TGXL tune lane, with AI-assisted implementation
//               via Anthropic Claude Code.
//   2026-10-01: fix round: a socket fake tuner and LAN PTT lines through
//               the listener; the sender, our own autotune and tuning=1
//               cases; the untested ends. J.J. Boyd (KG4VCF), AI-assisted
//               via Anthropic Claude Code.
// =================================================================

#include "MultiDeviceHarness.h"

#include "core/PgxlConnection.h"
#include "core/SmartSdrApiListener.h"
#include "core/TgxlConnection.h"
#include "core/TuneMemoryStore.h"
#include "core/safety/TransmitHolder.h"
#include "models/SliceModel.h"

#include <QHostAddress>
#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>

using namespace NereusSDR;

namespace {

const MirrorUpdate kOn{0, "on", MirrorWireKind::Bool, QVariant(true)};
const MirrorUpdate kOff{0, "on", MirrorWireKind::Bool, QVariant(false)};

// The Power Genius has no socket in these tests (the Core's operate
// commands to it are counted, not delivered); its unopened-socket write
// warnings are not findings here. Same filter as tst_amp_changeover_gate.
QtMessageHandler g_previousHandler = nullptr;
void quietUnopenedSockets(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    if (msg.startsWith(QLatin1String("QIODevice::write"))
        && msg.contains(QLatin1String("device not open"))) {
        return;
    }
    if (g_previousHandler != nullptr) {
        g_previousHandler(type, context, msg);
    }
}

// A SmartSDR-API client of the Core's listener, from a given address.
class LanClient {
public:
    bool connectTo(const QHostAddress& listener, quint16 port)
    {
        m_sock.connectToHost(listener, port);
        return m_sock.waitForConnected(2000);
    }
    void send(const QString& command)
    {
        m_sock.write(QStringLiteral("C%1|%2\n").arg(++m_seq).arg(command).toUtf8());
        m_sock.flush();
    }
    void close() { m_sock.abort(); }

private:
    QTcpSocket m_sock;
    int m_seq{0};
};

// The Tuner Genius: its :9010 control port, and its LAN PTT client.
class FakeTuner : public QObject {
public:
    bool listen()
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            m_conn = m_server.nextPendingConnection();
            connect(m_conn, &QTcpSocket::readyRead, this, [this]() { readCommands(); });
            m_conn->write("V1.2.17\n");
            m_conn->flush();
        });
        return m_server.listen(QHostAddress::LocalHostIPv6, 0);
    }
    quint16 port() const { return m_server.serverPort(); }
    void push(const QString& line)
    {
        if (!m_conn.isNull()) {
            m_conn->write((line + QLatin1Char('\n')).toUtf8());
            m_conn->flush();
        }
    }
    void dropLink()
    {
        if (!m_conn.isNull()) {
            m_conn->abort();
        }
        m_server.close();
    }
    int count(const QString& command) const { return m_commands.count(command); }
    LanClient lan;   // from ::1, the tuner's own address

private:
    void readCommands()
    {
        while (!m_conn.isNull() && m_conn->canReadLine()) {
            const QString line = QString::fromUtf8(m_conn->readLine()).trimmed();
            const int bar = line.indexOf(QLatin1Char('|'));
            if (!line.startsWith(QLatin1Char('C')) || bar < 0) {
                continue;
            }
            m_commands << line.mid(bar + 1);
            m_conn->write(QStringLiteral("R%1|0|\n").arg(line.mid(1, bar - 1)).toUtf8());
            m_conn->flush();
        }
    }

    QTcpServer m_server;
    QPointer<QTcpSocket> m_conn;
    QStringList m_commands;
};

// The Core with its SmartSDR API listener up (any address, a free port)
// and the fake tuner connected on :9010 and to the listener from ::1.
bool startTuner(Core& core, FakeTuner& tuner)
{
    SmartSdrApiListener* listener = core.model->smartSdrListener();
    if (!listener->start(QHostAddress::Any, 0) || !tuner.listen()) {
        return false;
    }
    core.model->tgxlConnection()->connectToTgxl(QStringLiteral("::1"), tuner.port());
    if (!QTest::qWaitFor([&core]() { return core.model->tgxlConnection()->isConnected(); },
                         3000)) {
        return false;
    }
    return tuner.lan.connectTo(QHostAddress::LocalHostIPv6, listener->serverPort());
}

// Device A holds transmit, unkeyed, after its own app TUNE, which the
// tuner answered as the pcap shows (its tune on, its sweep, its tune off).
void deviceHoldsAfterItsTune(Core& core, FakeTuner& tuner, LoopbackTransport* appA)
{
    MoxController* mox = core.model->moxController();
    QVERIFY(core.invoke(appA, "tx.tunerTune", {kOn}).value(QStringLiteral("accepted")).toBool());
    QTRY_VERIFY(mox->isMox());
    QTRY_COMPARE_WITH_TIMEOUT(tuner.count(QStringLiteral("autotune")), 1, 3000);
    tuner.lan.send(QStringLiteral("transmit tune on"));
    tuner.push(QStringLiteral("S0|state tuning=1"));
    QTest::qWait(100);
    QVERIFY(mox->isMox());   // the answer is an echo of this cycle
    tuner.lan.send(QStringLiteral("transmit tune off"));
    tuner.push(QStringLiteral("S0|state tuning=0"));
    QTRY_COMPARE(mox->state(), MoxState::Rx);
    QTRY_VERIFY_WITH_TIMEOUT(!core.model->isTune(), 5000);
    QVERIFY(!core.model->isTgxlAutotuneInProgress());
}

} // namespace

class TgxlCoreTuneTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { g_previousHandler = qInstallMessageHandler(quietUnopenedSockets); }
    void cleanupTestCase() { qInstallMessageHandler(g_previousHandler); }

    // Bug 2 replay, with JJ's ruling (2026-09-30): a device holds transmit
    // after its own tune; the operator presses the tuner's front-panel
    // TUNE, which arrives as its `transmit tune on` (before its interlock
    // ready, 23:30:16.249). It takes transmit as the radio's own PTT does
    // (ruling 8.9) and keys the tune carrier once the take ends; the
    // device is told; the Core sends no autotune of its own.
    void hardwareTuneTakesFromTheHolderAndKeys()
    {
        Core core;
        allowTransmit(core);
        Device a;
        core.pair(a);
        LoopbackTransport* appA = core.signIn(a, kTransmitter);
        QVERIFY(admitted(appA));
        FakeTuner tuner;
        QVERIFY(startTuner(core, tuner));
        deviceHoldsAfterItsTune(core, tuner, appA);
        MoxController* mox = core.model->moxController();
        TransmitHolder* holder = core.server->transmitHolder();
        QVERIFY(holder->isHeldBy(a.key.fingerprint()));
        appA->clearReceived();

        tuner.push(QStringLiteral("S0|state tuning=1"));
        tuner.lan.send(QStringLiteral("transmit tune on"));
        QTRY_VERIFY(core.model->isTgxlAutotuneInProgress());
        QTRY_VERIFY(core.model->isTune());
        QTRY_VERIFY(mox->isMox());
        QVERIFY(holder->isHeldBy(QByteArray(KeyerIdentity::kStationDeviceId)));
        QCOMPARE(holder->holder()->source, TransmitHolder::Source::RadioPtt);

        QTRY_VERIFY(!ofType(appA->received(), QStringLiteral("notice")).isEmpty());
        const QJsonObject notice = ofType(appA->received(), QStringLiteral("notice")).last();
        QCOMPARE(notice.value(QStringLiteral("kind")).toString(), QStringLiteral("transmitTaken"));
        QCOMPARE(notice.value(QStringLiteral("bySource")).toString(), QStringLiteral("radioPtt"));
        QCOMPARE(notice.value(QStringLiteral("byName")).toString(), QStringLiteral("Radio"));

        tuner.lan.send(QStringLiteral("transmit tune off"));
        tuner.push(QStringLiteral("S0|state tuning=0"));
        QTRY_COMPARE(mox->state(), MoxState::Rx);
        QTRY_VERIFY(!core.model->isTune());
        QVERIFY(!core.model->isTgxlAutotuneInProgress());
        QCOMPARE(tuner.count(QStringLiteral("autotune")), 1);   // only the device's
    }

    // C1: `transmit tune on` from any other SmartSDR-API client is not the
    // tuner's press: the station key is refused naming the holder (8.9a).
    void tuneOnFromAnotherClientDoesNotTake()
    {
        Core core;
        allowTransmit(core);
        Device a;
        core.pair(a);
        LoopbackTransport* appA = core.signIn(a, kTransmitter);
        QVERIFY(admitted(appA));
        FakeTuner tuner;
        QVERIFY(startTuner(core, tuner));
        deviceHoldsAfterItsTune(core, tuner, appA);
        MoxController* mox = core.model->moxController();
        TransmitHolder* holder = core.server->transmitHolder();

        LanClient other;
        QVERIFY(other.connectTo(QHostAddress::LocalHost,
                                core.model->smartSdrListener()->serverPort()));
        other.send(QStringLiteral("transmit tune on"));
        QTest::qWait(500);
        QVERIFY(!mox->isMox());
        QVERIFY(!core.model->isTune());
        QVERIFY(!core.model->isTgxlAutotuneInProgress());
        QVERIFY(holder->isHeldBy(a.key.fingerprint()));
    }

    // I2a: the band-change recall sends `autotune`; the tuner's answering
    // `transmit tune on` is not a press and takes nothing.
    void autoRecallAnswerDoesNotTake()
    {
        Core core;
        allowTransmit(core);
        Device a;
        core.pair(a);
        LoopbackTransport* appA = core.signIn(a, kTransmitter);
        QVERIFY(admitted(appA));
        FakeTuner tuner;
        QVERIFY(startTuner(core, tuner));
        deviceHoldsAfterItsTune(core, tuner, appA);
        MoxController* mox = core.model->moxController();
        TransmitHolder* holder = core.server->transmitHolder();

        AppSettings::instance().setValue(QStringLiteral("TGXL_AutoTuneMemoryRecall"),
                                         QStringLiteral("True"));
        core.model->tuneMemoryStore()->store({1, Band::Band40m, 10, 20, 30, 1});
        SliceModel* tx = core.model->txBoundSlice();
        QVERIFY(tx != nullptr);
        tx->setFrequency(7'100'000.0);
        QTRY_COMPARE(tuner.count(QStringLiteral("autotune")), 2);

        tuner.push(QStringLiteral("S0|state tuning=1"));
        tuner.lan.send(QStringLiteral("transmit tune on"));
        QTest::qWait(500);
        QVERIFY(!mox->isMox());
        QVERIFY(!core.model->isTune());
        QVERIFY(holder->isHeldBy(a.key.fingerprint()));
        tuner.lan.send(QStringLiteral("transmit tune off"));
        tuner.push(QStringLiteral("S0|state tuning=0"));

        // Answered: the next tune on is a press again.
        tuner.lan.send(QStringLiteral("transmit tune on"));
        QTRY_VERIFY(mox->isMox());
        QVERIFY(holder->isHeldBy(QByteArray(KeyerIdentity::kStationDeviceId)));
        tuner.lan.send(QStringLiteral("transmit tune off"));
        QTRY_COMPARE(mox->state(), MoxState::Rx);
    }

    // I2b: a device's tune turned off before the tuner answers its
    // autotune: the late `transmit tune on` takes nothing and keys nothing.
    void lateAnswerToADevicesTuneDoesNotTake()
    {
        Core core;
        allowTransmit(core);
        Device a;
        core.pair(a);
        LoopbackTransport* appA = core.signIn(a, kTransmitter);
        QVERIFY(admitted(appA));
        FakeTuner tuner;
        QVERIFY(startTuner(core, tuner));
        MoxController* mox = core.model->moxController();
        TransmitHolder* holder = core.server->transmitHolder();
        QVERIFY(core.invoke(appA, "tx.tunerTune", {kOn}).value(QStringLiteral("accepted")).toBool());
        QTRY_COMPARE_WITH_TIMEOUT(tuner.count(QStringLiteral("autotune")), 1, 3000);
        QVERIFY(core.invoke(appA, "tx.tunerTune", {kOff}).value(QStringLiteral("accepted")).toBool());
        QTRY_COMPARE(mox->state(), MoxState::Rx);
        QTRY_VERIFY_WITH_TIMEOUT(!core.model->isTune(), 5000);
        QVERIFY(holder->isHeldBy(a.key.fingerprint()));

        tuner.push(QStringLiteral("S0|state tuning=1"));
        tuner.lan.send(QStringLiteral("transmit tune on"));
        QTest::qWait(500);
        QVERIFY(!mox->isMox());
        QVERIFY(!core.model->isTune());
        QVERIFY(!core.model->isTgxlAutotuneInProgress());
        QVERIFY(holder->isHeldBy(a.key.fingerprint()));
    }

    // I1: a cycle a desktop's Tuner page starts from the tuner's tuning=1
    // (startTgxlAutotune(true), TunerApplet.cpp) is not a press: it keys
    // as the station and is refused while a device holds transmit.
    void aTuningEdgeAloneDoesNotTake()
    {
        Core core;
        allowTransmit(core);
        Device a;
        core.pair(a);
        LoopbackTransport* appA = core.signIn(a, kTransmitter);
        QVERIFY(admitted(appA));
        FakeTuner tuner;
        QVERIFY(startTuner(core, tuner));
        deviceHoldsAfterItsTune(core, tuner, appA);
        MoxController* mox = core.model->moxController();
        TransmitHolder* holder = core.server->transmitHolder();

        core.model->startTgxlAutotune(/*fromHardware=*/true);
        QTest::qWait(500);
        QVERIFY(!mox->isMox());
        QVERIFY(!core.model->isTune());
        QVERIFY(!core.model->isTgxlAutotuneInProgress());
        QVERIFY(holder->isHeldBy(a.key.fingerprint()));
    }

    // The tuner lets go while its take runs: the take ends, nothing keys.
    void hardwareTuneReleasedDuringTheTakeKeysNothing()
    {
        Core core;
        allowTransmit(core);
        Device a;
        core.pair(a);
        LoopbackTransport* appA = core.signIn(a, kTransmitter);
        QVERIFY(admitted(appA));
        FakeTuner tuner;
        QVERIFY(startTuner(core, tuner));
        deviceHoldsAfterItsTune(core, tuner, appA);
        MoxController* mox = core.model->moxController();

        tuner.lan.send(QStringLiteral("transmit tune on"));
        tuner.lan.send(QStringLiteral("transmit tune off"));
        QTest::qWait(500);
        QVERIFY(!core.model->isTgxlAutotuneInProgress());
        QVERIFY(!mox->isMox());
        QVERIFY(!core.model->isTune());
        QCOMPARE(mox->state(), MoxState::Rx);
    }

    // M2: a take that does not finish ends the cycle; nothing keys after.
    void aTakeThatFailsEndsTheCycle()
    {
        Core core;
        allowTransmit(core);
        Device a;
        core.pair(a);
        LoopbackTransport* appA = core.signIn(a, kTransmitter);
        QVERIFY(admitted(appA));
        FakeTuner tuner;
        QVERIFY(startTuner(core, tuner));
        deviceHoldsAfterItsTune(core, tuner, appA);
        MoxController* mox = core.model->moxController();

        // The take's end reported as failed, in the same call that asked
        // for it (after the Core's own handler, connected first), so before
        // the real transfer's end arrives a turn later.
        bool failedOnce = false;
        connect(core.model->smartSdrListener(), &SmartSdrApiListener::tuneRequested, this,
                [&core, mox, &failedOnce](bool on) {
                    if (on && !failedOnce && core.model->isTgxlAutotuneInProgress()
                        && !mox->isMox()) {
                        failedOnce = true;
                        emit mox->tunerTakeFinished(false);
                    }
                });
        tuner.lan.send(QStringLiteral("transmit tune on"));
        QTRY_VERIFY(failedOnce);
        QVERIFY(!core.model->isTgxlAutotuneInProgress());
        QTest::qWait(500);
        QVERIFY(!mox->isMox());
        QVERIFY(!core.model->isTune());
    }

    // M2: a holder on the air is never taken from: the cycle refuses to
    // start while RF flows.
    void aHolderOnTheAirIsNotTaken()
    {
        Core core;
        allowTransmit(core);
        Device a;
        core.pair(a);
        LoopbackTransport* appA = core.signIn(a, kTransmitter);
        QVERIFY(admitted(appA));
        FakeTuner tuner;
        QVERIFY(startTuner(core, tuner));
        MoxController* mox = core.model->moxController();
        TransmitHolder* holder = core.server->transmitHolder();
        mox->setMox(true, keyerFor(a));
        QTRY_VERIFY(mox->isMox());
        QVERIFY(holder->isHeldBy(a.key.fingerprint()));

        tuner.lan.send(QStringLiteral("transmit tune on"));
        QTest::qWait(500);
        QVERIFY(mox->isMox());
        QCOMPARE(mox->currentKeyer().deviceId, a.key.fingerprint());
        QVERIFY(!core.model->isTune());
        QVERIFY(!core.model->isTgxlAutotuneInProgress());
        QVERIFY(holder->isHeldBy(a.key.fingerprint()));
        mox->setMox(false, keyerFor(a));
        QTRY_COMPARE(mox->state(), MoxState::Rx);
    }

    // M2: the start watchdog covers a carrier keyed by a take.
    void aTakenCarrierDropsOnTheSafetyTimeout()
    {
        Core core;
        allowTransmit(core);
        Device a;
        core.pair(a);
        LoopbackTransport* appA = core.signIn(a, kTransmitter);
        QVERIFY(admitted(appA));
        FakeTuner tuner;
        QVERIFY(startTuner(core, tuner));
        deviceHoldsAfterItsTune(core, tuner, appA);
        MoxController* mox = core.model->moxController();

        tuner.lan.send(QStringLiteral("transmit tune on"));
        QTRY_VERIFY(mox->isMox());
        QTest::qWait(1000);
        QVERIFY(mox->isMox());
        QTRY_COMPARE_WITH_TIMEOUT(mox->state(), MoxState::Rx, 5000);
        QTRY_VERIFY(!core.model->isTune());
        QVERIFY(!core.model->isTgxlAutotuneInProgress());
    }

    // M2: the tuner lets go during the amplifier's standby wait: the cycle
    // ends, and the standby arriving afterwards keys nothing.
    void tuneOffDuringTheAmplifierStandbyKeysNothing()
    {
        Core core;
        allowTransmit(core);
        FakeTuner tuner;
        QVERIFY(startTuner(core, tuner));
        MoxController* mox = core.model->moxController();
        PgxlConnection* amp = core.model->pgxlConnection();
        amp->injectLineForTesting(QStringLiteral("V3.8.9"));
        amp->injectLineForTesting(QStringLiteral("R1|0|state=OPERATE"));

        tuner.lan.send(QStringLiteral("transmit tune on"));
        QTRY_VERIFY(core.model->isTgxlAutotuneInProgress());
        QVERIFY(!mox->isMox());   // waiting for the amplifier's standby
        tuner.lan.send(QStringLiteral("transmit tune off"));
        QTRY_VERIFY(!core.model->isTgxlAutotuneInProgress());
        amp->injectLineForTesting(QStringLiteral("S0|status state=STANDBY"));
        QTest::qWait(500);
        QVERIFY(!mox->isMox());
        QVERIFY(!core.model->isTune());
    }

    // A device's tx.tunerTune never takes: with another device holding
    // transmit it is refused, and nothing keys.
    void aDeviceTunerTuneStillCannotTake()
    {
        Core core;
        allowTransmit(core);
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = core.signIn(a, kTransmitter);
        LoopbackTransport* appB = core.signIn(b, kTransmitter);
        QVERIFY(admitted(appA));
        QVERIFY(admitted(appB));
        FakeTuner tuner;
        QVERIFY(startTuner(core, tuner));
        deviceHoldsAfterItsTune(core, tuner, appA);
        MoxController* mox = core.model->moxController();
        TransmitHolder* holder = core.server->transmitHolder();

        const QJsonObject answer = core.invoke(appB, "tx.tunerTune", {kOn});
        QVERIFY(!answer.value(QStringLiteral("accepted")).toBool());
        QVERIFY2(answer.value(QStringLiteral("reason")).toString().contains(QStringLiteral("iPhone")),
                 qPrintable(answer.value(QStringLiteral("reason")).toString()));
        QTest::qWait(500);
        QVERIFY(!mox->isMox());
        QVERIFY(!core.model->isTune());
        QVERIFY(holder->isHeldBy(a.key.fingerprint()));
        QCOMPARE(tuner.count(QStringLiteral("autotune")), 1);
    }

    // The tuner's front-panel TUNE with transmit unheld keys the station's
    // tune carrier without an autotune of ours, and its tune off drops it.
    void hardwareTuneKeysAndDropsOnTheTunersTuneOff()
    {
        Core core;
        allowTransmit(core);
        FakeTuner tuner;
        QVERIFY(startTuner(core, tuner));
        MoxController* mox = core.model->moxController();
        tuner.lan.send(QStringLiteral("transmit tune on"));
        QTRY_VERIFY(core.model->isTune());
        QTRY_VERIFY(mox->isMox());
        QVERIFY(core.model->isTgxlAutotuneInProgress());

        tuner.lan.send(QStringLiteral("transmit tune off"));
        QTRY_COMPARE(mox->state(), MoxState::Rx);
        QTRY_VERIFY(!core.model->isTune());
        QVERIFY(!core.model->isTgxlAutotuneInProgress());
        QCOMPARE(tuner.count(QStringLiteral("autotune")), 0);
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
        FakeTuner tuner;
        QVERIFY(startTuner(core, tuner));
        MoxController* mox = core.model->moxController();
        QVERIFY(core.invoke(appA, "tx.tunerTune", {kOn}).value(QStringLiteral("accepted")).toBool());
        QTRY_VERIFY(mox->isMox());
        QTRY_COMPARE_WITH_TIMEOUT(tuner.count(QStringLiteral("autotune")), 1, 3000);

        tuner.lan.send(QStringLiteral("transmit tune off"));
        tuner.push(QStringLiteral("M|LOW RF POWER"));
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
        FakeTuner tuner;
        QVERIFY(startTuner(core, tuner));
        MoxController* mox = core.model->moxController();
        QVERIFY(core.invoke(appA, "tx.tunerTune", {kOn}).value(QStringLiteral("accepted")).toBool());
        QTRY_VERIFY(mox->isMox());
        tuner.push(QStringLiteral("S0|state tuning=1"));
        QTest::qWait(100);
        QVERIFY(mox->isMox());
        tuner.push(QStringLiteral("S0|state tuning=0"));
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
        FakeTuner tuner;
        QVERIFY(startTuner(core, tuner));
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
        FakeTuner tuner;
        QVERIFY(startTuner(core, tuner));
        MoxController* mox = core.model->moxController();
        QVERIFY(core.invoke(appA, "tx.tunerTune", {kOn}).value(QStringLiteral("accepted")).toBool());
        QTRY_VERIFY(mox->isMox());
        tuner.push(QStringLiteral("S0|state tuning=1"));
        QTest::qWait(100);

        tuner.dropLink();
        QTRY_COMPARE(mox->state(), MoxState::Rx);
        QTRY_VERIFY(!core.model->isTune());
        QVERIFY(!core.model->isTgxlAutotuneInProgress());
    }

    // The same for the tuner's own front-panel cycle.
    void tunerDisconnectDropsAHardwareCycle()
    {
        Core core;
        allowTransmit(core);
        FakeTuner tuner;
        QVERIFY(startTuner(core, tuner));
        MoxController* mox = core.model->moxController();
        tuner.push(QStringLiteral("S0|state tuning=1"));
        tuner.lan.send(QStringLiteral("transmit tune on"));
        QTRY_VERIFY(mox->isMox());

        tuner.dropLink();
        QTRY_COMPARE(mox->state(), MoxState::Rx);
        QTRY_VERIFY(!core.model->isTune());
        QVERIFY(!core.model->isTgxlAutotuneInProgress());
    }

    // The tuner's front-panel cycle gets the 3 s start watchdog: a tuner
    // that never reports its sweep has its carrier dropped.
    void hardwareCycleDropsOnTheSafetyTimeout()
    {
        Core core;
        allowTransmit(core);
        FakeTuner tuner;
        QVERIFY(startTuner(core, tuner));
        MoxController* mox = core.model->moxController();
        tuner.lan.send(QStringLiteral("transmit tune on"));
        QTRY_VERIFY(mox->isMox());
        QTest::qWait(1000);
        QVERIFY(mox->isMox());
        QTRY_COMPARE_WITH_TIMEOUT(mox->state(), MoxState::Rx, 5000);
        QTRY_VERIFY(!core.model->isTune());
        QVERIFY(!core.model->isTgxlAutotuneInProgress());
    }

    // ... and its sweep's end (tuning 1 then 0) drops the carrier too.
    void hardwareCycleDropsWhenTheSweepEnds()
    {
        Core core;
        allowTransmit(core);
        FakeTuner tuner;
        QVERIFY(startTuner(core, tuner));
        MoxController* mox = core.model->moxController();
        tuner.push(QStringLiteral("S0|state tuning=1"));
        tuner.lan.send(QStringLiteral("transmit tune on"));
        QTRY_VERIFY(mox->isMox());
        QTest::qWait(3500);
        QVERIFY(mox->isMox());   // the sweep runs past the start watchdog
        tuner.push(QStringLiteral("S0|state tuning=0"));
        QTRY_COMPARE(mox->state(), MoxState::Rx);
        QTRY_VERIFY(!core.model->isTune());
        QVERIFY(!core.model->isTgxlAutotuneInProgress());
    }

    // The tuner's `M|` messages (why a tune ended) reach the log and a signal.
    void tunerMessageLineIsReported()
    {
        Core core;
        FakeTuner tuner;
        QVERIFY(startTuner(core, tuner));
        QSignalSpy spy(core.model->tgxlConnection(), &TgxlConnection::messageReceived);
        tuner.push(QStringLiteral("M|Tuned SWR: 1.05:1"));
        tuner.push(QStringLiteral("M|LOW RF POWER"));
        QTRY_COMPARE(spy.count(), 2);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("Tuned SWR: 1.05:1"));
        QCOMPARE(spy.at(1).at(0).toString(), QStringLiteral("LOW RF POWER"));
    }
};

QTEST_MAIN(TgxlCoreTuneTest)
#include "tst_tgxl_core_tune.moc"
