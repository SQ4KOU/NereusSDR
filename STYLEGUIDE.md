# NereusSDR Applet Style Guide

Design language reference for all applet panels in NereusSDR. Every applet
should follow these conventions to maintain visual consistency across the UI.

---

## Color Palette

### Core Theme

| Role | Hex | Usage |
|------|-----|-------|
| Background (app) | `#0f0f1a` | Main window, dark surfaces |
| Text (primary) | `#c8d8e8` | Standard labels, values, button text |
| Text (secondary) | `#8090a0` | Dim labels, slider row labels, section headers |
| Text (tertiary) | `#708090` | Section keywords ("STEP:", "Filter:") |
| Text (scale) | `#607080` | EQ dB scale, minor annotations |
| Text (inactive) | `#405060` | Disabled indicators, inactive state text |
| Accent | `#00b4d8` | Slider handles, active fills, pressed buttons |
| Title bar text | `#8aa8c0` | Applet title labels |

### Borders & Surfaces

| Role | Hex | Usage |
|------|-----|-------|
| Button base bg | `#1a2a3a` | Default button and combo background |
| Button hover bg | `#203040` | Hover state for buttons |
| Button alt hover | `#204060` | Hover state (variant used in P/CW) |
| Border (standard) | `#205070` | Button and combo borders |
| Border (subtle) | `#203040` | Triangle buttons, secondary borders |
| Inset background | `#0a0a18` | Value readout boxes, gauge bar fill area |
| Inset border | `#1e2e3e` | Value readout box borders, divider lines |
| Title gradient top | `#3a4a5a` | Title bar gradient stop 0 |
| Title gradient mid | `#2a3a4a` | Title bar gradient stop 0.5 |
| Title gradient bot | `#1a2a38` | Title bar gradient stop 1 |
| Title border | `#0a1a28` | Title bar bottom border |
| Groove | `#203040` | Slider groove background |

### Active/Checked Button States

| State | Background | Text | Border | Usage |
|-------|-----------|------|--------|-------|
| Green | `#006040` | `#00ff88` | `#00a060` | PROC, +ACC, MON, Sidetone, Breakin, NB/NR/ANF, EQ ON |
| Blue | `#0070c0` | `#ffffff` | `#0090e0` | DAX, Iambic, filter presets, antenna, EQ RX/TX |
| Amber | `#604000` | `#ffb800` | `#906000` | RIT, XIT, QSK |

### Gauge Fill Zones

| Zone | Color | Usage |
|------|-------|-------|
| Normal | `#00b4d8` (cyan) | Safe operating range |
| Warning | `#ddbb00` (yellow) | Approaching limit |
| Danger | `#ff4444` (red) | Over limit / clipping |
| Peak marker | `#ffffff` (white) | Peak hold indicator line |

---

## Typography

| Context | Size | Weight | Color |
|---------|------|--------|-------|
| Title bar | 10px | Bold | `#8aa8c0` |
| Button text | 10px | Bold | `#c8d8e8` |
| Slider row labels ("Delay:", "Speed:") | 11px | Normal | `#8090a0` |
| Section headers ("STEP:", "Filter:") | 11px | Normal | `#708090` |
| Value readouts (inset boxes) | 10-11px | Normal | `#c8d8e8` |
| Gauge tick labels | 9px | Normal | Zone-dependent |
| Gauge center label | 10px | Bold | `#8090a0` |
| Scale annotations (EQ dB) | 9px | Normal | `#607080` |

No explicit font family is set anywhere; all text uses the Qt system default.

---

## Components

### Title Bar

Every applet starts with a shared gradient title bar via `appletTitleBar(text)`:

- **Height:** 16px fixed
- **Background:** vertical linear gradient `#3a4a5a` -> `#2a3a4a` -> `#1a2a38`
- **Bottom border:** 1px solid `#0a1a28`
- **Label:** 10px bold `#8aa8c0`, positioned at (6, 1), transparent background

### Buttons

**Standard toggle button:**
```css
QPushButton {
    background: #1a2a3a;
    border: 1px solid #205070;
    border-radius: 3px;
    color: #c8d8e8;
    font-size: 10px;
    font-weight: bold;
    padding: 2px 4px;
}
QPushButton:hover { background: #204060; }
```

Pair with a checked-state override (`kGreenActive`, `kBlueActive`, `kAmberActive`)
depending on the button's semantic role.

**Triangle step button (TriBtn):**
- **Size:** 22x22px fixed
- **Style:** `#1a2a3a` bg, `#203040` border, 3px radius
- **Hover:** `#203040`
- **Pressed:** `#00b4d8`
- Paints a filled triangle (left or right), `#c8d8e8` normally, black when pressed

### Workspace look: lit buttons, choices and status lights

These apply with the workspace look on (JJ's colour answers, 2026-10-10).
With the look off the sheets above draw as written.

- **OPERATE on (D30):** the raised lit green of a checked processing button
  (D15, as PS-A on): fill `#306247` / `#254d38` / `#1d3d2c`, text `#bbe1c6`,
  rim `#3e7e5c`, top edge `#48946b`, the button's radius. It stays lit under
  the pointer. OPERATE on the amplifier, tuner and RF2K-S applets is a plain
  push button that shows its state by its sheet; it says so with the
  `nereusWorkspaceLitWhenOn` property and keeps today's engaged green sheet
  (`#006030`), which the look redraws.
- **The tuner's chosen antenna (D31):** the blue selection glass (fill
  `#2f5f92` to `#1e3d5f`, text `#dbe9f8`, rim `#3d78b0`); the other ANT
  buttons are raised buttons. The buttons are not checkable: a click asks
  the tuner, and the glass follows the tuner's report. They carry the
  `nereusWorkspaceChoice` property. A new set of buttons that shows one
  choice the same way uses the same property.
- **The PureSignal badge (D32):** its off state keeps Thetis DimGray
  (`#696969`) and its over-range state DodgerBlue (`#1e90ff`); the other
  states are the status tones.
- **Status lights by meaning (D21):** a light or dot that is on or
  connected is ok `#4caf6a`; one that is disconnected or reports a fault is
  danger `#c25a5c`; one that is off is `#58585e` (text-inactive). This holds
  for every state of the RF2K-S connection dot, the sound system and VAX
  dots, the speech chain's stage dots, the Alex filter and Alex-2 board
  LEDs, the HL2 I/O LEDs and the HL2 throttle line. A new light writes its
  colour through `WorkspacePreview::statusColourName()` or takes a row in
  `StylesheetColourTable.cpp`; a state that is none of the three is asked
  first.
- **The connection light's stale state:** the state pill of a radio last
  seen one to five minutes ago is none of the three; it is the warn colour
  `#d8a55f` (the `measured` role, `Workspace::kAmberText`; settled
  2026-10-10). Its other states follow the rule above: connected and online
  are ok, offline is danger. `ConnectionPanel::pillStyleSheet()` writes
  today's literals (`#39c167`, `#d39c2a`, `#c14848`), which the look
  translates by their rows.
- **Output pin LEDs, off:** filled with the border colour `#2c2c31`.
- **Progress bars:** the filled part is the selection glass; the groove is
  the platform style's, and the percent text stays centred.
- **A radio whose own sheet draws a rim wider than 1 px:** checked, the rim
  is the selection rim `#3d78b0` all round (no light top edge).
- **Kept as they are:** the TX filter overlay label (`#ffaa70`, the user's
  setting) and the Block TX tint.
- **Where the keyboard is (JJ, 2026-10-10):** one colour, the selection rim
  `#3d78b0`, marks the control the keyboard acts on, wherever the platform
  style draws the control.
  - **A dialog's default button** (the one Enter presses): its rim is the
    selection rim, 1 px, in the shape the platform style draws, so it has the
    corner radius and size of the buttons beside it. Its fill (the platform
    style's faint blue tint, `#212630` at its base), its text and its size
    are unchanged.
  - **A push button the keyboard moved the focus to** takes the same rim. A
    button focused by a click shows nothing, as before.
  - **The current cell of a list, table or tree:** a 1 px outline in the
    selection rim, with nothing tinted inside it. A selected row is the
    selection fill as before.
  - These are drawn by the look's style object (`Workspace::LookStyle`,
    `src/gui/styles/WorkspaceLookStyle.cpp`), not by a sheet: a sheet block
    on one button would change its corner radius against its neighbours.
    Do not style a dialog's buttons to get the rim; leave them unstyled.
  - A button a sheet draws (the glass buttons of applets and panels) is not
    affected. An action button a sheet draws marks its default state through
    `:default` with the same rim colour.

### Workspace look: container headers and arranging

With the preview switch on, a container's chrome is one 18 px header
(`WorkspaceGroupHeader`, `src/gui/containers/WorkspaceHeader.cpp`). With the
switch off the 22 px title bar and its menus are as they were; nothing here
applies to it. Settled with JJ on 2026-10-10; design in
`docs/architecture/2026-10-10-container-arrange-design.md`.

- **Two states, per container:** the header is shown or it is hidden. Shown,
  it sits in the layout above the content. Hidden, nothing is drawn and no
  room is kept for it; the content has the whole container. There is no
  header that appears on hover or on focus, and no timer.
- **Header buttons, left to right after the title and the source chip:**
  eye, pin (a popped-out window only), pop out or return arrow, options
  (three dots), close. Each is 16 x 14 px. All draw in one colour, the
  `text-scale` role, and on hover take the `text` role on a `button-hover`
  plate. A new header button follows the same size and the same two roles.
- **The eye:** an open eye means the header stays; a crossed-out eye means
  the header hides when not arranging. It is a toggle and changes that
  container only. Its tooltip says what a click will do ("Hide this header
  when not arranging" or "Keep this header showing"). The eye is drawn in
  code (`WorkspaceEyeButton`), not from an icon file, because an icon file
  cannot take the header's colour. The right-click menu's "Keep Header
  Showing" is the same switch, ticked when the header is shown.
- **The pin:** only in the header of a popped-out window, left of the return
  arrow. Tilted pin: the window is not kept on top. Upright pushpin: it is.
  Tooltips "Keep this window on top" and "Stop keeping this window on top".
  The shapes are the app's `pin` and `pinned` art
  (`resources/icons/emoji/`), drawn as a one-colour silhouette at 14 px in
  the header's glyph colour, so the pin never carries its own red. "Pin on
  Top" in the Options menu is the same switch.
- **Arrange:** headers of hidden containers are drawn only while arranging,
  over the top of the content, and take no layout room. Arranging is on
  while Shift is held alone for 300 ms (`ArrangeHoldFilter::kHoldDelayMs`),
  until Shift is let go, or while Containers > Arrange Workspace
  (Ctrl+Shift+A) is switched on, until it is switched off or Escape is
  pressed. A click, a wheel turn or another key while Shift is down cancels
  the hold for that press, so Shift-tuning never shows headers; a hold does
  not begin while a text field has the keyboard. Arranging never changes a
  container's shown or hidden choice and saves nothing by itself.
- **The Arrange cue:** every header showing and the dashed accent outline
  round each container. No banner, no toast, no tint over the content. The
  outline has a transparent fill, so a popped-out window keeps its face.
- **One drag rule while arranging.** The header is the handle, and where the
  pointer is at release decides, whatever the container was before:
  - within 48 px of an edge of the main window: it docks in that edge's
    strip. The landing box (the strips' translucent accent box) shows the
    place during the drag, for every kind of container;
  - elsewhere over the spectrum: it sits free there, on the 8 px grid unless
    Alt is held;
  - outside the main window: it becomes a window where it was dropped;
  - over the menu bar, the status bar or the title bar, or on Escape: the
    drag is cancelled and the container goes back where it started.
  When not arranging, a drag is as before: a strip container docks at an
  edge, a free container stays inside the spectrum pane, a window only
  moves.
- **One snap:** the 8 px grid, which Alt bypasses. Ctrl does not step a drag
  or a resize in the look.
- **No lock.** The look has no Lock setting, no padlock in the header and no
  Lock item in any menu. A container with a hidden header has no handle, so
  it cannot be moved by accident; moving anything takes Arrange.
- **Menus:** the body's right-click menu starts with "Pop Out" ("Return" on
  a window), then Container Settings, Hide container, Keep Header Showing,
  Arrange Workspace and Contents. The Options menu keeps Axis Lock (a free
  container only), Pin on Top (a window only), Pop Out or Dock, Container
  Settings, Move to Edge, Place Free, Arrange Workspace and Contents.
  Neither has a Header submenu.
- **Return goes home:** the return arrow puts a popped-out container back
  where it was popped out of (its strip, beside the neighbour it had, or
  its free place). A window that was never docked joins the Main Panel's
  strip.

### Sliders

**Horizontal:**
```css
QSlider::groove:horizontal {
    height: 4px; background: #203040; border-radius: 2px;
}
QSlider::handle:horizontal {
    width: 10px; height: 10px; margin: -3px 0;
    background: #00b4d8; border-radius: 5px;
}
```

**Vertical (EQ bands):**
```css
QSlider::groove:vertical {
    width: 4px; background: #203040; border-radius: 2px;
}
QSlider::handle:vertical {
    height: 10px; width: 16px; margin: 0 -6px;
    background: #00b4d8; border-radius: 5px;
}
```

**Workspace look (preview switch on):** the live pass rewrites every
slider's groove (`WorkspaceComponents.cpp` `sliderGroove()`); the sheets
above stay as they are with the switch off.

- **Track (JJ's D29, 2026-10-09):** fill `#1a1a1e` (button), flat, inside a
  1 px rim `#58585e` (text-inactive, the unticked check box's rim), the same
  colour on all four sides. No shade above or light below. Thickness (4 px
  plus the rim) and radius (3 px) are Longpath's.
- **Disabled track:** fill `#050507` (inset) inside a 1 px rim `#3d3d41`
  (border-muted) on all four sides, the disabled unticked box (D10). A sheet
  with no disabled groove rule gets one. Other disabled rules keep their
  colours.
- **Zoom track (D27, `nereusWorkspaceVisibleTrack`):** 6 px inside the 1 px
  rim (8 px overall), the 16 px handle; the fill stays `#050507` (inset) and
  the rim is the D29 `#58585e` on all four sides.
- **Filled sliders (D24, D26, `nereusWorkspaceFilledSlider`, the PC and
  RADIO speaker volume):** the filled side keeps its colour (PC accent
  `#4a7ba8`, RADIO `#d8a55f`); the unfilled part of the groove is the D29
  track.
- **Handle:** unchanged (D5): 10x14 plus a 1 px accent rim, never thinner
  than its 16 px span (D28: a slider the look raises takes its pixels from
  the panel).

### Inset Value Display

Used for numeric readouts (step size, RIT/XIT, pitch, filter cuts, slider values):

```css
QLabel {
    font-size: 10px;
    background: #0a0a18;
    border: 1px solid #1e2e3e;
    border-radius: 3px;
    padding: 1px 2px;
    color: #c8d8e8;
}
```

- **Alignment:** centered
- **Width:** 36-48px (narrow values), 48-60px (wider values like frequencies)

### Combo Boxes

All combo boxes **must** use the shared style from `src/gui/ComboStyle.h`:

```cpp
#include "ComboStyle.h"

auto* combo = new QComboBox;
NereusSDR::applyComboStyle(combo);
```

### HGauge (Horizontal Bar Gauge)

Reusable widget from `src/gui/HGauge.h`.

- **Height:** 24px fixed
- **Bar background:** `#0a0a18`
- **Bar border:** `#203040`
- **Fill:** three-zone (cyan/yellow/red) based on `yellowStart` and `redStart` thresholds
- **Reversed mode:** single red fill from right to left (used for compression)
- **Peak hold:** white vertical line marker, enabled via `setPeakValue()`
- **Tick labels:** 9px, colored by zone (cyan normal, yellow warning, red danger)
- **Center label:** 10px bold `#8090a0`

---

## Layout

### Margins & Spacing

| Context | Margins (L, T, R, B) | Spacing |
|---------|----------------------|---------|
| Outer applet layout | 0, 0, 0, 0 | 0 |
| Inner content area | 4, 2, 4, 2-6 | 2-4px |
| Slider row | — | 4px between items |

### Slider Row Pattern

Standard layout for a labeled slider with value readout:

```
[Label/Button (fixed width)] [gap] [Slider (stretch 1)] [Inset Value (fixed width)]
```

- **Left column width:** 70px
- **Gap:** 4px (between label and slider)
- **Value box width:** 36px
- **Row spacing:** 4px

### Button Row Pattern

Buttons in a row use stretch or fixed width depending on context:

- **Equal-width buttons:** `setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed)` + stretch
- **Fixed-width buttons:** `setFixedWidth(N)` or `setFixedSize(W, H)`
- **Standard height:** 22px

### Dividers

Thin horizontal lines between sections:
```css
QFrame { color: #1e2e3e; }
```
Using `QFrame::HLine` with `QFrame::Sunken`, fixed height 2px.

### Setup Page Reflow (D25)

A Setup page wider than the Settings window rearranges to fit: groups that sit side by side stack in reading order (`SetupFit::FitRow`, `SetupFit::FitGrid`), notes wrap, tabs stay at the top, and the page never scrolls sideways.

Rows stack only when they cannot fit side by side: a row that fits the page's width and height without a vertical scroll bar is side by side, whatever was shown before. Host the page or tab in a `SetupFit::FitScrollArea` (or `SetupFit::scrollingTab`), not a plain `QScrollArea`, so a scroll bar that showed first cannot keep the row stacked.

Group spacing: groups keep the height their content needs, and one group follows the next at the page layout's spacing, side by side or stacked. A page ends with a trailing stretch (`addStretch(1)`), so the height left over is at the bottom of the page, never between groups. A side-by-side row is as tall as its taller column; a shorter column ends with its own stretch, which puts its spare under its last group and does not ask for the page's spare (`FitRow::expandingDirections`). The row grows only when a widget in it asks to. A row for a message that is empty is hidden (`QFormLayout::setRowVisible`) instead of holding an empty band.

---

## Patterns

### Feedback Loop Prevention

All applets use one of two guard patterns when syncing UI state from the model:

1. **Boolean guard** (`m_updatingFromModel`): set true before updating widgets,
   checked in signal handlers to suppress outgoing commands.

2. **QSignalBlocker**: blocks signals on a specific widget during programmatic
   updates (preferred for combo boxes).

### Stepper Controls

For values adjusted in discrete steps (pitch, RIT/XIT, filter cuts):

```
[TriBtn left] [Inset Value Display] [TriBtn right]
```

- Step buttons are 22x22px triangle buttons
- Value display is an inset label (centered)
- Click handler reads current value from model, applies delta, sends command

### Mode-Aware Panels

Use `QStackedWidget` when a single applet slot shows different controls based
on radio state. Connect the mode signal to `setCurrentIndex()`.

### Gauge Configurations

Common gauge setups across applets:

| Gauge | Min | Max | Yellow | Red | Reversed |
|-------|-----|-----|--------|-----|----------|
| Fwd Power (barefoot) | 0 | 200 W | 125 | 125 | No |
| SWR | 1.0 | 3.0 | 2.5 | 2.5 | No |
| Mic Level | -40 | +10 dBFS | -10 | 0 | No |
| Compression | -25 | 0 dB | — | 1 | Yes |
| ALC | 0 | 100 | 80 | 80 | No |

---

## Applet Anatomy

A typical applet follows this vertical structure:

```
+-----------------------------------+
|  Title Bar (16px gradient)        |
+-----------------------------------+
|  Gauge(s)           (if any)      |
+-----------------------------------+
|  Combo row(s)       (if any)      |
+-----------------------------------+
|  Slider rows                      |
|  [Label] [Slider] [Value]        |
|  [Label] [Slider] [Value]        |
+-----------------------------------+
|  Toggle button rows               |
|  [Btn] [Btn] [stretch] [Btn]     |
+-----------------------------------+
|  Stepper rows       (if any)      |
|  [Btn] [< value >]               |
+-----------------------------------+
```

Content margins: 4px sides, 2px top, 2-6px bottom. Row spacing: 2-4px.
