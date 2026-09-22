// no-port-check: NereusSDR-original. R-R3-38 credential isolation regression.
#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "core/AppSettings.h"
#include "gui/CoreTargetStore.h"
#include "gui/StationStartupSelection.h"

using namespace NereusSDR;

class TestStationStartupSelection : public QObject {
    Q_OBJECT
private slots:
    void resolvesSelectedTupleAndNeverBorrowsForAnotherAddress()
    {
        QTemporaryDir directory;
        AppSettings settings(directory.filePath(QStringLiteral("settings.xml")));
        CoreTargetStore store(settings);
        QVERIFY(store.load());
        const SavedCoreTarget first{QStringLiteral("first"), QStringLiteral("First"),
            {QStringLiteral("wss://first.example.test:4433"), QStringLiteral("first-token"),
             QStringLiteral("first-pin"), true}, {}, {}};
        const SavedCoreTarget second{QStringLiteral("second"), QStringLiteral("Second"),
            {QStringLiteral("wss://second.example.test:4433"), QStringLiteral("second-token"),
             QStringLiteral("second-pin"), false}, {}, {}};
        QVERIFY(store.upsert(first));
        QVERIFY(store.upsert(second));
        QVERIFY(store.select(first.id));
        const auto saved = resolveStationStartup({}, store);
        QVERIFY(saved);
        QCOMPARE(saved->savedId, first.id);
        QCOMPARE(saved->connection.token, first.connection.token);
        QCOMPARE(saved->connection.fingerprint, first.connection.fingerprint);
        QVERIFY(saved->connection.allowUnpinned);

        StationStartupRequest cli;
        cli.stationSpecified = true;
        cli.connection.url = second.connection.url;
        const auto other = resolveStationStartup(cli, store);
        QVERIFY(other);
        QVERIFY(other->savedId.isEmpty());
        QCOMPARE(other->connection.url, second.connection.url);
        QVERIFY(other->connection.token.isEmpty());
        QVERIFY(other->connection.fingerprint.isEmpty());
        QVERIFY(!other->connection.allowUnpinned);
        QCOMPARE(store.selectedId(), first.id);

        cli.connection.url = first.connection.url;
        const auto same = resolveStationStartup(cli, store);
        QVERIFY(same);
        QCOMPARE(same->connection.token, first.connection.token);
        QCOMPARE(same->connection.fingerprint, first.connection.fingerprint);
    }

    void explicitEmptyCredentialsAndPinOverrideAreHonoured()
    {
        QTemporaryDir directory;
        AppSettings settings(directory.filePath(QStringLiteral("settings.xml")));
        CoreTargetStore store(settings);
        QVERIFY(store.load());
        const SavedCoreTarget target{QStringLiteral("core"), {},
            {QStringLiteral("wss://core.example.test"), QStringLiteral("secret"),
             QStringLiteral("old-pin"), true}, {}, {}};
        QVERIFY(store.upsert(target));
        QVERIFY(store.select(target.id));
        StationStartupRequest request;
        request.tokenSpecified = true;
        request.fingerprintSpecified = true;
        request.connection.fingerprint = QStringLiteral("new-pin");
        const auto result = resolveStationStartup(request, store);
        QVERIFY(result);
        QVERIFY(result->connection.token.isEmpty());
        QCOMPARE(result->connection.fingerprint, QStringLiteral("new-pin"));
        QVERIFY(!result->connection.allowUnpinned);
        request.allowUnpinnedSpecified = true;
        QVERIFY(resolveStationStartup(request, store)->connection.allowUnpinned);
        QCOMPARE(store.target(target.id)->connection.token, QStringLiteral("secret"));
        QCOMPARE(store.target(target.id)->connection.fingerprint, QStringLiteral("old-pin"));
    }

    void explicitLocalSuppressesSavedAndLegacyRemote()
    {
        QTemporaryDir directory;
        AppSettings settings(directory.filePath(QStringLiteral("settings.xml")));
        settings.setValue(QStringLiteral("RemoteStationUrl"), QStringLiteral("wss://old.example.test"));
        settings.setValue(QStringLiteral("RemoteStationToken"), QStringLiteral("secret"));
        CoreTargetStore store(settings);
        QVERIFY(store.load());
        StationStartupRequest request;
        request.local = true;
        const auto local = resolveStationStartup(request, store);
        QVERIFY(local);
        QCOMPARE(local->savedId, QStringLiteral("local"));
        QVERIFY(!local->connection.isRemote());
        QVERIFY(local->connection.token.isEmpty());
        QVERIFY(store.select(QStringLiteral("local")));
        const auto persistedLocal = resolveStationStartup({}, store);
        QVERIFY(persistedLocal);
        QVERIFY(!persistedLocal->connection.isRemote());
    }

    void conflictingAndMalformedInputDoesNotSelectAnotherTarget()
    {
        QTemporaryDir directory;
        AppSettings settings(directory.filePath(QStringLiteral("settings.xml")));
        CoreTargetStore store(settings);
        QVERIFY(store.load());
        StationStartupRequest request;
        request.local = true;
        request.stationSpecified = true;
        request.connection.url = QStringLiteral("wss://core.example.test");
        QString error;
        QVERIFY(!resolveStationStartup(request, store, &error));
        QVERIFY(!error.isEmpty());
        request.local = false;
        request.connection.url = QStringLiteral("https://private-secret@broken[");
        QVERIFY(!resolveStationStartup(request, store, &error));
        QVERIFY(!error.contains(QStringLiteral("private-secret")));
        request.stationSpecified = false;
        request.tokenSpecified = true;
        QVERIFY(!resolveStationStartup(request, store, &error));
        QCOMPARE(store.selectedId(), QStringLiteral("local"));
    }
};

QTEST_APPLESS_MAIN(TestStationStartupSelection)
#include "tst_station_startup_selection.moc"
