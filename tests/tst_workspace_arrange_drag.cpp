// no-port-check: NereusSDR-original regressions for the one drag rule while arranging in the workspace look.
// Modification history (NereusSDR):
//   2026-10-11 - While arranging, a header drag of a strip, free, floating
//                 or pop-out container docks in an edge zone, sits free over
//                 the pane, pops out outside the main window and is
//                 cancelled anywhere else, by Escape or by a lost grab;
//                 every drop commits once. Not arranging, drags are as they
//                 were. Ctrl no longer steps by 10 px in the look. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
#include <QtTest>
#include <QApplication>
#include <QMainWindow>
#include <QSignalSpy>
#include <QSplitter>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QWindow>
#include <memory>
#include "core/AppSettings.h"
#include "gui/containers/ArrangeHoldFilter.h"
#include "gui/containers/ContainerContentHost.h"
#include "gui/containers/ContainerContentRegistry.h"
#include "gui/containers/ContainerManager.h"
#include "gui/containers/ContainerWidget.h"
#include "gui/containers/ContainerWorkspaceStore.h"
#include "gui/containers/EdgeDockStrip.h"
#include "gui/containers/FloatingContainer.h"
#include "gui/containers/WorkspaceHeader.h"
#include "gui/styles/WorkspacePreview.h"

using namespace NereusSDR;

namespace {

// Longer than the manager's 200 ms geometry commit delay.
constexpr int kPastCommitDelayMs = 300;

void settle()
{
    for (int pass = 0; pass < 6; ++pass) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        QCoreApplication::processEvents(QEventLoop::AllEvents);
    }
}

void press(QWidget* target, const QPoint& global)
{
    QMouseEvent event(QEvent::MouseButtonPress, target->mapFromGlobal(global), global, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &event);
}

void move(QWidget* target, const QPoint& global)
{
    QMouseEvent event(QEvent::MouseMove, target->mapFromGlobal(global), global, Qt::NoButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(target, &event);
}

void release(QWidget* target, const QPoint& global)
{
    QMouseEvent event(QEvent::MouseButtonRelease, target->mapFromGlobal(global), global, Qt::LeftButton,
                      Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(target, &event);
}

// Makes QApplication::keyboardModifiers() report `modifiers`, as a held key
// does: a key event through the window system sets it, and the mouse events
// the cases send straight to a widget leave it alone.
bool holdModifiers(QWidget* window, Qt::Key key, Qt::KeyboardModifiers modifiers)
{
    QTest::keyEvent(modifiers == Qt::NoModifier ? QTest::Release : QTest::Press, window->windowHandle(), key,
                    modifiers);
    return QApplication::keyboardModifiers() == modifiers;
}

// QWidget::destroy() is protected.
class ReleasableMainWindow : public QMainWindow {
public:
    void releaseNativeWindow() { destroy(); }
};

// Offline fixture: a main window with a status bar whose central widget is
// the four-edge area around a plain centre pane, as in MainWindow. No
// radio, discovery or poller.
struct DragFixture {
    QTemporaryDir directory;
    AppSettings settings{directory.filePath("arrange-drag.xml")};
    ContainerWorkspaceStore store{settings};
    ContainerContentRegistry registry;
    ReleasableMainWindow window;
    QSplitter* middle{nullptr};
    QWidget* center{nullptr};
    EdgeDockArea* area{nullptr};
    std::unique_ptr<ContainerManager> manager;

    DragFixture()
    {
        middle = new EdgeDockSplitter(Qt::Horizontal, &window);
        center = new QWidget(middle);
        center->setMinimumWidth(400);
        middle->addWidget(center);
        area = new EdgeDockArea(middle, center, &window);
        window.setCentralWidget(area);
        // Part of the main window that is neither the pane nor an edge.
        window.statusBar()->setSizeGripEnabled(false);
        window.resize(1200, 800);
        manager = std::make_unique<ContainerManager>(center, middle);
        manager->setEdgeDockArea(area);
        manager->setWorkspaceAdapter(&store, &registry);
    }

    ContainerDocument container(const QString& id, DockMode mode, DockEdge edge = DockEdge::Right)
    {
        ContainerDocument result;
        result.id = id;
        result.name = id;
        result.layout = ContentLayout::VerticalStack;
        result.dockMode = mode;
        result.edge = edge;
        result.header = HeaderMode::Shown;
        if (mode == DockMode::OverlayDocked) {
            result.geometry = QRect(120, 100, 360, 300);
        } else if (mode == DockMode::Floating) {
            result.geometry = QRect(100, 300, 360, 300);
        }
        ContentEntry entry = registry.makeEntry(QStringLiteral("meter.signalMaxBin"));
        entry.id = id + QStringLiteral("-meter");
        entry.context["sliceId"] = 0;
        result.contents.append(entry);
        return result;
    }

    // The right strip's main container, a second strip container and one
    // container of `kind` named "source": "strip" (left strip), "free",
    // "floating" or "shell" (a pop-out shell).
    bool showWith(const QString& kind, bool arranging)
    {
        ContainerDocument source = container(QStringLiteral("source"),
            kind == "strip" ? DockMode::PanelDocked : kind == "free" ? DockMode::OverlayDocked : DockMode::Floating,
            DockEdge::Left);
        source.popOutShell = kind == "shell";
        WorkspaceDocument document = store.snapshot();
        document.containers = {container(QStringLiteral("main"), DockMode::PanelDocked),
                               container(QStringLiteral("other"), DockMode::PanelDocked), source};
        document.mainContainerId = QStringLiteral("main");
        if (manager->commitWorkspace(document, document.revision).status != CommitStatus::Saved) {
            return false;
        }
        window.show();
        if (!QTest::qWaitForWindowExposed(&window)) {
            return false;
        }
        settle();
        manager->setArrangeMode(arranging);
        settle();
        // Whatever the first layout has to save is saved before the case
        // counts commits.
        manager->saveState();
        QTest::qWait(kPastCommitDelayMs);
        settle();
        return manager->container(QStringLiteral("source")) != nullptr;
    }

    // Qt's offscreen plugin faults in QOffscreenBackingStore::clearHash
    // when a window that once painted a container is destroyed after the
    // window that painted it last (a test platform defect;
    // tst_workspace_edge_strips docks its container back for the same
    // reason). So a container that ends in a window of its own has the
    // main window give up its native window first.
    ~DragFixture()
    {
        if (manager && source() != nullptr && source()->isFloating()) {
            window.hide();
            window.releaseNativeWindow();
        }
    }

    ContainerWidget* source() const { return manager->container(QStringLiteral("source")); }
    QWidget* header() const { return source()->workspaceHeader(); }
    QPoint grabPoint() const { return header()->mapToGlobal(QPoint(header()->width() / 2, header()->height() / 2)); }
    // The source's top-left on the screen: its window's when it floats.
    QPoint sourceGlobalTopLeft() const
    {
        return source()->isFloating() ? source()->window()->pos() : source()->mapToGlobal(QPoint(0, 0));
    }
    QRect liveGeometry() const
    {
        return source()->isFloating() ? source()->window()->geometry() : source()->geometry();
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
    QStringList documentIds(DockEdge edge) const
    {
        QStringList ids;
        for (const auto& d : store.snapshot().containers) {
            if (d.dockMode == DockMode::PanelDocked && d.edge == edge) {
                ids.append(d.id);
            }
        }
        return ids;
    }

    QPoint paneGlobal(const QPoint& local) const { return center->mapToGlobal(local); }
    QPoint paneCentre() const { return paneGlobal(center->rect().center()); }
    QPoint leftZone() const { return area->mapToGlobal(QPoint(10, area->height() / 2)); }
    // Over the top of the right strip's first container: index 0 there.
    QPoint rightStripTop() const
    {
        ContainerWidget* main = manager->container(QStringLiteral("main"));
        return main->mapToGlobal(QPoint(main->width() / 2, 4));
    }
    QPoint outsideWindow() const { return window.mapToGlobal(QPoint(window.width() + 200, 120)); }
    QPoint overStatusBar() const
    {
        return window.statusBar()->mapToGlobal(window.statusBar()->rect().center());
    }

    // True when nothing but "source" and the order of the list changed.
    bool onlySourceChanged(const WorkspaceDocument& before) const
    {
        const WorkspaceDocument after = store.snapshot();
        if (after.arrangement != before.arrangement || after.containers.size() != before.containers.size()) {
            return false;
        }
        for (const auto& was : before.containers) {
            if (was.id != QStringLiteral("source") && document(was.id) != was) {
                return false;
            }
        }
        return true;
    }
};

} // namespace

class TstWorkspaceArrangeDrag : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { WorkspacePreview::setEnabledForTest(true); }
    void cleanupTestCase() { WorkspacePreview::setEnabledForTest(false); }
    void cleanup()
    {
        // No case leaves a held modifier behind.
        if (QApplication::keyboardModifiers() != Qt::NoModifier) {
            QWidget probe;
            probe.show();
            QVERIFY(QTest::qWaitForWindowExposed(&probe));
            QTest::keyEvent(QTest::Release, probe.windowHandle(), Qt::Key_Alt, Qt::NoModifier);
        }
    }

    // ---- Arranging: one rule for every kind of container -------------------

    // Arrange's outline lies over the whole container. In a window of its
    // own it painted the window's background over the header and the
    // content, so the window dragged while arranging was a blank box.
    void aWindowKeepsItsFaceWhileArranging()
    {
        QImage plain;
        QImage arranging;
        QRect band;
        for (bool on : {false, true}) {
            DragFixture f;
            QVERIFY(f.showWith(QStringLiteral("floating"), on));
            QVERIFY(f.header()->isVisible());
            QWidget* top = f.source()->window();
            // Inside the header, clear of the outline's own line.
            band = QRect(f.header()->mapTo(top, QPoint(0, 0)), f.header()->size()).adjusted(2, 2, -2, -2);
            (on ? arranging : plain) = top->grab().toImage();
        }
        const auto colours = [&band](const QImage& image) {
            QSet<QRgb> seen;
            const double dpr = image.devicePixelRatio();
            for (int y = band.top(); y <= band.bottom(); ++y) {
                for (int x = band.left(); x <= band.right(); ++x) {
                    seen.insert(image.pixel(qRound(x * dpr), qRound(y * dpr)));
                }
            }
            return seen.size();
        };
        // The header's name and buttons: many shades, not one fill.
        constexpr int kDrawnHeaderColours = 8;
        QVERIFY2(colours(plain) >= kDrawnHeaderColours, qPrintable(QString::number(colours(plain))));
        QVERIFY2(colours(arranging) >= kDrawnHeaderColours, qPrintable(QString::number(colours(arranging))));
    }

    void everyKindDocksInAnEdgeZone_data()
    {
        QTest::addColumn<QString>("kind");
        for (const char* kind : {"strip", "free", "floating", "shell"}) {
            QTest::newRow(kind) << QString::fromLatin1(kind);
        }
    }
    void everyKindDocksInAnEdgeZone()
    {
        QFETCH(QString, kind);
        DragFixture f;
        QVERIFY(f.showWith(kind, true));
        const WorkspaceDocument before = f.store.snapshot();
        const DockMode modeBefore = f.source()->dockMode();
        QWidget* parentBefore = f.source()->parentWidget();
        QSignalSpy commits(&f.store, &ContainerWorkspaceStore::committed);
        QWidget* header = f.header();
        const QPoint grab = f.grabPoint();
        // A strip source goes to the right strip, above its first
        // container; the others to the empty left edge.
        const QPoint drop = kind == "strip" ? f.rightStripTop() : f.leftZone();
        const auto target = f.area->dropTargetAt(drop);
        QVERIFY(target.valid);
        QCOMPARE(target.index, 0);

        press(header, grab);
        move(header, grab + QPoint(0, 30));
        // Over the pane there is no landing box; in the zone there is.
        move(header, f.paneCentre());
        QVERIFY(!f.area->dropPreviewVisible());
        move(header, drop);
        QVERIFY(f.area->dropPreviewVisible());
        move(header, f.paneCentre());
        QVERIFY(!f.area->dropPreviewVisible());
        move(header, drop);
        QVERIFY(f.area->dropPreviewVisible());
        // Nothing is docked or moved to another parent before the release.
        QCOMPARE(f.source()->dockMode(), modeBefore);
        QCOMPARE(f.source()->parentWidget(), parentBefore);
        QCOMPARE(commits.count(), 0);
        release(header, drop);
        settle();

        QVERIFY(!f.area->dropPreviewVisible());
        QVERIFY(!f.manager->containerDragActive());
        const ContainerDocument d = f.document("source");
        QCOMPARE(d.dockMode, DockMode::PanelDocked);
        QCOMPARE(d.edge, target.edge);
        QCOMPARE(f.documentIds(target.edge).indexOf("source"), target.index);
        QVERIFY(f.source()->isPanelDocked());
        QCOMPARE(commits.count(), 1);
        QVERIFY(f.onlySourceChanged(before));
        QVERIFY(f.manager->arrangeMode());
        // The place does not change again once the strips have settled.
        QTest::qWait(kPastCommitDelayMs);
        settle();
        QCOMPARE(f.document("source").dockMode, DockMode::PanelDocked);
        QCOMPARE(f.documentIds(target.edge).indexOf("source"), target.index);
        QCOMPARE(commits.count(), 1);
    }

    void everyKindSitsFreeOverThePane_data() { everyKindDocksInAnEdgeZone_data(); }
    void everyKindSitsFreeOverThePane()
    {
        QFETCH(QString, kind);
        DragFixture f;
        QVERIFY(f.showWith(kind, true));
        const WorkspaceDocument before = f.store.snapshot();
        QSignalSpy commits(&f.store, &ContainerWorkspaceStore::committed);
        QWidget* header = f.header();
        const QPoint grab = f.grabPoint();
        const QPoint offset = grab - f.sourceGlobalTopLeft();
        const QPoint drop = f.paneCentre() + QPoint(-37, 21);

        press(header, grab);
        move(header, grab + QPoint(0, 30));
        move(header, drop);
        QVERIFY(!f.area->dropPreviewVisible());
        release(header, drop);
        settle();

        const ContainerDocument d = f.document("source");
        QCOMPARE(d.dockMode, DockMode::OverlayDocked);
        // Where the pointer let go: the header is under it as it was grabbed.
        QCOMPARE(d.geometry.topLeft(), f.center->mapFromGlobal(drop - offset));
        QVERIFY(d.geometryPane.has_value());
        QCOMPARE(*d.geometryPane, f.center->size());
        QVERIFY(f.source()->isOverlayDocked());
        QCOMPARE(f.source()->parentWidget(), f.center);
        QCOMPARE(f.source()->geometry().topLeft(), d.geometry.topLeft());
        QCOMPARE(commits.count(), 1);
        // The committed place is the remembered one: nothing follows it.
        QTest::qWait(kPastCommitDelayMs);
        settle();
        QCOMPARE(commits.count(), 1);
        QCOMPARE(f.document("source"), d);
        QVERIFY(f.onlySourceChanged(before));
    }

    void everyKindPopsOutOutsideTheMainWindow_data() { everyKindDocksInAnEdgeZone_data(); }
    void everyKindPopsOutOutsideTheMainWindow()
    {
        QFETCH(QString, kind);
        DragFixture f;
        QVERIFY(f.showWith(kind, true));
        const WorkspaceDocument before = f.store.snapshot();
        QSignalSpy commits(&f.store, &ContainerWorkspaceStore::committed);
        QWidget* header = f.header();
        const QPoint grab = f.grabPoint();
        const QPoint offset = grab - f.sourceGlobalTopLeft();
        const QPoint drop = f.outsideWindow();

        press(header, grab);
        move(header, grab + QPoint(0, 30));
        move(header, drop);
        QVERIFY(!f.area->dropPreviewVisible());
        release(header, drop);
        settle();

        const ContainerDocument d = f.document("source");
        QCOMPARE(d.dockMode, DockMode::Floating);
        QCOMPARE(d.geometry.topLeft(), drop - offset);
        QVERIFY(!d.geometryPane.has_value());
        QVERIFY(f.source()->isFloating());
        QCOMPARE(commits.count(), 1);
        QTest::qWait(kPastCommitDelayMs);
        settle();
        QCOMPARE(commits.count(), 1);
        QCOMPARE(f.document("source"), d);
        QVERIFY(f.onlySourceChanged(before));
    }

    void aFreeContainerIsNotHeldInsideThePaneWhileItIsDragged()
    {
        DragFixture f;
        QVERIFY(f.showWith("free", true));
        QWidget* header = f.header();
        const QPoint grab = f.grabPoint();
        press(header, grab);
        move(header, grab + QPoint(0, 30));
        move(header, f.leftZone() - QPoint(600, 0));
        QVERIFY2(f.source()->x() < 0, qPrintable(QString::number(f.source()->x())));
        release(header, f.paneCentre());
        settle();
        // Released over the pane, it is wholly inside it.
        QVERIFY(f.center->rect().contains(f.source()->geometry()));
    }

    // ---- Cancelling --------------------------------------------------------

    void aCancelledDragRestoresTheStart_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::addColumn<QString>("how");
        for (const char* kind : {"strip", "free", "floating", "shell"}) {
            for (const char* how : {"escape", "lost grab", "status bar"}) {
                QTest::addRow("%s, %s", kind, how) << QString::fromLatin1(kind) << QString::fromLatin1(how);
            }
        }
    }
    void aCancelledDragRestoresTheStart()
    {
        QFETCH(QString, kind);
        QFETCH(QString, how);
        DragFixture f;
        ArrangeHoldFilter hold(f.manager.get(), nullptr);
        QVERIFY(f.showWith(kind, true));
        const WorkspaceDocument before = f.store.snapshot();
        const QRect geometry = f.liveGeometry();
        const DockMode mode = f.source()->dockMode();
        QWidget* parent = f.source()->parentWidget();
        QSignalSpy commits(&f.store, &ContainerWorkspaceStore::committed);
        QWidget* header = f.header();
        const QPoint grab = f.grabPoint();

        press(header, grab);
        move(header, grab + QPoint(0, 30));
        move(header, f.leftZone());
        QVERIFY(f.area->dropPreviewVisible());
        QVERIFY(f.manager->containerDragActive());
        if (how == "escape") {
            QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QApplication::sendEvent(f.source(), &escape);
        } else if (how == "lost grab") {
            QEvent ungrab(QEvent::UngrabMouse);
            QApplication::sendEvent(header, &ungrab);
        } else {
            move(header, f.overStatusBar());
            QVERIFY(!f.area->dropPreviewVisible());
            release(header, f.overStatusBar());
        }
        settle();
        QVERIFY(!f.area->dropPreviewVisible());
        QVERIFY(!f.manager->containerDragActive());
        // The button coming up afterwards changes nothing.
        if (how != "status bar") {
            move(header, f.paneCentre());
            release(header, f.paneCentre());
            settle();
        }

        QCOMPARE(f.source()->dockMode(), mode);
        QCOMPARE(f.source()->parentWidget(), parent);
        QCOMPARE(f.liveGeometry(), geometry);
        QTest::qWait(kPastCommitDelayMs);
        settle();
        QCOMPARE(commits.count(), 0);
        QCOMPARE(f.store.snapshot(), before);
        QCOMPARE(f.liveGeometry(), geometry);
        // One Escape cancels the drag; Arrange is as it was.
        QVERIFY(f.manager->arrangeMode());
    }

    void aPressAndReleaseWithNoMoveCommitsNothing_data() { everyKindDocksInAnEdgeZone_data(); }
    void aPressAndReleaseWithNoMoveCommitsNothing()
    {
        QFETCH(QString, kind);
        DragFixture f;
        QVERIFY(f.showWith(kind, true));
        const WorkspaceDocument before = f.store.snapshot();
        const QRect geometry = f.liveGeometry();
        QSignalSpy commits(&f.store, &ContainerWorkspaceStore::committed);
        const QPoint grab = f.grabPoint();
        press(f.header(), grab);
        release(f.header(), grab);
        settle();
        QTest::qWait(kPastCommitDelayMs);
        QCOMPARE(commits.count(), 0);
        QCOMPARE(f.store.snapshot(), before);
        QCOMPARE(f.liveGeometry(), geometry);
    }

    // ---- The grid ----------------------------------------------------------

    void aFreeReleaseLandsOnTheGridUnlessAltIsHeld_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::addColumn<bool>("alt");
        for (const char* kind : {"strip", "free", "floating"}) {
            QTest::addRow("%s", kind) << QString::fromLatin1(kind) << false;
            QTest::addRow("%s, Alt held", kind) << QString::fromLatin1(kind) << true;
        }
    }
    void aFreeReleaseLandsOnTheGridUnlessAltIsHeld()
    {
        QFETCH(QString, kind);
        QFETCH(bool, alt);
        DragFixture f;
        QVERIFY(f.showWith(kind, true));
        f.manager->setGridAlignment(true);
        settle();
        QVERIFY(f.manager->gridAlignment());
        QSignalSpy commits(&f.store, &ContainerWorkspaceStore::committed);
        QWidget* header = f.header();
        const QPoint grab = f.grabPoint();
        const QPoint offset = grab - f.sourceGlobalTopLeft();
        // A release whose exact place is on no multiple of 8.
        const QPoint exact(203, 133);
        const QPoint drop = f.paneGlobal(exact) + offset;
        QVERIFY(f.center->rect().contains(f.center->mapFromGlobal(drop)));

        press(header, grab);
        move(header, grab + QPoint(0, 30));
        move(header, drop);
        if (alt) {
            QVERIFY(holdModifiers(&f.window, Qt::Key_Alt, Qt::AltModifier));
        }
        release(header, drop);
        if (alt) {
            QVERIFY(holdModifiers(&f.window, Qt::Key_Alt, Qt::NoModifier));
        }
        settle();

        const ContainerDocument d = f.document("source");
        QCOMPARE(d.dockMode, DockMode::OverlayDocked);
        // Where it was let go, in the pane as it is now (a strip container
        // leaves its strip closed behind it).
        const QPoint landed = f.center->mapFromGlobal(drop - offset);
        QCOMPARE(landed.y(), exact.y());
        const QPoint onGrid = workspaceSnappedTopLeft(landed);
        QVERIFY(onGrid != landed);
        QCOMPARE(onGrid.x() % kWorkspaceSnapGridPx, 0);
        QCOMPARE(onGrid.y() % kWorkspaceSnapGridPx, 0);
        QCOMPARE(d.geometry.topLeft(), alt ? landed : onGrid);
        QCOMPARE(commits.count(), 1);
    }

    // ---- Ctrl no longer steps by 10 px in the look -------------------------

    void ctrlDoesNotStepADragOrAResizeInTheLook_data()
    {
        QTest::addColumn<bool>("arranging");
        QTest::newRow("not arranging") << false;
        QTest::newRow("arranging") << true;
    }
    void ctrlDoesNotStepADragOrAResizeInTheLook()
    {
        QFETCH(bool, arranging);
        DragFixture f;
        QVERIFY(f.showWith("free", arranging));
        QWidget* header = f.header();
        const QPoint start = f.source()->pos();
        QCOMPARE(start, QPoint(120, 100));
        const QPoint grab = f.grabPoint();
        QVERIFY(holdModifiers(&f.window, Qt::Key_Control, Qt::ControlModifier));
        press(header, grab);
        move(header, grab + QPoint(23, 17));
        const QPoint during = f.source()->pos();
        release(header, grab + QPoint(23, 17));
        settle();
        QCOMPARE(during, QPoint(143, 117));
        QCOMPARE(f.source()->pos(), QPoint(143, 117));

        if (arranging) {
            // The corner grip is there while arranging.
            auto* grip = f.source()->findChild<QWidget*>(QStringLiteral("containerResizeGrip"));
            QVERIFY(grip && grip->isVisible());
            const QSize size = f.source()->size();
            const QPoint corner = grip->mapToGlobal(grip->rect().center());
            press(grip, corner);
            move(grip, corner + QPoint(23, 17));
            const QSize resized = f.source()->size();
            release(grip, corner + QPoint(23, 17));
            settle();
            QCOMPARE(resized, size + QSize(23, 17));
        }
        QVERIFY(holdModifiers(&f.window, Qt::Key_Control, Qt::NoModifier));
    }

    void ctrlStillStepsByTenWithTheLookOff()
    {
        WorkspacePreview::setEnabledForTest(false);
        {
            QWidget parent;
            parent.resize(900, 700);
            auto* c = new ContainerWidget(&parent);
            QVERIFY(c->workspaceHeader() == nullptr);
            c->setDockMode(DockMode::OverlayDocked);
            c->setHeaderMode(HeaderMode::Shown);
            c->setGeometry(120, 100, 360, 300);
            parent.show();
            QVERIFY(QTest::qWaitForWindowExposed(&parent));
            settle();
            auto* bar = c->findChild<QWidget*>(QStringLiteral("containerTitleBar"));
            QVERIFY(bar && bar->isVisible());
            const QPoint grab = bar->mapToGlobal(QPoint(bar->width() / 2, bar->height() / 2));
            QVERIFY(holdModifiers(&parent, Qt::Key_Control, Qt::ControlModifier));
            press(bar, grab);
            move(bar, grab + QPoint(23, 17));
            release(bar, grab + QPoint(23, 17));
            QCOMPARE(c->pos(), QPoint(140, 120));

            auto* grip = c->findChild<QWidget*>(QStringLiteral("containerResizeGrip"));
            QVERIFY(grip);
            const QPoint corner = c->mapToGlobal(QPoint(c->width() - 4, c->height() - 4));
            press(grip, corner);
            move(grip, corner + QPoint(23, 17));
            release(grip, corner + QPoint(23, 17));
            QCOMPARE(c->size(), QSize(380, 320));
            QVERIFY(holdModifiers(&parent, Qt::Key_Control, Qt::NoModifier));
        }
        WorkspacePreview::setEnabledForTest(true);
    }

    // ---- Not arranging: as before ------------------------------------------

    void notArrangingAStripSourceDocksAtAnEdgeAndNowhereElse()
    {
        DragFixture f;
        QVERIFY(f.showWith("strip", false));
        QVERIFY(!f.manager->arranging());
        const WorkspaceDocument before = f.store.snapshot();
        QSignalSpy commits(&f.store, &ContainerWorkspaceStore::committed);
        QWidget* header = f.header();
        const QPoint grab = f.grabPoint();

        // Over the pane centre: nothing.
        press(header, grab);
        move(header, grab + QPoint(0, 30));
        move(header, f.paneCentre());
        QVERIFY(!f.area->dropPreviewVisible());
        release(header, f.paneCentre());
        settle();
        QTest::qWait(kPastCommitDelayMs);
        QCOMPARE(commits.count(), 0);
        QCOMPARE(f.store.snapshot(), before);

        // At an edge: docked there.
        const QPoint nearTop = f.area->mapToGlobal(QPoint(f.area->width() / 3, 12));
        press(header, grab);
        move(header, grab + QPoint(0, 30));
        move(header, nearTop);
        QVERIFY(f.area->dropPreviewVisible());
        release(header, nearTop);
        settle();
        QCOMPARE(f.document("source").dockMode, DockMode::PanelDocked);
        QCOMPARE(f.document("source").edge, DockEdge::Top);
    }

    void notArrangingAFreeSourceStaysInsideThePaneAndCannotDock()
    {
        DragFixture f;
        QVERIFY(f.showWith("free", false));
        QWidget* header = f.header();
        const QPoint grab = f.grabPoint();
        const QSize size = f.source()->size();
        press(header, grab);
        move(header, grab + QPoint(0, 30));
        move(header, f.leftZone() - QPoint(600, 0));
        QCOMPARE(f.source()->x(), 0);
        QVERIFY(!f.area->dropPreviewVisible());
        release(header, f.leftZone() - QPoint(600, 0));
        settle();
        QTest::qWait(kPastCommitDelayMs);
        settle();
        const ContainerDocument d = f.document("source");
        QCOMPARE(d.dockMode, DockMode::OverlayDocked);
        QCOMPARE(d.geometry.x(), 0);
        QCOMPARE(d.geometry.size(), size);
        QVERIFY(f.source()->isOverlayDocked());

        // Pulled out of the window it stays in the pane too.
        press(header, f.grabPoint());
        move(header, f.outsideWindow());
        release(header, f.outsideWindow());
        settle();
        QTest::qWait(kPastCommitDelayMs);
        settle();
        QCOMPARE(f.document("source").dockMode, DockMode::OverlayDocked);
        QVERIFY(f.center->rect().contains(f.source()->geometry()));
    }

    void notArrangingAWindowOnlyMoves_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::newRow("floating") << QStringLiteral("floating");
        QTest::newRow("shell") << QStringLiteral("shell");
    }
    void notArrangingAWindowOnlyMoves()
    {
        QFETCH(QString, kind);
        DragFixture f;
        QVERIFY(f.showWith(kind, false));
        QWidget* header = f.header();
        const QPoint grab = f.grabPoint();
        const QPoint start = f.source()->window()->pos();
        for (const QPoint& drop : {f.leftZone(), f.paneCentre()}) {
            const QPoint from = f.grabPoint();
            const QPoint was = f.source()->window()->pos();
            press(header, from);
            move(header, from + QPoint(0, 30));
            move(header, drop);
            QVERIFY(!f.area->dropPreviewVisible());
            release(header, drop);
            settle();
            QTest::qWait(kPastCommitDelayMs);
            settle();
            QVERIFY(f.source()->isFloating());
            QCOMPARE(f.document("source").dockMode, DockMode::Floating);
            QCOMPARE(f.source()->window()->pos(), was + (drop - from));
            QCOMPARE(f.document("source").geometry.topLeft(), was + (drop - from));
        }
        Q_UNUSED(grab);
        QVERIFY(f.source()->window()->pos() != start);
    }
};

QTEST_MAIN(TstWorkspaceArrangeDrag)
#include "tst_workspace_arrange_drag.moc"
