// no-port-check: NereusSDR-original. GPU spectrum frames must not grow the heap.
//
// The desktop window grew to 23 GB in 2.5 hours on a 3D pan: every GPU frame
// wrote the 2D trace's line and fill vertices into their dynamic buffers
// (QRhiResourceUpdateBatch::updateDynamicBuffer) although 3D mode never binds
// them. Qt's Metal backend keeps each partial dynamic-buffer write in a
// per-buffer pending list that it drains only when the buffer is bound
// (QRhiMetal::executeBufferHostWritesForSlot, qrhimetal.mm, Qt 6.11), so an
// unbound buffer written every frame keeps every frame's vertex copy. The
// same held for the fill buffer with pan fill off in 2D.
//
// Needs a real QRhi: the offscreen platform gives the widget no GPU backend,
// so renderGpuFrame() never runs there, and the Null backend applies writes
// at once. Registered NATIVE_WINDOW with the extra `gpu` label; it skips where
// no QRhi comes up, and reads heap statistics on macOS only (the Metal path
// is where the pending list lives).

#include <QTest>
#include <QSignalSpy>
#include <QVector>
#include <cmath>
#include <optional>

#ifdef Q_OS_MAC
#include <malloc/malloc.h>
#endif

#include "gui/SpectrumWidget.h"

using namespace NereusSDR;

namespace {

// Frames to warm up (pipelines, textures, the 3D ring's first uploads), then
// frames to measure over. At about 1000 display pixels the leak was about
// 72 KB a frame in 3D (line and fill) and 48 KB with pan fill off, so the
// measured run leaked 15 to 23 MB before the fix.
constexpr int kWarmupFrames = 40;
constexpr int kMeasuredFrames = 300;
// Steady-state rendering allocates and frees per frame; what it keeps must
// stay far below one frame's leak times the run. 4 MB is under a third of
// the smallest leak this test caught.
constexpr qint64 kGrowthBudgetBytes = 4 * 1024 * 1024;

qint64 heapInUse()
{
#ifdef Q_OS_MAC
    malloc_statistics_t stats{};
    malloc_zone_statistics(nullptr, &stats);
    return static_cast<qint64>(stats.size_in_use);
#else
    return -1;
#endif
}

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

}  // namespace

class TestSpectrumGpuBufferGrowth : public QObject {
    Q_OBJECT

private:
    // Feeds one spectrum frame and waits for the widget to submit a GPU
    // frame for it. False when no frame came within the timeout.
    static bool renderOneFrame(SpectrumWidget& w, QSignalSpy& submitted,
                               const QVector<float>& bins)
    {
        const int before = submitted.count();
        w.updateSpectrumLinear(0, bins, 2.0, -10.0);
        w.update();
        return QTest::qWaitFor([&] { return submitted.count() > before; }, 2000);
    }

    // Returns the heap growth across kMeasuredFrames GPU frames (negative
    // when the heap shrank), or nothing when the platform gave the widget no
    // QRhi (no frame is ever submitted, and the caller skips).
    static std::optional<qint64> measureGrowth(SpectrumWidget& w)
    {
        QSignalSpy submitted(&w, &QRhiWidget::frameSubmitted);
        const QVector<float> bins = syntheticBins();
        for (int i = 0; i < kWarmupFrames; ++i) {
            if (!renderOneFrame(w, submitted, bins)) {
                return std::nullopt;
            }
        }
        const qint64 before = heapInUse();
        for (int i = 0; i < kMeasuredFrames; ++i) {
            if (!renderOneFrame(w, submitted, bins)) {
                return std::nullopt;
            }
        }
        const qint64 after = heapInUse();
        qInfo().noquote() << QStringLiteral(
            "%1 GPU frames, %2 display pixels, QRhiWidget api %3: heap in use %4 -> %5 bytes (growth %6)")
            .arg(kMeasuredFrames)
            .arg(w.renderedPixels().size())
            .arg(static_cast<int>(w.api()))
            .arg(before).arg(after).arg(after - before);
        return after - before;
    }

    static void showWidget(SpectrumWidget& w)
    {
        w.resize(1000, 600);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
    }

private slots:
    void initTestCase()
    {
        if (heapInUse() < 0) {
            QSKIP("heap statistics are read on macOS only (the Metal backend "
                  "holds the pending dynamic-buffer writes)");
        }
    }

    // JJ's case: a 3D pan. The 2D trace buffers are never bound in 3D.
    void mode3D_framesDoNotGrowTheHeap()
    {
        SpectrumWidget w;
        showWidget(w);
        w.setSpectrumRenderMode(static_cast<int>(SpectrumRenderMode::Mode3D));
        const std::optional<qint64> measured = measureGrowth(w);
        if (!measured) {
            QSKIP("no QRhi on this platform; the GPU frame path did not run");
        }
        const qint64 growth = *measured;
        QVERIFY2(growth < kGrowthBudgetBytes,
                 qPrintable(QStringLiteral("heap grew %1 bytes over %2 3D frames")
                                .arg(growth).arg(kMeasuredFrames)));
    }

    // A 2D pan with pan fill off never binds the fill buffer.
    void mode2DFillOff_framesDoNotGrowTheHeap()
    {
        SpectrumWidget w;
        showWidget(w);
        w.setSpectrumRenderMode(static_cast<int>(SpectrumRenderMode::Mode2D));
        w.setPanFillEnabled(false);
        const std::optional<qint64> measured = measureGrowth(w);
        if (!measured) {
            QSKIP("no QRhi on this platform; the GPU frame path did not run");
        }
        const qint64 growth = *measured;
        QVERIFY2(growth < kGrowthBudgetBytes,
                 qPrintable(QStringLiteral("heap grew %1 bytes over %2 2D fill-off frames")
                                .arg(growth).arg(kMeasuredFrames)));
    }

    // Control: a 2D pan with fill and peak hold binds every buffer it writes.
    void mode2DFillOnPeakHold_framesDoNotGrowTheHeap()
    {
        SpectrumWidget w;
        showWidget(w);
        w.setSpectrumRenderMode(static_cast<int>(SpectrumRenderMode::Mode2D));
        w.setPanFillEnabled(true);
        w.setPeakHoldEnabled(true);
        const std::optional<qint64> measured = measureGrowth(w);
        if (!measured) {
            QSKIP("no QRhi on this platform; the GPU frame path did not run");
        }
        const qint64 growth = *measured;
        QVERIFY2(growth < kGrowthBudgetBytes,
                 qPrintable(QStringLiteral("heap grew %1 bytes over %2 2D frames")
                                .arg(growth).arg(kMeasuredFrames)));
    }
};

QTEST_MAIN(TestSpectrumGpuBufferGrowth)
#include "tst_spectrum_gpu_buffer_growth.moc"
