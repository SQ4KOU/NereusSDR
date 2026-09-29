// no-port-check: NereusSDR-original.
// =================================================================
// tests/tst_slice_access_verbs.cpp  (NereusSDR)
// =================================================================
//
// Slice control and shared listening plan Task 4: the sliceAccess feature
// and its two-key gate, the SliceAccess objects (`access:<id>`), the slice
// and marker forms a sharing device receives, and slice.listen,
// slice.stopListening, slice.takeControl and slice.release against a real
// Core over loopback links. Devices A, B and C share slices; D is an older
// app (sessionHolder only). No radio: the transmit cases key the fake MOX,
// never hardware.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-28: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), slice control and shared listening plan Task 4,
//               with AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include "MultiDeviceHarness.h"

#include "core/session/SliceAccessController.h"
#include "core/session/SliceAccessPolicy.h"

#include <QSignalSpy>

namespace {

// A device that shares slices, and one that also transmits.
const QHash<QByteArray, int> kShares{{"deviceAuth", 1}, {"sessionHolder", 1}, {"sliceAccess", 1}};
const QHash<QByteArray, int> kSharesTx{
    {"deviceAuth", 1}, {"sessionHolder", 1}, {"sliceAccess", 1}, {"remoteTx", 1}};

QString accessKey(int sliceId)
{
    return QStringLiteral("access:%1").arg(sliceId);
}

QJsonValue accessOf(const LoopbackTransport* app, int sliceId, const char* property)
{
    return latest(app->received(), accessKey(sliceId), QString::fromLatin1(property));
}

QStringList idList(const QJsonValue& json)
{
    QStringList ids;
    for (const QJsonValue& v : QJsonDocument::fromJson(json.toString().toUtf8()).array()) {
        ids.append(v.toString());
    }
    return ids;
}

qint64 resultValue(const QJsonObject& result, const QString& name)
{
    for (const QJsonValue& v : result.value(QStringLiteral("values")).toArray()) {
        if (v.toObject().value(QStringLiteral("name")).toString() == name) {
            return v.toObject().value(QStringLiteral("value")).toInteger();
        }
    }
    return -1;
}

bool accepted(const QJsonObject& result)
{
    return result.value(QStringLiteral("accepted")).toBool(false);
}

QString reasonOf(const QJsonObject& result)
{
    return result.value(QStringLiteral("reason")).toString();
}

// The index in `app`'s wire of the first message of `type` on `key` from
// `from` on, or -1.
int indexOf(const LoopbackTransport* app, int from, const QString& type, const QString& key)
{
    for (int i = std::max(0, from); i < app->received().size(); ++i) {
        const QJsonObject o = QJsonDocument::fromJson(app->received().at(i)).object();
        if (o.value(QStringLiteral("type")).toString() == type
            && o.value(QStringLiteral("key")).toString() == key) {
            return i;
        }
    }
    return -1;
}

QString listenerWords(const QString& letter, const QString& controller)
{
    return QStringLiteral("Slice %1 is controlled by %2. Take control to change it.")
        .arg(letter, controller);
}

// Every active receiver stream, as a bit per stream.
quint32 activeStreamMask(const RadioModel& model)
{
    quint32 mask = 0;
    const SliceStreamAllocator& allocator = model.streamAllocator();
    for (int i = 0; i < allocator.streamCount() && i < 32; ++i) {
        if (allocator.isStreamActive(i)) {
            mask |= 1u << i;
        }
    }
    return mask;
}

// The slice's reference and revision as `app` last read them.
struct Seen {
    qint64 sliceId = -1;
    qint64 incarnation = 0;
    qint64 revision = 0;
};

Seen seenBy(const LoopbackTransport* app, int sliceId)
{
    return Seen{sliceId, accessOf(app, sliceId, "incarnation").toInteger(),
                accessOf(app, sliceId, "controlRevision").toInteger()};
}

QList<MirrorUpdate> refArgs(const Seen& seen)
{
    return {int64("sliceId", seen.sliceId), int64("incarnation", seen.incarnation)};
}

QList<MirrorUpdate> revisionArgs(const Seen& seen)
{
    return {int64("sliceId", seen.sliceId), int64("incarnation", seen.incarnation),
            int64("controlRevision", seen.revision)};
}

} // namespace

class TstSliceAccessVerbs : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        const QString profile =
            QStringLiteral("slice-access-verbs-%1").arg(QCoreApplication::applicationPid());
        AppSettings::setProfileOverride(profile);
        AppSettings::instance().clear();
    }

    void cleanupTestCase()
    {
        const QString path = AppSettings::instance().filePath();
        QFile::remove(path);
        QFile::remove(path + QStringLiteral(".bak"));
    }

    // ── Negotiation ──────────────────────────────────────────────────────

    void onlyADeviceThatDeclaresSliceAccessIsOfferedIt()
    {
        Core core;
        Device a;
        Device d(QStringLiteral("Mac"), QStringLiteral("computer"));
        core.pair(a);
        core.pair(d);
        LoopbackTransport* appA = core.signIn(a, kShares);
        LoopbackTransport* appD = core.signIn(d, kHolder);
        QVERIFY(admitted(appA));
        QVERIFY(admitted(appD));
        QCOMPARE(capability(appA->received(), QStringLiteral("sliceAccessVersion")), 1);
        QVERIFY(!capability(appD->received(), QStringLiteral("sliceAccessVersion")).has_value());
        // The capability is the last before coreBuildInfo.
        const QJsonArray caps =
            firstOfType(appA->received(), QStringLiteral("capabilities"))
                .value(QStringLiteral("properties")).toArray();
        QString last;
        for (const QJsonValue& p : caps) {
            const QString name = p.toObject().value(QStringLiteral("name")).toString();
            if (name != QStringLiteral("coreBuildInfo")) {
                last = name;
            }
        }
        QCOMPARE(last, QStringLiteral("sliceAccessVersion"));

        // A: the class and an object per slice. D: neither.
        QTRY_VERIFY(holds(appA, accessKey(0)));
        QTRY_VERIFY(holds(appA, accessKey(1)));
        QCOMPARE(accessOf(appA, 0, "sliceId").toInt(), 0);
        QCOMPARE(static_cast<quint64>(accessOf(appA, 0, "incarnation").toInteger()),
                 core.model->sliceOwnership()->incarnation(0));
        QCOMPARE(accessOf(appA, 0, "controllerDeviceId").toString(), a.id());
        QCOMPARE(accessOf(appA, 0, "controlRevision").toInteger(),
                 static_cast<qint64>(core.model->sliceOwnership()->controlRevision(0)));
        QCOMPARE(idList(accessOf(appA, 0, "listenerDeviceIds")), QStringList{a.id()});
        QCOMPARE(idList(accessOf(appA, 0, "activeRxDeviceIds")), QStringList{a.id()});
        QCOMPARE(accessOf(appA, 1, "controllerDeviceId").toString(), d.id());
        for (const QByteArray& wire : appD->received()) {
            const QJsonObject o = QJsonDocument::fromJson(wire).object();
            QVERIFY(!o.value(QStringLiteral("key")).toString().startsWith(QStringLiteral("access:")));
            QVERIFY(o.value(QStringLiteral("class")).toString() != QStringLiteral("SliceAccess"));
        }
        // D keeps today's forms: its own slice, a marker for A's.
        QCOMPARE(heldKeys(appD, QStringLiteral("slice:")), QStringList{QStringLiteral("slice:1")});
        QCOMPARE(heldKeys(appD, QStringLiteral("marker:")), QStringList{QStringLiteral("marker:0")});

        // The verbs are refused to D by the gate, and change nothing.
        const QString update =
            QStringLiteral("Update this app to listen to and take slices on this Core.");
        QVERIFY(OperatorWording::isPlain(update));
        const Seen seen = seenBy(appA, 0);
        for (const QByteArray verb :
             {QByteArrayLiteral("slice.listen"), QByteArrayLiteral("slice.stopListening")}) {
            const QJsonObject r = core.invoke(appD, verb, refArgs(seen));
            QVERIFY(!accepted(r));
            QCOMPARE(reasonOf(r), update);
        }
        for (const QByteArray verb :
             {QByteArrayLiteral("slice.takeControl"), QByteArrayLiteral("slice.release")}) {
            const QJsonObject r = core.invoke(appD, verb, revisionArgs(seen));
            QVERIFY(!accepted(r));
            QCOMPARE(reasonOf(r), update);
        }
        QCOMPARE(core.model->sliceOwnership()->listenersOf(0),
                 QList<QByteArray>{a.key.fingerprint()});
        QCOMPARE(core.model->sliceOwnership()->mark(0).owner, a.key.fingerprint());
    }

    // ── Listen in ────────────────────────────────────────────────────────

    void listeningAtFullCapacityAllocatesNothingAndSwapsTheForm()
    {
        Core core;
        core.model->configureStreamPool(2, 5, 192000);
        core.model->sliceById(0)->setFrequency(14200000.0);
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = core.signIn(a, kShares);
        QVERIFY(admitted(appA));
        // A fills the slice cap, on both receivers.
        for (int i = 0; i < 8; ++i) {
            if (!accepted(core.invoke(appA, "addSlice", {utf8("initialPanId", QString())}))) {
                break;
            }
            if (i == 0) {
                core.model->sliceById(1)->setFrequency(7074000.0);
            }
        }
        const int full = core.model->slices().size();
        QVERIFY(full > 2);
        QCOMPARE(core.model->streamAllocator().activeStreamCount(),
                 core.model->streamAllocator().streamCount());
        QVERIFY(!accepted(core.invoke(appA, "addSlice", {utf8("initialPanId", QString())})));
        const quint32 streams = activeStreamMask(*core.model);

        LoopbackTransport* appB = core.signIn(b, kShares);
        QVERIFY(admitted(appB));
        QCOMPARE(core.model->slices().size(), full);
        QTRY_VERIFY(holds(appB, QStringLiteral("marker:0")));
        QVERIFY(!holds(appB, QStringLiteral("slice:0")));
        QTRY_VERIFY(holds(appB, accessKey(0)));

        const int from = appB->received().size();
        const Seen seen = seenBy(appB, 0);
        QVERIFY(seen.incarnation > 0);
        const QJsonObject r = core.invoke(appB, "slice.listen", refArgs(seen));
        QVERIFY2(accepted(r), qPrintable(reasonOf(r)));
        QCOMPARE(resultValue(r, QStringLiteral("controlRevision")), seen.revision);
        // Nothing allocated.
        QCOMPARE(core.model->slices().size(), full);
        QCOMPARE(core.model->streamAllocator().activeStreamCount(),
                 core.model->streamAllocator().streamCount());
        QCOMPARE(activeStreamMask(*core.model), streams);
        // B's view: the marker goes, then the slice comes.
        QTRY_VERIFY(holds(appB, QStringLiteral("slice:0")));
        const int destroyed = indexOf(appB, from, QStringLiteral("object.destroy"),
                                      QStringLiteral("marker:0"));
        const int created = indexOf(appB, from, QStringLiteral("object.create"),
                                    QStringLiteral("slice:0"));
        QVERIFY(destroyed >= 0);
        QVERIFY(destroyed < created);
        QVERIFY(!holds(appB, QStringLiteral("marker:0")));
        const QStringList both{a.id(), b.id()};
        QTRY_COMPARE(idList(accessOf(appB, 0, "listenerDeviceIds")), both);
        QTRY_COMPARE(idList(accessOf(appA, 0, "listenerDeviceIds")), both);
        // A's own form is unchanged.
        QVERIFY(holds(appA, QStringLiteral("slice:0")));
        QVERIFY(!everSaw(appA, QStringLiteral("marker:0")));

        // Listening again is accepted and changes nothing.
        const int again = appB->received().size();
        QVERIFY(accepted(core.invoke(appB, "slice.listen", refArgs(seen))));
        QTest::qWait(3 * StationServer::kDefaultDeltaFlushMs);
        QCOMPARE(indexOf(appB, again, QStringLiteral("object.create"), QStringLiteral("slice:0")),
                 -1);
        QCOMPARE(core.model->sliceOwnership()->listenersOf(0).size(), 2);
    }

    void aListenerIsRefusedAChangeAndHearsTheControllers()
    {
        Core core;
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = core.signIn(a, kShares);
        LoopbackTransport* appB = core.signIn(b, kShares);
        QVERIFY(admitted(appA) && admitted(appB));
        QTRY_VERIFY(holds(appB, accessKey(0)));
        QVERIFY(accepted(core.invoke(appB, "slice.listen", refArgs(seenBy(appB, 0)))));
        QTRY_VERIFY(holds(appB, QStringLiteral("slice:0")));
        SliceModel* slice = core.model->sliceById(0);
        const double frequency = slice->frequency();

        const QString words = listenerWords(QStringLiteral("A"), QStringLiteral("iPhone"));
        QVERIFY(OperatorWording::isPlain(words));
        appB->sendText(SessionMessages::encode(
            SessionMessages::propertyWrite("slice:0", {f64("frequency", 7074000.0)}, 601)));
        QTRY_VERIFY(!propertyResult(appB, 601).isEmpty());
        const QJsonObject refused =
            propertyResult(appB, 601).value(QStringLiteral("results")).toArray().first().toObject();
        QCOMPARE(refused.value(QStringLiteral("accepted")).toBool(true), false);
        QCOMPARE(refused.value(QStringLiteral("reason")).toString(), words);
        QCOMPARE(slice->frequency(), frequency);
        // A verb that names the slice is refused in the same words.
        const QJsonObject verb = core.invoke(appB, "nnr.resetTuning", {int64("sliceId", 0)});
        QVERIFY(!accepted(verb));
        QCOMPARE(reasonOf(verb), words);

        // A's change is applied, and B receives it.
        const int from = appB->received().size();
        appA->sendText(SessionMessages::encode(
            SessionMessages::propertyWrite("slice:0", {f64("frequency", 14100000.0)}, 602)));
        QTRY_COMPARE(slice->frequency(), 14100000.0);
        QVERIFY(QTest::qWaitFor(
            [&]() {
                const QList<QJsonValue> v =
                    deltaValues(appB->received(), from, QStringLiteral("slice:0"),
                                QStringLiteral("frequency"));
                return !v.isEmpty() && v.last().toDouble() == 14100000.0;
            },
            5000));

        // B makes it its active receive slice; its own and the Core's
        // active slice stay where they were.
        const int stationSlice = core.model->sliceOwnership()->stationActiveSlice();
        const int ownActive = core.model->sliceOwnership()->activeFor(b.key.fingerprint());
        const QJsonObject select = core.invoke(appB, "setActiveSliceById", {int64("sliceId", 0)});
        QVERIFY2(accepted(select), qPrintable(reasonOf(select)));
        QCOMPARE(core.model->sliceOwnership()->activeRxFor(b.key.fingerprint()), 0);
        QCOMPARE(core.model->sliceOwnership()->activeFor(b.key.fingerprint()), ownActive);
        QCOMPARE(core.model->sliceOwnership()->stationActiveSlice(), stationSlice);
        QTRY_COMPARE(idList(accessOf(appB, 0, "activeRxDeviceIds")),
                     (QStringList{a.id(), b.id()}));
    }

    // ── Stop listening ───────────────────────────────────────────────────

    void stoppingListeningSwapsBackAndTheControllerIsSentToRelease()
    {
        Core core;
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = core.signIn(a, kShares);
        LoopbackTransport* appB = core.signIn(b, kShares);
        QVERIFY(admitted(appA) && admitted(appB));
        QTRY_VERIFY(holds(appB, accessKey(0)));
        const Seen seen = seenBy(appB, 0);
        QVERIFY(accepted(core.invoke(appB, "slice.listen", refArgs(seen))));
        QVERIFY(accepted(core.invoke(appB, "setActiveSliceById", {int64("sliceId", 0)})));
        QCOMPARE(core.model->sliceOwnership()->activeRxFor(b.key.fingerprint()), 0);

        // The controller cannot stop listening (ruling Q5).
        const QJsonObject refused = core.invoke(appA, "slice.stopListening", refArgs(seen));
        QVERIFY(!accepted(refused));
        const QString release = QStringLiteral("You control slice A. Use Release to leave it.");
        QVERIFY(OperatorWording::isPlain(release));
        QCOMPARE(reasonOf(refused), release);

        const int from = appB->received().size();
        QVERIFY(accepted(core.invoke(appB, "slice.stopListening", refArgs(seen))));
        QVERIFY(!core.model->sliceOwnership()->isListening(b.key.fingerprint(), 0));
        // Its receive choice moves to its next joined slice, its own.
        QCOMPARE(core.model->sliceOwnership()->activeRxFor(b.key.fingerprint()), 1);
        QTRY_VERIFY(holds(appB, QStringLiteral("marker:0")));
        QVERIFY(!holds(appB, QStringLiteral("slice:0")));
        QVERIFY(indexOf(appB, from, QStringLiteral("object.destroy"), QStringLiteral("slice:0"))
                < indexOf(appB, from, QStringLiteral("object.create"), QStringLiteral("marker:0")));
        // The slice stays, its controller's.
        QVERIFY(core.model->sliceById(0) != nullptr);
        QCOMPARE(core.model->sliceOwnership()->mark(0).owner, a.key.fingerprint());
        // Not joined any more: accepted, nothing to leave.
        QVERIFY(accepted(core.invoke(appB, "slice.stopListening", refArgs(seen))));
    }

    // ── Take control ─────────────────────────────────────────────────────

    void takingControlKeepsTheSliceAndTellsTheFormerController()
    {
        Core core;
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = core.signIn(a, kShares);
        LoopbackTransport* appB = core.signIn(b, kShares);
        QVERIFY(admitted(appA) && admitted(appB));
        QTRY_VERIFY(holds(appB, accessKey(0)));
        QVERIFY(accepted(core.invoke(appB, "slice.listen", refArgs(seenBy(appB, 0)))));
        QTRY_VERIFY(holds(appB, QStringLiteral("slice:0")));

        SliceModel* slice = core.model->sliceById(0);
        const QPointer<SliceModel> same(slice);
        const double frequency = slice->frequency();
        const DSPMode mode = slice->dspMode();
        const int filterLow = slice->filterLow();
        const int filterHigh = slice->filterHigh();
        const quint64 incarnation = core.model->sliceOwnership()->incarnation(0);
        RxChannel* const channel = core.model->rxChannelForSlice(0);
        QSignalSpy added(core.model.get(), &RadioModel::sliceAdded);
        QSignalSpy removed(core.model.get(), &RadioModel::sliceRemoved);
        const int fromA = appA->received().size();
        const int fromB = appB->received().size();

        const Seen seen = seenBy(appB, 0);
        const QJsonObject r = core.invoke(appB, "slice.takeControl", revisionArgs(seen));
        QVERIFY2(accepted(r), qPrintable(reasonOf(r)));
        QCOMPARE(resultValue(r, QStringLiteral("controlRevision")), seen.revision + 1);
        const SliceOwnership* ownership = core.model->sliceOwnership();
        QCOMPARE(ownership->mark(0).owner, b.key.fingerprint());
        QCOMPARE(ownership->listenersOf(0),
                 (QList<QByteArray>{b.key.fingerprint(), a.key.fingerprint()}));
        // The same slice, untouched.
        QCOMPARE(core.model->sliceById(0), same.data());
        QCOMPARE(ownership->incarnation(0), incarnation);
        QCOMPARE(slice->sliceLetter(), QChar(QLatin1Char('A')));
        QCOMPARE(slice->frequency(), frequency);
        QCOMPARE(slice->dspMode(), mode);
        QCOMPARE(slice->filterLow(), filterLow);
        QCOMPARE(slice->filterHigh(), filterHigh);
        QCOMPARE(core.model->rxChannelForSlice(0), channel);
        QCOMPARE(added.count(), 0);
        QCOMPARE(removed.count(), 0);

        // Nobody's form changes: both already had slice:0.
        QTest::qWait(3 * StationServer::kDefaultDeltaFlushMs);
        QVERIFY(holds(appA, QStringLiteral("slice:0")));
        QVERIFY(holds(appB, QStringLiteral("slice:0")));
        QCOMPARE(indexOf(appA, fromA, QStringLiteral("object.destroy"), QStringLiteral("slice:0")),
                 -1);
        QCOMPARE(indexOf(appB, fromB, QStringLiteral("object.create"), QStringLiteral("slice:0")),
                 -1);
        QTRY_COMPARE(accessOf(appA, 0, "controllerDeviceId").toString(), b.id());
        QTRY_COMPARE(idList(accessOf(appA, 0, "listenerDeviceIds")), (QStringList{b.id(), a.id()}));

        // A is told.
        QTRY_VERIFY(!firstOfType(appA->received(), QStringLiteral("notice")).isEmpty());
        const QJsonObject notice = firstOfType(appA->received(), QStringLiteral("notice"));
        QCOMPARE(notice.value(QStringLiteral("kind")).toString(), QStringLiteral("controlTaken"));
        const QString told = QStringLiteral("iPad took control of slice A. You are still listening.");
        QVERIFY(OperatorWording::isPlain(told));
        QCOMPARE(notice.value(QStringLiteral("reason")).toString(), told);
        QCOMPARE(notice.value(QStringLiteral("takeBack")).toBool(true), false);
        QCOMPARE(notice.value(QStringLiteral("byDeviceId")).toString(), b.id());
        QCOMPARE(notice.value(QStringLiteral("byName")).toString(), QStringLiteral("iPad"));
        const QJsonObject entry =
            notice.value(QStringLiteral("slices")).toArray().first().toObject();
        QCOMPARE(entry.value(QStringLiteral("sliceId")).toInt(), 0);
        QCOMPARE(entry.value(QStringLiteral("letter")).toString(), QStringLiteral("A"));
        QCOMPARE(entry.value(QStringLiteral("frequencyHz")).toDouble(), frequency);
        QVERIFY(firstOfType(appB->received(), QStringLiteral("notice")).isEmpty());

        // A's next change is refused, in the listener's words.
        appA->sendText(SessionMessages::encode(
            SessionMessages::propertyWrite("slice:0", {f64("frequency", 7074000.0)}, 701)));
        QTRY_VERIFY(!propertyResult(appA, 701).isEmpty());
        QCOMPARE(propertyResult(appA, 701).value(QStringLiteral("results")).toArray().first()
                     .toObject().value(QStringLiteral("reason")).toString(),
                 listenerWords(QStringLiteral("A"), QStringLiteral("iPad")));
        QCOMPARE(slice->frequency(), frequency);

        // Taking it again with the revision seen is accepted, no change.
        const Seen now = seenBy(appB, 0);
        QTRY_COMPARE(seenBy(appB, 0).revision, seen.revision + 1);
        QVERIFY(accepted(core.invoke(appB, "slice.takeControl", revisionArgs(seenBy(appB, 0)))));
        Q_UNUSED(now);
        QCOMPARE(ownership->mark(0).owner, b.key.fingerprint());
    }

    void ofTwoTakesWithTheSameRevisionExactlyOneIsApplied()
    {
        Core core;
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        Device c(QStringLiteral("Mac"), QStringLiteral("computer"));
        for (Device* x : {&a, &b, &c}) {
            core.pair(*x);
        }
        LoopbackTransport* appA = core.signIn(a, kShares);
        LoopbackTransport* appB = core.signIn(b, kShares);
        LoopbackTransport* appC = core.signIn(c, kShares);
        QVERIFY(admitted(appA) && admitted(appB) && admitted(appC));
        QTRY_VERIFY(holds(appB, accessKey(0)));
        QTRY_VERIFY(holds(appC, accessKey(0)));
        const Seen seen = seenBy(appB, 0);
        QCOMPARE(seenBy(appC, 0).revision, seen.revision);
        const int slices = core.model->slices().size();

        const QJsonObject first = core.invoke(appB, "slice.takeControl", revisionArgs(seen));
        const QJsonObject second = core.invoke(appC, "slice.takeControl", revisionArgs(seen));
        QVERIFY(accepted(first));
        QVERIFY(!accepted(second));
        const QString changed =
            QStringLiteral("Someone else changed who controls slice A. Look again and try once "
                           "more.");
        QVERIFY(OperatorWording::isPlain(changed));
        QCOMPARE(reasonOf(second), changed);
        const SliceOwnership* ownership = core.model->sliceOwnership();
        QCOMPARE(ownership->mark(0).owner, b.key.fingerprint());
        QCOMPARE(ownership->controlRevision(0), static_cast<quint64>(seen.revision + 1));
        QCOMPARE(ownership->listenersOf(0),
                 (QList<QByteArray>{b.key.fingerprint(), a.key.fingerprint()}));
        QCOMPARE(core.model->slices().size(), slices);
    }

    void anOldIncarnationReachesNothing()
    {
        Core core;
        core.model->configureStreamPool(5, 5, 192000);
        core.model->sliceById(0)->setFrequency(14200000.0);
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = core.signIn(a, kShares);
        QVERIFY(admitted(appA));
        const QJsonObject added = core.invoke(appA, "addSlice", {utf8("initialPanId", QString())});
        QVERIFY(accepted(added));
        LoopbackTransport* appB = core.signIn(b, kShares);
        QVERIFY(admitted(appB));
        QTRY_VERIFY(holds(appB, accessKey(0)));
        const Seen old = seenBy(appB, 0);

        // A closes A0 and makes a new slice on the same letter.
        QVERIFY(accepted(core.invoke(appA, "removeSlice", {int64("sliceId", 0)})));
        QVERIFY(core.model->sliceById(0) == nullptr);
        int remade = -1;
        {
            const SliceOwnership::CreatorScope creator(core.model->sliceOwnership(),
                                                       a.key.fingerprint());
            remade = core.model->addSlice(QStringLiteral("pan-0"));
        }
        QCOMPARE(remade, 0);
        const SliceOwnership* ownership = core.model->sliceOwnership();
        QVERIFY(ownership->incarnation(0) != static_cast<quint64>(old.incarnation));
        const quint64 revision = ownership->controlRevision(0);

        const QString closed = QStringLiteral("That slice has closed. Choose it again from the list.");
        QVERIFY(OperatorWording::isPlain(closed));
        QJsonObject r = core.invoke(appB, "slice.listen", refArgs(old));
        QVERIFY(!accepted(r));
        QCOMPARE(reasonOf(r), closed);
        r = core.invoke(appB, "slice.takeControl",
                        {int64("sliceId", 0), int64("incarnation", old.incarnation),
                         int64("controlRevision", static_cast<qint64>(revision))});
        QVERIFY(!accepted(r));
        QCOMPARE(reasonOf(r), closed);
        QCOMPARE(ownership->mark(0).owner, a.key.fingerprint());
        QCOMPARE(ownership->listenersOf(0), QList<QByteArray>{a.key.fingerprint()});
        QCOMPARE(ownership->controlRevision(0), revision);
    }

    void controlPassesOnlyFromADeviceThatCanStayListening()
    {
        Core core;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        Device d(QStringLiteral("Mac"), QStringLiteral("computer"));
        core.pair(d);
        core.pair(b);
        LoopbackTransport* appD = core.signIn(d, kHolder);
        LoopbackTransport* appB = core.signIn(b, kShares);
        QVERIFY(admitted(appD) && admitted(appB));
        QCOMPARE(core.model->sliceOwnership()->mark(0).owner, d.key.fingerprint());
        QTRY_VERIFY(holds(appB, accessKey(0)));
        // B may listen to the older window's slice.
        QVERIFY(accepted(core.invoke(appB, "slice.listen", refArgs(seenBy(appB, 0)))));
        // It may not take it (ruling Q7).
        const QJsonObject r = core.invoke(appB, "slice.takeControl", revisionArgs(seenBy(appB, 0)));
        QVERIFY(!accepted(r));
        const QString words =
            QStringLiteral("Mac needs an update before control of slice A can pass to another "
                           "device.");
        QVERIFY(OperatorWording::isPlain(words));
        QCOMPARE(reasonOf(r), words);
        QCOMPARE(core.model->sliceOwnership()->mark(0).owner, d.key.fingerprint());
        // The older window's own close of it releases it (ruling Q6): the
        // slice stays for B, controlled by nobody.
        QVERIFY(accepted(core.invoke(appD, "removeSlice", {int64("sliceId", 0)})));
        QVERIFY(core.model->sliceById(0) != nullptr);
        QVERIFY(core.model->sliceOwnership()->mark(0).owner.isEmpty());
        QCOMPARE(core.model->sliceOwnership()->listenersOf(0),
                 QList<QByteArray>{b.key.fingerprint()});
        QTRY_VERIFY(!holds(appD, QStringLiteral("slice:0")));
    }

    // ── Transmit (ruling Q8) ─────────────────────────────────────────────

    void takingTheIdleTransmitSliceMovesTheFlagToTheFormerControllersOther()
    {
        Core core;
        allowTransmit(core);
        const int second = addCoHostedSlice(*core.model);
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = core.signIn(a, kSharesTx);
        LoopbackTransport* appB = core.signIn(b, kSharesTx);
        QVERIFY(admitted(appA) && admitted(appB));
        const SliceOwnership* ownership = core.model->sliceOwnership();
        QCOMPARE(ownership->mark(0).owner, a.key.fingerprint());
        QCOMPARE(ownership->mark(second).owner, a.key.fingerprint());
        MoxController* mox = core.model->moxController();
        TxSliceArbiter* arbiter = core.model->txSliceArbiter();
        // A holds transmit, unkeyed, on A0.
        mox->setMox(true, keyerFor(a));
        mox->setMox(false, keyerFor(a));
        QTRY_COMPARE(mox->state(), MoxState::Rx);
        QVERIFY(accepted(core.invoke(appA, "tx.setTxSlice", {int64("sliceId", 0)})));
        QCOMPARE(arbiter->txBoundSliceId(), 0);
        QVERIFY(core.server->transmitHolder()->isHeldBy(a.key.fingerprint()));
        QSignalSpy moxChanges(mox, &MoxController::moxChanged);

        QTRY_VERIFY(holds(appB, accessKey(0)));
        QVERIFY(accepted(core.invoke(appB, "slice.listen", refArgs(seenBy(appB, 0)))));
        const QJsonObject r = core.invoke(appB, "slice.takeControl", revisionArgs(seenBy(appB, 0)));
        QVERIFY2(accepted(r), qPrintable(reasonOf(r)));
        QCOMPARE(arbiter->txBoundSliceId(), second);
        QCOMPARE(core.server->chosenTxSliceForTest(a.key.fingerprint()), second);
        QVERIFY(core.server->transmitHolder()->isHeldBy(a.key.fingerprint()));
        QVERIFY(!core.server->transmitHolder()->isHeldBy(b.key.fingerprint()));
        QCOMPARE(moxChanges.count(), 0);
        QVERIFY(!mox->isMox());
        QTRY_COMPARE(accessOf(appA, 0, "txSelected").toBool(true), false);
    }

    void takingTheOnlyIdleTransmitSliceReleasesTransmit()
    {
        Core core;
        allowTransmit(core);
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = core.signIn(a, kSharesTx);
        QVERIFY(admitted(appA));
        LoopbackTransport* appB = core.signIn(b, kSharesTx);
        QVERIFY(admitted(appB));
        QCOMPARE(core.model->sliceOwnership()->ownedBy(a.key.fingerprint()), QList<int>{0});
        MoxController* mox = core.model->moxController();
        mox->setMox(true, keyerFor(a));
        mox->setMox(false, keyerFor(a));
        QTRY_COMPARE(mox->state(), MoxState::Rx);
        QVERIFY(core.server->transmitHolder()->isHeldBy(a.key.fingerprint()));
        QCOMPARE(core.model->txSliceArbiter()->txBoundSliceId(), 0);
        QSignalSpy moxChanges(mox, &MoxController::moxChanged);

        QTRY_VERIFY(holds(appB, accessKey(0)));
        const QJsonObject r = core.invoke(appB, "slice.takeControl", revisionArgs(seenBy(appB, 0)));
        QVERIFY2(accepted(r), qPrintable(reasonOf(r)));
        QTRY_VERIFY(!core.server->transmitHolder()->holder().has_value());
        QCOMPARE(moxChanges.count(), 0);
        QVERIFY(!mox->isMox());
    }

    void aTransmittingSliceCannotChangeHands()
    {
        Core core;
        allowTransmit(core);
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = core.signIn(a, kSharesTx);
        LoopbackTransport* appB = core.signIn(b, kSharesTx);
        QVERIFY(admitted(appA) && admitted(appB));
        QTRY_VERIFY(holds(appB, accessKey(0)));
        QVERIFY(accepted(core.invoke(appB, "slice.listen", refArgs(seenBy(appB, 0)))));
        MoxController* mox = core.model->moxController();
        TxSliceArbiter* arbiter = core.model->txSliceArbiter();
        mox->setMox(true, keyerFor(a));
        QTRY_COMPARE(mox->state(), MoxState::Tx);
        QCOMPARE(arbiter->txBoundSliceId(), 0);
        QTRY_COMPARE(accessOf(appB, 0, "onAir").toBool(false), true);

        const SliceOwnership* ownership = core.model->sliceOwnership();
        const Seen seen = seenBy(appB, 0);
        QJsonObject r = core.invoke(appB, "slice.takeControl", revisionArgs(seen));
        QVERIFY(!accepted(r));
        const QString words = QStringLiteral("Slice A is transmitting. Take control once it stops.");
        QVERIFY(OperatorWording::isPlain(words));
        QCOMPARE(reasonOf(r), words);
        QCOMPARE(ownership->mark(0).owner, a.key.fingerprint());
        QCOMPARE(ownership->listenersOf(0),
                 (QList<QByteArray>{a.key.fingerprint(), b.key.fingerprint()}));
        QCOMPARE(arbiter->txBoundSliceId(), 0);
        QVERIFY(mox->isMox());

        // A release with a listener is a hand-off too.
        r = core.invoke(appA, "slice.release", revisionArgs(seen));
        QVERIFY(!accepted(r));
        QCOMPARE(reasonOf(r), QStringLiteral("Slice A is transmitting. Release it once it stops."));
        QCOMPARE(ownership->mark(0).owner, a.key.fingerprint());

        mox->setMox(false, keyerFor(a));
        QTRY_COMPARE(mox->state(), MoxState::Rx);
        r = core.invoke(appB, "slice.takeControl", revisionArgs(seen));
        QVERIFY2(accepted(r), qPrintable(reasonOf(r)));
        QCOMPARE(ownership->mark(0).owner, b.key.fingerprint());
    }

    // Slice control fix wave (Critical 1): a transmit move waiting for the
    // unkey counts as transmitting on the slice it lands on, so control of
    // that slice cannot pass before the flag lands, and the former
    // controller's transmit never ends up on another device's slice.
    void aTransmitMoveWaitingForTheUnkeyHoldsItsSlice()
    {
        Core core;
        allowTransmit(core);
        const int second = addCoHostedSlice(*core.model);
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = core.signIn(a, kSharesTx);
        LoopbackTransport* appB = core.signIn(b, kSharesTx);
        QVERIFY(admitted(appA) && admitted(appB));
        const SliceOwnership* ownership = core.model->sliceOwnership();
        QCOMPARE(ownership->mark(0).owner, a.key.fingerprint());
        QCOMPARE(ownership->mark(second).owner, a.key.fingerprint());
        QTRY_VERIFY(holds(appB, accessKey(0)));
        QVERIFY(accepted(core.invoke(appB, "slice.listen", refArgs(seenBy(appB, 0)))));
        MoxController* mox = core.model->moxController();
        TxSliceArbiter* arbiter = core.model->txSliceArbiter();
        // A holds transmit on its second slice and keys there.
        mox->setMox(true, keyerFor(a));
        mox->setMox(false, keyerFor(a));
        QTRY_COMPARE(mox->state(), MoxState::Rx);
        QVERIFY(accepted(core.invoke(appA, "tx.setTxSlice", {int64("sliceId", second)})));
        QCOMPARE(arbiter->txBoundSliceId(), second);
        mox->setMox(true, keyerFor(a));
        QTRY_COMPARE(mox->state(), MoxState::Tx);
        // The key's end is held (its walk stalls, the gate's bound never
        // fires), so the move below waits.
        mox->setTimerIntervals(0, 0, 0, 0, 600000, 0);
        core.model->unkeyGate()->setScheduler([](int, QObject*, std::function<void()>) {});

        // A moves its transmit to A0 while keyed: the move waits.
        QVERIFY(accepted(core.invoke(appA, "tx.setTxSlice", {int64("sliceId", 0)})));
        QCOMPARE(arbiter->pendingHandoffSliceId(), 0);
        QCOMPARE(arbiter->txBoundSliceId(), second);

        // B cannot take A0 meanwhile, and A cannot release it to B.
        const Seen seen = seenBy(appB, 0);
        QJsonObject r = core.invoke(appB, "slice.takeControl", revisionArgs(seen));
        QVERIFY(!accepted(r));
        QCOMPARE(reasonOf(r), QStringLiteral("Slice A is transmitting. Take control once it stops."));
        r = core.invoke(appA, "slice.release", revisionArgs(seen));
        QVERIFY(!accepted(r));
        QCOMPARE(reasonOf(r), QStringLiteral("Slice A is transmitting. Release it once it stops."));
        QCOMPARE(ownership->mark(0).owner, a.key.fingerprint());

        // The key ends; the flag lands on A0, still A's.
        mox->setTimerIntervals(0, 0, 0, 0, 0, 0);
        QTRY_COMPARE(mox->state(), MoxState::Rx);
        QTRY_COMPARE(arbiter->txBoundSliceId(), 0);
        QCOMPARE(arbiter->pendingHandoffSliceId(), -1);

        // Now B takes it, idle: the flag leaves the slice B took.
        r = core.invoke(appB, "slice.takeControl", revisionArgs(seenBy(appB, 0)));
        QVERIFY2(accepted(r), qPrintable(reasonOf(r)));
        QCOMPARE(ownership->mark(0).owner, b.key.fingerprint());
        QCOMPARE(arbiter->txBoundSliceId(), second);
        QVERIFY(core.server->transmitHolder()->isHeldBy(a.key.fingerprint()));
        QVERIFY(!mox->isMox());
    }

    // Slice control fix wave (Important 4): the record of an explicit
    // transmit choice is written only by tx.setTxSlice (and the hosting
    // desktop's own selection), never by the binding a new holder gets by
    // itself, and it goes when control of the slice passes.
    void onlyAnExplicitSelectionMarksATransmitSliceChosen()
    {
        Core core;
        allowTransmit(core);
        core.model->configureStreamPool(5, 5, 192000);
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = core.signIn(a, kSharesTx);
        LoopbackTransport* appB = core.signIn(b, kSharesTx);
        QVERIFY(admitted(appA) && admitted(appB));
        SliceOwnership* ownership = core.model->sliceOwnership();
        if (ownership->ownedBy(b.key.fingerprint()).isEmpty()) {
            QVERIFY(accepted(core.invoke(appB, "addSlice", {utf8("initialPanId", QString())})));
        }
        const int mine = ownership->ownedBy(b.key.fingerprint()).first();
        TxSliceArbiter* arbiter = core.model->txSliceArbiter();
        QVERIFY(arbiter->txBoundSliceId() != mine);

        // B keys: its hold binds its own slice by itself.
        MoxController* mox = core.model->moxController();
        mox->setMox(true, keyerFor(b));
        mox->setMox(false, keyerFor(b));
        QTRY_COMPARE(mox->state(), MoxState::Rx);
        QVERIFY(core.server->transmitHolder()->isHeldBy(b.key.fingerprint()));
        QTRY_COMPARE(arbiter->txBoundSliceId(), mine);
        QCOMPARE(core.server->explicitTxSliceFor(b.key.fingerprint()), -1);

        // Its own selection is the record.
        QVERIFY(accepted(core.invoke(appB, "tx.setTxSlice", {int64("sliceId", mine)})));
        QCOMPARE(core.server->explicitTxSliceFor(b.key.fingerprint()), mine);

        // Control of the slice passes: the record goes with it.
        QTRY_VERIFY(holds(appA, accessKey(mine)));
        const QJsonObject r =
            core.invoke(appA, "slice.takeControl", revisionArgs(seenBy(appA, mine)));
        QVERIFY2(accepted(r), qPrintable(reasonOf(r)));
        QCOMPARE(core.server->explicitTxSliceFor(b.key.fingerprint()), -1);
        QCOMPARE(core.server->explicitTxSliceFor(a.key.fingerprint()), -1);

        // The hosting desktop's own selection is the station device's.
        QCOMPARE(core.server->explicitTxSliceFor(SliceOwnership::stationDevice()), -1);
        QVERIFY(core.model->requestTxHandoffToSlice(0));
        QCOMPARE(core.server->explicitTxSliceFor(SliceOwnership::stationDevice()), 0);
    }

    // The review's third-holder case: a device holding transmit with the
    // flag parked on a slice it does not control loses that selection when
    // another device takes the slice.
    void takingASliceClearsAnotherHoldersSelectionOfIt()
    {
        Core core;
        allowTransmit(core);
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        Device c(QStringLiteral("Mac"), QStringLiteral("computer"));
        core.pair(a);
        core.pair(b);
        core.pair(c);
        LoopbackTransport* appA = core.signIn(a, kSharesTx);
        LoopbackTransport* appB = core.signIn(b, kSharesTx);
        LoopbackTransport* appC = core.signIn(c, kSharesTx);
        QVERIFY(admitted(appA) && admitted(appB) && admitted(appC));
        SliceOwnership* ownership = core.model->sliceOwnership();
        QCOMPARE(ownership->mark(0).owner, a.key.fingerprint());
        // C gives up its own slices, so its transmit binds nowhere of its
        // own and the flag stays parked on A0.
        for (int id : ownership->ownedBy(c.key.fingerprint())) {
            QVERIFY(accepted(core.invoke(appC, "removeSlice", {int64("sliceId", id)})));
        }
        QVERIFY(ownership->ownedBy(c.key.fingerprint()).isEmpty());
        TxSliceArbiter* arbiter = core.model->txSliceArbiter();
        QCOMPARE(arbiter->txBoundSliceId(), 0);
        MoxController* mox = core.model->moxController();
        mox->setMox(true, keyerFor(c));
        mox->setMox(false, keyerFor(c));
        QTRY_COMPARE(mox->state(), MoxState::Rx);
        QVERIFY(core.server->transmitHolder()->isHeldBy(c.key.fingerprint()));
        QCOMPARE(arbiter->txBoundSliceId(), 0);

        QTRY_VERIFY(holds(appB, accessKey(0)));
        const QJsonObject r =
            core.invoke(appB, "slice.takeControl", revisionArgs(seenBy(appB, 0)));
        QVERIFY2(accepted(r), qPrintable(reasonOf(r)));
        QCOMPARE(ownership->mark(0).owner, b.key.fingerprint());
        // C controls no slice to move to: its transmit is released.
        QTRY_VERIFY(!core.server->transmitHolder()->isHeldBy(c.key.fingerprint()));
        QVERIFY(!mox->isMox());
    }

    // ── Leaving the Core (fix wave, Important 2) ────────────────────────
    // A device that leaves, is revoked or stays away past its 180 s stops
    // listening too. A slice kept only for it closes, with its receiver.

    // A and B on the Core: A controls A0 and A1, B listens to both, then A
    // releases A0, which stays for B alone. Returns A0's receiver stream.
    int listenerKeepsAReleasedSlice(Core& core, const Device& a, const Device& b,
                                    LoopbackTransport** appA, LoopbackTransport** appB)
    {
        core.model->configureStreamPool(5, 5, 192000);
        core.model->sliceById(0)->setFrequency(14200000.0);
        *appA = core.signIn(a, kShares);
        if (!admitted(*appA)) {
            return -1;
        }
        const QJsonObject added = core.invoke(*appA, "addSlice", {utf8("initialPanId", QString())});
        if (!accepted(added)) {
            return -1;
        }
        core.model->sliceById(0)->setFrequency(7074000.0);
        *appB = core.signIn(b, kShares);
        if (!admitted(*appB)) {
            return -1;
        }
        const bool seen = QTest::qWaitFor(
            [&]() { return holds(*appB, accessKey(0)) && holds(*appB, accessKey(1)); }, 5000);
        if (!seen
            || !accepted(core.invoke(*appB, "slice.listen", refArgs(seenBy(*appB, 0))))
            || !accepted(core.invoke(*appB, "slice.listen", refArgs(seenBy(*appB, 1))))
            || !accepted(core.invoke(*appA, "slice.release", revisionArgs(seenBy(*appA, 0))))) {
            return -1;
        }
        return core.model->sliceById(0)->streamIndex();
    }

    void aDeviceThatLeavesStopsListening()
    {
        Core core;
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = nullptr;
        LoopbackTransport* appB = nullptr;
        const int stream = listenerKeepsAReleasedSlice(core, a, b, &appA, &appB);
        QVERIFY(stream >= 0);
        const SliceOwnership* ownership = core.model->sliceOwnership();
        QVERIFY(ownership->mark(0).owner.isEmpty());
        QCOMPARE(ownership->listenersOf(0), QList<QByteArray>{b.key.fingerprint()});
        QCOMPARE(ownership->listenersOf(1),
                 (QList<QByteArray>{a.key.fingerprint(), b.key.fingerprint()}));

        QVERIFY(accepted(core.invoke(appB, "session.leave")));
        QTRY_VERIFY(!appB->isOpen());
        // A0 was kept only for B: it closes, and its receiver is free.
        QTRY_VERIFY(core.model->sliceById(0) == nullptr);
        QVERIFY(!core.model->streamAllocator().isStreamActive(stream));
        // A1 stays A's, with B gone from it.
        QCOMPARE(ownership->listenersOf(1), QList<QByteArray>{a.key.fingerprint()});
        QVERIFY(ownership->joinedBy(b.key.fingerprint()).isEmpty());
    }

    void aDeviceAwayPastItsGraceStopsListening()
    {
        Core core;
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = nullptr;
        LoopbackTransport* appB = nullptr;
        const int stream = listenerKeepsAReleasedSlice(core, a, b, &appA, &appB);
        QVERIFY(stream >= 0);
        const SliceOwnership* ownership = core.model->sliceOwnership();

        appB->closeLink(QStringLiteral("lost"));
        QTRY_VERIFY(core.sessions().entry(b.key.fingerprint()).has_value()
                    && core.sessions().entry(b.key.fingerprint())->state
                        == DeviceSessionRegistry::State::Away);
        // Away within its 180 s: still listening.
        QCOMPARE(ownership->listenersOf(0), QList<QByteArray>{b.key.fingerprint()});
        QVERIFY(core.model->sliceById(0) != nullptr);

        core.now += DeviceSessionRegistry::kGraceMs + 1;
        core.sessions().expireAway();
        QTRY_VERIFY(core.model->sliceById(0) == nullptr);
        QVERIFY(!core.model->streamAllocator().isStreamActive(stream));
        QCOMPARE(ownership->listenersOf(1), QList<QByteArray>{a.key.fingerprint()});
        QVERIFY(ownership->joinedBy(b.key.fingerprint()).isEmpty());
    }

    void aRevokedDeviceStopsListening()
    {
        Core core;
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = nullptr;
        LoopbackTransport* appB = nullptr;
        const int stream = listenerKeepsAReleasedSlice(core, a, b, &appA, &appB);
        QVERIFY(stream >= 0);
        const SliceOwnership* ownership = core.model->sliceOwnership();

        const QJsonObject revoked = core.invoke(appA, "devices.revoke", {utf8("id", b.id())});
        QVERIFY2(accepted(revoked), qPrintable(reasonOf(revoked)));
        QTRY_VERIFY(core.model->sliceById(0) == nullptr);
        QVERIFY(!core.model->streamAllocator().isStreamActive(stream));
        QCOMPARE(ownership->listenersOf(1), QList<QByteArray>{a.key.fingerprint()});
        QVERIFY(ownership->joinedBy(b.key.fingerprint()).isEmpty());
    }

    // ── Release ──────────────────────────────────────────────────────────

    void aReleasedSliceStaysForItsListenerWhoIsNotGivenControl()
    {
        Core core;
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = core.signIn(a, kShares);
        LoopbackTransport* appB = core.signIn(b, kShares);
        QVERIFY(admitted(appA) && admitted(appB));
        QTRY_VERIFY(holds(appB, accessKey(0)));
        QVERIFY(accepted(core.invoke(appB, "slice.listen", refArgs(seenBy(appB, 0)))));
        const SliceOwnership* ownership = core.model->sliceOwnership();
        QVERIFY(SliceAccessPolicy::mayHear(*ownership, b.key.fingerprint(), 0));

        // Only the controller releases.
        const Seen seen = seenBy(appA, 0);
        QJsonObject r = core.invoke(appB, "slice.release", revisionArgs(seen));
        QVERIFY(!accepted(r));
        QCOMPARE(reasonOf(r), QStringLiteral("Only the device that controls slice A can release it."));

        r = core.invoke(appA, "slice.release", revisionArgs(seen));
        QVERIFY2(accepted(r), qPrintable(reasonOf(r)));
        QVERIFY(core.model->sliceById(0) != nullptr);
        QVERIFY(ownership->mark(0).owner.isEmpty());
        QCOMPARE(ownership->listenersOf(0), QList<QByteArray>{b.key.fingerprint()});
        QVERIFY(SliceAccessPolicy::mayHear(*ownership, b.key.fingerprint(), 0));
        QTRY_VERIFY(!holds(appA, QStringLiteral("slice:0")));
        QTRY_VERIFY(holds(appA, QStringLiteral("marker:0")));
        QVERIFY(holds(appB, QStringLiteral("slice:0")));
        QTRY_COMPARE(accessOf(appB, 0, "controllerDeviceId").toString(), QString());
        QTRY_COMPARE(idList(accessOf(appB, 0, "listenerDeviceIds")), QStringList{b.id()});

        // B alone on the Core: a new slice nobody owns is adopted, the
        // released one is not (ruling Q9).
        QVERIFY(accepted(core.invoke(appA, "session.leave")));
        QTRY_COMPARE(core.sessions().entries().size(), 1);
        const int fresh = core.model->addSlice(QStringLiteral("pan-0"));
        QVERIFY(fresh >= 0);
        QTRY_COMPARE(ownership->mark(fresh).owner, b.key.fingerprint());
        QVERIFY(ownership->mark(0).owner.isEmpty());
        QCOMPARE(ownership->listenersOf(0), QList<QByteArray>{b.key.fingerprint()});
        // Its changes are refused while nobody controls it, in words that
        // name the slice as every other refusal does.
        const QJsonObject change = core.invoke(appB, "nnr.resetTuning", {int64("sliceId", 0)});
        QVERIFY(!accepted(change));
        const QString nobody =
            QStringLiteral("Nobody controls slice A. Take control to change it.");
        QVERIFY(OperatorWording::isPlain(nobody));
        QCOMPARE(reasonOf(change), nobody);

        // It can take it, with the revision it saw.
        QTRY_VERIFY(seenBy(appB, 0).revision == static_cast<qint64>(ownership->controlRevision(0)));
        r = core.invoke(appB, "slice.takeControl", revisionArgs(seenBy(appB, 0)));
        QVERIFY2(accepted(r), qPrintable(reasonOf(r)));
        QCOMPARE(ownership->mark(0).owner, b.key.fingerprint());
    }

    void aReleasedSliceNobodyElseIsOnCloses()
    {
        Core core;
        core.model->configureStreamPool(5, 5, 192000);
        core.model->sliceById(0)->setFrequency(14200000.0);
        Device a;
        core.pair(a);
        LoopbackTransport* appA = core.signIn(a, kShares);
        QVERIFY(admitted(appA));
        QVERIFY(accepted(core.invoke(appA, "addSlice", {utf8("initialPanId", QString())})));
        core.model->sliceById(0)->setFrequency(7074000.0);
        const int streamOfA0 = core.model->sliceById(0)->streamIndex();
        QVERIFY(streamOfA0 >= 0);
        QVERIFY(core.model->sliceById(1)->streamIndex() != streamOfA0);
        QTRY_VERIFY(holds(appA, accessKey(0)));

        const QJsonObject r = core.invoke(appA, "slice.release", revisionArgs(seenBy(appA, 0)));
        QVERIFY2(accepted(r), qPrintable(reasonOf(r)));
        QVERIFY(core.model->sliceById(0) == nullptr);
        QVERIFY(!core.model->streamAllocator().isStreamActive(streamOfA0));
        QTRY_VERIFY(!holds(appA, accessKey(0)));
        QVERIFY(!holds(appA, QStringLiteral("slice:0")));
    }

    // Slice control fix wave (Important 1): the Core's last slice stays when
    // released, so a release of it is a hand-off to nobody and is refused
    // while it transmits, as a release kept for listeners is.
    void releasingTheLastSliceIsRefusedWhileItTransmits()
    {
        Core core;
        allowTransmit(core);
        Device a;
        core.pair(a);
        LoopbackTransport* appA = core.signIn(a, kSharesTx);
        QVERIFY(admitted(appA));
        QCOMPARE(core.model->slices().size(), 1);
        const SliceOwnership* ownership = core.model->sliceOwnership();
        QCOMPARE(ownership->mark(0).owner, a.key.fingerprint());
        MoxController* mox = core.model->moxController();
        mox->setMox(true, keyerFor(a));
        QTRY_COMPARE(mox->state(), MoxState::Tx);
        QTRY_VERIFY(holds(appA, accessKey(0)));

        const Seen seen = seenBy(appA, 0);
        QJsonObject r = core.invoke(appA, "slice.release", revisionArgs(seen));
        QVERIFY(!accepted(r));
        QCOMPARE(reasonOf(r), QStringLiteral("Slice A is transmitting. Release it once it stops."));
        QCOMPARE(ownership->mark(0).owner, a.key.fingerprint());
        QVERIFY(core.server->transmitHolder()->isHeldBy(a.key.fingerprint()));
        QVERIFY(mox->isMox());

        mox->setMox(false, keyerFor(a));
        QTRY_COMPARE(mox->state(), MoxState::Rx);
        r = core.invoke(appA, "slice.release", revisionArgs(seen));
        QVERIFY2(accepted(r), qPrintable(reasonOf(r)));
        QVERIFY(core.model->sliceById(0) != nullptr);
        QVERIFY(ownership->mark(0).owner.isEmpty());
    }

    void aControllersCloseOfASharedSliceReleasesIt()
    {
        Core core;
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = core.signIn(a, kShares);
        LoopbackTransport* appB = core.signIn(b, kShares);
        QVERIFY(admitted(appA) && admitted(appB));
        QTRY_VERIFY(holds(appB, accessKey(0)));
        QVERIFY(accepted(core.invoke(appB, "slice.listen", refArgs(seenBy(appB, 0)))));
        const int slices = core.model->slices().size();
        const QJsonObject r = core.invoke(appA, "removeSlice", {int64("sliceId", 0)});
        QVERIFY2(accepted(r), qPrintable(reasonOf(r)));
        QCOMPARE(core.model->slices().size(), slices);
        QVERIFY(core.model->sliceOwnership()->mark(0).owner.isEmpty());
        QCOMPARE(core.model->sliceOwnership()->listenersOf(0),
                 QList<QByteArray>{b.key.fingerprint()});
        // A listener's close is refused as any change is.
        const QJsonObject refused = core.invoke(appB, "removeSlice", {int64("sliceId", 0)});
        QVERIFY(!accepted(refused));
        QCOMPARE(core.model->slices().size(), slices);
    }
};

QTEST_MAIN(TstSliceAccessVerbs)
#include "tst_slice_access_verbs.moc"
