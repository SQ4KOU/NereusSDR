// no-port-check: NereusSDR-original. Approved transmit power row behavior.
// Modification history (NereusSDR):
// 2026-10-12 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
#include <QtTest>
#include "core/AppSettings.h"
#include "gui/applets/TxApplet.h"
#include "gui/instruments/BarInstrument.h"
#include "models/RadioModel.h"
#include "models/AmplifierModel.h"
#include "models/MainPowerMeter.h"
using namespace NereusSDR;
namespace {
BarInstrument* row(TxApplet& applet, const QString& name)
{
    for (BarInstrument* bar : applet.findChildren<BarInstrument*>()) {
        if (bar->accessibleName() == name) { return bar; }
    }
    return nullptr;
}
} // namespace
class TstTxAppletPowerRow : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        AppSettings::setProfileOverride(QStringLiteral("tx-power-row-test"));
        AppSettings::instance().clear();
    }
    void amplifierReadingDriveAndUnkey()
    {
        RadioModel radio(RadioModel::Role::Remote);
        radio.setBoardForTest(HPSDRHW::Saturn);
        radio.currentRadioChanged(RadioInfo{});
        TxApplet applet(&radio);
        QVERIFY(radio.applyMirroredValue("transmitting", true).isEmpty());
        radio.radioStatus().setPowerReadings(42.0, 0.0, 1.12);
        radio.amplifierModel()->applyStationValue("forwardPowerW", 1236.0);
        radio.amplifierModel()->applyStationValue("swr", 1.12);
        radio.amplifierModel()->applyStationValue("present", true);
        radio.amplifierModel()->applyStationValue("operate", true);
        BarInstrument* power = row(applet, QStringLiteral("Forward power gauge"));
        BarInstrument* swr = row(applet, QStringLiteral("SWR gauge"));
        QVERIFY(power != nullptr);
        QVERIFY(swr != nullptr);
        QVERIFY(power->hasValue());
        QCOMPARE(power->value(), 1236.0);
        QCOMPARE(power->reading()->max, 2000.0);
        QCOMPARE(power->reading()->threshold.value(), 1500.0);
        QCOMPARE(power->compactNoteLabel(), QStringLiteral("DRIVE"));
        QCOMPARE(power->compactNoteNumber(), QStringLiteral("42 W"));
        QCOMPARE(swr->value(), 1.12);
        // Drive keeps changing while the amplifier supplies the display.
        radio.radioStatus().setPowerReadings(98.0, 0.0, 1.3);
        radio.amplifierModel()->applyStationValue("operate", false);
        QVERIFY(power->compactNoteLabel().isEmpty());
        QCOMPARE(power->reading()->max, 120.0);
        QCOMPARE(power->value(), 98.0);
        radio.radioStatus().setPowerReadings(50.0, 0.0, 1.3);
        QCOMPARE(power->value(), 98.0);
        QTRY_COMPARE(power->value(), 83.6);
        QVERIFY(radio.applyMirroredValue("transmitting", false).isEmpty());
        QVERIFY(!power->hasValue());
        QVERIFY(!swr->hasValue());
        QVERIFY(power->compactNoteLabel().isEmpty());
        // A late power update after unkey cannot resurrect either reading.
        radio.radioStatus().setPowerReadings(10.0, 0.0, 1.4);
        QVERIFY(!power->hasValue());
    }
    void qrpUsesOneDecimal()
    {
        RadioModel radio(RadioModel::Role::Remote);
        radio.mainPowerMeter()->setHardwareModel(HPSDRModel::HERMESLITE);
        TxApplet applet(&radio);
        BarInstrument* power = row(applet, QStringLiteral("Forward power gauge"));
        QVERIFY(power != nullptr);
        QCOMPARE(power->reading()->max, 6.0);
        QCOMPARE(power->reading()->textWithUnit(2.4), QStringLiteral("2.4 W"));
    }
};
QTEST_MAIN(TstTxAppletPowerRow)
#include "tst_tx_applet_power_row.moc"
