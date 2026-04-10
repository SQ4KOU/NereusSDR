# Phase 3E: 768 kHz DDC + CTUN Shift — Session Notes

**Date:** 2026-04-10
**Branch:** main
**Status:** Complete — all bugs fixed (see Known Bugs section)

---

## What Was Done

### 768 kHz DDC Upgrade
- DDC2 sample rate raised from 48 kHz to 768 kHz, matching Thetis pcap
- WDSP OpenChannel: `inputSampleRate=768000, dspSampleRate=48000, outputSampleRate=48000`
- WDSP decimates 768k→48k internally; output is 64 samples per 1024-sample input
  (`out_size = in_size * out_rate / in_rate = 1024 * 48000 / 768000 = 64`)
- FFTEngine sample rate updated to 768k for correct frequency axis
- Spectrum widget bandwidth set to 768 kHz (±384 kHz visible)
- NB1/NB2 noise blankers receive correct 768k sample rate (already parameterized)

### CTUN DDC Lock + WDSP Shift
- ReceiverManager gains `setDdcFrequencyLocked(bool)` and `forceHardwareFrequency()`
- When CTUN is on: DDC stays locked at pan center, VFO tunes within the 768k window
- WDSP `SetRXAShiftFreq` offsets demodulation so audio follows VFO, not DDC center
- Shift sign is negated: `shiftHz = -(vfoFreq - panCenter)`
- Band jumps (VFO off-screen): temporarily unlock DDC, force-retune to VFO, re-lock
- Pan drag: `forceHardwareFrequency` retunes DDC to new pan center, shift updates

### Adaptive FFT + Zoom
- FFT max size raised from 16384 to 65536
- `bandwidthChangeRequested` signal triggers adaptive FFT sizing in MainWindow
- Formula: `fftSize = nextPowerOf2(sampleRate * 1000 / bandwidth)`, clamped 1024-65536
- Zoom slider (QSlider) added below spectrum as external widget
- QRhiWidget with WA_NativeWindow on macOS does NOT support child widget overlays
  or mouse tracking without button press — zoom slider must be outside QRhiWidget

### TX Silence Stream (disabled)
- Timer-based TX I/Q silence on port 1029 implemented but disabled (`m_txIqTimer`)
- Code present for future enable when TX path is implemented

### Protocol Fix: Alex Enable
- CmdGeneral byte 59 now sends `0x03` (enable Alex0 + Alex1)
- Was missing entirely — Alex BPF board was disabled

---

## Known Bugs / Issues (All Fixed — 2026-04-10)

### ~~Signal Level Drop on Band Change~~ — FIXED (65acb5f)
- **Root cause:** `setCenterFrequency()` triggered `centerChanged` signal which
  fired `forceHardwareFrequency` a second time with the pan center (not VFO),
  creating competing DDC retune commands.
- **Fix:** Added `m_handlingBandJump` re-entrancy guard to suppress `centerChanged`
  handler during band-jump processing in `frequencyChanged` handler.

### ~~WDSP Shift Sign / Dial Accuracy~~ — FIXED (2259ed9)
- **Root cause:** NereusSDR passed `-(freq - center)` to `SetRXAShiftFreq`.
  Thetis source analysis (radio.cs:1417 + console.cs:31357) shows the correct
  sign is `+(freq - center)`.
- **Fix:** Corrected sign in both MainWindow handlers. Also added missing
  `RXANBPSetShiftFrequency` call (Thetis radio.cs:1418) to keep NB passband in sync.

### QRhiWidget Mouse Tracking on macOS — KNOWN LIMITATION
- QRhiWidget with `WA_NativeWindow` does not deliver `mouseMoveEvent` without
  a button press on macOS Metal, even with `setMouseTracking(true)`, `WA_Hover`,
  `event()` override for QHoverEvent, or transparent child widget overlay
- Current workaround: zoom slider is an external QWidget below the QRhiWidget
- **Status:** Qt6 platform limitation. Not a Phase 3E blocker.

### ~~Protocol Gaps vs Thetis pcap~~ — FIXED (143dc82, 001e5ee)
- TX sample rate restored to 192 (from Thetis netInterface.c:1513)
- mic_control bit-field fully documented for Phase 3I-1
- CW defaults remain at zero (correct per Thetis create_rnet:1447-1454;
  non-zero values come from console.cs setup, which is Phase 3I-2 scope)
- Root cause of signal drop was DDC lock re-entrancy, not protocol bytes

### ~~FFT Replan Audio Glitch~~ — FIXED (73b301f)
- **Fix:** `setFftSize()` now defers to `m_pendingFftSize` atomic; `feedIQ()`
  applies at frame boundary. `FFTW_MEASURE` replaced with `FFTW_ESTIMATE` for
  runtime replans to avoid FFTW global mutex contention with WDSP audio thread.

---

## Architecture Decisions

### Why DDC 768k instead of Wideband ADC
- Thetis pcap shows DDC2 at 768 kHz with wideband disabled
- Single data path for both audio and display (simpler)
- Wideband ADC stream is a separate feature for full-band scope (0-61 MHz)
- Wideband documented in `docs/architecture/wideband-adc-brainstorm.md`

### Why QSlider for Zoom Instead of In-Widget Drag
- QRhiWidget + WA_NativeWindow on macOS creates a native Metal NSView
- Native NSView does not participate in Qt's mouse tracking system
- Child QWidget overlays on QRhiWidget don't receive events
- External QSlider is a regular QWidget that works normally

### WDSP Buffer Sizing (Thetis formula)
- `in_size = 64 * sampleRate / 48000` → 1024 at 768 kHz
- `dsp_size = 4096` (fixed)
- `dsp_insize = dsp_size * (in_rate / dsp_rate)` → 65536 at 768k
- `out_size = in_size * (out_rate / in_rate)` → 64 at 768k→48k
- Source: Thetis `cmsetup.c:getbuffsize()` and `channel.c:pre_main_build()`

---

## Files Modified (this session)

| File | Changes |
|------|---------|
| `src/core/P2RadioConnection.h` | TX IQ timer, sequence counter |
| `src/core/P2RadioConnection.cpp` | DDC2 768k, TX silence stream (disabled), Alex enable |
| `src/core/ReceiverManager.h` | DDC lock, forceHardwareFrequency |
| `src/core/ReceiverManager.cpp` | DDC lock implementation |
| `src/core/RxChannel.h` | setShiftFrequency |
| `src/core/RxChannel.cpp` | WDSP SetRXAShiftRun/SetRXAShiftFreq |
| `src/core/FFTEngine.h` | Max FFT size 65536 |
| `src/gui/MainWindow.cpp` | 768k bandwidth, zoom slider, adaptive FFT, CTUN shift wiring |
| `src/gui/SpectrumWidget.h` | Mouse overlay member, eventFilter, kFreqScaleH, kMaxFftBins |
| `src/gui/SpectrumWidget.cpp` | WA_NativeWindow, WA_Hover, event() override, zoom limits |
| `src/models/RadioModel.h` | kWdspOutSize constant |
| `src/models/RadioModel.cpp` | WDSP 768k channel, 64-sample output |
| `docs/architecture/wideband-adc-brainstorm.md` | Wideband ADC future design |
