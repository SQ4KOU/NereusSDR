#include <QtTest/QtTest>
#include "core/AppSettings.h"

using namespace NereusSDR;

class TstAppSettingsVaxMigration : public QObject {
    Q_OBJECT
private slots:
    void migratesLegacyOutputDeviceKey() {
        auto& s = AppSettings::instance();
        s.clear();
        s.setValue("audio/OutputDevice", "Built-in Output");
        AppSettings::migrateVaxSchemaV1ToV2();
        QCOMPARE(s.value("audio/Speakers/DeviceName").toString(),
                 QStringLiteral("Built-in Output"));
        QVERIFY(!s.contains("audio/OutputDevice"));
        QCOMPARE(s.value("audio/FirstRunComplete", "True").toString(), "False");
    }

    void setsPlatformDefaultDriverApi() {
        auto& s = AppSettings::instance();
        s.clear();
        s.setValue("audio/OutputDevice", "Legacy Device");
        AppSettings::migrateVaxSchemaV1ToV2();
#if defined(Q_OS_WIN)
        QCOMPARE(s.value("audio/Speakers/DriverApi").toString(), QStringLiteral("WASAPI"));
#elif defined(Q_OS_MAC)
        QCOMPARE(s.value("audio/Speakers/DriverApi").toString(), QStringLiteral("CoreAudio"));
#else
        QCOMPARE(s.value("audio/Speakers/DriverApi").toString(), QStringLiteral("Pulse"));
#endif
        QCOMPARE(s.value("audio/Speakers/SampleRate").toString(), QStringLiteral("48000"));
        QCOMPARE(s.value("audio/Speakers/BitDepth").toString(), QStringLiteral("24"));
        QCOMPARE(s.value("audio/Speakers/Channels").toString(), QStringLiteral("2"));
        QCOMPARE(s.value("audio/Speakers/BufferSamples").toString(), QStringLiteral("256"));
    }

    void doesNothingIfNoLegacyKey() {
        auto& s = AppSettings::instance();
        s.clear();
        s.setValue("audio/Speakers/DeviceName", "Already set");
        AppSettings::migrateVaxSchemaV1ToV2();
        QCOMPARE(s.value("audio/Speakers/DeviceName").toString(),
                 QStringLiteral("Already set"));
        // Did not flip FirstRunComplete because we assume this is a
        // clean post-VAX install already set up.
    }

    void doesNothingIfAlreadyMigrated() {
        auto& s = AppSettings::instance();
        s.clear();
        s.setValue("audio/OutputDevice", "Legacy");
        s.setValue("audio/Speakers/DeviceName", "New");
        AppSettings::migrateVaxSchemaV1ToV2();
        // Legacy key stays (we don't clobber a present new key), new
        // key is preserved verbatim.
        QCOMPARE(s.value("audio/Speakers/DeviceName").toString(),
                 QStringLiteral("New"));
    }

    void isIdempotent() {
        auto& s = AppSettings::instance();
        s.clear();
        s.setValue("audio/OutputDevice", "Dev");
        AppSettings::migrateVaxSchemaV1ToV2();
        const auto afterFirst = s.value("audio/Speakers/DeviceName").toString();
        AppSettings::migrateVaxSchemaV1ToV2();
        const auto afterSecond = s.value("audio/Speakers/DeviceName").toString();
        QCOMPARE(afterFirst, afterSecond);
        QVERIFY(!s.contains("audio/OutputDevice"));
    }
};

QTEST_APPLESS_MAIN(TstAppSettingsVaxMigration)
#include "tst_app_settings_vax_migration.moc"
