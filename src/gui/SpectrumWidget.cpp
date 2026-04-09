#include "SpectrumWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QWheelEvent>

#include <cmath>

namespace NereusSDR {

// ---- Default waterfall gradient stops (AetherSDR style) ----
// From AetherSDR SpectrumWidget.cpp:43-51
static const WfGradientStop kDefaultStops[] = {
    {0.00f,   0,   0,   0},    // black
    {0.15f,   0,   0, 128},    // dark blue
    {0.30f,   0,  64, 255},    // blue
    {0.45f,   0, 200, 255},    // cyan
    {0.60f,   0, 220,   0},    // green
    {0.80f, 255, 255,   0},    // yellow
    {1.00f, 255,   0,   0},    // red
};

// Enhanced scheme — from Thetis display.cs:6864-6954 (9-band progression)
static const WfGradientStop kEnhancedStops[] = {
    {0.000f,   0,   0,   0},   // black
    {0.111f,   0,   0, 255},   // blue
    {0.222f,   0, 255, 255},   // cyan
    {0.333f,   0, 255,   0},   // green
    {0.444f, 128, 255,   0},   // yellow-green
    {0.556f, 255, 255,   0},   // yellow
    {0.667f, 255, 128,   0},   // orange
    {0.778f, 255,   0,   0},   // red
    {0.889f, 255,   0, 128},   // red-magenta
    {1.000f, 192,   0, 255},   // purple
};

// Spectran scheme — from Thetis display.cs:6956-7036
static const WfGradientStop kSpectranStops[] = {
    {0.00f,   0,   0,   0},    // black
    {0.10f,  32,   0,  64},    // dark purple
    {0.25f,   0,   0, 255},    // blue
    {0.40f,   0, 192,   0},    // green
    {0.55f, 255, 255,   0},    // yellow
    {0.70f, 255, 128,   0},    // orange
    {0.85f, 255,   0,   0},    // red
    {1.00f, 255, 255, 255},    // white
};

// Black-white scheme — from Thetis display.cs:7038-7075
static const WfGradientStop kBlackWhiteStops[] = {
    {0.00f,   0,   0,   0},    // black
    {1.00f, 255, 255, 255},    // white
};

const WfGradientStop* wfSchemeStops(WfColorScheme scheme, int& count)
{
    switch (scheme) {
    case WfColorScheme::Enhanced:
        count = 10;
        return kEnhancedStops;
    case WfColorScheme::Spectran:
        count = 8;
        return kSpectranStops;
    case WfColorScheme::BlackWhite:
        count = 2;
        return kBlackWhiteStops;
    case WfColorScheme::Default:
    default:
        count = 7;
        return kDefaultStops;
    }
}

// ---- SpectrumWidget ----

SpectrumWidget::SpectrumWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(400, 200);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);  // receive mouseMoveEvent without button pressed
    setCursor(Qt::CrossCursor);

    // Dark background matching NereusSDR STYLEGUIDE
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(0x0f, 0x0f, 0x1a));
    setPalette(pal);
}

SpectrumWidget::~SpectrumWidget() = default;

void SpectrumWidget::setFrequencyRange(double centerHz, double bandwidthHz)
{
    m_centerHz = centerHz;
    m_bandwidthHz = bandwidthHz;
    update();
}

void SpectrumWidget::setDbmRange(float minDbm, float maxDbm)
{
    m_refLevel = maxDbm;
    m_dynamicRange = maxDbm - minDbm;
    update();
}

void SpectrumWidget::setWfColorScheme(WfColorScheme scheme)
{
    m_wfColorScheme = scheme;
    update();
}

// Feed new FFT frame — apply smoothing, push waterfall row, repaint.
// From AetherSDR SpectrumWidget::updateSpectrum() + gpu-waterfall.md:895-911
void SpectrumWidget::updateSpectrum(int receiverId, const QVector<float>& binsDbm)
{
    Q_UNUSED(receiverId);
    m_bins = binsDbm;

    // Exponential smoothing for spectrum trace
    if (m_smoothed.size() != binsDbm.size()) {
        m_smoothed = binsDbm;  // first frame: no smoothing
    } else {
        for (int i = 0; i < binsDbm.size(); ++i) {
            m_smoothed[i] = kSmoothAlpha * binsDbm[i]
                          + (1.0f - kSmoothAlpha) * m_smoothed[i];
        }
    }

    // Push unsmoothed data to waterfall (sharper signal edges)
    // From gpu-waterfall.md:908
    pushWaterfallRow(binsDbm);
    update();
}

void SpectrumWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    // Recreate waterfall image at new width
    int wfW = width() - kDbmStripW;
    int wfH = static_cast<int>(height() * (1.0f - m_spectrumFrac)) - kFreqScaleH - kDividerH;
    if (wfW > 0 && wfH > 0) {
        m_waterfall = QImage(wfW, wfH, QImage::Format_RGB32);
        m_waterfall.fill(QColor(0x0f, 0x0f, 0x1a));
        m_wfWriteRow = 0;
    }
}

void SpectrumWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    int w = width();
    int h = height();
    int specH = static_cast<int>(h * m_spectrumFrac);
    int wfTop = specH + kDividerH;
    int wfH = h - wfTop - kFreqScaleH;

    // Spectrum area (right of dBm strip)
    QRect specRect(kDbmStripW, 0, w - kDbmStripW, specH);
    // Waterfall area
    QRect wfRect(kDbmStripW, wfTop, w - kDbmStripW, wfH);
    // Frequency scale bar
    QRect freqRect(kDbmStripW, h - kFreqScaleH, w - kDbmStripW, kFreqScaleH);

    // Draw divider bar between spectrum and waterfall
    p.fillRect(0, specH, w, kDividerH, QColor(0x30, 0x40, 0x50));

    // Draw components
    drawGrid(p, specRect);
    drawSpectrum(p, specRect);
    drawWaterfall(p, wfRect);
    drawVfoMarker(p, specRect, wfRect);
    drawFreqScale(p, freqRect);
    drawDbmScale(p, QRect(0, 0, kDbmStripW, specH));
    drawCursorInfo(p, specRect);
}

// ---- Grid drawing ----
// Adapted from Thetis display.cs grid colors:
//   grid_color = Color.FromArgb(65, 255, 255, 255)  — display.cs:2069
//   hgrid_color = Color.White — display.cs:2102
//   grid_text_color = Color.Yellow — display.cs:2003
void SpectrumWidget::drawGrid(QPainter& p, const QRect& specRect)
{
    // Horizontal dBm grid lines
    QColor hGridColor(255, 255, 255, 40);
    p.setPen(QPen(hGridColor, 1));

    float bottom = m_refLevel - m_dynamicRange;
    float step = 10.0f;  // 10 dB steps
    if (m_dynamicRange <= 50.0f) {
        step = 5.0f;
    }

    for (float dbm = bottom + step; dbm < m_refLevel; dbm += step) {
        int y = dbmToY(dbm, specRect);
        p.drawLine(specRect.left(), y, specRect.right(), y);
    }

    // Vertical frequency grid lines
    QColor vGridColor(255, 255, 255, 40);
    p.setPen(QPen(vGridColor, 1));

    // Compute a nice frequency step
    double freqStep = 10000.0;  // 10 kHz default
    if (m_bandwidthHz > 500000.0) {
        freqStep = 50000.0;
    } else if (m_bandwidthHz > 100000.0) {
        freqStep = 25000.0;
    } else if (m_bandwidthHz < 50000.0) {
        freqStep = 5000.0;
    }

    double startFreq = std::ceil((m_centerHz - m_bandwidthHz / 2.0) / freqStep) * freqStep;
    for (double f = startFreq; f < m_centerHz + m_bandwidthHz / 2.0; f += freqStep) {
        int x = hzToX(f, specRect);
        p.drawLine(x, specRect.top(), x, specRect.bottom());
    }
}

// ---- Spectrum trace drawing ----
void SpectrumWidget::drawSpectrum(QPainter& p, const QRect& specRect)
{
    if (m_smoothed.isEmpty()) {
        return;
    }

    int binCount = m_smoothed.size();
    float xStep = static_cast<float>(specRect.width()) / static_cast<float>(binCount);

    // Build polyline for spectrum trace
    QVector<QPointF> points(binCount);
    for (int i = 0; i < binCount; ++i) {
        float x = specRect.left() + static_cast<float>(i) * xStep;
        float y = static_cast<float>(dbmToY(m_smoothed[i], specRect));
        points[i] = QPointF(x, y);
    }

    // Fill under the trace (if enabled)
    // From AetherSDR: fill alpha 0.70, cyan color
    if (m_panFill && binCount > 1) {
        QPainterPath fillPath;
        fillPath.moveTo(points.first().x(), specRect.bottom());
        for (const QPointF& pt : points) {
            fillPath.lineTo(pt);
        }
        fillPath.lineTo(points.last().x(), specRect.bottom());
        fillPath.closeSubpath();

        QColor fill = m_fillColor;
        fill.setAlphaF(m_fillAlpha * 0.4f);  // softer fill
        p.fillPath(fillPath, fill);
    }

    // Draw trace line
    // From Thetis display.cs:2184 — data_line_color = Color.White
    // We use the fill color for consistency with AetherSDR style
    QPen tracePen(m_fillColor, 1.5);
    p.setPen(tracePen);
    p.drawPolyline(points.data(), binCount);
}

// ---- Waterfall drawing ----
void SpectrumWidget::drawWaterfall(QPainter& p, const QRect& wfRect)
{
    if (m_waterfall.isNull() || wfRect.width() <= 0 || wfRect.height() <= 0) {
        return;
    }

    // Ring buffer display — newest row at top, oldest at bottom.
    // From Thetis display.cs:7719-7729: new row written at top, old content shifts down.
    // Our ring buffer equivalent: m_wfWriteRow is where the NEWEST row lives.
    // Display order: writeRow → wrapping down → back to writeRow-1 (oldest).
    int wfH = m_waterfall.height();

    // Part 1 (top of screen): from writeRow to end of image
    int part1Rows = wfH - m_wfWriteRow;
    if (part1Rows > 0) {
        QRect src(0, m_wfWriteRow, m_waterfall.width(), part1Rows);
        QRect dst(wfRect.left(), wfRect.top(), wfRect.width(), part1Rows);
        p.drawImage(dst, m_waterfall, src);
    }

    // Part 2 (bottom of screen): from 0 to writeRow
    if (m_wfWriteRow > 0) {
        QRect src(0, 0, m_waterfall.width(), m_wfWriteRow);
        QRect dst(wfRect.left(), wfRect.top() + part1Rows, wfRect.width(), m_wfWriteRow);
        p.drawImage(dst, m_waterfall, src);
    }
}

// ---- Frequency scale bar ----
void SpectrumWidget::drawFreqScale(QPainter& p, const QRect& r)
{
    p.fillRect(r, QColor(0x10, 0x15, 0x20));

    QFont font = p.font();
    font.setPixelSize(10);
    p.setFont(font);
    // From Thetis display.cs:2003 — grid_text_color = Color.Yellow
    p.setPen(QColor(255, 255, 0));

    double freqStep = 25000.0;
    if (m_bandwidthHz > 500000.0) {
        freqStep = 50000.0;
    } else if (m_bandwidthHz < 50000.0) {
        freqStep = 5000.0;
    } else if (m_bandwidthHz < 100000.0) {
        freqStep = 10000.0;
    }

    double startFreq = std::ceil((m_centerHz - m_bandwidthHz / 2.0) / freqStep) * freqStep;
    for (double f = startFreq; f < m_centerHz + m_bandwidthHz / 2.0; f += freqStep) {
        int x = hzToX(f, r);
        // Format as MHz with appropriate decimals
        double mhz = f / 1.0e6;
        QString label;
        if (freqStep >= 100000.0) {
            label = QString::number(mhz, 'f', 1);
        } else if (freqStep >= 10000.0) {
            label = QString::number(mhz, 'f', 2);
        } else {
            label = QString::number(mhz, 'f', 3);
        }

        QRect textRect(x - 30, r.top() + 2, 60, r.height() - 2);
        p.drawText(textRect, Qt::AlignCenter, label);
    }
}

// ---- dBm scale strip ----
void SpectrumWidget::drawDbmScale(QPainter& p, const QRect& specRect)
{
    p.fillRect(QRect(0, specRect.top(), kDbmStripW, specRect.height()),
               QColor(0x10, 0x15, 0x20));

    QFont font = p.font();
    font.setPixelSize(9);
    p.setFont(font);
    p.setPen(QColor(255, 255, 0));  // Yellow text — from Thetis display.cs:2003

    float bottom = m_refLevel - m_dynamicRange;
    float step = 10.0f;
    if (m_dynamicRange <= 50.0f) {
        step = 5.0f;
    }

    for (float dbm = bottom; dbm <= m_refLevel; dbm += step) {
        int y = dbmToY(dbm, specRect);
        QString label = QString::number(static_cast<int>(dbm));
        QRect textRect(2, y - 6, kDbmStripW - 4, 12);
        p.drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, label);
    }
}

// ---- Coordinate helpers ----

int SpectrumWidget::hzToX(double hz, const QRect& r) const
{
    double lowHz = m_centerHz - m_bandwidthHz / 2.0;
    double frac = (hz - lowHz) / m_bandwidthHz;
    return r.left() + static_cast<int>(frac * r.width());
}

double SpectrumWidget::xToHz(int x, const QRect& r) const
{
    double frac = static_cast<double>(x - r.left()) / r.width();
    return (m_centerHz - m_bandwidthHz / 2.0) + frac * m_bandwidthHz;
}

int SpectrumWidget::dbmToY(float dbm, const QRect& r) const
{
    float bottom = m_refLevel - m_dynamicRange;
    float frac = (dbm - bottom) / m_dynamicRange;
    frac = qBound(0.0f, frac, 1.0f);
    return r.bottom() - static_cast<int>(frac * r.height());
}

// ---- Waterfall row push ----
// From Thetis display.cs:7719 — new row at top, old content shifts down.
// Ring buffer equivalent: decrement write pointer so newest row is always
// at m_wfWriteRow, and display reads forward from there (wrapping).
void SpectrumWidget::pushWaterfallRow(const QVector<float>& bins)
{
    if (m_waterfall.isNull()) {
        return;
    }

    int h = m_waterfall.height();
    // Decrement write pointer (wrapping) — newest data at top of display
    m_wfWriteRow = (m_wfWriteRow - 1 + h) % h;

    int w = m_waterfall.width();
    QRgb* scanline = reinterpret_cast<QRgb*>(m_waterfall.scanLine(m_wfWriteRow));
    float binScale = static_cast<float>(bins.size()) / static_cast<float>(w);

    for (int x = 0; x < w; ++x) {
        int srcBin = static_cast<int>(static_cast<float>(x) * binScale);
        srcBin = qBound(0, srcBin, bins.size() - 1);
        scanline[x] = dbmToRgb(bins[srcBin]);
    }
}

// ---- dBm to waterfall color ----
// From Thetis display.cs:6800+ — waterfall uses its own high/low thresholds
// (default -80/-130 dBm) for better contrast than the full spectrum range.
// Color gain and black level from AetherSDR SpectrumWidget.cpp:1700
QRgb SpectrumWidget::dbmToRgb(float dbm) const
{
    // Use waterfall-specific thresholds (narrower range = better contrast)
    // From Thetis display.cs:2522-2536
    float range = m_wfHighThreshold - m_wfLowThreshold;
    if (range < 1.0f) {
        range = 1.0f;
    }

    // Normalize to 0.0-1.0 based on waterfall thresholds
    float intensity = (dbm - m_wfLowThreshold) / range;
    intensity = qBound(0.0f, intensity, 1.0f);

    // Apply color gain and black level adjustments
    // From AetherSDR SpectrumWidget.cpp:1700
    float floorShift = (125.0f - static_cast<float>(m_wfBlackLevel)) * 0.4f / 125.0f;
    float visRange = 1.0f - static_cast<float>(m_wfColorGain) * 0.7f / 100.0f;
    if (visRange < 0.1f) {
        visRange = 0.1f;
    }

    float adjusted = (intensity - floorShift) / visRange;
    adjusted = qBound(0.0f, adjusted, 1.0f);

    // Look up in gradient stops for current color scheme
    int stopCount = 0;
    const WfGradientStop* stops = wfSchemeStops(m_wfColorScheme, stopCount);

    // Find the two surrounding stops and interpolate
    for (int i = 0; i < stopCount - 1; ++i) {
        if (adjusted <= stops[i + 1].pos) {
            float t = (adjusted - stops[i].pos)
                    / (stops[i + 1].pos - stops[i].pos);
            int r = static_cast<int>(stops[i].r + t * (stops[i + 1].r - stops[i].r));
            int g = static_cast<int>(stops[i].g + t * (stops[i + 1].g - stops[i].g));
            int b = static_cast<int>(stops[i].b + t * (stops[i + 1].b - stops[i].b));
            return qRgb(r, g, b);
        }
    }
    return qRgb(stops[stopCount - 1].r,
                stops[stopCount - 1].g,
                stops[stopCount - 1].b);
}

// ---- VFO marker + filter passband overlay ----
// From Thetis display.cs DrawPanadapterDX2D — VFO line + filter rectangle
// Colors from AetherSDR: orange VFO, cyan filter passband
void SpectrumWidget::drawVfoMarker(QPainter& p, const QRect& specRect, const QRect& wfRect)
{
    if (m_vfoHz <= 0.0) {
        return;
    }

    int vfoX = hzToX(m_vfoHz, specRect);
    if (vfoX < specRect.left() || vfoX > specRect.right()) {
        return;
    }

    // Filter passband rectangle
    // From AetherSDR: translucent cyan for active filter
    double loHz = m_vfoHz + m_filterLowHz;
    double hiHz = m_vfoHz + m_filterHighHz;
    int xLo = hzToX(loHz, specRect);
    int xHi = hzToX(hiHz, specRect);
    if (xLo > xHi) {
        std::swap(xLo, xHi);
    }

    // Spectrum passband
    QColor filterColor(0, 180, 216, 40);  // NereusSDR accent cyan, semi-transparent
    p.fillRect(xLo, specRect.top(), xHi - xLo, specRect.height(), filterColor);

    // Waterfall passband (slightly more transparent)
    QColor wfFilterColor(0, 180, 216, 25);
    p.fillRect(xLo, wfRect.top(), xHi - xLo, wfRect.height(), wfFilterColor);

    // VFO center line — orange for active slice
    // From AetherSDR SpectrumWidget: slice marker color
    QPen vfoPen(QColor(255, 165, 0), 1);
    p.setPen(vfoPen);
    p.drawLine(vfoX, specRect.top(), vfoX, wfRect.bottom());

    // Filter edge lines (thin white)
    QPen edgePen(QColor(255, 255, 255, 80), 1);
    p.setPen(edgePen);
    p.drawLine(xLo, specRect.top(), xLo, wfRect.bottom());
    p.drawLine(xHi, specRect.top(), xHi, wfRect.bottom());
}

// ---- Cursor frequency display ----
void SpectrumWidget::drawCursorInfo(QPainter& p, const QRect& specRect)
{
    if (!m_mouseInWidget) {
        return;
    }

    double hz = xToHz(m_mousePos.x(), specRect);
    double mhz = hz / 1.0e6;

    QString label = QString::number(mhz, 'f', 4) + QStringLiteral(" MHz");

    QFont font = p.font();
    font.setPixelSize(11);
    font.setBold(true);
    p.setFont(font);

    QFontMetrics fm(font);
    int textW = fm.horizontalAdvance(label) + 12;
    int textH = fm.height() + 6;

    // Position near cursor, offset to avoid covering the crosshair
    int labelX = m_mousePos.x() + 12;
    int labelY = m_mousePos.y() - textH - 4;
    if (labelX + textW > specRect.right()) {
        labelX = m_mousePos.x() - textW - 12;
    }
    if (labelY < specRect.top()) {
        labelY = m_mousePos.y() + 12;
    }

    // Background
    p.fillRect(labelX, labelY, textW, textH, QColor(0x10, 0x15, 0x20, 200));
    p.setPen(QColor(0xc8, 0xd8, 0xe8));
    p.drawText(labelX + 6, labelY + fm.ascent() + 3, label);
}

// ---- Mouse event handlers ----
// From gpu-waterfall.md:1064-1076 mouse interaction table

void SpectrumWidget::mousePressEvent(QMouseEvent* event)
{
    int w = width();
    int h = height();
    int specH = static_cast<int>(h * m_spectrumFrac);
    QRect specRect(kDbmStripW, 0, w - kDbmStripW, specH);

    if (event->button() == Qt::LeftButton) {
        // Check if clicking on dBm scale strip — start drag for ref level
        if (event->position().x() < kDbmStripW) {
            m_draggingDbm = true;
            m_dragStartY = static_cast<int>(event->position().y());
            m_dragStartRef = m_refLevel;
            return;
        }

        // Click on spectrum/waterfall — tune to clicked frequency
        double hz = xToHz(static_cast<int>(event->position().x()), specRect);
        // Snap to step size
        hz = std::round(hz / m_stepHz) * m_stepHz;
        emit frequencyClicked(hz);
    }

    QWidget::mousePressEvent(event);
}

void SpectrumWidget::mouseMoveEvent(QMouseEvent* event)
{
    m_mousePos = event->pos();
    m_mouseInWidget = true;

    if (m_draggingDbm) {
        // Drag dBm scale to adjust reference level
        int dy = static_cast<int>(event->position().y()) - m_dragStartY;
        int specH = static_cast<int>(height() * m_spectrumFrac);
        float dbPerPixel = m_dynamicRange / static_cast<float>(specH);
        m_refLevel = m_dragStartRef + static_cast<float>(dy) * dbPerPixel;
        m_refLevel = qBound(-20.0f, m_refLevel, 20.0f);
        update();
        return;
    }

    update();  // repaint for cursor frequency display
    QWidget::mouseMoveEvent(event);
}

void SpectrumWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_draggingDbm = false;
    }
    QWidget::mouseReleaseEvent(event);
}

void SpectrumWidget::wheelEvent(QWheelEvent* event)
{
    // Ctrl+scroll: zoom bandwidth
    // Plain scroll: adjust ref level
    int delta = event->angleDelta().y();

    if (event->modifiers() & Qt::ControlModifier) {
        // Zoom bandwidth: scroll up = zoom in (narrower), down = zoom out (wider)
        double factor = (delta > 0) ? 0.8 : 1.25;
        double newBw = m_bandwidthHz * factor;
        newBw = qBound(5000.0, newBw, 1000000.0);  // 5 kHz to 1 MHz
        m_bandwidthHz = newBw;
        emit bandwidthChangeRequested(newBw);
    } else {
        // Scroll ref level: up = increase (show stronger signals at top)
        float step = (delta > 0) ? 5.0f : -5.0f;
        m_refLevel = qBound(-160.0f, m_refLevel + step, 20.0f);
    }

    update();
    QWidget::wheelEvent(event);
}

} // namespace NereusSDR
