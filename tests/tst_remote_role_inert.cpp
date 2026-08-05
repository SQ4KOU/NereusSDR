// no-port-check: NereusSDR-original unit-test file. No Thetis logic is
// ported here; this exercises NereusSDR's own role/station-link seam
// (remote-daemon R2 Task 4).
// =================================================================
// tests/tst_remote_role_inert.cpp  (NereusSDR)
// =================================================================
//
// Remote-daemon R2 Task 4 -- RadioModel::Role, the station-link seam, and
// the remote-inert guard.
//
// A Role::Remote RadioModel is the shape a GUI-only process needs: no
// RadioConnection, no live WdspEngine channels, no running AudioEngine,
// because DSP and hardware I/O live in a daemon it talks to over the
// wire (R2 Task 18's wss session) instead of locally. The mechanism that
// makes that true is a single early return at the top of
// RadioModel::connectToRadio() (RadioModel.cpp) that fires when
// role() == Role::Remote, before RadioConnection creation, before
// WdspEngine::initialize(), before AudioEngine::start(), and before
// wireConnectionSignals() (which is what would otherwise construct
// RxDspWorker and wire every slice's WDSP connects) is ever called.
//
// The test below drives BOTH a Role::Local and a Role::Remote model
// through connectToRadio() and checks the same four things on each,
// asserting opposite outcomes. The Local arm is not a formality: a
// freshly constructed RadioModel already satisfies every assertion this
// test makes about the Remote model, because the constructor allocates
// AudioEngine and WdspEngine unconditionally (RadioModel.cpp) but starts
// neither. Without the Local arm actually reaching
// ConnectionState::Connected with all four things live, this test would
// pass against an empty/no-op implementation of the guard and prove
// nothing about it. See task-4-controller-notes.md.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-04 -- New test file for remote-daemon R2 Task 4. J.J. Boyd
//                 (KG4VCF), with AI-assisted implementation via Anthropic
//                 Claude Code.
// =================================================================

#include <QtTest/QtTest>

#include <memory>

#include "core/AudioEngine.h"
#include "core/ConnectionState.h"
#include "core/WdspEngine.h"
#include "core/session/IStationLink.h"
#include "fakes/ConnectableRadioModel.h"
#include "models/RadioModel.h"

using namespace NereusSDR;
using NereusSDR::Test::ConnectableRadioModel;

namespace {

// Trivial concrete IStationLink so attachStation()/detachStation() have
// something non-null to hold. IStationLink itself declares no methods
// yet (see the header comment), so there is nothing to override.
class NullStationLink : public NereusSDR::IStationLink {
public:
    ~NullStationLink() override = default;
};

} // namespace

class TestRemoteRoleInert : public QObject {
    Q_OBJECT

private slots:
    // Pins that every existing construction site (MainWindow.cpp,
    // DaemonApp.cpp, and every test that writes plain `RadioModel model;`
    // or `new RadioModel(parent)`) keeps resolving to Role::Local without
    // having to change a single call site. Cheap and standalone --
    // doesn't need the ConnectableRadioModel harness at all.
    void defaultConstructorDefaultsToLocalRole()
    {
        RadioModel model;
        QCOMPARE(model.role(), RadioModel::Role::Local);

        // The new two-arg overload, called with Role::Local explicitly and
        // no parent -- exercises the constructor overload itself (distinct
        // code path from the one-arg constructor above) rather than
        // re-testing the same default a second time. Brace-init, not
        // parens: `RadioModel explicitLocal(RadioModel::Role::Local);`
        // parses as a most-vexing-parse function declaration instead of a
        // variable definition.
        RadioModel explicitLocal{RadioModel::Role::Local};
        QCOMPARE(explicitLocal.role(), RadioModel::Role::Local);
    }

    // The Local arm. Must reach Connected with a live RadioConnection, a
    // live RX WDSP channel, and a running AudioEngine -- the "all four
    // live" side of the brief's assertion pair. If this arm regressed
    // (e.g. the guard fired for Local too, or fired unconditionally),
    // ConnectableRadioModel::create() would time out and return nullptr,
    // and the QVERIFY below would catch it directly.
    void localReachesConnectedWithAllFourLive()
    {
        std::unique_ptr<ConnectableRadioModel> harness = ConnectableRadioModel::create();
        QVERIFY(harness != nullptr);

        RadioModel& model = harness->model();
        QCOMPARE(model.role(), RadioModel::Role::Local);
        QCOMPARE(model.connectionState(), ConnectionState::Connected);
        QVERIFY(model.connection() != nullptr);
        QVERIFY(model.wdspEngine()->rxChannel(0) != nullptr);
        QVERIFY(model.audioEngine()->isRunning());

        // Explicit teardown under this test's watch, same pattern
        // tst_connected_state_equivalence.cpp uses.
        harness.reset();
    }

    // The Remote arm. connectToRadio() must return inert: no
    // RadioConnection, connectionState() stuck at Disconnected, no RX
    // WDSP channel at any index, AudioEngine never started. Uses the
    // Task 2 harness extended with a Role parameter (this task), so both
    // arms of this test exercise the identical fake-radio wiring and
    // differ only in role -- the harness's create() call recognizes
    // Role::Remote and returns immediately after connectToRadio() rather
    // than waiting for a Connected transition that a correctly-guarded
    // Remote model will never reach.
    void remoteConnectToRadioIsInert()
    {
        std::unique_ptr<ConnectableRadioModel> harness =
            ConnectableRadioModel::create(10000, RadioModel::Role::Remote);
        QVERIFY(harness != nullptr);

        RadioModel& model = harness->model();
        QCOMPARE(model.role(), RadioModel::Role::Remote);
        QCOMPARE(model.connectionState(), ConnectionState::Disconnected);
        QVERIFY(model.connection() == nullptr);
        for (int n = 0; n < 8; ++n) {
            QVERIFY(model.wdspEngine()->rxChannel(n) == nullptr);
        }
        QVERIFY(!model.audioEngine()->isRunning());

        harness.reset();
    }

    // attachStation()/detachStation() hold a non-owning pointer the same
    // way m_spectrumSink does: settable, clearable, and safe to leave
    // attached across the model's destruction (RadioModel never deletes
    // it). Not exercised by connectToRadio() at all in this task -- that
    // wiring is a later task's job -- so this only pins that the seam
    // itself is present and inert.
    void attachAndDetachStationDoesNotOwnOrCrash()
    {
        RadioModel model{RadioModel::Role::Remote};
        NullStationLink link;

        model.attachStation(&link);
        model.detachStation();
        // Re-attach and let `model` be destroyed (end of scope) with the
        // link still attached, proving RadioModel does not delete it.
        model.attachStation(&link);
    }
};

QTEST_MAIN(TestRemoteRoleInert)
#include "tst_remote_role_inert.moc"
