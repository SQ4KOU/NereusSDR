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

#include "core/StepAttenuatorController.h"
#include "core/StepAttenuatorFacade.h"
#include "core/session/ConfirmStep.h"

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

// iPhone app Task 75: two devices each on a receiver of their own, both fed
// from the HL2's one ADC. A (the iPhone) on 7.074 MHz, B (the iPad) on
// 14.074 MHz; the Core's attenuator bound.
struct SharedAdc {
    Core core;
    StepAttenuatorController stepAtt;
    Device a;
    Device b{QStringLiteral("iPad"), QStringLiteral("tablet"), QStringLiteral("iPad")};
    LoopbackTransport* appA = nullptr;
    LoopbackTransport* appB = nullptr;
    quint32 nextWriteId = 900;

    explicit SharedAdc(bool withB = true)
    {
        stepAtt.setTickTimerEnabled(false);
        core.model->setStepAttController(&stepAtt);
        core.model->configureStreamPool(3, 5, 192000);
        core.model->sliceById(0)->setFrequency(7074000.0);
        core.pair(a);
        core.pair(b);
        appA = core.signIn(a);
        if (withB) {
            appB = core.signIn(b);
            core.model->sliceById(bSlice())->setFrequency(14074000.0);
        }
    }

    int bSlice() const { return core.model->sliceOwnership()->ownedBy(b.key.fingerprint()).first(); }

    // A property.write of stepAtt; waits for its property.result.
    QJsonObject writeStepAtt(LoopbackTransport* app, const char* name, qint64 value,
                             MirrorWireKind kind = MirrorWireKind::Int64)
    {
        const quint32 id = nextWriteId++;
        app->sendText(SessionMessages::encode(SessionMessages::propertyWrite(
            "stepAtt", {MirrorUpdate{0, QByteArray(name), kind, QVariant(value)}}, id)));
        QJsonObject result;
        const bool answered = QTest::qWaitFor([&]() {
            result = propertyResult(app, id);
            return !result.isEmpty();
        }, 5000);
        Q_UNUSED(answered);
        return result;
    }

    QJsonObject proceed(LoopbackTransport* app, qint64 id)
    {
        return core.invoke(app, "confirm.proceed", {int64("id", id), int64("choice", -1)});
    }

    int attenuation() const { return core.model->stepAttFacade()->attenuationDb(); }
};

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

    // Fix wave C2 (ruling 5.2, its last paragraph): the phone leaves last,
    // so its slice is held for it; the desktop then takes that receiver.
    // Nobody is there to ask or tell, so the closed slice is saved in the
    // phone's layout store, and its next sign-in restores it with its
    // settings.
    void aHeldSliceATakeClosesIsSavedForItsOwnerAndRestoredAtItsReturn()
    {
        Core core;
        Device phone;
        Device desktop(QStringLiteral("Mac"), QStringLiteral("computer"));
        core.model->configureStreamPool(2, 5, 192000);
        core.model->sliceById(0)->setFrequency(7074000.0);
        core.pair(phone);
        core.pair(desktop);
        LoopbackTransport* appPhone = core.signIn(phone);
        QVERIFY(admitted(appPhone));
        core.model->sliceById(0)->setAfGain(17);
        QVERIFY(core.invoke(appPhone, "session.leave").value(QStringLiteral("accepted")).toBool());
        QTRY_VERIFY(!appPhone->isOpen());
        QCOMPARE(core.model->sliceOwnership()->mark(0).heldFor, phone.key.fingerprint());
        const int heldReceiver = streamOf(core, 0);
        QVERIFY(heldReceiver >= 0);

        LoopbackTransport* appDesk = core.signIn(desktop);
        QVERIFY(admitted(appDesk));
        const int deskSlice =
            core.model->sliceOwnership()->ownedBy(desktop.key.fingerprint()).first();
        core.model->sliceById(deskSlice)->setFrequency(14074000.0);
        QVERIFY(streamOf(core, deskSlice) >= 0 && streamOf(core, deskSlice) != heldReceiver);

        // Every receiver is in use: the desktop takes the held slice's.
        QCOMPARE(core.invoke(appDesk, "addSliceOnPan", {utf8("panId", QStringLiteral("pan-d2"))})
                     .value(QStringLiteral("accepted")).toBool(true),
                 false);
        const QJsonObject ask = waitForLast(appDesk, QStringLiteral("confirm.request"), 0);
        QCOMPARE(ask.value(QStringLiteral("kind")).toString(), QStringLiteral("takeReceiver"));
        int choice = -1;
        for (const QJsonValue& v : ask.value(QStringLiteral("choices")).toArray()) {
            if (v.toObject().value(QStringLiteral("streamIndex")).toInt() == heldReceiver) {
                choice = v.toObject().value(QStringLiteral("choice")).toInt();
            }
        }
        QVERIFY(choice >= 0);
        const QJsonObject done = core.invoke(
            appDesk, "confirm.proceed",
            {int64("id", ask.value(QStringLiteral("id")).toInteger()), int64("choice", choice)});
        QVERIFY2(done.value(QStringLiteral("accepted")).toBool(false),
                 QJsonDocument(done).toJson().constData());
        QVERIFY(core.model->sliceOwnership()->heldFor(phone.key.fingerprint()).isEmpty());

        // Saved for the phone, with its settings.
        const QString mac = core.model->currentRadioMac();
        const QList<SavedSlice> saved =
            DeviceLayoutStore::load(AppSettings::instance(), mac, phone.key.fingerprint());
        QCOMPARE(saved.size(), 1);
        QCOMPARE(saved.first().id, 0);
        QCOMPARE(saved.first().frequencyHz, 7074000.0);
        QCOMPARE(saved.first().settings.value(QStringLiteral("Slice/AfGain")), QStringLiteral("17"));

        // The desktop closes its new slice, freeing the receiver.
        const QString newKey = done.value(QStringLiteral("affected")).toArray().first().toString();
        QVERIFY(newKey.startsWith(QStringLiteral("slice:")));
        QVERIFY(core.invoke(appDesk, "removeSlice", {int64("sliceId", newKey.mid(6).toInt())})
                    .value(QStringLiteral("accepted")).toBool());

        // The phone signs in again and has its slice back.
        LoopbackTransport* back = core.signIn(phone);
        QVERIFY(admitted(back));
        const QList<int> own = core.model->sliceOwnership()->ownedBy(phone.key.fingerprint());
        QCOMPARE(own.size(), 1);
        QCOMPARE(core.model->sliceById(own.first())->frequency(), 7074000.0);
        QCOMPARE(core.model->sliceById(own.first())->afGain(), 17);
        QVERIFY(core.server->slicesNotRestored(phone.key.fingerprint()).isEmpty());
        QVERIFY(DeviceLayoutStore::load(AppSettings::instance(), mac, phone.key.fingerprint())
                    .isEmpty());
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

    // ── iPhone app Task 75: settings that affect every device ────────────

    void aloneAnAttenuatorChangeAppliesAtOnce()
    {
        SharedAdc s(false);
        QVERIFY(admitted(s.appA));
        const QJsonObject result = s.writeStepAtt(s.appA, "attenuationDb", 20);
        QCOMPARE(firstResultEntry(result).value(QStringLiteral("accepted")).toBool(false), true);
        QCOMPARE(s.attenuation(), 20);
        QCOMPARE(countOf(s.appA, QStringLiteral("confirm.request")), 0);
    }

    void anAttenuatorChangeWithAnotherDeviceOnTheAdcAsksThenTellsIt()
    {
        SharedAdc s;
        QVERIFY(admitted(s.appB));
        const int bSlice = s.bSlice();
        const int bReceiver = streamOf(s.core, bSlice);
        QVERIFY(bReceiver >= 0);
        QVERIFY(bReceiver != streamOf(s.core, 0));
        const QJsonObject held = s.writeStepAtt(s.appA, "attenuationDb", 20);
        QCOMPARE(firstResultEntry(held).value(QStringLiteral("accepted")).toBool(true), false);
        QCOMPARE(firstResultEntry(held).value(QStringLiteral("reason")).toString(), kWaiting);
        QCOMPARE(s.attenuation(), 0);

        const QJsonObject ask = waitForLast(s.appA, QStringLiteral("confirm.request"), 0);
        QCOMPARE(ask.value(QStringLiteral("kind")).toString(), QStringLiteral("sharedSetting"));
        QCOMPARE(ask.value(QStringLiteral("reason")).toString(), kWaiting);
        QCOMPARE(ask.value(QStringLiteral("expiresInMs")).toInteger(), 60000);
        QVERIFY(ask.contains(QStringLiteral("forWriteId")));
        const QJsonObject change = ask.value(QStringLiteral("change")).toObject();
        QCOMPARE(change.value(QStringLiteral("label")).toString(), QStringLiteral("Attenuator, ADC 1"));
        QCOMPARE(change.value(QStringLiteral("from")).toString(), QStringLiteral("0 dB"));
        QCOMPARE(change.value(QStringLiteral("to")).toString(), QStringLiteral("20 dB"));
        const QJsonArray affected = ask.value(QStringLiteral("affected")).toArray();
        QCOMPARE(affected.size(), 1);
        const QJsonObject who = affected.first().toObject();
        QCOMPARE(who.value(QStringLiteral("deviceName")).toString(), QStringLiteral("iPad"));
        QCOMPARE(who.value(QStringLiteral("deviceShortName")).toString(), QStringLiteral("iPad"));
        QCOMPARE(who.value(QStringLiteral("state")).toString(), QStringLiteral("listening"));
        QCOMPARE(who.value(QStringLiteral("holdsTransmit")).toBool(true), false);
        const QJsonObject slice = who.value(QStringLiteral("slices")).toArray().first().toObject();
        QCOMPARE(slice.value(QStringLiteral("sliceId")).toInt(), bSlice);
        QCOMPARE(slice.value(QStringLiteral("mode")).toInt(),
                 static_cast<int>(s.core.model->sliceById(bSlice)->dspMode()));
        QCOMPARE(slice.value(QStringLiteral("adc")).toInt(), 0);
        QCOMPARE(slice.value(QStringLiteral("streamIndex")).toInt(), bReceiver);
        QCOMPARE(slice.value(QStringLiteral("effect")).toString(), QStringLiteral("changes"));
        QCOMPARE(countOf(s.appB, QStringLiteral("notice")), 0);

        s.core.now = 7000;
        const QJsonObject done = s.proceed(s.appA, ask.value(QStringLiteral("id")).toInteger());
        QCOMPARE(done.value(QStringLiteral("accepted")).toBool(false), true);
        QCOMPARE(valueOf(done, QStringLiteral("objectKey")).toString(), QStringLiteral("stepAtt"));
        QCOMPARE(valueOf(done, QStringLiteral("attenuationDb")).toInt(), 20);
        QCOMPARE(s.attenuation(), 20);

        const QJsonObject told = waitForLast(s.appB, QStringLiteral("notice"), 0);
        QCOMPARE(told.value(QStringLiteral("kind")).toString(), QStringLiteral("settingChanged"));
        QCOMPARE(told.value(QStringLiteral("byName")).toString(), QStringLiteral("iPhone"));
        QCOMPARE(told.value(QStringLiteral("bySource")).toString(), QStringLiteral("device"));
        QCOMPARE(told.value(QStringLiteral("secondsAgo")).toInteger(), 0);
        QCOMPARE(told.value(QStringLiteral("takeBack")).toBool(true), false);
        QCOMPARE(told.value(QStringLiteral("change")).toObject(), change);
        QCOMPARE(told.value(QStringLiteral("reason")).toString(),
                 QStringLiteral("iPhone changed Attenuator, ADC 1 from 0 dB to 20 dB."));
        QCOMPARE(told.value(QStringLiteral("slices")).toArray().first().toObject()
                     .value(QStringLiteral("sliceId")).toInt(),
                 bSlice);
        // The asker is not told.
        QCOMPARE(countOf(s.appA, QStringLiteral("notice")), 0);
    }

    void aSharedQuestionExpiresSixtySecondsAfterItWasSent()
    {
        SharedAdc s;
        s.core.now = 1000;
        s.writeStepAtt(s.appA, "attenuationDb", 20);
        const QJsonObject ask = waitForLast(s.appA, QStringLiteral("confirm.request"), 0);
        s.core.now = 1000 + ConfirmStep::kExpiryMs;
        const QJsonObject late = s.proceed(s.appA, ask.value(QStringLiteral("id")).toInteger());
        QCOMPARE(late.value(QStringLiteral("accepted")).toBool(true), false);
        QCOMPARE(late.value(QStringLiteral("reason")).toString(),
                 QStringLiteral("That question has expired. Make the change again."));
        QCOMPARE(s.attenuation(), 0);
        QCOMPARE(countOf(s.appB, QStringLiteral("notice")), 0);

        // One millisecond short of it, the question is still open.
        s.writeStepAtt(s.appA, "attenuationDb", 20);
        const QJsonObject again = waitForLast(s.appA, QStringLiteral("confirm.request"), 1);
        s.core.now += ConfirmStep::kExpiryMs - 1;
        QCOMPARE(s.proceed(s.appA, again.value(QStringLiteral("id")).toInteger())
                     .value(QStringLiteral("accepted")).toBool(false),
                 true);
        QCOMPARE(s.attenuation(), 20);
    }

    void aPanMoveQuestionExpiresToo()
    {
        Shared s;
        s.core.now = 1000;
        s.core.invoke(s.appA, "requestStreamCentre", {int64("sliceId", 0), f64("centreHz", 7000000.0)});
        const QJsonObject ask = waitForLast(s.appA, QStringLiteral("confirm.request"), 0);
        QCOMPARE(ask.value(QStringLiteral("kind")).toString(), QStringLiteral("panMove"));
        s.core.now = 1000 + ConfirmStep::kExpiryMs;
        const QJsonObject late = s.proceed(s.appA, ask.value(QStringLiteral("id")).toInteger());
        QCOMPARE(late.value(QStringLiteral("reason")).toString(),
                 QStringLiteral("That question has expired. Make the change again."));
        QCOMPARE(streamOf(s.core, 1), s.receiver());
    }

    void aSecondDisturbingChangeReplacesTheFirst()
    {
        SharedAdc s;
        s.writeStepAtt(s.appA, "attenuationDb", 20);
        const QJsonObject first = waitForLast(s.appA, QStringLiteral("confirm.request"), 0);
        s.writeStepAtt(s.appA, "enabled", 0, MirrorWireKind::Bool);
        const QJsonObject second = waitForLast(s.appA, QStringLiteral("confirm.request"), 1);
        QVERIFY(second.value(QStringLiteral("id")).toInteger()
                != first.value(QStringLiteral("id")).toInteger());
        const QJsonObject stale = s.proceed(s.appA, first.value(QStringLiteral("id")).toInteger());
        QCOMPARE(stale.value(QStringLiteral("accepted")).toBool(true), false);
        QCOMPARE(stale.value(QStringLiteral("reason")).toString(),
                 QStringLiteral("That question is no longer open. Make the change again."));
        QCOMPARE(s.attenuation(), 0);
    }

    void aNewWriteToTheSameTargetCancelsTheQuestion()
    {
        SharedAdc s;
        s.writeStepAtt(s.appA, "attenuationDb", 20);
        const QJsonObject ask = waitForLast(s.appA, QStringLiteral("confirm.request"), 0);
        // Back to the value it already has: no change, nobody asked, and
        // the open question goes (ruling 7.6).
        const QJsonObject same = s.writeStepAtt(s.appA, "attenuationDb", 0);
        QCOMPARE(firstResultEntry(same).value(QStringLiteral("accepted")).toBool(false), true);
        QCOMPARE(countOf(s.appA, QStringLiteral("confirm.request")), 1);
        const QJsonObject stale = s.proceed(s.appA, ask.value(QStringLiteral("id")).toInteger());
        QCOMPARE(stale.value(QStringLiteral("reason")).toString(),
                 QStringLiteral("That question is no longer open. Make the change again."));
        QCOMPARE(s.attenuation(), 0);
    }

    void aProceedAfterTheTargetChangedIsRefused()
    {
        SharedAdc s;
        s.writeStepAtt(s.appA, "attenuationDb", 20);
        const QJsonObject ask = waitForLast(s.appA, QStringLiteral("confirm.request"), 0);
        // The Core's own change moves the target (ruling 7.6: whoever
        // changed it).
        s.core.model->stepAttFacade()->setAttenuationDb(10);
        QCOMPARE(s.attenuation(), 10);
        const QJsonObject refused = s.proceed(s.appA, ask.value(QStringLiteral("id")).toInteger());
        QCOMPARE(refused.value(QStringLiteral("accepted")).toBool(true), false);
        QCOMPARE(refused.value(QStringLiteral("reason")).toString(),
                 QStringLiteral("That setting changed since you asked. Make the change again."));
        QVERIFY(refused.value(QStringLiteral("values")).toArray().isEmpty());
        QCOMPARE(s.attenuation(), 10);
        QCOMPARE(countOf(s.appB, QStringLiteral("notice")), 0);
    }

    void aSharedProceedWhoseSetGrewAsksAgainAndAppliesNothing()
    {
        SharedAdc s;
        s.writeStepAtt(s.appA, "attenuationDb", 20);
        const QJsonObject ask = waitForLast(s.appA, QStringLiteral("confirm.request"), 0);
        // B opens a second slice on the ADC before A answers.
        QCOMPARE(s.core.invoke(s.appB, "addSliceOnPan", {utf8("panId", QStringLiteral("pan-b2"))})
                     .value(QStringLiteral("accepted")).toBool(false),
                 true);
        const QJsonObject again = s.proceed(s.appA, ask.value(QStringLiteral("id")).toInteger());
        QCOMPARE(again.value(QStringLiteral("accepted")).toBool(true), false);
        QCOMPARE(again.value(QStringLiteral("reason")).toString(), kWaiting);
        QCOMPARE(valueOf(again, QStringLiteral("phase")).toString(),
                 QStringLiteral("needsConfirmation"));
        const QJsonObject asked = waitForLast(s.appA, QStringLiteral("confirm.request"), 1);
        QCOMPARE(asked.value(QStringLiteral("affected")).toArray().first().toObject()
                     .value(QStringLiteral("slices")).toArray().size(),
                 2);
        QCOMPARE(s.attenuation(), 0);
        // The original write is not answered twice.
        QCOMPARE(countOf(s.appA, QStringLiteral("property.result")), 1);
        QCOMPARE(s.proceed(s.appA, asked.value(QStringLiteral("id")).toInteger())
                     .value(QStringLiteral("accepted")).toBool(false),
                 true);
        QCOMPARE(s.attenuation(), 20);
    }

    void aSharedQuestionIsDroppedWhenTheSessionEnds()
    {
        SharedAdc s;
        s.writeStepAtt(s.appA, "attenuationDb", 20);
        const QJsonObject ask = waitForLast(s.appA, QStringLiteral("confirm.request"), 0);
        s.appA->closeLink(QStringLiteral("lost"));
        QTRY_COMPARE(s.core.sessions().entry(s.a.key.fingerprint())->state,
                     DeviceSessionRegistry::State::Away);
        LoopbackTransport* back = s.core.signIn(s.a);
        QVERIFY(admitted(back));
        const QJsonObject stale = s.proceed(back, ask.value(QStringLiteral("id")).toInteger());
        QCOMPARE(stale.value(QStringLiteral("reason")).toString(),
                 QStringLiteral("That question is no longer open. Make the change again."));
        QCOMPARE(s.attenuation(), 0);
    }

    void aSettingsWriteIsHeldAndItsProceedCarriesTheReadback()
    {
        SharedAdc s;
        const QString key = QStringLiteral("DspOptionsBufferSizePhoneRx");
        const QString before = s.core.settings->value(key).toString();
        s.appA->sendText(SessionMessages::encode(
            SessionMessages::settingsWrite(key, QStringLiteral("1024"), QStringLiteral("a-1"))));
        const QJsonObject reject = waitForLast(s.appA, QStringLiteral("settings.reject"), 0);
        QCOMPARE(reject.value(QStringLiteral("key")).toString(), key);
        QCOMPARE(reject.value(QStringLiteral("reason")).toString(), kWaiting);
        const QJsonObject ask = waitForLast(s.appA, QStringLiteral("confirm.request"), 0);
        QCOMPARE(ask.value(QStringLiteral("forSettingsKey")).toString(), key);
        QCOMPARE(ask.value(QStringLiteral("change")).toObject().value(QStringLiteral("label")).toString(),
                 QStringLiteral("Receive buffer size, voice modes"));
        QCOMPARE(s.core.settings->value(key).toString(), before);

        const QJsonObject done = s.proceed(s.appA, ask.value(QStringLiteral("id")).toInteger());
        QCOMPARE(done.value(QStringLiteral("accepted")).toBool(false), true);
        QCOMPARE(valueOf(done, QStringLiteral("settingsKey")).toString(), key);
        QCOMPARE(valueOf(done, QStringLiteral("value")).toString(), QStringLiteral("1024"));
        QCOMPARE(s.core.settings->value(key).toString(), QStringLiteral("1024"));
        QCOMPARE(waitForLast(s.appB, QStringLiteral("notice"), 0).value(QStringLiteral("kind")).toString(),
                 QStringLiteral("settingChanged"));
    }

    void aNotchInsideAnotherDevicesPassbandAsksOneOutsideDoesNot()
    {
        SharedAdc s;
        const int notches = static_cast<int>(s.core.model->notchModel()->notches().size());
        // Outside B's passband (14.0741 to 14.077 MHz in USB): at once.
        QCOMPARE(s.core.invoke(s.appA, "notch.add",
                               {int64("sliceId", 0), f64("centreHz", 7075000.0), f64("widthHz", 100.0)})
                     .value(QStringLiteral("accepted")).toBool(false),
                 true);
        QCOMPARE(countOf(s.appA, QStringLiteral("confirm.request")), 0);
        QTRY_COMPARE(static_cast<int>(s.core.model->notchModel()->notches().size()), notches + 1);
        // Inside it: asked, naming B.
        const QJsonObject held = s.core.invoke(
            s.appA, "notch.add", {int64("sliceId", 0), f64("centreHz", 14075000.0), f64("widthHz", 100.0)});
        QCOMPARE(held.value(QStringLiteral("reason")).toString(), kWaiting);
        const QJsonObject ask = waitForLast(s.appA, QStringLiteral("confirm.request"), 0);
        QCOMPARE(ask.value(QStringLiteral("affected")).toArray().first().toObject()
                     .value(QStringLiteral("deviceName")).toString(),
                 QStringLiteral("iPad"));
        QCOMPARE(static_cast<int>(s.core.model->notchModel()->notches().size()), notches + 1);
    }

    void anOlderWindowsSharedChangeIsRefusedNamingWhoItWouldAffect()
    {
        SharedAdc s(false);
        Device older(QStringLiteral("Shack PC"), QStringLiteral("computer"));
        s.core.pair(older);
        LoopbackTransport* appOld = s.core.signIn(older, {{"deviceAuth", 1}});
        QVERIFY(admitted(appOld));
        const QJsonObject refused = s.writeStepAtt(appOld, "attenuationDb", 20);
        const QString reason = firstResultEntry(refused).value(QStringLiteral("reason")).toString();
        QCOMPARE(reason, QStringLiteral("This change would affect iPhone. Update NereusSDR to "
                                        "confirm changes that affect other devices."));
        QCOMPARE(firstResultEntry(refused).value(QStringLiteral("accepted")).toBool(true), false);
        QCOMPARE(s.attenuation(), 0);
        QCOMPARE(countOf(appOld, QStringLiteral("confirm.request")), 0);
    }

    void aNarrowerRateOnASharedReceiverMovesTheOtherDevicesSlice()
    {
        // No connection: the rate is per receiver (Protocol 2's rule). B's
        // slice, 76 kHz from A's, no longer fits a 96 kHz window: the plan
        // moves it to the free receiver (ruling 7.3).
        Shared s;
        const int receiver = s.receiver();
        const QJsonObject held = s.core.invoke(
            s.appA, "requestSliceSampleRate", {int64("sliceId", 0), int64("rateHz", 96000)});
        QCOMPARE(held.value(QStringLiteral("reason")).toString(), kWaiting);
        const QJsonObject ask = waitForLast(s.appA, QStringLiteral("confirm.request"), 0);
        const QJsonObject change = ask.value(QStringLiteral("change")).toObject();
        QCOMPARE(change.value(QStringLiteral("label")).toString(),
                 QStringLiteral("Sample rate, Receiver %1").arg(receiver + 1));
        QCOMPARE(change.value(QStringLiteral("from")).toString(), QStringLiteral("192 kHz"));
        QCOMPARE(change.value(QStringLiteral("to")).toString(), QStringLiteral("96 kHz"));
        const QJsonObject slice = ask.value(QStringLiteral("affected")).toArray().first().toObject()
                                      .value(QStringLiteral("slices")).toArray().first().toObject();
        QCOMPARE(slice.value(QStringLiteral("sliceId")).toInt(), 1);
        QCOMPARE(slice.value(QStringLiteral("effect")).toString(), QStringLiteral("moves"));
        QCOMPARE(ask.value(QStringLiteral("forCommandId")).toInteger(),
                 held.value(QStringLiteral("id")).toInteger());
        QCOMPARE(streamOf(s.core, 1), receiver);

        const QJsonObject done = s.proceed(s.appA, ask.value(QStringLiteral("id")).toInteger());
        QCOMPARE(done.value(QStringLiteral("accepted")).toBool(false), true);
        QCOMPARE(s.core.model->streamAllocator().streamSampleRateHz(receiver), 96000);
        QVERIFY(streamOf(s.core, 1) != receiver);
        QVERIFY(streamOf(s.core, 1) >= 0);
        const QJsonObject told = waitForLast(s.appB, QStringLiteral("notice"), 0);
        QCOMPARE(told.value(QStringLiteral("kind")).toString(), QStringLiteral("settingChanged"));
        QCOMPARE(told.value(QStringLiteral("reason")).toString(),
                 QStringLiteral("iPhone changed Sample rate, Receiver %1 from 192 kHz to 96 kHz. "
                                "Your slice B moved to another receiver.")
                     .arg(receiver + 1));
    }

    // Fix wave I1: a proceed answered when its held rate change's result
    // arrives is keyed by its session. B's own rate change, sent with the
    // same command id as A's held one, is B's result, never A's proceed
    // answer, and A's result never reaches B.
    void aDeferredProceedIsNotFinishedByAnotherDevicesResultWithTheSameId()
    {
        Shared s(3);
        QVERIFY(s.core.invoke(s.appB, "addSliceOnPan", {utf8("panId", QStringLiteral("pan-b2"))})
                    .value(QStringLiteral("accepted")).toBool());
        const int bOwn = s.core.model->sliceOwnership()->ownedBy(s.b.key.fingerprint()).last();
        s.core.model->sliceById(bOwn)->setFrequency(14074000.0);
        QVERIFY(streamOf(s.core, bOwn) != s.receiver());
        const quint32 sameId = 7777;
        const auto resultsFor = [](const LoopbackTransport* app, quint32 id) {
            QList<QJsonObject> out;
            for (const QJsonObject& o : ofType(app->received(), QStringLiteral("command.result"))) {
                if (o.value(QStringLiteral("id")).toInteger() == id) {
                    out.append(o);
                }
            }
            return out;
        };
        s.appA->sendText(SessionMessages::encode(SessionMessages::commandInvoke(
            "requestSliceSampleRate", sameId, {int64("sliceId", 0), int64("rateHz", 96000)})));
        QTRY_VERIFY(!resultsFor(s.appA, sameId).isEmpty());
        QCOMPARE(resultsFor(s.appA, sameId).first().value(QStringLiteral("reason")).toString(),
                 kWaiting);
        const QJsonObject ask = waitForLast(s.appA, QStringLiteral("confirm.request"), 0);
        QCOMPARE(ask.value(QStringLiteral("kind")).toString(), QStringLiteral("sharedSetting"));
        // B's own rate change first, then A's proceed, before the Core's
        // event loop runs either change.
        const quint32 proceedId = 7778;
        s.appB->sendText(SessionMessages::encode(SessionMessages::commandInvoke(
            "requestSliceSampleRate", sameId, {int64("sliceId", bOwn), int64("rateHz", 96000)})));
        s.appA->sendText(SessionMessages::encode(SessionMessages::commandInvoke(
            "confirm.proceed", proceedId,
            {int64("id", ask.value(QStringLiteral("id")).toInteger()), int64("choice", -1)})));
        QTRY_VERIFY(!resultsFor(s.appA, proceedId).isEmpty() && !resultsFor(s.appB, sameId).isEmpty());
        QTest::qWait(2 * StationServer::kDefaultDeltaFlushMs);
        const QList<QJsonObject> proceeds = resultsFor(s.appA, proceedId);
        QCOMPARE(proceeds.size(), 1);
        QVERIFY2(proceeds.first().value(QStringLiteral("accepted")).toBool(false),
                 QJsonDocument(proceeds.first()).toJson().constData());
        const QStringList proceedAffected = [&]() {
            QStringList keys;
            for (const QJsonValue& v : proceeds.first().value(QStringLiteral("affected")).toArray()) {
                keys.append(v.toString());
            }
            return keys;
        }();
        QVERIFY2(proceedAffected.contains(QStringLiteral("slice:0")),
                 qPrintable(proceedAffected.join(QLatin1Char(','))));
        QVERIFY(!proceedAffected.contains(QStringLiteral("slice:%1").arg(bOwn)));
        // B hears its own result, once, and A's rate change is not it.
        const QList<QJsonObject> bResults = resultsFor(s.appB, sameId);
        QCOMPARE(bResults.size(), 1);
        QVERIFY(bResults.first().value(QStringLiteral("accepted")).toBool(false));
        for (const QJsonValue& v : bResults.first().value(QStringLiteral("affected")).toArray()) {
            QVERIFY2(v.toString() != QStringLiteral("slice:0"), "A's result reached B");
        }
        // A heard no second answer to its held rate change.
        QCOMPARE(resultsFor(s.appA, sameId).size(), 1);
        QCOMPARE(s.core.model->streamAllocator().streamSampleRateHz(s.receiver()), 96000);
    }

    void aNarrowerRateWithNoReceiverFreeClosesTheOtherDevicesSlice()
    {
        Shared s(1);
        const QJsonObject held = s.core.invoke(
            s.appA, "requestSliceSampleRate", {int64("sliceId", 0), int64("rateHz", 96000)});
        QCOMPARE(held.value(QStringLiteral("reason")).toString(), kWaiting);
        const QJsonObject ask = waitForLast(s.appA, QStringLiteral("confirm.request"), 0);
        QCOMPARE(ask.value(QStringLiteral("affected")).toArray().first().toObject()
                     .value(QStringLiteral("slices")).toArray().first().toObject()
                     .value(QStringLiteral("effect")).toString(),
                 QStringLiteral("closes"));
        QCOMPARE(s.proceed(s.appA, ask.value(QStringLiteral("id")).toInteger())
                     .value(QStringLiteral("accepted")).toBool(false),
                 true);
        QVERIFY(s.core.model->sliceById(1) == nullptr);
        QCOMPARE(s.core.model->streamAllocator().streamSampleRateHz(s.receiver()), 96000);
        QVERIFY(waitForLast(s.appB, QStringLiteral("notice"), 0)
                    .value(QStringLiteral("reason")).toString()
                    .endsWith(QStringLiteral("Your slice B closed: no receiver was free.")));
    }
};

QTEST_GUILESS_MAIN(TstConfirmStep)
#include "tst_confirm_step.moc"
