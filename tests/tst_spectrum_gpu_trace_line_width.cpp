// no-port-check: NereusSDR-original test. The GPU path draws the trace at its line width.
//
// =================================================================
// tests/tst_spectrum_gpu_trace_line_width.cpp  (NereusSDR)
// =================================================================
//
// The GPU path drew the live trace as a line strip, one device pixel wide
// whatever the Line Width said (QRhi has no portable line width). It now
// draws the ribbon SpectrumTraceRibbon.h builds: the line width wide, and
// below one device pixel one pixel wide with its alpha scaled by the width.
//
// This grabs real GPU frames at 0.3, 1.0 and 4.0 px and compares how much
// of the trace's colour each holds, so it needs a native window (the
// offscreen platform gives the widget no GPU backend) and carries the `gpu`
// label; it skips where no QRhi comes up. The geometry itself is checked
// without a GPU in tst_spectrum_widget_additions (the lineWidth_ slots).
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-11 — Created for the 0.1 to 5 px trace line width. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#include <QImage>
#include <QSignalSpy>
#include <QTest>
#include <QVector>

#include <algorithm>
#include <cmath>

#include "NativeWindowFrames.h"
#include "gui/SpectrumWidget.h"

using namespace NereusSDR;
using NereusSDR::NativeWindowFrames::FrameWait;

namespace {

// Pure red: nothing else the spectrum draws (background, grid, scale text)
// is redder than it is green or blue.
const QColor kTraceColour(255, 0, 0);
constexpr float kMinDbm = -140.0f;
constexpr float kMaxDbm = -40.0f;
constexpr int kFrameTimeoutMs = 2000;
constexpr int kSettleFrames = 3;

// A trace that climbs, falls and runs level across the middle of the range.
QVector<float> zigzagTrace(int n = 96)
{
    QVector<float> row(n);
    const float mid = (kMinDbm + kMaxDbm) / 2.0f;
    const float swing = (kMaxDbm - kMinDbm) / 4.0f;
    for (int j = 0; j < n; ++j) {
        const int phase = j % 8;
        const float t = (phase < 4) ? static_cast<float>(phase) / 4.0f
                                    : static_cast<float>(8 - phase) / 4.0f;
        row[j] = (j < n / 2) ? mid + swing * (t - 0.5f) : mid;
    }
    return row;
}

// How much trace colour a frame holds inside the spectrum: for every pixel,
// how much redder it is than it is green or blue, summed. All three
// channels are read; a grey or white pixel adds nothing.
qint64 traceInk(const SpectrumWidget& w, const QImage& frame)
{
    const QImage rgb = frame.convertToFormat(QImage::Format_RGB32);
    const qreal dpr = static_cast<qreal>(rgb.height()) / qMax(1, w.height());
    const QRect spec = w.spectrumRectForTest();
    const int top = qMax(0, static_cast<int>(std::ceil(spec.top() * dpr)));
    const int bottom = qMin(rgb.height() - 1, static_cast<int>(std::floor(spec.bottom() * dpr)));
    const int left = qMax(0, static_cast<int>(std::ceil(spec.left() * dpr)));
    const int right = qMin(rgb.width() - 1, static_cast<int>(std::floor(spec.right() * dpr)));
    qint64 ink = 0;
    for (int y = top; y <= bottom; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
        for (int x = left; x <= right; ++x) {
            ink += qMax(0, qRed(line[x]) - qMax(qGreen(line[x]), qBlue(line[x])));
        }
    }
    return ink;
}

// "Measurably less": under 60% of the reference.
bool isMeasurablyLess(qint64 ink, qint64 reference)
{
    return ink * 10 < reference * 6;
}

}  // namespace

class TestSpectrumGpuTraceLineWidth : public QObject {
    Q_OBJECT

private:
    enum class Stage { Rendered, NoGpu, Failed };

    // Sets the width, renders a few GPU frames and reads the last one's ink.
    static Stage inkAtWidth(SpectrumWidget& w, QSignalSpy& submitted, float widthPx,
                            qint64* ink, QString* why)
    {
        w.setLineWidth(widthPx);
        for (int i = 0; i < kSettleFrames; ++i) {
            w.setRenderedPixelsForTest(zigzagTrace());
            const FrameWait wait =
                NativeWindowFrames::requestFrame(w, submitted, kFrameTimeoutMs);
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
        }
        const QImage frame = w.grabFramebuffer();
        if (frame.isNull()) {
            *why = QStringLiteral("the framebuffer grab came back empty");
            return Stage::Failed;
        }
        *ink = traceInk(w, frame);
        return Stage::Rendered;
    }

private slots:
    void thinnerBelowOnePixel_widerAbove()
    {
        SpectrumWidget w;
        w.resize(1000, 600);
        w.setSpectrumRenderMode(static_cast<int>(SpectrumRenderMode::Mode2D));
        w.setDbmRange(kMinDbm, kMaxDbm);
        w.setPanFillEnabled(false);
        w.setGradientEnabled(false);
        w.setTraceColor(kTraceColour);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));

        QSignalSpy submitted(&w, &QRhiWidget::frameSubmitted);
        QString why;
        qint64 one = 0;
        Stage stage = inkAtWidth(w, submitted, 1.0f, &one, &why);
        if (stage == Stage::NoGpu) {
            QSKIP("no QRhi on this platform; the GPU frame path did not run");
        }
        QVERIFY2(stage == Stage::Rendered, qPrintable(why));
        QVERIFY2(one > 0, "the trace was not drawn at 1.0 px; the test proves nothing");

        qint64 thin = 0;
        QVERIFY2(inkAtWidth(w, submitted, 0.3f, &thin, &why) == Stage::Rendered, qPrintable(why));
        qint64 wide = 0;
        QVERIFY2(inkAtWidth(w, submitted, 4.0f, &wide, &why) == Stage::Rendered, qPrintable(why));
        qint64 oneAgain = 0;
        QVERIFY2(inkAtWidth(w, submitted, 1.0f, &oneAgain, &why) == Stage::Rendered,
                 qPrintable(why));

        const QString inks = QStringLiteral("ink at 0.3 px %1, at 1.0 px %2 and %3, at 4.0 px %4")
                                 .arg(thin).arg(one).arg(oneAgain).arg(wide);
        qInfo().noquote() << inks;

        // The check can fail: 1.0 px against 1.0 px is not "measurably less".
        QVERIFY2(!isMeasurablyLess(oneAgain, one), qPrintable(inks));
        QVERIFY2(thin > 0, qPrintable(inks));
        QVERIFY2(isMeasurablyLess(thin, one), qPrintable(inks));
        // 4.0 px holds about four times the ink of 1.0 px: at least three
        // times, and no more than five.
        QVERIFY2(wide > one * 3, qPrintable(inks));
        QVERIFY2(wide < one * 5, qPrintable(inks));
    }
};

QTEST_MAIN(TestSpectrumGpuTraceLineWidth)
#include "tst_spectrum_gpu_trace_line_width.moc"
