// no-port-check: NereusSDR-original test — no Thetis source ported here.
// Comments below cite Thetis source paths for context only.
//
// Every receive channel takes the DSP > Options buffer size of its slice's
// mode before it runs a block, and again whenever that slice's mode changes.
// The filter size and filter type of that mode go with it.
//
// WDSP opens each receive channel with a DSP buffer of 4096 samples at
// 48,000 (cmaster.c:72-86 [v2.10.3.15]), 85 ms of signal per block. Thetis
// forces the configured size into every receiver once the channels exist
// (console.cs:2681-2692 SyncDSP) and sets each receiver from its own mode
// after that (console.cs:38975-38984).
//
// The values a receiver gets with nothing saved are NereusSDR's own: a block
// of 64 and Low Latency. Thetis's receiver objects start there too
// (radio.cs:521-522, 559-560), but its Setup form then starts every mode at
// 1024 and Linear Phase and sends those to the receivers (setup.cs:304-326,
// console.cs:39273-39281 [v2.10.3.15]), so a stock Thetis receiver does not
// run them.
//
// NereusSDR applied the sizes only when a slice's mode changed, and only to
// the first receiver: the others never had the engine handle the apply
// needs. On the Rock 5C on 2026-10-10 every receiver ran 85 ms blocks from
// the Core's start, and the first took its 64 only when its mode was next
// changed (the Core's log has one "bufSize: 4096 -> 64" line for the day).
//
// The DSP > Options page shows those same starting values. With nothing
// saved, each combo on it reads what a receiver or the transmitter in that
// mode group starts with, and a saved value wins on the page and in both
// channels. The FM row showed Thetis's 256 and 128 (console.cs:39149, 39160
// [v2.10.3.15]) while both channels ran 64.
//
// Real channels: openRxChannelPool runs createRxChannel's OpenChannel, as in
// tst_notch_channel_sync; the transmit channel is opened as in
// tst_tx_latency_dsp.

#include <QtTest/QtTest>

#include <QComboBox>

#include "core/AppSettings.h"
#include "core/DspControlThread.h"
#include "core/RxChannel.h"
#include "core/SampleRateCatalog.h"
#include "core/TxChannel.h"
#include "core/WdspEngine.h"
#include "gui/setup/DspOptionsPage.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "fakes/ConnectableRadioModel.h"

#ifdef HAVE_WDSP
// What a receiver runs is read off WDSP's own channel, not off RxChannel's
// record of what it asked for.
extern "C" {
#include "../third_party/wdsp/src/comm.h"
}
#undef min
#undef max
#endif

using namespace NereusSDR;
using NereusSDR::Test::ConnectableRadioModel;

namespace {

// The DSP block size WDSP's channel runs (ch[].dsp_size, which
// SetDSPBuffsize sets, third_party/wdsp/src/channel.c), or -1 when there is
// no channel.
int wdspBlockSize(const RxChannel* rx)
{
#ifdef HAVE_WDSP
    if (rx == nullptr || rx->channelId() < 0 || rx->channelId() >= MAX_CHANNELS) {
        return -1;
    }
    return ch[rx->channelId()].dsp_size;
#else
    Q_UNUSED(rx);
    return -1;
#endif
}

// The filter size WDSP's receive bandpass runs (rxa[].nbp0.p->nc, which
// RXASetNC sets through RXANBPSetNC, third_party/wdsp/src/RXA.c), or -1.
int wdspFilterSize(const RxChannel* rx)
{
#ifdef HAVE_WDSP
    if (rx == nullptr || rx->channelId() < 0 || rx->channelId() >= MAX_CHANNELS
        || rxa[rx->channelId()].nbp0.p == nullptr) {
        return -1;
    }
    return rxa[rx->channelId()].nbp0.p->nc;
#else
    Q_UNUSED(rx);
    return -1;
#endif
}

// True when WDSP's receive bandpass is linear phase: its minimum-phase flag
// (rxa[].nbp0.p->mp, RxChannel::bandpassMinimumPhaseForTest) is 0. Low
// Latency is 1.
bool wdspLinearPhase(const RxChannel* rx)
{
    return rx != nullptr && rx->bandpassMinimumPhaseForTest() == 0;
}

constexpr int kRateHz = 192000;
constexpr int kOpenedBlock = 4096;   // createRxChannel's dsp_size
constexpr int kPhoneBlock = 256;
constexpr int kCwBlock = 512;

bool laneIdle(RadioModel& model)
{
    return model.receiveLane() == nullptr || model.receiveLane()->waitIdleForTest(600000);
}

void saveBlockSizes()
{
    auto& s = AppSettings::instance();
    s.setValue(QStringLiteral("DspOptionsBufferSizePhoneRx"), QString::number(kPhoneBlock));
    s.setValue(QStringLiteral("DspOptionsBufferSizeCwRx"), QString::number(kCwBlock));
}

// A mode group on the DSP > Options page and a mode that belongs to it.
struct ModeGroup {
    const char* key;
    DSPMode mode;
};
constexpr ModeGroup kRxGroups[] = {
    {"Phone", DSPMode::USB},
    {"Fm", DSPMode::FM},
    {"Cw", DSPMode::CWU},
    {"Dig", DSPMode::DIGU},
};
// CW has no transmit row on the page.
constexpr ModeGroup kTxGroups[] = {
    {"Phone", DSPMode::USB},
    {"Fm", DSPMode::FM},
    {"Dig", DSPMode::DIGU},
};

// What the page's combo for a setting shows; empty when there is none.
QString shownOnPage(const DspOptionsPage& page, const char* setting, const ModeGroup& group,
                    const char* side)
{
    const QString name = QLatin1String(setting) + QLatin1String(group.key) + QLatin1String(side);
    const auto* box = page.findChild<QComboBox*>(name);
    return box != nullptr ? box->currentText() : QString();
}

// The transmit channel as the Core opens it (WdspEngine::kTxDspBufferSize),
// before any mode has been applied to it. On an engine of its own, which has
// no transmit lane, so the channel is open when this returns; a RadioModel's
// engine opens it on the lane.
TxChannel* openTxChannel(WdspEngine* engine)
{
    TxChannel* tx = engine->createTxChannel(WdspEngine::kTxChannelId, 64,
                                            WdspEngine::kTxDspBufferSize, 48000,
                                            WdspEngine::kTxDspSampleRate, 192000);
    if (tx != nullptr) {
        tx->setWdspEngine(engine);
    }
    return tx;
}

} // namespace

class TestDspOptionsFirstApply : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { AppSettings::instance().clear(); }
    void init()         { AppSettings::instance().clear(); }
    void cleanup()      { AppSettings::instance().clear(); }

    // Slices whose modes were set before any channel existed (a Core
    // restoring its slices, then connecting): each channel starts with the
    // block size of its own slice's mode.
    void each_receiver_starts_with_its_modes_block_size()
    {
        saveBlockSizes();
        RadioModel model;
        WdspEngine* engine = model.wdspEngine();
        engine->m_initialized = true;   // friend access (NEREUS_BUILD_TESTS)

        model.configureStreamPool(5, 5, kRateHz);
        const int a = model.addSlice();
        model.sliceById(a)->setDspMode(DSPMode::USB);
        const int b = model.addSlice();
        model.sliceById(b)->setDspMode(DSPMode::CWU);
        model.openRxChannelPool(5, bufferSizeForRate(kRateHz), kRateHz);
        QVERIFY(laneIdle(model));

        QVERIFY(engine->rxChannel(a) != nullptr);
        QVERIFY(engine->rxChannel(b) != nullptr);
        QCOMPARE(wdspBlockSize(engine->rxChannel(a)), kPhoneBlock);
        QCOMPARE(wdspBlockSize(engine->rxChannel(b)), kCwBlock);
    }

    // With nothing saved, NereusSDR's default of 64 (RxChannel::onModeChanged),
    // not the 4096 it was opened with.
    void a_receiver_starts_with_the_default_block_size()
    {
        RadioModel model;
        WdspEngine* engine = model.wdspEngine();
        engine->m_initialized = true;

        model.configureStreamPool(5, 5, kRateHz);
        const int a = model.addSlice();
        const int b = model.addSlice();
        model.openRxChannelPool(5, bufferSizeForRate(kRateHz), kRateHz);
        QVERIFY(laneIdle(model));

        QCOMPARE(wdspBlockSize(engine->rxChannel(a)), 64);
        QCOMPARE(wdspBlockSize(engine->rxChannel(b)), 64);
    }

    // A slice added once the pool is open binds to a channel that has been
    // sitting open at 4096.
    void a_slice_added_later_starts_with_its_modes_block_size()
    {
        saveBlockSizes();
        RadioModel model;
        WdspEngine* engine = model.wdspEngine();
        engine->m_initialized = true;

        model.configureStreamPool(5, 5, kRateHz);
        const int a = model.addSlice();
        model.sliceById(a)->setDspMode(DSPMode::USB);
        model.openRxChannelPool(5, bufferSizeForRate(kRateHz), kRateHz);
        QVERIFY(laneIdle(model));

        const int b = model.addSlice();
        QVERIFY(model.sliceById(b)->streamIndex() >= 0);
        QVERIFY(laneIdle(model));
        QVERIFY(engine->rxChannel(b) != nullptr);
        const int expected =
            dspOptionsModeGroup(model.sliceById(b)->dspMode()) == QLatin1String("Cw")
                ? kCwBlock : kPhoneBlock;
        QCOMPARE(wdspBlockSize(engine->rxChannel(b)), expected);
    }

    // A channel no slice uses is left as it was opened.
    void a_channel_without_a_slice_keeps_the_size_it_was_opened_with()
    {
        saveBlockSizes();
        RadioModel model;
        WdspEngine* engine = model.wdspEngine();
        engine->m_initialized = true;

        model.configureStreamPool(5, 5, kRateHz);
        const int a = model.addSlice();
        model.openRxChannelPool(5, bufferSizeForRate(kRateHz), kRateHz);
        QVERIFY(laneIdle(model));

        QVERIFY(engine->rxChannel(a) != nullptr);
        QVERIFY(engine->rxChannel(4) != nullptr);
        QCOMPARE(wdspBlockSize(engine->rxChannel(4)), kOpenedBlock);
    }

    // The filter type goes with the block size. WDSP opens a channel linear
    // phase (RXA.c create_nbp, mp 0); with nothing saved a receiver starts
    // Low Latency, NereusSDR's default (RxChannel::onModeChanged). Before
    // this the first receiver took it at its first mode change and the
    // others never did.
    void a_receiver_starts_with_the_default_filter_type()
    {
        RadioModel model;
        WdspEngine* engine = model.wdspEngine();
        engine->m_initialized = true;

        model.configureStreamPool(5, 5, kRateHz);
        const int a = model.addSlice();
        const int b = model.addSlice();
        model.openRxChannelPool(5, bufferSizeForRate(kRateHz), kRateHz);
        QVERIFY(laneIdle(model));

        QVERIFY(!wdspLinearPhase(engine->rxChannel(a)));
        QVERIFY(!wdspLinearPhase(engine->rxChannel(b)));
        // A channel no slice uses is left as it was opened.
        QVERIFY(wdspLinearPhase(engine->rxChannel(4)));
    }

    // Each receiver takes the type saved for its own slice's mode.
    void each_receiver_starts_with_its_modes_filter_type()
    {
        auto& s = AppSettings::instance();
        s.setValue(QStringLiteral("DspOptionsFilterTypeCwRx"), QStringLiteral("Linear Phase"));
        RadioModel model;
        WdspEngine* engine = model.wdspEngine();
        engine->m_initialized = true;

        model.configureStreamPool(5, 5, kRateHz);
        const int a = model.addSlice();
        model.sliceById(a)->setDspMode(DSPMode::USB);
        const int b = model.addSlice();
        model.sliceById(b)->setDspMode(DSPMode::CWU);
        model.openRxChannelPool(5, bufferSizeForRate(kRateHz), kRateHz);
        QVERIFY(laneIdle(model));

        QVERIFY(!wdspLinearPhase(engine->rxChannel(a)));
        QVERIFY(wdspLinearPhase(engine->rxChannel(b)));
    }

    // A mode change reaches a receiver other than the first. The slice's
    // mode handler (RadioModel::wireSliceSignals, wired only with a
    // connection) calls the channel's onModeChanged; a channel without the
    // engine handle returned from it having applied nothing.
    void a_mode_change_reaches_every_receiver()
    {
        saveBlockSizes();
        RadioModel model;
        WdspEngine* engine = model.wdspEngine();
        engine->m_initialized = true;

        model.configureStreamPool(5, 5, kRateHz);
        const int a = model.addSlice();
        model.sliceById(a)->setDspMode(DSPMode::USB);
        const int b = model.addSlice();
        model.sliceById(b)->setDspMode(DSPMode::USB);
        model.openRxChannelPool(5, bufferSizeForRate(kRateHz), kRateHz);
        QVERIFY(laneIdle(model));
        QCOMPARE(wdspBlockSize(engine->rxChannel(b)), kPhoneBlock);

        engine->rxChannel(b)->onModeChanged(DSPMode::CWU);
        QVERIFY(laneIdle(model));
        QCOMPARE(wdspBlockSize(engine->rxChannel(b)), kCwBlock);
        QCOMPARE(wdspBlockSize(engine->rxChannel(a)), kPhoneBlock);

        engine->rxChannel(b)->onModeChanged(DSPMode::USB);
        QVERIFY(laneIdle(model));
        QCOMPARE(wdspBlockSize(engine->rxChannel(b)), kPhoneBlock);
    }

    // A size picked on the local DSP > Options page (DspOptionsPage's combo
    // handler calls rebuildDspOptionsForMode with the combo's mode) reaches
    // every receiver whose slice is in that group, whatever mode the first
    // slice is in, and no receiver in another group.
    void a_setup_page_change_reaches_every_receiver_in_its_group()
    {
        saveBlockSizes();
        RadioModel model;
        WdspEngine* engine = model.wdspEngine();
        engine->m_initialized = true;

        model.configureStreamPool(5, 5, kRateHz);
        const int a = model.addSlice();
        model.sliceById(a)->setDspMode(DSPMode::USB);
        const int b = model.addSlice();
        model.sliceById(b)->setDspMode(DSPMode::LSB);
        const int c = model.addSlice();
        model.sliceById(c)->setDspMode(DSPMode::CWU);
        model.openRxChannelPool(5, bufferSizeForRate(kRateHz), kRateHz);
        QVERIFY(laneIdle(model));
        QCOMPARE(wdspBlockSize(engine->rxChannel(b)), kPhoneBlock);
        QCOMPARE(wdspBlockSize(engine->rxChannel(c)), kCwBlock);

        auto& s = AppSettings::instance();
        s.setValue(QStringLiteral("DspOptionsBufferSizePhoneRx"), QStringLiteral("128"));
        model.rebuildDspOptionsForMode(DSPMode::USB);
        QVERIFY(laneIdle(model));
        QCOMPARE(wdspBlockSize(engine->rxChannel(a)), 128);
        QCOMPARE(wdspBlockSize(engine->rxChannel(b)), 128);
        QCOMPARE(wdspBlockSize(engine->rxChannel(c)), kCwBlock);

        // The CW size, with the first slice in a Phone mode.
        s.setValue(QStringLiteral("DspOptionsBufferSizeCwRx"), QStringLiteral("1024"));
        model.rebuildDspOptionsForMode(DSPMode::CWU);
        QVERIFY(laneIdle(model));
        QCOMPARE(wdspBlockSize(engine->rxChannel(c)), 1024);
        QCOMPARE(wdspBlockSize(engine->rxChannel(a)), 128);
        QCOMPARE(wdspBlockSize(engine->rxChannel(b)), 128);
    }

    // With nothing saved, the page shows for every mode group the block size
    // and filter type a receiver in that group starts with. Building the page
    // saves nothing, so what it shows is a starting value and not a saved one.
    void the_page_shows_what_a_receiver_starts_with_when_nothing_is_saved()
    {
        RadioModel model;
        WdspEngine* engine = model.wdspEngine();
        engine->m_initialized = true;
        const DspOptionsPage page(&model);

        model.configureStreamPool(5, 5, kRateHz);
        QList<int> ids;
        for (const ModeGroup& group : kRxGroups) {
            const int id = model.addSlice();
            QVERIFY(id >= 0);
            model.sliceById(id)->setDspMode(group.mode);
            ids.append(id);
        }
        model.openRxChannelPool(5, bufferSizeForRate(kRateHz), kRateHz);
        QVERIFY(laneIdle(model));

        for (qsizetype i = 0; i < ids.size(); ++i) {
            const ModeGroup& group = kRxGroups[i];
            const RxChannel* rx = engine->rxChannel(ids[i]);
            QVERIFY(rx != nullptr);
            const QString size = shownOnPage(page, "DspOptionsBufferSize", group, "Rx");
            QVERIFY2(size.toInt() == wdspBlockSize(rx),
                     qPrintable(QStringLiteral("%1 receive: the page shows a block of \"%2\", "
                                               "the receiver runs %3")
                                    .arg(QLatin1String(group.key), size)
                                    .arg(wdspBlockSize(rx))));
            const QString type = shownOnPage(page, "DspOptionsFilterType", group, "Rx");
            QVERIFY2(!type.isEmpty()
                         && (type == QStringLiteral("Linear Phase")) == wdspLinearPhase(rx),
                     qPrintable(QStringLiteral("%1 receive: the page shows \"%2\", the receiver "
                                               "runs %3")
                                    .arg(QLatin1String(group.key), type,
                                         wdspLinearPhase(rx)
                                             ? QStringLiteral("Linear Phase")
                                             : QStringLiteral("Low Latency"))));
            QVERIFY(!AppSettings::instance().contains(
                QStringLiteral("DspOptionsBufferSize") + QLatin1String(group.key)
                + QStringLiteral("Rx")));
        }
    }

    // The same for the transmitter: each group's block size and filter type
    // on the page are what the transmit channel takes when that group's mode
    // is applied to it, as it is before each key.
    void the_page_shows_what_the_transmitter_starts_with_when_nothing_is_saved()
    {
        RadioModel model;
        const DspOptionsPage page(&model);
        WdspEngine txEngine;
        txEngine.m_initialized = true;
        WdspEngine* engine = &txEngine;

        for (const ModeGroup& group : kTxGroups) {
            TxChannel* tx = openTxChannel(engine);
            QVERIFY(tx != nullptr);
            QCOMPARE(tx->txDspBlockSize(), WdspEngine::kTxDspBufferSize);
            tx->onModeChanged(group.mode);

            const QString size = shownOnPage(page, "DspOptionsBufferSize", group, "Tx");
            const int runs = tx->txDspBlockSize();
            const QString type = shownOnPage(page, "DspOptionsFilterType", group, "Tx");
            // WDSP's minimum-phase flag: 1 is Low Latency, 0 is Linear Phase.
            const int minimumPhase = tx->bandpassMinimumPhaseForTest();
            engine->destroyTxChannel(WdspEngine::kTxChannelId);

            QVERIFY2(size.toInt() == runs,
                     qPrintable(QStringLiteral("%1 transmit: the page shows a block of \"%2\", "
                                               "the transmitter runs %3")
                                    .arg(QLatin1String(group.key), size)
                                    .arg(runs)));
            QVERIFY2(!type.isEmpty()
                         && (type == QStringLiteral("Low Latency")) == (minimumPhase == 1),
                     qPrintable(QStringLiteral("%1 transmit: the page shows \"%2\", WDSP's "
                                               "minimum-phase flag is %3")
                                    .arg(QLatin1String(group.key), type)
                                    .arg(minimumPhase)));
            QVERIFY(!AppSettings::instance().contains(
                QStringLiteral("DspOptionsBufferSize") + QLatin1String(group.key)
                + QStringLiteral("Tx")));
        }
    }

    // A saved value wins over the starting value, on the page and in both
    // channels. The values saved here are Thetis's FM sizes (256 receive,
    // 128 transmit; console.cs:39149, 39160 [v2.10.3.15]), which the page
    // showed unsaved before and no channel ran.
    void a_saved_fm_block_size_is_shown_and_is_what_both_channels_run()
    {
        auto& s = AppSettings::instance();
        s.setValue(QStringLiteral("DspOptionsBufferSizeFmRx"), QStringLiteral("256"));
        s.setValue(QStringLiteral("DspOptionsBufferSizeFmTx"), QStringLiteral("128"));
        RadioModel model;
        WdspEngine* engine = model.wdspEngine();
        engine->m_initialized = true;
        const DspOptionsPage page(&model);
        const ModeGroup fm{"Fm", DSPMode::FM};
        QCOMPARE(shownOnPage(page, "DspOptionsBufferSize", fm, "Rx"), QStringLiteral("256"));
        QCOMPARE(shownOnPage(page, "DspOptionsBufferSize", fm, "Tx"), QStringLiteral("128"));

        model.configureStreamPool(5, 5, kRateHz);
        const int a = model.addSlice();
        model.sliceById(a)->setDspMode(DSPMode::FM);
        model.openRxChannelPool(5, bufferSizeForRate(kRateHz), kRateHz);
        QVERIFY(laneIdle(model));
        QVERIFY(engine->rxChannel(a) != nullptr);
        QCOMPARE(wdspBlockSize(engine->rxChannel(a)), 256);

        WdspEngine txEngine;
        txEngine.m_initialized = true;
        TxChannel* tx = openTxChannel(&txEngine);
        QVERIFY(tx != nullptr);
        tx->onModeChanged(DSPMode::FM);
        const int runs = tx->txDspBlockSize();
        txEngine.destroyTxChannel(WdspEngine::kTxChannelId);
        QCOMPARE(runs, 128);
    }

    // Slice A's receiver on a real connect. The connect path opens channel 0
    // at WDSP's 4096 block, 4096 coefficients and linear phase (createRxChannel;
    // RXA.c create_nbp), then seeds it from slice A, DSP > Options included.
    // The values saved here differ from all three, in every mode group, so
    // whatever mode slice A comes up in, a channel the seed missed shows it.
    void slice_a_takes_its_modes_sizes_and_type_on_connect()
    {
        constexpr int kSavedBlock = 512;
        constexpr int kSavedFilter = 1024;
        auto& s = AppSettings::instance();
        for (const ModeGroup& group : kRxGroups) {
            const QString suffix = QLatin1String(group.key) + QStringLiteral("Rx");
            s.setValue(QStringLiteral("DspOptionsBufferSize") + suffix,
                       QString::number(kSavedBlock));
            s.setValue(QStringLiteral("DspOptionsFilterSize") + suffix,
                       QString::number(kSavedFilter));
            s.setValue(QStringLiteral("DspOptionsFilterType") + suffix,
                       QStringLiteral("Low Latency"));
        }

        auto harness = ConnectableRadioModel::create();
        QVERIFY(harness);
        RadioModel& model = harness->model();
        QVERIFY(model.waitForReceiveLaneForTest());

        SliceModel* const sliceA = model.sliceById(0);
        QVERIFY(sliceA != nullptr);
        const RxChannel* rx = model.wdspEngine()->rxChannel(0);
        QVERIFY(rx != nullptr);
        QCOMPARE(wdspBlockSize(rx), kSavedBlock);
        QCOMPARE(wdspFilterSize(rx), kSavedFilter);
        QVERIFY(!wdspLinearPhase(rx));
    }
};

QTEST_MAIN(TestDspOptionsFirstApply)
#include "tst_dsp_options_first_apply.moc"
