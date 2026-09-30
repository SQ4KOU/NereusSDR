// no-port-check: test-only. Thetis file names appear only in source-cite
// comments that document which upstream line each assertion verifies.
// No Thetis logic is ported here; this file is NereusSDR-original.
//
// =================================================================
// tests/tst_level_calibration_station.cpp  (NereusSDR)
// =================================================================
//
// Level Cal as a Core procedure: the Core runs Thetis CalibrateLevel
// (console.cs:9856-10232 [v2.10.3.15]) through LevelCalibrationService,
// a paired device starts and cancels it over the session with the
// startLevelCalibration and cancelLevelCalibration verbs
// (radioHardwareVersion 12), and radio's levelCalRunning, levelCalPercent,
// levelCalMessage and levelCalSucceeded carry its progress to a peer that
// declared levelCalibration 1. A remote window's RadioModel makes the same
// calls a local one does.
//
// The receiver is a fake meter and spectrum (FakeLevelCalibrationHost.h)
// put in place of the Core's own receiver; every wait is zero or short.
// The on-air case sets the transmit state only. Nothing here keys a radio
// or touches a device.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-29 - Written for NereusSDR by J.J. Boyd (KG4VCF), with
//                AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include "MultiDeviceHarness.h"

#include <QScopeGuard>
#include <QSignalSpy>

#include "FakeLevelCalibrationHost.h"
#include "core/LevelCalibrationService.h"
#include "core/StepAttenuatorController.h"
#include "core/session/StationClient.h"
#include "core/settings/SettingsProxy.h"

using namespace NereusSDR::LevelCalTest;

namespace {

const QString kPairedOnly = QStringLiteral("Calibrate the receive level from a paired device.");
const QString kUnread = QStringLiteral("The Core could not read this request.");
const QString kNoSlice = QStringLiteral("The slice to calibrate is not open.");
const QString kFinished = QStringLiteral("Level calibration finished.");
const QString kCanceled = QStringLiteral("Level calibration was canceled.");
const QString kRadioOff = QStringLiteral("Turn the radio on before calibrating the receive level.");

QHash<QByteArray, int> withLevelCal()
{
    QHash<QByteArray, int> features = kHolder;
    features.insert(QByteArrayLiteral("levelCalibration"), 1);
    return features;
}

QList<MirrorUpdate> startArgs(double levelDbm = -50.0, double hz = kCentre + 1000.0,
                              qint64 sliceId = 0)
{
    return {f64("levelDbm", levelDbm), f64("frequencyHz", hz), int64("sliceId", sliceId)};
}

bool accepted(const QJsonObject& result)
{
    return result.value(QStringLiteral("accepted")).toBool(false);
}

QString reason(const QJsonObject& result)
{
    return result.value(QStringLiteral("reason")).toString();
}

// The Core's service on the fake receiver. `holdMs` keeps the run going
// that long at its last wait, so a test can see it running.
LevelCalibrationService* fakeReceiver(Core& core, FakeHost& fake, int holdMs = 0)
{
    LevelCalibrationService* service = core.model->levelCalibrationServiceForTest();
    if (service == nullptr) {
        return nullptr;
    }
    service->setHostForTest(&fake);
    LevelCalibrationRun::Timings timings = instant();
    timings.finalSettleMs = holdMs;
    service->setTimingsForTest(timings);
    return service;
}

} // namespace

class TstLevelCalibrationStation : public QObject {
    Q_OBJECT

private slots:
    void init() { AppSettings::instance().clear(); }
    void cleanup() { AppSettings::instance().clear(); }

    // A paired device starts the run; the Core runs it and every step of
    // it reaches the device.
    void pairedDevice_startRunsAndReportsProgress()
    {
        Core core;
        FakeHost fake;
        QVERIFY(fakeReceiver(core, fake, 300) != nullptr);
        Device a;
        core.pair(a);
        LoopbackTransport* app = core.signIn(a, withLevelCal());
        QVERIFY(admitted(app));
        QCOMPARE(latest(app->received(), QStringLiteral("radio"),
                        QStringLiteral("levelCalRunning")).toBool(true), false);

        const QJsonObject started =
            core.invoke(app, "startLevelCalibration", startArgs());
        QVERIFY2(accepted(started), qPrintable(reason(started)));
        QTRY_COMPARE(latest(app->received(), QStringLiteral("radio"),
                            QStringLiteral("levelCalRunning")).toBool(false), true);
        QTRY_COMPARE(latest(app->received(), QStringLiteral("radio"),
                            QStringLiteral("levelCalRunning")).toBool(true), false);
        QTRY_COMPARE(latest(app->received(), QStringLiteral("radio"),
                            QStringLiteral("levelCalSucceeded")).toBool(false), true);
        QCOMPARE(latest(app->received(), QStringLiteral("radio"),
                        QStringLiteral("levelCalMessage")).toString(), kFinished);
        QCOMPARE(latest(app->received(), QStringLiteral("radio"),
                        QStringLiteral("levelCalPercent")).toInteger(), 100);
        QVERIFY(core.model->levelCalSucceeded());
        // The run measured and put the receiver back.
        QVERIFY(fake.meterReads > 0);
        QCOMPARE(fake.vfoHz, 14200000.0);
        QCOMPARE(fake.mode, DSPMode::USB);
    }

    // A window signed in with the pairing token cannot start it.
    void tokenSession_refused()
    {
        Core core(/*upgradedWithToken=*/true);
        FakeHost fake;
        QVERIFY(fakeReceiver(core, fake) != nullptr);
        LoopbackTransport* token = core.tokenSignIn(withLevelCal());
        QVERIFY(admitted(token));
        QVERIFY2(OperatorWording::isPlain(kPairedOnly), qPrintable(kPairedOnly));
        const QJsonObject refused = core.invoke(token, "startLevelCalibration", startArgs());
        QVERIFY(!accepted(refused));
        QCOMPARE(reason(refused), kPairedOnly);
        QVERIFY(fake.log.isEmpty());
        QVERIFY(!core.model->levelCalRunning());
        // Cancel only stops a run, so it is taken from anyone.
        QVERIFY(accepted(core.invoke(token, "cancelLevelCalibration")));
    }

    // On the air: the Core's on-air refusal, as for its other radio verbs.
    // State only: nothing is keyed.
    void onAir_refused()
    {
        Core core;
        FakeHost fake;
        QVERIFY(fakeReceiver(core, fake) != nullptr);
        Device a;
        core.pair(a);
        LoopbackTransport* app = core.signIn(a, withLevelCal());
        QVERIFY(admitted(app));
        core.model->transmitModel().setMox(true);
        const QJsonObject refused = core.invoke(app, "startLevelCalibration", startArgs());
        QVERIFY(!accepted(refused));
        QCOMPARE(reason(refused), RadioModel::onAirReason());
        QVERIFY(fake.log.isEmpty());
        core.model->transmitModel().setMox(false);
    }

    // No live radio: the run's own refusal comes back as the result.
    void radioOff_refused()
    {
        Core core;
        FakeHost fake;
        fake.live = false;
        QVERIFY(fakeReceiver(core, fake) != nullptr);
        Device a;
        core.pair(a);
        LoopbackTransport* app = core.signIn(a, withLevelCal());
        QVERIFY(admitted(app));
        const QJsonObject refused = core.invoke(app, "startLevelCalibration", startArgs());
        QVERIFY(!accepted(refused));
        QCOMPARE(reason(refused), kRadioOff);
        QVERIFY(fake.log.isEmpty());
    }

    void unreadableOrUnknownSlice_refused()
    {
        Core core;
        FakeHost fake;
        QVERIFY(fakeReceiver(core, fake) != nullptr);
        Device a;
        core.pair(a);
        LoopbackTransport* app = core.signIn(a, withLevelCal());
        QVERIFY(admitted(app));
        const QJsonObject missing = core.invoke(
            app, "startLevelCalibration", {f64("levelDbm", -50.0), int64("sliceId", 0)});
        QCOMPARE(reason(missing), kUnread);
        const QJsonObject wrongKind = core.invoke(
            app, "startLevelCalibration",
            {int64("levelDbm", -50), f64("frequencyHz", kCentre), int64("sliceId", 0)});
        QCOMPARE(reason(wrongKind), kUnread);
        const QJsonObject cancelArgs =
            core.invoke(app, "cancelLevelCalibration", {int64("sliceId", 0)});
        QCOMPARE(reason(cancelArgs), kUnread);
        QVERIFY2(OperatorWording::isPlain(kNoSlice), qPrintable(kNoSlice));
        const QJsonObject noSlice =
            core.invoke(app, "startLevelCalibration", startArgs(-50.0, kCentre + 1000.0, 7));
        QVERIFY(!accepted(noSlice));
        QCOMPARE(reason(noSlice), kNoSlice);
        QVERIFY(fake.log.isEmpty());
    }

    // Cancel stops the run and puts everything back (Thetis closing the
    // progress window); with nothing running it is accepted and does
    // nothing.
    void cancel_stopsAndRestores()
    {
        Core core;
        FakeHost fake;
        QVERIFY(fakeReceiver(core, fake, 60000) != nullptr);
        Device a;
        core.pair(a);
        LoopbackTransport* app = core.signIn(a, withLevelCal());
        QVERIFY(admitted(app));
        QVERIFY(accepted(core.invoke(app, "cancelLevelCalibration")));
        QVERIFY(accepted(core.invoke(app, "startLevelCalibration", startArgs())));
        QTRY_VERIFY(core.model->levelCalRunning());
        const QJsonObject again = core.invoke(app, "startLevelCalibration", startArgs());
        QVERIFY(!accepted(again));
        QVERIFY(reason(again).contains(QStringLiteral("already")));
        QVERIFY(accepted(core.invoke(app, "cancelLevelCalibration")));
        QTRY_COMPARE(latest(app->received(), QStringLiteral("radio"),
                            QStringLiteral("levelCalMessage")).toString(), kCanceled);
        QCOMPARE(latest(app->received(), QStringLiteral("radio"),
                        QStringLiteral("levelCalRunning")).toBool(true), false);
        QCOMPARE(latest(app->received(), QStringLiteral("radio"),
                        QStringLiteral("levelCalSucceeded")).toBool(true), false);
        QCOMPARE(fake.vfoHz, 14200000.0);
        QCOMPARE(fake.ritOn, true);
        QCOMPARE(fake.ritHz, 250);
        QCOMPARE(fake.phoneBuffer, 4096);
        QCOMPARE(fake.preamp1, PreampMode::Minus20);
    }

    // The progress properties go only to a peer that declared the feature.
    void progress_onlyToADeclaringPeer()
    {
        Core core;
        Device a;
        core.pair(a);
        LoopbackTransport* app = core.signIn(a, kHolder);
        QVERIFY(admitted(app));
        // The snapshot reached this peer, without the run's properties.
        QVERIFY(!app->received().isEmpty());
        for (const QByteArray& wire : app->received()) {
            QVERIFY2(!wire.contains("\"levelCal"), wire.constData());
        }
    }

    // A local window's calls run on its own model.
    void localModel_runsItsOwnService()
    {
        RadioModel model;
        QVERIFY(model.levelCalibrationRunAvailable());
        // No radio connected: the run's own refusal, synchronously.
        QCOMPARE(model.requestStartLevelCalibration(-50.0f, kCentre, -1),
                 QStringLiteral("Open a slice before calibrating the receive level."));
        model.addSlice(QStringLiteral("pan-0"));
        QCOMPARE(model.requestStartLevelCalibration(-50.0f, kCentre, -1), kRadioOff);
        QVERIFY(!model.levelCalRunning());
        // A remote-only apply is refused on a local model.
        QVERIFY(!model.applyStationLevelCalValue("levelCalRunning", true));
    }

    // A remote window: the same calls go to its Core, the progress comes
    // back, and a refusal the Core sends later arrives as a signal.
    void remoteWindow_roundTrip()
    {
        Core core(/*upgradedWithToken=*/true);
        core.server->setTokenSessionsMayChangeRadioForTest(true);
        // The Core's step attenuator, so it offers its radio's hardware
        // verbs (StationServer::radioHardwareVersion).
        StepAttenuatorController stepAtt;
        core.model->setStepAttController(&stepAtt);
        const auto unbind = qScopeGuard([&core]() { core.model->setStepAttController(nullptr); });
        FakeHost fake;
        QVERIFY(fakeReceiver(core, fake) != nullptr);

        RadioModel remote(RadioModel::Role::Remote);
        QVERIFY(!remote.levelCalibrationRunAvailable());
        QCOMPARE(remote.requestStartLevelCalibration(-50.0f, kCentre + 1000.0, 0),
                 QStringLiteral("Not connected to a Core, so the level calibration was not sent."));
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        QSignalSpy completed(&client, &StationClient::handshakeComplete);
        auto* stationEnd = new LoopbackTransport(QStringLiteral("cal-station"), this);
        auto* clientEnd = new LoopbackTransport(QStringLiteral("cal-client"), this);
        stationEnd->linkTo(clientEnd);
        client.startSession(clientEnd, core.server->token());
        core.server->acceptTransport(stationEnd);
        QTRY_COMPARE(completed.count(), 1);
        QTRY_VERIFY(remote.levelCalibrationRunAvailable());

        QSignalSpy state(&remote, &RadioModel::levelCalStateChanged);
        QCOMPARE(remote.requestStartLevelCalibration(-50.0f, kCentre + 1000.0, 0), QString());
        QTRY_VERIFY(remote.levelCalSucceeded());
        QCOMPARE(remote.levelCalMessage(), kFinished);
        QCOMPARE(remote.levelCalPercent(), 100);
        QVERIFY(!remote.levelCalRunning());
        QVERIFY(state.count() > 0);

        // The Core refuses after the window sent it.
        fake.live = false;
        QSignalSpy refused(&remote, &RadioModel::levelCalibrationRefused);
        QCOMPARE(remote.requestStartLevelCalibration(-50.0f, kCentre + 1000.0, 0), QString());
        QTRY_COMPARE(refused.count(), 1);
        QCOMPARE(refused.first().at(0).toString(), kRadioOff);
        QVERIFY(remote.requestCancelLevelCalibration().isEmpty());

        // The session ends: nothing is known until the next one.
        remote.clearStationLevelCal();
        QVERIFY(!remote.levelCalSucceeded());
        QVERIFY(remote.levelCalMessage().isEmpty());
        QCOMPARE(remote.levelCalPercent(), 0);
    }
};

QTEST_GUILESS_MAIN(TstLevelCalibrationStation)
#include "tst_level_calibration_station.moc"
