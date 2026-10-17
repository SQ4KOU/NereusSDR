// no-port-check: NereusSDR-original native-window checks of the arrange drag between the main window and a pop-out window.
// Modification history (NereusSDR):
//   2026-10-11 - While arranging, a free container dragged out of the main
//                 window becomes a window, and a window dragged onto an
//                 edge of the main window docks there, with the pointer
//                 delivered through real windows. J.J. Boyd (KG4VCF),
//                 AI-assisted via Anthropic Claude Code.
//   2026-10-11 - The press, the moves and the release all go through the
//                 window that took the press, so the header that started
//                 the drag receives them; before, only the press reached
//                 it and neither case exercised the drag. Each case now
//                 checks, with the button still down, that a move was
//                 handled. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//                 Claude Code.
//
// NATIVE_WINDOW: offscreen tests (tst_workspace_arrange_drag) cover the rule
// by sending every event straight to the header. These cases run it on a
// real screen: real top-level windows, the main window's real frame
// geometry with its title bar, and a real new window at the drop place. The
// pointer is injected at Qt's window-system layer (the QWindow overloads of
// QTest), above the operating system, so Qt's own routing carries each event
// from the press window to the widget that took the press.
//
// Still checked by hand, because QTest cannot show either: that the
// operating system keeps delivering the drag to the press window once the
// pointer has left it, and that leaving the app in the middle of a drag
// cancels it.
//
// QtTest prints "Mouse event at x, y occurs outside target window" for the
// steps of the first case that pass the main window's right edge
// (qtestmouse.h). That describes the gesture; it is not a fault.
#include <QtTest>
#include <QApplication>
#include <QMainWindow>
#include <QScreen>
#include <QSignalSpy>
#include <QSplitter>
#include <QTemporaryDir>
#include <memory>
#include "core/AppSettings.h"
#include "gui/containers/ContainerContentRegistry.h"
#include "gui/containers/ContainerManager.h"
#include "gui/containers/ContainerWidget.h"
#include "gui/containers/ContainerWorkspaceStore.h"
#include "gui/containers/EdgeDockStrip.h"
#include "gui/containers/WorkspaceHeader.h"
#include "gui/styles/WorkspacePreview.h"

using namespace NereusSDR;

namespace {

constexpr QSize kMainWindowSize(900, 600);
constexpr int kScreenMarginPx = 40;
// Room beside the main window for a window of its own.
constexpr int kRoomBesidePx = 460;
constexpr int kPlaceTolerancePx = 2;
constexpr int kStepPauseMs = 30;

void settle()
{
    for (int pass = 0; pass < 6; ++pass) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        QCoreApplication::processEvents(QEventLoop::AllEvents);
    }
}

// A main window with the four-edge area around a plain pane, as in
// MainWindow. No radio, discovery or poller.
struct NativeFixture {
    QTemporaryDir directory;
    AppSettings settings{directory.filePath("arrange-drag-native.xml")};
    ContainerWorkspaceStore store{settings};
    ContainerContentRegistry registry;
    QMainWindow window;
    QSplitter* middle{nullptr};
    QWidget* center{nullptr};
    EdgeDockArea* area{nullptr};
    std::unique_ptr<ContainerManager> manager;

    NativeFixture()
    {
        middle = new EdgeDockSplitter(Qt::Horizontal, &window);
        center = new QWidget(middle);
        center->setMinimumWidth(400);
        middle->addWidget(center);
        area = new EdgeDockArea(middle, center, &window);
        window.setCentralWidget(area);
        manager = std::make_unique<ContainerManager>(center, middle);
        manager->setEdgeDockArea(area);
        manager->setWorkspaceAdapter(&store, &registry);
    }

    // False when the screen has no room for the main window and a window
    // beside it.
    bool placeMainWindow()
    {
        const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
        if (available.width() < kMainWindowSize.width() + kRoomBesidePx + 2 * kScreenMarginPx
            || available.height() < kMainWindowSize.height() + 2 * kScreenMarginPx) {
            return false;
        }
        window.setGeometry(QRect(available.topLeft() + QPoint(kScreenMarginPx, kScreenMarginPx), kMainWindowSize));
        return true;
    }

    ContainerDocument container(const QString& id, DockMode mode, const QRect& geometry = QRect())
    {
        ContainerDocument result;
        result.id = id;
        result.name = id;
        result.layout = ContentLayout::VerticalStack;
        result.dockMode = mode;
        result.edge = DockEdge::Right;
        result.header = HeaderMode::Shown;
        result.geometry = geometry;
        ContentEntry entry = registry.makeEntry(QStringLiteral("meter.signalMaxBin"));
        entry.id = id + QStringLiteral("-meter");
        entry.context["sliceId"] = 0;
        result.contents.append(entry);
        return result;
    }

    bool show(const ContainerDocument& source)
    {
        WorkspaceDocument document = store.snapshot();
        document.containers = {container(QStringLiteral("main"), DockMode::PanelDocked), source};
        document.mainContainerId = QStringLiteral("main");
        if (manager->commitWorkspace(document, document.revision).status != CommitStatus::Saved) {
            return false;
        }
        window.show();
        window.raise();
        window.activateWindow();
        if (!QTest::qWaitForWindowExposed(&window)) {
            return false;
        }
        settle();
        manager->setArrangeMode(true);
        settle();
        manager->saveState();
        QTest::qWait(300);
        settle();
        return manager->container(source.id) != nullptr;
    }

    ContainerDocument document(const QString& id) const
    {
        for (const auto& d : store.snapshot().containers) {
            if (d.id == id) {
                return d;
            }
        }
        return {};
    }
};

// The window a press on the widget goes through. A drag's moves and its
// release go through the same one, wherever the pointer is by then, and Qt
// hands them to the widget that took the press.
QWindow* pressWindowOf(QWidget* widget)
{
    return widget->window()->windowHandle();
}

void pressAt(QWindow* pressWindow, const QPoint& global)
{
    QTest::mousePress(pressWindow, Qt::LeftButton, Qt::NoModifier, pressWindow->mapFromGlobal(global));
}

// A pointer move to a screen position with the button held.
void moveTo(QWindow* pressWindow, const QPoint& global)
{
    QTest::mouseMove(pressWindow, pressWindow->mapFromGlobal(global));
    QTest::qWait(kStepPauseMs);
}

// The pointer goes from `from` to `to` in steps, as a hand moves it.
void sweep(QWindow* pressWindow, const QPoint& from, const QPoint& to)
{
    constexpr int kSteps = 12;
    for (int step = 1; step <= kSteps; ++step) {
        moveTo(pressWindow, from + (to - from) * step / kSteps);
    }
}

void releaseAt(QWindow* pressWindow, const QPoint& global)
{
    QTest::mouseRelease(pressWindow, Qt::LeftButton, Qt::NoModifier, pressWindow->mapFromGlobal(global));
}

} // namespace

class TstWorkspaceArrangeDragNative : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { WorkspacePreview::setEnabledForTest(true); }
    void cleanupTestCase() { WorkspacePreview::setEnabledForTest(false); }

    void aFreeContainerDraggedOutOfTheMainWindowBecomesAWindow()
    {
        NativeFixture f;
        if (!f.placeMainWindow()) {
            QSKIP("The screen has no room for the main window and a window beside it.");
        }
        QVERIFY(f.show(f.container(QStringLiteral("source"), DockMode::OverlayDocked, QRect(60, 60, 360, 300))));
        ContainerWidget* source = f.manager->container(QStringLiteral("source"));
        QWidget* header = source->workspaceHeader();
        QVERIFY(header && header->isVisible());
        QSignalSpy commits(&f.store, &ContainerWorkspaceStore::committed);
        const QPoint grab = header->mapToGlobal(QPoint(header->width() / 2, header->height() / 2));
        const QPoint offset = grab - source->mapToGlobal(QPoint(0, 0));
        const QPoint drop = f.window.frameGeometry().topRight() + QPoint(260, 160);
        QWindow* pressWindow = pressWindowOf(header);
        QVERIFY(pressWindow);

        pressAt(pressWindow, grab);
        sweep(pressWindow, grab, drop);
        // The container has followed the pointer out of the main window,
        // held where it was grabbed. Outside there is no landing box.
        QCOMPARE(source->mapToGlobal(QPoint(0, 0)), drop - offset);
        QVERIFY(!f.area->dropPreviewVisible());
        // Still a free container of the main window until the button is up.
        QVERIFY(source->isOverlayDocked());
        QCOMPARE(source->window(), &f.window);
        QCOMPARE(commits.count(), 0);
        releaseAt(pressWindow, drop);
        settle();

        QTRY_VERIFY(source->isFloating());
        QVERIFY(source->window() != &f.window);
        QTRY_VERIFY(source->window()->isVisible());
        QCOMPARE(f.document("source").dockMode, DockMode::Floating);
        QCOMPARE(f.document("source").geometry.topLeft(), drop - offset);
        QTRY_VERIFY2((source->window()->pos() - (drop - offset)).manhattanLength() <= kPlaceTolerancePx,
                     qPrintable(QStringLiteral("%1,%2").arg(source->window()->x()).arg(source->window()->y())));
        QCOMPARE(commits.count(), 1);
        QVERIFY(!f.manager->containerDragActive());
    }

    void aWindowDraggedOntoAnEdgeOfTheMainWindowDocks()
    {
        NativeFixture f;
        if (!f.placeMainWindow()) {
            QSKIP("The screen has no room for the main window and a window beside it.");
        }
        const QRect beside(f.window.geometry().topRight() + QPoint(60, 100), QSize(360, 300));
        QVERIFY(f.show(f.container(QStringLiteral("source"), DockMode::Floating, beside)));
        ContainerWidget* source = f.manager->container(QStringLiteral("source"));
        QVERIFY(source->isFloating());
        QVERIFY(QTest::qWaitForWindowExposed(source->window()));
        QWidget* header = source->workspaceHeader();
        QVERIFY(header && header->isVisible());
        QSignalSpy commits(&f.store, &ContainerWorkspaceStore::committed);
        const QPoint grab = header->mapToGlobal(QPoint(header->width() / 2, header->height() / 2));
        const QPoint drop = f.area->mapToGlobal(QPoint(12, f.area->height() / 2));
        QWidget* floatingWindow = source->window();
        QWidget* parentBefore = source->parentWidget();
        const QPoint offset = grab - floatingWindow->pos();
        QWindow* pressWindow = pressWindowOf(header);
        QVERIFY(pressWindow);

        pressAt(pressWindow, grab);
        sweep(pressWindow, grab, drop);
        // The landing box shows while the button is down, and the window
        // has followed the pointer, held where it was grabbed.
        QVERIFY(f.area->dropPreviewVisible());
        QTRY_VERIFY2((floatingWindow->pos() - (drop - offset)).manhattanLength() <= kPlaceTolerancePx,
                     qPrintable(QStringLiteral("%1,%2").arg(floatingWindow->x()).arg(floatingWindow->y())));
        // Nothing is docked or moved to another parent before the release.
        QVERIFY(source->isFloating());
        QCOMPARE(source->parentWidget(), parentBefore);
        QCOMPARE(source->window(), floatingWindow);
        QCOMPARE(commits.count(), 0);
        releaseAt(pressWindow, drop);
        settle();

        QTRY_VERIFY(source->isPanelDocked());
        QCOMPARE(source->window(), &f.window);
        QCOMPARE(f.document("source").dockMode, DockMode::PanelDocked);
        QCOMPARE(f.document("source").edge, DockEdge::Left);
        QVERIFY(!f.area->dropPreviewVisible());
        QCOMPARE(commits.count(), 1);
        QVERIFY(!f.manager->containerDragActive());
    }
};

QTEST_MAIN(TstWorkspaceArrangeDragNative)
#include "tst_workspace_arrange_drag_native.moc"
