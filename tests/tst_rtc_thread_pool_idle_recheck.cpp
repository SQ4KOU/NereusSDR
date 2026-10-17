// no-port-check: NereusSDR-original.
// libdatachannel's worker pool takes a queued task even when no worker was
// woken for it. On 2026-10-11 a desktop window stayed frozen for hours with
// the certificate task of a new peer connection queued in that pool while
// all 18 of its workers slept; the unpatched pool looks at its queue only
// when a wake-up arrives (cmake/patches/libdatachannel-0006). These cases
// leave the queue the way a wake-up that reached no worker leaves it: a
// task queued and nobody told. No radio, Core, network or audio device is
// opened by this test.
// Modification history (NereusSDR):
//   2026-10-11: original implementation by J.J. Boyd (KG4VCF), with
//               AI-assisted implementation via Anthropic Claude Code.

#include <QtTest>

#include "rtc/common.hpp"
#include "impl/init.hpp"
#include "impl/internals.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <vector>

// The test seam: only the pool's own queue and mutex can queue a task
// without the notification that normally goes with it.
#define private public
#include "impl/threadpool.hpp"
#undef private

namespace {

using Pool = rtc::impl::ThreadPool;

constexpr int kWorkers = 4;
// Far longer than the patched pool's 100 ms look at the queue, and no time
// at all for the unpatched pool, which never looks again by itself.
constexpr int kTakenWithinMs = 5000;
constexpr int kSettleMs = 5000;

// True when no worker counts itself busy and the queue holds `queued`
// tasks. A worker counts itself out under the pool's mutex just before it
// sleeps and back in under the same mutex once it is awake.
bool allWorkersAsleep(Pool& pool, size_t queued)
{
    std::unique_lock lock(pool.mMutex);
    return pool.mBusyWorkers.load() == 0 && pool.mTasks.size() == queued;
}

// Holds every worker inside a task at the same moment, then lets them all
// go back to sleep with `queuedAfterwards` tasks left in the queue. The
// pool's count alone is not enough: a worker that has not started yet, and
// one that was notified and has not woken yet, both read as asleep, and
// either would take an unannounced task for the wrong reason. Once every
// worker is inside a task there is no such worker, and no notification is
// still on its way to one.
bool settleWorkers(Pool& pool, size_t queuedAfterwards)
{
    std::promise<void> release;
    const std::shared_future<void> gate = release.get_future().share();
    auto inside = std::make_shared<std::atomic<int>>(0);
    for (int i = 0; i < kWorkers; ++i) {
        pool.enqueue([inside, gate]() {
            inside->fetch_add(1);
            gate.wait();
        });
    }
    const bool everyWorkerInside = QTest::qWaitFor(
        [&]() { return inside->load() == kWorkers; }, kSettleMs);
    release.set_value();
    return everyWorkerInside
        && QTest::qWaitFor(
            [&]() { return allWorkersAsleep(pool, queuedAfterwards); }, kSettleMs);
}

// Queues a task the way a lost wake-up leaves it: in the queue, with no
// worker notified. False if a worker was awake to see it go in.
bool queueWithoutWaking(Pool& pool, size_t alreadyQueued, std::function<void()> task)
{
    std::unique_lock lock(pool.mMutex);
    if (pool.mBusyWorkers.load() != 0 || pool.mTasks.size() != alreadyQueued) {
        return false;
    }
    pool.mTasks.push({Pool::clock::now(), std::move(task)});
    return true;
}

} // namespace

class TstRtcThreadPoolIdleRecheck : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        Pool& pool = Pool::Instance();
        pool.spawn(kWorkers);
        QVERIFY(settleWorkers(pool, 0));
    }

    void cleanup()
    {
        Pool& pool = Pool::Instance();
        pool.join();
        pool.clear();
    }

    // The freeze: every worker asleep on an empty queue, then a task whose
    // wake-up reaches none of them.
    void takesATaskNoWorkerWasWokenFor()
    {
        Pool& pool = Pool::Instance();
        auto ran = std::make_shared<std::atomic<bool>>(false);
        QVERIFY(queueWithoutWaking(pool, 0, [ran]() { ran->store(true); }));
        QTRY_VERIFY_WITH_TIMEOUT(ran->load(), kTakenWithinMs);
    }

    // A timed task far in the future (the library queues handshake and
    // retirement timers this way) must not hold the unannounced task back
    // until its own time comes: every worker has seen the timed task and
    // sleeps until it is due.
    void takesItWhileALaterTimedTaskWaits()
    {
        Pool& pool = Pool::Instance();
        pool.schedule(std::chrono::minutes(10), []() {});
        QVERIFY(settleWorkers(pool, 1));
        auto ran = std::make_shared<std::atomic<bool>>(false);
        QVERIFY(queueWithoutWaking(pool, 1, [ran]() { ran->store(true); }));
        QTRY_VERIFY_WITH_TIMEOUT(ran->load(), kTakenWithinMs);
    }

    // The ordinary path is unchanged: a task queued the normal way runs,
    // and a timed one does not run early.
    void announcedAndTimedTasksKeepTheirOrder()
    {
        Pool& pool = Pool::Instance();
        auto order = std::make_shared<std::vector<int>>();
        auto guard = std::make_shared<std::mutex>();
        const auto note = [order, guard](int which) {
            const std::lock_guard<std::mutex> lock(*guard);
            order->push_back(which);
        };
        const Pool::clock::time_point due =
            Pool::clock::now() + std::chrono::milliseconds(400);
        auto ranAt = std::make_shared<std::atomic<qint64>>(0);
        pool.schedule(due, [note, ranAt]() {
            ranAt->store(Pool::clock::now().time_since_epoch().count());
            note(2);
        });
        pool.enqueue([note]() { note(1); });
        QTRY_VERIFY_WITH_TIMEOUT(ranAt->load() != 0, kTakenWithinMs);
        QVERIFY(ranAt->load() >= due.time_since_epoch().count());
        const std::lock_guard<std::mutex> lock(*guard);
        QVERIFY(*order == (std::vector<int>{1, 2}));
    }
};

QTEST_APPLESS_MAIN(TstRtcThreadPoolIdleRecheck)
#include "tst_rtc_thread_pool_idle_recheck.moc"
