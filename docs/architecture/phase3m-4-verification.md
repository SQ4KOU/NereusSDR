# Phase 3M-4 PureSignal: Bench Verification Matrix

> Companion to [phase3m-4-puresignal-design.md](phase3m-4-puresignal-design.md)
> and [phase3m-4-puresignal-plan.md](phase3m-4-puresignal-plan.md).
> Run on real hardware before merging the 3M-4 PR to main.

**Status:** Drafted at end-of-epic (Task 15). Bench validation pending hardware
access. All 14 PS-related test executables pass under `ctest`; full suite at
366/366 PASS as of Task 15.

---

## 1. Hardware required

- [ ] **ANAN-G2** (or G2-1K, 7000DLE, 8000DLE, 100D, 200D, OrionMkII) for
  primary G2-class verification.
- [ ] **Hermes Lite 2 (mi0bot firmware)** for HL2-specific path verification.
- [ ] **Dummy load** with thermal capacity for steady TX (50W+ at 70% duty).
- [ ] **Spectrum analyzer** OR **second SDR receiver** for IMD3 / IMD5
  measurement (resolution bandwidth wide enough to resolve fundamentals
  separated by 700 Hz tone spacing).
- [ ] (Optional) **Power meter** for output-power validation against PA
  telemetry.
- [ ] **Stable mains** + good thermals — calibration pulls peaks over a 100 ms
  window; thermal drift mid-cal will skew the corrections.

---

## 2. 15-surface acceptance checklist

Per design doc §11.2: every surface must demonstrate full UI surface
usability at land. Not scaffolded-but-disabled, not model-wired-but-not-
wire-format-correct.

### Surface 1 — PsForm modeless dialog

- [ ] Opens from `Tools > PureSignal...` menu.
- [ ] Capability-gated to `caps.hasPureSignal`: hidden on Atlas / Hermes / HL2
  if hasPureSignal=false.
- [ ] Title bar reads `PureSignal 2.0` verbatim (PSForm.Designer.cs:12
  [v2.10.3.13]).
- [ ] All 23 controls render at correct positions per Thetis
  PSForm.Designer.cs.
- [ ] Advanced toggle collapses ClientSize 560x300 -> 560x60 (and back).
- [ ] **Single Cal** button triggers calcc state transition LRESET -> LWAIT
  -> LDELAY -> LSIGNAL -> LCALC -> LCOMP (verify via debug log + indicator
  changes).
- [ ] **OFF** button halts cal and clears coefficients.
- [ ] Always-On-Top checkbox sets `Qt::WindowStaysOnTopHint`.
- [ ] All numeric spinboxes (TINT, MOX Wait, CAL Wait, AMP Delay, AutoAtten
  Quick Atten) accept input and persist via PureSignal coordinator setters.
- [ ] Save / Restore buttons gate correctly on `correctionsBeingApplied()`.

### Surface 2 — AmpViewWindow

- [ ] Opens from PsForm `btnPSAmpView` button.
- [ ] Title `AmpView 1.0` verbatim.
- [ ] 5 chart series render: Ref, MagAmp, PhsAmp, MagCorr, PhsCorr.
- [ ] All 4 toolbar checkboxes drive their effects: Show Gain, Phase Zoom,
  Low Res, On Top.
- [ ] FixAmpViewOnTop lifecycle: AmpView stays on top when PsForm is on top
  (per PSForm.cs:FixAmpViewOnTop() [v2.10.3.13]).
- [ ] Closing AmpView while PsForm is open does NOT crash.

### Surface 3 — Tools > PureSignal menu item

- [ ] Capability-gated: hidden when current board has `caps.hasPureSignal=false`.
- [ ] Menu shortcut works (whatever was chosen at Task 8).

### Surface 4 — TxApplet [PS-A] toggle

- [ ] Hidden when `caps.hasPureSignal=false`.
- [ ] Left-click toggles `PureSignal::setAutoCalEnabled`.
- [ ] Right-click opens PsForm via Tools > PureSignal action.
- [ ] Reflects `PureSignal::autoCalEnabledChanged` state changes (visual
  state matches model).

### Surface 5 — PsaIndicatorWidget bottom-banner pair

- [ ] Inserted between m_rxDashboard and m_stationBlock in MainWindow's
  bottom-banner area.
- [ ] 6-state machine transitions correctly per ucInfoBar.cs:842-895
  [v2.10.3.13]:
  - PS off / idle / under / marginal / in-range / over.
- [ ] FB color **default un-swapped**: Blue 0-90 / Yellow 91-128 / Green 129-181 / Red 182+.
- [ ] FB color swap toggles correctly via Setup checkbox **AND** left-click
  (mirror invariant).
- [ ] Hide-feedback toggles correctly via Setup checkbox **AND** right-click
  (mirror invariant).
- [ ] Compact-fonts mode swaps `FB`/`Correct` labels (verify threshold).

### Surface 6 — HPF Bypass on PureSignal feedback (Setup)

- [ ] Setup -> Antenna/ALEX -> Alex-1 Filters has
  `HPF Bypass on PureSignal feedback` checkbox.
- [ ] Default `Checked=true`.
- [ ] Un-checking shows IMD warning dialog (Surface 7).

### Surface 7 — IMD warning dialog

- [ ] Multi-paragraph text matches Thetis verbatim
  (`AntennaForm.cs:chkBypassPureSignalHPF_CheckedChanged()` [v2.10.3.13]).
- [ ] Cancel default-focused (Button2) reverts the toggle to checked.
- [ ] OK keeps the toggle un-checked.

### Surface 8 — `Hide feedback level` checkbox (Setup)

- [ ] Setup -> General -> Options has `Hide feedback level` checkbox.
- [ ] Toggling persists to per-MAC AppSettings.
- [ ] Banner FB indicator updates immediately.
- [ ] Right-click on FB also flips this state (mirror, Surface 15).

### Surface 9 — `Swap red and blue PS-A feedback colours` checkbox (Setup)

- [ ] Setup -> General -> Options has
  `Swap red and blue PS-A feedback colours` checkbox.
- [ ] Toggling persists to per-MAC AppSettings.
- [ ] Banner colors update immediately.
- [ ] Left-click on FB also flips this state (mirror, Surface 15).

### Surface 10 — `Pure_Signal_Enabled` per-TX-profile

- [ ] Per design doc §9.1: per-TX-profile is canonical PS-enable persistence.
- [ ] All 19+ stock TX profiles default to `Pure_Signal_Enabled=false`.
- [ ] User TX profile recall flips PS state (TransmitModel pureSigEnabled
  property).
- [ ] No editor checkbox in Setup -> TX Profiles (matches Thetis: state is
  implicit-via-profile-mechanism, not edited directly).

### Surface 11 — PSSaveCorr / PSRestoreCorr file dialogs

- [ ] PsForm Save button gated on `correctionsBeingApplied()=true`.
- [ ] PsForm Save -> QFileDialog opens at `~/.config/NereusSDR/PureSignal/`.
- [ ] Saved file is a binary blob; PSSaveCorr writes it.
- [ ] PsForm Restore -> QFileDialog opens at the same default folder; PSRestoreCorr
  reads.
- [ ] After Restore, `_restoreON=true` so calcc skips cal-from-scratch and
  uses loaded peaks.

### Surface 12 — Two-tone test

- [ ] PsForm Two-tone toggle activates TwoToneController.
- [ ] Fundamentals visible on spectrum at the expected offsets (matches
  Thetis tone frequencies).

### Surface 13 — Two-tone IMD spectrum overlay

- [ ] Renders ONLY when `MOX && testingIMD && showIMDMeasurements && displayDuplex`.
- [ ] Box at X=50, Y=50, 260x180 px, rounded corners 14 px (matches design
  doc §4 surface 13).
- [ ] Peak markers on f0 L/U, IMD3 L/U, IMD5 L/U.
- [ ] EMA-smoothed numeric readouts (3-column: label / val1 / val2).
- [ ] worstImd3 dBc, worstImd5 dBc, OIP3, OIP5 displayed in summary block.

### Surface 14 — `ForcePureSignalAutoCalDisable()`

- [ ] Public method on PureSignal coordinator (port of Thetis console.cs
  ForcePureSignalAutoCalDisable [v2.10.3.13]).
- [ ] Invoked on sample-rate change correctly stops cal (cancels in-flight
  cal, clears auto-cal state).

### Surface 15 — FB-label click handlers + dynamic tooltip

- [ ] Left-click swaps red/blue (mirror of Setup checkbox 9).
- [ ] Right-click hides numeric (mirror of Setup checkbox 8).
- [ ] Hover shows tooltip with current legend.
- [ ] Default tooltip text:
  `Showing level, Red 0-90, Yellow 91-128, Green 129-181, Blue 182+`.
- [ ] Swapped tooltip text:
  `Showing level, Blue 0-90, Yellow 91-128, Green 129-181, Red 182+`.

---

## 3. DSP correctness (bench-validated on hardware)

### 3.1 Two-tone test on G2 + dummy load

- [ ] Set TX to two-tone test, drive to ~70% of full power.
- [ ] **Without PS**: measure baseline IMD3 (typical 25-35 dBc below
  fundamentals on a stock G2 PA driving dummy load).
- [ ] Enable PS via PsForm.
- [ ] Run **Single Cal** via Single Cal button.
- [ ] FB level enters 129-181 of 256 target range (Lime in PsaIndicator).
- [ ] `Correcting` indicator activates (PS label Lime `Correcting`).
- [ ] Re-measure IMD3: improvement of **10-25 dB** (depending on PA
  characteristics; design doc §11.3).

### 3.2 HL2 verification

- [ ] Repeat 3.1 procedure on HL2 with mi0bot firmware.
- [ ] Confirm `psSampleRate=0` sentinel correctly uses rx1_rate (per
  BoardCapabilities Task 1).
- [ ] Confirm `cntrl1=4 + P1_DDCConfig=6` wire bytes match (per Task 5
  HL2 codec PS DDC config).
- [ ] Confirm `psDefaultPeak=0.233` displayed in PsForm (HL2 default).

---

## 4. Auto-attention regression

Per design doc §8.2 (`eAAState` sub-state machine).

- [ ] Set ATT manually too high (FB level << 128): PS auto-att pulls ATT
  down until level enters target range.
- [ ] Set ATT manually too low (FB level >> 181): PS auto-att pushes ATT up.
- [ ] Disable AutoCal via PS-A button: ATT-on-TX un-stash logic from
  `chkFWCATUBypass_CheckedChanged` runs correctly.
- [ ] eAAState transitions Monitor -> SetNewValues -> RestoreOperation ->
  Monitor cycle (verify via debug log).

---

## 5. Persistence

### 5.1 Per-TX-profile (canonical, design doc §9.1)

- [ ] `Pure_Signal_Enabled` per-TX-profile: save profile X with PS on,
  switch to Y with PS off, switch back to X — PS state should reflect
  profile.
- [ ] All 19+ stock factory profiles default to false.

### 5.2 Per-MAC PsForm scalars

- [ ] PsForm scalars (TINT, MOX Wait, CAL Wait, AMP Delay, Auto-Attenuate,
  Quick Attenuate, etc.) persist across app restart.
- [ ] AmpView checkboxes (Show Gain, Phase Zoom, Low Res, On Top) persist
  across app restart.

### 5.3 Calibration coefficients

- [ ] User-saved files (PsForm Save -> file dialog).  Save to file, exit,
  restart, Restore from file.
- [ ] After Restore, `_restoreON=true`; calcc uses loaded peaks (no
  cal-from-scratch on next MOX).
- [ ] Coefficients are **NOT** per-band (design doc §9.5 — Thetis matches).

### 5.4 Dead per-MAC `pureSignal/enabled` key (Task 15)

- [ ] Verify the orphaned key has no live writer in src/ (Task 15
  removed the dead read from `TransmitModel::loadFromSettings`).
- [ ] Stale entries in older user settings files do not affect runtime
  state (canonical path is per-TX-profile).

---

## 6. Capability gating

- [ ] **ANAN-G2** (caps.hasPureSignal=true): all PS surfaces visible:
  - Tools > PureSignal menu visible.
  - PureSignalApplet visible.
  - TxApplet [PS-A] visible.
  - PsaIndicatorWidget visible.
  - PsForm + AmpView open.
- [ ] **Atlas** (caps.hasPureSignal=false): all PS surfaces hidden.
- [ ] **HL2 (mi0bot firmware)**: hasPureSignal=true, all surfaces visible;
  HL2-specific feedback path exercised (per design doc §3.5).
- [ ] **Hermes** / **HermesII**: caps fan out per BoardCapabilities Task 1.

---

## 7. Carson-branch coordination

Per plan Task 12: the spectrum IMD overlay touches similar regions to the
carson-branch peak-blob work.

- [ ] If carson-branch peak-blob lands first: rebase 3M-4 onto it, refactor
  IMD overlay to share peak-detection helper.
- [ ] If 3M-4 lands first: carson-branch can refactor at landing time.
  Note in 3M-4 PR description.

---

## 8. Pre-merge final checks

- [ ] Build clean on Linux + macOS (Windows TBD per design §15).
- [ ] All ctest pass: `ctest --test-dir build --output-on-failure`.
  - **Status (Task 15 close-out):** 366/366 PASS.
- [ ] All verifiers green:
  - `python3 scripts/verify-thetis-headers.py --all-kinds`
  - `python3 scripts/check-new-ports.py`
  - `python3 scripts/verify-inline-cites.py`
  - `python3 scripts/compliance-inventory.py --fail-on-unclassified`
  - `python3 scripts/verify-inline-tag-preservation.py`
- [ ] Manual bench verification matrix above run on at least one G2-class
  board + HL2.
- [ ] PR branch rebased on latest main; merge conflicts (if any with
  carson-branch peak-blob work) resolved.
- [ ] MASTER-PLAN.md §3M-4 row updated to Complete with PR # and version
  stamp (release-process step, NOT Task 15).
- [ ] CHANGELOG.md entry added under Unreleased (release-process step,
  NOT Task 15).

---

## 9. Open follow-ups (post-merge)

- [ ] **HermesII PS DDC config**: verify branches at Thetis console.cs:8453+
  ramdor (deferred from Task 5).
- [ ] **AnvelinaPro3 PS path**: verify via Task 5 dispatch test.
- [ ] **WDSP file format** for PSSaveCorr / PSRestoreCorr blobs: document
  binary layout for forward-compat reasoning.
- [ ] **LinearityForm**: verified during brainstorm to NOT exist as a
  separate form (just startup-restoration helper for PsForm).
- [ ] **eAAState** exact state list and timings: verify against Thetis
  source one more time before bench cal.
- [ ] **`_ema_*` smoothing constants**: alpha=0.1f confirmed at task time;
  re-verify against display.cs at bench.
- [ ] **AmpView GetPSDisp** buffer indices: deferred phase math; current
  implementation matches AmpView.cs:disp_data_Update.
- [ ] **MicProfileManager `Pure_Signal_Enabled` key wiring**: per design
  §9.1 the key is canonical for PS-enable persistence; the current
  scaffold does not yet plumb it through `defaultProfileValues()` /
  `captureLiveValues()` / `applyValuesToModel()`.  Wire it in a follow-up
  PR before the 3M-4 bench session if the bench session needs profile-
  scoped recall to be authoritative.

---

*Generated 2026-05-06 by J.J. Boyd (KG4VCF) with AI-assisted source-first
protocol via Anthropic Claude Code.  Phase 3M-4 Task 15 close-out deliverable.*
