# Third-Party License Notices

NereusSDR binaries are shipped under GPL-3.0 (see root `LICENSE`). The
binary conveys a combined work that links or vendors the dependencies
listed below, each under its own licence. Full licence text for each
licence that governs code compiled into the binary ships in this
directory alongside NereusSDR's own `LICENSE`.

## Dependencies compiled into the binary

| Dependency | Role | Licence | Notice file | Full licence text |
| --- | --- | --- | --- | --- |
| NereusSDR | application | GPL-3.0 | (see root `LICENSE`) | `GPLv3.txt` |
| Qt 6 | GUI / network / audio framework | LGPL-3.0 (dynamic linking) | `qt6.txt` | `LGPLv3.txt` (+ `GPLv3.txt` by LGPL §4 reference) |
| FFTW3 | FFT library | GPL-2.0-or-later | `fftw3.txt`, `fftw3-notices.txt` | `GPLv2.txt` |
| WDSP | DSP engine | GPL-2.0-or-later | `wdsp.txt`, `wdsp-notices.txt` | `GPLv2.txt` |
| RADE (radae_nopy b2891023) | RADE digital voice modem, built as the rade shared library | BSD-2-Clause | `rade.txt`, `rade-notices.txt` | `rade.txt` |
| r8brain-free-src 5c44bebe | sample rate conversion for RADE transmit audio | MIT | `r8brain.txt`, `r8brain-notices.txt` | `r8brain.txt` |
| libspecbleach 0.2.0 (41d3f583) | NR4 noise reduction | LGPL-2.1-or-later | `libspecbleach.txt` | `LGPLv2.1.txt` |
| rnnoise 70f1d256 | NR3 noise reduction and its two bundled models | BSD-3-Clause | `rnnoise.txt`, `rnnoise-notices.txt` | `rnnoise.txt` |
| DeepFilterNet d375b2d8 | DFNR noise reduction library and its bundled model | Apache-2.0 OR MIT | `deepfilternet.txt` | `deepfilternet-apache.txt`, `deepfilternet-mit.txt` |
| PortAudio v19.7.0 | audio device input and output | MIT | `portaudio.txt` | `portaudio.txt` |
| nlohmann json 55f93686 | JSON parsing for libdatachannel | MIT | `nlohmann-json.txt` | `nlohmann-json.txt` |
| zlib v1.3.1 (Windows builds) | compression for stored equaliser settings | Zlib | `zlib.txt` | `zlib.txt` |
| libASPL v3.1.2 (macOS audio driver) | the NereusSDR VAX audio driver installed by the macOS package | MIT | `libaspl.txt` | `libaspl.txt` |
| libdatachannel 0.24.5 | direct DTLS/SCTP and SRTP media transport | MPL-2.0 | `libdatachannel.txt` | `MPLv2.txt` |
| libjuice | direct ICE backend for libdatachannel | MPL-2.0 | `libjuice.txt` | `MPLv2.txt` |
| plog | libdatachannel logging dependency | MIT | `plog.txt` | `plog.txt` |
| usrsctp | SCTP implementation for libdatachannel | BSD-3-Clause | `usrsctp.txt` | `usrsctp.txt` |
| libsrtp | SRTP implementation for libdatachannel | BSD-3-Clause | `libsrtp.txt` | `libsrtp.txt` |
| OpenSSL 3 | certificate, DTLS, and application cryptography | Apache-2.0 | `openssl.txt` | `Apache-2.0.txt` |
| Opus | audio codec (RADE on the desktop; receive audio and the microphone in the iPhone and iPad app) | BSD-3-Clause | `opus.txt` | `opus.txt` |

## Upstream projects whose code is ported into the binary

| Upstream | How used | Licence | Notice file |
| --- | --- | --- | --- |
| Thetis (ramdor/Thetis) | C# → C++/Qt6 port (logic, DSP integration, protocol, UI behaviour) | GPL-2.0-or-later | `thetis.txt` |
| mi0bot/Thetis-HL2 fork | HL2-specific discovery + capability ports | GPL-2.0-or-later | `mi0bot-thetis.txt` |
| AetherSDR (ten9876/AetherSDR) | Qt6 architectural template; ~45 derived files | GPL-3.0 | `aethersdr.txt` |

## Where each library comes from

Every directory under `third_party/` and every library the CMake files
fetch has a row here. `scripts/check-third-party-licenses.py` fails CI
when one is missing, when a named file is missing from this
directory, or when a text file here is named by no row.

| Library | Comes from | Pinned version | Ships in | Notice file |
| --- | --- | --- | --- | --- |
| Qt 6 | installed on the build machine; copied into packages by linuxdeploy, macdeployqt and windeployqt | the release workflow's Qt | desktop packages; the Core uses the system's Qt | `qt6.txt` |
| FFTW3 | `third_party/fftw3` (Windows headers and DLLs, fetched from fftw.org by the root CMakeLists.txt when missing); system package on Linux; Homebrew on macOS | 3.3.5 on Windows; the system or Homebrew package elsewhere | desktop packages and the Core | `fftw3.txt`, `fftw3-notices.txt` |
| WDSP | `third_party/wdsp` | TAPR v1.29 with NereusSDR changes | desktop packages and the Core | `wdsp.txt`, `wdsp-notices.txt` |
| RADE (radae_nopy) | `third_party/rade` | b2891023f3aecdf8b1793618000b1be6bcb2c4d1 | desktop packages and the Core | `rade.txt`, `rade-notices.txt` |
| Opus | ExternalProject `build_opus`, `build_opus_x86` and `build_opus_arm` in `third_party/rade/cmake/BuildOpus.cmake`, built into the rade library with its LPCNet and FARGAN parts | 940d4e5af64351ca8ba8390df3f555484c567fbb | desktop packages and the Core | `opus.txt`, `opus-notices.txt` |
| r8brain-free-src | `third_party/r8brain` | 5c44bebe9c477d47b1dc7037fcaae2794ff2b4e1 | desktop packages and the Core | `r8brain.txt`, `r8brain-notices.txt` |
| libspecbleach | `third_party/libspecbleach`, FetchContent `libspecbleach_upstream` | 41d3f58310391e05ecfb8b7c9efb62ea2ba8ef05 (v0.2.0) | desktop packages and the Core | `libspecbleach.txt` |
| rnnoise | `third_party/rnnoise`, FetchContent `rnnoise_upstream` | 70f1d256acd4b34a572f999a05c87bf00b67730d | desktop packages and the Core (models too) | `rnnoise.txt`, `rnnoise-notices.txt` |
| DeepFilterNet | `third_party/deepfilter`, built or downloaded by `setup-deepfilter.sh` and `setup-deepfilter.ps1` | d375b2d8309e0935d165700c91da9de862a99c31 | desktop packages, and the Core when its build has the library | `deepfilternet.txt` |
| PortAudio | FetchContent `portaudio` | v19.7.0 | desktop packages and the Core | `portaudio.txt` |
| zlib | FetchContent `zlib` on Windows; the system library elsewhere | v1.3.1 on Windows | Windows packages | `zlib.txt` |
| libdatachannel | FetchContent `nereus_libdatachannel` | v0.24.5 | desktop packages and the Core | `libdatachannel.txt` |
| libjuice | FetchContent `nereus_libjuice` | 3c40a3545b6b1b62c7adee7f8f2bd58aa290afd6 | desktop packages and the Core | `libjuice.txt` |
| plog | FetchContent `nereus_plog` | 94899e0b926ac1b0f4750bfbd495167b4a6ae9ef | desktop packages and the Core | `plog.txt` |
| usrsctp | FetchContent `nereus_usrsctp` | fec583d54493f879d2ae44a743423bf8a04371ab | desktop packages and the Core | `usrsctp.txt` |
| libsrtp | FetchContent `nereus_libsrtp` | 24b3bf8f19b6f5ab4cd2bcceb4f4064efca86fd5 | desktop packages and the Core | `libsrtp.txt` |
| nlohmann json | FetchContent `nereus_json` | 55f93686c01528224f448c19128836e7df245f72 | desktop packages and the Core | `nlohmann-json.txt` |
| OpenSSL 3 | vcpkg on Windows; Homebrew, or 3.0.21 built from source for Intel, on macOS; system package on Linux | the release workflow's OpenSSL | desktop packages and the Core | `openssl.txt` |
| libASPL | FetchContent `libASPL` in `hal-plugin/CMakeLists.txt` | v3.1.2 | the macOS audio driver package | `libaspl.txt` |

## Libraries a release step copies in

The deploy tools copy shared libraries the application needs from the
build machine. Only a built artifact shows the exact list. These rows
come from the release workflow's steps and each tool's documented
behaviour; confirm each on the next release build and give it a row
above, with its text, if it ships.

| Step | What it copies | Status |
| --- | --- | --- |
| linuxdeploy with its Qt plugin (Linux AppImage) | Qt libraries and plugins, and every other shared library the programs need that is not on linuxdeploy's exclude list, for example FFTW, OpenSSL, the PipeWire client library and the ICU libraries of the Qt install | confirm on the next release build |
| macdeployqt (macOS app) | Qt frameworks and plugins, and every other non-system library the app links, for example FFTW and OpenSSL, plus the libraries Homebrew's Qt depends on for the Apple Silicon build | confirm on the next release build |
| windeployqt (Windows) | Qt libraries and plugins | confirm on the next release build |
| release.yml copy steps (Windows) | `libfftw3-3.dll`, `libfftw3f-3.dll`, `deepfilter.dll` and `rade.dll`, all with rows above | listed above |

## Notices inside the compiled sources

Some libraries' source files carry copyright and licence notices that
their top-level licence text does not: other holders, other years or
other licences. Each `<library>-notices.txt` file lists those notices,
each distinct notice once and exactly as the source file writes it, with
the files it appears in. They cover the files NereusSDR compiles into its
programs, and hold upstream notices only: comments NereusSDR itself wrote
in those files (modification histories, port notes, the headers of
NereusSDR's own glue files) are left out. `scripts/collect-source-notices.py <library>` writes them;
rerun it after a vendor update. libspecbleach has no such file: its source
files carry only the notice `libspecbleach.txt` and `LGPLv2.1.txt` already
hold. The DeepFilterNet header NereusSDR compiles carries no notice.

DeepFilterNet's library is compiled from Rust crates, each under its own
licence. When `setup-deepfilter.sh` or `setup-deepfilter.ps1` builds the
library from source, it also writes `deepfilternet-crates.txt` here with
`scripts/collect-crate-notices.py`: every crate compiled into the library,
its version, its licence expression and its licence and notice files,
copied byte for byte.

## Opus model data

The RADE build of Opus downloads its LPCNet, FARGAN and other neural
network model data as `opus_data-4ed9445b96698bad25d852e912b41495ddfa30c8dbc8a55f9cde5826ed793453.tar.gz`
from media.xiph.org (`dnn/download_model.sh`, called by `autogen.sh` at
Opus 940d4e5a) and compiles the data files it holds into the library.
The archive holds model files and generated C sources, with no licence
file, and the generated C sources carry no notice. Neither
`dnn/download_model.sh` nor `dnn/download_model.bat` says anything about
a licence. The upstream statements nearest to the data are these.

Opus `README` at 940d4e5a:

      The Opus format and this implementation of it are subject to the royalty-
    free patent and copyright licenses specified in the file COPYING.

and, in its "Deep Learning and Opus" section:

    The license behind Opus or the intellectual property position of Opus does
    not change with Opus 1.5.

Opus `dnn/README.md` at 940d4e5a:

    The BSD licensed software is written in C and Python/Keras. For training, a GTX 1080 Ti or better is recommended.

Opus `dnn/torch/fargan/README.md` at 940d4e5a:

    Implementation of FARGAN, a low-complexity neural vocoder. Pre-trained models
    are provided as C code in the dnn/ directory with the corresponding model in
    dnn/models/ directory (name starts with fargan_). If you don't want to train
    a new FARGAN model, you can skip straight to the Inference section.

No upstream statement names a licence for the model data itself.

Per-file attribution for ported code lives in the source-file headers
and is indexed in `docs/attribution/THETIS-PROVENANCE.md`,
`docs/attribution/aethersdr-reconciliation.md`, and
`docs/attribution/WDSP-PROVENANCE.md` inside the NereusSDR source tree.

## Corresponding Source

A binary recipient's source-code rights under GPLv3 §6 / GPLv2 §3 are
detailed in `SOURCE-OFFER.txt`, which points at the public git tag
matching this binary and extends a three-year written source offer as
the §6(b) fallback.

## Combination of licences

- NereusSDR's own code: GPL-3.0.
- GPL-2.0-or-later dependencies (Thetis-derived ports, mi0bot-derived
  ports, WDSP, FFTW3): combined with NereusSDR under GPL-3.0 by
  exercising the "or later" grant (GPLv3 §5(b)).
- Qt 6 dynamic linking: permitted under LGPLv3 §4 when the combined
  work conveys (a) a prominent notice that the library is used and
  covered by LGPLv3 (this file), (b) a copy of the LGPLv3 text
  (`LGPLv3.txt`), and (c) a mechanism for the user to relink with a
  modified Qt (dynamic linking + the upstream Qt source pointer in
  `SOURCE-OFFER.txt` §3).
- libdatachannel and libjuice: MPL-2.0 covered files, without an applied
  Exhibit B incompatible-secondary-license notice in the pinned sources.
- plog, usrsctp and libsrtp: permissive MIT or BSD dependencies whose full
  notices are reproduced here.
- OpenSSL 3: Apache-2.0, compatible with this GPLv3 combined work.

## File inventory

- `README.md`           — this index
- `GPLv2.txt`           — full GNU General Public License, version 2
- `GPLv3.txt`           — full GNU General Public License, version 3
- `LGPLv3.txt`          — full GNU Lesser General Public License, version 3
- `MPLv2.txt`           — full Mozilla Public License, version 2.0
- `Apache-2.0.txt`      — full Apache License, version 2.0
- `qt6.txt`             — Qt 6 dependency notice
- `fftw3.txt`           — FFTW 3 dependency notice
- `wdsp.txt`            — WDSP dependency notice
- `libdatachannel.txt`  — libdatachannel dependency notice
- `libjuice.txt`        — libjuice dependency notice
- `plog.txt`            — plog full MIT notice
- `usrsctp.txt`         — usrsctp full BSD-3-Clause notice
- `libsrtp.txt`         — libsrtp full BSD-3-Clause notice
- `openssl.txt`         — OpenSSL dependency notice
- `thetis.txt`          — Thetis upstream-port notice
- `mi0bot-thetis.txt`   — mi0bot/Thetis-HL2 upstream-port notice
- `aethersdr.txt`       — AetherSDR upstream-port notice
- `SOURCE-OFFER.txt`    — written source offer + corresponding-source pointers
- `LGPLv2.1.txt`        full GNU Lesser General Public License, version 2.1
- `rade.txt`            RADE (radae_nopy) full BSD-2-Clause notice
- `opus.txt`            Opus full BSD-3-Clause notice
- `r8brain.txt`         r8brain-free-src full MIT notice
- `libspecbleach.txt`   libspecbleach dependency notice
- `rnnoise.txt`         rnnoise full BSD-3-Clause notice
- `deepfilternet.txt`   DeepFilterNet licence choice (Apache-2.0 or MIT)
- `deepfilternet-apache.txt` DeepFilterNet's copy of the Apache License 2.0
- `deepfilternet-mit.txt` DeepFilterNet full MIT notice
- `portaudio.txt`       PortAudio full MIT notice
- `nlohmann-json.txt`   nlohmann json full MIT notice
- `zlib.txt`            zlib full notice
- `libaspl.txt`         libASPL full MIT notice
- `fftw3-notices.txt`   notices in the FFTW header NereusSDR compiles
- `wdsp-notices.txt`    notices in the WDSP sources NereusSDR compiles
- `rade-notices.txt`    notices in the RADE sources NereusSDR compiles
- `opus-notices.txt`    notices in the Opus sources the RADE build compiles
- `r8brain-notices.txt` notices in the r8brain-free-src headers NereusSDR compiles
- `rnnoise-notices.txt` notices in the rnnoise sources NereusSDR compiles

This directory is installed to:

- Linux AppImage: `AppDir/usr/share/doc/nereussdr/licenses/`
- macOS DMG:      `NereusSDR.app/Contents/Resources/licenses/`
- Windows NSIS:   `$INSTDIR\licenses\`
- Windows ZIP:    `deploy\licenses\`
- Core (`cmake --install build --component nereusd`): `<prefix>/share/doc/nereussdr/licenses/`

…via `install(DIRECTORY packaging/third-party-licenses/ ...)` in the
root `CMakeLists.txt` and matching copy steps in
`.github/workflows/release.yml`.
