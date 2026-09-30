#pragma once
// no-port-check: NereusSDR-original.
// =================================================================
// tests/RendezvousTestHarness.h  (NereusSDR)
// =================================================================
//
// iPhone app plan Tasks 27 to 29 (R-IOS-08, R-IOS-16): the remote access
// service on this computer (the real Python service, rendezvous/server,
// with the fake STUN/TURN server tests/tools/fake_turn_server.py) and a
// Core with its StationServer, as tst_rendezvous_client stands them up,
// shared with tst_path_racer. A test that includes this defines
// NEREUS_SOURCE_DIR (tests/CMakeLists.txt). Nothing here reaches beyond
// this computer.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-27: moved out of tst_rendezvous_client for Task 29 by J.J.
//               Boyd (KG4VCF), with AI-assisted implementation via
//               Anthropic Claude Code.
//   2026-09-29: LocalService waits for the service's own "listening on"
//               line, fails at once if it exits, and says why with its
//               output (startFailure). J.J. Boyd (KG4VCF), AI-assisted via
//               Anthropic Claude Code.
// =================================================================

#include <QtTest>

#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QProcess>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

#include "core/session/RendezvousDialer.h"
#include <QUrl>

#include <memory>

#ifdef Q_OS_UNIX
#include <pwd.h>
#include <unistd.h>
#endif

#include "core/AppSettings.h"
#include "core/security/ClientDeviceIdentity.h"
#include "core/security/DeviceStore.h"
#include "core/security/StationIdentity.h"
#include "core/session/StationServer.h"
#include "models/RadioModel.h"

#include "fakes/UpgradedCoreToken.h"

namespace NereusSDR::Test::Rendezvous {

inline QString readText(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

/// Task 29 step 2b (the run rule on failures under load): how long a test
/// waits for a session through the service. One dial may take
/// RendezvousDialer::kDialDeadlineMs (the ICE gathering and connectivity
/// deadlines, 69.5 s) before it succeeds or fails, so a flat 60 s budget
/// failed a connect that the code itself still allowed (61.35 s at load
/// 152). The budget is that deadline and 10 s for the sign-in after it.
inline constexpr int kServiceConnectBudgetMs = RendezvousDialer::kDialDeadlineMs + 10000;

inline QByteArray randomBytes(int count)
{
    QByteArray bytes(count, Qt::Uninitialized);
    for (int index = 0; index < count; ++index) {
        bytes[index] = static_cast<char>(QRandomGenerator::system()->bounded(256));
    }
    return bytes;
}

// A P-256 key made at run time in a directory of its own.
struct TestKey {
    QTemporaryDir dir;
    StationIdentity identity = StationIdentity::loadOrCreate(dir.path());

    QByteArray spki() const { return identity.publicKeySpki(); }
    QByteArray sign(const QByteArray& message) const { return identity.sign(message); }
};

// The Python the service runs on, with this user's own packages: ctest
// gives every test a home of its own (tests/CMakeLists.txt), and Python
// finds a user's packages under the home directory, so the service is
// started with the account's real one.
inline QProcessEnvironment pythonEnvironment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
#ifdef Q_OS_UNIX
    if (const passwd* account = getpwuid(getuid()); account != nullptr && account->pw_dir != nullptr) {
        env.insert(QStringLiteral("HOME"), QString::fromLocal8Bit(account->pw_dir));
    }
#endif
    return env;
}

inline quint16 freeTcpPort()
{
    QTcpServer probe;
    probe.listen(QHostAddress::LocalHost, 0);
    return probe.serverPort();
}

class LocalService {
public:
    /// How long a live service may take to be listening. A test executable
    /// gets ctest's default TIMEOUT of 120 s (tests/CMakeLists.txt,
    /// nereus_add_test); half of it leaves the other half for the test
    /// body, so a service that is merely slow to start is reported here,
    /// with its output, before ctest kills the executable with none. A
    /// service that fails exits and is reported at once, and a ready one
    /// returns as soon as it says so, so the bound costs nothing when all
    /// is well.
    static constexpr int kServiceReadyBoundMs = 60000;

    /// `stun`: the service's hello names the fake's STUN server. `relay`:
    /// the service mints relay credentials for the fake's TURN server.
    explicit LocalService(bool stun = true, bool relay = true) : m_stun(stun), m_relay(relay) {}

    /// The fake relay refuses every allocation with 486 (Allocation Quota
    /// Reached), as a full relay does. Before start().
    void setRelayFull(bool full) { m_relayFull = full; }
    /// Task 29 re-review: the service also mints relay grants (rendezvous
    /// section 12.1), with its own relay secret, naming `relayUrl`. Before
    /// start().
    void setRelayGrants(bool on, const QString& relayUrl = QStringLiteral("wss://127.0.0.1:1/v1/relay"))
    {
        m_relayGrants = on;
        m_relayUrl = relayUrl;
    }

    ~LocalService() { stop(); stopTurn(); }

    bool start()
    {
        m_startFailure.clear();
        if ((m_stun || m_relay) && !startTurn()) {
            m_startFailure = QStringLiteral("the fake TURN server did not start");
            return false;
        }
        m_port = freeTcpPort();
        QFile secret(m_dir.filePath(QStringLiteral("turn-secret")));
        if (!secret.open(QIODevice::WriteOnly)) {
            return false;
        }
        secret.write(m_secret);
        secret.close();
        QString relayLines;
        if (m_relayGrants) {
            QFile relaySecret(m_dir.filePath(QStringLiteral("relay-secret")));
            if (!relaySecret.open(QIODevice::WriteOnly)) {
                return false;
            }
            relaySecret.write(randomBytes(32).toHex());
            relaySecret.close();
            relayLines = QStringLiteral("relay_secret_file = %1\nrelay_url = %2\n")
                             .arg(relaySecret.fileName(), m_relayUrl);
        }
        QFile config(m_dir.filePath(QStringLiteral("rendezvous.conf")));
        if (!config.open(QIODevice::WriteOnly)) {
            m_startFailure = QStringLiteral("could not write %1").arg(config.fileName());
            return false;
        }
        const QString stun = m_stun ? QStringLiteral("stun:127.0.0.1:%1").arg(m_turnPort)
                                    : QString();
        const QString turn = m_relay
            ? QStringLiteral("turn:127.0.0.1:%1?transport=udp").arg(m_turnPort)
            : QString();
        config.write(QStringLiteral("[rendezvous]\n"
                                    "listen = 127.0.0.1:%1\n"
                                    "stun_urls = %2\n"
                                    "turn_urls = %3\n"
                                    "turn_secret_file = %4\n"
                                    "log_level = info\n")
                         .arg(m_port)
                         .arg(stun, turn,
                              m_relay ? secret.fileName() : QString())
                         .toUtf8()
                     + relayLines.toUtf8());
        config.close();
        return launch();
    }

    // Starts the service again on the same port, as after a restart.
    //
    // Ready means the service's own "listening on" line (nereus_rendezvous
    // __main__.run logs it once every listening socket is serving), read
    // from its standard error. A process that exits first fails at once,
    // with its output. The bound only governs a service still starting;
    // see kServiceReadyBoundMs.
    bool launch()
    {
        m_startFailure.clear();
        const qsizetype logFrom = m_log.size();
        m_process = std::make_unique<QProcess>();
        QProcessEnvironment env = pythonEnvironment();
        env.insert(QStringLiteral("PYTHONPATH"),
                   QStringLiteral(NEREUS_SOURCE_DIR "/rendezvous/server"));
        env.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
        m_process->setProcessEnvironment(env);
        m_process->setWorkingDirectory(m_dir.path());
        QElapsedTimer elapsed;
        elapsed.start();
        const QDeadlineTimer deadline(kServiceReadyBoundMs);
        m_process->start(QStringLiteral("python3"),
                         {QStringLiteral("-m"), QStringLiteral("nereus_rendezvous"),
                          QStringLiteral("--config"),
                          m_dir.filePath(QStringLiteral("rendezvous.conf"))});
        if (!m_process->waitForStarted(static_cast<int>(deadline.remainingTime()))) {
            m_startFailure = QStringLiteral("python3 did not start: %1")
                                 .arg(m_process->errorString());
            return false;
        }
        while (true) {
            m_log += QString::fromUtf8(m_process->readAllStandardError());
            m_stdout += QString::fromUtf8(m_process->readAllStandardOutput());
            if (m_log.indexOf(QLatin1String("listening on "), logFrom) >= 0) {
                m_readyMs = elapsed.elapsed();
                return true;
            }
            if (m_process->state() == QProcess::NotRunning) {
                m_startFailure = QStringLiteral("the service exited (code %1) before it was "
                                                "listening, after %2 ms")
                                     .arg(m_process->exitCode())
                                     .arg(elapsed.elapsed());
                return false;
            }
            if (deadline.hasExpired()) {
                m_startFailure = QStringLiteral("the service was not listening after %1 ms")
                                     .arg(elapsed.elapsed());
                return false;
            }
            m_process->waitForReadyRead(
                static_cast<int>(qMin<qint64>(100, deadline.remainingTime())));
        }
    }

    /// Why start() or launch() returned false, with everything the service
    /// wrote, for QVERIFY2.
    QString startFailure()
    {
        if (m_process) {
            m_log += QString::fromUtf8(m_process->readAllStandardError());
            m_stdout += QString::fromUtf8(m_process->readAllStandardOutput());
        }
        return QStringLiteral("%1\n--- service stderr ---\n%2\n--- service stdout ---\n%3")
            .arg(m_startFailure.isEmpty() ? QStringLiteral("(no failure recorded)")
                                          : m_startFailure,
                 m_log, m_stdout);
    }

    /// How long the last launch took to be ready.
    qint64 readyMs() const { return m_readyMs; }

    void stop()
    {
        if (m_process) {
            m_log += QString::fromUtf8(m_process->readAllStandardError());
            m_process->terminate();
            if (!m_process->waitForFinished(5000)) {
                m_process->kill();
                m_process->waitForFinished(2000);
            }
            m_log += QString::fromUtf8(m_process->readAllStandardError());
            m_process.reset();
        }
    }

    QString log()
    {
        if (m_process) {
            m_log += QString::fromUtf8(m_process->readAllStandardError());
        }
        return m_log;
    }

    QString turnOutput()
    {
        if (m_turn) {
            m_turnLog += QString::fromUtf8(m_turn->readAllStandardOutput());
        }
        return m_turnLog;
    }

    QUrl url() const { return QUrl(QStringLiteral("ws://127.0.0.1:%1/").arg(m_port)); }
    quint16 port() const { return m_port; }

private:
    bool startTurn()
    {
        QFile secret(m_dir.filePath(QStringLiteral("turn-secret")));
        if (!secret.open(QIODevice::WriteOnly)) {
            return false;
        }
        secret.write(m_secret);
        secret.close();
        const QString portFile = m_dir.filePath(QStringLiteral("turn-port"));
        m_turn = std::make_unique<QProcess>();
        m_turn->setProcessEnvironment(pythonEnvironment());
        QStringList arguments{QStringLiteral(NEREUS_SOURCE_DIR "/tests/tools/fake_turn_server.py"),
                              QStringLiteral("--secret-file"), secret.fileName(),
                              QStringLiteral("--port-file"), portFile};
        if (m_relayFull) {
            arguments.append(QStringLiteral("--quota-full"));
        }
        m_turn->start(QStringLiteral("python3"), arguments);
        if (!m_turn->waitForStarted(10000)) {
            return false;
        }
        QDeadlineTimer deadline(10000);
        while (!QFile::exists(portFile) && !deadline.hasExpired()) {
            QTest::qWait(20);
        }
        m_turnPort = static_cast<quint16>(readText(portFile).toInt());
        return m_turnPort != 0;
    }

    void stopTurn()
    {
        if (m_turn) {
            m_turn->terminate();
            if (!m_turn->waitForFinished(3000)) {
                m_turn->kill();
                m_turn->waitForFinished(2000);
            }
            m_turn.reset();
        }
    }

    bool m_stun = true;
    bool m_relay = true;
    bool m_relayFull = false;
    bool m_relayGrants = false;
    QString m_relayUrl;
    QTemporaryDir m_dir;
    QByteArray m_secret = randomBytes(24).toHex();
    quint16 m_port = 0;
    quint16 m_turnPort = 0;
    std::unique_ptr<QProcess> m_process;
    std::unique_ptr<QProcess> m_turn;
    QString m_log;
    QString m_stdout;
    QString m_startFailure;
    qint64 m_readyMs = -1;
    QString m_turnLog;
};

// One Core with its StationServer, as the pairing tests stand it up.
struct Core {
    QTemporaryDir settingsDir;
    QTemporaryDir securityDir;
    std::unique_ptr<AppSettings> settings;
    std::unique_ptr<RadioModel> model;
    std::unique_ptr<StationServer> server;

    // `upgraded`: a Core from before paired devices, which still signs a
    // window in by its token (a new Core has none). `tokenLike` names
    // another upgraded Core's security directory whose token this one
    // shares, so a test can show the token was never sent to it.
    explicit Core(bool upgraded = false, const QString& tokenLike = QString())
    {
        settings = std::make_unique<AppSettings>(
            settingsDir.filePath(QStringLiteral("NereusSDR.settings")));
        settings->setValue(QStringLiteral("StationCallsign"), QStringLiteral("KG4VCF"));
        model = std::make_unique<RadioModel>();
        const QString security = NereusSDR::Test::seedCoreIdentity(securityDir.path());
        if (!tokenLike.isEmpty()) {
            QFile::copy(QDir(tokenLike).filePath(QStringLiteral("station-token")),
                        QDir(security).filePath(QStringLiteral("station-token")));
        }
        if (upgraded) {
            NereusSDR::Test::seedUpgradedCoreToken(security);
        }
        server = std::make_unique<StationServer>(model.get(), *settings, security);
        server->setHeartbeatIntervalMs(0);
    }

    ~Core() { server.reset(); }

    // This computer, paired as a device the way a pairing leaves it.
    bool pairComputer(const ClientDeviceIdentity& key)
    {
        PairedDevice device;
        device.id = key.fingerprint();
        device.publicKeySpki = key.publicKeySpki();
        device.name = QStringLiteral("Shack MacBook");
        device.kind = QStringLiteral("computer");
        return server->deviceStore()->add(device);
    }

    QUrl url() const
    {
        return QUrl(QStringLiteral("wss://127.0.0.1:%1").arg(server->serverPort()));
    }

    bool pair(const TestKey& key, const QString& name = QStringLiteral("Test phone"))
    {
        PairedDevice device;
        device.id = StationIdentity::fingerprintOf(key.spki());
        device.publicKeySpki = key.spki();
        device.name = name;
        device.kind = QStringLiteral("phone");
        return server->deviceStore()->add(device);
    }
};


} // namespace NereusSDR::Test::Rendezvous
