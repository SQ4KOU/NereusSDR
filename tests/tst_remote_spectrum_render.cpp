// no-port-check: NereusSDR-original. Remote display rendering contract.
#include <QTest>
#include <QMouseEvent>
#include <QSignalSpy>
#define private public
#include "gui/SpectrumWidget.h"
#undef private
#include "core/session/media/SpectrumEndpoint.h"

using namespace NereusSDR;

class TestRemoteSpectrumRender : public QObject {
    Q_OBJECT
private slots:
    void remoteFrequencyScaleDragStopsAtAvailableSourceBandwidth()
    {
        SpectrumWidget widget;
        widget.resize(1000, 700);
        widget.show();
        QVERIFY(QTest::qWaitForWindowExposed(&widget));
        widget.setConnectionState(ConnectionState::Connected);
        widget.setExtendedViewAllowed(true); // Keep the operator's saved preference.
        widget.setVfoFrequency(14225000);
        SpectrumEndpointContext context;
        context.codec = {31, 1, -180, 0, 128, 128, 0};
        context.exactCentreHz = 14225000;
        context.exactSpanHz = 180000;
        widget.setRemoteSpectrumContext(context, context.exactCentreHz, 192000);
        QSignalSpy spans(&widget, &SpectrumWidget::bandwidthChangeRequested);
        const int y = widget.notchSpecRect().height() + widget.kDividerH
            + widget.kFreqScaleH / 2;
        const auto mouse = [&](QEvent::Type type, int x, Qt::MouseButton button,
                               Qt::MouseButtons buttons) {
            const QPointF pos(x, y);
            QMouseEvent event(type, pos, widget.mapToGlobal(pos), button, buttons,
                              Qt::NoModifier);
            QCoreApplication::sendEvent(&widget, &event);
        };
        mouse(QEvent::MouseButtonPress, 800, Qt::LeftButton, Qt::LeftButton);
        mouse(QEvent::MouseMove, 300, Qt::NoButton, Qt::LeftButton);
        QCOMPARE(widget.bandwidth(), 192000.0); // Clamp during drag, before any ACK.
        mouse(QEvent::MouseButtonRelease, 300, Qt::LeftButton, Qt::NoButton);
        QCOMPARE(spans.size(), 1);
        QCOMPARE(spans.first().first().toDouble(), 192000.0);
        context.codec.contextGeneration++;
        context.exactSpanHz = 192000;
        widget.setRemoteSpectrumContext(context, context.exactCentreHz, 192000);
        QCOMPARE(widget.bandwidth(), 192000.0); // Accepted source cannot snap it narrower.
        QVERIFY(widget.extendedViewAllowed());
        QVERIFY(!widget.extendedMode());

        SpectrumWidget local;
        local.setSampleRate(192000);
        local.setExtendedViewAllowed(true);
        QVERIFY(local.maxZoomOutBandwidthHz() > 192000.0);
    }

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

    void acceptedGeometryReprojectsPaintedHistory()
    {
        SpectrumWidget widget;
        SpectrumEndpointContext context;
        context.codec = {7, 1, -180, 0, 128, 128, 0};
        context.exactCentreHz = 14225000;
        context.exactSpanHz = 24000;
        widget.setRemoteSpectrumContext(context, context.exactCentreHz, 192000);
        // A previously painted RF marker at 14.225 MHz, in both 2D rings.
        // At 50 Hz/pixel a 1 kHz view correction moves it 20 pixels left.
        QImage painted(480, 2, QImage::Format_RGB32);
        painted.fill(Qt::black);
        painted.setPixel(240, 0, qRgb(255, 0, 0));
        widget.m_waterfall = painted;
        widget.m_waterfallHistory = painted;
        widget.m_wfHistoryRowCount = 1;
        widget.m_wfHistoryTimestamps = {1234, 0};
        ++context.codec.contextGeneration;
        context.exactCentreHz += 1000;
        widget.setRemoteSpectrumContext(context, context.exactCentreHz, 192000);
        QCOMPARE(widget.m_waterfall.pixel(220, 0), qRgb(255, 0, 0));
        QCOMPARE(widget.m_waterfall.pixel(240, 0), qRgb(0, 0, 0));
        QCOMPARE(widget.m_waterfallHistory, widget.m_waterfall);
        QCOMPARE(widget.m_wfHistoryRowCount, 1);
        QCOMPARE(widget.m_wfHistoryTimestamps, QVector<qint64>({1234, 0}));
    }

    void contextRenewalPreservesHistoryWhileRejectingOldData_data()
    {
        QTest::addColumn<bool>("moveView");
        QTest::newRow("fixed-view-source-retune") << false;
        QTest::newRow("small-view-tune") << true;
    }

    void contextRenewalPreservesHistoryWhileRejectingOldData()
    {
        QFETCH(bool, moveView);
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
        QCOMPARE(widget.m_wfHistoryRowCount, 1);
        if (moveView) {
            context.exactCentreHz += 500;
            widget.setCenterFrequency(context.exactCentreHz);
        }
        const QImage paintedHistory = widget.m_waterfallHistory;
        const QImage paintedViewport = widget.m_waterfall;
        const auto timestamps = widget.m_wfHistoryTimestamps;
        ++context.codec.contextGeneration;
        widget.setRemoteSpectrumContext(context, 14225500, 192000);
        QCOMPARE(widget.dssRowsPushedForTest(), 1);
        QCOMPARE(widget.m_dss.rowCenterMhzAtAge(0), 14.225);
        QCOMPARE(widget.m_wfHistoryRowCount, 1);
        QCOMPARE(widget.m_waterfallHistory, paintedHistory);
        QCOMPARE(widget.m_waterfall, paintedViewport);
        QCOMPARE(widget.m_wfHistoryTimestamps, timestamps);
        QVERIFY(widget.renderedPixels().isEmpty());
        QVERIFY(!widget.updateRemoteSpectrum(frame));
        QTest::qWait(80);
        QCOMPARE(widget.dssRowsPushedForTest(), 1); // No replay of the old pending row.
        frame.context = context.codec;
        QVERIFY(widget.updateRemoteSpectrum(frame));
        QTRY_COMPARE(widget.dssRowsPushedForTest(), 2);
        QCOMPARE(widget.m_dss.rowCenterMhzAtAge(1), 14.225);
        QCOMPARE(widget.m_wfHistoryRowCount, 2);
        widget.setCenterFrequency(context.exactCentreHz + 200000);
        QVERIFY(widget.renderedPixels().isEmpty());
        QVERIFY(!widget.updateRemoteSpectrum(frame));
        widget.clearRemoteSpectrum();
        QVERIFY(!widget.updateRemoteSpectrum(frame));
        QTest::qWait(80);
        QCOMPARE(widget.dssRowsPushedForTest(), 0);
        QCOMPARE(widget.m_wfHistoryRowCount, 0);
    }
};
QTEST_MAIN(TestRemoteSpectrumRender)
#include "tst_remote_spectrum_render.moc"
