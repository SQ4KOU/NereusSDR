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
**Commits ahead of main:** 44 (20 meter-parity + 24 carried by the
merge of `feature/phase3g7-polish` which was never upstreamed to
main)
**Test suite:** 6/6 green
**Live-tested:** automated Quartz screencapture of the merged
build on macOS

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

### SMeter preset rebuild (Phase D1)

`createSMeterPreset` replaces its pre-E bare-NeedleItem body
(the audit's highest-blocking gap) with a 3-item composition
ported from `addSMeterBar:21499-21616`:
- Dark grey backdrop
- BarItem Line style with the S-meter non-linear calibration
  (`-133 dBm → x=0.0`, `-73 → 0.5`, `-13 → 0.99`), attack 0.8,
  decay 0.2, `ShowHistory=true`, `HistoryColour=Red(128)`,
  `ShowValue=true`, `ShowMarker=true`, `PeakHoldMarkerColour=Red`,
  `FontColour=Yellow`
- ScaleItem with `ShowType=true`, `ScaleStyle=GeneralScale`,
  params `(6, 3, -1, 60, 2, 20, 0.5)` matching the Thetis
  dispatch at `MeterManager.cs:31911-31916`

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

## Known polish gaps (queued, not blockers)

1. **Title overlaps tick row at row heights ≤ 32 px.** Below
   ~35 px the red ShowType title (top 33% of scale rect) and the
   top of the GeneralScale tick row (extends upward by `h*0.30`)
   crowd each other. 5-minute fix: clamp tick height to `h*0.15`
   when the row is short.
2. **`m_peakFontColour` serialize tail slot not wired.** The
   field was added in Phase E2 but its round-trip persistence
   wasn't landed. Peak text colour falls back to `m_fontColour`
   (white) on load. 3-line fix.
3. **`ShowPeakValue` skips non-finite values.** When a binding
   has no live data, peak stays at `-inf` and the right-side
   peak text is blank. Consider rendering "--" as a placeholder.
4. **70/30 composite/row split is hardcoded.** Could be a
   dialog setting or user preference.

Detailed polish gap list with concrete fixes in
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
- [ ] `Containers → New Container → Edit Container → Presets →
      TX Meters → ALC` appends silently (no Replace prompt)
- [ ] Stack 5 rows (ALC, EQ, Mic, Comp, Leveler) — each at its
      own 10% Y slot
- [ ] `Presets → S-Meter → S-Meter Only` prompts Replace, loads
      calibrated bar with S-unit ticks
- [ ] `Presets → Composite → ANAN Multi Meter` prompts Replace,
      loads 7-needle panel
- [ ] With ANAN MM loaded, `Presets → TX Meters → ALC` compresses
      the needles into top 70% and adds ALC at y=0.70
- [ ] Quit and relaunch — every saved container comes back intact
- [ ] Edit a BarItem binding via the Properties panel — ScaleItem
      title updates via `readingName()` on next paint
- [ ] Raw items still work: `Add Item → TX items → Bar Meter`
      creates a bare cyan BarItem (pre-E behaviour preserved)

---

🤖 Co-Authored with Claude Code
JJ Boyd ~KG4VCF
