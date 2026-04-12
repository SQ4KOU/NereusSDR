# Phase 3G-6 (One-Shot) — Execution Handoff

> **Status:** Plan frozen, ready to execute.
> **Branch:** `feature/phase3g6-container-settings-dialog`
> **PR:** boydsoftprez/NereusSDR#1 (will be force-updated as execution proceeds)
> **Worktree:** `/Users/j.j.boyd/NereusSDR/.worktrees/phase3g6`
> **Plan document:** [`docs/architecture/phase3g6a-plan.md`](phase3g6a-plan.md)

---

## Execution Prompt

Paste the following into a fresh Claude Code session to pick up execution:

---

I'm resuming work on NereusSDR Phase 3G-6 (one-shot). The plan is fully decided and written; I'm asking you to execute it.

**Worktree:** `/Users/j.j.boyd/NereusSDR/.worktrees/phase3g6`
**Branch:** `feature/phase3g6-container-settings-dialog`
**PR:** boydsoftprez/NereusSDR#1 (force-update as you commit)

## Read these first

1. `CLAUDE.md` — project conventions, source-first porting protocol, style rules.
2. `CONTRIBUTING.md` — coding conventions, GPG signing requirement, feedback loop patterns.
3. `STYLEGUIDE.md` — dark theme palette, widget styling rules.
4. `docs/architecture/phase3g6a-plan.md` — **the plan you are executing.** This is the authoritative source for scope, decisions, file touch list, acceptance criteria, and the commit sequence.
5. `docs/architecture/phase3g6-debug-handoff.md` — the original debug findings that motivated this phase (archival, not authoritative; superseded by the plan).
6. `docs/architecture/phase3g4-g6-advanced-meters-design.md` — design spec for meter items (referenced when implementing per-item property editors).

## Goal

Full Thetis parity for the NereusSDR meter system's user-facing configuration surface, in a single big-bang phase. This includes:

- Fix ANANMM/CrossNeedle background rendering (missing PNG paths, Qt resources).
- Fix scale-overlap / displayGroup filtering bugs in NeedleScalePwrItem and base MeterItem.
- Rewrite `ContainerSettingsDialog` to Thetis's 3-column layout (container dropdown + Available / In-use / Properties).
- Replace the live preview panel with in-place editing + snapshot/revert on Cancel.
- Add all 8 new container-level fields (Lock, Notes, Highlight, Title-bar toggle, Minimises, Auto height, Hides-when-RX-not-used, Duplicate action).
- Build per-item property editors for **every implemented MeterItem subclass** (~30 types) with 100% Thetis parity on fields Thetis exposes.
- Port Thetis's MMIO (Multi-Meter I/O) variable system: TCP/UDP/serial transports, variable registry, picker UI, parse rules, thread-safe cache, persistence.
- Add a Containers menu submenu listing all open containers.

## Critical user preferences (from memory, MUST follow)

- **GPG-sign every commit.** Never skip signing. No `--no-gpg-sign`, no `--no-verify`.
- **Plans in `docs/architecture/`.** Already done for this phase; don't move the plan file.
- **Re-read `CLAUDE.md` and `CONTRIBUTING.md` when starting a new task.** (You should have just done that.)
- **Diff-style code changes.** When presenting changes to the user, show diffs or full small functions, not large unchanged blocks.
- **Auto-launch after successful build.** After each build checkpoint, kill any running NereusSDR instance, rebuild, launch again, and screenshot to verify.
- **Visual verification is mandatory.** Every visible change gets a screenshot read with the `Read` tool before claiming the change worked. Use `screencapture -x` on macOS.
- **GitHub signature:** public GitHub comments and PR descriptions end with `JJ Boyd ~KG4VCF` + a Co-Authored-with-Claude-Code line. Commit messages use `Co-Authored-By: Claude Code <noreply@anthropic.com>`. No redundant `73 de KG4VCF` above the signature.
- **Review public posts before posting.** Show PR descriptions, issue comments, and release notes as drafts for approval before posting.
- **Open GitHub links after posting.** `open https://github.com/...` after every posted PR/issue/comment.
- **User identity:** JJ Boyd (KG4VCF). NOT KK7GWY — that's Jeremy, the project maintainer.

## Assets provided by user

- `resources/meters/ananMM.png` — 1472×704 RGB PNG, already in place. This is the ANANMM multi-scale background.
- `resources/meters/cross-needle.png` — **user to supply before executing the CrossNeedle-related commits.** Pause and ask if not present.
- `resources/meters/cross-needle-bg.png` — **user to supply before executing.** Same rule.

If the CrossNeedle PNGs are missing when you reach commit 2 (`fix(3G-6): use resource paths for ANANMM and CrossNeedle backgrounds`), ask the user to provide them before continuing.

## Execution order

Follow the 48-commit sequence in the plan document (section "Commit strategy"). The commits are grouped into 7 blocks:

1. **Rendering quick wins** (commits 1–5) — qt resources, ananMM path fix, base-class display-group filter, NeedleScalePwrItem fix.
2. **Container surface** (commits 6–10) — new state on ContainerWidget, duplicate + allContainers on ContainerManager, FloatingContainer title-bar/minimised support.
3. **Dialog rewrite** (commits 11–19) — remove preview, 3-column layout, dropdown, snapshot/revert, categorized Available list, reorder, container-level property bar, footer buttons.
4. **Per-item editors** (commits 20–35) — one editor per item type, then Copy/Paste clipboard.
5. **MMIO subsystem** (commits 36–44) — engine, transport workers (TCP/UDP/serial), parse rules, MeterPoller integration, dialogs, persistence.
6. **Menu restructure** (commits 45–46) — Edit Container submenu, verify Reset Default Layout.
7. **Polish + docs** (commits 47–48) — CHANGELOG, CLAUDE.md phase table, README, mark debug findings resolved.

**Pause for user review after each block.** Summarize what landed, attach screenshots for visual changes, wait for "continue" before the next block.

## Before you touch MMIO code

The plan's MMIO section is based on a shallow Thetis exploration. Before writing any `src/core/mmio/` code:

1. Dispatch a targeted `Explore` subagent on the Thetis repo (`/Users/j.j.boyd/NereusSDR/Thetis`) focused on:
   - `setup.cs` lines 25479–25548 (the `btnMMIO_variable` buttons)
   - The variable manager UI (search for `frmMMIO*`, `frmMultiMeterVariable*`, or equivalent)
   - Transport classes (search for `MMIO`, `VariableIn`, `TcpClient` / `UdpClient` / `SerialPort` usage in the Console directory)
   - The parse-rule format Thetis uses (look for `Parse*`, `Decode*`, regex-related code in variable handling)
2. Update the plan document's MMIO section (section 6) with any reality-checks, citing Thetis file:line throughout per the source-first protocol.
3. Re-confirm the user's OK on MMIO scope before writing code.

## Testing pattern (per block)

After each block finishes:

```bash
# Kill running app
pkill -f NereusSDR.app/Contents/MacOS/NereusSDR 2>/dev/null; true

# Build
cd /Users/j.j.boyd/NereusSDR/.worktrees/phase3g6
cmake --build build -j$(sysctl -n hw.ncpu)

# Launch
open build/NereusSDR.app
sleep 3

# Screenshot
mkdir -p /tmp/nereus_dbg/3g6
screencapture -x /tmp/nereus_dbg/3g6/block-N-main.png

# Read screenshots with Read tool
```

Archive screenshots per block under `/tmp/nereus_dbg/3g6/block-N-*.png`.

## Acceptance criteria

See the plan document's "Acceptance criteria" section. Phase is not complete until every checkbox passes a real test against a real build. No "probably works" claims.

## Branching

Continue on `feature/phase3g6-container-settings-dialog`. Many atomic commits (≈48), all GPG-signed, all imperative mood. Update PR #1 description at each block boundary with the progress summary.

## How to begin

1. Read the files listed at the top.
2. Verify the worktree state: `git status`, `git log --oneline -5`, `ls resources/meters/`.
3. Confirm ananMM.png is in place; ask for CrossNeedle PNGs if not.
4. Execute block 1 (commits 1–5).
5. Visually verify (screenshot + `Read` inspection).
6. Summarize and pause for user OK.
7. Continue with block 2 if approved.

---

## Why this handoff exists

Phase 3G-6 was originally landed as "WIP — needs visual debugging" after an earlier session built the infrastructure but left it with multiple rendering bugs and a minimal property editor. A subsequent debug session (see `phase3g6-debug-handoff.md`) found:

- ANANMM/CrossNeedle backgrounds referenced missing `resources/meters/*.png` paths that weren't Qt-resource-registered.
- Scale labels from TX-only NeedleScalePwrItems painted on top of RX-only ones (missing displayGroup filter).
- Live preview panel rendered as solid black when editing Container #0 (nested QRhiWidget in modal dialog, edit-path-specific).
- Property editors existed for only 6 item types (out of ~30), each minimal — couldn't tune an ANANMM needle from the UI at all.
- No menu-based access to containers other than Container #0.

An interview pass decided the fix should be a **single one-shot phase** covering the entire Thetis meter-configuration surface rather than a phased 3G-6a/6b/6c rollout. MMIO (Thetis's external-data subsystem) is included. The dialog stays standalone rather than moving into SetupDialog as a tab.

The plan document is `docs/architecture/phase3g6a-plan.md`. The filename retains the `a` suffix for history — it was originally titled "3G-6a" before the one-shot collapse; the file content has been rewritten for the one-shot scope but the path was kept to avoid breaking links.
