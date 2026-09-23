// =================================================================
// src/core/daemon/HostTelemetrySampler.h  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  Reads the Core computer's CPU, memory
// and thermal counters from Linux procfs/sysfs for observational telemetry
// (R-R3-32, R-R3-33); no upstream logic is involved.
// =================================================================

#pragma once

#include "core/session/StationTelemetry.h"

#include <QByteArray>
#include <QString>
#include <QVector>

#include <optional>

namespace NereusSDR {

/// Samples host load for the Core's 1 Hz telemetry.
///
/// Every file is read relative to a root directory, which is "/" on Linux
/// and empty (sampling disabled) everywhere else. Tests inject a fixture
/// directory laid out like the real one (proc/stat, proc/meminfo,
/// proc/self/stat, proc/self/status, sys/class/thermal/thermal_zone*/).
///
/// sample() reads a handful of small files into a stack buffer and does not
/// allocate beyond the returned value; it never logs. It is meant for the
/// Core's event-loop thread, never a real-time audio or DSP thread. A missing
/// or unreadable file makes only the values it carries absent.
class HostTelemetrySampler final {
public:
    /// "/" on Linux; an empty string on macOS and Windows, where the host
    /// section is sent absent rather than faked.
    static QString defaultRootDirectory();

    explicit HostTelemetrySampler(const QString& rootDirectory = defaultRootDirectory());

    bool isEnabled() const noexcept { return m_enabled; }

    /// Forgets both CPU baselines, so the next sample reports no CPU
    /// percentages, and rediscovers the thermal zones.
    void reset();

    /// One observation. CPU percentages are the difference from the previous
    /// sample and are absent on the first sample, after any reading error
    /// and when a counter went backwards.
    StationHostTelemetry sample();

private:
    struct ThermalZone {
        QByteArray tempPath;
        QString type;
    };

    void discoverThermalZones();

    bool m_enabled = false;
    QString m_root;
    QByteArray m_procStatPath;
    QByteArray m_procSelfStatPath;
    QByteArray m_procMeminfoPath;
    QByteArray m_procSelfStatusPath;
    QVector<ThermalZone> m_zones;

    struct SystemBaseline {
        quint64 total = 0;
        quint64 idle = 0;
    };
    struct ProcessBaseline {
        quint64 processTicks = 0;
        quint64 systemTotal = 0;
    };
    std::optional<SystemBaseline> m_systemBaseline;
    std::optional<ProcessBaseline> m_processBaseline;
};

} // namespace NereusSDR
