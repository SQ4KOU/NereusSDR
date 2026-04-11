#include "VoiceRecordPlayItem.h"
namespace NereusSDR {
static const char* const kVoiceLabels[] = { "REC", "PLAY", "STOP", "NEXT", "PREV" };

VoiceRecordPlayItem::VoiceRecordPlayItem(QObject* parent) : ButtonBoxItem(parent)
{
    setButtonCount(kButtonCount);
    setColumns(5);
    setCornerRadius(3.0f);
    for (int i = 0; i < kButtonCount; ++i) { setupButton(i, QString::fromLatin1(kVoiceLabels[i])); }
    button(0).onColour = QColor(0xff, 0x44, 0x44); // REC = red
    button(1).onColour = QColor(0x00, 0x60, 0x40); // PLAY = green
    connect(this, &ButtonBoxItem::buttonClicked, this, &VoiceRecordPlayItem::onButtonClicked);
}

void VoiceRecordPlayItem::onButtonClicked(int index, Qt::MouseButton btn)
{ if (btn == Qt::LeftButton) { emit voiceAction(index); } }

QString VoiceRecordPlayItem::serialize() const
{
    return QStringLiteral("VOICERECPLAY|%1|%2|%3|%4|%5|%6|%7")
        .arg(m_x).arg(m_y).arg(m_w).arg(m_h).arg(m_bindingId).arg(m_zOrder).arg(columns());
}

bool VoiceRecordPlayItem::deserialize(const QString& data)
{
    const QStringList parts = data.split(QLatin1Char('|'));
    if (parts.size() < 7 || parts[0] != QLatin1String("VOICERECPLAY")) { return false; }
    m_x = parts[1].toFloat(); m_y = parts[2].toFloat();
    m_w = parts[3].toFloat(); m_h = parts[4].toFloat();
    m_bindingId = parts[5].toInt(); m_zOrder = parts[6].toInt();
    if (parts.size() > 7) { setColumns(parts[7].toInt()); }
    return true;
}
} // namespace NereusSDR
