// =================================================================
// tests/tst_mirror_schema.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test infrastructure.
//
// Remote Daemon R2 Task 7: MirrorSchema walks Qt's meta-object system
// over the five mirrored models and assigns each Q_PROPERTY a dense,
// declaration-ordered, session-scoped ordinal for the wire; MirrorPolicy
// is the default-deny direction table that says which of those a remote
// GUI may write back.
//
// Three things this file pins that nothing else can:
//
//   1. The arity spike. StateMirror connects EVERY property's NOTIFY to
//      one zero-argument slot via connect(sender, QMetaMethod, receiver,
//      QMetaMethod).  Only arity 1 was ever verified in this project;
//      MainWindow::wireSliceStatusOverlayTriggers (the existing
//      precedent) connects only the six properties in
//      PanadapterApplet::statusOverlaySliceProperties(), none of which
//      has a two-argument notifier.  SliceModel::filterChanged(int,int)
//      does, and it drives the whole filter surface, so arity 2 is
//      pinned here against the real models.
//
//   2. The enum codec rule. These models' enums live in plain namespaces
//      with no Q_NAMESPACE / Q_ENUM_NS (see src/core/WdspTypes.h and
//      src/models/Band.h), so QMetaProperty::isEnumType() is FALSE for
//      them even though they are enums. Classifying by isEnumType()
//      would silently mis-encode dspMode, agcMode, band and nine others.
//      QMetaType::IsEnumeration is the flag that actually holds.
//
//   3. The golden-list guard, asserted as MEMBERSHIP and never as a
//      count. Counts have already drifted twice inside this plan's own
//      execution window. A count assertion turns a routine feature
//      commit into a mysterious failure; a membership assertion names
//      the property that needs a MirrorPolicy entry.
// =================================================================

#include <QtTest/QtTest>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QMetaType>
#include <QVariant>

#include "core/session/MirrorPolicy.h"
#include "core/session/MirrorSchema.h"
#include "models/MeterModel.h"
#include "models/PanadapterModel.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"
#include "models/TunerModel.h"

using namespace NereusSDR;

// Zero-argument receiver for the arity spike. Deliberately shaped exactly
// like StateMirror's own watcher slot: no parameters at all, so one slot
// can absorb notifiers of every arity.
class ArityWatcher : public QObject {
    Q_OBJECT
public:
    int hits = 0;
    int lastSignalIndex = -1;

public slots:
    void onNotified()
    {
        ++hits;
        lastSignalIndex = senderSignalIndex();
    }
};

class TestMirrorSchema : public QObject {
    Q_OBJECT

private:
    // Resolve a property by name off a live object's metaobject.
    static QMetaProperty prop(const QObject* obj, const char* name)
    {
        const QMetaObject* mo = obj->metaObject();
        const int idx = mo->indexOfProperty(name);
        Q_ASSERT(idx >= 0);
        return mo->property(idx);
    }

private slots:

    // ── Step 1: the go/no-go arity spike, against the real models ──────────
    //
    // If this fails at arity 2, the entire filter surface needs a different
    // mechanism and the R2 plan changes. It is pinned here rather than left
    // as a one-off scratch probe precisely so a Qt upgrade cannot quietly
    // withdraw the guarantee.
    void metaMethodConnectAcceptsEveryNotifyArity()
    {
        TunerModel tuner;
        SliceModel slice(0);
        ArityWatcher watcher;

        const QMetaObject* wMo = watcher.metaObject();
        const int slotIdx = wMo->indexOfSlot("onNotified()");
        QVERIFY2(slotIdx >= 0, "zero-argument receiver slot must resolve");
        const QMetaMethod slot = wMo->method(slotIdx);
        QCOMPARE(slot.parameterCount(), 0);

        // Arity 0: TunerModel::stateChanged()
        const QMetaMethod sig0 = prop(&tuner, "isOperate").notifySignal();
        QCOMPARE(sig0.name(), QByteArray("stateChanged"));
        QCOMPARE(sig0.parameterCount(), 0);

        // Arity 1: SliceModel::frequencyChanged(double)
        const QMetaMethod sig1 = prop(&slice, "frequency").notifySignal();
        QCOMPARE(sig1.name(), QByteArray("frequencyChanged"));
        QCOMPARE(sig1.parameterCount(), 1);

        // Arity 2: SliceModel::filterChanged(int, int)
        const QMetaMethod sig2 = prop(&slice, "filterLow").notifySignal();
        QCOMPARE(sig2.name(), QByteArray("filterChanged"));
        QCOMPARE(sig2.parameterCount(), 2);

        QVERIFY2(connect(&tuner, sig0, &watcher, slot, Qt::UniqueConnection),
                 "ARITY 0: connect(QMetaMethod -> zero-arg slot) must succeed");
        QVERIFY2(connect(&slice, sig1, &watcher, slot, Qt::UniqueConnection),
                 "ARITY 1: connect(QMetaMethod -> zero-arg slot) must succeed");
        QVERIFY2(connect(&slice, sig2, &watcher, slot, Qt::UniqueConnection),
                 "ARITY 2: connect(QMetaMethod -> zero-arg slot) must succeed");

        // Shared notifiers are what make UniqueConnection load-bearing:
        // filterLow and filterHigh both ask for filterChanged, and the
        // second ask must be refused rather than doubling every emission.
        QCOMPARE(prop(&slice, "filterHigh").notifySignal().methodIndex(),
                 sig2.methodIndex());
        QVERIFY2(!connect(&slice, sig2, &watcher, slot, Qt::UniqueConnection),
                 "UniqueConnection must refuse a duplicate arity-2 connect");

        // ...and they must actually fire, with senderSignalIndex() usable
        // from inside a zero-argument slot to identify which one did.
        watcher.hits = 0;
        slice.setFrequency(14200000.0);
        QCOMPARE(watcher.hits, 1);
        QCOMPARE(watcher.lastSignalIndex, sig1.methodIndex());

        watcher.hits = 0;
        slice.setFilterLow(-2700);
        QCOMPARE(watcher.hits, 1);
        QCOMPARE(watcher.lastSignalIndex, sig2.methodIndex());
    }

    // ── Step 2: enum codec regression guard ───────────────────────────────
    //
    // Measured, not assumed. If a future Qt or a future Q_ENUM_NS on
    // NereusSDR's enum namespaces flips either of these, the schema's
    // classification rule has to be revisited rather than silently
    // producing the other kind.
    void enumPropertiesReportIsEnumerationButNotIsEnumType()
    {
        SliceModel slice(0);
        for (const char* name : { "dspMode", "agcMode", "band" }) {
            const QMetaProperty p = prop(&slice, name);
            QVERIFY2(p.metaType().flags().testFlag(QMetaType::IsEnumeration),
                     qPrintable(QStringLiteral("%1: QMetaType::IsEnumeration "
                                               "must be set").arg(name)));
            QVERIFY2(!p.isEnumType(),
                     qPrintable(QStringLiteral("%1: isEnumType() is false for "
                                               "these unregistered enums; "
                                               "classifying by it would "
                                               "mis-encode").arg(name)));
        }
    }

    // The write half of the enum codec: an integer off the wire has to get
    // back into a property whose declared type is a scoped enum.
    void enumDecodeWritesBackThroughQMetaProperty()
    {
        SliceModel slice(0);
        const MirrorSchema& schema = MirrorSchema::forObject(&slice);

        const MirrorProperty* dsp = schema.byName("dspMode");
        QVERIFY(dsp != nullptr);
        QCOMPARE(dsp->kind, MirrorWireKind::Enum);

        const qlonglong wire = static_cast<qlonglong>(DSPMode::CWU);
        QVERIFY2(schema.write(*dsp, &slice, QVariant(wire)),
                 "enum decode + QMetaProperty::write must succeed");
        QCOMPARE(slice.dspMode(), DSPMode::CWU);

        // ...and round-trips back out as the underlying integer.
        QCOMPARE(schema.read(*dsp, &slice).toLongLong(), wire);
    }

    // ── Step 3/4: the schema walk ─────────────────────────────────────────

    void ordinalsAreDenseAndDeclarationOrdered()
    {
        SliceModel slice(0);
        const MirrorSchema& schema = MirrorSchema::forObject(&slice);
        QVERIFY(schema.size() > 0);

        int lastMetaIndex = -1;
        for (int i = 0; i < schema.size(); ++i) {
            const MirrorProperty& p = schema.properties().at(i);
            QCOMPARE(static_cast<int>(p.ordinal), i);
            // Declaration order == increasing metaobject property index.
            QVERIFY2(p.metaIndex > lastMetaIndex,
                     "ordinals must follow declaration order");
            lastMetaIndex = p.metaIndex;
            QCOMPARE(schema.byOrdinal(p.ordinal), &p);
            QCOMPARE(schema.byName(p.name), &p);
        }
    }

    // Five slices must pay for one walk, so the schema is cached per class
    // and two instances must hand back the very same object.
    void schemaIsCachedOncePerClass()
    {
        SliceModel a(0);
        SliceModel b(1);
        QCOMPARE(&MirrorSchema::forObject(&a), &MirrorSchema::forObject(&b));

        PanadapterModel pan;
        QVERIFY(&MirrorSchema::forObject(&pan) != &MirrorSchema::forObject(&a));
    }

    // The schema walks propertyOffset()..propertyCount(), so QObject's own
    // objectName must never reach the wire.
    void inheritedQObjectPropertiesAreNotMirrored()
    {
        SliceModel slice(0);
        QVERIFY(MirrorSchema::forObject(&slice).byName("objectName") == nullptr);
    }

    // ── Membership guard: every enum-typed property is kind Enum ──────────
    //
    // Derived from the metaobject, never from a hardcoded list, so a new
    // enum property is covered the moment it is declared.
    void everyEnumTypedPropertyIsClassifiedEnum()
    {
        QStringList offenders;
        for (const QMetaObject* mo : mirroredMetaObjects()) {
            const MirrorSchema& schema = MirrorSchema::forMetaObject(mo);
            for (const MirrorProperty& p : schema.properties()) {
                const bool isEnum =
                    p.metaType.flags().testFlag(QMetaType::IsEnumeration);
                if (isEnum && p.kind != MirrorWireKind::Enum) {
                    offenders << QStringLiteral("%1::%2 is an enum but kind is "
                                                "not Enum")
                                     .arg(QString::fromLatin1(mo->className()),
                                          QString::fromUtf8(p.name));
                }
                if (!isEnum && p.kind == MirrorWireKind::Enum) {
                    offenders << QStringLiteral("%1::%2 is kind Enum but is not "
                                                "an enum type")
                                     .arg(QString::fromLatin1(mo->className()),
                                          QString::fromUtf8(p.name));
                }
            }
        }
        QVERIFY2(offenders.isEmpty(), qPrintable(offenders.join(QLatin1String("\n"))));

        // At least the twelve known ones must be present, so a walk that
        // silently produced nothing cannot pass the loop above vacuously.
        SliceModel slice(0);
        const MirrorSchema& schema = MirrorSchema::forObject(&slice);
        for (const char* name : { "dspMode", "agcMode", "band", "nbMode",
                                  "activeNr", "nr1Position", "nr2GainMethod",
                                  "nr2NpeMethod", "nr2Position", "nr3Position",
                                  "nr4Algo", "fmTxMode" }) {
            const MirrorProperty* p = schema.byName(name);
            QVERIFY2(p != nullptr, name);
            QVERIFY2(p->kind == MirrorWireKind::Enum, name);
        }
    }

    // Wire kinds for the non-enum surface. float must widen to f64 rather
    // than getting its own wire kind.
    void wireKindsCoverTheNonEnumSurface()
    {
        SliceModel slice(0);
        const MirrorSchema& s = MirrorSchema::forObject(&slice);
        QCOMPARE(s.byName("frequency")->kind, MirrorWireKind::Float64);
        QCOMPARE(s.byName("filterLow")->kind, MirrorWireKind::Int64);
        QCOMPARE(s.byName("locked")->kind, MirrorWireKind::Bool);
        QCOMPARE(s.byName("panKey")->kind, MirrorWireKind::Utf8);

        // float widens: TransmitModel::micGain and TunerModel::fwdPower/swr
        // are the only float-declared properties in the surface.
        TransmitModel tx;
        QCOMPARE(MirrorSchema::forObject(&tx).byName("micGain")->kind,
                 MirrorWireKind::Float64);
        TunerModel tuner;
        QCOMPARE(MirrorSchema::forObject(&tuner).byName("swr")->kind,
                 MirrorWireKind::Float64);
    }

    // No property may reach the wire with a kind the codec cannot carry.
    // sliceLetter is the only QChar in the surface and is excluded outright;
    // this catches any future addition of an uncarryable type.
    void everyMirroredPropertyHasACarryableKind()
    {
        QStringList offenders;
        for (const QMetaObject* mo : mirroredMetaObjects()) {
            for (const MirrorProperty& p :
                 MirrorSchema::forMetaObject(mo).properties()) {
                if (p.metaType.id() == QMetaType::QChar) {
                    offenders << QStringLiteral("%1::%2 is QChar; the wire has "
                                                "no QChar kind")
                                     .arg(QString::fromLatin1(mo->className()),
                                          QString::fromUtf8(p.name));
                }
            }
        }
        QVERIFY2(offenders.isEmpty(), qPrintable(offenders.join(QLatin1String("\n"))));
    }

    // ── Exclusions ────────────────────────────────────────────────────────

    void sliceLetterIsExcludedFromTheSurface()
    {
        SliceModel slice(0);
        // It really is declared, CONSTANT and QChar...
        const QMetaProperty declared = prop(&slice, "sliceLetter");
        QVERIFY(declared.isValid());
        QVERIFY(!declared.hasNotifySignal());
        QCOMPARE(declared.metaType().id(), int(QMetaType::QChar));
        // ...and the mirror does not carry it.
        QVERIFY(MirrorSchema::forObject(&slice).byName("sliceLetter") == nullptr);
    }

    void meterModelIsNotAMirroredClass()
    {
        MeterModel meter;
        QVERIFY2(!MirrorSchema::isMirrorable(meter.metaObject()),
                 "MeterModel's setters have zero callers; mirroring it would "
                 "ship four construction defaults forever");
        QCOMPARE(MirrorSchema::forObject(&meter).size(), 0);

        // The five that ARE mirrored must all say so.
        for (const QMetaObject* mo : mirroredMetaObjects()) {
            QVERIFY2(MirrorSchema::isMirrorable(mo), mo->className());
        }
    }

    // ── CONSTANT properties ───────────────────────────────────────────────
    //
    // sliceIndex is the mirror's object identity. It carries no NOTIFY, so a
    // watcher that only enumerates notifiers skips it silently and every
    // object.create on the wire comes out anonymous.
    void constantPropertiesAreReachableOnlyThroughTheSnapshot()
    {
        SliceModel slice(3);
        const MirrorSchema& schema = MirrorSchema::forObject(&slice);

        const MirrorProperty* p = schema.byName("sliceIndex");
        QVERIFY2(p != nullptr, "sliceIndex must be in the schema");
        QVERIFY(p->isConstant);
        QCOMPARE(p->notifyMethodIndex, -1);
        QVERIFY(schema.constantOrdinals().contains(p->ordinal));

        // Not reachable through any notifier, by construction.
        for (int sigIdx : schema.notifyMethodIndices()) {
            QVERIFY(!schema.ordinalsForNotifySignal(sigIdx).contains(p->ordinal));
        }

        QCOMPARE(schema.read(*p, &slice).toLongLong(), 3LL);
    }

    // ── Shared notifiers ──────────────────────────────────────────────────

    void sharedNotifiersMapToEveryPropertyTheyName()
    {
        SliceModel slice(0);
        const MirrorSchema& schema = MirrorSchema::forObject(&slice);

        const MirrorProperty* low = schema.byName("filterLow");
        const MirrorProperty* high = schema.byName("filterHigh");
        QVERIFY(low && high);
        QCOMPARE(low->notifyMethodIndex, high->notifyMethodIndex);

        const QList<quint16> ords =
            schema.ordinalsForNotifySignal(low->notifyMethodIndex);
        QCOMPARE(ords.size(), 2);
        QVERIFY(ords.contains(low->ordinal));
        QVERIFY(ords.contains(high->ordinal));

        // Every distinct notifier appears exactly once in the connect list,
        // which is what keeps UniqueConnection from doing real work.
        const QList<int> distinct = schema.notifyMethodIndices();
        QSet<int> seen;
        for (int i : distinct) {
            QVERIFY2(!seen.contains(i), "notifyMethodIndices() must be distinct");
            seen.insert(i);
        }
        QVERIFY(seen.contains(low->notifyMethodIndex));
    }

    // ── Step 5: MirrorPolicy, the default-deny direction table ────────────

    void unlistedPropertiesDefaultToOutbound()
    {
        QCOMPARE(MirrorPolicy::directionFor("SliceModel", "noSuchPropertyEver"),
                 MirrorDirection::Outbound);
        QCOMPARE(MirrorPolicy::directionFor("NoSuchModel", "frequency"),
                 MirrorDirection::Outbound);
        QVERIFY(!MirrorPolicy::inboundAllowed("SliceModel", "noSuchPropertyEver"));
        QVERIFY(!MirrorPolicy::hasExplicitEntry("SliceModel", "noSuchPropertyEver"));
    }

    // QMetaObject::className() reports "NereusSDR::SliceModel"; the policy
    // table and hand-written call sites use the bare name. Both must land on
    // the same entry, or the golden-list guard passes vacuously while the
    // real lookups all fall through to the Outbound default.
    void qualifiedAndBareClassNamesResolveToTheSameEntry()
    {
        QCOMPARE(MirrorSchema::shortClassName("NereusSDR::SliceModel"),
                 QByteArray("SliceModel"));
        QCOMPARE(MirrorSchema::shortClassName("SliceModel"),
                 QByteArray("SliceModel"));

        SliceModel slice(0);
        const QByteArray qualified(slice.metaObject()->className());
        QVERIFY(qualified.contains("::"));
        QCOMPARE(MirrorPolicy::directionFor(qualified, "panKey"),
                 MirrorPolicy::directionFor("SliceModel", "panKey"));
        QVERIFY(MirrorPolicy::hasExplicitEntry(qualified, "frequency"));
    }

    // MEMBERSHIP, not counts. When a task lands a Q_PROPERTY without adding a
    // policy entry, this names it.
    void everyMirroredPropertyHasAnExplicitPolicyEntry()
    {
        QStringList missing;
        for (const QMetaObject* mo : mirroredMetaObjects()) {
            const QByteArray cls(mo->className());
            for (const MirrorProperty& p :
                 MirrorSchema::forMetaObject(mo).properties()) {
                if (!MirrorPolicy::hasExplicitEntry(cls, p.name)) {
                    missing << QStringLiteral("%1::%2 has no MirrorPolicy "
                                              "entry; add one to "
                                              "MirrorPolicy.cpp")
                                   .arg(QString::fromLatin1(cls),
                                        QString::fromUtf8(p.name));
                }
            }
        }
        QVERIFY2(missing.isEmpty(), qPrintable(missing.join(QLatin1String("\n"))));
    }

    // A property with no WRITE physically cannot be applied inbound, so the
    // policy must never call one Bidirectional.
    void everyReadOnlyPropertyIsDeniedInbound()
    {
        QStringList offenders;
        for (const QMetaObject* mo : mirroredMetaObjects()) {
            const QByteArray cls(mo->className());
            for (const MirrorProperty& p :
                 MirrorSchema::forMetaObject(mo).properties()) {
                if (p.isWritable) { continue; }
                if (MirrorPolicy::inboundAllowed(cls, p.name)) {
                    offenders << QStringLiteral("%1::%2 has no WRITE but the "
                                                "policy allows inbound")
                                     .arg(QString::fromLatin1(cls),
                                          QString::fromUtf8(p.name));
                }
            }
        }
        QVERIFY2(offenders.isEmpty(), qPrintable(offenders.join(QLatin1String("\n"))));
    }

    // The reverse guard: an entry naming a property that no longer exists is
    // dead weight that silently stops protecting anything.
    void everyPolicyEntryNamesALivePropertyOfAMirroredClass()
    {
        QStringList stale;
        for (const MirrorPolicy::Entry& e : MirrorPolicy::entries()) {
            bool found = false;
            for (const QMetaObject* mo : mirroredMetaObjects()) {
                if (MirrorSchema::shortClassName(QByteArray(mo->className()))
                    != MirrorSchema::shortClassName(QByteArray(e.className))) {
                    continue;
                }
                found = MirrorSchema::forMetaObject(mo).byName(e.property) != nullptr;
                break;
            }
            if (!found) {
                stale << QStringLiteral("MirrorPolicy entry %1::%2 names no "
                                        "mirrored property")
                             .arg(QString::fromLatin1(e.className),
                                  QString::fromLatin1(e.property));
            }
        }
        QVERIFY2(stale.isEmpty(), qPrintable(stale.join(QLatin1String("\n"))));
    }

    // The seven design decisions the R2 plan makes by name. These ARE
    // hardcoded, because they are choices rather than derivable facts: all
    // seven carry WRITE, so nothing in the metaobject would deny them.
    void theSevenWritableOutboundOnlyPropertiesAreDeniedInbound()
    {
        SliceModel slice(0);
        const MirrorSchema& schema = MirrorSchema::forObject(&slice);
        for (const char* name : { "chainIndex", "ddcIndex", "streamIndex",
                                  "shiftOffsetHz", "sampleRateHz",
                                  "widebandExtensionRequested", "psPaused" }) {
            const MirrorProperty* p = schema.byName(name);
            QVERIFY2(p != nullptr, name);
            QVERIFY2(p->isWritable,
                     qPrintable(QStringLiteral("%1 is expected to carry WRITE; "
                                               "if it lost it, this entry is "
                                               "now redundant").arg(name)));
            QVERIFY2(MirrorPolicy::directionFor("SliceModel", name)
                         == MirrorDirection::Outbound, name);
            QVERIFY2(!MirrorPolicy::inboundAllowed("SliceModel", name), name);
        }
    }

    // The R2 plan's step 5 deliberately corrects the design addendum's 6.1
    // here: panKey is mirrored outbound so a reconnecting client restores its
    // layout AND applied inbound under the mirror's guard, with pan-affecting
    // creation routed through the addSliceOnPan verb instead.
    void panKeyIsBidirectional()
    {
        QCOMPARE(MirrorPolicy::directionFor("SliceModel", "panKey"),
                 MirrorDirection::Bidirectional);
        QVERIFY(MirrorPolicy::inboundAllowed("SliceModel", "panKey"));
    }

    void sliceIndexIsConstantSnapshotAndNeverInbound()
    {
        QCOMPARE(MirrorPolicy::directionFor("SliceModel", "sliceIndex"),
                 MirrorDirection::ConstantSnapshot);
        QVERIFY(!MirrorPolicy::inboundAllowed("SliceModel", "sliceIndex"));
    }

    void bandIsOutbound()
    {
        QCOMPARE(MirrorPolicy::directionFor("SliceModel", "band"),
                 MirrorDirection::Outbound);
        QVERIFY(!MirrorPolicy::inboundAllowed("SliceModel", "band"));
    }

    // A representative operator control has to actually be writable back, or
    // the whole default-deny table is a very elaborate way of denying
    // everything.
    void ordinaryOperatorControlsAreBidirectional()
    {
        for (const char* name : { "frequency", "dspMode", "filterLow",
                                  "agcMode", "afGain", "muted" }) {
            QVERIFY2(MirrorPolicy::inboundAllowed("SliceModel", name), name);
        }
        QVERIFY(MirrorPolicy::inboundAllowed("TransmitModel", "power"));
        QVERIFY(MirrorPolicy::inboundAllowed("PanadapterModel", "centerFrequency"));
    }

    void policyEntriesAreUnique()
    {
        QSet<QByteArray> seen;
        QStringList dupes;
        for (const MirrorPolicy::Entry& e : MirrorPolicy::entries()) {
            const QByteArray key = QByteArray(e.className) + "::" + e.property;
            if (seen.contains(key)) {
                dupes << QString::fromUtf8(key);
            }
            seen.insert(key);
        }
        QVERIFY2(dupes.isEmpty(), qPrintable(dupes.join(QLatin1String(", "))));
    }

private:
    static QList<const QMetaObject*> mirroredMetaObjects()
    {
        return { &SliceModel::staticMetaObject,
                 &TransmitModel::staticMetaObject,
                 &TunerModel::staticMetaObject,
                 &RadioModel::staticMetaObject,
                 &PanadapterModel::staticMetaObject };
    }
};

QTEST_MAIN(TestMirrorSchema)
#include "tst_mirror_schema.moc"
