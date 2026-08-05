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
// =================================================================

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QVariant>

#include "core/session/MirrorSchema.h"

namespace NereusSDR {

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
    void propertiesChanged(const QByteArray& objectKey,
                           const QList<NereusSDR::MirrorUpdate>& updates);

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
};

} // namespace NereusSDR
