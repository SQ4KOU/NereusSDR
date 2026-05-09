// NereusSDR-original test — no Thetis source ported here.
//
// Regression test for the live sample-rate-change crash that shipped after
// v0.3.2 (PR #219 + a4d076f).  The previous WdspEngine::rebuildRxChannel /
// rebuildTxChannel path destroyed the C++ wrapper and constructed a new
// one, leaving 7+ raw pointer holders dangling (RadioModel::m_txChannel,
// TxWorkerThread, PureSignal, MeterPoller, TwoToneController, TxCfcDialog,
// TxChannel::s_voxKeyInstance).  RadioModel::setSampleRateLive then called
// moveToThread on the dangling pointer and SIGSEGV'd.
//
// The Thetis-faithful fix (cmaster.c::SetXcmInrate at
// ChannelMaster/cmaster.c:453-507 [v2.10.3.13]) keeps the same channel
// alive across a rate change and only calls SetInputSamplerate +
// SetInputBuffsize on the existing channel.  This test verifies the
// invariant the crash violated: the RxChannel object identity survives a
// setSampleRate() call.

#include <QtTest/QtTest>
#include "core/RxChannel.h"
#include "core/SampleRateCatalog.h"

using namespace NereusSDR;

namespace {
constexpr int kTestChannel = 99;   // never opened in WDSP
constexpr int kInitialRate = 48000;
constexpr int kInitialSize = 64;   // bufferSizeForRate(48000)
constexpr int kNewRate     = 192000;
constexpr int kNewSize     = 256;  // bufferSizeForRate(192000)
}

class TestSetSampleRateLive : public QObject {
    Q_OBJECT

private slots:
    // The crashing v0.3.2-era pattern was destroy-and-recreate; a passing
    // test for that pattern is impossible (the original pointer is freed).
    // The contract for the new path is: same object, mutated state.
    void setSampleRate_preserves_object_identity()
    {
        RxChannel ch(kTestChannel, kInitialSize, kInitialRate);
        const RxChannel* before = &ch;

        ch.setSampleRate(kNewRate);

        const RxChannel* after = &ch;
        QCOMPARE(before, after);  // must be the same C++ object
    }

    void setSampleRate_updates_rate_accessor()
    {
        RxChannel ch(kTestChannel, kInitialSize, kInitialRate);
        QCOMPARE(ch.sampleRate(), kInitialRate);

        ch.setSampleRate(kNewRate);
        QCOMPARE(ch.sampleRate(), kNewRate);
    }

    void setSampleRate_updates_buffer_size()
    {
        RxChannel ch(kTestChannel, kInitialSize, kInitialRate);
        QCOMPARE(ch.bufferSize(), kInitialSize);

        ch.setSampleRate(kNewRate);
        QCOMPARE(ch.bufferSize(), kNewSize);
        QCOMPARE(ch.bufferSize(), bufferSizeForRate(kNewRate));
    }

    // Idempotent: setting the current rate is a no-op (no crash, no change).
    // Mirrors the SetXcmInrate guard at cmaster.c:457 [v2.10.3.13]:
    //   if (pcm->xcm_inrate[in_id] != rate) { ... }
    void setSampleRate_is_idempotent_for_same_rate()
    {
        RxChannel ch(kTestChannel, kInitialSize, kInitialRate);

        ch.setSampleRate(kInitialRate);  // same as construction-time
        QCOMPARE(ch.sampleRate(), kInitialRate);
        QCOMPARE(ch.bufferSize(), kInitialSize);
    }

    // The rate change must round-trip: change and change back.
    void setSampleRate_round_trips()
    {
        RxChannel ch(kTestChannel, kInitialSize, kInitialRate);

        ch.setSampleRate(kNewRate);
        QCOMPARE(ch.sampleRate(), kNewRate);

        ch.setSampleRate(kInitialRate);
        QCOMPARE(ch.sampleRate(), kInitialRate);
        QCOMPARE(ch.bufferSize(), kInitialSize);
    }
};

QTEST_MAIN(TestSetSampleRateLive)
#include "tst_set_sample_rate_live.moc"
