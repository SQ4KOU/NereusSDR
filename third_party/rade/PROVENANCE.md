# Vendored codec provenance

Updated 2026-10-11 by J.J. Boyd (KG4VCF), with OpenAI Codex assistance.
Integration notes added 2026-10-11 by J.J. Boyd (KG4VCF), with Anthropic
Claude Code assistance (see "NereusSDR changes kept on top of upstream").

The refreshed/new C and header files listed below are byte-for-byte copies
from freedv/rade_c commit `c8a3dc156045cae2cd251e1a4be0c304c9ddf2f9`, with
one exception: `src/rade_api.h` carries the NereusSDR export patch described
below.
Their upstream BSD notices, generated-model notices and inline comments are
preserved. The previous V1 files absent from that checkout remain in place;
`rade_api_nopy.c` is retained but the library now builds `rade_api.c`.
Existing `COPYING` and all wav fixtures remain unchanged. No GPL source was
translated for the native V2 selection adapter.

## Refreshed or added upstream files

- `src/opus-nnet.c.diff`
- `src/radae_rx.c`
- `src/radae_tx.c`
- `src/rade_api.c`
- `src/rade_api.h`
- `src/rade_bpf.c`
- `src/rade_dec_v2.c`
- `src/rade_dec_v2.h`
- `src/rade_dec_v2_data.c`
- `src/rade_dec_v2_data.h`
- `src/rade_dec_v2_test.c`
- `src/rade_enc_v2.c`
- `src/rade_enc_v2.h`
- `src/rade_enc_v2_data.c`
- `src/rade_enc_v2_data.h`
- `src/rade_enc_v2_test.c`
- `src/rade_ofdm.c`
- `src/rade_rx.c`
- `src/rade_rx_v2.c`
- `src/rade_rx_v2.h`
- `src/rade_rx_wav.c`
- `src/rade_sync.c`
- `src/rade_sync.h`
- `src/rade_sync_data.c`
- `src/rade_sync_data.h`
- `src/rade_tx_v2.c`
- `src/rade_tx_v2.h`
- `src/rade_tx_wav.c`
- `src/rade_v1_text_test.c`
- `src/rade_v2_constants.h`
- `src/rade_v2_core.h`
- `src/rade_v2_ofdm.c`
- `src/rade_v2_ofdm.h`
- `src/rade_v2_text_test.c`

## Recorded hashes

SHA-256 of each refreshed or added file as it sits in this folder. Every one
equals the file in freedv/rade_c at the pin, except `src/rade_api.h`, which
is listed twice: upstream's bytes and the vendored file with the export
patch. Checked 2026-10-12 against the hashes recorded when the files were
taken (34 files: 33 identical, `src/rade_api.h` differing on purpose).

| File | SHA-256 |
| --- | --- |
| `src/opus-nnet.c.diff` | `f8c8689fbfed55f636249aa302d2f17e375596e4a569d0f1f8df6d51fa04e18d` |
| `src/radae_rx.c` | `41608c12a3794330d3d1068dd03af2a7a9d11e0a49f0c19af129da747d0eb9a8` |
| `src/radae_tx.c` | `3bb1741619e6277947409ec941e60307a66a08b26168b4bf1e49229c8670e752` |
| `src/rade_api.c` | `a9e369a05371e65125d942bedf7dad482b021b7959ff8e79fbf7dfa0dbe5b6d5` |
| `src/rade_dec_v2.c` | `de6355c97a2694f453085bc2f55ee8381cb689cd858fb73382d89a781c336c2e` |
| `src/rade_dec_v2.h` | `fa3554ac400bc35730f8ad99a8f40ebf76c619c82593fd32945bc062b1225b8f` |
| `src/rade_dec_v2_data.c` | `f06f591c4fb5b3aa142acd54d79731bfc87abcc7bd73ad603098c4f9221149c5` |
| `src/rade_dec_v2_data.h` | `9c21e8b19d54c928b8a82b4371e23672596367e6757fa3e263aae29c286670d1` |
| `src/rade_dec_v2_test.c` | `db08a2e4f06aeee3aed96597c0d94b16ad1694ab9987287cacdf89dc1ff2b00c` |
| `src/rade_enc_v2.c` | `a23dbd7206f4ffff6a6c6d9267e23dde1ec3e0240e9ad458d222b8f644b89ee8` |
| `src/rade_enc_v2.h` | `ecc6ff9b93762ab5a43dcdce5cb2569253945283f384e22c4f0f9f6711643d00` |
| `src/rade_enc_v2_data.c` | `2b54ad912166f56b754044ef33021eb4da4679a1c752b9b2de9f8b023fd2c824` |
| `src/rade_enc_v2_data.h` | `dc178a5d94d89a9d4cacf328260fe0fa3db401e28a82e036ec6d827757911313` |
| `src/rade_enc_v2_test.c` | `fc972eb4240dbd9615a6964ce5aa8c2edb10deec67be37ae6bf0017d4ba709da` |
| `src/rade_rx_v2.c` | `a9530caf50e08b53cc4b870145e029bde7ced1407d611cc052fad8e91f5bef0d` |
| `src/rade_rx_v2.h` | `f99f887a694d51a7b0c260f6e6b19d43258b4578298d85597e3df1061abdb22d` |
| `src/rade_rx_wav.c` | `f68c53353b970b25b30814ae9c64f245d6e867eb111f679ee51b87f3102b0a30` |
| `src/rade_sync.c` | `64a73629c14411e82ad2a8f9e887da0716af072052a304f1a48875baa9ba203b` |
| `src/rade_sync.h` | `743c0df3f32070fd8280819ad81db96424f8dff50a789e86328fa802b7cedffd` |
| `src/rade_sync_data.c` | `f0cbf91142399ba74b531f84de388b8927e8edb1d0fb18c0860ab93db95e783b` |
| `src/rade_sync_data.h` | `95f937539610b0ff89682c2a8e4a80134905185f1dc876c6b8d3576642c04814` |
| `src/rade_tx_v2.c` | `aed21fc16bb14884c076911903e8e802afed63623f4a1838c3f9cd78a61fb7eb` |
| `src/rade_tx_v2.h` | `167c9ada37f4ae5762df67a6382a223b878028c8e1de6716801d679b0ef7862b` |
| `src/rade_tx_wav.c` | `6312e3daa82729d8bd74e64759d3a3c8f2038b6b28e4aba562d4262be0a76cac` |
| `src/rade_v1_text_test.c` | `199cc1310be21e911a932fb0965352ad408bb129d2c79ce39f079e4b8069f639` |
| `src/rade_v2_constants.h` | `dc89a6b6f3418d9a8e45cbef3f89678f0e044cf75451c5a4b938644503e90f2c` |
| `src/rade_v2_core.h` | `74d5dda96f68b56810c316b83cc25b0089d9fb4c3a837f57da2d6fa8bd31bb1d` |
| `src/rade_v2_ofdm.c` | `ae99a1e1ea9e9a3ffb8c5266d073e2830541dae8c68ce5510312c7cb216bfd82` |
| `src/rade_v2_ofdm.h` | `2fdefcc0e3d1889978fec7fb8202f860e7ba2e7e2f1cba16ae15e4eeec388c89` |
| `src/rade_v2_text_test.c` | `1689e3e6cdc0ba5c946519585bfbb0b9bf3b514f8b926fa6b77df0e18b769f6c` |
| `src/rade_api.h` (upstream bytes) | `584b7041c6d478ad6b240da5a72534087ede88fb0f8fcb401b6986ffec10c5c3` |
| `src/rade_api.h` (as vendored, with the export patch) | `96fce39face5ebc8c4ac686f15e24cfc729a4c3c2b422d63a3edb89f3cdb8d68` |
| `src/rade_bpf.c` | `4c015e3f847d3cfd71b7e4fb7e2e8c4047a48d86ebdd8b796b06c287cff0480f` |
| `src/rade_ofdm.c` | `8e7ac144f301f8f760964279794856ef44ef2125b75333392cb9db4eaed7012b` |
| `src/rade_rx.c` | `4759c931d47305ca8778619e1d2f45089094b0890f17f393da9e0ed2214495c5` |

The second Opus patch is NereusSDR's own and has no upstream copy:

| File | SHA-256 |
| --- | --- |
| `src/opus-nnet.h.diff` | `668d3b0a8e0e472ac3301d4fb18f9408ec6502e8a11d5c7afc584805d4389dfd` |

## Nereus build adaptations retained

- Keep existing `rade` shared and imported `opus` targets, native MSVC Opus
  CMake build, non-Windows libm gating, Ninja make/byproducts fixes, macOS
  architecture handling, verified archive hash and model stash behavior.
- Keep Nereus's existing `opus-nnet.h.diff`: it exports the upstream neural
  functions and preserves the `_MSC_VER` guard absent in upstream's patch.
- Copy upstream `opus-nnet.c.diff` verbatim: raise convolution scratch from
  1536 to 2048 floats for V2 decoder's 1792 inputs. Apply both patches in all
  five ExternalProject build paths, including Windows and universal macOS.
  Native `cmake/NereusOpusPatches.cmake` verifies pinned source pre/postimages
  and applies only missing patches, supporting upgrades from a V1 build
  without reapplying its header patch. Unexpected source is rejected before
  either file changes.
- Add V2 source lists and current standalone WAV/text tools while preserving
  existing V1 convenience tool targets and files (except the
  `rade_callsign_test` target, see below).

## NereusSDR changes kept on top of upstream

Checked 2026-10-11 against a freedv/rade_c checkout at the pin: 61 files in
`src/` are byte-for-byte upstream, 3 differ on purpose, and 6 are older
radae_nopy files that rade_c does not have.

- `src/rade_api.h`: upstream bytes except the `RADE_EXPORT` block. The
  NereusSDR patch from 2026-05-14 (commit `bbab84aa3`) is kept: `__stdcall`
  is dropped from the two Windows definitions and the comment that explains
  it stays above the block. Upstream's `__declspec(...) __stdcall` before the
  return type is MSVC error C2059, and the Windows release package is built
  with MSVC. Nothing else in the file differs from upstream.
- `src/CMakeLists.txt`: NereusSDR's own build file (the adaptations above).
  The `rade_callsign_test` tool is no longer built: it calls
  `rade_tx_set_eoo_callsign` and `rade_rx_get_eoo_callsign`, which radae_nopy
  had and rade_c at the pin does not. Its source file stays in place.
- `src/opus-nnet.h.diff`: NereusSDR's MSVC-safe version, as described above.
- Kept from radae_nopy `b2891023f3aecdf8b1793618000b1be6bcb2c4d1`, absent
  from rade_c: `src/radae_rx_nopy.c`, `src/radae_tx_nopy.c`,
  `src/rade_api_nopy.c`, `src/rade_callsign_test.c`, `src/rade_demod_wav.c`,
  `src/rade_modulate_wav.c`. None is compiled into the `rade` library.

The notices NereusSDR ships for the compiled sources are generated from
this folder: `python3 scripts/collect-source-notices.py rade` writes
`packaging/third-party-licenses/rade-notices.txt`. Rerun it whenever the
source list in `src/CMakeLists.txt` changes.

API facts used by native adapter: `src/rade_api.h:97-181` (mode flags,
dimensions, V2 EOO without V1 bits); `src/rade_api.c:150-181,216-229`
(dynamic getters and nonnull `has_eoo_out` required by implementation).
