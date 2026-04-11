#pragma once
#include "MeterItem.h"
namespace NereusSDR {
class NeedleScalePwrItem : public MeterItem {
    Q_OBJECT
public:
    explicit NeedleScalePwrItem(QObject* parent = nullptr) : MeterItem(parent) {}
    Layer renderLayer() const override { return Layer::OverlayStatic; }
    void paint(QPainter& p, int widgetW, int widgetH) override { Q_UNUSED(p); Q_UNUSED(widgetW); Q_UNUSED(widgetH); }
};
} // namespace NereusSDR
