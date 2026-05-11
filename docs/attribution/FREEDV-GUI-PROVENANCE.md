# freedv-gui Provenance - NereusSDR derived-file inventory

This document catalogs every NereusSDR source file derived from, translated
from, or materially based on freedv-gui (drowe67/freedv-gui). Per-file license
headers live in the source files themselves; this index is the grep-able
summary.

NereusSDR is distributed under GPLv3 (root `LICENSE`). freedv-gui is LGPLv2.1+
with a BSD-2-Clause carve-out for `src/integrations/`. See §License below.

## When entries get added

A row is added to the table below - in the **same commit** that introduces
the ported logic - whenever a NereusSDR file:

1. Ports, translates, or materially re-expresses logic from any freedv-gui
   `.c`, `.cpp`, or `.h` file under `src/`, AND
2. That logic is not already covered by the Thetis / mi0bot / WDSP /
   AetherSDR / deskhpsdr lineage (i.e. freedv-gui is the *primary* source
   for that logic, not a cross-reference).

The procedure is identical to `THETIS-PROVENANCE.md` and
`DESKHPSDR-PROVENANCE.md`:

- Add the verbatim freedv-gui file header to the NereusSDR file (per
  `HOW-TO-PORT.md` §"Byte-for-byte headers and multi-file attribution").
- Add a `// From freedv-gui <path>:<line> [v<tag>|@<sha>]` inline cite at
  every ported function / constant (per `HOW-TO-PORT.md` §"Inline cite
  versioning").
- Add a PROVENANCE row here with the NereusSDR file, freedv-gui source,
  line ranges, derivation type, and notes.

## Upstream

- **Project:** FreeDV / freedv-gui
- **Repository:** https://github.com/drowe67/freedv-gui
- **Lineage:** founded 2012 by David Rowe (VK5DGR) and David Witten;
  primary maintainer Mooneer Salem (K6AQ); FreeDV Reporter, RADE, and PSK
  Reporter clients largely Mooneer's contributions.
- **Initial corpus reference SHA:** `@77e793a` (HEAD at time of first
  freedv-gui port plumbing, 2026-05-10)
- **Languages:** C++ (`.cpp` / `.h`), with some C in pipeline (`rade_text.c`)
  and bundled 3rd-party (yyjson / WebRTC_AGC / r8brain / websocketpp).

## License

freedv-gui is distributed under the **GNU Lesser General Public License
v2.1 or later** (LGPLv2.1+), per the project root `COPYING`. A BSD-2-Clause
carve-out applies to the radio-integration code under
`src/integrations/` (see `src/integrations/LICENSE`). Bundled third-party
libraries under `src/3rdparty/` (websocketpp, yyjson, WebRTC_AGC, r8brain)
each carry their own GPL-compatible license.

| Subtree | License |
| --- | --- |
| project root + most `src/**` | LGPLv2.1+ |
| `src/integrations/` | BSD-2-Clause (FreeDV Project, 2026) |
| `src/3rdparty/websocketpp/` | BSD-3 (Peter Thorson) |
| `src/3rdparty/yyjson/` | MIT (ibireme) |
| `src/3rdparty/WebRTC_AGC/` | BSD (Google / WebRTC project) |
| `src/3rdparty/r8brain/` | MIT (Aleksey Vaneev) |

NereusSDR is GPLv3. LGPLv2.1+ is **upgrade-compatible to GPLv3** when
linked into a GPLv3 work (LGPL §3 conversion clause). The BSD and MIT
carve-outs are GPL-compatible by their own terms. No dual-licensing
(Samphire-style or otherwise) exists in the freedv-gui source tree.

LGPL-relink obligations for freedv-gui code that NereusSDR statically
links will be documented in `docs/attribution/LGPL-COMPLIANCE.md` once
the first port lands (mirrors the libspecbleach pattern).

## Verifier and corpus tooling

- `scripts/verify-freedv-headers.py` runs in the pre-commit hook chain
  (`scripts/git-hooks/pre-commit`) and in CI. For every PROVENANCE row
  below, it verifies the NereusSDR file's first ~160 lines contain the
  required upstream-attribution markers.
- `scripts/discover-freedv-author-tags.py` walks `../freedv-gui/src/**`
  and refreshes `docs/attribution/freedv-gui-author-tags.json`. Re-run
  after every upstream sync (`git -C ../freedv-gui pull`).
- The PROVENANCE row + verbatim header + inline cites are what the
  pre-commit hook gates on. Skip any of the three and the commit is
  blocked.

## Inline cite format

- Tagged release: `// From freedv-gui src/path/file.cpp:N [v1.9.10]`
- Between releases: `// From freedv-gui src/path/file.cpp:N [@77e793a]`

(freedv-gui at `@77e793a` had no `git describe`-discoverable tag at the
time of A1 vendoring, so SHA stamps are the practical default until the
upstream tags a release we sync to.)

## Legend

Derivation type:
- `port`       - direct reimplementation in C++/Qt6 of a freedv-gui source file
- `reference`  - consulted for behavior during independent implementation
- `structural` - architectural template with substantive behavioral echo
- `wrapper`    - thin C++ wrapper around vendored C source

## Files derived from drowe67/freedv-gui

| NereusSDR file | freedv-gui source | Line ranges | Type | Notes |
| --- | --- | --- | --- | --- |
| (none yet - registry seeded by Phase 3J-2 + 3R port tasks) | | | | |

## Independently implemented - freedv-gui-like but not derived

Files whose behavior resembles freedv-gui but whose implementation was
written without consulting freedv-gui source. No per-file freedv-gui
header required. These rows are intentionally formatted with the file
path in column 2 (not column 1) so the header-verifier script does not
scan them.

| Behavioral resemblance | NereusSDR file | Basis of implementation |
| --- | --- | --- |
| (none yet) | | |
