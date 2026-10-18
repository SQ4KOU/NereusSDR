#include "gui/StationServiceManager.h"

#include "core/DspCacheLocation.h"
#include "fakes/ServiceEntryWords.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QScopeGuard>
#include <QStringDecoder>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QtTest>

using namespace NereusSDR;

namespace {

// A stand-in account, in the form Windows stores accounts in its own tasks.
const QString kWindowsUser = QStringLiteral("S-1-5-21-1004336348-1177238915-682003330-1001");

QString backslashes(QString path)
{
    return path.replace(QLatin1Char('/'), QLatin1Char('\\'));
}

QByteArray fileBytes(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// The Windows task file as Windows reads it: the bytes, the text they hold
// when read as UTF-16 after the byte order mark, and the values Task
// Scheduler takes from it.
struct WindowsTask {
    QByteArray bytes;
    QString text;
    QString command;
    QString arguments;
    QString triggerUser;
    QString principalUser;
    QString timeLimit;
    QString priority;
    bool wellFormed = false;
};

WindowsTask readWindowsTask(const QString& path)
{
    WindowsTask task;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return task;
    }
    task.bytes = file.readAll();
    if (task.bytes.startsWith(QByteArray::fromHex("fffe"))) {
        QStringDecoder utf16(QStringConverter::Utf16LE);
        task.text = utf16(task.bytes.mid(2));
    }
    // The reader takes the encoding from the bytes and the declaration, so
    // a file whose declaration disagrees with its bytes does not parse.
    QXmlStreamReader reader(task.bytes);
    QStringList open;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            const QString name = reader.name().toString();
            const QString parent = open.isEmpty() ? QString() : open.last();
            if (name == QStringLiteral("Command")) {
                task.command = reader.readElementText();
            } else if (name == QStringLiteral("Arguments")) {
                task.arguments = reader.readElementText();
            } else if (name == QStringLiteral("ExecutionTimeLimit")) {
                task.timeLimit = reader.readElementText();
            } else if (name == QStringLiteral("Priority") && parent == QStringLiteral("Settings")) {
                task.priority = reader.readElementText();
            } else if (name == QStringLiteral("UserId") && parent == QStringLiteral("LogonTrigger")) {
                task.triggerUser = reader.readElementText();
            } else if (name == QStringLiteral("UserId") && parent == QStringLiteral("Principal")) {
                task.principalUser = reader.readElementText();
            } else {
                open << name;
            }
        } else if (reader.isEndElement() && !open.isEmpty()) {
            open.removeLast();
        }
    }
    task.wellFormed = !reader.hasError();
    return task;
}

} // namespace

class StationServiceManagerTest : public QObject {
    Q_OBJECT
private slots:
    void missingConfigRefusesLaunch();
    void macLaunchAgentEscapesPathsAndKeepsServiceOwned();
    void linuxLingerFallbackAndDisable();
    void windowsTaskQuotesArgumentsAndDisablesLogin();
    void windowsTaskFileIsUtf16AndSaysSo();
    void windowsTaskNamesItsUser();
    void windowsTaskUsesBackslashPaths();
    void windowsEmptyDefaultProfileIsExplicit();
    void windowsRegistrationFailureSaysWhy();
    void windowsStartFailureSaysWhy();
    void windowsRefusedChangeKeepsTheLoginSwitch();
    void windowsOlderTaskFileStillReadsLoginStart();
    void windowsAcceptsTheTaskFile();
    void windowsTaskRunsAtNormalPriority();
    void macAndLinuxFailuresSayWhy();
    void stopRefusesFailedStateQuery();
    void macReloadsIdleRegistration();
    void macIdleRegistrationReadsAsStopped();
    void emptyDefaultProfileIsExplicit();
    void pendingStateIsNotStopped();
    void everyEntryNamesTheWindowsOwnFolders();
    void anUnnamedPlanFolderIsTheOneThisProgramUses();
    void anUnusablePlanFolderIsLeftOut();
};

struct Fixture {
    QTemporaryDir temp;
    QString binary;
    QString profileDir;
    // Where the window's own Core keeps its saved FFT plans.
    QString cacheDir;
    QStringList calls;
    bool lingerAllowed = false;
    bool alreadyLingered = false;
    bool active = false;
    bool enabled = false;
    bool taskExists = false;
    bool macLoaded = false;
    bool linuxPresent = false;
    bool queryFails = false;
    QString linuxState;
    QString windowsState;
    QString macState;
    // How launchctl words a registered Core that is not running.
    QString macIdleState = QStringLiteral("waiting");
    // A recorded call starting with failPrefix fails and prints failOutput.
    QString failPrefix;
    QString failOutput;

    Fixture()
    {
        binary = temp.filePath(QStringLiteral("bin with & space/nereusd.exe"));
        profileDir = temp.filePath(QStringLiteral("profile & $ space"));
        cacheDir = temp.filePath(QStringLiteral("plans & $ 100% space"));
        QDir().mkpath(QFileInfo(binary).absolutePath());
        QDir().mkpath(profileDir);
        QFile file(binary);
        if (!file.open(QIODevice::WriteOnly)) qFatal("fixture binary open failed");
        file.write("binary");
        file.close();
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
    void addConfig()
    {
        QFile file(QDir(profileDir).filePath(QStringLiteral("station.conf")));
        if (!file.open(QIODevice::WriteOnly)) qFatal("fixture config open failed");
        file.write("[station]\n");
    }
    StationServiceOptions options(StationPlatform platform)
    {
        StationServiceOptions o;
        o.profile = QStringLiteral("Desk_1");
        o.profileDirectory = profileDir;
        o.dspCacheDirectory = cacheDir;
        o.homeDirectory = temp.filePath(QStringLiteral("home"));
        o.binaryPath = binary;
        o.userName = QStringLiteral("testuser");
        o.userId = platform == StationPlatform::Windows ? kWindowsUser : QStringLiteral("1234");
        o.platform = platform;
        o.runner = [this](const QString& command, const QStringList& args) {
            calls << command + QLatin1Char(' ') + args.join(QLatin1Char('|'));
            if (!failPrefix.isEmpty() && calls.last().startsWith(failPrefix))
                return StationServiceCommandResult{1, failOutput};
            if (queryFails && ((command == QStringLiteral("powershell.exe"))
                               || (command == QStringLiteral("launchctl") && args.contains(QStringLiteral("list")))
                               || (command == QStringLiteral("systemctl") && args.contains(QStringLiteral("show")))))
                return StationServiceCommandResult{1, QStringLiteral("query failed")};
            if (command == QStringLiteral("loginctl") && args.contains(QStringLiteral("show-user")))
                return StationServiceCommandResult{0, alreadyLingered ? QStringLiteral("yes\n") : QStringLiteral("no\n")};
            if (command == QStringLiteral("loginctl"))
                return StationServiceCommandResult{lingerAllowed ? 0 : 1, {}};
            if (command == QStringLiteral("systemctl") && args.contains(QStringLiteral("is-enabled")))
                return StationServiceCommandResult{enabled ? 0 : 1, {}};
            if (command == QStringLiteral("systemctl") && args.contains(QStringLiteral("enable"))) {
                enabled = true;
                return StationServiceCommandResult{0, {}};
            }
            if (command == QStringLiteral("systemctl") && args.contains(QStringLiteral("disable"))) {
                enabled = false;
                return StationServiceCommandResult{0, {}};
            }
            if (command == QStringLiteral("systemctl") && args.contains(QStringLiteral("show")))
                return StationServiceCommandResult{0,
                    linuxPresent ? QStringLiteral("LoadState=loaded\nActiveState=%1\n")
                                        .arg(linuxState.isEmpty()
                                             ? (active ? QStringLiteral("active") : QStringLiteral("inactive"))
                                             : linuxState)
                                 : QStringLiteral("LoadState=not-found\nActiveState=inactive\n")};
            if (command == QStringLiteral("systemctl") && args.contains(QStringLiteral("start"))) {
                active = true;
                linuxPresent = true;
            }
            if (command == QStringLiteral("systemctl") && args.contains(QStringLiteral("stop"))) active = false;
            if (command == QStringLiteral("launchctl") && args.contains(QStringLiteral("list")))
                return StationServiceCommandResult{0, macLoaded
                    ? QStringLiteral("PID\tStatus\tLabel\n-\t0\tcom.boydsoftprez.NereusSDR.station\n")
                    : QStringLiteral("PID\tStatus\tLabel\n")};
            if (command == QStringLiteral("launchctl") && args.contains(QStringLiteral("print")))
                return StationServiceCommandResult{0, QStringLiteral("state = ")
                    + (macState.isEmpty() ? (active ? QStringLiteral("running") : macIdleState)
                                          : macState)};
            if (command == QStringLiteral("launchctl") && args.contains(QStringLiteral("bootstrap"))) macLoaded = true;
            if (command == QStringLiteral("launchctl") && args.contains(QStringLiteral("bootout"))) {
                macLoaded = false;
                active = false;
            }
            if (command == QStringLiteral("launchctl") && args.contains(QStringLiteral("kickstart"))) active = true;
            if (command == QStringLiteral("powershell.exe"))
                return StationServiceCommandResult{0, taskExists ? (windowsState.isEmpty()
                    ? (active ? QStringLiteral("4\n") : QStringLiteral("3\n")) : windowsState)
                                                               : QStringLiteral("-1\n")};
            if (command == QStringLiteral("schtasks") && args.contains(QStringLiteral("/Create")))
                taskExists = true;
            if (command == QStringLiteral("schtasks") && args.contains(QStringLiteral("/Run")))
                active = true;
            if (command == QStringLiteral("schtasks") && args.contains(QStringLiteral("/End")))
                active = false;
            if (command == QStringLiteral("schtasks") && args.contains(QStringLiteral("/Delete"))) {
                taskExists = false;
                active = false;
            }
            return StationServiceCommandResult{0, {}};
        };
        return o;
    }
};

// What follows the program in every entry: the profile, its Core settings
// file, and the two folders the window's own Core uses.
QStringList launchWords(const Fixture& f, const StationServiceManager& manager,
                        const QString& profile = QStringLiteral("Desk_1"))
{
    return {QStringLiteral("--profile"), profile,
            QStringLiteral("--config"), manager.configPath(),
            QStringLiteral("--security-dir"), f.profileDir,
            QStringLiteral("--dsp-cache-dir"), f.cacheDir};
}

// The same words as the Windows task file holds them: paths with
// backslashes, every word in double quotes. None of the fixture's words
// holds a quote or ends in a backslash, so plain quoting is the whole rule.
QString windowsLaunchWords(const Fixture& f, const StationServiceManager& manager,
                           const QString& profile = QStringLiteral("Desk_1"))
{
    QStringList quoted;
    for (const QString& word : launchWords(f, manager, profile)) {
        quoted << QLatin1Char('"') + backslashes(word) + QLatin1Char('"');
    }
    return quoted.join(QLatin1Char(' '));
}

void StationServiceManagerTest::missingConfigRefusesLaunch()
{
    Fixture f;
    StationServiceManager manager(f.options(StationPlatform::MacOS));
    QVERIFY(!manager.startBackground());
    QVERIFY(manager.lastError().contains(QStringLiteral("settings file")));
    QVERIFY(!f.calls.join(QLatin1Char('\n')).contains(QStringLiteral("bootstrap")));
    QVERIFY(!QFileInfo::exists(manager.entryPath()));
}

void StationServiceManagerTest::macLaunchAgentEscapesPathsAndKeepsServiceOwned()
{
    Fixture f;
    f.addConfig();
    StationServiceManager manager(f.options(StationPlatform::MacOS));
    QVERIFY(manager.setStartWithComputer(true));
    QCOMPARE(manager.startupMode(), StationServiceManager::StartupMode::Login);
    QVERIFY(manager.startsWithComputer());
    QVERIFY(manager.startBackground());
    QFile file(manager.entryPath());
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray xml = file.readAll();
    file.close(); // Windows cannot replace or remove a file that is open
    QVERIFY(xml.contains("bin with &amp; space"));
    QVERIFY(xml.contains("profile &amp; $ space/station.conf"));
    QVERIFY(xml.contains("<key>RunAtLoad</key><true/>"));
    QXmlStreamReader macXml(xml);
    while (!macXml.atEnd()) macXml.readNext();
    QVERIFY(!macXml.hasError());
    QVERIFY(f.calls.join(QLatin1Char('\n')).contains(QStringLiteral("launchctl bootstrap|gui/1234")));
    QVERIFY(f.calls.join(QLatin1Char('\n')).contains(QStringLiteral("launchctl kickstart|gui/1234/")));
    QVERIFY(manager.setStartWithComputer(false));
    QVERIFY(!manager.startsWithComputer());
    QCOMPARE(manager.startupMode(), StationServiceManager::StartupMode::Disabled);
    QVERIFY(!QFileInfo::exists(manager.entryPath()));
}

void StationServiceManagerTest::linuxLingerFallbackAndDisable()
{
    Fixture f;
    f.addConfig();
    auto options = f.options(StationPlatform::Linux);
    options.prefixArguments = {QStringLiteral("--nereus-station")};
    StationServiceManager manager(options);
    QVERIFY(manager.setStartWithComputer(true));
    QCOMPARE(manager.startupMode(), StationServiceManager::StartupMode::Login);
    QVERIFY(manager.lastError().contains(QStringLiteral("login")));
    QVERIFY(manager.startBackground());
    QFile file(manager.entryPath());
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray unit = file.readAll();
    file.close(); // Windows cannot replace or remove a file that is open
    QVERIFY(unit.contains("ExecStart=\""));
    QVERIFY(unit.contains("\"--nereus-station\""));
    QVERIFY(unit.contains("\"--profile\" \"Desk_1\""));
    QVERIFY(unit.contains("profile & $$ space/station.conf"));
    QVERIFY(f.calls.join(QLatin1Char('\n')).contains(QStringLiteral("systemctl --user|start|nereusd.service")));
    f.lingerAllowed = true;
    QVERIFY(manager.setStartWithComputer(true));
    QCOMPARE(manager.startupMode(), StationServiceManager::StartupMode::Boot);
    QVERIFY(f.calls.join(QLatin1Char('\n')).contains(QStringLiteral("loginctl --no-ask-password|enable-linger|testuser")));
    f.alreadyLingered = true;
    f.lingerAllowed = false;
    const qsizetype priorCalls = f.calls.size();
    QVERIFY(manager.setStartWithComputer(true));
    QCOMPARE(manager.startupMode(), StationServiceManager::StartupMode::Boot);
    QVERIFY(!f.calls.mid(priorCalls).join(QLatin1Char('\n')).contains(QStringLiteral("enable-linger")));
    QVERIFY(manager.setStartWithComputer(false));
    QCOMPARE(manager.startupMode(), StationServiceManager::StartupMode::Disabled);
    QVERIFY(!QFileInfo::exists(manager.entryPath()));
    QVERIFY(f.calls.join(QLatin1Char('\n')).contains(QStringLiteral("systemctl --user|disable|nereusd.service")));
}

void StationServiceManagerTest::windowsTaskQuotesArgumentsAndDisablesLogin()
{
    Fixture f;
    f.addConfig();
    StationServiceManager manager(f.options(StationPlatform::Windows));
    QVERIFY(manager.setStartWithComputer(true));
    QVERIFY(manager.startsWithComputer());
    QVERIFY(manager.startBackground());
    const WindowsTask task = readWindowsTask(manager.entryPath());
    QVERIFY(task.wellFormed);
    QVERIFY(task.text.contains(QStringLiteral("<Command>")));
    QVERIFY(task.text.contains(QStringLiteral("&quot;Desk_1&quot;")));
    QVERIFY(task.text.contains(QStringLiteral("profile &amp; $ space\\station.conf")));
    QCOMPARE(task.timeLimit, QStringLiteral("PT0S"));
    QVERIFY(f.calls.join(QLatin1Char('\n')).contains(QStringLiteral("schtasks /Create|/F|/TN|NereusSDR Station|/XML|")));
    QVERIFY(f.calls.join(QLatin1Char('\n')).contains(QStringLiteral("schtasks /Run|/TN|NereusSDR Station")));
    const qsizetype priorStartCalls = f.calls.size();
    QVERIFY(manager.startBackground());
    QVERIFY(!f.calls.mid(priorStartCalls).join(QLatin1Char('\n')).contains(QStringLiteral("/Run")));
    QVERIFY(manager.setStartWithComputer(false));
    QVERIFY(!manager.startsWithComputer());
    QVERIFY(QFileInfo::exists(manager.entryPath()));
    QVERIFY(f.taskExists);
    QVERIFY(f.calls.join(QLatin1Char('\n')).contains(QStringLiteral("schtasks /Create|/F|/TN|NereusSDR Station|/XML|")));
    QVERIFY(manager.stopBackground());
    QVERIFY(f.calls.join(QLatin1Char('\n')).contains(QStringLiteral("powershell.exe -NoProfile|-NonInteractive|-Command|")));
    QVERIFY(!f.calls.join(QLatin1Char('\n')).contains(QStringLiteral("Status: Running")));
    QVERIFY(!f.active);
    QVERIFY(manager.stopBackground());
}

void StationServiceManagerTest::stopRefusesFailedStateQuery()
{
    for (const auto platform : {StationPlatform::MacOS, StationPlatform::Linux, StationPlatform::Windows}) {
        Fixture f;
        f.queryFails = true;
        f.active = true;
        f.macLoaded = true;
        f.linuxPresent = true;
        f.taskExists = true;
        StationServiceManager manager(f.options(platform));
        QVERIFY(!manager.stopBackground());
        QVERIFY(manager.lastError().contains(QStringLiteral("check")));
        const QString calls = f.calls.join(QLatin1Char('\n'));
        QVERIFY(!calls.contains(QStringLiteral("bootout")));
        QVERIFY(!calls.contains(QStringLiteral("|stop|")));
        QVERIFY(!calls.contains(QStringLiteral("/End")));
    }
}

void StationServiceManagerTest::macReloadsIdleRegistration()
{
    Fixture f;
    f.addConfig();
    f.macLoaded = true;
    StationServiceManager manager(f.options(StationPlatform::MacOS));
    QVERIFY(manager.startBackground());
    const QString calls = f.calls.join(QLatin1Char('\n'));
    const qsizetype bootout = calls.indexOf(QStringLiteral("launchctl bootout|"));
    const qsizetype bootstrap = calls.indexOf(QStringLiteral("launchctl bootstrap|"));
    const qsizetype kickstart = calls.indexOf(QStringLiteral("launchctl kickstart|"));
    QVERIFY(bootout >= 0);
    QVERIFY(bootstrap > bootout);
    QVERIFY(kickstart > bootstrap);
    const qsizetype previous = f.calls.size();
    QVERIFY(manager.startBackground());
    QVERIFY(!f.calls.mid(previous).join(QLatin1Char('\n')).contains(QStringLiteral("bootout")));
}

// A registered Core that is not running reads "state = not running" on
// current macOS (every idle agent on macOS 27.0.1 does) and "state =
// waiting" on older releases. Both mean stopped: it can be started, and
// there is nothing to stop. Read as "changing state", the Core would start
// once and never again after the window had taken the radio back.
void StationServiceManagerTest::macIdleRegistrationReadsAsStopped()
{
    for (const QString& idle : {QStringLiteral("not running"), QStringLiteral("waiting")}) {
        Fixture f;
        f.addConfig();
        f.macLoaded = true;
        f.macIdleState = idle;
        StationServiceManager manager(f.options(StationPlatform::MacOS));
        QVERIFY(!manager.isBackgroundRunning());
        QVERIFY2(manager.stopBackground(), qPrintable(idle + QStringLiteral(": ") + manager.lastError()));
        QVERIFY2(manager.startBackground(), qPrintable(idle + QStringLiteral(": ") + manager.lastError()));
        const QString calls = f.calls.join(QLatin1Char('\n'));
        const qsizetype bootout = calls.indexOf(QStringLiteral("launchctl bootout|"));
        const qsizetype bootstrap = calls.indexOf(QStringLiteral("launchctl bootstrap|"));
        const qsizetype kickstart = calls.indexOf(QStringLiteral("launchctl kickstart|"));
        QVERIFY(bootout >= 0);
        QVERIFY(bootstrap > bootout);
        QVERIFY(kickstart > bootstrap);
        QVERIFY(manager.isBackgroundRunning());
    }
}

void StationServiceManagerTest::emptyDefaultProfileIsExplicit()
{
    Fixture f;
    f.addConfig();
    auto options = f.options(StationPlatform::Linux);
    options.inheritActiveProfile = false;
    options.profile.clear();
    StationServiceManager manager(options);
    QVERIFY(manager.startBackground());
    QFile file(manager.entryPath());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(file.readAll().contains("\"--profile\" \"\""));
}

void StationServiceManagerTest::pendingStateIsNotStopped()
{
    for (const QString& state : {QStringLiteral("activating"), QStringLiteral("deactivating")}) {
        Fixture f;
        f.addConfig();
        f.linuxPresent = true;
        f.linuxState = state;
        StationServiceManager manager(f.options(StationPlatform::Linux));
        QVERIFY(!manager.stopBackground());
        QVERIFY(manager.lastError().contains(QStringLiteral("changing state")));
        QVERIFY(!manager.startBackground());
        QVERIFY(!f.calls.join(QLatin1Char('\n')).contains(QStringLiteral("|start|")));
        QVERIFY(!f.calls.join(QLatin1Char('\n')).contains(QStringLiteral("|stop|")));
    }
    {
        Fixture f;
        f.addConfig();
        f.taskExists = true;
        f.windowsState = QStringLiteral("2\n"); // TASK_STATE_QUEUED
        StationServiceManager manager(f.options(StationPlatform::Windows));
        QVERIFY(!manager.stopBackground());
        QVERIFY(!manager.startBackground());
        QVERIFY(manager.lastError().contains(QStringLiteral("changing state")));
        QVERIFY(!f.calls.join(QLatin1Char('\n')).contains(QStringLiteral("/End")));
        QVERIFY(!f.calls.join(QLatin1Char('\n')).contains(QStringLiteral("/Create")));
    }
    {
        Fixture f;
        f.addConfig();
        f.macLoaded = true;
        f.macState = QStringLiteral("spawning");
        StationServiceManager manager(f.options(StationPlatform::MacOS));
        QVERIFY(!manager.stopBackground());
        QVERIFY(!manager.startBackground());
        QVERIFY(!f.calls.join(QLatin1Char('\n')).contains(QStringLiteral("bootout")));
    }
}

// Task Scheduler takes a task definition as a UTF-16 string ([MS-TSCH]
// 3.2.5.4.2 SchRpcRegisterTask: the xml parameter is a wchar_t string), so
// its reader refuses a definition that declares any other encoding. On
// Windows 11 a UTF-8 declaration gets "(1,40)::ERROR: unable to switch the
// encoding" and no task; "UTF-16" and no declaration are both accepted.
// Windows writes its own task files as UTF-16 with a byte order mark and
// that declaration, which is the form checked here.
void StationServiceManagerTest::windowsTaskFileIsUtf16AndSaysSo()
{
    Fixture f;
    f.addConfig();
    StationServiceManager manager(f.options(StationPlatform::Windows));
    QVERIFY(manager.startBackground());
    const WindowsTask task = readWindowsTask(manager.entryPath());
    QVERIFY2(task.bytes.startsWith(QByteArray::fromHex("fffe")),
             "the task file has no UTF-16 byte order mark");
    QCOMPARE(task.bytes.size() % 2, 0);
    QVERIFY2(task.text.startsWith(QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-16\"?>")),
             qPrintable(task.text.left(60)));
    QVERIFY(!task.text.contains(QStringLiteral("UTF-8")));
    QVERIFY(task.text.endsWith(QStringLiteral("</Task>\n")));
    QVERIFY(task.wellFormed);
}

// A logon trigger with no UserId starts the task for any user at logon, and
// only an administrator may register that ([MS-TSCH] 3.2.5.4.2: access is
// denied to a caller without administrative privileges for a logon trigger
// for everyone or for another user). The app runs without them, so the
// trigger and the principal both name the account that registers the task.
void StationServiceManagerTest::windowsTaskNamesItsUser()
{
    Fixture f;
    f.addConfig();
    StationServiceManager manager(f.options(StationPlatform::Windows));
    QVERIFY(manager.setStartWithComputer(true));
    const WindowsTask task = readWindowsTask(manager.entryPath());
    QVERIFY(task.wellFormed);
    QCOMPARE(task.triggerUser, kWindowsUser);
    QCOMPARE(task.principalUser, kWindowsUser);
    // The order Windows 11 accepted: the trigger's own fields follow the
    // ones every trigger has.
    QVERIFY(task.text.contains(QStringLiteral("<LogonTrigger><Enabled>true</Enabled><UserId>%1</UserId>"
                                              "</LogonTrigger>").arg(kWindowsUser)));
    QVERIFY(task.text.contains(QStringLiteral("<Principal id=\"Author\"><UserId>%1</UserId>"
                                              "<LogonType>InteractiveToken</LogonType>"
                                              "<RunLevel>LeastPrivilege</RunLevel></Principal>")
                                   .arg(kWindowsUser)));
}

// Windows paths are written with backslashes, in the task and on the
// schtasks command line (whose own options start with a forward slash). The
// program path is not quoted: Task Scheduler keeps it apart from the
// arguments, and Windows' own tasks store paths with spaces the same way.
void StationServiceManagerTest::windowsTaskUsesBackslashPaths()
{
    Fixture f;
    f.addConfig();
    StationServiceManager manager(f.options(StationPlatform::Windows));
    QVERIFY(manager.startBackground());
    const WindowsTask task = readWindowsTask(manager.entryPath());
    QVERIFY(task.wellFormed);
    QCOMPARE(task.command, backslashes(f.binary));
    QVERIFY(task.command.contains(QStringLiteral("\\bin with & space\\nereusd.exe")));
    QCOMPARE(task.arguments, windowsLaunchWords(f, manager));
    QVERIFY(!task.arguments.contains(QLatin1Char('/')));
    const QString create = QStringLiteral("schtasks /Create|/F|/TN|NereusSDR Station|/XML|");
    QStringList created;
    for (const QString& call : f.calls) {
        if (call.startsWith(create)) { created << call.mid(create.size()); }
    }
    QCOMPARE(created, QStringList{backslashes(manager.entryPath())});
}

// The background Core reads the window's own default profile only when it
// is told so with an empty --profile value, so the empty argument stays.
void StationServiceManagerTest::windowsEmptyDefaultProfileIsExplicit()
{
    Fixture f;
    f.addConfig();
    auto options = f.options(StationPlatform::Windows);
    options.inheritActiveProfile = false;
    options.profile.clear();
    StationServiceManager manager(options);
    QVERIFY(manager.startBackground());
    const WindowsTask task = readWindowsTask(manager.entryPath());
    QVERIFY(task.wellFormed);
    QVERIFY2(task.arguments.startsWith(QStringLiteral("\"--profile\" \"\" \"--config\" \"")),
             qPrintable(task.arguments));
}

void StationServiceManagerTest::windowsRegistrationFailureSaysWhy()
{
    Fixture f;
    f.addConfig();
    f.failPrefix = QStringLiteral("schtasks /Create");
    f.failOutput = QStringLiteral("ERROR: The task XML contains a value which is incorrectly "
                                  "formatted or out of range.\r\n(1,40)::ERROR: unable to switch "
                                  "the encoding\r\n\r\n");
    StationServiceManager manager(f.options(StationPlatform::Windows));
    QVERIFY(!manager.startBackground());
    QCOMPARE(manager.lastError(),
             QStringLiteral("Could not register the background Core with Windows. Windows reported: "
                            "ERROR: The task XML contains a value which is incorrectly formatted or "
                            "out of range. (1,40)::ERROR: unable to switch the encoding"));
    QVERIFY(!f.calls.join(QLatin1Char('\n')).contains(QStringLiteral("/Run")));
    // Nothing was registered, so no file may say that a login start was.
    QVERIFY(!QFileInfo::exists(manager.entryPath()));
    QVERIFY(!manager.startsWithComputer());

    QVERIFY(!manager.setStartWithComputer(true));
    QVERIFY(manager.lastError().startsWith(QStringLiteral("Could not change the Core login task. "
                                                           "Windows reported: ERROR: The task XML")));
    QVERIFY(!QFileInfo::exists(manager.entryPath()));
    QVERIFY(!manager.startsWithComputer());

    // A tool that prints nothing leaves the sentence alone.
    f.failOutput.clear();
    QVERIFY(!manager.startBackground());
    QCOMPARE(manager.lastError(),
             QStringLiteral("Could not register the background Core with Windows."));
}

void StationServiceManagerTest::windowsStartFailureSaysWhy()
{
    Fixture f;
    f.addConfig();
    f.failPrefix = QStringLiteral("schtasks /Run");
    f.failOutput = QStringLiteral("ERROR: Access is denied.\r\n");
    StationServiceManager manager(f.options(StationPlatform::Windows));
    QVERIFY(!manager.startBackground());
    QCOMPARE(manager.lastError(),
             QStringLiteral("Could not start the background Core at this login. "
                            "Windows reported: ERROR: Access is denied."));
    // The task itself was registered, so its file stays.
    QVERIFY(f.taskExists);
    QVERIFY(QFileInfo::exists(manager.entryPath()));

    f.failPrefix = QStringLiteral("schtasks /End");
    f.active = true;
    QVERIFY(!manager.stopBackground());
    QCOMPARE(manager.lastError(),
             QStringLiteral("Could not stop the background Core. "
                            "Windows reported: ERROR: Access is denied."));

    f.failPrefix = QStringLiteral("powershell.exe");
    f.failOutput = QStringLiteral("The Task Scheduler service is not available.\r\n");
    QVERIFY(!manager.startBackground());
    QCOMPARE(manager.lastError(),
             QStringLiteral("Could not check the background Core state. "
                            "Windows reported: The Task Scheduler service is not available."));
}

// The task file is also what the login switch reads. A change Windows
// refused leaves the file as the registered task still is.
void StationServiceManagerTest::windowsRefusedChangeKeepsTheLoginSwitch()
{
    Fixture f;
    f.addConfig();
    StationServiceManager manager(f.options(StationPlatform::Windows));
    QVERIFY(manager.setStartWithComputer(true));
    QVERIFY(manager.startsWithComputer());
    const QByteArray registered = readWindowsTask(manager.entryPath()).bytes;

    f.failPrefix = QStringLiteral("schtasks /Create");
    f.failOutput = QStringLiteral("ERROR: Access is denied.\r\n");
    QVERIFY(!manager.setStartWithComputer(false));
    QVERIFY(manager.lastError().contains(QStringLiteral("Windows reported: ERROR: Access is denied.")));
    QVERIFY(manager.startsWithComputer());
    QCOMPARE(readWindowsTask(manager.entryPath()).bytes, registered);
}

// A task file from a release that wrote UTF-8 still says what the login
// switch was set to, and the next start replaces it.
void StationServiceManagerTest::windowsOlderTaskFileStillReadsLoginStart()
{
    Fixture f;
    f.addConfig();
    StationServiceManager manager(f.options(StationPlatform::Windows));
    QVERIFY(QDir().mkpath(QFileInfo(manager.entryPath()).absolutePath()));
    QFile older(manager.entryPath());
    QVERIFY(older.open(QIODevice::WriteOnly));
    older.write("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Task version=\"1.4\" "
                "xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\"><Triggers>"
                "<LogonTrigger><Enabled>true</Enabled></LogonTrigger></Triggers></Task>\n");
    older.close();
    QVERIFY(manager.startsWithComputer());
    QVERIFY(manager.startBackground());
    const WindowsTask task = readWindowsTask(manager.entryPath());
    QVERIFY(task.bytes.startsWith(QByteArray::fromHex("fffe")));
    QCOMPARE(task.triggerUser, kWindowsUser);
    QVERIFY(manager.startsWithComputer());
}

// On Windows the file goes to Task Scheduler's own reader, so a build that
// writes a file Windows would refuse fails here. The flag 1 is
// TASK_VALIDATE_ONLY in Microsoft's TASK_CREATION enumeration: the service
// checks the definition and registers no task ([MS-TSCH] 3.2.5.4.2 returns
// success for that flag once the definition has parsed). Nothing is
// registered, started, changed or removed.
void StationServiceManagerTest::windowsAcceptsTheTaskFile()
{
#if defined(Q_OS_WIN)
    Fixture f;
    f.addConfig();
    auto options = f.options(StationPlatform::Windows);
    options.userId.clear(); // the manager asks Windows who this account is
    StationServiceManager manager(options);
    QVERIFY(manager.setStartWithComputer(true));
    const WindowsTask task = readWindowsTask(manager.entryPath());
    QVERIFY(task.wellFormed);
    QVERIFY2(task.triggerUser.startsWith(QStringLiteral("S-1-")), qPrintable(task.triggerUser));
    QCOMPARE(task.principalUser, task.triggerUser);

    // The path reaches the script through the environment, never as script
    // text. 3 is TASK_LOGON_INTERACTIVE_TOKEN, the logon type the file asks
    // for. After the check the script reads the definition back, so the
    // program and arguments compared below are the ones Windows understood.
    const QString script = QStringLiteral(
        "$ErrorActionPreference='Stop'; "
        "try { $s=New-Object -ComObject 'Schedule.Service'; $s.Connect() } "
        "catch { [Console]::Out.WriteLine('NO-SERVICE ' + $_.Exception.Message); exit 3 }; "
        "try { $x=[System.IO.File]::ReadAllText($env:NEREUS_TASK_FILE); "
        "$null=$s.GetFolder('\\').RegisterTask('NereusSDR task file check', $x, 1, $null, $null, 3); "
        "$d=$s.NewTask(0); $d.XmlText=$x; $a=$d.Actions.Item(1); "
        "[Console]::Out.WriteLine('ACCEPTED'); "
        "[Console]::Out.WriteLine('PATH=' + $a.Path); "
        "[Console]::Out.WriteLine('ARGS=' + $a.Arguments); "
        "[Console]::Out.WriteLine('PRIORITY=' + $d.Settings.Priority) } "
        "catch { [Console]::Out.WriteLine('REFUSED ' + $_.Exception.Message); exit 1 }");
    QProcess powershell;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("NEREUS_TASK_FILE"), backslashes(manager.entryPath()));
    powershell.setProcessEnvironment(environment);
    powershell.start(QStringLiteral("powershell.exe"),
                     {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                      QStringLiteral("-Command"), script});
    QVERIFY2(powershell.waitForStarted(30000), qPrintable(powershell.errorString()));
    QVERIFY(powershell.waitForFinished(60000));
    const QString said = QString::fromLocal8Bit(powershell.readAllStandardOutput()
                                                + powershell.readAllStandardError());
    if (powershell.exitCode() == 3) {
        QSKIP(qPrintable(QStringLiteral("Task Scheduler is not reachable here: ") + said.simplified()));
    }
    QStringList lines;
    for (const QString& line : said.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        lines << line.trimmed();
    }
    QVERIFY2(powershell.exitCode() == 0 && lines.value(0) == QStringLiteral("ACCEPTED"),
             qPrintable(said.simplified()));
    QCOMPARE(lines.value(1), QStringLiteral("PATH=") + backslashes(f.binary));
    QCOMPARE(lines.value(2), QStringLiteral("ARGS=") + windowsLaunchWords(f, manager));
    QCOMPARE(lines.value(3), QStringLiteral("PRIORITY=4"));
#else
    QSKIP("Task Scheduler's own reader exists only on Windows.");
#endif
}

// A task's program starts at the task's Priority, and a definition that
// names none gets 7: BELOW_NORMAL_PRIORITY_CLASS with low I/O and memory
// priority. The same Core started from a shortcut runs at the normal class,
// and a Core doing the receive DSP below it can fall behind when the
// computer is busy. 4 is NORMAL_PRIORITY_CLASS with normal I/O and normal memory
// priority, as a program started from a shortcut gets (Microsoft Learn,
// "Priority (settingsType) Element"). Every path that writes the task file
// asks for it; each starts here from no file, so a path that wrote none
// cannot pass on a file another path left.
void StationServiceManagerTest::windowsTaskRunsAtNormalPriority()
{
    Fixture f;
    f.addConfig();
    StationServiceManager manager(f.options(StationPlatform::Windows));

    // A window closing with "Keep it running when NereusSDR is closed" on.
    QVERIFY2(manager.startBackground(), qPrintable(manager.lastError()));
    WindowsTask task = readWindowsTask(manager.entryPath());
    QVERIFY(task.wellFormed);
    QCOMPARE(task.priority, QStringLiteral("4"));
    QVERIFY(manager.stopBackground());

    // "Start it with the computer" switched on.
    QVERIFY(QFile::remove(manager.entryPath()));
    QVERIFY2(manager.setStartWithComputer(true), qPrintable(manager.lastError()));
    task = readWindowsTask(manager.entryPath());
    QVERIFY(task.wellFormed);
    QCOMPARE(task.priority, QStringLiteral("4"));

    // Switched off again: the task stays registered without its logon
    // trigger, and the file it is registered from is written once more.
    QVERIFY(QFile::remove(manager.entryPath()));
    QVERIFY(f.taskExists);
    QVERIFY2(manager.setStartWithComputer(false), qPrintable(manager.lastError()));
    task = readWindowsTask(manager.entryPath());
    QVERIFY(task.wellFormed);
    QCOMPARE(task.priority, QStringLiteral("4"));
}

void StationServiceManagerTest::macAndLinuxFailuresSayWhy()
{
    {
        Fixture f;
        f.addConfig();
        f.failPrefix = QStringLiteral("launchctl bootstrap");
        f.failOutput = QStringLiteral("Bootstrap failed: 5: Input/output error\n"
                                      "Try re-running the command as root for richer errors.\n");
        StationServiceManager manager(f.options(StationPlatform::MacOS));
        QVERIFY(!manager.startBackground());
        QCOMPARE(manager.lastError(),
                 QStringLiteral("Could not register the background Core with macOS. macOS reported: "
                                "Bootstrap failed: 5: Input/output error Try re-running the command "
                                "as root for richer errors."));
    }
    {
        Fixture f;
        f.addConfig();
        f.failPrefix = QStringLiteral("launchctl kickstart");
        f.failOutput = QStringLiteral("Could not find service \"x\" in domain for user gui: 1234\n");
        StationServiceManager manager(f.options(StationPlatform::MacOS));
        QVERIFY(!manager.startBackground());
        QVERIFY(manager.lastError().startsWith(
            QStringLiteral("Could not start the background Core on macOS. macOS reported: "
                           "Could not find service")));
    }
    {
        Fixture f;
        f.addConfig();
        f.failPrefix = QStringLiteral("systemctl --user|start");
        f.failOutput = QStringLiteral("Failed to connect to bus: No medium found\n");
        StationServiceManager manager(f.options(StationPlatform::Linux));
        QVERIFY(!manager.startBackground());
        QCOMPARE(manager.lastError(),
                 QStringLiteral("Could not start the background Core in this login. Linux reported: "
                                "Failed to connect to bus: No medium found"));
    }
    {
        Fixture f;
        f.addConfig();
        f.failPrefix = QStringLiteral("systemctl --user|enable");
        f.failOutput = QStringLiteral("Failed to enable unit: Access denied\n");
        StationServiceManager manager(f.options(StationPlatform::Linux));
        QVERIFY(!manager.setStartWithComputer(true));
        QCOMPARE(manager.lastError(),
                 QStringLiteral("Could not change background Core startup for this login. "
                                "Linux reported: Failed to enable unit: Access denied"));
    }
    {
        // A long answer is cut so the dialog stays readable.
        Fixture f;
        f.addConfig();
        f.failPrefix = QStringLiteral("systemctl --user|start");
        f.failOutput = QString(2000, QLatin1Char('x'));
        StationServiceManager manager(f.options(StationPlatform::Linux));
        QVERIFY(!manager.startBackground());
        QVERIFY(manager.lastError().size() < 500);
        QVERIFY(manager.lastError().endsWith(QStringLiteral("...")));
    }
}

// The background Core is the window's own Core carried on while no window
// is open. It is the same Core only if it keeps its identity and its paired
// devices where the window's Core keeps them, and it has the radio in
// seconds only if it finds the FFT plans the window already built. Left to
// itself it does neither: with the default profile it keeps its identity
// in its own reserved folder, and it looks for plans in a folder named
// after its own program. So every system's entry names both folders.
void StationServiceManagerTest::everyEntryNamesTheWindowsOwnFolders()
{
    {
        Fixture f;
        f.addConfig();
        StationServiceManager manager(f.options(StationPlatform::MacOS));
        QVERIFY(manager.startBackground());
        QCOMPARE(Test::launchAgentWords(fileBytes(manager.entryPath())),
                 QStringList{f.binary} + launchWords(f, manager));
    }
    {
        Fixture f;
        f.addConfig();
        auto options = f.options(StationPlatform::Linux);
        options.prefixArguments = {QStringLiteral("--nereus-station")};
        StationServiceManager manager(options);
        QVERIFY(manager.startBackground());
        QCOMPARE(Test::systemdCommandWords(fileBytes(manager.entryPath())),
                 QStringList({f.binary, QStringLiteral("--nereus-station")})
                     + launchWords(f, manager));
    }
    {
        Fixture f;
        f.addConfig();
        StationServiceManager manager(f.options(StationPlatform::Windows));
        QVERIFY(manager.startBackground());
        const WindowsTask task = readWindowsTask(manager.entryPath());
        QVERIFY(task.wellFormed);
        QCOMPARE(task.arguments, windowsLaunchWords(f, manager));
    }
    {
        // The default profile is the one that went wrong: the window keeps
        // the Core's identity beside its settings, the background Core kept
        // its own elsewhere.
        Fixture f;
        f.addConfig();
        auto options = f.options(StationPlatform::Windows);
        options.inheritActiveProfile = false;
        options.profile.clear();
        StationServiceManager manager(options);
        QVERIFY(manager.startBackground());
        QCOMPARE(readWindowsTask(manager.entryPath()).arguments,
                 windowsLaunchWords(f, manager, QString()));
    }
}

// A caller that names no plan folder gets the one this program itself
// uses, from the same place the radio connection asks.
void StationServiceManagerTest::anUnnamedPlanFolderIsTheOneThisProgramUses()
{
    Fixture f;
    f.addConfig();
    const QString own = f.temp.filePath(QStringLiteral("this program's plans"));
    DspCacheLocation::setDirectory(own);
    const auto restore = qScopeGuard([] { DspCacheLocation::setDirectory({}); });
    auto options = f.options(StationPlatform::Linux);
    options.dspCacheDirectory.clear();
    StationServiceManager manager(options);
    QVERIFY(manager.startBackground());
    const QStringList words = Test::systemdCommandWords(fileBytes(manager.entryPath()));
    const qsizetype at = words.indexOf(QStringLiteral("--dsp-cache-dir"));
    QVERIFY2(at >= 0, qPrintable(words.join(QLatin1Char('|'))));
    QCOMPARE(words.value(at + 1), own);
}

// The plans can be built again, so a folder the entry cannot name safely
// must not stop the hand over: the background Core then uses its own.
void StationServiceManagerTest::anUnusablePlanFolderIsLeftOut()
{
    for (const QString& folder : {QStringLiteral("relative/plans"),
                                  QStringLiteral("/plans\nExecStartPre=/bin/true")}) {
        Fixture f;
        f.addConfig();
        auto options = f.options(StationPlatform::Linux);
        options.dspCacheDirectory = folder;
        StationServiceManager manager(options);
        QVERIFY2(manager.startBackground(), qPrintable(manager.lastError()));
        const QByteArray unit = fileBytes(manager.entryPath());
        QVERIFY(!unit.contains("--dsp-cache-dir"));
        QVERIFY(!unit.contains("ExecStartPre"));
        const QStringList words = Test::systemdCommandWords(unit);
        QCOMPARE(words.value(words.indexOf(QStringLiteral("--security-dir")) + 1), f.profileDir);
    }
}

QTEST_GUILESS_MAIN(StationServiceManagerTest)
#include "tst_station_service_manager.moc"
