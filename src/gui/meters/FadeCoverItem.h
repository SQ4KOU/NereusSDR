#pragma once

// From Thetis MeterManager.cs:7665 — clsFadeCover / renderFadeCover (line 36292)
// Semi-transparent overlay that activates on RX or TX state.

#include "MeterItem.h"
#include <QColor>

namespace NereusSDR {

class FadeCoverItem : public MeterItem {
    Q_OBJECT
public:
    explicit FadeCoverItem(QObject* parent = nullptr);

    // From Thetis MeterManager.cs:1900 — FadeOnRx / FadeOnTx
    void setFadeOnRx(bool on) { m_fadeOnRx = on; }
    bool fadeOnRx() const { return m_fadeOnRx; }

    void setFadeOnTx(bool on) { m_fadeOnTx = on; }
    bool fadeOnTx() const { return m_fadeOnTx; }

    // Overlay colours (gradient if they differ, solid if equal)
    void setColour1(const QColor& c) { m_colour1 = c; }
    QColor colour1() const { return m_colour1; }

    void setColour2(const QColor& c) { m_colour2 = c; }
    QColor colour2() const { return m_colour2; }

    // Alpha of the overlay when active (0-1). Default 0.75.
    void setAlpha(float a) { m_alpha = a; }
    float alpha() const { return m_alpha; }

    // From Thetis MeterManager.cs:7671 — ZOrder = int.MaxValue (always on top)
    // Call this when MOX state changes to update m_active.
    // m_active = (isTx && m_fadeOnTx) || (!isTx && m_fadeOnRx)
    void setTxState(bool isTx);

    Layer renderLayer() const override { return Layer::OverlayDynamic; }
    void paint(QPainter& p, int widgetW, int widgetH) override;
    QString serialize() const override;
    bool deserialize(const QString& data) override;

private:
    QColor m_colour1{0x20, 0x20, 0x20};
    QColor m_colour2{0x20, 0x20, 0x20};
    float  m_alpha{0.75f};
    bool   m_fadeOnRx{false};
    bool   m_fadeOnTx{false};
    bool   m_active{false};
};

} // namespace NereusSDR
