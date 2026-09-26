// no-port-check: NereusSDR-original.
// =================================================================
// tests/tst_slice_ownership.cpp  (NereusSDR)
// =================================================================
//
// iPhone app plan Task 73 (R-IOS-02; the several-devices design, sections
// 5.1, 5.2 and 5.7, rulings 5.1, 5.2, 5.10 and 5.11): SliceOwnership, the
// Core's record of whose each slice is and of each owner's active slice.
//
// Ownership first (who may change which slice rests on these marks): a new
// slice belongs to the open creator scope's owner, or to none; adoption takes
// only slices with no owner, never one held for another device; held slices
// return to the device they are held for and to nobody else; a slice being
// removed is nobody's, but its mark is kept until its removal is announced.
// Then the active slice: each owner's own, one per owner, A's choice never
// moving B's; a removed active slice passing to its owner's next; the
// station-level active slice following the most recent choice, or the
// transmit holder's while one holds transmit.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-25: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), iPhone app plan Task 73 (R-IOS-02), with
//               AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include <QtTest>

#include <QSignalSpy>

#include "core/SliceOwnership.h"

using namespace NereusSDR;

namespace {

const QByteArray kA = QByteArrayLiteral("device-a");
const QByteArray kB = QByteArrayLiteral("device-b");
const QByteArray kToken = QByteArrayLiteral("token:1");

void add(SliceOwnership& own, int id, const QByteArray& creator = {})
{
    SliceOwnership::CreatorScope scope(&own, creator);
    own.noteSliceAdded(id);
}

} // namespace

class TstSliceOwnership : public QObject {
    Q_OBJECT

private slots:
    // ── Owners ───────────────────────────────────────────────────────────

    void aNewSliceBelongsToTheOpenCreatorOrToNone()
    {
        SliceOwnership own;
        own.noteSliceAdded(0);
        add(own, 1, kA);
        {
            SliceOwnership::CreatorScope outer(&own, kB);
            {
                SliceOwnership::CreatorScope inner(&own, kToken);
                own.noteSliceAdded(2);
            }
            own.noteSliceAdded(3);
        }
        own.noteSliceAdded(4);
        QVERIFY(own.mark(0).owner.isEmpty());
        QCOMPARE(own.mark(1).owner, kA);
        QCOMPARE(own.mark(2).owner, kToken);
        QCOMPARE(own.mark(3).owner, kB);
        QVERIFY(own.mark(4).owner.isEmpty());
        QVERIFY(own.creator().isEmpty());
        QCOMPARE(own.liveSlices(), (QList<int>{0, 1, 2, 3, 4}));
    }

    void adoptionTakesOnlySlicesWithNoOwner()
    {
        SliceOwnership own;
        own.noteSliceAdded(0);
        add(own, 1, kB);
        own.noteSliceAdded(2);
        own.noteSliceAdded(3);
        own.hold(3, kB);
        QCOMPARE(own.adoptUnowned(kA), (QList<int>{0, 2}));
        QCOMPARE(own.ownedBy(kA), (QList<int>{0, 2}));
        QCOMPARE(own.mark(1).owner, kB);
        // Held for B: never adopted, still the station device's.
        QCOMPARE(own.mark(3).owner, SliceOwnership::stationDevice());
        QCOMPARE(own.mark(3).heldFor, kB);
        QVERIFY(own.unowned().isEmpty());
        // Nothing left to adopt.
        QVERIFY(own.adoptUnowned(kB).isEmpty());
    }

    void heldSlicesReturnToTheirOwnerAndToNobodyElse()
    {
        SliceOwnership own;
        add(own, 0, kA);
        add(own, 1, kA);
        add(own, 2, kB);
        own.hold(0, kA);
        own.hold(1, kA);
        QCOMPARE(own.heldFor(kA), (QList<int>{0, 1}));
        QVERIFY(own.ownedBy(kA).isEmpty());
        QCOMPARE(own.ownedBy(SliceOwnership::stationDevice()), (QList<int>{0, 1}));
        QVERIFY(own.mark(0).isHeld());
        QCOMPARE(own.mark(0).subject(), kA);

        QVERIFY(own.returnHeld(kB).isEmpty());
        QCOMPARE(own.heldFor(kA), (QList<int>{0, 1}));

        QCOMPARE(own.returnHeld(kA), (QList<int>{0, 1}));
        QCOMPARE(own.ownedBy(kA), (QList<int>{0, 1}));
        QVERIFY(!own.mark(0).isHeld());
        QVERIFY(own.heldFor(kA).isEmpty());
    }

    void aHeldMarkIsAlwaysTheStationDevices()
    {
        SliceOwnership own;
        add(own, 0, kA);
        own.setMark(0, SliceOwnership::Mark{kB, kA});
        QCOMPARE(own.mark(0).owner, SliceOwnership::stationDevice());
        QCOMPARE(own.mark(0).heldFor, kA);
    }

    void aMarkChangeIsSignalledWithWhatItWas()
    {
        SliceOwnership own;
        own.noteSliceAdded(0);
        QSignalSpy spy(&own, &SliceOwnership::markChanged);
        own.adoptUnowned(kA);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 0);
        QVERIFY(spy.at(0).at(1).toByteArray().isEmpty());
        own.hold(0, kA);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(1).toByteArray(), kA);
        QVERIFY(spy.at(1).at(2).toByteArray().isEmpty());
        // The same mark again is no change.
        own.hold(0, kA);
        QCOMPARE(spy.count(), 2);
        // A slice that is not live has no mark to change.
        own.setOwner(7, kB);
        QCOMPARE(spy.count(), 2);
        QVERIFY(own.mark(7).owner.isEmpty());
    }

    void aSliceBeingRemovedIsNobodysButKeepsItsMarkUntilAnnounced()
    {
        SliceOwnership own;
        add(own, 0, kA);
        add(own, 1, kA);
        own.beginRemove(1);
        QVERIFY(!own.isLive(1));
        QCOMPARE(own.ownedBy(kA), (QList<int>{0}));
        // What is sent about its removal still knows whose it was.
        QCOMPARE(own.mark(1).owner, kA);
        own.endRemove(1);
        QVERIFY(own.mark(1).owner.isEmpty());
        QCOMPARE(own.liveSlices(), (QList<int>{0}));
    }

    void theManifestsOrderIsTheCreationOrder()
    {
        SliceOwnership own;
        add(own, 0, kA);
        add(own, 2, kA);
        add(own, 1, kA);
        own.setOrder({2, 1, 0});
        QCOMPARE(own.liveSlices(), (QList<int>{2, 1, 0}));
        QCOMPARE(own.activeFor(kA), 2);
        // Ids it does not know are left out; live ids it was not given stay,
        // after the ones it was.
        own.setOrder({1, 9});
        QCOMPARE(own.liveSlices(), (QList<int>{1, 2, 0}));
    }

    // ── The active slice ─────────────────────────────────────────────────

    void eachOwnerHasOneActiveSliceOfItsOwn()
    {
        SliceOwnership own;
        add(own, 0, kA);
        add(own, 1, kA);
        add(own, 2, kB);
        add(own, 3, kB);
        QCOMPARE(own.activeFor(kA), 0);
        QCOMPARE(own.activeFor(kB), 2);
        QCOMPARE(own.activeFor(QByteArrayLiteral("nobody")), -1);
        int activeA = 0;
        int activeB = 0;
        for (int id : own.liveSlices()) {
            if (own.isActive(id)) {
                (own.mark(id).owner == kA ? activeA : activeB) += 1;
            }
        }
        QCOMPARE(activeA, 1);
        QCOMPARE(activeB, 1);
    }

    void aChoiceByAMovesAsActiveSliceAndNeverBs()
    {
        SliceOwnership own;
        add(own, 0, kA);
        add(own, 1, kA);
        add(own, 2, kB);
        add(own, 3, kB);
        own.setActive(kB, 3);
        QSignalSpy spy(&own, &SliceOwnership::activeChanged);
        own.setActive(kA, 1);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(own.activeFor(kA), 1);
        QCOMPARE(own.activeFor(kB), 3);
        QVERIFY(own.isActive(1));
        QVERIFY(!own.isActive(0));
        QVERIFY(own.isActive(3));
        // A choice of another owner's slice is not a choice.
        own.setActive(kA, 2);
        QCOMPARE(own.activeFor(kA), 1);
        QCOMPARE(own.activeFor(kB), 3);
    }

    void aRemovedActiveSlicePassesToItsOwnersNext()
    {
        SliceOwnership own;
        add(own, 0, kA);
        add(own, 1, kA);
        add(own, 2, kA);
        own.setActive(kA, 1);
        own.beginRemove(1);
        QCOMPARE(own.activeFor(kA), 0);
        own.endRemove(1);
        QCOMPARE(own.activeFor(kA), 0);
        // A choice survives while the slice is away from its owner, and is
        // its active slice again when it comes back.
        own.setActive(kA, 2);
        own.hold(2, kA);
        QCOMPARE(own.activeFor(kA), 0);
        own.returnHeld(kA);
        QCOMPARE(own.activeFor(kA), 2);
    }

    void adoptionCarriesTheActiveSliceOver()
    {
        SliceOwnership own;
        own.noteSliceAdded(0);
        own.noteSliceAdded(1);
        own.setActive(QByteArray(), 1);
        own.adoptUnowned(kA);
        QCOMPARE(own.activeFor(kA), 1);
    }

    void theStationLevelActiveSliceFollowsTheMostRecentChoice()
    {
        SliceOwnership own;
        QCOMPARE(own.stationActiveSlice(), -1);
        add(own, 0, kA);
        add(own, 1, kB);
        own.setActive(kA, 0);
        QCOMPARE(own.stationActiveSlice(), 0);
        own.setActive(kB, 1);
        QCOMPARE(own.stationActiveSlice(), 1);
        // Removing it moves the station-level slice to its owner's next, or
        // to none when that owner has no other.
        add(own, 2, kB);
        own.beginRemove(1);
        QCOMPARE(own.stationActiveSlice(), 2);
        own.endRemove(1);
        own.beginRemove(2);
        QCOMPARE(own.stationActiveSlice(), -1);
    }

    void theTransmitHoldersActiveSliceWinsWhileItHoldsTransmit()
    {
        SliceOwnership own;
        add(own, 0, kA);
        add(own, 1, kB);
        own.setActive(kA, 0);
        own.setActive(kB, 1);
        QSignalSpy spy(&own, &SliceOwnership::activeChanged);
        own.setTransmitHolder(kA);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(own.stationActiveSlice(), 0);
        own.setActive(kB, 1);
        QCOMPARE(own.stationActiveSlice(), 0);
        own.setTransmitHolder(QByteArray());
        QCOMPARE(own.stationActiveSlice(), 1);
        // A holder that owns no slice leaves the most recent choice.
        own.setTransmitHolder(QByteArrayLiteral("device-c"));
        QCOMPARE(own.stationActiveSlice(), 1);
    }
};

QTEST_GUILESS_MAIN(TstSliceOwnership)
#include "tst_slice_ownership.moc"
