// =================================================================
// tests/tst_gui_connection_controller.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. R-R3-38 controller presentation and
// session-switch acceptance coverage.
// =================================================================

#include <QtTest/QtTest>

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QHostAddress>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QWebSocketServer>

#include <chrono>

#include "OperatorWording.h"
#include "core/AppSettings.h"
#include "core/RadioDiscovery.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "gui/ConnectionSelector.h"
#include "gui/CoreTargetEditor.h"
#include "gui/CoreTargetStore.h"
#include "gui/GuiConnectionController.h"
#include "gui/MainWindow.h"
#include "models/RadioModel.h"

using namespace NereusSDR;

namespace {

SavedCoreTarget savedTarget(const QString& id, const QString& label, quint16 port,
                            const QString& token)
{
    SavedCoreTarget target;
    target.id = id;
    target.label = label;
    target.connection.url = QStringLiteral("ws://127.0.0.1:%1").arg(port);
    target.connection.token = token;
    target.connection.allowUnpinned = true;
    return target;
}

bool installTargets(const QList<SavedCoreTarget>& targets, const QString& selectedId)
{
    CoreTargetStore store(AppSettings::instance());
    if (!store.load()) {
        return false;
    }
    for (const SavedCoreTarget& target : targets) {
        if (!store.upsert(target)) {
            return false;
        }
    }
    return store.select(selectedId);
}

QPushButton* button(ConnectionSelector* selector, const QString& objectName)
{
    return selector->findChild<QPushButton*>(objectName);
}

QAction* managedConnectAction(MainWindow* window)
{
    for (QAction* action : window->findChildren<QAction*>()) {
        if (action->text() == QStringLiteral("&Connect")
            || action->shortcut() == QKeySequence(QStringLiteral("Ctrl+K"))) {
            return action;
        }
    }
    return nullptr;
}

struct LoopbackCores final {
    QTemporaryDir firstDirectory;
    QTemporaryDir secondDirectory;
    AppSettings firstSettings{firstDirectory.filePath(QStringLiteral("station.settings"))};
    AppSettings secondSettings{secondDirectory.filePath(QStringLiteral("station.settings"))};
    // Construct the two station models before any GUI session installs its
    // remote settings proxy. They are real StationServer models, not devices.
    RadioModel firstStation;
    RadioModel secondStation;
    StationServer firstServer{&firstStation, firstSettings, firstDirectory.path()};
    StationServer secondServer{&secondStation, secondSettings, secondDirectory.path()};
    QWebSocketServer firstListener{QStringLiteral("controller-A"), QWebSocketServer::NonSecureMode};
    QWebSocketServer secondListener{QStringLiteral("controller-B"), QWebSocketServer::NonSecureMode};

    bool start()
    {
        if (!firstDirectory.isValid() || !secondDirectory.isValid()
            || !firstListener.listen(QHostAddress::LocalHost, 0)
            || !secondListener.listen(QHostAddress::LocalHost, 0)) {
            return false;
        }
        firstServer.setHeartbeatIntervalMs(0);
        secondServer.setHeartbeatIntervalMs(0);
        QObject::connect(&firstListener, &QWebSocketServer::newConnection, &firstServer, [this] {
            firstServer.acceptTransport(new WebSocketTransport(firstListener.nextPendingConnection(),
                StationServer::kMaxIncomingMessageBytes));
        });
        QObject::connect(&secondListener, &QWebSocketServer::newConnection, &secondServer, [this] {
            secondServer.acceptTransport(new WebSocketTransport(secondListener.nextPendingConnection(),
                StationServer::kMaxIncomingMessageBytes));
        });
        return true;
    }

    SavedCoreTarget firstTarget() const
    {
        return savedTarget(QStringLiteral("a"), QStringLiteral("Core A"),
                           firstListener.serverPort(), firstServer.token());
    }

    SavedCoreTarget secondTarget() const
    {
        return savedTarget(QStringLiteral("b"), QStringLiteral("Core B"),
                           secondListener.serverPort(), secondServer.token());
    }
};

} // namespace

class TestGuiConnectionController final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();
    void cleanupTestCase();

    void managedConnectActionOpensSelectorForEmptyLocalProfile();
    void selectingAndCancellingEditLeaveLiveAAndDoNotDialB();
    void explicitConnectToSavedBReplacesWholeLiveA();
    void disconnectCancelsRetryWithoutUsingHighlightedB();
    void persistentLocalChoiceReturnsToEmbeddedCoreWithoutRadioAutoconnect();
    void savedCoreEditsDoNotChangeCurrentTupleBeforeConnect();
    void corruptStartupDocumentShowsIdleLocalAndNotice();
};

void TestGuiConnectionController::initTestCase()
{
    AppSettings::setProfileOverride(QStringLiteral("gui-connection-controller-%1")
        .arg(QCoreApplication::applicationPid()));
}

void TestGuiConnectionController::init()
{
    QVERIFY(!AppSettings::instance().remoteBackend());
    AppSettings::instance().clear();
    AppSettings::instance().setValue(QStringLiteral("audio/FirstRunComplete"), QStringLiteral("True"));
    QVERIFY(AppSettings::instance().save());
    RadioDiscovery::clearHoldOffForTest();
    RadioDiscovery discovery;
    discovery.holdOffScans(std::chrono::minutes{5});
}

void TestGuiConnectionController::cleanup()
{
    QVERIFY(!AppSettings::instance().remoteBackend());
    RadioDiscovery::clearHoldOffForTest();
}

void TestGuiConnectionController::cleanupTestCase()
{
    const QString path = AppSettings::instance().filePath();
    QFile::remove(path);
    QFile::remove(path + QStringLiteral(".bak"));
}

void TestGuiConnectionController::managedConnectActionOpensSelectorForEmptyLocalProfile()
{
    GuiConnectionController controller;
    controller.start({});
    MainWindow* window = controller.sessions()->window();
    QVERIFY(window != nullptr);
    QVERIFY(window->radioModel()->ownsLocalDsp());
    QVERIFY(window->findChild<StationClient*>() == nullptr);
    QAction* connectAction = managedConnectAction(window);
    QVERIFY(connectAction != nullptr);
    QVERIFY(connectAction->isEnabled());

    connectAction->trigger();
    QTRY_VERIFY(controller.selector()->isVisible());
    QCOMPARE(controller.sessions()->window(), window);
    controller.selector()->setSelectedKey(QStringLiteral("local"));
    auto* details = button(controller.selector(), QStringLiteral("connectionSelectorDetails"));
    QVERIFY(details && details->isEnabled());
    details->click();
    auto* notice = controller.selector()->findChild<QLabel*>(QStringLiteral("connectionSelectorNotice"));
    QVERIFY(notice && notice->text().contains(QStringLiteral("This computer runs its own Core")));
    QVERIFY(OperatorWording::isPlain(notice->text()));
    QVERIFY(window->radioModel()->connectionState() == ConnectionState::Disconnected);
    controller.shutdown();
}

void TestGuiConnectionController::selectingAndCancellingEditLeaveLiveAAndDoNotDialB()
{
    LoopbackCores cores;
    QVERIFY(cores.start());
    QVERIFY(installTargets({cores.firstTarget(), cores.secondTarget()}, QStringLiteral("a")));

    GuiConnectionController controller;
    controller.start({});
    QTRY_VERIFY(cores.firstServer.hasAuthenticatedSession());
    MainWindow* firstWindow = controller.sessions()->window();
    QPointer<MainWindow> firstWindowLifetime = firstWindow;
    QPointer<StationClient> firstClient = firstWindow->findChild<StationClient*>();
    QVERIFY(firstClient);
    QTRY_VERIFY(firstClient->isHandshakeComplete());

    QAction* connectAction = managedConnectAction(firstWindow);
    QVERIFY(connectAction != nullptr);
    connectAction->trigger();
    QTRY_VERIFY(controller.selector()->isVisible());
    controller.selector()->setSelectedKey(QStringLiteral("saved:b"));
    QCoreApplication::processEvents();
    QCOMPARE(controller.sessions()->window(), firstWindow);
    QVERIFY(firstClient->isHandshakeComplete());
    QVERIFY(!cores.secondServer.hasAuthenticatedSession());

    bool cancelledEditor = false;
    QTimer::singleShot(0, &controller, [&] {
        auto* editor = qobject_cast<CoreTargetEditor*>(QApplication::activeModalWidget());
        if (editor == nullptr) {
            return;
        }
        auto* cancel = editor->findChild<QPushButton*>(QStringLiteral("coreTargetEditorCancel"));
        if (cancel != nullptr) {
            cancelledEditor = true;
            cancel->click();
        }
    });
    auto* edit = button(controller.selector(), QStringLiteral("connectionSelectorEdit"));
    QVERIFY(edit != nullptr && edit->isEnabled());
    edit->click();
    QVERIFY(cancelledEditor);
    QCOMPARE(controller.sessions()->window(), firstWindow);
    QVERIFY(!firstWindowLifetime.isNull() && !firstClient.isNull());
    QVERIFY(firstClient->isHandshakeComplete());
    QVERIFY(!cores.secondServer.hasAuthenticatedSession());
    controller.shutdown();
}

void TestGuiConnectionController::explicitConnectToSavedBReplacesWholeLiveA()
{
    LoopbackCores cores;
    QVERIFY(cores.start());
    QVERIFY(installTargets({cores.firstTarget(), cores.secondTarget()}, QStringLiteral("a")));

    GuiConnectionController controller;
    controller.start({});
    QTRY_VERIFY(cores.firstServer.hasAuthenticatedSession());
    MainWindow* firstWindow = controller.sessions()->window();
    QPointer<MainWindow> firstWindowLifetime = firstWindow;
    QPointer<RadioModel> firstModel = firstWindow->radioModel();
    QPointer<StationClient> firstClient = firstWindow->findChild<StationClient*>();
    QVERIFY(firstClient);
    QTRY_VERIFY(firstClient->isHandshakeComplete());

    controller.showConnections();
    controller.selector()->setSelectedKey(QStringLiteral("saved:b"));
    auto* connect = button(controller.selector(), QStringLiteral("connectionSelectorConnect"));
    QVERIFY(connect != nullptr && connect->isEnabled());
    connect->click();
    QTRY_VERIFY(firstWindowLifetime.isNull() && firstModel.isNull() && firstClient.isNull());
    QTRY_VERIFY(cores.secondServer.hasAuthenticatedSession());
    QCOMPARE(controller.sessions()->selection().savedId, QStringLiteral("b"));
    StationClient* secondClient = controller.sessions()->window()->findChild<StationClient*>();
    QVERIFY(secondClient != nullptr);
    QTRY_VERIFY(secondClient->isHandshakeComplete());
    controller.shutdown();
}

void TestGuiConnectionController::disconnectCancelsRetryWithoutUsingHighlightedB()
{
    LoopbackCores cores;
    QVERIFY(cores.start());
    QVERIFY(installTargets({cores.firstTarget(), cores.secondTarget()}, QStringLiteral("a")));

    GuiConnectionController controller;
    controller.start({});
    QTRY_VERIFY(cores.firstServer.hasAuthenticatedSession());
    MainWindow* firstWindow = controller.sessions()->window();
    QPointer<StationClient> firstClient = firstWindow->findChild<StationClient*>();
    QVERIFY(firstClient);
    QTRY_VERIFY(firstClient->isHandshakeComplete());
    firstClient->disconnectFromStation(QStringLiteral("test retry"), true);
    QTRY_VERIFY(firstClient->isReconnectPending());

    controller.showConnections();
    controller.selector()->setSelectedKey(QStringLiteral("saved:b"));
    auto* disconnect = button(controller.selector(), QStringLiteral("connectionSelectorDisconnect"));
    QVERIFY(disconnect != nullptr && disconnect->isEnabled());
    QCOMPARE(disconnect->text(), QStringLiteral("Cancel retry"));
    disconnect->click();
    QTRY_VERIFY(!firstClient->isReconnectPending());
    QVERIFY(!firstClient->isConnectionActive());
    QCOMPARE(controller.sessions()->window(), firstWindow);
    QCOMPARE(controller.sessions()->selection().savedId, QStringLiteral("a"));
    QVERIFY(!cores.secondServer.hasAuthenticatedSession());
    controller.shutdown();
}

void TestGuiConnectionController::persistentLocalChoiceReturnsToEmbeddedCoreWithoutRadioAutoconnect()
{
    LoopbackCores cores;
    QVERIFY(cores.start());
    QVERIFY(installTargets({cores.firstTarget(), cores.secondTarget()}, QStringLiteral("a")));
    RadioInfo oldLocalRadio;
    oldLocalRadio.macAddress = QStringLiteral("AA:BB:CC:DD:EE:99");
    oldLocalRadio.name = QStringLiteral("Old local radio");
    oldLocalRadio.address = QHostAddress::LocalHost;
    oldLocalRadio.port = 1024;
    AppSettings::instance().saveRadio(oldLocalRadio, false, true);
    QVERIFY(AppSettings::instance().save());

    GuiConnectionController controller;
    controller.start({});
    QTRY_VERIFY(cores.firstServer.hasAuthenticatedSession());
    QPointer<MainWindow> remoteWindow = controller.sessions()->window();
    QVERIFY(remoteWindow);
    controller.showConnections();
    controller.selector()->setSelectedKey(QStringLiteral("local"));
    auto* connect = button(controller.selector(), QStringLiteral("connectionSelectorConnect"));
    QVERIFY(connect != nullptr && connect->isEnabled());
    connect->click();
    QTRY_VERIFY(remoteWindow.isNull());
    MainWindow* localWindow = controller.sessions()->window();
    QVERIFY(localWindow != nullptr);
    QVERIFY(localWindow->radioModel()->ownsLocalDsp());
    QVERIFY(localWindow->findChild<StationClient*>() == nullptr);
    QCOMPARE(localWindow->radioModel()->connectionState(), ConnectionState::Disconnected);
    CoreTargetStore store(AppSettings::instance());
    QVERIFY(store.load());
    QCOMPARE(store.selectedId(), QStringLiteral("local"));
    controller.shutdown();
}

void TestGuiConnectionController::savedCoreEditsDoNotChangeCurrentTupleBeforeConnect()
{
    LoopbackCores cores;
    QVERIFY(cores.start());
    const SavedCoreTarget first = cores.firstTarget();
    const SavedCoreTarget second = cores.secondTarget();
    QVERIFY(installTargets({first, second}, QStringLiteral("a")));

    GuiConnectionController controller;
    controller.start({});
    QTRY_VERIFY(cores.firstServer.hasAuthenticatedSession());
    QPointer<StationClient> firstClient = controller.sessions()->window()->findChild<StationClient*>();
    QVERIFY(firstClient);
    QTRY_VERIFY(firstClient->isHandshakeComplete());
    const StationStartupSelection active = controller.sessions()->selection();

    controller.showConnections();
    controller.selector()->setSelectedKey(QStringLiteral("saved:a"));
    bool savedEditor = false;
    QTimer::singleShot(0, &controller, [&] {
        auto* editor = qobject_cast<CoreTargetEditor*>(QApplication::activeModalWidget());
        if (editor == nullptr) {
            return;
        }
        auto* address = editor->findChild<QLineEdit*>(QStringLiteral("coreTargetEditorAddress"));
        auto* token = editor->findChild<QLineEdit*>(QStringLiteral("coreTargetEditorToken"));
        auto* save = editor->findChild<QPushButton*>(QStringLiteral("coreTargetEditorSave"));
        if (address != nullptr && token != nullptr && save != nullptr) {
            address->setText(second.connection.url);
            token->setText(second.connection.token);
            savedEditor = true;
            save->click();
        }
    });
    auto* edit = button(controller.selector(), QStringLiteral("connectionSelectorEdit"));
    QVERIFY(edit != nullptr && edit->isEnabled());
    edit->click();
    QVERIFY(savedEditor);
    QCOMPARE(controller.sessions()->selection().savedId, active.savedId);
    QCOMPARE(controller.sessions()->selection().connection.url, active.connection.url);
    QCOMPARE(controller.sessions()->selection().connection.token, active.connection.token);
    QVERIFY(!firstClient.isNull() && firstClient->isHandshakeComplete());
    QVERIFY(!cores.secondServer.hasAuthenticatedSession());
    controller.shutdown();
}

void TestGuiConnectionController::corruptStartupDocumentShowsIdleLocalAndNotice()
{
    AppSettings::instance().setValue(QStringLiteral("ConnectionTargets/V1"),
                                     QStringLiteral("{not valid JSON"));
    QVERIFY(AppSettings::instance().save());

    GuiConnectionController controller;
    controller.start({});
    MainWindow* window = controller.sessions()->window();
    QVERIFY(window != nullptr);
    QVERIFY(window->radioModel()->ownsLocalDsp());
    QCOMPARE(window->radioModel()->connectionState(), ConnectionState::Disconnected);
    QVERIFY(window->findChild<StationClient*>() == nullptr);
    QTRY_VERIFY(controller.selector()->isVisible());
    auto* notice = controller.selector()->findChild<QLabel*>(QStringLiteral("connectionSelectorNotice"));
    QVERIFY(notice != nullptr && notice->isVisible());
    QVERIFY(!notice->text().isEmpty());
    controller.shutdown();
}

QTEST_MAIN(TestGuiConnectionController)

#include "tst_gui_connection_controller.moc"
