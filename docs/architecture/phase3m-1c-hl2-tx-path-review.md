# Phase 3M-1c — HL2 TX Path Desk-Review vs mi0bot-Thetis

**Status:** Phase 3M-1c chunk 0 deliverable.
**Date:** 2026-04-28.
**Branch:** `feature/phase3m-1c-polish-persistence`.
**Design spec:** [`phase3m-1c-polish-persistence-design.md`](phase3m-1c-polish-persistence-design.md) §4bis.
**Author:** J.J. Boyd ~ KG4VCF, with AI tooling.

This is a **desk-review only** — no hardware, no protocol captures, no code
changes in this commit. The output is a categorised findings list that
informs Phase 3M-1c chunks 1-9 implementation.

---

## 1. Scope and method

### 1.1 Why

NereusSDR has shipped Phase 3M-1a (TUNE) and 3M-1b (SSB voice). Bench
testing on Hermes Lite 2 (HL2) is hardware-gated; rows M.2 / M.3 / M.4 /
M.5 in the verification matrix are open and likely to remain so for a
while.

**mi0bot-Thetis** is a Thetis fork by Reid Campbell (MI0BOT) focused on
HL2 support. It carries HL2-specific patches not in upstream Thetis.
This document compares mi0bot's HL2 TX path against upstream Thetis and
NereusSDR — so HL2 divergence is caught before bench testing reveals it.

### 1.2 Sources

| Codebase | Path | Version | Cite stamp |
|---|---|---|---|
| mi0bot-Thetis | `/Users/j.j.boyd/mi0bot-Thetis` | `v2.10.3.13-beta2` (`@c26a8a4`) | `[v2.10.3.13-beta2]` for tagged values; `[@c26a8a4]` for post-tag |
| Upstream Thetis | `/Users/j.j.boyd/Thetis` | `v2.10.3.13` (`@501e3f51`) | `[v2.10.3.13]` for tagged; `[@501e3f51]` for post-tag |
| NereusSDR | `feature/phase3m-1c-polish-persistence` @ `a8fb2d3` | (this branch) | n/a |

### 1.3 Method

For each of nine HL2 TX path areas defined in design spec §4bis:

1. Locate mi0bot's HL2-specific code (file:line range).
2. Compare to upstream Thetis (present, absent, or different).
3. Compare to NereusSDR equivalents.
4. Document the delta and propose action: **(a) absorb into 3M-1c chunk N**,
   **(b) file as separate follow-up issue**, or **(c) known-divergence
   with rationale**.

Files inspected (mi0bot):
`Console/HPSDR/IoBoardHl2.cs`, `Console/HPSDR/NetworkIO.cs`,
`Console/HPSDR/NetworkIOImports.cs`, `Console/HPSDR/clsRadioDiscovery.cs`,
`Console/HPSDR/Alex.cs`, `Console/clsHardwareSpecific.cs`,
`Console/console.cs`, `Console/setup.cs`, `Console/cmaster.cs`,
`Console/enums.cs`, `ChannelMaster/networkproto1.c`.

### 1.4 Scope boundaries

**In-scope:** HL2 TX paths only. RX, PureSignal, and CW-specific patches
are out of scope (those land in 3F / 3M-2 / 3M-4 follow-ups).

**Not in scope but flagged:** RX-side step attenuator inversion (mi0bot
also applies `31 − N` for RX on HL2 per `console.cs:11075/11251/19380`).
Outside this chunk's TX-only mandate but worth a follow-up issue.

---

## 2. Findings

### 2.1 Area 1 — HL2 P1 EP2 zone TX timing

**mi0bot:** `ChannelMaster/networkproto1.c:869-1201` (`WriteMainLoop_HL2`),
`:1241-1264` (`sendProtocol1Samples`). Two USB sub-frames per EP2 frame
(line 878), 512-byte sub-frames at offset 0 / 512. Per sub-frame: 3 sync
bytes + 5 C&C bytes + 8 × 63 = 504 payload bytes. Audio L/R + I/Q packed
8 bytes/sample. Single 1032-byte EP2 frame at base 48 kHz audio rate
(`SampleRateIn2Bits` in C0=0 case at line 949). Semaphores
`prn->hobbuffsRun[0/1]` (lines 1199-1200) throttle next iteration.

**Upstream Thetis:** `ChannelMaster/networkproto1.c:419-746`
(`WriteMainLoop`) — identical structure for non-HL2 boards. No
`WriteMainLoop_HL2` upstream.

**NereusSDR:** `src/core/P1RadioConnection.cpp:963-1026` (`sendTxIq`),
`:1047-1075` (`fillTxZone`); `.h:337-340` defines `kTxIqBufSamples =
4032` (~84 ms @ 48 kHz, 32× zone size), `kTxIqBytesPerSample = 8`,
`kSamplesPerZone = 63`. Float→int16 gain `kGain = 32767.0f` matches
mi0bot at `networkproto1.c:1245-1246`. SPSC ring with relaxed/release
semantics on `m_txIqCount`. Underrun zero-fills the zone.

**Delta:** Sample/byte semantics match exactly. NereusSDR's 32× ring is
generous, decoupled from mi0bot's Windows-specific semaphore-driven
cadence — appropriate for the Qt6 timer pacer model.

**Action:** **(c) known-divergence with rationale.** NereusSDR's ring
sizing is correct; mi0bot's semaphore-pacer is a Windows implementation
detail. Verify on hardware bench.

---

### 2.2 Area 2 — HL2 mic-jack handling

**mi0bot:** `console.cs:29473-29494, 29508-29528` (`ptbMic_Scroll`,
`setAudioMicGain`). On HL2, the mic gain slider is **repurposed** as VAC
TX gain, gated by `chkRX2.Checked && chkVAC2.Checked && chkVFOBTX.Checked`
and `_rx1_dsp_mode != DIGU/DIGU` (yes, the upstream typo in the gate).
Slider sets `SetupForm.VACTXGain = ptbMic.Value` and
`vac_tx_gain = ptbMic.Value` instead of radio mic preamp. Inline tag
`// MI0BOT: For HL2 Audio control is based on VFO and Mode`.
mi0bot still wires `mic_trs` / `mic_bias` / `mic_ptt` / `line_in_gain`
bits to C&C bank 11 (`networkproto1.c:1093-1097`) but HL2 firmware
ignores them — wire format preserved for protocol compatibility.

**Upstream Thetis:** `console.cs` lacks the HL2 mic-VAC repurposing.
Same C&C bank 11 wire format at `networkproto1.c:597-599`. No
HL2-specific UI behaviour.

**NereusSDR:** `src/core/BoardCapabilities.cpp:618` (`hasMicJack=false`
for `kHermesLite`) and `:662` (for `kHermesLiteRxOnly`).
`src/core/audio/CompositeTxMicRouter.cpp:36-38` enforces force-PC source
routing when `!m_hasMicJack`. The HL2 codec at
`src/core/codec/P1CodecHl2.cpp:131-159` correctly emits
`mic_trs` / `mic_bias` / `mic_ptt` bits with NereusSDR's polarity
inversions — wire format matches mi0bot byte-for-byte.

**Delta:** NereusSDR's `CompositeTxMicRouter` design is **cleaner** than
mi0bot's overloaded slider. The Mic Source = "Radio" path is collapsed
at construction for HL2; the user uses the dedicated VAC TX gain control
directly. Wire-byte emission matches mi0bot.

**Action:** **(c) known-divergence with rationale.** NereusSDR's design
is intentionally cleaner; document in 3M-1c spec.

---

### 2.3 Area 3 — HL2 watchdog (RUNSTOP `pkt[3]` bit 7)

**mi0bot:** `ChannelMaster/networkproto1.c:32-69` (`SendStartToMetis`),
`:71-104` (`SendStopToMetis`). `pkt[3] = 0x01` (start) or `0x00` (stop).
**No bit-7 watchdog manipulation in the RUNSTOP packet itself.** The
watchdog is encoded in C0=0 bank C1 (`SampleRateIn2Bits`) — bit 7 is the
watchdog disable bit set by Setup→Network checkbox via
`NetworkIO.SetWatchDogEnabled`. mi0bot makes **no HL2-specific change**
to MOX-boundary watchdog handling.

mi0bot adds `prn->reset_on_disconnect` bank 18 (`networkproto1.c:1170-1176`)
which writes the reset-on-disconnect flag to C0=0x74 — a new HL2-only
feature not in upstream Thetis.

**Upstream Thetis:** Identical RUNSTOP encoding at
`networkproto1.c:~28-99`. No bank 18 — that's mi0bot-only.

**NereusSDR:** `src/core/P1RadioConnection.cpp:897-928`
(`setWatchdogEnabled`) — stores state, no immediate re-send (matches
deskhpsdr / mi0bot pattern). RUNSTOP wire bytes at lines 904-918 match
upstream/mi0bot byte-for-byte. **Bank 18 (Reset on disconnect) is
already wired** in `P1CodecHl2.cpp:198-204` with `ctx.hl2ResetOnDisconnect`
field.

**Delta:** None. NereusSDR matches mi0bot's behaviour on both the
watchdog bit and bank 18 reset-on-disconnect.

**Action:** **(c) nothing material to absorb.** NereusSDR's 3M-1a Task
E.5 watchdog work is correct.

---

### 2.4 Area 4 — HL2 step attenuator on TX **[CRITICAL — absorb]**

**mi0bot:** `console.cs:10657-10663, 19164-19167, 27814-27817`. At every
TX-attenuator-update site, the HL2 path applies
`NetworkIO.SetTxAttenData(31 - txatt)` instead of the standard
`NetworkIO.SetTxAttenData(txatt)`. Inline tag
`// MI0BOT: Greater range for HL2`. The `31 − N` inversion is required
because HL2 firmware (`prn->adc[0].tx_step_attn` in netInterface.c) treats
the field as **"more attenuation = higher value"** — opposite of the
user-facing dB. HL2 LNA range −28..+32 (60 dB span); `console.cs:2113-2115`
sets `udTXStepAttData.Minimum = -28`. mi0bot's force-31 logic in
`SetATTOnTX` and PS-off paths uses the user-facing 31 dB **before**
passing through the inverter.

**Upstream Thetis:** `console.cs:10612-10620` and equivalent
`SetTxAttenData(txatt)` callsites lack the HL2 inversion — upstream sends
raw `txatt`. Since upstream Thetis lacks the HL2 boot path entirely
(`clsHardwareSpecific.cs` has no `HERMESLITE` case at lines 85-148), the
`31 − N` path is unreachable on upstream Thetis with stock HL2 hardware
even when `tx_step_attn` is wired.

**NereusSDR:** `src/core/StepAttenuatorController.cpp:300-315`
(`shouldForce31Db`) — predicate correctly returns 31 for
`(forceAttWhenPsOff && isPsOff) || CWL || CWU`.
`src/core/P1RadioConnection.cpp:828-833` (`setTxStepAttenuation`) clamps
to `[0, 63]` and stores raw to `m_txStepAttn`.
`src/core/codec/P1CodecHl2.cpp:155` writes
`out[4] = quint8((ctx.txStepAttn[0] & 0b00111111) | 0b01000000)` to wire.
**There is NO `31 − N` inversion anywhere in NereusSDR's HL2 TX path.**

**Delta — wire-format semantic bug:** With current code, user sets
`txAtt = 31` (max protection — the safety force-31-dB value for
CWL / CWU / PS-off) → NereusSDR sends raw 31 → HL2 firmware interprets
as ZERO attenuation → full PA drive at the moment we were trying to
PROTECT the PA. mi0bot sends `31 − 31 = 0` → HL2 firmware reads "0 = MORE
attenuation" → correct (max attenuation = 31 dB cut). The
`StepAttenuatorController::shouldForce31Db` predicate is logically
correct, but the value passed to the wire is inverted on HL2.

**Action:** **(a) absorb into 3M-1c chunk 0 fixup or chunk 7
(B.2 timer refactor as adjacent codec work).** Add HL2-specific `31 − N`
inversion in `P1CodecHl2.cpp` (codec-layer is preferable — centralised,
keeps `P1RadioConnection::setTxStepAttenuation` board-agnostic). Add a
unit test asserting `out[4] = ((31 − userValue) & 0b00111111) | 0b01000000`
on HL2 boards.

**Open question — RX side:** mi0bot also applies `31 − N` for HL2 RX-side
attenuator at `console.cs:11075, 11251, 19380` (`SetADC1StepAttenData(31 −
_rx1_attenuator_data)`). The agent's research initially claimed RX
doesn't need the inversion; spot-check shows mi0bot does invert RX too.
Need to verify whether NereusSDR's HL2 RX-path step attenuator has the
same bug. **Out of scope for this chunk's TX-only mandate**, but flagged
for follow-up issue.

**Cite:** `// From mi0bot-Thetis console.cs:10657-10658, 19164-19165, 27814-27815 [@c26a8a4]`
**Author tag to preserve:** `// MI0BOT: Greater range for HL2`.

---

### 2.5 Area 5 — HL2 codec path vs Saturn G2 path

**mi0bot:** `cmaster.cs:514-518`:
`int txinid = cmaster.inid(1, 0); int txch = cmaster.chid(txinid, 0); cmaster.SetXcmInrate(txinid, 48000)`.
WDSP channel ID for TX is **universal** across all boards: `inid(1, 0) →
chid` resolves to TXA channel 1. HL2 uses identical TX channel setup as
Saturn — divergence is purely in the wire-codec layer
(`WriteMainLoop_HL2` vs `WriteMainLoop`), not in WDSP. Buffer sizes
(input 256, output 256, sample rate 48 kHz) per `cmaster.cs:497, :518`.
`diff -wB` of mi0bot vs upstream Thetis `cmaster.cs` shows zero
meaningful changes (whitespace-only).

**Upstream Thetis:** Identical cmaster.cs TX channel setup.

**NereusSDR:** `src/core/WdspEngine.cpp:373-491` (`createTxChannel`),
`:401` already cites `// type=1 (TX) — from cmaster.c:184 [v2.10.3.13]`.
Calls `createTxChannel(WDSP.id(1, 0))` matching Thetis exactly.

**Delta:** None. HL2 and Saturn share identical WDSP channel ID, sample
rate, and buffer setup. The protocol-layer divergence
(`WriteMainLoop_HL2`) is wire-format only and is already handled by
NereusSDR's codec polymorphism (`P1CodecStandard` vs `P1CodecHl2`).

**Action:** **(c) no action needed.** Document in 3M-1c that HL2 uses the
same TXA channel ID 1 as Saturn.

---

### 2.6 Area 6 — HL2 firmware quirks

mi0bot has multiple HL2-only TX-affecting quirks:

1. **CWX bit-3 (`cwx_ptt`) on I-low byte** — `networkproto1.c:1247-1252`.
   HL2 path adds bit 3 under `cw_enable && j == 1 && HL2`:
   `temp = (cwx_ptt << 3 | dot << 2 | dash << 1 | cwx) & 0b00001111`.
   Non-HL2 boards send only bits 0-2 (mask `0b00000111`). Inline tag
   `// MI0BOT: Bit 3 in HL2 is used to signal PTT for CWX`.
2. **HL2 6-bit step ATT encoding** — `networkproto1.c:1099-1102`. C4 byte
   uses 6-bit mask + `0x40` enable, vs standard 5-bit + `0x20`.
3. **HL2 PS rate matching** — `console.cs:8476-8485`. When
   `puresignal_run` is on, HL2 forces `Rate[0] = rx1_rate, Rate[1] =
   rx1_rate` instead of standard `ps_rate` (192 kHz). Inline comment:
   `"HL2 can work at a high sample rate"`.
4. **`LRAudioSwap(1)` on HL2 init** — `clsHardwareSpecific.cs:91, 98`.
   Called for HL2 (and Hermes) but not higher boards. Swaps L/R in TX
   audio sub-frame.
5. **HL2 audio recording branch** — `console.cs:23895`. Receiver audio
   source mapping diverges for HL2.

**Upstream Thetis:** None of these quirks exist upstream.
`WriteMainLoop_HL2` is mi0bot-only.

**NereusSDR:**
- ✅ **CWX LSB-clear** (different from `cwx_ptt` — applies to ALL CW on
  HL2): `tests/tst_p1_tx_iq_wire.cpp:264-298` tests `iLoMask = 0xFE /
  qLoMask = 0xFE` for `HERMESLITE`. `src/core/P1RadioConnection.cpp:973-979`
  implements the `0xFE` mask gated on `model == HERMESLITE`.
- ✅ **HL2 6-bit step ATT encoding**: `P1CodecHl2.cpp:155-157`.
- ❌ **CWX bit-3 (`cwx_ptt`)** — not yet wired. 3M-2 (CW TX) territory.
- ❌ **HL2 PS rate matching** — not wired. 3M-1 doesn't enable
  PureSignal; 3M-4 territory.
- ❌ **`LRAudioSwap` for HL2** — not wired. `IoBoardHl2.h:245` mentions
  `swap_audio_channels` as a comment but no setter exists.
- ❌ **HL2 audio recording branch** — TX recording deferred to 3M-6.

**Delta:** Three HL2 quirks deferred (CWX bit-3, PS rate, audio-record
branch) are correctly out of 3M-1c scope. **`LRAudioSwap` is the
question mark** — mi0bot's `LRAudioSwap(1)` defaults ON for HL2 from
boot, suggesting it IS needed by HL2 firmware. If NereusSDR has been
bench-testing without swap and audio sounds correct via L/R PC mic, no
swap is needed; if HL2 transmits silence on USB but works on LSB,
swap is needed.

**Action:** **(b) for the LRAudioSwap question:** file as a 3M-1c
follow-up — defer concrete implementation to bench-test verification
rather than blind implementation. **(b) for CWX bit-3 (3M-2 follow-up).**
**(b) for HL2 PS rate (3M-4 follow-up).** **(c) for HL2 audio-recording
branch** — already captured by 3M-6 deferral.

**Cite (if LRAudioSwap is implemented):**
`// From mi0bot-Thetis clsHardwareSpecific.cs:98 [@c26a8a4] — LRAudioSwap(1) at HL2 init`
`// From mi0bot-Thetis ChannelMaster/networkproto1.c:1231-1239 [@c26a8a4]`

---

### 2.7 Area 7 — HL2 power scaling **[CRITICAL — absorb]**

**mi0bot:** `console.cs:25195-25199` (`computeRefPower`),
`:25269-25273` (`computeAlexFwdPower`):
```csharp
case HPSDRModel.HERMESLITE:         // MI0BOT: HL2
    bridge_volt = 1.5f;
    refvoltage = 3.3f;
    adc_cal_offset = 6;
    break;
```
Computation: `volts = (adc - 6) / 4095.0f * 3.3f`,
`watts = volts * volts / 1.5f`.

mi0bot's `convertToAmps` (`console.cs:25121-25131`) also has HL2-specific
PA current calculation:
`amps = ((3.26f * (IOreading / 4096.0f)) / 50.0f) / 0.04f / (1000.0f / 1270.0f)`.
Sense resistor 0.04 Ω, 50× gain, 3.26 V ref, divider compensation.

**Upstream Thetis:** `console.cs:25008-25072` (`computeAlexFwdPower`):
**No `HERMESLITE` case** — falls to default `{0.09f, 3.3f, 6}` at lines
25049-25053. Same default in `computeRefPower`. **Upstream Thetis is
broken for HL2 PA power readings.**

**NereusSDR:** `src/core/safety/safety_constants.cpp:78-101`
(`paScalingFor`): **No `HERMESLITE` case** — falls to default
`{0.09f, 3.3f, 6}` at lines 99-100. NereusSDR matches upstream Thetis
exactly, **which means HL2 fwd-power readings will be ~16× too high**
(HL2 should use `bridge_volt = 1.5f`, not `0.09f`). NereusSDR also
lacks the `convertToAmps` HL2 path entirely.

**Delta — safety bug:** A real HL2 5 W output would compute as
`5 × (1.5 / 0.09) ≈ 83 W` — vastly over the SwrProtectionController
threshold, causing **spurious TX inhibits** OR (worse) incorrect SWR
safety state. PA current readings (`convertToAmps`) are also wrong for
HL2.

**Action:** **(a) absorb into 3M-1c chunk 0 fixup or chunk 7.** Add
`case HPSDRModel::HERMESLITE: return { 1.5f, 3.3f, 6 };` to
`safety_constants.cpp:78-101`. PA-current `convertToAmps` is a meter
display concern (3M-3 territory), but the SAFETY pathway must be correct
for fwd-power → SWR computation now.

**Cite:** `// From mi0bot-Thetis console.cs:25269-25273 [@c26a8a4] (computeAlexFwdPower HL2 case)`
**Author tag to preserve:** `// MI0BOT: HL2`.

---

### 2.8 Area 8 — HL2 PTT path

**mi0bot:** `ChannelMaster/networkproto1.c:1093-1097` (case 11 in
`WriteMainLoop_HL2`). PTT bit encoding **identical to upstream** —
`mic_ptt` is bit 6 of C1 in bank 11. mi0bot adds an HL2-specific CWX PTT
bit (bit 3 of I-low byte at line 1249) which is a separate PTT path for
CWX. mi0bot also adds `SetCWXPTT` extern (`NetworkIOImports.cs:321`)
which routes to a netInterface.c function setting `prn->tx[0].cwx_ptt`.
The `mic_ptt` extraction (received PTT signal from radio) is **generic
and not HL2-specific** in either fork — read from inbound EP6 status
frame, dispatched the same way.

**Upstream Thetis:** Identical bank 11 wire format. No `SetCWXPTT`
extern, no `cwx_ptt` field. Dot/dash/cwx bits in I-low byte are 3 bits
only.

**NereusSDR:** `src/core/MoxController.h:484-490` documents the `mic_ptt`
extraction path: `bool mic_ptt = (dotdashptt & 0x01) != 0`. 3M-1b Task
H.5 implemented this. `src/core/RadioConnection.h:202-227` documents
`mic_trs` polarity inversion. Outbound PTT (mic_ptt bit 6 of C1) is in
`P1CodecHl2.cpp:149` with INVERTED polarity (`!ctx.p1MicPTT ? 0x40 : 0x00`).

**Delta:** None for voice PTT semantics (3M-1c scope). mi0bot's `cwx_ptt`
(bit 3 of I-low byte) is a CWX-specific outbound PTT for HL2 firmware to
acknowledge — deferred to 3M-2 (CW TX). NereusSDR's H.5 work matches.

**Action:** **(c) no action for 3M-1c.** **(b) optional:** file
`cwx_ptt` HL2 bit-3 wiring as a 3M-2 follow-up.

---

### 2.9 Area 9 — HL2-specific patches not upstream (TX-relevant inventory)

mi0bot patches that affect TX and are NOT in upstream Thetis (TX-relevant
only — RX-only and PureSignal-only patches excluded):

| # | mi0bot file:line | Patch | Author tag | NereusSDR status |
|---|---|---|---|---|
| 1 | `HPSDR/IoBoardHl2.cs:1-204` (whole file) | HL2 I/O board class for GPIO/I2C accessory control | Reid Campbell, MI0BOT, mi0bot@trom.uk | ✅ ported as `src/core/IoBoardHl2.{h,cpp}` (3L) |
| 2 | `clsHardwareSpecific.cs:94-100` | `HERMESLITE` case: `SetRxADC(1), SetMKIIBPF(0), SetADCSupply(0,33), LRAudioSwap(1)` | (no inline tag) | ✅ functionally covered by NereusSDR `BoardCapabilities`, except LRAudioSwap (Area 6) |
| 3 | `clsHardwareSpecific.cs:295/303` | `PSDefaultPeak = 0.233` for HermesLite | (mi0bot only) | n/a (3M-4 PureSignal) |
| 4 | `clsHardwareSpecific.cs:766-795` | `gains[]` table for HL2 per-band reference gains (PA bridge calibration) | (mi0bot only) | ❓ unclear — flag for 3M-1c review |
| 5 | `console.cs:10657-10658, 19164-19165, 27814-27815` | TX att inversion `31 − txatt` | `// MI0BOT: Greater range for HL2` | ❌ **MISSING — Area 4 absorb** |
| 6 | `console.cs:25195-25199, 25269-25273` | PA bridge_volt/refvoltage/adc_cal_offset for HL2 | `// MI0BOT: HL2` | ❌ **MISSING — Area 7 absorb** |
| 7 | `console.cs:29245-29274` | HL2 power slider 15-step UX (`(Math.Round(drv / 6.0) / 2) - 7.5 dB`) | `// MI0BOT: HL2 has only 15 output power levels` | ❌ deferred to 3M-3 follow-up |
| 8 | `console.cs:29473-29494, 29508-29528` | HL2 mic-slider repurposed as VAC TX gain | `// MI0BOT: For HL2 Audio control is based on VFO and Mode` | ✅ NereusSDR diverges intentionally (Area 2) |
| 9 | `networkproto1.c:1162-1176` | Banks 17 (TX latency, PTT hang) + 18 (Reset on disconnect) | (mi0bot only) | ✅ already in `P1CodecHl2.cpp:188-204` |
| 10 | `networkproto1.c:1247-1252` | CWX bit-3 (`cwx_ptt`) for HL2 | `// MI0BOT: Bit 3 in HL2 is used to signal PTT for CWX` | ❌ deferred to 3M-2 follow-up |
| 11 | `networkproto1.c:1099-1102` | HL2 6-bit step ATT encoding (mask 0x3F + 0x40 enable) | (mi0bot only) | ✅ already in `P1CodecHl2.cpp:155-157` |
| 12 | `NetworkIOImports.cs:321-405` | New externs: `SetCWXPTT`, `SetTxLatency`, `SetPttHang`, `SetResetOnDisconnect`, `SwapAudioChannels`, `I2CReadInitiate`, `I2CWriteInitiate`, `I2CWrite`, `I2CResponse` | `// MI0BIT` (typo in mi0bot source) and `// MI0BOT` | ✅ partial: hl2PttHang/hl2TxLatency/hl2ResetOnDisconnect in CodecContext |
| 13 | `Alex.cs:319-446` | HL2-specific antenna routing logic | `// MI0BOT: Alt RX has been requested or TX and Rx are not the same` | ❓ flag for follow-up — NereusSDR's 3P-I-a may already handle |
| 14 | `clsRadioDiscovery.cs:1153-1183` | HL2 discovery extra fields (`FixedIpHL2`, `EeConfigHL2`, beta version byte 21) | `// MI0BOT: Extra info from discovery for HL2` | ❌ deferred (not 3M scope) |

---

## 3. Categorised action plan

### 3.1 (a) Absorb into 3M-1c — 2 critical fixes

| # | Area | Fix | Target chunk | Test |
|---|---|---|---|---|
| A1 | Area 4 | Add HL2-specific `31 − N` TX step attenuator inversion in `P1CodecHl2.cpp` | Inline fixup as part of chunk 7 codec adjacency (or chunk 0 if surfaced as standalone fixup commit) | Unit test asserting `out[4] = ((31 − userValue) & 0b00111111) | 0b01000000` for HL2 |
| A2 | Area 7 | Add `case HPSDRModel::HERMESLITE: return { 1.5f, 3.3f, 6 };` to `safety_constants.cpp:paScalingFor` | Same as A1 | Unit test asserting HL2 PA scaling returns `bridge_volt=1.5f, refvoltage=3.3f, adc_cal_offset=6` |

Both are **wire-format / safety bugs** that affect the most safety-critical
TX paths in 3M-1c (force-31-dB protection + SWR-foldback). Both are
1-line table additions plus one unit test each.

### 3.2 (b) File as separate follow-up issues

| # | Area | Item | Target phase |
|---|---|---|---|
| B1 | Area 4 (RX side) | RX-side `31 − N` inversion at `console.cs:11075/11251/19380` — verify NereusSDR HL2 RX-path step attenuator | 3M-1c follow-up (RX, not TX) — issue title: "HL2 RX step attenuator may need 31 − N inversion" |
| B2 | Area 6 | `LRAudioSwap(1)` for HL2 — bench-test verification first; implement if HL2 TX audio is silent/swapped without it | 3M-1c bench follow-up — issue title: "Bench-verify HL2 needs LRAudioSwap on TX" |
| B3 | Area 6 | CWX bit-3 (`cwx_ptt`) on I-low byte for HL2 | 3M-2 (CW TX) — issue title: "HL2 CWX bit-3 cwx_ptt wire emission" |
| B4 | Area 6 | HL2 PS rate matching (rx1_rate instead of ps_rate) | 3M-4 (PureSignal) — issue title: "HL2 PureSignal sample-rate matching" |
| B5 | Area 9 #4 | `gains[]` table for HL2 per-band reference gains | 3M-1c follow-up — issue title: "HL2 per-band PA reference gains table" |
| B6 | Area 9 #7 | HL2 power slider 15-step UX | 3M-3 (TX processing) — issue title: "HL2 TX power slider 15-step quantisation" |
| B7 | Area 9 #13 | Alex.cs HL2 antenna routing patches — verify against NereusSDR 3P-I-a | 3M-1c follow-up audit — issue title: "Cross-check HL2 antenna routing vs mi0bot Alex.cs" |
| B8 | Area 9 #14 | Extended discovery fields (`FixedIpHL2`, `EeConfigHL2`, beta version) | post-3M (HL2 polish epic if surfaces) |

### 3.3 (c) Known-divergence with rationale

| # | Area | Divergence | Rationale |
|---|---|---|---|
| C1 | Area 1 | NereusSDR uses 32× ring buffer + Qt6 timer pacer; mi0bot uses Windows semaphore-driven cadence | NereusSDR's design is appropriate for cross-platform Qt6; semantic match on wire bytes |
| C2 | Area 2 | NereusSDR force-PC mic source on HL2 + dedicated VAC TX gain control; mi0bot overloads mic-gain slider as VAC gain on HL2 | NereusSDR's `CompositeTxMicRouter` is intentionally cleaner |
| C3 | Area 3 | None — NereusSDR matches mi0bot watchdog + bank 18 reset-on-disconnect | n/a |
| C4 | Area 5 | None — HL2 and Saturn use identical TXA channel ID / sample rate / buffer | TXA is board-agnostic |
| C5 | Area 8 | None — voice PTT semantics identical | `cwx_ptt` is 3M-2 territory |

### 3.4 Total tally

- **14 distinct mi0bot HL2 TX patches** vs upstream Thetis
- **7 already handled** by NereusSDR (Banks 17/18, HL2 6-bit ATT mask, mic-source force-PC, CWX LSB-clear, watchdog state, IoBoardHl2 I2C, Bank 11 polarity inversions)
- **2 require 3M-1c absorption** (TX att inversion + PA scaling)
- **8 follow-up issues to file** (some 3M-1c bench, some 3M-2/3/4 epics)
- **5 known-divergences** with rationale documented

---

## 4. Highest-priority finding

**Area 4 — HL2 TX step attenuator missing `31 − N` inversion** is the
ship-blocker. Reasoning:

- It's a **wire-format semantic bug**, not a missing feature.
- The force-31-dB code in `StepAttenuatorController::shouldForce31Db` is
  the most safety-critical TX path in 3M-1c — it protects the PA during
  CW TX (CWL/CWU) and PureSignal-off configurations.
- With the current code, the value we send when we **most** want PA
  protection (`txAtt = 31`) is the value HL2 firmware interprets as
  **least** protection (zero attenuation, full drive).
- The fix is a 1-line table addition + 1 unit test.

**Area 7 — HL2 PA scaling** is second priority. Spurious SWR-protect TX
inhibits won't damage hardware on their own but will surface as "HL2 TX
keeps cutting out" complaints. Same 1-line + 1-test profile.

Both should land as a **chunk 0 fixup commit** before the chunks 1-9
implementation begins, OR as the first task in the implementation plan
written next. They're small enough that splitting chunk 0 off as its own
pre-3M-1c branch (per design spec §9 risk row) is **not** warranted.

---

## 5. Open questions

1. **RX-side `31 − N` inversion** — mi0bot's `console.cs:11075/11251/19380`
   apply the same inversion to RX. Need to verify whether NereusSDR's
   HL2 RX-path step attenuator has the same bug. **Out of scope for
   this chunk's TX-only mandate** but flagged for follow-up issue B1.
2. **`gains[]` table for HL2 per-band reference gains** —
   `clsHardwareSpecific.cs:766-795`. Whether NereusSDR's PA scaling
   needs per-band variation on HL2 (in addition to the global
   `bridge_volt = 1.5f` from Area 7) needs a closer read. Flag B5.
3. **Alex.cs HL2 antenna routing** —
   `mi0bot-Thetis Alex.cs:319-446` has HL2-specific patches.
   NereusSDR 3P-I-a (PR #116) may already cover this; needs cross-check.
   Flag B7.
4. **Author tag corpus** — mi0bot's HL2 patches use mostly `// MI0BOT:`
   tags, with one typo `// MI0BIT` in `NetworkIOImports.cs`. The
   inline-tag preservation verifier needs to recognise both. Confirm
   `discover-mi0bot-thetis-author-tags.py` (if created) handles the
   typo gracefully.

---

## 6. Recommended next steps

1. **Land the two absorption fixes (A1 + A2) as a small fixup commit**
   on `feature/phase3m-1c-polish-persistence`, before writing the
   pre-code Thetis review for chunks 1-9. This keeps the chunks 1-9
   implementation clean and gets the safety-critical fixes into the
   3M-1c PR.
2. **File the 8 follow-up issues** (B1–B8) on GitHub. They become
   tracked work that lands in the right phase (some 3M-1c bench, some
   3M-2/3/4 epics).
3. **Skip the formal `MI0BOT-THETIS-PROVENANCE.md` setup** for now —
   only required if we port code from mi0bot. The two absorption fixes
   reference mi0bot lines but transcribe values (a 1-line table entry
   isn't a port). If the LRAudioSwap follow-up (B2) ends up porting
   code, set up the PROVENANCE file at that time.
4. **Continue brainstorming → pre-code Thetis review → plan → TDD**
   for chunks 1-9 per the design spec discipline section §7.

---

## 7. References

### NereusSDR

- Design spec: [`phase3m-1c-polish-persistence-design.md`](phase3m-1c-polish-persistence-design.md)
- 3M-0 safety foundation plan: [`phase3m-0-pa-safety-foundation-plan.md`](phase3m-0-pa-safety-foundation-plan.md)
- 3M-1a plan: [`phase3m-1a-tune-only-first-rf-plan.md`](phase3m-1a-tune-only-first-rf-plan.md)
- 3M-1b post-code review: [`phase3m-1b-post-code-review.md`](phase3m-1b-post-code-review.md)
- Step attenuator wiring (Phase 3G-13)
- Attribution / source-first protocol: [`docs/attribution/HOW-TO-PORT.md`](../attribution/HOW-TO-PORT.md)

### Upstream

- mi0bot-Thetis: `https://github.com/mi0bot/Thetis` @ `v2.10.3.13-beta2` / `@c26a8a4` (cloned at `/Users/j.j.boyd/mi0bot-Thetis`)
- Thetis: `https://github.com/ramdor/Thetis` @ `v2.10.3.13` / `@501e3f51` (cloned at `/Users/j.j.boyd/Thetis`)

### Key mi0bot files cited

- `Project Files/Source/Console/HPSDR/IoBoardHl2.cs` (HL2 I/O board)
- `Project Files/Source/Console/HPSDR/NetworkIO.cs`
- `Project Files/Source/Console/HPSDR/NetworkIOImports.cs` (new externs)
- `Project Files/Source/Console/HPSDR/clsRadioDiscovery.cs` (HL2 discovery)
- `Project Files/Source/Console/HPSDR/Alex.cs` (HL2 antenna routing)
- `Project Files/Source/Console/clsHardwareSpecific.cs` (HL2 init)
- `Project Files/Source/Console/console.cs` (HL2 power slider, TX att inversion, PA scaling)
- `Project Files/Source/ChannelMaster/networkproto1.c` (HL2 wire format, banks 17/18)
- `Project Files/Source/Console/cmaster.cs` (TXA channel setup — board-agnostic)

---

End of HL2 TX path review. Two ship-blocking fixes surfaced; both are
1-line table additions. Eight follow-up issues to file. Five
known-divergences documented. NereusSDR is **closer** to mi0bot than
expected — 7 of 14 HL2 patches already handled, due to the source-first
discipline applied during 3M-1a / 3M-1b.
