// =================================================================
// tests/tst_mainwindow_menu_layout.cpp  (NereusSDR)
// =================================================================
//
// Edit-container refactor Task 17 — menu reorganisation:
//   * View > Applets submenu holds the 7 applet show/hide toggles
//     (migrated out of Containers > ...).
//   * The Containers menu no longer lists applet toggles — only
//     container-lifecycle entries (New, Edit Container, Reset).
//
// Inspects the live menu bar exposed by a constructed MainWindow so
// the test survives future menu-ordering tweaks.
//
// no-port-check: test scaffolding; menu-bar layout is NereusSDR-
// original.
// =================================================================

#include <QtTest/QtTest>
#include <QAction>
#include <QMenu>
#include <QMenuBar>

#include "gui/MainWindow.h"

using namespace NereusSDR;

namespace {

QMenu* findTopLevelMenu(QMenuBar* bar, const QString& text)
{
    if (!bar) { return nullptr; }
    const QString needle = text.toLower();
    for (QAction* a : bar->actions()) {
        if (!a->menu()) { continue; }
        QString t = a->text();
        t.remove(QLatin1Char('&'));
        if (t.toLower() == needle) { return a->menu(); }
    }
    return nullptr;
}

QMenu* findSubmenu(QMenu* parent, const QString& text)
{
    if (!parent) { return nullptr; }
    const QString needle = text.toLower();
    for (QAction* a : parent->actions()) {
        if (!a->menu()) { continue; }
        QString t = a->text();
        t.remove(QLatin1Char('&'));
        if (t.toLower() == needle) { return a->menu(); }
    }
    return nullptr;
}

bool menuHasActionMatching(QMenu* menu, const QString& text)
{
    if (!menu) { return false; }
    for (QAction* a : menu->actions()) {
        if (a->text() == text) { return true; }
    }
    return false;
}

} // namespace

class TstMainWindowMenuLayout : public QObject
{
    Q_OBJECT

private slots:
    void viewMenu_hasAppletsSubmenu_with7Toggles();
    void containersMenu_noLongerHasAppletToggles();
};

// MainWindow owns a QThread (SpectrumThread) that only gets shut down
// in closeEvent. Constructing/destructing a MainWindow per test would
// leak the thread and crash on destruction. Instead, build it once per
// test, close() it explicitly so closeEvent runs, then let it go out
// of scope.

namespace {

struct MainWindowScope {
    MainWindow w;
    ~MainWindowScope() { w.close(); }
};

} // namespace

void TstMainWindowMenuLayout::viewMenu_hasAppletsSubmenu_with7Toggles()
{
    MainWindowScope scope;
    QMenu* viewMenu = findTopLevelMenu(scope.w.menuBar(), QStringLiteral("View"));
    QVERIFY(viewMenu != nullptr);

    QMenu* appletsMenu = findSubmenu(viewMenu, QStringLiteral("Applets"));
    QVERIFY2(appletsMenu != nullptr,
             "View > Applets submenu missing after Task 17 menu reorg");

    int toggleCount = 0;
    for (QAction* a : appletsMenu->actions()) {
        if (a->isSeparator()) { continue; }
        if (a->isCheckable()) { ++toggleCount; }
    }
    QCOMPARE(toggleCount, 7);

    // Spot-check a couple of expected labels so a rename doesn't
    // silently pass the count-only check.
    QVERIFY(menuHasActionMatching(appletsMenu, QStringLiteral("Digital / VAC")));
    QVERIFY(menuHasActionMatching(appletsMenu, QStringLiteral("ATU Control")));
}

void TstMainWindowMenuLayout::containersMenu_noLongerHasAppletToggles()
{
    MainWindowScope scope;
    QMenu* containersMenu = findTopLevelMenu(scope.w.menuBar(), QStringLiteral("Containers"));
    QVERIFY(containersMenu != nullptr);

    const QStringList appletNames = {
        QStringLiteral("Digital / VAC"),
        QStringLiteral("PureSignal"),
        QStringLiteral("Diversity"),
        QStringLiteral("CW Keyer"),
        QStringLiteral("Voice Keyer"),
        QStringLiteral("CAT / TCI"),
        QStringLiteral("ATU Control"),
    };
    for (const QString& name : appletNames) {
        QVERIFY2(!menuHasActionMatching(containersMenu, name),
                 qPrintable(QStringLiteral("Containers menu still lists applet toggle: %1").arg(name)));
    }
}

QTEST_MAIN(TstMainWindowMenuLayout)
#include "tst_mainwindow_menu_layout.moc"
