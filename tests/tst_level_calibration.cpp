// =================================================================
// tests/tst_level_calibration.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original test file. The Thetis citations below
// document the upstream behaviour exercised; no C# is translated here.
//
// Level calibration storage, reset and TCI report:
//   - RX1_MeterCalOffsetDb stays the meter offset, with the Thetis
//     per-model default (clsHardwareSpecific.cs:408-423 [v2.10.3.15]);
//     a value saved before this change reads back unchanged.
//   - RX1_DisplayCalOffsetDb is the display offset, with the Thetis
//     per-model default (clsHardwareSpecific.cs:424-440 [v2.10.3.15]).
//     It feeds only TCI calibration_ex (TCIServer.cs:1160-1176
//     [v2.10.3.15]) and never moves the panadapter, which follows the
//     meter offset (console.cs:12305-12311 [v2.10.3.15]).
//   - Reset (console.cs:46868-46886 [v2.10.3.15], ResetLevelCalibration)
//     returns the meter and display offsets to the model's defaults and
//     touches no other calibration.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-29 - Written by J.J. Boyd (KG4VCF), with AI-assisted
//                 implementation via Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>
#include <QSignalSpy>

#include "core/AppSettings.h"
#include "core/ConnectionState.h"
#include "core/HpsdrModel.h"
#include "core/RadioDiscovery.h"
#include "core/TciProtocol.h"
#include "models/RadioModel.h"
#ifdef HAVE_WEBSOCKETS
#include "core/TciServer.h"
#endif

using namespace NereusSDR;

namespace {

const QString kMac = QStringLiteral("AA:BB:CC:DD:1C:01");
const QString kMeterKey = QStringLiteral("RX1_MeterCalOffsetDb");
const QString kDisplayKey = QStringLiteral("RX1_DisplayCalOffsetDb");

void setUpLocal(RadioModel& model, HPSDRModel radio)
{
    model.setHpsdrModelForTest(radio);
    RadioInfo info;
    info.macAddress = kMac;
    info.boardType = boardForModel(radio);
    model.setLastRadioInfoForTest(info);
    model.setConnectionStateForTest(ConnectionState::Connected);
}

} // namespace

class TstLevelCalibration : public QObject {
    Q_OBJECT

private slots:
    void init() { AppSettings::instance().clear(); }
    void cleanup() { AppSettings::instance().clear(); }

    // A meter offset saved before this change reads back as it was, and the
    // receive offset built on it is unchanged.
    void meterOffset_savedValueUnchanged()
    {
        AppSettings::instance().setValue(kMeterKey, QStringLiteral("-3.000000"));
        RadioModel model;
        setUpLocal(model, HPSDRModel::ANAN_G2);
        QCOMPARE(model.rxMeterCalOffsetDb(), -3.0);
        QCOMPARE(model.calibrationMeter(0), -3.0);
        QCOMPARE(model.calibrationMeter(1), -3.0);
        QCOMPARE(model.rxMeterOffsetDb(),
                 model.rxPreampOffsetDb() - 3.0 + model.rx6mGainOffsetDb());
        QCOMPARE(AppSettings::instance().value(kMeterKey).toString(),
                 QStringLiteral("-3.000000"));
    }

    // With nothing saved, each model reads its Thetis default.
    void meterOffset_absentUsesModelDefault()
    {
        for (HPSDRModel radio : { HPSDRModel::ANAN_G2, HPSDRModel::ANAN7000D,
                                  HPSDRModel::HERMES, HPSDRModel::ANAN_G2E }) {
            RadioModel model;
            setUpLocal(model, radio);
            QCOMPARE(model.rxMeterCalOffsetDb(),
                     static_cast<double>(rxMeterCalOffsetDefaultFor(radio)));
            QCOMPARE(model.calibrationMeter(0),
                     static_cast<double>(rxMeterCalOffsetDefaultFor(radio)));
            QVERIFY(!AppSettings::instance().contains(kMeterKey));
        }
    }

    void displayOffset_absentUsesModelDefault()
    {
        for (HPSDRModel radio : { HPSDRModel::ANAN_G2, HPSDRModel::ANAN7000D,
                                  HPSDRModel::HERMES, HPSDRModel::ANAN_G2E }) {
            RadioModel model;
            setUpLocal(model, radio);
            QCOMPARE(model.rxDisplayCalOffsetDb(),
                     static_cast<double>(rxDisplayCalOffsetDefaultFor(radio)));
            QCOMPARE(model.calibrationDisplay(0),
                     static_cast<double>(rxDisplayCalOffsetDefaultFor(radio)));
            QCOMPARE(model.calibrationDisplay(1),
                     static_cast<double>(rxDisplayCalOffsetDefaultFor(radio)));
        }
    }

    // The display offset reaches TCI only: the meter offset and the
    // panadapter's calibration stay where they were.
    void displayOffset_neverMovesPanadapter()
    {
        RadioModel model;
        setUpLocal(model, HPSDRModel::ANAN_G2);
        const double meterBefore = model.rxMeterOffsetDb();
        const double keyedBefore = model.keyedDisplayOffsetDb(true);
        QSignalSpy meterSpy(&model, &RadioModel::rxMeterOffsetChanged);
        QSignalSpy calSpy(&model, &RadioModel::levelCalibrationChanged);

        AppSettings::instance().setValue(kDisplayKey, QStringLiteral("7.500000"));
        QVERIFY(model.applyLevelCalibrationSetting(kDisplayKey));

        QCOMPARE(model.calibrationDisplay(0), 7.5);
        QCOMPARE(model.rxMeterOffsetDb(), meterBefore);
        QCOMPARE(model.keyedDisplayOffsetDb(true), keyedBefore);
        QCOMPARE(meterSpy.count(), 0);
        QCOMPARE(calSpy.count(), 1);
    }

    // A meter offset change takes effect at once.
    void meterOffset_applyRefreshesMeter()
    {
        RadioModel model;
        setUpLocal(model, HPSDRModel::ANAN_G2);
        QSignalSpy meterSpy(&model, &RadioModel::rxMeterOffsetChanged);
        QSignalSpy calSpy(&model, &RadioModel::levelCalibrationChanged);
        const double preamp = model.rxPreampOffsetDb();

        AppSettings::instance().setValue(kMeterKey, QStringLiteral("-1.500000"));
        QVERIFY(model.applyLevelCalibrationSetting(kMeterKey));

        QCOMPARE(model.rxMeterOffsetDb(), preamp - 1.5 + model.rx6mGainOffsetDb());
        QCOMPARE(meterSpy.count(), 1);
        QCOMPARE(calSpy.count(), 1);
    }

    void applyLevelCalibrationSetting_ignoresOtherKeys()
    {
        RadioModel model;
        setUpLocal(model, HPSDRModel::ANAN_G2);
        QSignalSpy calSpy(&model, &RadioModel::levelCalibrationChanged);
        QVERIFY(!model.applyLevelCalibrationSetting(QStringLiteral("MultimeterDelayMs")));
        QVERIFY(!model.applyLevelCalibrationSetting(
            QStringLiteral("hardware/%1/cal/txDisplayOffset").arg(kMac)));
        QCOMPARE(calSpy.count(), 0);
    }

    // Reset returns the meter and display offsets to the model's defaults
    // and leaves every other calibration alone.
    void reset_clearsMeterAndDisplayOnly()
    {
        AppSettings& s = AppSettings::instance();
        s.setValue(kMeterKey, QStringLiteral("-3.000000"));
        s.setValue(kDisplayKey, QStringLiteral("7.500000"));
        s.setHardwareValue(kMac, QStringLiteral("cal/txDisplayOffset"), QStringLiteral("2.5"));
        s.setHardwareValue(kMac, QStringLiteral("cal/rx1_6mLna"), QStringLiteral("4.0"));

        RadioModel model;
        setUpLocal(model, HPSDRModel::ANAN7000D);
        QSignalSpy meterSpy(&model, &RadioModel::rxMeterOffsetChanged);
        QSignalSpy calSpy(&model, &RadioModel::levelCalibrationChanged);

        QVERIFY(model.levelCalibrationResetAvailable());
        QVERIFY(model.requestResetLevelCalibration().isEmpty());

        QVERIFY(!s.contains(kMeterKey));
        QVERIFY(!s.contains(kDisplayKey));
        QCOMPARE(model.calibrationMeter(0),
                 static_cast<double>(rxMeterCalOffsetDefaultFor(HPSDRModel::ANAN7000D)));
        QCOMPARE(model.calibrationDisplay(0),
                 static_cast<double>(rxDisplayCalOffsetDefaultFor(HPSDRModel::ANAN7000D)));
        QCOMPARE(s.hardwareValue(kMac, QStringLiteral("cal/txDisplayOffset")).toString(),
                 QStringLiteral("2.5"));
        QCOMPARE(s.hardwareValue(kMac, QStringLiteral("cal/rx1_6mLna")).toString(),
                 QStringLiteral("4.0"));
        QCOMPARE(meterSpy.count(), 1);
        QCOMPARE(calSpy.count(), 1);
    }

    // A remote window follows the Core's level calibration settings.
    void remote_followsStationSetting()
    {
        RadioModel remote(RadioModel::Role::Remote);
        QSignalSpy calSpy(&remote, &RadioModel::levelCalibrationChanged);
        remote.reportStationSettingChanged(kDisplayKey);
        QCOMPARE(calSpy.count(), 1);
        remote.reportStationSettingChanged(kMeterKey);
        QCOMPARE(calSpy.count(), 2);
        remote.reportStationSettingChanged(QString());
        QCOMPARE(calSpy.count(), 3);
        remote.reportStationSettingChanged(QStringLiteral("BandPlanName"));
        QCOMPARE(calSpy.count(), 3);
    }

    // With no Core, a remote window cannot reset and says why.
    void remote_resetWithoutCoreRefused()
    {
        RadioModel remote(RadioModel::Role::Remote);
        QVERIFY(!remote.levelCalibrationResetAvailable());
        QVERIFY(!remote.requestResetLevelCalibration().isEmpty());
    }

    // TCI calibration_ex carries the meter and display offsets
    // (TCIServer.cs:1160-1176 [v2.10.3.15]).
    void tci_calibrationExLine()
    {
        AppSettings::instance().setValue(kMeterKey, QStringLiteral("-3.000000"));
        AppSettings::instance().setValue(kDisplayKey, QStringLiteral("7.500000"));
        RadioModel model;
        setUpLocal(model, HPSDRModel::ANAN_G2);
        TciProtocol proto(&model);
        QCOMPARE(proto.calibrationExLineFor(0),
                 QStringLiteral("calibration_ex:0,-3.000000,7.500000,0.000000,0.000000,0.000000;"));
        QCOMPARE(proto.calibrationExLineFor(1),
                 QStringLiteral("calibration_ex:1,-3.000000,7.500000,0.000000,0.000000,0.000000;"));
    }

#ifdef HAVE_WEBSOCKETS
    // A change reaches connected apps for both receivers
    // (MeterCalOffsetChangedHandlers / DisplayOffsetChangedHandlers,
    // TCIServer.cs:6785-6786 [v2.10.3.15]).
    void tci_changeBroadcastsBothReceivers()
    {
        RadioModel model;
        setUpLocal(model, HPSDRModel::ANAN_G2);
        TciServer server(&model);
        TciProtocol* p = server.protocolForTest();
        QVERIFY(p);
        while (p->hasPendingNotification()) {
            p->takePendingNotification();
        }
        AppSettings::instance().setValue(kDisplayKey, QStringLiteral("7.500000"));
        model.applyLevelCalibrationSetting(kDisplayKey);
        QStringList out;
        while (p->hasPendingNotification()) {
            out << p->takePendingNotification();
        }
        const QString meter = QString::number(
            static_cast<double>(rxMeterCalOffsetDefaultFor(HPSDRModel::ANAN_G2)), 'f', 6);
        QCOMPARE(out, (QStringList{
            QStringLiteral("calibration_ex:0,%1,7.500000,0.000000,0.000000,0.000000;").arg(meter),
            QStringLiteral("calibration_ex:1,%1,7.500000,0.000000,0.000000,0.000000;").arg(meter)}));
    }
#endif
};

QTEST_MAIN(TstLevelCalibration)
#include "tst_level_calibration.moc"
