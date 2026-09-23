// =================================================================
// src/core/platform/ThreadPlacement.h  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original. Chooses which processor cores the
// Core's signal processing threads run on, from the kernel's own CPU
// capacity data, and keeps every other thread off those cores (R-R3-41).
// Thetis has no equivalent; no upstream logic is involved.
// =================================================================
// Modification history (NereusSDR):
//   2026-09-23: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code (R-R3-41).
// =================================================================

#pragma once

#include <QList>
#include <QMap>
#include <QMutex>
#include <QString>
#include <QStringList>

#include <atomic>
#include <memory>

namespace NereusSDR {

/// Nice level for signal processing threads when the system permits it.
/// No real-time scheduling policy is used: a systemd service on a kernel
/// with real-time group scheduling is refused one, and a never-sleeping
/// real-time thread starves its core's kernel threads.
constexpr int kDspNice = -10;

/// What the kernel says about the computer's processor cores. Everything is
/// read once, before any thread is moved.
struct CpuTopology {
    QList<int> online;               ///< cpu/online, ascending; empty = unreadable
    QList<int> allowed;              ///< the process's cores at startup; empty = all online
    QMap<int, int> capacity;         ///< cpuN/cpu_capacity, where present and valid
    QList<QList<int>> clockDomains;  ///< cpufreq/policy*/related_cpus
    QList<QList<int>> coreSiblings;  ///< SMT groups (more than one CPU per core)
    QStringList problems;            ///< plain-words notes on missing or bad files
};

/// Reads the topology under `cpuRoot` (normally /sys/devices/system/cpu).
/// `allowed` is the process's affinity mask, read before any pinning.
CpuTopology readCpuTopology(const QString& cpuRoot, const QList<int>& allowed);

/// Parses a kernel CPU list ("0-3,6-7"). Sets *ok to false on anything else.
QList<int> parseCpuList(const QString& text, bool* ok);

/// Formats a CPU list the way the kernel writes one ("0-3, 6-7").
QString formatCpuList(const QList<int>& cpus);

/// The threads that get a core of their own, in the order they get one.
enum class ThreadRole {
    RxWorker,        ///< a receive channel's WDSP worker (wdspmain)
    DspThread,       ///< RxDspWorker's thread (fexchange2 for every slice)
    TxWorker,        ///< the transmit channel's WDSP worker
    TxWorkerThread,  ///< TxWorkerThread, the transmit audio pump
};

/// Which signal processing threads are busy.
struct PlacementDemand {
    QList<int> rxChannels;        ///< active receive channels
    bool dspThread{false};
    bool txWorker{false};         ///< transmit channel active
    bool txWorkerThread{false};   ///< transmit pump running while transmitting
};

struct RoleAssignment {
    ThreadRole role{ThreadRole::RxWorker};
    int channel{-1};
    int cpu{-1};                  ///< -1: runs with everything else
};

/// The result of planThreadPlacement().
struct PlacementPlan {
    bool active{false};           ///< false: leave every thread where it is
    QString reason;               ///< plain words: why inactive, or the method
    QList<int> signalPool;        ///< cores a thread may own, in the order given
    QList<int> housekeeping;      ///< cores for every other thread
    QList<RoleAssignment> assignments;

    /// The dedicated core of a role, or -1 when it runs on housekeeping.
    int cpuFor(ThreadRole role, int channel = -1) const;
    /// Every core given to a thread, ascending.
    QList<int> signalCores() const;
};

/// Pure placement rule.
///
/// When the cores differ in capacity, the fast tier is every core with at
/// least 90% of the largest capacity; fast cores are ordered by capacity,
/// filling one clock domain before the next. With at least two slower
/// cores, those run everything else.
///
/// Otherwise (equal or unknown capacity, or fewer than two slower cores)
/// the highest-numbered physical cores are reserved, never CPU0 or its
/// core, never two SMT siblings of one core, and at most N-2 of N physical
/// cores (N-1 with two or three, none with one); everything else runs on
/// the rest.
///
/// Dedicated cores go, in order, to the first active receive worker (lowest
/// channel), the DSP thread, the other active receive workers, the transmit
/// worker, then the transmit pump. A role left without a core runs on the
/// housekeeping cores.
PlacementPlan planThreadPlacement(const CpuTopology& topology,
                                  const PlacementDemand& demand);

/// The operating-system calls placement makes, behind an interface so tests
/// can record them.
class ThreadSchedulingApi {
public:
    virtual ~ThreadSchedulingApi() = default;
    virtual qint64 currentThreadId() = 0;
    virtual bool setAffinity(qint64 threadId, const QList<int>& cpus) = 0;
    virtual bool setNice(qint64 threadId, int nice) = 0;
};

/// The real calls on Linux (sched_setaffinity, setpriority per thread);
/// elsewhere every call does nothing and reports failure.
std::unique_ptr<ThreadSchedulingApi> makeSystemThreadSchedulingApi();

/// Keeps track of the Core's signal processing threads and moves them as
/// channels start and stop. Off (every call a no-op) until start(); the
/// GUI never starts it.
///
/// Calls happen when a thread starts or stops, or a channel starts or stops,
/// never per block. A mutex guards the registry.
class ThreadPlacement final {
public:
    ThreadPlacement();
    ~ThreadPlacement();
    ThreadPlacement(const ThreadPlacement&) = delete;
    ThreadPlacement& operator=(const ThreadPlacement&) = delete;

    /// The process-wide registry nereusd starts and WDSP reports to.
    static ThreadPlacement& instance();

    /// True on Linux once instance() is placing threads. Housekeeping
    /// threads then skip their own nice calls, and the DSP thread and the
    /// transmit pump register here instead of asking for real-time policy.
    static bool managesThreadPriority();

    /// WDSP thread-start hook (WDSPSetThreadStartHook); forwards to
    /// instance().onWdspThreadStarted().
    static void wdspThreadStartHook(int kind, int channel);

    /// Starts placing threads. Moves the calling thread to the housekeeping
    /// cores, so threads it creates later start there. Returns the one
    /// startup line for the log. Does nothing (and returns the line saying
    /// why) when the plan for `startupDemand` is inactive.
    QString start(const CpuTopology& topology, const PlacementDemand& startupDemand,
                  std::unique_ptr<ThreadSchedulingApi> api, bool raisePriorityPermitted);

    bool isActive() const noexcept { return m_active.load(std::memory_order_acquire); }

    /// Called on the thread itself as it starts.
    void registerCurrentThread(ThreadRole role, int channel = -1);
    /// Called on the thread itself before it ends.
    void deregisterCurrentThread();

    /// A channel started or stopped (RxWorker or TxWorker). Kept whether or
    /// not its worker has registered yet.
    void setChannelActive(ThreadRole role, int channel, bool active);

    /// A WDSP channel closed: forget its threads (their IDs can be reused)
    /// and its active state.
    void forgetChannel(int channel);

    /// A WDSP thread started (kinds as in wdsp_api.h). Workers register;
    /// flush threads run with everything else.
    void onWdspThreadStarted(int kind, int channel);

    /// Moves the calling thread to the first signal processing core, for
    /// FFTW planning, which times its plans on the core it runs on.
    void placeCurrentThreadOnFastCore();

    /// The plan for the current demand (tests and diagnostics).
    PlacementPlan currentPlan() const;

private:
    struct Registered {
        qint64 threadId{0};
        ThreadRole role{ThreadRole::RxWorker};
        int channel{-1};
        QList<int> appliedCpus;
        int appliedNice{0};
        bool niceApplied{false};
    };

    PlacementDemand demandLocked() const;
    bool roleActiveLocked(ThreadRole role, int channel) const;
    void applyLocked();
    void applyOneLocked(Registered& thread, const PlacementPlan& plan);

    mutable QMutex m_mutex;
    std::atomic<bool> m_active{false};
    CpuTopology m_topology;
    PlacementPlan m_startupPlan;
    std::unique_ptr<ThreadSchedulingApi> m_api;
    bool m_raisePriority{false};
    QList<Registered> m_threads;
    QMap<int, bool> m_activeRx;   // channel -> active
    QMap<int, bool> m_activeTx;   // channel -> active
};

/// nereusd's startup call, on the main thread before any other thread
/// exists. `enabled` is nereusd.conf's thread_placement (auto = true).
/// On Linux reads /sys/devices/system/cpu and the process's affinity mask,
/// checks RLIMIT_NICE, starts instance() and logs the one startup line.
void startDaemonThreadPlacement(bool enabled, int sliceCount);

} // namespace NereusSDR
