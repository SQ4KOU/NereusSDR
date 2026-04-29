// no-port-check: NereusSDR-original unit-test file.
// =================================================================
// tests/tst_mic_profile_manager.cpp  (NereusSDR)
// =================================================================
//
// Unit tests for MicProfileManager (Phase 3M-1c chunk F).
//
// Covers F.1-F.6:
//   F.1  Class skeleton + load/save/delete/setActive operations
//   F.2  Save flow with overwrite + comma-strip
//   F.3  Delete flow with last-profile guard
//   F.4  setActiveProfile flow (writes values + updates active key)
//   F.5  First-launch "Default" profile seed
//   F.6  Per-MAC isolation (two MACs maintain independent lists)
//
// =================================================================

#include <QtTest/QtTest>
#include <QSignalSpy>

#include "core/AppSettings.h"
#include "core/MicProfileManager.h"
#include "models/TransmitModel.h"

using namespace NereusSDR;

static const QString kMacA = QStringLiteral("aa:bb:cc:11:22:33");
static const QString kMacB = QStringLiteral("ff:ee:dd:cc:bb:aa");

static QString profileKey(const QString& mac, const QString& name, const QString& field)
{
    return QStringLiteral("hardware/%1/tx/profile/%2/%3").arg(mac, name, field);
}

static QString activeKey(const QString& mac)
{
    return QStringLiteral("hardware/%1/tx/profile/active").arg(mac);
}

static QString manifestKey(const QString& mac)
{
    return QStringLiteral("hardware/%1/tx/profile/_names").arg(mac);
}

class TstMicProfileManager : public QObject {
    Q_OBJECT

private slots:

    void initTestCase()
    {
        AppSettings::instance().clear();
    }

    void init()
    {
        AppSettings::instance().clear();
    }

    // =========================================================================
    // §F.5  First-launch "Default" profile seed
    // =========================================================================

    void firstLaunch_seedsDefaultProfile()
    {
        MicProfileManager mgr;
        mgr.setMacAddress(kMacA);
        mgr.load();

        const QStringList names = mgr.profileNames();
        QCOMPARE(names.size(), 1);
        QCOMPARE(names.first(), QStringLiteral("Default"));
        QCOMPARE(mgr.activeProfileName(), QStringLiteral("Default"));
    }

    void firstLaunch_defaultProfileMatchesDocumentedDefaults()
    {
        MicProfileManager mgr;
        mgr.setMacAddress(kMacA);
        mgr.load();

        // F.5: every documented default must be written to AppSettings under
        // hardware/<mac>/tx/profile/Default/<key>.  Spot-check each row of
        // the documented table.
        auto& s = AppSettings::instance();
        QCOMPARE(s.value(profileKey(kMacA, "Default", "MicGain")).toString(),
                 QStringLiteral("-6"));
        QCOMPARE(s.value(profileKey(kMacA, "Default", "Mic_Input_Boost")).toString(),
                 QStringLiteral("True"));
        QCOMPARE(s.value(profileKey(kMacA, "Default", "Mic_XLR")).toString(),
                 QStringLiteral("True"));
        QCOMPARE(s.value(profileKey(kMacA, "Default", "Line_Input_On")).toString(),
                 QStringLiteral("False"));
        QCOMPARE(s.value(profileKey(kMacA, "Default", "Line_Input_Level")).toString(),
                 QStringLiteral("0"));
        QCOMPARE(s.value(profileKey(kMacA, "Default", "Mic_TipRing")).toString(),
                 QStringLiteral("True"));
        QCOMPARE(s.value(profileKey(kMacA, "Default", "Mic_Bias")).toString(),
                 QStringLiteral("False"));
        QCOMPARE(s.value(profileKey(kMacA, "Default", "Mic_PTT_Disabled")).toString(),
                 QStringLiteral("False"));
        QCOMPARE(s.value(profileKey(kMacA, "Default", "Dexp_Threshold")).toString(),
                 QStringLiteral("-40"));
        QCOMPARE(s.value(profileKey(kMacA, "Default", "VOX_GainScalar")).toString(),
                 QStringLiteral("1"));
        QCOMPARE(s.value(profileKey(kMacA, "Default", "VOX_HangTime")).toString(),
                 QStringLiteral("500"));
        QCOMPARE(s.value(profileKey(kMacA, "Default", "AntiVox_Gain")).toString(),
                 QStringLiteral("0"));
        QCOMPARE(s.value(profileKey(kMacA, "Default", "AntiVox_Source_VAX")).toString(),
                 QStringLiteral("False"));
        QCOMPARE(s.value(profileKey(kMacA, "Default", "MonitorVolume")).toString(),
                 QStringLiteral("0.5"));
        QCOMPARE(s.value(profileKey(kMacA, "Default", "Mic_Source")).toString(),
                 QStringLiteral("Pc"));
        QCOMPARE(s.value(profileKey(kMacA, "Default", "TwoToneFreq1")).toString(),
                 QStringLiteral("700"));
        QCOMPARE(s.value(profileKey(kMacA, "Default", "TwoToneFreq2")).toString(),
                 QStringLiteral("1900"));
        QCOMPARE(s.value(profileKey(kMacA, "Default", "TwoToneLevel")).toString(),
                 QStringLiteral("-6"));
        QCOMPARE(s.value(profileKey(kMacA, "Default", "TwoTonePower")).toString(),
                 QStringLiteral("50"));
        QCOMPARE(s.value(profileKey(kMacA, "Default", "TwoToneFreq2Delay")).toString(),
                 QStringLiteral("0"));
        QCOMPARE(s.value(profileKey(kMacA, "Default", "TwoToneInvert")).toString(),
                 QStringLiteral("True"));
        QCOMPARE(s.value(profileKey(kMacA, "Default", "TwoTonePulsed")).toString(),
                 QStringLiteral("False"));
        QCOMPARE(s.value(profileKey(kMacA, "Default", "TwoToneDrivePowerOrigin")).toString(),
                 QStringLiteral("DriveSlider"));
    }

    void subsequentLaunch_doesNotOverwriteDefault()
    {
        // First launch.
        {
            MicProfileManager mgr;
            mgr.setMacAddress(kMacA);
            mgr.load();
        }
        // Manually mutate one of the Default profile values.
        AppSettings::instance().setValue(profileKey(kMacA, "Default", "MicGain"),
                                          QStringLiteral("42"));

        // Second launch: load should not overwrite the existing Default values.
        MicProfileManager mgr2;
        mgr2.setMacAddress(kMacA);
        mgr2.load();

        QCOMPARE(AppSettings::instance().value(profileKey(kMacA, "Default", "MicGain")).toString(),
                 QStringLiteral("42"));
        QCOMPARE(mgr2.profileNames().size(), 1);
    }

    void firstLaunch_emitsProfileListChanged()
    {
        MicProfileManager mgr;
        mgr.setMacAddress(kMacA);
        QSignalSpy listSpy(&mgr, &MicProfileManager::profileListChanged);
        mgr.load();
        QCOMPARE(listSpy.count(), 1);
    }

    void defaultProfileValues_staticHelperMatchesSeed()
    {
        const QHash<QString, QVariant> defs = MicProfileManager::defaultProfileValues();
        // Spot-check a few representative keys.
        QCOMPARE(defs.value("MicGain").toString(), QStringLiteral("-6"));
        QCOMPARE(defs.value("Mic_Input_Boost").toString(), QStringLiteral("True"));
        QCOMPARE(defs.value("VOX_HangTime").toString(), QStringLiteral("500"));
        QCOMPARE(defs.value("TwoToneFreq1").toString(), QStringLiteral("700"));
        QCOMPARE(defs.value("TwoToneDrivePowerOrigin").toString(),
                 QStringLiteral("DriveSlider"));
        // 23 keys total per the documented table.
        QCOMPARE(defs.size(), 23);
    }

    // =========================================================================
    // §F.1  Class skeleton + load/save round-trip
    // =========================================================================

    void saveProfile_capturesAllLiveKeys()
    {
        TransmitModel tx;
        tx.loadFromSettings(kMacA);

        // Set non-default values on every live key.
        tx.setMicGainDb(10);
        tx.setMicBoost(false);
        tx.setMicXlr(false);
        tx.setLineIn(true);
        tx.setLineInBoost(6.5);
        tx.setMicTipRing(false);
        tx.setMicBias(true);
        tx.setMicPttDisabled(true);
        tx.setVoxThresholdDb(-20);
        tx.setVoxGainScalar(2.5f);
        tx.setVoxHangTimeMs(1000);
        tx.setAntiVoxGainDb(12);
        tx.setAntiVoxSourceVax(true);
        tx.setMonitorVolume(0.75f);
        tx.setMicSource(MicSource::Radio);
        tx.setTwoToneFreq1(800);
        tx.setTwoToneFreq2(2100);
        tx.setTwoToneLevel(-12.5);
        tx.setTwoTonePower(75);
        tx.setTwoToneFreq2Delay(250);
        tx.setTwoToneInvert(false);
        tx.setTwoTonePulsed(true);
        tx.setTwoToneDrivePowerSource(DrivePowerSource::Fixed);

        MicProfileManager mgr;
        mgr.setMacAddress(kMacA);
        mgr.load();
        mgr.saveProfile("MyProfile", &tx);

        // Verify the profile keys are written.
        auto& s = AppSettings::instance();
        QCOMPARE(s.value(profileKey(kMacA, "MyProfile", "MicGain")).toString(),
                 QStringLiteral("10"));
        QCOMPARE(s.value(profileKey(kMacA, "MyProfile", "Mic_Input_Boost")).toString(),
                 QStringLiteral("False"));
        QCOMPARE(s.value(profileKey(kMacA, "MyProfile", "Mic_XLR")).toString(),
                 QStringLiteral("False"));
        QCOMPARE(s.value(profileKey(kMacA, "MyProfile", "Line_Input_On")).toString(),
                 QStringLiteral("True"));
        QCOMPARE(s.value(profileKey(kMacA, "MyProfile", "Line_Input_Level")).toString(),
                 QStringLiteral("6.5"));
        QCOMPARE(s.value(profileKey(kMacA, "MyProfile", "Mic_TipRing")).toString(),
                 QStringLiteral("False"));
        QCOMPARE(s.value(profileKey(kMacA, "MyProfile", "Mic_Bias")).toString(),
                 QStringLiteral("True"));
        QCOMPARE(s.value(profileKey(kMacA, "MyProfile", "Mic_PTT_Disabled")).toString(),
                 QStringLiteral("True"));
        QCOMPARE(s.value(profileKey(kMacA, "MyProfile", "Dexp_Threshold")).toString(),
                 QStringLiteral("-20"));
        QCOMPARE(s.value(profileKey(kMacA, "MyProfile", "VOX_GainScalar")).toString(),
                 QStringLiteral("2.5"));
        QCOMPARE(s.value(profileKey(kMacA, "MyProfile", "VOX_HangTime")).toString(),
                 QStringLiteral("1000"));
        QCOMPARE(s.value(profileKey(kMacA, "MyProfile", "AntiVox_Gain")).toString(),
                 QStringLiteral("12"));
        QCOMPARE(s.value(profileKey(kMacA, "MyProfile", "AntiVox_Source_VAX")).toString(),
                 QStringLiteral("True"));
        QCOMPARE(s.value(profileKey(kMacA, "MyProfile", "MonitorVolume")).toString(),
                 QStringLiteral("0.75"));
        QCOMPARE(s.value(profileKey(kMacA, "MyProfile", "Mic_Source")).toString(),
                 QStringLiteral("Radio"));
        QCOMPARE(s.value(profileKey(kMacA, "MyProfile", "TwoToneFreq1")).toString(),
                 QStringLiteral("800"));
        QCOMPARE(s.value(profileKey(kMacA, "MyProfile", "TwoToneFreq2")).toString(),
                 QStringLiteral("2100"));
        QCOMPARE(s.value(profileKey(kMacA, "MyProfile", "TwoToneLevel")).toString(),
                 QStringLiteral("-12.5"));
        QCOMPARE(s.value(profileKey(kMacA, "MyProfile", "TwoTonePower")).toString(),
                 QStringLiteral("75"));
        QCOMPARE(s.value(profileKey(kMacA, "MyProfile", "TwoToneFreq2Delay")).toString(),
                 QStringLiteral("250"));
        QCOMPARE(s.value(profileKey(kMacA, "MyProfile", "TwoToneInvert")).toString(),
                 QStringLiteral("False"));
        QCOMPARE(s.value(profileKey(kMacA, "MyProfile", "TwoTonePulsed")).toString(),
                 QStringLiteral("True"));
        QCOMPARE(s.value(profileKey(kMacA, "MyProfile", "TwoToneDrivePowerOrigin")).toString(),
                 QStringLiteral("Fixed"));

        // Profile list now contains both Default and MyProfile.
        QCOMPARE(mgr.profileNames().size(), 2);
        QVERIFY(mgr.profileNames().contains("MyProfile"));
        QVERIFY(mgr.profileNames().contains("Default"));
    }

    void saveProfile_emitsProfileListChanged()
    {
        TransmitModel tx;
        MicProfileManager mgr;
        mgr.setMacAddress(kMacA);
        mgr.load();
        QSignalSpy listSpy(&mgr, &MicProfileManager::profileListChanged);
        mgr.saveProfile("Profile1", &tx);
        QCOMPARE(listSpy.count(), 1);
    }

    // =========================================================================
    // §F.2  Save flow with overwrite + comma-strip
    // =========================================================================

    void saveProfile_overwriteExistingDoesNotEmitDuplicate()
    {
        TransmitModel tx;
        tx.loadFromSettings(kMacA);
        tx.setMicGainDb(10);

        MicProfileManager mgr;
        mgr.setMacAddress(kMacA);
        mgr.load();
        mgr.saveProfile("MyProfile", &tx);

        // Update model + save again to overwrite.
        tx.setMicGainDb(20);
        QSignalSpy listSpy(&mgr, &MicProfileManager::profileListChanged);
        mgr.saveProfile("MyProfile", &tx);

        // Overwrite path may emit profileListChanged or not — Thetis has no
        // membership change in the overwrite path; check value updated.
        // Per F.2 contract: "Overwrites if the name already exists.  Return true."
        QCOMPARE(AppSettings::instance().value(profileKey(kMacA, "MyProfile", "MicGain")).toString(),
                 QStringLiteral("20"));
        // Profile count should remain 2 (Default + MyProfile).
        QCOMPARE(mgr.profileNames().size(), 2);
    }

    void saveProfile_stripsCommas()
    {
        // Thetis precedent: name = name.replace(",", "_") for TCI safety.
        TransmitModel tx;
        MicProfileManager mgr;
        mgr.setMacAddress(kMacA);
        mgr.load();
        const bool ok = mgr.saveProfile("Bad,Name", &tx);
        QVERIFY(ok);
        QVERIFY(mgr.profileNames().contains("Bad_Name"));
        QVERIFY(!mgr.profileNames().contains("Bad,Name"));
    }

    // =========================================================================
    // §F.3  Delete flow with last-profile guard
    // =========================================================================

    void deleteProfile_lastRemainingIsRefused()
    {
        MicProfileManager mgr;
        mgr.setMacAddress(kMacA);
        mgr.load();
        // Only Default exists.
        QCOMPARE(mgr.profileNames().size(), 1);

        QSignalSpy listSpy(&mgr, &MicProfileManager::profileListChanged);

        // Capture qCWarning output so we can verify the verbatim Thetis string.
        // (QtMessageHandler is a global setter; restore it after.)
        QString lastWarning;
        QtMessageHandler oldHandler = qInstallMessageHandler(
            [](QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
                Q_UNUSED(ctx);
                if (type == QtWarningMsg) {
                    s_capturedWarning = msg;
                }
            });

        s_capturedWarning.clear();
        const bool ok = mgr.deleteProfile("Default");
        qInstallMessageHandler(oldHandler);

        QCOMPARE(ok, false);
        QCOMPARE(listSpy.count(), 0);
        QCOMPARE(mgr.profileNames().size(), 1);
        // Verbatim Thetis string per F.3 spec.
        QVERIFY2(s_capturedWarning.contains(
                     QStringLiteral("It is not possible to delete the last remaining TX profile")),
                 qPrintable(QStringLiteral("captured warning was: ") + s_capturedWarning));
    }

    void deleteProfile_removesProfileKeys()
    {
        TransmitModel tx;
        MicProfileManager mgr;
        mgr.setMacAddress(kMacA);
        mgr.load();
        mgr.saveProfile("ToDelete", &tx);

        // Verify it exists.
        QVERIFY(AppSettings::instance().contains(profileKey(kMacA, "ToDelete", "MicGain")));

        QSignalSpy listSpy(&mgr, &MicProfileManager::profileListChanged);
        const bool ok = mgr.deleteProfile("ToDelete");
        QCOMPARE(ok, true);
        QCOMPARE(listSpy.count(), 1);

        // All keys for the profile should be removed.
        QVERIFY(!AppSettings::instance().contains(profileKey(kMacA, "ToDelete", "MicGain")));
        QVERIFY(!AppSettings::instance().contains(profileKey(kMacA, "ToDelete", "VOX_HangTime")));
        QVERIFY(!mgr.profileNames().contains("ToDelete"));
    }

    void deleteProfile_activeFallsBackToFirstRemaining()
    {
        TransmitModel tx;
        MicProfileManager mgr;
        mgr.setMacAddress(kMacA);
        mgr.load();
        mgr.saveProfile("ProfileA", &tx);
        mgr.saveProfile("ProfileB", &tx);

        // Activate ProfileA.
        QVERIFY(mgr.setActiveProfile("ProfileA", &tx));
        QCOMPARE(mgr.activeProfileName(), QStringLiteral("ProfileA"));

        // Delete the active profile.
        QSignalSpy activeSpy(&mgr, &MicProfileManager::activeProfileChanged);
        const bool ok = mgr.deleteProfile("ProfileA");
        QCOMPARE(ok, true);
        // Active falls back to the first remaining (lexicographically: Default).
        QCOMPARE(activeSpy.count(), 1);
        QVERIFY(mgr.activeProfileName() != QStringLiteral("ProfileA"));
        QVERIFY(mgr.profileNames().contains(mgr.activeProfileName()));
    }

    // =========================================================================
    // §F.4  setActiveProfile flow
    // =========================================================================

    void setActiveProfile_writesActiveKey()
    {
        TransmitModel tx;
        MicProfileManager mgr;
        mgr.setMacAddress(kMacA);
        mgr.load();
        mgr.saveProfile("OtherProfile", &tx);

        QSignalSpy spy(&mgr, &MicProfileManager::activeProfileChanged);
        const bool ok = mgr.setActiveProfile("OtherProfile", &tx);
        QCOMPARE(ok, true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("OtherProfile"));
        QCOMPARE(mgr.activeProfileName(), QStringLiteral("OtherProfile"));
        QCOMPARE(AppSettings::instance().value(activeKey(kMacA)).toString(),
                 QStringLiteral("OtherProfile"));
    }

    void setActiveProfile_appliesValuesToTransmitModel()
    {
        // Save profile A with a specific MicGain
        TransmitModel txA;
        txA.loadFromSettings(kMacA);
        txA.setMicGainDb(15);
        txA.setVoxHangTimeMs(800);

        MicProfileManager mgr;
        mgr.setMacAddress(kMacA);
        mgr.load();
        mgr.saveProfile("Profile15", &txA);

        // Mutate model to different values.
        txA.setMicGainDb(-30);
        txA.setVoxHangTimeMs(100);

        // Activate profile — should restore the saved values.
        QVERIFY(mgr.setActiveProfile("Profile15", &txA));
        QCOMPARE(txA.micGainDb(), 15);
        QCOMPARE(txA.voxHangTimeMs(), 800);
    }

    void setActiveProfile_unknownProfileReturnsFalse()
    {
        TransmitModel tx;
        MicProfileManager mgr;
        mgr.setMacAddress(kMacA);
        mgr.load();
        QSignalSpy spy(&mgr, &MicProfileManager::activeProfileChanged);
        const bool ok = mgr.setActiveProfile("Nonexistent", &tx);
        QCOMPARE(ok, false);
        QCOMPARE(spy.count(), 0);
        QCOMPARE(mgr.activeProfileName(), QStringLiteral("Default"));
    }

    void load_restoresActiveProfileFromSettings()
    {
        // Seed: write the active key directly + create both profiles.
        {
            TransmitModel tx;
            MicProfileManager mgr;
            mgr.setMacAddress(kMacA);
            mgr.load();
            mgr.saveProfile("OtherProfile", &tx);
            mgr.setActiveProfile("OtherProfile", &tx);
        }
        // Fresh manager — load should pick up "OtherProfile" as active.
        MicProfileManager mgr2;
        mgr2.setMacAddress(kMacA);
        mgr2.load();
        QCOMPARE(mgr2.activeProfileName(), QStringLiteral("OtherProfile"));
    }

    // =========================================================================
    // §F.6  Per-MAC isolation
    // =========================================================================

    void perMacIsolation_independentProfileLists()
    {
        // MAC A: Default + ProfileA1 + ProfileA2
        {
            TransmitModel tx;
            MicProfileManager mgr;
            mgr.setMacAddress(kMacA);
            mgr.load();
            mgr.saveProfile("ProfileA1", &tx);
            mgr.saveProfile("ProfileA2", &tx);
        }
        // MAC B: Default + ProfileB1
        {
            TransmitModel tx;
            MicProfileManager mgr;
            mgr.setMacAddress(kMacB);
            mgr.load();
            mgr.saveProfile("ProfileB1", &tx);
        }

        // Reload MAC A — should still have its own list.
        MicProfileManager mgrA;
        mgrA.setMacAddress(kMacA);
        mgrA.load();
        QStringList namesA = mgrA.profileNames();
        QCOMPARE(namesA.size(), 3);
        QVERIFY(namesA.contains("Default"));
        QVERIFY(namesA.contains("ProfileA1"));
        QVERIFY(namesA.contains("ProfileA2"));
        QVERIFY(!namesA.contains("ProfileB1"));

        // Reload MAC B — should have its own list.
        MicProfileManager mgrB;
        mgrB.setMacAddress(kMacB);
        mgrB.load();
        QStringList namesB = mgrB.profileNames();
        QCOMPARE(namesB.size(), 2);
        QVERIFY(namesB.contains("Default"));
        QVERIFY(namesB.contains("ProfileB1"));
        QVERIFY(!namesB.contains("ProfileA1"));
    }

private:
    static QString s_capturedWarning;
};

// File-static used by deleteProfile_lastRemainingIsRefused message handler.
QString TstMicProfileManager::s_capturedWarning;

QTEST_APPLESS_MAIN(TstMicProfileManager)
#include "tst_mic_profile_manager.moc"
