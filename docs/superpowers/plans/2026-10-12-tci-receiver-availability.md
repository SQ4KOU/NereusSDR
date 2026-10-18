# TCI receiver availability additive fix

**Goal:** Advertise each logical receiver only when its actual mapped slice exists, preserving the selected Slice A identity through absence and restoration.

**Spec:** `/Users/j.j.boyd/.config/nereus/work/codex-tci-wsjtx-crash-intake-2026-10-12.md`, remaining availability finding and managed handoff. Signed base `73986fb43e6417a0e1a7438ca880186e0768bc25`; previous empty-mode fix is frozen and already accepted.

**Architecture:** Reuse TciProtocol's existing logical receiver-to-slice mapping and RadioModel lifecycle signals. Add a common existence predicate; apply it to enabled state reads and refresh both exposed receivers on existing availability events. Keep signal routing, receiver identities, ownership, TX admission and DSP defaults unchanged.

**Tech stack:** C++20/Qt6, existing TciProtocol/TciServer/RadioModel and fake/device-free QtTests.

## Constraints

- Never alias Slice B as the absent selected A, create replacement slices, invent modes or change taken-not-chosen/program-PTT authorization.
- Source-first: study pinned Thetis TCIServer sendRXEnable/query/MOX/change contract plus existing Nereus slice map/lifecycle. Cite source facts; preserve exact existing headers/inline comments and provenance for adaptations.
- Honest initialization, query and MOX/lifecycle broadcasts for actual mapped slice existence; no unavailable set fabricates success or mutates an alias.
- Original a714 and 739 worktrees unchanged; one writer per file/builddir. No public posting/push/PR, live WSJT-X, hardware/network/RF/PTT/audio enumeration, settings, driver, app/deployment actions.
- Controller owns builds and Git. Mac narrow jobs2/nice19 only after protected diagnostic window release coordinated directly with combined; phone remains combined-owned. Existing tests for739 not repeated absent new composition reason.

## Task 1: Characterize and fix availability

Files: `src/core/TciProtocol.cpp/.h`, `src/core/TciServer.cpp/.h`; existing focused TCI tests (live-state, tx-mutex or remote-window); synthetic init golden only if its explicit mock contract changes; provenance/changelog only as required.

- [x] Read exact upstream/current paths and record source lines and current map/lifecycle behavior before edits.
- [x] Add meaningful failing cases: A only, B only, cold no slices; queries and MOX availability; live removal/restoration with stable IDs; explicit vacant/stale mapped IDs; mapped local desktop and remote mirror. Assert no empty modes, alias mutation or invented acknowledgements. Use fakes only, no PTT or timing experiment.
- [x] Controller compiles/runs selected new cases RED if practical; baseline existing739 evidence remains accepted.
- [x] Implement `bool TciProtocol::receiverAvailableNow(int receiver) const` (or an equally precise existing-helper extension): valid exposed receiver and actual mapped SliceModel; preserve mock receiver0 convention and mock RX2Enabled. Reuse for availability; refresh both logical receivers with existing events. Preserve mox/transmitRefused gating and fixed identifiers.
- [x] Controller rebuilds/runs affected narrow registered entries and needed mode/mapping compatibility checks, isolated HOME/CFFIXED_USER_HOME/offscreen. Run attribution/source gates.

## Task 2: Integrated review and local handoff

- [x] Sol6.1 high reviews requirements, source attribution, lifecycle/map order and exact test evidence. One correction wave and scoped re-review if required.
- [x] Controller verifies final tree, signs local additive commit, prepares exact unpublished PR delta and CURRENT report; sends actionable integration-ready result/resource release directly to combined/manager.
- [x] Describe any remaining real-client receive-only acceptance proposal; do not execute it. Broader UX/restoration policy needs JJ only if a new choice is required; this bugfix does not choose one.

## Workflow adaptation

Crew/cost-aware owns cadence: reuse the approved bug scope without a fresh design/approval ceremony; serial implementation with one integrated review. No routine acknowledgement dependency. Existing full-suite/platform/hardware approvals remain with their owners.

## Verified execution receipts

- Fresh worktree BASE73986fb43; original739/a714 checkouts preserved. Combined protected same-X window released before the jobs2/nice19 narrow build.
- Genuine RED: selected live-state3 passed/7 functional failures; desktop2 passed/1 functional failure. Source SHA manifest proves unmodified base; fixture start policy corrected before testing.
- Reviewed private source integrated byte-for-byte after RED. GREEN seven targets built; six registered entries passed, plus eighteen selected remote Qt results. Final explicit vacancy/stale-map Local+Remote rows rebuilt and live-state passed1/1.
- One integrated Sol6.1high review; its desktop fixture finding addressed by source-neutral correction, scoped re-review clear. Sol6.1medium implementation, controller owns integration/build/Git. Usage unknown.
- Existing upstream notices and comments retained; native attribution/precommit gates passed. Final signed head and authoritative logs are recorded in the private CURRENT report and verification manifest after local signing.
- No live WSJT-X, hardware, radio/PTT, driver, settings, application or deployment operation. Full-suite/platform CI and external-client startup/recovery acceptance remain for managed integration and any separate authorized runtime window. Remote mirrored-MOX authority gap remains pre-existing and outside this patch.
