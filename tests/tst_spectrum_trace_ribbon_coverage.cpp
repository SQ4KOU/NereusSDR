// =================================================================
// tests/tst_spectrum_trace_ribbon_coverage.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test infrastructure.
//
// The GPU trace's geometry, rasterized: a noisy trace draws as one joined
// line at every line width.
//
// Bench-reported 2026-10-11 on Windows (100% scaling, Direct3D 11, trace
// width 1.0 to 1.1 px): the spectrum trace drew with gaps along the line.
//
// The first ribbon (SpectrumTraceRibbon.h as created) put each point's two
// corners along the bend at that point. At a noise peak or dip the bend
// points straight up, so both ends of a steep run were offset vertically
// and the run between them was a sliver a fraction of a pixel across:
// most pixel centers missed it. The tests of that ribbon read vertex
// positions only and could not see it.
//
// This test rasterizes what buildSpectrumTraceRibbon() makes, with the rule
// Direct3D and Metal use (a pixel is drawn when its center is inside the
// triangle; top and left edges count), and counts neighboring pixel columns
// whose drawn pixels do not form one joined piece.
//
// Not proved here: what a real Direct3D or Metal device draws. The rule is
// the documented one, written out below; no GPU runs.
//
// 2026-10-11 - Created. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//               Claude Code.
// =================================================================
#include <QtTest/QtTest>

#include <QVector>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

#include "gui/SpectrumTraceRibbon.h"

using namespace NereusSDR;

namespace {

constexpr int kStride = 6;
// The trace's points: one per display pixel, as the pan draws it.
constexpr int kPoints = 400;
constexpr int kViewHeightPx = 300;

struct Point {
    double x;
    double y;
};

// Drawn pixels of a viewport, row-major.
struct Canvas {
    int width;
    int height;
    QVector<quint8> drawn;
    Canvas(int w, int h) : width(w), height(h), drawn(w * h, 0) {}
    void set(int x, int y)
    {
        if (x >= 0 && x < width && y >= 0 && y < height) {
            drawn[y * width + x] = 1;
        }
    }
    bool at(int x, int y) const { return drawn[y * width + x] != 0; }
};

// A noisy trace: every point is a peak or a dip, with steep runs between
// (up to 60 display pixels from one column to the next) and a few shallow
// ones. Fixed seed, so every run measures the same trace.
QVector<Point> noisyTrace(int density)
{
    QVector<Point> trace;
    std::uint32_t state = 0x2026'1011u;
    const auto next = [&]() {
        state = state * 1664525u + 1013904223u;
        return static_cast<double>(state >> 8) / static_cast<double>(1u << 24);
    };
    const double mid = kViewHeightPx / 2.0;
    for (int j = 0; j < kPoints; ++j) {
        const double swing = (j % 16 < 3) ? 1.5 * next() : 4.0 + 26.0 * next();
        const double y = mid + ((j % 2 == 0) ? swing : -swing);
        trace.append({(j + 0.5) * density, y * density});
    }
    return trace;
}

// The trace as the line vertices buildSpectrumTraceRibbon() takes.
QVector<float> lineVertices(const QVector<Point>& trace, int widthPx, int heightPx)
{
    QVector<float> line;
    for (const Point& p : trace) {
        line << static_cast<float>(p.x / (widthPx / 2.0) - 1.0)
             << static_cast<float>(p.y / (heightPx / 2.0) - 1.0)
             << 1.0f << 1.0f << 1.0f << 0.9f;
    }
    return line;
}

// One triangle, by the top-left rule: a pixel is drawn when its center is
// inside, or on an edge that is a top edge (level, with the inside below
// it) or a left edge.
void fillTriangle(Canvas& canvas, Point a, Point b, Point c)
{
    double area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (area == 0.0) {
        return;
    }
    if (area < 0.0) {
        std::swap(b, c);
    }
    const auto edge = [](Point from, Point to, double px, double py) {
        return (to.x - from.x) * (py - from.y) - (to.y - from.y) * (px - from.x);
    };
    // With y growing downward and this winding, an edge heading up is a
    // left edge and a level edge heading right is a top edge.
    const auto topLeft = [](Point from, Point to) {
        const double dx = to.x - from.x;
        const double dy = to.y - from.y;
        return dy < 0.0 || (dy == 0.0 && dx > 0.0);
    };
    const auto inside = [&](Point from, Point to, double px, double py) {
        const double e = edge(from, to, px, py);
        return e > 0.0 || (e == 0.0 && topLeft(from, to));
    };
    const int x0 = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
    const int x1 = std::min(canvas.width - 1,
                            static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
    const int y0 = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
    const int y1 = std::min(canvas.height - 1,
                            static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const double px = x + 0.5;
            const double py = y + 0.5;
            if (inside(a, b, px, py) && inside(b, c, px, py) && inside(c, a, px, py)) {
                canvas.set(x, y);
            }
        }
    }
}

// One segment of a line strip, as a one-pixel line is drawn: one pixel in
// every column it crosses when it is flatter than 45 degrees, one in every
// row when it is steeper.
void drawLine(Canvas& canvas, Point a, Point b)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    if (std::abs(dx) >= std::abs(dy)) {
        if (a.x > b.x) {
            std::swap(a, b);
        }
        for (int x = static_cast<int>(std::floor(a.x)); x <= static_cast<int>(std::floor(b.x));
             ++x) {
            const double t = (b.x == a.x) ? 0.0
                : std::clamp((x + 0.5 - a.x) / (b.x - a.x), 0.0, 1.0);
            canvas.set(x, static_cast<int>(std::floor(a.y + t * (b.y - a.y))));
        }
    } else {
        if (a.y > b.y) {
            std::swap(a, b);
        }
        for (int y = static_cast<int>(std::floor(a.y)); y <= static_cast<int>(std::floor(b.y));
             ++y) {
            const double t = std::clamp((y + 0.5 - a.y) / (b.y - a.y), 0.0, 1.0);
            canvas.set(static_cast<int>(std::floor(a.x + t * (b.x - a.x))), y);
        }
    }
}

// Neighboring columns x and x + 1 are joined when both have a drawn pixel
// and the drawn pixels of the two columns together are one piece (each
// touching the next along a side or a corner). A run with a hole in it, or
// two columns drawn rows apart, is a broken join.
bool columnsJoin(const Canvas& canvas, int x)
{
    QVector<int> piece(2 * canvas.height, 0);   // [column][row]: 0 none, 1 drawn, 2 reached
    int drawnCount = 0;
    int start = -1;
    bool hasLeft = false;
    bool hasRight = false;
    for (int y = 0; y < canvas.height; ++y) {
        for (int c = 0; c < 2; ++c) {
            if (canvas.at(x + c, y)) {
                piece[c * canvas.height + y] = 1;
                ++drawnCount;
                start = (start < 0) ? c * canvas.height + y : start;
                (c == 0 ? hasLeft : hasRight) = true;
            }
        }
    }
    if (!hasLeft || !hasRight) {
        return false;
    }
    QVector<int> todo{start};
    piece[start] = 2;
    int reached = 1;
    while (!todo.isEmpty()) {
        const int at = todo.takeLast();
        const int c = at / canvas.height;
        const int y = at % canvas.height;
        for (int nc = 0; nc < 2; ++nc) {
            for (int ny = y - 1; ny <= y + 1; ++ny) {
                if (ny < 0 || ny >= canvas.height) {
                    continue;
                }
                const int index = nc * canvas.height + ny;
                if (piece[index] == 1) {
                    piece[index] = 2;
                    ++reached;
                    todo.append(index);
                }
            }
        }
    }
    return reached == drawnCount;
}

struct Joins {
    int pairs{0};
    int broken{0};
};

// Builds the trace's geometry at `widthPx` display pixels on a display of
// `density` device pixels per display pixel, rasterizes it, and counts the
// broken joins between neighboring device pixel columns along the trace.
Joins measure(float widthPx, int density)
{
    const int viewW = kPoints * density;
    const int viewH = kViewHeightPx * density;
    const QVector<Point> trace = noisyTrace(density);
    const QVector<float> line = lineVertices(trace, viewW, viewH);
    const float deviceWidth = widthPx * density;

    QVector<float> geometry;
    buildSpectrumTraceRibbon(line, kPoints, kStride, static_cast<float>(viewW),
                             static_cast<float>(viewH), deviceWidth, geometry);
    const int count = static_cast<int>(geometry.size() / kStride);
    const auto vertex = [&](int i) {
        return Point{(geometry[i * kStride] + 1.0) * viewW / 2.0,
                     (geometry[i * kStride + 1] + 1.0) * viewH / 2.0};
    };

    Canvas canvas(viewW, viewH);
    if (traceRibbonTopology(deviceWidth) == TraceRibbonTopology::LineStrip) {
        for (int i = 0; i + 1 < count; ++i) {
            drawLine(canvas, vertex(i), vertex(i + 1));
        }
    } else {
        for (int i = 0; i + 2 < count; ++i) {
            fillTriangle(canvas, vertex(i), vertex(i + 1), vertex(i + 2));
        }
    }

    Joins joins;
    const int first = static_cast<int>(std::floor(trace.first().x));
    const int last = static_cast<int>(std::floor(trace.last().x));
    for (int x = first; x < last; ++x) {
        ++joins.pairs;
        if (!columnsJoin(canvas, x)) {
            ++joins.broken;
        }
    }
    return joins;
}

} // namespace

class TstSpectrumTraceRibbonCoverage : public QObject {
    Q_OBJECT

private slots:
    // The counter can count: a trace drawn as loose dots has broken joins.
    void theCounterSeesABrokenLine()
    {
        Canvas canvas(8, 40);
        canvas.set(2, 5);
        canvas.set(3, 5);       // joined to column 2
        canvas.set(4, 30);      // rows away from column 3
        canvas.set(5, 30);
        canvas.set(5, 10);      // a second piece in the same column
        QVERIFY(columnsJoin(canvas, 2));
        QVERIFY(!columnsJoin(canvas, 3));
        QVERIFY(!columnsJoin(canvas, 4));
        QVERIFY(!columnsJoin(canvas, 5));   // column 6 is empty
    }

    void aNoisyTraceDrawsAsOneJoinedLine_data()
    {
        QTest::addColumn<float>("widthPx");
        QTest::addColumn<int>("density");
        for (int density : {1, 2}) {
            for (float width : {0.1f, 1.0f, 1.1f, 1.5f, 5.0f}) {
                QTest::newRow(qPrintable(QStringLiteral("%1 px at %2x")
                                             .arg(double(width), 0, 'f', 1).arg(density)))
                    << width << density;
            }
        }
    }

    void aNoisyTraceDrawsAsOneJoinedLine()
    {
        QFETCH(float, widthPx);
        QFETCH(int, density);
        const Joins joins = measure(widthPx, density);
        QVERIFY(joins.pairs > 300);
        const QString text = QStringLiteral("%1 of %2 joins between neighboring columns are "
                                            "broken (%3%)")
                                 .arg(joins.broken).arg(joins.pairs)
                                 .arg(100.0 * joins.broken / joins.pairs, 0, 'f', 1);
        qInfo().noquote() << text;
        QVERIFY2(joins.broken == 0, qPrintable(text));
    }
};

QTEST_APPLESS_MAIN(TstSpectrumTraceRibbonCoverage)
#include "tst_spectrum_trace_ribbon_coverage.moc"
