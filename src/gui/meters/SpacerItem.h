#pragma once

// From Thetis MeterManager.cs:16116 — clsSpacerItem
// Solid or gradient background fill rectangle.

#include "MeterItem.h"
#include <QColor>

namespace NereusSDR {

class SpacerItem : public MeterItem {
    Q_OBJECT
public:
    explicit SpacerItem(QObject* parent = nullptr);

    // From Thetis MeterManager.cs:16143 — Colour1 / Colour2 / Padding
    void setColour1(const QColor& c) { m_colour1 = c; }
    QColor colour1() const { return m_colour1; }

    void setColour2(const QColor& c) { m_colour2 = c; }
    QColor colour2() const { return m_colour2; }

    void setPadding(float p) { m_padding = p; }
    float padding() const { return m_padding; }

    Layer renderLayer() const override { return Layer::Background; }
    void paint(QPainter& p, int widgetW, int widgetH) override;
    QString serialize() const override;
    bool deserialize(const QString& data) override;

private:
    // From Thetis MeterManager.cs:16123 — default Color.FromArgb(32, 32, 32)
    QColor m_colour1{0x20, 0x20, 0x20};
    QColor m_colour2{0x20, 0x20, 0x20};
    // From Thetis MeterManager.cs:16126 — default 0.1f
    float  m_padding{0.1f};
};

} // namespace NereusSDR
