// no-port-check: NereusSDR-original. Common power source behavior.
// Modification history (NereusSDR):
// 2026-10-12 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
#include <QtTest>
#include <QSignalSpy>
#include "models/MainPowerMeter.h"
#include "models/AmplifierModel.h"
#include "models/RfKitModel.h"
#include "models/RadioModel.h"
#include "core/RadioStatus.h"

using namespace NereusSDR;

class TstMainPowerMeter : public QObject {
    Q_OBJECT
private slots:
    void typedSignalsResolveAcrossTheSharedCoreBoundary()
    {
        QVERIFY(QMetaMethod::fromSignal(&MainPowerMeter::sourceChanged).isValid());
        QVERIFY(QMetaMethod::fromSignal(&MainPowerMeter::readingChanged).isValid());
        QVERIFY(QMetaMethod::fromSignal(&MainPowerMeter::scaleChanged).isValid());
        QVERIFY(QMetaMethod::fromSignal(&MainPowerMeter::driveChanged).isValid());
    }

    void sourcePriorityAndDisconnectKeepRadioDrive()
    {
        AmplifierModel pgxl;
        RfKitModel rfkit;
        RadioStatus radio;
        MainPowerMeter meter(&pgxl, &rfkit, &radio);
        meter.setHardwareModel(HPSDRModel::ANAN100);
        radio.setPowerReadings(42.0, 0.0, 1.12);
        QCOMPARE(meter.source(), MainPowerMeter::Source::Radio);
        QCOMPARE(meter.forwardW(), 42.0);
        QCOMPARE(meter.visualLimitW(), 100);
        QCOMPARE(meter.scaleTopW(), 120);
        QVERIFY(rfkit.applyStationValue("forwardPowerW", 900.0));
        QVERIFY(rfkit.applyStationValue("swr", 1.4));
        QVERIFY(rfkit.applyStationValue("operate", true));
        QVERIFY(!meter.onAmplifier());
        QVERIFY(rfkit.applyStationValue("present", true));
        QCOMPARE(meter.source(), MainPowerMeter::Source::RfKit);
        QCOMPARE(meter.forwardW(), 900.0);
        QCOMPARE(meter.swr(), 1.4);
        QCOMPARE(meter.driveW(), 42.0);
        QCOMPARE(meter.scaleTopW(), 2000);
        QCOMPARE(meter.visualLimitW(), 1500);
        QVERIFY(pgxl.applyStationValue("forwardPowerW", 1236.0));
        QVERIFY(pgxl.applyStationValue("swr", 1.12));
        QVERIFY(pgxl.applyStationValue("present", true));
        QCOMPARE(meter.source(), MainPowerMeter::Source::RfKit);
        QVERIFY(pgxl.applyStationValue("operate", true));
        QCOMPARE(meter.source(), MainPowerMeter::Source::PowerGenius);
        QCOMPARE(meter.forwardW(), 1236.0);
        QVERIFY(pgxl.applyStationValue("forwardPowerW", 2300.0));
        QCOMPARE(meter.forwardW(), 2300.0);  // legal marker is never an output cap
        QVERIFY(pgxl.applyStationValue("present", false));
        QVERIFY(pgxl.operate());
        QCOMPARE(meter.source(), MainPowerMeter::Source::RfKit);
        QVERIFY(rfkit.applyStationValue("operate", false));
        QCOMPARE(meter.source(), MainPowerMeter::Source::Radio);
        QCOMPARE(meter.forwardW(), 42.0);
        QCOMPARE(meter.swr(), 1.12);
        QCOMPARE(meter.scaleTopW(), 120);
    }

    void repeatedAndUnselectedReadingsEmitNothing()
    {
        AmplifierModel pgxl;
        RfKitModel rfkit;
        RadioStatus radio;
        MainPowerMeter meter(&pgxl, &rfkit, &radio);
        pgxl.applyStationValue("present", true);
        pgxl.applyStationValue("operate", true);
        pgxl.applyStationValue("forwardPowerW", 1236.0);
        QSignalSpy readings(&meter, &MainPowerMeter::readingChanged);
        QSignalSpy sources(&meter, &MainPowerMeter::sourceChanged);
        QSignalSpy scales(&meter, &MainPowerMeter::scaleChanged);
        QSignalSpy drive(&meter, &MainPowerMeter::driveChanged);
        QVERIFY(readings.isValid());
        QVERIFY(sources.isValid());
        QVERIFY(scales.isValid());
        QVERIFY(drive.isValid());
        pgxl.applyStationValue("forwardPowerW", 1236.0);
        rfkit.applyStationValue("forwardPowerW", 800.0);
        radio.setPowerReadings(42.0, 0.0, 1.2);
        QCOMPARE(readings.count(), 0);
        QCOMPARE(sources.count(), 0);
        QCOMPARE(scales.count(), 0);
        QCOMPARE(drive.count(), 1);
        pgxl.applyStationValue("forwardPowerW", 1250.0);
        QCOMPARE(readings.count(), 1);
        QCOMPARE(meter.forwardW(), 1250.0);
        pgxl.applyStationValue("operate", false);
        QCOMPARE(sources.count(), 1);
        QCOMPARE(scales.count(), 1);
        QCOMPARE(readings.count(), 2);
    }

    void radioScaleTracksModelWhileAmplifying()
    {
        AmplifierModel pgxl;
        RfKitModel rfkit;
        RadioStatus radio;
        MainPowerMeter meter(&pgxl, &rfkit, &radio);
        meter.setHardwareModel(HPSDRModel::HERMESLITE);
        QCOMPARE(meter.visualLimitW(), 5);
        QCOMPARE(meter.scaleTopW(), 6);
        pgxl.applyStationValue("present", true);
        pgxl.applyStationValue("operate", true);
        meter.setHardwareModel(HPSDRModel::ANAN8000D);
        QCOMPARE(meter.scaleTopW(), 2000);
        pgxl.applyStationValue("operate", false);
        QCOMPARE(meter.visualLimitW(), 200);
        QCOMPARE(meter.scaleTopW(), 240);
    }

    void remoteRadioOwnsTheSameRule()
    {
        RadioModel radio(RadioModel::Role::Remote);
        MainPowerMeter* meter = radio.mainPowerMeter();
        QVERIFY(meter != nullptr);
        radio.radioStatus().setPowerReadings(42.0, 0.0, 1.12);
        radio.amplifierModel()->applyStationValue("forwardPowerW", 1236.0);
        radio.amplifierModel()->applyStationValue("present", true);
        radio.amplifierModel()->applyStationValue("operate", true);
        QVERIFY(meter->onAmplifier());
        QCOMPARE(meter->forwardW(), 1236.0);
        QCOMPARE(meter->driveW(), 42.0);
    }
};
QTEST_MAIN(TstMainPowerMeter)
#include "tst_main_power_meter.moc"
