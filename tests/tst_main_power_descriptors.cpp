// no-port-check: NereusSDR-original. Per-instance power display scales.
// Modification history (NereusSDR):
// 2026-10-12 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
#include <QtTest>
#include "gui/instruments/PowerReadingDescriptor.h"
#include "gui/instruments/BarInstrument.h"
#include "gui/instruments/NeedleInstrument.h"
#include "gui/instruments/InstrumentFooter.h"
#include "gui/meters/MeterPoller.h"
using namespace NereusSDR;
class TstMainPowerDescriptors : public QObject {
    Q_OBJECT
private slots:
    void standardAndAmplifierTicksMatchExistingScales()
    {
        const ReadingDescriptor radio = powerReadingDescriptor(100, 120);
        const ReadingDescriptor amp = powerReadingDescriptor(1500, 2000);
        const QList<double> radioTicks{0,30,60,90,120};
        const QStringList radioLabels{"0","30","60","90","120"};
        const QList<double> ampTicks{0,500,1000,1500,2000};
        const QStringList ampLabels{"0","500","1000","1.5k","2k"};
        for (int i=0; i<5; ++i) {
            QCOMPARE(radio.ticks.at(i).value, radioTicks.at(i));
            QCOMPARE(radio.ticks.at(i).label, radioLabels.at(i));
            QCOMPARE(amp.ticks.at(i).value, ampTicks.at(i));
            QCOMPARE(amp.ticks.at(i).label, ampLabels.at(i));
        }
        QCOMPARE(radio.minorTicks, (QList<double>{15,45,75,105}));
        QCOMPARE(amp.minorTicks, (QList<double>{250,750,1250,1750}));
    }
    void boundInstrumentKeepsReadingAndFullOverrangeText()
    {
        BarInstrument bar;
        NeedleInstrument needle;
        QVERIFY(bar.setPrimary(MeterBinding::TxPower));
        QVERIFY(needle.setPrimary(MeterBinding::TxPower));
        const ReadingDescriptor amp = powerReadingDescriptor(1500,2000);
        bar.onReading(MeterBinding::TxPower, 1236);
        bar.setPrimaryScale(amp);
        QCOMPARE(bar.primary(), MeterBinding::TxPower);
        QCOMPARE(bar.value(), 1236.0);
        QCOMPARE(bar.readingName(), amp.thetisName());
        needle.setPrimaryScale(amp);
        bar.onReading(MeterBinding::TxPower, 2300);
        needle.onReading(MeterBinding::TxPower, 2300);
        QVERIFY(bar.hasValue());
        QVERIFY(needle.hasValue());
        QCOMPARE(bar.value(), 2300.0);
        QCOMPARE(bar.reading()->textWithUnit(bar.value()), QStringLiteral("2300 W"));
        QCOMPARE(needle.reading()->textWithUnit(2300), QStringLiteral("2300 W"));
        bar.setPrimaryScale(powerReadingDescriptor(5,6));
        QCOMPARE(bar.primary(), MeterBinding::TxPower);
        QCOMPARE(bar.value(), 2300.0);
        QCOMPARE(bar.reading()->max, 6.0);
    }
};
QTEST_MAIN(TstMainPowerDescriptors)
#include "tst_main_power_descriptors.moc"
