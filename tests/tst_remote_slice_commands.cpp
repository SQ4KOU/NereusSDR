// =================================================================
// tests/tst_remote_slice_commands.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test infrastructure.
//
// Remote Daemon R2: the GUI's slice controls reach the daemon.
//
// Task 11 built the command verbs, task 18 built the session that can
// carry them, task 20 gated the GUI, and no task connected the three:
// StationClient::invokeCommand() had ZERO callers, and the GUI's slice
// controls went on calling RadioModel directly. On a Role::Remote model
// that produced a silent, PERMANENT divergence rather than an error --
// setActiveSliceById() flipped SliceModel::active locally, that property
// is Outbound in MirrorPolicy so the mirror correctly refused to send it,
// nothing changed on the daemon, and with nothing changed there was no
// corrective delta to put it back.
//
// ── WHY THESE ASSERTIONS ARE END TO END, NOT MOCK-SHAPED ─────────────────
//
// Every slot below stands up a REAL StationServer over a real
// SessionCommandDispatcher and a REAL StationClient, joined by the
// in-process LoopbackTransport that tst_station_session.cpp and
// tst_session_link_loss.cpp already use, and then asserts on the DAEMON's
// own RadioModel. "The client called a function" is exactly the assertion
// that would have passed while this defect shipped. "The daemon's active
// slice moved" is not.
//
// ── THE ROUND TRIP IS ASYNCHRONOUS, AND THAT IS THE POINT ────────────────
//
// LoopbackTransport::sendText() delivers through a QUEUED invocation, on
// a later turn of the event loop, deliberately ("Queued, not direct. A
// real socket never delivers inside the send call" -- LoopbackTransport.
// cpp). Property deltas are queued a second time behind StationServer::
// kDefaultDeltaFlushMs, a 50 ms coalescing timer. So every slot here that
// follows a command through to the daemon is written in two halves:
//
//   1. Assert IMMEDIATELY after the client call returns, before the event
//      loop has been given a chance to run. Nothing may have moved on
//      either side yet. This is the no-optimism proof, and it is only
//      possible BECAUSE delivery is queued -- a synchronous transport
//      could not tell an optimistic local flip apart from a completed
//      round trip.
//   2. Then QTRY_* for the daemon acting on it and the answer coming back
//      through the mirror.
//
// An earlier draft of this file asserted step 2 synchronously, on the
// stated premise that the loopback delivered inside the send call. That
// premise was wrong about this fake, and the resulting assertions failed
// against a correct implementation.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-09  J.J. Boyd / KG4VCF  Remote daemon R2: route the GUI's
//                                    slice-mutating entry points through
//                                    IStationLink. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
// =================================================================

#include <QtTest/QtTest>

#include <QSignalSpy>
#include <QTemporaryDir>

#include <memory>

#include "core/AppSettings.h"
#include "core/ConnectionState.h"
#include "core/session/IStationLink.h"
#include "core/session/SessionMessages.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/settings/SettingsProxy.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include "fakes/LoopbackTransport.h"

using namespace NereusSDR;
using NereusSDR::Test::LoopbackTransport;

namespace {

/// A daemon-side RadioModel that reports Connected against a real board
/// without a socket. Same seam tst_session_link_loss.cpp's own
/// makeStationRadioModel() uses (file-local there, so reproduced here
/// rather than shared); connectToRadio() is unusable from a test.
///
/// HermesLite deliberately: BoardCapabilities gives it maxSlices == 5
/// while its userDdcCount is smaller, so the daemon has real headroom for
/// the add-slice slots without any stream pool being configured.
std::unique_ptr<RadioModel> makeStationRadioModel(int extraSlices)
{
    auto model = std::make_unique<RadioModel>();
    model->setBoardForTest(HPSDRHW::HermesLite);
    RadioInfo info;
    info.macAddress = QStringLiteral("AA:BB:CC:DD:EE:02");
    info.name = QStringLiteral("Bench HL2");
    info.boardType = HPSDRHW::HermesLite;
    model->setLastRadioInfoForTest(info);
    model->setConnectionStateForTest(ConnectionState::Connected);
    model->addSlice(QStringLiteral("pan-0"));
    for (int i = 0; i < extraSlices; ++i) {
        model->addSlice(QStringLiteral("pan-0"));
    }
    return model;
}

/// Both ends of one established session, kept alive together for the
/// lifetime of a slot. Assembled exactly the way tst_session_link_loss.cpp
/// assembles its own: a real StationServer, a real StationClient, and the
/// non-TLS in-process transport pair, so no slot here depends on a working
/// Qt TLS backend.
struct SessionFixture {
    QTemporaryDir settingsDir;
    QTemporaryDir securityDir;
    std::unique_ptr<AppSettings> stationSettings;
    std::unique_ptr<RadioModel> stationModel;
    std::unique_ptr<StationServer> server;
    std::unique_ptr<RadioModel> clientModel;
    std::unique_ptr<SettingsProxy> proxy;
    std::unique_ptr<StationClient> client;
};

/// `extraSlices` is on top of the one every RadioModel gets, so 0 means a
/// single-slice station and 1 means two.
std::unique_ptr<SessionFixture> establishSession(int extraSlices)
{
    auto fixture = std::make_unique<SessionFixture>();
    if (!fixture->settingsDir.isValid() || !fixture->securityDir.isValid()) {
        return nullptr;
    }

    fixture->stationSettings = std::make_unique<AppSettings>(
        fixture->settingsDir.filePath(QStringLiteral("NereusSDR.settings")));
    fixture->stationModel = makeStationRadioModel(extraSlices);
    fixture->server = std::make_unique<StationServer>(
        fixture->stationModel.get(), *fixture->stationSettings,
        fixture->securityDir.path());

    fixture->clientModel = std::make_unique<RadioModel>(RadioModel::Role::Remote);
    fixture->proxy = std::make_unique<SettingsProxy>();
    fixture->client = std::make_unique<StationClient>(fixture->clientModel.get(),
                                                      fixture->proxy.get());

    auto* stationEnd = new LoopbackTransport(QStringLiteral("station"));
    auto* clientEnd = new LoopbackTransport(QStringLiteral("client"));
    stationEnd->linkTo(clientEnd);

    QSignalSpy completed(fixture->client.get(), &StationClient::handshakeComplete);
    fixture->client->startSession(clientEnd, fixture->server->token());
    fixture->server->acceptTransport(stationEnd);
    if (!completed.wait(5000) && completed.isEmpty()) {
        return nullptr;
    }
    return fixture;
}

/// An IStationLink that only counts. Used by the local-direct-mode slot
/// to prove a Role::Local model never reaches for the link even when one
/// is attached -- an assertion no amount of watching the local mutations
/// succeed can make on its own.
class CountingLink : public NereusSDR::IStationLink {
public:
    int calls = 0;

    CommandOutcome requestAddSlice(const QString&) override { return count(); }
    CommandOutcome requestAddSliceOnPan(const QString&) override { return count(); }
    CommandOutcome requestRemoveSlice(int) override { return count(); }
    CommandOutcome requestActiveSlice(int) override { return count(); }
    CommandOutcome requestSliceSampleRate(int, int) override { return count(); }

private:
    CommandOutcome count()
    {
        ++calls;
        return CommandOutcome{ true, QString() };
    }
};

QList<int> sliceIds(const RadioModel& model)
{
    QList<int> ids;
    for (SliceModel* slice : model.slices()) {
        if (slice != nullptr) {
            ids.append(slice->sliceIndex());
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

} // namespace

class TstRemoteSliceCommands : public QObject {
    Q_OBJECT

private slots:
    // ---- The click reaches the daemon ----
    void remoteActiveSliceRequestMovesTheDaemonsActiveSliceAndTheClientFollowsViaTheMirror();
    void remoteAddSliceCreatesTheSliceOnTheDaemonAndTheClientAdoptsTheStationsId();
    void remoteRemoveSliceRemovesItOnTheDaemon();
    void remoteSampleRateRequestReachesTheDaemon();

    // ---- A refusal is visible, and nothing was optimistically flipped ----
    void stationRefusalReachesAnOperatorFacingSignalAndChangesNothingLocally();
    void stationRefusedSampleRateReachesSliceRetuneRejected();

    // ---- No link at all ----
    void withNoStationLinkEverySliceCommandRefusesInsteadOfMutatingLocally();

    // ---- The inbound half still composes ----
    void aSliceTheDaemonDestroysIsStillRemovedOnTheClient();

    // ---- Local direct mode ----
    void localDirectModeStillMutatesLocallyAndIgnoresAnyAttachedLink();
};

// ─────────────────────────────────────────────────────────────────────────
// The click reaches the daemon
// ─────────────────────────────────────────────────────────────────────────

void TstRemoteSliceCommands::
    remoteActiveSliceRequestMovesTheDaemonsActiveSliceAndTheClientFollowsViaTheMirror()
{
    auto fixture = establishSession(1);
    QVERIFY(fixture != nullptr);
    RadioModel& station = *fixture->stationModel;
    RadioModel& client = *fixture->clientModel;

    QCOMPARE(station.slices().size(), 2);
    QCOMPARE(client.slices().size(), 2);

    const int firstId = station.slices().first()->sliceIndex();
    const int secondId = station.slices().at(1)->sliceIndex();
    QVERIFY(firstId != secondId);

    // The station starts with its first slice active, and so does the
    // client (both by RadioModel::addSlice's own first-slice rule, and on
    // the client confirmed by the snapshot).
    QCOMPARE(station.activeSlice(), station.sliceById(firstId));
    QVERIFY(client.sliceById(firstId)->isActive());
    QVERIFY(!client.sliceById(secondId)->isActive());

    // THE DEFECT. Before this change the call below flipped
    // SliceModel::active on the CLIENT and stopped there.
    QVERIFY(client.setActiveSliceById(secondId));

    // NO OPTIMISM. The command has been handed to the transport and not
    // yet delivered (see this file's header): the click must not have
    // moved anything on this client on its way out. An implementation
    // that flipped locally and ALSO sent would pass every assertion below
    // and still be the divergence this round closes, because the local
    // flip is what survives when the station refuses.
    QVERIFY(client.sliceById(firstId)->isActive());
    QVERIFY(!client.sliceById(secondId)->isActive());
    QCOMPARE(client.activeSlice(), client.sliceById(firstId));
    QCOMPARE(station.activeSlice(), station.sliceById(firstId));

    // The daemon moved. This is the assertion the whole round exists for.
    QTRY_COMPARE(station.activeSlice(), station.sliceById(secondId));
    QVERIFY(station.sliceById(secondId)->isActive());
    QVERIFY(!station.sliceById(firstId)->isActive());

    // ...and the client's own view followed, through the mirror. Both the
    // per-slice flag AND RadioModel::activeSlice(), which is what every
    // active-slice GUI surface actually reads.
    QTRY_VERIFY(client.sliceById(secondId)->isActive());
    QTRY_VERIFY(!client.sliceById(firstId)->isActive());
    QTRY_COMPARE(client.activeSlice(), client.sliceById(secondId));
}

void TstRemoteSliceCommands::
    remoteAddSliceCreatesTheSliceOnTheDaemonAndTheClientAdoptsTheStationsId()
{
    auto fixture = establishSession(0);
    QVERIFY(fixture != nullptr);
    RadioModel& station = *fixture->stationModel;
    RadioModel& client = *fixture->clientModel;

    QCOMPARE(station.slices().size(), 1);
    QCOMPARE(client.slices().size(), 1);
    // Headroom on the daemon, so a refusal here would be a real failure
    // rather than the cap doing its job.
    QVERIFY(station.maxSlices() > 1);

    client.addSliceOnPan(QStringLiteral("pan-0"));

    // NO OPTIMISM, and for add-slice this is the sharper half of the two:
    // a client-minted slice would carry a client-minted id, and the
    // station's next real slice would collide with it.
    QCOMPARE(client.slices().size(), 1);
    QCOMPARE(station.slices().size(), 1);

    // THE DEFECT. Before this change the client minted a slice of its own
    // and the station stayed at one.
    QTRY_COMPARE(station.slices().size(), 2);
    QTRY_COMPARE(client.slices().size(), 2);

    // No client-local orphan and no duplicate: the client holds exactly
    // the station's id set, which is what makes every later verb address
    // the slice the operator is looking at.
    QCOMPARE(sliceIds(client), sliceIds(station));
}

void TstRemoteSliceCommands::remoteRemoveSliceRemovesItOnTheDaemon()
{
    auto fixture = establishSession(1);
    QVERIFY(fixture != nullptr);
    RadioModel& station = *fixture->stationModel;
    RadioModel& client = *fixture->clientModel;

    QCOMPARE(station.slices().size(), 2);
    const int victimId = station.slices().at(1)->sliceIndex();
    QVERIFY(client.sliceById(victimId) != nullptr);

    client.removeSlice(victimId);

    // NO OPTIMISM. Taking the flag off screen here would leave the daemon
    // still demodulating the slice, with no delta to correct it: the
    // station's own object.destroy is what removes it.
    QCOMPARE(client.slices().size(), 2);
    QCOMPARE(station.slices().size(), 2);

    QTRY_COMPARE(station.slices().size(), 1);
    QVERIFY(station.sliceById(victimId) == nullptr);
    QTRY_COMPARE(client.slices().size(), 1);
    QVERIFY(client.sliceById(victimId) == nullptr);
    QCOMPARE(sliceIds(client), sliceIds(station));
}

void TstRemoteSliceCommands::remoteSampleRateRequestReachesTheDaemon()
{
    auto fixture = establishSession(0);
    QVERIFY(fixture != nullptr);
    RadioModel& client = *fixture->clientModel;
    const int sliceId = client.slices().first()->sliceIndex();

    // The daemon here has no stream pool (nothing calls configureStreamPool
    // without connectToRadio), so RadioModel::requestSliceSampleRate takes
    // its own "not bound to a DDC yet" early return and the dispatcher
    // reports an accepted no-op. That is the point: what is under test is
    // that the verb LEFT the client and was acted on, and the accepted
    // result is the daemon's own answer rather than anything this client
    // decided. The refusal direction is covered by
    // stationRefusedSampleRateReachesSliceRetuneRejected below.
    QSignalSpy results(fixture->client.get(), &StationClient::commandResult);
    QSignalSpy retuneRejected(&client, &RadioModel::sliceRetuneRejected);

    client.requestSliceSampleRate(sliceId, 192000);

    // requestSliceSampleRate is the one verb SessionCommandDispatcher
    // defers to a later turn of the daemon's event loop (it can reach
    // setSampleRateLive, which blocks its caller for tens of
    // milliseconds), so the result cannot be inspected inline.
    QVERIFY(results.wait(5000) || !results.isEmpty());
    QCOMPARE(results.count(), 1);
    QCOMPARE(results.first().at(1).toBool(), true);
    QCOMPARE(retuneRejected.count(), 0);
}

// ─────────────────────────────────────────────────────────────────────────
// A refusal is visible, and nothing was optimistically flipped
// ─────────────────────────────────────────────────────────────────────────

void TstRemoteSliceCommands::
    stationRefusalReachesAnOperatorFacingSignalAndChangesNothingLocally()
{
    auto fixture = establishSession(0);
    QVERIFY(fixture != nullptr);
    RadioModel& station = *fixture->stationModel;
    RadioModel& client = *fixture->clientModel;

    QCOMPARE(station.slices().size(), 1);
    const int onlyId = client.slices().first()->sliceIndex();

    // The daemon refuses to remove the last remaining slice, with that
    // exact reason (SessionCommandDispatcher::handleRemoveSlice). The
    // wording is deliberately one that exists ONLY on the daemon: the
    // client is not allowed to pre-check the same rule and answer in its
    // own words, or this assertion would pass with nothing relayed.
    QSignalSpy rejected(&client, &RadioModel::sliceAddRejected);
    client.removeSlice(onlyId);

    // Nothing refused yet -- the command has not even been delivered.
    QCOMPARE(rejected.count(), 0);

    QTRY_COMPARE(rejected.count(), 1);
    QVERIFY2(rejected.first().first().toString().contains(
                 QStringLiteral("last remaining slice")),
             qPrintable(QStringLiteral("the station's own reason did not reach "
                                       "the operator; got: ")
                        + rejected.first().first().toString()));
    QCOMPARE(client.slices().size(), 1);
    QCOMPARE(station.slices().size(), 1);

    // An active-slice request for an id the station does not have is
    // refused, and NOTHING moved on the client -- which is what proves the
    // click is not optimistically applied before the station has spoken.
    //
    // Refused synchronously, and legitimately so: this is the ONE thing a
    // remote client may answer on its own, because its mirrored slice
    // list is a faithful copy of the station's and setActiveSliceById
    // owes its caller a bool. Everything else on this path defers.
    QSignalSpy rejectedAgain(&client, &RadioModel::sliceAddRejected);
    QVERIFY(client.sliceById(onlyId)->isActive());
    QVERIFY(!client.setActiveSliceById(4242));
    QCOMPARE(rejectedAgain.count(), 1);
    QVERIFY(client.sliceById(onlyId)->isActive());
    QCOMPARE(client.activeSlice(), client.sliceById(onlyId));
    QCOMPARE(station.activeSlice(), station.sliceById(onlyId));

    // ...and still nothing after the loop has had every chance to deliver
    // something. 250 ms is five StationServer::kDefaultDeltaFlushMs ticks,
    // so a command that HAD gone out and been acted on would have landed
    // and come back well inside it.
    QTest::qWait(250);
    QCOMPARE(rejectedAgain.count(), 1);
    QCOMPARE(client.slices().size(), 1);
    QCOMPARE(station.slices().size(), 1);
    QCOMPARE(client.activeSlice(), client.sliceById(onlyId));
    QCOMPARE(station.activeSlice(), station.sliceById(onlyId));
}

void TstRemoteSliceCommands::stationRefusedSampleRateReachesSliceRetuneRejected()
{
    auto fixture = establishSession(0);
    QVERIFY(fixture != nullptr);
    RadioModel& client = *fixture->clientModel;

    QSignalSpy retuneRejected(&client, &RadioModel::sliceRetuneRejected);
    client.requestSliceSampleRate(4242, 192000);

    QVERIFY(retuneRejected.wait(5000) || !retuneRejected.isEmpty());
    QCOMPARE(retuneRejected.count(), 1);
    QCOMPARE(retuneRejected.first().at(0).toInt(), 4242);
    QVERIFY2(retuneRejected.first().at(1).toString().contains(
                 QStringLiteral("no such slice")),
             qPrintable(QStringLiteral("the station's own reason did not reach "
                                       "the operator; got: ")
                        + retuneRejected.first().at(1).toString()));
}

// ─────────────────────────────────────────────────────────────────────────
// No link at all
// ─────────────────────────────────────────────────────────────────────────

void TstRemoteSliceCommands::
    withNoStationLinkEverySliceCommandRefusesInsteadOfMutatingLocally()
{
    // No StationClient, so RadioModel::attachStation was never called.
    // This is the state a remote GUI is in before its first handshake and
    // after a link loss, and it is precisely where a silent local mutation
    // would produce a slice the station has never heard of.
    RadioModel client(RadioModel::Role::Remote);
    client.addSliceWithStationId(0);
    QCOMPARE(client.slices().size(), 1);
    const int onlyId = client.slices().first()->sliceIndex();

    QSignalSpy rejected(&client, &RadioModel::sliceAddRejected);
    QSignalSpy retuneRejected(&client, &RadioModel::sliceRetuneRejected);

    QCOMPARE(client.addSlice(QStringLiteral("pan-0")), -1);
    QCOMPARE(client.slices().size(), 1);

    client.addSliceOnPan(QStringLiteral("pan-0"));
    QCOMPARE(client.slices().size(), 1);

    client.removeSlice(onlyId);
    QCOMPARE(client.slices().size(), 1);

    QVERIFY(!client.setActiveSliceById(onlyId));

    QCOMPARE(rejected.count(), 4);
    for (const QList<QVariant>& emission : rejected) {
        QVERIFY2(!emission.first().toString().isEmpty(),
                 "a refusal reached the operator with no reason in it");
    }

    client.requestSliceSampleRate(onlyId, 192000);
    QCOMPARE(retuneRejected.count(), 1);
    QCOMPARE(retuneRejected.first().at(0).toInt(), onlyId);
}

// ─────────────────────────────────────────────────────────────────────────
// The inbound half still composes
// ─────────────────────────────────────────────────────────────────────────

void TstRemoteSliceCommands::aSliceTheDaemonDestroysIsStillRemovedOnTheClient()
{
    // The outbound guard added for this defect must not swallow the
    // station's OWN destroy: StationClient::handleObjectDestroy() reaches
    // RadioModel to remove a slice on a Role::Remote model, and routing
    // that back out as a removeSlice verb would bounce the daemon's answer
    // straight back at it.
    auto fixture = establishSession(1);
    QVERIFY(fixture != nullptr);
    RadioModel& station = *fixture->stationModel;
    RadioModel& client = *fixture->clientModel;

    QCOMPARE(client.slices().size(), 2);
    const int victimId = station.slices().at(1)->sliceIndex();

    // A bounced destroy would arrive at the station as a removeSlice
    // command for a slice it has already removed, and come back refused
    // ("no such slice") on the client's operator-facing channel. Zero
    // emissions is what says the guard is a re-point and not a loop.
    QSignalSpy rejected(&client, &RadioModel::sliceAddRejected);

    // Removed at the STATION, by the station's own operator, with no
    // command from this client at all.
    station.removeSlice(victimId);

    QTRY_COMPARE(client.slices().size(), 1);
    QVERIFY(client.sliceById(victimId) == nullptr);
    QCOMPARE(station.slices().size(), 1);

    QTest::qWait(250);
    QCOMPARE(rejected.count(), 0);
    QCOMPARE(station.slices().size(), 1);
    QCOMPARE(client.slices().size(), 1);
}

// ─────────────────────────────────────────────────────────────────────────
// Local direct mode
// ─────────────────────────────────────────────────────────────────────────

void TstRemoteSliceCommands::localDirectModeStillMutatesLocallyAndIgnoresAnyAttachedLink()
{
    // A Role::Local model is what every existing construction site builds
    // (MainWindow in direct mode, DaemonApp, and every other test), and
    // none of this round may reach it.
    auto station = makeStationRadioModel(1);
    QCOMPARE(station->role(), RadioModel::Role::Local);
    QCOMPARE(station->slices().size(), 2);

    // A link IS attached, which is what makes this slot say what its name
    // says. Without it the assertions below hold for a model that has no
    // link to consult, and would keep holding even if the Role::Local
    // path had been routed by mistake -- so the gate is CountingLink's
    // own tally, not just the local mutations succeeding.
    //
    // This is not a hypothetical arrangement: StationClient attaches
    // itself in its constructor and warns rather than refuses when handed
    // a Role::Local model, so "a Local model with a link on it" is a
    // state a misconfigured GUI really can reach.
    CountingLink link;
    station->attachStation(&link);

    const int firstId = station->slices().first()->sliceIndex();
    const int secondId = station->slices().at(1)->sliceIndex();

    QSignalSpy rejected(station.get(), &RadioModel::sliceAddRejected);

    QVERIFY(station->setActiveSliceById(secondId));
    QCOMPARE(station->activeSlice(), station->sliceById(secondId));
    QVERIFY(station->sliceById(secondId)->isActive());
    QVERIFY(!station->sliceById(firstId)->isActive());

    const int addedId = station->addSlice(QStringLiteral("pan-0"));
    QVERIFY(addedId >= 0);
    QCOMPARE(station->slices().size(), 3);

    station->addSliceOnPan(QStringLiteral("pan-1"));
    QCOMPARE(station->slices().size(), 4);

    station->removeSlice(addedId);
    QCOMPARE(station->slices().size(), 3);
    QVERIFY(station->sliceById(addedId) == nullptr);

    station->requestSliceSampleRate(firstId, 192000);

    QCOMPARE(rejected.count(), 0);

    // Not one of the five verbs left this model. Direct mode does not
    // consult the link at all, which is the whole contract.
    QCOMPARE(link.calls, 0);

    station->detachStation();
}

QTEST_MAIN(TstRemoteSliceCommands)
#include "tst_remote_slice_commands.moc"
