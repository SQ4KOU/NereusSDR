// =================================================================
// src/core/security/CertificateStore.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original. See CertificateStore.h for the full
// design rationale (key algorithm, validity period, and fingerprint
// format choices, all made in the documented absence of a design-doc
// specification for any of the three).
//
// AI tooling: Anthropic Claude Code.
// =================================================================

#include "core/security/CertificateStore.h"

#include "core/AppSettings.h"
#include "core/LogCategories.h"

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/x509.h>

#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QSaveFile>
#include <QSslSocket>

#include <memory>

namespace NereusSDR {

namespace {

// RAII wrappers over the OpenSSL C API. "No raw new/delete" (CLAUDE.md
// C++ style guide) applies here in spirit even though these are
// alloc/free function pairs rather than new/delete: without this, every
// early-return path below (and there are several -- each OpenSSL call
// can fail) would need its own manual cleanup, which is exactly the
// leak-on-early-return shape unique_ptr exists to remove.
using EvpPkeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using X509Ptr    = std::unique_ptr<X509, decltype(&X509_free)>;
using BioPtr      = std::unique_ptr<BIO, decltype(&BIO_free)>;

EvpPkeyPtr makeEvpPkeyPtr(EVP_PKEY* p) { return EvpPkeyPtr(p, &EVP_PKEY_free); }
X509Ptr    makeX509Ptr(X509* p)        { return X509Ptr(p, &X509_free); }
BioPtr     makeBioPtr(BIO* p)          { return BioPtr(p, &BIO_free); }

// RSA key size in bits. NereusSDR-original choice (see CertificateStore.h
// top-of-file note): neither remote-daemon design document specifies an
// algorithm or size for this certificate. 2048 is the conventional floor
// for an RSA certificate expected to remain in service for years.
constexpr int kRsaKeyBits = 2048;

// Certificate validity window. NereusSDR-original choice, see
// CertificateStore.h: no rotation mechanism exists yet, and the client
// pins this certificate's fingerprint, so a long runway is deliberate.
constexpr long kNotBeforeSkewSeconds = -60L * 60L * 24L;             // -1 day
constexpr long kValiditySeconds      = 60L * 60L * 24L * 365L * 10L; // 10 years

// Subject/issuer CN for the self-signed certificate. Not verified by
// anything -- the client pins the fingerprint instead (parent design
// §10.5) -- so this exists only to give the certificate a human-legible
// label if someone inspects it by hand (e.g. `openssl x509 -text`).
const char* const kSubjectCommonName = "nereusd";

QString drainOpenSslErrors()
{
    QStringList parts;
    unsigned long code;
    char buf[256];
    while ((code = ERR_get_error()) != 0) {
        ERR_error_string_n(code, buf, sizeof(buf));
        parts << QString::fromLatin1(buf);
    }
    if (parts.isEmpty()) {
        return QStringLiteral("unknown OpenSSL error");
    }
    return parts.join(QStringLiteral("; "));
}

// Formats a SHA-256 digest as 32 colon-separated uppercase hex byte
// pairs -- see CertificateStore.h's fingerprintSha256() doc comment for
// why this format and not another.
QString formatFingerprint(const unsigned char* digest, unsigned int len)
{
    QStringList bytes;
    bytes.reserve(static_cast<int>(len));
    for (unsigned int i = 0; i < len; ++i) {
        bytes << QStringLiteral("%1").arg(digest[i], 2, 16, QLatin1Char('0'))
                                      .toUpper();
    }
    return bytes.join(QLatin1Char(':'));
}

// Reads the SHA-256 fingerprint straight from an OpenSSL X509*, via
// X509_digest() -- not through QSslCertificate::digest(). See
// CertificateStore.h: this keeps the fingerprint available even when
// tlsBackendDiagnostic() reports a degraded Qt TLS backend.
QString fingerprintOf(X509* cert)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    if (X509_digest(cert, EVP_sha256(), digest, &len) != 1) {
        return QString();
    }
    return formatFingerprint(digest, len);
}

// Serializes an X509*/EVP_PKEY* pair to PEM bytes via in-memory BIOs, so
// the same bytes can be written to disk AND handed to QSslCertificate/
// QSslKey without a redundant round trip through the filesystem.
bool pemEncode(X509* cert, EVP_PKEY* pkey, QByteArray* certPemOut, QByteArray* keyPemOut)
{
    BioPtr certBio = makeBioPtr(BIO_new(BIO_s_mem()));
    BioPtr keyBio  = makeBioPtr(BIO_new(BIO_s_mem()));
    if (!certBio || !keyBio) {
        return false;
    }
    if (PEM_write_bio_X509(certBio.get(), cert) != 1) {
        return false;
    }
    // No passphrase: nereusd runs unattended (systemd unit, headless
    // boot), so an encrypted key would need a passphrase prompt this
    // process has nowhere to show. File permissions are the protection
    // mechanism instead (see writePemFile() below) -- the same trust
    // model most daemon TLS keys (sshd, nginx) use.
    if (PEM_write_bio_PrivateKey(keyBio.get(), pkey, nullptr, nullptr, 0,
                                  nullptr, nullptr) != 1) {
        return false;
    }

    char* certData = nullptr;
    const long certLen = BIO_get_mem_data(certBio.get(), &certData);
    char* keyData = nullptr;
    const long keyLen = BIO_get_mem_data(keyBio.get(), &keyData);
    if (certLen <= 0 || keyLen <= 0) {
        return false;
    }
    *certPemOut = QByteArray(certData, static_cast<int>(certLen));
    *keyPemOut  = QByteArray(keyData, static_cast<int>(keyLen));
    return true;
}

// Atomic write (QSaveFile, matching AppSettings::save()'s own pattern)
// plus, for the private key only, owner-only permissions. restrictToOwner
// mirrors AppSettings::save()'s QFile::setPermissions() call after
// commit() -- called unconditionally (not #ifdef Q_OS_UNIX guarded): on
// Windows QFileDevice::ReadOwner|WriteOwner is still a reasonable
// best-effort ACL restriction, it just cannot be verified back with
// QFile::permissions() the same way (see tests/tst_certificate_store.cpp's
// privateKeyFileIsOwnerOnlyOnUnix() for why the *test* assertion, not
// this call, is what is UNIX-only).
bool writePemFile(const QString& path, const QByteArray& pem, bool restrictToOwner)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    if (file.write(pem) != pem.size()) {
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        return false;
    }
    if (restrictToOwner) {
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
    return true;
}

} // namespace

CertificateStore::CertificateStore(const QString& directory)
    : m_directory(directory)
    , m_certPath(directory + QStringLiteral("/tls-cert.pem"))
    , m_keyPath(directory + QStringLiteral("/tls-key.pem"))
{
    QDir().mkpath(m_directory);

    if (!loadExisting() && !generateAndStore()) {
        // m_lastError already set by whichever of the two set it.
        return;
    }

    // Independent of whether provisioning itself succeeded: a certificate
    // this Qt build cannot actually use for a handshake later is still
    // worth flagging now, at provisioning time, rather than leaving the
    // operator to discover it awkwardly during Task 18's connection
    // attempt. See CertificateStore.h's tlsBackendDiagnostic() doc
    // comment and task-17-brief.md Step 4.
    const QString backendIssue = tlsBackendDiagnostic();
    if (!backendIssue.isEmpty()) {
        m_valid = false;
        m_lastError = backendIssue;
        qCWarning(lcApp) << "CertificateStore:" << backendIssue;
    }
}

QString CertificateStore::defaultDirectory()
{
    return AppSettings::resolveConfigDir(
        QString::fromLatin1(AppSettings::kDaemonProfileName));
}

bool CertificateStore::loadExisting()
{
    QFile certFile(m_certPath);
    QFile keyFile(m_keyPath);
    if (!certFile.exists() || !keyFile.exists()) {
        return false;
    }
    if (!certFile.open(QIODevice::ReadOnly) || !keyFile.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray certPem = certFile.readAll();
    const QByteArray keyPem  = keyFile.readAll();

    BioPtr certBio = makeBioPtr(BIO_new_mem_buf(certPem.constData(), certPem.size()));
    BioPtr keyBio  = makeBioPtr(BIO_new_mem_buf(keyPem.constData(), keyPem.size()));
    if (!certBio || !keyBio) {
        return false;
    }

    X509Ptr cert = makeX509Ptr(PEM_read_bio_X509(certBio.get(), nullptr, nullptr, nullptr));
    EvpPkeyPtr pkey = makeEvpPkeyPtr(
        PEM_read_bio_PrivateKey(keyBio.get(), nullptr, nullptr, nullptr));
    if (!cert || !pkey) {
        // Corrupt or foreign-format file. Not an error yet -- fall
        // through to generateAndStore() and treat this the same as a
        // first run (see the class-level doc comment and
        // tst_certificate_store.cpp's regeneratesWhenStoredFilesAreCorrupt()).
        ERR_clear_error();
        return false;
    }

    const QString fingerprint = fingerprintOf(cert.get());
    if (fingerprint.isEmpty()) {
        return false;
    }

    m_certificate = QSslCertificate(certPem, QSsl::Pem);
    m_privateKey  = QSslKey(keyPem, kKeyAlgorithm, QSsl::Pem, QSsl::PrivateKey);
    if (m_certificate.isNull() || m_privateKey.isNull()) {
        return false;
    }

    m_fingerprint = fingerprint;
    m_valid = true;
    m_lastError.clear();
    return true;
}

bool CertificateStore::generateAndStore()
{
    // OpenSSL 3.0's one-call key generator. See CertificateStore.h's
    // top-of-file note for why RSA over ECDSA, and the find_package(
    // OpenSSL 3.0 ...) block in the top-level CMakeLists.txt for the
    // version floor this call depends on.
    EvpPkeyPtr pkey = makeEvpPkeyPtr(EVP_RSA_gen(kRsaKeyBits));
    if (!pkey) {
        m_lastError = QStringLiteral("RSA key generation failed: %1")
                          .arg(drainOpenSslErrors());
        qCWarning(lcApp) << "CertificateStore:" << m_lastError;
        return false;
    }

    X509Ptr cert = makeX509Ptr(X509_new());
    if (!cert) {
        m_lastError = QStringLiteral("X.509 certificate allocation failed: %1")
                          .arg(drainOpenSslErrors());
        qCWarning(lcApp) << "CertificateStore:" << m_lastError;
        return false;
    }

    X509_set_version(cert.get(), 2L); // X.509v3 (0-indexed: 2 == v3)

    // Random positive 63-bit serial (RAND_bytes then clear the sign bit,
    // so ASN1_INTEGER_set_int64 always encodes a positive INTEGER).
    unsigned char serialBytes[8];
    if (RAND_bytes(serialBytes, sizeof(serialBytes)) != 1) {
        m_lastError = QStringLiteral("Random serial generation failed: %1")
                          .arg(drainOpenSslErrors());
        qCWarning(lcApp) << "CertificateStore:" << m_lastError;
        return false;
    }
    serialBytes[0] &= 0x7Fu;
    int64_t serial = 0;
    for (unsigned char b : serialBytes) {
        serial = (serial << 8) | static_cast<int64_t>(b);
    }
    ASN1_INTEGER_set_int64(X509_get_serialNumber(cert.get()), serial);

    X509_gmtime_adj(X509_getm_notBefore(cert.get()), kNotBeforeSkewSeconds);
    X509_gmtime_adj(X509_getm_notAfter(cert.get()), kValiditySeconds);

    X509_set_pubkey(cert.get(), pkey.get());

    X509_NAME* name = X509_get_subject_name(cert.get());
    X509_NAME_add_entry_by_txt(
        name, "CN", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>(kSubjectCommonName), -1, -1, 0);
    X509_set_subject_name(cert.get(), name);
    X509_set_issuer_name(cert.get(), name); // self-signed: issuer == subject

    if (X509_sign(cert.get(), pkey.get(), EVP_sha256()) == 0) {
        m_lastError = QStringLiteral("Certificate signing failed: %1")
                          .arg(drainOpenSslErrors());
        qCWarning(lcApp) << "CertificateStore:" << m_lastError;
        return false;
    }

    QByteArray certPem;
    QByteArray keyPem;
    if (!pemEncode(cert.get(), pkey.get(), &certPem, &keyPem)) {
        m_lastError = QStringLiteral("PEM encoding failed: %1")
                          .arg(drainOpenSslErrors());
        qCWarning(lcApp) << "CertificateStore:" << m_lastError;
        return false;
    }

    if (!writePemFile(m_certPath, certPem, /*restrictToOwner=*/false)) {
        m_lastError = QStringLiteral("Could not write certificate to %1")
                          .arg(m_certPath);
        qCWarning(lcApp) << "CertificateStore:" << m_lastError;
        return false;
    }
    if (!writePemFile(m_keyPath, keyPem, /*restrictToOwner=*/true)) {
        m_lastError = QStringLiteral("Could not write private key to %1")
                          .arg(m_keyPath);
        qCWarning(lcApp) << "CertificateStore:" << m_lastError;
        return false;
    }

    const QString fingerprint = fingerprintOf(cert.get());
    if (fingerprint.isEmpty()) {
        m_lastError = QStringLiteral("Fingerprint computation failed: %1")
                          .arg(drainOpenSslErrors());
        qCWarning(lcApp) << "CertificateStore:" << m_lastError;
        return false;
    }

    m_certificate = QSslCertificate(certPem, QSsl::Pem);
    m_privateKey  = QSslKey(keyPem, kKeyAlgorithm, QSsl::Pem, QSsl::PrivateKey);
    if (m_certificate.isNull() || m_privateKey.isNull()) {
        m_lastError = QStringLiteral(
            "Generated certificate/key did not parse back through Qt "
            "(QSslCertificate/QSslKey); see tlsBackendDiagnostic().");
        qCWarning(lcApp) << "CertificateStore:" << m_lastError;
        return false;
    }

    m_fingerprint = fingerprint;
    m_valid = true;
    m_lastError.clear();
    qCInfo(lcApp) << "CertificateStore: generated new TLS identity at" << m_directory;
    return true;
}

// static
QString CertificateStore::tlsBackendDiagnostic()
{
    if (QSslSocket::supportsSsl()) {
        return QString();
    }

    const QString active = QSslSocket::activeBackend();
    const QStringList available = QSslSocket::availableBackends();
    const QString activeDisplay = active.isEmpty() ? QStringLiteral("<none>") : active;
    const QString availableDisplay =
        available.isEmpty() ? QStringLiteral("<none>") : available.join(QStringLiteral(", "));

#ifdef Q_OS_WIN
    // task-17-brief.md Step 4 / task-17-controller-notes.md: Qt on
    // Windows can be built with Schannel as its only TLS backend, which
    // reports QSslSocket::supportsSsl() == false when it cannot complete
    // its own runtime capability check. Named explicitly so this reads
    // as an actionable diagnosis rather than "TLS unavailable".
    if (!available.contains(QStringLiteral("openssl"))) {
        return QStringLiteral(
                   "Qt reports no usable TLS backend (active: %1; available: "
                   "%2). This Qt build was not compiled with the \"openssl\" "
                   "TLS backend, which is the likely cause on Windows -- Qt's "
                   "Schannel backend alone does not satisfy nereusd's "
                   "self-signed-certificate TLS requirements. Rebuild or "
                   "reinstall Qt with the OpenSSL TLS backend enabled, or "
                   "ship OpenSSL's DLLs alongside nereusd if the \"openssl\" "
                   "backend is present but its libraries are not found.")
            .arg(activeDisplay, availableDisplay);
    }
#endif

    return QStringLiteral(
               "Qt reports no usable TLS backend (active: %1; available: "
               "%2). nereusd cannot serve wss:// until a working Qt TLS "
               "backend is available.")
        .arg(activeDisplay, availableDisplay);
}

} // namespace NereusSDR
