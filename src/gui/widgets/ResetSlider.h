#pragma once

#include "GuardedSlider.h"
#include <QMouseEvent>

namespace NereusSDR {

// ResetSlider — ported from AetherSDR src/gui/VfoWidget.cpp:68-76
// GuardedSlider that snaps back to a configurable reset value on double-click.
// Used for AF gain, pan, APF tune, and other sliders that have a meaningful
// "default" position the operator may want to restore quickly.
class ResetSlider : public GuardedSlider {
public:
    explicit ResetSlider(int resetVal, Qt::Orientation o, QWidget* parent = nullptr)
        : GuardedSlider(o, parent), m_resetVal(resetVal) {}

protected:
    void mouseDoubleClickEvent(QMouseEvent*) override { setValue(m_resetVal); }

private:
    int m_resetVal;
};

} // namespace NereusSDR
