# Phase 1C: WDSP Investigation

## Status: Complete

## Executive Summary

WDSP v1.29 by Warren Pratt NR0V is the complete DSP engine for OpenHPSDR
client applications. It exposes 162+ exported API functions organized into 50+
functional areas. Thetis declares 256 DllImport bindings to WDSP. In NereusSDR,
WDSP performs ALL signal processing: the radio is an ADC/DAC with network
transport, and the client does demodulation, filtering, AGC, noise reduction,
TX processing, and PureSignal linearization entirely in software.

This document enumerates the complete WDSP API surface, maps each function to
its Thetis UI control, documents integration patterns, and identifies
modernization opportunities for the Qt6/C++20 port.

---

## Part A: Complete WDSP API Surface

### 1. Channel Management (12 functions)

WDSP operates on numbered channels. Each channel is an independent DSP
processing chain (RX or TX).

| Function | Purpose |
|----------|---------|
| `OpenChannel(channel, in_size, dsp_size, input_samplerate, dsp_rate, output_samplerate, type, state, ...)` | Create and configure a channel. `type`: 0=RX, 1=TX. `state`: 0=OFF, 1=ON. |
| `CloseChannel(channel)` | Destroy a channel and free all resources. |
| `SetChannelState(channel, state, dmode)` | Enable/disable a channel. `state`: 0=OFF, 1=ON. `dmode`: drain mode. |
| `SetType(channel, type)` | Change channel type (RX/TX). Rarely used after creation. |
| `SetInputBuffsize(channel, size)` | Set input buffer size (typically 1024 or 2048 samples). |
| `SetDSPBuffsize(channel, size)` | Set internal DSP processing buffer size. |
| `SetInputSamplerate(channel, rate)` | Set input sample rate (e.g. 48000, 96000, 192000, 384000). |
| `SetDSPSamplerate(channel, rate)` | Set internal DSP processing rate. |
| `SetOutputSamplerate(channel, rate)` | Set output sample rate (typically 48000 for audio). |
| `SetAllRates(channel, in_rate, dsp_rate, out_rate)` | Set all three rates atomically. Triggers filter coefficient recalculation. |
| `SetInputRate(channel, rate)` | Alias for SetInputSamplerate. |
| `SetOutputRate(channel, rate)` | Alias for SetOutputSamplerate. |

**Key constraints:**
- Maximum 32 channels (hard-coded in WDSP).
- Each channel allocates approximately 2-3 MB of memory.
- Changing sample rates triggers expensive filter coefficient recalculation.

### 2. Audio/Sample Exchange (2 functions)

These are the hot-path functions called every buffer cycle from the audio
thread.

| Function | Purpose |
|----------|---------|
| `fexchange0(channel, Cin, Cout, error*)` | Exchange interleaved I/Q samples. `Cin`: input buffer (interleaved I,Q pairs). `Cout`: output buffer. |
| `fexchange2(channel, Iin, Qin, Iout, Qout, error*)` | Exchange separate I/Q buffers. `Iin`/`Qin`: input I and Q arrays. `Iout`/`Qout`: output arrays. |

**Usage pattern:** The audio thread calls `fexchange0` or `fexchange2` every
buffer cycle. Input is raw I/Q from the radio; output is demodulated audio (RX)
or modulated I/Q (TX). The `error` pointer receives status codes.

### 3. Demodulation (2 functions)

| Function | Purpose |
|----------|---------|
| `SetRXAMode(channel, mode)` | Set RX demodulation mode. |
| `SetTXAMode(channel, mode)` | Set TX modulation mode. |

**Mode values:**

| Value | Enum | Description |
|-------|------|-------------|
| 0 | LSB | Lower Sideband |
| 1 | USB | Upper Sideband |
| 2 | DSB | Double Sideband |
| 3 | CWL | CW Lower |
| 4 | CWU | CW Upper |
| 5 | FM | Frequency Modulation |
| 6 | AM | Amplitude Modulation |
| 7 | DIGU | Digital Upper |
| 8 | SPEC | Spectrum (passthrough) |
| 9 | DIGL | Digital Lower |
| 10 | SAM | Synchronous AM |
| 11 | DRM | Digital Radio Mondiale |

TX mode adds `AM_LSB` and `AM_USB` variants for asymmetric AM transmission.

**Note:** Mode affects filter frequency interpretation. LSB uses negative
frequencies (e.g. -2800 to -200), USB uses positive (200 to 2800). The UI
must translate between user-facing bandwidth and WDSP's signed frequency
convention.

### 4. Bandpass Filtering (12+ functions)

| Function | Purpose |
|----------|---------|
| `SetRXABandpassFreqs(channel, f_low, f_high)` | Set RX filter passband edges in Hz (signed, relative to carrier). |
| `SetTXABandpassFreqs(channel, f_low, f_high)` | Set TX filter passband edges. |
| `SetRXABandpassNC(channel, nc)` | Set RX filter kernel size (number of coefficients). Higher = sharper. |
| `SetRXABandpassMP(channel, mp)` | Set RX filter minimum phase mode (0=linear, 1=minimum phase). |
| `SetRXABandpassWindow(channel, window)` | Set RX filter window function (0=Blackman-Harris 4-term, etc.). |
| `SetTXABandpassNC(channel, nc)` | Set TX filter kernel size. |
| `SetTXABandpassMP(channel, mp)` | Set TX filter minimum phase mode. |
| `SetTXABandpassWindow(channel, window)` | Set TX filter window function. |

**Filter frequency conventions by mode:**
- SSB (LSB): f_low=-2800, f_high=-200 (typical)
- SSB (USB): f_low=200, f_high=2800
- CW: narrow centered on pitch (e.g. f_low=300, f_high=900 for 600 Hz CW at 600 Hz pitch)
- AM/SAM: symmetric (e.g. f_low=-4000, f_high=4000)
- FM: wider (e.g. f_low=-8000, f_high=8000)
- DIGU/DIGL: varies by digital mode

### 5. AGC -- Automatic Gain Control (16+ functions)

| Function | Purpose |
|----------|---------|
| `SetRXAAGCMode(channel, mode)` | Set AGC mode. 0=OFF, 1=SLOW, 2=MED, 3=FAST, 4=CUSTOM. |
| `SetRXAAGCFixed(channel, fixed_gain)` | Set fixed gain when AGC is OFF (dB). |
| `GetRXAAGCTop(channel, top*)` | Get current AGC maximum gain (top). |
| `SetRXAAGCTop(channel, top)` | Set AGC maximum gain. Primary "AGC threshold" control. |
| `SetRXAAGCAttack(channel, attack)` | Set AGC attack time (ms). |
| `SetRXAAGCDecay(channel, decay)` | Set AGC decay time (ms). |
| `SetRXAAGCHang(channel, hang)` | Set AGC hang time (ms). |
| `SetRXAAGCSlope(channel, slope)` | Set AGC slope (dB). |
| `GetRXAAGCThresh(channel, thresh*, size, rate)` | Get AGC threshold for given buffer size and rate. |
| `SetRXAAGCThresh(channel, thresh, size, rate)` | Set AGC threshold. |
| `GetRXAAGCHangThreshold(channel, hangthresh*)` | Get hang threshold (level below which hang engages). |
| `SetRXAAGCHangThreshold(channel, hangthresh)` | Set hang threshold. |
| `GetRXAAGCHangLevel(channel, hanglevel*)` | Get hang level. |
| `SetRXAAGCHangLevel(channel, hanglevel)` | Set hang level. |
| `SetRXAAGCMaxInputLevel(channel, level)` | Set maximum expected input level. |
| `SetRXAAGCSlewRate(channel, rate)` | Set gain change slew rate. |

**AGC mode presets:**

| Mode | Attack (ms) | Decay (ms) | Hang (ms) |
|------|------------|------------|-----------|
| SLOW | 2 | 500 | 500 |
| MED | 2 | 250 | 100 |
| FAST | 2 | 100 | 0 |
| CUSTOM | user | user | user |

### 6. Noise Blanker NB (ANB) -- Analog Noise Blanker (8+ functions)

External functions for the analog noise blanker, operating before the main DSP
chain.

| Function | Purpose |
|----------|---------|
| `create_anbEXT(id, run, buffsize, samplerate, ...)` | Create ANB instance with parameters. |
| `destroy_anbEXT(id)` | Destroy ANB instance. |
| `flush_anbEXT(id)` | Flush ANB buffers. |
| `xanbEXT(id, in, out)` | Process samples through ANB. |
| `pSetRCVRANBRun(id, run)` | Enable/disable ANB. |
| `pSetRCVRANBBuffsize(id, size)` | Set ANB buffer size. |
| `pSetRCVRANBSamplerate(id, rate)` | Set ANB sample rate. |
| `pSetRCVRANBTau(id, tau)` | Set ANB time constant. |
| `pSetRCVRANBHangtime(id, hangtime)` | Set ANB hang time. |
| `pSetRCVRANBAdvtime(id, advtime)` | Set ANB advance time (lookahead). |
| `pSetRCVRANBBacktau(id, backtau)` | Set ANB back time constant. |
| `pSetRCVRANBThreshold(id, threshold)` | Set ANB detection threshold. |

**Note:** ANB operates on raw I/Q samples BEFORE the main WDSP channel
processing. It uses separate create/destroy lifecycle from channel management.

### 7. Noise Blanker NB2 (NOB) -- Optimal Noise Blanker (8+ functions)

| Function | Purpose |
|----------|---------|
| `create_nobEXT(id, run, mode, buffsize, samplerate, ...)` | Create NOB instance. `mode`: blanking algorithm. |
| `destroy_nobEXT(id)` | Destroy NOB instance. |
| `flush_nobEXT(id)` | Flush NOB buffers. |
| `xnobEXT(id, in, out)` | Process samples through NOB. |
| `pSetRCVRNOBRun(id, run)` | Enable/disable NOB. |
| `pSetRCVRNOBMode(id, mode)` | Set NOB algorithm mode. |
| `pSetRCVRNOBBuffsize(id, size)` | Set NOB buffer size. |
| `pSetRCVRNOBSamplerate(id, rate)` | Set NOB sample rate. |
| `pSetRCVRNOBTau(id, tau)` | Set NOB time constant. |
| `pSetRCVRNOBHangtime(id, hangtime)` | Set NOB hang time. |
| `pSetRCVRNOBAdvtime(id, advtime)` | Set NOB advance time. |
| `pSetRCVRNOBBacktau(id, backtau)` | Set NOB back time constant. |
| `pSetRCVRNOBThreshold(id, threshold)` | Set NOB detection threshold. |

**NB vs NB2:** NB (ANB) uses a simple threshold blanker. NB2 (NOB) uses an
optimal interpolating blanker that estimates the missing signal during blanked
intervals. NB2 produces less distortion but uses more CPU.

### 8. Noise Reduction ANF -- Adaptive Notch Filter (7 functions)

LMS-based adaptive filter that automatically tracks and removes narrowband
interference (heterodynes, carriers).

| Function | Purpose |
|----------|---------|
| `SetRXAANFRun(channel, run)` | Enable/disable ANF. |
| `SetRXAANFVals(channel, taps, delay, gain, leakage)` | Set all ANF parameters at once. |
| `SetRXAANFTaps(channel, taps)` | Set number of filter taps. More taps = more notches, slower convergence. |
| `SetRXAANFDelay(channel, delay)` | Set decorrelation delay. |
| `SetRXAANFGain(channel, gain)` | Set adaptation gain (step size). |
| `SetRXAANFLeakage(channel, leakage)` | Set leakage coefficient (prevents coefficient drift). |
| `SetRXAANFPosition(channel, position)` | Set position in DSP chain (0=pre-AGC, 1=post-AGC). |

### 9. Noise Reduction ANR -- Adaptive Noise Reduction (7 functions)

LMS-based adaptive filter for broadband noise reduction (NR1 in Thetis UI).

| Function | Purpose |
|----------|---------|
| `SetRXAANRRun(channel, run)` | Enable/disable ANR (NR1). |
| `SetRXAANRVals(channel, taps, delay, gain, leakage)` | Set all ANR parameters at once. |
| `SetRXAANRTaps(channel, taps)` | Set number of filter taps. |
| `SetRXAANRDelay(channel, delay)` | Set decorrelation delay. |
| `SetRXAANRGain(channel, gain)` | Set adaptation gain. |
| `SetRXAANRLeakage(channel, leakage)` | Set leakage coefficient. |
| `SetRXAANRPosition(channel, position)` | Set position in DSP chain. |

### 10. Noise Reduction EMNR -- Enhanced Spectral NR (12+ functions)

Spectral subtraction noise reduction (NR2 in Thetis UI). More effective than
LMS for stationary noise but adds more latency.

| Function | Purpose |
|----------|---------|
| `SetRXAEMNRRun(channel, run)` | Enable/disable EMNR (NR2). |
| `SetRXAEMNRPosition(channel, position)` | Set position in DSP chain. |
| `SetRXAEMNRgainMethod(channel, method)` | Set gain calculation method (0=optimal smoothing, 1=Gamma, 2=Lambda). |
| `SetRXAEMNRnpeMethod(channel, method)` | Set noise power estimation method (0=OSMS, 1=MMSE). |
| `SetRXAEMNRaeRun(channel, run)` | Enable/disable artifact elimination post-processing. |
| `SetRXAEMNRpost2Run(channel, run)` | Enable/disable secondary post-filter. |
| `SetRXAEMNRNlevel(channel, level)` | Set noise level estimation. |
| `SetRXAEMNRFactor(channel, factor)` | Set NR depth factor. |
| `SetRXAEMNRRate(channel, rate)` | Set noise estimation update rate. |
| `SetRXAEMNRTaper(channel, taper)` | Set transition taper width. |
| `SetRXAEMNRtrainZetaThresh(channel, thresh)` | Set training zeta threshold. |
| `SetRXAEMNRtrainT2(channel, t2)` | Set training T2 parameter. |

**Constraint:** EMNR (NR2) cannot be used simultaneously with ANF or ANR (NR1).
The UI must enforce mutual exclusivity between NR1+ANF and NR2.

### 11. Noise Reduction RNNR -- RNNoise (4 functions)

Neural network-based noise reduction using the RNNoise library.

| Function | Purpose |
|----------|---------|
| `SetRXARNNRRun(channel, run)` | Enable/disable RNNoise NR. |
| `SetRXARNNRPosition(channel, position)` | Set position in DSP chain. |
| `RNNRloadModel(channel, model_path)` | Load a custom RNNoise model file. |
| `SetRXARNNRUseDefaultGain(channel, use_default)` | Toggle between default and custom gain. |

### 12. Noise Reduction SBNR -- Spectral Bleach (8 functions)

Spectral bleaching noise reduction algorithm.

| Function | Purpose |
|----------|---------|
| `SetRXASBNRRun(channel, run)` | Enable/disable spectral bleach NR. |
| `SetRXASBNRPosition(channel, position)` | Set position in DSP chain. |
| `SetRXASBNRreductionAmount(channel, amount)` | Set noise reduction depth. |
| `SetRXASBNRsmoothingFactor(channel, factor)` | Set spectral smoothing factor. |
| `SetRXASBNRwhiteningFactor(channel, factor)` | Set spectral whitening factor. |
| `SetRXASBNRnoiseRescale(channel, rescale)` | Set noise rescaling factor. |
| `SetRXASBNRpostFilterThreshold(channel, thresh)` | Set post-filter threshold. |
| `SetRXASBNRnoiseScalingType(channel, type)` | Set noise scaling algorithm type. |

### 13. Notched Bandpass -- Dynamic Notch Filter (7 functions)

Manual notch filter database supporting up to 1024 independent notches shared
across channels.

| Function | Purpose |
|----------|---------|
| `RXANBPAddNotch(channel, notch, fcenter, fwidth, active)` | Add a notch at center frequency with width. |
| `RXANBPGetNotch(channel, notch, fcenter*, fwidth*, active*)` | Query notch parameters by index. |
| `RXANBPDeleteNotch(channel, notch)` | Remove a notch by index. |
| `RXANBPEditNotch(channel, notch, fcenter, fwidth, active)` | Modify an existing notch. |
| `RXANBPGetNumNotches(channel, num*)` | Get current notch count. |
| `RXANBPSetTuneFrequency(channel, freq)` | Set the tuning reference frequency for notch offsets. |
| `RXANBPSetNotchesRun(channel, run)` | Enable/disable all notches. |

**Key detail:** Up to 1024 dynamic notches. The notch database is shared across
channels. Notch frequencies are relative to the channel's tune frequency.

### 14. Equalization (8+ functions)

Multi-band parametric and graphic equalizer for both RX and TX paths.

| Function | Purpose |
|----------|---------|
| `SetRXAEQRun(channel, run)` | Enable/disable RX equalizer. |
| `SetRXAEQProfile(channel, nfreqs, F*, G*, Q*)` | Set RX parametric EQ profile. `F`: frequencies, `G`: gains (dB), `Q`: Q factors. |
| `SetRXAGrphEQ(channel, gains*)` | Set RX graphic EQ (array of band gains in dB). |
| `SetRXAGrphEQ10(channel, gains*)` | Set RX 10-band graphic EQ. |
| `SetTXAEQRun(channel, run)` | Enable/disable TX equalizer. |
| `SetTXAEQProfile(channel, nfreqs, F*, G*, Q*)` | Set TX parametric EQ profile. |
| `SetTXAGrphEQ(channel, gains*)` | Set TX graphic EQ. |
| `SetTXAGrphEQ10(channel, gains*)` | Set TX 10-band graphic EQ. |

### 15. TX Compression (14+ functions)

Multi-stage TX audio processing chain: phase rotation, leveler, compressor,
spectral compressor (CFCOMP), and ALC.

**Phase Rotation (anti-clipping):**

| Function | Purpose |
|----------|---------|
| `SetTXAPHROTRun(channel, run)` | Enable/disable phase rotator. |
| `SetTXAPHROTCorner(channel, corner)` | Set corner frequency (Hz). |
| `SetTXAPHROTNstages(channel, nstages)` | Set number of allpass stages. |
| `SetTXAPHROTReverse(channel, reverse)` | Reverse phase rotation direction. |

**Leveler:**

| Function | Purpose |
|----------|---------|
| `SetTXALevelerSt(channel, state)` | Enable/disable leveler. |
| `SetTXALevelerTop(channel, top)` | Set leveler target level (dB). |
| `SetTXALevelerAttack(channel, attack)` | Set leveler attack time (ms). |
| `SetTXALevelerDecay(channel, decay)` | Set leveler decay time (ms). |
| `SetTXALevelerHang(channel, hang)` | Set leveler hang time (ms). |

**Compressor:**

| Function | Purpose |
|----------|---------|
| `SetTXACompressorRun(channel, run)` | Enable/disable compressor. |
| `SetTXACompressorGain(channel, gain)` | Set compression gain (dB). Primary "COMP" control. |

**Spectral Compressor (CFCOMP):**

| Function | Purpose |
|----------|---------|
| `SetTXACFCOMPRun(channel, run)` | Enable/disable spectral compressor. |
| `SetTXACFCOMPprofile(channel, nfreqs, F*, G*, E*)` | Set CFCOMP spectral profile. |
| `SetTXACFCOMPPosition(channel, position)` | Set position in TX chain. |
| `SetTXACFCOMPPrecomp(channel, precomp)` | Set pre-compression gain. |
| `GetTXACFCOMPGainAndMask(channel, gain*, mask*, size)` | Get current gain and mask data (for PureSignal). |

**ALC (Automatic Level Control):**

| Function | Purpose |
|----------|---------|
| `SetTXAALCSt(channel, state)` | Enable/disable ALC. |
| `SetTXAALCAttack(channel, attack)` | Set ALC attack time. |
| `SetTXAALCDecay(channel, decay)` | Set ALC decay time. |
| `SetTXAALCHang(channel, hang)` | Set ALC hang time. |
| `SetTXAALCMaxGain(channel, maxgain)` | Set ALC maximum gain. |

**TX processing order:** Mic input -> Phase Rotation -> Leveler -> Compressor
-> CFCOMP (spectral) -> ALC -> Bandpass Filter -> output.

### 16. PureSignal -- PA Linearization

PureSignal uses a dedicated feedback RX channel to measure the actual PA output
and compute pre-distortion corrections.

| Function | Purpose |
|----------|---------|
| `pscc(channel, size, tx*, rx*)` | Core PureSignal function. Feeds TX reference and RX feedback samples for correction calculation. |

**PureSignal data flow:**
1. TX path produces modulated I/Q -> radio DAC -> PA -> antenna
2. Feedback RX captures PA output via coupler -> ADC -> WDSP feedback channel
3. `pscc()` compares TX reference with RX feedback
4. Correction coefficients computed and applied to subsequent TX buffers
5. CFCOMP spectral shaping profile updated from correction data

**Constraints:**
- Requires a dedicated RX channel for feedback (not available for normal RX)
- Feedback delay must be less than one buffer period (approximately 20ms at 48kHz)
- CPU intensive -- correction calculation runs every TX buffer cycle

### 17. Squelch (6 functions)

Mode-specific squelch implementations.

**AM Squelch:**

| Function | Purpose |
|----------|---------|
| `SetRXAAMSQRun(channel, run)` | Enable/disable AM squelch. |
| `SetRXAAMSQThreshold(channel, threshold)` | Set AM squelch threshold level. |
| `SetRXAAMSQMaxTail(channel, tail)` | Set maximum squelch tail time. |

**FM Squelch:**

| Function | Purpose |
|----------|---------|
| `SetRXAFMSQRun(channel, run)` | Enable/disable FM squelch. |
| `SetRXAFMSQThreshold(channel, threshold)` | Set FM squelch threshold. |

**Software Squelch (general purpose):**

| Function | Purpose |
|----------|---------|
| `SetRXASSQLRun(channel, run)` | Enable/disable software squelch. |
| `SetRXASSQLThreshold(channel, threshold)` | Set squelch threshold. |
| `SetRXASSQLTauMute(channel, tau)` | Set mute time constant. |
| `SetRXASSQLTauUnMute(channel, tau)` | Set unmute time constant. |

### 18. VOX/DEXP -- Voice-Operated Exchange (6+ functions)

| Function | Purpose |
|----------|---------|
| `create_dexp(id, run, size, rate, ...)` | Create DEXP (VOX) instance. |
| `destroy_dexp(id)` | Destroy DEXP instance. |
| `flush_dexp(id)` | Flush DEXP buffers. |
| `xdexp(id, in, out)` | Process samples through VOX detector. |
| `SetDEXPSize(id, size)` | Set DEXP buffer size. |
| `SetDEXPRate(id, rate)` | Set DEXP sample rate. |
| `SendAntiVOXData(id, data, size)` | Send anti-VOX reference data (speaker audio fed back to prevent false triggers). |

### 19. Spectrum/FFT Display (8+ functions)

| Function | Purpose |
|----------|---------|
| `SetRXASpectrum(channel, flag, disp_mode, position, ...)` | Configure spectrum analyzer for RX channel. |
| `RXAGetaSipF(channel, data*, flag)` | Get RX spectrum data (smoothed). |
| `RXAGetaSipF1(channel, data*, flag)` | Get RX spectrum data (alternate). |
| `TXAGetSpecF1(channel, data*, flag)` | Get TX spectrum data. |
| `TXAGetaSipF(channel, data*, flag)` | Get TX spectrum data (smoothed). |
| `TXAGetaSipF1(channel, data*, flag)` | Get TX spectrum data (alternate). |
| `SetupDetectMaxBin(channel, ...)` | Configure maximum bin detection (for signal peak finding). |
| `GetDetectMaxBin(channel, bin*)` | Get the bin index of the maximum signal. |

**Note:** NereusSDR will use its own FFTW3-based spectrum computation for the
main display, but WDSP's built-in spectrum functions are useful for TX
monitoring and signal analysis.

### 20. Audio Panel (8 functions)

Controls audio output routing and gain.

| Function | Purpose |
|----------|---------|
| `SetRXAPanelRun(channel, run)` | Enable/disable RX audio panel. |
| `SetRXAPanelGain1(channel, gain)` | Set RX audio gain (main volume). |
| `SetRXAPanelGain2(channel, gain)` | Set RX audio gain (sub-receiver volume). |
| `SetRXAPanelPan(channel, pan)` | Set stereo pan position (0.0=left, 0.5=center, 1.0=right). |
| `SetRXAPanelBinaural(channel, binaural)` | Enable binaural audio (I to left, Q to right). |
| `SetRXAPanelSelect(channel, select)` | Select audio source within panel. |
| `SetRXAPanelCopy(channel, copy)` | Copy mono to both stereo channels. |
| `SetTXAPanelRun(channel, run)` | Enable/disable TX audio panel. |
| `SetTXAPanelGain1(channel, gain)` | Set TX audio gain (mic level). |

### 21. FM-Specific Processing (8+ functions)

**RX FM:**

| Function | Purpose |
|----------|---------|
| `SetRXAFMDeviation(channel, deviation)` | Set FM deviation (Hz). Typical: 5000 for NBFM. |
| `SetRXAFMLimRun(channel, run)` | Enable/disable FM limiter. |
| `SetRXAFMLimGain(channel, gain)` | Set FM limiter gain. |
| `SetRXAFMAFFilter(channel, run)` | Enable/disable FM audio filter. |

**TX FM:**

| Function | Purpose |
|----------|---------|
| `SetTXAFMDeviation(channel, deviation)` | Set TX FM deviation. |
| `SetTXACTCSSFreq(channel, freq)` | Set CTCSS tone frequency (Hz). |
| `SetTXACTCSSRun(channel, run)` | Enable/disable CTCSS tone. |
| `SetTXAFMAFFilter(channel, run)` | Enable/disable TX FM audio filter. |
| `SetTXAFMEmphPosition(channel, position)` | Set pre-emphasis filter position in chain. |

### 22. CW and Special Filters (18+ functions)

Specialized filter types for CW reception and signal processing.

**BiQuad Filter:**

| Function | Purpose |
|----------|---------|
| `SetRXABiQuadRun(channel, run)` | Enable/disable biquad filter. |
| `SetRXABiQuadFreq(channel, freq)` | Set biquad center frequency. |
| `SetRXABiQuadBandwidth(channel, bw)` | Set biquad bandwidth. |
| `SetRXABiQuadGain(channel, gain)` | Set biquad gain. |

**Double-Pole Filter:**

| Function | Purpose |
|----------|---------|
| `SetRXADoublepoleRun(channel, run)` | Enable/disable double-pole filter. |
| `SetRXADoublepoleFreqs(channel, freq)` | Set double-pole center frequency. |
| `SetRXADoublepoleGain(channel, gain)` | Set double-pole gain. |

**Gaussian Filter:**

| Function | Purpose |
|----------|---------|
| `SetRXAGaussianRun(channel, run)` | Enable/disable Gaussian filter. |
| `SetRXAGaussianFreqs(channel, freq)` | Set Gaussian center frequency. |
| `SetRXAGaussianGain(channel, gain)` | Set Gaussian gain. |
| `SetRXAGaussianNC(channel, nc)` | Set Gaussian filter coefficient count. |

**Matched Filter:**

| Function | Purpose |
|----------|---------|
| `SetRXAMatchedRun(channel, run)` | Enable/disable matched filter. |
| `SetRXAMatchedFreqs(channel, freq)` | Set matched filter frequency. |
| `SetRXAMatchedGain(channel, gain)` | Set matched filter gain. |

**SPcw (Single Peak CW):**

| Function | Purpose |
|----------|---------|
| `SetRXASPCWRun(channel, run)` | Enable/disable SPcw filter. |
| `SetRXASPCWFreq(channel, freq)` | Set SPcw center frequency. |
| `SetRXASPCWBandwidth(channel, bw)` | Set SPcw bandwidth. |
| `SetRXASPCWGain(channel, gain)` | Set SPcw gain. |
| `SetRXASPCWSelection(channel, selection)` | Select SPcw filter variant. |

**Dolly/MPeak (Multi-Peak):**

| Function | Purpose |
|----------|---------|
| `SetRXAmpeakRun(channel, run)` | Enable/disable multi-peak filter. |
| `SetRXAmpeakFilFreq(channel, freq)` | Set peak filter frequency. |
| `SetRXAmpeakFilBw(channel, bw)` | Set peak filter bandwidth. |
| `SetRXAmpeakFilGain(channel, gain)` | Set peak filter gain. |

### 23. Test Signal Generators (60+ functions)

Comprehensive test signal generation for both RX and TX paths.

**RX Pre-Generator (inserts test signal before RX DSP):**

| Mode | Functions |
|------|-----------|
| Tone | `SetRXAPreGenRun`, `SetRXAPreGenMode(0)`, `SetRXAPreGenToneFreq`, `SetRXAPreGenToneMag` |
| Noise | `SetRXAPreGenMode(1)`, `SetRXAPreGenNoiseMag` |
| Sweep | `SetRXAPreGenMode(2)`, `SetRXAPreGenSweepFreq1/Freq2/Rate` |
| Gaussian | `SetRXAPreGenMode(3)`, `SetRXAPreGenGaussianMag` |

**TX Pre-Generator (inserts test signal at TX input):**

| Mode | Functions |
|------|-----------|
| Tone | `SetTXAPreGenRun`, `SetTXAPreGenMode(0)`, `SetTXAPreGenToneFreq/Mag` |
| Noise | `SetTXAPreGenMode(1)`, `SetTXAPreGenNoiseMag` |
| Sweep | `SetTXAPreGenMode(2)`, `SetTXAPreGenSweepFreq1/Freq2/Rate` |
| Sawtooth | `SetTXAPreGenMode(3)`, `SetTXAPreGenSawtoothFreq/Mag` |
| Triangle | `SetTXAPreGenMode(4)`, `SetTXAPreGenTriangleFreq/Mag` |
| Pulse | `SetTXAPreGenMode(5)`, `SetTXAPreGenPulseFreq/Mag/DutyCycle/TransitionTime` |

**TX Post-Generator (inserts test signal after TX DSP):**

| Mode | Functions |
|------|-----------|
| Tone | `SetTXAPostGenRun`, `SetTXAPostGenMode(0)`, `SetTXAPostGenToneFreq/Mag` |
| Noise | `SetTXAPostGenMode(1)`, `SetTXAPostGenNoiseMag` |
| Sweep | `SetTXAPostGenMode(2)`, `SetTXAPostGenSweepFreq1/Freq2/Rate` |
| Two-Tone | `SetTXAPostGenMode(3)`, `SetTXAPostGenTTFreq1/Freq2/Mag1/Mag2` |
| Pulse | `SetTXAPostGenMode(4)`, `SetTXAPostGenPulseFreq/Mag/DutyCycle/TransitionTime` |
| Two-Tone-Pulse | `SetTXAPostGenMode(5)` (combined two-tone + pulse) |

**Usage:** Test generators are essential for radio alignment, filter
verification, IMD testing, and PureSignal calibration without an external
signal source.

### 24. Metering (2 functions, multiple meter types)

| Function | Purpose |
|----------|---------|
| `GetRXAMeter(channel, type)` | Read an RX meter value. Returns double (dBFS or dB). |
| `GetTXAMeter(channel, type)` | Read a TX meter value. Returns double. |

**RX Meter Types:**

| Type | Description |
|------|-------------|
| `S_PK` | Signal peak level (for S-meter peak reading) |
| `S_AV` | Signal average level (for S-meter average reading) |
| `ADC_PK` | ADC peak level (input clipping detection) |
| `ADC_AV` | ADC average level |
| `AGC_GAIN` | Current AGC gain value (dB) |
| `AGC_PK` | AGC output peak |
| `AGC_AV` | AGC output average |

**TX Meter Types:**

| Type | Description |
|------|-------------|
| `MIC_PK` / `MIC_AV` | Microphone input level |
| `EQ_PK` / `EQ_AV` | Post-equalizer level |
| `CFC_PK` / `CFC_AV` | Post-CFCOMP level |
| `CFC_GAIN` | CFCOMP gain reduction |
| `COMP_PK` / `COMP_AV` | Post-compressor level |
| `ALC_PK` / `ALC_AV` | Post-ALC level |
| `ALC_GAIN` | ALC gain reduction |
| `OUT_PK` / `OUT_AV` | Final TX output level |
| `LVLR_PK` / `LVLR_AV` | Leveler output level |
| `LVLR_GAIN` | Leveler gain |

**Reading pattern:** Poll meters from a timer on the GUI thread at 10-30 Hz.
Each call is lightweight (reads a cached value, no computation).

### 25. System Functions (5+ functions)

| Function | Purpose |
|----------|---------|
| `WDSPwisdom(directory)` | Generate or load FFTW wisdom file from specified directory. Pre-computes optimal FFT plans. |
| `GetWDSPVersion()` | Returns WDSP version string. |
| `save_impulse_cache(filename)` | Save computed filter impulse responses to disk cache. |
| `read_impulse_cache(filename)` | Load filter impulse responses from disk cache. |
| `use_impulse_cache(flag)` | Enable/disable impulse response caching. |
| `init_impulse_cache()` | Initialize the impulse cache system. |
| `destroy_impulse_cache()` | Free impulse cache memory. |

**FFTW wisdom:** First run without wisdom is slow (FFTW measures optimal FFT
plans). Saving wisdom to `~/.config/NereusSDR/fftw_wisdom` speeds up subsequent
launches significantly. Wisdom generation can take 30-60 seconds on first run.

---

## Part B: Thetis Integration Patterns

### Channel Architecture

Thetis maps hardware receivers to WDSP channels using a
`channel_id = id(thread, subrx)` scheme:

| thread | subrx | Channel ID | Purpose |
|--------|-------|------------|---------|
| 0 | 0 | 0 | RX1 main receiver |
| 0 | 1 | 1 | RX1 sub-receiver |
| 1 | 0 | 2 | RX2 main receiver |
| 1 | 1 | 3 | RX2 sub-receiver |
| 2 | 0 | 4 | TX channel |

Each receiver gets its own independent WDSP channel with its own mode, filter,
AGC, NR, NB, and all other DSP settings. The TX channel is shared across all
transmit operations regardless of which VFO is active.

### Initialization Sequence

The full WDSP initialization in Thetis follows this order:

1. **System initialization:**
   - `WDSPwisdom(wisdom_directory)` -- load or generate FFTW wisdom
   - `init_impulse_cache()` -- initialize filter cache
   - `read_impulse_cache(cache_file)` -- load cached filter coefficients

2. **Per-receiver channel setup:**
   - `OpenChannel(ch, in_size, dsp_size, in_rate, dsp_rate, out_rate, 0, 0)` -- create RX channel (type=0, state=OFF)
   - `create_anbEXT(ch, ...)` -- create NB1 instance for this receiver
   - `create_nobEXT(ch, ...)` -- create NB2 instance for this receiver

3. **Hardware receiver enable:**
   - `NetworkIO.EnableRx(fwid, 1)` -- tell radio to start sending I/Q for this receiver
   - `cmaster.SetXcmInrate(stid, 48000)` -- set cross-mixer input rate

4. **DSP configuration:**
   - `SetRXAMode(ch, mode)` -- set initial demodulation mode
   - `SetRXABandpassFreqs(ch, f_low, f_high)` -- set initial filter
   - `SetRXAAGCMode(ch, agc_mode)` -- set AGC mode
   - (apply all saved DSP settings: NR, NB, ANF, EQ, squelch, etc.)

5. **Channel activation:**
   - `SetChannelState(ch, 1, 0)` -- ON, no drain

6. **TX channel setup (similar but type=1):**
   - `OpenChannel(tx_ch, in_size, dsp_size, in_rate, dsp_rate, out_rate, 1, 0)` -- create TX channel
   - Configure compressor, EQ, ALC, leveler, phase rotation
   - `create_dexp(tx_ch, ...)` -- create VOX instance

### Teardown Sequence

1. `SetChannelState(ch, 0, 1)` -- OFF with drain (allows buffers to flush)
2. `destroy_anbEXT(ch)` / `destroy_nobEXT(ch)` -- destroy NB instances
3. `CloseChannel(ch)` -- free channel resources
4. `save_impulse_cache(cache_file)` -- save filter cache for next session
5. `destroy_impulse_cache()` -- free cache memory

### Per-Receiver DSP State (Independent)

Each `RadioDSPRX` instance in Thetis owns one WDSP channel and maintains
independent state for all DSP parameters:

| Parameter Group | State Per Receiver | Real-Time Update |
|----------------|-------------------|-----------------|
| Mode (LSB/USB/AM/FM/CW/...) | Yes | Yes -- `SetRXAMode` |
| Filter (low/high) | Yes | Yes -- `SetRXABandpassFreqs` |
| AGC (mode, thresholds, timing) | Yes | Yes -- `SetRXAAGCMode` etc. |
| NB1 (threshold, timing) | Yes | Yes -- `pSetRCVRANBThreshold` etc. |
| NB2 (threshold, timing) | Yes | Yes -- `pSetRCVRNOBThreshold` etc. |
| NR1/ANR (taps, delay, gain) | Yes | Yes -- `SetRXAANRRun` etc. |
| NR2/EMNR (method, depth) | Yes | Yes -- `SetRXAEMNRRun` etc. |
| ANF (taps, delay, gain) | Yes | Yes -- `SetRXAANFRun` etc. |
| Equalizer (band gains) | Yes | Yes -- `SetRXAGrphEQ10` |
| Squelch (threshold, timing) | Yes | Yes -- `SetRXAAMSQThreshold` etc. |
| Audio panel (gain, pan) | Yes | Yes -- `SetRXAPanelGain1` etc. |
| Notch filters | Shared database | Yes -- `RXANBPAddNotch` etc. |

**Key design point:** All WDSP parameter changes take effect immediately on
the next buffer cycle. No channel restart is required for parameter updates
(only sample rate changes trigger expensive recalculation).

### PureSignal Integration Pattern

```
TX Audio -> WdspEngine TX Channel -> I/Q output -> Radio DAC -> PA -> Antenna
                                                                  |
                                                           Coupler tap
                                                                  |
                                              Radio ADC -> I/Q feedback
                                                                  |
                                              WdspEngine Feedback RX Channel
                                                                  |
                                              pscc(ch, size, tx_ref, rx_fb)
                                                                  |
                                              Correction coefficients
                                                                  |
                                              Applied to next TX buffer
```

**Timing constraint:** The feedback path must have less than one buffer period
of latency (approximately 21ms at 48kHz with 1024-sample buffers). Higher
sample rates allow shorter buffers and faster correction convergence.

### Thetis DllImport Organization

Thetis organizes its 256 WDSP DllImport declarations across several files:

| Thetis File | WDSP Function Group | Count (approx) |
|-------------|-------------------|-----------------|
| `RadioDSP.cs` | Channel management, mode, filter, AGC, NR, NB, ANF | ~80 |
| `RadioDSPRX.cs` | Per-receiver RX DSP functions | ~60 |
| `RadioDSPTX.cs` | Per-TX DSP functions (compression, EQ, FM) | ~50 |
| `console.cs` | System functions, meters, PureSignal | ~30 |
| `cmaster.cs` | Sample exchange, channel routing, NB external | ~20 |
| `display.cs` | Spectrum/FFT functions | ~10 |
| `setup.cs` | Parameter configuration from setup dialog | ~6 |

---

## Part C: Feature Matrix

Complete mapping of every DSP feature from WDSP functions through Thetis UI
controls to NereusSDR implementation notes.

| # | DSP Feature | WDSP Functions | Thetis UI Control | Notes |
|---|-------------|---------------|-------------------|-------|
| 1 | **Demodulation** | `SetRXAMode` | Mode selector buttons (LSB/USB/DSB/CWL/CWU/FM/AM/DIGU/SPEC/DIGL/SAM/DRM) | Mode determines filter freq interpretation. SAM has carrier tracking sub-modes. |
| 2 | **TX Modulation** | `SetTXAMode` | Follows RX mode. TX adds AM_LSB/AM_USB. | TX mode set when MOX/PTT activates. |
| 3 | **Bandpass Filter** | `SetRXABandpassFreqs`, `SetRXABandpassNC/MP/Window` | Filter width buttons (Var1/Var2), filter shift slider | NC (kernel size) affects sharpness vs CPU. MP (minimum phase) reduces latency. |
| 4 | **TX Filter** | `SetTXABandpassFreqs`, `SetTXABandpassNC/MP/Window` | TX filter low/high in setup dialog | Tighter TX filter reduces transmitted bandwidth. |
| 5 | **AGC Mode** | `SetRXAAGCMode` | AGC dropdown (Off/Slow/Med/Fast/Custom) | Per-receiver. Custom enables attack/decay/hang sliders. |
| 6 | **AGC Threshold** | `SetRXAAGCTop`, `SetRXAAGCFixed` | AGC-T slider, Fixed gain spinner | AGC-T is the most-used AGC control. Fixed used when AGC=Off. |
| 7 | **AGC Timing** | `SetRXAAGCAttack/Decay/Hang/Slope` | AGC setup dialog: attack/decay/hang sliders | Only editable in Custom mode. |
| 8 | **AGC Hang** | `Get/SetRXAAGCHangThreshold`, `Get/SetRXAAGCHangLevel` | Hang threshold slider in AGC setup | Controls at what level hang behavior engages. |
| 9 | **NB1** | `pSetRCVRANBRun/Threshold` | NB button, NB threshold slider | Analog blanker. Good for pulse noise (ignition, etc). |
| 10 | **NB2** | `pSetRCVRNOBRun/Threshold/Mode` | NB2 button, NB2 threshold slider | Optimal blanker. Better for complex noise patterns. |
| 11 | **NR1 (ANR)** | `SetRXAANRRun/Taps/Delay/Gain/Leakage` | NR button | LMS adaptive NR. Fast convergence, moderate reduction. |
| 12 | **NR2 (EMNR)** | `SetRXAEMNRRun/gainMethod/npeMethod` | NR2 button | Spectral NR. Superior to NR1 but more latency. Cannot combine with ANF/NR1. |
| 13 | **RNNR** | `SetRXARNNRRun`, `RNNRloadModel` | NR dropdown (RNN option) | Neural network NR. Requires trained model file. |
| 14 | **SBNR** | `SetRXASBNRRun/reductionAmount/smoothingFactor` | NR dropdown (Spectral Bleach option) | Spectral bleach NR algorithm. |
| 15 | **ANF** | `SetRXAANFRun/Taps/Delay/Gain/Leakage` | ANF button | Adaptive notch. Automatically removes carriers/heterodynes. |
| 16 | **Manual Notch** | `RXANBPAddNotch/DeleteNotch/EditNotch` | Right-click spectrum to add/edit/delete notch | Up to 1024 notches. Displayed as markers on spectrum. |
| 17 | **RX EQ** | `SetRXAEQRun`, `SetRXAGrphEQ10` | RX EQ button, 10-band graphic EQ sliders | Per-receiver equalization. |
| 18 | **TX EQ** | `SetTXAEQRun`, `SetTXAGrphEQ10` | TX EQ button, 10-band graphic EQ sliders | TX audio equalization. |
| 19 | **Compressor** | `SetTXACompressorRun/Gain` | COMP button, COMP slider (0-20 dB) | Primary TX audio compression. Slider controls gain. |
| 20 | **CFCOMP** | `SetTXACFCOMPRun/profile` | CFCOMP button in TX setup | Spectral compressor for flat TX spectrum. Used with PureSignal. |
| 21 | **Phase Rotation** | `SetTXAPHROTRun/Corner/Nstages` | Phase Rot button in TX setup | Reduces peak-to-average ratio. Improves TX efficiency. |
| 22 | **Leveler** | `SetTXALevelerSt/Top/Attack/Decay` | Leveler button, level slider in TX setup | Smooths level variations before compressor. |
| 23 | **ALC** | `SetTXAALCSt/Attack/Decay/Hang` | ALC always active (setup timing in TX dialog) | Prevents TX overdrive. Final gain limiter. |
| 24 | **PureSignal** | `pscc`, CFCOMP functions | PS button, PS info display, cal button | PA linearization. Requires feedback RX channel. |
| 25 | **AM Squelch** | `SetRXAAMSQRun/Threshold/MaxTail` | Squelch button + slider (AM/SAM modes) | Threshold based on AM signal level. |
| 26 | **FM Squelch** | `SetRXAFMSQRun/Threshold` | Squelch button + slider (FM mode) | Threshold based on FM noise level. |
| 27 | **Software Squelch** | `SetRXASSQLRun/Threshold/TauMute/TauUnMute` | Squelch button + slider (SSB/CW modes) | General-purpose squelch for non-AM/FM modes. |
| 28 | **VOX** | `create_dexp`, `xdexp`, DEXP functions | VOX button, VOX gain/delay/anti-VOX sliders | Voice-operated TX. Anti-VOX prevents speaker audio from triggering. |
| 29 | **FM Deviation** | `SetRXAFMDeviation`, `SetTXAFMDeviation` | FM deviation setting in setup | 5000 Hz typical for NBFM. |
| 30 | **CTCSS** | `SetTXACTCSSFreq/Run` | CTCSS tone selector in FM setup | Sub-audible tone for repeater access. |
| 31 | **RX Audio** | `SetRXAPanelGain1/Gain2/Pan/Binaural/Copy` | Volume slider, pan slider, binaural checkbox | Per-receiver volume and stereo control. |
| 32 | **TX Audio** | `SetTXAPanelGain1` | Mic gain slider | TX microphone level. |
| 33 | **CW Filters** | `SetRXASPCWRun/Freq/Bandwidth`, BiQuad, Gaussian, Matched | CW filter selection in DSP setup | Multiple CW filter shapes for different preferences. |
| 34 | **S-Meter** | `GetRXAMeter(S_PK, S_AV)` | S-meter display (bar + numeric) | Polled at 20-30 Hz. Peak and average readings. |
| 35 | **TX Meters** | `GetTXAMeter(MIC/EQ/COMP/ALC/OUT)` | TX meter strip (mic, comp, ALC, output) | Shows signal level at each TX processing stage. |
| 36 | **AGC Gain Meter** | `GetRXAMeter(AGC_GAIN)` | AGC gain reduction indicator | Shows how much gain AGC is applying. |
| 37 | **ADC Overload** | `GetRXAMeter(ADC_PK)` | ADC clip indicator (red LED) | Warns when ADC is clipping. |
| 38 | **RX Spectrum** | `RXAGetaSipF/aSipF1` | Spectrum/waterfall display (secondary) | Primary display uses FFTW3 directly; WDSP spectrum for monitoring. |
| 39 | **TX Spectrum** | `TXAGetSpecF1/aSipF/aSipF1` | TX spectrum display during transmit | Shows TX output spectrum in real-time. |
| 40 | **Test Generators** | PreGen/PostGen functions (60+) | Test tone controls in setup/diagnostics | Tone, noise, sweep, two-tone, pulse generators. |
| 41 | **FFTW Wisdom** | `WDSPwisdom` | First-run optimization dialog | 30-60 sec on first run, instant on subsequent launches. |
| 42 | **Filter Cache** | `save/read/use_impulse_cache` | Transparent (automatic) | Speeds up filter changes by caching computed coefficients. |

---

## Part D: Modernization for Qt6/C++20

### Thread Safety Improvements

WDSP v1.29 uses Windows `CRITICAL_SECTION` internally. For NereusSDR's
cross-platform build, the WDSP wrapper must handle thread safety at the API
boundary.

**Current (WDSP internal):**
```c
EnterCriticalSection(&ch[channel].csDSP);
// process
LeaveCriticalSection(&ch[channel].csDSP);
```

**NereusSDR wrapper approach:**
```cpp
// Use std::shared_mutex for reader/writer pattern
// Audio thread is the exclusive writer (fexchange)
// GUI thread is a frequent reader (meters, spectrum)
mutable std::shared_mutex m_channelMutex;

// Parameter updates from GUI thread
void Channel::setMode(DSPMode mode) {
    std::unique_lock lock(m_channelMutex);
    WDSP::SetRXAMode(m_channelId, static_cast<int>(mode));
    emit modeChanged(mode);
}

// Meter reading from GUI timer (shared/read lock)
double Channel::getMeter(MeterType type) const {
    // No lock needed -- WDSP meter reads are atomic
    return WDSP::GetRXAMeter(m_channelId, static_cast<int>(type));
}
```

**Atomic parameters for cross-thread DSP:** Following the project guideline,
frequently-read parameters should use `std::atomic` so the audio thread never
blocks on a mutex:

```cpp
std::atomic<int> m_agcMode{static_cast<int>(AGCMode::MED)};
std::atomic<double> m_agcTop{80.0};
std::atomic<bool> m_nrEnabled{false};
```

### Signal/Slot Integration

Create QObject-based channel wrappers that emit signals on parameter changes
for automatic UI updates:

```cpp
namespace nereus::dsp {

enum class DSPMode : int {
    LSB = 0, USB = 1, DSB = 2, CWL = 3, CWU = 4,
    FM = 5, AM = 6, DIGU = 7, SPEC = 8, DIGL = 9,
    SAM = 10, DRM = 11
};

enum class AGCMode : int {
    OFF = 0, SLOW = 1, MED = 2, FAST = 3, CUSTOM = 4
};

enum class MeterType : int {
    S_PK = 0, S_AV = 1, ADC_PK = 2, ADC_AV = 3,
    AGC_GAIN = 4, AGC_PK = 5, AGC_AV = 6
};

class Channel : public QObject {
    Q_OBJECT

    int m_channelId;
    mutable std::shared_mutex m_stateMutex;

    // Atomic parameters for lock-free audio thread access
    std::atomic<int> m_mode{static_cast<int>(DSPMode::USB)};
    std::atomic<int> m_agcMode{static_cast<int>(AGCMode::MED)};
    std::atomic<bool> m_nrEnabled{false};
    std::atomic<bool> m_nbEnabled{false};
    std::atomic<bool> m_anfEnabled{false};

public:
    explicit Channel(int channelId, QObject* parent = nullptr);
    ~Channel();

    // Mode
    void setMode(DSPMode mode);
    DSPMode mode() const { return static_cast<DSPMode>(m_mode.load()); }

    // Filter
    void setBandpassFreqs(double fLow, double fHigh);

    // AGC
    void setAgcMode(AGCMode mode);
    void setAgcTop(double top);
    AGCMode agcMode() const { return static_cast<AGCMode>(m_agcMode.load()); }

    // Noise reduction
    void setNrEnabled(bool enabled);
    void setNr2Enabled(bool enabled);
    void setNbEnabled(bool enabled);
    void setNb2Enabled(bool enabled);
    void setAnfEnabled(bool enabled);

    // Meters (lock-free, called from GUI timer)
    double getMeter(MeterType type) const;

    // Audio exchange (called from audio thread)
    void exchange(float* inI, float* inQ, float* outI, float* outQ, int* error);

signals:
    void modeChanged(DSPMode mode);
    void filterUpdated(double fLow, double fHigh);
    void agcModeChanged(AGCMode mode);
    void agcTopChanged(double top);
    void nrEnabledChanged(bool enabled);
    void nr2EnabledChanged(bool enabled);
    void nbEnabledChanged(bool enabled);
    void nb2EnabledChanged(bool enabled);
    void anfEnabledChanged(bool enabled);
    void meterUpdated(MeterType type, double value);
};

} // namespace nereus::dsp
```

**Meter reading via QTimer:**

```cpp
// In MainWindow or a dedicated MeterController
m_meterTimer = new QTimer(this);
m_meterTimer->setInterval(33); // ~30 Hz
connect(m_meterTimer, &QTimer::timeout, this, [this]() {
    for (auto* channel : m_rxChannels) {
        double sPk = channel->getMeter(MeterType::S_PK);
        double sAv = channel->getMeter(MeterType::S_AV);
        double agcGain = channel->getMeter(MeterType::AGC_GAIN);
        // Update UI meters
        emit meterDataReady(channel->channelId(), sPk, sAv, agcGain);
    }
});
m_meterTimer->start();
```

### WDSP Quirks and Constraints

These are the known behavioral constraints that NereusSDR must account for:

1. **Maximum 32 channels (hard-coded).** Sufficient for typical use (4 RX + 1 TX = 5 channels) but limits exotic configurations.

2. **Memory: approximately 2-3 MB per channel.** With 5 active channels, WDSP uses about 10-15 MB total. Negligible on modern systems.

3. **Mode-dependent filter frequencies.** LSB uses negative frequencies (-2800 to -200), USB uses positive (200 to 2800). The UI must translate between user-friendly bandwidth and WDSP's signed convention. Getting this wrong produces silence or distortion.

4. **ANF/ANR and EMNR are mutually exclusive.** Cannot enable NR1 + NR2 simultaneously. The UI must enforce this constraint and disable conflicting options.

5. **Sample rate changes recalculate filter coefficients.** This is expensive (can take 10-50ms depending on filter kernel size). Avoid changing sample rates during active reception. Use `SetAllRates()` to change all rates atomically rather than calling individual setters.

6. **Buffer alignment critical for SIMD.** WDSP internally uses FFTW which benefits from aligned memory. Input/output buffers should be 32-byte aligned for optimal SIMD performance.

7. **Notch database shared across channels.** Up to 1024 notches total. Adding/removing notches affects all channels referencing the same database. Notch positions are frequency-relative and must be updated when the VFO tunes.

8. **PureSignal feedback delay constraint.** The feedback RX path must have less than approximately 20ms of latency (one buffer at 48kHz/1024 samples). Higher sample rates reduce this constraint proportionally.

9. **NB1/NB2 have separate lifecycle from channels.** They use `create_anbEXT`/`create_nobEXT` rather than being created with the channel. They must be explicitly created and destroyed alongside the channel.

10. **VOX/DEXP has separate lifecycle.** Similar to NB, VOX uses `create_dexp`/`destroy_dexp` rather than being part of the channel.

11. **FFTW wisdom must be generated per platform.** Wisdom files are CPU-specific and not portable between machines. First-run generation takes 30-60 seconds.

12. **Impulse cache is format-sensitive.** Cache files become invalid if WDSP version changes or filter parameters change significantly. Implement a version check on cache load.

### Recommended C++20 Wrapper Architecture

```
nereus::dsp::Engine (singleton)
├── init() -- WDSPwisdom, init_impulse_cache
├── shutdown() -- save_impulse_cache, destroy channels
├── createChannel(type, rates) -> Channel*
├── destroyChannel(Channel*)
│
├── nereus::dsp::Channel (per-receiver or TX)
│   ├── exchange() -- hot path (audio thread)
│   ├── setMode/setFilter/setAgc/...  -- parameter updates (main thread)
│   ├── getMeter() -- meter reading (GUI timer)
│   └── signals: modeChanged, filterUpdated, meterUpdated, ...
│
├── nereus::dsp::NoiseBlanker (per-receiver, wraps ANB/NOB)
│   ├── process(in, out) -- called before Channel::exchange
│   ├── setEnabled/setThreshold/...
│   └── signals: enabledChanged, thresholdChanged, ...
│
├── nereus::dsp::VoxDetector (TX only, wraps DEXP)
│   ├── process(in, out)
│   ├── setEnabled/setThreshold/...
│   └── signals: triggered, released, ...
│
└── nereus::dsp::PureSignal (TX + feedback RX)
    ├── process(txRef, rxFeedback)
    ├── setEnabled/calibrate/...
    └── signals: calibrationComplete, correctionUpdated, ...
```

### Performance Priorities

Listed in order of impact on real-time audio performance:

1. **Buffer alignment for SIMD.** Allocate all I/Q buffers on 32-byte boundaries using `std::aligned_alloc` or platform-specific aligned allocators. FFTW and WDSP both benefit from aligned memory for AVX/AVX2 operations.

   ```cpp
   // Use aligned allocation for audio buffers
   constexpr size_t kBufferAlignment = 32; // AVX2
   float* buffer = static_cast<float*>(
       std::aligned_alloc(kBufferAlignment, bufferSize * sizeof(float)));
   ```

2. **Meter reading batching.** Read all 7 RXA meter types in a single batch from the GUI timer rather than individual calls scattered across widgets. Cache meter values for 33ms (one timer period) to avoid redundant WDSP calls.

3. **Filter coefficient caching.** Use `save_impulse_cache` / `read_impulse_cache` to persist computed filter kernels across sessions. Filter computation is the most expensive non-realtime WDSP operation. Save cache on clean shutdown; load on startup.

4. **FFTW wisdom persistence.** Save wisdom to `~/.config/NereusSDR/fftw_wisdom` on first run. Check for wisdom file on startup and skip generation if present. Wisdom is CPU-architecture-specific.

5. **Notch database optimization.** For NereusSDR's C++ wrapper, maintain a `std::vector<NotchInfo>` sorted by frequency with binary search for lookup. WDSP's internal notch array is linear; the wrapper can provide O(log n) access for the UI overlay rendering.

6. **Avoid sample rate changes during reception.** If the user changes sample rate, briefly disable the channel (`SetChannelState(ch, 0, 1)` with drain), apply rate change, then re-enable. This prevents audio glitches during the expensive filter recalculation.

7. **Lock-free parameter path.** Use `std::atomic` for DSP parameters that the audio thread reads frequently (mode, NR enabled, AGC mode). The main thread writes; the audio thread reads. No mutex in the audio callback, per project guidelines.

---

## Appendix: WDSP Function Count Summary

| Functional Area | Function Count | Hot Path | Per-Receiver |
|----------------|---------------|----------|-------------|
| Channel Management | 12 | No | N/A |
| Audio Exchange | 2 | **Yes** | Yes |
| Demodulation | 2 | No | Yes |
| Bandpass Filtering | 12 | No | Yes |
| AGC | 16 | No | Yes |
| Noise Blanker NB1 (ANB) | 12 | **Yes** | Yes |
| Noise Blanker NB2 (NOB) | 13 | **Yes** | Yes |
| ANF (Adaptive Notch) | 7 | No | Yes |
| ANR (Noise Reduction) | 7 | No | Yes |
| EMNR (Spectral NR) | 12 | No | Yes |
| RNNR (RNNoise) | 4 | No | Yes |
| SBNR (Spectral Bleach) | 8 | No | Yes |
| Notched Bandpass | 7 | No | Shared |
| Equalization | 8 | No | Yes |
| TX Compression | 14 | No | TX only |
| PureSignal | 1+ | **Yes** | TX only |
| Squelch | 8 | No | Yes |
| VOX/DEXP | 6 | **Yes** | TX only |
| Spectrum/FFT | 8 | No | Yes |
| Audio Panel | 9 | No | Yes |
| FM Processing | 9 | No | Yes |
| CW/Special Filters | 18 | No | Yes |
| Test Generators | 60+ | No | Yes |
| Metering | 2 | No | Yes |
| System | 7 | No | N/A |
| **Total** | **~256** | | |

---

## Appendix: NereusSDR Implementation Checklist

Priority order for implementing WDSP integration in NereusSDR:

- [ ] **P0 (MVP):** Channel management, audio exchange, demodulation, bandpass filtering, AGC, audio panel, metering, system/wisdom
- [ ] **P1 (Core):** NB1, NB2, NR1 (ANR), NR2 (EMNR), ANF, manual notch, squelch, RX/TX EQ
- [ ] **P2 (TX):** TX compression chain (compressor, leveler, ALC, phase rotation), VOX, FM TX (deviation, CTCSS), TX metering
- [ ] **P3 (Advanced):** PureSignal, CFCOMP, RNNR, SBNR, CW special filters
- [ ] **P4 (Polish):** Test generators, spectrum via WDSP, impulse caching, filter optimization
