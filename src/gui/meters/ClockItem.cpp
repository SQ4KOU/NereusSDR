#include "ClockItem.h"
#include <QPainter>
#include <QDateTime>

namespace NereusSDR {

ClockItem::ClockItem(QObject* parent) : MeterItem(parent)
{
    m_updateTimer.setInterval(1000);
    m_updateTimer.start();
}

void ClockItem::paint(QPainter& p, int widgetW, int widgetH)
{
    const QRect rect = pixelRect(widgetW, widgetH);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int halfW = rect.width() * 48 / 100;
    const int gap = rect.width() * 4 / 100;
    const QRect localRect(rect.left(), rect.top(), halfW, rect.height());
    const QRect utcRect(rect.left() + halfW + gap, rect.top(), halfW, rect.height());

    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime utcNow = now.toUTC();

    const QString timeFmt = m_show24Hour ? QStringLiteral("HH:mm:ss") : QStringLiteral("hh:mm:ss AP");
    const QString dateFmt = QStringLiteral("ddd d MMM yyyy");

    const int timeSize = qMax(10, rect.height() / 3);
    const int dateSize = qMax(8, rect.height() / 5);
    const int typeSize = qMax(7, rect.height() / 6);

    auto drawClock = [&](const QRect& r, const QDateTime& dt, const QString& typeLabel) {
        int yOff = r.top();
        if (m_showType) {
            QFont typeFont = p.font();
            typeFont.setPixelSize(typeSize);
            p.setFont(typeFont);
            p.setPen(m_typeTitleColour);
            p.drawText(QRect(r.left(), yOff, r.width(), typeSize + 2), Qt::AlignCenter, typeLabel);
            yOff += typeSize + 2;
        }
        QFont timeFont = p.font();
        timeFont.setPixelSize(timeSize);
        timeFont.setBold(true);
        p.setFont(timeFont);
        p.setPen(m_timeColour);
        p.drawText(QRect(r.left(), yOff, r.width(), timeSize + 2), Qt::AlignCenter, dt.toString(timeFmt));
        yOff += timeSize + 2;
        QFont dateFont = p.font();
        dateFont.setPixelSize(dateSize);
        dateFont.setBold(false);
        p.setFont(dateFont);
        p.setPen(m_dateColour);
        p.drawText(QRect(r.left(), yOff, r.width(), dateSize + 2), Qt::AlignCenter, dt.toString(dateFmt));
    };

    drawClock(localRect, now, QStringLiteral("Local"));
    drawClock(utcRect, utcNow, QStringLiteral("UTC"));
}

QString ClockItem::serialize() const
{
    return QStringLiteral("CLOCK|%1|%2|%3|%4|%5|%6|%7|%8")
        .arg(m_x).arg(m_y).arg(m_w).arg(m_h)
        .arg(m_bindingId).arg(m_zOrder)
        .arg(m_show24Hour ? 1 : 0).arg(m_showType ? 1 : 0);
}

bool ClockItem::deserialize(const QString& data)
{
    const QStringList parts = data.split(QLatin1Char('|'));
    if (parts.size() < 7 || parts[0] != QLatin1String("CLOCK")) { return false; }
    m_x = parts[1].toFloat(); m_y = parts[2].toFloat();
    m_w = parts[3].toFloat(); m_h = parts[4].toFloat();
    m_bindingId = parts[5].toInt(); m_zOrder = parts[6].toInt();
    if (parts.size() > 7) { m_show24Hour = parts[7].toInt() != 0; }
    if (parts.size() > 8) { m_showType = parts[8].toInt() != 0; }
    return true;
}

} // namespace NereusSDR
