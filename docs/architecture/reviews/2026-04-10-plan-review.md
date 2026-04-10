# NereusSDR Plan Review — 2026-04-10

**Author:** JJ Boyd ~KG4VCF, Co-Authored with Claude Code
**Scope:** Comprehensive review of all plans, architecture docs, and feature coverage
**Method:** Deep-dive into Thetis and AetherSDR codebases, cross-referenced against all NereusSDR planning documents

---

## 1. Review Summary

### What Was Reviewed
- `docs/MASTER-PLAN.md` (923 lines)
- `docs/project-brief.md`
- All 10 architecture docs in `docs/architecture/`
- Both implementation plans (`phase3d-*`, `phase3f-*`)
- Thetis codebase: `console.cs`, `cmaster.cs`, `radio.cs`, `wdsp.cs`, `PSForm.cs`, `ucMeter.cs`, `MeterManager.cs`, `frmMeterDisplay.cs`, `audio.cs`, `sidetone.c`, `calcc.c`, `iqc.c`, `TXA.c`, `display.cs`, `specHPSDR.cs`
- AetherSDR codebase: full `src/gui/`, `src/models/`, `src/core/` directory audit

### Key Findings
1. **5 incorrect WDSP default values** in `wdsp-integration.md` (code already fixed through debugging)
2. **TX pipeline drastically underscoped** — 3 bullet points for an 18-stage WDSP chain + CW subsystem + MOX state machine
3. **Container system needs architectural commitment** — decided on full Thetis-style composability with GPU rendering
4. **PureSignal is entirely client-side** for OpenHPSDR radios (no AetherSDR reference — pure Thetis port)
5. **~20 Thetis features missing** from the plan — all triaged below
6. **AetherSDR architectural assumptions confirmed correct** — QSplitter multi-pan, wirePanadapter(), AppSettings, threading model

---

## 2. Corrections Required in Existing Docs

### 2.1 wdsp-integration.md — Wrong Default Values

Code is already fixed through debugging. Doc needs updating to match.

| Parameter | Doc says | Thetis actual | Source |
|---|---|---|---|
| AGC top/MaxGain | 80.0 | **90.0** | `radio.cs:1018` |
| NR adapt rate | 0.0001 | **0.0016** | `radio.cs:678` |
| NR leak | 0.0001 | **1e-7** | `radio.cs:680` |
| NB1 threshold | 20.0 | **3.3** (radio.cs) / **30.0** (cmaster.c) | `radio.cs:843`, `cmaster.c:53` |
| Filter low/high | -2850/-150 | **Mode-dependent presets** | `console.cs:5180-5273` |

Filter defaults are mode-dependent preset tables (e.g. USB F5 = +100/+3000, LSB F5 = -3000/-100). No single default pair.

NB1/NB2 additional defaults from Thetis:
- NB1: tau=0.00005, hangtime=0.00005, advtime=0.00005, backtau=0.05
- NB2: tau=0.0001, advtime=0.0001, hangslewtime=0.0001, hangtime=0.0001, max_imp_seq_time=0.025, backtau=0.05, threshold=30.0

`xanbEXTF`/`xnobEXTF` take **separate float* I and float* Q** arguments (not interleaved). They interleave internally. Thetis calls them once per buffer with two separate arrays.

### 2.2 radio-abstraction.md — Stale Class Diagram

Section 11 class diagram still shows `QTcpSocket (cmd) + QUdpSocket (data)` for P2RadioConnection. P2 is UDP-only with a single socket (confirmed in Phase 3A). Update diagram.

### 2.3 radio-abstraction.md — hasPureSignal Too Broad

`hasPureSignal = (boardType != Metis)` is wrong for Hermes (single ADC, no feedback path). Should check for boards with 2+ ADCs.

### 2.4 radio-abstraction.md — sendCmdTx() Missing

P2 port map lists port 1026 for TX mic/CW/keyer data but P2RadioConnection interface doesn't include `sendCmdTx()`. Needed for Phase 3I.

### 2.5 MASTER-PLAN.md — Panel Count Correction

Thetis has **35 panels** (31 PanelTS + 4 plain Panel), not 33. Has **~16 spawned forms**, not 9. Group box count (11) is correct.

### 2.6 skin-compatibility.md — 2-Pan vs 4-Pan Inconsistency

Skin doc hard-caps legacy skins at 2 panadapters. MASTER-PLAN says always allow 4. Decision: always allow 4 pans. Skin doc to be updated.

### 2.7 MASTER-PLAN.md — AppletPanel References

Container system section says "no separate AppletPanel." Multi-panadapter.md still references AppletPanel. Reconcile: everything is a container.

### 2.8 skin-compatibility.md — TransparencyKey

WinForms `TransparencyKey` has no Qt6 equivalent. Decision: drop it. Legacy skins that use color-key transparency will render without transparency. Document as a known limitation.

---

## 3. Revised Phase Roadmap

### Completed Phases (no changes)

| Phase | What | Status |
|---|---|---|
| 0 | Scaffolding (69 files, CI, stubs) | Complete |
| 1 | Architectural Analysis (AetherSDR, Thetis, WDSP) | Complete |
| 2 | Architecture Design (6 design docs) | Complete |
| 3A | Radio Connection (P2 / ANAN-G2) | Complete |
| 3B | WDSP Integration + Audio Pipeline | Complete |
| 3C | macOS Build + Crash Fix | Complete |
| 3D | GPU Spectrum & Waterfall | Complete |
| 3E | VFO & Controls + Multi-Receiver Foundation | Complete |

### Revised Future Phases

#### Phase 3F: Multi-Panadapter Layout (PLAN READY)
**No changes to scope.** Implementation plan exists at `phase3f-multi-panadapter-plan.md`.

**Addition:** `UpdateDDCs()` port must include ALL state machine cases from Thetis `console.cs:8186-8538`, including PureSignal DDC states (DDC0+DDC1 sync at 192kHz, ADC cntrl1 override `(rx_adc_ctrl1 & 0xf3) | 0x08`), even though PS won't be enabled until 3I-4. This prevents reworking the state machine later.

#### Phase 3I-1: Basic SSB TX (NEEDS DESIGN DOC)
**Goal:** Get RF out the door — prove the TX I/Q output path works.

Scope:
- `TxChannel` WDSP wrapper — create TX channel, `fexchange2()` TX path
- Mic input via QAudioSource (48kHz, 16-bit) — matches Thetis mic path
- WDSP internal rate: 192kHz for P2, 48kHz for P1 (resampling handled by WDSP)
- MOX state machine — RX→TX→RX transition ported from Thetis `console.cs:29311`
  - 6 configurable delays: `rf_delay`, `mox_delay`, `ptt_out_delay`, `key_up_delay`, etc.
  - RX channel muting, DDC reconfiguration, T/R relay switching
  - Ordered WDSP channel enable/disable with flush
- TX I/Q output to radio — port 1029, 240 samples/packet, 24-bit big-endian
- `sendCmdTx()` on P2RadioConnection — port 1026 for mic/CW data
- TUNE function (reduced power carrier)
- Basic TX/RX audio routing (`UpdateAAudioMixerStates()`)

Thetis source references:
- `console.cs:29311-29650` — `chkMOX_CheckedChanged2()` (MOX state machine)
- `cmaster.cs:491-540` — `CMCreateCMaster()` (TX channel setup)
- `network.c:1250-1273` — `sendOutbound()` (TX I/Q packet format)
- `netInterface.c:893-944` — `CmdTx()` (TX command packet)

Verification: Key MOX, see RF output on ANAN-G2, hear SSB on another receiver.

#### Phase 3I-2: CW TX (NEEDS DESIGN DOC)
**Goal:** Full CW transmit with keyer and sidetone.

Scope:
- Sidetone generator — port from Thetis `sidetone.c`
  - Raised-cosine edge shaping, sample-accurate I/Q generation
  - NOT through TXA WDSP channel — separate path in ChannelMaster audio loop
  - Dot/dash timing derived from WPM: `dot_time = 1.2 / wpm`
  - IQ polarity configurable (`SetCWtxIQpolarity`)
- Firmware keyer support — dot/dash/PTT inputs via P2 port 1025
  - Key speed, mode (iambic A/B), weight sent via CmdTx packets
- QSK / semi break-in — `CWHangTime`, `key_up_delay`
- CW MOX special case — TX WDSP channel NOT enabled in CW mode
- APF (Audio Peak Filter) — narrowband CW filter via WDSP

Thetis source references:
- `ChannelMaster/sidetone.c` — complete sidetone state machine
- `cwkeyer.cs` — software keyer (legacy fallback)
- `console.cs:29590` — CW-specific MOX handling
- `netInterface.c:893-944` — CW keyer parameters in CmdTx

Verification: Send CW via paddle/keyboard, hear sidetone, clean CW on air.

#### Phase 3I-3: TX Processing Chain (NEEDS DESIGN DOC)
**Goal:** Full TX audio processing feature parity with Thetis.

The TXA chain has 18 stages (`TXA.c:557-591`). Each needs WDSP call wiring and UI controls:

| Stage | WDSP control call | UI location |
|---|---|---|
| Panel gain (mic) | `SetTXAPanelGain1` | PhoneControls |
| Phase Rotator | `SetTXAosctrlRun` | TxControls / Setup |
| 10-band TX EQ | `SetTXAEQRun`, `SetTXAGrphEQ10` | EqControls (TX tab) |
| Pre-emphasis (FM) | internal | automatic for FM mode |
| Leveler | `SetTXALevelerSt`, threshold, decay | TxControls |
| CFC | `SetTXACFCRun` + config dialog | Dedicated CFC dialog |
| Bandpass (BP0) | `SetTXABandpassFreqs` | automatic |
| CPDR (compressor) | `SetTXACompressorRun`, `SetTXACompressorGain` | PhoneControls |
| CESSB | `SetTXAosctrlRun` (with CESSB params) | PhoneControls toggle |
| ALC | `SetTXAALCMaxGain`, `SetTXAALCDecay` | always on |
| AM/FM modulators | `SetTXAAMCarrierLevel`, `SetTXAFMDeviation` | mode-dependent |
| DEXP (VOX/gate) | `SetDEXPRunVox`, `SetDEXPAttackThreshold` + anti-trip | PhoneControls |

Additional DSP features in this phase:
- SNB (Stochastic Noise Blanker) — `RxControls` toggle
- Spectral Peak Hold — `SpectrumWidget` overlay
- Histogram display mode — `SpectrumWidget` alternate render path
- RTTY mark/shift parameters — `SliceModel` properties

Thetis source references:
- `TXA.c:557-591` — full TXA signal chain
- `dsp.cs` — P/Invoke declarations for all TX WDSP functions
- `radio.cs` — TX parameter defaults and setters
- `setup.cs` — TX DSP control UI defaults

Verification: Clean SSB audio with TX EQ, compression, and ALC. Proper FM deviation. CESSB measurably improves average power.

#### Phase 3I-4: PureSignal (NEEDS DESIGN DOC)
**Goal:** PA linearization via client-side predistortion — full feature parity with Thetis PSForm.

Scope:
- **Feedback RX channel** — dedicated WDSP channel wired to DDC0 (synced with DDC1 at 192kHz)
  - ADC cntrl1 override: `(rx_adc_ctrl1 & 0xf3) | 0x08` routes TX coupler to feedback ADC
  - `SetPSRxIdx(0, 0)` / `SetPSTxIdx(0, 1)` — stream routing in ChannelMaster
- **calcc engine** — port `calcc.c` state machine (10 states: LRESET→LWAIT→LMOXDELAY→LSETUP→LCOLLECT→MOXCHECK→LCALC→LDELAY→LSTAYON→LTURNON)
  - Amplitude-binned sample collection (`ints` intervals, `spi` samples each)
  - 4-second collection watchdog, IQC dog count watchdog (count >= 6 → reset)
  - Correction computation thread (`doPSCalcCorrection`) via semaphore
  - Piecewise cubic Hermite spline fit for magnitude/I/Q correction curves
  - `scheck()` validation: no NaN, no zeros, values < 1.07, no sudden jumps > 0.05
  - Stabilize mode (alpha-blend new coefficients with old)
  - Pin mode (clamp corrections at amplitude extremes)
  - Map mode (convex envelope binning)
- **IQC real-time correction** — port `iqc.c`
  - Runs inside TXA audio path on every sample
  - `PRE_I = ym * (I*yc - Q*ys)`, `PRE_Q = ym * (I*ys + Q*yc)`
  - Double-buffered coefficient sets with cosine crossfade on swap
  - States: RUN, BEGIN (fade-in), SWAP (crossfade), END (fade-out), DONE (bypass)
- **TX/RX delay alignment** — fractional sample delay lines, 20ns step resolution
  - Positive = delay TX reference, negative = delay RX feedback
  - User-configurable via UI (Thetis `udPSPhnum`)
- **Auto-attenuation sub-state machine** — monitors feedback level (target 128-181 of 256)
  - Pauses calibration, adjusts TX attenuation, resumes
  - `eAAState` state machine running at 100ms intervals
- **PSForm UI** (menu bar accessible, not buried in a sub-dialog)
  - Calibrate button (single-shot + auto-cal modes)
  - Feedback level indicator (color-coded: Lime=good, Yellow=marginal, Red=bad, Blue=overdriven)
  - Corrections-active indicator
  - HW peak value (hardware-specific calibration)
  - Advanced panel: mox delay, loop delay, TX delay, stabilize/map/pin, polynomial tolerance
  - Save/restore correction coefficients to file
  - Two-tone test generator
  - Info bar integration for status bar feedback display
- **AmpView** — correction curve visualization (menu bar accessible under DSP or Tools)
  - Displays pre-distortion polynomial curves (magnitude, I, Q)
  - Read via `GetPSDisp()` — 7 display buffers

Thetis source references:
- `PSForm.cs` (1164 lines) — full UI and state machine
- `calcc.c` — pscc() state machine, calc() correction computation, delay alignment
- `iqc.c` — xiqc() real-time correction, coefficient management
- `TXA.c:557-591` — IQC position in TX chain
- `console.cs:8186-8538` — UpdateDDCs PS cases (ported in 3F)
- `cmaster.cs:533-534` — SetPSTxIdx/SetPSRxIdx stream routing
- `network.c:686-693` — P2 port 1025 feedback data

Verification: Enable PS on ANAN-G2, see feedback level indicator go green, measure IMD improvement on spectrum analyzer or second receiver. Save/restore coefficients across sessions. Auto-attenuation adjusts correctly when feedback level is wrong.

#### Phase 3G-1: Container Infrastructure (NEEDS DESIGN DOC)
**Goal:** Dock/float/resize/persist container shells — no rendering yet.

Scope:
- `ContainerWidget` — dock/float/resize/axis-lock (8 positions), title bar, settings gear
  - 23-field serialization (matching Thetis `ucMeter.ToString()` format concept)
  - Properties: border, background color, title/notes, RX source, show on RX/TX, auto-height, locked, hidden-by-macro, container-minimises, container-hides-when-rx-not-used
- `FloatingContainer` — `QWidget` with `Qt::Window | Qt::Tool`, pin-on-top via `Qt::WindowStaysOnTopHint`, per-monitor DPI via `QScreen`, save/restore geometry
- `ContainerManager` — singleton owning all containers, create/destroy, float/dock transitions, axis-lock repositioning on main window resize, serialization to AppSettings, restore on startup, macro visibility control
- Default layout: single right-side container (Container #0) with placeholder content

Thetis source references:
- `ucMeter.cs` — container widget, all 23 serialized fields, axis-lock system
- `frmMeterDisplay.cs` — floating form, DPI handling, title format
- `MeterManager.cs` — container lifecycle, persistence, import/export

Verification: Create containers, dock/float/resize, persist across restart, axis-lock holds position on window resize.

#### Phase 3G-2: MeterWidget GPU Renderer (NEEDS DESIGN DOC)
**Goal:** QRhi-based rendering engine for meter items following SpectrumWidget's 3-pipeline pattern.

Scope:
- `MeterWidget : public QRhiWidget` — one per container, renders all items in one draw pass
- **Pipeline 1 (textured quad):** cached QPainter textures (dial faces, scale labels), history graph ring buffer
- **Pipeline 2 (vertex-colored geometry):** needle sweep, bar fill, magic eye iris — uniform-driven animation
- **Pipeline 3 (QPainter → texture overlay):** tick marks, text readouts, LED states, button faces
- `MeterItem` base class — position, size, data source binding, visual properties, serialization
- `ItemGroup` — composites N items into a functional meter type (e.g. S-Meter = needle + scale + text + faceplate)
- Data binding framework — poll WDSP meter reads at ~100ms, push to bound items
- Dirty tracking — two-stage flags matching SpectrumWidget pattern

Shaders:
- `meter_bar.vert/.frag` — H/V bar with `fill_fraction`, `color_low/mid/high`, `orientation`
- `meter_needle.vert/.frag` — needle gauge with `angle`, `pivot`, `length`, `color`
- `meter_overlay.vert/.frag` — reuse existing `overlay.vert/.frag` (passthrough alpha blend)

Item types in this phase: BarItem (H_BAR, V_BAR), TextItem, ScaleItem (H_SCALE, V_SCALE), SolidColourItem, ImageItem

Verification: Container with a working bar meter bound to WDSP signal strength, updating at 10fps via GPU.

#### Phase 3G-3: Core Meter Groups (NEEDS DESIGN DOC)
**Goal:** Ship the meters operators expect on day one.

Scope:
- NeedleItem (NEEDLE, NEEDLE_SCALE_PWR) + `meter_needle` shader
- Default meter group presets:
  - S-Meter (NeedleItem + ScaleItem + TextItem + ImageItem background)
  - Power/SWR gauge (NeedleItem + ScaleItem)
  - ALC bar (BarItem + ScaleItem)
  - Mic/Comp bars (BarItem + ScaleItem)
- Default Container #0 pre-loaded with: S-Meter, Power/SWR, ALC
- Data binding: SIGNAL_STRENGTH, AVG_SIGNAL_STRENGTH, ADC, AGC_GAIN, PWR, REVERSE_PWR, SWR, MIC, COMP, ALC, ALC_GAIN

Verification: Live S-meter needle responding to signals. Power/SWR gauge during TX. All meters reading correctly against Thetis on same signal.

#### Phase 3G-4: Advanced Items (NEEDS DESIGN DOC)
**Goal:** Visual flair — the items that make it look like a real radio console.

Scope:
- HistoryItem (HISTORY) — scrolling signal graph, ring buffer texture (identical pattern to waterfall), `meter_history.vert/.frag`
- MagicEyeItem (MAGIC_EYE) — animated vacuum tube iris, `meter_eye.vert/.frag` with `aperture` uniform
- DialItem (DIAL_DISPLAY) — analog radio dial with rotating pointer (needle uniform + QPainter dial face)
- LedItem (LED) — on/off/blink states via color uniform
- FadeCoverItem (FADE_COVER) — alpha overlay effect
- WebImageItem (WEB_IMAGE) — async fetch + texture upload
- CustomMeterBarItem (CUSTOM_METER_BAR) — user-configurable meter (any WDSP meter type)

Verification: History graph scrolling with signal level over time. Magic eye responding to signal strength. Dial display showing frequency.

#### Phase 3G-5: Interactive Items (NEEDS DESIGN DOC)
**Goal:** Button grids and frequency displays inside containers.

Scope:
- ButtonItem base — GPU-drawn face + transparent QWidget overlay for events
  - BandButtons, ModeButtons, FilterButtons, AntennaButtons, TuneStepButtons, VoiceRecordPlayButtons, OtherButtons
- VfoDisplayItem — frequency readout inside a container (distinct from VfoWidget overlay on spectrum)
- ClockItem — UTC/local time display
- SpacerItem — layout spacing (no render)
- ClickBoxItem — generic clickable region

Verification: Band buttons switch bands. Mode buttons switch modes. All route through SliceModel correctly.

#### Phase 3G-6: Container Settings Dialog (NEEDS DESIGN DOC)
**Goal:** Full user customization — the composability UI.

Scope:
- Container settings dialog (in Setup or standalone):
  - Container selector dropdown
  - Item palette (available item types/groups to add)
  - Current item list (drag to reorder, click to select, remove)
  - Per-item property panel (data source, colors, size, scale range, units, font)
  - Preview pane (live rendering)
- Preset templates — one-click creation of common configurations
- Import/export — Base64-encoded container strings (matching Thetis `ContainerToString`/`ContainerFromString` concept)
- Duplicate container, recover off-screen container
- Macro visibility hooks — programmable buttons can show/hide containers

Verification: Create a container from scratch, add items from palette, configure data sources, export as Base64, import on another instance.

#### Phase 3H: Skin System (NEEDS DOC UPDATES)
**Goal:** Thetis-inspired skin format with 4-pan support + legacy skin import.

Updates from review:
- Always allow 4 pans (remove 2-pan legacy cap)
- Drop `TransparencyKey` support (no Qt6 equivalent — document as known limitation)
- Font fallback: map "Microsoft Sans Serif" → system sans-serif on macOS/Linux
- Error recovery for non-standard community skin ZIPs (graceful degradation, log unmapped controls)
- Widget tree must be stable before skin mapping works (depends on 3G completion)

#### Phase 3J: TCI Protocol + Spot System (NEEDS DESIGN DOC)
**Goal:** TCI v2.0 WebSocket server + DX spot overlay system.

Scope:
- TCI WebSocket server (scope TCI v2.0 command set — ~50 commands)
- TCI spot ingestion path — external programs push spots via TCI
- Built-in DX Cluster telnet client (AetherSDR-style `DxClusterClient`)
- Built-in RBN client (AetherSDR-style `RbnClient`)
- `SpotModel` for spot state management (AetherSDR pattern, batched at 1Hz)
- Spectrum overlay rendering for spots (callsign + frequency on panadapter)
- Country/prefix database for callsign → country lookup
- Verify against Thetis `SpotManager2.cs` for feature completeness

#### Phase 3K: CAT/rigctld + TCP Server (NEEDS DESIGN DOC)
**Goal:** External radio control for logging and contest software.

Scope:
- 4-channel slice-bound rigctld server (AetherSDR `RigctlServer` + `RigctlPty` pattern)
- TCP/IP CAT server (AetherSDR pattern, verified against Thetis `TCPIPcatServer.cs`)
- Verify CAT command set against Thetis `SIOListenerII` for completeness
- CatApplet UI for configuration

#### Phase 3L: Protocol 1 Support (NEEDS DESIGN DOC)
**Goal:** Support Hermes Lite 2 and older ANAN radios.

Scope:
- `P1RadioConnection` — UDP Metis framing (1032-byte frames)
- `MetisFrameParser` — C&C register rotation, EP6 I/Q format
- P1 discovery (0xEF 0xFE format, distinct from P2)
- Phase word encoding: `freq * 2^32 / 122880000`
- P1-specific DDC assignment (`rx_adc_ctrl_P1` encoding)
- TX at 48kHz (not 192kHz) — no CFIR needed

#### Phase 3M: Recording/Playback (NEEDS DESIGN DOC)
**Goal:** Full audio and I/Q recording system.

Scope:
- WAV audio recording — tap demodulated audio after WDSP, write to WAV file
- WAV playback through WDSP — route WAV file through DSP chain as if live I/Q (dev/demo without hardware)
- Quick record/playback — one-button 30-second scratch pad
- Scheduled recording — timer-based auto-start/stop
- I/Q recording — raw I/Q samples for offline analysis (large file efficient writer)
- Recording controls in VfoWidget (existing button stubs)

#### Phase 3N: Cross-Platform Packaging
**Goal:** Release builds for Linux, Windows, macOS.

CI workflows already exist. Finalize:
- AppImage (Linux x86_64 + ARM)
- Windows NSIS installer + portable ZIP
- macOS DMG (Apple Silicon)

---

## 4. Feature Inventory: Active vs Deferred

### Active Roadmap — Features Assigned to Phases

#### RX DSP (slot into RxControls / SpectrumWidget during relevant phases)
- NR, NR2, NB, NB2, ANF, TNF, BIN — already in plan
- **SNB** (Stochastic Noise Blanker) — RxControls toggle, Phase 3I-3
- **Spectral Peak Hold** — SpectrumWidget overlay, Phase 3I-3
- **Histogram display mode** — SpectrumWidget alternate render, Phase 3I-3

#### TX DSP (Phase 3I-3)
- **CESSB**, **TX Leveler**, **CFC** (with config dialog), **CPDR**, **TX EQ**, **ALC** — all in 3I-3
- **DEXP** (full VOX/gate parameter set) — PhoneControls, Phase 3I-3
- **Phase Rotator** — TxControls toggle, Phase 3I-3
- **RTTY mark/shift** — SliceModel properties, Phase 3I-3

#### PureSignal (Phase 3I-4)
- Full calcc/iqc engine, PSForm UI, AmpView (menu bar), auto-attenuation

#### Container/Meter System (Phase 3G-1 through 3G-6)
- Full Thetis-style composability, GPU rendering, 30+ item types

#### External Integration (Phases 3J, 3K)
- TCI server + spot ingestion via TCI
- Built-in DX Cluster + RBN clients
- 4-channel CAT/rigctld
- TCP/IP CAT server

#### AmpView (Phase 3I-4)
- PureSignal correction curve visualization, accessible from menu bar

#### Recording/Playback (Phase 3M)
- WAV record, WAV playback through WDSP, quick record, scheduled, I/Q record

### Documented, Deferred — Recognized But Not On Active Roadmap

| Feature | Why deferred | Revisit when |
|---|---|---|
| N1MM+ UDP integration | Not needed to ship | After 3K (CAT framework exists) |
| DVK (Digital Voice Keyer) | Contest feature, not core | After 3M (recording framework exists) |
| Andromeda front panel | Niche hardware (G8NJJ serial protocol) | User demand |
| RA (signal level recorder) | Low priority; HistoryItem covers similar ground | After 3G-4 |
| Quick Recall pad | Nice-to-have UX | Post-ship |
| Finder (settings search) | Nice-to-have UX | Post-ship |
| Wideband display | Research done (`wideband-adc-brainstorm.md`) | After 3L (P1) |
| Discord integration | Thetis-specific, niche | Unlikely |
| Phase/Phase2 display | I/Q Lissajous, niche diagnostic | User demand |
| Panascope/Spectrascope | Combined display modes | User demand |
| External amp monitoring | PGXL/TGXL are FlexRadio-specific | Post-ship, design for OpenHPSDR amps |
| DRM/SPEC implementation detail | Modes listed but no implementation plan | When digital mode support is designed |

---

## 5. Container System Architecture Decision

**Decision: Option C — Full Thetis-style composability with GPU rendering.**

### Rationale
The multi-meter container system is a killer feature that defines the Thetis experience. Users have spent years configuring custom meter layouts. NereusSDR must deliver equivalent composability.

### Architecture
- Each container holds a single `MeterWidget : public QRhiWidget` rendering all items
- 3-pipeline pattern matching SpectrumWidget: textured quad + vertex geometry + QPainter overlay
- Uniform-driven animation for needle/bar/eye (no per-frame geometry rebuild)
- Ring buffer texture for history graphs (same pattern as waterfall)
- Interactive buttons: GPU-drawn face + transparent QWidget overlay
- Scene graph of `MeterItem` objects with position/size/type/data-binding
- `ItemGroup` composition for pre-built meter templates

### Shaders (5 pairs)
| Shader | Purpose |
|---|---|
| `meter_bar.vert/.frag` | H/V bar meter (fill_fraction, colors, orientation) |
| `meter_needle.vert/.frag` | Needle gauge (angle, pivot, length, color) |
| `meter_eye.vert/.frag` | Magic eye iris (aperture, glow_color) |
| `meter_history.vert/.frag` | Scrolling history (scroll_offset — same as waterfall) |
| `meter_overlay.vert/.frag` | QPainter composite (reuse overlay.vert/.frag) |

### Item Type Hierarchy
```
MeterItem (base: position, size, data source, visual props, serialization)
├── BarItem (H_BAR, V_BAR)
├── NeedleItem (NEEDLE, NEEDLE_SCALE_PWR)
├── ScaleItem (H_SCALE, V_SCALE)
├── TextItem (TEXT, SIGNAL_TEXT_DISPLAY)
├── ImageItem (IMAGE, SOLID_COLOUR)
├── HistoryItem (HISTORY)
├── MagicEyeItem (MAGIC_EYE)
├── DialItem (DIAL_DISPLAY)
├── VfoDisplayItem (VFO_DISPLAY)
├── ClockItem (CLOCK)
├── LedItem (LED)
├── ButtonItem (BAND/MODE/FILTER/ANTENNA/TUNESTEP/OTHER/VOICE_RECORD_PLAY)
├── SpacerItem (layout only)
├── FadeCoverItem (FADE_COVER)
├── WebImageItem (WEB_IMAGE)
├── CustomMeterBarItem (CUSTOM_METER_BAR)
├── ClickBoxItem (CLICKBOX)
└── ItemGroup (composite — groups N items into a functional meter type)
```

### Data Source Binding (MeterType enum)
RX: SIGNAL_STRENGTH, AVG_SIGNAL_STRENGTH, SIGNAL_TEXT, ADC, AGC, AGC_GAIN, ESTIMATED_PBSNR
TX: MIC, EQ, LEVELER, LEVELER_GAIN, ALC, ALC_GAIN, ALC_GROUP, CFC, CFC_GAIN, COMP, PWR, REVERSE_PWR, SWR

---

## 6. PureSignal Architecture Summary

**Entirely client-side** for OpenHPSDR radios. No AetherSDR reference — pure Thetis port.

### Signal Path
```
TX audio → WDSP TXA chain → [xiqc() correction] → PA → antenna
                                    ↑                       ↓
                               calcc() computes        coupler samples
                               correction from:        via dedicated DDC
                               TX ref + RX feedback    (DDC0+DDC1 @192kHz)
```

### calcc State Machine (10 states)
LRESET → LWAIT → LMOXDELAY → LSETUP → LCOLLECT → MOXCHECK → LCALC → LDELAY → LSTAYON / back to LSETUP (auto)
                                                                         ↑
                                                                    LTURNON (restore saved coefficients)

### Key Parameters
- Feedback rate: 192kHz (hardcoded)
- Collection: `ints` amplitude intervals (default 16), `spi` samples per interval (default 256)
- TX/RX delay alignment: 20ns step resolution fractional delay lines
- Mox delay: configurable wait after MOX before collection (default 0.1s)
- Loop delay: wait between auto-cal cycles (default 0.0s)
- Polynomial tolerance: 0.4 (relaxed) or 0.8 (normal)
- Feedback level target: 128-181 (of 256 scale)

### Auto-Attenuation
- Runs at 100ms intervals
- Target feedback level: ~152 (center of 128-181 range)
- Adjusts TX attenuation in dB increments: `20*log10(FeedbackLevel/152.293)`
- Pauses calibration during adjustment

### UI
- PSForm accessible from menu bar (not buried in sub-dialog)
- AmpView (correction curve visualization) accessible from menu bar under DSP or Tools
- Info bar integration for status bar feedback level display

---

## 7. TX Pipeline Architecture Summary

### Decomposition
| Sub-phase | Scope |
|---|---|
| 3I-1 | Basic SSB TX: TxChannel, mic, MOX state machine, I/Q output |
| 3I-2 | CW TX: sidetone, firmware keyer, QSK |
| 3I-3 | TX processing: 18-stage TXA chain + RX DSP additions (SNB, peak hold, histogram, RTTY) |
| 3I-4 | PureSignal: feedback DDC, calcc, IQC, PSForm, AmpView |

### TXA Signal Chain (18 stages, from TXA.c:557-591)
1. Input resample (48k→192k for P2)
2. PreGen (tone/noise generator — test signals)
3. Panel gain (mic gain)
4. Phase Rotator
5. 10-band TX EQ
6. Pre-emphasis (FM mode)
7. Leveler (AGC-like)
8. CFC (Continuous Frequency Compressor)
9. BP0 (primary bandpass filter)
10. CPDR (compressor)
11. BP1 (post-compressor bandpass)
12. CESSB (controlled-envelope SSB)
13. BP2 (post-CESSB bandpass)
14. ALC (Automatic Level Control)
15. AM/FM modulators
16. Up-slew (5ms ramp)
17. PureSignal IQC (correction polynomial)
18. CFIR + output resample (P2 only)

### Key Numbers
- Mic input: 48kHz, 240 samples/packet (P2), 16-bit
- WDSP internal: 192kHz (P2), 48kHz (P1)
- TX I/Q output: port 1029, 240 samples/packet, 24-bit big-endian
- MOX delays: rf_delay (0), mox_delay (10ms), ptt_out_delay (configurable), key_up_delay
- CW sidetone: raised-cosine edges, 1.2/wpm dot timing, separate from TXA chain

---

## 8. AetherSDR Patterns Confirmed

The following NereusSDR architectural assumptions were verified against AetherSDR source:

| Pattern | AetherSDR Implementation | NereusSDR Status |
|---|---|---|
| QSplitter multi-pan | `PanadapterStack` with layout string IDs | Correctly planned in 3F |
| `wirePanadapter()` | Per-pan signal wiring in MainWindow | Correctly planned in 3F |
| Applets as plain QWidget | No shared base class | Correctly planned |
| `FloatingAppletWindow` | Top-level QWidget, dock button, geometry persist | Map to FloatingContainer |
| `AppSettings` singleton | XML, PascalCase keys, no QSettings | Already implemented |
| 4-5 named threads | Main, Connection, Audio, Spectrum, Spot | Correctly planned |
| Auto-queued signals | No mutexes in hot paths, std::atomic for DSP params | Already implemented |
| `QSignalBlocker` | Feedback loop prevention | Already implemented |
| Lambda wiring | Dominant style in connect() calls | Already used |
| Disconnect-before-removal | Explicit disconnect before widget destruction | Planned in 3F |

### AetherSDR Features to Adopt
| Feature | AetherSDR class | NereusSDR phase |
|---|---|---|
| DX Cluster + RBN | `DxClusterClient`, `RbnClient`, `SpotModel` | 3J |
| 4-channel CAT/rigctld | `RigctlServer`, `RigctlPty` | 3K |
| Profile manager | `ProfileManagerDialog` | Post-ship |
| Band plan overlay | `BandPlanManager` | 3J or 3G |

---

## 9. Quality Assessment Update

| Phase | Design Doc | Impl Plan | Ready? |
|---|---|---|---|
| **3F** Multi-Pan | Complete | Complete | **Yes — start now** |
| **3I-1** Basic TX | Outlined above | Needs full plan | **No** |
| **3I-2** CW TX | Outlined above | Needs full plan | **No** |
| **3I-3** TX Processing | Outlined above | Needs full plan | **No** |
| **3I-4** PureSignal | Outlined above | Needs full plan | **No** |
| **3G-1** Container Infra | Outlined above | Needs full plan | **No** |
| **3G-2** GPU Renderer | Outlined above | Needs full plan | **No** |
| **3G-3** Core Meters | Outlined above | Needs full plan | **No** |
| **3G-4** Advanced Items | Outlined above | Needs full plan | **No** |
| **3G-5** Interactive Items | Outlined above | Needs full plan | **No** |
| **3G-6** Settings Dialog | Outlined above | Needs full plan | **No** |
| **3H** Skins | Needs updates | Needs plan | **No** |
| **3J** TCI + Spots | Needs design | Needs plan | **No** |
| **3K** CAT/rigctld | Needs design | Needs plan | **No** |
| **3L** Protocol 1 | Partial design | Needs plan | **No** |
| **3M** Recording | Needs design | Needs plan | **No** |
| **3N** Packaging | CI exists | N/A | **Low effort** |

**Next design doc needed:** Phase 3I-1 (Basic SSB TX) — this unblocks the entire TX chain including PureSignal.
