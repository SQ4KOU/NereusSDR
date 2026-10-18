// no-port-check: NereusSDR-original unit-test file. Thetis and WDSP cite
// comments document upstream sources; no Thetis logic ported in this file.
// =================================================================
// tests/tst_rx_channel_agc_fixed_seed.cpp  (NereusSDR)
// =================================================================
//
// The AGC Fixed Gain a new or rebuilt RX channel runs in WDSP.
//
// WDSP opens every receiver with create_wcpagc's fixed_gain = 1000.0
// (linear, 60 dB; third_party/wdsp/src/RXA.c:385). RxChannel caches 20 dB
// (Thetis setup.Designer.cs:39565-39569 [v2.10.3.15], udDSPAGCFixedGaindB
// Value = 20) and its setter returns early when asked for the cached value.
// Without a seed at channel open, the slice's default 20 dB never reached
// WDSP and AGC Off ran at 60 dB, after every connect and every rebuild.
//
// The same holds for AGC Max Gain: WDSP opens at max_gain 10000.0 (80 dB,
// RXA.c:383), RxChannel caches 90 dB (setup.Designer.cs:39443-39447
// [v2.10.3.15]).
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-14 - New test file: AGC Fixed Gain seed at RX channel open.
//                 J.J. Boyd (KG4VCF), with AI-assisted implementation via
//                 Anthropic Claude Code.
//   2026-10-15 - Slices B to D open with their saved AGC gains.
//                 J.J. Boyd (KG4VCF), with AI-assisted implementation via
//                 Anthropic Claude Code.
//   2026-10-15 - AGC Max Gain seed at RX channel open. J.J. Boyd
//                 (KG4VCF), with AI-assisted implementation via Anthropic
//                 Claude Code.
// =================================================================

#include <QtTest/QtTest>

#include "core/AppSettings.h"
#include "core/DspControlThread.h"
#include "core/RxChannel.h"
#include "core/SampleRateCatalog.h"
#include "core/WdspEngine.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <cmath>

#ifdef HAVE_WDSP
// Read the applied fixed gain off the WDSP receiver itself.
extern "C" {
#include "../third_party/wdsp/src/comm.h"
}
#undef min
#undef max
#endif

using namespace NereusSDR;

namespace {

constexpr int kChannelId = 0;

#ifdef HAVE_WDSP
// The fixed gain WDSP applies, in dB (SetRXAAGCFixed stores
// pow(10, dB / 20), third_party/wdsp/src/wcpAGC.c:546-549).
double appliedFixedGainDb(int channelId)
{
    return 20.0 * std::log10(rxa[channelId].agc.p->fixed_gain);
}

// The max gain WDSP applies, in dB (SetRXAAGCTop stores pow(10, dB / 20),
// third_party/wdsp/src/wcpAGC.c:527-531).
double appliedMaxGainDb(int channelId)
{
    return 20.0 * std::log10(rxa[channelId].agc.p->max_gain);
}
#endif

} // namespace

class TestRxChannelAgcFixedSeed : public QObject {
    Q_OBJECT

private slots:
    // A freshly opened channel runs the fixed gain RxChannel reports.
    void openedChannelRunsTheCachedFixedGain()
    {
#ifndef HAVE_WDSP
        QSKIP("needs WDSP");
#else
        WdspEngine engine;
        engine.m_initialized = true;   // friend access (NEREUS_BUILD_TESTS)
        RxChannel* rx = engine.createRxChannel(kChannelId);
        QVERIFY(rx != nullptr);

        QCOMPARE(rx->agcFixedGain(), 20);
        QCOMPARE(qRound(appliedFixedGainDb(kChannelId)), 20);

        // The slice's default push (RadioModel's channel seed) is 20 dB.
        rx->setAgcFixedGain(20);
        QCOMPARE(qRound(appliedFixedGainDb(kChannelId)), 20);

        rx->setAgcFixedGain(35);
        QCOMPARE(qRound(appliedFixedGainDb(kChannelId)), 35);

        engine.destroyRxChannel(kChannelId);
#endif
    }

    // A rebuilt channel keeps the slice's fixed gain, including the
    // default the new wrapper already caches.
    void rebuiltChannelKeepsTheFixedGain()
    {
#ifndef HAVE_WDSP
        QSKIP("needs WDSP");
#else
        for (const int dB : {20, 45}) {
            WdspEngine engine;
            engine.m_initialized = true;
            RxChannel* rx = engine.createRxChannel(kChannelId);
            QVERIFY(rx != nullptr);
            rx->setAgcFixedGain(dB);

            ChannelConfig cfg;
            QVERIFY(engine.rebuildRxChannel(kChannelId, cfg) >= 0);
            RxChannel* rebuilt = engine.rxChannel(kChannelId);
            QVERIFY(rebuilt != nullptr);

            QCOMPARE(rebuilt->agcFixedGain(), dB);
            QCOMPARE(qRound(appliedFixedGainDb(kChannelId)), dB);

            engine.destroyRxChannel(kChannelId);
        }
#endif
    }

    // A freshly opened channel runs the max gain RxChannel reports (90 dB,
    // Thetis's default), so a later push of 90 is not lost to the
    // setter's equality guard.
    void openedChannelRunsTheCachedMaxGain()
    {
#ifndef HAVE_WDSP
        QSKIP("needs WDSP");
#else
        WdspEngine engine;
        engine.m_initialized = true;
        RxChannel* rx = engine.createRxChannel(kChannelId);
        QVERIFY(rx != nullptr);

        QCOMPARE(rx->agcMaxGain(), 90);
        QCOMPARE(qRound(appliedMaxGainDb(kChannelId)), 90);

        rx->setAgcMaxGain(90);
        QCOMPARE(qRound(appliedMaxGainDb(kChannelId)), 90);

        rx->setAgcMaxGain(70);
        QCOMPARE(qRound(appliedMaxGainDb(kChannelId)), 70);

        engine.destroyRxChannel(kChannelId);
#endif
    }

    // A second slice's channel opens with that slice's saved Fixed Gain and
    // Max Gain, as Slice A's does, not with the open-time seeds.
    void secondSliceOpensWithItsSavedGains()
    {
#ifndef HAVE_WDSP
        QSKIP("needs WDSP");
#else
        constexpr int kRateHz = 48000;
        AppSettings::instance().clear();
        RadioModel model;
        WdspEngine* engine = model.wdspEngine();
        engine->m_initialized = true;   // friend access (NEREUS_BUILD_TESTS)

        model.configureStreamPool(/*userDdcCount*/ 2, /*maxSlices*/ 2, kRateHz);
        model.addSlice();
        const int b = model.addSlice();
        SliceModel* sliceB = model.sliceById(b);
        QVERIFY(sliceB != nullptr);
        QVERIFY(sliceB->streamIndex() >= 0);
        // Saved before any channel exists, as a restore at connect is.
        sliceB->setAgcFixedGain(37);
        sliceB->setAgcMaxGain(75);

        model.openRxChannelPool(2, bufferSizeForRate(kRateHz), kRateHz);
        // The channel jobs run on the receive lane; let them land.
        QVERIFY(model.receiveLane() == nullptr
                || model.receiveLane()->waitIdleForTest(600000));
        RxChannel* ch = engine->rxChannel(b);
        QVERIFY(ch != nullptr);
        QVERIFY(ch->isActive());

        QCOMPARE(qRound(appliedFixedGainDb(b)), 37);
        QCOMPARE(qRound(appliedMaxGainDb(b)), 75);
#endif
    }
};

QTEST_MAIN(TestRxChannelAgcFixedSeed)
#include "tst_rx_channel_agc_fixed_seed.moc"
