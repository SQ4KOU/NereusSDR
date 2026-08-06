// =================================================================
// tests/tst_session_verbs.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test infrastructure.
//
// Remote Daemon R2 Task 11: command verbs and results. Exercises the
// SessionMessages CommandInvoke/CommandResult codec plus a real
// SessionCommandDispatcher round trip over fakes/LoopbackStationLink.h.
// There is no transport until Task 18, so "over the wire" here means the
// codec and dispatch, not a socket -- LoopbackStationLink is a plain
// in-process byte relay standing in for it.
//
// Central case (R2 plan Task 11 step 1): with A(0) B(1) C(2), removing B
// -- dispatched as a real removeSlice command over the loopback -- leaves
// ids and positions diverged (C sits at id 2, position 1).
// RadioModel::setActiveSliceById(2) must still select slice C, not
// whatever (if anything) sits at position 2. A test that never makes ids
// and positions diverge would pass whether resolution used one or the
// other; this one would not, because position 2 does not exist any more.
//
// Also covers: addSlice / removeSlice / addSliceOnPan accept and reject
// paths, relaying RadioModel's own rejection reason rather than inventing
// one; requestSliceSampleRate deferred to a LATER RadioModel event-loop
// turn (this is the one verb that can reach RadioModel::setSampleRateLive's
// >= 40 ms of QThread::msleep, so dispatch() must never block on it); the
// CommandResult's affectedKeys reporting the ACTUAL scope of a rate
// change rather than the requested one, for slices co-hosting one DDC
// stream (no live connection needed) and for a Protocol 1 board's
// radio-wide RadioModel::setSampleRateLive escalation (a real HermesLite
// connection via fakes/ConnectableRadioModel.h); the new
// RadioModel::activeSliceIdChanged(int) signal firing the resolved slice
// id beside the existing positional activeSliceChanged, without disturbing
// it; and StateMirror::applyInbound()'s m_applying guard surviving a
// genuinely NESTED apply -- the Task 8 defect Task 11's command dispatch
// makes reachable (StateMirror.cpp's ApplyingGuard).
// =================================================================

#include <QtTest/QtTest>
#include <QByteArray>
#include <QList>
#include <QSignalSpy>
#include <QVariant>

#include <memory>

#include "core/session/MirrorSchema.h"
#include "core/session/ObjectRegistry.h"
#include "core/session/SessionCommandDispatcher.h"
#include "core/session/SessionMessages.h"
#include "core/session/StateMirror.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include "fakes/ConnectableRadioModel.h"
#include "fakes/LoopbackStationLink.h"

using namespace NereusSDR;
using NereusSDR::Test::ConnectableRadioModel;
using NereusSDR::Test::LoopbackStationLink;

namespace {

MirrorUpdate intArg(const QByteArray& name, qint64 value)
{
    return MirrorUpdate{ 0, name, MirrorWireKind::Int64, QVariant(value) };
}

MirrorUpdate strArg(const QByteArray& name, const QString& value)
{
    return MirrorUpdate{ 0, name, MirrorWireKind::Utf8, QVariant(value) };
}

// Flattens every batch StateMirror emits, same shape as
// tst_mirror_forwarder.cpp / tst_mirror_inbound.cpp's own Collector
// (separate translation unit, so its own copy -- see either file's header
// comment for why this is not shared).
class Collector : public QObject {
public:
    explicit Collector(StateMirror* mirror)
    {
        connect(mirror, &StateMirror::propertiesChanged, this,
                [this](const QByteArray& key, const QList<MirrorUpdate>& ups) {
                    for (const MirrorUpdate& u : ups) {
                        flat.append(qMakePair(key, u));
                    }
                });
    }

    bool sawKey(const QByteArray& key) const
    {
        for (const auto& e : flat) {
            if (e.first == key) { return true; }
        }
        return false;
    }

    QList<QPair<QByteArray, MirrorUpdate>> flat;
};

// Wires a SessionCommandDispatcher to both ends of a LoopbackStationLink,
// exactly the shape Task 18's real session will have on the daemon side
// (decode inbound bytes, dispatch, encode+send whatever
// commandResultReady() produces) plus a client-side decode step so a test
// can inspect results without hand-rolling the codec calls at every call
// site.
class DispatchHarness : public QObject {
public:
    explicit DispatchHarness(RadioModel* radioModel, QObject* parent = nullptr)
        : QObject(parent)
        , dispatcher(radioModel)
    {
        connect(&link, &LoopbackStationLink::receivedByDaemon, this,
                [this](const QByteArray& wire) {
                    SessionMessage msg;
                    if (SessionMessages::decode(wire, &msg)) {
                        dispatcher.dispatch(msg);
                    }
                });
        connect(&dispatcher, &SessionCommandDispatcher::commandResultReady, this,
                [this](const SessionMessage& result) {
                    link.sendFromDaemon(SessionMessages::encode(result));
                });
        connect(&link, &LoopbackStationLink::receivedByClient, this,
                [this](const QByteArray& wire) {
                    SessionMessage msg;
                    if (SessionMessages::decode(wire, &msg)) {
                        results.append(msg);
                    }
                });
    }

    void invoke(const QByteArray& verb, quint32 commandId, const QList<MirrorUpdate>& arguments)
    {
        link.sendFromClient(
            SessionMessages::encode(SessionMessages::commandInvoke(verb, commandId, arguments)));
    }

    LoopbackStationLink link;
    SessionCommandDispatcher dispatcher;
    QList<SessionMessage> results;
};

} // namespace

class TestSessionVerbs : public QObject {
    Q_OBJECT

private slots:

    // ── Codec ────────────────────────────────────────────────────────────

    void commandMessagesRoundTripThroughJson()
    {
        {
            const SessionMessage m = SessionMessages::commandInvoke(
                "requestSliceSampleRate", 42,
                { intArg("sliceId", 2), intArg("rateHz", 384000) });
            SessionMessage back;
            QVERIFY(SessionMessages::decode(SessionMessages::encode(m), &back));
            QCOMPARE(back.kind, SessionMessageKind::CommandInvoke);
            QCOMPARE(back.commandVerb, QByteArray("requestSliceSampleRate"));
            QCOMPARE(back.commandId, quint32(42));
            QCOMPARE(back.arguments.size(), 2);
            QCOMPARE(back.arguments.at(0).name, QByteArray("sliceId"));
            QCOMPARE(back.arguments.at(0).value.toLongLong(), qint64(2));
            QCOMPARE(back.arguments.at(1).name, QByteArray("rateHz"));
            QCOMPARE(back.arguments.at(1).value.toLongLong(), qint64(384000));
        }
        {
            const SessionMessage m = SessionMessages::commandInvoke(
                "addSliceOnPan", 7, { strArg("panId", QStringLiteral("pan-b")) });
            SessionMessage back;
            QVERIFY(SessionMessages::decode(SessionMessages::encode(m), &back));
            QCOMPARE(back.arguments.first().value.toString(), QStringLiteral("pan-b"));
        }
        {
            const SessionMessage m = SessionMessages::commandResult(
                "requestSliceSampleRate", 42, true, QString(),
                { QByteArray("slice:0"), QByteArray("slice:1") });
            SessionMessage back;
            QVERIFY(SessionMessages::decode(SessionMessages::encode(m), &back));
            QCOMPARE(back.kind, SessionMessageKind::CommandResult);
            QCOMPARE(back.commandVerb, QByteArray("requestSliceSampleRate"));
            QCOMPARE(back.commandId, quint32(42));
            QVERIFY(back.accepted);
            QVERIFY(back.reason.isEmpty());
            QCOMPARE(back.affectedKeys, (QList<QByteArray>{ "slice:0", "slice:1" }));
        }
        {
            const SessionMessage m = SessionMessages::commandResult(
                "removeSlice", 9, false, QStringLiteral("no such slice"), {});
            SessionMessage back;
            QVERIFY(SessionMessages::decode(SessionMessages::encode(m), &back));
            QVERIFY(!back.accepted);
            QCOMPARE(back.reason, QStringLiteral("no such slice"));
            QVERIFY(back.affectedKeys.isEmpty());
        }
    }

    void decodeRejectsMalformedCommandMessages()
    {
        SessionMessage out;

        // CommandInvoke: missing "verb", missing "id", missing "args".
        QVERIFY2(!SessionMessages::decode(QByteArray(R"({"type":"command.invoke"})"), &out),
                 "command.invoke with no verb at all must be rejected");
        QVERIFY2(!SessionMessages::decode(
                     QByteArray(R"({"type":"command.invoke","verb":"addSlice"})"), &out),
                 "command.invoke with no id must be rejected");
        QVERIFY2(!SessionMessages::decode(
                     QByteArray(R"({"type":"command.invoke","verb":"addSlice","id":1})"), &out),
                 "command.invoke with no args array at all must be rejected");
        QVERIFY2(!SessionMessages::decode(
                     QByteArray(R"({"type":"command.invoke","verb":"addSlice","id":"not a number","args":[]})"),
                     &out),
                 "command.invoke with a non-numeric id must be rejected");

        // CommandResult: missing "verb"/"id"/"accepted"/"reason"/"affected".
        QVERIFY2(!SessionMessages::decode(QByteArray(R"({"type":"command.result"})"), &out),
                 "command.result with no verb at all must be rejected");
        QVERIFY2(!SessionMessages::decode(
                     QByteArray(R"({"type":"command.result","verb":"addSlice","id":1})"), &out),
                 "command.result with no accepted flag must be rejected");
        QVERIFY2(!SessionMessages::decode(
                     QByteArray(
                         R"({"type":"command.result","verb":"addSlice","id":1,"accepted":true})"),
                     &out),
                 "command.result with no reason string at all must be rejected");
        QVERIFY2(!SessionMessages::decode(
                     QByteArray(
                         R"({"type":"command.result","verb":"addSlice","id":1,"accepted":true,"reason":""})"),
                     &out),
                 "command.result with no affected array at all must be rejected");
        QVERIFY2(!SessionMessages::decode(
                     QByteArray(
                         R"({"type":"command.result","verb":"addSlice","id":1,"accepted":"yes","reason":"","affected":[]})"),
                     &out),
                 "a non-bool accepted value must be rejected");

        // Well-formed shell, still rejected: a non-string entry in
        // "affected" -- same reject-rather-than-coerce discipline as every
        // other array element this codec decodes.
        QVERIFY2(!SessionMessages::decode(
                     QByteArray(
                         R"({"type":"command.result","verb":"addSlice","id":1,"accepted":true,"reason":"","affected":[42]})"),
                     &out),
                 "a non-string affected-key entry must be rejected");
    }

    // ── Step 1: the central id-vs-position divergence test ──────────────

    void removingASliceOverTheWireDivergesIdsFromPositionsAndSetActiveSliceByIdPicksTheId()
    {
        RadioModel model;
        model.configureStreamPool(5, 5, 192000);
        DispatchHarness harness(&model);

        // A(0) B(1) C(2).
        const int a = model.addSlice();
        const int b = model.addSlice();
        const int c = model.addSlice();
        QCOMPARE(a, 0);
        QCOMPARE(b, 1);
        QCOMPARE(c, 2);

        // Remove B THROUGH THE WIRE: encode a command.invoke, hand it to
        // the daemon side of the loopback, and confirm a command.result
        // comes back out the client side.
        harness.invoke("removeSlice", 1, { intArg("sliceId", b) });
        QCOMPARE(harness.results.size(), 1);
        QVERIFY2(harness.results.first().accepted, qPrintable(harness.results.first().reason));
        QCOMPARE(harness.results.first().affectedKeys,
                 (QList<QByteArray>{ ObjectRegistry::keyForSlice(b) }));

        // Ids and positions have now diverged: C sits at id 2, position 1.
        QCOMPARE(model.slices().size(), 2);
        QCOMPARE(model.slices().indexOf(model.sliceById(c)), 1);
        QVERIFY(model.sliceById(b) == nullptr);

        // The actual discriminator: ask for id 2 (C), not position 2
        // (which does not exist -- the list only has 2 entries now). A
        // test that never makes ids and positions diverge would pass
        // whether this resolved by id or by position; this one would not.
        QVERIFY(model.setActiveSliceById(c));
        QVERIFY2(model.activeSlice() == model.sliceById(c),
                 "setActiveSliceById(2) must select slice C (the object "
                 "whose id is 2), not whatever sits at position 2 (nothing) "
                 "or position 0 (A)");
    }

    // ── addSlice ─────────────────────────────────────────────────────────

    void addSliceCommandCreatesAndReportsKeyOrRelaysRejectionReason()
    {
        RadioModel model;
        model.configureStreamPool(1, 5, 192000); // exactly one DDC
        DispatchHarness harness(&model);

        harness.invoke("addSlice", 1, { strArg("initialPanId", QStringLiteral("pan-a")) });
        QCOMPARE(harness.results.size(), 1);
        QVERIFY2(harness.results.at(0).accepted, qPrintable(harness.results.at(0).reason));
        QCOMPARE(harness.results.at(0).commandVerb, QByteArray("addSlice"));
        QCOMPARE(harness.results.at(0).commandId, quint32(1));
        QCOMPARE(harness.results.at(0).affectedKeys,
                 (QList<QByteArray>{ ObjectRegistry::keyForSlice(0) }));
        QVERIFY(model.sliceById(0) != nullptr);

        // A second, differently-panned add: the allocator has no room left
        // (RadioModel.cpp's bindSliceToStream rejection, the same path
        // tst_mirror_lifecycle.cpp's bindPathRejectionProducesNoObjectCreate
        // exercises).
        harness.invoke("addSlice", 2, { strArg("initialPanId", QStringLiteral("pan-b")) });
        QCOMPARE(harness.results.size(), 2);
        QVERIFY(!harness.results.at(1).accepted);
        QVERIFY2(!harness.results.at(1).reason.isEmpty(),
                 "must relay the real allocator rejection reason, not a generic one");
        QVERIFY(harness.results.at(1).affectedKeys.isEmpty());
        QVERIFY(model.sliceById(1) == nullptr); // nothing was created
    }

    // ── removeSlice ──────────────────────────────────────────────────────

    void removeSliceCommandRemovesReportsKeyAndRejectsInvalidRequests()
    {
        RadioModel model;
        model.configureStreamPool(5, 5, 192000);
        DispatchHarness harness(&model);

        const int a = model.addSlice();
        const int b = model.addSlice();

        harness.invoke("removeSlice", 1, { intArg("sliceId", 999) }); // unknown id
        QCOMPARE(harness.results.size(), 1);
        QVERIFY(!harness.results.at(0).accepted);
        QVERIFY(harness.results.at(0).affectedKeys.isEmpty());

        harness.invoke("removeSlice", 2, { intArg("sliceId", b) });
        QCOMPARE(harness.results.size(), 2);
        QVERIFY2(harness.results.at(1).accepted, qPrintable(harness.results.at(1).reason));
        QCOMPARE(harness.results.at(1).affectedKeys,
                 (QList<QByteArray>{ ObjectRegistry::keyForSlice(b) }));
        QVERIFY(model.sliceById(b) == nullptr);

        // RadioModel::removeSlice() (RadioModel.cpp) silently no-ops
        // rather than take the last remaining slice; the dispatcher must
        // catch this itself and report a rejection, not a false accept.
        harness.invoke("removeSlice", 3, { intArg("sliceId", a) });
        QCOMPARE(harness.results.size(), 3);
        QVERIFY(!harness.results.at(2).accepted);
        QVERIFY(harness.results.at(2).affectedKeys.isEmpty());
        QVERIFY(model.sliceById(a) != nullptr);
        QCOMPARE(model.slices().size(), 1);
    }

    // ── addSliceOnPan ────────────────────────────────────────────────────

    void addSliceOnPanCommandCreatesOrRelaysCapRejection()
    {
        RadioModel model; // disconnected: maxSlices() == 1
        DispatchHarness harness(&model);

        harness.invoke("addSliceOnPan", 1, { strArg("panId", QStringLiteral("pan-a")) });
        QCOMPARE(harness.results.size(), 1);
        QVERIFY2(harness.results.at(0).accepted, qPrintable(harness.results.at(0).reason));
        QCOMPARE(harness.results.at(0).affectedKeys,
                 (QList<QByteArray>{ ObjectRegistry::keyForSlice(0) }));

        // addSliceOnPan()'s own cap check, refusing before addSlice() is
        // even entered -- the same path tst_mirror_lifecycle.cpp's
        // addSliceOnPanCapRejectionProducesNoObjectCreate exercises.
        harness.invoke("addSliceOnPan", 2, { strArg("panId", QStringLiteral("pan-b")) });
        QCOMPARE(harness.results.size(), 2);
        QVERIFY(!harness.results.at(1).accepted);
        QVERIFY2(!harness.results.at(1).reason.isEmpty(),
                 "must relay the real cap-rejection reason");
        QVERIFY(harness.results.at(1).affectedKeys.isEmpty());
    }

    // ── requestSliceSampleRate ───────────────────────────────────────────

    void requestSliceSampleRateRejectsUnknownSliceIdSynchronously()
    {
        RadioModel model;
        model.configureStreamPool(5, 5, 192000);
        DispatchHarness harness(&model);

        harness.invoke("requestSliceSampleRate", 1,
                       { intArg("sliceId", 999), intArg("rateHz", 384000) });

        // Unlike a well-formed request (see below), an unknown slice id is
        // rejected BEFORE the deferred RadioModel call is even queued, so
        // this must complete synchronously.
        QCOMPARE(harness.results.size(), 1);
        QVERIFY(!harness.results.at(0).accepted);
        QVERIFY(harness.results.at(0).affectedKeys.isEmpty());
    }

    void requestSliceSampleRateIsDeferredAndReportsActualScopeAcrossCoHostedSlices()
    {
        RadioModel model;
        model.configureStreamPool(1, 5, 192000); // exactly one DDC -- forces co-hosting
        DispatchHarness harness(&model);

        // Three unpanned adds all share the SAME stream by default
        // (tst_stream_pool_binding.cpp's own fixture, "no pan id" ==
        // "join whatever the empty-pan slot already has").
        const int a = model.addSlice();
        const int b = model.addSlice();
        const int c = model.addSlice();
        QCOMPARE(model.sliceById(b)->streamIndex(), model.sliceById(a)->streamIndex());
        QCOMPARE(model.sliceById(c)->streamIndex(), model.sliceById(a)->streamIndex());
        QCOMPARE(model.sliceById(a)->sampleRateHz(), 192000); // configureStreamPool's default

        harness.invoke("requestSliceSampleRate", 7,
                       { intArg("sliceId", a), intArg("rateHz", 768000) });

        // Must NOT complete synchronously: this is the one verb Task 11
        // defers to a later RadioModel event-loop turn specifically so it
        // cannot stall the session read path once it reaches
        // RadioModel::setSampleRateLive's QThread::msleep calls.
        QCOMPARE(harness.results.size(), 0);

        QTRY_COMPARE(harness.results.size(), 1);
        const SessionMessage result = harness.results.first();
        QVERIFY2(result.accepted, qPrintable(result.reason));
        QCOMPARE(result.commandVerb, QByteArray("requestSliceSampleRate"));
        QCOMPARE(result.commandId, quint32(7));

        // Actual scope: all THREE co-hosted slices moved, not only the one
        // named in the request.
        QCOMPARE(model.sliceById(a)->sampleRateHz(), 768000);
        QCOMPARE(model.sliceById(b)->sampleRateHz(), 768000);
        QCOMPARE(model.sliceById(c)->sampleRateHz(), 768000);
        QCOMPARE(result.affectedKeys.size(), 3);
        QVERIFY(result.affectedKeys.contains(ObjectRegistry::keyForSlice(a)));
        QVERIFY(result.affectedKeys.contains(ObjectRegistry::keyForSlice(b)));
        QVERIFY(result.affectedKeys.contains(ObjectRegistry::keyForSlice(c)));
    }

    // Heavier variant of the above: a REAL Protocol 1 connection
    // (fakes/ConnectableRadioModel.h, a HermesLite over a loopback
    // P1FakeRadio), where requestSliceSampleRate can escalate all the way
    // to RadioModel::setSampleRateLive's radio-wide 12-step sequence
    // (RadioModel.cpp: requestSliceSampleRate -> setStreamSampleRate ->
    // sampleRateIsRadioWide() -> setSampleRateLive). Two slices on
    // DIFFERENT streams -- so co-hosting (the test above) cannot explain
    // the result -- both moving proves this is genuinely the radio-wide
    // path, not the stream-sharing one.
    void requestSliceSampleRateReportsStationWideScopeOnAProtocol1Board()
    {
        std::unique_ptr<ConnectableRadioModel> harness = ConnectableRadioModel::create();
        QVERIFY(harness != nullptr);
        RadioModel& model = harness->model();
        QVERIFY2(model.sampleRateIsRadioWide(),
                 "test setup: this scenario only proves anything on a "
                 "Protocol 1 (radio-wide sample rate) connection");

        QVERIFY(model.sliceById(0) != nullptr); // Slice A, seeded by connectToRadio()
        const int b = model.addSlice(QStringLiteral("pan-b")); // its OWN, DIFFERENT stream
        QVERIFY(b >= 0);
        QVERIFY2(model.sliceById(b)->streamIndex() != model.sliceById(0)->streamIndex(),
                 "test setup: slice B must be on a stream slice A is NOT on, "
                 "or a co-hosted-stream move could explain the result too");

        const int before = model.sliceById(0)->sampleRateHz();
        const int target = (before == 384000) ? 192000 : 384000; // both valid on HermesLite/P1

        DispatchHarness dispatch(&model);
        dispatch.invoke("requestSliceSampleRate", 11,
                        { intArg("sliceId", b), intArg("rateHz", target) });
        QCOMPARE(dispatch.results.size(), 0); // still deferred

        QTRY_COMPARE_WITH_TIMEOUT(dispatch.results.size(), 1, 10000);
        const SessionMessage result = dispatch.results.first();
        QVERIFY2(result.accepted, qPrintable(result.reason));

        // Station-wide: slice A, on a stream the request never named, must
        // ALSO have moved.
        QCOMPARE(model.sliceById(0)->sampleRateHz(), target);
        QCOMPARE(model.sliceById(b)->sampleRateHz(), target);
        QVERIFY(result.affectedKeys.contains(ObjectRegistry::keyForSlice(0)));
        QVERIFY(result.affectedKeys.contains(ObjectRegistry::keyForSlice(b)));
        QCOMPARE(result.affectedKeys.size(), 2);

        harness.reset();
    }

    void commandsRejectMissingArgumentsAndUnrecognisedVerbs()
    {
        RadioModel model;
        model.configureStreamPool(5, 5, 192000);
        DispatchHarness harness(&model);

        harness.invoke("addSlice", 1, {});                                    // no initialPanId
        harness.invoke("removeSlice", 2, {});                                 // no sliceId
        harness.invoke("requestSliceSampleRate", 3, { intArg("sliceId", 0) }); // no rateHz
        harness.invoke("addSliceOnPan", 4, {});                               // no panId
        harness.invoke("bogusVerb", 5, {});                                   // unrecognised

        QCOMPARE(harness.results.size(), 5);
        for (const SessionMessage& r : harness.results) {
            QVERIFY2(!r.accepted, qPrintable(r.commandVerb));
            QVERIFY2(!r.reason.isEmpty(), qPrintable(r.commandVerb));
            QVERIFY(r.affectedKeys.isEmpty());
        }
    }

    // ── activeSliceIdChanged ──────────────────────────────────────────────

    void activeSliceIdChangedFiresTheResolvedIdBesideThePositionalSignal()
    {
        RadioModel model;
        model.configureStreamPool(5, 5, 192000);

        QSignalSpy positional(&model, &RadioModel::activeSliceChanged);
        QSignalSpy byId(&model, &RadioModel::activeSliceIdChanged);

        const int a = model.addSlice();
        const int b = model.addSlice();
        model.addSlice();
        QVERIFY(!positional.isEmpty());
        QVERIFY(!byId.isEmpty());
        positional.clear();
        byId.clear();

        // Remove A (currently active): the fallback becomes whichever now
        // sits at position 0 -- B, carrying id 1. The positional signal
        // reports 0 (a raw list position); the id-based one must report 1
        // (an object identity), proving it resolves through the slice
        // rather than re-emitting the same number on both channels.
        model.removeSlice(a);

        QVERIFY(!positional.isEmpty());
        QVERIFY(!byId.isEmpty());
        QCOMPARE(positional.last().at(0).toInt(), 0);
        QCOMPARE(byId.last().at(0).toInt(), b);
        QVERIFY2(b != 0,
                 "the discriminator only works if the resolved id and the "
                 "raw position differ -- b must not be 0");

        // The positional signal is unaffected: existing GUI code binding
        // to it still gets exactly the position it always did.
        QCOMPARE(model.activeSlice(), model.sliceById(b));
    }

    // ── StateMirror nested-apply guard (the inherited defect) ────────────

    // Task 8's review flagged StateMirror's m_applying guard as Minor
    // because nothing could re-enter applyInbound() while one was already
    // in flight. Task 11's command dispatch is what makes that reachable
    // (a command handler running a RadioModel call whose synchronous side
    // effects lead back into the mirror), so this test constructs the
    // reentrancy directly against StateMirror rather than through the full
    // dispatcher stack -- what matters is that SOME same-thread,
    // synchronous path re-enters applyInbound() while an outer one is
    // still on the call stack, and StateMirror has no way to tell that
    // apart from any other cause.
    void nestedApplyInboundDoesNotLeakTheOuterCallsRemainingDeltas()
    {
        SliceModel sliceA(0);
        SliceModel sliceB(1);
        StateMirror mirror;
        QVERIFY(mirror.watch("slice:0", &sliceA));
        QVERIFY(mirror.watch("slice:1", &sliceB));
        Collector c(&mirror);

        // Baselines, confirmed different from what this test writes below
        // -- SliceModel's setters are uniformly change-guarded, so a value
        // that does not genuinely differ from what is already there
        // produces no NOTIFY at all, which would make every assertion
        // below pass whether or not the guard works (tst_mirror_inbound.
        // cpp's own discipline).
        QVERIFY(sliceA.frequency() != 21050000.0);
        QVERIFY(sliceA.afGain() != 77);
        QVERIFY(sliceB.rfGain() != 5);
        QVERIFY(sliceA.rfGain() != 41);

        // The reentrancy trigger: reacting to slice A's OWN frequency
        // notify -- fired synchronously, from inside the OUTER
        // applyInbound() call's own watch.schema->write() -- by (1)
        // running a SECOND, NESTED applyInbound() call against a
        // DIFFERENT watched object (slice B), simulating a command
        // handler whose RadioModel call leads back into the mirror, then
        // (2), still synchronously inside the same handler, writing
        // ANOTHER property on the SAME object (slice A) the outer call is
        // still in the middle of applying. That second write is the
        // discriminator: it happens strictly AFTER the nested call has
        // returned but strictly BEFORE the outer applyInbound() call
        // itself does.
        bool nestedReturned = false;
        connect(&sliceA, &SliceModel::frequencyChanged, &mirror, [&](double) {
            const MirrorApplyResult inner = mirror.applyInbound("slice:1", "rfGain", QVariant(5));
            QVERIFY(inner.accepted);
            nestedReturned = true;
            sliceA.setAfGain(77);
        });

        const MirrorApplyResult outer =
            mirror.applyInbound("slice:0", "frequency", QVariant(21050000.0));

        QVERIFY2(outer.accepted, qPrintable(outer.reason));
        QVERIFY(nestedReturned);
        QCOMPARE(sliceA.frequency(), 21050000.0);
        QCOMPARE(sliceB.rfGain(), 5);
        QCOMPARE(sliceA.afGain(), 77);

        // The actual assertion this test exists for. Pre-fix, m_applying
        // was set true then unconditionally cleared to false around each
        // write -- the nested call's own cleanup would clear it while the
        // OUTER call was still unwinding (still inside sliceA's own
        // frequencyChanged handler, itself inside the outer write()), so
        // sliceA.setAfGain(77)'s afGainChanged notify would stop being
        // suppressed and leak out as an ordinary delta. A saved/restored
        // guard keeps m_applying true for the whole nested cascade, so
        // NOTHING here may have reached the wire.
        QVERIFY2(c.flat.isEmpty(), "no delta may leak while a nested apply is still unwinding");

        // And the guard must not stick afterward, matching tst_mirror_
        // inbound.cpp's own guardDoesNotOutliveTheApplyInboundCallThatSetIt:
        // a genuinely local change now (nothing nested, no apply in
        // flight) must forward normally.
        sliceA.setRfGain(41);
        QVERIFY2(c.sawKey("slice:0"), "forwarding must resume once the OUTER apply has returned");
    }
};

QTEST_MAIN(TestSessionVerbs)
#include "tst_session_verbs.moc"
