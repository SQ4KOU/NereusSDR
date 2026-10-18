#pragma once
// no-port-check: NereusSDR-original. iPhone app plan Task 47.

#include "core/StationBinaryLocator.h"

#include <QObject>
#include <QStringList>

#include <functional>

namespace NereusSDR {

// Commands are argument vectors, never shell command lines. Tests replace
// this runner so no service tool or user configuration is touched.
struct StationServiceCommandResult {
    int exitCode = -1;
    QString output;
};
using StationServiceRunner =
    std::function<StationServiceCommandResult(const QString&, const QStringList&)>;

struct StationServiceOptions {
    QString profile;
    bool inheritActiveProfile = true;
    QString profileDirectory;
    // The folder the window's own Core keeps its saved FFT plans in; the
    // background Core is told to use the same one. Empty: this program's
    // own (DspCacheLocation::directory()).
    QString dspCacheDirectory;
    QString homeDirectory;
    QString binaryPath;
    QStringList prefixArguments;
    QString userName;
    // macOS: the numeric user id of the launchd domain. Windows: the
    // account's SID, which the login task names as its user.
    QString userId;
    StationPlatform platform = currentStationPlatform();
    StationServiceRunner runner;
};

class StationServiceManager : public QObject {
    Q_OBJECT
public:
    enum class StartupMode { Disabled, Boot, Login, Failed };

    explicit StationServiceManager(StationServiceOptions options = {}, QObject* parent = nullptr);
    bool startBackground();
    bool stopBackground();
    bool isBackgroundRunning() const;
    bool setStartWithComputer(bool enabled);
    bool startsWithComputer() const;

    StartupMode startupMode() const { return m_startupMode; }
    QString lastError() const { return m_lastError; }
    QString entryPath() const;
    QString configPath() const;

private:
    enum class ServiceState { Absent, Stopped, Pending, Running, Error };
    bool validateLaunch();
    QStringList launchArguments() const;
    bool writeEntry(bool startAtLogin);
    bool run(const QString& program, const QStringList& args, QString* output = nullptr) const;
    bool registerWindowsTask(QString* toolOutput) const;
    ServiceState probeState(QString* toolOutput = nullptr) const;
    QString serviceName() const;
    // toolOutput is what the system's own tool printed; it is added to the
    // sentence so the reason reaches the user and the log.
    void fail(const QString& error, const QString& toolOutput = {});

    StationServiceOptions m_options;
    StartupMode m_startupMode = StartupMode::Disabled;
    QString m_lastError;
    QString m_locatorError;
};

} // namespace NereusSDR
