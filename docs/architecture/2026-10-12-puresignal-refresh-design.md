# PureSignal instruments and IMD display refresh

Date: 2026-10-12 (America/New_York)
Status: visual directions approved in conversation; consolidated design prepared for implementation review.
Implementation has not started. The IMD failure has not been reproduced on the maintainer's radio.

## 1. Intent and approved direction

Make PureSignal feedback, correction status, amplifier curves, and two-tone measurements attractive and useful in NereusSDR's new workspace language. An operator should be able to distinguish feedback quality, corrections being applied, calibration activity, and measured distortion without interpreting decorative gauges as DSP measurements.

The maintainer selected:

1. The compact applet direction from the first study.
2. **PureSignal Bench view**, with persistent feedback/correction instruments, calibration controls beside them, and exposed diagnostics.
3. **AmpView Linked plots**, separating magnitude/gain and phase with a shared input position.
4. **IMD Measurement rail** as the everyday panadapter view, with **Bench inspector** available as an option.
5. Include diagnosis and repair of the reported IMD measurement failure in the implementation work.

The display-pill work is a reference for the fidelity and interactivity of mockups, not a requirement to copy the display pill's visual layout.

### Durable visual references

Snapshots are preserved under [2026-10-12-puresignal-refresh-mockups](2026-10-12-puresignal-refresh-mockups/README.md):

- [Applet study](2026-10-12-puresignal-refresh-mockups/applet.html): select Applet.
- [Window study](2026-10-12-puresignal-refresh-mockups/windows.html): PureSignal / Bench view and AmpView / Linked plots.
- [IMD panadapter study](2026-10-12-puresignal-refresh-mockups/imd.html): Measurement rail, with optional Bench inspector.

These are visual and interaction references, not executable Qt specifications. Their synthetic samples, initially enabled test signal, illustrative axes, timing of correction events, and preview-condition pickers are not production defaults or measurements. The text below takes precedence where a mockup conflicts with sourced behavior.

## 2. Baseline and reference study

The document was authored in the primary checkout at `e342467fb`, with pre-existing maintainer changes left intact. The current local `origin/main` is `1d99789ce8f0cbc7d3347f7d2e49a16d9cc4317e`; implementation must revalidate and use the then-current integration baseline in an isolated checkout. The newer source includes `MoxDisplayController`, real DUP routing, the workspace styling layer, and PS3 session-facade/snapshot presentation that the primary checkout lacks. Do not transplant its older MainWindow wiring wholesale.

Thetis was inspected locally at `v2.10.3.15`. References are:

| Source | What it establishes | Decision |
| --- | --- | --- |
| `Project Files/Source/Console/PSForm.cs` and its designer | Existing calibration controls, correction/feedback predicates, file eligibility, hardware peak adjustment, dialog lifecycle | Retain behavior and controls; design a Nereus bench layout |
| `AmpView.cs:107-153, 156-257, 355-482` and designer | Five series, spline evaluation, magnitude/gain modes, phase zoom, sample stride | Retain math, ranges, and feed; replace presentation with linked plots |
| `display.cs:5014, 5518-5590, 5600-5718, findImd` | Four measurement gates, six-peak matching, reference levels, smoothing, frequencies, missing-peak explanations | Retain measurement semantics; design rail/inspector and waterfall annotations |
| Nereus `AmpViewChart.h` | Magnitude `[0,1]`, gain `[0,2]`, phase `[-180,+180]`, phase zoom `[-45,+45]`, low-res stride 4 versus 1 | These ranges override the illustrative HTML ranges |
| Nereus `gui/styles/WorkspacePreview.h` on `origin/main` | Existing workspace color roles, classic fallback, 11 logical-pixel text floor | Reuse existing styling APIs; no independent global theme |
| Nereus `PureSignalSessionFacade`, `Ps3Snapshot`, `Ps3DisplayAdapter`, current applet/PsForm/AmpView on `origin/main` | Current local/remote action permissions, settings, status, owned display snapshot X domains, generations, sequence and capture-age invalidation | Preserve the facade/adapter boundary and current PS3 semantics; do not restore the older direct buffer feed |
| Nereus `MoxDisplayController` on `origin/main` | Pan hosting TX slice owns keyed display; DUP retains RX frames; DUP off by default | Preserve owner and routing; diagnose IMD against the actual selected source |

This is a Nereus presentation redesign based on the references, not a pixel-for-pixel port of WinForms layout. Any algorithm repair or partial-product extraction derived from Thetis is a port and must follow `docs/attribution/HOW-TO-PORT.md`.

## 3. Scope and constraints

- C++20 / Qt6; reuse QPainter and the existing spectrum/GPU overlay mechanisms. No new dependency, renderer, FFT engine, DSP instance, thread, or transport format.
- No file under `src/core/` or `src/models/` includes GUI headers (R1).
- All new measurement presentation state lives on the GUI thread. Painting consumes cached state and never advances EMA or performs DSP.
- Preserve WDSP calls, coefficient evaluation, DSP ranges/defaults/scaling, feedback thresholds, attenuation decisions, two-tone frequencies/power, and all existing TX safety checks.
- Opening a window, enabling measurement display, selecting a product, opening the inspector, or toggling guides never keys the radio or changes DUP, FFT parameters, VFO, mode, filter, or radio settings.
- Two-tone and calibration actions retain their existing authoritative controllers and behavior. No mockup-local simulation becomes production behavior.
- Use `AppSettings`, existing setting keys, and model-sync guards. New presentation keys use PascalCase names and boolean strings `"True"` / `"False"`; do not rename legacy keys as part of this work.
- Keep existing functionality, file eligibility and local/remote permission checks, modeless singleton ownership, hide-on-close, saved geometry, and On Top behavior. Retain legacy Advanced collapse compatibility through the existing API; expanded content becomes the approved bench layout.
- Preserve classic-look fallback when the workspace styling switch is disabled. Use the established readable-font helpers; essential labels/readings never fall below the 11 logical-pixel floor.
- No unrelated applet cleanup or global font/theme migration.
- **Core RX path flag:** checking whether the IMD display receives the correct feedback spectrum touches the boundary between RX/PS feedback routing and FFT display. Read-only tracing is required. If repair requires changing I/Q → WDSP → audio, allocating a new DSP/FFT channel, or changing signal routing/threads, stop that repair branch, document the evidence and a concrete proposal, and obtain maintainer approval. Other independent GUI tasks can proceed.

## 4. Applet

Use the existing applet container/header and width conventions. Preserve current main's `PureSignalSessionFacade` action/status/settings boundary, including local/remote actuation and arming refusals, instead of binding new widgets directly to an older coordinator API. The compact body presents:

- Correction state using the coordinator's actual `correctionsBeingApplied` and `isCorrecting` predicates.
- RF feedback level, its sourced range classification, and a quiet horizontal meter with correctly positioned tick/target geometry.
- Existing calibration count with an accurate label; do not substitute correction count for calibration attempts.
- Calibrate, Auto, and Two-tone controls; existing coefficient Save/Restore and a discoverable route to the full window.

The older primary-checkout applet's so-called correction magnitude is sourced from the calcc hardware peak; current main already uses PS3 facade status/summary fields. It is not a measurement of distortion reduction. Show that reference in the full window's hardware peak section, and give the applet correction status instead of inventing a correction-strength gauge. This changes the visible presentation; the old underlying logic remains available for diagnostics.

Right-click entry to the full window remains. Disconnection clears live-looking values and disables inapplicable actions. Rebinding to a recreated coordinator must not leave duplicate signals or stale model state. Initial control states come from the model, not the mockup.

## 5. PureSignal Bench window

The expanded full window uses the selected Bench layout:

1. Top action row: existing calibration / Two-tone / OFF actions plus AmpView access.
2. Left persistent instruments: feedback level and classification; correction-applied state; actual calibration/correction counters with distinct labels.
3. Right detail area: Calibration and IMD measurement tabs. Changing tabs has no measurement or TX side effect.
4. Exposed diagnostics beneath the instruments retain current available legacy and raw PS3 fields, native/routing state and action outcome. Existing PS-RX/PS-TX spectrum display option stays disabled with its unavailable explanation on current main; preserving that control does not authorize a new feedback display source.
5. Correction file actions and eligibility/refusal explanation in the footer; existing On Top control. Current PS3 Apply Current/Start Auto/arming controls remain reachable in the action/control grouping.

Calibration preserves the current PS3 controls: MOX/CAL waits, AMP delay, Auto Attenuate, Quick Attenuate, Automatic desired, Run calibration processing, Override hardware peak, GetPk/SetPk/Default, Start Auto, Apply Current, Single Cal, Two-tone and OFF. Preserve native/routing/action-result status, raw PS3 diagnostics and facade permission/refusal behavior. PIN/MAP/STBL/TINT/Relax Tolerance appear in the older mockup, but are not grounds to restore unsupported legacy controls or DSP behavior. Retain them only if supported by the actual execution baseline; the current native/facade control inventory takes precedence. Reuse current facade handlers and exact sourced limits/defaults. Retain Show Two-tone Measurements and existing persisted preferences. GetPk and SetPk keep their distinct measured/expected semantics, sourced from their actual getters.

Treat correction-applied, feedback quality, and current calibration activity as separate facts. Receiving, retaining coefficient data, actively applying correction, and having poor feedback must not be conflated. OFF reflects the actual reset result; it does not merely change a decorative status label. Counters and raw diagnostic codes come from the current facade/status snapshot; never infer a WDSP state code from a UI caption.

Use `MoxDisplayController::keyedPanId()` to select measurement ownership; `transmitPanId()` is empty with DUP on and is not an IMD owner lookup. The IMD tab consumes the same keyed-pan measurement frame as the pan rail/inspector. It must not run another analyzer or advance EMA independently. If there is no eligible keyed pan or usable feedback source, show an explicit status and blank current readings. Existing available advanced/intercept diagnostics remain reachable with their sourced calculations and units; they are not silently relabeled as calibrated RF output power.

## 6. AmpView Linked plots

Retain five series and their numerical construction: Ref, MagAmp, PhsAmp, MagCorr, PhsCorr. On the execution baseline, retain `PureSignalSessionFacade::displaySnapshotReady` → `AmpViewWindow::acceptSnapshot` → `Ps3DisplayAdapter::transform` → `AmpViewChart::setPlotData`. The old `fillAmpViewBuffers` feed has been removed on current main. Preserve PS3 transformations, independent measured/magnitude-correction/phase-correction X domains, subscription lifecycle, generation/sequence rejection and capture-age freshness. Do not reintroduce old cubic-Hermite construction or synthesized correction X arrays.

- Upper plot: amplifier magnitude or gain as samples, correction magnitude or gain as a line, sourced reference geometry as dashed lines.
- Lower plot: amplifier phase as distinctly shaped samples and correction phase as a line.
- Share one normalized input axis and a linked cursor. Both plots inspect the same input position, including when measured and correction arrays have different lengths.
- Numerical inspection interpolates each series in its own actual X domain. It never assumes correction indices equal measured sample indices. No data, an out-of-domain input, non-finite data, or invalid gain denominator produces a blank reading rather than extrapolated success.
- Keep Show Gain, Phase Zoom, Low Res, and On Top, with sourced ranges/stride and existing persistence. Series visibility is a local presentation choice, initially all five visible; it never changes samples or calibration.
- Explicitly distinguish available current/retained data from unavailable data using facts exposed by the existing feed. Do not claim a fresh curve just because a presentation timer fired. Receive mode alone does not clear valid retained coefficients. Disconnection or coordinator replacement must not label stale buffers as current.

Builder call: series visibility and cursor position are session-local; no new saved preference for them. Curves and axes use existing workspace roles with textual/shape distinctions, and the classic fallback remains coherent.

## 7. IMD rail, inspector, markers, and waterfall

### Measurement source and validity

Preserve the existing eligibility predicate: MOX, active two-tone testing, Show Two-tone Measurements, and display duplex. Keep DUP off by default. If the requested measurement cannot run, the rail explains the missing prerequisite; it never enables it automatically.

The FFT/display frame's actual source must be known before labeling it TX feedback. DUP keeps the receiver under the current controller; the presence of MOX alone does not establish that those pixels represent the required feedback path. No client-generated transmit spectrum may be presented as measured PA IMD.

Measure once per newly accepted displayed spectrum frame using its current visible-pixel mapping, frequency context, source eligibility, and sequence identity. Repaints, hover, exposure, tab switches, and GPU overlay refreshes do not count as new measurement frames. Preserve the current display-plane input semantics, including the established TNF visual-dent rule; do not switch to an undented or raw analyzer plane as an incidental refactor.

Statuses are: Disabled, Waiting for TX/two-tone, Duplex required, Waiting for eligible spectrum, Fundamentals unresolved, Products outside view, Products missing, and Valid. Missing gates/frames/products blank the relevant current values immediately. Do not describe a below-noise product as an exact low value, zero, or a limit without a separately sourced detector confidence policy.

### Rail and inspector

The enabled rail is anchored in pan chrome above the spectrum, outside the trace, with status and worst IMD3/IMD5. The Bench inspector opens within that pan; it does not cover peaks or own another measurement instance. Inspector choice and guide visibility are presentation-only.

The six readings expose lower/upper fundamentals, IMD3, IMD5, absolute level, relative level, and RF frequency. Preserve the existing relative reference semantics: individual values use the stronger fundamental; worst IMD summaries use the weaker fundamental against the stronger product of that order. Name the references explicitly, especially for unequal tones. Do not replace them with total two-tone power or add 3 dB.

All-six valid frames feed the existing EMA and full summary calculations. Partial peak detection may show independently matched current peaks and blank missing products, but the full worst-IMD summaries and legacy derived/intercept readout remain invalid until their required inputs are available. Recovery seeds EMA from a new valid frame rather than blending across a missing/changed-context interval. Retain the source smoothing constant; do not change DSP parameters to make the UI look steadier.

The existing intercept fields retain their exact sourced formulas until separately audited; the HTML's illustrative OIP5 calculation/unit is not an instruction to change production math.

### Markers and selection

Fundamentals: amber circles. IMD3: rose diamonds. IMD5: lavender squares. Pair color with names and shapes; use workspace-role transformations/classic fallback. Selected products have readable frequency, absolute/relative level, and measured/expected status. Canvas/painter interaction has keyboard-accessible product controls.

Use measured pixel locations for measured markers and one shared mapping for spectrum, frequency text, and waterfall guides. Expected positions are dashed/hollow only when grounded in a resolved tone pair/current context. Without that basis, show status rather than fabricated positions. Cropped products get an out-of-view explanation; selecting one does not change zoom or tune the radio.

### Waterfall history

Guides are subtle frequency annotations, not independent historical IMD measurements. They are visible only with the measurement display enabled and available frequency context; allow the operator to hide them. Preserve the user's waterfall palette and scaling; do not recolor RF data to match product markers.

Record a lightweight GUI annotation when the actual correction-applied predicate changes during a running test. Attach it to the corresponding accepted waterfall row sequence, not screen Y or a presumed ten-second interval. Do not backfill pre-existing history with invented correction state. It appears only in views showing that same frequency context and recorded rows. Rows and annotations age together; ring-wrap, pause, rewind, resize, reconnect, and retune cannot make it point to another time/context. Only GUI metadata changes; no new Core history format or remote protocol is introduced.

Builder call: keep annotations session-local and bounded by the existing retained waterfall rows. If the current history API cannot align/expire this metadata without changing architecture, implement the rail/guides first and present the event-annotation dependency for approval before adding a new storage subsystem.

### Ownership and persistence

The pan hosting the TX slice owns measurements; focus on another pan must not move them. New, floated, restored, and reconnected pans receive current authoritative state and future updates. Reuse `MoxDisplayController` ownership; no second competing TX-routing controller.

Builder call: default rail view, inspector closed, guides visible when the measurement display is enabled. Persist `PureSignalImdInspectorVisible` and `PureSignalImdGuidesVisible` through the existing per-pan presentation preference mechanism. Do not persist results, synthetic measurements, selected product, or correction events. Existing Show Two-tone Measurements and DUP defaults are retained.

## 8. IMD diagnosis: evidence and remaining uncertainty

### Confirmed source findings

1. **Double visible-range cropping.** In the primary checkout, `updateSpectrumLinear` crops full FFT bins at `SpectrumWidget.cpp:2690` and creates visible `m_renderedPixels`. `drawImdOverlay:6035` then calls `visibleBinRange` on those already visible pixels. The same error remains at `origin/main` `SpectrumWidget.cpp:9552`. Under zoom/CTUN this can drop products and remap markers. A focused regression must reproduce it before repair; this is not proof it is the maintainer's only bench failure.
2. **All-six gating and silent failure.** `ImdOverlay::labelImdProducts` fails the whole readout if any required product is missing. `drawImdOverlay` returns without an explanation. The new partial/status presentation is a deliberate documented departure, while valid-frame formulas remain unchanged.
3. **Paint-driven analysis.** `drawImdOverlay` performs detection and EMA advancement while painting. Measurement progression depends on repaint behavior and renderer invalidation. The presentation refactor makes frame acceptance the one analysis owner.
4. **Legacy active-widget signal binding.** Primary MainWindow binds testing/show state to `activeSpectrumWidget` during setup/late coordinator availability. Newer TX-display ownership differs; revalidate all relevant state binding against current `origin/main` and the actual keyed pan.
5. **Defaults differ from old comments.** The primary header suggests DUP defaults true; newer `MoxDisplayController` and MainWindow use DUP off. A closed gate may be correct behavior rather than failed peak detection.
6. **GPU gate-off invalidation gap.** `setTestingIMD` and `setShowIMDMeasurements` request update without dirtying the static overlay texture, while the IMD active branch only forces refresh when all gates are true. Verify and pin clearing on gate-off while MOX stays on in a real GPU test; do not rely on CPU captures to prove it.

A read-only coordinate calculation gives a concrete regression fixture: at 192 kHz sample rate, 48 kHz visible span, and 800 already rendered pixels, the erroneous second crop keeps indices 300–500. A six-product frame at 100, 220, 340, 460, 580, 700 loses all four IMD products. This demonstrates the coordinate loss; it is not a substitute for a failing SpectrumWidget integration test or radio reproduction.

### Investigation required before claiming repair

Trace the maintainer's symptom from coordinator gates → TX-pan ownership → selected spectrum source → visible pixel plane → peak positions → matching → cached result → CPU/GPU rendering. Capture bounded gate/source/sequence/span/peak-count/status diagnostics. Verify initial settings, late coordinator bind, reconnect, multi-pan focus, DUP changes while keyed, and valid→missing→valid behavior.

Synthetic input must establish the coordinate defect and analysis lifecycle independently of a radio. A live bench check then verifies actual two-tone feedback with DUP and Show Measurements explicitly enabled, a span including both fifth-order products, and sufficient fundamental separation. Ask for the radio/firmware, build revision, screenshot, FFT settings and bounded logs if the symptom remains. Ask for pcaps only if evidence reaches unclear protocol/feedback routing.

No measurement-path fix is accepted solely because a screenshot contains numbers. Compare expected frequencies, independent reference readings where available, and deliberately invalid states. Preserve evidence and unresolved limitations in the PR.

## 9. Acceptance and delivery

- Applet, Bench window, Linked plots, and default rail/optional inspector follow the selected mockups without mockup-only controls or invented production defaults.
- Every control and diagnostic on the implementation baseline remains reachable and retains its authoritative behavior and eligibility. Unsupported mockup-only legacy controls are not revived; existing unavailable controls retain their refusal explanation.
- The double-cropping regression is demonstrated failing, then passing on the implementation baseline. Any additional root cause gets its own evidence and meaningful test.
- IMD analysis advances once per accepted eligible display frame. Missing data/status and CPU/GPU paths agree; invalid/disabled results leave no stale overlay texture.
- Frequencies, marker/guide positions, zoom/CTUN, TX slice ownership, unequal references, reconnect, and retained waterfall annotation behavior are verified.
- Meaningful focused Qt tests run against freshly built targets; follow `docs/development/fast-test-loop.md`. Full-suite/release verification belongs to the final integration owner, not each iterative UI task.
- Render real Qt captures at native/HiDPI and normal/narrow sizes on Mac, Windows, and Linux. Inspect text, controls, tick positions, both renderer paths, classic fallback, disabled states, and On Top reopening.
- Attribution/header/tag checks pass for every touched ported file. No unlicensed new derivative is hidden behind a `no-port-check` marker.
- Live-radio acceptance remains explicitly unverified until the maintainer provides the bench result. No automated hardware TX is part of test execution.

## 10. Builder calls and handoff

Implementation details chosen here: GUI-only cached measurement state; session-local AmpView series/cursor; per-pan inspector/guide preferences; bounded session-local waterfall event annotations; valid-frame-only aggregate EMA; existing DSP/math/ranges/defaults; no new transport or Core processing path.

These choices complete the written design for review; they are not claims that the maintainer explicitly selected every technical detail. Implementation tasks and exact interfaces are in [the companion plan](2026-10-12-puresignal-refresh-plan.md).
