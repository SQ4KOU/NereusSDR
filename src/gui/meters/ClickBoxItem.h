#pragma once
#include "MeterItem.h"
class QMouseEvent;
class QWheelEvent;

namespace NereusSDR {
// Invisible hit target for mouse interaction overlay.
// From Thetis clsClickBox (MeterManager.cs:7571+).
class ClickBoxItem : public MeterItem {
    Q_OBJECT
public:
    explicit ClickBoxItem(QObject* parent = nullptr) : MeterItem(parent) {}
    Layer renderLayer() const override { return Layer::OverlayDynamic; }
    void paint(QPainter& p, int widgetW, int widgetH) override;
    bool handleMousePress(QMouseEvent* event, int widgetW, int widgetH) override;
    bool handleWheel(QWheelEvent* event, int widgetW, int widgetH) override;
    QString serialize() const override;
    bool deserialize(const QString& data) override;
signals:
    void clicked();
    void wheelIncrement();
    void wheelDecrement();
};
} // namespace NereusSDR
