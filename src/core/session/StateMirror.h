#pragma once
// =================================================================
// src/core/session/StateMirror.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R2 Task 7.
//
// Reflects model state out to a remote GUI. This is the OUTBOUND half:
// it watches model instances and emits their changes, encoded to wire
// kinds and identified by ordinal. Applying what a remote peer sends back
// is Task 8, and binding either half onto a real wss session is Task 18;
// nothing constructs a StateMirror in production yet.
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
// =================================================================
// Modification history (NereusSDR):
//   2026-08-05  J.J. Boyd / KG4VCF  Remote daemon R2 Task 7: outbound
//                                    state forwarder. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
// =================================================================

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPair>

#include "core/session/MirrorSchema.h"

namespace NereusSDR {

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

    QList<Watch> m_watches;
};

} // namespace NereusSDR
