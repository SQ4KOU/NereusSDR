// =================================================================
// tests/tst_flyout_owns_the_mouse.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test infrastructure.
//
// An open overlay flyout owns the mouse over its whole rectangle.
//
// Bench-reported 2026-10-11, macOS and Windows: with the Display pill open,
// a click and drag inside it still dragged the spectrum under it, and the
// first click tuned the receiver to that spot behind the pill.
//
// The flyouts (the Display pill, BAND, ANT, VAX, ATT) are plain children of
// the SpectrumWidget. Qt hands a mouse event a widget ignores to its parent,
// so a press on the flyout's background, a gap, a label or a disabled
// control went on to the spectrum, which started a pan drag and tuned on
// release. The outside click that closes a flyout went through to the
// spectrum as well.
//
// Now each flyout root stops the hand-on (WA_NoMousePropagation), and the
// press that closes a flyout by landing on the same pan's spectrum is taken
// with the moves, release and context menu that belong to it.
//
// Events are sent to the top-level window, so Qt's own hit-testing and
// hand-on route them. What a native view tree does with a press is
// tst_overlay_flyout_above_flags_native's part.
//
// 2026-10-11 - Created. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//               Claude Code.
// =================================================================
#include <QtTest/QtTest>

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QHash>
#include <QLabel>
#include <QList>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QStyleHints>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWindow>

#include "core/AppSettings.h"
#include "core/ConnectionState.h"
#include "core/ControlRanges.h"
#include "gui/PanadapterApplet.h"
#include "gui/SpectrumOverlayPanel.h"
#include "gui/SpectrumWidget.h"

using namespace NereusSDR;

namespace {

constexpr double kCenterHz = 14'200'000.0;
constexpr double kSpanHz   =    192'000.0;

// Counts the mouse events that reach a widget's own handlers. It also takes
// the context menu event, so a pan that is wrongly given one opens no menu
// (a menu's exec() would stop the test).
class MouseLog : public QObject {
public:
    explicit MouseLog(QWidget* watched) : QObject(watched) { watched->installEventFilter(this); }

    int count(QEvent::Type type) const { return m_counts.value(int(type)); }
    // Everything but hover moves: Qt sends a widget with WA_Hover its own
    // hover event beside each mouse move, and Linux's offscreen plugin can
    // also synthesize a buttonless MouseMove when hiding a popup. Neither
    // can continue a press or pan the spectrum.
    int total() const
    {
        int sum = 0;
        for (auto it = m_counts.cbegin(); it != m_counts.cend(); ++it) {
            if (it.key() != int(QEvent::HoverMove)) {
                sum += it.value();
            }
        }
        return sum;
    }
    void clear() { m_counts.clear(); }
    // "type:count" for each kind seen, for a failure message.
    QByteArray seen() const
    {
        QByteArray text;
        for (auto it = m_counts.cbegin(); it != m_counts.cend(); ++it) {
            text += QByteArray::number(it.key()) + ':' + QByteArray::number(it.value()) + ' ';
        }
        return text;
    }

protected:
    bool eventFilter(QObject*, QEvent* event) override
    {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
            ++m_counts[int(event->type())];
            return false;
        case QEvent::MouseMove:
            if (static_cast<QMouseEvent*>(event)->buttons() == Qt::NoButton) {
                ++m_counts[int(QEvent::HoverMove)];
                return false;
            }
            ++m_counts[int(event->type())];
            return false;
        case QEvent::HoverMove:
        case QEvent::Wheel:
            ++m_counts[int(event->type())];
            return false;
        case QEvent::ContextMenu:
            ++m_counts[int(event->type())];
            event->accept();
            return true;
        default:
            return false;
        }
    }

private:
    QHash<int, int> m_counts;
};

struct Pan {
    PanadapterApplet*     applet{nullptr};
    SpectrumWidget*       spectrum{nullptr};
    SpectrumOverlayPanel* strip{nullptr};
    MouseLog*             log{nullptr};
};

// Two pans stacked in one window, each with its own strip, as the main
// window builds them.
struct Harness {
    QWidget window;
    Pan a;
    Pan b;

    Harness()
    {
        window.resize(1200, 1300);
        auto* column = new QVBoxLayout(&window);
        column->setContentsMargins(0, 0, 0, 0);
        a = addPan(QStringLiteral("pan-0"), column);
        b = addPan(QStringLiteral("pan-1"), column);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QCoreApplication::processEvents();
    }

    Pan addPan(const QString& id, QVBoxLayout* column)
    {
        Pan pan;
        pan.applet = new PanadapterApplet(id, &window);
        column->addWidget(pan.applet);
        pan.spectrum = pan.applet->spectrumWidget();
        pan.spectrum->setUpdatesEnabled(false);
        pan.spectrum->setSampleRate(kSpanHz);
        pan.spectrum->setDdcCenterFrequency(kCenterHz);
        pan.spectrum->setFrequencyRange(kCenterHz, kSpanHz);
        // A disconnected pan answers every click by asking for the
        // connection panel; these cases are about a pan in use.
        pan.spectrum->setConnectionState(ConnectionState::Connected);
        pan.strip = new SpectrumOverlayPanel(pan.spectrum);
        pan.strip->move(4, 4);
        pan.strip->show();
        pan.log = new MouseLog(pan.spectrum);
        return pan;
    }

    QPushButton* stripButton(const Pan& pan, const QString& text)
    {
        const QList<QPushButton*> buttons =
            pan.strip->findChildren<QPushButton*>(QString(), Qt::FindDirectChildrenOnly);
        for (QPushButton* button : buttons) {
            if (button->text() == text) {
                return button;
            }
        }
        return nullptr;
    }

    // Opens the named flyout on a pan and returns its root.
    QWidget* open(const Pan& pan, const QString& button)
    {
        QList<QWidget*> before;
        const QList<QWidget*> children =
            pan.spectrum->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
        for (QWidget* child : children) {
            if (!child->isHidden()) {
                before.append(child);
            }
        }
        QPushButton* opener = stripButton(pan, button);
        if (!opener) {
            return nullptr;
        }
        opener->click();
        QCoreApplication::processEvents();
        for (QWidget* child : children) {
            if (!child->isHidden() && !before.contains(child)) {
                return child;
            }
        }
        return nullptr;
    }

    QPoint inWindow(const QWidget* widget, const QPoint& point) const
    {
        return widget->mapTo(&window, point);
    }

    // A point in the pan's spectrum well clear of the strip and any flyout.
    QPoint clearPoint(const Pan& pan) const
    {
        return inWindow(pan.spectrum, QPoint(pan.spectrum->width() - 260, 200));
    }
};

// The part of a flyout a case presses on.
enum class Spot { Background, Label, DisabledControl };

// A point, in `flyout`, of the asked kind; `target` is the widget there.
QPoint spotIn(QWidget* flyout, Spot spot, QWidget** target)
{
    *target = nullptr;
    if (spot == Spot::Background) {
        // The root's own margin, where no child lies.
        for (const QPoint candidate : {QPoint(1, 1), QPoint(flyout->width() - 2, 1),
                                       QPoint(1, flyout->height() - 2)}) {
            if (flyout->childAt(candidate) == nullptr) {
                *target = flyout;
                return candidate;
            }
        }
        return QPoint();
    }
    if (spot == Spot::Label) {
        const QList<QLabel*> labels = flyout->findChildren<QLabel*>();
        for (QLabel* label : labels) {
            const QPoint center = label->mapTo(flyout, label->rect().center());
            if (label->isVisibleTo(flyout) && flyout->childAt(center) == label) {
                *target = label;
                return center;
            }
        }
        return QPoint();
    }
    // A control that cannot act. The flyout may show none in this harness
    // (no radio is connected), so one of its controls is disabled for the
    // case: what is tested is where a press on a disabled control goes.
    const QList<QWidget*> widgets = flyout->findChildren<QWidget*>();
    for (QWidget* widget : widgets) {
        if (!qobject_cast<QAbstractButton*>(widget) && !qobject_cast<QComboBox*>(widget)) {
            continue;
        }
        const QPoint center = widget->mapTo(flyout, widget->rect().center());
        if (widget->isVisibleTo(flyout) && flyout->childAt(center) == widget) {
            widget->setEnabled(false);
            *target = widget;
            return center;
        }
    }
    return QPoint();
}

void flyoutRows()
{
    QTest::addColumn<QString>("button");
    QTest::addColumn<int>("spot");
    for (const char* button : {"Display", "ANT"}) {
        const QString name = QString::fromLatin1(button);
        QTest::newRow(qPrintable(name + QStringLiteral(", background")))
            << name << int(Spot::Background);
        QTest::newRow(qPrintable(name + QStringLiteral(", a label")))
            << name << int(Spot::Label);
        QTest::newRow(qPrintable(name + QStringLiteral(", a disabled control")))
            << name << int(Spot::DisabledControl);
    }
}

} // namespace

class TstFlyoutOwnsTheMouse : public QObject {
    Q_OBJECT

private:
    // Everything a press inside a flyout must leave alone.
    struct Watch {
        QSignalSpy tuned;
        QSignalSpy panned;
        QSignalSpy activated;
        double     centerBefore;
        explicit Watch(const Pan& pan)
            : tuned(pan.spectrum, &SpectrumWidget::frequencyClicked)
            , panned(pan.spectrum, &SpectrumWidget::centerChanged)
            , activated(pan.applet, &PanadapterApplet::activated)
            , centerBefore(pan.spectrum->centerFrequency())
        {
        }
    };

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void init() { AppSettings::instance().clear(); }

    // The control case: with no flyout in the way a click tunes and a drag
    // pans, so the cases below would notice either.
    void aClickOnTheBareSpectrumTunesAndADragPans()
    {
        Harness h;
        Watch watch(h.a);
        const QPoint point = h.clearPoint(h.a);
        QTest::mouseClick(h.window.windowHandle(), Qt::LeftButton, Qt::NoModifier, point);
        QCoreApplication::processEvents();
        QCOMPARE(watch.tuned.count(), 1);
        // The applet says so once for the press at the spectrum and once
        // more as the spectrum hands the press on to it.
        QVERIFY(watch.activated.count() >= 1);

        QTest::mousePress(h.window.windowHandle(), Qt::LeftButton, Qt::NoModifier, point);
        QTest::mouseMove(h.window.windowHandle(), point + QPoint(80, 0));
        QTest::mouseRelease(h.window.windowHandle(), Qt::LeftButton, Qt::NoModifier,
                            point + QPoint(80, 0));
        QCoreApplication::processEvents();
        QVERIFY(watch.panned.count() >= 1);
    }

    void aLeftPressDragAndReleaseInsideStaysInTheFlyout_data() { flyoutRows(); }
    void aLeftPressDragAndReleaseInsideStaysInTheFlyout()
    {
        QFETCH(QString, button);
        QFETCH(int, spot);
        Harness h;
        QWidget* flyout = h.open(h.a, button);
        QVERIFY2(flyout != nullptr, "the button opened no flyout");
        QWidget* target = nullptr;
        const QPoint at = spotIn(flyout, Spot(spot), &target);
        QVERIFY2(target != nullptr, "the flyout has no such spot");
        const QPoint point = h.inWindow(flyout, at);
        h.a.log->clear();
        Watch watch(h.a);

        // A click.
        QTest::mouseClick(h.window.windowHandle(), Qt::LeftButton, Qt::NoModifier, point);
        // A drag that stays inside.
        QTest::mousePress(h.window.windowHandle(), Qt::LeftButton, Qt::NoModifier, point);
        QTest::mouseMove(h.window.windowHandle(), point + QPoint(12, 6));
        QTest::mouseRelease(h.window.windowHandle(), Qt::LeftButton, Qt::NoModifier,
                            point + QPoint(12, 6));
        // A drag from inside to well outside: it stays with the flyout.
        const QPoint outside = h.clearPoint(h.a);
        QTest::mousePress(h.window.windowHandle(), Qt::LeftButton, Qt::NoModifier, point);
        QTest::mouseMove(h.window.windowHandle(), (point + outside) / 2);
        QTest::mouseMove(h.window.windowHandle(), outside);
        QTest::mouseRelease(h.window.windowHandle(), Qt::LeftButton, Qt::NoModifier, outside);
        QCoreApplication::processEvents();

        QCOMPARE(h.a.log->count(QEvent::MouseButtonPress), 0);
        QCOMPARE(h.a.log->count(QEvent::MouseMove), 0);
        QCOMPARE(h.a.log->count(QEvent::MouseButtonRelease), 0);
        QCOMPARE(watch.tuned.count(), 0);
        QCOMPARE(watch.panned.count(), 0);
        QCOMPARE(watch.activated.count(), 0);
        QCOMPARE(h.a.spectrum->centerFrequency(), watch.centerBefore);
        QVERIFY2(!flyout->isHidden(), "a press inside the flyout closed it");
    }

    void otherPressesInsideStayInTheFlyout_data() { flyoutRows(); }
    void otherPressesInsideStayInTheFlyout()
    {
        QFETCH(QString, button);
        QFETCH(int, spot);
        Harness h;
        QWidget* flyout = h.open(h.a, button);
        QVERIFY2(flyout != nullptr, "the button opened no flyout");
        QWidget* target = nullptr;
        const QPoint at = spotIn(flyout, Spot(spot), &target);
        QVERIFY2(target != nullptr, "the flyout has no such spot");
        const QPoint point = h.inWindow(flyout, at);
        const QPoint inTarget = target->mapFrom(flyout, at);
        const QPoint global = flyout->mapToGlobal(at);
        h.a.log->clear();
        Watch watch(h.a);

        // Right and middle presses, which no control in a flyout uses.
        QTest::mouseClick(h.window.windowHandle(), Qt::RightButton, Qt::NoModifier, point);
        QTest::mouseClick(h.window.windowHandle(), Qt::MiddleButton, Qt::NoModifier, point);
        // A double-click.
        QTest::mouseDClick(h.window.windowHandle(), Qt::LeftButton, Qt::NoModifier, point);
        // A move with no button down (hover).
        QTest::mouseMove(h.window.windowHandle(), point + QPoint(2, 1));
        QTest::mouseMove(h.window.windowHandle(), point);
        // The wheel, to the widget under the pointer.
        QWheelEvent wheel(QPointF(inTarget), QPointF(global), QPoint(), QPoint(0, 120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(target, &wheel);
        // The context menu, by mouse and by keyboard.
        QContextMenuEvent byMouse(QContextMenuEvent::Mouse, inTarget, global);
        QApplication::sendEvent(target, &byMouse);
        QContextMenuEvent byKey(QContextMenuEvent::Keyboard, inTarget, global);
        QApplication::sendEvent(target, &byKey);
        QCoreApplication::processEvents();

        QCOMPARE(h.a.log->count(QEvent::MouseButtonPress), 0);
        QCOMPARE(h.a.log->count(QEvent::MouseButtonRelease), 0);
        QCOMPARE(h.a.log->count(QEvent::MouseButtonDblClick), 0);
        QCOMPARE(h.a.log->count(QEvent::MouseMove), 0);
        QCOMPARE(h.a.log->count(QEvent::HoverMove), 0);
        QCOMPARE(h.a.log->count(QEvent::Wheel), 0);
        QCOMPARE(h.a.log->count(QEvent::ContextMenu), 0);
        QCOMPARE(watch.tuned.count(), 0);
        QCOMPARE(watch.panned.count(), 0);
        QCOMPARE(watch.activated.count(), 0);
        QCOMPARE(h.a.spectrum->centerFrequency(), watch.centerBefore);
        QVERIFY2(!flyout->isHidden(), "a press inside the flyout closed it");
    }

    void everyFlyoutStopsThePressAtItsRoot_data()
    {
        QTest::addColumn<QString>("button");
        for (const char* button : {"BAND", "ANT", "Display", "VAX", "ATT"}) {
            QTest::newRow(button) << QString::fromLatin1(button);
        }
    }
    void everyFlyoutStopsThePressAtItsRoot()
    {
        QFETCH(QString, button);
        Harness h;
        QWidget* flyout = h.open(h.a, button);
        QVERIFY2(flyout != nullptr, "the button opened no flyout");
        QWidget* target = nullptr;
        const QPoint at = spotIn(flyout, Spot::Background, &target);
        QVERIFY2(target != nullptr, "the flyout has no bare margin");
        h.a.log->clear();
        Watch watch(h.a);

        const QPoint point = h.inWindow(flyout, at);
        QTest::mousePress(h.window.windowHandle(), Qt::LeftButton, Qt::NoModifier, point);
        QTest::mouseMove(h.window.windowHandle(), point + QPoint(90, 0));
        QTest::mouseRelease(h.window.windowHandle(), Qt::LeftButton, Qt::NoModifier,
                            point + QPoint(90, 0));
        QWheelEvent wheel(QPointF(at), QPointF(flyout->mapToGlobal(at)), QPoint(),
                          QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                          false);
        QApplication::sendEvent(flyout, &wheel);
        QCoreApplication::processEvents();

        QVERIFY2(h.a.log->total() == 0, h.a.log->seen().constData());
        QCOMPARE(watch.tuned.count(), 0);
        QCOMPARE(watch.panned.count(), 0);
        QCOMPARE(watch.activated.count(), 0);
        QVERIFY(!flyout->isHidden());
    }

    // The flyout's own controls still get their clicks.
    void aControlInsideStillWorks()
    {
        Harness h;
        QWidget* pill = h.open(h.a, QStringLiteral("Display"));
        QVERIFY(pill != nullptr);
        QAbstractButton* tab = nullptr;
        const QList<QAbstractButton*> buttons = pill->findChildren<QAbstractButton*>();
        for (QAbstractButton* button : buttons) {
            if (button->objectName().startsWith(QStringLiteral("displayTab"))
                && !button->isChecked()) {
                tab = button;
            }
        }
        QVERIFY(tab != nullptr);
        QTest::mouseClick(h.window.windowHandle(), Qt::LeftButton, Qt::NoModifier,
                          h.inWindow(tab, tab->rect().center()));
        QCoreApplication::processEvents();
        QVERIFY(tab->isChecked());
        QVERIFY(!pill->isHidden());
        QVERIFY2(h.a.log->total() == 0, h.a.log->seen().constData());
    }

    // FFT sizing mode (design D38): the three Sizing buttons get their
    // clicks, and a press and drag on the sliders the mode grays (FFT Size
    // and Hz/bin, both grayed in Auto) stays in the pill like any other.
    void theSizingButtonsGetTheirClicksAndTheGrayedSlidersPassNothingOn()
    {
        using Mode = ControlRanges::DisplayFftSizingMode;
        Harness h;
        QWidget* pill = h.open(h.a, QStringLiteral("Display"));
        QVERIFY(pill != nullptr);
        h.a.strip->setDisplayPillTab(SpectrumOverlayPanel::DisplayPillTab::Fft);
        QCoreApplication::processEvents();
        QSignalSpy asked(h.a.strip, &SpectrumOverlayPanel::fftSizingModeRequested);
        h.a.log->clear();
        Watch watch(h.a);

        const QList<QPair<const char*, Mode>> sizing{
            {"pillDisplayFftSizingModeHzPerBin", Mode::HzPerBin},
            {"pillDisplayFftSizingModeFixed", Mode::Fixed},
            {"pillDisplayFftSizingModeAuto", Mode::Auto}};
        for (const auto& choice : sizing) {
            auto* button = pill->findChild<QPushButton*>(QString::fromLatin1(choice.first));
            QVERIFY2(button != nullptr, choice.first);
            QVERIFY2(button->isVisibleTo(pill) && button->isEnabled(), choice.first);
            const QPoint center = button->mapTo(pill, button->rect().center());
            QCOMPARE(pill->childAt(center), button);
            const int before = asked.count();
            QTest::mouseClick(h.window.windowHandle(), Qt::LeftButton, Qt::NoModifier,
                              h.inWindow(button, button->rect().center()));
            QCoreApplication::processEvents();
            QCOMPARE(asked.count(), before + 1);
            QCOMPARE(asked.constLast().at(0).value<Mode>(), choice.second);
            QVERIFY2(button->isChecked(), choice.first);
        }

        // No window took the requests, so the station is still in Auto and
        // both sliders are grayed.
        h.a.strip->setFftState(h.a.strip->fftState());
        for (const char* name : {"pillDisplayFftSize", "pillDisplayHzPerBinTarget"}) {
            auto* slider = pill->findChild<QWidget*>(QString::fromLatin1(name));
            QVERIFY2(slider != nullptr, name);
            QVERIFY2(slider->isVisibleTo(pill) && !slider->isEnabled(), name);
            const QPoint point = h.inWindow(slider, slider->rect().center());
            QTest::mouseClick(h.window.windowHandle(), Qt::LeftButton, Qt::NoModifier, point);
            QTest::mousePress(h.window.windowHandle(), Qt::LeftButton, Qt::NoModifier, point);
            QTest::mouseMove(h.window.windowHandle(), point + QPoint(30, 0));
            QTest::mouseRelease(h.window.windowHandle(), Qt::LeftButton, Qt::NoModifier,
                                point + QPoint(30, 0));
            QCoreApplication::processEvents();
        }

        QCOMPARE(asked.count(), 3);
        QVERIFY2(h.a.log->total() == 0, h.a.log->seen().constData());
        QCOMPARE(watch.tuned.count(), 0);
        QCOMPARE(watch.panned.count(), 0);
        QCOMPARE(watch.activated.count(), 0);
        QCOMPARE(h.a.spectrum->centerFrequency(), watch.centerBefore);
        QVERIFY(!pill->isHidden());
    }

    void anOutsideClickOnTheSamePanOnlyClosesTheFlyout_data()
    {
        QTest::addColumn<QString>("button");
        QTest::newRow("Display") << QStringLiteral("Display");
        QTest::newRow("ANT") << QStringLiteral("ANT");
    }
    void anOutsideClickOnTheSamePanOnlyClosesTheFlyout()
    {
        QFETCH(QString, button);
        Harness h;
        QWidget* flyout = h.open(h.a, button);
        QVERIFY2(flyout != nullptr, "the button opened no flyout");
        h.a.log->clear();
        Watch watch(h.a);
        const QPoint point = h.clearPoint(h.a);

        // Press, a drag far enough to pan, release.
        QTest::mousePress(h.window.windowHandle(), Qt::LeftButton, Qt::NoModifier, point);
        QVERIFY2(flyout->isHidden(), "the outside press did not close the flyout");
        QTest::mouseMove(h.window.windowHandle(), point + QPoint(70, 0));
        QTest::mouseRelease(h.window.windowHandle(), Qt::LeftButton, Qt::NoModifier,
                            point + QPoint(70, 0));
        QCoreApplication::processEvents();
        QVERIFY2(h.a.log->total() == 0, h.a.log->seen().constData());
        QCOMPARE(watch.tuned.count(), 0);
        QCOMPARE(watch.panned.count(), 0);
        QCOMPARE(watch.activated.count(), 0);
        QCOMPARE(h.a.spectrum->centerFrequency(), watch.centerBefore);

        // The next click is the pan's again.
        QTest::mouseClick(h.window.windowHandle(), Qt::LeftButton, Qt::NoModifier, point);
        QCoreApplication::processEvents();
        QCOMPARE(h.a.log->count(QEvent::MouseButtonPress), 1);
        QCOMPARE(h.a.log->count(QEvent::MouseButtonRelease), 1);
        QCOMPARE(watch.tuned.count(), 1);
        QVERIFY(watch.activated.count() >= 1);
    }

    // A right press outside closes the flyout and opens no pan menu, whether
    // the platform sends the context menu with the press (macOS, Linux) or
    // after the release (Windows). Qt makes the context menu event itself
    // from the right click; the style hint says when.
    void anOutsideRightClickOnTheSamePanOpensNoMenu_data()
    {
        QTest::addColumn<bool>("menuAfterRelease");
        QTest::newRow("menu with the press") << false;
        QTest::newRow("menu after the release") << true;
    }
    void anOutsideRightClickOnTheSamePanOpensNoMenu()
    {
        QFETCH(bool, menuAfterRelease);
        QStyleHints* hints = QGuiApplication::styleHints();
        const Qt::ContextMenuTrigger before = hints->contextMenuTrigger();
        hints->setContextMenuTrigger(menuAfterRelease ? Qt::ContextMenuTrigger::Release
                                                      : Qt::ContextMenuTrigger::Press);
        const auto restore = qScopeGuard([&]() { hints->setContextMenuTrigger(before); });

        Harness h;
        const QPoint point = h.clearPoint(h.a);
        QWidget* flyout = h.open(h.a, QStringLiteral("Display"));
        QVERIFY(flyout != nullptr);
        h.a.log->clear();
        Watch watch(h.a);

        QTest::mouseClick(h.window.windowHandle(), Qt::RightButton, Qt::NoModifier, point);
        QCoreApplication::processEvents();
        QVERIFY(flyout->isHidden());
        QVERIFY2(h.a.log->total() == 0, h.a.log->seen().constData());
        QCOMPARE(watch.activated.count(), 0);
        QVERIFY2(QApplication::activePopupWidget() == nullptr,
                 "the click that closed the flyout opened the pan's menu");

        // The next right click is the pan's again: it opens the pan's menu.
        QTest::mouseClick(h.window.windowHandle(), Qt::RightButton, Qt::NoModifier, point);
        QCoreApplication::processEvents();
        QCOMPARE(h.a.log->count(QEvent::MouseButtonPress), 1);
        QWidget* popup = QApplication::activePopupWidget();
        QVERIFY2(popup != nullptr, "the next right click did not open the pan's menu");
        popup->close();
    }

    void anOutsideClickOnAnotherPanClosesTheFlyoutAndIsThatPans()
    {
        Harness h;
        QWidget* flyout = h.open(h.a, QStringLiteral("Display"));
        QVERIFY(flyout != nullptr);
        h.a.log->clear();
        h.b.log->clear();
        Watch onA(h.a);
        Watch onB(h.b);

        QTest::mouseClick(h.window.windowHandle(), Qt::LeftButton, Qt::NoModifier,
                          h.clearPoint(h.b));
        QCoreApplication::processEvents();
        QVERIFY2(flyout->isHidden(), "a click on another pan left the flyout open");
        QVERIFY2(h.a.log->total() == 0, h.a.log->seen().constData());
        QCOMPARE(onA.tuned.count(), 0);
        QCOMPARE(onA.activated.count(), 0);
        QCOMPARE(h.b.log->count(QEvent::MouseButtonPress), 1);
        QCOMPARE(h.b.log->count(QEvent::MouseButtonRelease), 1);
        QCOMPARE(onB.tuned.count(), 1);
        QVERIFY(onB.activated.count() >= 1);
    }

    // The strip's own buttons toggle as before: the open flyout's button
    // closes it, another button swaps to its flyout.
    void theStripsButtonsStillToggle()
    {
        Harness h;
        QWidget* pill = h.open(h.a, QStringLiteral("Display"));
        QVERIFY(pill != nullptr);
        QPushButton* display = h.stripButton(h.a, QStringLiteral("Display"));
        QPushButton* band = h.stripButton(h.a, QStringLiteral("BAND"));
        QVERIFY(display && band);

        QTest::mouseClick(h.window.windowHandle(), Qt::LeftButton, Qt::NoModifier,
                          h.inWindow(band, band->rect().center()));
        QCoreApplication::processEvents();
        QVERIFY(pill->isHidden());
        QVERIFY2(!h.a.strip->displayPillVisible(), "the pill is still the open flyout");

        QTest::mouseClick(h.window.windowHandle(), Qt::LeftButton, Qt::NoModifier,
                          h.inWindow(display, display->rect().center()));
        QCoreApplication::processEvents();
        QVERIFY(h.a.strip->displayPillVisible());
        QTest::mouseClick(h.window.windowHandle(), Qt::LeftButton, Qt::NoModifier,
                          h.inWindow(display, display->rect().center()));
        QCoreApplication::processEvents();
        QVERIFY(!h.a.strip->displayPillVisible());
        QVERIFY2(h.a.log->total() == 0, h.a.log->seen().constData());
    }
};

QTEST_MAIN(TstFlyoutOwnsTheMouse)
#include "tst_flyout_owns_the_mouse.moc"
