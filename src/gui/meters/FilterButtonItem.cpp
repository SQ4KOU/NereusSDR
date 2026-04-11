#include "FilterButtonItem.h"

namespace NereusSDR {

// From Thetis clsFilterButtonBox (MeterManager.cs:7674+)
static const char* const kFilterLabels[] = {
    "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "Var1", "Var2"
};

FilterButtonItem::FilterButtonItem(QObject* parent)
    : ButtonBoxItem(parent)
{
    setButtonCount(kFilterCount);
    setColumns(4);
    setCornerRadius(3.0f);

    for (int i = 0; i < kFilterCount; ++i) {
        setupButton(i, QString::fromLatin1(kFilterLabels[i]));
        button(i).onColour = QColor(0x00, 0x70, 0xc0);
    }

    connect(this, &ButtonBoxItem::buttonClicked,
            this, &FilterButtonItem::onButtonClicked);
}

void FilterButtonItem::setActiveFilter(int index)
{
    if (m_activeFilter == index) { return; }
    if (m_activeFilter >= 0 && m_activeFilter < buttonCount()) {
        button(m_activeFilter).on = false;
    }
    m_activeFilter = index;
    if (m_activeFilter >= 0 && m_activeFilter < buttonCount()) {
        button(m_activeFilter).on = true;
    }
}

void FilterButtonItem::setFilterLabel(int index, const QString& label)
{
    if (index >= 0 && index < buttonCount()) {
        button(index).text = label;
    }
}

void FilterButtonItem::onButtonClicked(int index, Qt::MouseButton btn)
{
    if (btn == Qt::RightButton) {
        emit filterContextRequested(index);
    } else {
        emit filterClicked(index);
    }
}

QString FilterButtonItem::serialize() const
{
    return QStringLiteral("FILTERBTNS|%1|%2|%3|%4|%5|%6|%7|%8|%9")
        .arg(m_x).arg(m_y).arg(m_w).arg(m_h)
        .arg(m_bindingId).arg(m_zOrder)
        .arg(columns()).arg(m_activeFilter).arg(visibleBits());
}

bool FilterButtonItem::deserialize(const QString& data)
{
    const QStringList parts = data.split(QLatin1Char('|'));
    if (parts.size() < 7 || parts[0] != QLatin1String("FILTERBTNS")) { return false; }
    m_x = parts[1].toFloat();
    m_y = parts[2].toFloat();
    m_w = parts[3].toFloat();
    m_h = parts[4].toFloat();
    m_bindingId = parts[5].toInt();
    m_zOrder = parts[6].toInt();
    if (parts.size() > 7) { setColumns(parts[7].toInt()); }
    if (parts.size() > 8) { setActiveFilter(parts[8].toInt()); }
    if (parts.size() > 9) { setVisibleBits(parts[9].toUInt()); }
    return true;
}

} // namespace NereusSDR
