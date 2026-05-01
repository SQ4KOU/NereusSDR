// src/gui/widgets/OverflowChip.h
#pragma once

#include <QStringList>
#include <QWidget>

class QLabel;

namespace NereusSDR {

// OverflowChip — small "…" pill that surfaces items the host has hidden
// for layout-fit reasons.
//
// Per shell-chrome design §293-294: when ≥ 1 right-strip item has been
// dropped by the priority-drop logic, this chip appears at the strip end
// and its tooltip lists what was dropped. Hovering tells the user where
// items went; resizing the window restores them and the chip vanishes.
//
// The chip itself is presentation-only — the host owns the drop logic
// and just calls setDroppedItems() on each pass.
class OverflowChip : public QWidget {
    Q_OBJECT

public:
    explicit OverflowChip(QWidget* parent = nullptr);

    // Replace the dropped-items list. Empty list hides the chip;
    // non-empty list shows it + builds a multi-line tooltip.
    void setDroppedItems(const QStringList& items);
    QStringList droppedItems() const noexcept { return m_items; }

    bool hasDroppedItems() const noexcept { return !m_items.isEmpty(); }

private:
    QStringList m_items;
    QLabel*     m_glyph{nullptr};
};

} // namespace NereusSDR
