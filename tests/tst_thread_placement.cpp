// =================================================================
// tests/tst_thread_placement.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original. Fixture-driven tests for nereusd's
// signal processing thread placement (R-R3-41): the topology reader and the
// pure plan against sysfs trees laid out under a temporary root, the
// registry against a recording fake of the system calls, and a Linux-only
// smoke test of the real calls. Nothing reads the build machine's /sys.
// =================================================================

#include <QtTest>

#include "core/platform/ThreadPlacement.h"
#include "core/wdsp_api.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <memory>
#include <thread>

#ifdef Q_OS_LINUX
#include <cerrno>
#include <sched.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

using namespace NereusSDR;

namespace {

class SysfsFixture {
public:
    SysfsFixture() { Q_ASSERT(m_dir.isValid()); }

    QString root() const { return m_dir.path(); }

    void write(const QString& relative, const QByteArray& contents)
    {
        const QString path = m_dir.filePath(relative);
        QDir().mkpath(QFileInfo(path).path());
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(contents), contents.size());
    }

    void online(const QByteArray& list) { write(QStringLiteral("online"), list + '\n'); }

    void capacity(int cpu, int value)
    {
        write(QStringLiteral("cpu%1/cpu_capacity").arg(cpu), QByteArray::number(value) + '\n');
    }

    void policy(int number, const QByteArray& cpus)
    {
        write(QStringLiteral("cpufreq/policy%1/related_cpus").arg(number), cpus + '\n');
    }

    void siblings(int cpu, const QByteArray& cpus)
    {
        write(QStringLiteral("cpu%1/topology/core_cpus_list").arg(cpu), cpus + '\n');
    }

    CpuTopology read(const QList<int>& allowed = {}) const
    {
        return readCpuTopology(root(), allowed);
    }

private:
    QTemporaryDir m_dir;
};

// RK3588S (Rock 5C): four 405 cores (0-3), two 1024 (4-5), two 982 (6-7),
// clock domains 0-3, 4-5 and 6-7, as read on the Rock's 6.1 kernel.
void layOutRk3588s(SysfsFixture& f)
{
    f.online("0-7");
    for (int cpu = 0; cpu < 4; ++cpu) {
        f.capacity(cpu, 405);
    }
    f.capacity(4, 1024);
    f.capacity(5, 1024);
    f.capacity(6, 982);
    f.capacity(7, 982);
    // The kernel writes related_cpus space-separated.
    f.policy(0, "0 1 2 3");
    f.policy(4, "4 5");
    f.policy(6, "6 7");
}

PlacementDemand demand(QList<int> rx, bool dsp, bool tx = false, bool txThread = false)
{
    PlacementDemand d;
    d.rxChannels = std::move(rx);
    d.dspThread = dsp;
    d.txWorker = tx;
    d.txWorkerThread = txThread;
    return d;
}

struct Call {
    enum Kind { Affinity, Nice } kind;
    qint64 threadId;
    QList<int> cpus;
    int nice;
};

class RecordingApi final : public ThreadSchedulingApi {
public:
    explicit RecordingApi(QList<Call>* calls, qint64* current)
        : m_calls(calls), m_current(current) {}
    qint64 currentThreadId() override { return *m_current; }
    bool setAffinity(qint64 threadId, const QList<int>& cpus) override
    {
        m_calls->append({Call::Affinity, threadId, cpus, 0});
        return true;
    }
    bool setNice(qint64 threadId, int nice) override
    {
        m_calls->append({Call::Nice, threadId, {}, nice});
        return true;
    }

private:
    QList<Call>* m_calls;
    qint64* m_current;
};

// The last affinity and nice each thread was given.
QList<int> lastCpus(const QList<Call>& calls, qint64 tid)
{
    for (auto it = calls.crbegin(); it != calls.crend(); ++it) {
        if (it->kind == Call::Affinity && it->threadId == tid) {
            return it->cpus;
        }
    }
    return {};
}

int lastNice(const QList<Call>& calls, qint64 tid)
{
    for (auto it = calls.crbegin(); it != calls.crend(); ++it) {
        if (it->kind == Call::Nice && it->threadId == tid) {
            return it->nice;
        }
    }
    return 999;
}

int niceCalls(const QList<Call>& calls)
{
    int n = 0;
    for (const Call& c : calls) {
        n += c.kind == Call::Nice ? 1 : 0;
    }
    return n;
}

} // namespace

class TestThreadPlacement : public QObject {
    Q_OBJECT

private slots:
    // ---------------------------------------------------------- CPU lists

    void cpuListsParseAndFormat()
    {
        bool ok = false;
        QCOMPARE(parseCpuList(QStringLiteral("0-3,6-7"), &ok), (QList<int>{0, 1, 2, 3, 6, 7}));
        QVERIFY(ok);
        QCOMPARE(parseCpuList(QStringLiteral("5"), &ok), QList<int>{5});
        QVERIFY(ok);
        QCOMPARE(parseCpuList(QStringLiteral("4 5"), &ok), (QList<int>{4, 5}));
        QVERIFY(ok);
        for (const char* bad : {"", "abc", "3-1", "0-", "-2", "0,,1", "0-99999"}) {
            QVERIFY2(parseCpuList(QString::fromLatin1(bad), &ok).isEmpty(), bad);
            QVERIFY2(!ok, bad);
        }
        QCOMPARE(formatCpuList({4, 5, 6, 7}), QStringLiteral("4-7"));
        QCOMPARE(formatCpuList({7, 0, 2, 3}), QStringLiteral("0, 2-3, 7"));
    }

    // ---------------------------------------------------------- fixtures

    void rk3588sGivesTheBigCoresToSignalProcessing()
    {
        SysfsFixture f;
        layOutRk3588s(f);
        const CpuTopology topology = f.read();
        QCOMPARE(topology.online, (QList<int>{0, 1, 2, 3, 4, 5, 6, 7}));
        QCOMPARE(topology.clockDomains.size(), 3);
        QVERIFY(topology.problems.isEmpty());

        const PlacementPlan plan =
            planThreadPlacement(topology, demand({0, 1}, true, true));
        QVERIFY(plan.active);
        QCOMPARE(plan.cpuFor(ThreadRole::RxWorker, 0), 4);
        QCOMPARE(plan.cpuFor(ThreadRole::DspThread), 5);
        QCOMPARE(plan.cpuFor(ThreadRole::RxWorker, 1), 6);
        QCOMPARE(plan.cpuFor(ThreadRole::TxWorker, 5), 7);
        QCOMPARE(plan.housekeeping, (QList<int>{0, 1, 2, 3}));
        QCOMPARE(plan.signalPool, (QList<int>{4, 5, 6, 7}));

        // A fifth busy thread has no fast core left and runs on 0-3.
        const PlacementPlan full =
            planThreadPlacement(topology, demand({0, 1}, true, true, true));
        QCOMPARE(full.cpuFor(ThreadRole::TxWorkerThread), -1);
        QCOMPARE(full.housekeeping, (QList<int>{0, 1, 2, 3}));
    }

    void raspberryPiReservesTheTopTwoCores()
    {
        // Pi 4 and Pi 5: four equal cores, one clock domain.
        SysfsFixture f;
        f.online("0-3");
        for (int cpu = 0; cpu < 4; ++cpu) {
            f.capacity(cpu, 1024);
        }
        f.policy(0, "0 1 2 3");
        const PlacementPlan plan =
            planThreadPlacement(f.read(), demand({0}, true, true, true));
        QVERIFY(plan.active);
        QCOMPARE(plan.cpuFor(ThreadRole::RxWorker, 0), 3);
        QCOMPARE(plan.cpuFor(ThreadRole::DspThread), 2);
        QCOMPARE(plan.cpuFor(ThreadRole::TxWorker, 5), -1);
        QCOMPARE(plan.housekeeping, (QList<int>{0, 1}));
    }

    void x86WithoutCapacityUsesTheHighestCores()
    {
        SysfsFixture f;
        f.online("0-7");
        for (int cpu = 0; cpu < 8; ++cpu) {
            f.policy(cpu, QByteArray::number(cpu));
        }
        const CpuTopology topology = f.read();
        QVERIFY(topology.capacity.isEmpty());
        // Missing on every core is normal on x86, not a problem to report.
        QVERIFY(topology.problems.isEmpty());
        const PlacementPlan plan = planThreadPlacement(topology, demand({0}, true));
        QVERIFY(plan.active);
        QCOMPARE(plan.cpuFor(ThreadRole::RxWorker, 0), 7);
        QCOMPARE(plan.cpuFor(ThreadRole::DspThread), 6);
        QCOMPARE(plan.housekeeping, (QList<int>{0, 1, 2, 3, 4, 5}));
        QVERIFY(plan.reason.contains(QStringLiteral("unknown")));
    }

    void smtSiblingsAreNeverBothReserved()
    {
        // Eight logical CPUs on four cores: n and n+4 share a core.
        SysfsFixture f;
        f.online("0-7");
        for (int cpu = 0; cpu < 8; ++cpu) {
            f.siblings(cpu, QStringLiteral("%1,%2").arg(cpu % 4).arg(cpu % 4 + 4).toLatin1());
        }
        const CpuTopology topology = f.read();
        QCOMPARE(topology.coreSiblings.size(), 4);
        const PlacementPlan plan =
            planThreadPlacement(topology, demand({0, 1, 2}, true, true, true));
        QVERIFY(plan.active);
        const QList<int> reserved = plan.signalCores();
        QVERIFY(!reserved.isEmpty());
        QVERIFY(!reserved.contains(0));
        QVERIFY(!reserved.contains(4));  // CPU0's sibling
        for (int cpu : reserved) {
            QVERIFY2(!reserved.contains(cpu % 4 == cpu ? cpu + 4 : cpu - 4),
                     qPrintable(formatCpuList(reserved)));
        }
        // Four physical cores: at most two reserved.
        QCOMPARE(reserved, (QList<int>{6, 7}));
    }

    void twoCoresReserveOne()
    {
        SysfsFixture f;
        f.online("0-1");
        const PlacementPlan plan =
            planThreadPlacement(f.read(), demand({0}, true, true, true));
        QVERIFY(plan.active);
        QCOMPARE(plan.cpuFor(ThreadRole::RxWorker, 0), 1);
        QCOMPARE(plan.cpuFor(ThreadRole::DspThread), -1);
        QCOMPARE(plan.cpuFor(ThreadRole::TxWorker, 5), -1);
        QCOMPARE(plan.housekeeping, QList<int>{0});
    }

    void oneCoreDoesNothing()
    {
        SysfsFixture f;
        f.online("0");
        const PlacementPlan plan = planThreadPlacement(f.read(), demand({0}, true));
        QVERIFY(!plan.active);
        QVERIFY(plan.assignments.isEmpty());

        QList<Call> calls;
        qint64 current = 1;
        ThreadPlacement placement;
        const QString line = placement.start(f.read(), demand({0}, true),
                                             std::make_unique<RecordingApi>(&calls, &current),
                                             true);
        QCOMPARE(line, QStringLiteral("Thread placement: every thread may run on any core,"
                                      " because only one processor core is available."));
        QVERIFY(!placement.isActive());
        QVERIFY(calls.isEmpty());
    }

    void gapsInTheOnlineListAreRespected()
    {
        SysfsFixture f;
        f.online("0-3,6-7");
        const CpuTopology topology = f.read();
        QCOMPARE(topology.online, (QList<int>{0, 1, 2, 3, 6, 7}));
        const PlacementPlan plan = planThreadPlacement(topology, demand({0}, true));
        QCOMPARE(plan.cpuFor(ThreadRole::RxWorker, 0), 7);
        QCOMPARE(plan.cpuFor(ThreadRole::DspThread), 6);
        QCOMPARE(plan.housekeeping, (QList<int>{0, 1, 2, 3}));
    }

    void restrictedAllowedMaskIsRespected()
    {
        // systemd CPUAffinity=2-5 on the RK3588S: only 4-5 are fast.
        SysfsFixture f;
        layOutRk3588s(f);
        const PlacementPlan plan =
            planThreadPlacement(f.read({2, 3, 4, 5}), demand({0, 1}, true));
        QVERIFY(plan.active);
        QCOMPARE(plan.cpuFor(ThreadRole::RxWorker, 0), 4);
        QCOMPARE(plan.cpuFor(ThreadRole::DspThread), 5);
        QCOMPARE(plan.cpuFor(ThreadRole::RxWorker, 1), -1);
        QCOMPARE(plan.housekeeping, (QList<int>{2, 3}));

        // Uniform machine, mask of two cores: one reserved, never outside it.
        SysfsFixture g;
        g.online("0-7");
        const PlacementPlan narrow = planThreadPlacement(g.read({2, 3}), demand({0}, true));
        QCOMPARE(narrow.cpuFor(ThreadRole::RxWorker, 0), 3);
        QCOMPARE(narrow.housekeeping, QList<int>{2});
    }

    void missingOrGarbageFiles()
    {
        {
            SysfsFixture f;  // no online file at all
            const CpuTopology t = f.read();
            QVERIFY(t.online.isEmpty());
            QCOMPARE(t.problems.size(), 1);
            QVERIFY(!planThreadPlacement(t, demand({0}, true)).active);
        }
        {
            SysfsFixture f;
            f.online("zero to seven");
            const PlacementPlan plan = planThreadPlacement(f.read(), demand({0}, true));
            QVERIFY(!plan.active);
            QCOMPARE(plan.reason, QStringLiteral("the list of processor cores could not be read"));
        }
        {
            // Capacity on only some cores (one unreadable): uniform rules,
            // and the gap is reported.
            SysfsFixture f;
            layOutRk3588s(f);
            f.write(QStringLiteral("cpu6/cpu_capacity"), "garbage\n");
            const CpuTopology t = f.read();
            QVERIFY(!t.capacity.contains(6));
            QVERIFY(!t.problems.isEmpty());
            const PlacementPlan plan = planThreadPlacement(t, demand({0}, true));
            QVERIFY(plan.active);
            QCOMPARE(plan.cpuFor(ThreadRole::RxWorker, 0), 7);
            QCOMPARE(plan.cpuFor(ThreadRole::DspThread), 6);
        }
        {
            // Empty files read as absent.
            SysfsFixture f;
            f.online("0-3");
            f.write(QStringLiteral("cpu1/cpu_capacity"), "");
            f.write(QStringLiteral("cpufreq/policy0/related_cpus"), "");
            const CpuTopology t = f.read();
            QVERIFY(t.capacity.isEmpty());
            QVERIFY(t.clockDomains.isEmpty());
            QCOMPARE(planThreadPlacement(t, demand({0}, true)).cpuFor(ThreadRole::RxWorker, 0), 3);
        }
    }

    // ---------------------------------------------------------- startup line

    void rk3588sStartupLine()
    {
        SysfsFixture f;
        layOutRk3588s(f);
        // nereusd's startup demand with one slice (startDaemonThreadPlacement).
        const PlacementDemand startup = demand({0}, true, true, true);
        {
            QList<Call> calls;
            qint64 current = 1000;
            ThreadPlacement placement;
            const QString line = placement.start(
                f.read(), startup, std::make_unique<RecordingApi>(&calls, &current), true);
            QCOMPARE(line, QStringLiteral(
                "Thread placement: signal processing runs on cores 4-7, everything"
                " else on cores 0-3 (the fastest cores, from the kernel's core capacity"
                " data); signal processing priority raised."));
            QVERIFY(placement.isActive());
            // The starting (main) thread moves to the housekeeping cores.
            QCOMPARE(calls.size(), 1);
            QCOMPARE(calls.first().threadId, qint64(1000));
            QCOMPARE(calls.first().cpus, (QList<int>{0, 1, 2, 3}));
        }
        {
            QList<Call> calls;
            qint64 current = 1000;
            ThreadPlacement placement;
            const QString line = placement.start(
                f.read(), startup, std::make_unique<RecordingApi>(&calls, &current), false);
            QCOMPARE(line, QStringLiteral(
                "Thread placement: signal processing runs on cores 4-7, everything"
                " else on cores 0-3 (the fastest cores, from the kernel's core capacity"
                " data); raising signal processing priority is not permitted, so it"
                " runs at normal priority."));
        }
    }

    // ---------------------------------------------------------- registry

    void registryPromotesOnActivationAndReturnsOnStop()
    {
        SysfsFixture f;
        layOutRk3588s(f);
        QList<Call> calls;
        qint64 current = 1;
        ThreadPlacement placement;
        placement.start(f.read(), demand({0}, true, true, true),
                        std::make_unique<RecordingApi>(&calls, &current), true);
        const QList<int> hk{0, 1, 2, 3};

        // RX0's worker starts idle: housekeeping, normal priority.
        current = 101;
        placement.onWdspThreadStarted(kWdspThreadRxMain, 0);
        QCOMPARE(lastCpus(calls, 101), hk);
        QCOMPARE(lastNice(calls, 101), 0);

        // Order-independent: RX1 is activated before its worker registers.
        placement.setChannelActive(ThreadRole::RxWorker, 1, true);
        current = 102;
        placement.onWdspThreadStarted(kWdspThreadRxMain, 1);
        QCOMPARE(lastCpus(calls, 102), QList<int>{4});
        QCOMPARE(lastNice(calls, 102), kDspNice);

        // RX0 activates: it is the first receive worker, so it takes 4.
        placement.setChannelActive(ThreadRole::RxWorker, 0, true);
        QCOMPARE(lastCpus(calls, 101), QList<int>{4});
        QCOMPARE(lastNice(calls, 101), kDspNice);
        QCOMPARE(lastCpus(calls, 102), QList<int>{5});

        // The DSP thread takes the second slot; RX1 moves to 6.
        current = 200;
        placement.registerCurrentThread(ThreadRole::DspThread);
        QCOMPARE(lastCpus(calls, 200), QList<int>{5});
        QCOMPARE(lastNice(calls, 200), kDspNice);
        QCOMPARE(lastCpus(calls, 102), QList<int>{6});

        // Transmit worker and pump: the worker gets 7 while transmitting;
        // the pump, with no fast core left, runs on housekeeping, raised.
        current = 105;
        placement.onWdspThreadStarted(kWdspThreadTxMain, 5);
        QCOMPARE(lastCpus(calls, 105), hk);
        QCOMPARE(lastNice(calls, 105), 0);
        current = 300;
        placement.registerCurrentThread(ThreadRole::TxWorkerThread);
        QCOMPARE(lastNice(calls, 300), 0);
        placement.setChannelActive(ThreadRole::TxWorker, 5, true);
        QCOMPARE(lastCpus(calls, 105), QList<int>{7});
        QCOMPARE(lastNice(calls, 105), kDspNice);
        QCOMPARE(lastCpus(calls, 300), hk);
        QCOMPARE(lastNice(calls, 300), kDspNice);

        // RX0 stops: back to housekeeping at normal priority; the rest
        // move up.
        placement.setChannelActive(ThreadRole::RxWorker, 0, false);
        QCOMPARE(lastCpus(calls, 101), hk);
        QCOMPARE(lastNice(calls, 101), 0);
        QCOMPARE(lastCpus(calls, 102), QList<int>{4});
        QCOMPARE(lastCpus(calls, 200), QList<int>{5});
        QCOMPARE(lastCpus(calls, 105), QList<int>{6});
        QCOMPARE(lastCpus(calls, 300), QList<int>{7});

        // Transmit ends.
        placement.setChannelActive(ThreadRole::TxWorker, 5, false);
        QCOMPARE(lastCpus(calls, 105), hk);
        QCOMPARE(lastNice(calls, 105), 0);
        QCOMPARE(lastCpus(calls, 300), hk);
        QCOMPARE(lastNice(calls, 300), 0);

        // Channel 1 closes: its thread is forgotten, never touched again.
        placement.forgetChannel(1);
        const int before = calls.size();
        placement.setChannelActive(ThreadRole::RxWorker, 0, true);
        for (int i = before; i < calls.size(); ++i) {
            QVERIFY(calls.at(i).threadId != 102);
        }
        QCOMPARE(lastCpus(calls, 101), QList<int>{4});

        // A flush thread runs with everything else at normal priority.
        current = 400;
        placement.onWdspThreadStarted(kWdspThreadFlush, 0);
        QCOMPARE(lastCpus(calls, 400), hk);
        QCOMPARE(lastNice(calls, 400), 0);

        // FFTW planning runs on the first signal processing core.
        current = 500;
        placement.placeCurrentThreadOnFastCore();
        QCOMPARE(lastCpus(calls, 500), QList<int>{4});

        // The DSP thread ends.
        current = 200;
        placement.deregisterCurrentThread();
        QCOMPARE(placement.currentPlan().cpuFor(ThreadRole::DspThread), -1);
    }

    void aReplacedWorkerDropsTheOldThread()
    {
        // WDSP's own rebuilds (a rate change) start a new worker for the
        // same channel; the old thread's ID may be reused, so it is dropped.
        SysfsFixture f;
        layOutRk3588s(f);
        QList<Call> calls;
        qint64 current = 1;
        ThreadPlacement placement;
        placement.start(f.read(), demand({0}, true),
                        std::make_unique<RecordingApi>(&calls, &current), true);
        placement.setChannelActive(ThreadRole::RxWorker, 0, true);
        current = 101;
        placement.onWdspThreadStarted(kWdspThreadRxMain, 0);
        current = 111;
        placement.onWdspThreadStarted(kWdspThreadRxMain, 0);
        QCOMPARE(lastCpus(calls, 111), QList<int>{4});
        const int before = calls.size();
        placement.setChannelActive(ThreadRole::RxWorker, 0, false);
        for (int i = before; i < calls.size(); ++i) {
            QVERIFY(calls.at(i).threadId != 101);
        }
    }

    void priorityNotPermittedMakesNoNiceCalls()
    {
        SysfsFixture f;
        layOutRk3588s(f);
        QList<Call> calls;
        qint64 current = 1;
        ThreadPlacement placement;
        placement.start(f.read(), demand({0}, true),
                        std::make_unique<RecordingApi>(&calls, &current), false);
        placement.setChannelActive(ThreadRole::RxWorker, 0, true);
        current = 101;
        placement.onWdspThreadStarted(kWdspThreadRxMain, 0);
        current = 200;
        placement.registerCurrentThread(ThreadRole::DspThread);
        current = 400;
        placement.onWdspThreadStarted(kWdspThreadFlush, 0);
        QCOMPARE(lastCpus(calls, 101), QList<int>{4});
        QCOMPARE(lastCpus(calls, 200), QList<int>{5});
        QCOMPARE(niceCalls(calls), 0);
    }

    void anInactiveRegistryDoesNothing()
    {
        // The GUI never starts placement: every entry point is a no-op.
        ThreadPlacement placement;
        QVERIFY(!placement.isActive());
        placement.registerCurrentThread(ThreadRole::DspThread);
        placement.setChannelActive(ThreadRole::RxWorker, 0, true);
        placement.onWdspThreadStarted(kWdspThreadRxMain, 0);
        placement.placeCurrentThreadOnFastCore();
        placement.forgetChannel(0);
        placement.deregisterCurrentThread();
        QVERIFY(!placement.isActive());
        QVERIFY(!ThreadPlacement::managesThreadPriority());
    }

    // ---------------------------------------------------------- Linux smoke

    void linuxSystemCallsMoveAndRenice()
    {
#ifndef Q_OS_LINUX
        QSKIP("Thread placement makes real system calls only on Linux.");
#else
        cpu_set_t original;
        CPU_ZERO(&original);
        QCOMPARE(sched_getaffinity(0, sizeof(original), &original), 0);
        QList<int> allowed;
        for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
            if (CPU_ISSET(cpu, &original)) {
                allowed.append(cpu);
            }
        }
        QVERIFY(!allowed.isEmpty());

        // A helper thread, so the test thread keeps its mask and nice.
        const std::unique_ptr<ThreadSchedulingApi> api = makeSystemThreadSchedulingApi();
        bool moved = false;
        bool readBack = false;
        bool reniced = false;
        int niceRead = 0;
        std::thread helper([&]() {
            const qint64 tid = api->currentThreadId();
            moved = api->setAffinity(tid, {allowed.last()});
            cpu_set_t now;
            CPU_ZERO(&now);
            readBack = sched_getaffinity(0, sizeof(now), &now) == 0
                && CPU_COUNT(&now) == 1 && CPU_ISSET(allowed.last(), &now);
            // Raising the nice value needs no privilege.
            reniced = api->setNice(tid, 5);
            errno = 0;
            niceRead = getpriority(PRIO_PROCESS, static_cast<id_t>(tid));
        });
        helper.join();
        QVERIFY(moved);
        QVERIFY(readBack);
        QVERIFY(reniced);
        QCOMPARE(niceRead, 5);
#endif
    }
};

QTEST_GUILESS_MAIN(TestThreadPlacement)
#include "tst_thread_placement.moc"
