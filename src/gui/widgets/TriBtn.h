#pragma once

#include <QPushButton>
#include <QPainter>
#include <QPaintEvent>

namespace NereusSDR {

// TriBtn — ported from AetherSDR src/gui/VfoWidget.cpp:97-129
// Fixed 22×22 QPushButton that paints a solid directional triangle (#c8d8e8)
// over the button area. Used for RIT/XIT zero buttons and FM offset step
// controls. Dir::Left points the triangle left; Dir::Right points it right.
class TriBtn : public QPushButton {
public:
    enum Dir { Left, Right };

    explicit TriBtn(Dir dir, QWidget* parent = nullptr)
        : QPushButton(parent), m_dir(dir)
    {
        setFlat(false);
        setFixedSize(22, 22);
        setStyleSheet(
            "QPushButton { background: #1a2a3a; border: 1px solid #203040; "
            "border-radius: 3px; padding: 0; margin: 0; min-width: 0; min-height: 0; }"
            "QPushButton:hover { background: #203040; }"
            "QPushButton:pressed { background: #00b4d8; }");
    }

protected:
    void paintEvent(QPaintEvent* ev) override {
        QPushButton::paintEvent(ev);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xc8, 0xd8, 0xe8));
        const double cx = width() / 2.0;
        const double cy = height() / 2.0;
        if (m_dir == Left) {
            const QPointF tri[] = {{cx + 3.0, cy - 4.0}, {cx + 3.0, cy + 4.0}, {cx - 3.0, cy}};
            p.drawPolygon(tri, 3);
        } else {
            const QPointF tri[] = {{cx - 3.0, cy - 4.0}, {cx - 3.0, cy + 4.0}, {cx + 3.0, cy}};
            p.drawPolygon(tri, 3);
        }
    }

private:
    Dir m_dir;
};

} // namespace NereusSDR
