# Infrastructure Live-Apply — v0.3.0

Manual verification checklist for the Phase 1 cross-thread DSP coordinator
introduced in Task 1.x of the v0.3.0 parity audit. These are behavioral checks
that cannot be verified by static screenshot — they require a connected radio
and active audio path.

## Prerequisites

- Radio connected and I/Q flowing (verified by spectrum activity).
- Audio output audible (headphones or speakers).
- Application log visible in a terminal (`./build/NereusSDR.app/Contents/MacOS/NereusSDR 2>&1 | tee /tmp/nereus.log`).

---

## Checks

| # | Check | Procedure | Pass criterion | Failure mode |
|---|---|---|---|---|
| LA-1 | Sample-rate live-apply | With radio connected: open Setup → Hardware, change sample rate (e.g. 48k → 96k). Repeat 96k → 192k → 384k while listening. | Audio resumes within 1 second at the new rate; spectrum bandwidth visibly widens; no extended silence or crash | Audio silent >5 s, or application crashes, or sample rate does not persist across reconnect |
| LA-2 | Active-RX-count live-apply | On a multi-DDC radio (e.g. ANAN-G2): toggle RX2 on and off from Setup → Hardware while RX1 audio is playing. | RX1 audio is uninterrupted; RX2 receiver starts/stops cleanly; no crash | RX1 audio dropout >200 ms, or crash, or DDC count change hangs |
| LA-3 | Channel rebuild glitch budget | Change buffer size from 256 → 512 in DSP Options while listening to a signal. Measure (by ear or log timestamp) the audio gap. | Rebuild completes with audio dropout < 200 ms | Dropout > 200 ms, or audio does not resume |
| LA-4 | AudioEngine re-init on buffer-size change | Change buffer size from 256 → 1024 in DSP Options. Verify audio output continues after the rebuild. | Audio plays correctly at new buffer size; no persistent silence or QAudioSink error in log | Audio silent after rebuild; log shows QAudioSink error or incorrect sample count |
| LA-5 | DspChangeTimer signal at DspOptionsPage | With DSP Options page open: change any buffer/filter combo. Observe the "Time to last change" readout. | Readout shows "0:00:00" (or near-zero) immediately after the change, then increments | Readout stuck at zero; readout does not update; page does not exist |
| LA-6 | Settings migration runs once on v0.3.0 first launch | Delete (or rename) the settings file to simulate a first launch, then launch. Check `SettingsSchemaVersion` key and log output. | Log contains "Migrating settings to schema v3", `SettingsSchemaVersion=3` is written, retired keys (`DisplayAverageMode`, `DisplayPeakHold`, `DisplayPeakHoldDelayMs`, `DisplayReverseWaterfallScroll`) are absent from the new settings file | Log shows no migration message; schema version not written; retired keys still present |

## Manual verification notes

- LA-6: To inspect the settings file location, launch the app and note the path
  from the log, or look in `~/.config/NereusSDR/NereusSDR.settings`.
- LA-3 glitch budget target of < 200 ms comes from the design goal in
  `docs/architecture/thetis-display-dsp-parity-design.md` §Phase 1.
- LA-1 on macOS: CoreAudio device reconfiguration may add ~100 ms beyond the
  WDSP rebuild itself; total < 1 s is the pass threshold.
- For LA-2 the multi-DDC requirement means an ANAN-G2 or ANAN-8000DLE;
  skip on Hermes Lite 2 (single DDC) and mark "N/A — hardware limitation".
