// no-port-check: NereusSDR-original.
// =================================================================
// tests/tst_transmit_holder.cpp  (NereusSDR)
// =================================================================
//
// iPhone app plan Task 34 (R-IOS-02, R-IOS-03; the several-devices design,
// rulings 8.1, 8.2, 8.4 and 8.15): who holds transmit and how it changes
// hands, with injected clocks and fake hooks (the unkey gate, stopAllTx,
// MOX, VOX, the timer). Every refusal and the transfer's fence first, then
// the admit paths.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-25: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), iPhone app plan Task 34 (R-IOS-02), with
//               AI-assisted implementation via Anthropic Claude Code.
//   2026-09-26: Transmit group fix wave I3: a transfer during a dropped
//               holder's fence leaves no fence. J.J. Boyd (KG4VCF), with
//               AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include <QtTest>

#include "core/safety/TransmitHolder.h"

#include "OperatorWording.h"

using namespace NereusSDR;
using Source = TransmitHolder::Source;
using State = TransmitHolder::State;

namespace {

// Everything the holder reaches, faked and recorded.
struct Rig {
    TransmitHolder holder;
    qint64 now{1000};
    bool mox{false};
    int voxDisarmed{0};
    QStringList stops;
    QStringList unkeys;
    std::function<void(UnkeyOutcome)> pendingUnkey;
    QList<QPair<int, std::function<void()>>> timers;

    Rig()
    {
        TransmitHolder::Hooks hooks;
        hooks.clock = [this]() { return now; };
        hooks.moxOn = [this]() { return mox; };
        hooks.unkey = [this](const QString& reason, std::function<void(UnkeyOutcome)> done) {
            unkeys.append(reason);
            pendingUnkey = std::move(done);
        };
        hooks.stopAllTx = [this](const QString& reason) { stops.append(reason); };
        hooks.disarmVox = [this]() { ++voxDisarmed; };
        hooks.schedule = [this](int ms, std::function<void()> fire) {
            timers.append({ms, std::move(fire)});
        };
        hooks.describe = [](const QByteArray& id) -> std::optional<TransmitHolder::Words> {
            if (id == "phone") {
                return TransmitHolder::Words{QStringLiteral("Grant's iPhone"),
                                             QStringLiteral("iPhone"), QStringLiteral("phone")};
            }
            if (id == "pad") {
                return TransmitHolder::Words{QStringLiteral("Shack iPad"), QStringLiteral("iPad"),
                                             QStringLiteral("tablet")};
            }
            return std::nullopt;
        };
        holder.setHooks(hooks);
    }

    // The unkey gate answers.
    void answerUnkey(UnkeyOutcome outcome)
    {
        QVERIFY(pendingUnkey);
        auto done = std::move(pendingUnkey);
        pendingUnkey = {};
        done(outcome);
    }

    void fireTimer(int index = 0)
    {
        QVERIFY(timers.size() > index);
        auto fire = timers.takeAt(index).second;
        fire();
    }

    static TransmitHolder::Holder device(const QByteArray& id)
    {
        TransmitHolder::Holder h;
        h.deviceId = id;
        return h;
    }

    KeyingAnswer key(const QByteArray& id, Source source = Source::Device, bool program = false,
                     bool vox = false)
    {
        return holder.askKey({id, source, program, vox});
    }

    // Makes `id` the holder from unheld (a person's key) and keys it.
    void holdAndKey(const QByteArray& id)
    {
        QCOMPARE(key(id).verdict, KeyingVerdict::Admit);
        mox = true;
        holder.onMoxReading(true);
        holder.setKeyed(true);
    }
};

bool refusedWith(const KeyingAnswer& answer, const TxRefusal& refusal)
{
    return answer.verdict == KeyingVerdict::Refuse && answer.refusal == refusal;
}

} // namespace

class TstTransmitHolder : public QObject {
    Q_OBJECT

private slots:
    // ---- The record ---------------------------------------------------------

    void itStartsUnheld()
    {
        Rig rig;
        QCOMPARE(rig.holder.state(), State::Unheld);
        QVERIFY(!rig.holder.holder().has_value());
        QCOMPARE(rig.holder.epoch(), quint64(0));
    }

    // ---- Refusals and the fence first ---------------------------------------

    void duringATransferEveryKeyIsRefusedAndAnUnkeyIsNot()
    {
        Rig rig;
        rig.holdAndKey("phone");
        rig.holder.transferTo(Rig::device("pad"), QStringLiteral("take"));
        QCOMPARE(rig.holder.state(), State::Transferring);

        // The old holder's key, the radio's PTT, VOX, a program (TCI), and
        // any other device: each refused with the changing-hands sentence.
        const TxRefusal hands = TxRefusals::changingHands();
        QVERIFY(refusedWith(rig.key("phone"), hands));
        QVERIFY(refusedWith(rig.key("station", Source::RadioPtt), hands));
        QVERIFY(refusedWith(rig.key("station", Source::Device, false, /*vox=*/true), hands));
        QVERIFY(refusedWith(rig.key("station", Source::Device, /*program=*/true), hands));
        QVERIFY(refusedWith(rig.key("pad"), hands));
        QCOMPARE(hands.text, QStringLiteral("Transmit is changing hands. Try again in a moment."));
        // Unkeying is never asked of the holder: the gate has no say in it.
        // The unkey the transfer ran is the one in flight.
        QCOMPARE(rig.unkeys.size(), 1);
    }

    void aTransferFromAKeyedHolderAssignsNothingBeforeConfirmed()
    {
        Rig rig;
        rig.holdAndKey("phone");
        const quint64 epochBefore = rig.holder.epoch();
        bool finished = false;
        bool assigned = false;
        rig.holder.transferTo(Rig::device("pad"), QStringLiteral("take"),
                              [&](bool ok) { finished = true; assigned = ok; });

        // The unkey gate runs, and until it answers nothing is assigned.
        QCOMPARE(rig.unkeys.size(), 1);
        QVERIFY(!finished);
        QCOMPARE(rig.holder.state(), State::Transferring);
        QCOMPARE(rig.holder.epoch(), epochBefore);

        // Confirmed: MOX reached receive.
        rig.mox = false;
        rig.holder.onMoxReading(false);
        rig.answerUnkey(UnkeyOutcome::Confirmed);
        QVERIFY(finished);
        QVERIFY(assigned);
        QCOMPARE(rig.holder.state(), State::Held);
        QCOMPARE(rig.holder.holder()->deviceId, QByteArray("pad"));
        // A new holder always starts unkeyed.
        QVERIFY(!rig.holder.holder()->keyed);
        QVERIFY(!rig.holder.isKeyed());
        // The epoch advances once for the one change.
        QCOMPARE(rig.holder.epoch(), epochBefore + 1);
    }

    void timedOutWithMoxOnStopsAgainWaitsThenEndsUnheld()
    {
        Rig rig;
        rig.holdAndKey("phone");
        const quint64 epochBefore = rig.holder.epoch();
        bool assigned = true;
        rig.holder.transferTo(Rig::device("pad"), QStringLiteral("take"),
                              [&](bool ok) { assigned = ok; });

        // TimedOut, MOX still reads on: stopAllTx again, a 2000 ms wait.
        rig.answerUnkey(UnkeyOutcome::TimedOut);
        QCOMPARE(rig.stops.size(), 1);
        QCOMPARE(rig.timers.size(), 1);
        QCOMPARE(rig.timers.first().first, 2000);
        QCOMPARE(rig.holder.state(), State::Transferring);

        // Still on at the end of the wait: transmit unheld, and every key
        // refused until MOX reads off.
        rig.fireTimer();
        QVERIFY(!assigned);
        QCOMPARE(rig.holder.state(), State::Unheld);
        QVERIFY(!rig.holder.holder().has_value());
        QVERIFY(rig.holder.isStopUnconfirmed());
        QCOMPARE(rig.holder.epoch(), epochBefore + 1);
        const TxRefusal stop = TxRefusals::stopNotConfirmed();
        QCOMPARE(stop.text, QStringLiteral("The radio did not confirm it stopped transmitting."));
        QVERIFY(refusedWith(rig.key("pad"), stop));
        QVERIFY(refusedWith(rig.key("phone"), stop));
        QVERIFY(refusedWith(rig.key("station", Source::RadioPtt), stop));

        // MOX reads off: keys are taken again, as on unheld transmit.
        rig.mox = false;
        rig.holder.onMoxReading(false);
        QVERIFY(!rig.holder.isStopUnconfirmed());
        QCOMPARE(rig.key("pad").verdict, KeyingVerdict::Admit);
        QCOMPARE(rig.holder.holder()->deviceId, QByteArray("pad"));
    }

    void timedOutThenMoxOffInsideTheWaitAssigns()
    {
        Rig rig;
        rig.holdAndKey("phone");
        bool assigned = false;
        rig.holder.transferTo(Rig::device("pad"), QStringLiteral("take"),
                              [&](bool ok) { assigned = ok; });
        rig.answerUnkey(UnkeyOutcome::TimedOut);
        QCOMPARE(rig.stops.size(), 1);
        rig.mox = false;
        rig.holder.onMoxReading(false);
        QVERIFY(assigned);
        QCOMPARE(rig.holder.holder()->deviceId, QByteArray("pad"));
        // The wait's timer, when it fires later, changes nothing.
        const quint64 epoch = rig.holder.epoch();
        rig.fireTimer();
        QCOMPARE(rig.holder.epoch(), epoch);
        QCOMPARE(rig.holder.holder()->deviceId, QByteArray("pad"));
    }

    void confirmedButMoxStillReadsOnIsStoppedAgain()
    {
        // Keyed or not, MOX is read after the unkey: while it reads on no
        // holder is assigned.
        Rig rig;
        rig.holdAndKey("phone");
        rig.holder.transferTo(Rig::device("pad"), QStringLiteral("take"));
        rig.answerUnkey(UnkeyOutcome::Confirmed);   // MOX still true
        QCOMPARE(rig.stops.size(), 1);
        QCOMPARE(rig.holder.state(), State::Transferring);
        rig.mox = false;
        rig.holder.onMoxReading(false);
        QCOMPARE(rig.holder.state(), State::Held);
    }

    void anotherDevicesKeyIsRefusedNamingTheHolder()
    {
        Rig rig;
        QCOMPARE(rig.key("phone").verdict, KeyingVerdict::Admit);
        const KeyingAnswer answer = rig.key("pad");
        QVERIFY(refusedWith(answer, TxRefusals::otherDeviceHolds(QStringLiteral("Grant's iPhone"))));
        QCOMPARE(answer.refusal.fix, QByteArray("takeTransmit"));
        // The station device too (the radio's PTT, the desktop's buttons)
        // until Task 77 turns its press into a take.
        QVERIFY(refusedWith(rig.key("station", Source::RadioPtt),
                            TxRefusals::otherDeviceHolds(QStringLiteral("Grant's iPhone"))));
    }

    void aProgramNeverTakesTransmit()
    {
        Rig rig;
        const KeyingAnswer answer = rig.key("phone", Source::Device, /*program=*/true);
        QVERIFY(refusedWith(answer, TxRefusals::programNeedsTransmit()));
        QCOMPARE(rig.holder.state(), State::Unheld);
        // Its own device holding transmit, a program keys.
        QCOMPARE(rig.key("phone").verdict, KeyingVerdict::Admit);
        QCOMPARE(rig.key("phone", Source::Device, true).verdict, KeyingVerdict::Admit);
    }

    // ---- The holder goes away (ruling 8.15) ---------------------------------

    void aDroppedKeyedHolderIsUnkeyedAtOnceAndHoldsTransmitAway()
    {
        Rig rig;
        rig.holdAndKey("phone");
        const quint64 epoch = rig.holder.epoch();
        const int voxBefore = rig.voxDisarmed;
        rig.holder.holderDropped("phone", QStringLiteral("link lost"));

        // Unkeyed at once, through the fence: keys refused until MOX reads
        // off.
        QCOMPARE(rig.unkeys.size(), 1);
        QVERIFY(rig.holder.isFenced());
        QVERIFY(refusedWith(rig.key("pad"), TxRefusals::changingHands()));
        QVERIFY(refusedWith(rig.key("phone"), TxRefusals::changingHands()));
        rig.mox = false;
        rig.holder.onMoxReading(false);
        rig.answerUnkey(UnkeyOutcome::Confirmed);
        QVERIFY(!rig.holder.isFenced());

        // Still held, for it, away and unkeyed, VOX disarmed; not a change
        // of holder, so the epoch stays.
        QCOMPARE(rig.holder.state(), State::Held);
        QCOMPARE(rig.holder.holder()->deviceId, QByteArray("phone"));
        QVERIFY(rig.holder.holder()->away);
        QVERIFY(!rig.holder.holder()->keyed);
        QVERIFY(rig.voxDisarmed > voxBefore);
        QCOMPARE(rig.holder.epoch(), epoch);

        // Another device's key is refused naming the away holder.
        QVERIFY(refusedWith(rig.key("pad"),
                            TxRefusals::otherDeviceHolds(QStringLiteral("Grant's iPhone"))));
    }

    void aDroppedHolderThatTimesOutIsStoppedAgainInsideTheFence()
    {
        Rig rig;
        rig.holdAndKey("phone");
        rig.holder.holderDropped("phone", QStringLiteral("link lost"));
        rig.answerUnkey(UnkeyOutcome::TimedOut);
        QCOMPARE(rig.stops.size(), 1);
        QVERIFY(rig.holder.isFenced());
        QCOMPARE(rig.timers.size(), 1);
        QCOMPARE(rig.timers.first().first, 2000);
        rig.fireTimer();
        // MOX still on after the second wait: keys refused until it reads
        // off, the holder kept.
        QVERIFY(rig.holder.isStopUnconfirmed());
        QVERIFY(refusedWith(rig.key("phone"), TxRefusals::stopNotConfirmed()));
        rig.mox = false;
        rig.holder.onMoxReading(false);
        QVERIFY(!rig.holder.isFenced());
        QVERIFY(!rig.holder.isStopUnconfirmed());
        QCOMPARE(rig.holder.holder()->deviceId, QByteArray("phone"));
    }

    void theSameDeviceBackHoldsTransmitUnkeyedAndKeysOnItsNextPress()
    {
        Rig rig;
        rig.holdAndKey("phone");
        rig.holder.holderDropped("phone", QStringLiteral("link lost"));
        rig.mox = false;
        rig.holder.onMoxReading(false);
        rig.answerUnkey(UnkeyOutcome::Confirmed);
        const quint64 epoch = rig.holder.epoch();

        rig.holder.holderReturned("phone");
        QCOMPARE(rig.holder.holder()->deviceId, QByteArray("phone"));
        QVERIFY(!rig.holder.holder()->away);
        QVERIFY(!rig.holder.holder()->keyed);
        QCOMPARE(rig.holder.epoch(), epoch);
        // Nothing keyed by itself; its next press is admitted.
        QVERIFY(!rig.mox);
        QCOMPARE(rig.key("phone").verdict, KeyingVerdict::Admit);
    }

    void releasesTransferToNobody()
    {
        // The end of its 180 s, session.leave and a revoke all release.
        Rig rig;
        QCOMPARE(rig.key("phone").verdict, KeyingVerdict::Admit);
        const quint64 epoch = rig.holder.epoch();
        const int voxBefore = rig.voxDisarmed;
        rig.holder.release("pad", QStringLiteral("not the holder"));
        QCOMPARE(rig.holder.holder()->deviceId, QByteArray("phone"));

        rig.holder.release("phone", QStringLiteral("left"));
        QCOMPARE(rig.holder.state(), State::Unheld);
        QVERIFY(!rig.holder.holder().has_value());
        QCOMPARE(rig.holder.epoch(), epoch + 1);
        QVERIFY(rig.voxDisarmed > voxBefore);
    }

    void aKeyedHoldersReleaseUnkeysFirst()
    {
        Rig rig;
        rig.holdAndKey("phone");
        rig.holder.release("phone", QStringLiteral("revoked"));
        QCOMPARE(rig.unkeys.size(), 1);
        QCOMPARE(rig.holder.state(), State::Transferring);
        rig.mox = false;
        rig.holder.onMoxReading(false);
        rig.answerUnkey(UnkeyOutcome::Confirmed);
        QCOMPARE(rig.holder.state(), State::Unheld);
    }

    // ---- VOX follows the holder (ruling 8.4) --------------------------------

    void voxIsDisarmedAtEveryChangeOfHolder()
    {
        Rig rig;
        QCOMPARE(rig.key("phone").verdict, KeyingVerdict::Admit);   // unheld -> phone
        QCOMPARE(rig.voxDisarmed, 1);
        rig.holder.transferTo(Rig::device("pad"), QStringLiteral("take"));   // phone -> pad
        QCOMPARE(rig.voxDisarmed, 2);
        rig.holder.release("pad", QStringLiteral("left"));   // pad -> nobody
        QCOMPARE(rig.voxDisarmed, 3);
    }

    void theStationsOwnVoxKeyOnUnheldTransmitKeepsVoxArmed()
    {
        // The station device's key is admitted on unheld transmit (until
        // Task 77); when that key is VOX's own, disarming VOX at the change
        // of holder would end the key being admitted, so it stays armed.
        Rig rig;
        QCOMPARE(rig.key("station", Source::Device, false, /*vox=*/true).verdict,
                 KeyingVerdict::Admit);
        QCOMPARE(rig.holder.holder()->deviceId, QByteArray("station"));
        QCOMPARE(rig.voxDisarmed, 0);
        QCOMPARE(rig.holder.epoch(), quint64(1));
    }

    // ---- The admit paths ----------------------------------------------------

    void aPersonsKeyOnUnheldTransmitTakesItAndIsAdmitted()
    {
        Rig rig;
        QSignalSpy changed(&rig.holder, &TransmitHolder::changed);
        QCOMPARE(rig.key("phone").verdict, KeyingVerdict::Admit);
        QCOMPARE(rig.holder.state(), State::Held);
        const auto h = rig.holder.holder();
        QCOMPARE(h->deviceId, QByteArray("phone"));
        QCOMPARE(h->name, QStringLiteral("Grant's iPhone"));
        QCOMPARE(h->shortName, QStringLiteral("iPhone"));
        QCOMPARE(h->kind, QStringLiteral("phone"));
        QCOMPARE(h->source, Source::Device);
        QCOMPARE(h->sinceMs, qint64(1000));
        QCOMPARE(rig.holder.epoch(), quint64(1));
        QVERIFY(changed.count() >= 1);
        // Its next key is admitted without another change.
        QCOMPARE(rig.key("phone").verdict, KeyingVerdict::Admit);
        QCOMPARE(rig.holder.epoch(), quint64(1));
    }

    void theRadiosPttOnUnheldTransmitMakesTheRadioTheHolder()
    {
        Rig rig;
        QCOMPARE(rig.key("station", Source::RadioPtt).verdict, KeyingVerdict::Admit);
        const auto h = rig.holder.holder();
        QCOMPARE(h->deviceId, QByteArray("station"));
        QCOMPARE(h->source, Source::RadioPtt);
        QCOMPARE(h->name, QStringLiteral("Radio"));
        QCOMPARE(h->shortName, QStringLiteral("Radio"));
        QCOMPARE(h->kind, QStringLiteral("station"));
        // A station key from its buttons while the radio holds it is a key,
        // not a take: the names and the source stay.
        QCOMPARE(rig.key("station", Source::Device).verdict, KeyingVerdict::Admit);
        QCOMPARE(rig.holder.holder()->source, Source::RadioPtt);
        QCOMPARE(rig.holder.epoch(), quint64(1));
        // Another device is told the radio has it.
        QVERIFY(refusedWith(rig.key("phone"), TxRefusals::otherDeviceHolds(QStringLiteral("Radio"))));
    }

    void theStationDevicesButtonsOnUnheldTransmitHoldAsTheStation()
    {
        Rig rig;
        QCOMPARE(rig.key("station", Source::Device).verdict, KeyingVerdict::Admit);
        QCOMPARE(rig.holder.holder()->source, Source::Device);
        QCOMPARE(rig.holder.holder()->kind, QStringLiteral("station"));
    }

    void keyRefusalForAsksWithoutChangingAnything()
    {
        Rig rig;
        QVERIFY(rig.holder.keyRefusalFor("phone").isEmpty());
        QCOMPARE(rig.holder.state(), State::Unheld);
        QCOMPARE(rig.holder.keyRefusalFor("phone", true), TxRefusals::programNeedsTransmit());
        QCOMPARE(rig.key("phone").verdict, KeyingVerdict::Admit);
        QCOMPARE(rig.holder.keyRefusalFor("pad"),
                 TxRefusals::otherDeviceHolds(QStringLiteral("Grant's iPhone")));
    }

    void everySentenceIsPlain()
    {
        for (const TxRefusal& r : {TxRefusals::changingHands(), TxRefusals::stopNotConfirmed(),
                                   TxRefusals::programNeedsTransmit(),
                                   TxRefusals::otherDeviceHolds(QStringLiteral("Grant's iPhone"))}) {
            // The device's name is the operator's word, not held to the
            // wording rules ("Grant" is on its list); the sentence is.
            const QString sentence = QString(r.text).replace(QStringLiteral("Grant's iPhone"),
                                                             QStringLiteral("iPhone"));
            QVERIFY2(OperatorWording::isPlain(sentence), qPrintable(r.text));
        }
    }

    // ---- Fix wave I3: a transfer clears a dropped holder's fence ---------

    // A release while a dropped holder's unkey is still running: the
    // transfer supersedes the fence, and once it assigns nobody, keys are
    // admitted again (the fence never outlives the holder it was for).
    void aReleaseDuringTheDroppedHoldersFenceLeavesNoFence()
    {
        Rig rig;
        rig.holdAndKey("phone");
        rig.holder.holderDropped("phone", QStringLiteral("dropped"));
        QVERIFY(rig.holder.isFenced());
        QVERIFY(rig.pendingUnkey);
        rig.holder.release("phone", QStringLiteral("left"));
        QCOMPARE(rig.holder.state(), State::Transferring);
        rig.mox = false;
        rig.holder.onMoxReading(false);
        rig.answerUnkey(UnkeyOutcome::Confirmed);
        QCOMPARE(rig.holder.state(), State::Unheld);
        QVERIFY(!rig.holder.isFenced());
        QCOMPARE(rig.key("pad").verdict, KeyingVerdict::Admit);
        QVERIFY(rig.holder.isHeldBy("pad"));
    }

    // The same when the transfer ends with MOX still on: keys are refused
    // "did not confirm" until MOX reads off, then admitted; never stuck on
    // "changing hands".
    void aFailedTransferDuringTheFenceClearsWhenMoxReadsOff()
    {
        Rig rig;
        rig.holdAndKey("phone");
        rig.holder.holderDropped("phone", QStringLiteral("dropped"));
        QVERIFY(rig.holder.isFenced());
        rig.holder.release("phone", QStringLiteral("left"));
        rig.answerUnkey(UnkeyOutcome::TimedOut);
        // MOX still reads on: StopAllTx, then 2000 ms, then the failure.
        QVERIFY(!rig.timers.isEmpty());
        rig.fireTimer(rig.timers.size() - 1);
        QCOMPARE(rig.holder.state(), State::Unheld);
        QVERIFY(!rig.holder.isFenced());
        QVERIFY(refusedWith(rig.key("pad"), TxRefusals::stopNotConfirmed()));
        rig.mox = false;
        rig.holder.onMoxReading(false);
        QCOMPARE(rig.key("pad").verdict, KeyingVerdict::Admit);
    }
};

QTEST_MAIN(TstTransmitHolder)
#include "tst_transmit_holder.moc"
