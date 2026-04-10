# CTUN Zoom — Bin Subsetting Design

**Date:** 2026-04-10
**Status:** Approved
**Scope:** Make CTUN zoom visually functional — spectrum trace, waterfall, and
signal widths must scale with `m_bandwidthHz`.

## Problem

When the user drags the frequency scale bar to zoom, `m_bandwidthHz` changes
and `bandwidthChangeRequested` fires (adjusting FFT size), but the spectrum
trace, waterfall, and signal widths don't visually scale. The renderer always
maps all N FFT bins linearly across the display, ignoring the zoom window.

## Approach: Hybrid Zoom (Option C)

**During drag:** Bin subsetting only — select the visible subset of FFT bins
matching `m_centerHz ± m_bandwidthHz/2` and stretch them across the full
display width. Smooth, no FFT pipeline disruption.

**On mouse release:** Emit `bandwidthChangeRequested` once, triggering an FFT
replan in MainWindow. The new FFT size gives better frequency resolution at
the zoomed bandwidth.

## Bin Selection Math

FFT delivers N bins spanning the full DDC sample rate (768 kHz). Each bin:

```
binWidth  = m_sampleRateHz / N
binFreq(i) = m_ddcCenterHz - m_sampleRateHz/2 + i * binWidth
```

For display window `m_centerHz ± m_bandwidthHz/2`:

```
lowHz  = m_centerHz - m_bandwidthHz / 2
highHz = m_centerHz + m_bandwidthHz / 2

firstBin = floor((lowHz  - (m_ddcCenterHz - m_sampleRateHz/2)) / binWidth)
lastBin  = ceil ((highHz - (m_ddcCenterHz - m_sampleRateHz/2)) / binWidth)

// Clamped to [0, N-1]
```

A helper `visibleBinRange()` returns `{firstBin, lastBin}`, used by both GPU
and CPU render paths.

## SpectrumWidget State Changes

New members:

```cpp
double m_ddcCenterHz{14225000.0};   // DDC hardware center (read-only from widget)
double m_sampleRateHz{768000.0};    // DDC sample rate
void setDdcCenterFrequency(double hz);
void setSampleRate(double hz);
```

- `m_ddcCenterHz` — set by MainWindow when DDC frequency is established and on
  `hardwareFrequencyChanged`. Widget never modifies it.
- `m_sampleRateHz` — set once from MainWindow, replaces hardcoded 768000.0.
- `m_centerHz` / `m_bandwidthHz` — unchanged, define the display window.
- `visibleBinRange()` bridges the DDC bin space to the display window.

## Rendering Pipeline Changes

### GPU path (`renderGpuFrame`)

Replace full-array loop:

```cpp
auto [first, last] = visibleBinRange();
int count = last - first + 1;
for (int i = first; i <= last; ++i) {
    float x = 2.0f * (i - first) / (count - 1) - 1.0f;  // NDC
    // y, color unchanged
}
```

Vertex count passed to `draw()` changes from `n` to `count`. VBO capacity
stays at `kMaxFftBins` — no reallocation.

### CPU path (`drawSpectrum`)

Same subsetting: `xStep = specRect.width() / count`, iterate `first..last`.

### Overlay (`drawOverlay` / frequency grid)

No changes — `hzToX()` already uses `m_centerHz`/`m_bandwidthHz`.

## Zoom Drag Handler Changes

**During drag (`m_draggingBandwidth`):**
- `m_bandwidthHz` and `m_centerHz` update as before.
- `m_centerHz = m_vfoHz` (recenter on VFO).
- `bandwidthChangeRequested` does NOT fire during drag.
- Display zooms via bin subsetting on next frame.

**On mouse release (`mouseReleaseEvent`):**
- Detect `m_draggingBandwidth` was active.
- Emit `bandwidthChangeRequested(m_bandwidthHz)` once.
- MainWindow replans FFT to match final bandwidth.

**Zoom slider (MainWindow):**
- Continues to emit `bandwidthChangeRequested` immediately (discrete change).

## Waterfall Changes

`pushWaterfallRow()` pushes only the visible bin subset (`first..last` from
`visibleBinRange()`). Each row is interpolated to the waterfall texture width
(which matches the widget's pixel width — set at `m_waterfall.width()`).
The existing `binScale = bins.size() / w` interpolation already handles
variable-length input; passing fewer bins just changes the scale factor.

**Existing rows are preserved.** Old rows stay at their original zoom
resolution. New rows render at the current zoom scale. This creates a visible
seam between zoom levels — intentional, may be reverted to clear-on-zoom if
the visual result is unacceptable.

## FFT Replan on Drag End

After mouse release, MainWindow receives `bandwidthChangeRequested` and
replans the FFT:

```
fftSize = sampleRate * targetBins / bwHz   (target ~1000 visible bins)
```

Rounded to next power of 2, clamped to [1024, 65536]. The new FFT delivers
more bins in the visible window — `visibleBinRange()` recalculates
automatically.

**During-drag resolution:** Before replan, a narrow zoom with a small FFT
shows few bins stretched across the display (visibly blocky). Expected and
acceptable — resolves on release.

## CTUN Interaction

**CTUN mode (DDC locked):**
- `m_ddcCenterHz` stays constant, `m_centerHz` recenters on VFO when zooming.
- `visibleBinRange()` correctly maps the display window to bins using
  `m_ddcCenterHz` for the FFT coordinate system.
- Boundary clamp: if the visible window extends beyond the DDC passband,
  `visibleBinRange()` clamps to `[0, N-1]`. Display shows a partial window.

**Non-CTUN mode (traditional):**
- DDC follows VFO, so `m_ddcCenterHz` tracks `m_centerHz`.
- `visibleBinRange()` returns a symmetric subset. Zoom works identically.

## Thetis Comparison (READ -> SHOW -> TRANSLATE)

Thetis uses `rx_display_low`/`rx_display_high` as the visible frequency
window and maps bins with `(freq - Low) / width * W`. Our
`visibleBinRange()` + linear NDC mapping is the equivalent — same math,
different coordinate space (bin indices vs pixel coordinates).

Thetis clears the waterfall bitmap on span change. We preserve rows (may
revert to match Thetis if the seam is unacceptable).

## Files Modified

| File | Changes |
|------|---------|
| `src/gui/SpectrumWidget.h` | Add `m_ddcCenterHz`, `m_sampleRateHz`, `visibleBinRange()`, setters |
| `src/gui/SpectrumWidget.cpp` | Bin subsetting in GPU + CPU render, waterfall subset, drag handler signal move |
| `src/gui/MainWindow.cpp` | Wire `m_ddcCenterHz`/`m_sampleRateHz`, connect `hardwareFrequencyChanged` |
| `src/core/FFTEngine.cpp` | No changes — still emits full bin array |

## Edge Cases

- **Zero-width zoom:** `m_bandwidthHz` clamped to minimum 1000 Hz.
- **VFO near DDC edge + narrow zoom:** Partial window, clamped bins. Signal
  may appear off-center.
- **FFT size change mid-frame:** `visibleBinRange()` uses `m_smoothed.size()`
  which updates atomically per frame. No race.
- **Non-CTUN zoom:** Works identically since `m_ddcCenterHz` tracks VFO.
