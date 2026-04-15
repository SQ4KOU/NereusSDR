# Waterfall Tuning Rationale

> **Status:** Active. Last revised 2026-04-15 for Phase 3G-9b PR2.

This doc explains *why* the NereusSDR smooth-default profile looks the way it does. Every value in `RadioModel::applyClaritySmoothDefaults()` has a one-line "because X" that comes from this file.

If you're tempted to change a default value in `RadioModel::applyClaritySmoothDefaults()`, read the corresponding section here first. If your change invalidates the rationale, update this doc at the same time.

## Context

NereusSDR's display surface was feature-complete after Phase 3G-8 (47 controls wired) but the **default** look — what a fresh install presents to a user who has never touched Setup → Display — was the same high-contrast "Enhanced" rainbow palette that shipped in 3G-4 for meter system development. Side-by-side comparison against AetherSDR v0.8.12 on 80m (2026-04-14 reference screenshots in `docs/architecture/waterfall-tuning/`) showed a significant readability gap:

- AetherSDR renders noise as uniform dark navy and signals as bright cyan/white
- NereusSDR painted the entire noise floor in saturated reds/greens/cyans, burying signals in visual clutter

This doc is the per-knob rationale for the profile that fixes that.

## Visual comparison

*Screenshots land in PR2 Task 7 — captured against the live radio after the smooth-defaults profile is applied. Placeholder until then.*

## The seven recipes

### 1. Waterfall palette: `WfColorScheme::ClarityBlue`

**Value:** new narrow-band gradient, 6 stops:

| pos | r | g | b |
|---|---|---|---|
| 0.00 | 0x0a | 0x14 | 0x28 |
| 0.60 | 0x0d | 0x25 | 0x40 |
| 0.70 | 0x18 | 0x50 | 0xa0 |
| 0.85 | 0x30 | 0x90 | 0xe0 |
| 0.95 | 0x80 | 0xd0 | 0xff |
| 1.00 | 0xff | 0xff | 0xff |

**Why:** The biggest single difference between AetherSDR and NereusSDR's previous default is palette philosophy. The Thetis-lineage "Enhanced" palette spreads colour across the *entire* dynamic range, which means the noise floor gets coloured too (red/green/yellow). That burns visual contrast on content the user doesn't care about. ClarityBlue reserves 60% of the normalised range for uniform dark navy — the noise floor becomes a quiet dark background — and compresses the actual signal energy into the top 15% where it transitions from cyan to near-white. Result: signals jump out of a quiet background, exactly the AetherSDR/SmartSDR look.

**Source:** AetherSDR visual observation. Thetis has no equivalent palette (its schemes are all full-range).

### 2. Spectrum averaging mode: `AverageMode::Logarithmic`

**Value:** `sw->setAverageMode(AverageMode::Logarithmic)`

**Why:** Thetis's "Log Recursive" averaging mode smooths the dB-domain spectrum trace rather than the linear-power trace. On SSB voice the result is gentle voice-envelope hills instead of instantaneous grass. NereusSDR's previous default was `Weighted` (plain exponential) which produced visible trace jitter under noise. Logarithmic matches the dB axis the user is reading, and it matches Thetis's own default.

**Source:** `Thetis setup.cs:18055` (`console.specRX.GetSpecRX(0).AverageMode`) and `display.cs` log-recursive smoothing path.

### 3. Averaging time constant: `0.05f` alpha

**Value:** `sw->setAverageAlpha(0.05f)`

**Why:** The alpha value is the weight given to each new FFT frame in the exponential smoothing `smoothed = alpha * new + (1-alpha) * previous`. At `0.05` each new frame contributes only 5% — the smoothing window is approximately `1/alpha = 20 frames`. At the default 30 FPS that's about 667 ms of settling time, which reads as "~500 ms smooth" to the eye once you factor in frame-to-frame correlation. Heavier values (0.01) feel laggy on tuning; lighter (0.20) still show grass. 0.05 was empirically picked for this profile.

**UI note:** the Setup → Display → Spectrum Defaults "Averaging Time" spinbox converts ms → alpha via `qBound(0.05f, 1.0f - ms/5000.0f, 0.95f)`. That formula is for the manual knob; the smooth-defaults profile bypasses it and sets 0.05 directly.

**Source:** AetherSDR observation + NereusSDR empirical tuning.

### 4. Trace colour: `#e0e8f0` alpha 200

**Value:** `sw->setFillColor(QColor(0xe0, 0xe8, 0xf0, 200))`

**Why:** A neutral light-gray trace sits in front of the waterfall without competing for attention. The previous default (`#00e5ff` saturated cyan) competed with the ClarityBlue palette's own cyan highlights, making it hard to tell where the trace ended and the signal colour began. Alpha 200 (out of 255) keeps the trace visible without fully occluding the waterfall behind it.

**Source:** AetherSDR observation.

### 5. Waterfall Low / High thresholds: -110 / -70 dBm

**Value:** `sw->setWfLowThreshold(-110.0f); sw->setWfHighThreshold(-70.0f);`

**Why:** This is a 40 dB window centred on the typical HF noise floor. The old NereusSDR default was -130 / -40, which is a 90 dB window — 50 dB of it wasted on content that rarely appears (nothing at -130, nothing at -40 except local QRN). Compressing the window so the noise floor sits near the bottom of the palette and strong signals hit the top gives Clarity Blue the dynamic range it needs to render noise vs. signal as distinct colours.

**Interaction with Waterfall AGC (§6):** when AGC is on, these thresholds are treated as the *initial* values; the renderer continuously adjusts them based on a running min/max of recent frames. The starting point matters less than AGC tuning parameters, but these values produce a sensible cold-start look before AGC settles.

**Source:** 80m empirical observation + AetherSDR visual reference. Thetis default is -80 / -130, which is a full 50 dB range but anchored differently — not directly comparable because Thetis uses a full-range palette.

### 6. Waterfall AGC: `true`

**Value:** `sw->setWfAgcEnabled(true)`

**Why:** AGC tracks band conditions automatically. Without it, users must manually retune thresholds when changing bands (80m noise floor vs. 6m noise floor can differ by 30 dB). With it on, the display is optimised across all bands with zero user intervention. This is the single control that most separates "needs tuning to use" from "just works".

**Source:** `Thetis setup.designer.cs:34069` (`chkRX1WaterfallAGC`) default enabled.

### 7. Waterfall update period: 30 ms

**Value:** `sw->setWfUpdatePeriodMs(30)`

**Why:** 30 ms between waterfall row pushes produces ~33 rows/second, which reads as smooth scroll motion rather than discrete steps. The old NereusSDR default of 50 ms (20 rows/sec) was visibly steppy on long fade-ins. Going below 30 ms starts wasting GPU bandwidth without perceptible improvement — the eye can't distinguish 40 rows/sec from 33 on a typical monitor. Thetis's 2 ms default is unnecessarily aggressive.

**Source:** AetherSDR observation.

## First-launch gate

The profile is applied exactly once on a fresh install — the mechanism is an AppSettings key:

```
DisplayProfileApplied = "True"
```

Set to `"True"` immediately after the first successful apply. Subsequent `RadioModel` constructions and `MainWindow` invocations check the key and short-circuit if it's already `"True"`. Existing users (upgrading from pre-3G-9b) see no change — their current values are preserved.

## Reset button

`Setup → Display → Spectrum Defaults` has a "Reset to Smooth Defaults" button at the top of the page. Guarded by a confirmation dialog because it overwrites the user's Spectrum / Waterfall settings. It does NOT touch FFT size, frequency, band stack, or per-band grid slots — those are navigation/identity state, not display preferences.

## Open questions for PR3 (Clarity adaptive)

PR3 builds an adaptive auto-tune system (`Clarity`) on top of these static defaults. Open questions at the time of this doc:

- Does AGC (§6 here) get superseded by Clarity's noise-floor estimator? Likely yes — Clarity's estimator is more robust than the running min/max AGC, and having both produces conflicting behaviour.
- Should the static defaults in §5 be replaced by adaptive initial values? Probably keep them as the fallback when Clarity is off.
- Does the first-launch gate key (`DisplayProfileApplied`) need to track the profile *version* so future smooth-defaults revisions can re-run on upgrade? Yes — PR3 should migrate this key to `DisplayProfileVersion` with an integer version number.

See `docs/architecture/2026-04-15-display-refactor-design.md` §6 for the PR3 design.
