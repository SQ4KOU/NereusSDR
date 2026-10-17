# Experimental RADE v2 in NereusSDR

JJ approved separate experimental RADE2-U/RADE2-L modes on 2026-10-11. V1 remains selectable. This is an opt-in experiment, with no claim of on-air readiness or resolution of syllable repetition.

## Scope and sources

Use the Python-free C codec from freedv/rade_c at c8a3dc156045cae2cd251e1a4be0c304c9ddf2f9. Its standalone v2 text test and real-speech software-channel round trip passed in work/radev2-lab. The latest freedv-gui master is a8d84248837561b618141d0e82d072533d6df127; its DSP backend main is f1976cf0ba68a52bec7b0a1e54b3e861baebac89. Reference checkout source is /Users/j.j.boyd/rade_c.

The C API selects V2 with RADE_MODE_V2, returns mode-dependent input/output counts, and returns zero EOO bits in V2 (src/rade_api.h:97-149, src/rade_api.c:150-181). It still requires a nonnull has_eoo pointer in rade_rx (src/rade_api.c:216-229). V2 uses a streaming data symbol channel rather than V1 EOO callsign bits. First integration supports speech and EOO; it does not advertise V1 callsign decoding/reporting in V2.

RadeChannel keeps its current per-slice decoder thread, main-thread encoder, shared codec lock, dynamic nin handling, epoch bridge and resamplers. Configure version before start; switching sideband or version tears down/recreates the channel using the existing slice lifecycle. The published filter window remains the current RADE passband (650..2350 Hz upper, mirrored lower); it includes V2 carriers around 1500 Hz (upstream src/rade_v2_ofdm.c:45-54). This deliberately avoids a new DSP tuning choice in the first experiment.

Append DSPMode values without renumbering the existing domain. Treat both versions identically for sideband mapping, TX dispatch, band rules, starvation unkey, remote mode validation, saved layouts, filter storage and RADE status visibility. Preserve all native CMake portability, dependency hash/model cache patches. Add upstream's opus-nnet.c.diff as well as nnet.h.diff; V2 decoder needs more convolution scratch space.

Use existing mode menus and controls. Display RADE2-U/RADE2-L with Experimental designation in menu text or tooltip; no custom visual theme. Any later instruments use PR #381's workspace palette and container registry. The browser mockup is a design artifact, not live functionality.

## Acceptance

- V1 works as before, including EOO callsign decoding.
- V2 speech encode/decode and EOO work through RadeChannel with real audio, finite nonzero decoded speech and acquired sync.
- Mode switching V1/V2 and upper/lower recreates the correct codec and preserves the ordinary RxChannel.
- RADE2 mode settings/remote enums round-trip; WDSP never sees native mode values; starvation policy treats RADE2 as RADE.
- V2 does not feed zero-sized EOO bits to the V1 text decoder or emit stale V1 callsigns.
- Codec pin, BSD notices and patch provenance are recorded. No source license headers or inline comments are lost.
- Only selected affected tests and application build run, per fast-test-loop. No radio connections or keying during verification.

## Audit findings carried forward

The current pre-encoder peak tracker targets -9 dBFS; current FreeDV uses momentary loudness AGC (-23 LUFS) plus limiter. Nereus mic gain/leveler also reach WDSP after modem injection; speech EQ and processing can alter modem output. These require a separate characterized speech/modem stage policy, not a silent parameter change in this experiment. Current V1 wrapper review found no deliberate syllable replay; propagation-damaged neural features, missed decoder slots and remote Opus concealment remain separate possibilities. New upstream V1 oscillator normalization fixes are part of the codec refresh, without claiming they fix the symptom.

## Verified local result

The desktop app and nereusd build on macOS arm64. All 24 selected native test executables pass across the initial 21-pass run and the corrected 3-pass rerun; two pre-existing obsolete TX pump cases remain skipped. Actual real-speech V1/V2 wrapper loopbacks and a pending-decode version transition passed. Four Opus patch upgrade cases pass. Final phone receive/status suite passes 12 tests; the affected Modes catalog suite passes 53 tests on iOS Simulator, including G2 and HL2 catalog consumers. Independent review accepts this opt-in experiment with no open correctness/spec finding. Exact commands, failures/corrections and limits are in work/radev2-integration-report.md and work/radev2-review.md. The subsequent approved waveform increment adds native microphone and decoded-speech items, including remote Core desktop support; see 2026-10-11-rade-waveform-meters-design.md. The DSP conditioning findings remain separate work.
