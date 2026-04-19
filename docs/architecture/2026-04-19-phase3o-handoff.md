# Phase 3O VAX — Sub-Phase 3+ Continuation Handoff

## Where we are

Sub-Phases 1+2 of Phase 3O shipped as a PR from `feature/phase-3o-vax`
(branch in the worktree `~/NereusSDR/.worktrees/phase-3o-vax`). The
design spec, implementation plan, and the data-model + audio-abstraction
scaffolding are landed and GPG-signed. The rest of the plan is ready
for execution.

Run Sub-Phase 3 onward using `superpowers:subagent-driven-development`:
fresh implementer subagent per task, spec-compliance reviewer, then
code-quality reviewer, then fixes-and-re-review until approved.

## Key docs

- Design spec — `docs/architecture/2026-04-19-vax-design.md`
- Implementation plan — `docs/architecture/2026-04-19-phase3o-vax-plan.md`
- Project conventions — `CLAUDE.md` (AppSettings, atomics, pre-commit
  hooks, GPG signing, source-first porting for Thetis / AetherSDR / WDSP)
- Attribution rules — `docs/attribution/HOW-TO-PORT.md` (rule 6 for
  AetherSDR ports: no per-file GPL headers, cite at project URL; rule 4
  for multi-source stacked Thetis headers — not needed in Phase 3O after
  Direct ASIO was dropped in the GPL review)
- Widget styling — `STYLEGUIDE.md` (palette, button active-states, inset
  boxes, slider and combo conventions — new UI in Sub-Phases 8/9/10/11/12
  must follow)

## State at handoff

- Worktree: `/Users/j.j.boyd/NereusSDR/.worktrees/phase-3o-vax`
- Branch: `feature/phase-3o-vax`
- Build: clean (`cmake --build build-tests -j4`)
- Tests: full suite runs with 4 pre-existing failures
  (`tst_container_persistence`, `tst_p1_loopback_connection`,
  `tst_hardware_page_capability_gating` segfault, `tst_about_dialog`).
  No new failures. The 32 new Phase 3O tests from Sub-Phases 1+2 all
  pass.
- Pre-commit hooks: all green
- No uncommitted changes at PR push

## What's next — Sub-Phase 3: PortAudio Engine

Three tasks, all in plan §"Sub-Phase 3":

- **Task 3.1** — Vendor PortAudio v19.7.0 via CMake FetchContent.
  Static-link, `PA_USE_ASIO=OFF` (Direct ASIO was dropped in the GPL
  review; PortAudio's built-in ASIO host API is also gated off).
  **Critical sub-step:** verify the bundled `LICENSE.txt` at the
  v19.7.0 pin is the MIT-compatible Ross Bencina / Phil Burk grant
  before committing. If it's anything else, stop and surface.

- **Task 3.2** — `PortAudioBus` minimal render-only. Open PortAudio
  default output, accept `push()` from main thread into a lock-free
  ring, drain in the PortAudio callback. Peak-level metering via
  `std::atomic`.

- **Task 3.3** — Host API + device enumeration (needed by SetupAudioPage
  in Sub-Phase 12).

- **Task 3.4** — TX capture support (open input stream + `pull()`).

Difficulty per plan: medium-hard. PortAudio callback model and format
negotiation need careful handling.

## Forward issues to address in Sub-Phase 4

- `MasterMixer::setSliceGain` / `setSliceMuted` implicitly insert into
  the slice map via `operator[]`. Latent data race if called on a new
  slice ID mid-stream. Sub-Phase 4 must add an explicit
  `registerSlice()` entry point or document + enforce startup-only
  callers.

## How to start a fresh session

1. Verify the worktree is clean —
   `cd ~/NereusSDR/.worktrees/phase-3o-vax && git status`
2. Pull any new commits from origin —
   `git pull origin feature/phase-3o-vax`
3. Invoke `superpowers:subagent-driven-development`
4. Dispatch Task 3.1 implementer. The plan text at
   `docs/architecture/2026-04-19-phase3o-vax-plan.md` §"Sub-Phase 3:
   PortAudio Engine" → Task 3.1 is the authoritative task description;
   paste the full task + surrounding context + project conventions into
   the implementer prompt.

## Review lessons learned (avoid repeating)

- Slice-scoped AppSettings keys use `Slice<N>/` PascalCase (via
  `slicePrefix()` helper in `SliceModel.cpp:729-731`). Global keys
  lowercase like `tx/OwnerSlot`.
- Atomic memory ordering convention: `acquire` on loads, `release` on
  stores, `acq_rel` on `exchange`. Do not use default `seq_cst`.
- Don't call `AppSettings::instance().save()` eagerly from setters;
  batch-flush at caller. Verify by grepping existing setters first.
- No ported code in Sub-Phases 1–4 — those are NereusSDR-original.
  Sub-Phase 5 (macOS HAL) starts the port work from AetherSDR;
  Sub-Phase 6 (Linux bridge) continues; Sub-Phase 9 (VaxApplet +
  MeterSlider) is the biggest port. `HOW-TO-PORT.md` rule 6 governs
  AetherSDR ports.
- When fixing spec/plan errors, update BOTH docs so downstream tasks
  inherit the corrected convention (Task 1.1 had this — the key
  convention was wrong in the spec itself).
- Clangd diagnostics in the IDE are often from the main checkout, not
  the feature worktree. Ignore them unless the worktree build itself
  fails. The worktree's `cmake --build` + `ctest` is the truth.

J.J. Boyd ~ KG4VCF
