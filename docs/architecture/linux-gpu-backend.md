# Linux GPU Backend: OpenGL, Not Vulkan

**Decision:** On Linux, `SpectrumWidget` and `MeterWidget` use Qt's default
QRhiWidget backend, which is **OpenGL**. Do not call
`setApi(QRhiWidget::Api::Vulkan)` on the Linux branch. Do not set
`Qt::WA_NativeWindow` on the Linux branch.

**Status:** Active constraint, adopted 2026-04-13.

## TL;DR

Linux Vulkan + `Qt::WA_NativeWindow` + NereusSDR's semi-transparent Qt
overlay widgets are a three-way structural conflict. OpenGL on Linux is
the only configuration that makes the overlays composite correctly, and
it is the same choice AetherSDR (our architectural reference) makes.

## Why this came up

A session on 2026-04-13 added `setApi(Vulkan) + WA_NativeWindow` to the
Linux branch of `SpectrumWidget` (and briefly `MeterWidget`). The app
initially hung on first I/Q (unrelated: DSP deadlock, already fixed
upstream in `cd86ffb`). Once the deadlock fix was picked up, Vulkan
booted, and four new cosmetic bugs appeared that OpenGL had never had:

- Waterfall texture rendered upside down.
- Spectrum trace rendered upside down.
- Window chrome around the waterfall showed the desktop through
  transparent pixels.
- `SpectrumOverlayPanel` (the Phone/CW-style overlay panel) and the
  container panel backgrounds showed the desktop through transparent
  pixels and flashed as layout events shuffled the native subsurface.

Items 1 and 2 are real Vulkan port bugs (NDC Y-convention; `isYUpInNDC()`
returns `false` on Vulkan, and the pass-through vertex shader plus
CPU-built FFT geometry both assume the OpenGL Y-up convention). They are
fixable in code. Items 3 and 4 are not — they are an architectural
conflict, described below.

## The structural conflict

NereusSDR overlays semi-transparent Qt child widgets on top of the GPU
spectrum widget:

- `src/gui/SpectrumOverlayPanel.cpp` — `WA_NoSystemBackground`,
  `background: rgba(15, 15, 26, 220)` stylesheets, floats over the
  spectrum as a Qt child widget.
- `src/gui/widgets/VfoWidget.cpp` — `WA_TranslucentBackground`,
  `background: rgba(...)` and `background: transparent` stylesheets,
  custom `paintEvent()`.
- `ContainerWidget` / `FloatingContainer` — semi-transparent chrome
  using the same Qt stylesheet path.

For semi-transparent Qt children to composite correctly, Qt needs to
blend the child's pixels against the parent's backing-store pixels.
`QRhiWidget`'s OpenGL path does this: it renders into an offscreen FBO
and Qt blits the FBO into the parent's backing store, where the
overlays can then blend on top.

`QRhiWidget` on Vulkan with `WA_NativeWindow` does **not** go through
the backing store. It renders directly to a `VkSwapchainKHR`, which is
the window manager's business, not Qt's. Qt has no read access to the
Vulkan swapchain image, so it has nothing for the semi-transparent
overlay children to blend against. Those children then have
"nothing underneath" in Qt's eyes, and the compositor lets the desktop
show through — hence the transparent panel backgrounds, the transparent
window chrome, and the flashing as layout events shuffle the native
subsurface.

This is not a bug in NereusSDR or in Qt; it is a limitation of mixing
native-surface GPU widgets with semi-transparent composited Qt
children. Qt's public API does not expose a way to make `QRhiWidget`
render Vulkan through an offscreen FBO the way it does for OpenGL.

## Evidence

- `gdb thread apply 1 bt` on the running app confirmed the original
  deadlock was in `fexchange2 -> LinuxWaitForSingleObject`, not in
  Vulkan — that was a red herring. Fixed separately in `cd86ffb`.
- Log line with Vulkan enabled:
  `SpectrumWidget: QRhi backend: Vulkan isYUpInNDC: false isYUpInFramebuffer: false`
  — confirmed Qt does not emulate OpenGL Y-up on Vulkan.
- Quad-flip fix (swapping texcoord V) fixed the waterfall. CPU `yBot` /
  `yTop` sign-flip fixed the spectrum trace. Neither fix touched the
  overlay transparency.
- `Qt::WA_OpaquePaintEvent` on `SpectrumWidget` did not resolve the
  overlay transparency — Qt still could not see into the Vulkan
  swapchain.
- Removing `Qt::WA_NativeWindow` caused Vulkan init to silently fail
  (`QRhiWidget: No QRhi` warnings, no `Initializing QRhi Vulkan backend`
  log). The Vulkan path on Linux requires a native window for
  `VkSurfaceKHR` creation.

## Alternatives considered (and rejected)

- **Render all overlays on the GPU instead of as Qt child widgets.**
  Non-trivial: `SpectrumOverlayPanel` has interactive buttons,
  flyout sub-panels, live state binding. `VfoWidget` has click/scroll
  wheel input handling. Re-implementing them via `QRhi` + `QPainter`
  inside `SpectrumWidget`'s render pass is a multi-week project and
  would diverge from AetherSDR's architecture.
- **Promote the overlay panels to top-level windows.** Changes the
  widget hierarchy, breaks docking, breaks floating container
  behavior, and on Wayland still has compositing edge cases.
- **Make overlays fully opaque (drop `rgba` alpha).** Visual regression
  the maintainer does not want.
- **Use Vulkan without `WA_NativeWindow`.** Qt does not support this
  for `QRhiWidget` — `rhi()` returns null, widget never paints.

## What AetherSDR does

AetherSDR (our architectural reference, https://github.com/ten9876/AetherSDR)
targets Linux as its primary platform and ships the same overlay
pattern NereusSDR uses (`SpectrumOverlayMenu`, `VfoWidget` with
`WA_TranslucentBackground` and rgba stylesheets). AetherSDR's
`SpectrumWidget` only calls `setApi()` on macOS (Metal) and only sets
`WA_NativeWindow` on macOS. On Linux and Windows it falls through to
Qt's default (OpenGL). There are zero Vulkan references in the AetherSDR
tree. They never documented a workaround for the Vulkan/overlay
conflict — they picked the backend that avoids triggering it.

NereusSDR's Linux/OpenGL choice therefore matches the reference
architecture, not a regression from it.

## Revisit criteria

Revisit this decision if any of the following happen:

- Qt exposes a public API to render `QRhiWidget` with Vulkan through an
  offscreen FBO (i.e. without `WA_NativeWindow`) so Qt's backing-store
  compositor can see the Vulkan output.
- NereusSDR removes all semi-transparent Qt child widgets from
  `SpectrumWidget` and `MeterWidget` (draws all overlays on the GPU).
- A benchmark shows OpenGL is a material performance bottleneck that
  Vulkan would fix, *and* one of the two prior conditions is met.

Measure twice before flipping the switch — Linux OpenGL was already
rendering the spectrum and waterfall at full FPS with the DSP fix in
`cd86ffb`, so there is no performance motivation at present.
