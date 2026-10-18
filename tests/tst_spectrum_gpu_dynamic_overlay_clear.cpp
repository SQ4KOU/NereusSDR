// no-port-check: NereusSDR-original. The GPU dynamic overlay shows no
// pixels from an earlier frame below the spectrum.
//
// 2026-10-09, J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//
// JJ saw copies of the magenta dashed noise-floor line, each with its handle,
// over the frequency scale and the top of the waterfall, while the right one
// sat in the spectrum. On the GPU path the noise floor, peak-hold trace and
// peak blobs are painted into a per-frame dynamic overlay texture that covers
// the whole widget, but renderGpuFrame() cleared and uploaded only the rows of
// the current spectrum height. When the spectrum got shorter (setSpectrumFrac,
// the divider drag, a re-layout) the lines drawn while it was taller stayed in
// the rows below, in the image and in the texture.
//
// Needs a real QRhi: the offscreen platform gives the widget no GPU backend,
// so renderGpuFrame() never runs there. Registered NATIVE_WINDOW with the
// extra `gpu` label; it skips where no QRhi comes up.

#include <QImage>
#include <QSignalSpy>
#include <QTest>
#include <QVector>
#include <cmath>

#include "NativeWindowFrames.h"
#include "gui/SpectrumWidget.h"
#include "support/ImdSpectrumTestAccess.h"

using namespace NereusSDR;
using NereusSDR::NativeWindowFrames::FrameWait;

namespace {

// A noise-floor colour nothing else in the widget draws: not a trace,
// grid, chrome or waterfall palette colour.
const QColor kNfColour(250, 3, 140);
// Per-channel tolerance when matching kNfColour in a grabbed frame. The
// dynamic overlay is drawn without antialiasing and sampled 1:1.
constexpr int kColourTolerance = 3;
// The dBm range the test pins, and a floor near its bottom, so the line
// drawn in a tall spectrum lies below where a short spectrum ends.
constexpr float kMinDbm = -140.0f;
constexpr float kMaxDbm = -40.0f;
constexpr float kFloorDbm = -128.0f;
// Frames per stage. The dynamic overlay is rebuilt at most every 33 ms
// (updateSpectrumLinear's 30 Hz cap), so frames are spaced to let each one
// rebuild it.
constexpr int kFramesPerStage = 20;
constexpr int kFrameSpacingMs = 40;
constexpr int kFrameTimeoutMs = 2000;

QVector<float> syntheticBins()
{
    constexpr int fftSize = 4096;
    QVector<float> bins(fftSize, 1e-12f);
    bins[2048] = 1e-3f;
    for (int i = 0; i < fftSize; ++i) {
        bins[i] += static_cast<float>(std::abs(std::sin(i * 0.01f))) * 1e-11f;
    }
    return bins;
}

bool isNfColour(const QColor& c)
{
    return std::abs(c.red() - kNfColour.red()) <= kColourTolerance
        && std::abs(c.green() - kNfColour.green()) <= kColourTolerance
        && std::abs(c.blue() - kNfColour.blue()) <= kColourTolerance;
}

struct NfRows {
    int count{0};      // noise-floor-coloured pixels in the scanned rows
    int lowestRow{-1}; // device row of the lowest one, -1 when none
};

// Scans device rows [firstRow, lastRow] of `img` for noise-floor pixels.
NfRows scanNfRows(const QImage& img, int firstRow, int lastRow)
{
    NfRows rows;
    const QImage rgb = img.convertToFormat(QImage::Format_RGB32);
    const int top = qMax(0, firstRow);
    const int bottom = qMin(rgb.height() - 1, lastRow);
    for (int y = top; y <= bottom; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
        for (int x = 0; x < rgb.width(); ++x) {
            if (isNfColour(QColor(line[x]))) {
                ++rows.count;
                rows.lowestRow = y;
            }
        }
    }
    return rows;
}

// The first device row of `img` below the widget's current spectrum.
int firstRowBelowSpectrum(const SpectrumWidget& w, const QImage& img)
{
    const qreal dpr = static_cast<qreal>(img.height()) / qMax(1, w.height());
    return static_cast<int>(std::ceil((w.spectrumRectForTest().bottom() + 1) * dpr));
}

}  // namespace

class TestSpectrumGpuDynamicOverlayClear : public QObject {
    Q_OBJECT

private:
    enum class Stage { Rendered, NoGpu, Failed };

    // Feeds kFramesPerStage spectrum frames, each followed by a submitted
    // GPU frame. The measured floor is pinned after each feed so the line
    // sits at kFloorDbm whatever the estimator made of the synthetic bins.
    static Stage renderStage(SpectrumWidget& w, QSignalSpy& submitted,
                             const QVector<float>& bins, QString* why)
    {
        for (int i = 0; i < kFramesPerStage; ++i) {
            w.updateSpectrumLinear(0, bins, 2.0, -10.0);
            w.setMeasuredNoiseFloorForTest(kFloorDbm);
            int raises = 0;
            const FrameWait wait = NativeWindowFrames::requestFrame(
                w, submitted, kFrameTimeoutMs, &raises);
            if (wait == FrameWait::Hidden) {
                *why = QString::fromLatin1(NativeWindowFrames::kHiddenFailure);
                return Stage::Failed;
            }
            if (wait == FrameWait::Stalled) {
                if (submitted.isEmpty()) {
                    return Stage::NoGpu;
                }
                *why = QString::fromLatin1(NativeWindowFrames::kStalledFailure);
                return Stage::Failed;
            }
            QTest::qWait(kFrameSpacingMs);
        }
        return Stage::Rendered;
    }

    static void configure(SpectrumWidget& w)
    {
        w.setSpectrumRenderMode(static_cast<int>(SpectrumRenderMode::Mode2D));
        w.setDbmRange(kMinDbm, kMaxDbm);
        w.setNoiseFloorColor(kNfColour);
        w.setNoiseFloorTextColor(kNfColour);
        w.setNoiseFloorFastColor(kNfColour);
        w.setNoiseFloorFastAttack(false);
        w.setShowNoiseFloor(true);
    }

    // Renders a tall spectrum, makes it shorter with `shrink`, renders again
    // and checks that no noise-floor pixel is left below the spectrum.
    template <typename Shrink>
    static void verifyNoStaleFloorBelowSpectrum(SpectrumWidget& w, Shrink shrink)
    {
        QSignalSpy submitted(&w, &QRhiWidget::frameSubmitted);
        const QVector<float> bins = syntheticBins();
        QString why;

        Stage stage = renderStage(w, submitted, bins, &why);
        if (stage == Stage::NoGpu) {
            QSKIP("no QRhi on this platform; the GPU frame path did not run");
        }
        QVERIFY2(stage == Stage::Rendered, qPrintable(why));

        const QImage tall = w.grabFramebuffer();
        QVERIFY(!tall.isNull());
        const NfRows tallRows = scanNfRows(tall, 0, firstRowBelowSpectrum(w, tall) - 1);
        QVERIFY2(tallRows.count > 0,
                 "the noise floor was not drawn in the tall spectrum; the test proves nothing");

        shrink(w);

        stage = renderStage(w, submitted, bins, &why);
        QVERIFY2(stage == Stage::Rendered, qPrintable(why));

        const QImage shortFrame = w.grabFramebuffer();
        QVERIFY(!shortFrame.isNull());
        const int belowRow = firstRowBelowSpectrum(w, shortFrame);
        QVERIFY2(tallRows.lowestRow >= belowRow,
                 qPrintable(QStringLiteral(
                     "the tall line (device row %1 of %2) does not lie below the short "
                     "spectrum (ends before row %3 of %4); the test proves nothing")
                     .arg(tallRows.lowestRow).arg(tall.height())
                     .arg(belowRow).arg(shortFrame.height())));

        const NfRows nowAbove = scanNfRows(shortFrame, 0, belowRow - 1);
        QVERIFY2(nowAbove.count > 0,
                 "the noise floor was not drawn in the short spectrum");

        const NfRows stale = scanNfRows(shortFrame, belowRow, shortFrame.height() - 1);
        QVERIFY2(stale.count == 0,
                 qPrintable(QStringLiteral(
                     "%1 noise-floor pixels below the spectrum (lowest at device row %2; "
                     "the spectrum ends before row %3)")
                     .arg(stale.count).arg(stale.lowestRow).arg(belowRow)));
    }

private slots:
    void imdGateOffInvalidatesAndClearsTexture_data()
    {
        QTest::addColumn<bool>("showGate");
        QTest::newRow("show-measurements") << true;
        QTest::newRow("two-tone") << false;
    }

    void imdGateOffInvalidatesAndClearsTexture()
    {
        QFETCH(bool, showGate);
        SpectrumWidget w;
        w.resize(800, 600);
        w.setSpectrumRenderMode(static_cast<int>(SpectrumRenderMode::Mode2D));
        w.setSampleRate(192000.0);
        w.setDisplayDuplex(true);
        w.setMoxOverlay(true);
        w.setTestingIMD(true);
        w.setShowIMDMeasurements(true);
        w.setDbmRange(-140.0f, 0.0f);
        w.setShowNoiseFloor(false);
        QVector<float> pixels(800, -120.0f);
        const int positions[] = {100, 220, 340, 460, 580, 700};
        const float levels[] = {-88.0f, -65.0f, -22.0f, -22.0f, -65.0f, -88.0f};
        for (int i = 0; i < 6; ++i) { pixels[positions[i]] = levels[i]; }
        ImdSpectrumTestAccess::seedVisiblePixels(w, pixels);
        // First assert invalidation without a native window so this independently
        // pins the cache bug even on a host where QRhi cannot submit a frame.
        w.clearOverlayStaticDirtyForTest();
        if (showGate) { w.setShowIMDMeasurements(false); }
        else { w.setTestingIMD(false); }
        QVERIFY2(w.overlayStaticDirtyForTest(), "Gate-off must invalidate the IMD-bearing GPU static overlay");
        if (showGate) { w.setShowIMDMeasurements(true); }
        else { w.setTestingIMD(true); }
        QSignalSpy submitted(&w, &QRhiWidget::frameSubmitted);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        // Gate enable and native resize retire the old measurement. Accept
        // an explicit new synthetic receiver plane before drawing the cache.
        QVERIFY(!w.imdMeasurementFrame().summary);
        ImdSpectrumTestAccess::seedVisiblePixels(w, pixels);
        FrameWait wait = NativeWindowFrames::requestFrame(w, submitted, kFrameTimeoutMs);
        if (wait == FrameWait::Stalled && submitted.isEmpty()) {
            QSKIP("no QRhi; invalidation was tested but native texture clearing remains unverified");
        }
        QVERIFY2(wait == FrameWait::Submitted, "Native GPU frame must arrive for a valid IMD texture");
        QVERIFY(ImdSpectrumTestAccess::readout(w).valid);
        const auto countMarkerPixels = [&w](const QImage& image) {
            int count = 0;
            // Frequency-scale labels are yellow while keyed. IMD markers
            // belong to the spectrum, so exclude that separate chrome band.
            const int endRow = qMin(image.height(), firstRowBelowSpectrum(w, image));
            for (int y = 0; y < endRow; ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    const QColor c = image.pixelColor(x, y);
                    if (c.red() > 180 && c.green() > 145 && c.green() < 230 && c.blue() < 115) { ++count; }
                }
            }
            return count;
        };
        const QImage before = w.grabFramebuffer();
        QVERIFY(!before.isNull());
        QVERIFY2(countMarkerPixels(before) > 0, "Valid GPU image must contain the amber fundamental markers");
        w.clearOverlayStaticDirtyForTest();
        if (showGate) { w.setShowIMDMeasurements(false); }
        else { w.setTestingIMD(false); }
        QVERIFY(w.overlayStaticDirtyForTest());
        QCOMPARE(ImdSpectrumTestAccess::mox(w), true);
        wait = NativeWindowFrames::requestFrame(w, submitted, kFrameTimeoutMs);
        QVERIFY(wait == FrameWait::Submitted);
        const QImage after = w.grabFramebuffer();
        QVERIFY(!after.isNull());
        qInfo() << "IMD amber pixels before/after" << countMarkerPixels(before) << countMarkerPixels(after);
        const QString evidence = qEnvironmentVariable("NEREUS_IMD_EVIDENCE");
        if (!evidence.isEmpty()) {
            const QString gate = showGate ? QStringLiteral("show") : QStringLiteral("testing");
            QVERIFY(before.save(evidence + QStringLiteral("/gpu-%1-before.png").arg(gate)));
            QVERIFY(after.save(evidence + QStringLiteral("/gpu-%1-after.png").arg(gate)));
        }
        QCOMPARE(countMarkerPixels(after), 0);
    }
    // JJ's case: the spectrum gets shorter inside an unchanged widget.
    void spectrumFracShrink_leavesNoFloorBelowSpectrum()
    {
        SpectrumWidget w;
        w.resize(1000, 600);
        configure(w);
        w.setSpectrumFrac(0.8f);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        verifyNoStaleFloorBelowSpectrum(w, [](SpectrumWidget& sw) {
            sw.setSpectrumFrac(0.4f);
        });
    }

    // The widget gets shorter: the overlay texture is recreated at the new
    // size and must not show memory it was never given.
    void widgetResizeShrink_leavesNoFloorBelowSpectrum()
    {
        SpectrumWidget w;
        w.resize(1000, 700);
        configure(w);
        w.setSpectrumFrac(0.8f);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        verifyNoStaleFloorBelowSpectrum(w, [](SpectrumWidget& sw) {
            sw.resize(1000, 350);
        });
    }
};

QTEST_MAIN(TestSpectrumGpuDynamicOverlayClear)
#include "tst_spectrum_gpu_dynamic_overlay_clear.moc"
