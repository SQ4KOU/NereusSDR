# NereusSDR

**A cross-platform SDR console for OpenHPSDR radios**

[![CI](https://github.com/boydsoftprez/NereusSDR/actions/workflows/ci.yml/badge.svg)](https://github.com/boydsoftprez/NereusSDR/actions/workflows/ci.yml)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Qt6](https://img.shields.io/badge/Qt-6-green.svg)](https://www.qt.io/)

NereusSDR is a ground-up port of [Thetis](https://github.com/ramdor/Thetis) (the Apache Labs / OpenHPSDR SDR console) from C# to C++20 and Qt6. It uses [AetherSDR](https://github.com/ten9876/AetherSDR) as the architectural template. The goal is a modern, cross-platform, GPU-accelerated SDR console that preserves the full feature set of Thetis while dramatically improving the UI, multi-panadapter experience, and waterfall fluidity.

---

## Supported Radios

Works with any radio implementing OpenHPSDR Protocol 1 or Protocol 2:

- **Apache Labs ANAN line** — ANAN-G2 (Saturn), ANAN-7000DLE, ANAN-8000DLE, ANAN-200D, ANAN-100D, ANAN-100, ANAN-10E
- **Hermes Lite 2**
- **All OpenHPSDR Protocol 1 radios** — Metis, Hermes, Angelia, Orion, Orion MkII
- **All OpenHPSDR Protocol 2 radios**

---

## Planned Features

- [ ] OpenHPSDR Protocol 1 & Protocol 2 support
- [ ] WDSP DSP engine integration (AGC, NR, NB, ANF, demodulation, PureSignal)
- [ ] GPU-accelerated waterfall and spectrum display (QRhi)
- [ ] Multi-panadapter layout (up to 4 independent panadapters)
- [ ] Full RX/TX controls with per-receiver DSP state
- [ ] PureSignal PA linearization
- [ ] Legacy Thetis skin compatibility
- [ ] TCI protocol server
- [ ] CAT control (rigctld)
- [ ] DAX virtual audio channels
- [ ] Cross-platform support (Windows, Linux, macOS)

---

## Building from Source

### Dependencies

```bash
# Ubuntu 24.04+ / Debian
sudo apt install qt6-base-dev qt6-multimedia-dev \
  cmake ninja-build pkg-config \
  libfftw3-dev libgl1-mesa-dev

# Arch / CachyOS / Manjaro
sudo pacman -S qt6-base qt6-multimedia cmake ninja pkgconf fftw

# macOS (Homebrew)
brew install qt@6 ninja cmake pkgconf fftw
```

### Build & Run

```bash
git clone https://github.com/boydsoftprez/NereusSDR.git
cd NereusSDR
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
./build/NereusSDR
```

---

## Project Phases

### Phase 1 — Architectural Analysis
Deep dive into AetherSDR (architecture template), Thetis (feature source), and WDSP (DSP engine).

### Phase 2 — Architecture Design
Radio abstraction layer, multi-panadapter layout engine, GPU waterfall rendering, WDSP integration architecture, legacy skin compatibility.

### Phase 3 — Implementation
Radio protocol support, WDSP integration, GPU rendering, multi-pan layout, audio pipeline, full DSP control UI, skin compatibility, TCI, cross-platform packaging.

See [docs/project-brief.md](docs/project-brief.md) for the full project brief.

---

## Contributing

PRs, bug reports, and feature requests welcome! See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

**Development environment:** NereusSDR is developed using [Claude Code](https://claude.com/claude-code) as the primary development tool. We encourage contributors to use Claude Code for consistency. PRs must follow project conventions, pass CI, and include GPG-signed commits.

---

## Heritage

NereusSDR stands on the shoulders of these projects:

- **[Thetis](https://github.com/ramdor/Thetis)** — The canonical Apache Labs / OpenHPSDR SDR console (C# / WinForms). NereusSDR's feature source.
- **[AetherSDR](https://github.com/ten9876/AetherSDR)** — Native FlexRadio client (C++20 / Qt6). NereusSDR's architectural template.
- **[WDSP](https://github.com/TAPR/OpenHPSDR-wdsp)** — Warren Pratt NR0V's DSP library. The signal processing engine.
- **[OpenHPSDR](https://openhpsdr.org/)** — The open-source high-performance SDR project and protocol specifications.

---

## License

NereusSDR is free and open-source software licensed under the [GNU General Public License v3](LICENSE).

*NereusSDR is an independent project and is not affiliated with or endorsed by Apache Labs or the OpenHPSDR project.*
