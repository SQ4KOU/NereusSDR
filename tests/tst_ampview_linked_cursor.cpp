// no-port-check: NereusSDR-original linked AmpView presentation tests.
#include <QtTest/QtTest>

#include "core/dsp/Ps3DisplayAdapter.h"
#include "gui/AmpViewChart.h"

#include <limits>

using namespace NereusSDR;

class TstAmpViewLinkedCursor : public QObject {
    Q_OBJECT

private slots:
    void cursorInterpolatesEachDomain()
    {
        AmpViewChart chart;
        Ps3PlotData plot;
        plot.measuredMagnitude = {{0.0, 0.1}, {0.2, 0.3}, {0.7, 0.8}, {1.0, 0.9}};
        plot.measuredPhase = {{0.0, -20.0}, {0.2, -10.0}, {0.7, 40.0}, {1.0, 50.0}};
        plot.correctionMagnitude = {{0.0, 0.0}, {0.35, 0.7}, {1.0, 0.2}};
        plot.correctionPhase = {{0.0, -30.0}, {0.5, 10.0}, {0.9, 30.0}};
        chart.setPlotData(plot);
        chart.setSelectedInput(0.6);

        const AmpViewCursorReadout reading = chart.readoutAt(chart.selectedInput());
        QVERIFY(reading.magnitudeAmp.has_value());
        QVERIFY(reading.magnitudeCorrection.has_value());
        QVERIFY(reading.phaseAmp.has_value());
        QVERIFY(reading.phaseCorrection.has_value());
        QCOMPARE(*reading.magnitudeAmp, 0.7);
        QVERIFY(qAbs(*reading.magnitudeCorrection - (0.7 + (0.2 - 0.7) * .25 / .65)) < 1e-12);
        QCOMPARE(*reading.phaseAmp, 30.0);
        QCOMPARE(*reading.phaseCorrection, 15.0);

        chart.setLowRes(true);
        QCOMPARE(chart.lowResStride(), 4);
        QCOMPARE(*chart.readoutAt(0.6).phaseCorrection, 15.0);
        chart.setSeriesVisible(AmpViewChart::Series::MeasuredPhase, false);
        QVERIFY(!chart.readoutAt(0.6).phaseAmp.has_value());
        QVERIFY(chart.readoutAt(0.6).magnitudeAmp.has_value());
    }

    void gainZeroAndInvalidDomainsAreBlank()
    {
        Ps3Snapshot raw;
        raw.sampleCount = 3;
        raw.correctionCount = 2;
        raw.x = {0.2, 0.5, 0.8};
        raw.ym = {1.0, 0.0, 1.0};
        raw.yc = {1.0, 1.0, 1.0};
        raw.ys = {0.0, 0.0, 0.0};
        raw.xmCorrection = {0.2, 0.8};
        raw.ymCorrection = {1.0, 1.0};
        raw.xaCorrection = {0.2, 0.8};
        raw.yaCorrection = {0.0, 30.0};
        const Ps3PlotData transformed = Ps3DisplayAdapter::transform(raw);
        QCOMPARE(transformed.measuredMagnitude.size(), std::size_t{3});
        QCOMPARE(transformed.measuredGain.size(), std::size_t{2});
        AmpViewChart chart;
        chart.setPlotData(transformed);
        chart.setShowGain(true);
        QVERIFY(!chart.readoutAt(0.0).magnitudeAmp.has_value());
        QVERIFY(chart.readoutAt(0.5).magnitudeCorrection.has_value());

        Ps3PlotData invalid;
        invalid.measuredMagnitude = {{0.2, 0.4}, {0.2, 0.5}};
        invalid.correctionMagnitude = {{0.2, 0.4}, {0.8, 0.6}};
        invalid.measuredPhase = {{0.2, 0.0}, {0.8, std::numeric_limits<double>::quiet_NaN()}};
        chart.setShowGain(false);
        chart.setPlotData(invalid);
        const AmpViewCursorReadout reading = chart.readoutAt(0.5);
        QVERIFY(!reading.magnitudeAmp.has_value());
        QVERIFY(reading.magnitudeCorrection.has_value());
        QVERIFY(!reading.phaseAmp.has_value());
        QVERIFY(!reading.phaseCorrection.has_value());
        QVERIFY(!chart.readoutAt(1.0).magnitudeCorrection.has_value());

        raw.ys.clear();
        const Ps3PlotData rejected = Ps3DisplayAdapter::transform(raw);
        QVERIFY(rejected.measuredMagnitude.empty());
    }

    void zeroEndpointAndOffViewportSamplesKeepValidReadings()
    {
        AmpViewChart chart;
        Ps3PlotData plot;
        plot.measuredMagnitude = {{0.0, 0.2}, {0.8, 0.8}, {1.2, 1.4}};
        plot.measuredPhase = {{0.0, -15.0}, {0.8, 25.0}, {1.2, 30.0}};
        plot.correctionMagnitude = {{0.0, 0.0}, {0.4, 0.5}, {1.2, 1.2}};
        plot.correctionPhase = {{0.0, -10.0}, {0.5, 10.0}, {1.2, 50.0}};
        plot.measuredGain = {{0.0, 1.1}, {0.8, 1.2}};
        chart.setPlotData(plot);
        const AmpViewCursorReadout endpoint = chart.readoutAt(0.0);
        QCOMPARE(*endpoint.magnitudeAmp, 0.2);
        QCOMPARE(*endpoint.phaseAmp, -15.0);
        QCOMPARE(*endpoint.phaseCorrection, -10.0);
        QVERIFY(chart.readoutAt(0.6).magnitudeAmp.has_value());
        QVERIFY(chart.readoutAt(0.6).magnitudeCorrection.has_value());
        QVERIFY(chart.readoutAt(0.6).phaseCorrection.has_value());
        chart.setShowGain(true);
        QVERIFY(!chart.readoutAt(0.0).magnitudeAmp.has_value());
        QVERIFY(chart.readoutAt(0.0).phaseAmp.has_value());
    }

    void measuredCloudUsesSortedLookupWithoutChangingPlotOrder()
    {
        Ps3Snapshot raw;
        raw.sampleCount = 4;
        raw.correctionCount = 3;
        raw.x = {0.7, 0.2, 1.0, 0.0};
        raw.ym = {1.0, 1.0, 1.0, 1.0};
        raw.yc = {1.0, 1.0, 1.0, 1.0};
        raw.ys = {0.0, 0.0, 0.0, 0.0};
        raw.xmCorrection = {0.0, 0.35, 1.0};
        raw.ymCorrection = {1.0, 1.1, 1.0};
        raw.xaCorrection = {0.0, 0.5, 1.0};
        raw.yaCorrection = {-10.0, 0.0, 10.0};
        const Ps3PlotData plot = Ps3DisplayAdapter::transform(raw);
        QCOMPARE(plot.measuredMagnitude.at(0).x, 0.7);
        QCOMPARE(plot.measuredMagnitude.at(1).x, 0.2);
        AmpViewChart chart;
        chart.setPlotData(plot);
        QCOMPARE(chart.plotData().measuredMagnitude.at(0).x, 0.7);
        const AmpViewCursorReadout reading = chart.readoutAt(0.6);
        QCOMPARE(*reading.magnitudeAmp, 0.6);
        QCOMPARE(*reading.phaseAmp, 0.0);
        QVERIFY(reading.magnitudeCorrection.has_value());
        QCOMPARE(*reading.phaseCorrection, 2.0);

        Ps3PlotData repeated = plot;
        repeated.measuredMagnitude.push_back({0.7, 0.7});
        chart.setPlotData(repeated);
        QCOMPARE(*chart.readoutAt(0.6).magnitudeAmp, 0.6);
        repeated.measuredMagnitude.push_back({0.7, 0.8});
        chart.setPlotData(repeated);
        QVERIFY(!chart.readoutAt(0.6).magnitudeAmp.has_value());
        QVERIFY(!chart.readoutAt(0.7).magnitudeAmp.has_value());
        QVERIFY(chart.readoutAt(0.1).magnitudeAmp.has_value());
        QVERIFY(chart.readoutAt(0.6).phaseAmp.has_value());
        QVERIFY(chart.readoutAt(0.6).magnitudeCorrection.has_value());
    }

    void wrappedMeasuredPhaseDoesNotInventZero()
    {
        Ps3PlotData plot;
        plot.measuredMagnitude = {{0.2, 0.2}, {0.8, 0.8}};
        plot.measuredPhase = {{0.8, -179.0}, {0.2, 179.0}};
        plot.correctionPhase = {{0.2, 170.0}, {0.8, 190.0}};
        AmpViewChart chart;
        chart.setPlotData(plot);
        QCOMPARE(*chart.readoutAt(0.2).phaseAmp, 179.0);
        QCOMPARE(*chart.readoutAt(0.8).phaseAmp, -179.0);
        QVERIFY(!chart.readoutAt(0.5).phaseAmp.has_value());
        QCOMPARE(*chart.readoutAt(0.5).phaseCorrection, 180.0);
        QCOMPARE(*chart.readoutAt(0.5).magnitudeAmp, 0.5);
    }

    void keyboardMovesOneLinkedInput()
    {
        AmpViewChart chart;
        chart.resize(520, 350);
        chart.show();
        chart.setFocus();
        chart.setSelectedInput(0.5);
        QTest::keyClick(&chart, Qt::Key_Right);
        QCOMPARE(chart.selectedInput(), 0.51);
        QTest::keyClick(&chart, Qt::Key_Home);
        QCOMPARE(chart.selectedInput(), 0.0);
    }
};

QTEST_MAIN(TstAmpViewLinkedCursor)
#include "tst_ampview_linked_cursor.moc"
