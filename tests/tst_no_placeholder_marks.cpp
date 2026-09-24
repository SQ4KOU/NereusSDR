// =================================================================
// tests/tst_no_placeholder_marks.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original test. It builds real local and remote
// MainWindows, every Setup page, the Spot Hub, Network Diagnostics and a
// slice flag; no upstream logic is ported here.
//
// R3 unfinished controls, Task 4 (R-R3-49, R-R3-21): no control a user can
// reach carries a not-yet-implemented mark, overlay or tooltip. Every
// widget and menu item the app creates, local and remote, is swept (hidden
// ones too, since a hidden control comes back when its feature is built),
// once with the unbuilt list as it is and once with every feature marked
// built, so the surfaces hidden today are built and swept as well. The
// check itself is proven on a control marked the old way, so a mark added
// anywhere fails this test.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-24  J.J. Boyd / KG4VCF  R3 unfinished controls, Task 4.
//                                    AI-assisted transformation via
//                                    Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QGroupBox>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QRegularExpression>
#include <QTabWidget>
#include <QWidget>

#include <memory>

#include "core/AppSettings.h"
#include "core/BuildIdentity.h"
#include "core/RadioDiscovery.h"
#include "gui/GuiSessionCoordinator.h"
#include "gui/MainWindow.h"
#include "gui/NetworkDiagnosticsDialog.h"
#include "gui/SetupDialog.h"
#include "gui/SpectrumWidget.h"
#include "gui/SpotHubDialog.h"
#include "gui/UnbuiltFeatures.h"
#include "gui/applets/NyiOverlay.h"
#include "gui/widgets/VfoWidget.h"
#include "models/RadioModel.h"

using namespace NereusSDR;

namespace {

StationStartupSelection remoteCore()
{
    // Never dialled: replace(..., false) builds the remote window without
    // starting its connection.
    return {{QStringLiteral("ws://127.0.0.1:4433"), {}, {}, true}, QStringLiteral("core")};
}

// The words of a not-yet-implemented mark: the NYI badge, the old overlay's
// and markNyi's tooltips, and the "Phase X" roadmap placeholder.
const QRegularExpression& placeholderWording()
{
    static const QRegularExpression pattern(
        QStringLiteral("\\bNYI\\b|not yet implemented|\\bPhase X\\b|Available in Phase"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

QString describe(const QObject* object)
{
    QStringList chain;
    for (const QObject* o = object; o != nullptr; o = o->parent()) {
        const QString name = o->objectName();
        chain.prepend(QString::fromLatin1(o->metaObject()->className())
                      + (name.isEmpty() ? QString() : QStringLiteral("#") + name));
        if (chain.size() >= 4) { break; }
    }
    return chain.join(QStringLiteral(" > "));
}

// Every text a widget or a menu item can show a user: its captions, tab
// and list items, and its tooltip, status tip and "What's This".
QStringList textsOf(const QWidget* w)
{
    QStringList texts{w->toolTip(), w->statusTip(), w->whatsThis(),
                      w->accessibleName(), w->accessibleDescription()};
    if (const auto* label = qobject_cast<const QLabel*>(w)) {
        texts << label->text();
    } else if (const auto* button = qobject_cast<const QAbstractButton*>(w)) {
        texts << button->text();
    } else if (const auto* group = qobject_cast<const QGroupBox*>(w)) {
        texts << group->title();
    } else if (const auto* combo = qobject_cast<const QComboBox*>(w)) {
        for (int i = 0; i < combo->count(); ++i) {
            texts << combo->itemText(i) << combo->itemData(i, Qt::ToolTipRole).toString();
        }
    } else if (const auto* tabs = qobject_cast<const QTabWidget*>(w)) {
        for (int i = 0; i < tabs->count(); ++i) {
            texts << tabs->tabText(i) << tabs->tabToolTip(i);
        }
    } else if (const auto* menu = qobject_cast<const QMenu*>(w)) {
        texts << menu->title();
    }
    return texts;
}

QStringList textsOf(const QAction* a)
{
    return {a->text(), a->toolTip(), a->statusTip(), a->whatsThis(), a->iconText()};
}

// Every placeholder mark on the widgets alive now, hidden ones included.
QStringList marksInApp()
{
    QStringList found;
    QSet<const QAction*> actions;
    for (const QWidget* w : QApplication::allWidgets()) {
        if (qobject_cast<const NyiOverlay*>(w) != nullptr) {
            found << QStringLiteral("NYI badge on ") + describe(w->parent());
            continue;
        }
        for (const QString& text : textsOf(w)) {
            if (placeholderWording().match(text).hasMatch()) {
                found << describe(w) + QStringLiteral(": ") + text;
            }
        }
        for (const QAction* a : w->actions()) { actions.insert(a); }
        for (const QAction* a : w->findChildren<QAction*>(Qt::FindDirectChildrenOnly)) {
            actions.insert(a);
        }
    }
    for (const QAction* a : actions) {
        for (const QString& text : textsOf(a)) {
            if (placeholderWording().match(text).hasMatch()) {
                found << QStringLiteral("menu item ") + describe(a) + QStringLiteral(": ") + text;
            }
        }
    }
    found.removeDuplicates();
    return found;
}

// Builds everything the app creates for one window: the window (its menus,
// status bar, applets, pan overlays and containers), the Setup dialog with
// every registered page realized, the Spot Hub, Network Diagnostics and a
// slice flag.
QStringList marksInWindow(GuiSessionCoordinator& sessions, bool remote)
{
    const bool ok = remote ? sessions.replace(remoteCore(), false) : sessions.replace({}, false);
    MainWindow* window = ok ? sessions.window() : nullptr;
    if (window == nullptr) { return {QStringLiteral("no window")}; }
    if (window->radioModel()->ownsLocalDsp() == remote) {
        return {QStringLiteral("wrong window kind")};
    }
    window->resize(4000, 1000);
    window->show();
    QCoreApplication::processEvents();

    auto* setup = new SetupDialog(window->radioModel(), window);
    setup->realizeAllPagesForTest();
    QMetaObject::invokeMethod(window, "openSpotHub", Qt::DirectConnection);
    auto* netDiag = new NetworkDiagnosticsDialog(window->radioModel(), nullptr, window);
    Q_UNUSED(netDiag);

    auto flagHost = std::make_unique<SpectrumWidget>();
    flagHost->resize(1200, 500);
    flagHost->setSampleRate(192000.0);
    flagHost->setDdcCenterFrequency(14200000.0);
    flagHost->setFrequencyRange(14200000.0, 192000.0);
    VfoWidget* flag = flagHost->addVfoWidget(0);
    flag->setFrequency(14200000.0);
    flagHost->show();
    QCoreApplication::processEvents();
    flagHost->updateVfoPositions();

    return marksInApp();
}

} // namespace

class TstNoPlaceholderMarks : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Windows save their settings; this run keeps a file of its own.
        AppSettings::setProfileOverride(QStringLiteral("no-placeholder-marks-%1")
                                            .arg(QCoreApplication::applicationPid()));
    }

    void init()
    {
        UnbuiltFeatures::resetForTest();
        QVERIFY(!AppSettings::instance().remoteBackend());
        AppSettings::instance().clear();
        // No VAX first-run dialog and no discovery broadcast onto the LAN.
        AppSettings::instance().setValue(QStringLiteral("audio/FirstRunComplete"),
                                         QStringLiteral("True"));
        AppSettings::instance().ensureSettingsAtVersion(6);
        RadioDiscovery::clearHoldOffForTest();
        RadioDiscovery discovery;
        discovery.holdOffScans(std::chrono::minutes{5});
        BuildIdentity::setBuildTag(QString());
    }

    void cleanup()
    {
        UnbuiltFeatures::resetForTest();
        RadioDiscovery::clearHoldOffForTest();
    }

    void cleanupTestCase()
    {
        const QString path = AppSettings::instance().filePath();
        QFile::remove(path);
        QFile::remove(path + QStringLiteral(".bak"));
    }

    // The check finds a control marked the old way (overlay tooltip and
    // badge) and a menu item with a placeholder tooltip, so adding a mark
    // anywhere the app builds fails the sweep below.
    void theSweepFindsAMark()
    {
        QVERIFY(marksInApp().isEmpty());
        {
            QWidget host;
            auto* button = new QPushButton(QStringLiteral("Try"), &host);
            NyiOverlay::markNyi(button, QStringLiteral("Phase 9"));
            QVERIFY2(!marksInApp().isEmpty(), "a markNyi tooltip was not found");
        }
        {
            QWidget host;
            auto* badge = new NyiOverlay(QStringLiteral("Phase 9"), &host);
            badge->setToolTip(QString());
            QVERIFY2(!marksInApp().isEmpty(), "an NYI badge was not found");
        }
        {
            QMenu menu;
            QAction* item = menu.addAction(QStringLiteral("Something"));
            item->setToolTip(QStringLiteral("NYI - Phase X"));
            QVERIFY2(!marksInApp().isEmpty(), "a menu item's placeholder tooltip was not found");
        }
        QVERIFY(marksInApp().isEmpty());
    }

    // Nothing the app creates carries a mark, in a local or a remote window.
    void noPlaceholderMarkInLocalOrRemoteWindows()
    {
        GuiSessionCoordinator sessions;
        for (bool remote : {false, true}) {
            const QStringList marks = marksInWindow(sessions, remote);
            QVERIFY2(marks.isEmpty(),
                     qPrintable((remote ? QStringLiteral("remote: ") : QStringLiteral("local: "))
                                + marks.join(QStringLiteral("; "))));
        }
        QVERIFY(sessions.replace({}, false));
    }

    // With every feature on the unbuilt list marked built, the surfaces
    // hidden today (menus, Setup pages, applet pages) are built too; none of
    // them carries a mark either, so none comes back with one.
    void noPlaceholderMarkOnSurfacesHiddenUntilBuilt()
    {
        for (const UnbuiltFeatures::Entry& entry : UnbuiltFeatures::all()) {
            UnbuiltFeatures::setBuiltForTest(entry.feature, true);
        }
        GuiSessionCoordinator sessions;
        for (bool remote : {false, true}) {
            const QStringList marks = marksInWindow(sessions, remote);
            QVERIFY2(marks.isEmpty(),
                     qPrintable((remote ? QStringLiteral("remote, all built: ")
                                        : QStringLiteral("local, all built: "))
                                + marks.join(QStringLiteral("; "))));
        }
        QVERIFY(sessions.replace({}, false));
    }
};

QTEST_MAIN(TstNoPlaceholderMarks)
#include "tst_no_placeholder_marks.moc"
