// no-port-check: NereusSDR-original.
// =================================================================
// tests/tst_station_control_socket.cpp  (NereusSDR)
// =================================================================
//
// iPhone app plan Task 17 (R-IOS-08; spec section 9): the Core's console
// socket and the commands it answers, against a Core (DaemonApp) started
// in the test.
//
// Invariants first, each with a red check recorded in task-17-report.md:
//
//   - the socket is owner-only: no group or other permission bit, so a
//     second account on the same computer cannot connect;
//   - its place comes from --config (state_directory) or --profile, never
//     from $HOME;
//   - `reset --unclaimed` without --yes changes nothing;
//   - `token retire` is refused while no device is paired;
//   - `reset --unclaimed --yes` removes every device (moving a damaged
//     list aside first), retires the token, ends every connection, opens
//     pairing unclaimed and prints the new code;
//   - the code never reaches a log line.
//
// Then each subcommand, over the socket; the real entry point finding the
// Core through --profile or --config; and the real nereusd binary finding
// it through --config with no usable $HOME. A Core with no command is
// started by the same code as before (tst_daemon_signals runs it).
//
// Everything listens on loopback or a local socket in a scratch
// directory. Keys are made at run time.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-24: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
//   2026-09-24: Part C fix wave (R1-I1): the last device is not
//               revoked while no token is active. J.J. Boyd (KG4VCF), with
//               AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QMutex>
#include <QProcess>
#include <QScopeGuard>
#include <QTcpServer>
#include <QTemporaryDir>

#include <atomic>
#include <memory>
#include <thread>

#ifdef Q_OS_UNIX
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <cstring>

// The real entry point, run as a helper process below: a Core started the
// way nereusd starts, and console commands sent the way nereusd sends
// them, inside this test's settings sandbox.
#define main nereusdEntryPointForTest
#include "../src/server_main.cpp"
#undef main

#include "core/AppSettings.h"
#include "core/daemon/DaemonApp.h"
#include "core/daemon/DaemonConfig.h"
#include "core/daemon/StationControlCommands.h"
#include "core/daemon/StationControlSocket.h"
#include "core/security/CertificateStore.h"
#include "core/security/DeviceAuthenticator.h"
#include "core/security/DeviceStore.h"
#include "core/security/PairingWindow.h"
#include "core/security/StationIdentity.h"
#include "core/session/SessionMessages.h"
#include "core/session/StationCapabilities.h"
#include "core/session/StationDevicesFacade.h"
#include "core/session/StationServer.h"

#include "OperatorWording.h"
#include "fakes/LoopbackTransport.h"
#include "fakes/UpgradedCoreToken.h"

using namespace NereusSDR;
using NereusSDR::Test::LoopbackTransport;

namespace {

QMutex g_logMutex;
QStringList g_log;
QtMessageHandler g_previousHandler = nullptr;

void captureLog(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    {
        QMutexLocker lock(&g_logMutex);
        g_log.append(message);
    }
    if (g_previousHandler) {
        g_previousHandler(type, context, message);
    }
}

// A device key made at run time.
struct Device {
    QTemporaryDir dir;
    StationIdentity key = StationIdentity::loadOrCreate(dir.path());

    PairedDevice record(const QString& name = QStringLiteral("Shack iPhone")) const
    {
        PairedDevice device;
        device.id = key.fingerprint();
        device.publicKeySpki = key.publicKeySpki();
        device.name = name;
        device.kind = QStringLiteral("phone");
        return device;
    }

    QString id() const { return StationIdentity::toBase64Url(key.fingerprint()); }
};

QJsonObject firstOfType(const QList<QByteArray>& received, const QString& type)
{
    for (const QByteArray& wire : received) {
        const QJsonObject o = QJsonDocument::fromJson(wire).object();
        if (o.value(QStringLiteral("type")).toString() == type) {
            return o;
        }
    }
    return {};
}

int freeLoopbackPort()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0)) {
        return 0;
    }
    const int port = probe.serverPort();
    probe.close();
    return port;
}

// A console command from another thread, as a second nereusd process would
// send it, while this thread's event loop serves the Core.
StationControlReply ask(const QString& path, const QStringList& args)
{
    StationControlReply reply;
    std::atomic<bool> done{false};
    std::thread client([&]() {
        reply = StationControlSocket::request(path, args, 10000);
        done = true;
    });
    [[maybe_unused]] const bool waited1 = QTest::qWaitFor([&done]() { return done.load(); }, 15000);
    client.join();
    return reply;
}

// One Core started in the test: the listener on loopback, no status page,
// its console socket in a scratch state directory.
struct Core {
    QTemporaryDir stateDir;
    DaemonConfig config;
    std::unique_ptr<DaemonApp> app;
    QString socketPath;
    QList<LoopbackTransport*> clients;

    explicit Core(bool remoteOn = true, bool upgradedWithToken = false,
                  const QByteArray& damagedDeviceList = {})
    {
        const QString security = CertificateStore::defaultDirectory();
        QDir(security).removeRecursively();
        if (upgradedWithToken) {
            NereusSDR::Test::seedUpgradedCoreToken(security);
        } else {
            NereusSDR::Test::seedCoreIdentity(security);
        }
        if (!damagedDeviceList.isEmpty()) {
            QFile file(QDir(security).filePath(QString::fromLatin1(DeviceStore::kFileName)));
            if (file.open(QIODevice::WriteOnly)) {
                file.write(damagedDeviceList);
            }
        }
        config = DaemonConfig::defaults();
        config.remotePort = remoteOn ? freeLoopbackPort() : 0;
        config.remoteBind = QStringLiteral("127.0.0.1");
        config.statusPage = false;
        config.stateDirectory = stateDir.path();
        app = std::make_unique<DaemonApp>();
        app->primeBoardForTest(HPSDRHW::HermesLite);
        socketPath = StationControlSocket::socketPathFor(config, AppSettings::profileOverride());
    }

    bool start()
    {
        return app->start(config) && app->startControlSocket(socketPath);
    }

    ~Core()
    {
        app->stop();
        app.reset();
        qDeleteAll(clients);
        QDir(CertificateStore::defaultDirectory()).removeRecursively();
    }

    StationServer* server() const { return app->stationServer(); }

    StationControlReply run(const QStringList& args) const { return ask(socketPath, args); }

    QByteArray certSha256() const
    {
        QString pin = server()->certificateFingerprint();
        pin.remove(QLatin1Char(':'));
        return QByteArray::fromHex(pin.toLatin1());
    }

    LoopbackTransport* open()
    {
        auto* client = new LoopbackTransport(QStringLiteral("app"));
        auto* station = new LoopbackTransport(QStringLiteral("station"));
        station->setPeerAddress(QStringLiteral("192.0.2.7"));
        station->linkTo(client);
        clients.append(client);
        server()->acceptTransport(station);
        [[maybe_unused]] const bool waited2 = QTest::qWaitFor([client]() { return !client->received().isEmpty(); }, 5000);
        return client;
    }

    static bool signIn(LoopbackTransport* client, const SessionMessage& auth)
    {
        client->sendText(SessionMessages::encode(SessionMessages::hello(
            kSessionProtocolMajor, kSessionProtocolMinor, 0, QStringLiteral("NereusSDR iPhone"),
            {kSessionProtocolMajor}, {{"deviceAuth", 1}})));
        client->sendText(SessionMessages::encode(auth));
        [[maybe_unused]] const bool waited3 = QTest::qWaitFor(
            [client]() {
                return client->receivedKinds().contains(QByteArrayLiteral("snapshot.complete"))
                    || !client->isOpen();
            },
            5000);
        return client->isOpen()
               && client->receivedKinds().contains(QByteArrayLiteral("snapshot.complete"));
    }

    LoopbackTransport* deviceSession(const Device& device)
    {
        LoopbackTransport* client = open();
        const QJsonObject hello = firstOfType(client->received(), QStringLiteral("hello"));
        const QByteArray challenge =
            StationIdentity::fromBase64Url(hello.value(QStringLiteral("challenge")).toString());
        const QByteArray stationSpki = server()->stationIdentity().publicKeySpki();
        const SessionDeviceBlock block{
            device.id(), StationIdentity::toBase64Url(device.key.publicKeySpki()),
            QStringLiteral("Shack iPhone"), QStringLiteral("phone"),
            StationIdentity::toBase64Url(device.key.sign(DeviceAuthenticator::transcript(
                challenge, certSha256(), stationSpki, device.key.publicKeySpki())))};
        return signIn(client, SessionMessages::authRequest(QString(), block)) ? client : nullptr;
    }

    LoopbackTransport* tokenSession()
    {
        LoopbackTransport* client = open();
        return signIn(client, SessionMessages::authRequest(server()->token())) ? client : nullptr;
    }
};

bool endedWithCode(LoopbackTransport* client, const QString& code)
{
    [[maybe_unused]] const bool waited4 = QTest::qWaitFor([client]() { return !client->isOpen(); }, 5000);
    const QJsonObject end = firstOfType(client->received(), QStringLiteral("session.end"));
    if (end.value(QStringLiteral("code")).toString() != code) {
        qWarning() << "ended with" << end << "open" << client->isOpen() << client->receivedKinds();
    }
    return !client->isOpen() && end.value(QStringLiteral("code")).toString() == code;
}

// The reply's words, without what is not words: the pairing code (random
// words) and device ids (base64url) are data, not wording.
void verifyPlain(const StationControlReply& reply, const QString& code = QString())
{
    QStringList lines = reply.text.split(QLatin1Char('\n'));
    for (QString& line : lines) {
        if (line.startsWith(QStringLiteral("Pairing code: "))
            || line.startsWith(QStringLiteral("  id "))) {
            line = QStringLiteral("Pairing code");
        }
        if (!code.isEmpty()) {
            line.remove(code);
        }
    }
    const QString text = lines.join(QLatin1Char('\n'));
    QVERIFY2(OperatorWording::isPlain(text), qPrintable(text));
    QVERIFY2(OperatorWording::coreCalledStationIn(text).isEmpty(), qPrintable(text));
}

} // namespace

class TstStationControlSocket : public QObject {
    Q_OBJECT

    QTemporaryDir m_home{QStringLiteral("/tmp/tcs-XXXXXX")};

private slots:
    void initTestCase()
    {
        // A short home: a local socket's name has a short limit, and
        // ctest's own test home is deep. The settings sandbox, and so every
        // profile's directory, lives under it (on macOS and Linux alike),
        // and the helper processes inherit it. Set before anything resolves
        // a settings path.
        QVERIFY(m_home.isValid());
        qputenv("HOME", QFile::encodeName(m_home.path()));
        // macOS resolves the home through CoreFoundation, which ctest points
        // with CFFIXED_USER_HOME (tests/CMakeLists.txt).
        qputenv("CFFIXED_USER_HOME", QFile::encodeName(m_home.path()));
        qunsetenv("XDG_CONFIG_HOME");
        qunsetenv("XDG_DATA_HOME");
        qunsetenv("XDG_CACHE_HOME");
        // The Core's identity and devices live in the profile's directory:
        // one of this test's own, emptied before and after each Core.
        AppSettings::setProfileOverride(
            QStringLiteral("tcs-%1").arg(QCoreApplication::applicationPid()));
    }

    void cleanupTestCase()
    {
        QDir(AppSettings::resolveConfigDir(AppSettings::profileOverride())).removeRecursively();
    }

    // ── Invariants ────────────────────────────────────────────────────

    void theSocketIsOwnerOnly()
    {
        Core core;
        QVERIFY(core.start());
        const QFileInfo info(core.socketPath);
        QVERIFY2(info.exists(), qPrintable(core.socketPath));
        const QFileDevice::Permissions others =
            QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
            | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
        QVERIFY2((info.permissions() & others) == 0,
                 "another account on this computer could open the console socket");
#ifdef Q_OS_UNIX
        struct stat st {};
        QVERIFY(::stat(QFile::encodeName(core.socketPath).constData(), &st) == 0);
        QVERIFY(S_ISSOCK(st.st_mode));
        QCOMPARE(st.st_mode & 077, mode_t(0));
        QCOMPARE(st.st_uid, ::getuid());
#endif
        QVERIFY(core.run({QStringLiteral("status")}).ok);
    }

    void theSocketIsFoundFromConfigOrProfileNeverHome()
    {
        DaemonConfig config = DaemonConfig::defaults();
        config.stateDirectory = QStringLiteral("/var/lib/nereusd");
        const QByteArray home = qgetenv("HOME");
        qputenv("HOME", QByteArrayLiteral("/nonexistent-home-for-this-test"));
        const QString fromConfig = StationControlSocket::socketPathFor(config, QStringLiteral("x"));
        qputenv("HOME", home);
        QCOMPARE(fromConfig, QStringLiteral("/var/lib/nereusd/nereusd-control"));
        QCOMPARE(StationControlSocket::socketPathFor(config, QStringLiteral("other")), fromConfig);

        config.stateDirectory.clear();
        QCOMPARE(StationControlSocket::socketPathFor(config, QStringLiteral("bench")),
                 AppSettings::resolveConfigDir(QStringLiteral("bench"))
                     + QStringLiteral("/nereusd-control"));

        // A Core found through its profile alone.
        Core core;
        core.config.stateDirectory.clear();
        core.socketPath = StationControlSocket::socketPathFor(core.config,
                                                              AppSettings::profileOverride());
        QCOMPARE(core.socketPath, AppSettings::resolveConfigDir(AppSettings::profileOverride())
                                      + QStringLiteral("/nereusd-control"));
        QVERIFY(core.start());
        QVERIFY(core.run({QStringLiteral("status")}).ok);
    }

    void aSecondCoreDoesNotTakeALiveSocket()
    {
        Core core;
        QVERIFY(core.start());
        StationControlSocket second([](const QStringList&) {
            return StationControlReply{true, QStringLiteral("the wrong Core")};
        });
        QVERIFY(!second.listen(core.socketPath));
        QVERIFY(core.run({QStringLiteral("status")}).text.startsWith(QStringLiteral("This Core")));
    }

    void resetWithoutYesChangesNothing()
    {
        Core core(/*remoteOn=*/true, /*upgradedWithToken=*/true);
        QVERIFY(core.start());
        Device device;
        QVERIFY(core.server()->deviceStore()->add(device.record()));
        LoopbackTransport* session = core.deviceSession(device);
        QVERIFY(session != nullptr);
        const StationControlReply reply =
            core.run({QStringLiteral("reset"), QStringLiteral("--unclaimed")});
        QVERIFY(!reply.ok);
        QVERIFY(reply.text.contains(QStringLiteral("Nothing was changed")));
        verifyPlain(reply);
        QCOMPARE(core.server()->deviceStore()->list().size(), 1);
        QVERIFY(core.server()->devicesFacade()->tokenActive());
        QCOMPARE(core.server()->pairingWindow()->state(), PairingWindow::State::ClosedClaimed);
        QVERIFY(session->isOpen());
        // Nor does a reset without --unclaimed.
        QVERIFY(!core.run({QStringLiteral("reset"), QStringLiteral("--yes")}).ok);
        QCOMPARE(core.server()->deviceStore()->list().size(), 1);
    }

    void tokenRetireIsRefusedUntilADeviceIsPaired()
    {
        Core core(/*remoteOn=*/true, /*upgradedWithToken=*/true);
        QVERIFY(core.start());
        const QStringList retire{QStringLiteral("token"), QStringLiteral("retire")};
        StationControlReply reply = core.run(retire);
        QVERIFY(!reply.ok);
        QCOMPARE(reply.text, QStringLiteral("Pair a device with this Core first, so a device can "
                                            "still sign in once the pairing token stops working."));
        QVERIFY(core.server()->devicesFacade()->tokenActive());

        LoopbackTransport* tokenWindow = core.tokenSession();
        QVERIFY(tokenWindow != nullptr);
        Device device;
        QVERIFY(core.server()->deviceStore()->add(device.record()));
        reply = core.run(retire);
        QVERIFY2(reply.ok, qPrintable(reply.text));
        verifyPlain(reply);
        QVERIFY(!core.server()->devicesFacade()->tokenActive());
        QVERIFY(endedWithCode(tokenWindow, QStringLiteral("pairingRequired")));
        // Again: nothing to retire.
        reply = core.run(retire);
        QVERIFY(reply.ok);
        QCOMPARE(reply.text, QStringLiteral("This Core has no pairing token to retire."));
    }

    void resetWithYesEndsEverythingAndOpensUnclaimed()
    {
        const QStringList reset{QStringLiteral("reset"), QStringLiteral("--unclaimed"),
                                QStringLiteral("--yes")};
        // One connection at a time: a device's, then (on a second Core) one
        // signed in with the token.
        {
            Core core(/*remoteOn=*/true, /*upgradedWithToken=*/true);
            QVERIFY(core.start());
            Device first;
            Device second;
            QVERIFY(core.server()->deviceStore()->add(first.record(QStringLiteral("Shack iPhone"))));
            QVERIFY(core.server()->deviceStore()->add(second.record(QStringLiteral("Shack iPad"))));
            LoopbackTransport* deviceWindow = core.deviceSession(first);
            QVERIFY(deviceWindow != nullptr);
            QCOMPARE(core.server()->pairingWindow()->state(), PairingWindow::State::ClosedClaimed);

            const StationControlReply reply = core.run(reset);
            QVERIFY2(reply.ok, qPrintable(reply.text));
            verifyPlain(reply, core.server()->pairingWindow()->currentCode());
            QVERIFY(core.server()->deviceStore()->list().isEmpty());
            QVERIFY(!core.server()->devicesFacade()->tokenActive());
            QVERIFY(!core.server()->deviceStore()->isClaimed());
            QCOMPARE(core.server()->pairingWindow()->state(), PairingWindow::State::OpenUnclaimed);
            const QString code = core.server()->pairingWindow()->currentCode();
            QVERIFY(!code.isEmpty());
            QVERIFY2(reply.text.contains(code), "the reset did not print the new code");
            QVERIFY(endedWithCode(deviceWindow, QStringLiteral("deviceRemoved")));
        }
        {
            Core core(/*remoteOn=*/true, /*upgradedWithToken=*/true);
            QVERIFY(core.start());
            LoopbackTransport* tokenWindow = core.tokenSession();
            QVERIFY(tokenWindow != nullptr);
            const StationControlReply reply = core.run(reset);
            QVERIFY2(reply.ok, qPrintable(reply.text));
            QVERIFY(!core.server()->devicesFacade()->tokenActive());
            QCOMPARE(core.server()->pairingWindow()->state(), PairingWindow::State::OpenUnclaimed);
            QVERIFY(endedWithCode(tokenWindow, QStringLiteral("pairingRequired")));
        }
    }

    void resetMovesADamagedDeviceListAside()
    {
        const QByteArray damaged("{ this is not a device list");
        Core core(/*remoteOn=*/true, /*upgradedWithToken=*/false, damaged);
        QVERIFY(core.start());
        DeviceStore* store = core.server()->deviceStore();
        QVERIFY(!store->isValid());
        QVERIFY(store->isClaimed());
        StationControlReply reply = core.run({QStringLiteral("devices")});
        QVERIFY(!reply.ok);
        verifyPlain(reply);

        reply = core.run(
            {QStringLiteral("reset"), QStringLiteral("--unclaimed"), QStringLiteral("--yes")});
        QVERIFY2(reply.ok, qPrintable(reply.text));
        QVERIFY(store->isValid());
        QVERIFY(!store->isClaimed());
        QCOMPARE(core.server()->pairingWindow()->state(), PairingWindow::State::OpenUnclaimed);
        QVERIFY(reply.text.contains(core.server()->pairingWindow()->currentCode()));
        const QFileInfo list(store->filePath());
        const QStringList aside = list.dir().entryList(
            {QString::fromLatin1(DeviceStore::kFileName) + QStringLiteral(".damaged-*")},
            QDir::Files);
        QCOMPARE(aside.size(), 1);
        QFile kept(list.dir().filePath(aside.first()));
        QVERIFY(kept.open(QIODevice::ReadOnly));
        QCOMPARE(kept.readAll(), damaged);
    }

    void theCodeNeverReachesALogLine()
    {
        {
            QMutexLocker lock(&g_logMutex);
            g_log.clear();
        }
        g_previousHandler = qInstallMessageHandler(captureLog);
        QStringList codes;
        {
            Core core;
            if (core.start()) {
                codes << core.server()->pairingWindow()->currentCode();
                core.run({QStringLiteral("pairing"), QStringLiteral("show")});
                Device device;
                core.server()->deviceStore()->add(device.record());
                core.run({QStringLiteral("pairing"), QStringLiteral("open")});
                codes << core.server()->pairingWindow()->currentCode();
                core.run({QStringLiteral("reset"), QStringLiteral("--unclaimed"),
                          QStringLiteral("--yes")});
                codes << core.server()->pairingWindow()->currentCode();
            }
        }
        qInstallMessageHandler(g_previousHandler);
        codes.removeAll(QString());
        QCOMPARE(codes.size(), 3);
        QMutexLocker lock(&g_logMutex);
        QVERIFY(!g_log.isEmpty());
        for (const QString& line : std::as_const(g_log)) {
            for (const QString& code : std::as_const(codes)) {
                QVERIFY2(!line.contains(code), "a log line holds a pairing code");
                QVERIFY2(!line.contains(code.section(QLatin1Char('-'), 1)),
                         "a log line holds a pairing code's words");
            }
        }
    }

    // ── Each command ──────────────────────────────────────────────────

    void statusSaysWhatTheCoreIs()
    {
        Core core;
        QVERIFY(core.start());
        const StationControlReply reply = core.run({QStringLiteral("status")});
        QVERIFY2(reply.ok, qPrintable(reply.text));
        verifyPlain(reply);
        QVERIFY(reply.text.contains(QStringLiteral("This Core: ")));
        QVERIFY(reply.text.contains(QStringLiteral("Radio: ")));
        QVERIFY(reply.text.contains(QStringLiteral("Remote access: on")));
        QVERIFY(reply.text.contains(QStringLiteral("Paired devices: 0")));
        QVERIFY(reply.text.contains(QStringLiteral("Pairing: open until the first device")));
        QVERIFY(reply.text.contains(QStringLiteral("Status page: off")));
        // The code is for pairing show, not status.
        QVERIFY(!reply.text.contains(core.server()->pairingWindow()->currentCode()));
    }

    void pairingShowOpenAndClose()
    {
        Core core;
        QVERIFY(core.start());
        const QStringList show{QStringLiteral("pairing"), QStringLiteral("show")};
        StationControlReply reply = core.run(show);
        QVERIFY(reply.ok);
        verifyPlain(reply);
        QVERIFY(reply.text.contains(core.server()->pairingWindow()->currentCode()));

        // An unclaimed Core stays open.
        reply = core.run({QStringLiteral("pairing"), QStringLiteral("close")});
        QVERIFY(!reply.ok);
        verifyPlain(reply);
        QCOMPARE(core.server()->pairingWindow()->state(), PairingWindow::State::OpenUnclaimed);

        Device device;
        QVERIFY(core.server()->deviceStore()->add(device.record()));
        reply = core.run(show);
        QVERIFY(reply.ok);
        QVERIFY(reply.text.contains(QStringLiteral("Pairing is closed.")));

        reply = core.run({QStringLiteral("pairing"), QStringLiteral("open")});
        QVERIFY2(reply.ok, qPrintable(reply.text));
        verifyPlain(reply);
        QCOMPARE(core.server()->pairingWindow()->state(), PairingWindow::State::OpenReopened);
        const QString code = core.server()->pairingWindow()->currentCode();
        QVERIFY(!code.isEmpty());
        QVERIFY(reply.text.contains(code));
        QVERIFY(reply.text.contains(QStringLiteral("one more device")));

        reply = core.run({QStringLiteral("pairing"), QStringLiteral("close")});
        QVERIFY(reply.ok);
        QCOMPARE(core.server()->pairingWindow()->state(), PairingWindow::State::ClosedClaimed);
    }

    void devicesListsAndRevokes()
    {
        Core core;
        QVERIFY(core.start());
        StationControlReply reply = core.run({QStringLiteral("devices")});
        QVERIFY(reply.ok);
        QCOMPARE(reply.text, QStringLiteral("No device has paired with this Core."));

        Device device;
        QVERIFY(core.server()->deviceStore()->add(device.record()));
        LoopbackTransport* session = core.deviceSession(device);
        QVERIFY(session != nullptr);
        reply = core.run({QStringLiteral("devices")});
        QVERIFY(reply.ok);
        verifyPlain(reply);
        QVERIFY(reply.text.contains(QStringLiteral("Shack iPhone (phone)")));
        QVERIFY(reply.text.contains(device.id()));
        QVERIFY(reply.text.contains(QStringLiteral("connected now")));

        reply = core.run({QStringLiteral("devices"), QStringLiteral("revoke"),
                          QStringLiteral("not-a-device")});
        QVERIFY(!reply.ok);
        QCOMPARE(core.server()->deviceStore()->list().size(), 1);

        // The last device is not removed while no token is active: the
        // console's reset is how a Core becomes unclaimed (fix wave R1-I1).
        reply = core.run({QStringLiteral("devices"), QStringLiteral("revoke"), device.id()});
        QVERIFY(!reply.ok);
        verifyPlain(reply);
        QCOMPARE(reply.text, QStringLiteral("Pair another device first, or reset this Core from "
                                            "its own computer."));
        QCOMPARE(core.server()->deviceStore()->list().size(), 1);

        Device other;
        QVERIFY(core.server()->deviceStore()->add(other.record(QStringLiteral("Shack iPad"))));
        reply = core.run({QStringLiteral("devices"), QStringLiteral("revoke"), device.id()});
        QVERIFY2(reply.ok, qPrintable(reply.text));
        verifyPlain(reply);
        QVERIFY(reply.text.contains(QStringLiteral("Shack iPhone was removed")));
        QCOMPARE(core.server()->deviceStore()->list().size(), 1);
        QVERIFY(endedWithCode(session, QStringLiteral("deviceRemoved")));
    }

    void remoteAccessOffSaysSoAndChangesNothing()
    {
        Core core(/*remoteOn=*/false);
        QVERIFY(core.start());
        QVERIFY(core.server() == nullptr);
        StationControlReply reply = core.run({QStringLiteral("status")});
        QVERIFY(reply.ok);
        QVERIFY(reply.text.contains(QStringLiteral("Remote access: off")));
        for (const QStringList& args :
             {QStringList{QStringLiteral("pairing"), QStringLiteral("show")},
              QStringList{QStringLiteral("pairing"), QStringLiteral("open")},
              QStringList{QStringLiteral("devices")},
              QStringList{QStringLiteral("token"), QStringLiteral("retire")},
              QStringList{QStringLiteral("reset"), QStringLiteral("--unclaimed"),
                          QStringLiteral("--yes")}}) {
            reply = core.run(args);
            QVERIFY2(!reply.ok, qPrintable(args.join(QLatin1Char(' '))));
            QVERIFY(reply.text.startsWith(QStringLiteral("Remote access is off on this Core")));
            verifyPlain(reply);
        }
    }

    void anUnknownCommandListsTheCommands()
    {
        Core core;
        QVERIFY(core.start());
        const StationControlReply reply = core.run({QStringLiteral("pairing"),
                                                    QStringLiteral("sideways")});
        QVERIFY(!reply.ok);
        QVERIFY(reply.text.contains(QStringLiteral("nereusd reset --unclaimed --yes")));
        verifyPlain(reply);
    }

    void noCoreAnsweringSaysWhatToTry()
    {
        QTemporaryDir empty;
        const StationControlReply reply = StationControlSocket::request(
            QDir(empty.path()).filePath(QStringLiteral("nereusd-control")),
            {QStringLiteral("status")}, 1000);
        QVERIFY(!reply.ok);
        QVERIFY(reply.text.contains(QStringLiteral("sudo")));
        verifyPlain(reply);
    }

    // ── The real binary ───────────────────────────────────────────────

    void theNereusdBinaryFindsTheSocketThroughConfig()
    {
        Core core;
        QVERIFY(core.start());
        QTemporaryDir configDir;
        const QString configPath = configDir.filePath(QStringLiteral("nereusd.conf"));
        QFile config(configPath);
        QVERIFY(config.open(QIODevice::WriteOnly));
        config.write("state_directory = " + core.stateDir.path().toUtf8() + "\n");
        config.close();

        const auto runBinary = [](const QStringList& args, QString* out) {
            QProcess process;
            process.setProgram(QStringLiteral(NEREUSD_BINARY));
            process.setArguments(args);
            // The command reads no $HOME: point it nowhere.
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("HOME"), QStringLiteral("/nonexistent-home-for-this-test"));
            process.setProcessEnvironment(env);
            process.start();
            [[maybe_unused]] const bool waited5 = QTest::qWaitFor([&process]() { return process.state() == QProcess::NotRunning; },
                            30000);
            *out = QString::fromUtf8(process.readAllStandardOutput()
                                     + process.readAllStandardError());
            return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
        };

        QString out;
        QCOMPARE(runBinary({QStringLiteral("--config"), configPath, QStringLiteral("status")},
                           &out),
                 0);
        QVERIFY2(out.contains(QStringLiteral("This Core: ")), qPrintable(out));

        QCOMPARE(runBinary({QStringLiteral("--config"), configPath, QStringLiteral("--profile"),
                            QStringLiteral("anything"), QStringLiteral("pairing"),
                            QStringLiteral("show")},
                           &out),
                 0);
        QVERIFY(out.contains(core.server()->pairingWindow()->currentCode()));

        QCOMPARE(runBinary({QStringLiteral("--config"), configPath, QStringLiteral("reset"),
                            QStringLiteral("--unclaimed")},
                           &out),
                 1);
        QVERIFY(out.contains(QStringLiteral("Nothing was changed")));

        QCOMPARE(runBinary({QStringLiteral("--config"), configPath, QStringLiteral("sideways")},
                           &out),
                 2);
        QVERIFY(out.contains(QStringLiteral("Unknown command.")));
    }
    // The real entry point (server_main.cpp) as a console command: it finds
    // a running Core through --profile alone, or through --config's
    // state_directory. The Core here is this test's; the helper process
    // shares this test's short home, so the profile's directory is the same
    // on both sides.
    void theEntryPointFindsTheCoreThroughProfileOrConfig()
    {
        Core core;
        core.config.stateDirectory.clear();
        core.socketPath = StationControlSocket::socketPathFor(core.config,
                                                              AppSettings::profileOverride());
        QVERIFY(core.start());
        QTemporaryDir configDir;
        const QString noConfig = configDir.filePath(QStringLiteral("absent.conf"));

        const auto command = [](const QStringList& args, QString* out) {
            QProcess process;
            process.setProcessChannelMode(QProcess::MergedChannels);
            process.start(QCoreApplication::applicationFilePath(),
                          QStringList{QStringLiteral("--daemon-helper")} + args);
            [[maybe_unused]] const bool finished = QTest::qWaitFor(
                [&process]() { return process.state() == QProcess::NotRunning; }, 30000);
            *out = QString::fromUtf8(process.readAll());
            return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
        };
        QString out;
        QCOMPARE(command({QStringLiteral("--config"), noConfig, QStringLiteral("--profile"),
                          AppSettings::profileOverride(), QStringLiteral("status")},
                         &out),
                 0);
        QVERIFY2(out.contains(QStringLiteral("This Core: ")), qPrintable(out));
        QCOMPARE(command({QStringLiteral("--config"), noConfig, QStringLiteral("--profile"),
                          AppSettings::profileOverride(), QStringLiteral("pairing"),
                          QStringLiteral("show")},
                         &out),
                 0);
        QVERIFY(out.contains(core.server()->pairingWindow()->currentCode()));
        // Another profile finds no Core, and says what to try.
        QCOMPARE(command({QStringLiteral("--config"), noConfig, QStringLiteral("--profile"),
                          QStringLiteral("elsewhere"), QStringLiteral("status")},
                         &out),
                 1);
        QVERIFY(out.contains(QStringLiteral("sudo")));
        // The command words are checked before anything else.
        QCOMPARE(command({QStringLiteral("--config"), noConfig, QStringLiteral("sideways")}, &out),
                 2);
    }
};

int main(int argc, char* argv[])
{
    if (argc > 1 && std::strcmp(argv[1], "--daemon-helper") == 0) {
        return nereusdEntryPointForTest(argc - 1, argv + 1);
    }
    QCoreApplication app(argc, argv);
    TstStationControlSocket test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_station_control_socket.moc"
