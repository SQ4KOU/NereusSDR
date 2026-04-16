#include "VfoLevelBar.h"
#include "VfoStyles.h"
#include <QPainter>
#include <algorithm>
namespace NereusSDR {
// From AetherSDR src/gui/VfoWidget.cpp:38-64 — LevelBar port,
// extended with an S-unit tick strip above the bar (NereusSDR native).
VfoLevelBar::VfoLevelBar(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}
void VfoLevelBar::setValue(float dbm) {
    if (dbm == m_value) { return; }
    m_value = dbm;
    update();
}
double VfoLevelBar::fillFraction() const {
    const double v = std::clamp(static_cast<double>(m_value),
                                static_cast<double>(kFloorDbm),
                                static_cast<double>(kCeilingDbm));
    return (v - kFloorDbm) / (kCeilingDbm - kFloorDbm);
}
void VfoLevelBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    const int tickH = 8;
    const QRect barRect(0, tickH, width(), height() - tickH);

    // ── Tick strip above the bar ───────────────────────────────────────
    // Tick labels for S1, S3, S5, S7, S9, +20, +40
    static constexpr float kTickDbm[]    = {-121, -109, -97, -85, -73, -53, -33};
    static constexpr const char* kTickTxt[] = {"S1","3","5","7","9","+20","+40"};
    p.setPen(kLabelMuted);
    QFont f = p.font(); f.setPixelSize(8); p.setFont(f);
    for (int i = 0; i < 7; ++i) {
        double frac = (kTickDbm[i] - kFloorDbm) / (kCeilingDbm - kFloorDbm);
        int x = static_cast<int>(frac * (width() - 1));
        p.drawLine(x, tickH - 2, x, tickH);  // tick mark
        p.drawText(QRect(x - 10, 0, 20, tickH - 2),
                   Qt::AlignCenter, QString::fromLatin1(kTickTxt[i]));
    }

    // ── Bar itself (ported from AetherSDR LevelBar::paintEvent) ────────
    p.fillRect(barRect, QColor(0x10, 0x10, 0x1c));
    p.setPen(QColor(0x30, 0x40, 0x50));
    p.drawRect(barRect.adjusted(0, 0, -1, -1));
    const int fillW = static_cast<int>(fillFraction() * (barRect.width() - 2));
    if (fillW > 0) {
        const QColor color = isAboveS9() ? kMeterGreen : kMeterCyan;
        p.fillRect(barRect.x() + 1, barRect.y() + 1,
                   fillW, barRect.height() - 2, color);
    }
}
}
