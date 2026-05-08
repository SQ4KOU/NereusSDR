# Multimeter — v0.3.0

Added in Task 3A of the Thetis Display + DSP parity audit. Covers the new
**Meter Styles → Multimeter** setup sub-page plus several controls relocated
from Setup → General (relocated to keep related settings together).

## How to use

Open **Settings → Meter Styles → Multimeter** for the core multimeter controls.
Some relocated controls are under **Settings → General → Options** (CPU meter)
and **Settings → Radio Info** (ANAN-G2 volts/amps) and **Settings → Diagnostics
→ Logging & Performance** (spectrum warning/purge).
Screenshot prefix convention: `MM-` for Multimeter.

---

### Core Multimeter Controls

| # | Control name (UI label) | AppSettings key | Default value | Expected behavior on change | Screenshot expectation |
|---|---|---|---|---|---|
| MM-1 | Update delay (ms) | `MultimeterDelayMs` | 100 | Increase to 1000: S-meter needle/bar updates once per second instead of 10× | `mm-delay-ms.png` |
| MM-2 | Peak hold (ms) | `MultimeterPeakHoldMs` | 1500 | Increase to 5000: peak hold marker lingers 5 seconds before dropping | `mm-peak-hold-ms.png` |
| MM-3 | Text hold (ms) | `MultimeterTextHoldMs` | 1500 | Increase to 5000: peak text overlay holds for 5 seconds | `mm-text-hold-ms.png` |
| MM-4 | Average window | `MultimeterAverageWindow` | 1 | Increase to 5: S-meter reading averages over 5 samples (smoother, less responsive) | `mm-average-window.png` |
| MM-5 | Digital delay (ms) | `MultimeterDigitalDelayMs` | 100 | Increase to 500: digital numeric S-meter readout updates at 2 Hz | `mm-digital-delay-ms.png` |
| MM-6 | Show decimal | `MultimeterShowDecimal` | True | Disable: S-meter numeric readout shows integer dBm only | `mm-show-decimal.png` |

### Unit Mode

| # | Control name (UI label) | AppSettings key | Default value | Expected behavior on change | Screenshot expectation |
|---|---|---|---|---|---|
| MM-7 | Unit mode (S / dBm / µV) | `MultimeterUnitMode` | dBm | Switch to S: S-meter reads "S9+10 dB" style; switch to µV: reads in microvolts | `mm-unit-mode-s.png` |

### Signal History

| # | Control name (UI label) | AppSettings key | Default value | Expected behavior on change | Screenshot expectation |
|---|---|---|---|---|---|
| MM-8 | Enable signal history | `MultimeterSignalHistoryEnabled` | False | History graph appears in the S-meter area showing signal level over time | `mm-signal-history-on.png` |
| MM-9 | History duration (ms) | `MultimeterSignalHistoryDurationMs` | 60000 | Change to 300000 (5 min): history graph spans 5 minutes of level data | `mm-signal-history-duration.png` |

### Relocated Controls (from Setup → General / other pages)

| # | Control name (UI label) | AppSettings key | Default value | Expected behavior on change | Screenshot expectation |
|---|---|---|---|---|---|
| MM-10 | Small mode filter on VFOs (Meter Styles page) | `AppearanceSmallModeFilterOnVfos` | False | Filter bandwidth indicator on the VFO flag shrinks to compact form | `mm-small-mode-filter.png` |
| MM-11 | Show volts/amps (Radio Info — ANAN-8000DLE cap-gated) | `HardwareAnan8000DleShowVoltsAmps` | False | PA voltage and current meters appear in the status strip (ANAN-8000DLE only) | `mm-anan-volts-amps.png` |
| MM-12 | Spec warning LED: render delay (Diagnostics) | `DiagnosticsSpecWarningLedRenderDelay` | False | Enables a warning LED that lights when spectrum render takes too long | `mm-spec-warning-render.png` |
| MM-13 | Spec warning LED: get pixels (Diagnostics) | `DiagnosticsSpecWarningLedGetPixels` | False | Enables a warning LED that lights when pixel readback is slow | `mm-spec-warning-pixels.png` |
| MM-14 | Purge buffers (Diagnostics) | `DiagnosticsPurgeBuffers` | False | Instructs FFTEngine to purge internal accumulation buffers (debugging aid) | `mm-purge-buffers.png` |
| MM-15 | CPU meter update rate (Hz) | `GeneralCpuMeterUpdateRateHz` | 1 | Increase to 10: CPU percentage in the title bar updates 10× per second | `mm-cpu-meter-rate.png` |

## Notes

- MM-11 is visible only on radios with `caps.hasAnan8000Dle` capability flag set.
  On non-ANAN-8000DLE radios the row is hidden.
- MM-12/MM-13/MM-14 are "wire-up pending" controls (TODO comments in code);
  they persist and load correctly but the wire-up to SpectrumWidget/FFTEngine is deferred.
  Verify no crash on toggle.
- MM-7 unit mode combo items: "S", "dBm", "µV" — all three must be present and selectable.
