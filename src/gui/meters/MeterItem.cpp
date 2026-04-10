#include "MeterItem.h"

#include <QPainter>
#include <QFont>
#include <QFontMetrics>
#include <QStringList>
#include <cmath>
#include <algorithm>
#include <QtMath>

namespace NereusSDR {

// ---------------------------------------------------------------------------
// MeterItem base — serialize / deserialize
// Format: x|y|w|h|bindingId|zOrder
// ---------------------------------------------------------------------------

QString MeterItem::serialize() const
{
    return QStringLiteral("%1|%2|%3|%4|%5|%6")
        .arg(static_cast<double>(m_x))
        .arg(static_cast<double>(m_y))
        .arg(static_cast<double>(m_w))
        .arg(static_cast<double>(m_h))
        .arg(m_bindingId)
        .arg(m_zOrder);
}

bool MeterItem::deserialize(const QString& data)
{
    const QStringList parts = data.split(QLatin1Char('|'));
    if (parts.size() < 6) {
        return false;
    }

    bool ok = true;
    const float x  = parts[0].toFloat(&ok);  if (!ok) { return false; }
    const float y  = parts[1].toFloat(&ok);  if (!ok) { return false; }
    const float w  = parts[2].toFloat(&ok);  if (!ok) { return false; }
    const float h  = parts[3].toFloat(&ok);  if (!ok) { return false; }
    const int bid  = parts[4].toInt(&ok);    if (!ok) { return false; }
    const int zo   = parts[5].toInt(&ok);    if (!ok) { return false; }

    setRect(x, y, w, h);
    setBindingId(bid);
    setZOrder(zo);
    return true;
}

// ---------------------------------------------------------------------------
// Helper: build the base fields string (indices 1-6) for concrete types.
// Concrete types prepend their prefix and append type-specific fields.
// ---------------------------------------------------------------------------
static QString baseFields(const MeterItem& item)
{
    return QStringLiteral("%1|%2|%3|%4|%5|%6")
        .arg(static_cast<double>(item.x()))
        .arg(static_cast<double>(item.y()))
        .arg(static_cast<double>(item.itemWidth()))
        .arg(static_cast<double>(item.itemHeight()))
        .arg(item.bindingId())
        .arg(item.zOrder());
}

// Helper: parse base fields from indices 1..6 of parts (index 0 is the prefix).
static bool parseBaseFields(MeterItem& item, const QStringList& parts)
{
    if (parts.size() < 7) {
        return false;
    }
    // Re-join indices 1..6 and delegate to the base deserialize
    const QString base = QStringList(parts.mid(1, 6)).join(QLatin1Char('|'));
    return item.MeterItem::deserialize(base);
}

// ---------------------------------------------------------------------------
// SolidColourItem
// Format: SOLID|x|y|w|h|bindingId|zOrder|#aarrggbb
// ---------------------------------------------------------------------------

void SolidColourItem::paint(QPainter& p, int widgetW, int widgetH)
{
    p.fillRect(pixelRect(widgetW, widgetH), m_colour);
}

QString SolidColourItem::serialize() const
{
    return QStringLiteral("SOLID|%1|%2")
        .arg(baseFields(*this))
        .arg(m_colour.name(QColor::HexArgb));
}

bool SolidColourItem::deserialize(const QString& data)
{
    const QStringList parts = data.split(QLatin1Char('|'));
    if (parts.size() < 8 || parts[0] != QLatin1String("SOLID")) {
        return false;
    }
    if (!parseBaseFields(*this, parts)) {
        return false;
    }
    m_colour = QColor(parts[7]);
    return m_colour.isValid();
}

// ---------------------------------------------------------------------------
// ImageItem
// Format: IMAGE|x|y|w|h|bindingId|zOrder|path
// ---------------------------------------------------------------------------

void ImageItem::setImagePath(const QString& path)
{
    m_imagePath = path;
    m_image.load(path);
}

void ImageItem::paint(QPainter& p, int widgetW, int widgetH)
{
    if (m_image.isNull()) {
        return;
    }
    const QRect rect = pixelRect(widgetW, widgetH);
    p.drawImage(rect, m_image);
}

QString ImageItem::serialize() const
{
    return QStringLiteral("IMAGE|%1|%2")
        .arg(baseFields(*this))
        .arg(m_imagePath);
}

bool ImageItem::deserialize(const QString& data)
{
    const QStringList parts = data.split(QLatin1Char('|'));
    if (parts.size() < 8 || parts[0] != QLatin1String("IMAGE")) {
        return false;
    }
    if (!parseBaseFields(*this, parts)) {
        return false;
    }
    setImagePath(parts[7]);
    return true;
}

// ---------------------------------------------------------------------------
// BarItem
// Format: BAR|x|y|w|h|bindingId|zOrder|orientation|minVal|maxVal|
//             barColor|barRedColor|redThreshold|attackRatio|decayRatio
// ---------------------------------------------------------------------------

// Override setValue() for exponential smoothing.
// From Thetis MeterManager.cs — attack when rising, decay when falling.
void BarItem::setValue(double v)
{
    MeterItem::setValue(v);
    if (v > m_smoothedValue) {
        m_smoothedValue = static_cast<double>(m_attackRatio) * v
                        + (1.0 - static_cast<double>(m_attackRatio)) * m_smoothedValue;
    } else {
        m_smoothedValue = static_cast<double>(m_decayRatio) * v
                        + (1.0 - static_cast<double>(m_decayRatio)) * m_smoothedValue;
    }
}

void BarItem::paint(QPainter& p, int widgetW, int widgetH)
{
    const QRect rect = pixelRect(widgetW, widgetH);
    const double range = m_maxVal - m_minVal;
    const double frac  = (range > 0.0)
        ? std::clamp((m_smoothedValue - m_minVal) / range, 0.0, 1.0)
        : 0.0;

    const QColor& fillColor = (m_smoothedValue >= m_redThreshold) ? m_barRedColor : m_barColor;

    if (m_orientation == Orientation::Horizontal) {
        const int fillW = static_cast<int>(frac * rect.width());
        p.fillRect(QRect(rect.left(), rect.top(), fillW, rect.height()), fillColor);
    } else {
        const int fillH = static_cast<int>(frac * rect.height());
        // Vertical fills from bottom
        p.fillRect(QRect(rect.left(), rect.bottom() - fillH, rect.width(), fillH), fillColor);
    }
}

void BarItem::emitVertices(QVector<float>& verts, int widgetW, int widgetH)
{
    Q_UNUSED(widgetW); Q_UNUSED(widgetH);

    const double range = m_maxVal - m_minVal;
    const double frac  = (range > 0.0)
        ? std::clamp((m_smoothedValue - m_minVal) / range, 0.0, 1.0)
        : 0.0;

    // Convert normalized item rect to NDC
    const float ndcL = m_x * 2.0f - 1.0f;
    const float ndcR = (m_x + m_w) * 2.0f - 1.0f;
    const float ndcT = 1.0f - m_y * 2.0f;
    const float ndcB = 1.0f - (m_y + m_h) * 2.0f;

    const QColor& fillColor = (m_smoothedValue >= m_redThreshold) ? m_barRedColor : m_barColor;
    const float r = static_cast<float>(fillColor.redF());
    const float g = static_cast<float>(fillColor.greenF());
    const float b = static_cast<float>(fillColor.blueF());
    const float a = static_cast<float>(fillColor.alphaF());

    // Compute filled quad corners
    float qL, qR, qT, qB;
    if (m_orientation == Orientation::Horizontal) {
        qL = ndcL;
        qR = ndcL + static_cast<float>(frac) * (ndcR - ndcL);
        qT = ndcT;
        qB = ndcB;
    } else {
        // Vertical: fill from bottom
        qL = ndcL;
        qR = ndcR;
        qB = ndcB;
        qT = ndcB + static_cast<float>(frac) * (ndcT - ndcB);
    }

    // Emit 6 vertices (2 triangles) for Triangles topology.
    // This avoids degenerate strip artifacts when multiple bars share the VBO.
    // Triangle 1: TL, BL, TR
    verts << qL << qT << r << g << b << a;
    verts << qL << qB << r << g << b << a;
    verts << qR << qT << r << g << b << a;
    // Triangle 2: TR, BL, BR
    verts << qR << qT << r << g << b << a;
    verts << qL << qB << r << g << b << a;
    verts << qR << qB << r << g << b << a;
}

QString BarItem::serialize() const
{
    return QStringLiteral("BAR|%1|%2|%3|%4|%5|%6|%7|%8|%9")
        .arg(baseFields(*this))
        .arg(m_orientation == Orientation::Horizontal ? QStringLiteral("H") : QStringLiteral("V"))
        .arg(m_minVal)
        .arg(m_maxVal)
        .arg(m_barColor.name(QColor::HexArgb))
        .arg(m_barRedColor.name(QColor::HexArgb))
        .arg(m_redThreshold)
        .arg(static_cast<double>(m_attackRatio))
        .arg(static_cast<double>(m_decayRatio));
}

bool BarItem::deserialize(const QString& data)
{
    const QStringList parts = data.split(QLatin1Char('|'));
    if (parts.size() < 15 || parts[0] != QLatin1String("BAR")) {
        return false;
    }
    if (!parseBaseFields(*this, parts)) {
        return false;
    }

    m_orientation = (parts[7] == QLatin1String("H")) ? Orientation::Horizontal : Orientation::Vertical;

    bool ok = true;
    m_minVal       = parts[8].toDouble(&ok);  if (!ok) { return false; }
    m_maxVal       = parts[9].toDouble(&ok);  if (!ok) { return false; }
    m_barColor     = QColor(parts[10]);       if (!m_barColor.isValid()) { return false; }
    m_barRedColor  = QColor(parts[11]);       if (!m_barRedColor.isValid()) { return false; }
    m_redThreshold = parts[12].toDouble(&ok); if (!ok) { return false; }
    m_attackRatio  = parts[13].toFloat(&ok);  if (!ok) { return false; }
    m_decayRatio   = parts[14].toFloat(&ok);  if (!ok) { return false; }

    return true;
}

// ---------------------------------------------------------------------------
// ScaleItem
// Format: SCALE|x|y|w|h|bindingId|zOrder|orientation|minVal|maxVal|
//               majorTicks|minorTicks|tickColor|labelColor|fontSize
// ---------------------------------------------------------------------------

void ScaleItem::paint(QPainter& p, int widgetW, int widgetH)
{
    const QRect rect = pixelRect(widgetW, widgetH);
    const double range = m_maxVal - m_minVal;
    if (range <= 0.0 || m_majorTicks < 2) {
        return;
    }

    QFont font = p.font();
    font.setPointSize(m_fontSize);
    p.setFont(font);

    if (m_orientation == Orientation::Horizontal) {
        // Major ticks with labels — ticks go downward from top, labels below
        for (int i = 0; i < m_majorTicks; ++i) {
            const double frac = static_cast<double>(i) / (m_majorTicks - 1);
            const int x = rect.left() + static_cast<int>(frac * rect.width());

            p.setPen(m_tickColor);
            p.drawLine(x, rect.top(), x, rect.top() + rect.height() / 3);

            const double val = m_minVal + frac * range;
            const QString label = QString::number(val, 'f', 0);
            p.setPen(m_labelColor);
            const QFontMetrics fm(font);
            const int labelW = fm.horizontalAdvance(label);
            p.drawText(x - labelW / 2, rect.top() + rect.height() / 3 + fm.ascent(), label);

            // Minor ticks (between major ticks, except after last major)
            if (i < m_majorTicks - 1 && m_minorTicks > 0) {
                const double nextFrac = static_cast<double>(i + 1) / (m_majorTicks - 1);
                for (int j = 1; j <= m_minorTicks; ++j) {
                    const double minorFrac = frac + (nextFrac - frac) * (static_cast<double>(j) / (m_minorTicks + 1));
                    const int mx = rect.left() + static_cast<int>(minorFrac * rect.width());
                    p.setPen(m_tickColor);
                    p.drawLine(mx, rect.top(), mx, rect.top() + rect.height() / 6);
                }
            }
        }
    } else {
        // Vertical — ticks go left from right edge, labels to the left
        for (int i = 0; i < m_majorTicks; ++i) {
            const double frac = static_cast<double>(i) / (m_majorTicks - 1);
            // Top = maxVal, bottom = minVal for vertical orientation
            const int y = rect.top() + static_cast<int>((1.0 - frac) * rect.height());

            p.setPen(m_tickColor);
            p.drawLine(rect.right(), y, rect.right() - rect.width() / 3, y);

            const double val = m_minVal + frac * range;
            const QString label = QString::number(val, 'f', 0);
            p.setPen(m_labelColor);
            const QFontMetrics fm(font);
            const int labelW = fm.horizontalAdvance(label);
            const int labelX = rect.right() - rect.width() / 3 - labelW - 2;
            p.drawText(labelX, y + fm.ascent() / 2, label);

            // Minor ticks
            if (i < m_majorTicks - 1 && m_minorTicks > 0) {
                const double nextFrac = static_cast<double>(i + 1) / (m_majorTicks - 1);
                for (int j = 1; j <= m_minorTicks; ++j) {
                    const double minorFrac = frac + (nextFrac - frac) * (static_cast<double>(j) / (m_minorTicks + 1));
                    const int my = rect.top() + static_cast<int>((1.0 - minorFrac) * rect.height());
                    p.setPen(m_tickColor);
                    p.drawLine(rect.right(), my, rect.right() - rect.width() / 6, my);
                }
            }
        }
    }
}

QString ScaleItem::serialize() const
{
    return QStringLiteral("SCALE|%1|%2|%3|%4|%5|%6|%7|%8|%9")
        .arg(baseFields(*this))
        .arg(m_orientation == Orientation::Horizontal ? QStringLiteral("H") : QStringLiteral("V"))
        .arg(m_minVal)
        .arg(m_maxVal)
        .arg(m_majorTicks)
        .arg(m_minorTicks)
        .arg(m_tickColor.name(QColor::HexArgb))
        .arg(m_labelColor.name(QColor::HexArgb))
        .arg(m_fontSize);
}

bool ScaleItem::deserialize(const QString& data)
{
    const QStringList parts = data.split(QLatin1Char('|'));
    if (parts.size() < 15 || parts[0] != QLatin1String("SCALE")) {
        return false;
    }
    if (!parseBaseFields(*this, parts)) {
        return false;
    }

    m_orientation = (parts[7] == QLatin1String("H")) ? Orientation::Horizontal : Orientation::Vertical;

    bool ok = true;
    m_minVal     = parts[8].toDouble(&ok);  if (!ok) { return false; }
    m_maxVal     = parts[9].toDouble(&ok);  if (!ok) { return false; }
    m_majorTicks = parts[10].toInt(&ok);    if (!ok) { return false; }
    m_minorTicks = parts[11].toInt(&ok);    if (!ok) { return false; }
    m_tickColor  = QColor(parts[12]);       if (!m_tickColor.isValid()) { return false; }
    m_labelColor = QColor(parts[13]);       if (!m_labelColor.isValid()) { return false; }
    m_fontSize   = parts[14].toInt(&ok);    if (!ok) { return false; }

    return true;
}

// ---------------------------------------------------------------------------
// TextItem
// Format: TEXT|x|y|w|h|bindingId|zOrder|label|textColor|fontSize|bold(0/1)|suffix|decimals
// ---------------------------------------------------------------------------

void TextItem::paint(QPainter& p, int widgetW, int widgetH)
{
    const QRect rect = pixelRect(widgetW, widgetH);

    QFont font = p.font();
    font.setPointSize(m_fontSize);
    font.setBold(m_bold);
    p.setFont(font);
    p.setPen(m_textColor);

    QString text;
    if (m_bindingId >= 0) {
        text = QString::number(m_value, 'f', m_decimals) + m_suffix;
    } else {
        text = m_label;
    }

    p.drawText(rect, Qt::AlignCenter, text);
}

QString TextItem::serialize() const
{
    return QStringLiteral("TEXT|%1|%2|%3|%4|%5|%6|%7")
        .arg(baseFields(*this))
        .arg(m_label)
        .arg(m_textColor.name(QColor::HexArgb))
        .arg(m_fontSize)
        .arg(m_bold ? 1 : 0)
        .arg(m_suffix)
        .arg(m_decimals);
}

bool TextItem::deserialize(const QString& data)
{
    const QStringList parts = data.split(QLatin1Char('|'));
    if (parts.size() < 13 || parts[0] != QLatin1String("TEXT")) {
        return false;
    }
    if (!parseBaseFields(*this, parts)) {
        return false;
    }

    m_label     = parts[7];
    m_textColor = QColor(parts[8]);
    if (!m_textColor.isValid()) { return false; }

    bool ok = true;
    m_fontSize  = parts[9].toInt(&ok);   if (!ok) { return false; }
    m_bold      = parts[10].toInt(&ok) != 0; if (!ok) { return false; }
    m_suffix    = parts[11];
    m_decimals  = parts[12].toInt(&ok); if (!ok) { return false; }

    return true;
}

// ---------------------------------------------------------------------------
// NeedleItem — Arc-style S-meter needle
// Ported from AetherSDR src/gui/SMeterWidget.cpp
// ---------------------------------------------------------------------------

NeedleItem::NeedleItem(QObject* parent)
    : MeterItem(parent)
{
}

bool NeedleItem::participatesIn(Layer layer) const
{
    // NeedleItem renders across all 4 pipeline layers
    Q_UNUSED(layer);
    return true;
}

// From AetherSDR SMeterWidget.cpp setLevel() — SMOOTH_ALPHA = 0.3
void NeedleItem::setValue(double v)
{
    MeterItem::setValue(v);

    const float dbm = static_cast<float>(v);

    // Exponential smoothing (from AetherSDR SMOOTH_ALPHA = 0.3)
    m_smoothedDbm += kSmoothAlpha * (dbm - m_smoothedDbm);
    m_smoothedDbm = std::clamp(m_smoothedDbm, kS0Dbm, kMaxDbm);

    // Peak tracking (from AetherSDR Medium preset: 500ms hold, 10 dB/sec decay)
    if (m_smoothedDbm > m_peakDbm) {
        m_peakDbm = m_smoothedDbm;
        m_peakHoldCounter = kPeakHoldFrames;
    } else if (m_peakHoldCounter > 0) {
        --m_peakHoldCounter;
    } else {
        m_peakDbm -= kPeakDecayPerFrame;
        if (m_peakDbm < m_smoothedDbm) {
            m_peakDbm = m_smoothedDbm;
        }
    }

    // Hard reset every 10 seconds (from AetherSDR m_peakReset = 10000ms)
    ++m_peakResetCounter;
    if (m_peakResetCounter >= kPeakResetFrames) {
        m_peakDbm = m_smoothedDbm;
        m_peakResetCounter = 0;
    }
}

// From AetherSDR SMeterWidget.cpp dbmToFraction()
// S0-S9 occupies left 60% of arc; S9-S9+60 occupies right 40%
float NeedleItem::dbmToFraction(float dbm) const
{
    const float clamped = std::clamp(dbm, kS0Dbm, kMaxDbm);
    if (clamped <= kS9Dbm) {
        return 0.6f * (clamped - kS0Dbm) / (kS9Dbm - kS0Dbm);
    }
    return 0.6f + 0.4f * (clamped - kS9Dbm) / (kMaxDbm - kS9Dbm);
}

// From AetherSDR SMeterWidget.cpp sUnitsText()
QString NeedleItem::sUnitsText(float dbm) const
{
    if (dbm <= kS0Dbm) {
        return QStringLiteral("S0");
    }
    if (dbm <= kS9Dbm) {
        const int sUnit = qBound(0, qRound((dbm - kS0Dbm) / kDbPerS), 9);
        return QStringLiteral("S%1").arg(sUnit);
    }
    const int over = qRound(dbm - kS9Dbm);
    return QStringLiteral("S9+%1").arg(over);
}

void NeedleItem::paintForLayer(QPainter& p, int widgetW, int widgetH, Layer layer)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    switch (layer) {
    case Layer::Background:     paintBackground(p, widgetW, widgetH); break;
    case Layer::OverlayStatic:  paintOverlayStatic(p, widgetW, widgetH); break;
    case Layer::OverlayDynamic: paintOverlayDynamic(p, widgetW, widgetH); break;
    default: break; // Geometry handled by emitVertices()
    }
    p.restore();
}

void NeedleItem::paint(QPainter& p, int widgetW, int widgetH)
{
    // CPU fallback: render all layers in a single QPainter pass
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    paintBackground(p, widgetW, widgetH);
    paintOverlayStatic(p, widgetW, widgetH);

    // CPU needle: draw as a simple line (no vertex geometry available)
    const QRect rect = pixelRect(widgetW, widgetH);
    const float pw = static_cast<float>(rect.width());
    const float ph = static_cast<float>(rect.height());
    const float cx = rect.left() + pw * 0.5f;
    const float radius = pw * kRadiusRatio;
    const float cy = rect.top() + ph + radius - ph * kCenterYRatio;
    const float pivotY = rect.top() + ph + 6.0f;

    const float frac = dbmToFraction(m_smoothedDbm);
    const float angleRad = qDegreesToRadians(kArcEndDeg - frac * (kArcEndDeg - kArcStartDeg));
    const float tipDist = radius + 14.0f;
    const float tipX = cx + tipDist * std::cos(angleRad);
    const float tipY = cy - tipDist * std::sin(angleRad);

    // Shadow
    QPen shadowPen(QColor(0, 0, 0, 80));
    shadowPen.setWidthF(3.0f);
    p.setPen(shadowPen);
    p.drawLine(QPointF(cx + 1.0f, pivotY + 1.0f), QPointF(tipX + 1.0f, tipY + 1.0f));

    // Needle
    QPen needlePen(Qt::white);
    needlePen.setWidthF(2.0f);
    p.setPen(needlePen);
    p.drawLine(QPointF(cx, pivotY), QPointF(tipX, tipY));

    paintOverlayDynamic(p, widgetW, widgetH);
    p.restore();
}

// From AetherSDR SMeterWidget.cpp paintEvent() — arc drawing section
void NeedleItem::paintBackground(QPainter& p, int widgetW, int widgetH)
{
    const QRect rect = pixelRect(widgetW, widgetH);
    const float pw = static_cast<float>(rect.width());
    const float ph = static_cast<float>(rect.height());

    // From AetherSDR: cx = width*0.5, radius = width*0.85
    // cy = height + radius - height*0.65
    const float cx = rect.left() + pw * 0.5f;
    const float radius = pw * kRadiusRatio;
    const float cy = rect.top() + ph + radius - ph * kCenterYRatio;

    const QRectF arcRect(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);

    // S9 at fraction 0.6 → angle = 125 - 0.6*70 = 83 degrees
    const float arcSpan = kArcEndDeg - kArcStartDeg;  // 70 degrees
    const float s9Frac = 0.6f;
    const float s9Angle = kArcEndDeg - s9Frac * arcSpan;  // 83 degrees

    QPen arcPen;
    arcPen.setWidthF(3.0f);
    arcPen.setCapStyle(Qt::FlatCap);

    // White segment: S9 angle to ARC_END (left portion, S0-S9)
    // QPainter drawArc: angles in 1/16 degrees, 0=3 o'clock, positive=CCW
    arcPen.setColor(QColor(0xc8, 0xd8, 0xe8));  // From AetherSDR
    p.setPen(arcPen);
    const int whiteStart = static_cast<int>(s9Angle * 16.0f);
    const int whiteSpan  = static_cast<int>((kArcEndDeg - s9Angle) * 16.0f);
    p.drawArc(arcRect, whiteStart, whiteSpan);

    // Red segment: ARC_START to S9 angle (right portion, S9+)
    arcPen.setColor(QColor(0xff, 0x44, 0x44));  // From AetherSDR
    p.setPen(arcPen);
    const int redStart = static_cast<int>(kArcStartDeg * 16.0f);
    const int redSpan  = static_cast<int>((s9Angle - kArcStartDeg) * 16.0f);
    p.drawArc(arcRect, redStart, redSpan);
}

// Needle geometry: tapered quad from pivot to tip (2 triangles = 6 verts).
// Peak marker: amber triangle at arc edge (3 verts).
// From AetherSDR SMeterWidget.cpp paintEvent() — needle drawing section
void NeedleItem::emitVertices(QVector<float>& verts, int widgetW, int widgetH)
{
    const QRect rect = pixelRect(widgetW, widgetH);
    const float pw = static_cast<float>(rect.width());
    const float ph = static_cast<float>(rect.height());
    const float cx = rect.left() + pw * 0.5f;
    const float radius = pw * kRadiusRatio;
    const float cy = rect.top() + ph + radius - ph * kCenterYRatio;

    // From AetherSDR: needleCy = height + 6.0f (pivot below widget bottom)
    const float pivotX = cx;
    const float pivotY = rect.top() + ph + 6.0f;

    const float frac = dbmToFraction(m_smoothedDbm);
    const float angleDeg = kArcEndDeg - frac * (kArcEndDeg - kArcStartDeg);
    const float angleRad = qDegreesToRadians(angleDeg);

    // From AetherSDR: needle tip at radius + 14
    const float tipDist = radius + 14.0f;
    const float tipX = cx + tipDist * std::cos(angleRad);
    const float tipY = cy - tipDist * std::sin(angleRad);

    // Perpendicular for needle width
    const float dx = tipX - pivotX;
    const float dy = tipY - pivotY;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1.0f) { return; }

    const float halfWidth = 1.5f;
    const float nx = -dy / len * halfWidth;
    const float ny =  dx / len * halfWidth;

    // Pixel to NDC conversion lambdas
    const float invW = 2.0f / static_cast<float>(widgetW);
    const float invH = 2.0f / static_cast<float>(widgetH);
    auto ndcX = [invW](float px) { return px * invW - 1.0f; };
    auto ndcY = [invH](float py) { return 1.0f - py * invH; };

    // --- Shadow quad (black, offset +1px) ---
    // From AetherSDR: shadow color rgba(0,0,0,80), pen width 3, offset +1
    const float sr = 0.0f, sg = 0.0f, sb = 0.0f, sa = 80.0f / 255.0f;
    const float sOff = 1.0f;
    const float sp1x = pivotX + nx + sOff, sp1y = pivotY + ny + sOff;
    const float sp2x = pivotX - nx + sOff, sp2y = pivotY - ny + sOff;
    const float sp3x = tipX + nx * 0.3f + sOff, sp3y = tipY + ny * 0.3f + sOff;
    const float sp4x = tipX - nx * 0.3f + sOff, sp4y = tipY - ny * 0.3f + sOff;
    // Triangle 1
    verts << ndcX(sp1x) << ndcY(sp1y) << sr << sg << sb << sa;
    verts << ndcX(sp2x) << ndcY(sp2y) << sr << sg << sb << sa;
    verts << ndcX(sp3x) << ndcY(sp3y) << sr << sg << sb << sa;
    // Triangle 2
    verts << ndcX(sp3x) << ndcY(sp3y) << sr << sg << sb << sa;
    verts << ndcX(sp2x) << ndcY(sp2y) << sr << sg << sb << sa;
    verts << ndcX(sp4x) << ndcY(sp4y) << sr << sg << sb << sa;

    // --- Needle quad (white) ---
    // From AetherSDR: needle color #ffffff, pen width 2
    const float nr = 1.0f, ng = 1.0f, nb = 1.0f, na = 1.0f;
    const float np1x = pivotX + nx, np1y = pivotY + ny;
    const float np2x = pivotX - nx, np2y = pivotY - ny;
    const float np3x = tipX + nx * 0.3f, np3y = tipY + ny * 0.3f;  // tapers
    const float np4x = tipX - nx * 0.3f, np4y = tipY - ny * 0.3f;
    // Triangle 1
    verts << ndcX(np1x) << ndcY(np1y) << nr << ng << nb << na;
    verts << ndcX(np2x) << ndcY(np2y) << nr << ng << nb << na;
    verts << ndcX(np3x) << ndcY(np3y) << nr << ng << nb << na;
    // Triangle 2
    verts << ndcX(np3x) << ndcY(np3y) << nr << ng << nb << na;
    verts << ndcX(np2x) << ndcY(np2y) << nr << ng << nb << na;
    verts << ndcX(np4x) << ndcY(np4y) << nr << ng << nb << na;

    // --- Peak marker triangle (amber) ---
    // From AetherSDR: peak marker #ffaa00, shown when peak > current + 1 dBm
    if (m_peakDbm > m_smoothedDbm + 1.0f) {
        const float peakFrac = dbmToFraction(m_peakDbm);
        const float peakAngleDeg = kArcEndDeg - peakFrac * (kArcEndDeg - kArcStartDeg);
        const float peakAngleRad = qDegreesToRadians(peakAngleDeg);

        const float mr = 1.0f, mg = 0.667f, mb = 0.0f, ma = 1.0f;  // #ffaa00
        const float markerInnerR = radius + 2.0f;
        const float markerOuterR = radius + 10.0f;
        const float markerHalf = 3.0f;

        // Tip (outward)
        const float mtx = cx + markerOuterR * std::cos(peakAngleRad);
        const float mty = cy - markerOuterR * std::sin(peakAngleRad);
        // Base perpendicular
        const float perpX = -std::sin(peakAngleRad) * markerHalf;
        const float perpY = -std::cos(peakAngleRad) * markerHalf;
        const float mbx = cx + markerInnerR * std::cos(peakAngleRad);
        const float mby = cy - markerInnerR * std::sin(peakAngleRad);

        verts << ndcX(mtx) << ndcY(mty) << mr << mg << mb << ma;
        verts << ndcX(mbx + perpX) << ndcY(mby + perpY) << mr << mg << mb << ma;
        verts << ndcX(mbx - perpX) << ndcY(mby - perpY) << mr << mg << mb << ma;
    }
}

// From AetherSDR SMeterWidget.cpp paintEvent() — tick drawing section
void NeedleItem::paintOverlayStatic(QPainter& p, int widgetW, int widgetH)
{
    const QRect rect = pixelRect(widgetW, widgetH);
    const float pw = static_cast<float>(rect.width());
    const float ph = static_cast<float>(rect.height());
    const float cx = rect.left() + pw * 0.5f;
    const float radius = pw * kRadiusRatio;
    const float cy = rect.top() + ph + radius - ph * kCenterYRatio;

    // Tick definitions from AetherSDR SMeterWidget.cpp
    // Only odd S-units + over-S9 marks shown
    struct TickDef { float dbm; const char* label; bool isRed; };
    static const TickDef ticks[] = {
        {-121.0f, "S1",  false},  // From AetherSDR: frac 0.0667
        {-109.0f, "S3",  false},  // frac 0.200
        { -97.0f, "S5",  false},  // frac 0.333
        { -85.0f, "S7",  false},  // frac 0.467
        { -73.0f, "S9",  false},  // frac 0.600
        { -53.0f, "+20", true},   // frac 0.733
        { -33.0f, "+40", true},   // frac 0.867
    };

    // From AetherSDR: tick font = max(10, height/10) px, bold
    QFont tickFont = p.font();
    const int tickFontSize = qMax(10, static_cast<int>(ph) / 10);
    tickFont.setPixelSize(tickFontSize);
    tickFont.setBold(true);
    p.setFont(tickFont);

    for (const auto& tick : ticks) {
        const float frac = dbmToFraction(tick.dbm);
        const float angleDeg = kArcEndDeg - frac * (kArcEndDeg - kArcStartDeg);
        const float angleRad = qDegreesToRadians(angleDeg);

        // From AetherSDR: tick line 14px, inset 2px from arc, width 1.5
        const float innerR = radius - 2.0f;
        const float outerR = radius + 12.0f;
        const float ix = cx + innerR * std::cos(angleRad);
        const float iy = cy - innerR * std::sin(angleRad);
        const float ox = cx + outerR * std::cos(angleRad);
        const float oy = cy - outerR * std::sin(angleRad);

        const QColor tickColor = tick.isRed ? QColor(0xff, 0x44, 0x44)
                                            : QColor(0xc8, 0xd8, 0xe8);
        QPen tickPen(tickColor);
        tickPen.setWidthF(1.5f);
        p.setPen(tickPen);
        p.drawLine(QPointF(ix, iy), QPointF(ox, oy));

        // From AetherSDR: label 26px from arc, centered on tick angle
        const float labelR = radius + 26.0f;
        const float lx = cx + labelR * std::cos(angleRad);
        const float ly = cy - labelR * std::sin(angleRad);

        p.setPen(tickColor);
        const QFontMetrics fm(tickFont);
        const QString labelStr = QString::fromLatin1(tick.label);
        const int tw = fm.horizontalAdvance(labelStr);
        p.drawText(QPointF(lx - tw / 2.0f, ly + fm.ascent() / 2.0f), labelStr);
    }

    // From AetherSDR: source label centered, max(9, h/14) px, non-bold, #8090a0
    QFont srcFont = p.font();
    const int srcFontSize = qMax(9, static_cast<int>(ph) / 14);
    srcFont.setPixelSize(srcFontSize);
    srcFont.setBold(false);
    p.setFont(srcFont);
    p.setPen(QColor(0x80, 0x90, 0xa0));
    const QFontMetrics srcFm(srcFont);
    const int srcW = srcFm.horizontalAdvance(m_sourceLabel);
    p.drawText(QPointF(rect.left() + (pw - srcW) / 2.0f,
                        rect.top() + srcFm.height() + 2.0f), m_sourceLabel);
}

// From AetherSDR SMeterWidget.cpp paintEvent() — readout drawing section
void NeedleItem::paintOverlayDynamic(QPainter& p, int widgetW, int widgetH)
{
    const QRect rect = pixelRect(widgetW, widgetH);
    const float ph = static_cast<float>(rect.height());

    // Compute source label baseline for vertical positioning
    const int srcFontSize = qMax(9, static_cast<int>(ph) / 14);
    QFont srcFont = p.font();
    srcFont.setPixelSize(srcFontSize);
    const QFontMetrics srcFm(srcFont);
    const float topY = rect.top() + srcFm.height() + 2.0f;

    // From AetherSDR: value font = max(13, h/8) px, bold
    QFont valFont = p.font();
    const int valFontSize = qMax(13, static_cast<int>(ph) / 8);
    valFont.setPixelSize(valFontSize);
    valFont.setBold(true);
    p.setFont(valFont);
    const QFontMetrics valFm(valFont);

    // S-unit text (left, cyan) — from AetherSDR: #00b4d8, x=6
    const QString sText = sUnitsText(m_smoothedDbm);
    p.setPen(QColor(0x00, 0xb4, 0xd8));
    p.drawText(QPointF(rect.left() + 6.0f, topY + valFm.ascent()), sText);

    // dBm text (right, light steel) — from AetherSDR: #c8d8e8, right-aligned -6
    const QString dbmText = QString::number(static_cast<int>(std::round(m_smoothedDbm)))
                          + QStringLiteral(" dBm");
    p.setPen(QColor(0xc8, 0xd8, 0xe8));
    const int dbmW = valFm.horizontalAdvance(dbmText);
    p.drawText(QPointF(rect.right() - dbmW - 6.0f, topY + valFm.ascent()), dbmText);
}

// Format: NEEDLE|x|y|w|h|bindingId|zOrder|sourceLabel
QString NeedleItem::serialize() const
{
    return QStringLiteral("NEEDLE|%1|%2")
        .arg(baseFields(*this))
        .arg(m_sourceLabel);
}

bool NeedleItem::deserialize(const QString& data)
{
    const QStringList parts = data.split(QLatin1Char('|'));
    if (parts.size() < 8 || parts[0] != QLatin1String("NEEDLE")) {
        return false;
    }
    if (!parseBaseFields(*this, parts)) {
        return false;
    }
    m_sourceLabel = parts[7];
    return true;
}

} // namespace NereusSDR
