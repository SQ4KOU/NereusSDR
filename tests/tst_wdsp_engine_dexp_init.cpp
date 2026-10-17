// no-port-check: NereusSDR-original unit-test file. Thetis cite comments
// document upstream sources; no Thetis logic ported in this test file.
// =================================================================
// tests/tst_wdsp_engine_dexp_init.cpp  (NereusSDR)
// =================================================================
//
// Unit tests for Phase 3M-3a-iii Task 20: WdspEngine create_dexp /
// destroy_dexp lifecycle.  Verifies the bench-failing fix:
//
//   Before Task 20: pdexp[1] was permanently nullptr after createTxChannel
//   because create_dexp had never been ported into NereusSDR's TX-init
//   path.  Every SetDEXP* setter and the SendCBPushDexpVox callback
//   registration silently no-op'd via their pdexp[id] null guards.
//   Bench-confirmed by JJ on ANAN-G2: VOX-keying never engaged MOX
//   because xdexp() never ran (no DEXP module to drive).
//
//   After Task 20: WdspEngine::createTxChannel calls create_dexp with
//   default args sourced verbatim from Thetis cmaster.c:130-157
//   [v2.10.3.13].  pdexp[1] is non-null after createTxChannel returns
//   AND back to nullptr after destroyTxChannel.
//
// Test cases (6):
//   1. createTxChannel_initialisesDexp           - pdexp[0] non-null after create
//   2. destroyTxChannel_allowsRecreate           - destroy then re-create still
//                                                  populates pdexp[0] (proves
//                                                  destroy_dexp + create_dexp
//                                                  lifecycle is correctly wired)
//   3. createRxAfterTx_keepsDexpSlot             - issue #365: RX 0 opened after
//                                                  TX 5 leaves the DEXP alone
//   4. pumpDexp_isNoOpWithoutWdspEngineWire      - pumpDexp degrades safely
//                                                  on directly-constructed
//                                                  TxChannel (no segfault)
//   5. rxZeroResizeAndReopen_keepDexpWorking     - issue #414: RX 0 opened before
//                                                  TX 5, then resized, closed
//                                                  and reopened; the DEXP stays
//                                                  in WDSP's table and working
//   6. txReconnect_makesAFreshWorkingDexp        - issue #414: TX 5 destroyed
//                                                  and created again beside a
//                                                  live RX 0
//
// Source references (cited for traceability; logic lives in WdspEngine.cpp):
//   wdsp/cmaster.c:130-157 [v2.10.3.13]  - Thetis create_dexp call site
//                                          (default args sourced verbatim)
//   wdsp/cmaster.c:267     [v2.10.3.13]  - Thetis destroy_dexp call site
//                                          (BEFORE CloseChannel in destroy_xmtr)
//   wdsp/dexp.c:187-227    [v2.10.3.13]  - create_dexp impl: allocates DEXP
//                                          struct, stores in pdexp[id]
//   wdsp/dexp.c:230-239    [v2.10.3.13]  - destroy_dexp impl: deallocates,
//                                          but DOES NOT clear pdexp[id]!
//                                          (the test below assertion is
//                                          "pointer-after-free is unspecified")
//
// Note on destroy_dexp / pdexp[] bookkeeping (read this if assertion #2
// behaves unexpectedly): Thetis's destroy_dexp at dexp.c:230-239
// [v2.10.3.13] frees the struct but does NOT explicitly null out
// pdexp[id] afterward.  The pointer is dangling until the next
// create_dexp(id, ...) reassigns the slot.  In practice WDSP's
// memory-allocation pattern (and our own asan/UBSan runs) treats the
// slot as "stale pointer — undefined to deref".  The assertion below
// uses a DIFFERENT criterion: we verify that re-creating a TxChannel
// with the same id and then re-destroying behaves consistently, rather
// than asserting on the dangling-pointer value itself.  See test case 2
// for the exact semantics.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-03 - New test file for Phase 3M-3a-iii Task 20: WdspEngine
//                 DEXP DSP-instance lifecycle.  J.J. Boyd (KG4VCF), with
//                 AI-assisted implementation via Anthropic Claude Code.
//   2026-10-09 - Issue #365: production TX channel id, DEXP slot is the
//                 transmitter id, RX-after-TX regression.  J.J. Boyd
//                 (KG4VCF), with AI-assisted implementation via Anthropic
//                 Claude Code.
//   2026-10-12 - Issue #414: RX 0 opened first, then resized and reopened,
//                 and a TX reconnect beside a live RX 0.  J.J. Boyd
//                 (KG4VCF), with AI-assisted implementation via Anthropic
//                 Claude Code.
// =================================================================

#include <QtTest/QtTest>

#include "core/RxChannel.h"
#include "core/TxChannel.h"
#include "core/WdspEngine.h"

// Forward-declare the WDSP DEXP global so the test can introspect whether
// create_dexp populated the slot.  The full struct definition lives in
// third_party/wdsp/src/dexp.h (Windows-isms shimmed by linux_port.h);
// since we only test pointer non-null-ness, the opaque forward declaration
// is sufficient and avoids dragging in <Windows.h>-flavoured shims for
// platforms that don't define HAVE_WDSP.
//
// Cite: Thetis wdsp/dexp.h:104 [v2.10.3.13] - `extern DEXP pdexp[];`
//       (DEXP is `typedef struct _dexp *DEXP;` at dexp.h:102.)
#ifdef HAVE_WDSP
extern "C" {
struct _dexp;
typedef struct _dexp *DEXP;
#ifdef Q_OS_WIN
// The test also links WDSP's static library, so a plain declaration on
// Windows binds to a second, unused copy of the table in this executable.
// Read the Core DLL's table, the one WdspEngine fills.
extern __declspec(dllimport) DEXP pdexp[];
#else
extern DEXP pdexp[];
#endif
}
#endif

using namespace NereusSDR;

// The production TX channel id (5).  Issue #365: the DEXP slot is the
// transmitter id (TxChannel::kDexpId), never this channel id; WDSP's
// pdexp[] has four slots, so pdexp[5] wrote past its end.  Testing with
// id 1 hid that, because 1 happened to fit.
static constexpr int kTxChannelId = WdspEngine::kTxChannelId;
static constexpr int kDexpId = TxChannel::kDexpId;

#ifdef HAVE_WDSP
// How many of WDSP's DEXP slots hold an object.  Counts every slot of the
// table (Thetis wdsp/dexp.c:29 [v2.10.3.15]: `DEXP pdexp[4];`) instead of trusting the
// index the code under test uses, so a DEXP stored outside the table shows
// up as a count of zero.
static int dexpSlotsInUse()
{
    int used = 0;
    for (int slot = 0; slot < TxChannel::kWdspDexpSlots; ++slot) {
        if (pdexp[slot] != nullptr) {
            ++used;
        }
    }
    return used;
}

// True when the DEXP detector runs on the blocks the TX worker hands it:
// its peak falls on silence and rises on a half-scale block.  The peak is
// the detector's smoothed level (Thetis wdsp/dexp.c:307-309 [v2.10.3.15]), which only
// moves when xdexp runs on a live DEXP object.  512 doubles is the DEXP
// buffer createTxChannel sizes for its default 256-frame input block.
static bool dexpFollowsAudio(TxChannel* tx)
{
    const std::vector<double> silence(512, 0.0);
    const std::vector<double> halfScale(512, 0.5);
    for (int i = 0; i < 40; ++i) {
        tx->pumpDexp(silence.data());
    }
    const double quiet = tx->getDexpPeakSignal();
    tx->pumpDexp(halfScale.data());
    const double loud = tx->getDexpPeakSignal();
    return loud > 0.05 && quiet < loud / 10.0;
}
#endif

class TstWdspEngineDexpInit : public QObject {
    Q_OBJECT

private slots:

    // ── Test 1: pdexp[kDexpId] is non-null after createTxChannel ─────────────
    //
    // The assertion that proves the fix.  Before Task 20, this would have
    // failed (pdexp[1] permanently nullptr; create_dexp never called).
    // After Task 20, pdexp[1] is non-null because WdspEngine::createTxChannel
    // calls create_dexp(channelId, ...) right after OpenChannel(type=1).
    void createTxChannel_initialisesDexp() {
        WdspEngine engine;
        engine.m_initialized = true;   // friend access (NEREUS_BUILD_TESTS)
                                        // - bypasses async wisdom path

        TxChannel* tx = engine.createTxChannel(kTxChannelId);
        QVERIFY(tx != nullptr);

#ifdef HAVE_WDSP
        // The bench-failing assertion that motivated this entire commit.
        QVERIFY2(pdexp[kDexpId] != nullptr,
                 "pdexp[kDexpId] must be non-null after createTxChannel - "
                 "create_dexp call missing from WdspEngine::createTxChannel "
                 "is the root cause of bench-confirmed VOX-keying failure "
                 "(`[VOXDIAG] registerVoxCallback ch= 1 SKIPPED: pdexp NULL`)");
#endif

        engine.destroyTxChannel(kTxChannelId);
    }

    // ── Test 2: destroy + recreate cycle is consistent ───────────────────────
    //
    // Thetis destroy_dexp (dexp.c:230-239 [v2.10.3.13]) frees the struct
    // but does NOT null pdexp[id] afterward - the pointer dangles until
    // the next create_dexp(id, ...) reassigns the slot.  Asserting
    // pdexp[id] == nullptr after destroy would be incorrect (it's a
    // dangling pointer, not a null pointer).
    //
    // What we CAN safely assert is that a destroy + create cycle yields
    // a NEW non-null pointer that we can use, and that a second destroy
    // doesn't crash.  This proves the lifecycle is correctly wired -
    // create_dexp runs again on the second create, allocating a fresh
    // DEXP module (different address from the first, but we don't assert
    // that because a slab allocator could legitimately reuse the slot).
    void destroyTxChannel_allowsRecreate() {
        WdspEngine engine;
        engine.m_initialized = true;   // friend access (NEREUS_BUILD_TESTS)

        TxChannel* tx1 = engine.createTxChannel(kTxChannelId);
        QVERIFY(tx1 != nullptr);
#ifdef HAVE_WDSP
        QVERIFY(pdexp[kDexpId] != nullptr);
#endif

        engine.destroyTxChannel(kTxChannelId);
        QVERIFY(engine.txChannel(kTxChannelId) == nullptr);

        // Recreate on the same id - create_dexp must run again and pdexp[id]
        // must again be non-null (whether or not it's the same address).
        TxChannel* tx2 = engine.createTxChannel(kTxChannelId);
        QVERIFY(tx2 != nullptr);
#ifdef HAVE_WDSP
        QVERIFY2(pdexp[kDexpId] != nullptr,
                 "pdexp[kDexpId] must be non-null after RE-create - the second "
                 "createTxChannel call must invoke create_dexp again, not "
                 "skip it on the assumption a previous create lingered");
#endif

        engine.destroyTxChannel(kTxChannelId);
    }

    // ── Test 3: issue #365, opening RX 0 leaves the DEXP slot alone ─────────
    //
    // The crash order from the reporter's log: TX channel 5 is created, then
    // RX channel 0.  With DEXP stored at pdexp[5] (past the end of WDSP's
    // four-slot table) the Windows build kept the RX channel table right
    // after pdexp[], so opening RX 0 replaced the DEXP pointer and the next
    // xdexp entered a critical section inside RX audio memory.  The DEXP
    // object must sit in an in-bounds slot that RX creation cannot touch,
    // and pumping DEXP afterwards must run on that object.
    void createRxAfterTx_keepsDexpSlot() {
        WdspEngine engine;
        engine.m_initialized = true;   // friend access (NEREUS_BUILD_TESTS)

        TxChannel* tx = engine.createTxChannel(kTxChannelId);
        QVERIFY(tx != nullptr);
#ifdef HAVE_WDSP
        DEXP const created = pdexp[kDexpId];
        QVERIFY2(created != nullptr,
                 "create_dexp must store the TX DEXP inside pdexp[0..3]");
#endif

        RxChannel* rx = engine.createRxChannel(0);
        QVERIFY(rx != nullptr);
#ifdef HAVE_WDSP
        QCOMPARE(pdexp[kDexpId], created);
#endif

        // A block of silence through the real worker path (xdexp).
        const std::vector<double> silence(512, 0.0);
        for (int i = 0; i < 8; ++i) {
            tx->pumpDexp(silence.data());
        }
#ifdef HAVE_WDSP
        QCOMPARE(pdexp[kDexpId], created);
#endif

        engine.destroyRxChannel(0);
        engine.destroyTxChannel(kTxChannelId);
#ifdef HAVE_WDSP
        QVERIFY(pdexp[kDexpId] == nullptr);
#endif
    }

    // ── Test 5: issue #414, RX 0 open first, then resized and reopened ──────
    //
    // The order that survives the connect in release 2026.10.0 and crashes
    // later: RX channel 0 is open before TX channel 5 creates its DEXP.  That
    // release stored the DEXP at pdexp[5]; in its Windows build that cell was
    // RX 0's output buffer pointer (rxa[0].outbuff).  WDSP frees and replaces
    // that buffer with zeroed memory when RX 0's DSP buffer size changes
    // (Thetis wdsp/RXA.c:770-771 [v2.10.3.15]) and when RX 0 closes and
    // reopens (RXA.c:597 and :35 [v2.10.3.15]), so the next xdexp entered a
    // zeroed critical section.
    // The first mode change after a connect does the resize: RxChannel
    // starts at 4096 and the per-mode default is 64.
    //
    // The DEXP must sit in a slot of WDSP's own table, stay the same object
    // through every one of those RX 0 events, and keep following the audio.
    void rxZeroResizeAndReopen_keepDexpWorking() {
        WdspEngine engine;
        engine.m_initialized = true;   // friend access (NEREUS_BUILD_TESTS)

        RxChannel* rx = engine.createRxChannel(0);
        QVERIFY(rx != nullptr);
        TxChannel* tx = engine.createTxChannel(kTxChannelId);
        QVERIFY(tx != nullptr);
#ifdef HAVE_WDSP
        QVERIFY2(dexpSlotsInUse() == 1,
                 "create_dexp must store the TX DEXP inside pdexp[0..3]; a "
                 "slot past the table is other WDSP memory (issue #414)");
        DEXP const created = pdexp[kDexpId];
        QVERIFY(created != nullptr);
        QVERIFY(dexpFollowsAudio(tx));

        // The first mode change after a connect: DSP buffer 4096 -> 64.
        rx->setDspBufferSizeSamples(64);
        QCOMPARE(rx->dspBlockSize(), 64);
        QCOMPARE(pdexp[kDexpId], created);
        QVERIFY(dexpFollowsAudio(tx));

        // RX 0 closed, then opened again (receiver count change, reconnect).
        engine.destroyRxChannel(0);
        QCOMPARE(pdexp[kDexpId], created);
        QVERIFY(dexpFollowsAudio(tx));
        rx = engine.createRxChannel(0);
        QVERIFY(rx != nullptr);
        QCOMPARE(pdexp[kDexpId], created);
        QVERIFY(dexpFollowsAudio(tx));
        QCOMPARE(dexpSlotsInUse(), 1);
#endif

        engine.destroyRxChannel(0);
        engine.destroyTxChannel(kTxChannelId);
#ifdef HAVE_WDSP
        QCOMPARE(dexpSlotsInUse(), 0);
#endif
    }

    // ── Test 6: issue #414, a TX reconnect beside a live RX 0 ───────────────
    //
    // A disconnect destroys the TX channel and its DEXP; the next connect
    // creates both again while RX 0 may still be open.  No DEXP object is
    // left in the table between the two, and the new one follows the audio.
    void txReconnect_makesAFreshWorkingDexp() {
        WdspEngine engine;
        engine.m_initialized = true;   // friend access (NEREUS_BUILD_TESTS)

        RxChannel* rx = engine.createRxChannel(0);
        QVERIFY(rx != nullptr);
        TxChannel* tx = engine.createTxChannel(kTxChannelId);
        QVERIFY(tx != nullptr);
#ifdef HAVE_WDSP
        QCOMPARE(dexpSlotsInUse(), 1);
        QVERIFY(dexpFollowsAudio(tx));
#endif

        engine.destroyTxChannel(kTxChannelId);
#ifdef HAVE_WDSP
        QCOMPARE(dexpSlotsInUse(), 0);
#endif

        tx = engine.createTxChannel(kTxChannelId);
        QVERIFY(tx != nullptr);
#ifdef HAVE_WDSP
        QCOMPARE(dexpSlotsInUse(), 1);
        QVERIFY(dexpFollowsAudio(tx));
#endif

        engine.destroyTxChannel(kTxChannelId);
        engine.destroyRxChannel(0);
#ifdef HAVE_WDSP
        QCOMPARE(dexpSlotsInUse(), 0);
#endif
    }

    // ── Test 4: TxChannel::pumpDexp is null-safe before WdspEngine wires ─────
    //
    // Direct construction (bypassing WdspEngine::createTxChannel) leaves
    // m_dexpBuffer null; pumpDexp must degrade to a no-op without
    // dereferencing the missing buffer pointer.  This guarantees test
    // builds and edge-case construction paths don't segfault.
    void pumpDexp_isNoOpWithoutWdspEngineWire() {
        TxChannel ch(kTxChannelId);
        // 128 doubles == 64 complex samples == default block size (matches
        // TxWorkerThread::kBlockFrames * 2).  Content is irrelevant - the
        // assertion is that pumpDexp does not crash.
        const std::vector<double> dummy(128, 0.0);
        for (int i = 0; i < 20; ++i) {
            ch.pumpDexp(dummy.data());
        }
        // Calling with nullptr must also be a no-op.
        ch.pumpDexp(nullptr);
    }
};

QTEST_APPLESS_MAIN(TstWdspEngineDexpInit)
#include "tst_wdsp_engine_dexp_init.moc"
