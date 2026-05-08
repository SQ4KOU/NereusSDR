// NereusSDR-original test — no Thetis source ported here.
// No upstream attribution required (NereusSDR per-mode live-apply test).
//
// Tests for Task 4.2: per-mode buffer/filter/filter-type live-apply via
// RxChannel::onModeChanged() and TxChannel::onModeChanged().
//
// Design Section 4B:
//   - Changing a combo whose mode matches the slice's current mode triggers
//     RxChannel::onModeChanged() → rebuild() → dspChangeMeasured signal.
//   - Changing a combo for a different mode only persists to AppSettings;
//     no rebuild is triggered (applies on next mode-switch).
//   - Mode-switch triggers onModeChanged() which reads AppSettings for the
//     new mode and rebuilds if any value differs.
//   - Same-mode-same-settings → no rebuild (idempotent guard).
//
// The tests here work without a live WDSP session (no radio connected).
// The guard paths (m_wdspEngine == nullptr, channel not in map) return 0 or -1
// and are the values we test in the unconnected / no-engine scenarios.
//
// Test structure:
//   Part A — RxChannel::onModeChanged() unit tests (no WDSP).
//   Part B — TxChannel::onModeChanged() unit tests (no WDSP).
//   Part C — RadioModel::rebuildDspOptionsForMode() guard-path tests.

#include <QtTest/QtTest>
#include <QSignalSpy>

#include "core/AppSettings.h"
#include "core/RxChannel.h"
#include "core/TxChannel.h"
#include "core/WdspEngine.h"
#include "core/dsp/ChannelConfig.h"
#include "models/RadioModel.h"

using namespace NereusSDR;

static constexpr int kTestChannel  = 97;   // Never opened in WDSP
static constexpr int kTestBufSize  = 64;
static constexpr int kTestRate     = 48000;

// ── Helpers to set/clear per-mode AppSettings keys ────────────────────────────

namespace {

void setAppSettingsDefault()
{
    auto& s = AppSettings::instance();
    // Reset all DspOptions keys to known defaults so tests are hermetic.
    s.setValue("DspOptionsBufferSizePhone", "256");
    s.setValue("DspOptionsBufferSizeCw",    "256");
    s.setValue("DspOptionsBufferSizeDig",   "256");
    s.setValue("DspOptionsBufferSizeFm",    "256");

    s.setValue("DspOptionsFilterSizePhone", "4096");
    s.setValue("DspOptionsFilterSizeCw",    "4096");
    s.setValue("DspOptionsFilterSizeDig",   "4096");
    s.setValue("DspOptionsFilterSizeFm",    "4096");

    s.setValue("DspOptionsFilterTypePhoneRx", "Low Latency");
    s.setValue("DspOptionsFilterTypePhoneTx", "Linear Phase");
    s.setValue("DspOptionsFilterTypeCwRx",    "Low Latency");
    s.setValue("DspOptionsFilterTypeDigRx",   "Linear Phase");
    s.setValue("DspOptionsFilterTypeDigTx",   "Linear Phase");
    s.setValue("DspOptionsFilterTypeFmRx",    "Low Latency");
    s.setValue("DspOptionsFilterTypeFmTx",    "Linear Phase");

    s.setValue("DspOptionsCacheImpulse",                 "False");
    s.setValue("DspOptionsCacheImpulseSaveRestore",      "False");
    s.setValue("DspOptionsHighResFilterCharacteristics", "False");
}

}  // namespace

// ── Test class ────────────────────────────────────────────────────────────────

class TestDspOptionsPerModeApply : public QObject {
    Q_OBJECT

private slots:

    void init()
    {
        // Reset AppSettings keys to defaults before each test.
        setAppSettingsDefault();
    }

    // ── Part A: RxChannel::onModeChanged() ───────────────────────────────────

    // Without a WdspEngine attached, onModeChanged() must return 0 immediately.
    void rx_no_engine_returns_zero()
    {
        RxChannel ch(kTestChannel, kTestBufSize, kTestRate);
        // m_wdspEngine is null by default.
        const qint64 r = ch.onModeChanged(DSPMode::USB);
        QCOMPARE(r, qint64(0));
    }

    // With a WdspEngine attached but the channel not in the engine's map,
    // rebuild() returns -1 (channel not found). onModeChanged propagates -1.
    // Pre-condition: AppSettings buffer != RxChannel default (256 != 64) so
    // the equality guard does NOT skip the rebuild call.
    void rx_engine_attached_channel_not_in_map_returns_minus_one()
    {
        // RxChannel default m_bufferSize = kTestBufSize = 64.
        // AppSettings default for Phone = 256. 256 != 64 → no early-return.
        RxChannel ch(kTestChannel, kTestBufSize, kTestRate);

        WdspEngine engine;  // uninitialized — kTestChannel never created
        ch.setWdspEngine(&engine);

        const qint64 r = ch.onModeChanged(DSPMode::USB);
        // Channel not in engine map → rebuildRxChannel returns -1.
        QCOMPARE(r, qint64(-1));
    }

    // If the per-mode AppSettings values exactly match the current channel
    // config, onModeChanged() must return 0 (no rebuild).
    void rx_same_settings_returns_zero_no_rebuild()
    {
        // kTestBufSize = 64. Set AppSettings to 64 as well so equality guard fires.
        AppSettings::instance().setValue("DspOptionsBufferSizePhone", "64");
        // filterSize default = 4096 (matches ChannelConfig default + m_filterSize init).
        AppSettings::instance().setValue("DspOptionsFilterSizePhone", "4096");
        // filterType: RxChannel m_filterType=0 (LowLatency); "Low Latency" maps to 0.
        AppSettings::instance().setValue("DspOptionsFilterTypePhoneRx", "Low Latency");

        RxChannel ch(kTestChannel, kTestBufSize /* 64 */, kTestRate);

        WdspEngine engine;
        ch.setWdspEngine(&engine);

        const qint64 r = ch.onModeChanged(DSPMode::USB);
        // bufferSize 64==64, filterSize 4096==4096, filterType 0==0 → no rebuild.
        QCOMPARE(r, qint64(0));
    }

    // CW mode uses the "Cw" key suffix. Verify the key-part routing.
    void rx_cw_mode_reads_cw_key_suffix()
    {
        // Set CW buffer to 64 (matches kTestBufSize), filterSize and type match defaults.
        AppSettings::instance().setValue("DspOptionsBufferSizeCw",    "64");
        AppSettings::instance().setValue("DspOptionsFilterSizeCw",    "4096");
        AppSettings::instance().setValue("DspOptionsFilterTypeCwRx",  "Low Latency");

        RxChannel ch(kTestChannel, kTestBufSize /* 64 */, kTestRate);
        WdspEngine engine;
        ch.setWdspEngine(&engine);

        // All values match → no rebuild → 0.
        QCOMPARE(ch.onModeChanged(DSPMode::CWU), qint64(0));
        QCOMPARE(ch.onModeChanged(DSPMode::CWL), qint64(0));
    }

    // Dig mode uses "Dig" suffix.
    void rx_dig_mode_reads_dig_key_suffix()
    {
        AppSettings::instance().setValue("DspOptionsBufferSizeDig",   "64");
        AppSettings::instance().setValue("DspOptionsFilterSizeDig",   "4096");
        AppSettings::instance().setValue("DspOptionsFilterTypeDigRx", "Low Latency");  // 0 = LowLatency

        RxChannel ch(kTestChannel, kTestBufSize, kTestRate);
        WdspEngine engine;
        ch.setWdspEngine(&engine);

        QCOMPARE(ch.onModeChanged(DSPMode::DIGU), qint64(0));
        QCOMPARE(ch.onModeChanged(DSPMode::DIGL), qint64(0));
    }

    // FM mode uses "Fm" suffix.
    void rx_fm_mode_reads_fm_key_suffix()
    {
        AppSettings::instance().setValue("DspOptionsBufferSizeFm",    "64");
        AppSettings::instance().setValue("DspOptionsFilterSizeFm",    "4096");
        AppSettings::instance().setValue("DspOptionsFilterTypeFmRx",  "Low Latency");

        RxChannel ch(kTestChannel, kTestBufSize, kTestRate);
        WdspEngine engine;
        ch.setWdspEngine(&engine);

        QCOMPARE(ch.onModeChanged(DSPMode::FM), qint64(0));
    }

    // Changing AppSettings phone filter size to a different value causes
    // onModeChanged to attempt rebuild (returns -1 since channel not in map).
    void rx_changed_filter_size_triggers_rebuild_attempt()
    {
        // 8192 != 4096 (m_filterSize default) → rebuild is attempted.
        AppSettings::instance().setValue("DspOptionsFilterSizePhone", "8192");
        // Keep bufferSize matching kTestBufSize so only filterSize differs.
        AppSettings::instance().setValue("DspOptionsBufferSizePhone", "64");
        AppSettings::instance().setValue("DspOptionsFilterTypePhoneRx", "Low Latency");

        RxChannel ch(kTestChannel, kTestBufSize, kTestRate);
        WdspEngine engine;
        ch.setWdspEngine(&engine);

        // filterSize 8192 != 4096 → rebuild attempted → -1 (not in map).
        QCOMPARE(ch.onModeChanged(DSPMode::USB), qint64(-1));
    }

    // Changing filter type from Low Latency (0) to Linear Phase (1)
    // triggers a rebuild attempt.
    void rx_changed_filter_type_triggers_rebuild_attempt()
    {
        AppSettings::instance().setValue("DspOptionsBufferSizePhone",   "64");
        AppSettings::instance().setValue("DspOptionsFilterSizePhone",   "4096");
        // "Linear Phase" maps to filterType=1; m_filterType default is 0.
        AppSettings::instance().setValue("DspOptionsFilterTypePhoneRx", "Linear Phase");

        RxChannel ch(kTestChannel, kTestBufSize, kTestRate);
        WdspEngine engine;
        ch.setWdspEngine(&engine);

        // filterType 1 != 0 → rebuild attempted → -1.
        QCOMPARE(ch.onModeChanged(DSPMode::USB), qint64(-1));
    }

    // ── Part B: TxChannel::onModeChanged() ───────────────────────────────────

    // Without a WdspEngine attached, TxChannel::onModeChanged() returns 0.
    void tx_no_engine_returns_zero()
    {
        // TxChannel constructor: (channelId, inputBufferSize, outputBufferSize, parent)
        TxChannel tx(97, 64, 64);
        const qint64 r = tx.onModeChanged(DSPMode::USB);
        QCOMPARE(r, qint64(0));
    }

    // TxChannel: filter size matching kTxDspBufferSize (2048) + Low Latency (0)
    // → no rebuild (idempotent guard fires).
    // m_txFilterSize initialises to WdspEngine::kTxDspBufferSize = 2048.
    // m_txFilterType initialises to 0 (LowLatency). "Low Latency" maps to 0.
    void tx_same_settings_returns_zero()
    {
        AppSettings::instance().setValue("DspOptionsFilterSizePhone",   "2048");
        // "Low Latency" → filterType=0 == m_txFilterType default 0 → no rebuild.
        AppSettings::instance().setValue("DspOptionsFilterTypePhoneTx", "Low Latency");

        TxChannel tx(97, 64, 64);
        WdspEngine engine;
        tx.setWdspEngine(&engine);

        // filterSize 2048 == 2048, filterType 0 == 0 → no rebuild → 0.
        QCOMPARE(tx.onModeChanged(DSPMode::USB), qint64(0));
    }

    // TxChannel: changed filter size → rebuild attempt → -1 (channel not in map).
    void tx_changed_filter_size_triggers_rebuild_attempt()
    {
        // 4096 != kTxDspBufferSize (2048) → rebuild attempted.
        AppSettings::instance().setValue("DspOptionsFilterSizePhone",   "4096");
        AppSettings::instance().setValue("DspOptionsFilterTypePhoneTx", "Low Latency");

        TxChannel tx(97, 64, 64);
        WdspEngine engine;
        tx.setWdspEngine(&engine);

        QCOMPARE(tx.onModeChanged(DSPMode::USB), qint64(-1));
    }

    // ── Part C: RadioModel::rebuildDspOptionsForMode() guard paths ────────────

    // rebuildDspOptionsForMode() must be a no-op (no crash, no emission)
    // when WDSP is not initialized (radio disconnected).
    void radiomodel_rebuild_noop_when_disconnected()
    {
        RadioModel model;
        QSignalSpy spy(&model, &RadioModel::dspChangeMeasured);

        // Not connected → m_wdspEngine not initialized.
        model.rebuildDspOptionsForMode(DSPMode::USB);

        // No emission expected when unconnected.
        QCOMPARE(spy.count(), 0);
    }
};

QTEST_MAIN(TestDspOptionsPerModeApply)
#include "tst_dsp_options_per_mode_apply.moc"
