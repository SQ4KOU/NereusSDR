// =================================================================
// tests/tst_core_target_store.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original unit-test file. Remote-daemon R3
// Task 4g saved Core address-book persistence.
// =================================================================

#include <QtTest/QtTest>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "core/AppSettings.h"
#include "core/settings/SettingsProxy.h"
#include "core/settings/SettingsScope.h"
#include "gui/CoreTargetStore.h"

using namespace NereusSDR;

namespace {

constexpr auto kTargetKey = "ConnectionTargets/V1";

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

QString documentFor(const QJsonArray& cores, const QString& selectedId = QStringLiteral("local"))
{
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("version"), 1},
        {QStringLiteral("selectedId"), selectedId},
        {QStringLiteral("cores"), cores},
    }).toJson(QJsonDocument::Compact));
}

QJsonObject jsonTarget(const QString& id)
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

} // namespace

class TstCoreTargetStore : public QObject {
    Q_OBJECT

private slots:
    void newKeyIsOperatorLocal()
    {
        QCOMPARE(classifySettingsKey(QStringLiteral("ConnectionTargets/V1")),
                 SettingsScope::OperatorLocal);
        SettingsProxy proxy;
        QVERIFY(!proxy.handlesKey(QStringLiteral("ConnectionTargets/V1")));
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
        QVERIFY(settings.contains(QStringLiteral("ConnectionTargets/V1")));

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
        QVERIFY(!settings.contains(QStringLiteral("ConnectionTargets/V1")));
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
        settings.setValue(QStringLiteral("ConnectionTargets/V1"), malformed);
        QString error;

        QVERIFY(!store.load(&error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(store.selectedId(), QStringLiteral("known"));
        QCOMPARE(store.targets().size(), 1);
        QCOMPARE(store.targets().first().id, QStringLiteral("known"));
        QCOMPARE(settings.value(QStringLiteral("ConnectionTargets/V1")).toString(), malformed);
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
        settings.setValue(QStringLiteral("ConnectionTargets/V1"),
                          documentFor(QJsonArray{badType}, QStringLiteral("one")));
        QVERIFY(!store.load(&error));

        settings.setValue(QStringLiteral("ConnectionTargets/V1"),
                          documentFor(QJsonArray{jsonTarget(QStringLiteral("one")),
                                                 jsonTarget(QStringLiteral("one"))},
                                      QStringLiteral("one")));
        QVERIFY(!store.load(&error));

        QJsonArray tooMany;
        for (int i = 0; i < 129; ++i) {
            tooMany.append(jsonTarget(QStringLiteral("id%1").arg(i)));
        }
        settings.setValue(QStringLiteral("ConnectionTargets/V1"), documentFor(tooMany));
        QVERIFY(!store.load(&error));

        QJsonObject tooLong = jsonTarget(QStringLiteral("one"));
        tooLong.insert(QStringLiteral("token"), QString(8193, QLatin1Char('x')));
        settings.setValue(QStringLiteral("ConnectionTargets/V1"),
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
        settings.setValue(QStringLiteral("ConnectionTargets/V1"), valid);
        QVERIFY(settings.save());

        CoreTargetStore store(settings);
        QString error;
        QVERIFY(!store.upsert(makeTarget(QStringLiteral("two")), &error));
        QVERIFY(!store.remove(QStringLiteral("one"), &error));
        QVERIFY(!store.select(QStringLiteral("local"), &error));
        QCOMPARE(settings.value(QStringLiteral("ConnectionTargets/V1")).toString(), valid);
        AppSettings unchangedOnDisk(path);
        unchangedOnDisk.load();
        QCOMPARE(unchangedOnDisk.value(QStringLiteral("ConnectionTargets/V1")).toString(), valid);

        QVERIFY(store.load());
        QCOMPARE(store.selectedId(), QStringLiteral("one"));
        QCOMPARE(store.targets().size(), 1);
        const QString malformed = QStringLiteral("{bad json");
        settings.setValue(QStringLiteral("ConnectionTargets/V1"), malformed);
        QVERIFY(!store.load(&error));
        QCOMPARE(store.selectedId(), QStringLiteral("one"));
        QCOMPARE(store.targets().size(), 1);
        QVERIFY(!store.upsert(makeTarget(QStringLiteral("two")), &error));
        QVERIFY(!store.remove(QStringLiteral("one"), &error));
        QVERIFY(!store.select(QStringLiteral("local"), &error));
        QCOMPARE(settings.value(QStringLiteral("ConnectionTargets/V1")).toString(), malformed);

        settings.setValue(QStringLiteral("ConnectionTargets/V1"), valid);
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
            const QString before = settings.value(QStringLiteral("ConnectionTargets/V1")).toString();
            const int countBefore = store.targets().size();
            QString error;
            if (!store.upsert(target, &error)) {
                QVERIFY(!error.isEmpty());
                QCOMPARE(settings.value(QStringLiteral("ConnectionTargets/V1")).toString(), before);
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
        settings.setValue(QStringLiteral("ConnectionTargets/V1"), previous);
        CoreTargetStore store(settings);
        QVERIFY(store.load());
        QString error;

        QVERIFY(!store.upsert(makeTarget(QStringLiteral("second")), &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(settings.value(QStringLiteral("ConnectionTargets/V1")).toString(), previous);
        QCOMPARE(store.targets().size(), 1);
        QCOMPARE(store.selectedId(), QStringLiteral("first"));

        AppSettings missingSettings(blocker);
        CoreTargetStore missingStore(missingSettings);
        QVERIFY(!missingStore.load(&error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!missingSettings.contains(QStringLiteral("ConnectionTargets/V1")));
        QVERIFY(missingStore.targets().isEmpty());
        QCOMPARE(missingStore.selectedId(), QStringLiteral("local"));
    }
};

QTEST_APPLESS_MAIN(TstCoreTargetStore)

#include "tst_core_target_store.moc"
