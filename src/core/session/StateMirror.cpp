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
//   2026-08-05  J.J. Boyd / KG4VCF  Remote daemon R2 Task 8: inbound apply
//                                    (applyInbound, the per-model
//                                    applyMirroredValue hook dispatch, and
//                                    the m_applying forwarding guard). AI-
//                                    assisted transformation via Anthropic
//                                    Claude Code.
// =================================================================

#include "core/session/StateMirror.h"

#include "core/session/MirrorPolicy.h"

#include <QLoggingCategory>
#include <QMetaMethod>
#include <QMetaObject>

Q_LOGGING_CATEGORY(lcStateMirror, "nereus.mirror")

namespace NereusSDR {

namespace {

// The R2 plan names four client command verbs Task 11 will dispatch
// (requestSliceSampleRate, addSlice, removeSlice, addSliceOnPan). Only
// sampleRateHz maps onto one of them today -- the other three read as
// slice LIFECYCLE commands (create/destroy/place), not as replacements
// for writing an EXISTING slice's placement fields directly. The other
// six writable-but-Outbound SliceModel properties (chainIndex, ddcIndex,
// streamIndex, shiftOffsetHz, widebandExtensionRequested, psPaused --
// MirrorPolicy.cpp) are all codec-/coordinator-owned placement state with
// no assigned verb yet, so their rejection says that honestly rather than
// guessing at a name Task 11 has not chosen.
struct VerbHint {
    const char* className;
    const char* property;
    const char* verb;
};
constexpr VerbHint kVerbHints[] = {
    { "SliceModel", "sampleRateHz", "requestSliceSampleRate" },
};

QString writableButOutboundReason(const QByteArray& shortClassName,
                                  const QByteArray& property)
{
    for (const VerbHint& hint : kVerbHints) {
        if (shortClassName == hint.className && property == hint.property) {
            return QStringLiteral("daemon-authoritative; use %1 instead")
                .arg(QString::fromLatin1(hint.verb));
        }
    }
    return QStringLiteral(
        "daemon-authoritative; not directly writable (no client command "
        "assigned yet)");
}

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
    // An inbound apply is in flight on THIS mirror: applyInbound() is still
    // on the call stack, either writing the property this notify is FOR
    // directly, or -- same-thread, synchronous, still nested inside that
    // same write -- a side effect it triggered on a DIFFERENT watched
    // object. RadioModel::addSlice()'s co-hosted-slice NB-mode/NB-tuning
    // peer mirrors are exactly the second case: peer->setNbMode() fires
    // nbModeChanged on the PEER object, not the one applyInbound() was
    // asked to write. Checked before even asking which object fired, so
    // both cases are covered by one flag regardless of source.
    if (m_applying) {
        return;
    }

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

// ── Inbound apply ────────────────────────────────────────────────────────

MirrorApplyResult StateMirror::applyInbound(const QByteArray& objectKey,
                                            const QByteArray& propertyName,
                                            const QVariant& wireValue)
{
    MirrorApplyResult result;
    result.objectKey = objectKey;
    result.property = propertyName;

    const int index = indexOfKey(objectKey);
    if (index < 0 || m_watches.at(index).object == nullptr
        || m_watches.at(index).schema == nullptr) {
        result.reason = QStringLiteral("no such watched object");
        return result;
    }
    const Watch& watch = m_watches.at(index);

    const MirrorProperty* prop = watch.schema->byName(propertyName);
    if (prop == nullptr) {
        result.reason = QStringLiteral("no such mirrored property");
        return result;
    }
    return applyInboundToProperty(watch, *prop, wireValue);
}

MirrorApplyResult StateMirror::applyInbound(const QByteArray& objectKey,
                                            quint16 ordinal,
                                            const QVariant& wireValue)
{
    MirrorApplyResult result;
    result.objectKey = objectKey;

    const int index = indexOfKey(objectKey);
    if (index < 0 || m_watches.at(index).object == nullptr
        || m_watches.at(index).schema == nullptr) {
        result.reason = QStringLiteral("no such watched object");
        return result;
    }
    const Watch& watch = m_watches.at(index);

    const MirrorProperty* prop = watch.schema->byOrdinal(ordinal);
    if (prop == nullptr) {
        result.reason = QStringLiteral("no such mirrored property");
        return result;
    }
    return applyInboundToProperty(watch, *prop, wireValue);
}

MirrorApplyResult StateMirror::applyInboundToProperty(const Watch& watch,
                                                       const MirrorProperty& prop,
                                                       const QVariant& wireValue)
{
    MirrorApplyResult result;
    result.objectKey = watch.key;
    result.property = prop.name;

    // CONSTANT properties (sliceIndex, the mirror's object identity) have
    // no NOTIFY, travel only in the connect-time snapshot, and must never
    // be treated as a live write -- including through the hook below.
    if (prop.isConstant) {
        result.reason = QStringLiteral(
            "constant; travels only in the connect-time snapshot, never inbound");
        return result;
    }

    const QByteArray className = watch.schema->className();

    if (prop.isWritable) {
        // The standard path: a real Q_PROPERTY WRITE, gated by MirrorPolicy.
        // Most of the mirrored surface (115 of 145 properties) is
        // Bidirectional and passes; the seven SliceModel properties that
        // carry WRITE but are daemon-authoritative (MirrorPolicy.cpp) are
        // refused here, naming the client command to use instead where the
        // R2 plan has assigned one.
        if (!MirrorPolicy::inboundAllowed(className, prop.name)) {
            result.reason = writableButOutboundReason(
                MirrorSchema::shortClassName(className), prop.name);
            return result;
        }
        m_applying = true;
        const bool ok = watch.schema->write(prop, watch.object, wireValue);
        m_applying = false;
        if (!ok) {
            result.reason = QStringLiteral(
                "value could not be decoded for this property's type");
            return result;
        }
        result.accepted = true;
        return result;
    }

    // No WRITE at all. MirrorPolicy always classifies this Outbound --
    // there is nothing standard for it to write through -- but that is not
    // the final word: the target model gets one more chance to translate
    // the intent through its own applyMirroredValue(name, value), called by
    // name via QMetaObject::invokeMethod so this file never needs to
    // #include a model header. Decoded through the same codec a normal
    // WRITE would use, so the hook receives a properly narrowed, natively
    // typed value rather than a raw wire one.
    const QVariant native = MirrorSchema::decode(prop, wireValue);
    if (!native.isValid()) {
        result.reason = QStringLiteral(
            "value could not be decoded for this property's type");
        return result;
    }

    QString hookReason;
    m_applying = true;
    const bool invoked = QMetaObject::invokeMethod(
        watch.object, "applyMirroredValue", Qt::DirectConnection,
        Q_RETURN_ARG(QString, hookReason),
        Q_ARG(QByteArray, prop.name),
        Q_ARG(QVariant, native));
    m_applying = false;

    if (!invoked) {
        result.reason = QStringLiteral(
            "this model has no inbound hook for a property with no WRITE");
        return result;
    }
    if (!hookReason.isEmpty()) {
        result.reason = hookReason;
        return result;
    }
    result.accepted = true;
    return result;
}

} // namespace NereusSDR
