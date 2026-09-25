// no-port-check: NereusSDR-original.
// =================================================================
// tests/tst_confirm_step.cpp  (NereusSDR)
// =================================================================
//
// iPhone app plan Task 74 (R-IOS-02, R-IOS-30; the several-devices design,
// sections 6.1 to 6.4, 7.3, 7.4 and 10.7, rulings 6.2 to 6.10, design
// ruling 6.5a, rulings 7.4a and 10.2): receivers shared by several devices,
// asked before, told after.
//
// Authorisation first (who may move or take what), over one Core and the
// in-process loopback, several devices signed in by keys made at run time:
//   - a device that does not anchor its receiver has its C-Tune pin
//     request refused with the plain reason naming the anchor;
//   - the anchor's C-Tune move that would leave another device's slice
//     outside the window changes nothing and asks (confirm.request
//     panMove), naming the device and each slice's effect (moves, or closes
//     with no receiver free); proceed moves or closes it and tells its
//     device; cancel changes nothing;
//   - the anchor's band change on a shared receiver is the same question,
//     with the bands in `change`; alone, today's retune; with the anchor's
//     own other slice left outside, today's retune (design ruling 6.5a),
//     and with no receiver free the chooser lists the shared receiver
//     takeable false, naming that slice;
//   - a proceed whose set grew asks again and applies nothing;
//   - a device that does not anchor moves its pan to a free receiver,
//     nobody asked; with none free, the chooser;
//   - every receiver held: the refusal names who holds them, the chooser
//     lists each receiver's slices and devices; proceed closes every other
//     device's slice on the chosen one, tells each owner with Take it back,
//     and Take it back asks the same question the other way;
//   - the slice cap full: the slice chooser, and the same notice;
//   - an older window is never asked: the refusal names who it would
//     affect; a take of its last slice ends it (takenOver); with no slice
//     for it at admission it is refused, retryable.
// Then the readback (ruling 7.4a) for a command and a property write, and
// notices: waiting for an away device and arriving after its
// snapshot.complete with secondsAgo from when they happened; graceEnded for
// a device back after its 180 s.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-25: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), iPhone app plan Task 74 (R-IOS-02, R-IOS-30),
//               with AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include "MultiDeviceHarness.h"

namespace {

const QString kWaiting = QStringLiteral("Waiting for you to confirm.");

QList<QJsonObject> messagesOfType(const LoopbackTransport* app, const QString& type)
{
    return ofType(app->received(), type);
}

QJsonObject lastOf(const LoopbackTransport* app, const QString& type)
{
    const QList<QJsonObject> all = messagesOfType(app, type);
    return all.isEmpty() ? QJsonObject{} : all.last();
}

int countOf(const LoopbackTransport* app, const QString& type)
{
    return static_cast<int>(messagesOfType(app, type).size());
}

// The value of `name` in a command.result's `values`, or undefined.
QJsonValue valueOf(const QJsonObject& result, const QString& name)
{
    for (const QJsonValue& v : result.value(QStringLiteral("values")).toArray()) {
        if (v.toObject().value(QStringLiteral("name")).toString() == name) {
            return v.toObject().value(QStringLiteral("value"));
        }
    }
    return {};
}

// A slice's receiver, by slice id.
int streamOf(const Core& core, int sliceId)
{
    const SliceModel* slice = core.model->sliceById(sliceId);
    return slice == nullptr ? -2 : slice->streamIndex();
}

// A property.write of one slice's frequency; waits for its property.result.
QJsonObject writeFrequency(LoopbackTransport* app, int sliceId, double hz, quint32 writeId)
{
    app->sendText(SessionMessages::encode(SessionMessages::propertyWrite(
        ObjectRegistry::keyForSlice(sliceId), {f64("frequency", hz)}, writeId)));
    QJsonObject result;
    const bool answered = QTest::qWaitFor([&]() {
        result = propertyResult(app, writeId);
        return !result.isEmpty();
    }, 5000);
    Q_UNUSED(answered);
    return result;
}

QJsonObject firstResultEntry(const QJsonObject& propertyResultMessage)
{
    return propertyResultMessage.value(QStringLiteral("results")).toArray().first().toObject();
}

// Two devices on one receiver: A (the iPhone) anchors it with slice 0 on
// 7.074 MHz, B (the iPad) has slice 1 on 7.150 MHz, inside A's window. A
// second receiver is free. Receivers are 192 kHz wide (+/- 96 kHz).
struct Shared {
    Core core;
    Device a;
    Device b{QStringLiteral("iPad"), QStringLiteral("tablet")};
    Device c{QStringLiteral("Mac"), QStringLiteral("computer")};
    LoopbackTransport* appA = nullptr;
    LoopbackTransport* appB = nullptr;

    explicit Shared(int receivers = 2, int sliceCap = 5)
    {
        core.model->configureStreamPool(receivers, sliceCap, 192000);
        core.model->sliceById(0)->setFrequency(7074000.0);
        core.pair(a);
        core.pair(b);
        core.pair(c);
        appA = core.signIn(a);
        appB = core.signIn(b);
        core.model->sliceById(1)->setFrequency(7150000.0);
    }

    int receiver() const { return streamOf(core, 0); }

    // C signs in and takes the other receiver with its slice on 20 m.
    LoopbackTransport* cOnTheOtherReceiver(const QHash<QByteArray, int>& features = kHolder)
    {
        LoopbackTransport* appC = core.signIn(c, features);
        const QList<int> own = core.model->sliceOwnership()->ownedBy(c.key.fingerprint());
        if (!own.isEmpty()) {
            core.model->sliceById(own.first())->setFrequency(14074000.0);
        }
        return appC;
    }

    QJsonObject proceed(LoopbackTransport* app, qint64 id, qint64 choice = -1)
    {
        return core.invoke(app, "confirm.proceed", {int64("id", id), int64("choice", choice)});
    }
};

QJsonObject waitForLast(const LoopbackTransport* app, const QString& type, int after)
{
    const bool arrived = QTest::qWaitFor([&]() { return countOf(app, type) > after; }, 5000);
    Q_UNUSED(arrived);
    return lastOf(app, type);
}

// The index of the first message of `type` in what `app` received, or -1.
int indexOfType(const LoopbackTransport* app, const QString& type)
{
    const QList<QByteArray> all = app->received();
    for (int i = 0; i < all.size(); ++i) {
        if (QJsonDocument::fromJson(all.at(i)).object().value(QStringLiteral("type")).toString()
            == type) {
            return i;
        }
    }
    return -1;
}

} // namespace

class TstConfirmStep : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        AppSettings::setProfileOverride(
            QStringLiteral("confirm-step-%1").arg(QCoreApplication::applicationPid()));
        AppSettings::instance().clear();
    }

    void cleanupTestCase()
    {
        const QString path = AppSettings::instance().filePath();
        QFile::remove(path);
        QFile::remove(path + QStringLiteral(".bak"));
    }

    void init() { AppSettings::instance().clear(); }

    // ── Authorisation: the pin and the anchor ────────────────────────────

    void theSetupSharesOneReceiverAnchoredByA()
    {
        Shared s;
        QVERIFY(admitted(s.appA));
        QVERIFY(admitted(s.appB));
        QVERIFY(s.receiver() >= 0);
        QCOMPARE(streamOf(s.core, 1), s.receiver());
        QCOMPARE(s.core.model->sliceOwnership()->anchorOf(s.receiver()), s.a.key.fingerprint());
    }

    void aNonAnchorsPinRequestIsRefusedWithThePlainReason()
    {
        Shared s;
        const QJsonObject refused = s.core.invoke(
            s.appB, "requestStreamCtunPinned",
            {int64("sliceId", 1), MirrorUpdate{0, "pinned", MirrorWireKind::Bool, true}});
        QCOMPARE(refused.value(QStringLiteral("accepted")).toBool(true), false);
        const QString reason = refused.value(QStringLiteral("reason")).toString();
        QCOMPARE(reason, QStringLiteral("This panadapter shows iPhone's receiver. Its C-Tune "
                                        "setting is iPhone's."));
        QVERIFY(OperatorWording::isPlain(reason));
        QVERIFY(!s.core.model->sliceById(0)->streamCtunPinned());
        // The anchor's own pin goes through.
        const QJsonObject pinned = s.core.invoke(
            s.appA, "requestStreamCtunPinned",
            {int64("sliceId", 0), MirrorUpdate{0, "pinned", MirrorWireKind::Bool, true}});
        QCOMPARE(pinned.value(QStringLiteral("accepted")).toBool(false), true);
    }

    void theAnchorsCtuneMoveAsksNamingBThenMovesItsSlice()
    {
        Shared s;
        const int receiver = s.receiver();
        const double centreBefore = s.core.model->streamAllocator().streamCentreHz(receiver);
        const QJsonObject held = s.core.invoke(
            s.appA, "requestStreamCentre", {int64("sliceId", 0), f64("centreHz", 7000000.0)});
        QCOMPARE(held.value(QStringLiteral("accepted")).toBool(true), false);
        QCOMPARE(held.value(QStringLiteral("reason")).toString(), kWaiting);
        QCOMPARE(valueOf(held, QStringLiteral("phase")).toString(),
                 QStringLiteral("needsConfirmation"));
        const QJsonObject ask = waitForLast(s.appA, QStringLiteral("confirm.request"), 0);
        QCOMPARE(ask.value(QStringLiteral("kind")).toString(), QStringLiteral("panMove"));
        QCOMPARE(ask.value(QStringLiteral("reason")).toString(), kWaiting);
        QCOMPARE(ask.value(QStringLiteral("expiresInMs")).toInteger(), 60000);
        QCOMPARE(ask.value(QStringLiteral("forCommandId")).toInteger(),
                 held.value(QStringLiteral("id")).toInteger());
        const QJsonArray affected = ask.value(QStringLiteral("affected")).toArray();
        QCOMPARE(affected.size(), 1);
        const QJsonObject who = affected.first().toObject();
        QCOMPARE(who.value(QStringLiteral("deviceId")).toString(), s.b.id());
        QCOMPARE(who.value(QStringLiteral("deviceName")).toString(), QStringLiteral("iPad"));
        QCOMPARE(who.value(QStringLiteral("state")).toString(), QStringLiteral("listening"));
        const QJsonObject bSlice = who.value(QStringLiteral("slices")).toArray().first().toObject();
        QCOMPARE(bSlice.value(QStringLiteral("sliceId")).toInt(), 1);
        QCOMPARE(bSlice.value(QStringLiteral("letter")).toString(), QStringLiteral("B"));
        QCOMPARE(bSlice.value(QStringLiteral("effect")).toString(), QStringLiteral("moves"));
        QCOMPARE(bSlice.value(QStringLiteral("streamIndex")).toInt(), receiver);
        QCOMPARE(bSlice.value(QStringLiteral("adc")).toInt(), 0);
        QCOMPARE(bSlice.value(QStringLiteral("mode")).toInt(),
                 static_cast<int>(s.core.model->sliceById(1)->dspMode()));
        // Nothing changed before the answer.
        QCOMPARE(s.core.model->streamAllocator().streamCentreHz(receiver), centreBefore);
        QCOMPARE(streamOf(s.core, 1), receiver);
        QCOMPARE(countOf(s.appB, QStringLiteral("notice")), 0);

        const QJsonObject done = s.proceed(s.appA, ask.value(QStringLiteral("id")).toInteger());
        QCOMPARE(done.value(QStringLiteral("accepted")).toBool(false), true);
        // Ruling 7.4a: the command's own answer, as if it had applied at
        // once (requestStreamCentre names the slices on the receiver).
        QCOMPARE(done.value(QStringLiteral("affected")).toArray(),
                 QJsonArray{QStringLiteral("slice:0")});
        QCOMPARE(s.core.model->streamAllocator().streamCentreHz(receiver), 7000000.0);
        QVERIFY(streamOf(s.core, 1) >= 0);
        QVERIFY(streamOf(s.core, 1) != receiver);
        QCOMPARE(s.core.model->sliceById(1)->frequency(), 7150000.0);
        const QJsonObject told = waitForLast(s.appB, QStringLiteral("notice"), 0);
        QCOMPARE(told.value(QStringLiteral("kind")).toString(), QStringLiteral("sliceMoved"));
        QCOMPARE(told.value(QStringLiteral("takeBack")).toBool(true), false);
        QCOMPARE(told.value(QStringLiteral("byName")).toString(), QStringLiteral("iPhone"));
        QCOMPARE(told.value(QStringLiteral("byDeviceId")).toString(), s.a.id());
        QCOMPARE(told.value(QStringLiteral("bySource")).toString(), QStringLiteral("device"));
        QCOMPARE(told.value(QStringLiteral("secondsAgo")).toInteger(), 0);
        QCOMPARE(told.value(QStringLiteral("slices")).toArray().first().toObject()
                     .value(QStringLiteral("letter")).toString(),
                 QStringLiteral("B"));
        QVERIFY(OperatorWording::isPlain(told.value(QStringLiteral("reason")).toString()));
        // A was not told about itself.
        QCOMPARE(countOf(s.appA, QStringLiteral("notice")), 0);
    }

    void withNoReceiverFreeTheOtherSliceClosesAndItsDeviceIsTold()
    {
        Shared s;
        LoopbackTransport* appC = s.cOnTheOtherReceiver();
        QVERIFY(admitted(appC));
        const int receiver = s.receiver();
        QVERIFY(streamOf(s.core, 2) >= 0 && streamOf(s.core, 2) != receiver);
        s.core.invoke(s.appA, "requestStreamCentre",
                      {int64("sliceId", 0), f64("centreHz", 7000000.0)});
        const QJsonObject ask = waitForLast(s.appA, QStringLiteral("confirm.request"), 0);
        QCOMPARE(ask.value(QStringLiteral("affected")).toArray().first().toObject()
                     .value(QStringLiteral("slices")).toArray().first().toObject()
                     .value(QStringLiteral("effect")).toString(),
                 QStringLiteral("closes"));
        QVERIFY(s.core.model->sliceById(1) != nullptr);
        const QJsonObject done = s.proceed(s.appA, ask.value(QStringLiteral("id")).toInteger());
        QCOMPARE(done.value(QStringLiteral("accepted")).toBool(false), true);
        QVERIFY(s.core.model->sliceById(1) == nullptr);
        const QJsonObject told = waitForLast(s.appB, QStringLiteral("notice"), 0);
        QCOMPARE(told.value(QStringLiteral("kind")).toString(), QStringLiteral("sliceClosed"));
        QCOMPARE(told.value(QStringLiteral("takeBack")).toBool(true), false);
        QCOMPARE(told.value(QStringLiteral("slices")).toArray().first().toObject()
                     .value(QStringLiteral("frequencyHz")).toDouble(),
                 7150000.0);
        // C's receiver was never touched.
        QCOMPARE(s.core.model->sliceById(2)->frequency(), 14074000.0);
    }

    void theAnchorsOwnSliceOutsideStaysRefusedAsToday()
    {
        Shared s;
        const QJsonObject refused = s.core.invoke(
            s.appA, "requestStreamCentre", {int64("sliceId", 0), f64("centreHz", 7300000.0)});
        QCOMPARE(refused.value(QStringLiteral("accepted")).toBool(true), false);
        QVERIFY(refused.value(QStringLiteral("reason")).toString() != kWaiting);
        QCOMPARE(countOf(s.appA, QStringLiteral("confirm.request")), 0);
    }

    void aProceedWhoseSetGrewAsksAgainAndAppliesNothing()
    {
        Shared s;
        const int receiver = s.receiver();
        s.core.invoke(s.appA, "requestStreamCentre",
                      {int64("sliceId", 0), f64("centreHz", 7000000.0)});
        const QJsonObject ask = waitForLast(s.appA, QStringLiteral("confirm.request"), 0);
        // C arrives on the same receiver, outside the new window, before
        // the answer.
        LoopbackTransport* appC = s.core.signIn(s.c);
        QVERIFY(admitted(appC));
        const int cSlice = s.core.model->sliceOwnership()->ownedBy(s.c.key.fingerprint()).first();
        s.core.model->sliceById(cSlice)->setFrequency(7160000.0);
        QCOMPARE(streamOf(s.core, cSlice), receiver);

        const QJsonObject again = s.proceed(s.appA, ask.value(QStringLiteral("id")).toInteger());
        QCOMPARE(again.value(QStringLiteral("accepted")).toBool(true), false);
        QCOMPARE(again.value(QStringLiteral("reason")).toString(), kWaiting);
        const QJsonObject asked = waitForLast(s.appA, QStringLiteral("confirm.request"), 1);
        QVERIFY(asked.value(QStringLiteral("id")).toInteger()
                != ask.value(QStringLiteral("id")).toInteger());
        QCOMPARE(asked.value(QStringLiteral("affected")).toArray().size(), 2);
        // Nothing was applied.
        QVERIFY(s.core.model->streamAllocator().streamCentreHz(receiver) != 7000000.0);
        QCOMPARE(streamOf(s.core, 1), receiver);
        QCOMPARE(countOf(s.appB, QStringLiteral("notice")), 0);
        // The old question is gone.
        const QJsonObject stale = s.proceed(s.appA, ask.value(QStringLiteral("id")).toInteger());
        QCOMPARE(stale.value(QStringLiteral("accepted")).toBool(true), false);
    }

    // ── The anchor's band change (ruling 6.5, design ruling 6.5a) ────────

    void theAnchorsBandChangeOnASharedReceiverIsAPanMoveWithBands()
    {
        Shared s;
        const int receiver = s.receiver();
        const QJsonObject held = writeFrequency(s.appA, 0, 14074000.0, 501);
        QCOMPARE(firstResultEntry(held).value(QStringLiteral("accepted")).toBool(true), false);
        QCOMPARE(firstResultEntry(held).value(QStringLiteral("reason")).toString(), kWaiting);
        QCOMPARE(s.core.model->sliceById(0)->frequency(), 7074000.0);
        const QJsonObject ask = waitForLast(s.appA, QStringLiteral("confirm.request"), 0);
        QCOMPARE(ask.value(QStringLiteral("kind")).toString(), QStringLiteral("panMove"));
        QCOMPARE(ask.value(QStringLiteral("forWriteId")).toInteger(), 501);
        const QJsonObject change = ask.value(QStringLiteral("change")).toObject();
        QCOMPARE(change.value(QStringLiteral("label")).toString(),
                 QStringLiteral("Receiver %1").arg(receiver + 1));
        QCOMPARE(change.value(QStringLiteral("from")).toString(), QStringLiteral("40 m"));
        QCOMPARE(change.value(QStringLiteral("to")).toString(), QStringLiteral("20 m"));
        QCOMPARE(ask.value(QStringLiteral("affected")).toArray().first().toObject()
                     .value(QStringLiteral("slices")).toArray().first().toObject()
                     .value(QStringLiteral("effect")).toString(),
                 QStringLiteral("moves"));

        // Proceed: the receiver follows A, re-centred on its new frequency;
        // B's slice moves; the answer carries the readback.
        const QJsonObject done = s.proceed(s.appA, ask.value(QStringLiteral("id")).toInteger());
        QCOMPARE(done.value(QStringLiteral("accepted")).toBool(false), true);
        QCOMPARE(valueOf(done, QStringLiteral("objectKey")).toString(), QStringLiteral("slice:0"));
        QCOMPARE(valueOf(done, QStringLiteral("frequency")).toDouble(), 14074000.0);
        QCOMPARE(s.core.model->sliceById(0)->frequency(), 14074000.0);
        QCOMPARE(streamOf(s.core, 0), receiver);
        QCOMPARE(s.core.model->streamAllocator().streamCentreHz(receiver), 14074000.0);
        QVERIFY(streamOf(s.core, 1) != receiver);
        QCOMPARE(s.core.model->sliceById(1)->frequency(), 7150000.0);
        QCOMPARE(waitForLast(s.appB, QStringLiteral("notice"), 0).value(QStringLiteral("kind"))
                     .toString(),
                 QStringLiteral("sliceMoved"));
    }

    void cancelChangesNothing()
    {
        Shared s;
        const int receiver = s.receiver();
        writeFrequency(s.appA, 0, 14074000.0, 502);
        const QJsonObject ask = waitForLast(s.appA, QStringLiteral("confirm.request"), 0);
        const QJsonObject cancelled = s.core.invoke(
            s.appA, "confirm.cancel", {int64("id", ask.value(QStringLiteral("id")).toInteger())});
        QCOMPARE(cancelled.value(QStringLiteral("accepted")).toBool(false), true);
        QCOMPARE(s.core.model->sliceById(0)->frequency(), 7074000.0);
        QCOMPARE(streamOf(s.core, 1), receiver);
        QCOMPARE(countOf(s.appB, QStringLiteral("notice")), 0);
        const QJsonObject after = s.proceed(s.appA, ask.value(QStringLiteral("id")).toInteger());
        QCOMPARE(after.value(QStringLiteral("accepted")).toBool(true), false);
        QCOMPARE(s.core.model->sliceById(0)->frequency(), 7074000.0);
    }

    void aloneTheBandChangeIsTodaysRetune()
    {
        Shared s;
        // B's slice goes to 20 m first: A is alone on its receiver.
        s.core.model->sliceById(1)->setFrequency(14074000.0);
        QVERIFY(streamOf(s.core, 1) != s.receiver());
        const int receiver = s.receiver();
        const QJsonObject result = writeFrequency(s.appA, 0, 21074000.0, 503);
        QCOMPARE(firstResultEntry(result).value(QStringLiteral("accepted")).toBool(false), true);
        QCOMPARE(countOf(s.appA, QStringLiteral("confirm.request")), 0);
        QCOMPARE(streamOf(s.core, 0), receiver);
        QCOMPARE(s.core.model->streamAllocator().streamCentreHz(receiver), 21074000.0);
    }

    void theAnchorsOwnOtherSliceOutsideMakesItTodaysRetune()
    {
        Shared s;
        const int receiver = s.receiver();
        // A's second slice on the shared receiver.
        const QJsonObject added = s.core.invoke(s.appA, "addSlice", {utf8("initialPanId", QString())});
        QVERIFY(added.value(QStringLiteral("accepted")).toBool());
        const int second = s.core.model->sliceOwnership()->ownedBy(s.a.key.fingerprint()).last();
        s.core.model->sliceById(second)->setFrequency(7080000.0);
        QCOMPARE(streamOf(s.core, second), receiver);
        // A free receiver: slice 0 leaves for it, nobody asked.
        const QJsonObject moved = writeFrequency(s.appA, 0, 14074000.0, 504);
        QCOMPARE(firstResultEntry(moved).value(QStringLiteral("accepted")).toBool(false), true);
        QCOMPARE(countOf(s.appA, QStringLiteral("confirm.request")), 0);
        QVERIFY(streamOf(s.core, 0) != receiver);
        QCOMPARE(streamOf(s.core, 1), receiver);
        QCOMPARE(streamOf(s.core, second), receiver);
    }

    void withNoneFreeTheChooserListsTheSharedReceiverNotTakeable()
    {
        Shared s;
        const int receiver = s.receiver();
        const QJsonObject added = s.core.invoke(s.appA, "addSlice", {utf8("initialPanId", QString())});
        QVERIFY(added.value(QStringLiteral("accepted")).toBool());
        const int second = s.core.model->sliceOwnership()->ownedBy(s.a.key.fingerprint()).last();
        s.core.model->sliceById(second)->setFrequency(7080000.0);
        LoopbackTransport* appC = s.cOnTheOtherReceiver();
        QVERIFY(admitted(appC));
        const QJsonObject refused = writeFrequency(s.appA, 0, 21074000.0, 505);
        const QJsonObject entry = firstResultEntry(refused);
        QCOMPARE(entry.value(QStringLiteral("accepted")).toBool(true), false);
        const QString reason = entry.value(QStringLiteral("reason")).toString();
        QVERIFY2(reason.endsWith(QStringLiteral("The radio's receivers are in use by iPad and Mac.")),
                 qPrintable(reason));
        QVERIFY(OperatorWording::isPlain(reason));
        QCOMPARE(s.core.model->sliceById(0)->frequency(), 7074000.0);
        const QJsonObject ask = waitForLast(s.appA, QStringLiteral("confirm.request"), 0);
        QCOMPARE(ask.value(QStringLiteral("kind")).toString(), QStringLiteral("takeReceiver"));
        QVERIFY(!ask.contains(QStringLiteral("change")));
        const QJsonArray choices = ask.value(QStringLiteral("choices")).toArray();
        QCOMPARE(choices.size(), 2);
        for (const QJsonValue& v : choices) {
            const QJsonObject choice = v.toObject();
            if (choice.value(QStringLiteral("streamIndex")).toInt() == receiver) {
                QCOMPARE(choice.value(QStringLiteral("takeable")).toBool(true), false);
                QCOMPARE(choice.value(QStringLiteral("why")).toString(),
                         QStringLiteral("Your slice %1 would close.")
                             .arg(QChar(QLatin1Char('A').unicode() + second)));
            } else {
                QCOMPARE(choice.value(QStringLiteral("takeable")).toBool(false), true);
                QCOMPARE(choice.value(QStringLiteral("anchorName")).toString(),
                         QStringLiteral("Mac"));
            }
        }
    }

    // ── A device that does not anchor moves its pan (ruling 6.6) ────────

    void aNonAnchorMovesItsPanToAFreeReceiverNobodyAsked()
    {
        Shared s;
        const int receiver = s.receiver();
        const double centre = s.core.model->streamAllocator().streamCentreHz(receiver);
        const QJsonObject moved = s.core.invoke(
            s.appB, "requestStreamCentre", {int64("sliceId", 1), f64("centreHz", 7120000.0)});
        QCOMPARE(moved.value(QStringLiteral("accepted")).toBool(false), true);
        QVERIFY(streamOf(s.core, 1) != receiver);
        QCOMPARE(s.core.model->streamAllocator().streamCentreHz(streamOf(s.core, 1)), 7120000.0);
        // A's receiver did not move, and nobody was asked or told.
        QCOMPARE(s.core.model->streamAllocator().streamCentreHz(receiver), centre);
        QCOMPARE(countOf(s.appA, QStringLiteral("confirm.request")), 0);
        QCOMPARE(countOf(s.appB, QStringLiteral("confirm.request")), 0);
        QCOMPARE(countOf(s.appA, QStringLiteral("notice")), 0);
    }

    void aNonAnchorWithNoReceiverFreeGetsTheChooser()
    {
        Shared s;
        LoopbackTransport* appC = s.cOnTheOtherReceiver();
        QVERIFY(admitted(appC));
        const QJsonObject refused = s.core.invoke(
            s.appB, "requestStreamCentre", {int64("sliceId", 1), f64("centreHz", 7120000.0)});
        QCOMPARE(refused.value(QStringLiteral("accepted")).toBool(true), false);
        QVERIFY(OperatorWording::isPlain(refused.value(QStringLiteral("reason")).toString()));
        const QJsonObject ask = waitForLast(s.appB, QStringLiteral("confirm.request"), 0);
        QCOMPARE(ask.value(QStringLiteral("kind")).toString(), QStringLiteral("takeReceiver"));
        QCOMPARE(ask.value(QStringLiteral("choices")).toArray().size(), 2);
    }

    // ── Taking a receiver (section 6.4, rulings 6.7 to 6.9) ──────────────

    void takingAReceiverClosesTheOthersSlicesAndTakeItBackAsksTheOtherWay()
    {
        Shared s;
        // B's slice on 20 m: each device holds one receiver.
        s.core.model->sliceById(1)->setFrequency(14074000.0);
        const int bReceiver = streamOf(s.core, 1);
        QVERIFY(bReceiver >= 0 && bReceiver != s.receiver());
        const QJsonObject refused = s.core.invoke(s.appA, "addSliceOnPan",
                                                  {utf8("panId", QStringLiteral("pan-a2"))});
        QCOMPARE(refused.value(QStringLiteral("accepted")).toBool(true), false);
        const QString reason = refused.value(QStringLiteral("reason")).toString();
        QVERIFY2(reason.endsWith(QStringLiteral("The radio's receivers are in use by iPad.")),
                 qPrintable(reason));
        const QJsonObject ask = waitForLast(s.appA, QStringLiteral("confirm.request"), 0);
        QCOMPARE(ask.value(QStringLiteral("kind")).toString(), QStringLiteral("takeReceiver"));
        QCOMPARE(ask.value(QStringLiteral("affected")).toArray().size(), 0);
        int choice = -1;
        const QJsonArray choices = ask.value(QStringLiteral("choices")).toArray();
        for (const QJsonValue& v : choices) {
            const QJsonObject c = v.toObject();
            if (c.value(QStringLiteral("streamIndex")).toInt() == bReceiver) {
                choice = c.value(QStringLiteral("choice")).toInt();
                QCOMPARE(c.value(QStringLiteral("takeable")).toBool(false), true);
                const QJsonObject slice = c.value(QStringLiteral("slices")).toArray().first().toObject();
                QCOMPARE(slice.value(QStringLiteral("letter")).toString(), QStringLiteral("B"));
                QCOMPARE(slice.value(QStringLiteral("deviceName")).toString(), QStringLiteral("iPad"));
                QCOMPARE(slice.value(QStringLiteral("frequencyHz")).toDouble(), 14074000.0);
                const QJsonObject device = c.value(QStringLiteral("devices")).toArray().first().toObject();
                QCOMPARE(device.value(QStringLiteral("deviceId")).toString(), s.b.id());
                QCOMPARE(device.value(QStringLiteral("state")).toString(), QStringLiteral("listening"));
                QCOMPARE(c.value(QStringLiteral("anchorName")).toString(), QStringLiteral("iPad"));
            } else {
                // A's own panadapter's receiver cannot give it a new one.
                QCOMPARE(c.value(QStringLiteral("takeable")).toBool(true), false);
            }
        }
        QVERIFY(choice >= 0);
        QVERIFY(s.core.model->sliceById(1) != nullptr);

        const QJsonObject done = s.proceed(s.appA, ask.value(QStringLiteral("id")).toInteger(), choice);
        QVERIFY2(done.value(QStringLiteral("accepted")).toBool(false),
                 QJsonDocument(done).toJson().constData());
        // B's slice closed (its letter may be A's new slice's now).
        QVERIFY(s.core.model->sliceOwnership()->ownedBy(s.b.key.fingerprint()).isEmpty());
        const QString newKey = done.value(QStringLiteral("affected")).toArray().first().toString();
        QVERIFY(newKey.startsWith(QStringLiteral("slice:")));
        const int newSlice = newKey.mid(6).toInt();
        QCOMPARE(streamOf(s.core, newSlice), bReceiver);
        QCOMPARE(s.core.model->sliceOwnership()->mark(newSlice).owner, s.a.key.fingerprint());
        const QJsonObject told = waitForLast(s.appB, QStringLiteral("notice"), 0);
        QCOMPARE(told.value(QStringLiteral("kind")).toString(), QStringLiteral("receiverTaken"));
        QCOMPARE(told.value(QStringLiteral("takeBack")).toBool(false), true);
        QCOMPARE(told.value(QStringLiteral("byName")).toString(), QStringLiteral("iPhone"));
        QCOMPARE(told.value(QStringLiteral("slices")).toArray().first().toObject()
                     .value(QStringLiteral("frequencyHz")).toDouble(),
                 14074000.0);
        QVERIFY(OperatorWording::isPlain(told.value(QStringLiteral("reason")).toString()));

        // Take it back: the same question the other way.
        const QJsonObject back = s.core.invoke(
            s.appB, "notice.takeBack", {int64("id", told.value(QStringLiteral("id")).toInteger())});
        QCOMPARE(back.value(QStringLiteral("reason")).toString(), kWaiting);
        const QJsonObject askBack = waitForLast(s.appB, QStringLiteral("confirm.request"), 0);
        QCOMPARE(askBack.value(QStringLiteral("kind")).toString(), QStringLiteral("takeReceiver"));
        const QJsonObject first = askBack.value(QStringLiteral("choices")).toArray().first().toObject();
        QCOMPARE(first.value(QStringLiteral("streamIndex")).toInt(), bReceiver);
        QCOMPARE(first.value(QStringLiteral("slices")).toArray().first().toObject()
                     .value(QStringLiteral("sliceId")).toInt(),
                 newSlice);
        const QJsonObject returned =
            s.proceed(s.appB, askBack.value(QStringLiteral("id")).toInteger(), 0);
        QCOMPARE(returned.value(QStringLiteral("accepted")).toBool(false), true);
        QVERIFY(s.core.model->sliceById(newSlice) == nullptr
                || s.core.model->sliceOwnership()->mark(newSlice).owner != s.a.key.fingerprint());
        const QList<int> bOwns = s.core.model->sliceOwnership()->ownedBy(s.b.key.fingerprint());
        QCOMPARE(bOwns.size(), 1);
        QCOMPARE(s.core.model->sliceById(bOwns.first())->frequency(), 14074000.0);
        const QJsonObject aTold = waitForLast(s.appA, QStringLiteral("notice"), 0);
        QCOMPARE(aTold.value(QStringLiteral("kind")).toString(), QStringLiteral("receiverTaken"));
        QCOMPARE(aTold.value(QStringLiteral("byName")).toString(), QStringLiteral("iPad"));
        // Taken back once: the record is gone.
        const QJsonObject twice = s.core.invoke(
            s.appB, "notice.takeBack", {int64("id", told.value(QStringLiteral("id")).toInteger())});
        QCOMPARE(twice.value(QStringLiteral("accepted")).toBool(true), false);
        QVERIFY(twice.value(QStringLiteral("reason")).toString() != kWaiting);
    }

    void theSliceCapFullOffersTheSliceChooser()
    {
        Shared s(2, 2);
        QCOMPARE(s.core.model->slices().size(), 2);
        const QJsonObject refused =
            s.core.invoke(s.appA, "addSlice", {utf8("initialPanId", QString())});
        QCOMPARE(refused.value(QStringLiteral("accepted")).toBool(true), false);
        const QJsonObject ask = waitForLast(s.appA, QStringLiteral("confirm.request"), 0);
        QCOMPARE(ask.value(QStringLiteral("kind")).toString(), QStringLiteral("takeSlice"));
        const QJsonArray choices = ask.value(QStringLiteral("choices")).toArray();
        QCOMPARE(choices.size(), 1);
        QCOMPARE(choices.first().toObject().value(QStringLiteral("sliceId")).toInt(), 1);
        QCOMPARE(choices.first().toObject().value(QStringLiteral("deviceName")).toString(),
                 QStringLiteral("iPad"));
        const QJsonObject done = s.proceed(s.appA, ask.value(QStringLiteral("id")).toInteger(), 0);
        QCOMPARE(done.value(QStringLiteral("accepted")).toBool(false), true);
        QCOMPARE(s.core.model->sliceOwnership()->ownedBy(s.b.key.fingerprint()).size(), 0);
        QCOMPARE(s.core.model->sliceOwnership()->ownedBy(s.a.key.fingerprint()).size(), 2);
        const QJsonObject told = waitForLast(s.appB, QStringLiteral("notice"), 0);
        QCOMPARE(told.value(QStringLiteral("kind")).toString(), QStringLiteral("sliceTaken"));
        QCOMPARE(told.value(QStringLiteral("takeBack")).toBool(false), true);
    }

    // ── Notices for an away device (7.4) ─────────────────────────────────

    void aNoticeWaitsForAnAwayDeviceAndArrivesAfterItsSnapshotComplete()
    {
        Shared s;
        s.core.model->sliceById(1)->setFrequency(14074000.0);
        const int bReceiver = streamOf(s.core, 1);
        s.core.now = 1000;
        s.appB->closeLink(QStringLiteral("lost"));
        QTRY_COMPARE(s.core.sessions().entry(s.b.key.fingerprint())->state,
                     DeviceSessionRegistry::State::Away);
        s.core.now = 5000;
        s.core.invoke(s.appA, "addSliceOnPan", {utf8("panId", QStringLiteral("pan-a2"))});
        const QJsonObject ask = waitForLast(s.appA, QStringLiteral("confirm.request"), 0);
        int choice = -1;
        for (const QJsonValue& v : ask.value(QStringLiteral("choices")).toArray()) {
            if (v.toObject().value(QStringLiteral("streamIndex")).toInt() == bReceiver) {
                choice = v.toObject().value(QStringLiteral("choice")).toInt();
                QCOMPARE(v.toObject().value(QStringLiteral("devices")).toArray().first().toObject()
                             .value(QStringLiteral("state")).toString(),
                         QStringLiteral("away"));
            }
        }
        QCOMPARE(s.proceed(s.appA, ask.value(QStringLiteral("id")).toInteger(), choice)
                     .value(QStringLiteral("accepted")).toBool(false),
                 true);
        QVERIFY(s.core.model->sliceOwnership()->ownedBy(s.b.key.fingerprint()).isEmpty());

        s.core.now = 35000;
        LoopbackTransport* back = s.core.signIn(s.b);
        QVERIFY(admitted(back));
        const QJsonObject told = waitForLast(back, QStringLiteral("notice"), 0);
        QCOMPARE(told.value(QStringLiteral("kind")).toString(), QStringLiteral("receiverTaken"));
        QCOMPARE(told.value(QStringLiteral("secondsAgo")).toInteger(), 30);
        QCOMPARE(told.value(QStringLiteral("takeBack")).toBool(false), true);
        QVERIFY(indexOfType(back, QStringLiteral("notice"))
                > indexOfType(back, QStringLiteral("snapshot.complete")));
    }

    void aDeviceBackAfterIts180SecondsGetsGraceEnded()
    {
        Shared s;
        s.core.now = 1000;
        s.appB->closeLink(QStringLiteral("lost"));
        QTRY_COMPARE(s.core.sessions().entry(s.b.key.fingerprint())->state,
                     DeviceSessionRegistry::State::Away);
        s.core.now = 1000 + DeviceSessionRegistry::kGraceMs;
        QCOMPARE(s.core.sessions().expireAway().size(), 1);
        s.core.now = 1000 + DeviceSessionRegistry::kGraceMs + 10000;
        LoopbackTransport* back = s.core.signIn(s.b);
        QVERIFY(admitted(back));
        const QJsonObject told = waitForLast(back, QStringLiteral("notice"), 0);
        QCOMPARE(told.value(QStringLiteral("kind")).toString(), QStringLiteral("graceEnded"));
        QCOMPARE(told.value(QStringLiteral("reason")).toString(),
                 QStringLiteral("You were away for more than 3 minutes. Your slices are back."));
        QCOMPARE(told.value(QStringLiteral("secondsAgo")).toInteger(), 10);
        QCOMPARE(told.value(QStringLiteral("takeBack")).toBool(true), false);
        QVERIFY(!told.contains(QStringLiteral("byName")));
        QVERIFY(indexOfType(back, QStringLiteral("notice"))
                > indexOfType(back, QStringLiteral("snapshot.complete")));
    }

    // ── An older window (10.7, rulings 6.10 and 10.2) ────────────────────

    void anOlderWindowIsRefusedNamingWhoItWouldAffectNeverAsked()
    {
        Core core;
        core.model->configureStreamPool(2, 5, 192000);
        core.model->sliceById(0)->setFrequency(7074000.0);
        Device older(QStringLiteral("Shack PC"), QStringLiteral("computer"));
        Device b(QStringLiteral("Grant's iPhone"), QStringLiteral("phone"));
        core.pair(older);
        core.pair(b);
        LoopbackTransport* appOld = core.signIn(older, {{"deviceAuth", 1}});
        LoopbackTransport* appB = core.signIn(b);
        QVERIFY(admitted(appOld));
        QVERIFY(admitted(appB));
        core.model->sliceById(1)->setFrequency(7150000.0);
        const QJsonObject refused = core.invoke(
            appOld, "requestStreamCentre", {int64("sliceId", 0), f64("centreHz", 7000000.0)});
        QCOMPARE(refused.value(QStringLiteral("accepted")).toBool(true), false);
        const QString reason = refused.value(QStringLiteral("reason")).toString();
        QCOMPARE(reason, QStringLiteral("This change would affect Grant's iPhone. Update NereusSDR "
                                        "to confirm changes that affect other devices."));
        // A name is the operator's own words: checked with it set aside.
        QVERIFY(OperatorWording::isPlain(QString(reason).remove(QStringLiteral("Grant's iPhone"))));
        QCOMPARE(countOf(appOld, QStringLiteral("confirm.request")), 0);
        QCOMPARE(streamOf(core, 1), streamOf(core, 0));
    }

    void takingAnOlderWindowsLastSliceEndsItTakenOver()
    {
        Core core;
        core.model->configureStreamPool(2, 5, 192000);
        core.model->sliceById(0)->setFrequency(7074000.0);
        Device a;
        Device older(QStringLiteral("Shack PC"), QStringLiteral("computer"));
        core.pair(a);
        core.pair(older);
        LoopbackTransport* appA = core.signIn(a);
        LoopbackTransport* appOld = core.signIn(older, {{"deviceAuth", 1}});
        QVERIFY(admitted(appA));
        QVERIFY(admitted(appOld));
        const int oldSlice = core.model->sliceOwnership()->ownedBy(older.key.fingerprint()).first();
        core.model->sliceById(oldSlice)->setFrequency(14074000.0);
        const int oldReceiver = streamOf(core, oldSlice);
        core.invoke(appA, "addSliceOnPan", {utf8("panId", QStringLiteral("pan-a2"))});
        const QJsonObject ask = waitForLast(appA, QStringLiteral("confirm.request"), 0);
        int choice = -1;
        for (const QJsonValue& v : ask.value(QStringLiteral("choices")).toArray()) {
            if (v.toObject().value(QStringLiteral("streamIndex")).toInt() == oldReceiver) {
                choice = v.toObject().value(QStringLiteral("choice")).toInt();
            }
        }
        const QJsonObject done = core.invoke(
            appA, "confirm.proceed",
            {int64("id", ask.value(QStringLiteral("id")).toInteger()), int64("choice", choice)});
        QCOMPARE(done.value(QStringLiteral("accepted")).toBool(false), true);
        const QJsonObject end = endOf(appOld);
        QCOMPARE(end.value(QStringLiteral("reason")).toString(),
                 QStringLiteral("iPhone took the receiver this app was using. Update NereusSDR to "
                                "share the Core."));
        QCOMPARE(end.value(QStringLiteral("retryable")).toBool(true), false);
        QCOMPARE(end.value(QStringLiteral("code")).toString(), QStringLiteral("takenOver"));
        QVERIFY(!core.sessions().entry(older.key.fingerprint()));
        // The older window was never told by a notice.
        QCOMPARE(countOf(appOld, QStringLiteral("notice")), 0);
    }

    void anOlderWindowWithNoSliceAtAdmissionIsRefusedRetryable()
    {
        Shared s(2, 2);
        Device older(QStringLiteral("Shack PC"), QStringLiteral("computer"));
        s.core.pair(older);
        LoopbackTransport* appOld = s.core.signIn(older, {{"deviceAuth", 1}});
        const QJsonObject end = endOf(appOld);
        QCOMPARE(end.value(QStringLiteral("reason")).toString(),
                 QStringLiteral("All the radio's slices are in use. Try again when another device "
                                "closes one."));
        QCOMPARE(end.value(QStringLiteral("retryable")).toBool(false), true);
        QVERIFY(!end.contains(QStringLiteral("code")));
        QVERIFY(!s.core.sessions().entry(older.key.fingerprint()));
        QCOMPARE(s.core.model->slices().size(), 2);
    }
};

QTEST_GUILESS_MAIN(TstConfirmStep)
#include "tst_confirm_step.moc"
