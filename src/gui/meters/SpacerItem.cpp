#include "SpacerItem.h"

// From Thetis MeterManager.cs:16116 — clsSpacerItem / renderSpacer (line 35010)

#include <QPainter>
#include <QLinearGradient>
#include <QStringList>

namespace NereusSDR {

// From Thetis MeterManager.cs:16121
SpacerItem::SpacerItem(QObject* parent)
    : MeterItem(parent)
{
}

// ---------------------------------------------------------------------------
// paint()
// From Thetis MeterManager.cs:35010 — renderSpacer()
// Thetis renders colour1 (RX) or colour2 (TX) depending on MOX state,
// with a half-second cross-fade when MOX toggles.
// NereusSDR simplifies: if colours differ, show vertical linear gradient
// (colour1 top, colour2 bottom); otherwise solid fill with colour1.
// The padding field is preserved for serialization parity with Thetis
// (Thetis uses it for spacing in automatic layout, not for rendering).
// ---------------------------------------------------------------------------
void SpacerItem::paint(QPainter& p, int widgetW, int widgetH)
{
    const QRect rect = pixelRect(widgetW, widgetH);
    if (m_colour1 == m_colour2) {
        p.fillRect(rect, m_colour1);
    } else {
        QLinearGradient grad(rect.topLeft(), rect.bottomLeft());
        grad.setColorAt(0.0, m_colour1);
        grad.setColorAt(1.0, m_colour2);
        p.fillRect(rect, grad);
    }
}

// ---------------------------------------------------------------------------
// serialize()
// Format: SPACER|x|y|w|h|bindingId|zOrder|padding|colour1|colour2
// ---------------------------------------------------------------------------

// Local helper mirroring the static baseFields() in MeterItem.cpp
static QString spacerBaseFields(const MeterItem& item)
{
    return QStringLiteral("%1|%2|%3|%4|%5|%6")
        .arg(static_cast<double>(item.x()))
        .arg(static_cast<double>(item.y()))
        .arg(static_cast<double>(item.itemWidth()))
        .arg(static_cast<double>(item.itemHeight()))
        .arg(item.bindingId())
        .arg(item.zOrder());
}

// Local helper mirroring the static parseBaseFields() in MeterItem.cpp
static bool spacerParseBaseFields(MeterItem& item, const QStringList& parts)
{
    if (parts.size() < 7) {
        return false;
    }
    const QString base = QStringList(parts.mid(1, 6)).join(QLatin1Char('|'));
    return item.MeterItem::deserialize(base);
}

QString SpacerItem::serialize() const
{
    return QStringLiteral("SPACER|%1|%2|%3|%4")
        .arg(spacerBaseFields(*this))
        .arg(static_cast<double>(m_padding))
        .arg(m_colour1.name(QColor::HexArgb))
        .arg(m_colour2.name(QColor::HexArgb));
}

// ---------------------------------------------------------------------------
// deserialize()
// Expected: SPACER|x|y|w|h|bindingId|zOrder|padding|colour1|colour2
//           [0]    [1-6]             [7]     [8]     [9]
// ---------------------------------------------------------------------------
bool SpacerItem::deserialize(const QString& data)
{
    const QStringList parts = data.split(QLatin1Char('|'));
    if (parts.size() < 10 || parts[0] != QLatin1String("SPACER")) {
        return false;
    }
    if (!spacerParseBaseFields(*this, parts)) {
        return false;
    }

    bool ok = true;
    const float padding = parts[7].toFloat(&ok);
    if (!ok) {
        return false;
    }

    const QColor colour1(parts[8]);
    if (!colour1.isValid()) {
        return false;
    }

    const QColor colour2(parts[9]);
    if (!colour2.isValid()) {
        return false;
    }

    m_padding = padding;
    m_colour1 = colour1;
    m_colour2 = colour2;
    return true;
}

} // namespace NereusSDR
