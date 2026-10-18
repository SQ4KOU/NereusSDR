# Phase 3M-4: PureSignal PA Linearization (Design Doc)

**Status:** Design ratified, awaiting user review before plan-write.
**Owner:** J.J. Boyd (KG4VCF) + Claude Code (AI-assisted).
**Master-plan slot:** `docs/MASTER-PLAN.md` §3M-4 PureSignal PA Linearization.
**TX-epic slot:** `docs/architecture/phase3m-tx-epic-master-design.md` §8.1 (PSForm + calcc + iqc) + §8.2 (AmpView), merged into one PR per Q1.
**Created:** 2026-05-06.

---

## 1. Goal

Port Thetis PureSignal (PA pre-distortion / linearization) to NereusSDR with full feature parity. Ship as a single PR with logical commit boundaries.

PureSignal is entirely client-side for OpenHPSDR radios (no AetherSDR equivalent, pure Thetis port). It captures the actual PA output via a feedback ADC, runs the WDSP `calcc` correction calculator on the captured I/Q, and applies pre-distortion to outgoing TX I/Q via the WDSP `iqc` real-time correction stage in `xtxa()`.

Hardware coverage: ANAN-100D, ANAN-200D, ANAN-7000DLE, ANAN-8000DLE, ANAN-G2, ANAN-G2-1K, ANVELINAPRO3 (P2 dual-stream), HermesII (P1 2-ADC), Orion MkII (P1 2-ADC), Hermes Lite 2 (P1, mi0bot fork). Boards without PureSignal capability hide the entire surface via `BoardCapabilities::hasPureSignal`.

---

## 2. Source-First Protocol (binding)

This port follows the source-first protocol from `CLAUDE.md` (READ → SHOW → TRANSLATE) and the subagent-source-first rule from `feedback_subagent_thetis_source_first.md`. **Every line of ported logic must cite the upstream source** with a version stamp, every license header is preserved byte-for-byte, every inline author tag (`//MW0LGE`, `//W2PA`, `//DH1KLM`, etc.) is preserved within ±5 lines of the cited Thetis line per `feedback_inline_cite_versioning.md`.

### 2.1 Two upstreams

| Upstream | Path | Version stamp | Used for |
| --- | --- | --- | --- |
| ramdor/Thetis | `/Users/j.j.boyd/Thetis/Project Files/Source/` | `v2.10.3.13 @ 501e3f51` | All boards except HL2; canonical PSForm + AmpView + calcc + iqc + ucInfoBar |
| mi0bot-Thetis | `/Users/j.j.boyd/mi0bot-Thetis/Project Files/Source/` | `v2.10.3.13-beta2 @ c26a8a4c` | HL2-specific deltas only (NeedToRecalibrate_HL2, HL2 PS DDC config, HL2 sample-rate) |

Per `feedback_mi0bot_authoritative_for_hl2.md`: when porting HL2 PS behavior, read mi0bot first; when porting non-HL2 behavior, read ramdor.

### 2.2 Inline cite format (verbatim from `feedback_inline_cite_versioning.md`)

```cpp
// From Thetis console.cs:8264 [v2.10.3.13]
constexpr uint8_t kPSFeedbackCntrl1Mask = 0xf3;

// From mi0bot-Thetis console.cs:8488 [v2.10.3.13-beta2]
constexpr uint8_t kHL2PSFeedbackCntrl1 = 4;
```

Every new `// From Thetis ...` or `// From mi0bot-Thetis ...` cite carries `[vX.Y.Z.W]` or `[@shortsha]`. Annotations like "unmerged PR #N" go OUTSIDE the brackets.

### 2.3 Subagent dispatch protocol (binding for every dispatched agent)

When this design moves to plan-write and implementation, every subagent dispatched for 3M-4 work receives the following **mandatory context block** at the top of its prompt:

> **Thetis source-first context:**
> - ramdor/Thetis path: `/Users/j.j.boyd/Thetis/Project Files/Source/`
> - mi0bot-Thetis path: `/Users/j.j.boyd/mi0bot-Thetis/Project Files/Source/`
> - Version stamps: ramdor `v2.10.3.13 @ 501e3f51`, mi0bot `v2.10.3.13-beta2 @ c26a8a4c`
> - Protocol: READ the cited Thetis source FIRST. SHOW the original code in your commit message. TRANSLATE faithfully. Do not infer, do not fabricate, do not improvise.
> - **STOP-AND-ASK** if you cannot locate a Thetis source for any value, range, default, behavior, attribution, or UI structure. Do not invent a default. Do not infer a range. Do not paraphrase.
> - License preservation per `docs/attribution/HOW-TO-PORT.md`. Inline tag preservation per `feedback_inline_cite_versioning.md`. Both are CI-enforced via `scripts/verify-thetis-headers.py` and `scripts/verify-inline-tag-preservation.py`.

Every per-task subagent prompt also includes the **specific Thetis cites** the agent must read first (e.g., "port `SetPSControl` from `cmaster.cs:530-540 [v2.10.3.13]`; READ that DllImport, READ `wdsp/calcc.c:958` for the C signature/parameter range/defaults, READ `PSForm.cs:880` for the call-site"). Read-only research agents (Explore / code-explorer / general-purpose) are NOT exempt.

---

## 3. Architecture

### 3.1 Layer boundaries

```
                    ┌──────────────────────────────────────────┐
                    │  UI layer (src/gui/)                     │
                    │   PsForm (modeless, Tools > PureSignal)  │
                    │   ├── btnPSAmpView → AmpViewWindow       │
                    │   PureSignalApplet (RX-side quick)       │
                    │   TxApplet [PS-A] toggle                 │
                    │   PsaIndicatorWidget (bottom banner FB)  │
                    │   Antenna/ALEX page (HPF Bypass on PS)   │
                    │   General/Options page (2 PS chk)        │
                    │   SpectrumWidget IMD overlay             │
                    │   Top menu Tools > PureSignal entry      │
                    └────────────────────┬─────────────────────┘
                                         │ Q_PROPERTY signals/slots
                                         ▼
                    ┌──────────────────────────────────────────┐
                    │  Host coordinator (src/core/PureSignal.*) │
                    │   ~300 LOC class                         │
                    │   ├── Cal lifecycle (single + auto)      │
                    │   ├── MOX integration                    │
                    │   ├── Auto-attention sub-state machine   │
                    │   ├── Status reads (GetPS*)              │
                    │   ├── Save/Restore coefficients          │
                    │   └── Two-tone test integration          │
                    └────────────────────┬─────────────────────┘
                                         │
                          ┌──────────────┼──────────────┐
                          ▼              ▼              ▼
                ┌──────────────┐ ┌──────────────┐ ┌──────────────┐
                │ TxChannel    │ │ ReceiverMgr  │ │ Per-board    │
                │ PS setters   │ │ UpdateDDCs   │ │ Codec PS     │
                │ + readers    │ │ (PS branch)  │ │ DDC config   │
                │ +PsFeedback  │ │              │ │              │
                │ Channel      │ │              │ │              │
                └──────┬───────┘ └──────┬───────┘ └──────┬───────┘
                       │                │                │
                       ▼                ▼                ▼
                ┌─────────────────────────────────────────────────┐
                │  WDSP layer (third_party/wdsp/)                 │
                │   calcc.c (1,164 LOC) verbatim port             │
                │   iqc.c (315 LOC) verbatim port                 │
                │   pscc() autonomous via xputbuf chain           │
                │   xiqc() applies real-time correction in xtxa() │
                └─────────────────────────────────────────────────┘
                                         │
                                         ▼
                ┌─────────────────────────────────────────────────┐
                │  Radio protocol (src/core/codec/)               │
                │   P1RadioConnection (HL2 single-ADC, mi0bot)    │
                │   P2RadioConnection (G2/etc. dual-DDC stream)   │
                └─────────────────────────────────────────────────┘
```

### 3.2 Layer responsibilities

| Layer | Responsibility | Source-first scope |
| --- | --- | --- |
| WDSP | DSP math (calcc state machine, iqc real-time correction) | Verbatim C99 port from upstream |
| Channel wrappers | C++ wrapper around WDSP `SetPS*` / `GetPS*` API | Mirror Thetis cmaster.cs DllImport signatures exactly |
| Per-board codec | Translate PS DDC config to wire bytes per board | Mirror Thetis console.cs:8186-8538 switch verbatim |
| ReceiverManager | UpdateDDCs PS state machine | Port Thetis `UpdateDDCs()` switch verbatim |
| Host coordinator | Cal lifecycle + MOX gating + status polling | Mirror Thetis `PSForm.cs` timer1code + ucInfoBar PSA driver |
| UI | All Thetis PS surfaces | 1:1 IA per `feedback_thetis_userland_parity.md` |

### 3.3 Verified WDSP architecture (corrects earlier `wdsp-integration.md` §6 sketch)

The earlier `docs/architecture/wdsp-integration.md` §6 sketched a `PureSignal::processFeedback()` host-driven pump. **That is wrong.** Source-verified: `pscc()` is autonomous inside WDSP, defined at `wdsp/calcc.c:617`, called only from `wdsp/calcc.c:856` (an internal calcc state-machine function). Zero callsites in any `Source/Console/` C# file. The host's job is to:

1. **Configure** PS via `SetPS*` setters (control / mox / mancal / automode / turnon / mox-delay / loop-delay / TX-delay / hw-peak / ptol / pin / map / stabilize / ints-and-spi).
2. **Push samples** through the normal channel processing (TX samples through `xtxa()` which calls `xiqc()` for real-time pre-distortion; feedback samples through the dedicated feedback RX channel which calcc reads autonomously).
3. **Read state** via `GetPSInfo()` / `GetPSHWPeak()` / `GetPSMaxTX()` / `GetPSDisp()`.

The host does NOT push per-buffer samples into a `processFeedback()` method. The `PureSignal` C++ class is a configuration + monitoring layer, not a per-buffer DSP driver.

### 3.4 IQC chain placement (verified)

`xiqc(txa[channel].iqc.p0)` runs in `xtxa()` at `wdsp/TXA.c:583`, **after** `xuslew` / `xmeter(alcmeter)` / `xsiphon` and **before** `xcfir` (P2-only) / `xresample` / `xmeter(out)`. Pre-distortion applies to the final modulated I/Q just before the output resampler. This pins the IQC slot inside `TxChannel::processIq()` for the NereusSDR port.

### 3.5 Feedback path: G2 vs HL2

| Board class | Feedback hardware | Host-side architecture | Source |
| --- | --- | --- | --- |
| ANAN-100D / 200D / OrionMkII / 7000 / 8000 / G2 / G2-1K / ANVELINAPRO3 | Dedicated feedback ADC (separate from RX ADCs) | Dual stream: DDC0=feedback, DDC1=RX1 sync, both at `ps_rate=192000` | `console.cs:8186-8290 (ramdor)` |
| HermesII | Single ADC reused; firmware switches mode during MOX | Dual DDC stream; `ps_rate` | `console.cs:8453+ (ramdor)` |
| Hermes Lite 2 (mi0bot only) | Single ADC time-multiplexed via firmware mode bit | Dual DDC stream from host's perspective; `rx1_rate` (HL2 can run any rate) | `console.cs:8472-8488 (mi0bot)` |
| Atlas / classic Hermes / Angelia / classic Orion / Hermes-Lite (v1) | No PS hardware | `caps.hasPureSignal=false`, all UI hidden | n/a |

**Critical architectural finding**: from the WDSP / host's perspective, ALL PS-capable boards look dual-stream (DDC0 feedback, DDC1 sync). The HL2 single-ADC reality is hidden inside the radio firmware. The WDSP API call `SetPSRxIdx(0, 0); SetPSTxIdx(0, 1)` is identical for every board (per `cmaster.cs:533-534 [v2.10.3.13]` and `[v2.10.3.13-beta2]`, comment "all current models use Stream0 for RX feedback / Stream1 for TX feedback", unchanged across both upstreams).

The G2 vs HL2 divergence is purely at the protocol register layer:

| Aspect | G2-class (ramdor) | HL2 (mi0bot) |
| --- | --- | --- |
| Sample rate during MOX+PS | `ps_rate` (192000, `cmaster.cs:424`) | `rx1_rate` |
| `cntrl1` register | `(rx_adc_ctrl1 & 0xf3) \| 0x08` (`console.cs:8264, 8275, 8345, 8356`) | `4` literal (`console.cs:8488`) |
| `P1_DDCConfig` | `3` | `6` |
| DDC pattern (PS-on, no diversity) | `DDC0+DDC2`, Sync=`DDC1` | `DDC0`, Sync=`DDC1` |

**No `feedbackPathStyle` cap flag needed.** Both styles look dual-stream from the WDSP layer. Per-board divergence lives in the existing per-board codec layer (`P1CodecHl2`, `P1CodecStandard`, `P2CodecOrionMkII`, future `P2CodecG2`) inside an `applyPureSignalDdcConfig(...)` virtual method.

---

## 4. UI Inventory (15 Surfaces)

Every Thetis PS UI surface ports 1:1. Acceptance criterion: full UI surface usability at land. No scaffolded-but-disabled controls, no model-wired-but-not-wire-format-correct stubs.

| # | Thetis surface | Thetis cite | NereusSDR plan |
| --- | --- | --- | --- |
| 1 | PSForm.cs (1,164 LOC) | `Project Files/Source/Console/PSForm.cs` | New `PsForm` modeless dialog. Title "PureSignal 2.0" verbatim. ClientSize 560 × 300 px (basic) / 560 × 60 px (Advanced collapsed mode per `PSForm.cs:889-905`). Single flat form; `chkAdvancedViewHidden` toggles compact-strip mode |
| 2 | AmpView.cs (528 LOC) | `Project Files/Source/Console/AmpView.cs` | New `AmpViewWindow` modeless dialog. Title "AmpView 1.0" verbatim. ClientSize 564 × 401 px / Min 440 × 380 px. 5 chart series (Ref / MagAmp / PhsAmp / MagCorr / PhsCorr) feeding off `GetPSDisp()`. 4 toolbar checkboxes (Show Gain / Phase Zoom / Low Res / On Top). Lifecycle managed by PsForm (`FixAmpViewOnTop()`) |
| 3 | Menu item to open PsForm | `console.cs:43099-43104` (`linearityToolStripMenuItem_Click`) | Top-menu entry **`Tools > PureSignal...`** (user-chosen label; Thetis label is "Linearity..." but PureSignal is more recognizable). Capability-gated to `caps.hasPureSignal` |
| 4 | PS-A button on main console | `chkFWCATUBypass`, `console.cs:18605, 36762, 43707-43730, 46149-46152` | Existing scaffolded `TxApplet [PS-A]` button wired up. **Left-click** (toggle): drives `psform.AutoCalEnabled`, updates `infoBar.PSAEnabled`, ATT-on-TX un-stash on disable, `PSAChangedHandlers` invoke, `AndromedaIndicatorCheck` for front-panel feedback (`chkFWCATUBypass_Click`, `console.cs:36762`). **Right-click** (Thetis `chkFWCATUBypass_MouseDown` at `console.cs:46149-46152`): opens PsForm via `linearityToolStripMenuItem_Click(null, EventArgs.Empty)`. NereusSDR matches via `setContextMenuPolicy(Qt::CustomContextMenu)` on `m_psaBtn` (NereusSDR pattern, precedent: `RxApplet.cpp:1080`, `TxApplet.cpp:606` for [CFC]) |
| 5 | PSA indicator pair (FB + PS labels) | `ucInfoBar.cs:827-895` | New `PsaIndicatorWidget` in NereusSDR's existing bottom-banner HBox (`MainWindow.cpp:2586-2744`). Placement: **after `m_rxDashboard`, before `m_stationBlock`** (option B per visual brainstorm). 6-state machine: PS off (DimGray "Feedback") / Idle SeaGreen / Under-range Blue numeric / Marginal Yellow numeric / In-range Green numeric / Over-range Red numeric. PS label states: "Pure Signal2" SeaGreen → "Correcting" Lime when corrections applied |
| 6 | "HPF Bypass on PureSignal feedback" Setup checkbox | `setup.designer.cs:23635, 23673-23686` (`panelAlex1HPFControl`) | Already exists in NereusSDR at `AntennaAlexAlex1Tab.cpp:319` (built during 3P-I-a/b). 3M-4 adds the IMD warning dialog wire and the live-PureSignal hook. Default Checked=true |
| 7 | IMD warning dialog | `setup.cs:29275-29295` | Modal `QMessageBox::Warning` titled "PureSignal Issue". Multi-paragraph text copied verbatim. OK / Cancel buttons; Cancel default-focused (`MessageBoxDefaultButton.Button2`). Cancel reverts the toggle. Fires only when user UN-checks "HPF Bypass on PureSignal feedback" (i.e., chooses to include BPFs during PS TX) |
| 8 | "Hide feedback level" Setup checkbox | `chkHideFeebackLevel` (Thetis typo "Feeback") in `groupBoxTS23` on `tpOptions2`, `setup.designer.cs:10571` | New checkbox on **Setup → General → Options** (existing `Options` group). NereusSDR label: "Hide feedback level" (corrected spelling; source-cite preserves Thetis typo for traceability). Drives `_hideFeedback` in PsaIndicatorWidget. Per-MAC AppSettings persistence |
| 9 | "Swap red and blue PS-A feedback colours" Setup checkbox | `chkSwapREDBluePSAColours`, `setup.designer.cs:10572, 10619-10630` | New checkbox on **Setup → General → Options**. Drives `puresignal.InvertRedBlue`. Default unchecked. Per-MAC AppSettings persistence |
| 10 | `Pure_Signal_Enabled` per-TX-profile | `database.cs:4462`, `setup.cs:3711 / 9429 / 3349` | New key in `MicProfileManager` (key index TBD; current count is 91 from 3M-3a-ii). All 19+ stock profiles default to false. Profile recall flips PS state. **No Setup → TX Profiles editor checkbox**. Thetis doesn't have one either; state is implicit-via-profile-mechanism |
| 11 | PSSaveCorr / PSRestoreCorr file dialogs | `PSForm.cs:524-548` | Inside `PsForm` Save/Restore buttons: `QFileDialog::getSaveFileName` and `getOpenFileName`. Default folder `~/.config/NereusSDR/PureSignal/`. WDSP entry points: `puresignal.PSSaveCorr(txachannel, filename)` and `puresignal.PSRestoreCorr(txachannel, filename)`. Save button gated on `CorrectionsBeingApplied`; `_restoreON` flag set after Restore so calcc skips cal-from-scratch |
| 12 | Two-tone test (`btnPSTwoToneGen`) | inside `PSForm.cs` | PsForm button. Reuses existing `TwoToneController`. When activated, sets `Display.TestingIMD = true`, runs the WDSP tone generator, integrates with auto-cal triggering |
| 13 | Two-tone IMD spectrum overlay | `display.cs:5008` (show condition), `display.cs:5283-5298` (peak detection), `display.cs:5453-5475` (peak markers), `display.cs:5512-5560` (IMD3/IMD5 sort), `display.cs:5520` (box position), `display.cs:5650-5685` (readout text format) | New overlay layer on `SpectrumWidget`. Renders only when `MOX && testingIMD && showIMDMeasurements && displayDuplex`. **Coordinates with carson-branch peak-blob work**. Thetis `display.cs:5238` shares peak-detection between PS-blobs and IMD measurements via `peaks_imds = bPeakBlobs \|\| show_imd_measurements`. Box: 260 × 180 px rounded 14 px at X=50 Y=50. 3-column readout (label / absolute dBm / relative dBc): f0 L/U + IMD3 L/U + IMD5 L/U + worst IMD3 dBc + worst IMD5 dBc + OIP3 + OIP5. EMA-smoothed values |
| 14 | `ForcePureSignalAutoCalDisable()` external API | `console.cs:43703` | Public method on `PureSignal` coordinator. Called when sample rate changes mid-run, when band changes invalidate cal, etc. |
| 15 | FB-label left/right click handlers + dynamic tooltip | `ucInfoBar.cs:1042-1098` | On `PsaIndicatorWidget`. Left-click toggles `puresignal.InvertRedBlue` (mirror of Setup #9). Right-click toggles `_hideFeedback` (mirror of Setup #8). Hover shows tooltip with current color/range legend. PS label is passive (no handlers per Thetis) |

### 4.1 Color encoding for FB level (default un-swapped)

Verified from `ucInfoBar.cs:1090-1094`:

| Range | Default color | Swapped color | Meaning |
| --- | --- | --- | --- |
| 0-90 | Blue | Red | Under-range (signal too weak, ATT too high) |
| 91-128 | Yellow | Yellow | Marginal-low |
| 129-181 | **Green/Lime** | **Green/Lime** | **In target range, calcc happy** |
| 182+ | Red | Blue | Over-range (feedback ADC saturating) |

Tooltip text (verbatim):
- Default: `"[Showing level, ]Red 0-90, Yellow 91-128, Green 129-181, Blue 182+"`
- Swapped: `"[Showing level, ]Blue 0-90, Yellow 91-128, Green 129-181, Red 182+"`

The `[Showing level, ]` prefix appears only when `!_hideFeedback`.

PS label states:
- PS off → "Pure Signal2" / DimGray
- PS on, no MOX → "Pure Signal2" / SeaGreen
- PS on, MOX, not correcting → "Pure Signal2" / SeaGreen + FB shows numeric
- PS on, MOX, correcting → **"Correcting" / Lime** (compact: "Correct")

### 4.2 Retired NereusSDR Setup pages (no Thetis equivalent)

| Retired surface | NereusSDR file | Reason |
| --- | --- | --- |
| Setup → Hardware → PureSignal tab | `src/gui/setup/hardware/PureSignalTab.{h,cpp}` + entries in `HardwarePage.{h,cpp}` | No Thetis equivalent. Was a 3I scaffolding addition anticipating PS hardware-level config; PsForm covers everything |
| Setup → Transmit → PureSignal page | `PureSignalPage` in `TransmitSetupPages.cpp:1249-1366` | No Thetis equivalent. PsForm IS the PS control surface |

After retirement: Hardware tab count 9 → 8, Transmit sub-page count 4 → 3. Capability gating moves to `Tools > PureSignal...` menu visibility, `PureSignalApplet` visibility, `TxApplet [PS-A]` visibility, and `PsaIndicatorWidget` visibility. No regression vs. current state.

---

## 5. WDSP Layer

### 5.1 calcc.c port

**Source:** `Project Files/Source/wdsp/calcc.c` (1,164 LOC) + `calcc.h`.
**Destination:** `third_party/wdsp/calcc.c` (verbatim) + `third_party/wdsp/calcc.h` (verbatim).

The calcc state machine is a 10-state controller managing the calibration lifecycle:

| State | Meaning |
| --- | --- |
| `LRESET` | Reset state, clears coefficients |
| `LWAIT` | Wait for MOX |
| `LMOXDELAY` | Settle delay after MOX engaged |
| `LSETUP` | Initialize collection buffers |
| `LCOLLECT` | Sample-collection phase (amplitude-binned) |
| `MOXCHECK` | Verify MOX still active |
| `LCALC` | Calculate correction coefficients (cubic Hermite spline fit) |
| `LDELAY` | Inter-iteration delay |
| `LSTAYON` | Maintain corrections, monitor for drift |
| `LTURNON` | Activate corrections in iqc |

Key sub-features inside calcc:
- **Amplitude-binned sample collection**: `ints` intervals × `spi` samples each.
- **Watchdogs**: 4-second collection watchdog, IQC dog count watchdog (count ≥ 6 → reset).
- **Correction calculation thread**: `doPSCalcCorrection` invoked via semaphore signal.
- **Spline fit**: piecewise cubic Hermite for magnitude / I / Q correction curves.
- **Validation**: `scheck()` requires no NaN, no zeros, values < 1.07, no sudden jumps > 0.05.
- **Stabilize mode**: alpha-blend new coefficients with old (smooth transitions).
- **Pin mode**: clamp corrections at amplitude extremes.
- **Map mode**: convex envelope binning.

**Port discipline:** verbatim C99 source, byte-for-byte license header from upstream, every inline `//MW0LGE` / `//W2PA` / `//DH1KLM` etc. tag preserved within ±5 lines, every `// From Thetis ...` cite carries `[v2.10.3.13]`. No reformatting, no "improvements".

### 5.2 iqc.c port

**Source:** `Project Files/Source/wdsp/iqc.c` (315 LOC) + `iqc.h`.
**Destination:** `third_party/wdsp/iqc.c` + `third_party/wdsp/iqc.h`.

Real-time correction stage that runs on every TX sample inside `xtxa()`:
- Math: `PRE_I = ym * (I*yc - Q*ys)`, `PRE_Q = ym * (I*ys + Q*yc)` where `ym/yc/ys` are the magnitude/cosine/sine correction coefficients.
- Double-buffered coefficient sets with cosine crossfade on swap.
- States: `RUN` (steady-state), `BEGIN` (fade-in), `SWAP` (crossfade between coefficient sets), `END` (fade-out), `DONE` (bypass mode).

`xiqc()` is called at `xtxa()` line 583 in `wdsp/TXA.c:557-591`.

### 5.3 License header preservation

Both `calcc.c` and `iqc.c` carry their original Thetis license headers byte-for-byte (GPLv2-or-later, original-author copyright lines, Samphire dual-licensing statement if applicable). Plus a NereusSDR "Modification history" trailing block per `docs/attribution/HOW-TO-PORT.md` template. Failure to preserve license headers blocks the PR (CI-enforced via `scripts/verify-thetis-headers.py`).

---

## 6. Channel Wrappers

### 6.1 TxChannel PS setters (15)

Existing `TxChannel.{h,cpp}` gains 15 PS setter methods, each a thin C++ wrapper around the corresponding `WDSP::SetPS*` extern call:

| Setter | Thetis cite | WDSP entry |
| --- | --- | --- |
| `setPSControl(int reset, int mancal, int automode, int turnon)` | `cmaster.cs:530-535` | `SetPSControl` |
| `setPSMox(bool mox)` | -- | `SetPSMox` |
| `setPSReset(bool reset)` | -- | `SetPSReset` |
| `setPSMancal(bool mancal)` | -- | `SetPSMancal` |
| `setPSAutomode(bool automode)` | -- | `SetPSAutomode` |
| `setPSTurnon(bool turnon)` | -- | `SetPSTurnon` |
| `setPSMoxDelay(double delay)` | `calcc.c:982` | `SetPSMoxDelay` |
| `setPSLoopDelay(double delay)` | `calcc.c:971` | `SetPSLoopDelay` |
| `setPSTXDelay(double delay)` returns double | `calcc.c:993` | `SetPSTXDelay` |
| `setPSHWPeak(double peak)` | `calcc.c:1016` | `SetPSHWPeak` |
| `setPSPtol(double ptol)` | `calcc.c:1042` | `SetPSPtol` |
| `setPSPinMode(bool pin)` | -- | `SetPSPinMode` |
| `setPSMapMode(bool map)` | -- | `SetPSMapMode` |
| `setPSStabilize(bool stbl)` | -- | `SetPSStabilize` |
| `setPSIntsAndSpi(int ints, int spi)` | -- | `SetPSIntsAndSpi` |

Plus 4 readers:
- `getPSInfo()` returns int[16] (state + counters)
- `getPSHWPeak()` returns double
- `getPSMaxTX()` returns double
- `getPSDisp(int idx)` returns float[N] (display buffer for AmpView)

All methods are board-agnostic. The exact parameter ranges, defaults, and validation come from Thetis source. TBD-verify each one at plan-write per `feedback_subagent_thetis_source_first.md`.

### 6.2 PsFeedbackChannel

New class `src/core/PsFeedbackChannel.{h,cpp}` wraps the WDSP feedback RX channel id (likely channel 5 per existing `wdsp-integration.md` §6 sketch). Manages:
- Channel lifecycle (open/close)
- Sample-rate configuration (matches per-board `psSampleRate`)
- Routing samples from `RadioConnection` into the channel buffer

The class doesn't implement DSP; it's a thin holder. WDSP's autonomous calcc state machine reads samples from this channel via `pscc()`.

### 6.3 SetPSRxIdx / SetPSTxIdx (verbatim from cmaster.cs:533-534)

```cpp
// From Thetis cmaster.cs:533-534 [v2.10.3.13]
// (Same in mi0bot-Thetis [v2.10.3.13-beta2])
WDSP::SetPSRxIdx(0, 0);   // txid=0, all current models use Stream0 for RX feedback
WDSP::SetPSTxIdx(0, 1);   // txid=0, all current models use Stream1 for TX feedback
```

Called once at PureSignal initialization for every board. The "all current models" comment is unchanged in mi0bot, confirming HL2 follows the same convention.

---

## 7. Per-Board Codec PS DDC Config

### 7.1 New virtual method on per-board codecs

```cpp
struct PsDdcConfig {
    uint8_t cntrl1;
    uint8_t cntrl2;
    uint32_t rate[8];
    uint8_t ddcEnable;
    uint8_t syncEnable;
    int p1DdcConfig;   // P1-only board-config code
    int rxCount;       // P1-only RX count for state-machine
    int nDdc;
};

// Virtual method on every per-board codec:
virtual PsDdcConfig applyPureSignalDdcConfig(
    bool psEnabled,
    bool diversityEnabled,
    bool moxState,
    int rx1Rate,
    int rx2Rate,
    bool rx2Enabled
) const = 0;
```

### 7.2 Per-codec implementations (verbatim from console.cs:8186-8538 ramdor + mi0bot HL2 deltas)

| Codec | PS-on, no diversity, MOX | PS-on, with diversity, MOX | Source |
| --- | --- | --- | --- |
| `P2CodecOrionMkII` (and ANAN-100D / 200D / 7000DLE / 8000DLE / G2 / G2-1K / ANVELINAPRO3) | DDC0+DDC2, sync DDC1, rate=ps_rate=192000, cntrl1=`(rx_adc_ctrl1 & 0xf3) \| 0x08`, cntrl2=`rx_adc_ctrl2 & 0x3f`, P1DDCConfig=3 | DDC0+DDC2, sync DDC1, same as above, P1DDCConfig=3 | ramdor `console.cs:8264-8290` |
| `P1CodecHermesII` (and ANAN-10E / 100B) | TBD (verify at plan-write from `console.cs:8453+` ramdor) | TBD | ramdor `console.cs:8453+` |
| `P1CodecHl2` | DDC0, sync DDC1, rate=rx1_rate (HL2 can run any rate), cntrl1=4, cntrl2=0, P1DDCConfig=6 | (HL2 doesn't support diversity) | mi0bot `console.cs:8472-8488` |
| `P1CodecStandard` (Atlas / classic Hermes / Angelia / classic Orion / Hermes-Lite v1) | (no PS; caps gate hides UI) | -- | n/a |

### 7.3 ReceiverManager UpdateDDCs port

`ReceiverManager.{h,cpp}` gains a `updateDdcAssignment(...)` method ported verbatim from Thetis `console.cs:UpdateDDCs` (lines 8186-8538 ramdor / 8214-8560 mi0bot). The method dispatches per-board to the codec layer's `applyPureSignalDdcConfig()`, then writes the resulting `PsDdcConfig` to wire bytes via the existing `RadioConnection` send path.

---

## 8. Host Coordinator (PureSignal class)

### 8.1 Class skeleton

```cpp
// src/core/PureSignal.h
class PureSignal : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool autoCalEnabled READ isAutoCalEnabled WRITE setAutoCalEnabled NOTIFY autoCalEnabledChanged)
    Q_PROPERTY(bool correctionsApplied READ correctionsBeingApplied NOTIFY correctingChanged)
    Q_PROPERTY(int feedbackLevel READ feedbackLevel NOTIFY feedbackLevelChanged)
    Q_PROPERTY(int calibrationCount READ calibrationCount NOTIFY calibrationCountChanged)
    Q_PROPERTY(QColor feedbackColour READ feedbackColour NOTIFY feedbackColourChanged)
    Q_PROPERTY(bool invertRedBlue READ invertRedBlue WRITE setInvertRedBlue NOTIFY invertRedBlueChanged)

public:
    explicit PureSignal(WdspEngine* engine, TxChannel* tx, PsFeedbackChannel* fb, MoxController* mox, QObject* parent = nullptr);
    ~PureSignal() override;

    // Cal lifecycle
    void singleCalibrate();
    void setAutoCalEnabled(bool on);
    void forcePS();                          // ports console.cs ForcePS
    void forceAutoCalDisable();              // ports console.cs:43703 ForcePureSignalAutoCalDisable
    void reset();                            // OFF button
    void setDefaultPeaks();                  // ports SetDefaultPeaks

    // Save / Restore coefficients
    bool saveCorrections(const QString& filename);
    bool restoreCorrections(const QString& filename);

    // Two-tone test integration
    void setTwoToneOn(bool on);

    // Status reads (poll-driven)
    int feedbackLevel() const { return m_feedbackLevel.load(); }
    bool correctionsBeingApplied() const { return m_correctionsApplied.load(); }
    int calibrationCount() const { return m_calCount.load(); }
    QColor feedbackColour() const;

signals:
    void enabledChanged(bool);
    void autoCalEnabledChanged(bool);
    void correctingChanged(bool);
    void feedbackLevelChanged(int);
    void calibrationCountChanged(int);
    void feedbackColourChanged(QColor);
    void invertRedBlueChanged(bool);
    void calibrationStarted();
    void calibrationComplete(bool success);

private:
    // Auto-attention sub-state machine (target FB 128-181 of 256)
    void autoAttentionTick();

    // Polling timer (~100 ms, mirrors Thetis PSForm timer1code)
    void pollTimerTick();

    WdspEngine* m_engine;
    TxChannel* m_tx;
    PsFeedbackChannel* m_fb;
    MoxController* m_mox;

    QTimer m_pollTimer;
    QTimer m_autoAttTimer;

    std::atomic<int> m_feedbackLevel{0};
    std::atomic<bool> m_correctionsApplied{false};
    std::atomic<int> m_calCount{0};
    bool m_invertRedBlue{false};
    bool m_hideFeedback{false};
    // ... other Q_PROPERTY backers ...
};
```

### 8.2 Auto-attention sub-state machine

Ported from Thetis (need TBD-cite at plan-write for the `eAAState` machine). Monitors feedback level at 100 ms intervals during MOX. If level outside 128-181 of 256:
- Pauses calcc cal collection
- Adjusts `SetupForm.ATTOnTX` via the existing `StepAttenuatorController` (3G-13)
- Resumes cal once level back in range

The existing NereusSDR `StepAttenuatorController` + `TransmitModel::isPureSignalActiveForTest()` predicate seam (already wired) means PS plugs straight into the existing TX-side step-att / ATT-on-TX chain. **No step-att rework needed**; just flip the predicate from test-stub to live PureSignal state.

### 8.3 MOX integration

`PureSignal` listens to `MoxController::moxChanged(bool)` and:
- On MOX up: sets `SetPSMox(true)` so calcc enters its TX-aware state.
- On MOX down: sets `SetPSMox(false)`; calcc transitions to `LSTAYON` or `LWAIT`.

The PS pump cycles with TX state. No per-buffer host involvement.

### 8.4 Applet button wiring (PureSignalApplet + TxApplet [PS-A])

Two right-side applet surfaces drive PureSignal: the dedicated **PureSignalApplet** (NereusSDR-native quick-access, not a Thetis port) and the **[PS-A] toggle on TxApplet** (source-first port of Thetis `chkFWCATUBypass`).

#### 8.4.1 TxApplet [PS-A] toggle (`m_psaBtn`)

Existing scaffolding at `src/gui/applets/TxApplet.cpp:735-738`. Currently hidden until 3M-4 lands. Wiring at 3M-4:

| Gesture | Behavior | Source |
| --- | --- | --- |
| **Left-click** (toggle) | `PureSignal::setAutoCalEnabled(checked)` plus the side-effect chain: `infoBar.PSAEnabled` update, ATT-on-TX un-stash on disable, `PSAChangedHandlers` invoke, `AndromedaIndicatorCheck` front-panel update | Thetis `chkFWCATUBypass_Click`, `console.cs:36762` and the un-stash logic in the same handler around `console.cs:43707-43730` |
| **Right-click** | Opens PsForm (`Tools > PureSignal...`). Implemented via `setContextMenuPolicy(Qt::CustomContextMenu)` + signal connect to `MainWindow::openPureSignalDialog()` | Thetis `chkFWCATUBypass_MouseDown`, `console.cs:46149-46152` (`if (IsRightButton(e)) linearityToolStripMenuItem_Click(null, EventArgs.Empty);`) |
| Hover | Tooltip "Toggle PureSignal auto-calibration. Right-click to open PureSignal..." | NereusSDR-native tooltip wording, ports the meaning |

Visibility gated by `caps.hasPureSignal`. State driven by `PureSignal::autoCalEnabledChanged` signal.

#### 8.4.2 PureSignalApplet (NereusSDR-native quick-access)

Existing scaffolding at `src/gui/applets/PureSignalApplet.{h,cpp}`. 7 buttons, 1 FB level gauge, 1 correction gauge, 3 status LEDs, 3 info labels (per `PureSignalApplet.h:81-118`). NereusSDR-native; no direct Thetis equivalent (Thetis exposes everything through PsForm only). Wiring at 3M-4:

| Control | Left-click behavior | Right-click behavior | State binding |
| --- | --- | --- | --- |
| **Calibrate** (`m_calibrateBtn`) | `PureSignal::singleCalibrate()` (one-shot cal) | Opens PsForm (consistent with Thetis right-click-to-config pattern) | -- |
| **Auto** (`m_autoCalBtn`, green toggle) | Toggles `PureSignal::setAutoCalEnabled(checked)`. Mirrors TxApplet [PS-A] state | Opens PsForm | Bidirectional with TxApplet [PS-A] state via `PureSignal::autoCalEnabledChanged` |
| **FB Level gauge** (`m_feedbackGauge`) | (passive readout) | Opens PsForm | Driven by `PureSignal::feedbackLevelChanged(int)` 0..255 mapped to 0..100 gauge range, color zones at 70 (yellow) / 90 (red) |
| **Correction gauge** (`m_correctionGauge`) | (passive readout) | Opens PsForm | Driven by `PureSignal::correctionMagnitudeChanged(int)` 0..100, color zones at 80 / 95 |
| **Save** (`m_saveBtn`) | Triggers `QFileDialog::getSaveFileName` then `PureSignal::saveCorrections(filename)`. Gated on `correctionsBeingApplied=true` (matches Thetis `PSForm.cs:524-530, 576-590`) | Opens PsForm | Enabled state from `PureSignal::correctingChanged(bool)` |
| **Restore** (`m_restoreBtn`) | Triggers `QFileDialog::getOpenFileName` then `PureSignal::restoreCorrections(filename)` | Opens PsForm | Always enabled when PS enabled |
| **2-Tone** (`m_twoToneBtn`, green toggle) | `PureSignal::setTwoToneOn(checked)` (delegates to existing `TwoToneController`) | Opens PsForm | Mirrors PsForm's `btnPSTwoToneGen` state |
| **Cal LED** (`m_led[0]`) | -- | -- | Active during `LSETUP`/`LCOLLECT`/`LCALC` calcc states (driven by `PureSignal::calStateChanged`) |
| **Run LED** (`m_led[1]`) | -- | -- | Active during `LSTAYON` (corrections applied + monitoring) |
| **Fbk LED** (`m_led[2]`) | -- | -- | Active when feedback samples flowing (driven by `PsFeedbackChannel` activity) |
| **Iterations** info | (text) | -- | `puresignal.CalibrationCount` from `GetPSInfo[5]` |
| **Feedback dB** info | (text) | -- | dB-converted FB level (informational) |
| **Correction dB** info | (text) | -- | dB-converted correction peak (informational) |

**Right-click pattern unification**: every button on PureSignalApplet (including non-button controls like the gauges via per-widget `customContextMenuRequested`) has `setContextMenuPolicy(Qt::CustomContextMenu)` and routes to `MainWindow::openPureSignalDialog()`. This matches the Thetis right-click-to-associated-window pattern (cf. `chkTUN_MouseDown` opens Setup → Transmit, `chk2TONE_MouseDown` opens Setup → TEST, `chkExternalPA_MouseDown` opens Setup → OC, `chkFWCATUBypass_MouseDown` opens PsForm; all in `console.cs:46149+`).

Visibility of the PureSignalApplet itself is gated by `caps.hasPureSignal` (already in scaffolding via `NyiOverlay::markNyi(...)` calls, replaced with live wiring at 3M-4).

---

## 9. Persistence

### 9.1 Per-TX-profile

`Pure_Signal_Enabled` is a per-profile boolean column in the TX Profile DB. Already exists in NereusSDR's `MicProfileManager` (key index TBD; current count is 91 from 3M-3a-ii). Profile recall flips PS state via existing `console.PureSignalEnabled` equivalent. All 19+ stock factory profiles default to false. **No editor checkbox** in Setup → TX Profiles. Thetis doesn't have one either; state is implicit-via-profile-mechanism (saved from current state at save, applied to current state at recall).

### 9.2 Per-MAC AppSettings

Under `hardware/<mac>/puresignal/` namespace:

| Key | Type | Default | Source |
| --- | --- | --- | --- |
| `enabled` | bool | false | (per-profile actually; redundant; TBD) |
| `autoCalEnabled` | bool | false | -- |
| `moxDelay` | double | TBD | `udPSMoxDelay` |
| `calDelay` | double | TBD | `udPS????` (CAL Wait spinbox) |
| `ampDelay` | int | 0 | `udPSPhnum` |
| `pinMode` | bool | false | `chkPSPin` |
| `mapMode` | bool | false | `chkPSMap` |
| `stabilize` | bool | false | `chkPSStbl` |
| `autoAttenuate` | bool | true | `chkPSAutoAttenuate` |
| `relaxTolerance` | bool | false | `chkPSRelaxPtol` |
| `quickAttenuate` | bool | false | `chkQuickAttenuate` |
| `tint` | double | 0.5 | `comboPSTint` |
| `loopback` | bool | false | `checkLoopback` |
| `alwaysOnTop` | bool | false | `chkPSOnTop` |
| `show2ToneMeasurements` | bool | false | `chkShow2ToneMeasurements` |
| `hideFeedback` | bool | false | `chkHideFeebackLevel` |
| `invertRedBlue` | bool | false | `chkSwapREDBluePSAColours` |
| `ampViewOnTop` | bool | false | `chkStayOnTop` (AmpView) |
| `ampViewShowGain` | bool | false | `chkAVShowGain` |
| `ampViewPhaseZoom` | bool | false | `chkAVPhaseZoom` |
| `ampViewLowRes` | bool | false | `chkAVLowRes` |

Default values for `moxDelay` / `calDelay` / `ampDelay` are TBD-verify at plan-write from Thetis source.

### 9.3 Calibration coefficients (user-saved files, NOT auto-persisted)

PsForm Save/Restore buttons → `QFileDialog::getSaveFileName` / `getOpenFileName`. Default folder: `~/.config/NereusSDR/PureSignal/`. WDSP entry points: `puresignal.PSSaveCorr(txachannel, filename)` / `puresignal.PSRestoreCorr(txachannel, filename)`. File format is whatever WDSP writes (binary blob, TBD-verify at plan-write).

Save button enabled only when `CorrectionsBeingApplied=true`. After Restore, `_restoreON=true` flag is set so calcc skips cal-from-scratch and uses loaded peaks.

### 9.4 Per-board defaults

`BoardCapabilities::psDefaultPeak` (new field, one float per board model). Ports Thetis `HardwareSpecific.PSDefaultPeak`. PsForm shows a warning indicator (`pbWarningSetPk` per `PSForm.cs:802`) when current SetPk drifts from this default.

### 9.5 Coefficients are NOT per-band

Thetis maintains one calibration set per `_txachannel`; calcc adapts across band changes implicitly via the running TX frequency. NereusSDR matches: no per-band coefficient storage.

---

## 10. Capabilities

### 10.1 BoardCapabilities additions

| Field | Purpose | Existing? |
| --- | --- | --- |
| `hasPureSignal` (bool) | Master gate for all PS UI surfaces | Yes (existing) |
| `psDefaultPeak` (float) | Per-board default for SetPk | New |
| `psSampleRate` (int) | Sample rate during MOX+PS (192000 for G2-class, rx1_rate for HL2) | New |

The `psSampleRate` field is consumed by the per-board codec's `applyPureSignalDdcConfig()`. For G2-class boards the value is the static `cmaster.PSrate=192000`; for HL2 it's the radio's current `rx1_rate`.

**No `feedbackPathStyle` flag.** Both styles look dual-stream from the WDSP layer (per §3.5). Per-board codec handles the divergence internally.

### 10.2 Capability gating fan-out

When `caps.hasPureSignal=false`:
- `Tools > PureSignal...` menu item hidden
- `PureSignalApplet` hidden in applet picker
- `TxApplet [PS-A]` button hidden
- `PsaIndicatorWidget` hidden in bottom banner
- Setup → Antenna → Alex-1 Filters "HPF Bypass on PureSignal feedback" checkbox hidden (already gated in 3P-I)
- Setup → General → Options "Hide feedback level" + "Swap red and blue PS-A feedback colours" checkboxes hidden

---

## 11. Verification Plan

### 11.1 Bench setup (required for merge)

| Hardware | Purpose |
| --- | --- |
| ANAN-G2 (or G2-1K, 7000DLE, 8000DLE) | Primary G2-class verification |
| Hermes Lite 2 (mi0bot firmware) | HL2-specific path verification |
| Dummy load with thermal capacity for steady TX | Avoids antenna issues during cal |
| Spectrum analyzer OR second SDR receiver | IMD3/IMD5 measurement validation |
| Power meter (optional) | Output power validation |

### 11.2 Acceptance checklist (15 surfaces)

Every surface must demonstrate **full UI surface usability** at land. Not scaffolded-but-disabled, not model-wired-but-not-wire-format-correct.

1. **PsForm** opens from Tools menu, all 23 controls render, Advanced toggle collapses to compact strip and back.
2. **AmpView** opens from PsForm `btnPSAmpView`, 5 chart series render with mock data, 4 toolbar checkboxes drive their behaviors.
3. **Tools > PureSignal menu** capability-gated correctly per board.
4. **TxApplet [PS-A]** toggles AutoCal, updates infoBar, manages ATT-on-TX un-stash on disable.
5. **PsaIndicatorWidget** transitions through 6 states correctly (PS off / idle / under / marginal / in-range / over) with FB numeric and color encoding.
6. **HPF Bypass on PureSignal feedback**: toggling un-checks shows IMD warning dialog; Cancel reverts.
7. **IMD warning dialog** message text matches Thetis verbatim.
8. **Hide feedback level** toggle from Setup updates banner; right-click on FB also flips it; both stay in sync.
9. **Swap red/blue** toggle from Setup updates banner colors; left-click on FB also flips it; both stay in sync.
10. **TX Profile recall** flips `Pure_Signal_Enabled` correctly.
11. **Save/Restore coefficients** writes/reads files; Save gated on CorrectionsBeingApplied.
12. **Two-tone test** activates TwoToneController, fundamentals visible on spectrum.
13. **IMD overlay** renders box at correct position, peak markers on fundamentals/IMD3/IMD5, EMA-smoothed numeric readouts (f0 L/U + IMD3 L/U + IMD5 L/U + worst IMD3 dBc + worst IMD5 dBc + OIP3 + OIP5).
14. **ForcePureSignalAutoCalDisable()** invoked on sample-rate change correctly stops cal.
15. **FB-label clicks**: left-click swaps red/blue, right-click hides numeric, hover shows correct tooltip.

### 11.3 DSP correctness (bench-validated)

- Two-tone test on G2 with PA into dummy load.
- Without PS: measure IMD3 baseline (typical 25-35 dBc below fundamentals).
- Enable PS, run single calibration, verify FB level enters 129-181 range.
- Verify "Correcting" indicator activates (PS label Lime).
- Re-measure IMD3: should improve by 10-25 dB depending on PA characteristics.
- Repeat on HL2 with mi0bot firmware.

### 11.4 Auto-attention regression

- Set ATT manually too high: PS auto-att pulls it down until FB enters target range.
- Set ATT manually too low: PS auto-att pushes it up.
- Disable AutoCal: ATT-on-TX un-stash logic from `chkFWCATUBypass_CheckedChanged` runs correctly.

---

## 12. Sub-PR Commit Boundaries

Single PR per Q1, with logical commit boundaries that map to natural seams. Approximate commit list (~15 commits):

| # | Commit | Files | Rationale |
| --- | --- | --- | --- |
| 1 | `feat(caps): add psDefaultPeak + psSampleRate to BoardCapabilities` | `BoardCapabilities.{h,cpp}` | Foundation; per-board cap data |
| 2 | `chore(wdsp): vendor calcc.c + iqc.c verbatim from Thetis` | `third_party/wdsp/calcc.{c,h}`, `third_party/wdsp/iqc.{c,h}`, build wiring | License headers byte-for-byte; verifiers green |
| 3 | `feat(tx-channel): add 15 PS setters + 4 readers wrapping WDSP API` | `TxChannel.{h,cpp}` | Board-agnostic wrapper; tested against WDSP |
| 4 | `feat(core): new PsFeedbackChannel class for the feedback RX channel` | `PsFeedbackChannel.{h,cpp}` | Holds the WDSP feedback channel id |
| 5 | `feat(codec): add applyPureSignalDdcConfig to per-board codecs` | `P2CodecOrionMkII.cpp`, `P2CodecG2.cpp` (new), `P1CodecHl2.cpp`, `P1CodecHermesII.cpp` (new), `P1CodecStandard.cpp` | Per-board PS DDC config from Thetis switch verbatim |
| 6 | `feat(receiver-manager): port UpdateDDCs PS branch from console.cs:8186-8538` | `ReceiverManager.{h,cpp}` | Uses per-board codec output |
| 7 | `feat(core): PureSignal coordinator class (cal lifecycle + MOX + auto-att)` | `PureSignal.{h,cpp}` | Host-side controller |
| 8 | `feat(gui): port PsForm modeless dialog (Tools > PureSignal)` | `PsForm.{h,cpp}`, MainWindow menu wiring | All 23 controls wired |
| 9 | `feat(gui): port AmpView modeless dialog opened from PsForm btnPSAmpView` | `AmpViewWindow.{h,cpp}` | 5-series chart + 4 toolbar checkboxes |
| 10 | `feat(gui): PsaIndicatorWidget in bottom banner with 6-state machine + click handlers` | `PsaIndicatorWidget.{h,cpp}`, MainWindow bottom-banner wiring | Includes left/right-click + tooltip |
| 11 | `feat(setup): wire HPF Bypass on PS warning dialog + General Options 2 PS checkboxes` | `AntennaAlexAlex1Tab.cpp`, `GeneralOptionsPage.cpp` | Adds chkHideFeebackLevel + chkSwapREDBluePSAColours + IMD warning |
| 12 | `feat(spectrum): two-tone IMD overlay with peak markers + readout box` | `SpectrumWidget.{h,cpp}`, possibly new overlay class | Coordinates with carson-branch peak-blob work |
| 13 | `feat(applet): wire PureSignalApplet + TxApplet [PS-A] to live PureSignal state` | `PureSignalApplet.cpp`, `TxApplet.cpp` | Existing scaffolding gets live wires |
| 14 | `chore(setup): retire Setup → Hardware → PureSignal tab + Setup → Transmit → PureSignal page` | `HardwarePage.{h,cpp}`, `TransmitSetupPages.{h,cpp}`, delete `PureSignalTab.{h,cpp}` | No Thetis equivalent; cleanup |
| 15 | `test(puresignal): unit tests + verification matrix` | `tests/tst_puresignal_*.cpp`, `docs/architecture/phase3m-4-verification.md` | Bench checklist + deterministic unit coverage |

Each commit must:
- Pass `ctest` for new tests
- Pass `scripts/verify-thetis-headers.py` (if new ports)
- Pass `scripts/verify-inline-tag-preservation.py` (if new ports)
- Be GPG-signed
- Compile cleanly on Linux + macOS (Windows TBD-verify)

---

## 13. Coordination Dependencies

### 13.1 carson-branch peak-blob infrastructure (#13 IMD overlay)

The two-tone IMD overlay depends on peak-detection infrastructure currently in flight on `claude/wizardly-carson-a79b0e` (worktree at `/Users/j.j.boyd/NereusSDR/.claude/worktrees/wizardly-carson-a79b0e`). Thetis `display.cs:5238` literally has:

```csharp
bool peaks_imds = bPeakBlobs || show_imd_measurements;
```

Peak-blobs and IMD measurements share the same peak-detection code path. Same `processMaximums()` callsite is used for both.

**Coordination plan:**
- If carson-branch lands first: 3M-4 #13 builds on top, reuses the peak-detection plumbing.
- If 3M-4 lands first: 3M-4 ports its own peak-detection; carson-branch later refactors to share.
- Either way, the peak-detection algorithm is the SAME (Thetis `display.cs:5283-5298`); duplication risk is low.
- At plan-write, status of carson-branch determines which order. Both branches port the same upstream; merge is a coordination not a duplication.

### 13.2 Phase 3F multi-panadapter (downstream)

Phase 3F multi-panadapter is **blocked on 3M-4** landing the UpdateDDCs PS state machine. Per `docs/architecture/phase3f-multi-panadapter-plan.md`: "UpdateDDCs() port must include ALL state machine cases from Thetis console.cs:8186-8538, including PureSignal DDC states." 3M-4 must satisfy 3F's UpdateDDCs requirements; commit #6 closes that gap.

### 13.3 Existing 3G-13 step-attenuator integration (already wired)

`StepAttenuatorController` (3G-13) and `TransmitModel::isPureSignalActiveForTest()` predicate seam are pre-staged. Commit #7 (PureSignal coordinator) wires the predicate from test-stub to live; no step-att rework needed.

### 13.4 Existing 3I `BoardCapabilities::hasPureSignal` (already wired)

The capability flag exists from 3I work. 3M-4 just consumes it; no new infrastructure.

### 13.5 Existing 3P-I-a/b "HPF Bypass on PureSignal feedback" Setup checkbox (already exists)

Built during 3P-I-a/b. Commit #11 adds the warning dialog wire and the live-PureSignal hook; the checkbox UI is already in place.

---

## 14. Reconciliation Appendix (supersedes recap)

This design supersedes prior plans on the following points. All other prior content remains valid.

| Prior plan | Section | Old | New (this design) | Reason |
| --- | --- | --- | --- | --- |
| `phase3m-tx-epic-master-design.md` | §8.1 | "Menu → DSP → PureSignal opens PSForm modeless dialog" | **Tools > PureSignal...** | User express call; PS is more discoverable than DSP-buried |
| `phase3m-tx-epic-master-design.md` | §8.1 | "Setup → Transmit → PureSignal mirrors PSForm" | **Retired** | No Thetis equivalent; PsForm IS the PS surface |
| `phase3m-tx-epic-master-design.md` | §8.1/§8.2 | 3M-4a (PSForm + calcc + iqc) + 3M-4b (AmpView) split into separate phases | **Single PR with commit boundaries** | Q1 user call: one PR; sub-PR seams become commits |
| `phase3m-tx-epic-master-design.md` | §8.2 | "Menu → Tools → AmpView opens modeless dialog" as separate menu entry | **Reached from inside PsForm via btnPSAmpView** | Source-first: Thetis pattern; AmpView lifecycle managed by PsForm |
| `phase3m-tx-epic-master-design.md` | §8.1 | (HL2 not enumerated in PS scope) | **HL2 included via mi0bot fork** | mi0bot has HL2 PS deltas in PSForm.cs (NeedToRecalibrate_HL2) |
| `reviews/2026-04-10-plan-review.md` | §3I-4 | Menu placement DSP / Tools (general) | **Tools > PureSignal...** | Specific user call |
| `wdsp-integration.md` | §6 | `PureSignal::processFeedback()` per-buffer host-driven pump | **Configuration + monitoring layer; pscc autonomous in WDSP** | Source-verified: pscc has zero callsites in `Source/Console/`; WDSP runs cal autonomously via xputbuf chain |
| `wdsp-integration.md` | §6 | `PureSignal` updates CFCOMP profile via `SetTXACFCOMPprofile` | **iqc applies correction directly inside xtxa() at TXA.c:583** | Source-verified: iqc is its own xtxa stage, not a CFCOMP profile injection |
| `phase3i-radio-connector-port-design.md` | Hardware tab | "Setup → Hardware → PureSignal tab (PS Enable + feedback source select)" | **Retired** | No Thetis equivalent; PsForm covers everything |
| `ui-feature-mapping.md` | Feature 15 menu mapping | "DSP > PureSignal + DSP > AmpView (floating)" | **Tools > PureSignal opens PsForm; AmpView reached from inside PsForm** | Source-first |
| (new) | -- | (no `feedbackPathStyle` cap concept existed) | **No flag needed; per-board codec handles register/rate divergence** | Source-verified: HL2 looks dual-stream from host; divergence is register-level |
| (new) | -- | "LinearityForm" mistakenly considered as a 3rd separate sub-form | **No LinearityForm exists** | `ShowAtStartup_LinearityForm` is just a startup-restoration helper for PsForm |

---

## 15. Open Questions / TBDs (resolve at plan-write)

These items are not blocking the design but need source-verified resolution before the implementation plan finalizes:

1. **Default values** for `udPSMoxDelay`, `udPS????` (CAL Wait), `udPSPhnum`: verify from Thetis source.
2. **`comboPSTint` value list**: Thetis shows "0.5" default; full option list TBD.
3. **`puresignal.PSSaveCorr` / `PSRestoreCorr` file format**: binary blob layout TBD-verify in WDSP source.
4. **`HermesII` PS DDC config**: `console.cs:8453+` ramdor branch needs full source-verify.
5. **Auto-attention sub-state machine** (`eAAState`): exact state list and timings TBD-verify.
6. **Thetis menu hierarchy** for `linearityToolStripMenuItem`: confirm parent menu (DSP / Tools / View).
7. **`_ema_*` smoothing constants**: alpha values for IMD readout EMA TBD-verify.
8. **`findImd()` algorithm**: exact peak-indexing logic TBD-verify in `display.cs`.
9. **Compact-fonts threshold**: when does ucInfoBar use `_useSmallFonts=true`? TBD-verify.
10. **AmpView `GetPSDisp` buffer indices**: what idx values to call? TBD-verify in `AmpView.cs`.
11. **MicProfileManager key index** for `Pure_Signal_Enabled`: slot into existing 91-key list.
12. **Appearance sub-tab** for `chkSwapREDBluePSAColours` originally: confirmed it's actually on `tpOptions2`, not Appearance. (Already corrected in this design.)
13. **carson-branch coordination order**: does that branch land first or does 3M-4? Determines #13 commit content.
14. **Chart rendering choice** for AmpView: Qt6 `QChart` vs custom `QPainter` widget. Plan-write decision.

---

## 16. References

### 16.1 Master plan + TX epic

- `docs/MASTER-PLAN.md` §3M-4 PureSignal PA Linearization
- `docs/architecture/phase3m-tx-epic-master-design.md` §8.1 + §8.2
- `docs/architecture/reviews/2026-04-10-plan-review.md` §3I-4 (deepest feature scope detail; mostly still valid)

### 16.2 Architecture context

- `docs/architecture/wdsp-integration.md` §6 (sketch; see §3.3 for corrections)
- `docs/architecture/phase3f-multi-panadapter-plan.md` (downstream-dependent)
- `docs/architecture/phase3i-radio-connector-port-design.md` (3I cap flag, Setup retirement)
- `docs/architecture/comprehensive-ui-port-design.md` (high-level UI port)
- `docs/architecture/ui-feature-mapping.md` (Feature 15)

### 16.3 Thetis source files (READ before porting)

| File | LOC | Role |
| --- | --- | --- |
| `Project Files/Source/Console/PSForm.cs` | 1,164 | Main PS form (calibration UI, state polling) |
| `Project Files/Source/Console/PSForm.designer.cs` | -- | Form layout (control positions) |
| `Project Files/Source/Console/AmpView.cs` | 528 | Correction curve viewer |
| `Project Files/Source/Console/AmpView.Designer.cs` | -- | AmpView layout |
| `Project Files/Source/Console/ucInfoBar.cs` | -- | Bottom banner FB+PS labels (lines 827-1098) |
| `Project Files/Source/Console/ucInfoBar.Designer.cs` | -- | InfoBar layout |
| `Project Files/Source/Console/cmaster.cs` | -- | WDSP DllImports (SetPS*) at lines 530-540, 143-147 |
| `Project Files/Source/Console/console.cs` | -- | UpdateDDCs at 8186-8538 (ramdor); psform.Show at 43099-43104; PS-A at 18605, 43707-43730 |
| `Project Files/Source/Console/setup.cs` | -- | chkDisableHPFonPSb warning at 29275-29295 |
| `Project Files/Source/Console/setup.designer.cs` | -- | chkDisableHPFonPSb at 23635, chkSwapREDBluePSAColours at 10572, chkHideFeebackLevel at 10571 |
| `Project Files/Source/Console/database.cs` | -- | Pure_Signal_Enabled per-profile at 4462 |
| `Project Files/Source/Console/display.cs` | -- | Two-tone IMD overlay at 5008, 5283-5298, 5453-5475, 5512-5560, 5520, 5650-5685 |
| `Project Files/Source/wdsp/calcc.c` | 1,164 | calcc state machine, pscc autonomous |
| `Project Files/Source/wdsp/calcc.h` | -- | calcc API |
| `Project Files/Source/wdsp/iqc.c` | 315 | Real-time IQ correction |
| `Project Files/Source/wdsp/iqc.h` | -- | iqc API |
| `Project Files/Source/wdsp/TXA.c` | -- | xiqc placement at line 583 (xtxa chain) |
| `Project Files/Source/wdsp/cmaster.c` | -- | WDSP-side cmaster (autonomous dispatch) |

### 16.4 mi0bot deltas (READ for HL2 work)

| File | Notes |
| --- | --- |
| `Project Files/Source/Console/PSForm.cs` | HL2 branches at 737-788, NeedToRecalibrate_HL2 at 1142+ |
| `Project Files/Source/Console/console.cs` | UpdateDDCs HL2 branch at 8409-8488; HL2 model checks elsewhere |
| `Project Files/Source/Console/cmaster.cs` | Confirms unchanged "all current models use Stream0" comment |

### 16.5 NereusSDR existing files affected

- `src/core/BoardCapabilities.{h,cpp}` (add psDefaultPeak + psSampleRate)
- `src/core/TxChannel.{h,cpp}` (add 15 setters + 4 readers)
- `src/core/PsFeedbackChannel.{h,cpp}` (new)
- `src/core/PureSignal.{h,cpp}` (new, ~300 LOC)
- `src/core/ReceiverManager.{h,cpp}` (port UpdateDDCs PS branch)
- `src/core/codec/P2CodecOrionMkII.{h,cpp}` (add applyPureSignalDdcConfig)
- `src/core/codec/P2CodecG2.{h,cpp}` (new; currently OrionMkII covers G2 too; verify at plan-write)
- `src/core/codec/P1CodecHl2.{h,cpp}` (add applyPureSignalDdcConfig with mi0bot deltas)
- `src/core/codec/P1CodecHermesII.{h,cpp}` (new; currently Standard covers HermesII; verify at plan-write)
- `src/core/codec/P1CodecStandard.{h,cpp}` (PS-not-supported stub)
- `src/gui/PsForm.{h,cpp}` (new)
- `src/gui/AmpViewWindow.{h,cpp}` (new)
- `src/gui/PsaIndicatorWidget.{h,cpp}` (new)
- `src/gui/MainWindow.{h,cpp}` (Tools menu entry, PsaIndicatorWidget wiring)
- `src/gui/applets/PureSignalApplet.{h,cpp}` (wire to live state)
- `src/gui/applets/TxApplet.{h,cpp}` (wire [PS-A] toggle to live state)
- `src/gui/setup/hardware/AntennaAlexAlex1Tab.cpp` (add IMD warning + live wire)
- `src/gui/setup/GeneralOptionsPage.{h,cpp}` (add 2 PS checkboxes)
- `src/gui/SpectrumWidget.{h,cpp}` (IMD overlay layer; coordinate with carson-branch)
- `src/gui/setup/HardwarePage.{h,cpp}` (RETIRE PureSignal tab entry)
- `src/gui/setup/TransmitSetupPages.{h,cpp}` (RETIRE PureSignalPage class)
- `src/gui/setup/hardware/PureSignalTab.{h,cpp}` (DELETE)
- `third_party/wdsp/calcc.c` + `calcc.h` (new, verbatim port)
- `third_party/wdsp/iqc.c` + `iqc.h` (new, verbatim port)
- `MicProfileManager` (add Pure_Signal_Enabled key)
- `tests/tst_puresignal_coordinator.cpp` (new)
- `tests/tst_puresignal_codec_ddc_config.cpp` (new)
- `tests/tst_puresignal_setters.cpp` (new)
- `tests/tst_psa_indicator_state_machine.cpp` (new)
- `tests/tst_imd_peak_detection.cpp` (new)
- `docs/architecture/phase3m-4-verification.md` (new bench matrix)

---

## 17. Approval

This design document was developed via interactive brainstorming session 2026-05-06 with source-first verification at every decision point. All 15 UI surfaces are source-cited; the architecture matches Thetis's actual structure (verified by reading upstream source, not inferred); both ramdor and mi0bot upstreams are integrated; the subagent dispatch protocol is set; coordination dependencies are identified.

**User ratifications during brainstorm:**
- Q1 (epic shape): single PR with commits as required
- Q2 (UI placement): Tools > PureSignal opens Thetis-faithful PsForm; AmpView + sub-features reached from inside PsForm exactly as Thetis
- Q3 (WDSP feedback architecture): board-agnostic single PS channel + per-board codec for register/rate divergence (no `feedbackPathStyle` cap flag)
- Q4 (persistence): per-TX-profile enabled, file Save/Restore for coefficients, AppSettings for scalars, per-board psDefaultPeak
- Q5 (slotting): 4 supersedes ratified (later expanded to 11 in §14)
- Setup drops (A1): pure source-first; drop both Setup pages cleanly, port only what Thetis has
- Comprehensive port directive: every Thetis PS surface must have a NereusSDR port equivalent
- IMD scope add (#13): two-tone IMD spectrum telemetry + full UI usability acceptance criterion
- PSA placement (option B): bottom-banner PSA pair after RxDashboard, before StationBlock
- FB-banner click behavior (#15): left/right-click handlers + dynamic tooltip

**Next step:** user reviews this spec; on approval, hand off to `superpowers:writing-plans` skill to produce the detailed implementation plan.

---

*Generated 2026-05-06 by J.J. Boyd (KG4VCF) with AI-assisted source-first brainstorming via Anthropic Claude Code.*
