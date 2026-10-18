# Meter Container `AutoHeight` — Follow-up Plan

**Status:** Not started
**Trigger:** PR #42 (Windows container float/dock rendering) left one
symptom unresolved: the ANAN MM preset shows empty space below the
44.1%-tall needle panel when no bar rows are added. Thetis handles this
by auto-sizing the container to the meter's natural content height. This
plan ports that mechanism.

---

## What Thetis does

- `ucMeter` exposes a boolean `AutoHeight` property
  (`Project Files/Source/Console/ucMeter.cs:903`). Serialized as field 17
  of the per-container payload; added in Thetis 2.10.3.6.
- `DirectXMeterRenderer.drawMeters(out int height)`
  (`MeterManager.cs:31338`) walks each item group every frame and
  computes `hh = y + h + padY` in pixels at the current widget width. The
  running max across groups is the meter's natural height. Minimum floor
  is `ucMeter.MIN_CONTAINER_HEIGHT`.
- After each frame the renderer pushes the measured height back via
  `MeterManager.SetContainerHeight(id, height)` →
  `ucMeter.ChangeHeight(height)` → `forceResize()`
  (`ucMeter.cs:416-449`). If `_autoHeight` is true, the container calls
  `resize(Width, _height)`; otherwise `forceResize()` is a no-op
  (container fills whatever parent gives it).
- Item pixel height is `normalizedH × widgetWidth` — Thetis's render rect
  is `(0, 0, w × XRatio, w × YRatio)`, i.e. composite aspect is
  **width-driven**. That's why ANAN MM's `SizeF(1f, 0.441f)` produces a
  container that's 44.1% of its own width, not its own height.

**Net behavior:** items keep their authored proportions (stack rows stay
at `_fHeight = 0.05`, composites at whatever aspect was authored); the
container auto-shrinks to fit. The user toggles `AutoHeight` per
container from the settings dialog.

---

## NereusSDR port scope

### 1. `ContainerWidget` — `AutoHeight` property

- Add `bool m_autoHeight` + `autoHeight() const` / `setAutoHeight(bool)`.
- Serialize as **field 17** of `ContainerWidget::serialize()` (appending
  a new field keeps Thetis field ordering and preserves forward
  compatibility).
- `ContainerWidget::deserialize()`: treat missing field 17 as `false` so
  existing saved layouts keep their manual sizing.
- `ContainerWidget::kMinContainerHeight` already exists; reuse as the
  floor.

### 2. `MeterWidget::naturalPixelHeight(int widthPx)`

```cpp
int MeterWidget::naturalPixelHeight(int widthPx) const;
```

Walks `m_items`, computes each item's `(y + h) × widthPx` in pixels,
returns the max (clamped to `kMinContainerHeight`). For stack items this
uses the reflowed `m_y` / `m_h` (already expressed relative to widget
width in Thetis semantics — see `layoutInStackSlot` + `reflowStackedItems`).

Pad term: Thetis uses `padY = (PadY − Height × 0.75) × widgetWidth`. Port
the two constants (`_fPadY`, `_fHeight` from `clsMeter`) as static
constexpr floats inside `MeterWidget` so the pixel result matches.

### 3. Wire-up points

After every install / reflow / resize event on a MeterWidget, if the
host container's `autoHeight()` is `true`:

- **Docked (PanelDocked, OverlayDocked):** call
  `container->setFixedHeight(natural)` (or equivalent splitter sizing —
  the existing splitter in `MainWindow` may need a splitter-sizes update
  when a panel container shrinks).
- **Floating:** resize the owning `FloatingContainer` to
  `(currentWidth, natural + container chrome)`. Container chrome =
  title-bar height (the hidden-but-reserved one for hover-reveal).

Ideal place to trigger this is `MeterWidget::resizeEvent()` after it
calls `reflowStackedItems()`. A signal `MeterWidget::naturalHeightChanged(int)`
routes up through `ContainerWidget` → `ContainerManager` →
FloatingContainer / splitter.

### 4. UI toggle

Container Settings dialog → "Auto-size height" checkbox. On toggle:

- `setAutoHeight(true)` → immediately trigger a natural-height resize.
- `setAutoHeight(false)` → no immediate resize; user regains free
  vertical drag.

### 5. Default behavior

- **New containers: `autoHeight = true`.** NereusSDR's primary UX is
  "hug-the-meter" per user feedback on PR #42. Differs from Thetis's
  default-false, but this is a Qt widget behavior choice (not a DSP or
  protocol port), so native NereusSDR default takes precedence per
  `feedback_source_first_ui_vs_dsp.md`.
- **Existing saved containers:** `autoHeight = false` (missing field →
  false). Avoids surprise shrinking of layouts the user has already
  arranged.

### 6. Revert the PR #42 "share-the-band" reflow

Once `AutoHeight` is in and defaulted on, the Thetis-parity `5%`-per-row
layout is what users actually want. The temporary share-the-band scheme
in `MeterWidget::reflowStackedItems()` (PR #42) should be reverted —
restore `kNormalRowHNorm = 0.05` with the 24 px floor. With `AutoHeight`
on, the container hugs the 5% rows; with it off, empty space is a
user-chosen state.

Keep the strict-overlap (`> 0.002`) clustering fix from PR #42 — that
one is independent and correct.

---

## Test plan

- Create a fresh ANAN MM container → hugs 44.1% aspect automatically.
- Create a stacked bar-only meter → hugs the row stack (no empty below).
- Add bar rows to an ANAN MM container → container grows.
- Toggle `AutoHeight` off → container becomes free-resize; meter
  renders at Thetis `5%` proportions with empty space.
- Drag-resize a container with `AutoHeight` on → width drags freely,
  height snaps back to natural each frame.
- Load a pre-AutoHeight saved settings file → containers open with
  `autoHeight = false`, layout unchanged.
- Floating vs docked, panel vs overlay: all four dock modes honor the
  auto-height path.
- Serialize → restart → deserialize preserves the per-container flag.

---

## Out of scope for this plan

- Thetis's `XRatio` / `YRatio` fields on the meter object
  (`clsMeter.XRatio/YRatio`) — NereusSDR currently assumes both are 1.0
  and doesn't persist them. If a future preset needs a non-square render
  rect, that's a separate port.
- The full `DirectXMeterRenderer` render-loop architecture. NereusSDR's
  QRhi-based renderer measures natural height at reflow time, not per
  frame, which is the right granularity for our needs.
