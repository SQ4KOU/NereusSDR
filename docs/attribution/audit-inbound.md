# Inbound audit — discovered + judged derivations

Intermediate work product feeding `THETIS-PROVENANCE.md`. Removed after
PROVENANCE.md is finalized in Task 6.

## Scope notes

- **Method.** Started from the 28 unique Thetis sources cited in
  `audit-outbound.md` (Step 1). For each of the most-cited sources
  (MeterManager.cs, console.cs, setup.cs, radio.cs, display.cs,
  NetworkIO.cs, clsHardwareSpecific.cs, HPSDR/specHPSDR.cs, enums.cs,
  dsp.cs, xvtr.cs, DiversityForm.cs, PSForm.cs, frmAddCustomRadio.cs,
  ucRadioList.cs, ucMeter.cs, frmMeterDisplay.cs, frmVariablePicker,
  clsScaleItem), identified distinctive symbols/constants and swept
  `src/` + `tests/` for files that use them **without** being captured
  by the outbound audit's grep pattern. The outbound grep keyed off the
  exact strings `"From Thetis"`, `"Porting from"`, `"Thetis Project
  Files"`, `mi0bot`, `MW0LGE`, etc. — anything that cites Thetis with
  a slightly different phrasing (e.g. `"Source: Thetis Setup.cs:…"`,
  `"Port of Thetis frmAddCustomRadio.cs"`, `"Thetis ucRadioList port"`,
  `"from Thetis UpdateInterval=100ms"`) was missed.
- **Set-diff result.** 31 NereusSDR files mention `Thetis` or `MW0LGE`
  or `W5WC` but are absent from `audit-outbound.md`. Every one of them
  was inspected. After filtering, 21 are real derivations, 7 are
  independently implemented (including the 2 AboutDialog carry-overs
  from Task 4), 3 are AetherSDR-sourced files that mention Thetis only
  as a categorical label.
- **mi0bot carve-out.** 8 mi0bot-variant entries in the outbound audit
  cite `mi0bot/Thetis-HL2` paths that aren't locally clonable (hex test
  fixtures + HL2 I/O board files). Those stay as mi0bot-variant
  derivations — **not re-audited here** because the fork source isn't
  available locally.

## Summary
- Newly discovered derivations (add to PROVENANCE main table): **21**
- Independently implemented (add to PROVENANCE "not derived" section): **9**
  (7 candidates from this sweep + 2 carry-overs from Task 4)

---

## Newly discovered derivations

These NereusSDR files reproduce Thetis logic/constants/structure but
did not match the Task 4 grep pattern. Each gets a header block in
Tasks 8–11.

### src/gui/setup/hardware/AntennaAlexTab.cpp
- **Thetis source (inferred):** `Project Files/Source/Console/setup.cs` — `InitAlexAntTables()` (lines 13393-13478), `_AlexRxAntButtons` / `_AlexTxAntButtons` tables (13412-13436), `radAlexR/T` column enable (6185-6246), `chkRxOutOnTx` / `chkEXT1OutOnTx` (2892-2898), `chkEnableXVTRHF` (18639).
- **Evidence:** file header reads `// Source: Thetis Setup.cs InitAlexAntTables() lines 13393-13478`; multiple `// Source: Thetis Setup.cs:XXXX` callsite citations preserve the exact line numbers.
- **Variant:** thetis-samphire (setup.cs carries the continuing Samphire copyright; see outbound audit top-of-file table).

### src/gui/setup/hardware/AntennaAlexTab.h
- **Thetis source (inferred):** same as `.cpp` above — `InitAlexAntTables()` (13393) + per-band row structure.
- **Evidence:** header cites `Setup.cs InitAlexAntTables() (line 13393)` and `Setup.cs:2892-2898`.
- **Variant:** thetis-samphire.

### src/gui/setup/hardware/DiversityTab.cpp
- **Thetis source (inferred):** `Project Files/Source/Console/DiversityForm.cs`.
- **Evidence:** header reads `// Source: Thetis DiversityForm.cs`.
- **Variant:** thetis-samphire (DiversityForm.cs is a Samphire-modified Thetis form).

### src/gui/setup/hardware/DiversityTab.h
- **Thetis source (inferred):** `Project Files/Source/Console/DiversityForm.cs` — `chkLockAngle` (line 1216), `chkLockR` (line 1228).
- **Evidence:** header cites those exact control names and line numbers.
- **Variant:** thetis-samphire.

### src/gui/setup/hardware/PaCalibrationTab.cpp
- **Thetis source (inferred):** `Project Files/Source/Console/setup.cs` — PA calibration per-band gain arrays.
- **Evidence:** header reads `// Source: Thetis Setup.cs`.
- **Variant:** thetis-samphire.

### src/gui/setup/hardware/PaCalibrationTab.h
- **Thetis source (inferred):** `Project Files/Source/Console/setup.cs` — PA calibration page.
- **Evidence:** header cites `Setup.cs`.
- **Variant:** thetis-samphire.

### src/gui/setup/hardware/PureSignalTab.cpp
- **Thetis source (inferred):** `Project Files/Source/Console/PSForm.cs`.
- **Evidence:** header reads `// Source: Thetis PSForm.cs`.
- **Variant:** thetis-samphire (PSForm is Samphire-maintained).

### src/gui/setup/hardware/PureSignalTab.h
- **Thetis source (inferred):** `Project Files/Source/Console/PSForm.cs` — `chkPSAutoAttenuate` (line 841).
- **Evidence:** header cites `PSForm.cs — chkPSAutoAttenuate (line 841)`.
- **Variant:** thetis-samphire.

### src/gui/setup/hardware/RadioInfoTab.cpp
- **Thetis source (inferred):** `Project Files/Source/Console/setup.cs` — Hardware Config / General board label, `include_extra_p1_rate` (line 847), `numericUpDownNr` board_max_rx range (848-850+).
- **Evidence:** multiple `// Source: Thetis Setup.cs:XXX` citations preserving line numbers.
- **Variant:** thetis-samphire.

### src/gui/setup/hardware/RadioInfoTab.h
- **Thetis source (inferred):** `Project Files/Source/Console/setup.cs` — Hardware Config / General section (board model display).
- **Evidence:** header cites `Setup.cs Hardware Config / General section`.
- **Variant:** thetis-samphire.

### src/gui/setup/hardware/XvtrTab.h
- **Thetis source (inferred):** `Project Files/Source/Console/xvtr.cs` — `XVTRForm` class (lines 47-249), `chkEnable0..15` 16-row form.
- **Evidence:** header cites `Thetis xvtr.cs XVTRForm class (lines 47-249)` and notes the 16→5 row reduction.
- **Variant:** multi-source (xvtr.cs = FlexRadio + Wigley + Samphire dual-license; see outbound audit top-of-file table).

### src/gui/setup/hardware/OcOutputsTab.h
- **Thetis source (inferred):** `Project Files/Source/Console/setup.cs` — `UpdateOCBits()` (lines 12877-12934).
- **Evidence:** header cites `Setup.cs UpdateOCBits() lines 12877-12934`.
- **Variant:** thetis-samphire.

### src/gui/setup/HardwarePage.cpp
- **Thetis source (inferred):** `Project Files/Source/Console/setup.cs` — Hardware Config tab strip structure (9 sub-tabs, tab order).
- **Evidence:** header reads `9 sub-tabs mirroring Thetis Setup.cs Hardware Config sub-tabs` and `order mirrors Thetis Setup.cs Hardware Config tab strip`.
- **Variant:** thetis-samphire.

### src/gui/setup/HardwarePage.h
- **Thetis source (inferred):** `Project Files/Source/Console/setup.cs` — Hardware Config nested tab widget.
- **Evidence:** header cites Thetis Setup.cs Hardware Config nested QTabWidget layout.
- **Variant:** thetis-samphire.

### src/gui/AddCustomRadioDialog.h
- **Thetis source (inferred):** `Project Files/Source/Console/frmAddCustomRadio.cs` + `frmAddCustomRadio.Designer.cs`.
- **Evidence:** header reads `Port of Thetis frmAddCustomRadio.cs / frmAddCustomRadio.Designer.cs` and enumerates Designer.cs fields.
- **Variant:** thetis-samphire (frmAddCustomRadio is sole-Samphire — see outbound audit top-of-file table).
- **Notes:** `.cpp` counterpart `AddCustomRadioDialog.cpp` IS already cited in the outbound audit; the `.h` alone was missed by the grep.

### src/gui/ConnectionPanel.cpp
- **Thetis source (inferred):** `Project Files/Source/Console/ucRadioList.cs` (mi0bot/ramdor fork; bundled with Thetis), `Project Files/Source/Console/clsDiscoveredRadioPicker.cs`.
- **Evidence:** header reads `Thetis ucRadioList port — radio list UI for NereusSDR` and `Source: ucRadioList.cs (ramdor/Thetis), clsDiscoveredRadioPicker.cs`; multiple in-body `// Thetis layout` / `// Thetis behavior` citations.
- **Variant:** thetis-samphire (ucRadioList + clsDiscoveredRadioPicker are both Samphire-maintained; clsDiscoveredRadioPicker is sole-Samphire).

### src/gui/ConnectionPanel.h
- **Thetis source (inferred):** same as `.cpp` — ucRadioList.cs.
- **Evidence:** header reads `ConnectionPanel — Thetis ucRadioList-equivalent radio list UI` and a dedicated `Porting note:` block citing `Thetis source patterns`.
- **Variant:** thetis-samphire.

### src/gui/containers/ContainerSettingsDialog.h
- **Thetis source (inferred):** `Project Files/Source/Console/frmMeterDisplay.cs` (container settings UI — 3-column Available/In-use/Properties layout, "Add to Container" stacking).
- **Evidence:** in-file comments cite `Thetis 3-column layout: Available | In-use | Properties`, `Phase E — Thetis-parity "Add to Container" stacking flow`, `Thetis behavior`.
- **Variant:** thetis-samphire (frmMeterDisplay.cs is sole-Samphire).
- **Notes:** `.cpp` counterpart `ContainerSettingsDialog.cpp` IS already cited in the outbound audit; the `.h` alone was missed.

### src/gui/containers/meter_property_editors/NeedleItemEditor.h
- **Thetis source (inferred):** `Project Files/Source/Console/MeterManager.cs` — `clsNeedleItem` per-needle setters (colors, font, marks, power, direction).
- **Evidence:** header reads `Exposes full Thetis parity for every setter that exists on NeedleItem`.
- **Variant:** thetis-samphire.

### src/gui/containers/meter_property_editors/NeedleScalePwrItemEditor.h
- **Thetis source (inferred):** `Project Files/Source/Console/MeterManager.cs` — `clsNeedleScalePwrItem` (calibrated power-scale needle item).
- **Evidence:** header reads `Exposes full Thetis parity: colors, font, marks, power, direction`.
- **Variant:** thetis-samphire.

### src/gui/containers/meter_property_editors/ScaleItemEditor.h
- **Thetis source (inferred):** `Project Files/Source/Console/MeterManager.cs` — `clsScaleItem.ShowType` (centered title mode).
- **Evidence:** in-body citation `// Phase B4 — ShowType centered title (Thetis clsScaleItem.ShowType)`.
- **Variant:** thetis-samphire.

### src/gui/containers/MmioVariablePickerPopup.h
- **Thetis source (inferred):** `Project Files/Source/Console/frmVariablePicker` (MMIO/external-variable chooser dialog; sentinel `"--DEFAULT--"`).
- **Evidence:** header reads `Matches Thetis frmVariablePicker` and cites the `"--DEFAULT--"` sentinel and the Cancel-preserves-caller behavior.
- **Variant:** thetis-samphire (MMIO subsystem in Thetis is Samphire-authored; outbound audit already ties MMIO-adjacent files to Samphire).

### src/gui/meters/MeterPoller.cpp
- **Thetis source (inferred):** `Project Files/Source/Console/MeterManager.cs` — 100 ms `UpdateInterval` poll cadence.
- **Evidence:** in-body constant `m_timer.setInterval(100); // 10 fps default (from Thetis UpdateInterval=100ms)`.
- **Variant:** thetis-samphire (MeterManager is sole-Samphire).
- **Notes:** `.h` counterpart `MeterPoller.h` IS cited in outbound audit; `.cpp` alone was missed.

### src/gui/widgets/VfoModeContainers.h
- **Thetis source (inferred):** `Project Files/Source/Console/console.cs` + `Project Files/Source/Console/dsp.cs` — per-sideband DIGU/DIGL offset model.
- **Evidence:** header reads `DSP behavior bound to S1.6 SliceModel stub API — Thetis-authoritative` and cites the `DIGU → diguOffsetHz (Thetis uses separate per-sideband offsets)` rule.
- **Variant:** multi-source (console.cs + dsp.cs; console.cs is Samphire-heavy, dsp.cs is Pratt + Samphire dual-license).
- **Notes:** `.cpp` counterpart `VfoModeContainers.cpp` IS cited in outbound audit.

### src/models/RxDspWorker.h
- **Thetis source (inferred):** `Project Files/Source/Console/console.cs` — buffer-size formula `in_size = 64 * rate / 48000` (used to plan WDSP buffer sizing per sample rate).
- **Evidence:** in-body comment `// From RadioModel — Thetis formula: in_size = 64 * rate / 48000`.
- **Variant:** thetis-samphire (console.cs buffer-planning helpers are Samphire-touched).
- **Notes:** `.cpp` counterpart `RxDspWorker.cpp` is not in the outbound audit and contains no Thetis citation — treating that as incidental (the `.h` carries the formula comment; the `.cpp` is straightforward Qt worker plumbing). Task 8/11 applies to `.h`; `.cpp` gets the same header by convention for the matched pair.

---

## Independently implemented — Thetis-like but not derived

These NereusSDR files behave similarly to Thetis features but were
implemented without consulting Thetis source code. Listed here so they
appear in PROVENANCE's "not derived" section with justification.

### src/models/Band.cpp
- **Behavioral resemblance:** Band edges mirror Thetis `BandStackManager` HF definitions.
- **Basis of implementation:** ARRL band plan / IARU Region 2 — the normative amateur frequency spec. Header explicitly reads `Source: ARRL band plan / IARU Region 2, cross-checked against Thetis BandStackManager HF definitions`. Cross-check ≠ derive: the frequency edges are spec-normative, not Thetis-originated.

### src/gui/SpectrumOverlayPanel.cpp
- **Behavioral resemblance:** Left overlay button strip over the panadapter — Thetis has a similar overlay but this was ported from AetherSDR, not Thetis.
- **Basis of implementation:** Header reads `Ported from AetherSDR src/gui/SpectrumOverlayMenu.cpp` with `Adapted for NereusSDR OpenHPSDR/Thetis feature set` — the single "Thetis" mention is categorical (feature-set label), not a source citation. The actual widget hierarchy, constants, and interaction model come from AetherSDR.

### src/gui/SpectrumOverlayPanel.h
- **Behavioral resemblance:** Same as `.cpp`.
- **Basis of implementation:** Same — AetherSDR-derived with a categorical "Thetis feature set" mention in the file banner.

### src/gui/setup/AppearanceSetupPages.cpp
- **Behavioral resemblance:** Mentions "Thetis-format skin" in a single placeholder tooltip.
- **Basis of implementation:** Tooltip text describing a future Phase 3H feature (`Import Thetis-format skin — not yet implemented`); no derived code. The file is pure Qt widget construction + stylesheet.

### tests/tst_noise_floor_estimator.cpp
- **Behavioral resemblance:** Tests NoiseFloorEstimator math; one comment contrasts Thetis's `processNoiseFloor` approach.
- **Basis of implementation:** Pure test of NereusSDR math — percentile-based noise floor estimator. The Thetis mention is a *contrast* (our estimate is robust to single carriers unlike Thetis's moving-average). Not derived; the test exercises `NoiseFloorEstimator::estimate()` with synthesized bin vectors.

### tests/tst_p1_loopback_connection.cpp
- **Behavioral resemblance:** Notes Thetis connection-refused behavior in a single comment.
- **Basis of implementation:** Loopback tests against `P1FakeRadio`; the Thetis mention explains why the refusal-path test was deleted (Thetis enforces the same invariant elsewhere, so NereusSDR does too). Not derived — the test plumbs NereusSDR's own P1 connection state machine.

### src/core/RadioConnection.cpp  *(not in set-diff, noted for completeness)*
- **Behavioral resemblance:** Factory that picks P1 vs P2 based on `ProtocolVersion`.
- **Basis of implementation:** Thin factory — no Thetis logic. Not on the candidate list but checked as a sanity probe during the sweep.

### src/gui/AboutDialog.cpp  *(carried over from outbound audit, per Task 5 spec)*
- **Behavioral resemblance:** Renders "Richie / MW0LGE / Thetis" and "Reid Campbell / MI0BOT / OpenHPSDR-Thetis (HL2)" contributor strings in the About dialog.
- **Basis of implementation:** Attribution display — not derived code. The dialog names contributors and links to their upstream repos, which is an *act* of attribution, not a port of Thetis logic. Task 4 tagged it `thetis-samphire (by convention — attribution)`; Task 5 moves it to the "not derived" section so PROVENANCE reflects the semantic.

### tests/tst_about_dialog.cpp  *(carried over from outbound audit, per Task 5 spec)*
- **Behavioral resemblance:** Verifies that AboutDialog output contains `ramdor/Thetis` and `mi0bot/OpenHPSDR-Thetis` strings.
- **Basis of implementation:** Test of attribution rendering, not of Thetis-derived code. The test reads AboutDialog output and string-matches the expected contributor mentions. Task 4 tagged it `thetis-samphire (by convention)`; Task 5 moves it to "not derived" for the same reason as AboutDialog.cpp.

---

## Richard Samphire's flagged MeterManager.cs:1049 example

The plan calls out `MeterManager.cs:1049` specifically. Verification
chain:

1. The symbol `shouldRender` lives in `src/gui/meters/MeterWidget.cpp`
   lines 468, 475, 749, 766, 805, 820 (method definition at line 475).
2. `MeterWidget.cpp` and `MeterWidget.h` ARE cited in the outbound
   audit with `Project Files/Source/Console/MeterManager.cs`.
3. `src/gui/meters/MeterItem.h:60` contains an explicit cross-reference
   comment: `// MeterManager.cs:31366-31368 — see MeterWidget::shouldRender()`.

So Richie's specific concern IS already covered by outbound audit. No
inbound re-classification needed for that chain. This section exists so
the courtesy bundle for Richie (Task 22) can point directly at the
citations and confirm the flag was acted on.
