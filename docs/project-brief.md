# NereusSDR — Project Brief

## Overview

NereusSDR is an independent cross-platform SDR client deeply informed by the workflow, feature set, and operating style of Thetis, reimagined with a new GUI, a modernized architecture, and native support for macOS, Linux, and Windows.

## Supported Radio Hardware

- Apache Labs ANAN line (ANAN-G2 / Saturn, ANAN-7000DLE, ANAN-8000DLE, ANAN-200D, etc.)
- Hermes Lite 2
- All OpenHPSDR Protocol 1 and Protocol 2 radios

## Reference Repositories

1. **AetherSDR** — `https://github.com/ten9876/AetherSDR`
   - Qt6 / C++20 FlexRadio client
   - Use as the **architectural template** for: radio abstraction layer, state management (`AppSettings::instance()`), signal/slot architecture, panadapter composition, GPU spectrum rendering (QRHI pipeline), and multi-slice layout
   - Key conventions: `QSignalBlocker` to avoid feedback loops, radio as authoritative for radio-side state

2. **Thetis (ramdor)** — `https://github.com/ramdor/Thetis`
   - C# / WinForms — the canonical Apache Labs SDR console
   - This is the **feature source**: every capability in Thetis must be accounted for and ported
   - Also examine `https://github.com/ramdor/ThetisSkins` for legacy skin format

---

## Phase 1 — Architectural Analysis

### 1A: AetherSDR Deep Dive

Clone AetherSDR and document:

- **Radio abstraction layer** — how the radio connection is established, discovered, and managed
- **State management** — how `AppSettings` and the signal/slot architecture keep UI and radio state in sync
- **Audio pipeline** — how audio streams flow from radio to output
- **Spectrum / waterfall pipeline** — the GPU rendering path using QRHI; how spectrum data flows from radio -> FFT -> GPU -> display
- **Panadapter composition** — how panadapter widgets are instantiated, laid out, and independently managed
- **Multi-slice architecture** — how multiple slices share one radio connection

### 1B: Thetis Deep Dive

Clone Thetis and document:

- **Class hierarchy** — map all major classes, especially Radio, Receiver, Slice, Console, and Display-related classes
- **Receiver lifecycle** — how receivers are instantiated, tracked, and destroyed; how they bind to hardware receivers on the radio
- **State synchronization** — how VFO, mode, filter, and audio routing state flows between UI <-> radio
- **Two-panadapter limitation** — where this constraint is enforced in code and why
- **Multi-slice management** — how multiple independent receivers are managed on the same radio
- **Legacy skin format** — how skins define layout, button positions, color schemes, and control placement; parse the skin ZIP structure from ThetisSkins repo

### 1C: WDSP Investigation (Critical)

Perform a comprehensive audit of WDSP usage in Thetis:

- **Complete API surface** — enumerate every WDSP function called by Thetis
- **Feature mapping** — document what each WDSP call does: filters, AGC, noise reduction (NR/NR2), noise blanker (NB/NB2), ANF, equalization, compression, CESSB, PureSignal, VOX/DEXP, and all other DSP functions
- **UI <-> WDSP data flow** — trace how each form control (sliders, checkboxes, dropdowns) maps to a specific WDSP parameter or method call
- **Per-receiver DSP state** — understand how DSP settings are maintained independently per receiver/slice
- **WDSP initialization and teardown** — how the DSP engine is started, configured, and stopped
- **Identify modernization opportunities** — where the WDSP integration can be improved or made more modular in a Qt6 signal/slot architecture

Deliverable: A complete WDSP feature matrix mapping every Thetis UI control to its underlying WDSP call, organized by functional area.

---

## Phase 2 — Architecture Design

### 2A: Radio Abstraction Layer

Design a radio abstraction that:

- Supports Protocol 1 and Protocol 2 OpenHPSDR radios
- Handles radio discovery, connection, and lifecycle
- Manages multiple independent receivers/slices per radio
- Follows AetherSDR's pattern: radio is authoritative for radio-side state
- Exposes a clean Qt6 signal/slot interface for UI binding

### 2B: Multi-Panadapter Layout Engine

Design a **flexible grid-based layout engine** inspired by AetherSDR / SmartSDR:

- Panadapters are composable, independent widgets
- Support up to 4 panadapters on screen simultaneously
- Each panadapter maintains independent state (VFO, mode, filters, zoom) but shares the radio connection
- Layout configurations: 1-up, 2-up side-by-side, 2-up stacked, 2x2 grid, 1 wide + 2 stacked, etc.
- User-resizable and reconfigurable at runtime

### 2C: GPU-Accelerated Waterfall Rendering

- Use Qt6 QRHI for cross-platform GPU rendering (Vulkan, Metal, D3D12, OpenGL)
- Follow AetherSDR's `feature/gpu-spectrum-qrhi` branch patterns
- Priority on waterfall fluidity and responsiveness
- Each panadapter gets its own GPU-rendered waterfall and spectrum display

### 2D: Legacy Skin Compatibility Layer

- Build a skin parser that reads legacy Thetis skin ZIP files
- Map skin-defined button positions, layout regions, and color schemes to the new grid layout system
- Legacy skins render within a two-panadapter constrained grid (honoring the original layout intent)
- Control locations from skin definitions are mapped to the new control routing architecture
- Users can optionally "upgrade" a legacy skin session by enabling additional panadapters beyond what the skin defines

### 2E: WDSP Integration Architecture

- Design a clean C++20 wrapper around WDSP that exposes all DSP functions through a Qt6-native interface
- Each receiver/slice owns its own WDSP channel with independent DSP state
- All DSP parameters are exposed as Qt properties with signal/slot notification
- Preserve 100% feature parity with Thetis WDSP usage — no features dropped in the port

---

## Phase 3 — Implementation Priorities

1. **Radio abstraction + Protocol 1/2 support** — connect to hardware, discover radios, manage receivers
2. **WDSP integration layer** — C++20 wrapper with full feature parity
3. **GPU-accelerated waterfall and spectrum rendering** — QRHI pipeline, smooth and fluid
4. **Multi-panadapter grid layout system** — up to 4 pans, composable and resizable
5. **Audio pipeline** — RX audio routing, TX audio, VAC equivalent
6. **Full DSP control UI** — all filters, AGC, NR, NB, EQ, compression, PureSignal, etc.
7. **Legacy skin compatibility layer** — parse and render Thetis skins
8. **TCI protocol support**
9. **Cross-platform packaging** — Linux, Windows, macOS

---

## Design Principles

- **AetherSDR is the architectural North Star** — follow its patterns for radio abstraction, state management, and UI composition
- **Thetis is the feature North Star** — every feature in Thetis must be accounted for; nothing gets dropped
- **Radio is authoritative** — for radio-side state, the radio is the source of truth; UI reflects radio state, not the other way around
- **Use `QSignalBlocker`** — to prevent feedback loops when programmatically updating UI from radio state
- **Modern C++20** — use concepts, ranges, structured bindings, and modern idioms throughout
- **Cross-platform from day one** — no Windows-only dependencies; Qt6 abstracts platform differences
