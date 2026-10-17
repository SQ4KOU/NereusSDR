// no-port-check: NereusSDR-original regressions for the workspace look having no Lock setting.
// Modification history (NereusSDR):
//   2026-10-11 - Written: with the workspace look on a saved locked
//                 container loads, runs and saves unlocked, and the look
//                 shows no Lock item, padlock or checkbox; with the look
//                 off Lock is unchanged. J.J. Boyd (KG4VCF), AI-assisted
//                 via Anthropic Claude Code.
#include <QtTest>
#include <QApplication>
#include <QCheckBox>
#include <QContextMenuEvent>
#include <QMainWindow>
#include <QMenu>
#include <QPushButton>
#include <QSignalSpy>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTimer>
#include <memory>
#include "core/AppSettings.h"
#include "gui/containers/ContainerContentRegistry.h"
#include "gui/containers/ContainerDocumentCodec.h"
#include "gui/containers/ContainerManager.h"
#include "gui/containers/ContainerSettingsDialog.h"
#include "gui/containers/ContainerWidget.h"
#include "gui/containers/ContainerWorkspaceStore.h"
#include "gui/containers/EdgeDockStrip.h"
#include "gui/containers/WorkspaceHeader.h"
#include "gui/styles/WorkspacePreview.h"

using namespace NereusSDR;

namespace {

const QString kLookKey = QStringLiteral("ContainerWorkspacePreview");
const QString kShippedKey = QStringLiteral("ContainerWorkspace");
const QString kLockedField = QStringLiteral("\"locked\"");

void settle()
{
    for (int pass = 0; pass < 6; ++pass) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        QCoreApplication::processEvents(QEventLoop::AllEvents);
    }
}

ContainerDocument container(ContainerContentRegistry& registry, const QString& id, DockEdge edge, bool locked)
{
    ContainerDocument result;
    result.id = id;
    result.name = id;
    result.layout = ContentLayout::VerticalStack;
    result.dockMode = DockMode::PanelDocked;
    result.edge = edge;
    result.header = HeaderMode::Shown;
    result.locked = locked;
    ContentEntry entry = registry.makeEntry(QStringLiteral("meter.signalMaxBin"));
    entry.id = id + QStringLiteral("-meter");
    entry.context["sliceId"] = 0;
    result.contents.append(entry);
    return result;
}

// Two locked containers ("a" in the left strip, "main" in the right) and
// an unlocked "b" under "a".
WorkspaceDocument lockedDocument(ContainerContentRegistry& registry)
{
    WorkspaceDocument document;
    document.revision = 4;
    document.mainContainerId = QStringLiteral("main");
    document.containers = {container(registry, QStringLiteral("a"), DockEdge::Left, true),
                           container(registry, QStringLiteral("b"), DockEdge::Left, false),
                           container(registry, QStringLiteral("main"), DockEdge::Right, true)};
    return document;
}

// Offline fixture: a main window whose central widget is the four-edge
// area around a plain centre pane, as in MainWindow. `key` is seeded with
// the locked document before the store reads it. No radio, discovery or
// poller.
struct LookFixture {
    QTemporaryDir directory;
    AppSettings settings{directory.filePath("no-lock.xml")};
    ContainerContentRegistry registry;
    std::unique_ptr<ContainerWorkspaceStore> store;
    QMainWindow window;
    QSplitter* middle{nullptr};
    QWidget* center{nullptr};
    EdgeDockArea* area{nullptr};
    std::unique_ptr<ContainerManager> manager;

    explicit LookFixture(const QString& key)
    {
        settings.setValue(key, QString::fromUtf8(ContainerDocumentCodec::encode(lockedDocument(registry))));
        store = std::make_unique<ContainerWorkspaceStore>(settings);
        middle = new EdgeDockSplitter(Qt::Horizontal, &window);
        center = new QWidget(middle);
        center->setMinimumWidth(400);
        middle->addWidget(center);
        area = new EdgeDockArea(middle, center, &window);
        window.setCentralWidget(area);
        window.resize(1200, 800);
        manager = std::make_unique<ContainerManager>(center, middle);
        manager->setEdgeDockArea(area);
        manager->setWorkspaceAdapter(store.get(), &registry);
        manager->reconcileWorkspace(store->snapshot());
        window.show();
        settle();
    }

    QString saved(const QString& key) const { return settings.value(key).toString(); }

    ContainerDocument document(const QString& id) const
    {
        for (const auto& d : store->snapshot().containers) {
            if (d.id == id) {
                return d;
            }
        }
        return {};
    }

    bool anyLocked() const
    {
        const auto containers = store->snapshot().containers;
        return std::any_of(containers.cbegin(), containers.cend(),
                           [](const ContainerDocument& d) { return d.locked; });
    }
};

void press(QWidget* target, const QPoint& global)
{
    const QPoint local = target->mapFromGlobal(global);
    QMouseEvent event(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &event);
}

void move(QWidget* target, const QPoint& global)
{
    const QPoint local = target->mapFromGlobal(global);
    QMouseEvent event(QEvent::MouseMove, local, global, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &event);
}

void release(QWidget* target, const QPoint& global)
{
    const QPoint local = target->mapFromGlobal(global);
    QMouseEvent event(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(target, &event);
}

void drag(QSplitterHandle* handle, const QPoint& by)
{
    const QPoint grab = handle->mapToGlobal(handle->rect().center());
    press(handle, grab);
    move(handle, grab + by);
    release(handle, grab + by);
    settle();
}

void collect(const QMenu* menu, QStringList& texts)
{
    for (const QAction* action : menu->actions()) {
        if (action->isSeparator()) {
            continue;
        }
        texts.append(action->text());
        if (action->menu() != nullptr) {
            collect(action->menu(), texts);
        }
    }
}

// The action texts (submenus included) of the menu `open` shows; the menu
// is closed as soon as it is up.
QStringList menuTexts(const std::function<void()>& open)
{
    QStringList texts;
    bool seen = false;
    QTimer poll;
    poll.setInterval(10);
    QObject::connect(&poll, &QTimer::timeout, &poll, [&] {
        if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
            if (!seen) {
                seen = true;
                collect(menu, texts);
            }
            menu->close();
        }
    });
    poll.start();
    open();
    poll.stop();
    return texts;
}

QCheckBox* lockCheck(const QWidget& dialog)
{
    for (auto* box : dialog.findChildren<QCheckBox*>()) {
        if (box->text() == QStringLiteral("Lock")) {
            return box;
        }
    }
    return nullptr;
}

} // namespace

class TstWorkspaceNoLock : public QObject {
    Q_OBJECT
private slots:
    void init() { WorkspacePreview::setEnabledForTest(true); }
    void cleanupTestCase() { WorkspacePreview::setEnabledForTest(false); }

    // ---- look on ----------------------------------------------------------

    void savedLockedContainersLoadUnlocked()
    {
        LookFixture f(kLookKey);
        QCOMPARE(f.manager->containerCount(), 3);
        for (const QString& id : {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("main")}) {
            QVERIFY2(!f.manager->container(id)->isLocked(), qPrintable(id));
            QVERIFY2(!f.document(id).locked, qPrintable(id));
        }
        // Nothing else about them changed.
        WorkspaceDocument expected = lockedDocument(f.registry);
        WorkspaceDocument now = f.store->snapshot();
        for (auto& d : expected.containers) {
            d.locked = false;
        }
        QCOMPARE(now.containers.size(), expected.containers.size());
        for (int i = 0; i < expected.containers.size(); ++i) {
            QCOMPARE(now.containers[i].id, expected.containers[i].id);
            QCOMPARE(now.containers[i].dockMode, expected.containers[i].dockMode);
            QCOMPARE(now.containers[i].edge, expected.containers[i].edge);
            QCOMPARE(now.containers[i].header, expected.containers[i].header);
            QCOMPARE(now.containers[i].contents, expected.containers[i].contents);
        }
        QVERIFY(!f.saved(kLookKey).contains(kLockedField));
    }

    // The fixture's seed is what the assertions above depend on: it holds
    // the field, set, for two containers.
    void theSeedIsLocked()
    {
        ContainerContentRegistry registry;
        const QString raw = QString::fromUtf8(ContainerDocumentCodec::encode(lockedDocument(registry)));
        QCOMPARE(raw.count(kLockedField), 2);
        const DocumentResult decoded = ContainerDocumentCodec::decode(raw.toUtf8());
        QVERIFY(decoded.ok);
        QVERIFY(decoded.document.containers[0].locked);
        QVERIFY(!decoded.document.containers[1].locked);
        QVERIFY(decoded.document.containers[2].locked);
    }

    void aCommitNeverSavesLock()
    {
        LookFixture f(kLookKey);
        WorkspaceDocument draft = f.store->snapshot();
        for (auto& d : draft.containers) {
            d.locked = true;
        }
        QCOMPARE(f.manager->commitWorkspace(draft, draft.revision).status, CommitStatus::Saved);
        settle();
        QVERIFY(!f.anyLocked());
        QVERIFY(!f.saved(kLookKey).contains(kLockedField));
        QVERIFY(!f.manager->container("a")->isLocked());
    }

    // The case tst_workspace_edge_strips used to forbid: the divider under
    // a container saved locked, and the handle beside its strip.
    void stripDividersBesideASavedLockedContainerMove()
    {
        LookFixture f(kLookKey);
        EdgeDockStrip* left = f.area->strip(DockEdge::Left);
        QVERIFY(left->isVisible());
        const QList<int> before = left->extents();
        drag(left->splitter()->handle(1), QPoint(0, 80));
        QVERIFY(left->extents() != before);
        const int width = left->width();
        drag(f.area->middleSplitter()->handle(1), QPoint(80, 0));
        QVERIFY(left->width() != width);
    }

    void popOutMoveToEdgeAndPlaceFreeSucceed()
    {
        LookFixture f(kLookKey);
        QSignalSpy refused(f.manager.get(), &ContainerManager::workspaceError);
        f.manager->moveContainerToEdge("a", DockEdge::Top);
        settle();
        QCOMPARE(f.document("a").edge, DockEdge::Top);
        QCOMPARE(f.document("a").dockMode, DockMode::PanelDocked);
        f.manager->placeContainerFree("a");
        settle();
        QCOMPARE(f.document("a").dockMode, DockMode::OverlayDocked);
        QCOMPARE(f.manager->container("a")->parentWidget(), f.center);
        // Pop Out, by the header's own button, on the other saved-locked one.
        WorkspaceGroupHeader* header = f.manager->container("main")->workspaceHeader();
        QVERIFY(header);
        QVERIFY(header->popOutButton()->isEnabled());
        QVERIFY(header->hideButton()->isEnabled());
        header->popOutButton()->click();
        settle();
        QCOMPARE(f.document("main").dockMode, DockMode::Floating);
        QCOMPARE(refused.count(), 0);
        QVERIFY(!f.anyLocked());
        QVERIFY(!f.saved(kLookKey).contains(kLockedField));
        // Docked back before teardown: Qt's offscreen backing store faults
        // on a window that took a moved view (as tst_workspace_edge_strips'
        // popOutFromAStripKeepsTheContainerSize does).
        f.manager->moveContainerToEdge("main", DockEdge::Right);
        settle();
        QCOMPARE(f.document("main").dockMode, DockMode::PanelDocked);
    }

    void noLockItemPadlockOrCheckbox()
    {
        LookFixture f(kLookKey);
        ContainerWidget* c = f.manager->container("a");
        WorkspaceGroupHeader* header = c->workspaceHeader();
        QVERIFY(header && header->isVisible());

        const QStringList options = menuTexts([&] { header->optionsButton()->click(); });
        QVERIFY2(options.contains(QStringLiteral("Move to Edge")), qPrintable(options.join('|')));
        QVERIFY2(options.contains(QStringLiteral("Pop Out")), qPrintable(options.join('|')));
        QVERIFY2(!options.contains(QStringLiteral("Lock")), qPrintable(options.join('|')));

        const QStringList body = menuTexts([&] {
            const QPoint at = c->rect().center();
            QContextMenuEvent event(QContextMenuEvent::Mouse, at, c->mapToGlobal(at));
            QApplication::sendEvent(c, &event);
        });
        QVERIFY2(body.contains(QStringLiteral("Arrange Workspace")), qPrintable(body.join('|')));
        QVERIFY2(!body.contains(QStringLiteral("Lock")), qPrintable(body.join('|')));

        // No padlock: nothing in the header is a lock glyph, and the widget
        // has no way left to ask for a lock.
        for (const QWidget* child : header->findChildren<QWidget*>()) {
            QVERIFY2(!QString::fromLatin1(child->metaObject()->className()).contains(QStringLiteral("Lock")),
                     child->metaObject()->className());
        }
        QCOMPARE(c->metaObject()->indexOfSignal("lockRequested(bool)"), -1);
        QCOMPARE(header->metaObject()->indexOfMethod("setLocked(bool)"), -1);

        ContainerSettingsDialog dialog(c, nullptr, f.manager.get());
        dialog.show();
        settle();
        QVERIFY(dialog.editSession());
        QVERIFY(lockCheck(dialog) == nullptr);
        dialog.reject();
    }

    // First use of the look: its document is seeded from the look-off one,
    // which may hold a locked container. It loads unlocked there and the
    // look-off document is left as it was.
    void theLookOffWorkspaceSeedsTheLookUnlockedAndIsNotChanged()
    {
        LookFixture f(kShippedKey);
        const QString shipped = f.saved(kShippedKey);
        QCOMPARE(shipped.count(kLockedField), 2);
        QCOMPARE(f.manager->containerCount(), 3);
        QVERIFY(!f.anyLocked());
        QVERIFY(!f.manager->container("a")->isLocked());
        QVERIFY(f.settings.contains(kLookKey));
        QVERIFY(!f.saved(kLookKey).contains(kLockedField));
        f.manager->moveContainerToEdge("a", DockEdge::Top);
        settle();
        QCOMPARE(f.document("a").edge, DockEdge::Top);
        QCOMPARE(f.saved(kShippedKey), shipped);
    }

    // ---- look off ---------------------------------------------------------

    void lookOffLockIsUnchanged()
    {
        WorkspacePreview::setEnabledForTest(false);
        QTemporaryDir directory;
        AppSettings settings(directory.filePath("lock-off.xml"));
        ContainerContentRegistry registry;
        settings.setValue(kShippedKey, QString::fromUtf8(ContainerDocumentCodec::encode(lockedDocument(registry))));
        ContainerWorkspaceStore store(settings);
        QWidget root;
        QSplitter splitter(&root);
        ContainerManager manager(&root, &splitter);
        manager.setWorkspaceAdapter(&store, &registry);
        manager.reconcileWorkspace(store.snapshot());
        settle();
        // Loaded as saved: nothing was committed for it.
        QCOMPARE(store.snapshot().revision, quint64(4));
        QVERIFY(store.snapshot().containers[0].locked);
        QVERIFY(manager.container("a")->isLocked());
        QVERIFY(!manager.container("b")->isLocked());
        QVERIFY(manager.container("main")->isLocked());

        // A locked container refuses to move.
        QSignalSpy refused(&manager, &ContainerManager::workspaceError);
        manager.moveContainerToEdge("a", DockEdge::Top);
        QCOMPARE(refused.count(), 1);
        QCOMPARE(refused.first().first().toString(), QStringLiteral("Arrangement is locked"));
        manager.floatContainer("a");
        QCOMPARE(refused.count(), 2);
        QCOMPARE(store.snapshot().containers[0].edge, DockEdge::Left);
        QCOMPARE(store.snapshot().containers[0].dockMode, DockMode::PanelDocked);

        // A commit keeps the setting, and it round-trips through settings.
        WorkspaceDocument draft = store.snapshot();
        draft.containers[1].locked = true;
        draft.containers[2].locked = false;
        QCOMPARE(manager.commitWorkspace(draft, draft.revision).status, CommitStatus::Saved);
        settle();
        QVERIFY(manager.container("b")->isLocked());
        QVERIFY(!manager.container("main")->isLocked());
        QCOMPARE(settings.value(kShippedKey).toString().count(kLockedField), 2);
        ContainerWorkspaceStore reread(settings);
        QCOMPARE(reread.snapshot(), store.snapshot());
        QVERIFY(reread.snapshot().containers[0].locked);
        QVERIFY(reread.snapshot().containers[1].locked);
        QVERIFY(!reread.snapshot().containers[2].locked);

        // Container Settings still has the checkbox, showing the setting.
        ContainerSettingsDialog dialog(manager.container("a"), nullptr, &manager);
        dialog.show();
        settle();
        QCheckBox* lock = lockCheck(dialog);
        QVERIFY(lock);
        QVERIFY(lock->isChecked());
        dialog.reject();
    }
};

QTEST_MAIN(TstWorkspaceNoLock)
#include "tst_workspace_no_lock.moc"
