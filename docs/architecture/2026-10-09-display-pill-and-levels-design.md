# Display pill, waterfall levels and display fixes: design

**Date:** 2026-10-09
**Author:** J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code
**Status:** Decided, revised after review. Every item below was ruled on by JJ on 2026-10-09 (Q1 to Q12, settled calls 1 to 15, and the plain fixes he was shown and did not veto). Revision 2 (same day) folds in a three-reviewer review of transmit/receive behaviour, remote windows and the Core, and every open PR and branch: section 5 is rewritten, section 8 is rewritten, and the settled items S12 to S22 are added (section 13).
**Base:** `origin/main` at `2a8a09958` (PR 372 merged; PR 373 merged at `7e4eea193`). Every NereusSDR `file:line` below is on that tree unless it says otherwise.
**Merge order:** this work lands before the Longpath look branches (`claude/wvr-scenes`, `claude/nereus-longpath-visual-design-62f639`, `claude/wvr-meters`, `claude/wvr-t23`); that lane merges main afterwards and resolves its overlap in `SpectrumOverlayPanel.cpp`, `SpectrumOverlayMenu.cpp` and `SpectrumWidget.*` (orchestrator, 2026-10-09). The stale noise-floor dashed lines in `SpectrumWidget::renderGpuFrame`'s overlay clear are Longpath's fix, not this spec's; nothing here changes that clear.
**Upstream pins:** Thetis `v2.10.3.15` (`3759d096`). AetherSDR `@1e0718ad`.
**Mockups (approved):**
- [display-pill-layout.html](2026-10-09-display-pill-mockups/display-pill-layout.html): layouts A, B and C. JJ chose **B**.
- [display-pill-additions.html](2026-10-09-display-pill-mockups/display-pill-additions.html): layout B with the five AetherSDR additions outlined in amber and numbered. JJ chose all five.

Paths are repo-relative. Thetis paths are relative to `../Thetis/Project Files/Source/Console/`. AetherSDR paths are relative to `../AetherSDR/`.

---

## 1. Problems

These come from four read-only audits run on 2026-10-08 against `a0681c508`, re-checked against `0701d7dfc` and again against `2a8a09958`.

| # | Problem | Evidence on main |
| --- | --- | --- |
| P1 | **The left-strip Display flyout never seeds.** It shows its build-time constants (scheme "Classic", gain 50, black 15, fill alpha 70, cyan fill, cursor on) whatever the pan holds. The first touch of a slider writes a value built on the wrong baseline. Only Grid Lines is seeded. | Constants: `src/gui/SpectrumOverlayPanel.cpp:598`, `:614`, `:641`. No setters other than `setClarityStatus` and `setGridVisible`: `src/gui/SpectrumOverlayPanel.h:167-174`. Wiring is one-way: `src/gui/MainWindow.cpp:6349-6393`. |
| P2 | **Scheme names disagree, and Clarity Blue is missing from two surfaces.** The flyout lists four invented names (Classic, Phosphor, Sunrise, Inverted) mapped by index to Default, Enhanced, Spectran, Black & White. The right-click menu lists four real names. Both miss LinLog, LinRad, Custom and Clarity Blue. The menu clamps the index, so a pan on Clarity Blue shows "Black & White". | Flyout: `SpectrumOverlayPanel.cpp:598`, `MainWindow.cpp:6359-6364`. Menu: `src/gui/SpectrumOverlayMenu.cpp:108-113`, clamp `:466`. Eight names: `src/core/spectrum/WaterfallPalettes.cpp:209-224`. |
| P3 | **Black Level range and direction.** The flyout's Black slider is 0-100 with 15 as its start; the store and the menu are 0-125 with a default of 104. Nudging the flyout slider sends about 15, which lifts the black point about 44 dB. In the renderer a higher Black Level shows *more* noise, but all three tooltips say the opposite. | Range: `SpectrumOverlayPanel.cpp:639-643`, `SpectrumOverlayMenu.cpp:139-141`, `src/models/DisplaySettingsModel.h:273-274`. Formula: `src/gui/SpectrumWidget.cpp:7576-7577`, its own comment `:7564`. Applet tooltip: `src/gui/applets/DisplayApplet.cpp:122-125`. |
| P4 | **NF-AGC and AGC look dead under Clarity.** Clarity is on by default. While it drives the levels, the follower skips AGC and NF-AGC. That is the documented precedence, but nothing on screen says so. | Default on: `src/models/RadioModel.cpp:1692`. Skip: `src/core/spectrum/DisplayFollowers.cpp:245-285`. Precedence: `docs/architecture/waterfall-tuning.md:157-170`. |
| P5 | **Clarity re-anchors after un-key instead of restoring (the #359 behaviour).** Keying clears Clarity on every pan, not only the transmitting one. Un-key wipes the smoothing and the deadband, so the first frame after the key is taken whole as the floor and emitted unconditionally. On a real band that lands anywhere, and the deadband then holds it up to 2 dB away. The blue level then drifts back over about 9 s. Both edges also re-prime the waterfall AGC. Thetis restores the cached pre-key level instead. | `src/core/ClarityController.cpp:49-61` (wipe), `:133-134` (cadence gate off), `:153-154` (first sample whole), `:156-158` (smoothing step measured across the key); `src/gui/PanClarityRegistry.cpp:453` (every pan); `src/gui/MoxDisplayController.cpp:428`, `:528` (AGC re-prime); Core `src/core/session/media/DisplayExtras.cpp:655-657`. Thetis: `display.cs:1585`, `:2810-2820`, `:2856-2858`, `:5912`. Audit H1, H2, H6, H7. |
| P6 | **Clarity also re-anchors on a same-band re-centre.** Any DDC centre change marks the pan for a full reset, so the level jumps while tuning with CTUN off. | `MainWindow.cpp:8327-8346`, `:6407-6409`; `src/gui/PanClarityRegistry.cpp:308-314`, `:108-114`. Audit H4. |
| P7 | **Clarity's per-band levels are never saved.** Priming needs at least 30 samples inside a 2 s window, but samples arrive every 500 ms (at most about 5 per window). | `PanClarityRegistry.cpp:226-251`; cadence `src/core/ClarityController.h:169`. |
| P8 | **Clarity wobble.** A single raw frame's 30th percentile over the whole DDC, smoothed with tau 3 s, moves the levels in discrete 2 dB steps. Thetis lerps every row. | `ClarityController.h:169-174`; `src/core/NoiseFloorEstimator.cpp:32-45`; Thetis `display.cs:7666`. Audit H3. See [Open items](#13-open-items). |
| P9 | **The FFT size slider feels lethargic and does not stick.** Every tick of a drag replans and restarts the fill, so the trace freezes until the drag ends plus one fill time. The next zoom replaces the slider's size. The page loads the zoomed size, not the slider's. Issue #296: the size resets on restart. | `src/gui/setup/DisplaySetupPages.cpp:695-764` (no `setTracking(false)`), `:310-316` (loads `fftSize()`); `src/core/FFTEngine.cpp:404-405`; auto-zoom `MainWindow.cpp:8933-9009`. |
| P10 | **Setup pushes reach pan 0 only.** FFT size, window, Hz/bin, FPS and thread priority act on the primary engine. Grid & Scales acts on `firstPan`. The auto-zoom replan is wired to pan 0's widget only. | `DisplaySetupPages.cpp:695-764`, `:2154`, `:2265`, `:2282`; `src/models/RadioModel.h:2925`; `MainWindow.cpp:8955-8958`. |
| P11 | **Setup does not follow changes made on the pan.** Only the 3D View page subscribes to the display model; the other pages read once on open. | `DisplaySetupPages.cpp:3334-3379`. |
| P12 | **Setup Cal Offset does nothing.** The window overwrites the widget's offset with the station's receive offset on every frame. | `DisplaySetupPages.cpp:991-1004`; `MainWindow.cpp:6861-6862`. |
| P13 | **Setup's per-band dB Max and dB Min do nothing.** They write the per-band store, but nothing in the GUI listens. | `DisplaySetupPages.cpp:2255-2285`; `src/models/PanadapterModel.cpp:244-272`; no GUI reader of `levelChanged` (grep). |
| P14 | **Waterfall AGC moves both edges.** NereusSDR moves low and high to run-min minus 12 and run-max plus 12. Thetis moves only the low edge and keeps the user's High. | `DisplayFollowers.cpp:253-273`; Thetis `display.cs:6463-6478`, `:7666`. |
| P15 | **Remote window with no S-meter: the VFO flag's meter freezes.** The poller returns before the per-flag loop. | `src/gui/meters/MeterPoller.cpp:1159`. |
| P16 | **The fill colour dialog always opens on cyan.** | `SpectrumOverlayPanel.cpp:699-703`. |

---

## 2. Decisions

Each decision says what the user sees and what changes in code. "Q" numbers and "settled call" numbers refer to JJ's rulings of 2026-10-09.

### The pill

**D1. Every spectrum and waterfall control is reachable from the Display pill (Q8, Q9).**
The user opens Display on the left strip of a pan and sees four tabs: Spectrum, FFT, Waterfall, 3D View (layout B). Section 3 lists every row.
Code: rebuild `SpectrumOverlayPanel::buildDisplayFlyout()` (`SpectrumOverlayPanel.cpp:563-815`) as four tab pages. This supersedes the flyout design in `docs/architecture/comprehensive-ui-port-design.md:72-93` (per-slice tabs plus a "More Display Options" link).

**D2. The pill opens on the pan's values and follows changes made elsewhere while open (settled call 1; plain fix).**
Code: add setters for every row, seeded under `QSignalBlocker` from the pan's `DisplaySettingsModel` and `SpectrumWidget` on show, and connected to their change signals while visible. The right-click menu's pattern (`SpectrumWidget.cpp:10105-10171`) and AetherSDR's `syncDisplaySettings` (`src/gui/SpectrumOverlayMenu.cpp:2152-2252`) are the models. Fixes P1.

**D3. All eight schemes, with one set of names, in the pill, the right-click menu (where it remains) and Setup (settled call 2; plain fix).**
Names come from `wfSchemeName()` (`WaterfallPalettes.cpp:209-224`). Clarity Blue stays, and a fresh install keeps the Default palette (JJ 2026-04-15, `waterfall-tuning.md:131-138`). Code: fill every scheme combo by looping over `WfColorScheme::Count`, as Setup already does (`DisplaySetupPages.cpp:1863-1870`). Drop the index clamp at `SpectrumOverlayMenu.cpp:466` and the adapter at `MainWindow.cpp:6359-6364`. Fixes P2.

**D4. Fill and trace colour pickers open on the current colour (settled call 4; plain fix).**
Code: seed `QColorDialog::getColor` from `sw->fillColor()` (and the new trace colour, D30) instead of `#00e5ff` (`SpectrumOverlayPanel.cpp:701`). Fixes P16.

**D5. "Reset this pan" returns that pan's look to the Setup defaults and leaves the shared FFT settings alone (settled call 10).**
Code: remove the pan's own per-pan keys so it inherits again (the inheritance is in `SpectrumWidget.cpp:745-761`), then re-seed the pill. Station keys (FFT tab, per-band dB) are untouched. Pan 0 included: see S4 (Setup gets its own default keys).

**D6. "Setup defaults →" opens Setup > Display on the page that matches the open tab (settled call 11).**
Spectrum opens "Spectrum Defaults", FFT opens "Spectrum Defaults" (its Fast Fourier Transform group), Waterfall opens "Waterfall Defaults", 3D View opens "3D View" (page names: `src/gui/SetupDialog.cpp:1756-1795`). Code: reuse the `openSetupRequested` path, which already makes the pan active first (`MainWindow.cpp:6378-6388`).

**D7. The mouse wheel over the pill never tunes (settled call 12).**
Code: the panel already swallows wheel events (`SpectrumOverlayPanel.cpp:1294`) and NereusSDR has `GuardedSlider` (`src/gui/widgets/GuardedSlider.h`). Each tab page uses guarded sliders and combos. If a tab ever needs to scroll, AetherSDR's wheel guard (`src/gui/SpectrumOverlayWheelGuard.cpp`) is the reference, and porting it needs attribution (section 3.7).

**D8. The pill remembers the last open tab (settled call 14).**
Code: one per-window key (proposed name `DisplayPillLastTab`, on this computer).

### Waterfall levels

**D9. Under Clarity, Black Level is the offset of the black point from the noise floor; the middle of the slider is at the noise (Q1, Q3).** Section 4.

**D10. Under Clarity, Color Gain is the colour span: about 80 dB at 0 down to about 10 dB at 100, widened both ways from today's 30 dB authority (Q1, settled under Q1).** Section 4.

**D11. Black Level up = blacker. Tooltips say so (Q3, settled call 3).**
This matches FlexRadio and AetherSDR's stated meaning. Section 4.4 covers the shared AetherSDR bug and why ours differs.

**D12. Today's look under Clarity becomes the default slider positions, and stored values are converted on first run so nothing changes on first open (Q1, Q3, settled call 3).**
Today, under Clarity with Black 104 and Gain 45: black point about N+3.4 dB, top about N+41.5 dB. Section 4.3.

**D13. While Clarity is on, AGC, NF-AGC, Low and High are greyed with the note "Clarity is setting the levels. Turn it off to use these." When Clarity is off they keep the Thetis behaviour (Q1, confirmed after reconciliation).**
Disabled, not hidden. Code: the pill's Waterfall tab and Setup's Waterfall Defaults page (`DisplaySetupPages.cpp:1665-1789`) both grey these rows off the pan's Clarity state. The precedence itself (Clarity, then NF-AGC, then AGC, then manual) is unchanged; the missing indicator was the bug. Fixes P4.
Setup already greys Low, High, AGC and NF-AGC while "Use spectrum min/max" is on (issue #230; `DisplaySetupPages.cpp:1540-1551`, `:1705-1726`). The two gates combine: a row is live only when neither Clarity nor "Use spectrum min/max" owns the levels, in Setup and in the pill (S12). The note names whichever owns them. The phone's Setup description gets the same gates (S20).
When neither owns the levels and AGC is on, Low alone is gray in Setup and in the pill with "AGC is setting Low. Turn AGC off to set it yourself.", because AGC works the low level out and the stored Low is not used; High and AGC stay live (Thetis `setup.cs:16365` [v2.10.3.15]: `udDisplayWaterfallLowLevel.Enabled = !chkRX1WaterfallAGC.Checked;`).

**D13a. While Clarity is off, Black Level and Color Gain are greyed with the note "Clarity is off: Low and High set the levels." (JJ "a", 2026-10-09; S3).**
One set of controls is live at a time. Code: the pill and the right-click menu grey both rows off the pan's Clarity state; the renderer applies the Black/Gain offsets (`SpectrumWidget.cpp:7576-7577`) only while Clarity drives the levels, so with Clarity off the waterfall uses Low and High (or AGC / NF-AGC) as Thetis does.

**D14. Clarity holds its last level while paused, on every pan; there is no hand-off to AGC (Q1).**
Code: stop clearing `clarityActive` on `pausedChanged(true)` (`PanClarityRegistry.cpp:254-260`) and in `PanClarityRegistry::setKeyed` (`:453`, which today clears it on every pan, not only the transmitting one). `canOutput` still blocks new output while keyed (`:144`). Today the only pause source is transmit (`ClarityController.cpp:45-48`; `notifyManualOverride()` at `:64-71` has no caller). The transmitting pan shows the TX levels through `setMoxOverlay(true)` (`MoxDisplayController.cpp:337`, drawn at `SpectrumWidget.cpp:7544-7560`); the stored receive level stays underneath. The MOX-rise `setClarityActive(false)` at `MoxDisplayController.cpp:413` goes too. In a remote window this also stops `coreWaterfallLevelsInUse` flipping (`SpectrumWidget.h:886-890`), which today makes the Core rebuild the window's endpoints at every key and un-key (`RemoteMediaController.cpp:546`, `:697`). The un-key behaviour is D17.

**D15. Waterfall AGC moves only the low edge, as Thetis does (plain fix).**
With AGC on and Clarity off, High stays the user's High. Thetis: `high_threshold = waterfall_high_threshold;` (`display.cs:6463`), low from the smoothed minimum less the offset (`display.cs:6466-6474`, lerp `:7666`). Code: `WaterfallLevelFollower::compose` stops writing `activeHighDbm` in the AGC branch (`DisplayFollowers.cpp:253-273`). Fixes P14.

**D16. This model is a source-first exception (Q1, JJ 2026-10-09).** Section 4.5.

### Un-key and Clarity tracking

**D17. Un-key restores the pre-key level the Thetis way (Q2), with one hold and an FFT purge on every edge (Q12).** Section 5. Fixes P5. Applies to local windows, remote windows and the phone; section 5.2 says where each part runs.

**D17a. FFT input history is cleared on every key and un-key, on every pan, in the window and on the Core (Q12, JJ "a", 2026-10-09).** Thetis offers this only as a Setup option that is off by default (section 5.1); NereusSDR always does it, so a large FFT never shows the user's own transmission after un-key. Section 5.9 records it as ours.

**D18. A same-band re-centre keeps Clarity's smoothing; a stream, rate or band change still resets it (plain fix, audit H4).** Section 5.4. Fixes P6.

**D19. Per-band Clarity priming works (plain fix).**
Code: the window that collects samples must hold the 30 samples the check needs at the 500 ms cadence. The comment beside it already states that intent ("≥30 samples (≈ 15 s / cadence-0.5s = 30 ticks)", `PanClarityRegistry.cpp:226-228`), but the trim keeps only 2 s (`:233-236`). Fixes P7.

### FFT

**D20. Auto-zoom keeps about the slider's number of points on screen at every zoom; the slider sets normal detail and zoom adds to it the same way every time (Q4).**
Code: the slider writes the baseline only and replans through the same path as zoom, for every pan, on top of PR 372's `ControlRanges::autoZoomFftSize` (`MainWindow.cpp` auto-zoom lambda from `:8958`, cap at `:8987-8996`; today connected to the active widget only). A remote window's planner moves to the same rule: today it sizes for about one bin per screen pixel (`RemoteMediaController.cpp:604-632`, `plannedFftSize`), so the slider means something different there (S17).
Amended 2026-10-11 (D38): the slider is no longer the floor of the zoom plan. In Auto the plan's baseline is the built-in 4096 and the stored `DisplayFftSize` plays no part; the slider sets the size only in Fixed. Why: with the slider as a floor under a zoom plan that had already reached the largest size, the slider did nothing (a remote window at 1.536 MHz with a 35 kHz pan), and nothing on screen said so.

**D21. The zoom cap is a time per transform, about 0.7 s at every rate, with the engine maximum of 262144 on top (Q5).**
Done: PR 372, merged at `2a8a09958`. `ControlRanges::kAutoZoomMaxTransformSeconds = 0.7` and `autoZoomMaxFftSize(rate)` (`src/core/ControlRanges.h:262-294`) cap local and remote zoom alike. It supersedes the 2026-05-08 fixed-65536 decision. D20, D22 and D26 build on it.

**D22. The FFT size slider applies on release, not while dragging (settled call 6; plain fix).**
Code: `setTracking(false)` on the slider (pill and Setup, `DisplaySetupPages.cpp:580`), so a drag replans once. Keyboard steps (arrows, Page Up/Down) on a non-tracking slider still change the value each press, so they apply 300 ms after the last key (S19). In a remote window the station key is then written once per release. Fixes the "lethargic" half of P9.

**D23. The readout under the FFT size says which control is in charge (settled call 7; Q4).**
The Hz/bin target stays an optional override, off by default, with the slider as the floor (JJ 2026-05-08; `docs/architecture/2026-09-23-station-link-v1.md:5348-5362`). When the target is what sets the size, the readout line says so. Otherwise it shows bin width and time per transform, as in the mockup ("1.46 Hz per bin · 0.68 s per transform at 192 kHz"). In a remote window the readout uses the size the Core granted, as Setup already does (`DisplaySetupPages.cpp:261-275`).
Amended 2026-10-11 (D38, D42): the target is no longer an override with the slider as its floor. It is one of three sizing modes, with the built-in 4096 as its floor, and the Sizing selector says which control is in charge. The readout always shows the bin width and time per transform really running; its "Hz/bin target is setting the size" variant goes away with the selector's arrival.

**D24. The FFT size survives a restart (issue #296; plain fix).**
Code: load the slider from `fftSizeBaseline()` / the stored `DisplayFftSize`, never from the zoomed `fftSize()` (`DisplaySetupPages.cpp:310-316`). The reported reset to 16384 is not yet reproduced on main; the plan reproduces it first (root cause unverified).

### Setup and shared state

**D25. FFT tab rows are shared by every pan and every window on the Core. They are greyed with "Connect to the Core to change these." when the Core cannot be reached (settled call 5).**
Station keys: `DisplayFftSize`, `DisplayFftWindow`, `DisplayHzPerBinTarget`, `DisplaySpectrumFps` (`src/core/settings/SettingsScope.cpp:498-501`). Decimation has no stored key on the window; each window's engines hold it (`src/core/ControlRanges.h:383-390`). In a remote window, Decimation is greyed with "This Core is too old to change Decimation." when the Core does not offer it (capability gate `RemoteMediaController.cpp:1044-1046`; phone gate `resources/setup/display.json:205`). The FFT tab header carries the tag ALL PANS.

**D26. Setup pushes reach every pan (plain fix).**
The per-pan model stands: Setup holds the defaults, a pan may override them, and station keys are shared (`docs/architecture/thetis-display-dsp-parity-design.md:85-97`; `station-link-v1.md:5331-5341`). Code: FFT controls act on the engine pool, as Decimation already does (`DisplaySetupPages.cpp:910-918`), not on `model()->fftEngine()`. Grid & Scales stops using `firstPan` (`DisplaySetupPages.cpp:2154` and the lambdas at `:2265`, `:2282`, `:2299`, `:2310`). Fixes P10.

**D27. Setup updates live when a value changes from the pan (plain fix).**
Code: every Display page subscribes to `DisplaySettingsModel` and widget signals, as the 3D View page does (`DisplaySetupPages.cpp:3334-3379`). Fixes P11.

**D28. Setup Cal Offset works again (plain fix).**
Code: keep the station's receive offset (`MainWindow.cpp:6861-6862`) and add the user's `DisplayCalOffset` (`SpectrumWidget.cpp:911`) on top as a separate term, so the per-frame write no longer erases it. Fixes P12. In a remote window the pill's Cal Offset row is greyed with Setup's reason, "The Core calibrates the display for its radio." (`DisplaySetupPages.cpp:1009`).

**D29. Per-band dB Max and dB Min work again and stay per band (plain fix; settled call 8).**
Code: connect `PanadapterModel::levelChanged` and `bandChanged` to `SpectrumWidget::setDbmRange` for every pan. The keys stay the Core's (`DisplayGridMax_<band>`, `DisplayGridMin_<band>`, `SettingsScope.cpp:484-489`). Fixes P13.

### Additions, menu, remote

**D30. Five AetherSDR additions, all of them (Q10).** NB Blank, Background, Grid spacing, Scale text, separate trace colour. Sections 3.3, 3.5 and 3.7.

**D31. New controls start with nothing changed (settled call 13).** NB Blank off at 1.15. Grid spacing Auto. Scale text today's size. Trace colour equal to today's fill colour. Background off, opacity 80%, colour `#0a0a14`.

**D32. Background picture and its settings are saved per pan on this computer, like other display preferences; a remote window picks its own (settled call 9).**
The same holds for every addition and for Black Level and Color Gain: they are this computer's keys, never the Core's (station keys are only the four FFT keys and `DisplayGridMax_`/`DisplayGridMin_`, `SettingsScope.cpp:484-501`). The Core converts nothing in section 4.3.

**D33. The right-click pan menu is trimmed (Q11).** Section 7.

**D34. Remote windows get the same pill.** Section 8.

**D35. The VFO flag's meter keeps updating in a remote window that has no S-meter widget (plain fix).**
Code: move `if (!m_sMeter) { return; }` (`MeterPoller.cpp:1159`) below the per-flag loop, and give the flags the local fallback (S_AV, as `MainWindow.cpp:9365-9368` does). Fixes P15.

**D36. The iPhone parity doc lists the new controls so the phone's Display panel can follow in its own work (settled call 15).** Section 8.5.

**D37. Two bench checks, later, with JJ's OK.** The remote waterfall background that sometimes turns blue, and the S-meter Sig Avg check. The S-meter keeps the Thetis peak default with Sig Avg selectable (`docs/architecture/2026-05-18-pgxl-tgxl-and-analog-smeter-design.md:97-103`); nothing in this spec changes it. Section 11.3.

### FFT sizing mode (JJ's direction, 2026-10-11)

JJ found the FFT Size slider dead on a remote window at 1.536 MHz with a 35 kHz pan: the zoom plan was already at the largest size, so the slider, then the plan's floor, changed nothing. He asked for today's automatic behavior kept, a classic Thetis-style mode added, and the slider disabled whenever it is not what sets the size, on one three-position selector. These entries amend D20 and D23.

What Thetis does (v2.10.3.15, facts only; nothing is ported):

- The FFT Size slider is the only writer of the size: 7 positions, `4096 * 2^position`, 4096 to 262144 (`Console/setup.designer.cs:35165-35169`, `Console/setup.cs:16183-16207`; writers at `setup.cs:16189`, `:16215`, `:18179`).
- Zoom never changes the size; it only changes the analyzer's span clip (`Console/HPSDR/specHPSDR.cs:420-429`, `:557-568`; `Console/console.cs:33688-33695`).
- With more pixels than bins WDSP interpolates linearly between bins (`wdsp/analyzer.c:444-459`).
- The bin width readout is sample rate / FFT size (`setup.cs:16183-16207`).
- No automatic sizing and no Hz/bin target exist. Limits are fixed whatever the rate (`ChannelMaster/cmaster.c:192-198`).

**D38. One station setting, `DisplayFftSizingMode`, chooses how the running FFT size is set: `Auto`, `HzPerBin` or `Fixed`.** Station scope, like `DisplayFftSize`. Exactly one control sets the running size in each mode.

| Mode | Running FFT size |
| --- | --- |
| `Auto` (default) | The zoom plan with the baseline fixed at `kDisplayFftSizeDefault` (4096): wanted points = default x rate / span, rounded up to a power of two, capped at the time-per-transform cap (D21). The stored `DisplayFftSize` plays no part. |
| `HzPerBin` | The Hz/bin target plan (rate / target, rounded up, the same cap and limits), with the floor fixed at `kDisplayFftSizeDefault`. The stored `DisplayFftSize` plays no part. |
| `Fixed` | Exactly the stored `DisplayFftSize`. Zoom and span never change it. No time-per-transform cap, no hysteresis. As Thetis. |

`DisplayFftSize`'s range, positions and default are unchanged. The mini display and the transmit display keep their own rules.

**D39. The Hz/bin target keeps its old meaning on the wire.** `DisplayHzPerBinTarget` still means what it means to an older window or phone (above 0 = the target is in charge, 0 = off): it holds the target while the mode is `HzPerBin` and 0 otherwise. The last chosen target is remembered in a new station key, `DisplayHzPerBinLastTarget`, so leaving and re-entering Hz/bin restores it; with none remembered, entering Hz/bin starts from the bin width running now.

**D40. With the mode key absent, the mode is read from the target; nothing is migrated.** `DisplayHzPerBinTarget` above 0 gives `HzPerBin`; anything else gives `Auto`. A stored `DisplayFftSize` above the default is kept untouched as the Fixed slider's value. The rule is applied on read, in one function every reader uses (`readDisplayFftSizing`, `src/core/DisplayFftSizing.h`, over `ControlRanges::displayFftSizingModeOnRead`). A stored `HzPerBin` with no target above 0 also reads `Auto`, so what is read is always what runs. No settings schema change.

**D41. A client that does not know the mode moves it by writing the target.** A phone on an older Display description, or an older window, writes only `DisplayHzPerBinTarget`. Where the Core applies a station setting write, a write of that key moves the mode: above 0 sets `HzPerBin`; 0 sets `Auto` if the mode was `HzPerBin` (`Fixed` is left alone). Each settings write on the station link carries one key and the Core cannot tell which kind of client wrote it, so the rule runs on every write of that key. A client that knows the mode writes the mode first and the target second, which the rule leaves as written.

**D42. One planner for everything that plans a size.** `ControlRanges::planDisplayFft(mode, storedSize, rate, span, target)` (`src/core/ControlRanges.h`) is used by the local window, the remote window, Setup's readouts and the pill's readout, and has a Swift twin on the phone with a shared table of test vectors. A remote window asks the Core for the "fine" engine exactly when its planned size exceeds the size a plain request would get: in `Fixed` the plan equals the stored size, so it asks "wide" at that size; in `Auto` and `HzPerBin` the comparison uses the built-in default where it used the stored size. The Core grants a `Fixed` request unchanged (`min(request, 262144)`, or the shared engine's size).

**D43. The FFT size offset and the noise-floor fast attack follow the mode.** The offset (2 dB per step above 4096) belongs to the size the mode's control chose: the stored size in `Fixed`, the built-in 4096 (0 dB) in `Auto` and `HzPerBin`. A mode change sets it and restarts the fast attack once, at the same seam every FFT size change uses (`FFTEngine::setFftSizeBaseline`).

**D44. Controls.** The pill's FFT tab and Setup > Display > Spectrum Defaults get a "Sizing:" selector of three exclusive choices (Auto, Hz/bin, Fixed) in place of the Hz/bin Off/On button and Setup's "Off" value. The FFT Size slider is enabled only in `Fixed`; grayed otherwise, it shows the size running for the pan's stream. The Hz/bin control is enabled only in `HzPerBin`; grayed otherwise, it shows the remembered target. Disabled, never hidden; a station-unavailable reason still wins. The line under the FFT Size slider always shows the bin width and time per transform really running, identical on a local and a remote window.

**D45. Auto AGC-T reads a noise floor referred to a 4096-point FFT (JJ, 2026-10-11).** WDSP reads the AGC threshold as the noise in one bin of width rate / size (`third_party/wdsp/src/wcpAGC.c:504-515`), and `RxChannel` always gives it size 4096. `FFTEngine` normalizes by the window's coherent gain only, so the noise in each bin falls 10 x log10(2) = 3.01 dB each time the FFT size doubles, and the running size follows the zoom. So each frame's mean is referred to 4096 before Auto AGC-T uses it: mean + 10 x log10(N / 4096), where N is the frame's size. `NoiseFloorTracker` keeps both floors with the same lerp and the same fast attack: `noiseFloor()` is the floor per bin at the running size, which is what the pan draws and what every other reader keeps; `agcNoiseFloor()` is the referred one, read by `RadioModel::updateAutoAgc`. The "NF x dB" line beside AGC-T on the VFO flag and the RX applet in a local window (`MainWindow::refreshAutoAgcVisuals`) shows that same referred floor, since it explains the threshold, and so reads what a remote window reads for the same band. The correction is made per frame, so a zoom needs no restart and the threshold never lags the size. The display's decimation is not part of it: it drops samples without a filter, so the whole band's noise folds into the bins that remain and the level in a bin depends on N alone.

The tick's cal offset is the 2.0 alone (0 with AGC off): the 2 dB per slider step term is gone from Auto AGC-T. A local window at any running size and the Core, whose own Auto AGC FFT is 4096 (`DaemonAgcSource`), now use one formula and give one threshold for the same band noise. `FFTEngine::fftSizeOffsetDb` is still stored as D43 says; nothing reads it at present.

This differs from Thetis on purpose. Thetis gives WDSP its real display FFT size (`console.cs:46055-46056` [v2.10.3.15]; the old literal shows as `size/*4096.0*/` at `display.cs:9879-9880`) and still subtracts `Display.RX1FFTSizeOffset`, the slider's position times 2 dB (`console.cs:33346-33376`, `setup.cs:16195`). NereusSDR gives WDSP a fixed 4096 and its running size is not the slider's, so neither Thetis term fits: with them the threshold fell 3 dB per doubling as a pan zoomed in (15 dB from the full span to 6 kHz at 192 kHz) and by 1 dB per slider step in Fixed.

---

**D46. The phone is told the mode: Setup description version 28 and the station catalog (2026-10-11).** Display's description gains a `Sizing` row bound to `DisplayFftSizingMode` (text-valued options `Auto`, `HzPerBin`, `Fixed`), first in the Fast Fourier Transform group; `Size` is gated on `Fixed` and `Hz/bin Target` on `HzPerBin` with `enabledWhen`, and `Hz/bin Target` loses its "Off" value (minimum 0.5). The same version levels the two averaging times with the desktop (1 ms in steps of 1), and the Core's floor for an averaging time in a display request drops to 1 ms with it. A peer below 28 is sent the rows and the order it was sent before, byte for byte, and keeps working through D41. The station catalog's `display` object lists the same Sizing control and names the remembered target's key on the Hz/bin Target control (`rememberedKey`); that control keeps its `offValue` 0 there, because a catalog has no version to fit to and an older phone reads 0 as "Off". Details: `docs/architecture/2026-09-23-setup-description-v1.md` (Version 28) and `docs/architecture/2026-09-23-station-link-v1.md` section 7.4.

## 3. The Display pill (layout B)

### 3.1 Frame

- Opens from the "Display" button on the pan's left strip, as today (`SpectrumOverlayPanel.cpp:818-836`).
- Panel style, width and colours as the mockup: 268 px wide, `rgba(15,15,26,0.863)` background, `#304050` border, radius 3. The mockup's CSS header says where each value comes from (`src/gui/StyleConstants.h`, `SpectrumOverlayPanel.cpp` `OverlayColors` and `buildDisplayFlyout`, AetherSDR `SpectrumOverlayMenu.cpp:1261-2043`).
- **Tab strip:** four equal buttons, Spectrum | FFT | Waterfall | 3D View, 22 px high, in the menu-button idiom of the left strip (`rgba(20,30,45,0.94)`, active `rgba(0,112,192,0.706)` with `#0090e0` border).
- **Each tab** is one AetherSDR-style 4-column grid: label | button or swatch | slider | value. Margins 8/6, spacing 4 (AetherSDR `src/gui/SpectrumOverlayMenu.cpp:1300-1303`).
- **Header** at the top of each tab: accent `#00b4d8`, 9 px bold small caps, with a 1 px divider (AetherSDR `makeHeader`, `SpectrumOverlayMenu.cpp:1387-1406`). The FFT header carries the tag **ALL PANS** on the right.
- **Every tab fits a pan without scrolling.** The Spectrum tab with its additions is about 474 px.
- **Footer on every tab:** a divider, then **Reset this pan** (button, left) and **Setup defaults →** (link, right). D5, D6.
- **Disabled rows** use the greyed colours of the mockup (`#4a5a6a` text, `#141e28` pill) and stay visible.

### 3.2 Store legend

| Store | Meaning | Where |
| --- | --- | --- |
| **pan** | Per-pan key, saved on this computer. An untouched pan inherits pan 0's value. | `settingsKey()` and `rawValue`, `SpectrumWidget.cpp:711-717`, `:745-761`; display model keys `src/models/DisplaySettingsModel.cpp:247-311` |
| **global** | One key for every pan on this computer. | Read straight off `AppSettings`, `SpectrumWidget.cpp:1092-1176` |
| **station** | The Core's key, shared by every pan and window on the Core. Greyed when the Core cannot be reached. | `SettingsScope.cpp:484-489`, `:498-501` |
| **band** | Per band, inside a station key. | `DisplayGridMax_<band>`, `DisplayGridMin_<band>`; 3D Floor per band via `PanadapterModel` |
| **engine** | Held by each window's engines, not stored on the window. | Decimation, `ControlRanges.h:383-385` |
| **new** | A new key this spec adds. Names marked "proposed" are for the plan to confirm. | |

Ranges and defaults are today's code values. Where the mockup draws a different value it is noted; the mockup's numbers are illustrations.

### 3.3 Spectrum tab

Header "Spectrum". Rows in mockup order. Rows marked **(+n)** are addition n.

| Row | Control | Range | Default | Shown as | Store, key |
| --- | --- | --- | --- | --- | --- |
| Fill | toggle pill | on/off | on | pill | pan, `DisplayPanFill` (`DisplaySettingsModel.cpp:256`) |
| Grid | toggle pill | on/off | on | pill | pan, `DisplayGridEnabled` (`SpectrumWidget.cpp:1012`) |
| Cursor Freq | toggle pill | on/off | on | pill | pan, `DisplayShowCursorFreq` (`SpectrumWidget.cpp:1016`) |
| Peak Hold | toggle pill | on/off | off | pill | pan, `DisplayPeakHoldEnabled` (`SpectrumWidget.cpp:912`) |
| Fill: | fill colour swatch + slider | 0 to 100 | 70 | `70%` | pan, `DisplayFillColor` (`SpectrumWidget.cpp:1069`), `DisplayFftFillAlpha` (`DisplaySettingsModel.cpp:255`) |
| dB Max: | slider | -200 to 0 | per band | `-40 dBm` | band, `DisplayGridMax_<band>` (D29) |
| dB Min: | slider | -200 to 0 | per band | `-140 dBm` | band, `DisplayGridMin_<band>` (D29) |
| note | text | | | "dB Max and Min are kept per band (20 m)." with the current band | |
| Gradient | toggle pill | on/off | off | pill | pan, `DisplayGradientEnabled` (`SpectrumWidget.cpp:913`) |
| Noise Floor | toggle pill | on/off | off | pill | global, `DisplayShowNoiseFloor` (`SpectrumWidget.cpp:1095`) |
| Bin Width | toggle pill | on/off | off | pill | global, `DisplayShowBinWidth` (`SpectrumWidget.cpp:1092`) |
| Peak Value | toggle pill | on/off | off | pill | global, `DisplayShowPeakValueOverlay` (`SpectrumWidget.cpp:1141`) |
| Line: **(+5)** | trace colour swatch + width slider | 0.1 to 5.0 px in 0.1 steps (ruling 2026-10-10; the mockup drew 0.5 to 3) | 1.0 px (ruling 2026-10-11, Thetis's `udDisplayLineWidth` default; it was 1.5) | `1.0 px` | pan, `DisplayLineWidth` (`SpectrumWidget.cpp:910`); trace colour new, proposed `DisplayTraceColor` |
| Detector: | combo | Peak, Rosenfell, Average, Sample, RMS | Peak | name | pan, `DisplaySpectrumDetector` (`SpectrumWidget.cpp:885`) |
| Averaging: | combo | None, Recursive, Time Window, Log Recursive | Log Recursive | name | pan, `DisplaySpectrumAveraging` (`SpectrumWidget.cpp:890`) |
| Avg Time: | slider | 10 to 9999 ms | 30 | `ms` | pan, `DisplaySpectrumAverageTimeMs` (`SpectrumWidget.cpp:876`) |
| Cal Offset: | slider; greyed in a remote window (D28) | -30 to 30 dB | 0.0 | `0.0 dB` | pan, `DisplayCalOffset` (`SpectrumWidget.cpp:911`); D28 |
| Peak Delay: | slider, greyed while Peak Hold is off | 100 to 10000 ms | 2000 | `ms` | pan, `DisplayPeakHoldResetMs` (`SpectrumWidget.cpp:909`) |
| Grid: **(+3)** | combo | Auto, 1, 2, 5, 10, 25, 50, 100 kHz | Auto | name | pan, new, proposed `DisplayFreqGridSpacing` |
| Scale text: **(+4)** | combo | 8, 9, 10, 11, 12, 14 pt (AetherSDR) | today's size | name | pan, new, proposed `DisplayFreqScaleFontPt` |
| *Background* header **(+2)** | sub-header | | | | |
| Background: **(+2)** | Choose... / Clear / Off | | Off | buttons | pan on this computer, new |
| BG Opacity: **(+2)** | slider | 0 to 100 | 80 | `80%` | pan on this computer, new |
| BG Color: **(+2)** | swatch | any colour | `#0a0a14` | swatch | pan on this computer, new |

Notes:
- Averaging defaults to Log Recursive here because that is the widget's load default; the station-link table lists the same (`station-link-v1.md`, Spectrum Averaging default 3).
- Line width: Setup offers 1 to 3 px in whole pixels today (`DisplaySetupPages.cpp:956`), while the widget accepts 0.5 to 8 with a 1.5 default (`SpectrumWidget.cpp:2764-2766`, `:910`). The mockup draws 0.5 to 3. Settled S11: 0.5 px steps from 0.5 to 3, in Setup too. Superseded by the maintainer's ruling of 2026-10-10: 0.1 to 5.0 px in steps of 0.1, in the pill and in Setup (the maximum and step of Thetis's `udDisplayLineWidth`, `setup.designer.cs:53016-53043` [v2.10.3.15]; the minimum is 0.1 because a zero-width trace is invisible). Both draw paths honour it: the GPU path draws the trace as a ribbon of the line width.
- Today's scale text is a 10 px font (`SpectrumWidget.cpp:5672`); the AetherSDR control is in points. Settled S11: 8 to 14 px, default 10.
- Grid spacing today is automatic from the visible span (`SpectrumWidget.cpp:5076-5083` for the grid, `:5677-5684` for the scale labels). "Auto" keeps that.

### 3.4 FFT tab

Header "FFT", tagged **ALL PANS**. Every row is shared by every pan and window on the Core (D25).

| Row | Control | Range | Default | Shown as | Store, key |
| --- | --- | --- | --- | --- | --- |
| Sizing: | three exclusive buttons: Auto, Hz/bin, Fixed (D38, D44) | | Auto | the lit button | station, `DisplayFftSizingMode` |
| FFT Size: | slider, applies on release (D22); live in Fixed only, grayed otherwise | positions 0 to 6 = 4096 to 262144 | 4096 (position 0) | the size, e.g. `131072`; grayed, the size running for this pan | station, `DisplayFftSize` |
| Hz/bin: | slider; live in Hz/bin only, grayed otherwise | 0.5 to 200, step 0.5 | none | `x.x Hz`; grayed, the remembered target, or with none the running bin width on the slider's step | station, `DisplayHzPerBinTarget` (remembered in `DisplayHzPerBinLastTarget`) |
| readout | text line under the two sizing controls | | | bin width and time per transform really running, e.g. "1.46 Hz per bin · 0.68 s per transform at 192 kHz", in every mode (D44) | computed |
| FPS: | slider | 10 to 60 | 30 | number | station, `DisplaySpectrumFps` |
| Window: | combo | Rectangular, Blackman-Harris 4T, Hann, Flat-Top, Hamming, Kaiser, Blackman-Harris 7T | Blackman-Harris 4T (1) | name | station, `DisplayFftWindow` |
| Decimation: | slider; greyed in a remote window when the Core does not offer it (D25) | 1 to 16 | 1 | number | engine (`ControlRanges.h:383-390`) |

The mockup draws FFT Size at position 5 (131072) and Window as Blackman-Harris 7; both are illustrations.

Row order since 2026-10-11: the three rows that size the FFT (Sizing, FFT Size, Hz/bin) sit together with the running numbers under the pair, then FPS, Window and Decimation. Setup > Display > Spectrum Defaults' "Fast Fourier Transform" group reads Sizing, Size, Hz/bin Target, Window, and so does Setup description version 28 (D46).

### 3.5 Waterfall tab

Header "Waterfall". The mockup shows the tab with Clarity on.

| Row | Control | Range | Default | Shown as | Store, key |
| --- | --- | --- | --- | --- | --- |
| Scheme: | combo | all eight `wfSchemeName()` entries | Default | name | pan, `DisplayWfColorScheme` (`DisplaySettingsModel.cpp:247`) |
| Clarity: | status badge "C" + On pill + Re-tune button | on/off | on | badge green active, amber paused | global, `ClarityEnabled` (`RadioModel.cpp:1692`); Re-tune via `PanClarityRegistry::retunePan` (`PanClarityRegistry.cpp:458`) |
| Black Level: | slider | 0 to 100 (mockup) | converted look, section 4.3 | offset from the noise, e.g. `N+3 dB` | pan, `DisplayWfBlackLevel` (converted, section 4.3) |
| Color Gain: | slider | 0 to 100 | converted look, section 4.3 | colour span, e.g. `38 dB` | pan, `DisplayWfColorGain` (converted, section 4.3) |
| note | text, shown while Clarity is on | | | "Clarity is setting the levels. Turn it off to use these." | |
| AGC: | Off/On pill; greyed while Clarity is on | on/off | on | pill | pan, `DisplayWfAgc` (`SpectrumWidget.cpp:927`) |
| NF-AGC: | Off/On pill + offset slider; greyed while Clarity is on | -60 to 60 dB | off, 0 | `0 dB` | pan, `WaterfallNFAGCEnabled`, `WaterfallAGCOffsetDb` (`SpectrumWidget.cpp:930-931`) |
| Low: | slider; greyed while Clarity is on | -200 to 0 dBm | -122 | `dBm` | pan, `DisplayWfLowLevel` (`SpectrumWidget.cpp:843`) |
| High: | slider; greyed while Clarity is on | -200 to 0 dBm | -62 | `dBm` | pan, `DisplayWfHighLevel` (`SpectrumWidget.cpp:842`) |
| Use spectrum min/max | toggle pill; greyed while Clarity is on; while on, greys AGC, NF-AGC, Low and High with "The spectrum's dB Max and Min are setting the levels." | on/off | off | pill | pan, `DisplayWfUseSpectrumMinMax` (`SpectrumWidget.cpp:981`); S12 |
| Rate: | slider | 10 to 500 ms | 30 | `ms` | pan, `DisplayWfUpdatePeriodMs` (`SpectrumWidget.cpp:934`) |
| NB Blank: **(+1)** | Off/On pill + threshold slider | 5 to 95, shown as 1.05 to 1.95 | off, 15 (1.15) | `1.15` | pan, new, proposed `WaterfallBlankingEnabled`, `WaterfallBlankingThreshold` |
| WF Avg: | combo | None, Recursive, Time Window, Log Recursive | None | name | pan, `DisplayWaterfallAveraging` (`SpectrumWidget.cpp:900`) |
| WF Avg Time: | slider | 10 to 9999 ms | 120 | `ms` | pan, `DisplayWaterfallAverageTimeMs` (`SpectrumWidget.cpp:878`) |
| Opacity: | slider | 0 to 100 | 100 | `%` | pan, `DisplayWfOpacity` (`SpectrumWidget.cpp:933`) |
| Stop on TX | toggle pill | on/off | off | pill | pan, `WaterfallStopOnTx` (`SpectrumWidget.cpp:932`) |
| RX Filter | toggle pill | on/off | off | pill | pan, `DisplayShowRxFilterOnWaterfall` (`SpectrumWidget.cpp:994`) |
| TX Filter | toggle pill | on/off | on | pill | pan, `DisplayShowTxFilterOnRxWaterfall` (`SpectrumWidget.cpp:998`) |
| RX Zero | toggle pill | on/off | off | pill | pan, `DisplayShowRxZeroLine` (`SpectrumWidget.cpp:1008`) |
| TX Zero | toggle pill | on/off | off | pill | pan, `DisplayShowTxZeroLine` (`SpectrumWidget.cpp:1009`) |
| Timestamp: | combo | None, Left, ... (Setup's list, `DisplaySetupPages.cpp:2064`) | None | name | pan, `DisplayWfTimestampPos` (`SpectrumWidget.cpp:986`) |
| Rewind: | combo | 60 s, 5, 15, 20 min | 20 min | name | pan, `DisplayWaterfallHistoryMs` (`SpectrumWidget.cpp:977-980`; `SpectrumWidget.h:2824`) |

When Clarity is off, that note goes away, AGC, NF-AGC, Low and High are live, and they behave as Thetis (D13, D15). Black Level and Color Gain grey instead, with "Clarity is off: Low and High set the levels." (D13a). The mockup's Low -130 and High -40 are Setup's pre-load initials; the loaded defaults are -122 and -62.

### 3.6 3D View tab

Header "3D View". The same seven controls as today's right-click 3D section and Setup's 3D View page.

| Row | Control | Range | Default | Shown as | Store, key |
| --- | --- | --- | --- | --- | --- |
| Spectrum: | combo | 2D Waterfall, 3D Stacked Trace | 2D Waterfall | name | pan, `DisplaySpectrumRenderMode` (`DisplaySettingsModel.cpp:259`) |
| 3D Floor: | slider | 0 to 24 dB | 6 | `dB` | band, per pan (`DisplaySettingsModel.h:281`; per-band recall, see below) |
| 3D Gain: | slider | 0 to 100 | 70 | number | pan (`DisplaySettingsModel.h:282`) |
| 3D Span: | slider; disabled on the CPU fallback | 0 to 100 | 100 | number | pan (`DisplaySettingsModel.h:283`; CPU gate `SpectrumWidget.cpp:10172-10182`) |
| 3D Angle: | slider | 0 to 100 | 50 | number | pan (`DisplaySettingsModel.h:284`) |
| 3D Speed: | slider | 0 to 10, 0 is Match | Match | `Match` or number | pan (`DisplaySettingsModel.h:285`) |
| Slice Shadow | toggle pill; disabled on the CPU fallback | on/off | off | pill | pan (`DisplaySettingsModel.h:286`) |

Notes:
- Slice Shadow is drawn only by the GPU path (`SpectrumWidget.cpp:12063-12088`), so on the CPU fallback it is disabled like 3D Span. That fixes a dead control from the right-click menu.
- 3D Floor per-band recall is wired for pan 0 only today (`MainWindow.cpp:8675`, `:4654-4676`). With D26 it is wired for every pan.
- The mockup draws Slice Shadow on; the code default is off.

### 3.7 The five AetherSDR additions

All five are **ports from AetherSDR** and need full attribution under `docs/attribution/HOW-TO-PORT.md`. AetherSDR has no per-file headers, so rule 6 applies: in the same commit, each NereusSDR file that takes the code carries the AetherSDR project-level attribution block (as `SpectrumOverlayPanel.cpp:8-21` already does), a Modification-history entry with the date, J.J. Boyd (KG4VCF) and the AI tooling, and inline `// From AetherSDR <file>:<line> [@1e0718ad]` cites on each ported block and constant. Upstream `//` comments inside ported logic are copied verbatim.

| # | Addition | AetherSDR source (`@1e0718ad`) | What NereusSDR adapts |
| --- | --- | --- | --- |
| 1 | **NB Blank** (waterfall impulse blanker), Waterfall tab | Panel row: `src/gui/SpectrumOverlayMenu.cpp:1600-1613`; wiring `src/gui/MainWindow_Wiring.cpp:4061-4064`. Setters: `src/gui/SpectrumWidget.cpp:4672-4699`. Blanker: `SpectrumWidget.cpp:8428-8460` (frozen during TX; rolling baseline of row means; impulse when the row mean exceeds baseline x threshold; replaced by the last good row). Threshold clamp 1.05 to 2.0, slider 5 to 95 mapped to 1.00 + v/100. | AetherSDR works on Flex intensity tiles; NereusSDR rows are dBm. Settled S5: compare on linear power, so the 1.05 to 1.95 threshold keeps its AetherSDR meaning. |
| 2 | **Background**: Choose... / Clear / Off, BG Opacity, BG Color; Spectrum tab | Panel rows: `SpectrumOverlayMenu.cpp:1727-1760` (buttons), `:1764-1783` (opacity), `:1786-1814` (colour); wiring `MainWindow_Wiring.cpp:4065-4105`. Widget: `setBackgroundImage` `SpectrumWidget.cpp:11099-11115`, `setBackgroundFillColor` `:11117-11125`, `setBackgroundOpacity` `src/gui/SpectrumWidget.h:437`. | Saved per pan on this computer (D32). AetherSDR's Clear reverts to its logo. Settled S6: no default picture ships; Clear removes the picture, Off hides it and keeps it chosen. |
| 3 | **Grid spacing** combo, Spectrum tab | Panel row: `SpectrumOverlayMenu.cpp:1820-1836`; setter `SpectrumWidget.cpp:3030-3036` (key `DisplayFreqGridSpacing`). | Auto keeps today's automatic step. |
| 4 | **Scale text** combo, Spectrum tab | Panel row: `SpectrumOverlayMenu.cpp:1840-1856`; setter `SpectrumWidget.cpp:3037-3044` (clamp 8 to 14 pt; the strip grows to fit, AetherSDR #3501). | Default is today's size. Settled S11: 8 to 14 px, default 10. NereusSDR's scale strip is 28 px (`SpectrumWidget.h:2870`). |
| 5 | **Separate trace colour**, swatch on the Line row | Panel row: `SpectrumOverlayMenu.cpp:1476-1532`; setter `SpectrumWidget.cpp:4135-4141` (key `DisplayFftLineColor`, AetherSDR #4239); GPU use `:14405-14407`. | Today trace and fill share `m_fillColor` (`SpectrumWidget.cpp:1069`; Setup's "Trace & Fill Color", `src/gui/setup/AppearanceSetupPages.cpp:63-74`). The trace colour starts equal to the fill colour (D31), so nothing changes on first open. |

New ports also need a provenance row. There is no general AetherSDR provenance table yet (the only one is `docs/attribution/AETHER-TELEMETRY-PROVENANCE.md`, for telemetry graphs). The plan adds the rows to a provenance file under `docs/attribution/` in the style of that file.

---

## 4. Waterfall level model under Clarity

### 4.1 Today

Clarity emits `low = N - 5`, `high = N + 55` around its smoothed floor N (`ClarityController.h:172-173`, `ClarityController.cpp:174-176`). The renderer then offsets that pair:

```
effectiveLow  = activeLow  + (125 - black) * 0.4      // SpectrumWidget.cpp:7576
effectiveHigh = activeHigh - gain * 0.3               // SpectrumWidget.cpp:7577
```

With the defaults (Black 104, Gain 45): black point N - 5 + 8.4 = **N + 3.4 dB**, top N + 55 - 13.5 = **N + 41.5 dB**, a span of **38.1 dB**. Black has 50 dB of authority, Gain 30 dB, and higher Black shows more noise.

### 4.2 The decided model (Q1, Q3)

Clarity keeps measuring the noise floor as it does now. Under Clarity:

- **Black Level** sets the black point as an offset from the noise floor N. The middle of the slider puts black **at the noise**. Up moves the black point above the noise, so up = blacker.
- **Color Gain** sets the colour span: the top is the black point plus the span. About **80 dB at Gain 0** down to about **10 dB at Gain 100**. This is AetherSDR's span (`visRange = 80.0f - m_wfColorGain * 0.7f; // 80 dB down to 10 dB`, AetherSDR `src/gui/SpectrumWidget.cpp:11593`). Settled S2: AetherSDR's straight line, 80 - 0.7 x Gain, default 60 (38 dB).
- Clarity holds its last level while paused (D14).
- AGC, NF-AGC, Low and High are greyed with the note while Clarity is on (D13).

The pill shows Black as `N+x dB` and Gain as the span in dB (mockup: `N+3 dB`, `38 dB`).

### 4.3 Defaults and conversion

- **Defaults:** the slider positions that reproduce today's look become the defaults: black point N + 3.4 dB, span 38.1 dB.
- **Conversion on first run:** every stored `DisplayWfBlackLevel[_N]` and `DisplayWfColorGain[_N]` (`DisplaySettingsModel.cpp:248-249`, `:291-292`) is converted once, so that under Clarity the black point and the top land where they were:

```
black offset (dB above N) = -5 + 0.4 * (125 - oldBlack)
span (dB)                 = 60 - 0.3 * oldGain - 0.4 * (125 - oldBlack)
```

  Each result maps to the nearest new slider position and clamps at the slider ends. The conversion is recorded once per settings file (a marker key) so it never runs twice. It runs before any pan loads its display settings.
- Slider scales (S1, S2): Black is 0 to 100 at 0.5 dB per step, 50 = black at the noise, default 57 (N + 3.5 dB). Gain is 80 - 0.7 x Gain dB, default 60 (38 dB). The mockup's knob positions (52, 45) predate these scales; its readouts (`N+3 dB`, `38 dB`) are right.

### 4.4 Shared AetherSDR bug, and why ours differs

AetherSDR's own comment says "higher black_level = more of the noise is black" (AetherSDR `src/gui/SpectrumWidget.cpp:11588`), but its formula shifts the floor by `(125 - m_wfBlackLevel) * 0.4f` with the note "inverted: 0=max shift, 125=no shift" (`:11592`), so higher actually shows more noise. NereusSDR copied the formula (`SpectrumWidget.cpp:7576`) and the mismatch with it. Ours differs on purpose: Black up = blacker (Q3), the comment and the tooltips agree with the code, and stored values are converted so the look does not jump.

### 4.5 Source-first exception (JJ's ruling, 2026-10-09)

This level model is **AetherSDR's, not Thetis's**. Thetis has no Black Level or Color Gain; it has Low and High levels (`display.cs:2528-2548`) and a waterfall AGC that moves only the low edge (`display.cs:6463-6478`). On 2026-10-09 JJ ruled (Q1, then confirmed "a" after the reconciliation) to keep the AetherSDR Black/Gain model under Clarity, because Clarity itself has no Thetis equivalent and the Thetis knobs do nothing useful while it drives the levels. When Clarity is off, Thetis's Low, High, AGC and NF-AGC apply unchanged.

This supersedes:
- `docs/architecture/waterfall-tuning.md:157-170`: "Legacy AGC yields via `m_clarityActive`" and AGC resuming when Clarity is paused. AGC is now greyed under Clarity, and Clarity holds while paused.
- The April 2026 framing "AetherSDR look, Thetis knobs underneath; Clarity is an addition".
- `docs/architecture/2026-09-23-iphone-app-display-parity.md:18`: the formulas `low + (125-black)*0.4, high - gain*0.3`.
- `docs/architecture/tx-display-settings-master-plan.md:431`: the same receive formula in the TX display plan. The TX branch (`effectiveLow = lowThreshold`) is unchanged.

---

## 5. Key and un-key (Q2, Q12)

### 5.1 What Thetis does

On a MOX edge Thetis's display:

- restores the cached waterfall level for the band and the RX/TX context (`display.cs:1585-1595`; cache `:2810-2829`, written back at `:7682-7689`);
- calls `PurgeBuffers` (`display.cs:11630-11639`), which clears the display arrays and peaks, starts the noise-floor fast attack and sets the waterfall AGC delay;
- suppresses waterfall AGC for the FFT fill time plus two frames (`//delay waterfall agc`, `display.cs:2856-2858`; honoured through `bIgnoreAgc` at `:7640-7651`);
- treats the noise floor as settled only after `Math.Max(1000f, fftFillTime + ...)` (`display.cs:5912`). While it is not settled, NF-AGC uses the cached minimum (`display.cs:6466-6470`).

The analyzer itself (the FFT input history) is reset only when `_wdsp_mox_transition_buffer_clear` is set (`console.cs:24246`, "to remove possibilty of tx data being in the rx buffers, and vice versa"). That flag defaults to false (`console.cs:24132`) and is Setup's "Purge WDSP buffers on MOX" (`setup.cs:28338`), off by default. **Thetis by default does not purge the FFT on a MOX edge**; its display hold lets old data age out.

With the purge on, Thetis doubles the settle time: `Math.Max(1000f, fftFillTime + (_wdsp_mox_transition_buffer_clear ? fftFillTime : 0f))` (`display.cs:5912`). NereusSDR always purges (D17a), so its hold is that case: `max(1000 ms, 2 x fill time)`.

Thetis's "two extra frames" term in the AGC delay is `(m_nFps / 1000f) * 2` (`display.cs:2857`), which at 30 fps adds about 0.06 ms, not two frames. The one hold (5.5) is always at least the fill time plus two real frame periods once the fill time is two frames or more, and 1000 ms covers shorter fills, so NereusSDR does not port that term. The code comment at the hold records the upstream bug and why ours differs (CLAUDE.md: a bug an upstream shares is fixed in our copy).

### 5.2 Where each part runs

| Part | Local window | Desktop remote window | iPhone |
| --- | --- | --- | --- |
| Clarity | The window's `PanClarityRegistry` | **The window's** `PanClarityRegistry`, fed by the Core's noise-floor message (`PanClarityRegistry::feedRemoteFloor`, `PanClarityRegistry.cpp:392-403`; `RemoteMediaController.cpp:6313-6321`) | The Core's `ClarityController` inside `DisplayExtrasProcessor` (`DisplayExtras.cpp:584-593`) |
| AGC and NF-AGC | The window's `WaterfallLevelFollower` | **The Core's** `DisplayExtrasProcessor`; the window asks for `agc` / `noiseFloorAgc` levels (`RemoteMediaController.cpp:697-705`) | The Core |
| FFT engine | The window's engine pool | **The Core's** source engine (`DaemonSpectrumSource`) | The Core |
| Per-band Clarity save (D19) | The window | The window | Not changed |
| Clarity's per-row slew and calibrated floor (S8), same-band re-centre (D18) | The window | The window | The Core (re-requests: 5.6) |
| AGC moves only the low edge (D15), NF-AGC with Thetis semantics (S7) | The window | The Core (the same `WaterfallLevelFollower`, run through `DisplayExtrasProcessor::m_levels`) | The Core |
| TX display | `MoxDisplayController` | Unchanged: the Core's transmit display; on a Core that sends none, receive frames are held and the pan shows a status line (`TxDisplaySource.cpp:201-209`, `SpectrumWidget.h:1383-1387`, `PanStatusText.cpp:203-205`) | Unchanged |

Every rule below applies wherever that part runs. A desktop remote window therefore needs work on both sides: Clarity in the window, AGC, NF-AGC and the FFT purge on the Core.

### 5.3 The key edges

- **Display edge.** `MoxController::moxStateChanged` fires after the RF delay on the rise (`MoxController.cpp:2317-2319`) and after `rxReady` on the fall (`:2404-2406`). `MoxDisplayController` receives it queued (`MoxDisplayController.cpp:643-646`) and calls `PanClarityRegistry::setKeyed` (`MainWindow.cpp:4003-4005`, `:8558-8560`). Every rule in this section runs on this edge. The hardware flip and ATT-on-TX act earlier, at `hardwareFlipped` (`MoxController.cpp:1417`, `:2378`), and are unchanged.
- **Every key source** reaches that edge through `MoxController`: MOX, TUNE (`MoxController.cpp:1032`), two-tone, VOX, and TCI, CAT and hardware PTT through `PttSource`. Tests key through at least MOX, TUNE and a TCI or CAT source.
- **The existing noise-floor fast attack** is a flag in `NoiseFloorFollower`, not a timer: it clears once the estimate has converged and at least `kFastAttackMinMs` (1000 ms) has passed (`DisplayFollowers.cpp:191-201`, `DisplayFollowers.h:131`). It drives the grid-follow noise floor, not Clarity (Clarity takes raw engine bins, `PanClarityRegistry.cpp:367-375`). It fires on a band change, a jump of more than 0.5 MHz, an attenuator change and either MOX edge (`MainWindow.cpp:8616-8621`, `:8922-8931`; the Core's `NoiseFloorFastAttackTrigger`, `DisplayFollowers.cpp:210-229`). Today the MOX trigger listens to `TransmitModel::moxChanged` and reaches the active pan only. Under this spec the MOX trigger moves to the display edge, reaches every pan, and its minimum on a MOX edge becomes the hold length of 5.5; band, tune and attenuator triggers keep their 1000 ms minimum and do not affect Clarity.
- **A key shorter than the RF delay** (`kRfDelayMs = 30`, `MoxController.h:398`) produces no display edge (`setKeyed(false)` is a no-op, `MoxDisplayController.cpp:201-212`). Nothing changes on screen for such a key, and a test says so.
- **Order.** `keyedChanged` (`MoxDisplayController.cpp:215`) runs before `fall()` (`:235`); the restore relies on that, and a test pins it.
- **The Core** today feeds `inputs.mox` from `transmitModel().isMox()` (`DaemonMediaController.cpp:5080`), which changes before the RF delay, so a short key would still reach the Core's rules. The Core-side rules (AGC freeze and restore, purge, hold) run on `MoxController::moxStateChanged` instead, which `DaemonMediaController` already connects (`:855`), so R-DSP-53 holds on the Core too.

### 5.4 On key (rise)

1. **Every pan keeps its Clarity level** (D14). No pan falls to AGC, NF-AGC or manual levels during a key. Remove the clear in `PanClarityRegistry::setKeyed` (`PanClarityRegistry.cpp:453`) and the rise's `setClarityActive(false)` (`MoxDisplayController.cpp:413`). The transmitting pan shows the TX levels through `setMoxOverlay(true)`.
2. **The AGC run level is kept, not reset.** Remove `resetWaterfallAgc()` on the rise (`MoxDisplayController.cpp:428`). The level is saved for the current band in the receive context, as Thetis's cache does. On the Core, `DisplayExtrasProcessor` stops calling `m_levels.resetAgc()` on each MOX change (`DisplayExtras.cpp:655-657`) and keeps the same per-band save.
3. **Nothing re-estimates while keyed, on any pan.** Clarity already skips frames while keyed (`keyedNow()`, `PanClarityRegistry.cpp:371`). AGC does not: `compose` returns early only for a pan with the TX overlay (`m_moxOverlay`, `SpectrumWidget.cpp:6999`), which only the transmitting pan sets (`MoxDisplayController.cpp:337`), so other pans keep running AGC on frames that hear the transmitter. Every pan's AGC freezes while keyed (window and Core), and keeps drawing rows with its frozen levels. Because nothing estimates during the key, the rise needs no hold.
4. **FFT input history is cleared** on every pan's engine (D17a): local engines through `FFTEngine::resetInputHistory()` (`src/core/FFTEngine.cpp:342-353`), queued to each engine's own thread; on the Core, every source engine is reset on its worker thread. Today the Core resets only on dropped packets or a configuration change (`DaemonSpectrumSource.cpp:436`, `:446`, `:476-477`, `:517-518`), never on MOX.
5. **Display duplex (DUP).** With DUP on, the rise skips `beginTransmitView` (`MoxDisplayController.cpp:347-351`) and the pan draws TX levels through `m_moxOverlay` (`SpectrumWidget.cpp:7543-7561`). DUP pans follow the same rules as every other pan.

### 5.5 On un-key (fall)

1. **Restore.** `ClarityController::setTransmitting(false)` stops wiping its smoothed floor and thresholds (`ClarityController.cpp:49-61`). With D14 the widget kept its Clarity thresholds through the key, so the first receive row already uses the pre-key level and nothing is emitted. The one exception is a band change while keyed (today `bandChanged` returns while keyed, `PanClarityRegistry.cpp:344-346`): then Clarity emits once at un-key, the **current** band's stored level, or, when that band has none, nothing until its first level after the hold. Without Clarity, AGC restores the run level saved for the current band, and when that band has none it starts from Thetis's restart floor, `WATERFALL_AGC_RESTART_FLOOR_DBM = -150f` (`display.cs:2748`, used at `:2810-2819`), ported as a named `constexpr` with the cite. This replaces `resetWaterfallAgc()` on the fall (`MoxDisplayController.cpp:528`); on the Core, the same in `DisplayExtrasProcessor`.
2. **Clear FFT input history** again on every pan's engine, local and on the Core, as on the rise.
3. **One hold per pan, on every pan, started on the un-key display edge.** Its owners are `ClarityController` and `WaterfallLevelFollower` (both in `src/core/`, so the window and the Core share them; Rule R1 holds). Each gets a `beginHold(lengthMs, nowMs)` with the length passed in and an injected clock; the window's `PanClarityRegistry` and `MoxDisplayController`, and the Core's `DisplayExtrasProcessor`, start it. The noise-floor fast attack uses the same length as its minimum on a MOX edge (5.3), so there is one length, not two timers. Length: `max(1000 ms, 2 x fill time)` (Thetis `display.cs:5912` with the purge on, 5.1), where fill time = FFT size x decimation / sample rate (the engine takes size x decimation samples per transform, `FFTEngine.cpp:287-300`). In a remote window the size is the one the Core granted (`RemoteMediaController.cpp:6533-6539`); 262144 at 48 kHz gives a hold of about 11 s. During the hold the pan keeps showing its kept levels; only re-estimation waits. For the phone, `DisplayExtrasProcessor` computes the length from its own engine. During the hold:
   - Clarity takes in no frames and emits nothing; `noiseFloorChanged` is not emitted, so a transient cannot drive grid follow (`SpectrumWidget.cpp:3078-3118`) or bake into the saved per-band levels (audit H7);
   - waterfall AGC holds (Thetis `bIgnoreAgc`, `display.cs:7640-7651`);
   - NF-AGC uses the settled floor, or the cached minimum when there is none (`display.cs:6466-6470`).
4. **Resume** smooth tracking from the kept value. The smoothing clock restarts when the hold ends: today the step is measured from the last reading (`dt = now - m_lastPollMs`, `ClarityController.cpp:156-158`), so a 10 s key gives the first reading after it a weight of about 0.97, the #359 jump again. No whole-sample anchor, and the cadence gate stays (`ClarityController.cpp:133-134`, `:153-154`).
5. **Re-key during the hold** cancels the hold and keeps the kept level; the next un-key starts a fresh hold. The hold runs on an injected clock, as the Clarity tests already inject `nowMs` (`tests/tst_clarity_controller.cpp:150-165`). Acceptance cases: fast two-tone off and on, and TUNE, inside the hold (the #359 two-tone case).
6. **No level before the key.** If Clarity was enabled, or the pan bound, while keyed, there is nothing to restore (`setEnabled` resets `m_hasEmitted`, `ClarityController.cpp:34`); Clarity starts fresh after the hold.

### 5.6 Remote window and iPhone

- **Desktop remote window.** Clarity's restore, hold and resume run in the window, as for a local pan, with the hold length from the Core's grant. AGC and NF-AGC restore and hold on the Core (5.4 step 2, 5.5 steps 1 and 3). The Core clears its source engines on both edges. D14 also stops the window flipping `coreWaterfallLevelsInUse`, which changes the window's request (`RemoteMediaController.cpp:696`); the planner sees a request that differs from the one the Core observed (`:4495-4497`) and asks again, so the Core rebuilds the endpoint at each edge today.
- **iPhone.** The Core's `ClarityController` gets the same restore, hold and resume. Each phone zoom or pan re-request builds a fresh `DisplayExtrasProcessor` and Clarity today (`DaemonMediaController.cpp:3258`; nothing is carried from the replaced entry, `:3324-3336`), so the phone starts over on every re-request. The extras state (Clarity's floor and thresholds, the AGC run level) carries across a re-request for the same source.
- The remote TX view is unchanged.

### 5.7 Clarity always resumes

#359's guarantee stays: after un-key Clarity is active again without Re-tune or a band change. The #359 tests change to use a *different* post-key floor and assert that the pre-key thresholds come back first. The tests that change are listed in section 11.1.

### 5.8 Same-band re-centre

A DDC re-centre on the same band keeps the smoothing and applies the same hold. Only a stream, rate or band change resets fully (audit H4). Code: `PanClarityRegistry::associate` sets `pendingReset` only when the stream, epoch or rate differs, not on a `centreHz` change alone (`PanClarityRegistry.cpp:308-314`).

### 5.9 What is ours, and flags

- **Ours, not Thetis.** The FFT purge on every edge (D17a; Thetis: an off-by-default option). Rejecting frames during the hold (Thetis runs the noise-floor fast attack instead). Freezing every pan's AGC while keyed (5.4 step 3). Not porting the two-frame term (5.1). Each is labelled "NereusSDR, based on Thetis" at its code site, with the Thetis cite.
- **Core RX display path (CLAUDE.md flag).** The purge resets engine input on every MOX edge in the window and on the Core, crossing the spectrum thread hand-off; the Core's `DisplayExtrasProcessor` changes how it handles MOX. Neither touches demodulation or audio.
- **Comment to edit.** The #359 comment at `MoxDisplayController.cpp:409-412` describes the clear on the rise and becomes wrong; the plan rewrites it.

---

## 6. FFT (Q4, Q5)

- **Auto-zoom** keeps about the slider's number of points on screen at every zoom, so the waterfall texture is the same zoomed in or out. The slider is the baseline; zoom adds to it the same way every time (D20). Today the math is `baseline * sr / bw` with [0.66, 1.5] hysteresis, capped by `autoZoomFftSize` (`MainWindow.cpp:8937-9009`). A remote window's planner moves to the same rule (S17).
- **Zoom cap**: about 0.7 s per transform at every rate, engine maximum 262144 on top. Merged in PR 372 (D21). Its table: 48 kHz 32768, 96 kHz 65536, 192 kHz 131072, 384 kHz and above 262144.
- **Slider applies on release**; keyboard steps apply 300 ms after the last key (D22).
- **Readout says which control is in charge** (D23). Hz/bin target: optional override, default off, slider is the floor (JJ 2026-05-08).
- **Every pan** (D26). FFT settings act on the engine pool; the auto-zoom replan is wired per pan.
- **Sizing mode** (D38 to D44, 2026-10-11): Auto is the zoom plan above with the baseline fixed at 4096; Hz/bin is the target plan with the same floor; Fixed runs exactly the slider's size, as Thetis, and a zoom never replans it. The slider is the floor in no mode.
- **Bench:** watch the Rock's `mlock` at 131072 and 262144 (`replanFft` locks three buffers, `FFTEngine.cpp:356-413`).

---

## 7. Right-click pan menu (Q11): trimmed

| Keep | Remove (now in the pill) |
| --- | --- |
| CTUN (independent pan), `SpectrumOverlayMenu.cpp:217-228` | Color Scheme `:104-118` |
| Add notch here, `:230-255` | Color Gain `:120-134` |
| Ref Level, `:185-199` | Black Level `:136-150` |
| Dyn Range, `:201-215` | Fill spectrum trace and Fill Alpha `:157-178` |
| **new:** one "Display settings..." line that opens this pan's Display pill | the 3D section `:257-426` |

The menu is created lazily and seeded on open (`SpectrumWidget.cpp:10026-10171`); the removed rows' model connections go with them.

---

## 8. Remote and Core

### 8.1 Shared keys

Station keys (the four FFT keys and per-band `DisplayGridMax_`/`DisplayGridMin_`, `SettingsScope.cpp:484-501`) are the Core's and shared by every window on it. Their rows are greyed with "Connect to the Core to change these." when the Core cannot be reached, as Setup already gates them (`DisplaySetupPages.cpp:211-219`, `:2146-2149`). Everything else in the pill, including Black Level, Color Gain and the five additions, is this computer's (D32). S4's Setup default keys apply to per-pan keys only, never to station keys.

### 8.2 Remote windows get the same pill

The pill is the same in a remote window. Rows are coloured on the window (`SpectrumWidget.cpp:7043` onward), so Black and Gain apply there over the floor N (8.4). Differences, each greyed with a reason and never hidden:

- **Cal Offset**: greyed, "The Core calibrates the display for its radio." (D28).
- **Decimation**: greyed when the Core does not offer it (D25).
- **FFT readout**: uses the size the Core granted (D23).

When a station display key changes, the window's existing planner re-asks the Core on its 100 ms tick (`RemoteMediaController.cpp:1770`). PR 373 changed only the Core's side: it clamps a re-request and shrinks a shared engine when a request departs (`539dae1e7`, `dd30545a5`; the separate re-ask task was withdrawn, `21cc865aa`). The remote planner moves to the D20 rule (S17). Section 5.6 covers key and un-key.

### 8.3 The phone

- **Setup description.** `resources/setup/display.json` gets `enabledWhen` gates for the D13 and D13a greying: AGC, NF-AGC, Low and High while Clarity is on (the AGC row is at `:733-754`), Black and Gain while it is off, and the "Use spectrum min/max" gate. The description version is bumped (S20).
- **Sizing mode (D46).** Description version 28 adds the `Sizing` row, gates `Size` and `Hz/bin Target` on it and levels the averaging times; the station catalog's `display` object lists the same control. The phone's own reading of them is its Display panel work.
- **Key and un-key**: section 5.6.
- **Black and Gain**: the phone keeps its own stored values; the window's conversion (4.3) does not touch them.

### 8.4 What N means

Black Level is an offset from the noise floor N, so N must be the same measurement wherever it is used. N stays today's measurement: the 30th percentile of the raw engine bins across the whole DDC, before any display detector, in a local window (`PanClarityRegistry.cpp:367-375`, `ClarityController.cpp:124`) and on the Core (`DaemonMediaController.cpp:4672`, documented as deliberate at `RemoteMediaController.cpp:6311-6312`). The default Black and Gain positions reproduce today's look on that measurement (4.3), so it does not change. S8 adds the same calibration the rows get to the local estimate. The Core's message already carries it: `fullSourceNoiseFloor` applies the frame's offset and the station offset (`DaemonMediaController.cpp:4672-4689`), so the window adds nothing to a remote N. The message keeps its meaning, and an older window on a newer Core sees no change (S21).

The rows are drawn after the display detector, so N sits a detector-dependent distance below the drawn noise. That gap is one candidate for the remote waterfall that sometimes turns blue (11.3); this spec does not change it, and the bench check decides whether it is the cause.

### 8.5 iPhone parity doc

`docs/architecture/2026-09-23-iphone-app-display-parity.md` gets:
- Row 1 (line 18), marked Closed with the old formulas today, is re-opened: the new Black/Gain scale, the conversion (window only), and the greying while Clarity is off.
- New rows for NB Blank, Background (Choose/Clear/Off, opacity, colour), Grid spacing, Scale text and trace colour, each marked for the phone's own Display panel work.
- The Clarity and "Use spectrum min/max" greying of AGC, NF-AGC, Low and High.

The phone's own panel changes are not tasks in this spec (settled call 15); the Core changes in 5.6 and the Setup description in 8.3 are.

---

## 9. Docs to update in the plan

| Doc | Line(s) | Change |
| --- | --- | --- |
| `docs/architecture/waterfall-tuning.md` | 157-170 | Clarity precedence: AGC greyed under Clarity; Clarity holds while paused; Black/Gain model; point here. |
| `docs/architecture/2026-09-23-iphone-app-display-parity.md` | 18 | Section 8.5: re-open row 1; add the new rows. |
| `docs/architecture/tx-display-settings-master-plan.md` | 431 | Note that the receive formula is superseded by section 4. |
| `docs/architecture/comprehensive-ui-port-design.md` | 72-93 | Mark the flyout design superseded by section 3. |
| `docs/architecture/2026-09-23-setup-description-v1.md` | 384-391 | If the `waterfallLevels` subscription field changes with section 4, say so. |
| `docs/architecture/tx-display-attempt2-design.md` | 141 | Records "re-prime the waterfall AGC on each MOX edge"; mark it superseded by section 5 (R-DSP-28). |
| `docs/architecture/tx-display-brainstorm-handoff.md` | 89-102 | Same design; same note. |
| `resources/setup/display.json` | 205, 733-754 | Not a doc, but the phone's Setup description: the `enabledWhen` gates and version bump of 8.3. |

All paths and lines checked on `2a8a09958`.

---

## 10. Requirements

| ID | Requirement |
| --- | --- |
| R-DSP-01 | The Display pill has four tabs, Spectrum, FFT, Waterfall and 3D View, in that order, holding the rows of section 3 in mockup order. |
| R-DSP-02 | Each tab fits a pan without a scroll bar at the pan heights the window offers. |
| R-DSP-03 | Every tab has a footer with "Reset this pan" and "Setup defaults →". |
| R-DSP-04 | The FFT tab header shows the tag "ALL PANS". |
| R-DSP-05 | On open, every pill control shows the pan's current value (no build-time constants). |
| R-DSP-06 | While the pill is open, a change made from Setup, the applet or the right-click menu updates the pill without the pill writing it back. |
| R-DSP-07 | The pill, the right-click menu and Setup show all eight scheme names from `wfSchemeName()`; a pan on Clarity Blue shows "Clarity Blue" everywhere. |
| R-DSP-08 | The fill colour and trace colour dialogs open on the pan's current colours. |
| R-DSP-09 | "Reset this pan" removes that pan's own appearance keys and leaves station keys unchanged. |
| R-DSP-10 | "Setup defaults →" makes the pan active and opens the Setup page matching the open tab (D6). |
| R-DSP-11 | A wheel event over any part of the pill does not change the slice frequency. |
| R-DSP-12 | The pill reopens on the last tab used. |
| R-DSP-13 | Under Clarity, Black Level at the middle position puts the black point at the noise floor N; moving it up raises the black point. |
| R-DSP-14 | Under Clarity, the colour span is about 80 dB at Gain 0 and about 10 dB at Gain 100, and the top equals the black point plus the span. |
| R-DSP-15 | The default positions give black point N + 3.4 dB (±0.5) and span 38.1 dB (±0.5) under Clarity. |
| R-DSP-16 | First run converts every stored `DisplayWfBlackLevel[_N]` and `DisplayWfColorGain[_N]` so the black point and top under Clarity are within 0.5 dB of before (or clamped at a slider end), and the conversion runs once. |
| R-DSP-17 | Black Level tooltips say up = blacker, in the pill, the applet and anywhere else Black Level appears. |
| R-DSP-18 | While Clarity is on, AGC, NF-AGC, Low and High are disabled (visible) in the pill and Setup, with the note "Clarity is setting the levels. Turn it off to use these." |
| R-DSP-19 | While Clarity is off, AGC, NF-AGC, Low and High behave as today apart from R-DSP-20. |
| R-DSP-20 | With AGC on and Clarity off, the active High equals the user's High; only the low edge follows. |
| R-DSP-51 | While Clarity is off, Black Level and Color Gain are disabled in the pill and the right-click menu with the note "Clarity is off: Low and High set the levels.", and the rendered waterfall levels equal Low and High (or the AGC / NF-AGC result) with no Black/Gain offset. |
| R-DSP-21 | While keyed or paused, every pan (not only the transmitting one) keeps Clarity's last levels; AGC does not take over. In a remote window, `coreWaterfallLevelsInUse` does not change at either edge. |
| R-DSP-22 | On un-key, the first receive row uses the pre-key Clarity thresholds for the current band, even when the post-key floor differs; after a band change while keyed, the current band's stored level, or its first level after the hold when it has none. |
| R-DSP-23 | On each MOX display edge, every pan's FFT engine input history is reset on the engine's own thread, in the window and on the Core (every source engine). |
| R-DSP-24 | After un-key, on every pan, Clarity takes in no frames and does not emit `noiseFloorChanged` for max(1000 ms, 2 x fill time), fill time = FFT size x decimation / sample rate, using the Core's granted size in a remote window; waterfall AGC holds; NF-AGC uses the settled floor or the cached minimum. It then resumes smoothing from the kept value with the smoothing clock restarted, so a key longer than 3 x tau causes no jump. |
| R-DSP-25 | After un-key, Clarity is active again without Re-tune or a band change (#359). |
| R-DSP-26 | A same-band DDC re-centre keeps Clarity's smoothed floor; a stream, rate or band change resets it. |
| R-DSP-27 | With a steady floor at the 500 ms cadence, a band's NF estimate is saved. |
| R-DSP-28 | Without Clarity, neither edge re-primes the waterfall AGC; while keyed every pan's AGC is frozen; un-key restores the run level saved for the current band, or starts from -150 dBm when that band has none; in the window and in the Core's `DisplayExtrasProcessor`. |
| R-DSP-52 | A re-key during the hold cancels it and keeps the kept level; the next un-key starts a fresh hold. Fast two-tone and TUNE inside the hold behave the same. |
| R-DSP-53 | A key shorter than the RF delay (30 ms) changes nothing on screen, in a local window, a remote window or the phone; the Core's rules run on `moxStateChanged`, not on `isMox()`. |
| R-DSP-54 | MOX, TUNE, two-tone, VOX and TCI/CAT/hardware PTT keys all produce the behaviour of R-DSP-21 to R-DSP-28; DUP pans included. |
| R-DSP-55 | A phone zoom or pan re-request for the same source keeps Clarity's floor and thresholds and the AGC run level. |
| R-DSP-56 | On a MOX display edge, every pan's noise-floor fast attack stays on for at least the R-DSP-24 hold length; on a band change, a jump of more than 0.5 MHz or an attenuator change it keeps its 1000 ms minimum and Clarity is not held. The fast attack no longer listens to `TransmitModel::moxChanged`. |
| R-DSP-57 | Low, High, AGC and NF-AGC are live only when neither Clarity nor "Use spectrum min/max" owns the levels, in Setup, the pill and the phone's Setup description. |
| R-DSP-58 | In a remote window, Cal Offset is greyed with "The Core calibrates the display for its radio."; Decimation is greyed when the Core does not offer it; the FFT readout uses the Core's granted size. |
| R-DSP-59 | A remote window's FFT planner follows the same rule as a local window's, in every sizing mode (`ControlRanges::planDisplayFft`, D42); in Auto that is the D20 rule with the baseline 4096 (4096 x rate / span, capped by `autoZoomFftSize`). Amended 2026-10-11: was "slider baseline x rate / span". |
| R-DSP-60 | For the same bins and calibration, the Core's noise-floor message gives the same N a local pan computes; the message's meaning does not change, so an older window on a newer Core sees the same N. |
| R-DSP-61 | Keyboard steps on the FFT size slider apply once, 300 ms after the last key. |
| R-DSP-29 | Dragging the FFT size slider causes one replan, on release. |
| R-DSP-30 | The FFT size slider sets the running size in Fixed, exactly, on every pan, and a zoom does not change it; in Auto and Hz/bin the stored size plays no part and the plan's floor is 4096. Amended 2026-10-11 (D38): was "the slider sets the auto-zoom baseline and is the floor". |
| R-DSP-31 | The readout under FFT Size shows bin width and time per transform, and names the Hz/bin target when the target sets the size. |
| R-DSP-32 | The stored FFT size is shown and applied after a restart (#296). |
| R-DSP-33 | FFT size, window, Hz/bin, FPS and decimation reach every pan's engine. |
| R-DSP-34 | Station rows are disabled with "Connect to the Core to change these." when the Core cannot be reached. |
| R-DSP-35 | Grid & Scales and the other Setup Display pages act on every pan that has no override of its own, not on `firstPan`. |
| R-DSP-36 | Setup Display pages update live when the value changes from the pan. |
| R-DSP-37 | Setup Cal Offset shifts the displayed levels by its value on top of the station's receive offset. |
| R-DSP-38 | Per-band dB Max and dB Min move the pan's grid, live and on a band change, for every pan. |
| R-DSP-39 | NB Blank: off by default at 1.15; when on, a row whose level exceeds the rolling baseline by the threshold is replaced by the last good row; frozen during TX. |
| R-DSP-40 | Background: Choose sets an image, Off removes it, BG Opacity 0-100 (default 80) and BG Color (default `#0a0a14`) apply, all saved per pan on this computer. |
| R-DSP-41 | Grid spacing: Auto gives today's grid; a fixed choice gives that spacing on the grid and the scale. |
| R-DSP-42 | Scale text: the default matches today's frequency-scale text; other sizes resize the text and the strip fits it. |
| R-DSP-43 | Trace colour: defaults to the fill colour; changing it changes only the trace line, on the CPU and GPU paths. |
| R-DSP-44 | Each ported AetherSDR block carries the attribution of section 3.7, and the provenance row exists. |
| R-DSP-45 | The right-click menu holds CTUN, Add notch here, Ref Level, Dyn Range and one "Display settings..." line that opens the pill; it holds no scheme, gain, black, fill or 3D rows. |
| R-DSP-46 | A remote window shows the same pill; its appearance rows save on that computer. |
| R-DSP-47 | In a remote window with no S-meter widget, the VFO flags' meters keep updating. |
| R-DSP-48 | 3D Slice Shadow and 3D Span are disabled (visible) on the CPU fallback. |
| R-DSP-49 | 3D Floor per-band recall works on every pan. |
| R-DSP-50 | The docs of section 9 are updated in the same branch. |
| R-DSP-62 | `DisplayFftSizingMode` (station) is `Auto`, `HzPerBin` or `Fixed`. Auto plans 4096 x rate / span under the time cap whatever the stored size (1.536 MHz with a 35 kHz span plans 262144); Hz/bin plans rate / target with the floor 4096; Fixed plans exactly the stored size at every span and rate. |
| R-DSP-63 | With the mode key absent, `DisplayHzPerBinTarget` above 0 reads as `HzPerBin` and anything else as `Auto`; a stored `DisplayFftSize` is left as it is. No settings schema change. |
| R-DSP-64 | `DisplayHzPerBinTarget` holds the target while the mode is `HzPerBin` and 0 otherwise; `DisplayHzPerBinLastTarget` remembers the last target chosen. |
| R-DSP-65 | On the Core, a station write of `DisplayHzPerBinTarget` above 0 sets the mode `HzPerBin`; a write of 0 sets `Auto` when the mode was `HzPerBin` and leaves `Fixed` alone. Every window sees the new mode. |
| R-DSP-66 | A remote window in Fixed asks the Core for exactly the stored size on the "wide" tier, and a zoom does not change the size or tier it asks for; a local and a remote window plan the same size from the same inputs in all three modes. |
| R-DSP-67 | A sizing mode change sets the FFT size offset for the new mode (stored size in Fixed, 0 dB otherwise) and restarts each noise-floor tracker's fast attack once. |
| R-DSP-68 | The Sizing selector, the graying of the FFT Size and Hz/bin controls with their reasons, and the always-true readout are as D44, in the pill and in Setup. |
| R-DSP-69 | Auto AGC-T's threshold is the stream's noise floor referred to a 4096-point FFT (each frame's mean + 10 x log10(N / 4096)) + the offset - 2.0 (0 with AGC off), with no FFT size offset term: the same within 1 dB at every running FFT size, in every sizing mode, through a zoom with no tracker restart, and in a local window as in the Core (D45). Every other reader of the noise floor keeps the floor per bin at the running size, except the "NF x dB" line beside AGC-T in a local window, which shows the referred floor the threshold is built from (D45). |
| R-DSP-70 | Setup description version 28 describes the sizing mode: `display.spectrumDefaults.fftSizing` (Auto, Hz/bin, Fixed; default Auto) leads the Fast Fourier Transform group, Size is enabled in Fixed only and Hz/bin Target (0.5 to 200) in Hz/bin only, and the two averaging times run from 1 ms in steps of 1. A peer below 28 is sent what it was sent before, byte for byte. The desktop's Spectrum Defaults and Waterfall Defaults pages agree with the version 28 rows with no exception. |
| R-DSP-71 | The station catalog's `display` object lists the Sizing control (station, tokens `Auto`, `HzPerBin`, `Fixed`, default `Auto`), names `DisplayHzPerBinLastTarget` on the Hz/bin Target control and keeps that control's `offValue` 0; its two averaging time controls run from 1 ms in steps of 1, and the Core takes an averaging time from 1 ms in a display request. The remembered target follows a target write from any client. |

---

## 11. Verification

### 11.1 Software tests (offscreen)

Build single tests, never the whole suite (`docs/development/fast-test-loop.md`). Prefix direct binaries with offscreen; do not export it over ctest.

- **Pill:** extend `tests/tst_spectrum_overlay_panel.cpp`: tabs, row order, seeding from a pan with non-default values (R-DSP-05), live follow (R-DSP-06), footer actions, last tab, wheel (R-DSP-01 to R-DSP-12, R-DSP-34, R-DSP-48); the greying of Black/Gain with Clarity off and of Low/High/AGC/NF-AGC with Clarity on or min/max on, with their notes (R-DSP-18, R-DSP-51, R-DSP-57); a pill built as a remote window's: Cal Offset and Decimation greyed with their reasons, appearance rows saved on this computer (R-DSP-46, R-DSP-58); Black Level tooltips (R-DSP-17, with `tests/tst_display_applet.cpp` for the applet).
- **Schemes:** `tests/tst_b8_overlay_signals.cpp`, `tests/tst_dss_overlay_menu.cpp` (R-DSP-07, R-DSP-45).
- **Levels:** `tests/tst_display_followers.cpp` (R-DSP-19, R-DSP-20, and the renderer's plain Low/High with Clarity off for R-DSP-51), a renderer test of the effective black point and top under Clarity (R-DSP-13 to R-DSP-15), `tests/tst_display_settings_model.cpp` and `tests/tst_pan_display_settings_inherit.cpp` for the conversion, once only, per pan (R-DSP-16).
- **Key and un-key:** `tests/tst_clarity_controller.cpp`, `tests/tst_pan_clarity_registry.cpp`, `tests/tst_mox_display_controller.cpp` (R-DSP-21 to R-DSP-28, R-DSP-52 to R-DSP-56). New cases: a key longer than 3 x tau (no jump), band change while keyed, re-key inside the hold, a key under 30 ms, the `keyedChanged`-before-`fall()` order, a non-transmitting pan, a DUP pan, keys through MOX, TUNE and a TCI or CAT source. Each new test fails first on main.
- **Existing tests that change** (they assert today's behaviour): `tst_clarity_controller.cpp:196` (`transmitRelease_takesTheFirstPostKeyFloorWhole`, now contradicted), `:150` (emit count); `tst_pan_clarity_registry.cpp:89`, `:108`, `:444`; `tst_mox_display_controller.cpp:107`, `:129`, `:676`, `:696` (the rise and fall AGC resets). The #359 tests (`transmitRelease_reEmitsAtTheUnchangedFloor`, `unkeyRestoresClarityAtTheUnchangedFloor`) use a different post-key floor (5.7).
- **Core:** `tests/tst_daemon_media_controller.cpp` and the `DisplayExtras` tests: no AGC re-prime on MOX, restore per band, the hold with an injected fill time, the source-engine purge on both edges, state carried across a same-source re-request, the rules running on `moxStateChanged` (R-DSP-23, R-DSP-28, R-DSP-53, R-DSP-55); in `tests/tst_display_extras.cpp`, the Core's floor equals the local `ClarityController` floor for the same bins (R-DSP-60).
- **Remote window:** `tests/tst_remote_media_controller.cpp`: no endpoint rebuild at key and un-key, hold length from the grant, planner rule (R-DSP-21, R-DSP-24, R-DSP-58, R-DSP-59).
- **FFT:** `tests/tst_fft_engine_pool.cpp`, `tests/tst_display_setup_binding.cpp`, `tests/tst_auto_zoom_fft.cpp` (R-DSP-29 to R-DSP-33, R-DSP-61).
- **Auto AGC-T floor:** `tests/tst_auto_agc_floor_reference.cpp`, `tests/tst_noise_floor_tracker.cpp`, `tests/tst_daemon_agc_source.cpp` (R-DSP-69).
- **Setup:** `tests/tst_display_setup_binding.cpp` (R-DSP-18, R-DSP-35 to R-DSP-38, R-DSP-57), and `tests/tst_setup_description_parity.cpp` / `tst_setup_description_service.cpp` for `display.json`'s new gates (R-DSP-57).
- **Meter:** `tests/tst_remote_meter_poller.cpp` (R-DSP-47).
- **3D Floor:** `tests/tst_dss_persistence.cpp`: per-band recall on a second pan, beside the existing pan 0 case through `wireDss3DFloorRecallForTest` (R-DSP-49).
- **Additions:** new widget tests for NB Blank, background, grid spacing, scale text and trace colour (R-DSP-39 to R-DSP-43).
- **Attribution:** `scripts/verify-inline-tag-preservation.py` and the header checks pass (R-DSP-44).

### 11.2 Visual captures

The capture harness (`tests/visual/capture.py`) is **not on main** at `2a8a09958`; it lives on the `claude/wvr-scenes` lane, which lands after this work. If it is on main by the time this branch is reviewed, capture the pill's four tabs (Clarity on and off), the trimmed right-click menu and Setup's Waterfall Defaults with `--jobs 4`, with a before/after identity pass. If not, use screenshots of the running window. Offscreen tests cannot prove clicks or hover on macOS native views; use cursor geometry for those.

### 11.3 Bench (pending, with JJ's OK)

- **Remote waterfall background turns blue** (ANAN G2 behind the Core). Candidate, unverified: the Core's Clarity floor is a full-source percentile before the display detector (`RemoteMediaController.cpp:6310-6321`), while the rows are post-detector pixels. Run the window with `NEREUS_TRACE_WF_STATE=1` (`SpectrumWidget.cpp:7146`), read "Core noise floor received for Clarity" (`RemoteMediaController.cpp:6316`) and compare with the row noise.
- **S-meter Sig Avg check:** set the window's S-meter to Sig Avg and compare the flag on band noise; then a known carrier in Peak and Avg (audit-smeter section 4).
- **Rock `mlock`** at FFT 131072 and 262144.
- **Un-key** on a live band, local and remote, at a small and a large FFT size: the blue level returns to its pre-key look on the first row, no trace of the transmission shows after un-key, and there is no drift afterwards. Repeat with fast two-tone and TUNE.

---

## 12. Risks

- **Core RX display path.** The FFT-history purge resets engine input on every MOX edge, in the window and on the Core, on each engine's own thread. A wrong thread or a missed edge could drop receive frames or leave TX samples in a receive frame. Mitigation: queue the reset to the engine's thread, test both edges, bench un-key local and remote.
- **Remote split.** Clarity runs in the window, AGC and the FFT on the Core; a fix on one side only leaves remote un-key half done. Mitigation: section 5.2's table drives the plan's task list; the remote tests cover both sides.
- **Core and window versions.** A new window against an older Core gets window-side Clarity behaviour but the old Core AGC re-prime and no Core purge. An older window against a newer Core gets the Core's AGC freeze, restore and purge, and the same noise-floor message. Mitigation: no message changes meaning; the remote rows behave as today on an older Core; the pill does not depend on Core changes to open.
- **Settings conversion.** A wrong formula or a double run changes every user's waterfall on upgrade. Mitigation: convert from the formulas in section 4.3, record a once-only marker, test per pan index, clamp at slider ends.
- **Per-pan key fallbacks.** Untouched pans inherit pan 0 through `rawValue` (`SpectrumWidget.cpp:745-761`). "Reset this pan", Setup defaults and live Setup pushes all lean on that inheritance, and pan 0's key is the base key. Mixing them up can make one pan's change spill to others. S4 removes the mix: Setup has its own default keys and every pan, pan 0 included, stores only its overrides.
- **Remote parity.** New per-pan keys must stay on this computer, and station rows must gate correctly when the Core drops.
- **Ported code attribution.** Missing headers or cites block the PR.

---

## 13. Settled after review (2026-10-09)

JJ ruled on S3 ("a"). The rest were listed to him for veto with the recommendation and stand.

| # | Was | Settled |
| --- | --- | --- |
| S1 | Black Level scale | 0 to 100, 0.5 dB per step (AetherSDR SW auto-black step, AetherSDR `SpectrumWidget.cpp:11928`), 50 = black at the noise floor, default 57 (N + 3.5 dB, today's look). |
| S2 | Color Gain curve | AetherSDR's straight line, span = 80 - 0.7 x Gain dB (AetherSDR `SpectrumWidget.cpp:11593`), default 60 (38 dB, today's look). |
| S3 | Black and Gain with Clarity off | **JJ: A.** Greyed, with the note "Clarity is off: Low and High set the levels." Only one set of controls is live at a time: Black and Gain under Clarity; Low, High, AGC and NF-AGC (Thetis, unchanged) without it. The renderer's Black/Gain offsets apply only while Clarity drives the levels. |
| S4 | Setup defaults versus pan 0 | Setup gets its own default keys. Every pan, pan 0 included, stores only what the user changed on it; "Reset this pan" removes those overrides. On first run pan 0's current base-key values become the Setup defaults, so nothing looks different. Follows `thetis-display-dsp-parity-design.md:85-97` (Setup = global defaults, per-pan overrides). |
| S5 | NB Blank threshold domain | Ported on linear power (dBm rows converted for the row-mean comparison), so the 1.05 to 1.95 threshold means what it does in AetherSDR. |
| S6 | Background "Clear" | No default picture ships. Clear removes the chosen picture; Off hides it and keeps it chosen. |
| S7 | NF-AGC | Ported with Thetis semantics: an option under AGC, low = noise floor minus the offset, 0.1 dB step, High left alone (`display.cs:6466-6470`). This is the "NF-AGC does nothing" fix. |
| S8 | Clarity wobble and calibration | Emitted thresholds slew toward the target every row (Thetis per-row lerp, `display.cs:7662-7666`) instead of 2 dB steps. Clarity estimates the floor in calibrated dB (the same offset the rows get), which the decided model needs for "N + x dB" to be true and which stops ATT changes moving the level (audit H3, H5). |
| S9 | Right-click Ref Level and Dyn Range | Write the per-band store, the same values as dB Max and dB Min (settled call 8). |
| S10 | GPU fill colour | The GPU fill's lower edge follows the fill colour, done with the trace colour work. |
| S11 | Small values | Line width 0.1 px steps, 0.1 to 5.0, in Setup too (ruling 2026-10-10; was 0.5 px steps, 0.5 to 3). Scale text 8 to 14 px, default 10. Hz/bin readout: "Hz/bin target is setting the size". |

### Revision 2: JJ's ruling and the review's settled items

**Q12 (JJ: "a", 2026-10-09).** The FFT input history is cleared on every key and un-key, on every pan, in the window and on the Core (D17a). Thetis has this only as an off-by-default option; ours always does it, so a large FFT never shows the user's own transmission after un-key.

The following were listed to JJ for veto with the review and stand.

| # | Was | Settled |
| --- | --- | --- |
| S12 | "Use spectrum min/max" and Clarity both grey Low/High/AGC/NF-AGC | The gates combine (D13); the pill's Waterfall tab gets the "Use spectrum min/max" pill. |
| S13 | Existing noise-floor fast attack versus the new hold | One hold length. The hold lives in `ClarityController` and `WaterfallLevelFollower`; the fast attack's minimum on a MOX edge becomes the same length, on every pan, on the display edge. Band, tune and attenuator triggers are unchanged (5.3, 5.5). |
| S14 | Hold length and Thetis's "two extra frames" units bug | Hold = max(1000 ms, 2 x fill time), Thetis's purge-on case; fill counts decimation. The two-frame term is not ported; the comment records the upstream bug (5.1). AGC with no saved level starts at Thetis's -150 dBm restart floor. Every pan's AGC freezes while keyed (5.4). |
| S15 | DUP pans | Same rules as every pan (5.4). |
| S16 | Key sources | Every source reaches the display edge through `MoxController`; tests cover MOX, TUNE and TCI or CAT (5.3). |
| S17 | Remote planner sizing | Moves from one bin per pixel to the D20 rule, on top of PR 372's cap (D20). |
| S18 | Remote Cal Offset and Decimation | Greyed with reasons, as Setup does (D25, D28). |
| S19 | Keyboard steps on the FFT size slider | Apply 300 ms after the last key (D22). |
| S20 | Phone Setup description | `display.json` gets the D13/D13a/S12 gates and a version bump (8.3). Phone parity row 1 re-opened (8.5). |
| S21 | Meaning of N | N stays today's measurement (full-DDC percentile before the detector), the same local and remote, with S8's calibration on both (the Core's message is already calibrated; the window adds nothing); the message keeps its meaning (8.4). |
| S22 | Cites, test list, docs | Cites refreshed to `2a8a09958`; tests that change listed (11.1); three older TX display docs added to section 9. |

Also from the review, recorded as facts rather than choices: the TCI program-key branch and the native audio engines branches touch no display behaviour (they shift `RemoteMediaController.cpp` line numbers only). ATT-on-TX sends only `TxAttenData` (`StepAttenuatorController.cpp:764-830`) and RX attenuator changes already start the noise-floor hold (`MainWindow.cpp:8616-8621`). Unverified, for the plan to check: whether S8's calibrated floor includes the step attenuator during ATT-on-TX (`RadioModel.cpp:10540`).

No open items remain.

---

## Appendix A. Cites refreshed to `2a8a09958`

Every NereusSDR `file:line` in this spec was re-checked on `2a8a09958` (2026-10-09). Between `0701d7dfc` and `2a8a09958` only four source files moved (PR 372 and PR 373); every other file under `src/`, `tests/` and `resources/` is unchanged. The audits behind section 1 were written at `a0681c508`; their cites were brought to main earlier and are folded into the lines below.

| File | Shift on `2a8a09958` | Cites moved (old to new) |
| --- | --- | --- |
| `src/core/ControlRanges.h` | +2 above line 243; +55 below it (auto-zoom cap block) | FFT size 194-204 to 196-206; Window 247-259 to 302-314; Hz/bin 264-272 to 319-327; FPS 277-282 to 332-337; Decimation (store legend) 327-329 to 383-385; auto-zoom cap 262-293 to 262-294 |
| `src/gui/MainWindow.cpp` | -4 below 8943 (auto-zoom lambda shortened) | auto-zoom 8932-9013 to 8933-9009 and 8937-9020 to 8937-9009; lambda 8937 to 8958; pan-0-only wiring 8960-8962 to 8955-8958; S_AV fallback 9369-9372 to 9365-9368 |
| `src/gui/RemoteMediaController.cpp` | +4 below 630 | `plannedFftSize` 604-661 to 604-632; levels request 693 to 697, 693-701 to 697-705; Decimation gate 1040-1042 to 1044-1046; planner tick 1766 to 1770; Core noise floor 6306-6317 to 6310-6321, 6307 to 6311, 6309-6317 to 6313-6321, 6312 to 6316; granted size 6529-6535 to 6533-6539 |
| `src/core/session/media/DaemonMediaController.cpp` | +31 to +76 below 3134 | re-request carry 3326-3335 to 3324-3336; full-source noise floor 4600 to 4672 |

Ranges corrected in the same pass where the file did not move but the cite missed the code its sentence describes:

| File | Shift on `2a8a09958` | Cites moved (old to new) |
| --- | --- | --- |
| `src/gui/SpectrumOverlayMenu.cpp` | 0 | section 7 rows: Color Scheme 104-116 to 104-118, Color Gain 119-133 to 120-134, Black Level 135-149 to 136-150, Fill 156-177 to 157-178, Ref Level 184-198 to 185-199, Dyn Range 200-213 to 201-215, CTUN 220-228 to 217-228, Add notch 230-260 to 230-255, 3D section 272-437 to 257-426 |
| `src/gui/MoxDisplayController.cpp` | 0 | queued display edge 613-620 to 643-646; short-key no-op 202-213 to 201-212; `fall()` 234 to 235; Core with no transmit display 109 to 318-320 |
| `src/gui/SpectrumWidget.cpp` | 0 | right-click seeding 10104-10152 to 10105-10171, 10026-10152 to 10026-10171; CPU gate 10171-10181 to 10172-10182 |
| `src/gui/setup/DisplaySetupPages.cpp` | 0 | FFT size slider (D22) 695 to 580; 3D View subscriptions 3334-3374 to 3334-3379; scheme loop 1858-1885 to 1863-1870 |
| Others | 0 | `PanClarityRegistry.cpp` 306-313 to 308-314; `DisplaySettingsModel.cpp` 247-306 to 247-311; `DisplayFollowers.cpp` 210-225 to 210-229; `StepAttenuatorController.cpp` 764-809 to 764-830; `resources/setup/display.json` 733-745 to 733-754; `waterfall-tuning.md` 128-138 to 131-138 |

All other NereusSDR cites hold on `2a8a09958`. The Thetis cites (`v2.10.3.15`, `3759d096`) in sections 4.5, 5.1 and 13 and the AetherSDR cites (`@1e0718ad`) were re-checked and hold. The iPhone parity doc's row 1 still cites pre-audit lines (`SpectrumWidget.cpp:6225-6226`, `:654-655`; `SpectrumOverlayPanel.cpp:601-648`); section 9 refreshes them.
