// no-port-check: NereusSDR-original. R-R3-38 stop message of a remote window.
//
// R-R3-21 / R-R3-23 / R-R3-38, Task 5 of the R3 completion plan: when the
// Core ends a remote window for a reason that will not fix itself, the
// window stays as it is, says what happened in plain words over its
// content, offers the next steps as buttons and does not retry. A dropped
// link still retries as before. Each case runs a real remote MainWindow
// against a loopback Core: the real StationServer for the takeover, and a
// scripted Core that speaks the link's own messages for the refusals.
#include <QTest>
#include <QCoreApplication>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QWebSocket>
#include <QWebSocketServer>

#include "OperatorWording.h"
#include "core/AppSettings.h"
#include "core/RadioDiscovery.h"
#include "core/session/SessionMessages.h"
#include "core/session/SessionTransport.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/settings/SettingsProxy.h"
#include "gui/MainWindow.h"
#include "gui/OperatorReasonText.h"
#include "gui/RemoteConnectionController.h"
#include "models/RadioModel.h"
#include "fakes/MainWindowTestSettings.h"

using namespace NereusSDR;

namespace {

class ScopedRemoteBackend final {
public:
    explicit ScopedRemoteBackend(ISettingsBackend* backend)
    {
        AppSettings::instance().setRemoteBackend(backend);
    }
    ~ScopedRemoteBackend() { AppSettings::instance().setRemoteBackend(nullptr); }
};

// A Core that sends its hello on connect and, when told to, one session
// end after the app's hello: the order StationServer refuses in.
class ScriptedCore final : public QObject {
public:
    explicit ScriptedCore(quint16 helloMajor, QString endReason = {}, bool retryable = false)
        : m_helloMajor(helloMajor), m_endReason(std::move(endReason)), m_retryable(retryable)
    {
        connect(&m_server, &QWebSocketServer::newConnection, this, [this] {
            QWebSocket* socket = m_server.nextPendingConnection();
            socket->setParent(this);
            ++connections;
            socket->sendTextMessage(QString::fromUtf8(SessionMessages::encode(
                SessionMessages::hello(m_helloMajor, kSessionProtocolMinor, 7,
                                       QStringLiteral("scripted core")))));
            connect(socket, &QWebSocket::textMessageReceived, this,
                    [this, socket](const QString&) {
                if (dropWithoutEnd && !socket->property("ended").toBool()) {
                    // The link just goes: no session end, no close frame.
                    socket->setProperty("ended", true);
                    socket->abort();
                    return;
                }
                if (m_endReason.isEmpty() || socket->property("ended").toBool()) { return; }
                socket->setProperty("ended", true);
                socket->sendTextMessage(QString::fromUtf8(SessionMessages::encode(
                    SessionMessages::sessionEnd(m_endReason, m_retryable))));
                socket->close();
            });
        });
    }
    bool listen() { return m_server.listen(QHostAddress::LocalHost, 0); }
    QString url() const
    {
        return QStringLiteral("ws://127.0.0.1:%1").arg(m_server.serverPort());
    }
    int connections = 0;
    // After the app's hello, drop the transport with no session end.
    bool dropWithoutEnd = false;

private:
    QWebSocketServer m_server{QStringLiteral("scripted core"), QWebSocketServer::NonSecureMode};
    quint16 m_helloMajor;
    QString m_endReason;
    bool m_retryable;
};

struct StopBannerView {
    CoreStopBanner* banner = nullptr;
    QLabel* title = nullptr;
    QLabel* text = nullptr;
    QPushButton* takeBack = nullptr;
    QPushButton* chooseCore = nullptr;
    QPushButton* checkUpdates = nullptr;
};

StopBannerView bannerOf(MainWindow& window)
{
    StopBannerView view;
    view.banner = window.findChild<CoreStopBanner*>(QStringLiteral("coreStopBanner"));
    if (view.banner) {
        view.title = view.banner->findChild<QLabel*>(QStringLiteral("coreStopTitle"));
        view.text = view.banner->findChild<QLabel*>(QStringLiteral("coreStopText"));
        view.takeBack = view.banner->findChild<QPushButton*>(QStringLiteral("coreStopTakeBack"));
        view.chooseCore = view.banner->findChild<QPushButton*>(QStringLiteral("coreStopChooseCore"));
        view.checkUpdates =
            view.banner->findChild<QPushButton*>(QStringLiteral("coreStopCheckUpdates"));
    }
    return view;
}

// Every string the stop message shows is in plain words.
void verifyPlain(const StopBannerView& view)
{
    for (const QString& text : {view.title->text(), view.text->text()}) {
        QVERIFY2(OperatorWording::isPlain(text), qPrintable(text));
        QVERIFY2(!text.contains(QChar(0x2014)), qPrintable(text));
    }
    for (QPushButton* button : {view.takeBack, view.chooseCore, view.checkUpdates}) {
        QVERIFY2(OperatorWording::isPlain(button->text()), qPrintable(button->text()));
    }
}

} // namespace

class TestRemoteCoreStopNotice : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        const QString profile = QStringLiteral("remote-core-stop-notice-%1")
                                    .arg(QCoreApplication::applicationPid());
        AppSettings::setProfileOverride(profile);
        QCOMPARE(AppSettings::instance().filePath(), AppSettings::resolveSettingsPath(profile));
        AppSettings::instance().clear();
        QVERIFY(AppSettings::instance().save());
        RadioDiscovery::clearHoldOffForTest();
    }

    void init()
    {
        AppSettings::instance().clear();
        Test::suppressLinuxAudioFirstRun();
        QVERIFY(AppSettings::instance().save());
        RadioDiscovery::clearHoldOffForTest();
    }

    void cleanupTestCase()
    {
        const QString path = AppSettings::instance().filePath();
        QFile::remove(path);
        QFile::remove(path + QStringLiteral(".bak"));
        RadioDiscovery::clearHoldOffForTest();
    }

    // Another app takes the Core over: the window stays, names the other
    // app by the address the Core gives, offers Take it back and Choose
    // another Core, and does not retry. Take it back connects again.
    void takeoverStaysPutAndTakeItBackReconnects()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        StationServer server(&station, settings, dir.path());
        QWebSocketServer listener(QStringLiteral("stop notice test"),
                                  QWebSocketServer::NonSecureMode);
        QVERIFY(listener.listen(QHostAddress::LocalHost, 0));
        connect(&listener, &QWebSocketServer::newConnection, &server, [&] {
            server.acceptTransport(new WebSocketTransport(listener.nextPendingConnection(),
                                                           StationServer::kMaxIncomingMessageBytes));
        });
        const QString url = QStringLiteral("ws://127.0.0.1:%1").arg(listener.serverPort());

        SettingsProxy proxy;
        ScopedRemoteBackend remoteBackend(&proxy);
        MainWindow window({url, server.token(), {}, true}, nullptr,
                          MainWindow::ConnectionStartup::Deferred);
        StationClient* const client = window.findChild<StationClient*>();
        QVERIFY(client);
        client->setReconnectBackoffUnitMs(20);
        window.startInitialConnection();
        QTRY_VERIFY(client->isHandshakeComplete());
        StopBannerView view = bannerOf(window);
        QVERIFY(view.banner && view.title && view.text && view.takeBack
                && view.chooseCore && view.checkUpdates);
        QVERIFY(!view.banner->isVisibleTo(&window));

        // The other app: its own client, same token.
        RadioModel otherModel(RadioModel::Role::Remote);
        SettingsProxy otherProxy;
        StationClient other(&otherModel, &otherProxy);
        RemoteConnectionController otherControls(&other, &otherModel,
                                                 {url, server.token(), {}, true});
        otherControls.connectToStation();
        QTRY_VERIFY(other.isHandshakeComplete());
        QTRY_VERIFY(!client->isConnectionActive());

        QCOMPARE(client->lastEndReport().kind, StationEndReport::Kind::TakenOver);
        QCOMPARE(client->lastEndReport().takenOverBy, QStringLiteral("127.0.0.1"));
        QVERIFY(view.banner->isVisibleTo(&window));
        QCOMPARE(view.title->text(), QStringLiteral("Core taken over"));
        QCOMPARE(view.text->text(),
                 QStringLiteral("Another app at 127.0.0.1 connected to the Core and took "
                                "over. This window does not reconnect by itself. Take it "
                                "back to use the Core here again."));
        QVERIFY(view.takeBack->isVisibleTo(&window));
        // A --station window has no Connections to open.
        QVERIFY(!view.chooseCore->isVisibleTo(&window));
        QVERIFY(!view.checkUpdates->isVisibleTo(&window));
        verifyPlain(view);

        // No retry: well past the first backoff step, nothing dials.
        const quint32 epoch = client->sessionEpoch();
        QTest::qWait(200);
        QVERIFY(!client->isReconnectPending());
        QCOMPARE(client->sessionEpoch(), epoch);
        QVERIFY(other.isHandshakeComplete());

        // A window the connection picker manages: Choose another Core
        // opens Connections.
        window.setConnectionPickerManaged(true);
        QVERIFY(view.chooseCore->isVisibleTo(&window));
        QSignalSpy connections(&window, &MainWindow::connectionsRequested);
        view.chooseCore->click();
        QCOMPARE(connections.size(), 1);
        QVERIFY(!client->isConnectionActive());

        // Take it back connects again, which takes the Core back.
        view.takeBack->click();
        QTRY_VERIFY(client->isHandshakeComplete());
        QVERIFY(!view.banner->isVisibleTo(&window));
        QTRY_VERIFY(!other.isHandshakeComplete());
        QCOMPARE(otherControls.stopNotice(), CoreStopNotice::TakenOver);
    }

    // The Core refuses this app for good because the app's link version is
    // older: the window says to update this app and offers Choose another
    // Core and Check for updates; it does not retry.
    void coreRefusesAnOlderApp()
    {
        const QString reason = QStringLiteral(
            "Protocol major version mismatch: station speaks 2.0, client speaks 1.11. "
            "A differing major means an incompatible wire contract.");
        ScriptedCore core(kSessionProtocolMajor, reason);
        QVERIFY(core.listen());
        SettingsProxy proxy;
        ScopedRemoteBackend remoteBackend(&proxy);
        MainWindow window({core.url(), QStringLiteral("token"), {}, true}, nullptr,
                          MainWindow::ConnectionStartup::Deferred);
        window.setConnectionPickerManaged(true);
        StationClient* const client = window.findChild<StationClient*>();
        QVERIFY(client);
        client->setReconnectBackoffUnitMs(20);
        window.startInitialConnection();
        QTRY_COMPARE(client->lastEndReport().kind, StationEndReport::Kind::VersionRefused);
        QTRY_VERIFY(!client->isConnectionActive());

        const StopBannerView view = bannerOf(window);
        QVERIFY(view.banner);
        QVERIFY(view.banner->isVisibleTo(&window));
        QCOMPARE(view.title->text(), QStringLiteral("Update this app"));
        QCOMPARE(view.text->text(),
                 QStringLiteral("This app is too old to work with this Core. Update "
                                "NereusSDR on this computer to use it. This window does "
                                "not reconnect by itself."));
        QVERIFY(!view.takeBack->isVisibleTo(&window));
        QVERIFY(view.chooseCore->isVisibleTo(&window));
        QVERIFY(view.checkUpdates->isVisibleTo(&window));
        verifyPlain(view);

        QTest::qWait(200);
        QCOMPARE(core.connections, 1);
        QVERIFY(!client->isReconnectPending());
    }

    // The same refusal the other way round: the Core is older, so updating
    // this app does not help and Check for updates is not offered.
    void coreRefusesANewerApp()
    {
        const QString reason = QStringLiteral(
            "Protocol major version mismatch: station speaks 0.9, client speaks 1.11. "
            "A differing major means an incompatible wire contract.");
        ScriptedCore core(kSessionProtocolMajor, reason);
        QVERIFY(core.listen());
        SettingsProxy proxy;
        ScopedRemoteBackend remoteBackend(&proxy);
        MainWindow window({core.url(), QStringLiteral("token"), {}, true}, nullptr,
                          MainWindow::ConnectionStartup::Deferred);
        window.setConnectionPickerManaged(true);
        StationClient* const client = window.findChild<StationClient*>();
        QVERIFY(client);
        client->setReconnectBackoffUnitMs(20);
        window.startInitialConnection();
        QTRY_VERIFY(!client->isConnectionActive());

        const StopBannerView view = bannerOf(window);
        QVERIFY(view.banner->isVisibleTo(&window));
        QCOMPARE(view.title->text(), QStringLiteral("Update the Core"));
        QCOMPARE(view.text->text(),
                 QStringLiteral("This Core is too old to work with this app. Update "
                                "NereusSDR on the Core's computer to use it. This window "
                                "does not reconnect by itself."));
        QVERIFY(view.chooseCore->isVisibleTo(&window));
        QVERIFY(!view.checkUpdates->isVisibleTo(&window));
        QVERIFY(!view.takeBack->isVisibleTo(&window));
        verifyPlain(view);
        QTest::qWait(200);
        QCOMPARE(core.connections, 1);
    }

    // This app refuses a Core whose hello names a newer major: the same
    // message as the Core's own refusal of an older app.
    void appRefusesANewerCore()
    {
        ScriptedCore core(quint16(kSessionProtocolMajor + 1));
        QVERIFY(core.listen());
        SettingsProxy proxy;
        ScopedRemoteBackend remoteBackend(&proxy);
        MainWindow window({core.url(), QStringLiteral("token"), {}, true}, nullptr,
                          MainWindow::ConnectionStartup::Deferred);
        StationClient* const client = window.findChild<StationClient*>();
        QVERIFY(client);
        client->setReconnectBackoffUnitMs(20);
        window.startInitialConnection();
        QTRY_VERIFY(!client->isConnectionActive());
        QCOMPARE(client->lastEndReport().kind, StationEndReport::Kind::VersionRefused);
        QCOMPARE(client->lastEndReport().coreMajor, int(kSessionProtocolMajor + 1));

        const StopBannerView view = bannerOf(window);
        QVERIFY(view.banner->isVisibleTo(&window));
        QCOMPARE(view.title->text(), QStringLiteral("Update this app"));
        verifyPlain(view);
        QTest::qWait(200);
        QCOMPARE(core.connections, 1);
    }

    // Any other end the Core marks not retryable: the Core's reason in
    // plain words and Choose another Core; no retry.
    void otherRefusalShowsTheCoresReason()
    {
        const QString reason = QStringLiteral("undecodable message");
        ScriptedCore core(kSessionProtocolMajor, reason);
        QVERIFY(core.listen());
        SettingsProxy proxy;
        ScopedRemoteBackend remoteBackend(&proxy);
        MainWindow window({core.url(), QStringLiteral("token"), {}, true}, nullptr,
                          MainWindow::ConnectionStartup::Deferred);
        window.setConnectionPickerManaged(true);
        StationClient* const client = window.findChild<StationClient*>();
        QVERIFY(client);
        client->setReconnectBackoffUnitMs(20);
        window.startInitialConnection();
        QTRY_VERIFY(!client->isConnectionActive());
        QCOMPARE(client->lastEndReport().kind, StationEndReport::Kind::Refused);

        const StopBannerView view = bannerOf(window);
        QVERIFY(view.banner->isVisibleTo(&window));
        QCOMPARE(view.title->text(), QStringLiteral("Core refused this window"));
        QCOMPARE(view.text->text(),
                 OperatorReasonText::forDisplay(reason)
                     + QStringLiteral(" This window does not reconnect by itself."));
        QVERIFY(view.text->text() != reason + QStringLiteral(" This window does not "
                                                            "reconnect by itself."));
        QVERIFY(view.chooseCore->isVisibleTo(&window));
        QVERIFY(!view.takeBack->isVisibleTo(&window));
        QVERIFY(!view.checkUpdates->isVisibleTo(&window));
        verifyPlain(view);
        QTest::qWait(200);
        QCOMPARE(core.connections, 1);
    }

    // A dropped link (an end the Core marks retryable) behaves as today:
    // the window retries and says so, and no stop message is shown.
    void droppedLinkStillRetries()
    {
        ScriptedCore core(kSessionProtocolMajor, QStringLiteral("heartbeat timeout"),
                          /*retryable=*/true);
        QVERIFY(core.listen());
        SettingsProxy proxy;
        ScopedRemoteBackend remoteBackend(&proxy);
        MainWindow window({core.url(), QStringLiteral("token"), {}, true}, nullptr,
                          MainWindow::ConnectionStartup::Deferred);
        StationClient* const client = window.findChild<StationClient*>();
        auto* const controls = window.findChild<RemoteConnectionController*>();
        QVERIFY(client && controls);
        client->setReconnectBackoffUnitMs(10000);
        QSignalSpy retries(client, &StationClient::reconnectScheduled);
        window.startInitialConnection();
        QTRY_VERIFY(retries.size() >= 1);
        QVERIFY(client->isReconnectPending());
        QCOMPARE(client->lastEndReport().kind, StationEndReport::Kind::None);
        QCOMPARE(controls->stopNotice(), CoreStopNotice::None);
        QCOMPARE(controls->statusText(), QStringLiteral("Retrying Core (attempt 1)"));
        const StopBannerView view = bannerOf(window);
        QVERIFY(view.banner);
        QVERIFY(!view.banner->isVisibleTo(&window));
        controls->disconnectFromStation();
    }

    // R-R3-38: the transport drops with no session end at all (a network
    // cut, a Core that dies): the window retries and shows no stop message.
    void droppedTransportWithNoEndStillRetries()
    {
        ScriptedCore core(kSessionProtocolMajor);
        core.dropWithoutEnd = true;
        QVERIFY(core.listen());
        SettingsProxy proxy;
        ScopedRemoteBackend remoteBackend(&proxy);
        MainWindow window({core.url(), QStringLiteral("token"), {}, true}, nullptr,
                          MainWindow::ConnectionStartup::Deferred);
        window.setConnectionPickerManaged(true);
        StationClient* const client = window.findChild<StationClient*>();
        auto* const controls = window.findChild<RemoteConnectionController*>();
        QVERIFY(client && controls);
        client->setReconnectBackoffUnitMs(10000);
        QSignalSpy retries(client, &StationClient::reconnectScheduled);
        window.startInitialConnection();
        QTRY_VERIFY(retries.size() >= 1);
        QCOMPARE(core.connections, 1);
        QVERIFY(client->isReconnectPending());
        QCOMPARE(client->lastEndReport().kind, StationEndReport::Kind::None);
        QCOMPARE(controls->stopNotice(), CoreStopNotice::None);
        QCOMPARE(controls->statusText(), QStringLiteral("Retrying Core (attempt 1)"));
        const StopBannerView view = bannerOf(window);
        QVERIFY(view.banner);
        QVERIFY(!view.banner->isVisibleTo(&window));
        controls->disconnectFromStation();
    }
};

QTEST_MAIN(TestRemoteCoreStopNotice)
#include "tst_remote_core_stop_notice.moc"
