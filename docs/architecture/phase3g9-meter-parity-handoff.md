# Phase 3G-9 — Thetis Meter Parity Handoff

**Branch:** `test/meters-on-main`
**Base:** `origin/main` (`b9c4879` — Phase 3N release pipeline merged)
**Commits ahead of main:** 56 (44 from original handoff + 8 pre-PR
commits listed below, landed during live verification)
**Working tree:** clean
**Test suite:** 6/6 green (50+ assertions)
**Status:** Ready for PR — **not yet pushed, not yet PR'd**

## Pre-PR fixes (8 commits, landed during live verification)

Live smoke-testing the branch surfaced the three handoff polish
gaps plus four additional issues — two pre-existing bugs (reset
behaviour, quit-time crash) and two that only emerge when you
actually try to use stacked bar rows at different container
sizes. All eight are closed in GPG-signed commits at the branch
tip before push.

1. **`1b698a9 fix(meters): clamp GeneralScale tick row on short ScaleItems`**
   — closes handoff polish gap #1 (title vs tick overlap < 40 px).

2. **`59e306a fix(meters): persist BarItem peakFontColour across serialize (E2)`**
   — closes handoff polish gap #2. BAR format field 31 +
   round-trip test.

3. **`6d7a79b fix(meters): render "--" for idle ShowPeakValue slot`**
   — closes handoff polish gap #3.

4. **`48174d5 revert(meters): restore needle S-Meter as default; preserve bar opt-in`**
   — reverts commit 4bba2c2. `createSMeterPreset` returns to its
   pre-D1 single-NeedleItem shape so Container #0's fixed S-Meter
   header stays an arc needle. Thetis `addSMeterBar` bar
   composition preserved in new opt-in factory
   `createSMeterBarPreset`, not wired to any UI call site.
   Six tests retargeted at the new factory; a new
   `SMeter_preset_is_a_needle_by_default` guards the revert.

5. **`5852d87 fix(mainwindow): Reset Default Layout also rebuilds panel meter`**
   — pre-existing bug surfaced while trying to clear the stale
   bar S-Meter state left over from commit 4bba2c2. Reset used
   to skip the panel container entirely; now clears the panel
   MeterWidget and rebuilds the default S-Meter / Power/SWR /
   ALC groups alongside the floating-container teardown.

6. **`a8f72d9 fix(main): stop quit-time crash in messageHandler redactPii`**
   — pre-existing bug, every quit crashed with
   `EXC_BAD_ACCESS` in `QRegularExpression::pattern()`. Classic
   static-destruction-order fiasco: Qt's `QThreadDataDestroyer`
   emits warnings via `QMessageLogger` from `__cxa_finalize`
   after the function-local static regex objects in
   `redactPii()` had already been destroyed. Fix leaks the
   regex pointers intentionally so they survive teardown, plus
   `qInstallMessageHandler(nullptr)` right after `app.exec()`
   returns as belt-and-braces.

7. **`4ad29d3 fix(dialog): preserve stack metadata through Apply + snapshot clones`**
   — serialize / deserialize drops runtime-only stack metadata,
   so the dialog's Apply path was landing cloned items on the
   target MeterWidget with `m_stackSlot == -1`, which caused
   `reflowStackedItems()` to no-op on them. Fix copies
   `m_stackSlot` / `m_slotLocalY/H` across the clone alongside
   the existing MMIO-binding copy, and kicks a reflow at the
   end of `applyToContainer()`. Same fix applied on the
   dialog-open `populateItemList` side.

8. **`aececaa feat(meters): Thetis-parity normalized stack + ANAN MM 0.441 band`**
   — the big one. Replaces the Phase 3G-9 "compress composite
   to top 70%" layout with a faithful port of Thetis's
   Default Multimeter coordinate system:
   * Composite presets authored at Thetis-nominal normalized
     size directly by the factory. ANAN MM uses
     `(0, 0, 1, 0.441)` per
     `MeterManager.cs:22472 ni.Size = new SizeF(1f, 0.441f)`.
     No compress-to-fraction step runs anywhere.
   * Bar rows use Thetis `_fHeight = 0.05f` (normalized) from
     `MeterManager.cs:21266`, with a NereusSDR-only **24 px
     pixel floor** so rows stay readable in tight containers.
   * `MeterWidget::reflowStackedItems()` computes
     `slotHpx = max(0.05 * widgetH, 24)` and
     `bandTop = max(y + itemHeight)` over items with
     `itemHeight > 0.30` on every resize. Re-lays every
     stacked item from those two values.
   * `ContainerSettingsDialog::appendPresetRow` loses its
     compress-to-0.70 block entirely.
   * `MeterItem::layoutInStackSlot` signature grows a
     `bandTop` parameter; `m_stackBandTop` field removed.
   * `inferStackFromGeometry()` runs after
     `deserializeItems()` (MainWindow panel restore) so saved
     containers keep their reflow-on-resize behaviour without
     a persistence format bump.

**Handoff polish gaps #4 and #5** (`appendPresetRow` 70/30 split
hardcoded and `nextStackYPos` `h > 0.7` edge case) are **obsolete**
— the entire compress-and-stack logic they described was removed
in commit 8 above.

**Remaining polish gaps** (queued, not blockers):
- Scale tick labels duplicate at narrow widths
- `m_peakHoldDecayRatio = 0.02f` feels slow — consider `0.05f`
- QListWidget AX automation limitation
- `AutoHeight` container mode (Thetis `ucMeter.cs:903`)

---

## What this branch delivers

Completes the Thetis Default Multimeter parity work: every bar-row
meter type Thetis exposes in `Setup → Appearance → Meters/Gadgets`
now renders in NereusSDR with matching composition, colours, and
per-row calibration curves. Users can build multi-row multimeter
containers interactively by stacking presets in the Container
Settings dialog — matching how Thetis's Appearance dialog works.

### Phase inventory

| Phase | Area | Scope |
|---|---|---|
| A1 | BarItem | `ShowValue` + `ShowPeakValue` + `FontColour` + `m_peakValue` high-water tracking |
| A2 | BarItem | `ShowHistory` + `HistoryColour` + `HistoryDuration` + ring buffer |
| A3 | BarItem | `ShowMarker` + `MarkerColour` + `PeakHoldMarkerColour` + `PeakHoldDecayRatio` |
| A4 | BarItem | `BarStyle::Line` + non-linear `ScaleCalibration` map + `valueToNormalizedX()` |
| B1 | free fn | `readingName(bindingId)` — ported from `MeterManager.cs:2258-2318` |
| B2 | ScaleItem | `ShowType` + centered red title render via readingName |
| B3 | ScaleItem | `GeneralScale` two-tone baseline with major/minor tick math |
| B4 | Editor | `ScaleItemEditor` surfaces ShowType + title colour |
| C | Tests | Full A-phase tail-tolerant serialize sweep (3 round-trip cases) |
| D1 | Preset | `createSMeterPreset` rebuilt from `addSMeterBar:21499-21616` |
| D1b | Render | `BarItem::paint()` rewritten — Line style, history polyline, markers, ShowValue text |
| D1c | Layer | `BarItem::participatesIn(OverlayDynamic)` so paint() actually runs in MeterWidget's layered render loop |
| E1 | Factories | 16 per-reading bar-row factories rewritten via shared `buildBarRow()` helper, each citing its Thetis `Add*Bar` source |
| E2 | UX | Append-to-container flow: "RX/TX Meters (Thetis)" sections in the Available list, `appendPresetRow()` with slot rescaling |
| E3 | Dialog | `loadPresetByName` append-mode for bar-row presets (keeps composites on replace) |
| E4 | Dialog | Compress full-height composite into top 70% then append bar row into bottom 30% |

### Files touched

**Core meter subsystem** (13 files)
- `src/gui/meters/MeterItem.h` / `.cpp` — BarItem + ScaleItem API expansion, `readingName()` free function
- `src/gui/meters/ItemGroup.h` / `.cpp` — 16 per-reading wrapper rewrites, new `buildBarRow()` helper, `createSMeterPreset()` full rebuild
- `src/gui/containers/ContainerSettingsDialog.h` / `.cpp` — Meter Types picker sections, `appendPresetRow()`, append-mode routing, composite compression
- `src/gui/containers/meter_property_editors/ScaleItemEditor.h` / `.cpp` — ShowType checkbox + title colour swatch

**Tests** (5 files)
- `tests/tst_meter_item_bar.cpp` — 29 cases covering A1-A4 API, paint pixel scans, legacy payloads, full-phase round trip
- `tests/tst_meter_item_scale.cpp` — 16 cases covering ShowType paint, GeneralScale two-tone pixel scan
- `tests/tst_reading_name.cpp` — 26 assertions mapping every MeterBinding to its Thetis label
- `tests/tst_meter_presets.cpp` — 9 cases including PNG dumps for SMeter, ALC bar row, 3-row stack
- `tests/CMakeLists.txt` — 4 new test target registrations

**Docs** (2 files, new)
- `docs/architecture/meter-parity-audit.md` — full audit of 32 factories + 31 subclasses vs Thetis
- `docs/architecture/default-multimeter-row-port-plan.md` — 7-phase implementation plan

**Merge conflicts resolved** (3 files)
- `CMakeLists.txt` — combined main's app icon block with feat branch's `NereusSDRObjs` object-library pattern
- `src/gui/SpectrumWidget.h` — main-only addition of `m_wfTimestampTicker` QTimer
- `src/gui/SpectrumWidget.cpp` — 10 main-only polish hunks in Phase 3G-8 setters (`markOverlayDirty()`, comments)

---

## Commit sequence (newest first)

```
ba1c60f feat(dialog): compress full-height composite before appending bar row (E4)
6d2d495 feat(dialog): bar-row presets append instead of replace (Phase E3)
d8b7ae6 Merge branch 'feat/default-multimeter-row-port' into test/meters-on-main
a15cc0b feat(meters): Thetis-style Append-to-Container UX (Phase E2)
7e71588 feat(meters): Thetis-parity colours for bar row (Phase E polish)
e51e9df feat(meters): Phase E — per-reading bar rows from Thetis Add*Bar
198c1de fix(meters): BarItem participates in OverlayDynamic so paint() runs
f7093de feat(meters): BarItem::paint renders A-phase fields (Phase D1b)
4bba2c2 feat(meters): rebuild createSMeterPreset from Thetis addSMeterBar (Phase D1)
d516c5c test(meters): full A-phase tail-tolerance sweep (Phase C)
66f3a87 feat(meters): ScaleItemEditor surfaces ShowType + title colour (Phase B4)
12c475d feat(meters): ScaleItem GeneralScale two-tone baseline (Phase B3)
f8944dc feat(meters): ScaleItem ShowType + centered title render (Phase B2)
62717d6 feat(meters): readingName(bindingId) map (Phase B1)
ee4b234 feat(meters): BarItem BarStyle::Line + ScaleCalibration non-linear map (Phase A4)
dc61dc5 feat(meters): BarItem ShowMarker + PeakHoldMarker + decay (Phase A3)
5c6acd9 feat(meters): BarItem ShowHistory / HistoryColour / HistoryDuration (Phase A2)
de6d0c8 feat(meters): BarItem ShowValue / ShowPeakValue / FontColour + peak tracking (Phase A1)
30fdbdd docs(meters): audit NereusSDR vs Thetis meter subsystem + port plan
```

Below `30fdbdd` the branch carries 24 commits that originated in
`feature/phase3g7-polish` (PR #9 and its prep) which were never
merged into main. Those come along for the ride via the
`d8b7ae6` merge commit — they include:

- container item persistence round-trip fix (PR #9)
- AppSettings `aboutToQuit` save path
- Clock preset layout fix
- Load Preset stack Y offset
- ANAN MM-aware item stacking
- 8 test cases in `tst_container_persistence.cpp`

---

## Source-of-truth (Thetis) references

Every preset rewrite cites a specific Thetis source location in its
commit body and in-source comments. The key anchors:

| NereusSDR | Thetis source |
|---|---|
| `createSMeterPreset` | `MeterManager.cs:21523-21616` `addSMeterBar` |
| `createAlcPreset` | `MeterManager.cs:23326-23411` `AddALCBar` |
| `createMicPreset` | `MeterManager.cs:23004-23090` `AddMicBar` |
| `createEqBarPreset` | `MeterManager.cs:23091-23178` `AddEQBar` |
| `createLevelerBarPreset` | `MeterManager.cs:23179-23264` `AddLevelerBar` |
| `createLevelerGainBarPreset` | `MeterManager.cs:23265-23325` `AddLevelerGainBar` |
| `createAlcGainBarPreset` | `MeterManager.cs:23412-23472` `AddALCGainBar` |
| `createAlcGroupBarPreset` | `MeterManager.cs:23473-23533` `AddALCGroupBar` |
| `createCfcBarPreset` | `MeterManager.cs:23534-23619` `AddCFCBar` |
| `createCfcGainBarPreset` | `MeterManager.cs:23620-23680` `AddCFCGainBar` |
| `createCompPreset` | `MeterManager.cs:23681-23767` `AddCompBar` |
| `createAdcBarPreset` | `MeterManager.cs:21740-21827` `AddADCBar` |
| `createAdcMaxMagPreset` | `MeterManager.cs:21617-21677` `AddADCMaxMag` |
| `createPbsnrBarPreset` | `MeterManager.cs:21828-21898` `AddPBSNRBar` |
| `createAgcBarPreset` | `MeterManager.cs:21961-22050` `AddAGCBar` |
| `createAgcGainBarPreset` | `MeterManager.cs:21899-21960` `AddAGCGainBar` |
| `readingName()` | `MeterManager.cs:2258-2318` `ReadingName()` switch |
| `ScaleItem` `GeneralScale` | `MeterManager.cs:32338-32423` `generalScale()` helper |
| `ScaleItem` `ShowType` | `MeterManager.cs:31879-31886` render pass |
| Thetis bar-row colours | Setup → Appearance → Meters/Gadgets dialog (screenshot in conversation) |

---

## Known polish gaps (not blockers, queue for follow-up)

1. **Title-overlaps-tick at row heights ≤ 32 px.** Below ~35 px the
   red `ShowType` title (top 33% of scale rect) and the top of the
   GeneralScale tick row (extends upward by `h*0.30`) crowd each
   other. Fix: clamp tick height to `h*0.15` when `h < 40 px`, or
   when `ShowType` is enabled shrink the tick-row band to the
   bottom 60% of the scale rect. 5-minute fix.

2. **`m_peakFontColour` serialize round-trip not wired.** The
   BarItem field was added for Phase E2 colour polish but never
   got its tail slot in `serialize()/deserialize()`. A preset
   loaded from disk will fall back to `m_fontColour` for peak
   text colour, which happens to be white instead of red. Minor
   drift; fix is a 3-line tail append + bump `tst_container_persistence`
   by one field.

3. **`ShowPeakValue` skips non-finite values.** When a binding has
   no live data (e.g. TX not active) the peak stays at `-inf`
   and the right-side peak text is blank. Acceptable default but
   confusing during testing against parked signals. Consider
   rendering "--" as a placeholder.

4. **`appendPresetRow` compress-to-70% is hardcoded.** Could be a
   `float m_compositeReserveFrac{0.70f}` per-dialog setting or
   a user preference. Not urgent — 70/30 is a sane default.

5. **`nextStackYPos` `h > 0.7` filter edge case.** After composite
   compression, the full-height background ends up at exactly
   `h = 0.7` (not `> 0.7`), so it correctly participates in the
   stack math. But if a caller passes a compressed item at
   `h = 0.7001` the filter kicks in and the next row lands at
   `yPos = 0`, overlapping the composite. Defensive fix: clamp
   composite scale factor to `0.6999f` so compressed items stay
   safely under the threshold.

6. **Scale tick labels can duplicate at small widths.** The
   `Linear` renderer spaces ticks evenly by `m_majorTicks`, which
   on a narrow row can cause adjacent labels to overlap. Fix:
   skip rendering labels when measuredWidth > availableWidth /
   (majorTicks - 1).

7. **`m_peakHoldDecayRatio = 0.02f` may feel slow.** The ALC/EQ/
   Mic rows use a very slow decay which holds peak markers for
   many seconds after a transient. Acceptable for some users,
   frustrating for others. Consider `0.05f` default.

8. **UI automation flakiness in QListWidget.** AppleScript
   couldn't enumerate AXRow children of the Available list
   reliably. Not a user-facing bug but affects regression testing
   of the dialog flow. If future work wants automated dialog
   tests, consider adding a `QAccessibleInterface` shim or a
   hidden debug API that exposes the selected row by index.

---

## Polish gap verification checklist

Before PR, morning-you should sanity-check these by hand in the
live app:

- [ ] Launch the merged build, main window renders spectrum + waterfall + applet panel
- [ ] `Containers → New Container → Edit Container → Presets → TX Meters → ALC` appends at y=0 without a Replace prompt
- [ ] Append EQ, Mic, Compression, Leveler in sequence — 5 rows should stack cleanly
- [ ] `Presets → S-Meter → S-Meter Only` prompts Replace, nukes stack, loads calibrated bar
- [ ] `Presets → Composite → ANAN Multi Meter` prompts Replace, loads 7-needle panel
- [ ] With ANAN MM loaded, `Presets → TX Meters → ALC` should compress the needles to top 70% and add ALC at y=0.70
- [ ] Quit app, relaunch, every saved container comes back intact
- [ ] Edit an existing BarItem's binding dropdown — the ScaleItem title updates via `readingName()` on next paint
- [ ] Raw items still work: `Add Item → TX items → Bar Meter` creates a bare BarItem (not a full preset)

---

## Build & test commands

```bash
cd /Users/j.j.boyd/NereusSDR/.worktrees/default-multimeter-row

# Configure + build tests
cmake -B build-tests -G Ninja -DNEREUS_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-tests

# Run full suite
ctest --test-dir build-tests --output-on-failure

# Launch the app from the built bundle
open build-tests/NereusSDR.app

# Regenerate the PNG dumps (useful for visual inspection)
./build-tests/tests/tst_meter_presets
ls /tmp/sMeterPreset.png /tmp/alcBarRow.png /tmp/threeRowStack.png
```

---

## Ship checklist

1. Commit any remaining polish fixes (#1, #2 from the known-gaps list)
2. Re-run full suite: `ctest --test-dir build-tests`
3. Live smoke test in the app (see checklist above)
4. `git push -u origin test/meters-on-main`
5. `gh pr create --base main --head test/meters-on-main` with the
   body template in `phase3g9-pr-body.md` (generated in the next
   section of this doc)
6. After PR opens, open the PR URL in a browser (standing rule)
7. Wait for CI; fix any Linux/Windows-only build gaps that don't
   show up on the macOS dev build

---

*Handoff doc generated 2026-04-13 at end of working session. Author
was getting sleepy, the work landed visually correct on the live
app, ANAN compression + bar row stacking confirmed via pixel
inspection, all tests green.*
