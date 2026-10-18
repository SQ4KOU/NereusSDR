// NereusSDR-original test — no Thetis source ported here.
// No upstream attribution required (NereusSDR impulse cache persistence test).
//
// Tests for Task 4.3: Filter Impulse Cache toggle AppSettings persistence.
//
// Design Section 4C:
//   - DspOptionsCacheImpulse defaults to "False" (off).
//   - DspOptionsCacheImpulseSaveRestore defaults to "False" (off).
//   - Both keys persist as "True"/"False" strings in AppSettings.
//   - The WdspEngine reads these flags at finishInitialization() and shutdown();
//     they take effect at engine-init time, not live.
//
// These tests verify the AppSettings round-trip only (no WDSP engine needed).

#include <QtTest/QtTest>
#include "core/AppSettings.h"

using namespace NereusSDR;

class TestDspOptionsPersistence : public QObject {
    Q_OBJECT

private slots:

    void cache_impulse_default_false()
    {
        AppSettings::instance().remove("DspOptionsCacheImpulse");
        const QString v = AppSettings::instance()
            .value("DspOptionsCacheImpulse", "False").toString();
        QCOMPARE(v, QString("False"));
    }

    void cache_impulse_round_trip_true()
    {
        AppSettings::instance().setValue("DspOptionsCacheImpulse", "True");
        const QString v = AppSettings::instance()
            .value("DspOptionsCacheImpulse", "False").toString();
        QCOMPARE(v, QString("True"));
    }

    void cache_impulse_round_trip_false()
    {
        AppSettings::instance().setValue("DspOptionsCacheImpulse", "False");
        const QString v = AppSettings::instance()
            .value("DspOptionsCacheImpulse", "True").toString();
        QCOMPARE(v, QString("False"));
    }

    void cache_save_restore_default_false()
    {
        AppSettings::instance().remove("DspOptionsCacheImpulseSaveRestore");
        const QString v = AppSettings::instance()
            .value("DspOptionsCacheImpulseSaveRestore", "False").toString();
        QCOMPARE(v, QString("False"));
    }

    void cache_save_restore_round_trip_true()
    {
        AppSettings::instance().setValue("DspOptionsCacheImpulseSaveRestore", "True");
        const QString v = AppSettings::instance()
            .value("DspOptionsCacheImpulseSaveRestore", "False").toString();
        QCOMPARE(v, QString("True"));
    }

    void cache_save_restore_round_trip_false()
    {
        AppSettings::instance().setValue("DspOptionsCacheImpulseSaveRestore", "False");
        const QString v = AppSettings::instance()
            .value("DspOptionsCacheImpulseSaveRestore", "True").toString();
        QCOMPARE(v, QString("False"));
    }

    // Both flags can be set independently (orthogonal).
    void cache_impulse_and_save_restore_are_independent()
    {
        AppSettings::instance().setValue("DspOptionsCacheImpulse",            "True");
        AppSettings::instance().setValue("DspOptionsCacheImpulseSaveRestore", "False");

        QCOMPARE(AppSettings::instance()
            .value("DspOptionsCacheImpulse",            "False").toString(), QString("True"));
        QCOMPARE(AppSettings::instance()
            .value("DspOptionsCacheImpulseSaveRestore",  "True").toString(), QString("False"));
    }

    void cleanup()
    {
        // Restore defaults after each test so subsequent tests are hermetic.
        AppSettings::instance().setValue("DspOptionsCacheImpulse",           "False");
        AppSettings::instance().setValue("DspOptionsCacheImpulseSaveRestore","False");
    }
};

QTEST_MAIN(TestDspOptionsPersistence)
#include "tst_dsp_options_persistence.moc"
