// no-port-check: NereusSDR-original. iPhone app plan Task 47.
#include "gui/StationServiceManager.h"

#include "core/AppSettings.h"
#include "core/DspCacheLocation.h"
#include "core/LogCategories.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringDecoder>
#include <QStringEncoder>
#include <QXmlStreamReader>

#include <algorithm>
#include <cstddef>
#include <memory>

#if defined(Q_OS_MAC)
#include <unistd.h>
#endif
#if defined(Q_OS_WIN)
#include <qt_windows.h>
#include <sddl.h>
#endif

namespace NereusSDR {
namespace {

constexpr auto kLabel = "com.boydsoftprez.NereusSDR.station";
constexpr auto kWindowsTask = "NereusSDR Station";
// The priority Task Scheduler starts the task's program at. 4 is
// NORMAL_PRIORITY_CLASS with normal I/O and normal memory priority, what a
// program started from a shortcut gets. A definition that names none gets 7,
// BELOW_NORMAL_PRIORITY_CLASS with low I/O and low memory priority, where a
// Core doing the receive DSP can fall behind on a busy computer (Microsoft
// Learn, "Priority (settingsType) Element", Task Scheduler schema). A task
// registered by an earlier build keeps 7 until the app registers it again.
constexpr int kWindowsTaskPriority = 4;
// The script contains no user-controlled text. Task Scheduler's numeric
// TASK_STATE values are independent of the Windows display language.
constexpr auto kWindowsStateScript =
    "$ErrorActionPreference='Stop'; "
    "try { $s=New-Object -ComObject 'Schedule.Service'; $s.Connect(); "
    "$t=$s.GetFolder('\\').GetTask('NereusSDR Station'); "
    "[Console]::Out.WriteLine([int]$t.State) } "
    "catch { $e=$_.Exception; while ($e.InnerException) { $e=$e.InnerException }; "
    "if ($e.HResult -eq -2147024894) { [Console]::Out.WriteLine(-1) } "
    "else { [Console]::Out.WriteLine($e.Message); exit 1 } }";
// The byte order mark Windows puts at the start of its own task files.
constexpr char kUtf16LeMark[] = "\xFF\xFE";
// The longest tool answer an error sentence carries.
constexpr qsizetype kToolMessageLimit = 300;

StationServiceCommandResult runProcess(const QString& program, const QStringList& args)
{
    QProcess process;
    process.start(program, args);
    if (!process.waitForStarted(5000)) {
        return {-1, process.errorString()};
    }
    if (!process.waitForFinished(10000)) {
        process.kill();
        process.waitForFinished();
        return {-1, QStringLiteral("service command timed out")};
    }
    return {process.exitCode(), QString::fromLocal8Bit(process.readAllStandardOutput())
                                    + QString::fromLocal8Bit(process.readAllStandardError())};
}

QString xml(const QString& value)
{
    return value.toHtmlEscaped();
}

QString systemdArgument(const QString& value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    escaped.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    escaped.replace(QLatin1Char('%'), QStringLiteral("%%"));
    escaped.replace(QLatin1Char('$'), QStringLiteral("$$"));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

// Windows' CreateProcess argument quoting, kept inside Task Scheduler XML's
// Arguments field; no cmd.exe or shell expansion participates.
QString windowsArgument(const QString& value)
{
    QString quoted = QStringLiteral("\"");
    int backslashes = 0;
    for (const QChar ch : value) {
        if (ch == QLatin1Char('\\')) {
            ++backslashes;
        } else if (ch == QLatin1Char('"')) {
            quoted += QString(backslashes * 2 + 1, QLatin1Char('\\')) + ch;
            backslashes = 0;
        } else {
            quoted += QString(backslashes, QLatin1Char('\\')) + ch;
            backslashes = 0;
        }
    }
    quoted += QString(backslashes * 2, QLatin1Char('\\')) + QLatin1Char('"');
    return quoted;
}

// A path as Windows writes it. Keyed on the target platform, not on the
// computer this runs on, so the file is the same wherever it is built.
QString windowsPath(QString path)
{
    return path.replace(QLatin1Char('/'), QLatin1Char('\\'));
}

// What a service tool printed, as one line short enough for a dialog.
QString toolMessage(const QString& output)
{
    QString text = output.simplified();
    if (text.size() > kToolMessageLimit) {
        text = text.left(kToolMessageLimit).trimmed() + QStringLiteral("...");
    }
    return text;
}

// The Windows task file is also what the login switch reads, so a change
// Windows refused must not stay in it.
struct SavedEntry {
    bool existed = false;
    QByteArray bytes;
};

SavedEntry readEntry(const QString& path)
{
    SavedEntry saved;
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        saved.existed = true;
        saved.bytes = file.readAll();
    }
    return saved;
}

void putBack(const QString& path, const SavedEntry& saved)
{
    if (!saved.existed) {
        QFile::remove(path);
        return;
    }
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly) && file.write(saved.bytes) == saved.bytes.size()) {
        file.commit();
    }
}

#if defined(Q_OS_WIN)
// The account this process runs as, in the form Task Scheduler stores it.
QString currentWindowsUser()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return {};
    }
    QString sid;
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    if (size > 0) {
        const auto buffer = std::make_unique<std::byte[]>(size);
        if (GetTokenInformation(token, TokenUser, buffer.get(), size, &size)) {
            LPWSTR text = nullptr;
            if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.get())->User.Sid, &text)) {
                sid = QString::fromWCharArray(text);
                LocalFree(text);
            }
        }
    }
    CloseHandle(token);
    return sid;
}
#endif

bool hasLineBreak(const QString& value)
{
    return std::any_of(value.begin(), value.end(), [](QChar ch) {
        return ch.unicode() < 0x20 || ch.unicode() == 0x7f;
    });
}

bool xmlToggle(const QByteArray& bytes, const QString& key)
{
    QXmlStreamReader reader(bytes);
    bool pending = false;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == QStringLiteral("key")) {
            pending = reader.readElementText() == key;
        } else if (pending && reader.isStartElement()) {
            return reader.name() == QStringLiteral("true");
        }
    }
    return false;
}

} // namespace

StationServiceManager::StationServiceManager(StationServiceOptions options, QObject* parent)
    : QObject(parent)
    , m_options(std::move(options))
{
    if (m_options.inheritActiveProfile && m_options.profile.isEmpty()) {
        m_options.profile = AppSettings::profileOverride();
    }
    if (m_options.profileDirectory.isEmpty()) {
        m_options.profileDirectory = AppSettings::resolveConfigDir(m_options.profile);
    }
    if (m_options.homeDirectory.isEmpty()) {
        m_options.homeDirectory = QDir::homePath();
    }
    if (m_options.dspCacheDirectory.isEmpty()) {
        m_options.dspCacheDirectory = DspCacheLocation::directory();
    }
    if (m_options.binaryPath.isEmpty()) {
        const StationLaunchSpec launch = locateStationLaunch();
        m_options.binaryPath = launch.program;
        m_options.prefixArguments = launch.prefixArguments;
        m_locatorError = launch.error;
    }
    if (m_options.userName.isEmpty()) {
        m_options.userName = qEnvironmentVariable("USER");
    }
#if defined(Q_OS_MAC)
    if (m_options.userId.isEmpty()) {
        m_options.userId = QString::number(getuid());
    }
#endif
#if defined(Q_OS_WIN)
    if (m_options.userId.isEmpty() && m_options.platform == StationPlatform::Windows) {
        m_options.userId = currentWindowsUser();
    }
#endif
    if (!m_options.runner) {
        m_options.runner = runProcess;
    }
}

QString StationServiceManager::entryPath() const
{
    const QDir home(m_options.homeDirectory);
    switch (m_options.platform) {
    case StationPlatform::MacOS:
        return home.filePath(QStringLiteral("Library/LaunchAgents/") + QString::fromLatin1(kLabel)
                             + QStringLiteral(".plist"));
    case StationPlatform::Linux:
        return home.filePath(QStringLiteral(".config/systemd/user/nereusd.service"));
    case StationPlatform::Windows:
        return home.filePath(QStringLiteral("AppData/Local/NereusSDR/nereusd-task.xml"));
    }
    return {};
}

QString StationServiceManager::configPath() const
{
    return QDir(m_options.profileDirectory).filePath(QStringLiteral("station.conf"));
}

QString StationServiceManager::serviceName() const
{
    return m_options.platform == StationPlatform::Windows ? QString::fromLatin1(kWindowsTask)
                                                          : QString::fromLatin1(kLabel);
}

void StationServiceManager::fail(const QString& error, const QString& toolOutput)
{
    m_lastError = error;
    const QString said = toolMessage(toolOutput);
    if (!said.isEmpty()) {
        QString system = QStringLiteral("Linux");
        if (m_options.platform == StationPlatform::Windows) {
            system = QStringLiteral("Windows");
        } else if (m_options.platform == StationPlatform::MacOS) {
            system = QStringLiteral("macOS");
        }
        m_lastError += QStringLiteral(" %1 reported: %2").arg(system, said);
    }
    m_startupMode = StartupMode::Failed;
    qCWarning(lcApp).noquote() << "Background Core:" << m_lastError;
}

bool StationServiceManager::run(const QString& program, const QStringList& args,
                                QString* output) const
{
    const StationServiceCommandResult result = m_options.runner(program, args);
    if (output) {
        *output = result.output;
    }
    return result.exitCode == 0;
}

bool StationServiceManager::validateLaunch()
{
    if (m_options.binaryPath.isEmpty()) {
        fail(m_locatorError.isEmpty() ? QStringLiteral("The packaged Core app is missing. Reinstall NereusSDR.")
                                      : m_locatorError);
        return false;
    }
    if ((!m_options.profile.isEmpty() && !AppSettings::isValidProfileName(m_options.profile))
        || !QDir::isAbsolutePath(m_options.profileDirectory)
        || !QDir::isAbsolutePath(m_options.homeDirectory)
        || !QDir::isAbsolutePath(m_options.binaryPath)
        || hasLineBreak(m_options.profileDirectory) || hasLineBreak(m_options.homeDirectory)
        || hasLineBreak(m_options.binaryPath)
        || std::any_of(m_options.prefixArguments.begin(), m_options.prefixArguments.end(), hasLineBreak)) {
        fail(QStringLiteral("Choose a valid Core profile and installed Core app."));
        return false;
    }
    if (!QFileInfo(m_options.binaryPath).isFile()
        || !QFileInfo(m_options.binaryPath).isExecutable()) {
        fail(QStringLiteral("The packaged Core app is missing. Reinstall NereusSDR."));
        return false;
    }
    if (!QFileInfo(configPath()).isFile()) {
        fail(QStringLiteral("The Core settings file is missing. Set up this Core before starting it."));
        return false;
    }
    return true;
}

// What follows the program in every entry. The background Core is this
// window's own Core carried on while no window is open, so beside the
// profile and its settings file it is told the two folders that Core uses.
// Left to itself nereusd would use neither: with the default profile it
// keeps its identity and its paired devices in the reserved daemon profile
// (CertificateStore::defaultDirectory), so a paired device met a Core it
// did not know and hung up; and it looks for the saved FFT plans in a
// folder named after its own program (DspCacheLocation.h), so it built
// them all again, for minutes, before it could take the radio.
QStringList StationServiceManager::launchArguments() const
{
    const bool windows = m_options.platform == StationPlatform::Windows;
    const auto path = [windows](const QString& value) {
        return windows ? windowsPath(value) : value;
    };
    QStringList arguments = m_options.prefixArguments;
    arguments << QStringLiteral("--profile") << m_options.profile
              << QStringLiteral("--config") << path(configPath())
              << QStringLiteral("--security-dir") << path(m_options.profileDirectory);
    // The plans can be built again, so a folder the entry cannot name
    // safely never stops the hand over; the Core then uses its own.
    if (QDir::isAbsolutePath(m_options.dspCacheDirectory)
        && !hasLineBreak(m_options.dspCacheDirectory)) {
        arguments << QStringLiteral("--dsp-cache-dir") << path(m_options.dspCacheDirectory);
    } else {
        qCWarning(lcApp) << "Background Core: the saved FFT plans folder cannot be named in"
                         << "the startup entry, so the Core will build its own plans:"
                         << m_options.dspCacheDirectory;
    }
    return arguments;
}

bool StationServiceManager::writeEntry(bool startAtLogin)
{
    const QFileInfo entry(entryPath());
    if (!QDir().mkpath(entry.absolutePath())) {
        fail(QStringLiteral("Could not create the Core startup folder."));
        return false;
    }
    QString content;
    if (m_options.platform == StationPlatform::MacOS) {
        QString argumentsXml;
        for (const QString& argument : launchArguments()) {
            argumentsXml += QStringLiteral("<string>%1</string>").arg(xml(argument));
        }
        content = QStringLiteral(
                      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                      "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                      "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                      "<plist version=\"1.0\"><dict>"
                      "<key>Label</key><string>%1</string>"
                      "<key>ProgramArguments</key><array>"
                      "<string>%2</string>%3</array>"
                      "<key>RunAtLoad</key>%4"
                      "<key>KeepAlive</key><false/>"
                      "</dict></plist>\n")
                      .arg(xml(serviceName()), xml(m_options.binaryPath), argumentsXml,
                           startAtLogin ? QStringLiteral("<true/>") : QStringLiteral("<false/>"));
    } else if (m_options.platform == StationPlatform::Linux) {
        QStringList escapedArguments;
        for (const QString& argument : launchArguments()) {
            escapedArguments << systemdArgument(argument);
        }
        content = QStringLiteral("[Unit]\nDescription=NereusSDR background station\n"
                                 "After=network-online.target\nWants=network-online.target\n\n"
                                 "[Service]\nType=exec\nExecStart=%1 %2\n"
                                 "Restart=on-failure\nRestartSec=5\n\n"
                                 "[Install]\nWantedBy=default.target\n")
                      .arg(systemdArgument(m_options.binaryPath), escapedArguments.join(QLatin1Char(' ')));
    } else {
        QStringList quotedArguments;
        for (const QString& argument : launchArguments()) {
            quotedArguments << windowsArgument(argument);
        }
        // Without a UserId the logon trigger means any user, which only an
        // administrator may register. Naming this account keeps the task
        // inside what the app's own rights allow.
        const QString user = m_options.userId.isEmpty()
            ? QString()
            : QStringLiteral("<UserId>%1</UserId>").arg(xml(m_options.userId));
        // Task Scheduler reads the definition as UTF-16 text and refuses one
        // that declares another encoding, so the file says UTF-16 and is
        // saved as UTF-16 below, as Windows saves its own task files.
        content = QStringLiteral(
                      "<?xml version=\"1.0\" encoding=\"UTF-16\"?>\n"
                      "<Task version=\"1.4\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">"
                      "<Triggers><LogonTrigger><Enabled>%1</Enabled>%4</LogonTrigger></Triggers>"
                      "<Principals><Principal id=\"Author\">%4<LogonType>InteractiveToken</LogonType>"
                      "<RunLevel>LeastPrivilege</RunLevel></Principal></Principals>"
                      "<Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>"
                      "<DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>"
                      "<StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>"
                      "<AllowHardTerminate>true</AllowHardTerminate>"
                      "<StartWhenAvailable>true</StartWhenAvailable>"
                      "<ExecutionTimeLimit>PT0S</ExecutionTimeLimit>"
                      "<Priority>%5</Priority>"
                      "<Enabled>true</Enabled><AllowStartOnDemand>true</AllowStartOnDemand>"
                      "</Settings><Actions Context=\"Author\"><Exec><Command>%2</Command>"
                      "<Arguments>%3</Arguments></Exec></Actions></Task>\n")
                      .arg(startAtLogin ? QStringLiteral("true") : QStringLiteral("false"),
                           xml(windowsPath(m_options.binaryPath)),
                           xml(quotedArguments.join(QLatin1Char(' '))), user,
                           QString::number(kWindowsTaskPriority));
    }
    QSaveFile file(entryPath());
    QByteArray bytes;
    if (m_options.platform == StationPlatform::Windows) {
        QStringEncoder utf16(QStringConverter::Utf16LE);
        bytes = QByteArray(kUtf16LeMark, 2) + QByteArray(utf16(content));
    } else {
        bytes = content.toUtf8();
    }
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        fail(QStringLiteral("Could not save the Core startup entry."));
        return false;
    }
    if (m_options.platform != StationPlatform::Windows
        && !QFile::setPermissions(entryPath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        fail(QStringLiteral("Could not protect the Core startup entry."));
        return false;
    }
    return true;
}

StationServiceManager::ServiceState StationServiceManager::probeState(QString* toolOutput) const
{
    // An unknown state is reported with whatever the tool printed last.
    QString local;
    QString& output = toolOutput ? *toolOutput : local;
    output.clear();
    if (m_options.platform == StationPlatform::MacOS) {
        if (!run(QStringLiteral("launchctl"), {QStringLiteral("list")}, &output)) {
            return ServiceState::Error;
        }
        bool found = false;
        for (const QString& line : output.split(QLatin1Char('\n'))) {
            const QStringList fields = line.split(QRegularExpression(QStringLiteral("\\s+")),
                                                  Qt::SkipEmptyParts);
            if (!fields.isEmpty() && fields.last() == serviceName()) {
                found = true;
                break;
            }
        }
        if (!found) return ServiceState::Absent;
        if (!run(QStringLiteral("launchctl"),
                 {QStringLiteral("print"), QStringLiteral("gui/%1/%2")
                                               .arg(m_options.userId, serviceName())}, &output)) {
            return ServiceState::Error;
        }
        if (output.contains(QStringLiteral("state = running"))) return ServiceState::Running;
        // A registered Core that is not running: "not running" on current
        // macOS, "waiting" on older releases.
        if (output.contains(QStringLiteral("state = not running"))
            || output.contains(QStringLiteral("state = waiting"))) {
            return ServiceState::Stopped;
        }
        if (output.contains(QStringLiteral("state = "))) return ServiceState::Pending;
        output.clear(); // the whole service listing explains nothing to a user
        return ServiceState::Error;
    }
    if (m_options.platform == StationPlatform::Linux) {
        if (!run(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("show"),
                                                 QStringLiteral("nereusd.service"),
                                                 QStringLiteral("--property=LoadState"),
                                                 QStringLiteral("--property=ActiveState"),
                                                 QStringLiteral("--no-pager")}, &output)) {
            return ServiceState::Error;
        }
        if (output.contains(QStringLiteral("LoadState=not-found"))) return ServiceState::Absent;
        if (!output.contains(QStringLiteral("LoadState=loaded"))) return ServiceState::Error;
        if (output.contains(QStringLiteral("ActiveState=active"))) return ServiceState::Running;
        if (output.contains(QStringLiteral("ActiveState=inactive"))
            || output.contains(QStringLiteral("ActiveState=failed"))) return ServiceState::Stopped;
        if (output.contains(QStringLiteral("ActiveState=activating"))
            || output.contains(QStringLiteral("ActiveState=deactivating"))
            || output.contains(QStringLiteral("ActiveState=reloading"))) return ServiceState::Pending;
        return ServiceState::Error;
    }
    if (!run(QStringLiteral("powershell.exe"),
             {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
              QStringLiteral("-Command"), QString::fromLatin1(kWindowsStateScript)}, &output)) {
        return ServiceState::Error;
    }
    const QString state = output.trimmed();
    if (state == QStringLiteral("-1")) return ServiceState::Absent;
    if (state == QStringLiteral("4")) return ServiceState::Running;
    if (state == QStringLiteral("2")) return ServiceState::Pending;
    if (state == QStringLiteral("1") || state == QStringLiteral("3")) return ServiceState::Stopped;
    output.clear(); // a state number explains nothing to a user
    return ServiceState::Error;
}

bool StationServiceManager::isBackgroundRunning() const
{
    return probeState() == ServiceState::Running;
}

bool StationServiceManager::startBackground()
{
    m_lastError.clear();
    QString output;
    const ServiceState initial = probeState(&output);
    if (initial == ServiceState::Running) {
        return true;
    }
    if (initial == ServiceState::Error) {
        fail(QStringLiteral("Could not check the background Core state."), output);
        return false;
    }
    if (initial == ServiceState::Pending) {
        fail(QStringLiteral("The background Core is changing state. Try again shortly."));
        return false;
    }
    if (!validateLaunch()) {
        return false;
    }
    const bool atLogin = startsWithComputer();
    const SavedEntry previous = readEntry(entryPath());
    if (!writeEntry(atLogin)) {
        return false;
    }
    if (m_options.platform == StationPlatform::MacOS) {
        const QString domain = QStringLiteral("gui/%1").arg(m_options.userId);
        // launchd does not reload ProgramArguments on kickstart. Replace an
        // idle registration so a changed binary/profile takes effect.
        const ServiceState current = probeState(&output);
        if (current == ServiceState::Error) {
            fail(QStringLiteral("Could not check the background Core state."), output);
            return false;
        }
        if (current == ServiceState::Pending) {
            fail(QStringLiteral("The background Core is changing state. Try again shortly."));
            return false;
        }
        if (current == ServiceState::Running) return true;
        if (current == ServiceState::Stopped
            && !run(QStringLiteral("launchctl"),
                    {QStringLiteral("bootout"), domain + QLatin1Char('/') + serviceName()}, &output)) {
            fail(QStringLiteral("Could not reload the background Core on macOS."), output);
            return false;
        }
        if (!run(QStringLiteral("launchctl"),
                 {QStringLiteral("bootstrap"), domain, entryPath()}, &output)) {
            fail(QStringLiteral("Could not register the background Core with macOS."), output);
            return false;
        }
        if (!run(QStringLiteral("launchctl"),
                 {QStringLiteral("kickstart"), domain + QLatin1Char('/') + serviceName()}, &output)) {
            fail(QStringLiteral("Could not start the background Core on macOS."), output);
            return false;
        }
    } else if (m_options.platform == StationPlatform::Linux) {
        if (!run(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("daemon-reload")},
                 &output)
            || !run(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("start"),
                                                   QStringLiteral("nereusd.service")}, &output)) {
            fail(QStringLiteral("Could not start the background Core in this login."), output);
            return false;
        }
    } else {
        if (!registerWindowsTask(&output)) {
            putBack(entryPath(), previous);
            fail(QStringLiteral("Could not register the background Core with Windows."), output);
            return false;
        }
        if (!run(QStringLiteral("schtasks"), {QStringLiteral("/Run"), QStringLiteral("/TN"),
                                              serviceName()}, &output)) {
            fail(QStringLiteral("Could not start the background Core at this login."), output);
            return false;
        }
    }
    return true;
}

bool StationServiceManager::registerWindowsTask(QString* toolOutput) const
{
    return run(QStringLiteral("schtasks"), {QStringLiteral("/Create"), QStringLiteral("/F"),
                                            QStringLiteral("/TN"), serviceName(),
                                            QStringLiteral("/XML"), windowsPath(entryPath())},
               toolOutput);
}

bool StationServiceManager::stopBackground()
{
    m_lastError.clear();
    QString output;
    const ServiceState state = probeState(&output);
    if (state == ServiceState::Error) {
        fail(QStringLiteral("Could not check the background Core state."), output);
        return false;
    }
    if (state == ServiceState::Pending) {
        fail(QStringLiteral("The background Core is changing state. Try again shortly."));
        return false;
    }
    if (state != ServiceState::Running) {
        return true;
    }
    bool stopped = false;
    if (m_options.platform == StationPlatform::MacOS) {
        stopped = run(QStringLiteral("launchctl"),
                      {QStringLiteral("bootout"), QStringLiteral("gui/%1/%2")
                                                        .arg(m_options.userId, serviceName())},
                      &output);
    } else if (m_options.platform == StationPlatform::Linux) {
        stopped = run(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("stop"),
                                                     QStringLiteral("nereusd.service")}, &output);
    } else {
        stopped = run(QStringLiteral("schtasks"), {QStringLiteral("/End"), QStringLiteral("/TN"),
                                                   serviceName()}, &output);
    }
    if (!stopped) {
        fail(QStringLiteral("Could not stop the background Core."), output);
    }
    return stopped;
}

bool StationServiceManager::setStartWithComputer(bool enabled)
{
    m_lastError.clear();
    QString output;
    if (!enabled) {
        if (m_options.platform == StationPlatform::Windows) {
            const ServiceState state = probeState(&output);
            if (state == ServiceState::Error) {
                fail(QStringLiteral("Could not check the Core login task."), output);
                return false;
            }
            const bool taskExists = state != ServiceState::Absent;
            if (!taskExists && !QFileInfo::exists(entryPath())) {
                m_startupMode = StartupMode::Disabled;
                return true;
            }
            // Keep the on-demand task registered so /End can still stop a
            // running station; remove only its logon trigger.
            const SavedEntry previous = readEntry(entryPath());
            if (!writeEntry(false)) {
                return false;
            }
            if (taskExists && !registerWindowsTask(&output)) {
                putBack(entryPath(), previous);
                fail(QStringLiteral("Could not remove the Core login trigger."), output);
                return false;
            }
            m_startupMode = StartupMode::Disabled;
            return true;
        }
        const bool hadEntry = QFileInfo::exists(entryPath());
        const bool wasEnabled = m_options.platform == StationPlatform::Linux && startsWithComputer();
        if (!hadEntry && !wasEnabled) {
            m_startupMode = StartupMode::Disabled;
            return true;
        }
        if (wasEnabled
            && !run(QStringLiteral("systemctl"), {QStringLiteral("--user"),
                                                    QStringLiteral("disable"), QStringLiteral("nereusd.service")},
                    &output)) {
            fail(QStringLiteral("Could not disable the Core login unit."), output);
            return false;
        }
        if (QFileInfo::exists(entryPath()) && !QFile::remove(entryPath())) {
            fail(QStringLiteral("Could not remove the Core startup entry."));
            return false;
        }
        if (m_options.platform == StationPlatform::Linux
            && !run(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("daemon-reload")},
                    &output)) {
            fail(QStringLiteral("Could not reload the Core login unit."), output);
            return false;
        }
        m_startupMode = StartupMode::Disabled;
        return true;
    }
    if (!validateLaunch()) {
        return false;
    }
    const SavedEntry previous = readEntry(entryPath());
    if (!writeEntry(true)) {
        return false;
    }
    if (m_options.platform == StationPlatform::MacOS) {
        m_startupMode = StartupMode::Login;
        return true;
    }
    if (m_options.platform == StationPlatform::Linux) {
        if (!run(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("daemon-reload")},
                 &output)
            || !run(QStringLiteral("systemctl"), {QStringLiteral("--user"),
                                                   QStringLiteral("enable"),
                                                   QStringLiteral("nereusd.service")}, &output)) {
            fail(QStringLiteral("Could not change background Core startup for this login."), output);
            return false;
        }
        QString linger;
        const bool alreadyLingered = !m_options.userName.isEmpty()
            && run(QStringLiteral("loginctl"), {QStringLiteral("show-user"),
                                                 QStringLiteral("--property=Linger"),
                                                 QStringLiteral("--value"), m_options.userName},
                   &linger)
            && linger.trimmed() == QStringLiteral("yes");
        if (alreadyLingered
            || (!m_options.userName.isEmpty()
                && run(QStringLiteral("loginctl"), {QStringLiteral("--no-ask-password"),
                                                     QStringLiteral("enable-linger"),
                                                     m_options.userName}))) {
            m_startupMode = StartupMode::Boot;
        } else {
            m_startupMode = StartupMode::Login;
            m_lastError = QStringLiteral("The Core starts at login; boot startup is not available for this user.");
        }
        return true;
    }
    if (!registerWindowsTask(&output)) {
        putBack(entryPath(), previous);
        fail(QStringLiteral("Could not change the Core login task."), output);
        return false;
    }
    m_startupMode = StartupMode::Login;
    return true;
}

bool StationServiceManager::startsWithComputer() const
{
    if (m_options.platform == StationPlatform::Linux) {
        return run(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("is-enabled"),
                                                 QStringLiteral("--quiet"), QStringLiteral("nereusd.service")});
    }
    QFile file(entryPath());
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray content = file.readAll();
    if (m_options.platform == StationPlatform::MacOS) {
        return xmlToggle(content, QStringLiteral("RunAtLoad"));
    }
    // A file from a release that wrote UTF-8 has no byte order mark.
    QString text;
    if (content.startsWith(QByteArray(kUtf16LeMark, 2))) {
        QStringDecoder utf16(QStringConverter::Utf16LE);
        text = utf16(content.mid(2));
    } else {
        text = QString::fromUtf8(content);
    }
    return text.contains(QStringLiteral("<LogonTrigger><Enabled>true</Enabled>"));
}

} // namespace NereusSDR
