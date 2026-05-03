# Handoff — Plans 4 + 5 (TX Bandwidth + Profile-Awareness)

**Created:** 2026-05-02
**Worktree:** `/Users/j.j.boyd/NereusSDR-ui-polish-foundation`
**Branch:** `feature/ui-polish-foundation`
**HEAD:** `875a2c9` (RxApplet AGC-T Option B layout)
**Status:** Plans 1–3 complete + 5 bench-fix commits. Tests: 287/287 passing. 70 commits since `main`.

This handoff picks up the same campaign that [HANDOFF.md](HANDOFF.md) started. Plans 1, 2, and 3 are done. Plans 4 and 5 remain, plus the bench-verification queue.

---

## How to pick up

```bash
cd /Users/j.j.boyd/NereusSDR-ui-polish-foundation
claude
```

**First message in the new session:**

> "Read HANDOFF-PLAN4.md and continue with Plan 4 (TX Bandwidth) using subagent-driven execution."

The new session should:
1. Read this file.
2. Read [docs/superpowers/plans/2026-05-01-tx-bandwidth.md](docs/superpowers/plans/2026-05-01-tx-bandwidth.md) — Plan 4 (single substantive task: `FilterLow`/`FilterHigh` per-profile persistence + spectrum band/waterfall column rendering during MOX).
3. Invoke the `superpowers:subagent-driven-development` skill.
4. Dispatch fresh subagents per task; review between tasks.
5. After Plan 4 lands, proceed to Plan 5 (profile-awareness — depends on Plan 4's `FilterLow`/`FilterHigh` schema as test fixtures).

---

## What's done (Plans 1–3 + bench fixes)

### Plan 1 — Foundation (commits `5dfced7..2e37dd5`, 19 commits inc. 4 fix-ups)

- **A1**: AppletPanelWidget +8px scrollbar gutter + new `tst_applet_panel_gutter`
- **A2 prep**: `Style::kLabelMid`, `dspToggleStyle()`, `doubleSpinBoxStyle()`, `applyDarkPageStyle()` helpers in `StyleConstants.h`
- **A2 file consolidations**: EqApplet (`sliderVStyle()` wired), PhoneCwApplet (`phoneButtonStyle()` exception), FmApplet (`fmButtonStyle()` exception), VaxApplet (`vaxSectionStyle()`/`vaxButtonStyle()` exceptions), VfoWidget (4 `vfo*Style()` exceptions), SpectrumOverlayPanel (`OverlayColors::*` namespace + 2 file-local helpers + dropped dead `kDspBtnStyle`), TitleBar, RxApplet, TxApplet
- **A3**: `SetupPage::addLabeledX` helpers use canonical `Style::k*Style`; 4 `applyDarkStyle()` copies replaced with `Style::applyDarkPageStyle()` (deliberate +8px QGroupBox padding-top correction)
- **D**: AddCustomRadioDialog, NetworkDiagnosticsDialog, SupportDialog, AboutDialog migrated; TxEqDialog + TxCfcDialog use `Style::doubleSpinBoxStyle()`

### Plan 2 — Cross-Surface (commits `f0a8a4f..b621db2..fcf165d`, 21 commits inc. 4 fix-ups)

- **B1 AGC**: RxApplet AGC combo +Long (5 modes); VfoWidget AGC-T de-inverted to match RxApplet+Thetis convention
- **B2 Mode+Filter**: VfoWidget +DSB+DRM (11 modes); `SliceModel::presetsForMode()` and `commonPresetsForMode()` accessors; RxApplet/VfoWidget filter grids now mode-aware
- **B3 Antenna popup**: New `AntennaPopupBuilder` utility class (`src/gui/AntennaPopupBuilder.{h,cpp}`) + `tst_antenna_popup_builder`. RxApplet/VfoWidget/SpectrumOverlay all migrated. Plan divergence: `BoardCapabilities` lacks `hasExt1/2/Xvtr`; builder takes `BoardCapabilities` + `SkuUiProfile` instead, using `rxOnlyAntennaCount` + `SkuUiProfile::rxOnlyLabels`.
- **B4 Audio**: RxApplet AF row dropped; Pan/Mute/SQL/SQL-threshold wired (`setSsqlEnabled`/`setSsqlThresh`)
- **B5 DSP toggles**: verification-only — no commit
- **B6 XIT**: RadioModel applies `xitHz` at both `setTxFrequency` sites + `tst_xit_offset_application`. RxApplet/VfoWidget XIT placeholders wired. `wireSliceSignalsForTest()` test seam added to RadioModel.h.
- **B7 Lock**: VfoWidget X/RIT-tab Lock dropped; Close-strip + RxApplet header Locks preserved
- **B8 Spectrum display**: 3 orphaned signals wired + `tst_b8_overlay_signals`. Cursor Freq toggleable+persisted. Fill Color emit. Heat Map/Noise Floor/Weighted Avg dropped. "More Display Options" link wired. NYI strip verified visible-but-disabled.

### Plan 2 bench fixes (3 commits — `0d3b6a8`, `5316125`, `0e69d7f`)

- AGC-T missing slice→UI sync in RxApplet (was write-only)
- XVTR/EXT antenna selection no-op (RadioModel slice→Alex bridge only handled `ANT1-3`; extended to handle non-ANT via `SkuUiProfile::rxOnlyLabels` lookup → `setRxOnlyAnt`)
- Fill Alpha slider was decoration-only (added `fillAlphaChanged(float)` signal + connect)

### Plan 3 — Right-Panel (commits `c063042..7edd54f`, 7 commits inc. cleanup)

- **C1**: SWR gauge wired (and `m_swrGauge` type bug fixed: was `QWidget*`, now `HGauge*`); SWR Prot LED wired to `SwrProtectionController::highSwrChanged` (NOT removed — data path existed); ATU/MEM/Tune Mode/DUP/xPA NYI rows removed; TUNE+MOX promoted above VOX+MON
- **C2**: 8 ghost applets gated in MainWindow (FmApplet was already not present); PhoneCwApplet CW tab body stubbed with "Coming in 3M-2" placeholder

### Plan 3 bench fixes (3 commits — `1af9a07`, `39dbdd2`, `875a2c9`)

- HGauge tick labels clipping (height bumped 24 → 30)
- RxApplet Mute button removed (3rd surface for the same control)
- AGC-T relocated then restructured (Option B): combined AGC mode combo + AGC-T header in one row, slider on full-width row below

---

## Bench-verification queue (needs ANAN-G2 hardware)

| Plan | Item | What to verify |
|---|---|---|
| 2 / Task 4 | B1 AGC-T direction | VfoWidget de-inverted to match RxApplet + Thetis convention. Confirm against actual Thetis screenshot or hardware behavior. 1-line revert if wrong. |
| 2 / Task 18 | B6 XIT NCO shift | Tune 14.200 → MOX → expect TX trace at 14.200. XIT +500 → TX shifts to 14.2005. RIT -500 + XIT +500 → split (RX 14.1995, TX 14.2005). Persistence across reconnect. |
| 3 / Task 2 | SWR gauge | MOX with antenna → confirm live SWR (1.0+ on matched antenna) |
| 3 / Task 3 | SWR Prot LED | Trigger high SWR → LED lights amber |

---

## Plans remaining

- **Plan 4 — TX Bandwidth** (single substantive task): `FilterLow`/`FilterHigh` per-profile persistence + spectrum band/waterfall column rendering during MOX. Plan: [docs/superpowers/plans/2026-05-01-tx-bandwidth.md](docs/superpowers/plans/2026-05-01-tx-bandwidth.md). Bench-required for final E1 verification (sweep TX BW spinbox during MOX; verify panadapter band moves live).

- **Plan 5 — Profile-Awareness** (the largest plan, ~1500 lines): banner-equipped surfaces + profile-switch-while-modified UX. Plan: [docs/superpowers/plans/2026-05-01-profile-awareness.md](docs/superpowers/plans/2026-05-01-profile-awareness.md). **Depends on Plan 4's `FilterLow`/`FilterHigh` schema as test fixtures** — must wait until Plan 4 lands.

If Plan 5 feels too dense at review, it has a documented split point (after E2 infrastructure) — could become a sequel PR.

---

## Session learnings worth carrying forward

1. **Plan mappings are not always accurate** — the design doc author's understanding of code structure had gaps. For each constant/value/API the plan claims, **verify byte-for-byte before swapping**. Multiple Plan 1 + Plan 2 fix-ups were caught by code review (e.g. `kButtonBase = #1a3a5a` not `#1a2a3a`; `BoardCapabilities` lacks `hasExt1/2`; `SkuUiProfile::rxOnlyLabels` is the actual structure).
2. **§A2 "legitimate exception" pattern works well** — `phoneButtonStyle()`, `fmButtonStyle()`, `vaxButtonStyle()`, `vfo*Style()`, `overlay*Style()` all use the same template: file-local helper using canonical palette constants for matching values + raw hex (with §A2 comment) for genuinely-divergent values.
3. **Subagent-driven dispatch with batching by cluster** worked well for Plan 2/3. Dispatch one subagent per cluster (B1, B2, B3, etc.), produce N commits per dispatch.
4. **Bench checkpoints surfaced real bugs** that even spec+quality reviews missed (model→UI sync gaps, signal routing for non-default cases like XVTR/EXT antennas, fixed-height widgets clipping content). Worth keeping bench checkpoints between major plans.
5. **clangd false positives** are common with AUTOMOC (`*.moc` "file not found" warnings) and stale clangd-index (after class renames). Verify actual build before chasing clangd diagnostics.
6. **Discipline that paid off**: GPG-signing every commit (project policy), pre-commit hooks (Thetis attribution, port classification, inline-cite preservation) catching attribution gaps in real-time, single-file-per-commit scoping making review fast.

---

## Quick smoke check before starting Plan 4

```bash
cd /Users/j.j.boyd/NereusSDR-ui-polish-foundation
git status                              # clean tree expected
git log --oneline | head -10            # top should be 875a2c9 (AGC-T Option B)
cmake --build build -j$(sysctl -n hw.ncpu) 2>&1 | tail -5   # clean
ctest --test-dir build 2>&1 | tail -5   # 287/287 passing
```

If anything fails, surface to the user — do not start Plan 4 from a broken state.

---

## Tips for Plan 4 dispatch

- **Plan 4 is mostly model + protocol work**, not visual. The design doc has the full spec. Read [docs/superpowers/plans/2026-05-01-tx-bandwidth.md](docs/superpowers/plans/2026-05-01-tx-bandwidth.md) end-to-end before dispatching.
- **Source-first protocol applies** if Plan 4 touches Thetis logic (`FilterLow`/`FilterHigh` semantics likely come from Thetis `MicProfile` or similar). Pre-commit hooks will check attribution; trust them.
- **Plan 4 Task 13 is bench-required** — sweep TX BW spinbox during MOX, verify panadapter band moves live + waterfall column appears. Plus profile activation persistence (customize → save → switch → switch back → restored).
- **Plan 4 may add new tests** — schema fixtures for Plan 5. Make sure they're solid; Plan 5 will lean on them.

---

## When the campaign completes (Plan 5 lands)

After all 5 plans complete:

1. Open one PR from `feature/ui-polish-foundation` to `main`.
2. PR title suggestion: `feat(ui): UI audit + polish + TX bandwidth + profile-awareness`
3. PR description should include:
   - Links to the design doc + 5 plan docs
   - Bench verification table (paste from this handoff's "Bench-verification queue")
   - Test count delta (baseline 283 → 287 + Plan 4 + Plan 5 additions)
   - The natural review split point: between commit ending Plan 3 (`875a2c9`) and the start of Plan 5 (E2 infrastructure)

If review feedback says split at the natural point, Plans 1–4 ship as one PR and Plan 5 becomes a sequel.

---

End of Plan 4 / Plan 5 handoff. The next session: open this worktree, read this file, start Plan 4.
