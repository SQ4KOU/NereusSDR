// src/gui/widgets/BadgePair.h
#pragma once

#include <QWidget>

class QGridLayout;

namespace NereusSDR {

class StatusBadge;

// BadgePair — container that holds two StatusBadges and toggles between
// a side-by-side horizontal arrangement (default, wide-window state) and
// a stacked vertical arrangement (medium-window state).
//
// RxDashboard composes 3 BadgePairs + 1 lone SQL badge and uses
// setStacked(true) on every pair when its 3-stage drop ladder enters the
// "medium" rung. This trades horizontal width for vertical height (the
// status bar already gives 46 px of height — we may as well use it).
//
// Either or both children can be hidden (StatusBadge::setVisible(false))
// to model active-only badges. The pair widget itself does not emit any
// signals; the contained StatusBadges keep their own click signals.
class BadgePair : public QWidget {
    Q_OBJECT

public:
    // The pair takes ownership of both badges via Qt parent-child.
    BadgePair(StatusBadge* a, StatusBadge* b, QWidget* parent = nullptr);

    // Switch between horizontal (false, default) and vertical (true)
    // arrangement. No-op when already in the requested state.
    void setStacked(bool stacked);
    bool isStacked() const noexcept { return m_stacked; }

    StatusBadge* first()  const noexcept { return m_a; }
    StatusBadge* second() const noexcept { return m_b; }

private:
    void layoutChildren();

    StatusBadge* m_a{nullptr};
    StatusBadge* m_b{nullptr};
    QGridLayout* m_grid{nullptr};
    bool         m_stacked{false};
};

} // namespace NereusSDR
