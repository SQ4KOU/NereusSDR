#pragma once
// =================================================================
// src/core/daemon/DaemonConfig.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. R1 Task 9
// (docs/architecture/2026-08-02-remote-daemon-r1-plan.md, Task 9): the
// headless nereusd daemon's own configuration, read from a plain
// "key = value" text file (default /etc/nereusd.conf, overridable with
// nereusd --config <path>) instead of AppSettings' XML store. A Pi-hosted
// systemd service wants a single flat file an operator can hand-edit and a
// package can drop a default copy of, not the GUI client's per-user
// ~/.config/NereusSDR/NereusSDR.settings.
//
// On-disk format: "key = value" lines. '#' starts a comment, whether it is
// the whole line or trails a value; blank lines are ignored; leading and
// trailing whitespace around both key and value is trimmed. Unknown keys
// log a warning (via LogCategories' lcApp) and are otherwise ignored --
// never a hard failure -- so a config file written for a newer nereusd
// still starts an older one instead of refusing to boot. A malformed value
// for a known numeric key (sample_rate_hz, slice_count) is likewise logged
// and the field is left at whatever it already was, rather than being
// clobbered with 0. The optional display-limit pair is stricter: an explicit
// malformed or incomplete pair fails validate(), never disables enforcement.
//
// sliceCount's further clamp to the connected board's
// BoardCapabilities::maxSlices happens once a radio is actually discovered
// (R1 Task 10, DaemonApp): this struct is parsed before any radio is
// contacted, radioMac may be empty (meaning "first responder", so the
// board is not even known yet), and BoardCapabilities' maxSlices is a
// per-SKU field (2 to 5 across the current board table) with no
// board-independent ceiling to check here. validate() below therefore only
// enforces the generic floor of 1.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-02: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
//   2026-09-23: thread_placement key (R-R3-41). J.J. Boyd (KG4VCF), with
//               AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include <QString>
#include <optional>

#include "core/session/media/DisplayBudget.h"

namespace NereusSDR {

// Parsed, validated configuration for one nereusd process. See the file
// header above for the on-disk format and the design rationale.
//
// Every field here has a production consumer. An earlier revision shipped
// a `logLevel` field that nothing read, alongside a nereusd.conf.sample
// documenting it, which is why the rule is now written down: a key that
// reaches this struct must reach the daemon's behaviour too, or it does
// not belong in the sample file. Log verbosity is Qt's own
// QT_LOGGING_RULES environment variable instead (verified to take
// precedence over the QLoggingCategory::setFilterRules() call
// LogManager makes), set from the systemd unit, which needs no field
// here and no code at all.
struct DaemonConfig {
    QString radioMac;                          // empty = first discovered
    int     sampleRateHz {192000};             // seeded into the per-MAC
                                                // AppSettings key the shared
                                                // connect path reads; see
                                                // DaemonApp::applyConfigToSettings
    // True only when sample_rate_hz was actually present in the config
    // file. sampleRateHz alone cannot express "unset", because validate()
    // rejects <= 0 and so the field must always hold a usable rate. Without
    // this flag, a bare `nereusd` with no config file (a non-fatal case:
    // server_main logs a warning and continues with defaults) would stamp
    // the 192000 default over whatever rate the operator had already
    // persisted for that radio, and the GUI would come up at the wrong rate
    // on its next launch. Writing per-MAC settings is a side effect on
    // shared user state, so it happens only on an explicit request.
    bool    sampleRateExplicit {false};
    int     sliceCount   {1};                  // see header comment: the
                                                // board-specific ceiling is
                                                // applied later, by R1 Task 10
    QString audioDevice;                       // empty = platform default

    // ── Remote Daemon R2 Task 18: the wss control plane ──────────────────
    //
    // remotePort 0 means DO NOT LISTEN, and that is the default on
    // purpose. A daemon that binds a listener on first install, with a
    // pairing token printed to a log the operator may not have read, is a
    // worse default than one line of config: R2's own demo is two
    // processes on one host (design addendum section 2), so nothing needs
    // a listener until somebody asks for one. Turning it on is
    // `remote_port = <port>`.
    //
    // remoteBind defaults to loopback for the same reason. An operator who
    // wants the daemon reachable from another machine sets it explicitly,
    // which is also the moment they are thinking about who can reach it.
    //
    // Both feed StationServer::listen() from DaemonApp::start(); see
    // packaging/nereusd.conf.sample, and note that a key reaching this
    // struct must reach behaviour AND the sample, which
    // tst_daemon_config's sampleFileKeysAndParserKeysAgree pins.
    QString coreName;                         // empty = machine hostname for LAN discovery
    int     remotePort {0};
    QString remoteBind {QStringLiteral("127.0.0.1")};

    // R-R3-23: the Opus encoder target for station audio, bit/s. Only the
    // two supported profiles are accepted (OpusAudioEncoder refuses any
    // other; 24000 is the measured default, 48000 is not measured yet);
    // anything else in the file logs one warning and keeps 24000.
    // Feeds DaemonMediaController::setAudioTargetBitrate() from
    // DaemonApp::startStationServer().
    static constexpr int kDefaultAudioBitrate = 24000;
    static constexpr int kHighAudioBitrate = 48000;
    int     audioBitrate {kDefaultAudioBitrate};

    // R-R3-41: thread_placement = auto (default) runs each busy signal
    // processing thread on a fast core of its own, chosen from the kernel's
    // CPU capacity data, and keeps every other thread off those cores
    // (src/core/platform/ThreadPlacement.h, Linux only). off leaves every
    // thread free to run on any core. Anything else logs one warning and
    // keeps auto. Feeds startDaemonThreadPlacement() in server_main.cpp.
    bool    threadPlacement {true};

    // Optional measured limits, supplied as a pair. A malformed explicit
    // value becomes zero so validate() fails instead of disabling the cap.
    std::optional<quint64> displayApplicationBytesPerSecond;
    std::optional<quint64> spectrumSampleUnitsPerSecond;
    std::optional<DisplayBudgetLimits> displayBudgetLimits() const;

    // Reads and parses `path`. If the file cannot be opened, returns
    // defaults() with *errorOut set to a human-readable message describing
    // why; the caller decides whether that is fatal (src/server_main.cpp
    // logs it as a warning and continues with defaults -- a missing config
    // is not by itself a startup error, since a bare `nereusd` invocation
    // for local testing should still come up with sane values). On a
    // successful open, *errorOut is cleared, even if individual lines
    // inside the file were skipped with a logged warning.
    static DaemonConfig fromFile(const QString& path, QString* errorOut);

    // The struct's own default member initializers, as a value. Always
    // passes validate().
    static DaemonConfig defaults();

    // Generic sanity checks only; see the sliceCount comment above for why
    // the board-specific ceiling lives elsewhere. Returns false and fills
    // *errorOut with a human-readable reason on the first check that
    // fails; *errorOut is cleared on success.
    bool validate(QString* errorOut) const;
};

// Resolves nereusd's --profile command-line argument (R1 Task 9 fix round
// 1: this gap was flagged in Task 8's review, before Task 9 existed, as
// "Note for Task 9's daemon caller", but never reached this task's brief).
// Without it, every invocation of nereusd on a developer workstation reads
// and writes the SAME ~/.config/NereusSDR (or ~/Library/Preferences/
// NereusSDR on macOS) the real GUI client uses -- there is no isolation
// analogous to main.cpp's --profile, which is exactly the trap that
// produced task-9-report.md section 5.
//
// Remote Daemon R2, Task 1: the "share by default" behaviour this
// function used to implement was itself a trap one layer up. A daemon
// that silently reads and writes the GUI's own settings file lets R2's
// state-mirroring feature (SettingsProxy, R2 Task 15) pass its own
// verification while doing nothing at all -- a "remote" GUI would look
// correct because it was reading the SAME on-disk file the local daemon
// just wrote, not because anything was actually mirrored over the wire.
// See docs/architecture/2026-08-03-remote-daemon-r2-r3-design-addendum.md
// §2.1. The reserved profile below is the fix; sharing is now opt-in
// instead of the silent default.
//
// `requested` is the raw --profile value from QCommandLineParser (empty
// both when the option was not given and when it was given an empty
// value -- QCommandLineOption has no default here, so
// QCommandLineParser::value() cannot tell the two apart on its own).
// `wasSet` is parser.isSet(profileOpt):
//
//   wasSet == false             -> returns AppSettings::kDaemonProfileName
//     ("daemon"). No --profile on the command line at all now reserves
//     nereusd's own profile rather than silently sharing the GUI's.
//   wasSet == true, requested.isEmpty() -> returns an empty string,
//     meaning "share the GUI's own directory". Explicitly typing
//     --profile "" is the deliberate escape hatch for nereusd's primary
//     deployment (a single systemd-managed daemon on a Pi with no GUI to
//     collide with) -- opt-in now, not silent.
//   wasSet == true, requested non-empty -> validated against
//     AppSettings::isValidProfileName() as described below.
//
// A non-empty value must pass AppSettings::isValidProfileName(); on
// failure, returns an empty string and *errorOut is set to a human-
// readable reason. The caller (src/server_main.cpp) treats a non-empty
// *errorOut as fatal and refuses to start, rather than silently falling
// back to the shared directory the way a mistyped GUI --profile does
// (main.cpp only warns and continues) -- a daemon provisioning mistake in
// a systemd unit file should be loud, not silently ignored.
//
// Pure function: does not call AppSettings::setProfileOverride() itself.
// The caller is responsible for that (and must do so before AppSettings::
// instance() is first touched anywhere in the process -- see
// server_main.cpp's own comment on why the ordinary post-QCoreApplication
// QCommandLineParser path is sufficient here, unlike main.cpp's pre-
// QApplication argv scan).
QString resolveDaemonProfileArgument(const QString& requested, bool wasSet,
                                     QString* errorOut);

} // namespace NereusSDR
