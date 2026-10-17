// no-port-check: NereusSDR-original unit-test file. Thetis cite comments
// document upstream sources; no Thetis logic ported in this test file.
// =================================================================
// tests/tst_tx_channel_vox_listening.cpp  (NereusSDR)
// =================================================================
//
// Unit tests for TxChannel::setVoxListening() pump gate (Phase 3M-3a-iii
// Task 18 - bench fix).
//
// Source references (cited for traceability; logic lives in TxChannel.cpp):
//   wdsp/dexp.c:304 [v2.10.3.13]      - "DEXP code runs continuously so
//                                        it can be used to trigger VOX also."
//   audio.cs:168-192 [v2.10.3.13]     - Audio.VOXEnabled setter (no channel
//                                        state touch; only flips DEXP run_vox).
//   cmaster.cs:1039-1052 [v2.10.3.13] - cmaster.CMSetTXAVoxRun (sets DEXP
//                                        run_vox via SetDEXPRunVox).
//   TxChannel.cpp setVoxListening     - wraps the pump-gate state.
//
// Tests verify:
//   (a) Default vox-listening state is OFF (m_voxListening init false).
//   (b) setVoxListening(true) flips to ON; voxListeningForTest reads back true.
//   (c) setVoxListening(false) flips back to OFF; reads back false.
//   (d) Idempotent: setVoxListening(true)/setVoxListening(true) is a no-op.
//   (e) CRITICAL: setVoxListening operates independently of setRunning.
//       Both gates allow the pump independently; neither suppresses the
//       other.  The whole point of the bench fix.
//   (f) nereusd's thread placement is told the channel is listening while
//       it is up only for VOX, and transmitting only while keyed.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-04 - New test file for Phase 3M-3a-iii Task 18 (bench fix):
//                 verifies the VOX-listening pump gate flag round-trips
//                 correctly and is independent of m_running.  J.J. Boyd
//                 (KG4VCF), with AI-assisted implementation via Anthropic
//                 Claude Code.
//   2026-10-11 - (f): what the channel tells nereusd's thread placement
//                 on VOX on, key, unkey and VOX off.  J.J. Boyd (KG4VCF),
//                 with AI-assisted implementation via Anthropic Claude
//                 Code.
//   2026-10-15 - (f): each thread gets its own ID from the fake system
//                 calls, so a second registration cannot replace the test's
//                 DSP thread, and the test checks the DSP thread keeps its
//                 core.  J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//                 Claude Code.
//   2026-10-15 - (f): the test installs WDSP's thread start hook, so the
//                 transmit worker registers from its own thread, and checks
//                 the DSP thread keeps its core after that registration.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude
//                 Code.
// =================================================================

#define NEREUS_BUILD_TESTS 1

#include <QtTest/QtTest>

#include "core/TxChannel.h"
#include "core/WdspEngine.h"
#include "core/platform/ThreadPlacement.h"
#include "core/wdsp_api.h"

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

using namespace NereusSDR;

namespace {

// Accepts every move and priority change and makes none. Each thread has
// its own ID, so a registration from another thread (a WDSP worker) never
// replaces the test thread's. Counts the calls made from threads other than
// the one that created it, so the test can wait for the worker's.
class AcceptingApi final : public ThreadSchedulingApi {
public:
    explicit AcceptingApi(std::atomic<int>* otherThreadCalls)
        : m_otherThreadCalls(otherThreadCalls) {}
    qint64 currentThreadId() override
    {
        if (std::this_thread::get_id() != m_creator) {
            m_otherThreadCalls->fetch_add(1);
        }
        return static_cast<qint64>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    }
    bool setAffinity(qint64, const QList<int>&) override { return true; }
    bool setNice(qint64, int) override { return true; }

private:
    std::thread::id m_creator{std::this_thread::get_id()};
    std::atomic<int>* m_otherThreadCalls;
};

} // namespace

class TstTxChannelVoxListening : public QObject {
    Q_OBJECT

    // 3M-3a-iii ships exactly one TxChannel - channel id 1 matches
    // RadioModel::connectToRadio() at WdspEngine::createTxChannel
    // (channelId=1).  See TxChannel.cpp s_voxKeyInstance comment block
    // for the phase 3F multi-pan TX expansion plan.
    static constexpr int kChannelId = 1;

private slots:

    // ---------------------------------------------------------------
    // (a) Default vox-listening state is OFF.
    //     m_voxListening initialises to false in TxChannel.h header.
    // ---------------------------------------------------------------
    void default_voxListening_isFalse() {
        TxChannel ch(kChannelId);
        QCOMPARE(ch.voxListeningForTest(), false);
    }

    // ---------------------------------------------------------------
    // (b) setVoxListening(true) flips to ON.
    // ---------------------------------------------------------------
    void setVoxListening_true_roundTrip() {
        TxChannel ch(kChannelId);
        ch.setVoxListening(true);
        QCOMPARE(ch.voxListeningForTest(), true);
    }

    // ---------------------------------------------------------------
    // (c) setVoxListening(false) after true flips back to OFF.
    // ---------------------------------------------------------------
    void setVoxListening_false_roundTrip() {
        TxChannel ch(kChannelId);
        ch.setVoxListening(true);
        ch.setVoxListening(false);
        QCOMPARE(ch.voxListeningForTest(), false);
    }

    // ---------------------------------------------------------------
    // (d) Idempotent guard: setVoxListening(true) twice in a row is
    //     a no-op (return after the wasOn==on check in setVoxListening).
    // ---------------------------------------------------------------
    void setVoxListening_idempotent() {
        TxChannel ch(kChannelId);
        ch.setVoxListening(true);
        ch.setVoxListening(true);  // no-op
        QCOMPARE(ch.voxListeningForTest(), true);

        ch.setVoxListening(false);
        ch.setVoxListening(false); // no-op
        QCOMPARE(ch.voxListeningForTest(), false);
    }

    // ---------------------------------------------------------------
    // (e) CRITICAL — independence from m_running.
    //
    // The whole point of the bench fix: m_running and m_voxListening
    // are independent gates.  Either alone is sufficient to allow the
    // pump (verified at the gate site in driveOneTxBlockFromInter
    // leaved); neither suppresses the other.  Without this property
    // the chicken-and-egg between MOX and DEXP returns.
    //
    // Test sequence mirrors the runtime VOX path:
    //   1. Default: both off (idle).
    //   2. User clicks [VOX]:  setVoxListening(true)  - listening only.
    //                         m_running stays false.
    //   3. User speaks, DEXP fires, MoxController engages MOX:
    //                         setRunning(true)        - both on.
    //   4. User stops, hang timer expires, MOX drops:
    //                         setRunning(false)       - listening only.
    //                         m_voxListening stays true (still armed for VOX).
    //   5. User clicks [VOX] off:
    //                         setVoxListening(false) - both off.
    // ---------------------------------------------------------------
    void setVoxListening_independentOfRunning() {
        TxChannel ch(kChannelId);

        // Step 1 — both off (idle).
        QCOMPARE(ch.isRunning(),            false);
        QCOMPARE(ch.voxListeningForTest(),  false);

        // Step 2 — user clicks [VOX]: listening on, MOX still off.
        ch.setVoxListening(true);
        QCOMPARE(ch.isRunning(),            false);  // setVoxListening MUST NOT touch m_running
        QCOMPARE(ch.voxListeningForTest(),  true);

        // Step 3 — DEXP fires, MoxController engages MOX: both on.
        ch.setRunning(true);
        QCOMPARE(ch.isRunning(),            true);
        QCOMPARE(ch.voxListeningForTest(),  true);   // unaffected by setRunning

        // Step 4 — VOX hang timer expires, MOX drops back: listening still on.
        ch.setRunning(false);
        QCOMPARE(ch.isRunning(),            false);
        QCOMPARE(ch.voxListeningForTest(),  true);   // still listening after MOX drop

        // Step 5 — user clicks [VOX] off: both off again.
        ch.setVoxListening(false);
        QCOMPARE(ch.isRunning(),            false);
        QCOMPARE(ch.voxListeningForTest(),  false);
    }

    // ---------------------------------------------------------------
    // (f) nereusd's thread placement hears "listening" while the channel
    //     is up only for VOX and "transmitting" only while keyed.  Three
    //     receivers on a Rock 5C's cores (0-3 slow, 4-7 fast): listening,
    //     the later two keep cores 6 and 7 and the transmit worker shares
    //     them; keyed, the transmit worker has core 6 to itself.  Told
    //     "transmitting" for as long as VOX was on, placement kept the
    //     receivers off those cores between transmissions.
    //
    //     Last in the file: the process's placement stays started.
    // ---------------------------------------------------------------
    void placementHearsListeningApartFromTransmitting() {
#ifndef HAVE_WDSP
        QSKIP("needs a live WDSP transmit channel");
#else
        CpuTopology rock;
        for (int cpu = 0; cpu < 8; ++cpu) {
            rock.online.append(cpu);
            rock.capacity.insert(cpu, cpu < 4 ? 405 : 1024);
        }
        rock.clockDomains = {{0, 1, 2, 3}, {4, 5}, {6, 7}};
        PlacementDemand startup;
        startup.rxChannels = {0};
        startup.dspThread = true;
        ThreadPlacement& placement = ThreadPlacement::instance();
        std::atomic<int> workerCalls{0};
        placement.start(rock, startup, std::make_unique<AcceptingApi>(&workerCalls), true);
        QVERIFY(placement.isPlacing());
        placement.registerCurrentThread(ThreadRole::DspThread);
        for (int channel : {0, 1, 2}) {
            placement.setChannelActive(ThreadRole::RxWorker, channel, true);
        }

        const int tx = WdspEngine::kTxChannelId;
        WdspEngine engine;
        engine.m_initialized = true;   // friend access (NEREUS_BUILD_TESTS)
#ifndef Q_OS_WIN
        // As WdspEngine::initialize does in nereusd: the transmit worker
        // registers itself from its own thread as it starts. That
        // registration must leave the DSP thread's entry alone.
        WDSPSetThreadStartHook(&ThreadPlacement::wdspThreadStartHook);
#endif
        TxChannel* ch = engine.createTxChannel(tx);
        QVERIFY(ch != nullptr);
#ifndef Q_OS_WIN
        QTRY_VERIFY(workerCalls.load() > 0);
        QCOMPARE(placement.currentPlan().cpuFor(ThreadRole::DspThread), 5);
#endif
        QVERIFY(placement.currentPlan().coresFor(ThreadRole::TxWorker, tx).isEmpty());

        const auto expectListening = [&]() {
            const PlacementPlan plan = placement.currentPlan();
            QCOMPARE(plan.cpuFor(ThreadRole::DspThread), 5);
            QCOMPARE(plan.cpuFor(ThreadRole::RxWorker, 1), 6);
            QCOMPARE(plan.cpuFor(ThreadRole::RxWorker, 2), 7);
            QCOMPARE(plan.cpuFor(ThreadRole::TxWorker, tx), -1);
            QCOMPARE(plan.coresFor(ThreadRole::TxWorker, tx), (QList<int>{6, 7}));
        };

        // User clicks [VOX].
        ch->setVoxListening(true);
        expectListening();

        // Keyed (G-06): the transmit worker goes ahead of the later
        // receivers. No transmit pump is running here to take core 7.
        ch->setRunning(true);
        PlacementPlan plan = placement.currentPlan();
        QCOMPARE(plan.cpuFor(ThreadRole::TxWorker, tx), 6);
        QCOMPARE(plan.cpuFor(ThreadRole::RxWorker, 1), 7);
        QCOMPARE(plan.cpuFor(ThreadRole::RxWorker, 2), -1);

        // Unkeyed with VOX still on.
        ch->setRunning(false);
        expectListening();

        // VOX off.
        ch->setVoxListening(false);
        plan = placement.currentPlan();
        QVERIFY(plan.coresFor(ThreadRole::TxWorker, tx).isEmpty());
        QCOMPARE(plan.cpuFor(ThreadRole::RxWorker, 1), 6);

        // Keyed and unkeyed without VOX: transmitting, then off.
        ch->setRunning(true);
        QCOMPARE(placement.currentPlan().cpuFor(ThreadRole::TxWorker, tx), 6);
        ch->setRunning(false);
        QVERIFY(placement.currentPlan().coresFor(ThreadRole::TxWorker, tx).isEmpty());

        engine.destroyTxChannel(tx);
#ifndef Q_OS_WIN
        WDSPSetThreadStartHook(nullptr);
#endif
#endif
    }
};

QTEST_APPLESS_MAIN(TstTxChannelVoxListening)
#include "tst_tx_channel_vox_listening.moc"
