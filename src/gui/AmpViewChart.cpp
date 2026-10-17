// no-port-check: NereusSDR-original QPainter presentation for source-backed
// PS3 display curves.  Axis modes and colours retain Thetis AmpView behavior.
//
// =================================================================
// src/gui/AmpViewChart.cpp  (NereusSDR)
// =================================================================
// Modification history (NereusSDR):
//   2026-05-06 — Phase 3M-4 Task 9 created by J.J. Boyd (KG4VCF),
//                 with AI-assisted transformation via Anthropic
//                 Claude Code.
//   2026-09-22 — Accept owning PS3 curves with independent coordinates and
//                 series visibility; J.J. Boyd (KG4VCF), OpenAI Codex.
//   2026-10-09 - JJ's D22: with the workspace preview on, the chart takes
//                 its container's background through
//                 WorkspacePreview::dataBackground(); grid, traces, labels
//                 and legend unchanged. J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
// =================================================================

// Modification history (NereusSDR):
//   2026-10-12 - Apply the approved workspace-only 11 px text minimum.
//                 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
//   2026-10-13 - Linked plots and cursor; J.J. Boyd (KG4VCF), OpenAI Codex.

#include "AmpViewChart.h"

#include "gui/styles/WorkspacePreview.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>
#include <QKeyEvent>
#include <QMouseEvent>

#include <algorithm>
#include <cmath>

namespace NereusSDR {
namespace {

// AmpView.Designer.cs:56-65 [v2.10.3.13]: black background, DimGray grid.
constexpr QRgb kBackground = qRgb(0x00, 0x00, 0x00);
constexpr QRgb kGrid = qRgb(0x69, 0x69, 0x69);
// AmpView.cs:112-119 [v2.10.3.13]: LightSalmon axis labels.
constexpr QRgb kLabel = qRgb(0xFF, 0xA0, 0x7A);
constexpr int kLeft = 50;
constexpr int kRight = 50;
constexpr int kTop = 18;
constexpr int kBottom = 36;
constexpr int kPlotGap = 40;

const QColor kReference(0x69, 0x69, 0x69);
const QColor kMeasuredMagnitude(0x1E, 0x90, 0xFF);
const QColor kMeasuredPhase(0xFF, 0xD7, 0x00);
const QColor kCorrectionMagnitude(0xDC, 0x14, 0x3C);
const QColor kCorrectionPhase(0x00, 0xFF, 0x00);

QPointF mapPoint(const QRectF& plot, const Ps3PlotPoint& point,
                 double yMin, double yMax)
{
    const double x = std::clamp(point.x, 0.0, 1.0);
    const double y = std::clamp(point.y, yMin, yMax);
    return {plot.left() + x * plot.width(),
            plot.bottom() - (y - yMin) / (yMax - yMin) * plot.height()};
}

void drawPoints(QPainter& painter, const QRectF& plot,
                const std::vector<Ps3PlotPoint>& points, QColor colour,
                double yMin, double yMax, int stride, bool diamonds = false)
{
    painter.setPen(Qt::NoPen);
    painter.setBrush(colour);
    const std::size_t step = static_cast<std::size_t>(std::max(1, stride));
    for (std::size_t i = 0; i < points.size(); i += step) {
        if (std::isfinite(points[i].x) && std::isfinite(points[i].y)) {
            const QPointF mapped = mapPoint(plot, points[i], yMin, yMax);
            if (diamonds) {
                QPolygonF diamond{QPointF(mapped.x(), mapped.y() - 3.5),
                                  QPointF(mapped.x() + 3.5, mapped.y()),
                                  QPointF(mapped.x(), mapped.y() + 3.5),
                                  QPointF(mapped.x() - 3.5, mapped.y())};
                painter.drawPolygon(diamond);
            } else {
                painter.drawEllipse(mapped, 2.3, 2.3);
            }
        }
    }
}

void drawLine(QPainter& painter, const QRectF& plot,
              const std::vector<Ps3PlotPoint>& points, QColor colour,
              double yMin, double yMax)
{
    QPainterPath path;
    bool started = false;
    for (const Ps3PlotPoint& point : points) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
            started = false;
            continue;
        }
        const QPointF mapped = mapPoint(plot, point, yMin, yMax);
        if (started) {
            path.lineTo(mapped);
        } else {
            path.moveTo(mapped);
            started = true;
        }
    }
    QPen pen(colour, 1.8);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
}

std::size_t indexOf(AmpViewChart::Series series)
{
    return static_cast<std::size_t>(series);
}

// calcc.c:448-475,505-529,1263-1295,1791-1796 (TAPR WDSP b02d5bac):
// measured samples are published in bucket-major physical-slot order. Each
// bucket advances a sequential circular slot index and overwrites slots modulo
// 256 once full. Keep that published order for the painted cloud and make an
// independent GUI-only lookup copy because plot X is not guaranteed to be
// strictly monotonic inside a bucket.
std::vector<Ps3PlotPoint> measuredLookup(const std::vector<Ps3PlotPoint>& points)
{
    for (const Ps3PlotPoint& point : points) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
            return {};
        }
    }
    std::vector<Ps3PlotPoint> lookup = points;
    std::sort(lookup.begin(), lookup.end(),
              [](const Ps3PlotPoint& left, const Ps3PlotPoint& right) {
                  return left.x < right.x
                      || (left.x == right.x && left.y < right.y);
              });
    lookup.erase(std::unique(lookup.begin(), lookup.end(),
                              [](const Ps3PlotPoint& left, const Ps3PlotPoint& right) {
                                  return left.x == right.x && left.y == right.y;
                              }), lookup.end());
    return lookup;
}

std::optional<double> interpolate(const std::vector<Ps3PlotPoint>& points,
                                  double x, bool measured, bool wrappedPhase = false)
{
    if (!std::isfinite(x) || x < 0.0 || x > 1.0 || points.empty()) {
        return std::nullopt;
    }
    if (!measured) {
        for (std::size_t i = 0; i < points.size(); ++i) {
            const Ps3PlotPoint& point = points[i];
            if (!std::isfinite(point.x) || !std::isfinite(point.y)
                || (i > 0 && point.x <= points[i - 1].x)) {
                return std::nullopt;
            }
        }
    }
    const auto right = std::lower_bound(points.begin(), points.end(), x,
                                        [](const Ps3PlotPoint& point, double input) {
                                            return point.x < input;
                                        });
    if (right != points.end() && right->x == x) {
        if (measured && ((right != points.begin() && (right - 1)->x == x)
                         || (right + 1 != points.end() && (right + 1)->x == x))) {
            return std::nullopt;
        }
        return right->y;
    }
    if (right == points.begin() || right == points.end()) {
        return std::nullopt;
    }
    const auto left = right - 1;
    if (measured && ((left != points.begin() && (left - 1)->x == left->x)
                     || (right + 1 != points.end() && (right + 1)->x == right->x))) {
        return std::nullopt;
    }
    // The display adapter wraps measured phase to [-180, 180]. Interpolating
    // across that branch cut would invent a near-zero phase reading.
    if (wrappedPhase && std::abs(right->y - left->y) > 180.0) {
        return std::nullopt;
    }
    return left->y + (right->y - left->y)
        * (x - left->x) / (right->x - left->x);
}

} // namespace

AmpViewChart::AmpViewChart(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ampViewChart"));
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setFocusPolicy(Qt::StrongFocus);
}

AmpViewChart::~AmpViewChart() = default;

void AmpViewChart::setPlotData(const Ps3PlotData& data)
{
    m_plot = data;
    m_measuredMagnitudeLookup = measuredLookup(m_plot.measuredMagnitude);
    m_measuredGainLookup = measuredLookup(m_plot.measuredGain);
    m_measuredPhaseLookup = measuredLookup(m_plot.measuredPhase);
    update();
}

void AmpViewChart::clearData()
{
    m_plot = {};
    m_measuredMagnitudeLookup.clear();
    m_measuredGainLookup.clear();
    m_measuredPhaseLookup.clear();
    update();
}

AmpViewCursorReadout AmpViewChart::readoutAt(double inputMagnitude) const
{
    AmpViewCursorReadout result;
    result.inputMagnitude = inputMagnitude;
    if (!std::isfinite(inputMagnitude) || inputMagnitude < 0.0
        || inputMagnitude > 1.0) {
        return result;
    }
    if (seriesVisible(Series::MeasuredMagnitude)
        && (!m_showGain || inputMagnitude > 0.0)) {
        result.magnitudeAmp = interpolate(m_showGain ? m_measuredGainLookup
                                                     : m_measuredMagnitudeLookup,
                                          inputMagnitude, true);
    }
    if (seriesVisible(Series::CorrectionMagnitude)) {
        result.magnitudeCorrection = interpolate(m_showGain ? m_plot.correctionGain
                                                            : m_plot.correctionMagnitude,
                                                 inputMagnitude, false);
    }
    if (seriesVisible(Series::MeasuredPhase)) {
        result.phaseAmp = interpolate(m_measuredPhaseLookup, inputMagnitude, true, true);
    }
    if (seriesVisible(Series::CorrectionPhase)) {
        result.phaseCorrection = interpolate(m_plot.correctionPhase, inputMagnitude, false);
    }
    return result;
}

void AmpViewChart::setSelectedInput(double inputMagnitude)
{
    if (!std::isfinite(inputMagnitude)) {
        return;
    }
    const double selected = std::clamp(inputMagnitude, 0.0, 1.0);
    if (selected == m_selectedInput) {
        return;
    }
    m_selectedInput = selected;
    update();
    emit selectedInputChanged(selected);
}

QRectF AmpViewChart::plotBounds() const
{
    return QRectF(kLeft, kTop, width() - kLeft - kRight,
                  height() - kTop - kBottom);
}

void AmpViewChart::selectAtX(double x)
{
    const QRectF plot = plotBounds();
    if (plot.width() > 0.0) {
        setSelectedInput((x - plot.left()) / plot.width());
    }
}

void AmpViewChart::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        setFocus();
        selectAtX(event->position().x());
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void AmpViewChart::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons().testFlag(Qt::LeftButton)) {
        selectAtX(event->position().x());
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void AmpViewChart::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) {
        setSelectedInput(m_selectedInput + (event->key() == Qt::Key_Left ? -0.01 : 0.01));
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Home || event->key() == Qt::Key_End) {
        setSelectedInput(event->key() == Qt::Key_Home ? 0.0 : 1.0);
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void AmpViewChart::setShowGain(bool on)
{
    if (m_showGain == on) {
        return;
    }
    m_showGain = on;
    update();
}

void AmpViewChart::setPhaseZoom(bool on)
{
    if (m_phaseZoom == on) {
        return;
    }
    m_phaseZoom = on;
    update();
}

void AmpViewChart::setLowRes(bool on)
{
    if (m_lowRes == on) {
        return;
    }
    m_lowRes = on;
    update();
}

void AmpViewChart::setSeriesVisible(Series series, bool visible)
{
    const std::size_t index = indexOf(series);
    if (index >= m_visible.size() || m_visible[index] == visible) {
        return;
    }
    m_visible[index] = visible;
    update();
}

bool AmpViewChart::seriesVisible(Series series) const noexcept
{
    const std::size_t index = indexOf(series);
    return index < m_visible.size() && m_visible[index];
}

QStringList AmpViewChart::seriesNames() const
{
    return {QStringLiteral("Reference"),
            QStringLiteral("Measured magnitude"),
            QStringLiteral("Measured phase"),
            QStringLiteral("Correction magnitude"),
            QStringLiteral("Correction phase")};
}

void AmpViewChart::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), WorkspacePreview::dataBackground(QColor(kBackground), "app-bg", this));

    const QRectF bounds = plotBounds();
    if (!bounds.isValid() || bounds.height() <= kPlotGap + 40) {
        return;
    }
    const double plotHeight = (bounds.height() - kPlotGap) / 2.0;
    const QRectF magnitudePlot(bounds.left(), bounds.top(), bounds.width(), plotHeight);
    const QRectF phasePlot(bounds.left(), magnitudePlot.bottom() + kPlotGap,
                           bounds.width(), plotHeight);

    painter.setPen(QPen(WorkspacePreview::roleColour(QColor(kGrid), "border-subtle"), 1.0));
    for (const QRectF& plot : {magnitudePlot, phasePlot}) {
        for (int i = 1; i < 5; ++i) {
            const double x = plot.left() + plot.width() * i / 5.0;
            painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        }
        for (int i = 1; i < 4; ++i) {
            const double y = plot.top() + plot.height() * i / 4.0;
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }
        painter.drawRect(plot);
    }

    QFont labels = WorkspacePreview::readableFont(painter.font(), painter.device());
    if (labels.pointSizeF() > 0) {
        labels.setPointSizeF(labels.pointSizeF() * 0.85);
    }
    labels = WorkspacePreview::readableFont(labels, painter.device());
    const int logicalPixels = labels.pixelSize() > 0
        ? labels.pixelSize()
        : qRound(labels.pointSizeF() * painter.device()->logicalDpiY() / 72.0);
    if (logicalPixels < 11) {
        labels.setPixelSize(11);
    }
    painter.setFont(labels);
    const QFontMetrics metrics(labels,painter.device());
    painter.setPen(WorkspacePreview::roleColour(QColor(kLabel), "text-secondary"));

    const double magnitudeMax = magYMax();
    const double phaseMax = phaseYMax();
    painter.drawText(QPointF(4, magnitudePlot.top() + metrics.ascent()),
                     QString::number(magnitudeMax, 'f', 1));
    painter.drawText(QPointF(4, magnitudePlot.center().y() + metrics.ascent() / 2.0),
                     QString::number(magnitudeMax / 2.0, 'f', 1));
    painter.drawText(QPointF(4, magnitudePlot.bottom() - 2), QStringLiteral("0.0"));
    painter.drawText(QPointF(bounds.left(), magnitudePlot.bottom() + metrics.ascent() + 2),
                     m_showGain ? QStringLiteral("AMPLIFIER GAIN") : QStringLiteral("AMPLIFIER MAGNITUDE"));
    painter.drawText(QPointF(bounds.left(), phasePlot.top() - 5), QStringLiteral("PHASE · DEGREES"));
    painter.drawText(QPointF(4, phasePlot.top() + metrics.ascent()),
                     QString::number(phaseMax, 'f', 0));
    painter.drawText(QPointF(4,
                             phasePlot.center().y() + metrics.ascent() / 2.0),
                     QStringLiteral("0"));
    painter.drawText(QPointF(4, phasePlot.bottom() - 2),
                     QString::number(-phaseMax, 'f', 0));
    painter.drawText(QRectF(phasePlot.left(), phasePlot.bottom() + 2,
                            phasePlot.width(), metrics.height()),
                     Qt::AlignCenter, QStringLiteral("Input magnitude"));

    if (seriesVisible(Series::Reference)) {
        QPen referencePen(WorkspacePreview::roleColour(kReference, "text-tertiary"),
                          1.5, Qt::DashLine);
        painter.setPen(referencePen);
        const double startY = m_showGain ? 1.0 : 0.0;
        const double endY = 1.0;
        painter.drawLine(mapPoint(magnitudePlot, {0.0, startY}, 0.0, magnitudeMax),
                         mapPoint(magnitudePlot, {1.0, endY}, 0.0, magnitudeMax));
    }

    const auto& measuredMagnitude = m_showGain
        ? m_plot.measuredGain : m_plot.measuredMagnitude;
    const auto& correctionMagnitude = m_showGain
        ? m_plot.correctionGain : m_plot.correctionMagnitude;
    if (seriesVisible(Series::MeasuredMagnitude)) {
        drawPoints(painter, magnitudePlot, measuredMagnitude,
                   WorkspacePreview::enabled() ? QColor("#80a6c6") : kMeasuredMagnitude,
                   0.0, magnitudeMax, lowResStride());
    }
    if (seriesVisible(Series::MeasuredPhase)) {
        drawPoints(painter, phasePlot, m_plot.measuredPhase,
                   WorkspacePreview::enabled() ? QColor("#bf9cb8") : kMeasuredPhase,
                   -phaseMax, phaseMax, lowResStride(), true);
    }
    if (seriesVisible(Series::CorrectionMagnitude)) {
        drawLine(painter, magnitudePlot, correctionMagnitude,
                 WorkspacePreview::enabled() ? QColor("#d8a55f") : kCorrectionMagnitude,
                 0.0, magnitudeMax);
    }
    if (seriesVisible(Series::CorrectionPhase)) {
        drawLine(painter, phasePlot, m_plot.correctionPhase,
                 WorkspacePreview::enabled() ? QColor("#83b497") : kCorrectionPhase,
                 -phaseMax, phaseMax);
    }

    const double cursorX = bounds.left() + m_selectedInput * bounds.width();
    painter.setPen(QPen(WorkspacePreview::roleColour(QColor(kLabel), "text-tertiary"), 1.0,
                        Qt::DashLine));
    painter.drawLine(QPointF(cursorX, magnitudePlot.top()),
                     QPointF(cursorX, magnitudePlot.bottom()));
    painter.drawLine(QPointF(cursorX, phasePlot.top()),
                     QPointF(cursorX, phasePlot.bottom()));


}

} // namespace NereusSDR
