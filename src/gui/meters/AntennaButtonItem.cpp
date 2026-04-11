#include "AntennaButtonItem.h"

namespace NereusSDR {

// From Thetis clsAntennaButtonBox (MeterManager.cs:9502+)
static const char* const kAntennaLabels[] = {
    "Rx1", "Rx2", "Rx3", "Aux1", "Aux2",
    "XVTR", "Tx1", "Tx2", "Tx3", "Rx/Tx"
};

AntennaButtonItem::AntennaButtonItem(QObject* parent)
    : ButtonBoxItem(parent)
{
    setButtonCount(kAntennaCount);
    setColumns(5);
    setCornerRadius(3.0f);
    setVisibleBits(1023);

    for (int i = 0; i < kAntennaCount; ++i) {
        setupButton(i, QString::fromLatin1(kAntennaLabels[i]));
    }
    // Rx: LimeGreen
    for (int i = 0; i < 3; ++i) { button(i).onColour = QColor(0x00, 0xff, 0x00); }
    // Aux + XVTR: Orange
    button(3).onColour = QColor(0xff, 0xa5, 0x00);
    button(4).onColour = QColor(0xff, 0xa5, 0x00);
    button(5).onColour = QColor(0xff, 0xa5, 0x00);
    // Tx: Red
    for (int i = 6; i < 9; ++i) { button(i).onColour = QColor(0xff, 0x44, 0x44); }
    // Toggle: Yellow
    button(9).onColour = QColor(0xff, 0xff, 0x00);

    connect(this, &ButtonBoxItem::buttonClicked, this, &AntennaButtonItem::onButtonClicked);
}

void AntennaButtonItem::setActiveRxAntenna(int index)
{
    if (m_activeRxAnt == index) { return; }
    if (m_activeRxAnt >= 0 && m_activeRxAnt < 3) { button(m_activeRxAnt).on = false; }
    m_activeRxAnt = index;
    if (m_activeRxAnt >= 0 && m_activeRxAnt < 3) { button(m_activeRxAnt).on = true; }
}

void AntennaButtonItem::setActiveTxAntenna(int index)
{
    if (m_activeTxAnt == index) { return; }
    if (m_activeTxAnt >= 0 && m_activeTxAnt < 3) { button(6 + m_activeTxAnt).on = false; }
    m_activeTxAnt = index;
    if (m_activeTxAnt >= 0 && m_activeTxAnt < 3) { button(6 + m_activeTxAnt).on = true; }
}

void AntennaButtonItem::onButtonClicked(int index, Qt::MouseButton btn)
{
    if (btn == Qt::LeftButton) { emit antennaSelected(index); }
}

QString AntennaButtonItem::serialize() const
{
    return QStringLiteral("ANTENNABTNS|%1|%2|%3|%4|%5|%6|%7|%8")
        .arg(m_x).arg(m_y).arg(m_w).arg(m_h)
        .arg(m_bindingId).arg(m_zOrder)
        .arg(columns()).arg(visibleBits());
}

bool AntennaButtonItem::deserialize(const QString& data)
{
    const QStringList parts = data.split(QLatin1Char('|'));
    if (parts.size() < 7 || parts[0] != QLatin1String("ANTENNABTNS")) { return false; }
    m_x = parts[1].toFloat(); m_y = parts[2].toFloat();
    m_w = parts[3].toFloat(); m_h = parts[4].toFloat();
    m_bindingId = parts[5].toInt(); m_zOrder = parts[6].toInt();
    if (parts.size() > 7) { setColumns(parts[7].toInt()); }
    if (parts.size() > 8) { setVisibleBits(parts[8].toUInt()); }
    return true;
}

} // namespace NereusSDR
