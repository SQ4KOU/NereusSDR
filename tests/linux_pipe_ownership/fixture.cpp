// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 J.J. Boyd (KG4VCF)
// no-port-check: NereusSDR-original behavioral ownership fixture.
// 2026-10-14: J.J. Boyd, AI-assisted via OpenAI Codex.
#include "LinuxPipeBus.h"
#include "fixture_config.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QProcess>
#include <QTemporaryDir>

#include <atomic>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <memory>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace NereusSDR {
Q_LOGGING_CATEGORY(lcAudio, "nereus.fixture.audio")

// Standalone link support for IAudioBus's identity bookkeeping; this fixture
// intentionally does not link AudioEngine or the application library.
std::uint64_t IAudioBus::nextInstanceId() noexcept
{
    static std::atomic<std::uint64_t> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
}
} // namespace NereusSDR

namespace {
using NereusSDR::AudioFormat;
using NereusSDR::LinuxPipeBus;

bool expect(bool condition, const QString& message)
{
    if (!condition) {
        qCritical().noquote() << "FAIL:" << message;
    }
    return condition;
}

class OwnedFd {
public:
    explicit OwnedFd(int fd) : m_fd(fd) {}
    ~OwnedFd()
    {
        if (m_fd >= 0) {
            ::close(m_fd);
        }
    }
    OwnedFd(const OwnedFd&) = delete;
    OwnedFd& operator=(const OwnedFd&) = delete;
    int get() const { return m_fd; }

private:
    int m_fd;
};

bool withinRoot(const QString& path)
{
    const QString root = QFileInfo(QString::fromUtf8(kFixtureRoot)).canonicalFilePath();
    const QString parent = QFileInfo(path).dir().canonicalPath();
    return !root.isEmpty() && (parent == root || parent.startsWith(root + '/'));
}

bool validRoot()
{
    struct stat info {};
    return ::lstat(kFixtureRoot, &info) == 0 && S_ISDIR(info.st_mode)
        && info.st_uid == ::getuid() && (info.st_mode & 0077) == 0;
}

QJsonObject readState()
{
    QFile file(qEnvironmentVariable("NEREUS_PIPE_FIXTURE_STATE"));
    if (!file.open(QIODevice::ReadOnly)) {
        qFatal("Unable to read owned fixture state");
    }
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        qFatal("Invalid owned fixture state");
    }
    return doc.object();
}

bool writeState(const QJsonObject& state)
{
    QFile file(qEnvironmentVariable("NEREUS_PIPE_FIXTURE_STATE"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray bytes = QJsonDocument(state).toJson();
    return file.write(bytes) == bytes.size();
}

bool setControl(const QString& name, const QJsonValue& value)
{
    QJsonObject state = readState();
    state.insert(name, value);
    return writeState(state);
}

bool prepareProcessEvidence()
{
#ifdef Q_OS_MAC
    const QString proc = QString::fromUtf8(kFixtureProcRoot);
    if (!QDir().mkpath(proc + "/sys/kernel/random")
        || !QDir().mkpath(proc + "/self/ns")
        || !QDir().mkpath(proc + '/' + QString::number(::getpid()))) {
        return false;
    }
    QFile boot(proc + "/sys/kernel/random/boot_id");
    if (!boot.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || boot.write("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee\n") < 0) {
        return false;
    }
    QFile ns(proc + "/self/ns/pid");
    // Keep the namespace inode stable across fixture/worker processes.
    if (!ns.exists() && (!ns.open(QIODevice::WriteOnly) || ns.write("same namespace\n") < 0)) {
        return false;
    }
    QFile statFile(proc + '/' + QString::number(::getpid()) + "/stat");
    if (!statFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    // Linux /proc/PID/stat field 22 is starttime, following PID, comm, state
    // and 18 intervening fields. Include a space in comm to exercise parsing.
    QByteArray fields = QByteArray::number(::getpid()) + " (fixture worker) S";
    for (int field = 4; field <= 21; ++field) {
        fields += " 0";
    }
    fields += " 123 0\n";
    return statFile.write(fields) == fields.size();
#else
    return true;
#endif
}

bool forgetDeadProcessEvidence(qint64 pid)
{
#ifdef Q_OS_MAC
    return QFile::remove(QString::fromUtf8(kFixtureProcRoot) + '/' + QString::number(pid) + "/stat");
#else
    (void)pid;
    return true;
#endif
}

QJsonObject moduleAt(int index)
{
    const QJsonArray modules = readState().value("modules").toArray();
    for (const QJsonValue& value : modules) {
        const QJsonObject module = value.toObject();
        if (module.value("index").toInt(-1) == index) {
            return module;
        }
    }
    return {};
}

bool sameFile(const QString& path, const struct stat& before)
{
    struct stat after {};
    const QByteArray encoded = QFile::encodeName(path);
    return ::lstat(encoded.constData(), &after) == 0
        && after.st_dev == before.st_dev && after.st_ino == before.st_ino
        && S_ISFIFO(after.st_mode);
}

bool transfersTx(LinuxPipeBus& bus)
{
    // Independently supplied bytes cross the real FIFO and LinuxPipeBus::pull.
    const QByteArray payload("\x00\x00\x80\x3f\x00\x00\x00\x3f", 8);
    const OwnedFd writer(::open(bus.pipePath(), O_WRONLY | O_NONBLOCK));
    if (!expect(writer.get() >= 0, "TX FIFO accepts a writer after successful open")) {
        return false;
    }
    if (!expect(::write(writer.get(), payload.constData(), 8) == 8, "write real TX payload")) {
        return false;
    }
    char received[8] {};
    return expect(bus.pull(received, 8) == 8 && QByteArray(received, 8) == payload,
                  "real LinuxPipeBus returns the supplied FIFO bytes");
}

bool baseline()
{
    // Catches a fixture that cannot open/transport/close the real implementation.
    LinuxPipeBus bus(LinuxPipeBus::Role::TxInput);
    if (!expect(bus.open(AudioFormat{}), "baseline TX opens: " + bus.errorString())) {
        return false;
    }
    bool ok = expect(bus.isOpen(), "successful TX reports open");
    ok = transfersTx(bus) && ok;
    const QString path = QString::fromUtf8(bus.pipePath());
    const QJsonObject module = moduleAt(101);
    ok = expect(module.value("name") == "module-pipe-sink", "TX publishes a pipe sink") && ok;
    ok = expect(module.value("arguments").toArray().contains("sink_name=nereussdr-vax-tx"),
                "TX keeps the stable user-facing sink name") && ok;
    bus.close();
    ok = expect(!bus.isOpen(), "close clears open state") && ok;
    ok = expect(moduleAt(101).isEmpty(), "close unloads its module") && ok;
    return expect(!QFileInfo::exists(path), "close removes its owned FIFO") && ok;
}

bool unrelatedLiveRx(const QString& caseRoot)
{
    // The broad prefix cleanup is the production bug this test catches.
    // This pre-existing module has a different role and a held live FIFO reader.
    const QString unrelatedPath = caseRoot + "/unrelated-rx.pipe";
    const QByteArray encoded = QFile::encodeName(unrelatedPath);
    if (!expect(::mkfifo(encoded.constData(), 0600) == 0, "create unrelated private RX FIFO")) {
        return false;
    }
    const OwnedFd reader(::open(encoded.constData(), O_RDONLY | O_NONBLOCK));
    struct stat before {};
    if (!expect(reader.get() >= 0 && ::fstat(reader.get(), &before) == 0,
                "unrelated RX FIFO has a live reader")) {
        return false;
    }
    const QJsonObject unrelated{
        {"index", 37}, {"name", "module-pipe-source"},
        {"arguments", QJsonArray{"file=" + unrelatedPath, "source_name=nereussdr-vax-1",
                                 "format=float32le", "rate=48000", "channels=2"}}};
    QJsonObject state = readState();
    state.insert("modules", QJsonArray{unrelated});
    if (!expect(writeState(state), "seed unrelated module 37")) {
        return false;
    }
    LinuxPipeBus tx(LinuxPipeBus::Role::TxInput);
    if (!expect(tx.open(AudioFormat{}), "TX opens alongside unrelated RX: " + tx.errorString())) {
        return false;
    }
    bool ok = expect(moduleAt(37) == unrelated,
                     "opening TX preserves unrelated live RX module 37 and its identity");
    ok = expect(sameFile(unrelatedPath, before), "unrelated live RX FIFO identity survives TX open") && ok;
    ok = transfersTx(tx) && ok;
    tx.close();
    return expect(moduleAt(37) == unrelated, "closing TX preserves unrelated module 37") && ok;
}

bool sameRole(bool otherProcess)
{
    // Missing role admission lets a second open replace the first bus's FIFO.
    LinuxPipeBus first(LinuxPipeBus::Role::TxInput);
    if (!expect(first.open(AudioFormat{}), "first TX opens: " + first.errorString())) {
        return false;
    }
    struct stat before {};
    if (!expect(::lstat(first.pipePath(), &before) == 0, "capture first TX FIFO identity")) {
        return false;
    }
    const QJsonObject firstModule = moduleAt(101);
    bool refused = false;
    if (otherProcess) {
        QProcess contender;
        contender.start(QCoreApplication::applicationFilePath(), {"contender"});
        if (!contender.waitForFinished(5000)) {
            contender.kill();
            contender.waitForFinished(1000);
            return expect(false, "contender process finishes without hanging");
        }
        refused = contender.exitStatus() == QProcess::NormalExit && contender.exitCode() == 2;
        qInfo().noquote() << "contender:" << contender.readAllStandardOutput().trimmed()
                          << contender.readAllStandardError().trimmed();
    } else {
        LinuxPipeBus second(LinuxPipeBus::Role::TxInput);
        refused = !second.open(AudioFormat{});
        // Check before second's destructor: refusal must itself be harmless.
        bool ok = expect(refused, "second same-role object refuses open");
        ok = expect(sameFile(QString::fromUtf8(first.pipePath()), before),
                    "contending object preserves first FIFO inode") && ok;
        ok = expect(moduleAt(101) == firstModule && !firstModule.isEmpty(),
                    "contending object preserves first module identity") && ok;
        return transfersTx(first) && ok;
    }
    bool ok = expect(refused, "second process refuses the already-owned TX role");
    ok = expect(sameFile(QString::fromUtf8(first.pipePath()), before),
                "contending process preserves first FIFO inode") && ok;
    ok = expect(moduleAt(101) == firstModule && !firstModule.isEmpty(),
                "contending process preserves first module identity") && ok;
    return transfersTx(first) && ok;
}

bool unknownSameRole(const QString& caseRoot)
{
    const QString path = caseRoot + "/unknown-tx.pipe";
    const QByteArray encoded = QFile::encodeName(path);
    if (!expect(::mkfifo(encoded.constData(), 0600) == 0, "create unknown role FIFO")) {
        return false;
    }
    struct stat before {};
    if (::lstat(encoded.constData(), &before) != 0) {
        return false;
    }
    const QJsonObject unknown{{"index", 37}, {"name", "module-pipe-sink"},
        {"arguments", QJsonArray{"file=" + path, "sink_name=nereussdr-vax-tx",
                                 "format=float32le", "rate=48000", "channels=2"}}};
    if (!setControl("modules", QJsonArray{unknown})) {
        return false;
    }
    LinuxPipeBus bus(LinuxPipeBus::Role::TxInput);
    const bool opened = bus.open(AudioFormat{});
    bool ok = expect(!opened && !bus.isOpen(), "unknown same-role module refuses TX admission");
    bus.close();
    ok = expect(moduleAt(37) == unknown, "unknown same-role module remains untouched") && ok;
    return expect(sameFile(path, before), "unknown role FIFO remains untouched") && ok;
}

bool indexZero()
{
    if (!setControl("next_index", 0)) {
        return false;
    }
    LinuxPipeBus bus(LinuxPipeBus::Role::TxInput);
    if (!expect(bus.open(AudioFormat{}), "module index zero is a valid successful assignment: " + bus.errorString())) {
        return false;
    }
    bool ok = expect(!moduleAt(0).isEmpty(), "successful index-zero module exists");
    ok = transfersTx(bus) && ok;
    bus.close();
    return expect(moduleAt(0).isEmpty(), "index-zero module is unloaded on close") && ok;
}

bool failedLoad(const QString& result)
{
    if (!setControl("load_result", result)) {
        return false;
    }
    LinuxPipeBus bus(LinuxPipeBus::Role::TxInput);
    bool ok = expect(!bus.open(AudioFormat{}) && !bus.isOpen(), "unsuccessful/ambiguous load refuses an open bus");
    bus.close();
    ok = expect(readState().value("modules").toArray().isEmpty(),
                "failed/ambiguous load cleans up only its provably owned module") && ok;
    if (!setControl("load_result", "normal")) {
        return false;
    }
    LinuxPipeBus retry(LinuxPipeBus::Role::TxInput);
    const bool opened = retry.open(AudioFormat{});
    if (result == "failure") {
        // runPactl currently conflates command failure, timeout and an empty
        // successful reply. No observed module therefore cannot prove that
        // the single pending load will never arrive; replacement must refuse.
        return expect(!opened, "unresolved no-module command outcome retains role admission evidence") && ok;
    }
    return expect(opened, "role is safely reopenable after resolved failed load: " + retry.errorString()) && ok;
}

bool unloadFailure()
{
    LinuxPipeBus bus(LinuxPipeBus::Role::TxInput);
    if (!expect(bus.open(AudioFormat{}), "TX opens before simulated unload failure")) {
        return false;
    }
    const QString path = QString::fromUtf8(bus.pipePath());
    struct stat before {};
    if (::lstat(bus.pipePath(), &before) != 0 || !setControl("unload_failure", true)) {
        return false;
    }
    const QJsonObject module = moduleAt(101);
    bus.close();
    bool ok = expect(moduleAt(101) == module && !module.isEmpty(), "ambiguous/failed unload retains module identity");
    ok = expect(sameFile(path, before), "failed unload preserves FIFO evidence for later recovery") && ok;
    {
        LinuxPipeBus contender(LinuxPipeBus::Role::TxInput);
        ok = expect(!contender.open(AudioFormat{}), "unresolved unload refuses replacement while control still fails") && ok;
    }
    if (!setControl("unload_failure", false)) {
        return false;
    }
    LinuxPipeBus retry(LinuxPipeBus::Role::TxInput);
    if (!expect(retry.open(AudioFormat{}), "positively retired closed bus can recover after server control returns: " + retry.errorString())) {
        return false;
    }
    ok = expect(moduleAt(101).isEmpty(), "retired-generation retry unloads the prior exact module") && ok;
    return transfersTx(retry) && ok;
}

bool pathReplacement()
{
    LinuxPipeBus bus(LinuxPipeBus::Role::TxInput);
    if (!expect(bus.open(AudioFormat{}), "TX opens before pathname replacement")) {
        return false;
    }
    const QString path = QString::fromUtf8(bus.pipePath());
    const QJsonObject module = moduleAt(101);
    if (!withinRoot(path) || !QFile::remove(path)) {
        return false;
    }
    const QByteArray replacement("owned fixture replacement\n");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(replacement) != replacement.size()) {
        return false;
    }
    file.close();
    bus.close();
    bool ok = expect(moduleAt(101) == module && !module.isEmpty(),
                     "close leaves module untouched when its FIFO identity has changed");
    if (!file.open(QIODevice::ReadOnly)) {
        return expect(false, "close preserves another generation's replacement file") && ok;
    }
    return expect(file.readAll() == replacement, "replacement contents survive close") && ok;
}

bool crashRecover(bool resourcesAlreadyRemoved = false)
{
    QProcess owner;
    owner.start(QCoreApplication::applicationFilePath(), {"crash-owner"});
    if (!owner.waitForStarted(3000)) {
        return expect(false, "crash owner starts");
    }
    const qint64 pid = owner.processId();
    if (!owner.waitForFinished(5000)) {
        owner.kill();
        owner.waitForFinished(1000);
        return expect(false, "crash owner exits without cleanup");
    }
    if (!expect(owner.exitStatus() == QProcess::NormalExit && owner.exitCode() == 0,
                "child successfully opens before exiting without destruction: " + QString::fromUtf8(owner.readAllStandardError()))) {
        return false;
    }
    const QJsonObject stale = moduleAt(101);
    if (!expect(!stale.isEmpty(), "confirmed-dead child left its module")
        || !expect(forgetDeadProcessEvidence(pid), "remove only confirmed-dead child's fake proc stat")) {
        return false;
    }
    if (resourcesAlreadyRemoved) {
        // Model a crash after successful resource removal but before the
        // atomic receipt was cleared. The original unretired receipt remains.
        const QString path = stale.value("file").toString();
        const QString generation = QFileInfo(path).dir().absolutePath();
        if (!expect(withinRoot(path) && QFile::remove(path) && QDir().rmdir(generation)
                        && setControl("modules", QJsonArray{}),
                    "remove only dead child's already-journalled fixture resources")) {
            return false;
        }
    }
    LinuxPipeBus successor(LinuxPipeBus::Role::TxInput);
    if (!expect(successor.open(AudioFormat{}), "proven-dead owned generation can recover: " + successor.errorString())) {
        return false;
    }
    bool ok = expect(moduleAt(101).isEmpty(), "recovery removes dead owner's exact module");
    ok = expect(!moduleAt(102).isEmpty(), "successor publishes its own generation") && ok;
    return transfersTx(successor) && ok;
}

bool normalReopen()
{
    LinuxPipeBus bus(LinuxPipeBus::Role::TxInput);
    if (!expect(bus.open(AudioFormat{}), "initial normal open")) {
        return false;
    }
    const QString oldPath = QString::fromUtf8(bus.pipePath());
    bus.close();
    bool ok = expect(moduleAt(101).isEmpty() && !QFileInfo::exists(oldPath), "normal close cleans exact owned generation");
    if (!expect(bus.open(AudioFormat{}), "same object reopens after normal close: " + bus.errorString())) {
        return false;
    }
    return transfersTx(bus) && ok;
}

bool queryFailure(const QString& mode)
{
    if (!setControl("query_result", mode)) {
        return false;
    }
    LinuxPipeBus bus(LinuxPipeBus::Role::TxInput);
    bool ok = expect(!bus.open(AudioFormat{}), "uncertain module query refuses the role");
    bus.close();
    return expect(readState().value("modules").toArray().isEmpty(), "uncertain initial query loads no module") && ok;
}

bool epochRestart()
{
    LinuxPipeBus bus(LinuxPipeBus::Role::TxInput);
    if (!expect(bus.open(AudioFormat{}), "TX opens before fake server epoch changes")) {
        return false;
    }
    const QString path = QString::fromUtf8(bus.pipePath());
    struct stat before {};
    if (::lstat(bus.pipePath(), &before) != 0) {
        return false;
    }
    // An unrelated module occupies the same numeric index after a restart.
    const QJsonObject replacement{{"index", 101}, {"name", "module-null-sink"},
                                  {"arguments", QJsonArray{"sink_name=unrelated-after-restart"}}};
    if (!setControl("modules", QJsonArray{replacement}) || !setControl("epoch", 2)) {
        return false;
    }
    bus.close();
    bool ok = expect(moduleAt(101) == replacement, "old context cannot unload reused index after server restart");
    return expect(sameFile(path, before), "lost server epoch preserves local ownership evidence") && ok;
}

bool allRoles()
{
    const std::array<LinuxPipeBus::Role, 5> roles{
        LinuxPipeBus::Role::Vax1, LinuxPipeBus::Role::Vax2, LinuxPipeBus::Role::Vax3,
        LinuxPipeBus::Role::Vax4, LinuxPipeBus::Role::TxInput};
    const std::array<const char*, 5> names{
        "source_name=nereussdr-vax-1", "source_name=nereussdr-vax-2",
        "source_name=nereussdr-vax-3", "source_name=nereussdr-vax-4",
        "sink_name=nereussdr-vax-tx"};
    std::array<std::unique_ptr<LinuxPipeBus>, 5> buses;
    bool ok = true;
    for (std::size_t i = 0; i < roles.size(); ++i) {
        buses[i] = std::make_unique<LinuxPipeBus>(roles[i]);
        if (!expect(buses[i]->open(AudioFormat{}), "distinct role opens: " + buses[i]->errorString())) {
            return false;
        }
        const QJsonObject module = moduleAt(101 + static_cast<int>(i));
        ok = expect(module.value("arguments").toArray().contains(QString::fromUtf8(names[i])),
                    "independent role preserves its stable audio name") && ok;
        struct stat info {};
        ok = expect(::lstat(buses[i]->pipePath(), &info) == 0
                        && S_ISFIFO(info.st_mode) && (info.st_mode & 0777) == 0600,
                    "published FIFO is private to its user") && ok;
        if (i < 4) {
            const OwnedFd reader(::open(buses[i]->pipePath(), O_RDONLY | O_NONBLOCK));
            const QByteArray payload("\x00\x00\x00\x3f\x00\x00\x80\x3e", 8);
            char received[8] {};
            ok = expect(reader.get() >= 0 && buses[i]->push(payload.constData(), 8) == 8
                            && ::read(reader.get(), received, 8) == 8 && QByteArray(received, 8) == payload,
                        "RX push transports supplied bytes through the real private FIFO") && ok;
        }
    }
    const QJsonObject tx = moduleAt(105);
    buses[0]->close();
    ok = expect(moduleAt(101).isEmpty(), "closing RX1 unloads only its own module") && ok;
    ok = expect(moduleAt(105) == tx && !tx.isEmpty(), "closing RX1 preserves simultaneous TX identity") && ok;
    return transfersTx(*buses[4]) && ok;
}

bool replacedOnLoad()
{
    if (!setControl("replace_fifo_on_load", true)) {
        return false;
    }
    LinuxPipeBus bus(LinuxPipeBus::Role::TxInput);
    bool ok = expect(!bus.open(AudioFormat{}) && !bus.isOpen(), "non-FIFO replacement during load refuses bus open");
    const QJsonObject module = moduleAt(101);
    ok = expect(!module.isEmpty(), "ambiguous load leaves server module for explicit recovery") && ok;
    const QString path = module.value("file").toString();
    QFile file(path);
    if (!expect(withinRoot(path) && file.open(QIODevice::ReadOnly), "replacement survives failed-open cleanup")) {
        return false;
    }
    return expect(file.readAll() == "replacement owned by another generation",
                  "failed-open cleanup preserves replacement contents") && ok;
}

bool differentRoleProcess()
{
    LinuxPipeBus tx(LinuxPipeBus::Role::TxInput);
    if (!expect(tx.open(AudioFormat{}), "TX opens before another process opens RX1")) {
        return false;
    }
    struct stat before {};
    if (::lstat(tx.pipePath(), &before) != 0) {
        return false;
    }
    const QJsonObject module = moduleAt(101);
    QProcess other;
    other.start(QCoreApplication::applicationFilePath(), {"other-role"});
    if (!other.waitForFinished(5000)) {
        other.kill();
        other.waitForFinished(1000);
        return expect(false, "different-role worker finishes without hanging");
    }
    bool ok = expect(other.exitStatus() == QProcess::NormalExit && other.exitCode() == 0,
                     "different process can open independent RX1: " + QString::fromUtf8(other.readAllStandardError()));
    ok = expect(moduleAt(101) == module && !module.isEmpty(), "different-role process preserves live TX module") && ok;
    ok = expect(sameFile(QString::fromUtf8(tx.pipePath()), before), "different-role process preserves live TX FIFO") && ok;
    return transfersTx(tx) && ok;
}

bool pendingLateLoad()
{
    if (!setControl("load_result", "empty-no-module")) {
        return false;
    }
    LinuxPipeBus first(LinuxPipeBus::Role::TxInput);
    if (!expect(!first.open(AudioFormat{}), "ambiguous empty reply does not publish an open bus")) {
        return false;
    }
    const QString path = QString::fromUtf8(first.pipePath());
    const QByteArray encoded = QFile::encodeName(path);
    struct stat before {};
    if (!expect(withinRoot(path) && ::lstat(encoded.constData(), &before) == 0 && S_ISFIFO(before.st_mode),
                "pending load preserves its private generation FIFO")) {
        return false;
    }
    bool ok = true;
    {
        LinuxPipeBus contender(LinuxPipeBus::Role::TxInput);
        ok = expect(!contender.open(AudioFormat{}), "no observed exact module cannot clear a pending generation") && ok;
    }
    ok = expect(sameFile(path, before), "refused retry preserves pending FIFO identity") && ok;
    const QJsonObject late{{"index", 37}, {"name", "module-pipe-sink"},
        {"arguments", QJsonArray{"file=" + path, "sink_name=nereussdr-vax-tx",
                                 "format=float32le", "rate=48000", "channels=2"}}, {"file", path}};
    if (!setControl("modules", QJsonArray{late}) || !setControl("load_result", "normal")) {
        return false;
    }
    LinuxPipeBus successor(LinuxPipeBus::Role::TxInput);
    if (!expect(successor.open(AudioFormat{}), "late exact module resolves retired pending generation: " + successor.errorString())) {
        return false;
    }
    ok = expect(moduleAt(37).isEmpty(), "only the observed late generation is unloaded") && ok;
    ok = expect(!moduleAt(101).isEmpty(), "successor has its own module after late-load recovery") && ok;
    return transfersTx(successor) && ok;
}

bool missingExecutable()
{
    const QString executable = qEnvironmentVariable("PATH") + "/pactl";
    const QString hidden = executable + ".disabled";
    if (!expect(withinRoot(executable) && QFile::rename(executable, hidden), "hide only the private pactl executable")) {
        return false;
    }
    LinuxPipeBus first(LinuxPipeBus::Role::TxInput);
    const bool opened = first.open(AudioFormat{});
    const QString abandonedPath = QString::fromUtf8(first.pipePath());
    // Restore before any assertion can return, keeping the fixture's command
    // boundary usable even if the production behavior is wrong.
    if (!expect(QFile::rename(hidden, executable), "restore private pactl executable")) {
        return false;
    }
    bool ok = expect(!opened && !first.isOpen(), "missing pactl refuses bus open");
    ok = expect(readState().value("modules").toArray().isEmpty(), "never-started command created no server module") && ok;
    ok = expect(!QFileInfo::exists(abandonedPath), "never-started load clears its unused private FIFO intent") && ok;
    // A definite FailedToStart is distinguishable from a launched command's
    // ambiguous empty/nonzero response: it must not permanently reserve TX.
    LinuxPipeBus retry(LinuxPipeBus::Role::TxInput);
    if (!expect(retry.open(AudioFormat{}), "restoring pactl permits retry after definite FailedToStart: " + retry.errorString())) {
        return false;
    }
    return transfersTx(retry) && ok;
}

bool loadTimeoutNoModule()
{
    if (!setControl("load_result", "timeout-no-module")) {
        return false;
    }
    LinuxPipeBus first(LinuxPipeBus::Role::TxInput);
    bool ok = expect(!first.open(AudioFormat{}) && !first.isOpen(),
                     "real command timeout refuses TX publication");
    qint64 blockedPid = -1;
    QFile transcript(qEnvironmentVariable("NEREUS_PIPE_FIXTURE_LOG"));
    if (!transcript.open(QIODevice::ReadOnly)) {
        return false;
    }
    for (const QByteArray& line : transcript.readAll().split('\n')) {
        const QJsonObject entry = QJsonDocument::fromJson(line).object();
        if (entry.value("event") == "load-blocked") {
            blockedPid = entry.value("pid").toInteger(-1);
        }
    }
    ok = expect(blockedPid > 0, "owned command reached its blocking FIFO before timeout") && ok;
    if (blockedPid > 0) {
        int status = 0;
        errno = 0;
        const pid_t waited = ::waitpid(static_cast<pid_t>(blockedPid), &status, WNOHANG);
        ok = expect(waited == -1 && errno == ECHILD,
                    "QProcess terminated and reaped its own timed-out child") && ok;
    }
    ok = expect(readState().value("modules").toArray().isEmpty(), "timed-out command created no server module") && ok;
    const QString path = QString::fromUtf8(first.pipePath());
    struct stat before {};
    const QByteArray encoded = QFile::encodeName(path);
    if (!expect(withinRoot(path) && ::lstat(encoded.constData(), &before) == 0 && S_ISFIFO(before.st_mode),
                "ambiguous load timeout preserves private FIFO evidence")) {
        return false;
    }
    if (!setControl("load_result", "normal")) {
        return false;
    }
    LinuxPipeBus retry(LinuxPipeBus::Role::TxInput);
    ok = expect(!retry.open(AudioFormat{}), "no observed module cannot clear a launched load timeout") && ok;
    ok = expect(sameFile(path, before), "refused timeout retry preserves FIFO identity") && ok;
    return expect(readState().value("modules").toArray().isEmpty(), "refused timeout retry loads no replacement module") && ok;
}

bool unloadTimeout()
{
    LinuxPipeBus first(LinuxPipeBus::Role::TxInput);
    if (!expect(first.open(AudioFormat{}), "TX opens before real unload-operation deadline")) {
        return false;
    }
    const QString path = QString::fromUtf8(first.pipePath());
    struct stat before {};
    if (::lstat(first.pipePath(), &before) != 0 || !setControl("unload_result", "timeout")) {
        return false;
    }
    const QJsonObject original = moduleAt(101);
    first.close();
    bool ok = expect(!first.isOpen(), "unload timeout leaves the bus closed");
    ok = expect(moduleAt(101) == original && !original.isEmpty(), "timed-out unload preserves original module identity") && ok;
    ok = expect(sameFile(path, before), "timed-out unload preserves FIFO evidence") && ok;
    ok = expect(QFileInfo::exists(qEnvironmentVariable("XDG_RUNTIME_DIR") + "/nereussdr-vax/role-5.json"),
                "timed-out unload retains durable recovery receipt") && ok;
    if (!setControl("unload_result", "normal")) {
        return false;
    }
    LinuxPipeBus successor(LinuxPipeBus::Role::TxInput);
    if (!expect(successor.open(AudioFormat{}), "retired generation recovers when unload control returns: " + successor.errorString())) {
        return false;
    }
    ok = expect(moduleAt(101).isEmpty(), "recovery unloads the retired generation's exact module") && ok;
    ok = expect(!moduleAt(102).isEmpty(), "recovery publishes the successor module") && ok;
    return transfersTx(successor) && ok;
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (!validRoot() || app.arguments().size() != 2) {
        qCritical() << "Fixture requires its configure-created private root and one case name";
        return 90;
    }
    const QString testCase = app.arguments().at(1);
    if (testCase == "contender" || testCase == "crash-owner" || testCase == "other-role") {
        if (!withinRoot(qEnvironmentVariable("NEREUS_PIPE_FIXTURE_STATE"))
            || !withinRoot(qEnvironmentVariable("NEREUS_PIPE_FIXTURE_LOG"))
            || !withinRoot(qEnvironmentVariable("XDG_RUNTIME_DIR") + "/probe")
            || !prepareProcessEvidence()) {
            return 90;
        }
        LinuxPipeBus contender(testCase == "other-role" ? LinuxPipeBus::Role::Vax1 : LinuxPipeBus::Role::TxInput);
        const bool opened = contender.open(AudioFormat{});
        qInfo() << "contender opened=" << opened << contender.errorString();
        if (testCase == "crash-owner") {
            ::_exit(opened ? 0 : 1);
        }
        if (testCase == "other-role") {
            return opened ? 0 : 1;
        }
        return opened ? 0 : 2;
    }

    QTemporaryDir caseDir(QString::fromUtf8(kFixtureRoot) + "/case-XXXXXX");
    if (!caseDir.isValid()) {
        return 90;
    }
    const QString bin = caseDir.path() + "/bin";
    if (!QDir().mkdir(bin) || !QFile::copy(QString::fromUtf8(kPactlStub), bin + "/pactl")
        || !QFile::setPermissions(bin + "/pactl", QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) {
        return 90;
    }
    // PATH deliberately contains only the private bin. The script's shebang
    // uses CMake's absolute Python path, so missing stub cannot reach host pactl.
    qputenv("PATH", QFile::encodeName(bin));
    qputenv("NEREUS_PIPE_FIXTURE_STATE", QFile::encodeName(caseDir.path() + "/state.json"));
    qputenv("NEREUS_PIPE_FIXTURE_LOG", QFile::encodeName(caseDir.path() + "/commands.jsonl"));
    qputenv("XDG_RUNTIME_DIR", QFile::encodeName(caseDir.path()));
    if (!prepareProcessEvidence()
        || !writeState(QJsonObject{{"modules", QJsonArray{}}, {"next_index", 101}, {"epoch", 1}})) {
        return 90;
    }
    bool ok = false;
    if (testCase == "baseline") {
        ok = baseline();
    } else if (testCase == "unrelated-live-rx") {
        ok = unrelatedLiveRx(caseDir.path());
    } else if (testCase == "same-role-object") {
        ok = sameRole(false);
    } else if (testCase == "same-role-process") {
        ok = sameRole(true);
    } else if (testCase == "unknown-same-role") {
        ok = unknownSameRole(caseDir.path());
    } else if (testCase == "index-zero") {
        ok = indexZero();
    } else if (testCase == "load-failure" || testCase == "load-empty" || testCase == "load-malformed") {
        ok = failedLoad(testCase.mid(5));
    } else if (testCase == "unload-failure") {
        ok = unloadFailure();
    } else if (testCase == "path-replacement") {
        ok = pathReplacement();
    } else if (testCase == "crash-recover") {
        ok = crashRecover();
    } else if (testCase == "normal-reopen") {
        ok = normalReopen();
    } else if (testCase == "query-failure" || testCase == "query-incomplete" || testCase == "query-timeout") {
        ok = queryFailure(testCase.mid(6));
    } else if (testCase == "epoch-restart") {
        ok = epochRestart();
    } else if (testCase == "all-roles") {
        ok = allRoles();
    } else if (testCase == "replaced-on-load") {
        ok = replacedOnLoad();
    } else if (testCase == "different-role-process") {
        ok = differentRoleProcess();
    } else if (testCase == "pending-late-load") {
        ok = pendingLateLoad();
    } else if (testCase == "crash-after-generation-removal") {
        ok = crashRecover(true);
    } else if (testCase == "missing-executable") {
        ok = missingExecutable();
    } else if (testCase == "load-timeout-no-module") {
        ok = loadTimeoutNoModule();
    } else if (testCase == "unload-timeout") {
        ok = unloadTimeout();
    } else {
        return 90;
    }
    // Print the boundary transcript before QTemporaryDir removes only this
    // case's files. Production FIFO operations remain in the private build root.
    QFile log(qEnvironmentVariable("NEREUS_PIPE_FIXTURE_LOG"));
    if (log.open(QIODevice::ReadOnly)) {
        qInfo().noquote() << "private pactl transcript:\n" << log.readAll();
    }
    qInfo() << (ok ? "PASS" : "FAIL") << testCase;
    return ok ? 0 : 1;
}
