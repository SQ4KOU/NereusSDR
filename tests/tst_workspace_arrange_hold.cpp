// no-port-check: NereusSDR-original checks of the Shift hold and Escape for arranging the workspace.
// Modification history (NereusSDR):
//   2026-10-11 - Holding Shift alone arranges while it is held, anything
//                 else with Shift does not, Escape leaves the switched-on
//                 Arrange, and the look-off window has none of it. J.J.
//                 Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-11 - The hold does not begin with focus in a text field, and a
//                 container's menus tick Arrange Workspace from the switch,
//                 not the hold. J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
//   2026-10-11 - A pointer move without Shift ends a hold whose release
//                 was not seen, and Escape closes an open profile rail
//                 before it leaves Arrange. The timer that closes a menu
//                 left open ends with its lookup. J.J. Boyd (KG4VCF),
//                 AI-assisted via Anthropic Claude Code.
//   2026-10-11 - Each case marks the audio first-run notice done: on
//                 Linux it opened over the window and took Escape. An
//                 Escape that does not leave Arrange names the focus and
//                 the active window. J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
#include <QtTest>
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLoggingCategory>
#include <QMenu>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTimer>
#include <QWheelEvent>
#include <chrono>
#include <memory>

#include "core/AppSettings.h"
#include "core/RadioDiscovery.h"
#include "fakes/MainWindowTestSettings.h"
#include "fakes/RemoteWindowHarness.h"
#include "gui/MainWindow.h"
#include "gui/containers/ArrangeHoldFilter.h"
#include "gui/containers/ContainerContentHost.h"
#include "gui/containers/ContainerContentRegistry.h"
#include "gui/containers/ContainerManager.h"
#include "gui/containers/ContainerWidget.h"
#include "gui/containers/ContainerWorkspaceStore.h"
#include "gui/containers/WorkspaceHeader.h"
#include "gui/styles/AppTheme.h"
#include "gui/styles/WorkspacePreview.h"
#include "gui/widgets/ProfileRailDock.h"

using namespace NereusSDR;
using NereusSDR::Test::RemoteWindowHarness;

namespace {

// The delay the cases run with: timing comes from this hook, never from a
// wall-clock margin around the real 300 ms.
constexpr int kTestDelayMs = 20;
// Long enough for a 20 ms timer that was wrongly left running to fire.
constexpr int kWouldHaveFiredMs = 150;

void settle()
{
    for (int pass = 0; pass < 6; ++pass) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        QCoreApplication::processEvents(QEventLoop::AllEvents);
    }
}

// Counts the key presses that reach it.
class KeyProbe final : public QWidget {
public:
    using QWidget::QWidget;
    QList<int> pressed;
    QList<int> released;

protected:
    void keyPressEvent(QKeyEvent* event) override
    {
        pressed.append(event->key());
        event->accept();
    }
    void keyReleaseEvent(QKeyEvent* event) override
    {
        released.append(event->key());
        event->accept();
    }
};

// Offline fixture: a store, a registry and a manager over a plain dock
// widget, with the hold filter. No MainWindow, discovery, radio or poller.
struct HoldFixture {
    QTemporaryDir directory;
    AppSettings settings{directory.filePath("arrange-hold.xml")};
    ContainerWorkspaceStore store{settings};
    QWidget dock;
    QSplitter splitter;
    ContainerContentRegistry registry;
    ContainerManager manager{&dock, &splitter};
    std::unique_ptr<ArrangeHoldFilter> filter;
    KeyProbe* probe{nullptr};

    explicit HoldFixture(bool withFilter = true)
    {
        dock.resize(1200, 900);
        probe = new KeyProbe(&dock);
        probe->setFocusPolicy(Qt::StrongFocus);
        probe->setGeometry(0, 860, 40, 40);
        manager.setWorkspaceAdapter(&store, &registry);
        if (withFilter) {
            addFilter();
        }
    }
    void addFilter()
    {
        filter = std::make_unique<ArrangeHoldFilter>(&manager, nullptr);
        filter->setHoldDelayForTest(kTestDelayMs);
    }
    ContainerDocument container(const QString& id, HeaderMode mode, DockMode dock, int x)
    {
        ContainerDocument result;
        result.id = id;
        result.name = id;
        result.layout = ContentLayout::VerticalStack;
        result.dockMode = dock;
        result.header = mode;
        result.geometry = QRect(x, 20, 360, 420);
        for (const QString& variant : {QStringLiteral("signalMaxBin"), QStringLiteral("comp")}) {
            ContentEntry entry = registry.makeEntry("meter." + variant);
            entry.id = id + "-" + variant;
            entry.context["sliceId"] = 0;
            result.contents.append(entry);
        }
        return result;
    }
    // A free container with its header shown, a free one with it hidden
    // and a popped-out window with it hidden.
    bool show()
    {
        WorkspaceDocument document;
        document.containers.append(container("shown", HeaderMode::Shown, DockMode::OverlayDocked, 10));
        document.containers.append(container("hidden", HeaderMode::Hidden, DockMode::OverlayDocked, 400));
        document.containers.append(container("window", HeaderMode::Hidden, DockMode::Floating, 790));
        document.mainContainerId = "shown";
        if (manager.commitWorkspace(document, 0).status != CommitStatus::Saved) {
            return false;
        }
        dock.show();
        if (!QTest::qWaitForWindowExposed(&dock)) {
            return false;
        }
        settle();
        return manager.container("shown") && manager.container("hidden") && manager.container("window");
    }

    void key(QEvent::Type type, int k, Qt::KeyboardModifiers modifiers = Qt::NoModifier,
             QWidget* target = nullptr, bool autoRepeat = false)
    {
        QKeyEvent event(type, k, modifiers, QString(), autoRepeat);
        QApplication::sendEvent(target != nullptr ? target : probe, &event);
    }
    void shiftDown(Qt::KeyboardModifiers others = Qt::NoModifier, QWidget* target = nullptr)
    {
        key(QEvent::KeyPress, Qt::Key_Shift, others | Qt::ShiftModifier, target);
    }
    void shiftUp(QWidget* target = nullptr) { key(QEvent::KeyRelease, Qt::Key_Shift, Qt::NoModifier, target); }
    // Shift down, then the delay: true once the hold is arranging.
    bool hold(QWidget* target = nullptr)
    {
        shiftDown(Qt::NoModifier, target);
        return QTest::qWaitFor([this] { return manager.arrangeHeld(); }, 5000);
    }
    // Shift stays down for well past the delay; true when nothing arranged.
    bool nothingArranges()
    {
        if (filter && filter->holdPendingForTest()) {
            return false;
        }
        QTest::qWait(kWouldHaveFiredMs);
        return !manager.arranging() && !manager.arrangeHeld();
    }
};

void mouse(QEvent::Type type, QWidget* target, const QPoint& global, Qt::MouseButton button,
           Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers)
{
    QMouseEvent event(type, target->mapFromGlobal(global), global, button, buttons, modifiers);
    QApplication::sendEvent(target, &event);
}
void press(QWidget* target, const QPoint& global, Qt::KeyboardModifiers modifiers = Qt::ShiftModifier)
{
    mouse(QEvent::MouseButtonPress, target, global, Qt::LeftButton, Qt::LeftButton, modifiers);
}
void move(QWidget* target, const QPoint& global, Qt::KeyboardModifiers modifiers = Qt::ShiftModifier)
{
    mouse(QEvent::MouseMove, target, global, Qt::NoButton, Qt::LeftButton, modifiers);
}
void release(QWidget* target, const QPoint& global, Qt::KeyboardModifiers modifiers = Qt::ShiftModifier)
{
    mouse(QEvent::MouseButtonRelease, target, global, Qt::LeftButton, Qt::NoButton, modifiers);
}
void wheel(QWidget* target, Qt::KeyboardModifiers modifiers)
{
    const QPoint local(5, 5);
    QWheelEvent event(local, target->mapToGlobal(local), QPoint(), QPoint(0, 120), Qt::NoButton, modifiers,
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(target, &event);
}

// The window as the app builds it, in the look the case asks for; no radio
// and no discovery (scans are held off for the whole test).
struct LookWindow {
    std::unique_ptr<MainWindow> window;
    ContainerManager* manager{nullptr};

    explicit LookWindow(bool preview)
    {
        WorkspacePreview::setEnabledForTest(preview);
        auto& app = *qobject_cast<QApplication*>(QCoreApplication::instance());
        applyDarkPalette(app);
        applyAppBaselineQss(app);
        if (preview) {
            applyWorkspacePalette(app);
        }
        window = std::make_unique<MainWindow>(RemoteStationOptions{}, nullptr,
                                              MainWindow::ConnectionStartup::Deferred);
        window->resize(1280, 800);
        window->show();
        settle();
        manager = window->findChild<ContainerManager*>();
    }
};

} // namespace

class TstWorkspaceArrangeHold : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QLoggingCategory::setFilterRules(QStringLiteral("nereus.*.debug=false"));
        QVERIFY(RemoteWindowHarness::useIsolatedProfile(QStringLiteral("workspace-arrange-hold")));
        qApp->setStyle(QStringLiteral("Fusion"));
        RadioDiscovery::clearHoldOffForTest();
        RadioDiscovery discovery;
        discovery.holdOffScans(std::chrono::minutes{30});
    }
    void init()
    {
        QVERIFY(RemoteWindowHarness::clearIsolatedProfile());
        // No audio first-run dialog: on Linux the VAX first-run dialog opens
        // over the window and is the active window, and Escape with a
        // dialog active is the dialog's, not Arrange's.
        Test::markAudioFirstRunDone();
        WorkspacePreview::setEnabledForTest(true);
    }
    void cleanupTestCase()
    {
        WorkspacePreview::setEnabledForTest(false);
        RadioDiscovery::clearHoldOffForTest();
        QVERIFY(RemoteWindowHarness::removeIsolatedProfile());
    }

    void theDelayIsAThirdOfASecondAndNothingArrangesBeforeIt()
    {
        QCOMPARE(ArrangeHoldFilter::kHoldDelayMs, 300);
        HoldFixture f(false);
        // The filter as the window makes it, with the real delay.
        f.filter = std::make_unique<ArrangeHoldFilter>(&f.manager, nullptr);
        QVERIFY(f.show());
        f.shiftDown();
        QVERIFY(f.filter->holdPendingForTest());
        QVERIFY(!f.manager.arranging());
        f.shiftUp();
        QVERIFY(!f.filter->holdPendingForTest());
        QVERIFY(!f.manager.arranging());
    }

    void shiftHeldAloneArrangesUntilItIsLetGo()
    {
        HoldFixture f;
        QVERIFY(f.show());
        ContainerWidget* shown = f.manager.container("shown");
        ContainerWidget* hidden = f.manager.container("hidden");
        ContainerWidget* window = f.manager.container("window");
        QVERIFY(window->isFloating());
        QVERIFY(shown->chromeVisible());
        QVERIFY(!hidden->chromeVisible());
        QVERIFY(!window->chromeVisible());
        const quint64 revision = f.store.snapshot().revision;
        QSignalSpy switched(&f.manager, &ContainerManager::arrangeModeChanged);

        QVERIFY(f.hold());
        settle();
        QVERIFY(f.manager.arrangeHeld());
        QVERIFY(!f.manager.arrangeMode());
        // Every header is drawn, the popped-out window's too, with the
        // entry headers and the Arrange highlight.
        for (ContainerWidget* c : {shown, hidden, window}) {
            QVERIFY2(c->chromeVisible(), qPrintable(c->id()));
            QVERIFY2(c->arrangeActive(), qPrintable(c->id()));
            QVERIFY2(c->workspaceHeader()->arrangeAccent(), qPrintable(c->id()));
            auto* entryHeader = f.manager.contentHost(c->id())->entryHeader(c->id() + "-comp");
            QVERIFY2(entryHeader && entryHeader->isVisible(), qPrintable(c->id()));
        }
        // Auto-repeat presses while it is held change nothing.
        f.key(QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier, nullptr, true);
        QVERIFY(f.manager.arranging());

        f.shiftUp();
        settle();
        QVERIFY(!f.manager.arranging());
        QVERIFY(shown->chromeVisible());
        QVERIFY(!hidden->chromeVisible());
        QVERIFY(!window->chromeVisible());
        QVERIFY(!hidden->arrangeActive());
        // The switch never moved, so the menu's check mark never flickered,
        // and nothing was saved.
        QCOMPARE(switched.count(), 0);
        QCOMPARE(f.store.snapshot().revision, revision);
        // Shift itself always reached the widget it was typed at.
        QCOMPARE(f.probe->pressed, (QList<int>{Qt::Key_Shift, Qt::Key_Shift}));
        QCOMPARE(f.probe->released, (QList<int>{Qt::Key_Shift}));
    }

    void shiftHeldOverAPoppedOutWindowArrangesEverything()
    {
        HoldFixture f;
        QVERIFY(f.show());
        ContainerWidget* window = f.manager.container("window");
        QVERIFY(f.hold(window));
        settle();
        QVERIFY(window->chromeVisible());
        QVERIFY(f.manager.container("hidden")->chromeVisible());
        f.shiftUp(window);
        QVERIFY(!f.manager.arranging());
    }

    void anythingElseWithShiftCancelsThatPress_data()
    {
        QTest::addColumn<QString>("other");
        QTest::newRow("mouse press") << "press";
        QTest::newRow("wheel turn") << "wheel";
        QTest::newRow("another key") << "key";
        QTest::newRow("Shift let go") << "release";
    }
    void anythingElseWithShiftCancelsThatPress()
    {
        QFETCH(QString, other);
        HoldFixture f;
        // Slow enough that the other input always lands inside the delay.
        f.filter->setHoldDelayForTest(60000);
        QVERIFY(f.show());
        f.shiftDown();
        QVERIFY(f.filter->holdPendingForTest());
        const QPoint point = f.probe->mapToGlobal(QPoint(5, 5));
        if (other == "press") {
            press(f.probe, point);
            release(f.probe, point);
        } else if (other == "wheel") {
            wheel(f.probe, Qt::ShiftModifier);
        } else if (other == "key") {
            f.key(QEvent::KeyPress, Qt::Key_A, Qt::ShiftModifier);
            f.key(QEvent::KeyRelease, Qt::Key_A, Qt::ShiftModifier);
        } else {
            f.shiftUp();
        }
        QVERIFY(!f.filter->holdPendingForTest());
        // Still down, with auto-repeat, for well past the delay.
        f.filter->setHoldDelayForTest(kTestDelayMs);
        f.key(QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier, nullptr, true);
        QVERIFY(f.nothingArranges());
        // Let go and pressed again alone, the hold works.
        f.shiftUp();
        QVERIFY(f.hold());
        f.shiftUp();
        QVERIFY(!f.manager.arranging());
    }

    void shiftWithAnotherModifierOrAButtonDownIsNoHold_data()
    {
        QTest::addColumn<int>("modifierKey");
        QTest::addColumn<int>("modifier");
        QTest::newRow("Ctrl") << int(Qt::Key_Control) << int(Qt::ControlModifier);
        QTest::newRow("Alt") << int(Qt::Key_Alt) << int(Qt::AltModifier);
        QTest::newRow("Meta") << int(Qt::Key_Meta) << int(Qt::MetaModifier);
        QTest::newRow("mouse button") << 0 << 0;
    }
    void shiftWithAnotherModifierOrAButtonDownIsNoHold()
    {
        QFETCH(int, modifierKey);
        QFETCH(int, modifier);
        HoldFixture f;
        QVERIFY(f.show());
        const QPoint point = f.probe->mapToGlobal(QPoint(5, 5));
        const Qt::KeyboardModifiers other{modifier};
        if (modifierKey != 0) {
            f.key(QEvent::KeyPress, modifierKey, other);
            f.shiftDown(other);
        } else {
            press(f.probe, point, Qt::NoModifier);
            f.shiftDown();
        }
        QVERIFY(f.nothingArranges());
        f.shiftUp();
        if (modifierKey != 0) {
            f.key(QEvent::KeyRelease, modifierKey, Qt::NoModifier);
        } else {
            release(f.probe, point, Qt::NoModifier);
        }
        // With it out of the way the hold works.
        QVERIFY(f.hold());
        f.shiftUp();
        QVERIFY(!f.manager.arranging());
    }

    // Shift in a text field is for typing: a hold does not begin while one
    // has keyboard focus, whatever kind of field it is.
    void aHoldDoesNotBeginInATextField_data()
    {
        QTest::addColumn<QString>("kind");
        for (const char* kind : {"QLineEdit", "QTextEdit", "QPlainTextEdit", "QSpinBox", "editable QComboBox"}) {
            QTest::newRow(kind) << QString::fromLatin1(kind);
        }
    }
    void aHoldDoesNotBeginInATextField()
    {
        QFETCH(QString, kind);
        HoldFixture f;
        QWidget* field = nullptr;
        if (kind == "QLineEdit") {
            field = new QLineEdit(&f.dock);
        } else if (kind == "QTextEdit") {
            field = new QTextEdit(&f.dock);
        } else if (kind == "QPlainTextEdit") {
            field = new QPlainTextEdit(&f.dock);
        } else if (kind == "QSpinBox") {
            field = new QSpinBox(&f.dock);
        } else {
            auto* combo = new QComboBox(&f.dock);
            combo->setEditable(true);
            field = combo;
        }
        field->setGeometry(60, 860, 160, 36);
        QVERIFY(f.show());
        f.dock.activateWindow();
        field->setFocus(Qt::OtherFocusReason);
        QTRY_VERIFY(QApplication::focusWidget() != nullptr
                    && (QApplication::focusWidget() == field || field->isAncestorOf(QApplication::focusWidget())));
        QWidget* typedAt = QApplication::focusWidget();

        // Shift alone, held: the delay never starts, so nothing arranges.
        f.shiftDown(Qt::NoModifier, typedAt);
        QVERIFY(!f.filter->holdPendingForTest());
        QVERIFY(f.nothingArranges());
        f.shiftUp(typedAt);
        // Typing a capital does not either.
        f.shiftDown(Qt::NoModifier, typedAt);
        f.key(QEvent::KeyPress, Qt::Key_N, Qt::ShiftModifier, typedAt);
        f.key(QEvent::KeyRelease, Qt::Key_N, Qt::ShiftModifier, typedAt);
        QVERIFY(f.nothingArranges());
        f.shiftUp(typedAt);

        // With focus out of the field the same press arranges.
        f.probe->setFocus(Qt::OtherFocusReason);
        QTRY_COMPARE(QApplication::focusWidget(), f.probe);
        QVERIFY(f.hold());
        f.shiftUp();
        QVERIFY(!f.manager.arranging());
    }

    void theMenusTickTheSwitchAndChoosingItDuringAHoldTurnsItOn_data()
    {
        QTest::addColumn<bool>("options");
        QTest::newRow("header options menu") << true;
        QTest::newRow("body right-click menu") << false;
    }
    void theMenusTickTheSwitchAndChoosingItDuringAHoldTurnsItOn()
    {
        QFETCH(bool, options);
        HoldFixture f;
        QVERIFY(f.show());
        ContainerWidget* shown = f.manager.container("shown");
        const auto open = [shown, options] {
            if (options) {
                shown->workspaceHeader()->optionsButton()->click();
            } else {
                const QPoint at(20, shown->height() - 20);
                QContextMenuEvent event(QContextMenuEvent::Mouse, at, shown->mapToGlobal(at));
                QApplication::sendEvent(shown, &event);
            }
        };
        // Reads the item's tick and chooses it.
        const auto choose = [&open](bool& ticked) {
            bool found = false;
            QTimer::singleShot(0, [&found, &ticked] {
                auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
                if (menu == nullptr) {
                    return;
                }
                for (QAction* action : menu->actions()) {
                    if (action->text() == QStringLiteral("Arrange Workspace")) {
                        found = true;
                        ticked = action->isChecked();
                        action->trigger();
                    }
                }
                menu->close();
            });
            // Never leave a menu open under a failed lookup. The timer
            // goes with this call, so it cannot close a later case's menu.
            QObject untilMenuClosed;
            QTimer::singleShot(3000, &untilMenuClosed, [] {
                if (QWidget* popup = QApplication::activePopupWidget()) {
                    popup->close();
                }
            });
            open();
            return found;
        };

        QVERIFY(f.hold());
        settle();
        QVERIFY(f.manager.arranging());
        QVERIFY(!f.manager.arrangeMode());
        // Held, with the switch off: not ticked, and choosing it turns the
        // switch on.
        bool ticked = true;
        QVERIFY(choose(ticked));
        QVERIFY(!ticked);
        QVERIFY(f.manager.arrangeMode());
        f.shiftUp();
        settle();
        QVERIFY(f.manager.arranging());
        // Switched on: ticked, and choosing it turns the switch off.
        ticked = false;
        QVERIFY(choose(ticked));
        QVERIFY(ticked);
        QVERIFY(!f.manager.arrangeMode());
        QVERIFY(!f.manager.arranging());
    }

    void shiftLetGoDuringADragEndsTheHoldWhenTheDragDoes()
    {
        HoldFixture f;
        QVERIFY(f.show());
        ContainerWidget* hidden = f.manager.container("hidden");
        QVERIFY(f.hold());
        settle();
        // The hidden header is there to be grabbed only because of the hold.
        QWidget* header = hidden->workspaceHeader();
        QVERIFY(header->isVisible());
        const QPoint start = hidden->pos();
        const QPoint grab = header->mapToGlobal(QPoint(header->width() / 2, header->height() / 2));
        QVERIFY(!f.manager.containerDragActive());
        press(header, grab);
        move(header, grab + QPoint(30, 40));
        QVERIFY(f.manager.containerDragActive());
        QVERIFY(hidden->pos() != start);

        f.shiftUp();
        settle();
        QVERIFY(f.manager.arranging());
        QVERIFY(header->isVisible());
        move(header, grab + QPoint(60, 80), Qt::NoModifier);
        settle();
        QVERIFY(f.manager.arranging());

        release(header, grab + QPoint(60, 80), Qt::NoModifier);
        QVERIFY(!f.manager.containerDragActive());
        QTRY_VERIFY(!f.manager.arranging());
        settle();
        QVERIFY(!hidden->chromeVisible());
        QVERIFY(hidden->pos() != start);
    }

    void shiftPressedAgainDuringTheDragKeepsTheHold()
    {
        HoldFixture f;
        QVERIFY(f.show());
        ContainerWidget* hidden = f.manager.container("hidden");
        QVERIFY(f.hold());
        settle();
        QWidget* header = hidden->workspaceHeader();
        const QPoint grab = header->mapToGlobal(QPoint(header->width() / 2, header->height() / 2));
        press(header, grab);
        move(header, grab + QPoint(30, 40));
        f.shiftUp();
        f.shiftDown();
        release(header, grab + QPoint(30, 40));
        settle();
        QVERIFY(f.manager.arranging());
        f.shiftUp();
        QVERIFY(!f.manager.arranging());
    }

    // Shift let go where the filter saw no key release (another window had
    // the keyboard): the next pointer move says it is up.
    void shiftLetGoUnseenEndsTheHoldAtTheNextPointerMove()
    {
        HoldFixture f;
        QVERIFY(f.show());
        QVERIFY(f.hold());
        const QPoint at = f.probe->mapToGlobal(QPoint(5, 5));
        // A move with Shift still down changes nothing.
        mouse(QEvent::MouseMove, f.probe, at, Qt::NoButton, Qt::NoButton, Qt::ShiftModifier);
        QVERIFY(f.manager.arrangeHeld());
        mouse(QEvent::MouseMove, f.probe, at + QPoint(2, 0), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QVERIFY(!f.manager.arrangeHeld());
        QVERIFY(!f.manager.arranging());
        // And a press of Shift still pending is called off the same way.
        f.shiftDown();
        QVERIFY(f.filter->holdPendingForTest());
        mouse(QEvent::MouseMove, f.probe, at, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QVERIFY(!f.filter->holdPendingForTest());
        QVERIFY(f.nothingArranges());
    }

    void losingTheApplicationEndsTheHold()
    {
        HoldFixture f;
        QVERIFY(f.show());
        QVERIFY(f.hold());
        QEvent deactivate(QEvent::ApplicationDeactivate);
        QApplication::sendEvent(qApp, &deactivate);
        QVERIFY(!f.manager.arranging());
        // Shift's release went elsewhere; the next press alone still works.
        QVERIFY(f.hold());
        f.shiftUp();
        QVERIFY(!f.manager.arranging());

        // Lost before the delay is over, nothing arranges.
        f.shiftDown();
        QApplication::sendEvent(qApp, &deactivate);
        QVERIFY(f.nothingArranges());
    }

    void theSwitchStaysOnThroughAHold()
    {
        HoldFixture f;
        QVERIFY(f.show());
        QSignalSpy switched(&f.manager, &ContainerManager::arrangeModeChanged);
        f.manager.setArrangeMode(true);
        QCOMPARE(switched.count(), 1);
        f.shiftDown();
        QTRY_VERIFY(f.manager.arrangeHeld());
        f.shiftUp();
        settle();
        QVERIFY(!f.manager.arrangeHeld());
        QVERIFY(f.manager.arrangeMode());
        QVERIFY(f.manager.arranging());
        QVERIFY(f.manager.container("hidden")->chromeVisible());
        QCOMPARE(switched.count(), 1);

        // And the switch going off during a hold leaves the hold arranging.
        QVERIFY(f.hold());
        f.manager.setArrangeMode(false);
        settle();
        QCOMPARE(switched.count(), 2);
        QVERIFY(f.manager.arranging());
        QVERIFY(f.manager.container("hidden")->chromeVisible());
        f.shiftUp();
        settle();
        QVERIFY(!f.manager.arranging());
        QVERIFY(!f.manager.container("hidden")->chromeVisible());
    }

    void escapeLeavesTheSwitchedOnArrange()
    {
        HoldFixture f;
        QVERIFY(f.show());
        // With Arrange off, Escape goes where it went before.
        f.key(QEvent::KeyPress, Qt::Key_Escape);
        QCOMPARE(f.probe->pressed, (QList<int>{Qt::Key_Escape}));
        f.probe->pressed.clear();

        f.manager.setArrangeMode(true);
        settle();
        QVERIFY(f.manager.container("hidden")->chromeVisible());
        f.key(QEvent::KeyPress, Qt::Key_Escape);
        settle();
        QVERIFY(!f.manager.arrangeMode());
        QVERIFY(!f.manager.arranging());
        QVERIFY(!f.manager.container("hidden")->chromeVisible());
        // The key was used up.
        QVERIFY(f.probe->pressed.isEmpty());

        // A hold alone is not the switch: Escape passes through and the
        // hold goes on.
        QVERIFY(f.hold());
        f.key(QEvent::KeyPress, Qt::Key_Escape, Qt::ShiftModifier);
        QCOMPARE(f.probe->pressed.count(Qt::Key_Escape), 1);
        QVERIFY(f.manager.arranging());
        f.shiftUp();
    }

    void escapeGoesToAMenuADialogOrATextFieldFirst()
    {
        HoldFixture f;
        auto* edit = new QLineEdit(&f.dock);
        edit->setGeometry(60, 860, 160, 36);
        QVERIFY(f.show());
        f.manager.setArrangeMode(true);

        QMenu menu(&f.dock);
        menu.addAction(QStringLiteral("One"));
        menu.popup(f.dock.mapToGlobal(QPoint(100, 100)));
        QTRY_COMPARE(QApplication::activePopupWidget(), &menu);
        f.key(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier, &menu);
        QTRY_VERIFY(!menu.isVisible());
        QVERIFY(f.manager.arrangeMode());

        QDialog modal(&f.dock);
        modal.open();
        QTRY_COMPARE(QApplication::activeModalWidget(), &modal);
        f.key(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier, &modal);
        QTRY_VERIFY(!modal.isVisible());
        QVERIFY(f.manager.arrangeMode());

        f.dock.activateWindow();
        edit->setFocus(Qt::OtherFocusReason);
        QTRY_COMPARE(QApplication::focusWidget(), edit);
        f.key(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier, edit);
        QVERIFY(f.manager.arrangeMode());

        // Nothing else wants it now.
        f.probe->setFocus(Qt::OtherFocusReason);
        QTRY_COMPARE(QApplication::focusWidget(), f.probe);
        f.key(QEvent::KeyPress, Qt::Key_Escape);
        QVERIFY(!f.manager.arrangeMode());
    }

    // Either may see the key first: the hold filter, or the container
    // whose drag it cancels.
    void escapeDuringADragCancelsTheDragAndArrangeStaysOn_data()
    {
        QTest::addColumn<bool>("filterMadeLast");
        QTest::newRow("container sees Escape first") << false;
        QTest::newRow("hold filter sees Escape first") << true;
    }
    void escapeDuringADragCancelsTheDragAndArrangeStaysOn()
    {
        QFETCH(bool, filterMadeLast);
        HoldFixture f(!filterMadeLast);
        QVERIFY(f.show());
        if (filterMadeLast) {
            f.addFilter();
        }
        f.manager.setArrangeMode(true);
        settle();
        ContainerWidget* hidden = f.manager.container("hidden");
        QWidget* header = hidden->workspaceHeader();
        const QPoint start = hidden->pos();
        const QPoint grab = header->mapToGlobal(QPoint(header->width() / 2, header->height() / 2));
        press(header, grab, Qt::NoModifier);
        move(header, grab + QPoint(30, 40), Qt::NoModifier);
        QVERIFY(f.manager.containerDragActive());
        QVERIFY(hidden->pos() != start);

        f.key(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier, hidden);
        QVERIFY(!f.manager.containerDragActive());
        QCOMPARE(hidden->pos(), start);
        QVERIFY(f.manager.arrangeMode());
        release(header, grab + QPoint(30, 40), Qt::NoModifier);
        settle();
        QVERIFY(f.manager.arrangeMode());
        QCOMPARE(hidden->pos(), start);

        // With the drag over, Escape leaves Arrange.
        f.key(QEvent::KeyPress, Qt::Key_Escape);
        QVERIFY(!f.manager.arrangeMode());
    }

    void theLookOnWindowOwnsOneFilter()
    {
        LookWindow w(true);
        QVERIFY(w.manager);
        const auto filters = w.window->findChildren<ArrangeHoldFilter*>();
        QCOMPARE(filters.size(), 1);
        filters.first()->setHoldDelayForTest(kTestDelayMs);
        QKeyEvent down(QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier);
        QApplication::sendEvent(w.window.get(), &down);
        QTRY_VERIFY(w.manager->arranging());
        QVERIFY(!w.manager->arrangeMode());
        QKeyEvent up(QEvent::KeyRelease, Qt::Key_Shift, Qt::NoModifier);
        QApplication::sendEvent(w.window.get(), &up);
        QVERIFY(!w.manager->arranging());
    }

    // The profile rail is no popup, so the hold filter cannot see that it
    // is open. Escape closes the rail first; Arrange stays on until the
    // next Escape.
    void escapeClosesAnOpenProfileRailBeforeItLeavesArrange()
    {
        LookWindow w(true);
        QVERIFY(w.manager);
        auto* rail = w.window->findChild<ProfileRailDock*>();
        QVERIFY(rail);
        w.manager->setArrangeMode(true);
        settle();
        rail->open(true);
        QVERIFY(rail->isOpen());
        QVERIFY(QApplication::activePopupWidget() == nullptr);
        const auto escape = [&w] {
            QWidget* focus = QApplication::focusWidget();
            QKeyEvent event(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QApplication::sendEvent(focus != nullptr ? focus : static_cast<QWidget*>(w.window.get()), &event);
            settle();
        };
        escape();
        QVERIFY(!rail->isOpen());
        QVERIFY(w.manager->arrangeMode());
        const auto named = [](const QWidget* widget) {
            return widget == nullptr ? QStringLiteral("(none)")
                : QString::fromLatin1(widget->metaObject()->className()) + QLatin1Char('#') + widget->objectName();
        };
        const QString state = QStringLiteral("focus %1, active window %2, popup %3, modal %4")
            .arg(named(QApplication::focusWidget()), named(QApplication::activeWindow()),
                 named(QApplication::activePopupWidget()), named(QApplication::activeModalWidget()));
        escape();
        QVERIFY2(!w.manager->arrangeMode(), qPrintable(state));
    }

    void theLookOffWindowHasNoFilterAndShiftChangesNothing()
    {
        LookWindow w(false);
        QVERIFY(w.manager);
        QVERIFY(w.window->findChildren<ArrangeHoldFilter*>().isEmpty());
        ContainerWidget* panel = w.manager->panelContainer();
        QVERIFY(panel);
        const bool chrome = panel->chromeVisible();
        QKeyEvent down(QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier);
        QApplication::sendEvent(w.window.get(), &down);
        constexpr int kHeldMs = 500;
        QTest::qWait(kHeldMs);
        QVERIFY(!w.manager->arranging());
        QVERIFY(!w.manager->arrangeHeld());
        QVERIFY(!panel->arrangeActive());
        QCOMPARE(panel->chromeVisible(), chrome);
        QKeyEvent up(QEvent::KeyRelease, Qt::Key_Shift, Qt::NoModifier);
        QApplication::sendEvent(w.window.get(), &up);
        QVERIFY(!w.manager->arranging());
        // Escape has nothing to leave.
        w.manager->setArrangeMode(true);
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(w.window.get(), &escape);
        QVERIFY(w.manager->arrangeMode());
        w.manager->setArrangeMode(false);
    }
};

QTEST_MAIN(TstWorkspaceArrangeHold)
#include "tst_workspace_arrange_hold.moc"
