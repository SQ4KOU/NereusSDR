#pragma once

#include "ResetSlider.h"
#include <QPainter>
#include <QPaintEvent>

namespace NereusSDR {

// CenterMarkSlider — ported from AetherSDR src/gui/VfoWidget.cpp:79-94
// ResetSlider that paints a small antialiased circle at the slider groove
// centre, providing a visual reference for the midpoint (e.g. pan = 0,
// APF offset = 0). Colour #608090, radius 2.5px.
class CenterMarkSlider : public ResetSlider {
public:
    explicit CenterMarkSlider(int resetVal, Qt::Orientation o, QWidget* parent = nullptr)
        : ResetSlider(resetVal, o, parent) {}

protected:
    void paintEvent(QPaintEvent* ev) override {
        ResetSlider::paintEvent(ev);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        int cx = width() / 2;
        int cy = height() / 2;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#608090"));
        p.drawEllipse(QPointF(cx, cy), 2.5, 2.5);
    }
};

} // namespace NereusSDR
