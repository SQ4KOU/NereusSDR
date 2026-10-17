// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 J.J. Boyd (KG4VCF)
// no-port-check: NereusSDR-original Linux VAX ownership transaction.
// Modification history (NereusSDR):
//   2026-10-14: J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
//
// The AetherSDR prefix sweep cannot distinguish another live process.
// This design instead admits a role before touching resources and journals
// a unique private generation. See docs/development/linux-vax-ownership.md.
#include "LinuxPipeOwnership.h"
#include "LinuxPipeControl.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QUuid>

#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace NereusSDR {
namespace {

constexpr qsizetype kMaxReceiptBytes = 16384;

QString number(std::uint64_t value) { return QString::number(value); }

bool privateDirectory(const struct stat& info)
{
    return S_ISDIR(info.st_mode) && info.st_uid == ::geteuid()
        && (info.st_mode & 0077) == 0;
}

bool sameInode(const struct stat& a, const struct stat& b)
{
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino;
}

QByteArray readSmall(const QByteArray& path)
{
    QFile file(QString::fromLocal8Bit(path));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray bytes = file.read(kMaxReceiptBytes + 1);
    return bytes.size() <= kMaxReceiptBytes ? bytes.trimmed() : QByteArray{};
}

QByteArray startTime(const QByteArray& stat)
{
    // proc_pid_stat(5): field22 is starttime; comm(field2) may contain ')'.
    // https://man7.org/linux/man-pages/man5/proc_pid_stat.5.html
    const qsizetype end = stat.lastIndexOf(')');
    const QList<QByteArray> fields = stat.mid(end + 1).simplified().split(' ');
    if (end < 0 || fields.size() <= 19) {
        return {};
    }
    bool valid = false;
    fields[19].toULongLong(&valid);
    return valid ? fields[19] : QByteArray{};
}

// PulseAudio modargs use whitespace-separated key=value tokens with quoting
// and backslash escapes (v17.0 src/pulsecore/modargs.c). Reject ambiguous keys.
bool arguments(const QString& input, QMap<QString, QString>& output)
{
    QString token;
    QChar quote;
    bool escaped = false;
    const auto append = [&]() {
        if (token.isEmpty()) {
            return true;
        }
        const qsizetype equals = token.indexOf('=');
        if (equals <= 0 || output.contains(token.left(equals))) {
            return false;
        }
        output.insert(token.left(equals), token.mid(equals + 1));
        token.clear();
        return true;
    };
    for (const QChar ch : input) {
        if (escaped) {
            token += ch;
            escaped = false;
        } else if (ch == '\\') {
            escaped = true;
        } else if (!quote.isNull()) {
            if (ch == quote) {
                quote = QChar{};
            } else {
                token += ch;
            }
        } else if (ch == '"' || ch == '\'') {
            quote = ch;
        } else if (ch.isSpace()) {
            if (!append()) {
                return false;
            }
        } else {
            token += ch;
        }
    }
    return !escaped && quote.isNull() && append();
}

} // namespace

struct LinuxPipeOwnership::Impl {
    explicit Impl(int selectedRole) : m_role(selectedRole) {}
    ~Impl()
    {
        // flock(2): independently opened descriptions conflict even in one
        // process. CLOEXEC prevents pactl children retaining this role claim.
        // Never unlink the lock inode: another claimant may already hold it.
        if (m_generationFd >= 0) { ::close(m_generationFd); }
        if (m_lockFd >= 0) { ::close(m_lockFd); }
        if (m_rootFd >= 0) { ::close(m_rootFd); }
    }

    bool fail(const QString& detail)
    {
        m_error = QStringLiteral("Linux VAX %1: %2").arg(m_role).arg(detail);
        return false;
    }

    QString logicalName() const
    {
        return m_role == 5 ? QStringLiteral("nereussdr-vax-tx")
                         : QStringLiteral("nereussdr-vax-%1").arg(m_role);
    }

    QString nameKey() const
    {
        return m_role == 5 ? QStringLiteral("sink_name") : QStringLiteral("source_name");
    }

    QString moduleType() const
    {
        return m_role == 5 ? QStringLiteral("module-pipe-sink")
                         : QStringLiteral("module-pipe-source");
    }

    bool save(const QJsonObject& value)
    {
        struct stat lock {};
        if (::fstat(m_lockFd, &lock) != 0) {
            return fail(QStringLiteral("cannot identify role claim"));
        }
        QJsonObject committed = value;
        committed.insert("LockDevice", number(lock.st_dev));
        committed.insert("LockInode", number(lock.st_ino));
        const QByteArray bytes = QJsonDocument(committed).toJson(QJsonDocument::Compact);
        const QByteArray temporary = m_lockName + '.'
            + QUuid::createUuid().toString(QUuid::Id128).toLatin1() + ".part";
        const int fd = ::openat(m_rootFd, temporary.constData(),
                                O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0) { return fail(QStringLiteral("cannot create receipt transaction")); }
        qsizetype done = 0;
        while (done < bytes.size()) {
            const ssize_t count = ::write(fd, bytes.constData() + done,
                                           static_cast<size_t>(bytes.size() - done));
            if (count < 0 && errno == EINTR) { continue; }
            if (count <= 0) { break; }
            done += count;
        }
        const bool written = done == bytes.size() && ::fsync(fd) == 0;
        ::close(fd);
        if (!written || ::renameat(m_rootFd, temporary.constData(), m_rootFd, m_receiptName.constData()) != 0) {
            ::unlinkat(m_rootFd, temporary.constData(), 0);
            return fail(QStringLiteral("cannot persist ownership receipt"));
        }
        if (::fsync(m_rootFd) != 0) {
            return fail(QStringLiteral("cannot commit ownership receipt"));
        }
        m_receipt = committed;
        return true;
    }

    bool clear()
    {
        if ((::unlinkat(m_rootFd, m_receiptName.constData(), 0) != 0 && errno != ENOENT)
            || ::fsync(m_rootFd) != 0) {
            return fail(QStringLiteral("cannot clear ownership receipt"));
        }
        m_receipt = {};
        m_ownsGeneration = false;
        return true;
    }

    bool openRoot()
    {
        // All cooperating processes of this uid must share the claim even
        // when a service and a GUI have different XDG_RUNTIME_DIR values.
        m_rootPath = QStringLiteral("/tmp/nereussdr-vax-%1").arg(::geteuid());
        if (!QDir::isAbsolutePath(m_rootPath)) {
            return fail(QStringLiteral("runtime directory must be absolute"));
        }
        // A final symlink is rejected by O_NOFOLLOW; subsequent operations use
        // the held verified directory fd, rather than following child paths.
        const QByteArray encoded = QFile::encodeName(m_rootPath);
        if (::mkdir(encoded.constData(), 0700) != 0 && errno != EEXIST) {
            return fail(QStringLiteral("cannot create private runtime directory"));
        }
        m_rootFd = ::open(encoded.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        struct stat info {};
        if (m_rootFd < 0 || ::fstat(m_rootFd, &info) != 0 || !privateDirectory(info)) {
            return fail(QStringLiteral("runtime directory ownership is unknown"));
        }
        m_lockName = QByteArray("role-") + QByteArray::number(m_role) + ".lock";
        m_receiptName = QByteArray("role-") + QByteArray::number(m_role) + ".json";
        m_lockFd = ::openat(m_rootFd, m_lockName.constData(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
        struct stat pathInfo {};
        if (m_lockFd < 0 || ::fstat(m_lockFd, &info) != 0 || !S_ISREG(info.st_mode)
            || info.st_uid != ::geteuid() || (info.st_mode & 0077) != 0 || info.st_nlink != 1
            || ::flock(m_lockFd, LOCK_EX | LOCK_NB) != 0
            || ::fstatat(m_rootFd, m_lockName.constData(), &pathInfo, AT_SYMLINK_NOFOLLOW) != 0
            || !sameInode(info, pathInfo)) {
            return fail(QStringLiteral("role is in use or its ownership is unknown"));
        }
        return true;
    }

    bool ownerIsDead()
    {
        if (m_receipt.value("Retired").toBool()) {
            return true; // Closed bus persisted retirement before releasing its claim.
        }
        const QByteArray boot = readSmall("/proc/sys/kernel/random/boot_id");
        struct stat ns {};
        if (boot.isEmpty() || ::stat("/proc/self/ns/pid", &ns) != 0) {
            return fail(QStringLiteral("cannot verify previous owner identity"));
        }
        if (m_receipt.value("Boot").toString().toLatin1() != boot) {
            return true;
        }
        if (m_receipt.value("NamespaceDevice").toString() != number(ns.st_dev)
            || m_receipt.value("NamespaceInode").toString() != number(ns.st_ino)) {
            return fail(QStringLiteral("previous owner's PID namespace is unknown"));
        }
        const QByteArray pid = m_receipt.value("Pid").toString().toLatin1();
        const QByteArray ownerStatPath = "/proc/" + pid + "/stat";
        const int fd = ::open(ownerStatPath.constData(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            return errno == ENOENT || errno == ESRCH
                ? true : fail(QStringLiteral("cannot inspect previous owner"));
        }
        char buffer[kMaxReceiptBytes + 1] {};
        const ssize_t count = ::read(fd, buffer, kMaxReceiptBytes + 1);
        ::close(fd);
        const QByteArray start = count > 0 && count <= kMaxReceiptBytes
            ? startTime(QByteArray(buffer, count)) : QByteArray{};
        if (start.isEmpty()) {
            return fail(QStringLiteral("cannot establish previous owner start time"));
        }
        if (start == m_receipt.value("Start").toString().toLatin1()) {
            return fail(QStringLiteral("previous owner is still alive"));
        }
        return true; // Same boot/namespace, but recorded PID has been reused.
    }

    bool readReceipt()
    {
        const int fd = ::openat(m_rootFd, m_receiptName.constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        if (fd < 0) {
            return errno == ENOENT ? true : fail(QStringLiteral("cannot open ownership receipt"));
        }
        struct stat info {};
        if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != ::geteuid()
            || (info.st_mode & 0077) != 0 || info.st_nlink != 1
            || info.st_size <= 0 || info.st_size > kMaxReceiptBytes) {
            ::close(fd);
            return fail(QStringLiteral("ownership receipt identity or size is invalid"));
        }
        QByteArray bytes(info.st_size, '\0');
        const ssize_t count = ::read(fd, bytes.data(), bytes.size());
        ::close(fd);
        if (count != bytes.size()) {
            return fail(QStringLiteral("cannot read ownership receipt"));
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
        m_receipt = document.object();
        struct stat lock {};
        if (::fstat(m_lockFd, &lock) != 0
            || m_receipt.value("LockDevice").toString() != number(lock.st_dev)
            || m_receipt.value("LockInode").toString() != number(lock.st_ino)) {
            return fail(QStringLiteral("receipt belongs to a different role claim"));
        }
        const QString generation = m_receipt.value("Generation").toString();
        const QRegularExpression token(QStringLiteral("^role-%1-[a-f0-9]{32}$").arg(m_role));
        const QRegularExpression digits(QStringLiteral("^[0-9]+$"));
        const QRegularExpression bootId(QStringLiteral(
            "^[a-fA-F0-9]{8}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{12}$"));
        if (parseError.error != QJsonParseError::NoError || !document.isObject()
            || m_receipt.value("Schema").toInt() != 1 || m_receipt.value("Role").toInt() != m_role
            || m_receipt.value("Uid").toString() != number(::geteuid())
            || !token.match(generation).hasMatch()
            || !bootId.match(m_receipt.value("Boot").toString()).hasMatch()) {
            return fail(QStringLiteral("unrecognised ownership receipt"));
        }
        for (const QString& key : {QStringLiteral("Pid"), QStringLiteral("Start"),
                QStringLiteral("NamespaceDevice"), QStringLiteral("NamespaceInode"),
                QStringLiteral("DirectoryDevice"), QStringLiteral("DirectoryInode")}) {
            if (!digits.match(m_receipt.value(key).toString()).hasMatch()) {
                return fail(QStringLiteral("incomplete ownership receipt"));
            }
        }
        if (m_receipt.value("Pid").toString() == "0") {
            return fail(QStringLiteral("invalid owner PID"));
        }
        m_generationName = generation.toLatin1();
        m_path = QFile::encodeName(m_rootPath) + '/' + m_generationName + "/audio.pipe";
        return true;
    }

    bool openGeneration()
    {
        m_generationFd = ::openat(m_rootFd, m_generationName.constData(),
                                O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        struct stat info {};
        if (m_generationFd < 0 || ::fstat(m_generationFd, &info) != 0 || !privateDirectory(info)
            || m_receipt.value("DirectoryDevice").toString() != number(info.st_dev)
            || m_receipt.value("DirectoryInode").toString() != number(info.st_ino)) {
            return fail(QStringLiteral("generation directory identity changed"));
        }
        return true;
    }

    bool exact(const LinuxPipeModule& module) const
    {
        QMap<QString, QString> args;
        return module.name == moduleType() && arguments(module.arguments, args)
            && args.value(nameKey()) == logicalName()
            && QFile::encodeName(args.value("file")) == m_path
            && args.value("format") == "float32le" && args.value("rate") == "48000"
            && args.value("channels") == "2";
    }

    bool ownedModule(const QList<LinuxPipeModule>& modules, LinuxPipeModule& selected, bool& found)
    {
        found = false;
        for (const LinuxPipeModule& module : modules) {
            if (exact(module)) {
                if (found) { return fail(QStringLiteral("duplicate generation modules")); }
                selected = module;
                found = true;
            } else if (module.arguments.contains(QString::fromLocal8Bit(m_path))) {
                return fail(QStringLiteral("generation module identity is ambiguous"));
            }
        }
        return true;
    }

    bool fifoMatches(bool allowAbsent)
    {
        struct stat held {};
        struct stat named {};
        const QByteArray encodedRoot = QFile::encodeName(m_rootPath);
        if (::fstat(m_rootFd, &held) != 0 || ::lstat(encodedRoot.constData(), &named) != 0
            || !sameInode(held, named)
            || ::fstat(m_generationFd, &held) != 0
            || ::fstatat(m_rootFd, m_generationName.constData(), &named, AT_SYMLINK_NOFOLLOW) != 0
            || !sameInode(held, named)) {
            return fail(QStringLiteral("generation path was replaced"));
        }
        struct stat info {};
        if (::fstatat(m_generationFd, "audio.pipe", &info, AT_SYMLINK_NOFOLLOW) != 0) {
            return allowAbsent && errno == ENOENT
                ? true : fail(QStringLiteral("cannot verify generation FIFO"));
        }
        if (!S_ISFIFO(info.st_mode) || info.st_uid != ::geteuid()) {
            return fail(QStringLiteral("generation FIFO was replaced"));
        }
        const QString inode = m_receipt.value("FifoInode").toString();
        if (!inode.isEmpty() && (inode != number(info.st_ino)
            || m_receipt.value("FifoDevice").toString() != number(info.st_dev))) {
            return fail(QStringLiteral("generation FIFO identity changed"));
        }
        // Before load returns, the intent binds the unique directory+path,
        // not an index which might never have reached this process.
        return true;
    }

    bool cleanupGeneration()
    {
        if (!m_ownsGeneration) { return true; }
        QList<LinuxPipeModule> modules;
        LinuxPipeModule module;
        bool found = false;
        if (!m_control.modules(modules)) { return fail(m_control.errorString()); }
        if (!ownedModule(modules, module, found) || !fifoMatches(true)) { return false; }
        if (!found && m_receipt.value("LoadPending").toBool()) {
            return fail(QStringLiteral("load outcome is unresolved; generation preserved"));
        }
        if (found) {
            // Observing this unique generation proves its single load took
            // effect. Persist that observation before any uncertain unload.
            if (m_receipt.value("LoadPending").toBool()) {
                m_receipt.insert("LoadPending", false);
                if (!save(m_receipt)) { return false; }
            }
            // Both calls use this retained context. A server restart fails
            // closed; cooperating Nereus owners cannot replace this role.
            // Pulse has no CAS against arbitrary administrator replacement.
            if (!m_control.unload(module.index) || !m_control.modules(modules)) {
                return fail(m_control.errorString());
            }
            if (!ownedModule(modules, module, found)) { return false; }
            if (found) { return fail(QStringLiteral("module unload is unconfirmed")); }
        }
        if (!fifoMatches(true)) { return false; }
        if (::unlinkat(m_generationFd, "audio.pipe", 0) != 0 && errno != ENOENT) {
            return fail(QStringLiteral("cannot remove owned FIFO"));
        }
        ::close(m_generationFd);
        m_generationFd = -1;
        if (::unlinkat(m_rootFd, m_generationName.constData(), AT_REMOVEDIR) != 0 && errno != ENOENT) {
            return fail(QStringLiteral("generation directory is not empty"));
        }
        return clear();
    }

    bool begin()
    {
        if (m_role < 1 || m_role > 5) { return fail(QStringLiteral("invalid role")); }
        if (!openRoot() || !readReceipt() || !m_control.connect()) {
            if (m_error.isEmpty()) { m_error = m_control.errorString(); }
            return false;
        }
        if (!m_receipt.isEmpty()) {
            if (!ownerIsDead()) { return false; }
            // A crash after removing the generation directory but before
            // clearing its receipt must not permanently reserve the role.
            struct stat info {};
            if (::fstatat(m_rootFd, m_generationName.constData(), &info, AT_SYMLINK_NOFOLLOW) != 0
                && errno == ENOENT) {
                QList<LinuxPipeModule> remaining;
                LinuxPipeModule selected;
                bool found = false;
                if (!m_control.modules(remaining)) { return fail(m_control.errorString()); }
                if (!ownedModule(remaining, selected, found) || found
                    || m_receipt.value("LoadPending").toBool()) {
                    return fail(QStringLiteral("missing generation still has a module"));
                }
                if (!clear()) { return false; }
            } else {
                if (!openGeneration()) { return false; }
                m_ownsGeneration = true;
                if (!cleanupGeneration()) { return false; }
            }
        }
        QList<LinuxPipeModule> modules;
        if (!m_control.modules(modules)) { return fail(m_control.errorString()); }
        for (const LinuxPipeModule& module : modules) {
            QMap<QString, QString> args;
            const bool parsed = arguments(module.arguments, args);
            if ((parsed && (args.value("source_name") == logicalName()
                         || args.value("sink_name") == logicalName()))
                || (!parsed && module.arguments.contains(logicalName()))) {
                return fail(QStringLiteral("role belongs to an unknown generation"));
            }
        }
        const QByteArray boot = readSmall("/proc/sys/kernel/random/boot_id");
        const QByteArray pid = QByteArray::number(::getpid());
        const QByteArray start = startTime(readSmall("/proc/" + pid + "/stat"));
        struct stat ns {};
        if (boot.isEmpty() || start.isEmpty() || ::stat("/proc/self/ns/pid", &ns) != 0) {
            return fail(QStringLiteral("cannot record process identity"));
        }
        m_generationName = QByteArray("role-") + QByteArray::number(m_role) + '-'
            + QUuid::createUuid().toString(QUuid::Id128).toLatin1();
        if (::mkdirat(m_rootFd, m_generationName.constData(), 0700) != 0) {
            return fail(QStringLiteral("cannot create private generation"));
        }
        m_generationFd = ::openat(m_rootFd, m_generationName.constData(),
                                O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        struct stat dir {};
        if (m_generationFd < 0 || ::fstat(m_generationFd, &dir) != 0 || !privateDirectory(dir)) {
            return fail(QStringLiteral("cannot verify private generation"));
        }
        m_path = QFile::encodeName(m_rootPath) + '/' + m_generationName + "/audio.pipe";
        m_receipt = {{"Schema", 1}, {"Role", m_role}, {"Uid", number(::geteuid())},
            {"Generation", QString::fromLatin1(m_generationName)}, {"Boot", QString::fromLatin1(boot)},
            {"Pid", QString::fromLatin1(pid)}, {"Start", QString::fromLatin1(start)},
            {"NamespaceDevice", number(ns.st_dev)}, {"NamespaceInode", number(ns.st_ino)},
            {"DirectoryDevice", number(dir.st_dev)}, {"DirectoryInode", number(dir.st_ino)}};
        // Write-ahead intent, before load-module can create any server object.
        if (!save(m_receipt) || ::fsync(m_rootFd) != 0) {
            return fail(QStringLiteral("cannot commit generation intent"));
        }
        m_ownsGeneration = true;
        return true;
    }

    int m_role;
    int m_rootFd{-1};
    int m_lockFd{-1};
    int m_generationFd{-1};
    QByteArray m_lockName;
    QByteArray m_receiptName;
    QByteArray m_generationName;
    QByteArray m_path;
    QString m_rootPath;
    QString m_error;
    QJsonObject m_receipt;
    bool m_ownsGeneration{false};
    LinuxPipeControl m_control;
};

LinuxPipeOwnership::LinuxPipeOwnership(int role) : m_impl(std::make_unique<Impl>(role)) {}
LinuxPipeOwnership::~LinuxPipeOwnership() = default;
bool LinuxPipeOwnership::begin() { return m_impl->begin(); }
QByteArray LinuxPipeOwnership::pipePath() const { return m_impl->m_path; }
QString LinuxPipeOwnership::errorString() const { return m_impl->m_error; }
bool LinuxPipeOwnership::cleanup() { return m_impl->cleanupGeneration(); }

bool LinuxPipeOwnership::captureFifo()
{
    Impl& state = *m_impl;
    const int fd = ::openat(state.m_generationFd, "audio.pipe", O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
    struct stat info {};
    if (fd < 0) { return state.fail(QStringLiteral("module did not create its FIFO")); }
    const bool valid = ::fstat(fd, &info) == 0 && S_ISFIFO(info.st_mode)
        && info.st_uid == ::geteuid() && ::fchmod(fd, 0600) == 0;
    ::close(fd);
    if (!valid) { return state.fail(QStringLiteral("module FIFO ownership is unknown")); }
    state.m_receipt.insert("FifoDevice", number(info.st_dev));
    state.m_receipt.insert("FifoInode", number(info.st_ino));
    return state.save(state.m_receipt);
}

bool LinuxPipeOwnership::acceptFd(int fd)
{
    Impl& state = *m_impl;
    struct stat info {};
    if (::fstat(fd, &info) != 0 || !S_ISFIFO(info.st_mode)
        || info.st_uid != ::geteuid()
        || state.m_receipt.value("FifoDevice").toString() != number(info.st_dev)
        || state.m_receipt.value("FifoInode").toString() != number(info.st_ino)) {
        return state.fail(QStringLiteral("opened FIFO identity changed"));
    }
    return true;
}

bool LinuxPipeOwnership::confirmModule(std::uint32_t index)
{
    Impl& state = *m_impl;
    QList<LinuxPipeModule> modules;
    LinuxPipeModule selected;
    bool found = false;
    if (!state.m_control.modules(modules)) { return state.fail(state.m_control.errorString()); }
    if (!state.ownedModule(modules, selected, found) || !found || selected.index != index) {
        return state.fail(QStringLiteral("loaded module identity is unconfirmed"));
    }
    state.m_receipt.insert("LoadPending", false);
    return state.save(state.m_receipt);
}

} // namespace NereusSDR

namespace NereusSDR {
bool LinuxPipeOwnership::markLoadAttempt()
{
    m_impl->m_receipt.insert("LoadPending", true);
    return m_impl->save(m_impl->m_receipt);
}

bool LinuxPipeOwnership::cancelUnstartedLoad()
{
    m_impl->m_receipt.insert("LoadPending", false);
    return m_impl->save(m_impl->m_receipt);
}

bool LinuxPipeOwnership::retire()
{
    if (!m_impl->m_ownsGeneration) { return true; }
    m_impl->m_receipt.insert("Retired", true);
    return m_impl->save(m_impl->m_receipt);
}
} // namespace NereusSDR
