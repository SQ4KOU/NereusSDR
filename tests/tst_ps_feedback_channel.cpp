// no-port-check: NereusSDR-original unit-test file.  Thetis cite comments
// document upstream sources; no Thetis logic ported in this test file.
// =================================================================
// tests/tst_ps_feedback_channel.cpp  (NereusSDR)
// =================================================================
//
// Unit tests for the Phase 3M-4 Task 4 PsFeedbackChannel wrapper class.
// PsFeedbackChannel holds the WDSP feedback RX channel id that calcc reads
// autonomously via pscc() (calcc.c:617 [v2.10.3.13]).  WdspEngine owns one
// instance; tests exercise:
//
//   1. hasUniqueChannelId — ps feedback channel id is distinct from the
//      currently-allocated TX channel id (kTxChannelId = 1).
//
//   2. sampleRateRoundTrips — setSampleRate / sampleRate atomic round-trip.
//      Defaults to 192000 (G2-class ps_rate per cmaster.cs:424
//      [v2.10.3.13]); the PureSignal coordinator (Task 7) re-applies
//      rx1_rate for HL2 boards (psSampleRate=0 sentinel) before MOX.
//
//   3. feedSamplesIncreasesActivityCounter — totalSamplesIn() monotonically
//      increases on each feedSamples() call.  This counter is used by the
//      future PsaIndicatorWidget "Fbk LED" to indicate sample flow during
//      MOX+PS-on; until Task 7 wires the actual fexchange0 sample-pump,
//      the body is a counter-only stub so call-site activity is verifiable.
//
//   4. psFeedbackChannelDistinctFromTxAndRx — round-trip via WdspEngine
//      ownership: pulling the channel through engine.psFeedbackChannel()
//      yields a non-null pointer with a channel id that doesn't collide
//      with the existing TX channel id.
//
// Test strategy mirrors tst_wdsp_engine_dexp_init.cpp: friend access via
// NEREUS_BUILD_TESTS sets m_initialized = true synchronously to bypass the
// async wisdom path, then drives createTxChannel + psFeedbackChannel().
//
// Source: NereusSDR-original wrapper.  No Thetis source; Thetis manages
//         WDSP channels via ChannelMaster.dll.  See ps_sync_stub.c for
//         the SetPSRxIdx routing symbol.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-06 — New test file for Phase 3M-4 Task 4: PsFeedbackChannel
//                 wrapper smoke tests.  J.J. Boyd (KG4VCF), with
//                 AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>

#include "core/PsFeedbackChannel.h"
#include "core/TxChannel.h"
#include "core/WdspEngine.h"

using namespace NereusSDR;

// Channel ID convention (matches tst_wdsp_engine_tx_channel.cpp):
//   TX channel: WDSP.id(1, 0) = CMsubrcvr * CMrcvr = 1 * 1 = 1
//   (dsp.cs:926-944 case 2, with NereusSDR CMsubrcvr=CMrcvr=1)
static constexpr int kTxChannelIdForTest = 1;

class TstPsFeedbackChannel : public QObject {
    Q_OBJECT

private slots:

    // ── Test 1: PS feedback channel has a non-zero, distinct channel id ─────
    //
    // After WdspEngine::initialize() (synchronous test path via friend
    // m_initialized=true) and a createTxChannel(1) call, the ps feedback
    // channel must be live and its channelId() must not collide with the
    // TX channel id.
    void hasUniqueChannelId() {
        WdspEngine engine;
        engine.m_initialized = true;   // friend access (NEREUS_BUILD_TESTS)
                                        // bypasses async wisdom path

        // PS feedback channel is opened during initialize() — but in the
        // test path we bypass initialize() via friend.  Drive the open
        // explicitly the same way finishInitialization() would in production.
        engine.openPsFeedbackChannelForTesting();

        TxChannel* tx = engine.createTxChannel(kTxChannelIdForTest);
        QVERIFY(tx != nullptr);

        PsFeedbackChannel* fb = engine.psFeedbackChannel();
        QVERIFY(fb != nullptr);
        QVERIFY(fb->channelId() != tx->channelId());

        engine.destroyTxChannel(kTxChannelIdForTest);
    }

    // ── Test 2: setSampleRate round-trip ────────────────────────────────────
    //
    // Round-trips through the atomic m_sampleRate field.  In HAVE_WDSP
    // builds, also drives ::SetInputSamplerate(channelId, rate) on the
    // opened WDSP channel.  Both 192000 (G2-class) and 48000 (HL2 rx1_rate
    // example) must round-trip cleanly.
    void sampleRateRoundTrips() {
        WdspEngine engine;
        engine.m_initialized = true;
        engine.openPsFeedbackChannelForTesting();

        PsFeedbackChannel* fb = engine.psFeedbackChannel();
        QVERIFY(fb != nullptr);

        fb->setSampleRate(192000);
        QCOMPARE(fb->sampleRate(), 192000);

        fb->setSampleRate(48000);
        QCOMPARE(fb->sampleRate(), 48000);
    }

    // ── Test 3: feedSamples increases the activity counter ──────────────────
    //
    // The activity counter is used by the future PsaIndicatorWidget "Fbk
    // LED" to indicate sample flow.  Until Task 7 wires the actual
    // fexchange0 sample-pump in feedSamples(), this counter-only stub is
    // sufficient to prove the call site fires.
    void feedSamplesIncreasesActivityCounter() {
        WdspEngine engine;
        engine.m_initialized = true;
        engine.openPsFeedbackChannelForTesting();

        PsFeedbackChannel* fb = engine.psFeedbackChannel();
        QVERIFY(fb != nullptr);

        const qint64 before = fb->totalSamplesIn();
        float buf[1024] = {};   // 512 complex pairs == 1024 floats
        fb->feedSamples(buf, 512);
        QVERIFY(fb->totalSamplesIn() > before);
    }

    // ── Test 4: PS feedback channel id distinct from TX (and RX1) ───────────
    //
    // Verify the chosen ps feedback channel id (5 — see PsFeedbackChannel.cpp
    // for the rationale) does NOT collide with the existing TX channel id (1).
    // RX1 typically uses channel id 0 in production, but the live RX channel
    // is opened from RadioModel::connectToRadio() and we don't drive that
    // here; the assertion against TX is the load-bearing one.
    void psFeedbackChannelDistinctFromTxAndRx() {
        WdspEngine engine;
        engine.m_initialized = true;
        engine.openPsFeedbackChannelForTesting();

        PsFeedbackChannel* fb = engine.psFeedbackChannel();
        QVERIFY(fb != nullptr);
        const int psId = fb->channelId();

        TxChannel* tx = engine.createTxChannel(kTxChannelIdForTest);
        QVERIFY(tx != nullptr);
        const int txId = tx->channelId();

        QVERIFY(psId != txId);
        // PS id must also be distinct from RX1's typical id (0) and from
        // the future RX2 main / diversity / TX slots (2-4) sketched in
        // wdsp-integration.md §11.1.
        QVERIFY(psId != 0);

        engine.destroyTxChannel(kTxChannelIdForTest);
    }
};

QTEST_APPLESS_MAIN(TstPsFeedbackChannel)
#include "tst_ps_feedback_channel.moc"
