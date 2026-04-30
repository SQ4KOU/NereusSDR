// =================================================================
// tests/tst_audio_engine_tx_monitor_state.cpp  (NereusSDR)
// =================================================================
//
// Exercises AudioEngine's TX monitor (MON) enable + volume state API
// added in Phase 3M-1b E.2.
//
// Coverage:
//   - Default values (enabled=false, volume=0.5f).
//   - Round-trip set/get for enable and volume.
//   - Signal emission on state change.
//   - Idempotent: no signal when value is unchanged.
//   - Volume clamping: below-zero and above-one inputs are clamped.
//   - Signal payload carries the clamped value (not the raw input).
//
// No bus injection needed — this suite exercises the state-management
// surface only; the siphon mix logic lives in E.3.
//
// Plan: 3M-1b E.2. Pre-code review §4.4.
// =================================================================

#include <QtTest/QtTest>
#include <QSignalSpy>

#include "core/AudioEngine.h"

using namespace NereusSDR;

class TstAudioEngineTxMonitorState : public QObject {
    Q_OBJECT

private slots:

    // ── 1. Defaults ────────────────────────────────────────────────────────

    void txMonitorEnabled_default_isFalse()
    {
        AudioEngine engine;
        QCOMPARE(engine.txMonitorEnabled(), false);
    }

    void txMonitorVolume_default_isPointFive()
    {
        AudioEngine engine;
        // Default mirrors Thetis audio.cs aaudio mix coefficient of 0.5f
        // (pre-code review §4.4). AudioEngine is NereusSDR-native; not a port.
        QCOMPARE(engine.txMonitorVolume(), 0.5f);
    }

    // ── 2. Round-trip: enable ──────────────────────────────────────────────

    void setTxMonitorEnabled_true_round_trip()
    {
        AudioEngine engine;
        engine.setTxMonitorEnabled(true);
        QCOMPARE(engine.txMonitorEnabled(), true);
    }

    void setTxMonitorEnabled_false_round_trip()
    {
        AudioEngine engine;
        engine.setTxMonitorEnabled(true);
        engine.setTxMonitorEnabled(false);
        QCOMPARE(engine.txMonitorEnabled(), false);
    }

    // ── 3. Signal: enable ──────────────────────────────────────────────────

    void setTxMonitorEnabled_signal_emits()
    {
        AudioEngine engine;
        QSignalSpy spy(&engine, &AudioEngine::txMonitorEnabledChanged);

        engine.setTxMonitorEnabled(true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toBool(), true);
    }

    void setTxMonitorEnabled_idempotent_noSignal()
    {
        AudioEngine engine;
        engine.setTxMonitorEnabled(false);  // already false — no change

        QSignalSpy spy(&engine, &AudioEngine::txMonitorEnabledChanged);
        engine.setTxMonitorEnabled(false);
        QCOMPARE(spy.count(), 0);
    }

    // ── 4. Round-trip: volume ─────────────────────────────────────────────

    void setTxMonitorVolume_validValue_storesIt()
    {
        AudioEngine engine;
        engine.setTxMonitorVolume(0.75f);
        QCOMPARE(engine.txMonitorVolume(), 0.75f);
    }

    // ── 5. Clamping ────────────────────────────────────────────────────────

    void setTxMonitorVolume_clamp_belowZero()
    {
        AudioEngine engine;
        engine.setTxMonitorVolume(-0.1f);
        QCOMPARE(engine.txMonitorVolume(), 0.0f);
    }

    void setTxMonitorVolume_clamp_aboveOne()
    {
        AudioEngine engine;
        engine.setTxMonitorVolume(1.5f);
        QCOMPARE(engine.txMonitorVolume(), 1.0f);
    }

    // ── 6. Signal: volume ──────────────────────────────────────────────────

    void setTxMonitorVolume_signal_emits()
    {
        AudioEngine engine;
        QSignalSpy spy(&engine, &AudioEngine::txMonitorVolumeChanged);

        engine.setTxMonitorVolume(0.3f);
        QCOMPARE(spy.count(), 1);
    }

    void setTxMonitorVolume_signalCarriesClampedValue()
    {
        AudioEngine engine;
        QSignalSpy spy(&engine, &AudioEngine::txMonitorVolumeChanged);

        // Raw value exceeds 1.0; signal payload should carry the clamped 1.0f.
        engine.setTxMonitorVolume(2.0f);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toFloat(), 1.0f);
    }

    void setTxMonitorVolume_idempotent_atValue_noSignal()
    {
        AudioEngine engine;
        engine.setTxMonitorVolume(0.25f);

        QSignalSpy spy(&engine, &AudioEngine::txMonitorVolumeChanged);
        engine.setTxMonitorVolume(0.25f);  // same value — no change
        QCOMPARE(spy.count(), 0);
    }
};

QTEST_MAIN(TstAudioEngineTxMonitorState)
#include "tst_audio_engine_tx_monitor_state.moc"
