#pragma once

#include <QSlider>
#include <QMouseEvent>
#include <QWheelEvent>

namespace NereusSDR {

// ControlsLock — ported from AetherSDR src/gui/GuardedSlider.h:13-19
// Global singleton flag: when locked, all GuardedSlider (and derived) widgets
// ignore mouse and wheel input so that accidental adjustments are prevented
// while the operator has locked the controls panel.
class ControlsLock {
public:
    static bool isLocked() { return s_locked; }
    static void setLocked(bool locked) { s_locked = locked; }
private:
    static inline bool s_locked = false;
};

// GuardedSlider — ported from AetherSDR src/gui/GuardedSlider.h:22-47
// QSlider subclass that:
//   (a) ignores all mouse press and wheel events when ControlsLock::isLocked(),
//   (b) uses singleStep (default 1) for wheel adjustments rather than pageStep
//       (default 10) so that mouse-wheel tuning is fine-grained, and
//   (c) consumes wheel events at boundaries so scroll does not propagate to the
//       parent (e.g. SpectrumWidget tuning the VFO when the slider bottoms out).
class GuardedSlider : public QSlider {
public:
    using QSlider::QSlider;

    void mousePressEvent(QMouseEvent* ev) override {
        if (ControlsLock::isLocked()) {
            ev->ignore();
            return;
        }
        QSlider::mousePressEvent(ev);
    }

    void wheelEvent(QWheelEvent* ev) override {
        if (ControlsLock::isLocked()) {
            ev->ignore();
            return;
        }
        // Use singleStep (default 1) instead of pageStep (default 10) so
        // that mouse-wheel adjustments are fine-grained.
        int delta = ev->angleDelta().y();
        if (delta != 0) {
            setValue(value() + (delta > 0 ? singleStep() : -singleStep()));
        }
        ev->accept();
    }
};

} // namespace NereusSDR
