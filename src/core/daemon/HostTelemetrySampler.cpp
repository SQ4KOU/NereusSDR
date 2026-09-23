// =================================================================
// src/core/daemon/HostTelemetrySampler.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  See HostTelemetrySampler.h.
// =================================================================

#include "core/daemon/HostTelemetrySampler.h"

#include <QDir>
#include <QFile>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <utility>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace NereusSDR {
namespace {

// Large enough for /proc/meminfo and /proc/self/status on the Rock (each
// well under 2 KiB) and for the aggregate first line of /proc/stat on any
// machine. A value past the end of the buffer reads as absent.
constexpr qsizetype kReadBufferBytes = 8192;
using ReadBuffer = std::array<char, kReadBufferBytes>;

// Below absolute zero is a driver error, not a measurement.
constexpr qint64 kMinimumMilliCelsius = -273150;

// The largest integer JSON carries exactly; a larger KiB count is not real.
constexpr quint64 kMaxExactJsonInteger = 9007199254740991ULL;

// Reads up to the buffer size from a small procfs/sysfs file. procfs reports
// a zero size, so read until end of file. Returns the byte count, or -1.
qsizetype readSmallFile(const QByteArray& path, ReadBuffer& buffer)
{
    if (path.isEmpty()) { return -1; }
#ifdef Q_OS_UNIX
    int fd = -1;
    do {
        fd = ::open(path.constData(), O_RDONLY | O_CLOEXEC);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) { return -1; }
    qsizetype length = 0;
    bool failed = false;
    while (length < kReadBufferBytes) {
        const ssize_t got = ::read(fd, buffer.data() + length,
                                   static_cast<size_t>(kReadBufferBytes - length));
        if (got < 0) {
            if (errno == EINTR) { continue; }
            failed = true;
            break;
        }
        if (got == 0) { break; }
        length += got;
    }
    ::close(fd);
    return failed ? -1 : length;
#else
    QFile file(QFile::decodeName(path));
    if (!file.open(QIODevice::ReadOnly)) { return -1; }
    const qint64 got = file.read(buffer.data(), kReadBufferBytes);
    return got < 0 ? -1 : static_cast<qsizetype>(got);
#endif
}

bool isSpace(char c) { return c == ' ' || c == '\t'; }

// Parses one unsigned decimal after optional blanks; advances p.
bool parseUnsigned(const char*& p, const char* end, quint64* value)
{
    while (p < end && isSpace(*p)) { ++p; }
    if (p == end || *p < '0' || *p > '9') { return false; }
    quint64 result = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        const quint64 digit = static_cast<quint64>(*p - '0');
        if (result > (std::numeric_limits<quint64>::max() - digit) / 10) { return false; }
        result = result * 10 + digit;
        ++p;
    }
    *value = result;
    return true;
}

bool parseSigned(const char*& p, const char* end, qint64* value)
{
    while (p < end && (isSpace(*p) || *p == '\n')) { ++p; }
    bool negative = false;
    if (p < end && *p == '-') { negative = true; ++p; }
    quint64 magnitude = 0;
    if (!parseUnsigned(p, end, &magnitude)
        || magnitude > static_cast<quint64>(std::numeric_limits<qint64>::max())) {
        return false;
    }
    if (p < end && *p != '\n' && !isSpace(*p)) { return false; }
    *value = negative ? -static_cast<qint64>(magnitude) : static_cast<qint64>(magnitude);
    return true;
}

// Finds "<key>" at the start of a line and parses the number after it.
bool keyedValue(const char* data, qsizetype length, const char* key, quint64* value)
{
    const qsizetype keyLength = static_cast<qsizetype>(std::strlen(key));
    const char* const end = data + length;
    const char* line = data;
    while (line < end) {
        const char* newline = static_cast<const char*>(
            std::memchr(line, '\n', static_cast<size_t>(end - line)));
        const char* const lineEnd = newline ? newline : end;
        if (lineEnd - line >= keyLength && std::memcmp(line, key, static_cast<size_t>(keyLength)) == 0) {
            const char* p = line + keyLength;
            return parseUnsigned(p, lineEnd, value);
        }
        line = newline ? newline + 1 : end;
    }
    return false;
}

struct SystemTimes {
    quint64 total = 0;
    quint64 idle = 0;
};

// The aggregate "cpu" line: user nice system idle iowait irq softirq steal
// guest guest_nice, in USER_HZ ticks. guest and guest_nice are already
// counted in user and nice, so only the first eight make up the total.
// Idle time is idle plus iowait.
bool readSystemTimes(const QByteArray& path, ReadBuffer& buffer, SystemTimes* times)
{
    const qsizetype length = readSmallFile(path, buffer);
    if (length < 4 || std::memcmp(buffer.data(), "cpu ", 4) != 0) { return false; }
    const char* p = buffer.data() + 4;
    const char* const end = buffer.data() + length;
    std::array<quint64, 8> fields{};
    int count = 0;
    while (count < static_cast<int>(fields.size()) && parseUnsigned(p, end, &fields[count])) {
        ++count;
    }
    if (count < 4) { return false; }
    quint64 total = 0;
    for (int i = 0; i < count; ++i) {
        if (total > std::numeric_limits<quint64>::max() - fields[i]) { return false; }
        total += fields[i];
    }
    times->total = total;
    times->idle = fields[3] + (count > 4 ? fields[4] : 0);
    return true;
}

// /proc/self/stat: the command name sits in parentheses and may itself hold
// spaces or parentheses, so fields are counted after the LAST ')'. The
// first field after it is field 3 (state); utime and stime are fields 14
// and 15.
bool readProcessTicks(const QByteArray& path, ReadBuffer& buffer, quint64* ticks)
{
    const qsizetype length = readSmallFile(path, buffer);
    if (length <= 0) { return false; }
    const char* const begin = buffer.data();
    const char* close = nullptr;
    for (const char* q = begin + length; q > begin; --q) {
        if (*(q - 1) == ')') { close = q - 1; break; }
    }
    if (!close) { return false; }
    const char* p = close + 1;
    const char* const end = begin + length;
    for (int field = 3; field < 14; ++field) {
        while (p < end && isSpace(*p)) { ++p; }
        if (p == end) { return false; }
        while (p < end && !isSpace(*p) && *p != '\n') { ++p; }
    }
    quint64 utime = 0;
    quint64 stime = 0;
    if (!parseUnsigned(p, end, &utime) || !parseUnsigned(p, end, &stime)
        || utime > std::numeric_limits<quint64>::max() - stime) {
        return false;
    }
    *ticks = utime + stime;
    return true;
}

std::optional<qint64> kibValue(const char* data, qsizetype length, const char* key)
{
    quint64 value = 0;
    if (!keyedValue(data, length, key, &value)
        || value > kMaxExactJsonInteger) {
        return std::nullopt;
    }
    return static_cast<qint64>(value);
}

double percent(quint64 part, quint64 whole)
{
    return std::clamp(static_cast<double>(part) * 100.0 / static_cast<double>(whole),
                      0.0, 100.0);
}

int zoneNumber(const QString& name)
{
    bool ok = false;
    const int number = name.mid(qsizetype(std::strlen("thermal_zone"))).toInt(&ok);
    return ok ? number : std::numeric_limits<int>::max();
}

} // namespace

QString HostTelemetrySampler::defaultRootDirectory()
{
#ifdef Q_OS_LINUX
    return QStringLiteral("/");
#else
    return {};
#endif
}

HostTelemetrySampler::HostTelemetrySampler(const QString& rootDirectory)
    : m_enabled(!rootDirectory.isEmpty())
    , m_root(rootDirectory)
{
    if (!m_enabled) { return; }
    const QDir root(m_root);
    m_procStatPath = QFile::encodeName(root.filePath(QStringLiteral("proc/stat")));
    m_procSelfStatPath = QFile::encodeName(root.filePath(QStringLiteral("proc/self/stat")));
    m_procMeminfoPath = QFile::encodeName(root.filePath(QStringLiteral("proc/meminfo")));
    m_procSelfStatusPath = QFile::encodeName(root.filePath(QStringLiteral("proc/self/status")));
    reset();
}

void HostTelemetrySampler::reset()
{
    m_systemBaseline.reset();
    m_processBaseline.reset();
    if (m_enabled) {
        discoverThermalZones();
    }
}

void HostTelemetrySampler::discoverThermalZones()
{
    m_zones.clear();
    const QDir thermal(QDir(m_root).filePath(QStringLiteral("sys/class/thermal")));
    QStringList names = thermal.entryList({QStringLiteral("thermal_zone*")},
                                          QDir::Dirs | QDir::NoDotAndDotDot);
    // Numeric order, so a tie between zones names the lowest-numbered one.
    std::sort(names.begin(), names.end(), [](const QString& a, const QString& b) {
        return zoneNumber(a) < zoneNumber(b);
    });
    ReadBuffer buffer;
    for (const QString& name : std::as_const(names)) {
        const QDir zone(thermal.filePath(name));
        ThermalZone entry;
        entry.tempPath = QFile::encodeName(zone.filePath(QStringLiteral("temp")));
        const qsizetype length = readSmallFile(
            QFile::encodeName(zone.filePath(QStringLiteral("type"))), buffer);
        if (length > 0) {
            entry.type = QString::fromUtf8(buffer.data(), length).trimmed()
                             .left(kMaxHostZoneNameLength);
        }
        m_zones.append(entry);
    }
}

StationHostTelemetry HostTelemetrySampler::sample()
{
    StationHostTelemetry host;
    if (!m_enabled) { return host; }
    ReadBuffer buffer;

    SystemTimes system;
    const bool systemRead = readSystemTimes(m_procStatPath, buffer, &system);
    quint64 processTicks = 0;
    const bool processRead = readProcessTicks(m_procSelfStatPath, buffer, &processTicks);

    if (systemRead && m_systemBaseline && system.total > m_systemBaseline->total
        && system.idle >= m_systemBaseline->idle) {
        const quint64 total = system.total - m_systemBaseline->total;
        const quint64 idle = system.idle - m_systemBaseline->idle;
        if (idle <= total) {
            host.systemCpuPercent = percent(total - idle, total);
        }
    }
    if (systemRead && processRead && m_processBaseline
        && system.total > m_processBaseline->systemTotal
        && processTicks >= m_processBaseline->processTicks) {
        host.processCpuPercent = percent(processTicks - m_processBaseline->processTicks,
                                         system.total - m_processBaseline->systemTotal);
    }
    // A reading error forgets the baseline; a counter that went backwards
    // restarts it from the new value. Either way nothing negative is sent.
    if (systemRead) {
        m_systemBaseline = SystemBaseline{system.total, system.idle};
    } else {
        m_systemBaseline.reset();
    }
    if (systemRead && processRead) {
        m_processBaseline = ProcessBaseline{processTicks, system.total};
    } else {
        m_processBaseline.reset();
    }

    qsizetype length = readSmallFile(m_procMeminfoPath, buffer);
    if (length > 0) {
        host.memoryTotalKiB = kibValue(buffer.data(), length, "MemTotal:");
        host.memoryAvailableKiB = kibValue(buffer.data(), length, "MemAvailable:");
    }
    length = readSmallFile(m_procSelfStatusPath, buffer);
    if (length > 0) {
        host.processResidentKiB = kibValue(buffer.data(), length, "VmRSS:");
    }

    // Values are millidegrees Celsius. Some zones refuse reads (a sensor
    // that is powered down returns an error); those are skipped.
    std::optional<qint64> hottest;
    const ThermalZone* hottestZone = nullptr;
    for (const ThermalZone& zone : std::as_const(m_zones)) {
        length = readSmallFile(zone.tempPath, buffer);
        if (length <= 0) { continue; }
        const char* p = buffer.data();
        qint64 milli = 0;
        if (!parseSigned(p, buffer.data() + length, &milli) || milli < kMinimumMilliCelsius) {
            continue;
        }
        if (!hottest || milli > *hottest) {
            hottest = milli;
            hottestZone = &zone;
        }
    }
    if (hottest) {
        host.hottestZoneCelsius = static_cast<double>(*hottest) / 1000.0;
        host.hottestZoneName = hottestZone->type;
    }
    return host;
}

SharedHostSampler::SharedHostSampler(std::unique_ptr<HostTelemetrySampler> sampler,
                                     MonotonicClock clock)
    : m_sampler(std::move(sampler))
    , m_clock(std::move(clock))
{
    if (!m_sampler) {
        m_sampler = std::make_unique<HostTelemetrySampler>();
    }
    m_ownClock.start();
    if (!m_clock) {
        m_clock = [this] { return m_ownClock.elapsed(); };
    }
}

StationHostTelemetry SharedHostSampler::reading()
{
    const qint64 nowMs = m_clock();
    if (!m_sampledAtMs || nowMs < *m_sampledAtMs
        || nowMs - *m_sampledAtMs >= kMinimumIntervalMs) {
        m_cached = m_sampler->sample();
        m_sampledAtMs = nowMs;
    }
    return m_cached;
}

} // namespace NereusSDR
