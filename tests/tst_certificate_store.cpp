// =================================================================
// tests/tst_certificate_store.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test infrastructure. Remote Daemon R2,
// Task 17: TLS certificate provisioning for nereusd's wss:// listener.
//
// Design source: docs/architecture/2026-07-28-remote-daemon-architecture-
// design.md §10.5 "Encryption, and which phase owns it" -- "the daemon
// generates a self-signed certificate on first run and the client pins
// its fingerprint, displayed at pairing time alongside the token (§7.1)".
//
// Every slot below constructs its own CertificateStore against a fresh
// QTemporaryDir, never AppSettings::kDaemonProfileName's real directory --
// a test run must not leave (or read back) a TLS identity in the
// developer's actual daemon profile.
// =================================================================

#include <QtTest>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslSocket>
#include <QTemporaryDir>

#include "core/AppSettings.h"
#include "core/security/CertificateStore.h"

using namespace NereusSDR;

namespace {

// 32 colon-separated uppercase hex byte pairs -- a SHA-256 digest
// formatted the conventional X.509-fingerprint way.
const QRegularExpression kFingerprintPattern(
    QStringLiteral("^([0-9A-F]{2}:){31}[0-9A-F]{2}$"));

} // namespace

class TstCertificateStore : public QObject {
    Q_OBJECT
private slots:

    // First run: no cert/key on disk yet (the directory itself does not
    // even exist -- exercises the mkpath path), so the constructor must
    // generate both, write them to <dir>/, and hand back something
    // usable. "Assert both parse back through QSslCertificate and
    // QSslKey" (task-17-brief.md Step 1) is checked twice: once via the
    // store's own accessors, and once by independently re-reading the
    // on-disk PEM bytes and re-parsing them in this test -- proving the
    // on-disk artifact itself round-trips, not just whatever the store
    // happens to be holding in memory.
    void generatesOnFirstRun()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString dir = tmp.path() + QStringLiteral("/not_yet_created");

        CertificateStore store(dir);
        QVERIFY2(store.isValid(), qPrintable(store.lastError()));
        QVERIFY(store.lastError().isEmpty());

        QVERIFY(QFileInfo::exists(store.certificatePath()));
        QVERIFY(QFileInfo::exists(store.privateKeyPath()));

        const QSslCertificate cert = store.certificate();
        QVERIFY(!cert.isNull());
        QVERIFY(cert.isSelfSigned());

        const QSslKey key = store.privateKey();
        QVERIFY(!key.isNull());
        QCOMPARE(key.type(), QSsl::PrivateKey);
        QCOMPARE(key.algorithm(), CertificateStore::kKeyAlgorithm);

        QVERIFY(kFingerprintPattern.match(store.fingerprintSha256()).hasMatch());

        // Independent re-parse of the on-disk bytes, not the store's
        // cached objects.
        QFile certFile(store.certificatePath());
        QVERIFY(certFile.open(QIODevice::ReadOnly));
        const QSslCertificate reparsedCert(certFile.readAll(), QSsl::Pem);
        QVERIFY(!reparsedCert.isNull());
        QCOMPARE(reparsedCert.digest(QCryptographicHash::Sha256),
                 cert.digest(QCryptographicHash::Sha256));

        QFile keyFile(store.privateKeyPath());
        QVERIFY(keyFile.open(QIODevice::ReadOnly));
        const QSslKey reparsedKey(keyFile.readAll(), CertificateStore::kKeyAlgorithm,
                                   QSsl::Pem, QSsl::PrivateKey);
        QVERIFY(!reparsedKey.isNull());
    }

    // The assertion most likely to pass vacuously (task-17-controller-
    // notes.md): a freshly generated certificate also parses fine, so
    // merely checking the second store is "valid" proves nothing about
    // reuse. What actually proves it: RSA key generation is randomised,
    // so two INDEPENDENT generations can never produce the same
    // fingerprint or the same PEM bytes by chance. Comparing both across
    // two CertificateStore constructions against the same directory is
    // therefore real evidence the second construction loaded the first
    // construction's files rather than overwriting them.
    void reusesRatherThanRegeneratesOnSecondRun()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        CertificateStore first(tmp.path());
        QVERIFY2(first.isValid(), qPrintable(first.lastError()));
        const QString fingerprint1 = first.fingerprintSha256();
        QVERIFY(!fingerprint1.isEmpty());

        QFile certFile1(first.certificatePath());
        QVERIFY(certFile1.open(QIODevice::ReadOnly));
        const QByteArray certBytes1 = certFile1.readAll();
        QFile keyFile1(first.privateKeyPath());
        QVERIFY(keyFile1.open(QIODevice::ReadOnly));
        const QByteArray keyBytes1 = keyFile1.readAll();

        CertificateStore second(tmp.path());
        QVERIFY2(second.isValid(), qPrintable(second.lastError()));
        const QString fingerprint2 = second.fingerprintSha256();

        QCOMPARE(fingerprint2, fingerprint1);

        QFile certFile2(second.certificatePath());
        QVERIFY(certFile2.open(QIODevice::ReadOnly));
        QCOMPARE(certFile2.readAll(), certBytes1);
        QFile keyFile2(second.privateKeyPath());
        QVERIFY(keyFile2.open(QIODevice::ReadOnly));
        QCOMPARE(keyFile2.readAll(), keyBytes1);
    }

    // Corrupt (not merely missing) files on disk must not wedge the
    // daemon permanently: treat an unparseable pair the same as a first
    // run rather than leaving isValid() false forever.
    void regeneratesWhenStoredFilesAreCorrupt()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        CertificateStore first(tmp.path());
        QVERIFY2(first.isValid(), qPrintable(first.lastError()));
        const QString certPath = first.certificatePath();
        const QString keyPath  = first.privateKeyPath();

        {
            QFile certFile(certPath);
            QVERIFY(certFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
            certFile.write("not a certificate\n");
        }
        {
            QFile keyFile(keyPath);
            QVERIFY(keyFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
            keyFile.write("not a key\n");
        }

        CertificateStore recovered(tmp.path());
        QVERIFY2(recovered.isValid(), qPrintable(recovered.lastError()));
        QVERIFY(!recovered.certificate().isNull());
        QVERIFY(!recovered.privateKey().isNull());
        QVERIFY(kFingerprintPattern.match(recovered.fingerprintSha256()).hasMatch());
    }

    // task-17-brief.md Step 1: guard the file-permission assertion with
    // #ifdef Q_OS_UNIX and QSKIP on Windows, where QFile::permissions()
    // mirrors owner bits into group and other, making a 0600-style
    // assertion pass vacuously or fail spuriously depending on which way
    // it is written.
    void privateKeyFileIsOwnerOnlyOnUnix()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        CertificateStore store(tmp.path());
        QVERIFY2(store.isValid(), qPrintable(store.lastError()));

#ifdef Q_OS_UNIX
        const QFileDevice::Permissions perms =
            QFile(store.privateKeyPath()).permissions();
        QVERIFY(perms.testFlag(QFileDevice::ReadOwner));
        QVERIFY(perms.testFlag(QFileDevice::WriteOwner));
        const QFileDevice::Permissions forbidden =
            QFileDevice::ReadGroup  | QFileDevice::WriteGroup  | QFileDevice::ExeGroup |
            QFileDevice::ReadOther  | QFileDevice::WriteOther  | QFileDevice::ExeOther;
        QCOMPARE(perms & forbidden, QFileDevice::Permissions());
#else
        QSKIP("QFile::permissions() mirrors owner bits into group/other on "
              "Windows (task-17-brief.md Step 1); the on-disk ACL is set "
              "best-effort by CertificateStore but not meaningfully "
              "assertable here.");
#endif
    }

    // task-17-brief.md Step 4: handle QSslSocket::supportsSsl() == false
    // with a message naming the cause, rather than a generic "TLS
    // unavailable". This machine has a working backend either way, so
    // the meaningful assertion is the implication itself: empty
    // diagnostic exactly when Qt reports a working backend, non-empty
    // exactly when it does not -- true on any CI runner regardless of
    // which side of that it lands on.
    void tlsBackendDiagnosticMatchesSupportsSsl()
    {
        const QString diagnostic = CertificateStore::tlsBackendDiagnostic();
        QCOMPARE(diagnostic.isEmpty(), QSslSocket::supportsSsl());
        if (!QSslSocket::supportsSsl()) {
            QVERIFY(!diagnostic.isEmpty());
        }
    }

    // task-17-controller-notes.md: "resolve it through
    // AppSettings::resolveConfigDir rather than rebuilding the path by
    // hand". Pinned directly against the sanctioned resolver so a future
    // edit that starts hand-building an equivalent-looking path (and
    // silently drifts from it) fails here instead of only showing up as
    // a runtime surprise on a real daemon profile.
    void defaultDirectoryUsesAppSettingsResolver()
    {
        QCOMPARE(CertificateStore::defaultDirectory(),
                 AppSettings::resolveConfigDir(
                     QString::fromLatin1(AppSettings::kDaemonProfileName)));
    }
};

QTEST_MAIN(TstCertificateStore)
#include "tst_certificate_store.moc"
