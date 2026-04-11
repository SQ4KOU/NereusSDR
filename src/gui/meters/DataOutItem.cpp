#include "DataOutItem.h"
#include <QPainter>

namespace NereusSDR {

void DataOutItem::paint(QPainter& p, int widgetW, int widgetH)
{
    Q_UNUSED(p); Q_UNUSED(widgetW); Q_UNUSED(widgetH);
}

QString DataOutItem::serialize() const
{
    return QStringLiteral("DATAOUT|%1|%2|%3|%4|%5|%6|%7|%8|%9|%10")
        .arg(m_x).arg(m_y).arg(m_w).arg(m_h)
        .arg(m_bindingId).arg(m_zOrder)
        .arg(m_mmioGuid).arg(m_mmioVariable)
        .arg(static_cast<int>(m_outputFormat)).arg(static_cast<int>(m_transportMode));
}

bool DataOutItem::deserialize(const QString& data)
{
    const QStringList parts = data.split(QLatin1Char('|'));
    if (parts.size() < 7 || parts[0] != QLatin1String("DATAOUT")) { return false; }
    m_x = parts[1].toFloat(); m_y = parts[2].toFloat();
    m_w = parts[3].toFloat(); m_h = parts[4].toFloat();
    m_bindingId = parts[5].toInt(); m_zOrder = parts[6].toInt();
    if (parts.size() > 7) { m_mmioGuid = parts[7]; }
    if (parts.size() > 8) { m_mmioVariable = parts[8]; }
    if (parts.size() > 9) { m_outputFormat = static_cast<OutputFormat>(parts[9].toInt()); }
    if (parts.size() > 10) { m_transportMode = static_cast<TransportMode>(parts[10].toInt()); }
    return true;
}

} // namespace NereusSDR
