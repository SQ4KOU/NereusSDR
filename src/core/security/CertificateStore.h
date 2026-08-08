#pragma once
// =================================================================
// src/core/security/CertificateStore.h  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original. Remote Daemon R2, Task 17.
//
// Design source: docs/architecture/2026-07-28-remote-daemon-architecture-
// design.md §10.5 "Encryption, and which phase owns it":
//
//   "Certificate model. A headless Pi has no domain name, so the daemon
//   generates a self-signed certificate on first run and the client pins
//   its fingerprint, displayed at pairing time alongside the token
//   (§7.1)."
//
// Both remote-daemon design documents were searched (2026-08-08) for a
// key algorithm, key size, validity period, or fingerprint display
// format, and neither specifies any of the four -- see
// docs/architecture/2026-08-02-remote-station-identity-and-pairing-
// design.md §10.2, which lists "Certificate handling" as a component and
// cross-references it back to "already parent R2" (this document)
// without further detail, and its own §13 open item 10 ("Default
// rendezvous hostname, certificate strategy, and operational ownership.
// Not a code question, but it blocks packaging") which confirms the gap
// is known and still open. This task's choices, recorded once here
// rather than scattered across the .cpp:
//
//   - RSA 2048 / SHA-256, not ECDSA. Slightly larger and slower to
//     generate, but the safer default against an unknown-until-runtime
//     Qt TLS backend (see tlsBackendDiagnostic()) -- RSA has no curve-
//     selection dimension to get wrong.
//   - 10-year validity (see generateAndStore() in the .cpp). No renewal
//     mechanism exists yet, and because the client PINS this
//     certificate's fingerprint (parent design §10.5), regenerating it
//     later (expiry, or an operator wiping the profile) breaks every
//     already-paired client until they re-pair. A long runway is a
//     deliberate hedge against that, not a fix for the missing rotation
//     story -- whichever task owns pairing/re-pairing should decide the
//     real one.
//   - Fingerprint display: colon-separated uppercase SHA-256 hex pairs
//     (see fingerprintSha256() below), the conventional X.509 form. This
//     task's own choice in the documented absence of a specified one.
//
// SCOPE BOUNDARY: this class provisions a certificate and a private key,
// and nothing else. No socket, no handshake, no token -- those belong to
// Task 18 (the wss session) and its TokenStore. If you came here to add
// transport code, it belongs somewhere else.
//
// NOT THE SAME THING AS: the identity/pairing design doc's §2-3 describe
// a separate "asymmetric key pair... generated on first run, held by the
// machine" that is the station's identity for pairing and for deriving
// the rendezvous's scrambled registration name. That is a different
// artifact from the TLS certificate provisioned here, even though both
// are "a key pair generated on first run" in the abstract. Do not assume
// this class's RSA key can stand in for that identity key without an
// explicit decision to reuse it -- the two design sections were scoped
// apart on purpose (identity doc §10.2's "Certificate handling, already
// parent R2" cross-reference is what draws the line).
//
// AI tooling: Anthropic Claude Code.
// =================================================================
// Modification history (NereusSDR):
//   2026-08-08: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include <QSsl>
#include <QSslCertificate>
#include <QSslKey>
#include <QString>

namespace NereusSDR {

// Generates (first run) or loads (every run after) a self-signed TLS
// identity for nereusd's wss:// listener: one RSA-2048 key pair and one
// X.509 certificate, PEM-encoded, stored beside the daemon profile's own
// settings file.
//
// Construction does all the work, synchronously -- matching this
// codebase's other small provisioning-on-construct core classes (see
// FaultLog::FaultLog, which loads its ring buffer the same way in its
// constructor). There is no separate "ensure" call to remember to invoke;
// check isValid() right after constructing.
class CertificateStore {
public:
    // The key algorithm every certificate/key this class produces or
    // accepts uses. A public constant (rather than a magic QSsl::Rsa
    // scattered at every call site) so callers that need to construct
    // their own QSslKey from this store's PEM files -- Task 18, or this
    // class's own tests -- stay in sync with whatever this task chose.
    static constexpr QSsl::KeyAlgorithm kKeyAlgorithm = QSsl::Rsa;

    // directory is where cert.pem / key.pem live; it is created
    // (mkpath, recursively) if it does not exist yet. Defaults to the
    // daemon profile's own config directory -- see defaultDirectory().
    // Tests pass an explicit scratch directory (a QTemporaryDir path) so
    // a test run never touches, or reads back, a real profile's TLS
    // identity.
    explicit CertificateStore(const QString& directory = defaultDirectory());

    // AppSettings::resolveConfigDir(AppSettings::kDaemonProfileName), the
    // production default -- resolved through that function, never
    // rebuilt by hand (task-17-controller-notes.md). A free function so
    // a caller can read where the real one lives without constructing a
    // CertificateStore.
    static QString defaultDirectory();

    // True once a certificate and private key are loaded and usable.
    // False means lastError() names why: OpenSSL key/certificate
    // generation failed, the on-disk files exist but do not parse as
    // either, or Qt itself reports no working TLS backend (see
    // tlsBackendDiagnostic(), whose message becomes part of lastError()
    // in that case).
    bool isValid() const { return m_valid; }

    // Empty when isValid() is true.
    QString lastError() const { return m_lastError; }

    // Empty/null when isValid() is false.
    QSslCertificate certificate() const { return m_certificate; }
    QSslKey privateKey() const { return m_privateKey; }

    // Absolute paths to the PEM files this instance loaded or wrote.
    // Empty when isValid() is false and generation never reached the
    // point of choosing paths (directory could not be created).
    QString certificatePath() const { return m_certPath; }
    QString privateKeyPath() const { return m_keyPath; }

    // SHA-256 digest of the certificate's DER encoding, formatted as 32
    // colon-separated uppercase hex byte pairs (e.g.
    // "AB:12:CD:...:FF") -- what `openssl x509 -fingerprint -sha256`
    // prints, minus its "SHA256 Fingerprint=" label. See the top-of-file
    // note: neither design document specifies a display format for the
    // "TLS fingerprint, displayed at pairing time" (parent design
    // §7.1/§10.5); this is this task's chosen default.
    //
    // Computed directly from the OpenSSL X509* at generation/load time
    // (X509_digest(), not QSslCertificate::digest()), so it stays
    // available even when tlsBackendDiagnostic() reports a degraded Qt
    // TLS backend -- Task 18 wants something printable to qCInfo on
    // first run regardless of whether the backend can actually complete
    // a handshake yet. Empty when isValid() is false.
    QString fingerprintSha256() const { return m_fingerprint; }

    // Empty when Qt has a working TLS backend (QSslSocket::supportsSsl()
    // == true). Otherwise names the cause as specifically as this
    // platform allows -- in particular, a Windows Qt build whose only
    // compiled-in backend is Schannel, called out explicitly because a
    // generic "TLS unavailable" message costs a maintainer real
    // debugging time (task-17-brief.md Step 4). Static and independent
    // of any instance: this is a property of the Qt build in use, not of
    // a particular certificate, so it is callable without provisioning
    // anything.
    static QString tlsBackendDiagnostic();

private:
    // True if directory already held a cert + key that both parsed
    // successfully; false (and generateAndStore() must run instead) on a
    // missing, partial, or corrupt pair.
    bool loadExisting();

    // Creates a fresh RSA-2048 key pair and self-signed certificate,
    // writes both to m_certPath / m_keyPath (restrictive permissions on
    // the key), and populates m_certificate / m_privateKey /
    // m_fingerprint from the freshly generated bytes. Returns false (and
    // sets m_lastError) on any OpenSSL failure.
    bool generateAndStore();

    QString m_directory;
    QString m_certPath;
    QString m_keyPath;
    bool    m_valid{false};
    QString m_lastError;
    QSslCertificate m_certificate;
    QSslKey         m_privateKey;
    QString m_fingerprint;
};

} // namespace NereusSDR
