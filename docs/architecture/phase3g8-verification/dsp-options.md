# DSP Options — v0.3.0

Added in Phase 4 of the Thetis Display + DSP parity audit (Tasks 4.1–4.5). The
**Setup → DSP → DSP Options** page exposes per-mode WDSP buffer sizes, filter
sizes, and filter types, plus impulse caching toggles and high-resolution filter
characteristics. Changes take effect live via a Phase 1 channel rebuild.

## How to use

Open **Settings → DSP → DSP Options**.
Screenshot prefix convention: `DO-` for DSP Options. Where behavior is auditory
(glitch/dropout on rebuild), note the approximate dropout duration instead of a
screenshot.

---

### Buffer Size (4 combos)

| # | Control name (UI label) | AppSettings key | Default value | Expected behavior on change | Screenshot expectation |
|---|---|---|---|---|---|
| DO-1 | Buffer Size — Phone (SSB/AM) | `DspOptionsBufferSizePhone` | 256 | Change to 512: channel rebuilds; slight audio glitch audible | `do-buf-phone.png` |
| DO-2 | Buffer Size — CW | `DspOptionsBufferSizeCw` | 256 | Change to 512: channel rebuilds when switching to CW mode | `do-buf-cw.png` |
| DO-3 | Buffer Size — Digital | `DspOptionsBufferSizeDig` | 256 | Change to 512: channel rebuilds when switching to Digital mode | `do-buf-dig.png` |
| DO-4 | Buffer Size — FM | `DspOptionsBufferSizeFm` | 256 | Change to 512: channel rebuilds when switching to FM mode | `do-buf-fm.png` |

### Filter Size (4 combos)

| # | Control name (UI label) | AppSettings key | Default value | Expected behavior on change | Screenshot expectation |
|---|---|---|---|---|---|
| DO-5 | Filter Size — Phone (SSB/AM) | `DspOptionsFilterSizePhone` | 4096 | Change to 2048: channel rebuilds; filter transition band slightly wider | `do-filt-phone.png` |
| DO-6 | Filter Size — CW | `DspOptionsFilterSizeCw` | 4096 | Change to 2048: channel rebuilds when in CW mode | `do-filt-cw.png` |
| DO-7 | Filter Size — Digital | `DspOptionsFilterSizeDig` | 4096 | Change to 2048: channel rebuilds when in Digital mode | `do-filt-dig.png` |
| DO-8 | Filter Size — FM | `DspOptionsFilterSizeFm` | 4096 | Change to 2048: channel rebuilds when in FM mode | `do-filt-fm.png` |

### Filter Type (7 combos)

| # | Control name (UI label) | AppSettings key | Default value | Expected behavior on change | Screenshot expectation |
|---|---|---|---|---|---|
| DO-9  | Filter Type — Phone RX | `DspOptionsFilterTypePhoneRx` | Low Latency | Change to Linear Phase: channel rebuilds; filter shows better stopband rejection in high-res characteristic plot | `do-type-phone-rx.png` |
| DO-10 | Filter Type — Phone TX | `DspOptionsFilterTypePhoneTx` | Linear Phase | Change to Low Latency: channel rebuilds with reduced group-delay filter | `do-type-phone-tx.png` |
| DO-11 | Filter Type — CW RX | `DspOptionsFilterTypeCwRx` | Low Latency | Change to Linear Phase: CW channel rebuilds on mode switch | `do-type-cw-rx.png` |
| DO-12 | Filter Type — Digital RX | `DspOptionsFilterTypeDigRx` | Linear Phase | Change to Low Latency: digital RX channel rebuilds | `do-type-dig-rx.png` |
| DO-13 | Filter Type — Digital TX | `DspOptionsFilterTypeDigTx` | Linear Phase | Change to Low Latency: digital TX channel rebuilds | `do-type-dig-tx.png` |
| DO-14 | Filter Type — FM RX | `DspOptionsFilterTypeFmRx` | Low Latency | Change to Linear Phase: FM RX channel rebuilds | `do-type-fm-rx.png` |
| DO-15 | Filter Type — FM TX | `DspOptionsFilterTypeFmTx` | Linear Phase | Change to Low Latency: FM TX channel rebuilds | `do-type-fm-tx.png` |

### Impulse Cache

| # | Control name (UI label) | AppSettings key | Default value | Expected behavior on change | Screenshot expectation |
|---|---|---|---|---|---|
| DO-16 | Cache impulse responses | `DspOptionsCacheImpulse` | False | Enable: WDSP caches filter impulse responses; rebuild is faster on subsequent mode changes | `do-cache-impulse.png` |
| DO-17 | Save/restore cache across launches | `DspOptionsCacheImpulseSaveRestore` | False | Enable (requires DO-16 on): cache is written to disk and loaded on next launch | `do-cache-save-restore.png` |

### High-Resolution Filter Characteristics

| # | Control name (UI label) | AppSettings key | Default value | Expected behavior on change | Screenshot expectation |
|---|---|---|---|---|---|
| DO-18 | High-res filter characteristics | `DspOptionsHighResFilterCharacteristics` | False | Enable: the FilterDisplayItem meter item switches to 2048-point characteristic plot (finer frequency resolution) | `do-high-res-filter.png` |

### Live Readout and Warning Icons

| # | Control name (UI label) | AppSettings key | Default value | Expected behavior on change | Screenshot expectation |
|---|---|---|---|---|---|
| DO-19 | Time to last change (readout label) | *(no AppSettings — live signal)* | N/A | Label updates to show elapsed time since last DSP parameter change; subscribes to `dspChangeMeasured` signal | `do-time-readout.png` |
| DO-20 | Warning icon — buffer size | *(computed from validity rule)* | Hidden | Appears when buffer size exceeds 2× filter size or is not a power-of-2 | `do-warn-buffer.png` |
| DO-21 | Warning icon — filter size | *(computed from validity rule)* | Hidden | Appears when filter size is not a power-of-2 or is below the WDSP minimum | `do-warn-filter.png` |
| DO-22 | Warning icon — filter type | *(computed from validity rule)* | Hidden | Appears when filter type is incompatible with the current buffer/filter combination | `do-warn-type.png` |

## Notes

- Buffer and filter size changes take effect immediately via a Phase 1 channel rebuild.
  Expect a brief audio dropout (target < 200 ms; see `infrastructure-live-apply.md`).
- DO-17 (save/restore) depends on DO-16; the checkbox should be disabled when DO-16 is off.
- DO-9 through DO-15 type change only applies when the current RX/TX mode matches the row's
  mode. Changes for other modes are deferred until the radio switches to that mode.
- DO-18 takes effect immediately on the FilterDisplayItem in any open meter container.
- Warning icon rows (DO-20–DO-22) are expected to remain hidden during normal operation;
  trigger by entering known-bad combos (e.g. buffer=512, filter=256).
