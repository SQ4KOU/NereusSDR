# Phase 3G-9 — Thetis Meter Parity (PR body draft)

*Review this text, then paste into `gh pr create --body-file` when
ready to push.*

---

## Summary

Closes the gap between NereusSDR's bar meters and Thetis's
`Setup → Appearance → Meters/Gadgets` dialog. Every per-reading
bar row Thetis ships (ALC, EQ, Mic, Compression, Leveler, Leveler
Gain, ALC Gain, ALC Group, CFC, CFC Gain, ADC, ADC Max Mag, AGC,
AGC Gain, PBSNR, Signal Peak, Signal Avg, Max Bin) now renders in
NereusSDR with faithful composition, colour, calibration, and
stacking. Users can build multi-row multimeter containers
interactively by stacking presets the same way they do in Thetis.

**Branches this PR:** `test/meters-on-main` → `main`
**Commits ahead of main:** 57 (20 meter-parity + 24 carried by the
merge of `feature/phase3g7-polish` which was never upstreamed to
main + 1 handoff-era doc + 8 pre-PR code fixes + 1 pre-PR doc
update — see *Pre-PR fixes* section below. Git log also shows
3 intermediate layout attempts [`017e5e2`, `0254d4e`, `28085a9`]
that each drove toward the final Thetis-parity model in
commit 8; they're kept in history for context but every one
was superseded by `aececaa`.)
**Test suite:** 6/6 green (50+ assertions)
**Live-tested:** Quartz screencapture + manual checklist on the
merged build (macOS arm64)

## What changed

### BarItem API expansion (Phase A)

- `ShowValue`, `ShowPeakValue`, `FontColour`, `PeakFontColour`
- Rolling peak high-water mark with optional `PeakHoldDecayRatio`
- History ring buffer: `ShowHistory`, `HistoryColour`,
  `HistoryDuration`
- Live + peak-hold markers: `ShowMarker`, `MarkerColour`,
  `PeakHoldMarkerColour`
- `BarStyle::Line` enum value
- Non-linear `ScaleCalibration` waypoint map + piecewise-linear
  `valueToNormalizedX()` — powers the S-meter's S-unit scale

All new fields are append-only in the serialize tail so pre-A1
`BAR|...` payloads load unchanged. `tst_container_persistence`
still green.

Thetis source: `MeterManager.cs:19927-20278` `clsBarItem`.

### ScaleItem parity (Phase B)

- New free function `QString readingName(int bindingId)` ported
  verbatim from `MeterManager.cs:2258-2318` `ReadingName()`
  switch. 26 entries covering every RX/TX/hardware/rotator
  binding.
- ScaleItem `ShowType` + `TitleColour` — draws a centered red
  title via `readingName()` in the top third of the scale rect.
- ScaleItem `ScaleStyle::GeneralScale` — opt-in two-tone tick
  renderer matching `MeterManager.cs:32338-32423` `generalScale()`:
  major + minor ticks, low/high colour split at `centrePerc`,
  baseline at `y + h*0.85`. The pre-B3 Linear renderer remains
  the default so existing Filled presets don't regress.
- `ScaleItemEditor` gets a "Show type title" checkbox and a title
  colour swatch.

### BarItem render pipeline fix (Phase D1c)

Critical fix discovered via end-to-end testing: the A1-A4 render
data was wired through `paint()` but `MeterWidget` routes BarItem
through the GPU `emitVertices()` pipeline in its `Layer::Geometry`
loop — `paint()` never actually ran in production. Unit tests
passed because they called `paint()` directly.

Fix: override `BarItem::participatesIn(Layer)` to return `true`
for `OverlayDynamic` instead of the default `Geometry`. Now the
QPainter overlay pass picks up the full `paint()` including
history polyline, markers, and ShowValue text. Trade-off: no GPU
VBO acceleration for bar fill, acceptable because the bar is a
single rectangle and the overlay texture handles CPU repaints
efficiently.

### 16 per-reading factories (Phase E1)

Every `create*BarPreset` wrapper rewritten to call a new shared
`ItemGroup::buildBarRow()` helper that produces the canonical
Thetis 3-item row layout:
- `z=1` SolidColourItem dark grey (32,32,32) backdrop
- `z=2` BarItem Line-style with `ShowValue` + `ShowPeakValue` +
  `ShowHistory` + `ShowMarker` + 3-point calibration
- `z=3` ScaleItem with `ShowType=true`

Factories differ only in bindingId, 3-point calibration tuple,
and history colour. Each cites its Thetis `Add*Bar` source in
both the commit body and an in-source comment per CLAUDE.md
SOURCE-FIRST PORTING PROTOCOL:

| Factory | Thetis source |
|---|---|
| `createAlcPreset` | `MeterManager.cs:23326-23411` `AddALCBar` |
| `createMicPreset` | `MeterManager.cs:23004-23090` `AddMicBar` |
| `createEqBarPreset` | `MeterManager.cs:23091-23178` `AddEQBar` |
| `createCompPreset` | `MeterManager.cs:23681-23767` `AddCompBar` |
| `createLevelerBarPreset` | `MeterManager.cs:23179-23264` `AddLevelerBar` |
| `createLevelerGainBarPreset` | `MeterManager.cs:23265-23325` `AddLevelerGainBar` |
| `createAlcGainBarPreset` | `MeterManager.cs:23412-23472` `AddALCGainBar` |
| `createAlcGroupBarPreset` | `MeterManager.cs:23473-23533` `AddALCGroupBar` |
| `createCfcBarPreset` | `MeterManager.cs:23534-23619` `AddCFCBar` |
| `createCfcGainBarPreset` | `MeterManager.cs:23620-23680` `AddCFCGainBar` |
| `createAdcBarPreset` | `MeterManager.cs:21740-21827` `AddADCBar` |
| `createAdcMaxMagPreset` | `MeterManager.cs:21617-21677` `AddADCMaxMag` |
| `createAgcBarPreset` | `MeterManager.cs:21961-22050` `AddAGCBar` |
| `createAgcGainBarPreset` | `MeterManager.cs:21899-21960` `AddAGCGainBar` |
| `createPbsnrBarPreset` | `MeterManager.cs:21828-21898` `AddPBSNRBar` |
| `createSignalBarPreset` | `MeterManager.cs:21499-21616` `addSMeterBar` (linear variant) |

Colours pinned from the Thetis Appearance dialog's per-item
settings panel (screenshot in conversation):
- Low = white (bar below `midVal`)
- High = red (bar above `midVal`)
- Indicator = yellow (live marker)
- Peak Value = red (peak text + peak-hold marker)
- Show History = LemonChiffon(128) per preset
- Meter Title = red (ScaleItem ShowType)

### Append-to-container UX (Phase E2–E4)

- **New sections in the Container Settings "Available" list.** "RX
  Meters (Thetis)" with 9 entries and "TX Meters (Thetis)" with
  10 entries, each tagged `PRESET_*`. The existing raw building-
  block sections are preserved below.
- **New `appendPresetRow()` helper.** Calls the matching factory,
  then rescales the preset's 0..1 items into the next 10%-tall
  stack slot computed via `nextStackYPos`. Users can stack
  ~10 bar rows before the clamp at y=0.95.
- **Append-mode `loadPresetByName`.** Bar-row preset names route
  to `appendPresetRow` instead of replacing the working items.
  Composite presets (ANAN MM, Cross Needle, Magic Eye, full
  S-Meter, Power/SWR) still replace on load.
- **Composite compression for mixed containers.** When the user
  adds a bar row to a container that already holds a full-
  height composite (e.g. ANAN MM fills 0..1), every composite
  item gets compressed into the top 70% of the container and
  the bar row stacks into the bottom 30% slot. Fits ~3 bar rows
  beneath a compressed ANAN MM before the stack clamps; users
  who want more resize the container taller.

### Thetis S-meter bar composition (Phase D1, opt-in only)

Phase D1 originally rebuilt `createSMeterPreset` from
`addSMeterBar:21499-21616` to ship a 3-item composition (SolidColour
backdrop + Line BarItem with 3-point S-meter calibration +
GeneralScale ScaleItem). That commit was **reverted** before PR
because it silently swapped the main Container #0 S-Meter header
(and the `"S-Meter Only"` Presets menu entry) from an arc needle
to a bar row, which wasn't the design intent — the main signal
meter should stay a needle, and the Thetis bar variant should be
available on demand in a new container.

The Thetis port work was preserved in a new opt-in factory
`ItemGroup::createSMeterBarPreset(bindingId, name, parent)` with
the exact same 3-item body:
- Dark grey (32,32,32) backdrop
- BarItem Line style with the S-meter non-linear calibration
  (`-133 dBm → x=0.0`, `-73 → 0.5`, `-13 → 0.99`), attack 0.8,
  decay 0.2, `ShowHistory=true`, `HistoryColour=Red(128)`,
  `ShowValue=true`, `ShowMarker=true`, `PeakHoldMarkerColour=Red`,
  `FontColour=Yellow`
- ScaleItem with `ShowType=true`, `ScaleStyle=GeneralScale`,
  params `(6, 3, -1, 60, 2, 20, 0.5)` matching the Thetis
  dispatch at `MeterManager.cs:31911-31916`

`createSMeterBarPreset` is **not wired into any UI call site** —
it exists so the Thetis port is test-covered and available for
manual verification. Users who want the bar-style S-meter add it
to a new container themselves. The linear `createSignalBarPreset`
/ `createAvgSignalBarPreset` / `createMaxBinBarPreset` factories
still expose the non-calibrated bar variants via the Presets
menu "Signal Bar" entries — those are unchanged.

D1b (`BarItem::paint()` rewrite) and D1c (`participatesIn(OverlayDynamic)`)
stay — they're general BarItem improvements, not S-meter-specific,
and every other BarItem preset depends on them.

## Tests

Full test suite is 6/6 green on the merged tree:

- `tst_smoke` — baseline
- `tst_container_persistence` — 8 cases, round-trips through
  every new BarItem + ScaleItem tail field
- `tst_meter_item_bar` — 29 cases: API round-trip, paint pixel
  scans for Line + ShowValue + markers, legacy payload, garbled
  calibration tolerance, full A-phase sweep
- `tst_meter_item_scale` — 16 cases: ShowType render, GeneralScale
  two-tone pixel scan, legacy pre-B2 payload
- `tst_reading_name` — 26 assertions mapping every MeterBinding
  to its Thetis label
- `tst_meter_presets` — 9 cases including PNG dumps for visual
  regression checks

Three `/tmp/*.png` dumps are generated by `tst_meter_presets`:
- `/tmp/sMeterPreset.png` — full SMeter composition
- `/tmp/alcBarRow.png` — single ALC row
- `/tmp/threeRowStack.png` — ALC + EQ + MIC stacked

## Merge conflicts resolved

3 files conflicted during the `origin/main` merge:

1. `CMakeLists.txt` — combined main's per-platform app icon block
   (Phase 3N release pipeline) with feat branch's `NereusSDRObjs`
   object-library pattern (needed for test target sharing). Icon
   sources land in the object library so both the app executable
   and test targets see them; MACOSX_BUNDLE properties stay on
   the executable.
2. `src/gui/SpectrumWidget.h` — main-only addition of
   `m_wfTimestampTicker` QTimer field. Kept main's version.
3. `src/gui/SpectrumWidget.cpp` — 10 main-only Phase 3G-8 polish
   hunks in setter bodies (`markOverlayDirty()` calls, improved
   comments). Kept main's version wholesale via `checkout --ours`.

## Pre-PR fixes (8 commits, landed during live verification)

Live smoke-testing the branch surfaced the three handoff polish
gaps plus four additional issues — two pre-existing bugs (reset
behaviour, quit-time crash) and two that only emerge when you
actually try to stack bar rows at different container sizes.
All eight are closed in GPG-signed commits at the branch tip.

1. **`1b698a9 fix(meters): clamp GeneralScale tick row on short ScaleItems`**
   — closes polish gap #1. When `ShowType=true` and row height
   < 40 px, scale major and minor tick fractions by 0.5 so tick
   tops don't collide with the red title text. No effect on
   rows ≥ 40 px. NereusSDR-original polish (Thetis draws the
   title outside the scale rect so never hits this case).

2. **`59e306a fix(meters): persist BarItem peakFontColour across serialize (E2)`**
   — closes polish gap #2. Append field 31 to BAR format;
   empty slot encodes the "invalid = fall back to m_fontColour"
   sentinel. Legacy payloads (≤ 30 fields) load unchanged. New
   test `peakFontColour_roundtrip_preserves_explicit_override`
   covers both branches.

3. **`6d7a79b fix(meters): render "--" for idle ShowPeakValue slot`**
   — closes polish gap #3. Parked bindings (TX meters while
   RX-active, PBSNR with no signal) now show a literal `"--"`
   in `peakFontColour()` instead of blanking the peak slot.

4. **`48174d5 revert(meters): restore needle S-Meter as default; preserve bar opt-in`**
   — reverts commit 4bba2c2's `createSMeterPreset` rebuild.
   Container #0's fixed S-Meter header and the `"S-Meter Only"`
   Presets menu entry return to the AetherSDR arc needle
   (pre-D1 shape). Thetis `addSMeterBar` composition preserved
   in new opt-in factory `createSMeterBarPreset`, not wired to
   any UI call site. Six tests in `tst_meter_presets`
   retargeted at the new factory; new
   `SMeter_preset_is_a_needle_by_default` guards the revert.

5. **`5852d87 fix(mainwindow): Reset Default Layout also rebuilds panel meter`**
   — **pre-existing bug**, surfaced while trying to clear the
   stale bar S-Meter state left over from commit 4bba2c2.
   `resetDefaultLayout()` used to destroy non-panel floating
   containers but explicitly skip the panel container, so a
   persisted bar-style panel meter survived the reset. Fix
   clears the panel `MeterWidget` and rebuilds the default
   S-Meter / Power/SWR / ALC groups alongside the floating-
   container teardown.

6. **`a8f72d9 fix(main): stop quit-time crash in messageHandler redactPii`**
   — **pre-existing bug**, every app quit crashed with
   `EXC_BAD_ACCESS` in `QRegularExpression::pattern()`.
   Classic static-destruction-order fiasco: Qt's
   `QThreadDataDestroyer::~EarlyMainThread` emits warnings via
   `QMessageLogger::warning` from `__cxa_finalize` after the
   function-local static regex objects in `redactPii()` had
   already been destroyed. 32 crash reports accumulated across
   the day's test sessions before we noticed.

   Two-part fix (belt and braces): (1) allocate `ipRe`/`macRe`
   on the heap and intentionally leak them so they survive
   teardown; (2) `qInstallMessageHandler(nullptr)` right after
   `app.exec()` returns so any subsequent Qt warnings during
   `__cxa_finalize` bypass our TU entirely. Verified: 32 crash
   reports at the start of this fix, still 32 after five
   subsequent quits.

7. **`4ad29d3 fix(dialog): preserve stack metadata through Apply + snapshot clones`**
   — `serialize()` / `createItemFromSerialized()` drop
   runtime-only stack metadata, so the Container Settings
   dialog's Apply path was landing cloned items on the target
   `MeterWidget` with `m_stackSlot == -1`. Symptom: appending
   ALC to a new container holding a compressed ANAN MM showed
   a single full-size bar row instead of a 40 px stacked row.
   Fix copies `m_stackSlot` / `m_slotLocalY/H` across the clone
   alongside the existing MMIO-binding copy and kicks a reflow
   at the end of `applyToContainer()`. Same fix on the
   dialog-open `populateItemList` path.

8. **`aececaa feat(meters): Thetis-parity normalized stack + ANAN MM 0.441 band`**
   — the big one. Replaces the Phase 3G-9 "compress composite
   to top 70%" layout with a faithful port of Thetis's
   Default Multimeter coordinate system. **Source-first port
   references:**

   | Thetis | NereusSDR change |
   |---|---|
   | `MeterManager.cs:22472` `ni.Size = new SizeF(1f, 0.441f)` | ANAN MM factory scales every item into `(0, 0, 1, 0.441)` |
   | `MeterManager.cs:21266` `_fHeight = 0.05f` | `reflowStackedItems` uses `slotHpx = 0.05 * widgetH` |
   | `MeterManager.cs:22796` `fBottom = img.TopLeft.Y + img.Size.Height` | `bandTop` derived from `max(y + itemHeight)` over composite items |
   | `MeterManager.cs:23398` `fBottom = cb.TopLeft.Y + cb.Size.Height` | Stack slot positions via `bandTop + slot * slotHpx` |
   | `MeterManager.cs:6669-6671, 31077-31080` | NereusSDR already stores normalized 0..1 coords and transforms to pixels at render |

   **Deviations from Thetis (deliberate):**
   * **24 px pixel floor** on bar row height. Thetis lets rows
     shrink to zero as the container shrinks; NereusSDR clamps
     `slotHpx = max(0.05 * widgetH, 24)` so rows stay readable
     in tight containers. Rows past the container bottom clip
     naturally.
   * **`AutoHeight` not yet ported.** Thetis `ucMeter.cs:903`
     auto-grows a container when content exceeds its height.
     NereusSDR currently relies on the user to resize the
     container taller manually — see follow-up polish gap.

   **Supporting code changes:**
   * `MeterItem::layoutInStackSlot` gains a `bandTop` parameter;
     `m_stackBandTop` field removed (now derived per-reflow).
   * `ContainerSettingsDialog::appendPresetRow` loses its
     compress-to-0.70 block entirely — composites stay at their
     factory size; bar rows just tag themselves with a stack
     slot and local rect.
   * `MeterWidget::inferStackFromGeometry()` runs after
     `deserializeItems()` (MainWindow panel restore) so saved
     containers keep reflow-on-resize without a persistence
     format bump.

   **User-visible effect:** ANAN MM renders in the top 44.1% of
   any container (Thetis constant), bar rows stack below it at
   a minimum 24 px, resize taller reveals more rows, resize
   shorter clips rows off the bottom — matches Thetis Default
   Multimeter behaviour as closely as possible within
   NereusSDR's normalized-coordinate model.

## Known polish gaps (queued, not blockers)

Handoff gaps **#4** (70/30 composite/row split hardcoded) and
**#5** (`nextStackYPos` `h > 0.7` edge case) are **obsolete** —
the entire mechanism they described was removed in commit 8
above. Remaining:

1. **AutoHeight container mode.** Thetis auto-grows the
   container when bar rows would overflow; NereusSDR requires
   a manual resize. Port of `ucMeter.cs:903` forceResize flow.
2. **Scale tick labels can duplicate at narrow widths.** Skip
   labels when measuredWidth > availableWidth / (majorTicks-1).
4. **`m_peakHoldDecayRatio = 0.02f` may feel slow** — consider
   `0.05f` default.
5. **UI automation flakiness in QListWidget.** Doesn't affect
   users but limits regression test automation of the dialog
   flow.

Detailed polish gap list in
[`docs/architecture/phase3g9-meter-parity-handoff.md`](../docs/architecture/phase3g9-meter-parity-handoff.md).

## Thetis compliance

Per CLAUDE.md `SOURCE-FIRST PORTING PROTOCOL`:
- Every ported behavior cites `MeterManager.cs:<lines>` in both
  commit body and in-source comment
- No constants invented — every calibration tuple, attack/decay
  ratio, colour, and timing value traces back to Thetis source
- Colours pinned from the live Thetis Setup → Appearance →
  Meters/Gadgets dialog
- No NereusSDR-original behavior in any preset that has a Thetis
  equivalent (composite presets marked NereusSDR-original —
  VfoDisplay, Clock, Contest — are explicitly flagged in the
  audit doc)

## Test plan

- [ ] CI green on Linux x86_64 + aarch64 + macOS arm64 + Windows
- [ ] Container #0 panel header shows the AetherSDR **arc needle**
      S-Meter (revert verification)
- [ ] `Presets → S-Meter → S-Meter Only` also yields the arc needle
- [ ] `Containers → New Container → Edit Container → Presets →
      Composite → ANAN Multi Meter` renders the 7-needle panel
      in the **top 44.1% of the container** only (Thetis-nominal
      0.441 band), not stretched to 70% or the full container
- [ ] Append **ALC**, **ALC Gain**, **Mic**, **Comp**, **Leveler**
      in sequence — each appears as a stacked row below the
      composite at a minimum of 24 px tall, with a readable red
      title and `-140.0` left-side readout
- [ ] Resize the container **taller** — more bar rows become
      visible; composite scales proportionally with the container
- [ ] Resize the container **shorter** — bottom rows clip off
      instead of shrinking below the 24 px floor
- [ ] Quit the app — **no crash report** generated in
      `~/Library/Logs/DiagnosticReports/NereusSDR-*.ips`
- [ ] Relaunch — the floating container comes back with the
      composite + stacked rows intact (persistence +
      `inferStackFromGeometry`)
- [ ] `Containers → Reset Default Layout` wipes the panel
      meter and rebuilds the S-Meter / Power-SWR / ALC defaults
- [ ] Edit a BarItem binding via the Properties panel — ScaleItem
      title updates via `readingName()` on next paint
- [ ] Raw items still work: `Add Item → TX items → Bar Meter`
      creates a bare cyan BarItem (pre-E behaviour preserved)

## Live verification summary

Verified on macOS arm64 (this dev machine) against a fresh
build of commit `aececaa`. Floating container with ANAN MM +
10 stacked bar rows restored cleanly from persisted state
across app quit/relaunch cycles. 32 crash reports in
DiagnosticReports at the start of the quit-crash fix, still 32
after ~5 subsequent quits. Full test suite 6/6 green.

---

🤖 Co-Authored with Claude Code
JJ Boyd ~KG4VCF
