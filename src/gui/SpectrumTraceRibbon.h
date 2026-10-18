// no-port-check: NereusSDR-original. The GPU spectrum trace as a ribbon of its line width.

#pragma once

// =================================================================
// src/gui/SpectrumTraceRibbon.h  (NereusSDR)
// =================================================================
//
// The GPU path's live spectrum trace, as geometry of the trace's line
// width. QRhi has no portable line width (a line strip is one device pixel
// wide on every backend), so the trace was one device pixel wide whatever
// its Line Width said. SpectrumWidget::renderGpuFrame() uploads what
// buildSpectrumTraceRibbon() makes from the trace's line vertices.
//
// A width over one device pixel is a triangle strip, drawn with the fill
// pipeline (same shader and blend): one rectangle of that width per
// segment of the trace, so the line is the full width measured across
// every segment however steep, and the strip's own triangles between one
// rectangle and the next close the outside of each bend (a bevel).
//
// A width of one device pixel or less is the line strip itself, drawn with
// the line pipeline as it was before the ribbon: one device pixel wide,
// joined by the rasterizer's own rule, with its alpha scaled by the width.
// The render target is not multisampled, so geometry thinner than a pixel
// would cover only the pixel centers it happened to cross and draw as
// dashes; scaled alpha is the coverage a thinner line would have. The
// line strip is used here, not one-pixel rectangles, because rectangles
// overlap at every bend and a scaled alpha would add up there.
//
// The first ribbon (two vertices per point, offset along the bend at that
// point) drew with gaps: at a noise peak or dip the bend points straight
// up, both ends of a steep run were offset vertically, and the run between
// them was a sliver a fraction of a pixel across. Bench, Windows,
// 2026-10-11. tst_spectrum_trace_ribbon_coverage rasterizes the geometry
// and counts the broken joins.
//
// Where the trace's color is not opaque, the rectangles' overlap at a bend
// is blended twice. At the default alpha of 0.9 that is 0.99 against 0.9.
//
// Pure and GPU-free, so an offscreen test reads the geometry
// (tst_spectrum_widget_additions, the lineWidth_ slots).
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-11 — Created for the 0.1 to 5 px trace line width. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-11 - One rectangle per segment in place of two vertices per
//                 point offset along the bend (the trace drew with gaps on
//                 steep runs); one device pixel and under is the line
//                 strip again. J.J. Boyd (KG4VCF), AI-assisted
//                 implementation via Anthropic Claude Code.
// =================================================================

#include <QVector>

#include <algorithm>
#include <cmath>

namespace NereusSDR {

// The narrowest line drawn, in device pixels. At or below it the trace is
// a line strip and the width becomes alpha (see the file comment).
inline constexpr float kTraceRibbonMinDrawnPx = 1.0f;

// Vertices in one segment's rectangle.
inline constexpr int kTraceRibbonVerticesPerSegment = 4;

// What buildSpectrumTraceRibbon() makes for a width, and so which pipeline
// draws it.
enum class TraceRibbonTopology {
    LineStrip,      // one vertex per trace point
    TriangleStrip,  // four vertices per trace segment
};

inline TraceRibbonTopology traceRibbonTopology(float widthPx)
{
    return widthPx > kTraceRibbonMinDrawnPx ? TraceRibbonTopology::TriangleStrip
                                            : TraceRibbonTopology::LineStrip;
}

// The width the trace is drawn at for a line width of `widthPx` device
// pixels, and the factor its alpha is multiplied by.
inline float traceRibbonDrawnWidthPx(float widthPx)
{
    return std::max(kTraceRibbonMinDrawnPx, widthPx);
}

inline float traceRibbonCoverage(float widthPx)
{
    return std::clamp(widthPx / kTraceRibbonMinDrawnPx, 0.0f, 1.0f);
}

// The number of vertices buildSpectrumTraceRibbon() makes for `n` trace
// points at `widthPx` device pixels: what is uploaded and what is drawn.
inline int traceRibbonVertexCount(int n, float widthPx)
{
    if (n <= 0) {
        return 0;
    }
    if (traceRibbonTopology(widthPx) == TraceRibbonTopology::LineStrip) {
        return n;
    }
    return n >= 2 ? (n - 1) * kTraceRibbonVerticesPerSegment : 0;
}

// The most vertices any width makes for `n` trace points: the size of the
// buffer that holds them.
inline int traceRibbonMaxVertexCount(int n)
{
    return std::max(n, (n - 1) * kTraceRibbonVerticesPerSegment);
}

// Builds the trace's geometry for the first `n` vertices of `line`.
//
//   line     n vertices of `stride` floats: x, y in normalised device
//            coordinates (-1 to 1 across the viewport), then r, g, b, a.
//   viewportWidthPx, viewportHeightPx
//            the viewport the trace is drawn in, in device pixels.
//   widthPx  the line width in device pixels.
//   ribbon   receives traceRibbonVertexCount(n, widthPx) vertices of the
//            same layout.
//
// Over one device pixel: a triangle strip. For each segment, in order: its
// start half the width to the left of travel, its start half to the right,
// its end half to the left, its end half to the right. Each vertex has its
// trace point's color. The two triangles the strip makes between one
// segment's last pair and the next segment's first pair lie around the
// shared point and fill the outside of the bend.
//
// One device pixel or under: the line's own vertices, alpha times the
// coverage.
//
// A viewport without area gives rectangles with no area (every vertex at
// its trace point), which draw nothing.
inline void buildSpectrumTraceRibbon(const QVector<float>& line, int n, int stride,
                                     float viewportWidthPx, float viewportHeightPx,
                                     float widthPx, QVector<float>& ribbon)
{
    const int count = traceRibbonVertexCount(n, widthPx);
    ribbon.resize(count * stride);
    if (count <= 0) {
        return;
    }

    if (traceRibbonTopology(widthPx) == TraceRibbonTopology::LineStrip) {
        const float coverage = traceRibbonCoverage(widthPx);
        for (int j = 0; j < n; ++j) {
            const int at = j * stride;
            for (int c = 0; c < stride; ++c) {
                ribbon[at + c] = (c == stride - 1) ? line[at + c] * coverage : line[at + c];
            }
        }
        return;
    }

    const bool hasArea = viewportWidthPx > 0.0f && viewportHeightPx > 0.0f;
    const float half = hasArea ? traceRibbonDrawnWidthPx(widthPx) * 0.5f : 0.0f;
    // Normalised device coordinates span 2 across the viewport.
    const float pxPerNdcX = viewportWidthPx * 0.5f;
    const float pxPerNdcY = viewportHeightPx * 0.5f;

    for (int j = 0; j + 1 < n; ++j) {
        const int from = j * stride;
        const int to = from + stride;
        float offX = 0.0f;
        float offY = 0.0f;
        if (hasArea) {
            // The segment in device pixels, and its unit normal (to the
            // left of travel). Two points at one place have no direction:
            // straight up.
            const float dx = (line[to] - line[from]) * pxPerNdcX;
            const float dy = (line[to + 1] - line[from + 1]) * pxPerNdcY;
            const float len = std::sqrt(dx * dx + dy * dy);
            const float nx = (len > 0.0f) ? -dy / len : 0.0f;
            const float ny = (len > 0.0f) ? dx / len : 1.0f;
            offX = nx * half / pxPerNdcX;
            offY = ny * half / pxPerNdcY;
        }

        const int base = j * kTraceRibbonVerticesPerSegment * stride;
        for (int corner = 0; corner < kTraceRibbonVerticesPerSegment; ++corner) {
            const int src = (corner < 2) ? from : to;
            const float side = (corner % 2 == 0) ? 1.0f : -1.0f;
            const int dst = base + corner * stride;
            ribbon[dst]     = line[src] + side * offX;
            ribbon[dst + 1] = line[src + 1] + side * offY;
            for (int c = 2; c < stride; ++c) {
                ribbon[dst + c] = line[src + c];
            }
        }
    }
}

} // namespace NereusSDR
