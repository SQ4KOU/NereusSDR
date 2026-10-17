# RADE v2 and native waveform experiment

This guide records the approved October 11 experiment originally prepared on
`codex/radev2-experiment`, based on main
`979d721e69c3d09992ce4c87696e44262f36d368`. Its accepted integration was
preserved at `e5f367759043f4876e2c2654854215ff6115f473` and reconciled with
frozen development r at `1748b7bd2a48514f1fb9078850eaa80049688d23`.

October 12 private publication preparation extracts only the accepted RADE
stack onto public main `1d99789ce8f0cbc7d3347f7d2e49a16d9cc4317e`, as
`codex/rade-publication-codec-modes` and the stacked
`codex/rade-publication-waveform-remote`. These source candidates need their
own composition build, canonical fixture regeneration and focused checks.
The verification and recovery records below describe the original experiment;
they do not establish runtime acceptance of the new public-main composition.

## Using the experiment

1. Select **RADE2-U** or **RADE2-L** for the explicit experimental codec. Existing **RADE-U/L** remain available for a V1 comparison. Mode IDs and saved/wire labels stay stable.
2. Use the ordinary container add-item workflow to add **Microphone waveform** and **Decoded speech waveform**. They can be duplicated, mixed with normal meters, arranged in Stack or Canvas, resized, and saved through the existing workspace.
3. Bind the microphone face to the current TX slice; bind decoded speech to the desired live RADE receiver. Other or unavailable sources show an explanation.
4. **Microphone stage** defaults to **Conditioned encoder input** at 16 kHz. **Raw microphone** observes the existing 48 kHz source and is useful for setup when the existing conditioned path has not started. Adding a face does not open a mic, key a transmitter, or start a second conditioner.
5. **Time span** selects five seconds, one second, or 100 milliseconds. Close views follow actual samples; wider views preserve positive/negative extrema per physical pixel column. The fixed ±1 scale and .2 divisions follow the FreeDV plot convention. Missing data breaks the trace; genuine zero input is valid silence.
6. A desktop connected to a Core built from this revision negotiates the optional exact-PCM observer feed. The existing listening/transmit permissions apply. An older Core is explicitly Unsupported; an unavailable clock/source or insufficient full-rate bandwidth gets an explicit reason. Duplicate views share demand, and hiding the final face retires capture.

## Reference basis and styling

FreeDV GUI was updated to `a8d84248837561b618141d0e82d072533d6df127`,
its backend to `f1976cf0ba68a52bec7b0a1e54b3e861baebac89`, and the C codec
reference to `c8a3dc156045cae2cd251e1a4be0c304c9ddf2f9`. All 34 vendored
codec files still match the verified upstream manifest. Existing platform
adaptations remain, with an idempotent two-patch Opus upgrade helper.

The new waveform faces use the semantic palette from workspace
PR #381, pinned at `1f249591dc27da7abd54b44408006c1ab87a5ec8`, and the
approved thin green trace. Original Nereus rendering uses the FreeDV plot/tap
facts rather than porting its wxWidgets code. Common container chrome and
properties remain existing components; the full workspace style framework
belongs to its independent PR.

## Audio audit result

The encoder receives the actual conditioned speech stream. The inherited
pre-encoder gain/peak tracker differs from current FreeDV loudness conditioning,
and gain/leveler/EQ controls can also process the modem waveform later in WDSP.
This experiment adds codec choice and truthful observation; it does not silently
change that DSP policy. See [the audio audit](2026-10-11-rade-audio-audit.md)
for exact sources and the proposed controlled capture/comparison.

The audit found no explicit whole-syllable replay in the receive adapters.
Propagation-damaged neural features, late decoded audio and remote Opus
concealment remain distinct possible causes. Ideal-channel speech tests do not
identify the reported on-air repetition. V2 streaming text/callsign reporting
is not integrated in this first codec experiment.

## Historical experiment verification

The final matching app/Core build exited 0. All **29/29 selected test executables
passed**, with zero failures in 239.03 seconds. This covers complete desktop and
Core media controllers, authenticated encrypted loopback, exact waveform codec,
source retirement, microphone validity, bounded queues/budgets/recovery, native
rendering, ordinary containers/persistence, output invariants, receive routing,
protocol guards and the real V1/V2 speech/version-transition wrapper. The two
pre-existing legacy TX pump skips remain; no new waveform skips or increased
timeouts were introduced. Exact logs are `work/waveform-logs/final-acceptance-build.log`
and `work/waveform-logs/final-acceptance-green.log`.

The consolidated independent review has resolved decoded receive-source
retirement, hidden-only refresh, and canonical protocol inventory findings.
No open finding remains at the frozen 63-file waveform package. Its exact source
hashes equal the tested-source manifest. Primary checkout HEAD/status/tracked changes match the preserved checkpoint. A final environment check found that the existing native preview test had an unsafe macOS home exemption and could clear/remove the real base settings file. That check found the base file absent; it has since been restored as approved. Its separate running `radxa_5c_r3` profile remains present. The test registration now gives every macOS test a private home; both affected native-window tests passed again (2/2, 13.35 s), with their actual resolved settings paths checked before execution; JJ approved restoration of the validated October 6 base backup, which is now complete and verified byte for byte. The restore wrote only the base file; no write to the separate running profile was performed. Its hash matched immediately before and after the atomic restoration. This restores the approved older backup; an exact copy of the original checkpoint was not located.

Source-review fingerprint: waveform-only package SHA256
`53ea9ac01084f29c732f91a18c85774560264ceeac533d920da7c42f9eb9622c`.
Review reports and exact command/output evidence:
`work/radev2-review.md`, `work/radev2-integration-report.md`,
`work/waveform-review.md`, `work/waveform-implementation-report.md`, and
`work/waveform-remote-implementation-report.md`.

Actual CPU/native Qt fixture images are in `work/waveform-shots/`; they use
offline PCM, not an on-air recording. The browser design prototype also remains
available. The broader proposed RADE status, SNR/spectrum and station-list
instruments remain design increments under the same ordinary-meter and
high-resolution contract. Native waveform verification is macOS arm64 plus
fake/loopback media and native QRhi; live-radio/live-Core and other-platform
runtime acceptance are not claimed. No normal application launch, physical
radio connection/keying, commit, push or merge was performed.

Recovery evidence: `work/waveform-settings-restore-result.json`. Restoration completed October 11 at 21:12:30 America/New_York after JJ's explicit approval; source code and verified test results are unchanged.
