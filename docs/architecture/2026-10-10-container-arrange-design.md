# Container headers, arranging and placement design

Status: design, agreed with JJ (KG4VCF) on 2026-10-10. Mock-ups, drawn over
real captures of the workspace look, are in
[2026-10-10-container-arrange-design/](2026-10-10-container-arrange-design/).
They show intent, not final art. Built on 2026-10-11; "Built", near the end,
lists where the code differs from this text.

## Goal

Make the workspace look's containers easy to arrange and trustworthy to leave
alone:

* a clean screen with headers hidden, without losing the way to move a panel;
* one way to move a panel anywhere: drag it, and the edge it will land on
  shows itself;
* every panel and window starts where it was left, at the size it was left.

## Scope

The workspace look (`NEREUS_WORKSPACE_PREVIEW=1`). With the look off, the
chrome, menus and behaviour stay as they are, with one exception: the
placement rule in "Where things are on startup" fixes a bug that both looks
share, so it applies to both.

## Decisions (JJ, 2026-10-10)

| Question | Decision |
| --- | --- |
| Header choices | Two: shown or hidden. Each container remembers its own. The hover choice ("Reveal") is removed |
| Changing it | An eye button on the header (plain eye: the header stays; crossed-out eye: the header hides when not arranging), and "Keep Header Showing" in the right-click menu. No "hide all" or "show all" command |
| Arrange, held | Holding Shift alone for about a third of a second shows every header and enables arranging until Shift is let go. A click, a wheel turn or any other key while Shift is down cancels it, so Shift-tuning never triggers it |
| Arrange, switched on | The menu item and Ctrl+Shift+A stay. Escape now leaves Arrange |
| Arrange cue | None added. Every header showing and the existing highlight are the cue. No bar, no toast |
| Dragging while arranging | One rule for every container: near an edge of the main window the edge lights and a box shows where the panel will land; dropped over the spectrum it sits free; dropped outside the main window it pops out; a popped-out window dropped over the main window docks or sits free where it is dropped. Escape cancels a drag |
| Dragging when not arranging | As today |
| Pop out | "Pop Out" is added to the container's right-click menu |
| Stay on top | The pin button returns to a popped-out window's header, beside the return arrow, using the app's existing pin and pinned icons |
| Return | The return arrow puts a container back in the spot it was popped out of |
| Lock | Removed. A container with no header has no handle, so it cannot be moved by accident; moving anything else takes Arrange |
| Startup | Every panel and window starts where it was left, at the size it was left. The app moves a window only when its monitor is gone, and puts it back when the monitor returns. Nothing is reshuffled at startup |

Settled in this spec, listed for JJ's veto:

* A saved "Reveal" header loads as hidden. It was hidden most of the time, and
  Shift now does what the hover did.
* A saved locked container loads unlocked. Nothing else about it changes.
* "Reveal controls" leaves the right-click menu; holding Shift replaces it.
* "Axis Lock", "Move to Edge" and "Place Free" stay in the header's Options
  menu as they are. Dragging is the main way; the menu is the keyboard way.
* The two snap aids for a free panel (the 8 px grid, and the 10 px step while
  Ctrl is held) become one: the 8 px grid, which Alt bypasses.
* The pin button is sized to the header like its neighbours, not as large as
  in the mock-up.

## What exists today

All in `src/gui/containers/`.

* `HeaderMode { Always, Reveal, Hidden }` (`ContainerTypes.h`), saved per
  container as the integer `"header"` (`ContainerDocumentCodec.cpp`).
* The workspace header (`WorkspaceHeader.cpp`) is 18 px high with Pop out,
  Options and Close buttons, and a padlock that shows when a container is
  locked. Its Options menu (`ContainerWidget.cpp`) holds Axis Lock, Pin on Top
  (floating only), Lock, Pop Out or Dock, Container Settings, Header (three
  choices), Move to Edge, Place Free, Arrange Workspace and Contents.
* The body's right-click menu holds Container Settings, Hide container,
  Reveal controls, Header, Arrange Workspace and Contents. It has no Pop Out.
* The older title bar has a pin button (icons `pin` and `pinned` under
  `resources/icons/emoji/`). The look hides that title bar, so the only pin
  control left is the menu item.
* Dragging starts only from a visible header. A container in a strip docks
  when dragged within 48 px of an edge, with a preview
  (`ContainerManager.cpp`, `EdgeDockStrip.cpp`). A free container is kept
  inside the spectrum pane and cannot be dragged to an edge or out of the
  window. A popped-out window only moves; it cannot be dragged back in.
* Arrange Workspace is global, is not saved, and Escape does not leave it.
* The return arrow sends the main container to its strip and any other
  container to a free spot that is not the one it came from.
* Shift is in use for tuning steps on the spectrum, in the RX applet, on the
  VFO and in the equaliser editor. Every one of those uses Shift together with
  a click, a wheel turn or a drag.

### The stacking bug

Containers have come up on top of each other after a start. Read from the
code, in order of likelihood (none of these has a test today, and the first
is not yet reproduced):

1. Free containers are restored inside the `MainWindow` constructor, before
   the window is laid out. The spectrum pane they sit on is still at its
   unlaid-out size, and each container is clamped into it
   (`ContainerManager.cpp`, the reconcile step that calls `clampedGeometry`).
   They all land at the top left. The move and resize events that follow the
   first show then start the 200 ms save, and the clamped positions are saved
   as if the operator had chosen them.
2. A floating or popped-out window that is not wholly inside one screen is
   recentred on the main window (`FloatingContainer.cpp`,
   `ensureVisiblePosition`), with no offset between windows.
3. Starting with a smaller main window than at save pins free containers to an
   edge, and that position is saved too.
4. New pop-outs, new containers, Place Free and Recover all use one default
   spot with no offset.
5. Unplugging a monitor while running clamps windows to the nearest edge.
6. The first run with the look on copies free positions from the look-off
   layout, which was measured for a pane of a different width.

Saving itself is sound: the whole file is written atomically with a backup,
one profile cannot be opened twice, and a build that does not understand the
saved layout goes read-only rather than resetting it.

## Design

### Headers

`HeaderMode` becomes two values, shown and hidden. The saved integer keeps its
meaning for those two; the old middle value reads as hidden and is never
written again. The hover timers and focus rules that served Reveal are
removed.

The header gains an eye button to the left of Pop out. It is a toggle with a
tooltip that says what a click will do ("Hide this header when not arranging"
or "Keep this header showing"). The right-click menu's "Keep Header Showing"
is the same toggle, checked when the header is shown.

A header is drawn when its container's choice is shown, or while arranging.
Arranging never changes a container's choice.

### Arrange

Arranging is on when the switch is on (menu item or Ctrl+Shift+A) or while the
Shift hold is active. The two are independent: letting go of Shift never turns
the switch off.

The Shift hold, in one place in the main window's event handling:

* It starts a timer when Shift goes down with no other modifier, no mouse
  button down, and focus not in a text field. The delay is a named constant,
  0.3 s.
* Any mouse press, wheel turn or other key before the timer fires cancels it
  for that press of Shift.
* When the timer fires, arranging begins. It ends when Shift is released,
  unless a container drag is in progress, in which case it ends when the drag
  ends.
* Popped-out windows take part: Shift held while one of them is active shows
  headers in all of them and in the main window.

Escape leaves the switched-on Arrange when no drag is in progress. During a
drag, Escape cancels the drag and puts the container back where it started.

### Dragging while arranging

The drag handle is the header. While arranging, one rule decides where a
dragged container lands, whatever it was before:

| The pointer is | Result on release |
| --- | --- |
| Within the edge zone of the main window (48 px, as today) | Docks in that edge's strip at the previewed place. The edge lights and a box shows the place |
| Elsewhere over the spectrum pane | Sits free at that position, snapped to the 8 px grid unless Alt is held |
| Outside the main window | Pops out as a window at that position |

A popped-out window follows the same table when its header is dragged while
arranging. When not arranging, dragging a window only moves it, as today, so a
window can be parked over the main window without docking.

The edge preview is the one strips use today, extended to free and popped-out
sources. A container dragged out of a strip leaves a gap that closes on
release, not during the drag, so the layout does not jump under the pointer.

### Pop out, pin and return

* "Pop Out" in the body's right-click menu does what the header's Pop out
  button does.
* A popped-out window's header shows the pin button to the left of the return
  arrow: the `pin` icon when not on top, `pinned` when on top. The "Pin on
  Top" menu item stays and shows the same state.
* When a container is popped out, by button, menu or drag, the document
  records where it came from: the strip and its place in the order, or its
  free position and size. The return arrow puts it back there. If the place no
  longer exists (the strip is gone, or the free spot is outside the pane), it
  goes to the nearest place that fits and does not cover another free
  container. A container that has never been docked returns to the main
  strip.

### Lock

The setting, its menu item, the header padlock and the rule that held a strip
divider beside a locked container are removed. The saved `"locked"` field is
read and ignored, and no longer written.

### Where things are on startup

One rule: **the app never moves a container and then saves that as if the
operator had moved it.**

* **Remembered place and shown place are separate.** Each container keeps the
  position and size the operator last gave it. What is drawn may differ for a
  while (the window is too small, a monitor is missing); the remembered values
  are what is saved.
* **Only the operator's moves are saved.** A move or resize is written to the
  remembered place when it comes from a drag, a resize grip, a menu command or
  a settings dialog. Moves the app makes for itself (restore, clamping,
  layout, screen changes) are not.
* **Free containers are placed once the pane has its real size.** Restore
  creates them, and they are positioned after the main window's first layout,
  not in the constructor.
* **A window too small for a free container** shows it pulled inside the
  pane, and keeps the remembered place. When the pane is large enough again
  the container is back where it was.
* **Popped-out and floating windows remember their monitor** as well as their
  position and size. On start, a window whose monitor is present is put
  exactly where it was, with no "wholly inside the screen" test. A window
  whose monitor is absent, or whose remembered place is on no screen at all,
  is shown on the main window's screen and keeps its remembered monitor and
  place, so it returns there on a later start when the monitor is back.
* **Unplugging a monitor while running** follows the same rule: shown on a
  remaining screen, remembered place unchanged.
* **Two windows never get the same default spot.** A new pop-out, a new
  container, Place Free and Recover each offset from any container already at
  the spot they would use.
* **Look off to look on.** The first copy of free positions from the look-off
  layout is treated as remembered places for the look-on layout and goes
  through the same rule; nothing is clamped into the saved copy.

### Saved layout

Changes to the container document (`ContainerDocumentCodec.cpp`), all
readable by older builds, which keep unknown fields. This list is what the
code saves (corrected 2026-10-11; see "Built"):

* `"header"`: the integer 0 (shown), 1 (the hover choice, "Reveal") or 2
  (hidden). With the look on only 0 and 2 are written: a saved 1 is saved
  once as 2 when the layout is first loaded. With the look off all three are
  read and written as before.
* `"locked"`: written only when true. With the look on it is cleared when
  the layout is loaded and before every save, so it is never written there.
  With the look off it is read and written as before.
* New, `"geometryPane": [w, h]`: for a free container, the size the spectrum
  pane had when the operator last placed it, so it comes back where it was
  showing. Written once the operator has moved that container, and by Return
  for a container whose home carried one (see `"home"`).
* New, `"screen": {"name", "serial", "geometry": [x, y, w, h]}`: the monitor
  a floating or popped-out window is on, with that monitor's geometry at
  save, so a different monitor in the same place is not mistaken for it.
  Written only for a window. Unknown keys inside `"screen"` are kept on
  rewrite.
* New, `"home": {"dockMode", "edge", "before", "after", "geometry",
  "geometryPane"}`: where a popped-out container came from (its strip and the
  neighbours on each side, or its free rectangle with the pane size that
  rectangle was placed in). Written only while the container is a window,
  with the look on. Unknown keys inside `"home"` are kept on rewrite.
* `"pinOnTop"`, inside the container's existing `config`: with the look on
  it is saved as soon as the pin or the menu item changes it.

A new field that cannot be read (`geometryPane`, `screen`, `home`, or the
`geometry` of a window) is dropped for that container with a log line, and
the rest of the layout loads.

No schema version change was needed (`schemaVersion` stays 1); the new
fields are optional.

## Errors

* A saved monitor that cannot be matched: the window opens on the main
  window's screen; a line goes to the log category the container manager
  uses. No dialog.
* A saved place that fails to parse: that container takes a default place
  with an offset; the rest of the layout is unaffected.
* A drag that ends where nothing can take the container: the drag is
  cancelled.

## Acceptance and evidence

Software, offscreen tests:

1. Header: the eye and the menu item change one container only; a saved
   Reveal loads hidden; while arranging every header is drawn and no choice
   changes.
2. Shift hold: begins after the delay; does not begin when a click, wheel or
   key comes first, or when a text field has focus; ends on release; a drag in
   progress keeps it alive.
3. Drag rule: for a strip, a free and a popped-out source, a release in each
   of the three zones gives the result in the table; Escape restores the
   start.
4. Return: a container popped out of a strip, and one popped out of a free
   spot, return to the same place; the fallback never covers another free
   container.
5. Lock: a document with locked containers loads, saves without the field,
   and its dividers move.
6. Startup, one test per cause in "The stacking bug", each red before the
   change: a full `MainWindow` round trip with two free containers; a window
   smaller at start than at save, then large again; two windows on an absent
   monitor, then the monitor present again; two new pop-outs; the look-off to
   look-on copy. In every case the saved layout is unchanged after a start and
   quit with no operator action.

Rendered, by the capture tool at both profiles: the header with the eye in
both states; arranging; the edge preview for a free and a popped-out source;
the popped-out header with the pin in both states; the right-click menu. With
the look off, every existing scene is identical.

Native window, on macOS, with JJ's OK for the run: the drag across the main
window's edge in both directions and the Shift hold, since offscreen tests
cannot prove pointer behaviour between native views.

By JJ, on a build of his choosing: arrange his own layout, quit and start
several times with a window on each monitor, and confirm nothing moved.

## Built (2026-10-11)

Built on branch `claude/workspace-look` in seven changes, `309c0fdb7` to
`6df0ddcb7`. Where the code differs from the text above, the code is as
listed here.

Saved fields:

* A free container saves `geometryPane`, the pane size its place was chosen
  in. The design had no such field. Without it a container anchored to the
  right or the bottom could not be put back where it was showing.
* A window saves `screen` (name, serial, geometry). On the development Mac
  the built-in display reports an empty serial, so a monitor is matched by
  name and geometry when the serial is empty.
* A popped-out container saves `home`, with an open set of extra keys that
  are kept on rewrite.
* A free home saves `geometryPane` beside its `geometry` (review fix,
  2026-10-11): the size the pane had when the rectangle was placed. A free
  container's rectangle is read against that pane, so without it Return put
  a container anchored to the bottom or the right off by however much the
  pane had changed size. Return writes the rectangle and that pane size back
  to the container; when the place no longer fits the pane, it writes the
  new place with no pane size, which means the pane as it is now. A home
  saved before this fix has none and is read against the pane as it is at
  Return, as it was.
* `screen` keeps unknown keys on rewrite, as `home` does (review fix,
  2026-10-11). They play no part in matching a monitor, and a window the
  operator moves to another monitor saves a new record without them.

Headers:

* `HeaderMode` keeps three values, `Shown`, `Reveal` and `Hidden`. The
  design said two. `Reveal` shipped with the look off and stays there
  exactly as it was. With the look on a header is shown or hidden, and a
  saved `Reveal` is saved once as `Hidden` by the container manager when
  the layout is first loaded.
* The eye is drawn in code. No `eye.svg` or `eye-off.svg` exists: an icon
  file cannot take the header's colour.
* The pin is the approved `pin` and `pinned` art drawn as a one-colour
  silhouette, 14 px high in the 16 x 14 px button, in the header's glyph
  colour. The art's own red and its soft shadow are not drawn.
* The header of a hidden container, drawn over the content while arranging,
  is 0.96 opaque with a 6 px shadow. There is no fade and no timer.

Arranging and dragging:

* A Shift hold does not begin while a text field has the keyboard. Escape
  in a text field goes to the field.
* Dropped over the main window's menu bar, status bar or title bar, a drag
  is cancelled. So is a drag whose mouse grab is lost.
* A free container dropped over the spectrum is held wholly inside the
  pane, then placed on the 8 px grid unless Alt is held.
* The Ctrl 10 px step is gone everywhere in the look, for a drag and for a
  resize, while arranging or not. With the look off it is unchanged.
* While Arrange was on, a popped-out window was drawn as a blank box with a
  dashed border. That was a fault in the Arrange outline from earlier work
  and is fixed: the window keeps its face.

Pop out, pin and return:

* A window's place when it is created (by the header button, the menu, a
  drop outside the main window, a new container, Duplicate or a pop-out
  shell) is its remembered place, saved with its monitor. This holds with
  the look off too. Before, such a window took a default place on every
  start until it was moved.
* A window with no `home` returns to the end of the Main Panel's strip. The
  main container with no `home` returns to its panel dock as before.
* A returned container goes back to its place in the strip's order, but the
  share of the strip it had is not restored; it gets the strip's usual
  share.
* Pin on Top is saved as soon as it changes, with the look on. Before, it
  was written at the next save and could be undone by a layout pass.
* When a free home no longer fits the pane, Return takes the place inside
  the pane that moves the home rectangle least and covers no other free
  container. Only when no place in the pane is clear does it step off the
  others' top-left corners, as a default placement does (review fix,
  2026-10-11).
* A placement chosen in Container Settings records and clears `home` as
  Pop Out and Return do, with the look on (review fix, 2026-10-11).

Lock:

* With the look on, a layout holding a locked container is saved once,
  unlocked, when it is first loaded. `"locked"` is still read and written
  with the look off, and is written only when true.

Startup:

* A layout saved before this change has no `geometryPane`. Its free
  containers are placed against the pane's size once the pane has been
  still for 100 ms after the first show, since the pane is resized twice
  while the window lays out.
* A change of screen resolution or arrangement with no monitor added or
  removed does not re-place windows.
* Review fixes, 2026-10-11, with the look on and off. Of two monitors that
  report one serial, a window's is the one with the saved geometry, else
  the one its remembered place is on. A place the app restores (start-up, a
  monitor change, a layout loaded) is used only when at least 48 px of the
  window's header width and half its height are on a screen's usable area
  (clear of the system's bars); otherwise the window is shown on the screen
  and keeps its saved place. A place the operator drops or resizes a window
  to is never judged that way: it stays there. A save that does not change
  a window's place or monitor leaves the window where it is, so a window
  the system moved with no drag does not jump back; that move is still not
  remembered. A save that changes what sizes a window (auto height, its
  content, layout or header) sizes it from the saved geometry.

Evidence is in the capture run and gallery of 2026-10-11 (real renders at
both profiles; they replace the mock-ups below as evidence). Still owed:
the native-window tests and the checks by JJ that "Acceptance and evidence"
lists (the drag across the main window's edge on macOS, the Shift hold at a
real keyboard, two real monitors, and that a pinned window stays on top).

## Mock-ups

* [Arranging](2026-10-10-container-arrange-design/1-arranging.png): every
  header shown, with the eye on each.
* [Header close-up](2026-10-10-container-arrange-design/1b-header-buttons.png):
  the eye in both states.
* [Drag to an edge](2026-10-10-container-arrange-design/2-drag-to-edge.png):
  the lit edge and the landing box.
* [Popped-out header](2026-10-10-container-arrange-design/3-popped-out-pin.png):
  the pin beside the return arrow. Drawn too large; see the veto list.
* [Right-click menu](2026-10-10-container-arrange-design/4-right-click-menu.png):
  today and proposed.

## Out of scope

* The chrome and menus with the look off.
* What containers hold, and the Contents menu.
* Strip scrolling and strip sizing, other than removing the Lock rule on
  dividers.
* Named layout presets.
* The iPhone app.
