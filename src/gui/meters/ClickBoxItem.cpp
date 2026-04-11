#include "ClickBoxItem.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>

namespace NereusSDR {

void ClickBoxItem::paint(QPainter& p, int widgetW, int widgetH)
{
    Q_UNUSED(p); Q_UNUSED(widgetW); Q_UNUSED(widgetH);
}

bool ClickBoxItem::handleMousePress(QMouseEvent* event, int widgetW, int widgetH)
{
    Q_UNUSED(widgetW); Q_UNUSED(widgetH);
    if (event->button() == Qt::LeftButton) { emit clicked(); return true; }
    return false;
}

bool ClickBoxItem::handleWheel(QWheelEvent* event, int widgetW, int widgetH)
{
    Q_UNUSED(widgetW); Q_UNUSED(widgetH);
    if (event->angleDelta().y() > 0) { emit wheelIncrement(); }
    else { emit wheelDecrement(); }
    return true;
}

QString ClickBoxItem::serialize() const
{
    return QStringLiteral("CLICKBOX|%1|%2|%3|%4|%5|%6")
        .arg(m_x).arg(m_y).arg(m_w).arg(m_h).arg(m_bindingId).arg(m_zOrder);
}

bool ClickBoxItem::deserialize(const QString& data)
{
    const QStringList parts = data.split(QLatin1Char('|'));
    if (parts.size() < 7 || parts[0] != QLatin1String("CLICKBOX")) { return false; }
    m_x = parts[1].toFloat(); m_y = parts[2].toFloat();
    m_w = parts[3].toFloat(); m_h = parts[4].toFloat();
    m_bindingId = parts[5].toInt(); m_zOrder = parts[6].toInt();
    return true;
}

} // namespace NereusSDR
