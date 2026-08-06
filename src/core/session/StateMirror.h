#pragma once
// =================================================================
// src/core/session/StateMirror.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R2 Task 7.
//
// Reflects model state out to a remote GUI, and applies what a remote peer
// sends back. The OUTBOUND half watches model instances and emits their
// changes, encoded to wire kinds and identified by ordinal. The INBOUND
// half, applyInbound(), is Task 8. Binding either half onto a real wss
// session is Task 18; nothing constructs a StateMirror in production yet.
//
// The mechanism is one watcher per instance and ONE zero-argument slot
// connected to every distinct NOTIFY signal in that instance's
// MirrorSchema, with Qt::UniqueConnection. Notifiers are one-to-many, so
// several properties routinely ask for the same signal: filterChanged
// names filterLow and filterHigh, PanadapterModel::levelChanged names
// dBmFloor and dBmCeiling, TunerModel::stateChanged names four.
// UniqueConnection is what makes the second and later asks free.
//
// When a notifier fires, senderSignalIndex() identifies it and the schema
// maps it back to the ordinals it announces. Each is then RE-READ through
// QMetaProperty::read. Re-reading rather than taking the signal's
// arguments is not a stylistic preference; two independent facts force it:
//
//   - Arity 0 notifiers carry nothing to take. PanadapterModel::
//     levelChanged() announces two properties and has no arguments at all.
//   - Side effects. SliceModel::setDspMode rewrites both filter edges as
//     part of its per-(band, mode) LastFilter handling and emits
//     filterChanged separately, so a single operator action moves three
//     properties across two signals. Only a re-read is guaranteed to
//     agree with what the model actually holds.
//
// CONSTANT properties have no NOTIFY and so are invisible to the watcher
// by construction. They travel only in snapshot(), which reads the whole
// schema explicitly. SliceModel::sliceIndex is the case that matters: it
// is the mirror's object identity, and without the explicit read every
// object.create on the wire would be anonymous.
//
// applyInbound() (Task 8) is the INBOUND half. A property with a real
// Q_PROPERTY WRITE is gated by MirrorPolicy::inboundAllowed() and then
// decoded/written through MirrorSchema, exactly like snapshot()'s read
// path in reverse. A property with NO WRITE at all -- the case
// MirrorPolicy always resolves to Outbound, because there is nothing
// standard for it to write through -- gets one more chance: the target
// model's own applyMirroredValue(name, value) hook (Q_INVOKABLE, called by
// name through QMetaObject::invokeMethod so this file never needs to
// #include a model header) may translate the intent into a real command,
// the way TunerModel::isOperate becomes a call to setOperate(). A
// CONSTANT property never reaches the hook; it is refused before that,
// same as any other property with no legitimate inbound path.
//
// m_applying guards the whole call. It is checked at the very top of
// onWatchedPropertyChanged(), before that slot even asks which object
// fired it, so it suppresses every notify produced while an inbound apply
// is in flight -- not only the one on the property just written, but any
// same-thread, synchronous side effect that write triggers on ANOTHER
// watched object. RadioModel::addSlice() wires exactly such a side effect
// unconditionally: co-hosted slices (several demod slices sharing one DDC
// stream, hence one physical WDSP blanker) mirror nbMode and the NB1/NB2
// tuning knobs to each other so they can never disagree, whether or not a
// mirror -- or even a radio -- is involved. That mirroring must still run
// during an inbound apply (skipping it would desync the peer's real WDSP
// state from the one that just changed), but its result must not be
// forwarded outbound as a delta the remote peer never asked for. A single
// object-agnostic check accomplishes both without RadioModel needing to
// know a StateMirror exists.
//
// attachSession() (Task 10) is the CONNECT-TIME half: it sends whatever a
// brand-new session needs to build a client-side mirror that agrees with
// this daemon from the first message it receives -- a schema per distinct
// watched class, a full-property-bag object.create per watched object,
// and a snapshot-complete marker -- all via sessionMessageReady(),
// synchronously, before it returns. See its own doc comment for the
// single-thread precondition this rests on, which is the same class of
// dependency the m_applying paragraph above already describes for inbound
// echo suppression: both work only because Qt::AutoConnection resolves to
// a direct call, which is only true while sender and receiver share a
// thread.
//
// MirrorCoalescer (below) is what keeps a property change that happens
// SYNCHRONOUSLY during attachSession() -- a receiver reacting to one of
// its own burst messages by writing a watched object -- from being
// observed ahead of the snapshot-complete marker: onWatchedPropertyChanged()
// routes every change into it once a session has been attached, and
// sessionMessageReady() emits a Delta ONLY from flushCoalescedDeltas(),
// which attachSession() calls exactly once, as its LAST step. Outside of
// an attached session, none of this runs at all -- propertiesChanged()
// (Tasks 7-8's own signal) is untouched and fires exactly as it always
// has, which is what keeps local direct mode from regressing.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-05  J.J. Boyd / KG4VCF  Remote daemon R2 Task 7: outbound
//                                    state forwarder. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
//   2026-08-05  J.J. Boyd / KG4VCF  Remote daemon R2 Task 8: inbound apply
//                                    (applyInbound, the per-model
//                                    applyMirroredValue hook dispatch, and
//                                    the m_applying forwarding guard). AI-
//                                    assisted transformation via Anthropic
//                                    Claude Code.
//   2026-08-05  J.J. Boyd / KG4VCF  Remote daemon R2 Task 10: connect-time
//                                    snapshot (attachSession), the
//                                    outbound coalescer (MirrorCoalescer),
//                                    and flushCoalescedDeltas. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
// =================================================================

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QPair>
#include <QQueue>
#include <QString>
#include <QVariant>

#include "core/session/MirrorSchema.h"
#include "core/session/SessionMessages.h"

namespace NereusSDR {

/// Outbound delta coalescer: TciVfoCoalescer's shape (TciVfoCoalescer.h --
/// QHash latest-wins storage plus a QQueue for arrival order, mutex
/// guarded), generalised from a QString value to MirrorUpdate. A sibling
/// class, not a reuse: TciVfoCoalescer's value type is QString throughout
/// (`update(const QString&, const QString&)`, `QQueue<QString> m_order`),
/// so there is no shared implementation to factor out without contorting
/// the existing one into something it was never asked to be.
///
/// Keyed by (objectKey, ordinal) -- flattened here as an object-then-
/// ordinal nesting rather than a single compound-key hash, so flush() can
/// group every pending property of one object into the single batch a
/// Delta SessionMessage carries. The same property on two different
/// objects must not collide, and two different properties on the same
/// object must not collide either: SliceModel::restoreFromSettings
/// touches dozens of ordinals on ONE object, and a multi-slice band
/// change touches the SAME ordinal (band) on several.
///
/// Latest-wins is structural, not order-of-processing: a second update()
/// for a key already pending OVERWRITES its stored value, so the slot
/// always holds whichever call happened most recently, with nothing for
/// flush() to compute by comparing arrival timestamps itself.
///
/// That stored value is a PROVISIONAL marker, not the authoritative one a
/// Delta ends up carrying on the wire. StateMirror::flushCoalescedDeltas()
/// re-resolves each pending (objectKey, ordinal) against the LIVE watched
/// object at flush time rather than trusting what is stored here, and
/// that re-resolution is load-bearing, not defensive. A first version of
/// this comment argued the stored value could not go stale, on the
/// premise that nothing can change a watched object without going through
/// onWatchedPropertyChanged()'s re-read first -- but applyInbound()'s
/// m_applying guard (StateMirror.cpp) exists specifically to suppress
/// that notify, so a remote peer's write does not echo straight back to
/// itself. That means a property this coalescer already has pending CAN
/// be changed again on the model without update() ever being called a
/// second time for it, and reporting the stored value at flush would then
/// hand a peer a value the daemon no longer holds. The same
/// re-resolution against the live watch list is also what lets
/// flushCoalescedDeltas() silently drop a key that was unwatched (or
/// whose object was destroyed) before its pending property was ever
/// flushed, instead of emitting a Delta naming an object the peer has
/// already been told is gone. This class's own job stays narrow: record
/// which (objectKey, ordinal) pairs are dirty, in arrival order, with a
/// provisional value for convenience. Resolving that into what actually
/// goes on the wire is StateMirror's job, not this class's.
class MirrorCoalescer {
public:
    /// Marks `objectKey`'s `changed.ordinal` dirty, storing `changed` as a
    /// PROVISIONAL latest value (see the class comment for why flush()
    /// does not simply hand this back out). A second call for the same
    /// (objectKey, ordinal) before the next flush() overwrites the first.
    void update(const QByteArray& objectKey, const MirrorUpdate& changed);

    /// Every pending object, in the order each one FIRST went dirty since
    /// the last flush; within each object, its properties in the order
    /// each ordinal FIRST went dirty. Clears all pending state. The
    /// MirrorUpdate values returned are the PROVISIONAL ones update() was
    /// called with; StateMirror::flushCoalescedDeltas() is what
    /// re-resolves them against the live model before anything reaches
    /// the wire.
    QList<QPair<QByteArray, QList<MirrorUpdate>>> flush();

    /// Drops everything pending without returning it.
    void clear();

    /// Distinct objects with at least one pending property.
    int pendingObjectCount() const;

    /// Distinct (objectKey, ordinal) pairs pending, summed across objects.
    int pendingPropertyCount() const;

private:
    mutable QMutex m_mutex;
    QQueue<QByteArray> m_objectOrder;
    QHash<QByteArray, QList<quint16>> m_ordinalOrder;
    QHash<QByteArray, QHash<quint16, MirrorUpdate>> m_values;
};

/// Outcome of one applyInbound() call.
struct MirrorApplyResult {
    /// True when the write landed -- either through the property's own
    /// WRITE accessor, or through the target model's applyMirroredValue
    /// hook. False leaves every bit of state exactly as it was.
    bool accepted = false;

    QByteArray objectKey;
    QByteArray property;

    /// Empty iff accepted. Otherwise a reason suitable for relaying to the
    /// remote peer. Where the R2 plan assigns a client command to use
    /// instead of writing the property directly, the reason names that
    /// verb literally (today, only sampleRateHz -> requestSliceSampleRate;
    /// Task 11 owns the rest of the command set).
    QString reason;
};

class StateMirror : public QObject {
    Q_OBJECT

public:
    explicit StateMirror(QObject* parent = nullptr);
    ~StateMirror() override;

    /// Begin mirroring `object` under `objectKey` (the wire identity, for
    /// example "slice:3"). Idempotent for the same key and object.
    ///
    /// Returns false, changing nothing, for a null object, an empty key, a
    /// class outside the mirrored surface, or a key already bound to a
    /// DIFFERENT object -- rebinding a key silently would hand a remote
    /// GUI two objects' state under one identity.
    bool watch(const QByteArray& objectKey, QObject* object);

    void unwatch(const QByteArray& objectKey);
    void unwatchAll();

    bool isWatching(const QByteArray& objectKey) const;

    /// Watched keys in the order they were added.
    QList<QByteArray> watchedKeys() const;

    QObject* watchedObject(const QByteArray& objectKey) const;
    QByteArray keyFor(const QObject* object) const;

    /// Full current state of one watched object, INCLUDING its CONSTANT
    /// properties, which no notifier can ever produce. Empty for an
    /// unwatched key.
    QList<MirrorUpdate> snapshot(const QByteArray& objectKey) const;

    /// snapshot() for every watched object, in watch order. Task 10's
    /// connect-time burst is built from this.
    QList<QPair<QByteArray, QList<MirrorUpdate>>> snapshotAll() const;

    /// PRECONDITION -- single-threaded, and load-bearing, not
    /// documentation hygiene: this call is "one uninterrupted event-loop
    /// turn" (the property the ordering guarantee below rests on) only so
    /// long as this StateMirror, every object it watches, and whatever
    /// thread calls this method are all the SAME thread. Every connection
    /// watch() makes is Qt::AutoConnection, which resolves to a DIRECT
    /// call only because sender and receiver share a thread today -- the
    /// identical dependency the class-level comment documents for
    /// m_applying's inbound echo suppression. If a future task gives the
    /// session (and therefore whatever calls attachSession() and
    /// flushCoalescedDeltas()) its own thread while the watched models
    /// stay on the model thread, AutoConnection silently becomes queued,
    /// this method can return before the burst it describes has actually
    /// been delivered, and nothing here fails a test to say so: every
    /// test in this suite, like Tasks 7 through 9's, constructs on one
    /// thread. Task 18 must keep StateMirror, ObjectRegistry, and every
    /// watched model on the SAME thread as whatever attaches the session.
    ///
    /// Sends, in order, via sessionMessageReady(): one Schema message per
    /// DISTINCT class among the objects currently watched (first-watched
    /// order), one ObjectCreate per watched object (the FULL property
    /// bag from snapshot(), watch order), then the SnapshotComplete
    /// marker. A property change that happens SYNCHRONOUSLY during this
    /// call -- a receiver of one of the messages above reacting by
    /// writing a watched object -- is not lost and cannot be observed
    /// ahead of the marker: onWatchedPropertyChanged() always routes into
    /// the outbound coalescer once a session is attached, rather than
    /// emitting a Delta directly, and this method only drains the
    /// coalescer (flushCoalescedDeltas()) as its LAST step.
    ///
    /// Safe to call again later (a reconnecting client, Task 19):
    /// discards whatever the coalescer was still holding (the fresh burst
    /// this call is about to send already reflects every watched object's
    /// CURRENT state, so nothing queued from before could tell the new
    /// caller anything its own first object.create will not already say)
    /// and sends a complete burst again.
    void attachSession();

    /// Drains the outbound coalescer and emits one Delta
    /// sessionMessageReady() per object with anything pending. Returns the
    /// number of Delta messages emitted. attachSession() calls this itself
    /// as its last step; a periodic caller (Task 18's flush timer, once a
    /// live session exists) is expected to call it again on whatever
    /// cadence that session decides.
    ///
    /// This is where a pending property's value is actually decided, by
    /// re-resolving each (objectKey, ordinal) MirrorCoalescer::flush()
    /// returns against `m_watches` rather than trusting the PROVISIONAL
    /// value the coalescer stored (see MirrorCoalescer's class comment for
    /// why that value can go stale -- applyInbound()'s m_applying guard is
    /// the concrete case). Two things follow from doing the resolution
    /// here instead:
    ///
    ///   - A key still watched yields whatever the live object holds
    ///     RIGHT NOW, which is correct even if something changed it again,
    ///     through applyInbound(), after it was marked dirty and before
    ///     this call.
    ///   - A key no longer watched -- unwatched, or its object destroyed,
    ///     since it was marked dirty -- resolves to nothing at all, and
    ///     that batch is silently dropped rather than sent. No Delta this
    ///     method emits can ever name an object the peer was already told
    ///     is gone (or was never told about at all).
    int flushCoalescedDeltas();

    /// True once attachSession() has run at least once. Local direct mode
    /// (propertiesChanged(), Tasks 7-8) never checks this; it exists so a
    /// test -- or a future caller -- can tell without depending on
    /// sessionMessageReady() having fired anything yet.
    bool hasAttachedSession() const { return m_hasAttachedSession; }

    /// Apply one write a remote peer sent for a watched object's property,
    /// looked up by NAME. See the class-level comment for the three-way
    /// dispatch (Bidirectional WRITE / refused-with-reason / the
    /// applyMirroredValue hook) and for what m_applying suppresses while
    /// this runs.
    MirrorApplyResult applyInbound(const QByteArray& objectKey,
                                   const QByteArray& propertyName,
                                   const QVariant& wireValue);

    /// Same, keyed by the property's wire ordinal -- what an actual wire
    /// frame (Task 18) carries -- rather than its name.
    MirrorApplyResult applyInbound(const QByteArray& objectKey,
                                   quint16 ordinal,
                                   const QVariant& wireValue);

signals:
    /// One notifier firing produces one batch. A shared notifier therefore
    /// delivers all the properties it announces together, which is also
    /// the natural unit for a single wire frame.
    ///
    /// Fires exactly as it did in Tasks 7-8, regardless of whether a
    /// session has ever been attached -- attachSession() and the
    /// coalescer are ADDITIVE (sessionMessageReady() below), never a
    /// replacement, which is what keeps local direct mode from
    /// regressing.
    void propertiesChanged(const QByteArray& objectKey,
                           const QList<NereusSDR::MirrorUpdate>& updates);

    /// Task 10's session-facing stream: every Schema / ObjectCreate /
    /// Delta / SnapshotComplete message this mirror ever sends, in the
    /// exact order a session (Task 18) must relay them on the wire.
    /// Never fires before attachSession() has run at least once.
    void sessionMessageReady(const NereusSDR::SessionMessage& message);

private slots:
    /// The single zero-argument slot every watched notifier connects to,
    /// at every arity. Must stay zero-argument: that is precisely what
    /// lets it absorb filterChanged(int, int) and stateChanged() alike.
    void onWatchedPropertyChanged();

    void onWatchedObjectDestroyed(QObject* object);

private:
    struct Watch {
        QByteArray key;
        QObject* object = nullptr;
        const MirrorSchema* schema = nullptr;
    };

    int indexOfKey(const QByteArray& objectKey) const;
    int indexOfObject(const QObject* object) const;
    void detach(const Watch& watch);

    /// Shared body of both applyInbound() overloads once the property has
    /// been resolved.
    MirrorApplyResult applyInboundToProperty(const Watch& watch,
                                             const MirrorProperty& prop,
                                             const QVariant& wireValue);

    QList<Watch> m_watches;

    /// True for the duration of one applyInbound() call. Checked first in
    /// onWatchedPropertyChanged(), before that slot asks which object fired
    /// it, so it suppresses every notify produced while true -- not only on
    /// the property applyInbound() is writing, but on any other watched
    /// object a same-thread, synchronous side effect of that write touches.
    /// See the class-level comment.
    bool m_applying = false;

    /// True from the first attachSession() call onward. Gates whether
    /// onWatchedPropertyChanged() feeds m_coalescer at all: while false
    /// (no session has ever attached, which is every Task 7/8/9 test and
    /// every use of this class before Task 10), NOTHING about this task's
    /// changes runs, which is the mechanism behind "the coalescer must
    /// not change any behaviour when no session is attached."
    bool m_hasAttachedSession = false;

    /// Task 10's outbound coalescer. See MirrorCoalescer's own class
    /// comment and attachSession()'s doc comment for how the two
    /// cooperate to keep a mid-burst change from being observed ahead of
    /// the snapshot-complete marker.
    MirrorCoalescer m_coalescer;
};

} // namespace NereusSDR
