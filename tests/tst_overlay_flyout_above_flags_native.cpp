// =================================================================
// tests/tst_overlay_flyout_above_flags_native.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test infrastructure.
//
// An open overlay flyout stays above the pan's flags, in the native view
// tree. The companion of tst_overlay_flyout_above_flags, which checks Qt's
// own stacking order and hit-testing without a native window.
//
// In the main window on macOS every widget has its own NSView, so which
// widget a click reaches is AppKit's decision, made from the order of the
// subviews, not Qt's. QWidget::raise() on such a widget moves its NSView
// too. This test shows the real pan, strip and flag on the Cocoa platform,
// gives the pan's children native views as they have in the window, runs a
// placement pass, and asks AppKit:
//   - the pill's NSView is later in its superview's subviews than the
//     flag's and each of the flag's buttons' (later is in front);
//   - -[NSView hitTest:] at a point on a pill control the flag overlaps
//     returns the pill's view, which is the view AppKit would send a mouse
//     press at that point to.
// It then sends a click to the Qt window of the view AppKit picked and
// checks the pill's control took it.
//
// What it does not do: post a real mouse press through the window server
// (that would move the operator's pointer and needs accessibility access),
// so the last step from AppKit's hit view to Qt's handler is exercised with
// a Qt-level click on that view's window, not with an NSEvent.
//
// Objective-C++, macOS only (tests/CMakeLists.txt registers it under
// if(APPLE)).
//
// 2026-10-11 - Created. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//               Claude Code.
// =================================================================
#include <QtTest/QtTest>

#include <QAbstractButton>
#include <QApplication>
#include <QGuiApplication>
#include <QLabel>
#include <QList>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QWindow>

#include "core/AppSettings.h"
#include "core/ConnectionState.h"
#include "gui/SpectrumOverlayPanel.h"
#include "gui/SpectrumWidget.h"
#include "gui/widgets/VfoWidget.h"

#import <Cocoa/Cocoa.h>

using namespace NereusSDR;

namespace {

constexpr double kCentreHz = 14'200'000.0;
constexpr double kSpanHz   =    192'000.0;
// As in tst_overlay_flyout_above_flags: the flag flips to the VFO's right
// and lands across the flyouts.
constexpr double kNearLeftHz = kCentreHz - kSpanHz / 2.0 + kSpanHz / 8.0;

using Tab = SpectrumOverlayPanel::DisplayPillTab;

NSView* viewOf(QWidget* widget)
{
    return (__bridge NSView*)reinterpret_cast<void*>(widget->winId());
}

// A subview's place among its superview's subviews; later is in front.
NSInteger subviewIndex(NSView* view)
{
    NSView* parent = view.superview;
    if (!parent) {
        return -1;
    }
    const NSUInteger index = [parent.subviews indexOfObjectIdenticalTo:view];
    return index == NSNotFound ? -1 : NSInteger(index);
}

// The view AppKit picks for a point given in `pan` coordinates (Qt's views
// are flipped, so the point is the same in the pan's view).
NSView* hitViewAt(QWidget* pan, const QPoint& point)
{
    NSView* panView = viewOf(pan);
    const NSPoint inPan = NSMakePoint(point.x(), point.y());
    // -hitTest: takes the point in the receiver's superview's coordinates.
    const NSPoint inSuper = [panView convertPoint:inPan toView:panView.superview];
    return [panView hitTest:inSuper];
}

QWindow* windowOfView(NSView* view)
{
    const QWindowList windows = QGuiApplication::allWindows();
    for (QWindow* window : windows) {
        if (window->handle() && reinterpret_cast<void*>(window->winId()) == (__bridge void*)view) {
            return window;
        }
    }
    return nullptr;
}

// Counts the presses, moves, releases, double-clicks, wheel and context
// menu events that reach the pan's own handlers.
class PanMouseLog : public QObject {
public:
    explicit PanMouseLog(QWidget* watched) : QObject(watched)
    {
        watched->installEventFilter(this);
    }
    int seen{0};

protected:
    bool eventFilter(QObject*, QEvent* event) override
    {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseMove:
        case QEvent::Wheel:
            ++seen;
            return false;
        case QEvent::ContextMenu:
            ++seen;
            event->accept();
            return true;
        default:
            return false;
        }
    }
};

} // namespace

class TstOverlayFlyoutAboveFlagsNative : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        if (QApplication::platformName() != QStringLiteral("cocoa")) {
            QSKIP("The native view tree is the Cocoa platform's.");
        }
    }

    void init() { AppSettings::instance().clear(); }

    void appKitKeepsTheOpenPillAboveTheFlagAndGivesItTheClick()
    {
        SpectrumWidget pan;
        // On the desktop without taking the operator's keyboard focus.
        pan.setAttribute(Qt::WA_ShowWithoutActivating, true);
        pan.resize(1200, 760);
        pan.setSampleRate(kSpanHz);
        pan.setDdcCenterFrequency(kCentreHz);
        pan.setFrequencyRange(kCentreHz, kSpanHz);
        auto* panel = new SpectrumOverlayPanel(&pan);
        panel->move(4, 4);
        panel->show();
        VfoWidget* flag = pan.addVfoWidget(0);
        flag->setFrequency(kNearLeftHz);
        pan.show();
        QTRY_VERIFY(pan.windowHandle() && pan.windowHandle()->isVisible());
        pan.updateVfoPositions();

        panel->openDisplayPill();
        panel->setDisplayPillTab(Tab::Spectrum);
        QWidget* pill = pan.findChild<QWidget*>(QStringLiteral("displayPill"));
        QVERIFY(pill && !pill->isHidden());
        QCoreApplication::processEvents();

        // As in the main window: each child of the pan is its own NSView.
        // A pan on its own has none until one is asked for, and asking one
        // child for its native handle gives its siblings theirs.
        pill->winId();
        QVERIFY(pill->internalWinId() != 0);
        QVERIFY2(flag->internalWinId() != 0, "the flag did not get a native view");
        QCoreApplication::processEvents();

        NSView* pillView = viewOf(pill);
        NSView* flagView = viewOf(flag);
        QVERIFY(pillView != nil && flagView != nil);
        QVERIFY(pillView != flagView);
        QVERIFY2(pillView.superview == flagView.superview,
                 "the pill and the flag are not sibling views");

        // An unselected tab button the flag overlaps.
        QAbstractButton* covered = nullptr;
        QPoint point;
        const QList<QAbstractButton*> buttons = pill->findChildren<QAbstractButton*>();
        for (QAbstractButton* button : buttons) {
            const QPoint centre = button->mapTo(&pan, button->rect().center());
            if (button->objectName().startsWith(QStringLiteral("displayTab"))
                && !button->isChecked() && flag->geometry().contains(centre)) {
                covered = button;
                point = centre;
            }
        }
        QVERIFY2(covered != nullptr, "no unselected tab button lies under the flag");

        // Twice: the pass after the first raise must not undo it.
        QSignalSpy raised(&pan, &SpectrumWidget::flagsRaised);
        for (int pass = 0; pass < 2; ++pass) {
            pan.updateVfoPositions();
            QCoreApplication::processEvents();

            QVERIFY2(subviewIndex(pillView) > subviewIndex(flagView),
                     "AppKit has the flag's view in front of the open pill's");
            const QList<QPushButton*> floating =
                pan.findChildren<QPushButton*>(QString(), Qt::FindDirectChildrenOnly);
            for (QPushButton* button : floating) {
                if (button->internalWinId() == 0 || button->isHidden()) {
                    continue;
                }
                QVERIFY2(subviewIndex(pillView) > subviewIndex(viewOf(button)),
                         "AppKit has one of the flag's buttons in front of the open pill");
            }

            NSView* hit = hitViewAt(&pan, point);
            QVERIFY2(hit != nil, "AppKit found no view at the point");
            QVERIFY2([hit isDescendantOf:pillView],
                     "AppKit sends a press at the pill's control to another view (the flag)");
            QVERIFY(![hit isDescendantOf:flagView]);
        }
        // The raises started no pass of their own.
        QCOMPARE(raised.count(), 2);

        // From the view AppKit picked to the pill's control.
        NSView* hit = hitViewAt(&pan, point);
        QWindow* hitWindow = windowOfView(hit);
        QVERIFY2(hitWindow != nullptr, "the view AppKit picked is not a Qt window's");
        const NSPoint inHit = [hit convertPoint:NSMakePoint(point.x(), point.y())
                                       fromView:viewOf(&pan)];
        QTest::mouseClick(hitWindow, Qt::LeftButton, Qt::NoModifier,
                          QPoint(int(inHit.x), int(inHit.y)));
        QCoreApplication::processEvents();
        QVERIFY2(panel->displayPillVisible(), "the click closed the pill as an outside click");
        QVERIFY2(covered->isChecked(), "the click did not reach the pill's tab button");
    }

    // 2026-10-11: a press inside the open pill went on to the spectrum
    // under it. Here the press is sent to the Qt window of the view AppKit
    // picks at a label in the pill, with the pan's children as real NSViews.
    void aPressOnALabelInThePillNeverReachesThePansView()
    {
        SpectrumWidget pan;
        pan.setAttribute(Qt::WA_ShowWithoutActivating, true);
        pan.resize(1200, 760);
        pan.setSampleRate(kSpanHz);
        pan.setDdcCenterFrequency(kCentreHz);
        pan.setFrequencyRange(kCentreHz, kSpanHz);
        pan.setConnectionState(ConnectionState::Connected);
        auto* panel = new SpectrumOverlayPanel(&pan);
        panel->move(4, 4);
        panel->show();
        pan.show();
        QTRY_VERIFY(pan.windowHandle() && pan.windowHandle()->isVisible());

        panel->openDisplayPill();
        panel->setDisplayPillTab(Tab::Spectrum);
        QWidget* pill = pan.findChild<QWidget*>(QStringLiteral("displayPill"));
        QVERIFY(pill && !pill->isHidden());
        pill->winId();
        QVERIFY(pill->internalWinId() != 0);
        QCoreApplication::processEvents();

        QLabel* label = nullptr;
        QPoint inPill;
        const QList<QLabel*> labels = pill->findChildren<QLabel*>();
        for (QLabel* candidate : labels) {
            const QPoint center = candidate->mapTo(pill, candidate->rect().center());
            if (candidate->isVisibleTo(pill) && pill->childAt(center) == candidate) {
                label = candidate;
                inPill = center;
                break;
            }
        }
        QVERIFY2(label != nullptr, "the pill shows no label");
        const QPoint point = pill->mapTo(&pan, inPill);

        NSView* pillView = viewOf(pill);
        NSView* panView = viewOf(&pan);
        NSView* hit = hitViewAt(&pan, point);
        QVERIFY2(hit != nil, "AppKit found no view at the label");
        QVERIFY2([hit isDescendantOf:pillView],
                 "AppKit sends a press at the label to another view");
        QVERIFY2(hit != panView, "AppKit sends a press at the label to the pan's own view");
        QWindow* hitWindow = windowOfView(hit);
        QVERIFY2(hitWindow != nullptr, "the view AppKit picked is not a Qt window's");
        QVERIFY2(hitWindow != pan.windowHandle(), "the label's view is the pan's own window");

        PanMouseLog log(&pan);
        QSignalSpy tuned(&pan, &SpectrumWidget::frequencyClicked);
        QSignalSpy panned(&pan, &SpectrumWidget::centerChanged);
        const NSPoint inHitNs = [hit convertPoint:NSMakePoint(point.x(), point.y())
                                         fromView:panView];
        const QPoint inHit(int(inHitNs.x), int(inHitNs.y));
        // A click, then a drag well out of the pill, then a right click.
        QTest::mouseClick(hitWindow, Qt::LeftButton, Qt::NoModifier, inHit);
        QTest::mousePress(hitWindow, Qt::LeftButton, Qt::NoModifier, inHit);
        QTest::mouseMove(hitWindow, inHit + QPoint(300, 40));
        QTest::mouseMove(hitWindow, inHit + QPoint(600, 40));
        QTest::mouseRelease(hitWindow, Qt::LeftButton, Qt::NoModifier, inHit + QPoint(600, 40));
        QTest::mouseClick(hitWindow, Qt::RightButton, Qt::NoModifier, inHit);
        QCoreApplication::processEvents();

        QCOMPARE(log.seen, 0);
        QCOMPARE(tuned.count(), 0);
        QCOMPARE(panned.count(), 0);
        QVERIFY2(panel->displayPillVisible(), "a press inside the pill closed it");
        QVERIFY(QApplication::activePopupWidget() == nullptr);
    }
};

QTEST_MAIN(TstOverlayFlyoutAboveFlagsNative)
#include "tst_overlay_flyout_above_flags_native.moc"
