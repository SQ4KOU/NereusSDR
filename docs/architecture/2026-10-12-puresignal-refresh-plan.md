# PureSignal Instruments and IMD Display Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver the approved compact PureSignal applet, Bench window, Linked AmpView plots, and default IMD rail with optional inspector, while diagnosing and repairing the reported measurement failure.

**Architecture:** Keep current Core/DSP/controller ownership and existing Qt rendering. Analyze an eligible visible spectrum frame once on the GUI thread and distribute one cached result to the pan and PureSignal window. Reuse `MoxDisplayController` for TX-host selection and keep waterfall event metadata bounded to existing GUI history.

**Tech Stack:** C++20, Qt6 Widgets/QPainter, existing QRhi overlay path, QtTest, CMake/Ninja, AppSettings.

**Spec:** [2026-10-12-puresignal-refresh-design.md](2026-10-12-puresignal-refresh-design.md), including the saved mockup selections and all builder calls.

## Global Constraints

- C++20 / Qt6; reuse QPainter and the existing spectrum/GPU overlay mechanisms. No new dependency, renderer, FFT engine, DSP instance, thread, or transport format.
- No file under `src/core/` or `src/models/` includes GUI headers (R1).
- All new measurement presentation state lives on the GUI thread. Painting consumes cached state and never advances EMA or performs DSP.
- Preserve WDSP calls, coefficient evaluation, DSP ranges/defaults/scaling, feedback thresholds, attenuation decisions, two-tone frequencies/power, and all existing TX safety checks.
- Opening a window, enabling measurement display, selecting a product, opening the inspector, or toggling guides never keys the radio or changes DUP, FFT parameters, VFO, mode, filter, or radio settings.
- Use `AppSettings`, existing setting keys, and model-sync guards. New presentation keys use PascalCase names and boolean strings `"True"` / `"False"`; do not rename legacy keys as part of this work.
- Preserve classic-look fallback when the workspace styling switch is disabled. Use the established readable-font helpers; essential labels/readings never fall below the 11 logical-pixel floor.
- Keep existing functionality, file eligibility, modeless singleton ownership, hide-on-close, saved geometry, and On Top behavior.
- Preserve upstream headers byte-for-byte, all inline comments/tags, cited constants, and provenance in the same commit as any port. Use current inspected Thetis `v2.10.3.15`; older untouched cites are not silently restamped.
- Read `docs/development/fast-test-loop.md`; build each named test before running it. No automated radio TX and no repeated full-suite builds during UI iteration.

## Review Focus

1. TX slice on a secondary/floated/recreated pan while a different pan has focus: all measurement gates and results must follow the TX-host pan (Task 1, ownership test).
2. Missing fifth-order products, then recovery: valid-looking numbers and GPU pixels must not survive loss; recovery seeds EMA rather than blending stale results (Tasks 1–2, invalidation/recovery tests).
3. Unequal fundamental levels: per-product and worst-IMD references differ by design; no 3 dB or total-power reinterpretation (Task 2, unequal-reference test).
4. Waterfall pause, rewind, ring wrap, retune, and correction changes before any new row: event labels must not imply a different time or frequency context (Task 6, annotation tests).
5. Unequal measured/correction buffer lengths, zero input in gain mode, and unavailable/retained AmpView data: linked readouts must interpolate their own X domain and never invent current data (Task 5, cursor/domain tests).

## Execution baseline and dependency order

The primary checkout is `e342467fb` and contains pre-existing maintainer edits. **Do not implement this plan in that checkout.** Revalidate the current integration base; local `origin/main` was `1d99789ce8f0cbc7d3347f7d2e49a16d9cc4317e` during planning. Reuse a suitable attached worktree or create one through native worktree tools, branch `codex/puresignal-refresh`. Inspect relevant AGENTS instructions in that checkout before writing.

Task order: **1 → 2 → 3**, then **4 and 5**, then **6 → 7**. Tasks 4 and 5 can have separate workers after shared interfaces settle; Tasks 1/2/3/6 share SpectrumWidget/controller ownership and must have one writer. Do not duplicate the IMD investigation in each UI task. Record progress and evidence in the worktree's task-local progress record; report actual model/effort/elapsed time and unknown usage honestly.

Recommended execution: Sol 6.1 high for the IMD/source integration and integrated review, Sol 6.1 medium for bounded applet/window/chart implementation once interfaces are settled. A stronger model is warranted only for a concrete unresolved routing/concurrency problem, not by default. The user has requested documents and plan now; execution and publication are separate steps.

## File map

| File | Responsibility |
| --- | --- |
| Existing `src/gui/ImdOverlay.{h,cpp}` | Sourced peak detection/matching, valid-frame EMA/readout |
| New `src/gui/ImdMeasurementFrame.h` | GUI-only typed frame/status/product/context contract |
| New `src/gui/ImdMeasurementPresenter.{h,cpp}` | One-frame analysis/validity/partial products; cached result; no painting |
| New `src/gui/ImdMeasurementPanel.{h,cpp}` | Rail/inspector/shared numerical readout widget |
| New `src/gui/PureSignalViewStyle.h` | Scoped style-role adapters for these surfaces; no global palette change |
| Existing `src/gui/SpectrumWidget.{h,cpp}` | Accepted-frame integration, marker/guide/event painting, input mapping, texture invalidation |
| Existing `src/gui/MainWindow.{h,cpp}`, `MoxDisplayController.{h,cpp}` | Existing TX-host/gate/source wiring; shared-frame forwarding and current-state seeding |
| Existing `src/gui/applets/PureSignalApplet.{h,cpp}` | Compact approved instruments and existing authoritative actions |
| Existing `src/gui/PsForm.{h,cpp}` | Approved Bench layout, controls/diagnostics, shared IMD tab |
| Existing `src/gui/AmpViewChart.{h,cpp}`, `AmpViewWindow.{h,cpp}` | Linked plots/cursor, current PS3 five-series transform/snapshot/subscription lifecycle |
| New `src/gui/ImdWaterfallAnnotations.{h,cpp}` | Bounded GUI row-sequence/context annotations |
| `CMakeLists.txt`, `tests/CMakeLists.txt` | Register only new sources/tests required by these tasks |
| Attribution/provenance docs | Refresh touched-port mappings and notices; include new derivatives |

## Shared interfaces

Define in `NereusSDR` under `src/gui/`, with no new Core/model include dependency:

```cpp
enum class ImdStatus { Disabled, WaitingForTx, DuplexRequired,
    WaitingForSpectrum, FundamentalsUnresolved, ProductsOutsideView,
    ProductsMissing, Valid };
enum class ImdProduct { FundamentalLower, FundamentalUpper,
    ThirdLower, ThirdUpper, FifthLower, FifthUpper };

struct ImdInputContext {
    quint64 frameSequence{0};
    quint64 contextRevision{0};
    qint64 acceptedAtMs{0};
    double firstSampleHz{0.0};
    double sampleStepHz{0.0};
    bool mox{false};
    bool testing{false};
    bool measurementsEnabled{false};
    bool duplex{false};
    bool sourceEligible{false};
};
struct ImdPeakReading {
    int sampleIndex{0};
    double frequencyHz{0.0};
    float absoluteDbm{0.0f};
    std::optional<float> relativeDbc;
};
struct ImdMeasurementFrame {
    ImdStatus status{ImdStatus::Disabled};
    ImdInputContext context;
    std::array<std::optional<ImdPeakReading>, 6> peaks;
    std::array<std::optional<double>, 6> expectedFrequencyHz;
    std::optional<ImdReadout> summary;
};
```

`peaks` / `expectedFrequencyHz` index follows the `ImdProduct` declaration order, not the HTML's left-to-right order. A relative reading exists only when its measured reference exists. `summary` is populated only for a fully valid fresh context, and keeps existing ImdReadout sign conventions/formulas. Status precedence is Disabled → WaitingForTx → DuplexRequired → WaitingForSpectrum → fundamental resolution → outside/missing products → Valid. Select the pan before applying this status hierarchy.

Frequency mapping is `firstSampleHz + sampleIndex * sampleStepHz`; SpectrumWidget supplies both values from the **same established visible-pixel mapping as its trace/marker positions**, never from a second full-FFT crop. Test both endpoints and a CTUN offset against the actual widget mapping on the execution baseline. Do not guess an analyzer center from the slice VFO.

`ImdMeasurementPresenter` API: `const ImdMeasurementFrame& acceptVisibleSpectrum(const QVector<float>& pixelsDbm, const ImdInputContext& context); const ImdMeasurementFrame& frame() const; void invalidate(ImdStatus status, const ImdInputContext& context);` It owns the existing valid-frame EMA helper. `acceptVisibleSpectrum` on the same sequence/context twice returns the existing frame unchanged. `invalidate` clears current numerics and smoothing eligibility. Register the frame metatype only if a Qt signal connection requires it; no cross-thread producer is added.

`SpectrumWidget` API additions: `const ImdMeasurementFrame& imdMeasurementFrame() const; void setImdInspectorVisible(bool); void setImdGuidesVisible(bool);` signal `void imdMeasurementChanged(const ImdMeasurementFrame& frame);`. Existing gate setters remain the authoritative entry points.

`ImdMeasurementPanel` API: `enum class Presentation { Rail, Inspector, Window }; void setPresentation(Presentation); void setFrame(const ImdMeasurementFrame&); void setSelectedProduct(ImdProduct);` signal `void productSelected(ImdProduct);`. It has no TX/controller setters and no analyzer.

`PsForm` receives frames through `void setImdMeasurementFrame(const ImdMeasurementFrame&);` MainWindow forwards only the actual keyed-pan frame; losing that owner delivers invalid state immediately. Use existing `keyedPanId()` for ownership; `transmitPanId()` is empty in DUP and must not be used. This is GUI-to-GUI state sharing, not new model/Core authority.

### Task 1: Diagnose and pin the actual IMD failures

**Files:** modify existing SpectrumWidget/MainWindow/MoxDisplayController files only where evidence requires; create `tests/tst_imd_spectrum_integration.cpp`, `tests/support/ImdSpectrumTestAccess.h`; extend `tests/tst_mox_display_controller.cpp` and `tests/tst_spectrum_gpu_dynamic_overlay_clear.cpp`; register tests. Save an evidence report alongside this plan as `2026-10-12-puresignal-imd-diagnosis.md`.

**Interfaces:** consumes existing `updateSpectrumLinear(int, const QVector<float>&, double, double)`, gate setters, `MoxDisplayController::keyedPanId()` and current source interfaces and facade state. Produces focused reproductions, repaired coordinate/ownership/invalidation behavior, and the verified context/source mapping Task 2 consumes. A test-only friend/access class may read/render private IMD state; do not add a public analyzer merely for a test.

- [ ] Record execution SHA, relevant existing changes, local/remote configuration, source route and all four gate values. Inspect the source revision, not the older planning line numbers.
- [ ] Write `displayedPixelsAreNotCroppedTwice`: seed an 800-pixel visible display with six peaks at 100/220/340/460/580/700, levels −88/−65/−22/−22/−65/−88 dBm; sample rate 192000, span 48000. Existing code must fail complete measurement because it crops 300–500. Add full-frame `updateSpectrumLinear` and CTUN variants to prove the fixture respects the live pipeline, not just the private seam.
- [ ] Build/run `tst_imd_spectrum_integration`; record the actual failing assertion. If the current base already repaired this, keep the passing regression and document the upstream fix instead of recreating it.
- [ ] Remove only the redundant crop/reinterpretation. Detect across the already-visible array and map markers from that array's coordinates. Preserve trigger 10 dB and valid-frame algorithm.
- [ ] Write host/late-bind tests: TX on secondary pan while another pan is active; create/float/restore a pan; reconnect coordinator with Show Measurements already true; focus changes while keyed. Assert current state and future testing/show/MOX/DUP gates land on the TX-host pan together, with no duplicate connections.
- [ ] Repair GUI binding through the existing owner. Do not create a second TX-source switcher; preserve `MoxDisplayController` local/remote contracts and existing application-wide state consumers.
- [ ] Write GPU gate-off tests: after a valid IMD texture, disable Show Measurements or two-tone while MOX stays on; assert dirty invalidation and cleared IMD pixels. Add CPU invalid/missing state evidence separately.
- [ ] Trace source eligibility and frame arrival under DUP on/off. Do not call local generated TX pixels measured PA feedback. If a required source is unavailable, show that status; obtain approval before any new feedback fork, RX routing, DSP allocation, or protocol change. Other UI tasks can continue from invalid-state fixtures.
- [ ] Build/run the exact modified targets before ctest: `tst_imd_spectrum_integration`, `tst_mox_display_controller`, `tst_spectrum_gpu_dynamic_overlay_clear`, `tst_spectrum_pipeline_modes`, `tst_tx_display_feed`, `tst_remote_tx_display`. CPU offscreen cannot prove a native QRhi texture result; keep GPU evidence explicit rather than treating skips as passes.
- [ ] Record confirmed root causes, rejected hypotheses, remaining hardware/source questions, reproduction commands and results. Commit the focused diagnosis fixes with their regressions separately from visual changes.

### Task 2: Produce one cached, truthful measurement frame

**Files:** new frame/presenter files; modify ImdOverlay/SpectrumWidget; create `tests/tst_imd_measurement_presenter.cpp`; extend `tests/tst_imd_overlay.cpp` and integration test.

**Interfaces:** consumes Task 1's verified visible-pixel/source mapping; produces the frame/presenter/widget APIs above. Keep the existing ImdOverlay methods compatible for existing callers/tests.

- [ ] Write `sameFrameDoesNotAdvanceEma`: accept a valid frame, then a step-changed second sequence, then reaccept that second sequence 20 times. The summary must equal one update with alpha `0.1f`, not 21 updates. Repainting must leave it unchanged.
- [ ] Write `missingProductClearsAggregateAndRecoveryReseeds`: remove fifth upper product; `summary` is empty and that peak is absent, while resolved fundamentals/third-order peaks remain independently inspectable. Restore all six in a new sequence; summary seeds from new raw values. Context/source/retune changes also reset eligibility.
- [ ] Write `unequalToneReferencesStaySourceFaithful`: fundamentals −20/−22 dBm; third products −68/−70. Individual relative third lower is −48 dBc against the stronger tone; signed displayed worst IMD3 is −46 dBc against the weaker tone. Stored ImdReadout conventions remain unchanged. Pin current OIP formulas through existing tests; do not use HTML OIP5 as a new formula.
- [ ] Write gate/condition rows covering Disabled, missing TX/two-tone, DUP off, source unavailable/empty, too-close fundamentals (≤10 display pixels), missing products, and expected products outside current view. No invalid numbers or unsupported expected positions.
- [ ] Implement the exact presenter APIs. Reuse/source partial peak matching using Thetis search geometry and preserve its constants/tags/licenses. Derive partial matches from the actual peak list; never read uninitialized output arguments after the legacy labeler returns false.
- [ ] Integrate acceptance after the display plane is finalized in `updateSpectrumLinear`; preserve current TNF display-plane semantics. Cache result before painting; CPU/GPU render the cache. Gate changes synchronously invalidate cached numeric state and dirty every relevant overlay layer, even when no new FFT frame arrives.
- [ ] Write frequency/pixel agreement tests over full span, quarter span, CTUN offset, resize, endpoints, unequal display widths and retune. Frequency text, selected peak, spectrum symbol and waterfall guide must share the same mapping.
- [ ] Build/run `tst_imd_measurement_presenter`, `tst_imd_overlay`, `tst_imd_spectrum_integration`, `tst_spectrum_pipeline_modes` and relevant TNF regressions. Run header/tag/provenance checks for the changed ported files; commit cached analysis and contracts.

### Task 3: Add the rail and optional inspector to the keyed pan

**Files:** new `ImdMeasurementPanel.{h,cpp}`, `PureSignalViewStyle.h`; modify SpectrumWidget/MainWindow and existing per-pan presentation persistence owner; create `tests/tst_imd_measurement_panel.cpp`.

**Interfaces:** consumes shared frame and widget APIs from Task 2. Produces the approved rail/inspector, selected-product forwarding and presentation preference access; no measurement math.

- [ ] Write `invalidFrameExplainsReasonWithoutNumbers`, `selectionHasSameFrequencyAsFrame`, `panelActionsDoNotTransmit`, and `inspectorPreferenceIsPerPan`. For the action test, spy on existing MOX/two-tone/DUP setters/signals and assert zero changes from selection, guides, inspector, and display enable.
- [ ] Implement rail outside spectrum trace and inspector within the same pan. Render all six rows with absolute/relative levels and RF frequency, expose exact reference labels, and keep existing derived diagnostics accessible. Default inspector closed and guides visible only when enabled/eligible.
- [ ] Use `PureSignalImdInspectorVisible` / `PureSignalImdGuidesVisible` in the baseline's existing per-pan settings mechanism; write bool strings and seed newly created/restored panels. Preserve old show-measurements setting and DUP default.
- [ ] Paint fundamentals as circles, IMD3 diamonds, IMD5 squares with role-based colors; measured locations come from the frame, reliable expected locations are hollow/dashed. Keyboard product controls select the same state as mouse selection. Narrow windows stack/reflow rather than hiding measurements or covering peaks.
- [ ] Share the selected keyed-pan frame with MainWindow's window-forwarding path. When ownership changes or disappears, forward an invalid frame immediately; never use whichever pan was last focused.
- [ ] Capture real Qt rail/inspector at 736 and 320 logical-pixel widths, HiDPI, missing/outside/waiting states, and classic/new look. Compare against saved IMD mockups; no text below 11 px or false peaks. Verify CPU and native GPU markers/guides independently.
- [ ] Build/run panel and integration tests; commit the pan presentation and scoped style adapter.

### Task 4: Refresh the applet and PureSignal Bench window

**Files:** modify PureSignalApplet/PsForm/MainWindow; extend `tests/tst_applet_ps_wiring.cpp`, `tests/tst_psform.cpp`; create `tests/tst_puresignal_refresh_state.cpp`.

**Interfaces:** consumes scoped style helpers, `PureSignalSessionFacade` getters/signals/actions/settings/permissions and `PsForm::setImdMeasurementFrame`. Produces approved compact applet and Bench layout without replacing authority.

- [ ] Write state tests for `(correctionsBeingApplied, isCorrecting)` combinations, feedback classification, reconnect clearing, OFF actual reset, late-bind initial Auto/Two-tone states, and distinct calibration/correction counters, current action-result/native/routing status, raw PS3 fields and remote actuation/arming refusals. Assert HW peak is never captioned distortion improvement.
- [ ] Reshape the existing applet body into correction status, feedback level/meter, accurate counter and existing actions. Preserve existing container header, right-click window route, file eligibility and control wiring; move hardware reference detail to the full window instead of presenting it as correction strength.
- [ ] Reorganize PsForm's retained controls into persistent left instruments, Calibration/IMD tabs, exposed raw diagnostics and file footer. Retain Advanced-collapse API/lifecycle, On Top and saved geometry. No hardcoded mock samples, preview state selectors, or mockup initial keyed state.
- [ ] Preserve current PS3 defaults/ranges and facade permission/eligibility for MOX/CAL waits, AMP delay, Auto/Quick Attenuate, Automatic desired, Run calibration processing, Override hardware peak, GetPk/SetPk/Default, Show Measurements, Start Auto, Apply Current and correction file operations. Preserve raw PS3/native/routing/action-result status. Do not recreate PIN/MAP/STBL/TINT/Relax Tolerance solely because the older mockup showed them. Keep PS-RX/PS-TX spectra disabled where current main marks it unavailable.
- [ ] Bind shared IMD frame to the window tab. Switching the tab or opening the window does not advance EMA or start an FFT. Unknown/raw diagnostic codes remain sourced values; never assign caption-derived codes.
- [ ] Test real model setter echoes with `QSignalBlocker`/existing sync guard, close/reopen/reconnect, raw codes, SetPk editing and coefficient actions. Capture native Qt applet and full Bench window at intended width and narrow/HiDPI/classic states.
- [ ] Build/run the three named targets and `tst_puresignal_coordinator`, `tst_remote_puresignal_arming`, `tst_ps3_settings_persistence` and `tst_ps3_two_tone_completion`; commit applet and Bench presentation. If a target was renamed on the execution baseline, record its verified replacement rather than guessing a command.

### Task 5: Make AmpView a linked two-plot instrument

**Files:** modify AmpViewChart/Window; extend `tests/tst_ampview_window.cpp`; create `tests/tst_ampview_linked_cursor.cpp`.

**Interfaces:** consumes current `PureSignalSessionFacade::displaySnapshotReady(const Ps3Snapshot&)`, `AmpViewWindow::acceptSnapshot(const Ps3Snapshot&)`, `Ps3DisplayAdapter::transform(const Ps3Snapshot&)` and `AmpViewChart::setPlotData(const Ps3PlotData&)`, plus existing toggle APIs. Preserve generation, sequence, captured-time freshness, subscription and displayInvalidated behavior. Add `struct AmpViewCursorReadout { double inputMagnitude; std::optional<double> magnitudeAmp, magnitudeCorrection, phaseAmp, phaseCorrection; }; AmpViewCursorReadout readoutAt(double inputMagnitude) const; void setSelectedInput(double);` Reuse existing `void setSeriesVisible(AmpViewChart::Series, bool)` rather than adding an integer-index API. Enum order is Reference/MeasuredMagnitude/MeasuredPhase/CorrectionMagnitude/CorrectionPhase. Preserve five-series identity and existing range test APIs.

- [ ] Write `cursorInterpolatesEachDomain`: raw X `[0,0.2,0.7,1]`, magnitude correction X `[0,0.35,1]` and phase correction X `[0,0.5,0.9]` supplied by their independent Ps3PlotPoint arrays. At input `0.6`, compare each interpolation independently; raw/magnitude-correction/phase-correction indices and X domains must not be equated or regenerated. Add rejected old-session/duplicate-sequence tests preserving current snapshot acceptance.
- [ ] Write `gainZeroAndInvalidDomainsAreBlank`: zero denominator, non-finite samples, unavailable arrays, mismatched raw sizes, and out-of-domain input produce absent readings for the affected series. Valid other series remain usable.
- [ ] Retain the current facade snapshot feed, `Ps3DisplayAdapter` transforms, freshness and invalidation semantics. No direct WDSP polling or legacy spline reconstruction is introduced in this task. Split QPainter plot rectangles vertically, sharing input selection; independent magnitude/gain and phase scales stay exactly `[0,1]` / `[0,2]` and ±180 / ±45 degrees. Low-res affects plotted sample stride (4/1), not cursor data.
- [ ] Add consistent samples/line/reference distinctions, series visibility controls and linked numeric readout. Keep visibility/cursor session-local; all series initially visible. Data state derives from existing facade snapshot/invalidation evidence; disconnection invalidates current claims and receive mode labels retained data accurately.
- [ ] Extend toggle, close/reopen, On Top, buffer-size, invalid/retained data and interpolation tests. Render Linked plots at normal/narrow/HiDPI sizes and both theme modes; capture sourced ranges and zero input behavior, not the mockup's sample ranges.
- [ ] Build/run `tst_ampview_window`, `tst_ampview_linked_cursor`, `tst_ps3_display_adapter` and `tst_ps3_display_transform`; commit linked plotting with unchanged sourced PS3 transforms.

### Task 6: Align waterfall guides and correction event history

**Files:** new `ImdWaterfallAnnotations.{h,cpp}`; modify SpectrumWidget's accepted-row/history drawing hooks and GUI coordinator event wiring; create `tests/tst_imd_waterfall_annotations.cpp`.

**Interfaces:** `struct ImdWaterfallEvent { quint64 rowSequence; quint64 contextRevision; bool correctionsApplied; };` helper APIs `void record(quint64 rowSequence, quint64 contextRevision, bool correctionsApplied); void expireBefore(quint64 oldestRetainedSequence); void clear(); const QVector<ImdWaterfallEvent>& events() const;`. SpectrumWidget maps retained row sequences to screen Y using its existing history/rewind API. A correction edge queues one event for the next accepted row in the same context; no row means no placed event.

- [ ] Write `eventUsesAcceptedRowNotPaintTime`, `pausedRowsDoNotCreateTimePositions`, `wrapExpiresEvents`, `rewindMapsSameSequence`, `retuneAndReconnectClearContext`, and `preExistingHistoryGetsNoInventedEvents`. Assert retained event count is bounded by retained rows.
- [ ] Identify current local/remote waterfall row sequence/context ownership. Reuse available metadata and GUI history only. If a new transport, Core history format, routing or thread/storage subsystem is required, write the concrete dependency and obtain approval; do not infer authorization from the visual prototype.
- [ ] Paint subtle frequency guides from the shared frame and viewport transform while preserving the user's actual waterfall colors/scaling. Guides denote locations, never retrospective peak measurements. Add a row-aligned correction-applied event marker only for real observed predicate edges during testing.
- [ ] Tie event expiry/reset to the existing ring/history lifecycle. Toggle/retune/stop/reconnect invalidate inappropriate guides/events; paused/rewound display does not consume or move historical metadata.
- [ ] Build/run annotation/presenter/integration tests plus `tst_spectrum_gpu_waterfall_ring_seam`; verify native GPU and CPU event geometry at wrap, pause and rewind. Commit GUI-only annotations with no Core/DSP behavior changes.

### Task 7: Integrated visual, safety, attribution and bench verification

**Files:** final fixes only in task-owned surfaces; complete diagnosis report and development/manual notes; update attribution tables for touched ports/new derivatives.

**Interfaces:** consumes completed Tasks 1–6, real Qt captures and freshly built tests. Produces reviewable branch/PR evidence; no merge or publication implied.

- [ ] Review requirements against all four approved surfaces and the saved alternatives. Record each design requirement's task/test/capture and any explicit unverified hardware/architecture dependency; do not mark unavailable feedback measurement complete because GUI fixture values render.
- [ ] Build the app/headless targets from the same implementation SHA. Build/run named changed tests plus `tst_core_has_no_gui_includes`, `tst_puresignal_caps`, `tst_radio_model_puresignal_run_wiring`, `tst_p1_puresignal_ddc_freq`, `tst_p1_puresignal_run_setter`, `tst_two_tone_controller` and existing TX-display tests selected by actual changes. Do not execute stale test binaries.
- [ ] Run relevant header/cite/inline-comment/provenance verifiers using their supported changed-file options. ImdOverlay/AmpViewChart's existing `no-port-check` wording does not exempt new Thetis-derived logic/constants from verbatim notice/provenance requirements.
- [ ] Produce Mac/Windows/Linux real Qt captures for normal/narrow/HiDPI new and classic styles, both CPU/GPU paths where supported, On Top reopen, disabled/gate-off clearing, partial detection and selected out-of-view products. Do not count HTML or offscreen GPU skips as native-render proof. Use separate build directories and coordinate native test resource ownership.
- [ ] After synthetic tests pass, coordinate a maintainer-run live two-tone check: exact build/local or remote mode, radio/protocol/firmware, actual TX pan, DUP/show-measurements state, span/CTUN/FFT settings, six RF locations, before/after correction behavior and bounded source/gate logs. Keying remains the maintainer's action. Request pcap only if unresolved evidence points to protocol behavior.
- [ ] Arrange one independent integrated review focused on DSP-preserving presentation, feedback source truth, pan ownership, stale GPU pixels, frame cadence, and attribution. Resolve findings; retain hardware limitations honestly.
- [ ] Commit final documentation/captures. Prepare a PR description that explains the confirmed coordinate/invalidation/wiring fixes, sourced algorithm invariants and Nereus presentation departures. Final integration owner performs the required broader suite/build checks once; merging/releasing remains separately authorized.

## Plan self-review and current evidence

The spec maps to Tasks 1–2 (diagnosis/measurement semantics), 3 (rail/inspector), 4 (applet/Bench window), 5 (Linked plots), 6 (waterfall history) and 7 (verification/delivery). Review Focus inputs have tests assigned in their owning tasks. Shared types/signatures are defined above and are not Core transport types. Current PureSignal facade/PS3 snapshots remain the action and AmpView boundaries; the legacy HTML is not a mandate to restore unsupported PS2 controls.

Planning evidence: source audit on `e342467fb` and local `origin/main` `1d99789ce`; read-only double-crop coordinate reproduction; interactive HTML syntax/runtime/layout checks from the brainstorm. **No application repair, Qt regression execution, native GPU proof or live-radio acceptance has occurred during planning.**
