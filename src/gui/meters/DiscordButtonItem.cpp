#include "DiscordButtonItem.h"
namespace NereusSDR {
static const char* const kDiscordLabels[] = {
    "Active", "Away", "Double", "CQ", "QRV", "QRT",
    "QSY", "QRX", "DX", "Version", "Net Start", "Net Fin"
};

DiscordButtonItem::DiscordButtonItem(QObject* parent) : ButtonBoxItem(parent)
{
    setButtonCount(kDiscordButtonCount);
    setColumns(4);
    setCornerRadius(3.0f);
    for (int i = 0; i < kDiscordButtonCount; ++i) {
        setupButton(i, QString::fromLatin1(kDiscordLabels[i]));
        button(i).onColour = QColor(0x58, 0x65, 0xf2); // Discord blurple
    }
    connect(this, &ButtonBoxItem::buttonClicked, this, &DiscordButtonItem::onButtonClicked);
}

void DiscordButtonItem::onButtonClicked(int index, Qt::MouseButton btn)
{ if (btn == Qt::LeftButton) { emit discordAction(index); } }

QString DiscordButtonItem::serialize() const
{
    return QStringLiteral("DISCORDBTNS|%1|%2|%3|%4|%5|%6|%7")
        .arg(m_x).arg(m_y).arg(m_w).arg(m_h).arg(m_bindingId).arg(m_zOrder).arg(columns());
}

bool DiscordButtonItem::deserialize(const QString& data)
{
    const QStringList parts = data.split(QLatin1Char('|'));
    if (parts.size() < 7 || parts[0] != QLatin1String("DISCORDBTNS")) { return false; }
    m_x = parts[1].toFloat(); m_y = parts[2].toFloat();
    m_w = parts[3].toFloat(); m_h = parts[4].toFloat();
    m_bindingId = parts[5].toInt(); m_zOrder = parts[6].toInt();
    if (parts.size() > 7) { setColumns(parts[7].toInt()); }
    return true;
}
} // namespace NereusSDR
