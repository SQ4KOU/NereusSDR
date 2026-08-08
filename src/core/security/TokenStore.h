#pragma once
// =================================================================
// src/core/security/TokenStore.h  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original. Remote Daemon R2, Task 18.
//
// Design source: docs/architecture/2026-07-28-remote-daemon-architecture-
// design.md section 7.1 "Session model":
//
//   "Authentication is a generated pre-shared token, never user-chosen,
//   rate-limited on failure. The token distribution mechanism must be
//   specified before R2: daemon console output on first run, plus the
//   desktop Setup toggle, alongside the TLS fingerprint (section 10.5)."
//
// This class owns the first half of that sentence: generate, persist,
// and verify. The console output on first run belongs to StationServer
// (src/core/session/StationServer.cpp), which prints this store's token
// beside CertificateStore::fingerprintSha256() the one time the token is
// freshly generated.
//
// ---- Why a file beside the daemon profile, not an AppSettings key ----
//
// A token in AppSettings would be a secret sitting in the same XML the
// operator backs up, mails to a maintainer with a bug report, and syncs
// between machines. It would also land inside the reach of
// AppSettings::allKeys(), which SettingsProxyServer::buildSnapshot()
// scans, so keeping it out of the store is one fewer way for it to
// escape even by accident. CertificateStore already writes the TLS key
// beside the daemon profile rather than into settings for the same
// reason; this class mirrors that shape deliberately, down to the
// constructor doing all the work synchronously and the caller checking
// isValid() right afterwards.
//
// ---- Never user-chosen ----
//
// There is no setToken(). 256 bits from QRandomGenerator::system() (the
// OS CSPRNG, not the deterministic default engine), base64url without
// padding, so the printed form is one copy-pasteable word with no shell
// quoting hazard. An operator who wants a new token deletes the file;
// the next daemon start generates and prints a fresh one.
//
// ---- Rate limiting ----
//
// verify() counts CONSECUTIVE failures. On reaching maxFailuresPerLockout()
// it refuses every further attempt -- including one carrying the correct
// token -- until lockoutMs() has elapsed since the most recent failure,
// then starts a fresh count. A success at any point resets the count to
// zero. RateLimited is a DISTINCT result from Rejected because the two
// mean different things to the caller: Rejected is "that token is wrong",
// RateLimited is "I am not answering that question right now", and a
// station that collapsed them would leak, through timing and through its
// own logs, exactly which guesses were close.
//
// The comparison itself is constant-time over the SHA-256 of both sides
// rather than over the raw strings: hashing first makes the loop length
// independent of the candidate's length, so a caller cannot learn the
// stored token's length by timing candidates of different sizes.
//
// SCOPE BOUNDARY: this class generates, persists and checks one shared
// secret. It knows nothing about sockets, sessions, TLS, or who is
// asking. Pairing, rotation, and per-client tokens are later phases (see
// docs/architecture/2026-08-02-remote-station-identity-and-pairing-
// design.md); nothing here should grow toward them without that design
// being read first.
//
// AI tooling: Anthropic Claude Code.
// =================================================================
// Modification history (NereusSDR):
//   2026-08-08: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include <QElapsedTimer>
#include <QString>

namespace NereusSDR {

class TokenStore {
public:
    enum class VerifyResult {
        Accepted,     // matches the stored token
        Rejected,     // does not match
        RateLimited,  // too many consecutive failures; not even checked
    };

    // Consecutive failures tolerated before verify() starts returning
    // RateLimited, and how long the refusal lasts. Chosen so a human
    // fat-fingering a paste gets several tries, while an automated
    // guesser is held to roughly five attempts per minute against a
    // 256-bit secret. Both are overridable via setRateLimit() -- the
    // tests drive them down to single-digit milliseconds so a rate-limit
    // case does not cost a minute of wall clock.
    static constexpr int kDefaultMaxFailuresPerLockout = 5;
    static constexpr int kDefaultLockoutMs = 60000;

    // directory is where the token file lives; it is created (mkpath,
    // recursively) if it does not exist. Defaults to the daemon
    // profile's own config directory, the same place CertificateStore
    // keeps tls-cert.pem / tls-key.pem. Tests pass an explicit scratch
    // directory so a run never reads back, or overwrites, a real
    // station's token.
    explicit TokenStore(const QString& directory = defaultDirectory());

    // AppSettings::resolveConfigDir(AppSettings::kDaemonProfileName),
    // resolved through that function rather than rebuilt by hand.
    static QString defaultDirectory();

    // True once a token is loaded or generated. False means lastError()
    // names why: the directory could not be created, the file exists but
    // could not be read, or it could not be written.
    bool isValid() const { return m_valid; }

    // Empty when isValid() is true.
    QString lastError() const { return m_lastError; }

    // Empty when isValid() is false.
    QString token() const { return m_token; }

    // Absolute path this instance loads from / writes to. Always
    // populated, derived from the constructor's directory argument,
    // regardless of isValid().
    QString tokenPath() const { return m_tokenPath; }

    // True only when THIS construction created the token, i.e. a genuine
    // first run for this profile. StationServer gates its qCInfo banner
    // on this so the secret is printed once, at the moment the operator
    // needs to copy it, rather than into every log file forever.
    bool wasGeneratedThisRun() const { return m_generatedThisRun; }

    // See the class comment for the three results and the constant-time
    // comparison. Not const: it maintains the failure counter.
    VerifyResult verify(const QString& candidate);

    // Overrides the two constants above. A maxFailures below 1 is
    // clamped to 1 (zero would mean "rate-limited before the first
    // attempt", which would lock the station out of itself); a negative
    // lockout is clamped to 0.
    void setRateLimit(int maxFailures, int lockoutMs);

    int maxFailuresPerLockout() const { return m_maxFailures; }
    int lockoutMs() const { return m_lockoutMs; }

    // Consecutive failures since the last success (or since the last
    // lockout expired). Diagnostics and tests.
    int consecutiveFailures() const { return m_consecutiveFailures; }

    // True while verify() would return RateLimited without checking.
    bool isRateLimited() const;

private:
    bool loadExisting();
    bool generateAndStore();

    QString m_directory;
    QString m_tokenPath;
    bool    m_valid{false};
    bool    m_generatedThisRun{false};
    QString m_lastError;
    QString m_token;

    int m_maxFailures{kDefaultMaxFailuresPerLockout};
    int m_lockoutMs{kDefaultLockoutMs};
    int m_consecutiveFailures{0};

    // Restarted on every failure. Invalid (never started) until the
    // first one, which is what isRateLimited() checks before reading it.
    QElapsedTimer m_sinceLastFailure;
};

} // namespace NereusSDR
