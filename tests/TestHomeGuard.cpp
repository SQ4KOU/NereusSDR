// no-port-check: NereusSDR-original test support.
//
// tests/TestHomeGuard.cpp
//
// Auto-linked into every test binary by nereus_add_test(). Before main(),
// and so before any test code can write a settings file, it works out where
// this process would keep its settings and ends the process at once when
// that place is not inside the test's own home.
//
// Why this exists: QStandardPaths test mode (TestSandboxInit.cpp) does not
// by itself keep a test out of the operator's settings. On macOS Qt takes
// the settings folder from NSHomeDirectory(), which follows
// CFFIXED_USER_HOME, and test mode only rewrites that folder when it starts
// with $HOME. A test started with HOME pointing at a test home and
// CFFIXED_USER_HOME unset therefore resolved the operator's real
// ~/Library/Preferences/NereusSDR, test mode on or not. Until 2026-10-11
// ctest started every NATIVE_WINDOW test that way: over a thousand test
// profile folders were written into the maintainer's real settings folder
// and his base settings file was overwritten.
//
// How it knows the test home: nereus_add_test() sets NEREUS_TEST_HOME to
// the same folder as HOME (and CFFIXED_USER_HOME on macOS). The guard
// refuses to run when
//   - NEREUS_TEST_HOME is not set (a test binary started by hand),
//   - NEREUS_TEST_HOME is the real user's home or a folder above it, or
//   - the settings folder Qt resolves is not inside NEREUS_TEST_HOME.
// A child process a test starts inherits all three variables and passes
// the same check. A test that moves its home on purpose (a short path for
// a local socket, say) moves NEREUS_TEST_HOME with it before it starts a
// child. A profile of the test's own (AppSettings::setProfileOverride)
// lives under the same settings folder and needs nothing more.
//
// Windows: Qt's test mode there takes the folder from the known-folder API,
// which no environment variable redirects, so every test shares one
// qttest folder beside the real one and there is no per-test home to check
// against. The guard does nothing on Windows.
//
// This file has no public symbols.

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>
#include <QtGlobal>

#if defined(Q_OS_UNIX)

#include <cstdio>
#include <cstdlib>

#include <pwd.h>
#include <unistd.h>

namespace {

// ctest fails the test on any value but zero; 97 only makes the refusal
// easy to tell from QtTest's own exit codes (the number of failed tests).
constexpr int kRefusedExitCode = 97;

// True when `path` is `dir` or lies below it. Both are cleaned first.
bool isInside(const QString& path, const QString& dir)
{
    const QString cleanPath = QDir::cleanPath(path);
    const QString cleanDir = QDir::cleanPath(dir);
    if (cleanDir.isEmpty()) {
        return false;
    }
    if (cleanPath == cleanDir) {
        return true;
    }
    const QString prefix = cleanDir.endsWith(QLatin1Char('/'))
                               ? cleanDir
                               : cleanDir + QLatin1Char('/');
    return cleanPath.startsWith(prefix);
}

// `path` with symbolic links resolved in the part of it that exists (the
// settings folder itself usually does not exist yet). /tmp is a link to
// /private/tmp on macOS, and a test home may be named either way.
QString resolved(const QString& path)
{
    QString existing = QDir::cleanPath(path);
    QString rest;
    while (!existing.isEmpty() && !QFileInfo::exists(existing)) {
        const int slash = existing.lastIndexOf(QLatin1Char('/'));
        if (slash <= 0) {
            return QDir::cleanPath(path);
        }
        rest.prepend(existing.mid(slash));
        existing.truncate(slash);
    }
    const QString canonical = QFileInfo(existing).canonicalFilePath();
    if (canonical.isEmpty()) {
        return QDir::cleanPath(path);
    }
    return canonical + rest;
}

[[noreturn]] void refuse(const char* reason, const QString& settingsDir,
                         const QByteArray& testHome)
{
    std::fprintf(stderr,
        "\n"
        "NereusSDR test refused to start: %s\n"
        "  settings folder it would use: %s\n"
        "  NEREUS_TEST_HOME:             %s\n"
        "Nothing was written. Run the test through ctest, which gives each\n"
        "test a home of its own:\n"
        "  ctest --test-dir build -R '^<test name>$'\n"
        "or by hand with all three variables on one scratch folder:\n"
        "  T=$(mktemp -d); NEREUS_TEST_HOME=\"$T\" HOME=\"$T\" "
        "CFFIXED_USER_HOME=\"$T\" ./build/tests/<test name>\n"
        "See tests/TestHomeGuard.cpp.\n"
        "\n",
        reason,
        qPrintable(settingsDir),
        testHome.isEmpty() ? "(not set)" : testHome.constData());
    std::fflush(stderr);
    std::_Exit(kRefusedExitCode);
}

struct TestHomeGuard {
    TestHomeGuard()
    {
        // The order of static constructors across files is not defined:
        // do not count on TestSandboxInit.cpp having run. Safe to repeat.
        QStandardPaths::setTestModeEnabled(true);

        // The same lookup AppSettings::resolveConfigDir() makes. It only
        // computes a path; it creates nothing.
        const QString settingsDir =
            QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QStringLiteral("/NereusSDR");

        const QByteArray testHomeRaw = qgetenv("NEREUS_TEST_HOME");
        if (testHomeRaw.isEmpty()) {
            refuse("NEREUS_TEST_HOME is not set, so this test has no home of "
                   "its own.", settingsDir, testHomeRaw);
        }
        const QString testHome = QFile::decodeName(testHomeRaw);
        if (!QDir::isAbsolutePath(testHome)) {
            refuse("NEREUS_TEST_HOME is not an absolute path.",
                   settingsDir, testHomeRaw);
        }

        // The account's home from the password database, which no
        // environment variable changes.
        const struct passwd* account = getpwuid(getuid());
        if (account != nullptr && account->pw_dir != nullptr
            && account->pw_dir[0] != '\0') {
            const QString realHome = QFile::decodeName(account->pw_dir);
            if (isInside(realHome, testHome)
                || isInside(resolved(realHome), resolved(testHome))) {
                refuse("NEREUS_TEST_HOME is the real user's home, or a "
                       "folder above it.", settingsDir, testHomeRaw);
            }
        }

        if (!isInside(settingsDir, testHome)
            && !isInside(resolved(settingsDir), resolved(testHome))) {
            refuse("its settings would be kept outside its test home "
                   "(on macOS the settings folder follows CFFIXED_USER_HOME, "
                   "not HOME).", settingsDir, testHomeRaw);
        }
    }
};

// Global static constructor runs before main(). QStandardPaths needs no
// QCoreApplication for this lookup.
static TestHomeGuard g_nereusTestHomeGuard;

} // namespace

#endif // Q_OS_UNIX
