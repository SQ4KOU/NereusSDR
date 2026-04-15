# Phase 3G-9c (Clarity) — Session Handoff Prompt

> **Purpose:** Copy-paste prompt to resume work on Phase 3G-9c (PR3 in the Display Refactor arc) from a fresh session. Written 2026-04-15 at the end of the PR2 + 3G-12 shipping session.
>
> **How to use:** Start a new Claude Code session at `/Users/j.j.boyd/NereusSDR` and paste the fenced block below as the first message.

---

```
I'm picking up Phase 3G-9c (PR3 in the Phase 3G-9 Display Refactor arc) on the
NereusSDR project: the Clarity adaptive display tuning feature. This is the
third and final PR in the three-PR arc that started with PR #25 (3G-9a) and
continued with PR #26 (3G-9b).

## First step: sync and check state

BEFORE touching anything, verify where things stand:

1. cd /Users/j.j.boyd/NereusSDR
2. git checkout main && git pull --ff-only
3. git log --oneline -15 — look for PR #26 and PR #27 merge commits. If they
   haven't merged yet, note that. My PR3 work branches off main, so if the
   prerequisite PRs are still open, either wait for them OR base this branch
   off feature/display-smooth-defaults-pr2 (the PR2 branch, since PR3's
   rationale doc references PR2's palette work).
4. gh pr list — check the current state of PRs #26 (3G-9b smooth defaults)
   and #27 (3G-12 zoom persistence). Also scan for anything NEW on main
   that might affect the display subsystem — the parallel phase3g10 session
   may have landed changes to src/gui/SpectrumWidget.* or src/models/
   SliceModel.* that I need to be aware of.
5. Re-read CLAUDE.md and CONTRIBUTING.md (standing preference for every new
   branch).

Report back any surprises before proceeding.

## Read these before anything else

- docs/architecture/2026-04-15-display-refactor-design.md §6 — the PR3
  design section. Every locked decision (flavor C = continuous auto +
  one-shot re-tune button, name = "Clarity") is captured here.
- docs/architecture/2026-04-15-phase3g9b-smooth-defaults-plan.md — PR2's
  plan. PR3 builds on top of PR2's smooth-defaults profile, so the seven
  recipes and the AGC interactions matter.
- docs/architecture/waterfall-tuning.md — the rationale doc that PR2
  shipped. Its "Open questions for PR3 (Clarity adaptive)" section at
  the bottom has three concrete questions I need to resolve in the
  research doc.
- src/gui/SpectrumWidget.cpp — specifically pushWaterfallRow() around
  line 1430–1460 where Waterfall AGC currently lives. Clarity has to
  coexist with or supersede this. The existing AGC uses running min/max
  with 12 dB margin (set in PR2).

## The gate

PR3 is RESEARCH-DOC GATED. Do not write any code until you have:

1. Written docs/architecture/clarity-design.md with the full content
   specified in the design spec §6.2 (prior art survey, algorithm spec,
   test plan, failure modes catalog, UI spec).
2. Presented the research doc to the user for approval.
3. Received explicit user sign-off.

Only after those three steps do you invoke superpowers:writing-plans to
produce an implementation plan, and only then subagent-driven-development
to execute it.

## What Clarity is (recap from the design spec)

Flavor C: continuous adaptive thresholds + one-shot "Re-tune now" button.
Supersedes the current Waterfall AGC (which is crude — running min/max
with 12 dB margin, pumps badly on strong carriers). Clarity uses:
- Percentile-based noise floor estimator (candidate: 30th percentile)
- 2 Hz polling with EWMA τ ≈ 3s smoothing
- ±2 dB deadband before threshold updates
- Per-band memory stored in PanadapterModel::BandGridSettings
- Explicit TX pause via existing MOX signal
- Manual-override detection ("Clarity paused" indicator when user drags
  a slider)
- Master toggle in Setup → Display → Spectrum Defaults
- Status badge (green active / amber paused) in SpectrumOverlayPanel

The research doc locks each of these choices in writing with prior-art
references (Thetis WaterfallAGC, WDSP noise-floor code, GQRX / SDR++
auto-range) before any code.

## Name

The feature is called "Clarity". User locked this in during the
brainstorming session — do not rename.

## Skill to use

Start with superpowers:brainstorming to walk through the open research
questions (there are 5-6 locked-in choices from the design spec that
need validation against prior art, plus 2-3 genuinely open items the
research doc has to decide). Then write docs/architecture/clarity-design.md
directly (this is the research doc, not a plan). Show it to the user for
approval. After approval, superpowers:writing-plans to convert the
locked design into an implementation plan. Then
superpowers:subagent-driven-development to execute.

## Open questions the research doc must answer (from 3G-9b's waterfall-tuning.md)

1. Does Clarity supersede Waterfall AGC, or do they coexist? (Likely
   yes — supersede. Clarity's estimator is more robust than running
   min/max. Verify.)
2. Should the PR2 static defaults (seven recipes) be replaced by
   adaptive initial values? (Probably keep them as the fallback when
   Clarity is off.)
3. Should the current "DisplayProfileApplied" AppSettings key migrate
   to "DisplayProfileVersion" with an integer version? (PR2 removed the
   first-launch gate but reserved the key name for PR3 to repurpose.)

## Non-goals

- RX2 or TX display surface changes
- Spectrum Overlay panel refactors (only the Clarity status badge is
  added, no other changes)
- Skin system integration
- Changes to AverageMode, FFT window, or any other PR2-shipped knobs

## Context you should know

- PR #25 (3G-9a) is merged on main: source-first audit + tooltips +
  slider readouts.
- PR #26 (3G-9b) was open at the time of this handoff: smooth defaults
  + ClarityBlue palette + 12 dB AGC margin + Reset button. Status may
  have changed since — verify in Step 1.
- PR #27 (3G-12) was also open: zoom level persistence. Separate small
  fix, not directly related to PR3 but touches SpectrumWidget.cpp.
  Status may have changed — verify.
- There is a PARALLEL session working on Phase 3G-10 (RX DSP parity +
  AetherSDR flag port) in a worktree at .worktrees/phase3g10-rx-dsp-flag.
  Do NOT touch that worktree. It's an unrelated phase.
- Version on main: 0.1.4 (pre-PR2 merge) or newer. Check CMakeLists.txt.

## Signature convention (important)

On ALL public GitHub posts (PR descriptions, issue comments), use exactly
this signature block at the bottom — nothing more:

    JJ Boyd ~KG4VCF
    Co-Authored with Claude Code

Do NOT add "🤖 Generated with [Claude Code]..." — that duplicates
"Co-Authored with Claude Code" and creates visual noise. Memory
note at memory/feedback_no_generated_with_line.md.

Git commits use the structured footer:
    Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>

All commits GPG signed, never --no-verify, never --no-gpg-sign.

## When you've finished the research doc

Present it to the user and ask: "Research doc complete at
docs/architecture/clarity-design.md. Please review — especially the
algorithm spec (§6.2.2), the failure modes catalog (§6.2.4), and the
UI spec (§6.2.5). Sign off here before I invoke writing-plans to
convert this into an implementation plan."

Wait for explicit approval. Do not proceed to writing-plans until
the user says so.
```
