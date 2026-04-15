# Phase 3G-10 — RX DSP Parity + AetherSDR Flag Port

**Design document**
**Date:** 2026-04-15
**Author:** JJ Boyd (KG4VCF) with Claude Opus 4.6
**Branch:** `feature/phase3g10-rx-dsp-flag`
**Status:** Spec, pending plan

## 1. Phase identity & position

Phase 3G-10 is a two-stage polish/parity pass that (1) ports the AetherSDR
`VfoWidget` visual shell into NereusSDR with faithful color/font/layout
fidelity, then (2) finishes wiring every RX-side DSP NYI stub through the
existing `SliceModel → RxChannel → WDSP` chain. It ships behind a single
branch and a single plan, executed in two sequential stages.

3G-10 slots alongside 3G-9 (Display Refactor) in the 3G-series polish cadence.
Both phases are independent of TX work and ship in parallel with 3M-1 prep.
3G-9 owns the Display setup surface; 3G-10 owns the VFO flag and every RX
DSP control surfaced by `VfoWidget` and `RxApplet`. There is no file-level
collision between the two phases — verified by grepping 3G-9's design doc
(`2026-04-15-display-refactor-design.md`) for any reference to `VfoWidget`,
`RxApplet`, `SliceModel`, `RxChannel`, or the per-band bandstack surface —
none found.

**Sequence**: `3G-8 → 3G-10` runs in parallel with `3G-8 → 3G-9a/b/c`, both
converging before `3M-1` (Basic SSB TX).

## 2. Goals

1. **VFO flag visual parity with AetherSDR.** Faithful port of colors,
   fonts, layout, tab structure, hover/checked states, and custom-painted
   widgets (`LevelBar`, `ScrollableLabel`, `TriBtn`, `CenterMarkSlider`,
   `ResetSlider`) into NereusSDR. The flag must pass a visual diff against
   AetherSDR side-by-side on the same freq/mode/filter state.

2. **Complete RX DSP wiring.** Every NYI stub in `RxApplet` that represents
   a RX-side DSP control is wired end-to-end: `VfoWidget` or `RxApplet`
   control → `SliceModel` setter → `RxChannel` WDSP call → audible/visible
   effect on RX1. The following NYI list from the current tree
   (`grep -n NYI src/gui/applets/RxApplet.cpp`) is fully eliminated:

   - Frequency lock, mute, audio pan, step cycle
   - Squelch on/off + threshold (SSB / AM / FM variants)
   - AGC threshold, hang, slope, attack, decay
   - RIT on/off + value, XIT on/off + value
   - NB2 (WDSP NOB), EMNR (NR2), SNB, APF + tune
   - Binaural
   - Mode-specific containers: FM OPT, DIG offset, RTTY mark/shift,
     CW autotune

3. **AetherSDR DSP-tab button set → WDSP equivalents.** The flag DSP grid
   exposes the NereusSDR/WDSP-native set rather than AetherSDR's
   deep-learning filters:

   | AetherSDR slot | NereusSDR slot | WDSP entry point |
   |---|---|---|
   | NR  | NR   | `SetRXAANRRun`   |
   | NR2 | NR2  | `SetRXAEMNRRun`  |
   | NB  | NB   | `xanbEXTF`       |
   | — (new) | NB2 | `xnobEXTF` |
   | ANF | ANF  | `SetRXAANFRun`   |
   | — (new) | SNB | `SetRXASNBARun` |
   | — (new) | APF | `SetRXAAPFRun`  |

4. **Thetis-first tooltip sweep.** Every control on the flag and every
   rewired control in `RxApplet` gains a tooltip. Text is sourced from
   Thetis `Setup.cs` / `console.cs` verbatim where present (cited with
   `// From Thetis setup.cs:NNNN`), written in NereusSDR voice for
   native-only controls (`// NereusSDR native — no Thetis equivalent`).
   No empty tooltips and no placeholder strings.

5. **Per-slice-per-band persistence** for DSP-flavored state. NR, NB,
   AGC (mode/threshold/hang/slope/attack/decay), filter edges, squelch,
   APF tune, and the mode-specific container values are keyed under
   `Slice<N>/Band<KEY>/*`. Session-state (lock, mute, RIT/XIT values) is
   keyed under `Slice<N>/*` only. Switching bands restores the last-used
   DSP settings for that slice × band combination, matching Thetis
   bandstack semantics.

## 3. Non-goals

Explicitly out of scope for 3G-10, to be addressed by their owning phases:

- **Any TX path touch** — `TxChannel`, mic input, MOX state machine, TX I/Q
  producer. Belongs to 3M-1.
- **PureSignal** — feedback DDC, `calcc` / IQC engine, PSForm, AmpView.
  Belongs to 3M-PS.
- **Multi-panadapter assignment** — FFTRouter, PanadapterStack, RX2 enable.
  Belongs to 3F.
- **Display setup surface polish** — owned by 3G-9 (`Setup → Display`
  category). 3G-10 does not touch `Setup → Display/*` pages, overlay
  cache invalidation, waterfall chrome, peak hold VBO, or grid/trace color
  knobs.
- **Skin system changes.** 3H.
- **Container / meter system changes.** Complete at 3G-7 except for the
  deferred items listed in the 3G-7 handoff doc; those remain deferred.
- **Applet redesigns beyond `RxApplet` NYI flipping.** TxApplet,
  PhoneCwApplet, EqApplet, etc. are untouched.
- **CW keyer, sidetone, or break-in logic.** Belongs to 3M-CW.
- **Freq lock as a radio-authoritative property.** `SliceModel::setLocked`
  is a client-side guard only.

## 4. Hard gates (carried from CLAUDE.md + user memory)

- **Source-first protocol.** No WDSP function call is written without first
  (a) quoting the Thetis Console callsite that uses it and (b) showing the
  WDSP C export signature. The 3G-8 §10 divergence exception is **not**
  extended to 3G-10.
- **GPG-signed commits.** All contributions.
- **AppSettings only.** Never `QSettings`.
- **Atomic parameters** in `RxChannel` for every new DSP state bit. Main
  thread writes; audio thread reads. No mutex in the audio callback.
- **Radio-authoritative settings policy.** DSP state is client-authoritative,
  persisted locally; antennas, preamp, ADC att are radio-authoritative and
  are not persisted by 3G-10.
- **Options-with-recommendation** for every user-visible decision encountered
  during implementation.

## 5. Critical finding — partial Thetis clone

*(Resolved 2026-04-15 during plan handoff.)* The original local
`../Thetis/` clone was partial — it contained `Project Files/Source/wdsp/`
and `Project Files/Source/Midi2Cat/` but was missing
`Project Files/Source/Console/`. A fresh full clone now lives at
`/Users/j.j.boyd/Thetis` with the entire `Console/` directory present.
The partial clone was preserved at `Thetis.partial.<epoch>` as a
safety net.

**Filename correction**: the Thetis C# P/Invoke declarations for the
WDSP API live in `Project Files/Source/Console/dsp.cs`, **not** in a
file named `wdsp.cs`. Earlier drafts of this design and the project's
`CLAUDE.md` referenced `wdsp.cs` — that was incorrect. Source-first
citations for Stage 2 must target `dsp.cs` for P/Invoke signatures
plus `console.cs` / `setup.cs` / `cmaster.cs` for call-site usage
patterns.

**Thetis commit in use**: recorded in
`docs/architecture/phase3g10-verification/README.md` during the
Stage 2 pre-gate (§S2.0.2 of the plan).

Stage 1 is unaffected — its sources are AetherSDR (`~/AetherSDR`,
complete locally, verified on `main`) and NereusSDR-internal.

## 6. Background — current state of the RX DSP surface

### 6.1 Already wired (`RxChannel.cpp` + `SliceModel.cpp`)

- `setMode` → `SetRXAMode`
- `setFilterFreqs` → `SetRXABandpassFreqs`
- `setAgcMode` → `SetRXAAGCMode`
- `setAgcTop` → `SetRXAAGCTop`
- `setNb1Enabled` → `xanbEXTF` gating flag
- `setNb2Enabled` → `xnobEXTF` gating flag
- `setNrEnabled` → `SetRXAANRRun`
- `setAnfEnabled` → `SetRXAANFRun`
- `setShiftFrequency` → `SetRXAShiftRun` + `SetRXAShiftFreq`

### 6.2 NYI stubs (source of the Stage 2 work list)

Enumerated from `grep -n "NYI\|TODO Phase" src/gui/applets/RxApplet.cpp`
and the VfoWidget tab contents:

| NYI control | Location | WDSP entry (target) | SliceModel setter (target) |
|---|---|---|---|
| Frequency lock | RxApplet:114, VfoWidget X/RIT tab | *client-side guard only* | `setLocked(bool)` (new) |
| Mute | RxApplet:306, VfoWidget Audio tab | `SetRXAPanelRun` off | `setMuted(bool)` (new) |
| Audio pan | RxApplet:340, VfoWidget Audio tab | `SetRXAPanelPan` | `setAudioPan(double)` (new) |
| Step cycle | RxApplet:238 | *client-side only* | `cycleStep()` (new) |
| Squelch SSB | RxApplet:369 | `SetRXASSQLRun` + `SetRXASSQLThreshold` | `setSsqlEnabled`, `setSsqlThresh` |
| Squelch AM | RxApplet:369 | `SetRXAAMSQRun` + `SetRXAAMSQThreshold` | `setAmsqEnabled`, `setAmsqThresh` |
| Squelch FM | RxApplet:369 | `SetRXAFMSQRun` + `SetRXAFMSQThreshold` | `setFmsqEnabled`, `setFmsqThresh` |
| AGC threshold | RxApplet:418 | `SetRXAAGCThresh` | `setAgcThreshold(int)` |
| AGC hang | RxApplet:418 | `SetRXAAGCHang` | `setAgcHang(int)` |
| AGC slope | RxApplet:418 | `SetRXAAGCSlope` | `setAgcSlope(int)` |
| AGC attack | RxApplet:418 | `SetRXAAGCAttack` | `setAgcAttack(int)` |
| AGC decay | RxApplet:418 | `SetRXAAGCDecay` | `setAgcDecay(int)` |
| RIT on/off | RxApplet:439 | *client offset only* | `setRitEnabled`, `setRitHz` |
| XIT on/off | RxApplet:477 | *client offset only (TX gated by 3M-1)* | `setXitEnabled`, `setXitHz` |
| NR2 / EMNR | VfoWidget DSP tab | `SetRXAEMNRRun` | `setEmnrEnabled` |
| SNB | VfoWidget DSP tab | `SetRXASNBARun` | `setSnbEnabled` |
| APF | VfoWidget DSP tab | `SetRXAAPFRun` | `setApfEnabled`, `setApfTune` |
| Binaural | VfoWidget Audio tab | `SetRXABinaural` | `setBinauralEnabled` |
| FM OPT container | VfoWidget DSP tab | FM CTCSS/offset/reverse (see §8.1) | `setFmCtcss*`, `setFmOffsetHz`, `setFmSimplex`, `setFmReverse` |
| DIG offset container | VfoWidget DSP tab | *client offset to `setFrequency`* | `setDigOffsetHz` |
| RTTY mark/shift | VfoWidget DSP tab | `SetRXABandpassFreqs` derived | `setRttyMarkHz`, `setRttyShiftHz` |
| CW autotune | VfoWidget Mode tab | (Matched CW helper in WDSP) | `cwAutotuneOnce()`, `cwAutotuneLoop(bool)` |

All WDSP entry points in this table have been verified to exist in
`third_party/wdsp/src/` via `grep -rn "^PORT void Set\|^void Set" third_party/wdsp/src/`
against the NYI list.

## 7. Stage 1 — VfoWidget shell port

### 7.1 Files

| File | Action |
|---|---|
| `src/gui/widgets/VfoStyles.h` | **New** — all `k*` style string constants + color `constexpr`s copied verbatim from AetherSDR `VfoWidget.cpp`, tagged with source line numbers. |
| `src/gui/widgets/VfoLevelBar.{h,cpp}` | **New** — S-meter bar. Paint logic ports AetherSDR `LevelBar::paintEvent`, extended with an S-unit tick strip rendered **above** the bar (per user choice C in spec-session). dBm range: -130 to -20. Ticks: S1/3/5/7/9/+20/+40. Exposes `setValue(float dbm)` from the audio-thread meter poller via `Qt::QueuedConnection`. |
| `src/gui/widgets/ScrollableLabel.{h,cpp}` | **New** — port of AetherSDR helper used for RIT/XIT/DIG-offset readouts. Wheel-step + double-click inline edit. |
| `src/gui/widgets/VfoModeContainers.{h,cpp}` | **New** — four `QWidget` subclasses: `FmOptContainer`, `DigOffsetContainer`, `RttyMarkShiftContainer`, `CwAutotuneContainer`. Each with `setSlice(SliceModel*)`, `syncFromSlice()`, and owns its own child widgets and layout. Rationale: keeps `VfoWidget.cpp` under ~2.6k lines. |
| `src/gui/widgets/VfoWidget.{h,cpp}` | **Rewrite.** Currently 1014 lines. Expected size after Stage 1: ~2400–2600 lines. Filename preserved; all call sites unchanged. |
| `src/core/WdspTypes.h` | **Extend** — add `enum class NrMode { Off, ANR, EMNR }`, `NbMode { Off, NB1, NB2 }`, `SquelchMode { Off, Voice, AM, FM }`, `AgcHangMode`, and param bounds `constexpr` pulled from WDSP header comments. Declarations only; no runtime. |
| `src/models/SliceModel.{h,cpp}` | **Extend (additive stubs)** — declare every new `Q_PROPERTY` + setter + signal listed in §6.2, with `.cpp` bodies that are `if (m_foo == v) return; m_foo = v; emit fooChanged(v);` only. **No** `RxChannel` calls yet — Stage 1 is visual. |
| `src/gui/applets/RxApplet.cpp` | **Untouched in Stage 1.** |
| `tests/tst_vfo_level_bar.cpp` | **New** qtest — dBm → fill fraction, tick-strip layout math. |
| `tests/tst_scrollable_label.cpp` | **New** qtest — wheel-step math, inline-edit parse. |
| `tests/tst_vfo_mode_containers.cpp` | **New** qtest — each container's `syncFromSlice` populates correctly for a minimally-configured `SliceModel`. |

### 7.2 `VfoWidget` class map (replacement)

```
VfoWidget : QWidget   (252px fixed width, kBgStyle transparent)
├── Header row (QHBoxLayout, kFlatBtn transparent)
│   ├── m_rxAntBtn   QPushButton (color #4488ff)   → QMenu from m_antennaList
│   ├── m_txAntBtn   QPushButton (color #ff4444)   → QMenu
│   ├── m_filterWidthLbl QLabel (color #00c8ff)    ← derived from filterLow/high
│   ├── <stretch>
│   ├── m_splitBadge QPushButton (low-alpha)        → splitToggled / swapRequested
│   ├── m_txBadge    QPushButton (28×20)            → SliceModel::setTxSlice
│   └── m_sliceBadge QLabel (20×20 A/B/C/D, #0070c0 bg)
├── Frequency row
│   └── m_freqStack QStackedWidget
│       ├── m_freqLabel  QLabel (26px bold #c8d8e8, faint border)
│       └── m_freqEdit   QLineEdit (26px cyan #00e5ff, appears on double-click)
├── Meter row (QHBoxLayout 75/25)
│   ├── m_levelBar  VfoLevelBar (75%)
│   └── m_dbmLabel  QLabel (11px #6888a0, right-aligned, 25%)
├── Tab bar (QLabels + eventFilter click dispatch)
│   └── Audio | DSP | Mode | X/RIT           (kTabLblNormal / kTabLblActive)
├── m_tabStack QStackedWidget
│   ├── AudioTab QWidget
│   │   ├── m_afGainSlider   ResetSlider(50, Horizontal)
│   │   ├── m_panSlider      CenterMarkSlider(0, Horizontal)
│   │   ├── m_muteBtn        QPushButton (kDspToggle)
│   │   ├── m_binBtn         QPushButton (kDspToggle)
│   │   ├── m_sqlBtn + m_sqlSlider     ResetSlider(-150)
│   │   ├── m_agcRow         [Off|Long|Slow|Med|Fast] QPushButton × 5 (kModeBtn)
│   │   └── m_agcTSlider     ResetSlider(-20, Horizontal)
│   ├── DspTab QWidget
│   │   ├── m_dspGrid QGridLayout
│   │   │   NB   NB2  NR   NR2
│   │   │   ANF  SNB  APF  ·
│   │   ├── m_apfTuneSlider  ResetSlider(0, Horizontal)
│   │   ├── m_fmContainer    FmOptContainer*    (visible iff mode∈{FM,NFM})
│   │   ├── m_digContainer   DigOffsetContainer* (visible iff mode∈{DIGL,DIGU})
│   │   └── m_rttyContainer  RttyMarkShiftContainer* (visible iff mode=RTTY)
│   ├── ModeTab QWidget
│   │   ├── m_modeCombo      QComboBox (DSPMode populated)
│   │   ├── m_quickModeBtns  QPushButton[3] (user-assignable slots, persisted)
│   │   ├── m_filterGrid     QGridLayout (8 preset buttons + "Var")
│   │   └── m_cwAutotune     CwAutotuneContainer* (visible iff mode=CW)
│   └── XRitTab QWidget
│       ├── m_ritBtn + m_ritLabel(ScrollableLabel) + m_ritZeroBtn
│       ├── m_xitBtn + m_xitLabel(ScrollableLabel) + m_xitZeroBtn
│       ├── m_lockBtn        QPushButton (kDspToggle)
│       └── m_stepCycleBtn   QPushButton (kDspToggle, label "STEP ×10" etc.)
└── Floating siblings (parented to SpectrumWidget)
    ├── m_closeBtn   ✕  (kSliceBtnStyle)
    ├── m_lockVfoBtn 🔒
    ├── m_recBtn     ●  (kSliceBtnStyle, red when active)
    └── m_playBtn    ▶  (green when active)
```

### 7.3 Stage 1 commit sequence

1. `phase3g10(style): add VfoStyles.h with verbatim AetherSDR style constants`
2. `phase3g10(meter): add VfoLevelBar widget + unit test`
3. `phase3g10(widget): add ScrollableLabel + unit test`
4. `phase3g10(widget): add VfoModeContainers (FM/DIG/RTTY/CW) standalone`
5. `phase3g10(model): extend SliceModel with stub setters for new DSP state`
6. `phase3g10(flag): rewrite VfoWidget header + freq row + meter row`
7. `phase3g10(flag): rewrite VfoWidget tab bar + four tab panes (NYI-badged)`
8. `phase3g10(flag): embed VfoModeContainers with mode-visibility rules`
9. `phase3g10(flag): floating lock/close/rec/play against SpectrumWidget parent`
10. `phase3g10(test): VfoWidget render snapshot across 4 tabs × 5 modes`

Each commit compiles cleanly, ships the previous shell, and is reviewed
in isolation. Every commit message starts with `phase3g10(`scope`):`.

### 7.4 Stage 1 exit criteria

- NereusSDR builds clean and launches; flag renders alongside the spectrum
  with AetherSDR-faithful styling on macOS, Linux, and Windows (spot-check
  via `/release` artifact build).
- Every button present on the flag, NYI-badged where unwired, never crashes
  on click.
- Mode switching (USB → CW → FM → DIGU → RTTY) shows the correct
  mode-specific container inside the DSP or Mode tab and hides the others.
- `VfoLevelBar` paints live dBm from `MeterPoller` — verified on connected
  radio.
- `tst_vfo_level_bar`, `tst_scrollable_label`, `tst_vfo_mode_containers`
  pass.
- **No WDSP behavior change vs `main`** — confirmed by comparing
  `getMeter(RxMeterType::SMeter)` + audible output on an ANAN-G2 and a HL2
  before/after on the same band/mode/signal.

## 8. Stage 2 — SliceModel + RxChannel + WDSP backfill

Stage 2 lands one feature-vertical slice at a time. Each slice reads as a
small, reviewable commit that touches: `SliceModel.{h,cpp}`, `RxChannel.{h,cpp}`,
`VfoWidget.cpp` (remove NYI badge, wire signal), `RxApplet.cpp` (remove
NYI guard, wire same SliceModel slot), `tests/tst_slice_<feature>.cpp`,
`tests/tst_rxchannel_<feature>.cpp` (GoogleTest against WDSP in-process).

### 8.1 Slice order

1. **AGC threshold / hang / slope / attack / decay**
   WDSP: `SetRXAAGCThresh`, `SetRXAAGCHang`, `SetRXAAGCSlope`,
   `SetRXAAGCAttack`, `SetRXAAGCDecay`. First because it unblocks the
   existing AGC combo UX (m_agcTSlider was already visible, just inert).
2. **EMNR (NR2)** — `SetRXAEMNRRun` + position/gain-method/NPE-method
   accessors.
3. **SNB** — `SetRXASNBARun`.
4. **APF** — `SetRXAAPFRun` + `SetRXAAPFFreq` (tune slider) +
   `SetRXAAPFBw`.
5. **Squelch (SSB / AM / FM)** — `SetRXASSQLRun`+`Threshold`,
   `SetRXAAMSQRun`+`Threshold`, `SetRXAFMSQRun`+`Threshold`. Slice routes
   to the correct variant based on `dspMode()`.
6. **Mute / audio pan / binaural** — `SetRXAPanelRun`, `SetRXAPanelPan`,
   `SetRXABinaural`.
7. **NB2 activation polish** — the `xnobEXTF` gate is already wired;
   expose position/lead/tau/hang parameters matching Thetis defaults.
8. **RIT / XIT client offset** — `SliceModel::setRitHz` adds to shift
   offset applied via existing `setShiftFrequency` path. XIT stored but
   no TX effect until 3M-1.
9. **Frequency lock** — client-side guard in `SliceModel` that rejects
   `setFrequency` calls while locked.
10. **Mode-specific containers behavior**
    - **FM OPT**: CTCSS encode (`SetTXACTCSSRun` is TX-gated and deferred;
      3G-10 wires RX tone squelch decode via FMSQ only), offset (client
      applies via `setShiftFrequency` on PTT — RX preview only in 3G-10),
      reverse (swaps TX/RX tuning on keydown — gated by 3M-1), simplex
      (forces offset to 0). RX-only behaviors in 3G-10; TX behaviors
      stubbed with a 3M-1 TODO.
    - **DIG offset**: pure client-side offset added to demod shift.
    - **RTTY mark/shift**: computes `filterLow/High` from mark and shift,
      pushes through existing filter path.
    - **CW autotune**: invokes WDSP matched-CW helper (`matchedCW.h` —
      verified present) to find the nearest CW signal; once mode = one
      call, loop mode = QTimer at 500ms.

### 8.2 Per-feature commit template

```
phase3g10(rx-dsp): wire <feature>

- SliceModel: <properties added/changed>
- RxChannel: <WDSP calls added>
- VfoWidget: remove NYI badge on <button(s)>
- RxApplet: remove NYI guard on <control>
- tests: tst_slice_<feature>, tst_rxchannel_<feature>

From Thetis Project Files/Source/Console/<file>.cs:NNNN — <citation>
WDSP: third_party/wdsp/src/<file>.{c,h}:NNNN
```

### 8.3 Stage 2 exit criteria

- All RxApplet NYI markers removed.
- All VfoWidget buttons operate live end-to-end on an ANAN-G2 and a HL2.
- `tst_slice_*` + `tst_rxchannel_*` tests all pass.
- Per-feature manual verification matrix filled in
  (`docs/architecture/phase3g10-verification/README.md`).
- Audio regression check: on a clean-install profile, tune 14.225 MHz USB,
  verify identical baseline RX behavior to `main` with every new DSP
  control left at its default value.

## 9. Per-slice-per-band persistence scheme

### 9.1 Key layout

```
Slice0/Lock                     True|False           (session)
Slice0/Mute                     True|False           (session)
Slice0/Rit/Enabled              True|False           (session)
Slice0/Rit/ValueHz              int                  (session)
Slice0/Xit/Enabled              True|False           (session)
Slice0/Xit/ValueHz              int                  (session)
Slice0/Band20m/FilterLowHz      int                  (per-band)
Slice0/Band20m/FilterHighHz     int                  (per-band)
Slice0/Band20m/AgcMode          int (AGCMode enum)   (per-band)
Slice0/Band20m/AgcThreshold     int                  (per-band)
Slice0/Band20m/AgcHang          int                  (per-band)
Slice0/Band20m/AgcSlope         int                  (per-band)
Slice0/Band20m/AgcAttack        int                  (per-band)
Slice0/Band20m/AgcDecay         int                  (per-band)
Slice0/Band20m/NbMode           int                  (per-band)
Slice0/Band20m/NrMode           int                  (per-band)
Slice0/Band20m/AnfEnabled       True|False           (per-band)
Slice0/Band20m/SnbEnabled       True|False           (per-band)
Slice0/Band20m/ApfEnabled       True|False           (per-band)
Slice0/Band20m/ApfTuneHz        int                  (per-band)
Slice0/Band20m/SquelchEnabled   True|False           (per-band)
Slice0/Band20m/SquelchThresholdDb double             (per-band)
Slice0/Band20m/BinauralEnabled  True|False           (per-band)
Slice0/Band20m/FmCtcssMode      int                  (per-band, FM only)
Slice0/Band20m/FmCtcssValueHz   double               (per-band, FM only)
Slice0/Band20m/FmOffsetHz       int                  (per-band, FM only)
Slice0/Band20m/FmSimplex        True|False           (per-band, FM only)
Slice0/Band20m/FmReverse        True|False           (per-band, FM only)
Slice0/Band20m/DigOffsetHz      int                  (per-band, DIG only)
Slice0/Band20m/RttyMarkHz       int                  (per-band, RTTY only)
Slice0/Band20m/RttyShiftHz      int                  (per-band, RTTY only)
```

Band key suffix comes from `Band::bandKeyName()` (exists from 3G-8).

### 9.2 Restore trigger

- `PanadapterModel::bandChanged(Band)` signal is the canonical band-change
  hook (3G-8).
- `SliceModel` subscribes via a new `onBandChanged(Band)` slot that reads
  the new band's keys from `AppSettings` and applies them via the existing
  setters (which emit `*Changed` and propagate to `RxChannel` + UI).
- Initial population on app start: `SliceModel::restoreFromSettings()`
  reads both session-state and the current-band per-band state.

### 9.3 Migration

- Existing `Slice<N>/FilterLowHz` style keys from 3G-8 era are migrated
  once on first launch into `Slice<N>/Band<current>/FilterLowHz`. The
  migration is logged at info level and only runs if the old key exists.
- No other migration needed — every new key is fresh in 3G-10.

## 10. Tooltip sourcing protocol

### 10.1 Policy (per user choice B)

1. **Thetis-first.** If Thetis `Setup.cs` or `console.cs` has a
   `SetToolTip` or tooltip-constant call for the semantic equivalent of
   the NereusSDR control, port the text **verbatim** (including
   grammatical quirks) and attribute with a C++ comment
   `// From Thetis <path>:<line>` immediately above the
   `setToolTip()` call.
2. **NereusSDR fallback.** If no Thetis equivalent exists (e.g. "LOCK",
   "BIN", our custom S-unit tick strip), write NereusSDR-voice text and
   tag with `// NereusSDR native — no Thetis equivalent`. NereusSDR-voice
   is short, imperative, sentence-case, period-terminated.
3. **No empty tooltips** on any control that ships as enabled.
4. **NYI-badged controls** get a tooltip of the form
   `"<control name> — pending Phase <phase>"`.

### 10.2 Coverage check

A new unit test `tst_vfo_tooltip_coverage.cpp` walks the `VfoWidget`
children tree at runtime and asserts every `QAbstractButton` +
`QSlider` + `QComboBox` has a non-empty `toolTip()`. Fails build if a
new control is added without one.

### 10.3 Implementation location

Tooltips live inline at widget construction in `VfoWidget::buildUI` and
the `VfoModeContainers` constructors. No separate tooltip strings file —
keeping the Thetis citation adjacent to the `setToolTip()` call is the
whole point of the policy.

## 11. Testing + verification

### 11.1 Unit tests (new in 3G-10)

- `tst_vfo_level_bar.cpp` — dBm→fill mapping, tick-strip layout
- `tst_scrollable_label.cpp` — wheel-step math, inline-edit parse
- `tst_vfo_mode_containers.cpp` — each container populates correctly
- `tst_vfo_tooltip_coverage.cpp` — non-empty tooltips on all enabled controls
- `tst_slice_persistence_per_band.cpp` — round-trip save/restore for every
  per-band key, migration from legacy 3G-8 keys
- Per feature-slice in Stage 2: `tst_slice_<feature>`,
  `tst_rxchannel_<feature>`

### 11.2 Manual verification matrix

`docs/architecture/phase3g10-verification/README.md` (created as part of
the Stage 2 work). Same format as the 3G-8 verification doc: one row per
control, columns = {Hermes Lite 2 / ANAN-G2}, tester signs off with date.

### 11.3 Regression check

Before-and-after A/B on an ANAN-G2 and a HL2:
- 14.225 MHz USB with a live signal at -90 dBm
- Tune to 7.200 LSB
- Switch to FM on a NOAA Weather pipe via XVTR
- Verify audible parity at every step, S-meter reading within ±1 dB,
  spectrum/waterfall parity.

## 12. MASTER-PLAN.md + README.md reconciliation

### 12.1 MASTER-PLAN.md inserts

1. New subsection "Phase 3G-10: RX DSP Parity + AetherSDR Flag Port"
   between 3G-9 and 3M-1 (under "Phase 3 — Implementation"), mirroring
   the 3G-9 format: goal, two-stage deliverable list, non-goals, source
   links to this design doc and the plan doc.
2. "Recommended Next Step" section updated to note 3G-9 and 3G-10 ship
   in parallel as the two blocking polish phases before 3M-1.
3. Sequence diagram line added:
   `3G-8 → 3G-10 (RX DSP + flag)    (parallel with 3G-9a/b/c)`
4. The existing list of "Planned near-term phases" gains a 3G-10 bullet
   between the 3G-9 and 3M-1 entries.

### 12.2 README.md inserts (after MASTER-PLAN)

1. **"Current Status" paragraph**: append a sentence identifying 3G-9 and
   3G-10 as the twin polish phases that gate 3M-1.
2. **"Roadmap" table**: add a row for 3G-10 with Goal = "RX DSP parity +
   AetherSDR flag port", Status = "Planning" (updates through "In
   progress" → "Complete").
3. **"Planned" bullet list**: 3G-10 bullet between 3G-9 and 3M-1.
4. **CLAUDE.md** documentation-index table: add 3G-10 design + plan doc
   links to the "Implementation Plans" section.

## 13. Risks & open questions

- **R1. Partial Thetis clone.** Mitigated by §5 gate; still a delivery
  risk if network is unavailable at Stage 2 commit time.
- **R2. Stage 1 flag rewrite is a large single-file churn.** Mitigated
  by committing in 10 small pieces (§7.3) and by factoring
  `VfoModeContainers` + `VfoStyles` out to separate files.
- **R3. APF tune slider UX** — AetherSDR uses a slider for "tune" but
  Thetis's APF is a fixed-bandwidth peak filter. Need to reconcile during
  Stage 2. **Open question**: if the two semantics differ meaningfully,
  do we honor Thetis (source-first) or AetherSDR (UX parity)? Source-first
  wins unless user approves an exception.
- **R4. FM CTCSS TX path.** 3G-10 wires RX tone squelch only; TX encode
  is TODO-commented with a 3M-1 hand-off marker.
- **R5. AppSettings XML schema growth.** Per-slice-per-band adds ~25
  keys × 14 bands × N slices. Estimate impact on settings file size and
  parse time; if either is material, introduce a compact per-band blob
  format. **Open question**: 14×25 = 350 new keys per slice per launch.
  Monitor first, optimize if needed.
- **R6. CW autotune WDSP helper surface** is thinly documented. Verify
  `matchedCW.h` API against Thetis Console usage before Stage 2 step 8.1.10.

## 14. Exit criteria (phase)

The phase is complete when:

1. Every Stage 1 exit criterion (§7.4) is met.
2. Every Stage 2 exit criterion (§8.3) is met.
3. Every NYI marker in `RxApplet.cpp` relating to RX-side DSP is removed.
4. MASTER-PLAN.md, README.md, and CLAUDE.md show 3G-10 as complete.
5. A per-band manual A/B regression pass on a Hermes Lite 2 and an
   ANAN-G2 is signed off in the verification doc.
6. `/release` artifact build lane is green on all six lanes
   (Linux x86_64/arm64, macOS arm64, Windows x86_64 portable + NSIS).
7. Final PR lands with a GPG-signed merge commit and branch is
   auto-deleted.
