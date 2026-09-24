# Remote accessory control version 1

This is the wire contract for R-R3-47 and R-R3-22: the Power Genius XL
(PGXL), the Tuner Genius XL (TGXL) and the RF-Kit RF2K-S talk to the Core
only, and every window (the desktop remote window, the planned iPhone app, a
local window in-process) reads the same Core objects and asks the Core to act
through the same commands. It sits beside
[remote media control version 1](2026-09-20-remote-media-control-v1.md) and
[remote notch control version 1](2026-09-23-remote-notch-control-v1.md), and
uses the existing session envelope (schema, object snapshots, property
deltas, `property.write` / `property.result`, `command.invoke` /
`command.result`).

The plan that builds it is
[2026-09-23-r3-core-owned-accessories-plan.md](2026-09-23-r3-core-owned-accessories-plan.md).
Each of its tasks extends this document in the same commit that adds what it
describes. This revision covers Task 1: the read-only `amplifier` and `rfkit`
status objects, plus everything the Core already serves for its accessories
(the `tuner` object, the 4O3A fields, and the TGXL and 4O3A commands).

## Wire conventions

A non-Qt client needs only these rules.

- Every mirrored object is announced once per class by a `schema` message
  (`{"type":"schema","class":...,"fields":[{"name","kind","ordinal"}]}`),
  then sent whole by `object.create` (`{"type":"object.create","key",
  "class","properties":[{"name","kind","ordinal","value"}]}`), and changed
  by `delta` (`{"type":"delta","key","properties":[...]}`). The first burst
  ends with `{"type":"snapshot.complete"}`.
- Match properties by `name`. The `ordinal` is the property's position in
  its class for this build and is not stable across builds.
- Wire kinds: `bool`; `i64` (a whole number); `f64` (a number; values the
  Core reads as 32-bit floats arrive with their float rounding, for example
  1.399999976158142 for 1.4); `utf8` (text); `enum` (a whole number from a
  fixed table in this document).
- Enum values are fixed. New values are only appended. A client that meets a
  value it does not know ignores that one value and keeps the one it had
  (the desktop app refuses it and logs it), and keeps working.
- A client drops, without error, an object key or property name it does not
  know. That is how older apps survive newer Cores.
- A delta carries every property that shares one change notice on the Core,
  so it may repeat values that did not change. Apply each as a plain value.
- Units are in the property name where there is one: `W` watts, `C` degrees
  Celsius, `V` volts, `A` amperes. `swr` is a ratio (1.0 is a perfect match).

## Negotiation

Capabilities arrive in the `capabilities` message after authentication. The
session protocol minor (`kSessionProtocolMinor`) does not change for this
contract; each feature has its own version.

| Capability | Session minor | Value | Meaning |
| --- | --- | --- | --- |
| `remoteTgxlConfigVersion` | 4 or later | 1 | The Core owns the TGXL: `configureTgxl` and `disconnectTgxl` work |
| `remoteFourO3AControlVersion` | 4 or later | 1 | `setFourO3AEnabled` works |
| `remotePgxlControlVersion` | 11 | 1 | The Core mirrors its PGXL as the read-only `amplifier` object |
| `remoteRfKitControlVersion` | 11 | 1 | The Core mirrors its RF2K-S as the read-only `rfkit` object |

- The Core advertises all four as 1 when it owns its accessories (the
  headless Core, `nereusd`, always does) and 0 otherwise.
- `remotePgxlControlVersion` and `remoteRfKitControlVersion` travel last in
  the minor-11 block of the capabilities message, after `hpsdrModel`,
  `radioProtocol`, `radioAddress` and `radioHardwareVersion`, and only to an
  app that agreed minor 11. An app below minor 11 receives exactly the
  capabilities, objects and deltas it received before: neither entry, and no
  `amplifier` or `rfkit` schema, object or delta.
- An app that sees version 0, or no entry, shows no Power Genius or RF-Kit
  readings from this Core and says so (see Window behaviour).
- Later tasks of this plan raise these versions and add
  `stationTciVersion` (the Core's own TCI server) and `accessoryDataVersion`
  (fault history, interlock policy, tune memory). Their sections are added
  here when they are built.

## Connection phase

`tuner`, `amplifier` and `rfkit` share one connection-state shape and one
phase table, `connectionPhase`:

| Value | Name | Meaning |
| --- | --- | --- |
| 0 | `disabled` | The station has this accessory switched off |
| 1 | `disconnected` | Switched on, not connected, no attempt running |
| 2 | `discovering` | Waiting for the device's LAN discovery announcement |
| 3 | `connecting` | Opening the connection |
| 4 | `identifying` | Connected, checking the device is what was configured |
| 5 | `retrying` | Lost or refused; the Core will try again by itself |
| 6 | `connected` | Admitted; readings are live |
| 7 | `error` | Stopped with a reason in `connectionError`; no retry |

```
disabled --switch on--> disconnected --configure--> discovering --> connecting
    --> identifying --> connected
connected --drop--> retrying --> connecting ...      (automatic retry on)
connected --drop--> disconnected                     (automatic retry off)
any --failure--> error ; any --switch off--> disabled ; any --disconnect--> disconnected
```

Which phases each device reports today:

- TGXL: all eight. `discovering` and `identifying` are the Core's identity
  check (a TunerGenius or TunerGeniusXL discovery announcement from the same
  address and port, and the same serial in the tuner's own info reply).
- PGXL: `disabled`, `disconnected`, `retrying`, `connected`, `error`. The
  Core's identity check for the PGXL (Task 2) adds `discovering`,
  `connecting` and `identifying`.
- RF2K-S: `disabled`, `disconnected`, `connected`, `error`. Its identity
  check (Task 3) adds the rest.

**Accepted is not connected.** A command's `command.result` with
`accepted: true` means the Core took the request. Whether the device is now
connected is only ever said by `connectionPhase` becoming `connected`. A
window shows the request as pending until the phase moves, and shows
`connectionError` in user words if it ends at `error` or `retrying`.

## The `tuner` object (TGXL)

Class `TunerModel`, key `tuner`. Sent to every app. Every property is the
Core's to report.

| Property | Kind | Meaning |
| --- | --- | --- |
| `connectionPhase` | enum | Connection phase (table above) |
| `configuredHost` | utf8 | The address the Core dials |
| `configuredPort` | i64 | Its TCP port (9010 by default) |
| `connectionError` | utf8 | Why the last attempt failed; empty otherwise. Diagnostic text, shown in user words |
| `deviceModel` | utf8 | The model the device reported |
| `deviceSerial` | utf8 | Its serial number |
| `deviceVersion` | utf8 | Its firmware version |
| `deviceNickname` | utf8 | Its nickname |
| `relayC1`, `relayL`, `relayC2` | i64 | Matching network relay positions, 0 to 255 |
| `isOperate` | bool | In operate (not standby) |
| `isBypass` | bool | Bypassed |
| `isTuning` | bool | A tune cycle is running |
| `antennaA` | i64 | Selected antenna on a 3x1 tuner, as the tuner numbers it |
| `hasAntennaSwitch` | bool | The tuner is a 3x1 with an antenna switch |
| `isPresent` | bool | Admitted and reporting |
| `hasDirectConnection` | bool | Connected to the Core |
| `tgxlIp` | utf8 | The tuner's address while connected |
| `fwdPower` | f64 | Forward power through the tuner, W |
| `swr` | f64 | SWR ratio at the tuner |

Off `connected`, the Core clears the relays, operate, bypass, tuning,
antenna and meters (1.0 SWR, 0 W).

## The `amplifier` object (PGXL)

Class `AmplifierModel`, key `amplifier`, `remotePgxlControlVersion` 1.
Read-only: every property is the Core's to report.

| Property | Kind | Meaning |
| --- | --- | --- |
| `connectionPhase` | enum | Connection phase (table above) |
| `configuredHost` | utf8 | The address the Core last dialled |
| `configuredPort` | i64 | Its TCP port (9008 by default) |
| `connectionError` | utf8 | Why the last attempt failed; empty otherwise |
| `deviceModel` | utf8 | Empty until the Core's identity check (Task 2) |
| `deviceSerial` | utf8 | Empty until the Core's identity check (Task 2) |
| `deviceVersion` | utf8 | The version in the amp's connect banner, for example `3.8.9` |
| `deviceNickname` | utf8 | Empty until Task 2 |
| `present` | bool | The Core has a reading from the amp on the current connection. False: the values below are the last ones read and are not live |
| `state` | enum | The amp's state (table below) |
| `deviceState` | utf8 | The amp's own state word, as sent |
| `operate` | bool | Operating: `state` is `idle`, `operate`, `transmitA` or `transmitB` |
| `transmitting` | bool | Keyed: `state` is `transmitA` or `transmitB` |
| `forwardPowerW` | f64 | Peak forward power while transmitting, W; 0 otherwise |
| `swr` | f64 | SWR ratio while transmitting, capped at 99; 1.0 otherwise |
| `temperatureC` | f64 | Heat-sink temperature, degrees C |
| `mainsVoltageV` | f64 | Mains voltage, V |
| `drainCurrentA` | f64 | Drain current, A |
| `efficiencyText` | utf8 | The amp's efficiency label (MEffA), as sent, for example `off` |

`state`:

| Value | Name | The amp's word |
| --- | --- | --- |
| 0 | `unknown` | None yet, or a word this Core does not know |
| 1 | `powerUp` | `POWERUP` |
| 2 | `standby` | `STANDBY` |
| 3 | `idle` | `IDLE` (operating, ready to transmit) |
| 4 | `operate` | `OPERATE` |
| 5 | `transmitA` | `TRANSMIT_A` |
| 6 | `transmitB` | `TRANSMIT_B` |
| 7 | `fault` | Any word beginning `FAULT` |

### How the Core reads the PGXL

The Core converts the amp's status lines with one function
(`applyPgxlStatus`, `src/core/PgxlStatusGauges.cpp`); a local window runs the
same function in-process. A status line's key it does not carry keeps its
last value.

| Amp key | Unit on the amp | Property |
| --- | --- | --- |
| `peakfwd` | dBm | `forwardPowerW` = 10^(dBm/10) / 1000 |
| `swr` | signed dB return loss (negative on a good match) | `swr`: with G = 10^(RL/20), (1 + G) / (1 - G); 99 when RL >= 0 dB or G >= 0.999. -24.5 dB is 1.13 |
| `temp` | degrees C | `temperatureC` |
| `vac` | V | `mainsVoltageV` |
| `id` | A | `drainCurrentA` |
| `state` | word | `deviceState`, `state`, `operate`, `transmitting` |
| `meffa` | label | `efficiencyText` |

`peakfwd` and `swr` are hold values on the amp: they keep the last transmit
peak. The Core takes them only while `transmitting`, and a state line
outside transmit sets `forwardPowerW` to 0 and `swr` to 1.0.

## The `rfkit` object (RF2K-S)

Class `RfKitModel`, key `rfkit`, `remoteRfKitControlVersion` 1. Read-only.

| Property | Kind | Meaning |
| --- | --- | --- |
| `connectionPhase` | enum | Connection phase (table above) |
| `configuredHost` | utf8 | The amp's address |
| `configuredPort` | i64 | Its HTTP port (8080 by default) |
| `connectionError` | utf8 | Why the last attempt failed; empty otherwise |
| `deviceModel` | utf8 | The amp's `device` field from `/info`, for example `RF2K-S` |
| `deviceSerial` | utf8 | Empty; the amp's `/info` carries none |
| `deviceVersion` | utf8 | `G<GUI>C<controller>` from `/info`, for example `G200C267` |
| `deviceNickname` | utf8 | The amp's `custom_device_name` |
| `present` | bool | The Core has a `/power` reading on the current connection. False: the values below are not live |
| `operate` | bool | `/operate-mode` is `OPERATE` |
| `forwardPowerW` | f64 | `/power` forward, W |
| `reflectedPowerW` | f64 | `/power` reflected, W |
| `swr` | f64 | `/power` SWR ratio |
| `temperatureC` | f64 | `/power` temperature, degrees C |
| `voltageV` | f64 | `/power` supply voltage, V |
| `currentA` | f64 | `/power` current, A |

The RF2K-S antenna and tuner reports are not mirrored in version 1.

## The 4O3A and RF-Kit fields on the `radio` object

Class `RadioModel`, key `radio`, sent to every app.

| Property | Kind | Direction | Meaning |
| --- | --- | --- | --- |
| `fourO3AEnabled` | bool | Core to window | The station's 4O3A switch (PGXL, TGXL and the SmartSDR listener) for its radio |
| `fourO3AListening` | bool | Core to window | The Core's SmartSDR listener (TCP 4992) is accepting connections |
| `fourO3AListenerError` | utf8 | Core to window | Why the listener could not start; empty otherwise |
| `rfKitEnabled` | bool | both ways | The station's RF-Kit switch for its radio. Today a raw two-way value; Task 3 makes it Core to window with a `setRfKitEnabled` command |

## Commands

Each is a `command.invoke` with exactly the arguments shown, in exactly
these wire kinds. The answer is a `command.result`; `accepted: true` means
the Core took the request (see "Accepted is not connected").

| Verb | Arguments | Needs | Effect |
| --- | --- | --- | --- |
| `configureTgxl` | `host` (utf8), `port` (i64, 1 to 65535) | minor 4, `remoteTgxlConfigVersion` 1 | Saves the TGXL address for the Core's radio and starts connecting |
| `disconnectTgxl` | none | minor 4, `remoteTgxlConfigVersion` 1 | Cancels an attempt or closes the connection; the address stays |
| `setFourO3AEnabled` | `enabled` (bool) | minor 4, `remoteFourO3AControlVersion` 1 | Turns the station's 4O3A switch on or off for its radio |

The PGXL and RF-Kit verbs of this plan (connect, disconnect and configure
for each, the RF-Kit switch) are added here by Tasks 2 and 3.

## Refusals

A refusal is a `command.result` with `accepted: false`, or a
`property.result` with `accepted: false`, and a `reason`. There are no
numeric codes: the reason text is the identifier. Its exact wording is kept
from release to release, because older apps compare some of it exactly; an
app shows it through its own user-word translation and keeps the raw text in
its log.

Property writes:

| Write | Reason |
| --- | --- |
| Any property of `amplifier` | "The Core reports the amplifier's readings. They cannot be changed from this app." |
| Any property of `rfkit` | "The Core reports the RF-Kit amplifier's readings. They cannot be changed from this app." |
| `fourO3AEnabled`, `fourO3AListening`, `fourO3AListenerError` on `radio` | Refused with a diagnostic reason; use `setFourO3AEnabled` |
| `tuner` telemetry (everything except the three below) | "TunerModel::<name> is hardware telemetry TunerModel only learns from the tuner itself; there is no remote-write path" |
| `tuner` `isOperate`, `isBypass`, `antennaA` | Not refused today: see the known gap under "What waits for remote transmit" |

Commands:

| Verb | Reason |
| --- | --- |
| `configureTgxl`, `disconnectTgxl` below minor 4 | "Remote TGXL configuration requires a newer station protocol." |
| `setFourO3AEnabled` below minor 4 | "Remote 4O3A control requires a newer station protocol." |
| `configureTgxl` with other arguments | "invalid host or port argument" |
| `disconnectTgxl` with arguments | "disconnectTgxl takes no arguments" |
| `setFourO3AEnabled` with other arguments | "setFourO3AEnabled requires exactly one enabled boolean argument" |
| `configureTgxl`, `disconnectTgxl` on a Core that does not own its accessories | "Station accessory configuration is unavailable." |
| `configureTgxl` with no radio | "Connect Core to a radio before configuring its TGXL." |
| `configureTgxl` with 4O3A off | "Enable 4O3A on Core before connecting the TGXL." |
| `configureTgxl` with a bad address | "Enter a valid TGXL IP address or hostname and TCP port 1–65535." |
| `setFourO3AEnabled` with no radio | "Connect Core to a radio before changing its 4O3A integration." |
| Any refusal without its own reason | "TGXL configuration was refused", "TGXL disconnect was refused", "4O3A master change was refused" |

The desktop app does not send a command its Core did not offer. It shows
"The station does not support remote TGXL configuration." or "The station
does not support remote 4O3A control." instead.

## Core-owned settings

The Core keeps every accessory setting. A window changes one only through a
command above (or, for the settings not yet behind a command, not at all
from a remote window). Keys are the Core's `NereusSDR.settings` keys.

Per radio, under `hardware/<mac>/peripherals/`:

| Key | Value | Changed by |
| --- | --- | --- |
| `FourO3A_Enabled` | `True` / `False` | `setFourO3AEnabled` |
| `TGXL_ManualIp`, `TGXL_ManualPort` | text, whole number | `configureTgxl` |
| `PGXL_ManualIp`, `PGXL_ManualPort` | text, whole number | Task 2 |
| `RfKit_Enabled` | `True` / `False` | `rfKitEnabled` today; `setRfKitEnabled` from Task 3 |
| `RfKit_ManualIp`, `RfKit_ManualPort` | text, whole number | Task 3 |

Station-wide, not yet behind a command: `PGXL_AutoReconnect`,
`PGXL_KeepaliveSec`, `PGXL_BroadcastDiscovery`, `PGXL_BroadcastNickname`,
`PGXL_FlexRadioSerial`, `PGXL_FlexAmpSlice`, `PGXL_TxAnt`, `PGXL_AntMap`,
`PGXL_PairModel`, `PGXL_DiscoveryModel`, `PGXL_Nickname`, `PGXL_FanMode`,
`PGXL_BiasMode`, `PGXL_LedIntensity`, `PGXL_PowerCapEnabled`,
`PGXL_PowerCapW`, `PGXL_TxInterlockMode` (`Disabled`, `Warn`, `Block`),
`PGXL_TxInterlockGraceMs`, `PGXL_TxSwrGate`, `PGXL_TxSwrGateMax`,
`TGXL_AutoReconnect`, `TGXL_KeepaliveSec`, `TGXL_Nickname`,
`TGXL_AutoTuneMemoryRecall`, `TGXL_Ant1_Label` to `TGXL_Ant3_Label`,
`TGXL_TuneMemory_Ant<N>_Band<B>`, `RfKit_AutoReconnect`,
`RfKit_PollIntervalMs`, `RfKit_Ant1_Label` to `RfKit_Ant4_Label`,
`PGXL_FaultHistory`, `TGXL_FaultHistory`. Tasks 2 to 4 put the connection,
interlock, fault, label and tune-memory settings behind commands and
mirrored objects.

## The fault record

The Core records each PGXL fault edge (a state word beginning `FAULT` after
one that did not). The TGXL and RF2K-S are added by Task 4. Each device keeps
its last 10 faults, newest first, in its settings key (`PGXL_FaultHistory`,
`TGXL_FaultHistory`) as a compact JSON array of:

```json
{"whenMs":1790000000000,"state":"FAULT","fwdAtFaultW":1000.0,"swrAtFault":1.13,"tempAtFaultC":78.0,"likelyCause":"Unknown"}
```

| Field | Meaning |
| --- | --- |
| `whenMs` | When the Core saw it, milliseconds since 1970 UTC |
| `state` | The amp's word, for example `FAULT` |
| `fwdAtFaultW` | Forward power at the fault, W (from the amp's `fwd`, dBm) |
| `swrAtFault` | SWR ratio at the fault (from the amp's return loss) |
| `tempAtFaultC` | Temperature at the fault, degrees C |
| `likelyCause` | `SWR trip`, `Overtemp`, `Drive too high` or `Unknown` |

Version 1 does not mirror the list; a window reads it only through the
settings snapshot at connect. Task 4 mirrors it with a revision under
`accessoryDataVersion`.

## What waits for remote transmit

Nothing in this contract keys a transmitter, operates an amplifier, starts a
tune carrier or changes a relay or antenna on a transmit path. Until remote
transmit, the Core refuses or does not offer:

- PGXL OPERATE and STANDBY, and PGXL standby around a TGXL tune.
- TGXL TUNE (autotune), operate, bypass, antenna choice, relay nudges, and
  tune-memory recall on a band change.
- RF2K-S OPERATE and STANDBY, antenna choice, operational interface, error
  reset.
- Enforcement of the interlock policy, and the MOX RF-flow gate (these stay
  on the Core; only viewing and changing the policy comes before remote
  transmit, in Task 4).

Known gap: a `property.write` of `tuner` `isOperate`, `isBypass` or
`antennaA` still reaches the Core's tuner (`TunerModel::applyMirroredValue`,
an R2 path), whatever the receive-only policy. The desktop app never sends
one (its tuner controls are behind transmit permission). Until the Core
refuses these writes, a client must not send them.

A remote window shows these controls disabled with the reason "Amplifier
control is not available from a remote window yet." (Power Genius and
RF-Kit) or its transmit-permission reason (Tuner Genius).

## Window behaviour

A window reads `amplifier` and `rfkit` only while the Core offers them:

- Filled on attach from the `object.create` values, and updated by every
  delta. `false`, `0` and `standby` are values and are shown as such.
- The desktop applets show the gauges once `present` has been true, and keep
  the last values when it goes false.
- When the link to the Core is lost, the last values stay and a line says
  "Core disconnected. Power Genius readings are stale." (or "RF-Kit
  readings"). On a Core that does not offer the object: "This Core does not
  report its Power Genius to this app. Updating the Core may help." (or
  "its RF-Kit amplifier").
- A remote window never opens a connection to an accessory. A local window
  reads the same objects, fed by its own connections, and shows no stale
  line.

## Fixtures

`tests/fixtures/accessories/` holds files a client's conformance suite can
load:

- `amplifier.jsonl` and `rfkit.jsonl`: one session message per line, as the
  Core sends them: the `schema`, the `object.create` after the captured
  readings (PGXL `R1|0|state=OPERATE temp=42.5 vac=240 fwd=1480.0 swr=2.1`
  and the setup reply with `meffa=off`; RF2K-S `/info`, `/power` and
  `/operate-mode` replies), `snapshot.complete`, then deltas (PGXL
  transmitting at 60 dBm with -24.5 dB return loss, then standby; RF2K-S
  standby with an idle `/power`).
- `enums.json`: the `connectionPhase` and `amplifierState` tables and the
  two read-only refusal texts.

`tst_station_accessory_state` regenerates the two `.jsonl` files from the
objects and fails if they differ, parses them, applies them to a window's
objects, and checks `enums.json` against the enums. After a deliberate
contract change, run it once with `NEREUS_WRITE_ACCESSORY_FIXTURES=1` to
rewrite the fixtures, and update this document in the same commit.

## Evidence

- `tst_station_accessory_state`: the conversion from captured lines, the
  connection phases over a loopback socket, the RF2K-S replies, what a
  current, an older and a non-owning Core sends, the read-only refusals, and
  the fixtures.
- `tst_remote_peripherals`: a remote window's Power Genius and RF-Kit applets
  over the in-process loopback (filled on attach, updated, false and zero
  shown, stale on Core loss, no accessory connection opened), the older-Core
  line, and a local window showing the same values as before through the
  moved conversion.
- `tst_station_session`, `tst_display_budget_contract`: the capability
  entries' place, and an older app's capabilities byte for byte.

Hardware evidence is pending for the operator checkpoint: readings from the
real PGXL and RF2K-S reaching a remote window and the iPhone app.
