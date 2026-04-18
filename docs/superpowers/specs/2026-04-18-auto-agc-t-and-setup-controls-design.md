# Auto AGC-T, Setup AGC/ALC Wiring, and Slider Visual Enhancement

**Date:** 2026-04-18
**Status:** Design approved
**Scope:** Phase 3G-10 continuation — AGC-T auto tracking, Setup → DSP → AGC/ALC
control wiring, and VFO/RxApplet slider visual enhancement.

---

## 1. Overview

Four tightly coupled features, all Thetis source-first:

1. **Noise floor estimation** — port Thetis `Display.NoiseFloorRX1` FFT bin
   averaging into NereusSDR's FFT pipeline.
2. **Auto AGC-T engine** — 500ms timer-based automatic AGC threshold tracking
   from the noise floor, with configurable offset.
3. **Setup → AGC/ALC control wiring** — enable the existing disabled
   `AgcAlcSetupPage` controls and connect them to SliceModel → RxChannel → WDSP.
4. **AGC-T slider visual enhancement** — green handle, NF fill zone, AUTO badge,
   and info sub-line on both VFO Audio tab and RxApplet AGC-T sliders.

---

## 2. Noise Floor Estimation

### Thetis Source

- `display.cs:4628-4693` — `m_fNoiseFloorRX1/RX2`, `m_fLerpAverageRX1/RX2`,
  `m_fFFTBinAverageRX1/RX2`, `m_fAttackTimeInMSForRX1/RX2` (default 2000ms),
  `FastAttackNoiseFloorRX1/RX2`
- `display.cs:917-936` — `FastAttackNoiseFloorRX1/RX2` property (set true on
  retune, mode change, preamp change)
- `display.cs:4664-4678` — `NoiseFloorRX1/RX2` getter clears
  `m_bNoiseFloorGoodRX1/RX2` on read (consumer-acknowledges-read pattern)

### NereusSDR Design

**New class: `NoiseFloorTracker`** (header-only or small .cpp in `src/core/`)

- Receives FFT magnitude bins from `FFTEngine::spectrumReady()` (already emitted
  every frame).
- Computes per-frame bin average (mean of all magnitude bins in dBm).
- Lerps toward the bin average with a configurable attack time (default 2000ms
  from Thetis `m_fAttackTimeInMSForRX1`).
  ```
  // From Thetis display.cs:4635-4636 — lerp toward bin average
  float alpha = 1.0f - exp(-frameIntervalMs / attackTimeMs);
  m_lerpAverage += alpha * (binAverage - m_lerpAverage);
  ```
- Fast-attack mode: on retune or mode change, reset lerp to immediate bin
  average (settles instantly, then resumes slow tracking).
  ```
  // From Thetis display.cs:917-926 — FastAttackNoiseFloorRX1
  ```
- Emits `noiseFloorUpdated(float dBm, bool isGood)` — `isGood` is true after
  the lerp has had at least one full attack period to settle.

**Integration point:** `RadioModel` connects `FFTEngine::spectrumReady` →
`NoiseFloorTracker::feedSpectrum()`. The tracker lives on the main thread
(receives queued signal from FFT worker thread).

**Fast-attack triggers** (from Thetis display.cs:880-911):
- `SliceModel::frequencyChanged` — if delta > 500 Hz
- `SliceModel::modeChanged`
- Preamp/attenuator change

---

## 3. Auto AGC-T Engine

### Thetis Source

- `console.cs:45913-45954` — `m_bAutoAGCRX1/RX2`, `m_dAutoAGCOffsetRX1/RX2`
  properties; setter updates `Display.AutoAGCRX1`, sets `ptbRF.GreenThumb`,
  fires `AGCAutoModeChangedHandlers`
- `console.cs:46057-46116` — `tmrAutoAGC_Tick`: 500ms timer, reads
  `Display.NoiseFloorRX1`, calls
  `setAGCThresholdPoint(_lastRX1NoiseFloor + m_dAutoAGCOffsetRX1, 1)`
- `console.cs:45960-46008` — `setAGCThresholdPoint()`: applies `agcCalOffset()`,
  clamps to [-160, +2] dB, calls `WDSP.SetRXAAGCThresh()`, reads back
  `WDSP.GetRXAAGCTop()`, updates RF Gain/AGCMaxGain/AGCFixedGain
- `console.cs:33292-33319` — `agcCalOffset(int rx)`: returns
  `2.0f + (DisplayCalOffset + PreampOffset - AlexPreampOffset - FFTSizeOffset)`
  for non-FIXD modes, `0.0f` for FIXD

### NereusSDR Design

**New properties on `SliceModel`:**

| Property | Type | Default | Thetis Origin | Persisted |
|----------|------|---------|---------------|-----------|
| `autoAgcEnabled` | `bool` | `false` | `console.cs:45926` `m_bAutoAGCRX1` | Yes |
| `autoAgcOffset` | `double` | `20.0` | `console.cs:45913` `m_dAutoAGCOffsetRX1` | Yes |

Signals: `autoAgcEnabledChanged(bool)`, `autoAgcOffsetChanged(double)`.

**Auto AGC-T logic on `RadioModel`:**

- 500ms `QTimer` (from Thetis `tmrAutoAGC` interval).
- On tick:
  1. Guard: skip if not connected, or MOX active (from Thetis
     `console.cs:46059`).
  2. Read `NoiseFloorTracker::noiseFloor()` and `isGood()`.
  3. If good and `autoAgcEnabled`:
     ```
     // From Thetis console.cs:46107-46115
     double threshold = noiseFloor + autoAgcOffset;
     setAGCThresholdPoint(threshold);
     ```
  4. `setAGCThresholdPoint()` mirrors Thetis `console.cs:45960-46008`:
     - Apply cal offset (simplified — NereusSDR doesn't have Alex preamp offset
       yet, use `0.0f` for FIXD, `2.0f - fftSizeOffset` for others).
     - Clamp to [-160, +2] dB (from Thetis `console.cs:45969-45970`).
     - Call `RxChannel::setAgcThreshold(dBu)`.
     - Read back `RxChannel::readBackAgcTop()` → sync RF Gain.

**Auto-disable triggers:**

| Trigger | Thetis Source | NereusSDR Implementation |
|---------|---------------|--------------------------|
| Right-click AGC-T slider | `console.cs:46119-46122` `ptbRF_Click` | `customContextMenuRequested` on both VFO and RxApplet sliders |
| Manual slider drag | `console.cs:49129-49130` AGC knee drag | `sliderPressed` or `valueChanged` while not syncing |
| Setup checkbox | `setup.cs:21907-21910` `chkAutoAGCRX1_CheckedChanged` | Direct property binding |

**Persistence:** `AutoAgcEnabled` and `AutoAgcOffset` stored in AppSettings,
keyed per-slice (consistent with existing per-slice AGC persistence).

---

## 4. Setup → AGC/ALC Control Wiring

### Thetis Source

- `setup.cs:8996-9090` — AGC value-changed handlers
- `setup.cs:5046-5076` — `CustomRXAGCEnabled` enable/disable for Decay/Hang
- `setup.cs:21907-21944` — Auto AGC checkbox + offset spinner handlers
- `setup.designer.cs:38586-39455` — control layout, ranges, defaults

### Controls to Wire

**RX1 AGC group** (existing controls in `AgcAlcSetupPage`, currently disabled):

| Control | Range | Default | Thetis Handler | WDSP Call | SliceModel Property |
|---------|-------|---------|----------------|-----------|---------------------|
| Mode combo | Off/Long/Slow/Med/Fast/Custom | Med | `setup.cs:8996` | `SetRXAAGCMode` | `agcMode` |
| Attack spinner | 1–1000 ms | 2 | `setup.cs:9054` | `SetRXAAGCAttack` | `agcAttack` |
| Decay spinner | 1–5000 ms | 250 | `setup.cs:9040` | `SetRXAAGCDecay` | `agcDecay` (disabled unless Custom) |
| Hang spinner | 10–5000 ms | 250 | `setup.cs:9068` | `SetRXAAGCHang` | `agcHang` (disabled unless Custom) |
| Slope slider | 0–20 (×10 to WDSP) | 0 | `setup.cs:9054` | `SetRXAAGCSlope` | `agcSlope` |
| Max Gain spinner | -20–120 dB | 90 | `setup.cs:9011` | `SetRXAAGCTop` | (RF Gain readback) |
| Fixed Gain spinner | -20–120 dB | 20 | `setup.cs:9001` | `SetRXAAGCTop` (FIXD) | (new: `agcFixedGain`) |
| Hang Threshold slider | 0–100 | 0 | `setup.cs:9081` | `SetRXAAGCHangThreshold` | (new: `agcHangThreshold`) |

**New SliceModel properties needed:**

| Property | Type | Default | Thetis Origin |
|----------|------|---------|---------------|
| `agcFixedGain` | `int` | `20` | `setup.cs:9001` `udDSPAGCFixedGaindB` |
| `agcHangThreshold` | `int` | `0` | `setup.cs:9081` `tbDSPAGCHangThreshold` |
| `agcMaxGain` | `int` | `90` | `setup.cs:9011` `udDSPAGCMaxGaindB` |

**New RxChannel methods needed:**

| Method | WDSP Call | Thetis Source |
|--------|-----------|---------------|
| `setAgcHangThreshold(int)` | `SetRXAAGCHangThreshold(ch, val)` | `dsp.cs` P/Invoke |
| `setAgcFixedGain(double)` | `SetRXAAGCTop(ch, val)` (FIXD mode) | `console.cs:45994-46007` |

**New controls to add to `AgcAlcSetupPage`:**

| Control | Type | Range | Default | Thetis Source |
|---------|------|-------|---------|---------------|
| Auto AGC RX1 checkbox | QCheckBox | — | unchecked | `setup.designer.cs:38586` `chkAutoAGCRX1` |
| Auto AGC Offset spinner | QSpinBox | -60–+60 dB | 20 | `setup.designer.cs:38630` `udRX1AutoAGCOffset` |
| Fixed Gain spinner | QSpinBox | -20–120 dB | 20 | `setup.designer.cs:39320` `udDSPAGCFixedGaindB` |

**Custom AGC mode gating** (from Thetis `setup.cs:5046-5076`):
- Decay and Hang spinners are disabled unless AGC mode is Custom.
- When Custom is selected, enable them and fire their value-changed handlers.

**Slope ×10 multiplier** (from Thetis `setup.cs:9054`):
- UI shows 0–20, WDSP receives 0–200.
- `// From Thetis setup.cs:9054 — RXAGCSlope = 10 * (int)(udDSPAGCSlope.Value)`

**Existing controls with range corrections:**
- Slope slider: currently 0–10, should be 0–20 (from Thetis
  `setup.designer.cs:39380`).
- Hang spinner: currently 0–5000, should be 10–5000 (from Thetis
  `setup.designer.cs:39290` minimum 10).

**ALC and Leveler groups** remain disabled — these are TX-side controls, deferred
until Phase 3M (TX processing).

---

## 5. AGC-T Slider Visual Enhancement

### Design (validated via mockup)

Both VFO Audio tab and RxApplet AGC-T sliders get identical treatment:

**Auto ON state:**
- Slider handle color: yellow-green `#adff2f` (from cyan `#00b4d8`)
- Handle glow: `box-shadow: 0 0 4px #adff2f80`
- Groove: dark green NF fill zone from left edge to noise floor position
  (`linear-gradient(90deg, #0a1a0a, #1a3a1a)`)
- "NF" label at the noise floor position on the groove (color `#33aa33`)
- Row border: `1px solid #adff2f40` with background `#0a100a`
- Row label "AGC-T": color `#adff2f`, bold
- AUTO badge: right of dB value, `background:#1a2a1a; border:1px solid #adff2f;
  color:#adff2f; font-size:7px`
- Info sub-line below slider: `"NF -62 dB · offset +20 · right-click to disable"`
  in `#33aa33` at 7px

**Manual state (auto off):**
- Slider handle color: cyan `#00b4d8` (existing)
- Groove: plain `#1a2a3a` (existing)
- Row border: `1px solid #1a2a3a` (existing)
- Row label "AGC-T": color `#8899aa` (existing)
- No AUTO badge, no NF fill, no info sub-line

**NF position mapping:**
- The slider range is -160 to 0 dB.
- NF fill width = `(noiseFloor - (-160)) / 160.0` as fraction of groove width.
- Handle position tracks the auto-calculated threshold (NF + offset).

### Right-click toggle

Both sliders respond to `customContextMenuRequested`:
- If auto is off: enable auto, set `SliceModel::setAutoAgcEnabled(true)`
- If auto is on: disable auto, set `SliceModel::setAutoAgcEnabled(false)`
- No context menu popup — instant toggle (matching Thetis `ptbRF_Click` which
  is a direct toggle, not a menu).

### Implementation

The visual enhancement is pure Qt stylesheet + paint logic on the existing
`QSlider` widgets. No new widget class needed — the AGC-T slider row in both
`VfoWidget::buildAudioTab()` and `RxApplet` gets a wrapper method that updates
styling based on `SliceModel::autoAgcEnabled`.

---

## 6. Thetis Source Attribution and Licensing

### License

Thetis is licensed under the **GNU General Public License v2.0** (GPL-2.0).
NereusSDR's port of Thetis logic constitutes a derivative work under the GPL.

- **Thetis version:** v2.10.3.13 (Assembly `2.10.3.13`)
- **Thetis commit:** `501e3f5` (ramdor/Thetis, archived)
- **License:** GPL-2.0-or-later

### Byte-for-Byte Source File Headers

Each Thetis source file has its own distinct header with different copyright
holders and modification credits. When porting logic from a file, the NereusSDR
file's header must reproduce the **exact** header from each Thetis source file
it ports from. If a NereusSDR file ports from multiple Thetis files, all
relevant headers must be included.

**console.cs header:**
```
//=================================================================
// console.cs
//=================================================================
// Thetis is a C# implementation of a Software Defined Radio.
// Copyright (C) 2004-2009  FlexRadio Systems 
// Copyright (C) 2010-2020  Doug Wigley
// Credit is given to Sizenko Alexander of Style-7 (http://www.styleseven.com/) for the Digital-7 font.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//
// You may contact us via email at: sales@flex-radio.com.
// Paper mail may be sent to: 
//    FlexRadio Systems
//    8900 Marybank Dr.
//    Austin, TX 78750
//    USA
//
//=================================================================
// Modifications to support the Behringer Midi controllers
// by Chris Codella, W2PA, May 2017.  Indicated by //-W2PA comment lines. 
// Modifications for using the new database import function.  W2PA, 29 May 2017
// Support QSK, possible with Protocol-2 firmware v1.7 (Orion-MkI and Orion-MkII), and later.  W2PA, 5 April 2019 
// Modfied heavily - Copyright (C) 2019-2026 Richard Samphire (MW0LGE)
//
//============================================================================================//
// Dual-Licensing Statement (Applies Only to Author's Contributions, Richard Samphire MW0LGE) //
// ------------------------------------------------------------------------------------------ //
// For any code originally written by Richard Samphire MW0LGE, or for any modifications       //
// made by him, the copyright holder for those portions (Richard Samphire) reserves the       //
// right to use, license, and distribute such code under different terms, including           //
// closed-source and proprietary licences, in addition to the GNU General Public License      //
// granted above. Nothing in this statement restricts any rights granted to recipients under  //
// the GNU GPL. Code contributed by others (not Richard Samphire) remains licensed under      //
// its original terms and is not affected by this dual-licensing statement in any way.        //
// Richard Samphire can be reached by email at :  mw0lge@grange-lane.co.uk                    //
//============================================================================================//
```

**display.cs header:**
```
//=================================================================
// display.cs
//=================================================================
// Thetis is a C# implementation of a Software Defined Radio.
// Copyright (C) 2004-2009  FlexRadio Systems
// Copyright (C) 2010-2020  Doug Wigley (W5WC)
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//
// You may contact us via email at: sales@flex-radio.com.
// Paper mail may be sent to: 
//    FlexRadio Systems
//    8900 Marybank Dr.
//    Austin, TX 78750
//    USA
//
//=================================================================
// Waterfall AGC Modifications Copyright (C) 2013 Phil Harman (VK6APH)
// Transitions to directX and continual modifications Copyright (C) 2020-2025 Richard Samphire (MW0LGE)
//=================================================================
//
//============================================================================================//
// Dual-Licensing Statement (Applies Only to Author's Contributions, Richard Samphire MW0LGE) //
// ------------------------------------------------------------------------------------------ //
// For any code originally written by Richard Samphire MW0LGE, or for any modifications       //
// made by him, the copyright holder for those portions (Richard Samphire) reserves the       //
// right to use, license, and distribute such code under different terms, including           //
// closed-source and proprietary licences, in addition to the GNU General Public License      //
// granted above. Nothing in this statement restricts any rights granted to recipients under  //
// the GNU GPL. Code contributed by others (not Richard Samphire) remains licensed under      //
// its original terms and is not affected by this dual-licensing statement in any way.        //
// Richard Samphire can be reached by email at :  mw0lge@grange-lane.co.uk                    //
//============================================================================================//
```

**setup.cs header:**
```
//=================================================================
// setup.cs
//=================================================================
// Thetis is a C# implementation of a Software Defined Radio.
// Copyright (C) 2004-2009  FlexRadio Systems
// Copyright (C) 2010-2020  Doug Wigley
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//
// You may contact us via email at: sales@flex-radio.com.
// Paper mail may be sent to: 
//    FlexRadio Systems
//    8900 Marybank Dr.
//    Austin, TX 78750
//    USA
//
//=================================================================
// Continual modifications Copyright (C) 2019-2026 Richard Samphire (MW0LGE)
//=================================================================
//
//============================================================================================//
// Dual-Licensing Statement (Applies Only to Author's Contributions, Richard Samphire MW0LGE) //
// ------------------------------------------------------------------------------------------ //
// For any code originally written by Richard Samphire MW0LGE, or for any modifications       //
// made by him, the copyright holder for those portions (Richard Samphire) reserves the       //
// right to use, license, and distribute such code under different terms, including           //
// closed-source and proprietary licences, in addition to the GNU General Public License      //
// granted above. Nothing in this statement restricts any rights granted to recipients under  //
// the GNU GPL. Code contributed by others (not Richard Samphire) remains licensed under      //
// its original terms and is not affected by this dual-licensing statement in any way.        //
// Richard Samphire can be reached by email at :  mw0lge@grange-lane.co.uk                    //
//============================================================================================//
```

### NereusSDR File Header Format

Each new NereusSDR file that ports Thetis logic includes:

1. NereusSDR's own copyright line
2. "Ported from" line with Thetis version and commit
3. The **exact** header block(s) from each Thetis source file ported from,
   converted from `//` to C++ `//` (already matching)

Example for a file porting from both `console.cs` and `display.cs`:

```cpp
// NereusSDR — Cross-platform OpenHPSDR client
// Copyright (C) 2026 NereusSDR contributors
//
// Portions ported from Thetis v2.10.3.13 (commit 501e3f5)
// Original Thetis source headers follow:
//
// --- From console.cs ---
// [exact console.cs header block]
//
// --- From display.cs ---
// [exact display.cs header block]
```

### Inline Comment Preservation

All inline comments from the Thetis source code that appear within ported logic
**must be preserved byte-for-byte** in the C++ translation. This includes:

- `// MW0LGE` tags and explanatory notes
- `// TODO` and `// FIXME` annotations
- Developer attribution comments (e.g., `// -W2PA`)
- Behavioral notes (e.g., `// MW0LGE_21k5 change to rx2`)
- Any `//` comment on or above a ported line of logic

When the C++ translation changes the structure enough that the comment no longer
sits on the same line, place it on the nearest equivalent line with a note:

```cpp
// MW0LGE_21k5 change to rx2  [original inline comment from display.cs:10079]
```

### Per-Line Attribution

Every ported function, constant, range, default, and behavioral rule includes a
`// From Thetis v2.10.3.13 [file]:[line]` comment:

```cpp
// From Thetis v2.10.3.13 console.cs:45977 — agc_thresh_point default
static constexpr int kDefaultAgcThreshold = -20;
```

### Key Attributions

| NereusSDR Code | Thetis Source |
|----------------|---------------|
| NoiseFloorTracker lerp | `display.cs:4635-4636` |
| NoiseFloorTracker fast-attack | `display.cs:917-926` |
| NoiseFloorTracker attack time 2000ms | `display.cs:4638` |
| Auto AGC timer 500ms | `console.cs:46057` `tmrAutoAGC` |
| Auto AGC offset default +20 | `setup.designer.cs:38630` `udRX1AutoAGCOffset.Value` |
| Auto AGC offset range ±60 | `setup.designer.cs:38630` min/max |
| setAGCThresholdPoint clamp [-160, +2] | `console.cs:45969-45970` |
| agcCalOffset formula | `console.cs:33292-33319` |
| Slope ×10 multiplier | `setup.cs:9054` |
| Custom-mode decay/hang gating | `setup.cs:5046-5076` |
| Right-click auto toggle | `console.cs:46119-46122` `ptbRF_Click` |
| Manual drag disables auto | `console.cs:49129-49130` |
| AGC-T range -160 to 0 dB | `console.cs:45969-45970` |
| Fixed Gain default 20 dB | `setup.designer.cs:39320` |
| Max Gain default 90 dB | `setup.designer.cs:39245` |
| Hang Threshold range 0-100 | `setup.designer.cs:39418` |
| Hang spinner min 10 ms | `setup.designer.cs:39290` |

---

## 7. Persistence

| Setting Key | Type | Default | Scope |
|-------------|------|---------|-------|
| `AgcAutoEnabled` | bool | false | per-slice |
| `AgcAutoOffset` | double | 20.0 | per-slice |
| `AgcFixedGain` | int | 20 | per-slice |
| `AgcHangThreshold` | int | 0 | per-slice |
| `AgcMaxGain` | int | 90 | per-slice |

All via `AppSettings` (never `QSettings`), PascalCase keys, bool as
`"True"`/`"False"` strings.

---

## 8. Out of Scope

- **AGC knee line on spectrum display** — separate spectrum overlay work, not
  this feature. The slider groove serves as the knee line for now.
- **RX2 auto AGC** — deferred until multi-receiver (Phase 3F). Properties are
  per-slice so RX2 gets it for free when receivers land.
- **ALC / Leveler wiring** — TX-side, deferred to Phase 3M.
- **AGC-T display cal offset** — simplified for now (no Alex preamp offset).
  Full cal offset lands with the spectrum knee line overlay.
