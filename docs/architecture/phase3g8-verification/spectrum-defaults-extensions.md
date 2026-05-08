# Spectrum Defaults Extensions — v0.3.0

Added in Task 2.3 (Section 2B of the Thetis Display + DSP parity audit). Extends
the Spectrum Defaults setup page with 11 new overlay and normalization controls.

## How to use

Open **Settings → Display → Spectrum Defaults**, scroll to the Overlays section.
For each row: record the default state, change it, click Apply, capture screenshots.
Screenshot prefix convention: `SD-` for Spectrum Defaults extensions.

---

| # | Control name (UI label) | AppSettings key | Default value | Expected behavior on change | Screenshot expectation |
|---|---|---|---|---|---|
| SD-1 | Show cursor frequency | `DisplayShowMHzOnCursor` | False | Frequency label appears at cursor position on the spectrum area | `sd-show-mhz-on-cursor-on.png` |
| SD-2 | Show bin width | `DisplayShowBinWidth` | False | Bin-width readout (e.g. "23.438 Hz/bin") appears in spectrum corner | `sd-show-bin-width-on.png` |
| SD-3 | Show noise floor | `DisplayShowNoiseFloor` | False | Noise floor dBm level text appears at the selected corner | `sd-show-noise-floor-on.png` |
| SD-4 | Noise floor position | `DisplayShowNoiseFloorPosition` | BottomLeft (2) | Corner label relocates to chosen corner (TopLeft/TopRight/BottomLeft/BottomRight) | `sd-noise-floor-position.png` |
| SD-5 | Normalize to 1 Hz | `DisplayDispNormalize` | False | Spectrum trace shifts down by 10×log10(bin_width) dB (normalizes power to 1 Hz bandwidth) | `sd-disp-normalize-on.png` |
| SD-6 | Show peak value overlay | `DisplayShowPeakValueOverlay` | False | Peak signal value text appears at the selected corner, refreshed per delay setting | `sd-peak-value-overlay-on.png` |
| SD-7 | Peak value position | `DisplayPeakValuePosition` | TopRight (1) | Corner text relocates to chosen corner | `sd-peak-value-position.png` |
| SD-8 | Peak value delay (ms) | `DisplayPeakTextDelayMs` | 500 | Increase to 5000: peak text updates visibly more slowly | `sd-peak-text-delay.png` |
| SD-9 | Peak value color (swatch) | `DisplayPeakValueColor` | DodgerBlue (#1E90FFFF) | Peak text color changes to selected color | `sd-peak-value-color.png` |
| SD-10 | Get Monitor Hz (button) | *(no AppSettings — utility action)* | N/A | Queries screen refresh rate and snaps the FPS slider to that value | `sd-get-monitor-hz.png` |
| SD-11 | Decimation | `DisplayDecimation` (S16) | 1 | Combo wired; renderer scaffold only in 3G-8 — no visible spectrum change expected | `sd-decimation.png` |

## Notes

- SD-4 OverlayPosition enum: 0=TopLeft, 1=TopRight, 2=BottomLeft, 3=BottomRight.
  The persisted default `"2"` → BottomLeft; the C++ member default is `OverlayPosition::BottomLeft`.
- SD-7 persisted default `"1"` → TopRight; the C++ member default is `OverlayPosition::TopRight`.
- SD-9 default comes from Thetis `console.cs:20278 [v2.10.3.13]` `Color.DodgerBlue (#1E90FF)`.
- SD-10 is a one-shot utility — no persistence, no screenshot delta needed beyond confirming the
  FPS slider moved.
- SD-11 renderer effect deferred; verify no crash and that the combo persists across restarts.
