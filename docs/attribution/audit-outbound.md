# Outbound audit — self-declared Thetis derivations

Intermediate work product feeding `THETIS-PROVENANCE.md`. Removed after
PROVENANCE.md is finalized in Task 6.

**Total derived files identified: 151** (from Step 1 grep)

Grep command (from the task spec):
```
grep -rlnE "From Thetis|Porting from|Thetis Project Files|mi0bot|MW0LGE|W5WC|clsHardwareSpecific|MeterManager\.cs|console\.cs|setup\.cs|NetworkIO\.cs|cmaster\.cs|display\.cs|RXA\.c|TXA\.c|clsRadioDiscovery" src/ tests/
```

Count is above the expected 90-130 range because this branch's
post-3G-6 MeterManager.cs port exploded the meter item surface, and
3G-10 stage 2 added per-slice DSP parity tests that each cite Thetis.
Every entry below is a genuine derivation — no false positives.

## Summary by variant

- **thetis-samphire**: 110 files
- **thetis-no-samphire**: 2 files
- **mi0bot**: 8 files
- **multi-source**: 31 files

Total: 151. (variant judging rules per task spec §"Judging template variant".)

Two of the `thetis-samphire` entries are parenthesized with a
"(by convention — …)" qualifier because they're not code derivations
but attribution/test files that strongly touch Samphire-authored
material (AboutDialog.cpp lists him as a contributor; tst_about_dialog.cpp
verifies that listing).

## Canonical Thetis copyright blocks (cited sources)

The per-file entries below name Thetis source paths. Their verbatim copyright
blocks (extracted from `/Users/j.j.boyd/Thetis/...`) collapse to these
distinct patterns:

| Thetis source | Contributors in header | Samphire? |
|---|---|---|
| `Project Files/Source/Console/console.cs` | FlexRadio (2004-2009), Doug Wigley (2010-2020), Chris Codella W2PA (2017-2019 mods), Richard Samphire MW0LGE (2019-2026, "modified heavily") | yes |
| `Project Files/Source/Console/setup.cs` | FlexRadio (2004-2009), Doug Wigley (2010-2020), Richard Samphire MW0LGE (2019-2026 continual modifications) | yes |
| `Project Files/Source/Console/radio.cs` | FlexRadio (2004-2009), Doug Wigley (2010-2020), Richard Samphire (2019-2026) | yes |
| `Project Files/Source/Console/dsp.cs` | Warren Pratt NR0V (2013-2017) + Samphire dual-license block | yes |
| `Project Files/Source/Console/enums.cs` | Original authors (2000-2025), Richard Samphire MW0LGE (2020-2025) | yes |
| `Project Files/Source/Console/display.cs` | FlexRadio (2004-2009), Doug Wigley W5WC (2010-2020), Phil Harman VK6APH (2013 Waterfall AGC), Richard Samphire MW0LGE (2020-2025 DirectX transitions) | yes |
| `Project Files/Source/Console/MeterManager.cs` | Richard Samphire MW0LGE (2020-2026) — sole author | yes |
| `Project Files/Source/Console/ucMeter.cs` | Richard Samphire MW0LGE (2020-2025) — sole author | yes |
| `Project Files/Source/Console/cmaster.cs` | Original authors (2000-2025), Richard Samphire MW0LGE (2020-2025) | yes |
| `Project Files/Source/Console/rxa.cs` | (no explicit header — inherits Thetis project blanket; Samphire is continual maintainer) | yes (by project inheritance) |
| `Project Files/Source/Console/xvtr.cs` | FlexRadio (2004-2009), Doug Wigley (2010-2013) + Samphire dual-license block | yes |
| `Project Files/Source/Console/clsHardwareSpecific.cs` | Richard Samphire MW0LGE (2020-2025) — sole author | yes |
| `Project Files/Source/Console/clsDiscoveredRadioPicker.cs` | Richard Samphire MW0LGE (2020-2026) — sole author | yes |
| `Project Files/Source/Console/frmAddCustomRadio.cs` | Richard Samphire MW0LGE (2020-2026) — sole author | yes |
| `Project Files/Source/Console/frmMeterDisplay.cs` | Richard Samphire MW0LGE (2020-2025) — sole author | yes |
| `Project Files/Source/Console/HPSDR/specHPSDR.cs` | Doug Wigley (2010-2018) only — NO Samphire | no |
| `Project Files/Source/Console/HPSDR/NetworkIO.cs` | (no explicit header — inherits Thetis project blanket; Samphire is continual maintainer) | yes (by project inheritance) |
| `Project Files/Source/Console/HPSDR/clsRadioDiscovery.cs` | Richard Samphire MW0LGE (2020-2026) — sole author | yes |
| `Project Files/Source/Console/console.resx` | (auto-generated resource file — no copyright block; inherits console.cs blanket) | yes (by inheritance) |
| `Project Files/Source/ChannelMaster/network.c` | Doug Wigley W5WC (2015-2020) — LGPL — NO Samphire | no |
| `Project Files/Source/ChannelMaster/network.h` | Doug Wigley W5WC (2015-2020) — GPL — NO Samphire | no |
| `Project Files/Source/ChannelMaster/networkproto1.c` | Doug Wigley W5WC (2020) — LGPL — NO Samphire | no |
| `Project Files/Source/ChannelMaster/cmaster.c` | Warren Pratt NR0V (2014-2019) — NO Samphire | no |
| `Project Files/Source/ChannelMaster/netInterface.c` | Bill Tracey KD5TFD (2006-2007), Doug Wigley W5WC (2010-2020) — LGPL — NO Samphire | no |
| `Project Files/Source/ChannelMaster/bandwidth_monitor.{c,h}` | Richard Samphire MW0LGE (2025) — sole author | yes |
| `Project Files/Source/wdsp/wisdom.c` | Warren Pratt NR0V (2013-2025) — NO Samphire | no |
| mi0bot `HPSDR/IoBoardHl2.cs` | NOT LOCALLY AVAILABLE — mi0bot/Thetis-HL2 fork addition; upstream Thetis chain (FlexRadio / Wigley / Samphire) applies, plus Reid Campbell MI0BOT for fork contributions | inherited |
| mi0bot `bandwidth_monitor.{c,h}` fork copy | (see Thetis copy above — Samphire 2025 sole author) | yes |

---

## src/core/BoardCapabilities.cpp

- **Thetis sources cited:**
  - `Project Files/Source/Console/clsHardwareSpecific.cs` — SetRxADC, HasAudioAmplifier, model→hardware map (lines 85-184, 143-190 passim)
  - `Project Files/Source/Console/setup.cs` — HL 384k, step attenuator maxima (lines 850-853, 15765, 16085-16099)
  - `Project Files/Source/Console/console.cs` — GetDDC, SetComboPreampForHPSDR, PureSignal paths (lines 8378-8734, 30276-30277, 40755-40825)
  - `Project Files/Source/Console/enums.cs` — HPSDRModel / HPSDRHW integer values (lines 109, 388-398)
  - `Project Files/Source/Console/HPSDR/NetworkIO.cs` — DeviceType fw check (lines 136-143)
  - `Project Files/Source/Console/clsDiscoveredRadioPicker.cs` — FW version label (line 305)
  - `Project Files/Source/ChannelMaster/network.h` — HPSDRHW enum values (lines 446-456)
  - mi0bot/Thetis-HL2 branch for HL2 IoBoard specifics (noted explicitly as mi0bot)
- **Thetis copyright (from the cited source files):**
  - FlexRadio Systems (2004-2009) — via console.cs, setup.cs
  - Doug Wigley W5WC (2010-2020) — via console.cs, setup.cs, network.h
  - Richard Samphire MW0LGE (2019-2026) — via console.cs, setup.cs, clsHardwareSpecific.cs, clsDiscoveredRadioPicker.cs, enums.cs
  - Reid Campbell MI0BOT — mi0bot/Thetis-HL2 fork (HL2-specific branches)
- **Derivation type:** port (comprehensive capability registry)
- **Template variant:** multi-source
- **Notes:** synthesizes multiple Thetis headers including the Samphire-clean `network.h` / `network.c` / `cmaster.c` (LGPL, W5WC / NR0V only), and explicitly cites mi0bot fork content.

## src/core/BoardCapabilities.h

- **Thetis sources cited:**
  - `Project Files/Source/Console/clsHardwareSpecific.cs`
  - `Project Files/Source/Console/setup.cs`
  - `Project Files/Source/Console/console.cs` (SetComboPreampForHPSDR lines 40755-40825)
  - `Project Files/Source/Console/HPSDR/specHPSDR.cs`
  - `Project Files/Source/ChannelMaster/network.h`
- **Thetis copyright (from the cited source files):**
  - FlexRadio Systems (2004-2009), Doug Wigley (2010-2020), Richard Samphire MW0LGE (2019-2026) — via console.cs/setup.cs/clsHardwareSpecific.cs
  - Doug Wigley (2010-2018) only — via specHPSDR.cs (no Samphire)
  - Doug Wigley W5WC (2015-2020) only — via network.h (no Samphire, LGPL adjacent to GPL)
  - Reid Campbell MI0BOT — mi0bot fork content (`clsHardwareSpecific.cs` lives in the ramdor/Thetis chain but `mi0bot/Thetis-HL2` adds the HL2 branches).
- **Derivation type:** port (data struct definition)
- **Template variant:** multi-source
- **Notes:** header file that mirrors the .cpp — same multi-source mix. Comment explicitly names "mi0bot/Thetis-HL2".

## src/core/ClarityController.h

- **Thetis sources cited:**
  - `Project Files/Source/Console/display.cs` — processNoiseFloor() at line 5866
- **Thetis copyright (from the cited source files):**
  - FlexRadio Systems (2004-2009)
  - Doug Wigley W5WC (2010-2020)
  - Phil Harman VK6APH (2013, Waterfall AGC modifications)
  - Richard Samphire MW0LGE (2020-2025 DirectX transitions & continual modifications)
- **Derivation type:** inspired-by (explicitly called "Lineage" — replaces algorithm)
- **Template variant:** thetis-samphire
- **Notes:** Clarity replaces Thetis processNoiseFloor with a percentile-based estimator. Still cites the Thetis algorithm as the starting point.

## src/core/FFTEngine.cpp

- **Thetis sources cited:**
  - `Project Files/Source/Console/display.cs` — line 2842 (display data initial value -200 dBm)
- **Thetis copyright (from the cited source files):**
  - FlexRadio Systems (2004-2009)
  - Doug Wigley W5WC (2010-2020)
  - Phil Harman VK6APH (2013)
  - Richard Samphire MW0LGE (2020-2025)
- **Derivation type:** port (constant reference only)
- **Template variant:** thetis-samphire

## src/core/FFTEngine.h

- **Thetis sources cited:**
  - `Project Files/Source/Console/display.cs` — line 215 (BUFFER_SIZE = 16384), also "WDSP SetAnalyzer window options" by allusion
- **Thetis copyright (from the cited source files):**
  - FlexRadio Systems (2004-2009), Doug Wigley W5WC (2010-2020), Phil Harman VK6APH (2013), Richard Samphire MW0LGE (2020-2025)
- **Derivation type:** port (constant + enum mapping)
- **Template variant:** thetis-samphire

## src/core/HardwareProfile.cpp

- **Thetis sources cited:**
  - `Project Files/Source/Console/clsHardwareSpecific.cs` — lines 85-184 (model→profile mapping)
  - `Project Files/Source/Console/HPSDR/NetworkIO.cs` — lines 164-171 (compatibility list)
- **Thetis copyright (from the cited source files):**
  - Richard Samphire MW0LGE (2020-2025) — sole author of clsHardwareSpecific.cs
  - NetworkIO.cs has no explicit header — inherits Thetis project blanket
- **Derivation type:** port
- **Template variant:** thetis-samphire
- **Notes:** header explicitly names "mi0bot/Thetis" prefix, but the cited paths exist in ramdor/Thetis proper. Upstream Thetis copyright used.

## src/core/HardwareProfile.h

- **Thetis sources cited:**
  - `Project Files/Source/Console/clsHardwareSpecific.cs` — lines 85-184
  - `Project Files/Source/Console/HPSDR/NetworkIO.cs` — lines 164-171
- **Thetis copyright (from the cited source files):**
  - Richard Samphire MW0LGE (2020-2025) — via clsHardwareSpecific.cs
  - NetworkIO.cs inherits project blanket.
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/core/HpsdrModel.h

- **Thetis sources cited:**
  - `Project Files/Source/Console/enums.cs` — line 109 (HPSDRModel), line 388 (HPSDRHW)
  - `Project Files/Source/ChannelMaster/network.h` — line 446 (HPSDRHW in C)
- **Thetis copyright (from the cited source files):**
  - Original authors 2000-2025, Richard Samphire MW0LGE (2020-2025) — via enums.cs
  - Doug Wigley W5WC (2015-2020) only — via network.h (no Samphire)
- **Derivation type:** verbatim (enum integer values preserved exactly)
- **Template variant:** multi-source
- **Notes:** explicit `mi0bot/Thetis@Hermes-Lite` citation — but the cited files exist in ramdor/Thetis proper, mi0bot is the author's viewpoint.

## src/core/mmio/ExternalVariableEngine.h

- **Thetis sources cited:**
  - `Project Files/Source/Console/MeterManager.cs` — line 40831-40834 (MultiMeterIO static registry)
- **Thetis copyright:**
  - Richard Samphire MW0LGE (2020-2026) — sole author of MeterManager.cs
- **Derivation type:** port (Qt-native re-expression of Thetis subsystem)
- **Template variant:** thetis-samphire

## src/core/mmio/FormatParser.cpp

- **Thetis sources cited:**
  - `Project Files/Source/Console/MeterManager.cs` — lines 41344-41349 (RAW parser)
- **Thetis copyright:**
  - Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/core/mmio/FormatParser.h

- **Thetis sources cited:**
  - `Project Files/Source/Console/MeterManager.cs` — lines 41325-41422 (JSON / XML / RAW parser family)
- **Thetis copyright:**
  - Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/core/mmio/MmioEndpoint.h

- **Thetis sources cited:**
  - `Project Files/Source/Console/MeterManager.cs` — clsMMIO class, lines 40320-40353 (payload format enum)
- **Thetis copyright:**
  - Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/core/mmio/SerialEndpointWorker.cpp

- **Thetis sources cited:**
  - `Project Files/Source/Console/MeterManager.cs` — lines 40589-40816 (SerialPortHandler), 40630-40660 (Open), 40700-40760 (DataReceived)
- **Thetis copyright:**
  - Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/core/mmio/SerialEndpointWorker.h

- **Thetis sources cited:**
  - `Project Files/Source/Console/MeterManager.cs` — lines 40589-40816
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/core/mmio/TcpClientEndpointWorker.cpp

- **Thetis sources cited:**
  - `Project Files/Source/Console/MeterManager.cs` — lines 40160-40403 (TcpClientHandler), 40180 (reconnect delay), 40320-40353 (newline framing)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/core/mmio/TcpClientEndpointWorker.h

- **Thetis sources cited:**
  - `Project Files/Source/Console/MeterManager.cs` — lines 40160-40403
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/core/mmio/TcpListenerEndpointWorker.cpp

- **Thetis sources cited:**
  - `Project Files/Source/Console/MeterManager.cs` — lines 39899-40160 (TcpListener)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/core/mmio/TcpListenerEndpointWorker.h

- **Thetis sources cited:**
  - `Project Files/Source/Console/MeterManager.cs` — lines 39899-40160
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/core/mmio/UdpEndpointWorker.cpp

- **Thetis sources cited:**
  - `Project Files/Source/Console/MeterManager.cs` — line 40419 (UdpListener bind pattern)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/core/mmio/UdpEndpointWorker.h

- **Thetis sources cited:**
  - `Project Files/Source/Console/MeterManager.cs` — lines 40403-40588 (UdpListener)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/core/NoiseFloorEstimator.h

- **Thetis sources cited:**
  - `Project Files/Source/Console/display.cs` — processNoiseFloor() line 5866
- **Thetis copyright:** FlexRadio (2004-2009), Doug Wigley W5WC (2010-2020), Phil Harman VK6APH (2013), Richard Samphire MW0LGE (2020-2025)
- **Derivation type:** inspired-by (replaces the algorithm; cites Thetis for contrast)
- **Template variant:** thetis-samphire

## src/core/P1RadioConnection.cpp

- **Thetis sources cited:**
  - `Project Files/Source/ChannelMaster/networkproto1.c` — WriteMainLoop, MetisWriteFrame (lines 216-236, 597-864)
  - `Project Files/Source/Console/HPSDR/NetworkIO.cs` — VFOfreq, SetC0..SetC4, firmware refusal (lines 136-143, 215-223)
  - `Project Files/Source/Console/cmaster.cs` — RadioProtocol usage line 520
  - `Project Files/Source/Console/console.cs` — HermesII handling (lines 8378-8454)
  - mi0bot `HPSDR/IoBoardHl2.cs` — IOBoard.readRequest lines 83-145
  - mi0bot `bandwidth_monitor.{c,h}` — sequence-gap adaptation (explicitly noted "copyright MW0LGE")
- **Thetis copyright (from the cited source files):**
  - Doug Wigley W5WC (2020) — via networkproto1.c (LGPL)
  - NetworkIO.cs inherits Thetis blanket (FlexRadio/Wigley/Samphire)
  - cmaster.cs: Original authors + Samphire
  - console.cs: FlexRadio/Wigley/W2PA/Samphire
  - Reid Campbell MI0BOT — IoBoardHl2.cs (mi0bot fork)
  - Richard Samphire MW0LGE (2025) — bandwidth_monitor (ramdor/Thetis has it too now)
- **Derivation type:** port (faithful wire-format reproduction)
- **Template variant:** multi-source
- **Notes:** synthesizes LGPL ChannelMaster code (no Samphire) with Samphire-heavy console.cs/NetworkIO.cs AND mi0bot-only IoBoardHl2.cs + bandwidth_monitor. The mi0bot-specific pieces are called out; mi0bot source couldn't be directly opened for copyright extraction.

## src/core/P1RadioConnection.h

- **Thetis sources cited:**
  - `Project Files/Source/ChannelMaster/networkproto1.c`
  - `Project Files/Source/Console/HPSDR/NetworkIO.cs`
  - mi0bot `HPSDR/IoBoardHl2.cs` (lines 83-145)
  - mi0bot `bandwidth_monitor.{c,h}` (MW0LGE)
- **Thetis copyright (from the cited source files):**
  - Doug Wigley W5WC (2020) via networkproto1.c
  - NetworkIO.cs inherits Thetis blanket (FlexRadio/Wigley/Samphire)
  - Reid Campbell MI0BOT — mi0bot fork content
  - Richard Samphire MW0LGE — bandwidth_monitor
- **Derivation type:** port
- **Template variant:** multi-source

## src/core/P2RadioConnection.cpp

- **Thetis sources cited:**
  - `Project Files/Source/ChannelMaster/network.c` — nativeInitMetis, SendStart, SendStop, CmdGeneral, CmdRx, CmdTx, CmdHighPriority, ReadUDPFrame, ReadThreadMainLoop, sendPacket, KeepAliveLoop (lines 84-1440 passim)
  - `Project Files/Source/ChannelMaster/network.h` — `_radionet` struct (line 53)
  - `Project Files/Source/ChannelMaster/netInterface.c` — create_rnet (line 1416), StartAudioNative (line 83)
  - `Project Files/Source/Console/console.cs` — setAlexHPF, setAlexLPF, UpdateDDCs (lines 6830-7234, 8216-8241)
- **Thetis copyright (from the cited source files):**
  - Doug Wigley W5WC (2015-2020) — network.c, network.h (LGPL/GPL, no Samphire)
  - Bill Tracey KD5TFD (2006-2007) + Doug Wigley (2010-2020) — netInterface.c (LGPL, no Samphire)
  - FlexRadio/Wigley/W2PA/Samphire — console.cs
- **Derivation type:** port (faithful P2 wire-format reproduction)
- **Template variant:** multi-source
- **Notes:** primary code comes from the Samphire-clean LGPL ChannelMaster files; console.cs pulls band-filter + DDC logic. ChannelMaster files are *not* Samphire-authored.

## src/core/P2RadioConnection.h

- **Thetis sources cited:**
  - `Project Files/Source/ChannelMaster/network.c` / `network.h` (`_radionet` struct, line 53)
  - `Project Files/Source/ChannelMaster/netInterface.c` (line 1513 default sample rate)
  - `Project Files/Source/Console/console.cs` (setAlexHPF/LPF at 6830-7234)
- **Thetis copyright (from the cited source files):**
  - Doug Wigley W5WC (2015-2020) — network.c, network.h
  - Bill Tracey KD5TFD + Doug Wigley — netInterface.c
  - FlexRadio/Wigley/W2PA/Samphire — console.cs
- **Derivation type:** port
- **Template variant:** multi-source

## src/core/RadioDiscovery.cpp

- **mi0bot source cited:**
  - `HPSDR/clsRadioDiscovery.cs` — parseDiscoveryReply (lines 1145-1195 P1, 1201-1226 P2), DiscoverUsingAllNics, discoverOnNic, buildDiscoveryPacketP1/P2, applyScanPerformanceProfile
- **Copyright (from cited source file `/Users/j.j.boyd/Thetis/Project Files/Source/Console/HPSDR/clsRadioDiscovery.cs`):**
  - Richard Samphire MW0LGE (2020-2026) — sole author in ramdor/Thetis upstream
  - In mi0bot fork, assumed to include Reid Campbell MI0BOT for fork-specific modifications
- **Derivation type:** port (NIC-walk + tunable timing profiles)
- **Template variant:** mi0bot
- **Notes:** NereusSDR comment says "mi0bot/Thetis@Hermes-Lite clsRadioDiscovery.cs" — the file exists identically (or nearly so) in ramdor/Thetis proper under HPSDR/. mi0bot source couldn't be directly opened for copyright extraction; ramdor/Thetis upstream used as copyright-block reference.

## src/core/RadioDiscovery.h

- **mi0bot source cited:**
  - `HPSDR/clsRadioDiscovery.cs` — timing guide (lines 49-70), applyScanPerformanceProfile, parseDiscoveryReply
- **Copyright:**
  - Richard Samphire MW0LGE (2020-2026) — ramdor/Thetis upstream
  - Reid Campbell MI0BOT — mi0bot fork (assumed)
- **Derivation type:** port
- **Template variant:** mi0bot
- **Notes:** mi0bot source couldn't be directly opened; ramdor upstream used.

## src/core/ReceiverManager.cpp

- **Thetis sources cited:**
  - `Project Files/Source/Console/console.cs` — UpdateDDCs line 8216
- **Thetis copyright:** FlexRadio (2004-2009), Doug Wigley (2010-2020), Chris Codella W2PA (2017-2019), Richard Samphire MW0LGE (2019-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/core/ReceiverManager.h

- **Thetis sources cited:**
  - `Project Files/Source/Console/console.cs` — UpdateDDCs line 8216
- **Thetis copyright:** FlexRadio / Wigley / W2PA / Samphire
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/core/RxChannel.cpp

- **Thetis sources cited:**
  - `Project Files/Source/Console/radio.cs` — extensive: AGC/APF/squelch/NR/NB/panel ranges (lines 1037-2251 passim)
  - `Project Files/Source/Console/console.cs` — AGC Thresh/Top (lines 45977, 50350), SNB run flag (36347)
  - `Project Files/Source/Console/dsp.cs` — P/Invoke decls (lines 116-117, 393-394, 692-693, 243-297)
  - `Project Files/Source/Console/rxa.cs` — bp1/nbp0 usage (lines 110-111)
  - `Project Files/Source/Console/HPSDR/specHPSDR.cs` — SetEXTNOB* (lines 922-937)
  - `Project Files/Source/Console/setup.cs` — CW pitch freq (line 17071)
  - `Project Files/Source/Console/cmaster.c` — NB2 defaults (lines 55-68 in ChannelMaster/cmaster.c)
  - Thetis `wdsp-integration.md` design doc (section 4.2)
- **Thetis copyright (from the cited source files):**
  - FlexRadio / Doug Wigley / Samphire — via radio.cs, console.cs, setup.cs
  - Warren Pratt NR0V + Samphire dual-license — via dsp.cs
  - rxa.cs inherits Thetis blanket
  - Doug Wigley (2010-2018) only — via specHPSDR.cs (NO Samphire)
  - Warren Pratt NR0V (2014-2019) only — via cmaster.c (NO Samphire)
- **Derivation type:** port
- **Template variant:** multi-source
- **Notes:** one of the most synthesis-heavy files; pulls from Samphire, Wigley-only specHPSDR.cs, and NR0V-only cmaster.c.

## src/core/RxChannel.h

- **Thetis sources cited:**
  - `Project Files/Source/Console/radio.cs` (same range as .cpp)
  - `Project Files/Source/Console/console.cs`
  - `Project Files/Source/Console/dsp.cs`
  - `Project Files/Source/Console/HPSDR/specHPSDR.cs`
  - `Project Files/Source/ChannelMaster/cmaster.c` — "create_rcvr"
- **Thetis copyright:** as RxChannel.cpp above.
- **Derivation type:** port
- **Template variant:** multi-source

## src/core/StepAttenuatorController.cpp

- **Thetis sources cited:**
  - `Project Files/Source/Console/console.cs` — handleOverload (lines 21290-21763), pollOverloadSyncSeqErr, RX1AttenuatorData, comboPreamp_SelectedIndexChanged
- **Thetis copyright:** FlexRadio / Wigley / W2PA / Samphire (console.cs)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/core/StepAttenuatorController.h

- **Thetis sources cited:**
  - `Project Files/Source/Console/console.cs` — handleOverload (lines 21290-21763), PreampMode enum (21574-21586), overload level fields (21212-21224)
- **Thetis copyright:** FlexRadio / Wigley / W2PA / Samphire
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/core/wdsp_api.h

- **Thetis sources cited:**
  - `Project Files/Source/Console/dsp.cs` — P/Invoke declarations (lines 243-297, 393-394)
  - `Project Files/Source/Console/radio.cs` — default values and usage (lines 1145-2008 passim)
  - `Project Files/Source/Console/HPSDR/specHPSDR.cs` — SetEXTNOB* declarations (lines 922-937)
- **Thetis copyright (from the cited source files):**
  - Warren Pratt NR0V (2013-2017) + Samphire dual-license — via dsp.cs
  - FlexRadio / Wigley / Samphire — via radio.cs
  - Doug Wigley (2010-2018) only — via specHPSDR.cs (NO Samphire)
- **Derivation type:** verbatim (C API declarations)
- **Template variant:** multi-source

## src/core/WdspEngine.cpp

- **Thetis sources cited:**
  - `Project Files/Source/ChannelMaster/cmaster.c` — create_rcvr OpenChannel call (lines 72-86)
- **Thetis copyright:** Warren Pratt NR0V (2014-2019) — NO Samphire
- **Derivation type:** port
- **Template variant:** thetis-no-samphire

## src/core/WdspEngine.h

- **Thetis sources cited:**
  - `Project Files/Source/Console/cmaster.cs` — CMCreateCMaster (line 491)
  - `Project Files/Source/ChannelMaster/cmaster.c` — create_rcvr (lines 32-93)
- **Thetis copyright (from the cited source files):**
  - Original authors 2000-2025, Richard Samphire MW0LGE (2020-2025) — via cmaster.cs
  - Warren Pratt NR0V (2014-2019) — via cmaster.c (no Samphire)
- **Derivation type:** port
- **Template variant:** multi-source

## src/core/WdspTypes.h

- **Thetis sources cited:**
  - `Project Files/Source/Console/dsp.cs` — DSPMode and AGCMode enums
  - `Project Files/Source/Console/wdsp.cs` — rxaMeterType / txaMeterType (this is a mi0bot/Thetis-HL2 fork file — NOT in ramdor/Thetis)
  - `Project Files/Source/Console/setup.cs` — custom hang bucket reference
  - `Project Files/Source/Console/console.cs` — line 20873 FMTXMode.Simplex default
- **Thetis copyright (from the cited source files):**
  - Warren Pratt NR0V (2013-2017) + Samphire dual-license — via dsp.cs
  - FlexRadio / Wigley / Samphire — via setup.cs, console.cs
  - mi0bot `wdsp.cs` not locally available — but the enum values referenced are universal WDSP values
- **Derivation type:** verbatim (enum integer values)
- **Template variant:** multi-source
- **Notes:** `wdsp.cs` does not exist in the ramdor/Thetis clone at `/Users/j.j.boyd/Thetis/` — it is a mi0bot-specific filename. The values cited are the canonical WDSP API enum values and would match ramdor's `dsp.cs` DSPMode in practice.

## src/gui/AboutDialog.cpp

- **Thetis sources cited:** (indirectly)
  - Contributors array names: "Richie / MW0LGE / Thetis" (url `github.com/ramdor/Thetis`) plus "Reid Campbell / MI0BOT / OpenHPSDR-Thetis (HL2)" (url `github.com/mi0bot/OpenHPSDR-Thetis`).
- **Thetis copyright:** not a derivation — this file *credits* Thetis.
- **Derivation type:** attribution / credits
- **Template variant:** thetis-samphire (by convention — acknowledges Samphire as Thetis maintainer)
- **Notes:** technically not a port; flagged by grep because of "MW0LGE" string. Retaining in the audit so the header template run covers it.

## src/gui/AddCustomRadioDialog.cpp

- **Thetis sources cited:**
  - `Project Files/Source/Console/frmAddCustomRadio.cs` — default IP/port (line 60), designer defaults
  - `Project Files/Source/Console/frmAddCustomRadio.Designer.cs` — lines 64, 81-82 (defaults / combo items)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026) — sole author of frmAddCustomRadio.cs (Designer.cs inherits)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/applets/PhoneCwApplet.cpp

- **Thetis sources cited:**
  - `Project Files/Source/Console/setup.cs` — 38-tone CTCSS standard list
- **Thetis copyright:** FlexRadio / Wigley / Samphire (setup.cs)
- **Derivation type:** port (reference list)
- **Template variant:** thetis-samphire

## src/gui/applets/RxApplet.cpp

- **Thetis sources cited:**
  - `Project Files/Source/Console/console.cs` — comboPreamp / udRX1StepAttData, pbAutoAttWarningRX1, SetComboPreampForHPSDR (line 40755), AGC thresh (line 45977)
  - `Project Files/Source/Console/console.resx` — 8397 (ptbRF tooltip), 8277 (chkRxAnt), 8433 (ptbAF), 4554 (comboAGC), 4335 (chkRIT), 4416 (chkXIT), 5787 (chkVFOLock), 1560 (chkRX2Mute)
  - `Project Files/Source/Console/setup.cs` — udHermesStepAttenuatorData (line 15765)
- **Thetis copyright:** FlexRadio / Wigley / W2PA / Samphire (console.cs, setup.cs). console.resx auto-generated, inherits.
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/containers/ContainerManager.cpp

- **Thetis sources cited:**
  - `Project Files/Source/Console/MeterManager.cs` — many ranges: 5613-5673, 5812-5918, 6012-6105, 6391-6447, 6514-6579 (container lifecycle)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/containers/ContainerManager.h

- **Thetis sources cited:**
  - `Project Files/Source/Console/MeterManager.cs` — duplicate-container path
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/containers/ContainerSettingsDialog.cpp

- **Thetis sources cited:**
  - `Project Files/Source/Console/setup.cs` — lstMetersAvailable (lines 24522-24566), chkContainerHighlight (24443), container size values (24447)
  - `Project Files/Source/Console/MeterManager.cs` — size/layout constants (21266, 22472)
- **Thetis copyright:** FlexRadio / Wigley / Samphire (setup.cs); Richard Samphire MW0LGE (MeterManager.cs)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/containers/ContainerWidget.cpp

- **Thetis sources cited:**
  - `Project Files/Source/Console/ucMeter.cs` — extensive: lines 49-59 (axis enum), 281-294, 319-374, 400-407, 489-518, 520-572, 574-593, 912-935, 974-993, 1198-1229
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2025)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/containers/ContainerWidget.h

- **Thetis sources cited:**
  - `Project Files/Source/Console/ucMeter.cs` — axis enum (lines 49-59)
  - `Project Files/Source/Console/setup.cs` — chkContainerHighlight (24443), container values (24447)
  - `Project Files/Source/Console/MeterManager.cs` — ContainerMinimised runtime flag usage
- **Thetis copyright:** Samphire for ucMeter/MeterManager; FlexRadio/Wigley/Samphire for setup.cs
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/containers/FloatingContainer.cpp

- **Thetis sources cited:**
  - `Project Files/Source/Console/frmMeterDisplay.cs` — lines 114-179 (lifecycle, close, console state)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2025)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/containers/FloatingContainer.h

- **Thetis sources cited:**
  - `Project Files/Source/Console/frmMeterDisplay.cs` — lines 114-179
  - `Project Files/Source/Console/MeterManager.cs` — ContainerMinimised handler
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/containers/meter_property_editors/ScaleItemEditor.cpp

- **Thetis sources cited:**
  - `Project Files/Source/Console/MeterManager.cs` — clsScaleItem ShowType (line 14827)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/MainWindow.cpp

- **Thetis sources cited:**
  - `Project Files/Source/Console/MeterManager.cs` — AVG_SIGNAL_STRENGTH pattern
  - `Project Files/Source/Console/dsp.cs` — AGCMode / DSPMode enums
  - `Project Files/Source/Console/console.cs` — band definitions, ucInfoBar Warning(), pbAutoAttWarningRX1, SetupForm attenuator init
  - `Project Files/Source/Console/setup.cs` — line 24358 (btnAddRX1Container_Click)
  - `Project Files/Source/Console/radio.cs` — SetRXAShiftFreq (line 1417) (note: comment says `radio.rs` in one place, clearly a typo for `radio.cs`)
- **Thetis copyright (from the cited source files):**
  - Richard Samphire MW0LGE — MeterManager.cs
  - Warren Pratt NR0V + Samphire dual-license — dsp.cs
  - FlexRadio / Wigley / W2PA / Samphire — console.cs, setup.cs, radio.cs
- **Derivation type:** port / integration
- **Template variant:** multi-source

## src/gui/meters/AntennaButtonItem.cpp

- **Thetis sources cited:**
  - `Project Files/Source/Console/MeterManager.cs` — clsAntennaButtonBox (line 9502+)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/AntennaButtonItem.h

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsAntennaButtonBox (9502+)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/BandButtonItem.cpp

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsBandButtonBox (line 11482+)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/BandButtonItem.h

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsBandButtonBox (11482+), PopupBandstack (11896)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/ButtonBoxItem.cpp

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsButtonBox (12307+), layout constants
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/ButtonBoxItem.h

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsButtonBox (12307+), IndicatorType enum (12309-12327)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/ClickBoxItem.h

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsClickBox (7571+)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/ClockItem.h

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsClock (14075+)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/DataOutItem.h

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsDataOut (16047+)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/DialItem.cpp

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsDialDisplay (15399+), renderDialDisplay (33750-33899)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/DialItem.h

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsDialDisplay (15399+)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/DiscordButtonItem.h

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsDiscordButtonBox (11983+)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/FadeCoverItem.cpp

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsFadeCover (7665+), renderFadeCover (36292), FadeOnRx/Tx (7887-7888, 1900)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/FadeCoverItem.h

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsFadeCover (7665+), FadeOnRx/Tx (1900)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/FilterButtonItem.cpp

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsFilterButtonBox (7674+)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/FilterButtonItem.h

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsFilterButtonBox (7674+), PopupFilterContextMenu (7917)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/FilterDisplayItem.cpp

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsFilterItem (16852+), Update (18296+), _fill_spec (16936), _waterfall_frame_interval (16940+), FIDisplayMode (16865), FIWaterfallPalette, _edges_colour_rx/tx (16951-16952), _notch_colour (16957), MiniSpec.PIXELS (17006)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/FilterDisplayItem.h

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsFilterItem (16852+), enum definitions
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/HistoryGraphItem.cpp

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsHistoryItem (16149+), addReading (16468+)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port (with NereusSDR-local ring buffer divergence flagged in-body)
- **Template variant:** thetis-samphire

## src/gui/meters/HistoryGraphItem.h

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsHistoryItem (16149+)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/ItemGroup.cpp

- **Thetis sources cited:**
  - `Project Files/Source/Console/MeterManager.cs` — extensively: addSMeterBar (21499-21616), AddPWRBar (23862), AddSWRBar (23990), AddALCBar (23326-23411), AddMicBar (23025), AddCompBar (23681), AddADCBar (21740), AddADCMaxMag (21638-21640), AddEQBar (23091), AddLevelerBar (23179), AddLevelerGainBar (23265), AddALCGainBar (23412), AddALCGroupBar (23473), AddCFCBar (23534), AddCFCGainBar (23620), AddAnanMM (22461-22815), AddCrossNeedle (22817-23002), clsMagicEye, clsSignalText (20286+), clsHistoryItem (16149+)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port (comprehensive preset factories)
- **Template variant:** thetis-samphire
- **Notes:** largest outbound file — 35+ cited ranges in one file.

## src/gui/meters/ItemGroup.h

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — addSMeterBar (21523-21616), AddPWR/SWR, AddALCBar (23326-23411), AddAnanMM (22461-22815), AddCrossNeedle (22817-23002)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/LEDItem.cpp

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsLed (19448+), renderLed
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/LEDItem.h

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsLed (19448+), LedShape/LedStyle enums
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/MagicEyeItem.cpp

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsMagicEyeItem (15855+)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/MagicEyeItem.h

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsMagicEyeItem (15855+)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/MeterItem.cpp

- **Thetis sources cited:**
  - `Project Files/Source/Console/MeterManager.cs` — ReadingName (lines 2258-2318), clsBarItem (19917-20278), clsScaleItem (14827+, 32338+), attack/decay smoothing, addSMeterBar (21499-21616), renderHBar (35950-36140), AddAnanMM calibration tables, generalScale helper
  - `Project Files/Source/Console/console.cs` — edge meter colors (lines 12612-12678), 23589-23624 (bar draw edge)
- **Thetis copyright:**
  - Richard Samphire MW0LGE (2020-2026) — MeterManager.cs
  - FlexRadio / Wigley / W2PA / Samphire — console.cs
- **Derivation type:** port (verbatim ReadingName switch)
- **Template variant:** multi-source

## src/gui/meters/MeterItem.h

- **Thetis sources cited:**
  - `Project Files/Source/Console/MeterManager.cs` — ReadingName (2258-2318), clsBarItem fields/styles (19917-21616), clsScaleItem (14827+, 32338+), generalScale (32338-32423)
  - `Project Files/Source/Console/console.cs` — edge meter colors (12612-12678)
- **Thetis copyright:**
  - Samphire — MeterManager.cs
  - FlexRadio / Wigley / W2PA / Samphire — console.cs
- **Derivation type:** port
- **Template variant:** multi-source

## src/gui/meters/MeterPoller.h

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — Reading enum / TX meters
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/MeterWidget.cpp

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — container fixed-aspect (21266), per-item render gate (31366-31368)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/MeterWidget.h

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — visibility filter (31366-31368)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/ModeButtonItem.cpp

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsModeButtonBox (9951+)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/ModeButtonItem.h

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsModeButtonBox (9951+)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/NeedleScalePwrItem.cpp

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsNeedleScalePwrItem (14888+), renderNeedleScale (31645-31850)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/NeedleScalePwrItem.h

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsNeedleScalePwrItem (14888+), AddCrossNeedle (22817-23002), renderNeedleScale (31822-31823)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/OtherButtonItem.cpp

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsOtherButtons (8225+)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/OtherButtonItem.h

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsOtherButtons (8225+)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/RotatorItem.cpp

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsRotatorItem (15042+), renderRotator (35170-35569), Update (15290-15312)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/RotatorItem.h

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsRotatorItem (15042+), Update (15290-15312)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/SignalTextItem.cpp

- **Thetis sources cited:**
  - `Project Files/Source/Console/MeterManager.cs` — clsSignalText (20286-20540), Common.UVfromDBM usage, renderSignalText (20420+)
  - `Project Files/Source/Console/console.cs` — dBm format helpers
- **Thetis copyright:**
  - Samphire — MeterManager.cs
  - FlexRadio / Wigley / W2PA / Samphire — console.cs
- **Derivation type:** port
- **Template variant:** multi-source

## src/gui/meters/SignalTextItem.h

- **Thetis sources cited:**
  - `Project Files/Source/Console/MeterManager.cs` — clsSignalText (20286+)
  - `Project Files/Source/Console/console.cs` — format helpers
- **Thetis copyright:** Samphire / multi
- **Derivation type:** port
- **Template variant:** multi-source

## src/gui/meters/SpacerItem.cpp

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsSpacerItem (16116), renderSpacer (35010)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/SpacerItem.h

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsSpacerItem (16116, 16121-16143)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/TextOverlayItem.cpp

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsTextOverlay (18746+), parseText (19267-19395), render (18800+)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/TextOverlayItem.h

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsTextOverlay (18746+), parseText (19267+)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/TuneStepButtonItem.cpp

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — TuneStepList (7999+)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/TuneStepButtonItem.h

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsTunestepButtons (7999+)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/VfoDisplayItem.cpp

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsVfoDisplay (digit handling)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/VfoDisplayItem.h

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsVfoDisplay (12881+)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/VoiceRecordPlayItem.h

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsVoiceRecordPlay (10222+)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/WebImageItem.cpp

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsWebImage (14165+)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/meters/WebImageItem.h

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsWebImage (14165+)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/setup/DisplaySetupPages.cpp

- **Thetis sources cited:**
  - `Project Files/Source/Console/display.cs` — RX1DisplayCalOffset (line 1372)
- **Thetis copyright:** FlexRadio / Wigley W5WC / VK6APH / Samphire (display.cs)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/setup/DisplaySetupPages.h

- **Thetis sources cited:**
  - `Project Files/Source/Console/setup.cs` — Display tab sections
- **Thetis copyright:** FlexRadio / Wigley / Samphire
- **Derivation type:** port (UI mapping)
- **Template variant:** thetis-samphire

## src/gui/setup/DspSetupPages.cpp

- **Thetis sources cited:**
  - `Project Files/Source/Console/setup.cs` — tabDSP / tabPage{AGC,Noise,NoiseBlanker,CW,AMSAM,FM,VOX,CFC,MNF} controls
- **Thetis copyright:** FlexRadio / Wigley / Samphire (setup.cs)
- **Derivation type:** port (UI layout)
- **Template variant:** thetis-samphire

## src/gui/setup/GeneralOptionsPage.cpp

- **Thetis sources cited:**
  - `Project Files/Source/Console/setup.cs` — grpHermesStepAttenuator, groupBoxTS47, chkHermesStepAttenuator, chkAutoATTRx1/2
- **Thetis copyright:** FlexRadio / Wigley / Samphire (setup.cs)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/setup/GeneralOptionsPage.h

- **Thetis sources cited:** `Project Files/Source/Console/setup.cs` — grpHermesStepAttenuator, groupBoxTS47
- **Thetis copyright:** FlexRadio / Wigley / Samphire
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/setup/hardware/Hl2IoBoardTab.cpp

- **mi0bot source cited:** mi0bot `HPSDR/IoBoardHl2.cs` — GPIO_DIRECT_BASE (line 79), I2C address 0x1d (line 139), REG_TX_FREQ_BYTE* (lines 194-198)
- **Copyright (from mi0bot/Thetis-HL2 upstream):**
  - FlexRadio Systems (inherited via Thetis upstream chain)
  - Doug Wigley W5WC (inherited)
  - Richard Samphire MW0LGE (inherited — in ramdor/Thetis)
  - Reid Campbell MI0BOT (fork contributions — this file is mi0bot-specific)
- **Derivation type:** port (stub shell; note says "wraps closed-source I2C register code")
- **Template variant:** mi0bot
- **Notes:** `IoBoardHl2.cs` does NOT exist in `/Users/j.j.boyd/Thetis/` — mi0bot/Thetis-HL2 fork addition. Copyright block not directly extractable.

## src/gui/setup/hardware/Hl2IoBoardTab.h

- **mi0bot source cited:** mi0bot `HPSDR/IoBoardHl2.cs` (line 79 GPIO_DIRECT_BASE, 139 I2C, 194-198 REG_TX_FREQ_BYTE*)
- **Copyright:** as Hl2IoBoardTab.cpp
- **Derivation type:** port
- **Template variant:** mi0bot
- **Notes:** mi0bot fork file, not available locally.

## src/gui/setup/hardware/OcOutputsTab.cpp

- **Thetis sources cited:** `Project Files/Source/Console/setup.cs` (Setup.cs) — UpdateOCBits (lines 12877-12934), chkPenOCrcv/xmit pattern
- **Thetis copyright:** FlexRadio / Wigley / Samphire (setup.cs)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/setup/hardware/XvtrTab.cpp

- **Thetis sources cited:** `Project Files/Source/Console/xvtr.cs` — XVTRForm (lines 47-249)
- **Thetis copyright:** FlexRadio (2004-2009), Doug Wigley (2010-2013), Richard Samphire MW0LGE (dual-license block present)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/setup/TransmitSetupPages.cpp

- **Thetis sources cited:** `Project Files/Source/Console/setup.cs` — PS feedback DDC choices (board-dependent)
- **Thetis copyright:** FlexRadio / Wigley / Samphire
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/setup/TransmitSetupPages.h

- **Thetis sources cited:** `Project Files/Source/Console/setup.cs` — PA/Power tab
- **Thetis copyright:** FlexRadio / Wigley / Samphire
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/gui/SpectrumWidget.cpp

- **Thetis sources cited:**
  - `Project Files/Source/Console/display.cs` — waterfall colour mapping (lines 6826-7075), grid colors (2003, 2069, 2102), data_line_color (2184), waterfall high/low defaults (2522-2536), waterfall row shifting (7719-7729), RX1DisplayCalOffset (1372), band edge (1743-1754)
  - `Project Files/Source/Console/console.cs` — lines 31371-31385
- **Thetis copyright:**
  - FlexRadio / Wigley W5WC / VK6APH / Samphire — display.cs
  - FlexRadio / Wigley / W2PA / Samphire — console.cs
- **Derivation type:** port
- **Template variant:** multi-source

## src/gui/SpectrumWidget.h

- **Thetis sources cited:**
  - `Project Files/Source/Console/enums.cs` — ColorScheme enum (lines 68-79)
  - `Project Files/Source/Console/setup.cs` — setup.designer.cs 34635, comboDisplayLabelAlign, GridColor (1040-1044), ShowRXFilterOnWaterfall (1048-1052), WaterfallUseRX1SpectrumMinMax (7801)
  - `Project Files/Source/Console/display.cs` — band edge (1743-1754, 1941), RX1DisplayCalOffset (1372), waterfall defaults (2522-2536, 6889-6891)
- **Thetis copyright:**
  - Original authors + Samphire — enums.cs
  - FlexRadio / Wigley / Samphire — setup.cs
  - FlexRadio / Wigley W5WC / VK6APH / Samphire — display.cs
- **Derivation type:** port
- **Template variant:** multi-source

## src/gui/widgets/VfoModeContainers.cpp

- **Thetis sources cited:**
  - `Project Files/Source/Console/console.cs` — FM TX simplex (line 40412 chkFMTXSimplex_CheckedChanged)
  - `Project Files/Source/Console/setup.designer.cs` — RTTY mark default (line 40635)
  - `Project Files/Source/Console/radio.cs` — rx_dolly defaults (lines 2043-2044)
- **Thetis copyright:**
  - FlexRadio / Wigley / W2PA / Samphire — console.cs, radio.cs
  - setup.designer.cs inherits from setup.cs: FlexRadio / Wigley / Samphire
- **Derivation type:** port (UI skeleton) + AetherSDR pattern
- **Template variant:** multi-source

## src/gui/widgets/VfoWidget.cpp

- **Thetis sources cited:**
  - `Project Files/Source/Console/console.cs` — filter presets (lines 5180-5575), AGC thresh (line 45977), FM TX handling (27987-28041)
  - `Project Files/Source/Console/console.resx` — many tooltip IDs: 5787, 8277, 8433, 4554, 8397, 4335, 4416, 5631, 4017, 3879, 3927, 348, 303, 4185, 4224, 2028, 1941, 4062, 1560
  - `Project Files/Source/Console/display.cs` — cw_pitch (line 1023)
  - `Project Files/Source/Console/enums.cs` — DSPMode
  - `Project Files/Source/Console/radio.cs` — pan (line 1386), bin (1145), dolly (2043-2044)
  - `Project Files/Source/Console/dsp.cs` — P/Invoke decls (lines 393-394)
  - `Project Files/Source/Console/HPSDR/specHPSDR.cs` — NB2 (line 937)
- **Thetis copyright:**
  - FlexRadio / Wigley / W2PA / Samphire — console.cs, radio.cs, display.cs
  - Original + Samphire — enums.cs
  - NR0V + Samphire — dsp.cs
  - Doug Wigley (2010-2018) only — specHPSDR.cs (no Samphire)
  - console.resx auto-generated (inherits console.cs)
- **Derivation type:** port (comprehensive VFO widget)
- **Template variant:** multi-source

## src/models/Band.h

- **Thetis sources cited:** `Project Files/Source/Console/console.cs` — BandByFreq (line 6443)
- **Thetis copyright:** FlexRadio / Wigley / W2PA / Samphire (console.cs)
- **Derivation type:** port (simplified)
- **Template variant:** thetis-samphire

## src/models/PanadapterModel.cpp

- **Thetis sources cited:** `Project Files/Source/Console/console.cs` — uniform per-band defaults (lines 14242-14436)
- **Thetis copyright:** FlexRadio / Wigley / W2PA / Samphire (console.cs)
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/models/PanadapterModel.h

- **Thetis sources cited:** `Project Files/Source/Console/console.cs` — per-band defaults (14242-14436)
- **Thetis copyright:** FlexRadio / Wigley / W2PA / Samphire
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/models/RadioModel.cpp

- **Thetis sources cited:**
  - `Project Files/Source/Console/console.cs` — UpdateDDCs (8216), band defaults, CW pitch, AGC sync (45960-46006, 45977-45978), SNB (36347), filter presets, FMTXMode (20873), DIGL/DIGU offsets (14637, 14671, 14672)
  - `Project Files/Source/Console/setup.cs` — 17068-17073 (CW pitch), 17203 (dolly)
  - `Project Files/Source/Console/radio.cs` — extensive: AGC (1037-1124), APF (1910-2008), squelch (1185-1329), pan/bin (1386-1403, 1145-1162), NR/NB (2216-2251), rx_dolly (2024-2060, 2043-2044)
  - `Project Files/Source/Console/dsp.cs` — P/Invoke decls (116-120, 393-394)
  - `Project Files/Source/Console/HPSDR/NetworkIO.cs` — SetDDCRate + SetVFOfreq flow
  - `Project Files/Source/ChannelMaster/cmaster.c` — create_nobEXT (lines 55-68)
- **Thetis copyright:**
  - FlexRadio / Wigley / W2PA / Samphire — console.cs, setup.cs, radio.cs
  - NR0V + Samphire dual-license — dsp.cs
  - NetworkIO.cs inherits Thetis blanket
  - Warren Pratt NR0V (2014-2019) only — cmaster.c (no Samphire)
- **Derivation type:** port / integration hub
- **Template variant:** multi-source

## src/models/RadioModel.h

- **Thetis sources cited:** `Project Files/Source/Console/console.cs` — bidirectional sync pattern (45960-46006)
- **Thetis copyright:** FlexRadio / Wigley / W2PA / Samphire
- **Derivation type:** port
- **Template variant:** thetis-samphire

## src/models/SliceModel.cpp

- **Thetis sources cited:**
  - `Project Files/Source/Console/console.cs` — InitFilterPresets (lines 5180-5575), CW pitch band filter (7559-7565), DIGU offset (14636), DIGL offset (14671)
  - `Project Files/Source/Console/display.cs` — cw_pitch value (line 1023)
- **Thetis copyright:**
  - FlexRadio / Wigley / W2PA / Samphire — console.cs
  - FlexRadio / Wigley W5WC / VK6APH / Samphire — display.cs
- **Derivation type:** port
- **Template variant:** multi-source

## src/models/SliceModel.h

- **Thetis sources cited:**
  - `Project Files/Source/Console/console.cs` — InitFilterPresets (5180-5575), RIT, tune_step_list, ctcss_freq (40500), FMTXMode (20873), DIGL/DIGU offsets (14672, 14637)
  - `Project Files/Source/Console/radio.cs` — _fSSqlThreshold (line 1187), amsq threshold (1164-1165), agc defaults (1037-1057, 1107-1108), dolly (2899, 2043-2044)
  - `Project Files/Source/Console/setup.designer.cs` — udDSPRX1DollyF1 (lines 40635-40665)
- **Thetis copyright:**
  - FlexRadio / Wigley / W2PA / Samphire — console.cs, radio.cs
  - setup.designer.cs inherits setup.cs blanket
- **Derivation type:** port
- **Template variant:** multi-source

## tests/fixtures/discovery/p1_angelia_reply.hex

- **mi0bot source cited:** mi0bot `clsRadioDiscovery.cs` — mapP1DeviceType (line 1237), status at 1158, MAC at 1161, CodeVersion at 1185
- **Copyright (from cited source file in ramdor/Thetis upstream `HPSDR/clsRadioDiscovery.cs`):**
  - Richard Samphire MW0LGE (2020-2026) — sole author upstream
  - Reid Campbell MI0BOT — mi0bot fork (assumed)
- **Derivation type:** hand-crafted test fixture, layout verified against source
- **Template variant:** mi0bot
- **Notes:** binary-ish fixture with hex body + comment header. mi0bot source couldn't be directly opened; ramdor/Thetis upstream used for copyright reference. Test fixture is data not code — may not need code header template; flag for Task 8 or 6 to decide.

## tests/fixtures/discovery/p1_hermeslite_reply.hex

- **mi0bot source cited:** mi0bot `clsRadioDiscovery.cs` — parseDiscoveryReply P1 branch (lines 1145-1195)
- **Copyright:** Samphire via ramdor upstream + MI0BOT for fork (inherited)
- **Derivation type:** hand-crafted test fixture
- **Template variant:** mi0bot
- **Notes:** as above — data file, may need alternate handling in Task 8/10.

## tests/fixtures/discovery/p2_saturn_reply.hex

- **mi0bot source cited:** mi0bot `clsRadioDiscovery.cs` — parseDiscoveryReply P2 branch (lines 1201-1226)
- **Copyright:** Samphire via ramdor upstream + MI0BOT for fork (inherited)
- **Derivation type:** hand-crafted test fixture (no live P2 capture)
- **Template variant:** mi0bot
- **Notes:** as above — data file.

## tests/tst_about_dialog.cpp

- **Thetis sources cited:** (indirectly — checks for strings `ramdor/Thetis` and `mi0bot/OpenHPSDR-Thetis` in the About dialog output)
- **Thetis copyright:** n/a — this file verifies AboutDialog rendering.
- **Derivation type:** test (attribution verification)
- **Template variant:** thetis-samphire (by convention — tests a Samphire-acknowledging dialog)
- **Notes:** flagged by grep only because it asserts the contributor strings are present; not a derivation. Retained for template-run completeness.

## tests/tst_dig_rtty_wire.cpp

- **Thetis sources cited:**
  - `Project Files/Source/Console/console.cs` — DIGUClickTuneOffset (14637), DIGLClickTuneOffset (14672)
  - `Project Files/Source/Console/enums.cs` — DSPMode (lines 252-268 — no RTTY in WDSP)
  - `Project Files/Source/Console/radio.cs` — rx_dolly defaults (2043-2044)
  - `Project Files/Source/Console/setup.designer.cs` — RTTY MARK (40635-40637)
- **Thetis copyright:**
  - FlexRadio / Wigley / W2PA / Samphire — console.cs, radio.cs
  - Original + Samphire — enums.cs
- **Derivation type:** test (verifies ported defaults)
- **Template variant:** multi-source

## tests/tst_fm_opt_container_wire.cpp

- **Thetis sources cited:** `Project Files/Source/Console/console.cs` — chkFMTXSimplex_CheckedChanged (line 40412)
- **Thetis copyright:** FlexRadio / Wigley / W2PA / Samphire
- **Derivation type:** test
- **Template variant:** thetis-samphire

## tests/tst_hpsdr_enums.cpp

- **mi0bot / Thetis source cited:** `enums.cs` (line 109), `network.h` (line 446)
- **Thetis copyright:**
  - Original + Samphire — enums.cs
  - Doug Wigley W5WC (2015-2020) — network.h (no Samphire)
- **Derivation type:** test (verbatim integer value parity)
- **Template variant:** multi-source
- **Notes:** comment explicitly says "mi0bot/Thetis@Hermes-Lite enums.cs" — the file exists in ramdor/Thetis proper.

## tests/tst_meter_item_bar.cpp

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsBarItem (19917-20278), renderHBar (35950-36140), addSMeterBar (21499-21616)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** test (parity verification)
- **Template variant:** thetis-samphire

## tests/tst_meter_item_scale.cpp

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — clsScaleItem (14785-15853), ShowType (31879-31886), generalScale (32338+)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** test
- **Template variant:** thetis-samphire

## tests/tst_meter_presets.cpp

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — addSMeterBar (21499-21616), renderScale dispatch (31911-31916)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** test
- **Template variant:** thetis-samphire

## tests/tst_radio_discovery_parse.cpp

- **mi0bot source cited:** mi0bot `clsRadioDiscovery.cs` — P1 parseDiscoveryReply (1145-1195), P2 (1201-1226)
- **Copyright:** Samphire via ramdor upstream + MI0BOT fork (inherited)
- **Derivation type:** test
- **Template variant:** mi0bot
- **Notes:** mi0bot source couldn't be directly opened; ramdor/Thetis upstream used.

## tests/tst_reading_name.cpp

- **Thetis sources cited:** `Project Files/Source/Console/MeterManager.cs` — ReadingName switch (2258-2318)
- **Thetis copyright:** Richard Samphire MW0LGE (2020-2026)
- **Derivation type:** test (verbatim label parity)
- **Template variant:** thetis-samphire

## tests/tst_rxchannel_agc_advanced.cpp

- **Thetis sources cited:** `radio.cs` (1037-1124), `console.cs` (45977), `dsp.cs` (116-117)
- **Thetis copyright:** FlexRadio / Wigley / W2PA / Samphire (radio.cs, console.cs); NR0V + Samphire (dsp.cs)
- **Derivation type:** test (default + idempotency verification)
- **Template variant:** multi-source

## tests/tst_rxchannel_apf.cpp

- **Thetis sources cited:** `radio.cs` (1910-1927)
- **Thetis copyright:** FlexRadio / Wigley / W2PA / Samphire (radio.cs)
- **Derivation type:** test
- **Template variant:** thetis-samphire

## tests/tst_rxchannel_audio_panel.cpp

- **Thetis sources cited:** `dsp.cs` (393-394), `radio.cs` (1386-1403, 1145-1162)
- **Thetis copyright:** NR0V + Samphire — dsp.cs; FlexRadio / Wigley / Samphire — radio.cs
- **Derivation type:** test
- **Template variant:** multi-source

## tests/tst_rxchannel_emnr.cpp

- **Thetis sources cited:** `radio.cs` (2216-2232)
- **Thetis copyright:** FlexRadio / Wigley / Samphire (radio.cs)
- **Derivation type:** test
- **Template variant:** thetis-samphire

## tests/tst_rxchannel_nb2_polish.cpp

- **Thetis sources cited:** `HPSDR/specHPSDR.cs` (922-937), `ChannelMaster/cmaster.c` (55-68)
- **Thetis copyright:**
  - Doug Wigley (2010-2018) only — specHPSDR.cs (no Samphire)
  - Warren Pratt NR0V (2014-2019) — cmaster.c (no Samphire)
- **Derivation type:** test
- **Template variant:** thetis-no-samphire
- **Notes:** rare no-samphire case (both cited sources predate/exclude Samphire).

## tests/tst_rxchannel_snb.cpp

- **Thetis sources cited:** `console.cs` (36347), `dsp.cs` (692-693)
- **Thetis copyright:** FlexRadio / Wigley / Samphire — console.cs; NR0V + Samphire — dsp.cs
- **Derivation type:** test
- **Template variant:** multi-source

## tests/tst_rxchannel_squelch.cpp

- **Thetis sources cited:** `radio.cs` (1185, 1293, 1312)
- **Thetis copyright:** FlexRadio / Wigley / Samphire (radio.cs)
- **Derivation type:** test
- **Template variant:** thetis-samphire

## tests/tst_slice_agc_advanced.cpp

- **Thetis sources cited:** `radio.cs` (1037-1124), `console.cs` (45977)
- **Thetis copyright:** FlexRadio / Wigley / Samphire
- **Derivation type:** test
- **Template variant:** thetis-samphire

## tests/tst_slice_apf.cpp

- **Thetis sources cited:** `radio.cs` (1910-2008)
- **Thetis copyright:** FlexRadio / Wigley / Samphire
- **Derivation type:** test
- **Template variant:** thetis-samphire

## tests/tst_slice_audio_panel.cpp

- **Thetis sources cited:** `dsp.cs` (393-394), `radio.cs` (1386-1403, 1145-1162)
- **Thetis copyright:** NR0V + Samphire (dsp.cs); FlexRadio / Wigley / Samphire (radio.cs)
- **Derivation type:** test
- **Template variant:** multi-source

## tests/tst_slice_emnr.cpp

- **Thetis sources cited:** `radio.cs` (2216-2232)
- **Thetis copyright:** FlexRadio / Wigley / Samphire
- **Derivation type:** test
- **Template variant:** thetis-samphire

## tests/tst_slice_rit_xit.cpp

- **Thetis sources cited:** `console.cs` — RIT behaviour (no line number)
- **Thetis copyright:** FlexRadio / Wigley / W2PA / Samphire
- **Derivation type:** test
- **Template variant:** thetis-samphire

## tests/tst_slice_snb.cpp

- **Thetis sources cited:** `console.cs` (36347), `dsp.cs` (692-693)
- **Thetis copyright:** FlexRadio / Wigley / Samphire — console.cs; NR0V + Samphire — dsp.cs
- **Derivation type:** test
- **Template variant:** multi-source

## tests/tst_slice_squelch.cpp

- **Thetis sources cited:** `radio.cs` (1185-1329, 1164-1178, 1274-1291, 1293-1329)
- **Thetis copyright:** FlexRadio / Wigley / Samphire (radio.cs)
- **Derivation type:** test
- **Template variant:** thetis-samphire

## tests/tst_step_attenuator_controller.cpp

- **Thetis sources cited:** `console.cs` (21359-21382, 21366, 21369, 21373-21375, 21378, 21548-21567)
- **Thetis copyright:** FlexRadio / Wigley / W2PA / Samphire (console.cs)
- **Derivation type:** test
- **Template variant:** thetis-samphire

---

## Verification against Step 1 count

Step 1 grep count: **151**.

Entries above: **151** (one `## src/` or `## tests/` section per file).

Independent heading-count check can be done by running
`grep -cE '^## (src|tests)/' docs/attribution/audit-outbound.md` against
the 151-line file list in `/tmp/nereus-outbound.txt`.

## Files flagged "couldn't be directly verified"

All mi0bot-variant files — the mi0bot/Thetis-HL2 fork is not cloned
locally, so the per-file copyright blocks could not be extracted from
mi0bot itself. Copyright lineage inferred from the ramdor/Thetis
upstream `HPSDR/clsRadioDiscovery.cs` (Samphire) plus the Reid Campbell
MI0BOT contribution noted in the NereusSDR source comments and on the
fork's GitHub repo. The files are:

- `src/core/RadioDiscovery.cpp`
- `src/core/RadioDiscovery.h`
- `src/gui/setup/hardware/Hl2IoBoardTab.cpp`
- `src/gui/setup/hardware/Hl2IoBoardTab.h`
- `tests/fixtures/discovery/p1_angelia_reply.hex`
- `tests/fixtures/discovery/p1_hermeslite_reply.hex`
- `tests/fixtures/discovery/p2_saturn_reply.hex`
- `tests/tst_radio_discovery_parse.cpp`

`src/core/WdspTypes.h` cites `wdsp.cs` which is also a mi0bot-only
filename (does not exist in ramdor/Thetis); the values referenced are
canonical WDSP enums and would match ramdor `dsp.cs` `DSPMode` in
practice. Classified as multi-source.

`src/core/HpsdrModel.h` and `tests/tst_hpsdr_enums.cpp` use the prefix
"mi0bot/Thetis@Hermes-Lite" but the cited files exist in ramdor/Thetis
proper at the same paths — the comments are author viewpoint, not a
mi0bot-specific derivation. Classified as multi-source (combines the
Samphire-heavy `enums.cs` with the Samphire-clean `network.h`).
