// no-port-check: NereusSDR-original Qt6 chart widget.  See header for
// the rationale.  Series semantics mapped from AmpView.cs [v2.10.3.13].
//
// =================================================================
// src/gui/AmpViewChart.cpp  (NereusSDR)
// =================================================================
//
// Modification history (NereusSDR):
//   2026-05-06 — Phase 3M-4 Task 9 created by J.J. Boyd (KG4VCF),
//                 with AI-assisted transformation via Anthropic
//                 Claude Code.
// =================================================================

#include "AmpViewChart.h"

#include <QFont>
#include <QFontMetrics>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRectF>
#include <QString>

#include <algorithm>
#include <cmath>

namespace NereusSDR {

namespace {

// AmpView background — black per AmpView.Designer.cs:56-65 [v2.10.3.13]
// (chart1.BackColor = Color.Black, ChartArea1.BackColor = Color.Black).
constexpr QRgb kBgRgb         = qRgb(0x00, 0x00, 0x00);
// DimGray axis lines + grid per AmpView.Designer.cs:58-63 [v2.10.3.13].
constexpr QRgb kAxisGridRgb   = qRgb(0x69, 0x69, 0x69);
// LightSalmon axis labels per AmpView.cs:112-119 [v2.10.3.13].
constexpr QRgb kAxisLabelRgb  = qRgb(0xFF, 0xA0, 0x7A);
// Plot-area inset margins (px) — leave room for tick labels + axis titles.
constexpr int kMarginLeft   = 50;
constexpr int kMarginRight  = 50;  // secondary Y axis lives here
constexpr int kMarginTop    = 18;
constexpr int kMarginBottom = 28;

// Map a (data-x, data-y) point to plot pixel coordinates.
QPointF mapMag(const QRectF& plot, double x, double xMax,
               double y, double yMin, double yMax)
{
    if (xMax <= 0.0) { return plot.topLeft(); }
    if (yMax <= yMin) { return plot.topLeft(); }
    const double nx = std::clamp(x / xMax, 0.0, 1.0);
    const double ny = std::clamp((y - yMin) / (yMax - yMin), 0.0, 1.0);
    return QPointF(plot.left() + nx * plot.width(),
                   plot.bottom() - ny * plot.height());
}

// Map a (data-x, data-y) point on the secondary-axis (phase) plot.
QPointF mapPhs(const QRectF& plot, double x, double xMax,
               double y, double yMin, double yMax)
{
    return mapMag(plot, x, xMax, y, yMin, yMax);
}

} // namespace

AmpViewChart::AmpViewChart(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ampViewChart"));
    setAutoFillBackground(false);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
}

AmpViewChart::~AmpViewChart() = default;

void AmpViewChart::setSeriesData(const std::vector<double>& xs,
                                 const std::vector<double>& magAmp,
                                 const std::vector<double>& phsAmp,
                                 const std::vector<double>& magCorr,
                                 const std::vector<double>& phsCorr)
{
    m_x       = xs;
    m_magAmp  = magAmp;
    m_phsAmp  = phsAmp;
    m_magCorr = magCorr;
    m_phsCorr = phsCorr;
    update();
}

void AmpViewChart::setShowGain(bool on)
{
    if (on == m_showGain) { return; }
    m_showGain = on;
    update();
}

void AmpViewChart::setPhaseZoom(bool on)
{
    if (on == m_phaseZoom) { return; }
    m_phaseZoom = on;
    update();
}

void AmpViewChart::setLowRes(bool on)
{
    if (on == m_lowRes) { return; }
    m_lowRes = on;
    update();
}

void AmpViewChart::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(kBgRgb));

    const QRectF plot(kMarginLeft,
                      kMarginTop,
                      width() - kMarginLeft - kMarginRight,
                      height() - kMarginTop - kMarginBottom);
    if (!plot.isValid()) { return; }

    // ── Axes + grid (DimGray, mirrors disp_setup AxisX/Y/Y2 LineColor) ──
    p.setPen(QPen(QColor(kAxisGridRgb), 1));
    // 5 vertical gridlines (excluding edges).
    for (int i = 1; i <= 4; ++i) {
        const double x = plot.left() + plot.width() * i / 5.0;
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    }
    // 4 horizontal gridlines (excluding edges).
    for (int i = 1; i <= 3; ++i) {
        const double y = plot.top() + plot.height() * i / 4.0;
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }
    // Axis frame.
    p.setPen(QPen(QColor(kAxisGridRgb), 1.2));
    p.drawRect(plot);

    // ── Axis labels (LightSalmon, AmpView.cs:112-119 [v2.10.3.13]) ──────
    p.setPen(QPen(QColor(kAxisLabelRgb)));
    QFont labelFont = p.font();
    labelFont.setPointSizeF(labelFont.pointSizeF() * 0.85);
    p.setFont(labelFont);
    const QFontMetrics fm(labelFont);

    // Y axis (magnitude / gain) ticks at 0 / 0.5*max / max.
    const double yMax = magYMax();
    const QString yTitle = m_showGain ? QStringLiteral("Gain")
                                      : QStringLiteral("Magnitude");
    p.drawText(QPointF(4, plot.top() + fm.ascent()),
               QString::number(yMax, 'f', 1));
    p.drawText(QPointF(4, plot.center().y() + fm.ascent() / 2.0),
               QString::number(yMax / 2.0, 'f', 1));
    p.drawText(QPointF(4, plot.bottom() - 2),
               QStringLiteral("0.0"));
    // Y axis title (rotated).
    p.save();
    p.translate(14, plot.center().y());
    p.rotate(-90);
    p.drawText(QRectF(-50, -10, 100, 20),
               Qt::AlignCenter,
               yTitle);
    p.restore();

    // Y2 axis (phase) labels.
    const double phsMax = phaseYMax();
    p.drawText(QPointF(plot.right() + 4, plot.top() + fm.ascent()),
               QString::number(phsMax, 'f', 0));
    p.drawText(QPointF(plot.right() + 4, plot.center().y() + fm.ascent() / 2.0),
               QStringLiteral("0"));
    p.drawText(QPointF(plot.right() + 4, plot.bottom() - 2),
               QString::number(-phsMax, 'f', 0));

    // X axis ticks.
    p.drawText(QPointF(plot.left() - 6, plot.bottom() + fm.ascent() + 2),
               QStringLiteral("0.0"));
    p.drawText(QPointF(plot.center().x() - fm.horizontalAdvance(QStringLiteral("0.5")) / 2.0,
                       plot.bottom() + fm.ascent() + 2),
               QStringLiteral("0.5"));
    p.drawText(QPointF(plot.right() - fm.horizontalAdvance(QStringLiteral("1.0")),
                       plot.bottom() + fm.ascent() + 2),
               QStringLiteral("1.0"));

    // X axis title.
    p.drawText(QRectF(plot.left(), plot.bottom() + fm.height() + 2,
                      plot.width(), fm.height()),
               Qt::AlignHCenter | Qt::AlignTop,
               QStringLiteral("Input Magnitude"));

    // ── Ref series (DimGray, dashed) ────────────────────────────────────
    // From AmpView.cs:130-141 [v2.10.3.13]:
    //   showgain off → 4 points: (0,0)(1,1)(1,0.5)(0,0.5)
    //   showgain on  → 2 points: (0,1)(1,1)
    {
        QPen refPen(colourRef());
        refPen.setStyle(Qt::DashLine);
        refPen.setWidthF(1.5);
        p.setPen(refPen);
        QPainterPath path;
        if (!m_showGain) {
            path.moveTo(mapMag(plot, 0.0, 1.0, 0.0, 0.0, yMax));
            path.lineTo(mapMag(plot, 1.0, 1.0, 1.0, 0.0, yMax));
            path.lineTo(mapMag(plot, 1.0, 1.0, 0.5, 0.0, yMax));
            path.lineTo(mapMag(plot, 0.0, 1.0, 0.5, 0.0, yMax));
        } else {
            path.moveTo(mapMag(plot, 0.0, 1.0, 1.0, 0.0, yMax));
            path.lineTo(mapMag(plot, 1.0, 1.0, 1.0, 0.0, yMax));
        }
        p.drawPath(path);
    }

    // ── MagAmp (DodgerBlue) — scatter dots, low-res stride applies ──────
    if (!m_x.empty() && m_magAmp.size() == m_x.size()) {
        p.setPen(Qt::NoPen);
        p.setBrush(colourMagAmp());
        const int stride = lowResStride();
        for (size_t i = 0; i < m_x.size(); i += static_cast<size_t>(stride)) {
            const QPointF q = mapMag(plot, m_x[i], 1.0,
                                     m_magAmp[i], 0.0, yMax);
            p.drawEllipse(q, 1.6, 1.6);
        }
    }

    // ── PhsAmp (Gold, secondary axis) — scatter dots ────────────────────
    if (!m_x.empty() && m_phsAmp.size() == m_x.size()) {
        p.setPen(Qt::NoPen);
        p.setBrush(colourPhsAmp());
        const int stride = lowResStride();
        const double pmax = phsMax;
        for (size_t i = 0; i < m_x.size(); i += static_cast<size_t>(stride)) {
            const double phs = std::clamp(m_phsAmp[i], -pmax, pmax);
            const QPointF q = mapPhs(plot, m_x[i], 1.0, phs, -pmax, pmax);
            p.drawEllipse(q, 1.4, 1.4);
        }
    }

    // ── MagCorr (Crimson polyline) ─────────────────────────────────────
    if (!m_magCorr.empty()) {
        QPen pen(colourMagCorr());
        pen.setWidthF(2.0);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        QPainterPath path;
        const size_t n = m_magCorr.size();
        for (size_t i = 0; i < n; ++i) {
            // MagCorr is sampled at uniform x = i / (n-1).
            const double x = (n > 1) ? double(i) / double(n - 1) : 0.0;
            const QPointF q = mapMag(plot, x, 1.0, m_magCorr[i], 0.0, yMax);
            if (i == 0) { path.moveTo(q); } else { path.lineTo(q); }
        }
        p.drawPath(path);
    }

    // ── PhsCorr (Lime polyline) ────────────────────────────────────────
    if (!m_phsCorr.empty()) {
        QPen pen(colourPhsCorr());
        pen.setWidthF(1.6);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        QPainterPath path;
        const size_t n = m_phsCorr.size();
        const double pmax = phsMax;
        for (size_t i = 0; i < n; ++i) {
            const double x = (n > 1) ? double(i) / double(n - 1) : 0.0;
            const double phs = std::clamp(m_phsCorr[i], -pmax, pmax);
            const QPointF q = mapPhs(plot, x, 1.0, phs, -pmax, pmax);
            if (i == 0) { path.moveTo(q); } else { path.lineTo(q); }
        }
        p.drawPath(path);
    }

    // ── Legend (top-right) ──────────────────────────────────────────────
    const int legendX = static_cast<int>(plot.right()) - 95;
    const int legendY = static_cast<int>(plot.top()) + 8;
    QFont legendFont = labelFont;
    p.setFont(legendFont);
    const QFontMetrics lfm(legendFont);
    const int rowH = lfm.height();
    p.fillRect(QRectF(legendX - 4, legendY - 2, 100, 5 * rowH + 6),
               QColor(0, 0, 0, 200));
    p.setPen(QPen(QColor(kAxisGridRgb)));
    p.drawRect(QRectF(legendX - 4, legendY - 2, 100, 5 * rowH + 6));

    struct LegendEntry { QColor c; const char* name; bool dashed; };
    const LegendEntry entries[5] = {
        { colourRef(),     "Ref",                   true  },
        { colourMagAmp(),  m_showGain ? "Gain Amp" : "Mag Amp",  false },
        { colourPhsAmp(),  "Phs Amp",               false },
        { colourMagCorr(), m_showGain ? "Gain Corr" : "Mag Corr", false },
        { colourPhsCorr(), "Phs Corr",              false },
    };
    int yy = legendY + lfm.ascent();
    for (const auto& e : entries) {
        QPen swatchPen(e.c);
        swatchPen.setWidthF(2.0);
        if (e.dashed) { swatchPen.setStyle(Qt::DashLine); }
        p.setPen(swatchPen);
        p.drawLine(QPointF(legendX, yy - lfm.ascent() / 3.0),
                   QPointF(legendX + 16, yy - lfm.ascent() / 3.0));
        p.setPen(QPen(QColor(kAxisLabelRgb)));
        p.drawText(QPointF(legendX + 22, yy),
                   QString::fromLatin1(e.name));
        yy += rowH;
    }
}

} // namespace NereusSDR
