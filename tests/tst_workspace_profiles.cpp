// no-port-check: NereusSDR-original workspace preview profile regressions.
// Modification history (NereusSDR):
//   2026-10-11 - Headers are Shown or Hidden; the fixtures that used the
//                 hover mode use Hidden. J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
//   2026-10-11 - suppressedMovesAndResizesCommitNothing reports its simulated
//                 moves as the operator's (rememberOperatorGeometry). J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-08 - Re-review: a container moved into a strip keeps the
//                 split, suppressed moves commit nothing, a failed restore
//                 is logged. J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
//   2026-10-08 - Named workspace profiles: switch and restore, last profile,
//                 import, stale revision, remembered profile and the rail's
//                 remove affordance, the collapsing rail dock, by J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
//   2026-10-08 - Fix round 1: the remove cross beside the letter, the
//                 dialog's pill badges and elided summaries. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-08 - Review fixes: strip thickness and extents are not carried
//                 across profiles, the preview keeps its own workspace key,
//                 a failed switch restores, a stale key reloads, import
//                 validates entries, an edge drag ends with its container,
//                 hidden strips drop deleted ids. J.J. Boyd (KG4VCF),
//                 AI-assisted via Anthropic Claude Code.
//   2026-10-08 - The open rail keeps clear of the readout on its left.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
#include <QtTest>
#include <QApplication>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QPushButton>
#include <QSignalSpy>
#include <QSplitter>
#include <QTemporaryDir>
#include <memory>
#include "core/AppSettings.h"
#include "gui/containers/ContainerContentRegistry.h"
#include "gui/containers/ContainerDocumentCodec.h"
#include "gui/containers/ContainerManager.h"
#include "gui/containers/ContainerWidget.h"
#include "gui/containers/ContainerWorkspaceStore.h"
#include "gui/containers/EdgeDockStrip.h"
#include "gui/containers/WorkspaceProfiles.h"
#include "gui/containers/WorkspaceProfilesDialog.h"
#include "gui/styles/WorkspacePreview.h"
#include "gui/widgets/ProfileRail.h"
#include "gui/widgets/ProfileRailDock.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

namespace {

void settle()
{
    for (int pass = 0; pass < 6; ++pass) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        QCoreApplication::processEvents(QEventLoop::AllEvents);
    }
}

// The arrangement of a workspace, independent of revision and geometry
// noise: id, mode, edge, header and visibility per container, plus every
// edge's strip thickness (0 is the edge default, and is listed too, so a
// thickness carried in from another profile shows).
QStringList arrangement(const WorkspaceDocument& doc)
{
    QStringList out;
    for (const auto& c : doc.containers) {
        out << QStringLiteral("%1 mode=%2 edge=%3 header=%4 visible=%5 locked=%6")
                   .arg(c.id)
                   .arg(int(c.dockMode))
                   .arg(int(c.edge))
                   .arg(int(c.header))
                   .arg(c.visible)
                   .arg(c.locked);
    }
    out.sort();
    for (DockEdge edge : {DockEdge::Left, DockEdge::Right, DockEdge::Top, DockEdge::Bottom}) {
        out << QStringLiteral("strip %1 thickness=%2").arg(int(edge)).arg(doc.arrangement.strips.value(edge).thickness);
    }
    return out;
}

// Offline fixture: a main window whose central widget is the four-edge
// area around a plain centre pane. No radio, discovery or poller.
struct ProfilesFixture {
    QTemporaryDir directory;
    QString path{directory.filePath(QStringLiteral("profiles.xml"))};
    AppSettings settings{path};
    ContainerWorkspaceStore store{settings};
    ContainerContentRegistry registry;
    QMainWindow window;
    QSplitter* middle{nullptr};
    QWidget* center{nullptr};
    EdgeDockArea* area{nullptr};
    std::unique_ptr<ContainerManager> manager;

    ProfilesFixture()
    {
        middle = new QSplitter(Qt::Horizontal, &window);
        center = new QWidget(middle);
        center->setMinimumWidth(400);
        middle->addWidget(center);
        area = new EdgeDockArea(middle, center, &window);
        window.setCentralWidget(area);
        window.resize(1200, 800);
        manager = std::make_unique<ContainerManager>(center, middle);
        manager->setEdgeDockArea(area);
        manager->setWorkspaceAdapter(&store, &registry);
        window.show();
        settle();
    }

    ContainerDocument container(const QString& id, DockEdge edge, HeaderMode header)
    {
        ContainerDocument result;
        result.id = id;
        result.name = id;
        result.layout = ContentLayout::VerticalStack;
        result.dockMode = DockMode::PanelDocked;
        result.edge = edge;
        result.header = header;
        ContentEntry entry = registry.makeEntry(QStringLiteral("meter.signalMaxBin"));
        entry.id = id + QStringLiteral("-meter");
        entry.context["sliceId"] = 0;
        result.contents.append(entry);
        return result;
    }

    bool commit(const QList<ContainerDocument>& containers)
    {
        WorkspaceDocument document = store.snapshot();
        document.containers = containers;
        document.mainContainerId = containers.first().id;
        const auto result = manager->commitWorkspace(document, document.revision);
        settle();
        return result.status == CommitStatus::Saved;
    }

    // Arrangement A: main on the right, meters on the left.
    bool commitA()
    {
        return commit({container(QStringLiteral("main"), DockEdge::Right, HeaderMode::Hidden),
                       container(QStringLiteral("meters"), DockEdge::Left, HeaderMode::Hidden)});
    }

    // Arrangement B: main on the right, meters on top, headers always,
    // and a hidden third container.
    bool commitB()
    {
        ContainerDocument extra = container(QStringLiteral("band"), DockEdge::Bottom, HeaderMode::Hidden);
        extra.visible = false;
        return commit({container(QStringLiteral("main"), DockEdge::Right, HeaderMode::Shown),
                       container(QStringLiteral("meters"), DockEdge::Top, HeaderMode::Shown), extra});
    }
};

} // namespace

class TestWorkspaceProfiles : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { WorkspacePreview::setEnabledForTest(true); }

    void createFromCurrentSwitchAndBackRestoresArrangement()
    {
        ProfilesFixture f;
        QVERIFY(f.commitA());

        SliceModel slice(0);
        slice.setFrequency(7185000.0);
        slice.setDspMode(DSPMode::LSB);
        slice.setFilter(-2850, -150);
        QSignalSpy frequencySpy(&slice, &SliceModel::frequencyChanged);
        QSignalSpy modeSpy(&slice, &SliceModel::dspModeChanged);
        QSignalSpy filterSpy(&slice, &SliceModel::filterChanged);

        WorkspaceProfiles profiles(f.settings, f.manager.get());
        QVERIFY(profiles.load().ok());
        QCOMPARE(profiles.ids().size(), 1);
        const QString first = profiles.activeId();
        // A as captured into "Default" (the capture records the strips).
        const QStringList a = arrangement(f.store.snapshot());
        QCOMPARE(arrangement(profiles.profile(first)->workspace), a);

        const auto created = profiles.createFromCurrent(QStringLiteral("Contest"));
        QVERIFY2(created.ok(), qPrintable(created.error));
        QCOMPARE(profiles.activeId(), created.id);
        QVERIFY(created.id != first);

        // Rearrange live, then switch away: the switch flushes B into
        // "Contest" and applies A.
        QVERIFY(f.commitB());
        const QStringList b = arrangement(f.store.snapshot());
        QVERIFY(a != b);

        auto r = profiles.switchTo(first);
        QVERIFY2(r.ok(), qPrintable(r.error));
        settle();
        QCOMPARE(arrangement(f.store.snapshot()), a);
        QCOMPARE(profiles.activeId(), first);

        r = profiles.switchTo(created.id);
        QVERIFY2(r.ok(), qPrintable(r.error));
        settle();
        QCOMPARE(arrangement(f.store.snapshot()), b);

        // Arrangement only: the slice never moved.
        QCOMPARE(slice.frequency(), 7185000.0);
        QCOMPARE(slice.dspMode(), DSPMode::LSB);
        QCOMPARE(slice.filterLow(), -2850);
        QCOMPARE(slice.filterHigh(), -150);
        QCOMPARE(frequencySpy.count(), 0);
        QCOMPARE(modeSpy.count(), 0);
        QCOMPARE(filterSpy.count(), 0);
    }

    void stripSizesStayWithTheirProfile()
    {
        // Two left containers; one profile drags the strip and its split,
        // the other has neither saved.
        ProfilesFixture f;
        QVERIFY(f.commit({f.container(QStringLiteral("main"), DockEdge::Right, HeaderMode::Shown),
                          f.container(QStringLiteral("one"), DockEdge::Left, HeaderMode::Shown),
                          f.container(QStringLiteral("two"), DockEdge::Left, HeaderMode::Shown)}));
        EdgeDockStrip* left = f.area->strip(DockEdge::Left);
        const int defaultThickness = f.area->thickness(DockEdge::Left);
        QVERIFY(!f.area->hasThickness(DockEdge::Left));
        WorkspaceProfiles profiles(f.settings, f.manager.get());
        QVERIFY(profiles.load().ok());
        const QString wide = profiles.activeId();
        const auto plainCopy = profiles.duplicate(wide, QStringLiteral("Plain"));
        QVERIFY(plainCopy.ok());
        // "Plain" saves no strip sizes at all.
        WorkspaceProfilesState state = profiles.state();
        for (auto& p : state.profiles) {
            if (p.id == plainCopy.id) {
                p.workspace.arrangement.strips.clear();
            }
        }
        QVERIFY(profiles.commit(state, profiles.revision()).ok());

        // Drag the left strip and its split in the active profile.
        f.area->setThickness(DockEdge::Left, 400);
        const QList<int> before = left->extents();
        QCOMPARE(before.size(), 2);
        const int along = before[0] + before[1];
        left->setExtents({along / 4, along - along / 4});
        settle();
        const QList<int> dragged = left->extents();
        QVERIFY(dragged != before);

        QVERIFY(profiles.switchTo(plainCopy.id).ok());
        settle();
        QVERIFY(!f.area->hasThickness(DockEdge::Left));
        QCOMPARE(f.area->thickness(DockEdge::Left), defaultThickness);
        QVERIFY(left->extents() != dragged);
        QVERIFY(qAbs(left->extents()[0] - left->extents()[1]) <= 1);

        QVERIFY(profiles.switchTo(wide).ok());
        settle();
        QCOMPARE(f.area->thickness(DockEdge::Left), 400);
        QCOMPARE(left->extents(), dragged);

        // "Plain" kept its default: nothing from "Wide" was saved into it.
        const WorkspaceDocument& plain = profiles.profile(plainCopy.id)->workspace;
        QCOMPARE(plain.arrangement.strips.value(DockEdge::Left).thickness, 0);
        const auto savedExtents = plain.arrangement.strips.value(DockEdge::Left).extents;
        QVERIFY(savedExtents.value(QStringLiteral("one")) != dragged[0]);
    }

    void containerMovedInKeepsTheSplit()
    {
        // A dragged split survives a container moved into its strip; only
        // the newcomer takes a default share.
        ProfilesFixture f;
        QVERIFY(f.commit({f.container(QStringLiteral("main"), DockEdge::Right, HeaderMode::Shown),
                          f.container(QStringLiteral("one"), DockEdge::Left, HeaderMode::Shown),
                          f.container(QStringLiteral("two"), DockEdge::Left, HeaderMode::Shown),
                          f.container(QStringLiteral("three"), DockEdge::Right, HeaderMode::Shown)}));
        EdgeDockStrip* left = f.area->strip(DockEdge::Left);
        const QList<int> before = left->extents();
        QCOMPARE(before.size(), 2);
        const int along = before[0] + before[1];
        left->setExtents({along / 4, along - along / 4});
        settle();
        const QList<int> dragged = left->extents();
        const double ratio = double(dragged[0]) / double(dragged[1]);
        QVERIFY(ratio < 0.5);
        f.manager->saveState();

        f.manager->moveContainerToEdge(QStringLiteral("three"), DockEdge::Left);
        settle();
        const QList<int> after = left->extents();
        QCOMPARE(after.size(), 3);
        QVERIFY(after[2] > 0);
        QVERIFY2(qAbs(double(after[0]) / double(after[1]) - ratio) < 0.05,
                 qPrintable(QStringLiteral("%1:%2 after, %3:%4 dragged")
                                .arg(after[0]).arg(after[1]).arg(dragged[0]).arg(dragged[1])));
    }

    void suppressedMovesAndResizesCommitNothing()
    {
        ProfilesFixture f;
        ContainerDocument floating = f.container(QStringLiteral("meters"), DockEdge::Left, HeaderMode::Hidden);
        floating.dockMode = DockMode::Floating;
        floating.geometry = QRect(100, 100, 300, 200);
        QVERIFY(f.commit({f.container(QStringLiteral("main"), DockEdge::Right, HeaderMode::Hidden), floating}));
        QWidget* form = f.manager->container(QStringLiteral("meters"))->window();
        QVERIFY(form && form != &f.window);
        const quint64 revision = f.store.snapshot().revision;

        // Suppressed: the move and resize leave the store alone, before and
        // after the lift.
        f.manager->setGeometryCaptureSuppressed(true);
        form->move(form->pos() + QPoint(40, 30));
        form->resize(form->size() + QSize(50, 40));
        // Reported as the operator's, as a drag or grip resize would be.
        f.manager->rememberOperatorGeometry(QStringLiteral("meters"));
        settle();
        QTest::qWait(350);
        QCOMPARE(f.store.snapshot().revision, revision);
        f.manager->setGeometryCaptureSuppressed(false);
        QTest::qWait(350);
        QCOMPARE(f.store.snapshot().revision, revision);

        // The same edit unsuppressed is committed, so the test can see one.
        form->resize(form->size() + QSize(50, 40));
        f.manager->rememberOperatorGeometry(QStringLiteral("meters"));
        QTRY_VERIFY_WITH_TIMEOUT(f.store.snapshot().revision > revision, 2000);
    }

    void suppressionLiftsWhenProfilesGoFirst()
    {
        ProfilesFixture f;
        QVERIFY(f.commitA());
        {
            WorkspaceProfiles profiles(f.settings, f.manager.get());
            QVERIFY(profiles.load().ok());
            const QString first = profiles.activeId();
            const auto second = profiles.createFromCurrent(QStringLiteral("Second"));
            QVERIFY(second.ok());
            QVERIFY(f.commitB());
            QVERIFY(profiles.switchTo(first).ok());
            QVERIFY(f.manager->geometryCaptureSuppressed());
        }
        settle();
        QVERIFY(!f.manager->geometryCaptureSuppressed());
    }

    void failedRestoreIsLogged()
    {
        ProfilesFixture f;
        QVERIFY(f.commitA());
        WorkspaceProfiles profiles(f.settings, f.manager.get());
        QVERIFY(profiles.load().ok());
        const QString first = profiles.activeId();
        const auto second = profiles.createFromCurrent(QStringLiteral("Second"));
        QVERIFY(second.ok());
        QVERIFY(f.commitB());
        settle();

        // Another writer moves the profiles key, so the switch's commit is
        // refused; once the switch has applied, another writer also moves
        // the workspace key, so the restore is refused too.
        WorkspaceProfiles other(f.settings, f.manager.get());
        QVERIFY(other.load().ok());
        QVERIFY(other.rename(first, QStringLiteral("Elsewhere")).ok());
        const QString key = f.store.workspaceKey();
        bool moved = false;
        connect(&f.store, &ContainerWorkspaceStore::committed, &f.store, [&] {
            // The switch's own apply (capture suppressed), not the flush
            // of the live arrangement before it.
            if (!moved && f.manager->geometryCaptureSuppressed()) {
                moved = true;
                WorkspaceDocument elsewhere = f.store.snapshot();
                elsewhere.revision += 10;
                f.settings.setValue(key, QString::fromUtf8(ContainerDocumentCodec::encode(elsewhere)));
            }
        });
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("could not restore")));
        const auto refused = profiles.switchTo(first);
        QCOMPARE(refused.status, WorkspaceProfiles::Status::Stale);
        QVERIFY(moved);
        settle();
    }

    void previewNeverWritesTheShippedWorkspace()
    {
        ProfilesFixture f;
        QCOMPARE(f.store.workspaceKey(), QStringLiteral("ContainerWorkspacePreview"));
        // A shipped workspace on disk, as a normal launch saved it.
        WorkspaceDocument shipped;
        shipped.containers = {f.container(QStringLiteral("main"), DockEdge::Right, HeaderMode::Shown),
                              f.container(QStringLiteral("meters"), DockEdge::Left, HeaderMode::Shown)};
        shipped.mainContainerId = QStringLiteral("main");
        shipped.revision = 7;
        f.settings.setValue(QStringLiteral("ContainerWorkspace"),
                            QString::fromUtf8(ContainerDocumentCodec::encode(shipped)));
        QVERIFY(f.settings.save());
        const QVariant shippedBytes = f.settings.value(QStringLiteral("ContainerWorkspace"));
        QVERIFY(!f.settings.contains(QStringLiteral("ContainerWorkspacePreview")));

        // The preview is seeded from it.
        QVERIFY(f.store.load().ok);
        QCOMPARE(f.store.snapshot(), shipped);
        f.manager->reconcileWorkspace(f.store.snapshot());
        settle();

        // Preview operations: an Empty profile, then a container to the left.
        WorkspaceProfiles profiles(f.settings, f.manager.get());
        QVERIFY(profiles.load().ok());
        const QString first = profiles.activeId();
        QVERIFY(profiles.createEmpty(QStringLiteral("Empty")).ok());
        QVERIFY(profiles.switchTo(first).ok());
        settle();
        f.manager->moveContainerToEdge(QStringLiteral("main"), DockEdge::Left);
        settle();
        f.manager->saveState();

        QVERIFY(f.settings.contains(QStringLiteral("ContainerWorkspacePreview")));
        QCOMPARE(f.settings.value(QStringLiteral("ContainerWorkspace")), shippedBytes);
        QVERIFY(!f.settings.contains(QStringLiteral("ContainerWorkspaceBackup")));
        AppSettings reread(f.path);
        reread.load();
        QCOMPARE(reread.value(QStringLiteral("ContainerWorkspace")), shippedBytes);

        // With the switch off the store is on the shipped key.
        WorkspacePreview::setEnabledForTest(false);
        const auto restore = qScopeGuard([] { WorkspacePreview::setEnabledForTest(true); });
        ContainerWorkspaceStore shippedStore(reread);
        QCOMPARE(shippedStore.workspaceKey(), QStringLiteral("ContainerWorkspace"));
        QCOMPARE(shippedStore.snapshot(), shipped);
    }

    void failedSwitchRestoresAndSettlesBeforeCapture()
    {
        ProfilesFixture f;
        QVERIFY(f.commitA());
        WorkspaceProfiles profiles(f.settings, f.manager.get());
        QVERIFY(profiles.load().ok());
        const QString first = profiles.activeId();
        const auto second = profiles.createFromCurrent(QStringLiteral("Second"));
        QVERIFY(second.ok());
        QVERIFY(f.commitB());
        const QStringList b = arrangement(f.store.snapshot());

        // A good switch keeps capture suppressed until its queued lift.
        QVERIFY(profiles.switchTo(first).ok());
        QVERIFY(f.manager->geometryCaptureSuppressed());
        settle();
        QVERIFY(!f.manager->geometryCaptureSuppressed());
        QVERIFY(profiles.switchTo(second.id).ok());
        settle();
        QCOMPARE(arrangement(f.store.snapshot()), b);

        // Another writer moves the key: the switch applies, its commit is
        // refused, and the arrangement that was live is put back.
        WorkspaceProfiles other(f.settings, f.manager.get());
        QVERIFY(other.load().ok());
        QVERIFY(other.rename(first, QStringLiteral("Elsewhere")).ok());
        const auto refused = profiles.switchTo(first);
        QCOMPARE(refused.status, WorkspaceProfiles::Status::Stale);
        settle();
        QCOMPARE(arrangement(f.store.snapshot()), b);
        QCOMPARE(profiles.activeId(), second.id);
        QCOMPARE(profiles.name(first), QStringLiteral("Elsewhere"));
    }

    void importValidatesEveryEntry()
    {
        ProfilesFixture f;
        QVERIFY(f.commitA());
        WorkspaceProfiles profiles(f.settings, f.manager.get());
        QVERIFY(profiles.load().ok());
        const QByteArray good = profiles.exportProfile(profiles.activeId());
        QJsonObject root = QJsonDocument::fromJson(good).object();
        QJsonObject profile = root.value(QStringLiteral("profile")).toObject();
        WorkspaceDocument workspace = ContainerDocumentCodec::decode(
            QJsonDocument(profile.value(QStringLiteral("workspace")).toObject()).toJson(QJsonDocument::Compact)).document;
        ContentEntry broken = f.registry.makeEntry(QStringLiteral("meter.mic"));
        broken.config[QStringLiteral("legacyRecord")] = QStringLiteral("retained future or damaged record");
        QVERIFY(!f.registry.validateEntry(broken).isEmpty());
        workspace.containers.first().contents.append(broken);
        profile.insert(QStringLiteral("workspace"),
                       QJsonDocument::fromJson(ContainerDocumentCodec::encode(workspace)).object());
        root.insert(QStringLiteral("profile"), profile);
        const QByteArray bad = QJsonDocument(root).toJson();

        const quint64 revision = profiles.revision();
        const auto r = profiles.importProfile(bad);
        QCOMPARE(r.status, WorkspaceProfiles::Status::Invalid);
        QCOMPARE(profiles.ids().size(), 1);
        QCOMPARE(profiles.revision(), revision);
        QVERIFY(profiles.importProfile(good).ok());
    }

    void edgeDragEndsWithItsContainer()
    {
        ProfilesFixture f;
        QVERIFY(f.commitA());
        // The meters container is dragged, then removed by another commit.
        ContainerWidget* meters = f.manager->container(QStringLiteral("meters"));
        QVERIFY(meters);
        emit meters->edgeDragStarted();
        QVERIFY(f.area->targetBandsVisible());
        QVERIFY(f.commit({f.container(QStringLiteral("main"), DockEdge::Right, HeaderMode::Hidden)}));
        QVERIFY(!f.area->targetBandsVisible());

        // Dragged, then floated: the drag ends too.
        QVERIFY(f.commitA());
        meters = f.manager->container(QStringLiteral("meters"));
        emit meters->edgeDragStarted();
        QVERIFY(f.area->targetBandsVisible());
        ContainerDocument floating = f.container(QStringLiteral("meters"), DockEdge::Left, HeaderMode::Hidden);
        floating.dockMode = DockMode::Floating;
        floating.geometry = QRect(100, 100, 300, 200);
        QVERIFY(f.commit({f.container(QStringLiteral("main"), DockEdge::Right, HeaderMode::Hidden), floating}));
        QVERIFY(!f.area->targetBandsVisible());
        // A late release from the old drag does nothing.
        emit meters->edgeDragFinished(f.area->mapToGlobal(QPoint(2, 100)));
        QCOMPARE(f.store.snapshot().containers.at(1).dockMode, DockMode::Floating);
    }

    void hiddenStripDropsDeletedIds()
    {
        ProfilesFixture f;
        QVERIFY(f.commit({f.container(QStringLiteral("main"), DockEdge::Right, HeaderMode::Shown),
                          f.container(QStringLiteral("one"), DockEdge::Left, HeaderMode::Shown),
                          f.container(QStringLiteral("two"), DockEdge::Left, HeaderMode::Shown)}));
        f.manager->saveState();
        QVERIFY(f.store.snapshot().arrangement.strips.value(DockEdge::Left).extents.contains(QStringLiteral("two")));

        // Hide the left strip's containers, then delete one of them.
        ContainerDocument one = f.container(QStringLiteral("one"), DockEdge::Left, HeaderMode::Shown);
        one.visible = false;
        ContainerDocument two = f.container(QStringLiteral("two"), DockEdge::Left, HeaderMode::Shown);
        two.visible = false;
        WorkspaceDocument document = f.store.snapshot();
        document.containers = {f.container(QStringLiteral("main"), DockEdge::Right, HeaderMode::Shown), one, two};
        QVERIFY(f.manager->commitWorkspace(document, document.revision).status == CommitStatus::Saved);
        settle();
        QVERIFY(!f.area->strip(DockEdge::Left)->isVisible());
        document = f.store.snapshot();
        document.containers.removeLast();
        QVERIFY(f.manager->commitWorkspace(document, document.revision).status == CommitStatus::Saved);
        settle();
        QVERIFY(f.store.snapshot().arrangement.strips.value(DockEdge::Left).extents.contains(QStringLiteral("two")));

        f.manager->saveState();
        const auto extents = f.store.snapshot().arrangement.strips.value(DockEdge::Left).extents;
        QVERIFY(!extents.contains(QStringLiteral("two")));
        QVERIFY(extents.contains(QStringLiteral("one")));
    }

    void lastProfileCannotBeRemoved()
    {
        ProfilesFixture f;
        QVERIFY(f.commitA());
        WorkspaceProfiles profiles(f.settings, f.manager.get());
        QVERIFY(profiles.load().ok());
        const QString only = profiles.activeId();
        const quint64 revision = profiles.revision();

        const auto r = profiles.remove(only);
        QCOMPARE(r.status, WorkspaceProfiles::Status::LastProfile);
        QCOMPARE(profiles.ids(), QStringList{only});
        QCOMPARE(profiles.revision(), revision);

        // The rail offers no remove cross on the last badge, and one on
        // each badge once there are two.
        ProfileRail rail(&profiles, Qt::Horizontal);
        QVERIFY(rail.badgeButton(only));
        QVERIFY(rail.badgeButton(only)->findChildren<QPushButton*>().isEmpty());
        QVERIFY(profiles.duplicate(only, QStringLiteral("Copy")).ok());
        settle();
        QCOMPARE(rail.badges().size(), 2);
        for (const QString& id : rail.badges()) {
            QCOMPARE(rail.badgeButton(id)->findChildren<QPushButton*>().size(), 1);
        }

        // Horizontal: the cross sits right of the centred letter, inside
        // the badge (Longpath placed it over the letter's right half).
        rail.show();
        settle();
        for (const QString& id : rail.badges()) {
            QPushButton* badge = rail.badgeButton(id);
            QPushButton* cross = badge->findChildren<QPushButton*>().constFirst();
            const int letter = badge->fontMetrics().horizontalAdvance(badge->text());
            const int letterRight = (badge->width() + letter) / 2;
            QVERIFY2(cross->x() >= letterRight,
                     qPrintable(QStringLiteral("cross at %1, letter ends at %2, badge %3 wide")
                                    .arg(cross->x()).arg(letterRight).arg(badge->width())));
            QVERIFY(cross->geometry().right() < badge->width());
        }
    }

    void dialogBadgesArePillsAndSummariesElide()
    {
        ProfilesFixture f;
        const QString longName = QStringLiteral("a-container-with-a-rather-long-name");
        QVERIFY(f.commit({f.container(QStringLiteral("main"), DockEdge::Right, HeaderMode::Shown),
                          f.container(longName + QStringLiteral("-one"), DockEdge::Left, HeaderMode::Shown),
                          f.container(longName + QStringLiteral("-two"), DockEdge::Left, HeaderMode::Shown),
                          f.container(longName + QStringLiteral("-three"), DockEdge::Top, HeaderMode::Shown)}));
        WorkspaceProfiles profiles(f.settings, f.manager.get());
        QVERIFY(profiles.load().ok());
        const QString first = profiles.activeId();
        QVERIFY(profiles.duplicate(first, QStringLiteral("Copy")).ok());

        WorkspaceProfilesDialog dialog(&profiles);
        dialog.resize(600, 400);
        dialog.show();
        settle();

        // One pill per row, checked only for the active profile.
        const auto badges = dialog.findChildren<QPushButton*>(QStringLiteral("workspaceProfilesBadge"));
        QCOMPARE(badges.size(), 2);
        int checked = 0;
        for (QPushButton* b : badges) {
            QVERIFY(b->isCheckable());
            checked += b->isChecked() ? 1 : 0;
        }
        QCOMPARE(checked, 1);

        // The long summary is elided with an ellipsis and carried whole
        // in the tooltip.
        const QString full = profiles.summary(first);
        QVERIFY(full.contains(longName + QStringLiteral("-three")));
        const auto summaries = dialog.findChildren<QLabel*>(QStringLiteral("workspaceProfilesSummary"));
        QCOMPARE(summaries.size(), 2);
        for (QLabel* l : summaries) {
            QVERIFY2(l->text().endsWith(QChar(0x2026)), qPrintable(l->text()));
            QCOMPARE(l->toolTip(), full);
            QVERIFY(l->fontMetrics().horizontalAdvance(l->text()) <= l->contentsRect().width());
        }
    }

    void importCreatesANewId()
    {
        ProfilesFixture f;
        QVERIFY(f.commitA());
        WorkspaceProfiles profiles(f.settings, f.manager.get());
        QVERIFY(profiles.load().ok());
        const QString original = profiles.activeId();
        const QByteArray bytes = profiles.exportProfile(original);
        QVERIFY(!bytes.isEmpty());

        const auto first = profiles.importProfile(bytes);
        QVERIFY2(first.ok(), qPrintable(first.error));
        const auto second = profiles.importProfile(bytes);
        QVERIFY2(second.ok(), qPrintable(second.error));

        QCOMPARE(profiles.ids().size(), 3);
        QVERIFY(first.id != original);
        QVERIFY(second.id != original);
        QVERIFY(first.id != second.id);
        // Import never activates or replaces.
        QCOMPARE(profiles.activeId(), original);
        QCOMPARE(profiles.name(first.id), profiles.name(original));
        QCOMPARE(arrangement(profiles.profile(first.id)->workspace),
                 arrangement(profiles.profile(original)->workspace));

        QVERIFY(!profiles.importProfile(QByteArrayLiteral("{\"format\":\"other\"}")).ok());
        QCOMPARE(profiles.ids().size(), 3);
    }

    void staleRevisionIsRejected()
    {
        ProfilesFixture f;
        QVERIFY(f.commitA());
        WorkspaceProfiles profiles(f.settings, f.manager.get());
        QVERIFY(profiles.load().ok());
        const quint64 revision = profiles.revision();
        const QString before = f.settings.value(QString::fromLatin1(WorkspaceProfiles::kSettingsKey)).toString();

        WorkspaceProfilesState next = profiles.state();
        next.profiles.first().name = QStringLiteral("Renamed");
        const auto stale = profiles.commit(next, revision - 1);
        QCOMPARE(stale.status, WorkspaceProfiles::Status::Stale);
        QCOMPARE(profiles.revision(), revision);
        QCOMPARE(f.settings.value(QString::fromLatin1(WorkspaceProfiles::kSettingsKey)).toString(), before);

        // A second writer on the same settings moves the key; the first
        // instance's next write is refused rather than lost over it.
        WorkspaceProfiles other(f.settings, f.manager.get());
        QVERIFY(other.load().ok());
        QVERIFY(other.rename(other.activeId(), QStringLiteral("Other")).ok());
        QSignalSpy changed(&profiles, &WorkspaceProfiles::profilesChanged);
        const auto refused = profiles.rename(profiles.activeId(), QStringLiteral("Mine"));
        QCOMPARE(refused.status, WorkspaceProfiles::Status::Stale);
        // Refused, not retried: the key is read once more and the rail and
        // dialog are told, so the next action works without a reload.
        QCOMPARE(changed.count(), 1);
        QCOMPARE(profiles.name(profiles.activeId()), QStringLiteral("Other"));
        QVERIFY(profiles.rename(profiles.activeId(), QStringLiteral("Mine")).ok());
        QCOMPARE(profiles.name(profiles.activeId()), QStringLiteral("Mine"));

        // An invalid state is refused before anything is written.
        WorkspaceProfilesState broken = profiles.state();
        broken.activeId = QStringLiteral("missing");
        QCOMPARE(profiles.commit(broken, profiles.revision()).status, WorkspaceProfiles::Status::Invalid);
    }

    void lastSelectedProfileSurvivesReload()
    {
        ProfilesFixture f;
        QVERIFY(f.commitA());
        QString chosen;
        QStringList ids;
        {
            WorkspaceProfiles profiles(f.settings, f.manager.get());
            QVERIFY(profiles.load().ok());
            const auto created = profiles.createFromCurrent(QStringLiteral("DX"));
            QVERIFY(created.ok());
            QVERIFY(profiles.createFromCurrent(QStringLiteral("FT8")).ok());
            QVERIFY(profiles.switchTo(created.id).ok());
            chosen = created.id;
            ids = profiles.ids();
        }

        AppSettings reloaded(f.path);
        reloaded.load();
        WorkspaceProfiles again(reloaded, nullptr);
        QVERIFY(again.load().ok());
        QCOMPARE(again.activeId(), chosen);
        QCOMPARE(again.ids(), ids);
        QCOMPARE(again.name(chosen), QStringLiteral("DX"));
    }

    void railDockRestsCollapsedAndFansOutLeftward()
    {
        ProfilesFixture f;
        QVERIFY(f.commitA());
        WorkspaceProfiles profiles(f.settings, f.manager.get());
        QVERIFY(profiles.load().ok());
        QVERIFY(profiles.rename(profiles.activeId(), QStringLiteral("SSB")).ok());
        const auto dx = profiles.duplicate(profiles.activeId(), QStringLiteral("DX"));
        QVERIFY(dx.ok());

        QWidget host;
        host.resize(1000, 300);
        auto* bar = new QWidget(&host);
        bar->setGeometry(0, 0, 1000, 32);
        auto* row = new QHBoxLayout(bar);
        row->addStretch(1);
        auto* title = new QLabel(QStringLiteral("NereusSDR"), bar);
        row->addWidget(title);
        row->addStretch(1);
        ProfileRailDock dock(&profiles, title, &host, bar);
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        settle();
        dock.reposition();

        // At rest: one checked pill with the active letter, left of the
        // title text with the gap; no popover.
        QVERIFY(!dock.isOpen());
        QCOMPARE(dock.collapsedBadge()->text(), QStringLiteral("S"));
        QVERIFY(dock.collapsedBadge()->isChecked());
        const int textLeft = title->x()
            + (title->width() - title->fontMetrics().horizontalAdvance(title->text())) / 2;
        QCOMPARE(dock.geometry().right() + 1 + ProfileRailDock::kTitleGap, textLeft);

        // A click fans it out leftward: right edges line up.
        QTest::mouseClick(dock.collapsedBadge(), Qt::LeftButton);
        settle();
        QVERIFY(dock.isOpen());
        QCOMPARE(dock.rail()->badges().size(), 2);
        const QPoint badgeRight = dock.collapsedBadge()->mapTo(&host,
            QPoint(dock.collapsedBadge()->width(), 0));
        QCOMPARE(dock.popover()->geometry().right() + 1, badgeRight.x());
        QVERIFY(dock.popover()->width() > dock.width());

        // Escape closes it.
        QTest::keyClick(&host, Qt::Key_Escape);
        settle();
        QVERIFY(!dock.isOpen());

        // Picking a profile switches and closes.
        dock.open();
        QVERIFY(dock.rail()->clickBadge(dx.id));
        settle();
        QCOMPARE(profiles.activeId(), dx.id);
        QVERIFY(!dock.isOpen());
        QCOMPARE(dock.collapsedBadge()->text(), QStringLiteral("D"));

        // Picking the profile that is already active closes it too.
        dock.open();
        QVERIFY(dock.rail()->clickBadge(dx.id));
        settle();
        QVERIFY(!dock.isOpen());
    }

    void openRailKeepsClearOfTheReadoutOnItsLeft()
    {
        ProfilesFixture f;
        QVERIFY(f.commitA());
        WorkspaceProfiles profiles(f.settings, f.manager.get());
        QVERIFY(profiles.load().ok());
        for (const char* name : {"DX", "Contest", "FT8"}) {
            QVERIFY(profiles.duplicate(profiles.activeId(), QString::fromLatin1(name)).ok());
        }

        // A readout on the left wide enough that the rail, opened leftward
        // from its badge, would reach over it (the Core readout beside a
        // title pushed left by the PC and RADIO volume group).
        QWidget host;
        host.resize(700, 300);
        auto* bar = new QWidget(&host);
        bar->setGeometry(0, 0, 700, 32);
        auto* row = new QHBoxLayout(bar);
        row->setContentsMargins(0, 0, 0, 0);
        auto* readout = new QWidget(bar);
        readout->setFixedWidth(200);
        row->addWidget(readout);
        row->addStretch(1);
        auto* title = new QLabel(QStringLiteral("NereusSDR"), bar);
        row->addWidget(title);
        row->addStretch(1);
        auto* right = new QWidget(bar);
        right->setFixedWidth(260);
        row->addWidget(right);
        ProfileRailDock dock(&profiles, title, &host, bar);
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        settle();
        dock.reposition();

        dock.open();
        settle();
        QVERIFY(dock.isOpen());
        const QPoint badgeRight = dock.collapsedBadge()->mapTo(&host,
            QPoint(dock.collapsedBadge()->width(), 0));
        const int readoutRight = readout->mapTo(&host, QPoint(readout->width(), 0)).x();
        // Leftward from the badge it would start over the readout.
        QVERIFY(badgeRight.x() - dock.popover()->width() < readoutRight);
        // It starts clear of the readout instead.
        QVERIFY(dock.popover()->geometry().left() >= readoutRight);
    }
};

QTEST_MAIN(TestWorkspaceProfiles)
#include "tst_workspace_profiles.moc"
