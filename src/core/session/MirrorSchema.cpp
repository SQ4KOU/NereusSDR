// =================================================================
// src/core/session/MirrorSchema.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R2 Task 7.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-05  J.J. Boyd / KG4VCF  Remote daemon R2 Task 7: mirrored
//                                    property schema. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
// =================================================================

#include "core/session/MirrorSchema.h"

#include <QLoggingCategory>
#include <QMetaMethod>
#include <QMetaObject>
#include <QMetaProperty>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QString>

#include <memory>

Q_LOGGING_CATEGORY(lcMirrorSchema, "nereus.mirror.schema")

namespace NereusSDR {

namespace {

// The mirrored surface, by class name. An allowlist rather than a
// denylist: a model class added later is not mirrored until someone puts
// it here and regenerates MirrorPolicy's table for it.
//
// MeterModel's absence is the one that needed arguing. Its four setters
// have zero callers anywhere in src or tests, and the connect meant to
// write it is an empty lambda with four Q_UNUSED, so mirroring it would
// ship four construction defaults forever and nothing else.
const char* const kMirroredClasses[] = {
    "NereusSDR::SliceModel",
    "NereusSDR::TransmitModel",
    "NereusSDR::TunerModel",
    "NereusSDR::RadioModel",
    "NereusSDR::PanadapterModel",
};

// Per-property exclusions, as (class, property).
//
// sliceLetter is CONSTANT, derived entirely from sliceIndex (which IS
// mirrored), and the only QChar in the whole surface. Dropping it here is
// what keeps QChar off the wire without the codec needing a kind for it.
struct ExcludedProperty {
    const char* className;
    const char* property;
};
constexpr ExcludedProperty kExcludedProperties[] = {
    { "SliceModel", "sliceLetter" },
};

bool isExcludedProperty(const QByteArray& shortName, const char* property)
{
    for (const ExcludedProperty& e : kExcludedProperties) {
        if (shortName == e.className && qstrcmp(property, e.property) == 0) {
            return true;
        }
    }
    return false;
}

// Rebuild an integer of the property's own underlying width so
// QVariant(QMetaType, const void*) can copy-construct the enum. There is no
// other way in: these enums are not registered with Q_ENUM_NS, so QVariant
// has no name-based or converter-based route back from an integer.
QVariant enumVariantFromInteger(QMetaType metaType, qlonglong value)
{
    switch (metaType.sizeOf()) {
    case 1: { const auto v = static_cast<qint8>(value);  return QVariant(metaType, &v); }
    case 2: { const auto v = static_cast<qint16>(value); return QVariant(metaType, &v); }
    case 4: { const auto v = static_cast<qint32>(value); return QVariant(metaType, &v); }
    case 8: { const auto v = static_cast<qint64>(value); return QVariant(metaType, &v); }
    default:
        return QVariant();
    }
}

QMutex& cacheMutex()
{
    static QMutex mutex;
    return mutex;
}

QHash<const QMetaObject*, std::shared_ptr<MirrorSchema>>& cache()
{
    static QHash<const QMetaObject*, std::shared_ptr<MirrorSchema>> map;
    return map;
}

} // namespace

// ── Construction ──────────────────────────────────────────────────────────

MirrorSchema::MirrorSchema(const QMetaObject* mo)
    : m_metaObject(mo)
{
    if (mo == nullptr) {
        return;
    }
    m_className = QByteArray(mo->className());

    if (!isMirrorable(mo)) {
        return;
    }

    const QByteArray shortName = shortClassName(m_className);

    // propertyOffset() rather than 0: only this class's OWN declarations
    // belong on the wire. Starting at 0 would drag QObject::objectName into
    // every mirrored object.
    for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
        const QMetaProperty metaProp = mo->property(i);
        if (!metaProp.isReadable()) {
            continue;
        }
        if (isExcludedProperty(shortName, metaProp.name())) {
            continue;
        }

        MirrorProperty prop;
        prop.ordinal = static_cast<quint16>(m_properties.size());
        prop.name = QByteArray(metaProp.name());
        prop.metaIndex = i;
        prop.metaType = metaProp.metaType();
        prop.kind = kindFor(prop.metaType);
        prop.isWritable = metaProp.isWritable();
        prop.isConstant = !metaProp.hasNotifySignal();
        prop.notifyMethodIndex =
            metaProp.hasNotifySignal() ? metaProp.notifySignal().methodIndex() : -1;

        if (prop.kind == MirrorWireKind::Unsupported) {
            // Kept in the schema on purpose. tst_mirror_schema's membership
            // guard names it; dropping it here would hide it instead.
            qCWarning(lcMirrorSchema)
                << m_className << "::" << prop.name
                << "has no wire representation for type"
                << prop.metaType.name()
                << "-- it will not be mirrored";
        }

        if (prop.isConstant) {
            m_constantOrdinals.append(prop.ordinal);
        } else {
            QList<quint16>& sharing = m_ordinalsByNotify[prop.notifyMethodIndex];
            if (sharing.isEmpty()) {
                m_notifyMethodIndices.append(prop.notifyMethodIndex);
            }
            sharing.append(prop.ordinal);
        }

        m_ordinalByName.insert(prop.name, static_cast<int>(m_properties.size()));
        m_properties.append(prop);
    }
}

QByteArray MirrorSchema::shortClassName(const QByteArray& className)
{
    const int sep = className.lastIndexOf("::");
    return sep < 0 ? className : className.mid(sep + 2);
}

bool MirrorSchema::isMirrorable(const QMetaObject* mo)
{
    if (mo == nullptr) {
        return false;
    }
    const QByteArray shortName = shortClassName(QByteArray(mo->className()));
    for (const char* candidate : kMirroredClasses) {
        if (shortName == shortClassName(QByteArray(candidate))) {
            return true;
        }
    }
    return false;
}

const MirrorSchema& MirrorSchema::forMetaObject(const QMetaObject* mo)
{
    QMutexLocker locker(&cacheMutex());
    auto it = cache().constFind(mo);
    if (it != cache().constEnd()) {
        return *it.value();
    }
    // Private constructor, so make_shared is not available here.
    std::shared_ptr<MirrorSchema> schema(new MirrorSchema(mo));
    cache().insert(mo, schema);
    return *schema;
}

const MirrorSchema& MirrorSchema::forObject(const QObject* obj)
{
    return forMetaObject(obj != nullptr ? obj->metaObject() : nullptr);
}

// ── Lookup ────────────────────────────────────────────────────────────────

const MirrorProperty* MirrorSchema::byOrdinal(quint16 ordinal) const
{
    const int index = static_cast<int>(ordinal);
    if (index < 0 || index >= m_properties.size()) {
        return nullptr;
    }
    return &m_properties.at(index);
}

const MirrorProperty* MirrorSchema::byName(const QByteArray& name) const
{
    const auto it = m_ordinalByName.constFind(name);
    if (it == m_ordinalByName.constEnd()) {
        return nullptr;
    }
    return &m_properties.at(it.value());
}

const QList<quint16>& MirrorSchema::ordinalsForNotifySignal(int notifyMethodIndex) const
{
    static const QList<quint16> kEmpty;
    const auto it = m_ordinalsByNotify.constFind(notifyMethodIndex);
    return it == m_ordinalsByNotify.constEnd() ? kEmpty : it.value();
}

// ── Codec ─────────────────────────────────────────────────────────────────

MirrorWireKind MirrorSchema::kindFor(QMetaType metaType)
{
    // IsEnumeration, never QMetaProperty::isEnumType(). NereusSDR's enums
    // are declared in plain namespaces with no Q_NAMESPACE / Q_ENUM_NS, so
    // isEnumType() is false for all of them while this flag is true.
    // Measured against this tree's Qt; tst_mirror_schema pins it.
    if (metaType.flags().testFlag(QMetaType::IsEnumeration)) {
        return MirrorWireKind::Enum;
    }

    switch (metaType.id()) {
    case QMetaType::Bool:
        return MirrorWireKind::Bool;

    case QMetaType::Float:
    case QMetaType::Double:
        // float widens rather than earning its own kind.
        return MirrorWireKind::Float64;

    case QMetaType::QString:
        return MirrorWireKind::Utf8;

    case QMetaType::Char:
    case QMetaType::SChar:
    case QMetaType::UChar:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::Long:
    case QMetaType::ULong:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        return MirrorWireKind::Int64;

    default:
        return MirrorWireKind::Unsupported;
    }
}

QVariant MirrorSchema::encode(const MirrorProperty& prop, const QVariant& nativeValue)
{
    if (!nativeValue.isValid()) {
        return QVariant();
    }
    switch (prop.kind) {
    case MirrorWireKind::Bool:
        return QVariant(nativeValue.toBool());
    case MirrorWireKind::Int64:
    case MirrorWireKind::Enum:
        return QVariant(nativeValue.toLongLong());
    case MirrorWireKind::Float64:
        return QVariant(nativeValue.toDouble());
    case MirrorWireKind::Utf8:
        return QVariant(nativeValue.toString());
    case MirrorWireKind::Unsupported:
        break;
    }
    return QVariant();
}

QVariant MirrorSchema::decode(const MirrorProperty& prop, const QVariant& wireValue)
{
    if (!wireValue.isValid()) {
        return QVariant();
    }
    switch (prop.kind) {
    case MirrorWireKind::Bool:
        return QVariant(wireValue.toBool());
    case MirrorWireKind::Float64:
        return QVariant(wireValue.toDouble());
    case MirrorWireKind::Utf8:
        return QVariant(wireValue.toString());
    case MirrorWireKind::Int64: {
        // Narrow back to the property's own declared type so
        // QMetaProperty::write does not have to convert, and so an int
        // property never silently accepts a value it cannot hold.
        const QVariant narrowed = QVariant(wireValue.toLongLong());
        QVariant typed(narrowed);
        if (!typed.convert(prop.metaType)) {
            return QVariant();
        }
        return typed;
    }
    case MirrorWireKind::Enum:
        return enumVariantFromInteger(prop.metaType, wireValue.toLongLong());
    case MirrorWireKind::Unsupported:
        break;
    }
    return QVariant();
}

// ── Live-object access ────────────────────────────────────────────────────

QVariant MirrorSchema::read(const MirrorProperty& prop, const QObject* obj) const
{
    if (obj == nullptr || m_metaObject == nullptr) {
        return QVariant();
    }
    if (!obj->metaObject()->inherits(m_metaObject)) {
        return QVariant();
    }
    if (prop.metaIndex < 0 || prop.metaIndex >= m_metaObject->propertyCount()) {
        return QVariant();
    }
    return encode(prop, m_metaObject->property(prop.metaIndex).read(obj));
}

bool MirrorSchema::write(const MirrorProperty& prop, QObject* obj,
                         const QVariant& wireValue) const
{
    if (obj == nullptr || m_metaObject == nullptr || !prop.isWritable) {
        return false;
    }
    if (!obj->metaObject()->inherits(m_metaObject)) {
        return false;
    }
    if (prop.metaIndex < 0 || prop.metaIndex >= m_metaObject->propertyCount()) {
        return false;
    }
    const QVariant native = decode(prop, wireValue);
    if (!native.isValid()) {
        return false;
    }
    return m_metaObject->property(prop.metaIndex).write(obj, native);
}

} // namespace NereusSDR
