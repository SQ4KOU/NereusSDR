// SPDX-License-Identifier: GPL-3.0-or-later
//
// no-port-check: Test references fabricated DX-cluster callsigns
// (TEST1..TEST9, OVR1..OVR8) as fixtures so the collision and
// overflow paths can be exercised deterministically without real
// amateur callsigns. Precedent: B2-B6, C1-C4, D1-D5.
//
// NereusSDR - SpectrumWidget spot overlay render tests
//
// Phase 3J-2 Task E1. Pins the contract that drawSpotMarkers
// places spot labels with collision-avoiding multi-level stacking,
// overflows into +N cluster badges at maxBottom + 2, and exposes
// click-to-tune hit-test rectangles via spotClickRectsForTest()
// plus cluster badge rectangles via spotClustersForTest().
//
// Tests assert geometry not exact pixels: count of click rects,
// count of cluster badges, vertical stack offsets, and overflow
// thresholds. Pixel sampling stays minimal because the CPU
// QPainter path renders at multi-platform Qt versions.
//
// Eight tests:
//   - emptyOverlayDrawsNothing
//   - singleSpotDrawsOneLabel
//   - overlappingSpotsStackVertically
//   - overflowSpotsBecomeClusterBadge
//   - clickRectAtSpotXTuneable
//   - rejectsBeyondVisibleRange
//   - spotListChangeInvalidatesStaticOverlay
//   - spotDisplayInputsInvalidateStaticOverlay
//
// Modification history (NereusSDR):
//   2026-10-09 - The GPU path draws the spot labels into its cached static
//                overlay: a spot list change and every spot display input
//                must invalidate it, not only repaint. J.J. Boyd (KG4VCF),
//                AI-assisted via Anthropic Claude Code.

#include <QtTest>
#include <QImage>
#include <QPainter>
#include <QSignalSpy>

#include <functional>

#include "gui/SpectrumWidget.h"

using namespace NereusSDR;

namespace {

// Render a SpectrumWidget's spot overlay into an offscreen image at
// a known geometry. Returns the image so individual tests can sample
// pixels if they want, but the primary assertion path is via the
// click-rect / cluster vectors the renderer publishes.
QImage renderSpots(SpectrumWidget& sw, const QRect& specRect)
{
    QImage img(specRect.size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::black);
    QPainter p(&img);
    p.translate(-specRect.topLeft());
    sw.drawSpotMarkersForTest(p, specRect);
    p.end();
    return img;
}

SpectrumWidget::SpotMarker makeSpot(int idx, const QString& callsign, double freqMhz)
{
    SpectrumWidget::SpotMarker m;
    m.index = idx;
    m.callsign = callsign;
    m.freqMhz = freqMhz;
    return m;
}

} // namespace

class TestSpotOverlayRender : public QObject {
    Q_OBJECT
private slots:
    void emptyOverlayDrawsNothing();
    void singleSpotDrawsOneLabel();
    void overlappingSpotsStackVertically();
    void overflowSpotsBecomeClusterBadge();
    void clickRectAtSpotXTuneable();
    void rejectsBeyondVisibleRange();
    void spotListChangeInvalidatesStaticOverlay();
    void spotDisplayInputsInvalidateStaticOverlay();
};

void TestSpotOverlayRender::emptyOverlayDrawsNothing()
{
    SpectrumWidget sw;
    sw.resize(800, 400);
    // Center 14.250 MHz, 96 kHz bandwidth (typical 20m phone window).
    sw.setFrequencyRange(14'250'000.0, 96'000.0);

    QCOMPARE(sw.spotMarkersForTest().size(), 0);
    sw.setSpotMarkers({});

    const QRect specRect(0, 0, 800, 200);
    renderSpots(sw, specRect);

    QCOMPARE(sw.spotClickRectsForTest().size(), 0);
    QCOMPARE(sw.spotClustersForTest().size(), 0);
}

void TestSpotOverlayRender::singleSpotDrawsOneLabel()
{
    SpectrumWidget sw;
    sw.resize(800, 400);
    sw.setFrequencyRange(14'250'000.0, 96'000.0);

    QVector<SpectrumWidget::SpotMarker> markers;
    markers.append(makeSpot(1, QStringLiteral("TEST1"), 14.250));
    sw.setSpotMarkers(markers);

    const QRect specRect(0, 0, 800, 200);
    renderSpots(sw, specRect);

    QCOMPARE(sw.spotClickRectsForTest().size(), 1);
    QCOMPARE(sw.spotClustersForTest().size(), 0);
    QCOMPARE(sw.spotClickRectsForTest().first().freqMhz, 14.250);
    QCOMPARE(sw.spotClickRectsForTest().first().markerIndex, 0);
}

void TestSpotOverlayRender::overlappingSpotsStackVertically()
{
    SpectrumWidget sw;
    sw.resize(800, 400);
    // 96 kHz across 800 px -> 120 Hz/pixel. Spotting three callsigns
    // within a few hundred Hz collides their labels horizontally so
    // the stacking pass must nudge them down to fit within max levels.
    sw.setFrequencyRange(14'250'000.0, 96'000.0);

    QVector<SpectrumWidget::SpotMarker> markers;
    markers.append(makeSpot(1, QStringLiteral("TEST1"), 14.2500));
    markers.append(makeSpot(2, QStringLiteral("TEST2"), 14.2502));
    markers.append(makeSpot(3, QStringLiteral("TEST3"), 14.2504));
    sw.setSpotMarkers(markers);

    const QRect specRect(0, 0, 800, 200);
    renderSpots(sw, specRect);

    QCOMPARE(sw.spotClickRectsForTest().size(), 3);
    QCOMPARE(sw.spotClustersForTest().size(), 0);

    // Each subsequent label nudged below the previous one (different y).
    const auto& rects = sw.spotClickRectsForTest();
    const int y0 = rects[0].rect.top();
    const int y1 = rects[1].rect.top();
    const int y2 = rects[2].rect.top();
    QVERIFY(y1 > y0);
    QVERIFY(y2 > y1);
}

void TestSpotOverlayRender::overflowSpotsBecomeClusterBadge()
{
    SpectrumWidget sw;
    sw.resize(800, 400);
    sw.setFrequencyRange(14'250'000.0, 96'000.0);
    // Force max levels = 2 so the 3rd-and-beyond colliding spot
    // overflows into a +N cluster badge.
    sw.setSpotMaxLevels(2);

    QVector<SpectrumWidget::SpotMarker> markers;
    // Eight spots within a few hundred Hz of each other; only 2
    // fit within the level cap, the rest go to the overflow group.
    for (int i = 0; i < 8; ++i) {
        markers.append(makeSpot(i + 1,
                                QStringLiteral("OVR%1").arg(i + 1),
                                14.2500 + i * 0.0002));
    }
    sw.setSpotMarkers(markers);

    const QRect specRect(0, 0, 800, 200);
    renderSpots(sw, specRect);

    // Two within the level cap.
    QCOMPARE(sw.spotClickRectsForTest().size(), 2);
    // Six overflowed into a single cluster badge (40-px ClusterBinWidth
    // groups them together at this zoom).
    QCOMPARE(sw.spotClustersForTest().size(), 1);
    QCOMPARE(sw.spotClustersForTest().first().spots.size(), 6);
}

void TestSpotOverlayRender::clickRectAtSpotXTuneable()
{
    SpectrumWidget sw;
    sw.resize(800, 400);
    sw.setFrequencyRange(14'250'000.0, 96'000.0);

    QVector<SpectrumWidget::SpotMarker> markers;
    markers.append(makeSpot(7, QStringLiteral("TEST7"), 14.260));
    sw.setSpotMarkers(markers);

    const QRect specRect(0, 0, 800, 200);
    renderSpots(sw, specRect);

    QCOMPARE(sw.spotClickRectsForTest().size(), 1);
    const auto& hr = sw.spotClickRectsForTest().first();
    QCOMPARE(hr.freqMhz, 14.260);

    // Hit point at the click rect center should fall inside the rect.
    const QPoint center = hr.rect.center();
    QVERIFY(hr.rect.contains(center));
}

void TestSpotOverlayRender::rejectsBeyondVisibleRange()
{
    SpectrumWidget sw;
    sw.resize(800, 400);
    sw.setFrequencyRange(14'250'000.0, 96'000.0);

    QVector<SpectrumWidget::SpotMarker> markers;
    markers.append(makeSpot(1, QStringLiteral("TEST1"), 14.250));     // visible
    markers.append(makeSpot(2, QStringLiteral("TESTLO"), 7.000));      // far below
    markers.append(makeSpot(3, QStringLiteral("TESTHI"), 28.000));     // far above
    sw.setSpotMarkers(markers);

    const QRect specRect(0, 0, 800, 200);
    renderSpots(sw, specRect);

    // Only the in-range spot should produce a click rect.
    QCOMPARE(sw.spotClickRectsForTest().size(), 1);
    QCOMPARE(sw.spotClickRectsForTest().first().freqMhz, 14.250);
}

// The GPU path paints the spot labels into its cached static overlay
// (renderGpuFrame's overlay block), which is rebuilt only when it is marked
// dirty. A new spot list must mark it, as setNotchMarkers does; an equal list
// (the repaint guard) need not.
void TestSpotOverlayRender::spotListChangeInvalidatesStaticOverlay()
{
#ifdef NEREUS_GPU_SPECTRUM
    SpectrumWidget sw;
    sw.resize(800, 400);
    sw.setFrequencyRange(14'250'000.0, 96'000.0);
    const QVector<SpectrumWidget::SpotMarker> one{makeSpot(1, QStringLiteral("TEST1"), 14.250)};

    sw.clearOverlayStaticDirtyForTest();
    sw.setSpotMarkers(one);
    QVERIFY2(sw.overlayStaticDirtyForTest(), "a new spot list left the static overlay stale");

    sw.clearOverlayStaticDirtyForTest();
    sw.setSpotMarkers(one);
    QVERIFY2(!sw.overlayStaticDirtyForTest(), "an equal spot list rebuilt the static overlay");

    sw.clearOverlayStaticDirtyForTest();
    sw.setSpotMarkers({});
    QVERIFY2(sw.overlayStaticDirtyForTest(), "clearing the spots left the static overlay stale");
#else
    QSKIP("no cached overlay texture on the CPU-only spectrum path");
#endif
}

// Every input drawSpotMarkers reads is in the same cached overlay.
void TestSpotOverlayRender::spotDisplayInputsInvalidateStaticOverlay()
{
#ifdef NEREUS_GPU_SPECTRUM
    SpectrumWidget sw;
    sw.resize(800, 400);
    const QList<QPair<const char*, std::function<void()>>> inputs = {
        {"setHoverSpotIndexExternal", [&sw] { sw.setHoverSpotIndexExternal(3); }},
        {"setSpotSourceVisible", [&sw] { sw.setSpotSourceVisible(QStringLiteral("RBN"), false); }},
        {"setShowSpots", [&sw] { sw.setShowSpots(!sw.showSpots()); }},
        {"setSpotFontSize", [&sw] { sw.setSpotFontSize(15); }},
        {"setSpotMaxLevels", [&sw] { sw.setSpotMaxLevels(5); }},
        {"setSpotStartPct", [&sw] { sw.setSpotStartPct(40); }},
        {"setSpotOverrideColors", [&sw] { sw.setSpotOverrideColors(true); }},
        {"setSpotOverrideBg", [&sw] { sw.setSpotOverrideBg(true); }},
        {"setSpotColor", [&sw] { sw.setSpotColor(QColor(10, 20, 30)); }},
        {"setSpotBgColor", [&sw] { sw.setSpotBgColor(QColor(40, 50, 60)); }},
        {"setSpotBgOpacity", [&sw] { sw.setSpotBgOpacity(33); }},
        {"loadSpotDisplaySettings", [&sw] { sw.loadSpotDisplaySettings(); }},
    };
    for (const auto& [name, apply] : inputs) {
        sw.clearOverlayStaticDirtyForTest();
        apply();
        QVERIFY2(sw.overlayStaticDirtyForTest(),
                 qPrintable(QStringLiteral("%1 left the static overlay stale").arg(QLatin1String(name))));
    }
#else
    QSKIP("no cached overlay texture on the CPU-only spectrum path");
#endif
}

QTEST_MAIN(TestSpotOverlayRender)
#include "tst_spot_overlay_render.moc"
