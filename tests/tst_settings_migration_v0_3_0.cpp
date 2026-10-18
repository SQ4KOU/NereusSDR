// Task 5.1 — SettingsSchemaVersion + v0.3.0 migration unit tests.
//
// Verifies AppSettings::ensureSettingsAtVersion(3):
//   - Removes the 4 retired display keys on first call with stored version < 3
//   - Sets SettingsSchemaVersion=3
//   - Is idempotent (safe to call multiple times)
//   - Handles fresh-install state (no SettingsSchemaVersion key present)
//
// Uses the direct-construction path (AppSettings(filePath)) so each test
// operates on an isolated in-memory store — no singleton, no disk I/O.

#include <QtTest/QtTest>
#include "core/AppSettings.h"

#include <QTemporaryDir>

using namespace NereusSDR;

class TstSettingsMigrationV03 : public QObject {
    Q_OBJECT

private slots:
    // ─────────────────────────────────────────────────────────────────────
    // Retired keys are removed and version is stamped on migration
    // ─────────────────────────────────────────────────────────────────────
    void retired_keys_removed_on_migration()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AppSettings s(tmp.filePath(QStringLiteral("NereusSDR.settings")));

        // Simulate a v0.2.x settings file: version "2" + all 4 retired keys
        s.setValue(QStringLiteral("SettingsSchemaVersion"), QStringLiteral("2"));
        s.setValue(QStringLiteral("DisplayAverageMode"), QStringLiteral("2"));
        s.setValue(QStringLiteral("DisplayPeakHold"), QStringLiteral("True"));
        s.setValue(QStringLiteral("DisplayPeakHoldDelayMs"), QStringLiteral("2000"));
        s.setValue(QStringLiteral("DisplayReverseWaterfallScroll"), QStringLiteral("True"));

        s.ensureSettingsAtVersion(3);

        QVERIFY2(!s.contains(QStringLiteral("DisplayAverageMode")),
                 "DisplayAverageMode should be removed after v3 migration");
        QVERIFY2(!s.contains(QStringLiteral("DisplayPeakHold")),
                 "DisplayPeakHold should be removed after v3 migration");
        QVERIFY2(!s.contains(QStringLiteral("DisplayPeakHoldDelayMs")),
                 "DisplayPeakHoldDelayMs should be removed after v3 migration");
        QVERIFY2(!s.contains(QStringLiteral("DisplayReverseWaterfallScroll")),
                 "DisplayReverseWaterfallScroll should be removed after v3 migration");
        QCOMPARE(s.value(QStringLiteral("SettingsSchemaVersion")).toString().toInt(), 3);
    }

    // ─────────────────────────────────────────────────────────────────────
    // Migration is idempotent — calling twice is safe
    // ─────────────────────────────────────────────────────────────────────
    void migration_idempotent()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AppSettings s(tmp.filePath(QStringLiteral("NereusSDR.settings")));

        s.setValue(QStringLiteral("SettingsSchemaVersion"), QStringLiteral("3"));

        s.ensureSettingsAtVersion(3);
        s.ensureSettingsAtVersion(3);

        QCOMPARE(s.value(QStringLiteral("SettingsSchemaVersion")).toString().toInt(), 3);
    }

    // ─────────────────────────────────────────────────────────────────────
    // Fresh install (no SettingsSchemaVersion key) runs migration + stamps version
    // ─────────────────────────────────────────────────────────────────────
    void fresh_install_writes_version()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AppSettings s(tmp.filePath(QStringLiteral("NereusSDR.settings")));

        // No SettingsSchemaVersion key — simulates brand new installation
        QVERIFY(!s.contains(QStringLiteral("SettingsSchemaVersion")));

        s.ensureSettingsAtVersion(3);

        QCOMPARE(s.value(QStringLiteral("SettingsSchemaVersion")).toString().toInt(), 3);
    }

    // ─────────────────────────────────────────────────────────────────────
    // Already-at-version path: no keys removed, version unchanged
    // ─────────────────────────────────────────────────────────────────────
    void no_changes_when_already_at_version()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AppSettings s(tmp.filePath(QStringLiteral("NereusSDR.settings")));

        // Simulate a post-v0.3.0 launch: version=3, new keys present
        s.setValue(QStringLiteral("SettingsSchemaVersion"), QStringLiteral("3"));
        s.setValue(QStringLiteral("DisplaySpectrumDetector"), QStringLiteral("1"));
        s.setValue(QStringLiteral("DisplaySpectrumAveraging"), QStringLiteral("2"));

        s.ensureSettingsAtVersion(3);

        // New keys must survive the no-op pass
        QVERIFY(s.contains(QStringLiteral("DisplaySpectrumDetector")));
        QVERIFY(s.contains(QStringLiteral("DisplaySpectrumAveraging")));
        QCOMPARE(s.value(QStringLiteral("SettingsSchemaVersion")).toString().toInt(), 3);
    }

    // ─────────────────────────────────────────────────────────────────────
    // Stored version = 0 (explicit zero) triggers migration
    // ─────────────────────────────────────────────────────────────────────
    void version_zero_triggers_migration()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AppSettings s(tmp.filePath(QStringLiteral("NereusSDR.settings")));

        s.setValue(QStringLiteral("SettingsSchemaVersion"), QStringLiteral("0"));
        s.setValue(QStringLiteral("DisplayAverageMode"), QStringLiteral("3"));

        s.ensureSettingsAtVersion(3);

        QVERIFY2(!s.contains(QStringLiteral("DisplayAverageMode")),
                 "DisplayAverageMode should be removed when upgrading from v0");
        QCOMPARE(s.value(QStringLiteral("SettingsSchemaVersion")).toString().toInt(), 3);
    }
};

QTEST_APPLESS_MAIN(TstSettingsMigrationV03)
#include "tst_settings_migration_v0_3_0.moc"
