# Handoff — UI Polish + TX Bandwidth + Profile-Awareness

**Created:** 2026-05-01
**Worktree:** `/Users/j.j.boyd/NereusSDR/.claude/worktrees/ui-polish-foundation/`
**Branch:** `feature/ui-polish-foundation`
**Branched from:** `claude/stoic-dhawan-d8b720` at commit `5b1efa4`
**Started by:** brainstorming session (Claude Opus 4.7) on 2026-05-01

---

## What this is

A 5-plan implementation campaign that lands as one big PR (per locked design decision). Plans split for execution clarity but merge as a single PR per the user's "one big PR" choice.

Aggregate scope: ~120-150 unique files, ~2000-2200 new lines, 21 phased commits.

---

## Where everything lives

| Artifact | Location |
|---|---|
| Combined design doc | [docs/architecture/ui-audit-polish-plan.md](docs/architecture/ui-audit-polish-plan.md) — committed at `50a2830` (GPG-signed) |
| Inventory snapshot | `.superpowers/brainstorm/33882-1777646468/inventory-snapshot.md` (referenced by design doc; lives in original brainstorm worktree, not in this branch) |
| **Plan 1 — Foundation (start here)** | [docs/superpowers/plans/2026-05-01-ui-polish-foundation.md](docs/superpowers/plans/2026-05-01-ui-polish-foundation.md) |
| Plan 2 — Cross-surface | [docs/superpowers/plans/2026-05-01-ui-polish-cross-surface.md](docs/superpowers/plans/2026-05-01-ui-polish-cross-surface.md) |
| Plan 3 — Right-panel | [docs/superpowers/plans/2026-05-01-ui-polish-right-panel.md](docs/superpowers/plans/2026-05-01-ui-polish-right-panel.md) |
| Plan 4 — TX bandwidth | [docs/superpowers/plans/2026-05-01-tx-bandwidth.md](docs/superpowers/plans/2026-05-01-tx-bandwidth.md) |
| Plan 5 — Profile-awareness | [docs/superpowers/plans/2026-05-01-profile-awareness.md](docs/superpowers/plans/2026-05-01-profile-awareness.md) |

All plans + design doc committed at `5b1efa4` (5629 lines of plans, GPG-signed).

---

## How to pick up

**Open a new Claude Code session in this worktree:**

```bash
cd /Users/j.j.boyd/NereusSDR/.claude/worktrees/ui-polish-foundation/
claude
```

**First message in the new session:**

> "Read HANDOFF.md and continue with Plan 1 (Foundation) using subagent-driven execution."

The new session should:
1. Read this HANDOFF.md.
2. Read [docs/superpowers/plans/2026-05-01-ui-polish-foundation.md](docs/superpowers/plans/2026-05-01-ui-polish-foundation.md) — 28 tasks, ~70 files touched, mechanical refactor work.
3. Invoke the `superpowers:subagent-driven-development` skill.
4. Dispatch a fresh subagent for each task, review between tasks, commit after each.
5. After Plan 1 completes (~24 commits land), the user can choose to continue with Plans 2/3/4 in parallel (independent worktrees) OR sequentially in this worktree.

---

## Plan dependency graph

```
                    [design doc + plans]  (already on this branch)
                            │
                            ▼
                  ┌─── Plan 1 ─── Foundation
                  │     (palette consolidation, scrollbar gutter,
                  │      SetupDialog regime, modal dialog cleanup)
                  │
        ┌─────────┼─────────┬─────────────┐
        │         │         │             │
        ▼         ▼         ▼             ▼
     Plan 2    Plan 3     Plan 4 ──────► Plan 5
   Cross-     Right-      TX            Profile-
   surface    panel       bandwidth     awareness
   (B1-B8)    (C1+C2)     (E1)          (E2)

Plans 2, 3 independent of each other after Plan 1.
Plan 4 also independent after Plan 1.
Plan 5 needs Plan 4's FilterLow/FilterHigh schema as test fixtures.
```

**Recommended order in this worktree:** Plan 1 → Plan 2 → Plan 3 → Plan 4 → Plan 5 (sequential, single worktree, single PR).

**Alternative: parallel worktrees** after Plan 1 lands, create:
- `feature/ui-polish-cross-surface` worktree for Plan 2
- `feature/ui-polish-right-panel` worktree for Plan 3
- `feature/tx-bandwidth-filtering` worktree for Plan 4
- (Plan 5 waits for Plan 4 to land first)

For "one big PR" merge: merge each branch back to `feature/ui-polish-foundation` as Plans complete, OR cherry-pick.

---

## Bench-required tasks (need ANAN-G2 hardware)

Some tasks across the plans need verification on real hardware. Note these for scheduling:

| Plan | Task | What needs verifying |
|---|---|---|
| Plan 2 | Task 4 (B1 AGC-T direction) | Does Thetis's AGC-T slider go up = larger value or up = smaller? Verify against bench/screenshot. |
| Plan 2 | Task 18 (B6 XIT) | Engage MOX with XIT enabled; confirm TX NCO actually shifts. Test split (RIT + XIT both engaged). |
| Plan 4 | Task 13 (E1 final) | Sweep TX BW spinbox during MOX; verify panadapter band moves live and waterfall column appears. |
| Plan 4 | Task 13 | Profile activation persistence: customize → save → switch profile → switch back → verify restored. |
| Plan 5 | Task 22 (E2 final) | Visual regression sweep across all 11+ banner-equipped surfaces. Profile-switch-while-modified modal. |

If hardware isn't available immediately, sub-agents can land most code; the verification subset queues for later.

---

## Source-first / attribution requirements

- All commits **must be GPG-signed** (project policy — branch protection enforces this on `main`; pre-commit hook validates locally).
- Plan 4 Task 3 (factory profile filter values) requires `// From Thetis database.cs:<line> [v2.10.3.13]` cite per assignment.
- Plan 2 Task 16 (B6 XIT offset application) — cite `// From Thetis console.cs:<line>` if a Thetis line drives the offset formula. Initial check: simple `vfo + xitHz` addition; no complex algorithm to port. Verify during impl.
- Pre-commit hook (`scripts/install-hooks.sh` already installed) runs `verify-thetis-headers.py`, `check-new-ports.py`, `verify-inline-tag-preservation.py`. Don't bypass with `--no-verify` — fix attribution issues if any fire.

---

## State at handoff

- **Brainstorm worktree** (`claude/stoic-dhawan-d8b720`): contains `.superpowers/brainstorm/...` artifacts (visual companion screens, inventory snapshot). Brainstorm session ended; visual companion server stopped.
- **This worktree** (`feature/ui-polish-foundation`): has design doc + 5 plans, ready for Plan 1 implementation.
- **Origin/main**: at `2b70cc8` (PR #160 HL2 fixes merged). This branch is 2 commits ahead.

The brainstorm worktree's design doc + plans were branched from the same commit, so they're identical content here.

---

## Reading the plans (skill expectations)

- Each plan's tasks use `- [ ] **Step N:**` checkbox syntax — fill in as you go.
- Each task ends with a commit step that has the full commit message ready (including Co-Authored-By line).
- Plans assume `cmake --build build -j$(nproc)` and `ctest --test-dir build --output-on-failure` work as documented in `CLAUDE.md`.
- Tests live in `tests/` — a few new test files per plan (each plan documents which).
- Pre-commit hook output appears at every commit — that's normal.

---

## What the next-session prompt should look like

Past patterns suggest something like:

> "Read HANDOFF.md, then start executing Plan 1 (Foundation) with subagent-driven development. Use the superpowers:subagent-driven-development skill. Dispatch fresh subagents per task; review between commits; pause for me to verify after Tasks 1, 13, and 28 (the verification checkpoints baked into the plan)."

Or more terse:

> "Continue with Plan 1, subagent-driven."

Either works — the next session reads HANDOFF.md first, picks up the plan reference, and executes.

---

## Quick smoke check before starting

In the new session, before invoking subagents, verify the worktree is in a good state:

```bash
git status                              # clean tree expected
git log --oneline | head -3             # should show 5b1efa4 (plans), 50a2830 (design doc), 2b70cc8 (main)
cmake --build build -j$(nproc) 2>&1 | tail -10   # should build cleanly
ctest --test-dir build --output-on-failure 2>&1 | tail -5   # baseline tests pass
```

If `build/` doesn't exist (fresh worktree), bootstrap:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
```

---

## Tips for subagent dispatch

The `superpowers:subagent-driven-development` skill handles per-task dispatch. A few NereusSDR-specific notes for the dispatcher:

- **Auto-launch after build** (per user feedback memory): kill any running NereusSDR process + relaunch the binary after every successful build that touches UI. The plan steps include this command pattern.
- **Visual verification**: most A2/D tasks are visual-no-regression. Plans say "visual smoke check" with a `screencapture` command — that's a real screenshot the subagent should compare to a baseline saved at Task 1 of Plan 1.
- **Plan 1 Task 1 captures the baseline screenshot** at `/tmp/nereussdr-baseline.png`. The subagent for that task must do this — later tasks reference it.
- **GPG signing** is enforced by `git config --global commit.gpgsign true` (per CLAUDE.md). If a subagent's commit fails with a signing error, it's an environment issue (gpg-agent / passphrase) — surface to the user, don't `--no-gpg-sign`.

---

## When the campaign completes

After all 5 plans land (~21 commits across Plans 1-5):

1. Open one PR from `feature/ui-polish-foundation` (or whichever was the final branch in the chain) to `main`.
2. PR title: "feat(ui): UI audit + polish + TX bandwidth + profile-awareness".
3. PR description includes links to:
   - The design doc at the merge commit
   - Each plan
   - Bench verification notes (B1 slider direction result, B6 XIT result, E1 final test)
4. The natural review split point is between commit #14 (UI polish complete — end of Plan 3) and commit #15 (E2 infrastructure — start of Plan 5). If review feedback says split, the PR can be split there into two PRs.

---

## Questions for the user (not for the next session — for whoever opens the next session)

These came up during brainstorming and are still pending real-world signal:

1. **Plan 2 Task 4 (B1 AGC-T direction):** Which way does Thetis's slider go? Need bench or screenshot.
2. **Plan 2 Task 16 (B6 XIT):** Does the model→protocol path actually reach the radio? Needs ANAN-G2 verification.
3. **Plan 4 Task 13:** Does profile-switch-while-modified UX behave well in practice?
4. **Plan 5 (E2) overall:** ~1500 lines is the largest plan. If review feedback feels too dense, we have a documented split point — Plan 5 could become a sequel PR after Plans 1-4 ship.

---

End of handoff. The next session: open this worktree, read this file, start Plan 1.
