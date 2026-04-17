# NereusSDR Asset Provenance

Inventory of every graphical / binary asset shipped in NereusSDR.
Per-asset origin, author, license, and the date first added to the repo.

All assets listed here are GPL-2.0-or-later unless noted otherwise.

---

## resources/meters/

| File | Origin | Author | License | Added |
| --- | --- | --- | --- | --- |
| `ananMM.png` | Original NereusSDR artwork, hand-designed by J.J. Boyd (KG4VCF). 1360×768 PNG drawn from scratch for NereusSDR; no ANAN / Apache Labs / OE3IDE bitmap was copied, traced, or otherwise derived; no Ernst-style (Digital-7) typography is used. | J.J. Boyd (KG4VCF) | GPL-2.0-or-later | 2026-04-17 |
| `cross-needle.png` | Original NereusSDR artwork, hand-designed by J.J. Boyd (KG4VCF). 1104×928 PNG. Prior to the compliance remediation this file was a byte-identical placeholder copy of `ananMM.png` (commit `84f77e8`, 2026-04-11); the placeholder was never shipped in a release and has been replaced with wholly original artwork. No Thetis / OE3IDE skin image was copied or derived. | J.J. Boyd (KG4VCF) | GPL-2.0-or-later | 2026-04-17 |
| `cross-needle-bg.png` | Original NereusSDR artwork, hand-designed by J.J. Boyd (KG4VCF). 1104×928 PNG, same source as `cross-needle.png`; the two files exist so the rendering code's two-layer image path continues to function. | J.J. Boyd (KG4VCF) | GPL-2.0-or-later | 2026-04-17 |

### Provenance of earlier placeholders

Interim programmatic generators (`tools/generate-meter-face.py`,
`tools/generate-cross-needle.py`) were used during the compliance sweep
before the hand-designed artwork was final. They are retained in the
repository for history; the shipping artwork is the hand-designed
version authored by J.J. Boyd and listed above.

---

## resources/icons/

All NereusSDR brand icons are original artwork. They were created specifically
for the NereusSDR project and do not derive from any upstream SDR project's
branding.

| File | Origin | Author | License | Added |
| --- | --- | --- | --- | --- |
| `NereusSDR.png` | Original NereusSDR application icon | J.J. Boyd (KG4VCF) | GPL-2.0-or-later | 2026-04-13 (commit `c25b2f3`) |
| `NereusSDR.ico` | Windows ICO version of the same icon | J.J. Boyd (KG4VCF) | GPL-2.0-or-later | 2026-04-13 (commit `c25b2f3`) |
| `NereusSDR.icns` | macOS ICNS bundle version of the same icon | J.J. Boyd (KG4VCF) | GPL-2.0-or-later | 2026-04-13 (commit `c25b2f3`) |
| `NereusSDR.iconset/icon_16x16.png` | 16×16 variant | J.J. Boyd (KG4VCF) | GPL-2.0-or-later | 2026-04-13 |
| `NereusSDR.iconset/icon_16x16@2x.png` | 32×32 HiDPI variant | J.J. Boyd (KG4VCF) | GPL-2.0-or-later | 2026-04-13 |
| `NereusSDR.iconset/icon_32x32.png` | 32×32 variant | J.J. Boyd (KG4VCF) | GPL-2.0-or-later | 2026-04-13 |
| `NereusSDR.iconset/icon_32x32@2x.png` | 64×64 HiDPI variant | J.J. Boyd (KG4VCF) | GPL-2.0-or-later | 2026-04-13 |
| `NereusSDR.iconset/icon_128x128.png` | 128×128 variant | J.J. Boyd (KG4VCF) | GPL-2.0-or-later | 2026-04-13 |
| `NereusSDR.iconset/icon_128x128@2x.png` | 256×256 HiDPI variant | J.J. Boyd (KG4VCF) | GPL-2.0-or-later | 2026-04-13 |
| `NereusSDR.iconset/icon_256x256.png` | 256×256 variant | J.J. Boyd (KG4VCF) | GPL-2.0-or-later | 2026-04-13 |
| `NereusSDR.iconset/icon_256x256@2x.png` | 512×512 HiDPI variant | J.J. Boyd (KG4VCF) | GPL-2.0-or-later | 2026-04-13 |
| `NereusSDR.iconset/icon_512x512.png` | 512×512 variant | J.J. Boyd (KG4VCF) | GPL-2.0-or-later | 2026-04-13 |
| `NereusSDR.iconset/icon_512x512@2x.png` | 1024×1024 HiDPI variant | J.J. Boyd (KG4VCF) | GPL-2.0-or-later | 2026-04-13 |
| `NereusSDR.rc` | Windows resource script — references `NereusSDR.ico` for the app icon | J.J. Boyd (KG4VCF) | GPL-2.0-or-later | 2026-04-13 (commit `c25b2f3`) |

---

## resources/shaders/

All GLSL shaders are original NereusSDR artwork, written specifically for Qt6
QRhi-based spectrum, waterfall, and meter rendering. No shader source was
copied from Thetis (which uses GDI+/WinForms rendering) or any other project.

| File | Purpose | License |
| --- | --- | --- |
| `overlay.frag` | Fragment shader for the SpectrumWidget overlay layer (band edges, grid lines, VFO markers) | GPL-2.0-or-later |
| `overlay.vert` | Vertex shader for the overlay layer | GPL-2.0-or-later |
| `spectrum.frag` | Fragment shader for the spectrum trace (filled gradient trace + peak hold) | GPL-2.0-or-later |
| `spectrum.vert` | Vertex shader for spectrum trace geometry | GPL-2.0-or-later |
| `waterfall.frag` | Fragment shader for the waterfall display (color-maps dBm to palette) | GPL-2.0-or-later |
| `waterfall.vert` | Vertex shader for waterfall geometry | GPL-2.0-or-later |
| `meter_geometry.frag` | Fragment shader for MeterWidget vector-geometry pipeline (bar meters, needle arcs) | GPL-2.0-or-later |
| `meter_geometry.vert` | Vertex shader for meter geometry pipeline | GPL-2.0-or-later |
| `meter_textured.frag` | Fragment shader for MeterWidget textured pipeline (background images: ananMM, cross-needle) | GPL-2.0-or-later |
| `meter_textured.vert` | Vertex shader for meter textured pipeline | GPL-2.0-or-later |

---

## resources/bandplans/

Band-plan data files are derived from publicly published IARU and ARRL band-plan
documents. Frequency ranges and mode allocations are facts not subject to
copyright. The JSON arrangement, key naming, and encoding are original NereusSDR
work.

| File | Source data | License |
| --- | --- | --- |
| `iaru-region1.json` | IARU Region 1 band plan (https://www.iaru-r1.org/reference/band-plans/) | GPL-2.0-or-later (arrangement) |
| `iaru-region2.json` | IARU Region 2 band plan (https://www.iaru-r2.org/) | GPL-2.0-or-later (arrangement) |
| `iaru-region3.json` | IARU Region 3 band plan (https://www.iaru-r3.org/) | GPL-2.0-or-later (arrangement) |
| `arrl-us.json` | ARRL US Amateur Radio Band Plan (https://www.arrl.org/band-plan) | GPL-2.0-or-later (arrangement) |

---

## docs/images/

| File | Origin | Author | License | Added |
| --- | --- | --- | --- | --- |
| `nereussdr-v016-screenshot.jpg` | Screenshot of NereusSDR v0.1.6 running on macOS, captured by the maintainer | J.J. Boyd (KG4VCF) | GPL-2.0-or-later | 2026-04-16 (commit `45fc9a0`) |

---

## tests/fixtures/discovery/

See `tests/fixtures/discovery/README.md` for per-file capture provenance.

OpenHPSDR discovery reply packets contain only wire-protocol data (MAC
address, firmware version, radio model byte, in-use flag). These are
factual data values with no creative expression; they are not subject to
copyright. The arrangement and annotation of the hex fixtures are original
NereusSDR work (GPL-2.0-or-later).

| File | Radio / source |
| --- | --- |
| `p1_angelia_reply.hex` | Captured from a real Apache Labs ANAN-100D (Angelia board, firmware 21) running OpenHPSDR Protocol 1 |
| `p1_hermeslite_reply.hex` | Captured from a real Hermes Lite 2 (firmware 72) running OpenHPSDR Protocol 1 |
| `p2_saturn_reply.hex` | Hand-crafted from the OpenHPSDR Protocol 2 spec (no live ANAN-G2 capture available at time of authorship; see README for review note) |
