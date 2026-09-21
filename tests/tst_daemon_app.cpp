// tests/tst_daemon_app.cpp
//
// R1 Task 10 -- DaemonApp connects a headless nereusd process to a radio
// and creates min(cfg.sliceCount, connected-board-maxSlices) slices,
// using RadioModel's own addSlice() rather than the GUI-only
// addSliceOnPan(). Before this class existed, a headless daemon that
// called RadioModel::connectToRadio() got Slice A and nothing else --
// every other slice-creation call site is wired from MainWindow.
//
// This test primes the RadioModel via DaemonApp::primeBoardForTest(),
// NOT a real RadioModel::connectToRadio() round trip (whether against
// real hardware or a P1FakeRadio loopback fake). Found while writing
// this test: RadioModel::connectToRadio() contains a synchronous nested
// QEventLoop that blocks the calling thread until WdspEngine finishes
// generating FFTW wisdom (RadioModel.cpp: "Block here while the wisdom
// worker finishes, pumping the Qt event loop") -- deliberate, existing
// behaviour (it is how the GUI shows a wisdom progress dialog on a cold
// first connect; see CLAUDE.md's "First run generates FFTW wisdom
// (~15 min)"), not something this task introduces. Measured directly
// while writing this test: over 5 minutes on a cold cache, at which
// point QtTest's own per-function watchdog aborted the process rather
// than connectToRadio() ever returning within a usable test budget. No
// test anywhere in this suite calls RadioModel::connectToRadio() for
// exactly this reason (grep tests/*.cpp for ".connectToRadio(" --
// every hit goes through P1RadioConnection/P2RadioConnection directly,
// never through RadioModel). Every RadioModel-level test instead primes
// board state via RadioModel::setBoardForTest() + configureStreamPool(),
// e.g. tst_p1_hl2_rx2_wiring.cpp's second_live_stream_enables_rx2_end_to_end();
// primeBoardForTest() puts DaemonApp through the identical two calls.
//
// The "connects to the radio" half of DaemonApp::start() (production
// discovery + RadioModel::connectToRadio()) is exercised by manual
// verification instead (see task-10-report.md), not by an automated
// test, for the same reason.
//
// Uses QTEST_MAIN (not APPLESS_MAIN): RadioModel's construction touches
// Qt machinery (timers, WdspEngine, AudioEngine) that wants a
// QCoreApplication, matching every other RadioModel-constructing test.
//
// Modification history (NereusSDR):
//   2026-09-20: cover DaemonApp's RadioModel teardown state relay,
//               by J.J. Boyd (KG4VCF), with AI-assisted implementation
//               via OpenAI Codex.

#include <QtTest/QtTest>

#include <QHostAddress>
#include <QScopeGuard>
#include <QSslSocket>
#include <QTcpServer>

#include <utility>

#include "core/HpsdrModel.h"
#include "core/MoxController.h"
#include "core/StepAttenuatorController.h"
#define private public
#include "core/daemon/DaemonApp.h"
#undef private
#include "core/daemon/DaemonConfig.h"
#include "core/session/StationServer.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

class TstDaemonApp : public QObject {
    Q_OBJECT
private slots:
    // The whole point of the task: a headless start must create the
    // configured number of slices, not just Slice A. HermesLite's
    // BoardCapabilities row (BoardCapabilities.cpp kHermesLite) sets
    // maxSlices = 5, so 3 is well within the SKU's real capacity and
    // must come back exactly, not clamped.
    void createsConfiguredSliceCount()
    {
        DaemonConfig cfg = DaemonConfig::defaults();
        cfg.sliceCount = 3;

        DaemonApp app;
        app.primeBoardForTest(HPSDRHW::HermesLite);

        QVERIFY(app.start(cfg));
        QCOMPARE(app.sliceCount(), 3);

        app.stop();
    }

    // Fix round 1, Finding 1: mintFftEndpoints() populating m_topology
    // is not enough by itself -- it has to reach RadioModel's own live
    // FFTRouter (RadioModel::fftRouter()) via FftTopology::applyTo(),
    // the same way MainWindow::rebuildFftRouting() ends with
    // m_topology.applyTo(*router). Before the fix, m_topology was
    // subscribed to but never pushed anywhere, so the router never knew
    // about any of it.
    //
    // A fresh DaemonApp's endpoint-id counter starts at 0, so with
    // sliceCount = 1 the single slice created gets "daemon-ep-0" and
    // binds to stream 0 (the allocator's first placement for a slice
    // with no existing occupant to share with).
    void fftRouterReflectsSubscriptions()
    {
        DaemonConfig cfg = DaemonConfig::defaults();
        cfg.sliceCount = 1;

        DaemonApp app;
        app.primeBoardForTest(HPSDRHW::HermesLite);

        QVERIFY(app.start(cfg));
        QCOMPARE(app.sliceCount(), 1);

        const QList<int> mapped =
            app.fftRouterMappingsForTest(QStringLiteral("daemon-ep-0"));
        QCOMPARE(mapped.size(), 1);
        QCOMPARE(mapped.first(), 0);

        // Fix round 2, Finding 1 (reopened): exercise stop()'s ACTUAL
        // FFT-topology teardown step (clearFftTopologyForTest() runs the
        // exact same clearFftTopology() stop() calls) WHILE the
        // RadioModel and its FFTRouter are still alive, so this
        // assertion can actually fail if the removal regresses. Querying
        // only AFTER a full stop() cannot tell "the router was cleared"
        // apart from "the router no longer exists" -- stop() destroys
        // the RadioModel, and the FFTRouter Qt-parented to it, in the
        // very next step -- which is exactly how the round 1 fix's
        // broken stop() (m_topology = FftTopology{}; before
        // publishFftTopology(), which silently discarded
        // FftTopology's own record of what it had last pushed and so
        // removed nothing) passed this same test unnoticed.
        app.clearFftTopologyForTest();
        QVERIFY(app.fftRouterMappingsForTest(QStringLiteral("daemon-ep-0")).isEmpty());

        app.stop();

        // The RadioModel (and the FFTRouter it owned) is gone after the
        // full stop(); the observable contract is that DaemonApp reports
        // no mapping for anything, rather than a test reaching into a
        // dangling router pointer.
        QVERIFY(app.fftRouterMappingsForTest(QStringLiteral("daemon-ep-0")).isEmpty());
    }

    // HermesLite's maxSlices is 5 -- the request must clamp DOWN to the
    // board's real capability, not silently create 99 slices.
    void clampsSliceCountToBoardCapability()
    {
        DaemonConfig cfg = DaemonConfig::defaults();
        cfg.sliceCount = 99;

        DaemonApp app;
        app.primeBoardForTest(HPSDRHW::HermesLite);

        QVERIFY(app.start(cfg));
        QVERIFY(app.sliceCount() >= 1);
        QVERIFY(app.sliceCount() <= 5);   // no supported SKU exceeds 5

        app.stop();
    }

    void stopIsSafeWithoutStart()
    {
        DaemonApp app;
        app.stop();          // must not crash
        QCOMPARE(app.sliceCount(), 0);
    }

    // DaemonApp owns RadioModel with a unique_ptr. unique_ptr::reset() clears
    // that pointer before it deletes the old object, so a connection-state
    // relay emitted while the model tears down cannot query m_radioModel.
    // This takes the identical ownership shape explicitly, then emits the
    // source signal while the old model is still alive. The relay must use
    // the state argument, which is already the authoritative value.
    void connectionStateRelayDoesNotDereferenceReleasedModel()
    {
        DaemonConfig cfg = DaemonConfig::defaults();

        DaemonApp app;
        app.primeBoardForTest(HPSDRHW::HermesLite);
        QVERIFY(app.start(cfg));

        std::unique_ptr<RadioModel> releasedModel = std::move(app.m_radioModel);
        QVERIFY(releasedModel != nullptr);
        const auto restoreModel = qScopeGuard([&app, &releasedModel]() {
            app.m_radioModel = std::move(releasedModel);
        });

        QSignalSpy connectedSpy(&app, &DaemonApp::radioConnected);
        emit releasedModel->connectionStateChanged(ConnectionState::Disconnected);
        QCOMPARE(connectedSpy.count(), 1);
        QCOMPARE(connectedSpy.takeFirst().at(0).toBool(), false);
    }

    void restartIsClean()
    {
        DaemonConfig cfg = DaemonConfig::defaults();
        cfg.sliceCount = 2;

        DaemonApp app;
        app.primeBoardForTest(HPSDRHW::HermesLite);

        QVERIFY(app.start(cfg));
        app.stop();
        // Carry-forward from the coordinator's dispatch: stop() must
        // leave sliceCount() == 0 BEFORE the next start(), not just
        // "eventually" after it. Checked explicitly rather than only
        // inferred from the post-restart count below.
        QCOMPARE(app.sliceCount(), 0);

        QVERIFY(app.start(cfg));
        QCOMPARE(app.sliceCount(), 2);   // not 4

        app.stop();
    }

    void headlessControllerUsesSaturnDefaultsAndCalibration()
    {
        DaemonConfig cfg = DaemonConfig::defaults();
        cfg.sliceCount = 1;

        DaemonApp app;
        app.primeBoardForTest(HPSDRHW::Saturn,
                              QStringLiteral("02:00:00:00:00:91"));
        QVERIFY(app.start(cfg));

        StepAttenuatorController* const controller =
            app.m_stepAttController.get();
        QVERIFY(controller != nullptr);
        QCOMPARE(app.m_radioModel->stepAttController(), controller);
        QVERIFY(controller->settingsLoaded());

        const auto& caps = app.m_radioModel->boardCapabilities();
        QCOMPARE(controller->minAttenuation(), caps.attenuator.minDb);
        QCOMPARE(controller->maxAttenuation(), caps.attenuator.maxDb);
        QCOMPARE(controller->hasStepAttenuatorCal(),
                 caps.hasStepAttenuatorCal);
        QVERIFY(!controller->isHpsdrBoard());

        // This synthetic MAC has no persisted step-att/preamp keys. Preserve
        // the controller's existing defaults: step ATT enabled at 0 dB.
        // ANAN-G2/Saturn's Thetis factory calibration is -4.476 dB, so the
        // live RadioModel offset must now be that value rather than the
        // preamp-Off branch's +15.524 dB.
        QVERIFY(controller->stepAttEnabled());
        QCOMPARE(controller->attenuatorDb(), 0);
        QCOMPARE(app.m_radioModel->rxMeterOffsetDb(),
                 static_cast<double>(-4.476f));

        app.stop();
        QVERIFY(app.m_stepAttController == nullptr);
    }

    void headlessControllerTracksTxBandModeAndMox()
    {
        DaemonConfig cfg = DaemonConfig::defaults();
        cfg.sliceCount = 1;

        DaemonApp app;
        app.primeBoardForTest(HPSDRHW::Saturn);
        QVERIFY(app.start(cfg));

        StepAttenuatorController* const controller =
            app.m_stepAttController.get();
        SliceModel* const txSlice = app.m_radioModel->txBoundSlice();
        QVERIFY(controller != nullptr);
        QVERIFY(txSlice != nullptr);

        controller->setTxAttenuationForBand(Band::Band80m, 7);
        controller->setTxAttenuationForBand(Band::Band20m, 11);
        txSlice->setFrequency(3'830'000.0);
        txSlice->setDspMode(DSPMode::LSB);
        QCOMPARE(controller->attOnTxValue(), 7);
        QCOMPARE(controller->currentDspMode(), DSPMode::LSB);

        // Prove the daemon owns the desktop-equivalent MOX connection. With
        // PS marked active, the ordinary per-band 7 dB TX value is selected
        // rather than the force-31 safety branch, then RX restores to 0 dB.
        controller->setPsActive(true);
        app.m_radioModel->moxController()->setTimerIntervals(0, 0, 0, 0, 0, 0);
        app.m_radioModel->moxController()->setMox(true);
        QTRY_COMPARE(controller->attenuatorDb(), 7);
        app.m_radioModel->moxController()->setMox(false);
        QTRY_COMPARE(controller->attenuatorDb(), 0);

        app.stop();
    }

    void replacementSliceUsesStableIdForControllerWiring()
    {
        DaemonConfig cfg = DaemonConfig::defaults();
        cfg.sliceCount = 2;

        DaemonApp app;
        app.primeBoardForTest(HPSDRHW::Saturn);
        QVERIFY(app.start(cfg));

        RadioModel* const model = app.m_radioModel.get();
        StepAttenuatorController* const controller =
            app.m_stepAttController.get();
        QVERIFY(model != nullptr);
        QVERIFY(controller != nullptr);
        QVERIFY(model->sliceById(0) != nullptr);
        QVERIFY(model->sliceById(1) != nullptr);

        // Remove A, leaving B at list position 0, then recreate A. sliceAdded
        // carries stable id 0 while the replacement is list position 1. A
        // positional slices.at(0) lookup would silently wire B a second time.
        model->removeSlice(0);
        QVERIFY(model->sliceById(0) == nullptr);
        QCOMPARE(model->addSlice(), 0);
        SliceModel* const replacement = model->sliceById(0);
        QVERIFY(replacement != nullptr);
        QCOMPARE(model->slices().indexOf(replacement), 1);

        QVERIFY(model->requestTxHandoffToSlice(0));
        controller->setTxAttenuationForBand(Band::Band80m, 8);
        replacement->setFrequency(3'830'000.0);
        replacement->setDspMode(DSPMode::LSB);
        QCOMPARE(controller->attOnTxValue(), 8);
        QCOMPARE(controller->currentDspMode(), DSPMode::LSB);

        app.stop();
    }

    // ── Remote Daemon R2 Task 18 ─────────────────────────────────────────
    //
    // Until this landed, NOTHING in the tree constructed a StationServer:
    // grep for it across src/core/daemon, src/main.cpp and
    // src/core/CoreInit.cpp returned nothing, so nereusd never listened
    // and never printed the pairing banner. Task 18 step 4 is phrased as
    // daemon behaviour and its stated purpose is that without it the
    // acceptance run cannot authenticate, so it belongs here rather than
    // to the GUI-gating task.

    void remoteListenerIsNotStartedByDefault()
    {
        // remote_port defaults to 0, which means "do not listen". A
        // default nereusd must therefore open no port at all.
        DaemonConfig cfg = DaemonConfig::defaults();
        QCOMPARE(cfg.remotePort, 0);

        DaemonApp app;
        app.primeBoardForTest(HPSDRHW::HermesLite);
        QVERIFY(app.start(cfg));
        QVERIFY2(app.stationServer() == nullptr,
                 "a default config must not bring up a network listener");
        app.stop();
    }

    void configuredRemotePortBringsUpAListener()
    {
        if (!QSslSocket::supportsSsl()) {
            QSKIP("Qt reports no working TLS backend, so a wss listener cannot bind. "
                  "The listener is wss-only by design (parent design section 10.5).");
        }

        DaemonConfig cfg = DaemonConfig::defaults();
        cfg.remotePort = 0;
        // Port 0 means "disabled" in the config, so an ephemeral port has
        // to be requested explicitly. Bind loopback and let the OS pick by
        // asking for a high port; a fixed port would collide with a
        // parallel ctest shard.
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
        const quint16 freePort = probe.serverPort();
        probe.close();
        cfg.remotePort = static_cast<int>(freePort);
        cfg.remoteBind = QStringLiteral("127.0.0.1");

        DaemonApp app;
        app.primeBoardForTest(HPSDRHW::HermesLite);
        QVERIFY(app.start(cfg));

        StationServer* server = app.stationServer();
        QVERIFY2(server != nullptr, "remote_port was set but no StationServer exists");
        QVERIFY2(server->isListening(), qPrintable(server->lastError()));
        QCOMPARE(server->serverPort(), freePort);

        // Step 4's other half: the pairing material an operator has to
        // carry to the client by hand exists and is non-empty.
        QVERIFY(!server->token().isEmpty());
        QVERIFY(!server->certificateFingerprint().isEmpty());

        // And it is torn down with the daemon rather than outliving the
        // RadioModel its mirror holds QPointers into.
        app.stop();
        QVERIFY(app.stationServer() == nullptr);
    }

    void invalidRemoteBindIsLoggedRatherThanFatal()
    {
        DaemonConfig cfg = DaemonConfig::defaults();
        cfg.remotePort = 4711;
        cfg.remoteBind = QStringLiteral("not-an-address");

        DaemonApp app;
        app.primeBoardForTest(HPSDRHW::HermesLite);
        // A daemon that still demodulates locally is more useful than one
        // that refuses to boot over a mistyped config line.
        QVERIFY(app.start(cfg));
        QVERIFY(app.stationServer() == nullptr);
        QCOMPARE(app.sliceCount(), cfg.sliceCount);
        app.stop();
    }
};

QTEST_MAIN(TstDaemonApp)
#include "tst_daemon_app.moc"
