# Phase 3G-10 Handoff — 2026-04-15

## TL;DR

Pick up implementation of Phase 3G-10 (RX DSP Parity + AetherSDR Flag Port) starting at **Task S1.2 — VfoLevelBar**. Everything to start is checked in and ready.

- **Branch:** `feature/phase3g10-rx-dsp-flag`
- **Worktree:** `/Users/j.j.boyd/NereusSDR/.worktrees/phase3g10-rx-dsp-flag`
- **Plan:** `docs/architecture/phase3g10-rx-dsp-flag-plan.md`
- **Design:** `docs/architecture/2026-04-15-phase3g10-rx-dsp-flag-design.md`
- **Skill to use:** `superpowers:subagent-driven-development`
- **Next task:** S1.2 (see checklist below)

## What's already landed on the branch

```
9537a93 phase3g10(docs): fold S1.1 review findings + Thetis pre-gate resolution
ced4170 phase3g10(style): add VfoStyles.h with verbatim AetherSDR style constants
b479af4 phase3g10(plan): add two-stage implementation plan
434e595 fix(setup): Task 5 tooltip review findings       (pre-existing, shared base)
8c0c43d phase3g10(design): add RX DSP parity + AetherSDR flag port design
```

All commits are GPG-signed (JJ Boyd / KG4VCF). All 3G-10 commits carry the `Co-Authored-By: Claude Opus 4.6 (1M context)` trailer.

## Environment state (verified 2026-04-15)

- ✅ Worktree clean, on branch `feature/phase3g10-rx-dsp-flag`
- ✅ Baseline build succeeds: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build -j` → 235 targets
- ⚠️ `ctest --test-dir build` reports "No tests were found" — the project does not currently register qtest executables with ctest. When Stage 1 starts adding qtests (S1.2 is the first), the commits must also add `enable_testing()` and `add_test()` calls to `CMakeLists.txt`. This is not a blocker, just a setup step the first test-adding commit must handle.
- ✅ AetherSDR clone clean at `~/AetherSDR` on `origin/main`. Primary visual source for Stage 1 is `~/AetherSDR/src/gui/VfoWidget.cpp` (2938 lines).
- ✅ Thetis full clone at `/Users/j.j.boyd/Thetis` — critical finding from design §5 **resolved**. Console/ directory present with `console.cs`, `setup.cs`, `dsp.cs`, `cmaster.cs`. The C# P/Invoke declarations are in `dsp.cs` (not `wdsp.cs` — CLAUDE.md and early design drafts had this wrong; the design doc is now corrected).
- 🗑️ Old partial Thetis preserved at `/Users/j.j.boyd/Thetis.partial.1776225770` as safety net. Delete if space is tight.

## What S1.1 accomplished

Created `src/gui/widgets/VfoStyles.h` — 9 `inline constexpr QStringView` style strings + 9 `inline const QColor` constants, ported verbatim from AetherSDR with source-line comments. Zero runtime impact; header-only. `CMakeLists.txt` +1 line under `NereusSDRObjs`. Approved by both the spec compliance reviewer and the code-quality reviewer on 2026-04-15.

## Next task — S1.2 VfoLevelBar widget + unit test

Full spec lives at plan §S1.2. Summary:

**Files to create:**
- `src/gui/widgets/VfoLevelBar.h` — `QWidget` subclass with `setValue(float dbm)`, `fillFraction()`, `isAboveS9()`, `sizeHint()`
- `src/gui/widgets/VfoLevelBar.cpp` — `paintEvent` ports AetherSDR `LevelBar::paintEvent` (`~/AetherSDR/src/gui/VfoWidget.cpp:43-61`) and adds an S-unit tick strip **above** the bar (S1/3/5/7/9/+20/+40). Cyan `#00b4d8` below S9 (-73 dBm), green `#00d860` at/above. dBm range: -130 to -20.
- `tests/tst_vfo_level_bar.cpp` — 6 qtest cases: floor clamp, ceiling clamp, S9 fill fraction, below-floor clamp, above-ceiling clamp, color switch at S9.

**This is the first Stage 1 commit that adds a test** — the implementer subagent must also wire `enable_testing()` / `add_test()` in `CMakeLists.txt` if not already present. Record whatever pattern the subagent lands so subsequent test-adding commits (S1.3, S1.5, S1.11) follow the same shape.

**Plan §S1.2 has the full code blocks** including the complete VfoLevelBar.cpp paintEvent and the complete test file. The implementer subagent should reuse them verbatim — they were designed to drop in.

**Exit criteria:** `ctest --test-dir build -R tst_vfo_level_bar --output-on-failure` shows all 6 cases PASS, commit is GPG-signed with the commit message template from plan §S1.2.7.

## Remaining Stage 1 tasks (after S1.2)

| Task | Plan § | Summary |
|---|---|---|
| S1.3 | §S1.3 | ScrollableLabel + test (wheel-step + inline-edit; for RIT/XIT/DIG offset labels) |
| S1.4 | §S1.4 | ResetSlider / CenterMarkSlider / TriBtn — header-only utility ports |
| S1.6 | §S1.6 | SliceModel stub setters — **reorder to land before S1.5**; see plan note |
| S1.5 | §S1.5 | VfoModeContainers (FM/DIG/RTTY/CW) + test |
| S1.7 | §S1.7 | VfoWidget header/freq/meter row rewrite against AetherSDR |
| S1.8 | §S1.8 | VfoWidget tab bar + four tab panes (NYI-badged new controls) |
| S1.9 | §S1.9 | Mode-container visibility rules on `dspModeChanged` |
| S1.10 | §S1.10 | Floating lock/close/rec/play sibling buttons |
| S1.11 | §S1.11 | Tooltip coverage test + tooltip population |

Stage 1 exit criteria are at plan §S1-exit.1..4. A draft PR is opened at exit but **not merged** — Stage 2 lands on the same branch.

## Stage 2 (for future reference)

10 feature slices + persistence + tooltip sweep, all following the template at plan §"Stage 2 feature-slice template" (T.1 through T.13). Slices are enumerated at plan §S2.1 through §S2.10. The pre-Stage-2 gate (§S2.0) verifies the Thetis clone is present — already satisfied.

## Known non-critical drifts to fix opportunistically

1. **`CLAUDE.md` still references `Project Files/Source/Console/wdsp.cs`** — it's actually `dsp.cs`. The design doc is fixed; CLAUDE.md is stale. Fix in any CLAUDE.md touchup commit, not worth its own commit.
2. **`CLAUDE.md` "Current Phase" table still lists `3I-TX: Next`** — `3I-TX` was renumbered to `3M-1` per MASTER-PLAN. Same category: fix when convenient.
3. **`README.md` roadmap table** — the original session's edits to add 3G-9/3G-10 rows were reverted mid-session. The MASTER-PLAN has the 3G-10 entry but the README does not. Reconcile when convenient.

None of the three block Stage 1 progress.

## How to resume (copy-paste prompt for a fresh session)

> I'm continuing Phase 3G-10 of the NereusSDR project. The handoff doc is at `/Users/j.j.boyd/NereusSDR/.worktrees/phase3g10-rx-dsp-flag/docs/architecture/phase3g10-handoff.md`. Read it first for full context, then start at **Task S1.2 — VfoLevelBar widget + unit test** from `docs/architecture/phase3g10-rx-dsp-flag-plan.md` §S1.2. Use the `superpowers:subagent-driven-development` skill. Work only from the worktree at `/Users/j.j.boyd/NereusSDR/.worktrees/phase3g10-rx-dsp-flag` — do NOT work in the main repo. Dispatch one implementer subagent per Stage 1 task (S1.2, S1.3, S1.4, S1.6, S1.5, S1.7, S1.8, S1.9, S1.10, S1.11 — note the S1.6-before-S1.5 reorder), two-stage review between each, and stop at the end of Stage 1 to open a draft PR. Don't proceed to Stage 2 without explicit approval from me.

## Decisions already locked (from the brainstorming session)

1. **Scope A**: Phase 3G-10 finishes every RX-side DSP NYI before 3M-1 TX.
2. **Flag styling**: faithful AetherSDR port — flat transparent header, underline tabs, 26px `#c8d8e8` freq label, green-checked DSP toggles, blue-checked mode toggles, round `#c8d8e8` slider knobs.
3. **S-meter choice C**: gradient bar with S-unit ticks rendered *above* it (S1/3/5/7/9/+20/+40), cyan below S9 turning green above.
4. **Tooltips choice B**: Thetis-first with NereusSDR-voice fallback for native-only controls; tooltip coverage enforced by `tst_vfo_tooltip_coverage`.
5. **Mode containers A**: full AetherSDR parity — FM OPT, DIG offset, RTTY mark/shift inside the DSP tab; CW autotune inside the Mode tab; auto-show/hide on mode change.
6. **Persistence B**: per-slice-per-band bandstack. DSP-flavored state under `Slice<N>/Band<KEY>/*`; session-state (lock, mute, RIT/XIT values) under `Slice<N>/*`. Band key from `Band::bandKeyName()` (3G-8). Migration from legacy 3G-8-era keys on first launch.
7. **Approach 3 with immediate backfill**: Stage 1 ships the flag visual shell first, Stage 2 fills WDSP wiring, both on the same branch in the same plan.

These are not open questions; the executing agent should treat them as settled and look at the design doc only if it needs a rationale refresher.
