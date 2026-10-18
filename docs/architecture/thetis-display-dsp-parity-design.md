# Thetis Display + DSP Parity Audit — Design

**Status:** Approved (brainstorm 2026-05-01, KG4VCF)
**Target version:** v0.3.0
**Thetis upstream pinned at:** `v2.10.3.13-7-g501e3f51`
**Branch:** TBD (single PR, ~30 GPG-signed commits)
**Estimated calendar:** ~3 weeks

---

## Goal

Achieve Thetis v2.10.3.13 parity for `Display → RX1`, `Display → General`, and
`DSP → Options` Setup pages, revisit several handwave implementations from
3G-8, build the WDSP channel teardown/rebuild infrastructure required for full
live-apply behavior, and add one NereusSDR-original enhancement (per-band
noise-floor priming).

This is a **port**, not a reimagination. Every behavior comes from Thetis
source; every divergence is named explicitly with a comment marker. UI uses
NereusSDR's existing patterns (Setup tree, ColorSwatchButton, container per-pan
overrides).

---

## Scope at a glance

- **Approved depth:** "no handwave" floor — every parity item gets a real
  algorithmic / rendering port, not just a stored AppSettings key.
- **Single PR, ~30 commits**, sequence within: RX1 → Display-General →
  DSP-Options. Channel-rebuild infrastructure is the precondition for the
  whole work and lands first.
- **Live-apply scope:** "Standard" — DSP-Options + sample-rate + active-RX-count
  all live-apply via the new infrastructure. DDC-reassignment / Diversity /
  MultiRX2 surface deferred to Phase 3F.
- **Pan-agnostic naming throughout.** No `RX1` in new control names or
  AppSettings keys. Per-pan overrides via the existing Container Settings
  dialog (3G-6 pattern).
- **Settings migration:** the few keys whose semantics change reset to
  Thetis defaults on first launch of v0.3.0; one CHANGELOG line documents it.

---

## Section 0 — Sequencing summary

1. **Cluster 1 — Channel-rebuild infrastructure** (Section 5) — first
2. **Cluster 2 — RX1 audit** (Section 2) — biggest, depends on Cluster 1
3. **Cluster 3 — Display-General fold** (Section 3) — independent
4. **Cluster 4 — DSP-Options** (Section 4) — depends on Cluster 1
5. **Cluster 5 — Migration + verification matrix + CHANGELOG** (Sections 7+8)
6. **Cluster 6 — Polish** (review-driven)

---

## Section 1 — Architecture & Setup IA changes

### 1A — Setup tree changes

**Display category (current 3 pages → after 5 pages):**

| Page | Status | Notes |
|---|---|---|
| `Display → Spectrum Defaults` | Existing — extend | +overlay group (MHz on cursor, NF, bin width, peak value, normalize, GetMonitorHz, decimation wire-up); +cross-link buttons; +"moved to" hint lines |
| `Display → Spectrum Peaks` | **New** | Active Peak Hold (5 ctrls) + Peak Blobs (7 ctrls) + 2 ColorSwatchButtons |
| `Display → Waterfall Defaults` | Existing — extend | +NF-AGC group, +Stop-on-TX, +Calculated-Delay readout, +Copy button, –W5 reverse scroll |
| `Display → Grid & Scales` | Existing — extend | +NF-aware grid group (3 ctrls), +Copy button, +per-band NF priming logic |
| `Display → Multimeter` | **New** | 8 multimeter ctrls + S/dBm/µV unit combo + Signal History (2) + cross-link buttons |

**DSP category:**

| Page | Status | Notes |
|---|---|---|
| Existing DSP pages | Untouched | Out of scope |
| `DSP → Options` | **New** | 18 controls (4 buffer + 4 filter-size + 7 filter-type + 2 cache + 1 hi-res) + 1 readout + 3 warning icons |

**Single-control additions to existing pages:**

| Target page | Control | Note |
|---|---|---|
| `Hardware → RadioInfoTab` | `chkANAN8000DLEDisplayVoltsAmps` | Cap-gated on `caps.is8000DLE` |
| `Appearance → MeterStylesPage` | `chkSmallModeFilteronVFOs` (in new "VFO Flag" group box) | Closest semantic fit |
| `Diagnostics → Logging` (rename to "Logging & Performance") | Spec Warning LEDs + PurgeBuffers (in new "Performance" group box) | 3 controls; new group box not new page |
| `General → Options` | `udDisplayCPUMeter` (CPU meter rate) | Feeds chrome titlebar widget |

### 1B — Pan-agnostic naming convention

All new control names and AppSettings keys drop `RX1`. Settings on these pages
are **panadapter-agnostic global defaults**. Per-panadapter overrides go
through the existing Container Settings dialog (3G-6 pattern).

**Source-cite footer template** for ports that drop Thetis's RX1 scope:

```cpp
// From Thetis setup.cs:NNNN [v2.10.3.13]
// — RX1 scope dropped; NereusSDR applies as global panadapter default
//   with per-pan override via ContainerSettings dialog (3G-6 pattern).
```

**NereusSDR-original marker** for non-Thetis additions:

```cpp
// NereusSDR-original — no Thetis equivalent.
```

User-visible strings (tooltips, status bar, dialog labels, button captions)
stay plain English with **no Thetis source cites** per
`feedback_no_cites_in_user_strings`.

### 1C — AppSettings keys

**Naming convention** (matches existing NereusSDR pattern):

- Global keys: flat PascalCase (e.g. `DisplayActivePeakHoldEnabled`)
- Per-band keys: `<KeyName>_<bandKeyName>` underscore-separated (matching
  3G-8's `gridMaxKey()` / `gridMinKey()` convention via `bandKeyName(Band)`
  helper from `Band.cpp:146`)

**New keys (PascalCase global):**

```
// Spectrum (Section 2)
DisplaySpectrumDetector              "Peak" | "RMS" | "Sample" | "Average"
DisplaySpectrumAveraging             "None" | "Recursive" | "TimeWindow" | "LogRecursive" | "LogMp"
DisplayWaterfallDetector             same enum
DisplayWaterfallAveraging            same enum
DisplayShowMHzOnCursor
DisplayShowBinWidth
DisplayShowNoiseFloor
DisplayShowNoiseFloorPosition        TopLeft | TopRight | BottomLeft | BottomRight
DisplayDispNormalize
DisplayShowPeakValueOverlay
DisplayPeakValuePosition             TopLeft | TopRight | BottomLeft | BottomRight
DisplayPeakTextDelayMs               default 500 (Thetis default)
DisplayPeakValueColor                hex with alpha; default DodgerBlue (Thetis default)

// Spectrum Peaks (Section 2C)
DisplayActivePeakHoldEnabled
DisplayActivePeakHoldDurationMs
DisplayActivePeakHoldDropDbPerSec
DisplayActivePeakHoldFill
DisplayActivePeakHoldOnTx
DisplayPeakBlobsEnabled
DisplayPeakBlobsCount
DisplayPeakBlobsInsideFilterOnly
DisplayPeakBlobsHoldEnabled
DisplayPeakBlobsHoldMs
DisplayPeakBlobsHoldDrop
DisplayPeakBlobsFallDbPerSec
DisplayPeakBlobColor                 hex with alpha; default OrangeRed (Thetis)
DisplayPeakBlobTextColor             hex with alpha; default Chartreuse (Thetis)

// Grid & Scales (Section 2E)
DisplayAdjustGridMinToNoiseFloor
DisplayNFOffsetGridFollow            dB; range -60..+60
DisplayMaintainNFAdjustDelta

// Waterfall (Section 2D)
DisplayWaterfallNFAGCEnabled
DisplayWaterfallAGCOffsetDb
DisplayWaterfallStopOnTx
// W8/W9 retained, no rename

// Multimeter (Section 3A)
MultimeterDelayMs
MultimeterPeakHoldMs
MultimeterTextHoldMs
MultimeterAverageWindow
MultimeterDigitalDelayMs
MultimeterShowDecimal
MultimeterUnitMode                   "S" | "dBm" | "uV"
MultimeterSignalHistoryEnabled
MultimeterSignalHistoryDurationMs

// DSP-Options (Section 4)
DspOptionsBufferSizePhone
DspOptionsBufferSizeCw
DspOptionsBufferSizeDig
DspOptionsBufferSizeFm
DspOptionsFilterSizePhone
DspOptionsFilterSizeCw
DspOptionsFilterSizeDig
DspOptionsFilterSizeFm
DspOptionsFilterTypePhoneRx
DspOptionsFilterTypePhoneTx
DspOptionsFilterTypeCwRx
DspOptionsFilterTypeDigRx
DspOptionsFilterTypeDigTx
DspOptionsFilterTypeFmRx
DspOptionsFilterTypeFmTx
DspOptionsCacheImpulse
DspOptionsCacheImpulseSaveRestore
DspOptionsHighResFilterCharacteristics

// Section 3 single-page additions
HardwareAnan8000DleShowVoltsAmps
AppearanceSmallModeFilterOnVfos
DiagnosticsSpecWarningLedRenderDelay
DiagnosticsSpecWarningLedGetPixels
DiagnosticsPurgeBuffers
GeneralCpuMeterUpdateRateHz

// Section 6 NereusSDR-original
DisplayBandNFEstimate_<bandKeyName>  // 14 keys, one per band

// Section 7 migration plumbing
SettingsSchemaVersion                int (currently 2; this PR bumps to 3)
```

**Retired keys (deleted by migration):**

```
DisplayAverageMode                   // split into Detector + Averaging
DisplayPeakHold                      // promoted to ActivePeakHold...
DisplayPeakHoldDelayMs               // promoted to ActivePeakHoldDurationMs
DisplayReverseWaterfallScroll        // dropped (W5 removal)
```

### 1D — Cross-link buttons

Minimal set: 4 buttons + 2 hint text lines. Goal: help Thetis-legacy users
discover where Display→General contents went.

| Page | Element | Target / behavior |
|---|---|---|
| `Display → Spectrum Defaults` | `[Configure peaks →]` button | Jumps to `Display → Spectrum Peaks` |
| `Display → Spectrum Defaults` | `[Configure multimeter →]` button | Jumps to `Display → Multimeter` |
| `Display → Spectrum Peaks` | `[← Spectrum defaults]` button | Back link |
| `Display → Multimeter` | `[← Spectrum defaults]` button | Back link |
| `Display → Spectrum Defaults` | small italic note | "ANAN-8000DLE volts/amps moved to Hardware → ANAN-8000DLE." |
| `Display → Spectrum Defaults` | small italic note | "Small filter on VFOs moved to Appearance → VFO Flag." |

Implementation: each button emits a signal connected to `SetupDialog::setCurrentPage(...)`.
No new infrastructure beyond what already exists.

### 1E — Source-cite versioning policy

Every new line of ported code gets:

```cpp
// From Thetis <file>.cs:<line> [v2.10.3.13]
```

Thetis version stamp `v2.10.3.13` captured at session start
(`git -C ../Thetis describe --tags` returned `v2.10.3.13-7-g501e3f51`). Re-sync
of upstream during this branch's lifetime triggers re-stamp of affected cites.

Verbatim inline-tag preservation per `feedback_inline_cite_versioning` and
CLAUDE.md ship-blocking rule. License headers preserved per
`docs/attribution/HOW-TO-PORT.md`. PROVENANCE row added in
`docs/attribution/THETIS-PROVENANCE.md` for every new ported file.

---

## Section 2 — RX1 audit (Work Area #1)

Six sub-areas, ~28 net new controls.

### 2A — Detector & Averaging split (handwave fix)

**Today:** single `DisplayAverageMode` combo conflates two distinct concepts.

**Thetis source:** four separate combos:

- `comboDispPanDetector` / `comboDispWFDetector` — Peak / RMS / Sample / Average
- `comboDispPanAveraging` / `comboDispWFAveraging` — None / Recursive /
  Time-Window / Log-Recursive / Log-Mp

Thetis drives WDSP via the channel-master layer; verify exact signatures via
`dsp.cs` P/Invoke declarations in Thetis before commit (do not guess).

**Work:**

- Split `SpectrumWidget::setAverageMode` into four setters
  (`setSpectrumDetector`, `setSpectrumAveraging`, `setWaterfallDetector`,
  `setWaterfallAveraging`)
- Wire each setter to corresponding WDSP API call (verify signatures)
- Add 2 combos to Spectrum Defaults page; 2 to Waterfall Defaults page
  (matches Thetis layout: detector + averaging co-located on each tab)
- AppSettings: retire `DisplayAverageMode`; add 4 new keys per Section 1C
- Tests: `test_detector_modes`, `test_averaging_modes`

### 2B — Spectrum Defaults page extensions

| Control | Type | Behavior |
|---|---|---|
| `ShowMHzOnCursor` | checkbox | Toggle MHz format on cursor freq readout |
| `ShowBinWidth` + readout label | checkbox + label | Live `sampleRate / fftSize` display |
| `ShowNoiseFloor` + position combo | checkbox + combo | Render NF estimator value as text overlay; position selectable |
| `DispNormalize` | checkbox | Normalize trace to peak; algorithm ported from Thetis `Display.cs` (exact line during impl) |
| `ShowPeakValueOverlay` + position + delay | checkbox + combo + spinbox | Corner peak-text readout: scan visible bins, display strongest signal as text; refresh per `PeakTextDelayMs` (default 500, matches Thetis `console.cs:20073`) |
| `GetMonitorHz` button | button | Qt platform query → snap FPS slider to nearest valid refresh rate |
| `Decimation` (S16) wired | spinbox | Plumb scaffolded control to `FFTEngine::setDecimation()` |
| Cross-link buttons (×2) | button | Section 1D |
| Hint lines (×2) | small italic note | Section 1D |

**Group: "Spectrum Overlays"** — group box collecting the four corner-text
overlay toggles (NF, bin width, peak value, MHz on cursor). Avoids scattering
overlay toggles across the page.

Files: `DisplaySetupPages.cpp`, `SpectrumWidget.cpp`, `FFTEngine.cpp` (decimation).

### 2C — Spectrum Peaks (new page)

The deepest sub-area. Two groups, no handwaving on either.

**Active Peak Hold (5 controls):**

- New class `ActivePeakHoldTrace` in `src/gui/spectrum/`
- Internal state: `peak[N]` array sized to `visibleBinCount`
- Per FFT update: `peak[i] = max(peak[i], current[i])`
- Per frame: `peak[i] = max(peak[i] - dropDbPerSec/fps, current[i])`
- Render as separate trace on `SpectrumWidget` — own VBO (GPU path) or
  QPainter overlay (CPU path); honor TX state via `ActivePeakHoldOnTx` flag
- Optional fill mode: semi-transparent area between peak trace and current
  trace
- **Render path: separate pass** (per Q14.1 decision — flexibility over
  marginal compositing speedup)

**Peak Blobs (7 controls + 2 ColorSwatchButtons):**

- New class `PeakBlobDetector` in `src/gui/spectrum/`
- Per frame: find local maxima (`current[i] > current[i-1] &&
  current[i] > current[i+1]`); sort by dBm descending; take top N
- Optional inside-filter constraint: only consider bins within current slice's
  filter passband (low-cut and high-cut bin indices via
  `SliceModel::filterLow/HighHz` → bin)
- Per-blob state: `(binIndex, max_dBm, time)`
- Update on higher-value reads; apply hold + decay per `BlobPeakHold` /
  `BlobPeakHoldMS` / `BlobPeakHoldDrop` / `PeakBlobFallDbPerSec`
- **Decay math verbatim from Thetis `Display.cs:5483 [v2.10.3.13]`:**
  `entry.max_dBm -= m_dBmPerSecondPeakBlobFall / (float)m_nFps;`
- Render: ellipse at (bin → x pixel, dBm → y pixel) + text label `"f1 dBm"`
  next to it. QPainter overlay (cheap — typically <16 blobs).
- **Default colors per Thetis `Display.cs:8434-8435 [v2.10.3.13]`:**
  blob `OrangeRed`, text `Chartreuse`. Exposed as ColorSwatchButton per
  NereusSDR pattern (3G-8 convention) — user-customizable on top of Thetis
  defaults.

Cross-link: `[← Spectrum defaults]` button.

**Files (all new):**

- `src/gui/setup/SpectrumPeaksPage.cpp/h`
- `src/gui/spectrum/ActivePeakHoldTrace.cpp/h`
- `src/gui/spectrum/PeakBlobDetector.cpp/h`
- Wire-in: `SpectrumWidget.cpp`

**Tests:**

- `test_peak_blob_detection` — synthetic FFT with known peaks
- `test_peak_blob_decay` — hold + decay math correctness
- `test_peak_blob_inside_filter` — filter passband constraint
- `test_active_peak_hold_decay` — per-bin decay correctness
- `test_active_peak_hold_on_tx_state` — TX-state gating

### 2D — Waterfall Defaults page changes

- **Remove** W5 (Reverse Scroll) — control + AppSettings key + render hook;
  drop `DisplayReverseWaterfallScroll` in migration (Section 7)
- **Wire W7 Custom color scheme parser** — runtime color palette parsing for
  the `Custom` scheme in W10 (deferred 3G-8 item)
- **Add NF-AGC group:** `WaterfallNFAGCEnabled` + `WaterfallAGCOffsetDb`
- **Add Stop-on-TX:** `WaterfallStopOnTx` — pause `pushWaterfallRow()` during
  TX state
- **Add Calculated Delay readout:** live label
  `rowsInBuffer * updatePeriodMs / 1000` → `"Delay: NN.N s"`
- **Add Copy button:** "Copy spectrum min/max → waterfall thresholds" — sets
  W1/W2 from current G2/G3 values
- **Add Detector + Averaging combos** (from 2A handwave fix)
- **W8-W9 timestamps:** retained, mixed in with the rest of the page (no
  separate group box per Q18.1 = B). Source code marker
  `// NereusSDR-original — no Thetis equivalent.` on runtime hooks; tooltips
  plain English; no Thetis cites in UI strings.

Files: `DisplaySetupPages.cpp`, `SpectrumWidget.cpp`.

### 2E — Grid & Scales page extensions + per-band NF priming

**NF-aware grid group (3 ctrls):**

- `AdjustGridMinToNoiseFloor` (checkbox)
- `NFOffsetGridFollow` (spinbox, dB, default 0, range -60..+60)
- `MaintainNFAdjustDelta` (checkbox)

Wire to existing `NoiseFloorEstimator` from 3G-9c. Per FFT update: if
`AdjustGridMinToNoiseFloor` enabled, compute `proposedMin = nfEstimate + offset`;
if `MaintainNFAdjustDelta`, also bump `dbMax` by the delta to preserve range.

**Copy button:** waterfall thresholds → spectrum min/max (reverse direction
of 2D Copy).

**Per-band NF priming (NereusSDR-original enhancement, Section 6A):**

- Extend `BandGridSettings` struct with `bandNFEstimate` member (next to
  existing `clarityFloor`)
- On band change: `nfEstimator.prime(perBandGrid(newBand).bandNFEstimate)`
- On NF settle (variance < 1 dB sustained for 2s): persist
  `m_perBandGrid[currentBand].bandNFEstimate = nfEstimator.value()`
- Persist as `DisplayBandNFEstimate_<bandKeyName>` × 14 (matches 3G-8's
  `gridMaxKey()` convention via `bandKeyName(Band)` helper)
- Code marker: `// NereusSDR-original — no Thetis equivalent.`

Files: `DisplaySetupPages.cpp`, `PanadapterModel.cpp` (extend BandGridSettings
+ persistence), `SpectrumWidget.cpp` (NF-aware grid logic),
`NoiseFloorEstimator.cpp` (add `prime(double)` method).

**Tests:**

- `test_nf_aware_grid_offset`
- `test_nf_aware_grid_maintain_delta`
- `test_per_band_nf_priming` — primer applies stored value on band-change
- `test_per_band_nf_save_on_settle` — save fires after variance threshold
- `test_clarity_and_nf_grid_coexistence` — both enabled, no oscillation

### 2F — S2 FFT Window cite correction

Pure attribution fix. Today `SpectrumDefaultsPage.cpp` for the FFT Window combo
has a `// NereusSDR-original` comment. Replace with
`// From Thetis setup.cs:NNNN [v2.10.3.13] — comboDispWinType`. Update
tooltip if needed (remove any "NereusSDR" framing).

One commit, one-line audit during implementation.

### Section 2 totals

- **Net new controls:** ~28 (Detector/Averaging splits + Spectrum Defaults
  extensions + Spectrum Peaks + Waterfall changes + NF-grid + cite fix)
- **Net new classes:** 4 (`SpectrumPeaksPage`, `ActivePeakHoldTrace`,
  `PeakBlobDetector`, plus `BandGridSettings` extension)
- **Net new tests:** ~17
- **Estimated work:** ~14 commits, ~7 days

---

## Section 3 — Display-General audit (Work Area #2)

### 3A — Display → Multimeter (new page)

The single net-new page in Section 3.

**Multimeter group (8 controls):**

| Thetis name | NereusSDR key | Wires to |
|---|---|---|
| `udDisplayMeterDelay` | `MultimeterDelayMs` | `MeterPoller` polling interval (currently hardcoded 100ms) |
| `udDisplayMultiPeakHoldTime` | `MultimeterPeakHoldMs` | `BarItem` peak indicator hold |
| `udDisplayMultiTextHoldTime` | `MultimeterTextHoldMs` | `TextItem` / `SignalTextItem` text hold |
| `udDisplayMeterAvg` | `MultimeterAverageWindow` | `MeterPoller` averaging window |
| `udMeterDigitalDelay` | `MultimeterDigitalDelayMs` | digital readout polling |
| `chkDisplayMeterShowDecimal` | `MultimeterShowDecimal` | TextItem / SignalTextItem decimal-display flag |
| `radSReading`/`radDBM`/`radUV` (radio group → combo) | `MultimeterUnitMode` | `S \| dBm \| uV` enum — drives unit display in **all** MeterItem subclasses (BarItem, TextItem, SignalTextItem, NeedleItem, HistoryGraphItem) |
| `chkSignalHistory` + `udSignalHistoryDuration` | `MultimeterSignalHistoryEnabled` + `MultimeterSignalHistoryDurationMs` | `HistoryGraphItem` data length |

**Unit-mode fan-out (per Q15.1 = approved):** every relevant `MeterItem` subclass
extends with a unit-aware display-string method that consults
`MultimeterUnitMode`. S-mode shows "S5/9", dBm shows "-85", µV shows "1.4".

Cross-link: `[← Spectrum defaults]` button.

**Files:**

- `MultimeterPage.cpp/h` (new)
- `MeterPoller.cpp` (configurable interval + averaging window)
- All `MeterItem` subclasses (extend with unit-mode awareness)

**Tests:**

- `test_multimeter_unit_conversion` — S vs dBm vs µV math
- `test_multimeter_peak_hold_timing`
- `test_signal_history_duration`

### 3B — Relocations (4 mechanical moves to existing pages)

| Thetis Display→General control | NereusSDR home (existing) | Work |
|---|---|---|
| `chkSmallModeFilteronVFOs` | `MeterStylesPage` (new "VFO Flag" group box) | Wire to `VfoWidget` filter-display-style flag |
| `chkShowMHzOnCursor` | already in Section 2B Spectrum Defaults | done in 2B |
| `chkPurgeBuffers` + 2 Spec Warning LEDs | Existing Diagnostics page (rename "Logging" → "Logging & Performance"; new "Performance" group box per Q15.2 = approved) | Wire 2 LED warnings to `SpectrumWidget` render-delay + pixel-fetch monitors; wire purge-buffers to FFTEngine buffer reset |
| `chkANAN8000DLEDisplayVoltsAmps` | `RadioInfoTab` (cap-gated on `caps.is8000DLE`) | Wire to chrome titlebar volts/amps display (existing 3Q layer) |

### 3C — `udDisplayCPUMeter` → General → Options

Single control (CPU meter rate). Wire to chrome titlebar's CPU usage update
rate (existing 3Q chrome layer).

Files: `GeneralOptionsPage.cpp`, chrome titlebar logic.

### 3D — Drops (anti-alias / accurate frame timing / VSync DX)

DirectX-9-era display-driver toggles that don't apply to NereusSDR's QRhi
(Metal/Vulkan/D3D12) renderer. Qt + QRhi handle anti-aliasing and VSync
transparently per platform.

If a future need for an "advanced rendering" toggle emerges, add it as a
NereusSDR-original option at that time. Not in v0.3 scope.

### 3E — Deferred (Phase scope, Scope mode)

Thetis Display→General has a "Phase" group (3 controls) and a "Scope Mode"
group (1 control) for alternate display modes — phase scope and oscilloscope.
NereusSDR doesn't have a display-mode framework yet (only spectrum +
waterfall). Deferred until Phase 3F+ when display-mode infrastructure lands.

### Section 3 totals

- **Net new Setup pages:** 1 (Multimeter)
- **Existing pages extended:** 4 (MeterStylesPage, Diagnostics Logging,
  RadioInfoTab, GeneralOptionsPage)
- **Net new controls:** ~13
- **Drops:** 3 (anti-alias / frame timing / VSync DX)
- **Deferrals:** 4 (Phase scope group + Scope mode)
- **Net new classes:** 1 (`MultimeterPage`)
- **Net new tests:** 3
- **Estimated work:** ~5 commits, ~3 days

---

## Section 4 — DSP → Options audit (Work Area #3)

### 4A — DSP → Options page (new)

Mirror Thetis layout 1:1 per `feedback_thetis_userland_parity`. **18 controls
+ 1 readout + 3 warning icons.**

**Group 1 — Buffer Size (4 combos):**

| Control | Values |
|---|---|
| `DspOptionsBufferSizePhone` | 64/128/256/512/1024/2048 (verify exact Thetis enum during impl) |
| `DspOptionsBufferSizeCw` | same |
| `DspOptionsBufferSizeDig` | same |
| `DspOptionsBufferSizeFm` | same |

**Group 2 — Filter Size / Taps (4 combos):**

| Control | Values |
|---|---|
| `DspOptionsFilterSizePhone` | 1024/2048/4096/8192/16384 (verify) |
| `DspOptionsFilterSizeCw` | same |
| `DspOptionsFilterSizeDig` | same |
| `DspOptionsFilterSizeFm` | same |

**Group 3 — Filter Type (7 combos):**

| Control | Values |
|---|---|
| `DspOptionsFilterTypePhoneRx` | Low-Latency / Linear-Phase |
| `DspOptionsFilterTypePhoneTx` | same |
| `DspOptionsFilterTypeCwRx` | same (CW has no TX filter type per Thetis layout) |
| `DspOptionsFilterTypeDigRx` | same |
| `DspOptionsFilterTypeDigTx` | same |
| `DspOptionsFilterTypeFmRx` | same |
| `DspOptionsFilterTypeFmTx` | same |

**Group 4 — Filter Impulse Cache (2 checkboxes):**

- `DspOptionsCacheImpulse` — enable WDSP impulse cache (memory ↔ first-load
  time tradeoff)
- `DspOptionsCacheImpulseSaveRestore` — persist cache to disk between
  sessions (~`~/.config/NereusSDR/wdsp_impulse.cache`)

**Standalone:**

- `DspOptionsHighResFilterCharacteristics` (checkbox) — affects filter-graph
  display detail (Section 4D — full impl in this PR, no deferral)
- **"Time to make last change: NN ms"** live readout label — updated after
  every channel rebuild (Section 5 infrastructure)

**Warning icons (3):** small `QLabel` with warning glyph next to
Buffer/Filter/Buffer-Type combos. `setVisible()` driven by Thetis validity
rules — port from `setup.cs` `_ValueChanged` handlers verbatim
(`pbWarningBufferSize.Visible = ...` etc.). Tooltip text plain English; no
Thetis cite in user string.

Visual treatment per Q16.3 = approved: small yellow `⚠` Unicode glyph +
tooltip ("Buffer size cannot exceed filter size" or specific reason).

Files: `DspOptionsPage.cpp/h` (new), wired into existing DSP category in
`SetupDialog.cpp`.

### 4B — Per-mode live-apply behavior

Each buffer/filter/filter-type setting is **per-mode**. Behavior on combo
change:

| Scenario | Action |
|---|---|
| Changed setting's mode matches current `SliceModel::mode()` | Rebuild active channel now (Section 5 infrastructure) |
| Changed setting's mode is different (e.g., user is in CW, changes Phone buffer size) | Store to AppSettings; applies on next mode-switch |

The "Time to last change" readout shows ms elapsed during the most recent
actual rebuild. If only-stored, displays
`"Time to last change: — (stored, not applied)"`.

**Mode-switch wiring:**

- `SliceModel::setMode(newMode)` → emit `modeChanged(newMode)`
- `RxChannel::onModeChanged(newMode)` reads `DspOptionsBufferSize<NewMode>` +
  `DspOptionsFilterSize<NewMode>` + `DspOptionsFilterType<NewMode>Rx` from
  AppSettings; calls `rebuild()` if any value differs from current state
- `TxChannel` similar

Files: `RxChannel.cpp`, `TxChannel.cpp`, `DspOptionsPage.cpp`.

### 4C — Filter Impulse Cache wiring

`DspOptionsCacheImpulse` → toggles WDSP impulse caching at channel-create time.
`DspOptionsCacheImpulseSaveRestore` → toggles persisting the cache to disk
between app sessions.

Wire to existing `WdspEngine` impulse cache path. Thetis equivalents are
`RadioDSP.CacheImpulse` and `RadioDSP.CacheImpulseSaveRestore` — find the C#
call sites and translate the WDSP API calls verbatim during impl. No
shortcut.

### 4D — High-Resolution Filter Characteristics (full impl, no deferral)

`DspOptionsHighResFilterCharacteristics` toggles whether the filter-response
display shows the actual computed FIR magnitude curve (high-res) vs a
simplified box-shape passband (low-res).

NereusSDR has `FilterDisplayItem` (`src/gui/meters/FilterDisplayItem.cpp`) — a
`MeterItem` subclass that already paints filter edges. This PR extends it.

**Implementation:**

- Add `RxChannel::filterResponseMagnitudes(int nPoints) -> QVector<float>` —
  returns FFT-magnitude of current filter taps. Verify whether RxChannel
  exposes filter taps already; add accessor if missing.
- Add `FilterDisplayItem::setHighResolution(bool)` method.
- When ON: pull `filterResponseMagnitudes()` from the bound RxChannel; render
  the actual computed magnitude curve.
- When OFF: render simplified box-shape passband (current behavior — no
  regression).
- DspOptionsPage checkbox emits a signal connected to all relevant
  FilterDisplayItem instances (via `ContainerManager` broadcast).

### 4E — Warning icons (3)

Three small `QLabel` warning glyphs next to Buffer/Filter/Buffer-Type combos.
Visibility driven by Thetis validity rules — port from `setup.cs`
`_ValueChanged` handlers verbatim.

Visual: yellow `⚠` + tooltip explaining the issue.

### Section 4 totals

- **Net new Setup pages:** 1 (DSP Options)
- **Net new controls:** 18 + 1 readout + 3 warning icons
- **Net new classes:** 1 (`DspOptionsPage`)
- **Net new tests:** 4 (`test_dsp_options_persistence`,
  `test_dsp_options_per_mode_apply`, `test_dsp_options_warning_logic`,
  `test_filter_high_resolution`)
- **Estimated work:** ~6 commits, ~3-4 days

---

## Section 5 — Channel teardown / rebuild infrastructure

The architectural backbone of this PR. Standard scope per Q5 + Q9:
DSP-Options + sample-rate + active-RX-count all live-apply.

### 5A — `RxChannel::rebuild()` / `TxChannel::rebuild()`

**Public API:**

```cpp
struct ChannelConfig {
    int sampleRate;
    int bufferSize;
    int filterSize;
    int filterType;        // LowLatency | LinearPhase
    bool cacheImpulse;
    bool cacheImpulseSaveRestore;
    bool highResFilterCharacteristics;
};

// Tear down WDSP channel, recreate with new config, reapply all per-channel
// state. Returns elapsed ms for the live readout.
qint64 RxChannel::rebuild(const ChannelConfig& newCfg);
```

**State to capture before teardown** (so it can be reapplied after recreate):

```cpp
struct RxChannelState {
    Mode mode;
    int filterLowHz, filterHighHz;
    AgcMode agcMode;
    AgcParams agcParams;
    NbFamilyState nbState;
    NrFamilyState nrState;
    AnfState anfState;
    EqState eqState;
    SquelchState squelchState;
    int ritOffsetHz;
    int antennaIndex;
    double shiftOffsetHz;
    // …everything else RxChannel owns that's mode/filter/etc-dependent
};
```

**Sequence:**

1. `state = captureState();`
2. `wdspDestroyChannel(channelId);`
3. `channelId = wdspCreateChannel(newCfg);`
4. `applyState(state);`
5. Return elapsed ms

**Thread safety:** main thread writes via `std::atomic` per-channel-id
swap; audio thread reads via the atomic. Samples in flight at the moment of
swap are dropped (acceptable — < 1 ms of audio, manifests as a tick).

Files: `RxChannel.cpp`, `TxChannel.cpp`, `WdspEngine.cpp`.

### 5B — Setters that trigger rebuild

```cpp
void RxChannel::setBufferSize(int bytes);     // calls rebuild() if running
void RxChannel::setFilterSize(int taps);
void RxChannel::setFilterType(int type);
void RxChannel::setCacheImpulse(bool);        // cheap toggle, no rebuild
void RxChannel::setSampleRate(int hz);        // full rebuild + hardware reconfig
```

`setCacheImpulse` toggles a flag in WDSP without channel destroy — cheap.
The other three trigger full rebuild.

### 5C — Sample-rate live-apply path

Multi-step coordinated dance:

1. **Quiesce audio:** `AudioEngine::pauseInput()`
2. **Quiesce hardware:** P1 send "stop" command; P2 stop receiving on DDC ports
3. **Reconfigure radio:** P1 issue new sample-rate command + restart NetworkIO;
   P2 send sample-rate change command
4. **Rebuild WDSP channels:** all RxChannel/TxChannel call `rebuild()` with new
   sampleRate
5. **Re-init AudioEngine:** new buffer sizes derived from new sample rate
6. **Resume hardware:** start streaming
7. **Resume audio:** `AudioEngine::resumeInput()`

**Risk:** P1 NetworkIO restart is untested cross-thread coordination. Add
focused integration test before relying on this in a release.

Files: `P1RadioConnection.cpp`, `P2RadioConnection.cpp`, `AudioEngine.cpp`,
`RxChannel.cpp`.

### 5D — Active-RX-count live-apply path

User enables RX2 on a Hermes/Angelia/ANAN that has 2+ DDCs:

1. **Pause hardware** (same as 5C)
2. **Reconfigure DDC mapping:** `ReceiverManager::setActiveRxCount(N)` updates
   DDC→Receiver map
3. **Create/destroy WDSP channels:** for each newly-activated receiver, create
   RxChannel; for each de-activated, destroy
4. **For P1:** EP6 frame parser handles count change. **Verify:** does
   `MetisFrameParser` support mid-stream count changes today? If not, ~2-day
   rework absorbed into this PR per Q17.3 = approved.
5. **For P2:** add/remove DDC port subscriptions
6. **Resume hardware**

Files: `ReceiverManager.cpp`, `P1RadioConnection.cpp`, `P2RadioConnection.cpp`,
`MetisFrameParser.cpp`.

### 5E — `lblTimeToMakeDSPChange` readout

`RxChannel::rebuild()` returns elapsed ms. The DspOptionsPage subscribes to a
new signal `RadioModel::dspChangeMeasured(qint64 ms)` and updates the label.

Format:
- Live-applied: `"Time to last change: NN ms"`
- Stored only: `"Time to last change: — (stored, not applied)"`

### 5F — AudioEngine re-init when buffer size changes

`AudioEngine` (QAudioSink) output frame size depends on sample rate + buffer
size. When buffer size changes:

1. Drain current output buffer
2. Recreate `QAudioSink` with new format
3. Resume output

Files: `AudioEngine.cpp`.

### 5G — Glitch budget

Target: rebuild < 50 ms on a typical machine (Thetis hits ~20-50 ms). Audio
glitch limited to one buffer interval (~10-20 ms at 48 kHz). User perception:
brief click during explicit DSP setting changes — same as Thetis, acceptable
(per Q17.1 = approved).

If we routinely exceed 100 ms, that's a UX issue and triggers a follow-up
"pre-warm" infrastructure design.

### 5H — Tests

- `test_rx_channel_rebuild` — destroy/recreate cycle preserves all per-channel
  state
- `test_tx_channel_rebuild` — same for TX
- `test_sample_rate_live_apply` — full path including hardware coordination
  (mocked)
- `test_active_rx_count_live_apply` — both P1 and P2 paths
- `test_dsp_change_timer` — readout updates after rebuild
- `test_audio_engine_reinit_buffer_size` — output buffer drains cleanly
- `test_rebuild_glitch_budget` — measure rebuild time on synthetic load

### Section 5 totals

- **Net new APIs:** `RxChannel::rebuild()`, `TxChannel::rebuild()`,
  `RxChannel::filterResponseMagnitudes()`, captured-state structs,
  sample-rate-coordinator helper
- **Files touched:** 8-10
- **Net new tests:** 7
- **Estimated work:** ~8 commits, ~5-7 days (riskiest section — cross-thread +
  cross-protocol coordination)

---

## Section 6 — NereusSDR-original enhancements

Consolidates non-Thetis additions in this PR. Each carries the
`// NereusSDR-original — no Thetis equivalent.` comment marker; tooltips and
user-visible strings stay plain English.

### 6A — Per-band NF-estimate priming

Spec'd in 2E. Code marker on the priming code path. Tests `test_per_band_nf_priming`
and `test_per_band_nf_save_on_settle`.

### 6B — Waterfall Timestamps (W8-W9 retained)

Per Q18.1 = B: **mixed in with the rest of the Waterfall Defaults page** — no
separate group box. Source code comments mark them as NereusSDR-original; UI
doesn't visually distinguish them. Existing 3G-8 verification rows for W8/W9
retained.

### 6C — Clarity (3G-9c) — preserved untouched

No behavior changes. **Coordination:** new `AdjustGridMinToNoiseFloor` (2E)
reads from the same `NoiseFloorEstimator` instance Clarity uses. Both features
can be enabled simultaneously without double-tuning since they consume the
same NF stream and apply to different display dimensions (grid bounds vs
waterfall thresholds).

Test: `test_clarity_and_nf_grid_coexistence`.

### 6D — Reset to Smooth Defaults — preserved unchanged

Per Q18.2 = B: **no changes to the existing preset map.** New v0.3 keys
default to their migration values (Thetis defaults for ported controls;
sensible defaults for NereusSDR-original). The smooth-defaults button retains
its existing 3G-9b scope.

### 6E — ColorSwatchButton extensions (Peak Blobs)

Two new ColorSwatchButtons on Spectrum Peaks page. Defaults match Thetis
verbatim. Underlying behavior is Thetis-faithful — only the
user-customization surface is added per existing NereusSDR pattern.

Code marker: standard Thetis cite + note
`// — exposed as ColorSwatchButton per NereusSDR pattern (3G-8 convention)`.

### Section 6 totals

- **Net new NereusSDR-original code:** ~150 LOC
- **Net new tests:** 3 (priming + save-on-settle + Clarity-coexistence)
- **Estimated work:** ~2 commits, ~1-2 days (mostly absorbed by other sections)

---

## Section 7 — Settings migration policy

### 7A — Affected keys

**Retired (deleted on first launch of v0.3.0):**

| Old key | Retired because |
|---|---|
| `DisplayAverageMode` | Split into `DisplaySpectrumDetector` + `DisplaySpectrumAveraging` (2A) |
| `DisplayPeakHold` | Promoted to `DisplayActivePeakHoldEnabled` (2C — semantics changed) |
| `DisplayPeakHoldDelayMs` | Promoted to `DisplayActivePeakHoldDurationMs` (same) |
| `DisplayReverseWaterfallScroll` | Removed (W5 dropped per Q4) |

No silent value migration. User reconfigures via Setup → Display.

### 7B — Migration mechanism

```cpp
// In AppSettings::init() or equivalent
const QString versionKey = QStringLiteral("SettingsSchemaVersion");
const int currentVersion = 3;  // bumped from 2 → 3 for this PR
const int storedVersion = value(versionKey, "0").toInt();

if (storedVersion < 3) {
    qCInfo(lcSettings) << "Migrating settings to schema v3 (NereusSDR v0.3.0)";

    remove("DisplayAverageMode");
    remove("DisplayPeakHold");
    remove("DisplayPeakHoldDelayMs");
    remove("DisplayReverseWaterfallScroll");

    setValue(versionKey, currentVersion);
    qCInfo(lcSettings) << "Settings migration complete";
}
```

If `SettingsSchemaVersion` doesn't exist today, this PR introduces it. Existing
v0.2.x installations get treated as `storedVersion = 0` → migration runs once.
New installations get `storedVersion = 3` immediately on first launch.

### 7C — CHANGELOG entry (v0.3.0)

> **Display settings migration:** Several display preferences were restructured
> to match Thetis behavior more faithfully. The following settings reset to
> defaults on first launch of v0.3.0:
>
> - Spectrum / waterfall averaging mode (now split into separate Detector and
>   Averaging method controls)
> - Spectrum peak hold (replaced with Active Peak Hold per-bin trace; configure
>   under Setup → Display → Spectrum Peaks)
> - Reverse waterfall scroll (removed)
>
> Reconfigure these under Setup → Display.

### 7D — Verify-during-impl: callsite audit

Before commit:

```sh
grep -rn "DisplayAverageMode\|DisplayPeakHold\|DisplayReverseWaterfallScroll" src/ tests/ docs/
```

Expected zero hits after migration; any hit is a missed callsite to update.

### 7E — Tests

- `test_settings_migration_v0_3_0` — set up v0.2.x state, run migration, verify
  retired keys gone and `SettingsSchemaVersion=3`
- `test_settings_migration_idempotent` — second call no-ops
- `test_settings_migration_fresh_install` — no settings file → `SettingsSchemaVersion=3`
  written, no migration log

### Section 7 totals

- **Net new code:** ~30 LOC
- **Net new tests:** 3
- **Estimated work:** ~1 commit, ~0.5 days

---

## Section 8 — Testing strategy + verification matrix

### 8A — Unit tests by section

Total ~30 new tests; full per-section breakdown in Sections 2-7 above.

### 8B — Verification matrix extension

Extends `docs/architecture/phase3g8-verification/`:

```
docs/architecture/phase3g8-verification/
├── README.md                         (existing — extend table)
├── spectrum-defaults-extensions.md   (new — ~9 rows for 2B)
├── spectrum-peaks.md                 (new — ~12 rows for 2C)
├── waterfall-defaults-changes.md     (new — ~6 rows for 2D)
├── grid-scales-extensions.md         (new — ~4 rows for 2E)
├── multimeter.md                     (new — ~13 rows for 3A)
├── dsp-options.md                    (new — ~22 rows for Section 4)
└── infrastructure-live-apply.md      (new — ~6 rows for Section 5 manual)
```

**Total new rows:** ~72.

**Per-row format** (matching 3G-8):

- Control name (UI label)
- AppSettings key
- Default value
- Expected behavior on change
- Screenshot expectation (filename for the verification capture)

### 8C — Manual bench session (merge gate per Q19.3)

Before merge — JJ runs:

- ANAN-G2 primary: live-apply, sample-rate live-apply, RX2 toggle, NF priming
  per-band-change, NF-aware grid, Active Peak Hold + Peak Blobs sweep
- HL2 secondary: same checks where capability allows

### 8D — Pre-commit hook chain

Per CLAUDE.md ship-block rule: every commit runs `verify-thetis-headers.py`,
`verify-inline-tag-preservation.py`, `check-new-ports.py` locally and in CI.
No `--no-verify`. License headers + PROVENANCE rows verified mechanically.

### Section 8 totals

- **Net new tests:** ~30
- **Net new matrix files:** 7
- **Net new matrix rows:** ~72
- **Manual bench:** ~1 day
- **Estimated work:** ~3 commits

---

## Section 9 — Commit organization

### 9A — Logical commit clusters

**Cluster 1 — Infrastructure (Section 5) — must land first**

- `feat(infra): add RxChannel::rebuild() with state capture/restore`
- `feat(infra): add TxChannel::rebuild()`
- `feat(infra): add RxChannel::filterResponseMagnitudes() accessor`
- `feat(infra): sample-rate live-apply coordinator (P1 + P2 paths)`
- `feat(infra): active-RX-count live-apply (P1 EP6 + P2 DDC port handling)`
- `feat(infra): AudioEngine re-init on buffer-size change`
- `feat(infra): DspChangeTimer + RadioModel::dspChangeMeasured signal`
- `test(infra): channel rebuild + live-apply integration tests`

**Cluster 2 — RX1 work (Section 2)**

- `fix(handwave): split DisplayAverageMode into Detector + Averaging (spectrum)`
- `fix(handwave): split waterfall averaging into Detector + Averaging`
- `fix(cite): correct S2 FFT Window provenance to Thetis`
- `feat(display): show MHz on cursor + Get-Monitor-Hz button + decimation wire-up`
- `feat(display): show noise floor + show bin width + disp-normalize overlays`
- `feat(display): show peak value overlay + position + delay (PeakTextDelay)`
- `feat(display): SpectrumPeaksPage skeleton + cross-link buttons`
- `feat(display): ActivePeakHoldTrace class + render path`
- `feat(display): PeakBlobDetector class + ellipse/text rendering`
- `feat(display): waterfall NF-AGC + Stop-on-TX + calculated-delay`
- `feat(display): Copy buttons (spectrum ↔ waterfall thresholds)`
- `feat(display): NF-aware grid (AdjustGridMinToNoiseFloor + offset + maintain-delta)`
- `feat(display): per-band NF-estimate priming (NereusSDR-original)`
- `feat(display): waterfall W5 reverse-scroll removal`
- `test(display): RX1 audit unit tests (~17 tests)`

**Cluster 3 — Display-General fold (Section 3)**

- `feat(display): MultimeterPage + MeterPoller config + MeterItem unit-mode fan-out`
- `feat(display): SmallModeFilteronVFOs in MeterStylesPage VFO Flag group`
- `feat(diagnostics): Logging & Performance page rename + Spec Warning LEDs + Purge Buffers`
- `feat(hardware): ANAN-8000DLE volts/amps toggle in RadioInfoTab (cap-gated)`
- `feat(general): CPU meter rate spinbox`
- `test(display): Multimeter audit unit tests (~3 tests)`

**Cluster 4 — DSP Options (Section 4)**

- `feat(dsp): DspOptionsPage skeleton + 18 controls + warning icons`
- `feat(dsp): per-mode buffer/filter/filter-type live-apply via Cluster 1 rebuild`
- `feat(dsp): impulse cache toggles + WDSP wire-up`
- `feat(dsp): high-resolution filter characteristics + FilterDisplayItem high-res mode`
- `feat(dsp): time-to-last-change readout`
- `test(dsp): DSP-Options audit unit tests (~4 tests)`

**Cluster 5 — Migration + verification matrix + CHANGELOG**

- `feat(settings): SettingsSchemaVersion key + v0.3.0 migration`
- `docs(verification): extend phase3g8-verification with 7 new matrix files`
- `docs(changelog): v0.3.0 entry — display + DSP parity work`

**Cluster 6 — Polish (last)**

- Code-review-driven fixes
- Tooltip pass for new controls
- Final manual-bench fixes

### 9B — Commit ordering rules

- Cluster 1 first (others depend on rebuild infrastructure)
- Cluster 4 depends on Cluster 1
- Cluster 2 depends on Cluster 1 (channel rebuild used by Detector/Averaging
  split)
- Clusters 2, 3, 4 can interleave once Cluster 1 lands
- Cluster 5 lands near the end
- Cluster 6 is review-driven

### 9C — Per-commit hygiene

Per CLAUDE.md and standing rules:

- All commits GPG-signed (`feedback_gpg_sign_commits`)
- Source-cited where porting (`// From Thetis <file>:<line> [v2.10.3.13]`)
- Inline tag preservation verified by pre-commit hook chain
- License headers preserved for any new file ported from Thetis
- PROVENANCE row added in `docs/attribution/THETIS-PROVENANCE.md` for every
  new ported file
- No `--no-verify` (`feedback_no_ci_unblock_shortcuts`)
- No commits skipped through hook failures

### 9D — PR description shape

Title: `feat: Display + DSP parity audit (RX1 + General + Options) + WDSP channel rebuild infrastructure`

Body:

- Section-by-section summary lifted from this design doc
- Manual test results from the bench session
- Verification matrix screenshot count
- CHANGELOG diff
- "Closes #N" links for any open issues this addresses

---

## Appendix A — Verify-during-impl items

Items the design defers to grep / source-read at implementation time:

1. **Thetis WDSP detector / averaging API signatures** (Section 2A) — verify
   via `dsp.cs` P/Invoke decls before calling
2. **Thetis `DispNormalize` algorithm** (Section 2B) — find exact line in
   `Display.cs` before porting
3. **Thetis buffer/filter combo enum values** (Section 4A) — capture exact
   value lists from `setup.designer.cs`
4. **Thetis `pbWarningBufferSize` / `pbWarningFilterSize` validity rules**
   (Section 4E) — port from `setup.cs` `_ValueChanged` handlers verbatim
5. **`MetisFrameParser` mid-stream RX-count change support** (Section 5D) —
   verify whether existing parser handles it; ~2-day rework absorbed if not
6. **`RxChannel` filter-tap accessor** (Section 4D) — verify whether exists;
   add if missing
7. **NereusSDR `udDisplayPeakText` corner readout existence** (Section 2B,
   3D) — locked to add the readout if not present (no handwave)

---

## Appendix B — Explicit deferrals

Items spec'd but explicitly out of scope for v0.3:

- Thetis `Display → General` Phase scope group (3 ctrls) — needs
  display-mode framework, Phase 3F+
- Thetis `Display → General` Scope Mode group (1 ctrl) — same
- Thetis `chkAntiAlias` / `chkAccurateFrameTiming` / `chkVSyncDX` — DirectX-9-era,
  QRhi handles transparently
- Thetis `SpectrumGridMaxMoxModified` (TX-state grid overrides) —
  not in v0.3 scope; revisit when transmit display work happens
- DDC reassignment / Diversity / MultiRX2 surface — Phase 3F (panadapter-stack
  rework dependency)
- IMD3/IMD5 measurements (Thetis ties to Peak Blob render path) —
  PeakBlobDetector designed extensible so IMD detection can attach later
- Thetis `console.PeakTextColor` runtime flexibility — we hardcode default
  `DodgerBlue`, expose user override via ColorSwatchButton (per NereusSDR
  pattern)

---

## Appendix C — Risk register

| Risk | Likelihood | Mitigation |
|---|---|---|
| Channel rebuild glitch budget exceeds 100 ms routinely | Low | Synthetic load test; if exceeded, follow-up "pre-warm" design |
| P1 NetworkIO restart cross-thread coordination buggy | Medium | Focused integration test before relying in release |
| `MetisFrameParser` needs rework for active-RX-count change | Medium | Absorb into PR per Q17.3; ~2 days |
| Settings migration breaks user data | Low | Schema version key + idempotent migration; tests cover edge cases |
| Manual bench session reveals issues with combinations not in unit tests | Medium | Cluster 6 (polish) absorbs follow-up fixes |
| PR review effort overwhelming due to size | High | Each commit small + source-cited; reviewer can review cluster-by-cluster |

---

## Sign-off

- **Brainstorm session:** 2026-05-01 with KG4VCF
- **Design doc author:** Claude (Opus 4.7) via brainstorming skill
- **Approved by:** KG4VCF (sections 0-9 approved iteratively)
- **Implementation plan:** TBD via writing-plans skill
- **Verification:** docs/architecture/phase3g8-verification/ extension + manual bench
