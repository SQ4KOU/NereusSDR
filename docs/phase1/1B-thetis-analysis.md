# Phase 1B: Thetis Architecture Analysis

## Status: Not Started

## Objectives

Analyze Thetis (C# / WinForms) to understand the full feature set that must be ported.

- Map the class hierarchy (Radio, Receiver, Slice, Console, Display classes)
- Document receiver lifecycle (instantiation, binding to hardware, destruction)
- Trace state synchronization (VFO, mode, filter, audio routing between UI and radio)
- Identify where the two-panadapter limitation is enforced and why
- Document multi-slice management
- Parse the legacy skin format (ZIP structure from ThetisSkins repo)

## Deliverables

- [ ] Class hierarchy map
- [ ] Receiver lifecycle documentation
- [ ] State synchronization trace (UI <-> radio data flow)
- [ ] Two-panadapter limitation analysis
- [ ] Multi-slice management documentation
- [ ] Legacy skin format specification

## Key Files to Examine

- `Source/Console/console.cs` — Main application class
- `Source/Console/display.cs` — Spectrum/waterfall rendering
- `Source/Console/radio.cs` — Radio abstraction
- `Source/Console/setup.cs` — Settings/configuration
- `Source/Console/DSP/` — DSP integration
- `Source/Console/Skins/` — Skin loading
