# Spectrum Peaks — v0.3.0

Added in Task 2.5 (Section 2C of the Thetis Display + DSP parity audit). The
Spectrum Peaks page is a new setup page covering two independent peak systems:
Active Peak Hold (a decaying trace overlay) and Peak Blobs (labeled frequency
markers for the N strongest signals).

## How to use

Open **Settings → Display → Spectrum Peaks**.
Screenshot prefix convention: `SP-` for Spectrum Peaks.

---

### Active Peak Hold

| # | Control name (UI label) | AppSettings key | Default value | Expected behavior on change | Screenshot expectation |
|---|---|---|---|---|---|
| SP-1 | Enable Active Peak Hold | `DisplayActivePeakHoldEnabled` | False | Dotted peak trace appears above the live spectrum trace; decays over time | `sp-aph-enable-on.png` |
| SP-2 | Duration (ms) | `DisplayActivePeakHoldDurationMs` | 2000 | Increase to 10000: peak trace decays much more slowly | `sp-aph-duration.png` |
| SP-3 | Drop rate (dB/s) | `DisplayActivePeakHoldDropDbPerSec` | 6 | Decrease to 1: peak trace falls very slowly after signal drops | `sp-aph-drop-rate.png` |
| SP-4 | Fill under peak trace | `DisplayActivePeakHoldFill` | False | Filled area between live trace and peak trace appears | `sp-aph-fill-on.png` |
| SP-5 | Freeze peak hold on TX | `DisplayActivePeakHoldOnTx` | False | Peak trace freezes (no decay) while transmitting — requires TX state to verify | `sp-aph-on-tx.png` |

### Peak Blobs

| # | Control name (UI label) | AppSettings key | Default value | Expected behavior on change | Screenshot expectation |
|---|---|---|---|---|---|
| SP-6 | Enable Peak Blobs | `DisplayPeakBlobsEnabled` | True | Labeled peak markers appear at the top of each prominent signal peak | `sp-blobs-enable-off.png` |
| SP-7 | Blob count | `DisplayPeakBlobsCount` | 3 | Increase to 8: more blob markers appear (up to 20 max) | `sp-blobs-count.png` |
| SP-8 | Inside filter only | `DisplayPeakBlobsInsideFilterOnly` | False | Blobs are restricted to within the current RX filter passband | `sp-blobs-inside-filter.png` |
| SP-9 | Hold blobs | `DisplayPeakBlobsHoldEnabled` | False | Blob markers persist for the hold duration before clearing | `sp-blobs-hold-on.png` |
| SP-10 | Hold duration (ms) | `DisplayPeakBlobsHoldMs` | 500 | Increase to 3000: blob markers linger noticeably longer | `sp-blobs-hold-ms.png` |
| SP-11 | Drop held blobs | `DisplayPeakBlobsHoldDrop` | False | Held blobs gradually fade/drop rather than disappearing suddenly | `sp-blobs-hold-drop.png` |
| SP-12 | Fall rate (dB/s) | `DisplayPeakBlobsFallDbPerSec` | 6 | Decrease to 1: blob markers descend very slowly when signal drops | `sp-blobs-fall-rate.png` |
| SP-13 | Blob marker color (swatch) | `DisplayPeakBlobColor` | OrangeRed (#FF4500FF) | Blob marker background color changes | `sp-blobs-color.png` |
| SP-14 | Blob text color (swatch) | `DisplayPeakBlobTextColor` | Chartreuse (#7FFF00FF) | Frequency label text on blob markers changes color | `sp-blobs-text-color.png` |

## Notes

- SP-6 `PeakBlobsEnabled` defaults to **True** (Thetis `display.cs:4395 [v2.10.3.13]`
  `ShowPeakBlobs = true`). Expect blobs visible on first launch without any configuration.
- SP-7 maximum blob count is 20 (validated in SpectrumPeaksPage spinbox range).
- SP-5 and SP-11: TX-state-dependent behaviors require an active TX session to fully verify;
  confirm no crash when toggled without TX.
- SP-13 default `#FF4500FF` = OrangeRed with full opacity.
- SP-14 default `#7FFF00FF` = Chartreuse with full opacity.
