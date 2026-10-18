#pragma once
// no-port-check: NereusSDR-original Shift hold and Escape handling for arranging the workspace.
// Modification history (NereusSDR):
//   2026-10-11 - Holding Shift alone arranges while it is held, and Escape
//                 leaves the switched-on Arrange, by J.J. Boyd (KG4VCF),
//                 AI-assisted via Anthropic Claude Code.
#include <QObject>
#include <QPointer>
#include <QTimer>

namespace NereusSDR {

class ContainerManager;

// Workspace look only: an application event filter, one per main window.
//
// Shift pressed alone and held for kHoldDelayMs arranges until it is let
// go (ContainerManager::setArrangeHeld). A mouse press, a wheel turn or any
// other key before the delay is over cancels it for that press of Shift,
// so Shift with a click, a wheel or a letter never arranges. It does not
// begin while a text field has keyboard focus. Let go during a container
// drag, the hold ends when the drag does.
//
// Escape leaves the switched-on Arrange when nothing else has a claim on
// the key: no drag, no popup, no dialog, no text being edited.
//
// It only watches: no Shift key event is ever consumed. The one event it
// consumes is the Escape that leaves Arrange.
class ArrangeHoldFilter : public QObject {
    Q_OBJECT
public:
    static constexpr int kHoldDelayMs = 300;

    explicit ArrangeHoldFilter(ContainerManager* manager, QObject* parent);

    void setHoldDelayForTest(int ms);
    // True while a press of Shift is waiting out the delay.
    bool holdPendingForTest() const { return m_delay.isActive(); }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void shiftPressed(Qt::KeyboardModifiers modifiers);
    void shiftReleased();
    // A mouse press, a wheel turn or another key, with its modifiers.
    void otherInput(Qt::KeyboardModifiers modifiers);
    void beginHold();
    void endHold();
    // Ends a hold whose Shift was let go during a drag, once it is over.
    void endHoldAfterDrag();
    bool escapeLeavesArrange() const;

    QPointer<ContainerManager> m_manager;
    QTimer m_delay;
    Qt::MouseButtons m_buttonsDown{Qt::NoButton};
    bool m_shiftDown{false};
    bool m_holdActive{false};
    bool m_endAfterDrag{false};
};

} // namespace NereusSDR
