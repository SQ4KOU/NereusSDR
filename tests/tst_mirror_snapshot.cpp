// =================================================================
// tests/tst_mirror_snapshot.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test infrastructure.
//
// Remote Daemon R2 Task 10: the connect-time snapshot burst
// (StateMirror::attachSession()) and the outbound coalescer
// (MirrorCoalescer) that keeps a mutation triggered synchronously DURING
// that burst from being observed ahead of the snapshot-complete marker.
//
// The central fact this file rests on: StateMirror::sessionMessageReady()
// for a Delta is emitted ONLY from flushCoalescedDeltas(), which
// attachSession() calls exactly once, as its LAST step. Every other
// watched-object notification during the burst routes into the
// coalescer's dirty set instead (onWatchedPropertyChanged(), once a
// session is attached), so nothing can reach a session sink out of order
// -- the same "one uninterrupted event-loop turn on one thread" guarantee
// Tasks 7-9 already lean on for their own ordering proofs.
//
// Also central: ObjectRegistry attaches to slices only going FORWARD
// (Task 9). Three slices that already exist before StateMirror /
// ObjectRegistry are even constructed become visible only once
// backfillExistingSlices() is called explicitly -- constructing the
// registry AFTER the slices already exist, then calling that method, is
// what the first test below does, matching
// tst_mirror_lifecycle.cpp's own backfill test.
// =================================================================

#include <QtTest/QtTest>
#include <QByteArray>
#include <QList>
#include <QSet>
#include <QSignalSpy>
#include <QVariant>

#include "core/session/MirrorSchema.h"
#include "core/session/ObjectRegistry.h"
#include "core/session/SessionMessages.h"
#include "core/session/StateMirror.h"
#include "models/Band.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

namespace {

// Records every SessionMessage StateMirror::sessionMessageReady() emits,
// in arrival order -- the "recording sink" the task brief asks for.
class Sink : public QObject {
public:
    explicit Sink(StateMirror* mirror)
    {
        connect(mirror, &StateMirror::sessionMessageReady, this,
                [this](const SessionMessage& message) { entries.append(message); });
    }

    int indexOfFirst(SessionMessageKind kind) const
    {
        for (int i = 0; i < entries.size(); ++i) {
            if (entries.at(i).kind == kind) {
                return i;
            }
        }
        return -1;
    }

    int indexOfCreate(const QByteArray& key) const
    {
        for (int i = 0; i < entries.size(); ++i) {
            const SessionMessage& m = entries.at(i);
            if (m.kind == SessionMessageKind::ObjectCreate && m.objectKey == key) {
                return i;
            }
        }
        return -1;
    }

    // First Delta entry naming `property` for `key`, or -1.
    int indexOfDelta(const QByteArray& key, const QByteArray& property) const
    {
        for (int i = 0; i < entries.size(); ++i) {
            const SessionMessage& m = entries.at(i);
            if (m.kind != SessionMessageKind::Delta || m.objectKey != key) {
                continue;
            }
            for (const MirrorUpdate& u : m.updates) {
                if (u.name == property) {
                    return i;
                }
            }
        }
        return -1;
    }

    int countOf(SessionMessageKind kind) const
    {
        int n = 0;
        for (const SessionMessage& m : entries) {
            if (m.kind == kind) {
                ++n;
            }
        }
        return n;
    }

    QList<SessionMessage> entries;
};

} // namespace

class TestMirrorSnapshot : public QObject {
    Q_OBJECT

private slots:

    // ── Step 1: the central ordering test ───────────────────────────────

    // Three slices, already fully created before StateMirror / ObjectRegistry
    // even exist -- exactly the case Task 9's review round closed:
    // backfillExistingSlices() is what makes them visible at all.
    // attachSession() must then produce, in order: one schema message per
    // distinct watched class (here, one: SliceModel), three object.create
    // each with a FULL property bag, the snapshot-complete marker, and
    // ONLY THEN whatever the burst itself caused to become dirty.
    void attachSessionOrdersSchemaCreatesMarkerThenDeltas()
    {
        RadioModel model;
        model.configureStreamPool(5, 5, 192000);

        const int a = model.addSlice(QStringLiteral("pan-a"));
        const int b = model.addSlice(QStringLiteral("pan-b"));
        const int c = model.addSlice(QStringLiteral("pan-c"));
        QVERIFY(a >= 0 && b >= 0 && c >= 0);

        SliceModel* sliceA = model.sliceById(a);
        QVERIFY(sliceA != nullptr);
        // A known, unambiguous starting point -- NOT whatever addSlice()
        // happened to seed -- so the mid-build mutation below can be
        // guaranteed to touch frequency alone (see its own comment).
        sliceA->setFrequency(14200000.0);
        QCOMPARE(sliceA->band(), Band::Band20m);

        // StateMirror + ObjectRegistry constructed AFTER all three slices
        // already exist: the exact ordering Task 9's review round found
        // completely unhandled before backfillExistingSlices() was added.
        StateMirror mirror;
        ObjectRegistry registry(&model, &mirror);
        registry.backfillExistingSlices();

        Sink sink(&mirror);

        // A receiver reacting to one of the burst's OWN messages,
        // synchronously, by writing a watched object -- the "mid-build"
        // case the brief calls out. Mutates slice A -- which the create
        // loop will already have visited before slice B's create fires,
        // since watch order is a < b < c -- to a frequency STILL inside
        // 20m, so this produces exactly one delta (frequency) with no
        // band delta riding along, keeping the index arithmetic below
        // unambiguous.
        bool mutated = false;
        const QMetaObject::Connection midBuildHook = QObject::connect(
            &mirror, &StateMirror::sessionMessageReady, &sink,
            [&](const SessionMessage& message) {
                if (mutated) {
                    return;
                }
                if (message.kind == SessionMessageKind::ObjectCreate
                    && message.objectKey == ObjectRegistry::keyForSlice(b)) {
                    mutated = true;
                    sliceA->setFrequency(14250000.0); // still 20m
                }
            });

        mirror.attachSession();
        QObject::disconnect(midBuildHook);
        QVERIFY2(mutated, "test setup: the mid-build mutation never ran");
        QCOMPARE(sliceA->band(), Band::Band20m); // confirms no band delta rides along

        // N schema messages, COMPUTED rather than hardcoded: one per
        // distinct class among what backfillExistingSlices() watched. All
        // three slices are SliceModel, so N == 1 here; a separate test
        // below proves N generalises past 1.
        QSet<QByteArray> classesWatched;
        for (const QByteArray& key : mirror.watchedKeys()) {
            classesWatched.insert(MirrorSchema::shortClassName(
                MirrorSchema::forObject(mirror.watchedObject(key)).className()));
        }
        const int n = classesWatched.size();
        QCOMPARE(n, 1);

        for (int i = 0; i < n; ++i) {
            QCOMPARE(sink.entries.at(i).kind, SessionMessageKind::Schema);
            QVERIFY(classesWatched.contains(sink.entries.at(i).className));
        }
        QCOMPARE(sink.entries.at(0).className, QByteArray("SliceModel"));

        const MirrorSchema& sliceSchema = MirrorSchema::forObject(sliceA);
        QVERIFY2(!sink.entries.at(0).fields.isEmpty(), "schema must declare fields");
        QCOMPARE(sink.entries.at(0).fields.size(), sliceSchema.size());
        bool sawSliceIndexField = false;
        for (const SessionSchemaField& f : sink.entries.at(0).fields) {
            if (f.name == "sliceIndex") {
                sawSliceIndexField = true;
            }
        }
        QVERIFY2(sawSliceIndexField,
                 "schema must declare CONSTANT properties too (sliceIndex is "
                 "the object identity)");

        const QByteArray keyA = ObjectRegistry::keyForSlice(a);
        const QByteArray keyB = ObjectRegistry::keyForSlice(b);
        const QByteArray keyC = ObjectRegistry::keyForSlice(c);

        const int createA = sink.indexOfCreate(keyA);
        const int createB = sink.indexOfCreate(keyB);
        const int createC = sink.indexOfCreate(keyC);
        QVERIFY(createA >= 0 && createB >= 0 && createC >= 0);
        QVERIFY2(createA == n && createB == n + 1 && createC == n + 2,
                 "the three creates must immediately follow the schema "
                 "block, in watch order");

        for (int idx : { createA, createB, createC }) {
            QCOMPARE(sink.entries.at(idx).updates.size(), sliceSchema.size());
        }

        const int markerIdx = sink.indexOfFirst(SessionMessageKind::SnapshotComplete);
        QVERIFY2(markerIdx == n + 3, "the marker must come immediately after the third create");
        QCOMPARE(sink.countOf(SessionMessageKind::SnapshotComplete), 1);

        // The mid-build mutation: must be present (not lost), and strictly
        // after the marker -- never interleaved into the burst even though
        // it was TRIGGERED synchronously from inside the burst.
        const int deltaIdx = sink.indexOfDelta(keyA, "frequency");
        QVERIFY2(deltaIdx >= 0, "the mid-build frequency change was lost");
        QVERIFY2(deltaIdx > markerIdx, "a mid-build change must not precede the marker");
        QVERIFY2(deltaIdx > createA, "a mid-build change must not precede its own object's create");
        QCOMPARE(sink.entries.at(deltaIdx).updates.first().value.toDouble(), 14250000.0);

        // Nothing after the delta: exactly one property was mutated, so
        // the coalescer had exactly one thing to drain.
        QCOMPARE(sink.entries.size(), deltaIdx + 1);
    }

    // ── N generalises past 1 ─────────────────────────────────────────────

    void attachSessionSendsOneSchemaPerDistinctWatchedClass()
    {
        RadioModel model;
        SliceModel slice(0);
        StateMirror mirror;
        QVERIFY(mirror.watch("radio", &model));
        QVERIFY(mirror.watch("slice:0", &slice));

        Sink sink(&mirror);
        mirror.attachSession();

        QCOMPARE(sink.countOf(SessionMessageKind::Schema), 2);
        const QSet<QByteArray> names{ sink.entries.at(0).className, sink.entries.at(1).className };
        QCOMPARE(names, (QSet<QByteArray>{ "RadioModel", "SliceModel" }));
        QCOMPARE(sink.countOf(SessionMessageKind::ObjectCreate), 2);
        // 2 schema + 2 create = indices 0-3; the marker is index 4.
        QCOMPARE(sink.indexOfFirst(SessionMessageKind::SnapshotComplete), 4);
    }

    // ── Local direct mode is unaffected ─────────────────────────────────

    // propertiesChanged() -- the Task 7/8 forwarder -- must behave exactly
    // as it always has when attachSession() is never called: no
    // coalescing, no delay, one emission per notify. This is the
    // regression gate the brief names explicitly.
    void unattachedMirrorForwardsImmediatelyLikeTasksSevenAndEight()
    {
        SliceModel slice(0);
        StateMirror mirror;
        QVERIFY(mirror.watch("slice:0", &slice));
        QVERIFY2(!mirror.hasAttachedSession(), "test setup: attachSession() must never have run");

        QSignalSpy spy(&mirror, &StateMirror::propertiesChanged);
        slice.setFrequency(14200000.0);
        QCOMPARE(spy.count(), 1);

        const auto args = spy.takeFirst();
        QCOMPARE(args.at(0).toByteArray(), QByteArray("slice:0"));
    }

    // ── The coalescer, standalone ────────────────────────────────────────

    // Two properties on one object, one property on a second: flush()
    // must group by object (arrival order across objects) while keeping
    // each object's OWN ordinal arrival order, and a second update() for
    // an already-pending key must structurally overwrite rather than
    // append -- the "latest-wins" TciVfoCoalescer shape, generalised.
    void coalescerGroupsByObjectAndLatestValueWinsPerOrdinal()
    {
        MirrorCoalescer c;
        QCOMPARE(c.pendingObjectCount(), 0);

        c.update("slice:1", MirrorUpdate{ 5, "band", MirrorWireKind::Enum,
                                          QVariant(static_cast<qlonglong>(Band::Band40m)) });
        c.update("slice:0", MirrorUpdate{ 0, "frequency", MirrorWireKind::Float64, 14200000.0 });
        c.update("slice:0", MirrorUpdate{ 1, "dspMode", MirrorWireKind::Enum, QVariant(qlonglong(1)) });
        // Overwrite: same object, same ordinal, a genuinely different value.
        c.update("slice:0", MirrorUpdate{ 0, "frequency", MirrorWireKind::Float64, 14250000.0 });

        QCOMPARE(c.pendingObjectCount(), 2);
        QCOMPARE(c.pendingPropertyCount(), 3);

        const QList<QPair<QByteArray, QList<MirrorUpdate>>> batches = c.flush();
        QCOMPARE(batches.size(), 2);

        // Arrival order across objects: slice:1 went dirty before slice:0.
        QCOMPARE(batches.at(0).first, QByteArray("slice:1"));
        QCOMPARE(batches.at(1).first, QByteArray("slice:0"));

        QCOMPARE(batches.at(1).second.size(), 2);
        // Ordinal arrival order within slice:0: frequency (ordinal 0)
        // arrived before dspMode (ordinal 1).
        QCOMPARE(batches.at(1).second.at(0).name, QByteArray("frequency"));
        QCOMPARE(batches.at(1).second.at(0).value.toDouble(), 14250000.0); // latest, not first
        QCOMPARE(batches.at(1).second.at(1).name, QByteArray("dspMode"));

        // flush() drains fully.
        QCOMPARE(c.flush().size(), 0);
        QCOMPARE(c.pendingObjectCount(), 0);
    }

    void coalescerClearDropsEverythingUnflushed()
    {
        MirrorCoalescer c;
        c.update("slice:0", MirrorUpdate{ 0, "frequency", MirrorWireKind::Float64, 14200000.0 });
        QCOMPARE(c.pendingObjectCount(), 1);
        c.clear();
        QCOMPARE(c.pendingObjectCount(), 0);
        QCOMPARE(c.flush().size(), 0);
    }

    // ── SessionMessages: builders + JSON round-trip ─────────────────────

    void sessionMessagesRoundTripEveryKindThroughJson()
    {
        {
            const SessionMessage m = SessionMessages::schema(
                "SliceModel", { SessionSchemaField{ 0, "frequency", MirrorWireKind::Float64 },
                               SessionSchemaField{ 1, "sliceIndex", MirrorWireKind::Int64 } });
            const QByteArray wire = SessionMessages::encode(m);
            SessionMessage back;
            QVERIFY(SessionMessages::decode(wire, &back));
            QCOMPARE(back.kind, SessionMessageKind::Schema);
            QCOMPARE(back.className, QByteArray("SliceModel"));
            QCOMPARE(back.fields.size(), 2);
            QCOMPARE(back.fields.at(0).name, QByteArray("frequency"));
            QCOMPARE(back.fields.at(0).kind, MirrorWireKind::Float64);
            QCOMPARE(back.fields.at(1).ordinal, quint16(1));
        }
        {
            const QList<MirrorUpdate> bag{
                MirrorUpdate{ 0, "frequency", MirrorWireKind::Float64, 14200000.0 },
                MirrorUpdate{ 3, "active", MirrorWireKind::Bool, true },
                MirrorUpdate{ 7, "panKey", MirrorWireKind::Utf8, QStringLiteral("pan-0") },
            };
            const SessionMessage m = SessionMessages::objectCreate("slice:0", "SliceModel", bag);
            const QByteArray wire = SessionMessages::encode(m);
            SessionMessage back;
            QVERIFY(SessionMessages::decode(wire, &back));
            QCOMPARE(back.kind, SessionMessageKind::ObjectCreate);
            QCOMPARE(back.objectKey, QByteArray("slice:0"));
            QCOMPARE(back.className, QByteArray("SliceModel"));
            QCOMPARE(back.updates.size(), 3);
            QCOMPARE(back.updates.at(0).value.toDouble(), 14200000.0);
            QCOMPARE(back.updates.at(1).value.toBool(), true);
            QCOMPARE(back.updates.at(2).value.toString(), QStringLiteral("pan-0"));
        }
        {
            const SessionMessage m = SessionMessages::objectDestroy("slice:0", "SliceModel");
            SessionMessage back;
            QVERIFY(SessionMessages::decode(SessionMessages::encode(m), &back));
            QCOMPARE(back.kind, SessionMessageKind::ObjectDestroy);
            QCOMPARE(back.objectKey, QByteArray("slice:0"));
            QCOMPARE(back.className, QByteArray("SliceModel"));
        }
        {
            const SessionMessage m = SessionMessages::delta(
                "slice:0", { MirrorUpdate{ 0, "frequency", MirrorWireKind::Float64, 7100000.0 } });
            SessionMessage back;
            QVERIFY(SessionMessages::decode(SessionMessages::encode(m), &back));
            QCOMPARE(back.kind, SessionMessageKind::Delta);
            QCOMPARE(back.objectKey, QByteArray("slice:0"));
            QCOMPARE(back.updates.size(), 1);
            QCOMPARE(back.updates.first().value.toDouble(), 7100000.0);
        }
        {
            const SessionMessage m = SessionMessages::snapshotComplete();
            SessionMessage back;
            QVERIFY(SessionMessages::decode(SessionMessages::encode(m), &back));
            QCOMPARE(back.kind, SessionMessageKind::SnapshotComplete);
        }
    }

    void decodeRejectsMalformedInput()
    {
        SessionMessage out;
        QVERIFY(!SessionMessages::decode(QByteArray("not json"), &out));
        QVERIFY(!SessionMessages::decode(QByteArray("{}"), &out)); // no "type"
        QVERIFY(!SessionMessages::decode(QByteArray(R"({"type":"nonsense"})"), &out));
        QVERIFY(!SessionMessages::decode(
            QByteArray(
                R"({"type":"delta","key":"slice:0","properties":[{"ordinal":0,"name":"x","kind":"nope","value":1}]})"),
            &out));
    }
};

QTEST_MAIN(TestMirrorSnapshot)
#include "tst_mirror_snapshot.moc"
