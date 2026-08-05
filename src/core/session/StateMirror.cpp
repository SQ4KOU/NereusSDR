// =================================================================
// src/core/session/StateMirror.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R2 Task 7.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-05  J.J. Boyd / KG4VCF  Remote daemon R2 Task 7: outbound
//                                    state forwarder. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
// =================================================================

#include "core/session/StateMirror.h"

#include <QLoggingCategory>
#include <QMetaMethod>
#include <QMetaObject>

Q_LOGGING_CATEGORY(lcStateMirror, "nereus.mirror")

namespace NereusSDR {

namespace {

// Resolved once. Renaming the slot without updating this string would
// strand every watched object silently, so the lookup is loud on failure
// and StateMirror::watch refuses to pretend it worked.
const QMetaMethod& watcherSlot()
{
    static const QMetaMethod slot = [] {
        const int index =
            StateMirror::staticMetaObject.indexOfSlot("onWatchedPropertyChanged()");
        if (index < 0) {
            qCCritical(lcStateMirror)
                << "onWatchedPropertyChanged() slot not found; no model state "
                   "will be mirrored";
            return QMetaMethod();
        }
        return StateMirror::staticMetaObject.method(index);
    }();
    return slot;
}

} // namespace

StateMirror::StateMirror(QObject* parent)
    : QObject(parent)
{
}

StateMirror::~StateMirror()
{
    unwatchAll();
}

// ── Lookup helpers ────────────────────────────────────────────────────────

int StateMirror::indexOfKey(const QByteArray& objectKey) const
{
    for (int i = 0; i < m_watches.size(); ++i) {
        if (m_watches.at(i).key == objectKey) {
            return i;
        }
    }
    return -1;
}

int StateMirror::indexOfObject(const QObject* object) const
{
    for (int i = 0; i < m_watches.size(); ++i) {
        if (m_watches.at(i).object == object) {
            return i;
        }
    }
    return -1;
}

// ── Watch lifecycle ───────────────────────────────────────────────────────

bool StateMirror::watch(const QByteArray& objectKey, QObject* object)
{
    if (objectKey.isEmpty() || object == nullptr) {
        return false;
    }

    const MirrorSchema& schema = MirrorSchema::forObject(object);
    if (schema.size() == 0) {
        qCWarning(lcStateMirror)
            << "refusing to watch" << objectKey << "of class"
            << object->metaObject()->className()
            << "-- not part of the mirrored surface";
        return false;
    }

    // The binding must be one-to-one in BOTH directions.
    const int existing = indexOfKey(objectKey);
    if (existing >= 0 && m_watches.at(existing).object != object) {
        // Two objects under one wire identity would give the remote GUI an
        // interleaving of both, with no way to tell which it was looking at.
        qCWarning(lcStateMirror)
            << "refusing to rebind key" << objectKey << "to a different object";
        return false;
    }

    const int sameObject = indexOfObject(object);
    if (sameObject >= 0 && m_watches.at(sameObject).key != objectKey) {
        // One object under two keys is worse than useless. Every lookup
        // here is first-match, so the second record would forward nothing
        // while isWatching() reported true, unwatch() of the first key
        // would disconnect the object wholesale and mute the second
        // permanently, and destruction would remove only one record and
        // leave the other holding a freed pointer for snapshot() to read.
        qCWarning(lcStateMirror)
            << "refusing to watch an object already bound to key"
            << m_watches.at(sameObject).key << "under a second key" << objectKey;
        return false;
    }

    const QMetaMethod& slot = watcherSlot();
    if (!slot.isValid()) {
        return false;
    }

    // Connect the declared NOTIFY of every non-CONSTANT property, once per
    // DISTINCT signal. UniqueConnection makes the re-watch case (and the
    // shared-notifier case, already collapsed by the schema) a no-op rather
    // than a source of duplicate frames.
    for (int notifyMethodIndex : schema.notifyMethodIndices()) {
        const QMetaMethod signal = schema.metaObject()->method(notifyMethodIndex);
        if (!signal.isValid()) {
            continue;
        }
        connect(object, signal, this, slot, Qt::UniqueConnection);
    }

    if (existing >= 0) {
        return true;
    }

    // A slice removed from RadioModel is deleted underneath us; without
    // this the next notifier lookup would walk a dangling pointer.
    connect(object, &QObject::destroyed,
            this, &StateMirror::onWatchedObjectDestroyed,
            Qt::UniqueConnection);

    m_watches.append(Watch{ objectKey, object, &schema });
    return true;
}

void StateMirror::detach(const Watch& watch)
{
    if (watch.object != nullptr) {
        // Drops both the notify connections and the destroyed() hook, which
        // are the only connections this class ever makes from a watched
        // object to itself.
        disconnect(watch.object, nullptr, this, nullptr);
    }
}

void StateMirror::unwatch(const QByteArray& objectKey)
{
    const int index = indexOfKey(objectKey);
    if (index < 0) {
        return;
    }
    detach(m_watches.at(index));
    m_watches.removeAt(index);
}

void StateMirror::unwatchAll()
{
    for (const Watch& watch : m_watches) {
        detach(watch);
    }
    m_watches.clear();
}

void StateMirror::onWatchedObjectDestroyed(QObject* object)
{
    // Removes EVERY record naming this pointer, not just the first.
    // watch() refuses to create a second one, so this should only ever
    // remove a single record; it removes all of them anyway, because the
    // cost of being wrong is a record holding a freed pointer that
    // snapshot() would go on to read.
    //
    // No detach() here: the object is already in ~QObject, which has
    // dropped its connections itself.
    m_watches.removeIf([object](const Watch& watch) {
        return watch.object == object;
    });
}

bool StateMirror::isWatching(const QByteArray& objectKey) const
{
    return indexOfKey(objectKey) >= 0;
}

QList<QByteArray> StateMirror::watchedKeys() const
{
    QList<QByteArray> keys;
    keys.reserve(m_watches.size());
    for (const Watch& watch : m_watches) {
        keys.append(watch.key);
    }
    return keys;
}

QObject* StateMirror::watchedObject(const QByteArray& objectKey) const
{
    const int index = indexOfKey(objectKey);
    return index < 0 ? nullptr : m_watches.at(index).object;
}

QByteArray StateMirror::keyFor(const QObject* object) const
{
    const int index = indexOfObject(object);
    return index < 0 ? QByteArray() : m_watches.at(index).key;
}

// ── The forwarder ─────────────────────────────────────────────────────────

void StateMirror::onWatchedPropertyChanged()
{
    QObject* source = sender();
    if (source == nullptr) {
        return;
    }

    // The meta-method index of the signal that got us here, which is
    // directly comparable with QMetaProperty::notifySignal().methodIndex()
    // as recorded in the schema.
    const int notifyMethodIndex = senderSignalIndex();
    if (notifyMethodIndex < 0) {
        return;
    }

    const int index = indexOfObject(source);
    if (index < 0) {
        return;
    }

    // Copied out of m_watches before the emit below: a receiver may watch
    // or unwatch in response, which would move the list out from under a
    // pointer into it.
    const QByteArray key = m_watches.at(index).key;
    const MirrorSchema* schema = m_watches.at(index).schema;
    if (schema == nullptr) {
        return;
    }

    const QList<quint16>& ordinals = schema->ordinalsForNotifySignal(notifyMethodIndex);
    if (ordinals.isEmpty()) {
        return;
    }

    QList<MirrorUpdate> updates;
    updates.reserve(ordinals.size());
    for (quint16 ordinal : ordinals) {
        const MirrorProperty* prop = schema->byOrdinal(ordinal);
        if (prop == nullptr) {
            continue;
        }
        // RE-READ. Never the signal's arguments: an arity-0 notifier has
        // none, and setDspMode moves filterLow/filterHigh behind a
        // separate signal.
        const QVariant value = schema->read(*prop, source);
        if (!value.isValid()) {
            continue;
        }
        updates.append(MirrorUpdate{ prop->ordinal, prop->name, prop->kind, value });
    }

    if (updates.isEmpty()) {
        return;
    }
    emit propertiesChanged(key, updates);
}

// ── Snapshot ──────────────────────────────────────────────────────────────

QList<MirrorUpdate> StateMirror::snapshot(const QByteArray& objectKey) const
{
    const int index = indexOfKey(objectKey);
    if (index < 0) {
        return {};
    }
    const Watch& watch = m_watches.at(index);
    if (watch.object == nullptr || watch.schema == nullptr) {
        return {};
    }

    QList<MirrorUpdate> updates;
    updates.reserve(watch.schema->size());
    // Walks the WHOLE schema, not the notify map. That is the only way a
    // CONSTANT property such as sliceIndex -- the mirror's object identity
    // -- ever reaches the wire.
    for (const MirrorProperty& prop : watch.schema->properties()) {
        const QVariant value = watch.schema->read(prop, watch.object);
        if (!value.isValid()) {
            continue;
        }
        updates.append(MirrorUpdate{ prop.ordinal, prop.name, prop.kind, value });
    }
    return updates;
}

// O(n^2) in the watched-object count: each snapshot() re-resolves its key
// through the linear indexOfKey. Irrelevant at the five-ish slices this
// ever holds, and left simple on purpose, but Task 10 builds the
// connect-time burst on this call, so if the watched set ever grows past
// a handful, pass the index down instead of the key.
QList<QPair<QByteArray, QList<MirrorUpdate>>> StateMirror::snapshotAll() const
{
    QList<QPair<QByteArray, QList<MirrorUpdate>>> all;
    all.reserve(m_watches.size());
    for (const Watch& watch : m_watches) {
        all.append(qMakePair(watch.key, snapshot(watch.key)));
    }
    return all;
}

} // namespace NereusSDR
