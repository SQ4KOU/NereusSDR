// =================================================================
// tests/tst_mirror_inbound.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test infrastructure.
//
// Remote Daemon R2 Task 8, inbound half: StateMirror::applyInbound()
// applies one write a remote peer sent for a watched object's property.
//
// Three things this file pins that nothing else does:
//
//   1. A property with a real Q_PROPERTY WRITE goes through
//      MirrorPolicy::inboundAllowed() then MirrorSchema::write(). The
//      seven SliceModel properties that carry WRITE but are Outbound
//      (chainIndex, ddcIndex, streamIndex, shiftOffsetHz, sampleRateHz,
//      widebandExtensionRequested, psPaused -- MirrorPolicy.cpp) are
//      refused with a reason; sampleRateHz is the one the R2 plan names a
//      client verb for (requestSliceSampleRate), so its rejection must
//      name that verb literally.
//
//   2. A property with NO Q_PROPERTY WRITE at all (the ATU surface on
//      TunerModel, plus active/txSlice/band on SliceModel and
//      name/model/version on RadioModel) routes to that model's own
//      applyMirroredValue(name, value) hook instead -- a SEPARATE channel
//      from MirrorPolicy, because MirrorPolicy always classifies these
//      Outbound (there is nothing standard for it to write through) and
//      that guard is pinned for good in tst_mirror_schema's
//      everyReadOnlyPropertyIsDeniedInbound(). sliceIndex (CONSTANT) is
//      the one no-WRITE property that must NOT reach the hook; it is the
//      mirror's object identity and travels only in the snapshot.
//
//   3. Applying a write that genuinely changes a property's value must
//      produce NO outbound delta, for the object written AND for any
//      other watched object a same-thread, synchronous side effect of
//      that write touches -- RadioModel::addSlice()'s co-hosted-slice
//      NB-mode/NB-tuning peer mirrors are wired unconditionally
//      (RadioModel.cpp, inside addSlice(), same reasoning as the
//      frequencyChanged TX rollback handler beside them) and are exactly
//      this case: peer->setNbMode() fires nbModeChanged on the PEER
//      object, which a naive forwarder would send outbound as a delta the
//      remote peer never asked for. Task 7's own forwarder tests already
//      proved that applying an ALREADY-current value produces no NOTIFY
//      at all (SliceModel's setters are uniformly change-guarded), which
//      would make every test below pass whether or not the guard here
//      works; every value applied in this file is checked to differ from
//      what was there before.
// =================================================================

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QVariant>

#include "core/session/MirrorPolicy.h"
#include "core/session/MirrorSchema.h"
#include "core/session/StateMirror.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/TunerModel.h"

using namespace NereusSDR;

namespace {

// Flattens every batch StateMirror emits, same shape as
// tst_mirror_forwarder.cpp's Collector (separate translation unit, so its
// own copy).
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

} // namespace

class TestMirrorInbound : public QObject {
    Q_OBJECT

private slots:

    // ── Bidirectional properties: the standard write path ──────────────────

    void bidirectionalWriteLandsThroughTheRealSetter()
    {
        SliceModel slice(0);
        QVERIFY(slice.frequency() != 7100000.0); // genuinely different, not vacuous
        StateMirror mirror;
        QVERIFY(mirror.watch("slice:0", &slice));

        const MirrorApplyResult result =
            mirror.applyInbound("slice:0", "frequency", QVariant(7100000.0));

        QVERIFY2(result.accepted, qPrintable(result.reason));
        QVERIFY(result.reason.isEmpty());
        QCOMPARE(slice.frequency(), 7100000.0);
    }

    void bidirectionalEnumWriteDecodesThroughTheUnderlyingInteger()
    {
        SliceModel slice(0);
        QCOMPARE(slice.nbMode(), NbMode::Off); // ctor default; genuinely changing it below
        StateMirror mirror;
        QVERIFY(mirror.watch("slice:0", &slice));

        const MirrorApplyResult result = mirror.applyInbound(
            "slice:0", "nbMode", QVariant(static_cast<int>(NbMode::NB)));

        QVERIFY2(result.accepted, qPrintable(result.reason));
        QCOMPARE(slice.nbMode(), NbMode::NB);
    }

    // ── The seven writable-but-Outbound properties ──────────────────────────

    void sampleRateHzIsRejectedNamingTheVerbAndLeavesThePropertyUnchanged()
    {
        SliceModel slice(0);
        const int before = slice.sampleRateHz();
        QVERIFY(before != 768000); // genuinely different value being attempted
        StateMirror mirror;
        QVERIFY(mirror.watch("slice:0", &slice));

        const MirrorApplyResult result =
            mirror.applyInbound("slice:0", "sampleRateHz", QVariant(768000));

        QVERIFY(!result.accepted);
        QVERIFY2(result.reason.contains("requestSliceSampleRate"),
                 qPrintable(result.reason));
        QCOMPARE(slice.sampleRateHz(), before);
    }

    // The other six carry WRITE and are Outbound too, but the R2 plan names
    // no client verb for them yet (only sampleRateHz). The rejection must
    // still refuse, still leave the property untouched, and must NOT
    // fabricate a verb name nobody has assigned.
    void otherWritableOutboundPropertiesAreRejectedWithoutInventingAVerb()
    {
        SliceModel slice(0);
        const bool before = slice.psPaused();
        StateMirror mirror;
        QVERIFY(mirror.watch("slice:0", &slice));

        const MirrorApplyResult result =
            mirror.applyInbound("slice:0", "psPaused", QVariant(!before));

        QVERIFY(!result.accepted);
        QVERIFY(!result.reason.isEmpty());
        QVERIFY2(!result.reason.contains("requestSliceSampleRate"),
                 "psPaused has no assigned verb; must not borrow sampleRateHz's");
        QCOMPARE(slice.psPaused(), before);
    }

    // ── Structural rejections ────────────────────────────────────────────

    void constantPropertyIsRejectedAndNeverReachesTheHook()
    {
        SliceModel slice(7);
        StateMirror mirror;
        QVERIFY(mirror.watch("slice:7", &slice));

        const MirrorApplyResult result =
            mirror.applyInbound("slice:7", "sliceIndex", QVariant(99));

        QVERIFY(!result.accepted);
        QCOMPARE(slice.sliceIndex(), 7); // untouched -- the object identity
    }

    void unknownPropertyIsRejected()
    {
        SliceModel slice(0);
        StateMirror mirror;
        QVERIFY(mirror.watch("slice:0", &slice));

        const MirrorApplyResult result =
            mirror.applyInbound("slice:0", "noSuchPropertyEver", QVariant(1));

        QVERIFY(!result.accepted);
        QVERIFY(!result.reason.isEmpty());
    }

    void unknownObjectKeyIsRejected()
    {
        StateMirror mirror;
        const MirrorApplyResult result =
            mirror.applyInbound("slice:99", "frequency", QVariant(7100000.0));
        QVERIFY(!result.accepted);
        QVERIFY(!result.reason.isEmpty());
    }

    void ordinalOverloadResolvesTheSamePropertyAsTheNameOverload()
    {
        SliceModel slice(0);
        StateMirror mirror;
        QVERIFY(mirror.watch("slice:0", &slice));

        const MirrorSchema& schema = MirrorSchema::forObject(&slice);
        const MirrorProperty* prop = schema.byName("frequency");
        QVERIFY(prop != nullptr);

        const MirrorApplyResult result =
            mirror.applyInbound("slice:0", prop->ordinal, QVariant(7100000.0));

        QVERIFY2(result.accepted, qPrintable(result.reason));
        QCOMPARE(result.property, QByteArray("frequency"));
        QCOMPARE(slice.frequency(), 7100000.0);
    }

    // ── The per-model applyMirroredValue hook ───────────────────────────────

    // TunerModel's whole ATU surface carries no WRITE. Without the hook,
    // nothing on the tuner is remotely operable at all -- not even the
    // controls (isOperate/isBypass/antennaA) that DO have a real command
    // slot behind them.
    void tunerOperateAndBypassRouteThroughTheHookToTheRealCommandSlots()
    {
        TunerModel tuner;
        // Direct unit check of the hook itself, independent of StateMirror's
        // reflection-based dispatch (proven separately below): must return
        // accepted (empty reason)...
        QVERIFY(tuner.applyMirroredValue("isOperate", QVariant(true)).isEmpty());
        // ...and must NOT have taken a shortcut straight to the property.
        // setOperate() requires a live, connected TgxlConnection (none is
        // bound here) and is a no-op without one; if isOperate() changed
        // anyway, the hook bypassed the command path and wrote m_operate
        // directly, which is the one thing it must not do (a local GUI's
        // TunerApplet drives the SAME setOperate() slot, and the mirror
        // must not diverge from that contract).
        QCOMPARE(tuner.isOperate(), false);

        QVERIFY(tuner.applyMirroredValue("isBypass", QVariant(true)).isEmpty());
        QCOMPARE(tuner.isBypass(), false);
    }

    void tunerAntennaARejectsOutOfRangeWithoutInventingASilentSuccess()
    {
        TunerModel tuner;
        const QString reason = tuner.applyMirroredValue("antennaA", QVariant(7));
        QVERIFY2(!reason.isEmpty(), "antennaA=7 is out of TunerModel::setAntennaA's "
                                    "own 1..3 range and must be refused, not silently "
                                    "swallowed and reported as applied");
    }

    void tunerTelemetryPropertiesAreRejectedByTheHook()
    {
        TunerModel tuner;
        for (const char* name : { "relayC1", "relayL", "relayC2", "isTuning",
                                  "hasAntennaSwitch", "isPresent",
                                  "hasDirectConnection", "tgxlIp", "fwdPower",
                                  "swr" }) {
            const QString reason = tuner.applyMirroredValue(name, QVariant(1));
            QVERIFY2(!reason.isEmpty(), name);
        }
    }

    // Proves StateMirror's dispatch genuinely reaches the hook via
    // QMetaObject::invokeMethod (as opposed to the direct-call unit checks
    // above, which exercise the hook but not the routing to it).
    void stateMirrorRoutesANoWritePropertyToTheTunerHook()
    {
        TunerModel tuner;
        StateMirror mirror;
        QVERIFY(mirror.watch("tuner:0", &tuner));

        const MirrorApplyResult result =
            mirror.applyInbound("tuner:0", "isOperate", QVariant(true));

        QVERIFY2(result.accepted, qPrintable(result.reason));
    }

    void radioModelIdentityPropertiesAreRejectedByTheHook()
    {
        RadioModel radio;
        for (const char* name : { "name", "model", "version" }) {
            const QString reason =
                radio.applyMirroredValue(name, QVariant(QStringLiteral("bogus")));
            QVERIFY2(!reason.isEmpty(), name);
        }
    }

    // Task 3 re-points `connected` at m_connectionState and Task 18 drives
    // it through setConnectionState; applyMirroredValue must not build a
    // second path into it, so it falls through to the same generic refusal
    // as name/model/version rather than doing anything special.
    void radioModelConnectedIsRejectedByTheHookLikeAnyOtherUnhandledProperty()
    {
        RadioModel radio;
        const QString reason = radio.applyMirroredValue("connected", QVariant(true));
        QVERIFY(!reason.isEmpty());
    }

    // SliceModel's no-WRITE surface is active/txSlice/band -- THREE, not
    // the brief's stale two (Task 6 added band). sliceIndex is the fourth
    // no-WRITE property but is CONSTANT and is proven separately never to
    // reach the hook at all.
    void sliceModelNoWritePropertiesAreRejectedByTheHookWithDistinctReasons()
    {
        SliceModel slice(0);
        const QString activeReason = slice.applyMirroredValue("active", QVariant(true));
        const QString txReason = slice.applyMirroredValue("txSlice", QVariant(true));
        const QString bandReason = slice.applyMirroredValue(
            "band", QVariant(static_cast<int>(Band::Band40m)));

        QVERIFY(!activeReason.isEmpty());
        QVERIFY(!txReason.isEmpty());
        QVERIFY(!bandReason.isEmpty());
        // Distinct, not a single copy-pasted message for all three -- each
        // names why (arbitrated elsewhere, or derived from frequency).
        QVERIFY(activeReason != txReason);
        QVERIFY(activeReason != bandReason);
        QVERIFY(txReason != bandReason);

        QCOMPARE(slice.isActive(), false);
        QCOMPARE(slice.isTxSlice(), false);
        QCOMPARE(slice.band(), Band::Band20m); // ctor default (14.225 MHz); untouched
    }

    // ── No outbound delta ────────────────────────────────────────────────

    // The trap named in the task notes: SliceModel's setters are uniformly
    // change-guarded, so re-applying the CURRENT value produces no NOTIFY
    // at all and this assertion would hold whether or not the guard works.
    // nbMode's ctor default is Off; NB is genuinely different.
    void inboundApplyOfAGenuinelyDifferentValueProducesNoOutboundDelta()
    {
        SliceModel slice(0);
        QCOMPARE(slice.nbMode(), NbMode::Off);
        StateMirror mirror;
        QVERIFY(mirror.watch("slice:0", &slice));
        Collector c(&mirror);

        const MirrorApplyResult result = mirror.applyInbound(
            "slice:0", "nbMode", QVariant(static_cast<int>(NbMode::NB)));

        QVERIFY2(result.accepted, qPrintable(result.reason));
        QCOMPARE(slice.nbMode(), NbMode::NB); // the write genuinely landed...
        QVERIFY2(c.flat.isEmpty(),
                 "...yet must not have produced an outbound delta");
    }

    // The guard must not stick: once applyInbound() returns, this same
    // object's notifies must forward normally again.
    void guardDoesNotOutliveTheApplyInboundCallThatSetIt()
    {
        SliceModel slice(0);
        StateMirror mirror;
        QVERIFY(mirror.watch("slice:0", &slice));
        Collector c(&mirror);

        mirror.applyInbound("slice:0", "nbMode",
                            QVariant(static_cast<int>(NbMode::NB)));
        QVERIFY(c.flat.isEmpty());

        slice.setFrequency(7100000.0); // an ordinary LOCAL change, not mirrored
        QVERIFY2(c.sawKey("slice:0"),
                 "forwarding must resume once applyInbound() has returned");
    }

    // ── The RadioModel peer-mirror sites (the guard extension) ──────────────
    //
    // RadioModel::addSlice() wires two peer-mirror connects UNCONDITIONALLY
    // (not gated on wireSliceSignals / m_connection, by design -- see the
    // comment beside them): the nbModeChanged peer mirror, and the
    // mirrorNbTuning helper shared by nb1Threshold/nb1TransitionMs/
    // nb1LeadMs/nb1LagMs/nb2Mode. Both exist so co-hosted slices (multiple
    // demod slices sharing one DDC stream, hence one physical WDSP
    // blanker) never disagree on NB state. An inbound write to ONE
    // co-hosted slice's nbMode/nb-tuning therefore changes ANOTHER watched
    // SliceModel too, purely as a same-thread, synchronous side effect of
    // the write StateMirror is already applying.
    //
    // The peer's own change must still happen locally -- skipping it would
    // leave the peer's WDSP channel disagreeing with the one that just
    // changed, exactly the desync these connects exist to prevent -- but
    // it must not be forwarded outbound as a delta the remote peer never
    // asked for. StateMirror::onWatchedPropertyChanged() achieves both by
    // checking m_applying before looking at WHICH object fired the notify,
    // so the suppression covers this same-thread cascade without
    // RadioModel needing to know a mirror exists.

    void peerNbModeMirrorAppliesLocallyButIsNotForwarded()
    {
        RadioModel model;
        model.configureStreamPool(5, 5, 192000);
        const int a = model.addSlice();
        const int b = model.addSlice();
        // Matches tst_stream_pool_binding.cpp's own co-hosting fixture:
        // 10 kHz apart lands both inside one stream's window.
        model.sliceById(a)->setFrequency(14200000.0);
        model.sliceById(b)->setFrequency(14210000.0);
        QCOMPARE(model.sliceById(b)->streamIndex(), model.sliceById(a)->streamIndex());

        SliceModel* sliceA = model.sliceById(a);
        SliceModel* sliceB = model.sliceById(b);
        QCOMPARE(sliceA->nbMode(), NbMode::Off);
        QCOMPARE(sliceB->nbMode(), NbMode::Off);

        StateMirror mirror;
        QVERIFY(mirror.watch("slice:0", sliceA));
        QVERIFY(mirror.watch("slice:1", sliceB));
        Collector c(&mirror);

        const MirrorApplyResult result = mirror.applyInbound(
            "slice:0", "nbMode", QVariant(static_cast<int>(NbMode::NB)));

        QVERIFY2(result.accepted, qPrintable(result.reason));
        QCOMPARE(sliceA->nbMode(), NbMode::NB);
        // The peer mirror in addSlice() really ran (hardware correctness:
        // the co-hosted stream's single blanker must agree)...
        QCOMPARE(sliceB->nbMode(), NbMode::NB);
        // ...but neither slice's change reached the wire.
        QVERIFY2(c.flat.isEmpty(),
                 "the peer's own nbModeChanged must not have been forwarded");
    }

    void peerNbTuningMirrorAppliesLocallyButIsNotForwarded()
    {
        RadioModel model;
        model.configureStreamPool(5, 5, 192000);
        const int a = model.addSlice();
        const int b = model.addSlice();
        model.sliceById(a)->setFrequency(14200000.0);
        model.sliceById(b)->setFrequency(14210000.0);
        QCOMPARE(model.sliceById(b)->streamIndex(), model.sliceById(a)->streamIndex());

        SliceModel* sliceA = model.sliceById(a);
        SliceModel* sliceB = model.sliceById(b);
        const int before = sliceA->nb1Threshold();
        QCOMPARE(sliceB->nb1Threshold(), before);
        const int genuinelyDifferent = before + 5;

        StateMirror mirror;
        QVERIFY(mirror.watch("slice:0", sliceA));
        QVERIFY(mirror.watch("slice:1", sliceB));
        Collector c(&mirror);

        const MirrorApplyResult result = mirror.applyInbound(
            "slice:0", "nb1Threshold", QVariant(genuinelyDifferent));

        QVERIFY2(result.accepted, qPrintable(result.reason));
        QCOMPARE(sliceA->nb1Threshold(), genuinelyDifferent);
        QCOMPARE(sliceB->nb1Threshold(), genuinelyDifferent); // mirrorNbTuning ran
        QVERIFY(c.flat.isEmpty());
    }

    // Proves the guard extension changed nothing about RadioModel.cpp's own
    // behaviour: with NO StateMirror involved at all -- the exact situation
    // every existing local-mode user is in today -- a plain local write
    // still mirrors to the co-hosted peer exactly as it always has.
    void peerMirrorStillRunsForAPurelyLocalChangeNoMirrorInvolved()
    {
        RadioModel model;
        model.configureStreamPool(5, 5, 192000);
        const int a = model.addSlice();
        const int b = model.addSlice();
        model.sliceById(a)->setFrequency(14200000.0);
        model.sliceById(b)->setFrequency(14210000.0);
        QCOMPARE(model.sliceById(b)->streamIndex(), model.sliceById(a)->streamIndex());

        SliceModel* sliceA = model.sliceById(a);
        SliceModel* sliceB = model.sliceById(b);
        QCOMPARE(sliceB->nbMode(), NbMode::Off);

        sliceA->setNbMode(NbMode::NB); // local operator action, no mirror anywhere

        QCOMPARE(sliceB->nbMode(), NbMode::NB);
    }
};

QTEST_MAIN(TestMirrorInbound)
#include "tst_mirror_inbound.moc"
