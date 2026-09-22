// no-port-check: NereusSDR-original unit-test file. No Thetis logic is
// ported here; this exercises NereusSDR's own test-only WdspEngine seam
// (remote-daemon R2 Task 2).
// =================================================================
// tests/tst_connectable_radio_model.cpp  (NereusSDR)
// =================================================================
//
// Remote-daemon R2 Task 2 -- a connectable RadioModel for tests.
//
// RadioModel::connectToRadio() has never been called by any test in this
// suite (see tst_daemon_app.cpp's header comment): on a cold config
// directory -- which tests/TestSandboxInit.cpp forces on every run -- it
// blocks the calling thread inside a QEventLoop until WdspEngine finishes
// generating FFTW wisdom, which takes minutes. Tasks 3, 12 and 20 all need
// a genuinely connected RadioModel (task 12 specifically needs a live
// RxChannel), so this file builds and proves the harness that makes that
// possible inside QtTest's per-function timeout (TIMEOUT 120, set in
// tests/CMakeLists.txt).
//
// Spike (must run before the harness is trusted): confirms that skipping
// WDSPwisdom() does not just move the FFTW planning cost into WDSP's own
// channel-open path and blow the timeout anyway. See
// synchronousInitTimingSpike() below and task-2-report.md for the
// measured number.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-03 -- New test file for remote-daemon R2 Task 2. J.J. Boyd
//                 (KG4VCF), with AI-assisted implementation via Anthropic
//                 Claude Code.
// =================================================================

#include <QtTest/QtTest>
#include <QElapsedTimer>
#include <QStandardPaths>

#include <memory>

#include "core/RxChannel.h"
#include "core/WdspEngine.h"
#include "models/SliceModel.h"
#include "fakes/ConnectableRadioModel.h"

using namespace NereusSDR;
using NereusSDR::Test::ConnectableRadioModel;

class TestConnectableRadioModel : public QObject {
    Q_OBJECT

private slots:
    void reconnectPreservesLiveReceivers()
    {
        auto harness = ConnectableRadioModel::create();
        QVERIFY(harness);
        RadioModel& model = harness->model();
        model.setReceiveOnlyStationPolicy(true);
        SliceModel* const a = model.slices().first();
        SliceModel* const b = model.sliceById(model.addSlice());
        QVERIFY(b);
        a->setFrequency(3865100);
        a->setDspMode(DSPMode::LSB);
        b->setFrequency(14225000);
        b->setDspMode(DSPMode::USB);
        b->setPanKey(QStringLiteral("pan-1"));
        model.setActiveSlice(b->sliceIndex());
        QSignalSpy activeChanges(&model, &RadioModel::activeSliceChanged);
        QSignalSpy removed(&model, &RadioModel::sliceRemoved);

        model.disconnectFromRadio();
        RadioDiscovery::clearHoldOffForTest();
        model.connectToRadioPreservingSlices(harness->radioInfo());
        QTRY_COMPARE_WITH_TIMEOUT(model.connectionState(), ConnectionState::Connected, 10000);
        QCOMPARE(model.slices().size(), 2);
        QCOMPARE(model.slices().at(0), a);
        QCOMPARE(model.slices().at(1), b);
        QCOMPARE(model.activeSlice(), b);
        QCOMPARE(a->frequency(), 3865100.0);
        QCOMPARE(b->frequency(), 14225000.0);
        QCOMPARE(a->dspMode(), DSPMode::LSB);
        QCOMPARE(b->dspMode(), DSPMode::USB);
        QCOMPARE(b->panKey(), QStringLiteral("pan-1"));
        QCOMPARE(removed.count(), 0);
        QCOMPARE(activeChanges.count(), 0);
        QVERIFY(a->streamIndex() >= 0);
        QVERIFY(b->streamIndex() >= 0);
        QVERIFY(!model.mox());

        RadioInfo other = harness->radioInfo();
        other.macAddress = QStringLiteral("bb:bb:cc:11:22:33");
        model.connectToRadioPreservingSlices(other);
        QCOMPARE(model.currentRadioMac(), harness->radioInfo().macAddress);
        QCOMPARE(model.connectionState(), ConnectionState::Connected);
    }

    // Spike: WdspEngine::setSynchronousInitForTest(true) + initialize()
    // must run finishInitialization(false) for real (impulse cache init,
    // PS feedback channel open) and then let the caller open one more RX
    // channel, all without ever calling WDSPwisdom(). Skipping WDSPwisdom()
    // does not remove FFTW's planning work -- WDSP's own filter/channel
    // construction calls fftw_plan_dft_1d(..., FFTW_PATIENT) with no
    // wisdom to draw on (third_party/wdsp/src/fir.c and friends), so the
    // work simply moves to first channel use. This measures that moved
    // cost directly, on a config directory TestSandboxInit.cpp guarantees
    // is cold (no on-disk wisdom file, and this test process has never
    // planned an FFTW transform of any of these sizes before).
    void synchronousInitTimingSpike() {
        WdspEngine engine;
        engine.setSynchronousInitForTest(true);

        const QString configDir =
            QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);

        QElapsedTimer timer;
        timer.start();

        QVERIFY(engine.initialize(configDir));
        QVERIFY(engine.isInitialized());

        RxChannel* rx = engine.createRxChannel(0);

        const qint64 elapsedMs = timer.elapsed();
        QVERIFY(rx != nullptr);

        qInfo("synchronousInitTimingSpike: finishInitialization(false) + "
              "one RX channel took %lld ms", static_cast<long long>(elapsedMs));

        engine.destroyRxChannel(0);
    }

    // Core assertion for the task: RadioModel::connectToRadio() must reach
    // ConnectionState::Connected against a real (fake) radio within
    // QtTest's per-function timeout, using nothing but the
    // ConnectableRadioModel helper that tasks 3, 12 and 20 will also use.
    void radioModelReachesConnectedAgainstFake() {
        std::unique_ptr<ConnectableRadioModel> harness = ConnectableRadioModel::create();
        QVERIFY(harness != nullptr);

        QCOMPARE(harness->model().connectionState(), ConnectionState::Connected);
        // Belt-and-braces: ConnectionState::Connected is RadioConnection's
        // own state-machine output, but confirm the fake agrees the
        // metis-start handshake actually completed -- the same check
        // tst_p1_loopback_connection.cpp makes for a bare P1RadioConnection.
        QVERIFY(harness->fake().isRunning());

        // Step 3: prove the synchronous init seam never spawned the async
        // "WisdomThread" QThread. See WdspEngine::
        // wisdomThreadSpawnedForTest()'s doc comment (WdspEngine.h) for why
        // this flag -- rather than an OS-level thread-table scan, which
        // Qt has no cross-platform API for anyway -- is the correct and
        // sufficient check: WdspEngine.cpp is the only place in the tree
        // that ever constructs a thread with that name, and the flag is
        // sticky (only ever set true, never reset), so this single read,
        // taken immediately before we intentionally tear the harness down
        // below, stands for "true throughout connect AND teardown".
        QVERIFY(!harness->model().wdspEngine()->wisdomThreadSpawnedForTest());

        // Explicit teardown rather than letting `harness` fall out of
        // scope at the end of the slot, so RadioModel::~RadioModel() (via
        // teardownConnection()) and P1FakeRadio's destructor both run here,
        // under this test's watch, rather than implicitly after the last
        // assertion above.
        harness.reset();
    }
};

QTEST_MAIN(TestConnectableRadioModel)
#include "tst_connectable_radio_model.moc"
