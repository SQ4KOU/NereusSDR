# DSP Pipeline Architecture

**Status:** Phase 2E — Design pending

## Overview

NereusSDR uses WDSP (by Warren Pratt NR0V) for all signal processing.
Unlike AetherSDR where the radio handles DSP, NereusSDR performs all
DSP client-side.

## WDSP Feature Set

- Demodulation: AM, SAM, FM, USB, LSB, CW, DIGU, DIGL, DRM, SPEC
- AGC: Off, Long, Slow, Medium, Fast, Custom
- Noise blanker: NB and NB2
- Noise reduction: NR (LMS) and NR2 (spectral)
- Auto-notch filter: ANF
- Bandpass filtering: variable width per mode
- Equalization: RX and TX multi-band EQ
- TX processing: compression, CESSB, VOX/DEXP
- PureSignal: PA linearization
- Spectrum/FFT computation

## Architecture Goals

- One WDSP channel per receiver (independent DSP state)
- Clean C++20 wrapper (`WdspEngine`) around the WDSP C API
- All DSP parameters exposed as Qt properties with signal/slot notification
- 100% feature parity with Thetis WDSP usage
