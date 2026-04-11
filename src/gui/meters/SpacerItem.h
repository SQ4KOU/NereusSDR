#pragma once
#include "MeterItem.h"
namespace NereusSDR {
class SpacerItem : public MeterItem {
    Q_OBJECT
public:
    explicit SpacerItem(QObject* parent = nullptr) : MeterItem(parent) {}
    Layer renderLayer() const override { return Layer::Background; }
    void paint(QPainter& p, int widgetW, int widgetH) override { Q_UNUSED(p); Q_UNUSED(widgetW); Q_UNUSED(widgetH); }
};
} // namespace NereusSDR
