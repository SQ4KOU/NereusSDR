// no-port-check: NereusSDR-original offscreen regression for the Main Panel's width against its applets.
// Modification history (NereusSDR):
//   2026-10-08 - The Main Panel is never narrower than its applets, in
//                 today's look and in the workspace preview: the fit at
//                 1280x800 and 1512x869, the splitter minimum, a restored
//                 narrow size, and the minimum following the widest
//                 visible applet. J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
//   2026-10-08 - The geometry-capture cases wait on the capture itself
//                 (geometryCommitPendingForTest, the split it writes), not
//                 a fixed time. J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
//   2026-10-08 - stripFollowsAnAppletMinimumAtOnce: the preview's right
//                 strip follows an applet's own minimum (TCI server
//                 started and stopped) with no workspace change. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
#include <QtTest/QtTest>
#include <QJsonArray>
#include <QLoggingCategory>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <chrono>
#include <memory>

#include "core/AppSettings.h"
#include "core/ConnectionState.h"
#include "core/RadioDiscovery.h"
#include "fakes/RemoteWindowHarness.h"
#include "gui/MainWindow.h"
#include "core/TciServer.h"
#include "gui/applets/AppletWidget.h"
#include "gui/applets/RxApplet.h"
#include "gui/applets/TciApplet.h"
#include "gui/containers/ContainerContentHost.h"
#include "gui/containers/ContainerContentRegistry.h"
#include "gui/containers/ContainerManager.h"
#include "gui/containers/ContainerWidget.h"
#include "gui/containers/ContainerWorkspaceStore.h"
#include "gui/containers/EdgeDockStrip.h"
#include "gui/styles/AppTheme.h"
#include "gui/styles/WorkspacePreview.h"
#include "models/RadioModel.h"

using namespace NereusSDR;
using NereusSDR::Test::RemoteWindowHarness;

namespace {

// Generous: the geometry capture's own debounce is 200 ms.
constexpr int kCaptureTimeoutMs = 10000;

void settle()
{
    for (int pass = 0; pass < 8; ++pass) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        QCoreApplication::processEvents(QEventLoop::AllEvents);
    }
}

// The window, as the app builds it, in the look the case asks for: the
// startup theme of src/main.cpp, the preview switch, no radio and no
// discovery (scans held off for the case).
struct PanelWindow {
    std::unique_ptr<MainWindow> window;
    ContainerManager* manager{nullptr};

    PanelWindow(bool preview, const QSize& size)
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
        window->radioModel()->setBoardForTest(HPSDRHW::Saturn);
        window->resize(size);
        window->show();
        window->resize(size);
        settle();
        manager = window->findChild<ContainerManager*>();
    }

    ContainerWidget* panel() const { return manager != nullptr ? manager->panelContainer() : nullptr; }
    ContainerContentHost* host() const { return panel() != nullptr ? manager->contentHost(panel()->id()) : nullptr; }
    QScrollArea* scroll() const
    {
        return host() != nullptr ? host()->findChild<QScrollArea*>(QString(), Qt::FindDirectChildrenOnly) : nullptr;
    }
    // Today's look: the splitter the Main Panel sits in beside the spectrum.
    QSplitter* splitter() const { return panel() != nullptr ? qobject_cast<QSplitter*>(panel()->parentWidget()) : nullptr; }

    // Hides or shows one entry of the Main Panel, as Containers > Applets does.
    bool setEntryVisible(const QString& typeId, bool visible)
    {
        WorkspaceDocument document = manager->workspaceStore()->snapshot();
        bool found = false;
        for (auto& c : document.containers) {
            if (c.id != panel()->id()) {
                continue;
            }
            for (auto& e : c.contents) {
                if (e.typeId == typeId) {
                    e.visible = visible;
                    found = true;
                }
            }
        }
        if (!found) {
            return false;
        }
        const auto result = manager->commitWorkspace(document, document.revision);
        settle();
        return result.status == CommitStatus::Saved;
    }
};

int minimumWidthOf(const QWidget* w)
{
    return qMax(w->minimumWidth(), w->minimumSizeHint().width());
}

// The workspace entry whose singleton view is `view`.
QString typeIdOf(const PanelWindow& w, const QWidget* view)
{
    for (const auto& c : w.manager->workspaceStore()->snapshot().containers) {
        for (const auto& e : c.contents) {
            if (w.manager->contentRegistry()->singletonView(e.typeId) == view) {
                return e.typeId;
            }
        }
    }
    return {};
}

// The outermost visible applets in the Main Panel's scrolled body.
QList<AppletWidget*> visibleApplets(QWidget* body)
{
    QList<AppletWidget*> out;
    for (AppletWidget* a : body->findChildren<AppletWidget*>()) {
        if (!a->isVisibleTo(body)) {
            continue;
        }
        bool nested = false;
        for (QWidget* p = a->parentWidget(); p != nullptr && p != body; p = p->parentWidget()) {
            nested = nested || qobject_cast<AppletWidget*>(p) != nullptr;
        }
        if (!nested) {
            out.append(a);
        }
    }
    return out;
}

// Every visible applet lies inside the viewport, and its minimum fits it.
QString clippedApplets(const PanelWindow& w)
{
    QScrollArea* scroll = w.scroll();
    if (scroll == nullptr || scroll->widget() == nullptr) {
        return QStringLiteral("no Main Panel scroll area");
    }
    QWidget* viewport = scroll->viewport();
    QStringList problems;
    const QList<AppletWidget*> applets = visibleApplets(scroll->widget());
    if (applets.isEmpty()) {
        return QStringLiteral("no visible applet");
    }
    for (AppletWidget* a : applets) {
        const int left = a->mapTo(viewport, QPoint()).x();
        const int right = left + a->width();
        if (left < 0 || right > viewport->width() || minimumWidthOf(a) > viewport->width()) {
            problems.append(QStringLiteral("%1 x %2..%3 (minimum %4) in a %5 px viewport")
                                .arg(QString::fromLatin1(a->metaObject()->className()))
                                .arg(left).arg(right).arg(minimumWidthOf(a)).arg(viewport->width()));
        }
    }
    return problems.join(QStringLiteral("; "));
}

// The width the Main Panel's contents need: the scrolled body's minimum
// (the widest visible applet and the body margins) and the vertical bar.
int contentWidth(const PanelWindow& w)
{
    QScrollArea* scroll = w.scroll();
    QScrollBar* bar = scroll->verticalScrollBar();
    const QMargins margins = scroll->widget()->layout()->contentsMargins();
    int body = scroll->widget()->minimumSizeHint().width();
    if (WorkspacePreview::enabled()) {
        // The preview body gives up its right margin to the bar.
        body -= margins.right();
    }
    return body + bar->sizeHint().width();
}

QPoint handleCentre(QSplitterHandle* handle)
{
    return handle->mapToGlobal(handle->rect().center());
}

void mouse(QWidget* target, QEvent::Type type, const QPoint& global, Qt::MouseButtons buttons)
{
    const QPoint local = target->mapFromGlobal(global);
    QMouseEvent event(type, local, global, Qt::LeftButton, buttons, Qt::NoModifier);
    QApplication::sendEvent(target, &event);
}

} // namespace

class TstWorkspaceMainPanelFit : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QLoggingCategory::setFilterRules(QStringLiteral("nereus.*.debug=false"));
        QVERIFY(RemoteWindowHarness::useIsolatedProfile(QStringLiteral("workspace-main-panel-fit")));
        qApp->setStyle(QStringLiteral("Fusion"));
        RadioDiscovery::clearHoldOffForTest();
        RadioDiscovery discovery;
        discovery.holdOffScans(std::chrono::minutes{30});
    }
    void init() { QVERIFY(RemoteWindowHarness::clearIsolatedProfile()); }
    void cleanupTestCase()
    {
        WorkspacePreview::setEnabledForTest(false);
        RadioDiscovery::clearHoldOffForTest();
        QVERIFY(RemoteWindowHarness::removeIsolatedProfile());
    }

    void noAppletIsCutOff_data()
    {
        QTest::addColumn<bool>("preview");
        QTest::addColumn<QSize>("size");
        QTest::newRow("today 1280x800") << false << QSize(1280, 800);
        QTest::newRow("today 1512x869") << false << QSize(1512, 869);
        QTest::newRow("preview 1280x800") << true << QSize(1280, 800);
        QTest::newRow("preview 1512x869") << true << QSize(1512, 869);
    }
    void noAppletIsCutOff()
    {
        QFETCH(bool, preview);
        QFETCH(QSize, size);
        PanelWindow w(preview, size);
        QVERIFY(w.panel() != nullptr && w.scroll() != nullptr);
        QCOMPARE(w.window->width(), size.width());
        QVERIFY2(w.scroll()->verticalScrollBar()->isVisible(), "the panel's applets should scroll at this height");
        const QString clipped = clippedApplets(w);
        QVERIFY2(clipped.isEmpty(), qPrintable(clipped));
        QVERIFY2(w.panel()->width() >= contentWidth(w),
                 qPrintable(QStringLiteral("panel %1 < content %2").arg(w.panel()->width()).arg(contentWidth(w))));
        QVERIFY2(w.panel()->minimumWidth() >= contentWidth(w),
                 qPrintable(QStringLiteral("minimum %1 < content %2").arg(w.panel()->minimumWidth()).arg(contentWidth(w))));
        // The window still fits the screen size it was given.
        QVERIFY2(w.window->minimumSizeHint().width() <= size.width(),
                 qPrintable(QString::number(w.window->minimumSizeHint().width())));
    }

    void splitterRefusesANarrowerPanel()
    {
        PanelWindow w(false, QSize(1280, 800));
        QSplitter* splitter = w.splitter();
        QVERIFY(splitter != nullptr);
        const int needed = contentWidth(w);
        // Asked for less than the minimum, the splitter keeps the minimum.
        splitter->setSizes({splitter->width(), 40});
        settle();
        QVERIFY2(w.panel()->width() >= needed, qPrintable(QStringLiteral("%1 < %2").arg(w.panel()->width()).arg(needed)));
        // Dragging the handle toward the panel stops at the minimum.
        splitter->setSizes({splitter->width() - 500, 500});
        settle();
        QSplitterHandle* handle = splitter->handle(1);
        const QPoint grab = handleCentre(handle);
        mouse(handle, QEvent::MouseButtonPress, grab, Qt::LeftButton);
        mouse(handle, QEvent::MouseMove, grab + QPoint(480, 0), Qt::LeftButton);
        mouse(handle, QEvent::MouseButtonRelease, grab + QPoint(480, 0), Qt::NoButton);
        settle();
        QVERIFY2(w.panel()->width() >= needed, qPrintable(QStringLiteral("%1 < %2").arg(w.panel()->width()).arg(needed)));
        QVERIFY2(w.panel()->width() < 500, "the drag should have narrowed the panel");
        const QString clipped = clippedApplets(w);
        QVERIFY2(clipped.isEmpty(), qPrintable(clipped));
    }

    void restoredNarrowSizeIsRaisedWithoutSaving()
    {
        PanelWindow w(false, QSize(1512, 869));
        QSplitter* splitter = w.splitter();
        QVERIFY(splitter != nullptr);
        ContainerWorkspaceStore* store = w.manager->workspaceStore();
        // Startup's own geometry capture runs first.
        settle();
        QTRY_VERIFY_WITH_TIMEOUT(!w.manager->geometryCommitPendingForTest(), kCaptureTimeoutMs);
        WorkspaceDocument document = store->snapshot();
        const QJsonArray narrow{1300, 120};
        document.extensions[QStringLiteral("splitterSizes")] = narrow;
        QCOMPARE(w.manager->commitWorkspace(document, document.revision).status, CommitStatus::Saved);
        settle();
        QTRY_VERIFY_WITH_TIMEOUT(!w.manager->geometryCommitPendingForTest(), kCaptureTimeoutMs);
        QCOMPARE(store->snapshot().extensions.value(QStringLiteral("splitterSizes")).toArray(), narrow);
        const quint64 revision = store->snapshot().revision;
        const QString legacy = AppSettings::instance().value(QStringLiteral("MainSplitterSizes")).toString();

        w.manager->restoreSplitterState();
        settle();
        // Any capture the restore scheduled has run (and would show below).
        QTRY_VERIFY_WITH_TIMEOUT(!w.manager->geometryCommitPendingForTest(), kCaptureTimeoutMs);
        QVERIFY2(w.panel()->width() >= contentWidth(w),
                 qPrintable(QStringLiteral("%1 < %2").arg(w.panel()->width()).arg(contentWidth(w))));
        const QString clipped = clippedApplets(w);
        QVERIFY2(clipped.isEmpty(), qPrintable(clipped));
        // Nothing is written until the user changes something.
        QCOMPARE(store->snapshot().revision, revision);
        QCOMPARE(store->snapshot().extensions.value(QStringLiteral("splitterSizes")).toArray(), narrow);
        QCOMPARE(AppSettings::instance().value(QStringLiteral("MainSplitterSizes")).toString(), legacy);
    }

    void savedNarrowSizeIsRaisedAtStartup()
    {
        const QJsonArray narrow{1300, 120};
        {
            // A first session saves its workspace; then the saved Main
            // Panel width is one from before the minimum counted the bar.
            PanelWindow first(false, QSize(1512, 869));
            QVERIFY(first.panel() != nullptr);
        }
        {
            ContainerWorkspaceStore store(AppSettings::instance());
            WorkspaceDocument document = store.snapshot();
            QVERIFY(!document.containers.isEmpty());
            document.extensions[QStringLiteral("splitterSizes")] = narrow;
            QCOMPARE(store.commit(document, document.revision).status, CommitStatus::Saved);
        }
        PanelWindow w(false, QSize(1512, 869));
        QVERIFY(w.panel() != nullptr && w.scroll() != nullptr);
        // Startup's own geometry capture writes the split the panel has.
        QTRY_VERIFY_WITH_TIMEOUT(
            w.manager->workspaceStore()->snapshot().extensions.value(QStringLiteral("splitterSizes")).toArray() != narrow,
            kCaptureTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!w.manager->geometryCommitPendingForTest(), kCaptureTimeoutMs);
        QVERIFY2(w.panel()->width() >= contentWidth(w),
                 qPrintable(QStringLiteral("%1 < %2").arg(w.panel()->width()).arg(contentWidth(w))));
        const QString clipped = clippedApplets(w);
        QVERIFY2(clipped.isEmpty(), qPrintable(clipped));
        // Startup's own geometry capture (a container resized while the
        // window is shown, as before this change) then saves the width the
        // panel really has, never the narrow one.
        const QJsonArray saved =
            w.manager->workspaceStore()->snapshot().extensions.value(QStringLiteral("splitterSizes")).toArray();
        QCOMPARE(saved.size(), 2);
        QVERIFY2(saved.at(1).toInt() >= contentWidth(w), qPrintable(QString::number(saved.at(1).toInt())));
    }

    void minimumFollowsTheWidestVisibleApplet_data()
    {
        QTest::addColumn<bool>("preview");
        QTest::newRow("today") << false;
        QTest::newRow("preview") << true;
    }
    void minimumFollowsTheWidestVisibleApplet()
    {
        QFETCH(bool, preview);
        PanelWindow w(preview, QSize(1280, 800));
        QVERIFY(w.panel() != nullptr && w.scroll() != nullptr);
        // The widest visible applet.
        AppletWidget* widest = nullptr;
        for (AppletWidget* a : visibleApplets(w.scroll()->widget())) {
            if (widest == nullptr || minimumWidthOf(a) > minimumWidthOf(widest)) {
                widest = a;
            }
        }
        QVERIFY(widest != nullptr);
        const int before = w.panel()->minimumWidth();
        QVERIFY2(before >= contentWidth(w), qPrintable(QStringLiteral("%1 < %2").arg(before).arg(contentWidth(w))));
        const QString typeId = typeIdOf(w, widest);
        QVERIFY2(!typeId.isEmpty(), widest->metaObject()->className());

        QVERIFY(w.setEntryVisible(typeId, false));
        QVERIFY(widest->isHidden());
        const int hidden = w.panel()->minimumWidth();
        QVERIFY2(hidden < before, qPrintable(QStringLiteral("%1 vs %2").arg(hidden).arg(before)));
        QVERIFY2(hidden >= contentWidth(w), qPrintable(QStringLiteral("%1 < %2").arg(hidden).arg(contentWidth(w))));
        QString clipped = clippedApplets(w);
        QVERIFY2(clipped.isEmpty(), qPrintable(clipped));

        QVERIFY(w.setEntryVisible(typeId, true));
        QVERIFY(!widest->isHidden());
        QCOMPARE(w.panel()->minimumWidth(), before);
        QVERIFY2(w.panel()->width() >= before, qPrintable(QStringLiteral("%1 < %2").arg(w.panel()->width()).arg(before)));
        clipped = clippedApplets(w);
        QVERIFY2(clipped.isEmpty(), qPrintable(clipped));
    }

    // Workspace preview: an applet whose own minimum changes with no
    // workspace change (the TCI applet's running controls appear when its
    // server starts) moves the right strip's floor at once.
    void stripFollowsAnAppletMinimumAtOnce()
    {
        PanelWindow w(true, QSize(1280, 800));
        QVERIFY(w.panel() != nullptr && w.scroll() != nullptr);
        EdgeDockStrip* strip = w.manager->edgeDockArea() != nullptr
            ? w.manager->edgeDockArea()->strip(DockEdge::Right) : nullptr;
        QVERIFY(strip != nullptr);
        auto* tci = w.window->findChild<TciApplet*>();
        auto* server = w.window->findChild<TciServer*>();
        QVERIFY(tci != nullptr && server != nullptr);
        QVERIFY(!server->isRunning());
        auto* rx = w.window->findChild<RxApplet*>();
        QVERIFY(rx != nullptr);

        // Set up through the workspace: the RX applet hidden, so the TCI
        // applet's width decides, and the strip dragged as narrow as it goes.
        QVERIFY(w.setEntryVisible(typeIdOf(w, rx), false));
        WorkspaceDocument document = w.manager->workspaceStore()->snapshot();
        document.arrangement.strips[DockEdge::Right].thickness = 120;
        QCOMPARE(w.manager->commitWorkspace(document, document.revision).status, CommitStatus::Saved);
        settle();
        const int stoppedTci = minimumWidthOf(tci);
        const int stoppedStrip = strip->width();
        QVERIFY2(stoppedStrip >= contentWidth(w), qPrintable(QStringLiteral("%1 < %2").arg(stoppedStrip).arg(contentWidth(w))));
        const quint64 revision = w.manager->workspaceStore()->snapshot().revision;

        // The server starting, as the applet hears it (no socket is opened).
        emit server->serverStarted(0);
        settle();
        QVERIFY2(minimumWidthOf(tci) > stoppedTci, qPrintable(QString::number(minimumWidthOf(tci))));
        QVERIFY2(strip->width() > stoppedStrip, qPrintable(QStringLiteral("%1 vs %2").arg(strip->width()).arg(stoppedStrip)));
        QVERIFY2(strip->width() >= contentWidth(w), qPrintable(QStringLiteral("%1 < %2").arg(strip->width()).arg(contentWidth(w))));
        QString clipped = clippedApplets(w);
        QVERIFY2(clipped.isEmpty(), qPrintable(clipped));
        QCOMPARE(w.manager->workspaceStore()->snapshot().revision, revision);

        // Stopped again, the floor comes back down.
        emit server->serverStopped();
        settle();
        QCOMPARE(strip->width(), stoppedStrip);
        clipped = clippedApplets(w);
        QVERIFY2(clipped.isEmpty(), qPrintable(clipped));
    }
};

QTEST_MAIN(TstWorkspaceMainPanelFit)
#include "tst_workspace_main_panel_fit.moc"
