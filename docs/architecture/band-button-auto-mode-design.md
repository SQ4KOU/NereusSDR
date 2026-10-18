# Band Button Auto-Mode Design

**Issue:** [#118](https://github.com/boydsoftprez/NereusSDR/issues/118) — "set auto sideband mode for each band"
**Status:** Design approved, awaiting plan
**Scope:** Minimal fix for a shipped bug; closes #118 without expanding into Epic D (bandplan overlay) or Phase-C (full Thetis BandStack2 port)
**Thetis source reference:** `v2.10.3.13 @ 501e3f51`

---

## Problem

Clicking a band button in NereusSDR leaves the DSP mode wrong. There are two band-button entry points today, and neither sets mode:

1. **Spectrum overlay flyout** — the `BAND` button on `SpectrumOverlayPanel`. Emits `bandSelected(name, freqHz, mode)`. The connection in `MainWindow::wireSlice` calls `slice->setFrequency(freqHz)` but **explicitly discards the mode parameter** (see [MainWindow.cpp:2612-2615](../../src/gui/MainWindow.cpp:2612)). Clicking 80m tunes to 3.75 MHz but the mode stays wherever it was — USB → tuned into LSB territory, still decoding as USB.
2. **Container `BandButtonItem`** — the band button inside a meter container. Emits `bandClicked(int)` through `ContainerWidget::bandClicked(int)`, which has **no downstream handler**. Click is silently dropped.

The user reporting #118 expected the behavior every ham console has had for thirty years: click `20m` → VFO jumps into 20m, mode switches to USB; click `40m` → jumps into 40m, mode switches to LSB. Today both paths leave mode stale.

## Goal

Wire the orphaned signal. When the user clicks a band button on the active slice:

- If they're already on that band → no-op.
- If they've been on that band before → restore their last freq/mode/filter for that band.
- If they've never been on that band → seed from a Thetis-derived table (first voice entry, Region 2) and remember the seeded state so next visit restores instead of re-seeding.

## Non-goals

- **Multi-entry band stacks** (Thetis BandStack2) — repeated-click cycling, Shift=previous, Ctrl=add. Single-entry memory only. Full stack is a later phase.
- **Bandplan overlay** (Epic D from [phase3g-rx-experience-epic-design.md:37](phase3g-rx-experience-epic-design.md:37)) — semi-transparent mode-segment painting on the spectrum widget. Separate scope.
- **XVTR seed** — no default freq possible without user-configured transverters. XVTR click with no persisted state is a logged no-op. Becomes sensible once the XVTR epic lands.
- **GEN sub-band stack** — Thetis GEN is a bundle of broadcast sub-bands (LMF, 120m, 90m, 61m, 49m, …). NereusSDR ports a single entry for now (13.845 MHz SAM, Thetis's index-0).
- **RX2 / multi-slice behavior** — handler acts on whichever slice is "active" via `RadioModel::activeSlice()`. Multi-RX is Phase 3F.
- **AetherSDR bandplan JSON integration** — the 4 JSONs in `resources/bandplans/` already exist in-tree but are not yet wired to a model. Epic D handles that port. See Design decision 4.

---

## Design

### 1. Architecture

One new utility + wiring in two existing classes. No new threads, no cross-thread signals, no model additions.

| File | Change | Purpose |
| --- | --- | --- |
| `src/models/BandDefaults.h` *(new)* | `struct BandSeed { Band band; double frequencyHz; DSPMode mode; bool valid; }; constexpr BandSeed seedFor(Band);` | Pure-data seed lookup, 14 entries. |
| `src/models/BandDefaults.cpp` *(new)* | `constexpr` table with inline Thetis cites | Seed values sourced verbatim from Thetis Region 2. |
| `src/models/SliceModel.h` | Add `bool hasSettingsFor(Band) const;` | AppSettings key probe for first-visit detection. |
| `src/models/SliceModel.cpp` | Implement `hasSettingsFor` | Probes `slices/<idx>/bands/<key>/DspMode`. |
| `src/models/RadioModel.h` | Add `void onBandButtonClicked(Band);` public slot | Central handler. |
| `src/models/RadioModel.cpp` | Implement `onBandButtonClicked` | Uses `activeSlice()`; calls existing `saveToSettings`/`restoreFromSettings` from Phase 3G-10 Stage 2. |
| `src/gui/MainWindow.cpp` | (a) Replace the existing `SpectrumOverlayPanel::bandSelected` lambda so it routes through `RadioModel::onBandButtonClicked(Band)` instead of calling `slice->setFrequency` and dropping mode. (b) Add new connection `ContainerWidget::bandClicked(int)` → `RadioModel::onBandButtonClicked(Band::bandFromUiIndex(idx))` to close the orphaned signal. | Converges both band-button entry points on one handler. |
| *(no change)* `src/gui/SpectrumOverlayPanel.cpp` | `kBands` table's inline `freqHz`/`mode` fields become unused (handler now drives seeds/restore). Left in place to avoid churn; can be pruned to name-only later. | Avoids signature change on `bandSelected(QString, double, QString)`. |

The click handler lives on `RadioModel` because that class already owns `activeSlice()` and serves as the signal-routing hub (per `CLAUDE.md`). `BandDefaults` is a pure-data module so unit tests can hit it directly without constructing a radio.

### 2. Band seed table (US Region 2, voice-entry pick)

**Source:** Thetis [`clsBandStackManager.cs:2099-2176`](../../../Thetis/Project Files/Source/Console/clsBandStackManager.cs) `AddRegion2BandStack()`.

**Selection rule:** Thetis seeds each band with multiple entries (CW variants first, voice, digital). `SelectInitial()` returns index 0 — which is CW for almost every amateur band. That contradicts the #118 bug report's expectation of SSB-on-click. NereusSDR single-entry seed picks **the first LSB/USB entry** from each Thetis-seeded band (or the first entry if no voice exists, e.g. 30m which is voice-prohibited).

Mode-selection policy is a NereusSDR UI choice and is documented as a deliberate deviation from Thetis's `SelectInitial()` behavior. **Frequencies come verbatim from Thetis** — source-first on constants per the standing rule in CLAUDE.md.

| Band | Seed freq (MHz) | Mode | Thetis cite |
| --- | --- | --- | --- |
| 160m | 1.840 | LSB | `clsBandStackManager.cs:2104 [v2.10.3.13]` |
| 80m  | 3.650 | LSB | `:2109` |
| 60m  | 5.354 | USB | `:2114` (only uncommented entry) |
| 40m  | 7.152 | LSB | `:2120` |
| 30m  | 10.107 | CWU | `:2123` (voice prohibited per FCC §97.305(c)) |
| 20m  | 14.155 | USB | `:2130` |
| 17m  | 18.125 | USB | `:2135` |
| 15m  | 21.205 | USB | `:2140` |
| 12m  | 24.931 | USB | `:2145` |
| 10m  | 28.305 | USB | `:2150` |
| 6m   | 50.125 | USB | `:2155` |
| WWV  | 10.000 | SAM | `:2165` (5 entries; 10 MHz is mid-list and most commonly usable) |
| GEN  | 13.845 | SAM | `:2168` (index 0 of 5) |
| XVTR | — | — | no seed; handler no-ops until XVTR epic |

Note: Thetis uses **SAM** (synchronous AM) for WWV and broadcast GEN, not plain AM. We match.

### 3. Wiring & data flow

Both band-button entry points converge on `RadioModel::onBandButtonClicked(Band)`:

```
Path A — Spectrum overlay flyout
  User clicks BAND button on SpectrumOverlayPanel
   └─ SpectrumOverlayPanel::bandSelected(name, freqHz, mode)                 [unchanged signal]
       └─ MainWindow lambda                                                  [REPLACED body]
           └─ Band b = bandFromName(name);                                   [small helper: "80m" → Band::Band80m]
              RadioModel::onBandButtonClicked(b);
              // freqHz / mode args from the signal are ignored — seed/restore owns policy

Path B — Container BandButtonItem
  User clicks band button in MeterWidget
   └─ BandButtonItem::onButtonClicked(int, Qt::LeftButton)                   [unchanged]
       └─ emits bandClicked(int)
           └─ ContainerWidget::bandClicked(int)                               [unchanged — already forwards]
               └─ MainWindow lambda                                           [NEW connection]
                   └─ RadioModel::onBandButtonClicked(Band::bandFromUiIndex(idx))

Shared handler
  RadioModel::onBandButtonClicked(Band clicked)
   ├─ activeSlice() guard
   ├─ Band current = Band::bandFromFrequency(slice->frequency())
   ├─ if (clicked == current) return                                          [Q1(a) no-op]
   ├─ slice->saveToSettings(current)                                          [snapshot outgoing]
   └─ if (slice->hasSettingsFor(clicked))
      ├─ slice->restoreFromSettings(clicked)                                  [last-used wins]
      else
      ├─ BandSeed seed = BandDefaults::seedFor(clicked)
      ├─ if (!seed.valid) return                                              [XVTR path]
      ├─ slice->setFrequency(seed.frequencyHz)
      ├─ slice->setDspMode(seed.mode)
      └─ slice->saveToSettings(clicked)                                       [first-visit bakes seed]
```

**Name → Band helper** (new, trivial): `Band bandFromName(const QString& name)` — maps the string labels used by `SpectrumOverlayPanel::kBands` (`"160m"`, `"80m"`, …, `"WWV"`, `"GEN"`) back to the `Band` enum. Lives in `src/models/Band.{h,cpp}` next to `bandLabel()` and `bandFromUiIndex()`.

**Downstream effects (already wired, just noting):**
- `setFrequency` → `frequencyChanged` → `ReceiverManager` → `RadioConnection` (P1/P2 wire retune) + `AlexController` HPF/LPF switch + `PanadapterModel::setCenterFrequency` (fires `bandChanged`, grid reposition).
- `setDspMode` → `dspModeChanged` → `RxChannel::setMode` (WDSP reconfig) + default filter application per mode.

**Order:** frequency before mode, matching Thetis `SetBand` at [`console.cs:5867-5886`](../../../Thetis/Project Files/Source/Console/console.cs:5867). Alex band-dependent filters track the freq change first, then mode-dependent bandwidth is applied.

### 4. Persistence

**No schema change.** Phase 3G-10 Stage 2 already stores per-slice-per-band state under:

```
slices/<sliceIndex>/bands/<bandKey>/DspMode        int   (DSPMode enum)
slices/<sliceIndex>/bands/<bandKey>/Frequency      double (Hz)
slices/<sliceIndex>/bands/<bandKey>/FilterLow      int
slices/<sliceIndex>/bands/<bandKey>/FilterHigh     int
... other DSP keys (AGC, NR, NB, ANF, ...)
```

`<bandKey>` comes from `Band::bandKeyName()` — `"160m"`, `"80m"`, `"60m"`, …, `"6m"`, `"GEN"`, `"WWV"`, `"XVTR"` (see [`src/models/Band.cpp:132`](../../src/models/Band.cpp:132)).

**The new helper:**
```cpp
bool SliceModel::hasSettingsFor(Band b) const {
    const QString key = QStringLiteral("slices/%1/bands/%2/DspMode")
        .arg(m_sliceIndex).arg(bandKeyName(b));
    return AppSettings::instance().contains(key);
}
```

Probes `DspMode` specifically because it is the field most directly coupled to the bug: if a DSP mode is stored for a band, the band has been visited. The `setFrequency` / `setDspMode` setters do **not** auto-save — save only happens on explicit `saveToSettings(Band)` calls — so there is no risk of seed values bleeding into the outgoing band's namespace.

### 5. Edge cases

| Scenario | Behavior |
| --- | --- |
| `activeSlice() == nullptr` (no connection, or between-slice state) | Early return, silent. |
| Slice is frequency-locked (3G-10 S2.9) | Handler short-circuits entirely — freq AND mode frozen, no state written. Emits `bandClickIgnored(band, reason)` so MainWindow can show the user a status-bar message ("Band 20m ignored: slice is locked — unlock to change bands"). Earlier design let mode change (Thetis "lock is VFO-only"), but that corrupted the new band's persistence slot via a stale-freq write. 2026-04-23 review fix. |
| Clicked band has no seed (XVTR) **and** no persisted state | No freq/mode change. Emits `bandClickIgnored(band, "Band XVTR ignored: transverter config not yet supported")` for status-bar feedback. Also traced at `qCDebug(lcConnection)` level. |
| Disconnected radio | Handler still runs; local SliceModel updates. Connect path flushes to radio on next connect. |
| Seed lands outside its own band (paranoia — bands are disjoint) | `qCWarning` and skip. Only triggers if the seed table is corrupted. |

No exceptions thrown. All paths log-and-return using `qCWarning`/`qCInfo` with the `lcRadio` category, per project style.

### 6. Testing

**Unit tests** (two new test binaries):

1. **`tests/tst_band_defaults.cpp`** — seed table integrity:
   - Every `Band` enum value (except `XVTR`) has `valid == true`.
   - Every seeded freq lands inside its own band: `Band::bandFromFrequency(seed.frequencyHz) == seed.band`.
   - Every seed mode ∈ `{LSB, USB, CWU, CWL, SAM}`.

2. **`tests/tst_radio_model_band_click.cpp`** — handler behavior:
   - **First-visit seeds** — fresh SliceModel, simulate `onBandButtonClicked(Band::Band20m)` → freq == 14.155 MHz, mode == USB. Repeat for the 13 seeded bands.
   - **Second-visit restores** — seed 20m, tune to 14.100, switch to CWU, click 40m, click 20m → freq == 14.100, mode == CWU (not 14.155 USB).
   - **Same-band no-op** — slice on 20m at 14.100, click 20m → no freq change, no extra AppSettings writes.
   - **Save on exit** — slice on 20m at 14.100 CWU, click 40m → `slices/0/bands/20m/Frequency == 14.100` and `DspMode == CWU`.
   - **XVTR deferred** — click XVTR with no persisted state → no change; info log asserted.
   - **Null active slice** — `RadioModel` with no slice set → click any band → no crash, no setFrequency call observed.
   - **Locked slice** — `slice->setLocked(true)`, click different band → freq unchanged, mode changed.

**Manual verification** — one-row addendum to the 3G-8 verification matrix:
- **Path A (overlay flyout):** Connect to radio; open the spectrum `BAND` flyout; click each band 160m → 6m; confirm freq **and mode** both update (the 80m case is the reproducer for #118 — must land on 3.650 MHz LSB, not "freq-only while mode stays USB").
- **Path B (container band buttons):** Same sequence from the `BandButtonItem` grid in a meter container; confirm identical behavior (shared handler).
- **Restore path:** Tune each band to a different freq, switch bands, return; confirm last-used state restores instead of re-seeding.

No integration tests on the radio wire — the handler only drives existing setters whose wire paths are already covered by P1/P2 parity tests.

---

## Design decisions — record

1. **Single-entry band memory, not multi-entry stack.** Multi-entry (Thetis BandStack2) requires right-click stack UI, Shift/Ctrl modifiers, GUID-keyed DB entries. That is a phase-sized port. Single-entry closes the bug, reuses 3G-10 Stage 2 persistence, and does not preclude a later upgrade to multi-entry.

2. **Voice-first seed pick, not index-0.** Thetis's `SelectInitial()` returns index 0 which is CW for most bands. Shipping CW-by-default contradicts the #118 reporter's expectation and would surprise most first-time users. Voice-first matches the bug report and the common ham-op expectation. Frequencies still come verbatim from Thetis (constants are source-first); only the mode-selection policy is a documented NereusSDR deviation.

3. **Active-slice scope, not RX1-only.** `RadioModel::activeSlice()` / `setActiveSlice()` already exist. Using the active-slice concept costs nothing now and future-proofs for Phase 3F (multi-panadapter / RX2).

4. **Defer AetherSDR bandplan JSON integration.** The 4 JSONs in `resources/bandplans/` could theoretically seed the mode-per-band table, but the label vocabulary (`PHONE`, `SSB`, `USB`, `CW`, `DATA`, `AUTO`, `BCN`) does not cleanly map to `DSPMode` (`LSB`/`USB`/`CWL`/`CWU`/…). A 14-entry `constexpr` table cited back to Thetis is simpler and more source-faithful than inventing a label-inference heuristic. Epic D (the AetherSDR bandplan overlay port) lands separately and does not need to touch this seed table.

5. **Order: freq before mode.** Matches Thetis `SetBand` at [`console.cs:5867-5886`](../../../Thetis/Project Files/Source/Console/console.cs:5867). Alex HPF/LPF follows the new band before mode-dependent filter bandwidth is applied.

---

## Out-of-scope follow-ups (file as separate issues)

- **Full Thetis BandStack2 port** (multi-entry cycling, Shift/Ctrl modifiers, right-click stack UI, per-band DB entries). Tracks issue to be opened.
- **Epic D bandplan overlay** — port `BandplanModel` + `SpectrumWidget::drawBandplanOverlay` from AetherSDR. Already scoped in `phase3g-rx-experience-epic-design.md` §D.
- **XVTR seed** — once the XVTR epic adds user-configured transverters, wire a seed entry (likely "first XVTR IF centre").
- **GEN sub-band stack** — port Thetis's LMF/120m/90m/49m/… sub-band bundle as its own mini-stack.
- **Region selector** — Thetis has Region 1 / Region 2 / Region 3 / UK / etc. seeds. NereusSDR ships Region 2 only for this fix; region selection is an Epic D concern (same user-facing preference as the bandplan overlay region).
