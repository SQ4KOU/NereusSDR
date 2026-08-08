// =================================================================
// src/core/security/TokenStore.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original. Remote Daemon R2, Task 18.
// See TokenStore.h for the design rationale.
// =================================================================
// Modification history (NereusSDR):
//   2026-08-08: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include "core/security/TokenStore.h"

#include "core/AppSettings.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QRandomGenerator>
#include <QSaveFile>

namespace NereusSDR {

namespace {
Q_LOGGING_CATEGORY(lcTokenStore, "nereus.tokenstore")

// 256 bits. Base64url with no padding renders this as 43 characters,
// which is one shell-safe, copy-pasteable word.
constexpr int kTokenBytes = 32;

// Longest token file this class will read. A token is 43 characters; the
// bound exists so a corrupt or hostile file cannot make the daemon
// allocate on a whim before it ever gets to validate anything.
constexpr qint64 kMaxTokenFileBytes = 4096;

// Byte-wise difference accumulator with no early exit. Both arguments are
// SHA-256 digests, so they are always the same length and the loop count
// carries no information about either input -- see TokenStore.h's note on
// why the raw strings are not compared directly.
bool constantTimeEqual(const QByteArray& a, const QByteArray& b)
{
    if (a.size() != b.size()) {
        return false;
    }
    quint8 diff = 0;
    for (int i = 0; i < a.size(); ++i) {
        diff = static_cast<quint8>(
            diff | (static_cast<quint8>(a[i]) ^ static_cast<quint8>(b[i])));
    }
    return diff == 0;
}

QByteArray digestOf(const QString& s)
{
    return QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha256);
}
} // namespace

TokenStore::TokenStore(const QString& directory)
    : m_directory(directory)
    , m_tokenPath(directory + QStringLiteral("/station-token"))
{
    if (!QDir().mkpath(m_directory)) {
        m_lastError = QStringLiteral("Could not create %1").arg(m_directory);
        return;
    }

    if (loadExisting()) {
        m_valid = true;
        return;
    }
    if (!m_lastError.isEmpty()) {
        // loadExisting() found a file it could not read. Deliberately NOT
        // regenerated over: overwriting a station's token would silently
        // lock out every already-paired client, which is worse than
        // refusing to come up authenticated. Same reasoning as
        // CertificateStore::LoadResult::IoFailure.
        return;
    }

    if (generateAndStore()) {
        m_valid = true;
        m_generatedThisRun = true;
    }
}

QString TokenStore::defaultDirectory()
{
    return AppSettings::resolveConfigDir(
        QString::fromLatin1(AppSettings::kDaemonProfileName));
}

bool TokenStore::loadExisting()
{
    QFile file(m_tokenPath);
    if (!file.exists()) {
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        m_lastError = QStringLiteral("Token file %1 exists but could not be opened: %2")
                          .arg(m_tokenPath, file.errorString());
        return false;
    }
    const QByteArray raw = file.read(kMaxTokenFileBytes);
    file.close();

    const QString candidate = QString::fromUtf8(raw).trimmed();
    if (candidate.isEmpty()) {
        // Empty or whitespace-only is treated as "not present", exactly
        // like a genuine first run: there is no secret here to protect,
        // so regenerating loses nothing.
        return false;
    }
    m_token = candidate;
    return true;
}

bool TokenStore::generateAndStore()
{
    // ::system() is the OS CSPRNG. QRandomGenerator::global() is seeded
    // from it but is a userspace PRNG whose state a long-lived process
    // exposes to anything that can read its memory; for a shared secret
    // the extra call cost is irrelevant and the distinction is not.
    //
    // Filled as quint32 words into a properly aligned local array rather
    // than through a reinterpret_cast over QByteArray::data(), which
    // would rest on an alignment guarantee QByteArray does not make.
    quint32 words[kTokenBytes / sizeof(quint32)]{};
    QRandomGenerator::system()->fillRange(words);
    const QByteArray raw(reinterpret_cast<const char*>(words), kTokenBytes);

    m_token = QString::fromLatin1(
        raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));

    QSaveFile file(m_tokenPath);
    if (!file.open(QIODevice::WriteOnly)) {
        m_lastError = QStringLiteral("Could not open %1 for writing: %2")
                          .arg(m_tokenPath, file.errorString());
        m_token.clear();
        return false;
    }
    // Permissions are set on the QSaveFile's own temp file BEFORE
    // commit(), never with a separate setPermissions() call afterwards:
    // commit() renames the temp file into place and rename() does not
    // alter permission bits, so doing it this way means no file ever
    // exists under the final name at the temp file's default (often
    // group-readable) permissions. Same reasoning, and same ordering, as
    // CertificateStore::writePemFile().
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        file.cancelWriting();
        m_lastError = QStringLiteral("Could not restrict permissions on %1").arg(m_tokenPath);
        m_token.clear();
        return false;
    }
    const QByteArray payload = m_token.toUtf8();
    if (file.write(payload) != payload.size() || !file.commit()) {
        m_lastError = QStringLiteral("Could not write %1: %2")
                          .arg(m_tokenPath, file.errorString());
        m_token.clear();
        return false;
    }
    return true;
}

void TokenStore::setRateLimit(int maxFailures, int lockoutMs)
{
    m_maxFailures = maxFailures < 1 ? 1 : maxFailures;
    m_lockoutMs = lockoutMs < 0 ? 0 : lockoutMs;
}

bool TokenStore::isRateLimited() const
{
    if (m_consecutiveFailures < m_maxFailures) {
        return false;
    }
    if (!m_sinceLastFailure.isValid()) {
        return false;
    }
    return m_sinceLastFailure.elapsed() < m_lockoutMs;
}

TokenStore::VerifyResult TokenStore::verify(const QString& candidate)
{
    if (m_consecutiveFailures >= m_maxFailures && m_sinceLastFailure.isValid()
        && m_sinceLastFailure.elapsed() >= m_lockoutMs) {
        // The lockout ran out. Start a fresh count rather than leaving the
        // counter pinned at the limit, which would make every later
        // failure re-trigger the lockout immediately.
        m_consecutiveFailures = 0;
    }

    if (isRateLimited()) {
        qCWarning(lcTokenStore)
            << "Authentication attempt refused: rate limited after"
            << m_consecutiveFailures << "consecutive failures";
        return VerifyResult::RateLimited;
    }

    // An unprovisioned store has no secret to compare against. Refuse
    // rather than accepting anything (including an empty candidate).
    const bool ok = m_valid && !m_token.isEmpty()
                    && constantTimeEqual(digestOf(m_token), digestOf(candidate));
    if (ok) {
        m_consecutiveFailures = 0;
        return VerifyResult::Accepted;
    }

    ++m_consecutiveFailures;
    m_sinceLastFailure.start();
    return VerifyResult::Rejected;
}

} // namespace NereusSDR
