// =================================================================
// tests/tst_core_target_store.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original unit-test file. Remote-daemon R3
// Task 4g saved Core address-book persistence.
//
// iPhone app Task 18 (R-IOS-08): the list lives under ConnectionTargets/V2
// with each Core's identity fingerprint; a V1 list migrates once, every
// record's trust details exactly, and is never read again.
// =================================================================

#include <QtTest/QtTest>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTemporaryDir>

#include "core/AppSettings.h"
#include "core/settings/SettingsProxy.h"
#include "core/settings/SettingsScope.h"
#include "core/security/StationIdentity.h"
#include "gui/CoreTargetStore.h"

using namespace NereusSDR;

namespace {

constexpr auto kTargetKey = "ConnectionTargets/V2";
constexpr auto kV1Key = "ConnectionTargets/V1";

SavedCoreTarget makeTarget(const QString& id, const QString& token = QStringLiteral("token"))
{
    SavedCoreTarget target;
    target.id = id;
    target.label = QStringLiteral("Bench Core");
    target.connection.url = QStringLiteral("wss://core.example.test:4433");
    target.connection.token = token;
    target.connection.fingerprint = QStringLiteral("fingerprint");
    target.connection.allowUnpinned = false;
    target.lastRadioName = QStringLiteral("Radio One");
    target.lastRadioMac = QStringLiteral("00:11:22:33:44:55");
    return target;
}

QString documentFor(const QJsonArray& cores, const QString& selectedId = QStringLiteral("local"),
                    int version = 2)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("version"), version},
        {QStringLiteral("selectedId"), selectedId},
        {QStringLiteral("cores"), cores},
    }).toJson(QJsonDocument::Compact));
}

// A V1 record: no identity key.
QJsonObject jsonV1Target(const QString& id)
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("label"), QStringLiteral("Core")},
        {QStringLiteral("url"), QStringLiteral("wss://core.example.test")},
        {QStringLiteral("token"), QStringLiteral("secret")},
        {QStringLiteral("fingerprint"), QStringLiteral("pin")},
        {QStringLiteral("allowUnpinned"), false},
        {QStringLiteral("lastRadioName"), QStringLiteral("Radio")},
        {QStringLiteral("lastRadioMac"), QStringLiteral("aa:bb")},
    };
}

// A V2 record: the V1 fields and the identity (empty: not paired).
QJsonObject jsonTarget(const QString& id, const QString& identity = QString())
{
    QJsonObject object = jsonV1Target(id);
    object.insert(QStringLiteral("identity"), identity);
    return object;
}

// 32 bytes made at run time, never a real Core's.
QByteArray someIdentity()
{
    QByteArray bytes(32, '\0');
    for (int i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    }
    return bytes;
}

} // namespace

class TstCoreTargetStore : public QObject {
    Q_OBJECT

private slots:
    void newKeyIsOperatorLocal()
    {
        for (const char* key : {kV1Key, kTargetKey}) {
            QCOMPARE(classifySettingsKey(QString::fromLatin1(key)), SettingsScope::OperatorLocal);
            SettingsProxy proxy;
            QVERIFY(!proxy.handlesKey(QString::fromLatin1(key)));
        }
    }

    void persistsAndReloadsMultipleIndependentCredentials()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("settings.xml"));
        AppSettings settings(path);
        CoreTargetStore store(settings);
        QVERIFY(store.load());

        SavedCoreTarget first = makeTarget(QStringLiteral("first"), QStringLiteral("first-token"));
        SavedCoreTarget second = makeTarget(QStringLiteral("second"), QStringLiteral("second-token"));
        second.connection.url = QStringLiteral("ws://second.example.test:9000");
        second.connection.allowUnpinned = true;
        second.lastRadioName = QStringLiteral("Radio Two");

        QVERIFY(store.upsert(first));
        QVERIFY(store.upsert(second));
        QVERIFY(store.select(second.id));

        AppSettings reloadedSettings(path);
        reloadedSettings.load();
        CoreTargetStore reloaded(reloadedSettings);
        QVERIFY(reloaded.load());
        QCOMPARE(reloaded.selectedId(), QStringLiteral("second"));
        QCOMPARE(reloaded.targets().size(), 2);
        const auto restoredFirst = reloaded.target(QStringLiteral("first"));
        const auto restoredSecond = reloaded.target(QStringLiteral("second"));
        QVERIFY(restoredFirst.has_value());
        QVERIFY(restoredSecond.has_value());
        QCOMPARE(restoredFirst->connection.token, QStringLiteral("first-token"));
        QCOMPARE(restoredSecond->connection.token, QStringLiteral("second-token"));
        QCOMPARE(restoredSecond->connection.url, QStringLiteral("ws://second.example.test:9000"));
        QVERIFY(restoredSecond->connection.allowUnpinned);
        QCOMPARE(restoredSecond->lastRadioName, QStringLiteral("Radio Two"));
    }

    void migratesExactLegacyTrustTupleOnce()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("settings.xml"));
        AppSettings settings(path);
        settings.setValue(QStringLiteral("RemoteStationUrl"),
                          QStringLiteral("wss://Legacy.Example.Test:4433/path"));
        settings.setValue(QStringLiteral("RemoteStationToken"), QStringLiteral(" legacy token "));
        settings.setValue(QStringLiteral("RemoteStationFingerprint"), QStringLiteral(" legacy pin "));
        settings.setValue(QStringLiteral("RemoteStationAllowUnpinned"), QStringLiteral("True"));

        CoreTargetStore store(settings);
        QVERIFY(store.load());
        QCOMPARE(store.selectedId(), QStringLiteral("legacy-core"));
        const auto migrated = store.target(QStringLiteral("legacy-core"));
        QVERIFY(migrated.has_value());
        QCOMPARE(migrated->connection.url, QStringLiteral("wss://Legacy.Example.Test:4433/path"));
        QCOMPARE(migrated->connection.token, QStringLiteral(" legacy token "));
        QCOMPARE(migrated->connection.fingerprint, QStringLiteral(" legacy pin "));
        QVERIFY(migrated->connection.allowUnpinned);
        QCOMPARE(migrated->label, QStringLiteral("legacy.example.test"));
        QVERIFY(settings.contains(QLatin1String(kTargetKey)));

        QVERIFY(store.select(QStringLiteral("local")));
        QVERIFY(store.remove(QStringLiteral("legacy-core")));
        CoreTargetStore secondLoad(settings);
        QVERIFY(secondLoad.load());
        QCOMPARE(secondLoad.selectedId(), QStringLiteral("local"));
        QVERIFY(secondLoad.targets().isEmpty());
    }

    void invalidLegacyAddressDoesNotOverwrite()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        AppSettings settings(directory.filePath(QStringLiteral("settings.xml")));
        settings.setValue(QStringLiteral("RemoteStationUrl"), QStringLiteral("https://wrong.example.test"));
        CoreTargetStore store(settings);
        QString error;

        QVERIFY(!store.load(&error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!settings.contains(QLatin1String(kTargetKey)));
        QCOMPARE(store.selectedId(), QStringLiteral("local"));
        QVERIFY(store.targets().isEmpty());
    }

    void malformedDocumentDoesNotReplaceLoadedState()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        AppSettings settings(directory.filePath(QStringLiteral("settings.xml")));
        CoreTargetStore store(settings);
        QVERIFY(store.load());
        QVERIFY(store.upsert(makeTarget(QStringLiteral("known"))));
        QVERIFY(store.select(QStringLiteral("known")));
        const QString malformed = documentFor(QJsonArray{jsonTarget(QStringLiteral("known"))},
                                              QStringLiteral("missing"));
        settings.setValue(QLatin1String(kTargetKey), malformed);
        QString error;

        QVERIFY(!store.load(&error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(store.selectedId(), QStringLiteral("known"));
        QCOMPARE(store.targets().size(), 1);
        QCOMPARE(store.targets().first().id, QStringLiteral("known"));
        QCOMPARE(settings.value(QLatin1String(kTargetKey)).toString(), malformed);
    }

    void rejectsTypedMalformedAndBoundedDocuments()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        AppSettings settings(directory.filePath(QStringLiteral("settings.xml")));
        CoreTargetStore store(settings);
        QString error;

        QJsonObject badType = jsonTarget(QStringLiteral("one"));
        badType.insert(QStringLiteral("allowUnpinned"), QStringLiteral("False"));
        settings.setValue(QLatin1String(kTargetKey),
                          documentFor(QJsonArray{badType}, QStringLiteral("one")));
        QVERIFY(!store.load(&error));

        settings.setValue(QLatin1String(kTargetKey),
                          documentFor(QJsonArray{jsonTarget(QStringLiteral("one")),
                                                 jsonTarget(QStringLiteral("one"))},
                                      QStringLiteral("one")));
        QVERIFY(!store.load(&error));

        QJsonArray tooMany;
        for (int i = 0; i < 129; ++i) {
            tooMany.append(jsonTarget(QStringLiteral("id%1").arg(i)));
        }
        settings.setValue(QLatin1String(kTargetKey), documentFor(tooMany));
        QVERIFY(!store.load(&error));

        QJsonObject tooLong = jsonTarget(QStringLiteral("one"));
        tooLong.insert(QStringLiteral("token"), QString(8193, QLatin1Char('x')));
        settings.setValue(QLatin1String(kTargetKey),
                          documentFor(QJsonArray{tooLong}, QStringLiteral("one")));
        QVERIFY(!store.load(&error));
    }

    void mutationsRequireSuccessfulLoadAndRecoverAfterReload()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("settings.xml"));
        AppSettings settings(path);
        const QString valid = documentFor(QJsonArray{jsonTarget(QStringLiteral("one"))},
                                          QStringLiteral("one"));
        settings.setValue(QLatin1String(kTargetKey), valid);
        QVERIFY(settings.save());

        CoreTargetStore store(settings);
        QString error;
        QVERIFY(!store.upsert(makeTarget(QStringLiteral("two")), &error));
        QVERIFY(!store.remove(QStringLiteral("one"), &error));
        QVERIFY(!store.select(QStringLiteral("local"), &error));
        QCOMPARE(settings.value(QLatin1String(kTargetKey)).toString(), valid);
        AppSettings unchangedOnDisk(path);
        unchangedOnDisk.load();
        QCOMPARE(unchangedOnDisk.value(QLatin1String(kTargetKey)).toString(), valid);

        QVERIFY(store.load());
        QCOMPARE(store.selectedId(), QStringLiteral("one"));
        QCOMPARE(store.targets().size(), 1);
        const QString malformed = QStringLiteral("{bad json");
        settings.setValue(QLatin1String(kTargetKey), malformed);
        QVERIFY(!store.load(&error));
        QCOMPARE(store.selectedId(), QStringLiteral("one"));
        QCOMPARE(store.targets().size(), 1);
        QVERIFY(!store.upsert(makeTarget(QStringLiteral("two")), &error));
        QVERIFY(!store.remove(QStringLiteral("one"), &error));
        QVERIFY(!store.select(QStringLiteral("local"), &error));
        QCOMPARE(settings.value(QLatin1String(kTargetKey)).toString(), malformed);

        settings.setValue(QLatin1String(kTargetKey), valid);
        QVERIFY(store.load());
        QVERIFY(store.upsert(makeTarget(QStringLiteral("two"))));
        QVERIFY(store.remove(QStringLiteral("two")));
        QVERIFY(store.select(QStringLiteral("local")));
    }

    void serializedDocumentBoundPreventsAggregateEscapedGrowth()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        AppSettings settings(directory.filePath(QStringLiteral("settings.xml")));
        CoreTargetStore store(settings);
        QVERIFY(store.load());

        bool rejected = false;
        for (int i = 0; i < 128; ++i) {
            SavedCoreTarget target = makeTarget(QStringLiteral("id%1").arg(i));
            // The field is individually legal, but JSON escaping doubles
            // every byte and must not create a document load() will reject.
            target.connection.token = QString(8192, QLatin1Char('\\'));
            const QString before = settings.value(QLatin1String(kTargetKey)).toString();
            const int countBefore = store.targets().size();
            QString error;
            if (!store.upsert(target, &error)) {
                QVERIFY(!error.isEmpty());
                QCOMPARE(settings.value(QLatin1String(kTargetKey)).toString(), before);
                QCOMPARE(store.targets().size(), countBefore);
                rejected = true;
                break;
            }
        }
        QVERIFY(rejected);
    }

    void mutationRejectsUnsafeIdAndPreservesOrdering()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        AppSettings settings(directory.filePath(QStringLiteral("settings.xml")));
        CoreTargetStore store(settings);
        QVERIFY(store.load());
        QVERIFY(store.upsert(makeTarget(QStringLiteral("first"))));
        QVERIFY(store.upsert(makeTarget(QStringLiteral("second"))));

        SavedCoreTarget updated = makeTarget(QStringLiteral("first"), QStringLiteral("new-token"));
        updated.lastRadioMac = QStringLiteral("new-mac");
        QVERIFY(store.upsert(updated));
        QCOMPARE(store.targets().at(0).id, QStringLiteral("first"));
        QCOMPARE(store.targets().at(1).id, QStringLiteral("second"));
        QCOMPARE(store.targets().at(0).connection.token, QStringLiteral("new-token"));
        QCOMPARE(store.targets().at(0).lastRadioMac, QStringLiteral("new-mac"));

        SavedCoreTarget unsafe = makeTarget(QStringLiteral("local"));
        QString error;
        QVERIFY(!store.upsert(unsafe, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!store.remove(QStringLiteral("missing"), &error));
        QVERIFY(!store.select(QStringLiteral("missing"), &error));
    }

    void saveFailureRollsBackExistingKeyAndAbsentMigration()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString blocker = directory.filePath(QStringLiteral("not-a-file"));
        QVERIFY(QDir().mkpath(blocker));
        AppSettings settings(blocker);
        const QString previous = documentFor(QJsonArray{jsonTarget(QStringLiteral("first"))},
                                             QStringLiteral("first"));
        settings.setValue(QLatin1String(kTargetKey), previous);
        CoreTargetStore store(settings);
        QVERIFY(store.load());
        QString error;

        QVERIFY(!store.upsert(makeTarget(QStringLiteral("second")), &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(settings.value(QLatin1String(kTargetKey)).toString(), previous);
        QCOMPARE(store.targets().size(), 1);
        QCOMPARE(store.selectedId(), QStringLiteral("first"));

        AppSettings missingSettings(blocker);
        CoreTargetStore missingStore(missingSettings);
        QVERIFY(!missingStore.load(&error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!missingSettings.contains(QLatin1String(kTargetKey)));
        QVERIFY(missingStore.targets().isEmpty());
        QCOMPARE(missingStore.selectedId(), QStringLiteral("local"));
    }

    // iPhone app Task 18: the V1 list moves to V2 once. Every record's
    // trust details (address, token, pin, bench flag) and its label, last
    // radio and the selection come over exactly, with no identity; after
    // that V2 is what is read, whatever V1 holds.
    void v1RecordsMigrateOnceWithTheirTrustExactly()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("settings.xml"));
        AppSettings settings(path);
        QJsonObject first = jsonV1Target(QStringLiteral("first"));
        QJsonObject second = jsonV1Target(QStringLiteral("second"));
        second.insert(QStringLiteral("url"), QStringLiteral("ws://[2001:db8::7]:9000"));
        second.insert(QStringLiteral("token"), QStringLiteral(" spaced token "));
        second.insert(QStringLiteral("fingerprint"), QString());
        second.insert(QStringLiteral("allowUnpinned"), true);
        second.insert(QStringLiteral("label"), QStringLiteral("Bench"));
        second.insert(QStringLiteral("lastRadioName"), QStringLiteral("Saturn"));
        second.insert(QStringLiteral("lastRadioMac"), QStringLiteral("AA:BB:CC:DD:EE:01"));
        const QString v1 = documentFor(QJsonArray{first, second}, QStringLiteral("second"), 1);
        settings.setValue(QLatin1String(kV1Key), v1);
        QVERIFY(settings.save());

        CoreTargetStore store(settings);
        QVERIFY(store.load());
        QCOMPARE(store.selectedId(), QStringLiteral("second"));
        QCOMPARE(store.targets().size(), 2);
        const auto migrated = store.target(QStringLiteral("second"));
        QVERIFY(migrated.has_value());
        QCOMPARE(migrated->label, QStringLiteral("Bench"));
        QCOMPARE(migrated->connection.url, QStringLiteral("ws://[2001:db8::7]:9000"));
        QCOMPARE(migrated->connection.token, QStringLiteral(" spaced token "));
        QVERIFY(migrated->connection.fingerprint.isEmpty());
        QVERIFY(migrated->connection.allowUnpinned);
        QCOMPARE(migrated->lastRadioName, QStringLiteral("Saturn"));
        QCOMPARE(migrated->lastRadioMac, QStringLiteral("AA:BB:CC:DD:EE:01"));
        QVERIFY(migrated->connection.identityFingerprint.isEmpty());
        const auto plain = store.target(QStringLiteral("first"));
        QVERIFY(plain.has_value());
        QCOMPARE(plain->connection.token, QStringLiteral("secret"));
        QCOMPARE(plain->connection.fingerprint, QStringLiteral("pin"));
        QVERIFY(!plain->connection.allowUnpinned);

        // Written as V2 on disk; V1 is left exactly as it was.
        AppSettings onDisk(path);
        onDisk.load();
        QVERIFY(onDisk.contains(QLatin1String(kTargetKey)));
        QCOMPARE(onDisk.value(QLatin1String(kV1Key)).toString(), v1);
        const QJsonObject v2 = QJsonDocument::fromJson(
            onDisk.value(QLatin1String(kTargetKey)).toString().toUtf8()).object();
        QCOMPARE(v2.value(QStringLiteral("version")).toInt(), 2);
        for (const QJsonValue& core : v2.value(QStringLiteral("cores")).toArray()) {
            QCOMPARE(core.toObject().value(QStringLiteral("identity")).toString(), QString());
        }

        // Never read again: a change to V1 now has no effect.
        settings.setValue(QLatin1String(kV1Key),
                          documentFor(QJsonArray{jsonV1Target(QStringLiteral("other"))},
                                      QStringLiteral("other"), 1));
        CoreTargetStore again(settings);
        QVERIFY(again.load());
        QCOMPARE(again.targets().size(), 2);
        QVERIFY(!again.target(QStringLiteral("other")).has_value());
        QCOMPARE(again.selectedId(), QStringLiteral("second"));
    }

    // A V1 list that cannot be read writes nothing and is left as it is.
    void unreadableV1ListIsNotMigrated()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        AppSettings settings(directory.filePath(QStringLiteral("settings.xml")));
        const QString malformed = documentFor(QJsonArray{jsonV1Target(QStringLiteral("one"))},
                                              QStringLiteral("missing"), 1);
        settings.setValue(QLatin1String(kV1Key), malformed);
        CoreTargetStore store(settings);
        QString error;
        QVERIFY(!store.load(&error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!settings.contains(QLatin1String(kTargetKey)));
        QCOMPARE(settings.value(QLatin1String(kV1Key)).toString(), malformed);
        // A V2 document is not accepted as V1, nor a V1 document as V2.
        settings.setValue(QLatin1String(kV1Key),
                          documentFor(QJsonArray{jsonTarget(QStringLiteral("one"))},
                                      QStringLiteral("one"), 2));
        QVERIFY(!store.load(&error));
        settings.remove(QLatin1String(kV1Key));
        settings.setValue(QLatin1String(kTargetKey),
                          documentFor(QJsonArray{jsonV1Target(QStringLiteral("one"))},
                                      QStringLiteral("one"), 1));
        QVERIFY(!store.load(&error));
    }

    // A V1 migration that cannot be saved fails the load and writes nothing.
    void v1MigrationThatCannotBeSavedFails()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString blocker = directory.filePath(QStringLiteral("not-a-file"));
        QVERIFY(QDir().mkpath(blocker));
        AppSettings settings(blocker);
        settings.setValue(QLatin1String(kV1Key),
                          documentFor(QJsonArray{jsonV1Target(QStringLiteral("one"))},
                                      QStringLiteral("one"), 1));
        CoreTargetStore store(settings);
        QString error;
        QVERIFY(!store.load(&error));
        QVERIFY(!settings.contains(QLatin1String(kTargetKey)));
        QVERIFY(store.targets().isEmpty());
    }

    // The identity round-trips; a Core with none stays a valid entry; a
    // wrong length or unreadable identity is refused, on write and on load.
    void identityRoundTripsAndIsChecked()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("settings.xml"));
        AppSettings settings(path);
        CoreTargetStore store(settings);
        QVERIFY(store.load());

        SavedCoreTarget paired = makeTarget(QStringLiteral("paired"), QString());
        paired.connection.fingerprint.clear();
        paired.connection.identityFingerprint = someIdentity();
        const SavedCoreTarget tokenOnly = makeTarget(QStringLiteral("token-only"));
        QVERIFY(store.upsert(paired));
        QVERIFY(store.upsert(tokenOnly));

        AppSettings reloadedSettings(path);
        reloadedSettings.load();
        CoreTargetStore reloaded(reloadedSettings);
        QVERIFY(reloaded.load());
        QCOMPARE(reloaded.target(QStringLiteral("paired"))->connection.identityFingerprint,
                 paired.connection.identityFingerprint);
        QVERIFY(reloaded.target(QStringLiteral("token-only"))->connection.identityFingerprint
                    .isEmpty());
        QCOMPARE(reloaded.target(QStringLiteral("token-only"))->connection.token,
                 QStringLiteral("token"));

        SavedCoreTarget shortIdentity = paired;
        shortIdentity.connection.identityFingerprint.chop(1);
        QString error;
        QVERIFY(!store.upsert(shortIdentity, &error));
        QVERIFY(!error.isEmpty());

        const QString goodIdentity = StationIdentity::toBase64Url(someIdentity());
        settings.setValue(QLatin1String(kTargetKey),
                          documentFor(QJsonArray{jsonTarget(QStringLiteral("one"), goodIdentity)},
                                      QStringLiteral("one")));
        QVERIFY(store.load());
        QCOMPARE(store.target(QStringLiteral("one"))->connection.identityFingerprint.size(), 32);

        for (const QString& bad : {QStringLiteral("not base64!"),
                                   StationIdentity::toBase64Url(QByteArray(31, 'x')),
                                   goodIdentity + QStringLiteral("=")}) {
            settings.setValue(QLatin1String(kTargetKey),
                              documentFor(QJsonArray{jsonTarget(QStringLiteral("one"), bad)},
                                          QStringLiteral("one")));
            QVERIFY2(!store.load(&error), qPrintable(bad));
        }
        QJsonObject noIdentityKey = jsonTarget(QStringLiteral("one"));
        noIdentityKey.remove(QStringLiteral("identity"));
        settings.setValue(QLatin1String(kTargetKey),
                          documentFor(QJsonArray{noIdentityKey}, QStringLiteral("one")));
        QVERIFY(!store.load(&error));
    }
};

QTEST_APPLESS_MAIN(TstCoreTargetStore)

#include "tst_core_target_store.moc"
