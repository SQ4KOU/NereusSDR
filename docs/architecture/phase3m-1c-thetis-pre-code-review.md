# Phase 3M-1c — Polish & Persistence: Thetis Pre-Code Review

**Status:** ready for plan-writing.
**Date:** 2026-04-28.
**Branch:** `feature/phase3m-1c-polish-persistence`.
**Base:** `origin/main` @ `bfce1cf` (post-3M-1b merge).
**Design spec:** [`phase3m-1c-polish-persistence-design.md`](phase3m-1c-polish-persistence-design.md).
**Master design:** [`phase3m-tx-epic-master-design.md`](phase3m-tx-epic-master-design.md) §5.3.
**HL2 desk-review:** [`phase3m-1c-hl2-tx-path-review.md`](phase3m-1c-hl2-tx-path-review.md).

This review follows the 3M-1b precedent
([`phase3m-1b-thetis-pre-code-review.md`](phase3m-1b-thetis-pre-code-review.md))
— per chunk: Thetis source quotes + behaviour analysis + locked decisions
+ test surface + risk. Output of this doc feeds into
`phase3m-1c-polish-persistence-plan.md` (the TDD task list) which lands
next.

**Cite-stamp grammar for 3M-1c:**

- `[v2.10.3.13]` for Thetis tag-aligned values (verified against
  `git -C ../Thetis describe --tags` → `v2.10.3.13-7-g501e3f51`; the
  seven post-tag commits do not touch any chunk-1-9 files).
- `[@501e3f51]` for Thetis post-tag commits (none expected for 3M-1c
  given the spot-check above).
- `[@120188f]` for deskhpsdr (secondary; chunk 7 candidate).
- `[v2.10.3.13-beta2]` or `[@c26a8a4]` for mi0bot-Thetis (tertiary;
  chunk 7 cross-check; chunk 0 fixes already absorbed at `cf93ab6` /
  `69c4054`).

---

## 0. Brainstorm-locked decisions (input from design spec §2)

| # | Decision | Choice |
|---|---|---|
| Q1 | 3M-1c scope | **10 chunks** (master-design §5.3 minus TX recording, plus 3 carry-forwards from 3M-1b, plus chunk 0 HL2 desk-review) |
| Q2 | Mic profile schema scope | Live-fields-only with Thetis column names (~20 columns); 1 default factory profile; SSB-only mode-family pointer |
| Q3 | AppSettings rename strategy | Hard cutover — no migration (3M-1b 16-key naming hasn't shipped to a release yet) |
| Q4 | Two-tone test scope | Full Thetis port — continuous + pulsed, all 6 user-tunable params, Setup → Test → Two-Tone page, mode-aware tone inversion, Freq2 delay, power-source enum, TUN auto-stop, TxApplet 2-TONE button |
| Q5 | TX recording scope | Defer to 3M-6 |
| Q6 | HL2 TX path desk-review | Chunk 0 (precursor) — **completed** 2026-04-28; absorbed 2 ship-blocking fixes (HL2 PA scaling + HL2 TX step att inversion) |

---

## 0.5 Open items surfaced during pre-code review

These are **questions the implementation plan should call out** but do
not block plan-writing.

1. **Chunk 1 — VFO Flag TX badge attribution.** The VFO Flag widget is
   an **AetherSDR pattern**; the `setTransmitting(bool)` MOX-state
   colour-swap behaviour is **NereusSDR-native** (already implemented in
   3G-8). Per memory `feedback_source_first_ui_vs_dsp`, source-first
   governs DSP/radio only — Qt widgets are NereusSDR-native. Chunk 1 is
   pure UI wire-up; no Thetis cite required.
2. **Chunk 4 — Rename button.** Thetis has **no explicit Rename button**
   (Area B research). Decision: NereusSDR matches Thetis — Save (with
   new name) + Delete (old) covers the rename use case. The plan should
   list "Rename" as deferred-by-design.
3. **Chunk 6 — `int rx` argument semantics.** The argument is **the
   receiver index that owns the TX path** (`2` if RX2 enabled AND
   VFOBTX, else `1`). Plan should test this exact semantic; not
   "active receiver index" generically.
4. **Chunk 7 — accumulator block size.** Thetis uses 720 samples @ 48 kHz
   = 15 ms (cmaster.cs:495). NereusSDR's TxChannel accumulator can match
   Thetis exactly (720) or stay at NereusSDR's existing 256 (5.3 ms,
   matches PC mic block delivery). **Locking 720 samples** for source-
   first parity; plan should verify this doesn't introduce excess TX
   latency on PC mic (5.3 → 15 ms).

   **CORRECTION (2026-04-29 amendment).** This decision was based on a
   misread of `cmInboundSize[5]=720`.  The 720-sample value is the
   **network arrival block size** for stream 5 (mic input from the
   radio's ADC, multiplexed onto the Ethernet stream from Thetis's
   perspective — NereusSDR doesn't actually use this stream because
   our TX mic is local).  The DSP block size in Thetis is
   `xcm_insize == r1_outsize == in_size == getbuffsize(48000) == 64`
   per `cmaster.c:460-487` and `cmsetup.c:106-110` [v2.10.3.13] —
   uniform end-to-end with no re-blocking.  The cmaster ring absorbs
   720-sample network deliveries into 64-sample DSP slices via
   `cm_main`'s semaphore-driven worker loop (cmbuffs.c:151-168).

   The 720-sample lock here cascaded into the D.1 + E.1 + L.4 +
   bench-fix-A + bench-fix-B chain that produced the gravelly SSB
   voice TX bench regression on 2026-04-29.  Architectural review
   replaced the chain with a Thetis-style worker-thread + matching
   block size (NereusSDR uses 256 due to WDSP r2-ring divisibility,
   which is Thetis-equivalent up to the constant).  See
   `docs/architecture/phase3m-1c-tx-pump-architecture-plan.md` for
   the full redesign rationale.
5. **Chunk 6 — observer count.** 12 distinct Thetis subscribers
   identified (Area C research). NereusSDR will likely have fewer in
   3M-1c (no PS form yet, no TCI server yet, no MeterManager Pre/Post
   subscription), but the framework signal should be wired identically
   so future subscribers (3J / 3M-4 / 3M-7) plug in cleanly.

---

## 1. Chunk 1 — VFO Flag TX badge wire-up

### 1.1 Goal

Wire `VfoDisplayItem::setTransmitting(bool)` (already implemented in 3G-8)
to `MoxController` MOX state changes. Provide visual feedback during TX.

### 1.2 Source attribution — AetherSDR pattern + NereusSDR-native widget

**Per memory `feedback_source_first_ui_vs_dsp`:** source-first governs
DSP/radio only; Qt widgets are NereusSDR-native. Chunk 1 is therefore a
**NereusSDR UI wire-up** with an **AetherSDR pattern reference**, not a
Thetis port. No Thetis cite is required for chunk 1 because Thetis's
non-Qt UI implementation (button background colours, label foregrounds)
doesn't translate to the AetherSDR-style floating VFO Flag widget that
NereusSDR uses.

**AetherSDR pattern reference** — `AetherSDR/src/gui/VfoWidget.{h,cpp}`:
floating flag widget anchored to the VFO marker, flips right when
clipped (`VfoWidget.h:30-32`). AetherSDR exposes a `TX` push-button
badge (`m_txBadge` at `VfoWidget.cpp:359-366`) that toggles which slice
is the TX target via `m_slice->setTxSlice(!m_slice->isTxSlice())` —
this is **slice assignment**, not MOX-state visualisation.

**NereusSDR `VfoDisplayItem`** (already implemented in 3G-8) has both:
- The AetherSDR-style flag widget for slice assignment
- A NereusSDR-native MOX-state colour swap via:
  - `setTransmitting(bool tx)` setter (`VfoDisplayItem.h:83`)
  - `m_transmitting` member (`VfoDisplayItem.h:122`)
  - `m_txColour{0xff, 0x00, 0x00}` red default (`VfoDisplayItem.h:131`)
  - `setTxColour(const QColor& c)` for theme override (`VfoDisplayItem.h:96`)
  - Render-time swap: `const QColor stateColour = m_transmitting ?
    m_txColour : m_rxColour` (`VfoDisplayItem.cpp:96`)

The `setTransmitting()` API is NereusSDR-native — AetherSDR's `m_txBadge`
is for slice assignment only; AetherSDR has no comparable
"currently keying" indicator on the VFO flag itself. NereusSDR added the
MOX-state colour-swap behaviour during 3G-8 as a UX enhancement.

### 1.3 Locked decisions

1. **Reuse existing 3G-8 widget machinery.** `m_transmitting` flag,
   `m_txColour` (red default), `setTransmitting(bool)` API — all
   already implemented. Chunk 1 just wires the slot.
2. **Active VFO determined by `int rx` argument** of MoxController's
   3-arg `moxChanged(int rx, bool oldMox, bool newMox)` signal
   (chunk 6). When `rx == 1` (default), badge applies to VFO-A's flag.
   When `rx == 2` (RX2 enabled AND VFOBTX), badge applies to VFO-B's
   flag.
3. **No status-bar TX label.** AetherSDR doesn't have one; NereusSDR
   doesn't add one in 3M-1c. The MOX state already has visibility via
   (a) the TxApplet MOX button visual, (b) the per-flag colour swap
   from 3G-8, (c) future spectrum overlay tinting (already in 3M-1a).
4. **No per-mode TX palette.** AetherSDR ships a single TX colour;
   NereusSDR matches. Per-mode palette could be a future polish item
   but isn't part of 3M-1c scope.

### 1.4 Test surface

| Test | Scope |
|---|---|
| `tst_vfo_display_item_tx_badge.cpp` | `setTransmitting(true)` → render uses `m_txColour`; `setTransmitting(false)` → render uses `m_rxColour` |
| `tst_vfo_display_item_active_vfo_routing.cpp` | When `MoxController` emits `moxChanged(int=1, ...)`, only VFO-A's `VfoDisplayItem` receives `setTransmitting(true)`; `int=2` routes to VFO-B |

### 1.5 Risk

**Low.** UI wire-up only — no DSP, no protocol, no Thetis behaviour
translation. The widget machinery already exists from 3G-8; this chunk
just connects its slot to MoxController's signal with the `int rx`
routing logic.

---

## 2. Chunk 2 — Two-tone test full port

### 2.1 Goal

Full Thetis port of the two-tone IMD test (continuous + pulsed modes,
all 6 user-tunable params, Setup → Test → Two-Tone page, TxApplet
2-TONE button).

### 2.2 Thetis source — handler overview

**`console.cs:44728-44760` [v2.10.3.13]** — `chk2TONE_CheckedChanged`
(TxApplet button handler):

```csharp
private async void chk2TONE_CheckedChanged(object sender, EventArgs e)
{
    if (IsSetupFormNull || SetupForm.TestIMD == chk2TONE.Checked) return;
    // stop tune if currently running and we want to run 2tone
    if (chk2TONE.Checked && chkTUN.Checked) {
        chkTUN.CheckedChanged -= ...;
        chkTUN.Checked = false;
        chkTUN_CheckedChanged(this, EventArgs.Empty);
        chkTUN.CheckedChanged += ...;
        await Task.Delay(300);
    }
    SetupForm.TestIMD = chk2TONE.Checked;  // start/stop the test
    if (SetupForm.TestIMD && chk2TONE.Checked)
        chk2TONE.BackColor = button_selected_color;
    else if (!SetupForm.TestIMD && chk2TONE.Checked) {
        chk2TONE.BackColor = SystemColors.Control;
        chk2TONE.Checked = false;  // start failed
    } else
        chk2TONE.BackColor = SystemColors.Control;
    setupTuneDriveSlider();
}
```

Key behaviours:
1. **TUN auto-stop** with 300 ms delay (line 44740) — let the radio
   settle before starting two-tone
2. **Visual feedback** via background colour change
3. **Failed-start unwind** (line 44749-44752) — if `SetupForm.TestIMD =
   true` rejects, the button auto-unchecks

**`setup.cs:11019-11200ish` [v2.10.3.13]** — `chkTestIMD_CheckedChanged`
(actual implementation on Setup form):

```csharp
public bool TestIMD {
    set { chkTestIMD.Checked = value; }
    get { return chkTestIMD.Checked; }
}

private async void chkTestIMD_CheckedChanged(...) {
    if (chkTestIMD.Checked) {
        // disable Freq1/Freq2/Level/Power/Invert/Freq2Delay/defaults/stealth
        double ttfreq1 = (double)udTestIMDFreq1.Value;
        double ttfreq2 = (double)udTestIMDFreq2.Value;
        double ttmag = (double)udTwoToneLevel.Value;
        double ttmag1, ttmag2;
        ttmag1 = ttmag2 = 0.49999 * Math.Pow(10.0, ttmag / 20.0);
        DSPMode mode = console.radio.GetDSPTX(0).CurrentDSPMode;
        if (chkInvertTones.Checked &&
            ((mode == DSPMode.CWL) || (mode == DSPMode.DIGL) || (mode == DSPMode.LSB))) {
            ttfreq1 = -ttfreq1;
            ttfreq2 = -ttfreq2;
        }
        if (!console.PowerOn) {
            MessageBox.Show("Power must be on to run this test.");
            chkTestIMD.Checked = false;
            return;
        }
        if (console.MOX) {
            Audio.MOX = false;
            console.MOX = false;
            await Task.Delay(200);
        }
        bool pulsed = chkPulsed_TwoTone.Checked;
        if (pulsed) {
            setupTwoTonePulse();
            console.radio.GetDSPTX(0).TXPostGenMode = 7;  // pulsed two tone
            console.radio.GetDSPTX(0).TXPostGenTTPulseToneFreq1 = ttfreq1;
            console.radio.GetDSPTX(0).TXPostGenTTPulseToneFreq2 = ttfreq2;
            console.radio.GetDSPTX(0).TXPostGenTTPulseMag1 = ttmag1;
            if ((int)udFreq2Delay.Value == 0)
                console.radio.GetDSPTX(0).TXPostGenTTPulseMag2 = ttmag2;
            else
                console.radio.GetDSPTX(0).TXPostGenTTPulseMag2 = 0.0;
        } else {
            console.radio.GetDSPTX(0).TXPostGenMode = 1;  // continuous
            console.radio.GetDSPTX(0).TXPostGenTTFreq1 = ttfreq1;
            console.radio.GetDSPTX(0).TXPostGenTTFreq2 = ttfreq2;
            console.radio.GetDSPTX(0).TXPostGenTTMag1 = ttmag1;
            if ((int)udFreq2Delay.Value == 0)
                console.radio.GetDSPTX(0).TXPostGenTTMag2 = ttmag2;
            else
                console.radio.GetDSPTX(0).TXPostGenTTMag2 = 0.0;
        }
        console.radio.GetDSPTX(0).TXPostGenRun = 1;
        // remember old power and set new power per TwoToneDrivePowerOrigin
        if (console.TwoToneDrivePowerOrigin == DrivePowerSource.FIXED)
            console.PreviousPWR = console.PWR;
        int new_pwr = console.SetPowerUsingTargetDBM(...);
        if (console.TwoToneDrivePowerOrigin == DrivePowerSource.FIXED) {
            console.PWRSliderLimitEnabled = false;
            console.PWR = new_pwr;
        }
        console.ManualMox = true;
        console.TwoTone = true;
        Audio.MOX = true;
        console.MOX = true;
        if (!console.MOX) { chkTestIMD.Checked = false; return; }
        if ((int)udFreq2Delay.Value > 0) {
            await Task.Delay((int)udFreq2Delay.Value);
            if (pulsed)
                console.radio.GetDSPTX(0).TXPostGenTTPulseMag2 = ttmag2;
            else
                console.radio.GetDSPTX(0).TXPostGenTTMag2 = ttmag2;
        }
        chkTestIMD.BackColor = console.ButtonSelectedColor;
        console.psform.TTgenON = true;
        chkTestIMD.Text = "Stop";
    }
}
```

### 2.3 The 6 user-tunable params

| Thetis control | NereusSDR property | Default | Range | Source |
|---|---|---|---|---|
| `udTestIMDFreq1` | `TwoToneFreq1` | 700 Hz | -10000..10000 | setup.cs:11052 |
| `udTestIMDFreq2` | `TwoToneFreq2` | 1900 Hz | -10000..10000 | setup.cs:11053 |
| `udTwoToneLevel` | `TwoToneLevel` | -6 dBm | -60..0 | setup.cs:11054 |
| `udTestIMDPower` | `TwoTonePower` | 50 % | 0..100 | setup.cs:5267 |
| `udFreq2Delay` | `TwoToneFreq2Delay` | 0 ms | 0..3000 | setup.cs:11049 |
| `chkInvertTones` | `TwoToneInvert` | false | bool | setup.cs:11058 |
| `chkPulsed_TwoTone` | `TwoTonePulsed` | false | bool | setup.cs:11079 |

(7 controls counting `chkPulsed_TwoTone` separately — design spec said 6
"user-tunable params" because it bundled pulsed mode with the 6.)

### 2.4 WDSP setters used

```
WDSP.SetTXAPostGenMode(channel, mode)        // 0 = off, 1 = continuous, 7 = pulsed
WDSP.SetTXAPostGenTTFreq1(channel, freq)     // continuous mode
WDSP.SetTXAPostGenTTFreq2(channel, freq)
WDSP.SetTXAPostGenTTMag1(channel, mag)
WDSP.SetTXAPostGenTTMag2(channel, mag)
WDSP.SetTXAPostGenTTPulseToneFreq1(channel, freq)  // pulsed mode
WDSP.SetTXAPostGenTTPulseToneFreq2(channel, freq)
WDSP.SetTXAPostGenTTPulseMag1(channel, mag)
WDSP.SetTXAPostGenTTPulseMag2(channel, mag)
WDSP.SetTXAPostGenTTPulsePeriod(channel, period)   // setupTwoTonePulse()
WDSP.SetTXAPostGenTTPulseDuration(channel, dur)
WDSP.SetTXAPostGenTTPulseTransition(channel, trans)
WDSP.SetTXAPostGenRun(channel, on)            // 1 = start, 0 = stop
```

### 2.5 Mode-aware tone inversion

Lines 11058-11062 in setup.cs:

```csharp
if (chkInvertTones.Checked &&
    ((mode == DSPMode.CWL) || (mode == DSPMode.DIGL) || (mode == DSPMode.LSB))) {
    ttfreq1 = -ttfreq1;
    ttfreq2 = -ttfreq2;
}
```

The invert toggle is a **conditional** — it only affects LSB-family
modes (LSB, CWL, DIGL). USB-family modes ignore the toggle. This is the
correct behaviour because LSB is mathematically the conjugate of USB,
so the IMD test tones need to be sign-inverted to land at the same
spectral position.

### 2.6 Magnitude scaling

```csharp
ttmag1 = ttmag2 = 0.49999 * Math.Pow(10.0, ttmag / 20.0);
```

The `0.49999` is the **Thetis literal** for "just under unity" to leave
headroom. Two equal-amplitude tones at this level with sign-flip in the
IMD output produce 3rd-order products at -10·log10(2) below carrier
(~3 dB) per tone — standard IMD test signal. Preserve the literal
exactly per `feedback_source_first_exceptions`.

### 2.7 Setup → Test → Two-Tone page UI

Setup page should mirror the Thetis tab layout:

- Group: "Tone Frequencies" — Freq1, Freq2 spinboxes + Defaults / Stealth
  preset buttons
- Group: "Output Level" — Level slider + Power slider
- Group: "Mode" — Pulsed checkbox, Invert tones checkbox, Freq2 delay
- TwoToneDrivePowerOrigin: FIXED vs SLIDER radio buttons (uses
  `udTestIMDPower` vs current console PWR slider)

**`setupTwoTonePulse()`** (referenced at setup.cs:11083) sets the pulse
profile parameters: period, duration, transition. These are not
user-tunable in Thetis — they're hardcoded — so the NereusSDR Setup page
doesn't need exposing them, just call them inline when entering pulsed
mode.

### 2.8 Locked decisions

1. **Full continuous + pulsed port**, all 6+1 user-tunable params, Setup
   page, mode-aware invert.
2. **`0.49999` magnitude scaling literal** preserved exactly with cite.
3. **TwoToneDrivePowerOrigin enum** ports as `DrivePowerSource::Fixed`
   and `::Slider`.
4. **TUN auto-stop** with `Qt::DirectConnection` to TUN signal +
   `QTimer::singleShot(300, ...)` for the settle delay.
5. **`setupTwoTonePulse()` parameters** are hardcoded — no Setup page
   exposure (matches Thetis).
6. **Defaults / Stealth preset buttons** ship in 3M-1c. Stealth mode is
   useful for compliance; Defaults restores factory values.

### 2.9 Test surface

| Test | Scope |
|---|---|
| `tst_two_tone_continuous.cpp` | `TXPostGenMode=1` + `TXPostGenRun=1` wire-up; verify all four freq/mag setters fire |
| `tst_two_tone_pulsed.cpp` | `TXPostGenMode=7` + pulse profile setters; setupTwoTonePulse() called |
| `tst_two_tone_invert_modes.cpp` | Invert respected for LSB/CWL/DIGL; ignored for USB/CWU/DIGU/AM/FM/SAM |
| `tst_two_tone_freq2_delay.cpp` | Freq2 magnitude delayed when delay > 0; immediate when delay = 0 |
| `tst_two_tone_magnitude_scaling.cpp` | `0.49999 * pow(10, level/20)` formula matches Thetis literal byte-for-byte |
| `tst_two_tone_tun_auto_stop.cpp` | Activating 2-TONE while TUN is on stops TUN first (QTimer 300ms delay) |
| `tst_two_tone_band_plan_guard.cpp` | 2-TONE rejected in CW modes (CWL/CWU) per BandPlanGuard SSB-mode allow-list (3M-1b K.1) |
| `tst_two_tone_drive_power_origin.cpp` | FIXED uses udTestIMDPower; SLIDER uses console PWR slider |

### 2.10 Risk

**Medium.** Pulsed mode parameter ranges need bench verification with a
spectrum analyser. The `setupTwoTonePulse()` magnitude profile is
non-trivial — consider cross-checking deskhpsdr secondary if the Thetis
WDSP setters for pulse period/duration/transition are unclear. TUN
auto-stop must respect the existing 3M-0 SwrProtectionController state.

---

## 3. Chunk 3 — Mic profile schema (live fields, Thetis names)

### 3.1 Goal

Per-MAC AppSettings profile schema using Thetis column names. Live
fields only (~20 columns); 1 default factory profile; SSB-only mode-
family pointer.

### 3.2 Thetis source — TXProfile DataTable schema

**`database.cs:4299-4540` [v2.10.3.13]** — `AddTXProfileTable`. The full
schema is **206 columns** across two tables (`TXProfile` user-editable +
`TXProfileDef` factory reference). Excerpt of the live-field subset
(only columns NereusSDR will persist in 3M-1c):

```csharp
t.Columns.Add("Name", typeof(string));
t.Columns.Add("FilterLow", typeof(int));        // -10000..10000 Hz
t.Columns.Add("FilterHigh", typeof(int));
t.Columns.Add("MicGain", typeof(int));           // -20..70 dB depending on board
t.Columns.Add("MicMute", typeof(bool));          // true = mic in use (Thetis quirk)
t.Columns.Add("Mic_Input_On", typeof(bool));
t.Columns.Add("Mic_Input_Boost", typeof(bool));
t.Columns.Add("Line_Input_On", typeof(bool));
t.Columns.Add("Line_Input_Level", typeof(decimal));
t.Columns.Add("VOX_On", typeof(bool));           // NereusSDR: not persisted
t.Columns.Add("Dexp_On", typeof(bool));          // NereusSDR: not persisted (3M-3a-iii)
t.Columns.Add("Dexp_Threshold", typeof(int));    // 3M-1b: voxThresholdDb -> Dexp_Threshold
t.Columns.Add("Dexp_Attack", typeof(int));       // 3M-3a-iii
t.Columns.Add("VOX_HangTime", typeof(int));
t.Columns.Add("Dexp_Release", typeof(int));      // 3M-3a-iii
t.Columns.Add("Power", typeof(int));
t.Columns.Add("Tune_Power", typeof(int));
t.Columns.Add("Show_TX_Filter", typeof(bool));
```

Plus 187 more columns for TX EQ (10-band), CFC, CPDR/CESSB, VAC1/VAC2,
DSP buffer/filter sizes per mode, FM AF filters, RX EQ, etc. — all
deferred to 3M-3a (-i / -ii / -iii) and 3M-3b. The live subset for
3M-1c is the ~20 columns above plus the rename mapping in chunk 5
(see §5.2).

### 3.3 Thetis factory presets

**`database.cs:4544-end-of-method` [v2.10.3.13]** — 21 factory profiles
seeded only when `bIncludeExtraProfiles=true` (used for `TXProfileDef`,
not `TXProfile`):

```
"Default", "Default DX", "Digi 1K@1500", "Digi 1K@2210", "AM",
"Conventional", "D-104", "D-104+CPDR", "D-104+EQ", "DX / Contest",
"ESSB", "HC4-5", "HC4-5+CPDR", "PR40+W2IHY", "PR40+W2IHY+CPDR",
"PR781+EQ", "PR781+EQ+CPDR", "SSB 2.8k CFC", "SSB 3.0k CFC",
"SSB 3.3k CFC", "AM 10k CFC"
```

19 of the 21 require 3M-3a backends (TX EQ, CFC, CPDR) that don't ship
in 3M-1c. Only "Default" makes sense for 3M-1c.

### 3.4 Locked decisions

1. **Single factory profile "Default"** ships in 3M-1c with the live
   field subset's default values. Other 19 factory presets land
   incrementally as their backends ship in 3M-3a-i/-ii/-iii.
2. **SSB-only mode-family pointer.** AppSettings key:
   `hardware/<mac>/tx/profile/active = "Default"` (single string, not
   per-mode-family). 3M-3b adds Digital/FM/AM pointers when those modes
   ship.
3. **Schema-less AppSettings.** Adding new columns later doesn't require
   migration — they just appear when the backend writes them. The
   "live fields" for 3M-1c are bounded by what has working backends NOW.
4. **MicProfileManager class** owns load/save/delete (and rename = save-
   new + delete-old per Area B Thetis precedent).
5. **Cite stamp** on each persisted key: a `// From Thetis
   database.cs:NNNN [v2.10.3.13]` comment alongside the column name in
   the C++ persistence code.

### 3.5 Test surface

| Test | Scope |
|---|---|
| `tst_mic_profile_manager_load_save.cpp` | Round-trip a profile; AppSettings writes match expected keys |
| `tst_mic_profile_manager_delete.cpp` | Delete removes keys; cannot delete the last profile |
| `tst_mic_profile_manager_active_pointer.cpp` | `hardware/<mac>/tx/profile/active` round-trip |
| `tst_mic_profile_manager_rename_via_save_delete.cpp` | Rename = save-new + delete-old (Thetis pattern) |
| `tst_mic_profile_manager_default_seed.cpp` | First-launch creates "Default" with documented defaults |
| `tst_mic_profile_manager_per_mac_isolation.cpp` | Two MACs have independent profile lists |

### 3.6 Risk

**Medium.** Schema design is locked but the 1-default-profile default-
value list needs to be source-cited per field. The ~20-column live
subset will be consumed by both the editor UI (chunk 4) and the
persistence layer (chunk 5) — consistency matters.

---

## 4. Chunk 4 — TxApplet completes (profile combo + 2-TONE button)

### 4.1 Goal

TxApplet UI: profile combo with save/load/delete + 2-TONE button.
Mirror Thetis `comboTXProfile` (mode-family-gated read-only selector)
and `chk2TONE` (mutually-exclusive-with-TUN button).

### 4.2 Thetis source — TxApplet equivalents (Area B research)

The Thetis profile UX is **dual-combo**: a passive selector on the
console (`comboTXProfile` at `console.cs:30594`) and an active editing
combo on Setup → TX Profile (`comboTXProfileName` at `setup.cs:9505`).
The console combo is **mode-family-gated** — its
`SelectedIndexChanged` early-returns if `_rx1_dsp_mode` is
DIGL/DIGU/FM/AM/SAM, deferring those to dedicated combos
(`comboDigTXProfile`, `comboFMTXProfile`, `comboAMTXProfile`).

### 4.3 Save flow

**`setup.cs:9545-9612` [v2.10.3.13]** — `btnTXProfileSave_Click`:

```csharp
private void btnTXProfileSave_Click(object sender, EventArgs e) {
    string name = InputBox.Show("Save Profile", "Please enter a profile name:", _current_profile);
    if (name == "") return;
    name = name.Replace(",", "_");  // strip commas (TCI uses comma-delimited frame)
    DataRow[] rows = getDataRowsForTXProfile(name);
    if (rows.Length > 0) {
        DialogResult r = MessageBox.Show(
            $"Profile '{name}' already exists. Overwrite?",
            "Overwrite Profile", MessageBoxButtons.YesNo);
        if (r != DialogResult.Yes) return;
        // overwrite existing row
    } else {
        DataRow dr = DB.ds.Tables["TXProfile"].NewRow();
        // populate columns
        DB.ds.Tables["TXProfile"].Rows.Add(dr);
    }
    updateTXProfileInDB(dr);
    console.TXProfilesChangedHandlers?.Invoke();
    console.UpdateTXProfile(name);
}
```

### 4.4 Delete flow

**`setup.cs:9615-9656` [v2.10.3.13]** — `btnTXProfileDelete_Click`:

```csharp
private void btnTXProfileDelete_Click(object sender, EventArgs e) {
    if (comboTXProfileName.Items.Count == 1) {
        MessageBox.Show("It is not possible to delete the last remaining TX profile",
            "Delete Profile", MessageBoxButtons.OK);
        return;
    }
    DialogResult r = MessageBox.Show(
        $"Are you sure you want to delete the {comboTXProfileName.Text} TX Profile?",
        "Delete Profile", MessageBoxButtons.YesNo, MessageBoxIcon.Warning);
    if (r != DialogResult.Yes) return;
    DataRow[] rows = getDataRowsForTXProfile(comboTXProfileName.Text);
    if (rows.Length > 0) rows[0].Delete();
    int idx = comboTXProfileName.SelectedIndex;
    comboTXProfileName.Items.RemoveAt(idx);
    console.TXProfilesChangedHandlers?.Invoke();
    if (idx >= comboTXProfileName.Items.Count)
        idx = comboTXProfileName.Items.Count - 1;
    comboTXProfileName.SelectedIndex = idx;
    console.UpdateTXProfile(comboTXProfileName.Text);
}
```

### 4.5 Select flow

**`setup.cs:9505-9543` [v2.10.3.13]** — `comboTXProfileName_SelectedIndexChanged`:

```csharp
private void comboTXProfileName_SelectedIndexChanged(object sender, EventArgs e) {
    if (_timerCheckingTXProfile) return;  // bypass programmatic changes
    string newProfile = comboTXProfileName.Text;
    if (newProfile == _current_profile) return;
    if (chkAutoSaveTXProfile.Checked) {
        btnTXProfileSave_Click(this, EventArgs.Empty);  // auto-save before switch
    } else if (comboTXProfileName.Focused) {
        // unsaved-changes prompt only if user-initiated (focus-gated)
        DialogResult r = MessageBox.Show(
            "The current profile has changed. Would you like to save the current profile?",
            "Save Profile", MessageBoxButtons.YesNoCancel);
        if (r == DialogResult.Cancel) {
            comboTXProfileName.Text = _current_profile;
            return;
        }
        if (r == DialogResult.Yes) btnTXProfileSave_Click(this, EventArgs.Empty);
    }
    loadTXProfile(newProfile);
    _current_profile = newProfile;
}
```

### 4.6 chk2TONE button (TxApplet)

**`console.cs:44728-44760` [v2.10.3.13]** — already covered in §2.2.
Behaviour: TUN auto-stop, mutually exclusive with TUN, button-state
visual feedback.

### 4.7 Locked decisions

1. **NO Rename button.** Thetis has none; rename = Save-new + Delete-old.
   Document in TxApplet tooltip / Setup page help text.
2. **TxApplet combo is read-only selector** — clicking it shows the
   list, double-click or selection event triggers `setActiveProfile`.
   All editing (save/delete) lives on Setup → TX Profile page.
3. **Setup → TX Profile page has Save and Delete buttons** + the
   `comboTXProfileName` editing combo.
4. **Last-profile-guard on Delete** — refuse to delete the last profile
   ("It is not possible to delete the last remaining TX profile" string
   preserved verbatim).
5. **Comma-strip on save** — `name = name.replace(",", "_")` even though
   NereusSDR doesn't use TCI yet (3J), source-first parity. Document as
   forward-compat for 3J.
6. **Right-click on TxApplet combo opens Setup → TX Profile page**
   (matches `console.cs:44519-44522 comboTXProfile_MouseDown`).
7. **Auto-save toggle** — defer `chkAutoSaveTXProfile` to a future
   polish pass. 3M-1c uses focus-gated unsaved-changes prompt only.

### 4.8 Test surface

| Test | Scope |
|---|---|
| `tst_tx_applet_profile_combo_select.cpp` | Selecting a profile triggers `MicProfileManager::setActive` |
| `tst_tx_applet_two_tone_button.cpp` | 2-TONE mutually exclusive with TUN; calls `TransmitModel::setTwoTone(true)`; visual feedback |
| `tst_tx_applet_two_tone_band_plan.cpp` | 2-TONE rejected in CW mode (BandPlanGuard) — toast appears |
| `tst_setup_tx_profile_save.cpp` | Save flow: InputBox → comma strip → overwrite confirm → DB write → fire handlers |
| `tst_setup_tx_profile_delete.cpp` | Delete flow: last-profile guard → confirm → DB delete → re-index |
| `tst_setup_tx_profile_unsaved_prompt.cpp` | Focus-gated unsaved-changes prompt |
| `tst_tx_applet_combo_right_click.cpp` | Right-click jumps to Setup → TX Profile |

### 4.9 Risk

**Low.** UI work mirrors Thetis closely. Risk concentrated in the
unsaved-changes prompt's focus gating — getting this wrong leaks the
prompt into programmatic profile changes.

---

## 5. Chunk 5 — Persistence audit + hard-cutover key rename

### 5.1 Goal

Rename 16 3M-1b keys to Thetis column names + audit every TX-side
AppSettings key for round-trip. Hard cutover, no migration code (Q3
locked).

### 5.2 Rename mapping

3M-1b → 3M-1c column name remap. 16 keys, all under
`hardware/<mac>/tx/...`:

| 3M-1b name (NereusSDR-original) | 3M-1c name (Thetis column) | Source |
|---|---|---|
| `micGainDb` | `MicGain` | database.cs:4340 |
| `micBoost` | `Mic_Input_Boost` | database.cs:4457 |
| `micXlr` | `Mic_XLR` | (NereusSDR-only — keep as-is, no Thetis equivalent) |
| `lineIn` | `Line_Input_On` | database.cs:4458 |
| `lineInBoost` | `Line_Input_Level` | database.cs:4459 |
| `micTipRing` | `Mic_TipRing` | (NereusSDR-only — keep as-is) |
| `micBias` | `Mic_Bias` | (NereusSDR-only — keep as-is) |
| `micPttDisabled` | `Mic_PTT_Disabled` | (NereusSDR-only — keep as-is) |
| `voxThresholdDb` | `Dexp_Threshold` | database.cs:4360 |
| `voxGainScalar` | `VOX_GainScalar` | (NereusSDR-only — keep as-is) |
| `voxHangTimeMs` | `VOX_HangTime` | database.cs:4362 |
| `antiVoxGainDb` | `AntiVox_Gain` | (NereusSDR-only — keep as-is) |
| `antiVoxSourceVax` | `AntiVox_Source_VAX` | (NereusSDR-only — keep as-is) |
| `monitorVolume` | `MonitorVolume` | (NereusSDR-only — keep as-is, but match Thetis case) |
| `micSource` | `Mic_Source` | (NereusSDR-only — keep as-is) |

(Note: 8 of 16 keys have no direct Thetis equivalent and are
NereusSDR-specific. For those, rename to a consistent
`PascalCase_Underscored` style matching Thetis's stylistic conventions
even if the column name itself is invented. This is a NereusSDR-
original schema decision, not a Thetis port — stamp internally as
"NereusSDR-original" without a Thetis cite.)

### 5.3 Persistence audit method

For each TX-side AppSettings key:

1. Write the key with a known value.
2. Quit the app (or simulate close → AppSettings flush).
3. Restart (or reload AppSettings).
4. Read the key.
5. Assert read value == written value.

Audit covers all keys under `hardware/<mac>/tx/...` plus the new
profile-bank keys under `hardware/<mac>/tx/profile/<name>/...` plus the
new 2-tone keys (chunk 2).

### 5.4 Locked decisions

1. **Hard cutover** — no migration code (Q3 locked). 3M-1b naming
   hasn't shipped to a tagged release, so users have no settings files
   with the old keys yet.
2. **NereusSDR-only fields** keep PascalCase_Underscored style without
   Thetis cite (since Thetis doesn't have those columns).
3. **Persistence audit test** runs in the test harness as an
   end-to-end sweep over all TX-side keys.
4. **Audit covers chunks 1-9 keys** — every chunk that adds a setting
   contributes its keys to the audit list.

### 5.5 Test surface

| Test | Scope |
|---|---|
| `tst_persistence_audit_3m1c.cpp` | All ~30 TX-side keys round-trip (16 renamed + 7 new 2-tone + 7 new profile-related) |
| `tst_persistence_3m1b_old_keys_absent.cpp` | After 3M-1c launch, NO old 3M-1b key names exist in the settings file |
| `tst_persistence_per_mac_isolation.cpp` | Two MACs have independent settings — no cross-talk |

### 5.6 Risk

**Low.** Mechanical rename + audit. The only real risk is missing a
keysetting somewhere in the codebase that still uses the old name — the
audit test catches this.

---

## 6. Chunk 6 — MoxChangeHandlers / MoxPreChangeHandlers Qt signals

### 6.1 Goal

Generalise `MoxController::moxChanged(bool)` to Thetis-style multicast
signals. New signature:
- `moxChanging(int rx, bool oldMox, bool newMox)` (pre-change)
- `moxChanged(int rx, bool oldMox, bool newMox)` (post-change, replaces
  current single-arg `moxChanged(bool)`)

The existing single-arg signal renames to `moxStateChanged(bool)`
(simpler API for slots that just need the on/off state).

### 6.2 Thetis source — delegate definitions

**`console.cs:44851` [v2.10.3.13]**:

```csharp
public delegate void MoxChanged(int rx, bool oldMox, bool newMox);
```

**`console.cs:45012` [v2.10.3.13]**:

```csharp
public MoxChanged MoxChangeHandlers;
```

Plus `MoxPreChangeHandlers` (declared adjacent — same delegate type).

### 6.3 Pre-change invocation

**`console.cs:29324` [v2.10.3.13]**:

```csharp
MoxPreChangeHandlers?.Invoke(rx2_enabled && VFOBTX ? 2 : 1, _mox, chkMOX.Checked); // MW0LGE_21k8
```

Fires **before** any state change. `_mox` still holds old value.

### 6.4 Post-change invocation

**`console.cs:29677` [v2.10.3.13]**:

```csharp
if (bOldMox != tx) MoxChangeHandlers?.Invoke(rx2_enabled && VFOBTX ? 2 : 1, bOldMox, tx); // MW0LGE_21a
```

Fires **after** full transition. `_mox`, `Display.MOX`, `cmaster.Mox`,
`Audio.MOX`, hardware MOX all match.

### 6.5 The `int rx` argument

```csharp
rx2_enabled && VFOBTX ? 2 : 1
```

**Receiver index that owns the TX path:** `2` if RX2 enabled AND VFOBTX,
`1` otherwise. Not "active receiver index" generically — RX2 alone
without VFOBTX still produces `rx == 1` because TX comes off VFO-A.

### 6.6 Thetis subscriber inventory (Area C research)

12 distinct subscribers across the Thetis codebase. NereusSDR's 3M-1c
will have fewer (no PSForm yet, no TCI server yet, no MeterManager
Pre/Post in 3M-1c), but the framework signal should be wired
identically so 3M-7 / 3J / 3M-4 plug in cleanly. Anticipated NereusSDR
subscribers:

| NereusSDR Subscriber | Phase | Pre/Post | Action |
|---|---|---|---|
| VfoDisplayItem | 3M-1c chunk 1 | Post | setTransmitting(newMox) for the right `rx` |
| TxApplet (existing) | 3M-1c chunk 4 | Post | Mox button visual + tooltip |
| MeterPoller | 3M-1c chunk 6 | Pre + Post | Freeze readings on Pre; switch RX→TX poll set on Post |
| AudioEngine | 3M-1c chunk 6 | Post | RX-leak gate (already works via rxBlockReady — verify chunk 6 doesn't break) |
| TimeOutTimerManager | future (3M-7) | Pre or Post | Reset MOX time-out timer |
| TCIServer | future (3J) | Pre + Post | Push MoxChange to TCI clients |
| Audio recorder | future (3M-7) | Pre | Abort wav record/playback on MOX boundary |

### 6.7 Locked decisions

1. **Two new signals** on `MoxController`:
   - `moxChanging(int rx, bool oldMox, bool newMox)` — pre, fires
     before any state change
   - `moxChanged(int rx, bool oldMox, bool newMox)` — post, fires after
     full transition (replaces existing 1-arg moxChanged)
2. **Rename existing `moxChanged(bool)`** → `moxStateChanged(bool)`.
   Migrate ~30 callsites compile-time (signature change is
   compile-error-loud).
3. **`int rx` semantics** match Thetis: receiver index that owns TX
   path. Tests assert this exactly.
4. **Pre-change emits before any side effects.** Post-change emits
   after every side effect (audio path flip, hardware MOX, UI
   updates).
5. **Wire NereusSDR-equivalent subscribers** for chunks 1-9. Future
   subscribers (3M-7, 3J, 3M-4) plug in without signal changes.

### 6.8 Test surface

| Test | Scope |
|---|---|
| `tst_mox_controller_moxStateChanged_rename.cpp` | ~30 callsites still receive the on/off bool via renamed signal |
| `tst_mox_controller_moxChanging_pre_emit.cpp` | moxChanging fires BEFORE any state change |
| `tst_mox_controller_moxChanged_post_emit.cpp` | moxChanged fires AFTER state change with correct (rx, oldMox, newMox) |
| `tst_mox_controller_rx_argument_semantics.cpp` | rx=2 iff (rx2_enabled && VFOBTX); rx=1 otherwise |
| `tst_mox_controller_pre_post_ordering.cpp` | Pre always fires before Post, both within the same MOX transition |

### 6.9 Risk

**Low.** Signal generalisation with a clear Thetis precedent. The
signature change is compile-time enforced — slots that don't update
will fail to compile, surfacing every callsite for migration.

---

## 7. Chunk 7 — B.2 TX timer push-driven refactor

### 7.1 Goal

Replace `TxChannel::driveOneTxBlock` 5 ms QTimer with push-driven slot
connected to `AudioEngine::micBlockReady(const float*, int)` signal.
Drop the `1b353f4` zero-fill workaround.

### 7.2 Thetis source — TX pump architecture (Area D research)

Thetis is **callback-driven from native code**, not timer-driven. The C#
layer never calls `fexchange2` on a timer.

**`cmaster.cs:493-497` [v2.10.3.13]** — Stream block sizes:

```csharp
int[] cmInboundSize = new int[8] { 240, 240, 240, 240, 240, 720, 240, 240 };
fixed (int* pcmSPC = cmSPC, pcmIbSize = cmInboundSize)
    cmaster.SetRadioStructure(8, cmRCVR, 1, cmSubRCVR, 1, pcmSPC, pcmIbSize,
                               1536000, 48000, 384000);
```

**Stream 5 = mic input = 720 samples @ 48 kHz = 15 ms blocks.**

**`cmaster.cs:514-518` [v2.10.3.13]** — Mic input rate hardcoded:

```csharp
int txinid = cmaster.inid(1, 0);
int txch = cmaster.chid(txinid, 0);
cmaster.SetXcmInrate(txinid, 48000);
```

**`cmaster.cs:1123-1136` [v2.10.3.13]** — `SendCallbacks()` is the only
C# → native callback registration; no per-frame mic delivery from C#.

**`dsp.cs:101-102` [v2.10.3.13]** — `fexchange2` P/Invoke declaration is
the only managed-side reference; **zero call sites in managed code**.

The native code (inside `wdsp.dll` / `cmaster.dll`):
1. Receives the radio's mic frame (P1 EP6 mic byte zone or P2 mic packet)
2. Accumulates into the 720-sample buffer
3. Calls `fexchange2(channel=WDSP.id(1,0), Iin=mic, Qin=zeros, Iout=tx_iq, Qout=tx_iq_q, error)`
4. Hands TX I/Q back to the network layer

### 7.3 Producer/consumer alignment

Thetis's 720-sample (15 ms) accumulator is **larger than** the radio's
native mic frame:
- P1 EP2 mic = 63 samples / 1.31 ms — Thetis accumulates **11.4 EP2
  frames** before fexchange2
- P2 mic packet = variable

NereusSDR's PC mic delivers 256 samples / 5.33 ms blocks — Thetis's
720-sample accumulator would accumulate **2.81 NereusSDR PC mic blocks**
before each fexchange2.

### 7.4 mi0bot-Thetis cross-check

`mi0bot-Thetis cmaster.cs:495 [v2.10.3.13-beta2]` has the **identical**
720-sample mic block size. No HL2-specific TX block-size override —
HL2-specific TX timing differences (if any) live in
`ChannelMaster.dll`'s native code which is the Phase 3L target (out of
3M-1c scope).

### 7.5 Locked decisions

1. **Drop the QTimer.** `TxChannel::driveOneTxBlock(samples, frames)`
   becomes a slot connected to `AudioEngine::micBlockReady(samples,
   frames)` via `Qt::DirectConnection` (audio-thread → audio-thread).
2. **Accumulator size = 720 samples** (matches Thetis exactly per
   `cmaster.cs:495 [v2.10.3.13]`). NereusSDR's existing 256-sample
   AudioEngine block accumulates 2.81 PC-mic blocks before each
   fexchange2. Adds ~10 ms of latency on PC mic vs current 5 ms timer
   implementation, but matches Thetis behaviour.
3. **Drop `1b353f4` zero-fill workaround** — push-driven model
   eliminates the producer/consumer mismatch by definition.
4. **TxMicRouter still owns source selection** (PC vs Radio) — but the
   pump is push-driven from AudioEngine, not pull-driven from a timer.
5. **HL2 timing concerns** — none surfaced in the desk-review (chunk 0
   §2.1) or in this Area D research. Bench-verify chunk 7 changes don't
   regress HL2.

### 7.6 Test surface

| Test | Scope |
|---|---|
| `tst_audio_engine_mic_block_ready.cpp` | Signal fires on every 720-sample accumulation; not on partials |
| `tst_tx_channel_push_driven.cpp` | `driveOneTxBlock` only called via signal; no QTimer |
| `tst_tx_channel_no_zero_fill.cpp` | Zero-fill code path removed; verify no silent frames in 30s simulated transmission |
| `tst_tx_channel_accumulator_720.cpp` | Accumulator matches Thetis 720 samples per fexchange call |
| `tst_audio_engine_underrun_handling.cpp` | If mic input stalls, micBlockReady doesn't fire (no spurious zero-fill) |

### 7.7 Risk

**Medium.** Architecture change in the audio path. TDD harness can
simulate the underrun and overrun cases via mock audio source. Bench-
verify with 30-min SSB transmission (manual matrix row).

---

## 8. Chunk 8 — B.4 Initial-state-sync audit

### 8.1 Goal

Audit pass over every signal connected in `RadioModel::onConnected()`.
For each, verify the slot fires on attach OR the model state is
explicitly pushed in the constructor. Targets identified in the 3M-1b
post-code review §14b note 4.

### 8.2 Background

3M-1b's `1841462` fixed `micPreamp` not firing on `TxChannel` attach
because **Qt's signal-connect doesn't fire for the current value**. The
fix was to explicitly push the current model value after attaching. The
3M-1b carry-forward identified that other L.1 wires (TX monitor enabled,
TX monitor volume, anti-VOX gain, VOX threshold) may have the same gap.

### 8.3 Audit targets

For each NereusSDR signal connection in `RadioModel::onConnected()`:

| Signal | Slot | Push-on-attach status |
|---|---|---|
| `TransmitModel::micPreampChanged` | `TxChannel::setMicPreamp` | ✅ fixed in 3M-1b `1841462` |
| `TransmitModel::txMonitorEnabledChanged` | `AudioEngine::setTxMonitorEnabled` | ❓ verify |
| `TransmitModel::txMonitorVolumeChanged` | `AudioEngine::setTxMonitorVolume` | ❓ verify |
| `TransmitModel::antiVoxGainChanged` | `MoxController::setAntiVoxGain` | ❓ verify |
| `TransmitModel::voxThresholdChanged` | `MoxController::setVoxThreshold` | ❓ verify |
| `TransmitModel::voxGainScalarChanged` | `MoxController::setVoxGainScalar` | ❓ verify |
| `TransmitModel::voxHangTimeChanged` | `MoxController::setVoxHangTime` | ❓ verify |
| `TransmitModel::voxEnabledChanged` | `MoxController::setVoxEnabled` | ❓ verify |
| `TransmitModel::antiVoxSourceVaxChanged` | `MoxController::setAntiVoxSourceVax` | ❓ verify |
| `TransmitModel::micMuteChanged` | `TxChannel::setMicMute` | ❓ verify |
| `TransmitModel::micBoostChanged` | `RadioConnection::setMicBoost` | ❓ verify |
| (other mic-jack flags) | | ❓ verify |

Plus chunk-1c-specific new attaches:
- `TransmitModel::twoToneFreq1Changed` → `TxChannel::setTxPostGenTTFreq1`
- … (all new 2-tone setters from chunk 2)
- `TransmitModel::activeProfileChanged` → `MicProfileManager::loadProfile`

### 8.4 Locked decisions

1. **Audit method:** for each connection in `RadioModel::onConnected()`,
   verify that immediately after `connect()` the model's current value
   is explicitly pushed to the slot. Pattern (mirroring 3M-1b
   `1841462`):

```cpp
QObject::connect(model, &TransmitModel::xxxChanged, slot, &Target::setXxx);
slot->setXxx(model->xxx());  // push current value
```

2. **Test pattern:** for each attach path, write a test that:
   - Constructs RadioModel + dependencies
   - Sets non-default model state
   - Triggers `onConnected()`
   - Asserts the dependency received the model's state

### 8.5 Test surface

One test per audit target — ~10-15 tests total. Each tests the same
pattern: model has value X, attach happens, dependency now has value X.

### 8.6 Risk

**Low.** Mechanical audit. The fix per attach is a single line
(`slot->setXxx(model->xxx())` after `connect()`).

---

## 9. Chunk 9 — B.5 Mic-gain default refinement

### 9.1 Goal

Bench-tune the `−6 dB` mic-gain default from 3M-1b across multiple mic
types. Update default if warranted.

### 9.2 Bench-test targets

| Mic type | Expected behaviour |
|---|---|
| USB headset (e.g. Apple AirPods, Logitech) | Clean SSB at default gain |
| Dynamic mic (e.g. Heil PR40) | Clean SSB at default gain |
| Condenser mic | Clean SSB at default gain |
| Built-in laptop mic | Clean SSB at default gain |

### 9.3 Acceptance criteria

- ALC meter shows healthy deflection (not pinned, not silent)
- TxMic meter shows healthy deflection
- Test receiver hears clean SSB voice without distortion or low-level

### 9.4 Locked decisions

1. **Default value may change** based on bench results. If JJ heard
   noise floor at default settings during 3M-1b bench (`feedback_b5`),
   the default may need to drop further (to e.g. `-9 dB` or `-12 dB`).
2. **Persistence:** if default changes, update the factory "Default"
   profile (chunk 3) AND the `MicGain` AppSettings default.
3. **No code changes** if bench confirms `-6 dB` is correct across mic
   types.

### 9.5 Test surface

This chunk is **bench-driven** — no unit tests. Verification matrix row
for each mic type tested.

### 9.6 Risk

**Low.** Documentation deliverable + possible single-default-value
change.

---

## 10. Cross-cutting risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | Two-tone pulsed mode parameter ranges (chunk 2) | Cross-check deskhpsdr secondary; bench-verify with spectrum analyser |
| R2 | Push-driven TX timer regression (chunk 7) | TDD harness simulates underrun/overrun; bench-verify with 30-min SSB transmission |
| R3 | Hard-cutover key rename leaves orphans (chunk 5) | Persistence audit test asserts old key names absent after first 3M-1c launch |
| R4 | MoxController callsite migration breaks observers (chunk 6) | Compile-time enforced (signature change); test sweep catches behavioural regressions |
| R5 | HL2 path divergence (chunks 1, 7) | Chunk 0 desk-review already absorbed 2 critical fixes; bench rows confirm |
| R6 | `setupTwoTonePulse()` magnitude profile non-trivial (chunk 2) | Pulse profile uses Thetis defaults verbatim; cross-check with deskhpsdr if Thetis is unclear |
| R7 | Profile schema scope creep (chunk 3) | Live-fields-only locked at Q2; 3M-3a sub-PRs add their slices |

---

## 11. Test surface summary

Estimated new unit tests across chunks 1-9: **~45**.

| Chunk | Estimated tests |
|---|---|
| 1 | 2 |
| 2 | 8 |
| 3 | 6 |
| 4 | 7 |
| 5 | 3 |
| 6 | 5 |
| 7 | 5 |
| 8 | ~10 |
| 9 | 0 (bench-driven) |
| **Total** | **~46** |

Plus existing tests still pass (222/222 baseline confirmed at chunk 0
fixup commit `69c4054`).

Plus manual bench rows (added to verification matrix):

- `[3M-1c-bench-HL2]` VFO TX badge
- `[3M-1c-bench-HL2]` Two-tone continuous + pulsed
- `[3M-1c-bench-HL2]` Profile save/load/restart-app round-trip
- `[3M-1c-bench-G2]` Same matrix as HL2, plus mic-jack flag persistence
- `[3M-1c-bench]` Push-driven TX timer (30-min SSB transmission)
- `[3M-1c-bench]` Mic-gain bench-tune across mic types

---

## 12. References

### NereusSDR

- Design spec: [`phase3m-1c-polish-persistence-design.md`](phase3m-1c-polish-persistence-design.md)
- HL2 desk-review: [`phase3m-1c-hl2-tx-path-review.md`](phase3m-1c-hl2-tx-path-review.md)
- 3M-1b plan: [`phase3m-1b-mic-ssb-voice-plan.md`](phase3m-1b-mic-ssb-voice-plan.md)
- 3M-1b post-code review: [`phase3m-1b-post-code-review.md`](phase3m-1b-post-code-review.md)
- Master design: [`phase3m-tx-epic-master-design.md`](phase3m-tx-epic-master-design.md) §5.3
- Attribution / source-first protocol: [`docs/attribution/HOW-TO-PORT.md`](../attribution/HOW-TO-PORT.md)

### Upstream

- Thetis: `https://github.com/ramdor/Thetis` @ `v2.10.3.13` / `@501e3f51`
- deskhpsdr: `@120188f`
- mi0bot-Thetis: `v2.10.3.13-beta2` / `@c26a8a4`

### Key Thetis source files for 3M-1c

- `Project Files/Source/Console/console.cs` — `chk2TONE_CheckedChanged` (44728-44760), MOX state machine (29017-29677), `MoxChangeHandlers` (44851/45012), `BuildTXProfileCombos` (8125-8183), `comboTXProfile_MouseDown` (44519-44522)
- `Project Files/Source/Console/setup.cs` — `chkTestIMD_CheckedChanged` (11019-11200ish), `btnTXProfileSave_Click` (9545-9612), `btnTXProfileDelete_Click` (9615-9656), `comboTXProfileName_SelectedIndexChanged` (9505-9543), `loadTXProfile` (9252-9292), `highlightTXProfileSaveItems` (3399-3550)
- `Project Files/Source/Console/database.cs` — `AddTXProfileTable` (4299-4540) — column-name reference
- `Project Files/Source/Console/cmaster.cs` — `cmInboundSize` (493-497), mic input rate (514-518), `SendCallbacks` (1123-1136)
- `Project Files/Source/Console/dsp.cs` — `fexchange2` P/Invoke (101-102) — declaration only, no managed call sites

### Key mi0bot-Thetis source files for 3M-1c cross-checks

- `Project Files/Source/Console/cmaster.cs:495` — identical 720-sample mic block size (no HL2 override)

### AetherSDR pattern references (chunk 1)

- `AetherSDR/src/gui/VfoWidget.{h,cpp}` — floating VFO Flag widget pattern; `m_txBadge` (slice assignment, not MOX state)

---

End of pre-code Thetis review. Plan-writing next:
`phase3m-1c-polish-persistence-plan.md` with TDD task list per chunk.
