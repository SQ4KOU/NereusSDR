# NereusSDR — Project Context for Claude

## Project Goal

Port **Thetis** (the OpenHPSDR / Apache Labs SDR console, written in C#) to a
**cross-platform C++20 application** using Qt6. The architectural template is
**AetherSDR** (a FlexRadio SmartSDR client). Target radios: all OpenHPSDR
Protocol 1 and Protocol 2 devices, including the Apache Labs ANAN line and
Hermes Lite 2.

**Critical implication:** The client does ALL signal processing (DSP, FFT,
demodulation). The radio is essentially an ADC/DAC with network transport.

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

### The Two-Source Rule

| Question | Source |
| --- | --- |
| **What** does the code do? | Thetis (C# source) |
| **How** do we structure it in Qt6? | AetherSDR (C++20/Qt6 patterns) |

AetherSDR provides the **skeleton** (class structure, signals/slots, threading,
state management patterns). Thetis provides the **organs** (logic, algorithms,
constants, protocol handling, DSP flow, feature behavior).

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

---

## AI Agent Guidelines

When helping with NereusSDR:

* Prefer C++20 / Qt6 idioms (std::ranges, concepts if clean, Qt signals/slots)
* Keep classes small and single-responsibility
* Use RAII everywhere (no naked new/delete)
* Comment non-obvious protocol decisions with protocol version (P1 vs P2)
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

---

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

---

## Build

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
./build/NereusSDR
```

Dependencies: `qt6-base qt6-multimedia cmake ninja pkgconf fftw`

WDSP source is in `third_party/wdsp/` (TAPR v1.29 + linux_port.h for cross-platform).
FFTW3: system package on Linux/macOS, pre-built DLL on Windows (`third_party/fftw3/`).
First run generates FFTW wisdom (~15 min). Cached in `~/.config/NereusSDR/` for subsequent launches.

Current version: **0.1.0** (set in `CMakeLists.txt`).

---

## Architecture Quick Reference

Key source directories: `src/core/` (protocol, audio, DSP), `src/models/`
(RadioModel, SliceModel, etc.), `src/gui/` (MainWindow, SpectrumWidget, applets).

**Key classes:**

* `RadioModel` — central state, owns connection + all sub-models + WdspEngine
* `SliceModel` — per-receiver VFO state (freq, mode, filter, AGC, gains, antenna). Single source of truth.
* `ReceiverManager` — DDC-aware receiver lifecycle, maps logical receivers to hardware DDCs; exposes DDC center frequency for CTUN pan positioning
* `RadioDiscovery` — OpenHPSDR radio discovery on UDP port 1024
* `RadioConnection` — Protocol 1 (UDP) and Protocol 2 (UDP multi-port) connections
* `WdspEngine` — WDSP lifecycle manager (wisdom, channels, impulse cache)
* `RxChannel` — per-receiver WDSP channel wrapper (fexchange2, NB, mode/filter/AGC, shift offset for CTUN demodulation)
* `AudioEngine` — QAudioSink output (Int16 stereo, timer-based drain)
* `FFTEngine` — FFTW3 spectrum computation (worker thread, I/Q → dBm bins)
* `SpectrumWidget` — GPU spectrum trace + waterfall display (QRhiWidget — Metal/Vulkan/D3D12)
* `VfoWidget` — floating VFO flag (AetherSDR pattern): freq display, mode/filter/AGC tabs, antenna buttons
* `AppSettings` — custom XML settings persistence (NOT QSettings)
* `MainWindow` — wires everything together, signal routing hub

**Thread Architecture:**

| Thread | Components |
| --- | --- |
| **Main** | GUI rendering, RadioModel, all sub-models, user input |
| **Connection** | RadioConnection (UDP I/O, protocol framing) |
| **Audio** | AudioEngine + WdspEngine (I/Q processing, DSP, audio output) |
| **Spectrum** | FFT computation, waterfall data generation |

Cross-thread communication uses auto-queued signals exclusively.
RadioModel owns all sub-models on the main thread. Never hold a mutex in the
audio callback.

### Data Flow (Phase 3E + CTUN — VERIFIED WORKING)

```
Radio (ADC) → UDP port 1037 (DDC2) → P2RadioConnection
    ↓ iqDataReceived(ddcIndex=2, interleaved float I/Q)
ReceiverManager::feedIqData(2) → maps DDC2 → receiver 0
    ↓ iqDataForReceiver(0, samples)
RadioModel lambda:
    ├── emit rawIqData(samples) → FFTEngine → SpectrumWidget
    ├── Deinterleave I/Q, accumulate 238 → 1024 samples
    └── RxChannel::processIq() → fexchange2() → decoded audio
        ↓
    AudioEngine::feedAudio() → float→int16 → m_rxBuffer
        ↓ 10ms timer drain
    QAudioSink (48kHz stereo Int16) → Speakers

User tunes VFO:
    VfoWidget (wheel/click/edit) → emit frequencyChanged(hz)
    → SliceModel::setFrequency(hz)
    → ReceiverManager::setReceiverFrequency(0, hz)
      → hardwareFrequencyChanged(DDC2, hz)
      → P2RadioConnection::setReceiverFrequency(2, hz) + Alex HPF/LPF update
      → sendCmdHighPriority() → radio retunes DDC NCO
```

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

**Radio-authoritative (do NOT persist):** ADC attenuation, preamp, TX power,
antenna selection, hardware sample rate.

**Client-authoritative (persist in AppSettings):** VFO frequency, mode, filter,
DSP settings (AGC, NR, NB, ANF), layout arrangement, UI preferences, display
preferences. OpenHPSDR radios don't store per-slice state.

### GUI↔Model Sync (No Feedback Loops)

* Model setters emit signals → RadioConnection sends protocol commands
* Protocol responses update models via `applyStatus()` or equivalent
* Use `m_updatingFromModel` guard or `QSignalBlocker` to prevent echo loops
* Follow AetherSDR's proven pattern exactly

---

## Documentation Index

### Master Plan & Progress

| Document | Description |
| --- | --- |
| [docs/MASTER-PLAN.md](docs/MASTER-PLAN.md) | Full phased roadmap, menu bar layout, GUI container mapping (Thetis → NereusSDR), skin system design, progress tracking |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Contributor guidelines, coding conventions, PR process |
| [STYLEGUIDE.md](STYLEGUIDE.md) | Applet color palette, button states, gauge zones, slider/combo styling |
| [CHANGELOG.md](CHANGELOG.md) | Version history and per-phase feature additions |

### Architecture Design Docs (`docs/architecture/`)

| Document | Scope |
| --- | --- |
| [overview.md](docs/architecture/overview.md) | Layer diagram, thread architecture, RX/TX data flow overview |
| [radio-abstraction.md](docs/architecture/radio-abstraction.md) | P1/P2 connections, MetisFrameParser, ReceiverManager, C&C register map, protocol details |
| [multi-panadapter.md](docs/architecture/multi-panadapter.md) | PanadapterStack (5 layouts), PanadapterApplet, wirePanadapter(), FFTRouter |
| [gpu-waterfall.md](docs/architecture/gpu-waterfall.md) | FFTEngine, SpectrumWidget, QRhi shaders, overlay system, color schemes |
| [wdsp-integration.md](docs/architecture/wdsp-integration.md) | RxChannel/TxChannel wrappers, PureSignal, thread safety, WDSP channel lifecycle |
| [skin-compatibility.md](docs/architecture/skin-compatibility.md) | SkinParser, extended skin format, Thetis import, 4-pan support |
| [adc-ddc-panadapter-mapping.md](docs/architecture/adc-ddc-panadapter-mapping.md) | ADC->DDC->Receiver->FFT->Pan signal chain, Thetis UpdateDDCs() analysis, per-board DDC assignment, bandwidth limits |

### Implementation Plans (`docs/architecture/phase*-plan.md`)

| Plan | Phase | Status |
| --- | --- | --- |
| [phase3d-spectrum-waterfall-plan.md](docs/architecture/phase3d-spectrum-waterfall-plan.md) | 3D: GPU Spectrum & Waterfall | **Complete** |
| [phase3f-multi-panadapter-plan.md](docs/architecture/phase3f-multi-panadapter-plan.md) | 3F: Multi-Panadapter + DDC Assignment | Planning |

### Protocol Reference (`docs/protocols/`)

| Document | Scope |
| --- | --- |
| [openhpsdr-protocol1.md](docs/protocols/openhpsdr-protocol1.md) | P1 UDP framing, 1032-byte Metis frames, C&C bytes, EP6 I/Q format |
| [openhpsdr-protocol2.md](docs/protocols/openhpsdr-protocol2.md) | P2 UDP multi-port, command packets, per-DDC I/Q streams |

### Phase 1 Analysis Docs (`docs/phase1/`)

| Document | Key Findings |
| --- | --- |
| 1A: AetherSDR Analysis | RadioModel hub, auto-queued signals, worker threads, AppSettings XML, GPU rendering via QRhi |
| 1B: Thetis Analysis | Dual-thread DSP (RX1/RX2), pre-allocated receivers, one-way protocol, skin system |
| 1C: WDSP Analysis | 256 API functions, channel-based DSP, fexchange2() for I/Q, PureSignal feedback loop |

### Current Phase: 3F — Multi-Panadapter

| Phase | Goal | Status |
| --- | --- | --- |
| 3A: Radio Connection | Connect to ANAN-G2 via P2, receive I/Q | **Complete** |
| 3B: WDSP Integration | Process I/Q through WDSP, audio output | **Complete** |
| 3C: macOS Build | Cross-platform WDSP build + wisdom crash fix | **Complete** |
| 3D: Spectrum Display | GPU spectrum + waterfall (QRhi Metal/Vulkan/D3D12) | **Complete** |
| 3E: VFO + Multi-RX Foundation | VFO controls, CTUN panadapter, rewired I/Q pipeline | **Complete** |
| **3F: Multi-Panadapter** | **DDC assignment (incl. PS states), FFTRouter, PanadapterStack, enable RX2** | **Next up** |
| 3I-1: Basic SSB TX | TxChannel, mic input, MOX state machine, I/Q output | Planned |
| 3I-2: CW TX | Sidetone, firmware keyer, QSK/break-in | Planned |
| 3I-3: TX Processing | 18-stage TXA chain + RX DSP additions (SNB, peak hold, histogram) | Planned |
| 3I-4: PureSignal | Feedback DDC, calcc/IQC engine, PSForm, AmpView | Planned |
| 3G-1..6: Container System | GPU meter rendering, 30+ item types, full composability | Planned |
| 3H: Skins | Thetis-inspired skin format, 4-pan, legacy import | Planned |
| 3J: TCI + Spots | TCI server, DX Cluster/RBN clients, spot overlay | Planned |
| 3K: CAT/rigctld | 4-channel rigctld, TCP CAT server | Planned |
| 3L: Protocol 1 | Hermes Lite 2, older ANAN radios | Planned |
| 3M: Recording | WAV record/playback, I/Q record, scheduled | Planned |
| 3N: Packaging | AppImage, NSIS, DMG release builds | Planned |

---

## Reference Repositories

1. **AetherSDR** — `https://github.com/ten9876/AetherSDR`
   * Architectural template: radio abstraction, state management, signal/slot patterns, GPU rendering, multi-pan layout
2. **Thetis** — `https://github.com/ramdor/Thetis`
   * Feature source: every Thetis capability must be accounted for and ported
   * **Clone to `../Thetis/` relative to NereusSDR root**
3. **WDSP** — `https://github.com/TAPR/OpenHPSDR-wdsp`
   * DSP engine: all signal processing functions
