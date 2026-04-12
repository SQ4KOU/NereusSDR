# Phase 3G-7 Polish — Execution Handoff

> **Status:** ✅ **Complete (2026-04-12).** 4 GPG-signed commits.
> **Branch:** `feature/phase3g7-polish` (off `feature/phase3g6-oneshot`)
> **Parent PR:** boydsoftprez/NereusSDR#2 (Phase 3G-6 one-shot, complete, awaiting merge)
> **Created:** 2026-04-12 (handoff)
> **Completed:** 2026-04-12 (execution)
>
> **What landed:**
> - `25a7819` feat(meters): add read-back getters for editor populate (item B, narrowed)
> - `8774b7c` fix(meters): preserve MMIO bindings across dialog clone paths (item A, side-channel)
> - `41c7031` feat(editor): wrap NeedleItemEditor in 5 QGroupBox sections (item C)
> - this docs commit
>
> **What was deferred:** items D, E, F, plus the "phantom field" feature
> ports flagged in original item B. See **Outstanding work** at bottom.
>
> **Investigation finding:** Item A and item B both turned out to be
> dramatically smaller than the handoff proposed once the actual code
> was read. Item A was a 4-site clone leak in one dialog file, not a
> 30-subclass serialize sweep. Item B was 5 subclasses missing getters
> for fields they already had, not 10 subclasses missing entire fields.
> The original handoff scoped pessimistically; both narrowings are
> documented per commit.

## Context

Phase 3G-6 (one-shot) shipped on `feature/phase3g6-oneshot` as PR #2 — 40 GPG-signed commits across 7 execution blocks. The PR description's "Known limitations after block 7" section names a small backlog of polish items that don't block merging 3G-6 but would close out the meter system as truly finished.

Phase 3G-7 picks up that backlog. **No code has been written yet on this branch** — only this handoff doc. The new session needs to make a tier decision before cutting commits.

## Current branch state

```
git checkout feature/phase3g7-polish
git log --oneline main..HEAD     # 38 commits, all from 3G-6 + this doc
```

The branch is bisect-clean. `cmake --build build` passes. `feature/phase3g6-oneshot` is `9749777` and identical to this branch's parent.

## The 6 polish items (the backlog)

| # | Item | Code touched | Effort | Value | Risk |
|---|---|---|---|---|---|
| **A** | MMIO binding serialization sweep | 30 subclass `serialize`/`deserialize` pairs | high (mechanical, parallelizable) | **high — fixes the runtime-visible "binding lost on Apply" limitation** | low if subagented |
| **B** | MeterItem subclass setter gaps | ~5 MeterItem subclass headers + cpps | medium | medium — unblocks editor fields that are currently dead | low |
| **C** | NeedleItemEditor `QGroupBox` grouping | 1 file (`NeedleItemEditor.cpp`) | low | medium — readability win for the most-fields editor | very low |
| **D** | Editor widget width sweep | ~30 editor cpps | medium-high (mechanical) | low — block 4b scroll area already solved the load-bearing problem | low |
| **E** | ButtonBox per-button `ButtonState` sub-editor | `ButtonBoxItemEditor` + new index spinner UI | high (architectural — needs ButtonState struct knowledge) | medium — unblocks per-button color customization for 8 button-box types | medium (untested code path) |
| **F** | End-to-end MMIO smoke test | docs only + manual interaction | low | **high — first real proof MMIO works** | low |

## Per-item detail

### A — MMIO binding serialization sweep

**Problem.** `MeterItem::m_mmioGuid` and `m_mmioVariable` (added in 3G-6 block 5 commit `8761906`) are in-memory only. When the dialog clones items via `serialize → createItemFromSerialized` on Apply, the binding is lost. User-visible failure mode: bind a meter item to an MMIO variable via the picker popup, click Apply, the binding silently drops.

**Fix.** Append two trailing fields `|<guidStr>|<varName>` to every concrete MeterItem subclass's serialize format, and have each deserialize check for them and read if present (backward-compatible with pre-3G-7 serialized state). The `MeterItem` BASE serialize can't be touched directly because `baseFields()` hardcodes the 6-field format and every subclass uses fixed offsets after that — see how block 1 commit `978a1ce` and block 2 commit `21b65db` solved the same problem for filter fields and calibration on `NeedleItem` / `NeedleScalePwrItem` (both append at the end of their own format, not in `baseFields`).

**Files affected.** Each `src/gui/meters/<X>Item.cpp` (or `MeterItem.cpp` for the inline classes `BarItem`, `SolidColourItem`, `ImageItem`, `ScaleItem`, `TextItem`, `NeedleItem`). 30+ subclasses total.

**Pattern per subclass.**
```cpp
QString XItem::serialize() const {
    return QStringLiteral("<TAG>|...existing fields...|%1|%2")
        .arg(m_mmioGuid.toString(QUuid::WithoutBraces))
        .arg(m_mmioVariable);
}
bool XItem::deserialize(const QString& data) {
    // ...existing parse...
    // Tail check (block 7 polish A):
    if (parts.size() >= existingFieldCount + 2) {
        m_mmioGuid = QUuid(parts[existingFieldCount]);
        m_mmioVariable = parts[existingFieldCount + 1];
    }
    return true;
}
```

**Parallelizable.** Yes — the perfect subagent fan-out. 4 agents × 7-8 subclasses each. Each agent reads `MeterItem.h` for the binding accessor signatures, reads its assigned subclass headers/cpps, makes the edits, writes report. Main agent integrates and builds.

### B — MeterItem subclass setter gap fills

**Problem.** Plan section 5 lists fields for several item types whose underlying classes lack the public setters. Block 4 agents skipped these and flagged them per-editor.

**Affected classes** (from block 4 agent reports — verify against the actual headers):
- `TextOverlayItem` — many fields have setters but **no getters**, so editor `setItem()` can't populate the controls. Need getters for: `fontFamily1/2`, `fontSize1/2`, `fontBold1/2`, `textBackColour1/2`, `panelBackColour1/2`, `padding`, `showTextBack1/2`.
- `SignalTextItem::showMarker` — no setter exists.
- `LEDItem` — `amberColour`, `redColour`, `condition` (string) — no setters.
- `HistoryGraphItem` — `keepFor`, `ignoreHistory`, axis min/max, `fadeRx/Tx` — no setters.
- `MagicEyeItem` — `darkMode`, `eyeScale`, `eyeBezelScale` — no setters.
- `RotatorItem` — `beamWidthAlpha`, `darkMode`, `padding` — no getters (write-only).
- `FilterDisplayItem` — all colors (no getters).
- `ClockItem`, `VfoDisplayItem` — color getters missing.
- `DialItem::vfoClickBehavior` — no setter.

**Files affected.** ~10 `src/gui/meters/<X>Item.h` and `.cpp` files.

**Caveat.** This is additive subclass work, not editor work. Once the setters/getters land, the relevant `BaseItemEditor` subclass should populate the field via `setItem()` — verify each editor's `setItem()` reads from the new accessor.

### C — NeedleItemEditor QGroupBox grouping

**Problem.** `NeedleItemEditor` is the largest editor (17 type-specific fields plus the 9 base fields plus a `QTableWidget` calibration table). Today they all stack flat under one "Needle-specific" header, which is hard to scan.

**Fix.** Wrap the type-specific section in 4 `QGroupBox`es:

1. **Needle** — source label, needle color, attack/decay, length factor, stroke width, direction
2. **Geometry** — needle offset X/Y, radius ratio X/Y
3. **History** — history enabled, duration, color
4. **Power** — normaliseTo100W, max power
5. **Calibration** — the existing `QTableWidget` (already its own visual unit, but wrap it in a group box for consistency)

**Files affected.** Just `src/gui/containers/meter_property_editors/NeedleItemEditor.cpp` (and `.h` if you add a new helper).

**Risk.** Very low — purely visual reorganization, no behavior change.

### D — Editor widget width sweep

**Problem.** `BaseItemEditor` helpers (`makeDoubleRow` / `makeIntRow`) set `minimumWidth(140)` per block 4b commit `f383229`, but the 30 subagent-produced editors hand-roll a lot of `QLineEdit` / `QComboBox` / `QPushButton` instances directly without the helper. Those still shrink-wrap.

**Fix.** Add `setMinimumWidth(140)` (or the equivalent) on every hand-rolled `QLineEdit` / `QComboBox` / `QPushButton` in the 30 editor cpps. Or better: add `makeLineEditRow` / `makeComboRow` / `makeColorButtonRow` helpers to `BaseItemEditor` and have each editor adopt them.

**Files affected.** ~30 cpps in `src/gui/containers/meter_property_editors/`.

**Recommendation.** Defer unless you really care. Block 4b's scroll area solved the load-bearing problem (content reachability). The cosmetic squeeze is annoying but not blocking.

### E — ButtonBox per-button ButtonState sub-editor

**Problem.** Plan section 5 lists per-button color/style/font fields (indicator on/off color, fill, hover, border, click, font color, etc.) for the 8 button-box subclasses. These live on a `ButtonState` struct **per button**, not on `ButtonBoxItem` directly. Block 4's `ButtonBoxItemEditor` exposes only the 7 top-level fields (columns, border, margin, etc.) and skips the per-button stuff entirely.

**Fix.** Add a per-button-index spinner inside `ButtonBoxItemEditor` (e.g., "Edit button: [0..N-1]"). When the index changes, populate a sub-form with the selected `ButtonState`'s fields. On change, write back to the indexed `ButtonState`.

**Files affected.** `src/gui/containers/meter_property_editors/ButtonBoxItemEditor.h/.cpp`. The 8 subclass editors (`BandButtonItemEditor` etc.) inherit and don't need changes.

**Risk.** Medium — this is the only architectural addition in 3G-7. Untested code path because nobody's been able to edit per-button state via the dialog yet.

**Recommendation.** Defer unless someone actually requests per-button customization. It's not blocking any common workflow.

### F — End-to-end MMIO smoke test

**Problem.** 3G-6 block 5 wired the MMIO subsystem end-to-end but the only runtime verification was `ExternalVariableEngine initialized with 0 endpoints` on startup. Nobody's actually sent a JSON packet to a UDP listener and watched a meter item update.

**Fix.** Either:
- **Doc-only** — write `docs/MMIO-SMOKE-TEST.md` with a step-by-step procedure (launch app, add UDP listener on port N, `nc -u -w1 localhost N` a JSON blob, pick variable in editor, see meter update). User runs it manually.
- **Automated** — drive through it via `osascript` + `nc` in bash. Risky because `osascript` menu nav has been finicky throughout 3G-6 and the dialog interactions are deep.

**Recommendation.** Doc-only. Past `osascript` automation attempts have eaten more session time than they've saved.

## Recommended scope tiers

### Tier 1 — must do (highest user value)
- **A — MMIO binding serialization** (the runtime bug)
- **F — MMIO smoke test doc** (proves MMIO works)

→ **2 items, ~3 commits, ~30 minutes**

### Tier 2 — nice to have
- **C — NeedleItemEditor grouping** (small, contained, big readability win)
- **B — Setter gap fills** (unblocks dead editor fields)

→ **+2 items, +2 commits, ~1 hour added**

### Tier 3 — defer to future
- **D — Width sweep** (pure cosmetic)
- **E — ButtonBox sub-editor** (untested architectural addition)

## Default proposal if user just says "go"

**Tier 1 + 2** (items A, B, C, F). 4 items, ~5 commits.

Item A uses 4 parallel subagents (7-8 subclasses each) with the same pattern as 3G-6 block 4 / block 5 phase 2. Items B, C, F are serial main-agent work.

Commit shape:
1. `feat(meters): subclass setter gap fills` (B)
2. `feat(meters): MMIO binding serialization` (A — produced via subagents, integrated by main)
3. `feat(editor): NeedleItemEditor QGroupBox grouping` (C)
4. `docs(mmio): end-to-end smoke test procedure` (F)
5. `docs(3G-7): mark polish complete + CHANGELOG entry`

## Three questions for the new session

1. **Tier choice** — Tier 1, Tier 1+2, or all six items?
2. **Parallelize item A** — subagents OK, or serial?
3. **Item F** — doc-only, or attempt automation?

Default: **Tier 1+2, subagents OK for A, doc-only for F.**

## Files the new session needs to read first

1. This file — full context.
2. `docs/architecture/phase3g6a-plan.md` section 6 (the rewritten one) — for MMIO context.
3. `CHANGELOG.md` "Known limitations" section — same list, different framing.
4. `src/gui/meters/MeterItem.h` — find the `m_mmioGuid` / `m_mmioVariable` accessors.
5. `src/gui/meters/MeterItem.cpp` `NeedleItem::serialize/deserialize` (~line 1033-1100) — reference pattern for appending optional trailing fields.
6. `src/gui/meters/NeedleScalePwrItem.cpp` — second reference pattern for the same.
7. `CLAUDE.md` (re-read on every new session per user preference).
8. `CONTRIBUTING.md` (same).

## Critical user preferences (from memory — must follow)

- **GPG-sign every commit.** Never `--no-gpg-sign`, never `--no-verify`.
- **Plans in `docs/architecture/`.** This file is here for that reason.
- **Re-read CLAUDE.md and CONTRIBUTING.md when starting a new task.** Do it before the first commit.
- **Diff-style code presentation.** Show diffs / small functions to the user, not large unchanged blocks.
- **Auto-launch after every successful build.** `pkill` + rebuild + `open` + screenshot for any visible change.
- **Visual verification mandatory.** Read screenshots with the `Read` tool before claiming a change worked.
- **GitHub signature.** PR descriptions and public comments end with `JJ Boyd ~KG4VCF` plus a `Co-Authored with Claude Code` line. Commit messages use `Co-Authored-By: Claude Code <noreply@anthropic.com>`.
- **Review public posts before posting.** Show PR descriptions / issue comments / release notes as drafts first.
- **Open GitHub links after posting.** `open https://github.com/...` after every PR/issue update.
- **User identity:** JJ Boyd, callsign `KG4VCF`. NOT `KK7GWY` (that's Jeremy, the project maintainer).
- **No redundant `73 de KG4VCF`** above the signature block.
- **Pause for review at block boundaries** — show the user the work, get a "go" or "yes/post" before posting public content.

## Branch management

- This branch is on top of `feature/phase3g6-oneshot`, not `main`. PR #2 (3G-6) is open and posted but not yet merged.
- When 3G-6 merges to `main`, this branch can be rebased onto the new main with `git rebase main` — clean rebase expected (no overlapping files).
- If 3G-7 work needs to ship before 3G-6 merges, open a separate PR pointing at `main` and let GitHub auto-resolve when 3G-6 merges first.
- **Don't force-push** without user permission.

## Side notes still pending (out of 3G-7 scope but worth knowing)

- **Real CrossNeedle PNG artwork** — current files are byte-identical copies of `ananMM.png`. Waiting on user.
- **TX wiring from `RadioModel` / `TransmitModel` into `MeterWidget::setMox()`** — phase 3I-1 territory. Don't touch.

## How the new session should begin

1. Read this file end to end.
2. Re-read CLAUDE.md and CONTRIBUTING.md.
3. `git checkout feature/phase3g7-polish && git log --oneline -3` to confirm state.
4. Ask the user the three questions above.
5. Wait for tier choice.
6. Execute per the chosen tier.
7. Pause at the end of each commit for review.

---

## Outstanding work after 3G-7 (file as separate issues)

These items were deferred during 3G-7 execution. Each is a standalone
unit of work with no dependency on the others — file as individual
GitHub issues so the tracking doesn't rot in this handoff doc.

### Dialog / persistence

1. **MMIO binding disk persistence.** 3G-7 fixed in-session clone
   leaks via side-channel; serialize/deserialize on each MeterItem
   subclass still does not include `m_mmioGuid` / `m_mmioVariable`.
   Save-to-file + reload, or app restart, drops bindings. The
   path forward is the original handoff's **Item A — full
   serialize sweep**: append `|<guidStr>|<varName>` to every
   concrete subclass's serialize format, with tail-tolerant
   deserialize. ~30 files, parallelizable across 4 subagents.
   See "Per-item detail § A" above for the pattern reference.

2. **MMIO end-to-end smoke test doc.** Single-page
   `docs/MMIO-SMOKE-TEST.md` walking a user through: launch app,
   open MMIO Variables dialog, add a UDP listener on port N,
   `nc -u -w1 localhost N` a JSON blob, bind a meter item via
   "Variable…", observe the meter respond. First real
   end-to-end proof.

3. **Snapshot revert MMIO test.** 3G-7 added the parallel
   `m_mmioSnapshot` vector to `ContainerSettingsDialog` but it's
   only proven by code review. Manual test: bind, open dialog,
   change something, click Cancel — confirm binding survives.

### Editors

4. **Editor `QLineEdit` / `QComboBox` / `QPushButton` minimum-width
   sweep.** `BaseItemEditor::makeDoubleRow` etc. set `setMinimumWidth(140)`
   but the 30 subagent-produced editors hand-roll some controls
   without that. Cosmetic only; block 4b's scroll area solved
   reachability. Clean approach: add `makeLineEditRow`,
   `makeComboRow`, `makeColorButtonRow` helpers to `BaseItemEditor`
   and have each editor adopt them.

5. **`ButtonBoxItemEditor` per-button `ButtonState` sub-editor.**
   The 8 button-box subclasses inherit a `ButtonState` struct
   array (per-button fill / hover / click / border / font). Today
   the editor exposes only the 7 top-level container fields and
   skips the per-button stuff entirely. Needs an `Edit button:
   [0..N-1]` index spinner that pops a sub-form bound to the
   selected `ButtonState`. Untested code path because nobody's
   been able to edit per-button state via the dialog yet.

### Phantom features (Thetis ports, not gap fills)

These were listed in the original handoff's item B as "missing
setters" but turned out not to exist on the items at all. Each
is a real feature port from Thetis MeterManager.cs and warrants
its own commit with proper source-first attribution per
CLAUDE.md. None of them block any current workflow.

6. **`LEDItem` custom amber/red zone colors.** Fields exist
   (`m_amberColour`, `m_redColour`) but no setter; threshold
   zones use hardcoded `#ffb800` / `#ff4444`. Editor only
   customizes "true" and "false" colors today.

7. **`LEDItem` condition-expression mode.** Thetis
   `clsLed.Condition` is a string expression
   (e.g. `value > 0 && tx`) evaluated against the binding
   value + radio state. NereusSDR currently supports only
   numeric thresholds.

8. **`SignalTextItem::showMarker`.** Bar-style marker line
   visibility toggle. Currently whatever the paint code does
   is fixed.

9. **`HistoryGraphItem::keepFor`.** Time-based retention
   (seconds) instead of capacity-based (samples). Functionally
   equivalent at fixed poll rate but more intuitive in UI.

10. **`HistoryGraphItem::ignoreHistory`.** Pause/resume toggle.
    Today history always accumulates while running.

11. **`HistoryGraphItem` manual axis min/max.** When autoscale
    is off there's currently no way to enter a fixed range.

12. **`HistoryGraphItem::fadeRx` / `fadeTx`.** Visual fade across
    RX↔TX transitions.

13. **`MagicEyeItem::darkMode`.** Light variant of the tube.
    Stuck with one visual style today.

14. **`MagicEyeItem::eyeScale` / `eyeBezelScale`.** Relative
    sizing of the eye vs the bezel. Fixed proportions today.

15. **`DialItem::vfoClickBehavior`.** Quadrant click action remap
    (VFO swap, mute, etc.). Hardcoded today.

### Out-of-meter polish (existing 3G-6 leftovers)

16. **Real CrossNeedle PNG artwork.** Files at
    `:/meters/cross-needle/*.png` are byte-identical copies of
    `ananMM.png`. Waiting on user art.

17. **TX wiring** from `RadioModel` / `TransmitModel` into
    `MeterWidget::setMox()`. Phase 3I-1 territory; do not
    touch in the meter system.
