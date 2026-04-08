# Phase 1C: WDSP Investigation

## Status: Not Started

## Objectives

Comprehensive audit of WDSP usage in Thetis to inform the NereusSDR WDSP integration.

- Enumerate every WDSP function called by Thetis (complete API surface)
- Document what each WDSP call does (feature mapping)
- Trace how each UI control maps to a specific WDSP parameter (UI <-> WDSP data flow)
- Understand per-receiver DSP state management
- Document WDSP initialization and teardown sequences
- Identify modernization opportunities for Qt6 signal/slot architecture

## Deliverables

- [ ] Complete WDSP API surface enumeration
- [ ] Feature mapping (WDSP function -> DSP capability)
- [ ] UI control -> WDSP parameter mapping matrix
- [ ] Per-receiver DSP state documentation
- [ ] Initialization/teardown sequence documentation
- [ ] Modernization opportunities report

## WDSP Functional Areas

| Area | Key Functions | Thetis UI Controls |
|------|--------------|-------------------|
| Demodulation | SetRXAMode | Mode selector |
| Filtering | SetRXABandpassFreqs | Filter width/shift |
| AGC | SetRXAAGCMode, SetRXAAGCTop | AGC mode, threshold |
| Noise Blanker | SetRXAANBRun, SetRXANOBRun | NB/NB2 buttons |
| Noise Reduction | SetRXAANRRun, SetRXAEMNRRun | NR/NR2 buttons |
| Auto-Notch | SetRXAANFRun | ANF button |
| Equalization | SetRXAEQRun, SetRXAGrphEQ | EQ sliders |
| TX Compression | SetTXACompressorRun | COMP button/slider |
| CESSB | SetTXACESSBRun | CESSB button |
| PureSignal | SetPSControl | PureSignal button |
| VOX/DEXP | SetTXAVoxRun | VOX button/threshold |
| Squelch | SetRXAAMSQRun | Squelch button/level |
