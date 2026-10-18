// no-port-check: NereusSDR-original unit-test file. Thetis and WDSP cite
// comments document upstream sources; no Thetis logic ported in this file.
// =================================================================
// tests/tst_slice_squelch_channel_seed.cpp  (NereusSDR)
// =================================================================
//
// A slice other than A gets its saved squelch on its own WDSP channel, and
// a squelch (or other DSP) edit on a slice that is not the active one is
// saved under that slice's own keys.
//
// WDSP opens every squelch with run 0 (third_party/wdsp/src/RXA.c:163,228,
// 534). A slice restored with its signals blocked reaches WDSP only through
// RadioModel::activateSliceChannel's seed, so the seed is read back off
// rxa[] here.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-15 - New test file: squelch seed for slices B to D and the
//                 per-slice settings save. J.J. Boyd (KG4VCF), with
//                 AI-assisted implementation via Anthropic Claude Code.
//   2026-10-16 - The FM threshold check expects the slider mapping
//                 RxChannel::fmsqThresholdForWdsp sends, not pow(10, dB / 20).
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-15 - Added the non-squelch case (SNB, AGC hang, RIT) for the
//                 per-slice settings save. J.J. Boyd (KG4VCF), with
//                 AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>

#include "core/AppSettings.h"
#include "core/DspControlThread.h"
#include "core/RadioConnection.h"
#include "core/RxChannel.h"
#include "core/SampleRateCatalog.h"
#include "core/WdspEngine.h"
#include "models/Band.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <cmath>

#ifdef HAVE_WDSP
extern "C" {
#include "../third_party/wdsp/src/comm.h"
}
#undef min
#undef max
#endif

using namespace NereusSDR;

namespace {

// A connection that is connected and does nothing, so RadioModel wires each
// slice's DSP signals (wireSliceSignals returns without a connection).
class MockConnection : public RadioConnection {
    Q_OBJECT
public:
    explicit MockConnection(QObject* parent = nullptr)
        : RadioConnection(parent)
    {
        setState(ConnectionState::Connected);
    }

    void init() override {}
    void connectToRadio(const NereusSDR::RadioInfo&) override {}
    void disconnect() override {}
    void setReceiverFrequency(int, quint64) override {}
    void setTxFrequency(quint64) override {}
    void setActiveReceiverCount(int) override {}
    void setSampleRate(int) override {}
    void setAttenuator(int) override {}
    void setPreamp(bool) override {}
    void setTxDrive(int) override {}
    void sendTxIq(const float*, int) override {}
    void setWatchdogEnabled(bool) override {}
    void setAntennaRouting(AntennaRouting) override {}
    void setMox(bool) override {}
    void setTrxRelay(bool) override {}
    void setMicBoost(bool) override {}
    void setLineIn(bool) override {}
    void setMicTipRing(bool) override {}
    void setMicBias(bool) override {}
    void setLineInGain(int) override {}
    void setUserDigOut(quint8) override {}
    void setPuresignalRun(bool) override {}
    void setMicPTTDisabled(bool) override {}
    void setMicXlr(bool) override {}
};

} // namespace

class TestSliceSquelchChannelSeed : public QObject {
    Q_OBJECT

private slots:
    void init()    { AppSettings::instance().clear(); }
    void cleanup() { AppSettings::instance().clear(); }

    // Slice B (index 1), restored with its signals blocked as a layout
    // restore is, opens its channel running its saved squelch.
    void secondSliceOpensWithItsSavedSquelch()
    {
#ifndef HAVE_WDSP
        QSKIP("needs WDSP");
#else
        constexpr int kRateHz = 48000;
        auto& s = AppSettings::instance();
        s.setValue(QStringLiteral("Slice1/SsqlThresh"), 40.0);
        s.setValue(QStringLiteral("Slice1/SsqlEnabled"), QStringLiteral("True"));
        s.setValue(QStringLiteral("Slice1/AmsqThresh"), -60.0);
        s.setValue(QStringLiteral("Slice1/AmsqEnabled"), QStringLiteral("True"));
        s.setValue(QStringLiteral("Slice1/FmsqThresh"), -40.0);
        s.setValue(QStringLiteral("Slice1/FmsqEnabled"), QStringLiteral("True"));

        RadioModel model;
        WdspEngine* engine = model.wdspEngine();
        engine->m_initialized = true;   // friend access (NEREUS_BUILD_TESTS)

        model.configureStreamPool(/*userDdcCount*/ 2, /*maxSlices*/ 2, kRateHz);
        model.addSlice();
        const int b = model.addSlice();
        QCOMPARE(b, 1);
        SliceModel* sliceB = model.sliceById(b);
        QVERIFY(sliceB != nullptr);
        QVERIFY(sliceB->streamIndex() >= 0);
        {
            const QSignalBlocker block(sliceB);
            sliceB->restoreFromSettings(Band::Band20m);
        }
        QVERIFY(sliceB->ssqlEnabled());
        QVERIFY(sliceB->amsqEnabled());
        QVERIFY(sliceB->fmsqEnabled());

        model.openRxChannelPool(2, bufferSizeForRate(kRateHz), kRateHz);
        QVERIFY(model.receiveLane() == nullptr
                || model.receiveLane()->waitIdleForTest(600000));
        RxChannel* ch = engine->rxChannel(b);
        QVERIFY(ch != nullptr);
        QVERIFY(ch->isActive());

        // SetRXASSQLThreshold stores threshold / 2 (ssql.c:344).
        QCOMPARE(rxa[b].ssql.p->run, 1);
        QVERIFY(std::abs(rxa[b].ssql.p->wthresh - 0.40 / 2.0) < 1e-9);
        // SetRXAAMSQThreshold stores pow(10, dB / 20) (amsq.c:221-224).
        QCOMPARE(rxa[b].amsq.p->run, 1);
        QVERIFY(std::abs(rxa[b].amsq.p->unmute_thresh - std::pow(10.0, -60.0 / 20.0)) < 1e-12);
        // RxChannel::setFmsqThresh passes fmsqThresholdForWdsp(slider), the
        // Thetis slider mapping (console.cs:47295-47304 [v2.10.3.15]);
        // fmsq.c:250 stores it as tail_thresh.
        QCOMPARE(rxa[b].fmsq.p->run, 1);
        QVERIFY(std::abs(rxa[b].fmsq.p->tail_thresh - RxChannel::fmsqThresholdForWdsp(-40.0)) < 1e-12);
#endif
    }

    // A squelch edit on slice B while A is the active slice is saved under
    // B's keys, and reloads into B.
    void squelchEditOnInactiveSliceIsSavedToThatSlice()
    {
        RadioModel model;
        auto* mock = new MockConnection(&model);
        model.injectConnectionForTest(mock);
        const int a = model.addSlice();
        const int b = model.addSlice();
        QCOMPARE(b, 1);
        model.setActiveSliceById(a);
        model.flushPendingSettingsSave();

        SliceModel* sliceB = model.sliceById(b);
        QVERIFY(sliceB != nullptr);
        sliceB->setFmsqThresh(-25.0);
        sliceB->setFmsqEnabled(true);
        sliceB->setSsqlEnabled(true);
        model.flushPendingSettingsSave();

        SliceModel reloaded(1);
        reloaded.restoreFromSettings(sliceB->band());
        QCOMPARE(reloaded.fmsqThresh(), -25.0);
        QVERIFY(reloaded.fmsqEnabled());
        QVERIFY(reloaded.ssqlEnabled());
        model.injectConnectionForTest(nullptr);
    }

    // The same for non-squelch per-slice edits (SNB, AGC hang and RIT),
    // made on slice B while A is the active slice.
    void otherDspEditOnInactiveSliceIsSavedToThatSlice()
    {
        RadioModel model;
        auto* mock = new MockConnection(&model);
        model.injectConnectionForTest(mock);
        const int a = model.addSlice();
        const int b = model.addSlice();
        model.setActiveSliceById(a);
        model.flushPendingSettingsSave();

        SliceModel* sliceB = model.sliceById(b);
        QVERIFY(sliceB != nullptr);
        const bool snb = !sliceB->snbEnabled();
        const int hang = sliceB->agcHang() + 37;
        const bool rit = !sliceB->ritEnabled();
        sliceB->setSnbEnabled(snb);
        sliceB->setAgcHang(hang);
        sliceB->setRitEnabled(rit);
        model.flushPendingSettingsSave();

        SliceModel reloaded(1);
        reloaded.restoreFromSettings(sliceB->band());
        QCOMPARE(reloaded.snbEnabled(), snb);
        QCOMPARE(reloaded.agcHang(), hang);
        QCOMPARE(reloaded.ritEnabled(), rit);
        model.injectConnectionForTest(nullptr);
    }
};

QTEST_MAIN(TestSliceSquelchChannelSeed)
#include "tst_slice_squelch_channel_seed.moc"
