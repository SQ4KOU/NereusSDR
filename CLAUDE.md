# NereusSDR

Cross-platform C++20 / Qt6 port of **Thetis** (OpenHPSDR / Apache Labs SDR
console, C#), structured on **AetherSDR** patterns. Targets every OpenHPSDR
Protocol 1 and 2 radio (ANAN line, Hermes Lite 2). The client does ALL signal
processing; the radio is an ADC/DAC with network transport.

## SOURCE-FIRST PORTING PROTOCOL

NereusSDR is a port, not a reimagination. Thetis is authoritative for radio
logic, DSP behavior, protocol handling, constants, state machines and feature
behavior. **Do not guess. Read the source, then translate it.**

| Question | Source |
| --- | --- |
| **What** the code does | Thetis (`../Thetis/Project Files/Source/Console/*.cs`, `wdsp/*.c`) |
| **How** it is structured in Qt6 | AetherSDR (`../AetherSDR/`) |

**READ → SHOW → TRANSLATE.** Read the Thetis source first. Quote or
summarize it ("Porting from file:lines, original C# logic:") before writing.
Translate faithfully. If you can't find the source, **stop and ask** which file
to look in; never fabricate.

Never: write a function body before reading its Thetis equivalent; assume WDSP
signatures, protocol byte layouts, enum values, constants or defaults; infer
behavior from a feature's name; stub with TODOs what Thetis implements;
"improve" Thetis logic unasked; use general DSP knowledge in place of the WDSP
calls Thetis makes.

**Before any port, read [docs/attribution/HOW-TO-PORT.md](docs/attribution/HOW-TO-PORT.md)**
(header template, pre-port provenance checklist, cite grammar, enforcement
scripts, Thetis tree layout). Same protocol for mi0bot-Thetis, AetherSDR,
freedv-gui and WDSP ports.

### License-preservation rule (non-negotiable)

In the same commit that introduces a port, the NereusSDR file header gets the
upstream file's header **byte-for-byte**: every `Copyright (C)` line, the GPL
permission block, the Samphire dual-licence statement if present, plus a
"Modification history (NereusSDR)" block (date, human author, AI tooling).
Headers differ per upstream file and are not interchangeable; a file porting
from several gets each one under `// --- From [filename] ---`. New ports also
add a row to `docs/attribution/THETIS-PROVENANCE.md` (or
`FREEDV-GUI-PROVENANCE.md`). Missing notices are a GPL compliance bug that
blocks the PR.

### Inline comment preservation (SHIP-BLOCKING)

Every `//` comment inside ported logic is copied verbatim, above all developer
tags (`//MW0LGE`, `//-W2PA`, `//[2.10.3.13]MW0LGE`, `//DH1KLM`, `//MI0BOT`, ...),
behavioral notes and TODO/FIXME. If restructuring moves the line, put the
comment on the nearest equivalent line with
`[original inline comment from file:line]`.
`scripts/verify-inline-tag-preservation.py` enforces this in pre-commit and CI.

### Cites, constants, WDSP

* Every ported block and constant carries `// From Thetis file:line [v2.10.3.15]`
  (current pin: v2.10.3.15 / `3759d09`; `[@shortsha]` between releases; get it once per session with
  `git -C ../Thetis describe --tags`).
* Keep constants and magic numbers exactly (`0.98f` stays `0.98f`) as named
  `constexpr` with the cite.
* WDSP calls must match name, parameter order and types in Thetis `wdsp/` and
  the P/Invoke in `Console/dsp.cs`; ranges, defaults and scaling come from the
  Thetis callsite.
* Hardware facts (DDC count, board byte, clocks) may cite the FPGA gateware at
  `../n1gp-Anvelina_PROIII/` (pinned `8e86a61`, never pull). Cite facts only;
  ask before porting Verilog logic. Details in HOW-TO-PORT.md.
* piHPSDR (`../pihpsdr/`, pinned `4aa95c5`) and deskhpsdr (`../deskhpsdr/`,
  pinned `f3d857c`) follow the same rule: cite facts only (PROVENANCE kind
  `reference`), stop and ask before translating their logic, and never propose
  adopting their structure. Details in HOW-TO-PORT.md.

## Agent boundaries

May fix autonomously: bugs with a clear root cause, OpenHPSDR protocol
compliance, build/CI breakage.

Must NOT change without the maintainer: visual design, UX behavior, architecture
(threads, signal routing, dependencies), feature scope, user-facing defaults,
DSP parameters (unless ported from Thetis). When in doubt, implement and flag
the design decision in the PR.

Also: never propose Wine/CrossOver; flag anything touching the core RX path
(I/Q → WDSP → audio); ask for pcaps when protocol behavior is unclear; use
OpenHPSDR specs, not SmartSDR.

## C++ style

Full conventions in [CONTRIBUTING.md](CONTRIBUTING.md). Non-negotiables:

* No `goto`, no raw `new`/`delete` (unique_ptr or Qt parent), no `#define`
  constants (`constexpr`), braces on all control flow, `auto` only when the
  type is obvious.
* Naming: `PascalCase` classes, `camelCase` methods, `kPascalCase` constants,
  `m_camelCase` members.
* Platform guards `Q_OS_WIN` / `Q_OS_MAC` / `Q_OS_LINUX`, never `_WIN32` /
  `__APPLE__`.
* Errors: `qCWarning(lcCategory)`, no exceptions.
* Cross-thread DSP parameters are `std::atomic`; never hold a mutex in the
  audio callback.
* Don't remove code you didn't add.

## Settings

**`AppSettings`, never `QSettings`** (`src/core/AppSettings.h`, XML at
`~/.config/NereusSDR/NereusSDR.settings`). PascalCase keys; booleans are the
strings `"True"` / `"False"`.

* Radio-authoritative, never persisted: antenna selection.
* Saved and sent to the radio on connect, as Thetis does (console.cs:2174-2179):
  ADC attenuation, preamp, per-band TX power. Connecting never keys.
* Per-MAC under `hardware/<mac>/...`: sample rate, active RX count.
* Client-authoritative, persisted: VFO, mode, filter, DSP settings, layout, UI
  and display preferences.

GUI↔model sync: model setters emit, the connection sends; guard echo loops with
`m_updatingFromModel` or `QSignalBlocker` (AetherSDR pattern).

## Architecture

`src/core/` (protocol, audio, DSP), `src/models/` (RadioModel, SliceModel,
PanadapterModel), `src/gui/` (MainWindow, SpectrumWidget, applets). Threads:
main (GUI + all models), connection (UDP), audio (WDSP + output), spectrum
(FFT). Cross-thread traffic is auto-queued signals only. Details and data flow:
[docs/architecture/overview.md](docs/architecture/overview.md).

**Rule R1: nothing under `src/core/` or `src/models/` includes a GUI header.**
`tst_core_has_no_gui_includes` enforces it; extract an interface instead (as
`ISpectrumSink` did).

## Build and test

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
./build/NereusSDR
```

The build also produces the headless `nereusd`, installed only with
`--component nereusd`; install notes in [README.md](README.md).
Dependencies: [README.md](README.md) "Building from Source". **Read
[docs/development/fast-test-loop.md](docs/development/fast-test-loop.md)
before running tests**; build single tests, never the whole suite by default.
First launch generates FFTW wisdom (~15 min), cached in `~/.config/NereusSDR/`.

## Where things live

* Upstreams, cloned as siblings of the repo root:
  `../Thetis/` (github.com/ramdor/Thetis), `../mi0bot-Thetis/` (authoritative
  for HL2), `../AetherSDR/` (github.com/ten9876/AetherSDR), `../freedv-gui/`
  (github.com/drowe67/freedv-gui; RADE steps, FreeDV + PSK Reporter),
  `../n1gp-Anvelina_PROIII/` (github.com/n1gp/Anvelina_PROIII, pinned).
* Vendored: `third_party/wdsp/` (WDSP 2.10, TAPR b02d5bac), `third_party/rade/` (radae_nopy
  b289102, BSD-2), `third_party/r8brain/` (MIT resampler), `third_party/fftw3/`
  (Windows DLL).
* Version: `CMakeLists.txt`. Phase status, release history, plan index:
  [docs/development/project-status.md](docs/development/project-status.md),
  [CHANGELOG.md](CHANGELOG.md), [docs/MASTER-PLAN.md](docs/MASTER-PLAN.md).
* Design docs and plans: `docs/architecture/`. Protocols: `docs/protocols/`.
