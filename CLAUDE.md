# NereusSDR — Project Context for Claude

## Project Goal

Port **Thetis** (the OpenHPSDR / Apache Labs SDR console, written in C#) to a
**cross-platform C++20 application** using Qt6. The architectural template is
**AetherSDR** (a FlexRadio SmartSDR client). Target radios: all OpenHPSDR
Protocol 1 and Protocol 2 devices, including the Apache Labs ANAN line and
Hermes Lite 2.

---

## ⚠️ SOURCE-FIRST PORTING PROTOCOL (Read This Before Every Task)

NereusSDR is a **port**, not a reimagination. The Thetis codebase is the
authoritative source for all radio logic, DSP behavior, protocol handling,
constants, state machines, and feature behavior. **Do not guess. Do not
infer. Do not improvise.** Read the source, then translate it.

### The Rule: READ → SHOW → TRANSLATE

For every piece of logic you write that has a Thetis equivalent:

1. **READ** the relevant Thetis source file(s). Use `find`, `grep`, or `rg`
   to locate the C# code. The Thetis repo should be cloned at
   `../Thetis/` (relative to the NereusSDR root).
2. **SHOW** the original code before writing anything. State:
   `"Porting from [file]:[function/line range] — original C# logic:"` and
   quote or summarize the relevant section.
3. **TRANSLATE** the C# to C++20/Qt6 faithfully. Use AetherSDR patterns for
   the Qt6 structure (signals/slots, class layout, threading), but the
   **behavior and logic** must come from Thetis.

### What Counts As "Guessing" (NEVER Do These)

- Writing a function body without first reading the Thetis equivalent
- Assuming what WDSP function signatures, parameters, or return types look like
- Inventing enum values, constants, magic numbers, thresholds, or defaults
- Paraphrasing what a Thetis feature "probably does" based on its name
- Writing placeholder/stub logic with TODOs for things that exist in Thetis
- Assuming protocol message formats or byte layouts without reading the code
- "Improving" or "simplifying" Thetis logic without being asked to
- Using general DSP knowledge instead of the actual WDSP API calls Thetis makes

### Constants and Magic Numbers

Preserve ALL constants, thresholds, scaling factors, and magic numbers exactly
as they appear in Thetis. If Thetis uses `0.98f`, NereusSDR uses `0.98f`. If
Thetis uses `2048` as a buffer size, document where it came from and keep it.
Give constants a `constexpr` name but note the Thetis origin in a comment:

```cpp
// From Thetis console.cs:4821 — original value 0.98f
static constexpr float kAgcDecayFactor = 0.98f;
```

### WDSP Calls — Extra Caution

- Every WDSP function call must match the exact name, parameter order, and
  types from `Project Files/Source/wdsp/` in the Thetis repo
- Cross-reference against `Project Files/Source/Console/wdsp.cs` (the C#
  P/Invoke declarations) for the managed-side signatures
- DSP parameter ranges, defaults, and scaling come from Thetis code, not
  from general knowledge or WDSP documentation
- When in doubt, read both the WDSP C source AND the Thetis C# callsite

### If You Can't Find the Source

**STOP AND ASK.** Say: "I cannot locate the Thetis source for [X]. Which
file or class should I look in?" Do NOT fabricate an implementation. It is
always better to ask than to guess wrong.

### Thetis Source Layout Quick Reference

```
../Thetis/
├── Project Files/
│   └── Source/
│       ├── Console/          ← Main UI, radio logic, state management
│       │   ├── console.cs    ← Monster file: VFO, band, mode, DSP, display
│       │   ├── setup.cs      ← Setup dialog (hardware config, DSP params)
│       │   ├── display.cs    ← Spectrum/waterfall rendering
│       │   ├── audio.cs      ← Audio engine, VAC, portaudio
│       │   ├── cmaster.cs    ← Channel master (WDSP channel management)
│       │   ├── wdsp.cs       ← WDSP P/Invoke declarations
│       │   ├── NetworkIO.cs  ← Protocol 1/2 network I/O
│       │   ├── protocol2.cs  ← Protocol 2 specific handling
│       │   └── ...
│       └── wdsp/             ← WDSP C source (DSP engine)
│           ├── channel.c     ← Channel create/destroy/exchange
│           ├── RXA.c         ← RX channel pipeline
│           ├── TXA.c         ← TX channel pipeline
│           └── ...
```

### The Two-Source Rule

| Question | Source |
| --- | --- |
| **What** does the code do? | Thetis (C# source) |
| **How** do we structure it in Qt6? | AetherSDR (C++20/Qt6 patterns) |

AetherSDR provides the **skeleton** (class structure, signals/slots, threading,
state management patterns). Thetis provides the **organs** (logic, algorithms,
constants, protocol handling, DSP flow, feature behavior).

---

## Key Design Differences from AetherSDR

| Aspect | AetherSDR | NereusSDR |
| --- | --- | --- |
| Protocol | SmartSDR (TCP + VITA-49 UDP) | OpenHPSDR P1 (UDP only) + P2 (TCP + UDP) |
| DSP | Radio-side (FlexRadio firmware) | Client-side (WDSP library) |
| FFT/Waterfall | Radio sends FFT bins + WF tiles | Client computes from raw I/Q via FFTW3 |
| Audio path | Radio sends decoded PCM audio | Client decodes via WDSP from raw I/Q |
| Slice ownership | Radio-authoritative | Client-managed (radio has no slice concept) |
| PureSignal | N/A (FlexRadio has SmartSignal) | Client-side PA linearization via WDSP |

**Critical implication:** In NereusSDR, the client does ALL signal processing.
The radio is essentially an ADC/DAC with network transport. This means higher
client CPU/GPU usage and a fundamentally different data pipeline.

## AI Agent Guidelines

When helping with NereusSDR:

* Prefer C++20 / Qt6 idioms (std::ranges, concepts if clean, Qt signals/slots)
* Keep classes small and single-responsibility
* Use RAII everywhere (no naked new/delete)
* Comment non-obvious protocol decisions with protocol version (P1 vs P2)
* When suggesting code: show **diff-style** changes or full function/class if small
* Never suggest Wine/Crossover workarounds — goal is native cross-platform
* Flag any proposal that would break the core RX path (I/Q → WDSP → audio)
* If unsure about protocol behavior → ask for pcap captures first
* **Use `AppSettings`, never `QSettings`** — see "Settings Persistence" below
* **Read `CONTRIBUTING.md`** for full contributor guidelines and coding conventions
* Reference OpenHPSDR protocol specs, not SmartSDR protocol

### Autonomous Agent Boundaries

AI agents may autonomously fix:

* **Bugs with clear root cause** — persistence missing, guard missing, crash fix
* **Protocol compliance** — matching OpenHPSDR protocol spec behavior
* **Build/CI fixes** — missing dependencies, platform compatibility

AI agents must **NOT** autonomously change:

* **Visual design** — colors, fonts, layout, theme
* **UX behavior** — how controls work, what clicks do, keyboard shortcuts
* **Architecture** — adding new threads, changing signal routing, new dependencies
* **Feature scope** — adding features beyond what the issue describes
* **Default values** — changing defaults that affect all users
* **DSP parameters or constants** — unless directly porting from Thetis source

When in doubt, implement the fix and note in the PR that design decisions need
maintainer review.

## C++ Style Guide

* **No `goto`** — use early returns, break, or restructure the logic
* **No raw `new`/`delete`** — use `std::unique_ptr`, `std::make_unique`, or Qt parent ownership
* **No `#define` macros for constants** — use `constexpr` or `static constexpr`
* **Braces on all control flow** — even single-line `if`/`else`/`for`/`while`
* **`auto` sparingly** — use explicit types unless the type is obvious from context
* **Naming**: classes `PascalCase`, methods/variables `camelCase`, constants `kPascalCase`, member variables `m_camelCase`
* **Platform guards**: use `#ifdef Q_OS_WIN` / `Q_OS_MAC` / `Q_OS_LINUX`, not `_WIN32` or `__APPLE__`
* **Don't remove code you didn't add** — review the diff before submitting
* **Atomic parameters for cross-thread DSP** — main thread writes via `std::atomic`, audio thread reads. Never hold a mutex in the audio callback.
* **Error handling**: log with `qCWarning(lcCategory)`, don't throw exceptions
* **Thetis origin comments**: when porting logic, add `// From Thetis [file]:[line or function]` comments

## Build

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
./build/NereusSDR
```

Dependencies: `qt6-base qt6-multimedia cmake ninja pkgconf fftw`

Current version: **0.1.0** (set in `CMakeLists.txt`).

---

## Architecture Overview

Key source directories: `src/core/` (protocol, audio, DSP), `src/models/`
(RadioModel, SliceModel, etc.), `src/gui/` (MainWindow, future SpectrumWidget, applets).

**Key classes:**

* `RadioModel` — central state, owns connection + all sub-models + WdspEngine
* `RadioDiscovery` — OpenHPSDR radio discovery on UDP port 1024
* `RadioConnection` — Protocol 1 (UDP) and Protocol 2 (TCP+UDP) connections
* `WdspEngine` — C++20 wrapper around WDSP DSP library
* `AudioEngine` — I/Q → WDSP → audio output pipeline
* `AppSettings` — custom XML settings persistence (NOT QSettings)
* `MainWindow` — wires everything together, signal routing hub

**Design principle:** RadioModel owns all sub-models on the main thread.
Worker threads communicate exclusively via auto-queued signals. Never hold
a mutex in the audio callback.

### Data Flow

```
Radio (ADC) → UDP → Raw I/Q samples
                        ↓
                    WdspEngine (per-receiver channel)
                    ├── Demodulation (AM/FM/SSB/CW/DIGU/DIGL)
                    ├── AGC, NB, NR, ANF, Filtering
                    ├── FFT → Spectrum/Waterfall data
                    └── Decoded audio → AudioEngine → Speakers

User TX → Mic → AudioEngine → WdspEngine (TX channel)
                                ├── Compression, EQ, VOX
                                ├── PureSignal (PA linearization)
                                └── I/Q samples → UDP → Radio (DAC)
```

---

## OpenHPSDR Protocol

### Protocol 1 (Metis/Hermes/Angelia/Orion)

* **Transport:** UDP only, port 1024
* **Discovery:** Broadcast UDP packet to port 1024, radios respond with board type and MAC
* **Data format:** 1032-byte Metis frames containing:
  + 8-byte header (sync 0xEFFE, endpoint, sequence)
  + Two 512-byte USB frames, each with 5 bytes of C&C (command and control) data + 63 I/Q sample triplets
* **I/Q samples:** 24-bit big-endian, interleaved I-Q-I-Q or I-Q-Mic depending on receiver count
* **Control:** C&C bytes in the USB frame headers (bidirectional)
* **Endpoints:** EP2 (PC→Radio commands), EP4 (PC→Radio wideband data), EP6 (Radio→PC I/Q + mic)
* **Receiver count:** 1-7 receivers multiplexed in EP6 stream, configured via C&C

### Protocol 2 (Orion MkII / Saturn / ANAN-G2)

* **Transport:** TCP for commands + UDP for high-bandwidth data
* **Discovery:** UDP broadcast/multicast on port 1024
* **Command channel:** TCP with structured command/response protocol
* **Data channels:** Separate UDP streams for each data type
* **Supports:** Independent receiver streams, wider bandwidth, more control

### Key Protocol Facts

* Radio has NO concept of "slices" — that is a client-side abstraction
* Radio sends raw I/Q samples; ALL DSP is done client-side via WDSP
* Radio does NOT compute FFT — client must compute spectrum from raw I/Q
* Discovery uses UDP port 1024 (not 4992 like SmartSDR)
* No VITA-49 framing (unlike FlexRadio) — protocol-specific binary framing

---

## WDSP Integration

WDSP (by Warren Pratt NR0V) provides ALL signal processing:

* **Demodulation:** AM, SAM, FM, USB, LSB, CW, DIGU, DIGL, DRM, SPEC
* **AGC:** multiple modes (Off, Long, Slow, Medium, Fast, Custom)
* **Noise blanker:** NB and NB2
* **Noise reduction:** NR (LMS) and NR2 (spectral)
* **Auto-notch filter:** ANF
* **Bandpass filtering:** variable width per mode
* **Equalization:** RX and TX multi-band EQ
* **TX processing:** compression, CESSB, VOX/DEXP
* **PureSignal:** PA linearization using feedback I/Q
* **Spectrum/FFT:** WDSP can compute display spectrum data

### WDSP Architecture in NereusSDR

* One WDSP "channel" per receiver (independent DSP state)
* `WdspEngine` class wraps the WDSP C API with C++20/Qt6 interface
* All DSP parameters exposed as Qt properties with signal/slot notification
* Audio thread feeds I/Q into WDSP, gets demodulated audio out
* FFT data extracted from WDSP for spectrum/waterfall display
* PureSignal uses a separate feedback receiver channel

---

## Key Implementation Patterns

### Settings Persistence (AppSettings — NOT QSettings)

**IMPORTANT:** Do NOT use `QSettings` anywhere in NereusSDR. All client-side
settings are stored via `AppSettings` (`src/core/AppSettings.h`), which writes
an XML file at `~/.config/NereusSDR/NereusSDR.settings`. Key names use
PascalCase (e.g. `LastConnectedRadioMac`, `DisplayFftAverage`). Boolean
values are stored as `"True"` / `"False"` strings.

```
auto& s = AppSettings::instance();
s.setValue("MyFeatureEnabled", "True");
bool on = s.value("MyFeatureEnabled", "False").toString() == "True";
```

### Radio-Authoritative Settings Policy

The radio is authoritative for hardware state (sample rate, ADC configuration,
attenuator, preamp, TX power level). NereusSDR must not override these from
saved settings on reconnect.

**Radio-authoritative (do NOT persist):** ADC attenuation, preamp, TX power,
antenna selection, hardware sample rate.

**Client-authoritative (persist in AppSettings):** VFO frequency, mode, filter,
DSP settings (AGC, NR, NB, ANF), layout arrangement, UI preferences, display
preferences. These are all client-side because OpenHPSDR radios don't store
per-slice state.

### GUI↔Model Sync (No Feedback Loops)

* Model setters emit signals → RadioConnection sends protocol commands
* Protocol responses update models via `applyStatus()` or equivalent
* Use `m_updatingFromModel` guard or `QSignalBlocker` to prevent echo loops
* Follow AetherSDR's proven pattern exactly

### Thread Architecture

| Thread | Components |
| --- | --- |
| **Main** | GUI rendering, RadioModel, all sub-models, user input |
| **Connection** | RadioConnection (UDP/TCP I/O, protocol framing) |
| **Audio** | AudioEngine + WdspEngine (I/Q processing, DSP, audio output) |
| **Spectrum** | FFT computation, waterfall data generation |

Cross-thread communication uses auto-queued signals exclusively.

---

## Reference Repositories

1. **AetherSDR** — `https://github.com/ten9876/AetherSDR`
   * Architectural template: radio abstraction, state management, signal/slot patterns, GPU rendering, multi-pan layout
2. **Thetis** — `https://github.com/ramdor/Thetis`
   * Feature source: every Thetis capability must be accounted for and ported
   * **Clone to `../Thetis/` relative to NereusSDR root**
3. **WDSP** — `https://github.com/TAPR/OpenHPSDR-wdsp`
   * DSP engine: all signal processing functions
