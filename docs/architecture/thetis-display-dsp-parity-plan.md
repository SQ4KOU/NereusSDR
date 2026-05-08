# Thetis Display + DSP Parity — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Achieve Thetis v2.10.3.13 parity for `Display → RX1`, `Display → General`, and `DSP → Options` Setup pages, revisit handwave implementations from 3G-8, build WDSP channel teardown/rebuild infrastructure for live-apply, and add per-band noise-floor priming as a NereusSDR-original enhancement.

**Architecture:** Single PR organized into 6 sequential phases. Phase 1 (channel-rebuild infrastructure) lands first — Phases 2 and 4 depend on it. Phases 2/3/4 may interleave once Phase 1 is in. All ports source-cited at `[v2.10.3.13]`; AppSettings keys flat PascalCase for global, `<KeyName>_<bandKeyName>` for per-band; "no handwave" depth floor — every parity item gets full algorithm/render port, not just AppSettings persistence.

**Tech Stack:** C++20, Qt6 (Core/Widgets/Multimedia/Svg), QRhi (Metal/Vulkan/D3D12), CMake/Ninja, FFTW3, WDSP (TAPR v1.29 + linux_port.h), Qt Test framework (`QtTest/QtTest` with `tst_<name>.cpp` naming, `ctest --test-dir build -R <pattern>` to run).

**Reference design doc:** [`docs/architecture/thetis-display-dsp-parity-design.md`](./thetis-display-dsp-parity-design.md) (committed `b3a0246`). Read it first for full context.

**Thetis upstream pinned at:** `v2.10.3.13-7-g501e3f51` (captured at brainstorm session start). Every inline cite uses `[v2.10.3.13]`.

---

## File Structure

### New files

```
src/core/dsp/RxChannelState.h          - captured-state struct for rebuild
src/core/dsp/TxChannelState.h          - captured-state struct for TX rebuild
src/core/dsp/ChannelConfig.h           - rebuild input config struct
src/core/dsp/DspChangeTimer.h          - measures rebuild ms
src/core/dsp/DspChangeTimer.cpp
src/gui/spectrum/ActivePeakHoldTrace.h - per-bin decay trace (Section 2C)
src/gui/spectrum/ActivePeakHoldTrace.cpp
src/gui/spectrum/PeakBlobDetector.h    - top-N peak finder + state machine
src/gui/spectrum/PeakBlobDetector.cpp
src/gui/setup/SpectrumPeaksPage.h      - new Setup page (Section 2C)
src/gui/setup/SpectrumPeaksPage.cpp
src/gui/setup/MultimeterPage.h         - new Setup page (Section 3A)
src/gui/setup/MultimeterPage.cpp
src/gui/setup/DspOptionsPage.h         - new Setup page (Section 4)
src/gui/setup/DspOptionsPage.cpp
docs/architecture/phase3g8-verification/spectrum-defaults-extensions.md
docs/architecture/phase3g8-verification/spectrum-peaks.md
docs/architecture/phase3g8-verification/waterfall-defaults-changes.md
docs/architecture/phase3g8-verification/grid-scales-extensions.md
docs/architecture/phase3g8-verification/multimeter.md
docs/architecture/phase3g8-verification/dsp-options.md
docs/architecture/phase3g8-verification/infrastructure-live-apply.md
tests/tst_rx_channel_rebuild.cpp
tests/tst_tx_channel_rebuild.cpp
tests/tst_sample_rate_live_apply.cpp
tests/tst_active_rx_count_live_apply.cpp
tests/tst_dsp_change_timer.cpp
tests/tst_audio_engine_reinit.cpp
tests/tst_rebuild_glitch_budget.cpp
tests/tst_active_peak_hold.cpp
tests/tst_peak_blob_detector.cpp
tests/tst_nf_aware_grid.cpp
tests/tst_per_band_nf_priming.cpp
tests/tst_clarity_nf_grid_coexistence.cpp
tests/tst_detector_modes.cpp
tests/tst_averaging_modes.cpp
tests/tst_spectrum_overlays.cpp
tests/tst_multimeter_unit_conversion.cpp
tests/tst_multimeter_timing.cpp
tests/tst_signal_history.cpp
tests/tst_dsp_options_persistence.cpp
tests/tst_dsp_options_per_mode_apply.cpp
tests/tst_dsp_options_warning_logic.cpp
tests/tst_filter_high_resolution.cpp
tests/tst_settings_migration_v0_3_0.cpp
```

### Modified files

```
src/core/dsp/RxChannel.h/.cpp            - rebuild() + setters + state capture
src/core/dsp/TxChannel.h/.cpp            - rebuild() + setters
src/core/dsp/WdspEngine.h/.cpp           - destroy/recreate channel API
src/core/AudioEngine.h/.cpp              - re-init on buffer-size change
src/core/AppSettings.h/.cpp              - SettingsSchemaVersion + migration
src/core/protocol/P1RadioConnection.cpp  - sample-rate live-apply path
src/core/protocol/P2RadioConnection.cpp  - sample-rate live-apply path
src/core/protocol/MetisFrameParser.cpp   - mid-stream RX-count change support (verify)
src/core/ReceiverManager.cpp             - active-RX-count live-apply
src/dsp/NoiseFloorEstimator.h/.cpp       - prime(double) method for band priming
src/models/RadioModel.h/.cpp             - dspChangeMeasured signal
src/models/PanadapterModel.h/.cpp        - BandGridSettings.bandNFEstimate + persistence
src/gui/SpectrumWidget.h/.cpp            - new setters, new render passes, NF-aware grid
src/gui/setup/DisplaySetupPages.h/.cpp   - extensions to all 3 existing Display pages
src/gui/setup/AppearanceSetupPages.cpp   - VFO Flag group on MeterStylesPage
src/gui/setup/DiagnosticsSetupPages.cpp  - "Logging & Performance" rename + Performance group
src/gui/setup/GeneralOptionsPage.cpp     - CPU meter rate spinbox
src/gui/setup/hardware/RadioInfoTab.cpp  - ANAN-8000DLE volts/amps toggle
src/gui/SetupDialog.cpp                  - register 3 new pages + cross-link plumbing
src/gui/meters/MeterPoller.cpp           - configurable polling interval + averaging window
src/gui/meters/MeterItem.h/.cpp          - unit-mode awareness (S/dBm/µV)
src/gui/meters/BarItem.cpp               - unit-mode display
src/gui/meters/TextItem.cpp              - unit-mode + decimal flag
src/gui/meters/SignalTextItem.cpp        - unit-mode + decimal flag
src/gui/meters/NeedleItem.cpp            - unit-mode display
src/gui/meters/HistoryGraphItem.cpp      - duration setting
src/gui/meters/FilterDisplayItem.cpp     - high-res mode (Section 4D)
src/core/FFTEngine.cpp                   - decimation hook (Section 2B)
src/models/SliceModel.cpp                - modeChanged → DspOptions fan-out
docs/attribution/THETIS-PROVENANCE.md    - new rows for ported files
CHANGELOG.md                             - v0.3.0 entry
docs/architecture/phase3g8-verification/README.md - extend with new matrix files
```

### Cite conventions reminder

Every line of ported code: `// From Thetis <file>.cs:<line> [v2.10.3.13]`.
NereusSDR-original additions: `// NereusSDR-original — no Thetis equivalent.`
RX1-scope-dropped ports: footer noting per-pan-override path (see design §1B).
No source cites in user-visible strings — tooltips, status, button labels stay plain English (per `feedback_no_cites_in_user_strings`).

---

## Phase 1 — Channel rebuild infrastructure (Cluster 1)

**Must land first.** Phases 2 and 4 depend on the public API surface shipped here. Phase 3 is independent and may interleave.

### Task 1.1: Define `ChannelConfig` and `RxChannelState` structs

**Files:**
- Create: `src/core/dsp/ChannelConfig.h`
- Create: `src/core/dsp/RxChannelState.h`

- [ ] **Step 1: Write `ChannelConfig.h`**

```cpp
// src/core/dsp/ChannelConfig.h
#pragma once

namespace NereusSDR {

/// Inputs for RxChannel/TxChannel rebuild. All values authoritative for the
/// new channel; rebuild captures current state, destroys, recreates with this
/// config, then reapplies state.
struct ChannelConfig {
    int  sampleRate                 = 48000;
    int  bufferSize                 = 256;
    int  filterSize                 = 4096;
    int  filterType                 = 0;     // 0=LowLatency, 1=LinearPhase
    bool cacheImpulse               = false;
    bool cacheImpulseSaveRestore    = false;
    bool highResFilterCharacteristics = false;
};

}  // namespace NereusSDR
```

- [ ] **Step 2: Write `RxChannelState.h` (captured-state struct)**

```cpp
// src/core/dsp/RxChannelState.h
#pragma once

#include "models/SliceModel.h"   // for Mode enum

namespace NereusSDR {

/// Snapshot of all per-channel DSP state that survives a rebuild.
/// Captured before WDSP channel destroy; reapplied after recreate.
struct RxChannelState {
    SliceModel::Mode mode               = SliceModel::Mode::USB;
    int    filterLowHz                  = 200;
    int    filterHighHz                 = 2700;

    // AGC
    int    agcMode                      = 0;
    int    agcAttackMs                  = 5;
    int    agcDecayMs                   = 500;
    int    agcHangMs                    = 100;
    int    agcSlope                     = 5;
    int    agcMaxGainDb                 = 90;
    int    agcFixedGainDb               = 60;
    int    agcHangThresholdPct          = 0;

    // Noise blanker / noise reduction / ANF
    bool   nbEnabled                    = false;
    int    nbMode                       = 0;
    bool   nrEnabled                    = false;
    int    nrMode                       = 0;
    bool   anfEnabled                   = false;

    // EQ
    bool   eqEnabled                    = false;
    int    eqPreampDb                   = 0;
    int    eqBandsDb[10]                = {0,0,0,0,0,0,0,0,0,0};

    // Squelch
    bool   squelchEnabled               = false;
    int    squelchThresholdDb           = -150;

    // RIT, antenna, shift offset
    int    ritOffsetHz                  = 0;
    int    antennaIndex                 = 0;
    double shiftOffsetHz                = 0.0;
};

}  // namespace NereusSDR
```

- [ ] **Step 3: Add to CMake source list**

Modify `src/core/dsp/CMakeLists.txt` (or wherever DSP sources are listed) — add the two new headers. Headers-only, no compile units yet.

- [ ] **Step 4: Build to confirm headers compile**

Run: `cmake --build build -j --target NereusSDR_lib` (or equivalent target)
Expected: PASS, headers parse cleanly.

- [ ] **Step 5: Commit**

```bash
git add src/core/dsp/ChannelConfig.h src/core/dsp/RxChannelState.h src/core/dsp/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(infra): add ChannelConfig and RxChannelState structs

Foundation for RxChannel::rebuild(). ChannelConfig is the input;
RxChannelState is the snapshot captured/reapplied across teardown.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

### Task 1.2: Add `RxChannel::captureState()` and `applyState()`

**Files:**
- Modify: `src/core/dsp/RxChannel.h`
- Modify: `src/core/dsp/RxChannel.cpp`
- Test: `tests/tst_rx_channel_rebuild.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/tst_rx_channel_rebuild.cpp
#include <QtTest/QtTest>
#include "core/dsp/RxChannel.h"
#include "core/dsp/RxChannelState.h"

using namespace NereusSDR;

class TestRxChannelRebuild : public QObject {
    Q_OBJECT

private slots:
    void capture_returns_current_state()
    {
        RxChannel ch;
        ch.setMode(SliceModel::Mode::CWU);
        ch.setFilterLow(400);
        ch.setFilterHigh(800);

        const RxChannelState state = ch.captureState();
        QCOMPARE(state.mode, SliceModel::Mode::CWU);
        QCOMPARE(state.filterLowHz, 400);
        QCOMPARE(state.filterHighHz, 800);
    }

    void apply_round_trips_through_capture()
    {
        RxChannel ch;
        RxChannelState state;
        state.mode = SliceModel::Mode::USB;
        state.filterLowHz = 250;
        state.filterHighHz = 2750;
        state.agcMode = 3;

        ch.applyState(state);

        const RxChannelState verify = ch.captureState();
        QCOMPARE(verify.mode, SliceModel::Mode::USB);
        QCOMPARE(verify.filterLowHz, 250);
        QCOMPARE(verify.agcMode, 3);
    }
};

QTEST_MAIN(TestRxChannelRebuild)
#include "tst_rx_channel_rebuild.moc"
```

Add to `tests/CMakeLists.txt`:
```cmake
add_executable(tst_rx_channel_rebuild tst_rx_channel_rebuild.cpp)
target_link_libraries(tst_rx_channel_rebuild PRIVATE NereusSDR_lib Qt6::Test)
add_test(NAME tst_rx_channel_rebuild COMMAND tst_rx_channel_rebuild)
```

- [ ] **Step 2: Run test to verify it fails (no method yet)**

Run: `cmake --build build -j --target tst_rx_channel_rebuild`
Expected: COMPILE FAIL — `captureState()` / `applyState()` not declared.

- [ ] **Step 3: Add method declarations to `RxChannel.h`**

Add to public section:
```cpp
RxChannelState captureState() const;
void applyState(const RxChannelState& state);
```

- [ ] **Step 4: Implement in `RxChannel.cpp`**

```cpp
// In RxChannel.cpp

RxChannelState RxChannel::captureState() const
{
    RxChannelState s;
    s.mode               = m_mode;
    s.filterLowHz        = m_filterLowHz;
    s.filterHighHz       = m_filterHighHz;
    s.agcMode            = m_agcMode;
    s.agcAttackMs        = m_agcAttackMs;
    s.agcDecayMs         = m_agcDecayMs;
    s.agcHangMs          = m_agcHangMs;
    s.agcSlope           = m_agcSlope;
    s.agcMaxGainDb       = m_agcMaxGainDb;
    s.agcFixedGainDb     = m_agcFixedGainDb;
    s.agcHangThresholdPct = m_agcHangThresholdPct;
    s.nbEnabled          = m_nbEnabled;
    s.nbMode             = m_nbMode;
    s.nrEnabled          = m_nrEnabled;
    s.nrMode             = m_nrMode;
    s.anfEnabled         = m_anfEnabled;
    s.eqEnabled          = m_eqEnabled;
    s.eqPreampDb         = m_eqPreampDb;
    for (int i = 0; i < 10; ++i) s.eqBandsDb[i] = m_eqBandsDb[i];
    s.squelchEnabled     = m_squelchEnabled;
    s.squelchThresholdDb = m_squelchThresholdDb;
    s.ritOffsetHz        = m_ritOffsetHz;
    s.antennaIndex       = m_antennaIndex;
    s.shiftOffsetHz      = m_shiftOffsetHz;
    return s;
}

void RxChannel::applyState(const RxChannelState& s)
{
    setMode(s.mode);
    setFilterLow(s.filterLowHz);
    setFilterHigh(s.filterHighHz);
    setAgcMode(s.agcMode);
    setAgcAttack(s.agcAttackMs);
    setAgcDecay(s.agcDecayMs);
    setAgcHang(s.agcHangMs);
    setAgcSlope(s.agcSlope);
    setAgcMaxGain(s.agcMaxGainDb);
    setAgcFixedGain(s.agcFixedGainDb);
    setAgcHangThreshold(s.agcHangThresholdPct);
    setNbEnabled(s.nbEnabled);
    setNbMode(s.nbMode);
    setNrEnabled(s.nrEnabled);
    setNrMode(s.nrMode);
    setAnfEnabled(s.anfEnabled);
    setEqEnabled(s.eqEnabled);
    setEqPreamp(s.eqPreampDb);
    for (int i = 0; i < 10; ++i) setEqBand(i, s.eqBandsDb[i]);
    setSquelchEnabled(s.squelchEnabled);
    setSquelchThreshold(s.squelchThresholdDb);
    setRitOffset(s.ritOffsetHz);
    setAntennaIndex(s.antennaIndex);
    setShiftOffset(s.shiftOffsetHz);
}
```

- [ ] **Step 5: Run test to verify pass**

Run: `cmake --build build -j --target tst_rx_channel_rebuild && ctest --test-dir build -R tst_rx_channel_rebuild --output-on-failure`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/core/dsp/RxChannel.h src/core/dsp/RxChannel.cpp tests/tst_rx_channel_rebuild.cpp tests/CMakeLists.txt
git commit -m "feat(infra): add RxChannel::captureState/applyState round-trip"
```

---

### Task 1.3: Implement `RxChannel::rebuild()`

**Files:**
- Modify: `src/core/dsp/RxChannel.h`
- Modify: `src/core/dsp/RxChannel.cpp`
- Modify: `src/core/dsp/WdspEngine.h/.cpp` — expose destroy/recreate API
- Test: `tests/tst_rx_channel_rebuild.cpp` (extend)

- [ ] **Step 1: Add a new test for full rebuild cycle**

Append to `tst_rx_channel_rebuild.cpp`:
```cpp
    void rebuild_preserves_state_with_new_buffer_size()
    {
        RxChannel ch;
        ch.setMode(SliceModel::Mode::USB);
        ch.setFilterLow(200);
        ch.setFilterHigh(2700);
        ch.setAgcMode(2);
        ch.start(48000, 256, 4096, 0);  // sampleRate, bufSize, filterSize, filterType

        ChannelConfig cfg;
        cfg.sampleRate = 48000;
        cfg.bufferSize = 512;       // changed
        cfg.filterSize = 4096;
        cfg.filterType = 0;

        const qint64 elapsedMs = ch.rebuild(cfg);
        QVERIFY(elapsedMs >= 0);
        QVERIFY(elapsedMs < 200);   // glitch budget

        // State preserved across rebuild
        const RxChannelState s = ch.captureState();
        QCOMPARE(s.mode, SliceModel::Mode::USB);
        QCOMPARE(s.filterLowHz, 200);
        QCOMPARE(s.filterHighHz, 2700);
        QCOMPARE(s.agcMode, 2);
    }
```

- [ ] **Step 2: Run test to verify it fails**

Expected: COMPILE FAIL — `rebuild()` not declared.

- [ ] **Step 3: Add `rebuild()` declaration to `RxChannel.h`**

```cpp
/// Tear down WDSP channel, recreate with new config, reapply captured state.
/// Returns elapsed milliseconds (used by DspOptionsPage "Time to last change"
/// readout). Thread safety: called on main thread; audio thread sees the new
/// channel ID via atomic swap.
qint64 rebuild(const ChannelConfig& newCfg);
```

- [ ] **Step 4: Implement `rebuild()` in `RxChannel.cpp`**

```cpp
qint64 RxChannel::rebuild(const ChannelConfig& cfg)
{
    QElapsedTimer timer;
    timer.start();

    // Capture state before destroying channel
    const RxChannelState state = captureState();
    const bool wasRunning = m_running.load();

    // Tear down WDSP channel
    if (m_wdspChannelId >= 0) {
        m_wdspEngine->destroyChannel(m_wdspChannelId);
        m_wdspChannelId = -1;
    }

    // Recreate with new config
    m_wdspChannelId = m_wdspEngine->createChannel(cfg);
    m_sampleRate    = cfg.sampleRate;
    m_bufferSize    = cfg.bufferSize;
    m_filterSize    = cfg.filterSize;
    m_filterType    = cfg.filterType;

    // Reapply state
    applyState(state);

    if (wasRunning) {
        m_running.store(true);  // resume sample feed
    }

    return timer.elapsed();
}
```

- [ ] **Step 5: Add WdspEngine API**

Modify `src/core/dsp/WdspEngine.h`:
```cpp
int  createChannel(const ChannelConfig& cfg);
void destroyChannel(int channelId);
```

Implement in `WdspEngine.cpp`:
```cpp
int WdspEngine::createChannel(const ChannelConfig& cfg)
{
    const int id = ++m_nextChannelId;
    // From Thetis cmaster.cs:NNNN [v2.10.3.13] — channel allocation
    // (verify exact API names in cmaster.cs during impl)
    OpenChannel(id, cfg.sampleRate, cfg.bufferSize, cfg.bufferSize,
                cfg.filterSize, cfg.filterType,
                cfg.cacheImpulse ? 1 : 0,
                cfg.cacheImpulseSaveRestore ? 1 : 0);
    return id;
}

void WdspEngine::destroyChannel(int id)
{
    // From Thetis cmaster.cs:NNNN [v2.10.3.13] — channel free
    CloseChannel(id);
}
```

- [ ] **Step 6: Run tests**

Run: `cmake --build build -j && ctest --test-dir build -R tst_rx_channel_rebuild --output-on-failure`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/core/dsp/RxChannel.h src/core/dsp/RxChannel.cpp src/core/dsp/WdspEngine.h src/core/dsp/WdspEngine.cpp tests/tst_rx_channel_rebuild.cpp
git commit -m "feat(infra): RxChannel::rebuild() + WdspEngine create/destroyChannel API"
```

---

### Task 1.4: Mirror rebuild API on `TxChannel`

**Files:**
- Create: `src/core/dsp/TxChannelState.h`
- Modify: `src/core/dsp/TxChannel.h/.cpp`
- Test: `tests/tst_tx_channel_rebuild.cpp`

- [ ] **Step 1: Write `TxChannelState.h`**

```cpp
// src/core/dsp/TxChannelState.h
#pragma once
#include "models/SliceModel.h"

namespace NereusSDR {

/// Snapshot of all per-channel TX DSP state that survives a rebuild.
struct TxChannelState {
    SliceModel::Mode mode               = SliceModel::Mode::USB;
    int    filterLowHz                  = 200;
    int    filterHighHz                 = 2700;

    // Mic / TX EQ
    int    micGainDb                    = 0;
    bool   eqEnabled                    = false;
    int    eqPreampDb                   = 0;
    int    eqBandsDb[10]                = {0,0,0,0,0,0,0,0,0,0};

    // Leveler / ALC
    bool   levelerOn                    = false;
    int    levelerMaxGainDb             = 5;
    int    levelerDecayMs               = 500;
    int    alcMaxGainDb                 = 3;
    int    alcDecayMs                   = 10;

    // CFC + Phase Rotator + CESSB + CPDR
    bool   cfcOn                        = false;
    bool   cfcPostEqOn                  = false;
    int    cfcPrecompDb                 = 0;
    int    cfcPostEqGainDb              = 0;
    bool   phaseRotatorOn               = false;
    int    phaseRotatorFreqHz           = 338;
    int    phaseRotatorStages           = 8;
    bool   phaseRotatorReverse          = false;
    bool   cessbOn                      = false;
    bool   cpdrOn                       = false;
    double cpdrLevelDb                  = 0.0;

    // PureSignal placeholder (3M-4 work; capture but unused for now)
    bool   pureSignalEnabled            = false;
};

}  // namespace NereusSDR
```

- [ ] **Step 2: Write the failing test**

```cpp
// tests/tst_tx_channel_rebuild.cpp
#include <QtTest/QtTest>
#include "core/dsp/TxChannel.h"
#include "core/dsp/TxChannelState.h"

using namespace NereusSDR;

class TestTxChannelRebuild : public QObject {
    Q_OBJECT

private slots:
    void capture_returns_current_state()
    {
        TxChannel ch;
        ch.setMode(SliceModel::Mode::USB);
        ch.setMicGain(12);
        ch.setLevelerOn(true);
        ch.setLevelerMaxGain(8);

        const TxChannelState state = ch.captureState();
        QCOMPARE(state.mode, SliceModel::Mode::USB);
        QCOMPARE(state.micGainDb, 12);
        QVERIFY(state.levelerOn);
        QCOMPARE(state.levelerMaxGainDb, 8);
    }

    void apply_round_trips_through_capture()
    {
        TxChannel ch;
        TxChannelState state;
        state.mode               = SliceModel::Mode::USB;
        state.micGainDb          = 18;
        state.cfcOn              = true;
        state.phaseRotatorOn     = true;
        state.phaseRotatorStages = 9;

        ch.applyState(state);

        const TxChannelState verify = ch.captureState();
        QCOMPARE(verify.micGainDb, 18);
        QVERIFY(verify.cfcOn);
        QVERIFY(verify.phaseRotatorOn);
        QCOMPARE(verify.phaseRotatorStages, 9);
    }

    void rebuild_preserves_state_with_new_buffer_size()
    {
        TxChannel ch;
        ch.setMode(SliceModel::Mode::USB);
        ch.setMicGain(10);
        ch.setCfcOn(true);
        ch.start(48000, 256, 4096, 0);

        ChannelConfig cfg;
        cfg.sampleRate = 48000;
        cfg.bufferSize = 512;
        cfg.filterSize = 4096;
        cfg.filterType = 0;

        const qint64 elapsedMs = ch.rebuild(cfg);
        QVERIFY(elapsedMs >= 0);
        QVERIFY(elapsedMs < 200);

        const TxChannelState s = ch.captureState();
        QCOMPARE(s.mode, SliceModel::Mode::USB);
        QCOMPARE(s.micGainDb, 10);
        QVERIFY(s.cfcOn);
    }
};

QTEST_MAIN(TestTxChannelRebuild)
#include "tst_tx_channel_rebuild.moc"
```

Add to `tests/CMakeLists.txt`:
```cmake
add_executable(tst_tx_channel_rebuild tst_tx_channel_rebuild.cpp)
target_link_libraries(tst_tx_channel_rebuild PRIVATE NereusSDR_lib Qt6::Test)
add_test(NAME tst_tx_channel_rebuild COMMAND tst_tx_channel_rebuild)
```

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build -j --target tst_tx_channel_rebuild`
Expected: COMPILE FAIL — `captureState()` / `applyState()` / `rebuild()` not declared on TxChannel.

- [ ] **Step 4: Add declarations to `TxChannel.h`**

```cpp
TxChannelState captureState() const;
void applyState(const TxChannelState& state);
qint64 rebuild(const ChannelConfig& newCfg);
```

- [ ] **Step 5: Implement in `TxChannel.cpp`**

Pattern matches `RxChannel::captureState()` / `applyState()` / `rebuild()` from Tasks 1.2 and 1.3, but operating on TX-specific member fields (`m_micGainDb`, `m_cfcOn`, `m_phaseRotatorStages`, etc.). The rebuild sequence is identical: capture state → destroy WDSP channel → recreate with new config → reapply state → return elapsed ms.

```cpp
TxChannelState TxChannel::captureState() const
{
    TxChannelState s;
    s.mode               = m_mode;
    s.filterLowHz        = m_filterLowHz;
    s.filterHighHz       = m_filterHighHz;
    s.micGainDb          = m_micGainDb;
    s.eqEnabled          = m_eqEnabled;
    s.eqPreampDb         = m_eqPreampDb;
    for (int i = 0; i < 10; ++i) s.eqBandsDb[i] = m_eqBandsDb[i];
    s.levelerOn          = m_levelerOn;
    s.levelerMaxGainDb   = m_levelerMaxGainDb;
    s.levelerDecayMs     = m_levelerDecayMs;
    s.alcMaxGainDb       = m_alcMaxGainDb;
    s.alcDecayMs         = m_alcDecayMs;
    s.cfcOn              = m_cfcOn;
    s.cfcPostEqOn        = m_cfcPostEqOn;
    s.cfcPrecompDb       = m_cfcPrecompDb;
    s.cfcPostEqGainDb    = m_cfcPostEqGainDb;
    s.phaseRotatorOn     = m_phaseRotatorOn;
    s.phaseRotatorFreqHz = m_phaseRotatorFreqHz;
    s.phaseRotatorStages = m_phaseRotatorStages;
    s.phaseRotatorReverse = m_phaseRotatorReverse;
    s.cessbOn            = m_cessbOn;
    s.cpdrOn             = m_cpdrOn;
    s.cpdrLevelDb        = m_cpdrLevelDb;
    s.pureSignalEnabled  = m_pureSignalEnabled;
    return s;
}

void TxChannel::applyState(const TxChannelState& s)
{
    setMode(s.mode);
    setFilterLow(s.filterLowHz);
    setFilterHigh(s.filterHighHz);
    setMicGain(s.micGainDb);
    setEqEnabled(s.eqEnabled);
    setEqPreamp(s.eqPreampDb);
    for (int i = 0; i < 10; ++i) setEqBand(i, s.eqBandsDb[i]);
    setLevelerOn(s.levelerOn);
    setLevelerMaxGain(s.levelerMaxGainDb);
    setLevelerDecay(s.levelerDecayMs);
    setAlcMaxGain(s.alcMaxGainDb);
    setAlcDecay(s.alcDecayMs);
    setCfcOn(s.cfcOn);
    setCfcPostEqOn(s.cfcPostEqOn);
    setCfcPrecomp(s.cfcPrecompDb);
    setCfcPostEqGain(s.cfcPostEqGainDb);
    setPhaseRotatorOn(s.phaseRotatorOn);
    setPhaseRotatorFreq(s.phaseRotatorFreqHz);
    setPhaseRotatorStages(s.phaseRotatorStages);
    setPhaseRotatorReverse(s.phaseRotatorReverse);
    setCessbOn(s.cessbOn);
    setCpdrOn(s.cpdrOn);
    setCpdrLevel(s.cpdrLevelDb);
    // PureSignal not yet wired (3M-4 work)
}

qint64 TxChannel::rebuild(const ChannelConfig& cfg)
{
    QElapsedTimer timer;
    timer.start();

    const TxChannelState state = captureState();
    const bool wasRunning = m_running.load();

    if (m_wdspChannelId >= 0) {
        m_wdspEngine->destroyChannel(m_wdspChannelId);
        m_wdspChannelId = -1;
    }

    m_wdspChannelId = m_wdspEngine->createChannel(cfg);
    m_sampleRate    = cfg.sampleRate;
    m_bufferSize    = cfg.bufferSize;
    m_filterSize    = cfg.filterSize;
    m_filterType    = cfg.filterType;

    applyState(state);

    if (wasRunning) {
        m_running.store(true);
    }

    return timer.elapsed();
}
```

- [ ] **Step 6: Run tests**

Run: `cmake --build build -j --target tst_tx_channel_rebuild && ctest --test-dir build -R tst_tx_channel_rebuild --output-on-failure`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/core/dsp/TxChannelState.h src/core/dsp/TxChannel.h src/core/dsp/TxChannel.cpp tests/tst_tx_channel_rebuild.cpp tests/CMakeLists.txt
git commit -m "feat(infra): TxChannel::rebuild() with TX-specific state capture"
```

---

### Task 1.5: Add `RxChannel::filterResponseMagnitudes()` accessor

**Files:**
- Modify: `src/core/dsp/RxChannel.h/.cpp`
- Test: `tests/tst_filter_high_resolution.cpp`

This accessor returns the FFT-magnitude curve of the current filter taps for the FilterDisplayItem high-res rendering (Section 4D / Phase 4).

- [ ] **Step 1: Write the failing test**

```cpp
// tests/tst_filter_high_resolution.cpp
#include <QtTest/QtTest>
#include "core/dsp/RxChannel.h"

using namespace NereusSDR;

class TestFilterHighResolution : public QObject {
    Q_OBJECT
private slots:
    void returns_n_points_of_magnitude_data()
    {
        RxChannel ch;
        ch.start(48000, 256, 4096, 0);
        ch.setFilterLow(200);
        ch.setFilterHigh(2700);

        const QVector<float> mag = ch.filterResponseMagnitudes(512);
        QCOMPARE(mag.size(), 512);

        // Magnitude near passband center (1450 Hz) should be high (close to 0 dB)
        // Magnitude well outside (e.g., 5000 Hz) should be low.
        // Bin index = floor((freq / nyquist) * (nPoints/2))
        const int passBandBin = 31;   // 1450 Hz / 24000 nyquist * 256
        const int outsideBin  = 107;  // 5000 Hz / 24000 nyquist * 256
        QVERIFY(mag[passBandBin] > mag[outsideBin]);
    }
};

QTEST_MAIN(TestFilterHighResolution)
#include "tst_filter_high_resolution.moc"
```

- [ ] **Step 2: Run to verify fail.**

- [ ] **Step 3: Add declaration in `RxChannel.h`**

```cpp
/// Returns the FFT magnitude (in dB) of the current filter taps, sampled at
/// nPoints uniformly across [0, sampleRate/2]. For the high-resolution filter
/// graph (Section 4D, DspOptionsHighResFilterCharacteristics).
QVector<float> filterResponseMagnitudes(int nPoints) const;
```

- [ ] **Step 4: Implement in `RxChannel.cpp`** — Pull current filter taps from WDSP (verify API: likely a `getFilterTaps(channelId, &taps)` call), FFT to magnitude, sample at nPoints. Use FFTW3 (already a dependency).

- [ ] **Step 5: Run test, expect PASS.**

- [ ] **Step 6: Commit**

```bash
git commit -m "feat(infra): RxChannel::filterResponseMagnitudes() accessor"
```

---

### Task 1.6: Sample-rate live-apply coordinator

**Files:**
- Modify: `src/models/RadioModel.h/.cpp` — add `setSampleRateLive(int hz)` method
- Modify: `src/core/protocol/P1RadioConnection.cpp` — sample-rate change command + restart
- Modify: `src/core/protocol/P2RadioConnection.cpp` — sample-rate change command
- Modify: `src/core/AudioEngine.cpp` — re-init on sample-rate change
- Test: `tests/tst_sample_rate_live_apply.cpp`

- [ ] **Step 1: Write the integration test (mocked hardware)**

```cpp
// tests/tst_sample_rate_live_apply.cpp
// Exercises the full coordinator path: pause audio → reconfig hardware →
// rebuild channels → resume. Hardware mocked via test fakes.

class TestSampleRateLiveApply : public QObject {
    Q_OBJECT
private slots:
    void changes_rate_without_dropping_channel()
    {
        RadioModel model;
        model.startWithMockHardware();
        model.setSampleRateLive(96000);
        QCOMPARE(model.sampleRate(), 96000);
        QVERIFY(model.rxChannel()->isRunning());
    }

    void rebuild_completes_under_glitch_budget()
    {
        RadioModel model;
        model.startWithMockHardware();
        QElapsedTimer t; t.start();
        model.setSampleRateLive(192000);
        QVERIFY(t.elapsed() < 200);  // 200ms ceiling
    }
};
```

- [ ] **Step 2: Implement coordinator in `RadioModel`**

```cpp
qint64 RadioModel::setSampleRateLive(int newRateHz)
{
    QElapsedTimer t; t.start();

    // 1. Quiesce audio
    m_audioEngine->pauseInput();

    // 2. Quiesce hardware
    if (m_p1Connection) m_p1Connection->stopStream();
    if (m_p2Connection) m_p2Connection->stopStream();

    // 3. Reconfigure hardware
    if (m_p1Connection) m_p1Connection->setSampleRate(newRateHz);
    if (m_p2Connection) m_p2Connection->setSampleRate(newRateHz);

    // 4. Rebuild WDSP channels
    for (auto* rx : m_rxChannels) {
        ChannelConfig cfg = rx->config();
        cfg.sampleRate = newRateHz;
        rx->rebuild(cfg);
    }
    if (m_txChannel) {
        ChannelConfig cfg = m_txChannel->config();
        cfg.sampleRate = newRateHz;
        m_txChannel->rebuild(cfg);
    }

    // 5. Re-init AudioEngine
    m_audioEngine->reinitForSampleRate(newRateHz);

    // 6. Resume hardware
    if (m_p1Connection) m_p1Connection->startStream();
    if (m_p2Connection) m_p2Connection->startStream();

    // 7. Resume audio
    m_audioEngine->resumeInput();

    m_sampleRate = newRateHz;
    emit sampleRateChanged(newRateHz);
    emit dspChangeMeasured(t.elapsed());
    return t.elapsed();
}
```

- [ ] **Step 3: Implement P1/P2 setSampleRate methods, AudioEngine::reinitForSampleRate, AudioEngine::pauseInput/resumeInput.**

For P1: send Thetis-style command frame on EP2 with new rate code (verify exact byte layout from `NetworkIO.cs:NNNN [v2.10.3.13]` during impl).

For P2: send sample-rate change command on UDP port 1024 (or appropriate command port).

- [ ] **Step 4: Run test, expect PASS.**

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(infra): sample-rate live-apply coordinator (P1 + P2)"
```

---

### Task 1.7: Active-RX-count live-apply

**Files:**
- Modify: `src/core/ReceiverManager.cpp` — `setActiveRxCountLive(N)`
- Modify: `src/core/protocol/P1RadioConnection.cpp` + `MetisFrameParser.cpp` — handle EP6 frame format change
- Modify: `src/core/protocol/P2RadioConnection.cpp` — DDC port subscriptions
- Test: `tests/tst_active_rx_count_live_apply.cpp`

**Note:** Per design Appendix A item 5, MetisFrameParser may need rework to handle mid-stream RX-count change. Verify before coding by reading `MetisFrameParser.cpp` start-of-task.

- [ ] **Step 1: Read `MetisFrameParser.cpp` and document whether it currently handles count change mid-stream.**

If yes → proceed normally. If no → add ~2 days of parser rework as part of this task per Q17.3 = approved.

- [ ] **Step 2: Write the failing test**

```cpp
// tests/tst_active_rx_count_live_apply.cpp
class TestActiveRxCountLiveApply : public QObject {
    Q_OBJECT
private slots:
    void enable_rx2_creates_second_channel()
    {
        RadioModel model;
        model.startWithMockHardware(/*caps.maxRx=*/2);
        QCOMPARE(model.receiverManager()->activeRxCount(), 1);

        model.receiverManager()->setActiveRxCountLive(2);
        QCOMPARE(model.receiverManager()->activeRxCount(), 2);
        QVERIFY(model.rxChannel(0)->isRunning());
        QVERIFY(model.rxChannel(1)->isRunning());
    }

    void disable_rx2_destroys_second_channel()
    {
        RadioModel model;
        model.startWithMockHardware(/*caps.maxRx=*/2);
        model.receiverManager()->setActiveRxCountLive(2);
        model.receiverManager()->setActiveRxCountLive(1);
        QCOMPARE(model.receiverManager()->activeRxCount(), 1);
        QVERIFY(!model.rxChannel(1)->isRunning());
    }
};
```

- [ ] **Step 3: Implement `ReceiverManager::setActiveRxCountLive`** following the same coordinator pattern as Task 1.6 (pause → reconfig DDC mapping → create/destroy channels → resume).

- [ ] **Step 4: Run test, expect PASS.**

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(infra): active-RX-count live-apply (P1 EP6 + P2 DDC port handling)"
```

---

### Task 1.8: `DspChangeTimer` + `RadioModel::dspChangeMeasured` signal

**Files:**
- Create: `src/core/dsp/DspChangeTimer.h/.cpp`
- Modify: `src/models/RadioModel.h` — add `dspChangeMeasured(qint64 ms)` signal
- Test: `tests/tst_dsp_change_timer.cpp`

- [ ] **Step 1: Write the test**

```cpp
class TestDspChangeTimer : public QObject {
    Q_OBJECT
private slots:
    void measures_elapsed_ms_for_rebuild()
    {
        QSignalSpy spy(&radioModel, &RadioModel::dspChangeMeasured);

        // Trigger any rebuild (e.g., setBufferSize)
        radioModel.rxChannel()->setBufferSize(512);

        QCOMPARE(spy.count(), 1);
        const qint64 ms = spy.takeFirst().at(0).toLongLong();
        QVERIFY(ms >= 0);
        QVERIFY(ms < 200);
    }
};
```

- [ ] **Step 2: Add signal to `RadioModel.h`**

```cpp
signals:
    void dspChangeMeasured(qint64 ms);
```

- [ ] **Step 3: Wire `RxChannel::rebuild()` → emit signal via `RadioModel`**

In `RxChannel::rebuild()` after measuring elapsed ms, call back into RadioModel (via parent pointer or signal): `m_radioModel->emitDspChangeMeasured(elapsed);`

- [ ] **Step 4: Run test, expect PASS.**

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(infra): DspChangeTimer + RadioModel::dspChangeMeasured signal"
```

---

## Phase 2 — RX1 audit (Cluster 2)

**Depends on Phase 1.** ~14 tasks across 6 sub-areas (2A-2F per design).

### Task 2.1: Detector + Averaging split (handwave fix, Section 2A)

**Files:**
- Modify: `src/gui/SpectrumWidget.h/.cpp` — split `setAverageMode` into 4 setters
- Modify: `src/gui/setup/DisplaySetupPages.cpp` — add 2 combos to Spectrum Defaults; add 2 combos to Waterfall Defaults
- Test: `tests/tst_detector_modes.cpp`, `tests/tst_averaging_modes.cpp`

- [ ] **Step 1: Verify Thetis WDSP API for detector + averaging**

Run: `grep -n "SetRXASpectrum\|SetRXASpectrumDetector\|SetRXASpectrumAveraging" /Users/j.j.boyd/Thetis/Project\ Files/Source/Console/dsp.cs`
Capture exact P/Invoke signatures into commit message; do not guess.

- [ ] **Step 2: Write tests**

```cpp
// tests/tst_detector_modes.cpp
class TestDetectorModes : public QObject {
    Q_OBJECT
private slots:
    void peak_detector_returns_max_in_window()
    {
        // Synthetic FFT: 1024 bins, all -100 dBm except bin 500 = -40 dBm
        // With Peak detector across 4-bin window, output bin at i=125 should be -40
        SpectrumWidget w;
        w.setSpectrumDetector(SpectrumWidget::Detector::Peak);
        QVector<float> out;
        out.resize(256);
        w.applyDetectorTo(syntheticBins, out);
        QVERIFY(out[125] > -50.0f);  // peak survived
    }

    void rms_detector_returns_root_mean_square()
    {
        // …
    }

    void sample_detector_returns_first_bin_per_window() { … }
    void average_detector_returns_mean() { … }
};
```

- [ ] **Step 3: Add Detector + Averaging enums to `SpectrumWidget.h`**

```cpp
enum class SpectrumDetector { Peak, RMS, Sample, Average };
enum class SpectrumAveraging { None, Recursive, TimeWindow, LogRecursive, LogMp };

void setSpectrumDetector(SpectrumDetector d);
void setSpectrumAveraging(SpectrumAveraging a);
void setWaterfallDetector(SpectrumDetector d);
void setWaterfallAveraging(SpectrumAveraging a);

// Retire setAverageMode (moved to migration)
```

- [ ] **Step 4: Implement detector + averaging stages in render path**

Apply detector during bin reduction; apply averaging across frames. Reference `Display.cs:NNNN [v2.10.3.13]` for Thetis logic.

- [ ] **Step 5: Add UI combos to Spectrum Defaults + Waterfall Defaults pages**

In `DisplaySetupPages.cpp`, replace single `m_averageModeCombo` with two combos in Spectrum Defaults; add the same two on Waterfall Defaults.

- [ ] **Step 6: Run tests, expect PASS.**

- [ ] **Step 7: Commit**

```bash
git commit -m "fix(handwave): split DisplayAverageMode into Detector + Averaging (spectrum + waterfall)"
```

---

### Task 2.2: S2 FFT Window cite correction (Section 2F)

**Files:**
- Modify: `src/gui/setup/DisplaySetupPages.cpp` — replace NereusSDR-original comment near FFT Window combo with Thetis cite

- [ ] **Step 1: Locate current S2 FFT Window code** — grep for "FFT Window" or "FFT_Window".

- [ ] **Step 2: Replace comment**

Old (likely):
```cpp
// NereusSDR extension — FFT window selection
m_fftWindowCombo = new ComboStyle(this);
```

New:
```cpp
// From Thetis setup.cs:NNNN [v2.10.3.13] — comboDispWinType
//   FFT window selection (Blackman-Harris / Hann / Hamming / Flat-Top)
m_fftWindowCombo = new ComboStyle(this);
```

Verify NNNN by grepping Thetis for `comboDispWinType` and locating the designer line.

- [ ] **Step 3: Update tooltip if needed** — remove any "NereusSDR extension" framing (per `feedback_no_cites_in_user_strings`, tooltip stays plain English).

- [ ] **Step 4: Build to confirm no regression**

Run: `cmake --build build -j`

- [ ] **Step 5: Commit**

```bash
git commit -m "fix(cite): correct S2 FFT Window provenance to Thetis comboDispWinType"
```

---

### Task 2.3: Spectrum Defaults extensions — overlay group + decimation wire (Section 2B)

**Files:**
- Modify: `src/gui/setup/DisplaySetupPages.cpp` — add 7 controls to Spectrum Defaults page
- Modify: `src/gui/SpectrumWidget.h/.cpp` — new setters for overlay rendering
- Modify: `src/core/FFTEngine.cpp` — wire decimation
- Test: `tests/tst_spectrum_overlays.cpp`

- [ ] **Step 1: Write tests for overlay rendering**

```cpp
// tests/tst_spectrum_overlays.cpp
class TestSpectrumOverlays : public QObject {
    Q_OBJECT
private slots:
    void show_mhz_on_cursor_formats_with_decimal()
    {
        SpectrumWidget w;
        w.setShowMHzOnCursor(true);
        QCOMPARE(w.formatCursorFreq(7173500), QString("7.1735 MHz"));
    }

    void bin_width_readout_computed_from_fft_and_rate()
    {
        SpectrumWidget w;
        w.setFftSize(4096);
        w.setSampleRate(48000);
        QCOMPARE(w.binWidthHz(), 48000.0 / 4096.0);
    }

    void show_noise_floor_overlay_renders_text()
    {
        // Render to QImage and check for "-100" text at TopRight position
    }

    void show_peak_value_overlay_finds_max_in_visible_range()
    {
        // Synthetic spectrum with peak at 7.073 MHz, -67.3 dBm
        // Verify formatCursorFreq returns "Peak: -67.3 dBm @ 7.0730 MHz"
    }
};
```

- [ ] **Step 2: Add new setters and overlay rendering to `SpectrumWidget`**

```cpp
// In SpectrumWidget.h
void setShowMHzOnCursor(bool);
void setShowBinWidth(bool);
void setShowNoiseFloor(bool);
void setShowNoiseFloorPosition(OverlayPosition pos);
void setDispNormalize(bool);
void setShowPeakValueOverlay(bool);
void setPeakValuePosition(OverlayPosition pos);
void setPeakTextDelayMs(int ms);
void setPeakValueColor(QColor c);

enum class OverlayPosition { TopLeft, TopRight, BottomLeft, BottomRight };
```

Render overlays in QPainter pass after main spectrum trace. Each overlay reads its enabled flag + position; positions text via QFontMetrics.

- [ ] **Step 3: Wire decimation in FFTEngine**

```cpp
// FFTEngine::setDecimation(int factor)
// Decimate input I/Q by `factor` before FFT (every Nth sample).
// From Thetis setup.cs:NNNN [v2.10.3.13] — udDisplayDecimation
```

- [ ] **Step 4: Add 7 controls + 1 group box ("Spectrum Overlays") to Spectrum Defaults page**

Controls: ShowMHzOnCursor, ShowBinWidth + readout label, ShowNoiseFloor + position combo, DispNormalize, ShowPeakValueOverlay + position + delay spinbox + color swatch, GetMonitorHz button, Decimation spinbox.

Group "Spectrum Overlays" collects ShowMHzOnCursor + ShowBinWidth + ShowNoiseFloor + DispNormalize + ShowPeakValueOverlay (with their position combos and supplementary controls).

- [ ] **Step 5: Implement GetMonitorHz button**

```cpp
void SpectrumDefaultsPage::onGetMonitorHzClicked()
{
    const QScreen* screen = window()->screen();
    const qreal hz = screen->refreshRate();
    // Snap FPS slider to nearest valid value
    const int snapped = nearestValidFps(qRound(hz));
    m_fpsSlider->setValue(snapped);
}
```

- [ ] **Step 6: Run tests, expect PASS.**

- [ ] **Step 7: Commit**

```bash
git commit -m "feat(display): Spectrum Defaults overlay group (MHz cursor, NF, bin width, normalize, peak text) + decimation wire-up"
```

---

### Task 2.4: SpectrumPeaksPage skeleton + cross-link buttons (Section 2C)

**Files:**
- Create: `src/gui/setup/SpectrumPeaksPage.h/.cpp`
- Modify: `src/gui/SetupDialog.cpp` — register page in tree
- Modify: `src/gui/setup/DisplaySetupPages.cpp` — add cross-link buttons

- [ ] **Step 1: Write SpectrumPeaksPage skeleton**

```cpp
// src/gui/setup/SpectrumPeaksPage.h
#pragma once
#include "gui/SetupPage.h"

class QCheckBox;
class QSpinBox;
class QPushButton;
namespace NereusSDR { class ColorSwatchButton; }

namespace NereusSDR {

class SpectrumPeaksPage : public SetupPage {
    Q_OBJECT
public:
    explicit SpectrumPeaksPage(RadioModel* model, QWidget* parent = nullptr);

signals:
    void backToSpectrumDefaultsRequested();

private:
    void buildUI();

    // Active Peak Hold group (5 ctrls)
    QCheckBox*  m_aphEnable{nullptr};
    QSpinBox*   m_aphDurationMs{nullptr};
    QSpinBox*   m_aphDropDbPerSec{nullptr};
    QCheckBox*  m_aphFill{nullptr};
    QCheckBox*  m_aphOnTx{nullptr};

    // Peak Blobs group (7 ctrls + 2 colors)
    QCheckBox*  m_blobEnable{nullptr};
    QSpinBox*   m_blobCount{nullptr};
    QCheckBox*  m_blobInsideFilter{nullptr};
    QCheckBox*  m_blobHoldEnable{nullptr};
    QSpinBox*   m_blobHoldMs{nullptr};
    QCheckBox*  m_blobHoldDrop{nullptr};
    QSpinBox*   m_blobFallDbPerSec{nullptr};
    ColorSwatchButton* m_blobColor{nullptr};
    ColorSwatchButton* m_blobTextColor{nullptr};

    QPushButton* m_backBtn{nullptr};
};

}  // namespace NereusSDR
```

Implementation builds two QGroupBox sections, persists to AppSettings on change. Default values per design Section 1C.

- [ ] **Step 2: Register in `SetupDialog::buildPageTree()`** — add `SpectrumPeaksPage` between `SpectrumDefaultsPage` and `WaterfallDefaultsPage`.

- [ ] **Step 3: Add cross-link buttons in DisplaySetupPages.cpp**

In `SpectrumDefaultsPage::buildUI()`:
```cpp
auto* peaksBtn = new QPushButton("Configure peaks →", this);
connect(peaksBtn, &QPushButton::clicked, this,
    [this]() { emit pageJumpRequested("Display/Spectrum Peaks"); });

auto* multiBtn = new QPushButton("Configure multimeter →", this);
connect(multiBtn, &QPushButton::clicked, this,
    [this]() { emit pageJumpRequested("Display/Multimeter"); });
```

`SetupDialog` connects `pageJumpRequested` → `setCurrentPage(...)`.

- [ ] **Step 4: Add hint lines** to SpectrumDefaultsPage:

```cpp
auto* note1 = new QLabel("<i>ANAN-8000DLE volts/amps moved to Hardware → ANAN-8000DLE.</i>");
auto* note2 = new QLabel("<i>Small filter on VFOs moved to Appearance → VFO Flag.</i>");
```

- [ ] **Step 5: Build to confirm registration works.**

- [ ] **Step 6: Commit**

```bash
git commit -m "feat(display): SpectrumPeaksPage skeleton + cross-link buttons + hint lines"
```

---

### Task 2.5: ActivePeakHoldTrace class (Section 2C)

**Files:**
- Create: `src/gui/spectrum/ActivePeakHoldTrace.h/.cpp`
- Modify: `src/gui/SpectrumWidget.cpp` — add ActivePeakHold render pass
- Test: `tests/tst_active_peak_hold.cpp`

- [ ] **Step 1: Write the test**

```cpp
class TestActivePeakHold : public QObject {
    Q_OBJECT
private slots:
    void peak_value_persists_until_decay()
    {
        ActivePeakHoldTrace trace(/*nBins=*/256);
        QVector<float> bins(256, -100.0f);
        bins[100] = -40.0f;
        trace.update(bins);
        QCOMPARE(trace.peak(100), -40.0f);

        // Same input next frame
        bins[100] = -50.0f;
        trace.tickFrame(/*fps=*/30);
        trace.update(bins);
        // Peak should still be near -40, not -50, because decay
        // 6 dB/sec / 30 fps = 0.2 dB/frame
        QVERIFY(trace.peak(100) > -40.5f);
        QVERIFY(trace.peak(100) <= -40.0f);
    }

    void on_tx_state_disables_update()
    {
        ActivePeakHoldTrace trace(256);
        trace.setOnTx(false);  // do NOT track during TX
        trace.setTxActive(true);

        QVector<float> bins(256, -40.0f);
        trace.update(bins);
        // Peaks should stay at initial -INFINITY (no update)
        QVERIFY(!std::isfinite(trace.peak(0)) || trace.peak(0) <= -100.0f);
    }
};
```

- [ ] **Step 2: Implement `ActivePeakHoldTrace`**

```cpp
// src/gui/spectrum/ActivePeakHoldTrace.h
#pragma once
#include <QVector>

namespace NereusSDR {

class ActivePeakHoldTrace {
public:
    explicit ActivePeakHoldTrace(int nBins);
    void resize(int nBins);

    void setEnabled(bool e)         { m_enabled = e; }
    void setDurationMs(int ms)      { m_durationMs = ms; }
    void setDropDbPerSec(double r)  { m_dropDbPerSec = r; }
    void setFill(bool f)            { m_fill = f; }
    void setOnTx(bool o)            { m_onTx = o; }
    void setTxActive(bool t)        { m_txActive = t; }

    void update(const QVector<float>& currentBins);
    void tickFrame(int fps);
    void clear();

    bool   enabled() const          { return m_enabled; }
    bool   fill() const             { return m_fill; }
    float  peak(int bin) const      { return m_peaks[bin]; }
    const QVector<float>& peaks() const { return m_peaks; }

private:
    bool   m_enabled       = false;
    int    m_durationMs    = 2000;
    double m_dropDbPerSec  = 6.0;
    bool   m_fill          = false;
    bool   m_onTx          = false;
    bool   m_txActive      = false;
    QVector<float> m_peaks;
};

}
```

```cpp
// src/gui/spectrum/ActivePeakHoldTrace.cpp
#include "ActivePeakHoldTrace.h"
#include <limits>

namespace NereusSDR {

ActivePeakHoldTrace::ActivePeakHoldTrace(int nBins) { resize(nBins); }

void ActivePeakHoldTrace::resize(int nBins)
{
    m_peaks.resize(nBins);
    clear();
}

void ActivePeakHoldTrace::clear()
{
    std::fill(m_peaks.begin(), m_peaks.end(), -std::numeric_limits<float>::infinity());
}

void ActivePeakHoldTrace::update(const QVector<float>& bins)
{
    if (!m_enabled) return;
    if (m_txActive && !m_onTx) return;
    const int n = std::min(bins.size(), m_peaks.size());
    for (int i = 0; i < n; ++i) {
        if (bins[i] > m_peaks[i] || !std::isfinite(m_peaks[i])) {
            m_peaks[i] = bins[i];
        }
    }
}

void ActivePeakHoldTrace::tickFrame(int fps)
{
    if (!m_enabled || fps <= 0) return;
    const float dropPerFrame = static_cast<float>(m_dropDbPerSec / fps);
    for (auto& p : m_peaks) {
        if (std::isfinite(p)) p -= dropPerFrame;
    }
}

}
```

- [ ] **Step 3: Wire into SpectrumWidget render path** — separate render pass per Q14.1 = A.

```cpp
// In SpectrumWidget render loop:
m_activePeakHold.tickFrame(m_currentFps);
m_activePeakHold.update(m_smoothedBins);
if (m_activePeakHold.enabled()) {
    paintActivePeakHoldTrace(painter);  // or QRhi pipeline
}
```

- [ ] **Step 4: Run test, expect PASS.**

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(display): ActivePeakHoldTrace class with per-bin decay + render pass"
```

---

### Task 2.6: PeakBlobDetector class (Section 2C)

**Files:**
- Create: `src/gui/spectrum/PeakBlobDetector.h/.cpp`
- Modify: `src/gui/SpectrumWidget.cpp` — add Peak Blob render pass
- Test: `tests/tst_peak_blob_detector.cpp`

- [ ] **Step 1: Write tests**

```cpp
class TestPeakBlobDetector : public QObject {
    Q_OBJECT
private slots:
    void finds_top_n_peaks_in_synthetic_spectrum()
    {
        QVector<float> bins(1024, -100.0f);
        bins[100] = -30.0f;  // strongest
        bins[300] = -50.0f;  // 2nd
        bins[500] = -40.0f;  // 3rd... but actually 2nd

        PeakBlobDetector det;
        det.setCount(3);
        det.update(bins, /*filterLowBin=*/0, /*filterHighBin=*/1024);

        const auto blobs = det.blobs();
        QCOMPARE(blobs.size(), 3);
        QCOMPARE(blobs[0].binIndex, 100);   // -30
        QCOMPARE(blobs[1].binIndex, 500);   // -40
        QCOMPARE(blobs[2].binIndex, 300);   // -50
    }

    void inside_filter_excludes_outside_bins()
    {
        QVector<float> bins(1024, -100.0f);
        bins[50]  = -30.0f;   // outside passband
        bins[600] = -40.0f;   // inside

        PeakBlobDetector det;
        det.setCount(2);
        det.setInsideFilterOnly(true);
        det.update(bins, /*filterLowBin=*/100, /*filterHighBin=*/900);

        QCOMPARE(det.blobs().size(), 1);
        QCOMPARE(det.blobs()[0].binIndex, 600);
    }

    void hold_persists_then_decays()
    {
        QVector<float> bins(256, -100.0f);
        bins[100] = -40.0f;

        PeakBlobDetector det;
        det.setCount(1);
        det.setHoldEnabled(true);
        det.setHoldMs(500);
        det.setHoldDrop(true);
        det.setFallDbPerSec(6.0);
        det.update(bins, 0, 256);

        // After 100 ms — within hold window, value should be unchanged
        det.tickFrame(/*fps=*/30, /*elapsedMs=*/100);
        QCOMPARE(det.blobs()[0].max_dBm, -40.0f);

        // After 600 ms — past hold window, decay begins
        for (int i = 0; i < 14; ++i) det.tickFrame(30, 35);
        // Faded a bit — decay math: -40 - (6.0/30 * 14) = -40 - 2.8 = -42.8
        QVERIFY(det.blobs()[0].max_dBm < -42.0f);
    }
};
```

- [ ] **Step 2: Implement `PeakBlobDetector`**

```cpp
// src/gui/spectrum/PeakBlobDetector.h
#pragma once
#include <QVector>

namespace NereusSDR {

struct PeakBlob {
    int   binIndex;
    float max_dBm;
    qint64 timeMs;       // last update time (for hold)
};

class PeakBlobDetector {
public:
    void setEnabled(bool e)             { m_enabled = e; }
    void setCount(int n)                { m_count = n; }
    void setInsideFilterOnly(bool i)    { m_insideOnly = i; }
    void setHoldEnabled(bool h)         { m_holdEnabled = h; }
    void setHoldMs(int ms)              { m_holdMs = ms; }
    void setHoldDrop(bool d)            { m_holdDrop = d; }
    void setFallDbPerSec(double r)      { m_fallDbPerSec = r; }

    /// Update blob list from current spectrum bins. filterLowBin/HighBin
    /// constrain detection if insideFilterOnly is true.
    void update(const QVector<float>& bins, int filterLowBin, int filterHighBin);

    /// Per-frame tick: applies decay logic if past hold window.
    void tickFrame(int fps, int elapsedMs);

    bool enabled() const                { return m_enabled; }
    int  count() const                  { return m_count; }
    const QVector<PeakBlob>& blobs() const { return m_blobs; }

private:
    bool   m_enabled       = false;
    int    m_count         = 5;
    bool   m_insideOnly    = false;
    bool   m_holdEnabled   = false;
    int    m_holdMs        = 500;
    bool   m_holdDrop      = false;
    double m_fallDbPerSec  = 6.0;       // Thetis default
    QVector<PeakBlob>      m_blobs;
    qint64 m_currentTimeMs = 0;
};

}
```

`update()`:
```cpp
void PeakBlobDetector::update(const QVector<float>& bins, int loBin, int hiBin)
{
    if (!m_enabled) return;

    const int low  = m_insideOnly ? loBin : 1;
    const int high = m_insideOnly ? hiBin : bins.size() - 1;

    // Find local maxima
    QVector<PeakBlob> found;
    for (int i = low; i < high; ++i) {
        if (bins[i] > bins[i-1] && bins[i] > bins[i+1]) {
            found.append({i, bins[i], m_currentTimeMs});
        }
    }

    // Sort descending by dBm
    std::sort(found.begin(), found.end(),
              [](const PeakBlob& a, const PeakBlob& b) {
                  return a.max_dBm > b.max_dBm;
              });

    // Take top N
    if (found.size() > m_count) found.resize(m_count);

    // Merge with existing blobs (preserve hold timestamps for matching bins)
    for (auto& nb : found) {
        for (const auto& ob : m_blobs) {
            if (ob.binIndex == nb.binIndex && ob.max_dBm > nb.max_dBm) {
                // Keep older max if hold is enabled
                if (m_holdEnabled) {
                    nb.max_dBm = ob.max_dBm;
                    nb.timeMs  = ob.timeMs;
                }
            }
        }
    }
    m_blobs = found;
}
```

`tickFrame()`:
```cpp
void PeakBlobDetector::tickFrame(int fps, int elapsedMs)
{
    m_currentTimeMs += elapsedMs;
    if (!m_enabled || !m_holdEnabled) return;

    for (auto& b : m_blobs) {
        const qint64 ageMs = m_currentTimeMs - b.timeMs;
        if (ageMs > m_holdMs) {
            // From Thetis Display.cs:5483 [v2.10.3.13]
            //   entry.max_dBm -= m_dBmPerSecondPeakBlobFall / (float)m_nFps;
            if (m_holdDrop) {
                b.max_dBm -= static_cast<float>(m_fallDbPerSec / fps);
            }
        }
    }
}
```

- [ ] **Step 3: Wire into SpectrumWidget**

Render path: QPainter overlay. For each blob:
- Compute (x, y) = (binToX(blob.binIndex), dBmToY(blob.max_dBm))
- Draw ellipse with `m_blobPen` (default OrangeRed per Thetis Display.cs:8434)
- Draw text "f1 dBm" with `m_blobTextPen` (default Chartreuse per Thetis Display.cs:8435)

- [ ] **Step 4: Run tests, expect PASS.**

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(display): PeakBlobDetector class + ellipse/text rendering on SpectrumWidget"
```

---

### Task 2.7: Wire SpectrumPeaksPage controls to detector + trace (Section 2C)

**Files:**
- Modify: `src/gui/setup/SpectrumPeaksPage.cpp` — connect signals
- Modify: `src/models/RadioModel.cpp` — broadcast settings to all SpectrumWidget instances

- [ ] **Step 1: For each control on SpectrumPeaksPage**, connect `valueChanged`/`toggled` → `AppSettings::setValue` → `RadioModel::setActivePeakHoldX(...)` → `SpectrumWidget::activePeakHoldTrace().setX(...)`.

- [ ] **Step 2: Default values** match Thetis (per design Section 1C).

- [ ] **Step 3: ColorSwatchButtons** — initial color from AppSettings (defaults `OrangeRed` and `Chartreuse`); changes update both AppSettings and PeakBlobDetector pen.

- [ ] **Step 4: Add a manual smoke test**

Run NereusSDR, navigate to Setup → Display → Spectrum Peaks, toggle Active Peak Hold on, see the peak trace appear on the spectrum.

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(display): wire SpectrumPeaksPage controls to ActivePeakHold + PeakBlob render passes"
```

---

### Task 2.8: Waterfall Defaults — remove W5, add NF-AGC + Stop-on-TX + Calculated Delay + Copy button (Section 2D)

**Files:**
- Modify: `src/gui/setup/DisplaySetupPages.cpp` — WaterfallDefaultsPage edits
- Modify: `src/gui/SpectrumWidget.cpp` — render path changes
- Test: `tests/tst_waterfall_*.cpp` (covered in 2.10)

- [ ] **Step 1: Remove W5 control + AppSettings key + render hook.**

In `WaterfallDefaultsPage`: delete `m_reverseScrollCheck` and its layout entry. In SpectrumWidget delete the `setWfReverseScroll` setter and any code using `m_wfReverseScroll`. The migration deletes the AppSettings key.

- [ ] **Step 2: Add NF-AGC group** (`WaterfallNFAGCEnabled` + `WaterfallAGCOffsetDb`) with controls + AppSettings persistence + SpectrumWidget setters.

- [ ] **Step 3: Add `WaterfallStopOnTx` checkbox** — wires to SpectrumWidget which on TX state pauses pushWaterfallRow().

- [ ] **Step 4: Add Calculated Delay readout** — live label updated whenever update period or buffer size changes.

- [ ] **Step 5: Add Copy button** — "Copy spectrum min/max → waterfall thresholds" — reads G2/G3, writes W1/W2.

- [ ] **Step 6: Build and smoke test.**

- [ ] **Step 7: Commit**

```bash
git commit -m "feat(display): waterfall NF-AGC + Stop-on-TX + Calculated-Delay readout + Copy button; W5 removed"
```

---

### Task 2.9: Grid & Scales — NF-aware grid + Copy button (Section 2E)

**Files:**
- Modify: `src/gui/setup/DisplaySetupPages.cpp` — GridScalesPage extensions
- Modify: `src/gui/SpectrumWidget.cpp` — NF-aware grid math
- Modify: `src/dsp/NoiseFloorEstimator.cpp` — add `prime(double)` method
- Test: `tests/tst_nf_aware_grid.cpp`

- [ ] **Step 1: Write tests**

```cpp
class TestNFAwareGrid : public QObject {
    Q_OBJECT
private slots:
    void offset_shifts_grid_min_below_nf()
    {
        SpectrumWidget w;
        w.setAdjustGridMinToNoiseFloor(true);
        w.setNFOffsetGridFollow(-10);
        w.setMaintainNFAdjustDelta(false);

        const float nf = -100.0f;
        w.applyNFAwareGrid(nf);

        QCOMPARE(w.gridMin(), -110);  // -100 + (-10)
    }

    void maintain_delta_moves_grid_max_too()
    {
        SpectrumWidget w;
        w.setGridMin(-130);
        w.setGridMax(-30);
        // delta = 100 dB
        w.setAdjustGridMinToNoiseFloor(true);
        w.setNFOffsetGridFollow(0);
        w.setMaintainNFAdjustDelta(true);

        const float nf = -90.0f;
        w.applyNFAwareGrid(nf);

        QCOMPARE(w.gridMin(), -90);   // = nf + 0
        QCOMPARE(w.gridMax(), 10);    // delta preserved (100)
    }
};
```

- [ ] **Step 2: Add NF-aware grid group on GridScalesPage** (3 controls).

- [ ] **Step 3: Implement `SpectrumWidget::applyNFAwareGrid(float nf)`**

```cpp
void SpectrumWidget::applyNFAwareGrid(float nf)
{
    if (!m_adjustGridMinToNoiseFloor) return;

    const int oldMin = m_gridMin;
    const int oldMax = m_gridMax;
    const int delta = oldMax - oldMin;

    const int newMin = qRound(nf + m_nfOffsetGridFollow);
    setGridMin(newMin);

    if (m_maintainNFAdjustDelta) {
        setGridMax(newMin + delta);
    }
}
```

Connect: NoiseFloorEstimator's `valueChanged(float)` signal → `applyNFAwareGrid(value)`.

- [ ] **Step 4: Add `NoiseFloorEstimator::prime(double)` method**

```cpp
void NoiseFloorEstimator::prime(double initialDb)
{
    m_estimate = initialDb;
    m_history.clear();
    m_history.append(initialDb);
    emit valueChanged(static_cast<float>(initialDb));
}
```

- [ ] **Step 5: Add Copy button** — waterfall thresholds → grid min/max.

- [ ] **Step 6: Run tests, expect PASS.**

- [ ] **Step 7: Commit**

```bash
git commit -m "feat(display): NF-aware grid (auto-track noise floor) + Copy button + NoiseFloorEstimator::prime()"
```

---

### Task 2.10: Per-band NF-estimate priming (Sections 2E + 6A)

**Files:**
- Modify: `src/models/PanadapterModel.h/.cpp` — extend BandGridSettings + persistence
- Test: `tests/tst_per_band_nf_priming.cpp`

- [ ] **Step 1: Write tests**

```cpp
class TestPerBandNFPriming : public QObject {
    Q_OBJECT
private slots:
    void prime_applies_stored_nf_on_band_change()
    {
        PanadapterModel pan;
        pan.setBandNFEstimate(Band::B40m, -110.0f);
        pan.setBandNFEstimate(Band::B6m, -135.0f);

        // Switch to 40m → estimator should be primed to -110
        pan.setCenterFrequency(7150000);  // 40m
        QCOMPARE(pan.noiseFloorEstimator()->value(), -110.0f);

        // Switch to 6m
        pan.setCenterFrequency(50300000);
        QCOMPARE(pan.noiseFloorEstimator()->value(), -135.0f);
    }

    void save_fires_after_settle_window()
    {
        PanadapterModel pan;
        pan.setCenterFrequency(7150000);
        // Feed 60 samples of stable -100 dB (2 seconds at 30 fps)
        for (int i = 0; i < 60; ++i) pan.noiseFloorEstimator()->push(-100.0f);
        // Settle detector fires save
        QCOMPARE(pan.bandNFEstimate(Band::B40m), -100.0f);
    }
};
```

- [ ] **Step 2: Extend `BandGridSettings`**

```cpp
struct BandGridSettings {
    int   dbMax            = -40;
    int   dbMin            = -140;
    float clarityFloor     = std::numeric_limits<float>::quiet_NaN();
    float bandNFEstimate   = std::numeric_limits<float>::quiet_NaN();   // NEW
};
```

- [ ] **Step 3: Add per-band NF accessors in PanadapterModel**

```cpp
float bandNFEstimate(Band b) const {
    return m_perBandGrid.value(b).bandNFEstimate;
}

void setBandNFEstimate(Band b, float nf) {
    m_perBandGrid[b].bandNFEstimate = nf;
    AppSettings::instance().setValue(
        QStringLiteral("DisplayBandNFEstimate_") + bandKeyName(b),
        nf
    );
}
```

- [ ] **Step 4: Wire band-change to prime estimator**

In `PanadapterModel::setCenterFrequency(qint64 hz)`, after computing new `Band b`:
```cpp
if (b != m_currentBand) {
    m_currentBand = b;
    const float storedNF = bandNFEstimate(b);
    if (std::isfinite(storedNF)) {
        m_nfEstimator->prime(storedNF);
    }
    emit bandChanged(b);
}
```

- [ ] **Step 5: Add settle detector** — track variance of NF estimator over 2-second window; when variance < 1 dB, save current NF to per-band slot.

```cpp
// In PanadapterModel
void onNoiseFloorChanged(float nf)
{
    m_nfHistory.append({QDateTime::currentMSecsSinceEpoch(), nf});
    // Trim to 2-second window
    const qint64 cutoff = QDateTime::currentMSecsSinceEpoch() - 2000;
    while (!m_nfHistory.isEmpty() && m_nfHistory.first().t < cutoff) {
        m_nfHistory.removeFirst();
    }
    if (m_nfHistory.size() > 30) {
        const float variance = computeVariance(m_nfHistory);
        if (variance < 1.0f) {
            setBandNFEstimate(m_currentBand, nf);
        }
    }
}
```

- [ ] **Step 6: Persistence — load on startup**

In `PanadapterModel` constructor, after band slots initialized:
```cpp
for (int i = 0; i < 14; ++i) {
    const Band b = static_cast<Band>(i);
    const QString key = QStringLiteral("DisplayBandNFEstimate_") + bandKeyName(b);
    const QVariant v = AppSettings::instance().value(key);
    if (v.isValid()) m_perBandGrid[b].bandNFEstimate = v.toFloat();
}
```

- [ ] **Step 7: Add the `// NereusSDR-original — no Thetis equivalent.` marker** above all the per-band NF code paths.

- [ ] **Step 8: Run tests, expect PASS.**

- [ ] **Step 9: Commit**

```bash
git commit -m "feat(display): per-band NF-estimate priming (NereusSDR-original)"
```

---

### Task 2.11: Clarity / NF-aware grid coexistence test

**Files:**
- Test: `tests/tst_clarity_nf_grid_coexistence.cpp`

- [ ] **Step 1: Write coexistence test**

```cpp
class TestClarityNFGridCoexistence : public QObject {
    Q_OBJECT
private slots:
    void both_enabled_no_oscillation()
    {
        RadioModel model;
        model.startWithMockHardware();

        // Enable both Clarity and NF-aware grid
        model.clarityController()->setEnabled(true);
        model.spectrumWidget()->setAdjustGridMinToNoiseFloor(true);

        // Feed 10 seconds of synthetic spectrum data
        // Clarity adjusts waterfall thresholds, NF-grid adjusts grid bounds
        for (int i = 0; i < 300; ++i) {
            model.feedSyntheticSpectrum(/*nf=*/-100.0f);
            QTest::qWait(33);
        }

        // Verify neither feature has driven the other into oscillation
        QVERIFY(!model.clarityController()->isOscillating());
        QVERIFY(!model.spectrumWidget()->gridIsOscillating());
    }
};
```

- [ ] **Step 2: Implement smoothness checks** in ClarityController and SpectrumWidget (track value change rate; flag oscillation if rapid sign changes >5 in 1 second).

- [ ] **Step 3: Run test, expect PASS.**

- [ ] **Step 4: Commit**

```bash
git commit -m "test(display): Clarity + NF-aware grid coexistence (no oscillation)"
```

---

### Task 2.12: Smoke test for full RX1 page set

Manual check before moving to Phase 3.

- [ ] **Step 1: Build**

Run: `cmake --build build -j`

- [ ] **Step 2: Launch and verify all RX1 pages render**

- [ ] **Step 3: Toggle all new controls in turn, verify visual response.**

- [ ] **Step 4: Commit any small fixes found.**

---

## Phase 3 — Display-General fold (Cluster 3)

**Independent of Phase 1.** Can interleave with Phase 2.

### Task 3.1: MultimeterPage skeleton + MeterPoller config

**Files:**
- Create: `src/gui/setup/MultimeterPage.h/.cpp`
- Modify: `src/gui/meters/MeterPoller.h/.cpp` — configurable interval + averaging
- Modify: `src/gui/SetupDialog.cpp` — register new page
- Test: `tests/tst_multimeter_timing.cpp`

- [ ] **Step 1: Write `MultimeterPage` skeleton** — 8 multimeter controls + 1 unit-mode combo + 2 signal-history controls + cross-link button.

- [ ] **Step 2: Make `MeterPoller` interval + averaging window configurable** — currently hardcoded 100ms.

```cpp
void MeterPoller::setIntervalMs(int ms) { m_timer.setInterval(ms); }
void MeterPoller::setAverageWindow(int samples) { m_avgWindow = samples; }
```

- [ ] **Step 3: Write timing test**

```cpp
class TestMultimeterTiming : public QObject {
    Q_OBJECT
private slots:
    void poll_interval_changes_when_set()
    {
        MeterPoller poller;
        poller.setIntervalMs(50);
        QCOMPARE(poller.interval(), 50);
    }
};
```

- [ ] **Step 4: Run, expect PASS.**

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(display): MultimeterPage skeleton + configurable MeterPoller"
```

---

### Task 3.2: MeterItem unit-mode fan-out (S/dBm/µV)

**Files:**
- Modify: `src/gui/meters/MeterItem.h/.cpp` — add unit-mode awareness
- Modify: `src/gui/meters/{BarItem, TextItem, SignalTextItem, NeedleItem, HistoryGraphItem}.cpp` — implement unit-aware display
- Test: `tests/tst_multimeter_unit_conversion.cpp`

- [ ] **Step 1: Write conversion test**

```cpp
class TestMultimeterUnitConversion : public QObject {
    Q_OBJECT
private slots:
    void s_meter_units_format_correctly()
    {
        // S9 = -73 dBm at HF (per IARU)
        QCOMPARE(MeterItem::formatValue(-73.0f, MeterUnit::S),    QString("S9"));
        QCOMPARE(MeterItem::formatValue(-79.0f, MeterUnit::S),    QString("S8"));
        QCOMPARE(MeterItem::formatValue(-43.0f, MeterUnit::S),    QString("S9+30"));
    }

    void dbm_units_format_with_decimal()
    {
        QCOMPARE(MeterItem::formatValue(-73.5f, MeterUnit::dBm),  QString("-73.5"));
    }

    void uv_units_compute_from_dbm()
    {
        // -73 dBm @ 50Ω → 50 µV approximately
        const QString s = MeterItem::formatValue(-73.0f, MeterUnit::uV);
        QVERIFY(s.startsWith("50"));
    }
};
```

- [ ] **Step 2: Add `MeterUnit` enum and `formatValue()` static**

```cpp
// In MeterItem.h
enum class MeterUnit { S, dBm, uV };
static QString formatValue(float dBm, MeterUnit unit, bool decimal = true);
```

Implementation in `MeterItem.cpp` per IARU S-meter scale at HF, dBm with optional decimal, µV via `pow(10, dBm/20) * sqrt(50 * 0.001) * 1e6`.

- [ ] **Step 3: Update each MeterItem subclass** to read `m_unitMode` and call `formatValue()` for display.

- [ ] **Step 4: Run tests, expect PASS.**

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(meters): MeterItem unit-mode fan-out (S / dBm / µV) across all subclasses"
```

---

### Task 3.3: Signal History — HistoryGraphItem duration setting

**Files:**
- Modify: `src/gui/meters/HistoryGraphItem.cpp` — duration setting
- Test: `tests/tst_signal_history.cpp`

- [ ] **Step 1: Write test** — verify history buffer length scales with duration.

- [ ] **Step 2: Add `setDurationMs(int)` method to HistoryGraphItem.**

- [ ] **Step 3: Wire MultimeterPage's signal-history controls to all HistoryGraphItem instances.**

- [ ] **Step 4: Run test, expect PASS.**

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(meters): HistoryGraphItem duration setting + MultimeterPage wire-up"
```

---

### Task 3.4: SmallModeFilteronVFOs — Appearance → MeterStylesPage

**Files:**
- Modify: `src/gui/setup/AppearanceSetupPages.cpp` — add VFO Flag group on MeterStylesPage
- Modify: `src/gui/VfoWidget.cpp` — honor the flag

- [ ] **Step 1: Add "VFO Flag" group box** to MeterStylesPage with the small-mode-filter checkbox.

- [ ] **Step 2: Add AppSettings persistence** under `AppearanceSmallModeFilterOnVfos`.

- [ ] **Step 3: Wire to VfoWidget filter-display rendering.**

- [ ] **Step 4: Build + smoke test.**

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(appearance): SmallModeFilteronVFOs in MeterStylesPage VFO Flag group"
```

---

### Task 3.5: Diagnostics → Logging & Performance rename + 3 controls

**Files:**
- Modify: `src/gui/setup/DiagnosticsSetupPages.cpp/.h` — rename + new Performance group

- [ ] **Step 1: Rename `LoggingPage` label** in the Setup tree from "Logging" to "Logging & Performance".

- [ ] **Step 2: Add "Performance" group box** with:
  - `chkSpecWarningLEDRenderDelay` → wire to SpectrumWidget render-delay monitor
  - `chkSpecWarningLEDGetPixels` → wire to SpectrumWidget pixel-fetch monitor
  - `chkPurgeBuffers` → wire to FFTEngine buffer reset

- [ ] **Step 3: Build + smoke test.**

- [ ] **Step 4: Commit**

```bash
git commit -m "feat(diagnostics): Logging & Performance page rename + Spec Warning LEDs + Purge Buffers"
```

---

### Task 3.6: ANAN-8000DLE volts/amps + CPU meter rate

**Files:**
- Modify: `src/gui/setup/hardware/RadioInfoTab.cpp` — add cap-gated volts/amps toggle
- Modify: `src/gui/setup/GeneralOptionsPage.cpp` — add CPU meter rate spinbox

- [ ] **Step 1: ANAN-8000DLE volts/amps toggle**

In RadioInfoTab, add a checkbox visible only when `caps.is8000DLE`. Persists `HardwareAnan8000DleShowVoltsAmps`. Connects to chrome titlebar's volts/amps widget visibility.

- [ ] **Step 2: CPU meter rate spinbox** in GeneralOptionsPage. Range 1-30 Hz. Persists `GeneralCpuMeterUpdateRateHz`. Connects to chrome titlebar's CPU update timer.

- [ ] **Step 3: Build + smoke test.**

- [ ] **Step 4: Commit**

```bash
git commit -m "feat(hardware,general): ANAN-8000DLE volts/amps toggle + CPU meter rate spinbox"
```

---

## Phase 4 — DSP-Options (Cluster 4)

**Depends on Phase 1.** ~6 tasks.

### Task 4.1: DspOptionsPage skeleton + 18 controls

**Files:**
- Create: `src/gui/setup/DspOptionsPage.h/.cpp`
- Modify: `src/gui/SetupDialog.cpp` — register page in DSP category

- [ ] **Step 1: Write `DspOptionsPage` skeleton** with 4 group boxes (Buffer Size, Filter Size, Filter Type, Impulse Cache) + standalone HighRes checkbox + Time-to-last-change readout label + 3 warning icons.

- [ ] **Step 2: Verify Thetis combo enum values** before populating combos. Run:

```bash
grep -A 50 "comboDSPPhoneBufSize" /Users/j.j.boyd/Thetis/Project\ Files/Source/Console/setup.designer.cs
```

Capture exact Items lists; populate NereusSDR combos identically.

- [ ] **Step 3: Connect each combo's `currentIndexChanged` to AppSettings persistence.**

- [ ] **Step 4: Subscribe to `RadioModel::dspChangeMeasured(qint64 ms)` to update the readout label.**

- [ ] **Step 5: Build + smoke test.**

- [ ] **Step 6: Commit**

```bash
git commit -m "feat(dsp): DspOptionsPage skeleton + 18 controls + warning icon placeholders"
```

---

### Task 4.2: Per-mode live-apply behavior

**Files:**
- Modify: `src/core/dsp/RxChannel.cpp` — `onModeChanged()` reads per-mode AppSettings keys
- Modify: `src/core/dsp/TxChannel.cpp` — same
- Modify: `src/models/SliceModel.cpp` — emit `modeChanged` signal
- Test: `tests/tst_dsp_options_per_mode_apply.cpp`

- [ ] **Step 1: Write test**

```cpp
class TestDspOptionsPerModeApply : public QObject {
    Q_OBJECT
private slots:
    void changing_phone_buffer_while_in_cw_does_not_rebuild()
    {
        RadioModel model;
        model.startWithMockHardware();
        model.sliceModel(0)->setMode(SliceModel::Mode::CWU);

        QSignalSpy spy(&model, &RadioModel::dspChangeMeasured);
        AppSettings::instance().setValue("DspOptionsBufferSizePhone", 512);

        // Should NOT trigger rebuild (we're in CW, not Phone)
        QCOMPARE(spy.count(), 0);

        // Switching to Phone now should trigger rebuild with the new value
        model.sliceModel(0)->setMode(SliceModel::Mode::USB);
        QCOMPARE(spy.count(), 1);
    }
};
```

- [ ] **Step 2: Implement `RxChannel::onModeChanged()`**

```cpp
void RxChannel::onModeChanged(SliceModel::Mode newMode)
{
    auto& s = AppSettings::instance();
    const QString modeKey = modeKeyName(newMode);

    ChannelConfig cfg = m_currentConfig;
    cfg.bufferSize  = s.value("DspOptionsBufferSize" + modeKey, cfg.bufferSize).toInt();
    cfg.filterSize  = s.value("DspOptionsFilterSize" + modeKey, cfg.filterSize).toInt();
    cfg.filterType  = s.value("DspOptionsFilterType" + modeKey + "Rx", cfg.filterType).toInt();

    if (cfg.bufferSize != m_currentConfig.bufferSize ||
        cfg.filterSize != m_currentConfig.filterSize ||
        cfg.filterType != m_currentConfig.filterType) {
        rebuild(cfg);
    }
}
```

- [ ] **Step 3: Connect `SliceModel::modeChanged` → `RxChannel::onModeChanged`** in RadioModel.

- [ ] **Step 4: For DspOptionsPage** — when user changes Phone-* combo, check current slice mode. If matches Phone, trigger rebuild now; otherwise just persist.

- [ ] **Step 5: Run test, expect PASS.**

- [ ] **Step 6: Commit**

```bash
git commit -m "feat(dsp): per-mode buffer/filter/filter-type live-apply via Phase 1 rebuild"
```

---

### Task 4.3: Filter Impulse Cache wiring

**Files:**
- Modify: `src/core/dsp/WdspEngine.cpp` — wire CacheImpulse + SaveRestore
- Modify: `src/core/AppSettings.cpp` — load on startup, apply at channel-create time

- [ ] **Step 1: Verify Thetis API**

```bash
grep -n "CacheImpulse\|cache_impulse\|SaveRestoreCacheImpulse" /Users/j.j.boyd/Thetis/Project\ Files/Source/Console/dsp.cs /Users/j.j.boyd/Thetis/Project\ Files/Source/Console/cmaster.cs
```

Capture exact WDSP P/Invoke names + parameters.

- [ ] **Step 2: Implement in `WdspEngine::createChannel()`**

```cpp
if (cfg.cacheImpulse) {
    SetWDSPCacheImpulse(id, 1);  // verify name from Step 1
}
if (cfg.cacheImpulseSaveRestore) {
    LoadWDSPImpulseFromDisk(id, "wdsp_impulse.cache");
}
```

On app shutdown: if save-restore enabled, dump cache to disk via `SaveWDSPImpulseToDisk()`.

- [ ] **Step 3: Wire DspOptionsPage checkboxes** to AppSettings + RadioModel broadcast on change.

- [ ] **Step 4: Build + manual smoke test (verify cache file appears at `~/.config/NereusSDR/wdsp_impulse.cache` when save-restore enabled).**

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(dsp): Filter Impulse Cache toggles + WDSP cache load/save plumbing"
```

---

### Task 4.4: HighResolution Filter Characteristics — full impl

**Files:**
- Modify: `src/gui/meters/FilterDisplayItem.h/.cpp` — add high-res mode
- Modify: `src/gui/setup/DspOptionsPage.cpp` — checkbox connects to broadcast
- Test: `tests/tst_filter_high_resolution.cpp` (extend with integration)

- [ ] **Step 1: Add `setHighResolution(bool)` to FilterDisplayItem**

```cpp
// FilterDisplayItem.h
void setHighResolution(bool h);
void bindRxChannel(RxChannel* ch);     // for filterResponseMagnitudes()
```

- [ ] **Step 2: Implement high-res render path**

When ON:
```cpp
const QVector<float> mag = m_rxChannel->filterResponseMagnitudes(rect.width());
for (int x = 0; x < rect.width(); ++x) {
    const int y = rect.bottom() - static_cast<int>(mag[x] * rect.height() / 80.0);  // -80..0 dB range
    // draw line segment
}
```

When OFF: existing simplified box-shape passband (no regression).

- [ ] **Step 3: Broadcast via `ContainerManager`** — DspOptionsPage checkbox emits signal connected to all bound FilterDisplayItem instances.

- [ ] **Step 4: Run integration test, expect PASS.**

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(dsp): high-resolution filter characteristics + FilterDisplayItem high-res mode"
```

---

### Task 4.5: Warning icon logic

**Files:**
- Modify: `src/gui/setup/DspOptionsPage.cpp` — port Thetis validity rules
- Test: `tests/tst_dsp_options_warning_logic.cpp`

- [ ] **Step 1: Port Thetis validity rules**

```bash
grep -n "pbWarningBufferSize.Visible\|pbWarningFilterSize.Visible\|pbWarningBufferType.Visible" /Users/j.j.boyd/Thetis/Project\ Files/Source/Console/setup.cs
```

Capture each condition; port verbatim.

- [ ] **Step 2: Write tests** for each condition (e.g., "buffer > filter is invalid").

- [ ] **Step 3: Implement** as `recomputeWarnings()` method called on every combo `_changed`.

```cpp
void DspOptionsPage::recomputeWarnings()
{
    // From Thetis setup.cs:NNNN [v2.10.3.13]
    const int phoneBuf  = m_phoneBuf->currentData().toInt();
    const int phoneFilt = m_phoneFilt->currentData().toInt();

    m_bufWarning->setVisible(phoneBuf > phoneFilt);
    m_bufWarning->setToolTip(tr("Buffer size cannot exceed filter size"));
    // … repeat for other modes
}
```

- [ ] **Step 4: Run tests, expect PASS.**

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(dsp): warning icon logic ported from Thetis validity rules"
```

---

### Task 4.6: Time-to-last-change readout

**Files:**
- Modify: `src/gui/setup/DspOptionsPage.cpp` — subscribe to RadioModel signal

- [ ] **Step 1: Connect signal in DspOptionsPage**

```cpp
connect(m_model, &RadioModel::dspChangeMeasured,
        this, [this](qint64 ms) {
    m_timeReadout->setText(tr("Time to last change: %1 ms").arg(ms));
});
```

For "stored not applied" case (per-mode mismatch), set:
```cpp
m_timeReadout->setText(tr("Time to last change: — (stored, not applied)"));
```

- [ ] **Step 2: Build + smoke test** — change a Phone Buffer Size while in Phone mode, observe readout updates.

- [ ] **Step 3: Commit**

```bash
git commit -m "feat(dsp): time-to-last-change readout connected to dspChangeMeasured signal"
```

---

## Phase 5 — Migration + verification matrix + CHANGELOG (Cluster 5)

### Task 5.1: SettingsSchemaVersion + v0.3.0 migration

**Files:**
- Modify: `src/core/AppSettings.cpp` — add `ensureSettingsAtVersion()` method
- Test: `tests/tst_settings_migration_v0_3_0.cpp`

- [ ] **Step 1: Write tests**

```cpp
class TestSettingsMigrationV03 : public QObject {
    Q_OBJECT
private slots:
    void retired_keys_removed_on_migration()
    {
        AppSettings::instance().setValue("SettingsSchemaVersion", "2");
        AppSettings::instance().setValue("DisplayAverageMode", "Recursive");
        AppSettings::instance().setValue("DisplayPeakHold", "True");
        AppSettings::instance().setValue("DisplayReverseWaterfallScroll", "True");

        AppSettings::instance().ensureSettingsAtVersion(3);

        QVERIFY(!AppSettings::instance().contains("DisplayAverageMode"));
        QVERIFY(!AppSettings::instance().contains("DisplayPeakHold"));
        QVERIFY(!AppSettings::instance().contains("DisplayReverseWaterfallScroll"));
        QCOMPARE(AppSettings::instance().value("SettingsSchemaVersion").toInt(), 3);
    }

    void migration_idempotent()
    {
        AppSettings::instance().setValue("SettingsSchemaVersion", "3");
        AppSettings::instance().ensureSettingsAtVersion(3);  // no-op
        AppSettings::instance().ensureSettingsAtVersion(3);  // still no-op
        QCOMPARE(AppSettings::instance().value("SettingsSchemaVersion").toInt(), 3);
    }

    void fresh_install_writes_version()
    {
        // Empty AppSettings
        AppSettings::instance().clear();
        AppSettings::instance().ensureSettingsAtVersion(3);
        QCOMPARE(AppSettings::instance().value("SettingsSchemaVersion").toInt(), 3);
    }
};
```

- [ ] **Step 2: Implement migration**

```cpp
// AppSettings.cpp
void AppSettings::ensureSettingsAtVersion(int currentVersion)
{
    const int storedVersion = value("SettingsSchemaVersion", "0").toInt();

    if (storedVersion < 3 && currentVersion >= 3) {
        qCInfo(lcSettings) << "Migrating settings to schema v3 (NereusSDR v0.3.0)";
        remove("DisplayAverageMode");
        remove("DisplayPeakHold");
        remove("DisplayPeakHoldDelayMs");
        remove("DisplayReverseWaterfallScroll");
        qCInfo(lcSettings) << "Settings migration complete";
    }

    setValue("SettingsSchemaVersion", currentVersion);
}
```

- [ ] **Step 3: Call from main()**

```cpp
// main.cpp, after AppSettings::instance() initialization
AppSettings::instance().ensureSettingsAtVersion(3);
```

- [ ] **Step 4: Run tests, expect PASS.**

- [ ] **Step 5: Audit callsites for retired keys**

```bash
grep -rn "DisplayAverageMode\|DisplayPeakHold\|DisplayReverseWaterfallScroll" src/ tests/ docs/
```

Expected zero hits after refactor (other than the migration code itself + test); fix any stragglers.

- [ ] **Step 6: Commit**

```bash
git commit -m "feat(settings): SettingsSchemaVersion key + v0.3.0 migration (retired display keys)"
```

---

### Task 5.2: Verification matrix files (7 new)

**Files:**
- Create: 7 verification matrix files under `docs/architecture/phase3g8-verification/`

- [ ] **Step 1: Create matrix file template**

Each file follows existing 3G-8 row format:

| Control | AppSettings key | Default | Behavior on change | Screenshot |
|---|---|---|---|---|
| ... | ... | ... | ... | filename.png |

- [ ] **Step 2: Author each file**

- `spectrum-defaults-extensions.md` — 9 rows (ShowMHzOnCursor, ShowBinWidth, ShowNoiseFloor + position, DispNormalize, ShowPeakValueOverlay + position + delay + color, GetMonitorHz, Decimation)
- `spectrum-peaks.md` — 12 rows (Active Peak Hold ×5 + Peak Blobs ×7)
- `waterfall-defaults-changes.md` — 6 rows (NF-AGC enable, AGC offset, Stop-on-TX, Calculated Delay, Copy button, Detector + Averaging)
- `grid-scales-extensions.md` — 4 rows (AdjustGridMinToNoiseFloor, NFOffsetGridFollow, MaintainNFAdjustDelta, Copy button)
- `multimeter.md` — 13 rows (8 multimeter + unit-mode + signal-history)
- `dsp-options.md` — 22 rows (4 buffer + 4 filter-size + 7 filter-type + 2 cache + 1 hi-res + 1 readout + 3 warnings)
- `infrastructure-live-apply.md` — 6 rows (sample-rate live-apply, active-RX-count live-apply, channel rebuild, audio re-init, dsp-change-timer, glitch budget)

- [ ] **Step 3: Update `phase3g8-verification/README.md`** — add a "v0.3 extensions" section linking the 7 new files.

- [ ] **Step 4: Commit**

```bash
git commit -m "docs(verification): extend phase3g8-verification with 7 new matrix files"
```

---

### Task 5.3: PROVENANCE entries + CHANGELOG

**Files:**
- Modify: `docs/attribution/THETIS-PROVENANCE.md`
- Modify: `CHANGELOG.md`

- [ ] **Step 1: Add PROVENANCE rows** for every new ported file from Phases 1-4 (RxChannel state, ActivePeakHoldTrace, PeakBlobDetector, etc. — anywhere a Thetis cite was added).

- [ ] **Step 2: Author CHANGELOG entry under v0.3.0**

```markdown
## [0.3.0] — 2026-MM-DD

### Added
- **Display → Spectrum Peaks** (new): Active Peak Hold per-bin decay trace and Peak Blobs top-N markers, ported from Thetis `Display.cs` with Thetis-faithful decay math and default colors. Both customizable via ColorSwatchButton.
- **Display → Multimeter** (new): unified multimeter polling/display configuration page with S/dBm/µV unit selector and signal history.
- **Display → Spectrum Defaults**: new "Spectrum Overlays" group with MHz-on-cursor, Bin Width, Noise Floor, Disp Normalize, Peak Value Overlay; plus Get-Monitor-Hz button and decimation wire-up.
- **Display → Waterfall Defaults**: NF-AGC group, Stop-on-TX toggle, Calculated-Delay readout, Copy button (spectrum ↔ waterfall thresholds).
- **Display → Grid & Scales**: noise-floor-aware grid (auto-track grid min to live NF) with optional MaintainDelta to preserve dynamic range.
- **DSP → Options** (new): per-mode buffer size, filter size, filter type combos; impulse cache toggles; high-resolution filter characteristics; live "Time to last change" readout.
- **WDSP channel teardown/rebuild infrastructure**: `RxChannel::rebuild()` / `TxChannel::rebuild()` enables live-apply for sample-rate changes, active-RX-count toggle, and DSP-Options.
- **Per-band noise-floor priming** (NereusSDR-original): smoother visual on band changes; estimator primed from last-seen NF for the new band.

### Changed
- Spectrum + waterfall **averaging mode split** into separate Detector and Averaging method controls (matches Thetis layout).
- Active-RX-count toggle now applies live (no disconnect-reconnect needed).
- Sample-rate change now applies live.

### Display settings migration (v0.3.0 schema bump)
The following settings reset to defaults on first launch of v0.3.0:
- Spectrum / waterfall averaging mode (now split into separate Detector and Averaging method controls)
- Spectrum peak hold (replaced with Active Peak Hold per-bin trace; configure under Setup → Display → Spectrum Peaks)
- Reverse waterfall scroll (removed)

Reconfigure these under Setup → Display.

### Removed
- Display → Reverse Waterfall Scroll (W5).
```

- [ ] **Step 3: Commit**

```bash
git commit -m "docs(changelog): v0.3.0 entry + PROVENANCE rows for ported files"
```

---

### Task 5.4: Final cleanup audit

- [ ] **Step 1: Grep for placeholder cites**

```bash
grep -rn "// From Thetis .*:NNNN" src/
```

Replace any `:NNNN` placeholder with the actual line number.

- [ ] **Step 2: Verify no orphan code**

```bash
grep -rn "DisplayAverageMode\|DisplayPeakHold\|DisplayReverseWaterfallScroll" src/ tests/
# Expected: only in tst_settings_migration_v0_3_0.cpp + AppSettings.cpp migration block
```

- [ ] **Step 3: Run full test suite**

```bash
cmake --build build -j && ctest --test-dir build --output-on-failure
```

Expected: all tests PASS.

- [ ] **Step 4: Run pre-commit hook chain**

```bash
python3 scripts/verify-thetis-headers.py
python3 scripts/verify-inline-tag-preservation.py
python3 scripts/check-new-ports.py
```

All green.

- [ ] **Step 5: Commit any final fixes**

```bash
git commit -m "chore: final cleanup (placeholder cite resolution + audit)"
```

---

## Phase 6 — Polish (review-driven)

After PR review feedback comes in, address items as additional commits with `fix(...)` prefix per the affected cluster. No pre-planned tasks; this section is the placeholder for review-driven work.

Typical pattern:
- Code-review-driven fixes (logic, style)
- Tooltip pass for any unclear new control
- Manual-bench session findings (if not caught in unit tests)

After all polish lands, prepare PR description per design Section 9D.

---

## Manual bench session (merge gate)

Before final merge — JJ runs on:
- **ANAN-G2 primary** (full DDC + DSP capability)
- **HL2 secondary** (smaller config)

Test exercises:

- [ ] Live-apply: change DSP-Options buffer/filter/type with audio playing → no audible glitch beyond ~1 buffer interval (~10-20 ms at 48 kHz)
- [ ] Sample-rate live-apply: 48k → 96k → 192k → 384k → back to 48k while listening
- [ ] Active-RX-count: enable RX2 (if HW supports), verify both panadapters live, disable RX2
- [ ] Per-band NF priming: band-change between quiet (6m) and noisy (40m), verify spectrum doesn't slosh on band change
- [ ] NF-aware grid: enable AdjustGridMinToNoiseFloor, verify grid floats with band-change
- [ ] Active Peak Hold: enable, sweep frequencies, verify peak trace appears with correct decay
- [ ] Peak Blobs: enable, verify top-N peaks marked with ellipse + dBm text
- [ ] DSP-Options "Time to last change" readout: verify updates in real time on each setting change
- [ ] Multimeter unit-mode (S vs dBm vs µV): verify all bound MeterItems update display

If any item fails, fix and add to Phase 6 commits.

---

## Appendix A — Verify-during-impl items reference

These items are deferred from design to grep/source-read at implementation time. Each is called out in the relevant Task above with a verification step. Listed here for cross-reference:

1. **Thetis WDSP detector / averaging API signatures** (Task 2.1 Step 1)
2. **Thetis `DispNormalize` algorithm** (Task 2.3 — reading Display.cs during normalize implementation)
3. **Thetis buffer/filter combo enum values** (Task 4.1 Step 2)
4. **Thetis `pbWarning*.Visible` validity rules** (Task 4.5 Step 1)
5. **`MetisFrameParser` mid-stream RX-count change support** (Task 1.7 Step 1)
6. **`RxChannel` filter-tap accessor existence** (Task 1.5 — check before adding)
7. **NereusSDR peak-text corner readout existence** — locked in Task 2.3 to add the readout if not present (no handwave path)
8. **Thetis WDSP CacheImpulse + SaveRestore P/Invoke names** (Task 4.3 Step 1)

---

## Appendix B — Explicit deferrals

Items spec'd but explicitly out of scope for v0.3 (per design Appendix B):

- Thetis `Display → General` Phase scope group (3 ctrls) — needs display-mode framework, Phase 3F+
- Thetis `Display → General` Scope Mode group (1 ctrl) — same
- Thetis `chkAntiAlias` / `chkAccurateFrameTiming` / `chkVSyncDX` — DirectX-9-era, QRhi handles transparently
- Thetis `SpectrumGridMaxMoxModified` (TX-state grid overrides) — not in v0.3 scope; revisit when transmit display work happens
- DDC reassignment / Diversity / MultiRX2 surface — Phase 3F (panadapter-stack rework dependency)
- IMD3/IMD5 measurements — PeakBlobDetector designed extensible so IMD detection can attach later
- Thetis `console.PeakTextColor` runtime flexibility — hardcoded default `DodgerBlue`, exposed via ColorSwatchButton (NereusSDR pattern)

---

## Sign-off

- **Plan author:** Claude (Opus 4.7) via writing-plans skill
- **Design doc reference:** `docs/architecture/thetis-display-dsp-parity-design.md` (commit `b3a0246`)
- **Implementation owner:** TBD
- **Execution model:** Subagent-driven (recommended) or inline via executing-plans skill
- **Total estimated commits:** ~38 (8 + 12 + 6 + 6 + 4 + N polish)
- **Total estimated calendar:** ~3 weeks
