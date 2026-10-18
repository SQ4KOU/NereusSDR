# Waterfall Defaults Changes — v0.3.0

Changes and additions to the Waterfall Defaults setup page in Tasks 2.1 and 2.8
(Sections 2A and 2D of the Thetis Display + DSP parity audit). Includes the
Waterfall NF-AGC group, Stop on TX, a live delay readout, and the Detector +
Averaging split combos. The Reverse Scroll control (W5 in the 3G-8 matrix) was
**removed** from the UI and its AppSettings key retired in the v0.3.0 migration.

## How to use

Open **Settings → Display → Waterfall Defaults**.
Screenshot prefix convention: `WD-` for Waterfall Defaults changes.

---

| # | Control name (UI label) | AppSettings key | Default value | Expected behavior on change | Screenshot expectation |
|---|---|---|---|---|---|
| WD-1 | Enable NF-AGC | `WaterfallNFAGCEnabled` | False | Waterfall low/high thresholds automatically track the estimated noise floor | `wd-nf-agc-enable-on.png` |
| WD-2 | NF offset (dB) | `WaterfallAGCOffsetDb` | 0 | Change to −20: low threshold placed 20 dB below noise floor — waterfall baseline adjusts | `wd-agc-offset-db.png` |
| WD-3 | Stop on TX | `WaterfallStopOnTx` | False | Waterfall scroll pauses when transmitting; resumes when TX ends — requires TX session | `wd-stop-on-tx.png` |
| WD-4 | Calculated delay readout | *(no AppSettings — live label)* | N/A | Label displays "Delay: NN.N s" computed from visible rows × update period; updates live as update period changes | `wd-delay-readout.png` |
| WD-5 | WF Detector | `DisplayWaterfallDetector` | Peak (0) | Change to Average: waterfall shows time-averaged bins per pixel instead of peak | `wd-detector-combo.png` |
| WD-6 | WF Averaging (new) | `DisplayWaterfallAveraging` | None (0) | Change to Recursive: waterfall rows exponentially smooth in linear power space | `wd-averaging-combo.png` |
| WD-7 | Copy spectrum min/max → waterfall (button) | *(no AppSettings — one-shot action)* | N/A | Copies spectrum High/Low threshold spinboxes into waterfall High/Low threshold spinboxes | `wd-copy-button.png` |

## Removed control

- **W5 Reverse Scroll** (`DisplayReverseWaterfallScroll`) — removed in Task 2.8.
  The key is retired by the v0.3.0 settings schema migration (schema v3). No row needed;
  confirm the control is absent from the UI after upgrade.

## Notes

- WD-1/WD-2: when NF-AGC is off, the low/high threshold sliders operate
  independently as in 3G-8. When NF-AGC is on, sliders still show but are
  overridden by the computed noise floor estimate + offset.
- WD-2 range: −60 to +60 dB.
- WD-5 Detector items: Peak / Rosenfell / Average / Sample (4 items; no RMS unlike
  the spectrum detector which has 5).
- WD-6 Averaging items: None / Recursive / Time Window / Log Recursive (different
  from the spectrum averaging combo).
- WD-7 is a one-shot copy button; verify the waterfall threshold spinboxes update
  and persisted values change.
