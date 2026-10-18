# Display live mockup rebuild: recovered execution plan

This continues the approved implementation. It records the October 12 recovery;
it does not reopen the design or request another mockup review.

## Authority and baseline

JJ said "ok lets make this happen" at 09:09:52 EDT and "yes build whats in the
live mockup" at 09:10:48 EDT on October 12, 2026. He chose Gradient under Fill,
below Fill Color. Work stopped afterward because usage was exhausted, and the
Codex manager's recovery request resumes it.

Original implementation: `claude/display-pill-spec`, fixed at `c3996f7f6`.
Rebuild: `claude/display-rebuild`, starting from that exact commit in the same
`display-spectrum-settings-debug-8a645c` worktree. Preserve the first branch.
Do not use the stale `sweet-ptolemy-590496` placeholder for code.

The accepted reference is [the live page](2026-10-12-display-rebuild-reference/display-pill-live.html),
with [the 66 recorded calls](2026-10-12-display-rebuild-reference/display-pill-mockup-veto-list.md).
Items 47 onward supersede earlier items where they differ. In particular, item
50 replaces item 43's Track floor behavior. The existing
[original plan](2026-10-10-display-pill-and-levels-plan.md) describes the completed
four-tab baseline; it is not the acceptance contract for this rebuild.

Historical evidence: Claude transcript `cbec45ac-93bc-4acd-bd27-9c708255a189`,
last report at 09:17 EDT; `.crew/2026-10-10-display-pill-and-levels-plan/progress.md`;
`ux-map-carriers.md` in that same work directory. The live page's build sources
were in the previous session's temporary scratch directory. The accepted HTML
and the latest 66-item list are copied here so recovery does not depend on that
scratch directory or an ignored work folder.

## Constraints carried forward

- No further mockup review rounds or design ceremony. Implement the live page.
- Gradient belongs under Fill, below Fill Color in Setup; the pill keeps the
  live page's compact Normalize / Fill / Gradient line.
- "Follow all pans" naming remains under discussion. Do not invent a replacement.
  The last report's implementation assumptions remain recorded for veto: every
  pan takes a master change, Black Level and Color Gain work in every mode, and
  one transmit scale belongs to this computer. Do not call those separate rulings.
- The pill remains 268 pixels wide, at most 480 pixels tall, with no scrolling.
- Retain existing upstream headers, attribution, and inline comments when moving
  existing code. No new upstream port is needed for the initial page regrouping.
- No radio/Core connection, live app launch, deployment, real settings changes,
  public posting, or new chats in this recovery. JJ's subsequent crew instruction
  authorizes bounded native subagents with disjoint file ownership, controller-only
  Git/build operations and no recursive leaf delegation. Current execution policy
  governs coordinated rollout separately from this unfinished Display branch.
- The current October 12 execution policy grants this worktree a concurrent
  narrow lane: at most two compile jobs and one isolated test process, nice 19.
  No broad suite, hardware run, timing run or native app launch is authorized.

## Execution order and acceptance

1. **Pill pages and shared presentation.** Five tabs in this order: Spectrum,
   Levels, Waterfall, View, FFT. Both spectrum-scale and waterfall-level controls
   belong on Levels; Background belongs on View. Preserve current signal/setter
   behavior while regrouping. Add the on-screen reason area, shared labels,
   units, and compact rows from the live page. Preserve remembered tab tokens.
   Verify tab exclusivity, last-tab restoration, page ownership, target seeding,
   model following, wheel handling, and the width/height limits with
   `tst_spectrum_overlay_panel`.
2. **Per-pan/master settings and effective values.** Extend existing AppSettings
   and PanDisplayKeys patterns to all picture controls. Add own-value dots and
   reset/copy actions without feedback loops. No-op selections create no
   override. Keep mixed inherited slider pairs within their gap. Show stored
   versus effective levels and explain every gray state using the live page's
   exact rules, including transmit, disconnect, old Core, and limited graphics.
   Verify settings inheritance, reset scope, pair limits, and reconnect seeding.
3. **Levels modes and rendering.** One waterfall chooser (Clarity / Auto / Floor /
   Manual / Scale), one spectrum-scale chooser (Manual / Track floor). Reuse the
   existing level controllers and APIs. Implement the approved trims in every
   mode and the displayed effective values with focused controller/renderer
   coverage. Keep the I/Q to WDSP to audio path outside this display work.
4. **Desktop Setup.** Eight Display pages: Spectrum, Levels, Waterfall, View,
   Peaks & Markers, FFT & Engine, Transmit Display, Multimeter. Add Settings for
   pan chips and the Band picker. Move Trace Color and Fill Color out of
   Appearance, preserve its other colors, and put Gradient below Fill Color.
   Share control names, ranges, units, and complete disabled reasons with the
   pill. Update the existing Setup binding/parity tests and navigation links.
5. **Saved transmit display scale.** This workstream owns the follow-up formerly
   studied by `saved-transmit-display-s`. Read its saved study before code:
   `tx-grid-scale-study-2026-10-11.md` in the display session's side-session notes.
   The baseline already persists a transmit scale per pan; the work is to expose
   the accepted one-per-computer scale and synchronize keyed edits, Setup, local
   and remote display requests, restart, and un-key. Do not assume it was unsaved.
   Preserve existing users' settings with a deliberate migration and tests.
6. **Other surfaces and descriptions.** Carry the same contract through the
   Display applet, Setup description and service, station catalog, described
   phone Setup, the phone's hand-built display page, and Display sheet. Follow
   existing version negotiation; older peers retain their previous payloads.
   Verify each surface's actual local/Core ownership and version gates, rather
   than assuming the desktop implementation reaches the phone automatically.
7. **Custom gradients and remaining live-page behavior.** Implement the receive
   Custom scheme editor and preserve the transmit editor behavior, color reset
   scope, rewind/speed readings, and the listed no-effect control fixes with
   functional tests. Made-up radio signals and web-only demo controls remain
   reference material, not app features.
8. **Integrated delivery.** Update superseded layout assertions as each step
   replaces their contract; run focused rebuilt tests offscreen with isolated
   settings, then visual verification when the manager grants the slot. Perform
   one whole-change review and one fix pass, with no repeated review loop. Save
   exact public title/body drafts for JJ's explicit posting instruction. Report
   the implementation head and a concrete combined-build test list.

Track floor PR 420 remains the merge shepherd's responsibility. On integration,
keep its fixed-grid gap correct until this rebuild's stored dB Step drives the
grid; then use the same step for drawing and the min/max gap, with a regression.

## Source mapping

The [baseline source map](2026-10-12-display-rebuild-source-map.md) identifies
row owners, scope-changing setters, Setup navigation dependencies, and every
phone carrier. Setup description V28 already exists at this base; the rebuilt
contract needs the next available version with V28 preserved. Existing
normalization and grid-step rendering are reused, not rebuilt from stale notes.

## Recovery progress

October 12: corrected worktree and clean baseline verified, rebuild branch
created, accepted HTML and latest 66-item list preserved, saved-transmit-scale
ownership recovered, and the initial page-regrouping regression added to
`tst_spectrum_overlay_panel`. The regression was observed failing on the
four-tab baseline, then passed after the production regrouping.

The first implementation checkpoint includes five-tab regrouping, fixed reason
areas and parent/2D/3D gates, plus the approved equality/master override rules.
The parent-gate and master-rule regressions also ran red before their production
changes. Focused targets: `tst_spectrum_overlay_panel`, `tst_pan_display_keys`,
`tst_pan_display_settings_inherit`, and `tst_display_setup_binding`.

The next checkpoint implements the compact Spectrum Trace/Grid/Markers and
Waterfall Drawing/Overlays/Time rows, normalization and its detector gate,
rate-dependent Rewind capacity text, STATION FFT scope and whole-label fitting.
Normalization and fifteen previously global marker/tracking fields now use the
same sparse per-pan load/save/reset path. Level Cal snapshots, suspends and
restores each pan's own Track floor choice, including a pan loaded during the
hold. Existing rendering constants and ranges are retained.

A bounded compatibility investigation found that version tags cannot restore
removed controls and moved page trees. Exact baseline Display28/Appearance27
resources are frozen, with a pre29 projection before the existing older passes.
All 56 category/cap1–28 payload cases are pinned to independently captured
baseline hashes. Live resources, validation envelopes, negotiation ceilings and
phone remain at their existing versions until the full29 change is ready.

This is not the complete live-page rebuild. Mode choosers, own-value dots/counts,
per-pan Clarity and remaining picture controls, eight Setup pages, transmit-scale
migration and controls, receive Custom editor, applet and versioned phone parity
remain in the same plan. The held naming question has not changed any labels.
The shared whole-app minimum11px font change is owned separately; compact rows
must be verified again after that signed change is integrated.

## Requested Clarity and chooser milestone

The rebuild now separates requested per-pan Clarity from its runtime active
mirror. Registry registration, feed/output/status, band and rebind resets,
un-key and retirement guards use each owner's requested value. Explicit master
broadcasts include equal-value resets and closed-pan overrides; the existing
model transition signal contract stays intact. The pill's captured pan receives
its Clarity request, rather than the model master.

The Levels, View and FFT choosers implement the accepted mode projection.
Clarity retains inactive fallback flags; a same-mode press and initial seeding
write nothing. Runtime C status remains beside one Re-tune action. Actual-font
geometry covers normal and wider text without abbreviating labels or lowering
the approved font minimum. Classic hidden-page note geometry is measured too.

Setup's scope foundation can edit an open or closed pan independently, reset
only that page, or copy its own values to the master while clearing open and
closed overrides. Copying one waterfall endpoint stops at the master's other
endpoint and its five-dB gap. Canonical closed-pan defaults come from the same
native lazy-value inventory as live save/read/reset, with no hidden widget,
model or settings access. Visible scope chips, band selection and regrouped
native pages remain the next UI step; the foundation alone is not that UI.

Controller observed initial behavioral failures before these changes. Fresh
focused verification passes the complete registry, inheritance, pill wiring,
Setup binding, classic overlay and actual workspace-pill targets. New V29
Core-description tests fail as expected at the unchanged live28/27 base; the
old frozen payload tests remain green. Version29 was rechecked as available
and its strict Core/phone metadata implementation is in progress separately.

Full effective/TX/trim presentation, own dots/counts, remaining peak/grid scope,
eight native Setup pages, TX migration, receive Custom and complete phone/applet
parity remain in the original approved task. No completed-full-rebuild or
runtime-deployment claim is made by this milestone.
