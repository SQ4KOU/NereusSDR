# Phase 3M-3a-iv Manual Bench Verification

**Goal:** Confirm anti-VOX cancellation feed works end-to-end on real hardware.

**Setup:**
- Workstation running NereusSDR build of `feature/phase3m-3a-iv-antivox-feed`.
- ANAN-G2 (Protocol 2) and Hermes Lite 2 (Protocol 1) on the same bench.
- Quiet mic and a mic-mute switch.
- A known-strong RX signal source: tune to a CW spotting beacon or an SSB
  net at S9+5 measured on the in-applet S-meter.

**Pre-test:** Confirm the Setup page Tau (ms) spinbox shows 20 by default and
range-clamps at 1 and 500. Confirm the tooltip text matches Thetis verbatim
("Time-constant used in smoothing Anti-VOX data" from
`setup.designer.cs:44681 [v2.10.3.13]`).

## Per-radio matrix

For each radio (ANAN-G2, then HL2 mi0bot), run rows 1-5 in order. Record
PASS / FAIL with a short note for each cell.

| # | Test | Setup | Expected | ANAN-G2 | HL2 |
|---|------|-------|----------|---------|-----|
| 1 | Speaker-bleed false-trip baseline | VOX engaged with threshold above mic noise floor; mic muted; RX speaker on at S9+5 SSB voice; anti-VOX gain = 0 dB | VOX false-trips on RX speaker audio; TX-LED toggles on speech bursts | | |
| 2 | Anti-VOX -20 dB suppression | Same as row 1 but anti-VOX gain = -20 dB, tau = 20 ms | False-trips stop or substantially reduce; mic-talk still trips | | |
| 3 | Anti-VOX -40 dB suppression | Same as row 1 but anti-VOX gain = -40 dB | False-trips fully suppressed even at S9+10 RX bursts | | |
| 4 | Tau range bench | Anti-VOX gain = -20 dB; sweep tau from 1 ms to 500 ms in five values (1, 20, 80, 200, 500); strong RX signal | Larger tau audibly extends the engage / disengage time on bursty RX audio (e.g. SSB voice peaks). 1 ms responds twitchy; 500 ms feels lazy. | | |
| 5 | Restart persistence | Set anti-VOX gain = -15 dB and tau = 80 ms; close NereusSDR; reopen; reconnect to the radio | Anti-VOX gain spinbox and Tau (ms) spinbox both restored to set values. Anti-VOX behavior pre- and post-restart is identical. | | |

## Cross-cutting checks

- [ ] No `lcDsp` warnings in the log during normal RX -> TX -> RX cycles.
- [ ] No `lcDsp` warnings on radio reconnect (geometry change covered).
- [ ] CPU usage at idle RX with anti-VOX run = false matches v0.3.2 baseline
      (no perceptible regression from the per-chunk fork).
- [ ] CPU usage at idle RX with anti-VOX run = true is within +1% of run = false.

## Sign-off

Verification owner: J.J. Boyd (KG4VCF). PR is not merged until both
ANAN-G2 and HL2 columns are PASS for rows 1-5 plus all four cross-cutting
checks. If any cell is FAIL, file an issue against this branch and resolve
before tagging.
