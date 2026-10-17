// no-port-check: NereusSDR-original Shift hold and Escape handling for arranging the workspace.
// Modification history (NereusSDR):
//   2026-10-11 - Holding Shift alone arranges while it is held, and Escape
//                 leaves the switched-on Arrange, by J.J. Boyd (KG4VCF),
//                 AI-assisted via Anthropic Claude Code.
//   2026-10-11 - The hold does not begin while a text field has keyboard
//                 focus. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//                 Claude Code.
//   2026-10-11 - A pointer move without Shift ends a hold whose key
//                 release was not seen. J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
#include "ArrangeHoldFilter.h"

#include "ContainerManager.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QTextEdit>

namespace NereusSDR {

namespace {

constexpr Qt::KeyboardModifiers kOtherModifiers =
    Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;

// True when the widget with keyboard focus takes typed text.
bool textFieldHasFocus()
{
    const QWidget* focus = QApplication::focusWidget();
    if (focus == nullptr) {
        return false;
    }
    if (qobject_cast<const QLineEdit*>(focus) != nullptr || qobject_cast<const QTextEdit*>(focus) != nullptr
        || qobject_cast<const QPlainTextEdit*>(focus) != nullptr
        || qobject_cast<const QAbstractSpinBox*>(focus) != nullptr) {
        return true;
    }
    const auto* combo = qobject_cast<const QComboBox*>(focus);
    return combo != nullptr && combo->isEditable();
}

} // namespace

ArrangeHoldFilter::ArrangeHoldFilter(ContainerManager* manager, QObject* parent)
    : QObject(parent)
    , m_manager(manager)
{
    m_delay.setSingleShot(true);
    m_delay.setInterval(kHoldDelayMs);
    connect(&m_delay, &QTimer::timeout, this, &ArrangeHoldFilter::beginHold);
    if (m_manager) {
        // Queued: the container is still finishing its drag when it reports.
        connect(m_manager, &ContainerManager::containerDragFinished, this,
                &ArrangeHoldFilter::endHoldAfterDrag, Qt::QueuedConnection);
    }
    qApp->installEventFilter(this);
}

void ArrangeHoldFilter::setHoldDelayForTest(int ms)
{
    m_delay.setInterval(ms);
}

bool ArrangeHoldFilter::eventFilter(QObject* watched, QEvent* event)
{
    Q_UNUSED(watched);
    // One event is seen here more than once (its window, its widget, each
    // parent it is passed up to), so every step below is safe to repeat.
    switch (event->type()) {
    case QEvent::KeyPress: {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Shift) {
            if (!key->isAutoRepeat()) {
                shiftPressed(key->modifiers());
            }
            return false;
        }
        if (key->key() == Qt::Key_Escape && escapeLeavesArrange()) {
            m_manager->setArrangeMode(false);
            return true;
        }
        otherInput(key->modifiers());
        return false;
    }
    case QEvent::KeyRelease: {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Shift && !key->isAutoRepeat()) {
            shiftReleased();
        }
        return false;
    }
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick: {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        m_buttonsDown = mouse->buttons();
        otherInput(mouse->modifiers());
        return false;
    }
    case QEvent::MouseMove: {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        m_buttonsDown = mouse->buttons();
        if (m_shiftDown && !mouse->modifiers().testFlag(Qt::ShiftModifier)) {
            // Shift was let go where this filter could not see it.
            shiftReleased();
        }
        return false;
    }
    case QEvent::MouseButtonRelease:
        m_buttonsDown = static_cast<QMouseEvent*>(event)->buttons();
        if (m_endAfterDrag && m_buttonsDown == Qt::NoButton) {
            // After the widget under the pointer has had its release.
            QMetaObject::invokeMethod(this, &ArrangeHoldFilter::endHoldAfterDrag, Qt::QueuedConnection);
        }
        return false;
    case QEvent::Wheel:
        otherInput(static_cast<QWheelEvent*>(event)->modifiers());
        return false;
    case QEvent::ApplicationDeactivate:
        // Shift's release goes to whatever the operator switched to.
        m_shiftDown = false;
        m_buttonsDown = Qt::NoButton;
        m_delay.stop();
        endHold();
        return false;
    default:
        return false;
    }
}

void ArrangeHoldFilter::shiftPressed(Qt::KeyboardModifiers modifiers)
{
    if (m_shiftDown) {
        return;
    }
    m_shiftDown = true;
    if (m_holdActive) {
        // Pressed again during the drag the hold was waiting on: it goes on.
        m_endAfterDrag = false;
        return;
    }
    const Qt::KeyboardModifiers held = modifiers | QGuiApplication::keyboardModifiers();
    const bool buttonDown = m_buttonsDown != Qt::NoButton || QGuiApplication::mouseButtons() != Qt::NoButton;
    if (!m_manager || (held & kOtherModifiers) != Qt::NoModifier || buttonDown) {
        return;
    }
    // Shift in a text field is for typing: the hold does not begin there.
    if (textFieldHasFocus()) {
        return;
    }
    m_delay.start();
}

void ArrangeHoldFilter::shiftReleased()
{
    if (!m_shiftDown) {
        return;
    }
    m_shiftDown = false;
    m_delay.stop();
    if (!m_holdActive) {
        return;
    }
    if (m_buttonsDown != Qt::NoButton || (m_manager && m_manager->containerDragActive())) {
        m_endAfterDrag = true;
        return;
    }
    endHold();
}

void ArrangeHoldFilter::otherInput(Qt::KeyboardModifiers modifiers)
{
    if (!m_shiftDown) {
        return;
    }
    if (!modifiers.testFlag(Qt::ShiftModifier)) {
        // Shift was let go where this filter could not see it.
        shiftReleased();
        return;
    }
    // Shift with something else is not a hold; an arranging hold goes on.
    m_delay.stop();
}

void ArrangeHoldFilter::beginHold()
{
    if (!m_shiftDown || !m_manager) {
        return;
    }
    m_holdActive = true;
    m_endAfterDrag = false;
    m_manager->setArrangeHeld(true);
}

void ArrangeHoldFilter::endHold()
{
    m_endAfterDrag = false;
    if (!m_holdActive) {
        return;
    }
    m_holdActive = false;
    if (m_manager) {
        m_manager->setArrangeHeld(false);
    }
}

void ArrangeHoldFilter::endHoldAfterDrag()
{
    if (!m_endAfterDrag || m_buttonsDown != Qt::NoButton || (m_manager && m_manager->containerDragActive())) {
        return;
    }
    endHold();
}

bool ArrangeHoldFilter::escapeLeavesArrange() const
{
    if (!m_manager || !m_manager->arrangeMode()) {
        return false;
    }
    // A drag takes Escape to cancel itself.
    if (m_buttonsDown != Qt::NoButton || m_manager->containerDragActive()) {
        return false;
    }
    if (QApplication::activePopupWidget() != nullptr || QApplication::activeModalWidget() != nullptr) {
        return false;
    }
    if (qobject_cast<const QDialog*>(QApplication::activeWindow()) != nullptr) {
        return false;
    }
    return !textFieldHasFocus();
}

} // namespace NereusSDR
