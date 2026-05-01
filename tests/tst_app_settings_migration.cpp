// Phase 3Q Task 12 — migrateRadioKey unit tests.
//
// Verifies AppSettings::migrateRadioKey() moves all fields stored under a
// synthetic "manual-<ip>-<port>" key to a real-MAC key, and fully removes
// the synthetic key after migration.  Uses the direct-construction path
// (AppSettings(filePath)) so each test case operates on an isolated in-memory
// store — no singleton, no disk I/O required.

#include <QtTest/QtTest>
#include "core/AppSettings.h"

#include <QTemporaryDir>

using namespace NereusSDR;

class TstAppSettingsMigration : public QObject {
    Q_OBJECT

private:
    // Build a minimal synthetic SavedRadio entry under a manual key,
    // then migrate it to a real MAC and verify both sides of the invariant:
    //   (a) new key has the expected field values
    //   (b) old key is fully gone
    void populateManualEntry(AppSettings& s,
                              const QString& manualKey,
                              const QString& name,
                              const QString& ip,
                              quint16 port,
                              bool autoConnect,
                              bool pinToMac)
    {
        RadioInfo info;
        info.name       = name;
        info.address    = QHostAddress(ip);
        info.port       = port;
        // macAddress intentionally blank — as it would be for an offline save
        info.boardType  = HPSDRHW::Unknown;
        info.protocol   = ProtocolVersion::Protocol1;
        s.saveRadio(info, pinToMac, autoConnect);
        // Confirm the key was written under the expected synthetic path
        QVERIFY(s.savedRadio(manualKey).has_value());
    }

private slots:
    // ─────────────────────────────────────────────────────────────────────
    // Core migration: name + autoConnect move to real MAC; old key gone
    // ─────────────────────────────────────────────────────────────────────
    void migratesManualKeyToRealMac()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AppSettings s(tmp.filePath(QStringLiteral("NereusSDR.settings")));

        const QString manualKey = QStringLiteral("manual-192.168.8.3-1024");
        const QString realMac   = QStringLiteral("00:1C:C0:A2:14:8B");

        populateManualEntry(s, manualKey,
                            QStringLiteral("Beach HL2"),
                            QStringLiteral("192.168.8.3"),
                            1024,
                            /*autoConnect=*/true,
                            /*pinToMac=*/false);

        s.migrateRadioKey(manualKey, realMac);

        // New key exists with the expected name
        auto migrated = s.savedRadio(realMac);
        QVERIFY2(migrated.has_value(), "Expected real-MAC entry after migration");
        QCOMPARE(migrated->info.name, QStringLiteral("Beach HL2"));

        // autoConnect flag is preserved
        QVERIFY(migrated->autoConnect);

        // info.macAddress is updated to the real MAC
        QCOMPARE(migrated->info.macAddress, realMac);

        // Old key is fully gone
        QVERIFY2(!s.savedRadio(manualKey).has_value(),
                 "Expected manual-key entry to be removed after migration");
    }

    // ─────────────────────────────────────────────────────────────────────
    // pinToMac flag is carried across
    // ─────────────────────────────────────────────────────────────────────
    void preservesPinToMac()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AppSettings s(tmp.filePath(QStringLiteral("NereusSDR.settings")));

        const QString manualKey = QStringLiteral("manual-10.0.0.1-1024");
        const QString realMac   = QStringLiteral("AA:BB:CC:DD:EE:FF");

        populateManualEntry(s, manualKey,
                            QStringLiteral("Office ANAN"),
                            QStringLiteral("10.0.0.1"),
                            1024,
                            /*autoConnect=*/false,
                            /*pinToMac=*/true);

        s.migrateRadioKey(manualKey, realMac);

        auto migrated = s.savedRadio(realMac);
        QVERIFY(migrated.has_value());
        QVERIFY(migrated->pinToMac);
        QVERIFY(!migrated->autoConnect);
    }

    // ─────────────────────────────────────────────────────────────────────
    // No-op when oldKey == newKey
    // ─────────────────────────────────────────────────────────────────────
    void noOpWhenSameKey()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AppSettings s(tmp.filePath(QStringLiteral("NereusSDR.settings")));

        const QString mac = QStringLiteral("11:22:33:44:55:66");

        RadioInfo info;
        info.name       = QStringLiteral("Same Key Radio");
        info.macAddress = mac;
        info.address    = QHostAddress(QStringLiteral("192.168.1.1"));
        info.port       = 1024;
        s.saveRadio(info, false, false);

        s.migrateRadioKey(mac, mac); // same → no-op

        auto entry = s.savedRadio(mac);
        QVERIFY2(entry.has_value(), "Entry should still exist after same-key no-op");
        QCOMPARE(entry->info.name, QStringLiteral("Same Key Radio"));
    }

    // ─────────────────────────────────────────────────────────────────────
    // No-op when the old key has no stored entry
    // ─────────────────────────────────────────────────────────────────────
    void noOpWhenOldKeyAbsent()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AppSettings s(tmp.filePath(QStringLiteral("NereusSDR.settings")));

        // Neither key exists — should not crash, and new key should stay absent
        s.migrateRadioKey(QStringLiteral("manual-1.2.3.4-1024"),
                          QStringLiteral("AA:BB:CC:00:11:22"));

        QVERIFY(!s.savedRadio(QStringLiteral("manual-1.2.3.4-1024")).has_value());
        QVERIFY(!s.savedRadio(QStringLiteral("AA:BB:CC:00:11:22")).has_value());
    }

    // ─────────────────────────────────────────────────────────────────────
    // IP address and port are carried through
    // ─────────────────────────────────────────────────────────────────────
    void preservesIpAndPort()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AppSettings s(tmp.filePath(QStringLiteral("NereusSDR.settings")));

        const QString manualKey = QStringLiteral("manual-192.168.50.10-1024");
        const QString realMac   = QStringLiteral("DE:AD:BE:EF:CA:FE");

        populateManualEntry(s, manualKey,
                            QStringLiteral("VPN Radio"),
                            QStringLiteral("192.168.50.10"),
                            1024,
                            false, false);

        s.migrateRadioKey(manualKey, realMac);

        auto migrated = s.savedRadio(realMac);
        QVERIFY(migrated.has_value());
        QCOMPARE(migrated->info.address.toString(),
                 QStringLiteral("192.168.50.10"));
        QCOMPARE(migrated->info.port, static_cast<quint16>(1024));
    }

    // ─────────────────────────────────────────────────────────────────────
    // Old key leaves no orphan keys behind (field-complete cleanup)
    // ─────────────────────────────────────────────────────────────────────
    void noOrphanKeysAfterMigration()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AppSettings s(tmp.filePath(QStringLiteral("NereusSDR.settings")));

        const QString manualKey = QStringLiteral("manual-172.16.0.1-1024");
        const QString realMac   = QStringLiteral("CA:FE:BA:BE:00:01");

        populateManualEntry(s, manualKey,
                            QStringLiteral("Remote Station"),
                            QStringLiteral("172.16.0.1"),
                            1024,
                            true, true);

        s.migrateRadioKey(manualKey, realMac);

        // Scan all keys: none should start with "radios/manual-172.16.0.1-1024/"
        const QString oldPrefix = QStringLiteral("radios/%1/").arg(manualKey);
        const QStringList allKeys = s.allKeys();
        for (const QString& k : allKeys) {
            QVERIFY2(!k.startsWith(oldPrefix),
                     qPrintable(QStringLiteral("Orphan key found: ") + k));
        }
    }
};

QTEST_APPLESS_MAIN(TstAppSettingsMigration)
#include "tst_app_settings_migration.moc"
