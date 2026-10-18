// no-port-check: NereusSDR-original regressions for visible IMD display coordinates.
#include <QtTest>
#include "gui/SpectrumWidget.h"
#include "gui/PureSignalViewStyle.h"
#include "gui/styles/WorkspacePreview.h"
#include "core/session/media/SpectrumEndpoint.h"
#include <QScopeGuard>
#include <QSignalSpy>
#include <limits>
#include "support/ImdSpectrumTestAccess.h"
#include <array>
#include <cmath>

using namespace NereusSDR;

namespace {
constexpr std::array<int, 6> kPositions{100, 220, 340, 460, 580, 700};
constexpr std::array<float, 6> kLevels{-88.0f, -65.0f, -22.0f, -22.0f, -65.0f, -88.0f};

void configure(SpectrumWidget& widget, double offset = 0.0)
{
    widget.resize(800, 600);
    widget.setDbmScaleVisible(false);
    widget.setSampleRate(192000.0);
    widget.setDdcCenterFrequency(14200000.0);
    widget.setDisplayWindowPreservingHistory(14200000.0 + offset, 48000.0);
    widget.setSpectrumAveraging(SpectrumAveraging::None);
    widget.setDisplayDuplex(true);
    widget.setMoxOverlay(true);
    widget.setTestingIMD(true);
    widget.setShowIMDMeasurements(true);
    widget.setDbmRange(-140.0f, 0.0f);
}

QVector<float> visibleFrame()
{
    QVector<float> frame(800, -120.0f);
    for (size_t i = 0; i < kPositions.size(); ++i) { frame[kPositions[i]] = kLevels[i]; }
    return frame;
}
}

class TestImdSpectrumIntegration : public QObject {
    Q_OBJECT
private slots:
    void repaintDoesNotAdvanceEma()
    {
        SpectrumWidget widget;
        configure(widget);
        ImdSpectrumTestAccess::seedVisiblePixels(widget, visibleFrame());
        ImdSpectrumTestAccess::render(widget);
        QVector<float> changed = visibleFrame();
        changed[340] = -26.0f;
        changed[460] = -26.0f;
        ImdSpectrumTestAccess::seedVisiblePixels(widget, changed);
        ImdSpectrumTestAccess::render(widget);
        for (int i = 0; i < 20; ++i) {
            ImdSpectrumTestAccess::render(widget);
        }
        QVERIFY(std::abs(ImdSpectrumTestAccess::readout(widget).f0L + 22.4f) < 0.001f);
    }
    void lostProductClearsPreviousSummary()
    {
        SpectrumWidget widget;
        configure(widget);
        ImdSpectrumTestAccess::seedVisiblePixels(widget, visibleFrame());
        ImdSpectrumTestAccess::render(widget);
        QVERIFY(ImdSpectrumTestAccess::readout(widget).valid);
        QVector<float> missing = visibleFrame();
        missing[700] = -120.0f;
        ImdSpectrumTestAccess::seedVisiblePixels(widget, missing);
        ImdSpectrumTestAccess::render(widget);
        QVERIFY(!ImdSpectrumTestAccess::readout(widget).valid);
    }
    void silentReceiverExpiresCurrentReadingsOnceAndReseeds()
    {
        SpectrumWidget widget;
        configure(widget);
        ImdSpectrumTestAccess::seedVisiblePixels(widget, visibleFrame());
        QCOMPARE(widget.imdMeasurementFrame().status, ImdStatus::Valid);
        const qint64 accepted = widget.imdMeasurementFrame().context.acceptedAtMs;
        const qint64 timeout = ImdSpectrumTestAccess::freshnessTimeoutMs(widget);
        QVERIFY(timeout >= 1000);
        QSignalSpy changed(&widget, &SpectrumWidget::imdMeasurementChanged);
        widget.clearOverlayStaticDirtyForTest();
        ImdSpectrumTestAccess::checkFreshnessAt(widget, accepted + timeout);
        QCOMPARE(changed.count(), 0);
        ImdSpectrumTestAccess::checkFreshnessAt(widget, accepted + timeout + 1);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(widget.imdMeasurementFrame().status, ImdStatus::WaitingForSpectrum);
        QVERIFY(!widget.imdMeasurementFrame().context.sourceEligible);
        QVERIFY(!widget.imdMeasurementFrame().summary);
        for (const auto& peak : widget.imdMeasurementFrame().peaks) { QVERIFY(!peak); }
        QVERIFY(ImdSpectrumTestAccess::overlayDirty(widget));
        QImage blank(800, 360, QImage::Format_ARGB32_Premultiplied);
        blank.fill(Qt::transparent);
        QCOMPARE(ImdSpectrumTestAccess::render(widget), blank);
        ImdSpectrumTestAccess::checkFreshnessAt(widget, accepted + timeout + 1000);
        QCOMPARE(changed.count(), 1);
        QVector<float> returned = visibleFrame();
        returned[340] = -30.0f; returned[460] = -30.0f;
        ImdSpectrumTestAccess::seedVisiblePixels(widget, returned);
        QCOMPARE(widget.imdMeasurementFrame().status, ImdStatus::Valid);
        QCOMPARE(widget.imdMeasurementFrame().summary->f0L, -30.0f);
        widget.setShowIMDMeasurements(false);
        ImdSpectrumTestAccess::seedVisiblePixels(widget, returned);
        QCOMPARE(widget.imdMeasurementFrame().status, ImdStatus::Disabled);
        QSignalSpy disabledChanges(&widget, &SpectrumWidget::imdMeasurementChanged);
        const qint64 disabledAt = widget.imdMeasurementFrame().context.acceptedAtMs;
        ImdSpectrumTestAccess::checkFreshnessAt(widget, disabledAt + timeout + 1);
        QCOMPARE(widget.imdMeasurementFrame().status, ImdStatus::Disabled);
        QCOMPARE(disabledChanges.count(), 0);
        SpectrumWidget slowRemote;
        configure(slowRemote);
        SpectrumEndpointContext slowContext;
        slowContext.codec = {43, 1, -180.0f, 0.0f, 800, 800, 0};
        slowContext.exactCentreHz = 14200000.0;
        slowContext.exactSpanHz = 48000.0;
        slowContext.targetFps = 2;
        slowRemote.setRemoteSpectrumContext(slowContext, 14200000.0, 192000.0);
        QCOMPARE(ImdSpectrumTestAccess::freshnessTimeoutMs(slowRemote), qint64(2500));
    }
    void acceptedRemoteFramesUseSequenceAndCapture()
    {
        SpectrumWidget widget;
        configure(widget);
        SpectrumEndpointContext context;
        context.codec = {43, 1, -180.0f, 0.0f, 800, 800, 0};
        context.exactCentreHz = 14200000;
        context.exactSpanHz = 48000;
        widget.setRemoteSpectrumContext(context, 14200000, 192000);
        const RemoteSpectrumCapture firstCapture = ImdSpectrumTestAccess::remoteCapture(widget);
        DisplayCodecFrame frame;
        frame.context = context.codec;
        frame.encoderSequence = 1;
        frame.traceDbm = visibleFrame();
        frame.waterfallDbm = visibleFrame();
        frame.waterfallAdvance = true;
        QVERIFY(widget.updateRemoteSpectrum(frame, firstCapture));
        QCOMPARE(widget.imdMeasurementFrame().status, ImdStatus::Valid);
        QVERIFY(widget.imdMeasurementFrame().context.sourceEligible);
        frame.encoderSequence = 2;
        frame.traceDbm[340] = -26; frame.traceDbm[460] = -26;
        QVERIFY(widget.updateRemoteSpectrum(frame, firstCapture));
        const float smoothed = widget.imdMeasurementFrame().summary->f0L;
        QVERIFY(std::abs(smoothed + 22.4f) < 0.001f);
        QSignalSpy changed(&widget, &SpectrumWidget::imdMeasurementChanged);
        for (int i = 0; i < 20; ++i) {
            QVERIFY(widget.updateRemoteSpectrum(frame, firstCapture));
            QVERIFY(widget.enqueueRemoteWaterfallRow(frame.waterfallDbm, {}, firstCapture));
            ImdSpectrumTestAccess::render(widget);
        }
        QCOMPARE(changed.count(), 0);
        QCOMPARE(widget.imdMeasurementFrame().summary->f0L, smoothed);
        widget.clearOverlayStaticDirtyForTest();
        widget.setTestingIMD(false);
        QVERIFY(!widget.imdMeasurementFrame().summary);
#ifdef NEREUS_GPU_SPECTRUM
        QVERIFY(ImdSpectrumTestAccess::overlayDirty(widget));
#endif
        widget.setTestingIMD(true);
        QVERIFY(widget.updateRemoteSpectrum(frame, firstCapture));
        QCOMPARE(widget.imdMeasurementFrame().status, ImdStatus::WaitingForSpectrum);
        QVERIFY(!widget.imdMeasurementFrame().summary);
        frame.encoderSequence = 3;
        QVERIFY(widget.updateRemoteSpectrum(frame, firstCapture));
        QCOMPARE(widget.imdMeasurementFrame().summary->f0L, -26.0f);
        QVERIFY(widget.presentRemoteTraceCaptured(frame.traceDbm, 14200000, 48000));
        QVERIFY(!widget.imdMeasurementFrame().summary);
        QVERIFY(widget.updateRemoteSpectrum(frame, firstCapture));
        QVERIFY(!widget.imdMeasurementFrame().summary);
        ++context.codec.contextGeneration;
        context.exactCentreHz += 12000;
        widget.setRemoteSpectrumContext(context, 14200000, 192000);
        QVERIFY(!widget.updateRemoteSpectrum(frame, firstCapture));
        frame.context = context.codec; frame.encoderSequence = 1;
        const auto movedCapture = ImdSpectrumTestAccess::remoteCapture(widget);
        QVERIFY(widget.updateRemoteSpectrum(frame, movedCapture));
        QCOMPARE(widget.imdMeasurementFrame().summary->f0L, -26.0f);
        QCOMPARE(widget.imdMeasurementFrame().context.firstSampleHz, 14188000.0);
        widget.clearRemoteSpectrum();
        QVERIFY(!widget.imdMeasurementFrame().summary);
        QVERIFY(!widget.updateRemoteSpectrum(frame, movedCapture));
    }
    void remoteMissingAndRejectedCurrentPlaneClearThenReseed()
    {
        SpectrumWidget widget; configure(widget);
        SpectrumEndpointContext context;
        context.codec = {43, 1, -180.0f, 0.0f, 800, 800, 0};
        context.exactCentreHz = 14200000; context.exactSpanHz = 48000;
        widget.setRemoteSpectrumContext(context, 14200000, 192000);
        DisplayCodecFrame frame;
        frame.context = context.codec; frame.encoderSequence = 1;
        frame.traceDbm = visibleFrame(); frame.waterfallDbm = visibleFrame();
        QVERIFY(widget.updateRemoteSpectrum(frame));
        QVERIFY(widget.imdMeasurementFrame().summary);
        frame.encoderSequence = 2; frame.traceDbm[700] = -120;
        QVERIFY(widget.updateRemoteSpectrum(frame));
        QCOMPARE(widget.imdMeasurementFrame().status, ImdStatus::ProductsMissing);
        QVERIFY(!widget.imdMeasurementFrame().summary);
        QVERIFY(widget.imdMeasurementFrame().peaks[2]);
        QVERIFY(!widget.imdMeasurementFrame().peaks[5]);
        frame.encoderSequence = 3; frame.traceDbm = visibleFrame();
        frame.traceDbm[340] = -30; frame.traceDbm[460] = -30;
        QVERIFY(widget.updateRemoteSpectrum(frame));
        QCOMPARE(widget.imdMeasurementFrame().summary->f0L, -30.0f);
        frame.encoderSequence = 4; frame.traceDbm[0] = std::numeric_limits<float>::infinity();
        QVERIFY(!widget.updateRemoteSpectrum(frame));
        QVERIFY(!widget.imdMeasurementFrame().summary);
        frame.encoderSequence = 3; frame.traceDbm = visibleFrame();
        QVERIFY(widget.updateRemoteSpectrum(frame));
        QVERIFY(!widget.imdMeasurementFrame().summary);
        frame.encoderSequence = 5;
        QVERIFY(widget.updateRemoteSpectrum(frame));
        QCOMPARE(widget.imdMeasurementFrame().summary->f0L, -22.0f);
        frame.encoderSequence = 6; frame.traceDbm.resize(1);
        QVERIFY(!widget.updateRemoteSpectrum(frame));
        QVERIFY(!widget.imdMeasurementFrame().summary);
    }
    void gatesSourcesAndRetuneBlankUntilNewReceiverFrame()
    {
        SpectrumWidget widget;
        configure(widget);
        ImdSpectrumTestAccess::seedVisiblePixels(widget, visibleFrame());
        QVERIFY(widget.imdMeasurementFrame().summary);
        widget.setDisplayWindowPreservingHistory(14212000, 48000);
        QCOMPARE(widget.imdMeasurementFrame().status, ImdStatus::WaitingForSpectrum);
        QVERIFY(!widget.imdMeasurementFrame().summary);
        ImdSpectrumTestAccess::seedVisiblePixels(widget, visibleFrame());
        widget.updateSpectrumFromTxPixels(-1, visibleFrame());
        QVERIFY(!widget.imdMeasurementFrame().summary);
        ImdSpectrumTestAccess::seedVisiblePixels(widget, visibleFrame());
        widget.setDisplayDuplex(false);
        QCOMPARE(widget.imdMeasurementFrame().status, ImdStatus::DuplexRequired);
        QVERIFY(!widget.imdMeasurementFrame().summary);
        widget.setDisplayDuplex(true);
        QCOMPARE(widget.imdMeasurementFrame().status, ImdStatus::WaitingForSpectrum);
        ImdSpectrumTestAccess::seedVisiblePixels(widget, visibleFrame());
        widget.updateSpectrumLinear(0, {}, 1.0, 0.0);
        QVERIFY(!widget.imdMeasurementFrame().summary);
        ImdSpectrumTestAccess::seedVisiblePixels(widget, visibleFrame());
        widget.setDdcCenterFrequency(14201000);
        QVERIFY(!widget.imdMeasurementFrame().summary);
        ImdSpectrumTestAccess::seedVisiblePixels(widget, visibleFrame());
        widget.setSampleRate(384000);
        QVERIFY(!widget.imdMeasurementFrame().summary);
    }
    void sourceChangeAndResizeReseedCurrentReceiverPlane()
    {
        SpectrumWidget widget; configure(widget);
        widget.show();
        QVector<float> bins(3200, 1e-12f);
        for (size_t i = 0; i < kPositions.size(); ++i) {
            bins[1200 + kPositions[i]] = std::pow(10.0f, kLevels[i] / 10.0f);
        }
        widget.updateSpectrumLinear(0, bins, 1.0, 0.0);
        QCOMPARE(widget.imdMeasurementFrame().status, ImdStatus::Valid);
        const quint64 beforeRow = widget.imdMeasurementFrame().context.contextRevision;
        widget.pushTxWaterfallRow(-1, visibleFrame());
        QCOMPARE(widget.imdMeasurementFrame().context.contextRevision, beforeRow);
        bins[1540] = std::pow(10.0f, -2.6f); bins[1660] = std::pow(10.0f, -2.6f);
        widget.updateSpectrumLinear(0, bins, 1.0, 0.0);
        QVERIFY(std::abs(widget.imdMeasurementFrame().summary->f0L + 22.4f) < 0.001f);
        bins[1540] = 1e-3f; bins[1660] = 1e-3f;
        widget.updateSpectrumLinear(1, bins, 1.0, 0.0);
        QVERIFY(std::abs(widget.imdMeasurementFrame().summary->f0L + 30.0f) < 0.001f);
        const quint64 revision = widget.imdMeasurementFrame().context.contextRevision;
        widget.resize(1200, 600);
        QVERIFY(widget.imdMeasurementFrame().context.contextRevision > revision);
        QVERIFY(!widget.imdMeasurementFrame().summary);
        // At 1200 pixels the existing reducer interpolates 800 visible bins.
        // Pin re-seeding from that actual plane, not the pre-reduction bin.
        bins[1540] = 1e-4f; bins[1660] = 1e-4f;
        widget.updateSpectrumLinear(1, bins, 1.0, 0.0);
        const auto& resized = widget.imdMeasurementFrame();
        QCOMPARE(resized.status, ImdStatus::Valid);
        QVERIFY(resized.summary->f0L < -35.0f);
        QCOMPARE(resized.summary->f0L, resized.peaks[0]->absoluteDbm);
        const auto& context = resized.context;
        QCOMPARE(context.firstSampleHz + 1199 * context.sampleStepHz, 14224000.0);
    }
    void mappingMatchesTraceAndFrequencyAxis_data()
    {
        QTest::addColumn<int>("displayWidth");
        QTest::addColumn<double>("span");
        QTest::addColumn<double>("offset");
        QTest::newRow("full-span") << 800 << 192000.0 << 0.0;
        QTest::newRow("quarter-span") << 800 << 48000.0 << 0.0;
        QTest::newRow("ctun") << 800 << 48000.0 << 12000.0;
        QTest::newRow("narrow-with-800-samples") << 320 << 48000.0 << 0.0;
        QTest::newRow("wide") << 1200 << 48000.0 << 12000.0;
    }
    void mappingMatchesTraceAndFrequencyAxis()
    {
        QFETCH(int, displayWidth); QFETCH(double, span); QFETCH(double, offset);
        SpectrumWidget widget; configure(widget, offset);
        widget.resize(displayWidth, 600);
        widget.setDisplayWindowPreservingHistory(14200000 + offset, span);
        const int fftSize = span == 192000.0 ? 800 : 3200;
        QVector<float> bins(fftSize, 1e-12f);
        const int first = span == 192000.0 ? 0 : 1200 + int(offset / 60.0);
        for (size_t i = 0; i < kPositions.size(); ++i) {
            bins[first + kPositions[i]] = std::pow(10.0f, kLevels[i] / 10.0f);
        }
        widget.updateSpectrumLinear(0, bins, 1.0, 0.0);
        const auto& frame = widget.imdMeasurementFrame();
        QCOMPARE(frame.status, ImdStatus::Valid);
        const int count = ImdSpectrumTestAccess::visiblePixels(widget).size();
        QCOMPARE(frame.context.firstSampleHz, 14200000 + offset - span / 2);
        QCOMPARE(frame.context.firstSampleHz + (count - 1) * frame.context.sampleStepHz,
                 14200000 + offset + span / 2);
        const QRect trace(0, 0, displayWidth - 1, 360);
        for (const auto& peak : frame.peaks) {
            QVERIFY(peak);
            const double expectedHz = 14200000 + offset - span / 2
                + peak->sampleIndex * span / (count - 1);
            QCOMPARE(peak->frequencyHz, expectedHz);
            const int traceX = int(double(peak->sampleIndex) * trace.width() / (count - 1));
            QVERIFY(std::abs(ImdSpectrumTestAccess::frequencyX(widget, peak->frequencyHz, trace)
                             - traceX) <= 1);
        }
        const quint64 revision = frame.context.contextRevision;
        widget.setDisplayWindowPreservingHistory(14201000 + offset, span);
        QVERIFY(widget.imdMeasurementFrame().context.contextRevision > revision);
        QVERIFY(!widget.imdMeasurementFrame().summary);
    }
    void displayedPixelsAreNotCroppedTwice_data()
    {
        QTest::addColumn<bool>("workspaceLook");
        QTest::newRow("classic") << false;
        QTest::newRow("workspace") << true;
    }
    void displayedPixelsAreNotCroppedTwice()
    {
        QFETCH(bool, workspaceLook);
        const bool restoreWorkspaceLook = WorkspacePreview::enabled();
        const auto restore = qScopeGuard([restoreWorkspaceLook] {
            WorkspacePreview::setEnabledForTest(restoreWorkspaceLook);
        });
        WorkspacePreview::setEnabledForTest(workspaceLook);
        SpectrumWidget widget;
        configure(widget);
        widget.setImdGuidesVisible(false);
        ImdSpectrumTestAccess::seedVisiblePixels(widget, visibleFrame());
        const QImage image = ImdSpectrumTestAccess::render(widget);
        const QString evidence = qEnvironmentVariable("NEREUS_IMD_EVIDENCE");
        if (!evidence.isEmpty()) {
            const QString style = workspaceLook ? QStringLiteral("workspace")
                                                : QStringLiteral("classic");
            QVERIFY(image.save(evidence + QStringLiteral("/cpu-visible-plane-")
                               + style + QStringLiteral(".png")));
        }
        QVERIFY2(ImdSpectrumTestAccess::readout(widget).valid,
                 "Six products in the visible 800-pixel plane must form a complete measurement");
        QCOMPARE(ImdSpectrumTestAccess::readout(widget).imd5L, -88.0f);
        // The upper fifth marker belongs at display pixel 700, not a recropped index.
        const ImdMeasurementFrame& frame = widget.imdMeasurementFrame();
        QVERIFY(frame.peaks[5]);
        const QRect trace(0, 0, 799, 360);
        const int expectedX = ImdSpectrumTestAccess::frequencyX(
            widget, frame.peaks[5]->frequencyHz, trace);
        QVERIFY(std::abs(expectedX - 700) <= 1);
        const QColor expectedInk = PureSignalViewStyle::product(5);
        int markerPixels = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = expectedX - 6; x <= expectedX + 6; ++x) {
                const QColor colour = image.pixelColor(x, y);
                if (colour.alpha() > 200
                    && std::abs(colour.red() - expectedInk.red()) <= 3
                    && std::abs(colour.green() - expectedInk.green()) <= 3
                    && std::abs(colour.blue() - expectedInk.blue()) <= 3) {
                    ++markerPixels;
                }
            }
        }
        QVERIFY2(markerPixels > 8,
                 "The upper fifth marker must be rendered at its visible-plane coordinate");
    }
    void fullFftFrameUsesTheVisiblePlane_data()
    {
        QTest::addColumn<double>("offsetHz");
        QTest::newRow("quarter-span") << 0.0;
        QTest::newRow("ctun-offset") << 12000.0;
    }
    void fullFftFrameUsesTheVisiblePlane()
    {
        QFETCH(double, offsetHz);
        SpectrumWidget widget;
        configure(widget, offsetHz);
        // 3200 full-band bins yield 800 display pixels in a quarter-band view.
        QVector<float> bins(3200, 1e-12f);
        const int first = 1200 + static_cast<int>(offsetHz / 60.0);
        for (size_t i = 0; i < kPositions.size(); ++i) {
            bins[first + kPositions[i]] = std::pow(10.0f, kLevels[i] / 10.0f);
        }
        widget.updateSpectrumLinear(0, bins, 1.0, 0.0);
        QCOMPARE(ImdSpectrumTestAccess::visiblePixels(widget).size(), 800);
        const auto& pixels = ImdSpectrumTestAccess::visiblePixels(widget);
        std::vector<float> frame(pixels.begin(), pixels.end());
        const auto peaks = ImdOverlay::detectPeaks(frame, 10.0f);
        QCOMPARE(peaks.size(), size_t(6));
        for (size_t i = 0; i < peaks.size(); ++i) {
            QVERIFY(std::abs(peaks[i].x - kPositions[i]) <= 2);
        }
        ImdSpectrumTestAccess::render(widget);
        QVERIFY2(ImdSpectrumTestAccess::readout(widget).valid,
                 "Live full-FFT reduction must retain all six visible products through IMD rendering");
    }
    void gateOffClearsCpuOverlay_data()
    {
        QTest::addColumn<bool>("showGate");
        QTest::newRow("show-measurements") << true;
        QTest::newRow("two-tone") << false;
    }
    void gateOffClearsCpuOverlay()
    {
        QFETCH(bool, showGate);
        SpectrumWidget widget;
        configure(widget);
        widget.setDisplayWindowPreservingHistory(14200000.0, 192000.0);
        ImdSpectrumTestAccess::seedVisiblePixels(widget, visibleFrame());
        const QImage before = ImdSpectrumTestAccess::render(widget);
        QVERIFY(ImdSpectrumTestAccess::readout(widget).valid);
        if (showGate) { widget.setShowIMDMeasurements(false); }
        else { widget.setTestingIMD(false); }
        QVERIFY(!ImdSpectrumTestAccess::readout(widget).valid);
        QImage blank(before.size(), before.format());
        blank.fill(Qt::transparent);
        QCOMPARE(ImdSpectrumTestAccess::render(widget), blank);
    }
    void missingProductsDrawNoCpuOverlay()
    {
        SpectrumWidget widget;
        configure(widget);
        ImdSpectrumTestAccess::seedVisiblePixels(widget, QVector<float>(800, -120.0f));
        QImage blank(800, 360, QImage::Format_ARGB32_Premultiplied);
        blank.fill(Qt::transparent);
        QCOMPARE(ImdSpectrumTestAccess::render(widget), blank);
        QVERIFY(!ImdSpectrumTestAccess::readout(widget).valid);
    }
};
QTEST_MAIN(TestImdSpectrumIntegration)
#include "tst_imd_spectrum_integration.moc"
