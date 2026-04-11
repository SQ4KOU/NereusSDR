#include "ModeButtonItem.h"

namespace NereusSDR {

// From Thetis clsModeButtonBox (MeterManager.cs:9951+)
static const char* const kModeLabels[] = {
    "LSB", "USB", "DSB", "CWL", "CWU", "FM", "AM", "SAM", "DIGL", "DIGU"
};

ModeButtonItem::ModeButtonItem(QObject* parent)
    : ButtonBoxItem(parent)
{
    setButtonCount(kModeCount);
    setColumns(5);
    setCornerRadius(3.0f);

    for (int i = 0; i < kModeCount; ++i) {
        setupButton(i, QString::fromLatin1(kModeLabels[i]));
        button(i).onColour = QColor(0x00, 0x70, 0xc0);
    }

    connect(this, &ButtonBoxItem::buttonClicked,
            this, &ModeButtonItem::onButtonClicked);
}

void ModeButtonItem::setActiveMode(int index)
{
    if (m_activeMode == index) { return; }
    if (m_activeMode >= 0 && m_activeMode < buttonCount()) {
        button(m_activeMode).on = false;
    }
    m_activeMode = index;
    if (m_activeMode >= 0 && m_activeMode < buttonCount()) {
        button(m_activeMode).on = true;
    }
}

void ModeButtonItem::onButtonClicked(int index, Qt::MouseButton btn)
{
    if (btn == Qt::LeftButton) {
        emit modeClicked(index);
    }
}

QString ModeButtonItem::serialize() const
{
    return QStringLiteral("MODEBTNS|%1|%2|%3|%4|%5|%6|%7|%8|%9")
        .arg(m_x).arg(m_y).arg(m_w).arg(m_h)
        .arg(m_bindingId).arg(m_zOrder)
        .arg(columns()).arg(m_activeMode).arg(visibleBits());
}

bool ModeButtonItem::deserialize(const QString& data)
{
    const QStringList parts = data.split(QLatin1Char('|'));
    if (parts.size() < 7 || parts[0] != QLatin1String("MODEBTNS")) { return false; }
    m_x = parts[1].toFloat();
    m_y = parts[2].toFloat();
    m_w = parts[3].toFloat();
    m_h = parts[4].toFloat();
    m_bindingId = parts[5].toInt();
    m_zOrder = parts[6].toInt();
    if (parts.size() > 7) { setColumns(parts[7].toInt()); }
    if (parts.size() > 8) { setActiveMode(parts[8].toInt()); }
    if (parts.size() > 9) { setVisibleBits(parts[9].toUInt()); }
    return true;
}

} // namespace NereusSDR
