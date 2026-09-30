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
   `../Thetis/` (relative to the NereusSDR root). Capture the Thetis
   version tag once at the start of the session — `git -C ../Thetis
   describe --tags` (release) or `git -C ../Thetis rev-parse --short
   HEAD` (between releases) — every inline cite you write in this
   session gets that stamp.
2. **SHOW** the original code before writing anything. State:
   `"Porting from [file]:[function/line range] — original C# logic:"` and
   quote or summarize the relevant section.
3. **TRANSLATE** the C# to C++20/Qt6 faithfully. Use AetherSDR patterns for
   the Qt6 structure (signals/slots, class layout, threading), but the
   **behavior and logic** must come from Thetis.

### License-preservation rule (non-negotiable)

When porting any Thetis file, you MUST — in the same commit that introduces
the port — copy the following from the Thetis source into the NereusSDR
file's header comment:

1. All `Copyright (C)` lines naming contributors (FlexRadio, Wigley,
   Samphire, W2PA, mi0bot, etc.)
2. The GPLv2-or-later permission block verbatim
3. The Samphire dual-licensing statement — ONLY if the Thetis source file
   contains Samphire-authored contributions
4. A trailing "Modification history (NereusSDR)" block with the port date,
   human author, and AI tooling disclosure

Templates live in `docs/attribution/HOW-TO-PORT.md`. Failure to
preserve these notices on a new port is a GPL compliance bug, not a style
nit — reject the PR.

### Byte-for-byte headers and multi-file attribution

Each Thetis source file has its own distinct header with different copyright
holders and modification credits (e.g. `console.cs` credits W2PA and
Samphire; `display.cs` credits VK6APH and Samphire). These headers are NOT
interchangeable.

- Copy each source file's header **byte-for-byte** — do not paraphrase,
  summarize, or merge headers from different files.
- If a NereusSDR file ports from **multiple** Thetis files, include **every**
  relevant header, separated by `// --- From [filename] ---` markers.
- Include the Thetis version (`v2.10.3.15`) and commit (`3759d09`) in the
  "Ported from" line.

### Inline comment preservation — SHIP-BLOCKING

**This is a GPL attribution rule, not a style preference. Dropping a
developer-attribution tag during porting is a compliance bug that
blocks release.** A real incident (2026-04-21) shipped with a
`//DH1KLM` tag silently dropped during a `computeAlexFwdPower` port
— caught only because someone eyeballed the PR. The fix is
mechanical:

All inline comments from Thetis source code within ported logic **must be
preserved verbatim** in the C++ translation. This includes:

- **Developer attribution tags** — `//DH1KLM`, `//MW0LGE`, `//W2PA`,
  `//G8NJJ`, `//MI0BOT`, etc. Canonical list of recognized authors
  lives in `docs/attribution/thetis-author-tags.json`, built
  mechanically by `scripts/discover-thetis-author-tags.py`.
- **Dash-prefix attribution** — `//-W2PA`, `// -W2PA`
- **Version-tagged attribution** — `//[2.10.3.13]MW0LGE`, `//MW0LGE [2.9.0.7]`
- **Underscored variants** — `//MW0LGE_21k5 change to rx2`
- **Behavioral notes** — `// only cleared by getAndResetADC_Overload()`
- **TODO / FIXME / XXX / HACK** annotations
- Any `//` comment on or above a ported line of logic

When the C++ translation restructures the code so that the comment no longer
sits on the same line, place it on the nearest equivalent line with a note:
```cpp
// MW0LGE_21k5 change to rx2  [original inline comment from display.cs:10079]
```

**Mechanical enforcement:** `scripts/verify-inline-tag-preservation.py`
runs in the pre-commit hook chain and in CI. For every
`// From Thetis X:N [@sha]` cite in the diff, it opens `../Thetis/X`
(or `../mi0bot-Thetis/X`) at line N, extracts any author tag within
±5 source lines, and fails the commit if a corresponding tag is not
present within ±10 port lines. No way to land a port with a dropped
tag. If the check fires, re-insert the verbatim tag exactly as it
appears upstream.

**Corpus drift:** when you re-sync Thetis (`git -C ../Thetis pull`),
also run:
```
python3 scripts/discover-thetis-author-tags.py
```
to refresh the corpus. CI's `--drift` check fails the PR if new
upstream contributors aren't in the committed corpus.

### Pre-port checklist (Ring 1 — authoring-time)

Before reading any Thetis source file (`../Thetis/...`), state out loud:

1. **Thetis file** you're about to read.
2. **NereusSDR file(s)** the port will touch (new or existing).
3. **Provenance status** of each NereusSDR file — run:
    ```
    grep -l "<nereussdr-path>" docs/attribution/THETIS-PROVENANCE.md
    ```
   If the file is not registered, the port is a **new attribution event**.
   For freedv-gui ports specifically: also run
   `grep -l "<nereussdr-path>" docs/attribution/FREEDV-GUI-PROVENANCE.md`
   to check provenance status.
4. **Plan**: if (3) returned nothing, you will add the verbatim upstream
   header AND a PROVENANCE row in the same commit that introduces the
   ported logic. Use `docs/attribution/HOW-TO-PORT.md` for the format.

If you cannot answer (3) confidently, **stop and grep** before continuing.
The cost of asking is one shell command; the cost of skipping is a
merge-blocking CI failure (or worse, a missed gap that ships to main).

This applies equally to:
- New files that port Thetis logic.
- Edits to NereusSDR-original files that **add** new ported logic
  (e.g. wiring in a new Thetis-derived constant or formula).
- Ports from non-Thetis upstreams (`../mi0bot-Thetis/`, `../AetherSDR/`,
  `../freedv-gui/`, WDSP). Same protocol, different PROVENANCE table /
  variant.

Verifier scripts (`scripts/verify-thetis-headers.py`,
`scripts/verify-freedv-headers.py`, `scripts/check-new-ports.py`) are the
safety net (Ring 3, in CI). The local pre-commit hook installed via
`scripts/install-hooks.sh` runs the same scripts pre-push (Ring 2). The
primary control is this checklist.

### What Counts As "Guessing" (NEVER Do These)

- Writing a function body without first reading the Thetis equivalent
- Assuming what WDSP function signatures, parameters, or return types look like
- Inventing enum values, constants, magic numbers, thresholds, or defaults
- Paraphrasing what a Thetis feature "probably does" based on its name
- Writing placeholder/stub logic with TODOs for things that exist in Thetis
- Assuming protocol message formats or byte layouts without reading the code
- "Improving" or "simplifying" Thetis logic without being asked to
- Using general DSP knowledge instead of the actual WDSP API calls Thetis makes
- Porting a Thetis file without copying its license header and appending a modification note

### Constants and Magic Numbers

Preserve ALL constants, thresholds, scaling factors, and magic numbers exactly
as they appear in Thetis. If Thetis uses `0.98f`, NereusSDR uses `0.98f`. If
Thetis uses `2048` as a buffer size, document where it came from and keep it.
Give constants a `constexpr` name but note the Thetis origin — with a
version stamp — in a comment:

```cpp
// From Thetis console.cs:4821 [v2.10.3.13] — original value 0.98f
static constexpr float kAgcDecayFactor = 0.98f;
```

The `[v2.10.3.13]` tag records the Thetis release the value was verified
against. Use `[@shortsha]` when no tagged release applies, and refresh the
stamp whenever you re-port from a newer upstream. Full grammar:
`docs/attribution/HOW-TO-PORT.md` §Inline cite versioning.

### WDSP Calls — Extra Caution

- Every WDSP function call must match the exact name, parameter order, and
  types from `Project Files/Source/wdsp/` in the Thetis repo
- Cross-reference against `Project Files/Source/Console/dsp.cs` (the C#
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
│       │   ├── dsp.cs        ← WDSP P/Invoke declarations
│       │   ├── NetworkIO.cs  ← Protocol 1/2 network I/O
│       │   ├── protocol2.cs  ← Protocol 2 specific handling
│       │   └── ...
│       └── wdsp/             ← WDSP C source (DSP engine)
│           ├── channel.c     ← Channel create/destroy/exchange
│           ├── RXA.c         ← RX channel pipeline
│           ├── TXA.c         ← TX channel pipeline
│           └── ...
```

freedv-gui layout: `../freedv-gui/src/reporting/` (FreeDVReporter, pskreporter),
`src/pipeline/` (RADE RX/TX steps, rade_text, EQ, AGC). Tree in
`docs/development/project-status.md`.

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
* **Thetis origin comments**: when porting logic, add `// From Thetis [file]:[line or function] [v<version>|@<shortsha>]` comments. The bracketed stamp records the upstream release or commit the port was verified against; grab it from `git -C ../Thetis describe --tags` (or `rev-parse --short HEAD`) at the moment of porting. Full grammar and placement rules: `docs/attribution/HOW-TO-PORT.md` §Inline cite versioning

---

## Build

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
./build/NereusSDR
```

Dependencies (Arch): `qt6-base qt6-multimedia qt6-svg qt6-websockets cmake ninja pkgconf fftw alsa-lib jack2 pipewire`
Dependencies (Ubuntu/Debian): `qt6-base-dev qt6-base-private-dev qt6-multimedia-dev qt6-shadertools-dev qt6-svg-dev qt6-websockets-dev cmake ninja-build pkg-config libfftw3-dev libgl1-mesa-dev libasound2-dev libjack-jackd2-dev libpipewire-0.3-dev`
Notes:
* `qt6-svg` / `qt6-svg-dev` is hard-required (`find_package(Qt6 REQUIRED COMPONENTS Svg)`).
* `alsa-lib` / `libasound2-dev` and `jack2` / `libjack-jackd2-dev` are hard-required on Linux because PortAudio is built with `PA_USE_ALSA=ON` and `PA_USE_JACK=ON FORCE`; without them the static libportaudio links with zero host APIs.
* `libpipewire-0.3-dev` ≥ 0.3.50 enables the native PipeWire audio bridge (Phase 3O). Build still succeeds without it; the Linux audio path falls back to the existing pactl / LinuxPipeBus FIFO route.

WDSP source is in `third_party/wdsp/` (TAPR v1.29 + linux_port.h for cross-platform).
FFTW3: system package on Linux/macOS, pre-built DLL on Windows (`third_party/fftw3/`).
First run generates FFTW wisdom (~15 min). Cached in `~/.config/NereusSDR/` for subsequent launches.

Current version: see `CMakeLists.txt`. Release history, per-phase status and
what is next live in [docs/development/project-status.md](docs/development/project-status.md)
and [CHANGELOG.md](CHANGELOG.md).

---

## Architecture Quick Reference

Key source directories: `src/core/` (protocol, audio, DSP), `src/models/`
(RadioModel, SliceModel, etc.), `src/gui/` (MainWindow, SpectrumWidget, applets).
Full class list: `docs/development/project-status.md`.

* `RadioModel`: central state, owns connection + all sub-models + WdspEngine
* `SliceModel`: per-receiver VFO state (freq, mode, filter, AGC, gains, antenna). Single source of truth.
* `PanadapterModel`: per-pan display state + per-band grid storage; emits `bandChanged(Band)`
* `Band` (`src/models/Band.h`): 14-band enum with `bandFromFrequency()` / `bandKeyName()`
* `ReceiverManager`: maps logical receivers to hardware DDCs
* `RadioDiscovery` (UDP 1024), `RadioConnection` (P1 / P2)
* `WdspEngine`, `RxChannel`, `TxChannel`: WDSP lifecycle and per-channel wrappers
* `AudioEngine` (QAudioSink), `FFTEngine` (FFTW3 worker), `SpectrumWidget` (QRhi GPU trace + waterfall)
* `VfoWidget`: floating VFO flag (AetherSDR pattern)
* `ContainerWidget` / `FloatingContainer` / `ContainerManager`: Thetis ucMeter / frmMeterDisplay equivalents
* `MeterWidget` + `MeterItem` subclasses + `ItemGroup` presets + `MeterPoller`
* `AppSettings`: XML settings persistence (NOT QSettings)
* `MainWindow`: signal routing hub; `SetupDialog`; `AppletPanelWidget` + `applets/`
* TCI: `TciServer`, `TciProtocol`, `TciClientSession`, `TciBinaryFrame`, `TciSensorManager`, `TciVfoCoalescer`, `TciSendQueue`
* Spots: `SpotModel`, `SpotTableModel`, spot-source clients in `src/core/`, `SpotHubDialog`, `FreeDVReporterDialog`
* RADE: `RadeChannel`, `RadeText`, `RadeApplet`

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

### Data Flow (Phase 3E + CTUN + Zoom — VERIFIED WORKING)

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

FFT → Display (with zoom):
    FFTEngine emits N bins (full DDC bandwidth)
    → SpectrumWidget::updateSpectrum() stores in m_smoothed
    → visibleBinRange(N) maps m_centerHz ± m_bandwidthHz/2 to bin indices
      using m_ddcCenterHz + m_sampleRateHz for bin-to-frequency mapping
    → GPU/CPU renderer iterates only [firstBin..lastBin], stretched to full display
    → pushWaterfallRow() writes only visible bin subset to waterfall texture

User zooms (freq scale drag or Ctrl+scroll):
    m_bandwidthHz changes → visibleBinRange() narrows → immediate visual zoom
    On mouse release → bandwidthChangeRequested → MainWindow replans FFT size
    → FFTEngine delivers more bins → sharper resolution at new zoom level

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
antenna selection.

**Hardware sample rate and active RX count:** persisted per-MAC in AppSettings
under `hardware/<mac>/radioInfo/sampleRate` and `.../activeRxCount`. Applied
on next connect. This matches Thetis, which persists rate globally via
`DB.SaveVarsDictionary("Options", ...)` (setup.cs:1627). NereusSDR scopes
per-MAC so users with multiple radios retain per-radio selections.
(Live-apply history: `docs/development/project-status.md`.)

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

* [docs/MASTER-PLAN.md](docs/MASTER-PLAN.md): phased roadmap and GUI container mapping
* [CONTRIBUTING.md](CONTRIBUTING.md), [STYLEGUIDE.md](STYLEGUIDE.md), [CHANGELOG.md](CHANGELOG.md)
* [docs/development/fast-test-loop.md](docs/development/fast-test-loop.md): **read before running tests** (per-test builds, `ctest -L` labels)
* [docs/development/project-status.md](docs/development/project-status.md): release history, phase status, plan index
* `docs/architecture/`: overview, radio-abstraction, multi-panadapter, gpu-waterfall, wdsp-integration, skin-compatibility, adc-ddc-panadapter-mapping, ctun-zoom-design, plus per-phase plans and verification matrices
* `docs/protocols/`: openhpsdr-protocol1 (+ capture reference), openhpsdr-protocol2

---

## Reference Repositories

1. **AetherSDR** — `https://github.com/ten9876/AetherSDR`
   * Architectural template: radio abstraction, state management, signal/slot patterns, GPU rendering, multi-pan layout
2. **Thetis** — `https://github.com/ramdor/Thetis`
   * Feature source: every Thetis capability must be accounted for and ported
   * **Clone to `../Thetis/` relative to NereusSDR root**
3. **WDSP** — `https://github.com/TAPR/OpenHPSDR-wdsp`
   * DSP engine: all signal processing functions
4. **freedv-gui** - `https://github.com/drowe67/freedv-gui`
   * RADE codec wrappers (RADEReceiveStep, RADETransmitStep, rade_text)
   * FreeDV Reporter Socket.IO client (qso.freedv.org)
   * PSK Reporter UDP client
   * **Clone to `../freedv-gui/` relative to NereusSDR root**
5. **radae_nopy (peterbmarks)** - `https://github.com/peterbmarks/radae_nopy`
   * RADE C library (BSD-2-Clause) vendored at SHA b289102 into `third_party/rade/`
   * Neural-net weights compiled into librade; no external model file ships
   * Native callsign-over-EOO API consumed via `RadeText` wrapper (Task I4 Option B per Phase 3R)
6. **r8brain-free-src** - `https://github.com/avaneev/r8brain-free-src`
   * MIT-licensed 24-bit polyphase resampler vendored at `third_party/r8brain/`
   * Used by the RADE 48-to-16 kHz TX audio chain and reserved for future general resampling needs
7. **n1gp-Anvelina_PROIII (FPGA gateware)** - `https://github.com/n1gp/Anvelina_PROIII`
   * **Clone to `../n1gp-Anvelina_PROIII/` relative to NereusSDR root.** Pinned at
     SHA `8e86a61` ("Version 2.2.14 Final", 2026-07-06). Do not `git pull`.
   * Hardware authority for receiver/DDC count, board-type byte, protocol
     version, master clock. Key facts in `docs/development/project-status.md`.
   * **`NR` is a compile-time constant that changes between firmware releases**
     (shipped as 2, 4, 7 and 8 at different times on the same board). No static
     per-board DDC count can be correct across firmware versions.

### Gateware citations — cite facts, don't port logic

The gateware is **GPLv3**, the same licence NereusSDR itself ships under (root
`LICENSE`), so there is **no licence conflict** — unlike a GPLv2-only or
proprietary upstream, this can be used freely. The constraint below is about
scope and correctness, not legal risk:

* **Normal use** — cite a *fact* the gateware establishes: a receiver count, a
  board-type byte, a clock rate, a register width. A cite like
  `// From n1gp-Anvelina_PROIII Orion.v:958 [@8e86a61] — NR = 8` records where a
  hardware number actually came from, the same standing as citing a datasheet.
  Prefer this over a Thetis cite whenever the claim is about *hardware*.
* **Stop and ask first** — translating Verilog *logic* into NereusSDR. It is
  licence-compatible but almost always the wrong move: gateware logic runs on
  the radio, not in the client, so needing it usually means the design took a
  wrong turn. If a task genuinely calls for it, the full port protocol applies
  (verbatim header, PROVENANCE row as kind `port`, author tags preserved).
* Fact-only citations use PROVENANCE kind `reference`, not `port`.
* The gateware carries its own author tags (`Yurij-eu2av` in `Orion.v`). If a
  gateware comment is ever quoted verbatim, the inline-comment-preservation rule
  applies to it exactly as it does to Thetis tags.
