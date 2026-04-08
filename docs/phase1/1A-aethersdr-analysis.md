# Phase 1A: AetherSDR Architecture Analysis

## Status: Not Started

## Objectives

Analyze AetherSDR's architecture to identify patterns to reuse in NereusSDR.

- Document the radio abstraction layer (RadioConnection, RadioDiscovery, SmartSDR protocol)
- Map state management patterns (RadioModel, sub-models, AppSettings)
- Trace the audio pipeline (VITA-49 -> AudioEngine -> speakers)
- Understand spectrum/waterfall rendering (QRhi, FFT bins, WF tiles)
- Document panadapter composition and multi-slice architecture
- Identify which patterns to reuse vs. which to change for OpenHPSDR

## Deliverables

- [ ] Radio abstraction analysis
- [ ] State management map (RadioModel ownership, signal flow)
- [ ] Audio pipeline trace (data flow from UDP to speakers)
- [ ] Spectrum/waterfall rendering analysis (QRhi pipeline)
- [ ] Multi-panadapter architecture (PanadapterStack, wirePanadapter)
- [ ] Reuse/change decision matrix

## Key Files to Examine

- `src/core/RadioConnection.h/.cpp` — TCP 4992 protocol handling
- `src/core/PanadapterStream.h/.cpp` — VITA-49 UDP parsing
- `src/core/AudioEngine.h/.cpp` — Audio pipeline
- `src/models/RadioModel.h/.cpp` — Central state model
- `src/gui/SpectrumWidget.h/.cpp` — GPU spectrum rendering
- `src/gui/PanadapterStack.h/.cpp` — Pan layout management
- `CLAUDE.md` — Full architecture documentation
