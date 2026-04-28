# Phase 3M-0 PA Safety Foundation — Verification Matrix

Manual verification for the 3M-0 safety net. Run on hardware before merging
the 3M-0 PR. Recommended bench: **ANAN-G2 + Hermes-Lite 2 + Atlas (mic-jack-only)**
to cover the three SKU classes the safety net targets.

The 3M-0 controllers are inert until 3M-1a fires the first MOX. Most rows
will only show observable effects after 3M-1a lands; this matrix is the
acceptance gate for both phases. **Until 3M-1a, rows that need a real
TX go unchecked here and are tagged `[3M-1a-bench]`.**

| # | Test | Hardware | Procedure | Expected | Result |
|---|---|---|---|---|---|
| 1 | Synthetic SWR foldback unit test | none | `cd build && ctest -R '^tst_swr_protection_controller$' -V` | All 9 SWR slots PASS (per-sample foldback factor ~= limit/(swr+1) at 4 trips with windback off) | |
| 2 | Latched windback after 4 trip samples `[3M-1a-bench]` | ANAN-G2 + 50Ω dummy load with mismatch jig | TX SSB at full power into a deliberate mismatch (~3:1 SWR). Hold key for >100 ms. Watch the PA Power gauge. | After 4 consecutive 100ms samples above limit, drive multiplier drops to 0.01 (gauge collapses to ~1% of drive). Latch persists until MOX-off. | |
| 3 | Open-antenna detection `[3M-1a-bench]` | ANAN-G2, no antenna connected | Key TX at 15W carrier with antenna disconnected. | `openAntennaDetected()` asserts within 1 sample. Drive collapses to 0.01. HighSwr overlay appears. | |
| 4 | TX-inhibit user-IO pin `[3M-1a-bench]` | ANAN-G2, jumper connected to UserIO 1 | With "Update with TX Inhibit state" enabled, ground UserIO 1 (or +3.3V if reversed). | `txInhibitedChanged(true, UserIo01)` fires within 100ms. MOX rejected. Status-bar "TX INHIBIT" pill becomes visible. | |
| 5 | Watchdog enable `[3M-1a-bench]` | ANAN-G2 over network | With Network Watchdog checked (default), TX a continuous carrier, then physically pull the network cable. | Radio LED stops MOX within 1s of suppressed C&C (radio-side firmware behaviour). Note: in 3M-0, `setWatchdogEnabled(bool)` is a stub — the wire bit emit is deferred to 3M-1a. This row may not pass until 3M-1a wires the actual ChannelMaster.dll-equivalent bit position. | |
| 6 | BandPlanGuard 60m channelization | none | `cd build && ctest -R '^tst_band_plan_guard$' -V` | All 18 slots PASS. UK 11 channels + US 5 channels + JA 4.63 MHz + Europe/Australia/Spain band ranges + extended bypass + per-band rxBand/txBand prevent. | |
| 7 | Extended toggle bypass `[3M-1a-bench]` | ANAN-G2 | Setup → General → Hardware Configuration → check "Extended". Try TX at 13.5 MHz (out-of-band). | TX allowed (extended bypass per console.cs:6772). Uncheck Extended; same TX is rejected. | |
| 8 | PreventTxOnDifferentBand guard `[3M-1a-bench]` | ANAN-G2 with 2 receivers | Setup → General → Options → check "Prevent TX'ing on a different band to the RX band". RX1 on 20m, attempt to TX on 40m (or via VFO B on a different band). | MOX rejected. Status-bar TX INHIBIT visible (source: OutOfBand). | |
| 9 | Per-board PA scaling vs watt meter `[3M-1a-bench]` | ANAN-G2 + 100W dummy load + RF watt meter | Dial up 50W carrier on 14.200 MHz, read PA Power gauge against bench watt meter. Repeat at 100W. Compare to `tst_pa_scaling.cpp` reference values for ANAN_G2 (bridgeVolt=0.12, refVoltage=5.0, offset=32). | Gauge readings agree with bench meter within 5% across 25-100W range. | |
| 10 | HighSWR static overlay paints `[3M-1a-bench]` | any radio with TX, plus a deliberate mismatch | Force a high-SWR condition during TX (per row 2 or 3). | "HIGH SWR" red bold text appears in upper-center of spectrum. 6 px red border draws around the spectrum. With foldback active, "POWER FOLD BACK" appears under "HIGH SWR". | |
| 11 | RX-only SKU hard-blocks MOX entry | Hermes-Lite 2 (RX-only kit, if available) OR ANAN-G2 with chkGeneralRXOnly toggled | Setup → General → Hardware Configuration → check "Receive Only" (or use an HL2 RX-only kit so `BoardCapabilities::isRxOnlySku()` is true). Try MOX. | MOX rejected. Status-bar TX INHIBIT visible (source: Rx2OnlyRadio). | |
| 12 | PA-trip clears MOX automatically on Andromeda console | Andromeda + Ganymede 500W amp (skip on non-Andromeda) | While transmitting, induce a Ganymede CAT trip message (e.g. drive over rated, or mock by sending a CAT TripState=1 frame). | `RadioModel::paTripped()` asserts. Status-bar PA Status badge flips to "PA FAULT" (red). MOX drops automatically (`m_transmitModel.setMox(false)` per Andromeda.cs:920). | |
| 13 | Per-MAC persistence round-trip | any | Open Setup → Transmit + General. Toggle every 3M-0 control. Quit the app. Reopen. | All 15 AppSettings keys preserved exactly as set. (Automated coverage: `tst_phase3m0_persistence_audit`.) | |

## Regressions to watch

- TX path itself doesn't exist yet (3M-1a is next), so there should be **zero observable change** to the existing RX path: spectrum, waterfall, audio, VFO tuning, AGC, NR, NB, AGC, mode switching, band switching, multi-RX, FFT, panadapter — all unchanged.
- Existing setup pages (Display, Audio, CAT, Calibration, Antenna Control, etc.) — no controls changed, no defaults shifted.
- ADC overload, step attenuator, AlexController, BandStackManager — none touched by 3M-0.
- Existing 130-test suite from pre-3M-0 baseline must still pass cleanly (verified by full ctest run).

## Upstream cite traceability

Every safety controller carries verbatim Thetis cites with `[v2.10.3.13]` stamps:

- **SwrProtectionController** ports `console.cs:25933-26120` (PollPAPWR loop), `25972-25978` (SWR formula), `25989-26009` (open-antenna heuristic, `[v2.10.3.6]MW0LGE`), `26020-26057` (tune-time bypass), `26069-26075` (trip detection), `29191-29195` (UIMOXChangedFalse reset).
- **TxInhibitMonitor** ports `console.cs:25801-25839` (PollTXInhibit loop) with `//DH1KLM` per-board pin tag preserved verbatim.
- **BandPlanGuard** ports `console.cs:6770-6816` (CheckValidTXFreq), `console.cs:2643-2669` + `clsBandStackManager.cs:1467-1480` (60m channels), `clsBandStackManager.cs:1063-1083` (IsOKToTX broad-range gate, called out as a deliberate NereusSDR deviation in source comments — channelized US 60m gate is more restrictive per FCC Part 97.303(h)).
- **safety_constants** ports `console.cs:25008-25072` (computeAlexFwdPower per-board switch, including `//DH1KLM` REDPITAYA tag).
- **RadioModel::handleGanymedeTrip** ports `Andromeda.cs:855-866` + `Andromeda.cs:914-948` (CATHandleAmplifierTripMessage), with `//G8NJJ` tag preserved.
- **setWatchdogEnabled** is a stub for `NetworkIOImports.cs:197-198` (DllImport into closed-source ChannelMaster.dll); wire bit position deferred to 3M-1a per row 5.

## Commit sequence

3M-0 landed across the following commits on `feature/phase3m-0-pa-safety`:

| Task | Commits | Summary |
|---|---|---|
| 1 | `4fb4a79` + `46f246b` | BoardCapabilities — `isRxOnlySku()` + `canDriveGanymede()` |
| 2 | `2c414f8` + `fa7a001` + `bdd86e6` | BandPlanGuard + 60m channelization (3 commits incl. attribution + review fixups) |
| 3 | `921f25e` + `59443e8` + `5dfe9c5` + `33f8755` (partial) | SwrProtectionController two-stage foldback + windback latch |
| 4 | `6927f45` + `e357aab` + `33f8755` (partial) | TxInhibitMonitor 100 ms poll + 4-source dispatch |
| 5 | `ecfe93d` | `setWatchdogEnabled(bool)` stub |
| 6 | `eb37327` | `RadioModel::paTripped()` + Ganymede trip handler |
| 7 | `29fd3d8` | Per-board PA scaling + MeterPoller routing |
| 8 | `637fb20` | HighSwr static overlay |
| 9 | `48e8e35` | Setup → Transmit → SWR Protection group |
| 10 | `2cce850` | Setup → Transmit → External TX Inhibit group |
| 11 | `30c1411` | Setup → Transmit → Block-TX antennas + Disable HF PA |
| 12 | `19fcbe4` | Setup → General → Hardware Configuration |
| 13 | `4b02a6e` | Setup → General → Options → prevent-different-band |
| 14 | `fa4f9aa` | Status bar TX Inhibit + PA Status badge widgets |
| 15 | `8c2bf41` | Persistence audit (15 keys round-trip) |
| 16 | (this doc) | Verification matrix |
| 17 | (TBD) | Final integration — RadioModel owns safety controllers |

## Notes

- **Bench rows tagged `[3M-1a-bench]` are not blocking 3M-0 merge.** They form the acceptance criteria for the combined 3M-0 + 3M-1a release.
- **All non-bench rows (1, 6, 13) are automated** and gate the 3M-0 PR via CI's `ctest` step.
- **Row 5 (watchdog wire bit)** is a known stub; the wire format identification (capture or HL2 firmware analysis) is a planned 3M-1a task. **Resolved in 3M-1a Task E.5**: P1 RUNSTOP `pkt[3]` bit 7 (HL2 fw `dsopenhpsdr1.v:399-400 [@7472bd1]`); P2 deferred per pre-code §7.8.
- **Row 12 (Andromeda PA-trip)** requires Andromeda hardware + Ganymede PA; if no such hardware is available at bench time, the unit-test coverage in `tst_radio_model_pa_tripped::ganymedeTripMessage_dropsMoxIfActive` substitutes for hardware verification of the MOX-drop side effect.

---

# Phase 3M-1a TUNE-only First RF — Verification Matrix Extension

3M-1a builds on the 3M-0 safety net to fire the first MOX wire-bit on real
hardware. This section adds rows specific to the TUNE path (the only TX-on
path enabled in 3M-1a) plus the wire-byte snapshots verified in unit tests.

Recommended bench: **HL2 first** (lower-stakes smoke, RUNSTOP watchdog wire bit
verifiable, no PA chain), then **ANAN-G2** (Saturn FPGA, full P2 wire path,
includes external watt meter for power calibration).

| # | Test | Hardware | Procedure | Expected | Result |
|---|---|---|---|---|---|
| 14 | P1 MOX wire-byte snapshot | none | `cd build && ctest -R '^tst_p1_mox_wire$' -V` | All 13 cases PASS — C0 byte 3 bit 0 (0x01) emits `1` on MOX-on, `0` on MOX-off; bank-0 force-flush within ≤1 frame; HL2 firmware cross-check. | |
| 15 | P1 T/R relay wire-byte snapshot | none | `cd build && ctest -R '^tst_p1_trx_relay_wire$' -V` | All 14 cases PASS — bank 10 C3 bit 7 inverted: `1` = relay disabled (PA bypass), `0` = engaged. Codec path + legacy path both verified. | |
| 16 | P1 TX I/Q frame layout | none | `cd build && ctest -R '^tst_p1_tx_iq_wire$' -V` | All cases PASS — 16-bit signed BE samples in EP2 zones; HL2 CWX LSB-clear workaround active; SPSC ring saturation behaviour. | |
| 17 | P1 watchdog RUNSTOP byte | none | `cd build && ctest -R '^tst_p1_watchdog_wire$' -V` | All 12 cases PASS — `pkt[3]` bit 7 (RUNSTOP byte 1 bit 7); inverted (`1` = disabled, default-on writes `0`); both sendMetisStart and sendMetisStop carry the bit. | |
| 18 | P2 MOX wire-byte snapshot | none | `cd build && ctest -R '^tst_p2_mox_wire$' -V` | All 10 cases PASS — high-priority byte 4 bit 1 (0x02) for MOX; codec path (Saturn / OrionMkII) + legacy path; latent `pttOut→m_mox` bug fixed. | |
| 19 | P2 drive level wire-byte | none | `cd build && ctest -R '^tst_p2_drive_level_wire$' -V` | All 10 cases PASS — high-priority byte 345 carries `m_tx[0].driveLevel & 0xFF`; clamped [0,100]; out-of-band gate zeros byte 345 for GEN/WWV bands. | |
| 20 | P2 TX I/Q frame layout | none | `cd build && ctest -R '^tst_p2_tx_iq_wire$' -V` | All 14 cases PASS — 1444-byte frame (4-byte BE seq + 1440-byte payload), 240 samples, 24-bit signed BE I + Q (6 B/sample), float→int24 gain 8388607.0, port 1029. | |
| 21 | MoxController state machine | none | `cd build && ctest -R '^tst_mox_controller_' -V` | All 4 test files (basic, timers, phase signals, tune) PASS. State machine + 6 QTimer chains + 6 phase signals fire in correct order. | |
| 22 | RadioModel hardware-flip fan-out | none | `cd build && ctest -R '^tst_radio_model_mox_hardware_flip$' -V` | All 8 cases PASS — `onMoxHardwareFlipped` slot calls Alex routing → setMox → setTrxRelay in Thetis-§2.3 order. | |
| 23 | RadioModel TUN orchestrator | none | `cd build && ctest -R '^tst_radio_model_set_tune$' -V` | All 18 cases PASS — `setTune(true/false)` with CW→LSB/USB swap, tune-tone sign select, tune-power push, MoxController engage; cold-off guard prevents stale state restore. | |
| 24 | TxChannel WDSP wiring | none | `cd build && ctest -R '^tst_tx_channel_' -V` | All 4 test files (pipeline, tune-tone, running, wdsp-engine) PASS. 31-stage TXA pipeline; gen1 PostGen tone setup; channel ID 1. | |
| 25 | StepAttenuatorController TX-path | none | `cd build && ctest -R '^tst_step_att_tx_path$' -V` | All 12 cases PASS — `applyTxAttenuationForBand`, `shouldForce31Db` (PS-off OR CWL/CWU), HPSDR `saveRxPreampMode` / `restoreRxPreampMode`. | |
| 26 | TUN engages MOX (HL2 bench) `[bench]` | HL2 + 50Ω dummy load | Tune-Power slider at 25W (low). Press TUNE button. | TUNE button turns red. MOX status badge active. Spectrum overlay shows TX-mode tint. RUNSTOP packet on wire shows bit 7 = 0 (watchdog enabled). After release, TUNE button clears, MOX drops, watchdog state stable. | |
| 27 | TUN tone audible on HL2 `[bench]` | HL2 + dummy load + 100W watt meter | Slice in USB at 14.200 MHz. Tune-Power 25W. Press TUNE. | Watt meter shows ~25W carrier. Tone is at +cw_pitch (600 Hz USB → carrier at 14.200600 MHz). Drive level on dashboard = 25. | |
| 28 | CW→LSB swap during TUN `[bench]` | HL2 | Slice in CWL at 7.050 MHz. Press TUNE. | Slice mode briefly switches to LSB during TUN. Tone at -cw_pitch (600 Hz LSB → carrier at 7.049400 MHz). Release TUN: slice restores to CWL. | |
| 29 | Per-band tune-power persistence `[bench]` | HL2 | Set 20m tune power = 30W. Set 40m tune power = 15W. TUN on 20m (verify 30W). Switch to 40m, TUN (verify 15W). Quit + relaunch app. | Tune-Power slider on each band reads back the saved value after relaunch. | |
| 30 | ATT-on-TX activates on TUN `[bench]` | HL2 with chkATTOnTX checked | Setup → Transmit → check "ATT on TX" + "Force 31dB when PS off". TUN on 20m. | RX dashboard shows step ATT 31dB during TUN; releases on TUN-off. | |
| 31 | TUN refused without connection `[bench]` | HL2 | Disconnect radio. Press TUNE button. | Status-bar message "Power must be on to enable Tune". Button auto-unchecks. No MOX engaged. | |
| 32 | First RF on ANAN-G2 `[bench]` | ANAN-G2 + 200W dummy load + watt meter | Tune-Power 25W on 20m. Press TUNE. | Watt meter ~25W carrier on 14.200 MHz. P2 high-pri byte 4 bit 1 = 1 during MOX. P2 high-pri byte 345 = 25. TX I/Q on UDP port 1029 at 24-bit BE. | |
| 33 | ANAN-G2 ALC meter live during TUN `[bench]` | ANAN-G2 | Press TUNE at 50W. | ALC meter shows non-zero deflection during TUN. MeterPoller switches RX→TX poll set on MOX engage. | |
| 34 | ANAN-G2 SWR foldback during TUN `[bench]` | ANAN-G2 + mismatch jig (3:1 SWR) | TUN at 50W into mismatch. | SwrProtectionController trips after 4 trip-debounce samples. Drive collapses to ~1%. HighSWR overlay paints. (3M-0 row 2 + row 10 now actionable.) | |
| 35 | RX-only SKU refuses TUN `[bench]` | HL2 RX-only kit OR ANAN-G2 with chkGeneralRXOnly | Check Receive Only. Press TUNE. | TUN button auto-unchecks. Status-bar TX INHIBIT (Rx2OnlyRadio source). MOX never engages, no wire bytes emit. | |
| 36 | Reconnect cycle without leak `[bench]` | any | Connect radio → TUN → release → disconnect → reconnect → TUN → release. | TUN works identically on second connect cycle. No duplicated channel-creation logs (G.1 fixup verifies WDSP-init lambda doesn't accumulate). | |

## Regressions to watch (additional for 3M-1a)

- **RX path during MOX off** must remain unchanged: spectrum, waterfall,
  audio, VFO, AGC, NR, NB — all of 3M-0's "zero observable change" rule
  still applies when MOX is not engaged.
- **MOX-off → MOX-on → MOX-off** must leave no residual state: AlexController
  antenna selection, step attenuator, drive level, slice DSP mode all
  return to pre-TUN values.
- **`m_tunePowerByBand` persistence** must not duplicate or conflict with
  3G-13/3M-0 keys (verified by code review; keys live under
  `hardware/<mac>/tunePowerByBand/<idx>` per G.3).

## 3M-1a commit sequence

3M-1a landed on `feature/phase3m-1a-tune-only-first-rf` across these commits
(in addition to A.1, A.2 docs and B.*, C.*, D.* foundation work that
preceded the resume from `5c7015a`):

| Phase | Commits | Summary |
|---|---|---|
| E.3 | `5c7015a` | P1 setMox wire emission (C0 byte 3 bit 0) |
| E.4 | `0767d40` + `41ddf3b` | P1 setTrxRelay wire emission (bank 10 C3 bit 7) + codec polarity propagation |
| E.5 | `3425a84` + `e800c92` | P1 setWatchdogEnabled (RUNSTOP pkt[3] bit 7) + comment polish |
| E.6 | `867c727` + `0b557e9` | P2 sendTxIq SPSC ring (port 1029, 24-bit BE) + keep-in-sync comment |
| E.7 | `048f81c` + `37aaaef` | P2 setMox + drive level + latent pttOut→m_mox bug fix + cleanups |
| E.8 | `43a29e4` | P2 setWatchdogEnabled stub deferral note |
| F.1 | `1a84e58` + `5fb42ca` | RadioModel onMoxHardwareFlipped fan-out + queued-dispatch fix |
| F.2 | `f5a5d4a` + `8726aba` | StepAttenuatorController TX-path activation + threading + HPSDR setter wiring |
| F.3 | `a6fd1b9` + `fd08901` + `f2a1c09` | SwrProtectionController carry-forward TODOs + K2UE tag + clamp |
| G.1 | `7e349df` + `b156465` | RadioModel ownership of MoxController + TxChannel + TxMicRouter + lambda accumulation fix |
| G.2 | `56d17ce` + `8a4f794` | Receive-Only checkbox visibility from caps + sibling-field reset |
| G.3 | `d231a00` + `e494a86` + `15a4d5a` | TransmitModel tunePowerByBand[14] + per-MAC persistence + author tag + drop s.save() |
| G.4 | `d07b51c` + `03f7467` | RadioModel::setTune TUN orchestrator + cold-off guard |
| H.1 | `441dcbc` | SpectrumWidget MOX overlay wiring |
| H.2 | `0961301` | MeterPoller TX bindings on MOX |
| H.3 | `d11802e` | TxApplet TUN/Tune-Power/RF-Power/MOX activation |
| H.4 | `555d85d` + `48fa8d6` | PowerPaPage Power/TunePwr/ATTOnTX/ForceATT activation + QPointer + band wire + ATT tests |
| I.1 | (full ctest sweep) | 168/168 tests green |
| I.4 | (this matrix update) | Verification matrix extension |
| I.5 | (TBD) | Post-code Thetis review §2 |

---

# Phase 3M-1b Mic + SSB Voice — Verification Matrix Extension

Added 2026-04-28 as part of M.6. Manual rows tagged `[3M-1b-bench-*]` need
hardware (HL2 + ANAN-G2 dummy load + wireshark) and are deferred to JJ.
Unit-test rows are auto-checked by ctest and pass on commit `26eca01`
(221/221).

## New rows

| # | Test | Hardware | Procedure | Expected | Result |
|---|---|---|---|---|---|
| 14 | BandPlanGuard SSB-mode allow-list | none | `ctest -R '^tst_band_plan_guard_mode_allow_list$' -V` | All 20 slots pass: LSB/USB/DIGL/DIGU allowed; AM/SAM/DSB/FM/DRM rejected with "AM/FM TX coming in Phase 3M-3 (audio modes)"; CWL/CWU rejected with "CW TX coming in Phase 3M-2"; SPEC rejected. | ✅ |
| 15 | BandPlanGuard MOX rejection toast `[3M-1b-bench]` | any radio | Tune to LSB → MOX → "MOX engaged" (success). Switch to AM → click MOX → expect rejected; status-bar toast shows "AM/FM TX coming in Phase 3M-3 (audio modes)" for ~3s; TxApplet MOX button tooltip overrides to the same string. | Toast appears, tooltip overrides; m_mox stays false. | |
| 16 | VOX with mic-boost-aware threshold scaling | none | `ctest -R '^tst_mox_controller_vox_threshold$' -V` | All 14 slots pass: scaled formula `pow(10, dB/20.0) * voxGainScalar` when micBoost==true; passthrough when false. NaN sentinel forces first-call emit. | ✅ |
| 17 | VOX voice-family mode-gate | none | `ctest -R '^tst_mox_controller_vox_enabled$' -V` | All 22 assertions pass: LSB/USB/DSB/AM/SAM/FM/DIGL/DIGU enabled VOX → setVoxRun(true) emit; CWL/CWU/SPEC/DRM enabled VOX → no emit. | ✅ |
| 18 | Anti-VOX path-agnostic | none | `ctest -R '^tst_mox_controller_anti_vox$' -V` | All 18 cases pass: useVax=false → antiVoxSourceWhatRequested(false) emit; useVax=true → qCWarning + state unchanged + no emit. | ✅ |
| 19 | PTT-source dispatch (MIC/CAT/VOX/SPACE/X2) | none | `ctest -R '^tst_mox_controller_ptt_source_dispatch$' -V` | All 19 cases pass: 5 accepted sources × press/release transition with PttMode set; 2 rejected sources (CW/TCI) → qCWarning + no state change. | ✅ |
| 20 | mic_ptt extraction P1 + P2 | none | `ctest -R '^tst_mox_controller_mic_ptt_extraction$' -V` | All 18 cases pass: P1 OR-across-sub-frames bit 0 of C0; P2 raw[4] bit 0; ADC overload byte (raw[5]) does NOT cross-trigger PTT; end-to-end status frame → MoxController dispatch. | ✅ |
| 21 | TransmitModel per-MAC persistence | none | `ctest -R '^tst_transmit_model_persistence$' -V` | All 42 cases pass: 15 persisted keys round-trip; voxEnabled/monEnabled/micMute NOT persisted (safety: load to safe defaults); micGainDb defaults -6 first run; multi-MAC isolated. | ✅ |
| 22 | RadioModel HL2 force-Pc on connect | none | `ctest -R '^tst_radio_model_mic_source_hl2_force$' -V` | All 8 cases pass: HL2 caps force micSource=Pc + lock; non-HL2 unlocked; locked setter coerces Radio→Pc. | ✅ |
| 23 | RadioModel 3M-1b ownership wiring | none | `ctest -R '^tst_radio_model_3m1b_ownership$' -V` | All 15 cases pass: PcMicSource + RadioMicSource + CompositeTxMicRouter constructed on connect; 5 signal connections wired; MoxCheck callback installed. | ✅ |
| 24 | PC mic SSB out `[3M-1b-bench-HL2]` | HL2 + dummy load + USB headset/mic + test receiver | Setup → Audio → TX Input. Confirm Radio Mic radio button hidden + tooltip "Radio mic jack not present on Hermes Lite 2". Confirm PC Mic selected by default. Tune to 7.241 MHz LSB. Configure PC mic device. Run Test Mic → see VU bar move. Set Mic Gain to -6 dB. PTT via TxApplet MOX button → speak → observe SSB carrier on test receiver. Verify TxApplet TxMic + ALC meters paint live values. Confirm clean release on MOX off. | Voice on the air via test receiver; meters live; clean MOX↔Rx transitions. | |
| 25 | PC + Radio mic switching `[3M-1b-bench-G2]` | ANAN-G2 + dummy load + USB headset + radio mic | Tune to 7.241 MHz LSB. Configure PC mic (default), confirm SSB out. Switch to Radio Mic, configure 3.5mm jack default, plug radio mic, speak, confirm SSB out. | Both mic paths produce clean SSB; switching is < 1s; no MOX-locked-during-switch deadlock. | |
| 26 | Mic-jack bits wireshark cross-check `[3M-1b-bench-G2]` | ANAN-G2 + wireshark on RX/TX UDP traffic | Toggle each mic-jack control in turn (MicBoost / LineIn / MicTipRing / MicBias / MicPTT). For each, capture the next outbound P2 transmit-specific packet (port 1029) AND the next outbound P1 bank-10/bank-11 C&C frame (port 1024). | Bit positions match the deskhpsdr `new_protocol.c:1480-1502` polarity table verbatim. P1 bank-10 C2 bit 0 = mic_boost; bank-10 C2 bit 1 = line_in; bank-11 C1 bit 4 = mic_trs (inverted); bank-11 C1 bit 5 = mic_bias; bank-11 C1 bit 6 = mic_ptt (inverted). | |
| 27 | MicXlr P2 byte-50 bit 5 `[3M-1b-bench-G2]` | ANAN-G2 + XLR mic + wireshark | Switch Saturn G2 family between 3.5mm and XLR via Setup → Audio → TX Input → Radio Mic group. Capture P2 transmit-specific packet for each. | byte-50 bit 5 (0x20): set when XLR selected, clear when 3.5mm. Default-true means a fresh connection sees bit 5 set. | |
| 28 | MON enable + monitor volume `[3M-1b-bench]` | any radio + headphones | With headphones connected, enable MON via TxApplet. Set volume to 50. PTT and speak. Confirm self-audio in headphones. Move RX volume to 0 → MON audio still flows during MOX. | Self-audio audible during MOX, level scales with volume slider, independent of RX volume. Default volume on first run = 50. | |
| 29 | RX-leak fix during MOX `[3M-1b-bench]` | any radio + dummy load | With MON disabled, tune RX1 to 7.250 MHz LSB; RX2 (if available) to 14.200 MHz. Engage MOX (PTT + speak). | Active slice (RX1) RX audio silenced during MOX. Non-active slice (RX2) audio unaffected. (Was cosmetic bug in 3M-1a per row 30 carry-forward.) | |
| 30 | PTT-source dispatch bench `[3M-1b-bench]` | any radio | For each of MIC PTT (radio mic), CAT PTT (rigctl/hamlib send T command), VOX (auto-engaged on speech threshold), SPACE (UI keyboard), X2 (external TX trigger): exercise each source and confirm MoxController.pttMode reflects the right enum value, MOX engages, and TX I/Q starts flowing. | Each source independently engages + releases MOX with correct pttMode set. | |

## Carry-forward flips from 3M-1a

| Row | Change |
|---|---|
| 3M-1a "RX still plays during MOX (cosmetic)" | Flips from "deferred" → "fixed in 3M-1b". E.4 + RadioModel activeSlice gate fold-in resolves the cosmetic leak. Verified by row 29 above. |
| 3M-1a "PA telemetry meters during TUN" | Flips to "PA telemetry meters during MOX-voice TX". Same meters now exercised under voice TX (rows 24-25), not just TUN. |

## Result tracking

Rows 14, 16-23 (unit tests, 9 rows): ✅ all green on commit `26eca01` (221/221).
Rows 15, 24-30 (bench tests, 8 rows): pending JJ + hardware.

When bench rows complete, edit the Result column to `✅` with the commit SHA where the row was confirmed.

## Phase 3M-1b commit summary (added 2026-04-28 by M.6)

| Phase | Commits | Summary |
|---|---|---|
| Plan + pre-code review | `2a7a3b1` + `be2c52d` | Pre-code Thetis review + implementation plan |
| B.1-B.2 | `01fb507` + `4828d6d` | BoardCapabilities::hasMicJack + deskhpsdr provenance |
| C.1-C.5 | `c9700a7` + `9c6b4f1` + `5c29049` + `d6258b9` + `9e6fec8` + 2 fixups | TransmitModel mic gain + 8 mic-jack flags + 4 VOX + 2 anti-VOX + 2 MON props |
| D.1-D.7 | `db1fbd9` + `c900b88` + `372489c` + `33fe64b` + `940c7e7` + `24a9f2e` + `b4ad655` + 3 fixups | TxChannel mic-router + per-mode TXA + VOX/anti-VOX wrappers + stage Run + Sip1 signal + mic-mute + meters |
| E.1-E.4 | `0765bb0` + `27408aa` + `9ec4a9a` + `7ed6710` | AudioEngine pullTxMic + monitor enable/volume + slot + RX-leak gate |
| F.1-F.4 | `d39135b` + `f64a2d3` + `83f8625` + `ef588ec` + 1 fixup | PcMicSource + RadioMicSource (SPSC ring) + CompositeTxMicRouter + RadioConnection::micFrameDecoded |
| G.1-G.6 | `46d93f6` + `b3e63fb` + `4cc76a4` + `ce160da` + `c6b0712` + `dfd5393` | 6 mic-jack wire-bit setters byte-exact ported from Thetis P1 + deskhpsdr P2 |
| H.1-H.5 | `2b36db8` + `bd038ef` + `5d40a3d` + `81c6002` + `6829f5a` + `efd7ebc` | MoxController VOX + anti-VOX + PTT-source dispatch + mic_ptt extraction |
| I.1-I.5 | `7966a8d` + `647bae8` + `7816eed` + `c28adbb` + `910334e` + `2fe2c39` + `c787936` | AudioTxInputPage + per-family Radio Mic + mic gain per-board range + VOX integration |
| J.1-J.3 | `0ae8a9b` + `817b68b` + `c961690` + `318d422` + `047c762` | TxApplet Mic Gain + VOX toggle/popup + MON + mic-source badge |
| K.1-K.2 | `3c9c707` + `80359bb` | BandPlanGuard SSB-mode allow-list + MOX rejection toast/tooltip |
| L.1-L.3 | `64bb8fa` + `e3cedf8` + `0da0f2e` + `26eca01` | RadioModel ownership + per-MAC persistence + HL2 force-Pc lock |
| M.1 | (full ctest sweep) | 221/221 tests green on `26eca01` |
| M.6 | (this matrix update) | Verification matrix extended with 17 rows (3M-1b) |
| M.7 | (TBD) | Post-code Thetis review (`phase3m-1b-post-code-review.md`) |
