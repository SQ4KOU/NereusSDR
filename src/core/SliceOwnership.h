#pragma once
// no-port-check: NereusSDR-original.
// =================================================================
// src/core/SliceOwnership.h  (NereusSDR)
// =================================================================
//
// Whose each slice is (iPhone app plan Task 73, R-IOS-02; the several-
// devices design, docs/architecture/2026-09-24-several-devices-on-one-core-
// design.md, sections 5.1, 5.2 and 5.7, rulings 5.1, 5.2, 5.10 and 5.11).
//
// Up to four devices use one Core at once, and each owns its slices (D45).
// This class is the Core's record of that: every live slice's owner, a
// second mark, **held for**, on a slice the station device runs for a
// device that is not there, and each owner's active slice. It owns no slice
// and sends nothing: RadioModel tells it when slices come and go, and the
// Core's session server reads it to decide which device sees which object
// and who may change what. Task 74 adds each receiver's anchor.
//
// ---- Owners (ruling 5.1) ----
//
// A slice's owner is one of:
//   - a device, by the id the Core's session registry knows it by (a paired
//     device's raw key id, or "token:<n>" for a window signed in with the
//     older token);
//   - the station device, stationDevice(): the operating position at the
//     radio itself. On a Core with no desktop it owns only slices it runs
//     for absent devices, each held for one (heldFor);
//   - none (an empty id): the slices a Core made at its first start, or
//     restored from a manifest written before owners existed, until the
//     first device admitted while no other is connected adopts them.
//
// Owner marks live at the Core. On the wire they show only as which slice:
// objects a device receives and as the owner fields of markers.
//
// ---- The active slice (rulings 5.10, 5.11) ----
//
// Each owner has one active slice among its own (activeFor): the one it
// last chose while it still owns it, else its first slice in creation
// order. A slice's `active` property means "its owner's active slice"
// (isActive), so each device sees exactly one active slice among its own.
// The station-level active slice, which drives the duties that exist once
// per radio, is the transmit holder's active slice while transmit is held,
// otherwise the most recent active-slice choice by any owner
// (stationActiveSlice). Until remote transmit exists nobody holds it.
//
// ---- New slices ----
//
// A slice made while a CreatorScope is open belongs to that scope's owner
// (a device's addSlice, its first slice at admission, a restored slice);
// any other new slice has no owner.
//
// ---- Incarnation and control revision (slice control plan Task 1) ----
//
// A slice's id is its letter and is reused, lowest free first, once the
// slice closes. Its incarnation is not: noteSliceAdded gives each slice a
// value no other slice of this run had, kept until endRemove. It is a
// 20-bit boot nonce, drawn at random when this object is made, shifted
// left 32, plus a 32-bit counter, so it stays below 2^53 (exact as a JSON
// number) and a Core started again does not repeat the last run's values.
// A stored question or command names a slice by its SliceRef and acts only
// while matches() holds, so it never reaches a reused letter.
//
// A slice's control revision starts at 1 and rises by one on each change
// of mark().owner: adoption, hold, return, take, release. A command that
// carries the revision it saw acts only on the assignment it saw.
//
// ---- Listeners (slice control plan Task 2) ----
//
// A slice's controller is its owner, mark().owner. Other devices may be
// joined to it as listeners: they see and hear it but never change it.
// listenersOf() names the controller first, then every other joined device
// in join order; isListening() is true for each of them. Who may see, hear
// or change a slice is decided by SliceAccessPolicy
// (core/session/SliceAccessPolicy.h), never by comparing marks at a call
// site. Task 3 adds how devices join and leave; until then nobody but the
// controller is a listener outside a test.
//
// Single thread: RadioModel's.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-25: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), iPhone app plan Task 73 (R-IOS-02), with
//               AI-assisted implementation via Anthropic Claude Code.
//   2026-09-25: iPhone app plan Task 74 (R-IOS-02, R-IOS-30): each
//               receiver's anchor (rulings 6.2, 6.3). J.J. Boyd (KG4VCF),
//               with AI-assisted implementation via Anthropic Claude Code.
//   2026-09-28: slice control and shared listening plan Task 1: each
//               slice's incarnation and control revision. J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
//   2026-09-28: slice control and shared listening plan Task 2: each
//               slice's listener set (isListening, listenersOf). J.J.
//               Boyd (KG4VCF), with AI-assisted implementation via
//               Anthropic Claude Code.
// =================================================================

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>

#include <functional>

namespace NereusSDR {

class SliceOwnership : public QObject {
    Q_OBJECT

public:
    struct Mark {
        /// The owner's id, stationDevice(), or empty for no owner.
        QByteArray owner;
        /// The device the station device runs this slice for; set only
        /// when owner is stationDevice().
        QByteArray heldFor;

        bool operator==(const Mark& other) const
        {
            return owner == other.owner && heldFor == other.heldFor;
        }
        bool operator!=(const Mark& other) const { return !(*this == other); }
        bool isHeld() const { return !heldFor.isEmpty(); }
        /// Whose slice it is on the wire: the device it is held for, else
        /// its owner. Its marker goes to every view but this one's.
        QByteArray subject() const { return heldFor.isEmpty() ? owner : heldFor; }
    };

    /// One slice as it was when named: its reusable id and the incarnation
    /// that id had then.
    struct SliceRef {
        int sliceId = -1;
        quint64 incarnation = 0;

        bool operator==(const SliceRef& other) const
        {
            return sliceId == other.sliceId && incarnation == other.incarnation;
        }
        bool operator!=(const SliceRef& other) const { return !(*this == other); }
    };

    /// The station device's id. Never a paired device's id (those are 32
    /// raw bytes) or a token window's ("token:<n>").
    static const QByteArray& stationDevice();

    /// The boot nonce's width: incarnations stay below 2^(32 + kNonceBits).
    static constexpr int kNonceBits = 20;

    /// Draws the boot nonce at random.
    explicit SliceOwnership(QObject* parent = nullptr);
    /// With a given boot nonce (only its low kNonceBits bits are used), so
    /// a test can stand for a Core started again.
    SliceOwnership(quint32 bootNonce, QObject* parent);

    // ---- Lifecycle (RadioModel) ----

    /// A slice was made: appended to the creation order, owned by the open
    /// CreatorScope's owner, or by none.
    void noteSliceAdded(int sliceId);
    /// A slice is being removed: from now on it is nobody's slice and no
    /// owner's active slice, but its mark is kept until endRemove(), so what
    /// is sent about its removal still knows whose it was. The most recent
    /// choice moves to its owner's next slice.
    void beginRemove(int sliceId);
    /// The slice's removal has been announced; its mark is forgotten.
    void endRemove(int sliceId);
    /// The creation order, as the restart manifest listed it.
    void setOrder(const QList<int>& sliceIds);

    bool isLive(int sliceId) const;
    /// Every live slice, in creation order.
    QList<int> liveSlices() const;

    // ---- Incarnation and control revision (slice control plan Task 1) ----

    /// A live slice's incarnation; 0 when it is not live.
    quint64 incarnation(int sliceId) const;
    /// A live slice's control revision (1 when made, +1 on each change of
    /// owner); 0 when it is not live.
    quint64 controlRevision(int sliceId) const;
    /// Whether `ref` names a live slice with the same incarnation.
    bool matches(const SliceRef& ref) const;
    /// `sliceId` with its incarnation; incarnation 0 when not live.
    SliceRef refOf(int sliceId) const;

    // ---- Listeners (slice control plan Task 2) ----

    /// Whether `device` is joined to the slice: its controller, or another
    /// device listening to it. False for an empty id and for a slice that
    /// is neither live nor being removed.
    bool isListening(const QByteArray& device, int sliceId) const;
    /// The controller (when it has one) then every other joined device, in
    /// join order; empty for a slice nobody is joined to.
    QList<QByteArray> listenersOf(int sliceId) const;
    /// Stands a listener set in for Task 3's join and leave, for tests of
    /// the access rules: `devices` (the controller need not be named) are
    /// the slice's listeners from now on. Ignored for a slice that is not
    /// live.
    void setListenersForTest(int sliceId, const QList<QByteArray>& devices);

    // ---- Marks ----

    /// A live slice's mark, or a slice's being removed; empty otherwise.
    Mark mark(int sliceId) const;
    /// Sets a live slice's mark; markChanged when it differs. A held mark
    /// always has the station device as owner.
    void setMark(int sliceId, const Mark& mark);
    void setOwner(int sliceId, const QByteArray& owner);
    /// The station device runs the slice for `device`, which is absent.
    void hold(int sliceId, const QByteArray& device);

    /// Live slices whose owner is `owner` (for stationDevice(), held ones
    /// included), in creation order.
    QList<int> ownedBy(const QByteArray& owner) const;
    /// Live slices held for `device`, in creation order.
    QList<int> heldFor(const QByteArray& device) const;
    /// Live slices with no owner, in creation order.
    QList<int> unowned() const;

    /// Ruling 5.2 step 1: slices held for `device` become its own again.
    /// Returns them.
    QList<int> returnHeld(const QByteArray& device);
    /// Ruling 5.2 step 3: `device` adopts every slice with no owner (never a
    /// slice held for another device). The slice that was active among the
    /// unowned ones becomes its active slice when it has none. Returns them.
    QList<int> adoptUnowned(const QByteArray& device);

    // ---- The active slice ----

    /// `owner`'s active slice: the one it last chose while it still owns it,
    /// else its first in creation order; -1 when it owns none.
    int activeFor(const QByteArray& owner) const;
    /// Whether `sliceId` is its owner's active slice (the slice's `active`).
    bool isActive(int sliceId) const;
    /// `owner` chose `sliceId`; it is also now the most recent choice.
    void setActive(const QByteArray& owner, int sliceId);
    /// The station-level active slice (ruling 5.11); -1 when none is known.
    int stationActiveSlice() const;
    /// Task 34 names the transmit holder; empty while nobody holds it.
    void setTransmitHolder(const QByteArray& holder);
    QByteArray transmitHolder() const { return m_transmitHolder; }

    // ---- Anchors (Task 74) ----

    /// A slice's receiver changed to `stream` (-1: it has none). May come
    /// before noteSliceAdded (RadioModel binds a new slice first).
    void noteStream(int sliceId, int stream);
    /// The device anchoring `stream`: the subject of the slice that claimed
    /// it, or of the longest-staying one since. Empty when the receiver is
    /// free or its anchoring slice has no owner.
    QByteArray anchorOf(int stream) const;
    /// The slices on `stream`, longest there first.
    QList<int> slicesOnStreamInJoinOrder(int stream) const;

    // ---- New slices ----

    /// While one is open, a new slice belongs to its owner. Scopes nest.
    class CreatorScope {
    public:
        CreatorScope(SliceOwnership* ownership, const QByteArray& owner);
        ~CreatorScope();
        CreatorScope(const CreatorScope&) = delete;
        CreatorScope& operator=(const CreatorScope&) = delete;

    private:
        SliceOwnership* m_ownership;
        QByteArray m_previous;
    };
    QByteArray creator() const { return m_creator; }

signals:
    /// A live slice's mark changed from (oldOwner, oldHeldFor) to what
    /// mark() now reads.
    void markChanged(int sliceId, const QByteArray& oldOwner, const QByteArray& oldHeldFor);
    /// Some owner's active slice, or the station-level one, may have moved.
    void activeChanged();
    /// A live slice's owner changed; `revision` is its new control revision.
    void controlRevisionChanged(int sliceId, quint64 revision);

private:
    QList<int> matching(const std::function<bool(const Mark&)>& test) const;
    QByteArray subjectOf(int sliceId) const { return m_marks.value(sliceId).subject(); }
    void leaveStream(int sliceId);

    QList<int> m_order;
    QHash<int, Mark> m_marks;
    QSet<int> m_removing;
    QHash<QByteArray, int> m_chosen;
    int m_mostRecent = -1;
    QByteArray m_transmitHolder;
    QByteArray m_creator;
    // Task 1 (slice control plan): the boot nonce, already shifted, the
    // last counter used, and each slice's incarnation and control revision
    // (kept until endRemove).
    quint64 m_incarnationBase = 0;
    quint32 m_incarnationCounter = 0;
    QHash<int, quint64> m_incarnations;
    QHash<int, quint64> m_revisions;
    // Task 2 (slice control plan): each slice's joined devices other than
    // its controller, in join order (kept until endRemove).
    QHash<int, QList<QByteArray>> m_listeners;
    // Task 74: each slice's receiver, the slices on each receiver in the
    // order they arrived, and each receiver's anchor.
    QHash<int, int> m_streamOf;
    QHash<int, QList<int>> m_joinOrder;
    QHash<int, QByteArray> m_anchor;
};

} // namespace NereusSDR
