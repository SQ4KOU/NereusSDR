# Phase 3I — Smoke Test Checklist

**Scope:** validating the Phase 3I radio connector & radio-model port against real OpenHPSDR hardware.

**When to run:** before merging `feature/phase3i-radio-connector-port` (PR #12) to `main`, and any time you pull the branch onto a different machine.

**Source of truth for the feature:**
- Design — `docs/architecture/phase3i-radio-connector-port-design.md`
- Plan — `docs/architecture/phase3i-radio-connector-port-plan.md`
- Verification matrix — `docs/architecture/phase3i-verification.md`

---

## 0. Build & launch

```bash
cd ~/NereusSDR
git checkout feature/phase3i-radio-connector-port
cmake --build build-clean -j
ctest --test-dir build-clean --output-on-failure --timeout 90
```

**Expected:** 9/9 automated tests pass in ~51 s (the reconnect-silence contract test alone takes ~49 s of that).

Launch the app:

```bash
osascript -e 'tell application "NereusSDR" to quit' 2>/dev/null
open ~/NereusSDR/build-clean/NereusSDR.app
```

---

## 1. Idle stability

Before touching anything, let the app sit for ~30 seconds.

- [ ] Window draws fully, no beachballs
- [ ] No crash to desktop, no assertion dialog
- [ ] CPU meter in the status bar stays in single-digit % (should be ~1–5%)
- [ ] Spectrum widget draws the "no signal" baseline without flicker
- [ ] No new crash report in `~/Library/Logs/DiagnosticReports/NereusSDR-*.ips`

**If any of these fail, stop and file a bug with the crash log before proceeding.** The hotfix in this branch killed a 5 s continuous NIC-walk timer that was previously blocking the main thread; if responsiveness regresses, it means either that timer came back or a new hot loop was introduced.

---

## 2. Discovery

**Before:** make sure at least one radio is powered on and on the same LAN as your Mac.

1. Open **Radio → Connection…** (or whichever menu path opens ConnectionPanel)
2. Click **Start Discovery**
3. Wait ~5 seconds

**Check each radio row:**

- [ ] Appears in the list within ~5 s
- [ ] Status dot is **green** (online + free) or **amber** (online + in-use by another client)
- [ ] **Name** column populated from `BoardCapabilities::displayName` (e.g., "Hermes Lite 2", "ANAN-100D (Angelia)")
- [ ] **Board** column shows the correct `HPSDRHW` type
- [ ] **Protocol** column: P1 for HL2/Hermes/Angelia/Orion; P2 for OrionMKII/Saturn
- [ ] **IP** column has the real IPv4 (not `::ffff:…`)
- [ ] **MAC** column populated
- [ ] **Firmware** column populated

**Regressions to watch for:**
- Main thread freeze while discovery runs. A one-time click-triggered freeze of up to ~10 s is expected and a known follow-up (scanAllNics still runs on the main thread). **Repeated** or **multi-second idle** freezes indicate the continuous timer regressed.
- A P2 Saturn / ANAN-G2 not appearing — Phase 3I's P2 audit (Task 13) should be a pure no-op for Saturn. If it breaks, the audit went wrong.

---

## 3. First connect

Double-click the radio row or select it and click **Connect**.

- [ ] State transitions Disconnected → Connecting → Connected within ~2 s
- [ ] Status strip / chip shows the radio name + Connected state
- [ ] No crash — specifically watch for the WDSP-race crash in `GetRXAMeter` that the hotfix addressed. If you see a segfault within the first second of connecting, the `m_active` guard on `RxChannel::getMeter()` regressed.

---

## 4. Hardware Config tabs — **the big Phase 3I UI surface**

Open **Setup → Hardware Config** (or whatever the menu path is).

### 4.1 Radio Info tab

- [ ] Board type populated (e.g., "Hermes Lite 2")
- [ ] Protocol label populated (P1 / P2)
- [ ] ADC count populated (1 for Hermes/HL2; 2 for Angelia/Orion/Saturn)
- [ ] Max RX populated
- [ ] Firmware version matches what the radio reports
- [ ] MAC address populated
- [ ] IP address populated
- [ ] Sample-rate combo shows **only** the rates your board supports — HL2: 48/96/192/384 kHz; Hermes/HermesII: 48/96/192 kHz (no 384k — per `Setup.cs:850-853`); Angelia/Orion: 48/96/192/384 kHz; Saturn/OrionMKII: 48/96/192/384/768/1536 kHz (per `Setup.cs:854`)
- [ ] Active-receiver spinbox capped at the board's maximum (HL2 max 4, Saturn max 7)
- [ ] **Copy support info** button dumps a one-line text blob to clipboard you can paste into an issue

### 4.2 Per-board tab visibility

This is capability-gated via `BoardCapsTable::forBoard(hw).hasXxx`. Check the tab strip against the expected set for your board.

**Hermes Lite 2 (HL2):**
- [ ] Visible: Radio Info, HL2 I/O, Bandwidth Monitor
- [ ] Hidden: Antenna/ALEX, OC Outputs, XVTR, PureSignal, Diversity, PA Calibration

**ANAN-100D (Angelia), ANAN-200D (Orion):**
- [ ] Visible: Radio Info, Antenna/ALEX, OC Outputs, XVTR, PureSignal, Diversity (if 2-ADC), PA Calibration
- [ ] Hidden: HL2 I/O, Bandwidth Monitor

**ANAN-G2 (Saturn), ANAN-7000DLE/8000DLE (OrionMKII):**
- [ ] Same visible set as Angelia/Orion
- [ ] Hidden: HL2 I/O, Bandwidth Monitor

**Metis / HPSDR kit (Atlas):**
- [ ] Visible: Radio Info only
- [ ] Hidden: everything else

If a tab that should be visible is missing (or vice-versa), the mismatch points at a drift between `BoardCapabilities.cpp` and `HardwarePage::onCurrentRadioChanged`.

### 4.3 Sub-tab controls — quick smoke

You don't need to click every control, but verify each visible tab renders:

- [ ] **Antenna/ALEX:** 14-row × 3-col RX antenna grid, same for TX; 6 bypass checkboxes
- [ ] **OC Outputs:** two 14×7 grids (RX mask + TX mask), relay settle delay spinbox
- [ ] **XVTR:** editable table with 8 columns (Enabled / Name / RF Start / RF End / LO Offset / RX-only / Power / LO Error)
- [ ] **PureSignal:** Enable checkbox, feedback-source combo, auto-cal checkboxes, RX feedback atten slider (cold — DSP lands in a later phase)
- [ ] **Diversity:** Enable, phase slider −180..180, gain slider −60..20, reference-ADC combo, null-signal button
- [ ] **PA Calibration:** PA profile combo, per-band target power and gain correction tables, step-atten cal button (shown only if `caps.hasStepAttenuatorCal`)
- [ ] **HL2 I/O:** I/O board present label, PTT/CW input pin combos, aux output assignments
- [ ] **Bandwidth Monitor:** live PHY rate label (default "— Mbps" until a real feed is wired in Phase 3L), throttle threshold spinbox, auto-pause checkbox

---

## 5. Audio + DSP (RX path)

With the radio connected, tune to a live signal.

- [ ] Tune RX1 to a known active SSB frequency (e.g., 20 m: 14.150–14.350 MHz LSB daytime, 40 m: 7.150–7.300 MHz LSB nighttime)
- [ ] **Hear demodulated audio** through your output device
- [ ] Spectrum widget updates in real time at ~30 FPS
- [ ] Waterfall scrolls
- [ ] S-meter needle moves with signal level
- [ ] Cursor frequency readout matches VFO

**This is the success criterion for Phase 3I.** A P1 radio here behaves identically to how Saturn behaves on `main`. If audio doesn't come up but the connection is green and the spectrum is live, the I/Q flow into WDSP is broken — check `P1RadioConnection::iqDataReceived` emissions against `ReceiverManager::onIqData`.

---

## 6. Front-end controls

Still connected to the radio:

- [ ] **Atten slider 0 → 30 dB** — noise floor drops ~30 dB both audibly and on the meter. HL2 range is 0..60 dB (step-atten); Hermes/Angelia/Orion are 0..31 dB.
- [ ] **Sample-rate combo** — change 48k → 192k or similar, verify span/resolution updates, no crash
- [ ] **Active RX count** — change 1 → 2 on a 2-ADC board, verify second RX appears without crashing
- [ ] **Volume slider** works
- [ ] **Mode change** (USB / LSB / AM / CW) — audio character changes as expected

---

## 7. Disconnect / reconnect

- [ ] Click **Disconnect** — state flips to Disconnected, audio stops, spectrum shows idle baseline
- [ ] No crash
- [ ] Click **Connect** again — reconnects cleanly, audio resumes
- [ ] Let the radio time out (unplug it or pull its LAN cable for ~5 s) — watchdog trips, state goes to Error within 2 s
- [ ] Plug the radio back in — reconnect state machine tries up to 3 times at 5 s intervals and either recovers or stays in Error

The bounded-retry contract is asserted by `tst_reconnect_on_silence` but it's worth eyeballing on real hardware.

---

## 8. Persistence across restarts

- [ ] In Hardware Config, change a few settings (sample rate, atten, a PureSignal checkbox, an OC mask cell)
- [ ] Quit the app (red close button or ⌘Q) — **no crash report in `~/Library/Logs/DiagnosticReports/`** with today's timestamp (pre-hotfix, every close produced a segfault from `QThreadStoragePrivate::finish` firing against a destructed `QRegularExpression` in the PII-redaction message handler)
- [ ] Relaunch — saved radios still appear in ConnectionPanel
- [ ] Reconnect to the same radio — your settings from before the quit are still populated in Hardware Config tabs (per-MAC persistence under `hardware/<MAC>/*` in `AppSettings`)

---

## 9. Auto-reconnect on launch

- [ ] Connect to a radio
- [ ] Quit cleanly while still connected
- [ ] Relaunch — the app should silently reconnect within a few seconds without you opening ConnectionPanel
- [ ] On failure (radio off, IP changed and pinToMac=false): no popup, ConnectionPanel still works normally, no error visible to the user

---

## 10. Multi-board spot checks

If you have more than one radio on hand, connect to each in turn:

| Board | Check |
|---|---|
| HL2 | §4.2 tab set; atten 0..60 dB range; bandwidth monitor tab present |
| ANAN-100D / Angelia | Diversity tab visible + functional phase/gain sliders; PureSignal tab visible |
| ANAN-200D / Orion | PA Calibration tab with step-atten cal button |
| ANAN-7000/8000 / OrionMKII | Same tab set as Angelia but on P2 path |
| ANAN-G2 / Saturn | **Regression check** — behaviour must be identical to `main` before Phase 3I |

---

## Known deferred / cold-wired

Do **not** file bugs for these. They're explicitly out of scope for Phase 3I (see design doc §9 + verification doc).

- **TX audio** — TX IQ producer is plumbed cold. Commands go on the wire when the model asks for them; no SSB modulator exists yet.
- **PureSignal feedback DSP** — the tab persists its state, but no linearization loop runs. TX-phase work.
- **HL2 `IoBoardHl2` I2C init** — the helper logs "I/O board init deferred" and does not send real I2C bytes. The encoding lives inside closed `ChannelMaster.dll`; Phase 3L will extract it from a live capture.
- **Bandwidth monitor feed** — the tab shows `— Mbps` and `0 events` because no live feed is wired yet. The `Hl2BandwidthMonitor` sequence-gap heuristic in `P1RadioConnection` works as a crude throttle detector but doesn't publish to the UI yet.
- **TCI protocol, RedPitaya, sidetone, firmware flasher** — each is its own phase.
- **Multi-radio simultaneous connection** — not planned.

---

## Reporting a bug

When something fails a checklist item, capture:

1. **Which step failed** (e.g., §4.1 Radio Info tab — ADC count label blank)
2. **Which radio you were using** (board type + firmware version — Radio Info tab has this)
3. **App log lines** from the failure — they live in `~/.config/NereusSDR/nereussdr.log` (Linux) or `~/Library/Application Support/NereusSDR/nereussdr.log` (macOS, under `QStandardPaths::GenericConfigLocation`). The file is rotated per launch.
4. **Any new crash report** in `~/Library/Logs/DiagnosticReports/NereusSDR-*.ips` — attach the whole file
5. **Branch SHA**: `git -C ~/NereusSDR rev-parse HEAD`

File as a comment on PR #12 or a new GitHub issue tagged `phase-3i`.

---

## What a clean run looks like

1. `ctest` — 9/9 pass in ~51 s
2. App launches and stays responsive
3. Discovery finds your radio(s) in ~5 s with all columns populated
4. Connect works, Radio Info tab populates, the right sub-tabs are visible for your board
5. Audio plays, spectrum + waterfall + meters update
6. Controls (atten, sample rate, volume, mode) work
7. Disconnect + reconnect clean
8. Quit clean — **no new crash report**
9. Relaunch auto-reconnects silently
10. Per-MAC settings survive the restart

When you've checked all 10, Phase 3I is ready to flip PR #12 from draft to ready-for-review.
