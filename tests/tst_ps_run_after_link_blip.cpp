// no-port-check: NereusSDR-original test. The Thetis behaviour it guards is
// cited beside the code in RadioModel.cpp and P1RadioConnection.cpp; this
// file translates no C#.
//
// tests/tst_ps_run_after_link_blip.cpp
//
// TX safety fix round 1, I2 (2026-09-30). Every new link starts with the
// PureSignal run flag off: P1 clears it for each reconnect
// (P1RadioConnection::dropTransmitForNewLink) and P2's connectToRadio
// clears it after the model's pre-start push. With PS-A running, the
// model's own PureSignal enable (PureSignal::isPsEnabled, the source that
// drives the flag while PS-A runs) must go back once the link is up again,
// or the wire says PureSignal is off while the model and PS-A say it is on.
//
// Against in-process fake radios only (never a real radio); nothing keys.
//   1. P1: PS-A running, the link goes silent, the connection reconnects by
//      itself, and bank 11's C2 bit 6 (puresignal_run) is back on ep2.
//   2. P2: the transmit model's PureSignal enable on and PS-A running, the
//      link goes silent (LinkLost) and is rebuilt as the hosted retry does
//      (disconnectFromRadio, then connectToRadio), and the new
//      connection's run flag is on again, before PS-A has re-armed and
//      after. P2's connectToRadio clears the flag after the model's
//      pre-start push, so without the push on Connected the first connect
//      and every rebuilt link start with it off. P2 carries the flag on
//      the wire only while keyed (the DDC frequency override and the PS DDC
//      enable), so this test reads the flag the next key would send.

#include <QtTest/QtTest>
#include <QFile>
#include <QTimer>

#include <memory>

#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/DspControlThread.h"
#include "core/P1RadioConnection.h"
#include "core/P2RadioConnection.h"
#include "core/PureSignal.h"
#include "core/WdspEngine.h"
#include "core/session/PureSignalSessionFacade.h"
#include "models/RadioModel.h"
#include "fakes/ConnectableRadioModel.h"
#include "fakes/FakeAudioBus.h"
#include "fakes/P1FakeRadio.h"
#include "fakes/P2FakeRadio.h"

using namespace NereusSDR;
using NereusSDR::Test::ConnectableRadioModel;
using NereusSDR::Test::P1FakeRadio;
using NereusSDR::Test::P2FakeRadio;

namespace {

constexpr int kTestWatchdogSilenceMs   = 300;
constexpr int kTestReconnectIntervalMs = 300;
constexpr quint8 kBank11Address = 0x14;

// Any ep2 subframe in `log` that is bank 11 with C2 bit 6 (puresignal_run).
bool bank11PsRun(const QList<QByteArray>& log)
{
    for (const QByteArray& cc : log) {
        for (int sub = 0; sub < 2; ++sub) {
            const quint8 c0 = static_cast<quint8>(cc.at(sub * 5));
            if ((c0 & 0xFE) == kBank11Address
                && (static_cast<quint8>(cc.at(sub * 5 + 2)) & 0x40) != 0) {
                return true;
            }
        }
    }
    return false;
}

// Starts PS-A from the model's facade, as the [PS-A] button does, and waits
// for PureSignal to report itself enabled. Its timers then stop, so the
// blip that follows is shorter than any PureSignal tick: PureSignal never
// sees the link down and stays enabled throughout, as it does for a short
// blip in use (a longer one turns PS off on its readiness check and back on
// after the link returns, which re-sends the flag through psEnabledChanged
// and would hide the fault).
bool startPsA(RadioModel& model)
{
    PureSignalSessionFacade* facade = model.pureSignalFacade();
    if (facade == nullptr
        || !QTest::qWaitFor([facade]() { return facade->canActuate(); }, 10000)) {
        return false;
    }
    facade->executeAction(Ps3Action::StartAutomatic, {}, 1);
    const bool enabled = QTest::qWaitFor(
        [&model]() {
            PureSignal* ps = model.pureSignal();
            if (ps == nullptr) {
                return false;
            }
            ps->pollTimerTick();
            return ps->isPsEnabled();
        },
        10000);
    if (enabled) {
        model.pureSignal()->setTimersEnabled(false);
    }
    return enabled;
}

// The run flag as the connection holds it, read on its own thread.
bool connectionPsRun(RadioConnection* conn)
{
    bool run = false;
    QMetaObject::invokeMethod(conn, [conn, &run]() { run = conn->puresignalRunForTest(); },
                              Qt::BlockingQueuedConnection);
    return run;
}

bool waitLanesIdle(RadioModel& model)
{
    constexpr int kLaneTimeoutMs = 600000;
    for (DspControlThread* lane : {model.transmitLane(), model.receiveLane()}) {
        if (lane != nullptr
            && !QTest::qWaitFor([lane]() { return lane->waitIdleForTest(0); },
                                kLaneTimeoutMs)) {
            return false;
        }
    }
    return true;
}

void installOpenAudioBuses(AudioEngine& engine)
{
    AudioFormat format;
    format.sampleRate = 48000;
    format.channels = 2;
    format.sample = AudioFormat::Sample::Float32;
    auto speakers = std::make_unique<NereusSDR::FakeAudioBus>(QStringLiteral("Fake speakers"));
    auto txInput = std::make_unique<NereusSDR::FakeAudioBus>(QStringLiteral("Fake TX input"));
    speakers->open(format);
    txInput->open(format);
    engine.setSpeakersBusForTest(std::move(speakers));
    engine.setTxInputBusForTest(std::move(txInput));
}

} // namespace

class TestPsRunAfterLinkBlip : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // PS-A saves its preference; keep it in this run's own file.
        AppSettings::setProfileOverride(QStringLiteral("ps-run-after-link-blip-%1")
                                            .arg(QCoreApplication::applicationPid()));
    }

    void cleanupTestCase()
    {
        QFile::remove(AppSettings::instance().filePath());
    }

    void p1SelfReconnectPutsPsRunBack()
    {
        std::unique_ptr<ConnectableRadioModel> harness = ConnectableRadioModel::create();
        QVERIFY2(harness != nullptr, "the model never reached Connected");
        RadioModel& model = harness->model();
        auto* p1 = qobject_cast<P1RadioConnection*>(model.connection());
        QVERIFY(p1 != nullptr);
        QMetaObject::invokeMethod(p1, [p1]() {
            p1->setReconnectTimingForTest(kTestWatchdogSilenceMs, kTestReconnectIntervalMs);
        }, Qt::BlockingQueuedConnection);

        QVERIFY2(startPsA(model), "PS-A never reported PureSignal enabled");
        harness->fake().clearEp2CcLog();
        QTRY_VERIFY_WITH_TIMEOUT(bank11PsRun(harness->fake().ep2CcReceived()), 5000);

        // The link goes silent and the connection reconnects by itself.
        bool sawLinkLost = false;
        P1FakeRadio* fake = &harness->fake();
        const QMetaObject::Connection watch = connect(
            &model, &RadioModel::connectionStateChanged, this,
            [fake, &sawLinkLost](ConnectionState s) {
                if (s == ConnectionState::LinkLost && !sawLinkLost) {
                    sawLinkLost = true;
                    fake->resume();
                }
            });
        fake->goSilent();
        QTRY_VERIFY_WITH_TIMEOUT(sawLinkLost, 5000);
        disconnect(watch);
        QTRY_COMPARE_WITH_TIMEOUT(model.connectionState(), ConnectionState::Connected, 10000);
        QCOMPARE(model.connection(), static_cast<RadioConnection*>(p1));
        QVERIFY(fake->metisStopCount() >= 1);

        // PS-A still runs, and the wire says so again.
        QVERIFY(model.pureSignal() != nullptr && model.pureSignal()->isPsEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(bank11PsRun(fake->ep2CcReceived()), 5000);
        QVERIFY(connectionPsRun(p1));
    }

    void p2LinkBackPutsPsRunBack()
    {
        P2FakeRadio fake;
        QVERIFY(fake.start());
        const RadioInfo info = fake.radioInfo();

        RadioModel model;
        model.audioEngine()->setStartInitializerForTest(installOpenAudioBuses);
        model.wdspEngine()->setSynchronousInitForTest(true);
        model.configureP2TransportForTest(fake.outboundPortBase(), fake.inputRolePortBase(),
                                          1000, 200);
        QTimer ingress;
        ingress.setInterval(5);
        connect(&ingress, &QTimer::timeout, this, [&fake]() {
            if (fake.hasClient()) {
                fake.sendDdc(2);
                fake.sendDdc(3);
                fake.sendStatus();
            }
        });
        ingress.start();

        model.transmitModel().setPureSigEnabled(true);
        model.connectToRadio(info);
        QTRY_VERIFY_WITH_TIMEOUT(model.isConnected(), 15000);
        QVERIFY(waitLanesIdle(model));
        QTRY_VERIFY_WITH_TIMEOUT(connectionPsRun(model.connection()), 5000);
        QVERIFY2(startPsA(model), "PS-A never reported PureSignal enabled");
        QVERIFY(connectionPsRun(model.connection()));

        // The link goes silent: P2 sends one unkeyed stop and stays down.
        fake.stopIngress();
        QTRY_COMPARE_WITH_TIMEOUT(model.connectionState(), ConnectionState::LinkLost, 5000);
        QCOMPARE(fake.moxAssertedCount(), 0);

        // The link is rebuilt as the hosted retry does it.
        fake.resumeIngress();
        model.disconnectFromRadio();
        model.connectToRadio(info);
        QTRY_VERIFY_WITH_TIMEOUT(model.isConnected(), 15000);
        QVERIFY(waitLanesIdle(model));
        QVERIFY(qobject_cast<P2RadioConnection*>(model.connection()) != nullptr);

        // The rebuilt link has the enable back before PS-A has re-armed
        // (its timers are still stopped)...
        QTRY_VERIFY_WITH_TIMEOUT(connectionPsRun(model.connection()), 5000);
        // ...and keeps it once PS-A runs again.
        PureSignal* ps = model.pureSignal();
        QVERIFY(ps != nullptr);
        ps->setTimersEnabled(true);
        QTRY_VERIFY_WITH_TIMEOUT(
            [ps]() {
                ps->pollTimerTick();
                return ps->isPsEnabled();
            }(),
            10000);
        QVERIFY(connectionPsRun(model.connection()));
        QCOMPARE(fake.moxAssertedCount(), 0);

        ingress.stop();
        model.disconnectFromRadio();
        fake.stop();
    }
};

QTEST_MAIN(TestPsRunAfterLinkBlip)
#include "tst_ps_run_after_link_blip.moc"
