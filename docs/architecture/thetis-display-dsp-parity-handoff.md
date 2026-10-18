# Thetis Display + DSP Parity — Session Handoff

**Branch:** `claude/eager-borg-d64bed`
**Last commit at handoff:** `8e79435` (fix: render Peak Blobs + Active Peak Hold in GPU overlay path)
**Worktree:** `/Users/j.j.boyd/NereusSDR/.claude/worktrees/eager-borg-d64bed`
**Status:** All 36 plan tasks complete + merged with origin/main; bench testing in progress.

---

## tl;dr for the next session

Branch is **mergeable but not yet merged**. Implementation is complete; manual bench testing surfaced a couple of polish items. Ready for one or more of:

- Continue manual bench session per design Section 8C
- Address the 2 pre-existing test failures (out-of-scope but noted)
- Refactor GPU overlay perf compromise into proper dynamic layer
- Open the PR

The next session should **read this doc first**, then `docs/architecture/thetis-display-dsp-parity-design.md` (the spec) and `docs/architecture/thetis-display-dsp-parity-plan.md` (the plan) for full context.

---

## What's done (43 commits since v0.2.3 baseline `2b70cc8`)

**Phase 1 — Channel rebuild infrastructure (8 tasks, complete):**
- `RxChannel::rebuild()` / `TxChannel::rebuild()` / `WdspEngine::rebuildRxChannel/Tx`
- Sample-rate live-apply coordinator (P1+P2)
- Active-RX-count live-apply (P1 EP6 + P2 DDC port handling)
- `dspChangeMeasured(qint64 ms)` signal on RadioModel

**Phase 2 — RX1 audit (12 tasks, complete):**
- Detector / Averaging split (handwave fix from 3G-8)
- S2 FFT Window cite correction
- Spectrum Defaults overlay group (MHz cursor, NF, bin width, normalize, peak text, decimation, Get-Monitor-Hz)
- New `Display → Spectrum Peaks` page with Active Peak Hold + Peak Blobs (full Thetis decay math `Display.cs:5483 [v2.10.3.13]`)
- Waterfall NF-AGC + Stop-on-TX + Calculated-Delay readout + Copy button + W5 removal
- Grid & Scales NF-aware grid (auto-track grid min to live noise floor)
- Per-band NF-estimate priming (NereusSDR-original — eliminates cold-start visual jump on band change)

**Phase 3 — Display-General fold (6 tasks, complete):**
- New `Display → Multimeter` page with full S/dBm/µV unit-mode fan-out across all `MeterItem` subclasses
- HistoryGraphItem duration setting + signal history wire-up
- SmallModeFilteronVFOs in `Appearance → MeterStylesPage`
- Diagnostics page renamed "Logging & Performance" + 3 perf toggles
- ANAN-8000DLE volts/amps in `Hardware → RadioInfoTab` (cap-gated)
- CPU meter rate spinbox in `General → Options`

**Phase 4 — DSP-Options (6 tasks, complete):**
- New `Setup → DSP → Options` page (18 controls + warnings + readout)
- Per-mode buffer/filter/filter-type live-apply via Phase 1 rebuild
- Filter Impulse Cache wiring (load/save to `~/.config/NereusSDR/impulse_cache.bin`)
- High-resolution filter characteristics with FilterDisplayItem high-res render path
- Warning icon validity logic ported from Thetis `console.cs:38797-38807 [v2.10.3.13]`
- "Time to last change" readout subscribing to `dspChangeMeasured`

**Phase 5 — Migration + matrix + CHANGELOG (4 tasks, complete):**
- `SettingsSchemaVersion=3` migration retiring 4 keys (`DisplayAverageMode`, `DisplayPeakHold`, `DisplayPeakHoldDelayMs`, `DisplayReverseWaterfallScroll`)
- 7 new verification matrix files at `docs/architecture/phase3g8-verification/` (~79 rows)
- PROVENANCE.md audit + 22 new rows added
- CHANGELOG.md v0.3.0 entry
- Final cleanup pass (caught 13 missing `//MW0LGE` author tags in NF-aware grid code, fixed)

**Merge with main (commit `95653af`):**
- 21 commits from main since 2b70cc8 integrated, including v0.3.0 release tag, HL2 mic decimation work, connect-wisdom fix, TxChannel parent fix
- 3 conflicts resolved cleanly: `WdspEngine.cpp` (combined wisdom-thread strategy + Task 4.3 cache logic), `P1RadioConnection.cpp` (combined mic-decimation + Task 1.6/1.7 restart helpers), `RadioModel.cpp` (trivial include conflict)

**Post-merge fix (commit `8e79435`):**
- Peak Blobs + Active Peak Hold weren't visible in GPU rendering mode (the default). Tasks 2.5/2.6 implemented them as `drawSpectrum()` passes, but `drawSpectrum()` only runs in CPU mode. Fix: invoke them inside `renderGpuFrame()` overlay-build block + force `m_overlayStaticDirty=true` every spectrum frame when enabled. TODO comment flags the perf compromise.
- Also tuned Peak Blob ring size (radius 4 → 3, stroke 2px → 1px) to match Thetis-on-1x visual size after Qt6/QPainter Retina scaling.

---

## What's open

### 1. Two pre-existing test failures (not branch regressions)

Both failed on origin/main HEAD before merge; not caused by branch work:

- `tst_radio_model_set_tune::tuneOnPushesTunePower` — test expects `txDriveLog.first() == 50` (raw percent), but `RadioModel.cpp:4052` scales `tunePower * 255 / 100 = 127` (wire byte). Looks like main's mi0bot scaling work updated the implementation without updating the test. Then SEGFAULT cascades on subsequent sub-test.
- `tst_p1_mic_ptt_wire::setMicPTTTrue_hl2CodecPath_c1Bit6Clear` — test expects `bank11[1] & 0x40 == 0` (bit clear) on HL2 codec path when MicPTT=true; implementation produces `64` (bit set). Likely a test-vs-implementation drift in main's HL2 fix work.

**Action for next session:** triage these as separate issues. They predate this branch; v0.3.0 release accepted them. Might already be tracked.

### 2. GPU overlay perf compromise (TODO in `SpectrumWidget.cpp:1882-1895`)

Quick fix forces full overlay re-render every spectrum frame when peaks/blobs enabled. The static chrome (grid, scales, band plan, freq scale, time scale, waterfall chrome) gets repainted every frame as collateral.

**Action for next session:** refactor to a separate `m_overlayDynamic` layer:
- Static layer: grid, scales, band plan — re-rendered only on state change (current behavior)
- Dynamic layer: peak hold, blobs, FPS, peak text, NF readout — re-rendered every frame
- Composite both as separate textures in renderGpuFrame

This is a meaningful refactor (~half-day work). Not blocking ship; just future polish.

### 3. Peak Blob visual size — likely needs more user iteration

Current state: radius 3 logical pixels + 1px stroke. JJ flagged "donut" → "small dot," then "diameter still large." Last iteration not yet user-confirmed.

**Action for next session:** ask JJ if current size is acceptable, or fine-tune further. The relevant code is `SpectrumWidget.cpp:paintPeakBlobs()`. If JJ wants filled dots instead of rings (deviating from Thetis), change `setBrush(Qt::NoBrush)` → `setBrush(m_peakBlobColor)`.

### 4. Build performance — long iteration cycle

Each source change to `SpectrumWidget.cpp` (or other widely-included headers) triggers ~30-60s rebuild because **~300 test executables all relink** to the main object library.

Mitigations available:
- `cmake --build build -j --target NereusSDR` — builds only the app, ~5-10s (fine for visual eyeballing)
- Convert `NereusSDRObjs` from OBJECT library to shared library (`.dylib`/`.so`) — biggest structural fix; tests link symbolically instead of statically
- Precompiled headers for Qt — incremental win

**Action for next session:** offer to do the OBJECT → shared library conversion as a separate small PR. Useful productivity boost across all future work, not specific to this branch.

### 5. Manual bench session (design Section 8C)

Test plan in the handoff at the bottom. Status as of last bench:
- ✅ App launches cleanly
- ✅ Setup tree shows all new pages
- ✅ Spectrum Peaks page works (Peak Blobs visible after the GPU overlay fix)
- ⏳ Per-band NF priming (band-change behavior)
- ⏳ NF-aware grid (auto-track)
- ⏳ Sample-rate live-apply (48k → 96k → 192k → 384k while listening)
- ⏳ Active-RX-count live-apply (RX2 toggle)
- ⏳ DSP-Options live-apply (per-mode buffer/filter/type)
- ⏳ Multimeter unit-mode fan-out (S/dBm/µV)

### 6. Two `TODO(Task 1.6)` markers in `RadioModel.cpp`

Lines 4322 and 4366 — emit `tuneRefused` / `BlockingQueuedConnection` refinements for sample-rate live-apply. Task 1.6 shipped; these are polish items for a follow-up.

---

## Where everything lives

```
docs/architecture/thetis-display-dsp-parity-design.md       — design doc (commit b3a0246)
docs/architecture/thetis-display-dsp-parity-plan.md         — implementation plan (commit 704252c)
docs/architecture/thetis-display-dsp-parity-handoff.md      — this file
docs/architecture/phase3g8-verification/                    — extended with 7 new matrix files
CHANGELOG.md                                                — has [0.3.0] — TBD section ready

src/core/dsp/ChannelConfig.h         — Phase 1 input
src/core/dsp/RxChannelState.h        — Phase 1 RX state snapshot
src/core/dsp/TxChannelState.h        — Phase 1 TX state snapshot
src/core/RxChannel.h/.cpp            — Phase 1 rebuild API + per-mode onModeChanged
src/core/TxChannel.h/.cpp            — Phase 1 TX rebuild
src/core/WdspEngine.h/.cpp           — Phase 1 createChannel/rebuildRxChannel/rebuildTxChannel
src/core/AppSettings.h/.cpp          — Phase 5 migration
src/core/P1RadioConnection.cpp       — Phase 1 restartStreamWithRate/Count
src/core/P2RadioConnection.cpp       — Phase 1 sample-rate / RX-count live-apply
src/models/RadioModel.h/.cpp         — coordinator: setSampleRateLive, setActiveRxCountLive, dspChangeMeasured
src/models/PanadapterModel.h/.cpp    — per-band NF estimate storage + persistence
src/dsp/NoiseFloorEstimator.h/.cpp   — prime() method for Task 2.10
src/gui/SpectrumWidget.h/.cpp        — Detector/Averaging split, overlays, NF-aware grid, Active Peak Hold + Peak Blobs render
src/gui/spectrum/ActivePeakHoldTrace.h/.cpp  — Phase 2 per-bin decay trace
src/gui/spectrum/PeakBlobDetector.h/.cpp     — Phase 2 top-N peak finder
src/gui/setup/SpectrumPeaksPage.h/.cpp       — new Setup page
src/gui/setup/MultimeterPage.h/.cpp          — new Setup page
src/gui/setup/DspOptionsPage.h/.cpp          — new Setup page
src/gui/setup/DisplaySetupPages.h/.cpp       — extended (overlays, NF group, copy buttons, etc.)
src/gui/setup/AppearanceSetupPages.cpp       — VFO Flag group
src/gui/setup/DiagnosticsSetupPages.cpp      — Logging & Performance rename + perf group
src/gui/setup/GeneralOptionsPage.cpp         — CPU meter rate
src/gui/setup/hardware/RadioInfoTab.cpp      — ANAN-8000DLE volts/amps
src/gui/meters/MeterPoller.h/.cpp            — configurable interval + averaging
src/gui/meters/MeterItem.h/.cpp              — unit-mode + formatValue static
src/gui/meters/{Bar,Text,SignalText,Needle,HistoryGraph,FilterDisplay}Item.cpp  — unit-mode fan-out
src/gui/meters/HistoryGraphItem.h/.cpp       — duration setting

tests/                                       — ~30 new test executables added
```

---

## How to resume in a new session

1. Read this handoff first.
2. Read the design doc (`thetis-display-dsp-parity-design.md`) for full context on every decision.
3. Read the implementation plan (`thetis-display-dsp-parity-plan.md`) for the task-level breakdown.
4. Verify branch state: `git -C ~/NereusSDR/.claude/worktrees/eager-borg-d64bed log --oneline -5` should show `8e79435` at HEAD.
5. Verify build: `cmake --build ~/NereusSDR/.claude/worktrees/eager-borg-d64bed/build -j --target NereusSDR` (app-only, ~5-10s).
6. Pick from the open-items list (above) based on JJ's priority.

Probable conversation starters:
- "Continue the bench testing"
- "Fix the 2 pre-existing test failures from main"
- "Refactor the GPU overlay into a dynamic layer"
- "Open the PR" (verify all in-flight changes committed; push branch; gh pr create)

---

## Bench test plan (design Section 8C — abbreviated)

### Phase 2 visible features
- Open `Setup → Display → Spectrum Peaks` → toggle Peak Blobs ON → see top-N peak markers (rings)
- Toggle Active Peak Hold ON → see peak trace decay
- Open `Setup → Display → Spectrum Defaults` → toggle each overlay on/off (NF, bin width, MHz cursor, peak value)
- Open `Setup → Display → Grid & Scales` → toggle "Adjust grid min to noise floor" → grid bottom should track NF estimate
- Tune to 6m (quiet) then 40m (noisy) → spectrum should snap to new NF without 1-2s slosh (per-band priming)

### Phase 3 page checks
- Confirm `Display → Multimeter` page exists with 8 polling controls + S/dBm/µV combo + signal history
- Switch unit mode S → dBm → µV → all bound MeterItems update display

### Phase 4 live-apply (requires connected radio)
- Open `Setup → DSP → Options`
- While in Phone mode (USB), change Buffer Size Phone → "Time to last change: NN ms" updates
- Switch radio to CW → change Buffer Size Phone → readout shows "(stored, not applied)"
- Switch back to Phone → channel rebuilds with the stored value

### Phase 1 cross-thread coordinators (requires connected radio)
- Open Hardware setup → change sample rate 48k → 96k while audio playing → one click, audio resumes
- 96k → 192k → 384k → back to 48k — same one-click each
- Toggle RX2 (on radios with 2+ DDCs) → activates without disconnect

### Settings migration
- First launch of v0.3.0 should write `SettingsSchemaVersion=3` to `~/.config/NereusSDR/NereusSDR.settings`
- Verify retired keys are absent: `grep "DisplayAverageMode\|DisplayPeakHold\|DisplayReverseWaterfallScroll" ~/.config/NereusSDR/NereusSDR.settings` → no output

---

## Sign-off

- **Author:** Claude (Opus 4.7) over a multi-day session
- **Verification owner (next):** TBD (see open items list)
- **Bench owner:** JJ Boyd / KG4VCF on ANAN-G2 + HL2
- **Final commit before handoff:** `8e79435`
