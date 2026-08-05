#pragma once
// =================================================================
// src/core/session/ObjectRegistry.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R2 Task 9.
//
// StateMirror (Tasks 7-8) forwards PROPERTY deltas for objects it already
// knows about. ObjectRegistry is what tells a remote peer an object
// APPEARED or VANISHED at all: it watches RadioModel::sliceAdded /
// sliceRemoved, calls StateMirror::watch()/unwatch() at the right moments,
// and emits the corresponding object.create / object.destroy events. Task
// 10's connect-time snapshot is built on StateMirror::snapshotAll() (which
// works over whatever is currently watched, independent of how it got
// watched) plus these two signals for whatever changes after that burst.
//
// Slice ids are reused: RadioModel::addSlice() always hands out the lowest
// id NOT currently in its slice list, and removeSlice() never renumbers
// survivors. The reuse hazard is sharper than it first looks:
// RadioModel::removeSlice() calls slice->deleteLater() and only THEN emits
// sliceRemoved() (RadioModel.cpp, verified at :5122-5123 against this
// tree's HEAD at commit ae65934f). deleteLater() only SCHEDULES deletion --
// the object is fully alive, with a valid vtable and live signals, for the
// rest of that call stack. A same-event-loop-turn addSlice() can therefore
// remint the freed id while the corpse from the removed slice still
// exists.
//
// Two things follow from that, and both are load-bearing:
//
//   - The live-object bookkeeping here is keyed by QPointer<SliceModel>,
//     read only at the moment onSliceRemoved() fires (see there for why
//     that read is still safe), never held past it.
//   - Detaching from StateMirror happens SYNCHRONOUSLY inside the
//     sliceRemoved handler, not on QObject::destroyed. destroyed() does
//     not arrive until the event loop next spins; waiting for it would
//     leave the corpse still watched under "slice:<id>" at the exact
//     moment a same-turn addSlice() tries to watch a DIFFERENT object
//     under that same key, and StateMirror::watch() refuses a key already
//     bound to a different object.
//
// A create for an id this registry already considers live (no intervening
// sliceRemoved) is a protocol error: RadioModel's own id allocator makes it
// unreachable through normal operation, so it is refused rather than
// silently clobbering the existing watch, which would orphan it inside
// StateMirror while this registry's own bookkeeping moved on.
//
// Scoped to slices only, matching this task's brief ("Task 9: Slice
// lifecycle, tolerant of id reuse"). The other four mirrored model classes
// (RadioModel, TransmitModel, TunerModel, PanadapterModel) are constructed
// once at startup and never recreated within a session, so they need no
// lifecycle tracking of their own -- Task 10's connect-time snapshot
// watches them directly. The class is named generally because "object" is
// the wire vocabulary term (docs/architecture/2026-08-03-remote-daemon-r2-
// r3-design-addendum.md section 7), not because this task builds a
// general-purpose registry for every mirrored class.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-05  J.J. Boyd / KG4VCF  Remote daemon R2 Task 9: slice
//                                    lifecycle object registry
//                                    (create/destroy tracking, id-reuse
//                                    tolerant). AI-assisted transformation
//                                    via Anthropic Claude Code.
// =================================================================

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPointer>

#include "core/session/MirrorSchema.h"

namespace NereusSDR {

class RadioModel;
class SliceModel;
class StateMirror;

class ObjectRegistry : public QObject {
    Q_OBJECT

public:
    /// `radioModel` is watched for sliceAdded/sliceRemoved; `mirror` is the
    /// StateMirror this registry watches/unwatches slices on and reads
    /// snapshots from. Neither is owned; both are held as QPointer so
    /// either being destroyed first (in either order, relative to this
    /// object) cannot leave a dangling raw pointer behind.
    explicit ObjectRegistry(RadioModel* radioModel, StateMirror* mirror,
                            QObject* parent = nullptr);
    ~ObjectRegistry() override;

    /// The wire identity StateMirror watches a slice under: "slice:<id>".
    /// Shared so every caller -- Task 10's snapshot burst, Task 18's wire
    /// encoder, this class's own tests -- builds the identical string.
    static QByteArray keyForSlice(int sliceId);

    /// True while this registry currently has `sliceId` watched (between
    /// its create and its destroy).
    bool isLive(int sliceId) const;

    /// Currently-live slice ids, ascending.
    QList<int> liveSliceIds() const;

signals:
    /// A slice appeared and is now watched. `snapshot` is its FULL settled
    /// state -- StateMirror::snapshot() read immediately after watch()
    /// succeeds -- the same shape Task 10's connect-time burst sends for an
    /// object that was already live when a client connects.
    void objectCreated(const QByteArray& objectKey, const QByteArray& className,
                       int id, const QList<NereusSDR::MirrorUpdate>& snapshot);

    /// A slice vanished and has ALREADY been unwatched by the time this
    /// fires: no further delta for this key can arrive after this signal,
    /// even from the dying object, which may still be alive (deleteLater())
    /// at this exact moment.
    void objectDestroyed(const QByteArray& objectKey, const QByteArray& className,
                         int id);

public slots:
    /// Connected to RadioModel::sliceAdded. Public so a test can drive it
    /// directly (see tst_mirror_lifecycle.cpp's duplicate-create case);
    /// not meant to be called from production code other than via that
    /// connection.
    void onSliceAdded(int sliceId);

    /// Connected to RadioModel::sliceRemoved. See the class comment for why
    /// detaching here, synchronously, rather than on QObject::destroyed, is
    /// the entire mechanism this class exists to provide.
    void onSliceRemoved(int sliceId);

private:
    QPointer<RadioModel> m_radioModel;
    QPointer<StateMirror> m_mirror;

    /// Ordered by id (ascending), not by attach time. NereusSDR's slice
    /// count tops out around five, so the ordering guarantee QMap gives
    /// liveSliceIds() for free is not worth a second, insertion-ordered
    /// structure to get "true" creation order instead.
    QMap<int, QPointer<SliceModel>> m_live;
};

} // namespace NereusSDR
