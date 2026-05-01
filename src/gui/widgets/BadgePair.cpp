// src/gui/widgets/BadgePair.cpp
#include "BadgePair.h"

#include "StatusBadge.h"

#include <QGridLayout>

namespace NereusSDR {

BadgePair::BadgePair(StatusBadge* a, StatusBadge* b, QWidget* parent)
    : QWidget(parent), m_a(a), m_b(b)
{
    m_grid = new QGridLayout(this);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setHorizontalSpacing(3);
    m_grid->setVerticalSpacing(2);

    if (m_a) { m_a->setParent(this); }
    if (m_b) { m_b->setParent(this); }

    layoutChildren();
}

void BadgePair::setStacked(bool stacked)
{
    if (m_stacked == stacked) { return; }
    m_stacked = stacked;
    layoutChildren();
}

void BadgePair::layoutChildren()
{
    if (m_a) { m_grid->removeWidget(m_a); }
    if (m_b) { m_grid->removeWidget(m_b); }

    if (m_stacked) {
        // Vertical: badge a stacks on top of badge b. Single column,
        // two rows. Empty rows collapse — Qt's grid skips them when
        // the child is hidden.
        if (m_a) { m_grid->addWidget(m_a, 0, 0); }
        if (m_b) { m_grid->addWidget(m_b, 1, 0); }
    } else {
        // Horizontal: badge a left, badge b right. Single row, two
        // columns.
        if (m_a) { m_grid->addWidget(m_a, 0, 0); }
        if (m_b) { m_grid->addWidget(m_b, 0, 1); }
    }
}

} // namespace NereusSDR
