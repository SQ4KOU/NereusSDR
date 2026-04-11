#include "OtherButtonItem.h"

namespace NereusSDR {

// From Thetis clsOtherButtons (MeterManager.cs:8225+)
static const char* const kCoreLabels[] = {
    "PWR", "RX2", "MON", "TUN", "MOX", "2TON", "DUP", "PS",
    "PLAY", "REC", "ANF", "SNB", "MNF", "AVG", "PEAK", "CTUN",
    "VAC1", "VAC2", "MUTE", "BIN", "SUB", "SWAP", "XPA",
    "SPEC", "PAN", "SCP", "SCP2", "PHS",
    "WF", "HIST", "PANF", "PANS", "SPCS", "OFF"
};

OtherButtonItem::OtherButtonItem(QObject* parent)
    : ButtonBoxItem(parent)
{
    const int total = kCoreButtonCount + kMacroCount;
    setButtonCount(total);
    setColumns(6);
    setCornerRadius(3.0f);

    m_buttonMap.resize(total);
    for (int i = 0; i < total; ++i) { m_buttonMap[i] = i; }

    for (int i = 0; i < kCoreButtonCount; ++i) {
        setupButton(i, QString::fromLatin1(kCoreLabels[i]));
        button(i).onColour = QColor(0x00, 0x60, 0x40);
    }

    m_macroSettings.resize(kMacroCount);
    for (int i = 0; i < kMacroCount; ++i) {
        const int idx = kCoreButtonCount + i;
        setupButton(idx, QStringLiteral("M%1").arg(i));
        button(idx).visible = false;
        button(idx).onColour = QColor(0x00, 0x70, 0xc0);
    }

    connect(this, &ButtonBoxItem::buttonClicked, this, &OtherButtonItem::onButtonClicked);
}

void OtherButtonItem::setButtonState(ButtonId id, bool on)
{
    const int idVal = static_cast<int>(id);
    for (int i = 0; i < m_buttonMap.size(); ++i) {
        if (m_buttonMap[i] == idVal && i < buttonCount()) {
            button(i).on = on;
            return;
        }
    }
}

OtherButtonItem::MacroSettings& OtherButtonItem::macroSettings(int macroIndex)
{
    return m_macroSettings[macroIndex];
}

void OtherButtonItem::onButtonClicked(int index, Qt::MouseButton btn)
{
    if (btn != Qt::LeftButton) { return; }
    const int mapped = (index < m_buttonMap.size()) ? m_buttonMap[index] : index;
    if (mapped >= kCoreButtonCount) {
        const int macroIdx = mapped - kCoreButtonCount;
        if (macroIdx >= 0 && macroIdx < kMacroCount) { emit macroTriggered(macroIdx); }
    } else {
        emit otherButtonClicked(mapped);
    }
}

QString OtherButtonItem::serialize() const
{
    return QStringLiteral("OTHERBTNS|%1|%2|%3|%4|%5|%6|%7|%8")
        .arg(m_x).arg(m_y).arg(m_w).arg(m_h)
        .arg(m_bindingId).arg(m_zOrder)
        .arg(columns()).arg(visibleBits());
}

bool OtherButtonItem::deserialize(const QString& data)
{
    const QStringList parts = data.split(QLatin1Char('|'));
    if (parts.size() < 7 || parts[0] != QLatin1String("OTHERBTNS")) { return false; }
    m_x = parts[1].toFloat(); m_y = parts[2].toFloat();
    m_w = parts[3].toFloat(); m_h = parts[4].toFloat();
    m_bindingId = parts[5].toInt(); m_zOrder = parts[6].toInt();
    if (parts.size() > 7) { setColumns(parts[7].toInt()); }
    if (parts.size() > 8) { setVisibleBits(parts[8].toUInt()); }
    return true;
}

} // namespace NereusSDR
