// =================================================================
// tests/tst_workspace_reset_layout.cpp  (NereusSDR)
// =================================================================
//
// NereusSDR-original test. No Thetis port; no attribution-registry row.
// Containers > Reset Default Layout with the workspace look on, on the real
// MainWindow, offscreen.
// =================================================================

#include <QtTest/QtTest>
#include <QAction>
#include <QLoggingCategory>
#include <QMenu>
#include <QMenuBar>
#include <chrono>
#include <functional>
#include <memory>

#include "core/AppSettings.h"
#include "core/RadioDiscovery.h"
#include "fakes/RemoteWindowHarness.h"
#include "gui/MainWindow.h"
#include "gui/containers/ContainerContentRegistry.h"
#include "gui/containers/ContainerManager.h"
#include "gui/containers/ContainerWidget.h"
#include "gui/containers/ContainerWorkspaceStore.h"
#include "gui/styles/AppTheme.h"
#include "gui/styles/WorkspacePreview.h"
#include "models/RadioModel.h"

using namespace NereusSDR;
using NereusSDR::Test::RemoteWindowHarness;

namespace {

void settle()
{
    for (int pass = 0; pass < 8; ++pass) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        QCoreApplication::processEvents(QEventLoop::AllEvents);
    }
}

const QString kSMeter = QStringLiteral("applet:s_meter");

// "type" per shown entry, "type(hidden)" per hidden one, in document order.
QStringList entriesOf(const ContainerDocument& c)
{
    QStringList out;
    for (const auto& e : c.contents) {
        out.append(e.typeId + (e.visible ? QString() : QStringLiteral("(hidden)")));
    }
    return out;
}

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
        window->radioModel()->setBoardForTest(HPSDRHW::Saturn);
        window->resize(1512, 869);
        window->show();
        settle();
        manager = window->findChild<ContainerManager*>();
    }

    WorkspaceDocument document() const { return manager->workspaceStore()->snapshot(); }

    ContainerDocument main() const
    {
        const WorkspaceDocument d = document();
        for (const auto& c : d.containers) {
            if (c.id == d.mainContainerId) {
                return c;
            }
        }
        return {};
    }

    bool edit(const std::function<void(WorkspaceDocument&, ContainerDocument&)>& change)
    {
        WorkspaceDocument d = document();
        for (auto& c : d.containers) {
            if (c.id == d.mainContainerId) {
                change(d, c);
                break;
            }
        }
        const bool saved = manager->commitWorkspace(d, d.revision).status == CommitStatus::Saved;
        settle();
        return saved;
    }

    // Containers > Reset Default Layout, as the menu runs it.
    bool reset()
    {
        for (QAction* action : window->findChildren<QAction*>()) {
            if (action->text() == QStringLiteral("&Reset Default Layout")) {
                action->trigger();
                settle();
                return true;
            }
        }
        return false;
    }

    QWidget* sMeter() const { return manager->contentRegistry()->singletonView(kSMeter); }

    // The S-Meter's check boxes: Containers > Applets and the panel's own menu.
    QList<QAction*> sMeterCheckBoxes() const
    {
        QList<QAction*> out;
        for (QAction* action : window->findChildren<QAction*>()) {
            if (action->isCheckable() && action->text() == QStringLiteral("S-Meter")) {
                out.append(action);
            }
        }
        return out;
    }
    // "checked" or "unchecked" when every check box agrees, else what each says.
    QString sMeterChecked() const
    {
        QStringList states;
        for (const QAction* action : sMeterCheckBoxes()) {
            states.append(action->isChecked() ? QStringLiteral("checked") : QStringLiteral("unchecked"));
        }
        states.removeDuplicates();
        return states.join(QStringLiteral(" and "));
    }

    // Empty when the S-Meter shows in the window's top right quarter.
    QString sMeterMissing() const
    {
        QWidget* view = sMeter();
        if (view == nullptr || !view->isVisibleTo(window.get())) {
            return QStringLiteral("the S-Meter does not show");
        }
        const QPoint centre = view->mapTo(window.get(), view->rect().center());
        if (centre.x() < window->width() / 2 || centre.y() < 0 || centre.y() > window->height() / 2) {
            return QStringLiteral("the S-Meter's centre is at %1,%2 in a %3 x %4 window")
                .arg(centre.x()).arg(centre.y()).arg(window->width()).arg(window->height());
        }
        return {};
    }
};

int indexOfType(const ContainerDocument& c, const QString& typeId)
{
    for (int i = 0; i < c.contents.size(); ++i) {
        if (c.contents[i].typeId == typeId) {
            return i;
        }
    }
    return -1;
}

} // namespace

class TstWorkspaceResetLayout : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QLoggingCategory::setFilterRules(QStringLiteral("nereus.*.debug=false"));
        QVERIFY(RemoteWindowHarness::useIsolatedProfile(QStringLiteral("workspace-reset-layout")));
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

    // However the S-Meter was lost, a reset draws what a first launch
    // draws: one Main Panel, the applets in the first-launch order, the
    // S-Meter showing in the top right. One window per look (each window
    // takes seconds to build), the ways of losing it one after another.
    void resetDrawsTheFirstLaunch_data()
    {
        QTest::addColumn<bool>("preview");
        QTest::newRow("look on") << true;
        QTest::newRow("look off") << false;
    }
    void resetDrawsTheFirstLaunch()
    {
        QFETCH(bool, preview);
        LookWindow w(preview);
        QVERIFY(w.manager != nullptr && w.sMeter() != nullptr);
        // The first launch: the S-Meter first, showing in the top right.
        const QStringList first = entriesOf(w.main());
        QCOMPARE(w.document().containers.size(), 1);
        QCOMPARE(first.value(0), kSMeter);
        QVERIFY2(w.sMeterMissing().isEmpty(), qPrintable(w.sMeterMissing()));
        const QString rxId = w.main().contents.value(indexOfType(w.main(), QStringLiteral("applet:rx"))).id;
        QVERIFY(!rxId.isEmpty());

        QStringList problems;
        for (const char* lostName : {"nothing", "last in the panel", "hidden", "in another container",
                                     "popped out", "no entry", "a meter first and the S-Meter last"}) {
            const QString lost = QString::fromLatin1(lostName);
            if (indexOfType(w.main(), kSMeter) < 0) {
                // The reset before this one left the panel with no S-Meter
                // (reported below); the next way of losing it needs one.
                QVERIFY2(w.edit([&](WorkspaceDocument&, ContainerDocument& main) {
                    main.contents.prepend(w.manager->contentRegistry()->makeEntry(kSMeter));
                }), lostName);
            }
            QVERIFY2(w.edit([&](WorkspaceDocument& d, ContainerDocument& main) {
                const int at = indexOfType(main, kSMeter);
                if (lost == QLatin1String("last in the panel")) {
                    main.contents.append(main.contents.takeAt(at));
                } else if (lost == QLatin1String("hidden")) {
                    main.contents[at].visible = false;
                } else if (lost == QLatin1String("in another container") || lost == QLatin1String("popped out")) {
                    ContainerDocument other;
                    other.id = QStringLiteral("other");
                    other.name = QStringLiteral("Other");
                    other.layout = ContentLayout::VerticalStack;
                    other.geometry = QRect(200, 200, 320, 240);
                    if (lost == QLatin1String("popped out")) {
                        other.dockMode = DockMode::Floating;
                        other.popOutShell = true;
                        other.visible = false;
                        main.contents[at].returnLocation =
                            ReturnLocation{main.id, {}, main.contents.value(at + 1).id, {}};
                    } else {
                        other.dockMode = DockMode::PanelDocked;
                        other.edge = DockEdge::Left;
                    }
                    other.contents.append(main.contents.takeAt(at));
                    d.containers.append(other);
                } else if (lost == QLatin1String("no entry")) {
                    main.contents.removeAt(at);
                } else if (lost == QLatin1String("a meter first and the S-Meter last")) {
                    // JJ's panel, from his report: a meter at the top, the S-Meter last.
                    main.contents.append(main.contents.takeAt(at));
                    main.contents.prepend(w.manager->contentRegistry()->makeEntry(QStringLiteral("meter.customBar")));
                }
            }), lostName);

            QVERIFY2(w.reset(), lostName);
            if (w.document().containers.size() != 1) {
                problems.append(QStringLiteral("%1: %2 containers").arg(lost).arg(w.document().containers.size()));
            }
            if (entriesOf(w.main()) != first) {
                problems.append(QStringLiteral("%1: the panel holds %2").arg(lost, entriesOf(w.main()).join(QStringLiteral(", "))));
            }
            if (!w.sMeterMissing().isEmpty()) {
                problems.append(QStringLiteral("%1: %2").arg(lost, w.sMeterMissing()));
            }
            // A reset keeps each applet's own entry, so its settings stay.
            if (w.main().contents.value(indexOfType(w.main(), QStringLiteral("applet:rx"))).id != rxId) {
                problems.append(QStringLiteral("%1: the RX applet's entry was replaced").arg(lost));
            }
        }
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QStringLiteral("\n"))));

        // A reset leaves the Applets menu's other choices alone: an applet
        // the user hid stays hidden, in its first-launch place.
        QStringList expected = first;
        const int vax = indexOfType(w.main(), QStringLiteral("applet:vax"));
        QVERIFY(vax >= 0);
        expected[vax] += QStringLiteral("(hidden)");
        QVERIFY(w.edit([&](WorkspaceDocument&, ContainerDocument& main) { main.contents[vax].visible = false; }));
        QVERIFY(w.reset());
        QCOMPARE(entriesOf(w.main()), expected);
    }

    // The S-Meter has a check box with the other applets, in Containers >
    // Applets (JJ, 2026-10-12): it hides and shows the S-Meter, and follows
    // a hide made elsewhere and a reset.
    void sMeterHasACheckBox_data()
    {
        QTest::addColumn<bool>("preview");
        QTest::newRow("look on") << true;
        QTest::newRow("look off") << false;
    }
    void sMeterHasACheckBox()
    {
        QFETCH(bool, preview);
        LookWindow w(preview);
        QVERIFY(w.manager != nullptr && w.sMeter() != nullptr);
        // One in the Containers menu, the first of its applet rows.
        QMenu* containers = nullptr;
        QStringList menus;
        // The window's menu bar sits in its title bar (QMainWindow::menuBar()
        // would make a second, empty one).
        for (const QMenuBar* bar : w.window->findChildren<QMenuBar*>()) {
            for (QAction* top : bar->actions()) {
                menus.append(top->text());
                if (top->menu() != nullptr && top->text() == QStringLiteral("Contai&ners")) {
                    containers = top->menu();
                }
            }
        }
        QVERIFY2(containers != nullptr, qPrintable(menus.join(QStringLiteral(" | "))));
        QStringList appletRows;
        QAction* box = nullptr;
        for (QAction* action : containers->actions()) {
            if (action->isCheckable() && action->toolTip().startsWith(QStringLiteral("Show or hide the "))) {
                appletRows.append(action->text());
                if (action->text() == QStringLiteral("S-Meter")) {
                    box = action;
                }
            }
        }
        QVERIFY2(box != nullptr, qPrintable(appletRows.join(QStringLiteral(" | "))));
        QCOMPARE(appletRows.value(0), QStringLiteral("S-Meter"));
        QVERIFY(box->isEnabled());
        QCOMPARE(w.sMeterChecked(), QStringLiteral("checked"));
        const QStringList first = entriesOf(w.main());

        box->trigger();
        settle();
        QCOMPARE(w.sMeterChecked(), QStringLiteral("unchecked"));
        QVERIFY(!w.main().contents.value(indexOfType(w.main(), kSMeter)).visible);
        QVERIFY(!w.sMeter()->isVisibleTo(w.window.get()));

        box->trigger();
        settle();
        QCOMPARE(w.sMeterChecked(), QStringLiteral("checked"));
        QCOMPARE(entriesOf(w.main()), first);
        QVERIFY2(w.sMeterMissing().isEmpty(), qPrintable(w.sMeterMissing()));

        // Hidden, Containers > Hidden lists it too, and picking it there
        // shows it and checks the box.
        box->trigger();
        settle();
        auto* hidden = w.window->findChild<QMenu*>(QStringLiteral("containersHidden"));
        QVERIFY(hidden != nullptr);
        QAction* row = nullptr;
        QStringList rows;
        for (QAction* action : hidden->actions()) {
            rows.append(action->text());
            if (action->text().startsWith(QStringLiteral("S-Meter")) && action->isEnabled()) {
                row = action;
            }
        }
        QVERIFY2(row != nullptr, qPrintable(rows.join(QStringLiteral(" | "))));
        row->trigger();
        settle();
        QCOMPARE(w.sMeterChecked(), QStringLiteral("checked"));
        QVERIFY2(w.sMeterMissing().isEmpty(), qPrintable(w.sMeterMissing()));

        // Hidden some other way, the box shows it; a reset checks it again.
        QVERIFY(w.edit([&](WorkspaceDocument&, ContainerDocument& main) {
            main.contents[indexOfType(main, kSMeter)].visible = false;
        }));
        QCOMPARE(w.sMeterChecked(), QStringLiteral("unchecked"));
        QVERIFY(w.reset());
        QCOMPARE(w.sMeterChecked(), QStringLiteral("checked"));
        QVERIFY2(w.sMeterMissing().isEmpty(), qPrintable(w.sMeterMissing()));
    }
};

QTEST_MAIN(TstWorkspaceResetLayout)
#include "tst_workspace_reset_layout.moc"
