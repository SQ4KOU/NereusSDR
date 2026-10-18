// no-port-check: NereusSDR-original workspace preview header mode regressions.
// Modification history (NereusSDR):
//   2026-10-07 - Reveal, Arrange and header button checks for the workspace
//                 preview by J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//                 Claude Code.
//   2026-10-09 - In Arrange no header covers another header's buttons, in
//                 every header mode. J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
//   2026-10-11 - A header is shown or hidden: the hover cases are replaced
//                 by the eye button, "Keep Header Showing", the menus
//                 without Header or Reveal controls, and Arrange drawing
//                 every header without a commit. J.J. Boyd (KG4VCF),
//                 AI-assisted via Anthropic Claude Code.
//   2026-10-11 - With the look off the three header modes are unchanged:
//                 hover and focus show a Reveal header, the menu and the
//                 settings combo offer it, and a saved Reveal stays saved.
//                 With the look on a saved Reveal is saved once as Hidden
//                 and the combo has two choices. J.J. Boyd (KG4VCF),
//                 AI-assisted via Anthropic Claude Code.
#include <QtTest>
#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QGraphicsEffect>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTimer>
#include "core/AppSettings.h"
#include "gui/containers/ContainerContentHost.h"
#include "gui/containers/ContainerContentRegistry.h"
#include "gui/containers/ContainerDocumentCodec.h"
#include "gui/containers/ContainerManager.h"
#include "gui/containers/ContainerSettingsDialog.h"
#include "gui/containers/ContainerWidget.h"
#include "gui/containers/ContainerWorkspaceStore.h"
#include "gui/containers/WorkspaceHeader.h"
#include "gui/styles/WorkspacePreview.h"

using namespace NereusSDR;

// MinGW imports a static constexpr member of an exported class by address
// when it is bound to a reference (QCOMPARE does), and the GUI DLL never
// exports one, so this test did not link on Windows. It reads these
// copies by value instead.
namespace {
constexpr double kOverlayOpacity = NereusSDR::ContainerWidget::kOverlayOpacity;
} // namespace

namespace {

// Offline fixture: a store, a registry and a manager over a plain dock
// widget. No MainWindow, discovery, radio model or poller.
struct WorkspaceFixture {
    QTemporaryDir directory;
    AppSettings settings{directory.filePath("header-modes.xml")};
    ContainerWorkspaceStore store{settings};
    QWidget dock;
    QSplitter splitter;
    ContainerContentRegistry registry;
    ContainerManager manager{&dock, &splitter};

    WorkspaceFixture()
    {
        dock.resize(1200, 900);
        manager.setWorkspaceAdapter(&store, &registry);
    }
    ContainerDocument container(const QString& id, HeaderMode mode, int x)
    {
        ContainerDocument result;
        result.id = id;
        result.name = id;
        result.layout = ContentLayout::VerticalStack;
        result.dockMode = DockMode::OverlayDocked;
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
};

void settle()
{
    for (int pass = 0; pass < 6; ++pass) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        QCoreApplication::processEvents(QEventLoop::AllEvents);
    }
}

void hover(QWidget* target, const QPoint& position)
{
    QMouseEvent move(QEvent::MouseMove, position, target->mapToGlobal(position), Qt::NoButton,
                     Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(target, &move);
}

// Runs `open`, which blocks in a menu's exec, and hands the open menu to
// `visit` from inside that loop; the menu is closed afterwards. False when
// no menu opened.
template <typename Open, typename Visit>
bool withMenu(Open open, Visit visit)
{
    bool seen = false;
    QTimer::singleShot(0, [&seen, &visit] {
        if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
            seen = true;
            visit(menu);
            menu->close();
        }
    });
    // Never leave a menu open under a failed lookup.
    QTimer::singleShot(3000, [] {
        if (QWidget* popup = QApplication::activePopupWidget()) {
            popup->close();
        }
    });
    open();
    return seen;
}

QStringList actionTexts(const QMenu* menu)
{
    QStringList texts;
    for (const QAction* action : menu->actions()) {
        if (!action->isSeparator()) {
            texts.append(action->text());
        }
    }
    return texts;
}

QAction* actionNamed(const QMenu* menu, const QString& text)
{
    for (QAction* action : menu->actions()) {
        if (action->text() == text) {
            return action;
        }
    }
    return nullptr;
}

void openBodyMenu(ContainerWidget* c)
{
    const QPoint at(c->width() / 2, c->height() - 12);
    QContextMenuEvent event(QContextMenuEvent::Mouse, at, c->mapToGlobal(at));
    QApplication::sendEvent(c, &event);
}

const QString kKeepHeaderShowing = QStringLiteral("Keep Header Showing");
const QString kTipWhenShown = QStringLiteral("Hide this header when not arranging");
const QString kTipWhenHidden = QStringLiteral("Keep this header showing");

} // namespace

class TstWorkspaceHeaderModes : public QObject {
    Q_OBJECT
private slots:
    void init() { WorkspacePreview::setEnabledForTest(true); }
    void cleanupTestCase() { WorkspacePreview::setEnabledForTest(false); }

    void hiddenHeaderTakesNoRoomAndHoverShowsNothing()
    {
        QWidget parent;
        parent.resize(800, 600);
        auto* c = new ContainerWidget(&parent);
        QPushButton content("An ordinary control");
        c->setContent(&content);
        c->setDockMode(DockMode::OverlayDocked);
        c->setHeaderMode(HeaderMode::Shown);
        c->setGeometry(20, 20, 420, 260);
        parent.show();
        QVERIFY(QTest::qWaitForWindowExposed(&parent));
        settle();
        QVERIFY(c->workspaceHeader());
        QVERIFY(c->chromeVisible());
        QCOMPARE(c->workspaceHeader()->height(), WorkspaceGroupHeader::kHeight);
        QCOMPARE(c->headerExtent(), WorkspaceGroupHeader::kHeight);
        QVERIFY(c->workspaceHeader()->keepShown());
        QCOMPARE(c->workspaceHeader()->keepShownButton()->toolTip(), kTipWhenShown);
        const int shownTop = c->contentHolder()->mapTo(c, QPoint()).y();

        c->setHeaderMode(HeaderMode::Hidden);
        settle();
        QCOMPARE(c->headerExtent(), 0);
        QVERIFY(!c->chromeVisible());
        QVERIFY(!c->workspaceHeader()->keepShown());
        QCOMPARE(c->workspaceHeader()->keepShownButton()->toolTip(), kTipWhenHidden);
        const QPoint idle = content.mapTo(c, QPoint());
        const QSize idleSize = content.size();
        // Zero extent: the content area starts the header's height higher,
        // at the container's top edge.
        QCOMPARE(c->contentHolder()->mapTo(c, QPoint()).y(), shownTop - WorkspaceGroupHeader::kHeight);
        QCOMPARE(c->contentHolder()->mapTo(c, QPoint()).y(), 0);

        // Hovering the top of it for 2 s shows nothing.
        constexpr int kHoverMs = 2000;
        constexpr int kHoverStepMs = 250;
        for (int waited = 0; waited < kHoverMs; waited += kHoverStepMs) {
            hover(c, QPoint(40 + waited / kHoverStepMs, 6));
            hover(&content, content.mapFrom(c, QPoint(40 + waited / kHoverStepMs, 6)));
            QTest::qWait(kHoverStepMs);
            QVERIFY2(!c->chromeVisible(), qPrintable(QString::number(waited)));
        }
        // Keyboard focus shows nothing either.
        parent.activateWindow();
        content.setFocus(Qt::OtherFocusReason);
        c->setFocus(Qt::TabFocusReason);
        QTest::qWait(kHoverStepMs);
        QVERIFY(!c->chromeVisible());
        QCOMPARE(content.mapTo(c, QPoint()), idle);
        QCOMPARE(content.size(), idleSize);

        // Arranging draws it over the top of the content: nothing shifts.
        c->setArrangeActive(true);
        settle();
        QVERIFY(c->chromeVisible());
        QCOMPARE(c->headerExtent(), 0);
        // As an overlay it is slightly see-through, with a shadow under it.
        auto* overlayOpacity = qobject_cast<QGraphicsOpacityEffect*>(c->workspaceHeader()->graphicsEffect());
        QVERIFY(overlayOpacity && overlayOpacity->isEnabled());
        QCOMPARE(overlayOpacity->opacity(), kOverlayOpacity);
        QCOMPARE(c->workspaceHeader()->geometry(), QRect(0, 0, c->width(), WorkspaceGroupHeader::kHeight));
        QCOMPARE(content.mapTo(c, QPoint()), idle);
        QCOMPARE(content.size(), idleSize);
        QCOMPARE(c->headerMode(), HeaderMode::Hidden);
        c->setArrangeActive(false);
        settle();
        QVERIFY(!c->chromeVisible());
        QCOMPARE(content.mapTo(c, QPoint()), idle);

        // A minimised container keeps its header: it is the only handle.
        c->setMinimised(true);
        settle();
        QVERIFY(c->chromeVisible());
        QCOMPARE(c->headerExtent(), WorkspaceGroupHeader::kHeight);
        c->setMinimised(false);
        settle();
        QVERIFY(!c->chromeVisible());
        content.setParent(nullptr);
    }

    void arrangeOnThenOffRestoresEachMode()
    {
        WorkspaceFixture fixture;
        WorkspaceDocument document;
        document.containers.append(fixture.container("shown", HeaderMode::Shown, 10));
        document.containers.append(fixture.container("hidden", HeaderMode::Hidden, 400));
        document.containers.append(fixture.container("hidden2", HeaderMode::Hidden, 790));
        document.mainContainerId = "shown";
        const auto committed = fixture.manager.commitWorkspace(document, 0);
        QVERIFY2(committed.status == CommitStatus::Saved, qPrintable(committed.error));
        fixture.dock.show();
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.dock));
        settle();

        const QStringList ids{"shown", "hidden", "hidden2"};
        QHash<QString, bool> header;
        QHash<QString, bool> entries;
        QHash<QString, QPoint> contentTop;
        const auto entryHeaderShown = [&](const QString& id) {
            auto* entryHeader = fixture.manager.contentHost(id)->entryHeader(id + "-comp");
            return entryHeader && entryHeader->isVisible();
        };
        for (const QString& id : ids) {
            auto* c = fixture.manager.container(id);
            QVERIFY(c);
            header[id] = c->chromeVisible();
            entries[id] = entryHeaderShown(id);
            contentTop[id] = fixture.manager.contentHost(id)->mapTo(c, QPoint());
            QVERIFY(!c->arrangeActive());
        }
        QVERIFY(header["shown"]);
        QVERIFY(!header["hidden"]);
        QVERIFY(!header["hidden2"]);
        QVERIFY(entries["shown"]);
        QVERIFY(!entries["hidden"]);
        QVERIFY(!entries["hidden2"]);
        QCOMPARE(fixture.manager.container("hidden")->headerExtent(), 0);

        const quint64 revision = fixture.store.snapshot().revision;
        QSignalSpy commits(&fixture.store, &ContainerWorkspaceStore::committed);
        QSignalSpy changed(&fixture.manager, &ContainerManager::arrangeModeChanged);
        fixture.manager.setArrangeMode(true);
        settle();
        QCOMPARE(changed.count(), 1);
        for (const QString& id : ids) {
            auto* c = fixture.manager.container(id);
            QVERIFY2(c->chromeVisible(), qPrintable(id));
            QVERIFY2(c->workspaceHeader()->arrangeAccent(), qPrintable(id));
            QVERIFY2(entryHeaderShown(id), qPrintable(id));
            // Each eye shows its own container's choice.
            QCOMPARE(c->workspaceHeader()->keepShown(), id == QLatin1String("shown"));
            QVERIFY2(c->workspaceHeader()->keepShownButton()->isVisible(), qPrintable(id));
            // Arrange never reflows content.
            QCOMPARE(fixture.manager.contentHost(id)->mapTo(c, QPoint()), contentTop[id]);
            QVERIFY(c->findChild<QWidget*>("containerResizeGrip")->isVisible());
        }

        fixture.manager.setArrangeMode(false);
        settle();
        QCOMPARE(changed.count(), 2);
        for (const QString& id : ids) {
            auto* c = fixture.manager.container(id);
            QCOMPARE(c->chromeVisible(), header[id]);
            QCOMPARE(entryHeaderShown(id), entries[id]);
            QVERIFY(!c->workspaceHeader()->arrangeAccent());
            QVERIFY(!c->findChild<QWidget*>("containerResizeGrip")->isVisible());
            QCOMPARE(fixture.manager.contentHost(id)->mapTo(c, QPoint()), contentTop[id]);
            QCOMPARE(c->headerMode(), fixture.store.snapshot().containers[ids.indexOf(id)].header);
        }
        // Arranging is not saved: no commit happened.
        QCOMPARE(commits.count(), 0);
        QCOMPARE(fixture.store.snapshot().revision, revision);
        fixture.dock.hide();
    }

    // In Arrange every header shows. The group header overlays the content
    // when hidden; it must not cover an entry header's buttons (it covered
    // the first entry header's pop-out, hide and options).
    void arrangeHeadersNeverCoverEachOthersButtons()
    {
        WorkspaceFixture fixture;
        WorkspaceDocument document;
        document.containers.append(fixture.container("shown", HeaderMode::Shown, 10));
        document.containers.append(fixture.container("hidden", HeaderMode::Hidden, 400));
        document.mainContainerId = "shown";
        const auto committed = fixture.manager.commitWorkspace(document, 0);
        QVERIFY2(committed.status == CommitStatus::Saved, qPrintable(committed.error));
        fixture.dock.show();
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.dock));
        settle();
        fixture.manager.setArrangeMode(true);
        settle();

        for (const QString& id : {QStringLiteral("shown"), QStringLiteral("hidden")}) {
            auto* c = fixture.manager.container(id);
            QVERIFY(c);
            WorkspaceGroupHeader* group = c->workspaceHeader();
            QVERIFY2(group && group->isVisible(), qPrintable(id));
            const QRect groupRect = group->geometry();
            QList<QRect> entryRects;
            for (const QString& entry : {id + "-signalMaxBin", id + "-comp"}) {
                WorkspaceEntryHeader* header = fixture.manager.contentHost(id)->entryHeader(entry);
                QVERIFY2(header && header->isVisible(), qPrintable(entry));
                const QRect headerRect(header->mapTo(c, QPoint()), header->size());
                for (QPushButton* button : {header->popOutButton(), header->hideButton(), header->optionsButton()}) {
                    QVERIFY(button && button->isVisible());
                    const QRect r(button->mapTo(c, QPoint()), button->size());
                    QVERIFY2(!r.intersects(groupRect),
                             qPrintable(QStringLiteral("%1: the group header %2,%3 %4x%5 covers a button "
                                                       "of %6 at %7,%8 %9x%10")
                                            .arg(id).arg(groupRect.x()).arg(groupRect.y())
                                            .arg(groupRect.width()).arg(groupRect.height()).arg(entry)
                                            .arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height())));
                    for (const QRect& other : entryRects) {
                        QVERIFY2(!r.intersects(other), qPrintable(entry));
                    }
                }
                entryRects.append(headerRect);
            }
        }
        fixture.manager.setArrangeMode(false);
        settle();
        fixture.dock.hide();
    }

    // The eye and the body menu's "Keep Header Showing" are one toggle, and
    // each changes its own container's saved header only.
    void eyeAndMenuChangeOneContainerOnly()
    {
        WorkspaceFixture fixture;
        WorkspaceDocument document;
        document.containers.append(fixture.container("a", HeaderMode::Shown, 10));
        document.containers.append(fixture.container("b", HeaderMode::Shown, 400));
        document.containers.append(fixture.container("c", HeaderMode::Hidden, 790));
        document.mainContainerId = "a";
        const auto committed = fixture.manager.commitWorkspace(document, 0);
        QVERIFY2(committed.status == CommitStatus::Saved, qPrintable(committed.error));
        fixture.dock.show();
        QVERIFY(QTest::qWaitForWindowExposed(&fixture.dock));
        settle();
        const auto saved = [&fixture](const QString& id) {
            for (const ContainerDocument& d : fixture.store.snapshot().containers) {
                if (d.id == id) {
                    return d.header;
                }
            }
            return static_cast<HeaderMode>(-1);
        };
        const auto eye = [&fixture](const QString& id) {
            return fixture.manager.container(id)->workspaceHeader()->keepShownButton();
        };

        // The eye sits left of Pop out, at the size of its neighbours.
        WorkspaceGroupHeader* headerB = fixture.manager.container("b")->workspaceHeader();
        QVERIFY(eye("b")->isVisible());
        QVERIFY(eye("b")->isCheckable());
        QCOMPARE(eye("b")->size(), headerB->popOutButton()->size());
        QVERIFY(eye("b")->geometry().right() < headerB->popOutButton()->geometry().left());
        QCOMPARE(eye("b")->toolTip(), kTipWhenShown);

        // The eye on b hides b's header and nothing else.
        QTest::mouseClick(eye("b"), Qt::LeftButton);
        settle();
        QCOMPARE(saved("a"), HeaderMode::Shown);
        QCOMPARE(saved("b"), HeaderMode::Hidden);
        QCOMPARE(saved("c"), HeaderMode::Hidden);
        QVERIFY(fixture.manager.container("a")->chromeVisible());
        QVERIFY(!fixture.manager.container("b")->chromeVisible());
        QCOMPARE(fixture.manager.container("b")->headerExtent(), 0);
        QCOMPARE(eye("b")->toolTip(), kTipWhenHidden);
        QVERIFY(!eye("b")->isChecked());

        // While arranging, the eye on c (crossed out) keeps c's header.
        fixture.manager.setArrangeMode(true);
        settle();
        QVERIFY(eye("c")->isVisible());
        QVERIFY(!eye("c")->isChecked());
        QCOMPARE(eye("c")->toolTip(), kTipWhenHidden);
        QTest::mouseClick(eye("c"), Qt::LeftButton);
        settle();
        QCOMPARE(saved("a"), HeaderMode::Shown);
        QCOMPARE(saved("b"), HeaderMode::Hidden);
        QCOMPARE(saved("c"), HeaderMode::Shown);
        QVERIFY(eye("c")->isChecked());
        QCOMPARE(eye("c")->toolTip(), kTipWhenShown);
        fixture.manager.setArrangeMode(false);
        settle();
        QVERIFY(fixture.manager.container("c")->chromeVisible());
        QVERIFY(!fixture.manager.container("b")->chromeVisible());

        // The body menu: "Keep Header Showing" is checked exactly when the
        // header is shown, and there is no Reveal controls and no Header
        // submenu.
        for (const QString& id : {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}) {
            QStringList texts;
            bool found = false;
            bool checked = false;
            QVERIFY2(withMenu([&] { openBodyMenu(fixture.manager.container(id)); },
                              [&](QMenu* menu) {
                                  texts = actionTexts(menu);
                                  if (QAction* keep = actionNamed(menu, kKeepHeaderShowing)) {
                                      found = true;
                                      checked = keep->isCheckable() && keep->isChecked();
                                  }
                              }),
                     qPrintable(id));
            QVERIFY2(found, qPrintable(texts.join(QStringLiteral(" | "))));
            QCOMPARE(checked, saved(id) == HeaderMode::Shown);
            QVERIFY2(!texts.contains(QStringLiteral("Reveal controls")), qPrintable(texts.join(" | ")));
            QVERIFY2(!texts.contains(QStringLiteral("Header")), qPrintable(texts.join(" | ")));
        }

        // The menu item on b (hidden) shows b's header; on a it hides a's.
        QVERIFY(withMenu([&] { openBodyMenu(fixture.manager.container("b")); },
                         [](QMenu* menu) { actionNamed(menu, kKeepHeaderShowing)->trigger(); }));
        settle();
        QCOMPARE(saved("a"), HeaderMode::Shown);
        QCOMPARE(saved("b"), HeaderMode::Shown);
        QCOMPARE(saved("c"), HeaderMode::Shown);
        QVERIFY(fixture.manager.container("b")->chromeVisible());
        QVERIFY(eye("b")->isChecked());
        QVERIFY(withMenu([&] { openBodyMenu(fixture.manager.container("a")); },
                         [](QMenu* menu) { actionNamed(menu, kKeepHeaderShowing)->trigger(); }));
        settle();
        QCOMPARE(saved("a"), HeaderMode::Hidden);
        QCOMPARE(saved("b"), HeaderMode::Shown);
        QCOMPARE(saved("c"), HeaderMode::Shown);
        QVERIFY(!fixture.manager.container("a")->chromeVisible());

        // The Options menu has no Header submenu either.
        QStringList options;
        QVERIFY(withMenu([&] {
                             QTest::mouseClick(fixture.manager.container("b")->workspaceHeader()->optionsButton(),
                                               Qt::LeftButton);
                         },
                         [&options](QMenu* menu) { options = actionTexts(menu); }));
        QVERIFY2(options.contains(QStringLiteral("Arrange Workspace")), qPrintable(options.join(" | ")));
        QVERIFY2(!options.contains(QStringLiteral("Header")), qPrintable(options.join(" | ")));
        QVERIFY2(!options.contains(QStringLiteral("Reveal controls")), qPrintable(options.join(" | ")));
        fixture.dock.hide();
    }

    void headerButtonNeverStartsADrag()
    {
        QWidget parent;
        parent.resize(800, 600);
        auto* c = new ContainerWidget(&parent);
        QPushButton content("An ordinary control");
        c->setContent(&content);
        c->setDockMode(DockMode::OverlayDocked);
        c->setHeaderMode(HeaderMode::Shown);
        c->setGeometry(20, 20, 420, 260);
        parent.show();
        QVERIFY(QTest::qWaitForWindowExposed(&parent));
        settle();
        auto* header = c->workspaceHeader();
        QVERIFY(header && header->isVisible());
        QSignalSpy started(c, &ContainerWidget::geometryInteractionStarted);
        QSignalSpy floated(c, &ContainerWidget::floatRequested);
        QList<HeaderMode> headerMode;
        connect(c, &ContainerWidget::headerModeRequested, this,
                [&headerMode](HeaderMode mode) { headerMode.append(mode); });
        const QPoint position = c->pos();

        // A click on the pop-out button pops out; it never drags.
        QTest::mouseClick(header->popOutButton(), Qt::LeftButton);
        QCOMPARE(floated.count(), 1);
        QCOMPARE(started.count(), 0);
        QCOMPARE(c->pos(), position);

        // Press on the options button and pull away: no drag, no move.
        QPushButton* options = header->optionsButton();
        QTest::mousePress(options, Qt::LeftButton, {}, options->rect().center());
        QTest::mouseMove(options, options->rect().center() + QPoint(-60, 40));
        QTest::mouseRelease(options, Qt::LeftButton, {}, options->rect().center() + QPoint(-60, 40));
        QCOMPARE(started.count(), 0);
        QCOMPARE(c->pos(), position);

        // The same on the eye: no drag, no move, and no request either,
        // because the release was off the button.
        QAbstractButton* eye = header->keepShownButton();
        QVERIFY(eye && eye->isVisible());
        QTest::mousePress(eye, Qt::LeftButton, {}, eye->rect().center());
        QTest::mouseMove(eye, eye->rect().center() + QPoint(-60, 40));
        QTest::mouseRelease(eye, Qt::LeftButton, {}, eye->rect().center() + QPoint(-60, 40));
        QCOMPARE(started.count(), 0);
        QCOMPARE(c->pos(), position);
        QCOMPARE(headerMode.count(), 0);

        // A click on the eye asks for the other choice and never drags.
        // Nothing commits it here, so the eye still shows the saved one.
        QTest::mouseClick(eye, Qt::LeftButton);
        QCOMPARE(headerMode.count(), 1);
        QCOMPARE(headerMode.at(0), HeaderMode::Hidden);
        QCOMPARE(started.count(), 0);
        QCOMPARE(c->pos(), position);
        QCOMPARE(c->headerMode(), HeaderMode::Shown);
        QVERIFY(header->keepShown());
        QCOMPARE(eye->toolTip(), kTipWhenShown);

        // The header itself does drag.
        const QPoint grab(header->grip()->geometry().right() + 60, WorkspaceGroupHeader::kHeight / 2);
        QTest::mousePress(header, Qt::LeftButton, {}, grab);
        QCOMPARE(started.count(), 1);
        QTest::mouseRelease(header, Qt::LeftButton, {}, grab);
        content.setParent(nullptr);
    }

    // A look-on document that holds the look-off hover mode (an older
    // build's, or seeded from the look-off workspace) is saved once as
    // Hidden, and no commit puts it back.
    void aSavedRevealHeaderIsSavedOnceAsHiddenWithTheLookOn()
    {
        WorkspaceFixture f;
        WorkspaceDocument d = f.store.snapshot();
        d.mainContainerId = "a";
        d.containers = {f.container("a", HeaderMode::Reveal, 20), f.container("b", HeaderMode::Shown, 420)};
        // Straight into the store, as a load would find it.
        QCOMPARE(f.store.commit(d, d.revision).status, CommitStatus::Saved);
        settle();
        const quint64 revision = f.store.snapshot().revision;
        QCOMPARE(f.store.snapshot().containers[0].header, HeaderMode::Hidden);
        QCOMPARE(f.store.snapshot().containers[1].header, HeaderMode::Shown);
        QCOMPARE(f.manager.container("a")->headerMode(), HeaderMode::Hidden);
        f.manager.reconcileWorkspace(f.store.snapshot());
        settle();
        QCOMPARE(f.store.snapshot().revision, revision);

        WorkspaceDocument draft = f.store.snapshot();
        draft.containers[1].header = HeaderMode::Reveal;
        QCOMPARE(f.manager.commitWorkspace(draft, draft.revision).status, CommitStatus::Saved);
        settle();
        QCOMPARE(f.store.snapshot().containers[1].header, HeaderMode::Hidden);

        // Container Settings offers the two choices.
        ContainerSettingsDialog dialog(f.manager.container("a"), nullptr, &f.manager);
        dialog.show();
        settle();
        auto* combo = dialog.findChild<QComboBox*>("containerHeader");
        QVERIFY(combo);
        QCOMPARE(combo->count(), 2);
        QCOMPARE(combo->findData(int(HeaderMode::Reveal)), -1);
        QCOMPARE(combo->currentData().toInt(), int(HeaderMode::Hidden));
        dialog.reject();
    }

    // ---- look off: the three header modes are as they were ---------------

    void lookOffHoverAndFocusShowARevealHeader()
    {
        WorkspacePreview::setEnabledForTest(false);
        WorkspaceFixture f;
        f.dock.show();
        QVERIFY(QTest::qWaitForWindowExposed(&f.dock));
        WorkspaceDocument d = f.store.snapshot();
        d.mainContainerId = "a";
        d.containers = {f.container("a", HeaderMode::Reveal, 20), f.container("b", HeaderMode::Hidden, 420)};
        QCOMPARE(f.manager.commitWorkspace(d, d.revision).status, CommitStatus::Saved);
        settle();
        ContainerWidget* a = f.manager.container("a");
        ContainerWidget* b = f.manager.container("b");
        QVERIFY(a && b);
        QVERIFY(!a->workspaceHeader());
        QCOMPARE(a->headerMode(), HeaderMode::Reveal);
        // Start with the pointer away and focus outside both containers.
        QEvent away(QEvent::Leave);
        if (QWidget* focused = QApplication::focusWidget()) {
            focused->clearFocus();
        }
        QApplication::sendEvent(a, &away);
        settle();
        QVERIFY(!a->chromeVisible());
        QVERIFY(!b->chromeVisible());

        // The pointer over the container shows its header; leaving hides it.
        hover(a, QPoint(40, 60));
        QVERIFY(a->chromeVisible());
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(a, &leave);
        QVERIFY(!a->chromeVisible());
        // Hidden does not answer the pointer.
        hover(b, QPoint(40, 60));
        QVERIFY(!b->chromeVisible());
        QApplication::sendEvent(b, &leave);

        // Keyboard focus inside the container shows it too.
        f.dock.activateWindow();
        a->setFocusPolicy(Qt::StrongFocus);
        a->setFocus(Qt::TabFocusReason);
        settle();
        QCOMPARE(QApplication::focusWidget(), a);
        QVERIFY(a->chromeVisible());
        a->clearFocus();
        settle();
        QApplication::sendEvent(a, &leave);
        QVERIFY(!a->chromeVisible());

        // "Reveal controls" recovers a hidden header until the pointer leaves.
        QStringList body;
        QVERIFY(withMenu([&] { openBodyMenu(b); }, [&](QMenu* menu) {
            body = actionTexts(menu);
            if (QAction* reveal = actionNamed(menu, QStringLiteral("Reveal controls"))) {
                reveal->trigger();
            }
        }));
        QVERIFY2(body.contains(QStringLiteral("Reveal controls")), qPrintable(body.join('|')));
        QVERIFY2(body.contains(QStringLiteral("Header")), qPrintable(body.join('|')));
        QVERIFY2(!body.contains(kKeepHeaderShowing), qPrintable(body.join('|')));
        QVERIFY(b->chromeVisible());
        QApplication::sendEvent(b, &leave);
        QVERIFY(!b->chromeVisible());

        // The Header submenu has the three modes, the current one checked.
        QStringList modes;
        QString checked;
        QVERIFY(withMenu([&] { openBodyMenu(a); }, [&](QMenu* menu) {
            if (QAction* header = actionNamed(menu, QStringLiteral("Header")); header && header->menu()) {
                modes = actionTexts(header->menu());
                for (const QAction* action : header->menu()->actions()) {
                    if (action->isChecked()) {
                        checked = action->text();
                    }
                }
            }
        }));
        QCOMPARE(modes, QStringList({QStringLiteral("Always visible"), QStringLiteral("Reveal on hover/focus"),
                                     QStringLiteral("Hidden (hold Shift to recover)")}));
        QCOMPARE(checked, QStringLiteral("Reveal on hover/focus"));
    }

    void lookOffASavedRevealHeaderStaysSavedAndTheComboHasThreeChoices()
    {
        WorkspacePreview::setEnabledForTest(false);
        WorkspaceFixture f;
        WorkspaceDocument d = f.store.snapshot();
        d.mainContainerId = "a";
        d.containers = {f.container("a", HeaderMode::Reveal, 20), f.container("b", HeaderMode::Shown, 420)};
        QCOMPARE(f.store.commit(d, d.revision).status, CommitStatus::Saved);
        settle();
        const quint64 revision = f.store.snapshot().revision;
        f.manager.reconcileWorkspace(f.store.snapshot());
        settle();
        // Loaded as saved: nothing was committed for it.
        QCOMPARE(f.store.snapshot().revision, revision);
        QCOMPARE(f.store.snapshot().containers[0].header, HeaderMode::Reveal);
        QCOMPARE(f.manager.container("a")->headerMode(), HeaderMode::Reveal);

        // It round-trips through settings as "header": 1.
        ContainerWorkspaceStore reread(f.settings);
        QCOMPARE(reread.snapshot(), f.store.snapshot());
        const QByteArray saved = ContainerDocumentCodec::encode(reread.snapshot());
        QVERIFY2(saved.contains("\"header\":1"), saved.constData());

        // A commit keeps it.
        WorkspaceDocument draft = f.store.snapshot();
        draft.containers[1].header = HeaderMode::Reveal;
        QCOMPARE(f.manager.commitWorkspace(draft, draft.revision).status, CommitStatus::Saved);
        settle();
        QCOMPARE(f.store.snapshot().containers[1].header, HeaderMode::Reveal);

        ContainerSettingsDialog dialog(f.manager.container("a"), nullptr, &f.manager);
        dialog.show();
        settle();
        auto* combo = dialog.findChild<QComboBox*>("containerHeader");
        QVERIFY(combo);
        QCOMPARE(combo->count(), 3);
        QCOMPARE(combo->itemText(0), QStringLiteral("Always visible"));
        QCOMPARE(combo->itemText(1), QStringLiteral("Reveal on hover / focus"));
        QCOMPARE(combo->itemData(1).toInt(), int(HeaderMode::Reveal));
        QCOMPARE(combo->itemData(2).toInt(), int(HeaderMode::Hidden));
        QCOMPARE(combo->currentData().toInt(), int(HeaderMode::Reveal));
        dialog.reject();
    }
};

QTEST_MAIN(TstWorkspaceHeaderModes)
#include "tst_workspace_header_modes.moc"
