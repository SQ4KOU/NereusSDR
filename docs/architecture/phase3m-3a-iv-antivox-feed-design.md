# Phase 3M-3a-iv Design: Anti-VOX Cancellation Feed

**Status:** Approved (brainstorm 2026-05-07), ready for `superpowers:writing-plans`.
**Author:** J.J. Boyd (KG4VCF), AI-assisted (Anthropic Claude Code, Opus 4.7).
**Date:** 2026-05-07.
**Upstream stamps captured this session:**
- Thetis `v2.10.3.13` (`@501e3f51`)

**Master design alignment:** Closes the cancellation-feed gap left in 3M-3a-iii (DEXP/VOX). 3M-3a-iii landed the anti-VOX gain and source-toggle controls on `MoxController` and `TransmitModel`, but the actual RX-audio feedback path into the WDSP DEXP detector was never wired. This document specifies the minimal, single-RX path to close that gap, plus a deferred future-work hook for the Thetis `aamix` port that 3F multi-pan will need.

---

## 1. Goal

Make the existing anti-VOX gain control audibly effective on a single-RX setup. Today the user can move the anti-VOX gain slider, persist a value, and toggle the source path. WDSP `DEXP` is created with `antivox_run` plumbing and the `SetAntiVOXRun` / `SetAntiVOXGain` wrappers exist. But because `SendAntiVOXData()` is never called from anywhere in NereusSDR, `antivox_data` is always zeros, so `antivox_level` stays at zero, so the subtraction `asig = avsig - antivox_gain * antivox_level` is a no-op. The slider does nothing.

This phase wires the missing audio feedback path: RX speaker audio (the same buffer that already feeds `AudioEngine::rxBlockReady`) is forwarded to `TxChannel::sendAntiVoxData()`, which calls the WDSP `SendAntiVOXData()` entry point per chunk. With this in place, an operator with VOX engaged and a strong signal in the speaker can set anti-VOX gain to suppress the false-trip while still being triggered by the mic.

The phase also closes a constant-preservation drift (anti-VOX `tau` default is 10 ms in NereusSDR vs. 20 ms in Thetis), exposes `tau` in the existing Setup → Transmit → DEXP/VOX page, and adds the three remaining DEXP setters that 3M-3a-iii left unwrapped (`SetAntiVOXSize`, `SetAntiVOXRate`, `SetAntiVOXDetectorTau`).

## 2. Scope

### In scope

- 4 new TxChannel WDSP wrappers: `setAntiVoxSize(int)`, `setAntiVoxRate(double)`, `setAntiVoxDetectorTau(double)`, `sendAntiVoxData(const float*, int)`.
- 4 new declarations in `src/core/wdsp_api.h` mirroring the dexp.c entry points.
- New `RxDspWorker::antiVoxSampleReady(int sliceId, const QVector<float>&, int)` signal emitted per drained chunk alongside the existing `rxBlockReady` call.
- New `TxWorkerThread::onAntiVoxSamplesReady` queued slot. Gates on a local `m_antiVoxRun` atomic mirroring the existing `setAntiVoxRun(bool)` setter, calls `TxChannel::sendAntiVoxData` when enabled.
- `TransmitModel::antiVoxTauMs` int property, range 1..500 ms, default 20, persisted under AppSettings key `AntiVoxTauMs`.
- `MoxController::setAntiVoxTau(int ms)` slot, `antiVoxDetectorTauRequested(double seconds)` signal, ms→s scaling (`/1000.0`), NaN sentinel for first-call emit.
- `WdspEngine.cpp` constant fix: anti-VOX `tau` default 0.01 → 0.02 to match Thetis `udAntiVoxTau.Value=20` ([v2.10.3.13] `setup.designer.cs:44682-44686`).
- `WdspEngine.cpp` setup-time call to `SetAntiVOXSize(0, rxOutSize)` + `SetAntiVOXRate(0, rxOutRate)` once the RX channel's post-decimation block geometry is known. Replaces the current implicit assumption that TX `inputBufferSize` matches RX `outSize`.
- `RadioModel.cpp` signal wiring: `RxDspWorker::antiVoxSampleReady` → `TxWorkerThread::onAntiVoxSamplesReady` (queued auto-connection).
- `DexpVoxSetupPage` UI: one new control in the existing `grpAntiVOX` group. `QSpinBox` 1..500, default 20, single-step 1, label "Tau (ms)", tooltip "Time-constant used in smoothing Anti-VOX data" verbatim from [v2.10.3.13] `setup.designer.cs:44681`.
- Unit-test additions: `tst_tx_channel_anti_vox` (4 setters, gated on `HAVE_WDSP`), `tst_mox_controller_anti_vox` (extend with `antiVoxTau_*` cases), `tst_rx_dsp_worker_anti_vox` (new, verifies signal payload matches the `rxBlockReady` buffer), `tst_dexp_vox_setup_page` (extend with Tau spinbox round-trip).
- Manual bench-verification matrix at `docs/architecture/phase3m-3a-iv-verification/README.md`. Runs on ANAN-G2 and HL2; verifies anti-VOX gain audibly suppresses speaker-bleed VOX trips at tau=20 ms.

### Out of scope (deferred)

- **`aamix.c/h` port** (Thetis `Project Files/Source/ChannelMaster/aamix.c`, 699 lines, GPLv2-or-later, Warren Pratt NR0V). aamix is the canonical Thetis component for routing per-RX audio at heterogeneous rates into a single anti-VOX stream. It exists because Thetis can have multiple RX feeding a single TX anti-VOX detector, each at its own decimation rate, with per-input enable bits. For single-RX where the lone input feeds at the same rate as the detector, aamix degenerates to a passthrough buffer plus the `SendAntiVOXData` callback. We replicate that degenerate behavior directly. When 3F multi-pan ships and the `SetAntiVOXSourceWhat` mux becomes meaningful (more than one RX, possibly at different rates), aamix gets ported then. **None of this phase's plumbing is throwaway**: the dimension-alignment infrastructure (`SetAntiVOXSize` + `SetAntiVOXRate` driven by RX block geometry) is exactly what an aamix port would land on.
- **VAX as an anti-VOX source.** `MoxController::setAntiVoxSourceVax(true)` already exists but rejects with `qCWarning` (deferred since 3M-1b H.3). VAX path stays rejected. The Setup page source-selector remains effectively read-only on the VAX side until 3M-3c (per `phase3m-3a-iii-dexp-vox-design.md` §17, deep anti-VOX UI was already deferred there).
- **Multi-RX anti-VOX source matrix** (`SetAntiVOXSourceWhat` per-stream enable bits). Single source is hardcoded to RX1 for the duration of single-RX operation. The matrix surfaces with 3F.
- **Anti-VOX attack/decay or look-ahead** beyond `tau`. Thetis exposes only `tau` in `grpAntiVOX`; we match 1:1.

## 3. Source-of-Truth Mapping

All Thetis cites in this document use `[v2.10.3.13]` corresponding to upstream tag `v2.10.3.13` at commit `@501e3f51`. The tag is 7 commits behind current Thetis HEAD; none of those 7 commits touched DEXP, anti-VOX, or `cmaster.c` aamix wiring (verified by `git -C ../Thetis log v2.10.3.13..@501e3f51 -- '*dexp*' '*aamix*' '*antivox*' cmaster.cs setup.cs setup.Designer.cs` returning empty). Plan-author task: re-run the diff at the start of implementation, refresh stamps to `[@501e3f51]` if drift appears.

### 3.1 WDSP setters and entry points

| WDSP entry point | Thetis cite | Range / units | Already wrapped? |
|---|---|---|---|
| `SetAntiVOXRun` | `dexp.c:657` | `int` (0/1) | YES (3M-1b D.3) |
| `SetAntiVOXGain` | `dexp.c:688` | `double` linear (`Math.Pow(10, dB/20)`) | YES (3M-1b D.3) |
| `SetAntiVOXSize` | `dexp.c:666` | `int` complex samples | NO (this phase) |
| `SetAntiVOXRate` | `dexp.c:677` | `double` Hz | NO (this phase) |
| `SetAntiVOXDetectorTau` | `dexp.c:697` | `double` seconds (ms/1000) | NO (this phase) |
| `SendAntiVOXData` | `dexp.c:708` | `int id, int nsamples, double* data` (note `nsamples` ignored, buffer must be exactly `antivox_size` complex samples) | NO (this phase) |

### 3.2 Defaults from Thetis

| Quantity | Thetis cite | Value |
|---|---|---|
| `udAntiVoxTau.Minimum` | `setup.designer.cs:44672` | 1 ms |
| `udAntiVoxTau.Maximum` | `setup.designer.cs:44667` | 500 ms |
| `udAntiVoxTau.Increment` | `setup.designer.cs:44661` | 1 ms |
| `udAntiVoxTau.Value` (default) | `setup.designer.cs:44682` | **20 ms** |
| `udAntiVoxTau` tooltip | `setup.designer.cs:44681` | "Time-constant used in smoothing Anti-VOX data" |
| `udAntiVoxTau` label | `setup.designer.cs:44697` | "Tau (ms)" |
| `udAntiVoxTau_ValueChanged` wire conversion | `setup.cs:18995` | `cmaster.SetAntiVOXDetectorTau(0, value / 1000.0)` |
| anti-VOX gain default at `create_dexp` | `cmaster.c:156` | `0.01` linear (-40 dB) |
| anti-VOX tau default at `create_dexp` | `cmaster.c:157` | `0.01` seconds (10 ms) |
| `audio_outrate` (anti-VOX detector rate) | `cmsetup.c:73` | runtime-set by `cmaster.SetAudioRate` |
| `audio_outsize` (anti-VOX detector frame size) | `cmsetup.c:74` | `getbuffsize(audio_outrate)` |

The DEXP create-time constants `0.01 / 0.01` for gain and tau are immediately overridden by user-initiated `udAntiVoxGain_ValueChanged` and `udAntiVoxTau_ValueChanged` once the Setup page paints. The `udAntiVoxTau.Value=20` default in the Designer overrides the C-side `0.01` on every Thetis startup. **For NereusSDR the spinbox default is the source of truth**: we initialize `WdspEngine.cpp` create-time to 0.02 (matches the spinbox-applied value) so the radio behaves identically whether the Setup page has been opened or not.

### 3.3 ChannelMaster aamix routing (what we are NOT porting)

Thetis routes anti-VOX through an aamix instance per TX, created in `cmaster.c:159-175`. The aamix accepts one input per RX (`pcm->cmRCVR * pcm->cmSubRCVR` total inputs), each at its own `ch_outrate`. The aamix internally per-input resamples to `audio_outrate`, mixes per the `SetAntiVOXSourceWhat` enable bits, and emits `audio_outsize`-sample blocks at `audio_outrate` via the `SendAntiVOXData` function pointer (`cmaster.c:171`). DEXP is therefore created at `cmaster.c:154-155` with `antivox_size = audio_outsize` and `antivox_rate = audio_outrate`, NOT with TX `in_size`/`in_rate`.

Our single-RX, no-resample, single-source case lets us bypass aamix and pump directly. The cost of skipping aamix today is invisible to a single-RX user and to the rest of the system, provided we honor the aamix-aligned dimension contract: DEXP's anti-VOX detector is sized by the RX-side audio block (`outSize`), not the TX-side mic block (`inputBufferSize`). When 3F lands, the change is local: replace the direct `RxDspWorker::antiVoxSampleReady → TxWorkerThread::onAntiVoxSamplesReady` connection with an aamix instance fed by N `RxDspWorker` signals, with `aamix → SendAntiVOXData` callback restored to the Thetis pattern.

## 4. Architecture

### 4.1 Current state (the gap)

```
RX I/Q in
  │
  ▼
RxDspWorker::processIqBatch
  │   (chunks at WDSP in_size, processes via RxChannel::processIq)
  ▼
RxChannel::processIq → fexchange2 → outI/outQ at outSize
  │
  ▼
RxDspWorker interleaves L/R
  │
  ▼
AudioEngine::rxBlockReady ──► MasterMixer ──► speakers + VAX

                              ╳ (no fork to TX antivox)

TX path (separate):                              DEXP detector
mic ──► TxWorkerThread ──► TxChannel.processBlock ──► xdexp ──┤ antivox_data: ALL ZEROS
                                                              │ antivox_level: 0
                                                              │ asig = avsig - gain*0 = avsig
                                                              ▼ (anti-VOX has no effect)
                                                         VOX threshold
```

The anti-VOX gain slider drives `MoxController::setAntiVoxGain → antiVoxGainRequested → TxWorkerThread → TxChannel::setAntiVoxGain → SetAntiVOXGain` correctly. The DEXP block sees the correct gain. But because no caller ever invokes `SendAntiVOXData`, `antivox_data` stays at the zero-fill from `malloc0` and the smoothed `antivox_level` stays at zero. Anti-VOX is a no-op regardless of slider position.

### 4.2 Target state

```
RX I/Q in
  │
  ▼
RxDspWorker::processIqBatch
  │
  ▼
RxChannel::processIq → fexchange2 → outI/outQ at outSize
  │
  ▼
RxDspWorker interleaves L/R into m_interleavedOut
  │
  ├──► AudioEngine::rxBlockReady (existing)
  │
  └──► emit antiVoxSampleReady(sliceId=0, m_interleavedOut, outSize)  (NEW)
                  │
                  │ Qt::AutoConnection (queued cross-thread)
                  ▼
        TxWorkerThread::onAntiVoxSamplesReady (NEW)
                  │
                  │ if (m_antiVoxRun.load()) {
                  ▼
            TxChannel::sendAntiVoxData(interleaved, outSize)  (NEW)
                  │
                  │ float→double conversion into m_antiVoxScratch
                  ▼
            ::SendAntiVOXData(0, outSize, m_antiVoxScratch.data())
                  │
                  ▼
            DEXP antivox_data = freshly written
            DEXP antivox_new = 1
                  │
                  ▼
            xdexp computes antivox_level from new data
                  │
                  ▼
            asig = avsig - antivox_gain * antivox_level
                  │
                  ▼
            VOX threshold (now correctly compensated)
```

### 4.3 Setup-time DEXP dimension alignment

At present `WdspEngine::createTxChannel` calls `create_dexp` with `inputBufferSize` (TX in_size) and `inputSampleRate` (TX in_rate) for the anti-VOX detector dimensions ([src/core/WdspEngine.cpp:585-593](src/core/WdspEngine.cpp:585)). This is structurally wrong (the detector dimensions must follow the audio source, not the mic source) but works incidentally as long as both happen to be 1024 samples at 48 kHz.

The fix has two parts:

1. **Initial creation:** keep `create_dexp` as-is for now, since changing the create-time arguments requires plumbing RX-side dimensions into TX-channel construction and there is a chicken-and-egg with channel creation order. Acceptable because immediate post-create reconfiguration overrides the values.

2. **Post-create reconfiguration:** as soon as `RxDspWorker::setBufferSizes(inSize, outSize)` runs (called by `RadioModel` once the RX channel's post-decimation geometry is known), notify `TxWorkerThread` via a new `RadioModel`-mediated signal `antiVoxBlockGeometryChanged(outSize, outRate)`. `TxWorkerThread` calls `TxChannel::setAntiVoxSize(outSize)` and `TxChannel::setAntiVoxRate(outRate)`, which call `SetAntiVOXSize` and `SetAntiVOXRate` respectively. This re-allocates DEXP's internal `antivox_data` buffer at the right size for the chunks `RxDspWorker` actually emits.

3. **Runtime guarantee:** `TxChannel::sendAntiVoxData` asserts that the `nsamples` argument matches the most-recently-set `m_antiVoxSize`. If they ever drift (e.g. mid-session sample-rate change before a re-call to `setAntiVoxSize`), the wrapper logs `qCWarning(lcDsp)` and skips the WDSP call. No corrupted memcpy.

## 5. Component Changes

### 5.1 third_party/wdsp

No changes. `dexp.c` already exposes all six entry points NereusSDR consumes. Confirmed by inspection of [third_party/wdsp/src/dexp.c:657-714](third_party/wdsp/src/dexp.c:657).

### 5.2 src/core/wdsp_api.h

Add four declarations next to the existing `SetAntiVOXRun` / `SetAntiVOXGain` block at [src/core/wdsp_api.h:1029-1038](src/core/wdsp_api.h:1029):

```cpp
// From Thetis dexp.c:666 [v2.10.3.13]
void SetAntiVOXSize(int id, int size);
// From Thetis dexp.c:677 [v2.10.3.13]
void SetAntiVOXRate(int id, double rate);
// From Thetis dexp.c:697 [v2.10.3.13]
void SetAntiVOXDetectorTau(int id, double tau);
// From Thetis dexp.c:708 [v2.10.3.13]
void SendAntiVOXData(int id, int nsamples, double* data);
```

Update the file-level "Modification history (NereusSDR)" block to credit J.J. Boyd for the 3M-3a-iv additions.

### 5.3 src/core/TxChannel.{h,cpp}

Add four public methods next to the existing anti-VOX wrappers ([src/core/TxChannel.cpp:1674](src/core/TxChannel.cpp:1674) `setAntiVoxRun`):

```cpp
void setAntiVoxSize(int size);
void setAntiVoxRate(double rate);
void setAntiVoxDetectorTau(double seconds);
void sendAntiVoxData(const float* interleaved, int nsamples);
```

Add private state:
- `int m_antiVoxSize{0}` (last-applied size, used by sendAntiVoxData consistency check)
- `std::vector<double> m_antiVoxScratch` (resized on `setAntiVoxSize`, holds float→double conversion)

`sendAntiVoxData` body:
1. If `nsamples != m_antiVoxSize`, `qCWarning(lcDsp) << "anti-vox size mismatch:" << nsamples << "vs" << m_antiVoxSize`, return.
2. For each i in [0, 2*nsamples): `m_antiVoxScratch[i] = static_cast<double>(interleaved[i])`.
3. Call `::SendAntiVOXData(m_channelId, nsamples, m_antiVoxScratch.data())`.

The float→double conversion is unavoidable (RX audio is float, WDSP's antivox interface is double). Buffer is reused across calls to avoid per-call allocation. This runs on `TxWorkerThread`, NOT on the mic-input audio callback, so the allocation rule about audio-thread mutex-free safety is satisfied trivially.

### 5.4 src/core/WdspEngine.cpp

Two edits:

1. Line 593: change anti-VOX tau default from `0.01` to `0.02` to match Thetis `udAntiVoxTau.Value=20`. Add cite comment `// From Thetis setup.designer.cs:44682-44686 [v2.10.3.13] — udAntiVoxTau default 20 ms (= 0.02 s).`

2. No change to the create-time `antivox_size` and `antivox_rate` arguments at lines 590-591. They're correctly overridden post-create via `setAntiVoxSize` and `setAntiVoxRate` once RX geometry is known (see §4.3). The current values (`inputBufferSize`, `inputSampleRate`) are placeholder defaults, sufficient for the brief window between TX-channel creation and the first `setBufferSizes` call.

### 5.5 src/models/RxDspWorker.{h,cpp}

Add new signal in [src/models/RxDspWorker.h](src/models/RxDspWorker.h) under the existing `chunkDrained` and `batchProcessed` block:

```cpp
// Emitted per drained chunk after fexchange2 + post-NR processing, with
// the same interleaved L/R buffer that flows to AudioEngine::rxBlockReady.
// Consumed by TxWorkerThread::onAntiVoxSamplesReady to feed the WDSP
// DEXP anti-VOX detector. Signal-level fork keeps DSP thread free of
// any TX-side state.
//
// From Thetis ChannelMaster cmaster.c:159-175 [v2.10.3.13] — degenerate
// single-RX equivalent of pavoxmix → SendAntiVOXData.
void antiVoxSampleReady(int sliceId, const QVector<float>& interleaved, int sampleCount);
```

Emit immediately after `m_audioEngine->rxBlockReady(0, interleaved, outSize)` at [src/models/RxDspWorker.cpp:154](src/models/RxDspWorker.cpp:154). The buffer copy is unavoidable across the queued connection (Qt deep-copies `QVector` payload), but the worker's reusable `m_interleavedOut` is unchanged.

Performance note: the queued copy is `outSize * 2 * sizeof(float)` bytes per chunk. At 48 kHz with `outSize=1024`, that's 8 KiB per ~21 ms, well under the audio-block budget.

### 5.6 src/core/TxWorkerThread.{h,cpp}

Add new slot and atomic:

```cpp
// In TxWorkerThread.h, slots block:
public slots:
    void onAntiVoxSamplesReady(int sliceId, const QVector<float>& interleaved, int sampleCount);

// In TxWorkerThread.h, members block:
private:
    std::atomic<bool> m_antiVoxRun{false};
```

Wire `m_antiVoxRun` to follow the existing `setAntiVoxRun(bool)` queued setter (same place that calls `TxChannel::setAntiVoxRun`). `onAntiVoxSamplesReady` body:

```cpp
if (!m_antiVoxRun.load(std::memory_order_acquire)) return;
if (m_txChannel == nullptr) return;
if (sliceId != 0) return;  // single-RX gate; multi-RX mux is 3F
m_txChannel->sendAntiVoxData(interleaved.constData(), sampleCount);
```

The atomic gate avoids the cost of the float→double conversion when the user has anti-VOX disabled. It is acquired ahead of `m_txChannel` access to give a happens-before with `setAntiVoxRun(true)` (release on store) so a stale `m_txChannel` read is impossible.

### 5.7 src/core/MoxController.{h,cpp}

Add slot and signal mirroring the existing anti-VOX gain pattern:

```cpp
// In MoxController.h, slots block:
public slots:
    void setAntiVoxTau(int ms);

// In MoxController.h, signals block:
signals:
    void antiVoxDetectorTauRequested(double seconds);
```

Body of `setAntiVoxTau`:

```cpp
const double seconds = static_cast<double>(ms) / 1000.0;
if (!std::isnan(m_lastAntiVoxTauEmitted) && seconds == m_lastAntiVoxTauEmitted) return;
m_lastAntiVoxTauEmitted = seconds;
emit antiVoxDetectorTauRequested(seconds);
```

State:
- `int m_antiVoxTauMs{20}` (mirrors `TransmitModel::antiVoxTauMs()`)
- `double m_lastAntiVoxTauEmitted{std::numeric_limits<double>::quiet_NaN()}` (NaN sentinel forces first-call emit, matches `m_lastAntiVoxGainEmitted` pattern at [src/core/MoxController.h:954](src/core/MoxController.h:954))

The cite comment cluster at the top of MoxController.h gets a new entry: `// From Thetis setup.cs:18992-18996 [v2.10.3.13] — udAntiVoxTau_ValueChanged: cmaster.SetAntiVOXDetectorTau(0, value / 1000.0).`

### 5.8 src/models/TransmitModel.{h,cpp}

Add property `antiVoxTauMs`:
- Q_PROPERTY int with `READ antiVoxTauMs WRITE setAntiVoxTauMs NOTIFY antiVoxTauMsChanged`
- Default 20.
- Range clamped 1..500 in setter (matches Thetis `udAntiVoxTau.Minimum`/`Maximum`).
- AppSettings key `AntiVoxTauMs`, loaded by `loadFromSettings`, saved by `saveToSettings` alongside the existing `AntiVoxGainDb` and `AntiVoxSourceVax` keys.

`antiVoxTauMsChanged(int)` signal connects to `MoxController::setAntiVoxTau(int)` in `RadioModel.cpp` next to the existing `antiVoxGainDbChanged → setAntiVoxGain` wire.

### 5.9 src/core/AppSettings

No code change beyond `TransmitModel`'s use of the new key. AppSettings is already the persistence layer; adding `AntiVoxTauMs` as an int (string `"20"` default per the AppSettings boolean-as-string convention) is purely an addition.

### 5.10 src/gui/setup/DexpVoxSetupPage.{h,cpp}

The page already exists (added in 3M-3a-iii). Add one new control to the existing `grpAntiVOX` group:

- New member `QSpinBox* m_antiVoxTauSpin{nullptr}`.
- Layout row below the existing anti-VOX gain spinbox: label "Tau (ms)" + spinbox.
- Spinbox: `setRange(1, 500)`, `setSingleStep(1)`, `setValue(transmitModel->antiVoxTauMs())`.
- Tooltip: `setToolTip(tr("Time-constant used in smoothing Anti-VOX data"))`.
  - Cite comment in source above the call: `// Tooltip from Thetis setup.designer.cs:44681 [v2.10.3.13].`
- Wire `valueChanged(int) → TransmitModel::setAntiVoxTauMs(int)` and the reverse update via `connect(transmitModel, &TransmitModel::antiVoxTauMsChanged, m_antiVoxTauSpin, &QSpinBox::setValue)` with `QSignalBlocker` guard for the round-trip.

The new control sits in the same `grpAntiVOX` group as the gain spinbox to match the Thetis `grpAntiVOX` group composition ([v2.10.3.13] `setup.designer.cs:44634-44635`).

### 5.11 src/models/RadioModel.cpp

Three new signal wires, alongside the existing anti-VOX gain wiring:

1. `RxDspWorker::antiVoxSampleReady → TxWorkerThread::onAntiVoxSamplesReady` (queued auto-connection).
2. `TransmitModel::antiVoxTauMsChanged → MoxController::setAntiVoxTau` (direct, both on main thread).
3. `MoxController::antiVoxDetectorTauRequested → TxWorkerThread::setAntiVoxDetectorTau` (queued auto-connection).

A fourth wire handles the dimension-alignment infrastructure from §4.3:
4. `RxDspWorker::bufferSizesChanged(int outSize, double outRate) → TxWorkerThread::setAntiVoxBlockGeometry(int, double)` (new signal on RxDspWorker, fired by `setBufferSizes`; new slot on TxWorkerThread that calls `TxChannel::setAntiVoxSize` + `TxChannel::setAntiVoxRate`). Queued.

## 6. Sample-Rate / Buffer-Size Alignment

The aamix-skipping shortcut depends on RX-side and DEXP-side dimensions matching. Three invariants we maintain:

1. **antivox_size matches RxDspWorker outSize.** Enforced by `setAntiVoxSize(outSize)` after `setBufferSizes`. DEXP re-allocates its `antivox_data` buffer to the new size.
2. **antivox_rate matches RxDspWorker output sample rate.** Enforced by `setAntiVoxRate(outRate)`. The smoothing constant `antivox_mult = exp(-1 / (antivox_rate * antivox_tau))` is recomputed inside `SetAntiVOXRate`, so the time-constant remains correct in absolute seconds.
3. **TxChannel::sendAntiVoxData buffer length matches m_antiVoxSize.** Enforced by the runtime check in §5.3. If they ever drift (e.g. RX rate change in flight), the wrapper logs and skips rather than corrupting memory.

Connection-ordering implementation task (carries into the plan, not a design unknown): the `RxDspWorker::bufferSizesChanged → TxWorkerThread::setAntiVoxBlockGeometry` wire must be in place before the first `RxDspWorker::setBufferSizes` call from `RadioModel.cpp`, otherwise the first emission is dropped and DEXP runs at the placeholder dimensions until the next geometry change. Plan-author trace the existing `setBufferSizes` callsites and place the new connection ahead of them; if the ordering is awkward, the alternative is a stored-state fallback (TxWorkerThread caches the most recent geometry on receive and applies it on TxChannel ready).

## 7. Threading and Synchronization

- **DSP thread (`RxDspWorker`):** emits `antiVoxSampleReady`. No new state, no new locks, no new allocations beyond the existing per-chunk `QVector` lifecycle.
- **TX worker thread (`TxWorkerThread`):** receives `onAntiVoxSamplesReady` via queued connection. Reads `m_antiVoxRun` atomic. On enable, calls `TxChannel::sendAntiVoxData`. Owns the float→double scratch buffer.
- **Main thread (`MoxController`, `TransmitModel`, UI):** sets the gain/tau/run values. All cross-thread transmissions are queued signals as elsewhere in 3M-3a-iii.
- **No new locks. No mutex on the audio path.** The atomic gate replaces what would otherwise be a `bool m_antiVoxRun` plus a mutex; CLAUDE.md's "never hold a mutex in the audio callback" rule is preserved.

The `m_antiVoxScratch` buffer is owned exclusively by `TxWorkerThread`. It is resized on `setAntiVoxSize` (which runs on TX worker thread because §5.6's slot dispatches via `TxChannel`). It is read and written on the same thread inside `sendAntiVoxData`. No sharing.

## 8. Settings Persistence

| Key | Type | Default | Range | Read | Write |
|---|---|---|---|---|---|
| `AntiVoxTauMs` | int | 20 | 1..500 | `TransmitModel::loadFromSettings` | `TransmitModel::saveToSettings` |

The existing keys `AntiVoxGainDb` and `AntiVoxSourceVax` are unchanged. Default value 20 is sourced from Thetis `udAntiVoxTau.Value` ([v2.10.3.13] `setup.designer.cs:44682`).

`MicProfileManager` keyset additions: none. `tau` is a per-radio detector parameter, not a per-mic-profile parameter (Thetis stores it in the global Setup database, not per-`txProfile`). To match, leave `MicProfileManager` untouched. If a future user requests per-profile `tau`, that's a separate scope.

## 9. UI

### 9.1 Setup → Transmit → DEXP/VOX page

Single new control inside the existing `grpAntiVOX` group:

```
┌─ grpAntiVOX ─────────────────────────────────────────┐
│  [×] Enable Anti-VOX                  (existing)     │
│  Source: ( ) RX  ( ) VAX              (existing)     │
│  Gain (dB):  [   0]                   (existing)     │
│  Tau (ms):   [  20]                   (NEW)          │
└──────────────────────────────────────────────────────┘
```

Tooltip on the spinbox: "Time-constant used in smoothing Anti-VOX data" (verbatim Thetis).

No applet-side surface. Anti-VOX `tau` is a per-radio tuning constant in Thetis, not a per-mode quick control, and the Thetis UI does not expose it on the main console either.

### 9.2 No changes to TxApplet, PhoneCwApplet, or RxApplet

The 3M-3a-iii VOX/DEXP rows on PhoneCwApplet and the VOX row on TxApplet stay as-is. Anti-VOX is opaque to the applet layer.

## 10. Tests

| Test executable | New cases | Purpose |
|---|---|---|
| `tst_tx_channel_anti_vox` (new) | `setAntiVoxSize_callsWdsp`, `setAntiVoxRate_callsWdsp`, `setAntiVoxDetectorTau_callsWdsp`, `sendAntiVoxData_pumpsWdsp_whenSizeMatches`, `sendAntiVoxData_skipsWdsp_whenSizeMismatches`, `sendAntiVoxData_convertsFloatToDouble` | Direct TxChannel WDSP wrapper coverage (gated on `HAVE_WDSP`; uses existing fake-WDSP harness pattern) |
| `tst_mox_controller_anti_vox` (extend) | `antiVoxTau_defaultEmitsTwentyMs`, `antiVoxTau_idempotent`, `antiVoxTau_msToSeconds`, `antiVoxTau_nanSentinelFirstCall`, `antiVoxTau_boundary_1ms`, `antiVoxTau_boundary_500ms` | Mirrors the existing `antiVoxGain_*` test block at [tests/tst_mox_controller_anti_vox.cpp:138](tests/tst_mox_controller_anti_vox.cpp:138) |
| `tst_rx_dsp_worker_anti_vox` (new) | `antiVoxSampleReady_firesPerChunk`, `antiVoxSampleReady_payloadMatchesRxBlockReady`, `antiVoxSampleReady_sliceIdIsZero` | Verifies the signal-level fork without requiring a TX channel |
| `tst_dexp_vox_setup_page` (extend) | `tauSpinbox_defaultIs20`, `tauSpinbox_writesTransmitModel`, `tauSpinbox_readsTransmitModel`, `tauSpinbox_persistsViaAppSettings` | Round-trips the new spinbox through TransmitModel and AppSettings |
| `tst_transmit_model` (extend) | `antiVoxTauMs_defaultIs20`, `antiVoxTauMs_clampsToRange`, `antiVoxTauMs_persistsAcrossLoad` | Property-level coverage |

All tests live in `tests/`, added to `tests/CMakeLists.txt`, run under `ctest`.

## 11. Manual Bench Verification

A new verification matrix at `docs/architecture/phase3m-3a-iv-verification/README.md`. Required runs:

1. **ANAN-G2, single-RX, SSB.** RX audio: tune to a CW spotting beacon or a known SSB signal at S9+5 in the speaker. Mic muted. VOX engaged with threshold above mic noise floor but below RX speaker level. Expected: with anti-VOX disabled (gain=0 dB or run=0), VOX false-trips on RX audio. With anti-VOX gain=-20 dB and tau=20 ms, false-trips stop. With gain=-40 dB, suppression is stronger.
2. **HL2 (mi0bot), single-RX, SSB.** Same test on Hermes Lite 2 hardware. Confirms HL2's audio path delivers RX speaker audio at the same dimensions the dimension-alignment plumbing assumes.
3. **Tau range bench.** With a fixed RX speaker level and fixed anti-VOX gain, sweep `tau` from 1 ms to 500 ms in five steps. Confirm the smoothing time-constant audibly affects how quickly anti-VOX engages and disengages with bursty RX audio (e.g. SSB voice peaks).
4. **Restart persistence.** Set anti-VOX gain=-15 dB and tau=80 ms, close the radio, reopen. Confirm both values persist. Confirm the operator's anti-VOX behavior is identical pre- and post-restart.
5. **Mid-session sample-rate change** (if HW supports). Confirm `setAntiVoxSize`/`setAntiVoxRate` rewires DEXP to the new geometry, with no `lcDsp` warnings or audible glitch.

Verification is run by the maintainer (J.J. Boyd, KG4VCF) on both ANAN-G2 and HL2 before release. Results captured in the matrix file as a 5-row table per radio.

## 12. Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| RX outSize and TX-side DEXP `antivox_size` drift mid-session | Low | Runtime size-mismatch check in `TxChannel::sendAntiVoxData` logs and skips, never corrupts |
| Float→double conversion adds detectable latency | Very low | 2 KiB conversion per ~21 ms is well under any audio budget |
| Queued signal adds a frame of detector lag | Very low | tau=20 ms smoothing absorbs single-frame jitter; bench sweep in §11.3 confirms |
| `m_antiVoxRun` atomic acquire ordering wrong, racing `m_txChannel` setter | Low | acquire-load ahead of pointer deref; release-store in the setter; matches existing `m_antiVoxGain` pattern |
| Aamix-skipping invalidates Thetis bug-for-bug parity | Low | aamix-equivalent is a direct degenerate (single-input passthrough); no detector behavior changes; future aamix port restores parity |
| Setup page Tau spinbox round-trip echoes infinitely | Very low | `QSignalBlocker` guard pattern as in existing `udAntiVoxGain` wiring |

## 13. Future Work

When 3F multi-pan ships, port `aamix.c/h` from `Project Files/Source/ChannelMaster/aamix.c` ([v2.10.3.13]) into `third_party/wdsp/`. aamix is GPLv2-or-later, attribution Warren Pratt NR0V. Per-port checklist and PROVENANCE row entry per `docs/attribution/HOW-TO-PORT.md`. Replace the direct `RxDspWorker → TxWorkerThread` connection with a `create_aamix(0, txid=0, audio_outsize, audio_outsize, N_RX, ...)` instance, point its callback at `TxChannel::sendAntiVoxData` (or directly at `::SendAntiVOXData` if the float→double conversion can be lifted into aamix's input handler), and restore Thetis `SetAntiVOXSourceWhat`/`SetAntiVOXSourceStates` semantics for the multi-RX enable mux. The dimension-alignment infrastructure from this phase carries over unchanged.

Pre-emphasis (`emph.c` family, deferred to 3M-3b) is unrelated and untouched.

VAX as an anti-VOX source (still rejected with `qCWarning` in `MoxController::setAntiVoxSourceVax(true)`) is unblocked once VAX→TX routing lands in 3M-3c. At that point the rejection is removed and the source-toggle radio in `DexpVoxSetupPage` becomes meaningful.

## 14. Out of Scope

- Per-mode anti-VOX behavior. Thetis applies anti-VOX uniformly across modes; we match.
- Anti-VOX during PureSignal calibration. PureSignal is 3M-4; the interaction is a 3M-4 concern.
- Anti-VOX peak meter visualization on the applet. Thetis does not expose one; we don't either.
- Per-band anti-VOX gain. Thetis stores `tau` and gain globally per radio, not per-band; we match.

## 15. Open Questions

None. Brainstorm 2026-05-07 closed all design questions:
- Pump architecture: direct `RxDspWorker → TxWorkerThread` queued (rejected option 2 AudioEngine route, rejected option 3 direct pointer).
- aamix port: deferred (rejected porting today; recommended degenerate single-RX equivalent with future-aamix-compatible plumbing).
- Setter target: option B (Run/Gain already wired; add Size/Rate/DetectorTau/SendData).

## 16. Rollback

Single revert of the implementation commit chain backs out:
- The four new WDSP wrappers in TxChannel and `wdsp_api.h` (declarations remain unused, harmless).
- The new signal/slot wires in `RadioModel.cpp` (drop, no further effect).
- The new `TransmitModel::antiVoxTauMs` property and AppSettings key (orphaned key in any user's settings file is harmless; reads as default if re-introduced).
- The new spinbox on `DexpVoxSetupPage`.
- The constant fix in `WdspEngine.cpp:593` (0.02 → 0.01 reverts to current behavior, just less Thetis-faithful).

Post-revert state matches v0.3.2 exactly.

## 17. Implementation Sequence

Two GPG-signed commits planned:

**Commit 1: Plumbing + tests (no UI).**
- `wdsp_api.h` declarations.
- `TxChannel` four new methods + `m_antiVoxScratch` + size-mismatch check.
- `WdspEngine.cpp` constant fix (0.01 → 0.02).
- `RxDspWorker` `antiVoxSampleReady` and `bufferSizesChanged` signals.
- `TxWorkerThread` slots + `m_antiVoxRun` atomic.
- `MoxController` `setAntiVoxTau` slot + `antiVoxDetectorTauRequested` signal.
- `TransmitModel` `antiVoxTauMs` property + AppSettings key.
- `RadioModel` four new signal wires.
- `tst_tx_channel_anti_vox` (new), `tst_mox_controller_anti_vox` (extend), `tst_rx_dsp_worker_anti_vox` (new), `tst_transmit_model` (extend).
- All tests green under `ctest`.

**Commit 2: UI + bench-verification matrix + doc-refresh.**
- `DexpVoxSetupPage` new spinbox + wiring.
- `tst_dexp_vox_setup_page` extend.
- `docs/architecture/phase3m-3a-iv-verification/README.md` (new bench matrix).
- CHANGELOG + CLAUDE.md current-phase paragraph.
- Spec status updated to "Implemented".

PR is opened after Commit 2. Bench verification on ANAN-G2 + HL2 by maintainer is required for merge. CHANGELOG line records v0.3.3 (or whatever the next patch tag is) with "Anti-VOX cancellation feed wired (closes 3M-3a-iii gap)".

## 18. Architectural divergence from Thetis: anti-VOX source selector

After bench verification of the cancellation feed (sections 1-17 above) the
plan added a third implementation pass to clean up an architectural mismatch
that the original design carried over from Thetis without questioning.

### What Thetis does

Thetis exposes `chkAntiVoxSource` ("Use VAC Audio") at
`setup.designer.cs:44646-44657 [v2.10.3.13]`. The handler at
`setup.cs:18998-19002` flips the `AntiVOXSourceVAC` property in `audio.cs:446-454`,
which `cmaster.CMSetAntiVoxSourceWhat` (`cmaster.cs:912-943`) routes per WDSP
channel:

- `useVAC == false`: anti-VOX feed is the audio going to hardware (RX1, RX1S, RX2).
- `useVAC == true`: anti-VOX feed is the VAC TX-input bus.

Thetis lets the user choose which one is active because Thetis VAC is not just
a digital-mode bus; in some Thetis routings it can carry locally-monitored
audio that could feed back through the local mic during digital ops, and the
user wants to be able to subtract that as well.

### Why it does not map to NereusSDR

NereusSDR's audio architecture diverged from Thetis on this point. We have two
distinct speaker paths and one digital-mode bus:

- **Audio output device (PC speakers, the IAudioBus speakers path).** Wired
  today. This is the only path that can produce a mic-feedback loop.
- **Radio-attached speaker output.** Future, not wired.
- **VAX (digital-mode app bus).** Output-only to apps. No mic-feedback path:
  apps consuming VAX never loop back into the local mic. Documented in
  `feedback_vax_not_vac_port.md` ("VAX is not a VAC port") — VAX diverged
  intentionally from Thetis's VAC during NereusSDR design.

Anti-VOX exists to prevent **speaker bleed → local mic → false VOX trigger**.
The cancellation reference must be the audio about to be played through
speakers. There is no user choice to expose:

- VAX is never a valid anti-VOX source (no mic-feedback path).
- Audio output device is always the right source.

Exposing the Thetis chkAntiVoxSource toggle would be misleading — it would
look like a meaningful choice but flipping to "VAX" would degrade anti-VOX
without any compensating benefit.

### What was removed (Option A refactor)

Single GPG-signed commit in 3M-3a-iv (post-bench), removing the entire
source-selector plumbing:

- `TransmitModel::antiVoxSourceVax` Q_PROPERTY + getter + setter + signal +
  member + `AntiVox_Source_VAX` persistence read/write. Existing user
  settings carrying `AntiVox_Source_VAX` will see the key as an orphan in
  AppSettings (harmless; ignored on load — no migration logic needed).
- `MoxController::setAntiVoxSourceVax(bool)` slot + `antiVoxSourceWhatRequested`
  signal + `m_antiVoxSourceVax` / `m_antiVoxSourceVaxInitialized` state.
- `RadioModel.cpp` connect lambdas for the source chain (the `antiVoxSourceVaxChanged
  → setAntiVoxSourceVax` wire and the no-op `antiVoxSourceWhatRequested`
  lambda preserved for 3F multi-pan).
- `DexpVoxPage::m_chkAntiVoxSource` checkbox + bidirectional binding.
- `MicProfileManager` bundle key + factory-profile defaults + load/save.
- All `antiVoxSource*` test cases (3 from `tst_transmit_model_anti_vox`,
  5 from `tst_mox_controller_anti_vox`, 2 from `tst_dexp_vox_setup_page`,
  2 persistence tests, 2 from `tst_mic_profile_manager`).

### What replaced the checkbox

A static `QLabel` info row at the same Y-position (between the Enable
checkbox and the Gain spinbox) reading **"Source: Audio Output Device(s)"**.
The label's tooltip explains the architectural divergence verbatim (see
`TransmitSetupPages.cpp` — the tooltip cites Thetis `setup.designer.cs:44646-44657
[v2.10.3.13]` and explains why VAX is intentionally not subject to anti-VOX
treatment).

### Future tap-point signpost

The anti-VOX cancellation reference is forked from `RxDspWorker`'s demod
output **before** it reaches `AudioEngine`. Today's single-output PC speaker
path satisfies the assumption that "the demod block IS what plays through
speakers." When radio-speaker output with output divergence lands (per-bus
EQ, gain, mute beyond master, or independent processing for the radio's own
speaker), the anti-VOX tap MUST move from `RxDspWorker` to `AudioEngine`'s
post-mixer summing point so the cancellation reference matches the audio
actually leaving the speakers.

This is a tap-point relocation only; the WDSP DEXP block and
`TxChannel::sendAntiVoxData` wrapper stay unchanged. Signposted with verbatim
comment blocks in `RxDspWorker.cpp` (at the `antiVoxSampleReady` emit) and
`AudioEngine.h` (at the IAudioBus / MasterMixer description) so a future
maintainer can find both ends of the move with one grep.
