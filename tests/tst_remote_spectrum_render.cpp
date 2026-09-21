// no-port-check: NereusSDR-original. Remote display rendering contract.
#include <QTest>
#define private public
#include "gui/SpectrumWidget.h"
#undef private
#include "core/session/media/SpectrumEndpoint.h"

using namespace NereusSDR;

class TestRemoteSpectrumRender : public QObject {
    Q_OBJECT
private slots:
    void remoteMaxBinUsesEachRequestedPassbandAndClearsWithContext()
    {
        SpectrumWidget widget;
        SpectrumEndpointContext context;
        context.codec = {29, 1, -180, 0, 11, 11, 0};
        context.exactCentreHz = 10000000;
        context.exactSpanHz = 10000;
        widget.setRemoteSpectrumContext(context, context.exactCentreHz, 192000);
        DisplayCodecFrame frame;
        frame.context = context.codec;
        frame.traceDbm = QVector<float>(11, -120);
        frame.traceDbm[2] = -62;
        frame.traceDbm[8] = -43;
        frame.waterfallDbm = QVector<float>(11, -110);
        QVERIFY(widget.updateRemoteSpectrum(frame));
        QCOMPARE(widget.peakDbmInPassband(9996000, 9998000), -62.0);
        QCOMPARE(widget.peakDbmInPassband(10002000, 10004000), -43.0);
        QCOMPARE(widget.peakDbmInPassband(11000000, 11002000), -400.0);
        widget.clearRemoteSpectrum();
        QCOMPARE(widget.peakDbmInPassband(9996000, 9998000), -400.0);
    }

    void independentPlanesAndSuppliedWideCoverage()
    {
        SpectrumWidget widget;
        widget.resize(500, 300);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        widget.setSpectrumRenderMode(int(SpectrumRenderMode::Mode3D));
        widget.setDbmCalOffset(18.0f); // A remote client-local value is ignored.
        widget.setWfUpdatePeriodMs(20);
        widget.setActivePeakHoldEnabled(true);
        SpectrumEndpointContext context;
        context.codec = {1, 1, -180, 0, 128, 128, 96};
        context.exactCentreHz = 14225000;
        context.exactSpanHz = 24000;
        context.wideCentreHz = context.exactCentreHz;
        context.wideSpanHz = 96000;
        widget.setRemoteSpectrumContext(context, context.exactCentreHz, 192000);
        DisplayCodecFrame frame;
        frame.context = context.codec;
        frame.traceDbm = QVector<float>(128, -80);
        frame.waterfallDbm = QVector<float>(128, -130);
        frame.wideDbm = QVector<float>(96, -110);
        frame.waterfallAdvance = true;
        QVERIFY(widget.updateRemoteSpectrum(frame));
        QCOMPARE(widget.renderedPixels(), frame.traceDbm);
        QCOMPARE(widget.wfRenderedPixels(), frame.waterfallDbm);
        QCOMPARE(widget.activePeakHoldPeaksForTest().size(), frame.traceDbm.size());
        QVERIFY(std::isfinite(widget.activePeakHoldPeaksForTest().first()));
        QTRY_COMPARE(widget.dssRowsPushedForTest(), 1);
        QCOMPARE(widget.dssNewestRowWideBandwidthForTest(), 0.096);
        // A display pause must not replay the last received waterfall row.
        QTest::qWait(100);
        QCOMPARE(widget.dssRowsPushedForTest(), 1);
        frame.waterfallAdvance = false;
        frame.traceDbm.fill(-70);
        QVERIFY(widget.updateRemoteSpectrum(frame));
        QTest::qWait(70);
        QCOMPARE(widget.dssRowsPushedForTest(), 1);
        QCOMPARE(widget.renderedPixels(), frame.traceDbm);
    }

    void remoteFramesIgnoreClientCalibrationWhileLocalRenderingKeepsIt()
    {
        SpectrumWidget local;
        SpectrumWidget remote;
        SpectrumWidget uncalibrated;
        uncalibrated.setDbmCalOffset(0.0f);
        local.setDbmCalOffset(18.0f);
        remote.setDbmCalOffset(18.0f); // Deliberately wrong for the station.

        SpectrumEndpointContext context;
        context.codec = {19, 1, -180, 0, 64, 64, 0};
        context.exactCentreHz = 14225000;
        context.exactSpanHz = 24000;
        remote.setRemoteSpectrumContext(context, context.exactCentreHz, 192000);

        const QRect plot(0, 0, 640, 240);
        constexpr float stationCalibratedDbm = -120.0f;
        QCOMPARE(remote.dbmToY(stationCalibratedDbm, plot),
                 uncalibrated.dbmToY(stationCalibratedDbm, plot));
        QCOMPARE(remote.dbmToYf(stationCalibratedDbm, plot),
                 uncalibrated.dbmToYf(stationCalibratedDbm, plot));
        QVERIFY(local.dbmToY(stationCalibratedDbm, plot)
                 < remote.dbmToY(stationCalibratedDbm, plot));
    }

    void contextReplacementRejectsOldDataAndClearsHistory()
    {
        SpectrumWidget widget;
        widget.resize(500, 300);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        widget.setSpectrumRenderMode(int(SpectrumRenderMode::Mode3D));
        widget.setWfUpdatePeriodMs(20);
        SpectrumEndpointContext context;
        context.codec = {7, 1, -180, 0, 128, 128, 0};
        context.exactCentreHz = 14225000;
        context.exactSpanHz = 24000;
        widget.setRemoteSpectrumContext(context, context.exactCentreHz, 192000);
        DisplayCodecFrame frame;
        frame.context = context.codec;
        frame.traceDbm = QVector<float>(128, -90);
        frame.waterfallDbm = QVector<float>(128, -120);
        frame.waterfallAdvance = true;
        QVERIFY(widget.updateRemoteSpectrum(frame));
        QTRY_COMPARE(widget.dssRowsPushedForTest(), 1);
        ++context.codec.contextGeneration;
        context.exactCentreHz += 100000;
        widget.setRemoteSpectrumContext(context, context.exactCentreHz, 192000);
        QCOMPARE(widget.dssRowsPushedForTest(), 0);
        QVERIFY(widget.renderedPixels().isEmpty());
        QVERIFY(!widget.updateRemoteSpectrum(frame));
        frame.context = context.codec;
        QVERIFY(widget.updateRemoteSpectrum(frame));
        QTRY_COMPARE(widget.dssRowsPushedForTest(), 1);
        widget.setCenterFrequency(context.exactCentreHz + 200000);
        QVERIFY(widget.renderedPixels().isEmpty());
        QVERIFY(!widget.updateRemoteSpectrum(frame));
        widget.clearRemoteSpectrum();
        QVERIFY(!widget.updateRemoteSpectrum(frame));
        QTest::qWait(80);
        QCOMPARE(widget.dssRowsPushedForTest(), 0);
    }
};
QTEST_MAIN(TestRemoteSpectrumRender)
#include "tst_remote_spectrum_render.moc"
