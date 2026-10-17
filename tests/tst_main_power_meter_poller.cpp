// no-port-check: NereusSDR-original. Common power binding into container meters.
// Modification history (NereusSDR):
// 2026-10-12 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
#include <QtTest>
#include "models/MainPowerMeter.h"
#include "models/AmplifierModel.h"
#include "models/RfKitModel.h"
#include "core/RadioStatus.h"
#include "gui/meters/MeterPoller.h"
#include "gui/meters/MeterWidget.h"
#include "gui/meters/MeterItem.h"
using namespace NereusSDR;
class TstMainPowerMeterPoller : public QObject {
    Q_OBJECT
private slots:
    void amplifierReadingsAndScaleSurviveRadioUpdatesAndReplay()
    {
        AmplifierModel amp;
        RfKitModel rf;
        RadioStatus radio;
        MainPowerMeter meter(&amp, &rf, &radio);
        MeterPoller poller;
        MeterWidget first;
        poller.setRadioStatus(&radio);
        poller.rescalePowerMeters(100);
        poller.setMainPowerMeter(&meter);
        poller.addTarget(&first);
        radio.setPowerReadings(42.0, 3.0, 1.2);
        amp.applyStationValue("forwardPowerW", 1236.0);
        amp.applyStationValue("swr", 1.12);
        amp.applyStationValue("present", true);
        amp.applyStationValue("operate", true);
        QCOMPARE(poller.cachedReading({}, MeterBinding::TxPower).value(), 1236.0);
        QCOMPARE(poller.cachedReading({}, MeterBinding::TxSwr).value(), 1.12);
        QCOMPARE(first.powerScale(), 1500);
        QCOMPARE(first.powerScaleTop(), 2000.0);
        poller.setInTx(true);
        QCOMPARE(poller.cachedReading({}, MeterBinding::TxPower).value(), 1236.0);
        QCOMPARE(poller.cachedReading({}, MeterBinding::TxSwr).value(), 1.12);
        radio.setPowerReadings(98.0, 4.0, 1.4);
        QCOMPARE(poller.cachedReading({}, MeterBinding::TxPower).value(), 1236.0);
        QCOMPARE(poller.cachedReading({}, MeterBinding::TxSwr).value(), 1.12);
        QCOMPARE(poller.cachedReading({}, MeterBinding::TxReversePower).value(), 4.0);
        poller.rescalePowerMeters(200);
        QCOMPARE(first.powerScaleTop(), 2000.0);
        MeterWidget second;
        poller.addTarget(&second);
        QCOMPARE(second.powerScaleTop(), 2000.0);
        amp.applyStationValue("present", false);
        QVERIFY(amp.operate());
        QCOMPARE(poller.cachedReading({}, MeterBinding::TxPower).value(), 98.0);
        QCOMPARE(poller.cachedReading({}, MeterBinding::TxSwr).value(), 1.4);
        QCOMPARE(first.powerScale(), 200);
        QCOMPARE(second.powerScaleTop(), 240.0);
    }
    void settingRadioStatusAfterRuleCannotOverwriteAmplifier()
    {
        AmplifierModel amp;
        RfKitModel rf;
        RadioStatus radio;
        MainPowerMeter meter(&amp, &rf, &radio);
        amp.applyStationValue("forwardPowerW", 1236.0);
        amp.applyStationValue("present", true);
        amp.applyStationValue("operate", true);
        MeterPoller poller;
        poller.setMainPowerMeter(&meter);
        poller.setRadioStatus(&radio);
        QCOMPARE(poller.cachedReading({}, MeterBinding::TxPower).value(), 1236.0);
        poller.setMainPowerMeter(nullptr);
        QCOMPARE(poller.cachedReading({}, MeterBinding::TxPower).value(), 0.0);
        poller.setRadioStatus(nullptr);
        poller.setMainPowerMeter(&meter);
        double consumerValue = 0;
        poller.addReadingConsumer(this, {}, {MeterBinding::TxPower},
            [&consumerValue](int, double value) { consumerValue = value; });
        const QMetaObject::Connection consumerConnection = connect(&poller, &MeterPoller::readingUpdated,
            this, [&consumerValue](const QJsonObject&, int binding, double value) {
                if (binding == MeterBinding::TxPower) { consumerValue = value; }
            });
        QCOMPARE(consumerValue, 1236.0);
        poller.setMainPowerMeter(nullptr);
        QVERIFY(consumerValue < 0);
        disconnect(consumerConnection);
        poller.removeReadingConsumer(this);
    }
};
QTEST_MAIN(TstMainPowerMeterPoller)
#include "tst_main_power_meter_poller.moc"
