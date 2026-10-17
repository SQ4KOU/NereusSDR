// tests/tst_accessory_panels_layout3.cpp (NereusSDR)
//
// no-port-check: NereusSDR-original test file.
//
// The Tuner Genius, Power Genius and RF2K-S panels in layout 3: forward
// power as one large bar, the rest as slim rows; values while the device
// passes power, dashes on forward, reflected and SWR when it does not
// (temperature stays live), every row dashed for an absent device. Nothing
// here connects to a device: the panels are fed through their setters and
// models.

#include "core/PgxlConnection.h"
#include "core/Rf2ksConnection.h"
#include "core/TgxlConnection.h"
#include "gui/HGauge.h"
#include "gui/applets/AccessoryReadouts.h"
#include "gui/applets/AmpApplet.h"
#include "gui/applets/Rf2ksApplet.h"
#include "gui/applets/TunerApplet.h"
#include "gui/instruments/BarInstrument.h"
#include "models/AmplifierModel.h"
#include "models/RadioModel.h"
#include "models/RfKitModel.h"
#include "models/TunerModel.h"

#include <QtTest/QtTest>

using namespace NereusSDR;

namespace {

// The panel's bars, top to bottom: "name/large" or "name/slim".
QStringList rows(const QWidget& panel)
{
    QList<BarInstrument*> bars = panel.findChildren<BarInstrument*>();
    std::sort(bars.begin(), bars.end(), [&panel](BarInstrument* a, BarInstrument* b) {
        return a->mapTo(&panel, QPoint(0, 0)).y() < b->mapTo(&panel, QPoint(0, 0)).y();
    });
    QStringList out;
    for (BarInstrument* bar : bars) {
        out << bar->readingName() + (bar->isCompact() ? QStringLiteral("/slim")
                                                      : QStringLiteral("/large"));
    }
    return out;
}

void layOut(QWidget& panel)
{
    panel.resize(260, panel.sizeHint().height());
    panel.ensurePolished();
    if (panel.layout()) { panel.layout()->activate(); }
}

BarInstrument* bar(const QWidget& panel, const QString& name)
{
    for (BarInstrument* b : panel.findChildren<BarInstrument*>()) {
        if (b->readingName() == name) { return b; }
    }
    return nullptr;
}

RfKitPowerSnapshot rfKitPower(int forwardW, int reflectedW, float swr, float tempC)
{
    RfKitPowerSnapshot snap;
    snap.forwardW = forwardW;
    snap.reflectedW = reflectedW;
    snap.swr = swr;
    snap.temperatureC = tempC;
    snap.voltageV = 52.7f;
    snap.currentA = 31.46f;
    return snap;
}

TunerModel::StationConnectionState phase(TunerModel::ConnectionPhase p)
{
    TunerModel::StationConnectionState s;
    s.configuredHost = QStringLiteral("192.0.2.41");
    s.configuredPort = 9008;
    s.phase = p;
    return s;
}

} // namespace

class TstAccessoryPanelsLayout3 : public QObject {
    Q_OBJECT
private slots:
    void tunerHasForwardReflectedAndSwr();
    void ampHasAllFourRows();
    void rf2ksHasAllFourRows();
    void rowsLineUpAcrossThePanels();

    void tunerShowsValuesWhilePassingPower();
    void tunerShowsDashesBelowHalfAWatt();
    void tunerForwardScales();
    void tunerPostTuneTextIsKept();

    void ampShowsValuesWhileTransmitting();
    void ampShowsDashesWhenNotTransmitting();
    void ampFollowsItsModel();
    void ampAbsentShowsAllDashes();
    void ampTextLineIsUnchanged();

    void rf2ksShowsValuesWhilePassingPower();
    void rf2ksShowsDashesAtZeroWatts();
    void rf2ksAbsentShowsAllDashes();
    void rf2ksTextLineKeepsVoltsAndAmps();
};

void TstAccessoryPanelsLayout3::tunerHasForwardReflectedAndSwr()
{
    TunerModel model;
    TunerApplet tuner(nullptr, &model);
    layOut(tuner);
    QCOMPARE(rows(tuner), (QStringList{"Forward/large", "Reflected/slim", "SWR/slim"}));
    QVERIFY(tuner.findChildren<HGauge*>().isEmpty());
    QCOMPARE(bar(tuner, "Reflected")->height(), 30);
    QCOMPARE(bar(tuner, "SWR")->height(), 30);
    QVERIFY(!bar(tuner, "Forward")->footerDetailVisible());
}

void TstAccessoryPanelsLayout3::ampHasAllFourRows()
{
    AmpApplet amp(nullptr);
    layOut(amp);
    QCOMPARE(rows(amp), (QStringList{"Forward/large", "Reflected/slim", "SWR/slim", "Temp/slim"}));
    QVERIFY(amp.findChildren<HGauge*>().isEmpty());
    // One limit per reading, the yellow band gone: the scales are the
    // shared ones.
    QCOMPARE(bar(amp, "Forward")->reading()->max, 2000.0);
    QCOMPARE(bar(amp, "Forward")->reading()->threshold.value(), 1500.0);
    QCOMPARE(bar(amp, "Reflected")->reading()->max, 500.0);
    QVERIFY(!bar(amp, "Reflected")->reading()->threshold.has_value());
    QCOMPARE(bar(amp, "SWR")->reading()->threshold.value(), 2.5);
    QCOMPARE(bar(amp, "Temp")->reading()->threshold.value(), 80.0);
    for (const char* name : {"Reflected", "SWR", "Temp"}) {
        QCOMPARE(bar(amp, name)->height(), 30);
        QCOMPARE(bar(amp, name)->compactValuePx(), 13);
    }
}

void TstAccessoryPanelsLayout3::rf2ksHasAllFourRows()
{
    Rf2ksApplet rf(nullptr);
    layOut(rf);
    QCOMPARE(rows(rf), (QStringList{"Forward/large", "Reflected/slim", "SWR/slim", "Temp/slim"}));
    QVERIFY(rf.findChildren<HGauge*>().isEmpty());
    QCOMPARE(bar(rf, "Forward")->reading()->max, 2000.0);
    QCOMPARE(bar(rf, "Forward")->reading()->threshold.value(), 1500.0);
    QCOMPARE(bar(rf, "SWR")->reading()->min, 1.0);
    QCOMPARE(bar(rf, "SWR")->reading()->max, 3.0);
}

void TstAccessoryPanelsLayout3::rowsLineUpAcrossThePanels()
{
    // The same bars at the same sizes in all three panels.
    TunerModel model;
    TunerApplet tuner(nullptr, &model);
    AmpApplet amp(nullptr);
    Rf2ksApplet rf(nullptr);
    const int large = bar(amp, "Forward")->minimumHeight();
    QVERIFY(large > 30);
    QCOMPARE(bar(amp, "Forward")->maximumHeight(), large);
    QCOMPARE(bar(tuner, "Forward")->minimumHeight(), large);
    QCOMPARE(bar(rf, "Forward")->minimumHeight(), large);
    // The large bar is tall enough to keep its footer (name and number).
    QCOMPARE(large, bar(amp, "Forward")->sizeHint().height());
}

void TstAccessoryPanelsLayout3::tunerShowsValuesWhilePassingPower()
{
    TunerModel model;
    TunerApplet tuner(nullptr, &model);
    QVERIFY(!tuner.fwdHasValueForTesting());
    QVERIFY(!tuner.reflectedHasValueForTesting());
    QVERIFY(!tuner.swrHasValueForTesting());

    tuner.updateMeters(98.0f, 3.0f);
    QVERIFY(tuner.fwdHasValueForTesting());
    QVERIFY(tuner.reflectedHasValueForTesting());
    QVERIFY(tuner.swrHasValueForTesting());
    QCOMPARE(tuner.fwdValueForTesting(), 98.0);
    QCOMPARE(tuner.swrValueForTesting(), 3.0);
    // Worked out from the two: a quarter comes back at 3:1.
    QCOMPARE(tuner.reflectedValueForTesting(), 24.5);

    // The open or short cap: all of it comes back.
    tuner.updateMeters(50.0f, 99.0f);
    QCOMPARE(tuner.reflectedValueForTesting(), 50.0);
}

void TstAccessoryPanelsLayout3::tunerShowsDashesBelowHalfAWatt()
{
    TunerModel model;
    TunerApplet tuner(nullptr, &model);
    tuner.updateMeters(0.5f, 1.2f);
    QVERIFY(tuner.fwdHasValueForTesting());
    QVERIFY(tuner.swrHasValueForTesting());

    tuner.updateMeters(0.49f, 1.2f);
    QVERIFY(!tuner.fwdHasValueForTesting());
    QVERIFY(!tuner.reflectedHasValueForTesting());
    QVERIFY(!tuner.swrHasValueForTesting());

    // The held peak does not linger over the dashes: after a carrier and
    // a pause, a smaller carrier's peak is its own.
    tuner.updateMeters(100.0f, 1.2f);
    tuner.updateMeters(0.0f, 1.0f);
    tuner.updateMeters(20.0f, 1.2f);
    QCOMPARE(bar(tuner, "Forward")->peakHold().value(), 20.0);

    // A tuner that goes away reports 0 W through its model: dashes.
    TunerModel::StationConnectionState up = phase(TunerModel::ConnectionPhase::Connected);
    model.setStationConnectionState(up);
    QVERIFY(model.applyStationValue(QByteArrayLiteral("fwdPower"), 75.0f));
    QVERIFY(tuner.fwdHasValueForTesting());
    QCOMPARE(tuner.fwdValueForTesting(), 75.0);
    model.setStationConnectionState(phase(TunerModel::ConnectionPhase::Disconnected));
    QVERIFY(!tuner.fwdHasValueForTesting());
    QVERIFY(!tuner.reflectedHasValueForTesting());
    QVERIFY(!tuner.swrHasValueForTesting());
}

void TstAccessoryPanelsLayout3::tunerForwardScales()
{
    TunerModel model;
    TunerApplet tuner(nullptr, &model);
    const auto labels = [&tuner]() {
        QStringList out;
        for (const ReadingTick& t : bar(tuner, "Forward")->reading()->ticks) { out << t.label; }
        return out;
    };
    // Barefoot from the start.
    QCOMPARE(tuner.fwdScaleMaxForTesting(), 200.0);
    QCOMPARE(tuner.fwdScaleLimitForTesting(), 125.0);
    QCOMPARE(labels(), (QStringList{"0", "50", "100", "150", "200"}));

    tuner.setPowerScale(500, false);
    QCOMPARE(tuner.fwdScaleMaxForTesting(), 600.0);
    QCOMPARE(tuner.fwdScaleLimitForTesting(), 500.0);
    QCOMPARE(labels(), (QStringList{"0", "150", "300", "450", "600"}));

    tuner.setPowerScale(0, true);
    QCOMPARE(tuner.fwdScaleMaxForTesting(), 2000.0);
    QCOMPARE(tuner.fwdScaleLimitForTesting(), 1500.0);
    QCOMPARE(labels(), (QStringList{"0", "500", "1000", "1.5k", "2k"}));
    // The amplifier wins over the radio's rating.
    tuner.setPowerScale(500, true);
    QCOMPARE(tuner.fwdScaleMaxForTesting(), 2000.0);

    tuner.setPowerScale(100, false);
    QCOMPARE(tuner.fwdScaleMaxForTesting(), 200.0);
    QCOMPARE(tuner.fwdScaleLimitForTesting(), 125.0);

    // A reading on the bar stays through a change of scale, still named.
    tuner.updateMeters(98.0f, 1.1f);
    tuner.setPowerScale(0, true);
    QVERIFY(tuner.fwdHasValueForTesting());
    QCOMPARE(tuner.fwdValueForTesting(), 98.0);
    QCOMPARE(bar(tuner, "Forward")->readingName(), QStringLiteral("Forward"));
}

void TstAccessoryPanelsLayout3::tunerPostTuneTextIsKept()
{
    TunerModel model;
    TunerApplet tuner(nullptr, &model);
    QCOMPARE(tuner.tuneButtonTextForTesting(), QStringLiteral("TUNE"));
    tuner.updateMeters(50.0f, 1.23f);
    QCOMPARE(tuner.tuneButtonTextForTesting(), QStringLiteral("TUNE"));
    QVERIFY(model.applyStationValue("isTuning", true));
    QCOMPARE(tuner.tuneButtonTextForTesting(), QStringLiteral("TUNING..."));
    QVERIFY(model.applyStationValue("isTuning", false));
    tuner.updateMeters(50.0f, 1.23f);
    QCOMPARE(tuner.tuneButtonTextForTesting(), QStringLiteral("SWR 1.23"));
}

void TstAccessoryPanelsLayout3::ampShowsValuesWhileTransmitting()
{
    AmpApplet amp(nullptr);
    amp.setState(QStringLiteral("TRANSMIT_A"));
    amp.setFwdPower(1310.0f);
    amp.setFwdPower(1236.0f);
    amp.setSwr(3.0f);
    amp.setTemp(41.0f);
    QVERIFY(amp.fwdHasValueForTesting());
    QVERIFY(amp.reflectedHasValueForTesting());
    QVERIFY(amp.swrHasValueForTesting());
    QVERIFY(amp.tempHasValueForTesting());
    QCOMPARE(amp.fwdGaugeValueForTesting(), 1236.0);
    QCOMPARE(amp.swrGaugeValueForTesting(), 3.0);
    QCOMPARE(amp.tempGaugeValueForTesting(), 41.0);
    // The Power Genius does not report reflected power: a quarter of
    // forward at 3:1.
    QCOMPARE(amp.reflectedValueForTesting(), 309.0);
    // The large bar holds the peak.
    QCOMPARE(bar(amp, "Forward")->peakHold().value(), 1310.0);
}

void TstAccessoryPanelsLayout3::ampShowsDashesWhenNotTransmitting()
{
    AmpApplet amp(nullptr);
    // Before anything: every row dashed.
    QVERIFY(!amp.fwdHasValueForTesting());
    QVERIFY(!amp.reflectedHasValueForTesting());
    QVERIFY(!amp.swrHasValueForTesting());
    QVERIFY(!amp.tempHasValueForTesting());

    // In operate and not keyed: the amplifier's numbers do not show, the
    // temperature does.
    amp.setState(QStringLiteral("OPERATE"));
    amp.setFwdPower(1480.0f);
    amp.setSwr(2.1f);
    amp.setTemp(38.0f);
    QVERIFY(!amp.fwdHasValueForTesting());
    QVERIFY(!amp.reflectedHasValueForTesting());
    QVERIFY(!amp.swrHasValueForTesting());
    QVERIFY(amp.tempHasValueForTesting());
    QCOMPARE(amp.tempGaugeValueForTesting(), 38.0);

    // Keyed: they show. Unkeyed again: dashes, the peak with them.
    amp.setState(QStringLiteral("TRANSMIT_B"));
    QVERIFY(amp.fwdHasValueForTesting());
    QVERIFY(amp.reflectedHasValueForTesting());
    QVERIFY(amp.swrHasValueForTesting());
    QCOMPARE(amp.fwdGaugeValueForTesting(), 1480.0);
    amp.setState(QStringLiteral("IDLE"));
    QVERIFY(!amp.fwdHasValueForTesting());
    QVERIFY(!amp.reflectedHasValueForTesting());
    QVERIFY(!amp.swrHasValueForTesting());
    QVERIFY(amp.tempHasValueForTesting());
    QCOMPARE(bar(amp, "Forward")->peakHold().value(), 0.0);
}

void TstAccessoryPanelsLayout3::ampFollowsItsModel()
{
    // With a model the keyed state is the AmplifierModel's transmitting
    // flag.
    RadioModel model;
    AmpApplet amp(&model);
    PgxlConnection* pgxl = model.pgxlConnection();
    pgxl->injectLineForTesting(
        QStringLiteral("R1|0|state=OPERATE temp=42.5 vac=240 fwd=1480.0 swr=2.1"));
    QVERIFY(!model.amplifierModel()->transmitting());
    QVERIFY(amp.tempHasValueForTesting());
    QCOMPARE(amp.tempGaugeValueForTesting(), 42.5);
    QVERIFY(!amp.fwdHasValueForTesting());
    QVERIFY(!amp.reflectedHasValueForTesting());
    QVERIFY(!amp.swrHasValueForTesting());

    pgxl->injectLineForTesting(
        QStringLiteral("S0|status state=TRANSMIT_A peakfwd=60.0 swr=-24.5 id=22.5"));
    QVERIFY(model.amplifierModel()->transmitting());
    QVERIFY(amp.fwdHasValueForTesting());
    QVERIFY(amp.reflectedHasValueForTesting());
    QVERIFY(amp.swrHasValueForTesting());
    QCOMPARE(amp.fwdGaugeValueForTesting(), model.amplifierModel()->forwardPowerW());
    QCOMPARE(amp.swrGaugeValueForTesting(), model.amplifierModel()->swr());
    QCOMPARE(amp.reflectedValueForTesting(),
             AccessoryReadouts::reflectedWatts(model.amplifierModel()->forwardPowerW(),
                                               model.amplifierModel()->swr()));

    pgxl->injectLineForTesting(
        QStringLiteral("S0|status state=STANDBY peakfwd=60.0 swr=-24.5 id=0.0"));
    QVERIFY(!amp.fwdHasValueForTesting());
    QVERIFY(!amp.reflectedHasValueForTesting());
    QVERIFY(!amp.swrHasValueForTesting());
    QVERIFY(amp.tempHasValueForTesting());
}

void TstAccessoryPanelsLayout3::ampAbsentShowsAllDashes()
{
    RadioModel model;
    AmpApplet amp(&model);
    AmplifierModel* device = model.amplifierModel();
    // Before the first reading.
    QVERIFY(!device->present());
    QVERIFY(!amp.tempHasValueForTesting());
    QVERIFY(!amp.fwdHasValueForTesting());

    model.pgxlConnection()->injectLineForTesting(
        QStringLiteral("R1|0|state=TRANSMIT_A temp=42.5 vac=240 peakfwd=60.0 swr=-24.5 id=22.5"));
    QVERIFY(device->present());
    QVERIFY(amp.tempHasValueForTesting());
    QVERIFY(amp.fwdHasValueForTesting());

    // It disconnects: every row, temperature too.
    device->setStationConnectionState(phase(TunerModel::ConnectionPhase::Disconnected));
    QVERIFY(!device->present());
    QVERIFY(!amp.fwdHasValueForTesting());
    QVERIFY(!amp.reflectedHasValueForTesting());
    QVERIFY(!amp.swrHasValueForTesting());
    QVERIFY(!amp.tempHasValueForTesting());
}

void TstAccessoryPanelsLayout3::ampTextLineIsUnchanged()
{
    AmpApplet amp(nullptr);
    amp.setMainsVoltage(240);
    amp.setDrainCurrent(22.5f);
    amp.setMeff(QStringLiteral("off"));
    QCOMPARE(amp.powerLabelTextForTesting(), QStringLiteral("Volts: 240V  Amps: 22.5A"));
    QCOMPARE(amp.meffLabelTextForTesting(), QStringLiteral("MEffA:   off"));
}

void TstAccessoryPanelsLayout3::rf2ksShowsValuesWhilePassingPower()
{
    Rf2ksApplet rf(nullptr);
    rf.setPower(rfKitPower(1236, 4, 1.12f, 41.0f));
    QVERIFY(rf.fwdHasValueForTesting());
    QVERIFY(rf.reflectedHasValueForTesting());
    QVERIFY(rf.swrHasValueForTesting());
    QVERIFY(rf.tempHasValueForTesting());
    QCOMPARE(rf.fwdGaugeValueForTesting(), 1236);
    // The RF2K-S reports reflected power itself: shown as reported, not
    // worked out (1236 W at 1.12 would be 4 W; 17 W is the amp's word).
    rf.setPower(rfKitPower(1236, 17, 1.12f, 41.0f));
    QCOMPARE(rf.reflectedValueForTesting(), 17);
    QCOMPARE(rf.swrGaugeValueForTesting(), 1.12f);
    QCOMPARE(rf.tempGaugeValueForTesting(), 41.0f);
}

void TstAccessoryPanelsLayout3::rf2ksShowsDashesAtZeroWatts()
{
    Rf2ksApplet rf(nullptr);
    rf.setPower(rfKitPower(850, 3, 1.4f, 27.0f));
    QVERIFY(rf.fwdHasValueForTesting());
    rf.setPower(rfKitPower(0, 0, 1.0f, 38.0f));
    QVERIFY(!rf.fwdHasValueForTesting());
    QVERIFY(!rf.reflectedHasValueForTesting());
    QVERIFY(!rf.swrHasValueForTesting());
    QVERIFY(rf.tempHasValueForTesting());
    QCOMPARE(rf.tempGaugeValueForTesting(), 38.0f);
    QCOMPARE(bar(rf, "Forward")->peakHold().value(), 0.0);
    // One watt is passing power.
    rf.setPower(rfKitPower(1, 0, 1.0f, 38.0f));
    QVERIFY(rf.fwdHasValueForTesting());
}

void TstAccessoryPanelsLayout3::rf2ksAbsentShowsAllDashes()
{
    RadioModel model;
    Rf2ksApplet rf(&model);
    RfKitModel* device = model.rfKitModel();
    QVERIFY(!device->present());
    QVERIFY(!rf.tempHasValueForTesting());
    QVERIFY(!rf.fwdHasValueForTesting());

    device->applyPower(rfKitPower(850, 3, 1.4f, 27.0f));
    QVERIFY(device->present());
    QVERIFY(rf.fwdHasValueForTesting());
    QVERIFY(rf.tempHasValueForTesting());
    QCOMPARE(rf.reflectedValueForTesting(), 3);

    device->setStationConnectionState(phase(TunerModel::ConnectionPhase::Disconnected));
    QVERIFY(!device->present());
    QVERIFY(!rf.fwdHasValueForTesting());
    QVERIFY(!rf.reflectedHasValueForTesting());
    QVERIFY(!rf.swrHasValueForTesting());
    QVERIFY(!rf.tempHasValueForTesting());
}

void TstAccessoryPanelsLayout3::rf2ksTextLineKeepsVoltsAndAmps()
{
    Rf2ksApplet rf(nullptr);
    rf.setPower(rfKitPower(1236, 4, 1.12f, 41.0f));
    // Forward power and SWR are on the face; the line keeps volts (no
    // decimals) and amps (one), as before.
    QCOMPARE(rf.telemetryStripTextForTesting(), QStringLiteral("53 V  31.5 A"));
}

QTEST_MAIN(TstAccessoryPanelsLayout3)
#include "tst_accessory_panels_layout3.moc"
