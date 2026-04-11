#include "FadeCoverItem.h"

// From Thetis MeterManager.cs:7665 — clsFadeCover / renderFadeCover (line 36292)

#include <QPainter>
#include <QLinearGradient>
#include <QStringList>

namespace NereusSDR {

// From Thetis MeterManager.cs:7667 — clsFadeCover constructor
// ZOrder = int.MaxValue (always rendered on top of all other items)
FadeCoverItem::FadeCoverItem(QObject* parent)
    : MeterItem(parent)
{
    // From Thetis MeterManager.cs:7671 — ZOrder = int.MaxValue
    setZOrder(std::numeric_limits<int>::max());
}

// ---------------------------------------------------------------------------
// setTxState()
// From Thetis MeterManager.cs:7887-7888 — FadeOnRx/FadeOnTx guard logic.
// Thetis: items skip rendering when "if (FadeOnRx && !MOX) return;"
//         This means FadeOnRx items are visible during TX (MOX=true),
//         and FadeOnTx items are visible during RX (MOX=false).
// NereusSDR: m_active = true when the overlay should be painted.
//   - FadeOnRx: active when receiving (isTx == false)
//   - FadeOnTx: active when transmitting (isTx == true)
// ---------------------------------------------------------------------------
void FadeCoverItem::setTxState(bool isTx)
{
    m_active = (isTx && m_fadeOnTx) || (!isTx && m_fadeOnRx);
}

// ---------------------------------------------------------------------------
// paint()
// From Thetis MeterManager.cs:36292 — renderFadeCover()
// Thetis fills the rect with BackgroundColour at nFade alpha.
// NereusSDR: fills pixelRect with colour1 (or gradient to colour2) at m_alpha.
// ---------------------------------------------------------------------------
void FadeCoverItem::paint(QPainter& p, int widgetW, int widgetH)
{
    if (!m_active) {
        return;
    }

    const QRect rect = pixelRect(widgetW, widgetH);
    const int alphaInt = static_cast<int>(m_alpha * 255.0f);

    if (m_colour1 == m_colour2) {
        QColor fill = m_colour1;
        fill.setAlpha(alphaInt);
        p.fillRect(rect, fill);
    } else {
        QColor c1 = m_colour1;
        QColor c2 = m_colour2;
        c1.setAlpha(alphaInt);
        c2.setAlpha(alphaInt);
        QLinearGradient grad(rect.topLeft(), rect.bottomLeft());
        grad.setColorAt(0.0, c1);
        grad.setColorAt(1.0, c2);
        p.fillRect(rect, grad);
    }
}

// ---------------------------------------------------------------------------
// serialize()
// Format: FADECOVER|x|y|w|h|bindingId|zOrder|colour1|colour2|alpha|flags
//   flags = (fadeOnRx ? 2 : 0) | (fadeOnTx ? 1 : 0)
// ---------------------------------------------------------------------------

static QString fadeCoverBaseFields(const MeterItem& item)
{
    return QStringLiteral("%1|%2|%3|%4|%5|%6")
        .arg(static_cast<double>(item.x()))
        .arg(static_cast<double>(item.y()))
        .arg(static_cast<double>(item.itemWidth()))
        .arg(static_cast<double>(item.itemHeight()))
        .arg(item.bindingId())
        .arg(item.zOrder());
}

static bool fadeCoverParseBaseFields(MeterItem& item, const QStringList& parts)
{
    if (parts.size() < 7) {
        return false;
    }
    const QString base = QStringList(parts.mid(1, 6)).join(QLatin1Char('|'));
    return item.MeterItem::deserialize(base);
}

QString FadeCoverItem::serialize() const
{
    const int flags = (m_fadeOnRx ? 2 : 0) | (m_fadeOnTx ? 1 : 0);
    return QStringLiteral("FADECOVER|%1|%2|%3|%4|%5")
        .arg(fadeCoverBaseFields(*this))
        .arg(m_colour1.name(QColor::HexArgb))
        .arg(m_colour2.name(QColor::HexArgb))
        .arg(static_cast<double>(m_alpha))
        .arg(flags);
}

// ---------------------------------------------------------------------------
// deserialize()
// Expected: FADECOVER|x|y|w|h|bindingId|zOrder|colour1|colour2|alpha|flags
//           [0]       [1-6]             [7]     [8]     [9]    [10]
// ---------------------------------------------------------------------------
bool FadeCoverItem::deserialize(const QString& data)
{
    const QStringList parts = data.split(QLatin1Char('|'));
    if (parts.size() < 11 || parts[0] != QLatin1String("FADECOVER")) {
        return false;
    }
    if (!fadeCoverParseBaseFields(*this, parts)) {
        return false;
    }

    const QColor colour1(parts[7]);
    if (!colour1.isValid()) {
        return false;
    }

    const QColor colour2(parts[8]);
    if (!colour2.isValid()) {
        return false;
    }

    bool ok = true;
    const float alpha = parts[9].toFloat(&ok);
    if (!ok) {
        return false;
    }

    const int flags = parts[10].toInt(&ok);
    if (!ok) {
        return false;
    }

    m_colour1   = colour1;
    m_colour2   = colour2;
    m_alpha     = alpha;
    m_fadeOnRx  = (flags & 2) != 0;
    m_fadeOnTx  = (flags & 1) != 0;
    return true;
}

} // namespace NereusSDR
