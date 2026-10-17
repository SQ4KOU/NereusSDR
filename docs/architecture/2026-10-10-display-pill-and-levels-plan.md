# Display Pill, Waterfall Levels and Display Fixes Implementation Plan

> **Execution:** run with `crew` (`/crew <this file>`) under `cost-aware-execution`.
> Requirements and acceptance cases are binding; test order and review effort follow
> the risk-based policy; UI evidence follows `ui-verification`. No review between
> tasks; one whole-branch review at the end.

**Goal:** Every spectrum and waterfall control is in the pan's left-strip Display pill
(four tabs), the waterfall levels under Clarity follow the decided Black/Gain model,
key and un-key restore the pre-key look, and the display bugs P1 to P16 of the spec
are fixed, in local windows, remote windows and on the Core.

**Architecture:** Per-pan display values become sparse overrides over Setup's default
keys, and `SpectrumWidget` announces every display change, so the pill, Setup and the
right-click menu can all seed from and follow one source. Level maths, the un-key hold
and the FFT sizing rule live in `src/core/` helpers that the window and the Core
share. The pill talks to its pan's `SpectrumWidget` directly for per-pan rows and to
`MainWindow` by signal for station rows, Clarity, reset and Setup.

**Tech Stack:** C++20, Qt6 Widgets and QRhi (`SpectrumWidget`), `AppSettings`,
`FFTEngine` / `FftEnginePool`, the station link (`DaemonMediaController`,
`DisplayExtrasProcessor`, `RemoteMediaController`), the phone Setup description
(`SetupDescriptionService`, `resources/setup/display.json`, `ios/NereusKit`), Qt Test.

**Spec:** [2026-10-09-display-pill-and-levels-design.md](2026-10-09-display-pill-and-levels-design.md)
(revision 2, approved by JJ 2026-10-10). Every decision is settled there; this is how
it gets built. Mockups: [2026-10-09-display-pill-mockups/](2026-10-09-display-pill-mockups/).
`D`, `S`, `P` and `R-DSP` numbers below are the spec's.

## Global Constraints

- Read `CLAUDE.md`, `CONTRIBUTING.md` and `docs/development/fast-test-loop.md` first.
  C++20/Qt6. No `goto`, no raw `new`/`delete` (`unique_ptr` or a Qt parent), no
  `#define` constants (`constexpr`), braces on all control flow, `kPascalCase`
  constants, `m_camelCase` members, `Q_OS_*` guards, `qCWarning(lcCategory)` for
  errors, no exceptions. Don't remove code you didn't add, except where a task says so.
- `AppSettings`, never `QSettings`. PascalCase keys; booleans are the strings
  `"True"` / `"False"`.
- Rule R1: nothing under `src/core/` or `src/models/` includes a GUI header
  (`tst_core_has_no_gui_includes`).
- Threads: main (GUI and all models), connection, audio, spectrum (FFT). Cross-thread
  traffic is queued signals or a queued `QMetaObject::invokeMethod` only. Never hold a
  mutex in the audio callback. Nothing in this plan touches demodulation or audio; a
  task that finds it must is BLOCKED.
- Upstream pins, never pulled: Thetis `v2.10.3.15` (`3759d096`) at `../Thetis/`;
  AetherSDR `@1e0718ad` at `../AetherSDR/` (its HEAD today). From a worktree the
  siblings are at `/Users/j.j.boyd/Thetis` and `/Users/j.j.boyd/AetherSDR`.
- Ported code (HOW-TO-PORT.md, merge-blocking). Thetis: every ported block and
  constant carries `// From Thetis <file>:<line> [v2.10.3.15]`; constants keep their
  exact value as a named `constexpr`; every upstream `//` comment inside ported logic
  is copied verbatim (author tags above all), moved comments get
  `[original inline comment from file:line]`. AetherSDR has no per-file headers
  (rule 6): each NereusSDR file that takes AetherSDR code carries the AetherSDR
  project-level attribution block (as `src/gui/SpectrumOverlayPanel.cpp:8-21` does), a
  "Modification history (NereusSDR)" line with the date, `J.J. Boyd (KG4VCF)` and
  `AI-assisted transformation via Anthropic Claude Code`, and inline
  `// From AetherSDR <file>:<line> [@1e0718ad]` cites on each ported block and
  constant, in the same commit as the port. New ports add a provenance row in the same
  commit. `scripts/verify-inline-tag-preservation.py`, `scripts/verify-inline-cites.py`,
  `scripts/verify-thetis-headers.py`, `scripts/check-new-ports.py` and
  `scripts/verify-provenance-sync.py` must pass.
- Where NereusSDR differs from Thetis on purpose, the code site says
  "NereusSDR, based on Thetis" with the Thetis cite and one line on why.
- User-facing strings: plain operator words, no em dash, no cites, no file names.
  Use the spec's wording exactly where it gives one.
- Disabled, never hidden: a control that cannot act stays visible, greyed, with its
  reason as a note or tooltip.
- The stale noise-floor dashed lines in `SpectrumWidget::renderGpuFrame`'s overlay
  clear belong to another branch. Do not change that clear
  (`m_overlayStatic.fill(Qt::transparent)` and the dynamic overlay clear).
- New and changed keys are this computer's unless a task says station. The only
  station display keys are `DisplayFftSize`, `DisplayFftWindow`,
  `DisplayHzPerBinTarget`, `DisplaySpectrumFps` and `DisplayGridMax_<band>` /
  `DisplayGridMin_<band>` (`src/core/settings/SettingsScope.cpp:484-501`). Do not bump
  `SettingsSchemaVersion` (`ensureSettingsAtVersion(9)`, `src/core/CoreInit.cpp:171`):
  it is compared between window and Core. One-time conversions use their own marker
  key.
- No station-link message changes meaning and none is added, except the phone Setup
  description version in Task 23. No `pan:<N>` object is put on the station link and
  the Core gets no pan object (Task 9 says why).
- Tests: build and run single named tests, never the whole suite while iterating:
  `cmake --build build --target <tst_name> && ctest --test-dir build -R '^<tst_name>$' --output-on-failure`.
  A test binary run directly is prefixed `QT_QPA_PLATFORM=offscreen`; never export
  that over ctest. Never sleep to wait for state; wait on the signal (`QTRY_*`) or
  inject the clock. New tests are registered with `nereus_add_test(<name>)` in
  `tests/CMakeLists.txt`; `scripts/verify-test-registration.py` must pass. A test that
  asserts today's behaviour and is contradicted by the spec is rewritten to the new
  behaviour and named in the report; no valid test is weakened or deleted to get green.
- No test and no implementer connects to a radio or a Core, launches the app with a
  profile, or touches `~/Applications/NereusSDR Dev.app`.
- Commits: GPG-signed (never `--no-gpg-sign`; a signing failure is reported, not
  worked around). No `Co-Authored-By` trailer. No push, no PR, no public post.
- Line numbers in this plan are from `2a8a09958`. Earlier tasks move them; find the
  code by the names given.

## What already exists

- **Pill:** `SpectrumOverlayPanel` (`src/gui/SpectrumOverlayPanel.{h,cpp}`), built as
  `new SpectrumOverlayPanel(sw)` (`src/gui/MainWindow.cpp:6258`). It holds no
  `SpectrumWidget`; `buildDisplayFlyout()` (`.cpp:563-816`) is one 4-column grid;
  `toggleDisplayFlyout()` (`:818-837`, private) shows it; the Display button is
  `m_menuBtns[4]`. Display signals: `wfColorGainChanged(int)`,
  `wfBlackLevelChanged(int)`, `colorSchemeChanged(int)`,
  `cursorFreqVisibleChanged(bool)`, `gridVisibleChanged(bool)`,
  `fillColorChanged(const QColor&)`, `fillAlphaChanged(float)`,
  `openSetupRequested(const QString&)`, `clarityRetuneRequested()`. Setters:
  `setClarityStatus(bool active, bool paused)`, `setGridVisible(bool)`. Wired by
  `MainWindow::wirePanDisplayFlyout(panel, sw, panId)` (`MainWindow.cpp:6349-6394`).
  `MainWindow::m_overlayPanels` maps pan id to panel. The panel's `wheelEvent`
  (`:1294`) covers the strip only; the flyout is a sibling under the spectrum widget.
- **Widget:** `SpectrumWidget` (`src/gui/SpectrumWidget.{h,cpp}`) owns per-pan display
  state. Per-pan keys: file-static `settingsKey(base, panIndex)` (`.cpp:711-717`; pan
  0 is the bare key, pan N is `base_N`) and the `rawValue` fallback to the bare key
  (`:757-761`). `loadSettings()` (public), `saveSettings()` (`:1217-1430`, writes
  every per-pan key), `scheduleSettingsSave()` (`:1442`, 500 ms). Its
  `DisplaySettingsModel` (`src/models/DisplaySettingsModel.{h,cpp}`, `settingsKeyFor`
  at `.cpp:35`, fallback `:219-223`, `load()`, `save()` writing 14 keys at `:290-309`)
  holds scheme, Black, Gain, ref level, dynamic range, fill alpha, pan fill, spectrum
  fraction and the seven 3D values, each with a `...Changed` signal. Most other
  display setters on the widget have no change signal.
- **Levels:** `SpectrumWidget::dbmToRgb` applies
  `effectiveLow = activeLow + (125 - black) * 0.4`, `effectiveHigh = activeHigh - gain * 0.3`
  (`.cpp:7576-7577`), with a second copy in `traceWaterfallState` (`:7156-7159`).
  `pushWaterfallRow` (`:7043`) adds `displayCalOffsetDb()` (`:6188-6203`) and calls
  `composeWaterfallActiveThresholds` (`:6980-7031`), which runs `m_wfLevels`
  (`WaterfallLevelFollower`, `src/core/spectrum/DisplayFollowers.{h,cpp}`).
  `m_noiseFloor` is the pan's `NoiseFloorFollower`. Test hooks: `dbmToRgbForTest`,
  `pushWaterfallRowForTest`, `liveWaterfallForTest`, `wfActiveLowThreshold()`,
  `wfActiveHighThreshold()`.
- **Clarity:** `ClarityController` (`src/core/ClarityController.{h,cpp}`):
  `feedBins(const QVector<float>&, qint64 nowMs = -1)`,
  `feedNoiseFloor(float, qint64 nowMs = -1)`, `setTransmitting(bool)`,
  `snapToFloor(float)`, `retuneNow()`, `smoothedFloor()`; signals
  `waterfallThresholdsChanged(float low, float high)`, `noiseFloorChanged(float)`,
  `pausedChanged(bool)`. `PanClarityRegistry` (`src/gui/PanClarityRegistry.{h,cpp}`)
  owns one controller per pan:
  `registerPan(id, SpectrumWidget*, SpectrumOverlayPanel*, PanadapterModel* saved = nullptr)`,
  `bindLocal`, `bindRemote`, `feedRemoteFloor`, `setKeyed(bool)`, `retunePan`,
  `setLocalKeyedQueryForTest`;
  `SourceAssociation{streamIndex, streamEpoch, centreHz, sampleRateHz}`.
  `RadioModel::clarityEnabled()` / `setClarityEnabled(bool)` /
  `clarityEnabledChanged(bool)`.
- **Per-band store:** `PanadapterModel` (`src/models/PanadapterModel.{h,cpp}`):
  `setCenterFrequency(double)` derives `band()`; `perBandGrid(Band)`,
  `setPerBandDbMax(Band, int)`, `setPerBandDbMin(Band, int)`,
  `bandNFEstimate(Band)` / `setBandNFEstimate`, `dss3DFloorDepthForBand(Band)` /
  `setDss3DFloorDepthForBand`, `applyStationGridSetting(const QString& key)`,
  `setFollowsBandGrid(bool)`; signals `levelChanged()`, `bandChanged(Band)`. Its
  keys are global per band (`DisplayGridMax_<band>`, `DisplayGridMin_<band>`,
  `DisplayBandNFEstimate_<band>`, `ClarityFloor_<band>`, `Display3DFloorDepth_<band>`).
  **No production code creates one:** `RadioModel::addPanadapter()` has no caller
  outside tests, so `RadioModel::panadapters()` is empty in the app and in `nereusd`,
  and every reader of it is dormant (`MainWindow.cpp:6203`, `:6266`, `:8647`,
  `:14446`; `RxApplet.cpp:259`; `DisplaySetupPages.cpp:2118`; `RadioModel.cpp:10436`,
  `:23107`, `:23126`; `StationClient.cpp:3927`; `StationServer.cpp:7636`).
- **Key edges:** `MoxController::moxStateChanged(bool)` is the display edge.
  `MoxDisplayController` (`src/gui/MoxDisplayController.{h,cpp}`):
  `setKeyed(bool, int txSliceId)` emits `keyedChanged(bool)` (`:215`) before
  `rise()` / `fall()`; `beginTransmitView` / `endTransmitView` hold today's
  `setClarityActive(false)` (`:413`) and `resetWaterfallAgc()` (`:428`, `:528`).
- **FFT:** `FFTEngine` (`src/core/FFTEngine.{h,cpp}`): `fftSize()`, `setFftSize(int)`,
  `fftSizeBaseline()`, `setFftSizeBaseline(int)`, `decimation()`, `sampleRate()`,
  slot `resetInputHistory()` (must run on the engine's thread).
  `FftEnginePool` (`src/core/spectrum/FftEnginePool.{h,cpp}`): `setConfig`,
  `setConfigForNewStreams`, `config()`, `setDecimation(int)`, `engineForStream(int)`,
  `streams()`, `sources()`; `FftPoolConfig{fps, fftSize, windowType, hzPerBinTarget, threadCount}`.
  `ControlRanges::autoZoomMaxFftSize(double)` and
  `autoZoomFftSize(int desiredPow2, int baseline, double sampleRateHz)`
  (`src/core/ControlRanges.h:262-294`). The local auto-zoom lambda
  (`MainWindow.cpp:8958-9009`) is connected to one widget and stream 0's engine.
  The remote planner is `plannedFftSize` (`src/gui/RemoteMediaController.cpp:604-632`).
- **Core:** `DaemonMediaController` (`src/core/session/media/`), its
  `DaemonSpectrumSource m_source`, and `DisplayExtrasProcessor`
  (`DisplayExtras.{h,cpp}`, built per allocation at `DaemonMediaController.cpp:3258`,
  fed `DisplayExtrasInputs` with `mox` from `transmitModel().isMox()` at `:5080`).
- **Setup:** `src/gui/setup/DisplaySetupPages.{h,cpp}`; pages read once from
  `model()->spectrumWidget()` (the active pan); station gating through
  `SetupPage::gateStationControls(list, available, reason)`; remote window is
  `!model()->ownsLocalDsp()`. Page names: `src/gui/SetupDialog.cpp:1756-1795`.
- **Settings:** `AppSettings::remove(key)`, `contains(key)`, `allKeys()`
  (`src/core/AppSettings.h:167-182`). GUI startup calls
  `CoreInit::initialize(activeProfile)` at `src/main.cpp:295`, before any window.
- **Pan lifecycle in the window:** `PanadapterStack::addPanadapter(panId)` emits
  `countChanged`, handled by `MainWindow::ensureOverlayPanels()` (`MainWindow.cpp:6180`);
  `PanadapterStack::removePanadapter` emits `panRetired`, handled at
  `MainWindow.cpp:7112`. Pan ids are "pan-N" and sparse.
- **Widgets:** `GuardedSlider` (`src/gui/widgets/GuardedSlider.h`).
  `wfSchemeName()` and `WfColorScheme::Count` (`src/core/spectrum/WaterfallPalettes.{h,cpp}`).

## File Structure

Create:

| Path | Responsibility |
| --- | --- |
| `src/core/settings/PanDisplayKeys.{h,cpp}` | Per-pan override keys over Setup default keys: read, sparse write, clear (Task 1) |
| `src/core/spectrum/ClarityLevels.h` | Black/Gain level maths under Clarity and the stored-value conversion (Task 3) |
| `src/core/spectrum/MoxDisplayHold.h` | The un-key hold length (Task 11) |
| `src/gui/PanModels.{h,cpp}` | One `PanadapterModel` per window pan, by pan id, and its band-grid wiring (Task 9) |
| `src/gui/DisplayPillWiring.{h,cpp}` | Connects a pill to its pan and to the window's hooks (Task 18) |
| `docs/attribution/AETHERSDR-PROVENANCE.md` | Provenance rows for AetherSDR ports (Task 4) |
| `tests/tst_pan_display_keys.cpp` | Task 1 |
| `tests/tst_spectrum_widget_display_signals.cpp` | Task 2 |
| `tests/tst_clarity_levels.cpp` | Task 3 |
| `tests/tst_spectrum_widget_additions.cpp` | Tasks 4 and 5 |
| `tests/tst_spectrum_widget_cal_offset.cpp` | Task 8 |
| `tests/tst_pan_models.cpp` | Task 9 |
| `tests/tst_spectrum_widget_keyed_levels.cpp` | Task 13 |
| `tests/tst_display_pill_wiring.cpp` | Task 18 |

Modify (main files; each task lists its own):

| Path | Change |
| --- | --- |
| `src/gui/SpectrumWidget.{h,cpp}` | Sparse save, change signal, level model, additions, cal offset terms, keyed AGC |
| `src/models/DisplaySettingsModel.{h,cpp}` | Sparse save, Black/Gain range and defaults |
| `src/gui/SpectrumOverlayPanel.{h,cpp}` | The four-tab pill |
| `src/gui/SpectrumOverlayMenu.{h,cpp}` | Trimmed right-click menu |
| `src/gui/MainWindow.{h,cpp}` | Pill wiring, per-pan models, per-pan replan, key-edge fan-out |
| `src/gui/setup/DisplaySetupPages.{h,cpp}`, `AppearanceSetupPages.cpp` | Defaults, every pan, live follow, greying, FFT slider |
| `src/core/ClarityController.{h,cpp}`, `src/gui/PanClarityRegistry.{h,cpp}` | Keep, hold, restore, slew, calibration |
| `src/core/spectrum/DisplayFollowers.{h,cpp}` | AGC low edge only, NF-AGC, hold, keyed freeze |
| `src/gui/MoxDisplayController.{h,cpp}` | No Clarity clear, no AGC re-prime |
| `src/core/spectrum/FftEnginePool.{h,cpp}`, `src/core/ControlRanges.h` | Fan-out setters, purge, sizing rule |
| `src/core/session/media/DaemonMediaController.cpp`, `DaemonSpectrumSource.{h,cpp}`, `DisplayExtras.{h,cpp}` | Core key edges |
| `src/gui/RemoteMediaController.cpp` | Planner rule, hold length |
| `src/gui/meters/MeterPoller.cpp` | Flag meters without an S-meter |
| `src/core/setup/SetupDescriptionService.cpp`, `resources/setup/display.json`, `ios/NereusKit/Sources/NereusMirror/` | Phone Setup description gates |
| `docs/architecture/*.md` (spec section 9) | Superseded notes |

## Task order and flags

Tasks run in number order, one implementer at a time. Tasks 13 and 14 clear FFT input
on the receive display path on every key edge (CLAUDE.md flag: core RX display path).
JJ was asked on 2026-10-10 whether they need their own early review; with no answer
that night the stated default stands: the whole-branch review at the end only.

Calls settled in this plan, for JJ's veto (none changes a spec decision):

1. Setup's default for a per-pan value is the existing bare key, and every pan,
   pan 0 included, keeps its own changes under `<key>_<N>`. Existing pan 0 values
   become the Setup defaults with nothing copied.
2. A pan saves only the values that differ from Setup's default (today the first
   touch saves all of them).
3. The Black/Gain conversion, the Cal Offset clean-up and the stored-floor clean-up
   each run once from the window's startup under their own marker key; the settings
   schema number does not move.
4. Stored Cal Offset values are cleared once: until now that key held the station's
   offset, never a user value.
5. Stored per-band Clarity floors are cleared once, because they were uncalibrated.
6. Two pans on one receiver stream: the pan that needs the larger FFT wins.
7. A remote pan's un-key hold uses the decimation this window applied (1 when the
   Core does not offer the control); nothing is added to the link.
8. Each window pan gets its own per-band model, owned by the window and kept off the
   station link; the Core gets none. The dormant readers of `RadioModel::panadapters()`
   stay dormant.
9. The phone's Setup description goes to version 26 for Display, with a combined
   "all of" gate. The phone's Setup has no Black or Gain rows, so that part of S20
   has nothing to grey.
10. NF-AGC keeps its stored sign for the offset (added), so saved values, the phone
    and older windows and Cores keep their meaning.
11. Waterfall scheme names stay as `wfSchemeName()` gives them ("BlackWhite").
12. The GPU trace line stays 1 px wide; line width acts on the CPU path as today.

Bench checks stay pending until JJ runs them (spec 11.3): live un-key local and remote
at a small and a large FFT size, with fast two-tone and TUNE; the Rock's `mlock` at
131072 and 262144; the remote waterfall that sometimes turns blue; the S-meter Sig Avg
check.

## Task 1: Per-pan overrides over Setup default keys

**Requirements:** S4, D5 (storage half), R-DSP-09 (storage half), spec section 12
"Per-pan key fallbacks".

**Files:**
- Create: `src/core/settings/PanDisplayKeys.h`, `src/core/settings/PanDisplayKeys.cpp`
- Modify: `src/gui/SpectrumWidget.{h,cpp}` (`loadSettings`, `saveSettings`, a live
  reset), `src/models/DisplaySettingsModel.{h,cpp}` (`load`, `save`), `CMakeLists.txt`
  source list
- Test: `tests/tst_pan_display_keys.cpp` (new), `tests/tst_pan_display_settings_inherit.cpp`,
  `tests/tst_display_settings_model.cpp`, `tests/tst_display_settings_golden.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces, in namespace `NereusSDR::PanDisplayKeys`:
  - `QString defaultKey(QStringView base)`: the Setup default key. It is the bare key
    (`base`), so every existing install's pan 0 values become the Setup defaults with
    no copy.
  - `QString panKey(QStringView base, int panIndex)`: the pan's own override key,
    `base_N` for every pan, pan 0 included (`base_0`).
  - `bool hasOverride(QStringView base, int panIndex)`.
  - `QVariant read(QStringView base, int panIndex, const QVariant& fallback)`: the
    pan key when present, else the default key, else `fallback`.
  - `void writeSparse(QStringView base, int panIndex, const QVariant& value, const QVariant& fallback)`:
    writes the pan key when it already exists or when `value` differs from what the
    pan would inherit (`read` of the default key, else `fallback`); otherwise writes
    nothing.
  - `void writeDefault(QStringView base, const QVariant& value)`.
  - `void clearPan(int panIndex, const QStringList& bases)`.
- Produces, on `SpectrumWidget`:
  - `QStringList perPanDisplayKeyBases() const`: every per-pan display key base this
    widget and its `DisplaySettingsModel` persist (the list `saveSettings` and
    `DisplaySettingsModel::save` write today).
  - `void resetPanDisplayOverrides()`: cancels a pending save, calls
    `PanDisplayKeys::clearPan(panIndex, perPanDisplayKeyBases())`, reloads the widget
    and its `DisplaySettingsModel` from settings on the live widget (members, caches,
    palette, repaint), and emits `panDisplayReset()`.
  - signal `void panDisplayReset()`.

**Acceptance:**
- With bare `DisplayWfOpacity = 80` and no `DisplayWfOpacity_0`, pan 0 and pan 2 both
  load 80. After pan 2's setter is called with 60 and the save runs, only
  `DisplayWfOpacity_2` is written; no other `_2` key appears.
- A pan that already has `DisplayWfOpacity_2` and is set back to the inherited value
  keeps the key, now holding that value.
- Pan 0 changing a value writes `<base>_0`, never the bare key. The bare key changes
  only through `writeDefault`.
- An install with old-style data (bare keys for pan 0, a full set of `_1` keys for pan
  1) loads exactly the values it loaded before this task, for both pans.
- `resetPanDisplayOverrides()` on pan 1 removes every `_1` key in
  `perPanDisplayKeyBases()`, the widget then reports the inherited values through its
  getters without a restart, `panDisplayReset()` fires once, and `DisplayFftSize`,
  `DisplayFftWindow`, `DisplayHzPerBinTarget`, `DisplaySpectrumFps`,
  `DisplayGridMax_20m` and every global key are unchanged.
- A save scheduled before the reset does not rewrite the removed keys.
- `DisplayWaterfallHistoryMs` and the TX waterfall palette keys read at
  `SpectrumWidget.cpp:942-980` go through the same helper if they are per-pan; if they
  are global they are left alone and the report says which.
- A key base that could be read as a station key once it carries a pan suffix (the
  per-pan `DisplayGridMax_<N>` against the station prefix `DisplayGridMax_`) is named
  in the report; Task 9 retires those two per-pan keys.
- `tst_core_has_no_gui_includes` passes.

**Verification:** ordinary feature with a settings-compatibility risk: write the
old-install loading test first and see it pass on the unchanged code, then change the
code. Unit tests on the helper; widget tests offscreen. Commands: build and run
`tst_pan_display_keys`, `tst_pan_display_settings_inherit`, `tst_display_settings_model`,
`tst_display_settings_golden`, `tst_core_has_no_gui_includes`, then build `NereusSDR`.

**Execution note (advisory):** opus. No prerequisites. Touches the settings every
user's display loads from; the old-install test is the guard.

- [ ] **Step 1:** Write `tst_pan_display_keys` for the helper's contract above, and
  add the old-install loading case to `tst_pan_display_settings_inherit` before
  changing the widget.
- [ ] **Step 2:** Add `PanDisplayKeys`. Replace the file-static `settingsKey` /
  `rawValue` reads in `SpectrumWidget::loadSettings` and `settingsKeyFor` reads in
  `DisplaySettingsModel::load` with `PanDisplayKeys::read`, and every per-pan write in
  `saveSettings` and `DisplaySettingsModel::save` with `writeSparse`, passing the same
  fallback default the load uses for that key.
- [ ] **Step 3:** Add `perPanDisplayKeyBases()`, `resetPanDisplayOverrides()` and
  `panDisplayReset()`. The reset must leave the widget drawing with the reloaded
  values: rebuild what `loadSettings` assumes is not yet built (palette, overlay
  caches, averaging state as the matching setters do).
- [ ] **Step 4:** Update the golden and model tests that pin written key names to the
  new sparse behaviour; name each in the report.

## Task 2: Announce every display change

**Requirements:** D2 and D27 (the signals they need), R-DSP-06, R-DSP-36.

**Files:**
- Modify: `src/gui/SpectrumWidget.{h,cpp}`
- Test: `tests/tst_spectrum_widget_display_signals.cpp` (new)

**Interfaces:**
- Consumes: nothing.
- Produces, on `SpectrumWidget`:
  - signal `void displaySettingChanged(const QString& keyBase)`: emitted once, after
    the value has changed, by every per-pan or global display setter that has no
    change signal of its own. `keyBase` is the setting's key base as persisted
    (for example `DisplayGridEnabled`, `DisplayFillColor`, `DisplayWfAgc`,
    `WaterfallNFAGCEnabled`, `DisplayShowNoiseFloor`). Not emitted when the value is
    unchanged, and not emitted during `loadSettings()`.
  - signal `void clarityActiveChanged(bool active)`; `setClarityActive(bool)` gains a
    same-value guard.
  - The setters covered are, at least: `setFillColor`, `setGridEnabled`,
    `setCursorFreqVisible`, `setPeakHoldDelayMs`, `setGradientEnabled`,
    `setShowNoiseFloor`, `setShowBinWidth`, `setShowPeakValueOverlay`, `setLineWidth`,
    `setSpectrumDetector`, `setSpectrumAveraging`, `setSpectrumAverageTimeMs`,
    `setDbmCalOffset`, `setWfAgcEnabled`, `setWaterfallNFAGCEnabled`,
    `setWaterfallAGCOffsetDb`, `setWfLowThreshold`, `setWfHighThreshold`,
    `setWfUseSpectrumMinMax`, `setWfUpdatePeriodMs`, `setWaterfallAveraging`,
    `setWaterfallAverageTimeMs`, `setWfOpacity`, `setWaterfallStopOnTx`,
    `setShowRxFilterOnWaterfall`, `setShowTxFilterOnRxWaterfall`,
    `setShowRxZeroLineOnWaterfall`, `setShowTxZeroLineOnWaterfall`,
    `setWfTimestampPosition`, `setWaterfallHistoryMs`, `setDbmRange`.
    `setPeakHoldEnabled` keeps `peakHoldEnabledChanged(bool)`.

**Acceptance:**
- For each setter listed, a `QSignalSpy` on `displaySettingChanged` sees exactly one
  emission with that setting's key base when the value changes, and none when the same
  value is set again.
- `loadSettings()` on a widget with non-default stored values emits nothing.
- `setClarityActive(true)` twice emits `clarityActiveChanged(true)` once.
- A global setter that fans out to other pans (`shareWithOtherPans`, for example
  `setShowNoiseFloor`) makes each pan emit once and does not loop.
- `m_showTxFilterOnRxWaterfall`'s in-class initialiser matches its load default
  (`true`, `SpectrumWidget.cpp:998`); a test constructs a widget with cleared settings
  and checks the getter.

**Verification:** ordinary feature; unit test offscreen. Build and run
`tst_spectrum_widget_display_signals`; build `NereusSDR`.

**Execution note (advisory):** opus. After Task 1 (same file). No risk flag.

- [ ] **Step 1:** Add the two signals and the emissions, each after the existing
  same-value guard (add a guard where a listed setter has none).
- [ ] **Step 2:** Write the data-driven test over the setter list.

## Task 3: Waterfall levels under Clarity, and the stored-value conversion

**Requirements:** D9 to D12, D13a (rendering half), D16, S1, S2, S3; R-DSP-13,
R-DSP-14, R-DSP-15, R-DSP-16, R-DSP-17, R-DSP-51 (rendering half); spec sections 4.2
to 4.5.

**Files:**
- Create: `src/core/spectrum/ClarityLevels.h`
- Modify: `src/gui/SpectrumWidget.{h,cpp}` (`dbmToRgb`, `traceWaterfallState`, a
  Clarity floor input), `src/models/DisplaySettingsModel.{h,cpp}` (Black range and
  both defaults), `src/gui/PanClarityRegistry.cpp` (pass the floor),
  `src/gui/applets/DisplayApplet.cpp` (range, tooltip, scheme names),
  `src/main.cpp` (run the conversion), `src/core/AppSettings.{h,cpp}` (the conversion
  function, beside the other one-shot migrations)
- Test: `tests/tst_clarity_levels.cpp` (new), `tests/tst_display_settings_model.cpp`,
  `tests/tst_display_settings_golden.cpp`, `tests/tst_display_applet.cpp`

**Interfaces:**
- Consumes: `PanDisplayKeys` (Task 1).
- Produces, in `src/core/spectrum/ClarityLevels.h`, namespace `NereusSDR::ClarityLevels`:
  - `inline constexpr int kBlackLevelMin = 0;`, `kBlackLevelMax = 100;`,
    `kBlackLevelAtNoise = 50;`, `kBlackLevelDefault = 57;`
  - `inline constexpr float kBlackLevelDbPerStep = 0.5f;`
    (`// From AetherSDR src/gui/SpectrumWidget.cpp:11928 [@1e0718ad]`, S1)
  - `inline constexpr int kColorGainDefault = 60;`
  - `inline constexpr float kColorSpanAtZeroGainDb = 80.0f;`,
    `kColorSpanDbPerGainStep = 0.7f;`
    (`// From AetherSDR src/gui/SpectrumWidget.cpp:11593 [@1e0718ad]`:
    `visRange = 80.0f - m_wfColorGain * 0.7f; // 80 dB down to 10 dB`, comment kept verbatim)
  - `constexpr float blackPointDbm(float noiseFloorDbm, int blackLevel) noexcept`:
    `noiseFloorDbm + (blackLevel - kBlackLevelAtNoise) * kBlackLevelDbPerStep`.
  - `constexpr float colorSpanDb(int colorGain) noexcept`:
    `kColorSpanAtZeroGainDb - colorGain * kColorSpanDbPerGainStep`.
  - `int convertLegacyBlackLevel(int oldBlack) noexcept` and
    `int convertLegacyColorGain(int oldGain, int oldBlack) noexcept`, from spec 4.3:
    offset dB = `-5 + 0.4 * (125 - oldBlack)`, new Black =
    `round(50 + offset / 0.5)` clamped 0 to 100; span dB =
    `60 - 0.3 * oldGain - 0.4 * (125 - oldBlack)`, new Gain =
    `round((80 - span) / 0.7)` clamped 0 to 100.
- Produces, on `SpectrumWidget`: `void setClarityFloorDbm(float noiseFloorDbm)` and
  `float clarityFloorDbm() const`. Task 10 and Task 12 replace what feeds it; the
  setter's name and meaning do not change.
- Produces, on `AppSettings`:
  `static void convertWaterfallLevelsForClarityModel(AppSettings& settings)`, guarded
  by the marker key `DisplayWfClarityLevelsConverted` (`"True"` once done).
- `DisplaySettingsModel::setWfBlackLevel` clamps 0 to 100; defaults Black 57, Gain 60.

**Acceptance:**
- `blackPointDbm(-120, 50) == -120`; `blackPointDbm(-120, 57) == -116.5`;
  `colorSpanDb(0) == 80`; `colorSpanDb(100) == 10`; `colorSpanDb(60) == 38`.
- `convertLegacyBlackLevel(104) == 57`; `convertLegacyColorGain(45, 104) == 60`.
  `convertLegacyBlackLevel(125) == 40` (N - 5 dB); `convertLegacyBlackLevel(0) == 100`
  (clamped); `convertLegacyColorGain(100, 125) == 71`.
- For every old pair (Black 0 to 125 step 5, Gain 0 to 100 step 10) whose results are
  not clamped, the new black point and the new top (black point plus span) are within
  0.5 dB of the old formula's `N - 5 + 0.4 * (125 - b)` and `N + 55 - 0.3 * g`.
- Renderer, Clarity active, floor -120 dBm, Black 57, Gain 60: the effective low is
  -116.5 dBm and the effective high is -78.5 dBm (through `dbmToRgbForTest` at the two
  ends and one mid point, or an exposed effective-levels accessor for tests).
- Renderer, Clarity active: raising Black raises the black point.
- Renderer, Clarity not active, Low -122, High -62, AGC and NF-AGC off: the effective
  levels are exactly -122 and -62 whatever Black and Gain hold.
- The transmit branch (`effectiveLow = lowThreshold` under the MOX overlay) is
  unchanged; `tst_spectrum_widget_mox_overlay` still passes.
- `traceWaterfallState` reports the same effective pair `dbmToRgb` uses (one shared
  function, not two copies of the formula).
- Conversion: a settings file with `DisplayWfBlackLevel=104`, `DisplayWfColorGain=45`,
  `DisplayWfBlackLevel_2=80`, `DisplayWfColorGain_2=20` converts to 57, 60, and the
  values the two functions give for (80, 20); the marker is set; a second run changes
  nothing; a file with no such keys gets only the marker.
- The conversion runs in the GUI binary after `CoreInit::initialize` and before the
  first window is built; `nereusd` (`src/server_main.cpp`) never calls it.
- `SettingsSchemaVersion` is unchanged.
- Applet: the Black slider is 0 to 100 and its tooltip says that up is blacker; the
  applet's scheme list is the eight `wfSchemeName()` entries.

**Verification:** consequential state transition (every user's saved look): the
conversion tests and the old-formula equivalence sweep are written first and fail on
main. Unit tests on `ClarityLevels` and the conversion; renderer tests offscreen.
Build and run `tst_clarity_levels`, `tst_display_settings_model`,
`tst_display_settings_golden`, `tst_display_applet`, `tst_spectrum_widget_mox_overlay`,
`tst_pan_clarity_registry`; build `NereusSDR` and `nereusd`.

**Execution note (advisory):** opus. After Tasks 1 and 2. A wrong formula or a double
run changes every user's waterfall on upgrade (spec section 12): candidate for the
reviewer's closest look.

- [ ] **Step 1:** `ClarityLevels.h` with the constants, cites and functions; tests.
- [ ] **Step 2:** One private `SpectrumWidget` function computes the effective low and
  high: under the MOX overlay, today's TX branch; with Clarity active,
  `blackPointDbm(m_clarityFloorDbm, black)` and that plus `colorSpanDb(gain)`;
  otherwise the active low and high with no Black/Gain offset. `dbmToRgb` and
  `traceWaterfallState` both call it. The comment states the meaning (up = blacker),
  cites AetherSDR `SpectrumWidget.cpp:11588-11593 [@1e0718ad]`, and says why ours
  differs from AetherSDR's inverted formula (spec 4.4).
- [ ] **Step 3:** `PanClarityRegistry` calls `setClarityFloorDbm(controller->smoothedFloor())`
  wherever it applies Clarity thresholds to a widget (`PanClarityRegistry.cpp:201-203`)
  and on `snapToFloor`.
- [ ] **Step 4:** The conversion in `AppSettings`, walking `allKeys()` for
  `DisplayWfBlackLevel`, `DisplayWfColorGain` and their `_N` forms, converting Gain
  with the pan's own old Black (the inherited bare value when the pan has none), then
  setting the marker; called from `src/main.cpp` only.
- [ ] **Step 5:** Model clamp and defaults, applet range, tooltip and scheme names;
  update the tests that pin 104, 45 and 0 to 125.

## Task 4: NB Blank, grid spacing and scale text (AetherSDR ports)

**Requirements:** D30 (additions 1, 3, 4), D31, D32, S5, S11; R-DSP-39, R-DSP-41,
R-DSP-42, R-DSP-44; spec 3.7.

**Files:**
- Create: `docs/attribution/AETHERSDR-PROVENANCE.md` (in the style of
  `docs/attribution/AETHER-TELEMETRY-PROVENANCE.md`)
- Modify: `src/gui/SpectrumWidget.{h,cpp}`
- Test: `tests/tst_spectrum_widget_additions.cpp` (new)

**Interfaces:**
- Consumes: `PanDisplayKeys` (Task 1), `displaySettingChanged` (Task 2).
- Produces, on `SpectrumWidget`, each per pan, saved through `PanDisplayKeys`, each
  emitting `displaySettingChanged(<key base>)`:
  - `bool waterfallBlankingEnabled() const`, `void setWaterfallBlankingEnabled(bool)`;
    key `WaterfallBlankingEnabled`, default off.
  - `int waterfallBlankingThreshold() const`, `void setWaterfallBlankingThreshold(int)`:
    5 to 95, meaning a factor of `1.00 + v / 100`; key `WaterfallBlankingThreshold`,
    default 15.
  - `int freqGridSpacingHz() const`, `void setFreqGridSpacingHz(int)`: 0 is Auto; the
    allowed fixed values are 1000, 2000, 5000, 10000, 25000, 50000, 100000; key
    `DisplayFreqGridSpacing`, default 0.
  - `int freqScaleFontPx() const`, `void setFreqScaleFontPx(int)`: 8 to 14; key
    `DisplayFreqScaleFontPx`, default 10.

**Acceptance:**
- NB Blank off: `pushWaterfallRowForTest` rows pass through unchanged.
- NB Blank on at 15: after 10 rows at a steady -110 dBm, a row whose linear-power mean
  is 1.2 times the baseline's (about +0.8 dB) is replaced by the last good row; a row
  at 1.1 times is kept. The comparison is on linear power (S5).
- NB Blank is frozen during transmit (`m_moxOverlay` or `m_txActiveForTest`): rows
  pushed then neither update the baseline nor get replaced.
- The blanker sits in `pushWaterfallRow` after calibration and before
  `composeWaterfallActiveThresholds`; `pushDssRow` and `pushTxWaterfallRow` are not
  blanked.
- Grid spacing Auto: the grid lines and scale labels are exactly today's for spans of
  3 kHz, 48 kHz, 100 kHz and 384 kHz (pin today's steps in the test before changing
  the code). A fixed 5 kHz gives 5 kHz on both the grid (`drawGrid`) and the scale
  (`drawFreqScale`).
- Scale text 10 px renders as today (`font.setPixelSize(10)`). At 14 px the labels are
  not clipped: the scale strip grows from `kFreqScaleH` (28) to fit, and
  `SpectrumOverlayPanel.cpp:1364`'s assumed strip height follows the widget's value.
- All four values survive a save and reload per pan and inherit through Task 1's keys.
- Attribution: `SpectrumWidget.{h,cpp}` carry the AetherSDR project-level block and a
  Modification-history line for this port; each ported block cites its AetherSDR
  source; upstream `//` comments inside ported logic are verbatim;
  `AETHERSDR-PROVENANCE.md` has rows for the three ports; the verifier scripts in
  Global Constraints pass.

**Verification:** ordinary feature plus an attribution gate. Before reading AetherSDR,
state the HOW-TO-PORT pre-port checklist in the report. Sources at `@1e0718ad`:
blanker `src/gui/SpectrumWidget.cpp:8428-8468` (the body ends at 8468, including the
ring update) and setters `:4672-4699`; grid spacing setter `:3030-3036`; scale text
setter `:3037-3044`; panel rows `src/gui/SpectrumOverlayMenu.cpp:1600-1613`,
`:1820-1836`, `:1840-1856` (for ranges and labels). Build and run
`tst_spectrum_widget_additions`; run the verifier scripts; build `NereusSDR`.
Visual check of text sizes is part of Task 16's captures.

**Execution note (advisory):** opus. After Task 2. Port attribution is merge-blocking.

- [ ] **Step 1:** Pin today's Auto grid steps and 10 px scale rendering in tests.
- [ ] **Step 2:** Port the blanker (rolling baseline of row means, threshold clamp
  1.05 to 2.0, slider mapping, replace with the last good row, frozen during TX),
  converting each dBm row to linear power for the row mean.
- [ ] **Step 3:** Port grid spacing and scale text; Auto keeps both of today's step
  ladders untouched.
- [ ] **Step 4:** Attribution block, cites, provenance file and rows, verifier scripts.

## Task 5: Trace colour, fill colour on the GPU, and the pan background (AetherSDR ports)

**Requirements:** D30 (additions 2, 5), D31, D32, S6, S10; R-DSP-40, R-DSP-43,
R-DSP-44; spec 3.7.

**Files:**
- Modify: `src/gui/SpectrumWidget.{h,cpp}`, `docs/attribution/AETHERSDR-PROVENANCE.md`
- Test: `tests/tst_spectrum_widget_additions.cpp`

**Interfaces:**
- Consumes: `PanDisplayKeys`, `displaySettingChanged`, the provenance file (Task 4).
- Produces, on `SpectrumWidget`, per pan on this computer, each emitting
  `displaySettingChanged(<key base>)`:
  - `QColor traceColor() const`, `void setTraceColor(const QColor&)`; key
    `DisplayTraceColor`. When neither the pan nor the default key holds one, the trace
    colour is the fill colour (D31).
  - `QString backgroundImagePath() const`, `void setBackgroundImagePath(const QString&)`
    (empty clears the choice); key `DisplayBackgroundImage`.
  - `bool backgroundEnabled() const`, `void setBackgroundEnabled(bool)`; key
    `DisplayBackgroundEnabled`, default off. Off hides the picture and keeps the
    choice (S6).
  - `int backgroundOpacity() const`, `void setBackgroundOpacity(int)`: 0 to 100; key
    `DisplayBackgroundOpacity`, default 80.
  - `QColor backgroundFillColor() const`, `void setBackgroundFillColor(const QColor&)`;
    key `DisplayBackgroundColor`, default `#0a0a14`.

**Acceptance:**
- With no trace colour stored, the CPU trace pen and the GPU line vertices use the
  fill colour, as today. After `setTraceColor(red)`, the trace is red and the fill
  keeps the fill colour, on the CPU path (`drawSpectrum`, `QPen` at
  `SpectrumWidget.cpp:5256`) and in the GPU vertex colours (`renderGpuFrame`,
  `:12801-12860`).
- The GPU fill's lower edge follows the fill colour instead of the fixed
  `(0, 0, 0.3, fa)` (S10); with today's default fill colour the test pins what the
  lower edge becomes and the report shows it beside the old value.
- Background off (default): the CPU and GPU spectrum background are exactly today's.
- Background colour set and enabled: the spectrum area clears to that colour at the
  set opacity on both paths. A chosen image draws behind the grid and trace, scaled to
  the spectrum area. Clear removes the image; Off keeps the path and stops drawing it.
- A missing or unreadable image file logs one `qCWarning` and draws no image; the
  widget keeps working.
- The overlay clears in `renderGpuFrame` are untouched (diff shows no change to
  `m_overlayStatic.fill(Qt::transparent)` or the dynamic overlay clear).
- All five values save per pan through Task 1's keys and survive a reload.
- Attribution as Task 4; provenance rows added for the two ports.

**Verification:** ordinary feature, GPU path included, plus the attribution gate.
AetherSDR sources at `@1e0718ad`: trace colour setter `src/gui/SpectrumWidget.cpp:4135-4141`
and GPU use `:14405-14407`; background `setBackgroundImage` `:11099-11115`,
`setBackgroundFillColor` `:11117-11125`, `setBackgroundOpacity`
`src/gui/SpectrumWidget.h:437`; panel rows `src/gui/SpectrumOverlayMenu.cpp:1476-1532`,
`:1727-1814`. CPU-path colour and background are asserted from an offscreen
`grab()`; GPU vertex colours through a test accessor on the built vertex data, since
offscreen cannot render QRhi. Build and run `tst_spectrum_widget_additions`; run the
verifier scripts; build `NereusSDR`. GPU rendering of the background is a human
smoke item: pending until seen in a running window (Task 16's captures).

**Execution note (advisory):** opus. After Task 4. The GPU background needs a new
textured draw in `renderGpuFrame`; if that cannot be done without changing the overlay
clears, report BLOCKED rather than changing them.

- [ ] **Step 1:** Trace colour on both paths, defaulting to the fill colour; S10.
- [ ] **Step 2:** Background colour, image and opacity on the CPU path, then the GPU
  path, following AetherSDR's approach.
- [ ] **Step 3:** Attribution, cites, provenance rows, verifier scripts.

## Task 6: One FFT sizing rule, local and remote

**Requirements:** D20, D23 (the "in charge" fact), S17; R-DSP-30 (rule), R-DSP-31
(rule), R-DSP-59.

**Files:**
- Modify: `src/core/ControlRanges.h`, `src/gui/RemoteMediaController.cpp`
  (`plannedFftSize`)
- Test: `tests/tst_auto_zoom_fft.cpp`, `tests/tst_remote_media_controller.cpp`

**Interfaces:**
- Consumes: `ControlRanges::autoZoomFftSize`, `autoZoomMaxFftSize`.
- Produces, in `ControlRanges`:
  - `struct AutoZoomPlan { int size; bool hzPerBinInCharge; };`
  - `AutoZoomPlan planAutoZoomFft(int baseline, double sampleRateHz, double spanHz, double hzPerBinTarget) noexcept`:
    desired points = `hzPerBinTarget > 0 ? sampleRateHz / hzPerBinTarget : baseline * sampleRateHz / spanHz`,
    rounded up to a power of two, then
    `autoZoomFftSize(desiredPow2, baseline, sampleRateHz)`. `hzPerBinInCharge` is true
    when the target is on and the result is larger than the baseline. A rate or span
    that is not finite or not positive returns `{baseline, false}`.
  - `int largestPlannedFftSize(std::span<const AutoZoomPlan> plans, int baseline) noexcept`:
    the largest `size` among the plans, `baseline` when there are none (Task 7 uses
    it for pans that share a stream).
  - The local hysteresis ([0.66, 1.5]) is not part of these functions; Task 7 keeps
    it in the caller.

**Acceptance:**
- Baseline 4096, 96 kHz, span 96 kHz, target off: 4096, not in charge.
- Baseline 4096, 96 kHz, span 300 Hz, target off: 65536 (the 0.7 s cap).
- Baseline 4096, 96 kHz, span 5549 Hz: 65536 (4096 x 96000 / 5549 = 70861, capped).
- Baseline 131072, 96 kHz, span 300 Hz: 131072 (baseline above the cap is honoured).
- Baseline 4096, 96 kHz, target 0.5 Hz/bin: 65536, in charge. Target 50 Hz/bin: 4096,
  not in charge.
- Rate 0 or span 0: `{baseline, false}`.
- `largestPlannedFftSize` of plans sized 4096 and 65536 is 65536; of none, the
  baseline.
- The remote planner asks the Core for exactly `planAutoZoomFft(...).size` with the
  stored `DisplayFftSize` as baseline, the slice's rate, the widget's span and the
  stored Hz/bin target; the pan's pixel width no longer changes the size (two widths,
  same request). Tier stays "fine" when the size is above the baseline.
- `tst_auto_zoom_fft.cpp`'s test-local `remotePlannedFftSize` copy (`:69-77`) is
  replaced by calls to the shared function; its 5549 Hz row changes from 32768 to
  65536 and the report says so.
- The three subscribe-request rows in `tst_remote_media_controller.cpp:6309-6390`
  still pass, or the report shows each changed expectation with the arithmetic.

**Verification:** ordinary feature; update the tests first so they fail on the
per-pixel rule. Build and run `tst_auto_zoom_fft`, `tst_remote_media_controller`
(300 s limit; run it alone); build `NereusSDR`.

**Execution note (advisory):** opus. No prerequisites. Changes what a remote window
asks the Core for; the Core's grant logic is not touched.

- [ ] **Step 1:** Tests for `planAutoZoomFft`, `largestPlannedFftSize` and the remote
  request.
- [ ] **Step 2:** Add the functions; `plannedFftSize` uses the first and still reports
  the baseline through `baseSizeOut`.

## Task 7: FFT settings reach every engine, and every pan replans

**Requirements:** D20, D26 (FFT half); R-DSP-30, R-DSP-33; P10.

**Files:**
- Modify: `src/core/spectrum/FftEnginePool.{h,cpp}`, `src/gui/MainWindow.{h,cpp}`
  (the auto-zoom lambda at `:8958-9009`, the note at `:6012-6018`)
- Test: `tests/tst_fft_engine_pool.cpp`, `tests/tst_auto_zoom_fft.cpp`

**Interfaces:**
- Consumes: `ControlRanges::planAutoZoomFft`, `largestPlannedFftSize` (Task 6).
- Produces, on `FftEnginePool`, each updating the pool's stored config and every
  existing engine (queued to each engine's thread where the engine's setter requires
  it):
  - `void setFftSizeBaseline(int size)`: sets the baseline only; an invalid size
    (not a power of two in 1024 to 262144) is ignored with a `qCWarning`.
  - `void setWindowType(int windowType)`, `void setHzPerBinTarget(double hz)`,
    `void setOutputFps(int fps)`, `void setThreadPriority(QThread::Priority priority)`.
  - `void resetInputHistoryAll()`: queues `FFTEngine::resetInputHistory()` to every
    engine on its own thread (used by Task 13).
- Produces, on `MainWindow`:
  - `void replanFftForStream(int streamIndex)`: for every pan showing that stream,
    `planAutoZoomFft(engine->fftSizeBaseline(), widget->sampleRate(), widget->bandwidth(), engine hz/bin target)`;
    the engine gets `largestPlannedFftSize` of those plans (the finest need wins, as
    the Core's shared engine does), with today's [0.66, 1.5] hysteresis applied to the
    zoom term. Connected to `bandwidthChangeRequested` and `requestAutoZoomReplan()`
    of every pan's widget, not only the widget active at start.
  - `void applyFftBaseline(int size)`: writes `DisplayFftSize`, calls
    `m_fftEnginePool->setFftSizeBaseline(size)` and replans every stream. Tasks 18 and
    20 call it.

**Acceptance:**
- Pool with two streams: `setFftSizeBaseline(16384)` gives both engines baseline 16384
  and leaves each engine's current `fftSize()` for the replan to set; an engine
  created afterwards starts with baseline 16384.
- `setWindowType`, `setHzPerBinTarget`, `setOutputFps` reach both engines and a later
  engine.
- `resetInputHistoryAll()` runs `resetInputHistory()` once on each engine, on that
  engine's thread (assert the thread in a test slot or spy).
- Two pans on one stream, spans 96 kHz and 3 kHz, baseline 4096, 96 kHz: the engine
  runs 65536 (the narrow pan's need), and closing the narrow pan's zoom returns it to
  4096.
- A zoom on pan 1 replans pan 1's stream (today only the first widget is wired).
- After `applyFftBaseline(8192)` at full span, the engine's size is 8192; zooming to
  half span gives 16384; zooming back gives 8192 (the slider's choice is the floor).
- Baseline 262144 at 48 kHz is honoured exactly.

**Verification:** ordinary feature on the spectrum-thread boundary. Pool tests are
unit tests; the per-stream arithmetic is tested through Task 6's pure functions; say
in the report how the `MainWindow` connection was checked. Build and run
`tst_fft_engine_pool`, `tst_auto_zoom_fft`; build `NereusSDR`.

**Execution note (advisory):** opus. After Task 6. Crosses the spectrum thread
hand-off: every engine call from the main thread is queued.

- [ ] **Step 1:** Pool setters and tests.
- [ ] **Step 2:** `replanFftForStream` and `applyFftBaseline`; connect every pan as it
  is created and drop the single-widget connection.

## Task 8: Cal Offset as its own term

**Requirements:** D28; R-DSP-37, R-DSP-58 (Cal Offset fact); P12.

**Files:**
- Modify: `src/gui/SpectrumWidget.{h,cpp}`, `src/gui/MainWindow.cpp` (`:6861-6862`,
  `pushSpectrumCalToPans` at `:6885-6894`), `src/core/AppSettings.{h,cpp}`,
  `src/main.cpp`
- Test: `tests/tst_spectrum_widget_cal_offset.cpp` (new)

**Interfaces:**
- Consumes: `PanDisplayKeys`, `displaySettingChanged`.
- Produces, on `SpectrumWidget`:
  - `void setStationCalOffsetDb(float db)` and `float stationCalOffsetDb() const`: the
    station's receive offset (`RadioModel::rxMeterOffsetDbForStream`). Never saved.
  - `setDbmCalOffset(float)` / `dbmCalOffset()` become the user's Cal Offset only
    (key `DisplayCalOffset`, -30 to 30 dB, default 0), saved per pan.
  - `displayCalOffsetDb()` returns the station term plus the user term on the receive
    path; the keyed and DUP branches use the station term where they used
    `m_dbmCalOffset` and add the user term once; a remote spectrum still returns 0.
- Produces, on `AppSettings`:
  `static void resetPollutedDisplayCalOffset(AppSettings& settings)`, guarded by the
  marker key `DisplayCalOffsetReset`: removes `DisplayCalOffset` and every
  `DisplayCalOffset_<N>` once, because until now those keys held the station's offset,
  not a user value (the Setup control was overwritten every frame).

**Acceptance:**
- Station offset -12.5 dB, user offset 0: a pushed row is shifted -12.5 dB, as today.
- User offset +3 dB on top: the row is shifted -9.5 dB, and the next per-frame
  `setStationCalOffsetDb(-12.5)` does not erase the 3 dB.
- `DisplayCalOffset` saves 3, never the station value; `setStationCalOffsetDb` does
  not schedule a save.
- A settings file holding `DisplayCalOffset=-12.5` and `DisplayCalOffset_1=-12.5`
  loses both on first run and keeps a value written after the marker.
- Remote spectrum: `displayCalOffsetDb()` is 0 whatever the user term holds.
- DUP keyed branch: the result equals today's for user offset 0.

**Verification:** bug fix with a one-time settings reset: test the reset first. Build
and run `tst_spectrum_widget_cal_offset`; build `NereusSDR`.

**Execution note (advisory):** opus. After Task 2. Touches the level every receive
row is drawn at (display only).

- [ ] **Step 1:** The one-time reset and its test; called from `src/main.cpp` beside
  Task 3's conversion.
- [ ] **Step 2:** Split the member, update `displayCalOffsetDb()`, and switch the two
  `MainWindow` call sites to `setStationCalOffsetDb`.

## Task 9: A per-band model for every window pan

**Requirements:** D29, D19 (the store it needs), S9 (store half); R-DSP-27 (store
half), R-DSP-38, R-DSP-49; P13, P16.

**Background the implementer needs:** `PanadapterModel` holds the per-band dB Max and
Min, the per-band noise estimate and the per-band 3D Floor, but nothing in the app
creates one (see "What already exists"). So per-band dB Max/Min, the saved Clarity
level per band and 3D Floor recall do nothing today. This task gives every window pan
its own model, owned by the window. It does **not** call
`RadioModel::addPanadapter()`: that list is mirrored on the station link under an
index-only `pan:<N>` name that cannot tell two windows apart, and filling it would
also switch on unrelated dormant code (`loadSliceState`'s band fallback, the scale
drag at `MainWindow.cpp:14446`, `RxApplet`'s antenna buttons, Setup's `firstPan`).
Those readers stay dormant and the Core gets no pan object.

**Files:**
- Create: `src/gui/PanModels.h`, `src/gui/PanModels.cpp`
- Modify: `src/gui/MainWindow.{h,cpp}` (`ensureOverlayPanels` at `:6180`, the
  `panRetired` handler at `:7112`, the `registerPan` calls at `:6203` and `:6266`,
  the pan-0-only block at `:8647-8675`), `src/gui/SpectrumWidget.{h,cpp}`,
  `src/models/DisplaySettingsModel.{h,cpp}`, `src/models/PanadapterModel.cpp`
  (`applyStationGridSetting`), `src/models/RadioModel.{h,cpp}`
  (`applyPanGridSetting`), `CMakeLists.txt`
- Test: `tests/tst_pan_models.cpp` (new), `tests/tst_remote_pan_grid.cpp`,
  `tests/tst_dss_persistence.cpp`, `tests/tst_band_2m_stores.cpp`

**Interfaces:**
- Consumes: `PanDisplayKeys` (Task 1), `displaySettingChanged` (Task 2).
- Produces, class `NereusSDR::PanModels : public QObject` in `src/gui/PanModels.{h,cpp}`:
  - `PanadapterModel* ensure(const QString& panId, SpectrumWidget* widget)`: creates
    the pan's model (parented to this object) on first call, wires it to the widget
    as below, and returns it; a later call returns the same model.
  - `PanadapterModel* model(const QString& panId) const` (null when unknown).
  - `void retire(const QString& panId)`: deletes the pan's model.
  - `QList<PanadapterModel*> all() const`.
  - `void applyStationGridSetting(const QString& key)`: forwards to every model.
  - signal `void bandGridChanged(const QString& panId)`: the pan's band or its dB
    Max/Min changed (the pill and Setup refresh from it).
- Wiring done by `ensure`:
  - The model's centre follows the widget's centre frequency: seeded on creation and
    on every change (add `SpectrumWidget` signal
    `void centerFrequencyChanged(double centreHz)` emitted by `setCenterFrequency`
    when the value changes), so `PanadapterModel::band()` is the pan's band.
  - `PanadapterModel::levelChanged` sets the widget's range:
    `widget->setDbmRange(model->dBmFloor(), model->dBmCeiling())`, and once on
    creation. This is the only writer of the widget's dB range from settings.
  - The 3D Floor recall that `wireDss3DFloorRecallForTest(PanadapterModel*, SpectrumWidget*)`
    (`MainWindow.cpp:4658-4676`) does for one pan is done for this pan.
  - Every model keeps `followsBandGrid()` true, in local and remote windows alike
    (there is no Core pan to mirror from, so the window applies the band's values
    itself; in a remote window the values are the Core's, see below).
- Produces, on `RadioModel`: signal `void panGridSettingChanged(const QString& key)`,
  emitted by `applyPanGridSetting(key)` after it has handled its own (empty) list.
  `MainWindow` connects it to `PanModels::applyStationGridSetting`, so a per-band
  value arriving from the Core or changed by Setup reaches every window pan.
- Produces, on `SpectrumWidget`: the per-pan range keys stop existing. The widget and
  its `DisplaySettingsModel` no longer read or write `DisplayGridMax` /
  `DisplayGridMin` (bare or `_<N>`) for the dB range; `perPanDisplayKeyBases()` drops
  them. `DisplaySettingsModel`'s ref level and dynamic range stay as live values
  driven from the widget's range, not persisted (S9: the band store owns them).
- `MainWindow` owns one `PanModels`; `ensureOverlayPanels` calls `ensure` for each
  new pan before `registerPan`, and passes that pan's model as `registerPan`'s
  `saved` argument for every pan (today `saved` is null). The `panRetired` handler
  calls `retire`. The pan-0-only block at `:8647-8675` is replaced by the per-pan
  wiring; its noise-floor fast attack on a band change now runs on the pan whose
  band changed.

**Acceptance (`tst_pan_models`, real `SpectrumWidget`s, cleared settings):**
- `ensure("pan-0", w0)` twice returns one model; `retire` then `model("pan-0")` is
  null and no connection to the widget remains (changing the widget's centre does not
  crash).
- Sparse ids: `ensure("pan-0")`, `ensure("pan-3")`, `retire("pan-0")`:
  `model("pan-3")` is still pan 3's.
- Widget centre 14.2 MHz then 7.1 MHz: the model's band goes 20 m then 40 m and
  `bandGridChanged("pan-0")` fires.
- Stored `DisplayGridMax_40m = -30`, `DisplayGridMin_40m = -120`: moving the pan to
  40 m sets the widget's range to -120 / -30. Back on 20 m with nothing stored, it is
  the default -140 / -40.
- `setPerBandDbMax(Band40m, -25)` on pan 0's model while both pans are on 40 m: both
  widgets' ranges move (the store is shared), `DisplayGridMax_40m` holds -25, and a
  pan on 20 m does not move.
- `applyStationGridSetting("DisplayGridMax_40m")` after the key is changed
  underneath (as a value from the Core arrives): pans on 40 m apply it.
- A pan on 2 m applies a change to the transverter slot's key: fix the comparison in
  `PanadapterModel::applyStationGridSetting` (it tests `b == m_band`,
  `PanadapterModel.cpp:407`, where the store uses `gridSlot(m_band)`), with a test in
  `tst_band_2m_stores`.
- 3D Floor: stored `Display3DFloorDepth_40m = 12`; a pan arriving on 40 m shows 12;
  changing it on that pan stores 12's replacement under 40 m only; a second pan on
  40 m follows. `tst_dss_persistence` still passes.
- An old install's per-pan `DisplayGridMax` / `DisplayGridMin` / `DisplayGridMax_1`
  values are ignored (never read, never rewritten, not deleted); the report says so.
  `DisplayGridMax_20m` is untouched by any pan-suffix handling.
- `RadioModel::panadapters()` is still empty after `MainWindow` builds two pans (the
  dormant readers stay dormant): assert through `PanModels` plus a `RadioModel` in
  the test, and state in the report that `addPanadapter()` gained no caller.
- `tst_remote_pan_grid` still passes unchanged (it adds its own `RadioModel` pans).
- Remote window: `PanadapterModel` writes `DisplayGridMax_<band>` through
  `AppSettings`. Trace whether a remote window's station-scope write already reaches
  the Core through the settings proxy (`SettingsScope` classifies the key as Station);
  the report states what was found with file and line. If the write does not reach
  the Core, the task is DONE_WITH_CONCERNS with that finding; do not invent a new
  link message.

**Verification:** ordinary feature that switches on a dormant store: tests first for
the band follow, the range apply and the shared store. Unit tests offscreen. Build and
run `tst_pan_models`, `tst_remote_pan_grid`, `tst_dss_persistence`,
`tst_band_2m_stores`, `tst_per_band_nf_priming`, `tst_pan_clarity_registry`,
`tst_display_settings_model`, `tst_display_settings_golden`; build `NereusSDR` and
`nereusd`. Hardware: pending (per-band dB Max/Min against a Core from a remote
window, JJ).

**Execution note (advisory):** opus. After Tasks 1 and 2. Changes what sets every
pan's dB range at startup: with nothing stored for a band the range is the store's
default (-140 / -40), which the report compares with today's widget default.

- [ ] **Step 1:** `tst_pan_models` for the cases above.
- [ ] **Step 2:** `PanModels`, the widget's `centerFrequencyChanged`, the wiring in
  `ensure`.
- [ ] **Step 3:** `MainWindow`: own it, `ensure` and `retire` at the pan lifecycle
  sites, pass each pan's model as `saved`, replace the pan-0-only block, connect
  `panGridSettingChanged`.
- [ ] **Step 4:** Retire the per-pan range keys from the widget and
  `DisplaySettingsModel`; update the tests that pin them.
- [ ] **Step 5:** The 2 m slot fix and the remote write trace.

## Task 10: Clarity keeps its level through a key, holds after it, and slews

**Requirements:** D14 (controller half), D17, S8, S13; R-DSP-22, R-DSP-24 (Clarity
half), R-DSP-25, R-DSP-52 (controller half); spec 5.5, 5.7; P5, P8.

**Files:**
- Modify: `src/core/ClarityController.{h,cpp}`
- Test: `tests/tst_clarity_controller.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces, on `ClarityController`:
  - `void beginHold(qint64 lengthMs, qint64 nowMs = -1)`: until `nowMs + lengthMs`
    the controller takes in no bins or floors and emits nothing. When the hold ends,
    the smoothing clock restarts: the first accepted reading after it uses a step
    measured from the hold's end, not from the last pre-key reading.
  - `void cancelHold()`; `bool inHold(qint64 nowMs = -1) const`.
  - `void setCalibrationOffsetDb(float db)`: added to the estimate from `feedBins`
    before smoothing, so the floor is in calibrated dB. `feedNoiseFloor` values are
    already calibrated and get nothing added.
  - `setTransmitting(false)` no longer clears the smoothed floor or the emitted state.
    `setTransmitting(true)` cancels any hold and keeps the kept level.
  - The 2 dB deadband no longer gates `waterfallThresholdsChanged`: a threshold pair
    is emitted on every accepted reading whose floor differs from the last emitted
    one (the consumer slews, Task 12). `setDeadbandDb` and its member are removed
    unless a caller outside the tests still uses them; the report says which.
  - `noiseFloorChanged(float)` is not emitted while keyed or in a hold.

**Acceptance (all with injected `nowMs`):**
- Steady floor -120 before a key; `setTransmitting(true)`, then `(false)`,
  `beginHold(1000, t)`: `smoothedFloor()` is still -120 and no signal fired at either
  edge.
- Readings at -100 fed during the hold change nothing and emit nothing.
- A key 10 s long (longer than 3 x tau), hold 1000 ms, then readings at -100 every
  500 ms: the first reading after the hold moves the floor by the 500 ms smoothing
  step (`1 - exp(-0.5 / 3)`, about 15 percent of the 20 dB difference), not by 97
  percent.
- `setTransmitting(true)` during a hold: `inHold()` is false, the kept floor is
  unchanged; a later `setTransmitting(false)` plus `beginHold` starts a fresh hold.
- Enabled while keyed (no level before the key): after un-key and the hold, the first
  reading is taken whole and emitted (nothing to restore).
- After un-key Clarity is active without `retuneNow()` or a band change: readings
  after the hold emit thresholds (R-DSP-25).
- `setCalibrationOffsetDb(-12)` with bins whose 30th percentile is -100: the floor is
  -112. `feedNoiseFloor(-112)` gives -112 with the same offset set.
- A floor moving 0.5 dB per reading emits thresholds on each reading (no 2 dB steps).
- The cadence gate still applies after the first emission.
- Tests that change, named in the report: `transmitRelease_takesTheFirstPostKeyFloorWhole`
  (`:196`, now contradicted and rewritten to the restore), `transmitRelease_resumesEmission`
  (`:150`, emit count), `transmitRelease_reEmitsAtTheUnchangedFloor` (`:167`, now uses
  a different post-key floor and asserts the pre-key thresholds stay first).

**Verification:** consequential state transition: each new case is written first and
fails on main. Unit tests with the injected clock. Build and run
`tst_clarity_controller`, `tst_display_extras` (the Core uses this class); build
`NereusSDR` and `nereusd`.

**Execution note (advisory):** opus. No prerequisites. Shared by the window and the
Core; Tasks 12 and 14 start the hold.

- [ ] **Step 1:** New and rewritten tests.
- [ ] **Step 2:** Hold, kept state across the key, clock restart, calibration offset,
  emission on every changed reading. Comment at the hold: "NereusSDR, based on
  Thetis": Thetis runs the noise-floor fast attack for
  `Math.Max(1000f, fftFillTime + ...)` (`display.cs:5912 [v2.10.3.15]`); NereusSDR
  rejects frames for the same length.

## Task 11: Waterfall AGC and NF-AGC the Thetis way, with the hold

**Requirements:** D15, S7, S13, S14; R-DSP-19, R-DSP-20, R-DSP-24 (AGC and NF-AGC
half), R-DSP-28 (follower half), R-DSP-56 (follower half); spec 5.1, 5.4 step 3, 5.5;
P14.

**Files:**
- Create: `src/core/spectrum/MoxDisplayHold.h`
- Modify: `src/core/spectrum/DisplayFollowers.{h,cpp}`, and every caller of
  `WaterfallLevelFollower::compose` (`src/gui/SpectrumWidget.cpp`,
  `src/core/session/media/DisplayExtras.cpp`)
- Test: `tests/tst_display_followers.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces, in `src/core/spectrum/MoxDisplayHold.h`:
  - `inline constexpr qint64 kMoxHoldMinMs = 1000;`
    (`// From Thetis display.cs:5912 [v2.10.3.15]`: `Math.Max(1000f, fftFillTime + (_wdsp_mox_transition_buffer_clear ? fftFillTime : 0f))`)
  - `qint64 moxHoldLengthMs(int fftSize, int decimation, double sampleRateHz) noexcept`:
    `max(kMoxHoldMinMs, 2 x fill)`, fill ms = `fftSize * decimation * 1000 / sampleRateHz`
    truncated to whole milliseconds; a rate that is not finite or not positive, or a
    size or decimation below 1, returns `kMoxHoldMinMs`. The comment records that
    NereusSDR always purges (so the doubled case applies) and that Thetis's
    "2 extra frames" term (`(m_nFps / 1000f) * 2`, `display.cs:2857 [v2.10.3.15]`,
    about 0.06 ms at 30 fps) is an upstream units bug that is not ported, and why the
    hold still covers two frames.
- Produces, on `WaterfallLevelSettings`: `float noiseFloorDbm` and
  `bool noiseFloorSettled` (the pan's noise-floor follower value and whether it is out
  of fast attack); `noiseFloorAgcOffsetDb` becomes `float`.
- Produces, on `WaterfallLevelFollower`:
  - `inline static constexpr float kAgcRestartFloorDbm = -150.0f;`
    (`// From Thetis display.cs:2748 [v2.10.3.15]`, `WATERFALL_AGC_RESTART_FLOOR_DBM`)
  - `void setKeyed(bool keyed)`: while keyed `compose` re-estimates nothing and
    returns the levels it last produced.
  - `void beginHold(qint64 lengthMs, qint64 nowMs)` and `void cancelHold()`; `compose`
    gains a trailing `qint64 nowMs` argument; during a hold the AGC run level does not
    move (Thetis `bIgnoreAgc`, `display.cs:7640-7651 [v2.10.3.15]`).
  - `std::optional<float> agcRunLevel() const` (the smoothed minimum, empty when not
    primed) and `void restoreAgcRunLevel(std::optional<float> saved)`: a value primes
    the follower with it; empty primes it at `kAgcRestartFloorDbm`.
  - `resetAgc()` stays for callers that are not key edges.
- Produces, on `NoiseFloorFollower`:
  `bool setFastAttack(bool on, qint64 nowMs, qint64 minMs = kFastAttackMinMs)`: the
  fast attack stays on for at least `minMs`.
- Behaviour of `compose`, Clarity not active:
  - AGC on, NF-AGC off: the low edge follows the smoothed row minimum as today
    (`m_agcRunMin - kAgcMarginDb`); the high edge is `settings.highDbm`
    (`high_threshold = waterfall_high_threshold;`, `display.cs:6463 [v2.10.3.15]`).
  - AGC on, NF-AGC on (NF-AGC is an option under AGC, S7): target =
    `noiseFloorDbm + noiseFloorAgcOffsetDb`; the run level moves toward the target as
    Thetis does, `run = run * 0.6f + target * 0.4f` per row
    (`display.cs:7662-7666 [v2.10.3.15]`, constants kept as named `constexpr`); low =
    the target when `noiseFloorSettled`, else the run level
    (`display.cs:6466-6470 [v2.10.3.15]`); high = `settings.highDbm`.
    NereusSDR keeps its stored sign (the offset is added; Thetis subtracts
    `m_fWaterfallAGCOffsetRX1`, `display.cs:6207 [v2.10.3.15]`) so stored values, the
    phone and older windows and Cores keep their meaning; the comment says so.
  - AGC off: Low and High are the user's, whatever NF-AGC holds.
  - "Use spectrum min/max" and Clarity behave as today.

**Acceptance:**
- `moxHoldLengthMs(4096, 1, 192000) == 1000`; `(262144, 1, 48000) == 10922` (2 x
  5461 ms); `(65536, 2, 96000) == 2730`; `(4096, 1, 0) == 1000`.
- AGC on, High -62, rows with minimum -118 and maximum -40: after 200 rows the low is
  about -130 (minimum less 12) and the high is exactly -62.
- NF-AGC on under AGC, noise floor -115, offset -3, settled: low is exactly -118, high
  exactly the user's High. Not settled, run level -150: the low rises toward -118 by
  the 0.6/0.4 blend each row (-137.2 after one row).
- NF-AGC on with AGC off: low and high are the user's Low and High.
- `setKeyed(true)`: 50 rows 40 dB hotter change neither edge nor `agcRunLevel()`.
- `beginHold(2000, 0)`: rows at `nowMs` 0 to 1999 do not move the run level; a row at
  2000 does.
- `restoreAgcRunLevel(-121.0f)` then one identical row: low is -133;
  `restoreAgcRunLevel(std::nullopt)`: `agcRunLevel()` is -150.
- `setFastAttack(true, 0, 5000)`: still on at 4999 ms even when converged; the
  default keeps today's 1000 ms behaviour.
- The existing AGC test (`tst_display_followers.cpp:197`) and NF-AGC test (`:222`) are
  rewritten to the above and named in the report.

**Verification:** DSP-adjacent display behaviour ported from Thetis: read
`display.cs:6190-6208`, `:6400-6480`, `:7640-7690` first and state the pre-port
checklist; tests first, failing on main. `DisplayFollowers.{h,cpp}` already port
Thetis; keep the header and add cites. Build and run `tst_display_followers`,
`tst_display_extras`; run the verifier scripts; build `NereusSDR` and `nereusd`.

**Execution note (advisory):** opus. No prerequisites. Changes waterfall behaviour
with AGC on for every user (the high edge stops moving); that is D15.

- [ ] **Step 1:** `MoxDisplayHold.h` and its tests.
- [ ] **Step 2:** Rewritten and new follower tests.
- [ ] **Step 3:** `compose` changes with cites and verbatim Thetis comments
  (`//delay waterfall agc`, `//wfagc`), keyed freeze, hold, run-level save and
  restore, fast-attack minimum.
- [ ] **Step 4:** Update every `compose` caller for the new argument and settings
  fields so both binaries build; callers pass the wall clock and their own
  noise-floor follower's value and settled state (Tasks 13 and 14 finish the wiring).

## Task 12: The window's Clarity registry: keep, hold, re-centre, priming

**Requirements:** D14, D17, D18, D19, S8, S21; R-DSP-21 (Clarity half), R-DSP-22,
R-DSP-24, R-DSP-26, R-DSP-27, R-DSP-52; spec 5.4 step 1, 5.5, 5.8, 8.4; P6, P7.

**Files:**
- Modify: `src/gui/PanClarityRegistry.{h,cpp}`, `src/gui/SpectrumWidget.{h,cpp}`
  (floor target and slew), `src/core/AppSettings.{h,cpp}`, `src/main.cpp`
- Test: `tests/tst_pan_clarity_registry.cpp`

**Interfaces:**
- Consumes: `ClarityController::beginHold`, `cancelHold`, `setCalibrationOffsetDb`
  (Task 10); `SpectrumWidget::setClarityFloorDbm` (Task 3), `clarityActiveChanged`
  (Task 2), `displayCalOffsetDb()` receive value (Task 8); a `saved` model for every
  pan (Task 9); `kMoxHoldMinMs` (Task 11).
- Produces, on `PanClarityRegistry`:
  - `void setHoldLengthProvider(std::function<qint64(const QString& panId)> provider)`:
    asked per pan when a hold starts; with none set the hold is `kMoxHoldMinMs`.
  - `void setClockForTest(std::function<qint64()> nowMs)`: replaces the wall clock in
    the priming window and the hold.
  - `setKeyed(true)` no longer clears any pan's Clarity. `setKeyed(false)` starts each
    pan's hold; a pan whose band changed while keyed first snaps once to the current
    band's stored level, or, with none stored, emits nothing until its first level
    after the hold.
- Produces, on `SpectrumWidget`: `void setClarityFloorTargetDbm(float noiseFloorDbm)`.
  The drawn floor moves toward the target on every waterfall row in
  `pushWaterfallRow`, by Thetis's `current * 0.6f + target * 0.4f`
  (`display.cs:7662-7666 [v2.10.3.15]`); `setClarityFloorDbm` (Task 3) is the
  immediate form, used for a snap and the first level.
- Produces, on `AppSettings`:
  `static void clearUncalibratedClarityFloors(AppSettings& settings)`, guarded by the
  marker key `ClarityFloorsCalibrated`: removes every `DisplayBandNFEstimate_<band>`
  and `ClarityFloor_<band>` once, because stored floors were raw and are now
  calibrated. Called from `src/main.cpp` beside the other two conversions.

**Acceptance (injected clock, `setLocalKeyedQueryForTest`):**
- Two pans with Clarity active; `setKeyed(true)`: both widgets still report
  `clarityActive()`; `coreWaterfallLevelsInUse()` is unchanged on a remote pan.
- `setKeyed(false)`: on the first frame after it each widget's Clarity floor is the
  pre-key value even though the frames now carry a floor 15 dB higher; nothing is
  taken in for the provider's hold length; after it the floor moves smoothly.
- Band change while keyed, the new band has a stored level: one snap to it at un-key.
  No stored level: no emission until the first reading after the hold.
- Re-key 300 ms into a 1000 ms hold, then un-key: a fresh full hold starts; the kept
  level never moved.
- `pausedChanged(true)` (`PanClarityRegistry.cpp:254-260`) no longer clears
  `clarityActive`.
- Re-centre: `bindLocal` with the same stream, epoch and rate and a different
  `centreHz` keeps the smoothed floor and starts a hold; a different stream, epoch or
  rate resets fully, as today.
- Priming: readings every 500 ms at a steady floor (variance under 1.0) for 15 s save
  the band's estimate through the pan's saved model (`setBandNFEstimate`); on main the
  same test saves nothing. The window keeps the 30 samples the check needs. Every pan
  with a saved model saves, not only pan 0.
- Calibration: with the widget's receive cal offset at -12 dB, the floor Clarity holds
  for local bins is 12 dB below the raw percentile; for `feedRemoteFloor` nothing is
  added. The offset is read when each frame is fed, so the keyed attenuator value is
  never used (Clarity takes no frames while keyed).
- Slew: a target 10 dB above the current floor moves the drawn floor 4 dB on the next
  row, then 2.4 dB, and so on.
- The one-time clean-up removes `DisplayBandNFEstimate_20m` and `ClarityFloor_20m`
  once and leaves values written after the marker.
- Tests that change, named in the report: `keyedTransitionsSurviveSameSourceRebind`
  (`:89`, asserts at `:108-109`), the `!clarityActive` checks after a key
  (`:118-147`, `:297-313`), `unkeyRestoresClarityAtTheUnchangedFloor` (`:428`, assert
  `:444`, now with a different post-key floor), `bandHintsAndSolePan0Writer` (`:480`,
  now on the injected clock at the real cadence, and no longer pan 0 only).

**Verification:** consequential state transition: new cases first, failing on main.
Widget tests offscreen. Build and run `tst_pan_clarity_registry`,
`tst_clarity_controller`, `tst_per_band_nf_priming`, `tst_remote_media_controller`
(alone); build `NereusSDR`.

**Execution note (advisory):** opus. After Tasks 3, 8, 9, 10, 11.

- [ ] **Step 1:** New and rewritten tests with the injected clock.
- [ ] **Step 2:** Remove the clears; hold start on un-key with the provider; band
  change while keyed; split the `SourceAssociation` comparison for the re-centre.
- [ ] **Step 3:** Priming window sized for 30 samples at the cadence, on the injected
  clock; calibration offset fed per frame.
- [ ] **Step 4:** Floor target and per-row slew in the widget, with the Thetis cite;
  the one-time clean-up.

## Task 13: Window key edges: purge, AGC freeze and restore, fast attack on every pan

**Requirements:** D14, D17, D17a, S13, S15, S16; R-DSP-21, R-DSP-23 (window),
R-DSP-24 (window), R-DSP-28 (window), R-DSP-53 (window), R-DSP-54, R-DSP-56; spec
5.3, 5.4, 5.5, 5.9.

**Files:**
- Modify: `src/gui/MoxDisplayController.{h,cpp}`, `src/gui/SpectrumWidget.{h,cpp}`,
  `src/gui/MainWindow.cpp` (`:4003-4005`, `:8558-8560`, `:8922-8931`)
- Test: `tests/tst_mox_display_controller.cpp`, `tests/tst_spectrum_widget_keyed_levels.cpp`
  (new)

**Interfaces:**
- Consumes: `FftEnginePool::resetInputHistoryAll()` (Task 7);
  `WaterfallLevelFollower::setKeyed`, `beginHold`, `agcRunLevel`,
  `restoreAgcRunLevel`, `NoiseFloorFollower::setFastAttack(on, nowMs, minMs)`,
  `moxHoldLengthMs` (Task 11); `PanClarityRegistry::setHoldLengthProvider` (Task 12);
  the pan's band from `PanModels::model(panId)->band()` (Task 9).
- Produces, on `SpectrumWidget`:
  - `void setDisplayKeyed(bool keyed, qint64 holdLengthMs, int bandKey)`: on `true`,
    saves the AGC run level under `bandKey` (receive context) and freezes the
    follower; on `false`, restores the run level saved under `bandKey` (or the restart
    floor), starts the follower's hold and the noise-floor fast attack with
    `holdLengthMs` as its minimum. `bandKey` is the pan's current band as an `int`.
  - `qint64 moxHoldLengthMs() const`: from this pan's engine size, decimation and
    rate for a local pan (given to the widget through
    `void setLocalFftShape(int fftSize, int decimation, double sampleRateHz)`, which
    `MainWindow` calls when the pan's engine changes size, decimation or rate);
    `kMoxHoldMinMs` when it has none (Task 15 supplies the remote value).
  - The per-band AGC run levels live in the widget, in memory.
- `MoxDisplayController::setKeyed` drives every pan (`m_pans->allApplets()`), not
  only the transmit pan: before `rise()` / `fall()` it calls each pan's
  `setDisplayKeyed`, taking the band through a
  `std::function<int(const QString& panId)>` set by
  `void setPanBandProvider(...)`. `keyedChanged` still fires first (`:215`).
- `MainWindow` connects `MoxDisplayController::keyedChanged` to
  `m_fftEnginePool->resetInputHistoryAll()`, sets the band provider from `PanModels`,
  and sets the registry's hold-length provider to the pan widget's
  `moxHoldLengthMs()`.

**Acceptance:**
- Rise: no pan's `clarityActive()` changes; `resetWaterfallAgc()` is not called on any
  pan; the transmit pan shows TX levels through `setMoxOverlay(true)`.
- While keyed, a non-transmitting pan with AGC on keeps its levels when fed rows 40 dB
  hotter, and keeps drawing rows.
- Fall: no `resetWaterfallAgc()`; each pan's AGC run level equals its pre-key value on
  the first receive row; the hold length is `moxHoldLengthMs()` of that pan.
- Band changed while keyed, AGC on, no saved level for the new band: the run level
  starts at -150 dBm.
- Both edges: `resetInputHistoryAll()` is called once per edge.
- The call log pins the order `keyedChanged` before `fall()`.
- A key shorter than the RF delay produces no `setKeyed(true)` and so no purge, no
  hold and no change on any pan (the existing no-op at `MoxDisplayController.cpp:201-212`
  is pinned by a test).
- Keys through MOX, TUNE and one of TCI or CAT (whichever the existing
  `MoxController` tests already drive) reach `setKeyed` and produce the same result.
- A display-duplex pan follows the same rules.
- The noise-floor fast attack on a MOX edge is started from the display edge on every
  pan with the hold length as its minimum, and the `TransmitModel::moxChanged`
  connection at `MainWindow.cpp:8922-8931` is gone. Band, 0.5 MHz jump and attenuator
  triggers are unchanged and still use 1000 ms.
- The #359 comment at `MoxDisplayController.cpp:409-412` is rewritten to describe the
  kept level.
- Tests that change, named in the report: the `resetWaterfallAgc` assertions at
  `tst_mox_display_controller.cpp:107`, `:129`, `:676`, `:696`, the
  `setClarityActive(false)` expectations at `:103`, `:674`, `:694` and
  `QVERIFY(!two->m_clarityActive)` at `:708`.

**Verification:** consequential state transition on the receive display path
(CLAUDE.md flag): new cases first, failing on main. Offscreen widget tests with the
call log. Build and run `tst_mox_display_controller`,
`tst_spectrum_widget_keyed_levels`, `tst_spectrum_widget_mox_overlay`,
`tst_pan_clarity_registry`; build `NereusSDR`. Hardware: pending (live un-key on the
bench, JJ).

**Execution note (advisory):** opus. After Tasks 7, 9, 11, 12. Flagged: receive
display path, spectrum-thread hand-off. Does not touch demodulation, audio or what
keys the radio.

- [ ] **Step 1:** New and rewritten tests.
- [ ] **Step 2:** `setDisplayKeyed`, the per-band run-level store, `setLocalFftShape`,
  `moxHoldLengthMs()`.
- [ ] **Step 3:** `MoxDisplayController` fans out to every pan and drops the three
  resets; rewrite the #359 comment. Each site that differs from Thetis says
  "NereusSDR, based on Thetis" with the cite (always purging; freezing every pan's
  AGC; rejecting frames in the hold).
- [ ] **Step 4:** `MainWindow`: purge on `keyedChanged`, the two providers, the FFT
  shape, fast attack moved to the display edge for every pan.

## Task 14: Core key edges: purge, AGC keep and restore, hold, carried state

**Requirements:** D17, D17a, S13, S21; R-DSP-23 (Core), R-DSP-24 (Core), R-DSP-28
(Core), R-DSP-53 (Core), R-DSP-55, R-DSP-60; spec 5.2, 5.3 last bullet, 5.6, 8.4.

**Files:**
- Modify: `src/core/session/media/DaemonSpectrumSource.{h,cpp}`,
  `src/core/session/media/DaemonMediaController.{h,cpp}` (`:855`, `:3258`,
  `:3324-3336`, `:5080`), `src/core/session/media/DisplayExtras.{h,cpp}`
- Test: `tests/tst_display_extras.cpp`, `tests/tst_daemon_media_controller.cpp`

**Interfaces:**
- Consumes: `ClarityController::beginHold` (Task 10); `WaterfallLevelFollower`
  additions and `moxHoldLengthMs` (Task 11).
- Produces:
  - `void DaemonSpectrumSource::resetInputHistoryAll()`: marks every active source for
    an input reset through the existing discontinuity path so each engine resets on
    its own worker thread.
  - `DisplayExtrasInputs::holdLengthMs` (`qint64`): the un-key hold for this endpoint,
    `moxHoldLengthMs(sourceFftSize, sourceDecimation, sourceSampleRateHz)` from the
    `EndpointEntry` fields.
  - `DisplayExtrasInputs::mox` is fed from a `DaemonMediaController` member set in the
    `MoxController::moxStateChanged` slot (`:855`), not from `transmitModel().isMox()`.
    When the Core has no `MoxController` (the `else` branch near `:861`), it stays on
    `isMox()` and a comment says why.
  - `struct DisplayExtrasCarry` with `DisplayExtrasCarry DisplayExtrasProcessor::takeCarry()`
    and `void DisplayExtrasProcessor::adoptCarry(DisplayExtrasCarry&& carry)`: Clarity's
    smoothed floor and last thresholds, the AGC run level and the per-band AGC run
    levels.
  - `DisplayExtrasProcessor::process`: on a MOX rise it saves the AGC run level for
    the current band and freezes; on the fall it restores (or the restart floor),
    starts the Clarity and follower holds with `inputs.holdLengthMs` and uses it as
    the fast-attack minimum; it no longer calls `m_levels.resetAgc()` on a MOX change
    (`DisplayExtras.cpp:655-657`).

**Acceptance:**
- `moxStateChanged(true)` and `(false)` each reset every active source engine once, on
  its worker thread.
- `DisplayExtrasProcessor`, AGC levels: a MOX rise and fall leave the run level at its
  pre-key value on the first receive frame; no re-prime. With a band change while
  keyed and nothing saved, it starts at -150 dBm.
- Hold with injected `nowMs` and `holdLengthMs` 3000: Clarity and AGC do not move for
  3000 ms after the fall, then resume; the noise-floor state stays fast-attack for at
  least 3000 ms.
- A key shorter than the RF delay (no `moxStateChanged`) changes nothing in the
  extras, even though `transmitModel().isMox()` toggled.
- Re-request for the same `MediaSourceKey` (`replacedSource == source`): the new
  processor's Clarity floor, thresholds and AGC run level equal the replaced one's on
  its first frame. A different source carries nothing.
- For the same bins and calibration, `fullSourceNoiseFloor`'s value equals what a
  local `ClarityController` with the same calibration offset computes (R-DSP-60); the
  noise-floor message's fields and meaning are unchanged.
- `logUnkeyStats()` still runs on the fall.

**Verification:** consequential state transition on the Core's receive display path
(CLAUDE.md flag): new cases first, failing on main. Uses the existing fixtures
(`publishFrameForTest`, the injected `m_monotonicClock`, `displayExtrasForTest`).
Build and run `tst_display_extras`, `tst_daemon_media_controller`; build `nereusd`
and `NereusSDR`. Hardware: pending (remote un-key on the bench, JJ; never install on
a Core from this task).

**Execution note (advisory):** opus. After Tasks 10 and 11. Flagged: Core receive
display path, worker-thread hand-off. No wire change.

- [ ] **Step 1:** New tests in both files.
- [ ] **Step 2:** `resetInputHistoryAll()` and the `moxStateChanged` slot.
- [ ] **Step 3:** `DisplayExtrasProcessor` MOX handling, hold, per-band AGC levels.
- [ ] **Step 4:** Carry across a same-source re-request at `:3324-3336`.

## Task 15: Remote window key edges

**Requirements:** D14 (remote), D17 (remote), spec 5.6; R-DSP-21 (remote), R-DSP-24
(remote).

**Files:**
- Modify: `src/gui/RemoteMediaController.cpp`, `src/gui/SpectrumWidget.{h,cpp}`
  (`moxHoldLengthMs()` for a remote pan)
- Test: `tests/tst_remote_media_controller.cpp`

**Interfaces:**
- Consumes: `SpectrumWidget::moxHoldLengthMs()` and `setDisplayKeyed` (Task 13);
  `PanClarityRegistry` behaviour (Task 12).
- Produces: for a remote pan `moxHoldLengthMs()` uses
  `remoteGrantedFftSize()`, the source rate given to `setRemoteSpectrumContext`, and
  the decimation this window last applied through the Core's decimation control (1
  when `spectrumDecimationAvailable()` is false), given to the widget through
  `void setRemoteDecimation(int decimation)`. No field is added to the grant.

**Acceptance:**
- A remote pan with Clarity active: a key and an un-key send no new subscribe request
  and cause no endpoint rebuild (count the outbound `subscribe` controls across both
  edges: zero), because `coreWaterfallLevelsInUse()` does not flip.
- Granted size 262144 at 48 kHz, decimation 1: the Clarity hold after un-key is
  10922 ms; granted 4096 at 192 kHz: 1000 ms.
- During the hold, `noise-floor` control messages from the Core do not move the
  window's Clarity floor; after it they do.
- Against a Core that sends the old behaviour (AGC re-primed on the Core), the window
  still opens, draws and keeps its own Clarity level (no dependency on Task 14 to
  run).

**Verification:** integration-level unit test in the existing remote fixture
(injected `noise-floor` control JSON at `:5098`, `:5202`). Build and run
`tst_remote_media_controller` alone; build `NereusSDR`. Hardware: pending.

**Execution note (advisory):** opus. After Tasks 12, 13.

- [ ] **Step 1:** Tests for the request count, the hold length and the held floor.
- [ ] **Step 2:** Remote `moxHoldLengthMs()`; confirm nothing else at the key edges
  changes the request.

## Task 16: The pill frame, and the Spectrum and 3D View tabs

**Requirements:** D1, D2, D4, D5 (button), D6 (link), D7, D8; R-DSP-01 to R-DSP-06,
R-DSP-08, R-DSP-11, R-DSP-12, R-DSP-48; spec 3.1, 3.3, 3.6.

**Files:**
- Modify: `src/gui/SpectrumOverlayPanel.{h,cpp}`, `src/gui/MainWindow.cpp`
  (`wirePanDisplayFlyout`, only as far as needed to compile)
- Test: `tests/tst_spectrum_overlay_panel.cpp`, `tests/tst_b8_overlay_signals.cpp`

**Interfaces:**
- Consumes: `SpectrumWidget` getters and setters for every Spectrum and 3D row,
  `displaySettings()` model signals, `displaySettingChanged` and `panDisplayReset`
  (Tasks 1, 2), the additions (Tasks 4, 5), `GuardedSlider`.
- Produces, on `SpectrumOverlayPanel`:
  - `void setDisplayTarget(SpectrumWidget* widget)`: the pan the pill edits. Per-pan
    rows read and write this widget (and its `DisplaySettingsModel`) directly.
  - `void openDisplayPill()` (public; shows the pill if hidden) and
    `bool displayPillVisible() const`.
  - `enum class DisplayPillTab { Spectrum, Fft, Waterfall, View3D };`
    `DisplayPillTab displayPillTab() const`, `void setDisplayPillTab(DisplayPillTab)`.
    The last tab is saved under `DisplayPillLastTab` (one key for this computer).
  - `void setBandGrid(const QString& bandLabel, int dbMax, int dbMin)` and signals
    `void perBandDbMaxRequested(int db)`, `void perBandDbMinRequested(int db)`.
  - `void setGpuCapabilities(bool rowSpanSupported, bool sliceShadowSupported)`.
  - `void setRemoteWindow(bool remote, bool coreOffersDecimation)`.
  - signals `void resetPanRequested()` and `void openSetupRequested(const QString& page)`
    (existing; the page is chosen from the open tab: Spectrum and FFT give
    "Spectrum Defaults", Waterfall gives "Waterfall Defaults", 3D View gives "3D View").
  - Object names for tests: the tab buttons `displayTabSpectrum`, `displayTabFft`,
    `displayTabWaterfall`, `displayTab3D`; each row's control is named
    `pill<KeyBase>` (for example `pillDisplayGridEnabled`); footer `pillResetPan`,
    `pillSetupDefaults`.
  - The old display signals (`wfColorGainChanged`, `wfBlackLevelChanged`,
    `colorSchemeChanged`, `cursorFreqVisibleChanged`, `gridVisibleChanged`,
    `fillColorChanged`, `fillAlphaChanged`) and `setGridVisible` are removed with
    their `MainWindow` connections (Task 18 rewires; this task leaves
    `wirePanDisplayFlyout` compiling by calling `setDisplayTarget`).

**Acceptance:**
- The pill has four tab buttons in the order Spectrum, FFT, Waterfall, 3D View; each
  tab has the accent header and the footer with "Reset this pan" and
  "Setup defaults →"; the FFT header shows "ALL PANS".
- Spectrum tab rows in the order of spec table 3.3, 3D View rows in the order of 3.6,
  with the ranges and "shown as" formats given there (line width 0.5 to 3 px in 0.5
  steps; scale text 8 to 14 px; grid spacing Auto, 1, 2, 5, 10, 25, 50, 100 kHz).
- Opened on a widget with non-default values for every Spectrum and 3D row, each
  control shows the widget's value; opening emits no setter call on the widget
  (spy on `displaySettingChanged` and the model signals: zero).
- While open, changing a value on the widget (as Setup or the menu would) updates the
  control without the pill writing it back (one widget change, zero echoes).
- Changing a control calls the widget's setter once.
- Peak Delay is disabled while Peak Hold is off; 3D Span and Slice Shadow are disabled
  after `setGpuCapabilities(false, false)`; Cal Offset is disabled with the tooltip
  "The Core calibrates the display for its radio." after `setRemoteWindow(true, ...)`.
  All stay visible.
- The fill and trace colour buttons open their dialog on the widget's current colours
  (test through a seam that captures the initial colour).
- dB Max / dB Min show `setBandGrid`'s values and the note
  "dB Max and Min are kept per band (20 m)." with the band label given; moving them
  emits `perBandDbMaxRequested` / `perBandDbMinRequested`.
- Background: Choose..., Clear and Off buttons, BG Opacity and BG Color drive the
  Task 5 setters.
- "Reset this pan" emits `resetPanRequested()`; after the widget's `panDisplayReset()`
  the pill re-seeds.
- A wheel event delivered to the pill or any child is accepted and reaches no slider
  that does not have focus and not the spectrum widget (frequency unchanged).
- The pill reopens on the tab last used, after the panel is rebuilt.
- The Spectrum tab's size hint is no taller than 480 px and 268 px wide; no tab has a
  scroll area.
- Every string follows the spec; none has an em dash.

**Verification:** UI task: functional checks offscreen in
`tst_spectrum_overlay_panel` (extend `PanelHarness` with a real `SpectrumWidget`),
plus rendered evidence per `ui-verification`: after this task and Task 17, the
controller captures the four tabs from a running window (Clarity on and off) beside
the approved mockups. `tests/visual/capture.py` is not on this branch; use
screenshots of the running window, no radio and no Core connected. Offscreen tests
cannot prove clicks or hover on macOS native views. Build and run
`tst_spectrum_overlay_panel`, `tst_b8_overlay_signals`; build `NereusSDR`.

**Execution note (advisory):** opus. After Tasks 1 to 5 and 8. Large file; read it in
big ranges.

- [ ] **Step 1:** Tests for tabs, order, seeding, live follow, footer, wheel, last tab.
- [ ] **Step 2:** Rebuild `buildDisplayFlyout()` as a tab strip over four pages in the
  mockup's style (spec 3.1: 268 px, colours and sizes from the mockup's CSS header,
  4-column grid, margins 8/6, spacing 4); guarded sliders and combos; the pill itself
  swallows wheel events.
- [ ] **Step 3:** Spectrum and 3D View pages bound to the widget: seed under
  `QSignalBlocker` on show, connect while visible, disconnect on hide.
- [ ] **Step 4:** Leave the Waterfall and FFT pages as built frames with their headers
  and footers for Task 17 to fill; update `tst_b8_overlay_signals` for the removed
  signals.

## Task 17: The pill's Waterfall and FFT tabs

**Requirements:** D3 (pill), D13, D13a, D22, D23, D25, S12, S18, S19; R-DSP-07
(pill), R-DSP-17 (pill), R-DSP-18, R-DSP-34, R-DSP-46, R-DSP-51 (controls), R-DSP-57,
R-DSP-58, R-DSP-61 (pill); spec 3.4, 3.5.

**Files:**
- Modify: `src/gui/SpectrumOverlayPanel.{h,cpp}`, `src/gui/PanClarityRegistry.cpp`
  (the status call)
- Test: `tests/tst_spectrum_overlay_panel.cpp`

**Interfaces:**
- Consumes: Task 16's frame; `ClarityLevels` (Task 3); the Task 4 blanker setters;
  `ControlRanges` FFT ranges; `wfSchemeName()` / `WfColorScheme::Count`.
- Produces, on `SpectrumOverlayPanel`:
  - `void setClarityState(bool enabled, bool active, bool paused)` (replaces
    `setClarityStatus`; `PanClarityRegistry`'s call is updated) and signal
    `void clarityEnabledRequested(bool on)`; `clarityRetuneRequested()` stays.
  - `struct DisplayPillFftState { int baselineSize; int fps; int windowType; double hzPerBinTarget; int decimation; int runningSize; double sampleRateHz; bool hzPerBinInCharge; };`
    and `void setFftState(const DisplayPillFftState& state)`.
  - signals `void fftBaselineRequested(int size)`, `void fftFpsRequested(int fps)`,
    `void fftWindowRequested(int windowType)`,
    `void hzPerBinTargetRequested(double hz)`, `void decimationRequested(int decimation)`.
  - `void setStationRowsAvailable(bool available, const QString& reason)`.

**Acceptance:**
- Waterfall tab rows in the order of spec table 3.5; FFT tab rows in the order of 3.4.
- Scheme lists all eight `wfSchemeName()` entries; a widget on Clarity Blue shows
  "Clarity Blue".
- Black Level is 0 to 100 and reads as an offset from the noise (`N+3 dB` at 57,
  `N+0 dB` at 50, `N-5 dB` at 40); Color Gain reads as the span (`38 dB` at 60). The
  Black tooltip says that up is blacker.
- Clarity on and active: AGC, NF-AGC (pill and offset), Low, High and
  "Use spectrum min/max" are disabled, and the note
  "Clarity is setting the levels. Turn it off to use these." shows.
- Clarity off: Black Level and Color Gain are disabled with the note
  "Clarity is off: Low and High set the levels."; the other four are enabled.
- Clarity off and "Use spectrum min/max" on: AGC, NF-AGC, Low and High are disabled
  with "The spectrum's dB Max and Min are setting the levels."
- Clarity off, AGC off: the NF-AGC row is disabled with "Turn on AGC to use NF-AGC."
- The NF-AGC offset moves in 0.1 dB steps from -60 to 60.
- NB Blank shows the threshold as `1.15` at 15 and is 5 to 95.
- The Clarity row shows the badge (green active, amber paused), the On pill and
  Re-tune; the pill emits `clarityEnabledRequested`; Re-tune emits
  `clarityRetuneRequested`.
- FFT Size slider: positions 0 to 6 are 4096 to 262144; dragging emits nothing until
  release, then `fftBaselineRequested` once; keyboard steps emit once, 300 ms after
  the last key (injected timer or `QTRY` with the signal count).
- Readout under FFT Size from `setFftState`: with running size 131072 at 192 kHz it
  reads "1.46 Hz per bin · 0.68 s per transform at 192 kHz"; with
  `hzPerBinInCharge` it reads "Hz/bin target is setting the size".
- Hz/bin is an Off/On pill plus a 0 to 200 slider in 0.5 steps; 0 is Off.
- `setStationRowsAvailable(false, "Connect to the Core to change these.")` disables
  every FFT row and the dB Max / dB Min rows and shows the reason.
- `setRemoteWindow(true, false)`: Decimation is disabled with
  "This Core is too old to change Decimation."; with `(true, true)` it is enabled.
- Every Waterfall row seeds from and follows the widget as in Task 16; per-pan rows
  save on this computer (no station key is written by a Waterfall row).

**Verification:** as Task 16 (functional offscreen, then captures of all four tabs
with Clarity on and off against the mockups). Build and run
`tst_spectrum_overlay_panel`, `tst_pan_clarity_registry`; build `NereusSDR`.

**Execution note (advisory):** opus. After Task 16 (same file).

- [ ] **Step 1:** Tests for row order, formats, the four greying states and their
  notes, FFT slider release and key debounce, the readout strings, station and remote
  gates.
- [ ] **Step 2:** Waterfall page bound to the widget; greying driven by
  `setClarityState`, the widget's `clarityActiveChanged`, AGC and min/max.
- [ ] **Step 3:** FFT page driven by `setFftState` and the request signals;
  `setTracking(false)` plus the 300 ms key timer.

## Task 18: Wire the pill to the window

**Requirements:** D2, D5, D6, D25, D26 (pill half), D34; R-DSP-09, R-DSP-10,
R-DSP-33 (pill), R-DSP-34, R-DSP-46.

**Files:**
- Create: `src/gui/DisplayPillWiring.h`, `src/gui/DisplayPillWiring.cpp`
- Modify: `src/gui/MainWindow.{h,cpp}` (`wirePanDisplayFlyout` and its callers),
  `CMakeLists.txt`
- Test: `tests/tst_display_pill_wiring.cpp` (new)

**Interfaces:**
- Consumes: Tasks 7, 9, 16, 17; `MainWindow::stationSettingsAvailable()` /
  `stationSettingsReason()`; `StationClient::spectrumDecimationAvailable()`.
- Produces, in `src/gui/DisplayPillWiring.{h,cpp}`:
  `void wireDisplayPill(SpectrumOverlayPanel* panel, SpectrumWidget* widget, const DisplayPillHooks& hooks)`
  with
  `struct DisplayPillHooks { std::function<void(int)> applyFftBaseline; std::function<void(int)> applyFps; std::function<void(int)> applyWindowType; std::function<void(double)> applyHzPerBinTarget; std::function<void(int)> applyDecimation; std::function<DisplayPillFftState()> fftState; std::function<void(bool)> setClarityEnabled; std::function<void()> retuneClarity; std::function<void(int)> setPerBandDbMax; std::function<void(int)> setPerBandDbMin; std::function<void(const QString&)> openSetupPage; };`
  so the wiring is testable without a `MainWindow`. `MainWindow::wirePanDisplayFlyout`
  builds the hooks and calls it.

**Acceptance (test with fake hooks and a real panel and widget):**
- `fftBaselineRequested(16384)` calls `applyFftBaseline(16384)` once; the other four
  FFT requests call their hooks; after each, the panel's `setFftState` is refreshed
  from `fftState()`.
- `resetPanRequested()` calls `widget->resetPanDisplayOverrides()`; the station keys
  are unchanged afterwards.
- `openSetupRequested("Waterfall Defaults")` calls `openSetupPage` with that name.
- `clarityEnabledRequested(false)` calls `setClarityEnabled(false)`.
- `perBandDbMaxRequested(-30)` calls `setPerBandDbMax(-30)`.
- In `MainWindow`: `applyFftBaseline` is `MainWindow::applyFftBaseline` (Task 7);
  FPS, window, Hz/bin and decimation go to the pool setters and write their station
  keys once; in a remote window the same keys are written through the station
  settings path and the planner re-asks on its own tick. `setPerBandDbMax` /
  `setPerBandDbMin` call `PanModels::model(panId)->setPerBandDbMax(band, db)` for the
  pan's current band, and `PanModels::bandGridChanged(panId)` calls the panel's
  `setBandGrid` with the band's label and values. The scheme adapter lambda
  (`MainWindow.cpp:6359-6364`) and the one-off `setGridVisible` seed are gone.
- Station availability changes call `setStationRowsAvailable` on every pan's panel;
  `setRemoteWindow(!ownsLocalDsp(), spectrumDecimationAvailable())` is set when the
  panel is wired and when the capability changes.
- A change to a station FFT key from Setup or another window refreshes every open
  pill's FFT tab.

**Verification:** functional wiring: integration evidence through the hook test plus
a build; the `MainWindow` side is checked by reading the diff and by the running
window in the captures. Build and run `tst_display_pill_wiring`,
`tst_spectrum_overlay_panel`; build `NereusSDR`.

**Execution note (advisory):** opus. After Tasks 7, 9, 16, 17.

- [ ] **Step 1:** `DisplayPillWiring` and its test.
- [ ] **Step 2:** `MainWindow` builds the hooks per pan (local and remote), wires
  station availability, the band grid and the remote gates, and refreshes FFT state
  on engine and key changes.

## Task 19: Trim the right-click pan menu

**Requirements:** D3 (menu), D33, S9; R-DSP-07 (menu), R-DSP-45; spec section 7.

**Files:**
- Modify: `src/gui/SpectrumOverlayMenu.{h,cpp}`, `src/gui/SpectrumWidget.{h,cpp}`
  (`:10026-10182`), `src/gui/MainWindow.cpp`
- Test: `tests/tst_dss_overlay_menu.cpp`, `tests/tst_display_popup_binding.cpp`

**Interfaces:**
- Consumes: `SpectrumOverlayPanel::openDisplayPill()` (Task 16); `PanModels`
  (Task 9).
- Produces:
  - `SpectrumOverlayMenu` keeps CTUN, Add notch here, Ref Level and Dyn Range and
    gains one "Display settings..." line with signal `void displaySettingsRequested()`.
    `setValues` shrinks to `(float refLevel, float dynRange, bool ctun = true)`;
    `setDssValues`, `setDssRowSpanSupported`, the scheme, gain, black, fill and seven
    3D signals are removed.
  - `SpectrumWidget` signals `void displayPillRequested()`, emitted for the menu
    line, and `void bandRangeRequested(float minDbm, float maxDbm)`, emitted when the
    menu's Ref Level or Dyn Range changes (the widget no longer stores the range
    itself). `MainWindow` routes the first to
    `m_overlayPanels[panId]->openDisplayPill()` and the second to the pan's model:
    `setPerBandDbMax(band, maxDbm)` and `setPerBandDbMin(band, minDbm)` for its
    current band.

**Acceptance:**
- The menu holds exactly CTUN, Add notch here, Ref Level, Dyn Range and
  "Display settings..."; no scheme, gain, black, fill or 3D control exists in it
  (`findChildren` by type and name).
- Choosing "Display settings..." emits `displaySettingsRequested()`; from a real
  `SpectrumWidget` it emits `displayPillRequested()`.
- Changing Ref Level in the menu emits `bandRangeRequested` with the new ceiling and
  the unchanged span; with a `PanModels` model wired, the current band's stored dB
  Max changes and the widget's range follows.
- The removed rows' model connections in `SpectrumWidget.cpp:10041-10152` are gone
  and the widget still seeds Ref Level, Dyn Range and CTUN on open.

**Verification:** UI change: functional tests offscreen plus one capture of the
trimmed menu from the running window. Build and run `tst_dss_overlay_menu`,
`tst_display_popup_binding`; build `NereusSDR`.

**Execution note (advisory):** opus. After Tasks 9, 16.

- [ ] **Step 1:** Rewrite the menu tests to the trimmed set.
- [ ] **Step 2:** Trim the menu and its seeding; add the line and the signals; route
  them in `MainWindow`.

## Task 20: Setup's FFT controls

**Requirements:** D22, D23, D24, D26 (Setup FFT half); R-DSP-29, R-DSP-31, R-DSP-32,
R-DSP-33 (Setup), R-DSP-61; P9; issue #296.

**Files:**
- Modify: `src/gui/setup/DisplaySetupPages.{h,cpp}` (`:580-590`, `:675-764`, `:773`,
  `:192-193`, `:310-327`, `:1418-1421`), `src/gui/SetupDialog.{h,cpp}`,
  `src/gui/MainWindow.cpp` (`refreshFftPoolConfig` at `:5328-5359`)
- Test: `tests/tst_display_setup_binding.cpp`

**Interfaces:**
- Consumes: `FftEnginePool` setters and `MainWindow::applyFftBaseline` (Task 7);
  `planAutoZoomFft` (Task 6).
- Produces: `SetupDialog::setFftBaselineHandler(std::function<void(int)> handler)`,
  which `MainWindow` sets to `applyFftBaseline`; pages reach it through `SetupPage`.

**Acceptance:**
- Dragging the FFT size slider across four positions and releasing causes exactly one
  baseline change and one replan; no `setFftSize` call happens during the drag.
- Three arrow-key presses within 300 ms cause one baseline change, 300 ms after the
  last.
- The slider handler sets the baseline only (through the handler), never the running
  size directly.
- The page opens on `fftSizeBaseline()`: with baseline 8192 and a zoomed running size
  of 65536 the slider shows 8192.
- Stored `DisplayFftSize=32768`: after a simulated restart (`refreshFftPoolConfig`
  then a new page) the engine baseline is 32768 and the slider shows 32768.
- Stored `DisplayFftSize=5000` (invalid): the baseline falls back to the default 4096,
  the key is rewritten to 4096 and one `qCWarning` is logged, instead of a silent
  mismatch between the key and the engine.
- Issue #296: the report states whether the reset to 16384 was reproduced on this
  branch and what the cause was; if it cannot be reproduced after a real attempt
  (restart with the stored key at each slider position), it says so and names the two
  fixes above as the covered causes. "16384" in the issue matches 4096 zoomed x4 at
  192 kHz shown by the old page load; say whether that is confirmed.
- Window, Hz/bin, FPS and thread priority act on the pool, so a second stream's engine
  receives them.
- The readout line shows bin width and time per transform for the running size, and
  "Hz/bin target is setting the size" when `planAutoZoomFft(...).hzPerBinInCharge`;
  in a remote window it uses `remoteGrantedFftSize()` as today.

**Verification:** bug fix: reproduce first (the page-load and invalid-value tests fail
on main). Build and run `tst_display_setup_binding`, `tst_fft_engine_pool`; build
`NereusSDR`.

**Execution note (advisory):** opus. After Task 7.

- [ ] **Step 1:** Tests for release-only, key debounce, page load, restart, invalid
  value.
- [ ] **Step 2:** `setTracking(false)`, the 300 ms key timer, the baseline handler,
  pool calls, load from the baseline, validation in `refreshFftPoolConfig`.
- [ ] **Step 3:** The #296 reproduction attempt and its written result.

## Task 21: Setup's Display pages: defaults for every pan, live follow, greying

**Requirements:** D3 (Setup), D13 (Setup), D26, D27, S4, S11, S12; R-DSP-07 (Setup),
R-DSP-18 (Setup), R-DSP-35, R-DSP-36, R-DSP-57 (Setup); P10, P11.

**Files:**
- Modify: `src/gui/setup/DisplaySetupPages.{h,cpp}`,
  `src/gui/setup/AppearanceSetupPages.cpp` (`:50-74`), `src/gui/SetupDialog.{h,cpp}`,
  `src/gui/MainWindow.cpp`
- Test: `tests/tst_display_setup_binding.cpp`

**Interfaces:**
- Consumes: `PanDisplayKeys` (Task 1), `displaySettingChanged` and
  `clarityActiveChanged` (Task 2), the trace colour (Task 5), `PanModels` (Task 9).
- Produces:
  - `SetupDialog::setPanWidgetsProvider(std::function<QList<SpectrumWidget*>()> provider)`,
    set by `MainWindow` to every pan's widget; `SetupPage` exposes it to pages as
    `panWidgets()`.
  - `SetupDialog::setBandGridModelProvider(std::function<PanadapterModel*()> provider)`,
    set by `MainWindow` to the active pan's model from `PanModels`; it replaces
    `firstPan`.
  - A page helper,
    `void applyDisplayDefault(const QString& keyBase, const QVariant& value, const std::function<void(SpectrumWidget*)>& apply)`:
    writes `PanDisplayKeys::writeDefault(keyBase, value)`, then calls `apply` on every
    pan widget for which `PanDisplayKeys::hasOverride(keyBase, panIndex)` is false.

**Acceptance:**
- Two pans, pan 1 with its own `DisplayWfOpacity_1 = 60`: changing Waterfall
  Defaults' opacity to 90 writes the bare `DisplayWfOpacity = 90`, pan 0 shows 90,
  pan 1 stays 60, and no `DisplayWfOpacity_0` key is written.
- Every per-pan control on Spectrum Defaults, Waterfall Defaults, Grid & Scales and
  3D View uses `applyDisplayDefault`; none calls a setter on only the active widget,
  and `firstPan` is gone (`:2115-2120` and its uses at `:2154`, `:2265`, `:2282`,
  `:2299`, `:2310`).
- Each page shows the Setup default (`PanDisplayKeys::read` of the default key), not
  the active pan's override.
- Live follow: with a page open, a default changed from another surface updates the
  control under `QSignalBlocker`; the page writes nothing back. The 3D View page's
  existing subscriptions (`:3334-3379`) are the model.
- Waterfall Defaults: Low, High, AGC, NF-AGC and NF offset are disabled while Clarity
  drives the active pan's levels, with
  "Clarity is setting the levels. Turn it off to use these."; while
  "Use spectrum min/max" is on, with
  "The spectrum's dB Max and Min are setting the levels."; live only when neither
  owns the levels. NF-AGC rows are disabled while AGC is off.
- The scheme combo still lists the eight `wfSchemeName()` entries.
- Line width is 0.5 to 3 px in 0.5 steps and loads 1.5 as 1.5 (not truncated to 1).
- Appearance has separate Trace and Fill colour buttons; each opens on the current
  default colour and applies through `applyDisplayDefault`.
- Grid & Scales' per-band dB Max and dB Min write the band store through the
  provider's model and move every pan on that band; with no pan the rows are disabled
  with a note, not hidden.
- Station controls keep their existing gate (`gateStationControls`).

**Verification:** ordinary feature with a spill risk between pans (spec section 12):
the two-pan override test is written first. Build and run
`tst_display_setup_binding`, `tst_pan_display_keys`, `tst_remote_pan_grid`; build
`NereusSDR`. One capture of Waterfall Defaults with Clarity on, from the running
window.

**Execution note (advisory):** opus. After Tasks 1, 2, 5, 9, 20. Large file.

- [ ] **Step 1:** Tests: two-pan default push, page shows defaults, live follow,
  greying states, line width.
- [ ] **Step 2:** Providers, `panWidgets()`, `applyDisplayDefault`; convert each page.
- [ ] **Step 3:** Live subscriptions per page; Clarity and min/max greying; line
  width; Appearance colours; Grid & Scales on the band store.

## Task 22: VFO flag meters without an S-meter

**Requirements:** D35; R-DSP-47; P15.

**Files:**
- Modify: `src/gui/meters/MeterPoller.cpp` (`pollRemoteRxMeters`, `:1112-1200`)
- Test: `tests/tst_remote_meter_poller.cpp`

**Interfaces:**
- Consumes: nothing. Produces: nothing new.

**Acceptance:**
- Fixture as the existing one (`:111-146`) but without `setSMeter`: after a poll,
  `remoteSliceLevelUpdated(sliceIndex, dbm)` fires for each slice with the signal
  average reading (the same fallback `MainWindow.cpp:9365-9368` uses,
  `SliceMeterPump::MeterSource::SignalAverage`).
- With an S-meter set, the emitted values are exactly today's for each of its modes.
- No null dereference when `m_sMeter` is absent (the loop reads `m_sMeter->rxMode()`
  today).

**Verification:** bug fix: the no-S-meter case is written first and fails on main.
Build and run `tst_remote_meter_poller`; build `NereusSDR`.

**Execution note (advisory):** opus. No prerequisites.

- [ ] **Step 1:** The failing test.
- [ ] **Step 2:** Move the early return below the per-flag loop and use the fallback
  mode when there is no S-meter.

## Task 23: The phone's Setup description gates

**Requirements:** D13 (phone), S12, S20; R-DSP-57 (phone); spec 8.3.

**Files:**
- Modify: `src/core/setup/SetupDescriptionService.cpp`, `resources/setup/display.json`,
  `ios/NereusKit/Sources/NereusMirror/` (a new closed table beside
  `SetupDescription+V12.swift`, and `SetupDescription+ClosedMetadata.swift`),
  `docs/architecture/2026-09-23-setup-description-v1.md` (`:384-413`)
- Test: `tests/tst_setup_description_service.cpp`, `tests/tst_setup_description_parity.cpp`,
  `ios/NereusKit/Tests/NereusMirrorTests/`

**Interfaces:**
- Consumes: nothing from other tasks.
- Produces: Display description version 26 (the next after the highest today, 25).
  A new closed form of `enabledWhen`:
  `{"allOf":[{"phone":<key>,"oneOf":[<values>]}, ...]}`, enabling a row only while
  every term holds. A phone that asks for a version below 26 receives the version 12
  Display rows unchanged.
- The version 26 rows that change, all on Waterfall Defaults:
  - High Threshold, Low Threshold, AGC: `allOf` of `ClarityEnabled` one of `[false]`
    and `DisplayWfUseSpectrumMinMax` one of `[false]`.
  - Enable NF-AGC, NF offset: those two terms plus `DisplayWfAgc` one of `[true]`.
  - Use spectrum min/max: `enabledWhen` `ClarityEnabled` one of `[false]` (single
    form).
- `display.json` has no Black Level or Color Gain rows, so the spec's "Black and Gain
  while Clarity is off" gate has nothing to attach to; the report records that and
  Task 24 notes it in the parity doc.

**Acceptance:**
- A phone asking for version 26 gets Display at 26 with the rows above; asking for 25
  or 12 gets the version 12 rows byte for byte as today.
- `resources/setup/display.json` carries version 26 and exactly the object the
  service's version 26 table serves (the parity test's rule).
- The Core's closed-shape check accepts the `allOf` form only for Display at 26 and
  rejects an `allOf` with zero terms, a term with extra keys, or nesting.
- The control counts pinned in `tst_setup_description_service.cpp` (`:3813`, `:3967`)
  are unchanged; the version pins (`:4211`, `:4621`) move to 26 where they mean
  "latest".
- iOS: the parser accepts the version 26 rows and exposes each term; a row is enabled
  only when every term holds; version 12 parsing is unchanged; a malformed `allOf`
  makes the row fail closed as other malformed rows do.
- The description doc defines `allOf` beside `enabledWhen` and lists the gated rows.

**Verification:** wire format between the Core and the phone: tests first on both
sides. Follow the pattern used when another category moved versions (Hardware 18 to
23 in `SetupDescriptionService.cpp` near `:3205-3230`). Build and run
`tst_setup_description_service`, `tst_setup_description_parity`; then
`ios/scripts/swift-test.sh --filter NereusMirrorTests` (or `swift test --filter
NereusMirrorTests` in `ios/NereusKit`); build `nereusd`. No phone build is uploaded
or installed. Hardware: pending (a phone against a Core).

**Execution note (advisory):** opus. No prerequisites. Touches the station wire
protocol surface for Setup; no link message changes.

- [ ] **Step 1:** Core tests for versions 12, 25 and 26 and the closed-shape rules;
  iOS tests for the new rows.
- [ ] **Step 2:** The version 26 Display table and ceiling logic; `display.json`.
- [ ] **Step 3:** The iOS closed table and dependency evaluation.
- [ ] **Step 4:** The description doc.

## Task 24: Docs

**Requirements:** D36; R-DSP-50, R-DSP-44 (final check); spec sections 8.5 and 9.

**Files:**
- Modify: `docs/architecture/waterfall-tuning.md` (`:157-170`),
  `docs/architecture/2026-09-23-iphone-app-display-parity.md` (`:18` and new rows),
  `docs/architecture/tx-display-settings-master-plan.md` (`:431`),
  `docs/architecture/comprehensive-ui-port-design.md` (`:72-93`),
  `docs/architecture/tx-display-attempt2-design.md` (`:141`),
  `docs/architecture/tx-display-brainstorm-handoff.md` (`:89-102`),
  `docs/architecture/2026-09-24-remote-window-parity-plan.md` (ruling C12, `:2425-2431`),
  `CHANGELOG.md`

**Interfaces:** consumes the finished behaviour; produces nothing.

**Acceptance:**
- `waterfall-tuning.md`: Clarity precedence now reads: AGC, NF-AGC, Low and High are
  greyed under Clarity; Clarity holds its level while paused and through a key; the
  Black/Gain model with its scales and defaults; a link to the spec.
- iPhone parity doc: row 1 is re-opened with the new Black/Gain scale, the
  window-only conversion and the greying while Clarity is off; new rows for NB Blank,
  Background (Choose, Clear, Off, opacity, colour), Grid spacing, Scale text and trace
  colour, each marked for the phone's own Display panel work; the Clarity and
  "Use spectrum min/max" gates of Task 23; a note that the phone's Setup description
  has no Black or Gain rows to gate; its stale line cites refreshed.
- The three TX display docs and the UI port design each carry a dated one-line
  "superseded by" note pointing at the spec section named in spec section 9.
- The remote parity plan's C12 ruling carries a dated note: the Core has no pan
  object, each window pan applies the Core's per-band values itself, and pans are not
  mirrored on the link.
- `CHANGELOG.md` has an entry under the unreleased heading in the file's own style,
  in operator words, covering the pill, the level model, un-key, the FFT slider and
  the fixes.
- No em dash in any added text. `python3 scripts/verify-provenance-sync.py` and
  `python3 scripts/check-new-ports.py` pass on the branch.

**Verification:** documentation: read-through against the spec and the verifier
scripts. No tests.

**Execution note (advisory):** sonnet. After every other task.

- [ ] **Step 1:** Edit each doc as above.
- [ ] **Step 2:** Run the provenance scripts and report their output.

## Final checks (once, on the finished branch)

- `cmake --build build --target all_tests`, then
  `ctest --test-dir build -LE realtime --output-on-failure`, then
  `ctest --test-dir build -L realtime --output-on-failure` alone. A `realtime` failure
  under load is rerun alone before anything is called done.
- The pre-commit and CI checks that only CI runs for a worktree: every
  `scripts/verify-*.py` and `scripts/check-new-ports.py` the primary hook lists.
- Build `NereusSDR` and `nereusd`.
- Captures, from a running window with no radio and no Core: the pill's four tabs
  with Clarity on and off beside the mockups, the trimmed right-click menu, Setup's
  Waterfall Defaults. The Dev.app bundle is not replaced without JJ's OK.
- Bench items stay pending for JJ (listed under "Task order and flags").
