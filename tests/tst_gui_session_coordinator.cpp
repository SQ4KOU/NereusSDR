// no-port-check: NereusSDR-original. R-R3-38 session replacement invariants.
#include <QtTest/QtTest>
#include <QApplication>
#include <QFile>
#include <QPointer>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QWebSocketServer>

#include "core/AppSettings.h"
#include "core/RadioDiscovery.h"
#include "core/WdspEngine.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/settings/SettingsProxy.h"
#include "gui/GuiSessionCoordinator.h"
#include "gui/MainWindow.h"
#include "gui/RemoteConnectionController.h"
#include "gui/SpectrumWidget.h"
#include "gui/widgets/StatusToast.h"
#include "models/NotchModel.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

namespace {
StationStartupSelection core(const QString& id, quint16 port = 4433)
{
    return {{QStringLiteral("ws://127.0.0.1:%1").arg(port), {}, {}, true}, id};
}

int toastsStartingWith(MainWindow* window, const QString& prefix)
{
    int count = 0;
    for (StatusToast* toast : window->findChildren<StatusToast*>()) {
        if (toast->message().startsWith(prefix)) { ++count; }
    }
    return count;
}

void dismissToasts(MainWindow* window)
{
    qDeleteAll(window->findChildren<StatusToast*>());
}
}

class TestGuiSessionCoordinator : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        AppSettings::setProfileOverride(QStringLiteral("station-switch-%1")
            .arg(QCoreApplication::applicationPid()));
    }

    void init()
    {
        QVERIFY(!AppSettings::instance().remoteBackend());
        AppSettings::instance().clear();
        AppSettings::instance().setValue(QStringLiteral("audio/FirstRunComplete"), QStringLiteral("True"));
        QVERIFY(AppSettings::instance().save());
        RadioDiscovery::clearHoldOffForTest();
        RadioDiscovery discovery;
        discovery.holdOffScans(std::chrono::minutes{5});
    }

    void cleanup()
    {
        QVERIFY(!AppSettings::instance().remoteBackend());
        RadioDiscovery::clearHoldOffForTest();
    }

    void cleanupTestCase()
    {
        const QString path = AppSettings::instance().filePath();
        QFile::remove(path);
        QFile::remove(path + QStringLiteral(".bak"));
    }

    void replacesWholeSessionAndRetiresQueuedPickerAndProxy()
    {
        AppSettings::instance().setValue(QStringLiteral("DisplayFftSize"), QStringLiteral("2048"));
        GuiSessionCoordinator sessions;
        QSignalSpy pickerRequests(&sessions, &GuiSessionCoordinator::connectionsRequested);
        QVERIFY(sessions.replace({}, false));
        QPointer<MainWindow> local = sessions.window();
        QVERIFY(local->radioModel()->ownsLocalDsp());
        QVERIFY(!AppSettings::instance().remoteBackend());
        QVERIFY(sessions.replace(core(QStringLiteral("a")), false));
        QVERIFY(local.isNull());
        QPointer<MainWindow> first = sessions.window();
        QPointer<RadioModel> firstModel = first->radioModel();
        QPointer<StationClient> firstClient = first->findChild<StationClient*>();
        auto* firstProxy = dynamic_cast<SettingsProxy*>(AppSettings::instance().remoteBackend());
        QVERIFY(firstProxy);
        QVERIFY(!firstProxy->ready());
        QVERIFY(firstClient);
        QVERIFY(!firstClient->isConnectionActive());
        QVERIFY(!firstModel->ownsLocalDsp());
        QVERIFY(!firstModel->connection());
        firstProxy->applySnapshot({{QStringLiteral("DisplayFftSize"), QStringLiteral("8192")}});
        QCOMPARE(AppSettings::instance().value(QStringLiteral("DisplayFftSize")).toString(),
                 QStringLiteral("8192"));
        QCOMPARE(firstModel->addSliceWithStationId(42, QStringLiteral("pan-0")), 42);
        QPointer<SliceModel> oldSlice = firstModel->sliceById(42);
        QVERIFY(oldSlice);

        bool backendPresentDuringOldWindowDestruction = false;
        bool detachedBeforeProxyDestruction = false;
        connect(first, &QObject::destroyed, &sessions, [&] {
            backendPresentDuringOldWindowDestruction =
                AppSettings::instance().remoteBackend() == firstProxy;
        });
        connect(firstProxy, &QObject::destroyed, &sessions, [&] {
            detachedBeforeProxyDestruction = AppSettings::instance().remoteBackend() == nullptr;
        });
        // The forwarded call is queued in the coordinator, where disconnect
        // alone cannot retract it. The generation guard must reject it.
        QVERIFY(QMetaObject::invokeMethod(first, "connectionsRequested", Qt::DirectConnection));
        const quint64 oldGeneration = sessions.generation();
        QVERIFY(sessions.replace(core(QStringLiteral("b")), false));
        QVERIFY(sessions.generation() > oldGeneration);
        QVERIFY(first.isNull() && firstModel.isNull() && firstClient.isNull() && oldSlice.isNull());
        QVERIFY(backendPresentDuringOldWindowDestruction);
        QVERIFY(detachedBeforeProxyDestruction);
        QCoreApplication::processEvents();
        QCOMPARE(pickerRequests.count(), 0);
        QVERIFY(sessions.window()->radioModel()->slices().isEmpty());
        auto* secondProxy = dynamic_cast<SettingsProxy*>(AppSettings::instance().remoteBackend());
        QVERIFY(secondProxy && !secondProxy->ready());
        QVERIFY(AppSettings::instance().value(QStringLiteral("DisplayFftSize")).toString()
                != QStringLiteral("8192"));

        QPointer<MainWindow> second = sessions.window();
        QPointer<SettingsProxy> secondProxyLifetime = secondProxy;
        QVERIFY(sessions.replace({}, false));
        QVERIFY(second.isNull() && secondProxyLifetime.isNull());
        QVERIFY(!AppSettings::instance().remoteBackend());
        QVERIFY(sessions.window()->radioModel()->ownsLocalDsp());
        QCOMPARE(AppSettings::instance().value(QStringLiteral("DisplayFftSize")).toString(),
                 QStringLiteral("2048"));
        sessions.shutdown();
        QVERIFY(!sessions.window());
    }

    void constructedWindowBindsNotchCreateOnce()
    {
        GuiSessionCoordinator sessions;
        QVERIFY(sessions.replace({}, false));
        MainWindow* window = sessions.window();
        SpectrumWidget* sw = window->activeSpectrumWidget();
        QVERIFY(sw);
        QVERIFY(QMetaObject::invokeMethod(window, "wirePanNotchHandlers", Qt::DirectConnection));
        QVERIFY(QMetaObject::invokeMethod(window, "wirePanNotchHandlers", Qt::DirectConnection));
        QSignalSpy added(window->radioModel()->notchModel(), &NotchModel::notchAdded);
        QSignalSpy rejected(window->radioModel()->notchModel(), &NotchModel::notchAddRejected);
        sw->notchCreateRequested(14'200'000.0, false);
        QCOMPARE(added.count(), 1);
        QCOMPARE(rejected.count(), 0);
    }

    void initializationCannotRetireItsOwnModel()
    {
        GuiSessionCoordinator sessions;
        QVERIFY(sessions.replace({}, false));
        QPointer<MainWindow> original = sessions.window();
        RadioModel* model = original->radioModel();
        model->wdspEngine()->setSynchronousInitForTest(true);
        bool observed = false;
        connect(model->wdspEngine(), &WdspEngine::initializedChanged, &sessions, [&](bool ready) {
            if (!ready) { return; }
            observed = true;
            QVERIFY(model->localConnectionSetupActive());
            QString error;
            QVERIFY(!sessions.replace(core(QStringLiteral("other")), false, &error));
            QVERIFY(!error.isEmpty());
            QCOMPARE(sessions.window(), original.data());
            // Before the model's initialization callback: do not open audio
            // devices or create a radio connection in this ownership test.
            model->disconnectFromRadio();
        });
        RadioInfo info;
        info.address = QHostAddress::LocalHost;
        info.port = 9;
        info.boardType = HPSDRHW::HermesLite;
        info.protocol = ProtocolVersion::Protocol1;
        info.macAddress = QStringLiteral("AA:BB:CC:11:22:33");
        model->connectToRadio(info);
        QVERIFY(observed);
        QVERIFY(!model->localConnectionSetupActive());
        QVERIFY(!model->connection());
        QVERIFY(sessions.replace(core(QStringLiteral("other")), false));
        QVERIFY(original.isNull());
    }

    void invalidTargetAndTransmitStateLeaveCurrentSessionUntouched()
    {
        GuiSessionCoordinator sessions;
        QVERIFY(sessions.replace(core(QStringLiteral("current")), false));
        MainWindow* original = sessions.window();
        ISettingsBackend* originalBackend = AppSettings::instance().remoteBackend();
        const quint64 generation = sessions.generation();
        auto invalid = core(QStringLiteral("invalid"));
        invalid.connection.url = QStringLiteral("https://secret@invalid");
        QString error;
        QVERIFY(!sessions.replace(invalid, true, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!error.contains(QStringLiteral("secret")));
        QCOMPARE(sessions.window(), original);
        QCOMPARE(AppSettings::instance().remoteBackend(), originalBackend);
        QCOMPARE(sessions.generation(), generation);

        // Mirror observations only: no attached radio, PTT request or RF.
        original->radioModel()->transmitModel().setMox(true);
        QVERIFY(!sessions.replace({}, false, &error));
        QCOMPARE(sessions.window(), original);
        original->radioModel()->transmitModel().setMox(false);
        original->radioModel()->transmitModel().setTune(true);
        QVERIFY(!sessions.replace({}, false, &error));
        QCOMPARE(sessions.window(), original);
        original->radioModel()->transmitModel().setTune(false);
        QVERIFY(sessions.replace({}, false));
    }

    void cancelsRetryAndLateOldCoreStateOnSwitch()
    {
        QTemporaryDir firstDir;
        QTemporaryDir secondDir;
        AppSettings firstSettings(firstDir.filePath(QStringLiteral("station.settings")));
        AppSettings secondSettings(secondDir.filePath(QStringLiteral("station.settings")));
        firstSettings.setValue(QStringLiteral("DisplayFftSize"), QStringLiteral("8192"));
        secondSettings.setValue(QStringLiteral("DisplayFftSize"), QStringLiteral("4096"));
        RadioModel firstStation;
        RadioModel secondStation;
        StationServer firstServer(&firstStation, firstSettings, firstDir.path());
        StationServer secondServer(&secondStation, secondSettings, secondDir.path());
        QWebSocketServer firstListener(QStringLiteral("A"), QWebSocketServer::NonSecureMode);
        QWebSocketServer secondListener(QStringLiteral("B"), QWebSocketServer::NonSecureMode);
        QVERIFY(firstListener.listen(QHostAddress::LocalHost, 0));
        QVERIFY(secondListener.listen(QHostAddress::LocalHost, 0));
        connect(&firstListener, &QWebSocketServer::newConnection, &firstServer, [&] {
            firstServer.acceptTransport(new WebSocketTransport(firstListener.nextPendingConnection(),
                StationServer::kMaxIncomingMessageBytes));
        });
        connect(&secondListener, &QWebSocketServer::newConnection, &secondServer, [&] {
            secondServer.acceptTransport(new WebSocketTransport(secondListener.nextPendingConnection(),
                StationServer::kMaxIncomingMessageBytes));
        });
        auto a = core(QStringLiteral("a"), firstListener.serverPort());
        a.connection.token = firstServer.token();
        auto b = core(QStringLiteral("b"), secondListener.serverPort());
        b.connection.token = secondServer.token();
        GuiSessionCoordinator sessions;
        QVERIFY(sessions.replace(a, true));
        QPointer<StationClient> firstClient = sessions.window()->findChild<StationClient*>();
        QVERIFY(firstClient);
        QTRY_VERIFY(firstClient->isHandshakeComplete());
        QCOMPARE(AppSettings::instance().value(QStringLiteral("DisplayFftSize")).toString(),
                 QStringLiteral("8192"));
        firstClient->disconnectFromStation(QStringLiteral("test link failure"), true);
        QVERIFY(firstClient->isReconnectPending());
        QPointer<StatusToast> pendingNotice = sessions.window()->findChild<StatusToast*>();
        QVERIFY(pendingNotice); // The teardown regression requires a live notice.
        // Queue an old-window picker event along with the pending retry.
        QSignalSpy picker(&sessions, &GuiSessionCoordinator::connectionsRequested);
        QVERIFY(QMetaObject::invokeMethod(sessions.window(), "connectionsRequested", Qt::DirectConnection));
        QVERIFY(sessions.replace(b, true));
        QVERIFY(firstClient.isNull());
        QVERIFY(pendingNotice.isNull());
        StationClient* secondClient = sessions.window()->findChild<StationClient*>();
        QVERIFY(secondClient);
        QTRY_VERIFY(secondClient->isHandshakeComplete());
        QCOMPARE(picker.count(), 0);
        QCOMPARE(AppSettings::instance().value(QStringLiteral("DisplayFftSize")).toString(),
                 QStringLiteral("4096"));
        QVERIFY(!secondClient->isReconnectPending());
        QVERIFY(!sessions.window()->radioModel()->isConnected()); // Core online, radio offline.
        sessions.shutdown();
    }

    // R-R3-17: a redial that keeps failing reports the same reason at every
    // backoff step, up to once a minute. The reason stays on screen in the
    // Connections window, Core panel and title bar, so the toasts announce
    // each distinct reason once. Toasts are dismissed between steps so a
    // repeat is counted as a new toast rather than merged into a live one.
    void linkLossToastsOncePerDistinctReason()
    {
        QTemporaryDir stationDir;
        AppSettings stationSettings(stationDir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        StationServer server(&station, stationSettings, stationDir.path());
        QWebSocketServer listener(QStringLiteral("A"), QWebSocketServer::NonSecureMode);
        QVERIFY(listener.listen(QHostAddress::LocalHost, 0));
        connect(&listener, &QWebSocketServer::newConnection, &server, [&] {
            server.acceptTransport(new WebSocketTransport(listener.nextPendingConnection(),
                StationServer::kMaxIncomingMessageBytes));
        });
        auto a = core(QStringLiteral("a"), listener.serverPort());
        a.connection.token = server.token();
        GuiSessionCoordinator sessions;
        QVERIFY(sessions.replace(a, true));
        MainWindow* window = sessions.window();
        StationClient* client = window->findChild<StationClient*>();
        QVERIFY(client);
        QTRY_VERIFY(client->isHandshakeComplete());
        const QString lost = QStringLiteral("Station link lost: ");
        const QString retry = QStringLiteral("Reconnecting to station");
        const auto failure = [&](const QString& reason, int attempt) {
            dismissToasts(window);
            emit client->sessionEnded(reason);
            emit client->reconnectScheduled(attempt, 1000 * attempt);
        };

        failure(QStringLiteral("Remote host closed"), 1);
        QCOMPARE(toastsStartingWith(window, lost), 1);
        QCOMPARE(toastsStartingWith(window, retry), 1);
        // The same reason again, as each backoff step reports it: silent.
        failure(QStringLiteral("Remote host closed"), 2);
        QCOMPARE(toastsStartingWith(window, lost), 0);
        QCOMPARE(toastsStartingWith(window, retry), 0);
        failure(QStringLiteral("Remote host closed"), 3);
        QCOMPARE(toastsStartingWith(window, lost), 0);
        QCOMPARE(toastsStartingWith(window, retry), 0);
        // A different reason is news.
        failure(QStringLiteral("Connection refused"), 4);
        QCOMPARE(toastsStartingWith(window, lost), 1);
        QCOMPARE(toastsStartingWith(window, retry), 1);

        // The real path with the remembered reason stays silent too, and
        // its retry reaches the live Core, whose handshake clears the memory.
        dismissToasts(window);
        client->setReconnectBackoffUnitMs(50);
        client->disconnectFromStation(QStringLiteral("Connection refused"), true);
        QVERIFY(client->isReconnectPending());
        QCOMPARE(toastsStartingWith(window, lost), 0);
        QCOMPARE(toastsStartingWith(window, retry), 0);
        QTRY_VERIFY(client->isHandshakeComplete());
        failure(QStringLiteral("Connection refused"), 1);
        QCOMPARE(toastsStartingWith(window, lost), 1);
        QCOMPARE(toastsStartingWith(window, retry), 1);

        // An operator disconnect clears it as well, without a toast of its own.
        dismissToasts(window);
        QVERIFY(QMetaObject::invokeMethod(window, "disconnectFromStation", Qt::DirectConnection));
        QVERIFY(!client->isConnectionActive());
        QCOMPARE(toastsStartingWith(window, lost), 0);
        failure(QStringLiteral("Connection refused"), 1);
        QCOMPARE(toastsStartingWith(window, lost), 1);
        QCOMPARE(toastsStartingWith(window, retry), 1);
        sessions.shutdown();
    }

    // R-R3-17 fix wave, Important 1: the Connections window's Disconnect /
    // "Cancel retry" and the Core panel's Disconnect reach
    // RemoteConnectionController::disconnectFromStation() directly, not
    // MainWindow::disconnectFromStation(). With a retry pending there is no
    // transport, so sessionEnded never reaches the toast handler. Cancelling
    // the retry must still forget the remembered reason, so the next
    // explicit Connect that fails the same way is announced.
    void cancellingRetryThroughControllerForgetsToastedReason()
    {
        QTemporaryDir stationDir;
        AppSettings stationSettings(stationDir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        StationServer server(&station, stationSettings, stationDir.path());
        QWebSocketServer listener(QStringLiteral("A"), QWebSocketServer::NonSecureMode);
        QVERIFY(listener.listen(QHostAddress::LocalHost, 0));
        connect(&listener, &QWebSocketServer::newConnection, &server, [&] {
            server.acceptTransport(new WebSocketTransport(listener.nextPendingConnection(),
                StationServer::kMaxIncomingMessageBytes));
        });
        auto a = core(QStringLiteral("a"), listener.serverPort());
        a.connection.token = server.token();
        GuiSessionCoordinator sessions;
        QVERIFY(sessions.replace(a, true));
        MainWindow* window = sessions.window();
        StationClient* client = window->findChild<StationClient*>();
        auto* controller = window->findChild<RemoteConnectionController*>();
        QVERIFY(client);
        QVERIFY(controller);
        QTRY_VERIFY(client->isHandshakeComplete());
        const QString lost = QStringLiteral("Station link lost: ");
        const QString retry = QStringLiteral("Reconnecting to station");

        // The link drops and a retry is pending: announced once.
        dismissToasts(window);
        client->setReconnectBackoffUnitMs(60000);
        client->disconnectFromStation(QStringLiteral("Connection refused"), true);
        QVERIFY(client->isReconnectPending());
        QCOMPARE(toastsStartingWith(window, lost), 1);
        QCOMPARE(toastsStartingWith(window, retry), 1);

        // The operator cancels the retry from the Connections window.
        dismissToasts(window);
        controller->disconnectFromStation();
        QVERIFY(!client->isConnectionActive());
        QCOMPARE(toastsStartingWith(window, lost), 0);

        // Their next Connect fails for the same reason: that is news again.
        emit client->sessionEnded(QStringLiteral("Connection refused"));
        emit client->reconnectScheduled(1, 1000);
        QCOMPARE(toastsStartingWith(window, lost), 1);
        QCOMPARE(toastsStartingWith(window, retry), 1);
        sessions.shutdown();
    }

    void replacementKeepsEventLoopAliveAndNormalCloseQuits()
    {
        GuiSessionCoordinator sessions;
        QVERIFY(sessions.replace({}, false));
        const bool originalQuit = QApplication::quitOnLastWindowClosed();
        bool replacementSucceeded = false;
        bool nextEventRan = false;
        QTimer::singleShot(0, &sessions, [&] {
            replacementSucceeded = sessions.replace(core(QStringLiteral("other")), false);
            QTimer::singleShot(0, &sessions, [&] {
                nextEventRan = true;
                QCoreApplication::quit();
            });
        });
        QCoreApplication::exec();
        QVERIFY(replacementSucceeded && nextEventRan);
        QCOMPARE(QApplication::quitOnLastWindowClosed(), originalQuit);
        sessions.shutdown();

        QVERIFY(sessions.replace({}, false));
        bool ordinaryCloseRan = false;
        QTimer watchdog;
        watchdog.setSingleShot(true);
        bool timedOut = false;
        connect(&watchdog, &QTimer::timeout, &sessions, [&] {
            timedOut = true;
            QCoreApplication::quit();
        });
        watchdog.start(5000);
        QTimer::singleShot(0, &sessions, [&] {
            ordinaryCloseRan = sessions.window()->close();
        });
        QCoreApplication::exec();
        QVERIFY(ordinaryCloseRan);
        QVERIFY(!timedOut);
    }
};

QTEST_MAIN(TestGuiSessionCoordinator)
#include "tst_gui_session_coordinator.moc"
