# Phase 3Q — Connection Workflow Refactor — Execution Handoff

**Date:** 2026-04-27
**Branch:** `feature/phase3q-connection-workflow-refactor`
**Predecessor branch (still alive, harmless):** `claude/elegant-elgamal-e3a99e`
**Tip commit:** `7d656de` (plan), built on `1cdf8b3` (design + master-plan placement).

---

## Pasteable handoff prompt (copy this into a fresh Claude Code session)

```
You're picking up Phase 3Q for NereusSDR — the brainstorming and design
phases are complete and a detailed implementation plan is committed.
Your job is to execute the plan via the superpowers:subagent-driven-
development skill.

Branch + workspace:

  cd /Users/j.j.boyd/NereusSDR
  git checkout feature/phase3q-connection-workflow-refactor

  # Or, if you want an isolated worktree (recommended for parallel work
  # alongside Phase 3M-1 SSB TX):
  # git worktree add ../NereusSDR-phase3q feature/phase3q-connection-workflow-refactor

What's already committed on this branch (last two commits):

  1cdf8b3  docs: introduce Phase 3Q design + master plan placement
  7d656de  docs: implementation plan for Phase 3Q

Read these in this order before invoking any skill:

  1. docs/architecture/2026-04-27-phase3q-execution-handoff.md
     (this file — context + workflow + conventions)
  2. docs/architecture/2026-04-27-phase3q-connection-workflow-refactor-plan.md
     (the implementation plan — 8 PRs, 13 tasks, TDD steps)
  3. docs/architecture/2026-04-26-connection-workflow-refactor-design.md
     (the design spec — what + why for every behavior)
  4. CLAUDE.md (project conventions)
  5. CONTRIBUTING.md (contributor guidelines, commit format)

Mockups (gitignored, local-only — already in the worktree at the
predecessor branch path):

  .superpowers/brainstorm/67485-1777249005/content/connect-flows-r5.html
  .superpowers/brainstorm/67485-1777249005/content/menu-and-states-r6.html
  .superpowers/brainstorm/67485-1777249005/content/add-radio-flows-r7.html

If you can't see the mockups locally, the design spec embeds enough
detail to proceed without them.

Then invoke the skill:

  Skill: superpowers:subagent-driven-development
  Args:
    Execute the implementation plan at
    docs/architecture/2026-04-27-phase3q-connection-workflow-refactor-plan.md.

    PR-1 first — Tasks 1, 2, 3 sequentially, all foundational
    (ConnectionState enum, RadioDiscovery::probeAddress unicast probe,
    structured ConnectFailure + frameReceived signal on RadioConnection).
    These three commits stay on the branch as PR-1.

    PR-2 through PR-8 are independently mergeable after PR-1. Tasks
    within a PR are sequential; PRs themselves can be parallelized
    across multiple subagents.

    Use the existing tests/fakes/P1FakeRadio for connection-test
    scaffolding (the plan's Task 2 includes a similar FakeP1Probe
    pattern for the unicast-probe test).

    Every commit GPG-signed (-S). Every commit message follows the
    conventional-commits style of recent project commits ("feat(scope):"
    / "docs:" / "fix(scope):"). Pre-commit hooks (verify-thetis-headers,
    check-new-ports, verify-inline-cites, compliance-inventory) MUST
    pass on every commit.

The user will check in after each PR is complete. Open PRs against main
when each is green and let the user merge.
```

---

## Project conventions (binding for any subagent execution)

- **Build:** `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build -j$(nproc)`
- **Run:** `./build/NereusSDR`
- **Test:** `ctest --test-dir build` (or per-test: `ctest --test-dir build -R <name> -V`)
- **Language:** C++20, Qt 6 (Widgets, Network, Test). RAII; no raw `new`/`delete`. No `goto`. Atomic for cross-thread DSP — never hold a mutex in the audio callback. `qCWarning(lcCategory)` for errors. Braces on all control flow even single-line.
- **Naming:** Classes `PascalCase`. Methods/variables `camelCase`. Constants `kPascalCase`. Member variables `m_camelCase`.
- **Settings:** `AppSettings` (custom XML at `~/.config/NereusSDR/NereusSDR.settings`). **Never** `QSettings`. Keys are PascalCase. Booleans are `"True"` / `"False"` strings.
- **Tests:** Qt Test framework. Register via `nereus_add_test()` in `tests/CMakeLists.txt`. Patterns from existing `tst_radio_discovery_parse.cpp`, `tst_p1_loopback_connection.cpp`, `tests/fakes/P1FakeRadio.{h,cpp}`.
- **Commits:** GPG-signed (`git commit -S`). Conventional-commit style. Co-Authored-By trailer required:

  ```
  Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
  ```

- **Source-first attribution:** Any port from Thetis / mi0bot-Thetis / AetherSDR / WDSP needs the verbatim file header from upstream + an inline cite stamp like `[v2.10.3.13]` (release) or `[@501e3f51]` (commit). For Phase 3Q specifically, most code is NereusSDR-original (UI work) — only the `RadioDiscovery::probeAddress` parsing reuse touches Thetis-derived code, and the existing `parseP1Reply`/`parseP2Reply` helpers already carry their attribution.
- **Pre-commit hooks** (run automatically): `verify-thetis-headers`, `check-new-ports`, `verify-inline-cites`, `compliance-inventory`, `verify-inline-tag-preservation`. All must be green.
- **Platform guards:** `#ifdef Q_OS_WIN` / `Q_OS_MAC` / `Q_OS_LINUX`. Not `_WIN32` / `__APPLE__`.

## What runs in parallel — Phase 3M-1 SSB TX

Phase 3M-1 (SSB TX, the next major epic) runs in parallel with this work. No file-level overlap:

- 3M-1 touches: `src/core/TxChannel.{h,cpp}` (new), `src/core/AudioEngine` (input path), `src/core/RadioConnection` (TX paths), `src/models/RadioModel` (MOX state).
- 3Q touches: `src/gui/{TitleBar, MainWindow, ConnectionPanel, AddCustomRadioDialog, SpectrumWidget}.{h,cpp}`, `src/core/{RadioDiscovery, RadioConnection, AppSettings}.{h,cpp}`, `src/models/RadioModel`.

Both touch `RadioConnection` and `RadioModel` — be alert for merge conflicts when 3M-1 also adds signals/methods to those types. The 3Q changes are additive (new signal `frameReceived`, new method `connectToSavedMac`, new state machine) so conflicts should be mechanical.

## State of the world right now

- Branch `feature/phase3q-connection-workflow-refactor` exists locally, **not yet pushed to origin**. Push when ready:

  ```bash
  git push -u origin feature/phase3q-connection-workflow-refactor
  ```

- Branch `claude/elegant-elgamal-e3a99e` (the brainstorming worktree branch) points at the same tip. Harmless to leave around. Worktree at `.claude/worktrees/elegant-elgamal-e3a99e/` can be removed with `git worktree remove` once you've moved to the new branch.

- Master plan ([`docs/MASTER-PLAN.md`](../MASTER-PLAN.md)) lists Phase 3Q in:
  - Up Next section (Progress Summary)
  - Named phase section (full scope)
  - Recommended Next Steps (3Q + 3M-1 parallel pair)
  - Phase Dependencies (independent phases list)

## After execution

When all 8 PRs merge:

- Update `docs/MASTER-PLAN.md`: move Phase 3Q from "Up Next" to "Completed" and append the version it shipped in.
- Update `CLAUDE.md` "Current Phase" line.
- Cut a release per the `/release` skill.
- Notify Miguel (the original report) that the WireGuard-tunnel case now works.
