# Level Cal and Preamp Bench Verification Matrix

**Status:** `Pending bench`. Nothing here has been run on a radio. The
software side is covered by unit and byte-level wire tests; these rows
need a signal generator and each radio family on the bench.

**What changed:** the preamp combo now carries Thetis's ten preamp modes
with the per-board labels from `console.cs:28405-28450 [v2.10.3.15]`, and
each mode drives the step attenuator, the preamp bit and the Alex
attenuator as `console.cs:19230-19285 [v2.10.3.15]` does. Classic auto-att
steps through the step attenuator settings (`console.cs:21614
[v2.10.3.15]`). Each preamp setting keeps its own receive offset, and the
Level Cal Start button runs Thetis's CalibrateLevel on the Core
(`console.cs:9856-10232 [v2.10.3.15]`).

**Safety:** every row is receive-only. Do not key the radio for any row.
Feed the signal generator through a suitable attenuator; keep the level
at or below -20 dBm at the antenna jack.

| # | Row | Procedure | Pass criterion | Status |
|---|---|---|---|---|
| 1 | Preamp labels, HPSDR | Connect an Atlas/HPSDR, open the preamp combo | Items read as Thetis shows for HPSDR | Pending |
| 2 | Preamp labels, Hermes/Angelia/Orion | Connect each, open the preamp combo | Items read as Thetis shows for that board | Pending |
| 3 | Preamp labels, ANAN-G2 / G2E / 7000D / 8000D | Connect each, open the preamp combo | Items read as Thetis shows for the MKII class | Pending |
| 4 | Preamp labels, HL2 | Connect the HL2, open the preamp combo | Items read as mi0bot-Thetis shows for HL2 | Pending |
| 5 | Preamp drive | Carrier at -73 dBm on 14.100 MHz, step through every preamp item | Each step moves the S-meter by the item's dB within 1 dB; no item leaves the receiver deaf | Pending |
| 6 | Stored preamp choice after upgrade | Pick a preamp item on the old build, update, reconnect | The same item is selected and the S-meter reads as before | Pending |
| 7 | Classic auto-att | Enable Classic auto-att, raise the carrier until ADC overload | Preamp steps to the step attenuator settings, then back as the level falls | Pending |
| 8 | Level Cal, local | Carrier at -73 dBm on 14.100 MHz, Setup, Calibration, Level Cal, Start, answer Yes | Progress runs to 100, "Level Calibration complete." shows, S-meter reads -73 dBm within 1 dB with each preamp item | Pending |
| 9 | Level Cal, remote window | Same as row 8 from a remote window on the Core | Progress follows the Core's run; the result lands on the Core; the local window shows the same reading | Pending |
| 10 | Level Cal, cancel | Start a run, press Cancel part way | Status reads "Level calibration was canceled."; frequency, mode, preamp, step attenuator and buffer are back as before | Pending |
| 11 | Level Cal, no signal | Start with the generator off | The run stops with its reason and nothing changes | Pending |
| 12 | Grid follow | Turn on the grid's noise-floor follow, run row 8 | Follow is off during the run and back on after | Pending |
| 13 | Level Cal, phone | Start and cancel from the phone app | Same as rows 9 and 10 | Pending |

## Boards whose wire output changed

Picking a preamp item now sends Thetis's step attenuator, preamp bit and
Alex attenuator values on: Atlas/HPSDR, Hermes, Hermes II, Angelia,
Orion, Orion MKII, Saturn / Saturn MKII (ANAN-G2), HermesC10 (ANAN-G2E),
Anvelina Pro 3, Red Pitaya, and Hermes Lite 2. Boards without the
HPSDR preamp relay no longer receive the preamp bit. Classic auto-att
drives the step attenuator settings of the preamp list.
