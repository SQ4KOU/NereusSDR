#include "gui/MainWindow.h"
#include "gui/styles/AppTheme.h"
#include "core/AppSettings.h"
#include "core/AudioDeviceConfig.h"
#include "core/BuildIdentity.h"
#include "core/CoreInit.h"
#include "core/MacMicPermission.h"
#include "core/audio/RealtimeAudioPriority.h"
#include "core/RadioConnection.h"
#include "core/mmio/ExternalVariableEngine.h"
// Remote-daemon R2 Task 20: --station / --token and the client-side
// settings backend they bring with them.
#include "core/session/RemoteStationOptions.h"
#include "core/settings/SettingsProxy.h"

// Generated into the build tree by cmake/NereusBuildTag.cmake, once per
// build, so NEREUSSDR_BUILD_TAG names the commit actually being compiled
// instead of whatever HEAD happened to be at the last cmake configure.
//
// This is the only translation unit that includes it, and that is on
// purpose: it is compiled into the application target alone, so a new commit
// rebuilds this file and relinks this binary, and leaves the test suite (which
// links the NereusCore object library) untouched. See CMakeLists.txt
// section "Build tag" and src/core/BuildIdentity.h.
#include "NereusBuildTag.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QMetaObject>
#include <csignal>
#include <QCommandLineParser>
#include <QIcon>
#include <QStyleFactory>
#include <QFile>
#include <QStandardPaths>
#include <QStringList>
#include <memory>

// Parse --profile <name> out of argv *before* constructing QApplication so
// AppSettings can pin the right path on first access. QCommandLineParser
// wants a QCoreApplication instance, so we do a cheap manual scan here and
// re-parse properly inside main() once the app is built (for --help / error
// diagnostics).
//
// Issue #100 — multiple NereusSDR instances against different radios.
static QString extractProfileFromArgv(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String("--profile") || a == QLatin1String("-p")) {
            if (i + 1 < argc) {
                return QString::fromLocal8Bit(argv[i + 1]);
            }
        } else if (a.startsWith(QLatin1String("--profile="))) {
            return a.mid(QLatin1String("--profile=").size());
        }
    }
    return {};
}

int main(int argc, char* argv[])
{
    // Hand the build stamp to the core accessor before anything can build a
    // window title from it. Empty on release artifacts, in which case the
    // title stays exactly as it was.
    NereusSDR::BuildIdentity::setBuildTag(
        QString::fromUtf8(NEREUSSDR_BUILD_TAG));

    // Resolve profile name first — downstream path lookups (AppSettings,
    // log dir, pre-QApplication UI scale read) all consult it.
    const QString earlyProfile = extractProfileFromArgv(argc, argv);
    if (!earlyProfile.isEmpty()) {
        if (NereusSDR::AppSettings::isValidProfileName(earlyProfile)) {
            NereusSDR::AppSettings::setProfileOverride(earlyProfile);
        } else {
            fprintf(stderr,
                    "NereusSDR: ignoring invalid --profile '%s' "
                    "(allowed: [A-Za-z0-9_-]+)\n",
                    earlyProfile.toLocal8Bit().constData());
        }
    }
    const QString activeProfile = NereusSDR::AppSettings::profileOverride();

    // Apply saved UI scale factor BEFORE QApplication is created.
    {
        const QString settingsPath =
            NereusSDR::AppSettings::resolveSettingsPath(activeProfile);
        QFile f(settingsPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QByteArray data = f.readAll();
            QByteArray tag = "<UiScalePercent>";
            int idx = data.indexOf(tag);
            if (idx >= 0) {
                idx += tag.size();
                int end = data.indexOf('<', idx);
                if (end > idx) {
                    int pct = data.mid(idx, end - idx).trimmed().toInt();
                    if (pct > 0 && pct != 100) {
                        qputenv("QT_SCALE_FACTOR", QByteArray::number(pct / 100.0, 'f', 2));
                    }
                }
            }
        }
    }

    QApplication app(argc, argv);
    app.setApplicationName("NereusSDR");
    app.setApplicationVersion(NEREUSSDR_VERSION);
    app.setOrganizationName("NereusSDR");
    app.setWindowIcon(QIcon(":/icons/NereusSDR.png"));

    // 2026-05-25 KG4VCF bench fix: elevate the main GUI thread to
    // USER_INTERACTIVE QoS so heavy user-initiated background work
    // (parallel compiles, mdworker indexing, Time Machine snapshots,
    // etc.) does not preempt the Qt event loop and produce visibly
    // choppy spectrum / waterfall rendering.  The audio DSP thread
    // already gets a stronger elevation (see RxDspWorker::onThreadStarted)
    // but the GUI thread runs the spectrum paint cycle and was still
    // being preempted at DEFAULT QoS.  Bench symptom: "whole program
    // stutters when a build happens".
    //
    // Cross-platform via src/core/audio/RealtimeAudioPriority.cpp:
    //   macOS:   pthread_set_qos_class_self_np(USER_INTERACTIVE)
    //   Linux:   nice(-5)  (soft-fail without privilege)
    //   Windows: SetThreadPriority(HIGHEST)
    NereusSDR::elevateGuiMainThreadPriority();

    // 2026-05-22 bench-finding: pkill / kill / system shutdown sends SIGTERM
    // by default; the OS terminates the process without giving Qt a chance
    // to run aboutToQuit handlers.  Without translation, this skips
    // RadioConnection::disconnect, the radio gateware never sees run=0, and
    // some community P2 firmwares require power-cycle to recover.  Install
    // POSIX signal handlers that convert SIGTERM / SIGINT into
    // QApplication::quit, which fires aboutToQuit and runs the graceful
    // disconnect path.  SIGKILL (kill -9, Activity Monitor "Force Quit") is
    // uncatchable — power-cycle is still the only recovery there.
    //
    // R1 Task 9: resolved by giving src/server_main.cpp its own SIGTERM/
    // SIGINT pair rather than sharing this one -- same QMetaObject::
    // invokeMethod + Qt::QueuedConnection pattern, adapted to that file's
    // simpler global-pointer structure. See task-9-report.md for why a
    // shared call was not worth it (a daemon's signal set may still grow a
    // SIGHUP handler for config reload that this GUI pair never will).
    std::signal(SIGTERM, [](int) {
        // Async-signal-safe: only QCoreApplication::quit() is approximately
        // safe to call.  Internally it just sets an atomic flag the event
        // loop polls.
        if (QCoreApplication::instance()) {
            QMetaObject::invokeMethod(QCoreApplication::instance(),
                                      "quit", Qt::QueuedConnection);
        }
    });
    std::signal(SIGINT, [](int) {
        if (QCoreApplication::instance()) {
            QMetaObject::invokeMethod(QCoreApplication::instance(),
                                      "quit", Qt::QueuedConnection);
        }
    });

    // Trigger the macOS microphone permission dialog deterministically
    // (issue #203). The OS only prompts when something actually engages
    // TCC; relying on PortAudio's CoreAudio backend to do so is unreliable
    // on machines without a built-in mic, so call AVCaptureDevice directly.
    NereusSDR::requestMicrophonePermission();

    // Re-parse properly so --help / --version / unknown options surface
    // via Qt's standard machinery. The earlyProfile pass above already
    // pinned AppSettings; this second pass is purely for user-facing UX.
    //
    // Remote-daemon R2 Task 20: --station and --token are read here rather
    // than in the early argv scan, because nothing they affect happens
    // before this point. The profile scan has to be early (AppSettings
    // resolves its path on first access); the station does not.
    NereusSDR::RemoteStationOptions station;
    {
        QCommandLineParser parser;
        parser.setApplicationDescription(
            QStringLiteral("NereusSDR — cross-platform OpenHPSDR client."));
        parser.addHelpOption();
        parser.addVersionOption();
        QCommandLineOption profileOpt(
            QStringList() << QStringLiteral("p") << QStringLiteral("profile"),
            QStringLiteral(
                "Run in an isolated profile (separate settings + logs). "
                "Lets two instances drive two radios without clobbering "
                "each other. Name must match [A-Za-z0-9_-]+."),
            QStringLiteral("name"));
        parser.addOption(profileOpt);

        QCommandLineOption stationOpt(
            QStringLiteral("station"),
            QStringLiteral(
                "Drive a radio owned by a nereusd station instead of one "
                "attached to this machine. Takes a wss:// (or ws://) URL. "
                "Without this, NereusSDR runs in local direct mode exactly "
                "as before."),
            QStringLiteral("wss://host:port"));
        parser.addOption(stationOpt);

        QCommandLineOption tokenOpt(
            QStringLiteral("token"),
            QStringLiteral(
                "Shared token for --station, as printed by nereusd on its "
                "first run. Overrides the value saved in Setup."),
            QStringLiteral("token"));
        parser.addOption(tokenOpt);

        QCommandLineOption fingerprintOpt(
            QStringLiteral("station-fingerprint"),
            QStringLiteral(
                "SHA-256 fingerprint of the station certificate to pin."),
            QStringLiteral("sha256"));
        parser.addOption(fingerprintOpt);

        QCommandLineOption allowUnpinnedOpt(
            QStringLiteral("station-allow-unpinned"),
            QStringLiteral(
                "Accept the station's self-signed certificate without a "
                "pinned fingerprint. Bench use only."));
        parser.addOption(allowUnpinnedOpt);

        parser.process(app);

        // Command-line values only. The saved-Setup fallback cannot be read
        // yet: AppSettings is not loaded until CoreInit::initialize() below,
        // and a value() call before that returns the ship default rather
        // than what the operator saved. Resolved after CoreInit instead.
        station.url         = parser.value(stationOpt);
        station.token       = parser.value(tokenOpt);
        station.fingerprint = parser.value(fingerprintOpt);
        station.allowUnpinned = parser.isSet(allowUnpinnedOpt);
    }

    // Fusion style as a clean cross-platform base, then layer the
    // NereusSDR dark palette + minimal baseline QSS on top so every
    // widget (including ones without their own stylesheet) renders
    // with the dark theme. Without this, Linux/Ubuntu Yaru leaks
    // light-grey backgrounds and orange Highlight through into popups,
    // group-box titles, tooltips, and any unstyled control.
    app.setStyle(QStyleFactory::create("Fusion"));
    NereusSDR::applyDarkPalette(app);
    NereusSDR::applyAppBaselineQss(app);

    // Register custom metatypes for cross-thread signal/slot connections.
    // R1 Task 9: src/server_main.cpp registers this same pair itself
    // (duplicated, not moved here or folded into CoreInit -- see
    // task-9-report.md); this is now the first of two call sites.
    qRegisterMetaType<NereusSDR::RadioConnectionError>();
    qRegisterMetaType<NereusSDR::AudioDeviceConfig>();

    // Shared startup sequence (R1 Task 8): loads AppSettings, applies every
    // one-shot settings-schema migration, restores LogManager's category
    // toggles, and installs file-backed logging. `activeProfile` is the
    // same already-resolved name pinned into AppSettings::setProfileOverride()
    // above; CoreInit::initialize() only uses it to resolve the log
    // directory, it does not re-pin the override itself. See
    // src/core/CoreInit.h for the full contract and its idempotency guard.
    NereusSDR::CoreInit::initialize(activeProfile);

    qDebug() << "Starting NereusSDR" << app.applicationVersion();
    if (!activeProfile.isEmpty()) {
        const QString logDir = NereusSDR::AppSettings::resolveConfigDir(activeProfile);
        qDebug() << "Profile:" << activeProfile
                 << "config dir:" << logDir;
    }

    // Phase 3G-6 block 5: bring up the MMIO subsystem so persisted
    // endpoints (under AppSettings MmioEndpoints/<guid>/*) start
    // their transport workers before the main window is shown.
    NereusSDR::ExternalVariableEngine::instance().init();

    // ── Remote-daemon R2 Task 20: resolve the station, then install the
    //    settings proxy BEFORE the window exists ──────────────────────────
    //
    // Saved-Setup fallback runs here, after CoreInit::initialize() has
    // actually loaded AppSettings. Command line wins: an operator who typed
    // --station on a machine that also has one saved means the one they
    // just typed.
    {
        NereusSDR::AppSettings& s = NereusSDR::AppSettings::instance();
        if (station.url.isEmpty()) {
            station.url = s.value(QStringLiteral("RemoteStationUrl"),
                                  QString()).toString();
        }
        if (station.token.isEmpty()) {
            station.token = s.value(QStringLiteral("RemoteStationToken"),
                                    QString()).toString();
        }
        if (station.fingerprint.isEmpty()) {
            station.fingerprint =
                s.value(QStringLiteral("RemoteStationFingerprint"),
                        QString()).toString();
        }
        if (!station.allowUnpinned) {
            station.allowUnpinned =
                s.value(QStringLiteral("RemoteStationAllowUnpinned"),
                        QStringLiteral("False")).toString()
                == QStringLiteral("True");
        }

        // A malformed URL falls back to local direct mode with a message on
        // stderr, rather than starting a GUI whose every control silently
        // does nothing because its RadioModel is Remote and nothing was
        // ever dialled.
        QString whyNot;
        if (!station.url.isEmpty()
            && !NereusSDR::RemoteStationOptions::isValidStationUrl(station.url,
                                                                   &whyNot)) {
            fprintf(stderr,
                    "NereusSDR: ignoring station address '%s': %s\n"
                    "           Starting in local direct mode.\n",
                    station.url.toLocal8Bit().constData(),
                    whyNot.toLocal8Bit().constData());
            station.url.clear();
        }
    }

    // The proxy MUST be installed before MainWindow constructs its
    // RadioModel, and MUST still report ready() == false while that
    // construction runs. SliceModel, NotchModel, FilterPresetStore and
    // TciServer all seed Station-classified keys if absent in their
    // constructors; what keeps those ship defaults out of the STATION store
    // is entirely that the proxy is not ready yet and drops the write. See
    // SettingsProxy.h's "ready()==false is load-bearing beyond this class"
    // section, which names R2 Task 20 as the task that had to confirm the
    // ordering rather than inherit it. tst_remote_gui_gating pins it.
    //
    // Declared before `window` so it outlives it: MainWindow's destructor
    // can still touch AppSettings, and AppSettings would then be holding a
    // dangling backend if these two were the other way round.
    std::unique_ptr<NereusSDR::SettingsProxy> settingsProxy;
    if (station.isRemote()) {
        settingsProxy = std::make_unique<NereusSDR::SettingsProxy>();
        NereusSDR::AppSettings::instance().setRemoteBackend(settingsProxy.get());
        qDebug() << "Remote station mode:" << station.url;
    }

    NereusSDR::MainWindow window(station);
    window.show();

    const int rc = app.exec();

    // Detach before the proxy is destroyed. AppSettings is a singleton and
    // outlives both, so leaving the pointer in place would be a dangling
    // read on any late settings access during static teardown.
    NereusSDR::AppSettings::instance().setRemoteBackend(nullptr);

    // Graceful shutdown so worker threads drain before the engine
    // singleton is destroyed.
    NereusSDR::ExternalVariableEngine::instance().shutdown();

    // Uninstalls the custom message handler and closes the log file that
    // CoreInit::initialize() installed above. See src/core/CoreInit.cpp
    // for why the handler teardown has to be safe even if Qt logs
    // something between here and its own thread-storage teardown.
    NereusSDR::CoreInit::shutdown();
    return rc;
}
