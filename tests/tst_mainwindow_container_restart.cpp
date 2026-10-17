// no-port-check: NereusSDR-original offscreen regression for free containers across a quit and a start of the real window.
// Modification history (NereusSDR):
//   2026-10-11 - A third container, anchored to the pane's right edge and
//                 saved with no pane size: it too is shown at its saved
//                 place, though the pane is given two sizes on the way up
//                 in today's look. J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
//   2026-10-11 - Two free containers keep their places across a quit and
//                 a start of the real MainWindow, in today's look and in
//                 the workspace preview: the saved layout is unchanged and
//                 they do not come up on top of each other. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
#include <QtTest/QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <chrono>
#include <memory>

#include "core/AppSettings.h"
#include "core/RadioDiscovery.h"
#include "fakes/RemoteWindowHarness.h"
#include "gui/MainWindow.h"
#include "gui/containers/ContainerDocumentCodec.h"
#include "gui/containers/ContainerManager.h"
#include "gui/containers/ContainerWidget.h"
#include "gui/containers/ContainerWorkspaceStore.h"
#include "gui/styles/AppTheme.h"
#include "gui/styles/WorkspacePreview.h"
#include "models/RadioModel.h"

using namespace NereusSDR;
using NereusSDR::Test::RemoteWindowHarness;

namespace {

constexpr QSize kWindowSize(1500, 950);
constexpr QRect kFirstRect(60, 50, 300, 200);
constexpr QRect kSecondRect(420, 280, 320, 240);
// Anchored to the pane's right edge.
constexpr QRect kThirdRect(780, 560, 300, 200);
constexpr int kShownMs = 400;
constexpr int kSettleTimeoutMs = 10000;

void settle()
{
    for (int pass = 0; pass < 8; ++pass) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        QCoreApplication::processEvents(QEventLoop::AllEvents);
    }
}

ContainerDocument freeContainer(const QString& id, const QRect& geometry, AxisLock anchor = AxisLock::Left)
{
    ContainerDocument d;
    d.id = id;
    d.name = id;
    d.layout = ContentLayout::VerticalStack;
    d.dockMode = DockMode::OverlayDocked;
    d.anchor = anchor;
    d.geometry = geometry;
    d.config = {{QStringLiteral("enabled"), true},
                {QStringLiteral("showOnRx"), true},
                {QStringLiteral("showOnTx"), true},
                {QStringLiteral("border"), true},
                {QStringLiteral("pinOnTop"), false}};
    return d;
}

// Every size the pane is given, in order.
class ResizeLog : public QObject
{
public:
    QVector<QSize> sizes;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Resize) {
            sizes.append(static_cast<QWidget*>(watched)->size());
        }
        return QObject::eventFilter(watched, event);
    }
};

std::unique_ptr<MainWindow> buildWindow(bool preview)
{
    WorkspacePreview::setEnabledForTest(preview);
    auto& app = *qobject_cast<QApplication*>(QCoreApplication::instance());
    applyDarkPalette(app);
    applyAppBaselineQss(app);
    if (preview) {
        applyWorkspacePalette(app);
    }
    auto window = std::make_unique<MainWindow>(RemoteStationOptions{}, nullptr,
                                               MainWindow::ConnectionStartup::Deferred);
    window->radioModel()->setBoardForTest(HPSDRHW::Saturn);
    return window;
}

QString workspaceKey(bool preview)
{
    return preview ? QStringLiteral("ContainerWorkspacePreview") : QStringLiteral("ContainerWorkspace");
}

// The saved document's "containers" array, exactly as written.
QByteArray savedContainersJson(bool preview)
{
    const QByteArray raw = AppSettings::instance().value(workspaceKey(preview)).toString().toUtf8();
    return QJsonDocument(QJsonDocument::fromJson(raw).object().value(QStringLiteral("containers")).toArray())
        .toJson(QJsonDocument::Compact);
}

WorkspaceDocument savedDocument(bool preview)
{
    return ContainerDocumentCodec::decode(AppSettings::instance().value(workspaceKey(preview)).toString().toUtf8())
        .document;
}

QRect geometryOf(const WorkspaceDocument& document, const QString& id)
{
    for (const auto& d : document.containers) {
        if (d.id == id) {
            return d.geometry;
        }
    }
    return {};
}

} // namespace

class TstMainWindowContainerRestart : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QLoggingCategory::setFilterRules(QStringLiteral("nereus.*.debug=false"));
        QVERIFY(RemoteWindowHarness::useIsolatedProfile(QStringLiteral("mainwindow-container-restart")));
        qApp->setStyle(QStringLiteral("Fusion"));
        qApp->setQuitOnLastWindowClosed(false);
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

    void freeContainersKeepTheirPlacesAcrossARestart_data()
    {
        QTest::addColumn<bool>("preview");
        QTest::newRow("look on") << true;
        QTest::newRow("look off") << false;
    }

    void freeContainersKeepTheirPlacesAcrossARestart()
    {
        QFETCH(bool, preview);
        QSize savedPane;
        // A first session leaves two free containers at distinct places
        // inside the pane, and quits.
        {
            std::unique_ptr<MainWindow> window = buildWindow(preview);
            window->resize(kWindowSize);
            window->show();
            window->resize(kWindowSize);
            settle();
            auto* manager = window->findChild<ContainerManager*>();
            QVERIFY(manager && manager->workspaceStore());
            WorkspaceDocument document = manager->workspaceStore()->snapshot();
            document.containers.append(freeContainer(QStringLiteral("free-a"), kFirstRect));
            document.containers.append(freeContainer(QStringLiteral("free-b"), kSecondRect));
            document.containers.append(freeContainer(QStringLiteral("free-c"), kThirdRect, AxisLock::Right));
            QCOMPARE(manager->commitWorkspace(document, document.revision).status, CommitStatus::Saved);
            settle();
            ContainerWidget* a = manager->container(QStringLiteral("free-a"));
            ContainerWidget* b = manager->container(QStringLiteral("free-b"));
            ContainerWidget* c = manager->container(QStringLiteral("free-c"));
            QVERIFY(a && b && c);
            QWidget* pane = a->parentWidget();
            QVERIFY(pane && b->parentWidget() == pane);
            savedPane = pane->size();
            QVERIFY2(pane->rect().contains(kFirstRect) && pane->rect().contains(kSecondRect)
                         && pane->rect().contains(kThirdRect),
                     qPrintable(QStringLiteral("pane %1x%2 does not hold the fixture rects")
                                    .arg(savedPane.width()).arg(savedPane.height())));
            QTest::qWait(kShownMs);
            QVERIFY(QTest::qWaitFor([manager] { return !manager->geometryCommitPendingForTest(); }, kSettleTimeoutMs));
            QCOMPARE(a->geometry(), kFirstRect);
            QCOMPARE(b->geometry(), kSecondRect);
            QCOMPARE(c->geometry(), kThirdRect);
            window->close();
        }
        const QByteArray committedJson = savedContainersJson(preview);
        const WorkspaceDocument committed = savedDocument(preview);
        QCOMPARE(geometryOf(committed, QStringLiteral("free-a")), kFirstRect);
        QCOMPARE(geometryOf(committed, QStringLiteral("free-b")), kSecondRect);

        // The next start.
        QRect shownA;
        QRect shownB;
        QRect shownC;
        QSize finalPane;
        {
            std::unique_ptr<MainWindow> window = buildWindow(preview);
            auto* manager = window->findChild<ContainerManager*>();
            QVERIFY(manager);
            ContainerWidget* a = manager->container(QStringLiteral("free-a"));
            ContainerWidget* b = manager->container(QStringLiteral("free-b"));
            ContainerWidget* c = manager->container(QStringLiteral("free-c"));
            QVERIFY(a && b && c);
            QWidget* pane = a->parentWidget();
            QVERIFY(pane);
            ResizeLog log;
            pane->installEventFilter(&log);
            qInfo() << "dock parent size at restore:" << pane->size() << "free-a:" << a->geometry()
                    << "free-b:" << b->geometry() << "window:" << window->size();
            // The window comes back at the size it had (the test profile
            // restores no window geometry of its own).
            window->resize(kWindowSize);
            window->show();
            window->resize(kWindowSize);
            QTest::qWait(kShownMs);
            shownA = a->geometry();
            shownB = b->geometry();
            shownC = c->geometry();
            finalPane = pane->size();
            qInfo() << "pane sizes after show, in order:" << log.sizes << "window:" << window->size()
                    << "pane at save:" << savedPane;
            qInfo() << "shown free-a:" << shownA << "free-b:" << shownB << "free-c:" << shownC;
            pane->removeEventFilter(&log);
            window->close();
        }
        const WorkspaceDocument after = savedDocument(preview);
        for (const auto& d : after.containers) {
            const QRect was = geometryOf(committed, d.id);
            if (was != d.geometry) {
                qInfo() << "saved geometry changed:" << d.id << d.name << int(d.dockMode) << was << "->" << d.geometry;
            }
        }
        QVERIFY2(!shownA.intersects(shownB), "the two free containers are shown on top of each other");
        QCOMPARE(geometryOf(after, QStringLiteral("free-a")), kFirstRect);
        QCOMPARE(geometryOf(after, QStringLiteral("free-b")), kSecondRect);
        QCOMPARE(after.containers, committed.containers);
        QCOMPARE(savedContainersJson(preview), committedJson);
        // Same window, same pane: shown exactly where they were left.
        QCOMPARE(finalPane, savedPane);
        QCOMPARE(shownA, kFirstRect);
        QCOMPARE(shownB, kSecondRect);
        // No pane size was saved with it (no operator move), so its right
        // anchor works from the pane as it settled, not from the first
        // size the pane was given.
        QVERIFY(!after.containers.last().geometryPane.has_value());
        QCOMPARE(shownC, kThirdRect);
    }
};

QTEST_MAIN(TstMainWindowContainerRestart)
#include "tst_mainwindow_container_restart.moc"
