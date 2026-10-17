# Container Headers, Arranging and Placement Implementation Plan

> **Execution:** run with `crew` (`/crew <this file>`) under `cost-aware-execution`.
> Requirements and acceptance cases are binding; test order and review effort follow
> the risk-based policy; UI evidence follows `ui-verification`. No review between
> tasks; one whole-branch review at the end.

**Goal:** containers start where they were left, and the workspace look gets
two header states, a Shift-held Arrange, one drag rule, a pin button and a
return-to-origin, with Lock removed.

**Architecture:** placement first. Each container gets a remembered place that
only the operator's moves change; what is drawn is derived from it, and only
the remembered place is saved. Floating windows also remember their monitor,
through an injectable screen source so tests can fake monitors. The look's
chrome changes (header, Arrange, drag, pin, return) are built on top, all
gated by the existing workspace look switch.

**Tech Stack:** C++20, Qt6 Widgets, QtTest. Code in `src/gui/containers/` and
`src/gui/MainWindow.cpp`. Tests in `tests/`, registered with
`nereus_add_test` in `tests/CMakeLists.txt`. Scenes for the capture tool in
`tests/visual/`.

**Spec:** [2026-10-10-container-arrange-design.md](2026-10-10-container-arrange-design.md).
Every decision is settled there; this is how it gets built.

## Global Constraints

- Read `CLAUDE.md`, `CONTRIBUTING.md` and
  `docs/development/fast-test-loop.md` first. C++20/Qt6. No `goto`, no raw
  `new`/`delete` (unique_ptr or Qt parent), no `#define` constants
  (`constexpr`), braces on all control flow, `auto` only when the type is
  obvious. `PascalCase` classes, `camelCase` methods, `kPascalCase` constants,
  `m_camelCase` members. `Q_OS_WIN` / `Q_OS_MAC` / `Q_OS_LINUX` guards.
  Errors are `qCWarning(lcCategory)`, no exceptions. Don't remove code you
  didn't add, except what a task names for removal.
- Rule R1: nothing under `src/core/` or `src/models/` includes a GUI header.
- `AppSettings`, never `QSettings`. Never read or write `~/.config/NereusSDR`
  or `~/Library/Preferences/NereusSDR/`. Tests use `QTemporaryDir` settings or
  `RemoteWindowHarness::useIsolatedProfile`.
- Never connect to or key a radio. Never call `MoxController::setMox`. Never
  start discovery or any connection (4O3A PGXL/TGXL, RF-Kit, TCI, CAT,
  rendezvous, remote Core). A `MainWindow` in a test is built with
  `MainWindow::ConnectionStartup::Deferred`.
- With the workspace look off (`WorkspacePreview` disabled), chrome, menus and
  behaviour are unchanged, except the placement rule of Tasks 1 and 2, which
  fixes a bug both looks share. Every look-off scene of the capture tool stays
  pixel-identical unless a task says otherwise.
- This is NereusSDR's own code, not a port. New files carry
  `// no-port-check: NereusSDR-original <purpose>.` in the header. Files that
  already carry a "Modification history (NereusSDR)" block get a dated line
  (2026-10-11, J.J. Boyd, AI tooling Claude), in date order. Ported files
  touched in passing keep every upstream header line and inline comment.
- User-visible strings carry no file or source cites. Plain operator wording.
- Build dir `build/` is configured offline. Never run `cmake -B` or delete it.
  Build single targets only (`cmake --build build --target <t> -j4`), and only
  while at most one other build runs. Do not build the `NereusSDR` app target.
- Tests run offscreen: prefix a direct binary with `QT_QPA_PLATFORM=offscreen`;
  never export it over `ctest`. No native-window test (`NATIVE_WINDOW` label)
  is run by an implementer. Never use `timeout` (zsh has none); never
  `git stash`; never `pkill` or `killall`; stop only processes you started, by
  PID. Wait on other processes with `pgrep`, never by grepping `ps` for text
  your own command line holds.
- Never start a capture (`tests/visual/capture.py`) or
  `tst_workspace_colour_sweep` while another runs
  (`pgrep -fl 'capture\.py --binary|tst_workspace_colour_sweep'`). Captures use
  `--jobs 4` from a frozen copy of the preview binary.
- Never quit, focus or script any NereusSDR process you did not start.
- Before each commit that changes code, run by hand and get exit 0 from:
  `scripts/verify-no-image-cursors.py`, `scripts/verify-test-registration.py`,
  `scripts/check-third-party-licenses.py`, `scripts/check-new-ports.py`,
  `scripts/verify-inline-tag-preservation.py`, `scripts/verify-inline-cites.py`,
  `scripts/compliance-inventory.py --fail-on-unclassified`,
  `scripts/verify-thetis-headers.py --all-kinds`. If a name differs, find the
  script the pre-commit hook runs and use that.
- Commits are GPG-signed (`git commit -S`, never `--no-gpg-sign`, never
  `--no-verify`), with no Co-Authored-By trailer and no "Generated with" line.
  Conventional prefix, plain prose body. No em-dashes in anything you write.
  No CHANGELOG line. No push.

## What already exists

Paths are under `src/gui/containers/` unless given in full.

- `ContainerDocument` (`ContainerDocument.h`): `id`, `name`, `contents`,
  `layout`, `dockMode`, `edge`, `anchor` (`AxisLock`), `header`
  (`HeaderMode`), `QRect geometry`, `visible`, `locked`, `autoHeight`,
  `popOutShell`, `config`, `extensions`. `WorkspaceDocument` holds
  `schemaVersion = 1`, `containers`, `arrangement`
  (`WorkspaceArrangement`: `strips`, `gridAlignment`, `extensions`).
- `ContainerDocumentCodec` (`decode`, `encode`, `validate`). Unknown fields
  are kept in `extensions`. A missing optional field keeps the struct default.
  A wrong-typed field fails the whole document. A schema version other than 1
  is refused and the store goes read-only. A new typed field must be added to
  `kContainerFields`, or `validate` rejects it as a collision with
  `extensions`. `edge` is the precedent for a field written only when set.
- `ContainerWorkspaceStore`: loads and commits the document under the key
  `ContainerWorkspace` (look off) or `ContainerWorkspacePreview` (look on).
  When the look-on key is absent, `load()` uses
  `LegacyContainerImporter::fromSettings`, which returns the look-off document
  whole.
- `ContainerManager`:
  - `restoreState()` calls `reconcileWorkspace(snapshot)`. Reconcile applies
    `d.geometry`, calls `FloatingContainer::ensureVisiblePosition(m_dockParent)`
    for floating containers, and for free (`OverlayDocked`) containers clamps
    with `clampedGeometry(...)` into `m_dockParent->rect()`, then
    `storeLocation()` and `setDelta(...)`.
  - `saveState()` writes, for every container, the floating form's
    `geometry()` or the widget's `geometry()` into `d.geometry`. Callers: the
    200 ms `m_geometryCommit` timer, `saveSplitterState`,
    `MainWindow::closeEvent`, the `aboutToQuit` hook, and
    `WorkspaceProfiles::captureLive`.
  - `eventFilter` arms the timer on any `QEvent::Move` or `QEvent::Resize` of
    a container or its floating form, unless `m_reconciling` or
    `m_geometryCaptureSuppressed`. `geometryInteractionFinished` (emitted by
    `ContainerWidget` at the end of a drag or a grip resize) arms it too.
  - `updateDockedPositions(int hDelta, int vDelta)`, called from
    `MainWindow::resizeEvent`, moves free containers by their `AxisLock`
    anchor and clamps them into the pane.
  - A `QGuiApplication::screenRemoved` lambda clamps floating rects to a
    remaining screen and commits them. There is no `screenAdded` handler.
- `FloatingContainer` is a frameless `Qt::Window`; its `ensureVisiblePosition`
  recentres any window not wholly inside one screen's `availableGeometry()`.
- `MainWindow` constructor: `restoreMainWindowGeometry()`, then the spectrum
  pane (`spectrumPane`, the manager's `m_dockParent`) is created, then
  `ContainerManager::restoreState()`, all before the window is shown.
- Test helpers: `QTemporaryDir` + `AppSettings settings(path)` +
  `ContainerWorkspaceStore store(settings)` +
  `ContainerManager manager(&dock, &splitter)` + `setWorkspaceAdapter` +
  `restoreState()` (see `tests/tst_container_position_restore.cpp`); fixtures
  `EdgeFixture` (`tests/tst_workspace_edge_strips.cpp`) and `ProfilesFixture`
  (`tests/tst_workspace_profiles.cpp`); a full offscreen `MainWindow` as
  `PanelWindow` in `tests/tst_workspace_main_panel_fit.cpp`, with
  `RemoteWindowHarness::useIsolatedProfile(name)` and
  `WorkspacePreview::setEnabledForTest(bool)`.

## Task 1: Containers keep the place the operator gave them

**Requirements:** spec "Where things are on startup": remembered place and
shown place are separate; only the operator's moves are saved; free
containers are placed once the pane has its real size; a window too small for
a free container; look off to look on. Spec "The stacking bug" causes 1, 3
and 6. Acceptance item 6, first, second and last cases.

**Files:**
- Modify: `src/gui/containers/ContainerManager.{h,cpp}` (remembered place,
  save path, restore timing, `updateDockedPositions`)
- Modify: `src/gui/containers/ContainerWidget.{h,cpp}` only if the operator
  move signal needs to carry more than it does
- Modify: `src/gui/MainWindow.cpp` (when free containers are positioned)
- Modify: `src/gui/containers/WorkspaceProfiles.cpp` if `captureLive` needs a
  change to capture remembered places
- Test: `tests/tst_container_remembered_place.cpp` (new, manager level)
- Test: `tests/tst_mainwindow_container_restart.cpp` (new, full `MainWindow`)
- Modify: `tests/CMakeLists.txt` (register both; the `MainWindow` test adds
  `fakes/RemoteWindowHarness.cpp` as `tst_workspace_main_panel_fit` does)

**Interfaces:**
- Consumes: nothing.
- Produces, for Task 2 and later tasks:
  - In `ContainerManager`, one remembered rect per container, kept in the
    document's existing `geometry` field (no new saved field for docked or
    free containers). It changes only through:
    `void ContainerManager::rememberOperatorGeometry(const QString& id)`,
    called when the operator finishes a drag or a grip resize
    (`geometryInteractionFinished`), and through document commits made by
    menu commands and dialogs.
  - `saveState()` writes each container's remembered rect, never
    `widget->geometry()`.
  - A free container's remembered place is anchor-relative as today
    (`storeLocation()` / `delta()` / `axisLock()`); those values are
    re-based only by an operator move.
  - `void ContainerManager::placeFreeContainers()`: positions every free
    container from its remembered place into the dock parent's current rect,
    for display only. Safe to call repeatedly.

**Acceptance:**
- Manager level, dock parent 1000x700, two free containers saved at
  (100, 80, 300, 200) and (520, 300, 320, 240): after `restoreState()` with
  the dock parent still at its unlaid-out size, then the dock parent resized
  to 1000x700 and events processed for 300 ms, each widget's `geometry()`
  equals its saved rect, and the store's document is unchanged
  (`ContainerDocumentCodec::encode` of the snapshot is byte-identical to
  before `restoreState()`).
- Same two containers, dock parent shrunk to 500x300 and
  `updateDockedPositions` called: each widget is wholly inside the dock
  parent; after 300 ms the stored document is unchanged; dock parent back to
  1000x700 and `updateDockedPositions` called: each widget's `geometry()`
  equals its saved rect again.
- An `AxisLock::Right` free container saved at x = 600 in a 1000-wide pane:
  pane widened to 1200 gives x = 800 on screen, and the stored document is
  unchanged; narrowed back to 1000 gives x = 600.
- An operator move still saves: drive the container's drag end the way
  `tests/tst_container_position_restore.cpp` or the edge-strip tests do, move
  it by (40, 30), wait 300 ms: the stored geometry moved by exactly (40, 30).
  Same for a grip resize.
- `autoHeight` and strip splitter sizes still save after an operator change
  (the existing tests that cover them stay green; add a case if none asserts
  the save).
- Look off to look on: a document committed under look off with a free
  container at (600, 400, 300, 200); `WorkspacePreview::setEnabledForTest(true)`;
  a new store and manager over a 500-wide dock parent; `restoreState()`,
  `saveState()`: if the look-on key now exists its container geometry is
  (600, 400, 300, 200); it is never the clamped rect.
- Full `MainWindow`, isolated profile, look on and again with look off: commit
  two free containers at distinct rects inside the pane the window will have;
  construct the window with `ConnectionStartup::Deferred`, show it, process
  events for 400 ms, close it. The stored document's containers are
  byte-identical to what was committed, and while shown the two widgets do
  not overlap.
- Red first: the first, second, sixth and seventh cases fail on the base
  commit. Record in the report what the dock parent's size was at restore on
  the base (the spec's cause 1 says it is the unlaid-out default) and whether
  the full `MainWindow` case reproduced stacking; if it did not, say exactly
  what the base did instead.
- `tst_container_position_restore`, `tst_container_persistence`,
  `tst_container_workspace_store`, `tst_container_document`,
  `tst_container_legacy_import`, `tst_container_application_quit`,
  `tst_workspace_profiles`, `tst_workspace_edge_strips`,
  `tst_container_chrome` pass. A workspace profile captured live holds
  remembered places.

**Verification:** consequential state (a saved layout the operator relies
on): regression tests in place and red before the behaviour changes. Unit and
integration layers, offscreen. Commands, per test:
`cmake --build build --target <test> -j4 && QT_QPA_PLATFORM=offscreen build/tests/<test>`.
Human smoke, pending until JJ does it on a build of his choosing: arrange
free containers, quit and start three times, nothing moved.

**Execution note (advisory):** opus. Touches saved layout state; worth an
early look by the controller at the report's red-phase findings before Task 2
starts. Prerequisite: none. Not parallel with any other task (shared files).

- [ ] **Step 1:** Write the tests above. Run them on the base and record
  which are red and the measured dock parent size at restore.
- [ ] **Step 2:** Make the remembered rect the only thing `saveState()`
  writes. The `Move`/`Resize` arm in `eventFilter` no longer changes any
  remembered place; operator drags and grip resizes do, through
  `rememberOperatorGeometry`. Keep whatever the `Move`/`Resize` arm was
  needed for besides geometry (auto-height, splitter and strip sizes) saving
  as before; say in the report how each is still saved.
- [ ] **Step 3:** Reconcile no longer clamps a free container's remembered
  place. Free containers are created at restore and positioned by
  `placeFreeContainers()` once the dock parent has received its first real
  resize after the window is laid out, and again whenever it resizes.
  Clamping into the pane and `AxisLock` anchoring happen there, for display
  only.
- [ ] **Step 4:** `updateDockedPositions` derives shown positions from
  remembered places; it never re-bases them.
- [ ] **Step 5:** Tests green, the listed regression tests green, the eight
  checks, signed commit(s).

## Task 2: Windows remember their monitor, and no two share a default spot

**Requirements:** spec "Where things are on startup": popped-out and floating
windows remember their monitor; unplugging a monitor while running; two
windows never get the same default spot. Spec "Saved layout": the monitor
field. Spec "Errors", first two items. Spec "The stacking bug" causes 2, 4
and 5. Acceptance item 6, third and fourth cases.

**Files:**
- Create: `src/gui/containers/ScreenSource.{h,cpp}` (the list of screens the
  containers code consults)
- Modify: `src/gui/containers/ContainerDocument.h`,
  `ContainerDocumentCodec.cpp` (the monitor field)
- Modify: `src/gui/containers/FloatingContainer.{h,cpp}`
  (`ensureVisiblePosition`)
- Modify: `src/gui/containers/ContainerManager.{h,cpp}` (restore, screen
  removed and added, default placements)
- Modify: `src/gui/containers/ContainerArrangeController.cpp` (pop-out shell
  default rect)
- Modify: `CMakeLists.txt` or the source list that names the containers files
- Test: `tests/tst_container_window_monitors.cpp` (new)
- Modify: `tests/tst_container_document.cpp` (codec cases),
  `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: from Task 1, `saveState()` writes remembered rects only;
  `rememberOperatorGeometry(const QString& id)`.
- Produces:
  - `struct ScreenInfo { QString name; QString serial; QRect geometry; QRect available; };`
  - `class ScreenSource` with `virtual QVector<ScreenInfo> screens() const`,
    `virtual ScreenInfo primary() const`, a default implementation over
    `QGuiApplication`, and
    `void ContainerManager::setScreenSourceForTest(ScreenSource*)` (the
    manager passes its source to `FloatingContainer` calls).
  - `struct SavedScreen { QString name; QString serial; QRect geometry; };` and
    `std::optional<SavedScreen> ContainerDocument::screen`, saved per
    container as the JSON object `"screen"` with keys `"name"`, `"serial"`,
    `"geometry"` (`[x,y,w,h]`), written only for floating and pop-out
    containers, added to `kContainerFields`. A `"screen"` or a floating
    `"geometry"` that fails to parse affects that container only: it takes a
    default place and a `qCWarning`; the document still loads.
  - A screen matches a `SavedScreen` when the serial is non-empty and equal,
    or else when name and geometry are both equal.
  - `QRect ContainerManager::defaultWindowRect(const QRect& wanted) const`:
    returns `wanted` moved down and right by `kCascadeOffsetPx` (24) as many
    times as needed until no visible free or floating container has the same
    top-left, kept on its screen.

**Acceptance:**
- Codec: a document with `"screen"` round-trips; a document without it loads
  with `screen` empty and is written without the key; a document whose
  `"screen"` is a string loads, with that container's `screen` empty and the
  others intact; a build without this change would keep `"screen"` in
  `extensions` (covered by the existing unknown-field test staying green).
- Two fake screens, A (0, 0, 1920x1080) and B (1920, 0, 1920x1080). A
  floating container saved at (2100, 200, 400, 300) with screen B: after
  `restoreState()` its form's `geometry()` is exactly that rect.
- The same container saved at (1800, 200, 400, 300), straddling A and B, with
  screen A present: restored exactly there. No recentring.
- Screen B absent at start, two floating containers saved on B at distinct
  rects: both are shown wholly on screen A, at different top-lefts; after
  `saveState()` the stored geometry and `screen` of both are unchanged. A new
  manager started with B present puts both exactly at their saved rects.
- Screen removed while running: the same result as absent at start, and the
  stored document is unchanged. Screen added back while running: each window
  whose saved screen now matches moves to its saved rect.
- A floating container with no saved `screen` (an older document) whose rect
  is on a present screen is restored exactly there; one whose rect is on no
  screen is shown on the main window's screen and its stored geometry is
  unchanged.
- An operator move of a floating window to (300, 300) on screen A stores
  geometry (300, 300, w, h) and `screen` A.
- Defaults: two pop-out shells created in a row have different top-lefts
  (the first at `QRect(140, 140, 360, 300)`, the second offset by 24, 24);
  two `Place Free` commands in a row give different top-lefts; two Recovers
  likewise; two new floating containers likewise.
- Red first: the straddling case, the absent-monitor pair and the defaults
  fail on Task 1's commit.
- All regression tests listed in Task 1 pass, plus the Task 1 tests.

**Verification:** consequential state: red tests first. Unit and integration,
offscreen, with the fake `ScreenSource`. Hardware evidence pending until JJ
observes it: with a window on each of his monitors, quit and start; unplug a
monitor, start, plug it back, start. `QScreen::serialNumber()` may be empty
on some systems, which is why the match falls back to name and geometry; say
in the report what it returns on this Mac.

**Execution note (advisory):** opus. Touches saved layout state. Prerequisite:
Task 1. Not parallel.

- [ ] **Step 1:** `ScreenSource` and the manager and floating-container code
  reading screens only through it. No behaviour change yet; existing tests
  green.
- [ ] **Step 2:** The codec field and its tests.
- [ ] **Step 3:** The tests above, red on the current behaviour.
- [ ] **Step 4:** Restore: a window whose saved screen is present, or whose
  rect intersects any present screen when no screen was saved, is placed
  exactly. Otherwise it is shown on the main window's screen through
  `defaultWindowRect`, and its remembered rect and `screen` are left alone.
  `ensureVisiblePosition` keeps its other callers working or is replaced.
- [ ] **Step 5:** Screen removed and screen added handlers follow the same
  rule and commit nothing.
- [ ] **Step 6:** Every default placement (pop-out shell, Place Free, strip
  pop-out by menu, Recover, new container, the `ensureVisiblePosition`
  fallback) goes through `defaultWindowRect` or its free-container
  equivalent, so no two land on one spot.
- [ ] **Step 7:** Tests green, the eight checks, signed commit(s).

## Task 3: Lock leaves the workspace look

**Requirements:** spec "Lock" and decision row "Lock"; veto list item "A saved
locked container loads unlocked". Scope: the look only; with the look off,
Lock works as it does today.

**Files:**
- Modify: `src/gui/containers/ContainerManager.{h,cpp}` (with the look on,
  every container is unlocked; the divider rule
  `edgeHandleTouchesLockedContainer`; the "Arrangement is locked" refusals)
- Modify: `src/gui/containers/ContainerWidget.cpp` (the look's Options menu
  and body menu lose "Lock"; nothing is disabled for lock in the look)
- Modify: `src/gui/containers/WorkspaceHeader.{h,cpp}` (the padlock
  `WorkspaceLockGlyph` / `m_lock` and `WorkspaceGroupHeader::setLocked` go)
- Modify: `src/gui/containers/ContainerSettingsDialog.cpp` (no Lock checkbox
  when the look is on)
- Modify: `src/gui/containers/ContainerDocumentCodec.cpp` (`"locked"` is
  written only when true)
- Modify: `src/gui/containers/ContainerArrangeController.cpp`,
  `ContainerContentHost.cpp`, `FreeCanvasSurface.cpp`,
  `ContainerPreviewWidget.cpp` only where the look's unlocked rule needs it
- Test: `tests/tst_workspace_no_lock.cpp` (new); update the look-on lock cases
  in `tests/tst_workspace_edge_strips.cpp` (`lockedContainerBlocksStripHandles`),
  `tests/tst_workspace_live_input.cpp`, `tests/tst_workspace_profiles.cpp`;
  `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: with `WorkspacePreview` enabled, `ContainerDocument::locked` is
  false for every container after load and in every commit;
  `WorkspaceGroupHeader` has no `setLocked` and no padlock. The projection at
  `ContainerContentHost.cpp` that sets `sceneDocument.locked` from
  `!m_arrange` (contents are not editable outside Arrange) is kept: it is not
  the Lock setting.

**Acceptance:**
- Look on: a document with two containers `"locked": true` loads; both report
  `isLocked() == false`; the strip divider beside one can be dragged (the
  case `lockedContainerBlocksStripHandles` used to forbid); Pop Out, Move to
  Edge and Place Free succeed on it; after any commit the encoded document
  has no `"locked"` key.
- Look on: neither the Options menu nor the body menu has an action whose
  text is "Lock"; the header has no padlock child; the settings dialog has no
  Lock checkbox.
- Look off: a locked container still refuses a drag, its menu still shows
  "Lock" checked, the settings dialog still has the checkbox, and
  `"locked": true` round-trips. `tst_container_arrangement`,
  `tst_container_chrome`, `tst_free_canvas`, `tst_container_preview`,
  `tst_container_settings_workflow`, `tst_container_legacy_import`,
  `tst_container_application_quit` (its "locked-quit" case) pass unchanged.
- Codec: `"locked": false` is not written; `"locked": true` is; a document
  without the key loads unlocked (`tst_container_document` updated).

**Verification:** ordinary feature removal behind the look switch: test the
new behaviour and the look-off regression. Unit and integration, offscreen.
UI: the scenes `header-always`, `arrange-on` and
`look-menu-container-context*` are recaptured in Task 8; no look-off scene
changes.

**Execution note (advisory):** opus. Prerequisite: Task 2 (shared files).
Not parallel.

- [ ] **Step 1:** Tests for the acceptance cases; the look-on ones are red.
- [ ] **Step 2:** One place decides it: with the look on, the manager treats
  and commits every container as unlocked. Remove the look's Lock UI and the
  padlock. Leave look-off paths alone.
- [ ] **Step 3:** Tests green, the listed regression tests, the eight checks,
  signed commit.

## Task 4: A header is shown or hidden, with an eye button

**Requirements:** spec "Headers"; decision rows "Header choices" and
"Changing it"; veto list items "A saved Reveal header loads as hidden" and
"Reveal controls leaves the right-click menu".

**Files:**
- Modify: `src/gui/containers/ContainerTypes.h` (`HeaderMode`)
- Modify: `src/gui/containers/ContainerDocumentCodec.cpp` (read 1 as hidden)
- Modify: `src/gui/containers/ContainerWidget.{h,cpp}` (visibility rule,
  menus, removal of the Reveal machinery)
- Modify: `src/gui/containers/WorkspaceHeader.{h,cpp}` (the eye button)
- Modify: `src/gui/containers/ContainerContentHost.cpp`,
  `ContainerSettingsDialog.cpp`, `LegacyContainerImporter.cpp` (two choices)
- Create: `resources/icons/workspace/eye.svg`,
  `resources/icons/workspace/eye-off.svg`, and their resource entries
- Modify: `tests/visual/PreviewScenes.cpp` (scenes), `tests/visual/README.md`
- Test: `tests/tst_workspace_header_modes.cpp` (rewritten cases),
  `tests/tst_container_document.cpp`

**Interfaces:**
- Consumes: from Task 3, `WorkspaceGroupHeader` without the padlock.
- Produces:
  - `enum class HeaderMode { Shown = 0, Hidden = 2 };` The saved integer is
    unchanged for both. The codec reads 1 as `Hidden` and never writes 1.
    Every use of `HeaderMode::Always` becomes `HeaderMode::Shown`.
  - `WorkspaceGroupHeader`: a checkable eye button to the left of Pop out,
    accessor `QAbstractButton* keepShownButton() const`, signal
    `void keepShownToggled(bool keepShown)`, setter
    `void setKeepShown(bool)`. It is created like the other header buttons
    (`makeButton`, `kGroupButtonWidth` x `kGroupButtonHeight`) and shows
    `eye.svg` when the header is kept shown, `eye-off.svg` when not, drawn in
    the header glyph colour.
  - A header is drawn when its mode is `Shown`, when the container is
    minimised, or while arranging (`m_arrangeActive`). While arranging, a
    `Hidden` header is drawn over the top of the content as today (it takes
    no layout space, so nothing shifts).
  - Removed from `ContainerWidget`: the Reveal constants, timers, opacity and
    shadow members, `trackRevealBand`, `setHeaderRevealed`, `headerHasFocus`,
    `recoverChrome` and `m_recoverChrome`, and the hover and focus call sites
    that served them. `setHeaderRevealedForPreview` goes with its scenes. The
    look-off Shift handling in `eventFilter` and `updateChrome` stays.

**Acceptance:**
- Codec: `"header": 0` loads `Shown`, `2` loads `Hidden`, `1` loads `Hidden`
  and is written back as `2`; 3 is rejected as before.
- Clicking the eye on one of three containers changes that container's
  committed `header` only. The body menu's "Keep Header Showing" is checked
  exactly when the mode is `Shown` and toggles the same value.
- The body menu has no "Reveal controls" and no "Header" submenu; the Options
  menu has no "Header" submenu.
- Not arranging: a `Hidden` container's header is not visible and
  `headerExtent()` is 0; hovering the top of it for 2 s shows nothing.
  Arranging: every container's header is visible; leaving Arrange restores
  each to its own mode, and no document commit happened.
- The eye's tooltip is "Hide this header when not arranging" when the header
  is kept shown and "Keep this header showing" when not.
- The settings dialog offers two header choices. If that combo is visible
  with the look off, it offers two there too; say so in the report and list
  the look-off scene that changes.
- A header button (eye included) never starts a drag (the existing case in
  `tst_workspace_header_modes` covers the others; add the eye).
- Scenes: `header-always` renamed `header-shown`; `header-hidden`;
  `arrange-on` (now with a mix of kept and hidden headers so both eye states
  show); `header-reveal-idle`, `header-reveal-shown` and
  `clean-spectrum-reveal` removed or repointed, with `tests/visual/README.md`
  updated. A 5x crop of the eye in both states at both profiles is read by
  the implementer and described in the report.

**Verification:** UI interaction: `ui-verification`. Functional tests for the
toggle and the visibility rule, offscreen; rendered evidence from the preview
tool for the named scenes at `mac-1512x869@2` and `laptop-1280x800@1`. The
eye art is new: JJ judges it from the Task 8 gallery; pending until then.

**Execution note (advisory):** opus. Prerequisite: Task 3. Not parallel.

- [ ] **Step 1:** The enum, codec and call sites; tests for the codec.
- [ ] **Step 2:** The visibility rule and removal of the Reveal machinery;
  rewrite the header mode tests to the acceptance cases.
- [ ] **Step 3:** The eye icons (two small monochrome SVGs, 12 px artwork on
  a transparent ground, strokes that survive at 1x) and the button, wired to
  the existing `headerModeRequested(HeaderMode)` commit path.
- [ ] **Step 4:** Menus and the settings dialog.
- [ ] **Step 5:** Scenes and README; capture the named scenes; read the crops.
- [ ] **Step 6:** Tests green, `tst_workspace_*` and `tst_container_*` that
  touch headers green, the eight checks, signed commit(s).

## Task 5: Hold Shift to arrange, Escape to leave

**Requirements:** spec "Arrange"; decision rows "Arrange, held", "Arrange,
switched on" and "Arrange cue".

**Files:**
- Create: `src/gui/containers/ArrangeHoldFilter.{h,cpp}`
- Modify: `src/gui/containers/ContainerManager.{h,cpp}` (held state)
- Modify: `src/gui/MainWindow.cpp` (creates the filter when the look is on)
- Modify: the source list that names the containers files
- Test: `tests/tst_workspace_arrange_hold.cpp` (new), `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `ContainerManager::setArrangeMode(bool)`, signal
  `arrangeModeChanged(bool)`; from Task 4, headers drawn while arranging.
- Produces:
  - `void ContainerManager::setArrangeHeld(bool)`, `bool arrangeHeld() const`,
    `bool arranging() const` (`m_arrangeMode || m_arrangeHeld`). Containers,
    content hosts and the edge area are told `arranging()`.
    `arrangeModeChanged` still reports the switch only, so the menu's check
    mark does not flicker while Shift is held.
  - `bool ContainerManager::containerDragActive() const` (true while any
    header drag or grip resize is in progress).
  - `class ArrangeHoldFilter : public QObject`, an application event filter:
    `explicit ArrangeHoldFilter(ContainerManager*, QObject* parent)`,
    `static constexpr int kHoldDelayMs = 300;`,
    `void setHoldDelayForTest(int ms)`.

**Acceptance:**
- Shift pressed alone and held 350 ms: `arranging()` is true and
  `arrangeMode()` is false; Shift released: `arranging()` is false.
- Shift pressed, then within 100 ms any of: a mouse press, a wheel event, a
  press of another key. Held a further 400 ms: `arranging()` stays false.
  After Shift is released and pressed again alone, the hold works.
- Shift pressed together with Ctrl, Alt or Meta already down: no hold.
- Focus in a `QLineEdit`, `QTextEdit`, `QPlainTextEdit`, `QAbstractSpinBox`
  or an editable `QComboBox`: no hold.
- A mouse button already down when Shift goes down: no hold.
- Hold active and a container header drag begins; Shift released mid-drag:
  `arranging()` stays true until the drag ends, then becomes false.
- Hold active, the application loses focus (`QEvent::ApplicationDeactivate`):
  the hold ends.
- Switch on (`setArrangeMode(true)`), Shift held and released:
  `arrangeMode()` is still true.
- Escape with the switch on, no drag, no popup or modal widget active, focus
  not in a text field: `arrangeMode()` becomes false and the key event is
  consumed. With a `QMenu` open or a modal dialog active, Escape reaches it
  and Arrange stays on. During a container drag, Escape cancels the drag (the
  existing behaviour) and Arrange stays on.
- With the look off the filter is not installed: Shift held 500 ms changes
  nothing.
- A floating container's header is drawn while the hold is active.

**Verification:** UI interaction: functional tests, offscreen, sending key,
mouse and wheel events through `QApplication::sendEvent` as
`tests/tst_workspace_edge_strips.cpp` does for the mouse. Native evidence
pending: on macOS, JJ holds Shift over the main window and over a popped-out
window, and Shift-wheel tunes without headers appearing.

**Execution note (advisory):** opus. Prerequisite: Task 4. Not parallel.
Shift is used for tuning steps in `SpectrumWidget.cpp`, `RxApplet.cpp`,
`VfoWidget.cpp` and `ParametricEqWidget.cpp`; the filter must never consume
a Shift key event, only observe it.

- [ ] **Step 1:** Held state in the manager, with `arranging()` replacing the
  reads of `m_arrangeMode` that mean "is arranging now".
- [ ] **Step 2:** The filter and its tests; Escape.
- [ ] **Step 3:** `MainWindow` owns one filter when the look is on.
- [ ] **Step 4:** Tests green, `tst_workspace_live_input` and
  `tst_workspace_header_modes` green, the eight checks, signed commit.

## Task 6: One drag rule while arranging

**Requirements:** spec "Dragging while arranging"; decision rows "Dragging
while arranging" and "Dragging when not arranging"; veto list item on the
snap aids.

**Files:**
- Modify: `src/gui/containers/ContainerWidget.{h,cpp}` (`beginDrag`,
  `updateDrag`, `endDrag`, `cancelDrag`, `updateResize`)
- Modify: `src/gui/containers/ContainerManager.{h,cpp}` (`finishEdgeDrag` and
  the edge drag lambdas)
- Modify: `src/gui/containers/EdgeDockStrip.{h,cpp}` only if the drop target
  query needs a new case
- Modify: `tests/visual/PreviewScenes.cpp`, `tests/visual/README.md`
- Test: `tests/tst_workspace_arrange_drag.cpp` (new), `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `ContainerManager::arranging()`,
  `containerDragActive()` (Task 5); `rememberOperatorGeometry` and
  `defaultWindowRect` (Tasks 1 and 2);
  `EdgeDockArea::dropTargetAt(globalPos)`, `showDropPreview`,
  `kEdgeHotZonePx = 48`; `ContainerManager::moveContainerToEdge`,
  `placeContainerFree`, `commitDockMode`.
- Produces: while `arranging()`, a header drag of a strip, free, floating or
  pop-out container emits the same `edgeDragStarted/Moved/Finished/Cancelled`
  signals, and `finishEdgeDrag` decides the result by where the pointer is at
  release:

  | Pointer at release | Result |
  | --- | --- |
  | A valid edge drop target | `moveContainerToEdge` to the previewed place |
  | Elsewhere over the spectrum pane | Free at the release position (grid snap if "Align to 8 px Grid" is on and Alt is not held) |
  | Outside the main window's frame | Floating at the release position |

  Nothing is reparented or re-docked before release. The edge preview shows
  for every source kind.

**Acceptance:** with the look on and `arranging()` true, in an `EdgeFixture`
style setup with the pane at a known global rect:
- Strip source released over the pane centre: the container is free
  (`OverlayDocked`) at the release position; released over the right edge
  zone: it is in the right strip at the previewed index; released 200 px
  outside the main window: it is floating with its top-left at the pointer
  minus the press offset.
- Free source: released in the left edge zone docks it in the left strip
  (today impossible); released elsewhere over the pane moves it; released
  outside the main window makes it floating. During the drag it is not
  clamped to the pane.
- Floating source and pop-out shell source: released in an edge zone docks
  there; released over the pane makes it free at that position; released
  outside the main window just moves the window.
- For each source kind the edge preview is visible while the pointer is in an
  edge zone and hidden when it leaves
  (`showDropPreviewForTest`/the overlay's visibility).
- Escape during any of these drags: the container's dock mode, strip place
  and geometry equal those before the press, and no commit happened.
- Every release commits once, and the committed place is the remembered
  place (Task 1): after 300 ms the stored document shows the new place and
  nothing else changed.
- Not arranging (header kept shown): a strip source still docks at an edge
  and still does nothing over the pane centre; a free source is clamped to
  the pane and cannot dock; a floating window only moves. Exactly as on the
  base.
- Snap: with "Align to 8 px Grid" on, a free release lands on a multiple of
  8 in x and y; with Alt held it does not. Holding Ctrl during a drag or a
  grip resize no longer rounds to 10 px, with the look on. With the look
  off, Ctrl still rounds to 10.
- Scenes: `arrange-drag-edge-free` (a free container dragged to the left
  edge, preview shown) and `arrange-drag-edge-floating` (a floating window
  over the right edge), added through the existing drop-preview seam; read
  and described in the report.

**Verification:** UI interaction: functional tests offscreen with global
coordinates, as `tst_workspace_edge_strips` drives header drags. Offscreen
cannot prove pointer delivery between native windows on macOS: a
`NATIVE_WINDOW` case for the floating-to-dock and free-to-floating drags is
written and registered here and run only by the controller with JJ's OK;
pending until then. Human smoke, pending: JJ drags each kind to an edge, to
the pane and out of the window.

**Execution note (advisory):** opus. Prerequisite: Task 5. Not parallel.
The riskiest task for regressions in today's strip drags; the controller may
want an early look at the diff of `finishEdgeDrag`.

- [ ] **Step 1:** Tests for the acceptance cases, including the
  not-arranging cases, which must be green on the base and stay green.
- [ ] **Step 2:** Free, floating and pop-out header drags report through the
  edge drag signals while arranging; no clamp to the pane while arranging.
- [ ] **Step 3:** `finishEdgeDrag` implements the table, committing on
  release only.
- [ ] **Step 4:** The Ctrl 10 px rounding is skipped when the look is on.
- [ ] **Step 5:** Scenes; the native case, registered and not run.
- [ ] **Step 6:** Tests green, `tst_workspace_edge_strips`,
  `tst_workspace_live_input`, `tst_container_position_restore` and the Task 1
  and 2 tests green, the eight checks, signed commit(s).

## Task 7: Pop Out in the menu, the pin on the header, and return to origin

**Requirements:** spec "Pop out, pin and return"; decision rows "Pop out",
"Stay on top" and "Return"; spec "Saved layout", the origin field; veto list
item on the pin's size.

**Files:**
- Modify: `src/gui/containers/ContainerDocument.h`,
  `ContainerDocumentCodec.cpp` (the origin field)
- Modify: `src/gui/containers/ContainerManager.{h,cpp}` (`commitDockMode`,
  `floatContainer`, `dockContainer`, `finishEdgeDrag`)
- Modify: `src/gui/containers/ContainerWidget.cpp` (body menu "Pop Out";
  pin state to the header)
- Modify: `src/gui/containers/WorkspaceHeader.{h,cpp}` (the pin button)
- Modify: `tests/visual/PreviewScenes.cpp`, `tests/visual/README.md`
- Test: `tests/tst_workspace_popout_return.cpp` (new),
  `tests/tst_container_document.cpp`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `defaultWindowRect` (Task 2); `finishEdgeDrag` (Task 6);
  `ContainerWidget::setPinOnTop(bool)`; `AppIcon::apply(QAbstractButton*,
  const QString& name, int logicalPx)` with the icons `pin` and `pinned`.
- Produces:
  - `struct ContainerHome { DockMode dockMode; DockEdge edge; QString beforeId; QString afterId; QRect geometry; };`
    and `std::optional<ContainerHome> ContainerDocument::home`, saved as the
    JSON object `"home"` with keys `"dockMode"`, `"edge"`, `"before"`,
    `"after"`, `"geometry"`, written only while a container is floating,
    added to `kContainerFields`, parsed leniently (a bad `"home"` is dropped
    for that container with a `qCWarning`). `beforeId` and `afterId` are the
    neighbours in the strip, as `ReturnLocation` does for entries.
  - `WorkspaceGroupHeader`: a checkable pin button shown only when floating,
    to the left of the return arrow: `QAbstractButton* pinButton() const`,
    `void setPinned(bool)`, signal `void pinToggled(bool)`. Icon `pin` when
    not on top, `pinned` when on top, at `kHeaderPinIconPx = 12` inside the
    16 x 14 header button.

**Acceptance:**
- The body right-click menu's first action is "Pop Out" on a docked or free
  container and it makes the container floating; on a floating container the
  action reads "Return".
- Strip container, second of three in the right strip, popped out by the
  header button, by the menu, and by an arranging drag out of the window:
  each time its document has `home` = right strip with the first container as
  `beforeId` and the third as `afterId`. Return puts it second of three
  again, and `home` is cleared.
- Free container at (200, 150, 320, 240) popped out and returned: free at
  exactly that rect.
- Return when `beforeId` no longer exists but `afterId` does: placed directly
  before `afterId`. When neither exists but the strip does: at the end of
  that strip. When the strip has no containers left: it is recreated with
  this one.
- Return of a free container whose home rect is now outside the pane: shown
  inside the pane, not on the same top-left as another free container.
- A floating container with no `home` (an older document, or one created
  floating): Return docks it in the main strip, as the main container does
  today.
- A popped-out window dragged to a new place while arranging and docked
  there (Task 6) does not go to `home`; `home` is cleared.
- The pin: visible only while floating; clicking it sets
  `Qt::WindowStaysOnTopHint` on the floating form, shows `pinned`, checks the
  "Pin on Top" menu item, and `config["pinOnTop"]` is true in the committed
  document; clicking again reverses all four. A document saved with
  `pinOnTop` true restores with the pin shown as `pinned`. (If the menu item
  did not write `pinOnTop` before this task, it does now; say so in the
  report.)
- Codec: `"home"` round-trips; absent loads empty; a malformed one affects
  only its container.
- Scenes: `floating-container` shows the pin unpinned;
  `floating-container-pinned`; `look-menu-container-context` shows "Pop Out".
  A 5x crop of the pin in both states at both profiles is read and described.

**Verification:** UI interaction plus saved state: functional tests
offscreen, rendered evidence for the scenes. Native evidence pending:
JJ confirms on macOS that a pinned window stays above the main window and
other applications' windows as it did with the old title bar.

**Execution note (advisory):** opus. Prerequisite: Task 6. Not parallel.

- [ ] **Step 1:** The codec field and tests.
- [ ] **Step 2:** Record `home` wherever a container becomes floating; use
  and clear it in `dockContainer`; the fallbacks.
- [ ] **Step 3:** "Pop Out" in the body menu.
- [ ] **Step 4:** The pin button, wired to `setPinOnTop` and the document.
- [ ] **Step 5:** Scenes, README, crops.
- [ ] **Step 6:** Tests green, `tst_container_popout_removal`,
  `tst_container_arrangement` and the earlier tasks' tests green, the eight
  checks, signed commit(s).

## Task 8: Whole-change evidence, gallery and style guide

**Requirements:** spec "Acceptance and evidence", the rendered and look-off
paragraphs; spec "Mock-ups" (the real renders replace them as evidence).

**Files:**
- Modify: `STYLEGUIDE.md` (the container header: eye, pin, two states;
  Arrange by Shift; the drag rule; no Lock)
- Modify: `docs/architecture/2026-10-10-container-arrange-design.md` only to
  add a "Built" note with any deviation the earlier tasks reported
- Modify: `tests/tst_workspace_colour_sweep.cpp` only if the new icons or
  scenes need an entry, with the reason
- Outside the repository (crew workspace): a gallery folder named in the
  dispatch

**Interfaces:**
- Consumes: every scene added or renamed by Tasks 4, 6 and 7.
- Produces: nothing for later tasks.

**Acceptance:**
- A full capture at both profiles from a frozen preview binary built at this
  task's start, compared with `--verify --against` the base run named in the
  dispatch (captured from the commit before Task 1). Every changed look-on
  scene is listed with its cause (which task); every look-off scene is
  identical, apart from any the Task 4 report named. Anything unexplained is
  a finding, fixed at its cause or reported.
- `tst_workspace_colour_sweep` (with `QTEST_FUNCTION_TIMEOUT=1700000`) passes
  twice, with its counts.
- The offscreen regression set: every `tst_workspace_*`, `tst_container_*`,
  `tst_setup_*`, `tst_free_canvas`, `tst_mainwindow_*` binary, with pass
  counts, 0 failures.
- A gallery with an `index.html`: the header in both eye states, Arrange on,
  the two drag previews, the floating header with the pin in both states, the
  body menu, each at both profiles with 5x crops of the eye and the pin. The
  implementer opens each image and says what it shows.
- `STYLEGUIDE.md` describes the rules above in the look's container section,
  with no text left that describes Lock, Reveal or the hover header as part
  of the look.

**Verification:** `ui-verification`: the capture tool is this repository's UI
gate for the look. This task is the combined final check for the plan's UI;
the full test suite and the native-window tests are run once by the
controller at the end, not here.

**Execution note (advisory):** opus (it judges renders and may fix findings).
Prerequisite: Task 7. Not parallel.

- [ ] **Step 1:** Freeze the binary, capture, verify against the base,
  account for every change.
- [ ] **Step 2:** The sweep twice and the regression set.
- [ ] **Step 3:** Gallery and `STYLEGUIDE.md`.
- [ ] **Step 4:** The eight checks, signed commit(s).
